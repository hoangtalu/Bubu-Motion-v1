// Menu system ported from bubu_ota in stages.
// This pass ports the CARE menu and the STATS arc, backed by CareSystem.

#include "menu_system.h"

#include "care_system.h"
#include "checker_game.h"
#include "audio/audio_codec.h"
#include "assets.h"
#include "board.h"
#include "boards/common/wifi_board.h"
#include "boards/common/wifi_connect_service.h"
#include "eye_display.h"
#include "green_eye_game.h"
#include "fortune_system.h"
#include "level_system.h"
#include "notes_system.h"
#include "pomodoro_timer.h"
#include "quick_tap_game.h"
#include "reminder_system.h"
#include "snake_game.h"
#include "tilt_maze_game.h"
#include "traffic_runner_game.h"
#include "lvgl_display/gif/lvgl_gif.h"
#include "lvgl_display/lvgl_image.h"
#include "lvgl_display/lvgl_theme.h"
#include "settings.h"
#include "assets/lang_config.h"
#include "audio/vox.h"
#include <font_awesome.h>
#include "display/display.h"
#include "application.h"
#include <ssid_manager.h>

#include <array>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

#include <esp_log.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <esp_system.h>
#include <lvgl.h>

#define TAG "MenuSystem"

namespace {
// Every finished game makes Bubu happier. Care stats commit to NVS (a level-up
// saves immediately), so the write goes to the main task, as the games' other
// end-of-round saves do.
void RewardGameMood(const char* game, int amount) {
    ESP_LOGI(TAG, "Game reward: %s +%d mood", game, amount);
    Application::GetInstance().Schedule([amount]() { CareSystem::AddMood(amount); });
}
}  // namespace

extern const lv_font_t lv_font_montserrat_vn_20;
extern const lv_font_t lv_font_montserrat_vn_22;
extern const lv_font_t lv_font_montserrat_vn_28;
extern const lv_font_t font_awesome_20_4;

namespace {

enum CareItem {
    CARE_FEED,
    CARE_PLAY,
    CARE_CLEAN,
    CARE_SLEEP,
    CARE_STATS,
    CARE_LEVEL,
    CARE_ITEM_COUNT
};

enum ConnectItem {
    // Order here IS the on-screen order, and connectItemLabelTexts below is
    // indexed by it -- change one without the other and the labels swap actions.
    // On-device first because it is the path parents actually use: it needs
    // nothing but the toy, while the phone route means joining another WiFi and
    // opening 192.168.4.1.
    CONNECT_DEVICE,
    CONNECT_PHONE,
    CONNECT_ITEM_COUNT
};

enum ConnectView {
    CONNECT_VIEW_METHODS,
    CONNECT_VIEW_WIFI_LIST,
    CONNECT_VIEW_PHONE_HELP
};

enum SettingsItem {
    SETTINGS_VOLUME,
    SETTINGS_ITEM_COUNT
};

enum GameSelection {
    GAME_SELECTION_GREEN_EYE,
    GAME_SELECTION_CHECKER,
    GAME_SELECTION_QUICK_TAP,
    GAME_SELECTION_SNAKE,
    GAME_SELECTION_TILT_MAZE,
    GAME_SELECTION_TRAFFIC_RUNNER,
    GAME_SELECTION_COUNT
};

enum ActiveGameType {
    ACTIVE_GAME_NONE,
    ACTIVE_GAME_GREEN_EYE,
    ACTIVE_GAME_CHECKER,
    ACTIVE_GAME_QUICK_TAP,
    ACTIVE_GAME_SNAKE,
    ACTIVE_GAME_TILT_MAZE,
    ACTIVE_GAME_TRAFFIC_RUNNER
};

constexpr size_t STAT_COUNT = 4;

// Navigation state. Never assign this directly -- go through SetMenuState() so
// the active screen is published and the render policy follows immediately.
MenuState currentState = MENU_CLOSED;
MenuItem selectedItem = MENU_CARE;
CareItem selectedCareItem = CARE_FEED;
ConnectItem selectedConnectItem = CONNECT_DEVICE;
ConnectView connectView = CONNECT_VIEW_METHODS;
SettingsItem selectedSettingsItem = SETTINGS_VOLUME;
size_t selectedWifiIndex = 0;
size_t statIndex = 0;
bool statsOpenedFromCare = false;
Display* displayHandle = nullptr;

constexpr uint32_t COLOR_BACKGROUND = 0x050812;
constexpr uint32_t COLOR_MINT = 0x58F5C9;
constexpr uint32_t COLOR_TEXT = 0xFFFFFF;

lv_obj_t* menuPanel = nullptr;
bool menuRollerCreated = false;
lv_obj_t* menuHeroImage = nullptr; // single full-screen image, swapped as selection changes
lv_obj_t* menuHeroLabel = nullptr; // fallback caption, shown for items with no image yet
lv_obj_t* menuItems[MENU_ITEM_COUNT] = {nullptr}; // hit-test target; only [selectedItem] is kept live
bool menuItemIsIcon[MENU_ITEM_COUNT] = {false};
std::unique_ptr<LvglRawImage> menuItemIcons[MENU_ITEM_COUNT];
lv_obj_t* upButton = nullptr;
lv_obj_t* downButton = nullptr;

lv_obj_t* carePanel = nullptr;
lv_obj_t* careHeroImage = nullptr; // single full-screen image, swapped as selection changes
lv_obj_t* careHeroLabel = nullptr; // fallback caption, shown for items with no image yet
lv_obj_t* careItems[CARE_ITEM_COUNT] = {nullptr}; // hit-test target; only [selectedCareItem] is kept live
bool careItemIsIcon[CARE_ITEM_COUNT] = {false};
std::unique_ptr<LvglRawImage> careItemIcons[CARE_ITEM_COUNT];
lv_obj_t* careUpButton = nullptr;
lv_obj_t* careDownButton = nullptr;

lv_obj_t* connectPanel = nullptr;
lv_obj_t* connectList = nullptr;
lv_obj_t* connectTitle = nullptr;
lv_obj_t* connectHint = nullptr;
lv_obj_t* connectItems[CONNECT_ITEM_COUNT] = {nullptr};
lv_obj_t* connectUpButton = nullptr;
lv_obj_t* connectDownButton = nullptr;
lv_obj_t* connectWifiList = nullptr;

lv_obj_t* keyboardPanel = nullptr;
lv_obj_t* keyboardSsid = nullptr;
lv_obj_t* keyboardValue = nullptr;
lv_obj_t* keyboardGrid = nullptr;
std::array<lv_obj_t*, 13> keyboardButtons = {};

lv_obj_t* settingsPanel = nullptr;
lv_obj_t* settingsTitle = nullptr;
lv_obj_t* settingsItems[SETTINGS_ITEM_COUNT] = {nullptr};

lv_obj_t* notesPanel = nullptr;
lv_obj_t* notesTitle = nullptr;
lv_obj_t* notesList = nullptr;
lv_obj_t* notesUpButton = nullptr;
lv_obj_t* notesDownButton = nullptr;
std::vector<lv_obj_t*> notesItems;
std::vector<NotesSystem::NoteEntry> notesEntries;
size_t selectedNoteIndex = 0;

lv_obj_t* noteDetailPanel = nullptr;
lv_obj_t* noteDetailTitle = nullptr;
lv_obj_t* noteDetailValue = nullptr;
lv_obj_t* noteDetailHint = nullptr;

lv_obj_t* remindersPanel = nullptr;
lv_obj_t* remindersTitle = nullptr;
lv_obj_t* remindersList = nullptr;
lv_obj_t* remindersUpButton = nullptr;
lv_obj_t* remindersDownButton = nullptr;
std::vector<lv_obj_t*> remindersItems;
std::vector<ReminderSystem::Reminder> remindersEntries;
size_t selectedReminderIndex = 0;

lv_obj_t* reminderDetailPanel = nullptr;
lv_obj_t* reminderDetailTitle = nullptr;
lv_obj_t* reminderDetailValue = nullptr;
lv_obj_t* reminderDetailHint = nullptr;

lv_obj_t* volumePanel = nullptr;
lv_obj_t* volumeTitle = nullptr;
lv_obj_t* volumeValueLabel = nullptr;
lv_obj_t* volumeMinusButton = nullptr;
lv_obj_t* volumePlusButton = nullptr;
lv_obj_t* volumeBackButton = nullptr;
int volumeWorkingValue = 0;
bool volumeDirty = false;


lv_obj_t* statsPanel = nullptr;
lv_obj_t* statsArc = nullptr;
lv_obj_t* statsTitle = nullptr;
lv_obj_t* statsLeftBtn = nullptr;
lv_obj_t* statsRightBtn = nullptr;
lv_obj_t* statsActionZone = nullptr;
lv_obj_t* gamesPanel = nullptr;
lv_obj_t* gamesRing = nullptr;      // carousel position, one third lit
lv_obj_t* gamesEmblem = nullptr;    // the disc under the centre tap zone
lv_obj_t* gamesAction = nullptr;    // game name
lv_obj_t* gamesStatus = nullptr;    // stat line under the name
// One drawn mark per game, parented to gamesEmblem; only the selected one is
// ever visible.
lv_obj_t* gamesEmblemMarks[GAME_SELECTION_COUNT] = {nullptr};
lv_obj_t* levelPanel = nullptr;
lv_obj_t* levelArc = nullptr;
lv_obj_t* levelTitle = nullptr;
lv_obj_t* transientOverlay = nullptr;
lv_obj_t* transientImage = nullptr;
std::unique_ptr<LvglGif> transientGifController = nullptr;
std::unique_ptr<LvglRawImage> transientRawImage = nullptr;
esp_timer_handle_t transientTimer = nullptr;
bool transientAnimationActive = false;
enum class TransientAnimationType {
    NONE,
    LEVEL_UP,
};
TransientAnimationType transientAnimationType = TransientAnimationType::NONE;
bool levelOpenedFromCare = false;
bool gamesOpenedFromCare = false;
CareItem gamesReturnCareItem = CARE_STATS;
GameSelection selectedGame = GAME_SELECTION_GREEN_EYE;
ActiveGameType activeGame = ACTIVE_GAME_NONE;
lv_obj_t* gamesPrevBtn = nullptr;
lv_obj_t* gamesNextBtn = nullptr;
// "ĐANG TẢI..." shown while a game's screens are being built.
lv_obj_t* gamesLoadingLabel = nullptr;
lv_obj_t* checkerGrid = nullptr;
std::array<lv_obj_t*, 9> checkerCellButtons = {};
// Screens of the one checker game -- not separate games.
enum class CheckerScreen : uint8_t {
    kTitle,
    kMatchup,
    kBoard,
    kResult,
    kPlayAgain,
};

constexpr uint32_t kCheckerMatchupHoldMs = 1200;
constexpr int kCheckerRecordSlots = 3;
constexpr int kCheckerParticleCount = 10;

std::array<lv_obj_t*, 9> checkerXMarks = {};
std::array<lv_obj_t*, 9> checkerOMarks = {};
lv_obj_t* checkerBotIcon = nullptr;
int8_t checkerLastPlaced = -1;
CheckerScreen checkerScreen = CheckerScreen::kTitle;
uint32_t checkerScreenStartMs = 0;
lv_obj_t* checkerTitleScreen = nullptr;
lv_obj_t* checkerMatchupScreen = nullptr;
lv_obj_t* checkerPlayAgainScreen = nullptr;
lv_obj_t* checkerYesBtn = nullptr;
lv_obj_t* checkerNoBtn = nullptr;
lv_obj_t* checkerPlayBtn = nullptr;
lv_obj_t* checkerDotsRow = nullptr;
std::array<lv_obj_t*, kCheckerRecordSlots> checkerDots = {};
lv_obj_t* checkerFx = nullptr;
std::array<lv_obj_t*, kCheckerParticleCount> checkerParticles = {};
// Rolling record of the last three finished games: 0 none, 1 win, 2 loss, 3 draw.
std::array<uint8_t, kCheckerRecordSlots> checkerRecord = {};
std::array<CheckerGame::Cell, 9> checkerRenderedCells = {};
bool checkerWinPulsePlayed = false;
bool checkerBubuThinking = false;
uint32_t checkerBubuMoveStartMs = 0;
bool checkerFinishHolding = false;
uint32_t checkerFinishHoldStartMs = 0;
uint32_t gamesActionColor = COLOR_MINT;
uint32_t gamesStatusColor = COLOR_TEXT;

constexpr uint32_t kCheckerBubuThinkDelayMs = 550;
// Keep the finished board on screen briefly so the winning line and its pulse
// are actually visible before switching to the result screen.
constexpr uint32_t kCheckerFinishHoldMs = 1500;

// Bright, kid-friendly palette for the tic-tac-toe board. The 240x240 round
// panel turns sky blue during play so the white board pops.
// Neon-on-black makeover: cyan is the player (X), pink is Bubu (O).
constexpr uint32_t kCheckerPanelBg = 0x000000;
constexpr uint32_t kCheckerPanelRing = 0x14141A;
constexpr uint32_t kCheckerCellBg = 0x0D0D0F;
constexpr uint32_t kCheckerCellBorder = 0x26262B;
constexpr uint32_t kCheckerPlayerMark = 0x35C6F4;   // cyan X
constexpr uint32_t kCheckerBubuMark = 0xFF3B7B;     // pink O
constexpr uint32_t kCheckerBotBody = 0xFFC93C;      // Bubu head glyph
constexpr uint32_t kCheckerDrawText = 0xFFFFFF;

// Board geometry: 156 outer - 2*4 border - 2*8 padding = 132 content, which is
// exactly 3 cells of 40 plus 2 gaps of 6. Cell positions are relative to the
// content area, so they must NOT re-add the padding.
// 156px board keeps all four corner cells inside the 120px bezel radius
// (worst corner sits 114.6px from centre). 48px cells + 6px gaps = 156.
constexpr int kCheckerBoardSize = 156;
constexpr int kCheckerCellSize = 48;
constexpr int kCheckerCellStride = 54;
constexpr int kCheckerBoardOffsetY = 10;   // clears the header row above it
constexpr int kCheckerMarkBox = 26;      // X hit box inside a cell
constexpr int kCheckerRingSize = 30;     // O outer diameter
constexpr int kCheckerRingWidth = 5;

// Shared by every X; lv_line stores the pointer rather than copying.
const lv_point_precise_t kCheckerXStrokeA[] = {{3, 3}, {kCheckerMarkBox - 3, kCheckerMarkBox - 3}};
const lv_point_precise_t kCheckerXStrokeB[] = {{kCheckerMarkBox - 3, 3}, {3, kCheckerMarkBox - 3}};

// Larger X for the matchup card.
constexpr int kCheckerBigMarkBox = 40;
const lv_point_precise_t kCheckerBigXStrokeA[] = {{4, 4}, {kCheckerBigMarkBox - 4, kCheckerBigMarkBox - 4}};
const lv_point_precise_t kCheckerBigXStrokeB[] = {{kCheckerBigMarkBox - 4, 4}, {4, kCheckerBigMarkBox - 4}};

// ---------------------------------------------------------------------------
// MẮT XANH -- colour reflex on the round panel: tap the leaf-green eye.
//
// The rules live in green_eye_game.cc; everything here is presentation and
// input routing. The HUD is deliberately colourless -- white rim, white score,
// white hearts -- because colour on this screen is the question being asked,
// and chrome in red or blue would be one more thing to mistake for an eye.
// ---------------------------------------------------------------------------
constexpr uint32_t kGreenEyePanelBg = 0x000000;
constexpr uint32_t kGreenEyePanelRing = 0x14161C;
constexpr uint32_t kGreenEyeRimTrack = 0x23262E;
constexpr uint32_t kGreenEyeChrome = 0xECEEF2;
constexpr uint32_t kGreenEyeMuted = 0x8A8F9C;
constexpr uint32_t kGreenEyeHeartLost = 0x30343D;
constexpr uint32_t kGreenEyePillDim = 0x1A1C22;
// CẢM XÚC's own colour in the stats panel (statColors[1]), so the reward reads
// as the same stat the parent sees there.
constexpr uint32_t kGreenEyeMood = 0x70C1FF;
constexpr uint32_t kGreenEyeMoodDim = 0x3F6A8C;   // the same, half into the track
constexpr uint32_t kGreenEyeTickMs = 33;
constexpr uint32_t kGreenEyeBannerMs = 1300;      // HẾT LƯỢT! before the scoreboard
constexpr uint32_t kGreenEyeBarDelayMs = 250;     // scoreboard up, then the bar grows
constexpr uint32_t kGreenEyeBarFillMs = 700;
constexpr uint32_t kGreenEyeDemoPulseMs = 1200;
// Buttons are small for a child's finger; a press this close still counts.
constexpr int kGreenEyeButtonSlackPx = 8;

// Geometry, as offsets from the panel centre. tools/verify_green_eye_layout.py
// reads these and checks every eye layout in green_eye_game.cc against them --
// change them together and re-run it.
constexpr int kGreenEyeRimSize = 224;
constexpr int kGreenEyeRimWidth = 6;
constexpr int kGreenEyeScoreY = -88;      // score label centre
constexpr int kGreenEyeHeartsY = 80;      // hearts row centre
constexpr int kGreenEyeHeartPitch = 28;
constexpr int kGreenEyeMoodBarW = 140;
constexpr int kGreenEyeMoodBarH = 10;

// Screens of the one MẮT XANH game -- not separate games.
enum class GreenEyeScreen : uint8_t {
    kSetup,      // how to play + CHƠI
    kPlaying,    // countdown and rounds
    kBanner,     // HẾT LƯỢT!
    kScore,
};

// What each eye object last had applied, so a frame that changes nothing
// touches no style and invalidates nothing.
struct GreenEyeDrawn {
    bool hidden = true;
    int16_t w = -1;
    int16_t h = -1;
    int16_t x = 0;
    int16_t y = 0;
    uint32_t color = 0;
    uint8_t opa = 0;
    uint8_t border = 0;
    bool glint = false;
};

GreenEyeScreen greenEyeScreen = GreenEyeScreen::kSetup;
lv_timer_t* greenEyeTimer = nullptr;
uint32_t greenEyeScreenStartMs = 0;
uint16_t greenEyeBest = 0;              // NVS "greeneye"/"best", loaded at boot
bool greenEyeNewRecord = false;
// True whenever there is nothing (left) to pay: before the first CHƠI, and
// once a game's reward has gone to CareSystem.
bool greenEyeRewardPaid = true;
int greenEyeMoodBefore = 0;
int greenEyeMoodAfter = 0;
uint16_t greenEyeSeenDecisions = 0;
int greenEyeDrawnScore = -1;
int greenEyeDrawnCountdown = -1;
int greenEyeDrawnBarW = -1;
std::array<int8_t, GreenEyeGame::kLives> greenEyeDrawnHearts = {};
std::array<GreenEyeDrawn, GreenEyeGame::kMaxEyes> greenEyeDrawn = {};

lv_obj_t* greenEyeRim = nullptr;
lv_obj_t* greenEyeScoreLabel = nullptr;
std::array<lv_obj_t*, GreenEyeGame::kLives> greenEyeHearts = {};
std::array<lv_obj_t*, GreenEyeGame::kMaxEyes> greenEyeEyes = {};
std::array<lv_obj_t*, GreenEyeGame::kMaxEyes> greenEyeGlints = {};
lv_obj_t* greenEyePlus = nullptr;
lv_obj_t* greenEyeCenterLabel = nullptr;   // 3-2-1, then HẾT LƯỢT!
lv_obj_t* greenEyeSetupScreen = nullptr;
lv_obj_t* greenEyeSetupRecord = nullptr;
lv_obj_t* greenEyeDemoGreen = nullptr;
lv_obj_t* greenEyePlayBtn = nullptr;
lv_obj_t* greenEyeScoreScreen = nullptr;
lv_obj_t* greenEyeResultValue = nullptr;
lv_obj_t* greenEyeResultCaption = nullptr;
lv_obj_t* greenEyeRewardLabel = nullptr;
lv_obj_t* greenEyeMoodGain = nullptr;
lv_obj_t* greenEyeMoodBase = nullptr;
lv_obj_t* greenEyeAgainBtn = nullptr;
lv_obj_t* greenEyeMenuBtn = nullptr;

void HideGreenEyeAll();
void StopGreenEyeTimer();

// ---------------------------------------------------------------------------
// CHẠM NHANH (Quick Tap) -- 30s reflex game on the round panel.
//
// ĐIỂM = HIT + (CHUỖI dài nhất × HỆ SỐ) - TRẬT. The logic lives in
// quick_tap_game.cc; everything here is presentation and input routing.
// ---------------------------------------------------------------------------
constexpr uint32_t kQuickTapPanelBg = 0x000000;
constexpr uint32_t kQuickTapPanelRing = 0x14141A;
constexpr uint32_t kQuickTapRing = 0x00E5C0;      // countdown arc
constexpr uint32_t kQuickTapRingTrack = 0x24242C;
constexpr uint32_t kQuickTapWhiteDot = 0xFFFFFF;
constexpr uint32_t kQuickTapRedDot = 0xFF3B5C;
constexpr uint32_t kQuickTapPillDim = 0x1A1A22;
constexpr uint32_t kQuickTapMuted = 0x8A8A96;

// The banner between the last dot and the scoreboard.
constexpr uint32_t kQuickTapTimeUpHoldMs = 3000;    // HẾT GIỜ!  (spec)
constexpr uint32_t kQuickTapRedHoldMs = 2000;       // CHẠM NHẦM!
constexpr uint32_t kQuickTapTickMs = 33;

// Screens of the one Quick Tap game -- not separate games.
enum class QuickTapScreen : uint8_t {
    kSetup,      // mode + difficulty picker
    kPlaying,
    kBanner,     // HẾT GIỜ! / CHẠM NHẦM!
    kScore,
};

constexpr int kQuickTapModeCount = static_cast<int>(QuickTapGame::Mode::kModeCount);
constexpr int kQuickTapDiffCount = static_cast<int>(QuickTapGame::Difficulty::kDifficultyCount);

QuickTapScreen quickTapScreen = QuickTapScreen::kSetup;
QuickTapGame::Mode quickTapMode = QuickTapGame::Mode::kSimple;
QuickTapGame::Difficulty quickTapDifficulty = QuickTapGame::Difficulty::kEasy;
uint32_t quickTapBannerStartMs = 0;
uint32_t quickTapBannerHoldMs = 0;
bool quickTapNewRecord = false;
lv_timer_t* quickTapTimer = nullptr;
// Best score per mode x difficulty, so an easy grind cannot beat a hard run.
std::array<uint16_t, kQuickTapModeCount * kQuickTapDiffCount> quickTapRecords = {};

lv_obj_t* quickTapArc = nullptr;
lv_obj_t* quickTapDot = nullptr;
lv_obj_t* quickTapBanner = nullptr;
lv_obj_t* quickTapSetupScreen = nullptr;
std::array<lv_obj_t*, kQuickTapModeCount> quickTapModeBtns = {};
std::array<lv_obj_t*, kQuickTapDiffCount> quickTapDiffBtns = {};
lv_obj_t* quickTapRecordLabel = nullptr;
std::array<lv_obj_t*, kQuickTapModeCount> quickTapModeRecords = {};
lv_obj_t* quickTapScoreScreen = nullptr;
lv_obj_t* quickTapScoreValue = nullptr;
lv_obj_t* quickTapScoreCaption = nullptr;
lv_obj_t* quickTapStatRow = nullptr;
lv_obj_t* quickTapHitsLabel = nullptr;
lv_obj_t* quickTapRedMark = nullptr;
lv_obj_t* quickTapRedLabel = nullptr;
lv_obj_t* quickTapMissLabel = nullptr;
lv_obj_t* quickTapStreakLabel = nullptr;
lv_obj_t* quickTapReplayBtn = nullptr;
lv_obj_t* quickTapMenuBtn = nullptr;

void HideQuickTapAll();
void ShowQuickTapScreen(QuickTapScreen screen);
void BeginQuickTapRound(QuickTapGame::Mode mode);
void StartQuickTapTimer();
void StopQuickTapTimer();

// ---------------------------------------------------------------------------
// RẮN SĂN MỒI (Snake) -- the arcade classic on the round panel.
//
// The rules live in snake_game.cc; everything here is presentation and input.
// Three things about this screen are not like the other games:
//
//   * The board is drawn into an lv_canvas by writing RGB565 straight into its
//     buffer, not out of LVGL objects. A 15x15 board would be 225 widgets that
//     exist for one game, and the snake can occupy every one of them; the eye
//     animation already owns a full-screen canvas the same way, so this is the
//     established pattern here rather than a new one.
//   * Steering takes a swipe OR a tap, and a tap anywhere on the playfield
//     counts -- the quadrant the tap falls in picks the direction. The drawn
//     chevrons are affordances that flash on a turn, not hit targets. A child's
//     finger is far bigger than a 24px arrow, and a steer that reads as
//     "nothing happened" is the worst failure this game has.
//   * There is no pause, for the same reason HỌC TẬP has none: the menu's 30s
//     inactivity close-out would either fire under a paused game or have to be
//     defeated, and a long press already quits from every other game here.
//
// Every y below was measured against the compiled faces with tools/lvwidth.py
// before it was written, not adjusted afterwards, and
// tools/verify_snake_layout.py re-tests all of them against the constants in
// this file. Worst case is "ĂN HẾT BÀN!" at vn_22, which clears the r=109 ring
// by 4.35px; the board's bottom corners clear it by 12.21px against the r=112
// panel clip.
// ---------------------------------------------------------------------------
constexpr uint32_t kSnakeAccent     = 0x5BE36B;   // carousel + chrome
constexpr uint32_t kSnakePanelBg    = 0x04080A;
constexpr uint32_t kSnakePanelRing  = 0x122018;
constexpr uint32_t kSnakeBoardBg    = 0x081410;
constexpr uint32_t kSnakeGridLine   = 0x10241A;
constexpr uint32_t kSnakeBody       = 0x3FCB5E;
constexpr uint32_t kSnakeHead       = 0x9CFFAE;
constexpr uint32_t kSnakePellet     = 0xFF5A3C;
constexpr uint32_t kSnakeMuted      = 0x7E8C84;
constexpr uint32_t kSnakePillDim    = 0x14201A;

// 15 cells x 9px = 135px. Centred 6px low so the score line has room above it
// without pushing the board's bottom corners into the bezel.
constexpr int kSnakeCell = 9;
constexpr int kSnakeBoardPx = SnakeGame::kBoardCells * kSnakeCell;   // 135
constexpr int kSnakeBoardOffsetY = 6;
// Left inside each cell so segments read as separate blocks rather than one
// bar. 7px of 9 is the largest inset that still leaves a visible seam.
constexpr int kSnakeCellInset = 1;

// Every offset below is a dy for lv_obj_align(obj, LV_ALIGN_CENTER, 0, dy) on
// the 240x240 panel, and every one was measured against the compiled faces
// before it was written. tools/verify_snake_layout.py reads these back out of
// this file and re-tests them, so changing one without re-measuring fails.
constexpr int kSnakeScoreDy       = -93;
constexpr int kSnakeChevronUpDy   = -72;
constexpr int kSnakeChevronDownDy = 86;
constexpr int kSnakeChevronSideDx = 90;
constexpr int kSnakeTitleDy       = -76;
constexpr int kSnakeBestDy        = -46;
constexpr int kSnakeSpeedCapDy    = -20;
constexpr int kSnakeSpeedDy       = 8;
constexpr int kSnakePlayDy        = 46;
constexpr int kSnakeHintDy        = 78;
constexpr int kSnakeOverHeadDy    = -60;
constexpr int kSnakeOverScoreDy   = -28;
constexpr int kSnakeOverBestDy    = -2;
constexpr int kSnakeAgainDy       = 36;
constexpr int kSnakeMenuDy        = 76;
constexpr int kSnakeSpeedPillW    = 118;
constexpr int kSnakeSpeedPillH    = 30;
constexpr int kSnakePlayPillW     = 120;
constexpr int kSnakePlayPillH     = 34;
constexpr int kSnakeAgainPillW    = 130;
constexpr int kSnakeAgainPillH    = 32;
constexpr int kSnakeMenuPillW     = 88;
constexpr int kSnakeMenuPillH     = 28;

constexpr uint32_t kSnakeTickMs = 33;
constexpr uint32_t kSnakeChevronFlashMs = 140;
// Long enough to read the cause of death before the scoreboard replaces it.
constexpr uint32_t kSnakeOverHoldMs = 900;

enum class SnakeScreen : uint8_t {
    kSetup,      // title + record + speed picker + CHƠI
    kPlaying,
    kHold,       // dead board, held a beat so the cause is visible
    kOver,       // cause, score, record, CHƠI LẠI / MENU
};

constexpr int kSnakeSpeedCount = static_cast<int>(SnakeGame::Speed::kSpeedCount);

SnakeScreen snakeScreen = SnakeScreen::kSetup;
SnakeGame::Speed snakeSpeed = SnakeGame::Speed::kNormal;
bool snakeNewRecord = false;
uint32_t snakeHoldStartMs = 0;
lv_timer_t* snakeTimer = nullptr;
// Best score per speed, so an easy grind cannot beat a fast run.
std::array<uint16_t, kSnakeSpeedCount> snakeRecords = {};
// Which chevron is lit, and until when. -1 is none. snakeFlashApplied is the
// value the widgets were last styled for: without it the 33ms tick would set
// four styles a frame forever, and every style write invalidates its object.
int snakeFlashChevron = -1;
int snakeFlashApplied = -2;
uint32_t snakeFlashUntilMs = 0;

lv_obj_t* snakeCanvas = nullptr;
lv_color_t* snakeCanvasBuf = nullptr;
lv_obj_t* snakeScoreLabel = nullptr;
std::array<lv_obj_t*, 4> snakeChevrons = {};      // indexed by SnakeGame::Direction
lv_obj_t* snakeSetupScreen = nullptr;
lv_obj_t* snakeSetupRecord = nullptr;
// One pill that cycles CHẬM -> VỪA -> NHANH, not three side by side: "NHANH"
// alone is 79px at vn_20, so three pills each wide enough for their own label
// would not fit the chord at this height.
lv_obj_t* snakeSpeedBtn = nullptr;
lv_obj_t* snakePlayBtn = nullptr;
lv_obj_t* snakeOverScreen = nullptr;
lv_obj_t* snakeOverHead = nullptr;
lv_obj_t* snakeOverScore = nullptr;
lv_obj_t* snakeOverBest = nullptr;
lv_obj_t* snakeAgainBtn = nullptr;
lv_obj_t* snakeMenuBtn = nullptr;

void HideSnakeAll();
void ShowSnakeScreen(SnakeScreen screen);
void BeginSnakeRound();
void StartSnakeTimer();
void StopSnakeTimer();

// ---------------------------------------------------------------------------
// MÊ CUNG NGHIÊNG -- IMU marble maze for the 240x240 round panel.
//
// Physics and level data live in tilt_maze_game.cc. This file owns only LVGL,
// raw RGB565 painting and touch routing. The 240x240 canvas is ~113 KiB in
// PSRAM; one canvas is substantially cheaper than hundreds of tile widgets.
// ---------------------------------------------------------------------------
constexpr uint32_t kMazeAccent = 0x42E8F5;
constexpr uint32_t kMazePanelBg = 0x071A2B;
constexpr uint32_t kMazeFloor = 0x0A2135;
constexpr uint32_t kMazeFloorDot = 0x123A55;
constexpr uint32_t kMazeWall = 0x31CDE3;
constexpr uint32_t kMazeWallLight = 0x8AFAFF;
constexpr uint32_t kMazeWallShade = 0x16869F;
constexpr uint32_t kMazeGold = 0xFFD84A;
constexpr uint32_t kMazeGoal = 0x53E06F;
constexpr uint32_t kMazeBreakable = 0xFF755F;
constexpr uint32_t kMazeWormA = 0xF75CCF;
constexpr uint32_t kMazeWormB = 0x9B73FF;
constexpr uint32_t kMazeMuted = 0x8DB2C8;
constexpr uint32_t kMazeTickMs = 33;
constexpr int kMazeCanvasPx = 240;

lv_timer_t* mazeTimer = nullptr;
lv_obj_t* mazeCanvas = nullptr;
uint16_t* mazeCanvasBuf = nullptr;
lv_obj_t* mazeHud = nullptr;
lv_obj_t* mazeCalibrationScreen = nullptr;
lv_obj_t* mazeCalibrationStatus = nullptr;
lv_obj_t* mazeCalibrateBtn = nullptr;
lv_obj_t* mazeCountdownLabel = nullptr;
lv_obj_t* mazePlanningTitle = nullptr;
lv_obj_t* mazePlanUpBtn = nullptr;
lv_obj_t* mazePlanDownBtn = nullptr;
lv_obj_t* mazePlanLeftBtn = nullptr;
lv_obj_t* mazePlanRightBtn = nullptr;
lv_obj_t* mazePlanResumeBtn = nullptr;
lv_obj_t* mazeResultScreen = nullptr;
std::array<lv_obj_t*, 3> mazeResultStars = {};
lv_obj_t* mazeResultSummary = nullptr;
lv_obj_t* mazeResultPrimaryBtn = nullptr;
lv_obj_t* mazeResultRetryBtn = nullptr;
TiltMazeGame::Phase mazeVisiblePhase = TiltMazeGame::Phase::kStopped;

void HideTiltMazeAll();
void CreateTiltMazeUI();
void UpdateTiltMazeUI();
void StartTiltMazeTimer();
void StopTiltMazeTimer();

// ---------------------------------------------------------------------------
// ĐƯỜNG PHỐ -- three-lane endless runner controlled by the IMU.
// ---------------------------------------------------------------------------
constexpr uint32_t kRunnerAccent = 0x42E8F5;
constexpr uint32_t kRunnerPanelBg = 0x06131D;
constexpr uint32_t kRunnerRoad = 0x102936;
constexpr uint32_t kRunnerLane = 0x557482;
constexpr uint32_t kRunnerGold = 0xFFD84A;
constexpr uint32_t kRunnerMuted = 0x8DB2C8;
constexpr uint32_t kRunnerTickMs = 33;
constexpr int kRunnerCanvasPx = 240;

lv_timer_t* runnerTimer = nullptr;
lv_obj_t* runnerCanvas = nullptr;
uint16_t* runnerCanvasBuf = nullptr;
lv_obj_t* runnerHudScore = nullptr;
lv_obj_t* runnerHudGold = nullptr;
lv_obj_t* runnerHudBoost = nullptr;
lv_obj_t* runnerSetupScreen = nullptr;
lv_obj_t* runnerSetupBest = nullptr;
lv_obj_t* runnerPlayBtn = nullptr;
lv_obj_t* runnerPhaseLabel = nullptr;
lv_obj_t* runnerOverScreen = nullptr;
lv_obj_t* runnerOverTitle = nullptr;
lv_obj_t* runnerOverScore = nullptr;
lv_obj_t* runnerOverBest = nullptr;
lv_obj_t* runnerAgainBtn = nullptr;
lv_obj_t* runnerMenuBtn = nullptr;
TrafficRunnerGame::Phase runnerVisiblePhase = TrafficRunnerGame::Phase::kStopped;

void HideTrafficRunnerAll();
void CreateTrafficRunnerUI();
void UpdateTrafficRunnerUI();
void StartTrafficRunnerTimer();
void StopTrafficRunnerTimer();

// ---------------------------------------------------------------------------
// HỌC TẬP (Pomodoro) -- focus/break blocks on the round panel.
//
// The logic lives in pomodoro_timer.cc; everything here is presentation and
// input routing. Two of its rules shape this UI directly:
//
//   * There is no pause, so there is no pause control and no tap gesture that
//     could produce one. A long press voids the block, and so does closing the
//     menu -- nothing ticks this in the background.
//   * Only a focus block that runs to zero is banked, so the only celebratory
//     screen is the boundary banner.
//
// Every position below was measured against the compiled faces with
// tools/lvwidth.py, not estimated. Worst cases, against the ring's 109px inner
// edge: the outer preset discs reach 104.03 (4.97 clear), "HÔM NAY 12" reaches
// 98.08, "CHUỖI n NGÀY" reaches 95.03. The apostrophe is deliberate ASCII:
// U+0027 is in all three faces, the true prime U+2032 is not.
// ---------------------------------------------------------------------------
constexpr uint32_t kPomodoroFocusColor = 0xFF6B45;   // tomato, the focus phase
constexpr uint32_t kPomodoroRestColor  = 0x58F5C9;   // COLOR_MINT, the breaks
constexpr uint32_t kPomodoroTrack      = 0x24242C;
constexpr uint32_t kPomodoroMuted      = 0x7F8AA3;
constexpr uint32_t kPomodoroPanelRing  = 0x1C2E45;

// 200ms: the countdown only needs whole seconds, but a 1Hz tick would show a
// second of lag on entry and after every boundary.
constexpr uint32_t kPomodoroTickMs   = 200;
constexpr uint32_t kPomodoroBannerMs = 1400;

constexpr int kPomodoroRingRadius = 112;
constexpr int kPomodoroRingWidth  = 8;
constexpr int kPomodoroRingBox    = kPomodoroRingRadius * 2 + kPomodoroRingWidth;

// Three round containers at x = 46 / 120 / 194, cy = 118. Fill marks the
// selection rather than size, so no slot ever has to grow into the ring.
constexpr int kPomodoroPresetCount = static_cast<int>(PomodoroTimer::Preset::kPresetCount);
constexpr int kPomodoroDiscSize   = 60;
constexpr int kPomodoroDiscLeft   = 16;   // left edge of disc 0
constexpr int kPomodoroDiscStride = 74;
constexpr int kPomodoroDiscTop    = 88;
constexpr int kPomodoroDiscHitSlack = 10;   // a child's finger is wider than 60px

constexpr int kPomodoroTodayTop = 42;
constexpr int kPomodoroBreakTop = 158;

constexpr int kPomodoroPhaseTop  = 62;
constexpr int kPomodoroClockTop  = 96;
constexpr int kPomodoroLenTop    = 138;
constexpr int kPomodoroDotsTop   = 178;
constexpr int kPomodoroDotSize   = 9;
constexpr int kPomodoroMaxDots   = 4;

constexpr int kPomodoroBannerTopY = 74;
constexpr int kPomodoroBannerBigY = 104;
constexpr int kPomodoroBannerSubY = 148;

// Screens of the one panel -- not separate features.
enum class PomodoroScreen : uint8_t {
    kSetup,
    kRunning,
    kBanner,
};

PomodoroScreen pomodoroScreen = PomodoroScreen::kSetup;
PomodoroTimer::Preset pomodoroPreset = PomodoroTimer::Preset::kClassic;
uint32_t pomodoroBannerStartMs = 0;
bool pomodoroBannerIsVoid = false;
lv_timer_t* pomodoroTimer = nullptr;

lv_obj_t* pomodoroPanel = nullptr;
lv_obj_t* pomodoroArc = nullptr;
lv_obj_t* pomodoroSetupScreen = nullptr;
std::array<lv_obj_t*, kPomodoroPresetCount> pomodoroDiscs = {};
lv_obj_t* pomodoroTodayLabel = nullptr;
lv_obj_t* pomodoroBreakLabel = nullptr;
lv_obj_t* pomodoroRunScreen = nullptr;
lv_obj_t* pomodoroPhaseLabel = nullptr;
lv_obj_t* pomodoroClockLabel = nullptr;
lv_obj_t* pomodoroLenLabel = nullptr;
lv_obj_t* pomodoroDotsRow = nullptr;
std::array<lv_obj_t*, kPomodoroMaxDots> pomodoroDots = {};
lv_obj_t* pomodoroBannerScreen = nullptr;
lv_obj_t* pomodoroBannerTop = nullptr;
lv_obj_t* pomodoroBannerBig = nullptr;
lv_obj_t* pomodoroBannerSub = nullptr;

void ShowPomodoroScreen(PomodoroScreen screen);
void StartPomodoroTimer();
void StopPomodoroTimer();
void EnterPomodoroBanner(const char* top, const char* big, const char* sub,
                         uint32_t color, bool is_void);

constexpr uint32_t kCheckerDotWin = kCheckerPlayerMark;
constexpr uint32_t kCheckerDotLoss = kCheckerBubuMark;
constexpr uint32_t kCheckerDotIdle = 0x2A2A30;
constexpr uint32_t LEVEL_UP_ANIMATION_DURATION_MS = 2200;
constexpr const char* kLevelUpGifAssetFile = "level up.gif";
constexpr const char* kLevelUpGifMissingNotice = "Level up GIF missing";

const char* menuItemLabelTexts[MENU_ITEM_COUNT] = {
    "CHĂM SÓC",
    "KẾT NỐI",
    "NHẮC NHỞ",
    "GHI CHÚ",
    "HỌC TẬP",
    "CÀI ĐẶT",
    "BỐC QUẺ",
};

// Packed into assets.bin via DEFAULT_ASSETS_EXTRA_FILES (see main/CMakeLists.txt)
// from main/assets/menu_icons/. Order must match the MenuItem enum. Each is a
// full 240x240 screen-sized image; nullptr means no image is provided yet for
// that item, and it falls back to a plain text caption.
const char* menuItemIconFiles[MENU_ITEM_COUNT] = {
    "menu_care.png",
    "menu_connect.png",
    "menu_reminders.png",
    "menu_notes.png",
    // Not in assets.bin yet -- ResolvePersistentAssetImage returns nullptr and
    // UpdateMenuItemStyles falls back to the "HỌC TẬP" caption. Drop the file
    // into main/assets/menu_icons/ and regenerate the bundle to light it up;
    // no code change needed.
    "menu_pomodoro.png",
    "menu_settings.png",
    "menu_fortune.png",
};

const char* careItemLabelTexts[CARE_ITEM_COUNT] = {
    "CHO ĂN",
    "GIẢI TRÍ",
    "TẮM RỬA",
    "NGỦ",
    "TRẠNG THÁI",
    "CẤP ĐỘ",
};

// Packed into assets.bin via DEFAULT_ASSETS_EXTRA_FILES (see main/CMakeLists.txt)
// from main/assets/menu_icons/. Order must match the CareItem enum. Each is a
// full 240x240 screen-sized image, same convention as menuItemIconFiles.
const char* careItemIconFiles[CARE_ITEM_COUNT] = {
    "sub_care_feed.png",
    "sub_care_play.png",
    "sub_care_clean.png",
    "sub_care_sleep.png",
    "sub_care_stats.png",
    "sub_care_level.png",
};

const char* connectItemLabelTexts[CONNECT_ITEM_COUNT] = {
    "TRÊN THIẾT BỊ",     // CONNECT_DEVICE
    "BẰNG ĐIỆN THOẠI",   // CONNECT_PHONE
};

const std::array<const char*, 13> keyboardButtonTexts = {
    "1!@", "2 abc", "3 def",
    "4 ghi", "5 jkl", "6 mno",
    "7 pqrs", "8 tuv", "9 wxyz",
    LV_SYMBOL_UP, "0", LV_SYMBOL_LEFT,
    "OK",
};

std::vector<lv_obj_t*> connectWifiItems;
uint32_t lastWifiScanVersion = 0;
bool lastWifiScanActive = false;
std::string selectedWifiSsid;
char keyboardText[65] = {0};
size_t keyboardLen = 0;
char keyboardLastKey = '\0';
size_t keyboardLastIndex = 0;
uint32_t keyboardLastTapMs = 0;
bool keyboardCaps = false;


constexpr uint32_t T9_TAP_TIMEOUT_MS = 1100;
constexpr uint32_t MENU_INACTIVITY_TIMEOUT_MS = 30000;

uint32_t lastMenuActivityMs = 0;

const char* statNames[STAT_COUNT] = {
    "CÁI BỤNG",
    "CẢM XÚC",
    "NĂNG LƯỢNG",
    "SẠCH SẼ",
};

const uint32_t statColors[STAT_COUNT] = {
    0xFF7F50,
    0x70C1FF,
    0xFFD23F,
    0x58F5C9,
};

char gameStatusMsg[96] = "Chạm để chơi";

constexpr std::array<uint32_t, 10> kLevelArcStrongColors = {{
    0xFF1744,
    0xFF6D00,
    0xFFD600,
    0x00C853,
    0x00B8D4,
    0x2962FF,
    0x651FFF,
    0xAA00FF,
    0xD500F9,
    0xC51162,
}};
// The whole TRÒ CHƠI list opens at this level.
constexpr int kGamesUnlockLevel = 1;

// ---------------------------------------------------------------------------
// TRÒ CHƠI list -- the carousel of the three games.
//
// Laid out in concentric zones because the panel is a circle: a position ring
// on the rim, a tappable emblem in the middle, the game's name under it, and a
// short stat line under that.
//
// Every vertical position here was measured against the compiled fonts with
// tools/fit.py rather than estimated -- it walks the generated lv_font_*.c and
// tests the worst ink corner of each string against the ring's inner edge.
// The Vietnamese diacritics are what make it tight, not the letter widths: the
// dot below on Ạ in "CHẠM NHANH" drops ink to y=190, and the stacked breve and
// acute on Ắ in "MẮT XANH" reach 23px above the baseline. Every string clears
// the ring by at least 6.3px at these positions; at baseline y=186, which is
// where the name first sat, "CHẠM NHANH" cleared it by 0.8px.
// ---------------------------------------------------------------------------
constexpr int kGamesRingRadius = 112;
constexpr int kGamesRingWidth = 6;
// lv_arc draws its centreline at (min(w,h) - arc_width) / 2.
constexpr int kGamesRingBox = kGamesRingRadius * 2 + kGamesRingWidth;
constexpr int kGamesRingInner = kGamesRingRadius - kGamesRingWidth / 2;   // 109
constexpr uint32_t kGamesRingTrack = 0x1A2437;

// One slice of the rim per game, lit with a small gap either side so two
// neighbouring segments never look like one arc. LVGL angles start at 3
// o'clock and run clockwise, so segment 0 is centred on 12 o'clock (270°).
//
// Derived rather than tabulated: with three games this was a hand-written
// {214, 334, 94}, which had to be recomputed by hand the moment a fourth was
// added. lv_arc_set_angles() normalises internally, so an end past 360 is
// fine and no wrapping is needed here.
constexpr int kGamesSegmentGap = 8;
constexpr int kGamesSegmentSpan = 360 / GAME_SELECTION_COUNT - kGamesSegmentGap;
constexpr int GamesSegmentStart(int selection) {
    return 270 - kGamesSegmentSpan / 2 + selection * (360 / GAME_SELECTION_COUNT);
}

constexpr int kGamesEmblemSize = 104;
constexpr int kGamesEmblemTop = 38;

// Label TOPS, not baselines -- lv_obj_align positions the line box. LVGL puts
// the baseline at top + line_height - base_line, i.e. +23 for vn_22 (29/6) and
// +21 for vn_20 (27/6).
constexpr int kGamesNameTop = 153;     // baseline y = 176
constexpr int kGamesChipTop = 183;     // baseline y = 204
constexpr uint32_t kGamesChipColor = 0x7F8AA3;

bool IsPointInside(lv_obj_t* obj, uint16_t x, uint16_t y) {
    if (obj == nullptr) {
        return false;
    }
    lv_area_t area;
    lv_obj_get_coords(obj, &area);
    return x >= area.x1 && x <= area.x2 && y >= area.y1 && y <= area.y2;
}

void MarkMenuActivity() {
    lastMenuActivityMs = lv_tick_get();
}

bool IsGamesUnlocked() {
    return LevelSystem::GetLevel() >= kGamesUnlockLevel;
}

const char* GetSelectedGameTitle() {
    switch (selectedGame) {
        case GAME_SELECTION_GREEN_EYE:
            return "MẮT XANH";
        case GAME_SELECTION_CHECKER:
            return "CỜ CA-RÔ";
        case GAME_SELECTION_QUICK_TAP:
            return "CHẠM NHANH";
        case GAME_SELECTION_SNAKE:
            // 157px at vn_22, clearing the ring by 12.57px at baseline y=176 --
            // roomier than CHẠM NHANH, which clears by 6.99. Measured, not eyed.
            return "RẮN SĂN MỒI";
        case GAME_SELECTION_TILT_MAZE:
            return "MÊ CUNG";
        case GAME_SELECTION_TRAFFIC_RUNNER:
            return "ĐƯỜNG PHỐ";
        case GAME_SELECTION_COUNT:
            break;
    }
    return "TRÒ CHƠI";
}

// The stat line under the game's name. Kept to one short string: at vn_20 the
// row is 24px below centre, where the circle only affords about 150px, so a
// sentence does not fit here -- the instruction copy the old pill carried has
// no home on this layout and the emblem says it instead.
//
// A game that persists a score shows its best here (MẮT XANH "greeneye",
// CHẠM NHANH "quicktap", RẮN SĂN MỒI "snake", ĐƯỜNG PHỐ). CỜ CA-RÔ stores
// nothing, so its line is blank rather than an invented number.
void SetGamesMenuStatusForSelection() {
    gameStatusMsg[0] = '\0';
    gamesStatusColor = kGamesChipColor;

    switch (selectedGame) {
        case GAME_SELECTION_GREEN_EYE:
            gamesActionColor = GreenEyeGame::HueRgb(GreenEyeGame::Hue::kGreen);
            // "KỶ LỤC 999" is 114px at vn_20 -- the same string Snake already
            // fits here. A score cannot really reach it; the clamp is for NVS.
            if (greenEyeBest > 0) {
                std::snprintf(gameStatusMsg, sizeof(gameStatusMsg), "KỶ LỤC %u",
                              static_cast<unsigned>(std::min<uint16_t>(greenEyeBest, 999)));
            }
            break;
        case GAME_SELECTION_CHECKER:
            gamesActionColor = 0xA7D8FF;
            break;
        case GAME_SELECTION_QUICK_TAP: {
            gamesActionColor = kQuickTapRing;
            // Records are per mode x difficulty; the line shows the best of
            // them, which is the only one that reads as "your record".
            uint16_t best = 0;
            for (uint16_t record : quickTapRecords) {
                best = std::max(best, record);
            }
            // Clamped to three digits: the row affords ~128px at vn_20 and
            // "KỶ LỤC 9999" is 126px, i.e. 0.8px off the ring. The game cannot
            // score that high (33 hits + 33 streak x3 caps it near 132), but
            // this value comes back from NVS, so it is not ours to trust.
            if (best > 0) {
                std::snprintf(gameStatusMsg, sizeof(gameStatusMsg), "KỶ LỤC %u",
                              static_cast<unsigned>(std::min<uint16_t>(best, 999)));
            }
            break;
        }
        case GAME_SELECTION_SNAKE: {
            gamesActionColor = kSnakeAccent;
            uint16_t best = 0;
            for (uint16_t record : snakeRecords) {
                best = std::max(best, record);
            }
            // Same three-digit clamp as Quick Tap: "KỶ LỤC 999" is 114px and
            // clears the ring by 4.15px, and the value comes back from NVS, so
            // it is not ours to trust. A perfect game caps the score at 222.
            if (best > 0) {
                std::snprintf(gameStatusMsg, sizeof(gameStatusMsg), "KỶ LỤC %u",
                              static_cast<unsigned>(std::min<uint16_t>(best, 999)));
            }
            break;
        }
        case GAME_SELECTION_TILT_MAZE:
            gamesActionColor = kMazeAccent;
            std::snprintf(gameStatusMsg, sizeof(gameStatusMsg), "LEVEL %u",
                          static_cast<unsigned>(TiltMazeGame::GetCurrentLevel() + 1));
            break;
        case GAME_SELECTION_TRAFFIC_RUNNER: {
            gamesActionColor = kRunnerAccent;
            const uint32_t best = TrafficRunnerGame::GetHighScore();
            if (best > 0) {
                std::snprintf(gameStatusMsg, sizeof(gameStatusMsg), "KỶ LỤC %u",
                              static_cast<unsigned>(std::min<uint32_t>(best, 999)));
            }
            break;
        }
        case GAME_SELECTION_COUNT:
            gamesActionColor = COLOR_MINT;
            break;
    }
}

EyeDisplay* GetEyeDisplay() {
    return dynamic_cast<EyeDisplay*>(displayHandle);
}

// The only place currentState is assigned. Publishing here rather than at the
// end of input dispatch means a menu transition takes effect on the same call
// that caused it -- closing a menu brings the eyes straight back.
void SetMenuState(MenuState state) {
    if (currentState == state) {
        return;
    }
    const bool was_game_active = currentState == MENU_GAME_ACTIVE;
    const bool will_be_game_active = state == MENU_GAME_ACTIVE;
    currentState = state;
    if (was_game_active != will_be_game_active) {
        Application::GetInstance().SetInteractiveGameActive(will_be_game_active);
    }
    // EyeDisplay owns the derivation so precedence stays in one place: hatching
    // and sleep outrank any panel, and a closed menu hands the screen back to
    // the eyes (or to feeding/bathing, whichever is running).
    if (displayHandle != nullptr) {
        displayHandle->RefreshScreen();
    }
}

void TryEnterSleepMode() {
    auto* eye_display = GetEyeDisplay();
    if (eye_display == nullptr) {
        if (displayHandle != nullptr) {
            displayHandle->ShowNotification("Sleep mode unavailable");
        }
        return;
    }

    MenuSystem::Close();
    if (!eye_display->StartSleepMode()) {
        eye_display->ShowNotification("Can't sleep right now");
    }
}

void StopTransientAnimationLocked() {
    if (!transientAnimationActive) {
        return;
    }

    transientAnimationActive = false;
    transientAnimationType = TransientAnimationType::NONE;
    if (transientTimer != nullptr) {
        esp_timer_stop(transientTimer);
    }

    if (transientGifController) {
        transientGifController->Stop();
        transientGifController.reset();
    }

    if (transientImage != nullptr) {
        lv_image_set_src(transientImage, nullptr);
        lv_obj_add_flag(transientImage, LV_OBJ_FLAG_HIDDEN);
    }
    if (transientOverlay != nullptr) {
        lv_obj_add_flag(transientOverlay, LV_OBJ_FLAG_HIDDEN);
    }
    transientRawImage.reset();
}

// Full-screen black backdrop plus a centred image, created lazily and reused.
// Both start hidden; StartTransientAnimation reveals and raises them.
void EnsureTransientOverlayLocked() {
    if (transientOverlay == nullptr) {
        transientOverlay = lv_obj_create(lv_screen_active());
        lv_obj_set_size(transientOverlay, 240, 240);
        lv_obj_center(transientOverlay);
        lv_obj_set_style_radius(transientOverlay, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(transientOverlay, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(transientOverlay, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(transientOverlay, 0, 0);
        lv_obj_set_style_pad_all(transientOverlay, 0, 0);
        lv_obj_clear_flag(transientOverlay, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(transientOverlay, LV_OBJ_FLAG_HIDDEN);
    }

    if (transientImage == nullptr) {
        transientImage = lv_image_create(lv_screen_active());
        lv_obj_center(transientImage);
        lv_obj_add_flag(transientImage, LV_OBJ_FLAG_HIDDEN);
    }
}

void EnsureTransientTimerCreated() {
    if (transientTimer != nullptr) {
        return;
    }

    esp_timer_create_args_t timer_args = {
        .callback = [](void*) {
            Application::GetInstance().Schedule([]() {
                if (!transientAnimationActive || displayHandle == nullptr) {
                    return;
                }
                DisplayLockGuard lock(displayHandle);
                StopTransientAnimationLocked();
            });
        },
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "transient_gif",
        .skip_unhandled_events = true,
    };

    esp_err_t err = esp_timer_create(&timer_args, &transientTimer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create transient animation timer: %s", esp_err_to_name(err));
        transientTimer = nullptr;
    }
}

void RestartTransientTimer(uint32_t duration_ms) {
    if (transientTimer == nullptr) {
        return;
    }
    esp_timer_stop(transientTimer);
    esp_err_t err = esp_timer_start_once(
        transientTimer, static_cast<uint64_t>(duration_ms) * 1000ULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start transient animation timer: %s", esp_err_to_name(err));
    }
}

const LvglImage* ResolveAssetImage(const char* asset_file) {
    transientRawImage.reset();

    if (asset_file == nullptr) {
        return nullptr;
    }

    auto& assets = Assets::GetInstance();
    if (!assets.partition_valid()) {
        return nullptr;
    }

    void* asset_ptr = nullptr;
    size_t asset_size = 0;
    if (!assets.GetAssetData(asset_file, asset_ptr, asset_size) ||
        asset_ptr == nullptr || asset_size == 0) {
        return nullptr;
    }

    transientRawImage = std::make_unique<LvglRawImage>(asset_ptr, asset_size);
    return transientRawImage.get();
}

// Like ResolveAssetImage, but keeps the decoded image alive in `out_holder`
// instead of the single shared transient slot -- for images (e.g. menu icons)
// that need to stay on screen alongside other images, not just one at a time.
const LvglImage* ResolvePersistentAssetImage(const char* asset_file, std::unique_ptr<LvglRawImage>& out_holder) {
    if (asset_file == nullptr) {
        return nullptr;
    }

    auto& assets = Assets::GetInstance();
    if (!assets.partition_valid()) {
        return nullptr;
    }

    void* asset_ptr = nullptr;
    size_t asset_size = 0;
    if (!assets.GetAssetData(asset_file, asset_ptr, asset_size) ||
        asset_ptr == nullptr || asset_size == 0) {
        return nullptr;
    }

    out_holder = std::make_unique<LvglRawImage>(asset_ptr, asset_size);
    return out_holder.get();
}

bool StartTransientAnimation(const LvglImage* image, TransientAnimationType animation_type, uint32_t duration_ms) {
    if (displayHandle == nullptr) {
        return false;
    }

    if (image == nullptr) {
        return false;
    }

    EnsureTransientTimerCreated();
    if (transientTimer == nullptr) {
        return false;
    }

    DisplayLockGuard lock(displayHandle);
    EnsureTransientOverlayLocked();
    if (transientOverlay == nullptr || transientImage == nullptr) {
        return false;
    }

    if (transientAnimationActive) {
        if (transientAnimationType == animation_type) {
            RestartTransientTimer(duration_ms);
            return true;
        }
        StopTransientAnimationLocked();
        EnsureTransientOverlayLocked();
        if (transientOverlay == nullptr || transientImage == nullptr) {
            return false;
        }
    }

    transientGifController.reset();
    lv_image_set_src(transientImage, nullptr);

    transientAnimationType = animation_type;
    if (image->IsGif()) {
        transientGifController = std::make_unique<LvglGif>(image->image_dsc());
        if (!transientGifController->IsLoaded()) {
            ESP_LOGW(TAG, "Transient GIF failed to load");
            transientGifController.reset();
            transientAnimationType = TransientAnimationType::NONE;
            return false;
        }
        transientGifController->SetFrameCallback([]() {
            if (transientImage != nullptr && transientGifController != nullptr) {
                lv_image_set_src(transientImage, transientGifController->image_dsc());
            }
        });
        lv_image_set_src(transientImage, transientGifController->image_dsc());
        transientGifController->Start();
    } else {
        lv_image_set_src(transientImage, image->image_dsc());
    }

    transientAnimationActive = true;
    lv_obj_clear_flag(transientImage, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(transientOverlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(transientOverlay);
    lv_obj_move_foreground(transientImage);
    RestartTransientTimer(duration_ms);
    ESP_LOGI(TAG, "Transient animation started (level-up)");
    return true;
}

void TriggerFeedingAnimation() {
    if (displayHandle == nullptr) {
        return;
    }

    // Feeding renders on the eye layer; close menu panels first so it is visible.
    MenuSystem::Close();

    auto* eye_display = GetEyeDisplay();
    if (eye_display == nullptr) {
        ESP_LOGW(TAG, "Feeding skipped: EyeDisplay not available");
        return;
    }

    // Hunger is credited per chomp by EyeDisplay, not up front.
    eye_display->StartFeeding();
    ESP_LOGI(TAG, "Feeding animation started");
}

void TriggerLevelUpAnimationImpl(int level) {
    if (displayHandle == nullptr) {
        return;
    }

    Application::GetInstance().Schedule([level]() {
        if (displayHandle == nullptr) {
            return;
        }

        const LvglImage* level_up_image = ResolveAssetImage(kLevelUpGifAssetFile);
        if (level_up_image == nullptr) {
            ESP_LOGW(TAG, "Level-up image not found (file: %s)", kLevelUpGifAssetFile);
            displayHandle->ShowNotification(kLevelUpGifMissingNotice, 1200);
            return;
        }

        if (!StartTransientAnimation(level_up_image, TransientAnimationType::LEVEL_UP, LEVEL_UP_ANIMATION_DURATION_MS)) {
            displayHandle->ShowNotification(kLevelUpGifMissingNotice, 1200);
            return;
        }

        ESP_LOGI(TAG, "Level-up animation started for level %d", level);
    });
}

void TriggerBathAnimation() {
    if (displayHandle == nullptr) {
        return;
    }

    // Bathing renders on the eye layer; close menu panels first so it is visible.
    MenuSystem::Close();

    auto* eye_display = GetEyeDisplay();
    if (eye_display == nullptr) {
        ESP_LOGW(TAG, "Bathing skipped: EyeDisplay not available");
        return;
    }

    // Cleanliness is credited by EyeDisplay when the sequence reaches its clean
    // beat, not up front -- otherwise the grime clears before it is ever seen.
    eye_display->StartBathing();
    ESP_LOGI(TAG, "Bathing animation started");
}

lv_obj_t* CreateNavButton(lv_obj_t* parent, lv_align_t align, lv_coord_t x_ofs, lv_coord_t y_ofs,
                          const char* symbol, lv_align_t label_align, lv_coord_t label_x_ofs,
                          lv_coord_t label_y_ofs, lv_event_cb_t callback) {
    lv_obj_t* button = lv_btn_create(parent);
    lv_obj_set_size(button, 100, 100);
    lv_obj_align(button, align, x_ofs, y_ofs);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x303030), 0);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);

    lv_obj_t* label = lv_label_create(button);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_label_set_text(label, symbol);
    lv_obj_align(label, label_align, label_x_ofs, label_y_ofs);

    if (callback != nullptr) {
        lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, nullptr);
    }
    return button;
}

// Shows whichever of menuHeroImage/menuHeroLabel matches the current
// selection and hides the other. Only one main-menu item is ever on screen
// at a time now (the source images are already full-screen, 240x240).
void UpdateMenuItemStyles() {
    if (menuHeroImage == nullptr || menuHeroLabel == nullptr) {
        return;
    }

    int idx = static_cast<int>(selectedItem);
    if (idx < 0 || idx >= MENU_ITEM_COUNT) {
        return;
    }

    if (menuItemIsIcon[idx] && menuItemIcons[idx] != nullptr) {
        lv_image_set_src(menuHeroImage, menuItemIcons[idx]->image_dsc());
        lv_obj_clear_flag(menuHeroImage, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(menuHeroLabel, LV_OBJ_FLAG_HIDDEN);
        menuItems[idx] = menuHeroImage;
    } else {
        lv_label_set_text(menuHeroLabel, menuItemLabelTexts[idx]);
        lv_obj_add_flag(menuHeroImage, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(menuHeroLabel, LV_OBJ_FLAG_HIDDEN);
        menuItems[idx] = menuHeroLabel;
    }
}

void UpdateCareItemStyles() {
    if (careHeroImage == nullptr || careHeroLabel == nullptr) {
        return;
    }

    int idx = static_cast<int>(selectedCareItem);
    if (idx < 0 || idx >= CARE_ITEM_COUNT) {
        return;
    }

    if (careItemIsIcon[idx] && careItemIcons[idx] != nullptr) {
        lv_image_set_src(careHeroImage, careItemIcons[idx]->image_dsc());
        lv_obj_clear_flag(careHeroImage, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(careHeroLabel, LV_OBJ_FLAG_HIDDEN);
        careItems[idx] = careHeroImage;
    } else {
        lv_label_set_text(careHeroLabel, careItemLabelTexts[idx]);
        lv_obj_add_flag(careHeroImage, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(careHeroLabel, LV_OBJ_FLAG_HIDDEN);
        careItems[idx] = careHeroLabel;
    }
}

void UpdateConnectItemStyles() {
    if (connectList == nullptr) {
        return;
    }

    for (int i = 0; i < CONNECT_ITEM_COUNT; ++i) {
        int dist = std::abs(i - static_cast<int>(selectedConnectItem));
        lv_obj_t* item = connectItems[i];
        if (item == nullptr) {
            continue;
        }

        const lv_font_t* font = dist == 0 ? &lv_font_montserrat_vn_22 : &lv_font_montserrat_vn_20;
        lv_opa_t opa = dist == 0 ? LV_OPA_COVER : (dist == 1 ? 200 : LV_OPA_40);

        lv_obj_set_style_text_font(item, font, 0);
        lv_obj_set_style_text_opa(item, opa, 0);
        lv_obj_set_style_text_color(item, lv_color_hex(COLOR_TEXT), 0);
        lv_obj_set_style_text_align(item, LV_TEXT_ALIGN_CENTER, 0);
    }
}

void UpdateWifiItemStyles() {
    for (size_t i = 0; i < connectWifiItems.size(); ++i) {
        auto* item = connectWifiItems[i];
        if (item == nullptr) {
            continue;
        }

        int dist = std::abs(static_cast<int>(i) - static_cast<int>(selectedWifiIndex));
        const lv_font_t* font = dist == 0 ? &lv_font_montserrat_vn_22 : &lv_font_montserrat_vn_20;
        lv_opa_t opa = dist == 0 ? LV_OPA_COVER : (dist == 1 ? 200 : LV_OPA_40);

        lv_obj_set_style_text_font(item, font, 0);
        lv_obj_set_style_text_opa(item, opa, 0);
        lv_obj_set_style_text_color(item, lv_color_hex(COLOR_TEXT), 0);
        lv_obj_set_style_text_align(item, LV_TEXT_ALIGN_CENTER, 0);
    }
}

void SetConnectHint(const char* text) {
    if (connectHint == nullptr) {
        return;
    }
    lv_label_set_text(connectHint, text);
}

void ApplyConnectView() {
    if (connectPanel == nullptr) {
        return;
    }

    lv_label_set_text(connectTitle, "WIFI");

    switch (connectView) {
        case CONNECT_VIEW_METHODS:
            lv_obj_clear_flag(connectList, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(connectWifiList, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(connectHint, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(connectUpButton, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(connectDownButton, LV_OBJ_FLAG_HIDDEN);
            break;
        case CONNECT_VIEW_WIFI_LIST:
            lv_obj_add_flag(connectList, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(connectWifiList, LV_OBJ_FLAG_HIDDEN);
            if (lv_label_get_text(connectHint)[0] == '\0') {
                lv_obj_add_flag(connectHint, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_clear_flag(connectHint, LV_OBJ_FLAG_HIDDEN);
            }
            lv_obj_clear_flag(connectUpButton, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(connectDownButton, LV_OBJ_FLAG_HIDDEN);
            break;
        case CONNECT_VIEW_PHONE_HELP:
            lv_obj_add_flag(connectList, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(connectWifiList, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(connectHint, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(connectUpButton, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(connectDownButton, LV_OBJ_FLAG_HIDDEN);
            break;
    }
}

void UpdateKeyboardCapsButtonStyle();

void ResetKeyboardState(const char* initial_text = "") {
    keyboardCaps = false;
    keyboardLastKey = '\0';
    keyboardLastIndex = 0;
    keyboardLastTapMs = 0;
    std::strncpy(keyboardText, initial_text, sizeof(keyboardText) - 1);
    keyboardText[sizeof(keyboardText) - 1] = '\0';
    keyboardLen = std::strlen(keyboardText);
    UpdateKeyboardCapsButtonStyle();
}

void UpdateKeyboardValue() {
    if (keyboardValue == nullptr || displayHandle == nullptr) {
        return;
    }
    DisplayLockGuard lock(displayHandle);
    lv_label_set_text(keyboardValue, keyboardText);
}

void UpdateKeyboardCapsButtonStyle() {
    if (displayHandle == nullptr || keyboardButtons[9] == nullptr) {
        return;
    }
    DisplayLockGuard lock(displayHandle);
    lv_obj_set_style_bg_color(
        keyboardButtons[9],
        lv_color_hex(keyboardCaps ? 0x2EBE4E : 0x1C2334),
        0);
}

const char* T9CharsForKey(char key) {
    switch (key) {
        case '1': return "1!@";
        case '2': return keyboardCaps ? "2ABC" : "2abc";
        case '3': return keyboardCaps ? "3DEF" : "3def";
        case '4': return keyboardCaps ? "4GHI" : "4ghi";
        case '5': return keyboardCaps ? "5JKL" : "5jkl";
        case '6': return keyboardCaps ? "6MNO" : "6mno";
        case '7': return keyboardCaps ? "7PQRS" : "7pqrs";
        case '8': return keyboardCaps ? "8TUV" : "8tuv";
        case '9': return keyboardCaps ? "9WXYZ" : "9wxyz";
        case '0': return "0";
        case '*': return "*";
        case '#': return "#";
        default: return "";
    }
}

void HandleKeyboardBackspace() {
    if (keyboardLen > 0) {
        --keyboardLen;
        keyboardText[keyboardLen] = '\0';
    }
    keyboardLastKey = '\0';
    keyboardLastIndex = 0;
    keyboardLastTapMs = 0;
    UpdateKeyboardValue();
}

void HandleT9Key(char key) {
    const uint32_t now = lv_tick_get();
    const char* chars = T9CharsForKey(key);
    const size_t chars_len = std::strlen(chars);
    if (chars_len == 0) {
        return;
    }

    if (key == keyboardLastKey && (now - keyboardLastTapMs) < T9_TAP_TIMEOUT_MS && keyboardLen > 0) {
        keyboardLastIndex = (keyboardLastIndex + 1) % chars_len;
        keyboardText[keyboardLen - 1] = chars[keyboardLastIndex];
    } else if (keyboardLen + 1 < sizeof(keyboardText)) {
        keyboardLastIndex = 0;
        keyboardText[keyboardLen++] = chars[keyboardLastIndex];
        keyboardText[keyboardLen] = '\0';
    }

    keyboardLastKey = key;
    keyboardLastTapMs = now;
    UpdateKeyboardValue();
}

void ScrollMenuToIndex(uint8_t idx, lv_anim_enable_t anim) {
    (void)anim; // no scrolling anymore -- kept for call-site compatibility
    if (!menuRollerCreated || idx >= MENU_ITEM_COUNT) {
        return;
    }
    selectedItem = static_cast<MenuItem>(idx);
    UpdateMenuItemStyles();
}

void ScrollCareToIndex(uint8_t idx, lv_anim_enable_t anim) {
    (void)anim; // no scrolling anymore -- kept for call-site compatibility
    if (careHeroImage == nullptr || idx >= CARE_ITEM_COUNT) {
        return;
    }
    selectedCareItem = static_cast<CareItem>(idx);
    UpdateCareItemStyles();
}

void ScrollConnectToIndex(uint8_t idx, lv_anim_enable_t anim) {
    if (connectList == nullptr || idx >= CONNECT_ITEM_COUNT) {
        return;
    }
    selectedConnectItem = static_cast<ConnectItem>(idx);
    UpdateConnectItemStyles();
    if (connectItems[idx] != nullptr) {
        lv_obj_scroll_to_view(connectItems[idx], anim);
    }
}

void ScrollWifiToIndex(size_t idx, lv_anim_enable_t anim) {
    if (connectWifiItems.empty() || idx >= connectWifiItems.size()) {
        return;
    }
    selectedWifiIndex = idx;
    UpdateWifiItemStyles();
    if (connectWifiItems[idx] != nullptr) {
        lv_obj_scroll_to_view(connectWifiItems[idx], anim);
    }
}

void UpdateStatsUI() {
    if (statsPanel == nullptr) {
        return;
    }

    lv_label_set_text(statsTitle, statNames[statIndex]);

    int value = 0;
    switch (statIndex) {
        case 0:
            value = CareSystem::GetHunger();
            break;
        case 1:
            value = CareSystem::GetMood();
            break;
        case 2:
            value = CareSystem::GetEnergy();
            break;
        case 3:
            value = CareSystem::GetCleanliness();
            break;
        default:
            break;
    }

    if (value < 0) {
        value = 0;
    } else if (value > 100) {
        value = 100;
    }

    lv_arc_set_value(statsArc, value);
    lv_obj_set_style_arc_color(statsArc, lv_color_hex(statColors[statIndex]), LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(statsArc, lv_color_hex(0x202020), LV_PART_MAIN);
}

uint32_t PickLevelArcColor(int level) {
    uint32_t seed = static_cast<uint32_t>(level <= 0 ? 1 : level);
    seed ^= (seed << 13);
    seed ^= (seed >> 17);
    seed ^= (seed << 5);
    return kLevelArcStrongColors[seed % kLevelArcStrongColors.size()];
}

void UpdateLevelUI() {
    if (levelPanel == nullptr || levelArc == nullptr || levelTitle == nullptr) {
        return;
    }

    const int level = std::max(1, LevelSystem::GetLevel());
    const int xp = std::max(0, LevelSystem::GetXP());
    const int xp_for_next = std::max(1, LevelSystem::GetXPForNextLevel());
    const int progress = std::clamp((xp * 100) / xp_for_next, 0, 100);

    char title[24];
    std::snprintf(title, sizeof(title), "LEVEL %d", level);
    lv_label_set_text(levelTitle, title);

    lv_arc_set_value(levelArc, progress);
    lv_obj_set_style_arc_color(levelArc, lv_color_hex(PickLevelArcColor(level)), LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(levelArc, lv_color_hex(0x202020), LV_PART_MAIN);
}

void ApplyCurrentStatAction() {
    switch (statIndex) {
        case 0:
            TriggerFeedingAnimation();
            break;
        case 1:
            MenuSystem::OpenGamesMenu();
            break;
        case 2:
            TryEnterSleepMode();
            break;
        case 3:
            TriggerBathAnimation();
            break;
        default:
            break;
    }
}

void HidePanel(lv_obj_t* panel) {
    if (panel != nullptr) {
        lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);
    }
}

void ShowPanel(lv_obj_t* panel) {
    if (panel != nullptr) {
        lv_obj_clear_flag(panel, LV_OBJ_FLAG_HIDDEN);
    }
}

void UpdateNotesItemStyles() {
    if (notesList == nullptr) {
        return;
    }

    for (size_t i = 0; i < notesItems.size(); ++i) {
        int dist = std::abs(static_cast<int>(i) - static_cast<int>(selectedNoteIndex));
        lv_obj_t* item = notesItems[i];
        if (item == nullptr) {
            continue;
        }

        const lv_font_t* font = dist == 0 ? &lv_font_montserrat_vn_22 : &lv_font_montserrat_vn_20;
        lv_opa_t opa = dist == 0 ? LV_OPA_COVER : (dist == 1 ? 200 : LV_OPA_40);
        lv_obj_set_style_text_font(item, font, 0);
        lv_obj_set_style_text_opa(item, opa, 0);
        lv_obj_set_style_text_color(item, lv_color_hex(COLOR_TEXT), 0);
        lv_obj_set_style_text_align(item, LV_TEXT_ALIGN_CENTER, 0);
    }
}

void ScrollNotesToIndex(size_t idx, lv_anim_enable_t anim) {
    if (notesItems.empty() || idx >= notesItems.size()) {
        return;
    }
    selectedNoteIndex = idx;
    UpdateNotesItemStyles();
    if (notesItems[idx] != nullptr) {
        lv_obj_scroll_to_view(notesItems[idx], anim);
    }
}

void RebuildNotesList() {
    if (notesList == nullptr) {
        return;
    }

    while (lv_obj_get_child_cnt(notesList) > 0) {
        lv_obj_delete(lv_obj_get_child(notesList, 0));
    }
    notesItems.clear();

    notesEntries = NotesSystem::List();
    if (notesEntries.empty()) {
        auto* label = lv_label_create(notesList);
        lv_label_set_text(label, "NO NOTES");
        lv_obj_set_width(label, lv_pct(100));
        lv_obj_set_style_pad_all(label, 8, 0);
        lv_obj_set_style_min_height(label, 28, 0);
        notesItems.push_back(label);
        selectedNoteIndex = 0;
        UpdateNotesItemStyles();
        return;
    }

    for (size_t i = 0; i < notesEntries.size(); ++i) {
        auto* label = lv_label_create(notesList);
        std::string row_text = std::to_string(i + 1) + ". " + notesEntries[i].key;
        lv_label_set_text(label, row_text.c_str());
        lv_obj_set_width(label, lv_pct(100));
        lv_obj_set_style_pad_all(label, 8, 0);
        lv_obj_set_style_min_height(label, 28, 0);
        notesItems.push_back(label);
    }

    if (selectedNoteIndex >= notesItems.size()) {
        selectedNoteIndex = 0;
    }
    ScrollNotesToIndex(selectedNoteIndex, LV_ANIM_OFF);
}

void ShowNoteDetailForCurrentSelection() {
    if (noteDetailTitle == nullptr || noteDetailValue == nullptr || noteDetailHint == nullptr) {
        return;
    }

    if (notesEntries.empty()) {
        lv_label_set_text(noteDetailTitle, "NO NOTES");
        lv_label_set_text(noteDetailValue, "Use remember(key, value) via MCP to store notes.");
        lv_label_set_text(noteDetailHint, "TAP/POWER: BACK");
        return;
    }

    if (selectedNoteIndex >= notesEntries.size()) {
        selectedNoteIndex = 0;
    }
    const auto& note = notesEntries[selectedNoteIndex];
    lv_label_set_text(noteDetailTitle, note.key.c_str());
    lv_label_set_text(noteDetailValue, note.value.c_str());
    lv_label_set_text(noteDetailHint, "UP/DOWN: NEXT NOTE  TAP/POWER: BACK");
}

const char* ReminderStateText(ReminderSystem::ReminderState state) {
    switch (state) {
        case ReminderSystem::ReminderState::kPending:
            return "PENDING";
        case ReminderSystem::ReminderState::kFiring:
            return "FIRING";
        case ReminderSystem::ReminderState::kSnoozed:
            return "SNOOZED";
        case ReminderSystem::ReminderState::kConfirmed:
            return "CONFIRMED";
        case ReminderSystem::ReminderState::kCancelled:
            return "CANCELLED";
        default:
            return "UNKNOWN";
    }
}

void UpdateRemindersItemStyles() {
    if (remindersList == nullptr) {
        return;
    }

    for (size_t i = 0; i < remindersItems.size(); ++i) {
        int dist = std::abs(static_cast<int>(i) - static_cast<int>(selectedReminderIndex));
        lv_obj_t* item = remindersItems[i];
        if (item == nullptr) {
            continue;
        }

        const lv_font_t* font = dist == 0 ? &lv_font_montserrat_vn_22 : &lv_font_montserrat_vn_20;
        lv_opa_t opa = dist == 0 ? LV_OPA_COVER : (dist == 1 ? 200 : LV_OPA_40);
        lv_obj_set_style_text_font(item, font, 0);
        lv_obj_set_style_text_opa(item, opa, 0);
        lv_obj_set_style_text_color(item, lv_color_hex(COLOR_TEXT), 0);
        lv_obj_set_style_text_align(item, LV_TEXT_ALIGN_CENTER, 0);
    }
}

void ScrollRemindersToIndex(size_t idx, lv_anim_enable_t anim) {
    if (remindersItems.empty() || idx >= remindersItems.size()) {
        return;
    }
    selectedReminderIndex = idx;
    UpdateRemindersItemStyles();
    if (remindersItems[idx] != nullptr) {
        lv_obj_scroll_to_view(remindersItems[idx], anim);
    }
}

void RebuildRemindersList() {
    if (remindersList == nullptr) {
        return;
    }

    while (lv_obj_get_child_cnt(remindersList) > 0) {
        lv_obj_delete(lv_obj_get_child(remindersList, 0));
    }
    remindersItems.clear();

    remindersEntries = ReminderSystem::List();
    if (remindersEntries.empty()) {
        auto* label = lv_label_create(remindersList);
        lv_label_set_text(label, "NO REMINDERS");
        lv_obj_set_width(label, lv_pct(100));
        lv_obj_set_style_pad_all(label, 8, 0);
        lv_obj_set_style_min_height(label, 28, 0);
        remindersItems.push_back(label);
        selectedReminderIndex = 0;
        UpdateRemindersItemStyles();
        return;
    }

    for (size_t i = 0; i < remindersEntries.size(); ++i) {
        auto* label = lv_label_create(remindersList);
        char time_text[8];
        std::snprintf(time_text, sizeof(time_text), "%02d:%02d",
                      remindersEntries[i].target_hour, remindersEntries[i].target_minute);
        std::string row_text = std::to_string(i + 1) + ". ";
        row_text += time_text;
        row_text += " ";
        row_text += remindersEntries[i].message;
        lv_label_set_text(label, row_text.c_str());
        lv_obj_set_width(label, lv_pct(100));
        lv_obj_set_style_pad_all(label, 8, 0);
        lv_obj_set_style_min_height(label, 28, 0);
        remindersItems.push_back(label);
    }

    if (selectedReminderIndex >= remindersItems.size()) {
        selectedReminderIndex = 0;
    }
    ScrollRemindersToIndex(selectedReminderIndex, LV_ANIM_OFF);
}

void ShowReminderDetailForCurrentSelection() {
    if (reminderDetailTitle == nullptr || reminderDetailValue == nullptr || reminderDetailHint == nullptr) {
        return;
    }

    if (remindersEntries.empty()) {
        lv_label_set_text(reminderDetailTitle, "NO REMINDERS");
        lv_label_set_text(reminderDetailValue, "Create reminders via AI.");
        lv_label_set_text(reminderDetailHint, "TAP/POWER: BACK");
        return;
    }

    if (selectedReminderIndex >= remindersEntries.size()) {
        selectedReminderIndex = 0;
    }
    const auto& reminder = remindersEntries[selectedReminderIndex];
    char title[32];
    std::snprintf(title, sizeof(title), "#%d %02d:%02d", static_cast<int>(reminder.id),
                  reminder.target_hour, reminder.target_minute);
    lv_label_set_text(reminderDetailTitle, title);

    std::string detail = reminder.message;
    detail += "\nState: ";
    detail += ReminderStateText(reminder.state);
    detail += "\nSnooze: ";
    detail += std::to_string(reminder.snooze_interval_min);
    detail += "m";
    if (reminder.snooze_count > 0) {
        detail += " (";
        detail += std::to_string(reminder.snooze_count);
        detail += ")";
    }
    lv_label_set_text(reminderDetailValue, detail.c_str());
    lv_label_set_text(reminderDetailHint, "UP/DOWN: NEXT  TAP/POWER: BACK");
}

lv_obj_t* CreateTextButton(lv_obj_t* parent, const char* text, lv_coord_t w, lv_coord_t h, const lv_font_t* font = &lv_font_montserrat_14) {
    auto* button = lv_btn_create(parent);
    lv_obj_set_size(button, w, h);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x101722), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(0x284552), 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_radius(button, 14, 0);

    auto* label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_center(label);
    return button;
}

void UpdateSettingsItemStyles() {
    for (int i = 0; i < SETTINGS_ITEM_COUNT; ++i) {
        if (settingsItems[i] == nullptr) {
            continue;
        }

        const bool selected = static_cast<int>(selectedSettingsItem) == i;
        lv_obj_set_style_text_color(settingsItems[i],
                                    selected ? lv_color_hex(COLOR_MINT) : lv_color_hex(COLOR_TEXT), 0);
        lv_obj_set_style_text_font(settingsItems[i],
                                   selected ? &lv_font_montserrat_vn_28 : &lv_font_montserrat_vn_22, 0);
        lv_obj_set_style_text_opa(settingsItems[i], selected ? LV_OPA_COVER : LV_OPA_80, 0);
    }
}

int GetOutputVolume() {
    auto* codec = Board::GetInstance().GetAudioCodec();
    return codec != nullptr ? codec->output_volume() : 0;
}

void UpdateVolumeValueLabel() {
    if (volumeValueLabel == nullptr) {
        return;
    }

    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%d", volumeWorkingValue);
    if (displayHandle == nullptr) {
        return;
    }
    DisplayLockGuard lock(displayHandle);
    lv_label_set_text(volumeValueLabel, buffer);
}

void PreviewOutputVolumeClamped(int volume) {
    auto* codec = Board::GetInstance().GetAudioCodec();
    if (codec == nullptr) {
        return;
    }

    const int clamped = std::clamp(volume, 0, 100);
    if (clamped == volumeWorkingValue) {
        return;
    }

    codec->PreviewOutputVolume(clamped);
    volumeWorkingValue = clamped;
    volumeDirty = true;
    UpdateVolumeValueLabel();
}

void CommitVolumeIfPending() {
    if (!volumeDirty) {
        return;
    }

    auto* codec = Board::GetInstance().GetAudioCodec();
    if (codec == nullptr) {
        volumeDirty = false;
        return;
    }

    codec->PersistOutputVolume();
    volumeDirty = false;
}

void OpenVolumePanel() {
    if (volumePanel == nullptr) {
        return;
    }

    volumeWorkingValue = GetOutputVolume();
    volumeDirty = false;
    UpdateVolumeValueLabel();
    DisplayLockGuard lock(displayHandle);
    HidePanel(settingsPanel);
    ShowPanel(volumePanel);
    SetMenuState(MENU_VOLUME_OPEN);
}

void HideAllPanels() {
    HidePanel(menuPanel);
    HidePanel(carePanel);
    HidePanel(connectPanel);
    HidePanel(keyboardPanel);
    HidePanel(notesPanel);
    HidePanel(noteDetailPanel);
    HidePanel(remindersPanel);
    HidePanel(reminderDetailPanel);
    HidePanel(statsPanel);
    HidePanel(gamesPanel);
    HidePanel(levelPanel);
    HidePanel(settingsPanel);
    HidePanel(volumePanel);
    HidePanel(pomodoroPanel);
}

void CreateCircularPanel() {
    if (menuPanel != nullptr) {
        return;
    }

    menuPanel = lv_obj_create(lv_screen_active());
    lv_obj_set_size(menuPanel, 240, 240);
    lv_obj_center(menuPanel);
    lv_obj_set_style_radius(menuPanel, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(menuPanel, lv_color_hex(COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(menuPanel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(menuPanel, 12, 0);
    lv_obj_set_style_border_color(menuPanel, lv_color_hex(COLOR_MINT), 0);
    lv_obj_set_style_border_opa(menuPanel, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(menuPanel, 0, 0);
    lv_obj_clear_flag(menuPanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(menuPanel, LV_OBJ_FLAG_HIDDEN);
}

void CreateMenuRoller() {
    if (menuRollerCreated) {
        return;
    }
    menuRollerCreated = true;

    for (int i = 0; i < MENU_ITEM_COUNT; ++i) {
        const LvglImage* icon = ResolvePersistentAssetImage(menuItemIconFiles[i], menuItemIcons[i]);
        menuItemIsIcon[i] = (icon != nullptr);
        if (icon == nullptr) {
            ESP_LOGW(TAG, "No menu image for item %d ('%s'), falling back to text",
                     i, menuItemLabelTexts[i]);
        }
    }

    // One full-screen image, source swapped on selection change instead of a
    // scrolling list -- the source PNGs are already 240x240 (the whole
    // screen), so they're just centered as-is, no scaling/cropping.
    menuHeroImage = lv_img_create(menuPanel);
    lv_obj_center(menuHeroImage);

    menuHeroLabel = lv_label_create(menuPanel);
    lv_obj_set_style_text_font(menuHeroLabel, &lv_font_montserrat_vn_22, 0);
    lv_obj_set_style_text_color(menuHeroLabel, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_align(menuHeroLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(menuHeroLabel, 180);
    lv_obj_center(menuHeroLabel);

    ScrollMenuToIndex(0, LV_ANIM_OFF);

    // Created last so they draw on top of the full-screen hero image instead
    // of being hidden underneath it.
    upButton = CreateNavButton(menuPanel, LV_ALIGN_TOP_MID, 0, -65, LV_SYMBOL_UP,
                               LV_ALIGN_CENTER, 0, 30,
                               [](lv_event_t*) { MenuSystem::SelectPrev(); });
    downButton = CreateNavButton(menuPanel, LV_ALIGN_BOTTOM_MID, 0, 65, LV_SYMBOL_DOWN,
                                 LV_ALIGN_CENTER, 0, -30,
                                 [](lv_event_t*) { MenuSystem::SelectNext(); });
}

void CreateCarePanel() {
    if (carePanel != nullptr) {
        return;
    }

    carePanel = lv_obj_create(lv_screen_active());
    lv_obj_set_size(carePanel, 240, 240);
    lv_obj_center(carePanel);
    lv_obj_set_style_radius(carePanel, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(carePanel, lv_color_hex(COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(carePanel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(carePanel, 12, 0);
    lv_obj_set_style_border_color(carePanel, lv_color_hex(COLOR_MINT), 0);
    lv_obj_set_style_border_opa(carePanel, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(carePanel, 0, 0);
    lv_obj_clear_flag(carePanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(carePanel, LV_OBJ_FLAG_HIDDEN);

    for (int i = 0; i < CARE_ITEM_COUNT; ++i) {
        const LvglImage* icon = ResolvePersistentAssetImage(careItemIconFiles[i], careItemIcons[i]);
        careItemIsIcon[i] = (icon != nullptr);
        if (icon == nullptr) {
            ESP_LOGW(TAG, "No care image for item %d ('%s'), falling back to text",
                     i, careItemLabelTexts[i]);
        }
    }

    // One full-screen image, source swapped on selection change instead of a
    // scrolling list -- the source PNGs are already 240x240 (the whole
    // screen), so they're just centered as-is, no scaling/cropping.
    careHeroImage = lv_img_create(carePanel);
    lv_obj_center(careHeroImage);

    careHeroLabel = lv_label_create(carePanel);
    lv_obj_set_style_text_font(careHeroLabel, &lv_font_montserrat_vn_22, 0);
    lv_obj_set_style_text_color(careHeroLabel, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_align(careHeroLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(careHeroLabel, 180);
    lv_obj_center(careHeroLabel);

    ScrollCareToIndex(static_cast<uint8_t>(selectedCareItem), LV_ANIM_OFF);

    // Created last so they draw on top of the full-screen hero image instead
    // of being hidden underneath it.
    careUpButton = CreateNavButton(carePanel, LV_ALIGN_TOP_MID, 0, -65, LV_SYMBOL_UP,
                                   LV_ALIGN_CENTER, 0, 30,
                                   [](lv_event_t*) { MenuSystem::SelectCarePrev(); });
    careDownButton = CreateNavButton(carePanel, LV_ALIGN_BOTTOM_MID, 0, 65, LV_SYMBOL_DOWN,
                                     LV_ALIGN_CENTER, 0, -30,
                                     [](lv_event_t*) { MenuSystem::SelectCareNext(); });
}

void CreateConnectPanel() {
    if (connectPanel != nullptr) {
        return;
    }

    connectPanel = lv_obj_create(lv_screen_active());
    lv_obj_set_size(connectPanel, 240, 240);
    lv_obj_center(connectPanel);
    lv_obj_set_style_radius(connectPanel, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(connectPanel, lv_color_hex(COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(connectPanel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(connectPanel, 12, 0);
    lv_obj_set_style_border_color(connectPanel, lv_color_hex(COLOR_MINT), 0);
    lv_obj_set_style_border_opa(connectPanel, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(connectPanel, 0, 0);
    lv_obj_clear_flag(connectPanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(connectPanel, LV_OBJ_FLAG_HIDDEN);

    connectTitle = lv_label_create(connectPanel);
    lv_label_set_text(connectTitle, "WIFI");
    lv_obj_set_style_text_color(connectTitle, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(connectTitle, &lv_font_montserrat_vn_20, 0);
    lv_obj_align(connectTitle, LV_ALIGN_TOP_MID, 0, 26);

    connectHint = lv_label_create(connectPanel);
    lv_label_set_text(connectHint, "");
    lv_obj_set_width(connectHint, 172);
    lv_obj_set_style_text_color(connectHint, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_opa(connectHint, 200, 0);
    lv_obj_set_style_text_font(connectHint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(connectHint, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(connectHint, LV_LABEL_LONG_WRAP);
    lv_obj_align(connectHint, LV_ALIGN_CENTER, 0, 24);
    lv_obj_add_flag(connectHint, LV_OBJ_FLAG_HIDDEN);

    connectList = lv_obj_create(connectPanel);
    lv_obj_set_size(connectList, 190, 92);
    lv_obj_align(connectList, LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_scroll_dir(connectList, LV_DIR_VER);
    lv_obj_set_scroll_snap_y(connectList, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(connectList, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(connectList, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_pad_all(connectList, 0, 0);
    lv_obj_set_style_pad_row(connectList, 10, 0);
    lv_obj_set_style_bg_opa(connectList, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(connectList, 0, 0);
    lv_obj_set_flex_flow(connectList, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(connectList, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);

    for (int i = 0; i < CONNECT_ITEM_COUNT; ++i) {
        lv_obj_t* label = lv_label_create(connectList);
        connectItems[i] = label;
        lv_label_set_text(label, connectItemLabelTexts[i]);
        lv_obj_set_width(label, lv_pct(100));
        lv_obj_set_style_pad_all(label, 8, 0);
        lv_obj_set_style_min_height(label, 30, 0);
    }

    ScrollConnectToIndex(static_cast<uint8_t>(selectedConnectItem), LV_ANIM_OFF);

    connectWifiList = lv_obj_create(connectPanel);
    lv_obj_set_size(connectWifiList, 190, 110);
    lv_obj_align(connectWifiList, LV_ALIGN_CENTER, 0, 28);
    lv_obj_set_scroll_dir(connectWifiList, LV_DIR_VER);
    lv_obj_set_scroll_snap_y(connectWifiList, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(connectWifiList, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(connectWifiList, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_pad_all(connectWifiList, 0, 0);
    lv_obj_set_style_pad_row(connectWifiList, 6, 0);
    lv_obj_set_style_bg_opa(connectWifiList, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(connectWifiList, 0, 0);
    lv_obj_set_flex_flow(connectWifiList, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(connectWifiList, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_add_flag(connectWifiList, LV_OBJ_FLAG_HIDDEN);

    connectUpButton = CreateNavButton(connectPanel, LV_ALIGN_TOP_MID, 0, -65, LV_SYMBOL_UP,
                                      LV_ALIGN_CENTER, 0, 30,
                                      [](lv_event_t*) { MenuSystem::NavigatePrev(); });
    connectDownButton = CreateNavButton(connectPanel, LV_ALIGN_BOTTOM_MID, 0, 65, LV_SYMBOL_DOWN,
                                        LV_ALIGN_CENTER, 0, -30,
                                        [](lv_event_t*) { MenuSystem::NavigateNext(); });

    ApplyConnectView();
}

void RebuildWifiList() {
    if (connectWifiList == nullptr) {
        return;
    }

    while (lv_obj_get_child_cnt(connectWifiList) > 0) {
        lv_obj_delete(lv_obj_get_child(connectWifiList, 0));
    }
    connectWifiItems.clear();

    const auto& wifi_service = WifiConnectService::GetInstance();
    const bool scanning = wifi_service.IsScanning();
    const auto results = wifi_service.GetScanResults();

    if (scanning) {
        SetConnectHint("SCANNING....");
        lv_obj_clear_flag(connectHint, LV_OBJ_FLAG_HIDDEN);
        selectedWifiIndex = 0;
        return;
    }

    SetConnectHint("");
    lv_obj_add_flag(connectHint, LV_OBJ_FLAG_HIDDEN);

    if (results.empty()) {
        auto* label = lv_label_create(connectWifiList);
        lv_label_set_text(label, "KHÔNG CÓ WIFI");
        lv_obj_set_width(label, lv_pct(100));
        lv_obj_set_style_text_color(label, lv_color_hex(COLOR_TEXT), 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        connectWifiItems.push_back(label);
        selectedWifiIndex = 0;
        return;
    }

    for (const auto& result : results) {
        auto* label = lv_label_create(connectWifiList);
        std::string text = result.ssid + (result.known ? " *" : "");
        lv_label_set_text(label, text.c_str());
        lv_obj_set_width(label, lv_pct(100));
        lv_obj_set_style_pad_all(label, 8, 0);
        lv_obj_set_style_min_height(label, 28, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(COLOR_TEXT), 0);
        connectWifiItems.push_back(label);
    }

    if (selectedWifiIndex >= connectWifiItems.size()) {
        selectedWifiIndex = 0;
    }
    UpdateWifiItemStyles();
    ScrollWifiToIndex(selectedWifiIndex, LV_ANIM_OFF);
}

void CreateKeyboardPanel() {
    if (keyboardPanel != nullptr) {
        return;
    }

    keyboardPanel = lv_obj_create(lv_screen_active());
    lv_obj_set_size(keyboardPanel, 240, 240);
    lv_obj_center(keyboardPanel);
    lv_obj_set_style_radius(keyboardPanel, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(keyboardPanel, lv_color_hex(COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(keyboardPanel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(keyboardPanel, 0, 0);
    lv_obj_set_style_border_opa(keyboardPanel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(keyboardPanel, 0, 0);
    lv_obj_clear_flag(keyboardPanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(keyboardPanel, LV_OBJ_FLAG_HIDDEN);

    // No separate "PASSWORD" title — it only ever showed near the top edge
    // of the round bezel, where the circle is narrowest, so it read as
    // clipped/cramped. Dropping it also frees ~24px of vertical room for
    // bigger keys below.
    keyboardSsid = lv_label_create(keyboardPanel);
    lv_label_set_text(keyboardSsid, "");
    lv_obj_set_width(keyboardSsid, 180);
    lv_obj_set_style_text_color(keyboardSsid, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_align(keyboardSsid, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(keyboardSsid, &lv_font_montserrat_14, 0);
    lv_obj_align(keyboardSsid, LV_ALIGN_TOP_MID, 0, 2);

    keyboardValue = lv_label_create(keyboardPanel);
    lv_label_set_text(keyboardValue, "");
    lv_obj_set_width(keyboardValue, 180);
    lv_label_set_long_mode(keyboardValue, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(keyboardValue, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_align(keyboardValue, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(keyboardValue, &lv_font_montserrat_14, 0);
    lv_obj_align(keyboardValue, LV_ALIGN_TOP_MID, 0, 18);

    // The panel is a 240x240 circle (round display), so a uniform 3-column
    // grid gets its outer columns clipped by the bezel in the rows farthest
    // from vertical center. Each row's button width below is the widest that
    // still keeps all 3 buttons fully inside the circular safe area at that
    // row's vertical position, so every key is both bigger and fully usable.
    // OK sits alone below the grid, narrower than the grid rows (it doesn't
    // need to be as wide as a 3-column row), so it can sit lower and leave
    // more of the header's freed-up vertical space to the keys above.
    constexpr lv_coord_t kButtonHeight = 32;
    constexpr lv_coord_t kGapX = 4;
    constexpr lv_coord_t kGapY = 4;
    constexpr std::array<lv_coord_t, 4> kRowButtonWidths = {58, 71, 73, 62};
    constexpr lv_coord_t kGridWidth = 73 * 3 + kGapX * 2;
    constexpr lv_coord_t kGridHeight = kButtonHeight * 4 + kGapY * 3;
    constexpr lv_coord_t kGridTopOffset = 46;
    constexpr size_t kGridButtonCount = 12;
    constexpr uint32_t kKeyBgColor = 0x1C2334;
    constexpr uint32_t kKeyBorderColor = 0x38415A;

    keyboardGrid = lv_obj_create(keyboardPanel);
    lv_obj_set_size(keyboardGrid, kGridWidth, kGridHeight);
    lv_obj_align(keyboardGrid, LV_ALIGN_TOP_MID, 0, kGridTopOffset);
    lv_obj_set_style_bg_opa(keyboardGrid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(keyboardGrid, 0, 0);
    lv_obj_set_style_pad_all(keyboardGrid, 0, 0);
    lv_obj_clear_flag(keyboardGrid, LV_OBJ_FLAG_SCROLLABLE);

    for (size_t i = 0; i < kGridButtonCount; ++i) {
        auto* button = lv_btn_create(keyboardGrid);
        keyboardButtons[i] = button;

        const int row = static_cast<int>(i / 3);
        const int col = static_cast<int>(i % 3);
        const lv_coord_t buttonWidth = kRowButtonWidths[row];
        lv_obj_set_size(button, buttonWidth, kButtonHeight);

        const lv_coord_t rowWidth = buttonWidth * 3 + kGapX * 2;
        const lv_coord_t rowXOffset = (kGridWidth - rowWidth) / 2;
        const lv_coord_t x = rowXOffset + col * (buttonWidth + kGapX);
        const lv_coord_t y = row * (kButtonHeight + kGapY);
        lv_obj_set_pos(button, x, y);
        lv_obj_set_style_bg_color(button, lv_color_hex(kKeyBgColor), 0);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(button, 1, 0);
        lv_obj_set_style_border_color(button, lv_color_hex(kKeyBorderColor), 0);
        lv_obj_set_style_border_opa(button, LV_OPA_COVER, 0);
        lv_obj_set_style_shadow_width(button, 0, 0);
        lv_obj_set_style_radius(button, 12, 0);

        auto* label = lv_label_create(button);
        lv_label_set_text(label, keyboardButtonTexts[i]);
        lv_obj_set_style_text_color(label, lv_color_hex(COLOR_TEXT), 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
        lv_obj_center(label);
    }

    constexpr lv_coord_t kOkButtonWidth = 100;
    constexpr lv_coord_t kOkButtonHeight = 32;
    constexpr lv_coord_t kOkButtonTopOffset = kGridTopOffset + kGridHeight + 8;
    constexpr size_t kOkButtonIndex = 12;

    auto* okButton = lv_btn_create(keyboardPanel);
    keyboardButtons[kOkButtonIndex] = okButton;
    lv_obj_set_size(okButton, kOkButtonWidth, kOkButtonHeight);
    lv_obj_align(okButton, LV_ALIGN_TOP_MID, 0, kOkButtonTopOffset);
    lv_obj_set_style_bg_color(okButton, lv_color_hex(kKeyBgColor), 0);
    lv_obj_set_style_bg_opa(okButton, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(okButton, 1, 0);
    lv_obj_set_style_border_color(okButton, lv_color_hex(kKeyBorderColor), 0);
    lv_obj_set_style_border_opa(okButton, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(okButton, 0, 0);
    lv_obj_set_style_radius(okButton, 16, 0);

    auto* okLabel = lv_label_create(okButton);
    lv_label_set_text(okLabel, keyboardButtonTexts[kOkButtonIndex]);
    lv_obj_set_style_text_color(okLabel, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(okLabel, &lv_font_montserrat_14, 0);
    lv_obj_center(okLabel);

    UpdateKeyboardCapsButtonStyle();
}

void CreateSettingsPanel() {
    if (settingsPanel != nullptr) {
        return;
    }

    settingsPanel = lv_obj_create(lv_screen_active());
    lv_obj_set_size(settingsPanel, 240, 240);
    lv_obj_center(settingsPanel);
    lv_obj_set_style_radius(settingsPanel, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(settingsPanel, lv_color_hex(COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(settingsPanel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(settingsPanel, 12, 0);
    lv_obj_set_style_border_color(settingsPanel, lv_color_hex(COLOR_MINT), 0);
    lv_obj_set_style_border_opa(settingsPanel, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(settingsPanel, 0, 0);
    lv_obj_clear_flag(settingsPanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(settingsPanel, LV_OBJ_FLAG_HIDDEN);

    settingsTitle = lv_label_create(settingsPanel);
    lv_label_set_text(settingsTitle, "SETTINGS");
    lv_obj_set_style_text_color(settingsTitle, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(settingsTitle, &lv_font_montserrat_vn_20, 0);
    lv_obj_align(settingsTitle, LV_ALIGN_TOP_MID, 0, 44);

    settingsItems[SETTINGS_VOLUME] = lv_label_create(settingsPanel);
    lv_label_set_text(settingsItems[SETTINGS_VOLUME], "VOLUME");
    lv_obj_set_width(settingsItems[SETTINGS_VOLUME], 180);
    lv_obj_set_style_text_align(settingsItems[SETTINGS_VOLUME], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(settingsItems[SETTINGS_VOLUME], LV_ALIGN_CENTER, 0, 0);

    UpdateSettingsItemStyles();
}

void CreateNotesPanel() {
    if (notesPanel != nullptr) {
        return;
    }

    notesPanel = lv_obj_create(lv_screen_active());
    lv_obj_set_size(notesPanel, 240, 240);
    lv_obj_center(notesPanel);
    lv_obj_set_style_radius(notesPanel, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(notesPanel, lv_color_hex(COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(notesPanel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(notesPanel, 12, 0);
    lv_obj_set_style_border_color(notesPanel, lv_color_hex(COLOR_MINT), 0);
    lv_obj_set_style_border_opa(notesPanel, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(notesPanel, 0, 0);
    lv_obj_clear_flag(notesPanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(notesPanel, LV_OBJ_FLAG_HIDDEN);

    notesTitle = lv_label_create(notesPanel);
    lv_label_set_text(notesTitle, "NOTES");
    lv_obj_set_style_text_color(notesTitle, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(notesTitle, &lv_font_montserrat_vn_20, 0);
    lv_obj_align(notesTitle, LV_ALIGN_TOP_MID, 0, 26);

    notesList = lv_obj_create(notesPanel);
    lv_obj_set_size(notesList, 190, 110);
    lv_obj_align(notesList, LV_ALIGN_CENTER, 0, 22);
    lv_obj_set_scroll_dir(notesList, LV_DIR_VER);
    lv_obj_set_scroll_snap_y(notesList, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(notesList, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(notesList, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_pad_all(notesList, 0, 0);
    lv_obj_set_style_pad_row(notesList, 6, 0);
    lv_obj_set_style_bg_opa(notesList, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(notesList, 0, 0);
    lv_obj_set_flex_flow(notesList, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(notesList, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);

    notesUpButton = CreateNavButton(notesPanel, LV_ALIGN_TOP_MID, 0, -65, LV_SYMBOL_UP,
                                    LV_ALIGN_CENTER, 0, 30,
                                    [](lv_event_t*) { MenuSystem::SelectNotesPrev(); });
    notesDownButton = CreateNavButton(notesPanel, LV_ALIGN_BOTTOM_MID, 0, 65, LV_SYMBOL_DOWN,
                                      LV_ALIGN_CENTER, 0, -30,
                                      [](lv_event_t*) { MenuSystem::SelectNotesNext(); });

    noteDetailPanel = lv_obj_create(lv_screen_active());
    lv_obj_set_size(noteDetailPanel, 240, 240);
    lv_obj_center(noteDetailPanel);
    lv_obj_set_style_radius(noteDetailPanel, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(noteDetailPanel, lv_color_hex(COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(noteDetailPanel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(noteDetailPanel, 12, 0);
    lv_obj_set_style_border_color(noteDetailPanel, lv_color_hex(COLOR_MINT), 0);
    lv_obj_set_style_border_opa(noteDetailPanel, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(noteDetailPanel, 0, 0);
    lv_obj_clear_flag(noteDetailPanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(noteDetailPanel, LV_OBJ_FLAG_HIDDEN);

    noteDetailTitle = lv_label_create(noteDetailPanel);
    lv_label_set_text(noteDetailTitle, "NOTE");
    lv_obj_set_width(noteDetailTitle, 180);
    lv_obj_set_style_text_color(noteDetailTitle, lv_color_hex(COLOR_MINT), 0);
    lv_obj_set_style_text_font(noteDetailTitle, &lv_font_montserrat_vn_20, 0);
    lv_obj_set_style_text_align(noteDetailTitle, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(noteDetailTitle, LV_LABEL_LONG_DOT);
    lv_obj_align(noteDetailTitle, LV_ALIGN_TOP_MID, 0, 22);

    noteDetailValue = lv_label_create(noteDetailPanel);
    lv_label_set_text(noteDetailValue, "");
    lv_obj_set_width(noteDetailValue, 176);
    lv_obj_set_style_text_color(noteDetailValue, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(noteDetailValue, &lv_font_montserrat_vn_20, 0);
    lv_obj_set_style_text_align(noteDetailValue, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(noteDetailValue, LV_LABEL_LONG_WRAP);
    lv_obj_align(noteDetailValue, LV_ALIGN_CENTER, 0, 8);

    noteDetailHint = lv_label_create(noteDetailPanel);
    lv_label_set_text(noteDetailHint, "TAP/POWER: BACK");
    lv_obj_set_width(noteDetailHint, 186);
    lv_obj_set_style_text_color(noteDetailHint, lv_color_hex(0xA8BBC2), 0);
    lv_obj_set_style_text_font(noteDetailHint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(noteDetailHint, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(noteDetailHint, LV_LABEL_LONG_WRAP);
    lv_obj_align(noteDetailHint, LV_ALIGN_BOTTOM_MID, 0, -20);

    RebuildNotesList();
}

void CreateRemindersPanel() {
    if (remindersPanel != nullptr) {
        return;
    }

    remindersPanel = lv_obj_create(lv_screen_active());
    lv_obj_set_size(remindersPanel, 240, 240);
    lv_obj_center(remindersPanel);
    lv_obj_set_style_radius(remindersPanel, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(remindersPanel, lv_color_hex(COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(remindersPanel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(remindersPanel, 12, 0);
    lv_obj_set_style_border_color(remindersPanel, lv_color_hex(COLOR_MINT), 0);
    lv_obj_set_style_border_opa(remindersPanel, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(remindersPanel, 0, 0);
    lv_obj_clear_flag(remindersPanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(remindersPanel, LV_OBJ_FLAG_HIDDEN);

    remindersTitle = lv_label_create(remindersPanel);
    lv_label_set_text(remindersTitle, "REMINDERS");
    lv_obj_set_style_text_color(remindersTitle, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(remindersTitle, &lv_font_montserrat_vn_20, 0);
    lv_obj_align(remindersTitle, LV_ALIGN_TOP_MID, 0, 26);

    remindersList = lv_obj_create(remindersPanel);
    lv_obj_set_size(remindersList, 190, 110);
    lv_obj_align(remindersList, LV_ALIGN_CENTER, 0, 22);
    lv_obj_set_scroll_dir(remindersList, LV_DIR_VER);
    lv_obj_set_scroll_snap_y(remindersList, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_scrollbar_mode(remindersList, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(remindersList, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_style_pad_all(remindersList, 0, 0);
    lv_obj_set_style_pad_row(remindersList, 6, 0);
    lv_obj_set_style_bg_opa(remindersList, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(remindersList, 0, 0);
    lv_obj_set_flex_flow(remindersList, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(remindersList, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);

    remindersUpButton = CreateNavButton(remindersPanel, LV_ALIGN_TOP_MID, 0, -65, LV_SYMBOL_UP,
                                        LV_ALIGN_CENTER, 0, 30,
                                        [](lv_event_t*) { MenuSystem::SelectRemindersPrev(); });
    remindersDownButton = CreateNavButton(remindersPanel, LV_ALIGN_BOTTOM_MID, 0, 65, LV_SYMBOL_DOWN,
                                          LV_ALIGN_CENTER, 0, -30,
                                          [](lv_event_t*) { MenuSystem::SelectRemindersNext(); });

    reminderDetailPanel = lv_obj_create(lv_screen_active());
    lv_obj_set_size(reminderDetailPanel, 240, 240);
    lv_obj_center(reminderDetailPanel);
    lv_obj_set_style_radius(reminderDetailPanel, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(reminderDetailPanel, lv_color_hex(COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(reminderDetailPanel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(reminderDetailPanel, 12, 0);
    lv_obj_set_style_border_color(reminderDetailPanel, lv_color_hex(COLOR_MINT), 0);
    lv_obj_set_style_border_opa(reminderDetailPanel, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(reminderDetailPanel, 0, 0);
    lv_obj_clear_flag(reminderDetailPanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(reminderDetailPanel, LV_OBJ_FLAG_HIDDEN);

    reminderDetailTitle = lv_label_create(reminderDetailPanel);
    lv_label_set_text(reminderDetailTitle, "REMINDER");
    lv_obj_set_width(reminderDetailTitle, 180);
    lv_obj_set_style_text_color(reminderDetailTitle, lv_color_hex(COLOR_MINT), 0);
    lv_obj_set_style_text_font(reminderDetailTitle, &lv_font_montserrat_vn_20, 0);
    lv_obj_set_style_text_align(reminderDetailTitle, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(reminderDetailTitle, LV_LABEL_LONG_DOT);
    lv_obj_align(reminderDetailTitle, LV_ALIGN_TOP_MID, 0, 22);

    reminderDetailValue = lv_label_create(reminderDetailPanel);
    lv_label_set_text(reminderDetailValue, "");
    lv_obj_set_width(reminderDetailValue, 176);
    lv_obj_set_style_text_color(reminderDetailValue, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(reminderDetailValue, &lv_font_montserrat_vn_20, 0);
    lv_obj_set_style_text_align(reminderDetailValue, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(reminderDetailValue, LV_LABEL_LONG_WRAP);
    lv_obj_align(reminderDetailValue, LV_ALIGN_CENTER, 0, 8);

    reminderDetailHint = lv_label_create(reminderDetailPanel);
    lv_label_set_text(reminderDetailHint, "TAP/POWER: BACK");
    lv_obj_set_width(reminderDetailHint, 186);
    lv_obj_set_style_text_color(reminderDetailHint, lv_color_hex(0xA8BBC2), 0);
    lv_obj_set_style_text_font(reminderDetailHint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(reminderDetailHint, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(reminderDetailHint, LV_LABEL_LONG_WRAP);
    lv_obj_align(reminderDetailHint, LV_ALIGN_BOTTOM_MID, 0, -20);

    RebuildRemindersList();
}

void CreateVolumePanel() {
    if (volumePanel != nullptr) {
        return;
    }

    volumePanel = lv_obj_create(lv_screen_active());
    lv_obj_set_size(volumePanel, 240, 240);
    lv_obj_center(volumePanel);
    lv_obj_set_style_radius(volumePanel, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(volumePanel, lv_color_hex(COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(volumePanel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(volumePanel, 12, 0);
    lv_obj_set_style_border_color(volumePanel, lv_color_hex(COLOR_MINT), 0);
    lv_obj_set_style_border_opa(volumePanel, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(volumePanel, 0, 0);
    lv_obj_clear_flag(volumePanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(volumePanel, LV_OBJ_FLAG_HIDDEN);

    volumeTitle = lv_label_create(volumePanel);
    lv_label_set_text(volumeTitle, "VOLUME");
    lv_obj_set_style_text_color(volumeTitle, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(volumeTitle, &lv_font_montserrat_vn_20, 0);
    lv_obj_align(volumeTitle, LV_ALIGN_TOP_MID, 0, 34);

    volumeValueLabel = lv_label_create(volumePanel);
    lv_label_set_text(volumeValueLabel, "0");
    lv_obj_set_style_text_color(volumeValueLabel, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(volumeValueLabel, &lv_font_montserrat_vn_28, 0);
    lv_obj_align(volumeValueLabel, LV_ALIGN_CENTER, 0, -8);

    volumeMinusButton = CreateTextButton(volumePanel, "-", 58, 42, &lv_font_montserrat_vn_22);
    lv_obj_align(volumeMinusButton, LV_ALIGN_CENTER, -70, -6);

    volumePlusButton = CreateTextButton(volumePanel, "+", 58, 42, &lv_font_montserrat_vn_22);
    lv_obj_align(volumePlusButton, LV_ALIGN_CENTER, 70, -6);

    volumeBackButton = CreateTextButton(volumePanel, "BACK", 72, 30);
    lv_obj_align(volumeBackButton, LV_ALIGN_BOTTOM_MID, 0, -22);

    UpdateVolumeValueLabel();
}

void CreateStatsPanel() {
    if (statsPanel != nullptr) {
        return;
    }

    statsPanel = lv_obj_create(lv_screen_active());
    lv_obj_set_size(statsPanel, 240, 240);
    lv_obj_center(statsPanel);
    lv_obj_set_style_radius(statsPanel, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(statsPanel, lv_color_hex(COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(statsPanel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_opa(statsPanel, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(statsPanel, 0, 0);
    lv_obj_clear_flag(statsPanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(statsPanel, LV_OBJ_FLAG_HIDDEN);

    statsTitle = lv_label_create(statsPanel);
    lv_obj_set_style_text_color(statsTitle, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(statsTitle, &lv_font_montserrat_vn_22, 0);
    lv_label_set_text(statsTitle, statNames[0]);
    lv_obj_align(statsTitle, LV_ALIGN_CENTER, 0, 0);

    statsArc = lv_arc_create(statsPanel);
    lv_obj_set_size(statsArc, 240, 240);
    lv_obj_center(statsArc);
    lv_arc_set_rotation(statsArc, 135);
    lv_arc_set_bg_angles(statsArc, 0, 270);
    lv_arc_set_mode(statsArc, LV_ARC_MODE_NORMAL);
    lv_arc_set_range(statsArc, 0, 100);
    lv_obj_clear_flag(statsArc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_style(statsArc, nullptr, LV_PART_KNOB);
    lv_obj_set_style_arc_width(statsArc, 16, LV_PART_MAIN);
    lv_obj_set_style_arc_width(statsArc, 16, LV_PART_INDICATOR);

    statsLeftBtn = CreateNavButton(statsPanel, LV_ALIGN_LEFT_MID, -50, 0, LV_SYMBOL_LEFT,
                                   LV_ALIGN_CENTER, 28, 0,
                                   [](lv_event_t*) { MenuSystem::StatsPrev(); });
    statsRightBtn = CreateNavButton(statsPanel, LV_ALIGN_RIGHT_MID, 50, 0, LV_SYMBOL_RIGHT,
                                    LV_ALIGN_CENTER, -28, 0,
                                    [](lv_event_t*) { MenuSystem::StatsNext(); });

    lv_obj_move_background(statsLeftBtn);
    lv_obj_move_background(statsRightBtn);

    statsActionZone = lv_obj_create(statsPanel);
    lv_obj_set_size(statsActionZone, 120, 120);
    lv_obj_center(statsActionZone);
    lv_obj_set_style_bg_opa(statsActionZone, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(statsActionZone, LV_OPA_TRANSP, 0);
    lv_obj_set_style_outline_opa(statsActionZone, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(statsActionZone, 0, 0);
    lv_obj_clear_flag(statsActionZone, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(statsActionZone, LV_OBJ_FLAG_CLICKABLE);

    UpdateStatsUI();
}

void UpdateGamesUI() {
    if (gamesAction == nullptr || gamesStatus == nullptr ||
        gamesRing == nullptr || gamesEmblem == nullptr) {
        return;
    }

    lv_obj_set_style_text_color(gamesAction, lv_color_hex(gamesActionColor), 0);
    lv_obj_set_style_text_color(gamesStatus, lv_color_hex(gamesStatusColor), 0);

    if (currentState == MENU_GAMES_OPEN) {
        const uint32_t accent = gamesActionColor;

        // The ring is this screen's edge, so the panel's own border comes off.
        // The two game screens below put it back.
        lv_obj_set_style_border_width(gamesPanel, 0, 0);
        lv_obj_set_style_bg_color(gamesPanel, lv_color_hex(COLOR_BACKGROUND), 0);

        const int segment = GamesSegmentStart(static_cast<int>(selectedGame));
        lv_arc_set_angles(gamesRing, segment, segment + kGamesSegmentSpan);
        lv_obj_set_style_arc_color(gamesRing, lv_color_hex(accent), LV_PART_INDICATOR);
        lv_obj_remove_flag(gamesRing, LV_OBJ_FLAG_HIDDEN);

        lv_obj_set_style_bg_color(gamesEmblem, lv_color_hex(accent), 0);
        lv_obj_set_style_border_color(gamesEmblem, lv_color_hex(accent), 0);
        lv_obj_remove_flag(gamesEmblem, LV_OBJ_FLAG_HIDDEN);
        for (int i = 0; i < GAME_SELECTION_COUNT; ++i) {
            if (gamesEmblemMarks[i] == nullptr) {
                continue;
            }
            if (i == static_cast<int>(selectedGame)) {
                lv_obj_remove_flag(gamesEmblemMarks[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(gamesEmblemMarks[i], LV_OBJ_FLAG_HIDDEN);
            }
        }

        lv_obj_remove_flag(gamesAction, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(gamesAction, GetSelectedGameTitle());
        lv_obj_set_style_text_font(gamesAction, &lv_font_montserrat_vn_22, 0);
        lv_obj_set_style_bg_opa(gamesAction, LV_OPA_TRANSP, 0);
        lv_obj_set_width(gamesAction, 240);
        lv_obj_align(gamesAction, LV_ALIGN_TOP_MID, 0, kGamesNameTop);

        lv_obj_set_style_bg_opa(gamesStatus, LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_font(gamesStatus, &lv_font_montserrat_vn_20, 0);
        lv_obj_set_style_pad_top(gamesStatus, 0, 0);
        lv_obj_set_style_pad_bottom(gamesStatus, 0, 0);
        lv_obj_set_width(gamesStatus, 240);
        lv_obj_align(gamesStatus, LV_ALIGN_TOP_MID, 0, kGamesChipTop);

        for (lv_obj_t* button : {gamesPrevBtn, gamesNextBtn}) {
            if (button == nullptr) {
                continue;
            }
            lv_obj_remove_flag(button, LV_OBJ_FLAG_HIDDEN);
            lv_obj_t* glyph = lv_obj_get_child(button, 0);
            if (glyph != nullptr) {
                lv_obj_set_style_text_color(glyph, lv_color_hex(accent), 0);
                lv_obj_set_style_text_opa(glyph, LV_OPA_50, 0);
            }
        }
        if (checkerGrid != nullptr) {
            lv_obj_add_flag(checkerGrid, LV_OBJ_FLAG_HIDDEN);
        }
        if (checkerBotIcon != nullptr) {
            lv_obj_add_flag(checkerBotIcon, LV_OBJ_FLAG_HIDDEN);
        }
        HideQuickTapAll();
        HideSnakeAll();
        HideTiltMazeAll();
        HideTrafficRunnerAll();
        HideGreenEyeAll();
    } else if (currentState == MENU_GAME_ACTIVE &&
               activeGame == ACTIVE_GAME_TRAFFIC_RUNNER) {
        lv_obj_set_style_bg_color(gamesPanel, lv_color_hex(kRunnerPanelBg), 0);
        lv_obj_set_style_border_width(gamesPanel, 0, 0);
        for (lv_obj_t* obj : {gamesRing, gamesEmblem, gamesAction, gamesStatus, gamesPrevBtn,
                              gamesNextBtn, checkerGrid, checkerBotIcon, checkerDotsRow,
                              checkerTitleScreen, checkerMatchupScreen,
                              checkerPlayAgainScreen, checkerFx}) {
            if (obj != nullptr) {
                lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
            }
        }
        HideQuickTapAll();
        HideSnakeAll();
        HideTiltMazeAll();
        UpdateTrafficRunnerUI();
    } else if (currentState == MENU_GAME_ACTIVE && activeGame == ACTIVE_GAME_TILT_MAZE) {
        lv_obj_set_style_bg_color(gamesPanel, lv_color_hex(kMazePanelBg), 0);
        lv_obj_set_style_border_width(gamesPanel, 0, 0);
        for (lv_obj_t* obj : {gamesRing, gamesEmblem, gamesAction, gamesStatus, gamesPrevBtn,
                              gamesNextBtn, checkerGrid, checkerBotIcon, checkerDotsRow,
                              checkerTitleScreen, checkerMatchupScreen,
                              checkerPlayAgainScreen, checkerFx}) {
            if (obj != nullptr) {
                lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
            }
        }
        HideQuickTapAll();
        HideSnakeAll();
        HideTrafficRunnerAll();
        UpdateTiltMazeUI();
    } else if (currentState == MENU_GAME_ACTIVE && activeGame == ACTIVE_GAME_SNAKE) {
        // Snake owns the whole panel, same deal as Quick Tap: its own ground,
        // no carousel chrome, and ShowSnakeScreen() drives what is visible.
        lv_obj_set_style_bg_color(gamesPanel, lv_color_hex(kSnakePanelBg), 0);
        lv_obj_set_style_border_width(gamesPanel, 8, 0);
        lv_obj_set_style_border_color(gamesPanel, lv_color_hex(kSnakePanelRing), 0);
        for (lv_obj_t* obj : {gamesRing, gamesEmblem, gamesAction, gamesStatus, gamesPrevBtn,
                              gamesNextBtn, checkerGrid, checkerBotIcon, checkerDotsRow,
                              checkerTitleScreen, checkerMatchupScreen,
                              checkerPlayAgainScreen, checkerFx}) {
            if (obj != nullptr) {
                lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
            }
        }
        HideQuickTapAll();
        HideTiltMazeAll();
        HideTrafficRunnerAll();
    } else if (currentState == MENU_GAME_ACTIVE && activeGame == ACTIVE_GAME_GREEN_EYE) {
        // MẮT XANH owns the whole panel the way Quick Tap does: black ground so
        // the colours carry, no carousel chrome, and ShowGreenEyeScreen()
        // drives what is visible.
        lv_obj_set_style_bg_color(gamesPanel, lv_color_hex(kGreenEyePanelBg), 0);
        lv_obj_set_style_border_width(gamesPanel, 8, 0);
        lv_obj_set_style_border_color(gamesPanel, lv_color_hex(kGreenEyePanelRing), 0);
        for (lv_obj_t* obj : {gamesRing, gamesEmblem, gamesAction, gamesStatus, gamesPrevBtn,
                              gamesNextBtn, checkerGrid, checkerBotIcon, checkerDotsRow,
                              checkerTitleScreen, checkerMatchupScreen,
                              checkerPlayAgainScreen, checkerFx}) {
            if (obj != nullptr) {
                lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
            }
        }
        HideQuickTapAll();
        HideSnakeAll();
        HideTiltMazeAll();
        HideTrafficRunnerAll();
    } else if (currentState == MENU_GAME_ACTIVE && activeGame == ACTIVE_GAME_QUICK_TAP) {
        // Quick Tap owns the whole panel: black ground, no menu chrome, and
        // its own screens are driven by ShowQuickTapScreen().
        lv_obj_set_style_bg_color(gamesPanel, lv_color_hex(kQuickTapPanelBg), 0);
        lv_obj_set_style_border_width(gamesPanel, 8, 0);
        lv_obj_set_style_border_color(gamesPanel, lv_color_hex(kQuickTapPanelRing), 0);
        for (lv_obj_t* obj : {gamesRing, gamesEmblem, gamesAction, gamesStatus, gamesPrevBtn,
                              gamesNextBtn, checkerGrid, checkerBotIcon, checkerDotsRow,
                              checkerTitleScreen, checkerMatchupScreen,
                              checkerPlayAgainScreen, checkerFx}) {
            if (obj != nullptr) {
                lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
            }
        }
        HideSnakeAll();
        HideTiltMazeAll();
        HideTrafficRunnerAll();
    } else if (currentState == MENU_GAME_ACTIVE && activeGame == ACTIVE_GAME_CHECKER) {
        // Neon on black: the board owns the screen and the turn indicator moves
        // to a header row at the top, beside the Bubu glyph.
        lv_obj_set_style_bg_color(gamesPanel, lv_color_hex(kCheckerPanelBg), 0);
        lv_obj_set_style_border_width(gamesPanel, 8, 0);
        lv_obj_set_style_border_color(gamesPanel, lv_color_hex(kCheckerPanelRing), 0);
        lv_obj_set_style_bg_opa(gamesStatus, LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_top(gamesStatus, 0, 0);
        lv_obj_set_style_pad_bottom(gamesStatus, 0, 0);
        lv_obj_set_style_text_font(gamesStatus, &lv_font_montserrat_vn_20, 0);

        if (gamesRing != nullptr) {
            lv_obj_add_flag(gamesRing, LV_OBJ_FLAG_HIDDEN);
        }
        if (gamesEmblem != nullptr) {
            lv_obj_add_flag(gamesEmblem, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_add_flag(gamesAction, LV_OBJ_FLAG_HIDDEN);
        // Content-sized, nudged right by half the glyph+gap so the icon and the
        // text read as one centred group.
        lv_obj_set_width(gamesStatus, LV_SIZE_CONTENT);
        // The result banner is wider than any turn text, so it drops the glyph
        // and takes the full centred width instead.
        const bool banner = (checkerScreen == CheckerScreen::kResult);
        lv_obj_align(gamesStatus, LV_ALIGN_TOP_MID, banner ? 0 : 12, 20);
        if (gamesPrevBtn != nullptr) {
            lv_obj_add_flag(gamesPrevBtn, LV_OBJ_FLAG_HIDDEN);
        }
        if (gamesNextBtn != nullptr) {
            lv_obj_add_flag(gamesNextBtn, LV_OBJ_FLAG_HIDDEN);
        }
        // Only the board screens own the grid and header; the title, matchup
        // and play-again screens must not have it forced back on underneath.
        const bool board_visible = (checkerScreen == CheckerScreen::kBoard ||
                                    checkerScreen == CheckerScreen::kResult);
        if (checkerGrid != nullptr) {
            if (board_visible) {
                lv_obj_remove_flag(checkerGrid, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(checkerGrid, LV_OBJ_FLAG_HIDDEN);
            }
        }
        if (checkerBotIcon != nullptr) {
            if (board_visible && !banner) {
                lv_obj_remove_flag(checkerBotIcon, LV_OBJ_FLAG_HIDDEN);
                lv_obj_move_foreground(checkerBotIcon);
            } else {
                lv_obj_add_flag(checkerBotIcon, LV_OBJ_FLAG_HIDDEN);
            }
        }
        if (gamesStatus != nullptr) {
            if (board_visible) {
                lv_obj_remove_flag(gamesStatus, LV_OBJ_FLAG_HIDDEN);
                // The grid is created after this label, so without this the
                // board and its glow paint straight over the header.
                lv_obj_move_foreground(gamesStatus);
            } else {
                lv_obj_add_flag(gamesStatus, LV_OBJ_FLAG_HIDDEN);
            }
        }
        HideSnakeAll();
        HideTiltMazeAll();
        HideTrafficRunnerAll();
    }

    if (gamesStatus != nullptr) {
        lv_label_set_text(gamesStatus, gameStatusMsg);
    }

    // The header label is content-sized, so the glyph has to be re-pinned to its
    // left edge whenever the text changes width.
    if (checkerBotIcon != nullptr && gamesStatus != nullptr &&
        currentState == MENU_GAME_ACTIVE && activeGame == ACTIVE_GAME_CHECKER) {
        lv_obj_update_layout(gamesPanel);
        lv_obj_align_to(checkerBotIcon, gamesStatus, LV_ALIGN_OUT_LEFT_MID, -6, 0);
    }
}

void AnimCellScaleCb(void* obj, int32_t value) {
    lv_obj_t* target = static_cast<lv_obj_t*>(obj);
    lv_obj_set_style_transform_scale_x(target, value, 0);
    lv_obj_set_style_transform_scale_y(target, value, 0);
}

void PlayCellPlaceAnimation(lv_obj_t* label) {
    lv_obj_set_style_transform_pivot_x(label, lv_obj_get_width(label) / 2, 0);
    lv_obj_set_style_transform_pivot_y(label, lv_obj_get_height(label) / 2, 0);
    lv_obj_set_style_opa(label, LV_OPA_TRANSP, 0);
    lv_obj_fade_in(label, 160, 0);

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, label);
    lv_anim_set_exec_cb(&anim, AnimCellScaleCb);
    lv_anim_set_values(&anim, 110, LV_SCALE_NONE);
    lv_anim_set_time(&anim, 220);
    lv_anim_set_path_cb(&anim, lv_anim_path_overshoot);
    lv_anim_start(&anim);
}

void PlayCellWinPulse(lv_obj_t* button) {
    lv_obj_set_style_transform_pivot_x(button, lv_obj_get_width(button) / 2, 0);
    lv_obj_set_style_transform_pivot_y(button, lv_obj_get_height(button) / 2, 0);

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, button);
    lv_anim_set_exec_cb(&anim, AnimCellScaleCb);
    // 285/256 ≈ 1.11x — stays inside the 6px gap between cells.
    lv_anim_set_values(&anim, LV_SCALE_NONE, 285);
    lv_anim_set_time(&anim, 260);
    lv_anim_set_playback_time(&anim, 260);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_start(&anim);
}

// ---------------------------------------------------------------------------
// Checker makeover: shared shape builders and the rolling win record.
// ---------------------------------------------------------------------------
lv_obj_t* CreateCheckerXMark(lv_obj_t* parent, int box,
                             const lv_point_precise_t* stroke_a,
                             const lv_point_precise_t* stroke_b,
                             int line_width, uint32_t color) {
    lv_obj_t* mark = lv_obj_create(parent);
    lv_obj_remove_style_all(mark);
    lv_obj_set_size(mark, box, box);
    lv_obj_remove_flag(mark, LV_OBJ_FLAG_SCROLLABLE);
    for (int stroke = 0; stroke < 2; ++stroke) {
        lv_obj_t* line = lv_line_create(mark);
        lv_line_set_points(line, stroke == 0 ? stroke_a : stroke_b, 2);
        lv_obj_set_style_line_width(line, line_width, 0);
        lv_obj_set_style_line_rounded(line, true, 0);
        lv_obj_set_style_line_color(line, lv_color_hex(color), 0);
        lv_obj_set_pos(line, 0, 0);
    }
    return mark;
}

lv_obj_t* CreateCheckerORing(lv_obj_t* parent, int size, int border_w, uint32_t color) {
    lv_obj_t* ring = lv_obj_create(parent);
    lv_obj_remove_style_all(ring);
    lv_obj_set_size(ring, size, size);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ring, border_w, 0);
    lv_obj_set_style_border_color(ring, lv_color_hex(color), 0);
    lv_obj_set_style_border_opa(ring, LV_OPA_COVER, 0);
    lv_obj_remove_flag(ring, LV_OBJ_FLAG_SCROLLABLE);
    return ring;
}

void ApplyCheckerGlow(lv_obj_t* obj, uint32_t color, int width, lv_opa_t opa) {
    lv_obj_set_style_shadow_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_shadow_width(obj, width, 0);
    lv_obj_set_style_shadow_spread(obj, 1, 0);
    lv_obj_set_style_shadow_offset_x(obj, 0, 0);
    lv_obj_set_style_shadow_offset_y(obj, 0, 0);
    lv_obj_set_style_shadow_opa(obj, opa, 0);
}

void LoadCheckerRecord() {
    Settings settings("checker", false);
    for (int i = 0; i < kCheckerRecordSlots; ++i) {
        char key[4] = {'r', static_cast<char>('0' + i), 0, 0};
        int value = settings.GetInt(key, 0);
        checkerRecord[i] = static_cast<uint8_t>((value < 0 || value > 3) ? 0 : value);
    }
}

void SaveCheckerRecord() {
    Settings settings("checker", true);
    for (int i = 0; i < kCheckerRecordSlots; ++i) {
        char key[4] = {'r', static_cast<char>('0' + i), 0, 0};
        settings.SetInt(key, checkerRecord[i]);
    }
}

void UpdateCheckerDots() {
    for (int i = 0; i < kCheckerRecordSlots; ++i) {
        if (checkerDots[i] == nullptr) {
            continue;
        }
        uint32_t color = kCheckerDotIdle;
        if (checkerRecord[i] == 1) {
            color = kCheckerDotWin;
        } else if (checkerRecord[i] == 2) {
            color = kCheckerDotLoss;
        }
        lv_obj_set_style_bg_color(checkerDots[i], lv_color_hex(color), 0);
    }
}

// Newest result lands in the last slot; the oldest falls off the front.
// Array + NVS only. The dots are LVGL objects, so repainting them is left to
// the caller, which must hold the display lock.
void PushCheckerResult(uint8_t code) {
    for (int i = 0; i + 1 < kCheckerRecordSlots; ++i) {
        checkerRecord[i] = checkerRecord[i + 1];
    }
    checkerRecord[kCheckerRecordSlots - 1] = code;
    SaveCheckerRecord();
}

void PlayCheckerConfetti() {
    if (checkerFx == nullptr) {
        return;
    }
    lv_obj_remove_flag(checkerFx, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(checkerFx);
    for (int i = 0; i < kCheckerParticleCount; ++i) {
        lv_obj_t* particle = checkerParticles[i];
        if (particle == nullptr) {
            continue;
        }
        const int start_x = 26 + static_cast<int>(esp_random() % 188);
        const int start_y = 60 + static_cast<int>(esp_random() % 40);
        lv_obj_set_pos(particle, start_x, start_y);
        lv_obj_remove_flag(particle, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_opa(particle, LV_OPA_COVER, 0);

        lv_anim_t fall;
        lv_anim_init(&fall);
        lv_anim_set_var(&fall, particle);
        lv_anim_set_exec_cb(&fall, [](void* obj, int32_t value) {
            lv_obj_set_y(static_cast<lv_obj_t*>(obj), value);
        });
        lv_anim_set_values(&fall, start_y, start_y + 90 + static_cast<int>(esp_random() % 40));
        lv_anim_set_time(&fall, 900 + static_cast<int>(esp_random() % 500));
        lv_anim_set_path_cb(&fall, lv_anim_path_ease_in);
        lv_anim_start(&fall);

        lv_anim_t fade;
        lv_anim_init(&fade);
        lv_anim_set_var(&fade, particle);
        lv_anim_set_exec_cb(&fade, [](void* obj, int32_t value) {
            lv_obj_set_style_opa(static_cast<lv_obj_t*>(obj),
                                 static_cast<lv_opa_t>(value), 0);
        });
        lv_anim_set_values(&fade, LV_OPA_COVER, LV_OPA_TRANSP);
        lv_anim_set_time(&fade, 1100 + static_cast<int>(esp_random() % 400));
        lv_anim_start(&fade);
    }
}

void HideCheckerConfetti() {
    if (checkerFx == nullptr) {
        return;
    }
    for (auto* particle : checkerParticles) {
        if (particle != nullptr) {
            lv_anim_delete(particle, nullptr);
        }
    }
    lv_obj_add_flag(checkerFx, LV_OBJ_FLAG_HIDDEN);
}

void UpdateCheckerBoardUI();   // defined below

void ShowCheckerScreen(CheckerScreen screen) {
    checkerScreen = screen;
    checkerScreenStartMs = lv_tick_get();

    const bool title = (screen == CheckerScreen::kTitle);
    const bool matchup = (screen == CheckerScreen::kMatchup);
    const bool play_again = (screen == CheckerScreen::kPlayAgain);
    const bool board = (screen == CheckerScreen::kBoard || screen == CheckerScreen::kResult);

    auto set_hidden = [](lv_obj_t* obj, bool hidden) {
        if (obj == nullptr) return;
        if (hidden) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
        else        lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    };

    set_hidden(checkerTitleScreen, !title);
    set_hidden(checkerMatchupScreen, !matchup);
    set_hidden(checkerPlayAgainScreen, !play_again);
    set_hidden(checkerGrid, !board);
    set_hidden(checkerBotIcon, !board);
    set_hidden(gamesStatus, !board);
    // The record is shown where it reads as context, not during play-again.
    set_hidden(checkerDotsRow, !(title || board));

    if (title || board) {
        lv_obj_align(checkerDotsRow, LV_ALIGN_BOTTOM_MID, 0, title ? -70 : -14);
        lv_obj_move_foreground(checkerDotsRow);
    }
    if (!(screen == CheckerScreen::kResult)) {
        HideCheckerConfetti();
    }
}

// Starts an actual match and drops onto the board.
void BeginCheckerMatch() {
    CheckerGame::Start();
    checkerRenderedCells.fill(CheckerGame::Cell::kEmpty);
    checkerWinPulsePlayed = false;
    checkerBubuThinking = false;
    checkerFinishHolding = false;
    checkerLastPlaced = -1;
    gamesStatusColor = kCheckerPlayerMark;
    std::snprintf(gameStatusMsg, sizeof(gameStatusMsg), "%s", "LƯỢT BẠN");
    ShowCheckerScreen(CheckerScreen::kBoard);
    UpdateCheckerBoardUI();
}

// Shows a short celebration on the board itself before the result screen.
void BeginCheckerFinishHold() {
    checkerBubuThinking = false;
    checkerFinishHolding = true;
    checkerFinishHoldStartMs = lv_tick_get();
    checkerScreen = CheckerScreen::kResult;

    uint8_t record_code = 0;
    switch (CheckerGame::GetResult()) {
        case CheckerGame::Result::kPlayerWin: record_code = 1; break;
        case CheckerGame::Result::kBubuWin:   record_code = 2; break;
        case CheckerGame::Result::kDraw:      record_code = 3; break;
        default: break;
    }
    if (record_code != 0) {
        PushCheckerResult(record_code);   // NVS write, deliberately outside the lock
    }

    switch (CheckerGame::GetResult()) {
        case CheckerGame::Result::kPlayerWin:
            gamesStatusColor = kCheckerPlayerMark;
            std::snprintf(gameStatusMsg, sizeof(gameStatusMsg), "%s", "BẠN THẮNG!");
            break;
        case CheckerGame::Result::kBubuWin:
            gamesStatusColor = kCheckerBubuMark;
            std::snprintf(gameStatusMsg, sizeof(gameStatusMsg), "%s", "BUBU THẮNG!");
            break;
        case CheckerGame::Result::kDraw:
        default:
            gamesStatusColor = kCheckerDrawText;
            std::snprintf(gameStatusMsg, sizeof(gameStatusMsg), "%s", "HÒA RỒI!");
            break;
    }

    // Everything below touches LVGL. Both callers reach here unlocked, so the
    // guard belongs here -- doing this unlocked hung the UI on a draw.
    if (displayHandle == nullptr) {
        return;
    }
    DisplayLockGuard lock(displayHandle);
    UpdateCheckerDots();
    if (record_code == 1 || record_code == 3) {
        PlayCheckerConfetti();
    }
}

void UpdateCheckerBoardUI() {
    if (checkerGrid == nullptr) {
        return;
    }

    for (uint8_t index = 0; index < checkerCellButtons.size(); ++index) {
        lv_obj_t* button = checkerCellButtons[index];
        lv_obj_t* x_mark = checkerXMarks[index];
        lv_obj_t* o_mark = checkerOMarks[index];
        if (button == nullptr || x_mark == nullptr || o_mark == nullptr) {
            continue;
        }

        const CheckerGame::Cell cell = CheckerGame::GetCell(index);
        const bool winning_cell = CheckerGame::IsWinningCell(index);

        uint32_t mark_color = 0;
        switch (cell) {
            case CheckerGame::Cell::kPlayer:
                lv_obj_remove_flag(x_mark, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(o_mark, LV_OBJ_FLAG_HIDDEN);
                mark_color = kCheckerPlayerMark;
                break;
            case CheckerGame::Cell::kBubu:
                lv_obj_add_flag(x_mark, LV_OBJ_FLAG_HIDDEN);
                lv_obj_remove_flag(o_mark, LV_OBJ_FLAG_HIDDEN);
                mark_color = kCheckerBubuMark;
                break;
            case CheckerGame::Cell::kEmpty:
            default:
                lv_obj_add_flag(x_mark, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(o_mark, LV_OBJ_FLAG_HIDDEN);
                break;
        }

        // Glow follows the mark's own colour: brightest on the winning line,
        // softer on the move that just landed, off everywhere else.
        const bool highlight = winning_cell || (checkerLastPlaced == static_cast<int8_t>(index));
        if (highlight && mark_color != 0) {
            lv_obj_set_style_border_width(button, winning_cell ? 3 : 2, 0);
            lv_obj_set_style_border_color(button, lv_color_hex(mark_color), 0);
            lv_obj_set_style_shadow_color(button, lv_color_hex(mark_color), 0);
            lv_obj_set_style_shadow_width(button, winning_cell ? 16 : 10, 0);
            lv_obj_set_style_shadow_spread(button, winning_cell ? 2 : 1, 0);
            lv_obj_set_style_shadow_offset_x(button, 0, 0);
            lv_obj_set_style_shadow_offset_y(button, 0, 0);
            lv_obj_set_style_shadow_opa(button, winning_cell ? LV_OPA_80 : LV_OPA_50, 0);
        } else {
            lv_obj_set_style_border_width(button, 2, 0);
            lv_obj_set_style_border_color(button, lv_color_hex(kCheckerCellBorder), 0);
            lv_obj_set_style_shadow_opa(button, LV_OPA_TRANSP, 0);
        }
        lv_obj_set_style_bg_color(button, lv_color_hex(kCheckerCellBg), 0);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
        lv_obj_set_style_outline_opa(button, LV_OPA_TRANSP, 0);

        if (cell != checkerRenderedCells[index] && cell != CheckerGame::Cell::kEmpty) {
            checkerLastPlaced = static_cast<int8_t>(index);
            PlayCellPlaceAnimation(cell == CheckerGame::Cell::kPlayer ? x_mark : o_mark);
        }
        checkerRenderedCells[index] = cell;
    }

    if (!checkerWinPulsePlayed &&
        (CheckerGame::GetResult() == CheckerGame::Result::kPlayerWin ||
         CheckerGame::GetResult() == CheckerGame::Result::kBubuWin)) {
        checkerWinPulsePlayed = true;
        for (uint8_t index = 0; index < checkerCellButtons.size(); ++index) {
            if (CheckerGame::IsWinningCell(index) && checkerCellButtons[index] != nullptr) {
                PlayCellWinPulse(checkerCellButtons[index]);
            }
        }
    }

    if (CheckerGame::IsRunning()) {
        const bool player_turn = CheckerGame::IsPlayerTurn();
        gamesActionColor = 0xA7D8FF;
        gamesStatusColor = player_turn ? kCheckerPlayerMark : kCheckerBubuMark;
        // Keep these short: the status pill hugs its text and must stay on one
        // line to fit the round screen below the board.
        std::snprintf(gameStatusMsg, sizeof(gameStatusMsg), "%s",
                      player_turn ? "LƯỢT BẠN" : "BUBU NGHĨ");
    }

    UpdateGamesUI();
}

// ---------------------------------------------------------------------------
// CHẠM NHANH
// ---------------------------------------------------------------------------

int QuickTapRecordIndex(QuickTapGame::Mode mode, QuickTapGame::Difficulty difficulty) {
    return static_cast<int>(mode) * kQuickTapDiffCount + static_cast<int>(difficulty);
}

void LoadQuickTapRecords() {
    Settings settings("quicktap", false);
    for (size_t i = 0; i < quickTapRecords.size(); ++i) {
        char key[4] = {'b', static_cast<char>('0' + i), 0, 0};
        const int value = settings.GetInt(key, 0);
        quickTapRecords[i] = static_cast<uint16_t>(value < 0 ? 0 : value);
    }
}

void SaveQuickTapRecord(int index) {
    if (index < 0 || index >= static_cast<int>(quickTapRecords.size())) {
        return;
    }
    Settings settings("quicktap", true);
    char key[4] = {'b', static_cast<char>('0' + index), 0, 0};
    settings.SetInt(key, quickTapRecords[index]);
}

const char* QuickTapDifficultyName(QuickTapGame::Difficulty difficulty) {
    switch (difficulty) {
        case QuickTapGame::Difficulty::kEasy:   return "DỄ";
        case QuickTapGame::Difficulty::kMedium: return "VỪA";
        case QuickTapGame::Difficulty::kHard:   return "KHÓ";
        default: break;
    }
    return "VỪA";
}

void PlayQuickTapSound(const std::string_view& sound) {
    Application::GetInstance().Schedule([sound]() {
        Application::GetInstance().PlayOverlaySound(sound);
    });
}

void StyleQuickTapPill(lv_obj_t* pill, uint32_t bg, uint32_t border, uint32_t text) {
    if (pill == nullptr) {
        return;
    }
    lv_obj_set_style_bg_color(pill, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(pill, lv_color_hex(border), 0);
    lv_obj_set_style_border_width(pill, 2, 0);
    lv_obj_t* label = lv_obj_get_child(pill, 0);
    if (label != nullptr) {
        lv_obj_set_style_text_color(label, lv_color_hex(text), 0);
    }
}

void UpdateQuickTapSetupUI() {
    // Difficulty is a selection; mode is not -- tapping a mode pill is what
    // starts the round, exactly like the reference watch face.
    for (int i = 0; i < kQuickTapDiffCount; ++i) {
        const bool selected = (i == static_cast<int>(quickTapDifficulty));
        StyleQuickTapPill(quickTapDiffBtns[i],
                          selected ? kQuickTapRing : kQuickTapPillDim,
                          selected ? kQuickTapRing : 0x33333D,
                          selected ? 0x001A16 : COLOR_TEXT);
    }

    // The record is per mode AND difficulty, so it belongs on the pills rather
    // than in one line that could not say which mode it meant.
    if (quickTapRecordLabel != nullptr) {
        lv_label_set_text_fmt(quickTapRecordLabel, "KỶ LỤC %s",
                              QuickTapDifficultyName(quickTapDifficulty));
    }
    for (int i = 0; i < kQuickTapModeCount; ++i) {
        if (quickTapModeRecords[i] == nullptr) {
            continue;
        }
        const int index = QuickTapRecordIndex(static_cast<QuickTapGame::Mode>(i),
                                              quickTapDifficulty);
        lv_label_set_text_fmt(quickTapModeRecords[i], "%u",
                              static_cast<unsigned>(quickTapRecords[index]));
    }
}

void UpdateQuickTapScoreUI() {
    const bool no_go = QuickTapGame::GetMode() == QuickTapGame::Mode::kNoGo;

    if (quickTapScoreValue != nullptr) {
        lv_label_set_text_fmt(quickTapScoreValue, "%d", QuickTapGame::GetScore());
    }
    if (quickTapHitsLabel != nullptr) {
        lv_label_set_text_fmt(quickTapHitsLabel, "%u",
                              static_cast<unsigned>(QuickTapGame::GetHits()));
    }
    if (quickTapMissLabel != nullptr) {
        lv_label_set_text_fmt(quickTapMissLabel, "TRẬT %u",
                              static_cast<unsigned>(QuickTapGame::GetMisses()));
    }
    if (quickTapRedLabel != nullptr) {
        lv_label_set_text_fmt(quickTapRedLabel, "%u",
                              static_cast<unsigned>(QuickTapGame::GetRedAvoided()));
    }
    // The red tally only means something in the mode that has red dots.
    for (lv_obj_t* obj : {quickTapRedMark, quickTapRedLabel}) {
        if (obj == nullptr) continue;
        if (no_go) lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
        else       lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }

    if (quickTapStreakLabel != nullptr) {
        // Spelled out so the number above is never a mystery.
        const unsigned multiplier =
            QuickTapGame::GetProfile(QuickTapGame::GetDifficulty()).streak_multiplier;
        lv_label_set_text_fmt(quickTapStreakLabel, "CHUỖI %u x%u = %d",
                              static_cast<unsigned>(QuickTapGame::GetLongestStreak()),
                              multiplier, QuickTapGame::GetStreakPoints());
    }
    if (quickTapScoreCaption != nullptr) {
        lv_label_set_text(quickTapScoreCaption, quickTapNewRecord ? "KỶ LỤC MỚI!" : "điểm");
        lv_obj_set_style_text_color(quickTapScoreCaption,
                                    lv_color_hex(quickTapNewRecord ? kQuickTapRing : kQuickTapRedDot), 0);
    }
}

void UpdateQuickTapPlayfield() {
    if (quickTapArc != nullptr) {
        const uint32_t round_ms = QuickTapGame::RoundMs();
        const uint32_t remaining = QuickTapGame::RemainingMs();
        lv_arc_set_value(quickTapArc, round_ms == 0 ? 0 :
                         static_cast<int32_t>((remaining * 1000ULL) / round_ms));
    }
    if (quickTapDot == nullptr) {
        return;
    }
    const QuickTapGame::Dot dot = QuickTapGame::GetDot();
    if (!dot.visible) {
        lv_obj_add_flag(quickTapDot, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    const uint32_t color = dot.red ? kQuickTapRedDot : kQuickTapWhiteDot;
    lv_obj_set_style_bg_color(quickTapDot, lv_color_hex(color), 0);
    lv_obj_set_style_shadow_color(quickTapDot, lv_color_hex(color), 0);
    // The logic module works in screen coordinates on a 240x240 panel; aligning
    // from the centre keeps this independent of the panel's border width.
    lv_obj_align(quickTapDot, LV_ALIGN_CENTER, dot.x - 120, dot.y - 120);
    lv_obj_remove_flag(quickTapDot, LV_OBJ_FLAG_HIDDEN);
}

void ShowQuickTapScreen(QuickTapScreen screen) {
    quickTapScreen = screen;

    auto set_hidden = [](lv_obj_t* obj, bool hidden) {
        if (obj == nullptr) return;
        if (hidden) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
        else        lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    };

    const bool playing = (screen == QuickTapScreen::kPlaying);
    const bool banner = (screen == QuickTapScreen::kBanner);
    set_hidden(quickTapSetupScreen, screen != QuickTapScreen::kSetup);
    set_hidden(quickTapScoreScreen, screen != QuickTapScreen::kScore);
    // The ring stays up under the banner -- drained, as the round just ended.
    set_hidden(quickTapArc, !(playing || banner));
    set_hidden(quickTapDot, !playing);
    set_hidden(quickTapBanner, !banner);

    switch (screen) {
        case QuickTapScreen::kSetup:
            UpdateQuickTapSetupUI();
            break;
        case QuickTapScreen::kPlaying:
            UpdateQuickTapPlayfield();
            break;
        case QuickTapScreen::kScore:
            UpdateQuickTapScoreUI();
            break;
        case QuickTapScreen::kBanner:
            break;
    }
    // gamesPanel is opaque and covers the screen, so repainting it is enough to
    // clear anything the previous screen left behind. Screen changes happen a
    // handful of times per round, so a full repaint here is cheap -- and it is
    // the only thing that reliably kills stale bands, whatever caused them.
    if (gamesPanel != nullptr) {
        lv_obj_invalidate(gamesPanel);
    }
    UpdateGamesUI();
}

void HideQuickTapAll() {
    for (lv_obj_t* obj : {quickTapArc, quickTapDot, quickTapBanner,
                          quickTapSetupScreen, quickTapScoreScreen}) {
        if (obj != nullptr) {
            lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

// Round over: bank the record, pay the care reward, show the scoreboard.
void FinishQuickTapRound() {
    const int index = QuickTapRecordIndex(QuickTapGame::GetMode(),
                                          QuickTapGame::GetDifficulty());
    const int score = QuickTapGame::GetScore();
    quickTapNewRecord = score > 0 && score > static_cast<int>(quickTapRecords[index]);
    if (quickTapNewRecord) {
        quickTapRecords[index] = static_cast<uint16_t>(score);
    }

    PlayQuickTapSound(quickTapNewRecord ? Lang::Sounds::OGG_SUCCESS
                                        : Lang::Sounds::OGG_POPUP);
    ShowQuickTapScreen(QuickTapScreen::kScore);

    // Both of these commit to NVS synchronously (AddMood can level up, and a
    // level-up saves immediately). This function runs on the LVGL task with the
    // display lock held, so handing them to the main task keeps a flash write
    // out of the frame that is drawing the scoreboard.
    const bool save_record = quickTapNewRecord;
    Application::GetInstance().Schedule([index, score, save_record]() {
        if (save_record) {
            SaveQuickTapRecord(index);
        }
        // Scales with the real score but capped, so Quick Tap does not become
        // a mood farm next to the other games' flat kGamesBoost.
        const int mood = std::min(5 + score / 8, 25);
        ESP_LOGI(TAG, "Game reward: quick_tap +%d mood", mood);
        CareSystem::AddMood(mood);
    });
}

void BeginQuickTapRound(QuickTapGame::Mode mode) {
    quickTapMode = mode;
    quickTapNewRecord = false;
    QuickTapGame::Start(mode, quickTapDifficulty);
    ShowQuickTapScreen(QuickTapScreen::kPlaying);
}

void EnterQuickTapBanner(const char* text, uint32_t hold_ms) {
    if (quickTapBanner != nullptr) {
        lv_label_set_text(quickTapBanner, text);
    }
    quickTapBannerStartMs = lv_tick_get();
    quickTapBannerHoldMs = hold_ms;
    ShowQuickTapScreen(QuickTapScreen::kBanner);
}

// Runs on the LVGL task inside lv_timer_handler, which already holds the
// display lock -- taking DisplayLockGuard here would deadlock on it.
void QuickTapTimerCb(lv_timer_t* timer) {
    (void)timer;
    if (activeGame != ACTIVE_GAME_QUICK_TAP) {
        return;
    }

    // The menu's 30s inactivity close-out is exactly one round long: without
    // this a slow player gets dropped back to the eyes mid-game.
    if (quickTapScreen == QuickTapScreen::kPlaying ||
        quickTapScreen == QuickTapScreen::kBanner) {
        MarkMenuActivity();
    }

    if (quickTapScreen == QuickTapScreen::kPlaying) {
        QuickTapGame::Update();
        UpdateQuickTapPlayfield();
        if (!QuickTapGame::IsRunning()) {
            if (QuickTapGame::GetResult() == QuickTapGame::Result::kTappedRed) {
                PlayQuickTapSound(Vox::Pick(Lang::Sounds::OGG_VOX_SAD_1_A, Lang::Sounds::OGG_VOX_SAD_1_B));
                EnterQuickTapBanner("CHẠM NHẦM!", kQuickTapRedHoldMs);
            } else {
                EnterQuickTapBanner("HẾT GIỜ!", kQuickTapTimeUpHoldMs);
            }
        }
        return;
    }

    if (quickTapScreen == QuickTapScreen::kBanner &&
        lv_tick_elaps(quickTapBannerStartMs) >= quickTapBannerHoldMs) {
        FinishQuickTapRound();
    }
}

void StartQuickTapTimer() {
    if (quickTapTimer == nullptr) {
        quickTapTimer = lv_timer_create(QuickTapTimerCb, kQuickTapTickMs, nullptr);
    }
}

void StopQuickTapTimer() {
    if (quickTapTimer != nullptr) {
        lv_timer_delete(quickTapTimer);
        quickTapTimer = nullptr;
    }
}

lv_obj_t* CreateQuickTapPill(lv_obj_t* parent, int width, int height,
                             int offset_x, int offset_y, const char* text,
                             const lv_font_t* font) {
    lv_obj_t* pill = lv_obj_create(parent);
    lv_obj_remove_style_all(pill);
    lv_obj_set_size(pill, width, height);
    lv_obj_align(pill, LV_ALIGN_CENTER, offset_x, offset_y);
    lv_obj_set_style_radius(pill, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
    lv_obj_set_style_border_opa(pill, LV_OPA_COVER, 0);
    lv_obj_remove_flag(pill, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* label = lv_label_create(pill);
    lv_obj_set_style_text_font(label, font, 0);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return pill;
}

// A muted number pinned to the right edge of a mode pill.
lv_obj_t* AddQuickTapPillRecord(lv_obj_t* pill, uint32_t color) {
    lv_obj_t* label = lv_label_create(pill);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_vn_20, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_label_set_text(label, "0");
    lv_obj_align(label, LV_ALIGN_RIGHT_MID, -14, 0);
    return label;
}

void CreateQuickTapUI() {
    if (gamesPanel == nullptr || quickTapArc != nullptr) {
        return;
    }

    // ---- Countdown ring ---------------------------------------------------
    quickTapArc = lv_arc_create(gamesPanel);
    lv_obj_set_size(quickTapArc, 216, 216);
    lv_obj_center(quickTapArc);
    lv_arc_set_rotation(quickTapArc, 270);        // drains from 12 o'clock
    lv_arc_set_bg_angles(quickTapArc, 0, 360);
    lv_arc_set_mode(quickTapArc, LV_ARC_MODE_NORMAL);
    lv_arc_set_range(quickTapArc, 0, 1000);
    lv_arc_set_value(quickTapArc, 1000);
    lv_obj_clear_flag(quickTapArc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_style(quickTapArc, nullptr, LV_PART_KNOB);
    lv_obj_set_style_arc_width(quickTapArc, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_width(quickTapArc, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(quickTapArc, lv_color_hex(kQuickTapRingTrack), LV_PART_MAIN);
    lv_obj_set_style_arc_color(quickTapArc, lv_color_hex(kQuickTapRing), LV_PART_INDICATOR);
    lv_obj_add_flag(quickTapArc, LV_OBJ_FLAG_HIDDEN);

    // ---- The dot ----------------------------------------------------------
    const int dot_size = QuickTapGame::GetDotRadius() * 2;
    quickTapDot = lv_obj_create(gamesPanel);
    lv_obj_remove_style_all(quickTapDot);
    lv_obj_set_size(quickTapDot, dot_size, dot_size);
    lv_obj_set_style_radius(quickTapDot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(quickTapDot, lv_color_hex(kQuickTapWhiteDot), 0);
    lv_obj_set_style_bg_opa(quickTapDot, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(quickTapDot, 12, 0);
    lv_obj_set_style_shadow_opa(quickTapDot, LV_OPA_40, 0);
    lv_obj_set_style_shadow_color(quickTapDot, lv_color_hex(kQuickTapWhiteDot), 0);
    lv_obj_remove_flag(quickTapDot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(quickTapDot, LV_OBJ_FLAG_HIDDEN);

    // ---- HẾT GIỜ! / CHẠM NHẦM! -------------------------------------------
    quickTapBanner = lv_label_create(gamesPanel);
    lv_obj_set_style_text_font(quickTapBanner, &lv_font_montserrat_vn_28, 0);
    lv_obj_set_style_text_color(quickTapBanner, lv_color_hex(COLOR_TEXT), 0);
    lv_label_set_text(quickTapBanner, "HẾT GIỜ!");
    lv_obj_align(quickTapBanner, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(quickTapBanner, LV_OBJ_FLAG_HIDDEN);

    // ---- Setup screen: mode pills + difficulty row ------------------------
    quickTapSetupScreen = lv_obj_create(gamesPanel);
    lv_obj_remove_style_all(quickTapSetupScreen);
    lv_obj_set_size(quickTapSetupScreen, 224, 224);
    lv_obj_center(quickTapSetupScreen);
    lv_obj_remove_flag(quickTapSetupScreen, LV_OBJ_FLAG_SCROLLABLE);

    quickTapRecordLabel = lv_label_create(quickTapSetupScreen);
    lv_obj_set_style_text_font(quickTapRecordLabel, &lv_font_montserrat_vn_20, 0);
    lv_obj_set_style_text_color(quickTapRecordLabel, lv_color_hex(kQuickTapMuted), 0);
    lv_label_set_text(quickTapRecordLabel, "KỶ LỤC DỄ");
    lv_obj_align(quickTapRecordLabel, LV_ALIGN_CENTER, 0, -62);

    quickTapModeBtns[0] = CreateQuickTapPill(quickTapSetupScreen, 158, 34, 0, -22,
                                             "CƠ BẢN", &lv_font_montserrat_vn_20);
    quickTapModeBtns[1] = CreateQuickTapPill(quickTapSetupScreen, 158, 34, 0, 18,
                                             "TRÁNH ĐỎ", &lv_font_montserrat_vn_20);
    StyleQuickTapPill(quickTapModeBtns[0], 0xFFFFFF, 0xFFFFFF, 0x101014);
    StyleQuickTapPill(quickTapModeBtns[1], kQuickTapPanelBg, kQuickTapRedDot, kQuickTapRedDot);
    for (int i = 0; i < kQuickTapModeCount; ++i) {
        lv_obj_t* label = lv_obj_get_child(quickTapModeBtns[i], 0);
        if (label != nullptr) {
            lv_obj_align(label, LV_ALIGN_LEFT_MID, 14, 0);
        }
    }
    quickTapModeRecords[0] = AddQuickTapPillRecord(quickTapModeBtns[0], 0x55555E);
    quickTapModeRecords[1] = AddQuickTapPillRecord(quickTapModeBtns[1], kQuickTapRedDot);

    static const char* const kDiffNames[kQuickTapDiffCount] = {"DỄ", "VỪA", "KHÓ"};
    for (int i = 0; i < kQuickTapDiffCount; ++i) {
        // 54/54 keeps the outer two pills' bottom corners ~105px from centre,
        // clear of the 112px clip circle on the round panel.
        quickTapDiffBtns[i] = CreateQuickTapPill(quickTapSetupScreen, 52, 28,
                                                 (i - 1) * 54, 54, kDiffNames[i],
                                                 &lv_font_montserrat_vn_20);
    }
    lv_obj_add_flag(quickTapSetupScreen, LV_OBJ_FLAG_HIDDEN);

    // ---- Scoreboard -------------------------------------------------------
    quickTapScoreScreen = lv_obj_create(gamesPanel);
    lv_obj_remove_style_all(quickTapScoreScreen);
    lv_obj_set_size(quickTapScoreScreen, 224, 224);
    lv_obj_center(quickTapScoreScreen);
    lv_obj_remove_flag(quickTapScoreScreen, LV_OBJ_FLAG_SCROLLABLE);

    quickTapScoreValue = lv_label_create(quickTapScoreScreen);
    lv_obj_set_style_text_font(quickTapScoreValue, &lv_font_montserrat_vn_28, 0);
    lv_obj_set_style_text_color(quickTapScoreValue, lv_color_hex(COLOR_TEXT), 0);
    lv_label_set_text(quickTapScoreValue, "0");
    lv_obj_align(quickTapScoreValue, LV_ALIGN_CENTER, 0, -70);

    quickTapScoreCaption = lv_label_create(quickTapScoreScreen);
    lv_obj_set_style_text_font(quickTapScoreCaption, &lv_font_montserrat_vn_20, 0);
    lv_obj_set_style_text_color(quickTapScoreCaption, lv_color_hex(kQuickTapRedDot), 0);
    lv_label_set_text(quickTapScoreCaption, "điểm");
    lv_obj_align(quickTapScoreCaption, LV_ALIGN_CENTER, 0, -44);

    // Colour-coded tallies: the dots are drawn objects because the VN font has
    // no icon set (the checker game's record row does the same).
    quickTapStatRow = lv_obj_create(quickTapScoreScreen);
    lv_obj_remove_style_all(quickTapStatRow);
    lv_obj_set_size(quickTapStatRow, LV_SIZE_CONTENT, 26);
    lv_obj_align(quickTapStatRow, LV_ALIGN_CENTER, 0, -16);
    lv_obj_set_flex_flow(quickTapStatRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(quickTapStatRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(quickTapStatRow, 8, 0);
    lv_obj_remove_flag(quickTapStatRow, LV_OBJ_FLAG_SCROLLABLE);

    auto make_mark = [](lv_obj_t* parent, uint32_t color) {
        lv_obj_t* mark = lv_obj_create(parent);
        lv_obj_remove_style_all(mark);
        lv_obj_set_size(mark, 12, 12);
        lv_obj_set_style_radius(mark, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(mark, lv_color_hex(color), 0);
        lv_obj_set_style_bg_opa(mark, LV_OPA_COVER, 0);
        return mark;
    };
    auto make_stat_label = [](lv_obj_t* parent, uint32_t color) {
        lv_obj_t* label = lv_label_create(parent);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_vn_20, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
        lv_label_set_text(label, "0");
        return label;
    };

    make_mark(quickTapStatRow, kQuickTapWhiteDot);
    quickTapHitsLabel = make_stat_label(quickTapStatRow, COLOR_TEXT);
    quickTapRedMark = make_mark(quickTapStatRow, kQuickTapRedDot);
    quickTapRedLabel = make_stat_label(quickTapStatRow, kQuickTapRedDot);
    quickTapMissLabel = make_stat_label(quickTapStatRow, kQuickTapMuted);
    lv_label_set_text(quickTapMissLabel, "TRẬT 0");

    quickTapStreakLabel = lv_label_create(quickTapScoreScreen);
    lv_obj_set_style_text_font(quickTapStreakLabel, &lv_font_montserrat_vn_20, 0);
    lv_obj_set_style_text_color(quickTapStreakLabel, lv_color_hex(kQuickTapRing), 0);
    lv_label_set_text(quickTapStreakLabel, "CHUỖI 0 x1 = 0");
    lv_obj_align(quickTapStreakLabel, LV_ALIGN_CENTER, 0, 12);

    quickTapReplayBtn = CreateQuickTapPill(quickTapScoreScreen, 130, 32, 0, 46,
                                           "CHƠI LẠI", &lv_font_montserrat_vn_20);
    StyleQuickTapPill(quickTapReplayBtn, 0xFFFFFF, 0xFFFFFF, 0x101014);
    quickTapMenuBtn = CreateQuickTapPill(quickTapScoreScreen, 88, 28, 0, 80,
                                         "MENU", &lv_font_montserrat_vn_20);
    StyleQuickTapPill(quickTapMenuBtn, kQuickTapPillDim, 0x33333D, COLOR_TEXT);
    lv_obj_add_flag(quickTapScoreScreen, LV_OBJ_FLAG_HIDDEN);

    LoadQuickTapRecords();
    UpdateQuickTapSetupUI();
}

// ---------------------------------------------------------------------------
// RẮN SĂN MỒI
// ---------------------------------------------------------------------------

void LoadSnakeRecords() {
    Settings settings("snake", false);
    for (size_t i = 0; i < snakeRecords.size(); ++i) {
        char key[4] = {'b', static_cast<char>('0' + i), 0, 0};
        const int value = settings.GetInt(key, 0);
        snakeRecords[i] = static_cast<uint16_t>(value < 0 ? 0 : value);
    }
}

void SaveSnakeRecord(int index) {
    if (index < 0 || index >= static_cast<int>(snakeRecords.size())) {
        return;
    }
    Settings settings("snake", true);
    char key[4] = {'b', static_cast<char>('0' + index), 0, 0};
    settings.SetInt(key, snakeRecords[index]);
}

const char* SnakeSpeedName(SnakeGame::Speed speed) {
    switch (speed) {
        case SnakeGame::Speed::kSlow:   return "CHẬM";
        case SnakeGame::Speed::kNormal: return "VỪA";
        case SnakeGame::Speed::kFast:   return "NHANH";
        default: break;
    }
    return "VỪA";
}

void PlaySnakeSound(const std::string_view& sound) {
    Application::GetInstance().Schedule([sound]() {
        Application::GetInstance().PlayOverlaySound(sound);
    });
}

// ---- Board painting -------------------------------------------------------
//
// Straight RGB565 writes into the canvas buffer, the same way the eye
// animation paints its fog (eye_animation.cc, CanvasBlendPixel565) and for the
// same reason: these are solid blocks, so going through an lv_draw layer would
// cost a layer set-up per frame to produce identical pixels.

uint16_t SnakeColor565(uint32_t rgb) {
    const uint8_t r = static_cast<uint8_t>((rgb >> 16) & 0xFF);
    const uint8_t g = static_cast<uint8_t>((rgb >> 8) & 0xFF);
    const uint8_t b = static_cast<uint8_t>(rgb & 0xFF);
    return static_cast<uint16_t>(((r & 0xF8U) << 8U) | ((g & 0xFCU) << 3U) | (b >> 3U));
}

void SnakeFillRect(uint16_t* buf, int x, int y, int w, int h, uint16_t color) {
    if (buf == nullptr) {
        return;
    }
    const int x0 = std::max(x, 0);
    const int y0 = std::max(y, 0);
    const int x1 = std::min(x + w, kSnakeBoardPx);
    const int y1 = std::min(y + h, kSnakeBoardPx);
    for (int row = y0; row < y1; ++row) {
        uint16_t* line = buf + row * kSnakeBoardPx;
        for (int col = x0; col < x1; ++col) {
            line[col] = color;
        }
    }
}

void SnakeFillCell(uint16_t* buf, const SnakeGame::Cell& cell, uint16_t color) {
    SnakeFillRect(buf, cell.x * kSnakeCell + kSnakeCellInset,
                  cell.y * kSnakeCell + kSnakeCellInset,
                  kSnakeCell - kSnakeCellInset, kSnakeCell - kSnakeCellInset, color);
}

void RedrawSnakeBoard() {
    if (snakeCanvasBuf == nullptr || snakeCanvas == nullptr) {
        return;
    }
    uint16_t* buf = reinterpret_cast<uint16_t*>(snakeCanvasBuf);

    SnakeFillRect(buf, 0, 0, kSnakeBoardPx, kSnakeBoardPx, SnakeColor565(kSnakeBoardBg));
    // A 1px seam on every cell boundary. Without it a 15x15 field of flat
    // colour gives the eye nothing to judge the snake's speed against.
    const uint16_t grid = SnakeColor565(kSnakeGridLine);
    for (int i = 1; i < SnakeGame::kBoardCells; ++i) {
        SnakeFillRect(buf, i * kSnakeCell, 0, 1, kSnakeBoardPx, grid);
        SnakeFillRect(buf, 0, i * kSnakeCell, kSnakeBoardPx, 1, grid);
    }

    SnakeFillCell(buf, SnakeGame::GetPellet(), SnakeColor565(kSnakePellet));

    const uint16_t body = SnakeColor565(kSnakeBody);
    const uint16_t head = SnakeColor565(kSnakeHead);
    const int length = static_cast<int>(SnakeGame::GetLength());
    // Tail first, so the head paints over the overlap on the one step where it
    // enters the cell the tail is leaving.
    for (int i = length - 1; i >= 0; --i) {
        SnakeFillCell(buf, SnakeGame::GetSegment(static_cast<uint16_t>(i)),
                      i == 0 ? head : body);
    }
    lv_obj_invalidate(snakeCanvas);
}

void UpdateSnakeChevrons() {
    const uint32_t now = lv_tick_get();
    if (snakeFlashChevron >= 0 && static_cast<int32_t>(now - snakeFlashUntilMs) >= 0) {
        snakeFlashChevron = -1;
    }
    if (snakeFlashChevron == snakeFlashApplied) {
        return;
    }
    snakeFlashApplied = snakeFlashChevron;
    for (size_t i = 0; i < snakeChevrons.size(); ++i) {
        if (snakeChevrons[i] == nullptr) {
            continue;
        }
        const bool lit = (static_cast<int>(i) == snakeFlashChevron);
        lv_obj_t* line = lv_obj_get_child(snakeChevrons[i], 0);
        if (line != nullptr) {
            lv_obj_set_style_line_color(line, lv_color_hex(lit ? kSnakeHead : kSnakeMuted), 0);
            lv_obj_set_style_line_opa(line, lit ? LV_OPA_COVER : LV_OPA_40, 0);
        }
    }
}

void FlashSnakeChevron(SnakeGame::Direction direction) {
    snakeFlashChevron = static_cast<int>(direction);
    snakeFlashUntilMs = lv_tick_get() + kSnakeChevronFlashMs;
    UpdateSnakeChevrons();
}

void UpdateSnakeScore() {
    if (snakeScoreLabel != nullptr) {
        lv_label_set_text_fmt(snakeScoreLabel, "%u",
                              static_cast<unsigned>(SnakeGame::GetScore()));
    }
}

void UpdateSnakeSetupUI() {
    if (snakeSpeedBtn != nullptr) {
        lv_obj_t* label = lv_obj_get_child(snakeSpeedBtn, 0);
        if (label != nullptr) {
            lv_label_set_text(label, SnakeSpeedName(snakeSpeed));
        }
        StyleQuickTapPill(snakeSpeedBtn, kSnakePillDim, kSnakeAccent, kSnakeAccent);
    }
    if (snakeSetupRecord != nullptr) {
        lv_label_set_text_fmt(
            snakeSetupRecord, "KỶ LỤC %u",
            static_cast<unsigned>(
                std::min<uint16_t>(snakeRecords[static_cast<int>(snakeSpeed)], 999)));
    }
}

const char* SnakeOutcomeHeadline(SnakeGame::Result result) {
    switch (result) {
        // "VÁCH" and not the more natural "TƯỜNG" purely on width: "ĐỤNG
        // TƯỜNG" is 161px at vn_22 and clears the ring by 1.33px, tighter than
        // anything else on the panel. "ĐỤNG VÁCH" is 141px and clears by 8.59.
        case SnakeGame::Result::kHitWall:     return "ĐỤNG VÁCH";
        case SnakeGame::Result::kHitSelf:     return "CẮN ĐUÔI";
        case SnakeGame::Result::kFilledBoard: return "ĂN HẾT BÀN!";
        default: break;
    }
    return "ĐÃ DỪNG";
}

void UpdateSnakeOverUI() {
    const int speed_index = static_cast<int>(SnakeGame::GetSpeed());
    const bool won = SnakeGame::GetResult() == SnakeGame::Result::kFilledBoard;
    if (snakeOverHead != nullptr) {
        lv_label_set_text(snakeOverHead, SnakeOutcomeHeadline(SnakeGame::GetResult()));
        lv_obj_set_style_text_color(snakeOverHead,
                                    lv_color_hex(won ? kSnakeAccent : kSnakePellet), 0);
    }
    if (snakeOverScore != nullptr) {
        if (snakeNewRecord) {
            lv_label_set_text(snakeOverScore, "KỶ LỤC MỚI!");
            lv_obj_set_style_text_color(snakeOverScore, lv_color_hex(kSnakeAccent), 0);
        } else {
            lv_label_set_text_fmt(snakeOverScore, "ĐIỂM %u",
                                  static_cast<unsigned>(SnakeGame::GetScore()));
            lv_obj_set_style_text_color(snakeOverScore, lv_color_hex(COLOR_TEXT), 0);
        }
    }
    if (snakeOverBest != nullptr) {
        lv_label_set_text_fmt(
            snakeOverBest, "KỶ LỤC %u",
            static_cast<unsigned>(std::min<uint16_t>(snakeRecords[speed_index], 999)));
    }
}

void HideSnakeAll() {
    for (lv_obj_t* obj : {snakeCanvas, snakeScoreLabel, snakeSetupScreen, snakeOverScreen}) {
        if (obj != nullptr) {
            lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
        }
    }
    for (lv_obj_t* obj : snakeChevrons) {
        if (obj != nullptr) {
            lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void ShowSnakeScreen(SnakeScreen screen) {
    snakeScreen = screen;

    auto set_hidden = [](lv_obj_t* obj, bool hidden) {
        if (obj == nullptr) return;
        if (hidden) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
        else        lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    };

    // The board stays up through kHold -- that is the whole point of kHold.
    const bool board_up = (screen == SnakeScreen::kPlaying || screen == SnakeScreen::kHold);
    set_hidden(snakeSetupScreen, screen != SnakeScreen::kSetup);
    set_hidden(snakeOverScreen, screen != SnakeScreen::kOver);
    set_hidden(snakeCanvas, !board_up);
    set_hidden(snakeScoreLabel, !board_up);
    for (lv_obj_t* obj : snakeChevrons) {
        set_hidden(obj, screen != SnakeScreen::kPlaying);
    }

    switch (screen) {
        case SnakeScreen::kSetup:
            UpdateSnakeSetupUI();
            break;
        case SnakeScreen::kPlaying:
            UpdateSnakeScore();
            UpdateSnakeChevrons();
            RedrawSnakeBoard();
            break;
        case SnakeScreen::kHold:
            RedrawSnakeBoard();
            break;
        case SnakeScreen::kOver:
            UpdateSnakeOverUI();
            break;
    }
    // Same reasoning as ShowQuickTapScreen: gamesPanel is opaque and covers the
    // screen, so one repaint is the cheap and reliable way to clear whatever the
    // previous screen left behind.
    if (gamesPanel != nullptr) {
        lv_obj_invalidate(gamesPanel);
    }
    UpdateGamesUI();
}

// Round over: bank the record, pay the care reward, show the scoreboard.
void FinishSnakeRound() {
    const int index = static_cast<int>(SnakeGame::GetSpeed());
    const int score = static_cast<int>(SnakeGame::GetScore());
    snakeNewRecord = score > 0 && score > static_cast<int>(snakeRecords[index]);
    if (snakeNewRecord) {
        snakeRecords[index] = static_cast<uint16_t>(score);
    }

    PlaySnakeSound(snakeNewRecord ? Lang::Sounds::OGG_SUCCESS : Lang::Sounds::OGG_POPUP);
    ShowSnakeScreen(SnakeScreen::kOver);

    // Both of these commit to NVS synchronously (AddMood can level up, and a
    // level-up saves immediately). This runs on the LVGL task with the display
    // lock held, so the writes go to the main task rather than stalling the
    // frame that is drawing the scoreboard -- same hand-off as Quick Tap.
    const bool save_record = snakeNewRecord;
    Application::GetInstance().Schedule([index, score, save_record]() {
        if (save_record) {
            SaveSnakeRecord(index);
        }
        // Scaled but capped, matching Quick Tap, so a long snake does not turn
        // into a mood farm next to the other games' flat kGamesBoost.
        const int mood = std::min(5 + score / 3, 25);
        ESP_LOGI(TAG, "Game reward: snake +%d mood", mood);
        CareSystem::AddMood(mood);
    });
}

void BeginSnakeRound() {
    snakeNewRecord = false;
    snakeFlashChevron = -1;
    snakeFlashApplied = -2;
    SnakeGame::Start(snakeSpeed);
    ShowSnakeScreen(SnakeScreen::kPlaying);
}

// Runs on the LVGL task inside lv_timer_handler, which already holds the
// display lock -- taking DisplayLockGuard here would deadlock on it.
void SnakeTimerCb(lv_timer_t* timer) {
    (void)timer;
    if (activeGame != ACTIVE_GAME_SNAKE) {
        return;
    }

    if (snakeScreen == SnakeScreen::kPlaying) {
        // The menu's 30s inactivity close-out would otherwise drop a player
        // mid-round, since steering by swipe never reaches MarkMenuActivity().
        // Deliberately NOT done on setup/scoreboard: an abandoned game should
        // close out like any other idle panel.
        MarkMenuActivity();

        const bool stepped = SnakeGame::Update();
        UpdateSnakeChevrons();
        if (stepped) {
            UpdateSnakeScore();
            RedrawSnakeBoard();
        }
        if (!SnakeGame::IsRunning()) {
            PlaySnakeSound(SnakeGame::GetResult() == SnakeGame::Result::kFilledBoard
                               ? Lang::Sounds::OGG_SUCCESS
                               : Vox::Pick(Lang::Sounds::OGG_VOX_SAD_1_A, Lang::Sounds::OGG_VOX_SAD_1_B));
            snakeHoldStartMs = lv_tick_get();
            ShowSnakeScreen(SnakeScreen::kHold);
        }
        return;
    }

    if (snakeScreen == SnakeScreen::kHold) {
        MarkMenuActivity();
        if (lv_tick_elaps(snakeHoldStartMs) >= kSnakeOverHoldMs) {
            FinishSnakeRound();
        }
    }
}

void StartSnakeTimer() {
    if (snakeTimer == nullptr) {
        snakeTimer = lv_timer_create(SnakeTimerCb, kSnakeTickMs, nullptr);
    }
}

void StopSnakeTimer() {
    if (snakeTimer != nullptr) {
        lv_timer_delete(snakeTimer);
        snakeTimer = nullptr;
    }
}

// Resolves a tap anywhere on the playfield into a direction: whichever axis the
// tap is further out on, then the sign. Deliberately not a hit-test on the
// drawn chevrons -- a child's fingertip is wider than a 23px arrow, and the
// only thing a tap can mean during play is "turn".
SnakeGame::Direction SnakeDirectionForTap(int x, int y) {
    const int dx = x - 120;
    const int dy = y - (120 + kSnakeBoardOffsetY);
    if (std::abs(dx) >= std::abs(dy)) {
        return dx < 0 ? SnakeGame::Direction::kLeft : SnakeGame::Direction::kRight;
    }
    return dy < 0 ? SnakeGame::Direction::kUp : SnakeGame::Direction::kDown;
}

// A chevron: one 3-point polyline in a transparent box. Drawn rather than set
// as text because the VN faces carry no arrow glyph -- the same reason the
// checker game builds its X out of two lv_line strokes. The point arrays are
// static because lv_line stores the pointer instead of copying.
lv_obj_t* CreateSnakeChevron(lv_obj_t* parent, SnakeGame::Direction direction,
                             int offset_x, int offset_y) {
    static const lv_point_precise_t kUp[]    = {{0, 9}, {11, 0}, {22, 9}};
    static const lv_point_precise_t kDown[]  = {{0, 0}, {11, 9}, {22, 0}};
    static const lv_point_precise_t kLeft[]  = {{9, 0}, {0, 11}, {9, 22}};
    static const lv_point_precise_t kRight[] = {{0, 0}, {9, 11}, {0, 22}};

    const lv_point_precise_t* points = kUp;
    int w = 23;
    int h = 10;
    switch (direction) {
        case SnakeGame::Direction::kUp:    points = kUp;    w = 23; h = 10; break;
        case SnakeGame::Direction::kDown:  points = kDown;  w = 23; h = 10; break;
        case SnakeGame::Direction::kLeft:  points = kLeft;  w = 10; h = 23; break;
        case SnakeGame::Direction::kRight: points = kRight; w = 10; h = 23; break;
    }

    lv_obj_t* box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, w, h);
    lv_obj_align(box, LV_ALIGN_CENTER, offset_x, offset_y);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* line = lv_line_create(box);
    lv_line_set_points(line, points, 3);
    lv_obj_set_style_line_width(line, 3, 0);
    lv_obj_set_style_line_rounded(line, true, 0);
    lv_obj_set_style_line_color(line, lv_color_hex(kSnakeMuted), 0);
    lv_obj_set_style_line_opa(line, LV_OPA_40, 0);
    lv_obj_set_pos(line, 0, 0);

    lv_obj_add_flag(box, LV_OBJ_FLAG_HIDDEN);
    return box;
}

void CreateSnakeUI() {
    if (gamesPanel == nullptr || snakeSetupScreen != nullptr) {
        return;
    }

    // ---- Playfield --------------------------------------------------------
    const size_t canvas_bytes =
        static_cast<size_t>(kSnakeBoardPx) * kSnakeBoardPx * sizeof(lv_color_t);
    snakeCanvasBuf = static_cast<lv_color_t*>(
        heap_caps_malloc(canvas_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (snakeCanvasBuf == nullptr) {
        // ~36 KB against megabytes of free PSRAM, so this is close to
        // impossible -- but the board simply does not appear rather than
        // dereferencing null. Every draw path null-checks the buffer.
        ESP_LOGE(TAG, "Snake: failed to allocate the %u-byte board canvas",
                 static_cast<unsigned>(canvas_bytes));
    } else {
        snakeCanvas = lv_canvas_create(gamesPanel);
        lv_obj_remove_style_all(snakeCanvas);
        lv_obj_set_size(snakeCanvas, kSnakeBoardPx, kSnakeBoardPx);
        lv_obj_align(snakeCanvas, LV_ALIGN_CENTER, 0, kSnakeBoardOffsetY);
        lv_canvas_set_buffer(snakeCanvas, snakeCanvasBuf, kSnakeBoardPx, kSnakeBoardPx,
                             LV_COLOR_FORMAT_RGB565);
        lv_obj_add_flag(snakeCanvas, LV_OBJ_FLAG_HIDDEN);
    }

    snakeScoreLabel = lv_label_create(gamesPanel);
    lv_obj_set_style_text_font(snakeScoreLabel, &lv_font_montserrat_vn_20, 0);
    lv_obj_set_style_text_color(snakeScoreLabel, lv_color_hex(COLOR_TEXT), 0);
    lv_label_set_text(snakeScoreLabel, "0");
    lv_obj_align(snakeScoreLabel, LV_ALIGN_CENTER, 0, kSnakeScoreDy);
    lv_obj_add_flag(snakeScoreLabel, LV_OBJ_FLAG_HIDDEN);

    // In the gaps the board leaves. The board spans +-67.5px about a centre 6px
    // low, so the side chevrons sit 90px out and the vertical pair clear both
    // the board edge and the score line above it.
    snakeChevrons[static_cast<int>(SnakeGame::Direction::kUp)] =
        CreateSnakeChevron(gamesPanel, SnakeGame::Direction::kUp, 0, kSnakeChevronUpDy);
    snakeChevrons[static_cast<int>(SnakeGame::Direction::kDown)] =
        CreateSnakeChevron(gamesPanel, SnakeGame::Direction::kDown, 0, kSnakeChevronDownDy);
    snakeChevrons[static_cast<int>(SnakeGame::Direction::kLeft)] =
        CreateSnakeChevron(gamesPanel, SnakeGame::Direction::kLeft,
                           -kSnakeChevronSideDx, kSnakeBoardOffsetY);
    snakeChevrons[static_cast<int>(SnakeGame::Direction::kRight)] =
        CreateSnakeChevron(gamesPanel, SnakeGame::Direction::kRight,
                           kSnakeChevronSideDx, kSnakeBoardOffsetY);

    // ---- Setup ------------------------------------------------------------
    snakeSetupScreen = lv_obj_create(gamesPanel);
    lv_obj_remove_style_all(snakeSetupScreen);
    lv_obj_set_size(snakeSetupScreen, 224, 224);
    lv_obj_center(snakeSetupScreen);
    lv_obj_remove_flag(snakeSetupScreen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(snakeSetupScreen);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_vn_28, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(kSnakeAccent), 0);
    // "RẮN" alone here: the carousel already said "RẮN SĂN MỒI", and the full
    // name at vn_28 does not clear the ring this far from centre.
    lv_label_set_text(title, "RẮN");
    lv_obj_align(title, LV_ALIGN_CENTER, 0, kSnakeTitleDy);

    snakeSetupRecord = lv_label_create(snakeSetupScreen);
    lv_obj_set_style_text_font(snakeSetupRecord, &lv_font_montserrat_vn_20, 0);
    lv_obj_set_style_text_color(snakeSetupRecord, lv_color_hex(kSnakeMuted), 0);
    lv_label_set_text(snakeSetupRecord, "KỶ LỤC 0");
    lv_obj_align(snakeSetupRecord, LV_ALIGN_CENTER, 0, kSnakeBestDy);

    lv_obj_t* speed_caption = lv_label_create(snakeSetupScreen);
    lv_obj_set_style_text_font(speed_caption, &lv_font_montserrat_vn_20, 0);
    lv_obj_set_style_text_color(speed_caption, lv_color_hex(kSnakeMuted), 0);
    lv_label_set_text(speed_caption, "TỐC ĐỘ");
    lv_obj_align(speed_caption, LV_ALIGN_CENTER, 0, kSnakeSpeedCapDy);

    snakeSpeedBtn = CreateQuickTapPill(snakeSetupScreen, kSnakeSpeedPillW, kSnakeSpeedPillH,
                                       0, kSnakeSpeedDy, "VỪA", &lv_font_montserrat_vn_20);

    snakePlayBtn = CreateQuickTapPill(snakeSetupScreen, kSnakePlayPillW, kSnakePlayPillH,
                                      0, kSnakePlayDy, "CHƠI", &lv_font_montserrat_vn_22);
    StyleQuickTapPill(snakePlayBtn, kSnakeAccent, kSnakeAccent, 0x04180B);

    lv_obj_t* hint = lv_label_create(snakeSetupScreen);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_vn_20, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(kSnakeMuted), 0);
    // 109px, clears the ring by 7.61. The fuller "Vuốt hoặc chạm mũi tên" is
    // 252px -- wider than the glass, never mind the chord at this height.
    lv_label_set_text(hint, "Vuốt để lái");
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, kSnakeHintDy);

    lv_obj_add_flag(snakeSetupScreen, LV_OBJ_FLAG_HIDDEN);

    // ---- Scoreboard -------------------------------------------------------
    snakeOverScreen = lv_obj_create(gamesPanel);
    lv_obj_remove_style_all(snakeOverScreen);
    lv_obj_set_size(snakeOverScreen, 224, 224);
    lv_obj_center(snakeOverScreen);
    lv_obj_remove_flag(snakeOverScreen, LV_OBJ_FLAG_SCROLLABLE);

    snakeOverHead = lv_label_create(snakeOverScreen);
    lv_obj_set_style_text_font(snakeOverHead, &lv_font_montserrat_vn_22, 0);
    lv_obj_set_style_text_color(snakeOverHead, lv_color_hex(kSnakePellet), 0);
    lv_label_set_text(snakeOverHead, "ĐỤNG VÁCH");
    lv_obj_align(snakeOverHead, LV_ALIGN_CENTER, 0, kSnakeOverHeadDy);

    snakeOverScore = lv_label_create(snakeOverScreen);
    lv_obj_set_style_text_font(snakeOverScore, &lv_font_montserrat_vn_20, 0);
    lv_obj_set_style_text_color(snakeOverScore, lv_color_hex(COLOR_TEXT), 0);
    lv_label_set_text(snakeOverScore, "ĐIỂM 0");
    lv_obj_align(snakeOverScore, LV_ALIGN_CENTER, 0, kSnakeOverScoreDy);

    snakeOverBest = lv_label_create(snakeOverScreen);
    lv_obj_set_style_text_font(snakeOverBest, &lv_font_montserrat_vn_20, 0);
    lv_obj_set_style_text_color(snakeOverBest, lv_color_hex(kSnakeMuted), 0);
    lv_label_set_text(snakeOverBest, "KỶ LỤC 0");
    lv_obj_align(snakeOverBest, LV_ALIGN_CENTER, 0, kSnakeOverBestDy);

    snakeAgainBtn = CreateQuickTapPill(snakeOverScreen, kSnakeAgainPillW, kSnakeAgainPillH,
                                       0, kSnakeAgainDy, "CHƠI LẠI", &lv_font_montserrat_vn_20);
    StyleQuickTapPill(snakeAgainBtn, kSnakeAccent, kSnakeAccent, 0x04180B);
    snakeMenuBtn = CreateQuickTapPill(snakeOverScreen, kSnakeMenuPillW, kSnakeMenuPillH,
                                      0, kSnakeMenuDy, "MENU", &lv_font_montserrat_vn_20);
    StyleQuickTapPill(snakeMenuBtn, kSnakePillDim, 0x27362C, COLOR_TEXT);
    lv_obj_add_flag(snakeOverScreen, LV_OBJ_FLAG_HIDDEN);

    LoadSnakeRecords();
    UpdateSnakeSetupUI();
}

// ---------------------------------------------------------------------------
// MÊ CUNG NGHIÊNG rendering + UI
// ---------------------------------------------------------------------------

void MazeFillRect(uint16_t* buffer, int x, int y, int width, int height, uint16_t color) {
    if (buffer == nullptr || width <= 0 || height <= 0) {
        return;
    }
    const int x0 = std::max(0, x);
    const int y0 = std::max(0, y);
    const int x1 = std::min(kMazeCanvasPx, x + width);
    const int y1 = std::min(kMazeCanvasPx, y + height);
    // A world object can legitimately be completely outside the camera. In
    // that case clipping produces a reversed/empty interval (for example
    // x0=300, x1=240). Passing that to std::fill is undefined behavior: it
    // walks forward through PSRAM looking for an end pointer behind it.
    if (x0 >= x1 || y0 >= y1) {
        return;
    }
    for (int row = y0; row < y1; ++row) {
        std::fill(buffer + row * kMazeCanvasPx + x0,
                  buffer + row * kMazeCanvasPx + x1, color);
    }
}

void MazeFillCircle(uint16_t* buffer, int center_x, int center_y, int radius, uint16_t color) {
    if (buffer == nullptr || radius < 0 ||
        center_x + radius < 0 || center_x - radius >= kMazeCanvasPx ||
        center_y + radius < 0 || center_y - radius >= kMazeCanvasPx) {
        return;
    }
    for (int dy = -radius; dy <= radius; ++dy) {
        const int half = static_cast<int>(std::sqrt(
            static_cast<float>(radius * radius - dy * dy)));
        MazeFillRect(buffer, center_x - half, center_y + dy, half * 2 + 1, 1, color);
    }
}

void MazeDrawLine(uint16_t* buffer, int x0, int y0, int x1, int y1,
                  int thickness, uint16_t color) {
    const int dx = std::abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    while (true) {
        MazeFillCircle(buffer, x0, y0, std::max(0, thickness / 2), color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int twice = error * 2;
        if (twice >= dy) {
            error += dy;
            x0 += sx;
        }
        if (twice <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

int MazeWorldToScreen(float value, float camera) {
    return static_cast<int>(std::lround(value - camera));
}

float MazeCellCenter(uint8_t coordinate) {
    return coordinate * TiltMazeGame::kCellSize + TiltMazeGame::kCellSize * 0.5f;
}

void DrawMazeWormhole(uint16_t* buffer, int x, int y, uint32_t rgb, uint8_t style) {
    const uint16_t outer = SnakeColor565(rgb);
    const uint16_t inner = SnakeColor565(kMazePanelBg);
    MazeFillCircle(buffer, x, y, 7, outer);
    MazeFillCircle(buffer, x, y, 4, inner);
    if (style == 0) {
        MazeFillRect(buffer, x - 1, y - 6, 3, 3, SnakeColor565(0xFFFFFF));
    } else {
        MazeFillRect(buffer, x + 3, y - 4, 3, 3, SnakeColor565(0xFFFFFF));
        MazeFillRect(buffer, x - 5, y + 2, 2, 2, SnakeColor565(0xFFFFFF));
    }
}

void RedrawTiltMaze() {
    if (mazeCanvasBuf == nullptr || mazeCanvas == nullptr) {
        return;
    }
    uint16_t* buffer = mazeCanvasBuf;
    std::fill(buffer, buffer + kMazeCanvasPx * kMazeCanvasPx,
              SnakeColor565(kMazePanelBg));

    const TiltMazeGame::State& state = TiltMazeGame::GetState();
    const TiltMazeGame::LevelDefinition& level = TiltMazeGame::GetLevel(state.level_index);
    const float camera_x = state.camera_x;
    const float camera_y = state.camera_y;

    for (uint8_t row = 0; row < level.height; ++row) {
        for (uint8_t col = 0; col < level.width; ++col) {
            const int x = MazeWorldToScreen(col * TiltMazeGame::kCellSize, camera_x);
            const int y = MazeWorldToScreen(row * TiltMazeGame::kCellSize, camera_y);
            if (x >= kMazeCanvasPx || y >= kMazeCanvasPx ||
                x + TiltMazeGame::kCellSize <= 0 || y + TiltMazeGame::kCellSize <= 0) {
                continue;
            }
            const char tile = level.rows[row][col];
            const bool breakable = tile == 'X' && !TiltMazeGame::IsBreakableBroken(col, row);
            if (tile == '#' || breakable) {
                const uint32_t main_color = breakable ? kMazeBreakable : kMazeWall;
                const uint32_t shade_color = breakable ? 0xB94347 : kMazeWallShade;
                MazeFillRect(buffer, x, y, TiltMazeGame::kCellSize,
                             TiltMazeGame::kCellSize, SnakeColor565(shade_color));
                MazeFillRect(buffer, x + 2, y + 2, TiltMazeGame::kCellSize - 4,
                             TiltMazeGame::kCellSize - 4, SnakeColor565(main_color));
                MazeFillRect(buffer, x + 4, y + 4, TiltMazeGame::kCellSize - 8, 3,
                             SnakeColor565(breakable ? 0xFFB09B : kMazeWallLight));
                if (breakable) {
                    MazeDrawLine(buffer, x + 20, y + 5, x + 15, y + 18, 2,
                                 SnakeColor565(0x6D3040));
                    MazeDrawLine(buffer, x + 15, y + 18, x + 26, y + 25, 2,
                                 SnakeColor565(0x6D3040));
                    MazeDrawLine(buffer, x + 26, y + 25, x + 20, y + 38, 2,
                                 SnakeColor565(0x6D3040));
                }
            } else {
                MazeFillRect(buffer, x, y, TiltMazeGame::kCellSize,
                             TiltMazeGame::kCellSize, SnakeColor565(kMazeFloor));
                if (((row * 3 + col) & 3U) == 0) {
                    MazeFillCircle(buffer, x + 8, y + 8, 1, SnakeColor565(kMazeFloorDot));
                }
            }
        }
    }

    // Destination under every moving/collectible object.
    const int goal_x = MazeWorldToScreen(MazeCellCenter(level.goal.col), camera_x);
    const int goal_y = MazeWorldToScreen(MazeCellCenter(level.goal.row), camera_y);
    MazeFillCircle(buffer, goal_x, goal_y, 15, SnakeColor565(0x1D6F4B));
    MazeFillCircle(buffer, goal_x, goal_y, 11, SnakeColor565(kMazeGoal));
    MazeFillCircle(buffer, goal_x, goal_y, 6, SnakeColor565(kMazeFloor));
    MazeDrawLine(buffer, goal_x - 3, goal_y, goal_x, goal_y + 4, 2,
                 SnakeColor565(0xFFFFFF));
    MazeDrawLine(buffer, goal_x, goal_y + 4, goal_x + 6, goal_y - 5, 2,
                 SnakeColor565(0xFFFFFF));

    for (uint8_t i = 0; i < level.gold_count; ++i) {
        if (TiltMazeGame::IsGoldCollected(i)) {
            continue;
        }
        const int x = MazeWorldToScreen(MazeCellCenter(level.gold[i].col), camera_x);
        const int y = MazeWorldToScreen(MazeCellCenter(level.gold[i].row), camera_y);
        MazeFillCircle(buffer, x, y, 7, SnakeColor565(0x9A6315));
        MazeFillCircle(buffer, x, y, 5, SnakeColor565(kMazeGold));
        MazeFillRect(buffer, x - 1, y - 4, 3, 9, SnakeColor565(0xFFF7B0));
        MazeFillRect(buffer, x - 4, y - 1, 9, 3, SnakeColor565(0xFFF7B0));
    }

    for (uint8_t i = 0; i < level.wormhole_count; ++i) {
        const auto& pair = level.wormholes[i];
        DrawMazeWormhole(
            buffer,
            MazeWorldToScreen(MazeCellCenter(pair.a.col) + pair.a_offset_x, camera_x),
            MazeWorldToScreen(MazeCellCenter(pair.a.row) + pair.a_offset_y, camera_y),
            pair.style == 0 ? kMazeWormA : kMazeWormB, pair.style);
        DrawMazeWormhole(
            buffer,
            MazeWorldToScreen(MazeCellCenter(pair.b.col) + pair.b_offset_x, camera_x),
            MazeWorldToScreen(MazeCellCenter(pair.b.row) + pair.b_offset_y, camera_y),
            pair.style == 0 ? kMazeWormA : kMazeWormB, pair.style);
    }

    for (uint8_t i = 0; i < level.boost_count; ++i) {
        const auto& boost = level.boosts[i];
        const int x = MazeWorldToScreen(MazeCellCenter(boost.cell.col), camera_x);
        const int y = MazeWorldToScreen(MazeCellCenter(boost.cell.row), camera_y);
        const int dx = boost.direction_x;
        const int dy = boost.direction_y;
        // Two bright chevrons. Level 4 points right, while this remains generic
        // for future up/down/left pads.
        for (int offset : {-5, 4}) {
            const int cx = x + dx * offset;
            const int cy = y + dy * offset;
            const int px = -dy;
            const int py = dx;
            MazeDrawLine(buffer, cx - dx * 5 + px * 5, cy - dy * 5 + py * 5,
                         cx + dx * 5, cy + dy * 5, 3, SnakeColor565(kMazeAccent));
            MazeDrawLine(buffer, cx + dx * 5, cy + dy * 5,
                         cx - dx * 5 - px * 5, cy - dy * 5 - py * 5,
                         3, SnakeColor565(kMazeAccent));
        }
    }

    const int ball_x = MazeWorldToScreen(state.ball_x, camera_x);
    const int ball_y = MazeWorldToScreen(state.ball_y, camera_y);
    MazeFillCircle(buffer, ball_x + 2, ball_y + 3, TiltMazeGame::kBallRadius,
                   SnakeColor565(0x06111D));
    MazeFillCircle(buffer, ball_x, ball_y, TiltMazeGame::kBallRadius,
                   SnakeColor565(state.boost_style ? kMazeAccent : 0xFFFFFF));
    MazeFillCircle(buffer, ball_x - 2, ball_y - 2, 2, SnakeColor565(0xFFFFFF));

    lv_obj_invalidate(mazeCanvas);
}

lv_obj_t* CreateMazeControl(lv_obj_t* parent, int x, int y, int size, const char* text,
                            uint32_t color) {
    lv_obj_t* button = lv_obj_create(parent);
    lv_obj_remove_style_all(button);
    lv_obj_set_size(button, size, size);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(kMazePanelBg), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_80, 0);
    lv_obj_set_style_border_width(button, 2, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(color), 0);
    lv_obj_set_style_border_opa(button, LV_OPA_COVER, 0);
    lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* label = lv_label_create(button);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_vn_22, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    lv_obj_add_flag(button, LV_OBJ_FLAG_HIDDEN);
    return button;
}

void HideTiltMazeAll() {
    for (lv_obj_t* object : {mazeCanvas, mazeHud, mazeCalibrationScreen,
                             mazeCountdownLabel, mazePlanningTitle, mazePlanUpBtn,
                             mazePlanDownBtn, mazePlanLeftBtn, mazePlanRightBtn,
                             mazePlanResumeBtn, mazeResultScreen}) {
        if (object != nullptr) {
            lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
        }
    }
    mazeVisiblePhase = TiltMazeGame::Phase::kStopped;
}

void KeepTiltMazeHudVisible() {
    if (mazeHud == nullptr) {
        return;
    }
    lv_obj_remove_flag(mazeHud, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(mazeHud);
}

void ApplyTiltMazePhase(TiltMazeGame::Phase phase) {
    HideTiltMazeAll();
    if (phase == TiltMazeGame::Phase::kComplete && mazeVisiblePhase != phase) {
        // A cleared level pays the flat game boost plus 5 per star, capped
        // like Quick Tap and Snake.
        const uint8_t stars = TiltMazeGame::GetState().last_run_stars;
        const int earned = ((stars & 1U) ? 1 : 0) + ((stars & 2U) ? 1 : 0) + ((stars & 4U) ? 1 : 0);
        RewardGameMood("tilt_maze", std::min(CareSystem::kGamesBoost + 5 * earned, 25));
    }
    mazeVisiblePhase = phase;
    switch (phase) {
        case TiltMazeGame::Phase::kCalibrating:
            lv_obj_remove_flag(mazeCalibrationScreen, LV_OBJ_FLAG_HIDDEN);
            break;
        case TiltMazeGame::Phase::kCountdown:
            lv_obj_remove_flag(mazeCanvas, LV_OBJ_FLAG_HIDDEN);
            KeepTiltMazeHudVisible();
            lv_obj_remove_flag(mazeCountdownLabel, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(mazeCountdownLabel);
            break;
        case TiltMazeGame::Phase::kPlaying:
            lv_obj_remove_flag(mazeCanvas, LV_OBJ_FLAG_HIDDEN);
            KeepTiltMazeHudVisible();
            break;
        case TiltMazeGame::Phase::kPlanning:
            // Keep the canvas behind every overlay. Moving it to the foreground here
            // used to leave it above the HUD after planning mode was closed.
            lv_obj_remove_flag(mazeCanvas, LV_OBJ_FLAG_HIDDEN);
            for (lv_obj_t* object : {mazePlanningTitle, mazePlanUpBtn, mazePlanDownBtn,
                                     mazePlanLeftBtn, mazePlanRightBtn, mazePlanResumeBtn}) {
                lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
                lv_obj_move_foreground(object);
            }
            break;
        case TiltMazeGame::Phase::kComplete:
            lv_obj_remove_flag(mazeResultScreen, LV_OBJ_FLAG_HIDDEN);
            break;
        case TiltMazeGame::Phase::kStopped:
            break;
    }
}

void UpdateTiltMazeUI() {
    const TiltMazeGame::State& state = TiltMazeGame::GetState();
    const TiltMazeGame::LevelDefinition& level = TiltMazeGame::GetLevel(state.level_index);
    if (state.phase != mazeVisiblePhase) {
        ApplyTiltMazePhase(state.phase);
    }

    if (state.phase == TiltMazeGame::Phase::kCalibrating) {
        lv_label_set_text(mazeCalibrationStatus,
                          state.neutral_stable ? "ĐÃ ỔN ĐỊNH" : "GIỮ YÊN MỘT CHÚT");
        lv_obj_set_style_text_color(mazeCalibrationStatus,
                                    lv_color_hex(state.neutral_stable ? kMazeGoal : kMazeGold), 0);
        lv_obj_set_style_border_color(mazeCalibrateBtn,
                                      lv_color_hex(state.neutral_stable ? kMazeGoal : kMazeMuted), 0);
        return;
    }

    if (state.phase == TiltMazeGame::Phase::kPlaying ||
        state.phase == TiltMazeGame::Phase::kCountdown ||
        state.phase == TiltMazeGame::Phase::kPlanning) {
        RedrawTiltMaze();
    }

    if (state.phase == TiltMazeGame::Phase::kPlaying ||
        state.phase == TiltMazeGame::Phase::kCountdown) {
        // Reassert both visibility and z-order every frame so another overlay cannot
        // leave the always-on play HUD hidden or behind the full-screen canvas.
        KeepTiltMazeHudVisible();
        const uint32_t seconds = state.elapsed_ms / 1000U;
        lv_label_set_text_fmt(mazeHud, "L%u   %u/%u   %02u:%02u",
                              static_cast<unsigned>(state.level_index + 1),
                              static_cast<unsigned>(TiltMazeGame::CountCollectedGold()),
                              static_cast<unsigned>(level.gold_count),
                              static_cast<unsigned>(seconds / 60U),
                              static_cast<unsigned>(seconds % 60U));
    }
    if (state.phase == TiltMazeGame::Phase::kCountdown) {
        const unsigned count = static_cast<unsigned>(
            std::max<uint32_t>(1U, (state.countdown_remaining_ms + 999U) / 1000U));
        lv_label_set_text_fmt(mazeCountdownLabel, "%u", count);
        lv_obj_move_foreground(mazeCountdownLabel);
    }
    if (state.phase == TiltMazeGame::Phase::kComplete) {
        for (size_t i = 0; i < mazeResultStars.size(); ++i) {
            const bool earned = (state.last_run_stars & (1U << i)) != 0;
            lv_obj_set_style_bg_color(mazeResultStars[i],
                                      lv_color_hex(earned ? kMazeGold : 0x263D50), 0);
            lv_obj_set_style_border_color(mazeResultStars[i],
                                          lv_color_hex(earned ? 0xFFF2A1 : kMazeMuted), 0);
        }
        lv_label_set_text_fmt(mazeResultSummary, "%u/%u VÀNG  ·  %u GIÂY",
                              static_cast<unsigned>(TiltMazeGame::CountCollectedGold()),
                              static_cast<unsigned>(level.gold_count),
                              static_cast<unsigned>(state.elapsed_ms / 1000U));
        lv_obj_t* primary_label = lv_obj_get_child(mazeResultPrimaryBtn, 0);
        if (primary_label != nullptr) {
            lv_label_set_text(primary_label,
                              state.level_index + 1 < TiltMazeGame::kImplementedLevels
                                  ? "LEVEL TIẾP" : "HOÀN TẤT");
        }
    }
}

void TiltMazeTimerCb(lv_timer_t*) {
    if (currentState != MENU_GAME_ACTIVE || activeGame != ACTIVE_GAME_TILT_MAZE) {
        return;
    }
    TiltMazeGame::Tick(lv_tick_get());
    UpdateTiltMazeUI();
}

void StartTiltMazeTimer() {
    if (mazeTimer == nullptr) {
        mazeTimer = lv_timer_create(TiltMazeTimerCb, kMazeTickMs, nullptr);
    }
}

void StopTiltMazeTimer() {
    if (mazeTimer != nullptr) {
        lv_timer_delete(mazeTimer);
        mazeTimer = nullptr;
    }
}

void CreateTiltMazeUI() {
    if (gamesPanel == nullptr || mazeCalibrationScreen != nullptr) {
        return;
    }

    // LVGL 9's lv_color_t is a 24-bit logical color, while this canvas stores
    // packed RGB565 pixels. Size the backing memory for the actual format.
    const size_t bytes = static_cast<size_t>(kMazeCanvasPx) * kMazeCanvasPx *
                         sizeof(uint16_t);
    mazeCanvasBuf = static_cast<uint16_t*>(
        heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (mazeCanvasBuf == nullptr) {
        ESP_LOGE(TAG, "Tilt maze: failed to allocate the %u-byte canvas",
                 static_cast<unsigned>(bytes));
    } else {
        mazeCanvas = lv_canvas_create(gamesPanel);
        lv_obj_remove_style_all(mazeCanvas);
        lv_obj_set_size(mazeCanvas, kMazeCanvasPx, kMazeCanvasPx);
        lv_obj_center(mazeCanvas);
        lv_canvas_set_buffer(mazeCanvas, mazeCanvasBuf, kMazeCanvasPx, kMazeCanvasPx,
                             LV_COLOR_FORMAT_RGB565);
        lv_obj_add_flag(mazeCanvas, LV_OBJ_FLAG_HIDDEN);
    }

    mazeHud = lv_label_create(gamesPanel);
    lv_obj_set_style_text_font(mazeHud, &lv_font_montserrat_vn_20, 0);
    lv_obj_set_style_text_color(mazeHud, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_color(mazeHud, lv_color_hex(kMazePanelBg), 0);
    lv_obj_set_style_bg_opa(mazeHud, LV_OPA_80, 0);
    lv_obj_set_style_radius(mazeHud, 12, 0);
    lv_obj_set_style_pad_left(mazeHud, 10, 0);
    lv_obj_set_style_pad_right(mazeHud, 10, 0);
    lv_obj_set_style_pad_top(mazeHud, 3, 0);
    lv_obj_set_style_pad_bottom(mazeHud, 3, 0);
    lv_label_set_text(mazeHud, "L1   0/2   00:00");
    lv_obj_align(mazeHud, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_add_flag(mazeHud, LV_OBJ_FLAG_HIDDEN);

    mazeCountdownLabel = lv_label_create(gamesPanel);
    lv_obj_set_style_text_font(mazeCountdownLabel, &lv_font_montserrat_vn_28, 0);
    lv_obj_set_style_text_color(mazeCountdownLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_color(mazeCountdownLabel, lv_color_hex(kMazeAccent), 0);
    lv_obj_set_style_bg_opa(mazeCountdownLabel, LV_OPA_80, 0);
    lv_obj_set_style_radius(mazeCountdownLabel, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(mazeCountdownLabel, 16, 0);
    lv_label_set_text(mazeCountdownLabel, "3");
    lv_obj_center(mazeCountdownLabel);
    lv_obj_add_flag(mazeCountdownLabel, LV_OBJ_FLAG_HIDDEN);

    mazeCalibrationScreen = lv_obj_create(gamesPanel);
    lv_obj_remove_style_all(mazeCalibrationScreen);
    lv_obj_set_size(mazeCalibrationScreen, 224, 224);
    lv_obj_center(mazeCalibrationScreen);
    lv_obj_set_style_bg_color(mazeCalibrationScreen, lv_color_hex(kMazePanelBg), 0);
    lv_obj_set_style_bg_opa(mazeCalibrationScreen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(mazeCalibrationScreen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* calibration_title = lv_label_create(mazeCalibrationScreen);
    lv_obj_set_style_text_font(calibration_title, &lv_font_montserrat_vn_22, 0);
    lv_obj_set_style_text_color(calibration_title, lv_color_hex(kMazeAccent), 0);
    lv_label_set_text(calibration_title, "VÀO VỊ TRÍ");
    lv_obj_align(calibration_title, LV_ALIGN_CENTER, 0, -72);

    lv_obj_t* calibration_hint = lv_label_create(mazeCalibrationScreen);
    lv_obj_set_style_text_font(calibration_hint, &lv_font_montserrat_vn_20, 0);
    lv_obj_set_style_text_color(calibration_hint, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(calibration_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(calibration_hint, "NGỬA MẶT\nBUBU LÊN");
    lv_obj_align(calibration_hint, LV_ALIGN_CENTER, 0, -24);

    mazeCalibrationStatus = lv_label_create(mazeCalibrationScreen);
    lv_obj_set_style_text_font(mazeCalibrationStatus, &lv_font_montserrat_vn_20, 0);
    lv_obj_set_style_text_color(mazeCalibrationStatus, lv_color_hex(kMazeGold), 0);
    lv_label_set_text(mazeCalibrationStatus, "GIỮ YÊN MỘT CHÚT");
    lv_obj_align(mazeCalibrationStatus, LV_ALIGN_CENTER, 0, 24);

    mazeCalibrateBtn = CreateMazeControl(mazeCalibrationScreen, 84, 148, 56, "OK", kMazeGoal);
    lv_obj_set_style_text_font(lv_obj_get_child(mazeCalibrateBtn, 0), &font_awesome_20_4, 0);
    lv_label_set_text(lv_obj_get_child(mazeCalibrateBtn, 0), FONT_AWESOME_CHECK);
    lv_obj_remove_flag(mazeCalibrateBtn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(mazeCalibrationScreen, LV_OBJ_FLAG_HIDDEN);

    mazePlanningTitle = lv_label_create(gamesPanel);
    lv_obj_set_style_text_font(mazePlanningTitle, &lv_font_montserrat_vn_20, 0);
    lv_obj_set_style_text_color(mazePlanningTitle, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_color(mazePlanningTitle, lv_color_hex(kMazePanelBg), 0);
    lv_obj_set_style_bg_opa(mazePlanningTitle, LV_OPA_80, 0);
    lv_obj_set_style_radius(mazePlanningTitle, 10, 0);
    lv_obj_set_style_pad_hor(mazePlanningTitle, 8, 0);
    lv_obj_set_style_pad_ver(mazePlanningTitle, 3, 0);
    lv_label_set_text(mazePlanningTitle, "XEM MAP");
    lv_obj_align(mazePlanningTitle, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_add_flag(mazePlanningTitle, LV_OBJ_FLAG_HIDDEN);

    mazePlanUpBtn = CreateMazeControl(gamesPanel, 102, 42, 36, "^", kMazeAccent);
    mazePlanDownBtn = CreateMazeControl(gamesPanel, 102, 162, 36, "v", kMazeAccent);
    mazePlanLeftBtn = CreateMazeControl(gamesPanel, 42, 102, 36, "<", kMazeAccent);
    mazePlanRightBtn = CreateMazeControl(gamesPanel, 162, 102, 36, ">", kMazeAccent);
    mazePlanResumeBtn = CreateMazeControl(gamesPanel, 92, 92, 56, "OK", kMazeGoal);
    lv_obj_set_style_text_font(lv_obj_get_child(mazePlanResumeBtn, 0), &font_awesome_20_4, 0);
    lv_label_set_text(lv_obj_get_child(mazePlanResumeBtn, 0), FONT_AWESOME_CHECK);

    mazeResultScreen = lv_obj_create(gamesPanel);
    lv_obj_remove_style_all(mazeResultScreen);
    lv_obj_set_size(mazeResultScreen, 224, 224);
    lv_obj_center(mazeResultScreen);
    lv_obj_set_style_bg_color(mazeResultScreen, lv_color_hex(kMazePanelBg), 0);
    lv_obj_set_style_bg_opa(mazeResultScreen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(mazeResultScreen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* result_title = lv_label_create(mazeResultScreen);
    lv_obj_set_style_text_font(result_title, &lv_font_montserrat_vn_22, 0);
    lv_obj_set_style_text_color(result_title, lv_color_hex(kMazeGoal), 0);
    lv_label_set_text(result_title, "HOÀN THÀNH!");
    lv_obj_align(result_title, LV_ALIGN_CENTER, 0, -76);

    for (size_t i = 0; i < mazeResultStars.size(); ++i) {
        lv_obj_t* medal = lv_obj_create(mazeResultScreen);
        mazeResultStars[i] = medal;
        lv_obj_remove_style_all(medal);
        lv_obj_set_size(medal, 34, 34);
        lv_obj_set_pos(medal, 48 + static_cast<int>(i) * 47, 57);
        lv_obj_set_style_radius(medal, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(medal, lv_color_hex(0x263D50), 0);
        lv_obj_set_style_bg_opa(medal, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(medal, 2, 0);
        lv_obj_set_style_border_color(medal, lv_color_hex(kMazeMuted), 0);
        lv_obj_t* mark = lv_label_create(medal);
        lv_obj_set_style_text_font(mark, &font_awesome_20_4, 0);
        lv_obj_set_style_text_color(mark, lv_color_hex(kMazePanelBg), 0);
        lv_label_set_text(mark, FONT_AWESOME_STAR);
        lv_obj_center(mark);
    }

    mazeResultSummary = lv_label_create(mazeResultScreen);
    lv_obj_set_style_text_font(mazeResultSummary, &lv_font_montserrat_vn_20, 0);
    lv_obj_set_style_text_color(mazeResultSummary, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(mazeResultSummary, "0/0 VÀNG · 0 GIÂY");
    lv_obj_align(mazeResultSummary, LV_ALIGN_CENTER, 0, 14);

    mazeResultPrimaryBtn = CreateQuickTapPill(mazeResultScreen, 136, 34, 0, 52,
                                              "LEVEL TIẾP", &lv_font_montserrat_vn_20);
    StyleQuickTapPill(mazeResultPrimaryBtn, kMazeGoal, kMazeGoal, kMazePanelBg);
    mazeResultRetryBtn = CreateQuickTapPill(mazeResultScreen, 112, 28, 0, 86,
                                            "CHƠI LẠI", &lv_font_montserrat_vn_20);
    StyleQuickTapPill(mazeResultRetryBtn, 0x173B4F, kMazeMuted, 0xFFFFFF);
    lv_obj_add_flag(mazeResultScreen, LV_OBJ_FLAG_HIDDEN);

    TiltMazeGame::Initialize();
    HideTiltMazeAll();
}

// ---------------------------------------------------------------------------
// ĐƯỜNG PHỐ rendering + UI
// ---------------------------------------------------------------------------

int RunnerLaneX(int8_t lane) {
    constexpr int kLaneCenters[] = {64, 120, 176};
    return kLaneCenters[std::clamp<int>(lane, 0, 2)];
}

void DrawRunnerBoost(uint16_t* buffer, int x, int y) {
    MazeFillCircle(buffer, x + 1, y + 2, 13, SnakeColor565(0x06131D));
    MazeFillCircle(buffer, x, y, 11, SnakeColor565(kRunnerAccent));
    MazeFillCircle(buffer, x, y, 9, SnakeColor565(0xB8F8FF));
    MazeFillCircle(buffer, x, y, 8, SnakeColor565(kRunnerRoad));
    MazeDrawLine(buffer, x + 3, y - 6, x - 3, y, 3, SnakeColor565(0xFFFFFF));
    MazeDrawLine(buffer, x - 3, y, x + 2, y, 3, SnakeColor565(0xFFFFFF));
    MazeDrawLine(buffer, x + 2, y, x - 2, y + 7, 3, SnakeColor565(0xFFFFFF));
}

void RedrawTrafficRunner() {
    if (runnerCanvasBuf == nullptr || runnerCanvas == nullptr) {
        return;
    }

    uint16_t* buffer = runnerCanvasBuf;
    const auto& state = TrafficRunnerGame::GetState();
    std::fill(buffer, buffer + kRunnerCanvasPx * kRunnerCanvasPx,
              SnakeColor565(kRunnerPanelBg));

    // Layered road and shoulders give the simple geometry enough depth for
    // the small round display without turning any object into a car sprite.
    MazeFillRect(buffer, 31, 0, 178, 240, SnakeColor565(0x081A24));
    MazeFillRect(buffer, 34, 0, 172, 240, SnakeColor565(0x284553));
    MazeFillRect(buffer, 38, 0, 164, 240, SnakeColor565(kRunnerRoad));
    MazeFillRect(buffer, 40, 0, 51, 240, SnakeColor565(0x112D3A));
    MazeFillRect(buffer, 94, 0, 53, 240, SnakeColor565(0x0E2633));
    MazeFillRect(buffer, 150, 0, 50, 240, SnakeColor565(0x112D3A));

    // The divider and shoulder markers share the world scroll offset. Moving
    // them downward at the actual game speed makes the road feel in motion.
    const int road_offset = static_cast<int>(std::lround(state.road_scroll_px));
    for (int y = -24 + road_offset; y < 240; y += 32) {
        MazeFillRect(buffer, 91, y + 1, 4, 15, SnakeColor565(0x071721));
        MazeFillRect(buffer, 147, y + 1, 4, 15, SnakeColor565(0x071721));
        MazeFillRect(buffer, 92, y, 2, 14, SnakeColor565(kRunnerLane));
        MazeFillRect(buffer, 148, y, 2, 14, SnakeColor565(kRunnerLane));
        MazeFillRect(buffer, 34, y, 4, 12, SnakeColor565(kRunnerAccent));
        MazeFillRect(buffer, 202, y, 4, 12, SnakeColor565(kRunnerAccent));
    }

    constexpr uint32_t kObstacleColors[] = {
        0xFF6B5C, 0x9B73FF, 0x53E06F, 0xFF9F43,
    };
    constexpr uint32_t kObstacleHighlights[] = {
        0xFFAAA1, 0xC4ADFF, 0x9CF0AA, 0xFFD09B,
    };
    const TrafficRunnerGame::Entity* entities = TrafficRunnerGame::GetEntities();
    for (size_t index = 0; index < TrafficRunnerGame::GetEntityCount(); ++index) {
        const auto& entity = entities[index];
        if (!entity.active) {
            continue;
        }
        const int x = RunnerLaneX(entity.lane);
        const int y = static_cast<int>(std::lround(entity.y));
        if (entity.type == TrafficRunnerGame::EntityType::kObstacle) {
            const int height = 20 + static_cast<int>(entity.length) * 14;
            const size_t style = entity.style % 4;
            MazeFillRect(buffer, x - 18, y - height / 2 + 4, 36, height,
                         SnakeColor565(0x06131D));
            MazeFillRect(buffer, x - 17, y - height / 2, 34, height,
                         SnakeColor565(0x263743));
            MazeFillRect(buffer, x - 15, y - height / 2 + 2, 30, height - 4,
                         SnakeColor565(kObstacleColors[style]));
            MazeFillRect(buffer, x - 11, y - height / 2 + 6, 22, 3,
                         SnakeColor565(kObstacleHighlights[style]));
            MazeFillRect(buffer, x - 11, y + height / 2 - 9, 22, 3,
                         SnakeColor565(0x263743));
        } else if (entity.type == TrafficRunnerGame::EntityType::kCoin) {
            MazeFillCircle(buffer, x + 1, y + 2, 10, SnakeColor565(0x06131D));
            MazeFillCircle(buffer, x, y, 8, SnakeColor565(0xA86C13));
            MazeFillCircle(buffer, x, y, 6, SnakeColor565(kRunnerGold));
            MazeFillRect(buffer, x - 1, y - 5, 3, 10, SnakeColor565(0xFFF4A8));
            MazeFillCircle(buffer, x - 3, y - 3, 1, SnakeColor565(0xFFFFFF));
        } else {
            DrawRunnerBoost(buffer, x, y);
        }
    }

    const int player_x = RunnerLaneX(state.player_lane);
    MazeFillCircle(buffer, player_x + 1, 198, 13, SnakeColor565(0x06131D));
    if (TrafficRunnerGame::IsBoostActive()) {
        MazeFillCircle(buffer, player_x, 224, 3, SnakeColor565(0x247C8C));
        MazeFillCircle(buffer, player_x, 214, 5, SnakeColor565(kRunnerAccent));
        MazeFillCircle(buffer, player_x, 194, 15, SnakeColor565(kRunnerAccent));
        MazeFillCircle(buffer, player_x, 194, 12, SnakeColor565(kRunnerRoad));
    }
    MazeFillCircle(buffer, player_x, 194, 12, SnakeColor565(0xB8F8FF));
    MazeFillCircle(buffer, player_x, 194, 10, SnakeColor565(0xFFFFFF));
    MazeFillCircle(buffer, player_x - 3, 191, 2, SnakeColor565(0xE8FDFF));

    lv_obj_invalidate(runnerCanvas);
}

void HideTrafficRunnerAll() {
    for (lv_obj_t* object : {runnerCanvas, runnerHudScore, runnerHudGold, runnerHudBoost,
                             runnerSetupScreen, runnerPhaseLabel, runnerOverScreen}) {
        if (object != nullptr) {
            lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
        }
    }
    runnerVisiblePhase = TrafficRunnerGame::Phase::kStopped;
}

void KeepRunnerPlayfieldVisible() {
    for (lv_obj_t* object : {runnerCanvas, runnerHudScore, runnerHudGold}) {
        if (object != nullptr) {
            lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(object);
        }
    }
    if (runnerCanvas != nullptr) {
        lv_obj_move_background(runnerCanvas);
    }
}

void ApplyTrafficRunnerPhase(TrafficRunnerGame::Phase phase) {
    HideTrafficRunnerAll();
    if (phase == TrafficRunnerGame::Phase::kGameOver && runnerVisiblePhase != phase &&
        runnerVisiblePhase != TrafficRunnerGame::Phase::kStopped) {
        // A run that ended (not the setup screen, which is shown by forcing the
        // visible phase to kGameOver when the game opens). Scaled and capped.
        const uint32_t score = TrafficRunnerGame::GetState().score;
        RewardGameMood("traffic_runner", std::min<int>(5 + static_cast<int>(score / 100), 25));
    }
    runnerVisiblePhase = phase;
    switch (phase) {
        case TrafficRunnerGame::Phase::kStopped:
            if (runnerSetupScreen != nullptr) {
                lv_obj_remove_flag(runnerSetupScreen, LV_OBJ_FLAG_HIDDEN);
            }
            break;
        case TrafficRunnerGame::Phase::kCalibrating:
        case TrafficRunnerGame::Phase::kCountdown:
            KeepRunnerPlayfieldVisible();
            if (runnerPhaseLabel != nullptr) {
                lv_obj_remove_flag(runnerPhaseLabel, LV_OBJ_FLAG_HIDDEN);
                lv_obj_move_foreground(runnerPhaseLabel);
            }
            break;
        case TrafficRunnerGame::Phase::kPlaying:
            KeepRunnerPlayfieldVisible();
            break;
        case TrafficRunnerGame::Phase::kGameOver:
            if (runnerOverScreen != nullptr) {
                lv_obj_remove_flag(runnerOverScreen, LV_OBJ_FLAG_HIDDEN);
                lv_obj_move_foreground(runnerOverScreen);
            }
            break;
    }
}

void UpdateTrafficRunnerUI() {
    const auto& state = TrafficRunnerGame::GetState();
    if (state.phase != runnerVisiblePhase) {
        ApplyTrafficRunnerPhase(state.phase);
    }

    if (state.phase == TrafficRunnerGame::Phase::kStopped) {
        if (runnerSetupBest != nullptr) {
            lv_label_set_text_fmt(runnerSetupBest, "KỶ LỤC %u",
                                  static_cast<unsigned>(state.high_score));
        }
        return;
    }

    if (state.phase == TrafficRunnerGame::Phase::kCalibrating ||
        state.phase == TrafficRunnerGame::Phase::kCountdown ||
        state.phase == TrafficRunnerGame::Phase::kPlaying) {
        RedrawTrafficRunner();
        KeepRunnerPlayfieldVisible();
        lv_label_set_text_fmt(runnerHudScore, "%u", static_cast<unsigned>(state.score));
        lv_label_set_text_fmt(runnerHudGold, "VÀNG %u", static_cast<unsigned>(state.gold));
    }

    if (state.phase == TrafficRunnerGame::Phase::kCalibrating) {
        lv_label_set_text(runnerPhaseLabel, "GIỮ BUBU THẲNG");
    } else if (state.phase == TrafficRunnerGame::Phase::kCountdown) {
        const unsigned count = static_cast<unsigned>(
            std::max<uint32_t>(1, (state.countdown_remaining_ms + 999) / 1000));
        lv_label_set_text_fmt(runnerPhaseLabel, "%u", count);
    }

    if (state.phase == TrafficRunnerGame::Phase::kPlaying &&
        state.boost_remaining_ms > 0) {
        lv_label_set_text_fmt(runnerHudBoost, "BOOST %.1fs",
                              static_cast<double>(state.boost_remaining_ms) / 1000.0);
        lv_obj_remove_flag(runnerHudBoost, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(runnerHudBoost);
    } else if (runnerHudBoost != nullptr) {
        lv_obj_add_flag(runnerHudBoost, LV_OBJ_FLAG_HIDDEN);
    }

    if (state.phase == TrafficRunnerGame::Phase::kGameOver) {
        lv_label_set_text(runnerOverTitle, state.new_high_score ? "KỶ LỤC MỚI!" : "VA CHẠM");
        lv_obj_set_style_text_color(runnerOverTitle,
                                    lv_color_hex(state.new_high_score ? kRunnerGold : 0xFF6B5C), 0);
        lv_label_set_text_fmt(runnerOverScore, "ĐIỂM %u",
                              static_cast<unsigned>(state.score));
        lv_label_set_text_fmt(runnerOverBest, "KỶ LỤC %u",
                              static_cast<unsigned>(state.high_score));
    }
}

void TrafficRunnerTimerCb(lv_timer_t*) {
    if (currentState != MENU_GAME_ACTIVE || activeGame != ACTIVE_GAME_TRAFFIC_RUNNER) {
        return;
    }
    TrafficRunnerGame::Tick(lv_tick_get());
    UpdateTrafficRunnerUI();
}

void StartTrafficRunnerTimer() {
    if (runnerTimer == nullptr) {
        runnerTimer = lv_timer_create(TrafficRunnerTimerCb, kRunnerTickMs, nullptr);
    }
}

void StopTrafficRunnerTimer() {
    if (runnerTimer != nullptr) {
        lv_timer_delete(runnerTimer);
        runnerTimer = nullptr;
    }
}

lv_obj_t* CreateRunnerLabel(lv_obj_t* parent, const lv_font_t* font,
                            uint32_t color, const char* text, int y) {
    lv_obj_t* label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(label, text);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, y);
    return label;
}

void CreateTrafficRunnerUI() {
    if (gamesPanel == nullptr || runnerSetupScreen != nullptr) {
        return;
    }

    const size_t bytes = static_cast<size_t>(kRunnerCanvasPx) * kRunnerCanvasPx *
                         sizeof(uint16_t);
    runnerCanvasBuf = static_cast<uint16_t*>(
        heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (runnerCanvasBuf == nullptr) {
        ESP_LOGE(TAG, "Traffic runner: failed to allocate the %u-byte canvas",
                 static_cast<unsigned>(bytes));
    } else {
        runnerCanvas = lv_canvas_create(gamesPanel);
        lv_obj_remove_style_all(runnerCanvas);
        lv_obj_set_size(runnerCanvas, kRunnerCanvasPx, kRunnerCanvasPx);
        lv_obj_center(runnerCanvas);
        lv_canvas_set_buffer(runnerCanvas, runnerCanvasBuf, kRunnerCanvasPx,
                             kRunnerCanvasPx, LV_COLOR_FORMAT_RGB565);
        lv_obj_add_flag(runnerCanvas, LV_OBJ_FLAG_HIDDEN);
    }

    runnerHudScore = lv_label_create(gamesPanel);
    lv_obj_set_style_text_font(runnerHudScore, &lv_font_montserrat_vn_22, 0);
    lv_obj_set_style_text_color(runnerHudScore, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_color(runnerHudScore, lv_color_hex(kRunnerPanelBg), 0);
    lv_obj_set_style_bg_opa(runnerHudScore, LV_OPA_80, 0);
    lv_obj_set_style_radius(runnerHudScore, 9, 0);
    lv_obj_set_style_pad_hor(runnerHudScore, 7, 0);
    lv_obj_set_style_pad_ver(runnerHudScore, 1, 0);
    lv_label_set_text(runnerHudScore, "0");
    lv_obj_align(runnerHudScore, LV_ALIGN_TOP_MID, 0, 7);

    runnerHudGold = lv_label_create(gamesPanel);
    lv_obj_set_style_text_font(runnerHudGold, &lv_font_montserrat_vn_20, 0);
    lv_obj_set_style_text_color(runnerHudGold, lv_color_hex(kRunnerGold), 0);
    lv_obj_set_style_bg_color(runnerHudGold, lv_color_hex(kRunnerPanelBg), 0);
    lv_obj_set_style_bg_opa(runnerHudGold, LV_OPA_80, 0);
    lv_obj_set_style_radius(runnerHudGold, 8, 0);
    lv_obj_set_style_pad_hor(runnerHudGold, 5, 0);
    lv_label_set_text(runnerHudGold, "VÀNG 0");
    lv_obj_align(runnerHudGold, LV_ALIGN_TOP_MID, 0, 34);

    runnerHudBoost = lv_label_create(gamesPanel);
    lv_obj_set_style_text_font(runnerHudBoost, &lv_font_montserrat_vn_20, 0);
    lv_obj_set_style_text_color(runnerHudBoost, lv_color_hex(kRunnerAccent), 0);
    lv_label_set_text(runnerHudBoost, "BOOST 3.0s");
    lv_obj_align(runnerHudBoost, LV_ALIGN_TOP_MID, 0, 60);

    runnerPhaseLabel = lv_label_create(gamesPanel);
    lv_obj_set_style_text_font(runnerPhaseLabel, &lv_font_montserrat_vn_22, 0);
    lv_obj_set_style_text_color(runnerPhaseLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_color(runnerPhaseLabel, lv_color_hex(kRunnerPanelBg), 0);
    lv_obj_set_style_bg_opa(runnerPhaseLabel, LV_OPA_90, 0);
    lv_obj_set_style_radius(runnerPhaseLabel, 12, 0);
    lv_obj_set_style_pad_all(runnerPhaseLabel, 9, 0);
    lv_label_set_text(runnerPhaseLabel, "GIỮ BUBU THẲNG");
    lv_obj_center(runnerPhaseLabel);

    runnerSetupScreen = lv_obj_create(gamesPanel);
    lv_obj_remove_style_all(runnerSetupScreen);
    lv_obj_set_size(runnerSetupScreen, 224, 224);
    lv_obj_center(runnerSetupScreen);
    lv_obj_set_style_bg_color(runnerSetupScreen, lv_color_hex(kRunnerPanelBg), 0);
    lv_obj_set_style_bg_opa(runnerSetupScreen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(runnerSetupScreen, LV_OBJ_FLAG_SCROLLABLE);
    CreateRunnerLabel(runnerSetupScreen, &lv_font_montserrat_vn_22,
                      kRunnerAccent, "ĐƯỜNG PHỐ", -72);
    runnerSetupBest = CreateRunnerLabel(runnerSetupScreen, &lv_font_montserrat_vn_20,
                                        kRunnerGold, "KỶ LỤC 0", -37);
    lv_obj_t* hint = CreateRunnerLabel(runnerSetupScreen, &lv_font_montserrat_vn_20,
                                       0xFFFFFF, "NGHIÊNG BUBU\nĐỂ ĐỔI LÀN", 4);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    runnerPlayBtn = CreateQuickTapPill(runnerSetupScreen, 104, 38, 0, 66,
                                       "CHƠI", &lv_font_montserrat_vn_22);
    StyleQuickTapPill(runnerPlayBtn, kRunnerAccent, kRunnerAccent, kRunnerPanelBg);

    runnerOverScreen = lv_obj_create(gamesPanel);
    lv_obj_remove_style_all(runnerOverScreen);
    lv_obj_set_size(runnerOverScreen, 224, 224);
    lv_obj_center(runnerOverScreen);
    lv_obj_set_style_bg_color(runnerOverScreen, lv_color_hex(kRunnerPanelBg), 0);
    lv_obj_set_style_bg_opa(runnerOverScreen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(runnerOverScreen, LV_OBJ_FLAG_SCROLLABLE);
    runnerOverTitle = CreateRunnerLabel(runnerOverScreen, &lv_font_montserrat_vn_22,
                                        0xFF6B5C, "VA CHẠM", -72);
    runnerOverScore = CreateRunnerLabel(runnerOverScreen, &lv_font_montserrat_vn_22,
                                        0xFFFFFF, "ĐIỂM 0", -31);
    runnerOverBest = CreateRunnerLabel(runnerOverScreen, &lv_font_montserrat_vn_20,
                                       kRunnerGold, "KỶ LỤC 0", 2);
    runnerAgainBtn = CreateQuickTapPill(runnerOverScreen, 124, 34, 0, 48,
                                        "CHƠI LẠI", &lv_font_montserrat_vn_20);
    StyleQuickTapPill(runnerAgainBtn, kRunnerAccent, kRunnerAccent, kRunnerPanelBg);
    runnerMenuBtn = CreateQuickTapPill(runnerOverScreen, 82, 28, 0, 84,
                                       "MENU", &lv_font_montserrat_vn_20);
    StyleQuickTapPill(runnerMenuBtn, 0x173B4F, kRunnerMuted, 0xFFFFFF);

    TrafficRunnerGame::Initialize();
    HideTrafficRunnerAll();
}

// ---------------------------------------------------------------------------
// MẮT XANH
//
// Built on demand like the other games (CreateGreenEyeUI via LoadGameUi) and
// driven by its own 33ms lv_timer. One game is: setup -> 3-2-1 -> rounds until
// the three hearts are gone -> HẾT LƯỢT! -> scoreboard with the CẢM XÚC it paid.
// ---------------------------------------------------------------------------

void LoadGreenEyeBest() {
    Settings settings("greeneye", false);
    const int best = static_cast<int>(settings.GetInt("best", 0));
    greenEyeBest = static_cast<uint16_t>(std::clamp(best, 0, 999));
}

void SaveGreenEyeBest(uint16_t best) {
    Settings settings("greeneye", true);
    settings.SetInt("best", best);
}

void PlayGreenEyeSound(const std::string_view& sound) {
    Application::GetInstance().Schedule([sound]() {
        Application::GetInstance().PlayOverlaySound(sound);
    });
}

void SetGreenEyeHidden(lv_obj_t* obj, bool hidden) {
    if (obj == nullptr) {
        return;
    }
    if (hidden) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

// IsPointInside() with a margin: the pills are small for a child's finger.
bool IsPointNear(lv_obj_t* obj, uint16_t x, uint16_t y, int slack) {
    if (obj == nullptr) {
        return false;
    }
    lv_area_t area;
    lv_obj_get_coords(obj, &area);
    return x >= area.x1 - slack && x <= area.x2 + slack &&
           y >= area.y1 - slack && y <= area.y2 + slack;
}

float GreenEyeEaseOut(float t) {
    const float u = 1.0f - t;
    return 1.0f - u * u * u;
}

int GreenEyeMoodBarWidth(int mood) {
    // CareSystem stats run 0..100.
    return std::clamp(mood, 0, 100) * kGreenEyeMoodBarW / 100;
}

// A bar narrower than its own round cap is drawn as nothing rather than a dot.
void SetGreenEyeBarWidth(lv_obj_t* bar, int width) {
    if (bar == nullptr) {
        return;
    }
    if (width < kGreenEyeMoodBarH) {
        lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_set_width(bar, width);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_HIDDEN);
}

// Forget what the eyes and HUD last showed, and hide the eyes, so the next
// frame redraws everything from the game state.
void ResetGreenEyeDrawn() {
    greenEyeDrawnScore = -1;
    greenEyeDrawnCountdown = -1;
    greenEyeDrawnBarW = -1;
    greenEyeDrawnHearts.fill(-1);
    for (int i = 0; i < GreenEyeGame::kMaxEyes; ++i) {
        SetGreenEyeHidden(greenEyeEyes[i], true);
        SetGreenEyeHidden(greenEyeGlints[i], true);
        greenEyeDrawn[i] = GreenEyeDrawn{};
    }
}

// One eye's look for this frame. Only what changed is touched, so a still
// frame -- most of every round -- costs nothing but the rim.
void DrawGreenEye(int index, bool hidden, int w, int h, int cx, int cy, int radius,
                  uint32_t color, uint8_t opa, uint8_t border, bool glint) {
    lv_obj_t* eye = greenEyeEyes[index];
    if (eye == nullptr) {
        return;
    }
    GreenEyeDrawn& drawn = greenEyeDrawn[index];
    if (hidden) {
        if (!drawn.hidden) {
            lv_obj_add_flag(eye, LV_OBJ_FLAG_HIDDEN);
            drawn.hidden = true;
        }
        return;
    }
    if (drawn.hidden) {
        lv_obj_remove_flag(eye, LV_OBJ_FLAG_HIDDEN);
        drawn.hidden = false;
    }
    if (w != drawn.w || h != drawn.h) {
        lv_obj_set_size(eye, w, h);
        // A closing eye is a flat bar, not a squashed rounded square.
        lv_obj_set_style_radius(eye, std::min(radius, h / 2), 0);
        if (greenEyeGlints[index] != nullptr) {
            const int glint_size = std::max(8, w / 7);
            lv_obj_set_size(greenEyeGlints[index], glint_size, glint_size);
            lv_obj_align(greenEyeGlints[index], LV_ALIGN_TOP_RIGHT, -w / 5, w / 6);
        }
        drawn.w = static_cast<int16_t>(w);
        drawn.h = static_cast<int16_t>(h);
        drawn.x = INT16_MIN;   // re-centre at the new size
    }
    if (cx != drawn.x || cy != drawn.y) {
        lv_obj_align(eye, LV_ALIGN_CENTER, cx - 120, cy - 120);
        drawn.x = static_cast<int16_t>(cx);
        drawn.y = static_cast<int16_t>(cy);
    }
    if (color != drawn.color) {
        lv_obj_set_style_bg_color(eye, lv_color_hex(color), 0);
        drawn.color = color;
    }
    if (opa != drawn.opa) {
        lv_obj_set_style_bg_opa(eye, opa, 0);
        drawn.opa = opa;
    }
    if (border != drawn.border) {
        lv_obj_set_style_border_width(eye, border, 0);
        drawn.border = border;
    }
    if (glint != drawn.glint) {
        SetGreenEyeHidden(greenEyeGlints[index], !glint);
        drawn.glint = glint;
    }
}

void UpdateGreenEyePlayfield() {
    using GreenEyeGame::Outcome;
    using GreenEyeGame::Phase;
    const GreenEyeGame::State& s = GreenEyeGame::GetState();
    const uint32_t elapsed = GreenEyeGame::PhaseElapsedMs(lv_tick_get());
    const float t = s.phase_len_ms == 0
        ? 1.0f
        : std::min(1.0f, static_cast<float>(elapsed) / static_cast<float>(s.phase_len_ms));
    const bool missed = s.outcome == Outcome::kWrong || s.outcome == Outcome::kTimeout;
    constexpr float kTwoPi = 2.0f * static_cast<float>(M_PI);

    // ---- Rim: refills while the eyes open, drains while they are looked at,
    // and holds where the round was decided.
    int32_t rim = 1000;
    switch (s.phase) {
        case Phase::kOpening:
            rim = static_cast<int32_t>(1000.0f * t);
            break;
        case Phase::kLooking:
            rim = elapsed >= s.window_ms
                ? 0
                : 1000 - static_cast<int32_t>((static_cast<uint64_t>(elapsed) * 1000ULL) / s.window_ms);
            break;
        case Phase::kFeedback:
        case Phase::kClosing:
            rim = s.decided_permille;
            break;
        case Phase::kOver:
            rim = 0;
            break;
        default:
            break;
    }
    if (greenEyeRim != nullptr) {
        lv_arc_set_value(greenEyeRim, rim);   // a no-op when unchanged
    }

    if (greenEyeScoreLabel != nullptr && greenEyeDrawnScore != s.score) {
        lv_label_set_text_fmt(greenEyeScoreLabel, "%u",
                              static_cast<unsigned>(std::min<uint16_t>(s.score, 999)));
        greenEyeDrawnScore = s.score;
    }

    // ---- Hearts: the one just lost blinks through the miss, then goes dark.
    for (int i = 0; i < GreenEyeGame::kLives; ++i) {
        bool lit = i < s.lives;
        if (s.phase == Phase::kFeedback && missed && i == s.lives) {
            lit = (elapsed / 120) % 2 == 0;
        }
        const int8_t want = lit ? 1 : 0;
        if (greenEyeHearts[i] != nullptr && greenEyeDrawnHearts[i] != want) {
            lv_obj_set_style_text_color(greenEyeHearts[i],
                                        lv_color_hex(lit ? kGreenEyeChrome : kGreenEyeHeartLost), 0);
            greenEyeDrawnHearts[i] = want;
        }
    }

    // ---- 3-2-1 (the banner reuses this label and owns it outside kPlaying).
    if (greenEyeCenterLabel != nullptr && greenEyeScreen == GreenEyeScreen::kPlaying) {
        const int countdown = s.phase == Phase::kCountdown ? s.countdown : 0;
        if (countdown != greenEyeDrawnCountdown) {
            if (countdown > 0) {
                lv_label_set_text_fmt(greenEyeCenterLabel, "%d", countdown);
            }
            SetGreenEyeHidden(greenEyeCenterLabel, countdown <= 0);
            greenEyeDrawnCountdown = countdown;
        }
    }

    // ---- The eyes.
    const bool eyes_up = s.phase == Phase::kOpening || s.phase == Phase::kLooking ||
                         s.phase == Phase::kFeedback || s.phase == Phase::kClosing;
    for (int i = 0; i < GreenEyeGame::kMaxEyes; ++i) {
        if (!eyes_up || i >= s.eye_count) {
            DrawGreenEye(i, true, 0, 0, 0, 0, 0, 0, 0, 0, false);
            continue;
        }
        const GreenEyeGame::Eye& eye = s.eyes[i];
        const bool is_green = (i == s.green_index);
        const int size = eye.size;
        int w = size;
        int h = size;
        int cx = eye.x;
        uint8_t opa = LV_OPA_COVER;
        uint8_t border = 0;
        bool hidden = false;
        switch (s.phase) {
            case Phase::kOpening:
                // Eyes open like Bubu's own: the lids part, the width never changes.
                h = std::max(4, static_cast<int>(size * GreenEyeEaseOut(t) + 0.5f));
                break;
            case Phase::kFeedback:
                if (s.outcome == Outcome::kHit) {
                    if (is_green) {
                        // Pops under the finger and fades out.
                        w = h = static_cast<int>(size * (1.0f + 0.3f * t) + 0.5f);
                        opa = static_cast<uint8_t>(255.0f * (1.0f - t));
                    } else {
                        h = std::max(4, static_cast<int>(size * (1.0f - t) + 0.5f));
                    }
                } else if (is_green) {
                    // A miss shows the answer: ringed and breathing, so the
                    // child sees which one the green eye was.
                    const float pulse = 1.0f + 0.08f * sinf(t * 3.0f * kTwoPi);
                    w = h = static_cast<int>(size * pulse + 0.5f);
                    border = 3;
                } else if (i == s.tapped_index) {
                    // The wrong one shakes its head.
                    cx += static_cast<int>(7.0f * sinf(t * 3.0f * kTwoPi) * (1.0f - t));
                    opa = 90;
                } else {
                    opa = 60;
                }
                break;
            case Phase::kClosing:
                if (s.outcome == Outcome::kHit && is_green) {
                    hidden = true;   // already popped
                    break;
                }
                h = std::max(4, static_cast<int>(size * (1.0f - t) + 0.5f));
                if (missed && !is_green) {
                    opa = (i == s.tapped_index) ? 90 : 60;
                }
                break;
            default:
                break;
        }
        const bool glint = h * 100 >= size * 85 && w <= size + 2 && opa >= 200;
        DrawGreenEye(i, hidden, w, h, cx, eye.y, eye.radius,
                     GreenEyeGame::HueRgb(eye.hue), opa, border, glint);
    }

    // ---- "+1" rising out of the eye that was just found.
    if (greenEyePlus != nullptr) {
        if (s.phase == Phase::kFeedback && s.outcome == Outcome::kHit && s.green_index >= 0) {
            const GreenEyeGame::Eye& green = s.eyes[s.green_index];
            lv_obj_align(greenEyePlus, LV_ALIGN_CENTER, green.x - 120,
                         green.y - 120 - static_cast<int>(22.0f * t));
            lv_obj_set_style_text_opa(greenEyePlus, static_cast<lv_opa_t>(255.0f * (1.0f - t * t)), 0);
            SetGreenEyeHidden(greenEyePlus, false);
            lv_obj_move_foreground(greenEyePlus);
        } else {
            SetGreenEyeHidden(greenEyePlus, true);
        }
    }
}

// One sound per decided round, whichever path decided it (the tick or a tap).
void AnnounceGreenEyeDecision() {
    const GreenEyeGame::State& s = GreenEyeGame::GetState();
    if (s.decisions == greenEyeSeenDecisions) {
        return;
    }
    greenEyeSeenDecisions = s.decisions;
    switch (s.outcome) {
        case GreenEyeGame::Outcome::kHit:
            // Every fifth green in a row Bubu cheers instead of the pop -- the
            // game is making Bubu happier, and this is where the child hears it.
            if (s.streak > 0 && s.streak % 5 == 0) {
                PlayGreenEyeSound(Vox::Pick(Lang::Sounds::OGG_VOX_HAPPY_1_A,
                                            Lang::Sounds::OGG_VOX_HAPPY_1_B));
            } else {
                PlayGreenEyeSound(Lang::Sounds::OGG_POPUP);
            }
            break;
        case GreenEyeGame::Outcome::kWrong:
            PlayGreenEyeSound(Vox::Pick(Lang::Sounds::OGG_VOX_SURPRISE_1_A,
                                        Lang::Sounds::OGG_VOX_SURPRISE_1_B));
            break;
        case GreenEyeGame::Outcome::kTimeout:
            PlayGreenEyeSound(Vox::Pick(Lang::Sounds::OGG_VOX_SAD_1_A,
                                        Lang::Sounds::OGG_VOX_SAD_1_B));
            break;
        case GreenEyeGame::Outcome::kNone:
            break;
    }
}

// Pays CẢM XÚC for the greens found and settles the record -- exactly once per
// game, whichever way it ends: the scoreboard, a long press out of a round, or
// the menu closing underneath it. Safe to call again; later calls do nothing.
void PayGreenEyeReward() {
    if (greenEyeRewardPaid) {
        return;
    }
    greenEyeRewardPaid = true;
    const uint16_t score = GreenEyeGame::GetState().score;
    greenEyeNewRecord = score > greenEyeBest;
    if (greenEyeNewRecord) {
        greenEyeBest = score;
    }
    const int reward = GreenEyeGame::MoodReward(score);
    greenEyeMoodBefore = CareSystem::GetMood();
    greenEyeMoodAfter = std::min(100, greenEyeMoodBefore + reward);
    if (greenEyeNewRecord) {
        // NVS on the main task, not in the frame drawing the scoreboard.
        const uint16_t best = greenEyeBest;
        Application::GetInstance().Schedule([best]() { SaveGreenEyeBest(best); });
    }
    if (reward > 0) {
        RewardGameMood("green_eye", reward);
    }
}

void UpdateGreenEyeSetupUI() {
    if (greenEyeSetupRecord == nullptr) {
        return;
    }
    if (greenEyeBest > 0) {
        lv_label_set_text_fmt(greenEyeSetupRecord, "KỶ LỤC %u",
                              static_cast<unsigned>(std::min<uint16_t>(greenEyeBest, 999)));
    }
    SetGreenEyeHidden(greenEyeSetupRecord, greenEyeBest == 0);
}

void UpdateGreenEyeScoreUI() {
    const uint16_t score = GreenEyeGame::GetState().score;
    if (greenEyeResultValue != nullptr) {
        lv_label_set_text_fmt(greenEyeResultValue, "%u",
                              static_cast<unsigned>(std::min<uint16_t>(score, 999)));
    }
    if (greenEyeResultCaption != nullptr) {
        lv_label_set_text(greenEyeResultCaption, greenEyeNewRecord ? "KỶ LỤC MỚI!" : "mắt xanh");
        lv_obj_set_style_text_color(
            greenEyeResultCaption,
            lv_color_hex(greenEyeNewRecord ? GreenEyeGame::HueRgb(GreenEyeGame::Hue::kGreen)
                                           : kGreenEyeMuted),
            0);
    }
    if (greenEyeRewardLabel != nullptr) {
        // What CẢM XÚC actually gained, which is less than the reward when the
        // stat is already near the top -- the bar below must agree with it.
        const int gained = greenEyeMoodAfter - greenEyeMoodBefore;
        if (gained > 0) {
            lv_label_set_text_fmt(greenEyeRewardLabel, "+%d CẢM XÚC", gained);
        } else if (GreenEyeGame::MoodReward(score) > 0) {
            lv_label_set_text(greenEyeRewardLabel, "CẢM XÚC ĐẦY");
        } else {
            lv_label_set_text(greenEyeRewardLabel, "+0 CẢM XÚC");
        }
    }
    // The dim part is where CẢM XÚC was; the bright part grows over it in
    // GreenEyeTimerCb to where this game took it.
    const int before = GreenEyeMoodBarWidth(greenEyeMoodBefore);
    SetGreenEyeBarWidth(greenEyeMoodBase, before);
    SetGreenEyeBarWidth(greenEyeMoodGain, before);
    greenEyeDrawnBarW = before;
}

void ShowGreenEyeScreen(GreenEyeScreen screen) {
    greenEyeScreen = screen;
    greenEyeScreenStartMs = lv_tick_get();
    const bool playing = (screen == GreenEyeScreen::kPlaying);
    const bool banner = (screen == GreenEyeScreen::kBanner);

    ResetGreenEyeDrawn();
    SetGreenEyeHidden(greenEyeSetupScreen, screen != GreenEyeScreen::kSetup);
    SetGreenEyeHidden(greenEyeScoreScreen, screen != GreenEyeScreen::kScore);
    // Rim, score and hearts stay up under HẾT LƯỢT! -- drained, final, dark.
    SetGreenEyeHidden(greenEyeRim, !(playing || banner));
    SetGreenEyeHidden(greenEyeScoreLabel, !(playing || banner));
    for (lv_obj_t* heart : greenEyeHearts) {
        SetGreenEyeHidden(heart, !(playing || banner));
    }
    SetGreenEyeHidden(greenEyePlus, true);
    SetGreenEyeHidden(greenEyeCenterLabel, !banner);

    switch (screen) {
        case GreenEyeScreen::kSetup:
            UpdateGreenEyeSetupUI();
            break;
        case GreenEyeScreen::kPlaying:
            UpdateGreenEyePlayfield();
            break;
        case GreenEyeScreen::kBanner:
            if (greenEyeCenterLabel != nullptr) {
                lv_label_set_text(greenEyeCenterLabel, "HẾT LƯỢT!");
                lv_obj_move_foreground(greenEyeCenterLabel);
            }
            UpdateGreenEyePlayfield();
            break;
        case GreenEyeScreen::kScore:
            UpdateGreenEyeScoreUI();
            break;
    }
    // Same reason as Quick Tap: the panel is opaque and screen changes are
    // rare, so one full repaint clears anything the last screen left behind.
    if (gamesPanel != nullptr) {
        lv_obj_invalidate(gamesPanel);
    }
    UpdateGamesUI();
}

// Caller holds the display lock.
void BeginGreenEyeGame() {
    greenEyeNewRecord = false;
    greenEyeRewardPaid = false;
    greenEyeSeenDecisions = 0;
    GreenEyeGame::Start(lv_tick_get(), esp_random());
    ShowGreenEyeScreen(GreenEyeScreen::kPlaying);
}

// Game over: pay CẢM XÚC, bank the record, show the scoreboard. Runs on the
// LVGL task with the display lock held.
void FinishGreenEyeGame() {
    PayGreenEyeReward();
    const uint16_t score = GreenEyeGame::GetState().score;
    if (greenEyeNewRecord) {
        PlayGreenEyeSound(Lang::Sounds::OGG_SUCCESS);
    } else if (score > 0) {
        PlayGreenEyeSound(Vox::Pick(Lang::Sounds::OGG_VOX_HAPPY_2_A,
                                    Lang::Sounds::OGG_VOX_HAPPY_2_B));
    }
    // A real game leaves Bubu visibly happier when the menu closes. On the
    // main task: SetEmotion takes the display lock this task already holds.
    if (GreenEyeGame::MoodReward(score) >= 5) {
        Application::GetInstance().Schedule([]() {
            if (auto* eye_display = GetEyeDisplay(); eye_display != nullptr) {
                eye_display->SetEmotion("happy");
            }
        });
    }
    ShowGreenEyeScreen(GreenEyeScreen::kScore);
}

// Runs on the LVGL task inside lv_timer_handler, which already holds the
// display lock -- so nothing here may take DisplayLockGuard or call
// HandleGameFinished().
void GreenEyeTimerCb(lv_timer_t* timer) {
    (void)timer;
    if (currentState != MENU_GAME_ACTIVE || activeGame != ACTIVE_GAME_GREEN_EYE) {
        return;
    }
    const uint32_t since = lv_tick_elaps(greenEyeScreenStartMs);
    switch (greenEyeScreen) {
        case GreenEyeScreen::kSetup:
            // The demo's green eye breathes its ring: "this one".
            if (greenEyeDemoGreen != nullptr) {
                const float phase = static_cast<float>(since % kGreenEyeDemoPulseMs) /
                                    static_cast<float>(kGreenEyeDemoPulseMs);
                const float k = 0.5f + 0.5f * sinf(phase * 2.0f * static_cast<float>(M_PI));
                lv_obj_set_style_border_opa(greenEyeDemoGreen, static_cast<lv_opa_t>(70.0f + 185.0f * k), 0);
            }
            return;
        case GreenEyeScreen::kPlaying:
            // A game outlasts the menu's 30 s inactivity close-out, and a child
            // watching for green is playing even between taps.
            MarkMenuActivity();
            GreenEyeGame::Update(lv_tick_get());
            AnnounceGreenEyeDecision();
            UpdateGreenEyePlayfield();
            if (GreenEyeGame::GetState().phase == GreenEyeGame::Phase::kOver) {
                ShowGreenEyeScreen(GreenEyeScreen::kBanner);
            }
            return;
        case GreenEyeScreen::kBanner:
            MarkMenuActivity();
            if (since >= kGreenEyeBannerMs) {
                FinishGreenEyeGame();
            }
            return;
        case GreenEyeScreen::kScore: {
            if (since < kGreenEyeBarDelayMs) {
                return;
            }
            const float t = std::min(1.0f, static_cast<float>(since - kGreenEyeBarDelayMs) /
                                               static_cast<float>(kGreenEyeBarFillMs));
            const int from = GreenEyeMoodBarWidth(greenEyeMoodBefore);
            const int to = GreenEyeMoodBarWidth(greenEyeMoodAfter);
            const int width = from + static_cast<int>(static_cast<float>(to - from) * GreenEyeEaseOut(t) + 0.5f);
            if (width != greenEyeDrawnBarW) {
                SetGreenEyeBarWidth(greenEyeMoodGain, width);
                greenEyeDrawnBarW = width;
            }
            return;
        }
    }
}

void StartGreenEyeTimer() {
    if (greenEyeTimer == nullptr) {
        greenEyeTimer = lv_timer_create(GreenEyeTimerCb, kGreenEyeTickMs, nullptr);
    }
}

void StopGreenEyeTimer() {
    if (greenEyeTimer != nullptr) {
        lv_timer_delete(greenEyeTimer);
        greenEyeTimer = nullptr;
    }
}

void HideGreenEyeAll() {
    for (lv_obj_t* obj : {greenEyeRim, greenEyeScoreLabel, greenEyePlus, greenEyeCenterLabel,
                          greenEyeSetupScreen, greenEyeScoreScreen}) {
        SetGreenEyeHidden(obj, true);
    }
    for (lv_obj_t* heart : greenEyeHearts) {
        SetGreenEyeHidden(heart, true);
    }
    ResetGreenEyeDrawn();
}

// Caller holds the display lock. Built by LoadGameUi(), deleted by UnloadGameUi().
void CreateGreenEyeUI() {
    if (gamesPanel == nullptr || greenEyeRim != nullptr) {
        return;
    }
    using GreenEyeGame::Hue;
    const uint32_t green = GreenEyeGame::HueRgb(Hue::kGreen);

    // ---- Rim: the round's clock -------------------------------------------
    greenEyeRim = lv_arc_create(gamesPanel);
    lv_obj_set_size(greenEyeRim, kGreenEyeRimSize, kGreenEyeRimSize);
    lv_obj_center(greenEyeRim);
    lv_arc_set_rotation(greenEyeRim, 270);        // drains from 12 o'clock
    lv_arc_set_bg_angles(greenEyeRim, 0, 360);
    lv_arc_set_mode(greenEyeRim, LV_ARC_MODE_NORMAL);
    lv_arc_set_range(greenEyeRim, 0, 1000);
    lv_arc_set_value(greenEyeRim, 1000);
    lv_obj_remove_flag(greenEyeRim, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_style(greenEyeRim, nullptr, LV_PART_KNOB);
    lv_obj_set_style_arc_width(greenEyeRim, kGreenEyeRimWidth, LV_PART_MAIN);
    lv_obj_set_style_arc_width(greenEyeRim, kGreenEyeRimWidth, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(greenEyeRim, lv_color_hex(kGreenEyeRimTrack), LV_PART_MAIN);
    lv_obj_set_style_arc_color(greenEyeRim, lv_color_hex(kGreenEyeChrome), LV_PART_INDICATOR);
    lv_obj_add_flag(greenEyeRim, LV_OBJ_FLAG_HIDDEN);

    // ---- HUD: score above, hearts below -------------------------------------
    greenEyeScoreLabel = lv_label_create(gamesPanel);
    lv_obj_set_style_text_font(greenEyeScoreLabel, &lv_font_montserrat_vn_28, 0);
    lv_obj_set_style_text_color(greenEyeScoreLabel, lv_color_hex(kGreenEyeChrome), 0);
    lv_label_set_text(greenEyeScoreLabel, "0");
    lv_obj_align(greenEyeScoreLabel, LV_ALIGN_CENTER, 0, kGreenEyeScoreY);
    lv_obj_add_flag(greenEyeScoreLabel, LV_OBJ_FLAG_HIDDEN);

    for (int i = 0; i < GreenEyeGame::kLives; ++i) {
        lv_obj_t* heart = lv_label_create(gamesPanel);
        lv_obj_set_style_text_font(heart, &font_awesome_20_4, 0);
        lv_obj_set_style_text_color(heart, lv_color_hex(kGreenEyeChrome), 0);
        lv_label_set_text(heart, FONT_AWESOME_HEART);
        lv_obj_align(heart, LV_ALIGN_CENTER, (i - 1) * kGreenEyeHeartPitch, kGreenEyeHeartsY);
        lv_obj_add_flag(heart, LV_OBJ_FLAG_HIDDEN);
        greenEyeHearts[i] = heart;
    }

    // ---- The eyes: rounded squares like Bubu's own, with a glint ------------
    for (int i = 0; i < GreenEyeGame::kMaxEyes; ++i) {
        lv_obj_t* eye = lv_obj_create(gamesPanel);
        lv_obj_remove_style_all(eye);
        lv_obj_set_size(eye, 56, 56);
        lv_obj_set_style_bg_opa(eye, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(eye, lv_color_hex(COLOR_TEXT), 0);
        lv_obj_set_style_border_opa(eye, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(eye, 0, 0);
        lv_obj_remove_flag(eye, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(eye, LV_OBJ_FLAG_HIDDEN);
        greenEyeEyes[i] = eye;

        lv_obj_t* glint = lv_obj_create(eye);
        lv_obj_remove_style_all(glint);
        lv_obj_set_size(glint, 8, 8);
        lv_obj_set_style_radius(glint, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(glint, lv_color_hex(COLOR_TEXT), 0);
        lv_obj_set_style_bg_opa(glint, LV_OPA_80, 0);
        lv_obj_remove_flag(glint, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(glint, LV_OBJ_FLAG_HIDDEN);
        greenEyeGlints[i] = glint;
    }

    greenEyePlus = lv_label_create(gamesPanel);
    lv_obj_set_style_text_font(greenEyePlus, &lv_font_montserrat_vn_22, 0);
    lv_obj_set_style_text_color(greenEyePlus, lv_color_hex(COLOR_TEXT), 0);
    lv_label_set_text(greenEyePlus, "+1");
    lv_obj_add_flag(greenEyePlus, LV_OBJ_FLAG_HIDDEN);

    greenEyeCenterLabel = lv_label_create(gamesPanel);
    lv_obj_set_style_text_font(greenEyeCenterLabel, &lv_font_montserrat_vn_28, 0);
    lv_obj_set_style_text_color(greenEyeCenterLabel, lv_color_hex(kGreenEyeChrome), 0);
    lv_label_set_text(greenEyeCenterLabel, "3");
    lv_obj_align(greenEyeCenterLabel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(greenEyeCenterLabel, LV_OBJ_FLAG_HIDDEN);

    // ---- Setup: the whole rule in one picture, then CHƠI --------------------
    // Label centres are offsets from the panel centre, fitted with
    // tools/lvwidth.py: "Tìm mắt XANH LÁ" is 182px at vn_20 and clears the
    // glass with room at +34; "Chạm mắt XANH LÁ" (204px) did not fit there.
    greenEyeSetupScreen = lv_obj_create(gamesPanel);
    lv_obj_remove_style_all(greenEyeSetupScreen);
    lv_obj_set_size(greenEyeSetupScreen, 224, 224);
    lv_obj_center(greenEyeSetupScreen);
    lv_obj_remove_flag(greenEyeSetupScreen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(greenEyeSetupScreen);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_vn_22, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(green), 0);
    lv_label_set_text(title, "MẮT XANH");
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -66);

    greenEyeSetupRecord = lv_label_create(greenEyeSetupScreen);
    lv_obj_set_style_text_font(greenEyeSetupRecord, &lv_font_montserrat_vn_20, 0);
    lv_obj_set_style_text_color(greenEyeSetupRecord, lv_color_hex(kGreenEyeMuted), 0);
    lv_label_set_text(greenEyeSetupRecord, "KỶ LỤC 0");
    lv_obj_align(greenEyeSetupRecord, LV_ALIGN_CENTER, 0, -40);

    // Red, GREEN, blue: blue beside green on purpose -- it is "xanh" too.
    const Hue demo[3] = {Hue::kRed, Hue::kGreen, Hue::kBlue};
    for (int i = 0; i < 3; ++i) {
        lv_obj_t* mini = lv_obj_create(greenEyeSetupScreen);
        lv_obj_remove_style_all(mini);
        lv_obj_set_size(mini, 36, 36);
        lv_obj_set_style_radius(mini, 11, 0);
        lv_obj_set_style_bg_color(mini, lv_color_hex(GreenEyeGame::HueRgb(demo[i])), 0);
        lv_obj_set_style_bg_opa(mini, LV_OPA_COVER, 0);
        lv_obj_align(mini, LV_ALIGN_CENTER, (i - 1) * 50, 0);
        lv_obj_remove_flag(mini, LV_OBJ_FLAG_SCROLLABLE);
        if (demo[i] == Hue::kGreen) {
            lv_obj_set_style_border_width(mini, 3, 0);
            lv_obj_set_style_border_color(mini, lv_color_hex(COLOR_TEXT), 0);
            lv_obj_set_style_border_opa(mini, LV_OPA_COVER, 0);
            greenEyeDemoGreen = mini;
        }
    }

    lv_obj_t* hint = lv_label_create(greenEyeSetupScreen);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_vn_20, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(COLOR_TEXT), 0);
    lv_label_set_text(hint, "Tìm mắt XANH LÁ");
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 34);

    greenEyePlayBtn = CreateQuickTapPill(greenEyeSetupScreen, 120, 38, 0, 74, "CHƠI",
                                         &lv_font_montserrat_vn_22);
    StyleQuickTapPill(greenEyePlayBtn, green, green, 0x06210D);
    lv_obj_add_flag(greenEyeSetupScreen, LV_OBJ_FLAG_HIDDEN);

    // ---- Scoreboard ---------------------------------------------------------
    greenEyeScoreScreen = lv_obj_create(gamesPanel);
    lv_obj_remove_style_all(greenEyeScoreScreen);
    lv_obj_set_size(greenEyeScoreScreen, 224, 224);
    lv_obj_center(greenEyeScoreScreen);
    lv_obj_remove_flag(greenEyeScoreScreen, LV_OBJ_FLAG_SCROLLABLE);

    greenEyeResultValue = lv_label_create(greenEyeScoreScreen);
    lv_obj_set_style_text_font(greenEyeResultValue, &lv_font_montserrat_vn_28, 0);
    lv_obj_set_style_text_color(greenEyeResultValue, lv_color_hex(COLOR_TEXT), 0);
    lv_label_set_text(greenEyeResultValue, "0");
    lv_obj_align(greenEyeResultValue, LV_ALIGN_CENTER, 0, -68);

    greenEyeResultCaption = lv_label_create(greenEyeScoreScreen);
    lv_obj_set_style_text_font(greenEyeResultCaption, &lv_font_montserrat_vn_20, 0);
    lv_obj_set_style_text_color(greenEyeResultCaption, lv_color_hex(kGreenEyeMuted), 0);
    lv_label_set_text(greenEyeResultCaption, "mắt xanh");
    lv_obj_align(greenEyeResultCaption, LV_ALIGN_CENTER, 0, -38);

    // What the game paid Bubu, in CẢM XÚC's colour and with its heart: the
    // widest string, "CẢM XÚC ĐẦY", makes the row 171px at -10 -- well clear.
    lv_obj_t* reward_row = lv_obj_create(greenEyeScoreScreen);
    lv_obj_remove_style_all(reward_row);
    lv_obj_set_size(reward_row, LV_SIZE_CONTENT, 27);
    lv_obj_align(reward_row, LV_ALIGN_CENTER, 0, -10);
    lv_obj_set_flex_flow(reward_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(reward_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(reward_row, 6, 0);
    lv_obj_remove_flag(reward_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* reward_heart = lv_label_create(reward_row);
    lv_obj_set_style_text_font(reward_heart, &font_awesome_20_4, 0);
    lv_obj_set_style_text_color(reward_heart, lv_color_hex(kGreenEyeMood), 0);
    lv_label_set_text(reward_heart, FONT_AWESOME_HEART);
    greenEyeRewardLabel = lv_label_create(reward_row);
    lv_obj_set_style_text_font(greenEyeRewardLabel, &lv_font_montserrat_vn_20, 0);
    lv_obj_set_style_text_color(greenEyeRewardLabel, lv_color_hex(kGreenEyeMood), 0);
    lv_label_set_text(greenEyeRewardLabel, "+0 CẢM XÚC");

    // CẢM XÚC as a bar: the bright part is drawn first and runs to the new
    // value; the dim part covers it up to where the stat was before.
    lv_obj_t* track = lv_obj_create(greenEyeScoreScreen);
    lv_obj_remove_style_all(track);
    lv_obj_set_size(track, kGreenEyeMoodBarW, kGreenEyeMoodBarH);
    lv_obj_set_style_radius(track, kGreenEyeMoodBarH / 2, 0);
    lv_obj_set_style_bg_color(track, lv_color_hex(kGreenEyeRimTrack), 0);
    lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
    lv_obj_align(track, LV_ALIGN_CENTER, 0, 16);
    lv_obj_remove_flag(track, LV_OBJ_FLAG_SCROLLABLE);
    for (lv_obj_t** bar : {&greenEyeMoodGain, &greenEyeMoodBase}) {
        *bar = lv_obj_create(track);
        lv_obj_remove_style_all(*bar);
        lv_obj_set_size(*bar, kGreenEyeMoodBarH, kGreenEyeMoodBarH);
        lv_obj_set_style_radius(*bar, kGreenEyeMoodBarH / 2, 0);
        lv_obj_set_style_bg_opa(*bar, LV_OPA_COVER, 0);
        lv_obj_align(*bar, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_remove_flag(*bar, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(*bar, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_style_bg_color(greenEyeMoodGain, lv_color_hex(kGreenEyeMood), 0);
    lv_obj_set_style_bg_color(greenEyeMoodBase, lv_color_hex(kGreenEyeMoodDim), 0);

    greenEyeAgainBtn = CreateQuickTapPill(greenEyeScoreScreen, 132, 34, 0, 50, "CHƠI LẠI",
                                          &lv_font_montserrat_vn_20);
    StyleQuickTapPill(greenEyeAgainBtn, green, green, 0x06210D);
    greenEyeMenuBtn = CreateQuickTapPill(greenEyeScoreScreen, 92, 28, 0, 84, "MENU",
                                         &lv_font_montserrat_vn_20);
    StyleQuickTapPill(greenEyeMenuBtn, kGreenEyePillDim, 0x33363F, COLOR_TEXT);
    lv_obj_add_flag(greenEyeScoreScreen, LV_OBJ_FLAG_HIDDEN);

    UpdateGreenEyeSetupUI();
}

// ---------------------------------------------------------------------------
// HỌC TẬP
// ---------------------------------------------------------------------------

void PlayPomodoroSound(const std::string_view& sound) {
    Application::GetInstance().Schedule([sound]() {
        Application::GetInstance().PlayOverlaySound(sound);
    });
}

void UpdatePomodoroSetupUI() {
    const PomodoroTimer::Stats stats = PomodoroTimer::GetStats();

    for (int i = 0; i < kPomodoroPresetCount; ++i) {
        if (pomodoroDiscs[i] == nullptr) {
            continue;
        }
        const bool selected = (i == static_cast<int>(pomodoroPreset));
        lv_obj_set_style_bg_color(pomodoroDiscs[i],
                                  lv_color_hex(selected ? kPomodoroFocusColor : kQuickTapPanelBg), 0);
        lv_obj_set_style_bg_opa(pomodoroDiscs[i], selected ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(pomodoroDiscs[i],
                                      lv_color_hex(selected ? kPomodoroFocusColor : kPomodoroTrack), 0);
        lv_obj_t* label = lv_obj_get_child(pomodoroDiscs[i], 0);
        if (label != nullptr) {
            lv_obj_set_style_text_color(label, lv_color_hex(selected ? 0x180703 : COLOR_TEXT), 0);
        }
    }

    if (pomodoroTodayLabel != nullptr) {
        lv_label_set_text_fmt(pomodoroTodayLabel, "HÔM NAY %u",
                              static_cast<unsigned>(std::min<uint16_t>(stats.today, 99)));
    }
    if (pomodoroBreakLabel != nullptr) {
        lv_label_set_text_fmt(pomodoroBreakLabel, "NGHỈ %u'",
                              static_cast<unsigned>(
                                  PomodoroTimer::GetProfile(pomodoroPreset).short_break_min));
    }
}

void UpdatePomodoroRunUI() {
    const PomodoroTimer::Phase phase = PomodoroTimer::GetPhase();
    const bool focus = (phase == PomodoroTimer::Phase::kFocus);
    const uint32_t color = focus ? kPomodoroFocusColor : kPomodoroRestColor;

    if (pomodoroArc != nullptr) {
        const uint32_t total = PomodoroTimer::PhaseTotalMs();
        const uint32_t remaining = PomodoroTimer::RemainingMs();
        lv_arc_set_value(pomodoroArc, total == 0 ? 0 :
                         static_cast<int32_t>((static_cast<uint64_t>(remaining) * 1000ULL) / total));
        lv_obj_set_style_arc_color(pomodoroArc, lv_color_hex(color), LV_PART_INDICATOR);
    }

    if (pomodoroPhaseLabel != nullptr) {
        lv_label_set_text(pomodoroPhaseLabel, PomodoroTimer::PhaseName(phase));
        lv_obj_set_style_text_color(pomodoroPhaseLabel, lv_color_hex(color), 0);
    }

    if (pomodoroClockLabel != nullptr) {
        // Ceil, so a fresh 25' block reads 25:00 rather than 24:59.
        const uint32_t seconds = (PomodoroTimer::RemainingMs() + 999) / 1000;
        lv_label_set_text_fmt(pomodoroClockLabel, "%02u:%02u",
                              static_cast<unsigned>(seconds / 60),
                              static_cast<unsigned>(seconds % 60));
    }

    if (pomodoroLenLabel != nullptr) {
        const PomodoroTimer::Profile& profile = PomodoroTimer::GetProfile(PomodoroTimer::GetPreset());
        unsigned minutes = profile.focus_min;
        if (phase == PomodoroTimer::Phase::kShortBreak) minutes = profile.short_break_min;
        if (phase == PomodoroTimer::Phase::kLongBreak)  minutes = profile.long_break_min;
        lv_label_set_text_fmt(pomodoroLenLabel, "%u'", minutes);
    }

    // Progress toward the long break. A run that has just crossed a multiple of
    // four shows all four lit -- that is the state the long break belongs to.
    const uint8_t before_long = std::max<uint8_t>(PomodoroTimer::FocusBeforeLong(), 1);
    const uint8_t done = PomodoroTimer::FocusDoneInRun();
    uint8_t lit = done % before_long;
    if (done > 0 && lit == 0) {
        lit = std::min<uint8_t>(before_long, kPomodoroMaxDots);
    }
    for (int i = 0; i < kPomodoroMaxDots; ++i) {
        if (pomodoroDots[i] == nullptr) {
            continue;
        }
        const bool on = i < lit;
        lv_obj_set_style_bg_color(pomodoroDots[i],
                                  lv_color_hex(on ? color : kPomodoroTrack), 0);
    }
}

void ShowPomodoroScreen(PomodoroScreen screen) {
    pomodoroScreen = screen;

    auto set_hidden = [](lv_obj_t* obj, bool hidden) {
        if (obj == nullptr) return;
        if (hidden) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
        else        lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    };

    set_hidden(pomodoroSetupScreen, screen != PomodoroScreen::kSetup);
    set_hidden(pomodoroRunScreen, screen != PomodoroScreen::kRunning);
    set_hidden(pomodoroBannerScreen, screen != PomodoroScreen::kBanner);
    // The ring stays up under the banner unless the block was voided, where a
    // drained tomato ring would read as "finished".
    set_hidden(pomodoroArc, screen == PomodoroScreen::kSetup ||
                            (screen == PomodoroScreen::kBanner && pomodoroBannerIsVoid));

    switch (screen) {
        case PomodoroScreen::kSetup:
            UpdatePomodoroSetupUI();
            break;
        case PomodoroScreen::kRunning:
            UpdatePomodoroRunUI();
            break;
        case PomodoroScreen::kBanner:
            break;
    }
    // Same reasoning as ShowQuickTapScreen: the panel is opaque and covers the
    // screen, so one repaint at a screen change is the cheapest reliable way to
    // clear whatever the previous screen left behind.
    if (pomodoroPanel != nullptr) {
        lv_obj_invalidate(pomodoroPanel);
    }
}

void EnterPomodoroBanner(const char* top, const char* big, const char* sub,
                         uint32_t color, bool is_void) {
    pomodoroBannerIsVoid = is_void;
    if (pomodoroBannerTop != nullptr) {
        lv_label_set_text(pomodoroBannerTop, top);
    }
    if (pomodoroBannerBig != nullptr) {
        lv_label_set_text(pomodoroBannerBig, big);
        lv_obj_set_style_text_color(pomodoroBannerBig, lv_color_hex(color), 0);
    }
    if (pomodoroBannerSub != nullptr) {
        lv_label_set_text(pomodoroBannerSub, sub);
        lv_obj_set_style_text_color(pomodoroBannerSub,
                                    lv_color_hex(is_void ? kPomodoroMuted : kPomodoroRestColor), 0);
    }
    pomodoroBannerStartMs = lv_tick_get();
    ShowPomodoroScreen(PomodoroScreen::kBanner);
}

// A focus block ran to zero. This is the only path that banks anything.
void BankPomodoroFocusBlock() {
    const PomodoroTimer::Stats stats = PomodoroTimer::GetStats();
    char sub[24];
    std::snprintf(sub, sizeof(sub), "CHUỖI %u NGÀY",
                  static_cast<unsigned>(std::min<uint16_t>(stats.streak_days, 999)));
    PlayPomodoroSound(Lang::Sounds::OGG_SUCCESS);
    EnterPomodoroBanner("HẾT CHẶNG", "XONG!", sub, kPomodoroFocusColor, false);

    // Both of these commit to NVS synchronously (AddMood can level up, and a
    // level-up saves immediately). This runs on the LVGL task with the display
    // lock held, so the flash writes are handed to the main task.
    Application::GetInstance().Schedule([]() {
        PomodoroTimer::CommitCompletedFocus();
        CareSystem::AddMood(CareSystem::kGamesBoost);
        LevelSystem::AddXP(10);
    });
}

void VoidPomodoroBlock() {
    if (PomodoroTimer::GetPhase() != PomodoroTimer::Phase::kFocus) {
        return;
    }
    PomodoroTimer::VoidBlock();
    PlayPomodoroSound(Vox::Pick(Lang::Sounds::OGG_VOX_SAD_1_A, Lang::Sounds::OGG_VOX_SAD_1_B));
    EnterPomodoroBanner("BỊ NGẮT", "ĐÃ HỦY", "KHÔNG TÍNH", kPomodoroMuted, true);
}

// Runs on the LVGL task inside lv_timer_handler, which already holds the
// display lock -- taking DisplayLockGuard here would deadlock on it.
void PomodoroTimerCb(lv_timer_t* timer) {
    (void)timer;
    if (currentState != MENU_POMODORO_OPEN) {
        return;
    }

    // Without this the menu's 30s inactivity close-out drops a running block
    // back to the eyes -- and with no pause, that close is a void.
    if (pomodoroScreen != PomodoroScreen::kSetup) {
        MarkMenuActivity();
    }

    if (pomodoroScreen == PomodoroScreen::kBanner) {
        if (lv_tick_elaps(pomodoroBannerStartMs) < kPomodoroBannerMs) {
            return;
        }
        if (pomodoroBannerIsVoid) {
            ShowPomodoroScreen(PomodoroScreen::kSetup);
        } else {
            ShowPomodoroScreen(PomodoroScreen::kRunning);
        }
        return;
    }

    if (pomodoroScreen != PomodoroScreen::kRunning) {
        return;
    }

    switch (PomodoroTimer::Update()) {
        case PomodoroTimer::Event::kFocusCompleted:
            BankPomodoroFocusBlock();
            return;
        case PomodoroTimer::Event::kBreakCompleted:
            PlayPomodoroSound(Lang::Sounds::OGG_NOTIFICATION);
            EnterPomodoroBanner("HẾT GIỜ NGHỈ", "TIẾP TỤC",
                                PomodoroTimer::PhaseName(PomodoroTimer::Phase::kFocus),
                                kPomodoroRestColor, false);
            return;
        case PomodoroTimer::Event::kNone:
            break;
    }
    UpdatePomodoroRunUI();
}

void StartPomodoroTimer() {
    if (pomodoroTimer == nullptr) {
        pomodoroTimer = lv_timer_create(PomodoroTimerCb, kPomodoroTickMs, nullptr);
    }
}

void StopPomodoroTimer() {
    if (pomodoroTimer != nullptr) {
        lv_timer_delete(pomodoroTimer);
        pomodoroTimer = nullptr;
    }
}

void BeginPomodoroRun(PomodoroTimer::Preset preset) {
    pomodoroPreset = preset;
    PomodoroTimer::Start(preset);
    PlayPomodoroSound(Lang::Sounds::OGG_POPUP);
    ShowPomodoroScreen(PomodoroScreen::kRunning);
}

lv_obj_t* CreatePomodoroLabel(lv_obj_t* parent, const lv_font_t* font, uint32_t color,
                              int top, const char* text) {
    lv_obj_t* label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(label, 240);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(label, text);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, top);
    return label;
}

void CreatePomodoroPanel() {
    if (pomodoroPanel != nullptr) {
        return;
    }

    pomodoroPanel = lv_obj_create(lv_screen_active());
    lv_obj_set_size(pomodoroPanel, 240, 240);
    lv_obj_center(pomodoroPanel);
    lv_obj_set_style_radius(pomodoroPanel, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(pomodoroPanel, lv_color_hex(COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(pomodoroPanel, LV_OPA_COVER, 0);
    // No border: the countdown ring is this screen's edge, the same call the
    // games list makes. Children align to the content area, so a border here
    // would shift every measured coordinate below.
    lv_obj_set_style_border_width(pomodoroPanel, 0, 0);
    lv_obj_set_style_pad_all(pomodoroPanel, 0, 0);
    lv_obj_clear_flag(pomodoroPanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(pomodoroPanel, LV_OBJ_FLAG_HIDDEN);

    // ---- Countdown ring ---------------------------------------------------
    pomodoroArc = lv_arc_create(pomodoroPanel);
    lv_obj_remove_style_all(pomodoroArc);
    lv_obj_set_size(pomodoroArc, kPomodoroRingBox, kPomodoroRingBox);
    lv_obj_center(pomodoroArc);
    lv_arc_set_rotation(pomodoroArc, 270);        // drains from 12 o'clock
    lv_arc_set_bg_angles(pomodoroArc, 0, 360);
    lv_arc_set_mode(pomodoroArc, LV_ARC_MODE_NORMAL);
    lv_arc_set_range(pomodoroArc, 0, 1000);
    lv_arc_set_value(pomodoroArc, 1000);
    lv_obj_remove_flag(pomodoroArc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(pomodoroArc, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_arc_width(pomodoroArc, kPomodoroRingWidth, LV_PART_MAIN);
    lv_obj_set_style_arc_width(pomodoroArc, kPomodoroRingWidth, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(pomodoroArc, lv_color_hex(kPomodoroTrack), LV_PART_MAIN);
    lv_obj_set_style_arc_color(pomodoroArc, lv_color_hex(kPomodoroFocusColor), LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(pomodoroArc, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(pomodoroArc, true, LV_PART_INDICATOR);
    lv_obj_add_flag(pomodoroArc, LV_OBJ_FLAG_HIDDEN);

    // ---- Setup: three round preset containers -----------------------------
    pomodoroSetupScreen = lv_obj_create(pomodoroPanel);
    lv_obj_remove_style_all(pomodoroSetupScreen);
    lv_obj_set_size(pomodoroSetupScreen, 240, 240);
    lv_obj_center(pomodoroSetupScreen);
    lv_obj_remove_flag(pomodoroSetupScreen, LV_OBJ_FLAG_SCROLLABLE);

    pomodoroTodayLabel = CreatePomodoroLabel(pomodoroSetupScreen, &lv_font_montserrat_vn_20,
                                             kPomodoroMuted, kPomodoroTodayTop, "HÔM NAY 0");

    for (int i = 0; i < kPomodoroPresetCount; ++i) {
        lv_obj_t* disc = lv_obj_create(pomodoroSetupScreen);
        lv_obj_remove_style_all(disc);
        lv_obj_set_size(disc, kPomodoroDiscSize, kPomodoroDiscSize);
        lv_obj_set_pos(disc, kPomodoroDiscLeft + i * kPomodoroDiscStride, kPomodoroDiscTop);
        lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(disc, 2, 0);
        lv_obj_set_style_border_opa(disc, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(disc, lv_color_hex(kPomodoroTrack), 0);
        lv_obj_remove_flag(disc, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* label = lv_label_create(disc);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_vn_28, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(COLOR_TEXT), 0);
        lv_label_set_text(label, PomodoroTimer::PresetLabel(static_cast<PomodoroTimer::Preset>(i)));
        lv_obj_center(label);
        pomodoroDiscs[i] = disc;
    }

    pomodoroBreakLabel = CreatePomodoroLabel(pomodoroSetupScreen, &lv_font_montserrat_vn_20,
                                             kPomodoroRestColor, kPomodoroBreakTop, "NGHỈ 5'");
    lv_obj_add_flag(pomodoroSetupScreen, LV_OBJ_FLAG_HIDDEN);

    // ---- Running ----------------------------------------------------------
    pomodoroRunScreen = lv_obj_create(pomodoroPanel);
    lv_obj_remove_style_all(pomodoroRunScreen);
    lv_obj_set_size(pomodoroRunScreen, 240, 240);
    lv_obj_center(pomodoroRunScreen);
    lv_obj_remove_flag(pomodoroRunScreen, LV_OBJ_FLAG_SCROLLABLE);

    pomodoroPhaseLabel = CreatePomodoroLabel(pomodoroRunScreen, &lv_font_montserrat_vn_20,
                                             kPomodoroFocusColor, kPomodoroPhaseTop, "TẬP TRUNG");
    pomodoroClockLabel = CreatePomodoroLabel(pomodoroRunScreen, &lv_font_montserrat_vn_28,
                                             COLOR_TEXT, kPomodoroClockTop, "25:00");
    pomodoroLenLabel = CreatePomodoroLabel(pomodoroRunScreen, &lv_font_montserrat_vn_20,
                                           kPomodoroMuted, kPomodoroLenTop, "25'");

    pomodoroDotsRow = lv_obj_create(pomodoroRunScreen);
    lv_obj_remove_style_all(pomodoroDotsRow);
    lv_obj_set_size(pomodoroDotsRow, LV_SIZE_CONTENT, kPomodoroDotSize);
    lv_obj_align(pomodoroDotsRow, LV_ALIGN_TOP_MID, 0, kPomodoroDotsTop);
    lv_obj_set_flex_flow(pomodoroDotsRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(pomodoroDotsRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(pomodoroDotsRow, 9, 0);
    lv_obj_remove_flag(pomodoroDotsRow, LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < kPomodoroMaxDots; ++i) {
        lv_obj_t* dot = lv_obj_create(pomodoroDotsRow);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, kPomodoroDotSize, kPomodoroDotSize);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, lv_color_hex(kPomodoroTrack), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        pomodoroDots[i] = dot;
    }
    lv_obj_add_flag(pomodoroRunScreen, LV_OBJ_FLAG_HIDDEN);

    // ---- Boundary / void banner -------------------------------------------
    pomodoroBannerScreen = lv_obj_create(pomodoroPanel);
    lv_obj_remove_style_all(pomodoroBannerScreen);
    lv_obj_set_size(pomodoroBannerScreen, 240, 240);
    lv_obj_center(pomodoroBannerScreen);
    lv_obj_remove_flag(pomodoroBannerScreen, LV_OBJ_FLAG_SCROLLABLE);
    pomodoroBannerTop = CreatePomodoroLabel(pomodoroBannerScreen, &lv_font_montserrat_vn_20,
                                            kPomodoroMuted, kPomodoroBannerTopY, "HẾT CHẶNG");
    pomodoroBannerBig = CreatePomodoroLabel(pomodoroBannerScreen, &lv_font_montserrat_vn_28,
                                            kPomodoroFocusColor, kPomodoroBannerBigY, "XONG!");
    pomodoroBannerSub = CreatePomodoroLabel(pomodoroBannerScreen, &lv_font_montserrat_vn_20,
                                            kPomodoroRestColor, kPomodoroBannerSubY, "CHUỖI 1 NGÀY");
    lv_obj_add_flag(pomodoroBannerScreen, LV_OBJ_FLAG_HIDDEN);

    UpdatePomodoroSetupUI();
}

// The X strokes of the CỜ CA-RÔ emblem. lv_line does not copy its points, so
// these must outlive every line object -- same rule as the board's own marks.
const lv_point_precise_t kGamesCheckerXA[] = {{5, 5}, {35, 35}};
const lv_point_precise_t kGamesCheckerXB[] = {{35, 5}, {5, 35}};

lv_obj_t* CreateEmblemBox(lv_obj_t* parent) {
    lv_obj_t* box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, kGamesEmblemSize, kGamesEmblemSize);
    lv_obj_center(box);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(box, LV_OBJ_FLAG_HIDDEN);
    return box;
}

// A filled rounded rect. Used for the eye shapes and the target's dot.
lv_obj_t* AddEmblemSolid(lv_obj_t* parent, int x, int y, int w, int h, int radius,
                         uint32_t color) {
    lv_obj_t* part = lv_obj_create(parent);
    lv_obj_remove_style_all(part);
    lv_obj_set_size(part, w, h);
    lv_obj_set_pos(part, x, y);
    lv_obj_set_style_radius(part, radius, 0);
    lv_obj_set_style_bg_color(part, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(part, LV_OPA_COVER, 0);
    lv_obj_remove_flag(part, LV_OBJ_FLAG_SCROLLABLE);
    return part;
}

// An unfilled ring: circle radius, no fill, coloured border.
lv_obj_t* AddEmblemRing(lv_obj_t* parent, int x, int y, int size, int width,
                        uint32_t color, lv_opa_t opa) {
    lv_obj_t* ring = lv_obj_create(parent);
    lv_obj_remove_style_all(ring);
    lv_obj_set_size(ring, size, size);
    lv_obj_set_pos(ring, x, y);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ring, width, 0);
    lv_obj_set_style_border_color(ring, lv_color_hex(color), 0);
    lv_obj_set_style_border_opa(ring, opa, 0);
    lv_obj_remove_flag(ring, LV_OBJ_FLAG_SCROLLABLE);
    return ring;
}

// The three emblem marks. Each is drawn once and then only shown or hidden --
// nothing here is rebuilt on navigation.
void CreateGamesEmblemMarks() {
    // MẮT XANH: four of the game's eyes, and the green one ringed -- the
    // answer. Blue sits beside it on purpose: xanh dương is the trap.
    {
        using GreenEyeGame::Hue;
        lv_obj_t* eyes = CreateEmblemBox(gamesEmblem);
        gamesEmblemMarks[GAME_SELECTION_GREEN_EYE] = eyes;
        AddEmblemSolid(eyes, 22, 22, 26, 26, 8, GreenEyeGame::HueRgb(Hue::kRed));
        AddEmblemSolid(eyes, 56, 22, 26, 26, 8, GreenEyeGame::HueRgb(Hue::kBlue));
        AddEmblemSolid(eyes, 22, 56, 26, 26, 8, GreenEyeGame::HueRgb(Hue::kYellow));
        lv_obj_t* answer = AddEmblemSolid(eyes, 56, 56, 26, 26, 8, GreenEyeGame::HueRgb(Hue::kGreen));
        lv_obj_set_style_border_width(answer, 3, 0);
        lv_obj_set_style_border_color(answer, lv_color_hex(COLOR_TEXT), 0);
        lv_obj_set_style_border_opa(answer, LV_OPA_COVER, 0);
    }

    // CỜ CA-RÔ: one X, one O. The 3x3 grid behind them is dropped -- at this
    // size its lines land under 2px and read as noise rather than a board.
    lv_obj_t* checker = CreateEmblemBox(gamesEmblem);
    gamesEmblemMarks[GAME_SELECTION_CHECKER] = checker;
    {
        lv_obj_t* x_mark = lv_obj_create(checker);
        lv_obj_remove_style_all(x_mark);
        lv_obj_set_size(x_mark, 40, 40);
        lv_obj_set_pos(x_mark, 12, 32);
        lv_obj_remove_flag(x_mark, LV_OBJ_FLAG_SCROLLABLE);
        for (int stroke = 0; stroke < 2; ++stroke) {
            lv_obj_t* line = lv_line_create(x_mark);
            lv_line_set_points(line, stroke == 0 ? kGamesCheckerXA : kGamesCheckerXB, 2);
            lv_obj_set_style_line_width(line, 6, 0);
            lv_obj_set_style_line_rounded(line, true, 0);
            lv_obj_set_style_line_color(line, lv_color_hex(kCheckerPlayerMark), 0);
            lv_obj_set_pos(line, 0, 0);
        }
        AddEmblemRing(checker, 54, 33, 38, 6, COLOR_TEXT, LV_OPA_70);
    }

    // CHẠM NHANH: the target, plus a short sweep for the 30-second clock.
    lv_obj_t* quick = CreateEmblemBox(gamesEmblem);
    gamesEmblemMarks[GAME_SELECTION_QUICK_TAP] = quick;
    AddEmblemRing(quick, 21, 21, 62, 2, kQuickTapRing, LV_OPA_40);
    AddEmblemRing(quick, 33, 33, 38, 3, kQuickTapRing, LV_OPA_70);
    AddEmblemSolid(quick, 44, 44, 16, 16, LV_RADIUS_CIRCLE, kQuickTapWhiteDot);
    {
        lv_obj_t* sweep = lv_arc_create(quick);
        lv_obj_remove_style_all(sweep);
        lv_obj_set_size(sweep, 84, 84);
        lv_obj_center(sweep);
        lv_obj_remove_flag(sweep, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(sweep, LV_OBJ_FLAG_SCROLLABLE);
        lv_arc_set_bg_angles(sweep, 0, 0);
        lv_arc_set_angles(sweep, 268, 330);
        lv_obj_set_style_arc_width(sweep, 4, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(sweep, lv_color_hex(kQuickTapRing), LV_PART_INDICATOR);
        lv_obj_set_style_arc_rounded(sweep, true, LV_PART_INDICATOR);
    }

    // RẮN SĂN MỒI: three body blocks curling towards the pellet. Blocks rather
    // than a smooth line so it reads as the same grid the game is played on.
    lv_obj_t* snake = CreateEmblemBox(gamesEmblem);
    gamesEmblemMarks[GAME_SELECTION_SNAKE] = snake;
    AddEmblemSolid(snake, 20, 56, 16, 16, 3, kSnakeBody);
    AddEmblemSolid(snake, 38, 56, 16, 16, 3, kSnakeBody);
    AddEmblemSolid(snake, 38, 38, 16, 16, 3, kSnakeBody);
    AddEmblemSolid(snake, 38, 20, 16, 16, 3, kSnakeHead);
    AddEmblemSolid(snake, 62, 22, 12, 12, LV_RADIUS_CIRCLE, kSnakePellet);

    // MÊ CUNG: a narrow cyan route, white marble and green destination.
    lv_obj_t* maze = CreateEmblemBox(gamesEmblem);
    gamesEmblemMarks[GAME_SELECTION_TILT_MAZE] = maze;
    AddEmblemSolid(maze, 18, 22, 68, 10, 4, kMazeWall);
    AddEmblemSolid(maze, 18, 32, 10, 48, 4, kMazeWall);
    AddEmblemSolid(maze, 48, 32, 10, 34, 4, kMazeWall);
    AddEmblemSolid(maze, 28, 56, 30, 10, 4, kMazeWall);
    AddEmblemSolid(maze, 28, 38, 12, 12, LV_RADIUS_CIRCLE, 0xFFFFFF);
    AddEmblemRing(maze, 66, 58, 18, 4, kMazeGoal, LV_OPA_COVER);

    // ĐƯỜNG PHỐ: the same deliberately simple geometry used in the game.
    lv_obj_t* runner = CreateEmblemBox(gamesEmblem);
    gamesEmblemMarks[GAME_SELECTION_TRAFFIC_RUNNER] = runner;
    AddEmblemSolid(runner, 24, 18, 4, 68, 2, kRunnerLane);
    AddEmblemSolid(runner, 76, 18, 4, 68, 2, kRunnerLane);
    AddEmblemSolid(runner, 37, 25, 22, 34, 4, 0xFF6B5C);
    AddEmblemSolid(runner, 66, 36, 12, 12, LV_RADIUS_CIRCLE, kRunnerGold);
    AddEmblemSolid(runner, 20, 66, 18, 18, LV_RADIUS_CIRCLE, 0xFFFFFF);
}

// Tic-tac-toe screens. Built on demand by LoadGameUi(), deleted by
// UnloadGameUi() -- see "On-demand game screens".
void CreateCheckerUI() {
    if (gamesPanel == nullptr || checkerGrid != nullptr) {
        return;
    }

    checkerGrid = lv_obj_create(gamesPanel);
    lv_obj_set_size(checkerGrid, kCheckerBoardSize, kCheckerBoardSize);
    lv_obj_align(checkerGrid, LV_ALIGN_CENTER, 0, kCheckerBoardOffsetY);
    lv_obj_set_style_radius(checkerGrid, 0, 0);
    lv_obj_set_style_bg_opa(checkerGrid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(checkerGrid, 0, 0);
    lv_obj_set_style_outline_opa(checkerGrid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_opa(checkerGrid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(checkerGrid, 0, 0);
    lv_obj_clear_flag(checkerGrid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(checkerGrid, LV_OBJ_FLAG_HIDDEN);

    for (uint8_t index = 0; index < checkerCellButtons.size(); ++index) {
        const int row = index / 3;
        const int col = index % 3;
        lv_obj_t* cell = lv_btn_create(checkerGrid);
        checkerCellButtons[index] = cell;
        lv_obj_set_size(cell, kCheckerCellSize, kCheckerCellSize);
        lv_obj_set_pos(cell, col * kCheckerCellStride, row * kCheckerCellStride);
        lv_obj_set_style_radius(cell, 10, 0);
        lv_obj_set_style_bg_color(cell, lv_color_hex(kCheckerCellBg), 0);
        lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(cell, 2, 0);
        lv_obj_set_style_border_color(cell, lv_color_hex(kCheckerCellBorder), 0);
        lv_obj_set_style_shadow_width(cell, 0, 0);
        lv_obj_set_style_shadow_opa(cell, LV_OPA_TRANSP, 0);

        // X: two rounded strokes. The point arrays are shared and static --
        // lv_line does not copy them, so they must outlive every line object.
        lv_obj_t* x_mark = lv_obj_create(cell);
        checkerXMarks[index] = x_mark;
        lv_obj_remove_style_all(x_mark);
        lv_obj_set_size(x_mark, kCheckerMarkBox, kCheckerMarkBox);
        lv_obj_center(x_mark);
        lv_obj_remove_flag(x_mark, LV_OBJ_FLAG_SCROLLABLE);
        for (int stroke = 0; stroke < 2; ++stroke) {
            lv_obj_t* line = lv_line_create(x_mark);
            lv_line_set_points(line, stroke == 0 ? kCheckerXStrokeA : kCheckerXStrokeB, 2);
            lv_obj_set_style_line_width(line, 6, 0);
            lv_obj_set_style_line_rounded(line, true, 0);
            lv_obj_set_style_line_color(line, lv_color_hex(kCheckerPlayerMark), 0);
            lv_obj_set_pos(line, 0, 0);
        }
        lv_obj_add_flag(x_mark, LV_OBJ_FLAG_HIDDEN);

        // O: a ring -- circle radius, no fill, thick coloured border.
        lv_obj_t* o_mark = lv_obj_create(cell);
        checkerOMarks[index] = o_mark;
        lv_obj_remove_style_all(o_mark);
        lv_obj_set_size(o_mark, kCheckerRingSize, kCheckerRingSize);
        lv_obj_center(o_mark);
        lv_obj_set_style_radius(o_mark, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(o_mark, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(o_mark, kCheckerRingWidth, 0);
        lv_obj_set_style_border_color(o_mark, lv_color_hex(kCheckerBubuMark), 0);
        lv_obj_set_style_border_opa(o_mark, LV_OPA_COVER, 0);
        lv_obj_remove_flag(o_mark, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(o_mark, LV_OBJ_FLAG_HIDDEN);
    }

    // Bubu head glyph for the turn header (no robot glyph exists in the icon font).
    checkerBotIcon = lv_obj_create(gamesPanel);
    lv_obj_remove_style_all(checkerBotIcon);
    lv_obj_set_size(checkerBotIcon, 18, 18);
    lv_obj_remove_flag(checkerBotIcon, LV_OBJ_FLAG_SCROLLABLE);
    {
        struct Part { int x, y, w, h, r; uint32_t color; };
        const Part parts[] = {
            { 8, 0,  2, 5, 1, kCheckerBotBody},   // antenna
            { 1, 4, 16, 13, 5, kCheckerBotBody},  // head
            { 5, 9,  3, 3, 1, 0x101014},          // left eye
            {10, 9,  3, 3, 1, 0x101014},          // right eye
        };
        for (const auto& part : parts) {
            lv_obj_t* piece = lv_obj_create(checkerBotIcon);
            lv_obj_remove_style_all(piece);
            lv_obj_set_size(piece, part.w, part.h);
            lv_obj_set_pos(piece, part.x, part.y);
            lv_obj_set_style_radius(piece, part.r, 0);
            lv_obj_set_style_bg_color(piece, lv_color_hex(part.color), 0);
            lv_obj_set_style_bg_opa(piece, LV_OPA_COVER, 0);
        }
    }
    lv_obj_add_flag(checkerBotIcon, LV_OBJ_FLAG_HIDDEN);

    // ---- Title screen -----------------------------------------------------
    checkerTitleScreen = lv_obj_create(gamesPanel);
    lv_obj_remove_style_all(checkerTitleScreen);
    lv_obj_set_size(checkerTitleScreen, 240, 240);
    lv_obj_center(checkerTitleScreen);
    lv_obj_remove_flag(checkerTitleScreen, LV_OBJ_FLAG_SCROLLABLE);
    {
        lv_obj_t* title = lv_label_create(checkerTitleScreen);
        lv_label_set_text(title, "CỜ CA-RÔ");
        lv_obj_set_style_text_font(title, &lv_font_montserrat_vn_28, 0);
        lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
        lv_obj_align(title, LV_ALIGN_CENTER, 0, -10);

        checkerPlayBtn = lv_obj_create(checkerTitleScreen);
        lv_obj_remove_style_all(checkerPlayBtn);
        lv_obj_set_size(checkerPlayBtn, 68, 40);
        lv_obj_align(checkerPlayBtn, LV_ALIGN_CENTER, 0, 46);
        lv_obj_set_style_radius(checkerPlayBtn, 20, 0);
        lv_obj_set_style_bg_color(checkerPlayBtn, lv_color_hex(kCheckerBotBody), 0);
        lv_obj_set_style_bg_opa(checkerPlayBtn, LV_OPA_COVER, 0);
        ApplyCheckerGlow(checkerPlayBtn, kCheckerBotBody, 14, LV_OPA_40);
        lv_obj_t* play_icon = lv_label_create(checkerPlayBtn);
        lv_label_set_text(play_icon, FONT_AWESOME_PLAY);
        lv_obj_set_style_text_color(play_icon, lv_color_hex(0x1A1A00), 0);
        lv_obj_center(play_icon);
    }
    lv_obj_add_flag(checkerTitleScreen, LV_OBJ_FLAG_HIDDEN);

    // ---- Matchup card -----------------------------------------------------
    checkerMatchupScreen = lv_obj_create(gamesPanel);
    lv_obj_remove_style_all(checkerMatchupScreen);
    lv_obj_set_size(checkerMatchupScreen, 240, 240);
    lv_obj_center(checkerMatchupScreen);
    lv_obj_remove_flag(checkerMatchupScreen, LV_OBJ_FLAG_SCROLLABLE);
    for (int side = 0; side < 2; ++side) {
        const bool player_side = (side == 0);
        const uint32_t color = player_side ? kCheckerPlayerMark : kCheckerBubuMark;
        lv_obj_t* card = lv_obj_create(checkerMatchupScreen);
        lv_obj_remove_style_all(card);
        lv_obj_set_size(card, 76, 104);
        lv_obj_align(card, LV_ALIGN_CENTER, player_side ? -44 : 44, 8);
        lv_obj_set_style_radius(card, 18, 0);
        lv_obj_set_style_bg_color(card, lv_color_hex(kCheckerCellBg), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(card, 3, 0);
        lv_obj_set_style_border_color(card, lv_color_hex(color), 0);
        ApplyCheckerGlow(card, color, 14, LV_OPA_50);
        lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* mark = player_side
            ? CreateCheckerXMark(card, kCheckerBigMarkBox, kCheckerBigXStrokeA,
                                 kCheckerBigXStrokeB, 7, color)
            : CreateCheckerORing(card, kCheckerBigMarkBox, 7, color);
        lv_obj_align(mark, LV_ALIGN_CENTER, 0, -14);

        lv_obj_t* who = lv_label_create(card);
        lv_label_set_text(who, player_side ? "BẠN" : "BUBU");
        lv_obj_set_style_text_font(who, &lv_font_montserrat_vn_20, 0);
        lv_obj_set_style_text_color(who, lv_color_hex(0xFFFFFF), 0);
        lv_obj_align(who, LV_ALIGN_BOTTOM_MID, 0, -8);
    }
    lv_obj_add_flag(checkerMatchupScreen, LV_OBJ_FLAG_HIDDEN);

    // ---- Play again -------------------------------------------------------
    checkerPlayAgainScreen = lv_obj_create(gamesPanel);
    lv_obj_remove_style_all(checkerPlayAgainScreen);
    lv_obj_set_size(checkerPlayAgainScreen, 240, 240);
    lv_obj_center(checkerPlayAgainScreen);
    lv_obj_remove_flag(checkerPlayAgainScreen, LV_OBJ_FLAG_SCROLLABLE);
    {
        lv_obj_t* prompt = lv_label_create(checkerPlayAgainScreen);
        lv_label_set_text(prompt, "CHƠI LẠI?");
        lv_obj_set_style_text_font(prompt, &lv_font_montserrat_vn_22, 0);
        lv_obj_set_style_text_color(prompt, lv_color_hex(0xFFFFFF), 0);
        lv_obj_align(prompt, LV_ALIGN_CENTER, 0, -58);

        for (int side = 0; side < 2; ++side) {
            const bool yes = (side == 0);
            const uint32_t color = yes ? kCheckerPlayerMark : kCheckerBubuMark;
            lv_obj_t* btn = lv_obj_create(checkerPlayAgainScreen);
            lv_obj_remove_style_all(btn);
            lv_obj_set_size(btn, 84, 84);
            lv_obj_align(btn, LV_ALIGN_CENTER, yes ? -46 : 46, 12);
            lv_obj_set_style_radius(btn, 20, 0);
            lv_obj_set_style_bg_color(btn, lv_color_hex(color), 0);
            lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
            ApplyCheckerGlow(btn, color, 14, LV_OPA_40);
            lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);

            lv_obj_t* icon = lv_label_create(btn);
            lv_label_set_text(icon, yes ? FONT_AWESOME_CHECK : FONT_AWESOME_XMARK);
            lv_obj_set_style_text_color(icon, lv_color_hex(0x0A0A0C), 0);
            lv_obj_center(icon);

            if (yes) {
                checkerYesBtn = btn;
            } else {
                checkerNoBtn = btn;
            }
        }
    }
    lv_obj_add_flag(checkerPlayAgainScreen, LV_OBJ_FLAG_HIDDEN);

    // ---- Record dots ------------------------------------------------------
    checkerDotsRow = lv_obj_create(gamesPanel);
    lv_obj_remove_style_all(checkerDotsRow);
    lv_obj_set_size(checkerDotsRow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(checkerDotsRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(checkerDotsRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(checkerDotsRow, 10, 0);
    lv_obj_remove_flag(checkerDotsRow, LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < kCheckerRecordSlots; ++i) {
        lv_obj_t* dot = lv_obj_create(checkerDotsRow);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, 8, 8);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(dot, lv_color_hex(kCheckerDotIdle), 0);
        checkerDots[i] = dot;
    }
    lv_obj_add_flag(checkerDotsRow, LV_OBJ_FLAG_HIDDEN);
    LoadCheckerRecord();
    UpdateCheckerDots();

    // ---- Confetti ---------------------------------------------------------
    checkerFx = lv_obj_create(gamesPanel);
    lv_obj_remove_style_all(checkerFx);
    lv_obj_set_size(checkerFx, 240, 240);
    lv_obj_center(checkerFx);
    lv_obj_remove_flag(checkerFx, LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < kCheckerParticleCount; ++i) {
        lv_obj_t* particle = lv_obj_create(checkerFx);
        lv_obj_remove_style_all(particle);
        const bool square = (i % 3) == 0;
        lv_obj_set_size(particle, square ? 6 : 5, square ? 6 : 5);
        lv_obj_set_style_radius(particle, square ? 1 : LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(particle, LV_OPA_COVER, 0);
        const uint32_t palette[] = {kCheckerPlayerMark, kCheckerBubuMark, kCheckerBotBody};
        lv_obj_set_style_bg_color(particle, lv_color_hex(palette[i % 3]), 0);
        lv_obj_add_flag(particle, LV_OBJ_FLAG_HIDDEN);
        checkerParticles[i] = particle;
    }
    lv_obj_add_flag(checkerFx, LV_OBJ_FLAG_HIDDEN);
}

void CreateGamesPanel() {
    if (gamesPanel != nullptr) {
        return;
    }

    gamesPanel = lv_obj_create(lv_screen_active());
    lv_obj_set_size(gamesPanel, 240, 240);
    lv_obj_center(gamesPanel);
    lv_obj_set_style_radius(gamesPanel, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(gamesPanel, lv_color_hex(COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(gamesPanel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(gamesPanel, 8, 0);
    lv_obj_set_style_border_color(gamesPanel, lv_color_hex(0x1C2E45), 0);
    lv_obj_set_style_border_opa(gamesPanel, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(gamesPanel, 0, 0);
    lv_obj_clear_flag(gamesPanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(gamesPanel, LV_OBJ_FLAG_HIDDEN);

    // Position ring. The track is the full circle; the indicator is the lit
    // third and is re-angled in UpdateGamesUI().
    gamesRing = lv_arc_create(gamesPanel);
    lv_obj_remove_style_all(gamesRing);
    lv_obj_set_size(gamesRing, kGamesRingBox, kGamesRingBox);
    lv_obj_center(gamesRing);
    lv_obj_remove_flag(gamesRing, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(gamesRing, LV_OBJ_FLAG_SCROLLABLE);
    lv_arc_set_bg_angles(gamesRing, 0, 360);
    lv_obj_set_style_arc_width(gamesRing, kGamesRingWidth, LV_PART_MAIN);
    lv_obj_set_style_arc_color(gamesRing, lv_color_hex(kGamesRingTrack), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(gamesRing, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_width(gamesRing, kGamesRingWidth, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(gamesRing, true, LV_PART_INDICATOR);

    // The emblem disc. It sits under the centre of the panel, which is already
    // the tap-to-play zone: HandleTap() treats any games-list tap that misses
    // the two nav buttons as activate.
    gamesEmblem = lv_obj_create(gamesPanel);
    lv_obj_remove_style_all(gamesEmblem);
    lv_obj_set_size(gamesEmblem, kGamesEmblemSize, kGamesEmblemSize);
    lv_obj_align(gamesEmblem, LV_ALIGN_TOP_MID, 0, kGamesEmblemTop);
    lv_obj_set_style_radius(gamesEmblem, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(gamesEmblem, LV_OPA_10, 0);
    lv_obj_set_style_border_width(gamesEmblem, 2, 0);
    lv_obj_set_style_border_opa(gamesEmblem, LV_OPA_60, 0);
    lv_obj_remove_flag(gamesEmblem, LV_OBJ_FLAG_SCROLLABLE);
    CreateGamesEmblemMarks();

    gamesAction = lv_label_create(gamesPanel);
    lv_obj_set_style_text_font(gamesAction, &lv_font_montserrat_vn_22, 0);
    lv_label_set_text(gamesAction, "MẮT XANH");
    lv_obj_set_width(gamesAction, 240);
    lv_obj_set_style_text_align(gamesAction, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(gamesAction, LV_ALIGN_TOP_MID, 0, kGamesNameTop);

    // Single line by design: the fit budget below the name is one 20px row, so
    // wrapping here would push the second line off the glass.
    gamesStatus = lv_label_create(gamesPanel);
    lv_obj_set_style_text_font(gamesStatus, &lv_font_montserrat_vn_20, 0);
    lv_obj_set_style_text_color(gamesStatus, lv_color_hex(kGamesChipColor), 0);
    lv_label_set_long_mode(gamesStatus, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(gamesStatus, 240);
    lv_obj_set_style_text_align(gamesStatus, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(gamesStatus, gameStatusMsg);
    lv_obj_align(gamesStatus, LV_ALIGN_TOP_MID, 0, kGamesChipTop);

    // The 100x100 hit targets are unchanged; only the artwork is. The glyph is
    // pulled in to sit inside the ring instead of on a disc hanging off-screen.
    gamesPrevBtn = CreateNavButton(gamesPanel, LV_ALIGN_LEFT_MID, -52, 0, LV_SYMBOL_LEFT,
                                   LV_ALIGN_CENTER, 28, 0,
                                   [](lv_event_t*) { MenuSystem::NavigatePrev(); });
    gamesNextBtn = CreateNavButton(gamesPanel, LV_ALIGN_RIGHT_MID, 52, 0, LV_SYMBOL_RIGHT,
                                   LV_ALIGN_CENTER, -28, 0,
                                   [](lv_event_t*) { MenuSystem::NavigateNext(); });
    for (lv_obj_t* button : {gamesPrevBtn, gamesNextBtn}) {
        lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_opa(button, LV_OPA_TRANSP, 0);
        lv_obj_move_background(button);
    }

    // Game screens are no longer built here -- see "On-demand game screens"
    // below. What the carousel itself reads (records, maze progress) is data,
    // not widgets, so it still loads at boot.
    gamesLoadingLabel = lv_label_create(gamesPanel);
    lv_obj_set_style_text_font(gamesLoadingLabel, &lv_font_montserrat_vn_20, 0);
    lv_obj_set_style_text_color(gamesLoadingLabel, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_align(gamesLoadingLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(gamesLoadingLabel, 200);
    lv_label_set_text(gamesLoadingLabel, "ĐANG TẢI...");
    lv_obj_center(gamesLoadingLabel);
    lv_obj_add_flag(gamesLoadingLabel, LV_OBJ_FLAG_HIDDEN);

    LoadCheckerRecord();
    LoadGreenEyeBest();
    LoadQuickTapRecords();
    LoadSnakeRecords();
    TiltMazeGame::Initialize();
    TrafficRunnerGame::Initialize();

    SetGamesMenuStatusForSelection();
    UpdateGamesUI();
}

void CreateLevelPanel() {
    if (levelPanel != nullptr) {
        return;
    }

    levelPanel = lv_obj_create(lv_screen_active());
    lv_obj_set_size(levelPanel, 240, 240);
    lv_obj_center(levelPanel);
    lv_obj_set_style_radius(levelPanel, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(levelPanel, lv_color_hex(COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(levelPanel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_opa(levelPanel, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(levelPanel, 0, 0);
    lv_obj_clear_flag(levelPanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(levelPanel, LV_OBJ_FLAG_HIDDEN);

    levelTitle = lv_label_create(levelPanel);
    lv_obj_set_style_text_color(levelTitle, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(levelTitle, &lv_font_montserrat_vn_22, 0);
    lv_label_set_text(levelTitle, "LEVEL 1");
    lv_obj_align(levelTitle, LV_ALIGN_CENTER, 0, 0);

    levelArc = lv_arc_create(levelPanel);
    lv_obj_set_size(levelArc, 240, 240);
    lv_obj_center(levelArc);
    lv_arc_set_rotation(levelArc, 135);
    lv_arc_set_bg_angles(levelArc, 0, 270);
    lv_arc_set_mode(levelArc, LV_ARC_MODE_NORMAL);
    lv_arc_set_range(levelArc, 0, 100);
    lv_obj_clear_flag(levelArc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_style(levelArc, nullptr, LV_PART_KNOB);
    lv_obj_set_style_arc_width(levelArc, 16, LV_PART_MAIN);
    lv_obj_set_style_arc_width(levelArc, 16, LV_PART_INDICATOR);

    UpdateLevelUI();
}

}  // namespace

namespace MenuSystem {

void StartChecker3x3();
void StartQuickTap();
void StartSnake();
void StartTiltMaze();
void StartTrafficRunner();
void OpenFortuneTeller();
void CloseFortuneToMenu();

// ---------------------------------------------------------------------------
// On-demand game screens
//
// Every game used to build its whole LVGL tree in CreateGamesPanel() at boot
// and keep it, hidden, for as long as the device was on. Hidden objects are
// not drawn, but they still occupy memory -- and here that memory is internal
// SRAM: LVGL allocates through the C library (CONFIG_LV_USE_CLIB_MALLOC) and
// nearly every widget is below the 2 KB CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL
// line, so each game paid in the pool WiFi and TLS depend on, even when it was
// never opened.
//
// Now only the carousel is permanent. Opening a game shows "ĐANG TẢI...",
// builds that one game's screens, and starts it; leaving the game (back to the
// carousel, or the menu closing for any reason) deletes them again. At most one
// game's screens exist at a time. Mắt Xanh draws on the eyes and owns no
// widgets, so it starts immediately and never goes through here.
//
// Roots are found by diffing gamesPanel's children before and after the build,
// not by index: builders may lv_obj_move_background() their own objects.
// ---------------------------------------------------------------------------
ActiveGameType loadedGameUi = ACTIVE_GAME_NONE;
std::vector<lv_obj_t*> loadedGameRoots;
ActiveGameType gameLoadPending = ACTIVE_GAME_NONE;
lv_timer_t* gameLoadTimer = nullptr;
// Long enough for at least two LVGL refresh periods, so "ĐANG TẢI..." is on
// the glass before the build holds the display lock.
constexpr uint32_t kGameLoadPaintMs = 80;
constexpr uint32_t kGameLoadSpinMs = 900;

void LogInternalHeap(const char* what) {
    ESP_LOGI(TAG, "%s: internal free %u B, largest block %u B", what,
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));
}

// Every pointer below points into a tree UnloadGameUi() just deleted. Anything
// left non-null would be a use-after-free the next time a Hide*/Update* helper
// null-checks it and finds it "valid".
void ForgetGameWidgets() {
    checkerGrid = nullptr;
    checkerCellButtons.fill(nullptr);
    checkerXMarks.fill(nullptr);
    checkerOMarks.fill(nullptr);
    checkerBotIcon = nullptr;
    checkerTitleScreen = nullptr;
    checkerMatchupScreen = nullptr;
    checkerPlayAgainScreen = nullptr;
    checkerYesBtn = nullptr;
    checkerNoBtn = nullptr;
    checkerPlayBtn = nullptr;
    checkerDotsRow = nullptr;
    checkerDots.fill(nullptr);
    checkerFx = nullptr;
    checkerParticles.fill(nullptr);

    greenEyeRim = nullptr;
    greenEyeScoreLabel = nullptr;
    greenEyeHearts.fill(nullptr);
    greenEyeEyes.fill(nullptr);
    greenEyeGlints.fill(nullptr);
    greenEyePlus = nullptr;
    greenEyeCenterLabel = nullptr;
    greenEyeSetupScreen = nullptr;
    greenEyeSetupRecord = nullptr;
    greenEyeDemoGreen = nullptr;
    greenEyePlayBtn = nullptr;
    greenEyeScoreScreen = nullptr;
    greenEyeResultValue = nullptr;
    greenEyeResultCaption = nullptr;
    greenEyeRewardLabel = nullptr;
    greenEyeMoodGain = nullptr;
    greenEyeMoodBase = nullptr;
    greenEyeAgainBtn = nullptr;
    greenEyeMenuBtn = nullptr;
    greenEyeDrawn.fill(GreenEyeDrawn{});

    quickTapArc = nullptr;
    quickTapDot = nullptr;
    quickTapBanner = nullptr;
    quickTapSetupScreen = nullptr;
    quickTapModeBtns.fill(nullptr);
    quickTapDiffBtns.fill(nullptr);
    quickTapRecordLabel = nullptr;
    quickTapModeRecords.fill(nullptr);
    quickTapScoreScreen = nullptr;
    quickTapScoreValue = nullptr;
    quickTapScoreCaption = nullptr;
    quickTapStatRow = nullptr;
    quickTapHitsLabel = nullptr;
    quickTapRedMark = nullptr;
    quickTapRedLabel = nullptr;
    quickTapMissLabel = nullptr;
    quickTapStreakLabel = nullptr;
    quickTapReplayBtn = nullptr;
    quickTapMenuBtn = nullptr;

    snakeCanvas = nullptr;
    snakeScoreLabel = nullptr;
    snakeChevrons.fill(nullptr);
    snakeSetupScreen = nullptr;
    snakeSetupRecord = nullptr;
    snakeSpeedBtn = nullptr;
    snakePlayBtn = nullptr;
    snakeOverScreen = nullptr;
    snakeOverHead = nullptr;
    snakeOverScore = nullptr;
    snakeOverBest = nullptr;
    snakeAgainBtn = nullptr;
    snakeMenuBtn = nullptr;
    // Force the chevron state to repaint on the next build.
    snakeFlashChevron = -1;
    snakeFlashApplied = -2;

    mazeCanvas = nullptr;
    mazeHud = nullptr;
    mazeCalibrationScreen = nullptr;
    mazeCalibrationStatus = nullptr;
    mazeCalibrateBtn = nullptr;
    mazeCountdownLabel = nullptr;
    mazePlanningTitle = nullptr;
    mazePlanUpBtn = nullptr;
    mazePlanDownBtn = nullptr;
    mazePlanLeftBtn = nullptr;
    mazePlanRightBtn = nullptr;
    mazePlanResumeBtn = nullptr;
    mazeResultScreen = nullptr;
    mazeResultStars.fill(nullptr);
    mazeResultSummary = nullptr;
    mazeResultPrimaryBtn = nullptr;
    mazeResultRetryBtn = nullptr;
    mazeVisiblePhase = TiltMazeGame::Phase::kStopped;

    runnerCanvas = nullptr;
    runnerHudScore = nullptr;
    runnerHudGold = nullptr;
    runnerHudBoost = nullptr;
    runnerSetupScreen = nullptr;
    runnerSetupBest = nullptr;
    runnerPlayBtn = nullptr;
    runnerPhaseLabel = nullptr;
    runnerOverScreen = nullptr;
    runnerOverTitle = nullptr;
    runnerOverScore = nullptr;
    runnerOverBest = nullptr;
    runnerAgainBtn = nullptr;
    runnerMenuBtn = nullptr;
    runnerVisiblePhase = TrafficRunnerGame::Phase::kStopped;
}

// Caller holds the display lock.
void UnloadGameUi() {
    if (loadedGameUi == ACTIVE_GAME_NONE && loadedGameRoots.empty()) {
        return;
    }
    // Timers first: each one draws into the widgets about to be deleted.
    StopGreenEyeTimer();
    StopQuickTapTimer();
    StopSnakeTimer();
    StopTiltMazeTimer();
    StopTrafficRunnerTimer();
    HideCheckerConfetti();   // cancels the particle animations

    for (lv_obj_t* root : loadedGameRoots) {
        if (root != nullptr && lv_obj_is_valid(root)) {
            lv_obj_delete(root);
        }
    }
    loadedGameRoots.clear();

    // The canvases are gone, so nothing references their pixel buffers now.
    if (snakeCanvasBuf != nullptr) {
        heap_caps_free(snakeCanvasBuf);
        snakeCanvasBuf = nullptr;
    }
    if (mazeCanvasBuf != nullptr) {
        heap_caps_free(mazeCanvasBuf);
        mazeCanvasBuf = nullptr;
    }
    if (runnerCanvasBuf != nullptr) {
        heap_caps_free(runnerCanvasBuf);
        runnerCanvasBuf = nullptr;
    }
    ForgetGameWidgets();
    loadedGameUi = ACTIVE_GAME_NONE;
    LogInternalHeap("Game UI unloaded");
}

// Caller holds the display lock. Returns false for an unknown game.
bool LoadGameUi(ActiveGameType game) {
    UnloadGameUi();
    if (gamesPanel == nullptr) {
        return false;
    }
    LogInternalHeap("Game UI before load");

    std::vector<lv_obj_t*> before;
    const uint32_t before_count = lv_obj_get_child_count(gamesPanel);
    before.reserve(before_count);
    for (uint32_t i = 0; i < before_count; ++i) {
        before.push_back(lv_obj_get_child(gamesPanel, static_cast<int32_t>(i)));
    }

    switch (game) {
        case ACTIVE_GAME_GREEN_EYE:
            CreateGreenEyeUI();
            break;
        case ACTIVE_GAME_CHECKER:
            CreateCheckerUI();
            break;
        case ACTIVE_GAME_QUICK_TAP:
            CreateQuickTapUI();
            break;
        case ACTIVE_GAME_SNAKE:
            CreateSnakeUI();
            break;
        case ACTIVE_GAME_TILT_MAZE:
            CreateTiltMazeUI();
            break;
        case ACTIVE_GAME_TRAFFIC_RUNNER:
            CreateTrafficRunnerUI();
            break;
        default:
            return false;
    }

    const uint32_t after_count = lv_obj_get_child_count(gamesPanel);
    for (uint32_t i = 0; i < after_count; ++i) {
        lv_obj_t* child = lv_obj_get_child(gamesPanel, static_cast<int32_t>(i));
        if (std::find(before.begin(), before.end(), child) == before.end()) {
            loadedGameRoots.push_back(child);
        }
    }
    loadedGameUi = game;
    ESP_LOGI(TAG, "Game UI loaded: game %d, %u root objects", static_cast<int>(game),
             static_cast<unsigned>(loadedGameRoots.size()));
    LogInternalHeap("Game UI loaded");
    return true;
}

void SpinGamesRing(void* obj, int32_t value) {
    lv_arc_set_rotation(static_cast<lv_obj_t*>(obj), static_cast<uint32_t>(value));
}

// Caller holds the display lock.
void ShowGameLoadingLocked() {
    for (lv_obj_t* obj : {gamesEmblem, gamesAction, gamesStatus, gamesPrevBtn, gamesNextBtn}) {
        if (obj != nullptr) {
            lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (gamesLoadingLabel != nullptr) {
        lv_obj_remove_flag(gamesLoadingLabel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(gamesLoadingLabel);
    }
    if (gamesRing != nullptr) {
        // The carousel ring, in the selected game's colour, turning.
        lv_arc_set_angles(gamesRing, 0, 70);
        lv_obj_set_style_arc_color(gamesRing, lv_color_hex(gamesActionColor), LV_PART_INDICATOR);
        lv_obj_remove_flag(gamesRing, LV_OBJ_FLAG_HIDDEN);
        lv_anim_t anim;
        lv_anim_init(&anim);
        lv_anim_set_var(&anim, gamesRing);
        lv_anim_set_exec_cb(&anim, SpinGamesRing);
        lv_anim_set_values(&anim, 0, 359);
        lv_anim_set_duration(&anim, kGameLoadSpinMs);
        lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
        lv_anim_start(&anim);
    }
}

// Caller holds the display lock. Leaves the carousel chrome hidden: either the
// game's own screens take over next, or the caller runs UpdateGamesUI().
void HideGameLoadingLocked() {
    if (gamesRing != nullptr) {
        lv_anim_delete(gamesRing, SpinGamesRing);
        lv_arc_set_rotation(gamesRing, 0);
    }
    if (gamesLoadingLabel != nullptr) {
        lv_obj_add_flag(gamesLoadingLabel, LV_OBJ_FLAG_HIDDEN);
    }
}

// Caller holds the display lock.
void CancelGameLoadLocked() {
    if (gameLoadTimer != nullptr) {
        lv_timer_delete(gameLoadTimer);
        gameLoadTimer = nullptr;
    }
    if (gameLoadPending == ACTIVE_GAME_NONE) {
        return;
    }
    gameLoadPending = ACTIVE_GAME_NONE;
    HideGameLoadingLocked();
}

bool IsGameLoadPending() {
    return gameLoadPending != ACTIVE_GAME_NONE;
}

// Runs on the application task, never inside lv_timer_handler: the builders
// and Start*() take the display lock themselves.
void FinishGameLoad() {
    const ActiveGameType game = gameLoadPending;
    if (game == ACTIVE_GAME_NONE) {
        return;   // cancelled while queued
    }
    gameLoadPending = ACTIVE_GAME_NONE;

    if (currentState != MENU_GAMES_OPEN) {
        DisplayLockGuard lock(displayHandle);
        HideGameLoadingLocked();
        return;
    }

    {
        DisplayLockGuard lock(displayHandle);
        HideGameLoadingLocked();
        LoadGameUi(game);
    }
    MarkMenuActivity();

    switch (game) {
        case ACTIVE_GAME_GREEN_EYE:
            StartGreenEye();
            break;
        case ACTIVE_GAME_CHECKER:
            StartChecker3x3();
            break;
        case ACTIVE_GAME_QUICK_TAP:
            StartQuickTap();
            break;
        case ACTIVE_GAME_SNAKE:
            StartSnake();
            break;
        case ACTIVE_GAME_TILT_MAZE:
            StartTiltMaze();
            break;
        case ACTIVE_GAME_TRAFFIC_RUNNER:
            StartTrafficRunner();
            break;
        default:
            break;
    }

    // A start that refused (a canvas allocation failed) leaves us on the
    // carousel with screens nobody will use -- give the memory straight back.
    if (currentState != MENU_GAME_ACTIVE) {
        DisplayLockGuard lock(displayHandle);
        UnloadGameUi();
        UpdateGamesUI();
    }
}

void GameLoadTimerCb(lv_timer_t* timer) {
    // lv_timer_handler holds the display lock here, so hand the build to the
    // application task rather than building (and re-locking) in place.
    lv_timer_delete(timer);
    gameLoadTimer = nullptr;
    Application::GetInstance().Schedule([]() { FinishGameLoad(); });
}

// Called from the carousel, without the display lock.
void BeginGameLoad(ActiveGameType game) {
    if (currentState != MENU_GAMES_OPEN || gameLoadPending != ACTIVE_GAME_NONE) {
        return;
    }
    MarkMenuActivity();
    gameLoadPending = game;
    DisplayLockGuard lock(displayHandle);
    ShowGameLoadingLocked();
    gameLoadTimer = lv_timer_create(GameLoadTimerCb, kGameLoadPaintMs, nullptr);
}

void Begin(Display* display) {
    displayHandle = display;
    MarkMenuActivity();
    CheckerGame::Config checker_cfg;
    checker_cfg.take_best_move_chance_pct = 45;
    checker_cfg.block_player_chance_pct = 72;
    checker_cfg.take_center_chance_pct = 65;
    checker_cfg.take_corner_chance_pct = 75;
    CheckerGame::Configure(checker_cfg);
    EnsureTransientTimerCreated();
    ESP_LOGI(TAG, "Initializing menu system...");
    CreateCircularPanel();
    CreateMenuRoller();
    FortuneSystem::Begin(displayHandle);
    CreateCarePanel();
    CreateConnectPanel();
    CreateKeyboardPanel();
    CreateSettingsPanel();
    CreateNotesPanel();
    CreateRemindersPanel();
    CreateVolumePanel();
    CreateStatsPanel();
    CreateGamesPanel();
    CreateLevelPanel();
    PomodoroTimer::Begin();
    CreatePomodoroPanel();
    ESP_LOGI(TAG, "Menu system ready");
}

// Re-resolves every menu/care icon against whatever is currently mapped in
// the assets partition. Call this after a successful Assets::Download() +
// Apply() -- CreateMenuRoller()/CreateCarePanel() already resolved every icon
// once at boot into a raw pointer via ResolvePersistentAssetImage(), and nothing
// re-fetches it afterwards on its own. A download that lands *after* that
// first resolve (the assets-refresh poll in application.cc runs for the whole
// session, not just at boot) re-maps the partition to a new location/layout,
// so those boot-time pointers go stale -- not a crash (the fix for that is
// partition_valid() gating in Open()), but every icon silently pointing at
// wrong/garbage bytes until the icon is re-resolved. Reproduced on hardware
// 2026-09-10 across 3 devices: "OTA + first assets load -> every menu icon
// gone; reboot + re-load -> icons fine" -- fine on reboot only because that
// second load starts from a stale-pointer-free boot.
void RefreshIcons() {
    DisplayLockGuard lock(displayHandle);
    for (int i = 0; i < MENU_ITEM_COUNT; ++i) {
        const LvglImage* icon = ResolvePersistentAssetImage(menuItemIconFiles[i], menuItemIcons[i]);
        menuItemIsIcon[i] = (icon != nullptr);
        if (icon == nullptr) {
            ESP_LOGW(TAG, "No menu image for item %d ('%s') after assets refresh, falling back to text",
                     i, menuItemLabelTexts[i]);
        }
    }
    for (int i = 0; i < CARE_ITEM_COUNT; ++i) {
        const LvglImage* icon = ResolvePersistentAssetImage(careItemIconFiles[i], careItemIcons[i]);
        careItemIsIcon[i] = (icon != nullptr);
        if (icon == nullptr) {
            ESP_LOGW(TAG, "No care image for item %d ('%s') after assets refresh, falling back to text",
                     i, careItemLabelTexts[i]);
        }
    }
    // Force whichever hero image is currently on screen to pick up the fresh
    // pointer immediately, instead of waiting for the next selection change.
    UpdateMenuItemStyles();
    UpdateCareItemStyles();
}

void TriggerLevelUpAnimation(int level) {
    TriggerLevelUpAnimationImpl(level);
}

void Open() {
    if (currentState == MENU_OPEN) {
        return;
    }

    // Every menu/care icon was resolved once at boot into a raw pointer inside
    // the assets partition's mmap (ResolvePersistentAssetImage, not re-fetched
    // per draw). Assets::Download() unmaps that partition before it writes a
    // single byte, and leaves it unmapped for good if the download then fails
    // -- partition_valid() tracks exactly that window. Showing the menu while
    // it's false means LVGL redraws a dangling pointer: a Cache error / MMU
    // fault, reproduced on hardware 2026-09-10. No-op instead; the eyes screen
    // stays up and the tap is simply not consumed.
    if (!Assets::GetInstance().partition_valid()) {
        ESP_LOGW(TAG, "Assets partition unavailable (download in progress or failed) -- not opening menu");
        return;
    }

    if (FortuneSystem::IsOpen()) {
        FortuneSystem::Close(false);
    }

    DisplayLockGuard lock(displayHandle);
    HideAllPanels();
    ShowPanel(menuPanel);
    SetMenuState(MENU_OPEN);
    selectedItem = MENU_CARE;
    connectView = CONNECT_VIEW_METHODS;
    ScrollMenuToIndex(0, LV_ANIM_OFF);
    ApplyConnectView();
    MarkMenuActivity();
}

void Close() {
    if (currentState == MENU_CLOSED) {
        return;
    }

    // Quick Tap runs a 33ms LVGL timer. Every other close path funnels through
    // here, so if it is not stopped here it outlives the menu and keeps waking
    // the LVGL task at 30Hz for the rest of the session.
    bool closing_quick_tap = false;
    // Snake owns a 33ms lv_timer for exactly the same reason, and Close() is
    // the funnel for every close path here too.
    bool closing_snake = false;
    bool closing_tilt_maze = false;
    bool closing_traffic_runner = false;
    // MẮT XANH owns a 33ms lv_timer too.
    bool closing_green_eye = false;
    // Same trap as Quick Tap: this screen owns a 200ms lv_timer, and Close() is
    // the funnel for every close path. With no pause, leaving also discards a
    // running focus block -- there is no background tick to keep it alive.
    const bool closing_pomodoro = (currentState == MENU_POMODORO_OPEN);

    if (FortuneSystem::IsOpen()) {
        FortuneSystem::Close(false);
    }

    if (currentState == MENU_GAME_ACTIVE) {
        if (activeGame == ACTIVE_GAME_GREEN_EYE) {
            closing_green_eye = true;   // paid and stopped under the lock below
        } else if (activeGame == ACTIVE_GAME_CHECKER) {
            CheckerGame::Stop();
        } else if (activeGame == ACTIVE_GAME_QUICK_TAP) {
            QuickTapGame::Stop();
            closing_quick_tap = true;
        } else if (activeGame == ACTIVE_GAME_SNAKE) {
            SnakeGame::Stop();
            closing_snake = true;
        } else if (activeGame == ACTIVE_GAME_TILT_MAZE) {
            TiltMazeGame::Stop();
            closing_tilt_maze = true;
        } else if (activeGame == ACTIVE_GAME_TRAFFIC_RUNNER) {
            TrafficRunnerGame::Stop();
            closing_traffic_runner = true;
        }
        activeGame = ACTIVE_GAME_NONE;
    }

    if (currentState == MENU_VOLUME_OPEN) {
        CommitVolumeIfPending();
    }

    DisplayLockGuard lock(displayHandle);
    if (auto* eye_display = GetEyeDisplay(); eye_display != nullptr) {
        eye_display->CancelBathing();
    }
    StopTransientAnimationLocked();
    if (closing_quick_tap) {
        StopQuickTapTimer();
        HideQuickTapAll();
        quickTapScreen = QuickTapScreen::kSetup;
    }
    if (closing_green_eye) {
        // The menu closing under a round still pays for the greens found.
        PayGreenEyeReward();
        GreenEyeGame::Stop();
        StopGreenEyeTimer();
        HideGreenEyeAll();
        greenEyeScreen = GreenEyeScreen::kSetup;
    }
    if (closing_snake) {
        StopSnakeTimer();
        HideSnakeAll();
        snakeScreen = SnakeScreen::kSetup;
    }
    if (closing_tilt_maze) {
        StopTiltMazeTimer();
        HideTiltMazeAll();
    }
    if (closing_traffic_runner) {
        StopTrafficRunnerTimer();
        HideTrafficRunnerAll();
    }
    // A game half-way through loading, or one that was on screen: either way
    // its screens must not outlive the menu.
    CancelGameLoadLocked();
    UnloadGameUi();
    if (closing_pomodoro) {
        StopPomodoroTimer();
        if (PomodoroTimer::GetPhase() == PomodoroTimer::Phase::kFocus) {
            ESP_LOGI(TAG, "Menu closed during a focus block -- voided");
        }
        PomodoroTimer::Stop();
        pomodoroScreen = PomodoroScreen::kSetup;
        pomodoroBannerIsVoid = false;
    }
    HideAllPanels();
    SetMenuState(MENU_CLOSED);
    statsOpenedFromCare = false;
    levelOpenedFromCare = false;
    connectView = CONNECT_VIEW_METHODS;
}

bool IsAnyOpen() {
    return currentState != MENU_CLOSED;
}

bool IsCelebrationActive() {
    return transientAnimationActive;
}

bool IsOpen() {
    return currentState == MENU_OPEN;
}

MenuState GetState() {
    return currentState;
}

ScreenManager::ScreenId ActiveScreen() {
    using ScreenId = ScreenManager::ScreenId;
    switch (currentState) {
        case MENU_OPEN:                  return ScreenId::Menu;
        case MENU_CARE_OPEN:             return ScreenId::Care;
        case MENU_CONNECT_OPEN:          return ScreenId::Connect;
        case MENU_KEYBOARD_OPEN:         return ScreenId::Keyboard;
        case MENU_REMINDERS_OPEN:        return ScreenId::Reminders;
        case MENU_REMINDER_DETAIL_OPEN:  return ScreenId::ReminderDetail;
        case MENU_STATS_OPEN:            return ScreenId::Stats;
        case MENU_GAMES_OPEN:            return ScreenId::GamesList;
        case MENU_FORTUNE_OPEN:          return ScreenId::Fortune;
        case MENU_LEVEL_OPEN:            return ScreenId::Level;
        case MENU_NOTES_OPEN:            return ScreenId::Notes;
        case MENU_NOTE_DETAIL_OPEN:      return ScreenId::NoteDetail;
        case MENU_SETTINGS_OPEN:         return ScreenId::Settings;
        case MENU_VOLUME_OPEN:           return ScreenId::Volume;
        case MENU_POMODORO_OPEN:         return ScreenId::Pomodoro;
        case MENU_GAME_ACTIVE:
            if (activeGame == ACTIVE_GAME_GREEN_EYE) {
                return ScreenId::GreenEyeGame;
            }
            if (activeGame == ACTIVE_GAME_QUICK_TAP) {
                return ScreenId::QuickTapGame;
            }
            if (activeGame == ACTIVE_GAME_SNAKE) {
                return ScreenId::SnakeGame;
            }
            if (activeGame == ACTIVE_GAME_TILT_MAZE) {
                return ScreenId::TiltMazeGame;
            }
            if (activeGame == ACTIVE_GAME_TRAFFIC_RUNNER) {
                return ScreenId::TrafficRunnerGame;
            }
            return ScreenId::CheckerGame;
        case MENU_CLOSED:
            break;
    }
    // Menu closed -- caller decides (eyes, feeding, bathing, ...).
    return ScreenId::Main;
}

MenuItem GetSelected() {
    return selectedItem;
}

void SelectNext() {
    if (currentState != MENU_OPEN) {
        return;
    }
    MarkMenuActivity();
    int current = static_cast<int>(selectedItem);
    if (current < MENU_ITEM_COUNT - 1) {
        DisplayLockGuard lock(displayHandle);
        ScrollMenuToIndex(static_cast<uint8_t>(current + 1), LV_ANIM_ON);
    }
}

void SelectPrev() {
    if (currentState != MENU_OPEN) {
        return;
    }
    MarkMenuActivity();
    int current = static_cast<int>(selectedItem);
    if (current > 0) {
        DisplayLockGuard lock(displayHandle);
        ScrollMenuToIndex(static_cast<uint8_t>(current - 1), LV_ANIM_ON);
    }
}

void ActivateSelected() {
    if (currentState != MENU_OPEN) {
        return;
    }

    MarkMenuActivity();

    switch (selectedItem) {
        case MENU_CARE: {
            DisplayLockGuard lock(displayHandle);
            HidePanel(menuPanel);
            ShowPanel(carePanel);
            SetMenuState(MENU_CARE_OPEN);
            ScrollCareToIndex(static_cast<uint8_t>(selectedCareItem), LV_ANIM_OFF);
            break;
        }
        case MENU_CONNECT: {
            DisplayLockGuard lock(displayHandle);
            HidePanel(menuPanel);
            ShowPanel(connectPanel);
            SetMenuState(MENU_CONNECT_OPEN);
            connectView = CONNECT_VIEW_METHODS;
            // Opens on the on-device method, matching the list order.
            selectedConnectItem = CONNECT_DEVICE;
            ApplyConnectView();
            ScrollConnectToIndex(static_cast<uint8_t>(selectedConnectItem), LV_ANIM_OFF);
            break;
        }
        case MENU_REMINDERS: {
            DisplayLockGuard lock(displayHandle);
            RebuildRemindersList();
            HidePanel(menuPanel);
            ShowPanel(remindersPanel);
            SetMenuState(MENU_REMINDERS_OPEN);
            ScrollRemindersToIndex(selectedReminderIndex, LV_ANIM_OFF);
            break;
        }
        case MENU_NOTES: {
            DisplayLockGuard lock(displayHandle);
            RebuildNotesList();
            HidePanel(menuPanel);
            ShowPanel(notesPanel);
            SetMenuState(MENU_NOTES_OPEN);
            ScrollNotesToIndex(selectedNoteIndex, LV_ANIM_OFF);
            break;
        }
        case MENU_POMODORO: {
            OpenPomodoro();
            break;
        }
        case MENU_SETTINGS: {
            DisplayLockGuard lock(displayHandle);
            HidePanel(menuPanel);
            ShowPanel(settingsPanel);
            SetMenuState(MENU_SETTINGS_OPEN);
            selectedSettingsItem = SETTINGS_VOLUME;
            UpdateSettingsItemStyles();
            break;
        }
        case MENU_FORTUNE: {
            OpenFortuneTeller();
            break;
        }
        case MENU_ITEM_COUNT:
            break;
    }
}

void NavigateNext() {
    if (currentState != MENU_CLOSED) {
        MarkMenuActivity();
    }
    if (IsGameLoadPending()) {
        return;
    }
    switch (currentState) {
        case MENU_OPEN:
            SelectNext();
            break;
        case MENU_CARE_OPEN:
            SelectCareNext();
            break;
        case MENU_CONNECT_OPEN:
            if (connectView == CONNECT_VIEW_METHODS) {
                if (selectedConnectItem < CONNECT_ITEM_COUNT - 1) {
                    DisplayLockGuard lock(displayHandle);
                    ScrollConnectToIndex(static_cast<uint8_t>(selectedConnectItem + 1), LV_ANIM_ON);
                }
            } else if (!connectWifiItems.empty() && selectedWifiIndex + 1 < connectWifiItems.size()) {
                DisplayLockGuard lock(displayHandle);
                ScrollWifiToIndex(selectedWifiIndex + 1, LV_ANIM_ON);
            }
            break;
        case MENU_STATS_OPEN:
            StatsNext();
            break;
        case MENU_VOLUME_OPEN:
            VolumeStep(true);
            break;
        case MENU_SETTINGS_OPEN:
            if (selectedSettingsItem < SETTINGS_ITEM_COUNT - 1) {
                selectedSettingsItem = static_cast<SettingsItem>(selectedSettingsItem + 1);
                DisplayLockGuard lock(displayHandle);
                UpdateSettingsItemStyles();
            }
            break;
        case MENU_GAMES_OPEN:
            selectedGame = static_cast<GameSelection>((selectedGame + 1) % GAME_SELECTION_COUNT);
            SetGamesMenuStatusForSelection();
            {
                DisplayLockGuard lock(displayHandle);
                UpdateGamesUI();
            }
            break;
        case MENU_POMODORO_OPEN:
            // Moves the selection on the setup screen; during a break it is the
            // skip. A focus block ignores it -- it cannot be skipped, only
            // voided, and that is the long press.
            if (pomodoroScreen == PomodoroScreen::kSetup) {
                pomodoroPreset = static_cast<PomodoroTimer::Preset>(
                    (static_cast<int>(pomodoroPreset) + 1) % kPomodoroPresetCount);
                DisplayLockGuard lock(displayHandle);
                UpdatePomodoroSetupUI();
            } else if (pomodoroScreen == PomodoroScreen::kRunning &&
                       PomodoroTimer::SkipBreak()) {
                DisplayLockGuard lock(displayHandle);
                UpdatePomodoroRunUI();
            }
            break;
        case MENU_REMINDERS_OPEN:
            SelectRemindersNext();
            break;
        case MENU_REMINDER_DETAIL_OPEN:
            RemindersDetailNext();
            break;
        case MENU_NOTES_OPEN:
            SelectNotesNext();
            break;
        case MENU_NOTE_DETAIL_OPEN:
            NotesDetailNext();
            break;
        default:
            break;
    }
}

void NavigatePrev() {
    if (currentState != MENU_CLOSED) {
        MarkMenuActivity();
    }
    if (IsGameLoadPending()) {
        return;
    }
    switch (currentState) {
        case MENU_OPEN:
            SelectPrev();
            break;
        case MENU_CARE_OPEN:
            SelectCarePrev();
            break;
        case MENU_CONNECT_OPEN:
            if (connectView == CONNECT_VIEW_METHODS) {
                if (selectedConnectItem > 0) {
                    DisplayLockGuard lock(displayHandle);
                    ScrollConnectToIndex(static_cast<uint8_t>(selectedConnectItem - 1), LV_ANIM_ON);
                }
            } else if (!connectWifiItems.empty() && selectedWifiIndex > 0) {
                DisplayLockGuard lock(displayHandle);
                ScrollWifiToIndex(selectedWifiIndex - 1, LV_ANIM_ON);
            }
            break;
        case MENU_STATS_OPEN:
            StatsPrev();
            break;
        case MENU_VOLUME_OPEN:
            VolumeStep(false);
            break;
        case MENU_SETTINGS_OPEN:
            if (selectedSettingsItem > 0) {
                selectedSettingsItem = static_cast<SettingsItem>(selectedSettingsItem - 1);
                DisplayLockGuard lock(displayHandle);
                UpdateSettingsItemStyles();
            }
            break;
        case MENU_GAMES_OPEN:
            selectedGame = static_cast<GameSelection>((selectedGame + GAME_SELECTION_COUNT - 1) % GAME_SELECTION_COUNT);
            SetGamesMenuStatusForSelection();
            {
                DisplayLockGuard lock(displayHandle);
                UpdateGamesUI();
            }
            break;
        case MENU_POMODORO_OPEN:
            if (pomodoroScreen == PomodoroScreen::kSetup) {
                pomodoroPreset = static_cast<PomodoroTimer::Preset>(
                    (static_cast<int>(pomodoroPreset) + kPomodoroPresetCount - 1) %
                    kPomodoroPresetCount);
                DisplayLockGuard lock(displayHandle);
                UpdatePomodoroSetupUI();
            } else if (pomodoroScreen == PomodoroScreen::kRunning &&
                       PomodoroTimer::SkipBreak()) {
                DisplayLockGuard lock(displayHandle);
                UpdatePomodoroRunUI();
            }
            break;
        case MENU_REMINDERS_OPEN:
            SelectRemindersPrev();
            break;
        case MENU_REMINDER_DETAIL_OPEN:
            RemindersDetailPrev();
            break;
        case MENU_NOTES_OPEN:
            SelectNotesPrev();
            break;
        case MENU_NOTE_DETAIL_OPEN:
            NotesDetailPrev();
            break;
        default:
            break;
    }
}

void ActivateCurrent() {
    if (currentState != MENU_CLOSED) {
        MarkMenuActivity();
    }
    switch (currentState) {
        case MENU_OPEN:
            ActivateSelected();
            break;
        case MENU_CARE_OPEN:
            ActivateCareSelected();
            break;
        case MENU_CONNECT_OPEN:
            HandleConnectTap(0, 0);
            break;
        case MENU_KEYBOARD_OPEN:
            break;
        case MENU_STATS_OPEN:
            ActivateCurrentOption();
            break;
        case MENU_GAMES_OPEN:
            if (IsGameLoadPending()) {
                break;   // one load at a time; the spinner is already up
            }
            switch (selectedGame) {
                case GAME_SELECTION_GREEN_EYE:
                    BeginGameLoad(ACTIVE_GAME_GREEN_EYE);
                    break;
                case GAME_SELECTION_CHECKER:
                    BeginGameLoad(ACTIVE_GAME_CHECKER);
                    break;
                case GAME_SELECTION_QUICK_TAP:
                    BeginGameLoad(ACTIVE_GAME_QUICK_TAP);
                    break;
                case GAME_SELECTION_SNAKE:
                    BeginGameLoad(ACTIVE_GAME_SNAKE);
                    break;
                case GAME_SELECTION_TILT_MAZE:
                    BeginGameLoad(ACTIVE_GAME_TILT_MAZE);
                    break;
                case GAME_SELECTION_TRAFFIC_RUNNER:
                    BeginGameLoad(ACTIVE_GAME_TRAFFIC_RUNNER);
                    break;
                case GAME_SELECTION_COUNT:
                    break;
            }
            break;
        case MENU_POMODORO_OPEN:
            HandlePomodoroTap(120, 120);
            break;
        case MENU_LEVEL_OPEN:
            CloseLevelToMenu();
            break;
        case MENU_SETTINGS_OPEN:
            switch (selectedSettingsItem) {
                case SETTINGS_VOLUME:
                    OpenVolumePanel();
                    break;
                case SETTINGS_ITEM_COUNT:
                    break;
            }
            break;
        case MENU_FORTUNE_OPEN:
            FortuneSystem::HandleTap(0, 0);
            break;
        case MENU_VOLUME_OPEN:
            VolumeStep(true);
            break;
        case MENU_REMINDERS_OPEN:
            ActivateRemindersSelected();
            break;
        case MENU_REMINDER_DETAIL_OPEN:
            CloseReminderDetailToReminders();
            break;
        case MENU_NOTES_OPEN:
            ActivateNotesSelected();
            break;
        case MENU_NOTE_DETAIL_OPEN:
            CloseNoteDetailToNotes();
            break;
        default:
            break;
    }
}

void SelectCareNext() {
    if (currentState != MENU_CARE_OPEN) {
        return;
    }
    MarkMenuActivity();
    int current = static_cast<int>(selectedCareItem);
    if (current < CARE_ITEM_COUNT - 1) {
        DisplayLockGuard lock(displayHandle);
        ScrollCareToIndex(static_cast<uint8_t>(current + 1), LV_ANIM_ON);
    }
}

void SelectCarePrev() {
    if (currentState != MENU_CARE_OPEN) {
        return;
    }
    MarkMenuActivity();
    int current = static_cast<int>(selectedCareItem);
    if (current > 0) {
        DisplayLockGuard lock(displayHandle);
        ScrollCareToIndex(static_cast<uint8_t>(current - 1), LV_ANIM_ON);
    }
}

void ActivateCareSelected() {
    if (currentState != MENU_CARE_OPEN) {
        return;
    }

    MarkMenuActivity();

    switch (selectedCareItem) {
        case CARE_FEED:
            TriggerFeedingAnimation();
            break;
        case CARE_PLAY:
            OpenGamesMenu();
            return;
        case CARE_CLEAN:
            TriggerBathAnimation();
            break;
        case CARE_SLEEP:
            TryEnterSleepMode();
            break;
        case CARE_STATS:
            ShowStats();
            return;
        case CARE_LEVEL:
            levelOpenedFromCare = true;
            if (displayHandle != nullptr) {
                DisplayLockGuard lock(displayHandle);
                HidePanel(carePanel);
                ShowPanel(levelPanel);
                SetMenuState(MENU_LEVEL_OPEN);
                UpdateLevelUI();
            }
            return;
        case CARE_ITEM_COUNT:
            break;
    }
}

void CloseCareToMenu() {
    if (currentState != MENU_CARE_OPEN) {
        return;
    }

    DisplayLockGuard lock(displayHandle);
    HidePanel(carePanel);
    ShowPanel(menuPanel);
    SetMenuState(MENU_OPEN);
    selectedItem = MENU_CARE;
    ScrollMenuToIndex(static_cast<uint8_t>(selectedItem), LV_ANIM_OFF);
}

bool HandleConnectTap(uint16_t x, uint16_t y) {
    if (currentState != MENU_CONNECT_OPEN) {
        return false;
    }

    MarkMenuActivity();

    if (connectView == CONNECT_VIEW_METHODS) {
        if (x != 0 || y != 0) {
            if (IsPointInside(connectItems[CONNECT_PHONE], x, y)) {
                selectedConnectItem = CONNECT_PHONE;
                DisplayLockGuard lock(displayHandle);
                ScrollConnectToIndex(static_cast<uint8_t>(selectedConnectItem), LV_ANIM_ON);
            } else if (IsPointInside(connectItems[CONNECT_DEVICE], x, y)) {
                selectedConnectItem = CONNECT_DEVICE;
                DisplayLockGuard lock(displayHandle);
                ScrollConnectToIndex(static_cast<uint8_t>(selectedConnectItem), LV_ANIM_ON);
            } else {
                return false;
            }
        }

        switch (selectedConnectItem) {
            case CONNECT_PHONE: {
                auto* wifi_board = dynamic_cast<WifiBoard*>(&Board::GetInstance());
                if (wifi_board != nullptr) {
                    wifi_board->EnterWifiConfigMode();
                    DisplayLockGuard lock(displayHandle);
                    connectView = CONNECT_VIEW_PHONE_HELP;
                    SetConnectHint("Kết nối điện thoại vào WiFi Bubu,\nsau đó mở 192.168.4.1\nvà chọn mạng WiFi của bạn.");
                    ApplyConnectView();
                } else if (displayHandle != nullptr) {
                    displayHandle->ShowNotification("WiFi config unsupported on this board");
                }
                return true;
            }
            case CONNECT_DEVICE: {
                DisplayLockGuard lock(displayHandle);
                connectView = CONNECT_VIEW_WIFI_LIST;
                ApplyConnectView();
                selectedWifiIndex = 0;
                lastWifiScanVersion = 0;
                lastWifiScanActive = true;
                WifiConnectService::GetInstance().StartScan();
                RebuildWifiList();
                return true;
            }
            case CONNECT_ITEM_COUNT:
                return false;
        }
    }

    if (connectWifiItems.empty()) {
        return false;
    }

    size_t tapped_index = connectWifiItems.size();
    if (x == 0 && y == 0) {
        tapped_index = selectedWifiIndex;
    } else {
        for (size_t i = 0; i < connectWifiItems.size(); ++i) {
            if (IsPointInside(connectWifiItems[i], x, y)) {
                tapped_index = i;
                break;
            }
        }
        if (tapped_index == connectWifiItems.size()) {
            return false;
        }
    }

    if (WifiConnectService::GetInstance().IsScanning() || WifiConnectService::GetInstance().GetScanResults().empty()) {
        return false;
    }

    {
        DisplayLockGuard lock(displayHandle);
        ScrollWifiToIndex(tapped_index, LV_ANIM_ON);
        selectedWifiSsid = WifiConnectService::GetInstance().GetScanResults()[tapped_index].ssid;
        lv_obj_add_flag(connectPanel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(keyboardPanel, LV_OBJ_FLAG_HIDDEN);
        SetMenuState(MENU_KEYBOARD_OPEN);
        lv_label_set_text(keyboardSsid, selectedWifiSsid.c_str());
    }

    std::string existing_password;
    for (const auto& item : SsidManager::GetInstance().GetSsidList()) {
        if (item.ssid == selectedWifiSsid) {
            existing_password = item.password;
            break;
        }
    }
    ResetKeyboardState(existing_password.c_str());
    UpdateKeyboardValue();
    return true;
}

bool HandleKeyboardTap(uint16_t x, uint16_t y) {
    if (currentState != MENU_KEYBOARD_OPEN) {
        return false;
    }

    MarkMenuActivity();

    if (displayHandle == nullptr) {
        return false;
    }

    size_t tapped_index = keyboardButtons.size();
    {
        DisplayLockGuard lock(displayHandle);
        for (size_t i = 0; i < keyboardButtons.size(); ++i) {
            if (!IsPointInside(keyboardButtons[i], x, y)) {
                continue;
            }
            tapped_index = i;
            break;
        }
    }

    if (tapped_index == keyboardButtons.size()) {
        return false;
    }

    if (tapped_index == 11) {
        HandleKeyboardBackspace();
        return true;
    }
    if (tapped_index == 9) {
        keyboardCaps = !keyboardCaps;
        UpdateKeyboardCapsButtonStyle();
        return true;
    }
    if (tapped_index == 12) {
        if (!selectedWifiSsid.empty()) {
            std::string password(keyboardText, keyboardLen);
            WifiConnectService::GetInstance().ConnectTo(selectedWifiSsid, password);
            Close();
            if (displayHandle != nullptr) {
                displayHandle->ShowNotification("Đang kết nối WiFi", 3000);
            }
        }
        return true;
    }
    const char* label = keyboardButtonTexts[tapped_index];
    if (label != nullptr && label[0] != '\0') {
        HandleT9Key(label[0]);
        return true;
    }

    return false;
}

void CloseConnectToMenu() {
    if (currentState != MENU_CONNECT_OPEN) {
        return;
    }

    DisplayLockGuard lock(displayHandle);
    HidePanel(connectPanel);
    ShowPanel(menuPanel);
    SetMenuState(MENU_OPEN);
    connectView = CONNECT_VIEW_METHODS;
    selectedItem = MENU_CONNECT;
    ApplyConnectView();
    ScrollMenuToIndex(static_cast<uint8_t>(selectedItem), LV_ANIM_OFF);
}

void CloseKeyboardToConnect() {
    if (currentState != MENU_KEYBOARD_OPEN) {
        return;
    }

    DisplayLockGuard lock(displayHandle);
    HidePanel(keyboardPanel);
    ShowPanel(connectPanel);
    SetMenuState(MENU_CONNECT_OPEN);
    connectView = CONNECT_VIEW_WIFI_LIST;
    ApplyConnectView();
}

void SelectRemindersNext() {
    if (currentState != MENU_REMINDERS_OPEN || remindersItems.empty()) {
        return;
    }
    MarkMenuActivity();
    size_t next = selectedReminderIndex + 1;
    if (next >= remindersItems.size()) {
        next = remindersItems.size() - 1;
    }
    DisplayLockGuard lock(displayHandle);
    ScrollRemindersToIndex(next, LV_ANIM_ON);
}

void SelectRemindersPrev() {
    if (currentState != MENU_REMINDERS_OPEN || remindersItems.empty()) {
        return;
    }
    MarkMenuActivity();
    size_t next = selectedReminderIndex == 0 ? 0 : selectedReminderIndex - 1;
    DisplayLockGuard lock(displayHandle);
    ScrollRemindersToIndex(next, LV_ANIM_ON);
}

void ActivateRemindersSelected() {
    if (currentState != MENU_REMINDERS_OPEN) {
        return;
    }
    MarkMenuActivity();
    DisplayLockGuard lock(displayHandle);
    RebuildRemindersList();
    HidePanel(remindersPanel);
    ShowPanel(reminderDetailPanel);
    SetMenuState(MENU_REMINDER_DETAIL_OPEN);
    ShowReminderDetailForCurrentSelection();
}

void CloseRemindersToMenu() {
    if (currentState != MENU_REMINDERS_OPEN) {
        return;
    }

    DisplayLockGuard lock(displayHandle);
    HidePanel(remindersPanel);
    ShowPanel(menuPanel);
    SetMenuState(MENU_OPEN);
    selectedItem = MENU_REMINDERS;
    ScrollMenuToIndex(static_cast<uint8_t>(selectedItem), LV_ANIM_OFF);
}

void CloseReminderDetailToReminders() {
    if (currentState != MENU_REMINDER_DETAIL_OPEN) {
        return;
    }

    DisplayLockGuard lock(displayHandle);
    RebuildRemindersList();
    HidePanel(reminderDetailPanel);
    ShowPanel(remindersPanel);
    SetMenuState(MENU_REMINDERS_OPEN);
    ScrollRemindersToIndex(selectedReminderIndex, LV_ANIM_OFF);
}

void RemindersDetailNext() {
    if (currentState != MENU_REMINDER_DETAIL_OPEN || remindersEntries.empty()) {
        return;
    }
    MarkMenuActivity();
    selectedReminderIndex = (selectedReminderIndex + 1) % remindersEntries.size();
    DisplayLockGuard lock(displayHandle);
    ShowReminderDetailForCurrentSelection();
}

void RemindersDetailPrev() {
    if (currentState != MENU_REMINDER_DETAIL_OPEN || remindersEntries.empty()) {
        return;
    }
    MarkMenuActivity();
    selectedReminderIndex = (selectedReminderIndex + remindersEntries.size() - 1) % remindersEntries.size();
    DisplayLockGuard lock(displayHandle);
    ShowReminderDetailForCurrentSelection();
}

void ShowStats() {
    if (currentState != MENU_CARE_OPEN && currentState != MENU_OPEN) {
        return;
    }

    MarkMenuActivity();
    statsOpenedFromCare = (currentState == MENU_CARE_OPEN);

    DisplayLockGuard lock(displayHandle);
    if (statsOpenedFromCare) {
        HidePanel(carePanel);
    } else {
        HidePanel(menuPanel);
    }
    ShowPanel(statsPanel);
    SetMenuState(MENU_STATS_OPEN);
    UpdateStatsUI();
}

void CloseStatsToMenu() {
    if (currentState != MENU_STATS_OPEN) {
        return;
    }

    DisplayLockGuard lock(displayHandle);
    HidePanel(statsPanel);
    if (statsOpenedFromCare) {
        ShowPanel(carePanel);
        SetMenuState(MENU_CARE_OPEN);
        selectedCareItem = CARE_STATS;
        ScrollCareToIndex(static_cast<uint8_t>(selectedCareItem), LV_ANIM_OFF);
    } else {
        ShowPanel(menuPanel);
        SetMenuState(MENU_OPEN);
        selectedItem = MENU_CARE;
        ScrollMenuToIndex(static_cast<uint8_t>(selectedItem), LV_ANIM_OFF);
    }
    statsOpenedFromCare = false;
}

void StatsNext() {
    if (currentState != MENU_STATS_OPEN) {
        return;
    }
    MarkMenuActivity();
    statIndex = (statIndex + 1) % STAT_COUNT;
    DisplayLockGuard lock(displayHandle);
    UpdateStatsUI();
}

void StatsPrev() {
    if (currentState != MENU_STATS_OPEN) {
        return;
    }
    MarkMenuActivity();
    statIndex = (statIndex + STAT_COUNT - 1) % STAT_COUNT;
    DisplayLockGuard lock(displayHandle);
    UpdateStatsUI();
}

void OpenOptionsForCurrentStat() {}
void CloseOptionsToStats() {}

void ActivateCurrentOption() {
    if (currentState != MENU_STATS_OPEN) {
        return;
    }
    MarkMenuActivity();
    ApplyCurrentStatAction();
    DisplayLockGuard lock(displayHandle);
    UpdateStatsUI();
}

void OpenGamesMenu() {
    if (currentState != MENU_STATS_OPEN && currentState != MENU_CARE_OPEN) {
        return;
    }

    if (!IsGamesUnlocked()) {
        if (displayHandle != nullptr) {
            displayHandle->ShowNotification("Mở khóa GIẢI TRÍ ở Level 1");
        }
        return;
    }

    if (currentState == MENU_CARE_OPEN) {
        gamesOpenedFromCare = true;
        gamesReturnCareItem = selectedCareItem;
    } else {
        gamesOpenedFromCare = statsOpenedFromCare;
        gamesReturnCareItem = CARE_STATS;
    }
    activeGame = ACTIVE_GAME_NONE;
    SetGamesMenuStatusForSelection();

    DisplayLockGuard lock(displayHandle);
    if (currentState == MENU_CARE_OPEN) {
        HidePanel(carePanel);
    } else {
        HidePanel(statsPanel);
    }
    ShowPanel(gamesPanel);
    SetMenuState(MENU_GAMES_OPEN);
    UpdateGamesUI();
}

void CloseGamesToStats() {
    if (currentState != MENU_GAMES_OPEN) {
        return;
    }

    DisplayLockGuard lock(displayHandle);
    HidePanel(gamesPanel);
    if (gamesOpenedFromCare) {
        ShowPanel(carePanel);
        SetMenuState(MENU_CARE_OPEN);
        selectedCareItem = gamesReturnCareItem;
        ScrollCareToIndex(static_cast<uint8_t>(selectedCareItem), LV_ANIM_OFF);
    } else {
        ShowPanel(statsPanel);
        SetMenuState(MENU_STATS_OPEN);
        UpdateStatsUI();
    }
    gamesOpenedFromCare = false;
}

void StartGreenEye() {
    if (currentState != MENU_GAMES_OPEN) {
        return;
    }

    activeGame = ACTIVE_GAME_GREEN_EYE;
    gamesActionColor = GreenEyeGame::HueRgb(GreenEyeGame::Hue::kGreen);
    gamesStatusColor = COLOR_TEXT;
    greenEyeRewardPaid = true;   // nothing played yet
    // MẮT XANH draws its own panel; the eyes stay in their normal mode
    // underneath, paused by the panel screen policy.
    if (auto* eye_display = GetEyeDisplay(); eye_display != nullptr) {
        eye_display->CancelBathing();
    }

    DisplayLockGuard lock(displayHandle);
    ShowPanel(gamesPanel);
    SetMenuState(MENU_GAME_ACTIVE);
    ShowGreenEyeScreen(GreenEyeScreen::kSetup);
    StartGreenEyeTimer();
}

void StartQuickTap() {
    if (currentState != MENU_GAMES_OPEN) {
        return;
    }

    activeGame = ACTIVE_GAME_QUICK_TAP;
    quickTapNewRecord = false;
    gamesActionColor = kQuickTapRing;
    gamesStatusColor = COLOR_TEXT;
    // Quick Tap draws its own panel; the eyes stay in their normal mode
    // underneath it rather than being driven by the game.
    if (auto* eye_display = GetEyeDisplay(); eye_display != nullptr) {
        eye_display->CancelBathing();
    }

    DisplayLockGuard lock(displayHandle);
    ShowPanel(gamesPanel);
    SetMenuState(MENU_GAME_ACTIVE);
    ShowQuickTapScreen(QuickTapScreen::kSetup);
    StartQuickTapTimer();
}

void StartSnake() {
    if (currentState != MENU_GAMES_OPEN) {
        return;
    }

    // Only reachable if the PSRAM allocation in CreateSnakeUI() failed, which
    // has never been seen -- but an invisible board is worse than an honest no.
    if (snakeCanvas == nullptr || snakeCanvasBuf == nullptr) {
        if (displayHandle != nullptr) {
            displayHandle->ShowNotification("Lỗi trò chơi");
        }
        return;
    }

    activeGame = ACTIVE_GAME_SNAKE;
    snakeNewRecord = false;
    gamesActionColor = kSnakeAccent;
    gamesStatusColor = COLOR_TEXT;
    // Snake draws its own panel; the eyes stay in their normal mode underneath
    // rather than being driven by the game, same as Quick Tap.
    if (auto* eye_display = GetEyeDisplay(); eye_display != nullptr) {
        eye_display->CancelBathing();
    }

    DisplayLockGuard lock(displayHandle);
    ShowPanel(gamesPanel);
    SetMenuState(MENU_GAME_ACTIVE);
    ShowSnakeScreen(SnakeScreen::kSetup);
    StartSnakeTimer();
}

void StartTiltMaze() {
    if (currentState != MENU_GAMES_OPEN) {
        return;
    }
    if (mazeCanvas == nullptr || mazeCanvasBuf == nullptr) {
        if (displayHandle != nullptr) {
            displayHandle->ShowNotification("Lỗi trò chơi");
        }
        return;
    }

    activeGame = ACTIVE_GAME_TILT_MAZE;
    gamesActionColor = kMazeAccent;
    gamesStatusColor = COLOR_TEXT;
    if (auto* eye_display = GetEyeDisplay(); eye_display != nullptr) {
        eye_display->CancelBathing();
    }

    DisplayLockGuard lock(displayHandle);
    ShowPanel(gamesPanel);
    SetMenuState(MENU_GAME_ACTIVE);
    TiltMazeGame::StartLevel(TiltMazeGame::GetCurrentLevel(), lv_tick_get());
    mazeVisiblePhase = TiltMazeGame::Phase::kStopped;
    UpdateGamesUI();
    StartTiltMazeTimer();
}

void StartTrafficRunner() {
    if (currentState != MENU_GAMES_OPEN) {
        return;
    }
    if (runnerCanvas == nullptr || runnerCanvasBuf == nullptr) {
        if (displayHandle != nullptr) {
            displayHandle->ShowNotification("Lỗi trò chơi");
        }
        return;
    }

    activeGame = ACTIVE_GAME_TRAFFIC_RUNNER;
    gamesActionColor = kRunnerAccent;
    gamesStatusColor = COLOR_TEXT;
    if (auto* eye_display = GetEyeDisplay(); eye_display != nullptr) {
        eye_display->CancelBathing();
    }

    TrafficRunnerGame::Stop();
    DisplayLockGuard lock(displayHandle);
    ShowPanel(gamesPanel);
    SetMenuState(MENU_GAME_ACTIVE);
    runnerVisiblePhase = TrafficRunnerGame::Phase::kGameOver;
    UpdateGamesUI();
    StartTrafficRunnerTimer();
}

void OpenFortuneTeller() {
    if (currentState != MENU_OPEN) {
        return;
    }

    MarkMenuActivity();
    {
        DisplayLockGuard lock(displayHandle);
        HidePanel(menuPanel);
        SetMenuState(MENU_FORTUNE_OPEN);
    }

    FortuneSystem::Open(CloseFortuneToMenu);
    if (!FortuneSystem::IsOpen()) {
        FortuneSystem::Close(false);
        DisplayLockGuard lock(displayHandle);
        ShowPanel(menuPanel);
        SetMenuState(MENU_OPEN);
    }
}

void CloseFortuneToMenu() {
    if (currentState != MENU_FORTUNE_OPEN) {
        return;
    }

    MarkMenuActivity();
    DisplayLockGuard lock(displayHandle);
    ShowPanel(menuPanel);
    SetMenuState(MENU_OPEN);
    selectedItem = MENU_FORTUNE;
    ScrollMenuToIndex(static_cast<uint8_t>(selectedItem), LV_ANIM_OFF);
}

void StartChecker3x3() {
    if (currentState != MENU_GAMES_OPEN) {
        return;
    }

    activeGame = ACTIVE_GAME_CHECKER;
    checkerLastPlaced = -1;
    checkerRenderedCells.fill(CheckerGame::Cell::kEmpty);
    checkerWinPulsePlayed = false;
    checkerBubuThinking = false;
    checkerFinishHolding = false;
    gamesActionColor = 0xA7D8FF;
    gamesStatusColor = kCheckerPlayerMark;
    std::snprintf(gameStatusMsg, sizeof(gameStatusMsg), "%s", "LƯỢT BẠN");
    if (auto* eye_display = GetEyeDisplay(); eye_display != nullptr) {
        eye_display->CancelBathing();
    }

    DisplayLockGuard lock(displayHandle);
    ShowPanel(gamesPanel);
    SetMenuState(MENU_GAME_ACTIVE);
    ShowCheckerScreen(CheckerScreen::kTitle);
    UpdateGamesUI();
}

void HandleGameFinished() {
    if (currentState != MENU_GAME_ACTIVE && currentState != MENU_GAMES_OPEN) {
        return;
    }

    if (activeGame == ACTIVE_GAME_GREEN_EYE) {
        gamesActionColor = GreenEyeGame::HueRgb(GreenEyeGame::Hue::kGreen);
        gamesStatusColor = COLOR_TEXT;
        if (displayHandle != nullptr) {
            // Under the lock GreenEyeTimerCb runs under, so the tick cannot
            // finish the game -- and pay for it -- in between. Every way out
            // of a game comes through here, and a round left mid-way is still
            // paid for (a no-op if the scoreboard already paid).
            DisplayLockGuard teardown_lock(displayHandle);
            PayGreenEyeReward();
            GreenEyeGame::Stop();
            StopGreenEyeTimer();   // lv_timer_delete needs the LVGL lock
            HideGreenEyeAll();
        }
        greenEyeScreen = GreenEyeScreen::kSetup;
    } else if (activeGame == ACTIVE_GAME_CHECKER) {
        const CheckerGame::Result result = CheckerGame::GetResult();
        auto* eye_display = GetEyeDisplay();
        switch (result) {
            case CheckerGame::Result::kPlayerWin:
                ESP_LOGI(TAG, "Game reward: checker +%d mood", CareSystem::kGamesBoost);
                CareSystem::AddMood(CareSystem::kGamesBoost);
                gamesStatusColor = COLOR_MINT;
                gamesActionColor = 0xA7D8FF;
                std::snprintf(gameStatusMsg, sizeof(gameStatusMsg), "Bạn thắng! +%d Tâm trạng",
                              CareSystem::kGamesBoost);
                if (eye_display != nullptr) {
                    eye_display->SetEmotion("sad");
                }
                break;
            case CheckerGame::Result::kBubuWin:
                // Playing with Bubu is fun whoever wins; a finished game still pays.
                RewardGameMood("checker", CareSystem::kGamesBoost / 2);
                gamesStatusColor = 0xFFB366;
                gamesActionColor = 0xFFD23F;
                std::snprintf(gameStatusMsg, sizeof(gameStatusMsg), "%s", "Bubu thắng rồi");
                if (eye_display != nullptr) {
                    eye_display->SetEmotion("laughing");
                    eye_display->EyeAnimLaugh();
                }
                break;
            case CheckerGame::Result::kDraw:
                RewardGameMood("checker", CareSystem::kGamesBoost / 2);
                gamesStatusColor = 0xA7D8FF;
                gamesActionColor = 0xA7D8FF;
                std::snprintf(gameStatusMsg, sizeof(gameStatusMsg), "%s", "Hòa rồi");
                if (eye_display != nullptr) {
                    eye_display->SetEmotion("surprised");
                }
                break;
            case CheckerGame::Result::kStopped:
                gamesStatusColor = COLOR_TEXT;
                gamesActionColor = 0xA7D8FF;
                std::snprintf(gameStatusMsg, sizeof(gameStatusMsg), "%s", "Đã dừng");
                break;
            case CheckerGame::Result::kNone:
            default:
                gamesStatusColor = COLOR_TEXT;
                gamesActionColor = 0xA7D8FF;
                std::snprintf(gameStatusMsg, sizeof(gameStatusMsg), "%s", "Đã dừng");
                break;
        }
        checkerBubuThinking = false;
        checkerFinishHolding = false;
        checkerScreen = CheckerScreen::kTitle;
        if (displayHandle != nullptr) {
            DisplayLockGuard teardown_lock(displayHandle);
            HideCheckerConfetti();
            for (lv_obj_t* screen : {checkerTitleScreen, checkerMatchupScreen,
                                     checkerPlayAgainScreen, checkerDotsRow}) {
                if (screen != nullptr) {
                    lv_obj_add_flag(screen, LV_OBJ_FLAG_HIDDEN);
                }
            }
        }
    } else if (activeGame == ACTIVE_GAME_QUICK_TAP) {
        quickTapScreen = QuickTapScreen::kSetup;
        gamesActionColor = kQuickTapRing;
        gamesStatusColor = COLOR_TEXT;
        std::snprintf(gameStatusMsg, sizeof(gameStatusMsg),
                      "%s", "Chạm chấm trắng thật nhanh\n30 giây");
        if (displayHandle != nullptr) {
            DisplayLockGuard teardown_lock(displayHandle);
            StopQuickTapTimer();   // lv_timer_delete needs the LVGL lock
            HideQuickTapAll();
        }
    } else if (activeGame == ACTIVE_GAME_SNAKE) {
        snakeScreen = SnakeScreen::kSetup;
        gamesActionColor = kSnakeAccent;
        gamesStatusColor = COLOR_TEXT;
        if (displayHandle != nullptr) {
            DisplayLockGuard teardown_lock(displayHandle);
            StopSnakeTimer();   // lv_timer_delete needs the LVGL lock
            HideSnakeAll();
        }
    } else if (activeGame == ACTIVE_GAME_TILT_MAZE) {
        TiltMazeGame::Stop();
        gamesActionColor = kMazeAccent;
        gamesStatusColor = COLOR_TEXT;
        if (displayHandle != nullptr) {
            DisplayLockGuard teardown_lock(displayHandle);
            StopTiltMazeTimer();
            HideTiltMazeAll();
        }
    } else if (activeGame == ACTIVE_GAME_TRAFFIC_RUNNER) {
        TrafficRunnerGame::Stop();
        gamesActionColor = kRunnerAccent;
        gamesStatusColor = COLOR_TEXT;
        if (displayHandle != nullptr) {
            DisplayLockGuard teardown_lock(displayHandle);
            StopTrafficRunnerTimer();
            HideTrafficRunnerAll();
        }
    }

    activeGame = ACTIVE_GAME_NONE;

    // Back to the carousel, not to a result banner. The list's one text row is
    // a single vn_20 line with ~128px of chord to live in, and the outcome
    // strings set above run 156-343px -- they only ever fitted because the old
    // status pill wrapped them over three lines. Both games that have a result
    // worth reading already show it on their own end screen (the checker's
    // result screen, Quick Tap's scoreboard) before this runs.
    SetGamesMenuStatusForSelection();

    DisplayLockGuard lock(displayHandle);
    UnloadGameUi();
    ShowPanel(gamesPanel);
    SetMenuState(MENU_GAMES_OPEN);
    UpdateGamesUI();
}

bool HandleGameTap(uint16_t x, uint16_t y) {
    if (currentState != MENU_GAME_ACTIVE) {
        return false;
    }

    MarkMenuActivity();
    if (activeGame == ACTIVE_GAME_GREEN_EYE) {
        bool leave = false;
        {
            DisplayLockGuard lock(displayHandle);
            switch (greenEyeScreen) {
                case GreenEyeScreen::kSetup:
                    if (IsPointNear(greenEyePlayBtn, x, y, kGreenEyeButtonSlackPx)) {
                        BeginGreenEyeGame();
                    }
                    break;
                case GreenEyeScreen::kPlaying:
                    // Judged under the lock the tick also holds, and drawn now
                    // rather than on the next tick: a hit pops under the finger.
                    GreenEyeGame::HandleTap(static_cast<int>(x), static_cast<int>(y), lv_tick_get());
                    AnnounceGreenEyeDecision();
                    UpdateGreenEyePlayfield();
                    break;
                case GreenEyeScreen::kBanner:
                    break;   // let HẾT LƯỢT! run its hold
                case GreenEyeScreen::kScore:
                    if (IsPointNear(greenEyeAgainBtn, x, y, kGreenEyeButtonSlackPx)) {
                        BeginGreenEyeGame();
                    } else if (IsPointNear(greenEyeMenuBtn, x, y, kGreenEyeButtonSlackPx)) {
                        leave = true;
                    }
                    break;
            }
        }
        if (leave) {
            HandleGameFinished();   // takes the display lock itself
        }
        return true;
    }

    if (activeGame == ACTIVE_GAME_TRAFFIC_RUNNER) {
        const auto phase = TrafficRunnerGame::GetState().phase;
        if (phase == TrafficRunnerGame::Phase::kStopped) {
            if (IsPointInside(runnerPlayBtn, x, y)) {
                TrafficRunnerGame::Start(lv_tick_get());
                DisplayLockGuard lock(displayHandle);
                UpdateTrafficRunnerUI();
            }
            return true;
        }
        if (phase == TrafficRunnerGame::Phase::kGameOver) {
            if (IsPointInside(runnerAgainBtn, x, y)) {
                TrafficRunnerGame::Start(lv_tick_get());
                DisplayLockGuard lock(displayHandle);
                UpdateTrafficRunnerUI();
            } else if (IsPointInside(runnerMenuBtn, x, y)) {
                HandleGameFinished();
            }
            return true;
        }
        return true;
    }

    if (activeGame == ACTIVE_GAME_TILT_MAZE) {
        const TiltMazeGame::State& maze_state = TiltMazeGame::GetState();
        switch (maze_state.phase) {
            case TiltMazeGame::Phase::kCalibrating:
                if (IsPointInside(mazeCalibrateBtn, x, y)) {
                    if (!TiltMazeGame::ConfirmNeutral(lv_tick_get()) && displayHandle != nullptr) {
                        displayHandle->ShowNotification("Hãy giữ Bubu yên một chút");
                    }
                    DisplayLockGuard lock(displayHandle);
                    UpdateTiltMazeUI();
                }
                return true;
            case TiltMazeGame::Phase::kCountdown:
                return true;
            case TiltMazeGame::Phase::kPlaying: {
                TiltMazeGame::EnterPlanning();
                DisplayLockGuard lock(displayHandle);
                UpdateTiltMazeUI();
                return true;
            }
            case TiltMazeGame::Phase::kPlanning: {
                if (IsPointInside(mazePlanUpBtn, x, y)) {
                    TiltMazeGame::PanPlanning(TiltMazeGame::PanDirection::kUp);
                } else if (IsPointInside(mazePlanDownBtn, x, y)) {
                    TiltMazeGame::PanPlanning(TiltMazeGame::PanDirection::kDown);
                } else if (IsPointInside(mazePlanLeftBtn, x, y)) {
                    TiltMazeGame::PanPlanning(TiltMazeGame::PanDirection::kLeft);
                } else if (IsPointInside(mazePlanRightBtn, x, y)) {
                    TiltMazeGame::PanPlanning(TiltMazeGame::PanDirection::kRight);
                } else if (IsPointInside(mazePlanResumeBtn, x, y)) {
                    TiltMazeGame::ResumeFromPlanning(lv_tick_get());
                }
                DisplayLockGuard lock(displayHandle);
                UpdateTiltMazeUI();
                return true;
            }
            case TiltMazeGame::Phase::kComplete:
                if (IsPointInside(mazeResultRetryBtn, x, y)) {
                    TiltMazeGame::StartLevel(maze_state.level_index, lv_tick_get());
                    DisplayLockGuard lock(displayHandle);
                    UpdateTiltMazeUI();
                } else if (IsPointInside(mazeResultPrimaryBtn, x, y)) {
                    if (maze_state.level_index + 1 < TiltMazeGame::kImplementedLevels) {
                        TiltMazeGame::StartLevel(maze_state.level_index + 1, lv_tick_get());
                        DisplayLockGuard lock(displayHandle);
                        UpdateTiltMazeUI();
                    } else {
                        HandleGameFinished();
                    }
                }
                return true;
            case TiltMazeGame::Phase::kStopped:
                return true;
        }
        return true;
    }

    if (activeGame == ACTIVE_GAME_QUICK_TAP) {
        // Screens of the same game: route the tap to whichever is on screen.
        switch (quickTapScreen) {
            case QuickTapScreen::kSetup: {
                for (int i = 0; i < kQuickTapDiffCount; ++i) {
                    if (IsPointInside(quickTapDiffBtns[i], x, y)) {
                        quickTapDifficulty = static_cast<QuickTapGame::Difficulty>(i);
                        DisplayLockGuard lock(displayHandle);
                        UpdateQuickTapSetupUI();
                        return true;
                    }
                }
                // Tapping a mode pill is what starts the round -- the mode is
                // never a separate selection step.
                for (int i = 0; i < kQuickTapModeCount; ++i) {
                    if (IsPointInside(quickTapModeBtns[i], x, y)) {
                        DisplayLockGuard lock(displayHandle);
                        BeginQuickTapRound(static_cast<QuickTapGame::Mode>(i));
                        return true;
                    }
                }
                return true;
            }
            case QuickTapScreen::kPlaying: {
                QuickTapGame::HandleTap(static_cast<int>(x), static_cast<int>(y));
                // Redraw now rather than waiting up to 33ms for the tick: the
                // dot has to feel like it moved the instant it was hit.
                DisplayLockGuard lock(displayHandle);
                UpdateQuickTapPlayfield();
                return true;
            }
            case QuickTapScreen::kBanner:
                return true;   // let HẾT GIỜ! / CHẠM NHẦM! run its hold
            case QuickTapScreen::kScore:
                if (IsPointInside(quickTapReplayBtn, x, y)) {
                    DisplayLockGuard lock(displayHandle);
                    BeginQuickTapRound(quickTapMode);
                } else if (IsPointInside(quickTapMenuBtn, x, y)) {
                    DisplayLockGuard lock(displayHandle);
                    ShowQuickTapScreen(QuickTapScreen::kSetup);
                }
                return true;
        }
        return true;
    }

    if (activeGame == ACTIVE_GAME_SNAKE) {
        // Screens of the same game: route the tap to whichever is on screen.
        switch (snakeScreen) {
            case SnakeScreen::kSetup: {
                DisplayLockGuard lock(displayHandle);
                if (IsPointInside(snakePlayBtn, x, y)) {
                    BeginSnakeRound();
                } else if (IsPointInside(snakeSpeedBtn, x, y)) {
                    // One pill, tapped to cycle. Wrapping means a child who
                    // overshoots NHANH gets back to CHẬM without hunting for a
                    // second control.
                    snakeSpeed = static_cast<SnakeGame::Speed>(
                        (static_cast<int>(snakeSpeed) + 1) % kSnakeSpeedCount);
                    UpdateSnakeSetupUI();
                }
                return true;
            }
            case SnakeScreen::kPlaying: {
                const SnakeGame::Direction direction =
                    SnakeDirectionForTap(static_cast<int>(x), static_cast<int>(y));
                // Flash whatever was aimed at even when Steer() refuses it (a
                // reversal, or a queue already two deep). The tap was read; the
                // rule is what declined it, and silence here reads as a dropped
                // touch -- the one failure this control cannot afford.
                DisplayLockGuard lock(displayHandle);
                SnakeGame::Steer(direction);
                FlashSnakeChevron(direction);
                return true;
            }
            case SnakeScreen::kHold:
                return true;   // let the dead board hold its beat
            case SnakeScreen::kOver:
                if (IsPointInside(snakeAgainBtn, x, y)) {
                    DisplayLockGuard lock(displayHandle);
                    BeginSnakeRound();
                } else if (IsPointInside(snakeMenuBtn, x, y)) {
                    DisplayLockGuard lock(displayHandle);
                    ShowSnakeScreen(SnakeScreen::kSetup);
                }
                return true;
        }
        return true;
    }

    if (activeGame == ACTIVE_GAME_CHECKER) {
        // Screens of the same game: route the tap to whichever is on screen.
        if (checkerScreen == CheckerScreen::kTitle) {
            if (IsPointInside(checkerPlayBtn, x, y)) {
                DisplayLockGuard lock(displayHandle);
                ShowCheckerScreen(CheckerScreen::kMatchup);
            }
            return true;
        }
        if (checkerScreen == CheckerScreen::kMatchup) {
            DisplayLockGuard lock(displayHandle);
            BeginCheckerMatch();   // tap skips the pause
            return true;
        }
        if (checkerScreen == CheckerScreen::kPlayAgain) {
            if (IsPointInside(checkerYesBtn, x, y)) {
                DisplayLockGuard lock(displayHandle);
                BeginCheckerMatch();
            } else if (IsPointInside(checkerNoBtn, x, y)) {
                HandleGameFinished();
            }
            return true;
        }
        if (checkerScreen == CheckerScreen::kResult) {
            return true;   // let the celebration finish
        }
        if (checkerBubuThinking) {
            // Bubu is "thinking" — ignore taps until its move lands.
            return true;
        }

        size_t tapped_index = checkerCellButtons.size();
        for (size_t index = 0; index < checkerCellButtons.size(); ++index) {
            if (IsPointInside(checkerCellButtons[index], x, y)) {
                tapped_index = index;
                break;
            }
        }
        if (tapped_index >= checkerCellButtons.size()) {
            return true;
        }

        const CheckerGame::TapOutcome outcome = CheckerGame::HandleTap(static_cast<uint8_t>(tapped_index));
        if (outcome != CheckerGame::TapOutcome::kPlaced) {
            return true;
        }

        {
            DisplayLockGuard lock(displayHandle);
            UpdateCheckerBoardUI();
        }

        if (!CheckerGame::IsRunning()) {
            BeginCheckerFinishHold();
            DisplayLockGuard lock(displayHandle);
            UpdateGamesUI();
        } else if (!CheckerGame::IsPlayerTurn()) {
            checkerBubuThinking = true;
            checkerBubuMoveStartMs = lv_tick_get();
        }
        return true;
    }

    return true;
}

void HandleGameLongPress() {
    if (currentState != MENU_GAME_ACTIVE) {
        return;
    }
    if (activeGame == ACTIVE_GAME_GREEN_EYE) {
        // Stopped (and paid) under the display lock in HandleGameFinished().
    } else if (activeGame == ACTIVE_GAME_CHECKER) {
        CheckerGame::Stop();
    } else if (activeGame == ACTIVE_GAME_QUICK_TAP) {
        QuickTapGame::Stop();
    } else if (activeGame == ACTIVE_GAME_SNAKE) {
        SnakeGame::Stop();
    } else if (activeGame == ACTIVE_GAME_TILT_MAZE) {
        TiltMazeGame::Stop();
    } else if (activeGame == ACTIVE_GAME_TRAFFIC_RUNNER) {
        TrafficRunnerGame::Stop();
    }
    HandleGameFinished();
}
void OpenPomodoro() {
    DisplayLockGuard lock(displayHandle);
    HideAllPanels();
    ShowPanel(pomodoroPanel);
    SetMenuState(MENU_POMODORO_OPEN);
    pomodoroBannerIsVoid = false;
    ShowPomodoroScreen(PomodoroScreen::kSetup);
    StartPomodoroTimer();
    MarkMenuActivity();
}

void ClosePomodoroToMenu() {
    if (currentState != MENU_POMODORO_OPEN) {
        return;
    }
    MarkMenuActivity();
    DisplayLockGuard lock(displayHandle);
    StopPomodoroTimer();
    PomodoroTimer::Stop();
    pomodoroScreen = PomodoroScreen::kSetup;
    pomodoroBannerIsVoid = false;
    HidePanel(pomodoroPanel);
    ShowPanel(menuPanel);
    SetMenuState(MENU_OPEN);
    ScrollMenuToIndex(static_cast<uint8_t>(MENU_POMODORO), LV_ANIM_OFF);
}

// Tap routing. Deliberately asymmetric: on the setup screen a tap picks and
// starts a block, while during a running block NO tap does anything at all --
// with no pause, any gesture that "stops" is a void, and a void must be
// deliberate. Long press is the only way out.
bool HandlePomodoroTap(uint16_t x, uint16_t y) {
    if (currentState != MENU_POMODORO_OPEN) {
        return false;
    }
    MarkMenuActivity();
    if (pomodoroScreen != PomodoroScreen::kSetup) {
        return true;   // consumed, intentionally inert
    }

    DisplayLockGuard lock(displayHandle);
    for (int i = 0; i < kPomodoroPresetCount; ++i) {
        lv_obj_t* disc = pomodoroDiscs[i];
        if (disc == nullptr) {
            continue;
        }
        lv_area_t area;
        lv_obj_get_coords(disc, &area);
        if (static_cast<int>(x) >= area.x1 - kPomodoroDiscHitSlack &&
            static_cast<int>(x) <= area.x2 + kPomodoroDiscHitSlack &&
            static_cast<int>(y) >= area.y1 - kPomodoroDiscHitSlack &&
            static_cast<int>(y) <= area.y2 + kPomodoroDiscHitSlack) {
            BeginPomodoroRun(static_cast<PomodoroTimer::Preset>(i));
            return true;
        }
    }
    // Anywhere else on the setup screen starts whatever is selected -- 25' on
    // entry, which is the only one of the three that is a real pomodoro.
    BeginPomodoroRun(pomodoroPreset);
    return true;
}

void StartPomodoroFromVoice(int focus_minutes) {
    // Snap to the nearest preset rather than inventing a duration: the whole
    // point of a pomodoro is that its length is fixed.
    PomodoroTimer::Preset preset = PomodoroTimer::Preset::kClassic;
    int best = 0x7FFFFFFF;
    for (int i = 0; i < kPomodoroPresetCount; ++i) {
        const auto candidate = static_cast<PomodoroTimer::Preset>(i);
        const int delta = std::abs(focus_minutes -
                                   static_cast<int>(PomodoroTimer::GetProfile(candidate).focus_min));
        if (delta < best) {
            best = delta;
            preset = candidate;
        }
    }
    if (currentState != MENU_POMODORO_OPEN) {
        OpenPomodoro();
    }
    DisplayLockGuard lock(displayHandle);
    BeginPomodoroRun(preset);
    MarkMenuActivity();
}

void StopPomodoro() {
    if (currentState != MENU_POMODORO_OPEN) {
        PomodoroTimer::Stop();
        return;
    }
    DisplayLockGuard lock(displayHandle);
    PomodoroTimer::Stop();
    pomodoroBannerIsVoid = false;
    ShowPomodoroScreen(PomodoroScreen::kSetup);
    MarkMenuActivity();
}

void CloseLevelToMenu() {
    if (currentState != MENU_LEVEL_OPEN) {
        return;
    }

    DisplayLockGuard lock(displayHandle);
    HidePanel(levelPanel);
    if (levelOpenedFromCare) {
        ShowPanel(carePanel);
        SetMenuState(MENU_CARE_OPEN);
        selectedCareItem = CARE_LEVEL;
        ScrollCareToIndex(static_cast<uint8_t>(selectedCareItem), LV_ANIM_OFF);
    } else {
        ShowPanel(menuPanel);
        SetMenuState(MENU_OPEN);
        selectedItem = MENU_CARE;
        ScrollMenuToIndex(static_cast<uint8_t>(selectedItem), LV_ANIM_OFF);
    }
    levelOpenedFromCare = false;
}

void CloseSettingsToMenu() {
    if (currentState != MENU_SETTINGS_OPEN) {
        return;
    }

    DisplayLockGuard lock(displayHandle);
    HidePanel(settingsPanel);
    ShowPanel(menuPanel);
    SetMenuState(MENU_OPEN);
    selectedItem = MENU_SETTINGS;
    ScrollMenuToIndex(static_cast<uint8_t>(selectedItem), LV_ANIM_OFF);
}

bool HandleVolumeTap(uint16_t x, uint16_t y) {
    if (currentState != MENU_VOLUME_OPEN) {
        return false;
    }

    MarkMenuActivity();

    bool minus_pressed = false;
    bool plus_pressed = false;
    bool back_pressed = false;
    if (displayHandle != nullptr) {
        DisplayLockGuard lock(displayHandle);
        minus_pressed = IsPointInside(volumeMinusButton, x, y);
        plus_pressed = IsPointInside(volumePlusButton, x, y);
        back_pressed = IsPointInside(volumeBackButton, x, y);
    }

    if (minus_pressed) {
        VolumeStep(false);
        return true;
    }
    if (plus_pressed) {
        VolumeStep(true);
        return true;
    }
    if (back_pressed) {
        VolumeBack();
        return true;
    }

    return false;
}

void VolumeStep(bool increase) {
    if (currentState != MENU_VOLUME_OPEN) {
        return;
    }

    MarkMenuActivity();
    const int delta = increase ? 10 : -10;
    PreviewOutputVolumeClamped(volumeWorkingValue + delta);
}

void VolumeBack() {
    if (currentState != MENU_VOLUME_OPEN) {
        return;
    }

    CommitVolumeIfPending();
    DisplayLockGuard lock(displayHandle);
    HidePanel(volumePanel);
    ShowPanel(settingsPanel);
    SetMenuState(MENU_SETTINGS_OPEN);
    selectedSettingsItem = SETTINGS_VOLUME;
    UpdateSettingsItemStyles();
}

bool HandleSettingsTap(uint16_t x, uint16_t y) {
    if (currentState != MENU_SETTINGS_OPEN) {
        return false;
    }

    MarkMenuActivity();

    for (int i = 0; i < SETTINGS_ITEM_COUNT; ++i) {
        if (settingsItems[i] == nullptr || !IsPointInside(settingsItems[i], x, y)) {
            continue;
        }

        selectedSettingsItem = static_cast<SettingsItem>(i);
        {
            DisplayLockGuard lock(displayHandle);
            UpdateSettingsItemStyles();
        }
        ActivateCurrent();
        return true;
    }

    return false;
}

void SelectSleepNext() {}
void SelectSleepPrev() {}
void ActivateSleepSelected() {}
void CloseSleepToCare() {}
void SelectNotesNext() {
    if (currentState != MENU_NOTES_OPEN || notesItems.empty()) {
        return;
    }
    MarkMenuActivity();
    size_t next = selectedNoteIndex + 1;
    if (next >= notesItems.size()) {
        next = notesItems.size() - 1;
    }
    DisplayLockGuard lock(displayHandle);
    ScrollNotesToIndex(next, LV_ANIM_ON);
}

void SelectNotesPrev() {
    if (currentState != MENU_NOTES_OPEN || notesItems.empty()) {
        return;
    }
    MarkMenuActivity();
    size_t next = selectedNoteIndex == 0 ? 0 : selectedNoteIndex - 1;
    DisplayLockGuard lock(displayHandle);
    ScrollNotesToIndex(next, LV_ANIM_ON);
}

void ActivateNotesSelected() {
    if (currentState != MENU_NOTES_OPEN) {
        return;
    }
    MarkMenuActivity();
    DisplayLockGuard lock(displayHandle);
    RebuildNotesList();
    HidePanel(notesPanel);
    ShowPanel(noteDetailPanel);
    SetMenuState(MENU_NOTE_DETAIL_OPEN);
    ShowNoteDetailForCurrentSelection();
}

void CloseNotesToMenu() {
    if (currentState != MENU_NOTES_OPEN) {
        return;
    }

    DisplayLockGuard lock(displayHandle);
    HidePanel(notesPanel);
    ShowPanel(menuPanel);
    SetMenuState(MENU_OPEN);
    selectedItem = MENU_NOTES;
    ScrollMenuToIndex(static_cast<uint8_t>(selectedItem), LV_ANIM_OFF);
}

void CloseNoteDetailToNotes() {
    if (currentState != MENU_NOTE_DETAIL_OPEN) {
        return;
    }

    DisplayLockGuard lock(displayHandle);
    RebuildNotesList();
    HidePanel(noteDetailPanel);
    ShowPanel(notesPanel);
    SetMenuState(MENU_NOTES_OPEN);
    ScrollNotesToIndex(selectedNoteIndex, LV_ANIM_OFF);
}

void NotesDetailNext() {
    if (currentState != MENU_NOTE_DETAIL_OPEN || notesEntries.empty()) {
        return;
    }
    MarkMenuActivity();
    selectedNoteIndex = (selectedNoteIndex + 1) % notesEntries.size();
    DisplayLockGuard lock(displayHandle);
    ShowNoteDetailForCurrentSelection();
}

void NotesDetailPrev() {
    if (currentState != MENU_NOTE_DETAIL_OPEN || notesEntries.empty()) {
        return;
    }
    MarkMenuActivity();
    selectedNoteIndex = (selectedNoteIndex + notesEntries.size() - 1) % notesEntries.size();
    DisplayLockGuard lock(displayHandle);
    ShowNoteDetailForCurrentSelection();
}
void StartCleanAnimation() {}

void Render() {
    if (displayHandle == nullptr) {
        return;
    }

    if (transientAnimationActive && transientOverlay != nullptr) {
        DisplayLockGuard lock(displayHandle);
        lv_obj_move_foreground(transientOverlay);
        if (transientImage != nullptr) {
            lv_obj_move_foreground(transientImage);
        }
    }

    if (currentState != MENU_CLOSED &&
        lv_tick_elaps(lastMenuActivityMs) >= MENU_INACTIVITY_TIMEOUT_MS) {
        ESP_LOGI(TAG, "Menu inactivity timeout -> close");
        Close();
        return;
    }

    if (currentState == MENU_VOLUME_OPEN) {
        DisplayLockGuard lock(displayHandle);
        UpdateVolumeValueLabel();
        return;
    }

    if (currentState == MENU_CONNECT_OPEN && connectView == CONNECT_VIEW_WIFI_LIST) {
        auto& wifi_service = WifiConnectService::GetInstance();
        const uint32_t scan_version = wifi_service.GetScanVersion();
        const bool scanning = wifi_service.IsScanning();
        if (scan_version != lastWifiScanVersion || scanning != lastWifiScanActive) {
            DisplayLockGuard lock(displayHandle);
            lastWifiScanVersion = scan_version;
            lastWifiScanActive = scanning;
            RebuildWifiList();
        }
        return;
    }

    if (currentState == MENU_LEVEL_OPEN) {
        DisplayLockGuard lock(displayHandle);
        UpdateLevelUI();
        return;
    }

    if (currentState == MENU_GAMES_OPEN) {
        // Not while a game is loading: the 1 Hz repaint would put the carousel
        // back on screen underneath "ĐANG TẢI...".
        if (IsGameLoadPending()) {
            return;
        }
        DisplayLockGuard lock(displayHandle);
        UpdateGamesUI();
        return;
    }

    if (currentState == MENU_GAME_ACTIVE) {
        if (activeGame == ACTIVE_GAME_GREEN_EYE) {
            // Nothing here: GreenEyeTimerCb ticks the game at 30Hz. This path
            // runs at 1Hz, far too slowly for rounds that last a second.
        } else if (activeGame == ACTIVE_GAME_CHECKER) {
            if (checkerScreen == CheckerScreen::kTitle) {
                return;   // waits for the play button
            }
            if (checkerScreen == CheckerScreen::kMatchup) {
                if (lv_tick_elaps(checkerScreenStartMs) >= kCheckerMatchupHoldMs) {
                    DisplayLockGuard lock(displayHandle);
                    BeginCheckerMatch();
                }
                return;
            }
            if (checkerScreen == CheckerScreen::kPlayAgain) {
                return;   // waits for yes / no
            }
            if (checkerBubuThinking && lv_tick_elaps(checkerBubuMoveStartMs) >= kCheckerBubuThinkDelayMs) {
                checkerBubuThinking = false;
                CheckerGame::PlayBubuTurn();
                if (!CheckerGame::IsRunning()) {
                    BeginCheckerFinishHold();
                }
            }
            {
                DisplayLockGuard lock(displayHandle);
                UpdateCheckerBoardUI();
            }
            // Let the winning line linger before the result screen takes over.
            if (checkerFinishHolding &&
                lv_tick_elaps(checkerFinishHoldStartMs) >= kCheckerFinishHoldMs) {
                checkerFinishHolding = false;
                DisplayLockGuard lock(displayHandle);
                ShowCheckerScreen(CheckerScreen::kPlayAgain);
            } else if (!checkerFinishHolding && !CheckerGame::IsRunning() &&
                       checkerScreen != CheckerScreen::kResult) {
                HandleGameFinished();
            }
        }
        return;
    }

    if (currentState != MENU_STATS_OPEN) {
        return;
    }

    DisplayLockGuard lock(displayHandle);
    UpdateStatsUI();
}

bool HandleCareAnimationTap() {
    auto* eye_display = GetEyeDisplay();
    if (eye_display != nullptr) {
        if (eye_display->HandleFeedTap()) {
            return true;
        }
        if (eye_display->HandleBathTap()) {
            return true;
        }
    }
    if (!transientAnimationActive || displayHandle == nullptr) {
        return false;
    }
    DisplayLockGuard lock(displayHandle);
    StopTransientAnimationLocked();
    return true;
}

bool HandleCareAnimationScrub(int x, int y) {
    auto* eye_display = GetEyeDisplay();
    return eye_display != nullptr && eye_display->HandleBathScrub(x, y);
}

bool IsTapOnSelected(uint16_t x, uint16_t y) {
    if (currentState == MENU_OPEN) {
        return IsPointInside(menuItems[selectedItem], x, y);
    }
    return false;
}

bool IsTapOnUpButton(uint16_t x, uint16_t y) {
    if (!IsOpen()) {
        return false;
    }
    return IsPointInside(upButton, x, y);
}

bool IsTapOnDownButton(uint16_t x, uint16_t y) {
    if (!IsOpen()) {
        return false;
    }
    return IsPointInside(downButton, x, y);
}

bool IsTapOnPrevButton(uint16_t x, uint16_t y) {
    switch (currentState) {
        case MENU_OPEN:
            return IsPointInside(upButton, x, y);
        case MENU_CARE_OPEN:
            return IsPointInside(careUpButton, x, y);
        case MENU_CONNECT_OPEN:
            return IsPointInside(connectUpButton, x, y);
        case MENU_REMINDERS_OPEN:
            return IsPointInside(remindersUpButton, x, y);
        case MENU_NOTES_OPEN:
            return IsPointInside(notesUpButton, x, y);
        case MENU_STATS_OPEN:
            return IsPointInside(statsLeftBtn, x, y);
        case MENU_GAMES_OPEN:
            return IsPointInside(gamesPrevBtn, x, y);
        case MENU_FORTUNE_OPEN:
            return false;
        default:
            return false;
    }
}

bool IsTapOnNextButton(uint16_t x, uint16_t y) {
    switch (currentState) {
        case MENU_OPEN:
            return IsPointInside(downButton, x, y);
        case MENU_CARE_OPEN:
            return IsPointInside(careDownButton, x, y);
        case MENU_CONNECT_OPEN:
            return IsPointInside(connectDownButton, x, y);
        case MENU_REMINDERS_OPEN:
            return IsPointInside(remindersDownButton, x, y);
        case MENU_NOTES_OPEN:
            return IsPointInside(notesDownButton, x, y);
        case MENU_STATS_OPEN:
            return IsPointInside(statsRightBtn, x, y);
        case MENU_GAMES_OPEN:
            return IsPointInside(gamesNextBtn, x, y);
        case MENU_FORTUNE_OPEN:
            return false;
        default:
            return false;
    }
}

bool IsTapOnCareSelected(uint16_t x, uint16_t y) {
    if (currentState != MENU_CARE_OPEN) {
        return false;
    }
    return IsPointInside(careItems[selectedCareItem], x, y);
}

bool IsTapOnStatsTitle(uint16_t x, uint16_t y) {
    if (currentState != MENU_STATS_OPEN) {
        return false;
    }
    return IsPointInside(statsActionZone, x, y);
}

bool IsTapOnStatsNav(uint16_t x, uint16_t y) {
    if (currentState != MENU_STATS_OPEN) {
        return false;
    }
    return IsPointInside(statsLeftBtn, x, y) || IsPointInside(statsRightBtn, x, y);
}

bool IsTapOnSleepSelected(uint16_t, uint16_t) { return false; }
bool IsTapOnNotesSelected(uint16_t x, uint16_t y) {
    if (currentState != MENU_NOTES_OPEN || notesItems.empty() || selectedNoteIndex >= notesItems.size()) {
        return false;
    }
    return IsPointInside(notesItems[selectedNoteIndex], x, y);
}

bool IsTapOnRemindersSelected(uint16_t x, uint16_t y) {
    if (currentState != MENU_REMINDERS_OPEN || remindersItems.empty() ||
        selectedReminderIndex >= remindersItems.size()) {
        return false;
    }
    return IsPointInside(remindersItems[selectedReminderIndex], x, y);
}

// ---------------------------------------------------------------------------
// Consolidated input dispatch.
//
// These own the state->handler mapping that input callers used to duplicate.
// When adding a screen, add its cases here; nothing outside this file needs to
// change.
// ---------------------------------------------------------------------------

bool HandleTap(uint16_t x, uint16_t y) {
    if (!IsAnyOpen()) {
        return false;
    }
    // Swallow taps while a game's screens are being built: nothing on the
    // loading screen is interactive, and a stray tap must not reach the
    // carousel underneath.
    if (IsGameLoadPending()) {
        MarkMenuActivity();
        return true;
    }

    // Shared chrome first: the up/down arrows exist on most panels.
    if (IsTapOnPrevButton(x, y)) {
        NavigatePrev();
        return true;
    }
    if (IsTapOnNextButton(x, y)) {
        NavigateNext();
        return true;
    }

    switch (currentState) {
        case MENU_OPEN:
            if (IsTapOnSelected(x, y)) {
                ActivateSelected();
                return true;
            }
            return false;
        case MENU_CARE_OPEN:
            if (IsTapOnCareSelected(x, y)) {
                ActivateCareSelected();
                return true;
            }
            return false;
        case MENU_CONNECT_OPEN:
            return HandleConnectTap(x, y);
        case MENU_KEYBOARD_OPEN:
            return HandleKeyboardTap(x, y);
        case MENU_SETTINGS_OPEN:
            return HandleSettingsTap(x, y);
        case MENU_POMODORO_OPEN:
            return HandlePomodoroTap(x, y);
        case MENU_VOLUME_OPEN:
            return HandleVolumeTap(x, y);
        case MENU_REMINDERS_OPEN:
            if (IsTapOnRemindersSelected(x, y)) {
                ActivateCurrent();
                return true;
            }
            return false;
        case MENU_NOTES_OPEN:
            if (IsTapOnNotesSelected(x, y)) {
                ActivateCurrent();
                return true;
            }
            return false;
        case MENU_REMINDER_DETAIL_OPEN:
        case MENU_NOTE_DETAIL_OPEN:
        case MENU_GAMES_OPEN:
        case MENU_LEVEL_OPEN:
            // Any tap acts as confirm/back on these panels.
            ActivateCurrent();
            return true;
        case MENU_STATS_OPEN:
            if (IsTapOnStatsTitle(x, y)) {
                ActivateCurrentOption();
                return true;
            }
            return false;
        case MENU_GAME_ACTIVE:
            return HandleGameTap(x, y);
        case MENU_FORTUNE_OPEN:
            return FortuneSystem::HandleTap(x, y);
        default:
            return false;
    }
}

bool HandleSwipe(SwipeDirection direction) {
    // Snake is the only consumer today. Everything else returns false, which is
    // what lets the board dispatch every non-tap release here unconditionally
    // instead of switching on the active screen itself -- the same rule the
    // other input entry points in this header follow.
    if (currentState != MENU_GAME_ACTIVE || activeGame != ACTIVE_GAME_SNAKE ||
        snakeScreen != SnakeScreen::kPlaying) {
        return false;
    }
    SnakeGame::Direction mapped = SnakeGame::Direction::kUp;
    switch (direction) {
        case SwipeDirection::kUp:    mapped = SnakeGame::Direction::kUp;    break;
        case SwipeDirection::kDown:  mapped = SnakeGame::Direction::kDown;  break;
        case SwipeDirection::kLeft:  mapped = SnakeGame::Direction::kLeft;  break;
        case SwipeDirection::kRight: mapped = SnakeGame::Direction::kRight; break;
    }
    MarkMenuActivity();
    DisplayLockGuard lock(displayHandle);
    SnakeGame::Steer(mapped);
    FlashSnakeChevron(mapped);
    return true;
}

void HandleImuAccel(float ax, float ay, float az) {
    if (currentState != MENU_GAME_ACTIVE ||
        (activeGame != ACTIVE_GAME_TILT_MAZE &&
         activeGame != ACTIVE_GAME_TRAFFIC_RUNNER)) {
        return;
    }
    // Tilting is active play even when the child does not touch the screen.
    // Keep the menu inactivity watchdog from closing an IMU game underneath it.
    MarkMenuActivity();
    if (activeGame == ACTIVE_GAME_TILT_MAZE) {
        TiltMazeGame::FeedAccel(ax, ay, az, lv_tick_get());
    } else {
        TrafficRunnerGame::FeedAccel(ax, ay, az, lv_tick_get());
    }
}

bool HandleLongPress(uint16_t x, uint16_t y, bool close_by_default) {
    if (!IsAnyOpen()) {
        return false;
    }
    switch (currentState) {
        case MENU_VOLUME_OPEN:
            VolumeBack();
            return true;
        case MENU_FORTUNE_OPEN:
            FortuneSystem::HandleLongPress(x, y);
            return true;
        case MENU_GAMES_OPEN:
            // Exiting a game is a touch-gesture back action. The power button
            // deliberately falls through here so Wi-Fi config stays reachable
            // while a game is on screen.
            if (!close_by_default) {
                return false;
            }
            if (IsGameLoadPending()) {
                // Back while loading cancels the load, not the carousel.
                DisplayLockGuard lock(displayHandle);
                CancelGameLoadLocked();
                UpdateGamesUI();
                return true;
            }
            CloseGamesToStats();
            return true;
        case MENU_GAME_ACTIVE:
            if (!close_by_default) {
                return false;
            }
            // In MẮT XANH a child pressing an eye hard and long is still
            // aiming at it: that is a (late) tap, not a request to leave.
            // Leaving mid-game is a long press on the black around the eyes.
            if (activeGame == ACTIVE_GAME_GREEN_EYE &&
                greenEyeScreen == GreenEyeScreen::kPlaying &&
                GreenEyeGame::EyeAt(static_cast<int>(x), static_cast<int>(y)) >= 0) {
                return HandleGameTap(x, y);
            }
            HandleGameLongPress();
            return true;
        case MENU_POMODORO_OPEN: {
            if (!close_by_default) {
                return false;
            }
            // The one deliberate gesture on this screen. During a focus block it
            // voids -- the technique's interruption rule -- and the banner hands
            // back to setup; anywhere else it just leaves.
            if (PomodoroTimer::GetPhase() == PomodoroTimer::Phase::kFocus &&
                pomodoroScreen == PomodoroScreen::kRunning) {
                MarkMenuActivity();
                DisplayLockGuard lock(displayHandle);
                VoidPomodoroBlock();
                return true;
            }
            ClosePomodoroToMenu();
            return true;
        }
        default:
            if (!close_by_default) {
                return false;
            }
            // Long press in any other menu layer closes back out.
            Close();
            return true;
    }
}

bool HandleActivate() {
    if (!IsAnyOpen()) {
        return false;
    }
    switch (currentState) {
        case MENU_GAME_ACTIVE:
        case MENU_KEYBOARD_OPEN:
            // These consume their own input; a bare activate is a no-op rather
            // than a fall-through to Close().
            return true;
        case MENU_OPEN:
        case MENU_CARE_OPEN:
        case MENU_CONNECT_OPEN:
        case MENU_SETTINGS_OPEN:
        case MENU_REMINDERS_OPEN:
        case MENU_REMINDER_DETAIL_OPEN:
        case MENU_NOTES_OPEN:
        case MENU_NOTE_DETAIL_OPEN:
        case MENU_VOLUME_OPEN:
        case MENU_STATS_OPEN:
        case MENU_GAMES_OPEN:
        case MENU_FORTUNE_OPEN:
        case MENU_LEVEL_OPEN:
        case MENU_POMODORO_OPEN:
            ActivateCurrent();
            return true;
        default:
            // Unhandled sub-panel: close back out rather than trapping the user.
            Close();
            return true;
    }
}

bool HandleNavigate(bool forward) {
    if (!IsAnyOpen()) {
        return false;
    }
    switch (currentState) {
        case MENU_FORTUNE_OPEN:
            // Fortune advances on any input rather than having a cursor.
            FortuneSystem::HandleTap(0, 0);
            return true;
        default:
            if (forward) {
                NavigateNext();
            } else {
                NavigatePrev();
            }
            return true;
    }
}

}  // namespace MenuSystem

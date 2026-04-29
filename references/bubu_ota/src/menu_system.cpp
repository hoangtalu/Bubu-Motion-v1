#include "menu_system.h"
#include "care_system.h"
#include "level_system.h"
#include "display_system.h"
#include "eye_game.h"
#include "message_system.h"
#include "wifi_service.h"
#include "ota/ota_manager.h"
#include "chat/chat_system.h"
#include "chat_config.h"
#include "mock_test_screen.h"
#include "tools/note_system.h"
#include "tools/reminder_system.h"
#include "sound/sound_effects.h"
#include <algorithm>
#include <vector>
#include <esp_system.h>
#include <esp_sleep.h>
#include <lvgl.h>
#include <cstring>
#include <cstdio>
#include <cmath>
#include "logger.h"
#include "crash_monitor.h"
DEFINE_MODULE_LOGGER_DISABLED(MenuLog)  // Set to DEFINE_MODULE_LOGGER to enable

namespace MenuSystem {
void openKeyboardFromConnect();
}

namespace {

MenuState currentState = MENU_CLOSED;
MenuItem selectedItem = MENU_CARE;
bool gamesOpenedFromCare = false;
bool statsOpenedFromCare = false;
bool levelOpenedFromCare = false;

enum FeedReturnTarget {
  FEED_RETURN_MENU,
  FEED_RETURN_CARE,
  FEED_RETURN_STATS
};

FeedReturnTarget feedReturn = FEED_RETURN_MENU;

enum CareItem {
  CARE_FEED,
  CARE_PLAY,
  CARE_CLEAN,
  CARE_SLEEP,
  CARE_STATS,
  CARE_LEVEL,
  CARE_ITEM_COUNT
};

CareItem selectedCareItem = CARE_FEED;

enum SleepItem {
  SLEEP_ZZZ,         // Regular sleep (Z animation)
  SLEEP_WHITE_NOISE, // White noise mode
  SLEEP_RAIN,        // Rain (pink noise) mode
  SLEEP_ITEM_COUNT
};

SleepItem selectedSleepItem = SLEEP_ZZZ;

enum TestSoundItem {
  TEST_SOUND_HAPPY1,
  TEST_SOUND_CURIOUS,
  TEST_SOUND_SAD1,
  TEST_SOUND_ANGRY1,
  TEST_SOUND_TIRED,
  TEST_SOUND_EXCITED,
  TEST_SOUND_LOVE,
  TEST_SOUND_WORRIED,
  TEST_SOUND_ITEM_COUNT
};

TestSoundItem selectedTestSoundItem = TEST_SOUND_HAPPY1;

// LVGL objects
lv_obj_t* menuPanel = nullptr;
lv_obj_t* menuList = nullptr;
lv_obj_t* menuItems[MENU_ITEM_COUNT] = {nullptr};
lv_obj_t* carePanel = nullptr;
lv_obj_t* careList = nullptr;
lv_obj_t* careItems[CARE_ITEM_COUNT] = {nullptr};
lv_obj_t* careTitle = nullptr;
lv_obj_t* statsPanel = nullptr;
lv_obj_t* statsArc = nullptr;
lv_obj_t* statsTitle = nullptr;
lv_obj_t* statsLeftBtn = nullptr;
lv_obj_t* statsRightBtn = nullptr;
lv_obj_t* optionsPanel = nullptr;
lv_obj_t* optionsTitle = nullptr;
lv_obj_t* optionsAction = nullptr;
lv_obj_t* gamesPanel = nullptr;
lv_obj_t* gamesTitle = nullptr;
lv_obj_t* gamesAction = nullptr;
lv_obj_t* gamesStatus = nullptr;
lv_obj_t* connectPanel = nullptr;
lv_obj_t* connectTitle = nullptr;
lv_obj_t* connectRow = nullptr;
lv_obj_t* connectLabel = nullptr;
lv_obj_t* connectSwitch = nullptr;
lv_obj_t* connectOtaBtn = nullptr;
lv_obj_t* connectPassBtn = nullptr;
lv_obj_t* wifiList = nullptr;
lv_obj_t* connectPassValue = nullptr;
lv_obj_t* keyboardPanel = nullptr;
lv_obj_t* keyboardTitle = nullptr;
lv_obj_t* keyboardLabel = nullptr;
lv_obj_t* keyboardMatrix = nullptr;
lv_obj_t* levelPanel = nullptr;
lv_obj_t* levelArc = nullptr;
lv_obj_t* levelLabel = nullptr;
lv_obj_t* settingsPanel = nullptr;
lv_obj_t* settingsTitle = nullptr;
lv_obj_t* settingsResetBtn = nullptr;
lv_obj_t* settingsChatBtn = nullptr;
lv_obj_t* settingsMockingBtn = nullptr;  // Test button for voice mocking
lv_obj_t* sleepPanel = nullptr;
lv_obj_t* sleepList = nullptr;
lv_obj_t* sleepItems[SLEEP_ITEM_COUNT] = {nullptr};
lv_obj_t* sleepTitle = nullptr;

// Test sound panel objects
lv_obj_t* testSoundPanel = nullptr;
lv_obj_t* testSoundList = nullptr;
lv_obj_t* testSoundItems[TEST_SOUND_ITEM_COUNT] = {nullptr};
lv_obj_t* testSoundTitle = nullptr;

// Notes panel objects
lv_obj_t* notesPanel = nullptr;
lv_obj_t* notesList = nullptr;
lv_obj_t* notesTitle = nullptr;
lv_obj_t* noteDetailPanel = nullptr;
lv_obj_t* noteDetailTitle = nullptr;
lv_obj_t* noteDetailContent = nullptr;

// Notes display data
struct NotesDisplayItem {
  String title;
  String content;
  bool isReminder;
};
static std::vector<NotesDisplayItem> notesDisplayItems;
static size_t notesSelectedIndex = 0;
static size_t noteDetailIndex = 0;
static constexpr size_t NOTES_MAX_DISPLAY = 20;
static lv_obj_t* notesItemLabels[NOTES_MAX_DISPLAY] = {nullptr};
static size_t notesItemCount = 0;

// Menu items (uses Vietnamese-capable font)
const char* menuItemNames =
  "CARE\n"
  "CONNECT\n"
  "MESSAGE\n"
  "NOTES\n"
  "SETTINGS";
const char* menuItemLabelTexts[MENU_ITEM_COUNT] = {
  "CHĂM SÓC",
  "KẾT NỐI",
  "THÔNG ĐIỆP",
  "GHI CHÚ",
  "CÀI ĐẶT",
  "TEST SOUND",
  "TEST MOCK"
};
const char* careItemLabelTexts[CARE_ITEM_COUNT] = {
  "CHO ĂN",
  "GIẢI TRÍ",
  "TẮM RỬA",
  "NGỦ",
  "TRẠNG THÁI",
  "CẤP ĐỘ"
};

const char* sleepItemLabelTexts[SLEEP_ITEM_COUNT] = {
  "NGỦ NGHỈ",
  "WHITE NOISE",
  "RAIN"
};

const char* testSoundItemLabelTexts[TEST_SOUND_ITEM_COUNT] = {
  "HAPPY1",
  "CURIOUS",
  "SAD1",
  "ANGRY1",
  "TIRED",
  "EXCITED",
  "LOVE",
  "WORRIED"
};

// Stats
constexpr size_t STAT_COUNT = 4;
const char* statNames[STAT_COUNT] = {"CÁI BỤNG", "CẢM XÚC", "NĂNG LƯỢNG", "SẠCH SẼ"};
uint32_t statColors[STAT_COUNT] = {0xFF7F50, 0x70C1FF, 0xFFD23F, 0x58F5C9};
size_t statIndex = 0;
const char* statOptionNames[STAT_COUNT] = {"BIT-Za", "TRÒ CHƠI", "NGỦ", "TẮM"};

// Colors
constexpr uint32_t COLOR_BACKGROUND = 0x050812;
constexpr uint32_t COLOR_MINT = 0x58F5C9;
constexpr uint32_t COLOR_PINK = 0xDB1758;
constexpr uint32_t COLOR_TEXT = 0xFFFFFF;
constexpr uint32_t COLOR_CONNECT_OK = 0x4CAF50;

static constexpr uint32_t OTA_BREATH_PERIOD_MS = 2000;
static bool otaActive = false;
static uint32_t otaStartMs = 0;
static uint32_t otaLastTickMs = 0;

static bool keyboardCaps = false;

static constexpr uint32_t T9_TAP_TIMEOUT_MS = 900;
static char keyboardText[65] = {0};
static size_t keyboardLen = 0;
static char keyboardLastKey = '\0';
static size_t keyboardLastIndex = 0;
static uint32_t keyboardLastTapMs = 0;

static constexpr size_t WIFI_LIST_MAX = 5;
static uint32_t wifiScanVersionSeen = 0;
static bool wifiScanWasRunning = false;
static bool wifiListDirty = true;
static int wifiSelectedIndex = -1;
static char wifiSelectedSsid[33] = {0};
static char connectPassword[65] = {0};

char gameStatusMsg[64] = "Chạm để chơi";

static constexpr uint32_t FEED_ANIM_DURATION_MS = 5000;
static uint32_t feedAnimEndMs = 0;

enum OptionSelection {
  OPTION_MAIN
};

static OptionSelection optionsSelection = OPTION_MAIN;

void createCircularPanel() {
  if (menuPanel != nullptr) return;
  
  // Create circular panel (240x240 fills screen)
  menuPanel = lv_obj_create(lv_screen_active());
  lv_obj_set_size(menuPanel, 240, 240);
  lv_obj_center(menuPanel);
  
  // Circular shape
  lv_obj_set_style_radius(menuPanel, LV_RADIUS_CIRCLE, 0);
  
  // Dark background
  lv_obj_set_style_bg_color(menuPanel, lv_color_hex(COLOR_BACKGROUND), 0);
  lv_obj_set_style_bg_opa(menuPanel, LV_OPA_COVER, 0);
  
  // Neon border (gradient mint to pink)
  lv_obj_set_style_border_width(menuPanel, 12, 0);
  lv_obj_set_style_border_color(menuPanel, lv_color_hex(COLOR_MINT), 0);
  lv_obj_set_style_border_opa(menuPanel, LV_OPA_COVER, 0);
  
  // No padding
  lv_obj_set_style_pad_all(menuPanel, 0, 0);
  
  // Remove scrollbar
  lv_obj_clear_flag(menuPanel, LV_OBJ_FLAG_SCROLLABLE);
  
  // Start hidden
  lv_obj_add_flag(menuPanel, LV_OBJ_FLAG_HIDDEN);
}

static uint8_t wrapIndex(int idx) {
  int v = idx % static_cast<int>(MENU_ITEM_COUNT);
  if (v < 0) v += MENU_ITEM_COUNT;
  return static_cast<uint8_t>(v);
}

static uint8_t stepTowardLinear(uint8_t current, int target) {
  if (target > static_cast<int>(current)) {
    return static_cast<uint8_t>(current + 1 >= MENU_ITEM_COUNT ? MENU_ITEM_COUNT - 1 : current + 1);
  } else if (target < static_cast<int>(current)) {
    return (current == 0) ? 0 : static_cast<uint8_t>(current - 1);
  }
  return current;
}

static uint8_t stepToward(uint8_t current, int target) {
  int n = static_cast<int>(MENU_ITEM_COUNT);
  int diff = target - static_cast<int>(current);
  if (diff > n / 2) diff -= n;
  if (diff < -n / 2) diff += n;
  if (diff > 1) diff = 1;
  if (diff < -1) diff = -1;
  return wrapIndex(static_cast<int>(current) + diff);
}

static void updateMenuItemStyles() {
  if (!menuList) return;
  // Center is 0 distance; compute styles by distance with wrap
  for (size_t i = 0; i < MENU_ITEM_COUNT; ++i) {
    int diff = static_cast<int>(i) - static_cast<int>(selectedItem);
    int adiff = abs(diff);
    int wrapDiff = static_cast<int>(MENU_ITEM_COUNT) - adiff;
    int dist = (wrapDiff < adiff) ? wrapDiff : adiff;
    lv_obj_t* item = menuItems[i];
    if (!item) continue;
    const lv_font_t* font = &lv_font_montserrat_vn_20;  // fallback smaller
    lv_opa_t opa = LV_OPA_50;
    if (dist == 0) { font = &lv_font_montserrat_vn_22; opa = LV_OPA_COVER; }
    else if (dist == 1) { font = &lv_font_montserrat_vn_20; opa = 200; }
    else if (dist == 2) { font = &lv_font_montserrat_vn_20; opa = LV_OPA_50; }
    else { font = &lv_font_montserrat_vn_20; opa = LV_OPA_40; }
    lv_obj_set_style_text_font(item, font, 0);
    lv_obj_set_style_text_opa(item, opa, 0);
    lv_obj_set_style_text_color(item, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_align(item, LV_TEXT_ALIGN_CENTER, 0);
  }
}

static void scrollMenuToIndex(uint8_t idx, lv_anim_enable_t anim) {
  if (!menuList || idx >= MENU_ITEM_COUNT) return;
  selectedItem = static_cast<MenuItem>(idx);
  updateMenuItemStyles();
  if (menuItems[idx]) {
    lv_obj_scroll_to_view(menuItems[idx], anim);
  }
}

static void updateCareItemStyles() {
  if (!careList) return;
  for (size_t i = 0; i < CARE_ITEM_COUNT; ++i) {
    int dist = abs(static_cast<int>(i) - static_cast<int>(selectedCareItem));
    lv_obj_t* item = careItems[i];
    if (!item) continue;
    const lv_font_t* font = &lv_font_montserrat_vn_20;
    lv_opa_t opa = LV_OPA_50;
    if (dist == 0) { font = &lv_font_montserrat_vn_22; opa = LV_OPA_COVER; }
    else if (dist == 1) { font = &lv_font_montserrat_vn_20; opa = 200; }
    else { font = &lv_font_montserrat_vn_20; opa = LV_OPA_40; }
    lv_obj_set_style_text_font(item, font, 0);
    lv_obj_set_style_text_opa(item, opa, 0);
    lv_obj_set_style_text_color(item, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_align(item, LV_TEXT_ALIGN_CENTER, 0);
  }
}

static void scrollCareToIndex(uint8_t idx, lv_anim_enable_t anim) {
  if (!careList || idx >= CARE_ITEM_COUNT) return;
  selectedCareItem = static_cast<CareItem>(idx);
  updateCareItemStyles();
  if (careItems[idx]) {
    lv_obj_scroll_to_view(careItems[idx], anim);
  }
}

static void menuListScrollCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_SCROLL_END) return;
  lv_obj_t* list = static_cast<lv_obj_t*>(lv_event_get_target(e));
  lv_area_t listCoords;
  lv_obj_get_coords(list, &listCoords);
  int16_t listMidY = (listCoords.y1 + listCoords.y2) / 2;
  int bestIdx = selectedItem;
  int bestDelta = 32000;
  for (size_t i = 0; i < MENU_ITEM_COUNT; ++i) {
    lv_obj_t* item = menuItems[i];
    if (!item) continue;
    lv_area_t c;
    lv_obj_get_coords(item, &c);
    int itemMid = (c.y1 + c.y2) / 2;
    int delta = abs(itemMid - listMidY);
    if (delta < bestDelta) {
      bestDelta = delta;
      bestIdx = static_cast<int>(i);
    }
  }
  uint8_t next = stepTowardLinear(static_cast<uint8_t>(selectedItem), bestIdx);
  if (next != static_cast<uint8_t>(selectedItem)) {
    scrollMenuToIndex(next, LV_ANIM_ON);
  }
}

void createMenuRoller() {
  if (menuList != nullptr) return;
  
  menuList = lv_obj_create(menuPanel);
  lv_obj_set_size(menuList, 200, 160);
  lv_obj_center(menuList);
  lv_obj_set_scroll_dir(menuList, LV_DIR_VER);
  lv_obj_set_scroll_snap_y(menuList, LV_SCROLL_SNAP_CENTER);
  lv_obj_set_scrollbar_mode(menuList, LV_SCROLLBAR_MODE_OFF);
  lv_obj_clear_flag(menuList, LV_OBJ_FLAG_SCROLL_MOMENTUM);  // limit fling to reduce multi-item jumps
  lv_obj_set_style_pad_all(menuList, 0, 0);
  lv_obj_set_style_pad_row(menuList, 6, 0);
  lv_obj_set_style_bg_opa(menuList, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(menuList, 0, 0);
  lv_obj_set_flex_flow(menuList, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(menuList, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
  lv_obj_add_event_cb(menuList, menuListScrollCb, LV_EVENT_SCROLL_END, nullptr);

  for (size_t i = 0; i < MENU_ITEM_COUNT; ++i) {
    lv_obj_t* label = lv_label_create(menuList);
    menuItems[i] = label;
    lv_label_set_text(label, menuItemLabelTexts[i]);
    lv_obj_set_width(label, lv_pct(100));
    lv_obj_set_style_pad_all(label, 8, 0);
    lv_obj_set_style_min_height(label, 28, 0);
  }
  
  scrollMenuToIndex(0, LV_ANIM_OFF);

  // Add up/down buttons
  lv_obj_t* upBtn = lv_btn_create(menuPanel);
  lv_obj_set_size(upBtn, 100, 100);
  lv_obj_align(upBtn, LV_ALIGN_TOP_MID, 0, -65);
  lv_obj_set_style_bg_color(upBtn, lv_color_hex(0x303030), 0);
  lv_obj_set_style_radius(upBtn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_shadow_width(upBtn, 0, 0);
  lv_obj_t* upLabel = lv_label_create(upBtn);
  lv_obj_set_style_text_font(upLabel, &lv_font_montserrat_14, 0);
  lv_label_set_text(upLabel, LV_SYMBOL_UP);
  lv_obj_align(upLabel, LV_ALIGN_CENTER, 0, 30);
  lv_obj_add_event_cb(upBtn, [](lv_event_t* e){ MenuSystem::selectPrev(); }, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* downBtn = lv_btn_create(menuPanel);
  lv_obj_set_size(downBtn, 100, 100);
  lv_obj_align(downBtn, LV_ALIGN_BOTTOM_MID, 0, 65);
  lv_obj_set_style_bg_color(downBtn, lv_color_hex(0x303030), 0);
  lv_obj_set_style_radius(downBtn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_shadow_width(downBtn, 0, 0);
  lv_obj_t* downLabel = lv_label_create(downBtn);
  lv_obj_set_style_text_font(downLabel, &lv_font_montserrat_14, 0);
  lv_label_set_text(downLabel, LV_SYMBOL_DOWN);
  lv_obj_align(downLabel, LV_ALIGN_CENTER, 0, -30);
  lv_obj_add_event_cb(downBtn, [](lv_event_t* e){ MenuSystem::selectNext(); }, LV_EVENT_CLICKED, nullptr);
}

void createCarePanel() {
  if (carePanel != nullptr) return;

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


  careList = lv_obj_create(carePanel);
  lv_obj_set_size(careList, 200, 150);
  lv_obj_align(careList, LV_ALIGN_CENTER, 0, 12);
  lv_obj_set_scroll_dir(careList, LV_DIR_VER);
  lv_obj_set_scroll_snap_y(careList, LV_SCROLL_SNAP_CENTER);
  lv_obj_set_scrollbar_mode(careList, LV_SCROLLBAR_MODE_OFF);
  lv_obj_clear_flag(careList, LV_OBJ_FLAG_SCROLL_MOMENTUM);
  lv_obj_set_style_pad_all(careList, 0, 0);
  lv_obj_set_style_pad_row(careList, 6, 0);
  lv_obj_set_style_bg_opa(careList, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(careList, 0, 0);
  lv_obj_set_flex_flow(careList, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(careList, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);

  for (size_t i = 0; i < CARE_ITEM_COUNT; ++i) {
    lv_obj_t* label = lv_label_create(careList);
    careItems[i] = label;
    lv_label_set_text(label, careItemLabelTexts[i]);
    lv_obj_set_width(label, lv_pct(100));
    lv_obj_set_style_pad_all(label, 8, 0);
    lv_obj_set_style_min_height(label, 28, 0);
  }

  scrollCareToIndex(static_cast<uint8_t>(selectedCareItem), LV_ANIM_OFF);

  lv_obj_t* upBtn = lv_btn_create(carePanel);
  lv_obj_set_size(upBtn, 100, 100);
  lv_obj_align(upBtn, LV_ALIGN_TOP_MID, 0, -65);
  lv_obj_set_style_bg_color(upBtn, lv_color_hex(0x303030), 0);
  lv_obj_set_style_radius(upBtn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_shadow_width(upBtn, 0, 0);
  lv_obj_t* upLabel = lv_label_create(upBtn);
  lv_obj_set_style_text_font(upLabel, &lv_font_montserrat_14, 0);
  lv_label_set_text(upLabel, LV_SYMBOL_UP);
  lv_obj_align(upLabel, LV_ALIGN_CENTER, 0, 30);
  lv_obj_add_event_cb(upBtn, [](lv_event_t* e){ MenuSystem::selectCarePrev(); }, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* downBtn = lv_btn_create(carePanel);
  lv_obj_set_size(downBtn, 100, 100);
  lv_obj_align(downBtn, LV_ALIGN_BOTTOM_MID, 0, 65);
  lv_obj_set_style_bg_color(downBtn, lv_color_hex(0x303030), 0);
  lv_obj_set_style_radius(downBtn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_shadow_width(downBtn, 0, 0);
  lv_obj_t* downLabel = lv_label_create(downBtn);
  lv_obj_set_style_text_font(downLabel, &lv_font_montserrat_14, 0);
  lv_label_set_text(downLabel, LV_SYMBOL_DOWN);
  lv_obj_align(downLabel, LV_ALIGN_CENTER, 0, -30);
  lv_obj_add_event_cb(downBtn, [](lv_event_t* e){ MenuSystem::selectCareNext(); }, LV_EVENT_CLICKED, nullptr);
}

void updateBorderGradient() {
  // TODO: Implement gradient border animation
  // For now using solid mint color
  // Future: animate between mint and pink based on position
}

static void updateSleepItemStyles() {
  if (!sleepList) return;
  for (size_t i = 0; i < SLEEP_ITEM_COUNT; ++i) {
    int dist = abs(static_cast<int>(i) - static_cast<int>(selectedSleepItem));
    lv_obj_t* item = sleepItems[i];
    if (!item) continue;
    const lv_font_t* font = &lv_font_montserrat_vn_20;
    lv_opa_t opa = LV_OPA_50;
    if (dist == 0) { font = &lv_font_montserrat_vn_22; opa = LV_OPA_COVER; }
    else { font = &lv_font_montserrat_vn_20; opa = 200; }
    lv_obj_set_style_text_font(item, font, 0);
    lv_obj_set_style_text_opa(item, opa, 0);
    lv_obj_set_style_text_color(item, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_align(item, LV_TEXT_ALIGN_CENTER, 0);
  }
}

static void scrollSleepToIndex(uint8_t idx, lv_anim_enable_t anim) {
  if (!sleepList || idx >= SLEEP_ITEM_COUNT) return;
  selectedSleepItem = static_cast<SleepItem>(idx);
  updateSleepItemStyles();
  if (sleepItems[idx]) {
    lv_obj_scroll_to_view(sleepItems[idx], anim);
  }
}

static void updateTestSoundItemStyles() {
  for (size_t i = 0; i < TEST_SOUND_ITEM_COUNT; ++i) {
    if (!testSoundItems[i]) continue;
    lv_obj_t* item = testSoundItems[i];
    int dist = static_cast<int>(i) - static_cast<int>(selectedTestSoundItem);
    lv_obj_set_style_bg_color(item, lv_color_hex(0x222222), 0);
    lv_obj_set_style_bg_opa(item, 0, 0);
    if (dist == 0) { lv_obj_set_style_bg_opa(item, 80, 0); }
    const lv_font_t* font = &lv_font_montserrat_vn_20;
    lv_opa_t opa = 160;
    if (dist == 0) { font = &lv_font_montserrat_vn_22; opa = LV_OPA_COVER; }
    else { font = &lv_font_montserrat_vn_20; opa = 200; }
    lv_obj_set_style_text_font(item, font, 0);
    lv_obj_set_style_text_opa(item, opa, 0);
    lv_obj_set_style_text_color(item, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_align(item, LV_TEXT_ALIGN_CENTER, 0);
  }
}

static void scrollTestSoundToIndex(uint8_t idx, lv_anim_enable_t anim) {
  if (!testSoundList || idx >= TEST_SOUND_ITEM_COUNT) return;
  selectedTestSoundItem = static_cast<TestSoundItem>(idx);
  updateTestSoundItemStyles();
  if (testSoundItems[idx]) {
    lv_obj_scroll_to_view(testSoundItems[idx], anim);
  }
}

void createSleepPanel() {
  if (sleepPanel != nullptr) return;

  sleepPanel = lv_obj_create(lv_screen_active());
  lv_obj_set_size(sleepPanel, 240, 240);
  lv_obj_center(sleepPanel);
  lv_obj_set_style_radius(sleepPanel, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(sleepPanel, lv_color_hex(COLOR_BACKGROUND), 0);
  lv_obj_set_style_bg_opa(sleepPanel, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(sleepPanel, 12, 0);
  lv_obj_set_style_border_color(sleepPanel, lv_color_hex(COLOR_MINT), 0);
  lv_obj_set_style_border_opa(sleepPanel, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(sleepPanel, 0, 0);
  lv_obj_clear_flag(sleepPanel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(sleepPanel, LV_OBJ_FLAG_HIDDEN);

  sleepTitle = lv_label_create(sleepPanel);
  lv_label_set_text(sleepTitle, "NGỦ");
  lv_obj_set_style_text_font(sleepTitle, &lv_font_montserrat_vn_22, 0);
  lv_obj_set_style_text_color(sleepTitle, lv_color_hex(COLOR_MINT), 0);
  lv_obj_align(sleepTitle, LV_ALIGN_TOP_MID, 0, 25);

  sleepList = lv_obj_create(sleepPanel);
  lv_obj_set_size(sleepList, 200, 100);
  lv_obj_align(sleepList, LV_ALIGN_CENTER, 0, 12);
  lv_obj_set_scroll_dir(sleepList, LV_DIR_VER);
  lv_obj_set_scroll_snap_y(sleepList, LV_SCROLL_SNAP_CENTER);
  lv_obj_set_scrollbar_mode(sleepList, LV_SCROLLBAR_MODE_OFF);
  lv_obj_clear_flag(sleepList, LV_OBJ_FLAG_SCROLL_MOMENTUM);
  lv_obj_set_style_pad_all(sleepList, 0, 0);
  lv_obj_set_style_pad_row(sleepList, 6, 0);
  lv_obj_set_style_bg_opa(sleepList, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(sleepList, 0, 0);
  lv_obj_set_flex_flow(sleepList, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(sleepList, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);

  for (size_t i = 0; i < SLEEP_ITEM_COUNT; ++i) {
    lv_obj_t* label = lv_label_create(sleepList);
    sleepItems[i] = label;
    lv_label_set_text(label, sleepItemLabelTexts[i]);
    lv_obj_set_width(label, lv_pct(100));
    lv_obj_set_style_pad_all(label, 8, 0);
    lv_obj_set_style_min_height(label, 28, 0);
  }

  scrollSleepToIndex(static_cast<uint8_t>(selectedSleepItem), LV_ANIM_OFF);

  lv_obj_t* upBtn = lv_btn_create(sleepPanel);
  lv_obj_set_size(upBtn, 100, 100);
  lv_obj_align(upBtn, LV_ALIGN_TOP_MID, 0, -65);
  lv_obj_set_style_bg_color(upBtn, lv_color_hex(0x303030), 0);
  lv_obj_set_style_radius(upBtn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_shadow_width(upBtn, 0, 0);
  lv_obj_t* upLabel = lv_label_create(upBtn);
  lv_obj_set_style_text_font(upLabel, &lv_font_montserrat_14, 0);
  lv_label_set_text(upLabel, LV_SYMBOL_UP);
  lv_obj_align(upLabel, LV_ALIGN_CENTER, 0, 30);
  lv_obj_add_event_cb(upBtn, [](lv_event_t* e){ MenuSystem::selectSleepPrev(); }, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* downBtn = lv_btn_create(sleepPanel);
  lv_obj_set_size(downBtn, 100, 100);
  lv_obj_align(downBtn, LV_ALIGN_BOTTOM_MID, 0, 65);
  lv_obj_set_style_bg_color(downBtn, lv_color_hex(0x303030), 0);
  lv_obj_set_style_radius(downBtn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_shadow_width(downBtn, 0, 0);
  lv_obj_t* downLabel = lv_label_create(downBtn);
  lv_obj_set_style_text_font(downLabel, &lv_font_montserrat_14, 0);
  lv_label_set_text(downLabel, LV_SYMBOL_DOWN);
  lv_obj_align(downLabel, LV_ALIGN_CENTER, 0, -30);
  lv_obj_add_event_cb(downBtn, [](lv_event_t* e){ MenuSystem::selectSleepNext(); }, LV_EVENT_CLICKED, nullptr);
}

void createTestSoundPanel() {
  if (testSoundPanel != nullptr) return;

  testSoundPanel = lv_obj_create(lv_screen_active());
  lv_obj_set_size(testSoundPanel, 240, 240);
  lv_obj_center(testSoundPanel);
  lv_obj_set_style_radius(testSoundPanel, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(testSoundPanel, lv_color_hex(COLOR_BACKGROUND), 0);
  lv_obj_set_style_bg_opa(testSoundPanel, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(testSoundPanel, 12, 0);
  lv_obj_set_style_border_color(testSoundPanel, lv_color_hex(COLOR_MINT), 0);
  lv_obj_set_style_border_opa(testSoundPanel, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(testSoundPanel, 0, 0);
  lv_obj_clear_flag(testSoundPanel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(testSoundPanel, LV_OBJ_FLAG_HIDDEN);

  testSoundTitle = lv_label_create(testSoundPanel);
  lv_label_set_text(testSoundTitle, "TEST SOUND");
  lv_obj_set_style_text_font(testSoundTitle, &lv_font_montserrat_vn_22, 0);
  lv_obj_set_style_text_color(testSoundTitle, lv_color_hex(COLOR_MINT), 0);
  lv_obj_align(testSoundTitle, LV_ALIGN_TOP_MID, 0, 25);

  testSoundList = lv_obj_create(testSoundPanel);
  lv_obj_set_size(testSoundList, 200, 100);
  lv_obj_align(testSoundList, LV_ALIGN_CENTER, 0, 12);
  lv_obj_set_scroll_dir(testSoundList, LV_DIR_VER);
  lv_obj_set_scroll_snap_y(testSoundList, LV_SCROLL_SNAP_CENTER);
  lv_obj_set_scrollbar_mode(testSoundList, LV_SCROLLBAR_MODE_OFF);
  lv_obj_clear_flag(testSoundList, LV_OBJ_FLAG_SCROLL_MOMENTUM);
  lv_obj_set_style_pad_all(testSoundList, 0, 0);
  lv_obj_set_style_pad_row(testSoundList, 6, 0);
  lv_obj_set_style_bg_opa(testSoundList, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(testSoundList, 0, 0);
  lv_obj_set_flex_flow(testSoundList, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(testSoundList, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);

  for (size_t i = 0; i < TEST_SOUND_ITEM_COUNT; ++i) {
    lv_obj_t* label = lv_label_create(testSoundList);
    testSoundItems[i] = label;
    lv_label_set_text(label, testSoundItemLabelTexts[i]);
    lv_obj_set_width(label, lv_pct(100));
    lv_obj_set_style_pad_all(label, 8, 0);
    lv_obj_set_style_min_height(label, 28, 0);
  }

  scrollTestSoundToIndex(static_cast<uint8_t>(selectedTestSoundItem), LV_ANIM_OFF);

  lv_obj_t* upBtn = lv_btn_create(testSoundPanel);
  lv_obj_set_size(upBtn, 100, 100);
  lv_obj_align(upBtn, LV_ALIGN_TOP_MID, 0, -65);
  lv_obj_set_style_bg_color(upBtn, lv_color_hex(0x303030), 0);
  lv_obj_set_style_radius(upBtn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_shadow_width(upBtn, 0, 0);
  lv_obj_t* upLabel = lv_label_create(upBtn);
  lv_obj_set_style_text_font(upLabel, &lv_font_montserrat_14, 0);
  lv_label_set_text(upLabel, LV_SYMBOL_UP);
  lv_obj_align(upLabel, LV_ALIGN_CENTER, 0, 30);
  lv_obj_add_event_cb(upBtn, [](lv_event_t* e){ MenuSystem::selectTestSoundPrev(); }, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* downBtn = lv_btn_create(testSoundPanel);
  lv_obj_set_size(downBtn, 100, 100);
  lv_obj_align(downBtn, LV_ALIGN_BOTTOM_MID, 0, 65);
  lv_obj_set_style_bg_color(downBtn, lv_color_hex(0x303030), 0);
  lv_obj_set_style_radius(downBtn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_shadow_width(downBtn, 0, 0);
  lv_obj_t* downLabel = lv_label_create(downBtn);
  lv_obj_set_style_text_font(downLabel, &lv_font_montserrat_14, 0);
  lv_label_set_text(downLabel, LV_SYMBOL_DOWN);
  lv_obj_align(downLabel, LV_ALIGN_CENTER, 0, -30);
  lv_obj_add_event_cb(downBtn, [](lv_event_t* e){ MenuSystem::selectTestSoundNext(); }, LV_EVENT_CLICKED, nullptr);
}

// ── Notes helpers ───────────────────────────────────────────────

static void buildNotesDisplayList() {
  notesDisplayItems.clear();

  auto notes = NoteSystem::getAllNotes();
  std::sort(notes.begin(), notes.end(),
    [](const NoteSystem::Note& a, const NoteSystem::Note& b) {
      if (a.pinned != b.pinned) return a.pinned > b.pinned;
      return a.modified > b.modified;
    });
  for (const auto& n : notes) {
    NotesDisplayItem item;
    item.title = n.title;
    item.content = n.content;
    item.isReminder = false;
    notesDisplayItems.push_back(item);
  }

  auto reminders = ReminderSystem::getAllReminders();
  std::sort(reminders.begin(), reminders.end(),
    [](const ReminderSystem::Reminder& a, const ReminderSystem::Reminder& b) {
      return a.triggerTime < b.triggerTime;
    });
  for (const auto& r : reminders) {
    if (!r.active) continue;
    NotesDisplayItem item;
    item.title = String("[!] ") + r.title;
    item.content = r.title;
    item.isReminder = true;
    notesDisplayItems.push_back(item);
  }
}

static void updateNotesItemStyles() {
  if (!notesList) return;
  for (size_t i = 0; i < notesItemCount; ++i) {
    int dist = abs(static_cast<int>(i) - static_cast<int>(notesSelectedIndex));
    lv_obj_t* item = notesItemLabels[i];
    if (!item) continue;
    const lv_font_t* font = &lv_font_montserrat_vn_20;
    lv_opa_t opa = LV_OPA_50;
    if (dist == 0) { font = &lv_font_montserrat_vn_22; opa = LV_OPA_COVER; }
    else if (dist == 1) { opa = 200; }
    else { opa = LV_OPA_40; }
    lv_obj_set_style_text_font(item, font, 0);
    lv_obj_set_style_text_opa(item, opa, 0);
    lv_obj_set_style_text_color(item, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_align(item, LV_TEXT_ALIGN_CENTER, 0);
  }
}

static void scrollNotesToIndex(size_t idx, lv_anim_enable_t anim) {
  if (!notesList || idx >= notesItemCount) return;
  notesSelectedIndex = idx;
  updateNotesItemStyles();
  if (notesItemLabels[idx]) {
    lv_obj_scroll_to_view(notesItemLabels[idx], anim);
  }
}

static void rebuildNotesList() {
  if (notesList) {
    lv_obj_clean(notesList);
  }
  for (size_t i = 0; i < NOTES_MAX_DISPLAY; ++i) {
    notesItemLabels[i] = nullptr;
  }

  buildNotesDisplayList();
  notesItemCount = notesDisplayItems.size();
  if (notesItemCount > NOTES_MAX_DISPLAY) notesItemCount = NOTES_MAX_DISPLAY;

  if (notesItemCount == 0) {
    lv_obj_t* label = lv_label_create(notesList);
    notesItemLabels[0] = label;
    lv_label_set_text(label, "Trống");
    lv_obj_set_width(label, lv_pct(100));
    lv_obj_set_style_pad_all(label, 8, 0);
    lv_obj_set_style_min_height(label, 28, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_vn_20, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_opa(label, LV_OPA_50, 0);
    notesItemCount = 1;
    return;
  }

  for (size_t i = 0; i < notesItemCount; ++i) {
    lv_obj_t* label = lv_label_create(notesList);
    notesItemLabels[i] = label;
    String displayTitle = notesDisplayItems[i].title;
    if (displayTitle.length() > 18) {
      displayTitle = displayTitle.substring(0, 16) + "..";
    }
    lv_label_set_text(label, displayTitle.c_str());
    lv_obj_set_width(label, lv_pct(100));
    lv_obj_set_style_pad_all(label, 8, 0);
    lv_obj_set_style_min_height(label, 28, 0);
  }

  notesSelectedIndex = 0;
  scrollNotesToIndex(0, LV_ANIM_OFF);
}

void createNotesPanel() {
  if (notesPanel != nullptr) return;

  notesPanel = lv_obj_create(lv_screen_active());
  lv_obj_set_size(notesPanel, 240, 240);
  lv_obj_center(notesPanel);
  lv_obj_set_style_radius(notesPanel, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(notesPanel, lv_color_hex(COLOR_BACKGROUND), 0);
  lv_obj_set_style_bg_opa(notesPanel, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(notesPanel, 12, 0);
  lv_obj_set_style_border_color(notesPanel, lv_color_hex(0xFFD23F), 0);
  lv_obj_set_style_border_opa(notesPanel, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(notesPanel, 0, 0);
  lv_obj_clear_flag(notesPanel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(notesPanel, LV_OBJ_FLAG_HIDDEN);

  notesTitle = lv_label_create(notesPanel);
  lv_label_set_text(notesTitle, "GHI CHÚ");
  lv_obj_set_style_text_font(notesTitle, &lv_font_montserrat_vn_22, 0);
  lv_obj_set_style_text_color(notesTitle, lv_color_hex(0xFFD23F), 0);
  lv_obj_align(notesTitle, LV_ALIGN_TOP_MID, 0, 25);

  notesList = lv_obj_create(notesPanel);
  lv_obj_set_size(notesList, 200, 150);
  lv_obj_align(notesList, LV_ALIGN_CENTER, 0, 12);
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

  lv_obj_t* upBtn = lv_btn_create(notesPanel);
  lv_obj_set_size(upBtn, 100, 100);
  lv_obj_align(upBtn, LV_ALIGN_TOP_MID, 0, -65);
  lv_obj_set_style_bg_color(upBtn, lv_color_hex(0x303030), 0);
  lv_obj_set_style_radius(upBtn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_shadow_width(upBtn, 0, 0);
  lv_obj_t* upLabel = lv_label_create(upBtn);
  lv_obj_set_style_text_font(upLabel, &lv_font_montserrat_14, 0);
  lv_label_set_text(upLabel, LV_SYMBOL_UP);
  lv_obj_align(upLabel, LV_ALIGN_CENTER, 0, 30);
  lv_obj_add_event_cb(upBtn, [](lv_event_t* e){ MenuSystem::selectNotesPrev(); }, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* downBtn = lv_btn_create(notesPanel);
  lv_obj_set_size(downBtn, 100, 100);
  lv_obj_align(downBtn, LV_ALIGN_BOTTOM_MID, 0, 65);
  lv_obj_set_style_bg_color(downBtn, lv_color_hex(0x303030), 0);
  lv_obj_set_style_radius(downBtn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_shadow_width(downBtn, 0, 0);
  lv_obj_t* downLabel = lv_label_create(downBtn);
  lv_obj_set_style_text_font(downLabel, &lv_font_montserrat_14, 0);
  lv_label_set_text(downLabel, LV_SYMBOL_DOWN);
  lv_obj_align(downLabel, LV_ALIGN_CENTER, 0, -30);
  lv_obj_add_event_cb(downBtn, [](lv_event_t* e){ MenuSystem::selectNotesNext(); }, LV_EVENT_CLICKED, nullptr);
}

static void updateNoteDetailUI() {
  if (!noteDetailPanel) return;
  if (noteDetailIndex >= notesDisplayItems.size()) return;
  const auto& item = notesDisplayItems[noteDetailIndex];
  lv_label_set_text(noteDetailTitle, item.title.c_str());
  lv_label_set_text(noteDetailContent, item.content.c_str());
}

void createNoteDetailPanel() {
  if (noteDetailPanel != nullptr) return;

  noteDetailPanel = lv_obj_create(lv_screen_active());
  lv_obj_set_size(noteDetailPanel, 240, 240);
  lv_obj_center(noteDetailPanel);
  lv_obj_set_style_radius(noteDetailPanel, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(noteDetailPanel, lv_color_hex(COLOR_BACKGROUND), 0);
  lv_obj_set_style_bg_opa(noteDetailPanel, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(noteDetailPanel, 12, 0);
  lv_obj_set_style_border_color(noteDetailPanel, lv_color_hex(0xFFD23F), 0);
  lv_obj_set_style_border_opa(noteDetailPanel, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(noteDetailPanel, 0, 0);
  lv_obj_clear_flag(noteDetailPanel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(noteDetailPanel, LV_OBJ_FLAG_HIDDEN);

  noteDetailTitle = lv_label_create(noteDetailPanel);
  lv_obj_set_style_text_color(noteDetailTitle, lv_color_hex(0xFFD23F), 0);
  lv_obj_set_style_text_font(noteDetailTitle, &lv_font_montserrat_vn_22, 0);
  lv_label_set_text(noteDetailTitle, "");
  lv_obj_align(noteDetailTitle, LV_ALIGN_TOP_MID, 0, 30);
  lv_obj_set_width(noteDetailTitle, 180);
  lv_obj_set_style_text_align(noteDetailTitle, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(noteDetailTitle, LV_LABEL_LONG_DOT);

  noteDetailContent = lv_label_create(noteDetailPanel);
  lv_obj_set_style_text_color(noteDetailContent, lv_color_hex(COLOR_TEXT), 0);
  lv_obj_set_style_text_font(noteDetailContent, &lv_font_montserrat_vn_20, 0);
  lv_label_set_text(noteDetailContent, "");
  lv_obj_align(noteDetailContent, LV_ALIGN_CENTER, 0, 10);
  lv_obj_set_width(noteDetailContent, 160);
  lv_obj_set_style_text_align(noteDetailContent, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(noteDetailContent, LV_LABEL_LONG_WRAP);

  lv_obj_t* leftBtn = lv_btn_create(noteDetailPanel);
  lv_obj_set_size(leftBtn, 100, 100);
  lv_obj_align(leftBtn, LV_ALIGN_LEFT_MID, -50, 0);
  lv_obj_set_style_bg_color(leftBtn, lv_color_hex(0x303030), 0);
  lv_obj_set_style_radius(leftBtn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_shadow_width(leftBtn, 0, 0);
  lv_obj_t* leftLabel = lv_label_create(leftBtn);
  lv_obj_set_style_text_font(leftLabel, &lv_font_montserrat_14, 0);
  lv_label_set_text(leftLabel, LV_SYMBOL_LEFT);
  lv_obj_align(leftLabel, LV_ALIGN_CENTER, 28, 0);
  lv_obj_add_event_cb(leftBtn, [](lv_event_t* e){ MenuSystem::notesDetailPrev(); }, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* rightBtn = lv_btn_create(noteDetailPanel);
  lv_obj_set_size(rightBtn, 100, 100);
  lv_obj_align(rightBtn, LV_ALIGN_RIGHT_MID, 50, 0);
  lv_obj_set_style_bg_color(rightBtn, lv_color_hex(0x303030), 0);
  lv_obj_set_style_radius(rightBtn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_shadow_width(rightBtn, 0, 0);
  lv_obj_t* rightLabel = lv_label_create(rightBtn);
  lv_obj_set_style_text_font(rightLabel, &lv_font_montserrat_14, 0);
  lv_label_set_text(rightLabel, LV_SYMBOL_RIGHT);
  lv_obj_align(rightLabel, LV_ALIGN_CENTER, -28, 0);
  lv_obj_add_event_cb(rightBtn, [](lv_event_t* e){ MenuSystem::notesDetailNext(); }, LV_EVENT_CLICKED, nullptr);

  lv_obj_move_background(leftBtn);
  lv_obj_move_background(rightBtn);
}

// ── End notes ───────────────────────────────────────────────────

static bool isPointInside(lv_obj_t* obj, uint16_t x, uint16_t y) {
  if (!obj) return false;
  lv_area_t area;
  lv_obj_get_coords(obj, &area);
  return (x >= area.x1 && x <= area.x2 && y >= area.y1 && y <= area.y2);
}

static bool isTapOnPassword(uint16_t x, uint16_t y) {
  if (connectPassBtn) {
    lv_obj_update_layout(connectPassBtn);
    if (lv_obj_has_state(connectPassBtn, LV_STATE_DISABLED) ||
        lv_obj_has_flag(connectPassBtn, LV_OBJ_FLAG_HIDDEN)) {
      return false;
    }
    if (isPointInside(connectPassBtn, x, y)) {
      return true;
    }
  }
  // Fallback to the intended button bounds if layout hasn't resolved yet.
  if (wifiGetState() == WifiState::CONNECTED) {
    return false;
  }
  const int16_t cx = 120;
  const int16_t cy = 202;
  const int16_t halfW = 90;
  const int16_t halfH = 22;
  return (x >= (cx - halfW) && x <= (cx + halfW) &&
          y >= (cy - halfH) && y <= (cy + halfH));
}

static bool connectSwitchIsOn() {
  return connectSwitch && lv_obj_has_state(connectSwitch, LV_STATE_CHECKED);
}

static void setConnectSwitchState(bool on) {
  if (!connectSwitch) return;
  if (on) {
    lv_obj_add_state(connectSwitch, LV_STATE_CHECKED);
  } else {
    lv_obj_clear_state(connectSwitch, LV_STATE_CHECKED);
  }
}

static void syncConnectSwitchState() {
  WifiState s = wifiGetState();
  bool on = (s == WifiState::SCANNING ||
             s == WifiState::CONNECTING ||
             s == WifiState::CONNECTED);
  setConnectSwitchState(on);

  if (otaActive) {
    uint32_t nowMs = millis();
    float phase = 0.0f;
    if (OTA_BREATH_PERIOD_MS > 0) {
      phase = static_cast<float>((nowMs - otaStartMs) % OTA_BREATH_PERIOD_MS) /
              static_cast<float>(OTA_BREATH_PERIOD_MS);
    }
    float sWave = sinf(phase * 2.0f * 3.14159265f);
    float blend = 0.5f + 0.5f * sWave;
    uint8_t baseR = static_cast<uint8_t>((COLOR_CONNECT_OK >> 16) & 0xFF);
    uint8_t baseG = static_cast<uint8_t>((COLOR_CONNECT_OK >> 8) & 0xFF);
    uint8_t baseB = static_cast<uint8_t>(COLOR_CONNECT_OK & 0xFF);
    uint8_t r = static_cast<uint8_t>(baseR + (255 - baseR) * blend);
    uint8_t g = static_cast<uint8_t>(baseG + (255 - baseG) * blend);
    uint8_t b = static_cast<uint8_t>(baseB + (255 - baseB) * blend);
    if (connectPanel) {
      lv_obj_set_style_bg_color(connectPanel, lv_color_hex(COLOR_BACKGROUND), 0);
      lv_obj_set_style_border_color(connectPanel, lv_color_make(r, g, b), 0);
    }
    return;
  }

  // Update connect panel border to indicate state
  uint32_t color = 0xF44336; // red default
  switch (s) {
    case WifiState::SCANNING:     color = 0xFFC107; break; // yellow
    case WifiState::CONNECTING:   color = 0x2196F3; break; // blue
    case WifiState::CONNECTED:    color = COLOR_CONNECT_OK; break; // green
    case WifiState::FAILED:
    case WifiState::OFF:
    default: color = 0xF44336; break; // red
  }
  if (connectPanel) {
    lv_obj_set_style_bg_color(connectPanel, lv_color_hex(COLOR_BACKGROUND), 0);
    lv_obj_set_style_border_color(connectPanel, lv_color_hex(color), 0);
  }
}

static void resetKeyboardState() {
  keyboardLen = 0;
  keyboardLastKey = '\0';
  keyboardLastIndex = 0;
  keyboardLastTapMs = 0;
  keyboardText[0] = '\0';
  if (keyboardLabel) {
    lv_label_set_text(keyboardLabel, "");
  }
}

static const char* t9CharsForKey(char key) {
  switch (key) {
    case '0': return "0";
    case '1': return "1!@";
    case '2': return keyboardCaps ? "2ABC" : "2abc";
    case '3': return keyboardCaps ? "3DEF" : "3def";
    case '4': return keyboardCaps ? "4GHI" : "4ghi";
    case '5': return keyboardCaps ? "5JKL" : "5jkl";
    case '6': return keyboardCaps ? "6MNO" : "6mno";
    case '7': return keyboardCaps ? "7PQRS" : "7pqrs";
    case '8': return keyboardCaps ? "8TUV" : "8tuv";
    case '9': return keyboardCaps ? "9WXYZ" : "9wxyz";
    default: return "";
  }
}

static void updateKeyboardLabel() {
  if (!keyboardLabel) return;
  lv_label_set_text(keyboardLabel, keyboardText);
}

static void updateConnectPasswordLabel() {
  if (!connectPassValue) return;
  lv_label_set_text(connectPassValue, connectPassword);
}

static void loadKeyboardFromPassword() {
  keyboardLastKey = '\0';
  keyboardLastIndex = 0;
  keyboardLastTapMs = 0;
  strncpy(keyboardText, connectPassword, sizeof(keyboardText) - 1);
  keyboardText[sizeof(keyboardText) - 1] = '\0';
  keyboardLen = strlen(keyboardText);
  updateKeyboardLabel();
}

static void clearConnectPassword() {
  connectPassword[0] = '\0';
  resetKeyboardState();
  updateConnectPasswordLabel();
}

static void clearWifiSelection() {
  wifiSelectedIndex = -1;
  wifiSelectedSsid[0] = '\0';
  // Password button removed - no need to disable it
  clearConnectPassword();
  wifiListDirty = true;
}

static void selectWifiIndex(int index) {
  if (index < 0 || static_cast<size_t>(index) >= wifiGetScanCount()) {
    clearWifiSelection();
    wifiListDirty = true;
    return;
  }
  wifiSelectedIndex = index;
  const char* ssid = wifiGetScanSsid(static_cast<size_t>(index));
  strncpy(wifiSelectedSsid, ssid ? ssid : "", sizeof(wifiSelectedSsid) - 1);
  wifiSelectedSsid[sizeof(wifiSelectedSsid) - 1] = '\0';
  // Password button removed - keyboard opens directly on WiFi click
  clearConnectPassword();
  wifiListDirty = true;
}

static void wifiSelectCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  uintptr_t idx = reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));
  selectWifiIndex(static_cast<int>(idx));
  // Directly open keyboard for password input when WiFi is clicked
  MenuSystem::openKeyboardFromConnect();
}

static void rebuildWifiList() {
  if (!wifiList) return;
  lv_obj_clean(wifiList);
  size_t count = wifiGetScanCount();
  bool scanning = wifiIsScanning();
  if (scanning) {
    lv_obj_t* label = lv_label_create(wifiList);
    if (!label) return;
    lv_obj_set_style_text_color(label, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_label_set_text(label, "SCANNING...");
    lv_obj_center(label);
    return;
  }
  if (count == 0) {
    lv_obj_t* label = lv_label_create(wifiList);
    if (!label) return;
    lv_obj_set_style_text_color(label, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_label_set_text(label, "NO WIFI");
    lv_obj_center(label);
    return;
  }

  // Show all WiFi networks (user can scroll with UP/DOWN buttons)
  for (size_t i = 0; i < count; ++i) {
    const char* ssid = wifiGetScanSsid(i);
    lv_obj_t* btn = lv_btn_create(wifiList);
    if (!btn) break;  // OOM guard
    lv_obj_set_width(btn, lv_pct(100));
    lv_obj_set_style_radius(btn, 6, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x202020), 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 4, 0);
    if (static_cast<int>(i) == wifiSelectedIndex) {
      lv_obj_set_style_bg_color(btn, lv_color_hex(0x3A3A3A), 0);
    }
    lv_obj_t* label = lv_label_create(btn);
    if (!label) { lv_obj_del(btn); break; }  // OOM guard
    lv_obj_set_style_text_color(label, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(label, lv_pct(100));
    lv_label_set_text(label, ssid && ssid[0] ? ssid : "(hidden)");
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 6, 0);
    lv_obj_add_event_cb(btn, wifiSelectCb, LV_EVENT_CLICKED,
                        reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
  }
}

static void handleBackspace() {
  if (keyboardLen > 0) {
    keyboardLen--;
    keyboardText[keyboardLen] = '\0';
  }
  keyboardLastKey = '\0';
  keyboardLastIndex = 0;
  keyboardLastTapMs = 0;
  updateKeyboardLabel();
}

static void handleT9Key(char key) {
  uint32_t now = millis();
  if (key == '*') {
    if (keyboardLen + 1 < sizeof(keyboardText)) {
      keyboardText[keyboardLen++] = '*';
      keyboardText[keyboardLen] = '\0';
    }
    keyboardLastKey = key;
    keyboardLastIndex = 0;
    keyboardLastTapMs = now;
    updateKeyboardLabel();
    return;
  }
  if (key == '#') {
    if (keyboardLen + 1 < sizeof(keyboardText)) {
      keyboardText[keyboardLen++] = '#';
      keyboardText[keyboardLen] = '\0';
    }
    keyboardLastKey = key;
    keyboardLastIndex = 0;
    keyboardLastTapMs = now;
    updateKeyboardLabel();
    return;
  }
  const char* chars = t9CharsForKey(key);
  if (chars[0] == '\0') return;
  size_t charsLen = strlen(chars);
  if (key == keyboardLastKey && (now - keyboardLastTapMs) < T9_TAP_TIMEOUT_MS &&
      keyboardLen > 0) {
    keyboardLastIndex = (keyboardLastIndex + 1) % charsLen;
    keyboardText[keyboardLen - 1] = chars[keyboardLastIndex];
  } else {
    keyboardLastIndex = 0;
    if (keyboardLen + 1 < sizeof(keyboardText)) {
      keyboardText[keyboardLen++] = chars[keyboardLastIndex];
      keyboardText[keyboardLen] = '\0';
    }
  }
  keyboardLastKey = key;
  keyboardLastTapMs = now;
  updateKeyboardLabel();
  if (keyboardCaps) {
    keyboardCaps = false;
  }
}

static void submitWifiPasswordAndConnect() {
  strncpy(connectPassword, keyboardText, sizeof(connectPassword) - 1);
  connectPassword[sizeof(connectPassword) - 1] = '\0';
  updateConnectPasswordLabel();
  if (wifiSelectedSsid[0] == '\0') {
    MenuLog::println("[MenuSystem] Wi-Fi connect blocked: no SSID selected");
    return;
  }
  MenuSystem::closeKeyboardToConnect();
  wifiConnectTo(wifiSelectedSsid, connectPassword, true);
}

static void keyboardMatrixCb(lv_event_t* e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  lv_obj_t* obj = static_cast<lv_obj_t*>(lv_event_get_target(e));
  uint16_t btnId = lv_btnmatrix_get_selected_btn(obj);
  const char* txt = lv_btnmatrix_get_btn_text(obj, btnId);
  if (!txt || txt[0] == '\0') return;
  const char* p = txt;
  while (*p == ' ') ++p;
  if (*p == '\0') return;
  if (strcmp(p, "OK") == 0) {
    submitWifiPasswordAndConnect();
    return;
  }
  if (strcmp(p, LV_SYMBOL_LEFT) == 0) {
    handleBackspace();
    return;
  }
  if (strcmp(p, LV_SYMBOL_UP) == 0) {
    keyboardCaps = !keyboardCaps;
    return;
  }
  char key = *p;
  handleT9Key(key);
}

void createLevelPanel() {
  if (levelPanel != nullptr) return;

  levelPanel = lv_obj_create(lv_screen_active());
  lv_obj_set_size(levelPanel, 240, 240);
  lv_obj_center(levelPanel);
  lv_obj_set_style_radius(levelPanel, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(levelPanel, lv_color_hex(COLOR_BACKGROUND), 0);
  lv_obj_set_style_bg_opa(levelPanel, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(levelPanel, 12, 0);
  lv_obj_set_style_border_color(levelPanel, lv_color_hex(COLOR_PINK), 0);
  lv_obj_set_style_border_opa(levelPanel, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(levelPanel, 0, 0);
  lv_obj_clear_flag(levelPanel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(levelPanel, LV_OBJ_FLAG_HIDDEN);

  levelArc = lv_arc_create(levelPanel);
  lv_obj_set_size(levelArc, 200, 200);
  lv_obj_center(levelArc);
  lv_arc_set_rotation(levelArc, 135);
  lv_arc_set_bg_angles(levelArc, 0, 270);
  lv_arc_set_mode(levelArc, LV_ARC_MODE_NORMAL);
  lv_obj_remove_style(levelArc, nullptr, LV_PART_KNOB);
  lv_obj_set_style_arc_width(levelArc, 14, LV_PART_MAIN);
  lv_obj_set_style_arc_width(levelArc, 14, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(levelArc, lv_color_hex(0x202020), LV_PART_MAIN);

  levelLabel = lv_label_create(levelPanel);
  lv_obj_set_style_text_color(levelLabel, lv_color_hex(COLOR_TEXT), 0);
  lv_obj_set_style_text_font(levelLabel, &lv_font_montserrat_48, 0);
  lv_label_set_text(levelLabel, "1");
  lv_obj_center(levelLabel);
}

void updateLevelUI() {
  if (!levelPanel) return;

  int level = LevelSystem::getLevel();
  int current_xp = LevelSystem::getXP();
  int next_level_xp = LevelSystem::getXPForNextLevel();

  lv_arc_set_range(levelArc, 0, next_level_xp);
  lv_arc_set_value(levelArc, current_xp);

  lv_label_set_text_fmt(levelLabel, "%d", level);

  // Cycle through colors based on level
  const uint32_t level_colors[] = {0xFF7F50, 0x70C1FF, 0xFFD23F, 0x58F5C9, 0xDB1758, 0x9A3BFF};
  const int num_colors = sizeof(level_colors) / sizeof(level_colors[0]);
  uint32_t color = level_colors[(level - 1) % num_colors];
  
  lv_obj_set_style_arc_color(levelArc, lv_color_hex(color), LV_PART_INDICATOR);
  lv_obj_set_style_border_color(levelPanel, lv_color_hex(color), 0);
}

bool isLevelOpen() {
  return currentState == MENU_LEVEL_OPEN;
}

void showLevel() {
  levelOpenedFromCare = (currentState == MENU_CARE_OPEN);
  currentState = MENU_LEVEL_OPEN;
  if (levelOpenedFromCare) {
    lv_obj_add_flag(carePanel, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(menuPanel, LV_OBJ_FLAG_HIDDEN);
  }
  lv_obj_clear_flag(levelPanel, LV_OBJ_FLAG_HIDDEN);
  updateLevelUI();
  MenuLog::println("[MenuSystem] Level screen opened (Layer 2)");
}

void closeLevelToMenu() {
  if (currentState != MENU_LEVEL_OPEN) return;
  lv_obj_add_flag(levelPanel, LV_OBJ_FLAG_HIDDEN);
  if (levelOpenedFromCare) {
    lv_obj_clear_flag(carePanel, LV_OBJ_FLAG_HIDDEN);
    currentState = MENU_CARE_OPEN;
    selectedCareItem = CARE_LEVEL;
    scrollCareToIndex(static_cast<uint8_t>(selectedCareItem), LV_ANIM_OFF);
    MenuLog::println("[MenuSystem] Level screen closed -> back to care");
  } else {
    lv_obj_clear_flag(menuPanel, LV_OBJ_FLAG_HIDDEN);
    currentState = MENU_OPEN;
    selectedItem = MENU_CARE;
    scrollMenuToIndex(static_cast<uint8_t>(selectedItem), LV_ANIM_OFF);
    MenuLog::println("[MenuSystem] Level screen closed -> back to menu");
  }
  levelOpenedFromCare = false;
}

void createStatsPanel() {
  if (statsPanel != nullptr) return;

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
  lv_label_set_text(statsTitle, "Hunger");
  lv_obj_align(statsTitle, LV_ALIGN_TOP_MID, 0, 20);

  statsArc = lv_arc_create(statsPanel);
  lv_obj_set_size(statsArc, 240, 240);
  lv_obj_center(statsArc);
  lv_arc_set_rotation(statsArc, 135);
  lv_arc_set_bg_angles(statsArc, 0, 270);
  lv_arc_set_mode(statsArc, LV_ARC_MODE_NORMAL);
  lv_arc_set_range(statsArc, 0, 100);
  lv_obj_clear_flag(statsArc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_style(statsArc, nullptr, LV_PART_KNOB);  // hide knob
  lv_obj_set_style_arc_width(statsArc, 16, LV_PART_MAIN);
  lv_obj_set_style_arc_width(statsArc, 16, LV_PART_INDICATOR);

  // Center stat name inside arc
  lv_obj_align(statsTitle, LV_ALIGN_CENTER, 0, 0);

  // Add left/right buttons
  lv_obj_t* leftBtn = lv_btn_create(statsPanel);
  statsLeftBtn = leftBtn;
  lv_obj_set_size(leftBtn, 100, 100);
  lv_obj_align(leftBtn, LV_ALIGN_LEFT_MID, -50, 0);
  lv_obj_set_style_bg_color(leftBtn, lv_color_hex(0x303030), 0);
  lv_obj_set_style_radius(leftBtn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_shadow_width(leftBtn, 0, 0);
  lv_obj_t* leftLabel = lv_label_create(leftBtn);
  lv_obj_set_style_text_font(leftLabel, &lv_font_montserrat_14, 0);
  lv_label_set_text(leftLabel, LV_SYMBOL_LEFT);
  lv_obj_align(leftLabel, LV_ALIGN_CENTER, 28, 0);
  lv_obj_add_event_cb(leftBtn, [](lv_event_t* e){ MenuSystem::statsPrev(); }, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* rightBtn = lv_btn_create(statsPanel);
  statsRightBtn = rightBtn;
  lv_obj_set_size(rightBtn, 100, 100);
  lv_obj_align(rightBtn, LV_ALIGN_RIGHT_MID, 50, 0);
  lv_obj_set_style_bg_color(rightBtn, lv_color_hex(0x303030), 0);
  lv_obj_set_style_radius(rightBtn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_shadow_width(rightBtn, 0, 0);
  lv_obj_t* rightLabel = lv_label_create(rightBtn);
  lv_obj_set_style_text_font(rightLabel, &lv_font_montserrat_14, 0);
  lv_label_set_text(rightLabel, LV_SYMBOL_RIGHT);
  lv_obj_align(rightLabel, LV_ALIGN_CENTER, -28, 0);
  lv_obj_add_event_cb(rightBtn, [](lv_event_t* e){ MenuSystem::statsNext(); }, LV_EVENT_CLICKED, nullptr);

  // Keep nav buttons behind the arc ring.
  lv_obj_move_background(leftBtn);
  lv_obj_move_background(rightBtn);
}

void createOptionsPanel() {
  if (optionsPanel != nullptr) return;

  optionsPanel = lv_obj_create(lv_screen_active());
  lv_obj_set_size(optionsPanel, 240, 240);
  lv_obj_center(optionsPanel);
  lv_obj_set_style_radius(optionsPanel, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(optionsPanel, lv_color_hex(COLOR_BACKGROUND), 0);
  lv_obj_set_style_bg_opa(optionsPanel, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(optionsPanel, 12, 0);
  lv_obj_set_style_border_color(optionsPanel, lv_color_hex(COLOR_MINT), 0);
  lv_obj_set_style_border_opa(optionsPanel, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(optionsPanel, 0, 0);
  lv_obj_clear_flag(optionsPanel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(optionsPanel, LV_OBJ_FLAG_HIDDEN);

  optionsTitle = lv_label_create(optionsPanel);
  lv_obj_set_style_text_color(optionsTitle, lv_color_hex(COLOR_TEXT), 0);
  lv_obj_set_style_text_font(optionsTitle, &lv_font_montserrat_vn_22, 0);
  lv_label_set_text(optionsTitle, "Option");
  lv_obj_align(optionsTitle, LV_ALIGN_TOP_MID, 0, 30);

  optionsAction = lv_label_create(optionsPanel);
  lv_obj_set_style_text_color(optionsAction, lv_color_hex(COLOR_MINT), 0);
  lv_obj_set_style_text_font(optionsAction, &lv_font_montserrat_vn_22, 1);
  lv_label_set_text(optionsAction, "Action");
  lv_obj_align(optionsAction, LV_ALIGN_CENTER, 0, -5);
}

void createGamesPanel() {
  if (gamesPanel != nullptr) return;

  gamesPanel = lv_obj_create(lv_screen_active());
  lv_obj_set_size(gamesPanel, 240, 240);
  lv_obj_center(gamesPanel);
  lv_obj_set_style_radius(gamesPanel, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(gamesPanel, lv_color_hex(COLOR_BACKGROUND), 0);
  lv_obj_set_style_bg_opa(gamesPanel, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(gamesPanel, 12, 0);
  lv_obj_set_style_border_color(gamesPanel, lv_color_hex(COLOR_PINK), 0);
  lv_obj_set_style_border_opa(gamesPanel, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(gamesPanel, 0, 0);
  lv_obj_clear_flag(gamesPanel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(gamesPanel, LV_OBJ_FLAG_HIDDEN);

  gamesTitle = lv_label_create(gamesPanel);
  lv_obj_set_style_text_color(gamesTitle, lv_color_hex(COLOR_TEXT), 0);
  lv_obj_set_style_text_font(gamesTitle, &lv_font_montserrat_vn_22, 0);
  lv_label_set_text(gamesTitle, "Trò chơi");
  lv_obj_align(gamesTitle, LV_ALIGN_TOP_MID, 0, 24);

  gamesAction = lv_label_create(gamesPanel);
  lv_obj_set_style_text_color(gamesAction, lv_color_hex(COLOR_MINT), 0);
  lv_obj_set_style_text_font(gamesAction, &lv_font_montserrat_vn_22, 0);
  lv_label_set_text(gamesAction, "Chạm màu xanh");
  lv_obj_align(gamesAction, LV_ALIGN_CENTER, 0, -10);

  gamesStatus = lv_label_create(gamesPanel);
  lv_obj_set_style_text_color(gamesStatus, lv_color_hex(COLOR_TEXT), 0);
  lv_obj_set_style_text_font(gamesStatus, &lv_font_montserrat_vn_20, 0);
  lv_label_set_long_mode(gamesStatus, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(gamesStatus, 200);
  lv_obj_set_style_text_align(gamesStatus, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(gamesStatus, gameStatusMsg);
  lv_obj_align(gamesStatus, LV_ALIGN_CENTER, 0, 50);
}

void createSettingsPanel() {
  if (settingsPanel != nullptr) return;

  settingsPanel = lv_obj_create(lv_screen_active());
  lv_obj_set_size(settingsPanel, 240, 240);
  lv_obj_center(settingsPanel);
  lv_obj_set_style_radius(settingsPanel, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(settingsPanel, lv_color_hex(COLOR_BACKGROUND), 0);
  lv_obj_set_style_bg_opa(settingsPanel, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(settingsPanel, 12, 0);
  lv_obj_set_style_border_color(settingsPanel, lv_color_hex(0xFF6B6B), 0);
  lv_obj_set_style_border_opa(settingsPanel, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(settingsPanel, 0, 0);
  lv_obj_clear_flag(settingsPanel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(settingsPanel, LV_OBJ_FLAG_HIDDEN);

  settingsTitle = lv_label_create(settingsPanel);
  lv_obj_set_style_text_color(settingsTitle, lv_color_hex(COLOR_TEXT), 0);
  lv_obj_set_style_text_font(settingsTitle, &lv_font_montserrat_vn_22, 0);
  lv_label_set_text(settingsTitle, "Cài Đặt");
  lv_obj_align(settingsTitle, LV_ALIGN_TOP_MID, 0, 30);

  settingsResetBtn = lv_label_create(settingsPanel);
  lv_obj_set_style_text_color(settingsResetBtn, lv_color_hex(COLOR_MINT), 0);
  lv_obj_set_style_text_font(settingsResetBtn, &lv_font_montserrat_vn_22, 0);
  lv_label_set_text(settingsResetBtn, "CẬP NHẬT");
  lv_obj_align(settingsResetBtn, LV_ALIGN_CENTER, 0, 0);
  lv_obj_add_flag(settingsResetBtn, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(settingsResetBtn, [](lv_event_t* e){
    MenuLog::println("[MenuSystem] Entering firmware update mode...");

    // Use deep sleep with immediate wake to achieve true USB re-enumeration
    // Deep sleep completely powers down USB PHY, causing host to see disconnect
    // On wake (immediate), it's a fresh boot with proper USB enumeration
    esp_sleep_enable_timer_wakeup(1);  // Wake after 1 microsecond
    esp_deep_sleep_start();
  }, LV_EVENT_CLICKED, nullptr);

  // Chat toggle button
  settingsChatBtn = lv_label_create(settingsPanel);
  lv_obj_set_style_text_font(settingsChatBtn, &lv_font_montserrat_vn_22, 0);
  lv_obj_align(settingsChatBtn, LV_ALIGN_CENTER, 0, 40);
  lv_obj_add_flag(settingsChatBtn, LV_OBJ_FLAG_CLICKABLE);
  if (ChatSystem::isEnabled()) {
    lv_label_set_text(settingsChatBtn, "CHAT: BẬT");
    lv_obj_set_style_text_color(settingsChatBtn, lv_color_hex(COLOR_CONNECT_OK), 0);
  } else {
    lv_label_set_text(settingsChatBtn, "CHAT: TẮT");
    lv_obj_set_style_text_color(settingsChatBtn, lv_color_hex(COLOR_PINK), 0);
  }
  lv_obj_add_event_cb(settingsChatBtn, [](lv_event_t* e){
    if (ChatSystem::isEnabled()) {
      ChatSystem::disable();
      lv_label_set_text(settingsChatBtn, "CHAT: TẮT");
      lv_obj_set_style_text_color(settingsChatBtn, lv_color_hex(COLOR_PINK), 0);
    } else {
      ChatSystem::enable();
      lv_label_set_text(settingsChatBtn, "CHAT: BẬT");
      lv_obj_set_style_text_color(settingsChatBtn, lv_color_hex(COLOR_CONNECT_OK), 0);
    }
  }, LV_EVENT_CLICKED, nullptr);

  // Test mocking button (voice playback with pitch shift)
  settingsMockingBtn = lv_label_create(settingsPanel);
  lv_obj_set_style_text_color(settingsMockingBtn, lv_color_hex(COLOR_MINT), 0);
  lv_obj_set_style_text_font(settingsMockingBtn, &lv_font_montserrat_vn_20, 0);
  lv_label_set_text(settingsMockingBtn, "TEST MOCK");
  lv_obj_align(settingsMockingBtn, LV_ALIGN_CENTER, 0, 80);
  lv_obj_add_flag(settingsMockingBtn, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(settingsMockingBtn, [](lv_event_t* e){
    MenuLog::println("[Settings] TEST: Triggering mocking playback now");
    ChatSystem::testMockingNow();
  }, LV_EVENT_CLICKED, nullptr);
}

void createConnectPanel() {
  if (connectPanel != nullptr) return;

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
  lv_obj_set_style_text_color(connectTitle, lv_color_hex(COLOR_TEXT), 0);
  lv_obj_set_style_text_font(connectTitle, &lv_font_montserrat_vn_22, 0);
  lv_label_set_text(connectTitle, "Connect");
  lv_obj_align(connectTitle, LV_ALIGN_TOP_MID, 0, 14);

  connectRow = lv_obj_create(connectPanel);
  lv_obj_set_size(connectRow, 180, 46);
  lv_obj_align(connectRow, LV_ALIGN_TOP_MID, 0, 34);
  lv_obj_set_style_bg_opa(connectRow, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(connectRow, 0, 0);
  lv_obj_set_style_pad_all(connectRow, 0, 0);
  lv_obj_set_style_pad_column(connectRow, 16, 0);
  lv_obj_clear_flag(connectRow, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(connectRow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(connectRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  connectLabel = lv_label_create(connectRow);
  lv_obj_set_style_text_color(connectLabel, lv_color_hex(COLOR_TEXT), 0);
  lv_obj_set_style_text_font(connectLabel, &lv_font_montserrat_vn_20, 0);
  lv_label_set_text(connectLabel, "Wifi");

  connectSwitch = lv_switch_create(connectRow);
  syncConnectSwitchState();

  wifiList = lv_obj_create(connectPanel);
  lv_obj_set_size(wifiList, 150, 96);
  lv_obj_align(wifiList, LV_ALIGN_CENTER, 0, 20);
  lv_obj_set_style_radius(wifiList, 8, 0);
  lv_obj_set_style_bg_color(wifiList, lv_color_hex(0x101010), 0);
  lv_obj_set_style_bg_opa(wifiList, LV_OPA_100, 0);
  lv_obj_set_style_border_width(wifiList, 0, 0);
  lv_obj_set_style_pad_all(wifiList, 6, 0);
  lv_obj_set_style_pad_row(wifiList, 4, 0);
  lv_obj_clear_flag(wifiList, LV_OBJ_FLAG_SCROLL_MOMENTUM);
  lv_obj_set_scrollbar_mode(wifiList, LV_SCROLLBAR_MODE_AUTO);  // Enable scrollbar for scrolling
  lv_obj_set_flex_flow(wifiList, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(wifiList, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);

  connectOtaBtn = lv_btn_create(connectPanel);
  lv_obj_set_size(connectOtaBtn, 150, 48);
  lv_obj_align(connectOtaBtn, LV_ALIGN_CENTER, 0, 35);
  // LVGL “gummy” look: pill radius, soft gradient, shadow, outline
  lv_obj_set_style_radius(connectOtaBtn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(connectOtaBtn, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(connectOtaBtn, lv_color_hex(0xFF6FA5), 0);
  lv_obj_set_style_bg_grad_dir(connectOtaBtn, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_bg_grad_color(connectOtaBtn, lv_color_hex(0xFF3E7C), 0);
  lv_obj_set_style_shadow_width(connectOtaBtn, 16, 0);
  lv_obj_set_style_shadow_opa(connectOtaBtn, LV_OPA_50, 0);
  lv_obj_set_style_shadow_color(connectOtaBtn, lv_color_hex(0xC60F55), 0);
  lv_obj_set_style_outline_width(connectOtaBtn, 2, 0);
  lv_obj_set_style_outline_opa(connectOtaBtn, LV_OPA_40, 0);
  lv_obj_set_style_outline_color(connectOtaBtn, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_border_width(connectOtaBtn, 0, 0);
  lv_obj_set_style_pad_all(connectOtaBtn, 12, 0);
  // Pressed state: slightly darker, softer shadow
  lv_obj_set_style_bg_color(connectOtaBtn, lv_color_hex(0xFF4F8D), LV_STATE_PRESSED);
  lv_obj_set_style_bg_grad_color(connectOtaBtn, lv_color_hex(0xE73275), LV_STATE_PRESSED);
  lv_obj_set_style_shadow_opa(connectOtaBtn, LV_OPA_30, LV_STATE_PRESSED);
  // Gummy transition styles
  static lv_style_prop_t gum_props[] = {LV_STYLE_TRANSFORM_WIDTH, LV_STYLE_TRANSFORM_HEIGHT, LV_STYLE_TEXT_LETTER_SPACE, 0};
  static lv_style_transition_dsc_t gum_tr_def;
  static lv_style_transition_dsc_t gum_tr_pr;
  static lv_style_t gum_style_def;
  static lv_style_t gum_style_pr;
  static bool gum_init = false;
  if (!gum_init) {
    lv_style_transition_dsc_init(&gum_tr_def, gum_props, lv_anim_path_overshoot, 250, 100, nullptr);
    lv_style_transition_dsc_init(&gum_tr_pr, gum_props, lv_anim_path_ease_in_out, 250, 0, nullptr);
    lv_style_init(&gum_style_def);
    lv_style_set_transition(&gum_style_def, &gum_tr_def);
    lv_style_init(&gum_style_pr);
    lv_style_set_transform_width(&gum_style_pr, 10);
    lv_style_set_transform_height(&gum_style_pr, -10);
    lv_style_set_text_letter_space(&gum_style_pr, 10);
    lv_style_set_transition(&gum_style_pr, &gum_tr_pr);
    gum_init = true;
  }
  // Apply gummy styles to main part with proper state masks
  lv_obj_add_style(connectOtaBtn, &gum_style_pr, LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_add_style(connectOtaBtn, &gum_style_def, LV_PART_MAIN | LV_STATE_DEFAULT);

  lv_obj_t* otaLabel = lv_label_create(connectOtaBtn);
  lv_label_set_text(otaLabel, "CẬP NHẬT");
  lv_obj_set_style_text_color(otaLabel, lv_color_hex(COLOR_TEXT), 0);
  lv_obj_center(otaLabel);
  lv_obj_add_flag(connectOtaBtn, LV_OBJ_FLAG_HIDDEN);

  // Left/right buttons for scrolling WiFi list (centered on sides)
  lv_obj_t* leftBtn = lv_btn_create(connectPanel);
  lv_obj_set_size(leftBtn, 100, 100);
  lv_obj_align(leftBtn, LV_ALIGN_LEFT_MID, -50, 10);
  lv_obj_set_style_bg_color(leftBtn, lv_color_hex(0x303030), 0);
  lv_obj_set_style_radius(leftBtn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_shadow_width(leftBtn, 0, 0);
  lv_obj_t* leftLabel = lv_label_create(leftBtn);
  lv_obj_set_style_text_font(leftLabel, &lv_font_montserrat_14, 0);
  lv_label_set_text(leftLabel, LV_SYMBOL_LEFT);
  lv_obj_align(leftLabel, LV_ALIGN_CENTER, 28, 0);
  lv_obj_add_event_cb(leftBtn, [](lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (wifiList) {
      lv_obj_scroll_by(wifiList, 0, 30, LV_ANIM_ON);  // Scroll up
    }
  }, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* rightBtn = lv_btn_create(connectPanel);
  lv_obj_set_size(rightBtn, 100, 100);
  lv_obj_align(rightBtn, LV_ALIGN_RIGHT_MID, 50, 10);
  lv_obj_set_style_bg_color(rightBtn, lv_color_hex(0x303030), 0);
  lv_obj_set_style_radius(rightBtn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_shadow_width(rightBtn, 0, 0);
  lv_obj_t* rightLabel = lv_label_create(rightBtn);
  lv_obj_set_style_text_font(rightLabel, &lv_font_montserrat_14, 0);
  lv_label_set_text(rightLabel, LV_SYMBOL_RIGHT);
  lv_obj_align(rightLabel, LV_ALIGN_CENTER, -28, 0);
  lv_obj_add_event_cb(rightBtn, [](lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (wifiList) {
      lv_obj_scroll_by(wifiList, 0, -30, LV_ANIM_ON);  // Scroll down
    }
  }, LV_EVENT_CLICKED, nullptr);

  // Note: Removed PASSWORD button - users now tap WiFi name directly to enter password
  connectPassBtn = nullptr;
  connectPassValue = nullptr;
}

void createKeyboardPanel() {
  if (keyboardPanel != nullptr) return;

  keyboardPanel = lv_obj_create(lv_screen_active());
  lv_obj_set_size(keyboardPanel, 240, 240);
  lv_obj_center(keyboardPanel);
  lv_obj_set_style_radius(keyboardPanel, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(keyboardPanel, lv_color_hex(COLOR_BACKGROUND), 0);
  lv_obj_set_style_bg_opa(keyboardPanel, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(keyboardPanel, 0, 0);
  lv_obj_clear_flag(keyboardPanel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(keyboardPanel, LV_OBJ_FLAG_HIDDEN);

  keyboardTitle = lv_label_create(keyboardPanel);
  lv_obj_set_style_text_color(keyboardTitle, lv_color_hex(COLOR_TEXT), 0);
  lv_obj_set_style_text_font(keyboardTitle, &lv_font_montserrat_14, 0);
  lv_label_set_text(keyboardTitle, "PASSWORD");
  lv_obj_align(keyboardTitle, LV_ALIGN_TOP_MID, 0, 10);

  keyboardLabel = lv_label_create(keyboardPanel);
  lv_obj_set_style_text_color(keyboardLabel, lv_color_hex(COLOR_TEXT), 0);
  lv_obj_set_style_text_font(keyboardLabel, &lv_font_montserrat_14, 0);
  lv_label_set_long_mode(keyboardLabel, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(keyboardLabel, 200);
  lv_obj_align(keyboardLabel, LV_ALIGN_TOP_MID, 50, 34);
  lv_label_set_text(keyboardLabel, "");

  static const char* keyMap[] = {
    "1!@", "2 abc", "3 def", "\n",
    "4 ghi", "5 jkl", "6 mno", "\n",
    "7 pqrs", "8 tuv", "9 wxyz", "\n",
    "*", "0", "#", "\n",
    LV_SYMBOL_UP, "OK", LV_SYMBOL_LEFT, ""
  };

  keyboardMatrix = lv_btnmatrix_create(keyboardPanel);
  lv_btnmatrix_set_map(keyboardMatrix, keyMap);
  lv_obj_set_size(keyboardMatrix, 220, 170);
  lv_obj_align(keyboardMatrix, LV_ALIGN_BOTTOM_MID, 0, -10);
  lv_obj_set_style_bg_color(keyboardMatrix, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(keyboardMatrix, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(keyboardMatrix, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_row(keyboardMatrix, 2, LV_PART_MAIN);
  lv_obj_set_style_text_color(keyboardMatrix, lv_color_hex(0xFFFFFF), LV_PART_ITEMS);
  lv_obj_set_style_bg_color(keyboardMatrix, lv_color_hex(0x000000), LV_PART_ITEMS);
  lv_obj_set_style_bg_opa(keyboardMatrix, LV_OPA_COVER, LV_PART_ITEMS);
  lv_obj_set_style_shadow_width(keyboardMatrix, 0, LV_PART_ITEMS);
  lv_obj_set_style_shadow_opa(keyboardMatrix, LV_OPA_TRANSP, LV_PART_ITEMS);
  lv_obj_set_style_bg_color(keyboardMatrix, lv_color_hex(0x000000), LV_PART_ITEMS | LV_STATE_PRESSED);
  lv_obj_set_style_text_color(keyboardMatrix, lv_color_hex(0xFFFFFF), LV_PART_ITEMS | LV_STATE_PRESSED);
  lv_obj_set_style_text_font(keyboardMatrix, &lv_font_montserrat_14, LV_PART_ITEMS);
  lv_obj_add_event_cb(keyboardMatrix, keyboardMatrixCb, LV_EVENT_CLICKED, nullptr);
}

void updateStatsUI() {
  if (!statsPanel) return;
  lv_label_set_text(statsTitle, statNames[statIndex]);

  int value = 0;
  switch (statIndex) {
    case 0: value = CareSystem::getHunger(); break;
    case 1: value = CareSystem::getMood(); break;
    case 2: value = CareSystem::getEnergy(); break;
    case 3: value = CareSystem::getCleanliness(); break;
  }

  if (value < 0) value = 0;
  if (value > 100) value = 100;

  lv_arc_set_value(statsArc, value);
  lv_obj_set_style_arc_color(statsArc, lv_color_hex(statColors[statIndex]), LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(statsArc, lv_color_hex(0x202020), LV_PART_MAIN);
}

void updateOptionsUI() {
  if (!optionsPanel) return;
  lv_label_set_text(optionsTitle, statNames[statIndex]);
  lv_label_set_text(optionsAction, statOptionNames[statIndex]);

  // Highlight selection
  if (optionsSelection == OPTION_MAIN) {
    lv_obj_set_style_text_color(optionsAction, lv_color_hex(COLOR_MINT), 0);
  } else {
    lv_obj_set_style_text_color(optionsAction, lv_color_hex(COLOR_TEXT), 0);
  }
}

void updateGamesUI() {
  if (!gamesPanel) return;
  lv_label_set_text(gamesStatus, gameStatusMsg);
}

static void startFeedAnim() {
  CrashMonitor::setContext("feeding-start");
  feedAnimEndMs = millis() + FEED_ANIM_DURATION_MS;
  if (currentState == MENU_CARE_OPEN) {
    feedReturn = FEED_RETURN_CARE;
  } else if (currentState == MENU_STATS_OPEN || currentState == MENU_OPTIONS_OPEN) {
    feedReturn = FEED_RETURN_STATS;
  } else {
    feedReturn = FEED_RETURN_MENU;
  }
  currentState = MENU_FEEDING;
  if (feedReturn == FEED_RETURN_CARE) {
    lv_obj_add_flag(carePanel, LV_OBJ_FLAG_HIDDEN);
  } else if (feedReturn == FEED_RETURN_STATS) {
    lv_obj_add_flag(optionsPanel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(statsPanel, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(menuPanel, LV_OBJ_FLAG_HIDDEN);
  }
  MenuLog::println("[MenuSystem] Feed animation started");
}

}  // anonymous namespace

namespace MenuSystem {

void begin() {
  MenuLog::println("[MenuSystem] Initializing LVGL menu...");
  createCircularPanel();
  createMenuRoller();
  createCarePanel();
  createStatsPanel();
  createOptionsPanel();
  createGamesPanel();
  createConnectPanel();
  createKeyboardPanel();
  MessageSystem::begin();
  createLevelPanel();
  createSettingsPanel();
  createSleepPanel();
  createTestSoundPanel();
  createNotesPanel();
  createNoteDetailPanel();
  MockTestScreen::begin();  // Initialize mock test screen
  syncConnectSwitchState();
  MenuLog::println("[MenuSystem] Ready!");
}

void open() {
  if (currentState == MENU_OPEN) return;
  
  currentState = MENU_OPEN;
  selectedItem = MENU_CARE;
  MessageSystem::close();
  
  // Show panel with fade-in animation
  lv_obj_clear_flag(menuPanel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(carePanel, LV_OBJ_FLAG_HIDDEN);
  
  // Hide other panels if they were visible
  lv_obj_add_flag(statsPanel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(connectPanel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(keyboardPanel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(levelPanel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(sleepPanel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(testSoundPanel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(notesPanel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(noteDetailPanel, LV_OBJ_FLAG_HIDDEN);

  // Reset list to first item
  scrollMenuToIndex(0, LV_ANIM_OFF);
  
  MenuLog::println("[MenuSystem] Menu opened (Layer 1)");
}

void close() {
  if (currentState == MENU_CLOSED) return;
  
  currentState = MENU_CLOSED;
  feedAnimEndMs = 0;
  
  // Hide all panels
  lv_obj_add_flag(menuPanel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(carePanel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(statsPanel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(optionsPanel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(gamesPanel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(connectPanel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(keyboardPanel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(levelPanel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(settingsPanel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(sleepPanel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(testSoundPanel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(notesPanel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(noteDetailPanel, LV_OBJ_FLAG_HIDDEN);
  MessageSystem::close();
  
  MenuLog::println("[MenuSystem] Menu closed (back to Layer 0)");
}

bool isOpen() {
  return currentState == MENU_OPEN;
}

bool isCareOpen() {
  return currentState == MENU_CARE_OPEN;
}

bool isFeeding() {
  return currentState == MENU_FEEDING;
}

bool isLevelOpen() {
  return currentState == MENU_LEVEL_OPEN;
}

bool isConnectOpen() {
  return currentState == MENU_CONNECT_OPEN;
}

bool isKeyboardOpen() {
  return currentState == MENU_KEYBOARD_OPEN;
}

bool isMessageOpen() {
  return currentState == MENU_MESSAGE_OPEN;
}

bool isSettingsOpen() {
  return currentState == MENU_SETTINGS_OPEN;
}

static void showCare() {
  currentState = MENU_CARE_OPEN;
  lv_obj_add_flag(menuPanel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(carePanel, LV_OBJ_FLAG_HIDDEN);
  scrollCareToIndex(static_cast<uint8_t>(selectedCareItem), LV_ANIM_OFF);
  MenuLog::println("[MenuSystem] Care opened (Layer 2)");
}

static void showConnect() {
  currentState = MENU_CONNECT_OPEN;
  lv_obj_add_flag(menuPanel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(connectPanel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(connectPanel);
  syncConnectSwitchState();
  wifiListDirty = true;
  MenuLog::println("[MenuSystem] Connect opened (Layer 2)");
}

static void showKeyboard() {
  currentState = MENU_KEYBOARD_OPEN;
  lv_obj_add_flag(connectPanel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(keyboardPanel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(keyboardPanel);
  loadKeyboardFromPassword();
  MenuLog::println("[MenuSystem] Keyboard opened (Layer 3)");
}

void openKeyboardFromConnect() {
  if (currentState != MENU_CONNECT_OPEN) return;
  showKeyboard();
}

static void showMessage() {
  currentState = MENU_MESSAGE_OPEN;
  lv_obj_add_flag(menuPanel, LV_OBJ_FLAG_HIDDEN);
  MessageSystem::open(MenuSystem::closeMessageToMenu);
  MenuLog::println("[MenuSystem] Message opened (Layer 2)");
}

static void showTestSound() {
  currentState = MENU_TEST_SOUND_OPEN;
  lv_obj_add_flag(menuPanel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(testSoundPanel, LV_OBJ_FLAG_HIDDEN);
  scrollTestSoundToIndex(static_cast<uint8_t>(selectedTestSoundItem), LV_ANIM_OFF);
  MenuLog::println("[MenuSystem] Test sound opened");
}

void closeConnectToMenu() {
  if (currentState != MENU_CONNECT_OPEN) return;
  lv_obj_add_flag(connectPanel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(menuPanel, LV_OBJ_FLAG_HIDDEN);
  currentState = MENU_OPEN;
  selectedItem = MENU_CONNECT;
  scrollMenuToIndex(static_cast<uint8_t>(selectedItem), LV_ANIM_OFF);
  MenuLog::println("[MenuSystem] Connect closed -> back to menu");
}

void closeKeyboardToConnect() {
  if (currentState != MENU_KEYBOARD_OPEN) return;
  strncpy(connectPassword, keyboardText, sizeof(connectPassword) - 1);
  connectPassword[sizeof(connectPassword) - 1] = '\0';
  updateConnectPasswordLabel();
  lv_obj_add_flag(keyboardPanel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(connectPanel, LV_OBJ_FLAG_HIDDEN);
  currentState = MENU_CONNECT_OPEN;
  MenuLog::println("[MenuSystem] Keyboard closed -> back to connect");
}

void closeMessageToMenu() {
  if (currentState != MENU_MESSAGE_OPEN) return;
  MessageSystem::close();
  lv_obj_clear_flag(menuPanel, LV_OBJ_FLAG_HIDDEN);
  currentState = MENU_OPEN;
  selectedItem = MENU_MESSAGE;
  scrollMenuToIndex(static_cast<uint8_t>(selectedItem), LV_ANIM_OFF);
  MenuLog::println("[MenuSystem] Message closed -> back to menu");
}

void closeCareToMenu() {
  if (currentState != MENU_CARE_OPEN) return;
  lv_obj_add_flag(carePanel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(menuPanel, LV_OBJ_FLAG_HIDDEN);
  currentState = MENU_OPEN;
  selectedItem = MENU_CARE;
  scrollMenuToIndex(static_cast<uint8_t>(selectedItem), LV_ANIM_OFF);
  MenuLog::println("[MenuSystem] Care closed -> back to menu");
}

bool handleConnectTap(uint16_t x, uint16_t y) {
  if (currentState != MENU_CONNECT_OPEN) return false;
  if (isTapOnPassword(x, y)) {
    showKeyboard();
    return false;
  }
  if (connectOtaBtn && !lv_obj_has_flag(connectOtaBtn, LV_OBJ_FLAG_HIDDEN) &&
      isPointInside(connectOtaBtn, x, y)) {
    if (wifiGetState() == WifiState::CONNECTED) {
      MenuLog::println("[MenuSystem] OTA triggered from Connect");
      BubuOTA::runManual();
    } else {
      MenuLog::println("[MenuSystem] OTA blocked: Wi-Fi not connected");
    }
    return true;
  }
  if (isPointInside(connectSwitch, x, y)) {
    bool enable = !connectSwitchIsOn();
    setConnectSwitchState(enable);
    if (enable) {
      wifiScanStart();
      wifiScanVersionSeen = 0;
      wifiScanWasRunning = false;
      wifiListDirty = true;
    } else {
      wifiStop();
      clearWifiSelection();
      wifiListDirty = true;
    }
    return true;
  }
  return false;
}

void selectNext() {
  if (currentState != MENU_OPEN) return;
  
  int current = static_cast<int>(selectedItem);
  if (current < MENU_ITEM_COUNT - 1) {
    uint8_t next = static_cast<uint8_t>(current + 1);
    scrollMenuToIndex(next, LV_ANIM_ON);
    MenuLog::printf("[MenuSystem] Selected: %d\n", selectedItem);
  }
}

void selectPrev() {
  if (currentState != MENU_OPEN) return;
  
  int current = static_cast<int>(selectedItem);
  if (current > 0) {
    uint8_t prev = static_cast<uint8_t>(current - 1);
    scrollMenuToIndex(prev, LV_ANIM_ON);
    MenuLog::printf("[MenuSystem] Selected: %d\n", selectedItem);
  }
}

void selectCareNext() {
  if (currentState != MENU_CARE_OPEN) return;

  int current = static_cast<int>(selectedCareItem);
  if (current < CARE_ITEM_COUNT - 1) {
    uint8_t next = static_cast<uint8_t>(current + 1);
    scrollCareToIndex(next, LV_ANIM_ON);
    MenuLog::printf("[MenuSystem] Care selected: %d\n", selectedCareItem);
  }
}

void selectCarePrev() {
  if (currentState != MENU_CARE_OPEN) return;

  int current = static_cast<int>(selectedCareItem);
  if (current > 0) {
    uint8_t prev = static_cast<uint8_t>(current - 1);
    scrollCareToIndex(prev, LV_ANIM_ON);
    MenuLog::printf("[MenuSystem] Care selected: %d\n", selectedCareItem);
  }
}

MenuItem getSelected() {
  return selectedItem;
}

size_t getCurrentStatIndex() {
  return statIndex;
}

bool isStatsOpen() {
  return currentState == MENU_STATS_OPEN;
}

void showStats() {
  statsOpenedFromCare = (currentState == MENU_CARE_OPEN);
  currentState = MENU_STATS_OPEN;
  if (statsOpenedFromCare) {
    lv_obj_add_flag(carePanel, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(menuPanel, LV_OBJ_FLAG_HIDDEN);
  }
  lv_obj_clear_flag(statsPanel, LV_OBJ_FLAG_HIDDEN);
  updateStatsUI();
  MenuLog::println("[MenuSystem] Stats opened (Layer 2)");
}

void closeStatsToMenu() {
  if (currentState != MENU_STATS_OPEN) return;
  lv_obj_add_flag(statsPanel, LV_OBJ_FLAG_HIDDEN);
  if (statsOpenedFromCare) {
    lv_obj_clear_flag(carePanel, LV_OBJ_FLAG_HIDDEN);
    currentState = MENU_CARE_OPEN;
    selectedCareItem = CARE_STATS;
    scrollCareToIndex(static_cast<uint8_t>(selectedCareItem), LV_ANIM_OFF);
    MenuLog::println("[MenuSystem] Stats closed -> back to care");
  } else {
    lv_obj_clear_flag(menuPanel, LV_OBJ_FLAG_HIDDEN);
    currentState = MENU_OPEN;
    selectedItem = MENU_CARE;
    scrollMenuToIndex(static_cast<uint8_t>(selectedItem), LV_ANIM_OFF);
    MenuLog::println("[MenuSystem] Stats closed -> back to menu");
  }
  statsOpenedFromCare = false;
}

void closeSettingsToMenu() {
  if (currentState != MENU_SETTINGS_OPEN) return;
  lv_obj_add_flag(settingsPanel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(menuPanel, LV_OBJ_FLAG_HIDDEN);
  currentState = MENU_OPEN;
  selectedItem = MENU_SETTINGS;
  scrollMenuToIndex(static_cast<uint8_t>(selectedItem), LV_ANIM_OFF);
  MenuLog::println("[MenuSystem] Settings closed -> back to menu");
}

void closeLevelToMenu() {
  if (currentState != MENU_LEVEL_OPEN) return;
  lv_obj_add_flag(levelPanel, LV_OBJ_FLAG_HIDDEN);
  if (levelOpenedFromCare) {
    lv_obj_clear_flag(carePanel, LV_OBJ_FLAG_HIDDEN);
    currentState = MENU_CARE_OPEN;
    selectedCareItem = CARE_LEVEL;
    scrollCareToIndex(static_cast<uint8_t>(selectedCareItem), LV_ANIM_OFF);
    MenuLog::println("[MenuSystem] Level screen closed -> back to care");
  } else {
    lv_obj_clear_flag(menuPanel, LV_OBJ_FLAG_HIDDEN);
    currentState = MENU_OPEN;
    selectedItem = MENU_CARE;
    scrollMenuToIndex(static_cast<uint8_t>(selectedItem), LV_ANIM_OFF);
    MenuLog::println("[MenuSystem] Level screen closed -> back to menu");
  }
  levelOpenedFromCare = false;
}

void startCleanAnimation() {
  DisplaySystem_startClean();
  close();
}

void statsNext() {
  if (currentState != MENU_STATS_OPEN) return;
  statIndex = (statIndex + 1) % STAT_COUNT;
  updateStatsUI();
}

void statsPrev() {
  if (currentState != MENU_STATS_OPEN) return;
  statIndex = (statIndex + STAT_COUNT - 1) % STAT_COUNT;
  updateStatsUI();
}

bool isOptionsOpen() {
  return currentState == MENU_OPTIONS_OPEN;
}

bool isGamesOpen() {
  return currentState == MENU_GAMES_OPEN;
}

bool isGameActive() {
  return currentState == MENU_GAME_ACTIVE;
}

void openOptionsForCurrentStat() {
  if (currentState != MENU_STATS_OPEN) return;
  currentState = MENU_OPTIONS_OPEN;
  optionsSelection = OPTION_MAIN;
  lv_obj_add_flag(statsPanel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(optionsPanel, LV_OBJ_FLAG_HIDDEN);
  updateOptionsUI();
  MenuLog::printf("[MenuSystem] Options opened for %s (Layer 3)\n", statNames[statIndex]);
}

void closeOptionsToStats() {
  if (currentState != MENU_OPTIONS_OPEN) return;
  lv_obj_add_flag(optionsPanel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(statsPanel, LV_OBJ_FLAG_HIDDEN);
  currentState = MENU_STATS_OPEN;
  updateStatsUI();
  MenuLog::println("[MenuSystem] Options closed -> back to stats");
}

void openGamesMenu() {
  bool fromStats = (currentState == MENU_STATS_OPEN);
  bool fromCare = (currentState == MENU_CARE_OPEN);
  if (!fromStats && !fromCare) return;
  if (fromStats && statIndex != 1) return;  // Stats path only when Mood

  gamesOpenedFromCare = fromCare;
  currentState = MENU_GAMES_OPEN;

  if (fromStats) {
    lv_obj_add_flag(statsPanel, LV_OBJ_FLAG_HIDDEN);
  } else if (fromCare) {
    lv_obj_add_flag(carePanel, LV_OBJ_FLAG_HIDDEN);
  }

  lv_obj_clear_flag(gamesPanel, LV_OBJ_FLAG_HIDDEN);
  updateGamesUI();
  MenuLog::println(fromCare ? "[MenuSystem] Games menu opened from Care (Layer 4)" : "[MenuSystem] Games menu opened (Layer 4)");
}

void closeGamesToStats() {
  if (currentState != MENU_GAMES_OPEN) return;
  lv_obj_add_flag(gamesPanel, LV_OBJ_FLAG_HIDDEN);
  if (gamesOpenedFromCare) {
    lv_obj_clear_flag(carePanel, LV_OBJ_FLAG_HIDDEN);
    currentState = MENU_CARE_OPEN;
    selectedCareItem = CARE_PLAY;
    scrollCareToIndex(static_cast<uint8_t>(selectedCareItem), LV_ANIM_OFF);
    MenuLog::println("[MenuSystem] Games closed -> back to care");
  } else {
    lv_obj_clear_flag(statsPanel, LV_OBJ_FLAG_HIDDEN);
    currentState = MENU_STATS_OPEN;
    updateStatsUI();
    MenuLog::println("[MenuSystem] Games closed -> back to stats");
  }
  gamesOpenedFromCare = false;
}

void startTapTheGreens() {
  if (currentState != MENU_GAMES_OPEN) return;
  lv_obj_add_flag(gamesPanel, LV_OBJ_FLAG_HIDDEN);
  currentState = MENU_GAME_ACTIVE;
  strncpy(gameStatusMsg, "Playing...", sizeof(gameStatusMsg) - 1);
  EyeGame::start(CareSystem::STAT_MOOD);
  MenuLog::println("[MenuSystem] Starting Tap the Greens (Layer 5)");
}

void handleGameFinished() {
  if (currentState != MENU_GAME_ACTIVE && currentState != MENU_GAMES_OPEN) return;
  EyeGame::GameResult res = EyeGame::getLastResult();
  uint8_t score = EyeGame::getScore();
  int reward = static_cast<int>(score) * static_cast<int>(EyeGame::getRewardPerHit());

  switch (res) {
    case EyeGame::GAME_FINISH_NORMAL:
      snprintf(gameStatusMsg, sizeof(gameStatusMsg), "Giỏi quá! %u (+%d Tâm trạng)", score, reward);
      break;
    case EyeGame::GAME_FINISH_WRONG_TAP:
      snprintf(gameStatusMsg, sizeof(gameStatusMsg), "SAI RỒI! %+d Tâm trạng, -5 Năng lượng", reward - 10);
      break;
    case EyeGame::GAME_NONE:
    default:
      snprintf(gameStatusMsg, sizeof(gameStatusMsg), "Stopped");
      break;
  }

  currentState = MENU_GAMES_OPEN;
  lv_obj_clear_flag(gamesPanel, LV_OBJ_FLAG_HIDDEN);
  updateGamesUI();
  MenuLog::println("[MenuSystem] Game finished -> back to games menu");
  // Celebrate play session
  DisplaySystem_showCareHappy();
}

void otaSetActive(bool active) {
  otaActive = active;
  if (active) {
    otaStartMs = millis();
    otaLastTickMs = otaStartMs;
    syncConnectSwitchState();
  } else {
    otaStartMs = 0;
    otaLastTickMs = 0;
    syncConnectSwitchState();
  }
}

void otaPulse(uint32_t nowMs) {
  if (!otaActive) return;
  if (currentState != MENU_CONNECT_OPEN) return;
  if (!connectPanel) return;

  if (otaLastTickMs == 0 || nowMs < otaLastTickMs) {
    otaLastTickMs = nowMs;
  } else {
    uint32_t dt = nowMs - otaLastTickMs;
    if (dt > 0) {
      lv_tick_inc(dt);
      otaLastTickMs = nowMs;
    }
  }

  float phase = 0.0f;
  if (OTA_BREATH_PERIOD_MS > 0) {
    phase = static_cast<float>((nowMs - otaStartMs) % OTA_BREATH_PERIOD_MS) /
            static_cast<float>(OTA_BREATH_PERIOD_MS);
  }
  float sWave = sinf(phase * 2.0f * 3.14159265f);
  float blend = 0.5f + 0.5f * sWave;
  uint8_t baseR = static_cast<uint8_t>((COLOR_CONNECT_OK >> 16) & 0xFF);
  uint8_t baseG = static_cast<uint8_t>((COLOR_CONNECT_OK >> 8) & 0xFF);
  uint8_t baseB = static_cast<uint8_t>(COLOR_CONNECT_OK & 0xFF);
  uint8_t r = static_cast<uint8_t>(baseR + (255 - baseR) * blend);
  uint8_t g = static_cast<uint8_t>(baseG + (255 - baseG) * blend);
  uint8_t b = static_cast<uint8_t>(baseB + (255 - baseB) * blend);
  lv_obj_set_style_bg_color(connectPanel, lv_color_hex(COLOR_BACKGROUND), 0);
  lv_obj_set_style_border_color(connectPanel, lv_color_make(r, g, b), 0);
  lv_timer_handler();
}

void activateCurrentOption() {
  if (currentState != MENU_OPTIONS_OPEN) return;
  if (optionsSelection == OPTION_MAIN) {
    MenuLog::printf("[MenuSystem] Activate option: %s (%s)\n", statNames[statIndex], statOptionNames[statIndex]);
    // Simple stat boosts per option
    switch (statIndex) {
      case 0: startFeedAnim(); return;                                         // Sandwich (feed anim)
      case 1: CareSystem::addMood(CareSystem::kGamesBoost); break;             // Games
      case 2: CareSystem::addEnergy(CareSystem::kSleepBoost); break;           // Sleep
      case 3: CareSystem::addCleanliness(CareSystem::kBathBoost); break;       // Bath
    }
    updateStatsUI();
    closeOptionsToStats();
  } else {
    updateOptionsUI();
  }
}

void selectOptionsPrev() {
  if (currentState != MENU_OPTIONS_OPEN) return;
  optionsSelection = OPTION_MAIN;
  updateOptionsUI();
}

void selectOptionsNext() {
  if (currentState != MENU_OPTIONS_OPEN) return;
  optionsSelection = OPTION_MAIN;
  updateOptionsUI();
}

void activateCareSelected() {
  if (currentState != MENU_CARE_OPEN) return;

  MenuLog::printf("[MenuSystem] Care activated: %s\n", careItemLabelTexts[selectedCareItem]);

  switch (selectedCareItem) {
    case CARE_FEED:
      startFeedAnim();
      break;
    case CARE_PLAY:
      openGamesMenu();
      startTapTheGreens();
      break;
    case CARE_CLEAN:
      startCleanAnimation();
      break;
    case CARE_SLEEP:
      // Open sleep submenu
      currentState = MENU_SLEEP_OPEN;
      lv_obj_add_flag(carePanel, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(sleepPanel, LV_OBJ_FLAG_HIDDEN);
      scrollSleepToIndex(static_cast<uint8_t>(selectedSleepItem), LV_ANIM_OFF);
      break;
    case CARE_STATS:
      showStats();
      break;
    case CARE_LEVEL:
      showLevel();
      break;
    case CARE_ITEM_COUNT:
      break;
  }
}

void selectSleepNext() {
  if (currentState != MENU_SLEEP_OPEN) return;
  if (selectedSleepItem < SLEEP_ITEM_COUNT - 1) {
    scrollSleepToIndex(static_cast<uint8_t>(selectedSleepItem) + 1, LV_ANIM_ON);
  }
}

void selectSleepPrev() {
  if (currentState != MENU_SLEEP_OPEN) return;
  if (selectedSleepItem > 0) {
    scrollSleepToIndex(static_cast<uint8_t>(selectedSleepItem) - 1, LV_ANIM_ON);
  }
}

void activateSleepSelected() {
  if (currentState != MENU_SLEEP_OPEN) return;

  MenuLog::printf("[MenuSystem] Sleep activated: %s\n", sleepItemLabelTexts[selectedSleepItem]);

  switch (selectedSleepItem) {
    case SLEEP_ZZZ:
      DisplaySystem_startSleep();
      close();
      break;
    case SLEEP_WHITE_NOISE:
      DisplaySystem_startWhiteNoiseSleep();
      close();
      break;
    case SLEEP_RAIN:
      DisplaySystem_startRainSleep();
      close();
      break;
    case SLEEP_ITEM_COUNT:
      break;
  }
}

void closeSleepToCare() {
  if (currentState != MENU_SLEEP_OPEN) return;
  currentState = MENU_CARE_OPEN;
  lv_obj_add_flag(sleepPanel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(carePanel, LV_OBJ_FLAG_HIDDEN);
  scrollCareToIndex(static_cast<uint8_t>(selectedCareItem), LV_ANIM_OFF);
}

bool isSleepOpen() {
  return currentState == MENU_SLEEP_OPEN;
}

bool isTapOnSleepSelected(uint16_t x, uint16_t y) {
  if (currentState != MENU_SLEEP_OPEN) return false;
  if (sleepItems[selectedSleepItem]) {
    lv_obj_update_layout(sleepItems[selectedSleepItem]);
    return isPointInside(sleepItems[selectedSleepItem], x, y);
  }
  return false;
}

// ── Notes navigation ────────────────────────────────────────────

static void showNotes() {
  currentState = MENU_NOTES_OPEN;
  lv_obj_add_flag(menuPanel, LV_OBJ_FLAG_HIDDEN);
  rebuildNotesList();
  lv_obj_clear_flag(notesPanel, LV_OBJ_FLAG_HIDDEN);
  MenuLog::println("[MenuSystem] Notes opened (Layer 2)");
}

bool isNotesOpen() {
  return currentState == MENU_NOTES_OPEN;
}

bool isNoteDetailOpen() {
  return currentState == MENU_NOTE_DETAIL_OPEN;
}

void selectNotesNext() {
  if (currentState != MENU_NOTES_OPEN) return;
  if (notesSelectedIndex < notesItemCount - 1) {
    scrollNotesToIndex(notesSelectedIndex + 1, LV_ANIM_ON);
  }
}

void selectNotesPrev() {
  if (currentState != MENU_NOTES_OPEN) return;
  if (notesSelectedIndex > 0) {
    scrollNotesToIndex(notesSelectedIndex - 1, LV_ANIM_ON);
  }
}

void activateNotesSelected() {
  if (currentState != MENU_NOTES_OPEN) return;
  if (notesDisplayItems.empty()) return;

  noteDetailIndex = notesSelectedIndex;
  currentState = MENU_NOTE_DETAIL_OPEN;
  lv_obj_add_flag(notesPanel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(noteDetailPanel, LV_OBJ_FLAG_HIDDEN);
  updateNoteDetailUI();
  MenuLog::println("[MenuSystem] Note detail opened (Layer 3)");
}

void closeNotesToMenu() {
  if (currentState != MENU_NOTES_OPEN) return;
  lv_obj_add_flag(notesPanel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(menuPanel, LV_OBJ_FLAG_HIDDEN);
  currentState = MENU_OPEN;
  selectedItem = MENU_NOTES;
  scrollMenuToIndex(static_cast<uint8_t>(selectedItem), LV_ANIM_OFF);
  MenuLog::println("[MenuSystem] Notes closed -> back to menu");
}

void closeNoteDetailToNotes() {
  if (currentState != MENU_NOTE_DETAIL_OPEN) return;
  lv_obj_add_flag(noteDetailPanel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(notesPanel, LV_OBJ_FLAG_HIDDEN);
  currentState = MENU_NOTES_OPEN;
  scrollNotesToIndex(notesSelectedIndex, LV_ANIM_OFF);
  MenuLog::println("[MenuSystem] Note detail closed -> back to notes list");
}

void notesDetailNext() {
  if (currentState != MENU_NOTE_DETAIL_OPEN) return;
  if (notesDisplayItems.empty()) return;
  noteDetailIndex = (noteDetailIndex + 1) % notesDisplayItems.size();
  updateNoteDetailUI();
}

void notesDetailPrev() {
  if (currentState != MENU_NOTE_DETAIL_OPEN) return;
  if (notesDisplayItems.empty()) return;
  noteDetailIndex = (noteDetailIndex + notesDisplayItems.size() - 1) % notesDisplayItems.size();
  updateNoteDetailUI();
}

bool isTapOnNotesSelected(uint16_t x, uint16_t y) {
  if (currentState != MENU_NOTES_OPEN) return false;
  if (notesSelectedIndex >= notesItemCount) return false;
  lv_obj_t* item = notesItemLabels[notesSelectedIndex];
  if (item) {
    lv_obj_update_layout(item);
    return isPointInside(item, x, y);
  }
  return false;
}

// ── End notes navigation ────────────────────────────────────────

void activateSelected() {
  if (currentState != MENU_OPEN) return;

  MenuLog::printf("[MenuSystem] Activated: %s\n", menuItemLabelTexts[selectedItem]);

  switch (selectedItem) {
    case MENU_CARE:
      showCare();
      break;
    case MENU_CONNECT:
      showConnect();
      break;
    case MENU_MESSAGE:
      showMessage();
      break;
    case MENU_NOTES:
      showNotes();
      break;
    case MENU_SETTINGS:
      currentState = MENU_SETTINGS_OPEN;
      lv_obj_add_flag(menuPanel, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(settingsPanel, LV_OBJ_FLAG_HIDDEN);
      break;
    case MENU_TEST_SOUND:
      showTestSound();
      break;
    case MENU_MOCK:
      // Open interactive mock test screen
      MenuLog::println("[MenuSystem] TEST MOCK: Opening interactive test screen");
      MockTestScreen::open();
      MenuSystem::close();
      break;
    case MENU_ITEM_COUNT:
      break;
  }
}

// ── Test Sound navigation ────────────────────────────────────────────

void selectTestSoundNext() {
  if (currentState != MENU_TEST_SOUND_OPEN) return;
  if (selectedTestSoundItem < TEST_SOUND_ITEM_COUNT - 1) {
    scrollTestSoundToIndex(static_cast<uint8_t>(selectedTestSoundItem) + 1, LV_ANIM_ON);
  }
}

void selectTestSoundPrev() {
  if (currentState != MENU_TEST_SOUND_OPEN) return;
  if (selectedTestSoundItem > 0) {
    scrollTestSoundToIndex(static_cast<uint8_t>(selectedTestSoundItem) - 1, LV_ANIM_ON);
  }
}

void activateTestSoundSelected() {
  MenuLog::printf("[MenuSystem] activateTestSoundSelected() called - state=%d TEST_SOUND_OPEN=%d\n", currentState, MENU_TEST_SOUND_OPEN);
  if (currentState != MENU_TEST_SOUND_OPEN) {
    MenuLog::println("[MenuSystem] TEST SOUND not open, returning");
    return;
  }

  MenuLog::printf("[MenuSystem] Test sound activated: %s\n", testSoundItemLabelTexts[selectedTestSoundItem]);

  // Play the selected sound effect
  switch (selectedTestSoundItem) {
    case TEST_SOUND_HAPPY1:
      SoundEffects::playHappy1();
      break;
    case TEST_SOUND_CURIOUS:
      SoundEffects::playCurious();
      break;
    case TEST_SOUND_SAD1:
      SoundEffects::playSad1();
      break;
    case TEST_SOUND_ANGRY1:
      SoundEffects::playAngry1();
      break;
    case TEST_SOUND_TIRED:
      SoundEffects::playTired();
      break;
    case TEST_SOUND_EXCITED:
      SoundEffects::playExcited();
      break;
    case TEST_SOUND_LOVE:
      SoundEffects::playLove();
      break;
    case TEST_SOUND_WORRIED:
      SoundEffects::playWorried();
      break;
    case TEST_SOUND_ITEM_COUNT:
      break;
  }
}

void closeTestSoundToMenu() {
  if (currentState != MENU_TEST_SOUND_OPEN) return;
  currentState = MENU_OPEN;
  lv_obj_add_flag(testSoundPanel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(menuPanel, LV_OBJ_FLAG_HIDDEN);
  scrollMenuToIndex(static_cast<uint8_t>(selectedItem), LV_ANIM_OFF);
}

bool isTestSoundOpen() {
  return currentState == MENU_TEST_SOUND_OPEN;
}

bool isTapOnTestSoundSelected(uint16_t x, uint16_t y) {
  if (currentState != MENU_TEST_SOUND_OPEN) return false;
  if (testSoundItems[selectedTestSoundItem]) {
    lv_obj_update_layout(testSoundItems[selectedTestSoundItem]);
    return isPointInside(testSoundItems[selectedTestSoundItem], x, y);
  }
  return false;
}

void render() {
  if (currentState == MENU_STATS_OPEN) {
    updateStatsUI();
  } else if (currentState == MENU_LEVEL_OPEN) {
    updateLevelUI();
  } else if (currentState == MENU_OPTIONS_OPEN) {
    updateOptionsUI();
  } else if (currentState == MENU_GAMES_OPEN) {
    updateGamesUI();
  } else if (currentState == MENU_FEEDING) {
    if (feedAnimEndMs != 0 && millis() >= feedAnimEndMs) {
      feedAnimEndMs = 0;
      CareSystem::addHunger(CareSystem::kSandwichBoost); // Apply feed after anim
      CrashMonitor::clearContext();
      DisplaySystem_showCareHappy();           // end on happy face
      close();                                 // return to layer 0 instead of reopening menus
      feedReturn = FEED_RETURN_MENU;
    }
  } else if (currentState == MENU_KEYBOARD_OPEN) {
    if (keyboardPanel) {
      lv_obj_clear_flag(keyboardPanel, LV_OBJ_FLAG_HIDDEN);
    }
    if (connectPanel) {
      lv_obj_add_flag(connectPanel, LV_OBJ_FLAG_HIDDEN);
    }
  } else if (currentState == MENU_CONNECT_OPEN) {
    syncConnectSwitchState();
    bool scanRunning = wifiIsScanning();
    if (scanRunning != wifiScanWasRunning) {
      wifiScanWasRunning = scanRunning;
      wifiListDirty = true;
    }
    uint32_t scanVersion = wifiGetScanVersion();
    if (scanVersion != wifiScanVersionSeen) {
      wifiScanVersionSeen = scanVersion;
      wifiListDirty = true;
    }
    if (wifiSelectedSsid[0] != '\0') {
      int foundIdx = -1;
      size_t count = wifiGetScanCount();
      for (size_t i = 0; i < count; ++i) {
        const char* ssid = wifiGetScanSsid(i);
        if (ssid && strcmp(ssid, wifiSelectedSsid) == 0) {
          foundIdx = static_cast<int>(i);
          break;
        }
      }
      if (foundIdx < 0) {
        clearWifiSelection();
      } else if (foundIdx != wifiSelectedIndex) {
        wifiSelectedIndex = foundIdx;
        wifiListDirty = true;
      }
    }
    if (wifiListDirty) {
      rebuildWifiList();
      wifiListDirty = false;
    }
    bool connected = (wifiGetState() == WifiState::CONNECTED);
    if (connectOtaBtn) {
      if (connected) {
        lv_obj_clear_flag(connectOtaBtn, LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_obj_add_flag(connectOtaBtn, LV_OBJ_FLAG_HIDDEN);
      }
    }
    if (wifiList) {
      lv_obj_clear_flag(wifiList, LV_OBJ_FLAG_HIDDEN);
    }
    if (connectPassBtn) {
      if (connected) {
        lv_obj_add_flag(connectPassBtn, LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_obj_clear_flag(connectPassBtn, LV_OBJ_FLAG_HIDDEN);
      }
    }
  }
}

bool isTapOnSelected(uint16_t x, uint16_t y) {
  if (!MenuSystem::isOpen()) return false;
  lv_obj_t* sel = menuItems[selectedItem];
  return isPointInside(sel, x, y);
}

bool isTapOnCareSelected(uint16_t x, uint16_t y) {
  if (!MenuSystem::isCareOpen()) return false;
  lv_obj_t* sel = careItems[selectedCareItem];
  return isPointInside(sel, x, y);
}

bool isTapOnStatsTitle(uint16_t x, uint16_t y) {
  if (currentState != MENU_STATS_OPEN) return false;
  return isPointInside(statsTitle, x, y);
}

bool isTapOnStatsNav(uint16_t x, uint16_t y) {
  if (currentState != MENU_STATS_OPEN) return false;
  return isPointInside(statsLeftBtn, x, y) || isPointInside(statsRightBtn, x, y);
}

}  // namespace MenuSystem

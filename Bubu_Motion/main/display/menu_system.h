#pragma once

#include <cstdint>
#include "display/display.h"
#include "screen_manager.h"

enum MenuState {
    MENU_CLOSED,
    MENU_OPEN,
    MENU_CARE_OPEN,
    MENU_CONNECT_OPEN,
    MENU_KEYBOARD_OPEN,
    MENU_REMINDERS_OPEN,
    MENU_REMINDER_DETAIL_OPEN,
    MENU_STATS_OPEN,
    MENU_GAMES_OPEN,
    MENU_GAME_ACTIVE,
    MENU_FORTUNE_OPEN,
    MENU_LEVEL_OPEN,
    MENU_NOTES_OPEN,
    MENU_NOTE_DETAIL_OPEN,
    MENU_SETTINGS_OPEN,
    MENU_VOLUME_OPEN,
    MENU_POMODORO_OPEN,
    MENU_BADGES_OPEN
};

enum MenuItem {
    MENU_CARE,
    MENU_CONNECT,
    MENU_REMINDERS,
    MENU_NOTES,
    MENU_POMODORO,
    MENU_SETTINGS,
    MENU_FORTUNE,
    MENU_ITEM_COUNT
};

namespace MenuSystem {

// Initialize all menu panels (call once from SetupUI, pass the display for locking)
void Begin(Display* display);

// Open/close the main menu
void Open();
void Close();

// State queries
bool IsAnyOpen();  // true if any menu/sub-panel is visible
// True while a full-screen game owns input; callers suppress incidental
// feedback (tap voice, idle nudges) during play.
// True while the level-up overlay owns the screen.
bool IsCelebrationActive();
bool IsOpen();
MenuState GetState();

// ScreenId for the panel currently on screen. Only meaningful while a menu is
// open; EyeDisplay owns the screen when the menu is closed.
ScreenManager::ScreenId ActiveScreen();
MenuItem GetSelected();

// Main menu navigation
void SelectNext();
void SelectPrev();
void ActivateSelected();
void NavigateNext();
void NavigatePrev();
void ActivateCurrent();

// Care sub-menu
void SelectCareNext();
void SelectCarePrev();
void ActivateCareSelected();
void CloseCareToMenu();

// Connect
bool HandleConnectTap(uint16_t x, uint16_t y);
bool HandleKeyboardTap(uint16_t x, uint16_t y);
void CloseConnectToMenu();
void CloseKeyboardToConnect();

// Reminders
void SelectRemindersNext();
void SelectRemindersPrev();
void ActivateRemindersSelected();
void CloseRemindersToMenu();
void CloseReminderDetailToReminders();
void RemindersDetailNext();
void RemindersDetailPrev();

// Stats
void ShowStats();
void CloseStatsToMenu();
void StatsNext();
void StatsPrev();

// Options / Games
void OpenOptionsForCurrentStat();
void CloseOptionsToStats();
void ActivateCurrentOption();
void OpenGamesMenu();
void CloseGamesToStats();
void StartGreenEye();
void HandleGameFinished();
bool HandleGameTap(uint16_t x, uint16_t y);
void HandleGameLongPress();

// Pomodoro (HỌC TẬP)
void OpenPomodoro();
void ClosePomodoroToMenu();
bool HandlePomodoroTap(uint16_t x, uint16_t y);
// Entry points for MCP. Both touch LVGL, so they must run on the main task --
// callers on any other task hand them over with Application::Schedule.
// focus_minutes snaps to the nearest preset (15 / 25 / 45).
void StartPomodoroFromVoice(int focus_minutes);
void StopPomodoro();

// Level (TÌNH BẠN)
void CloseLevelToMenu();

// HUY HIỆU. OpenBadgeAward() shows the oldest badge waiting to be received,
// from the eyes' medal bubble (menu closed); a tap receives it.
void OpenBadgeAward();

// Settings
void CloseSettingsToMenu();
bool HandleSettingsTap(uint16_t x, uint16_t y);
bool HandleVolumeTap(uint16_t x, uint16_t y);
void VolumeStep(bool increase);
void VolumeBack();

// Sleep
void SelectSleepNext();
void SelectSleepPrev();
void ActivateSleepSelected();
void CloseSleepToCare();

// Notes
void SelectNotesNext();
void SelectNotesPrev();
void ActivateNotesSelected();
void CloseNotesToMenu();
void CloseNoteDetailToNotes();
void NotesDetailNext();
void NotesDetailPrev();

// ---------------------------------------------------------------------------
// Input entry points.
//
// These are the only functions input handling should need. Each takes a raw
// user intent, resolves it against the current state internally, and reports
// whether it was consumed. Callers must not switch on GetState() to decide
// which per-screen handler to call -- that duplicates the state machine
// outside this file and has to be updated in lockstep every time a screen is
// added. Adding a screen should only require editing menu_system.cc.
//
// Each returns true if the input was consumed by the menu.
// ---------------------------------------------------------------------------

// A tap at a screen coordinate.
bool HandleTap(uint16_t x, uint16_t y);

// A directional flick. The board classifies a release that travelled too far
// to be a tap and hands it here; today only the snake game consumes one, and
// every other screen returns false, so the caller can treat "not consumed" as
// "this release was not a gesture" without knowing which screen is up.
enum class SwipeDirection : uint8_t { kUp, kDown, kLeft, kRight };
bool HandleSwipe(SwipeDirection direction);

// Latest accelerometer sample, delivered on the application task. Ignored
// unless the tilt-maze screen owns the IMU.
void HandleImuAccel(float ax, float ay, float az);

// A long press. x/y are the touch point, or 0,0 for a physical button.
// close_by_default controls the fallback for screens with no long-press
// behaviour of their own: true closes the menu (touch gesture), false leaves
// it untouched and returns false so the caller can do something else (the
// power button, which opens Wi-Fi config from any state).
bool HandleLongPress(uint16_t x, uint16_t y, bool close_by_default = true);

// The confirm/select action (touch on the active item, or the power button).
bool HandleActivate();

// Directional navigation. forward=false is "previous"/up.
bool HandleNavigate(bool forward);

// Render (call from display update loop)
void Render();

// Transient GIF overlay
bool HandleCareAnimationTap();
bool HandleCareAnimationScrub(int x, int y);
void TriggerLevelUpAnimation(int level);

// Hit-test helpers
bool IsTapOnSelected(uint16_t x, uint16_t y);
bool IsTapOnUpButton(uint16_t x, uint16_t y);
bool IsTapOnDownButton(uint16_t x, uint16_t y);
bool IsTapOnPrevButton(uint16_t x, uint16_t y);
bool IsTapOnNextButton(uint16_t x, uint16_t y);
bool IsTapOnCareSelected(uint16_t x, uint16_t y);
bool IsTapOnStatsTitle(uint16_t x, uint16_t y);
bool IsTapOnStatsNav(uint16_t x, uint16_t y);
bool IsTapOnSleepSelected(uint16_t x, uint16_t y);
bool IsTapOnNotesSelected(uint16_t x, uint16_t y);
bool IsTapOnRemindersSelected(uint16_t x, uint16_t y);

// Clean animation trigger
void StartCleanAnimation();

// Re-resolves every menu/care icon from the assets partition as currently
// mapped. Call after a successful Assets::Download() + Apply() -- icons are
// otherwise raw pointers cached once at boot and never refreshed, so a
// download landing after that first resolve leaves them stale (silently
// garbled, not a crash -- see the comment at the implementation).
void RefreshIcons();

}  // namespace MenuSystem

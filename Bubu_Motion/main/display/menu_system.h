#pragma once

#include <cstdint>
#include "display/display.h"

enum MenuState {
    MENU_CLOSED,
    MENU_OPEN,
    MENU_CARE_OPEN,
    MENU_FEEDING,
    MENU_CONNECT_OPEN,
    MENU_KEYBOARD_OPEN,
    MENU_MESSAGE_OPEN,
    MENU_STATS_OPEN,
    MENU_OPTIONS_OPEN,
    MENU_GAMES_OPEN,
    MENU_GAME_ACTIVE,
    MENU_LEVEL_OPEN,
    MENU_NOTES_OPEN,
    MENU_NOTE_DETAIL_OPEN,
    MENU_SETTINGS_OPEN,
    MENU_VOLUME_OPEN,
    MENU_EYE_EDITOR_OPEN,
    MENU_SLEEP_OPEN
};

enum MenuItem {
    MENU_CARE,
    MENU_CONNECT,
    MENU_MESSAGE,
    MENU_NOTES,
    MENU_SETTINGS,
    MENU_ITEM_COUNT
};

namespace MenuSystem {

// Initialize all menu panels (call once from SetupUI, pass the display for locking)
void Begin(Display* display);

// Open/close the main menu
void Open();
void Close();

// State queries
bool IsOpen();
bool IsAnyOpen();  // true if any menu/sub-panel is visible
MenuState GetState();
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

// Message
void CloseMessageToMenu();

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
void StartTapTheGreens();
void HandleGameFinished();

// Level
void CloseLevelToMenu();

// Settings
void CloseSettingsToMenu();
bool HandleSettingsTap(uint16_t x, uint16_t y);
bool HandleVolumeTap(uint16_t x, uint16_t y);
void VolumeStep(bool increase);
void VolumeBack();
bool HandleEyeEditorTap(uint16_t x, uint16_t y);
void EyeEditorCycleMode(bool forward);
void EyeEditorApplyIncrement();
void EyeEditorBack();

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

// Render (call from display update loop)
void Render();

// Feeding animation overlay
bool IsFeedingAnimationActive();
bool HandleFeedingAnimationTap();

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

// Clean animation trigger
void StartCleanAnimation();

}  // namespace MenuSystem

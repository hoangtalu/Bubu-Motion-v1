#pragma once
#include <Arduino.h>

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
  MENU_SLEEP_OPEN,
  MENU_TEST_SOUND_OPEN
};

enum MenuItem {
  MENU_CARE,
  MENU_CONNECT,
  MENU_MESSAGE,
  MENU_NOTES,
  MENU_SETTINGS,
  MENU_TEST_SOUND, // Test sound effects
  MENU_MOCK,       // Test: trigger voice mocking playback immediately
  MENU_ITEM_COUNT  // Total number of items
};

namespace MenuSystem {
  void begin();
  void open();
  void close();
  void showStats();
  bool isOpen();
  bool isCareOpen();
  bool isFeeding();
  bool isConnectOpen();
  bool isKeyboardOpen();
  bool isMessageOpen();
  bool isStatsOpen();
  bool isOptionsOpen();
  bool isGamesOpen();
  bool isGameActive();
  bool isLevelOpen();
  bool isSettingsOpen();
  bool isSleepOpen();
  bool isTestSoundOpen();
  void selectSleepNext();
  void selectSleepPrev();
  void activateSleepSelected();
  void closeSleepToCare();
  bool isTapOnSleepSelected(uint16_t x, uint16_t y);
  bool isNotesOpen();
  bool isNoteDetailOpen();
  void selectNotesNext();
  void selectNotesPrev();
  void activateNotesSelected();
  void closeNotesToMenu();
  void closeNoteDetailToNotes();
  void notesDetailNext();
  void notesDetailPrev();
  bool isTapOnNotesSelected(uint16_t x, uint16_t y);
  void otaSetActive(bool active);
  void otaPulse(uint32_t nowMs);
  
  void selectNext();
  void selectPrev();
  void selectCareNext();
  void selectCarePrev();
  void statsNext();
  void statsPrev();
  size_t getCurrentStatIndex();
  MenuItem getSelected();
  void activateCareSelected();
  void activateSelected();
  bool handleConnectTap(uint16_t x, uint16_t y);
  void closeCareToMenu();
  void closeConnectToMenu();
  void closeKeyboardToConnect();
  void closeMessageToMenu();
  void closeStatsToMenu();
  void closeLevelToMenu();
  void closeSettingsToMenu();
  void closeTestSoundToMenu();
  void startCleanAnimation();
  void openOptionsForCurrentStat();
  void closeOptionsToStats();
  void activateCurrentOption();
  void selectOptionsPrev();
  void selectOptionsNext();
  void openGamesMenu();
  void closeGamesToStats();
  void startTapTheGreens();
  void handleGameFinished();
  void selectTestSoundNext();
  void selectTestSoundPrev();
  void activateTestSoundSelected();
  bool isTapOnTestSoundSelected(uint16_t x, uint16_t y);

  void render();  // Call this from display update

  // Hit-test helpers
  bool isTapOnSelected(uint16_t x, uint16_t y);
  bool isTapOnStatsTitle(uint16_t x, uint16_t y);
  bool isTapOnStatsNav(uint16_t x, uint16_t y);
  bool isTapOnCareSelected(uint16_t x, uint16_t y);
}

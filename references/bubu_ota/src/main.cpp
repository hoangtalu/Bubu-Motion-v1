#include <Arduino.h>
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_idf_version.h"
#include "display_system.h"
#include "care_system.h"
#include "eye_game.h"
#include "wifi_service.h"
#include "logger.h"
#include "ota/ota_manager.h"
#include "sound/sound_effects.h"
#include "level_system.h"
#include "chat/chat_system.h"
// gemini_handler removed — Live API handler include goes here
#include "chat_screen.h"
#include "chat_config.h"
#include <Wire.h>
#include "tca6408.h"
#include "board_pins.h"
#include "tools/reminder_system.h"
#include "tools/note_system.h"
#include "tools/tool_notification.h"
#include "config_fetcher.h"
#include "voice_detector.h"
#include "crash_monitor.h"
#include "heartbeat.h"
DEFINE_MODULE_LOGGER(MainLog)

// ============================================================================
// OTA VERSION - No hardcoded API key, no factory provisioner
// API key is read from encrypted NVS (provisioned by base version at factory)
// Safe to publish to GitHub
// ============================================================================

static void checkPsram() {
  if (psramFound()) {
    MainLog::println("[BOOT] PSRAM detected");
    MainLog::printf("[BOOT] PSRAM free: %u bytes\n",
                  heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  } else {
    MainLog::println("[BOOT] PSRAM NOT FOUND");
  }
}

static void printMemoryReport(const char* label) {
  multi_heap_info_t info;
  heap_caps_get_info(&info, MALLOC_CAP_INTERNAL);
  size_t freeHeap = ESP.getFreeHeap();
  size_t largestFree = info.largest_free_block;
  size_t freePsram = ESP.getFreePsram();
  size_t totalPsram = ESP.getPsramSize();

  MainLog::println("------ Memory Report ------");
  if (label) MainLog::printf("Label: %s\n", label);
  MainLog::printf("Internal RAM free:    %u bytes\n", static_cast<unsigned>(freeHeap));
  MainLog::printf("Internal largest blk: %u bytes\n", static_cast<unsigned>(largestFree));
  MainLog::printf("PSRAM total:          %u bytes\n", static_cast<unsigned>(totalPsram));
  MainLog::printf("PSRAM free:           %u bytes\n", static_cast<unsigned>(freePsram));
  MainLog::println("---------------------------");
}

// Pet hearing removed - Gemini AI now controls emotions via function calling

// Main app entrypoints
void setup() {
  Logger::begin(115200);
  delay(100);

  // Report any crash from the previous boot (reads RTC memory + reset reason)
  CrashMonitor::begin();

  // Make WDT non-fatal: log violations but don't abort/reboot.
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
  {
    esp_task_wdt_config_t wdtCfg = {
        .timeout_ms     = 60000,
        .idle_core_mask = (1 << 0) | (1 << 1),
        .trigger_panic  = true   // TEMP: force WDT reboot for stack trace on hang
    };
    esp_task_wdt_reconfigure(&wdtCfg);
  }
#else
  // ESP-IDF 4.x: deinit, reinit with panic=false, re-add idle tasks
  esp_task_wdt_deinit();
  esp_task_wdt_init(60, true);   // TEMP: force WDT reboot for stack trace on hang
  esp_task_wdt_add(xTaskGetIdleTaskHandleForCPU(0));
  esp_task_wdt_add(xTaskGetIdleTaskHandleForCPU(1));
#endif

  checkPsram();
  printMemoryReport("Boot start");

  wifiInit();
  SoundEffects::begin();
  // Init I2C and I/O expander for battery system
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  TCA6408::begin();
  DisplaySystem_begin();

  printMemoryReport("After display init");
  LevelSystem::begin();
  // WiFi is user-controlled via the toggle — no auto-scan or auto-connect at boot.
  CareSystem::begin();
  EyeGame::Config gameCfg;
  gameCfg.maxRounds = 40;
  gameCfg.rewardPerHit = CareSystem::kGameRewardPerHit;
  gameCfg.wrongTapMoodDelta = CareSystem::kGameWrongTapMood;
  gameCfg.wrongTapEnergyDelta = CareSystem::kGameWrongTapEnergy;
  EyeGame::configure(gameCfg);
  BubuOTA::begin();
  if (BubuOTA::wasRollback()) {
    Serial.println("[OTA] Rollback detected (previous update crashed).");
  }

  // Pet hearing removed - Gemini AI controls emotions via function calling
  MainLog::println("[BOOT] Emotion control via Gemini function calling");

  // Initialize chat system (loads API key from encrypted NVS, creates queues)
  ChatSystem::begin();
  printMemoryReport("After chat init");

  // Initialize tools (reminder and note systems)
  ReminderSystem::begin();
  NoteSystem::begin();
  ToolNotification::begin();

  // Set reminder callback to show notification when reminder triggers
  ReminderSystem::setReminderCallback([](const ReminderSystem::Reminder& r) {
    ToolNotification::showReminder(r.title.c_str(), r.id);
  });

  MainLog::println("[BOOT] Tools initialized (reminder/note systems ready)");
  printMemoryReport("After tools init");

  // API key is loaded from encrypted NVS (provisioned at factory by base version)
  if (ConfigFetcher::hasLocalConfig()) {
    MainLog::println("[BOOT] API key found in NVS - ready");
  } else {
    MainLog::println("[BOOT] WARNING: No API key in NVS - device may not be provisioned");
    MainLog::println("[BOOT] Flash base version first to provision this device");
  }

  // Heartbeat task: prints "[HB] alive" every 5s on Core 0.
  // If output stops → Arduino loop (Core 1) is frozen.
  Heartbeat::begin();
}

static bool ota_check_done = false;

// Serial command buffer for chat configuration
static char serialBuf[128];
static size_t serialLen = 0;

static void handleSerialCommand(const char* cmd) {
  String s(cmd);
  s.trim();

  if (s == "chat key show" || s == "chat key get") {
    // Read directly from NVS so we always get the stored value
    Preferences prefs;
    prefs.begin("chat", true);
    String key = prefs.getString("apikey", "");
    prefs.end();
    if (key.isEmpty()) {
      Serial.println("[Chat] API key: (not set)");
    } else {
      Serial.printf("[Chat] API key: %s\n", key.c_str());
    }
  } else if (s.startsWith("chat key ")) {
    String key = s.substring(9);
    key.trim();
    if (key.length() < 10) {
      Serial.println("[Chat] Key too short — paste full API key after 'chat key '");
    } else {
      chatConfig.apiKey = key;
      chatConfig.save();
      Serial.printf("[Chat] API key saved (%u chars). Restart or tap eye to connect.\n",
                    (unsigned)key.length());
    }
  } else if (s.startsWith("chat voice ")) {
    String voice = s.substring(11);
    voice.trim();
    chatConfig.voiceName = voice;
    chatConfig.save();
    Serial.printf("[Chat] Voice set to: %s\n", voice.c_str());
  } else if (s == "chat audio on") {
    chatConfig.audioEnabled = true;
    chatConfig.save();
    Serial.println("[Chat] Audio mode enabled");
  } else if (s == "chat audio off") {
    chatConfig.audioEnabled = false;
    chatConfig.save();
    Serial.println("[Chat] Audio mode disabled (text-only)");
  } else if (s == "chat fetch") {
    Serial.println("[Chat] Remote config fetch is disabled - using NVS-provisioned config");
  } else if (s.startsWith("chat text ")) {
    // TODO: Restore when Live API handler is implemented
    Serial.println("[Chat] 'chat text' command requires Live API handler (not yet implemented)");
  } else if (s.startsWith("chat say ")) {  // alias
    String msg = s.substring(9);
    msg.trim();
    ChatSystem::sendText(msg.c_str());
  } else if (s == "chat on") {
    ChatSystem::enable();
  } else if (s == "chat off") {
    ChatSystem::disable();
  } else if (s == "chat status") {
    String keyStatus = "(not set)";
    if (!chatConfig.apiKey.isEmpty()) {
      // Show first 8 chars + "..." so you can confirm which key is loaded
      keyStatus = chatConfig.apiKey.substring(0, 8) + "...(" +
                  String(chatConfig.apiKey.length()) + " chars)";
    }
    Serial.printf("[Chat] enabled=%d audio=%d key=%s voice=%s state=%d\n",
                  chatConfig.enabled,
                  chatConfig.audioEnabled,
                  keyStatus.c_str(),
                  chatConfig.voiceName.c_str(),
                  ChatSystem::getState());
  }
  // Tools demo commands
  else if (s == "demo-setup") {
    // Create test reminders and notes
    ReminderSystem::clearAll();
    NoteSystem::clearAll();

    time_t now = time(nullptr);
    if (now > 0) {
      // Reminder in 10 seconds
      ReminderSystem::addReminder("Test reminder in 10s!", now + 10, false, 0, 1);
      // Daily reminder
      ReminderSystem::addReminder("Daily task", now + 30, true, 1, 2);
      Serial.println("[Demo] Added 2 test reminders");
    } else {
      Serial.println("[Demo] No valid time - connect to WiFi first");
    }

    NoteSystem::addNote("Shopping List", "Milk, eggs, bread, butter", 1);
    NoteSystem::addNote("Ideas", "Build a robot pet with AI voice assistant", 0);
    NoteSystem::addNote("Important", "Remember to backup the code!", 2);
    Serial.println("[Demo] Added 3 test notes");
  }
  else if (s == "demo-reminders") {
    auto reminders = ReminderSystem::getAllReminders();
    Serial.printf("\n=== Reminders (%d) ===\n", reminders.size());
    for (const auto& r : reminders) {
      if (r.active) {
        Serial.printf("ID %u: %s (at %u) %s\n",
                      r.id, r.title.c_str(), (uint32_t)r.triggerTime,
                      r.repeating ? "[REPEAT]" : "");
      }
    }
  }
  else if (s == "demo-notes") {
    auto notes = NoteSystem::getAllNotes();
    Serial.printf("\n=== Notes (%d) ===\n", notes.size());
    for (const auto& n : notes) {
      Serial.printf("ID %u: %s\n", n.id, n.title.c_str());
      Serial.printf("  Content: %s\n", n.content.c_str());
      Serial.printf("  Category: %d, Pinned: %s\n",
                    n.category, n.pinned ? "YES" : "NO");
    }
  }
  else if (s.startsWith("demo-show")) {
    // Extract note ID: "demo-show1" -> 1
    int noteId = s.substring(9).toInt();
    auto* note = NoteSystem::getNoteById(noteId);
    if (note) {
      ToolNotification::showNote(note->title.c_str(), note->content.c_str());
      Serial.printf("[Demo] Showing note: %s\n", note->title.c_str());
    } else {
      Serial.printf("[Demo] Note %d not found\n", noteId);
    }
  }
  else if (s == "demo-chime") {
    // TODO: Implement reminderChime in SoundEffects
    // SoundEffects::playReminderChime();
    Serial.println("[Demo] Reminder chime - not yet implemented");
  }
  else if (s == "demo-help") {
    Serial.println("\n=== Tools Demo Commands ===");
    Serial.println("demo-setup      - Create test reminders and notes");
    Serial.println("demo-reminders  - List all reminders");
    Serial.println("demo-notes      - List all notes");
    Serial.println("demo-show1      - Show note ID 1 on screen");
    Serial.println("demo-chime      - Play reminder sound");
    Serial.println("demo-help       - Show this help");
  }
  // TEMP: emotion test — remove before release
  else if (s == "emotest") {
    DisplaySystem_startEmotionTest();
  }
}

static void processSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (serialLen > 0) {
        serialBuf[serialLen] = '\0';
        handleSerialCommand(serialBuf);
        serialLen = 0;
      }
    } else if (serialLen < sizeof(serialBuf) - 1) {
      serialBuf[serialLen++] = c;
    }
  }
}

// ============================================================================
// Loop timing helpers
// Each function runs at its own cadence — not all every frame.
// EVERY_N_MS(lastVar, intervalMs) returns true once per interval.
// ============================================================================
#define EVERY_N_MS(last, interval) \
  ([&]() -> bool { \
    uint32_t _now = millis(); \
    if (_now - (last) >= (interval)) { (last) = _now; return true; } \
    return false; \
  }())

void loop() {
  // ---- Timestamps for each throttled function ----
  static uint32_t tVoice      = 0;  // VoiceDetector  — every 10ms  (I2S DMA fills ~8ms)
  static uint32_t tSound      = 0;  // SoundSystem    — every 20ms  (~50 Hz audio loop)
  static uint32_t tGame       = 0;  // EyeGame        — every 16ms  (~60 fps game tick)
  static uint32_t tCare       = 0;  // CareSystem     — every 500ms (internal timers are 60s+)
  static uint32_t tChat       = 0;  // ChatSystem     — every 200ms (mocking timer is 30-60 min)
  static uint32_t tWifi       = 0;  // wifiUpdate     — every 100ms (state machine polling)
  static uint32_t tSerial     = 0;  // processSerial  — every 20ms  (responsive enough)
  static uint32_t tReminder   = 0;  // ReminderSystem — every 1000ms

  // ---- One-shot OTA check after WiFi connects ----
  if (!ota_check_done && wifiGetState() == WifiState::CONNECTED) {
    ota_check_done = true;
    BubuOTA::runOnce();
    if (!ChatSystem::isEnabled()) {
      wifiStop();
    }
  }

  // ---- TIER 1: Every frame — display must run uncapped for smooth animation ----
  DisplaySystem_update();

  // ---- TIER 2: Every ~10ms — I2S DMA fills at ~8ms, no point running faster ----
  if (EVERY_N_MS(tVoice, 10)) {
    VoiceDetector::update();
  }

  // ---- TIER 3: Every ~16–20ms — game animation + audio loop ----
  if (EVERY_N_MS(tGame, 16)) {
    EyeGame::update();
  }
  if (EVERY_N_MS(tSound, 20)) {
    SoundEffects::update();
  }

  // ---- TIER 4: Every ~100–200ms — state machines, not real-time ----
  if (EVERY_N_MS(tWifi, 100)) {
    wifiUpdate();
  }
  if (EVERY_N_MS(tChat, 200)) {
    ChatSystem::update();
  }

  // ---- TIER 5: Every ~500ms — care decay is 60s minimum, save is 10min ----
  if (EVERY_N_MS(tCare, 500)) {
    CareSystem::setDecaySuspended(DisplaySystem_isHatching());
    CareSystem::update();
    LevelSystem::tick();  // Flush deferred XP to NVS (max once per 30s, never during feeding)
  }

  // ---- TIER 6: Every 1000ms — reminders are minute-granularity ----
  if (EVERY_N_MS(tReminder, 1000)) {
    time_t currentTime = time(nullptr);
    if (currentTime > 0) {
      ReminderSystem::update(currentTime);
    }
  }

  // ---- TIER 7: Every 20ms — serial is human-typed, 50Hz is more than enough ----
  if (EVERY_N_MS(tSerial, 20)) {
    processSerial();
  }
}

# ESP32-S3 System Diagnostic Report — bubu_ota
**Date:** 2026-02-27
**Scope:** FreeRTOS task map, memory layout, CPU bottlenecks, NVS save conflicts
**Question:** Why does CPU-heavy + state save cause a freeze now but not before?

---

## 1. HARDWARE BASELINE

| Resource | Value |
|----------|-------|
| CPU | ESP32-S3 dual-core Xtensa LX7 @ 240 MHz |
| Internal SRAM | ~512 KB (used: ~37% = ~189 KB per build stats) |
| PSRAM | External (detected at boot, used for LVGL buffers + audio PCM) |
| Flash | NVS partition (multiple namespaces) + SPIFFS partition |
| I2S ports | I2S_NUM_0 (mic), I2S_NUM_1 (speaker) |
| I2C | SDA/SCL → TCA6408 IO expander (battery system) |

---

## 2. BOOT SEQUENCE — What's Active at Each Stage

| Time | What Runs | Notes |
|------|-----------|-------|
| T+0 | `Serial.begin(115200)` | UART init |
| T+~50ms | `VoiceDetector::begin()` | Installs I2S driver on **I2S_NUM_0** |
| T+~100ms | `SoundSystem::begin()` | Prepares I2S_NUM_1 for speaker |
| T+~150ms | `Wire.begin()` + `TCA6408::begin()` | I2C bus init |
| T+~200ms–1s | `DisplaySystem_begin()` | LVGL init, allocates **~230 KB PSRAM** (2× 240×240×2 double buffer) |
| T+~1s | `LevelSystem::begin()` | Opens + reads NVS `"bubu-level"`, then **closes** |
| T+~1–4s | `wifiAutoConnectKnown()` | WiFi scan + connect (can block 2–3s, Core 0 background task) |
| T+~4s | `CareSystem::begin()` | Opens NVS `"care_stats"` in read-write mode — **stays open permanently** |
| T+~5s | `BubuOTA::begin()` | Checks rollback flag |
| T+~6s | `ChatSystem::begin()` | Loads `chatConfig` from NVS, creates playback task if enabled |
| T+~7s | `ReminderSystem::begin()` + `NoteSystem::begin()` + `ToolNotification::begin()` | SPIFFS reads + LVGL pre-create |
| T+~8–10s | `loop()` starts | All subsystems running |

---

## 3. FREERTOS TASK MAP AT ~10 SECONDS

These are the tasks running on the ESP32-S3. Core assignment and priority are critical.

| Task | Core | Priority | Stack | State at T+10s | Notes |
|------|------|----------|-------|----------------|-------|
| **Main loop** (Arduino) | Core 1 | 1 | Default | Running | Display, CareSystem, EyeGame, Sound, VoiceDetector, ChatSystem::update, WiFi update |
| **WiFi stack** (ESP-IDF) | Core 0 | varies | Internal | Background | Handles WiFi events, TCP/IP |
| **chat_play** | Core 1 | 2 | 8 KB | Blocked (waiting for queue) | Created by `ChatAudio::startPlaybackTask()` at ChatSystem::begin |
| **sound_ambient** | Core 1 | 1 | 4 KB | Running or sleeping | Ambient audio chunk playback |
| **ESP-IDF idle tasks** | Both cores | 0 | Internal | Idle | Watchdog feed, power management |
| **chat_mic** | Core 1 | **3** | 16 KB | **NOT YET CREATED** at T+10s — spawned on first `enable()` call |
| **chat_proc** | Core 0 | 2 | 24 KB | **NOT YET CREATED** at T+10s — spawned per turn, deletes itself |

---

## 4. MEMORY USAGE ESTIMATE

### Internal SRAM (~512 KB total, ~37% used = ~189 KB)

| Component | Estimated RAM |
|-----------|--------------|
| FreeRTOS kernel + task stacks | ~60 KB |
| Arduino framework overhead | ~20 KB |
| WiFi stack buffers (ESP-IDF) | ~40 KB |
| I2S DMA buffers (NUM_0 mic: 4×128, NUM_1 spk: 8×256) | ~6 KB |
| Static module globals (care, level, display, chat) | ~15 KB |
| Serial buffer, misc | ~5 KB |
| **Estimated free internal SRAM** | **~365 KB** |

### PSRAM (typically 4–8 MB on ESP32-S3)

| Component | PSRAM Used |
|-----------|-----------|
| LVGL draw buffer 1 (240×240×2) | 115.2 KB |
| LVGL draw buffer 2 (double buffer) | 115.2 KB |
| Eye canvas A + B (LovyanGFX sprites) | ~115 KB each = ~230 KB |
| **When chat active — PCM capture buffer** | Up to ~400 KB (audio recording) |
| **When chat active — pitch-shifted output buffer** | Up to ~400 KB |
| **Total PSRAM in heavy use** | ~1.2 MB peak during chat |

---

## 5. THE FREEZE — ROOT CAUSE ANALYSIS

There are **three interlocking causes** that combine to produce the freeze. They each existed partially before but were not all active simultaneously. They are now.

---

### ROOT CAUSE #1 — `chat_mic` at Priority 3 STARVES the Main Loop on Core 1 ⚠️ CRITICAL

```
Core 1 task priorities:
  chat_mic   → Priority 3  ← HIGHEST on Core 1
  chat_play  → Priority 2
  Main loop  → Priority 1  ← runs only when chat_mic is blocked
```

`chat_mic` uses:
```cpp
esp_err_t err = i2s_read(MIC_PORT, readBuf, sizeof(readBuf), &bytesRead, portMAX_DELAY);
```

`portMAX_DELAY` = blocks forever waiting for DMA data. When I2S DMA fills a buffer (every ~8ms at 16kHz, 128-sample buf), `chat_mic` wakes up, drains it, processes RMS, queues the chunk, and immediately goes back to `i2s_read()`. In that gap the main loop gets ONE scheduler slot at priority 1.

**Result:** During an active chat session, the main loop (which runs LVGL, display updates, CareSystem, VoiceDetector) is effectively running at a fraction of its normal rate. The display stops animating smoothly. This is visible as a freeze.

**Why this is new:** `VoiceDetector` was added to replace `PetHearing`. But `VoiceDetector::begin()` now installs the I2S_NUM_0 driver at boot. When `chat_mic` task starts, it also uses I2S_NUM_0. The comment in `chat_audio.cpp` confirms this: `"// I2S mic configuration (shares I2S_NUM_0 with PetHearing)"`. Both are on the same port. VoiceDetector suppresses itself via `setChatMode(true)` but the driver contention during the handoff creates stalls.

---

### ROOT CAUSE #2 — `LevelSystem::saveState()` Has Zero Throttling ⚠️ HIGH

Every single XP gain triggers a full NVS write cycle:

```cpp
void addXP(int amount) {
    currentXP += amount;
    checkLevelUp();  // always calls saveState()
}

void checkLevelUp() {
    if (currentXP >= requiredXP) {
        // level up
        saveState();
    } else {
        saveState();  // ← ALSO called when NOT leveling up
    }
}

void saveState() {
    preferences.begin(NVS_NAMESPACE, false);  // opens NVS, ~2-5ms
    preferences.putInt(NVS_KEY_LEVEL, currentLevel);  // flash write
    preferences.putInt(NVS_KEY_XP, currentXP);        // flash write
    preferences.end();  // commits to flash, ~10-50ms, blocks CPU
}
```

`addXP()` is called from `addHunger()`, `addMood()`, `addEnergy()`, `addCleanliness()`. Every time the user feeds or interacts with Bubu, a full NVS write fires. NVS flash write = **10–50ms blocking on the calling core.**

If this fires while `chat_proc` is running on Core 0 doing a Gemini HTTP request (also CPU-intensive), both cores are simultaneously under load + flash I/O. The main loop on Core 1 is already starved by `chat_mic`, and now the NVS mutex adds more latency.

---

### ROOT CAUSE #3 — Simultaneous NVS Save Collision at Session End ⚠️ MEDIUM

At `endSession()` in `chat_system.cpp`:

```cpp
chatConfig.save();  // opens NVS "chat" namespace, writes, closes
```

At the same time, if the user fed/interacted during the session, `LevelSystem::saveState()` already fired multiple times. And `CareSystem::update()` has a 10-minute auto-save timer that may coincide:

```cpp
// care_system.cpp
static const uint32_t SAVE_INTERVAL_MS = 10UL * 60UL * 1000UL;
// ...
if (prefsReady && (now - lastSaveMs) >= SAVE_INTERVAL_MS) {
    saveSnapshot();  // writes to permanently-open "care_stats" handle
    lastSaveMs = now;
}
```

Three different NVS namespaces writing nearly simultaneously:
- `"care_stats"` (CareSystem — persisted open handle)
- `"bubu-level"` (LevelSystem — open/write/close on every XP gain)
- `"chat"` (chatConfig at session end)

NVS flash operations share the underlying flash hardware. Even though the namespaces are logically separate, the flash write serializes at the hardware level. Each operation holds the flash off (interrupts may be disabled during erase/write). Combined: **30–150ms of cumulative flash blocking**.

---

## 6. PROCESS TIMELINE DURING A FREEZE EVENT

```
User taps screen → Chat session starts

Core 0:
  [WiFi/TCP]     ══════════════════════════════════════════
  [chat_proc]    ────────[HTTP request to Gemini, JSON parse, audio decode]────────
  [NVS write]              ↑ if addXP() fires here: 10-50ms flash block

Core 1:
  [chat_mic p3]  ═══[i2s_read block]═══[run]═══[block]═══[run]═══[block]═
  [chat_play p2] ──────────────────────────[playback]─────────────────────
  [main loop p1] ──[gap]──[gap]──[gap]───STARVED──[gap]─────────────────
                              ↑ display stops updating = visible freeze
```

---

## 7. WEAK SPOTS SUMMARY TABLE

| # | Location | Problem | Severity | When It Triggers |
|---|----------|---------|----------|-----------------|
| 1 | `chat_audio.cpp` | `chat_mic` on Core 1 priority 3 — starves main loop | **CRITICAL** | Any active chat session |
| 2 | `level_system.cpp` | `saveState()` called on EVERY XP gain with no throttle | **HIGH** | Any stat improvement (feed, mood, etc.) |
| 3 | `chat_system.cpp` + session end | `chatConfig.save()` + pending LevelSystem saves fire together | **MEDIUM** | End of every chat session |
| 4 | `voice_detector.cpp` / `chat_audio.cpp` | Both use I2S_NUM_0 — transition between them during chat start/stop | **MEDIUM** | Chat enable/disable |
| 5 | `care_system.cpp` | 10-min NVS save interval can coincide with chat session | **LOW-MEDIUM** | ~Every 10 min if chat active |

---

## 8. FIXES — DIRECT PATCHES

### Fix #1 — Move `chat_mic` to Core 0 (or reduce its priority)

**Option A — Move to Core 0** (cleanest):
```cpp
// chat_audio.cpp
xTaskCreatePinnedToCore(micTaskFunc, "chat_mic", 1024 * 16,
                        nullptr, 3, &sMicTaskHandle, 0);  // ← Core 0 instead of 1
```
This removes `chat_mic` from Core 1 entirely. Core 1 remains the display/UI core, Core 0 handles audio + HTTP.

**Option B — Reduce `chat_mic` priority to 1** (cheaper change):
```cpp
xTaskCreatePinnedToCore(micTaskFunc, "chat_mic", 1024 * 16,
                        nullptr, 1, &sMicTaskHandle, 1);  // ← priority 1, same as main loop
```
Main loop gets fair scheduling. But `chat_mic` may lose samples under heavy load — acceptable for a conversational device.

---

### Fix #2 — Add throttle to `LevelSystem::saveState()`

```cpp
// level_system.cpp — add these two variables
static uint32_t sLastNvsSaveMs = 0;
static bool sDirty = false;
static constexpr uint32_t NVS_SAVE_INTERVAL_MS = 30000;  // save at most every 30s

void checkLevelUp() {
    int requiredXP = getXPForNextLevel();
    if (currentXP >= requiredXP) {
        while (currentXP >= requiredXP) {
            currentLevel++;
            currentXP -= requiredXP;
            requiredXP = getXPForNextLevel();
        }
        saveState();  // Level up: save immediately (rare event)
        sDirty = false;
        sLastNvsSaveMs = millis();
    } else {
        sDirty = true;  // Mark dirty but DON'T save yet
    }
}

// Call this from the main loop or a periodic task:
void tick() {
    if (sDirty) {
        uint32_t now = millis();
        if (now - sLastNvsSaveMs >= NVS_SAVE_INTERVAL_MS) {
            saveState();
            sDirty = false;
            sLastNvsSaveMs = now;
        }
    }
}
```

This reduces NVS writes from potentially dozens per minute down to once every 30 seconds maximum, while still saving immediately on level-up. XP is never lost — it's in RAM; only the persistence timing changes.

---

### Fix #3 — Stagger session-end saves

In `endSession()` or `applySessionMoodAndSave()`:
```cpp
// After applying mood (which triggers XP save):
vTaskDelay(pdMS_TO_TICKS(100));  // let LevelSystem NVS settle
chatConfig.save();               // then save chatConfig
```
Small delay ensures the NVS flash isn't hit twice in rapid succession.

---

### Fix #4 — Guard I2S_NUM_0 handoff between VoiceDetector and ChatAudio

VoiceDetector installs I2S_NUM_0 at boot. ChatAudio uses the same port but assumes it's already configured. When chat ends and VoiceDetector resumes, there's a window where both may try to read. Add an explicit uninstall/reinstall cycle:

```cpp
// In ChatAudio when mic task ends, explicitly release I2S_NUM_0:
i2s_driver_uninstall(MIC_PORT);

// VoiceDetector::setChatMode(false) should reinstall if needed:
void setChatMode(bool active) {
    _chatModeActive = active;
    if (!active && _initialized) {
        // Reinstall I2S after chat releases it
        // (or just call begin() again with a guard)
    }
}
```

---

## 9. QUICK ANSWER — WHY NOW AND NOT BEFORE

| Factor | Before | Now |
|--------|--------|-----|
| `VoiceDetector` | Not present (`PetHearing` was different) | Runs in main loop every frame, installs I2S_NUM_0 at boot |
| `chat_mic` priority 3 on Core 1 | Same — but no VoiceDetector competition on I2S_NUM_0 | Both share I2S_NUM_0; handoff creates stall |
| `LevelSystem::saveState()` | Same — unthrottled | Now called more frequently as Gemini emotion function calls deferred mood is applied at session end |
| Mocking voice system | Not present | Added `sMockingTimerStart`, `scheduleMocking()` — more state tracked per loop tick |

**The system didn't change fundamentally. The load did.** VoiceDetector + mocking system added more concurrent activity on Core 1. The `chat_mic` priority 3 was always the latent risk — it just wasn't exposed until Core 1 got busier.

---

## 10. PRIORITY ORDER OF FIXES

| Order | Fix | Effort | Impact |
|-------|-----|--------|--------|
| 1 | Move `chat_mic` to Core 0 | 1 line change | Eliminates main loop starvation |
| 2 | Throttle `LevelSystem::saveState()` with dirty flag | ~10 lines | Eliminates NVS write spikes |
| 3 | Stagger session-end NVS writes | 1 line (`vTaskDelay`) | Reduces flash contention |
| 4 | Clean I2S_NUM_0 handoff | ~5 lines | Prevents driver conflict on chat toggle |

---

*Report generated: 2026-02-27*
*Source: full static analysis of bubu_ota/src/ codebase*

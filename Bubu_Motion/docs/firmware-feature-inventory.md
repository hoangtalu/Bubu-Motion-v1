# Firmware Feature Inventory

Last verified: 2026-04-25  
Scope: active runtime project `xiaozhi-doremon/xiaozhi-esp32`  
Target board in this inventory: `esp32s3-1.28-round-i80`

This file is a practical feature map of what the firmware currently does.  
It is based on the active code path, not `bubu_ota`.

## 1) Core Runtime and Control Flow

- Entry path: `main/main.cc` initializes NVS, then runs `Application::Initialize()` and `Application::Run()`.
- Main orchestrator: `main/application.cc` controls board, display, protocol, audio, OTA, and scheduled callbacks on the app task.
- Device state machine is enforced by `DeviceStateMachine` with transition validation.
- Device states currently defined:
  - `unknown`
  - `starting`
  - `wifi_configuring`
  - `idle`
  - `connecting`
  - `listening`
  - `speaking`
  - `upgrading`
  - `activating`
  - `audio_testing`
  - `fatal_error`

## 2) Conversation and Session Logic

- Conversation APIs exist in `Application`:
  - `InitiateConversation(const std::string& seed_prompt)`
  - `EndConversation()`
- Hidden proactive prompt suppression is implemented by normalized text comparison before hidden prompt forwarding.
- Listening VAD tracking exists:
  - `last_vad_speaking_`
  - `listening_voice_detected_`
- Listening no-speech timeout exists:
  - `kListeningNoSpeechTimeoutMs = 8000` in `application.cc`.
- Session control messages are sent through protocol helpers:
  - `listen start`
  - `listen stop`
  - `listen detect`
  - `abort`
  - `mcp`

## 3) Audio Features

- Active board codec path: `NoAudioCodecDuplex` (no external ES83xx codec control path in this board profile).
- I2S TX is configured as mono duplicated to both slots (`I2S_STD_SLOT_BOTH`) for dual-speaker output.
- I2S RX mic path is left slot (`I2S_STD_SLOT_LEFT`).
- Playback DSP in `NoAudioCodecDuplex::Write()`:
  - high-pass cleanup
  - mild presence lift
  - soft limiter
- Output volume range is `0..100` and persisted in NVS (`audio/output_volume`).
- Input gain is persisted in NVS (`audio/input_gain`).
- Audio service pipeline includes:
  - Opus encode/decode queues and codec task
  - queue bridge between mic PCM and network packets
  - decode queue to playback queue
- Local file playback supports Ogg Opus via `OggDemuxer` in `AudioService::PlaySound`.
- Wake-word audio packet export path exists (`EncodeWakeWord`, `PopWakeWordPacket`).
- Device AEC mode toggle exists (`EnableDeviceAec`).
- Audio power save timeout handling exists (`CheckAndUpdateAudioPowerState`).
- Interaction voices:
  - tap sound always on eye tap
  - blink voice with chance + cooldown
  - mischief voice with chance + cooldown
  - mischief voice pool is mumbling-dominant with occasional singing variants

## 4) Display and UI Framework

- LVGL display stack includes top bar, status line, notification line, center content, and bottom chat area.
- `SetStatus()` and `ShowNotification()` are separate channels:
  - notification temporarily hides status
  - timer restores status after notification duration
- Chat line is controlled by `SetChatMessage(role, text)`.
- Eye display mode (`EyeDisplay`) overrides base theme to black and renders animated eyes instead of emoji box.
- Clock screensaver exists:
  - activates only when idle and allowed
  - idle timeout is `5 minutes`
  - hides main UI bars while active
- Status text can auto-refresh to clock text when idle and stale.

## 5) Eyes and Emotion Engine

- Emotion parser and mapper is in `eye_animation.cc` (`EyeEmotion_Apply`).
- Emotion names currently handled include:
  - `neutral`, `relaxed`, `cool`
  - `happy`, `funny`
  - `laughing`, `confident`, `loving`, `kissy`, `delicious`, `shocked`
  - `surprised`
  - `sad`, `crying`
  - `worried`
  - `embarrassed`
  - `nervous`, `anxious`
  - `angry`
  - `annoyed`
  - `sleepy`
  - `thinking`, `winking`, `silly`
  - `skeptic`, `skeptical`
  - `doubt`, `doubtful`
  - `confused`
- Geometry/motion highlights implemented:
  - angry/annoyed triangular upper lids with different slope strengths
  - sad/worried tired lids with different slope strengths
  - skeptic/doubt asymmetric single-eye inner-corner top lid, randomized side
  - surprised corner radius target (`SURPRISED_RADIUS = 32`, clamped by current eye size)
  - sweat overlay for embarrassed/nervous/anxious
- Idle eye look timing is configurable in code and currently defaults to:
  - interval `500 ms`
  - variation `500 ms`
  - effective next-look range `0.5s to 1.0s`
- Care-driven emotion scheduler in `EyeDisplay`:
  - weighted table with 16 entries
  - weights influenced by hunger/mood/energy/cleanliness
  - random emotion duration window `1s to 3s`
  - external emotion hold window `4s`

## 6) Care and Level Systems

- `CareSystem` tracks four stats:
  - hunger
  - mood
  - energy
  - cleanliness
- Stat range is clamped `0..100`.
- Decay runs every 60-second tick with independent decay rates:
  - hunger: -1 per 6 min
  - mood: -1 per 8 min
  - energy: -1 per 5 min
  - cleanliness: -1 per 10 min
- Care snapshot persists every 3 minutes to NVS namespace `care_stats`.
- Care status helpers exist:
  - `NeedsAttention()`: any stat in 20..39
  - `IsCritical()`: any stat == 0
- `LevelSystem` exists with XP gain, level-up, and feature unlock checks.
- Level state persists to NVS namespace `bubu-level`.

## 7) Menu and On-Device UX

- Main menu states and overlays exist in `MenuSystem`.
- Implemented menu flows:
  - main menu categories: Care, Connect, Message, Notes, Settings
  - care actions: feed/play/clean/sleep/stats/level entry
  - stats arc panel with left/right nav and center action
  - connect panel:
    - phone provisioning path trigger
    - on-device Wi-Fi scan list
    - T9 keyboard password input
  - settings panel with volume entry
  - volume panel with preview and commit on back
  - eye editor panel (left/right eye size/radius edit, save/reset/back)
- Known placeholder or minimal implementations currently present:
  - message detail flows
  - notes flows
  - sleep sub-flow actions
  - options sub-menu
  - games sub-menu
  - level screen detail (currently not full screen, only notification)

## 8) Touch, Buttons, and Local Input

- Round board touch stack uses CST816 + IO expander interrupt + periodic poll classification.
- Touch gesture handling includes TAP and LONG_PRESS with drift and duration filtering.
- Eye tap behavior:
  - on-eyes tap triggers immediate mischief change + tap voice
  - outside-eyes tap opens menu when idle
- Long press behavior:
  - in menu layers: back/close behavior
  - on power button: enters Wi-Fi config mode from runtime states
- Side button mapping includes talk trigger and volume up/down behavior.
- IMU is initialized and sampled; shake can trigger confused expression in eye animation flow.

## 9) Wi-Fi, Provisioning, and Networking

- `WifiBoard` startup behavior:
  - auto-connect if saved SSID exists
  - otherwise enters startup-idle/offline state until user picks a connection method
- Connection timeout handling exists (60s timeout timer).
- Wi-Fi provisioning entry path exists (`EnterWifiConfigMode`).
- Compile-time provisioning options supported in code:
  - hotspot AP provisioning
  - ESP BLUFI provisioning
  - acoustic provisioning
- On-device Wi-Fi connect flow is exposed by `WifiConnectService`.
- Network status icon logic is RSSI-based when connected.

## 10) Protocol and Transport

- Protocol abstraction exists in `Protocol`.
- Transport implementations:
  - MQTT protocol
  - WebSocket protocol
- Protocol layer supports:
  - audio stream send/receive
  - text send
  - listen start/stop/detect messages
  - abort speaking reason
  - MCP payload messaging
- Listening modes supported:
  - auto-stop
  - manual-stop
  - realtime (AEC-dependent)

## 11) OTA, Activation, Assets, and Wake Words

- OTA flow supports:
  - check version endpoint
  - config pull for protocol settings
  - firmware download and partition write
  - reboot after successful upgrade
  - rollback-valid marking for pending verify images
- Activation support includes:
  - activation message/code/challenge parsing
  - timeout parsing
  - serial-number aware request headers
  - HMAC-based payload path when supported by target
- Server time sync support exists from OTA response (`server_time` + optional timezone offset).
- Assets subsystem supports:
  - assets partition discovery
  - mmap-based LVGL assets strategy
  - `index.json` parsing
  - dynamic font/theme/emoji/skin updates
  - hide subtitle option
  - srmodel loading from assets index
- Custom wake-word mode (`CustomWakeWord`) supports:
  - reading multinet configuration from assets `index.json`
  - command table with display text + action
  - wake-word PCM buffering and Opus export

## 12) MCP Tool Surface

- Common tools registered:
  - `self.get_device_status`
  - `self.audio_speaker.set_volume`
  - `self.screen.set_brightness` (if backlight exists)
  - `self.screen.set_theme` (if theme manager exists)
  - `self.camera.take_photo` (if camera exists)
- User-only tools registered:
  - `self.get_system_info`
  - `self.reboot`
  - `self.upgrade_firmware`
  - `self.screen.get_info` (with extra snapshot/preview when available)
  - `self.assets.set_download_url` (when assets partition is valid)

## 13) Persistent Settings (NVS) In Use

Commonly used namespaces/keys in this runtime:

- `audio`: `output_volume`, `input_gain`
- `care_stats`: `has`, `h`, `m`, `e`, `c`
- `bubu-level`: `level`, `xp`
- `display`: `theme`, `eye_l_w`, `eye_l_h`, `eye_l_r`, `eye_r_w`, `eye_r_h`, `eye_r_r`
- `wifi`: `ota_url`
- `websocket`: server/token/version values from OTA config
- `mqtt`: endpoint/auth/topic/keepalive values from OTA config
- `assets`: `download_url`
- `board`: `uuid`
- `backlight`: `brightness`

## 14) Hardware-Specific Features in Active Board Profile

- Display: GC9A01 240x240 round LCD on I80 8-bit bus.
- Touch: CST816 interrupt-driven touch with board-level gesture classification.
- IMU: QMI8658 initialized and sampled.
- Audio:
  - I2S mic input
  - I2S amp output via software codec path
  - dual-speaker mono duplication enabled
- Storage: microSD SPI mount support exists (`/sdcard`).
- LEDs/buttons: board-level controls and callbacks are wired.

## 15) Current Implementation Gaps to Keep in Mind

- Menu has visible entries for some subsystems that are still placeholders internally.
- Some optional MCP features depend on compile-time flags or hardware presence.
- Provisioning mode behavior depends on selected compile-time provisioning options.
- This inventory is for the active firmware target only, not for sibling reference projects.

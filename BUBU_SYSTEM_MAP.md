# Bubu System Map

Last source check: 2026-04-29

## Purpose
This file is the fast-start map for the active Bubu firmware project.

Any AI agent or human changing this workspace should read this file first before exploring the tree. The goal is to avoid rescanning the whole project every new session and to prevent wrong assumptions about board, audio, touch, UI, and reference folders.

## Scope
The active firmware project in this workspace is:

- `/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion`

Historical upstream/source path used by older notes:

- `/Users/judes/Downloads/xiaozhi-main/xiaozhi-doremon/xiaozhi-esp32`

The active board is:

- `esp32s3-1.28-round-i80`

Do not treat sibling folders, vendor samples, or `references/bubu_ota` as the runtime target.

## Mandatory First Reads
Read these before board-specific changes:

1. `/Users/judes/Documents/Arduino/Bubu_Motion_V1/BUBU_SYSTEM_MAP.md`
2. `/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/main/boards/esp32s3-1.28-round-i80/config.h`
3. `/Users/judes/Documents/Arduino/Bubu_Motion_V1/Bubu_Motion/main/boards/esp32s3-1.28-round-i80/esp32s3_round_i80_board.cc`

## Active Build Selection
The round board is selected by:

- `xiaozhi-doremon/xiaozhi-esp32/sdkconfig`
  - `CONFIG_BOARD_TYPE_ESP32S3_128_ROUND_I80=y`
  - `CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y`
  - `CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions/v2/16m.csv"`
- `xiaozhi-doremon/xiaozhi-esp32/sdkconfig.defaults.round_i80`
  - `CONFIG_BOARD_TYPE_ESP32S3_128_ROUND_I80=y`
  - `CONFIG_LANGUAGE_EN_US=y`
  - `CONFIG_USE_AFE_WAKE_WORD=y`
  - `CONFIG_SEND_WAKE_WORD_DATA=y`
  - `CONFIG_LV_FONT_MONTSERRAT_48=y`
- `xiaozhi-doremon/xiaozhi-esp32/main/CMakeLists.txt`
  - maps `CONFIG_BOARD_TYPE_ESP32S3_128_ROUND_I80` to `BOARD_TYPE "esp32s3-1.28-round-i80"`
  - uses `font_puhui_basic_20_4`, `font_awesome_20_4`, and `twemoji_64`
  - compiles all `main/boards/esp32s3-1.28-round-i80/*.cc` and `*.c`

The active partition table is:

- `xiaozhi-doremon/xiaozhi-esp32/partitions/v2/16m.csv`

Important partition entries:

- `ota_0`: `0x580000`
- `ota_1`: `0x580000`
- `assets`: `0xB20000`, size `0x4E0000`

## Source Of Truth
Use these in this order:

1. This file for orientation.
2. `config.h` and `esp32s3_round_i80_board.cc` for hardware/software binding.
3. `Application`, `AudioService`, `EyeDisplay`, `EyeAnimation`, and `MenuSystem` for behavior.
4. Vendor board package only when schematic-level or chip-reference confirmation is needed.

## Hardware Reference
Vendor board package used as hardware reference:

- `/Users/judes/Downloads/ESP32S3-NxxRxx-128I80T_开发板 V1.01`

Main vendor references:

- `/Users/judes/Downloads/ESP32S3-NxxRxx-128I80T_开发板 V1.01/Dev reference documentation/ESP32S3-128I80T系列开发板开发参考  V1.0.pdf`
- `/Users/judes/Downloads/ESP32S3-NxxRxx-128I80T_开发板 V1.01/Sample code/xiaozhi-esp32-ESP32S3-128I80T-v1.6.0`

Known hardware reference chips in the vendor package:

- MAX98357
- ICS-43434
- CST816S
- QMI8658
- GC9A01
- TCA6408A / TCA9554-style IO expander
- BQ25170
- TPS63802
- WS2812

Limit: this map is grounded in current source files and the known vendor package. It is not a line-by-line PDF or schematic extraction.

## Hardware Map

### Active Firmware Binding
Board-specific files:

- `main/boards/esp32s3-1.28-round-i80/config.h`
- `main/boards/esp32s3-1.28-round-i80/config.json`
- `main/boards/esp32s3-1.28-round-i80/esp32s3_round_i80_board.cc`

### Main Hardware Blocks
The active code targets:

- ESP32-S3 MCU
- GC9A01 240 x 240 round TFT over 8-bit I80 parallel bus
- CST816 touch controller at I2C address `0x15`
- QMI8658 IMU
- TCA9554 IO expander at I2C address `0x20`
- direct I2S audio path using `NoAudioCodecDuplex`
- likely MAX98357A-style I2S speaker amplifier on output
- I2S digital microphone input
- microSD over SDSPI
- PWM backlight
- built-in LED pin is defined, but the round board does not currently override `GetLed()`, so runtime LED behavior falls back to base `NoLed`

### Pin Map From `config.h`

#### Audio
- input sample rate: `24000`
- output sample rate: `24000`
- I2S MCLK: not used
- I2S WS / LRCK: GPIO 4
- I2S BCLK: GPIO 5
- I2S DIN, microphone data into ESP32: GPIO 6
- I2S DOUT, speaker data out: GPIO 7
- PA enable: not used in firmware, `GPIO_NUM_NC`
- I2C pins used by board peripherals: SDA GPIO 8, SCL GPIO 9

#### LED / Button
- built-in LED: GPIO 46
- boot button define: GPIO 0

Current runtime note: `BUILTIN_LED_GPIO` is defined in `config.h`, but `Esp32S3RoundI80Board` does not override `Board::GetLed()`. The active LED object is therefore the base `NoLed` unless this board implementation is changed.

Current side-button runtime path is through the IO expander, not direct GPIO button polling.

#### Display
- resolution: `240 x 240`
- mirror X: true
- mirror Y: true
- swap XY: true
- offset X/Y: `0`, `0`
- backlight: GPIO 42, not inverted
- I80 data bus:
  - D0: GPIO 10
  - D1: GPIO 11
  - D2: GPIO 12
  - D3: GPIO 13
  - D4: GPIO 14
  - D5: GPIO 15
  - D6: GPIO 16
  - D7: GPIO 17
- I80 control:
  - WR: GPIO 3
  - DC: GPIO 18
  - CS: GPIO 2
  - RST: GPIO 21
  - RD: not used

Runtime display init:

- creates an I80 bus
- uses 20 MHz pixel clock
- creates `esp_lcd_new_panel_gc9a01`
- inverts color
- mirrors X/Y
- creates `EyeDisplay`

#### Touch / IO Expander
- touch reset: GPIO 0
- touch interrupt into ESP32: GPIO 45
- touch controller I2C address: `0x15`
- IO expander I2C address: `0x20`
- IO expander ports:
  - `XIO_TOUCH_INT`
  - `XIO_IMU_INT1`
  - `XIO_IMU_INT2`
  - `XIO_KEY_UP`
  - `XIO_KEY_POWER`
  - `XIO_KEY_DOWN`
  - `XIO_USB_DET`
  - `XIO_RTC_INT`

Touch init currently:

- hardware reset low 10 ms, high 50 ms
- reads CST816 chip ID from `0xA7`
- writes `0xFA = 0x20` to enable touch-change interrupt
- writes `0xEB = 50` for about 0.5 s long-press time
- attaches falling-edge ISR on GPIO 45
- starts a 20 ms `esp_timer` callback

Important: the timer reads IMU and touch state, but UI actions are scheduled onto the app/main task with `Application::Schedule(...)`.

#### IMU
- `Qmi8658` on the board I2C bus
- initialized in `InitializeImu()`
- accelerometer reads happen from the 20 ms touch timer
- current `EyeAnimation::UpdateImuOffset()` decays legacy IMU offset back to zero, so source comments saying "eye tilt tracking enabled" are stronger than the current animation behavior

#### SD Card
- host: `SPI3_HOST`
- CS: GPIO 40
- CLK: GPIO 41
- MOSI: GPIO 47
- MISO: GPIO 48
- mount point: `/sdcard`

SD is mounted lazily by the speaker-test path, not during board construction.

## Critical Hardware Interpretation

### Audio Is Direct I2S, Not ES83xx
The active board does not use ES8311 / ES8374 / ES8388 / ES8389 codec control for runtime audio.

Active path:

- `NoAudioCodecDuplex`
- I2S0 master
- TX and RX on one duplex channel pair
- TX mono data sent to both I2S slots
- RX uses left channel only

Do not switch this board to an ES83xx codec path unless the hardware is changed and confirmed.

### Touch Is Custom CST816D Logic
Touch is not a generic managed `esp_lcd_touch_*` path here.

Current path:

- local `Cst816d` class inside `esp32s3_round_i80_board.cc`
- raw CST816 register reads
- GPIO interrupt flag plus TCA poll
- tap/long-press classifier in the 20 ms timer
- actual behavior dispatch on the app/main task

### Heavy Work Must Stay Off Timer Callbacks
Current safe pattern:

- ISR/timer detects or samples
- UI, state changes, audio calls, and menu actions run through `Application::Schedule(...)`

Keep this pattern.

## Software Layout

Main active project:

- `xiaozhi-doremon/xiaozhi-esp32`

Key folders:

- `main/` - app source
- `main/boards/` - board abstractions and board implementations
- `main/audio/` - codec abstraction, mic/playback pipeline, wake word, local sound
- `main/display/` - display, eyes, menu, LVGL helpers
- `main/led/` - LED state indicators
- `main/protocols/` - MQTT and WebSocket protocol logic
- `main/assets/` - embedded sound files, locale files, generated bindings, and `assets.bin`
- `managed_components/` - ESP-IDF managed components
- `partitions/` - partition tables
- `build/` - generated build output

Workspace-level supporting folders:

- `references/bubu_ota/` - reference PlatformIO/older Bubu project, not runtime truth
- `asset_sources/bubu_voice_ogg/` - raw/source Bubu voice assets, not automatically active
- `artifacts/releases/` - packaged release zips
- `project_docs/` - tracker sheets and non-runtime planning docs
- `hardware_reference/` - local hardware notes/manuals
- `third_party/esp-idf/` - vendored ESP-IDF checkout for reference only

## Boot / Application Flow

### Entry Point
File:

- `main/main.cc`

Flow:

1. initialize NVS
2. get `Application::GetInstance()`
3. call `Initialize()`
4. call `Run()`

### Application
Files:

- `main/application.h`
- `main/application.cc`

`Application::Initialize()` currently:

- sets state to `Starting`
- gets board singleton
- sets up display UI
- shows board/user-agent string
- initializes and starts `AudioService`
- installs audio callbacks
- initializes `LevelSystem` and `CareSystem`
- starts one-second clock timer
- registers MCP common and user-only tools
- installs network event callback
- calls `board.StartNetwork()`
- updates the status bar

`Application::Run()` is the main event loop. It handles:

- scheduled main-task callbacks
- audio send queue
- wake word events
- VAD changes
- error events
- activation events
- network connected/disconnected/startup-idle events
- chat toggle
- manual start/stop listening
- state-change UI/audio side effects
- one-second clock tick

Clock tick currently updates:

- display status bar
- care stats decay/save via `CareSystem::Update()`
- level deferred save via `LevelSystem::Tick()`
- menu rendering
- listening inactivity timeout
- heap debug logging every 10 ticks

## Board Abstraction

### Base Board
Files:

- `main/boards/common/board.h`
- `main/boards/common/board.cc`

Defines board contract for:

- display
- audio codec
- network
- LED
- backlight
- battery
- camera
- system info
- power-save level
- board/device JSON
- speaker test

### Wi-Fi Board Base
Files:

- `main/boards/common/wifi_board.h`
- `main/boards/common/wifi_board.cc`

Current behavior:

- AP SSID prefix is `Bubu-Motion`
- saved SSIDs are managed through `SsidManager`
- if saved SSIDs exist, station connect starts with a 60 s timeout
- if no saved SSIDs exist, boot stays offline and emits `NetworkEvent::StartupIdle`
- Wi-Fi config mode is user-driven
- long press / connect menu can enter config mode
- timeout stops station mode and returns to startup-idle/offline behavior

### Round Board Implementation
File:

- `main/boards/esp32s3-1.28-round-i80/esp32s3_round_i80_board.cc`

Main responsibilities:

- initialize I2C
- initialize TCA9554 IO expander
- initialize side buttons
- initialize GC9A01 I80 display and `EyeDisplay`
- initialize QMI8658 IMU
- initialize CST816 touch
- initialize Wi-Fi power timer
- provide `NoAudioCodecDuplex`
- restore backlight brightness
- seed default startup volume without overwriting saved NVS volume
- set input gain to `3.0`
- provide SD-card speaker-test support

Constructor order:

1. `InitializeI2c()`
2. `InitializeIoExpander()`
3. `InitializeButtons()`
4. `InitializeDisplay()`
5. `InitializeImu()`
6. `InitializeTouch()`
7. `InitializeWifiPowerTimer()`
8. add state-change listener for Wi-Fi power policy
9. `GetAudioCodec()->PreviewOutputVolume(12)`
10. `GetAudioCodec()->SetInputGain(3.0f)`
11. `GetBacklight()->RestoreBrightness()`

## Device State Machine

Files:

- `main/device_state.h`
- `main/device_state_machine.h`
- `main/device_state_machine.cc`

States:

- `Unknown`
- `Starting`
- `WifiConfiguring`
- `Idle`
- `Connecting`
- `Listening`
- `Speaking`
- `Upgrading`
- `Activating`
- `AudioTesting`
- `FatalError`

Important transitions:

- `Unknown` -> `Starting`
- `Starting` -> `WifiConfiguring` or `Activating`
- `WifiConfiguring` -> `Activating` or `AudioTesting`
- `AudioTesting` -> `WifiConfiguring`
- `Idle` -> `Connecting`, `Listening`, `Speaking`, `Activating`, `Upgrading`, or `WifiConfiguring`
- `Connecting` -> `Idle` or `Listening`
- `Listening` -> `Speaking` or `Idle`
- `Speaking` -> `Listening` or `Idle`
- `FatalError` cannot transition out

## Current Input Behavior

### Touch
Tap classification:

- tap duration at least 20 ms
- long press threshold 400 ms
- max tap/long-press drift 35 px
- read timeout over 120 ms forces release

Closed menu behavior:

- tap on eyes:
  - interrupts local playback if allowed
  - plays tap voice when screensaver is not active
  - toggles conversation through `HandleConversationTrigger()`
  - if currently `Listening` or `AudioTesting`, stops listening/testing
  - otherwise starts manual listening
  - sets Wi-Fi power to balanced and resets Wi-Fi power timer
- tap outside eyes:
  - opens main menu

Screensaver behavior:

- first tap or long press dismisses the clock screensaver and does not continue into normal menu/conversation action

Menu-open tap behavior:

- tap nav buttons to move prev/next
- tap selected main item to activate
- tap care item to activate
- tap connect item/list/keyboard according to current connect panel state
- tap settings item, volume controls, or eye-editor controls when those panels are active
- tap stats title/action zone applies current stat action

Long press:

- in eye editor: back from eye editor
- in volume panel: back from volume
- in other menus: close menu
- on closed layer: currently no-op after screensaver dismissal

### Side Buttons Through IO Expander
Side buttons:

- power: `XIO_KEY_POWER`
- up: `XIO_KEY_UP`
- down: `XIO_KEY_DOWN`

Power single click:

- if state is `Starting`, enter Wi-Fi config mode
- if a menu is open, activate current menu item or apply eye-editor increment depending on menu state
- otherwise toggle conversation

Power long press:

- if eye editor is open, back from eye editor
- if volume panel is open, back from volume
- otherwise enter Wi-Fi config mode

Up click:

- if menu open:
  - eye editor cycles mode backward
  - other menus navigate previous
- if menu closed:
  - volume +10, clamped to 100, persisted through `SetOutputVolume`

Down click:

- if menu open:
  - eye editor cycles mode forward
  - other menus navigate next
- if menu closed:
  - volume -10, clamped to 0, persisted through `SetOutputVolume`

## Audio Map

### Audio Core
Files:

- `main/audio/audio_service.h`
- `main/audio/audio_service.cc`

Audio pipeline:

1. microphone -> optional processor -> Opus encoder -> send queue -> server
2. server -> decode queue -> Opus decoder -> playback queue -> speaker

Task model:

- one task for microphone/speaker/processors
- one task for Opus encode/decode

Important constants:

- Opus frame duration: 60 ms
- audio testing max capture: 10000 ms
- audio power timeout: 15000 ms

### Active Codec
Files:

- `main/audio/audio_codec.h`
- `main/audio/audio_codec.cc`
- `main/audio/codecs/no_audio_codec.h`
- `main/audio/codecs/no_audio_codec.cc`

Active board codec:

- `NoAudioCodecDuplex`
- 24 kHz input/output I2S at board level
- audio service resamples mic input to 16 kHz for Opus encoder if needed
- output decoder is opened at codec output sample rate

Current `NoAudioCodecDuplex::Write()` adds board-specific speaker tuning:

- high-pass cleanup for tiny speakers
- mild presence lift for speech clarity
- soft limiter
- volume scaling from `output_volume_`

Current `NoAudioCodecDuplex::Read()`:

- reads I2S with 200 ms timeout
- applies `input_gain_`
- clamps to int16 range

Volume/gain persistence:

- volume stored in NVS namespace `audio`, key `output_volume`
- input gain stored in NVS namespace `audio`, key `input_gain`, scaled by 10
- board seeds default preview volume 12, but does not overwrite saved NVS volume
- board sets input gain 3.0 and persists it

### Wake Word
Files:

- `main/audio/wake_words/afe_wake_word.*`
- `main/audio/wake_words/custom_wake_word.*`
- `main/audio/wake_words/esp_wake_word.*`

Current round-board defaults use:

- `CONFIG_USE_AFE_WAKE_WORD=y`
- `CONFIG_USE_CUSTOM_WAKE_WORD` is currently not set in active `sdkconfig`
- `CONFIG_SEND_WAKE_WORD_DATA=y`

Current offline command note:

- custom command assets may be present in `assets/index.json`, but command handling only runs when `CONFIG_USE_CUSTOM_WAKE_WORD=y`
- when custom wake word mode is enabled, `CustomWakeWord` arms a local command window (`command_window_timeout_ms = 10000`) after the wake command
- local command actions are emitted as `__local_cmd__:<action>`
- current `Application::HandleWakeWordDetectedEvent()` local action mapping handles `smile` and `angry`

`Application` default listening mode:

- AEC off -> `kListeningModeAutoStop`
- AEC on -> `kListeningModeRealtime`

Manual eye tap uses `StartListening()` and opens with `kListeningModeManualStop`.

### Audio Testing
Audio testing is entered from `WifiConfiguring` when chat/start-listening is toggled.

Current behavior:

- `EnableAudioTesting(true)` captures mic audio into an Opus testing queue
- stopping testing moves the testing queue to decode queue for playback
- state returns to `WifiConfiguring`

This is not the same as SD speaker test.

### Local OGG Playback Trap
Files:

- `main/audio/demuxer/ogg_demuxer.h`
- `main/audio/demuxer/ogg_demuxer.cc`

Local sound playback expects:

- Ogg container
- Opus codec packets
- `OpusHead`
- `OpusTags`

Do not assume any `.ogg` file is playable. Ogg Vorbis will not work in this local playback path.

### Emotion Voices
Files:

- `main/audio/emotion_voice_map.h`
- `main/audio/emotion_voice_map.cc`

Mapped Bubu voices:

- `happy`: `bubu_happy2`, `bubu_happy1`, `bubu_happy3`
- `laugh`, `laughing`: `bubu_laugh`
- `sad`: `bubu_sad1`, `bubu_sad2`
- `angry`: `bubu_angry1`, `bubu_angry2`
- `bored`: `bubu_bored1`
- `curious`, `thinking`, `winking`, `silly`: `bubu_curious1`
- `tired`, `sleepy`: `bubu_tired1`
- `confused`: `bubu_bored1`
- `embarrassed`: mumbling/sing variants plus `bubu_bored1`
- `nervous`, `anxious`: mumbling/sing variants

Recognized but currently no automatic voice:

- `neutral`
- `relaxed`
- `cool`
- `shocked`
- `surprised`

### Passive Interaction Voices
Files:

- `main/audio/bubu_interaction_voice.h`
- `main/audio/bubu_interaction_voice.cc`

Events:

- eye tap -> `bubu_tap`, no cooldown update
- blink -> `bubu_blink`, gated by global and blink cooldown and random chance
- mischief -> mumbling or occasional sing variant, gated by global and mischief cooldown and random chance

Current cooldown constants:

- global: 2000 ms
- blink: 8000 ms
- mischief: 6000 ms

## Display / Eyes

### Display Base
Files:

- `main/display/display.h`
- `main/display/display.cc`
- `main/display/lcd_display.h`
- `main/display/lcd_display.cc`
- `main/display/lvgl_display/*`

Provides:

- LVGL display infrastructure
- status bar
- notification display
- fonts/images/theme support
- GIF/JPEG helpers

### Active Face UI
Files:

- `main/display/eye_display.h`
- `main/display/eye_display.cc`
- `main/display/eye_animation.h`
- `main/display/eye_animation.cc`

`EyeDisplay` is the active UI for the round board.

Current behavior:

- forces a black/dark display background
- hides the normal emoji box
- creates a full-screen `EyeAnimation` canvas
- re-parents top/status bars to keep boot/status text visible above the eye canvas
- maps emotion strings to persistent RoboEyes-style eye moods
- updates the clock screensaver from the status-bar update loop
- runs care-driven idle emotion scheduling
- gates the mischief engine to menu-closed/screensaver-off
- schedules interaction voices safely through `Application`

### Clock Screensaver
Current behavior:

- appears only in `Idle`
- does not appear while menu or notification is active
- idle timeout: 5 minutes
- refresh interval: 1 second
- time format: `%H:%M`
- date format: `%d/%m/%y`
- if system time year is before 2025, labels show `--:--` and `--/--/--`
- first tap/long press dismisses it

### Eye Emotion Mapping
`EyeEmotion_Apply()` supports:

- neutral / relaxed / cool
- happy / funny
- laughing / confident / loving / kissy / delicious / shocked
- surprised
- sad / crying
- worried
- embarrassed
- nervous / anxious
- angry
- annoyed
- sleepy
- thinking / winking / silly
- skeptic / skeptical
- doubt / doubtful
- confused

Notable behavior:

- moods are persistent until replaced
- `confused` triggers a horizontal shake
- laughing-style emotions trigger a vertical laugh animation
- embarrassed/nervous/anxious use sweat effects
- skeptic/doubt use asymmetric lid effects

### Care-Driven Idle Emotions
`EyeDisplay` uses care stats to pick idle emotions while:

- device is idle
- menu is closed
- screensaver is inactive

Care-driven emotion table currently includes:

- neutral
- relaxed
- happy
- laughing
- surprised
- skeptic
- skeptical
- doubt
- worried
- sad
- annoyed
- angry
- sleepy
- embarrassed
- nervous
- anxious

External/server emotion overrides are held for about 4000 ms before care-driven selection can override them.

### Mischief Engine
Implemented in:

- `main/display/eye_animation.*`

Current behavior:

- enabled only when idle, menu closed, screensaver off
- randomly changes eye width, height, border radius, and color
- may affect both eyes together, both independently, left only, or right only
- uses a bright playful palette
- returns to saved base eye shape and base colors after each cycle
- emits a mischief interaction event for optional local voice playback

Default config:

- width range: 52 to 96
- height range: 34 to 96
- radius range: 0 to 36
- color channel range: 64 to 255
- change duration: 160 ms
- retreat duration: 220 ms
- stay duration: 1800 to 20000 ms

Important correction: in the current board code, tapping the eyes no longer directly triggers a manual mischief action. A closed-menu eye tap toggles conversation.

### Eye Personalization
Files:

- `main/display/menu_system.*`
- `main/display/eye_display.*`
- `main/display/eye_animation.*`
- `main/settings.*`

Persisted NVS namespace:

- `display`

Persisted keys:

- `eye_l_w`
- `eye_l_h`
- `eye_l_r`
- `eye_r_w`
- `eye_r_h`
- `eye_r_r`

Factory/default shape:

- width 80
- height 80
- radius 24

Editor constraints:

- width: 48 to 110
- height: 24 to 110
- radius: 0 to 48, additionally clamped to half width/height

Current status:

- eye editor code and panel exist
- preview/save/reset/back logic exists
- unsaved edits restore saved/base shapes on close/back
- the current settings menu exposes only `VOLUME`, so the eye editor is not reachable through the visible settings list unless another code path is added

## Menu / UI

### Menu Core
Files:

- `main/display/menu_system.h`
- `main/display/menu_system.cc`

Current public `MenuState` values include:

- `MENU_CLOSED`
- `MENU_OPEN`
- `MENU_CARE_OPEN`
- `MENU_FEEDING`
- `MENU_CONNECT_OPEN`
- `MENU_KEYBOARD_OPEN`
- `MENU_MESSAGE_OPEN`
- `MENU_STATS_OPEN`
- `MENU_OPTIONS_OPEN`
- `MENU_GAMES_OPEN`
- `MENU_GAME_ACTIVE`
- `MENU_LEVEL_OPEN`
- `MENU_NOTES_OPEN`
- `MENU_NOTE_DETAIL_OPEN`
- `MENU_SETTINGS_OPEN`
- `MENU_VOLUME_OPEN`
- `MENU_EYE_EDITOR_OPEN`
- `MENU_SLEEP_OPEN`

Current visible top-level menu items:

- Care
- Connect
- Message
- Notes
- Settings

Current implementation status:

- Care: implemented
- Connect: implemented
- Stats: implemented
- Volume: implemented
- Feeding overlay: implemented if `feeding` emoji or `feeding.gif` asset is available
- Eye editor: code exists, but not reachable from current visible settings item list
- Message: logs "not yet implemented"
- Notes: logs "not yet implemented"
- Options/Games/Level/Sleep deeper flows: functions exist but are empty or placeholder

Menu inactivity timeout:

- 30 seconds
- closes menu and returns to idle eyes

### Care Menu
Visible care items:

- Feed
- Play
- Clean
- Sleep
- Status
- Level

Current actions:

- Feed: adds hunger and tries feeding animation overlay
- Play: adds mood
- Clean: adds cleanliness
- Sleep: adds energy
- Status: opens stats panel
- Level: shows "Level screen coming soon"

Feeding animation:

- first tries emoji named `feeding` from the active LVGL theme
- then tries `feeding.gif` from the mapped assets partition
- shows "Feeding GIF missing" if neither is available
- runs for 2000 ms
- timer callback schedules overlay cleanup on the app task

`main/assets.bin` currently includes an entry named `feeding.gif`.

### Stats Panel
Stats:

- hunger
- mood
- energy
- cleanliness

The stats action zone applies the action for the currently selected stat.

### Connect Menu
Connection methods:

- phone setup
- on-device setup

Phone setup:

- calls `WifiBoard::EnterWifiConfigMode()`
- enters phone-help view with AP/browser instructions

On-device setup:

- starts a scan through `WifiConnectService`
- shows scanned SSIDs sorted by RSSI
- marks known SSIDs
- selecting an SSID opens the T9-style password keyboard
- OK stores credentials through `SsidManager` and starts station mode

Keyboard:

- 15 buttons
- T9 multi-tap timeout: 1100 ms
- caps toggle
- OK
- backspace
- password max buffer: 64 chars plus terminator

### Settings / Volume
Current visible settings list contains only:

- `VOLUME`

Volume panel:

- preview step size: 10
- clamps 0 to 100
- commits on back or close
- uses `AudioCodec::PreviewOutputVolume()` while editing
- persists through `AudioCodec::PersistOutputVolume()`

### SD Speaker Test
Board API:

- `Board::StartSpeakerTest()`
- implemented by `Esp32S3RoundI80Board::StartSpeakerTest()`

Current behavior:

- allowed only in `Idle` or `AudioTesting`
- mounts SD card lazily at `/sdcard`
- searches `/sdcard` and `/sdcard/speaker_test`
- accepts `.ogg` and `.oga` filenames
- max file size: 2 MB
- loads one file, cycles index each run, schedules `Application::PlaySound(...)`
- shows notifications for mount/read/no-file/task/busy failures

Current reachability:

- the board API exists
- the current visible menu does not expose a speaker-test settings item

## Wi-Fi Map

Files:

- `main/boards/common/wifi_board.*`
- `main/boards/common/wifi_connect_service.*`
- `main/display/menu_system.cc`

Startup policy:

- saved credentials present -> start station connect with 60 s timeout
- no saved credentials -> stay offline and emit startup-idle
- no automatic provisioning on empty credentials

Config/provisioning policy:

- user enters phone config mode from power button or connect menu
- `EnterWifiConfigMode()` can reset protocol and delay one second if currently idle/listening/speaking
- if called outside `Starting`, `Idle`, `Listening`, or `Speaking`, it logs an error and refuses

On-device scan/connect:

- `WifiConnectService::StartScan()` creates `wifi_ui_scan` task
- scan stops config AP/station, creates temporary station netif, scans, sorts by RSSI, deduplicates SSIDs, marks known networks, then stops/destroys temp station
- `ConnectTo()` creates `wifi_ui_connect` task
- connect stores SSID/password and starts station mode

Power policy on round board:

- Wi-Fi power timer starts at boot for 60 seconds
- on `Listening` or `Connecting`: stop timer and set `PERFORMANCE`
- on `Idle`: reset timer
- on timer expiry: set `LOW_POWER`
- eye tap conversation trigger sets `BALANCED` and resets timer

## Persistence Map

Generic NVS wrapper:

- `main/settings.h`
- `main/settings.cc`

Used by:

- audio volume/gain
- eye shape settings
- care stats
- level/XP
- other small project settings

### Care System
Files:

- `main/care_system.h`
- `main/care_system.cc`

NVS namespace:

- `care_stats`

Keys:

- `has`
- `h`
- `m`
- `e`
- `c`

Default first-boot stat:

- 30

Runtime initial static values before load:

- 80 each, but first-boot save writes default 30 if no saved snapshot exists

Stats:

- hunger
- mood
- energy
- cleanliness

Decay:

- hunger: -1 per 6 minutes
- mood: -1 per 8 minutes
- energy: -1 per 5 minutes
- cleanliness: -1 per 10 minutes
- internal update tick: 60 seconds
- save interval: 3 minutes

Boost constants:

- feed/hunger: +30
- play/mood: +10
- sleep/energy: +90
- bath/cleanliness: +90
- clean animation reward: +90

Positive stat changes add XP by improved points divided by 10.

### Level System
Files:

- `main/level_system.h`
- `main/level_system.cc`

NVS namespace:

- `bubu-level`

Keys:

- `level`
- `xp`

Defaults:

- level 1
- XP 0

XP required for next level:

- `50 + current_level * 25`

Saves:

- immediate on some level-up cases
- otherwise deferred up to 30 seconds through `LevelSystem::Tick()`

Feature unlock enum exists, but many entries are legacy gates and not necessarily wired to active UI.

## Assets Map

Generated/compiled bindings:

- `main/assets.h`
- `main/assets.cc`
- `main/assets/lang_config.h`

Runtime assets partition:

- partition label `assets`
- `Assets` mmaps the partition and exposes file lookup by name
- current `main/assets.bin` includes at least `srmodels.bin`, `feeding.gif`, and `index.json`

Embedded common sounds currently present in `main/assets/common`:

- `bubu_angry1.ogg`
- `bubu_angry2.ogg`
- `bubu_blink.ogg`
- `bubu_bored1.ogg`
- `bubu_curious1.ogg`
- `bubu_happy1.ogg`
- `bubu_happy2.ogg`
- `bubu_happy3.ogg`
- `bubu_laugh.ogg`
- `bubu_mumbling_1.ogg`
- `bubu_mumbling_2.ogg`
- `bubu_mumbling_3.ogg`
- `bubu_mumbling_4.ogg`
- `bubu_sad1.ogg`
- `bubu_sad2.ogg`
- `bubu_sing1.ogg`
- `bubu_sing2.ogg`
- `bubu_sing3.ogg`
- `bubu_sing4.ogg`
- `bubu_tap.ogg`
- `bubu_tired1.ogg`
- `exclamation.ogg`
- `low_battery.ogg`
- `popup.ogg`
- `success.ogg`
- `vibration.ogg`

Raw voice source folder:

- `asset_sources/bubu_voice_ogg`

Important:

- files in `asset_sources/bubu_voice_ogg` are not active just because they exist there
- active local sound bindings come from files copied into `main/assets/common` and regenerated into `assets.cc` / `lang_config.h`
- local playback still requires Ogg Opus

## Protocol / Network Audio

Files:

- `main/protocols/protocol.*`
- `main/protocols/mqtt_protocol.*`
- `main/protocols/websocket_protocol.*`

`Application` owns the active protocol instance.

Important paths:

- opens audio channel before listening
- sends Opus packets from audio send queue
- handles wake-word-detected flow
- handles hidden text prompt for proactive conversation
- closes/resets protocol when entering Wi-Fi config mode from active states

## Current Customizations In This Project

These are not stock upstream assumptions:

- active board is `esp32s3-1.28-round-i80`
- direct I2S audio via `NoAudioCodecDuplex`
- round-board speaker EQ/limiter in `NoAudioCodecDuplex::Write()`
- Bubu-specific local emotion voices
- passive tap/blink/mischief voices
- idle UI is animated eyes
- care-driven idle emotion scheduler exists
- clock screensaver exists
- mischief engine exists
- eye tap currently toggles conversation, not manual mischief
- menu opens by tapping outside the eyes
- Wi-Fi no longer auto-enters provisioning when no saved SSID exists
- connect menu supports phone setup and on-device scan/password entry
- care/stats menu is backed by `CareSystem`
- feeding overlay uses theme `feeding` emoji or assets-partition `feeding.gif`
- settings currently exposes volume only
- eye editor and SD speaker test code exist but are not currently visible settings-menu entries

## Known Traps / Do Not Guess

### 1. Do Not Assume `.ogg` Is Enough
Local playback requires Ogg Opus with `OpusHead` and `OpusTags`.

If a local sound does not play, check codec/container format before touching board wiring.

### 2. Do Not Switch Audio To ES83xx Logic
The active round board uses direct I2S and `NoAudioCodecDuplex`.

If sound is wrong, inspect:

- Ogg Opus format
- decode path
- PCM level
- software volume
- `NoAudioCodecDuplex::Write()` EQ/limiter
- I2S pins and sample rates

Do not assume there is a register-configured external codec.

### 3. Do Not Run Heavy UI/State Work In `esp_timer`
Current timer code schedules UI/state work with `Application::Schedule(...)`.

Keep timers/ISRs minimal.

### 4. Do Not Treat `references/bubu_ota` As Runtime Truth
`references/bubu_ota` is a reference project outside the active firmware root.

Useful for:

- old menu ideas
- pet/care logic ideas
- touch behavior history

Not authoritative for active firmware.

### 5. Do Not Treat `asset_sources/bubu_voice_ogg` As Runtime Truth
`asset_sources/bubu_voice_ogg` is a source/staging asset folder.

Runtime embedded sounds must be in:

- `main/assets/common`
- generated bindings
- flashed/compiled assets as applicable

### 6. Do Not Assume Eye Editor Is Reachable
Eye editor code exists and is handled by board/menu input paths, but the current visible settings list only has volume.

If adding an "Eyes" settings item, wire it deliberately through `SettingsItem`, settings panel labels, navigation, and activation.

### 7. Do Not Assume Speaker Test Is Reachable
The board `StartSpeakerTest()` implementation exists, but there is no current visible settings item calling it.

If adding speaker test UI, call the board API and preserve state checks.

### 8. Do Not Overwrite Saved Eye Base Shape With Temporary Preview
Preview and saved base shape are separate.

Unsaved editor changes restore saved/base on exit. Mischief returns to saved base, not temporary preview.

### 9. Comments May Be Older Than Behavior
Example: some IMU comments still say eye tilt tracking is enabled, but current `UpdateImuOffset()` decays IMU offset to zero. Verify behavior in code, not comments alone.

## Recommended Read Order

### Board / Hardware Behavior
1. `BUBU_SYSTEM_MAP.md`
2. `main/boards/esp32s3-1.28-round-i80/config.h`
3. `main/boards/esp32s3-1.28-round-i80/esp32s3_round_i80_board.cc`
4. vendor package only if schematic-level confirmation is needed

### Audio
1. `main/audio/audio_service.h`
2. `main/audio/audio_service.cc`
3. `main/audio/audio_codec.*`
4. `main/audio/codecs/no_audio_codec.*`
5. `main/audio/demuxer/ogg_demuxer.*`
6. `main/audio/emotion_voice_map.*`
7. `main/audio/bubu_interaction_voice.*`

### Eyes / Character Behavior
1. `main/display/eye_display.*`
2. `main/display/eye_animation.*`
3. `main/display/menu_system.*`
4. `main/care_system.*`

### Menu / UI
1. `main/display/menu_system.h`
2. `main/display/menu_system.cc`
3. `main/display/eye_display.cc`
4. `main/boards/esp32s3-1.28-round-i80/esp32s3_round_i80_board.cc`

### Wi-Fi UX
1. `main/boards/common/wifi_board.*`
2. `main/boards/common/wifi_connect_service.*`
3. `main/display/menu_system.*`
4. `main/application.*`

### Care / Level
1. `main/care_system.*`
2. `main/level_system.*`
3. `main/display/menu_system.*`
4. `main/display/eye_display.*`

### Assets / Local Sounds
1. `main/assets/common`
2. `main/assets/lang_config.h`
3. `main/assets.cc`
4. `main/audio/demuxer/ogg_demuxer.*`
5. `scripts/ogg_converter` if converting new local sounds

## Short Practical Summary
If a future agent only remembers these, remember:

1. Active firmware is `/Users/judes/Downloads/xiaozhi-main/xiaozhi-doremon/xiaozhi-esp32`, board `esp32s3-1.28-round-i80`.
2. Active audio is direct I2S with `NoAudioCodecDuplex`, not ES83xx.
3. The active face UI is `EyeDisplay` + `EyeAnimation` + `MenuSystem`.
4. Eye tap currently toggles conversation; tap outside eyes opens the menu.
5. Settings currently exposes volume only; eye editor and speaker test code exist but are not visible settings entries.
6. Local `.ogg` playback expects Ogg Opus.
7. Keep heavy UI/state work out of timers and schedule it onto the app task.
8. `references/bubu_ota` and `asset_sources/bubu_voice_ogg` are reference/source folders, not runtime truth.

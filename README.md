# Bubu Motion V1 Firmware

Firmware workspace for Bubu Motion V1 on ESP32-S3 round display hardware.

## Scope

Active firmware project path:
- `Bubu_Motion/`

Do not treat `references/` as runtime source.

## Active Target Hardware

- Board profile: `esp32s3-1.28-round-i80`
- MCU target: `esp32s3`
- Display: GC9A01, 240x240, I80 8-bit bus
- Touch: CST816 (`0x15`) with IO expander interrupt
- IMU: QMI8658
- Audio path: `NoAudioCodecDuplex` (direct I2S duplex)
- Partition table: `partitions/v2/16m.csv` (16MB flash layout)

Primary board files:
- `Bubu_Motion/main/boards/esp32s3-1.28-round-i80/config.h`
- `Bubu_Motion/main/boards/esp32s3-1.28-round-i80/esp32s3_round_i80_board.cc`
- `Bubu_Motion/main/boards/esp32s3-1.28-round-i80/config.json`

## What This Firmware Implements

- Application state machine with idle/listening/speaking/upgrading flows.
- Voice/audio pipeline with wake-word support, Opus streaming, and local Ogg Opus playback.
- Animated eye engine (`EyeDisplay` + `EyeAnimation`) with care-driven emotion scheduling.
- Overlay-burst emotion logic that returns to the current base emotion after short bursts.
- Menu system for care actions, connect flow, notes/messages/settings, volume, and eye editor.
- Touch, side-button, and IMU-driven interactions.
- OTA update path and assets partition flashing hooks.

## Repository Layout

- `Bubu_Motion/` : ESP-IDF project (build this folder)
- `BUBU_SYSTEM_MAP.md` : source-of-truth orientation map for this workspace
- `project_docs/` : tracker and project documentation
- `references/` : reference-only sources, not active runtime
- `esp/esp-idf/` : local ESP-IDF checkout used by this workspace

## Build Prerequisites

- macOS or Linux shell environment.
- ESP-IDF available at `./esp/esp-idf`.
- Python dependencies installed for that ESP-IDF checkout.

If dependency checks fail during `export.sh`, run:

```bash
cd esp/esp-idf
./install.sh esp32s3
```

## Build

```bash
cd Bubu_Motion
unset IDF_PATH IDF_PYTHON_ENV_PATH
source ../esp/esp-idf/export.sh
idf.py set-target esp32s3
idf.py build
```

## Flash and Monitor

```bash
cd Bubu_Motion
unset IDF_PATH IDF_PYTHON_ENV_PATH
source ../esp/esp-idf/export.sh
idf.py -p /dev/cu.<your-port> flash monitor
```

## Configuration Notes

- Board selection in `sdkconfig`: `CONFIG_BOARD_TYPE_ESP32S3_128_ROUND_I80=y`
- Wake-word toggles in `sdkconfig`: `CONFIG_USE_AFE_WAKE_WORD`, `CONFIG_SEND_WAKE_WORD_DATA`
- Current assets flash mode in `sdkconfig`: `CONFIG_FLASH_CUSTOM_ASSETS=y`

If `CONFIG_FLASH_CUSTOM_ASSETS=y`, ensure `CONFIG_CUSTOM_ASSETS_FILE` points to a real local file before flashing.

## Runtime Notes

- Local `.ogg` playback path is Ogg Opus in this codebase.
- Heavy UI/state changes should run on the app task, not directly inside timer callbacks.
- For board-specific changes, start with `BUBU_SYSTEM_MAP.md` and the round-board files listed above.

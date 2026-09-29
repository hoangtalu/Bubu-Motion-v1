# AGENTS.md

Read `/Users/judes/Downloads/Bubu-Motion-v1-main/BUBU_SYSTEM_MAP.md` first before exploring or changing this workspace.

## Scope
The active workspace root is:

- `/Users/judes/Downloads/Bubu-Motion-v1-main`

The active firmware project inside this workspace is:

- `/Users/judes/Downloads/Bubu-Motion-v1-main/Bubu_Motion`

Do not assume sibling folders are the runtime target.

## Working Rules
- Be direct.
- Do not guess. If unsure, say so.
- Do not reinvent architecture or hardware assumptions without approval.
- Prefer the active project code over older reference projects.
- For board-specific work, start from the round-board files and the system map.

## Mandatory First Reads
1. `/Users/judes/Downloads/Bubu-Motion-v1-main/BUBU_SYSTEM_MAP.md`
2. `/Users/judes/Downloads/Bubu-Motion-v1-main/Bubu_Motion/main/boards/esp32s3-1.28-round-i80/config.h`
3. `/Users/judes/Downloads/Bubu-Motion-v1-main/Bubu_Motion/main/boards/esp32s3-1.28-round-i80/esp32s3_round_i80_board.cc`


## Important Project-Specific Traps
- Local `.ogg` playback is Ogg Opus, not generic Ogg/Vorbis.
- The active audio path is `NoAudioCodecDuplex`.
- Heavy UI/state work should not run inside `esp_timer` callbacks.
- `references/bubu_ota` is a reference source, not the active runtime.

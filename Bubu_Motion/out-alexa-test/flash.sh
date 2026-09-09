#!/usr/bin/env bash
# Alexa wake word test build — hardware qualification only, NOT shippable.
#
# Answers one question: can this board wake reliably with a known-good model?
# Device responds to "Alexa", not "Hey Bubu". Bootloader and partition table are
# unchanged from production, so only the app, OTA selector and assets are written.
#
# Usage: ./flash.sh [/dev/cu.usbmodemXXXX]
set -euo pipefail
PORT="${1:-/dev/cu.usbmodem101}"
cd "$(dirname "$0")"
echo "Flashing Alexa test build to ${PORT} ..."
python -m esptool --chip esp32s3 -p "${PORT}" -b 460800 \
  --before default_reset --after hard_reset write_flash \
  --flash_mode dio --flash_size 16MB --flash_freq 80m \
  0xd000   ota_data_initial.bin \
  0x20000  xiaozhi.bin \
  0xb20000 assets-alexa-test.bin
echo
echo "Done. Say \"Alexa\" ~20 times and count. Watch the log for:"
echo "  AfeWakeWord: Model 0: wn9_alexa      <- model loaded"
echo "  Audio detection task started         <- AFE running"
echo "To go back to production: ./restore-production.sh"

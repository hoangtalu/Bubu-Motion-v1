#!/usr/bin/env bash
# Put a device back on the production (Hey Bubu) firmware and assets.
set -euo pipefail
PORT="${1:-/dev/cu.usbmodem101}"
REPO="$(cd "$(dirname "$0")/.." && pwd)"
cd "${REPO}"
cp sdkconfig.bubu-production.bak sdkconfig
source ~/esp/esp-idf/export.sh >/dev/null 2>&1
idf.py build
idf.py -p "${PORT}" flash

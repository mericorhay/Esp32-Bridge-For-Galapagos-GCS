#!/usr/bin/env bash
# Build the bridge and stage the binaries the web installer needs.
#
# Usage:  tools/build_web.sh   (run from the repo root)
#
# Produces:
#   web/builds/bootloader.bin          0x1000
#   web/builds/partition-table.bin     0x8000
#   web/builds/galapagos-bridge.bin    0x10000
#
# Serve the web/ folder (e.g. `npx serve web`) and open index.html.

set -euo pipefail

cd "$(dirname "$0")/.."

if ! command -v idf.py >/dev/null 2>&1; then
    echo "idf.py not on PATH. Activate the ESP-IDF environment first:" >&2
    echo "  source \$IDF_PATH/export.sh" >&2
    exit 1
fi

idf.py build

# The chip family decides the bootloader part layout below. ESP32 classic
# for now; ESP32-S3/C3 users adjust the manifest offsets (0x0000) or use
# `idf.py flash` directly.
APP_NAME=galapagos-bridge

mkdir -p web/builds

# The app image is named after the project; ESP-IDF names it <project>.bin.
cp "build/${APP_NAME}.bin" "web/builds/galapagos-bridge.bin"
cp build/bootloader/bootloader.bin "web/builds/bootloader.bin"
cp build/partition_table/partition-table.bin "web/builds/partition-table.bin"

echo "Staged. Serve web/ and open it in Chrome/Edge/Firefox:"
echo "  (cd web && npx serve .)"

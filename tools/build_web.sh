#!/usr/bin/env bash
# Build the bridge for every supported chip and stage the binaries the web
# installer needs, matching web/manifest.json exactly:
#
#   web/builds/esp32/bootloader.bin
#   web/builds/esp32/partitions.bin
#   web/builds/esp32/galapagos-bridge.bin
#   web/builds/esp32s3/...  (same three)
#   web/builds/esp32c3/...  (same three)
#
# The manifest's `parts[].path` and `offset` fields are what ESP Web Tools
# flashes; wrong paths here silently ship a stale build. This script is the
# only writer of web/builds, and it produces all three chip families.
#
# Usage: tools/build_web.sh    (run from the repo root)
# Serve web/ (e.g. `npx serve web`) and open index.html in Chrome or Edge.

set -euo pipefail

cd "$(dirname "$0")/.."

if ! command -v pio >/dev/null 2>&1 && ! command -v platformio >/dev/null 2>&1; then
    echo "PlatformIO not on PATH. Install it first:" >&2
    echo "  python3 -m pip install --user platformio" >&2
    exit 1
fi
PIO="$(command -v pio || command -v platformio)"

# Chip family -> PlatformIO environment (platformio.ini defines these).
# (A plain case keeps this working on macOS's legacy bash 3.2, where
# `declare -A` + `set -u` misbehaves.)
env_for() { case "$1" in esp32) echo esp32;; esp32s3) echo esp32s3;; esp32c3) echo esp32c3;; esac; }

# ESP Web Tools flashes parts at explicit offsets (see manifest.json). The
# bootloader offset differs per family: classic ESP32 uses 0x1000, S3/C3
# put the bootloader at 0x0000.
boot_offset_for() { case "$1" in esp32) echo 0x1000;; *) echo 0x0000;; esac; }

rm -rf web/builds

for chip in esp32 esp32s3 esp32c3; do
    echo "=== Building ${chip} ==="
    env="$(env_for "$chip")"
    offset="$(boot_offset_for "$chip")"
    "$PIO" run -e "$env"

    out="web/builds/${chip}"
    mkdir -p "$out"

    # PlatformIO stages ESP-IDF outputs under .pio/build/<env>/.
    src=".pio/build/$env"
    # PlatformIO names the app image after the project dir; the CMake
    # project name here is "main", but the linker output is firmware.bin.
    if [ -f "$src/firmware.bin" ]; then
        cp "$src/firmware.bin" "$out/galapagos-bridge.bin"
    else
        echo "ERROR: $src/firmware.bin not found" >&2
        exit 1
    fi
    cp "$src/bootloader.bin" "$out/bootloader.bin"
    # Manifest calls it partitions.bin; PlatformIO already names it that
    # (ESP-IDF's native build would call it partition-table.bin).
    cp "$src/partitions.bin" "$out/partitions.bin"

    echo "  -> $out/ (bootloader at $offset)"
done

echo "Staged all three chip families under web/builds/. Serve web/ and open"
echo "it in Chrome or Edge:"
echo "  (cd web && npx serve .)"

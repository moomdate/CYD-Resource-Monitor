#!/usr/bin/env bash
# Build release images for both panel variants into <out_dir> (default: release/).
#   <name>-<env>.bin          app only   - flash at 0x10000 over an existing install / OTA-style
#   <name>-<env>-factory.bin  everything - flash at 0x0 on a blank or unknown board:
#                             esptool --chip esp32 write_flash 0x0 cyd-resource-monitor-esp32dev-factory.bin
# Env: PIO (default: pio), PY (python with esptool<5; default: python3).
set -euo pipefail
cd "$(dirname "$0")/.."
OUT="${1:-release}"
PIO="${PIO:-pio}"
PY="${PY:-python3}"
mkdir -p "$OUT"
BOOT_APP0="$(ls "$HOME"/.platformio/packages/framework-arduinoespressif32*/tools/partitions/boot_app0.bin 2>/dev/null | head -1)"
[ -n "$BOOT_APP0" ] || { echo "boot_app0.bin not found - run '$PIO run' once first" >&2; exit 1; }

for env in esp32dev cyd-noinvert; do
    "$PIO" run -e "$env"
    B=".pio/build/$env"
    cp "$B/firmware.bin" "$OUT/cyd-resource-monitor-$env.bin"
    "$PY" -m esptool --chip esp32 merge_bin -o "$OUT/cyd-resource-monitor-$env-factory.bin" \
        --flash_mode dio --flash_freq 40m --flash_size 4MB \
        0x1000 "$B/bootloader.bin" 0x8000 "$B/partitions.bin" 0xe000 "$BOOT_APP0" 0x10000 "$B/firmware.bin"
done
( cd "$OUT" && shasum -a 256 cyd-resource-monitor-*.bin > SHA256SUMS.txt )
ls -l "$OUT"

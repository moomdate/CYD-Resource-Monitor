#!/usr/bin/env bash
# Build + run the host preview. PY must have Pillow (for PNG/GIF conversion).
#   PY=/path/to/venv/bin/python tools/host_preview/run.sh
# Needs the ArduinoJson headers: run `pio run` or `pio test -e native` once (PlatformIO
# downloads them into .pio/libdeps), or set ARDUINOJSON=/path/to/ArduinoJson/src.
set -euo pipefail
cd "$(dirname "$0")"
PY="${PY:-python3}"
SRC=../../src
AJ="${ARDUINOJSON:-$(ls -d ../../.pio/libdeps/*/ArduinoJson/src 2>/dev/null | head -1)}"
if [ -z "$AJ" ]; then echo "ArduinoJson not found - run 'pio test -e native' first" >&2; exit 1; fi
rm -rf out && mkdir -p out
c++ -std=c++17 -O2 -Wall -Wno-unused-function -I "$SRC" -I "$AJ" preview.cpp \
    "$SRC/ui/canvas.cpp" "$SRC/ui/monitor_ui.cpp" "$SRC/ui/monitor_model.cpp" "$SRC/ui/settings_ui.cpp" \
    "$SRC/ui/detail_ui.cpp" "$SRC/ui/widgets.cpp" "$SRC/ui/theme.cpp" \
    "$SRC/data/telemetry.cpp" "$SRC/data/sim_source.cpp" -o out/preview
./out/preview
"$PY" to_png.py

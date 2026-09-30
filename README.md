# 📊 CYD Resource Monitor — SYSTEM RESEARCH / NODE 01

**English** · [ภาษาไทย](README.th.md)

A PC/Mac hardware monitor on a **$6 ESP32 board with a 2.8" touch screen** (CYD "Cheap Yellow Display" family). One USB cable carries both power and data — no WiFi setup, no extra wiring, no drivers to install.

![PlatformIO](https://img.shields.io/badge/PlatformIO-ESP32-orange)
![Agent](https://img.shields.io/badge/agent-Windows%20%7C%20macOS%20%7C%20Linux-blue)
![License](https://img.shields.io/badge/license-MIT-green)

![Dashboard](images/dashboard.png)

> The pictures in this README are **renders of the real firmware UI code** compiled for the host (`tools/host_preview`), not photos of a board. See [What has not been tested on hardware](#status).

| ICE / LIME | VIOLET | AMBER |
|---|---|---|
| ![ICE](images/theme-ice.png) | ![VIOLET](images/theme-violet.png) | ![AMBER](images/theme-amber.png) |

| | | |
|---|---|---|
| ![Detail](images/detail.png) | ![Settings](images/settings.png) | ![Offline](images/offline.png) |
| Tap a panel → 60 s history with MIN / AVG / MAX | Theme, data source, demo scenario, brightness — saved to flash | Agent not running → clear OFFLINE state |

## Features

- **"SYSTEM RESEARCH" dashboard** (design: [`research-monitor/`](research-monitor/)) — header with node name, link state and host clock; **CPU** ring gauge with load %, core temperature, **per-core load bars** and clock / power / voltage / fan rows; **system memory** as a segmented bar; **network** RX/TX in Mbps with a scrolling graph; **NVMe** activity ring with read / write / drive temperature
- **3 colour themes** from the design — `ICE / LIME`, `VIOLET`, `AMBER` — switchable on the device and saved to flash
- **Honest data**: any value the PC does not provide, or that is older than 2.5 s, shows `--`. If the agent stops, the header turns red and an **OFFLINE / WAITING FOR AGENT** banner appears; it clears itself when data returns
- **Demo mode** (Settings → DATA SOURCE → DEMO): built-in simulator with the design's IDLE / GAMING / TRANSFER presets, or CYCLE through them — no PC needed
- **History pages** — tap CPU / RAM / NET / NVMe for a 60-second graph, MIN / AVG / MAX; the arrows also reach CPU temperature and **GPU load** (GPU name, temperature, VRAM), which the dashboard layout has no panel for
- **Flicker-free**: the static frames and labels are pre-rendered into flash; only the live widgets are drawn, each region repainted **only when its value changed** (a steady screen sends no pixels at all). No full-screen buffer — the CYD has no PSRAM
- Backlight **brightness** control; all settings persist in NVS flash across power cycles

## Architecture

```
┌─────────────┐  JSON lines @ 2 Hz   ┌──────────────┐
│   PC / Mac   │ ──── USB serial ────▶│  CYD display  │
│ monitor_agent│      115200 baud     │   firmware    │
└─────────────┘                      └──────────────┘
```

The protocol is transport-agnostic line-delimited JSON. No custom drivers: the board's CH340 USB-serial chip is supported out of the box on macOS (Big Sur+) and Windows 10/11.

### Protocol

One JSON object per line. Everything except the four top-level objects is optional; the firmware shows `--` for what is missing and never fails on a missing field.

```json
{"cpu":{"load":68,"freq":4720,"cores":16,"temp":64,"name":"AMD Ryzen 7 7800X3D","power":82.4,"volt":1.081,"fan":1236,"per_core":[72,61,69,63]},
 "ram":{"pct":38.8,"used":12.4,"total":32.0},
 "gpus":[{"name":"RTX 4070","load":54,"temp":61,"vram_used":6.3,"vram_total":12,"discrete":true}],
 "disk":{"pct":54,"act":72,"r":1843,"w":420,"temp":49,"model":"Samsung SSD 990 PRO"},
 "net":{"dl":10.03,"ul":1.52},
 "host":{"name":"research-pc","os":"win","clock":52056}}
```

| Key | Meaning | Added in this version? |
|---|---|---|
| `cpu.load / freq / cores / temp` | %, MHz, logical CPUs, °C | no |
| `cpu.name / power / volt / fan` | marketing name, W, V, RPM | **yes** |
| `cpu.per_core` | per-logical-CPU load %, at most 16 entries (more are averaged into 16) | **yes** |
| `ram.pct / used / total` | %, GiB, GiB | no |
| `gpus[]` | name, load, temp, VRAM (discrete first) | no |
| `disk.pct / r / w` | capacity used %, read / write MiB/s | no |
| `disk.act / temp / model` | busy %, °C, drive model | **yes** |
| `net.dl / ul` | MiB/s (the display converts to Mbps) | no |
| `host.name / os` | | no |
| `host.clock` | local seconds since midnight, for the header clock | **yes** |

**Compatibility**: firmware ignores keys it does not know, so an old firmware keeps working with the new agent; and the new firmware works with an old agent (it simply shows `--` for power, voltage, fan, per-core bars, drive temperature / activity and the clock). `disk.pct` (capacity) is deliberately **not** shown as NVMe activity.

## The board

This runs on a 2.8" ESP32 display board of the **CYD family** — the pinout matches the widely documented `ESP32-2432S028R`, which is what the config in `platformio.ini` is built around.

**👉 The exact board used here: [Shopee listing](https://s.shopee.co.th/30mqperiSK)** (USB-C, LCD panel marked `TPM408-2.8`)

| Part | Detail |
|---|---|
| MCU | ESP32-WROOM-32, dual-core 240 MHz, 4 MB flash, no PSRAM |
| Display | 2.8" ILI9341, 320×240, SPI @ 65 MHz |
| Touch | XPT2046 resistive (bit-banged: CLK 25, DIN 32, DOUT 39, CS 33) |
| USB-serial | CH340 — built-in OS support, no driver install |

> ⚠️ CYD units vary. If colors look inverted, flash the other environment: `pio run -e cyd-noinvert -t upload` (the default `esp32dev` environment enables `TFT_INVERSION_ON`). If touches land in the wrong place, update the `touch.setCal(...)` values in `src/main.cpp`.

## Getting started

### 1. Flash the firmware

```bash
git clone https://github.com/moomdate/CYD-Resource-Monitor.git
cd CYD-Resource-Monitor
pio run -t upload
```

Nothing to configure: with no agent running the display shows the OFFLINE banner, and *Settings → DEMO* shows the UI with simulated data.

### 2. Run the agent on the computer you want to monitor

Grab a prebuilt binary from [Releases](https://github.com/moomdate/CYD-Resource-Monitor/releases) — `cyd-monitor-agent-windows.exe` or `cyd-monitor-agent-macos` — or run from source:

```bash
cd agent
pip install -r requirements.txt
python monitor_agent.py          # auto-detects the board's port
```

> macOS note: the release binary is unsigned — right-click → Open the first time.

| Option | Use when |
|---|---|
| `--list` | show available serial ports |
| `--port COM5` | pick a port manually |
| `--print` | dry run: print JSON to stdout, no board needed |
| `--interval 1` | slow down updates (default 0.5 s) |

### 3. Unlock more sensors (optional)

| Data | What to do |
|---|---|
| NVIDIA GPU load / temp / VRAM | `pip install pynvml` |
| Windows: CPU temp / **power / voltage / fan**, **drive temp + activity + model**, AMD/Intel GPU | run [LibreHardwareMonitor](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor) with Options → Remote Web Server on (agent auto-connects to `localhost:8085`) |
| macOS: CPU temp | `brew install smctemp` |
| Linux: CPU / drive temp, fan | works through your lm-sensors kernel drivers (psutil) — experimental, not tested on a real Linux box yet |

What each OS can provide:

| | Windows (+ LHM) | Windows (no LHM) | macOS | Linux |
|---|---|---|---|---|
| CPU load, per-core, clock, RAM, network, read/write | ✅ | ✅ | ✅ | ✅ |
| CPU temperature | ✅ | – | with `smctemp` | if sensors exist |
| CPU power / voltage / fan | ✅ if the sensor exists | – | – | fan only |
| Drive temperature / model | ✅ | – | model | ✅ |
| NVMe activity % | ✅ real | ~ estimate | ~ estimate | ✅ real |

"Estimate" = throughput relative to the highest throughput seen since the agent started (at least 150 MiB/s) — a hint of how busy the drive is, not a measurement. Everything not available shows `--` on the display; the agent never sends `null`/`NaN`.

## Using the display

| Touch | Does |
|---|---|
| Header or footer (`SETUP`) | opens the settings page |
| A panel (CPU / memory / network / NVMe) | opens its 60 s history page |
| `<` `>` on a history page | steps through CPU load → CPU temp → memory → GPU → network → NVMe |
| `BACK` / `DONE` | back to the dashboard |

**Settings**: theme (thumbnails of the real art), data source (`PC AGENT` / `DEMO`), demo scenario (`IDLE` / `GAMING` / `TRANSFER` / `CYCLE`), brightness (20–100 %). A line shows whether an agent is currently connected, even while in DEMO.

Header states: green **ONLINE** (live data), amber **DEMO** (simulator), blinking red **OFFLINE** (no data for 2.5 s).

> The previous firmware's tile picker and animated RGB bar are gone: the dashboard is now one fixed layout (the design). Theme, brightness, history pages, GPU details and persisted settings were kept.

## Daily use & auto-start

### Windows

The `.exe` is **portable — no installer**. Put it anywhere, plug in the board, double-click, done. A console window opens and shows `connected to COM5`; the display starts updating immediately.

- **First run only**: SmartScreen will warn because the binary is unsigned — click *More info → Run anyway*.
- **No configuration needed**: the agent finds the board's COM port (CH340) by itself, and NVIDIA support is bundled in. Only the LHM-backed sensors (CPU temp / power / voltage / fan, drive info, AMD / Intel GPU) need LibreHardwareMonitor running alongside (see the table above).
- **After a reboot the agent does NOT start by itself** — the display shows *OFFLINE / WAITING FOR AGENT* until you run it again. To make it automatic:

| Method | Steps | Result |
|---|---|---|
| **Startup folder** (easiest) | `Win + R` → type `shell:startup` → Enter → right-drag the `.exe` in → *Create shortcut here* | runs at every login, console window stays open (minimize it) |
| **Task Scheduler** (cleaner) | create a task, trigger *At log on*, action = the `.exe`, tick *Hidden* | runs silently in the background, no window |

If you use LibreHardwareMonitor for temperatures, enable its own *Run On Windows Startup* option too.

### macOS

Run `./cyd-monitor-agent-macos` (first time: right-click → Open, because it's unsigned). To start it at login: *System Settings → General → Login Items → +* and pick the binary.

### Good to know

The agent has a built-in reconnect loop — unplugging the board, replugging it, or rebooting the display never requires restarting the agent. It just reconnects.

> ⚠️ **macOS limitation**: Apple locks GPU-load counters behind sudo-only APIs on Apple Silicon, so GPU load shows `--` on Macs (only visible on the GPU history page). CPU power / voltage / fan are also unavailable there. Windows gets the full picture with LibreHardwareMonitor running.

## Development

```bash
pio test -e native                 # host unit tests: JSON parsing (incl. missing fields), model / staleness,
                                   # renderer pushes only changed regions, settings + history taps, simulator, theme art
python -m unittest discover -s agent -v            # agent tests (fake psutil, no hardware)
pio run -e esp32dev                # firmware (or -e cyd-noinvert)
tools/host_preview/run.sh          # render the real UI code to PNGs in tools/host_preview/out/ (needs Pillow)
```

The **host preview** compiles the actual firmware UI, model, JSON parser and simulator for your computer and writes 640×480 PNGs (all themes, demo presets, agent JSON samples from `tools/host_preview/samples/` — including an old-agent message — offline / stale states, settings and history pages) plus `sim.gif`, so layout work needs no board.

### Art pipeline

```
tools/gen_chrome.py   → src/assets/bg_{ice,violet,amber}.h   static frames/labels/ring backdrops per theme (RGB565, in flash)
                      → src/assets/layout.h                  every rectangle, shared with the C++ renderer
tools/make_fonts.py   → src/fonts/*.h                        anti-aliased fonts blended against the real pixels
```

Fonts: the design uses **Barlow Condensed** and **IBM Plex Mono** (SIL OFL). If `tools/font_src/*.ttf` is missing, `make_fonts.py` / `gen_chrome.py` fall back to the font bundled with Pillow (emboldened and condensed) so everything still builds — for the exact look run `python tools/make_fonts.py --fetch` then `python tools/gen_chrome.py`, and commit the regenerated headers. To change the layout, edit `LAYOUT` in `gen_chrome.py` and re-run it. Needs Pillow + numpy.

CI (`.github/workflows/ci.yml`) runs the native tests, builds both panel variants, renders the host preview and runs the agent tests on Linux, Windows and macOS.

## Releases (CI)

Pushing a `v*` tag builds and attaches everything automatically:

```bash
git tag v1.0.0 && git push origin v1.0.0
```

| Artifact | Built on |
|---|---|
| `cyd-monitor-agent-windows.exe` | windows-latest (PyInstaller, no Python needed) |
| `cyd-monitor-agent-macos` | macos-latest |
| `cyd-resource-monitor-firmware.bin` | ubuntu-latest (PlatformIO) |

## Project structure

```
agent/
├── monitor_agent.py       # Python agent (Windows / macOS / Linux)
└── test_monitor_agent.py
research-monitor/          # the design: index.html (source of truth), concept image, LVGL reference
src/
├── config.h               # pins, timing (30 fps, 2.5 s staleness)
├── settings.h             # theme / source / scenario / brightness
├── main.cpp               # serial ingest, demo source, touch, screens, NVS, backlight
├── data/                  # telemetry.* (JSON parser + line reader), sim_source.* (demo)
├── ui/                    # monitor_model (staleness, units, history), monitor_ui (partial-redraw renderer),
│                          # canvas (RGB565 region canvas), settings_ui, detail_ui, widgets, theme
├── assets/                # generated: per-theme backgrounds + layout.h
└── fonts/                 # generated: AA fonts
tools/                     # gen_chrome.py, gen_font.py, make_fonts.py, host_preview/
test/test_native/          # host unit tests
```

<a id="status"></a>
## What has not been tested on hardware

This UI was developed without a board attached. Verified: firmware builds for both panel variants, 36 host unit tests, renders of every screen. **Not verified on a real CYD**: actual frame rate and SPI throughput, colour and contrast on the ILI9341 (the art is unmodified design colours; TFT panels look flatter than a monitor), touch hit accuracy on the small buttons, backlight PWM, NVS persistence, and long-run serial behaviour. The agent's new sensor code is tested against synthetic LibreHardwareMonitor data and a fake `psutil`, not on real Windows / Linux machines.

## License

[MIT](LICENSE) — do whatever you like; a link back is appreciated.

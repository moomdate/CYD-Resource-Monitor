# 📊 CYD Resource Monitor — SYSTEM RESEARCH / NODE 01

**English** · [ภาษาไทย](README.th.md)

A PC/Mac hardware monitor on a **$6 ESP32 board with a 2.8" touch screen** (CYD "Cheap Yellow Display" family). One USB cable carries both power and data — no WiFi setup, no extra wiring, no drivers to install.

![PlatformIO](https://img.shields.io/badge/PlatformIO-ESP32-orange)
![Agent](https://img.shields.io/badge/agent-Windows%20%7C%20macOS%20%7C%20Linux-blue)
![License](https://img.shields.io/badge/license-MIT-green)

<p align="center"><img src="images/demo.gif" width="320" alt="Dashboard in demo mode, then tapping through the CPU / temperature / memory / GPU history pages"></p>

**On a real board:**

| Live data from an M1 MacBook | Demo: GAMING | Demo: TRANSFER |
|---|---|---|
| ![Live on a Mac](images/photo-live-mac.jpg) | ![Demo gaming](images/photo-demo-gaming.jpg) | ![Demo transfer](images/photo-demo-transfer.jpg) |

<sub>The live photo was taken before the agent learned to read Apple Silicon temperatures, so CPU / NVMe temperature still show `--` there.</sub>

**Renders of the same UI code** (`tools/host_preview`, pixel-exact):

![Dashboard](images/dashboard.png)

| ICE / LIME | VIOLET | AMBER |
|---|---|---|
| ![ICE](images/theme-ice.png) | ![VIOLET](images/theme-violet.png) | ![AMBER](images/theme-amber.png) |

| | | |
|---|---|---|
| ![Detail](images/detail.png) | ![Settings](images/settings.png) | ![Offline](images/offline.png) |
| Tap a panel → 60 s history with MIN / AVG / MAX | Theme, data source, demo scenario — saved to flash | Agent not running → clear OFFLINE state |

## Features

- **"SYSTEM RESEARCH" dashboard** (design: [`research-monitor/`](research-monitor/)) — header with node name, link state and host clock; **CPU** ring gauge with load %, core temperature, **per-core load bars** and clock / power / voltage / fan rows; **system memory** as a segmented bar; **network** RX/TX in Mbps with a scrolling graph; **NVMe** activity ring with read / write / drive temperature
- **3 colour themes** from the design — `ICE / LIME`, `VIOLET`, `AMBER` — switchable on the device and saved to flash
- **Honest data**: any value the PC does not provide, or that is older than 2.5 s, shows `--`. If the agent stops, the header turns red and an **OFFLINE / WAITING FOR AGENT** banner appears; it clears itself when data returns
- **Demo mode** (Settings → DATA SOURCE → DEMO): built-in simulator with the design's IDLE / GAMING / TRANSFER presets, or CYCLE through them — no PC needed
- **History pages** — tap CPU / RAM / NET / NVMe for a 60-second graph, MIN / AVG / MAX; the arrows also reach CPU temperature and **GPU load** (GPU name, temperature, VRAM), which the dashboard layout has no panel for
- **Flicker-free**: the static frames and labels are pre-rendered into flash; only the live widgets are drawn, each region repainted **only when its value changed** (a steady screen sends no pixels at all). No full-screen buffer — the CYD has no PSRAM
- All settings persist in NVS flash across power cycles. The backlight runs full-on: this CYD's backlight cannot dim with PWM (see [Status](#status)); boards that can dim get a brightness control with `BACKLIGHT_DIMMING 1`

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

**Easiest — a ready-made image from [Releases](https://github.com/moomdate/CYD-Resource-Monitor/releases):** download
`cyd-resource-monitor-esp32dev-factory.bin` and flash it at address `0x0` (one file contains bootloader, partitions and app):

```bash
pip install esptool
esptool --chip esp32 --baud 460800 write_flash 0x0 cyd-resource-monitor-esp32dev-factory.bin
```

If the colours come out inverted (dark UI shows light), use `cyd-resource-monitor-cyd-noinvert-factory.bin` instead —
CYD panels come in two variants. Any browser-based ESP flasher that accepts a single image at `0x0` works too.
If flashing stops with *"Unable to verify flash chip connection"*, lower the baud rate (`--baud 115200`).

**Or build from source** (PlatformIO):

```bash
git clone https://github.com/moomdate/CYD-Resource-Monitor.git
cd CYD-Resource-Monitor
pio run -t upload                      # or: pio run -e cyd-noinvert -t upload
```

Nothing to configure: with no agent running the display shows the OFFLINE banner, and *Settings → DEMO* shows the UI with simulated data.

### 2. Run the agent on the computer you want to monitor

Grab a prebuilt binary from [Releases](https://github.com/moomdate/CYD-Resource-Monitor/releases) — `cyd-monitor-agent-windows.exe`, `cyd-monitor-agent-macos` or `cyd-monitor-agent-linux` — or run from source:

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

### 3. GPUs and temperatures

GPU load works for **every vendor out of the box** — no extra installs (see `agent/hwinfo.py`):

| | NVIDIA | AMD | Intel | Apple |
|---|---|---|---|---|
| **Windows** | load, VRAM (PDH) · + temp / power / clock / fan with `nvidia-ml-py` | load, VRAM (PDH) · temp with LHM or `pyadl` | load, VRAM (PDH) · temp with LHM | – |
| **Linux** | full with `nvidia-ml-py` / `nvidia-smi` | load, VRAM, temp, power, fan, clock (amdgpu sysfs) | load (RC6), clock, temp on Arc (i915 / xe sysfs) | – |
| **macOS** | – | load, VRAM (Intel Macs) | load (Intel Macs) | load + temp (Apple Silicon) |

| Temperatures | How |
|---|---|
| macOS Apple Silicon: CPU, GPU, SSD | built in (IOHID sensors, no sudo) |
| macOS Intel: CPU | `brew install smctemp` |
| Linux: CPU, drive, AMD GPU | built in (lm-sensors kernel drivers via psutil, amdgpu hwmon) |
| Windows: CPU / GPU / drive temps, CPU **power / voltage / fan**, drive activity | run [LibreHardwareMonitor](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor) with Options → Remote Web Server on (agent auto-connects to `localhost:8085`) — Windows needs a kernel driver for these |

Optional installs: `pip install nvidia-ml-py` (NVIDIA detail), `pip install pyadl` (AMD temp on Windows without LHM).
The slow probes run on a background thread, so the 2 Hz stream never waits on them; the whole agent uses about 0.7 % of one core (measured on an M1).

What each OS can provide (besides GPUs):

| | Windows (+ LHM) | Windows (no LHM) | macOS | Linux |
|---|---|---|---|---|
| CPU load, per-core, clock, RAM, network, read/write | ✅ | ✅ | ✅ | ✅ |
| CPU temperature | ✅ | – | ✅ Apple Silicon · `smctemp` on Intel | if sensors exist |
| CPU power / voltage / fan | ✅ if the sensor exists | – | – | fan only |
| Drive temperature / model | ✅ | – | ✅ Apple Silicon / model | ✅ |
| NVMe activity % | ✅ real | ~ estimate | ~ estimate | ✅ real |

"Estimate" = throughput relative to the highest throughput seen since the agent started (at least 150 MiB/s) — a hint of how busy the drive is, not a measurement. Everything not available shows `--` on the display; the agent never sends `null`/`NaN`.

## Using the display

| Touch | Does |
|---|---|
| Header or footer (`SETUP`) | opens the settings page |
| A panel (CPU / memory / network / NVMe) | opens its 60 s history page |
| `<` `>` on a history page | steps through CPU load → CPU temp → memory → GPU → network → NVMe |
| `BACK` / `DONE` | back to the dashboard |

**Settings**: theme (thumbnails of the real art), data source (`PC AGENT` / `DEMO`), demo scenario (`IDLE` / `GAMING` / `TRANSFER` / `CYCLE`), and brightness (20–100 %) only when `BACKLIGHT_DIMMING` is 1. A line shows whether an agent is currently connected, even while in DEMO.

Header states: green **ONLINE** (live data), amber **DEMO** (simulator), blinking red **OFFLINE** (no data for 2.5 s).

> The previous firmware's tile picker and animated RGB bar are gone: the dashboard is now one fixed layout (the design). Theme, history pages, GPU details and persisted settings were kept.

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

> **macOS**: GPU load and CPU / GPU / SSD temperatures work without sudo on Apple Silicon. CPU power / voltage / fan are not available there. Windows gets the full picture with LibreHardwareMonitor running.

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
git tag v2.0.0 && git push origin v2.0.0     # the tag must match FW_VERSION in src/config.h
```

| Artifact | Built on |
|---|---|
| `cyd-resource-monitor-<env>-factory.bin` | full image for `0x0` — `esp32dev` (inverted panel) and `cyd-noinvert` |
| `cyd-resource-monitor-<env>.bin` | app only, for `0x10000` over an existing install |
| `SHA256SUMS.txt` | checksums of the firmware images |
| `cyd-monitor-agent-windows.exe` / `-macos` / `-linux` | PyInstaller single-file agent, no Python needed |

`tools/make_release.sh` builds the same firmware images locally.

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

Tested on a real CYD: boots, ~29 fps, partial redraws, live data from the agent on macOS (M1), touch and theme switching. Found on the hardware: this board's backlight goes dark with any PWM duty below 100 %, so the backlight is driven full-on and BRIGHTNESS is hidden (`BACKLIGHT_DIMMING` in `config.h`). The agent is tested live on macOS (Apple Silicon: GPU load, CPU / GPU / SSD temps) and with recorded / synthetic data for NVIDIA (NVML, nvidia-smi), AMD and Intel (Linux sysfs), Windows PDH counters and LibreHardwareMonitor. **Not yet run on a real Windows or Linux PC.**

## License

[MIT](LICENSE) — do whatever you like; a link back is appreciated.

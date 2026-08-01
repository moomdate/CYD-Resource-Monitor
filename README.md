# 📊 CYD Resource Monitor

**English** · [ภาษาไทย](README.th.md)

A PC/Mac hardware monitor on a **$6 ESP32 board with a 2.8" touch screen** (CYD "Cheap Yellow Display" family). One USB cable carries both power and data — no WiFi setup, no extra wiring, no drivers to install.

![PlatformIO](https://img.shields.io/badge/PlatformIO-ESP32-orange)
![Agent](https://img.shields.io/badge/agent-Windows%20%7C%20macOS-blue)
![License](https://img.shields.io/badge/license-MIT-green)

<!-- Screenshots: drop photos into images/ with these names -->
![Dashboard](images/dashboard.jpg)

| | |
|---|---|
| ![Detail](images/detail.jpg) | ![Settings](images/settings.jpg) |
| Tap any tile → 60-second history graph with MIN / AVG / MAX | Pick tiles, theme, and RGB bar — saved to flash |

## Features

- **Dashboard** — CPU / GPU / RAM / TEMP / DISK / NET tiles with donut gauges, area-filled sparklines, and a layout that auto-adjusts to however many tiles you enable (1–6)
- **4 themes**, switchable on-device: `CYBER` (neon on black), `SYNTHWAVE` (purple/pink/orange), `MATRIX` (all green), `LIGHT`
- **Animated RGB bar** under the header, motherboard-style — and yes, you can turn it off in Settings
- Tiles pulse a **red warning border** past 85% load
- **Detail pages** — tap a tile for a 60-second graph, MIN/AVG/MAX, GPU name, dGPU/iGPU flag, VRAM
- **Discrete GPU detection** — the agent reports every GPU it finds, discrete cards first
- All settings persist in NVS flash across power cycles

## Architecture

```
┌─────────────┐  JSON lines @ 2 Hz   ┌──────────────┐
│   PC / Mac   │ ──── USB serial ────▶│  CYD display  │
│ monitor_agent│      115200 baud     │   firmware    │
└─────────────┘                      └──────────────┘
```

The protocol is transport-agnostic line-delimited JSON — the same lines could ride a WebSocket later without changing either side. No custom drivers: the board's CH340 USB-serial chip is supported out of the box on macOS (Big Sur+) and Windows 10/11.

## The board

This runs on a 2.8" ESP32 display board of the **CYD family** — the pinout matches the widely documented `ESP32-2432S028R`, which is what the config in `platformio.ini` is built around.

**👉 The exact board used here: [Shopee listing](https://s.shopee.co.th/30mqperiSK)** (USB-C, LCD panel marked `TPM408-2.8`)

| Part | Detail |
|---|---|
| MCU | ESP32-WROOM-32, dual-core 240 MHz, 4 MB flash, no PSRAM |
| Display | 2.8" ILI9341, 320×240, SPI @ 65 MHz |
| Touch | XPT2046 resistive (bit-banged: CLK 25, DIN 32, DOUT 39, CS 33) |
| USB-serial | CH340 — built-in OS support, no driver install |

> ⚠️ CYD units vary. If colors look inverted, remove `-D TFT_INVERSION_ON=1` from `platformio.ini`. If touches land in the wrong place, update the `touch.setCal(...)` values in `src/main.cpp`.

## Getting started

### 1. Flash the firmware

```bash
git clone https://github.com/moomdate/CYD-Resource-Monitor.git
cd CYD-Resource-Monitor
pio run -t upload
```

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
| Windows: CPU temp + AMD/Intel GPU | run [LibreHardwareMonitor](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor) with Options → Remote Web Server on (agent auto-connects to `localhost:8085`) |
| macOS: CPU temp | `brew install smctemp` |

The agent degrades gracefully — anything unavailable shows as `n/a` on the display.

> ⚠️ **macOS limitation**: Apple locks GPU-load counters behind sudo-only APIs on Apple Silicon, so GPU load shows `n/a` on Macs. Windows gets the full picture with LibreHardwareMonitor running.

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
└── monitor_agent.py   # Python agent (Windows + macOS)
src/
├── config.h           # pins
├── ui.h               # sprite renderer, themes, touch, widgets
├── data.h             # JSON parse, history rings, settings NVS
├── dash.h             # dashboard grid
├── detail.h           # per-metric graph page
├── settings.h         # theme / RGB / tile picker
└── main.cpp           # loop + screen switching
```

## License

[MIT](LICENSE) — do whatever you like; a link back is appreciated.

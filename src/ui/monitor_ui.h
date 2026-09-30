#pragma once
// Renders a MonitorView onto the themed "SYSTEM RESEARCH" background.
// The static chrome (frames, titles, labels) is a pre-rendered RGB565 image in flash
// (tools/gen_chrome.py). On top of it the screen is split into independent live regions
// (clock, status, CPU ring, per-core bars, RAM, network graph, NVMe ring ...). Each frame a
// region is re-composed - background + AA shapes + AA text in a small scratch canvas - and
// pushed ONLY if what it shows changed, so a steady screen costs almost no SPI time and
// nothing flickers. There is no full-frame buffer.
#include <stdint.h>
#include "ui/monitor_model.h"
#include "ui/theme.h"

class Canvas;

// Pushes a w*h block of plain RGB565 pixels to the screen at (x,y).
typedef void (*PushFn)(int x, int y, int w, int h, const uint16_t *pixels);

namespace monitor_ui {

void begin(PushFn push, const Theme &theme);
void setTheme(const Theme &theme);   // swap background + accents, full redraw
const Theme &theme();
void redrawAll();                    // repaint background, then every region on the next render()
void invalidate();                   // force every region to redraw next frame
// demoLabel: scenario name shown in the footer while the source is the simulator (else nullptr)
void render(const MonitorView &v, const char *demoLabel);
int  lastPushedRegions();            // how many regions the last render() pushed
Canvas &canvas();                    // shared scratch canvas (settings / detail pages use it too)
PushFn  pushFn();

// Touch zones (screen coordinates)
enum Zone : uint8_t { Z_NONE, Z_SETUP, Z_CPU, Z_RAM, Z_NET, Z_DISK };
Zone hit(int x, int y);

}  // namespace monitor_ui

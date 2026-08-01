#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <TFT_Touch.h>
#include <Preferences.h>
#include "config.h"

TFT_eSPI    tft;
TFT_eSprite fb(&tft);   // full-frame 8-bit back buffer
TFT_Touch   touch(TOUCH_DCS, TOUCH_DCLK, TOUCH_DIN, TOUCH_DOUT);
Preferences prefs;

// ── Screens ────────────────────────────────────────
enum { SCR_DASH = 0, SCR_DETAIL, SCR_SETTINGS };
int g_screen = SCR_DASH;

// ── Themes (colors chosen to survive 8-bit RRRGGGBB) ─
struct Theme {
  const char *name;
  uint16_t bg, card, border, grid, text, muted, accent;
  uint16_t cpu, gpu, ram, temp, disk, net;
  uint16_t ok, warn, bad;
  bool light;
};

const Theme themes[] = {
  {"CYBER",     TFT_BLACK, 0x0008, 0x4208, 0x0841, TFT_WHITE, 0x8410, TFT_CYAN,
   TFT_CYAN, TFT_MAGENTA, TFT_YELLOW, TFT_ORANGE, TFT_GREEN, 0x255F,
   TFT_GREEN, TFT_YELLOW, TFT_RED, false},
  {"SYNTHWAVE", TFT_BLACK, 0x2008, 0x8010, 0x2004, TFT_WHITE, 0x8410, 0xF81F,
   0xFD20, 0xF81F, 0xFFE0, 0xFB2C, 0x07FF, 0xAD5F,
   TFT_GREEN, TFT_YELLOW, TFT_RED, false},
  {"MATRIX",    TFT_BLACK, 0x0120, 0x0480, 0x0100, 0x9FE6, 0x0480, 0x07E0,
   0x07E0, 0x9FE6, 0xAFE0, 0xFFE0, 0x04A0, 0x07EF,
   0x07E0, TFT_YELLOW, TFT_RED, false},
  {"LIGHT",     0xEF7D, TFT_WHITE, 0x8410, 0xC618, TFT_BLACK, 0x630C, 0x001F,
   0x019F, 0xA017, 0xBC40, 0xCB20, 0x0460, 0x001F,
   0x0460, 0xBC40, TFT_RED, false},
};
#define N_THEMES 4
int themeIdx = 0;
#define th (themes[themeIdx])

#define C_BG     th.bg
#define C_CARD   th.card
#define C_BORDER th.border
#define C_MUTED  th.muted
#define C_TEXT   th.text
#define C_OK     th.ok
#define C_WARN   th.warn
#define C_BAD    th.bad

const uint16_t rgbCycle[6] = {
  TFT_RED, TFT_ORANGE, TFT_YELLOW, TFT_GREEN, TFT_CYAN, TFT_MAGENTA
};

uint16_t dimc(uint16_t c) { return (c >> 1) & 0x7BEF; }

// ── Input with edge detection ──────────────────────
struct Input {
  bool down = false, justDown = false, justUp = false;
  int  x = 0, y = 0;
} in;

void inputUpdate() {
  bool p = touch.Pressed();
  in.justDown = p && !in.down;
  in.justUp   = !p && in.down;
  in.down     = p;
  if (p) { in.x = touch.X(); in.y = touch.Y(); }
}

bool tapIn(int x, int y, int w, int h) {
  return in.justDown && in.x >= x && in.x < x + w && in.y >= y && in.y < y + h;
}

// ── Widgets ────────────────────────────────────────
bool uiButton(int x, int y, int w, int h, const char *label, uint16_t col) {
  bool over = in.x >= x && in.x < x + w && in.y >= y && in.y < y + h;
  bool hot  = in.down && over;
  fb.fillRoundRect(x, y, w, h, 6, hot ? col : C_CARD);
  fb.drawRoundRect(x, y, w, h, 6, col);
  fb.setTextFont(2); fb.setTextSize(1); fb.setTextDatum(MC_DATUM);
  fb.setTextColor(hot ? C_BG : col);
  fb.drawString(label, x + w / 2, y + h / 2);
  return in.justDown && over;
}

uint16_t loadColor(float pct) {
  if (pct < 60) return C_OK;
  if (pct < 85) return C_WARN;
  return C_BAD;
}

// animated RGB accent strip — the PC-modder signature
void rgbStrip(int y, int hgt = 2) {
  int t = millis() / 120;
  for (int x = 0; x < SCR_W; x += 4)
    fb.fillRect(x, y, 4, hgt, rgbCycle[(x / 4 + t) % 6]);
}

// dotted background grid
void bgGrid(int top) {
  for (int y = top + 8; y < SCR_H; y += 16)
    for (int x = 8; x < SCR_W; x += 16)
      fb.drawPixel(x, y, th.grid);
}

// cyber corner brackets on a rect
void corners(int x, int y, int w, int h, uint16_t col) {
  fb.drawFastHLine(x + 3, y, 10, col);      fb.drawFastVLine(x, y + 3, 10, col);
  fb.drawFastHLine(x + w - 13, y + h - 1, 10, col);
  fb.drawFastVLine(x + w - 1, y + h - 13, 10, col);
}

// 300-degree gauge arc, gap at the bottom
void gauge(int cx, int cy, int r, float frac, uint16_t col) {
  frac = constrain(frac, 0.0f, 1.0f);
  int ir = r - 5;
  fb.drawArc(cx, cy, r, ir, 30, 330, th.light ? 0xC618 : 0x2104, C_BG, false);
  if (frac > 0.01f)
    fb.drawArc(cx, cy, r, ir, 30, 30 + (int)(frac * 300), col, C_BG, false);
  fb.drawCircle(cx, cy, ir - 3, th.grid);   // techy inner ring
}

// sparkline; optional area fill below the line
void sparkline(int x, int y, int w, int h, const float *ring, int head,
               int total, int n, float vmax, uint16_t col, bool fill = false) {
  if (total < 2 || vmax <= 0) return;
  n = min(n, total);
  uint16_t dc = dimc(dimc(col));
  int px = -1, py = -1;
  for (int i = 0; i < n; i++) {
    int idx = (head - n + i + 240) % 120;        // ring length 120
    float v = constrain(ring[idx] / vmax, 0.0f, 1.0f);
    int gx = x + i * w / (n - 1);
    int gy = y + h - 1 - (int)(v * (h - 1));
    if (fill && (i & 1)) fb.drawFastVLine(gx, gy, y + h - gy, dc);
    if (px >= 0) fb.drawLine(px, py, gx, gy, col);
    px = gx; py = gy;
  }
}

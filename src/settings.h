#pragma once
#include "data.h"

namespace settings {

uint8_t draft;          // edited copy of tilesMask
int     draftTheme;
bool    draftRgb;
bool    editing = false;

void update() {
  if (!editing) {
    draft = tilesMask; draftTheme = themeIdx; draftRgb = rgbOn;
    editing = true;
  }

  int savedTheme = themeIdx;
  themeIdx = draftTheme;          // live-preview the chosen theme

  fb.fillSprite(C_BG);
  bgGrid(0);
  fb.setTextFont(4); fb.setTextSize(1); fb.setTextDatum(TC_DATUM);
  fb.setTextColor(C_TEXT);
  fb.drawString("SETTINGS", SCR_W / 2, 2);
  if (draftRgb) rgbStrip(26, 2);
  else          fb.drawFastHLine(0, 27, SCR_W, C_BORDER);

  // theme selector
  fb.setTextFont(2); fb.setTextDatum(ML_DATUM); fb.setTextColor(C_MUTED);
  fb.drawString("THEME", 24, 46);
  if (uiButton(96, 33, 30, 26, "<", th.accent))
    draftTheme = (draftTheme + N_THEMES - 1) % N_THEMES;
  fb.setTextDatum(MC_DATUM); fb.setTextFont(2); fb.setTextColor(th.accent);
  fb.fillRoundRect(132, 33, 120, 26, 6, C_CARD);
  fb.drawRoundRect(132, 33, 120, 26, 6, th.accent);
  fb.drawString(th.name, 192, 46);
  if (uiButton(258, 33, 30, 26, ">", th.accent))
    draftTheme = (draftTheme + 1) % N_THEMES;

  // RGB strip toggle
  fb.setTextDatum(ML_DATUM); fb.setTextColor(C_MUTED);
  fb.drawString("RGB BAR", 24, 76);
  if (uiButton(132, 63, 120, 26, draftRgb ? "ON" : "OFF",
               draftRgb ? C_OK : C_MUTED))
    draftRgb = !draftRgb;

  // 2 × 3 grid of tile toggles
  for (int m = 0; m < M_COUNT; m++) {
    int cx = m % 2, cy = m / 2;
    int x = 24 + cx * 148, y = 96 + cy * 34, w = 136, h = 30;
    bool on = draft & (1 << m);
    fb.fillRoundRect(x, y, w, h, 6, on ? C_CARD : C_BG);
    fb.drawRoundRect(x, y, w, h, 6, on ? metricCol(m) : C_BORDER);
    fb.drawRect(x + 8, y + 7, 16, 16, C_MUTED);
    if (on) {
      fb.drawLine(x + 11, y + 15, x + 15, y + 19, metricCol(m));
      fb.drawLine(x + 15, y + 19, x + 21, y + 10, metricCol(m));
    }
    fb.setTextFont(2); fb.setTextDatum(ML_DATUM);
    fb.setTextColor(on ? metricCol(m) : C_MUTED);
    fb.drawString(metricName[m], x + 34, y + h / 2);
    if (tapIn(x, y, w, h)) draft ^= (1 << m);
  }

  if (uiButton(52, 204, 100, 28, "SAVE", C_OK)) {
    if (draft & 0x3F) tilesMask = draft;   // never save an empty dashboard
    themeIdx = draftTheme;
    rgbOn    = draftRgb;
    saveSettings();
    editing = false;
    g_screen = SCR_DASH;
    return;
  }
  if (uiButton(168, 204, 100, 28, "CANCEL", C_MUTED)) {
    themeIdx = savedTheme;
    editing = false;
    g_screen = SCR_DASH;
    return;
  }
  themeIdx = savedTheme;          // restore until saved (preview is per-frame)
}

} // namespace settings

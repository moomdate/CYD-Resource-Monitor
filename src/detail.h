#pragma once
#include "data.h"

namespace dpage {

void stats(int m, float &mn, float &mx, float &avg) {
  mn = 1e9; mx = 0; avg = 0;
  if (!histCnt) { mn = 0; return; }
  for (int i = 0; i < histCnt; i++) {
    float v = hist[m][i];
    mn = min(mn, v); mx = max(mx, v); avg += v;
  }
  avg /= histCnt;
}

void update() {
  int m = curMetric;
  uint16_t col = metricCol(m);

  // header
  if (uiButton(4, 3, 62, 22, "< BACK", C_MUTED)) { g_screen = SCR_DASH; return; }
  fb.setTextFont(4); fb.setTextSize(1); fb.setTextDatum(TC_DATUM);
  fb.setTextColor(col);
  fb.drawString(metricName[m], SCR_W / 2, 3);

  char t[40], val[16], sub[28], vunit[8];
  dash::valueText(m, val, sizeof(val), sub, sizeof(sub), vunit, sizeof(vunit));
  fb.setTextDatum(TR_DATUM); fb.setTextFont(4); fb.setTextColor(C_TEXT);
  snprintf(t, sizeof(t), "%s%s", val, vunit);
  fb.drawString(t, SCR_W - 6, 3);
  fb.drawFastHLine(0, 28, SCR_W, C_BORDER);

  // context line (name / totals)
  fb.setTextFont(2); fb.setTextDatum(TL_DATUM); fb.setTextColor(C_MUTED);
  switch (m) {
    case M_CPU:  snprintf(t, sizeof(t), "%d cores @ %.2f GHz", pc.cores, pc.cpuFreq / 1000); break;
    case M_GPU:
      if (pc.gpuCount) snprintf(t, sizeof(t), "%s%s  VRAM %.1f/%.0fG  (%d gpu)",
                                pc.gpuName, pc.gpuDiscrete ? " [discrete]" : "",
                                pc.vramUsed, pc.vramTotal, pc.gpuCount);
      else strlcpy(t, "no GPU detected", sizeof(t));
      break;
    case M_RAM:  snprintf(t, sizeof(t), "%.1f GB used of %.0f GB", pc.ramUsed, pc.ramTotal); break;
    case M_TEMP: snprintf(t, sizeof(t), "CPU %.0fC   GPU %.0fC", pc.cpuTemp, pc.gpuTemp); break;
    case M_DISK: snprintf(t, sizeof(t), "read %.1f MB/s   write %.1f MB/s", pc.diskR, pc.diskW); break;
    case M_NET:  snprintf(t, sizeof(t), "down %.2f MB/s   up %.2f MB/s", pc.netDl, pc.netUl); break;
  }
  fb.drawString(t, 8, 34);

  // graph area
  const int gx = 30, gy = 56, gw = SCR_W - gx - 10, gh = 130;
  float vmax = (m == M_NET) ? max(1.0f, histMax(m) * 1.15f) : 100.0f;
  fb.drawRect(gx, gy, gw, gh, C_BORDER);
  fb.setTextFont(1); fb.setTextColor(C_MUTED);
  for (int i = 1; i <= 3; i++) {                         // grid at 25/50/75%
    int y = gy + gh - i * gh / 4;
    for (int x = gx + 2; x < gx + gw - 2; x += 6) fb.drawPixel(x, y, th.grid);
    fb.setTextDatum(MR_DATUM);
    snprintf(t, sizeof(t), "%.0f", vmax * i / 4);
    fb.drawString(t, gx - 3, y);
  }
  fb.setTextDatum(MR_DATUM);
  snprintf(t, sizeof(t), "%.0f", vmax);
  fb.drawString(t, gx - 3, gy);
  sparkline(gx + 2, gy + 2, gw - 4, gh - 4, hist[m], histHead, histCnt, HIST_N, vmax, col);

  // stats footer
  float mn, mx, avg;
  stats(m, mn, mx, avg);
  fb.setTextFont(2); fb.setTextDatum(TL_DATUM);
  const char *unit = (m == M_NET) ? "MB/s" : (m == M_TEMP ? "C" : "%");
  fb.setTextColor(C_MUTED);  fb.drawString("MIN", 12, 200);
  fb.setTextColor(C_TEXT);   snprintf(t, sizeof(t), "%.1f%s", mn, unit);  fb.drawString(t, 12, 216);
  fb.setTextColor(C_MUTED);  fb.drawString("AVG", 96, 200);
  fb.setTextColor(C_TEXT);   snprintf(t, sizeof(t), "%.1f%s", avg, unit); fb.drawString(t, 96, 216);
  fb.setTextColor(C_MUTED);  fb.drawString("MAX", 180, 200);
  fb.setTextColor(C_TEXT);   snprintf(t, sizeof(t), "%.1f%s", mx, unit);  fb.drawString(t, 180, 216);
  fb.setTextColor(C_MUTED);  fb.drawString("last 60 s", 250, 208);
}

} // namespace dpage

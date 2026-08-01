#pragma once
#include "data.h"

int curMetric = M_CPU;   // metric shown on the detail screen

namespace dash {

// tiles currently enabled, in order
int visible[M_COUNT], nVis = 0;
void rebuildVisible() {
  nVis = 0;
  for (int m = 0; m < M_COUNT; m++)
    if (tilesMask & (1 << m)) visible[nVis++] = m;
}

void valueText(int m, char *val, size_t vn, char *sub, size_t sn,
               char *unit, size_t un) {
  unit[0] = 0;
  switch (m) {
    case M_CPU:
      snprintf(val, vn, "%d", (int)pc.cpu); strlcpy(unit, "%", un);
      if (pc.hasCpuTemp) snprintf(sub, sn, "%.0fC  %.1fGHz", pc.cpuTemp, pc.cpuFreq / 1000);
      else               snprintf(sub, sn, "%.1f GHz", pc.cpuFreq / 1000);
      break;
    case M_GPU:
      if (!pc.gpuCount)            { snprintf(val, vn, "--");  snprintf(sub, sn, "no GPU"); }
      else if (!pc.hasGpuLoad)     { snprintf(val, vn, "n/a"); strlcpy(sub, pc.gpuName, sn); }
      else {
        snprintf(val, vn, "%d", (int)pc.gpu); strlcpy(unit, "%", un);
        if (pc.hasGpuTemp) snprintf(sub, sn, "%.0fC %s", pc.gpuTemp, pc.gpuDiscrete ? "dGPU" : "iGPU");
        else               strlcpy(sub, pc.gpuName, sn);
      }
      break;
    case M_RAM:
      snprintf(val, vn, "%d", (int)pc.ram); strlcpy(unit, "%", un);
      snprintf(sub, sn, "%.1f/%.0f GB", pc.ramUsed, pc.ramTotal);
      break;
    case M_TEMP: {
      float t = max(pc.cpuTemp, pc.gpuTemp);
      if (!pc.hasCpuTemp && !pc.hasGpuTemp) { snprintf(val, vn, "n/a"); snprintf(sub, sn, "no sensor"); }
      else {
        snprintf(val, vn, "%.0f", t); strlcpy(unit, "C", un);
        snprintf(sub, sn, "C:%.0f G:%.0f", pc.cpuTemp, pc.gpuTemp);
      }
      break;
    }
    case M_DISK:
      snprintf(val, vn, "%d", (int)pc.disk); strlcpy(unit, "%", un);
      snprintf(sub, sn, "R%.0f W%.0f MB/s", pc.diskR, pc.diskW);
      break;
    case M_NET:
      snprintf(val, vn, "%.1f", pc.netDl); strlcpy(unit, "MB/s", un);
      snprintf(sub, sn, "DL MB/s  UL %.1f", pc.netUl);
      break;
  }
}

float gaugeFrac(int m) {
  if (m == M_NET) return pc.netDl / max(1.0f, histMax(M_NET));
  return metricValue(m) / 100.0f;
}

uint16_t gaugeCol(int m) {
  if (m == M_NET) return th.net;
  if (m == M_GPU && !pc.hasGpuLoad) return C_MUTED;
  return loadColor(metricValue(m));
}

void drawTile(int m, int x, int y, int w, int h) {
  fb.fillRoundRect(x, y, w, h, 8, C_CARD);
  fb.drawRoundRect(x, y, w, h, 8, metricCol(m));
  corners(x, y, w, h, th.accent);
  // hot tile: pulse a second border
  float v = metricValue(m);
  if (m != M_NET && v >= 85 && (millis() / 300) % 2)
    fb.drawRoundRect(x + 1, y + 1, w - 2, h - 2, 7, C_BAD);

  fb.setTextFont(2); fb.setTextSize(1); fb.setTextDatum(TL_DATUM);
  fb.setTextColor(metricCol(m));
  fb.drawString(metricName[m], x + 7, y + 5);

  char val[16], sub[28], unit[8];
  valueText(m, val, sizeof(val), sub, sizeof(sub), unit, sizeof(unit));

  int r  = constrain(min(w, h) / 2 - 24, 22, 44);
  int cx = x + w / 2, cy = y + h / 2 - 4;
  gauge(cx, cy, r, gaugeFrac(m), gaugeCol(m));

  // number sized to fit the donut, unit small & muted below it
  bool big = r >= 32;
  fb.setTextDatum(MC_DATUM);
  fb.setTextFont(big ? 4 : 2); fb.setTextColor(C_TEXT);
  fb.drawString(val, cx, cy - (unit[0] ? 3 : 0));
  if (unit[0]) {
    fb.setTextFont(1); fb.setTextColor(C_MUTED);
    fb.drawString(unit, cx, cy + (big ? 14 : 11));
  }

  fb.setTextFont(2); fb.setTextColor(C_MUTED);
  fb.drawString(sub, cx, y + h - 22);

  float vmax = (m == M_NET) ? max(1.0f, histMax(M_NET)) : 100.0f;
  sparkline(x + 8, y + h - 14, w - 16, 10, hist[m], histHead, histCnt, 40, vmax,
            metricCol(m), true);
}

void drawGear(int x, int y) {
  fb.fillCircle(x, y, 8, C_MUTED);
  for (int a = 0; a < 360; a += 60) {
    float r = a * DEG_TO_RAD;
    fb.fillCircle(x + (int)(cosf(r) * 9), y + (int)(sinf(r) * 9), 2, C_MUTED);
  }
  fb.fillCircle(x, y, 4, C_BG);
}

void drawHeader() {
  fb.fillRect(4, 5, 5, 14, th.accent);       // accent block
  fb.setTextFont(2); fb.setTextSize(1); fb.setTextDatum(TL_DATUM);
  fb.setTextColor(pc.valid ? th.accent : C_TEXT);
  char t[40];
  snprintf(t, sizeof(t), "%s", pc.valid ? pc.host : "RESOURCE MONITOR");
  fb.drawString(t, 14, 4);
  if (pc.valid) {
    fb.setTextColor(C_MUTED);
    snprintf(t, sizeof(t), "%s  %d core", pc.os, pc.cores);
    fb.drawString(t, 14 + fb.textWidth(pc.host) + 10, 4);
  }
  // link dot
  bool on = pc.valid && (millis() - pc.lastRx < 1200);
  fb.fillCircle(SCR_W - 44, 12, 4, pc.valid ? (on ? C_OK : C_WARN) : C_BAD);
  drawGear(SCR_W - 18, 12);
  if (rgbOn) rgbStrip(23, 2);                // animated RGB accent line
  else       fb.drawFastHLine(0, 24, SCR_W, C_BORDER);
}

void drawWaiting() {
  fb.fillRoundRect(40, 78, 240, 90, 10, C_BG);
  fb.drawRoundRect(40, 78, 240, 90, 10, C_WARN);
  fb.setTextDatum(MC_DATUM); fb.setTextFont(4); fb.setTextColor(C_WARN);
  fb.drawString("WAITING FOR PC", SCR_W / 2, 104);
  fb.setTextFont(2); fb.setTextColor(C_TEXT);
  fb.drawString("run the agent on your computer:", SCR_W / 2, 130);
  fb.setTextColor(th.accent);
  fb.drawString("python monitor_agent.py", SCR_W / 2, 150);
  if ((millis() / 400) % 2) fb.fillCircle(60, 104, 4, C_WARN);
}

void update() {
  bgGrid(26);
  drawHeader();
  rebuildVisible();

  int cols = nVis <= 3 ? nVis : (nVis == 4 ? 2 : 3);
  int rows = (nVis + cols - 1) / cols;
  const int gap = 4, top = 28;
  int tw = (SCR_W - gap * (cols + 1)) / cols;
  int tlh = (SCR_H - top - gap * rows) / rows;

  for (int i = 0; i < nVis; i++) {
    int cx = i % cols, cy = i / cols;
    int x = gap + cx * (tw + gap);
    int y = top + cy * (tlh + gap);
    drawTile(visible[i], x, y, tw, tlh);
    if (tapIn(x, y, tw, tlh) && pc.valid) {
      curMetric = visible[i];
      g_screen  = SCR_DETAIL;
    }
  }

  if (tapIn(SCR_W - 34, 0, 34, 24)) g_screen = SCR_SETTINGS;
  if (!pc.valid) drawWaiting();
}

} // namespace dash

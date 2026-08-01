#pragma once
#include <ArduinoJson.h>
#include "ui.h"

// ── Metric ids (also settings-tile order) ──────────
enum { M_CPU = 0, M_GPU, M_RAM, M_TEMP, M_DISK, M_NET, M_COUNT };
const char *metricName[M_COUNT] = {"CPU", "GPU", "RAM", "TEMP", "DISK", "NET"};
uint16_t metricCol(int m) {
  const uint16_t c[M_COUNT] = {th.cpu, th.gpu, th.ram, th.temp, th.disk, th.net};
  return c[m];
}

struct Metrics {
  // cpu
  float cpu = 0, cpuTemp = 0, cpuFreq = 0;
  int   cores = 0;
  // ram (GB)
  float ram = 0, ramUsed = 0, ramTotal = 0;
  // gpu (first = preferred; agent sorts discrete first)
  float gpu = 0, gpuTemp = 0, vramUsed = 0, vramTotal = 0;
  char  gpuName[26] = "";
  bool  gpuDiscrete = false;
  int   gpuCount = 0;
  // disk
  float disk = 0, diskR = 0, diskW = 0;
  // net (MB/s)
  float netDl = 0, netUl = 0;
  // host
  char  host[22] = "";
  char  os[8] = "";
  // availability
  bool  hasCpuTemp = false, hasGpuLoad = false, hasGpuTemp = false;
  bool  valid = false;
  uint32_t lastRx = 0;
};
Metrics pc;

// ── History rings (2 Hz × 120 = last 60 s) ─────────
#define HIST_N 120
float hist[M_COUNT][HIST_N] = {};
int   histHead = 0, histCnt = 0;

float metricValue(int m) {
  switch (m) {
    case M_CPU:  return pc.cpu;
    case M_GPU:  return pc.gpu;
    case M_RAM:  return pc.ram;
    case M_TEMP: return max(pc.cpuTemp, pc.gpuTemp);
    case M_DISK: return pc.disk;
    case M_NET:  return pc.netDl;
    default:     return 0;
  }
}

void pushHistory() {
  for (int m = 0; m < M_COUNT; m++) hist[m][histHead] = metricValue(m);
  histHead = (histHead + 1) % HIST_N;
  if (histCnt < HIST_N) histCnt++;
}

float histMax(int m) {
  float mx = 0;
  for (int i = 0; i < histCnt; i++) mx = max(mx, hist[m][i]);
  return mx;
}

// ── Serial JSON ingest ─────────────────────────────
void parseLine(char *line) {
  static JsonDocument doc;
  if (deserializeJson(doc, line)) return;
  if (!doc["cpu"].is<JsonObject>()) return;

  pc.cpu     = doc["cpu"]["load"]  | 0.0f;
  pc.cpuFreq = doc["cpu"]["freq"]  | 0.0f;
  pc.cores   = doc["cpu"]["cores"] | 0;
  pc.hasCpuTemp = doc["cpu"]["temp"].is<float>();
  pc.cpuTemp    = doc["cpu"]["temp"] | 0.0f;

  pc.ram      = doc["ram"]["pct"]   | 0.0f;
  pc.ramUsed  = doc["ram"]["used"]  | 0.0f;
  pc.ramTotal = doc["ram"]["total"] | 0.0f;

  JsonArray gpus = doc["gpus"];
  pc.gpuCount = gpus.size();
  if (pc.gpuCount > 0) {
    JsonObject g = gpus[0];
    strlcpy(pc.gpuName, g["name"] | "GPU", sizeof(pc.gpuName));
    pc.gpuDiscrete = g["discrete"] | false;
    pc.hasGpuLoad  = g["load"].is<float>();
    pc.gpu         = g["load"] | 0.0f;
    pc.hasGpuTemp  = g["temp"].is<float>();
    pc.gpuTemp     = g["temp"] | 0.0f;
    pc.vramUsed    = g["vram_used"]  | 0.0f;
    pc.vramTotal   = g["vram_total"] | 0.0f;
  } else {
    pc.hasGpuLoad = pc.hasGpuTemp = false;
    pc.gpu = pc.gpuTemp = 0;
  }

  pc.disk  = doc["disk"]["pct"] | 0.0f;
  pc.diskR = doc["disk"]["r"]   | 0.0f;
  pc.diskW = doc["disk"]["w"]   | 0.0f;

  pc.netDl = doc["net"]["dl"] | 0.0f;
  pc.netUl = doc["net"]["ul"] | 0.0f;

  strlcpy(pc.host, doc["host"]["name"] | "PC", sizeof(pc.host));
  strlcpy(pc.os,   doc["host"]["os"]   | "?",  sizeof(pc.os));

  pc.valid  = true;
  pc.lastRx = millis();
  pushHistory();
}

void serialPoll() {
  static char buf[1600];
  static int  len = 0;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      buf[len] = 0;
      if (len > 2) parseLine(buf);
      len = 0;
    } else if (len < (int)sizeof(buf) - 1) {
      buf[len++] = c;
    }
  }
  if (pc.valid && millis() - pc.lastRx > DATA_TIMEOUT_MS) pc.valid = false;
}

// ── Settings (which tiles on the dashboard) ────────
uint8_t tilesMask = 0x3F;   // all six on by default
bool    rgbOn = true;       // animated RGB strip under the header

void loadSettings() {
  prefs.begin("resmon", true);
  tilesMask = prefs.getUChar("tiles", 0x3F);
  themeIdx  = prefs.getUChar("theme", 0) % N_THEMES;
  rgbOn     = prefs.getBool("rgb", true);
  prefs.end();
  if ((tilesMask & 0x3F) == 0) tilesMask = 0x3F;
}
void saveSettings() {
  prefs.begin("resmon", false);
  prefs.putUChar("tiles", tilesMask);
  prefs.putUChar("theme", themeIdx);
  prefs.putBool("rgb", rgbOn);
  prefs.end();
}

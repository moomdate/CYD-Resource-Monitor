#include "data/telemetry.h"
#include <ArduinoJson.h>
#include <math.h>
#include <string.h>

// Read a finite number from v, clamped to [lo, hi]. False if v is missing / null / not a number.
static bool num(JsonVariantConst v, float &out, float lo, float hi) {
    if (!v.is<float>()) return false;
    float f = v.as<float>();
    if (!isfinite(f)) return false;
    out = f < lo ? lo : f > hi ? hi : f;
    return true;
}

// Like num(), but 0 or negative means "the agent has no reading" (freq, power, voltage).
static bool posNum(JsonVariantConst v, float &out, float hi) {
    float f;
    if (!num(v, f, -1e9f, hi) || f <= 0) return false;
    out = f;
    return true;
}

// Copy a non-empty string; truncates to cap-1 (strlcpy is not on every libc, so no strlcpy).
static bool text(JsonVariantConst v, char *out, size_t cap) {
    const char *s = v.is<const char *>() ? v.as<const char *>() : nullptr;
    if (!s || !s[0]) return false;
    size_t n = strlen(s);
    if (n >= cap) n = cap - 1;
    memcpy(out, s, n);
    out[n] = 0;
    return true;
}

bool telemetryParse(const char *line, Telemetry &out, uint32_t nowMs) {
    static JsonDocument doc;
    if (deserializeJson(doc, line) != DeserializationError::Ok) return false;

    JsonObjectConst cpu = doc["cpu"], ram = doc["ram"], net = doc["net"], disk = doc["disk"], host = doc["host"];
    if (cpu.isNull() && ram.isNull() && net.isNull() && disk.isNull()) return false;

    Telemetry t;   // fresh: anything the agent stopped sending disappears instead of going stale
    float f;

    // ---- cpu ----
    if (num(cpu["load"], f, 0, 100))        { t.cpuLoad = f;    t.set(F_CPU_LOAD); }
    if (num(cpu["temp"], f, -50, 150))      { t.cpuTemp = f;    t.set(F_CPU_TEMP); }
    if (posNum(cpu["freq"], f, 10000))      { t.cpuMHz = f;     t.set(F_CPU_FREQ); }     // 0 = unknown
    if (posNum(cpu["power"], f, 1000))      { t.cpuPowerW = f;  t.set(F_CPU_POWER); }
    if (posNum(cpu["volt"], f, 5))           { t.cpuVolt = f;    t.set(F_CPU_VOLT); }
    if (num(cpu["fan"], f, 0, 20000))       { t.cpuFanRpm = f;  t.set(F_CPU_FAN); }
    if (text(cpu["name"], t.cpuName, sizeof t.cpuName)) t.set(F_CPU_NAME);
    JsonArrayConst pc = cpu["per_core"];
    if (!pc.isNull() && pc.size() > 0) {
        for (JsonVariantConst c : pc) {
            if (t.coreN >= kMaxCores) break;
            float v = 0;
            num(c, v, 0, 100);                  // a bad entry shows as an empty bar, not a crash
            t.core[t.coreN++] = v;
        }
        t.set(F_CORES);
    }

    // ---- ram ----
    if (num(ram["used"], f, 0, 65536))  t.ramUsedGB = f;
    if (num(ram["total"], f, 0, 65536)) t.ramTotalGB = f;
    if (t.ramTotalGB > 0 && ram["used"].is<float>()) t.set(F_RAM_GB);
    if (num(ram["pct"], f, 0, 100))     { t.ramPct = f; t.set(F_RAM_PCT); }
    else if (t.has(F_RAM_GB))           { t.ramPct = t.ramUsedGB * 100.0f / t.ramTotalGB; t.set(F_RAM_PCT); }

    // ---- net ----
    if (num(net["dl"], f, 0, 100000)) { t.netRxMBs = f; t.set(F_NET_RX); }
    if (num(net["ul"], f, 0, 100000)) { t.netTxMBs = f; t.set(F_NET_TX); }

    // ---- disk: "act" is busy time; the old agent's "pct" was capacity used, so it is NOT activity ----
    if (num(disk["act"], f, 0, 100))     { t.diskAct = f;   t.set(F_DISK_ACT); }
    if (num(disk["r"], f, 0, 100000))    { t.diskRMBs = f;  t.set(F_DISK_R); }
    if (num(disk["w"], f, 0, 100000))    { t.diskWMBs = f;  t.set(F_DISK_W); }
    if (num(disk["temp"], f, -50, 150))  { t.diskTemp = f;  t.set(F_DISK_TEMP); }
    if (text(disk["model"], t.diskName, sizeof t.diskName)) t.set(F_DISK_NAME);

    // ---- gpu (first entry) ----
    JsonObjectConst g = doc["gpus"][0];
    if (!g.isNull()) {
        if (num(g["load"], f, 0, 100))   { t.gpuLoad = f; t.set(F_GPU_LOAD); }
        if (num(g["temp"], f, -50, 150)) { t.gpuTemp = f; t.set(F_GPU_TEMP); }
        if (text(g["name"], t.gpuName, sizeof t.gpuName)) t.set(F_GPU_NAME);
        if (num(g["vram_total"], f, 0, 4096) && f > 0) {
            t.vramTotalGB = f;
            if (num(g["vram_used"], f, 0, 4096)) { t.vramUsedGB = f; t.set(F_GPU_VRAM); }
        }
    }

    // ---- host ----
    if (text(host["name"], t.host, sizeof t.host)) t.set(F_HOST);
    text(host["os"], t.os, sizeof t.os);
    if (num(host["clock"], f, 0, 86399)) { t.clockSec = (int32_t)f; t.set(F_CLOCK); }

    t.rxAt = nowMs ? nowMs : 1;        // 0 is reserved for "never"
    out = t;
    return true;
}

bool LineReader::feed(char c) {
    if (c == '\n' || c == '\r') {
        bool ready = len_ >= 2 && !overflow_;
        if (overflow_) dropped_++;
        if (ready) buf_[len_] = 0;
        len_ = 0;
        overflow_ = false;
        return ready;
    }
    if (overflow_) return false;
    if (len_ < kCap - 1) buf_[len_++] = c;
    else overflow_ = true;
    return false;
}

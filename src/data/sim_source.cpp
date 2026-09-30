#include "data/sim_source.h"
#include <initializer_list>
#include <math.h>
#include <string.h>

namespace {

struct Preset { float cpu, temp, ramGB, rxMbps, disk; };
// Values from the presets in research-monitor/index.html
const Preset kPresets[3] = {
    {  8, 39,  7.2f,   2.4f,  4 },   // IDLE
    { 78, 74, 22.8f,  18.5f, 86 },   // GAMING
    { 42, 58, 15.1f, 176.4f, 98 },   // TRANSFER
};
const char *const kNames[SC_COUNT] = { "IDLE", "GAMING", "TRANSFER", "CYCLE" };

const uint32_t kCycleMs = 12000;         // CYCLE: time spent on each preset
const float    kMbpsPerMBs = 8.388608f;  // 1 MiB/s = 8.39 Mbit/s (the agent reports MiB/s)

float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

void copy(char *dst, size_t cap, const char *src) {
    size_t n = strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = 0;
}

}  // namespace

const char *SimSource::scenarioName(uint8_t sc) { return kNames[sc < SC_COUNT ? sc : 0]; }

void SimSource::begin(uint32_t nowMs) {
    t0_ = last_ = nowMs;
    init_ = false;
}

void SimSource::sample(uint32_t nowMs, uint8_t scenario, Telemetry &out) {
    float t = (nowMs - t0_) / 1000.0f;
    phase_ = scenario == SC_CYCLE ? (uint8_t)(((nowMs - t0_) / kCycleMs) % 3) : (scenario > SC_TRANSFER ? SC_IDLE : scenario);
    const Preset &p = kPresets[phase_];

    if (!init_) {                        // start on the preset, no ramp-up from zero
        cpu_ = p.cpu; temp_ = p.temp; ram_ = p.ramGB; rx_ = p.rxMbps; disk_ = p.disk;
        init_ = true;
    } else {
        float dt = clampf((nowMs - last_) / 1000.0f, 0.0f, 2.0f);
        float k = 1.0f - expf(-dt / 1.4f);            // exponential approach: presets blend, no jumps
        cpu_ += (p.cpu - cpu_) * k;
        temp_ += (p.temp - temp_) * (1.0f - expf(-dt / 4.0f));
        ram_ += (p.ramGB - ram_) * k;
        rx_ += (p.rxMbps - rx_) * k;
        disk_ += (p.disk - disk_) * k;
    }
    last_ = nowMs;

    Telemetry o;
    float cpu = clampf(cpu_ + 5 * sinf(t * 1.3f) + 3 * sinf(t * 3.1f), 0, 100);
    o.cpuLoad = cpu;
    o.cpuTemp = clampf(temp_ + 1.5f * sinf(t * 0.7f), 25, 100);
    o.cpuMHz = 1200 + cpu * 52;
    o.cpuPowerW = 12 + cpu * 0.9f;
    o.cpuVolt = 0.95f + cpu * 0.0022f;
    o.cpuFanRpm = 780 + cpu * 14;
    o.coreN = kMaxCores;
    for (int i = 0; i < kMaxCores; i++)
        o.core[i] = clampf(cpu + 22 * sinf(t * (0.9f + 0.37f * i) + i * 1.7f), 0, 100);
    copy(o.cpuName, sizeof o.cpuName, "AMD RYZEN 7 7800X3D");

    o.ramTotalGB = 32;
    o.ramUsedGB = clampf(ram_ + 0.3f * sinf(t * 0.4f), 1, 32);
    o.ramPct = o.ramUsedGB * 100.0f / o.ramTotalGB;

    float rxMbps = clampf(rx_ * (1.0f + 0.3f * sinf(t * 1.9f) * sinf(t * 0.6f)), 0, 990);
    o.netRxMBs = rxMbps / kMbpsPerMBs;
    o.netTxMBs = rxMbps * (0.152f + 0.05f * sinf(t * 2.3f)) / kMbpsPerMBs;

    float disk = clampf(disk_ + 4 * sinf(t * 1.1f), 0, 100);
    o.diskAct = disk;
    o.diskRMBs = 200 + disk * 22;
    o.diskWMBs = 50 + disk * 5.14f;
    o.diskTemp = 37 + disk * 0.17f;
    copy(o.diskName, sizeof o.diskName, "SAMSUNG 990 PRO 2TB");

    o.gpuLoad = clampf(cpu * 0.9f + 4 * sinf(t * 1.7f), 0, 100);
    o.gpuTemp = 38 + o.gpuLoad * 0.35f;
    o.vramTotalGB = 12;
    o.vramUsedGB = 1.5f + o.gpuLoad * 0.08f;
    copy(o.gpuName, sizeof o.gpuName, "DEMO GPU 12GB");

    // wall clock starts at the design's 14:27:36 and runs on
    o.clockSec = (int32_t)((14 * 3600 + 27 * 60 + 36 + (uint32_t)t) % 86400);
    copy(o.host, sizeof o.host, "NODE 01");
    copy(o.os, sizeof o.os, "demo");

    o.have = 0;
    for (int f : { F_CPU_LOAD, F_CPU_TEMP, F_CPU_FREQ, F_CPU_POWER, F_CPU_VOLT, F_CPU_FAN, F_CORES, F_CPU_NAME,
                   F_RAM_PCT, F_RAM_GB, F_NET_RX, F_NET_TX, F_DISK_ACT, F_DISK_R, F_DISK_W, F_DISK_TEMP,
                   F_DISK_NAME, F_GPU_LOAD, F_GPU_TEMP, F_GPU_NAME, F_GPU_VRAM, F_CLOCK, F_HOST })
        o.set((Field)f);
    o.rxAt = nowMs ? nowMs : 1;
    out = o;
}

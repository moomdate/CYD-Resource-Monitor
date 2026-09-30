#include "ui/monitor_model.h"
#include <ctype.h>
#include <string.h>

void History::push(float x) {
    v[head] = x;
    head = (head + 1) % kHist;
    if (count < kHist) count++;
    pushes++;
}

float History::at(int i) const {
    if (i < 0 || i >= count) return 0;
    return v[(head - count + i + 2 * kHist) % kHist];
}

float History::maxOf(int n) const {
    if (n > count) n = count;
    float m = 0;
    for (int i = count - n; i < count; i++) {
        float x = at(i);
        if (x > m) m = x;
    }
    return m;
}

void History::stats(float &mn, float &mx, float &avg) const {
    if (!count) { mn = mx = avg = 0; return; }
    mn = 1e30f; mx = -1e30f; avg = 0;
    for (int i = 0; i < count; i++) {
        float x = at(i);
        if (x < mn) mn = x;
        if (x > mx) mx = x;
        avg += x;
    }
    avg /= count;
}

const History &Histories::of(int m) const {
    switch (m) {
        case M_CPU:  return cpu;
        case M_TEMP: return temp;
        case M_RAM:  return ram;
        case M_GPU:  return gpu;
        case M_NET:  return rx;
        default:     return disk;
    }
}

static void upper(char *dst, size_t cap, const char *src) {
    size_t i = 0;
    for (; src[i] && i + 1 < cap; i++) dst[i] = (char)toupper((unsigned char)src[i]);
    dst[i] = 0;
}

void MonitorModel::reset() {
    hist_ = Histories();
    lastRx_ = lastPush_ = 0;
}

void MonitorModel::update(const Telemetry &t, bool demo, uint32_t now, MonitorView &v) {
    bool fresh = t.rxAt != 0 && (uint32_t)(now - t.rxAt) <= STALE_MS;
    v.link = !fresh ? Link::Offline : demo ? Link::Demo : Link::Live;
    v.blink = (now / 500) % 2 == 0;

    auto val = [&](Field f, float x) { Val r; r.v = x; r.ok = fresh && t.has(f); return r; };

    v.cpuLoad  = val(F_CPU_LOAD, t.cpuLoad);
    v.cpuTemp  = val(F_CPU_TEMP, t.cpuTemp);
    v.cpuGHz   = val(F_CPU_FREQ, t.cpuMHz / 1000.0f);
    v.cpuPower = val(F_CPU_POWER, t.cpuPowerW);
    v.cpuVolt  = val(F_CPU_VOLT, t.cpuVolt);
    v.cpuFan   = val(F_CPU_FAN, t.cpuFanRpm);
    v.coresOk  = fresh && t.has(F_CORES);
    v.coreN    = v.coresOk ? t.coreN : 0;
    memcpy(v.core, t.core, sizeof v.core);

    v.ramPct   = val(F_RAM_PCT, t.ramPct);
    v.ramUsed  = val(F_RAM_GB, t.ramUsedGB);
    v.ramTotal = val(F_RAM_GB, t.ramTotalGB);

    v.rxMbps = val(F_NET_RX, t.netRxMBs * MonitorModel::kMbpsPerMBs);
    v.txMbps = val(F_NET_TX, t.netTxMBs * MonitorModel::kMbpsPerMBs);

    v.diskAct  = val(F_DISK_ACT, t.diskAct);
    v.diskR    = val(F_DISK_R, t.diskRMBs);
    v.diskW    = val(F_DISK_W, t.diskWMBs);
    v.diskTemp = val(F_DISK_TEMP, t.diskTemp);

    v.gpuLoad   = val(F_GPU_LOAD, t.gpuLoad);
    v.gpuTemp   = val(F_GPU_TEMP, t.gpuTemp);
    v.vramUsed  = val(F_GPU_VRAM, t.vramUsedGB);
    v.vramTotal = val(F_GPU_VRAM, t.vramTotalGB);

    // names only while the link is up, so "--"/generic labels take over when it drops
    v.host[0] = v.cpuName[0] = v.diskName[0] = v.gpuName[0] = v.os[0] = 0;
    if (fresh) {
        if (t.has(F_HOST)) upper(v.host, sizeof v.host, t.host);
        if (t.has(F_CPU_NAME)) upper(v.cpuName, sizeof v.cpuName, t.cpuName);
        if (t.has(F_DISK_NAME)) upper(v.diskName, sizeof v.diskName, t.diskName);
        if (t.has(F_GPU_NAME)) upper(v.gpuName, sizeof v.gpuName, t.gpuName);
        upper(v.os, sizeof v.os, t.os);
    }

    // The host's clock only arrives with a message; tick it forward in between.
    v.clockOk = fresh && t.has(F_CLOCK);
    v.clockSec = v.clockOk ? (int)(((uint32_t)t.clockSec + (now - t.rxAt) / 1000) % 86400) : 0;

    // One history sample per accepted message, at most every SAMPLE_MS.
    if (fresh && t.rxAt != lastRx_) {
        lastRx_ = t.rxAt;
        if (!lastPush_ || (uint32_t)(t.rxAt - lastPush_) >= SAMPLE_MS - 100) {
            lastPush_ = t.rxAt;
            hist_.cpu.push(v.cpuLoad.ok ? v.cpuLoad.v : 0);
            hist_.temp.push(v.cpuTemp.ok ? v.cpuTemp.v : 0);
            hist_.ram.push(v.ramPct.ok ? v.ramPct.v : 0);
            hist_.gpu.push(v.gpuLoad.ok ? v.gpuLoad.v : 0);
            hist_.rx.push(v.rxMbps.ok ? v.rxMbps.v : 0);
            hist_.tx.push(v.txMbps.ok ? v.txMbps.v : 0);
            hist_.disk.push(v.diskAct.ok ? v.diskAct.v : 0);
        }
    }
    v.hist = &hist_;
}

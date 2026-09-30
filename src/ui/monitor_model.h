#pragma once
// Turns a Telemetry snapshot into what the screen shows: link state (LIVE / DEMO /
// OFFLINE), per-value validity with the staleness rule (older than STALE_MS -> "--"),
// display units (network in Mbps), and the 60 s history rings used by the graphs.
// Pure logic, no drawing - unit-tested on the host.
#include <stdint.h>
#include "config.h"
#include "data/telemetry.h"

enum class Link : uint8_t { Live, Demo, Offline };

// Metrics with a history graph (order = detail-page navigation order)
enum Metric : uint8_t { M_CPU, M_TEMP, M_RAM, M_GPU, M_NET, M_DISK, M_COUNT };

constexpr int kHist = 120;   // 2 Hz x 120 = last 60 s

struct History {
    float    v[kHist] = {};
    int      head = 0;        // next write slot
    int      count = 0;
    uint32_t pushes = 0;      // total samples ever pushed: the renderer's change key

    void  clear() { head = count = 0; }
    void  push(float x);
    float at(int i) const;                           // 0 = oldest of `count`
    float last() const { return count ? at(count - 1) : 0; }
    float maxOf(int n) const;                        // max over the newest n samples
    void  stats(float &mn, float &mx, float &avg) const;   // over everything held
};

struct Histories {
    History cpu, temp, ram, gpu, rx, tx, disk;   // rx/tx in Mbps, the rest in their own unit
    const History &of(int metric) const;         // M_NET -> rx
};

struct Val {
    float v = 0;
    bool  ok = false;    // present in the latest message AND that message is fresh
};

struct MonitorView {
    Link link = Link::Offline;
    bool blink = false;                // 1 Hz square wave for status dots

    Val cpuLoad, cpuTemp, cpuGHz, cpuPower, cpuVolt, cpuFan;
    uint8_t coreN = 0;
    float   core[kMaxCores] = {};
    bool    coresOk = false;
    Val ramPct, ramUsed, ramTotal;
    Val rxMbps, txMbps;
    Val diskAct, diskR, diskW, diskTemp;   // r/w in MB/s
    Val gpuLoad, gpuTemp, vramUsed, vramTotal;

    char host[20] = "", cpuName[28] = "", diskName[24] = "", gpuName[24] = "", os[8] = "";
    bool clockOk = false;
    int  clockSec = 0;                 // seconds since local midnight, advanced between messages

    const Histories *hist = nullptr;
};

class MonitorModel {
public:
    // demo: the snapshot came from the simulator (link reads DEMO instead of LIVE)
    void update(const Telemetry &t, bool demo, uint32_t nowMs, MonitorView &out);
    void reset();                                    // clear histories (source switched)
    const Histories &hist() const { return hist_; }

    static constexpr float kMbpsPerMBs = 8.388608f;  // agent reports MiB/s; 1 MiB/s = 8.39 Mbit/s

private:
    Histories hist_;
    uint32_t  lastRx_ = 0, lastPush_ = 0;
};

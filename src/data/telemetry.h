#pragma once
// One snapshot of the PC's state, as delivered by agent/monitor_agent.py (or the demo
// simulator). Every value carries a "present" bit: the agent only sends what it can read
// on the current OS, and old agents send less than new ones. Nothing here may assume a
// field exists - the UI shows "--" for anything whose bit is clear.
#include <stdint.h>

enum Field : uint8_t {
    F_CPU_LOAD, F_CPU_TEMP, F_CPU_FREQ, F_CPU_POWER, F_CPU_VOLT, F_CPU_FAN, F_CORES, F_CPU_NAME,
    F_RAM_PCT, F_RAM_GB,
    F_NET_RX, F_NET_TX,
    F_DISK_ACT, F_DISK_R, F_DISK_W, F_DISK_TEMP, F_DISK_NAME,
    F_GPU_LOAD, F_GPU_TEMP, F_GPU_NAME, F_GPU_VRAM,
    F_CLOCK, F_HOST,
};

constexpr int kMaxCores = 16;

struct Telemetry {
    uint32_t have = 0;          // bit per Field present in the LAST message
    uint32_t rxAt = 0;          // millis() when it arrived; 0 = nothing received yet

    // cpu
    float   cpuLoad = 0, cpuTemp = 0, cpuMHz = 0, cpuPowerW = 0, cpuVolt = 0, cpuFanRpm = 0;
    uint8_t coreN = 0;
    float   core[kMaxCores] = {};
    char    cpuName[28] = "";
    // ram (GB)
    float   ramPct = 0, ramUsedGB = 0, ramTotalGB = 0;
    // net (MB/s, as sent by the agent)
    float   netRxMBs = 0, netTxMBs = 0;
    // nvme / disk
    float   diskAct = 0, diskRMBs = 0, diskWMBs = 0, diskTemp = 0;
    char    diskName[24] = "";
    // gpu (first entry of the agent's list)
    float   gpuLoad = 0, gpuTemp = 0, vramUsedGB = 0, vramTotalGB = 0;
    char    gpuName[24] = "";
    // host
    int32_t clockSec = 0;       // local seconds since midnight when sent
    char    host[20] = "";
    char    os[8] = "";

    bool has(Field f) const { return (have >> f) & 1u; }
    void set(Field f) { have |= 1u << f; }
};

// Parse one JSON line from the agent. Returns false (and leaves `out` untouched) if it is
// not a monitor message. Missing / null / non-numeric fields simply stay "absent".
// `nowMs` becomes out.rxAt.
bool telemetryParse(const char *line, Telemetry &out, uint32_t nowMs);

// Accumulates serial bytes into lines. Lines longer than the buffer are dropped whole
// (never parsed as a truncated fragment). Feed it bytes; it calls back once per line.
class LineReader {
public:
    static const int kCap = 1536;
    // returns true when a complete line is ready in line()
    bool feed(char c);
    const char *line() const { return buf_; }
    int  droppedLines() const { return dropped_; }
private:
    char buf_[kCap];
    int  len_ = 0;
    bool overflow_ = false;
    int  dropped_ = 0;
};

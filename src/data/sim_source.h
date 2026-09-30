#pragma once
// Built-in demo source: the IDLE / GAMING / TRANSFER presets of the HTML simulator
// (research-monitor/index.html) with a little deterministic wobble so every widget moves,
// and a CYCLE mode that walks through the presets. Produces the same Telemetry the serial
// agent does, so the UI cannot tell the difference (the header says DEMO).
#include <stdint.h>
#include "data/telemetry.h"
#include "settings.h"

class SimSource {
public:
    void begin(uint32_t nowMs);
    // Advance the simulation to nowMs and write a complete snapshot (every field present).
    void sample(uint32_t nowMs, uint8_t scenario, Telemetry &out);

    static const char *scenarioName(uint8_t sc);    // "IDLE", "GAMING", ...
    uint8_t resolved() const { return phase_; }     // preset in effect (CYCLE resolves to one of the 3)

private:
    uint32_t t0_ = 0, last_ = 0;
    uint8_t  phase_ = SC_IDLE;
    bool     init_ = false;
    float    cpu_ = 0, temp_ = 0, ram_ = 0, rx_ = 0, disk_ = 0;   // smoothed toward the preset
};

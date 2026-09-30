#pragma once
// User settings, editable on the device's settings page and stored in NVS by main.cpp.
#include <stdint.h>

enum SourceId : uint8_t { SRC_LIVE, SRC_DEMO, SRC_COUNT };
enum Scenario : uint8_t { SC_IDLE, SC_GAMING, SC_TRANSFER, SC_CYCLE, SC_COUNT };

struct Settings {
    uint8_t theme      = 0;             // index into kThemes
    uint8_t source     = SRC_LIVE;      // PC agent over USB serial, or the built-in simulator
    uint8_t scenario   = SC_CYCLE;      // demo load profile
    uint8_t brightness = 100;           // backlight %, 20..100

    static const uint8_t kBrightMin = 20, kBrightStep = 20;
};

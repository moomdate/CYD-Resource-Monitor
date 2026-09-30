#pragma once
// Full-screen settings page: colour theme (live thumbnails of the real art), data source
// (PC agent / built-in demo), demo scenario, backlight brightness.
// Opened by tapping the header or the SETUP pill on the main screen.
#include "settings.h"

namespace settings_ui {

enum Action : uint8_t {
    ACT_NONE,
    ACT_CHANGED,     // a setting changed - apply it (and persist)
    ACT_CLOSE,       // DONE pressed - back to the dashboard
};

extern const char *const kSourceLabels[SRC_COUNT];
extern const char *const kScenarioLabels[SC_COUNT];

// agentLive: shows whether the PC agent is currently sending (informational line).
void   draw(const Settings &s, bool agentLive);   // full repaint (open / after a change)
Action tap(int x, int y, Settings &s);            // handle a tap; edits s (caller repaints)

}  // namespace settings_ui

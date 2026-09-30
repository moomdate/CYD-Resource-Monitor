#pragma once
#define FW_VERSION "2.0.0"     // keep in sync with the git tag (v2.0.0) and agent __version__

// ── CYD (ESP32-2432S028R compatible) ───────────────
#define SCR_W 320
#define SCR_H 240

// XPT2046 touch (bit-banged)
#define TOUCH_DOUT 39
#define TOUCH_DIN  32
#define TOUCH_DCS  33
#define TOUCH_DCLK 25

#define SERIAL_BAUD 115200

// ── Timing ─────────────────────────────────────────
#define UI_FPS          30     // frame pacing; regions only repaint when their content changed
#define STALE_MS        2500   // no message for this long -> values show "--" and the link is OFFLINE
#define SAMPLE_MS       500    // history / graph cadence, and the demo source's update rate
#define TAP_MAX_MS      600    // a press longer than this is not a tap

// ---- Backlight ---------------------------------------------------------------------------
// This CYD's backlight goes completely dark with ANY PWM duty below 100 % (verified on the
// board: 20 % and 43 % both blank the screen, full-on works). So by default the backlight is
// driven plain HIGH and the BRIGHTNESS setting is hidden. Set 1 only on a board whose
// backlight really dims with PWM.
#define BACKLIGHT_DIMMING 0

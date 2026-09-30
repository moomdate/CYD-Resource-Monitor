#pragma once

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

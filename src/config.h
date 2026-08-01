#pragma once

// ── CYD (ESP32-2432S028R compatible) ───────────────
#define SCR_W 320
#define SCR_H 240

// XPT2046 touch (bit-banged)
#define TOUCH_DOUT 39
#define TOUCH_DIN  32
#define TOUCH_DCS  33
#define TOUCH_DCLK 25

// Data timeout before showing "waiting" screen
#define DATA_TIMEOUT_MS 4000

#pragma once
// Colour themes from research-monitor/index.html: ICE / LIME, VIOLET, AMBER. Each has its
// own pre-rendered background (tools/gen_chrome.py) plus the three accent colours the live
// widgets draw with. Neutral colours are shared. Add a theme = one entry in THEMES in
// gen_chrome.py, one row in theme.cpp.
#include <stdint.h>

struct Theme {
    const char     *name;          // shown in settings, <= 10 chars
    const char     *key;           // stable id ("ice"...)
    const uint16_t *background;    // 320x240 RGB565 in flash
    uint32_t accent;               // --a     : CPU ring, RX line, buttons
    uint32_t accentBright;         // --a2    : panel titles, selected text
    uint32_t lime;                 // --lime  : temperatures, RAM bar, TX line, "online"
};

enum { THEME_ICE, THEME_VIOLET, THEME_AMBER, THEME_COUNT };
extern const Theme kThemes[THEME_COUNT];

// shared palette (index.html :root)
#define COL_BG      0x060A0D
#define COL_TRACK   0x1A2C33   // ring / bar tracks
#define COL_SEG_OFF 0x172B31   // unlit bar segments
#define COL_LINE    0x25414B
#define COL_DIM     0x80939C
#define COL_WHITE   0xEAFFFF
#define COL_VALUE   0xEDFAFF
#define COL_WARN    0xFFAC37
#define COL_RED     0xFF4659
#define COL_FOOT    0x62767E

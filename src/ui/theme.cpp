#include "ui/theme.h"
#include "assets/bg_ice.h"
#include "assets/bg_violet.h"
#include "assets/bg_amber.h"

// Colours from the HTML's theme() table; keep in sync with THEMES in tools/gen_chrome.py.
//                                          accent    bright    lime
const Theme kThemes[THEME_COUNT] = {
    { "ICE / LIME", "ice",    bg_ice,    0x21DDF3, 0x73F5FF, 0xA8F437 },
    { "VIOLET",     "violet", bg_violet, 0xBB79FF, 0xDFBAFF, 0x56E0CC },
    { "AMBER",      "amber",  bg_amber,  0xFFAB37, 0xFFD078, 0xB3F250 },
};

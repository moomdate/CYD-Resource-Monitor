#include "ui/settings_ui.h"
#include "ui/canvas.h"
#include "ui/monitor_ui.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "fonts/font_ui.h"
#include "fonts/font_sm.h"
#include "fonts/font_md.h"
#include <stdio.h>

namespace settings_ui {

const char *const kSourceLabels[SRC_COUNT] = { "PC AGENT", "DEMO" };
const char *const kScenarioLabels[SC_COUNT] = { "IDLE", "GAMING", "TRANSFER", "CYCLE" };

// ---- layout ----------------------------------------------------------------------
static const Rect R_DONE     = { 250, 4, 64, 22 };
static Rect themeCard(int i) { return { 8 + i * 104, 46, 96, 72 }; }
static Rect sourceBtn(int i) { return { 8 + i * 82, 152, 78, 22 }; }
static Rect scenarioBtn(int i) { return { 8 + i * 74, 192, 70, 20 }; }
static const Rect R_BRI_DN   = { 110, 216, 26, 20 };
static const Rect R_BRI_V    = { 138, 216, 60, 20 };
static const Rect R_BRI_UP   = { 200, 216, 26, 20 };

#define COL_MUTED 0x8796A0

static inline uint16_t C(uint32_t rgb) { return rgb565(rgb); }

static void themeThumb(Canvas &cv, const Theme &cur, int i, bool selected) {
    Rect r = themeCard(i);
    const uint16_t *bg = kThemes[i].background;
    int y0 = r.y > cv.y0 ? r.y : cv.y0, y1 = r.y + r.h < cv.y0 + cv.h ? r.y + r.h : cv.y0 + cv.h;
    for (int y = y0; y < y1; y++) {
        int sy = (y - r.y) * 240 / r.h;
        for (int x = r.x; x < r.x + r.w; x++) {
            uint16_t c = bg[sy * 320 + (x - r.x) * 320 / r.w];
            cv.pixel(x, y, selected ? c : blend565(120, 0, c));
        }
    }
    uint16_t edge = selected ? C(cur.accent) : C(0x34434E);
    for (int k = 1; k <= (selected ? 2 : 1); k++) {
        cv.fillRect(r.x - k, r.y - k, r.w + 2 * k, 1, edge);
        cv.fillRect(r.x - k, r.y + r.h + k - 1, r.w + 2 * k, 1, edge);
        cv.fillRect(r.x - k, r.y - k, 1, r.h + 2 * k, edge);
        cv.fillRect(r.x + r.w + k - 1, r.y - k, 1, r.h + 2 * k, edge);
    }
    // theme name + its accent swatch
    int nameW = cv.textWidth(font_ui, kThemes[i].name, 1);
    int nx = r.x + (r.w - nameW) / 2 + 5;
    cv.fillRect(nx - 10, 127, 6, 6, C(kThemes[i].accent));
    cv.text(font_ui, nx, 134, kThemes[i].name, C(selected ? kThemes[i].accentBright : COL_MUTED), ALIGN_LEFT, 1);
}

static void compose(Canvas &cv, const Settings &s, bool agentLive) {
    const Theme &t = kThemes[s.theme];

    cv.text(font_ui, 12, 20, "SETTINGS", C(t.accentBright), ALIGN_LEFT, 2);
    cv.text(font_sm, 100, 19, "SYSTEM RESEARCH / NODE 01", C(COL_MUTED));
    widgets::button(cv, t, R_DONE, "DONE", true, font_ui);
    widgets::fadeRule(cv, t, 8, 30, 304);

    cv.text(font_sm, 12, 41, "THEME", C(COL_MUTED), ALIGN_LEFT, 1);
    for (int i = 0; i < THEME_COUNT; i++) themeThumb(cv, t, i, i == s.theme);

    cv.text(font_sm, 12, 148, "DATA SOURCE", C(COL_MUTED), ALIGN_LEFT, 1);
    for (int i = 0; i < SRC_COUNT; i++) widgets::button(cv, t, sourceBtn(i), kSourceLabels[i], i == s.source, font_ui);
    cv.text(font_sm, 178, 166, agentLive ? "AGENT: CONNECTED" : "AGENT: NOT DETECTED",
            C(agentLive ? t.lime : COL_MUTED));

    cv.text(font_sm, 12, 188, "DEMO SCENARIO", C(COL_MUTED), ALIGN_LEFT, 1);
    for (int i = 0; i < SC_COUNT; i++)
        widgets::button(cv, t, scenarioBtn(i), kScenarioLabels[i], i == s.scenario, font_ui);

    cv.text(font_sm, 12, 230, "BRIGHTNESS", C(COL_MUTED), ALIGN_LEFT, 1);
    widgets::button(cv, t, R_BRI_DN, "-", false, font_ui);
    char buf[8];
    snprintf(buf, sizeof buf, "%d%%", s.brightness);
    widgets::button(cv, t, R_BRI_V, buf, false, font_ui);
    widgets::button(cv, t, R_BRI_UP, "+", false, font_ui);
}

void draw(const Settings &s, bool agentLive) {
    Canvas &cv = monitor_ui::canvas();
    PushFn push = monitor_ui::pushFn();
    // 40-row strips: 320*40 fits the shared buffer; every element clips itself
    for (int y = 0; y < 240; y += 40) {
        cv.beginFlat(0, y, 320, 40, C(COL_BG));
        compose(cv, s, agentLive);
        push(0, y, 320, 40, cv.px);
    }
}

Action tap(int x, int y, Settings &s) {
    using widgets::inside;
    if (inside(R_DONE, x, y)) return ACT_CLOSE;
    Action a = ACT_NONE;
    for (int i = 0; i < THEME_COUNT; i++) {
        Rect r = themeCard(i);
        r.h += 18;                                     // the name row is tappable too
        if (inside(r, x, y) && s.theme != i) { s.theme = i; a = ACT_CHANGED; }
    }
    for (int i = 0; i < SRC_COUNT; i++)
        if (inside(sourceBtn(i), x, y) && s.source != i) { s.source = i; a = ACT_CHANGED; }
    for (int i = 0; i < SC_COUNT; i++)
        if (inside(scenarioBtn(i), x, y) && s.scenario != i) { s.scenario = i; a = ACT_CHANGED; }
    if (inside(R_BRI_DN, x, y) && s.brightness > Settings::kBrightMin) {
        s.brightness = s.brightness - Settings::kBrightStep < Settings::kBrightMin ? Settings::kBrightMin
                                                                                   : s.brightness - Settings::kBrightStep;
        a = ACT_CHANGED;
    }
    if (inside(R_BRI_UP, x, y) && s.brightness < 100) {
        s.brightness = s.brightness + Settings::kBrightStep > 100 ? 100 : s.brightness + Settings::kBrightStep;
        a = ACT_CHANGED;
    }
    return a;
}

}  // namespace settings_ui

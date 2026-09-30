// Host unit tests - run with:  pio test -e native
// Everything here is the real firmware code (parser, model, renderer, settings page,
// simulator) compiled for the host; only the display and NVS are absent.
#include <unity.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "settings.h"
#include "data/sim_source.h"
#include "data/telemetry.h"
#include "assets/layout.h"
#include "ui/canvas.h"
#include "ui/detail_ui.h"
#include "ui/monitor_model.h"
#include "ui/monitor_ui.h"
#include "ui/settings_ui.h"
#include "ui/theme.h"
#include "fonts/font_big.h"
#include "fonts/font_temp.h"
#include "fonts/font_val.h"
#include "fonts/font_ui.h"
#include "fonts/font_md.h"
#include "fonts/font_sm.h"

// ---- fake display ------------------------------------------------------------------------
static uint16_t fb[320 * 240];
static int pushes = 0;
static long pushedPx = 0;
struct PushRec { int x, y, w, h; };
static PushRec rec[64];
static bool outOfBounds = false;

static void push(int x, int y, int w, int h, const uint16_t *px) {
    if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > 320 || y + h > 240) { outOfBounds = true; return; }
    for (int r = 0; r < h; r++) memcpy(&fb[(y + r) * 320 + x], px + r * w, w * 2);
    if (pushes < 64) rec[pushes] = { x, y, w, h };
    pushes++;
    pushedPx += (long)w * h;
}
static void resetPushCount() { pushes = 0; pushedPx = 0; }

void setUp() { outOfBounds = false; }
void tearDown() {}

// ---- helpers -------------------------------------------------------------------------------
static Telemetry parseOk(const char *json, uint32_t now = 1000) {
    Telemetry t;
    TEST_ASSERT_TRUE_MESSAGE(telemetryParse(json, t, now), json);
    return t;
}

static const char *kNewAgent =
    "{\"cpu\":{\"load\":68.0,\"freq\":4720,\"cores\":16,\"temp\":64.0,\"name\":\"AMD Ryzen 7 7800X3D\","
    "\"power\":82.4,\"volt\":1.081,\"fan\":1236,\"per_core\":[72,61,69,63,71,58,66,59]},"
    "\"ram\":{\"pct\":38.8,\"used\":12.4,\"total\":32.0},"
    "\"gpus\":[{\"name\":\"RTX 4070\",\"load\":54.0,\"temp\":61.0,\"vram_used\":6.3,\"vram_total\":12.0}],"
    "\"disk\":{\"pct\":54.0,\"act\":72.0,\"r\":1843.0,\"w\":420.0,\"temp\":49.0,\"model\":\"Samsung 990 PRO\"},"
    "\"net\":{\"dl\":10.03,\"ul\":1.52},\"host\":{\"name\":\"research-pc\",\"os\":\"win\",\"clock\":52056}}";

// exactly what the previous agent version sent
static const char *kOldAgent =
    "{\"cpu\":{\"load\":37.5,\"freq\":3200.0,\"cores\":8,\"temp\":58.0},"
    "\"ram\":{\"pct\":61.2,\"used\":9.8,\"total\":16.0},"
    "\"gpus\":[{\"name\":\"NVIDIA GeForce RTX 3060\",\"load\":22.0,\"temp\":49.0,\"vram_used\":2.1,\"vram_total\":12.0,\"discrete\":true}],"
    "\"disk\":{\"pct\":54.0,\"r\":12.5,\"w\":3.4},\"net\":{\"dl\":1.85,\"ul\":0.22},"
    "\"host\":{\"name\":\"WORKSTATION\",\"os\":\"win\"}}";

// ---- canvas / colour ---------------------------------------------------------------------------
static void test_rgb565_and_blend() {
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, rgb565(0xFFFFFF));
    TEST_ASSERT_EQUAL_HEX16(0xF800, rgb565(0xFF0000));
    TEST_ASSERT_EQUAL_HEX16(0x07E0, rgb565(0x00FF00));
    TEST_ASSERT_EQUAL_HEX16(0x001F, rgb565(0x0000FF));
    TEST_ASSERT_EQUAL_HEX16(0x1234, blend565(0, 0xFFFF, 0x1234));
    TEST_ASSERT_EQUAL_HEX16(0xABCD, blend565(255, 0xABCD, 0x0000));
    uint16_t mid = blend565(128, 0xFFFF, 0x0000);
    TEST_ASSERT_INT_WITHIN(2, 15, mid >> 11);           // ~half red
}

static void test_canvas_clips_and_rejects_oversize() {
    static uint16_t buf[100];
    static uint16_t bg[320 * 240];
    Canvas cv(buf, 100);
    TEST_ASSERT_FALSE(cv.begin(0, 0, 20, 20, bg));      // 400 px > capacity
    TEST_ASSERT_TRUE(cv.begin(10, 10, 10, 10, bg));
    cv.fillRect(0, 0, 320, 240, 0xFFFF);                // way outside: must clip, not overflow
    for (int i = 0; i < 100; i++) TEST_ASSERT_EQUAL_HEX16(0xFFFF, buf[i]);
    cv.pixel(-5, 500, 0x0000);                          // out of range: ignored
    cv.line(-50, -50, 500, 500, 0x1234);                // line clipped too
    cv.text(font_md, -20, 5, "CLIPPED", 0x0000);
}

static void test_tabular_digits_have_equal_width() {
    static uint16_t buf[16];
    Canvas cv(buf, 16);
    int w = cv.textWidth(font_big, "0000", 0, true);
    const char *samples[] = { "1111", "4444", "7777", "1234", "9876" };
    for (const char *s : samples) TEST_ASSERT_EQUAL_INT(w, cv.textWidth(font_big, s, 0, true));
}

// Every string the UI draws must exist in the font it is drawn with.
static bool fontHas(const UiFont &f, const char *s) {
    while (*s) {
        uint8_t c = (uint8_t)*s;
        uint16_t code = c;
        if (c >= 0xC0) { code = (uint16_t)(((c & 0x1F) << 6) | ((uint8_t)s[1] & 0x3F)); s++; }
        s++;
        bool found = false;
        for (int i = 0; i < f.count; i++) if (f.glyphs[i].code == code) found = true;
        if (!found) { printf("missing glyph U+%04X\n", code); return false; }
    }
    return true;
}

static void test_fonts_cover_every_ui_string() {
    TEST_ASSERT_TRUE(fontHas(font_big, "0123456789-."));
    TEST_ASSERT_TRUE(fontHas(font_temp, "0123456789-°C"));
    TEST_ASSERT_TRUE(fontHas(font_val, "0123456789.%°-/"));
    TEST_ASSERT_TRUE(fontHas(font_md, "0123456789:. /-%ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz°"));
    TEST_ASSERT_TRUE(fontHas(font_sm, "0123456789:. /-%ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_>°"));
    TEST_ASSERT_TRUE(fontHas(font_ui, "SETTINGS DONE + - 100% < BACK > 0123456789"));
    for (int t = 0; t < THEME_COUNT; t++) TEST_ASSERT_TRUE(fontHas(font_ui, kThemes[t].name));
    for (int i = 0; i < SRC_COUNT; i++) TEST_ASSERT_TRUE(fontHas(font_ui, settings_ui::kSourceLabels[i]));
    for (int i = 0; i < SC_COUNT; i++) TEST_ASSERT_TRUE(fontHas(font_ui, settings_ui::kScenarioLabels[i]));
    TEST_ASSERT_TRUE(fontHas(font_ui, "OFFLINE"));
    TEST_ASSERT_TRUE(fontHas(font_ui, "CPU LOAD CPU TEMP MEMORY GPU LOAD NETWORK NVME ACTIVITY"));
}

static void test_font_tables_sorted() {
    const UiFont *fonts[] = { &font_big, &font_temp, &font_val, &font_ui, &font_md, &font_sm };
    for (const UiFont *f : fonts)
        for (int i = 1; i < f->count; i++) TEST_ASSERT_TRUE(f->glyphs[i - 1].code < f->glyphs[i].code);
}

// ---- telemetry parsing ---------------------------------------------------------------------------
static void test_parse_new_agent_full() {
    Telemetry t = parseOk(kNewAgent, 5000);
    TEST_ASSERT_EQUAL_UINT32(5000, t.rxAt);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 68.0f, t.cpuLoad);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 4720.0f, t.cpuMHz);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 82.4f, t.cpuPowerW);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.081f, t.cpuVolt);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1236.0f, t.cpuFanRpm);
    TEST_ASSERT_EQUAL_STRING("AMD Ryzen 7 7800X3D", t.cpuName);
    TEST_ASSERT_EQUAL_INT(8, t.coreN);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 71.0f, t.core[4]);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 72.0f, t.diskAct);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 49.0f, t.diskTemp);
    TEST_ASSERT_EQUAL_STRING("Samsung 990 PRO", t.diskName);
    TEST_ASSERT_EQUAL_INT32(52056, t.clockSec);
    TEST_ASSERT_EQUAL_STRING("research-pc", t.host);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 6.3f, t.vramUsedGB);
    uint32_t all[] = { F_CPU_LOAD, F_CPU_TEMP, F_CPU_FREQ, F_CPU_POWER, F_CPU_VOLT, F_CPU_FAN, F_CORES, F_CPU_NAME,
                       F_RAM_PCT, F_RAM_GB, F_NET_RX, F_NET_TX, F_DISK_ACT, F_DISK_R, F_DISK_W, F_DISK_TEMP,
                       F_DISK_NAME, F_GPU_LOAD, F_GPU_TEMP, F_GPU_NAME, F_GPU_VRAM, F_CLOCK, F_HOST };
    for (uint32_t f : all) TEST_ASSERT_TRUE_MESSAGE(t.has((Field)f), "field bit");
}

// The previous agent version must still work: what it never sent must read as absent.
static void test_parse_old_agent_missing_fields() {
    Telemetry t = parseOk(kOldAgent);
    TEST_ASSERT_TRUE(t.has(F_CPU_LOAD));
    TEST_ASSERT_TRUE(t.has(F_CPU_TEMP));
    TEST_ASSERT_TRUE(t.has(F_CPU_FREQ));
    TEST_ASSERT_TRUE(t.has(F_RAM_PCT));
    TEST_ASSERT_TRUE(t.has(F_NET_RX));
    TEST_ASSERT_TRUE(t.has(F_DISK_R));
    TEST_ASSERT_TRUE(t.has(F_GPU_LOAD));
    TEST_ASSERT_FALSE(t.has(F_CPU_POWER));
    TEST_ASSERT_FALSE(t.has(F_CPU_VOLT));
    TEST_ASSERT_FALSE(t.has(F_CPU_FAN));
    TEST_ASSERT_FALSE(t.has(F_CORES));
    TEST_ASSERT_FALSE(t.has(F_CPU_NAME));
    TEST_ASSERT_FALSE(t.has(F_DISK_ACT));     // the old "pct" was capacity, not activity
    TEST_ASSERT_FALSE(t.has(F_DISK_TEMP));
    TEST_ASSERT_FALSE(t.has(F_DISK_NAME));
    TEST_ASSERT_FALSE(t.has(F_CLOCK));
    TEST_ASSERT_EQUAL_INT(0, t.coreN);
}

static void test_parse_partial_null_and_wrong_types() {
    Telemetry t = parseOk("{\"cpu\":{\"load\":null,\"temp\":\"hot\",\"freq\":0,\"power\":-5,\"fan\":0},"
                          "\"gpus\":[],\"disk\":{\"act\":null,\"r\":1.5},\"host\":{\"name\":\"\"}}");
    TEST_ASSERT_FALSE(t.has(F_CPU_LOAD));     // null
    TEST_ASSERT_FALSE(t.has(F_CPU_TEMP));     // string
    TEST_ASSERT_FALSE(t.has(F_CPU_FREQ));     // 0 MHz = "unknown"
    TEST_ASSERT_FALSE(t.has(F_CPU_POWER));    // negative
    TEST_ASSERT_TRUE(t.has(F_CPU_FAN));       // a stopped fan is a real reading
    TEST_ASSERT_FALSE(t.has(F_DISK_ACT));
    TEST_ASSERT_TRUE(t.has(F_DISK_R));
    TEST_ASSERT_FALSE(t.has(F_GPU_LOAD));     // empty gpu list
    TEST_ASSERT_FALSE(t.has(F_HOST));         // empty string
}

static void test_parse_clamps_and_limits() {
    Telemetry t = parseOk("{\"cpu\":{\"load\":250,\"temp\":9999,\"per_core\":[1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20]},"
                          "\"ram\":{\"used\":8,\"total\":16}}");
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, t.cpuLoad);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 150.0f, t.cpuTemp);
    TEST_ASSERT_EQUAL_INT(kMaxCores, t.coreN);                 // extra cores dropped, no overflow
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 50.0f, t.ramPct);          // derived from used / total when pct is absent
    Telemetry b = parseOk("{\"cpu\":{\"per_core\":[10,\"x\",null,55]}}");
    TEST_ASSERT_EQUAL_INT(4, b.coreN);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, b.core[1]);          // bad entry -> empty bar
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 55.0f, b.core[3]);
    // over-long strings are truncated, not overflowed
    Telemetry n = parseOk("{\"cpu\":{\"name\":\"THIS IS A VERY LONG PROCESSOR MARKETING NAME THAT NEVER FITS\"}}");
    TEST_ASSERT_EQUAL_INT((int)sizeof(n.cpuName) - 1, (int)strlen(n.cpuName));
}

static void test_parse_rejects_garbage_and_keeps_state() {
    Telemetry keep = parseOk(kNewAgent, 111);
    Telemetry t = keep;
    TEST_ASSERT_FALSE(telemetryParse("", t, 5));
    TEST_ASSERT_FALSE(telemetryParse("hello world", t, 5));
    TEST_ASSERT_FALSE(telemetryParse("{\"cpu\":{\"load\":1", t, 5));      // truncated
    TEST_ASSERT_FALSE(telemetryParse("{\"foo\":1}", t, 5));               // JSON, but not ours
    TEST_ASSERT_FALSE(telemetryParse("[1,2,3]", t, 5));
    TEST_ASSERT_FALSE(telemetryParse("{\"cpu\":5}", t, 5));               // cpu is not an object
    TEST_ASSERT_EQUAL_UINT32(111, t.rxAt);                                 // failed parses leave the state alone
    TEST_ASSERT_EQUAL_UINT16(keep.have & 0xFFFF, t.have & 0xFFFF);
}

static void test_parse_replaces_previous_fields() {
    Telemetry t = parseOk(kNewAgent, 100);
    TEST_ASSERT_TRUE(t.has(F_CPU_POWER));
    TEST_ASSERT_TRUE(telemetryParse(kOldAgent, t, 200));       // next message lacks power: it must not linger
    TEST_ASSERT_FALSE(t.has(F_CPU_POWER));
    TEST_ASSERT_EQUAL_UINT32(200, t.rxAt);
}

static void test_line_reader() {
    LineReader lr;
    const char *msg = "{\"cpu\":{\"load\":5}}\r\n{\"ram\":{\"pct\":9}}\n\n";
    int lines = 0;
    char first[64] = "", second[64] = "";
    for (const char *p = msg; *p; p++)
        if (lr.feed(*p)) {
            if (lines == 0) strncpy(first, lr.line(), 63);
            else strncpy(second, lr.line(), 63);
            lines++;
        }
    TEST_ASSERT_EQUAL_INT(2, lines);                            // CRLF handled, empty lines ignored
    TEST_ASSERT_EQUAL_STRING("{\"cpu\":{\"load\":5}}", first);
    TEST_ASSERT_EQUAL_STRING("{\"ram\":{\"pct\":9}}", second);

    // a line longer than the buffer is dropped whole, and the next line is fine
    LineReader big;
    int got = 0;
    for (int i = 0; i < LineReader::kCap + 500; i++) big.feed('x');
    TEST_ASSERT_FALSE(big.feed('\n'));
    TEST_ASSERT_EQUAL_INT(1, big.droppedLines());
    for (const char *p = "{\"cpu\":{\"load\":7}}\n"; *p; p++) if (big.feed(*p)) got++;
    TEST_ASSERT_EQUAL_INT(1, got);
}

// ---- model / staleness ---------------------------------------------------------------------------
static void test_model_live_fresh_and_stale() {
    MonitorModel m;
    MonitorView v;
    Telemetry t = parseOk(kNewAgent, 10000);
    m.update(t, false, 10100, v);
    TEST_ASSERT_TRUE(v.link == Link::Live);
    TEST_ASSERT_TRUE(v.cpuLoad.ok);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 68.0f, v.cpuLoad.v);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 4.72f, v.cpuGHz.v);
    TEST_ASSERT_TRUE(v.coresOk);
    TEST_ASSERT_EQUAL_STRING("RESEARCH-PC", v.host);          // shown upper-case

    m.update(t, false, 10000 + STALE_MS, v);                   // exactly at the limit: still fresh
    TEST_ASSERT_TRUE(v.link == Link::Live);
    TEST_ASSERT_TRUE(v.cpuLoad.ok);

    m.update(t, false, 10000 + STALE_MS + 1, v);               // one ms later: everything reads "--"
    TEST_ASSERT_TRUE(v.link == Link::Offline);
    TEST_ASSERT_FALSE(v.cpuLoad.ok);
    TEST_ASSERT_FALSE(v.cpuTemp.ok);
    TEST_ASSERT_FALSE(v.ramPct.ok);
    TEST_ASSERT_FALSE(v.rxMbps.ok);
    TEST_ASSERT_FALSE(v.diskAct.ok);
    TEST_ASSERT_FALSE(v.coresOk);
    TEST_ASSERT_FALSE(v.clockOk);
    TEST_ASSERT_EQUAL_STRING("", v.host);
}

static void test_model_never_received_is_offline() {
    MonitorModel m;
    MonitorView v;
    Telemetry none;
    m.update(none, false, 123456, v);
    TEST_ASSERT_TRUE(v.link == Link::Offline);
    TEST_ASSERT_FALSE(v.cpuLoad.ok);
    m.update(none, false, 0, v);                                // and at t=0 too
    TEST_ASSERT_TRUE(v.link == Link::Offline);
}

static void test_model_missing_fields_stay_missing_while_fresh() {
    MonitorModel m;
    MonitorView v;
    Telemetry t = parseOk(kOldAgent, 1000);
    m.update(t, false, 1200, v);
    TEST_ASSERT_TRUE(v.link == Link::Live);
    TEST_ASSERT_TRUE(v.cpuLoad.ok);
    TEST_ASSERT_FALSE(v.cpuPower.ok);
    TEST_ASSERT_FALSE(v.cpuVolt.ok);
    TEST_ASSERT_FALSE(v.cpuFan.ok);
    TEST_ASSERT_FALSE(v.coresOk);
    TEST_ASSERT_FALSE(v.diskAct.ok);
    TEST_ASSERT_FALSE(v.diskTemp.ok);
    TEST_ASSERT_FALSE(v.clockOk);
}

static void test_model_units_and_demo_flag() {
    MonitorModel m;
    MonitorView v;
    Telemetry t = parseOk(kNewAgent, 100);
    m.update(t, true, 150, v);
    TEST_ASSERT_TRUE(v.link == Link::Demo);
    TEST_ASSERT_FLOAT_WITHIN(0.2f, 84.1f, v.rxMbps.v);          // 10.03 MiB/s -> 84.1 Mbit/s
    TEST_ASSERT_FLOAT_WITHIN(0.2f, 12.75f, v.txMbps.v);
}

static void test_model_clock_ticks_between_messages() {
    MonitorModel m;
    MonitorView v;
    Telemetry t = parseOk(kNewAgent, 1000);                     // host clock 14:27:36
    m.update(t, false, 1000, v);
    TEST_ASSERT_TRUE(v.clockOk);
    TEST_ASSERT_EQUAL_INT(52056, v.clockSec);
    m.update(t, false, 3100, v);                                // 2.1 s later, still fresh
    TEST_ASSERT_EQUAL_INT(52058, v.clockSec);
    Telemetry w = parseOk("{\"cpu\":{\"load\":1},\"host\":{\"clock\":86399}}", 1000);
    m.update(w, false, 2500, v);
    TEST_ASSERT_EQUAL_INT(0, v.clockSec);                       // wraps at midnight
}

static void test_model_history_cadence_and_reset() {
    MonitorModel m;
    MonitorView v;
    Telemetry t = parseOk(kNewAgent, 0);
    uint32_t now = 1000;
    for (int i = 0; i < 20; i++) {                              // agent at 10 Hz: history still 2 Hz
        t.rxAt = now;
        m.update(t, false, now, v);
        now += 100;
    }
    TEST_ASSERT_TRUE(m.hist().cpu.count >= 3 && m.hist().cpu.count <= 5);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 68.0f, m.hist().cpu.last());
    m.reset();
    TEST_ASSERT_EQUAL_INT(0, m.hist().cpu.count);
    for (int i = 0; i < kHist + 30; i++) { t.rxAt = now; m.update(t, false, now, v); now += 500; }
    TEST_ASSERT_EQUAL_INT(kHist, m.hist().cpu.count);           // ring wraps, never grows
    float mn, mx, avg;
    m.hist().cpu.stats(mn, mx, avg);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 68.0f, avg);
}

static void test_history_ring() {
    History h;
    for (int i = 0; i < kHist + 10; i++) h.push((float)i);
    TEST_ASSERT_EQUAL_INT(kHist, h.count);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, h.at(0));                            // oldest survivor
    TEST_ASSERT_FLOAT_WITHIN(0.01f, (float)(kHist + 9), h.at(h.count - 1));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, (float)(kHist + 9), h.maxOf(5));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, h.at(kHist + 3));                     // out of range -> 0
}

// ---- simulator ------------------------------------------------------------------------------------
static void test_sim_fills_every_field_in_range() {
    SimSource sim;
    sim.begin(1000);
    Telemetry t;
    for (uint8_t sc = 0; sc < SC_COUNT; sc++)
        for (uint32_t ms = 1000; ms < 60000; ms += 500) {
            sim.sample(ms, sc, t);
            TEST_ASSERT_EQUAL_UINT32(23, __builtin_popcount(t.have));
            TEST_ASSERT_TRUE(t.cpuLoad >= 0 && t.cpuLoad <= 100);
            TEST_ASSERT_TRUE(t.cpuTemp >= 25 && t.cpuTemp <= 100);
            TEST_ASSERT_TRUE(t.ramPct >= 0 && t.ramPct <= 100);
            TEST_ASSERT_TRUE(t.diskAct >= 0 && t.diskAct <= 100);
            TEST_ASSERT_TRUE(t.netRxMBs >= 0);
            TEST_ASSERT_EQUAL_INT(kMaxCores, t.coreN);
            for (int i = 0; i < t.coreN; i++) TEST_ASSERT_TRUE(t.core[i] >= 0 && t.core[i] <= 100);
            TEST_ASSERT_TRUE(t.clockSec >= 0 && t.clockSec < 86400);
        }
}

static void test_sim_scenarios_differ_and_cycle_moves() {
    SimSource a, b, c;
    Telemetry ta, tb, tc;
    a.begin(0); b.begin(0); c.begin(0);
    for (uint32_t ms = 500; ms <= 30000; ms += 500) {
        a.sample(ms, SC_IDLE, ta);
        b.sample(ms, SC_GAMING, tb);
        c.sample(ms, SC_TRANSFER, tc);
    }
    TEST_ASSERT_TRUE(ta.cpuLoad < 25);
    TEST_ASSERT_TRUE(tb.cpuLoad > 60);
    TEST_ASSERT_TRUE(tc.netRxMBs * MonitorModel::kMbpsPerMBs > 120);                  // TRANSFER = fat network
    TEST_ASSERT_TRUE(tc.diskAct > 85);
    SimSource cy;
    cy.begin(0);
    Telemetry t;
    cy.sample(500, SC_CYCLE, t);
    TEST_ASSERT_EQUAL_UINT8(SC_IDLE, cy.resolved());
    cy.sample(13000, SC_CYCLE, t);
    TEST_ASSERT_EQUAL_UINT8(SC_GAMING, cy.resolved());
    cy.sample(25000, SC_CYCLE, t);
    TEST_ASSERT_EQUAL_UINT8(SC_TRANSFER, cy.resolved());
}

static void test_sim_is_deterministic() {
    SimSource a, b;
    Telemetry ta, tb;
    a.begin(0); b.begin(0);
    for (uint32_t ms = 500; ms <= 20000; ms += 500) { a.sample(ms, SC_GAMING, ta); b.sample(ms, SC_GAMING, tb); }
    TEST_ASSERT_EQUAL_FLOAT(ta.cpuLoad, tb.cpuLoad);
    TEST_ASSERT_EQUAL_FLOAT(ta.netRxMBs, tb.netRxMBs);
}

// ---- layout / theme assets ---------------------------------------------------------------------------
static bool inside(const Rect &in, const Rect &out) {
    return in.x >= out.x && in.y >= out.y && in.x + in.w <= out.x + out.w && in.y + in.h <= out.y + out.h;
}

static void test_layout_regions_are_inside_their_panels_and_the_screen() {
    const Rect screen = { 0, 0, 320, 240 };
    const Rect all[] = { L_HEADER, L_CPU, L_RAM, L_NET, L_DISK, L_NODE, L_STATUS, L_CLOCK, L_CPU_NAME, L_CPU_RING,
                         L_CPU_TEMP, L_CPU_ROWS, L_CPU_CORES, L_RAM_LIVE, L_NET_RATES, L_NET_GRAPH, L_DISK_NAME,
                         L_DISK_RING, L_DISK_IO, L_FOOT_L, L_FOOT_R, L_SETUP, L_BANNER };
    for (const Rect &r : all) {
        TEST_ASSERT_TRUE(r.w > 0 && r.h > 0);
        TEST_ASSERT_TRUE(inside(r, screen));
    }
    TEST_ASSERT_TRUE(inside(L_NODE, L_HEADER));
    TEST_ASSERT_TRUE(inside(L_STATUS, L_HEADER));
    TEST_ASSERT_TRUE(inside(L_CLOCK, L_HEADER));
    TEST_ASSERT_TRUE(inside(L_CPU_NAME, L_CPU));
    TEST_ASSERT_TRUE(inside(L_CPU_RING, L_CPU));
    TEST_ASSERT_TRUE(inside(L_CPU_TEMP, L_CPU));
    TEST_ASSERT_TRUE(inside(L_CPU_ROWS, L_CPU));
    TEST_ASSERT_TRUE(inside(L_CPU_CORES, L_CPU));
    TEST_ASSERT_TRUE(inside(L_RAM_LIVE, L_RAM));
    TEST_ASSERT_TRUE(inside(L_NET_RATES, L_NET));
    TEST_ASSERT_TRUE(inside(L_NET_GRAPH, L_NET));
    TEST_ASSERT_TRUE(inside(L_DISK_NAME, L_DISK));
    TEST_ASSERT_TRUE(inside(L_DISK_RING, L_DISK));
    TEST_ASSERT_TRUE(inside(L_DISK_IO, L_DISK));
    // the largest region must fit the scratch buffer (14400 px) and the settings/detail strips
    TEST_ASSERT_TRUE(L_BANNER.w * L_BANNER.h <= 14400);
    TEST_ASSERT_TRUE(320 * 40 <= 14400);
}

static void test_theme_backgrounds_match_layout() {
    for (int t = 0; t < THEME_COUNT; t++) {
        const uint16_t *bg = kThemes[t].background;
        // CPU ring centre is the dark disc colour, its track area is not yet drawn (live), the mark is the accent
        uint16_t centre = bg[P_CPU_RING_CY * 320 + P_CPU_RING_CX];
        TEST_ASSERT_EQUAL_HEX16(rgb565(0x081015), centre);
        uint16_t mark = bg[20 * 320 + 19];
        TEST_ASSERT_EQUAL_HEX16_MESSAGE(rgb565(kThemes[t].accent), mark, kThemes[t].name);
        TEST_ASSERT_EQUAL_HEX16(rgb565(COL_BG), bg[3 * 320 + 3]);                // margin outside every panel
        // panel frame is drawn: top edge of the RAM card
        TEST_ASSERT_TRUE(bg[L_RAM.y * 320 + L_RAM.x + 40] != rgb565(COL_BG));
    }
    TEST_ASSERT_TRUE(kThemes[0].background[20 * 320 + 19] != kThemes[1].background[20 * 320 + 19]);
    TEST_ASSERT_TRUE(kThemes[1].background[20 * 320 + 19] != kThemes[2].background[20 * 320 + 19]);
}

// ---- renderer: only changed regions are pushed -----------------------------------------------------------
static MonitorView liveView(MonitorModel &m, const char *json, uint32_t now) {
    static Telemetry t;
    MonitorView v;
    TEST_ASSERT_TRUE(telemetryParse(json, t, now));
    m.update(t, false, now, v);
    return v;
}

static void test_renderer_steady_state_pushes_nothing() {
    monitor_ui::begin(push, kThemes[THEME_ICE]);
    MonitorModel m;
    MonitorView v = liveView(m, kNewAgent, 1000);
    monitor_ui::render(v, nullptr);
    TEST_ASSERT_TRUE(monitor_ui::lastPushedRegions() >= 12);     // first frame paints every live region
    resetPushCount();
    monitor_ui::render(v, nullptr);
    TEST_ASSERT_EQUAL_INT(0, monitor_ui::lastPushedRegions());   // identical frame: zero SPI traffic
    TEST_ASSERT_EQUAL_INT(0, pushes);
    TEST_ASSERT_FALSE(outOfBounds);
}

static void test_renderer_pushes_only_what_changed() {
    monitor_ui::begin(push, kThemes[THEME_ICE]);
    MonitorModel m;
    MonitorView v = liveView(m, kNewAgent, 1000);
    monitor_ui::render(v, nullptr);

    // 1) only the clock moved -> exactly one region, and it lies inside the header
    v.clockSec += 1;
    resetPushCount();
    monitor_ui::render(v, nullptr);
    TEST_ASSERT_EQUAL_INT(1, pushes);
    TEST_ASSERT_TRUE(inside({ rec[0].x, rec[0].y, rec[0].w, rec[0].h }, L_HEADER));

    // 2) CPU load changed -> the ring only
    v.cpuLoad.v = 91;
    resetPushCount();
    monitor_ui::render(v, nullptr);
    TEST_ASSERT_EQUAL_INT(1, pushes);
    TEST_ASSERT_TRUE(inside({ rec[0].x, rec[0].y, rec[0].w, rec[0].h }, L_CPU));

    // 3) the same load again, sub-percent change: still nothing (keys use the displayed value)
    v.cpuLoad.v = 91.2f;
    resetPushCount();
    monitor_ui::render(v, nullptr);
    TEST_ASSERT_EQUAL_INT(0, pushes);

    // 4) one core changes -> the core strip only
    v.core[3] = 5;
    resetPushCount();
    monitor_ui::render(v, nullptr);
    TEST_ASSERT_EQUAL_INT(1, pushes);

    // 5) NVMe activity -> its ring only
    v.diskAct.v = 12;
    resetPushCount();
    monitor_ui::render(v, nullptr);
    TEST_ASSERT_EQUAL_INT(1, pushes);
    TEST_ASSERT_TRUE(inside({ rec[0].x, rec[0].y, rec[0].w, rec[0].h }, L_DISK));
    TEST_ASSERT_FALSE(outOfBounds);
}

static void test_renderer_new_network_sample_repaints_graph_only() {
    monitor_ui::begin(push, kThemes[THEME_ICE]);
    MonitorModel m;
    static Telemetry t;
    MonitorView v;
    TEST_ASSERT_TRUE(telemetryParse(kNewAgent, t, 1000));
    m.update(t, false, 1000, v);
    for (int i = 1; i <= 5; i++) { t.rxAt = 1000 + i * 500; m.update(t, false, t.rxAt, v); }
    monitor_ui::render(v, nullptr);
    t.rxAt += 500;                                      // one more identical sample: history advances
    m.update(t, false, t.rxAt, v);
    resetPushCount();
    monitor_ui::render(v, nullptr);
    TEST_ASSERT_EQUAL_INT(1, pushes);                   // the scrolling graph, nothing else
    TEST_ASSERT_TRUE(inside({ rec[0].x, rec[0].y, rec[0].w, rec[0].h }, L_NET));
}

static void test_renderer_stays_inside_regions() {
    monitor_ui::begin(push, kThemes[THEME_AMBER]);
    uint16_t before[320 * 240];
    memcpy(before, fb, sizeof fb);
    MonitorModel m;
    MonitorView v = liveView(m, kNewAgent, 1000);
    monitor_ui::render(v, nullptr);
    // pixels in the outer margin (no region touches them) are unchanged from the pure background
    for (int x = 0; x < 320; x++) { TEST_ASSERT_EQUAL_HEX16(before[x], fb[x]); TEST_ASSERT_EQUAL_HEX16(before[3 * 320 + x], fb[3 * 320 + x]); }
    for (int y = 0; y < 240; y++) TEST_ASSERT_EQUAL_HEX16(before[y * 320 + 1], fb[y * 320 + 1]);
    TEST_ASSERT_FALSE(outOfBounds);
}

static void test_renderer_offline_banner_lifecycle() {
    monitor_ui::begin(push, kThemes[THEME_ICE]);
    MonitorModel m;
    MonitorView v = liveView(m, kNewAgent, 1000);
    monitor_ui::render(v, nullptr);
    TEST_ASSERT_TRUE(fb[(L_BANNER.y + 20) * 320 + L_BANNER.x + 1] != rgb565(COL_RED));   // no banner while live

    static Telemetry stale;
    telemetryParse(kNewAgent, stale, 1000);
    m.update(stale, false, 1000 + STALE_MS + 500, v);             // agent went quiet
    TEST_ASSERT_TRUE(v.link == Link::Offline);
    monitor_ui::render(v, nullptr);
    // banner drawn: its red left bar is on screen
    TEST_ASSERT_EQUAL_HEX16(rgb565(COL_RED), fb[(L_BANNER.y + 20) * 320 + L_BANNER.x + 1]);

    // steady offline: only the blinking status dot repaints, the banner is not re-pushed each frame
    resetPushCount();
    monitor_ui::render(v, nullptr);
    TEST_ASSERT_TRUE(pushes <= 1);

    // agent is back: banner disappears, live values are repainted
    m.update(stale, false, 1000 + STALE_MS + 600, v);             // still stale...
    Telemetry back;
    telemetryParse(kNewAgent, back, 9000);
    m.update(back, false, 9100, v);
    TEST_ASSERT_TRUE(v.link == Link::Live);
    monitor_ui::render(v, nullptr);
    TEST_ASSERT_TRUE(fb[(L_BANNER.y + 20) * 320 + L_BANNER.x + 1] != rgb565(COL_RED));
    TEST_ASSERT_FALSE(outOfBounds);
}

static void test_renderer_theme_switch_repaints_everything() {
    monitor_ui::begin(push, kThemes[THEME_ICE]);
    MonitorModel m;
    MonitorView v = liveView(m, kNewAgent, 1000);
    monitor_ui::render(v, nullptr);
    resetPushCount();
    monitor_ui::setTheme(kThemes[THEME_VIOLET]);
    TEST_ASSERT_TRUE(pushedPx >= 320 * 240);                       // full-screen background
    monitor_ui::render(v, nullptr);
    TEST_ASSERT_TRUE(monitor_ui::lastPushedRegions() >= 12);       // every live region again, in the new colours
    TEST_ASSERT_EQUAL_HEX16(rgb565(kThemes[THEME_VIOLET].accent), fb[20 * 320 + 19]);
}

static void test_renderer_handles_extreme_values() {
    monitor_ui::begin(push, kThemes[THEME_ICE]);
    MonitorView v;
    MonitorModel m;
    Telemetry t = parseOk("{\"cpu\":{\"load\":100,\"temp\":150,\"freq\":10000,\"power\":999,\"volt\":4.9,\"fan\":20000,"
                          "\"name\":\"WWWWWWWWWWWWWWWWWWWWWWWWWWWW\",\"per_core\":[100,100,100,100,100,100,100,100,100,100,100,100,100,100,100,100]},"
                          "\"ram\":{\"pct\":100,\"used\":65536,\"total\":65536},\"net\":{\"dl\":99999,\"ul\":99999},"
                          "\"disk\":{\"act\":100,\"r\":99999,\"w\":99999,\"temp\":150,\"model\":\"XXXXXXXXXXXXXXXXXXXXXXX\"},"
                          "\"host\":{\"name\":\"AVERYLONGHOSTNAMEOF19\",\"clock\":86399}}", 1000);
    m.update(t, false, 1100, v);
    monitor_ui::render(v, nullptr);
    Telemetry z = parseOk("{\"cpu\":{\"load\":0,\"temp\":-50,\"per_core\":[0,0]},\"ram\":{\"pct\":0,\"used\":0,\"total\":1},"
                          "\"net\":{\"dl\":0,\"ul\":0},\"disk\":{\"act\":0,\"r\":0,\"w\":0}}", 2000);
    m.update(z, false, 2100, v);
    monitor_ui::render(v, nullptr);
    TEST_ASSERT_FALSE(outOfBounds);                                // nothing drew or pushed outside the screen
}

static void test_touch_zones() {
    using namespace monitor_ui;
    TEST_ASSERT_EQUAL_INT(Z_SETUP, hit(160, 10));                  // header
    TEST_ASSERT_EQUAL_INT(Z_SETUP, hit(290, 232));                 // SETUP pill
    TEST_ASSERT_EQUAL_INT(Z_CPU, hit(50, 100));
    TEST_ASSERT_EQUAL_INT(Z_RAM, hit(230, 60));
    TEST_ASSERT_EQUAL_INT(Z_NET, hit(230, 120));
    TEST_ASSERT_EQUAL_INT(Z_DISK, hit(150, 190));
    TEST_ASSERT_EQUAL_INT(Z_NONE, hit(4, 120));                    // margin
    TEST_ASSERT_EQUAL_INT(Z_NONE, hit(148, 60));                   // gap between the columns
}

// ---- settings page ------------------------------------------------------------------------------------------
static void test_settings_defaults() {
    Settings s;
    TEST_ASSERT_EQUAL_UINT8(0, s.theme);
    TEST_ASSERT_EQUAL_UINT8(SRC_LIVE, s.source);
    TEST_ASSERT_EQUAL_UINT8(100, s.brightness);
}

static void test_settings_draw_covers_whole_screen() {
    monitor_ui::begin(push, kThemes[THEME_ICE]);
    Settings s;
    resetPushCount();
    settings_ui::draw(s, true);
    TEST_ASSERT_EQUAL_INT(6, pushes);                              // six 40-row strips
    TEST_ASSERT_EQUAL_INT(320 * 240, (int)pushedPx);
    TEST_ASSERT_FALSE(outOfBounds);
}

static void test_settings_taps() {
    Settings s;
    // theme cards: centre of cards 1 and 2
    TEST_ASSERT_EQUAL_INT(settings_ui::ACT_CHANGED, settings_ui::tap(8 + 104 + 48, 80, s));
    TEST_ASSERT_EQUAL_UINT8(THEME_VIOLET, s.theme);
    TEST_ASSERT_EQUAL_INT(settings_ui::ACT_CHANGED, settings_ui::tap(8 + 208 + 48, 80, s));
    TEST_ASSERT_EQUAL_UINT8(THEME_AMBER, s.theme);
    TEST_ASSERT_EQUAL_INT(settings_ui::ACT_NONE, settings_ui::tap(8 + 208 + 48, 80, s));   // already selected
    // the name row under a card is tappable too
    TEST_ASSERT_EQUAL_INT(settings_ui::ACT_CHANGED, settings_ui::tap(8 + 48, 130, s));
    TEST_ASSERT_EQUAL_UINT8(THEME_ICE, s.theme);

    // data source: DEMO (second button), then back to the agent
    TEST_ASSERT_EQUAL_INT(settings_ui::ACT_CHANGED, settings_ui::tap(8 + 82 + 39, 163, s));
    TEST_ASSERT_EQUAL_UINT8(SRC_DEMO, s.source);
    TEST_ASSERT_EQUAL_INT(settings_ui::ACT_CHANGED, settings_ui::tap(8 + 39, 163, s));
    TEST_ASSERT_EQUAL_UINT8(SRC_LIVE, s.source);

    // scenario buttons
    s.scenario = SC_CYCLE;
    for (int i = 0; i < SC_COUNT; i++) {
        TEST_ASSERT_EQUAL_INT(settings_ui::ACT_CHANGED, settings_ui::tap(8 + i * 74 + 35, 202, s));
        TEST_ASSERT_EQUAL_UINT8(i, s.scenario);
        TEST_ASSERT_EQUAL_INT(settings_ui::ACT_NONE, settings_ui::tap(8 + i * 74 + 35, 202, s));   // same again: no change
    }

    // brightness: steps of 20, clamped to 20..100
    s.brightness = 100;
    TEST_ASSERT_EQUAL_INT(settings_ui::ACT_NONE, settings_ui::tap(213, 226, s));           // '+' at max
    for (int i = 0; i < 6; i++) settings_ui::tap(123, 226, s);                              // '-' many times
    TEST_ASSERT_EQUAL_UINT8(Settings::kBrightMin, s.brightness);
    TEST_ASSERT_EQUAL_INT(settings_ui::ACT_CHANGED, settings_ui::tap(213, 226, s));
    TEST_ASSERT_EQUAL_UINT8(Settings::kBrightMin + Settings::kBrightStep, s.brightness);
    s.brightness = 90;                                                                       // odd value from old NVS
    settings_ui::tap(213, 226, s);
    TEST_ASSERT_EQUAL_UINT8(100, s.brightness);

    // DONE, and a tap on empty space
    TEST_ASSERT_EQUAL_INT(settings_ui::ACT_CLOSE, settings_ui::tap(282, 15, s));
    Settings before = s;
    TEST_ASSERT_EQUAL_INT(settings_ui::ACT_NONE, settings_ui::tap(150, 182, s));
    TEST_ASSERT_EQUAL_UINT8(before.theme, s.theme);
    TEST_ASSERT_EQUAL_UINT8(before.source, s.source);
}

// ---- detail page --------------------------------------------------------------------------------------------------
static void test_detail_page() {
    using namespace detail_ui;
    TEST_ASSERT_EQUAL_INT(M_CPU, metricForZone(monitor_ui::Z_CPU));
    TEST_ASSERT_EQUAL_INT(M_RAM, metricForZone(monitor_ui::Z_RAM));
    TEST_ASSERT_EQUAL_INT(M_NET, metricForZone(monitor_ui::Z_NET));
    TEST_ASSERT_EQUAL_INT(M_DISK, metricForZone(monitor_ui::Z_DISK));

    int m = M_CPU;
    TEST_ASSERT_EQUAL_INT(ACT_BACK, tap(30, 15, m));
    TEST_ASSERT_EQUAL_INT(ACT_METRIC, tap(290, 15, m));
    TEST_ASSERT_EQUAL_INT(M_TEMP, m);
    m = M_CPU;
    TEST_ASSERT_EQUAL_INT(ACT_METRIC, tap(250, 15, m));            // previous wraps to the last metric
    TEST_ASSERT_EQUAL_INT(M_DISK, m);
    TEST_ASSERT_EQUAL_INT(ACT_NONE, tap(150, 120, m));

    // draws a full page for every metric, with and without data; key changes with new samples
    monitor_ui::begin(push, kThemes[THEME_ICE]);
    MonitorModel model;
    MonitorView v = liveView(model, kNewAgent, 1000);
    for (int k = 0; k < M_COUNT; k++) {
        resetPushCount();
        draw(v, k);
        TEST_ASSERT_EQUAL_INT(320 * 240, (int)pushedPx);
    }
    uint32_t k1 = contentKey(v, M_CPU);
    static Telemetry t;
    telemetryParse(kNewAgent, t, 1500);
    model.update(t, false, 1500, v);
    TEST_ASSERT_TRUE(contentKey(v, M_CPU) != k1);
    Telemetry none;
    MonitorView off;
    MonitorModel m2;
    m2.update(none, false, 100, off);
    for (int k = 0; k < M_COUNT; k++) draw(off, k);                // no data: must not crash, shows "--"
    TEST_ASSERT_FALSE(outOfBounds);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_rgb565_and_blend);
    RUN_TEST(test_canvas_clips_and_rejects_oversize);
    RUN_TEST(test_tabular_digits_have_equal_width);
    RUN_TEST(test_fonts_cover_every_ui_string);
    RUN_TEST(test_font_tables_sorted);
    RUN_TEST(test_parse_new_agent_full);
    RUN_TEST(test_parse_old_agent_missing_fields);
    RUN_TEST(test_parse_partial_null_and_wrong_types);
    RUN_TEST(test_parse_clamps_and_limits);
    RUN_TEST(test_parse_rejects_garbage_and_keeps_state);
    RUN_TEST(test_parse_replaces_previous_fields);
    RUN_TEST(test_line_reader);
    RUN_TEST(test_model_live_fresh_and_stale);
    RUN_TEST(test_model_never_received_is_offline);
    RUN_TEST(test_model_missing_fields_stay_missing_while_fresh);
    RUN_TEST(test_model_units_and_demo_flag);
    RUN_TEST(test_model_clock_ticks_between_messages);
    RUN_TEST(test_model_history_cadence_and_reset);
    RUN_TEST(test_history_ring);
    RUN_TEST(test_sim_fills_every_field_in_range);
    RUN_TEST(test_sim_scenarios_differ_and_cycle_moves);
    RUN_TEST(test_sim_is_deterministic);
    RUN_TEST(test_layout_regions_are_inside_their_panels_and_the_screen);
    RUN_TEST(test_theme_backgrounds_match_layout);
    RUN_TEST(test_renderer_steady_state_pushes_nothing);
    RUN_TEST(test_renderer_pushes_only_what_changed);
    RUN_TEST(test_renderer_new_network_sample_repaints_graph_only);
    RUN_TEST(test_renderer_stays_inside_regions);
    RUN_TEST(test_renderer_offline_banner_lifecycle);
    RUN_TEST(test_renderer_theme_switch_repaints_everything);
    RUN_TEST(test_renderer_handles_extreme_values);
    RUN_TEST(test_touch_zones);
    RUN_TEST(test_settings_defaults);
    RUN_TEST(test_settings_draw_covers_whole_screen);
    RUN_TEST(test_settings_taps);
    RUN_TEST(test_detail_page);
    return UNITY_END();
}

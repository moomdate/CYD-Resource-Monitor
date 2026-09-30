#include "ui/monitor_ui.h"
#include "ui/canvas.h"
#include "assets/layout.h"
#include "fonts/font_big.h"
#include "fonts/font_temp.h"
#include "fonts/font_val.h"
#include "fonts/font_ui.h"
#include "fonts/font_md.h"
#include "fonts/font_sm.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

// ---- state ------------------------------------------------------------------------
enum {
    RG_NODE, RG_STATUS, RG_CLOCK,
    RG_CPU_NAME, RG_CPU_RING, RG_CPU_TEMP, RG_CPU_ROWS, RG_CPU_CORES,
    RG_RAM, RG_NET_RATES, RG_NET_GRAPH,
    RG_DISK_NAME, RG_DISK_RING, RG_DISK_IO,
    RG_FOOT_L, RG_FOOT_R,
    RG_COUNT
};

static uint16_t  s_buf[14400];                 // fits the largest region / a 320x40 strip (28 KB)
static Canvas    cv(s_buf, sizeof(s_buf) / sizeof(s_buf[0]));
static const Theme *s_theme = &kThemes[0];
static const uint16_t *s_bg = nullptr;
static PushFn    s_push = nullptr;
static char      s_key[RG_COUNT][80];          // what each region last showed
static int       s_pushed = 0;
static bool      s_bannerOn = false;           // OFFLINE banner currently on screen
static bool      s_bannerDirty = false;        // a region under the banner repainted -> banner again

static inline uint16_t C(uint32_t rgb) { return rgb565(rgb); }

#define COL_OFF_VALUE 0x4A5A62                 // value text while there is no data ("--")

// Returns true if the region must be redrawn (its key changed).
static bool changed(int rg, const char *key) {
    if (strncmp(s_key[rg], key, sizeof(s_key[rg])) == 0) return false;
    strncpy(s_key[rg], key, sizeof(s_key[rg]) - 1);
    s_key[rg][sizeof(s_key[rg]) - 1] = 0;
    return true;
}

static bool beginRegion(const Rect &r) { return cv.begin(r.x, r.y, r.w, r.h, s_bg); }

static bool overlaps(const Rect &a, const Rect &b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

static void flush() {
    s_push(cv.x0, cv.y0, cv.w, cv.h, cv.px);
    s_pushed++;
    Rect r = { cv.x0, cv.y0, cv.w, cv.h };
    if (s_bannerOn && overlaps(r, L_BANNER)) s_bannerDirty = true;
}

static void fmtVal(char *out, size_t n, const Val &v, const char *fmt) {
    if (v.ok) snprintf(out, n, fmt, v.v);
    else snprintf(out, n, "--");
}

// Cut text (UTF-8 safe for our ASCII names) until it fits maxW.
static void fitText(char *s, const UiFont &f, int maxW, int tracking = 0) {
    int n = (int)strlen(s);
    while (n > 0 && cv.textWidth(f, s, tracking) > maxW) s[--n] = 0;
}

// ---- shapes ---------------------------------------------------------------------------
// Ring gauge, clockwise from 12 o'clock. segs == 0: continuous arc; segs > 0: that many
// separate blocks with gapDeg between them (lit blocks = round(frac * segs)).
static void ring(float cx, float cy, float rOut, float rIn, float frac,
                 uint16_t on, uint16_t off, int segs, float gapDeg) {
    const float rad = 3.14159265f / 180.0f;
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    int lit = (int)lroundf(frac * segs);
    float segDeg = segs ? 360.0f / segs : 360.0f;
    for (int y = (int)floorf(cy - rOut - 1); y <= (int)ceilf(cy + rOut + 1); y++)
        for (int x = (int)floorf(cx - rOut - 1); x <= (int)ceilf(cx + rOut + 1); x++) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            float d2 = dx * dx + dy * dy;
            if (d2 > (rOut + 1) * (rOut + 1) || d2 < (rIn - 1) * (rIn - 1)) continue;
            float d = sqrtf(d2);
            float radial = fminf(rOut - d, d - rIn) + 0.5f;
            if (radial <= 0) continue;
            if (radial > 1) radial = 1;
            float ang = atan2f(dx, -dy) / rad;       // 0 = up, clockwise
            if (ang < 0) ang += 360.0f;
            if (segs) {
                int s = (int)(ang / segDeg);
                if (s >= segs) s = segs - 1;
                float local = ang - s * segDeg;
                float edge = fminf(local - gapDeg / 2, segDeg - gapDeg / 2 - local) * rad * d + 0.5f;
                if (edge <= 0) continue;
                if (edge > 1) edge = 1;
                cv.blendPixel(x, y, s < lit ? on : off, (uint8_t)(radial * edge * 255));
            } else {
                cv.blendPixel(x, y, off, (uint8_t)(radial * 255));
                float end = frac * 360.0f;
                float cov = frac >= 1.0f ? 1.0f : fminf(end - ang, ang) * rad * d + 0.5f;
                if (frac > 0 && cov > 0) cv.blendPixel(x, y, on, (uint8_t)(fminf(cov, 1.0f) * radial * 255));
            }
        }
}

// 7 px tall arrow (stem + head), tip down or up.
static void arrow(float cx, int top, bool down, uint16_t c) {
    for (int r = 0; r < 7; r++) {
        int k = down ? r : 6 - r;                 // k: 0..2 stem, 3..6 head, tip at 6
        float half = k < 3 ? 0.8f : (6 - k) * 0.9f + 0.2f;
        cv.hspan(cx - half, cx + half, top + r, c);
    }
}

// status dot with a soft glow, like the box-shadow in the HTML
static void dot(float x, float y, uint16_t c) {
    for (int py = (int)y - 5; py <= (int)y + 5; py++)
        for (int px = (int)x - 5; px <= (int)x + 5; px++) {
            float d = sqrtf((px + 0.5f - x) * (px + 0.5f - x) + (py + 0.5f - y) * (py + 0.5f - y));
            if (d < 5.0f) cv.blendPixel(px, py, c, (uint8_t)((1.0f - d / 5.0f) * 70));
        }
    cv.fillCircle(x, y, 2.4f, c);
}

// ---- header ---------------------------------------------------------------------------
static void drawNode(const MonitorView &v) {
    char t[32];
    snprintf(t, sizeof t, "/ %s", v.host[0] ? v.host : "NODE 01");
    if (!changed(RG_NODE, t) || !beginRegion(L_NODE)) return;
    fitText(t, font_sm, L_NODE.w);
    cv.text(font_sm, L_NODE.x, 24, t, C(COL_DIM), ALIGN_LEFT, 0);
    flush();
}

static void drawStatus(const MonitorView &v) {
    const char *label = v.link == Link::Live ? "ONLINE" : v.link == Link::Demo ? "DEMO" : "OFFLINE";
    uint32_t col = v.link == Link::Live ? s_theme->lime : v.link == Link::Demo ? COL_WARN : COL_RED;
    bool lit = v.link != Link::Offline || v.blink;     // offline dot blinks
    char key[32];
    snprintf(key, sizeof key, "%s|%d", label, lit);
    if (!changed(RG_STATUS, key) || !beginRegion(L_STATUS)) return;
    if (lit) dot(L_STATUS.x + 4.0f, 21.0f, C(col));
    cv.text(font_sm, 250, 24, label, C(col), ALIGN_RIGHT, 1);
    flush();
}

static void drawClock(const MonitorView &v) {
    char t[12];
    if (v.clockOk) snprintf(t, sizeof t, "%02d:%02d:%02d", v.clockSec / 3600, v.clockSec / 60 % 60, v.clockSec % 60);
    else strcpy(t, "--:--:--");
    if (!changed(RG_CLOCK, t) || !beginRegion(L_CLOCK)) return;
    cv.text(font_md, 304, 25, t, C(v.clockOk ? 0xB9D6DC : COL_OFF_VALUE), ALIGN_RIGHT, 0, true);
    flush();
}

// ---- CPU ------------------------------------------------------------------------------
static bool hot(const Val &t) { return t.ok && t.v >= 85; }

static void drawCpuName(const MonitorView &v) {
    if (!changed(RG_CPU_NAME, v.cpuName) || !beginRegion(L_CPU_NAME)) return;
    char t[28];
    snprintf(t, sizeof t, "%s", v.cpuName[0] ? v.cpuName : "PROCESSOR");
    fitText(t, font_sm, L_CPU_NAME.w);
    cv.text(font_sm, L_CPU_NAME.x, 52, t, C(COL_DIM));
    flush();
}

static void drawCpuRing(const MonitorView &v) {
    int pct = v.cpuLoad.ok ? (int)lroundf(v.cpuLoad.v) : -1;
    char key[32];
    snprintf(key, sizeof key, "%d|%d", pct, hot(v.cpuTemp));
    if (!changed(RG_CPU_RING, key) || !beginRegion(L_CPU_RING)) return;

    const float cx = P_CPU_RING_CX, cy = P_CPU_RING_CY;
    ring(cx, cy, P_CPU_RING_R, P_CPU_RING_R - P_CPU_RING_W, pct < 0 ? 0 : pct / 100.0f,
         C(s_theme->accent), C(COL_TRACK), 0, 0);

    char num[8];
    if (pct >= 0) snprintf(num, sizeof num, "%d", pct); else strcpy(num, "--");
    uint16_t col = pct < 0 ? C(COL_OFF_VALUE) : hot(v.cpuTemp) ? C(COL_RED) : C(COL_WHITE);
    cv.text(font_big, (int)cx, (int)cy + 8, num, col, ALIGN_CENTER, 0, true);
    cv.text(font_sm, (int)cx, (int)cy + 19, "% LOAD", C(s_theme->accentBright), ALIGN_CENTER, 1);
    flush();
}

static void drawCpuTemp(const MonitorView &v) {
    char t[12];
    if (v.cpuTemp.ok) snprintf(t, sizeof t, "%d°C", (int)lroundf(v.cpuTemp.v)); else strcpy(t, "--");
    char key[16];
    snprintf(key, sizeof key, "%s|%d", t, hot(v.cpuTemp));
    if (!changed(RG_CPU_TEMP, key) || !beginRegion(L_CPU_TEMP)) return;
    uint16_t col = !v.cpuTemp.ok ? C(COL_OFF_VALUE) : hot(v.cpuTemp) ? C(COL_RED) : C(s_theme->lime);
    cv.text(font_temp, P_CPU_COL_X, 79, t, col, ALIGN_LEFT, 0, true);
    flush();
}

// value + dim unit, right-aligned at x
static void valueUnit(int x, int base, const char *val, const char *unit, bool ok) {
    int uw = cv.textWidth(font_sm, unit);
    cv.text(font_sm, x, base, unit, C(COL_DIM), ALIGN_RIGHT);
    cv.text(font_md, x - uw - 1, base, val, C(ok ? COL_VALUE : COL_OFF_VALUE), ALIGN_RIGHT, 0, true);
}

static void drawCpuRows(const MonitorView &v) {
    char clk[10], pwr[10], vlt[10], fan[10];
    fmtVal(clk, sizeof clk, v.cpuGHz, "%.2f");
    if (v.cpuPower.ok && v.cpuPower.v >= 100) fmtVal(pwr, sizeof pwr, v.cpuPower, "%.0f");
    else fmtVal(pwr, sizeof pwr, v.cpuPower, "%.1f");
    fmtVal(vlt, sizeof vlt, v.cpuVolt, "%.3f");
    fmtVal(fan, sizeof fan, v.cpuFan, "%.0f");
    char key[48];
    snprintf(key, sizeof key, "%s|%s|%s|%s", clk, pwr, vlt, fan);
    if (!changed(RG_CPU_ROWS, key) || !beginRegion(L_CPU_ROWS)) return;
    const int b = P_CPU_ROW_Y0, dy = P_CPU_ROW_DY, r = P_CPU_COL_R;
    valueUnit(r, b,          clk, "GHz", v.cpuGHz.ok);
    valueUnit(r, b + dy,     pwr, "W",   v.cpuPower.ok);
    valueUnit(r, b + 2 * dy, vlt, "V",   v.cpuVolt.ok);
    valueUnit(r, b + 3 * dy, fan, "RPM", v.cpuFan.ok);
    flush();
}

static void drawCpuCores(const MonitorView &v) {
    const int n = v.coresOk && v.coreN ? v.coreN : 0;
    char key[80];
    int len = snprintf(key, sizeof key, "%d|", n);
    for (int i = 0; i < n && len < (int)sizeof key - 3; i++)
        len += snprintf(key + len, sizeof key - len, "%x", (int)lroundf(v.core[i] * 12 / 100));   // 0..12
    if (!changed(RG_CPU_CORES, key) || !beginRegion(L_CPU_CORES)) return;

    const int x0 = 17, span = 122, top = 130, hgt = 13;
    const int bars = n ? n : 16;                       // no data: empty tracks, so the strip keeps its shape
    for (int i = 0; i < bars; i++) {
        int xa = x0 + i * span / bars, xb = x0 + (i + 1) * span / bars - (bars > 8 ? 2 : 3);
        int w = xb - xa;
        if (w < 1) w = 1;
        cv.fillRect(xa, top, w, hgt, C(COL_SEG_OFF));
        if (!n) continue;
        float load = v.core[i];
        int fill = (int)lroundf(load * hgt / 100.0f);
        if (fill < 1) fill = 1;
        uint16_t col = load >= 90 ? C(COL_WARN) : C(s_theme->accent);
        cv.fillRect(xa, top + hgt - fill, w, fill, col);
        cv.fillRect(xa, top + hgt - fill, w, 1, shade565(col, 0.35f));   // bright cap, like the HTML glow
    }
    flush();
}

// ---- memory ---------------------------------------------------------------------------
static void drawRam(const MonitorView &v) {
    char used[24], pct[8];
    if (v.ramUsed.ok) {
        if (v.ramTotal.v >= 10) snprintf(used, sizeof used, "%.1f/%.0f GB", v.ramUsed.v, v.ramTotal.v);
        else snprintf(used, sizeof used, "%.1f/%.1f GB", v.ramUsed.v, v.ramTotal.v);
    } else strcpy(used, "--/-- GB");
    int segs = P_RAM_SEGS;
    int lit = v.ramPct.ok ? (int)lroundf(v.ramPct.v * segs / 100.0f) : 0;
    if (v.ramPct.ok) snprintf(pct, sizeof pct, "%d%%", (int)lroundf(v.ramPct.v)); else strcpy(pct, "--");
    char key[48];
    snprintf(key, sizeof key, "%s|%s|%d", used, pct, lit);
    if (!changed(RG_RAM, key) || !beginRegion(L_RAM_LIVE)) return;

    cv.text(font_md, 303, 52, pct, C(v.ramPct.ok ? s_theme->lime : COL_OFF_VALUE), ALIGN_RIGHT, 0, true);
    cv.text(font_md, 303, 66, used, C(v.ramUsed.ok ? COL_WHITE : COL_OFF_VALUE), ALIGN_RIGHT, 0, true);
    for (int i = 0; i < segs; i++) {
        uint16_t col = i < lit ? C(s_theme->lime) : C(COL_SEG_OFF);
        cv.fillRect(159 + i * 4, 74, 2, 8, col);
        if (i < lit) cv.fillRect(159 + i * 4, 74, 2, 1, shade565(col, 0.3f));
    }
    flush();
}

// ---- network --------------------------------------------------------------------------
// "84.2 Mbps", or "1.02 Gbps" once past 1000
static void rateText(const Val &v, char *num, size_t n, const char **unit) {
    *unit = "Mbps";
    if (!v.ok) { snprintf(num, n, "--"); return; }
    if (v.v >= 1000) { snprintf(num, n, "%.2f", v.v / 1000); *unit = "Gbps"; }
    else if (v.v >= 100) snprintf(num, n, "%.0f", v.v);
    else snprintf(num, n, "%.1f", v.v);
}

static void drawNetRates(const MonitorView &v) {
    char rx[12], tx[12];
    const char *ru, *tu;
    rateText(v.rxMbps, rx, sizeof rx, &ru);
    rateText(v.txMbps, tx, sizeof tx, &tu);
    char key[48];
    snprintf(key, sizeof key, "%s%s|%s%s", rx, ru, tx, tu);
    if (!changed(RG_NET_RATES, key) || !beginRegion(L_NET_RATES)) return;
    const int base = 122;
    arrow(163.5f, 115, true, C(s_theme->accent));
    int w = cv.text(font_md, 170, base, rx, C(v.rxMbps.ok ? s_theme->accent : COL_OFF_VALUE), ALIGN_LEFT, 0, true);
    cv.text(font_sm, 170 + w + 3, base, ru, C(COL_DIM));
    arrow(239.5f, 115, false, C(s_theme->lime));
    w = cv.text(font_md, 246, base, tx, C(v.txMbps.ok ? s_theme->lime : COL_OFF_VALUE), ALIGN_LEFT, 0, true);
    cv.text(font_sm, 246 + w + 3, base, tu, C(COL_DIM));
    flush();
}

// Smallest "nice" full-scale value (Mbps) that holds the peak.
static float niceScale(float peak) {
    static const float steps[] = { 10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000 };
    for (float s : steps) if (peak <= s * 0.95f) return s;
    return 10000;
}

static void drawNetGraph(const MonitorView &v) {
    const History &rx = v.hist->rx, &tx = v.hist->tx;
    const int N = P_NET_GRAPH_N;
    int n = rx.count < N ? rx.count : N;
    float peak = fmaxf(rx.maxOf(N), tx.maxOf(N));
    float vmax = niceScale(peak);
    char key[40];
    snprintf(key, sizeof key, "%u|%d|%d", (unsigned)rx.pushes, (int)vmax, v.link == Link::Offline);
    if (!changed(RG_NET_GRAPH, key) || !beginRegion(L_NET_GRAPH)) return;

    const int gx = L_NET_GRAPH.x + 1, gw = L_NET_GRAPH.w - 3;      // drawable columns gx .. gx+gw
    const int top = L_NET_GRAPH.y + 1, bot = L_NET_GRAPH.y + L_NET_GRAPH.h - 2;
    const float gh = (float)(bot - top);
    cv.fillRect(gx, bot + 1, gw + 1, 1, C(COL_LINE));               // baseline
    for (int x = gx; x <= gx + gw; x += 4) cv.pixel(x, (top + bot) / 2, C(0x1B333C));   // dotted mid line
    char lab[12];
    if (vmax >= 1000) snprintf(lab, sizeof lab, "%.0fG", vmax / 1000); else snprintf(lab, sizeof lab, "%.0fM", vmax);
    cv.text(font_sm, gx + gw, top + 6, lab, C(0x3E525B), ALIGN_RIGHT);

    if (n >= 2) {
        float step = (float)gw / (N - 1);
        auto X = [&](int i) { return gx + gw - (n - 1 - i) * step; };
        auto Y = [&](const History &h, int i) {
            float f = h.at(h.count - n + i) / vmax;
            return bot - (f > 1 ? 1 : f) * gh;
        };
        // soft area under the RX line
        for (int i = 0; i + 1 < n; i++) {
            float xa = X(i), xb = X(i + 1), ya = Y(rx, i), yb = Y(rx, i + 1);
            for (int x = (int)ceilf(xa); x <= (int)xb; x++) {
                float f = xb > xa ? (x - xa) / (xb - xa) : 0;
                for (int y = (int)(ya + (yb - ya) * f) + 1; y <= bot; y++)
                    cv.blendPixel(x, y, C(s_theme->accent), 22);
            }
        }
        for (int i = 0; i + 1 < n; i++) cv.line(X(i), Y(tx, i), X(i + 1), Y(tx, i + 1), C(s_theme->lime), 230);
        for (int i = 0; i + 1 < n; i++) {
            cv.line(X(i), Y(rx, i), X(i + 1), Y(rx, i + 1), C(s_theme->accent));
            cv.line(X(i), Y(rx, i) - 0.5f, X(i + 1), Y(rx, i + 1) - 0.5f, C(s_theme->accent), 110);  // a touch thicker
        }
    }
    flush();
}

// ---- NVMe -----------------------------------------------------------------------------
static void drawDiskName(const MonitorView &v) {
    char t[40];
    snprintf(t, sizeof t, "/ %s", v.diskName[0] ? v.diskName : "STORAGE TELEMETRY");
    if (!changed(RG_DISK_NAME, t) || !beginRegion(L_DISK_NAME)) return;
    fitText(t, font_sm, L_DISK_NAME.w);
    cv.text(font_sm, L_DISK_NAME.x, 167, t, C(COL_DIM), ALIGN_LEFT, 0);
    flush();
}

static void drawDiskRing(const MonitorView &v) {
    int pct = v.diskAct.ok ? (int)lroundf(v.diskAct.v) : -1;
    char key[16];
    snprintf(key, sizeof key, "%d", pct);
    if (!changed(RG_DISK_RING, key) || !beginRegion(L_DISK_RING)) return;
    const float cx = P_DISK_RING_CX, cy = P_DISK_RING_CY;
    ring(cx, cy, P_DISK_RING_R, P_DISK_RING_R - P_DISK_RING_W, pct < 0 ? 0 : pct / 100.0f,
         C(s_theme->lime), C(COL_SEG_OFF), 20, 5.0f);
    char num[8];
    // "100%" is wider than the ring's hole: at 100 the % sign is dropped
    if (pct >= 100) strcpy(num, "100");
    else if (pct >= 0) snprintf(num, sizeof num, "%d%%", pct);
    else strcpy(num, "--");
    cv.text(font_val, (int)cx, (int)cy + 8, num, C(pct < 0 ? COL_OFF_VALUE : COL_WHITE), ALIGN_CENTER, 0, true);
    flush();
}

// MB/s -> "1.8" + "GB/s" or "420" + "MB/s"
static void ioText(const Val &v, char *num, size_t n, const char **unit) {
    *unit = "MB/s";
    if (!v.ok) { snprintf(num, n, "--"); return; }
    if (v.v >= 1000) { snprintf(num, n, "%.1f", v.v / 1024); *unit = "GB/s"; }
    else snprintf(num, n, "%.0f", v.v);
}

static void drawDiskIo(const MonitorView &v) {
    char rd[10], wr[10], tp[10];
    const char *ru, *wu;
    ioText(v.diskR, rd, sizeof rd, &ru);
    ioText(v.diskW, wr, sizeof wr, &wu);
    if (v.diskTemp.ok) snprintf(tp, sizeof tp, "%d", (int)lroundf(v.diskTemp.v)); else strcpy(tp, "--");
    bool hotDisk = v.diskTemp.ok && v.diskTemp.v >= 70;
    char key[48];
    snprintf(key, sizeof key, "%s%s|%s%s|%s|%d", rd, ru, wr, wu, tp, hotDisk);
    if (!changed(RG_DISK_IO, key) || !beginRegion(L_DISK_IO)) return;
    const int base = 206;
    int w = cv.text(font_val, 84, base, rd, C(v.diskR.ok ? COL_VALUE : COL_OFF_VALUE), ALIGN_LEFT, 0, true);
    cv.text(font_sm, 84 + w + 3, base, ru, C(COL_DIM));
    w = cv.text(font_val, 164, base, wr, C(v.diskW.ok ? COL_VALUE : COL_OFF_VALUE), ALIGN_LEFT, 0, true);
    cv.text(font_sm, 164 + w + 3, base, wu, C(COL_DIM));
    uint16_t tc = !v.diskTemp.ok ? C(COL_OFF_VALUE) : hotDisk ? C(COL_RED) : C(s_theme->lime);
    w = cv.text(font_val, 244, base, tp, tc, ALIGN_LEFT, 0, true);
    if (v.diskTemp.ok) cv.text(font_sm, 244 + w + 3, base, "°C", C(COL_DIM));
    flush();
}

// ---- footer ---------------------------------------------------------------------------
static void drawFooter(const MonitorView &v, const char *demoLabel) {
    char l[40], r[40];
    uint32_t lc = COL_FOOT, rc = COL_FOOT;
    switch (v.link) {
        case Link::Live:    snprintf(l, sizeof l, "LIVE FEED"); snprintf(r, sizeof r, "USB / LINK OK"); break;
        case Link::Demo:    snprintf(l, sizeof l, "DEMO / %s", demoLabel ? demoLabel : "");
                            snprintf(r, sizeof r, "SIMULATED DATA"); lc = COL_WARN; break;
        default:            snprintf(l, sizeof l, "NO SIGNAL"); snprintf(r, sizeof r, "WAITING FOR AGENT");
                            lc = rc = COL_RED; break;
    }
    if (changed(RG_FOOT_L, l) && beginRegion(L_FOOT_L)) {
        cv.text(font_sm, L_FOOT_L.x, 233, l, C(lc), ALIGN_LEFT, 0);
        flush();
    }
    if (changed(RG_FOOT_R, r) && beginRegion(L_FOOT_R)) {
        cv.text(font_sm, L_FOOT_R.x + L_FOOT_R.w - 2, 233, r, C(rc), ALIGN_RIGHT, 0);
        flush();
    }
}

static void invalidateAll() {
    memset(s_key, 0, sizeof s_key);
    for (auto &k : s_key) k[0] = 1;
}

// ---- offline banner -------------------------------------------------------------------
// Composed from the background (not the widgets under it), pushed after them. When the
// link comes back the background is restored and every region redraws.
static void drawBanner(bool show) {
    if (!show) {
        if (s_bannerOn) {
            s_bannerOn = false;
            beginRegion(L_BANNER);
            flush();
            invalidateAll();
        }
        return;
    }
    bool first = !s_bannerOn;
    s_bannerOn = true;
    if (!first && !s_bannerDirty) return;
    s_bannerDirty = false;
    beginRegion(L_BANNER);
    const Rect &r = L_BANNER;
    cv.blendRect(r.x, r.y, r.w, r.h, 0x0000, 225);
    uint16_t red = C(COL_RED);
    cv.frame(r.x, r.y, r.w, r.h, shade565(red, -0.35f));
    cv.fillRect(r.x, r.y, 3, r.h, red);
    int mid = r.x + r.w / 2 + 2;
    cv.text(font_ui, mid, r.y + 21, "OFFLINE", red, ALIGN_CENTER, 3);
    cv.text(font_md, mid, r.y + 36, "WAITING FOR AGENT", C(COL_WHITE), ALIGN_CENTER, 0);
    cv.text(font_sm, mid, r.y + 49, "python monitor_agent.py", C(s_theme->accentBright), ALIGN_CENTER, 0);
    cv.text(font_sm, mid, r.y + 59, "or SETUP > DEMO for a demo", C(COL_DIM), ALIGN_CENTER, 0);
    flush();
    s_bannerDirty = false;     // flush() of the banner itself is not "a region under it"
}

// ---- public ---------------------------------------------------------------------------
namespace monitor_ui {

void begin(PushFn push, const Theme &theme) {
    s_push = push;
    setTheme(theme);
}

void setTheme(const Theme &theme) {
    s_theme = &theme;
    s_bg = theme.background;
    redrawAll();
}

const Theme &theme() { return *s_theme; }

void redrawAll() {
    const int rows = (int)(sizeof(s_buf) / sizeof(s_buf[0])) / 320;
    for (int y = 0; y < 240; y += rows) {
        int h = y + rows > 240 ? 240 - y : rows;
        cv.begin(0, y, 320, h, s_bg);
        s_push(0, y, 320, h, cv.px);
    }
    s_bannerOn = false;
    invalidate();
}

void invalidate() { invalidateAll(); }

void render(const MonitorView &v, const char *demoLabel) {
    s_pushed = 0;
    bool offline = v.link == Link::Offline;
    if (!offline && s_bannerOn) drawBanner(false);      // link is back: restore art, repaint all

    drawNode(v);
    drawStatus(v);
    drawClock(v);
    drawCpuName(v);
    drawCpuRing(v);
    drawCpuTemp(v);
    drawCpuRows(v);
    drawCpuCores(v);
    drawRam(v);
    drawNetRates(v);
    drawNetGraph(v);
    drawDiskName(v);
    drawDiskRing(v);
    drawDiskIo(v);
    drawFooter(v, demoLabel);
    if (offline) drawBanner(true);
}

int lastPushedRegions() { return s_pushed; }
Canvas &canvas() { return cv; }
PushFn pushFn() { return s_push; }

Zone hit(int x, int y) {
    auto in = [&](const Rect &r) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; };
    if (y < 36 || in(L_SETUP) || y >= 222) return Z_SETUP;   // header and footer strips open settings
    if (in(L_CPU)) return Z_CPU;
    if (in(L_RAM)) return Z_RAM;
    if (in(L_NET)) return Z_NET;
    if (in(L_DISK)) return Z_DISK;
    return Z_NONE;
}

}  // namespace monitor_ui

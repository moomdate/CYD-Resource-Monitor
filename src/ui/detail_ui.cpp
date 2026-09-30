#include "ui/detail_ui.h"
#include "ui/canvas.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "fonts/font_big.h"
#include "fonts/font_ui.h"
#include "fonts/font_md.h"
#include "fonts/font_sm.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace detail_ui {

// ---- layout ----------------------------------------------------------------------
static const Rect R_BACK = { 4, 4, 64, 22 };
static const Rect R_PREV = { 236, 4, 36, 22 };
static const Rect R_NEXT = { 276, 4, 36, 22 };
static const int GX = 36, GY = 76, GW = 272, GH = 104;     // graph box

static inline uint16_t C(uint32_t rgb) { return rgb565(rgb); }

struct Info { const char *name; const char *unit; };
static const Info kInfo[M_COUNT] = {
    { "CPU LOAD",      "%"    },
    { "CPU TEMP",      "°C"   },
    { "MEMORY",        "%"    },
    { "GPU LOAD",      "%"    },
    { "NETWORK",       "Mbps" },
    { "NVME ACTIVITY", "%"    },
};

int metricForZone(monitor_ui::Zone z) {
    switch (z) {
        case monitor_ui::Z_RAM:  return M_RAM;
        case monitor_ui::Z_NET:  return M_NET;
        case monitor_ui::Z_DISK: return M_DISK;
        default:                 return M_CPU;
    }
}

static uint32_t metricColor(const Theme &t, int m) {
    switch (m) {
        case M_CPU:  return t.accent;
        case M_GPU:  return t.accentBright;
        case M_NET:  return t.accent;
        default:     return t.lime;
    }
}

static const Val &current(const MonitorView &v, int m) {
    switch (m) {
        case M_CPU:  return v.cpuLoad;
        case M_TEMP: return v.cpuTemp;
        case M_RAM:  return v.ramPct;
        case M_GPU:  return v.gpuLoad;
        case M_NET:  return v.rxMbps;
        default:     return v.diskAct;
    }
}

// Full-scale of the graph: fixed 0..100 for percentages and temperature, "nice" steps for network.
static float graphMax(const MonitorView &v, int m) {
    if (m != M_NET) return 100.0f;
    float peak = fmaxf(v.hist->rx.maxOf(kHist), v.hist->tx.maxOf(kHist));
    static const float steps[] = { 10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000 };
    for (float s : steps) if (peak <= s * 0.95f) return s;
    return 10000;
}

uint32_t contentKey(const MonitorView &v, int m) {
    uint32_t k = v.hist->of(m).pushes * 8 + (uint32_t)v.link * 2 + (current(v, m).ok ? 1 : 0);
    return k * 8 + (uint32_t)m;
}

static void contextLines(const MonitorView &v, int m, char *l1, size_t n1, char *l2, size_t n2) {
    l1[0] = l2[0] = 0;
    switch (m) {
        case M_CPU:
            snprintf(l1, n1, "%s", v.cpuName[0] ? v.cpuName : "PROCESSOR");
            if (v.coresOk) snprintf(l2, n2, "%d THREADS", v.coreN);
            if (v.cpuGHz.ok) snprintf(l2 + strlen(l2), n2 - strlen(l2), "%s%.2f GHZ", l2[0] ? "  " : "", v.cpuGHz.v);
            break;
        case M_TEMP:
            snprintf(l1, n1, "CPU TEMPERATURE");
            if (v.cpuPower.ok) snprintf(l2, n2, "PACKAGE POWER %.1f W", v.cpuPower.v);
            break;
        case M_RAM:
            snprintf(l1, n1, "PHYSICAL MEMORY");
            if (v.ramUsed.ok) snprintf(l2, n2, "%.1f OF %.0f GB IN USE", v.ramUsed.v, v.ramTotal.v);
            break;
        case M_GPU:
            snprintf(l1, n1, "%s", v.gpuName[0] ? v.gpuName : "NO GPU DATA");
            if (v.gpuTemp.ok) snprintf(l2, n2, "%.0f°C", v.gpuTemp.v);
            if (v.vramTotal.ok) snprintf(l2 + strlen(l2), n2 - strlen(l2), "%sVRAM %.1f/%.0f GB", l2[0] ? "  " : "",
                                         v.vramUsed.v, v.vramTotal.v);
            break;
        case M_NET:
            snprintf(l1, n1, "DOWNLOAD (RX) / UPLOAD (TX)");
            if (v.txMbps.ok) snprintf(l2, n2, "TX NOW %.1f Mbps", v.txMbps.v);
            break;
        default:
            snprintf(l1, n1, "%s", v.diskName[0] ? v.diskName : "STORAGE");
            if (v.diskR.ok && v.diskW.ok) snprintf(l2, n2, "READ %.0f  WRITE %.0f MB/s", v.diskR.v, v.diskW.v);
            break;
    }
}

static void plot(Canvas &cv, const History &h, float vmax, uint16_t col, bool fill) {
    if (h.count < 2) return;
    float step = (float)(GW - 2) / (kHist - 1);
    float xr = GX + GW - 1;
    int n = h.count;
    auto X = [&](int i) { return xr - (n - 1 - i) * step; };
    auto Y = [&](int i) { float f = h.at(i) / vmax; return GY + GH - 2 - (f > 1 ? 1 : f) * (GH - 4); };
    if (fill) {
        for (int i = 0; i + 1 < n; i++) {
            float xa = X(i), xb = X(i + 1), ya = Y(i), yb = Y(i + 1);
            for (int x = (int)ceilf(xa); x <= (int)xb; x++) {
                float f = xb > xa ? (x - xa) / (xb - xa) : 0;
                for (int y = (int)(ya + (yb - ya) * f) + 1; y < GY + GH - 1; y++) cv.blendPixel(x, y, col, 22);
            }
        }
    }
    for (int i = 0; i + 1 < n; i++) {
        cv.line(X(i), Y(i), X(i + 1), Y(i + 1), col);
        cv.line(X(i), Y(i) - 0.5f, X(i + 1), Y(i + 1) - 0.5f, col, 110);
    }
}

static void compose(Canvas &cv, const MonitorView &v, int m) {
    const Theme &t = monitor_ui::theme();
    uint16_t col = C(metricColor(t, m));
    const Val &cur = current(v, m);
    const History &h = v.hist->of(m);

    widgets::button(cv, t, R_BACK, "< BACK", false, font_ui);
    widgets::button(cv, t, R_PREV, "<", false, font_ui);
    widgets::button(cv, t, R_NEXT, ">", false, font_ui);
    cv.text(font_ui, 154, 21, kInfo[m].name, col, ALIGN_CENTER, 2);
    widgets::fadeRule(cv, t, 8, 30, 304);

    // current value
    char num[12];
    if (cur.ok) {
        if (m == M_NET) snprintf(num, sizeof num, cur.v >= 100 ? "%.0f" : "%.1f", cur.v);
        else snprintf(num, sizeof num, "%d", (int)lroundf(cur.v));
    } else strcpy(num, "--");
    int w = cv.text(font_big, 12, 66, num, C(cur.ok ? COL_WHITE : 0x4A5A62), ALIGN_LEFT, 0, true);
    cv.text(font_md, 12 + w + 5, 66, kInfo[m].unit, C(COL_DIM), ALIGN_LEFT, 0);
    char l1[48], l2[48];
    contextLines(v, m, l1, sizeof l1, l2, sizeof l2);
    cv.text(font_md, 308, 50, l1, C(COL_VALUE), ALIGN_RIGHT);
    cv.text(font_sm, 308, 64, l2, C(COL_DIM), ALIGN_RIGHT);

    // graph box
    float vmax = graphMax(v, m);
    cv.fillRect(GX, GY, GW, GH, C(0x091116));
    cv.frame(GX, GY, GW, GH, C(COL_LINE));
    char lab[12];
    for (int i = 0; i <= 4; i++) {
        int y = GY + GH - 1 - i * (GH - 1) / 4;
        if (i > 0 && i < 4)
            for (int x = GX + 2; x < GX + GW - 2; x += 5) cv.pixel(x, y, C(0x1B333C));
        float val = vmax * i / 4;
        if (vmax >= 1000) snprintf(lab, sizeof lab, "%.1fG", val / 1000);
        else snprintf(lab, sizeof lab, "%.0f", val);
        cv.text(font_sm, GX - 4, y + 3, lab, C(COL_DIM), ALIGN_RIGHT);
    }
    if (m == M_NET) plot(cv, v.hist->tx, vmax, C(t.lime), false);
    plot(cv, h, vmax, col, true);

    // stats
    float mn, mx, avg;
    h.stats(mn, mx, avg);
    const char *fmt = m == M_NET ? "%.1f" : "%.0f";
    struct { const char *lab; float val; int x; } st[3] = { { "MIN", mn, 12 }, { "AVG", avg, 96 }, { "MAX", mx, 180 } };
    for (auto &s : st) {
        char b[16];
        snprintf(b, sizeof b, fmt, s.val);
        cv.text(font_sm, s.x, 199, s.lab, C(COL_DIM), ALIGN_LEFT, 1);
        int bw = cv.text(font_md, s.x, 215, h.count ? b : "--", C(COL_VALUE), ALIGN_LEFT, 0, true);
        cv.text(font_sm, s.x + bw + 3, 215, kInfo[m].unit, C(COL_DIM));
    }
    cv.text(font_sm, 308, 215, m == M_NET ? "RX / TX  LAST 60 S" : "LAST 60 S", C(COL_DIM), ALIGN_RIGHT);
    if (v.link == Link::Offline) cv.text(font_sm, 308, 230, "OFFLINE - NO NEW DATA", C(COL_RED), ALIGN_RIGHT);
}

void draw(const MonitorView &v, int m) {
    Canvas &cv = monitor_ui::canvas();
    PushFn push = monitor_ui::pushFn();
    for (int y = 0; y < 240; y += 40) {
        cv.beginFlat(0, y, 320, 40, C(COL_BG));
        compose(cv, v, m);
        push(0, y, 320, 40, cv.px);
    }
}

Action tap(int x, int y, int &m) {
    using widgets::inside;
    if (inside(R_BACK, x, y)) return ACT_BACK;
    if (inside(R_PREV, x, y)) { m = (m + M_COUNT - 1) % M_COUNT; return ACT_METRIC; }
    if (inside(R_NEXT, x, y)) { m = (m + 1) % M_COUNT; return ACT_METRIC; }
    return ACT_NONE;
}

}  // namespace detail_ui

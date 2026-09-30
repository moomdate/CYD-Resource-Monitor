// Host-side preview: compiles the REAL firmware UI, model, JSON parser and simulator for
// macOS/Linux and writes frames as PPM, so layout changes can be checked without flashing.
//   tools/host_preview/run.sh           -> tools/host_preview/out/*.png (+ sim.gif)
#include <stdio.h>
#include <string.h>
#include <string>
#include "data/sim_source.h"
#include "data/telemetry.h"
#include "ui/detail_ui.h"
#include "ui/monitor_model.h"
#include "ui/monitor_ui.h"
#include "ui/settings_ui.h"
#include "ui/theme.h"

static uint16_t fb[320 * 240];
static long pushedPixels = 0;

static void push(int x, int y, int w, int h, const uint16_t *px) {
    for (int r = 0; r < h; r++) memcpy(&fb[(y + r) * 320 + x], px + r * w, w * 2);
    pushedPixels += (long)w * h;
}

static void save(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    fprintf(f, "P6 320 240 255\n");
    for (int i = 0; i < 320 * 240; i++) {
        uint16_t c = fb[i];
        uint8_t rgb[3] = { (uint8_t)((c >> 11) << 3 | (c >> 13)), (uint8_t)(((c >> 5) & 63) << 2 | ((c >> 9) & 3)),
                           (uint8_t)((c & 31) << 3 | ((c >> 2) & 7)) };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

static std::string readFile(const char *path) {
    std::string s;
    if (FILE *f = fopen(path, "rb")) {
        char b[512];
        size_t n;
        while ((n = fread(b, 1, sizeof b, f)) > 0) s.append(b, n);
        fclose(f);
    } else {
        fprintf(stderr, "missing %s (run from tools/host_preview)\n", path);
    }
    return s;
}

// Feed the same snapshot for `seconds` of virtual time (2 Hz), render the last state.
struct Scene {
    MonitorModel model;
    MonitorView view;
    uint32_t now = 100000;
    void feed(const Telemetry &t, bool demo, int seconds) {
        Telemetry cur = t;
        for (int i = 0; i < seconds * 2; i++) {
            now += 500;
            cur.rxAt = now;
            model.update(cur, demo, now, view);
        }
    }
};

static void dash(const char *path, const Theme &th, Scene &s, const char *label) {
    monitor_ui::setTheme(th);
    monitor_ui::invalidate();
    monitor_ui::render(s.view, label);
    save(path);
}

// Run the simulator through a scenario and return the scene (histories filled).
static void simulate(Scene &s, SimSource &sim, uint8_t scenario, int seconds) {
    Telemetry t;
    for (int i = 0; i < seconds * 2; i++) {
        s.now += 500;
        sim.sample(s.now, scenario, t);
        s.model.update(t, true, s.now, s.view);
    }
}

static bool loadJson(const char *path, Telemetry &t, uint32_t now) {
    std::string j = readFile(path);
    bool ok = telemetryParse(j.c_str(), t, now);
    if (!ok) fprintf(stderr, "parse failed: %s\n", path);
    return ok;
}

int main() {
    monitor_ui::begin(push, kThemes[THEME_ICE]);
    char p[96];

    // ---- the three themes, GAMING preset ----
    for (int t = 0; t < THEME_COUNT; t++) {
        Scene s;
        SimSource sim;
        sim.begin(s.now);
        simulate(s, sim, SC_GAMING, 25);
        snprintf(p, sizeof p, "out/dash_%s.ppm", kThemes[t].key);
        dash(p, kThemes[t], s, "GAMING");
    }
    // ---- the other presets (ICE) ----
    {
        Scene s; SimSource sim; sim.begin(s.now);
        simulate(s, sim, SC_IDLE, 25);
        dash("out/dash_ice_idle.ppm", kThemes[THEME_ICE], s, "IDLE");
        Scene s2; SimSource sim2; sim2.begin(s2.now);
        simulate(s2, sim2, SC_TRANSFER, 25);
        dash("out/dash_ice_transfer.ppm", kThemes[THEME_ICE], s2, "TRANSFER");
    }
    // ---- real agent messages: full new agent, OLD agent (missing fields), partial (macOS-like) ----
    {
        const char *files[3][2] = { { "samples/new_agent.json", "out/dash_agent_new.ppm" },
                                    { "samples/old_agent.json", "out/dash_agent_old.ppm" },
                                    { "samples/partial_agent.json", "out/dash_agent_partial.ppm" } };
        for (auto &f : files) {
            Scene s;
            Telemetry t;
            if (!loadJson(f[0], t, s.now)) continue;
            s.feed(t, false, 20);
            dash(f[1], kThemes[THEME_ICE], s, nullptr);
        }
    }
    // ---- offline: never connected, and "was live, agent stopped" ----
    {
        Scene s;
        Telemetry none;                                          // rxAt = 0: nothing ever received
        s.model.update(none, false, s.now, s.view);
        dash("out/dash_offline.ppm", kThemes[THEME_ICE], s, nullptr);
    }
    {
        Scene s;
        Telemetry t;
        loadJson("samples/new_agent.json", t, s.now);
        s.feed(t, false, 10);
        monitor_ui::setTheme(kThemes[THEME_AMBER]);
        monitor_ui::render(s.view, nullptr);                    // live frame first...
        s.now += 4000;                                          // ...then the agent goes quiet
        s.model.update(t, false, s.now, s.view);
        monitor_ui::render(s.view, nullptr);
        save("out/dash_stale_amber.ppm");
    }

    // ---- settings pages ----
    for (int t = 0; t < THEME_COUNT; t++) {
        Settings st;
        st.theme = t; st.source = t == 1 ? SRC_DEMO : SRC_LIVE; st.scenario = SC_GAMING; st.brightness = 60;
        settings_ui::draw(st, t != 2);
        snprintf(p, sizeof p, "out/settings_%s.ppm", kThemes[t].key);
        save(p);
    }

    // ---- detail pages ----
    {
        Scene s; SimSource sim; sim.begin(s.now);
        simulate(s, sim, SC_CYCLE, 100);
        monitor_ui::setTheme(kThemes[THEME_ICE]);
        static const char *names[M_COUNT] = { "cpu", "temp", "ram", "gpu", "net", "disk" };
        for (int m = 0; m < M_COUNT; m++) {
            detail_ui::draw(s.view, m);
            snprintf(p, sizeof p, "out/detail_%s.ppm", names[m]);
            save(p);
        }
    }

    // ---- run the demo for 90 s of virtual time: frames, and how much of the screen is pushed ----
    {
        monitor_ui::setTheme(kThemes[THEME_ICE]);
        Scene s; SimSource sim; sim.begin(s.now);
        Telemetry t;
        long frames = 0, regions = 0, maxRegions = 0;
        monitor_ui::invalidate();
        pushedPixels = 0;
        for (int f = 0; f < 90 * 30; f++) {
            s.now += 33;
            if (f % 15 == 0) sim.sample(s.now, SC_CYCLE, t);        // 500 ms cadence, like the agent
            s.model.update(t, true, s.now, s.view);
            monitor_ui::render(s.view, SimSource::scenarioName(SC_CYCLE));
            frames++;
            regions += monitor_ui::lastPushedRegions();
            if (monitor_ui::lastPushedRegions() > maxRegions) maxRegions = monitor_ui::lastPushedRegions();
            if (f > 30 && f % 9 == 0 && f < 30 * 24) {
                snprintf(p, sizeof p, "out/sim_%05d.ppm", f / 9);
                save(p);
            }
        }
        printf("demo run: %ld frames, avg %.1f regions/frame (max %ld), avg pushed px/frame = %ld (%.1f%% of the screen)\n",
               frames, (double)regions / frames, maxRegions, pushedPixels / frames, 100.0 * pushedPixels / frames / 76800.0);
    }
    return 0;
}

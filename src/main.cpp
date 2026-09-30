// CYD Resource Monitor - "SYSTEM RESEARCH / NODE 01"
// PC/Mac resource dashboard on an ESP32-2432S028R-compatible 2.8" TFT board. Data arrives
// as JSON lines over USB serial from agent/monitor_agent.py, or comes from the built-in
// demo simulator. See README.md.
//
//   loop(): serial -> Telemetry -> MonitorModel -> partial-redraw renderer, touch, settings.
//
// Touch (dashboard): tap the header or SETUP -> settings page
//                    tap a panel (CPU / RAM / NET / NVMe) -> 60 s history page
#include <Arduino.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <TFT_Touch.h>

#include "config.h"
#include "settings.h"
#include "data/sim_source.h"
#include "data/telemetry.h"
#include "ui/detail_ui.h"
#include "ui/monitor_model.h"
#include "ui/monitor_ui.h"
#include "ui/settings_ui.h"
#include "ui/theme.h"

static TFT_eSPI  tft;
//                    DCS DCLK DIN DOUT  (bit-banged XPT2046, proven for this board)
static TFT_Touch touch(TOUCH_DCS, TOUCH_DCLK, TOUCH_DIN, TOUCH_DOUT);
static Preferences prefs;

static Settings      settings;
static Telemetry     telem;          // what the UI shows (agent data in LIVE, simulator in DEMO)
static MonitorModel  model;
static MonitorView   view;
static SimSource     sim;
static LineReader    lineReader;
static uint32_t      lastAgentMs = 0;   // last valid agent message, whatever the source setting

enum Screen { SCR_DASH, SCR_DETAIL, SCR_SETTINGS };
static Screen screen = SCR_DASH;
static int    detailMetric = M_CPU;

// ---- settings (NVS) -----------------------------------------------------------------
static void loadSettings() {
    prefs.begin("resmon", true);
    settings.theme      = prefs.getUChar("theme", settings.theme);
    settings.source     = prefs.getUChar("src", settings.source);
    settings.scenario   = prefs.getUChar("scn", settings.scenario);
    settings.brightness = prefs.getUChar("bright", settings.brightness);
    prefs.end();
    if (settings.theme >= THEME_COUNT) settings.theme = 0;     // also maps the old firmware's 4 themes
    if (settings.source >= SRC_COUNT) settings.source = SRC_LIVE;
    if (settings.scenario >= SC_COUNT) settings.scenario = SC_CYCLE;
    if (settings.brightness < Settings::kBrightMin) settings.brightness = Settings::kBrightMin;
    if (settings.brightness > 100) settings.brightness = 100;
}

static void saveSettings() {   // Preferences only writes keys whose value changed
    prefs.begin("resmon", false);
    prefs.putUChar("theme", settings.theme);
    prefs.putUChar("src", settings.source);
    prefs.putUChar("scn", settings.scenario);
    prefs.putUChar("bright", settings.brightness);
    prefs.end();
}

// ---- backlight (see BACKLIGHT_DIMMING in config.h) ----------------------------------------
static void backlightBegin() {
#if BACKLIGHT_DIMMING
    ledcAttach(TFT_BL, 5000, 8);           // after tft.init() has driven TFT_BL HIGH
#else
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);            // full on: this board's backlight can't dim with PWM
#endif
}

static void setBacklight(uint8_t pct) {
#if BACKLIGHT_DIMMING
    ledcWrite(TFT_BL, (uint32_t)pct * 255 / 100);
#else
    (void)pct;
#endif
}

// ---- data source ----------------------------------------------------------------------
// LIVE: JSON lines from the agent; DEMO: simulator. Agent lines are parsed in both modes so
// the settings page can say whether an agent is connected, but only LIVE uses them.
static void pollSerial(uint32_t now) {
    static Telemetry incoming;
    while (Serial.available()) {
        if (!lineReader.feed((char)Serial.read())) continue;
        if (!telemetryParse(lineReader.line(), incoming, now)) continue;
        lastAgentMs = now;
        if (settings.source == SRC_LIVE) telem = incoming;
    }
}

static void pollDemo(uint32_t now) {
    static uint32_t last = 0;
    if (settings.source != SRC_DEMO || (last && now - last < SAMPLE_MS)) return;
    last = now;
    sim.sample(now, settings.scenario, telem);
}

static void switchSource(uint32_t now) {
    telem = Telemetry();          // rxAt = 0: OFFLINE until the new source delivers
    model.reset();
    if (settings.source == SRC_DEMO) sim.begin(now);
    monitor_ui::invalidate();
}

// ---- display ---------------------------------------------------------------------------
static void pushToTft(int x, int y, int w, int h, const uint16_t *px) {
    tft.pushImage(x, y, w, h, const_cast<uint16_t *>(px));
}

static bool agentLive(uint32_t now) { return lastAgentMs && now - lastAgentMs <= STALE_MS; }

static void openSettings(uint32_t now) {
    screen = SCR_SETTINGS;
    settings_ui::draw(settings, agentLive(now));
}

static void closeToDash() {
    screen = SCR_DASH;
    monitor_ui::setTheme(kThemes[settings.theme]);   // repaints art, all regions redraw next frame
}

// ---- touch (acts on release, at the press position, so a drag never fires a button) ----------
static void handleTouch(uint32_t now) {
    static bool down = false;
    static uint32_t downAt = 0;
    static int sx = 0, sy = 0;

    bool pressed = touch.Pressed();
    if (pressed) {
        int x = touch.X(), y = touch.Y();
        if (!down) { down = true; downAt = now; sx = x; sy = y; }
        return;
    }
    if (!down) return;
    down = false;
    if (now - downAt > TAP_MAX_MS) return;

    switch (screen) {
    case SCR_DASH: {
        monitor_ui::Zone z = monitor_ui::hit(sx, sy);
        if (z == monitor_ui::Z_SETUP) openSettings(now);
        else if (z != monitor_ui::Z_NONE) {
            detailMetric = detail_ui::metricForZone(z);
            screen = SCR_DETAIL;
            detail_ui::draw(view, detailMetric);
        }
        break;
    }
    case SCR_DETAIL:
        switch (detail_ui::tap(sx, sy, detailMetric)) {
            case detail_ui::ACT_BACK:   closeToDash(); break;
            case detail_ui::ACT_METRIC: detail_ui::draw(view, detailMetric); break;
            default: break;
        }
        break;
    case SCR_SETTINGS: {
        Settings s = settings;
        switch (settings_ui::tap(sx, sy, s)) {
            case settings_ui::ACT_CHANGED: {
                bool srcChanged = s.source != settings.source || s.scenario != settings.scenario;
                bool briChanged = s.brightness != settings.brightness;
                settings = s;
                if (briChanged) setBacklight(settings.brightness);
                if (srcChanged) switchSource(now);
                saveSettings();
                settings_ui::draw(settings, agentLive(now));
                break;
            }
            case settings_ui::ACT_CLOSE: closeToDash(); break;
            default: break;
        }
        break;
    }
    }
}

void setup() {
    Serial.setRxBufferSize(2048);          // must precede begin(); a JSON line is ~600 bytes
    Serial.begin(SERIAL_BAUD);

    loadSettings();

    tft.init();
    tft.setRotation(1);
    tft.setSwapBytes(true);                // canvases hold plain RGB565
    backlightBegin();
    setBacklight(settings.brightness);
    touch.setCal(526, 3443, 750, 3377, 320, 240, 1);   // proven values for this board

    monitor_ui::begin(pushToTft, kThemes[settings.theme]);
    switchSource(millis());
    Serial.printf("[monitor] SYSTEM RESEARCH v" FW_VERSION " ready: theme %s, source %s, free heap %u\n",
                  kThemes[settings.theme].name, settings.source == SRC_DEMO ? "DEMO" : "PC AGENT",
                  (unsigned)ESP.getFreeHeap());
}

void loop() {
    static uint32_t nextFrame = 0, statAt = 0, frames = 0, pushes = 0, detailKey = 0;
    static bool lastAgentLive = false;

    uint32_t now = millis();
    if ((int32_t)(now - nextFrame) < 0) { delay(1); return; }
    nextFrame = now + 1000 / UI_FPS;

    pollSerial(now);
    pollDemo(now);
    model.update(telem, settings.source == SRC_DEMO, now, view);
    handleTouch(now);

    switch (screen) {
    case SCR_DASH:
        monitor_ui::render(view, SimSource::scenarioName(settings.scenario));
        pushes += monitor_ui::lastPushedRegions();
        break;
    case SCR_DETAIL: {
        uint32_t k = detail_ui::contentKey(view, detailMetric);
        if (k != detailKey) { detailKey = k; detail_ui::draw(view, detailMetric); }
        break;
    }
    case SCR_SETTINGS: {
        bool live = agentLive(now);
        if (live != lastAgentLive) { lastAgentLive = live; settings_ui::draw(settings, live); }
        break;
    }
    }

    frames++;
    if (now - statAt > 10000) {
        char cpuTxt[8] = "--";
        if (view.cpuLoad.ok) snprintf(cpuTxt, sizeof cpuTxt, "%d", (int)view.cpuLoad.v);
        Serial.printf("[monitor] %s  cpu=%s  ui %.1f fps, %.1f regions/frame, dropped lines %d, heap %u\n",
                      view.link == Link::Live ? "LIVE" : view.link == Link::Demo ? "DEMO" : "OFFLINE", cpuTxt,
                      frames * 1000.0f / (now - statAt), (float)pushes / frames, lineReader.droppedLines(),
                      (unsigned)ESP.getFreeHeap());
        statAt = now; frames = 0; pushes = 0;
    }
}

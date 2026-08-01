// ── CYD Resource Monitor ───────────────────────────
// PC/Mac resource dashboard on an ESP32-2432S028R-
// compatible 2.8" TFT board. Data arrives as JSON
// lines over USB serial from agent/monitor_agent.py.

#include <Arduino.h>
#include "config.h"
#include "ui.h"
#include "data.h"
#include "dash.h"
#include "detail.h"
#include "settings.h"

void setup() {
  Serial.begin(115200);
  Serial.setRxBufferSize(2048);

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  touch.setCal(526, 3443, 750, 3377, 320, 240, 1);

  fb.setColorDepth(8);
  if (!fb.createSprite(SCR_W, SCR_H)) {
    tft.setTextColor(TFT_RED);
    tft.drawString("Sprite alloc failed!", 20, 100, 2);
    while (true) delay(1000);
  }

  loadSettings();
}

void loop() {
  inputUpdate();
  serialPoll();

  fb.fillSprite(C_BG);
  switch (g_screen) {
    case SCR_DASH:     dash::update();     break;
    case SCR_DETAIL:   dpage::update();   break;
    case SCR_SETTINGS: settings::update(); break;
  }
  fb.pushSprite(0, 0);
}

#pragma once
// Small helpers shared by the settings and detail pages.
#include "assets/layout.h"
#include "ui/canvas.h"
#include "ui/theme.h"

namespace widgets {

// Resistive touch is +-3 px on a good day: every button gets a small margin.
bool inside(const Rect &r, int x, int y, int margin = 3);

// Button with a chamfered top-left corner, echoing the angled panels of the main screen.
// `on` = selected (accent outline + underline).
void button(Canvas &cv, const Theme &t, const Rect &r, const char *label, bool on, const UiFont &font);

// Thin accent rule that fades out to the right.
void fadeRule(Canvas &cv, const Theme &t, int x, int y, int w);

}  // namespace widgets

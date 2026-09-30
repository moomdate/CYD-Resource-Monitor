#include "ui/widgets.h"

#define COL_BTN  0x141B21
#define COL_EDGE 0x34434E
#define COL_TEXT 0xD9E2E7

namespace widgets {

bool inside(const Rect &r, int x, int y, int margin) {
    return x >= r.x - margin && x < r.x + r.w + margin && y >= r.y - margin && y < r.y + r.h + margin;
}

void button(Canvas &cv, const Theme &t, const Rect &r, const char *label, bool on, const UiFont &font) {
    const int cut = 4;
    uint16_t accent = rgb565(t.accent);
    uint16_t fill = on ? blend565(90, accent, rgb565(COL_BTN)) : rgb565(COL_BTN);
    uint16_t edge = on ? accent : rgb565(COL_EDGE);
    for (int y = 0; y < r.h; y++) {
        int inset = y < cut ? cut - y : 0;
        cv.fillRect(r.x + inset, r.y + y, r.w - inset, 1, fill);
        cv.pixel(r.x + inset, r.y + y, edge);                       // left edge / chamfer
        cv.pixel(r.x + r.w - 1, r.y + y, edge);
    }
    cv.fillRect(r.x + cut, r.y, r.w - cut, 1, edge);
    cv.fillRect(r.x, r.y + r.h - 1, r.w, 1, edge);
    if (on) cv.fillRect(r.x + 1, r.y + r.h - 3, r.w - 2, 2, accent);
    int base = r.y + (r.h + font.ascent) / 2 - (on ? 1 : 0);
    cv.text(font, r.x + r.w / 2, base, label, rgb565(on ? t.accentBright : COL_TEXT), ALIGN_CENTER, 1);
}

void fadeRule(Canvas &cv, const Theme &t, int x, int y, int w) {
    for (int i = 0; i < w; i++) cv.blendPixel(x + i, y, rgb565(t.accent), (uint8_t)(255 - i * 200 / w));
}

}  // namespace widgets

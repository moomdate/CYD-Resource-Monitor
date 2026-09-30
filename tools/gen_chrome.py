#!/usr/bin/env python3
"""
gen_chrome.py - render the static "SYSTEM RESEARCH" chrome (panel frames, titles, row
labels, ring backdrops, footer) once per colour theme at build time, and write it as
320x240 RGB565 headers for flash. The firmware only draws the *live* widgets on top.

  python3 tools/gen_chrome.py        # -> src/assets/bg_*.h, src/assets/layout.h, tools/art/bg_*.png

Geometry follows research-monitor/index.html (the source of truth) and
research_monitor.c (exact pixel positions). Where the concept image shows more than the
HTML (per-core bars, CPU power / voltage / fan, NVMe ring) those widgets are fitted
into the same panels. LAYOUT below is the single owner of every rectangle: it is also
emitted as src/assets/layout.h so the C++ renderer can never drift from the art.

Run tools/make_fonts.py first if you changed fonts. Requires Pillow + numpy.
"""
import math
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

import gen_font
from make_fonts import FONT_SPECS, TITLE_TTF, resolve

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
SS = 4  # supersampling for shapes; text is drawn at 1x so it stays crisp

# ---- palette (index.html :root + theme() colours) ------------------------------------------
COMMON = dict(bg=0x060A0D, panel=0x091116, line=0x25414B, dim=0x80939C, white=0xEAFFFF,
              warn=0xFFAC37, red=0xFF4659, track=0x1A2C33, disc=0x081015, disc_edge=0x27404A,
              inner_line=0x18313B, divider=0x213740, foot=0x62767E, label=0xC8E1E5)
THEMES = {
    #        accent    accent2   lime (secondary)
    "ice":    (0x21DDF3, 0x73F5FF, 0xA8F437),
    "violet": (0xBB79FF, 0xDFBAFF, 0x56E0CC),
    "amber":  (0xFFAB37, 0xFFD078, 0xB3F250),
}

# ---- layout: (x, y, w, h) in screen px ------------------------------------------------------
LAYOUT = {
    # panels
    "HEADER":    (9, 8, 302, 25),
    "CPU":       (9, 38, 137, 110),
    "RAM":       (151, 38, 160, 51),
    "NET":       (151, 95, 160, 53),
    "DISK":      (9, 155, 302, 66),
    # live regions (each one is composed + pushed on its own); NODE / CPU_NAME / DISK_NAME
    # start where their baked title ends - see fit_layout()
    "NODE":      (130, 12, 78, 18),
    "STATUS":    (206, 12, 46, 18),
    "CLOCK":     (256, 12, 50, 18),
    "CPU_NAME":  (38, 42, 104, 13),
    "CPU_RING":  (16, 60, 66, 66),
    "CPU_TEMP":  (82, 60, 60, 22),
    "CPU_ROWS":  (82, 84, 60, 42),
    "CPU_CORES": (15, 129, 126, 15),
    "RAM_LIVE":  (155, 41, 152, 45),
    "NET_RATES": (155, 110, 152, 15),
    "NET_GRAPH": (158, 126, 146, 19),
    "DISK_NAME": (66, 158, 240, 12),
    "DISK_RING": (21, 175, 44, 44),
    "DISK_IO":   (80, 174, 228, 44),
    "FOOT_L":    (76, 225, 100, 12),
    "FOOT_R":    (176, 225, 88, 12),
    "SETUP":     (268, 223, 43, 14),
    "BANNER":    (50, 82, 220, 64),
}
POINTS = {
    "CPU_RING_CX": 49, "CPU_RING_CY": 93, "CPU_RING_R": 31, "CPU_RING_W": 6,
    "DISK_RING_CX": 43, "DISK_RING_CY": 197, "DISK_RING_R": 20, "DISK_RING_W": 5,
    "CPU_ROW_Y0": 93, "CPU_ROW_DY": 10,          # baselines of CLK / PWR / VLT / FAN
    "CPU_COL_X": 83, "CPU_COL_R": 142,           # label x, value right edge
    "NET_GRAPH_N": 48,                           # samples across NET_GRAPH
    "RAM_SEGS": 36,
}

HEAD_SIZE = 12                                   # header title
TITLE_SIZE, TITLE_TRACK = 10, 0.8                # panel titles

# polygon clip-paths from the HTML (percent of the panel box)
CHAMFER = {
    "HEADER": [(0, 0), (98, 0), (100, 30), (100, 100), (2, 100), (0, 75)],
    "CPU":    [(0, 0), (95, 0), (100, 6), (100, 92), (95, 100), (0, 100)],
}


def rgb(v):
    return ((v >> 16) & 255, (v >> 8) & 255, v & 255)


class Face:
    """A font as the firmware will see it (real .ttf if present, else the Pillow fallback)."""

    def __init__(self, ttf, size, fallback):
        path, self.emb, self.cond = resolve(ttf, fallback)
        self.font = gen_font.load_font(path, size)

    def width(self, text, tracking=0.0):
        return gen_font.text_width(text, self.font, tracking / self.cond if self.cond else tracking,
                                   self.emb, 1.0) * self.cond

    def draw(self, img, xy, text, fill, tracking=0.0):
        gen_font.draw_text(img, xy, text, self.font, fill, tracking, self.emb, self.cond)


def title_face(size):
    return Face(TITLE_TTF[0], size, TITLE_TTF[1])


def small_face(name="font_sm"):
    ttf, size, _, fb = FONT_SPECS[name]
    return Face(ttf, size, fb)


def title_end(text, x, size, tracking):
    """Right edge of a panel title drawn at x, so dynamic text can start right after it."""
    return int(math.ceil(x + title_face(size).width(text, tracking)))


def fit_layout():
    """Dynamic text that follows a baked title starts where that title ends (font-independent)."""
    e = title_end("SYSTEM RESEARCH", 27, HEAD_SIZE, 1.2) + 7
    LAYOUT["NODE"] = (e, 12, 204 - e, 18)
    e = title_end("CPU", 17, TITLE_SIZE, TITLE_TRACK) + 6
    LAYOUT["CPU_NAME"] = (e, 42, 142 - e, 13)
    e = title_end("NVMe SSD", 17, TITLE_SIZE, TITLE_TRACK) + 6
    LAYOUT["DISK_NAME"] = (e, 158, 306 - e, 12)
    POINTS["NET_SUB_X"] = title_end("NETWORK", 159, TITLE_SIZE, TITLE_TRACK) + 6


fit_layout()


def panel_poly(name, inset=0.0):
    x, y, w, h = LAYOUT[name]
    pts = CHAMFER.get(name, [(0, 0), (100, 0), (100, 100), (0, 100)])
    return [(x + inset + px * (w - 2 * inset) / 100.0, y + inset + py * (h - 2 * inset) / 100.0) for px, py in pts]


def mask_of(draw_fn):
    """Render draw_fn at SS x, return a 320x240 float coverage mask (0..1)."""
    im = Image.new("L", (320 * SS, 240 * SS), 0)
    draw_fn(ImageDraw.Draw(im), SS)
    return np.asarray(im.resize((320, 240), Image.BOX), dtype=np.float32) / 255.0


def composite(canvas, mask, color):
    c = np.array(rgb(color) if isinstance(color, int) else color, dtype=np.float32)
    canvas[:] = canvas * (1 - mask[..., None]) + c * mask[..., None]


def gradient(x, y, w, h, c1, c2):
    yy, xx = np.mgrid[0:240, 0:320].astype(np.float32)
    t = np.clip(((xx - x) / max(w, 1) * 0.6 + (yy - y) / max(h, 1) * 0.4), 0, 1)[..., None]
    return np.array(rgb(c1), np.float32) * (1 - t) + np.array(rgb(c2), np.float32) * t


def poly_fill(name):
    return mask_of(lambda d, s: d.polygon([(px * s, py * s) for px, py in panel_poly(name)], fill=255))


def poly_line(name, inset, closed=True):
    pts = panel_poly(name, inset)

    def f(d, s):
        p = [(px * s, py * s) for px, py in pts]
        d.line(p + ([p[0]] if closed else []), fill=255, width=s)
    return mask_of(f)


def ring_mask(cx, cy, r_out, r_in, dash=None):
    """Annulus mask, optionally dashed (dash = degrees on/off)."""
    def f(d, s):
        if dash is None:
            d.ellipse([(cx - r_out) * s, (cy - r_out) * s, (cx + r_out) * s, (cy + r_out) * s], fill=255)
            d.ellipse([(cx - r_in) * s, (cy - r_in) * s, (cx + r_in) * s, (cy + r_in) * s], fill=0)
        else:
            rm = (r_out + r_in) / 2
            a = 0.0
            while a < 360:
                d.arc([(cx - rm) * s, (cy - rm) * s, (cx + rm) * s, (cy + rm) * s], a - 90, a + dash - 90,
                      fill=255, width=max(1, int((r_out - r_in) * s)))
                a += dash * 2
    return mask_of(f)


def disc_mask(cx, cy, r):
    return mask_of(lambda d, s: d.ellipse([(cx - r) * s, (cy - r) * s, (cx + r) * s, (cy + r) * s], fill=255))


def rect_mask(x, y, w, h):
    return mask_of(lambda d, s: d.rectangle([x * s, y * s, (x + w) * s - 1, (y + h) * s - 1], fill=255))


def build_theme(key):
    a, a2, lime = THEMES[key]
    C = COMMON
    img = np.zeros((240, 320, 3), np.float32)
    img[:] = rgb(C["bg"])

    # ---- panels: gradient fill + 1px frame ---------------------------------------------------
    for name, (c1, c2) in {"HEADER": (0x0A1217, 0x0A1217), "CPU": (0x0C171D, 0x071015),
                           "RAM": (0x0D181D, 0x081116), "NET": (0x0B161B, 0x081116),
                           "DISK": (0x0D181D, 0x081116)}.items():
        x, y, w, h = LAYOUT[name]
        fill = poly_fill(name)
        img[:] = img * (1 - fill[..., None]) + gradient(x, y, w, h, c1, c2) * fill[..., None]
        composite(img, poly_line(name, 0.5), C["line"])
    composite(img, poly_line("CPU", 4.5), C["inner_line"])      # inner frame of the CPU card (:after inset 4px)

    # ---- accent details: header mark with glow, panel corner ticks ---------------------------
    mark = rect_mask(18, 14, 3, 13)
    glow = np.asarray(Image.fromarray((mark * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(2.2)), np.float32) / 255
    composite(img, np.clip(glow * 0.7, 0, 1), a)
    composite(img, mark, a)
    for name in ("RAM", "NET", "DISK"):
        x, y, w, h = LAYOUT[name]
        tick = rect_mask(x, y, 9, 2) + rect_mask(x, y, 2, 9)                                   # top-left bracket
        tick += rect_mask(x + w - 9, y + h - 2, 9, 2) + rect_mask(x + w - 2, y + h - 9, 2, 9)  # bottom-right
        composite(img, np.clip(tick, 0, 1) * 0.75, a)
    composite(img, rect_mask(253, 14, 1, 13), C["line"])         # header status / clock separator

    # ---- ring backdrops (the live code draws the annulus itself) -----------------------------
    cx, cy, R, W = POINTS["CPU_RING_CX"], POINTS["CPU_RING_CY"], POINTS["CPU_RING_R"], POINTS["CPU_RING_W"]
    ri = R - W
    composite(img, disc_mask(cx, cy, ri + 0.5), C["disc"])
    composite(img, ring_mask(cx, cy, ri + 0.5, ri - 0.5), C["disc_edge"])
    composite(img, ring_mask(cx, cy, ri - 4.5, ri - 5.5, dash=7), 0x28414A)
    dx, dy, DR, DW = POINTS["DISK_RING_CX"], POINTS["DISK_RING_CY"], POINTS["DISK_RING_R"], POINTS["DISK_RING_W"]
    composite(img, disc_mask(dx, dy, DR - DW - 0.5), C["disc"])   # NVMe ring: dark centre so the caption reads

    # row hairlines in the CPU info column, NVMe column dividers, title rules
    for i in range(1, 4):
        composite(img, rect_mask(POINTS["CPU_COL_X"], POINTS["CPU_ROW_Y0"] + POINTS["CPU_ROW_DY"] * i - 8,
                                 POINTS["CPU_COL_R"] - POINTS["CPU_COL_X"], 1), C["divider"])
    for xx in (76, 156, 236):
        composite(img, rect_mask(xx, 176, 1, 38), C["divider"])
    composite(img, rect_mask(17, 171, 286, 1), C["divider"])     # NVMe title rule
    composite(img, rect_mask(159, 55, 144, 1), C["divider"])     # RAM title rule
    composite(img, rect_mask(17, 55, 124, 1), C["divider"])      # CPU title rule

    # ---- SETUP pill (bottom right): frame + cog ------------------------------------------------
    sx, sy, sw, sh = LAYOUT["SETUP"]
    pill = mask_of(lambda d, s: d.rounded_rectangle([sx * s, sy * s, (sx + sw) * s - 1, (sy + sh) * s - 1],
                                                    radius=2 * s, outline=255, width=s))
    composite(img, pill * 0.8, C["line"])
    ccx, ccy = sx + 9.0, sy + sh / 2.0
    cog = np.zeros((240, 320), np.float32)
    for yy in range(int(ccy) - 6, int(ccy) + 7):                  # 4x4 supersampled 8-tooth cog
        for xx in range(int(ccx) - 6, int(ccx) + 7):
            hits = 0
            for j in range(4):
                for i in range(4):
                    ddx, ddy = xx + (i + .5) / 4 - ccx, yy + (j + .5) / 4 - ccy
                    r, ang = math.hypot(ddx, ddy), math.atan2(ddy, ddx)
                    outer = 5.2 if math.cos(ang * 8) > 0.1 else 3.8
                    if 1.8 <= r <= outer:
                        hits += 1
            cog[yy, xx] = hits / 16
    composite(img, cog, a2)

    # ---- text (1x, real glyph shapes) ----------------------------------------------------------
    out = Image.fromarray(np.clip(img + 0.5, 0, 255).astype(np.uint8), "RGB")
    sm = small_face("font_sm")
    tit = lambda size: title_face(size)

    tit(HEAD_SIZE).draw(out, (27, 25), "SYSTEM RESEARCH", rgb(C["white"]), 1.2)
    tit(TITLE_SIZE).draw(out, (17, 52), "CPU", rgb(a2), TITLE_TRACK)
    tit(TITLE_SIZE).draw(out, (159, 51), "SYSTEM MEMORY", rgb(C["label"]), TITLE_TRACK)
    sm.draw(out, (159, 64), "PHYSICAL / IN USE", rgb(C["dim"]), 0.3)
    tit(TITLE_SIZE).draw(out, (159, 108), "NETWORK", rgb(C["label"]), TITLE_TRACK)
    sm.draw(out, (POINTS["NET_SUB_X"], 108), "/ REAL TIME", rgb(C["dim"]), 0.3)
    tit(TITLE_SIZE).draw(out, (17, 167), "NVMe SSD", rgb(a2), TITLE_TRACK)
    for i, lab in enumerate(("CLK", "PWR", "VLT", "FAN")):
        sm.draw(out, (POINTS["CPU_COL_X"], POINTS["CPU_ROW_Y0"] + POINTS["CPU_ROW_DY"] * i), lab, rgb(C["dim"]), 0.2)
    for xx, lab in ((84, "READ"), (164, "WRITE"), (244, "TEMP")):
        sm.draw(out, (xx, 187), lab, rgb(C["dim"]), 0.6)
    sm.draw(out, (11, 233), "TELEMETRY //", rgb(C["foot"]), 0.4)
    sm.draw(out, (sx + 17, sy + 10), "SETUP", rgb(C["label"]), 0.5)
    return out


def rgb565_header(img, name, src):
    raw = img.tobytes()
    px = [((raw[i] >> 3) << 11) | ((raw[i + 1] >> 2) << 5) | (raw[i + 2] >> 3) for i in range(0, len(raw), 3)]
    path = os.path.join(ROOT, "src", "assets", f"{name}.h")
    with open(path, "w") as f:
        f.write(f"// {name}: generated by tools/gen_chrome.py ({src}) - do not edit.\n")
        f.write("#pragma once\n#include <stdint.h>\n\n")
        f.write(f"static const uint16_t {name}[320 * 240] = {{\n")
        for i in range(0, len(px), 16):
            f.write("  " + ",".join(f"0x{p:04X}" for p in px[i:i + 16]) + ",\n")
        f.write("};\n")
    print(name, "->", os.path.normpath(path))


def write_layout():
    path = os.path.join(ROOT, "src", "assets", "layout.h")
    with open(path, "w") as f:
        f.write("// Screen geometry shared by the art (tools/gen_chrome.py) and the renderer - do not edit.\n")
        f.write("#pragma once\n\nstruct Rect { int x, y, w, h; };\n\n")
        for k, (x, y, w, h) in LAYOUT.items():
            f.write(f"static const Rect L_{k} = {{ {x}, {y}, {w}, {h} }};\n")
        f.write("\n")
        for k, v in POINTS.items():
            f.write(f"#define P_{k} {v}\n")
    print("layout ->", os.path.normpath(path))


def main():
    os.makedirs(os.path.join(ROOT, "src", "assets"), exist_ok=True)
    os.makedirs(os.path.join(HERE, "art"), exist_ok=True)
    for key in THEMES:
        im = build_theme(key)
        im.save(os.path.join(HERE, "art", f"bg_{key}.png"))
        rgb565_header(im, f"bg_{key}", f"theme {key}")
    write_layout()


if __name__ == "__main__":
    main()

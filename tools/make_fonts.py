#!/usr/bin/env python3
"""Regenerate every font header in src/fonts/.   python3 tools/make_fonts.py   (needs Pillow)

The design fonts are Barlow Condensed (titles / big numbers) and IBM Plex Mono
(values / labels), both SIL OFL. Put the .ttf files in tools/font_src/ (or run
`python3 tools/make_fonts.py --fetch` to download them from google/fonts first).
Without them this script falls back to the font bundled with Pillow (regular weight,
emboldened), so the project still builds - it just looks a little less like the design.

FONT_SPECS is shared with gen_chrome.py, which bakes the static labels with the same faces.
"""
import os
import sys
import urllib.request

import gen_font

HERE = os.path.dirname(os.path.abspath(__file__))
FONT_DIR = os.path.join(HERE, "font_src")
OUT_DIR = os.path.join(HERE, "..", "src", "fonts")

ASCII = ''.join(chr(c) for c in range(32, 127) if chr(c) not in "`{|}~") + "°·"
CAPS = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789%+-/.,:<>°"

# name: (ttf file, size px, characters, (fallback embolden, fallback condense))
# The fallback numbers only apply when the real .ttf is missing: Pillow's bundled face is a
# regular-weight, normal-width sans, so it is thickened and squeezed toward Barlow Condensed.
FONT_SPECS = {
    "font_big":  ("BarlowCondensed-ExtraBold.ttf", 30, "0123456789-.",      (1.0, 0.82)),
    "font_temp": ("BarlowCondensed-ExtraBold.ttf", 19, "0123456789-.°C",    (0.7, 0.82)),
    "font_val":  ("BarlowCondensed-ExtraBold.ttf", 13, "0123456789.%°-/",   (0.5, 0.85)),
    "font_ui":   ("BarlowCondensed-ExtraBold.ttf", 13, CAPS,                (0.5, 0.85)),
    "font_md":   ("IBMPlexMono-SemiBold.ttf",       9, ASCII,               (0.4, 1.0)),
    "font_sm":   ("IBMPlexMono-Medium.ttf",         7, ASCII,               (0.15, 1.0)),
}
# the static panel titles baked into the backgrounds (gen_chrome.py)
TITLE_TTF = ("BarlowCondensed-Bold.ttf", (0.6, 0.82))


def resolve(ttf, fallback):
    """(font path or 'builtin', embolden, condense to apply)"""
    p = os.path.join(FONT_DIR, ttf)
    return (p, 0.0, 1.0) if os.path.isfile(p) else ("builtin", fallback[0], fallback[1])


def fetch():
    base = "https://raw.githubusercontent.com/google/fonts/main/ofl"
    files = [("barlowcondensed", "BarlowCondensed-ExtraBold.ttf"), ("barlowcondensed", "BarlowCondensed-Bold.ttf"),
             ("ibmplexmono", "IBMPlexMono-SemiBold.ttf"), ("ibmplexmono", "IBMPlexMono-Medium.ttf"),
             ("barlowcondensed", "OFL.txt"), ("ibmplexmono", "OFL.txt")]
    os.makedirs(FONT_DIR, exist_ok=True)
    for d, f in files:
        dst = f if f != "OFL.txt" else f"OFL-{d}.txt"
        urllib.request.urlretrieve(f"{base}/{d}/{f}", os.path.join(FONT_DIR, dst))
        print("fetched", dst)


def main():
    if "--fetch" in sys.argv:
        fetch()
    os.makedirs(OUT_DIR, exist_ok=True)
    for name, (ttf, size, chars, fb) in FONT_SPECS.items():
        path, e, c = resolve(ttf, fb)
        gen_font.generate(path, size, name, chars, os.path.join(OUT_DIR, name + ".h"), e, c)


if __name__ == "__main__":
    main()

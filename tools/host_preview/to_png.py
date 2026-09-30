#!/usr/bin/env python3
"""Convert the preview's .ppm frames in out/ to 2x PNGs and an animated sim.gif (needs Pillow).
Run from tools/host_preview (run.sh does this for you)."""
import glob
import os

from PIL import Image

os.chdir(os.path.dirname(os.path.abspath(__file__)))
for p in glob.glob("out/*.ppm"):
    if "/sim_" not in p:
        Image.open(p).resize((640, 480), Image.NEAREST).save(p[:-4] + ".png")
frames = [Image.open(p).convert("RGB") for p in sorted(glob.glob("out/sim_*.ppm"))]
if frames:
    frames[0].save("out/sim.gif", save_all=True, append_images=frames[1:], duration=300, loop=0)
for p in glob.glob("out/*.ppm"):
    os.remove(p)
print("wrote out/*.png and out/sim.gif")

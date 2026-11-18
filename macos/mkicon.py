#!/usr/bin/env python3
"""Make macos/Papaya.icns from meta/icon.png (the 48x48 app icon).

It is pixel art, so every size is a whole-number nearest-neighbour scale-up
of it, centred (1024 -> 21x = 1008 px with a thin margin, 512 -> 10x, ...).
The two sizes smaller than the art (16 and 32) are smooth scale-downs.
Needs Pillow; run it again after changing the icon:

    python3 macos/mkicon.py
"""
import os
import shutil
import subprocess
import sys
import tempfile

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "meta", "icon.png")
OUT = os.path.join(HERE, "Papaya.icns")

# (pixels, iconset name)
SIZES = [(16, "icon_16x16"), (32, "icon_16x16@2x"), (32, "icon_32x32"),
         (64, "icon_32x32@2x"), (128, "icon_128x128"),
         (256, "icon_128x128@2x"), (256, "icon_256x256"),
         (512, "icon_256x256@2x"), (512, "icon_512x512"),
         (1024, "icon_512x512@2x")]


def render(art, size):
    w, h = art.size
    k = size // max(w, h)
    if k < 1:
        return art.resize((size, size), Image.LANCZOS)
    big = art.resize((w * k, h * k), Image.NEAREST)
    canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    canvas.paste(big, ((size - w * k) // 2, (size - h * k) // 2))
    return canvas


def main():
    art = Image.open(SRC).convert("RGBA")
    tmp = tempfile.mkdtemp()
    iconset = os.path.join(tmp, "Papaya.iconset")
    os.mkdir(iconset)
    try:
        for size, name in SIZES:
            render(art, size).save(os.path.join(iconset, name + ".png"))
        subprocess.check_call(["iconutil", "-c", "icns", iconset, "-o", OUT])
    finally:
        shutil.rmtree(tmp)
    print("wrote", OUT)


if __name__ == "__main__":
    sys.exit(main())

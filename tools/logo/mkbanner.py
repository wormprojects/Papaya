#!/usr/bin/env python3
"""
The HOME Menu banner (256x128) from the logo layers.

    python3 tools/logo/mkbanner.py word.png note.png meta/banner.png [overlay.png]

The word (with the optional overlay flattened onto it) and the note are
flattened, cropped to the logo, scaled down to 240 px wide and centred on
a transparent canvas. Needs Pillow and numpy.
"""

import sys

import numpy as np
from PIL import Image, ImageChops

WIDTH = 240


def main():
    if len(sys.argv) not in (4, 5):
        sys.exit(__doc__)
    word = Image.open(sys.argv[1]).convert("RGBA")
    note = Image.open(sys.argv[2]).convert("RGBA")
    if len(sys.argv) == 5:
        over = Image.open(sys.argv[4]).convert("RGBA")
        keep = word.getchannel("A")
        word = Image.alpha_composite(word, over)
        word.putalpha(keep)

    flat = Image.alpha_composite(word, note)
    flat = flat.crop(flat.getbbox())
    th = round(flat.height * WIDTH / flat.width)

    # Scale with premultiplied alpha so the edges don't pick up colour
    r, g, b, a = flat.split()
    pm = Image.merge("RGBA", tuple(ImageChops.multiply(c, a)
                                   for c in (r, g, b)) + (a,))
    r, g, b, a = pm.resize((WIDTH, th), Image.LANCZOS).split()
    alpha = np.array(a, dtype=float)
    out = np.zeros((th, WIDTH, 4), dtype=np.uint8)
    for i, c in enumerate((r, g, b)):
        v = np.array(c, dtype=float)
        out[..., i] = np.where(alpha > 0,
                               np.clip(v * 255 / np.maximum(alpha, 1), 0, 255),
                               0)
    out[..., 3] = alpha.astype(np.uint8)
    logo = Image.fromarray(out, "RGBA")

    banner = Image.new("RGBA", (256, 128), (0, 0, 0, 0))
    banner.paste(logo, ((256 - WIDTH) // 2, (128 - th) // 2), logo)
    banner.save(sys.argv[3])


main()

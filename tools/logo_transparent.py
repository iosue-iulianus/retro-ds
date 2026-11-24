#!/usr/bin/env python3
"""Make the in-app RetroDS banner from the owner's artwork.

The artwork (assets/brand/source/retrods-app-logo.jpg) is red and black ink
on a slightly noisy white. For the app's dark background:

- every white goes transparent: round the artwork and inside the two
  screens alike (the owner, Nov 24, 2026: white borders, transparent
  inside, the red D and S on the background);
- the black ink (the screens' borders) turns white;
- the red ink stays red.

Each pixel's ink coverage comes from how far it is from white, so the
anti-aliased edges blend onto any background without a white halo. Then the
picture is cropped to the artwork with a small margin.

    tools/logo_transparent.py assets/brand/source/retrods-app-logo.jpg \\
        assets/brand/retrods-logo.png

Needs Pillow and numpy.
"""
import sys

import numpy as np
from PIL import Image

src, out = sys.argv[1], sys.argv[2]
rgb = np.asarray(Image.open(src).convert("RGB")).astype(np.float64)
r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]

# the red ink: the solid colour of clearly red pixels
redish = (r - np.maximum(g, b)) > 60
solid = redish & (g < 80) & (b < 80)
red = np.median(rgb[solid], axis=0)

# coverage: red ink from how much green and blue are missing (red keeps R
# high), black ink from how dark the pixel is
low = np.minimum(g, b)
a_red = np.clip((255 - low) / (255 - min(red[1], red[2])), 0, 1)
a_black = np.clip((255 - (r + g + b) / 3) / 255, 0, 1)
is_red = redish | ((r - low) > 25)  # the red's soft edge is still reddish

alpha = np.where(is_red, a_red, a_black)
# JPEG noise: the white's (and its ringing beside the ink) is below 0.1,
# the solid ink's above 0.9:
# clamp both, and stretch what's between so the edges stay soft
alpha = np.clip((alpha - 0.1) / 0.8, 0, 1)
colour = np.where(is_red[..., None], red, 255.0)

img = np.dstack([colour, alpha * 255]).round().clip(0, 255).astype(np.uint8)
ys, xs = np.nonzero(alpha > 0.1)
m = 8
y0, y1 = max(ys.min() - m, 0), min(ys.max() + m + 1, img.shape[0])
x0, x1 = max(xs.min() - m, 0), min(xs.max() + m + 1, img.shape[1])
Image.fromarray(img[y0:y1, x0:x1], "RGBA").save(out, optimize=True)
print(f"{out}: {x1 - x0}x{y1 - y0}, red {red.round().astype(int).tolist()}")

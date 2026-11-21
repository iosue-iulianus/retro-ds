"""Make the SNES DS logo's white background transparent.

Background = near-white regions connected to the border, plus enclosed
near-white regions (the D's counter, gaps between the buttons) that are not
inside the red bar (whose white lettering stays). Anti-aliased edges get
alpha from how far they sit between white and the solid colour beside them,
so they blend onto any background without a white halo or a dark rim.

    tools/logo_transparent.py assets/brand/source/snes-ds-logo-gemini.webp \
        assets/brand/snes-ds-logo.png

Needs Pillow, numpy and scipy.
"""
import sys

import numpy as np
from PIL import Image
from scipy import ndimage

src, out = sys.argv[1], sys.argv[2]
rgb = np.asarray(Image.open(src).convert('RGB')).astype(np.float64)
h, w, _ = rgb.shape

# near white: every channel at least 225 (the lossy WebP's white varies)
white = (rgb >= 225).all(axis=2)
labels, n = ndimage.label(white)

# the red bar: rows and columns where the bar's red is dense, below the title
red = (rgb[..., 0] > 180) & (rgb[..., 1] < 120) & (rgb[..., 2] < 120)


def longest_run(row):
    best = run = 0
    for v in row:
        run = run + 1 if v else 0
        best = max(best, run)
    return best


# the bar's top and bottom edges are one unbroken red run across most of the
# width; the title's letters never are
runs = np.array([longest_run(red[y]) for y in range(h)])
rows = np.where(runs > w * 0.5)[0]
bar_y0, bar_y1 = rows.min(), rows.max()
cols = np.where(red[bar_y0])[0]
bar_x0, bar_x1 = cols.min(), cols.max()
print(f'red bar: x {bar_x0}-{bar_x1}, y {bar_y0}-{bar_y1}')

background = np.zeros((h, w), bool)
kept = 0
for i in range(1, n + 1):
    comp = labels == i
    ys, xs = np.nonzero(comp)
    touches_border = ys.min() == 0 or xs.min() == 0 or ys.max() == h - 1 or xs.max() == w - 1
    inside_bar = ys.min() >= bar_y0 and ys.max() <= bar_y1 and xs.min() >= bar_x0 and xs.max() <= bar_x1
    if touches_border or not inside_bar:
        background |= comp
    else:
        kept += 1
print(f'{n} white regions: {kept} kept white (the bar lettering), the rest background')

alpha = np.where(background, 0.0, 1.0)
# Edge band: non-background pixels within 2 px of the background or of the
# image's edge (the title touches the top). Each is a blend of white and the
# solid colour beside it (F, the nearest pixel outside the band): its alpha
# is how far it sits from white towards F, and its colour becomes F. Solid
# colour keeps alpha 1; a pink fringe becomes partly transparent red.
edge = np.zeros((h, w), bool)
edge[:2, :] = edge[-2:, :] = True
edge[:, :2] = edge[:, -2:] = True
band = (ndimage.binary_dilation(background, iterations=2) | edge) & ~background
# light leftovers outside the bar (sharp inner corners the band misses)
in_bar = np.zeros((h, w), bool)
in_bar[bar_y0:bar_y1 + 1, bar_x0:bar_x1 + 1] = True
band |= ~background & ~in_bar & (rgb.min(axis=2) > 150)
solid = ~background & ~band
_, (iy, ix) = ndimage.distance_transform_edt(~solid, return_indices=True)
F = rgb[iy, ix]  # nearest solid colour, for every pixel
W = np.array([255.0, 255.0, 255.0])
c = rgb[band] - W
f = F[band] - W
denom = np.maximum((f * f).sum(axis=1), 1.0)
a = np.clip((c * f).sum(axis=1) / denom, 0.0, 1.0)
rgb[band] = F[band]
alpha[band] = a
# tiny alphas are noise from the lossy source
alpha[alpha < 0.04] = 0.0

rgba = np.dstack([rgb, alpha * 255.0]).round().astype(np.uint8)
rgba[alpha == 0] = 0  # fully transparent pixels: no stray colour
Image.fromarray(rgba, 'RGBA').save(out, optimize=True)
print(f'wrote {out}: {w}x{h}, {(alpha == 0).mean():.0%} transparent')

#!/usr/bin/env python3
"""Make RetroDS's Android launcher icons from the owner's picture.

    tools/make_icon.py [assets/brand/source/retrods-android-icon.jpg]

The picture is used as it is, white background and all (the owner, Nov 24,
2026). Writes into android/app/src/main/res/:

- mipmap-*/ic_launcher.png: the whole picture at 48 dp (older Androids);
- mipmap-*/ic_launcher_foreground.png: the adaptive icon's foreground at
  108 dp, the artwork cropped to its ink and scaled so all of it is inside
  the 66 dp circle every launcher mask keeps, on white;
- mipmap-anydpi-v26/ic_launcher.xml and values/ic_launcher_background.xml:
  the adaptive icon, its background white.

Needs Pillow.
"""
import os
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RES = os.path.join(ROOT, "android", "app", "src", "main", "res")
DENSITIES = {"mdpi": 1, "hdpi": 1.5, "xhdpi": 2, "xxhdpi": 3, "xxxhdpi": 4}
SAFE = 66 / 108  # the safe circle's diameter, as a share of the foreground

src = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "assets", "brand", "source", "retrods-android-icon.jpg")
pic = Image.open(src).convert("RGB")

# the artwork's box: pixels clearly darker than the noisy white (253 to 255)
ink = pic.convert("L").point(lambda v: 255 if v < 200 else 0)
art = pic.crop(ink.getbbox())
# its diagonal fits the safe circle (a little inside, for the soft edges)
scale = 0.97 * SAFE / ((art.width ** 2 + art.height ** 2) ** 0.5)

for name, d in DENSITIES.items():
    folder = os.path.join(RES, "mipmap-" + name)
    os.makedirs(folder, exist_ok=True)
    legacy = round(48 * d)
    pic.resize((legacy, legacy), Image.LANCZOS).save(os.path.join(folder, "ic_launcher.png"), optimize=True)

    size = round(108 * d)
    fg = Image.new("RGB", (size, size), (255, 255, 255))
    a = art.resize((round(art.width * scale * size), round(art.height * scale * size)), Image.LANCZOS)
    fg.paste(a, ((size - a.width) // 2, (size - a.height) // 2))
    fg.save(os.path.join(folder, "ic_launcher_foreground.png"), optimize=True)
    print(f"{name}: {legacy} px, foreground {size} px (art {a.width}x{a.height})")

os.makedirs(os.path.join(RES, "mipmap-anydpi-v26"), exist_ok=True)
with open(os.path.join(RES, "mipmap-anydpi-v26", "ic_launcher.xml"), "w") as f:
    f.write('<?xml version="1.0" encoding="utf-8"?>\n'
            '<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">\n'
            '    <background android:drawable="@color/ic_launcher_background" />\n'
            '    <foreground android:drawable="@mipmap/ic_launcher_foreground" />\n'
            '</adaptive-icon>\n')
with open(os.path.join(RES, "values", "ic_launcher_background.xml"), "w") as f:
    f.write('<?xml version="1.0" encoding="utf-8"?>\n'
            '<resources>\n'
            '    <color name="ic_launcher_background">#FFFFFF</color>\n'
            '</resources>\n')

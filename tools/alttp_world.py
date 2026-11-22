#!/usr/bin/env python3
"""A Link to the Past's overworld, assembled from the game's own screens (M3).

The reference for SNES DS's world map (ALTTP-1, ALTTP-2): while Link is
outdoors, what the game screen shows of the ground is kept, in the game's
own pixels, and put together into a map of each world; the parts never seen
are covered with the clouds of the game's world map (light clouds in the
Light World, dark ones in the Dark World). Not the picture itself: the
ground layer (BG2) the game has in video RAM, drawn with the colours in its
palette buffer, so Link, enemies, the HUD and the rain aren't kept.

    alttp_world.py ROM OUT.png NAME [NAME ...] [--zoom 1|2|4] [--crop]
    alttp_world.py ROM --check NAME SHOT.png

NAME is a harness dump pair: NAME.ram (dumpram) and NAME.vram (dumpvram),
taken in that order on the same frame. OUT.png gets the world of the last
dump. --zoom shrinks by averaging 2 x 2 or 4 x 4 pixels; --crop keeps the
seen part's bounding box (to whole areas). --check compares what one dump
captures with the game's own picture of that frame (the harness's `shot`).

How it's found is in docs/games/a-link-to-the-past.md ("The overworld from
the game's screens").
"""
import argparse
import sys

from PIL import Image

WORLD = 4096          # each world is 4096 x 4096 pixels
CELL = 16             # what counts as seen: 16 x 16 pixels, whole
SKY_ROWS = 10         # the cloud strip: the world map's top 10 tile rows

# video RAM (word addresses), as the game sets the PPU for play
BG2_MAP = 0x0000      # 64 x 64 tiles: four 32 x 32 screens
BG1_MAP = 0x1000
BG12_CHR = 0x2000     # 4 bits a pixel

# the world map's art, for the clouds (see alttp_map.py)
MAP_TILES = 0x18C000
LIGHT_TILEMAP = 0x0AC727
MAP_PALETTE = 0x0ADB27


def lorom(addr):
    return ((addr >> 16) & 0x7F) * 0x8000 + (addr & 0x7FFF)


def w16(b, o):
    return b[o] | b[o + 1] << 8


def rgb(c):
    r, g, b = c & 31, (c >> 5) & 31, (c >> 10) & 31
    return (r << 3 | r >> 2, g << 3 | g >> 2, b << 3 | b >> 2)


class Dump:
    def __init__(self, name):
        self.ram = open(name + ".ram", "rb").read()
        self.vram = open(name + ".vram", "rb").read()

    def r8(self, a):
        return self.ram[a & 0x1FFFF]

    def r16(self, a):
        return w16(self.ram, a & 0x1FFFF)

    def v(self, word):
        return w16(self.vram, (word & 0x7FFF) * 2)

    def playing_outdoors(self):
        """Link outdoors, playable, the screen at full brightness."""
        return (self.r8(0x10) in (0x09, 0x0B) and self.r8(0x11) == 0
                and self.r8(0x1B) == 0 and self.r8(0x13) == 0x0F)

    def dark(self):
        return bool(self.r8(0x8A) & 0x40)

    def camera(self):
        """The world pixel at the picture's top left (BG2's scroll)."""
        return self.r16(0xE2), self.r16(0xE8)

    def palette(self):
        return [self.r16(0xC500 + 2 * i) for i in range(256)]

    def bg_pixel(self, mapbase, x, y):
        """Colour index of a background layer at layer pixel (x, y), 0 if clear."""
        x &= 511
        y &= 511
        screen = (x >> 8) + ((y >> 8) << 1)
        e = self.v(mapbase + screen * 0x400 + ((y >> 3) & 31) * 32 + ((x >> 3) & 31))
        tx, ty = x & 7, y & 7
        if e & 0x4000:
            tx = 7 - tx
        if e & 0x8000:
            ty = 7 - ty
        a = BG12_CHR + (e & 0x3FF) * 16
        p0, p1 = self.v(a + ty), self.v(a + 8 + ty)
        b = 7 - tx
        c = (p0 >> b & 1) | (p0 >> (8 + b) & 1) << 1 | (p1 >> b & 1) << 2 | (p1 >> (8 + b) & 1) << 3
        return ((e >> 10) & 7) * 16 + c if c else 0

    def capture(self):
        """The ground as the screen shows it: (x0, y0, 256 x 224 colours).

        Picture line y shows layer row camera y + y + 1 (as for Super
        Metroid's pause map)."""
        cx, cy = self.camera()
        pal = self.palette()
        bg1 = bool(self.r8(0x1C) & 1)   # BG1 on the main screen
        out = []
        for y in range(224):
            wy = cy + 1 + y
            row = []
            for x in range(256):
                wx = cx + x
                i = self.bg_pixel(BG1_MAP, wx, wy) if bg1 else 0
                if not i:
                    i = self.bg_pixel(BG2_MAP, wx, wy)
                row.append(pal[i])
            out.append(row)
        return cx, cy + 1, out


class World:
    def __init__(self):
        self.px = {}        # (x, y) -> SNES colour
        self.seen = set()   # cells (x // CELL, y // CELL)

    def add(self, x0, y0, rows):
        for y, row in enumerate(rows):
            for x, c in enumerate(row):
                if 0 <= x0 + x < WORLD and 0 <= y0 + y < WORLD:
                    self.px[(x0 + x, y0 + y)] = c
        # whole cells only
        for cy in range((y0 + CELL - 1) // CELL, (y0 + len(rows)) // CELL):
            for cx in range((x0 + CELL - 1) // CELL, (x0 + 256) // CELL):
                self.seen.add((cx, cy))


def clouds(rom, dark):
    """The world map's cloud strip (its top rows, 512 x 80), in the world's colours."""
    o = lorom(MAP_TILES)
    tiles = rom[o:o + 0x4000]
    o = lorom(LIGHT_TILEMAP)
    tmap = rom[o:o + 1024 * 2]       # top quadrants: rows 0-31 of the left, then the right
    o = lorom(MAP_PALETTE) + (256 if dark else 0)
    pal = [rgb(w16(rom, o + 2 * i)) for i in range(128)]
    im = Image.new("RGB", (512, SKY_ROWS * 8))
    p = im.load()
    for ty in range(SKY_ROWS):
        for tx in range(64):
            q, c = (tx >= 32), tx & 31
            ch = tmap[q * 1024 + ty * 32 + c]
            for y in range(8):
                for x in range(8):
                    p[tx * 8 + x, ty * 8 + y] = pal[tiles[ch * 64 + y * 8 + x]]
    return im


def draw(world, rom, dark, zoom, crop):
    if crop and world.seen:
        xs = [c[0] * CELL for c in world.seen]
        ys = [c[1] * CELL for c in world.seen]
        x0, y0 = min(xs) // 512 * 512, min(ys) // 512 * 512
        x1, y1 = (max(xs) // 512 + 1) * 512, (max(ys) // 512 + 1) * 512
    else:
        x0, y0, x1, y1 = 0, 0, WORLD, WORLD
    im = Image.new("RGB", (x1 - x0, y1 - y0))
    p = im.load()
    for (x, y), c in world.px.items():
        if x0 <= x < x1 and y0 <= y < y1 and (x // CELL, y // CELL) in world.seen:
            p[x - x0, y - y0] = rgb(c)
    if zoom > 1:
        im = im.resize((im.width // zoom, im.height // zoom), Image.BOX)
    # the clouds go on at the shown size, so they keep the game's pixels
    sky = clouds(rom, dark)
    sp = sky.load()
    p = im.load()
    s = CELL // zoom
    for y in range(im.height):
        for x in range(im.width):
            wx, wy = x0 + x * zoom, y0 + y * zoom
            if (wx // CELL, wy // CELL) not in world.seen:
                p[x, y] = sp[(x0 // zoom + x) % 512, (y0 // zoom + y) % (SKY_ROWS * 8)]
    return im


def check(dump, shot):
    x0, y0, rows = dump.capture()
    pic = Image.open(shot).convert("RGB").load()
    same = total = 0
    for y in range(40, 224):     # below the HUD
        for x in range(256):
            total += 1
            same += pic[x, y] == rgb(rows[y][x])
    print(f"camera {x0},{y0 - 1}: {same} of {total} pixels below the HUD as the game shows them"
          f" ({100 * same / total:.1f} %; the rest: Link, sprites, the HUD)")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("rom")
    ap.add_argument("args", nargs="+")
    ap.add_argument("--zoom", type=int, choices=(1, 2, 4), default=1)
    ap.add_argument("--crop", action="store_true")
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    rom = open(a.rom, "rb").read()
    if a.check:
        return check(Dump(a.args[0]), a.args[1])
    out, names = a.args[0], a.args[1:]
    worlds = {False: World(), True: World()}
    last = False
    used = 0
    for n in names:
        d = Dump(n)
        if not d.playing_outdoors():
            continue
        last = d.dark()
        worlds[last].add(*d.capture())
        used += 1
    print(f"{used} of {len(names)} dumps outdoors and playable")
    draw(worlds[last], rom, last, a.zoom, a.crop).save(out)
    return 0


if __name__ == "__main__":
    sys.exit(main())

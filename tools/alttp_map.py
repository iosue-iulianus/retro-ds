#!/usr/bin/env python3
"""A Link to the Past's world map, drawn flat from the cartridge (M3 reference).

The game's own map screen (X outdoors) is a Mode 7 picture: 64 x 64 tiles
of 8 x 8 pixels, 8 bits a pixel, shown tilted. This draws the same picture
without the tilt, as SNES DS would show it on the second screen, and covers
the parts Link hasn't seen yet. Addresses and formats are in
docs/games/a-link-to-the-past.md ("The data").

    alttp_map.py ROM OUT.png [--world light|dark] [--crop]
                 [--seen RECTS] [--ram RAM] [--fog dim|sky]
    alttp_map.py ROM --check STATE

--crop      only the world's own square (texture 128..383 both ways, 256 x
            256), not the clouds round it
--seen      world-pixel rectangles Link has seen, "x0,y0,x1,y1;..." (the
            overworld is 4096 x 4096 in each world); without --seen and
            --ram nothing is covered
--ram       a work RAM dump (the harness's dumpram): the world from $8A and
            the camera's rectangle ($E2, $E8, 256 x 224) count as seen; Link's
            place on the map ($20, $22) is printed
--fog       how unseen tiles look: dim (the map's colours at a quarter) or
            sky (the map's plain sky tile, $28)
--check     compare the Mode 7 VRAM in a save state taken with the map open
            (the harness's `save`) with the one this script builds: the
            tilemap (low bytes) and the tiles (high bytes), word for word
"""
import argparse
import sys

from PIL import Image

# SNES addresses (LoROM)
MAP_TILES = 0x18C000      # 256 tiles x 64 bytes, a byte a pixel
LIGHT_TILEMAP = 0x0AC727  # 4096 bytes: four 32 x 32 quadrants (TL, TR, BL, BR)
DARK_TILEMAP = 0x0AD727   # 1024 bytes: 32 x 32, replaces the middle (tiles 16..47)
MAP_PALETTE = 0x0ADB27     # 128 colours light, then 128 dark
SKY_TILE = 0x28

MAP_TILES_N = 64          # the picture is 64 x 64 tiles
WORLD_TO_MAP = 16         # a map pixel is 16 x 16 world pixels
WORLD_ORIGIN = 128        # world (0, 0) is map pixel (128, 128)


def lorom(addr):
    return ((addr >> 16) & 0x7F) * 0x8000 + (addr & 0x7FFF)


def read(rom, addr, n):
    o = lorom(addr)
    return rom[o:o + n]


def colour(c):
    r, g, b = c & 31, (c >> 5) & 31, (c >> 10) & 31
    return (r << 3 | r >> 2, g << 3 | g >> 2, b << 3 | b >> 2)


def tilemap(rom, dark):
    """The 64 x 64 tilemap as the game puts it in VRAM (rows of 64)."""
    light = read(rom, LIGHT_TILEMAP, 4096)
    t = [0] * (64 * 64)
    for q in range(4):
        qx, qy = (q & 1) * 32, (q >> 1) * 32
        for r in range(32):
            for c in range(32):
                t[(qy + r) * 64 + qx + c] = light[q * 1024 + r * 32 + c]
    if dark:
        d = read(rom, DARK_TILEMAP, 1024)
        for r in range(32):
            for c in range(32):
                t[(16 + r) * 64 + 16 + c] = d[r * 32 + c]
    return t


def palette(rom, dark):
    raw = read(rom, MAP_PALETTE + (256 if dark else 0), 256)
    return [colour(raw[2 * i] | raw[2 * i + 1] << 8) for i in range(128)]


def seen_tiles(rects):
    """Map tiles (64 x 64) touched by any of the world-pixel rectangles."""
    seen = set()
    for x0, y0, x1, y1 in rects:
        tx0 = (WORLD_ORIGIN + x0 // WORLD_TO_MAP) // 8
        ty0 = (WORLD_ORIGIN + y0 // WORLD_TO_MAP) // 8
        tx1 = (WORLD_ORIGIN + (x1 - 1) // WORLD_TO_MAP) // 8
        ty1 = (WORLD_ORIGIN + (y1 - 1) // WORLD_TO_MAP) // 8
        for ty in range(max(ty0, 0), min(ty1, 63) + 1):
            for tx in range(max(tx0, 0), min(tx1, 63) + 1):
                seen.add((tx, ty))
    return seen


def draw(rom, dark, seen=None, fog="dim"):
    tiles = read(rom, MAP_TILES, 0x4000)
    t = tilemap(rom, dark)
    pal = palette(rom, dark)
    dimmed = [(r // 4, g // 4, b // 4) for r, g, b in pal]
    im = Image.new("RGB", (512, 512))
    px = im.load()
    for ty in range(64):
        for tx in range(64):
            ch, p = t[ty * 64 + tx], pal
            # only the world's own square is fogged; the clouds round it stay
            inside = 16 <= tx < 48 and 16 <= ty < 48
            if seen is not None and inside and (tx, ty) not in seen:
                if fog == "sky":
                    ch = SKY_TILE
                else:
                    p = dimmed
            for y in range(8):
                for x in range(8):
                    px[tx * 8 + x, ty * 8 + y] = p[tiles[ch * 64 + y * 8 + x]]
    return im


def check(rom, state):
    tiles = read(rom, MAP_TILES, 0x4000)
    # find VRAM in the state by the tiles' bytes (the high bytes of words)
    probe = tiles[0x1000:0x1040]
    base = -1
    for i in range(len(state) - 0x8000):
        if state[i + 0x2001:i + 0x2081:2] == probe:
            base = i
            break
    if base < 0:
        print("no Mode 7 VRAM in this state (is the world map open?)")
        return 1
    vram = state[base:base + 0x8000]
    got_tiles = vram[1::2]
    got_map = vram[0::2]
    bad_tiles = sum(a != b for a, b in zip(got_tiles, tiles))
    print(f"VRAM at state offset {base:#x}: tiles {bad_tiles} bytes differ of {len(tiles)}")
    best = None
    for dark in (False, True):
        t = tilemap(rom, dark)
        bad = sum(got_map[r * 128 + c] != t[r * 64 + c] for r in range(64) for c in range(64))
        print(f"  {'dark' if dark else 'light'} world tilemap: {bad} of 4096 entries differ")
        best = bad if best is None else min(best, bad)
    return 0 if bad_tiles == 0 and best == 0 else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("rom")
    ap.add_argument("out", nargs="?")
    ap.add_argument("--world", choices=("light", "dark"), default="light")
    ap.add_argument("--crop", action="store_true")
    ap.add_argument("--seen")
    ap.add_argument("--ram")
    ap.add_argument("--fog", choices=("dim", "sky"), default="dim")
    ap.add_argument("--check")
    a = ap.parse_args()
    rom = open(a.rom, "rb").read()
    if len(rom) % 1024 == 512:
        rom = rom[512:]
    if a.check:
        return check(rom, open(a.check, "rb").read())
    if not a.out:
        ap.error("OUT.png is needed without --check")
    dark = a.world == "dark"
    rects = None
    if a.seen:
        rects = [tuple(int(v, 0) for v in r.split(",")) for r in a.seen.split(";") if r]
    if a.ram:
        ram = open(a.ram, "rb").read()
        w16 = lambda o: ram[o] | ram[o + 1] << 8
        area = ram[0x8A]
        dark = bool(area & 0x40)
        cx, cy = w16(0xE2), w16(0xE8)
        lx, ly = w16(0x22), w16(0x20)
        rects = (rects or []) + [(cx, cy, cx + 256, cy + 224)]
        print(f"area {area:#04x} ({'dark' if dark else 'light'} world), camera {cx},{cy}, "
              f"Link {lx},{ly}: map pixel {WORLD_ORIGIN + lx // WORLD_TO_MAP},"
              f"{WORLD_ORIGIN + ly // WORLD_TO_MAP}")
    im = draw(rom, dark, seen_tiles(rects) if rects is not None else None, a.fog)
    if a.crop:
        im = im.crop((128, 128, 384, 384))
    im.save(a.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())

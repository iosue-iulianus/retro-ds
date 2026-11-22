#!/usr/bin/env python3
"""A Link to the Past's dungeon map, built as the game builds it (M3, ALTTP-5).

The game's dungeon map (X in a dungeon) is three layers: BG3 (the wooden
frames and MAP, the HUD's tiles), BG2 (the floor list's backdrop and boxes,
the two floors' rooms) and BG1 (the grid), all made when the map opens
(Module0E_03_01_DrawMap, $0A:E0DC). This builds the same tilemaps from the
cartridge's tables and the game's work RAM, without opening the map.

    alttp_dungeon.py ROM --check NAME

NAME.ram / NAME.vram: a harness dump taken with the game's dungeon map open.
Compares the built tilemaps with its video RAM (BG2 and BG1 words $0000-
$1FFF, BG3 $6000-$67FF), word for word.

Addresses: docs/games/a-link-to-the-past.md ("The dungeon map's data").
"""
import argparse
import sys

FRAME_STRIPES = 0x0EDA9C      # the frames, MAP, the grid: stripes
DUNGEON_LEVEL = 0x0AE196      # a byte per dungeon: its LEVEL numeral x 2, $FF none
LEVEL_TOP, LEVEL_BOTTOM = 0x0AE15A, 0x0AE168  # stripes: header and 5 words each
NUMERALS_TOP, NUMERALS_BOTTOM = 0x0AE176, 0x0AE186
BACKDROP_POSITION = 0x0AE1E1  # 9 words: where the floor backdrop starts
GRADIENT_TILES = 0x0AEFD1     # 7 words
MOUNTAIN_STRIPES = 0x0AEFDF   # 21 words (stripes)
BOX_TILES = 0x0AE2E5          # 8 words: the floor list's boxes
FLOOR_COUNT = 0x0AF5D9        # 2 bytes a dungeon
ROOM_POINTERS = 0x0AF605      # a word a dungeon (bank $0A): 25 rooms a floor
ROOM_ORDER_POINTERS = 0x0AFBE4  # a word a dungeon: a template a room
ROOM_TEMPLATES = 0x0AF009     # 4 words a template
CORNER_TILES, CORNER_ADDR = 0x0AE429, 0x0AE431
HORIZONTAL_TILES, HORIZONTAL_ADDR = 0x0AE439, 0x0AE43D
VERTICAL_TILES, VERTICAL_ADDR = 0x0AE441, 0x0AE445
FLOOR_NUMBERS = 0x0AE4E9      # 8 words: 1 to 8
ROW_OFFSET = 0x0AE56F         # 5 words: each row of rooms in the buffer

LETTER_B, LETTER_F = 0x1F1C, 0x1F1D
EMPTY_TEMPLATE = 0x51


def lorom(addr):
    return ((addr >> 16) & 0x7F) * 0x8000 + (addr & 0x7FFF)


class Rom:
    def __init__(self, d):
        self.d = d

    def b(self, a):
        return self.d[lorom(a)]

    def w(self, a):
        o = lorom(a)
        return self.d[o] | self.d[o + 1] << 8


def stripes(vram, data_at, read):
    """The game's stripes (HandleStripes14, $00:92A1): a big-endian VRAM word
    address, then a big-endian word: bit 15 down a column (else along a
    row), bit 14 one word repeated, the rest the length in bytes less one;
    until a byte with bit 7 set."""
    p = data_at
    while not read(p) & 0x80:
        addr = read(p) << 8 | read(p + 1)
        flags = read(p + 2) << 8 | read(p + 3)
        n = (flags & 0x3FFF) + 1
        step = 32 if flags & 0x8000 else 1
        p += 4
        if flags & 0x4000:
            v = read(p) | read(p + 1) << 8
            for i in range((n + 1) // 2):
                vram[(addr + i * step) & 0x7FFF] = v
            p += 2
        else:
            for i in range(n // 2):
                vram[(addr + i * step) & 0x7FFF] = read(p + 2 * i) | read(p + 2 * i + 1) << 8
            p += n


def build(rom, ram):
    r8 = lambda a: ram[a]
    r16 = lambda a: ram[a] | ram[a + 1] << 8
    vram = {}
    # EraseTileMaps ($00:8355): BG1, BG2 with $0300; BG3 with $007F
    for i in range(0x2000):
        vram[i] = 0x0300
    for i in range(0x800):
        vram[0x6000 + i] = 0x007F
    stripes(vram, FRAME_STRIPES, rom.b)

    dung = r16(0x040C) >> 1
    t5 = rom.w(FLOOR_COUNT + dung * 2)
    up, down = (t5 >> 4) & 15, t5 & 15

    # LEVEL n for the Dark World's dungeons (Module0E_03_01_01_DrawLEVEL)
    lv = rom.b(DUNGEON_LEVEL + dung)
    if lv != 0xFF:
        i = lv >> 1
        for hdr, num in ((LEVEL_TOP, NUMERALS_TOP), (LEVEL_BOTTOM, NUMERALS_BOTTOM)):
            buf = [rom.b(hdr + k) for k in range(14)] + list(rom.w(num + i * 2).to_bytes(2, "little")) + [0xFF]
            stripes(vram, 0, lambda q: buf[q])

    # the floor list's backdrop (Module0E_03_01_02_DrawFloorsBackdrop)
    if t5 & 0x100:
        buf = [rom.b(MOUNTAIN_STRIPES + k) for k in range(42)] + [0xFF]
        stripes(vram, 0, lambda q: buf[q])
        for k in range(16):  # 8 words from each of 16 rows
            for x in range(8):
                vram[0x1123 + k * 0x20 + x] = 0x1B2E
    lo = t5 & 0xFF
    sel = ((lo >> 4) - 4) if lo >= 0x50 else (lo & 15) if (lo & 15) >= 5 else 0
    t7 = rom.w(BACKDROP_POSITION + sel * 2)
    t7_org = t7
    j = 0
    while t7 < 0x1360:
        v = rom.w(GRADIENT_TILES + j * 2) + (0x400 if t5 & 0x200 else 0)
        for x in range(8):
            vram[t7 + x] = v
        j += j != 6
        t7 += 0x20
    # the floor list's boxes (DungeonMap_BuildFloorListBoxes)
    tiles = [rom.w(BOX_TILES + k * 2) for k in range(8)]
    n = down + up
    r14 = t7_org - 0x40 + 2 + down * 0x40
    for _ in range(n):
        for k in range(4):
            vram[r14 + k] = tiles[k]
        for k in range(4):
            vram[r14 + 0x20 + k] = tiles[4 + k]
        r14 -= 0x40

    # the rooms (Module0E_03_01_03_DrawRooms) into the game's buffer at
    # $7F0000 (2048 words), then into BG2's top left and bottom left screens
    # (what the game leaves untouched in it shows as cleared: $0300)
    buf = [0x0300] * 2048
    cur = r8(0xA4)
    t = (-down) & 0xFF
    shown = (cur + 1) & 0xFF if cur == t else cur
    layout_at = 0x0A0000 | rom.w(ROOM_POINTERS + dung * 2)
    order_at = 0x0A0000 | rom.w(ROOM_ORDER_POINTERS + dung * 2)
    floors = up + down
    layout = [rom.b(layout_at + k) for k in range(floors * 25)]
    have_map = r16(0xF368) & (0x8000 >> dung)

    def put(off_bytes, pd, v):
        buf[((off_bytes + pd) & 0xFFF) >> 1] = v

    def draw_floor(floor, pd):
        mask = 0xEFFF
        # the floor's name (DungeonMap_DrawFloorNumbersByRoom)
        p = 0xDE
        while p != 0x39E:
            put(p, pd, 0x0F00)
            put(p + 2, pd, 0x0F00)
            p += 0x40
        if floor & 0x80:
            a, b = LETTER_B, rom.w(FLOOR_NUMBERS + ((~floor) & 0xFF) * 2)
        else:
            a, b = rom.w(FLOOR_NUMBERS + (floor & 15) * 2), LETTER_F
        put(0x35E, pd, a & mask)
        put(0x360, pd, b & mask)
        # the border (DungeonMap_DrawBorderForRooms)
        for i in range(4):
            put(rom.w(CORNER_ADDR + i * 2), pd, rom.w(CORNER_TILES + i * 2) & mask)
        for i in range(2):
            a0 = rom.w(HORIZONTAL_ADDR + i * 2)
            for k in range(0, 20, 2):
                put(a0 + k, pd, rom.w(HORIZONTAL_TILES + i * 2) & mask)
        for i in range(2):
            a0 = rom.w(VERTICAL_ADDR + i * 2)
            for k in range(0, 0x280, 0x40):
                put(a0 + k, pd, rom.w(VERTICAL_TILES + i * 2) & mask)
        # the rooms (DungeonMap_DrawDungeonLayout, DrawSingleRowOfRooms)
        fi = (floor + down) & 0xFF
        for row in range(5):
            x = rom.w(ROW_OFFSET + row * 2) + pd
            for col in range(5):
                v = layout[fi * 25 + row * 5 + col]
                if v == 0x0F:
                    tpl, seen = EMPTY_TEMPLATE, 0
                else:
                    seen = r16(0xF000 + v * 2) & 15
                    count = 0
                    k = 0
                    while layout[k] != v:
                        count += layout[k] != 0x0F
                        k += 1
                    tpl = rom.b(order_at + count)
                for q, (bit, off) in enumerate(((8, 0), (4, 2), (2, 0x40), (1, 0x42))):
                    w0 = rom.w(ROOM_TEMPLATES + tpl * 8 + q * 2)
                    if w0 != 0x0B00 and not (seen & bit):
                        if not (w0 & 0x1000):
                            add = 0x400
                        elif have_map:
                            put(x + off - pd, pd, (w0 & ~0x1C00) | 0x0C00)
                            continue
                        else:
                            add = 0
                    else:
                        add = 0
                    put(x + off - pd, pd, (w0 + add) if (have_map or seen & bit) else 0x0B00)
                x += 4

    draw_floor(shown, 0)
    draw_floor((shown - 1) & 0xFF, 0x300)
    for i in range(0x400):
        vram[i] = buf[i]
        vram[0x800 + i] = buf[0x400 + i]
    return vram


def check(rom, name):
    ram = open(name + ".ram", "rb").read()
    v = open(name + ".vram", "rb").read()
    built = build(rom, ram)
    bad = {}
    for a, w in built.items():
        g = v[a * 2] | v[a * 2 + 1] << 8
        if g != w:
            bad[a] = (g, w)
    print(f"{len(built) - len(bad)} of {len(built)} tilemap words as the game's ({len(bad)} differ)")
    for a in sorted(bad)[:30]:
        g, w = bad[a]
        layer = "BG3" if a >= 0x6000 else "BG1" if a >= 0x1000 else "BG2"
        base = 0x6000 if a >= 0x6000 else 0x1000 if a >= 0x1000 else 0
        i = a - base
        scr, k = divmod(i, 0x400)
        print(f"  {layer} word {a:04X} (screen {scr}, row {k // 32}, col {k % 32}): game {g:04X}, built {w:04X}")
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("rom")
    ap.add_argument("--check", required=True)
    a = ap.parse_args()
    return check(Rom(open(a.rom, "rb").read()), a.check)


if __name__ == "__main__":
    sys.exit(main())

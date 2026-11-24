#!/usr/bin/env python3
"""A Link to the Past's items screen, built as the game builds it (M3).

The reference for SNES DS's items screen (ALTTP-10). The game draws its
items screen (START) as a 32 x 32 tilemap in work RAM ($7E1000) for BG3,
with tiles that stay in video RAM for the HUD; this builds the same tilemap
from the cartridge's tables and Link's items in work RAM, pane by pane, so
the panes can be placed elsewhere (the Y items at the bottom right).

    alttp_items.py ROM --check NAME     compare with the game's own tilemap
    alttp_items.py ROM NAME OUT.png [--layout game|touch] [--ring 0|1]

NAME is a harness dump pair taken with the items screen open (NAME.ram,
NAME.vram) for --check; any dump while playing for drawing (the tiles are
the HUD's). Addresses: docs/games/a-link-to-the-past.md ("The items
screen").
"""
import argparse
import sys

from PIL import Image

MENU_BUF = 0x1000           # the game's tilemap, 32 x 32 words
BG3_CHR = 0x7000            # 2 bits a pixel
ITEM_ICON_POINTERS = 0x0DFA93   # bank $0D pointers, one per item kind
MENU_CURSOR_POSITIONS = 0x0DFAD5  # 20 words: the Y items' places in the buffer
NAME_YITEMS = 0x0DF1C9      # 16 words (two rows of 8) per item, from the bow
NAME_BOTTLES = 0x0DF449     # per bottle content, from the mushroom (1)
NAME_POWDER = 0x0DF549
NAME_FLUTE = 0x0DF569
NAME_MIRROR = 0x0DF5A9
NAME_BOW = 0x0DF5C9
PENDANTS_WINDOW = 0x0DE860  # 10 x 9 words
CRYSTALS_WINDOW = 0x0DE914
LIFT_TEXT = 0x0DF931        # 10 words each: lift 2 (gloves), lift 3 (mitts)
ABILITY_TEXT = 0x0DF959     # 10 words each: lift 1, read, talk, -, pull, run, swim, pray
HEART_PIECES = 0x0DF911     # 4 words per count of heart pieces (0 to 3)
EQUIPMENT_TEXT = 0x0DED09   # 8 words
DUNGEON_ITEM_TEXT = 0x0DED19

BLANK = 0x207F              # nothing (see-through)
FILL = 0x24F5               # a pane's dark inside

# where each pane is on the game's own screen (column, row of the 32 x 32
# map; width, height), and where SNES DS puts it for touch: the Y items at
# the bottom right, every pane moved as a whole, its inside as the game's
PANES = ("items", "name", "progress", "ability", "equipment")
GAME = {"items": (1, 5), "name": (21, 5), "progress": (21, 11),
        "ability": (1, 21), "equipment": (21, 21)}
SIZE = {"items": (19, 15), "name": (10, 6), "progress": (10, 9),
        "ability": (19, 9), "equipment": (10, 9)}
TOUCH = {"items": (12, 15), "name": (1, 24), "progress": (1, 15),
         "ability": (12, 5), "equipment": (1, 5)}


def lorom(addr):
    return ((addr >> 16) & 0x7F) * 0x8000 + (addr & 0x7FFF)


class Rom:
    def __init__(self, data):
        self.d = data

    def w(self, addr):
        o = lorom(addr)
        return self.d[o] | self.d[o + 1] << 8

    def words(self, addr, n):
        return [self.w(addr + 2 * i) for i in range(n)]


class Ram:
    def __init__(self, name):
        self.d = open(name + ".ram", "rb").read()

    def b(self, a):
        return self.d[a & 0x1FFFF]

    def w(self, a):
        return self.b(a) | self.b(a + 1) << 8


class Pane:
    """A pane drawn at the game's own place, moved by (dx, dy) when put down."""

    def __init__(self, out, name, layout):
        gx, gy = GAME[name]
        tx, ty = layout[name]
        self.out, self.dx, self.dy = out, tx - gx, ty - gy

    def put(self, col, row, word):
        self.out[(col + self.dx, row + self.dy)] = word

    def icon(self, col, row, four):
        self.put(col, row, four[0])
        self.put(col + 1, row, four[1])
        self.put(col, row + 1, four[2])
        self.put(col + 1, row + 1, four[3])

    def block(self, col, row, words, w, h):
        for y in range(h):
            for x in range(w):
                self.put(col + x, row + y, words[y * w + x])

    def box(self, c0, r0, c1, r1, pal):
        """The game's frame: corners $FB, sides $FC, top and bottom $F9."""
        p = pal << 10
        self.put(c0, r0, 0x20FB | p)
        self.put(c1, r0, 0x60FB | p)
        self.put(c0, r1, 0xA0FB | p)
        self.put(c1, r1, 0xE0FB | p)
        for r in range(r0 + 1, r1):
            self.put(c0, r, 0x20FC | p)
            self.put(c1, r, 0x60FC | p)
        for c in range(c0 + 1, c1):
            self.put(c, r0, 0x20F9 | p)
            self.put(c, r1, 0xA0F9 | p)
        for r in range(r0 + 1, r1):
            for c in range(c0 + 1, c1):
                self.put(c, r, FILL)


def icon_for(rom, ram, item):
    """The 4 tile words of Y item `item` (1 bow ... 20 mirror) as Link has it."""
    table = rom.w(ITEM_ICON_POINTERS + (item - 1) * 2) | 0x0D0000
    v = ram.b(0xF340 + item - 1)
    if item == 4:                       # bombs: any
        v = 1 if v else 0
    elif item == 16:                    # the bottle: the one selected
        i = ram.b(0xF34F)
        v = ram.b(0xF35C + i - 1) if i else 0
    return rom.words(table + v * 8, 4)


def slot_place(rom, i):
    """Column and row of Y item slot i (0..19) on the game's screen."""
    a = rom.w(MENU_CURSOR_POSITIONS + 2 * i) - MENU_BUF
    return (a // 2) % 32, (a // 2) // 32


def build(rom, ram, layout=GAME, ring=None):
    out = {}
    cur = ram.b(0x202)
    if ring is None:
        ring = bool(ram.b(0x207) & 0x10)

    # the Y items (DrawYItems, $0D:E3D9; its frame always in palette 7:
    # every caller gives GetPaletteMask 1, Link's items or not)
    p = Pane(out, "items", layout)
    p.box(1, 5, 19, 19, 7)
    p.put(2, 6, 0x3CF0)
    p.put(2, 7, 0x3CF1)
    p.put(3, 5, 0x246E)
    p.put(4, 5, 0x246F)
    for i in range(20):
        c, r = slot_place(rom, i)
        p.icon(c, r, icon_for(rom, ram, i + 1))

    # the selected item and its name (DrawEquippedYItem, $0D:EB3A)
    n = Pane(out, "name", layout)
    n.box(21, 5, 30, 10, 7)
    if cur:
        c, r = slot_place(rom, cur - 1)
        n.icon(25, 6, icon_for(rom, ram, cur))
        if ring:
            ring_around(p, c, r)
    n.block(22, 8, name_for(rom, ram, cur), 8, 2)

    # pendants or crystals (DrawProgressWindow, $0D:E9C8)
    g = Pane(out, "progress", layout)
    if ram.b(0xF3C5) < 3:
        g.block(21, 11, rom.words(PENDANTS_WINDOW, 90), 10, 9)
        have = ram.b(0xF374)
        for bit, (c, r), base in ((1, (25, 14), 0x252B), (2, (23, 17), 0x2D2B), (4, (27, 17), 0x3D2B)):
            if have & bit:
                g.icon(c, r, [base, base + 1, base + 2, base + 3])
            else:
                g.icon(c, r, [0x313B, 0x313C, 0x313D, 0x313E])
    else:
        g.block(21, 11, rom.words(CRYSTALS_WINDOW, 90), 10, 9)
        have = ram.b(0xF37A)
        for bit, (c, r) in ((1, (24, 14)), (2, (26, 14)), (4, (23, 16)), (8, (25, 16)),
                            (16, (27, 16)), (32, (24, 18)), (64, (26, 18))):
            if have & bit:
                g.put(c, r, 0x2D44)
                g.put(c + 1, r, 0x2D45)

    # DO: the abilities (DrawAbilityText, $0D:E6B6, and the icons)
    a = Pane(out, "ability", layout)
    a.box(1, 21, 19, 29, 1)
    flags = ram.b(0xF379)
    text = rom.words(ABILITY_TEXT, 80)
    for i in range(2):
        for j in range(3):
            if flags & (0x80 >> (i * 4 + j)):
                a.block(4 + j * 5, 22 + i * 2, abil(text, i * 4 + j), 5, 2)
    a.put(2, 22, 0xA4F0)
    a.put(2, 23, 0x24F2)
    a.put(3, 21, 0x2482)
    a.put(4, 21, 0x2483)
    for item, c in ((22, 4), (21, 8), (23, 12), (24, 16)):     # boots, gloves, flippers, pearl
        a.icon(c, 27, icon_for_equip(rom, ram, item))
    gloves = ram.b(0xF354)
    if gloves:
        a.block(4, 22, rom.words(LIFT_TEXT + (gloves != 1) * 20, 10), 5, 2)

    # the equipment (DrawEquipmentWindow, $0D:ED29)
    e = Pane(out, "equipment", layout)
    e.box(21, 21, 30, 29, 2)
    for i in range(8):
        e.put(22 + i, 25, 0x28D7)
    e.block(22, 22, rom.words(EQUIPMENT_TEXT, 8), 8, 1)
    e.block(22, 26, rom.words(DUNGEON_ITEM_TEXT, 8), 8, 1)
    dungeon = ram.b(0x40C)
    if dungeon == 0xFF:
        for i in range(8):
            e.put(22 + i, 26, FILL)
        e.icon(25, 27, rom.words(HEART_PIECES + ram.b(0xF36B) * 8, 4))
    sword = ram.b(0xF359)
    e.icon(22, 23, icon_for_kind(rom, 26, 0 if sword == 0xFF else sword))
    e.icon(25, 23, icon_for_kind(rom, 27, ram.b(0xF35A)))
    e.icon(28, 23, icon_for_kind(rom, 28, ram.b(0xF35B)))
    if dungeon != 0xFF:
        bit = 0x8000 >> (dungeon >> 1)
        if ram.w(0xF366) & bit:     # big key (DrawMapAndBigKey, $0D:EDDF)
            e.icon(28, 27, [0x28D6, 0x68D6, 0x28E6, 0x28E7])
        if ram.w(0xF368) & bit:     # map
            e.icon(22, 27, [0x28DE, 0x28DF, 0x28EE, 0x28EF])
        if ram.w(0xF364) & bit:     # compass (DrawCompass, $0D:F00C)
            e.icon(25, 27, [0x24BF, 0x64BF, 0x2CCF, 0x6CCF])
    return out


def icon_for_kind(rom, kind, v):
    table = rom.w(ITEM_ICON_POINTERS + (kind - 1) * 2) | 0x0D0000
    return rom.words(table + v * 8, 4)


def abil(text, n):
    """Ability n's two rows of 5 (the table holds the top rows, then the bottom ones)."""
    i, j = divmod(n, 4)
    top = text[i * 40 + j * 10: i * 40 + j * 10 + 5]
    bottom = text[i * 40 + j * 10 + 5: i * 40 + j * 10 + 10]
    return top + bottom


def icon_for_equip(rom, ram, kind):
    """Icons drawn from ItemIconPointers' later entries (21 gloves ... 24 pearl)."""
    table = rom.w(ITEM_ICON_POINTERS + (kind - 1) * 2) | 0x0D0000
    v = ram.b(0xF354 + kind - 21)
    return rom.words(table + v * 8, 4)


def name_for(rom, ram, cur):
    if cur == 0:
        return rom.words(NAME_YITEMS + 19 * 32, 16)
    if cur == 16:
        i = ram.b(0xF34F)
        content = ram.b(0xF35C + i - 1) if i else 0
        return rom.words(NAME_BOTTLES + (content - 1) * 32, 16)
    special = {5: (0xF344, NAME_POWDER), 20: (0xF353, NAME_MIRROR),
               13: (0xF34C, NAME_FLUTE), 1: (0xF340, NAME_BOW)}
    if cur in special:
        addr, table = special[cur]
        v = ram.b(addr)
        if v != 1:
            return rom.words(table + (v - 2) * 32, 16)
    return rom.words(NAME_YITEMS + (cur - 1) * 32, 16)


def ring_around(p, c, r):
    """The selected item's flashing ring (palette 7)."""
    q = 7 << 10
    p.put(c, r - 1, q | 0x2061)
    p.put(c + 1, r - 1, q | 0x6061)
    p.put(c - 1, r, q | 0x2070)
    p.put(c + 2, r, q | 0x6070)
    p.put(c - 1, r + 1, q | 0xA070)
    p.put(c + 2, r + 1, q | 0xE070)
    p.put(c, r + 2, q | 0xA061)
    p.put(c + 1, r + 2, q | 0xE061)
    p.put(c - 1, r - 1, q | 0x2060)
    p.put(c + 2, r - 1, q | 0x6060)
    p.put(c + 2, r + 2, q | 0xE060)
    p.put(c - 1, r + 2, q | 0xA060)


def check(rom, name):
    ram = Ram(name)
    got = build(rom, ram)
    bad = 0
    for (c, r), w in sorted(got.items(), key=lambda kv: (kv[0][1], kv[0][0])):
        game = ram.w(MENU_BUF + (r * 32 + c) * 2)
        if game != w:
            bad += 1
            if bad <= 40:
                print(f"  col {c:2} row {r:2}: game {game:04X}, built {w:04X}")
    print(f"{len(got) - bad} of {len(got)} words as the game's ({bad} differ)")
    return 1 if bad else 0


def render(name, words, rows=(3, 31)):
    """The tilemap drawn with the HUD's tiles and the game's palette buffer."""
    ram = Ram(name)
    vram = open(name + ".vram", "rb").read()
    v = lambda a: vram[(a & 0x7FFF) * 2] | vram[(a & 0x7FFF) * 2 + 1] << 8
    pal = [ram.w(0xC500 + 2 * i) for i in range(32)]
    rgb = lambda c: ((c & 31) << 3 | (c & 31) >> 2, (c >> 5 & 31) << 3 | (c >> 5 & 31) >> 2,
                     (c >> 10 & 31) << 3 | (c >> 10 & 31) >> 2)
    h = (rows[1] - rows[0]) * 8
    im = Image.new("RGB", (256, h))
    px = im.load()
    for (c, r), e in words.items():
        if not rows[0] <= r < rows[1] or e == BLANK:
            continue
        t, p = e & 0x3FF, (e >> 10) & 7
        for y in range(8):
            ty = 7 - y if e & 0x8000 else y
            row = v(BG3_CHR + t * 8 + ty)
            for x in range(8):
                tx = 7 - x if e & 0x4000 else x
                b = 7 - tx
                ci = (row >> b & 1) | (row >> (8 + b) & 1) << 1
                if ci:
                    px[c * 8 + x, (r - rows[0]) * 8 + y] = rgb(pal[p * 4 + ci])
    return im


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("rom")
    ap.add_argument("args", nargs="+")
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--layout", choices=("game", "touch"), default="touch")
    ap.add_argument("--ring", type=int, choices=(0, 1), default=1)
    a = ap.parse_args()
    rom = Rom(open(a.rom, "rb").read())
    if a.check:
        return check(rom, a.args[0])
    name, out = a.args
    words = build(rom, Ram(name), TOUCH if a.layout == "touch" else GAME, bool(a.ring))
    render(name, words).save(out)
    return 0


if __name__ == "__main__":
    sys.exit(main())

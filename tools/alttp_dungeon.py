#!/usr/bin/env python3
"""A Link to the Past's dungeon map, built as the game builds it (M3, ALTTP-5).

The game's dungeon map (X in a dungeon) is three layers: BG3 (the wooden
frames and MAP, the HUD's tiles), BG2 (the floor list's backdrop and boxes,
the two floors' rooms) and BG1 (the grid), all made when the map opens
(Module0E_03_01_DrawMap, $0A:E0DC). This builds the same tilemaps from the
cartridge's tables and the game's work RAM, without opening the map.

    alttp_dungeon.py ROM --check NAME [--shot SHOT.png]
    alttp_dungeon.py ROM --draw NAME OUT.png

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

# the sprites (DungeonMap_DrawSprites, $0A:EAB2)
MARKER_Y_BASE = 0x0AE7F3      # 2 words: each shown floor's top
FAIRY_ROOMS = 0x0AE7F9        # 3 words: rooms the marker shows as another
FAIRY_REPLACEMENTS = 0x0AE7FF
BOSS_ROOMS = 0x0AE807         # a word a dungeon
BLINK_PROPS = 0x0AEB48        # 4 bytes: the room dot's colours
MARKER_DX, MARKER_DY, MARKER_PROPS, MARKER_FLASH = 0x0AEB8A, 0x0AEB8E, 0x0AEB92, 0x0AEB96
FLOOR_ICON_Y = 0x0AECBE       # 8 bytes: each floor's line in the list
FLOOR_NUMBER_CHAR = 0x0AECC6  # 8 bytes: 1 to 8
BLINKER_CHAR = 0x0AED50       # 4 bytes
BOSS_POSITION = 0x0AEE5D      # a word a dungeon: the skull's place in its room
BOSS_FLOOR = 0x0AEE79         # a word a dungeon ($FFFF: none)
LINK_HEAD = 0x10A9C0          # Link's head, 16 x 16 (4 bits a pixel, a 16-tile row)

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


def sprites(rom, ram):
    """The sprites as DungeonMap_DrawSprites places them: {OAM index: (x, y,
    tile, attributes, big)}. Link's head is tile 0; the rest are the second
    name table's (the map's sprite set)."""
    r8 = lambda a: ram[a]
    r16 = lambda a: ram[a] | ram[a + 1] << 8
    s8 = lambda v: v - 256 if v >= 128 else v
    frame = r8(0x1A)
    dung = r16(0x040C) >> 1
    t5 = rom.w(FLOOR_COUNT + dung * 2) & 0xFF
    up, down = t5 >> 4, t5 & 15
    cur = r8(0xA4)
    oam = {}

    def fix(r):
        # where the floor list puts floor index r (from the lowest)
        if 4 - down >= 0:
            r += 4 - down
            a = up - 4
            if a >= 0:
                r -= a
        return r

    # Link pointing at his floor (DungeonMap_DrawLinkPointing)
    r3 = fix((down + cur) & 0xFF)
    oam[0] = (0x19, rom.b(FLOOR_ICON_Y + r3) - 4, 0x00, 0x3E, True)

    # where Link is on the map (DungeonMap_DrawRoomMarkers, $0A:E823)
    idx = 2 if cur == ((-down) & 0xFF) else 0
    shown = (cur + 1) & 0xFF if idx else cur
    room = r16(0xA0)
    for i in range(3):
        if room == rom.w(FAIRY_ROOMS + i * 2):
            room = rom.w(FAIRY_REPLACEMENTS + i * 2)
    layout_at = 0x0A0000 | rom.w(ROOM_POINTERS + dung * 2)
    floor1 = (down + cur) & 0xFF
    cells = [rom.b(layout_at + floor1 * 25 + k) for k in range(25)]
    xc = yc = 0
    for k in range(25):
        if cells[k] == (room & 0xFF):
            break
        if xc < 64:
            xc += 16
        else:
            xc, yc = 0, yc + 16
    var3 = (xc + 0x90 + ((r16(0x22) & 0x1E0) >> 5)) & 0xFF
    var6 = yc
    var5 = (yc + rom.w(MARKER_Y_BASE + idx) + ((r16(0x20) & 0x1E0) >> 5)) & 0xFFFF
    # the boss's room on its floor
    boss_floor = rom.w(BOSS_FLOOR + dung * 2)
    floor2 = (down + boss_floor) & 0xFF
    cells2 = [rom.b(layout_at + floor2 * 25 + k) for k in range(25)]
    var7, var8 = 0x40, 0x40
    look = rom.w(BOSS_ROOMS + dung * 2) & 0xFF
    for j in range(24, -1, -1):
        if cells2[j] != 0x0F and cells2[j] == look:
            break
        var7 -= 0x10
        if var7 < 0:
            var7 = 0x40
            var8 = (var8 - 0x10) & 0xFF
    floor3 = s8((shown - (boss_floor & 0xFF)) & 0xFF)
    # (16 bits: a boss floor that isn't shown puts it at 256 and over: hidden)
    var8 = (var8 + 0x60 * floor3 + rom.w(MARKER_Y_BASE)) & 0xFFFF

    # the brackets round Link's room on each shown floor (DrawLocationMarker)
    pos = 1
    for r14 in range(2):
        for i in range(3, -1, -1):
            r15 = (var6 + rom.w(MARKER_Y_BASE + r14 * 2)) & 0xFF
            fr = (frame >> 2) & 1
            if ((var5 + 1) & 0xF0) == r15 + 1 and var5 < 256:
                fr += 2
            x = (s8(rom.b(MARKER_DX + i)) + (var3 & 0xF0)) & 0xFF
            y = (r15 + s8(rom.b(MARKER_DY + i))) & 0xFF
            oam[pos] = (x, y, 0x00, rom.b(MARKER_FLASH + fr) | rom.b(MARKER_PROPS + i), True)
            pos += 1
    # the blinking dot (DungeonMap_DrawBlinkingIndicator)
    oam[pos] = ((var3 - 3) & 0xFF, ((var5 if var5 < 256 else 0xF0) - 3) & 0xFF, 0x34, rom.b(BLINK_PROPS + ((frame >> 2) & 3)), False)
    pos += 1
    # the boss, with the compass, until beaten (DungeonMap_DrawBossIcon)
    # ($FFFF: no boss; floors below the ground are $00FF, $00FE...)
    if boss_floor != 0xFFFF and not (r16(0xF000 + look * 2) & 0x800) and r16(0xF364) & (0x8000 >> dung):
        if (frame & 15) < 10:
            r3b = fix((down + boss_floor) & 0xFF)
            oam[pos] = (0x4C, rom.b(FLOOR_ICON_Y + r3b), 0x31, 0x33, False)
            pos += 1
            xy = rom.w(BOSS_POSITION + dung * 2)
            oam[pos] = (((xy >> 8) + var7 + 0x90) & 0xFF, ((xy & 0xFF) + var8) & 0xFF if var8 < 256 else 0xF0, 0x31, 0x33, False)
            pos += 1
    # each floor's number in the list (DungeonMap_DrawFloorNumberObjects)
    yv = 7
    if up + down != 8 and up < 4:
        yv = 6
        i = 3
        while i != 0 and i != up:
            yv -= 1
            i -= 1
        if down >= 5:
            i = 5
            while i != down and down != 8:
                yv += 1
                i += 1
    r4 = (rom.b(FLOOR_ICON_Y + yv) + 1) & 0xFF
    r2 = (up - 1) & 0xFF
    r3n = (-down) & 0xFF
    while True:
        a = 0x1C if r2 & 0x80 else rom.b(FLOOR_NUMBER_CHAR + r2)
        b = rom.b(FLOOR_NUMBER_CHAR + (r2 ^ 0xFF)) if r2 & 0x80 else 0x1D
        oam[pos] = (0x30, r4, a, 0x3D, False)
        oam[pos + 1] = (0x38, r4, b, 0x3D, False)
        r4 = (r4 + 16) & 0xFF
        pos += 2
        done = r2 == r3n
        r2 = (r2 - 1) & 0xFF
        if done:
            break
    # the shown floors lit in the list, every other 16 frames
    # (DungeonMap_DrawFloorBlinker, sprites $40 and up)
    floor = shown
    flag = 1 if up + down != 1 else 0
    floor = (floor - flag) & 0xFF
    r0 = 0
    for _ in range(flag + 1):
        r0 = fix((floor + down) & 0xFF)
        floor = (floor + 1) & 0xFF
    if frame & 0x10:
        y0 = (rom.b(FLOOR_ICON_Y + r0) - 4) & 0xFF
        for f in range(flag, -1, -1):
            x = 40
            p = 0x40 + (0, 8)[f]
            for i in range(3, -1, -1):
                at = 0x3D | (0 if i else 0x40)
                ch = rom.b(BLINKER_CHAR + i)
                oam[p] = (x, (y0 + f * 16) & 0xFF, ch, at, False)
                oam[p + 4] = (x, (y0 + f * 16 + 8) & 0xFF, ch, at | 0x80, False)
                x += 8
                p += 1
    return oam


HUD_PALETTE = 0x1BD6A0        # rows 0-1: the HUD's second palette
MAP_BG_PALETTE = 0x1BE544     # rows 2-7
MAP_SPRITE_PALETTE = 0x1BD70A  # rows 12-14, colours 1-7


def palette(rom, ram):
    """The colours while the map is up: the map's own (Palette_Load_HUD,
    _DungeonMapBG, _DungeonMapSprite), the rest the game's as it was."""
    pal = [ram[0xC500 + 2 * i] | ram[0xC501 + 2 * i] << 8 for i in range(256)]
    for i in range(32):
        pal[i] = rom.w(HUD_PALETTE + i * 2)
    for i in range(96):
        pal[32 + i] = rom.w(MAP_BG_PALETTE + i * 2)
    for r in range(3):
        for c in range(7):
            pal[(12 + r) * 16 + 1 + c] = rom.w(MAP_SPRITE_PALETTE + (r * 7 + c) * 2)
    return pal


def render(rom, ram, vram_tiles, built, oam):
    """The picture as the PPU makes it in mode 1 with BG3 on top (BGMODE
    $09): main screen BG2, BG3 and sprites; BG1 (the grid) on the sub
    screen, added where the main screen shows the backdrop (CGWSEL $02,
    CGADSUB $20). vram_tiles(word) gives video RAM's tiles."""
    pal = palette(rom, ram)
    rgb = lambda c: ((c & 31) << 3 | (c & 31) >> 2, (c >> 5 & 31) << 3 | (c >> 5 & 31) >> 2, (c >> 10 & 31) << 3 | (c >> 10 & 31) >> 2)

    def bg(mapbase, chrbase, bpp, x, y):
        y = (y + 1) & 511  # (line 0 shows row 1)
        x &= 511
        scr = (x >> 8) + ((y >> 8) << 1)
        e = built.get(mapbase + scr * 0x400 + ((y >> 3) & 31) * 32 + ((x >> 3) & 31), 0)
        tx, ty = x & 7, y & 7
        if e & 0x4000:
            tx = 7 - tx
        if e & 0x8000:
            ty = 7 - ty
        b = 7 - tx
        if bpp == 4:
            a = chrbase + (e & 0x3FF) * 16
            p0, p1 = vram_tiles(a + ty), vram_tiles(a + 8 + ty)
            c = (p0 >> b & 1) | (p0 >> (8 + b) & 1) << 1 | (p1 >> b & 1) << 2 | (p1 >> (8 + b) & 1) << 3
            return (((e >> 10) & 7) * 16 + c if c else 0), (e >> 13) & 1
        p0 = vram_tiles(chrbase + (e & 0x3FF) * 8 + ty)
        c = (p0 >> b & 1) | (p0 >> (8 + b) & 1) << 1
        return (((e >> 10) & 7) * 4 + c if c else 0), (e >> 13) & 1

    # sprites into a line buffer: colour and priority, the first in OAM on top
    spr = {}
    for i in sorted(oam, reverse=True):
        x, y, t, at, big = oam[i]
        size = 16 if big else 8
        base = 0x5000 if at & 1 else 0x4000
        for yy in range(size):
            for xx in range(size):
                sx, sy = (x + xx) & 0x1FF, (y + yy) & 0xFF  # (picture line Y: a line below BG row Y)
                if sx >= 256 or sy >= 224:
                    continue
                tx = size - 1 - xx if at & 0x40 else xx
                ty = size - 1 - yy if at & 0x80 else yy
                tile = (t + (tx >> 3) + (ty >> 3) * 16) & 0xFF
                a = base + tile * 16
                p0, p1 = vram_tiles(a + (ty & 7)), vram_tiles(a + 8 + (ty & 7))
                b = 7 - (tx & 7)
                c = (p0 >> b & 1) | (p0 >> (8 + b) & 1) << 1 | (p1 >> b & 1) << 2 | (p1 >> (8 + b) & 1) << 3
                if c:
                    spr[(sx, sy)] = (128 + ((at >> 1) & 7) * 16 + c, (at >> 4) & 3)
    from PIL import Image
    im = Image.new("RGB", (256, 224))
    px = im.load()
    for y in range(224):
        for x in range(256):
            b2, p2 = bg(0x0000, 0x2000, 4, x, y)
            b3, p3 = bg(0x6000, 0x7000, 2, x, y)
            s = spr.get((x, y))
            # mode 1, BG3 priority: BG3 hi, OBJ 3, BG2 hi, OBJ 2, BG2 lo, OBJ 1, BG3 lo, OBJ 0
            order = [(b3, p3 == 1), (s and s[0], s and s[1] == 3), (b2, p2 == 1), (s and s[0], s and s[1] == 2),
                     (b2, p2 == 0), (s and s[0], s and s[1] == 1), (b3, p3 == 0), (s and s[0], s and s[1] == 0)]
            c = next((i for i, ok in order if i and ok), None)
            if c is None:
                b1, _ = bg(0x1000, 0x2000, 4, x, y)
                back, sub = pal[0], pal[b1] if b1 else 0
                c0 = tuple(min(31, (back >> k & 31) + (sub >> k & 31)) for k in (0, 5, 10))
                px[x, y] = rgb(c0[0] | c0[1] << 5 | c0[2] << 10)
            else:
                px[x, y] = rgb(pal[c])
    return im


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
    oam = sprites(rom, ram)
    obad = 0
    for i, (x, y, t, at, big) in oam.items():
        g = ram[0x800 + i * 4:0x804 + i * 4]
        hi = (ram[0xA00 + i // 4] >> ((i % 4) * 2)) & 3
        gx = g[0] | (hi & 1) << 8
        if (gx & 0xFF, g[1], g[2], g[3], bool(hi & 2)) != (x, y, t, at, big):
            obad += 1
            print(f"  sprite {i}: game x {gx} y {g[1]} tile {g[2]:02X} attr {g[3]:02X} big {bool(hi & 2)};"
                  f" built x {x} y {y} tile {t:02X} attr {at:02X} big {big}")
    print(f"{len(oam) - obad} of {len(oam)} sprites as the game's")
    if obad:
        return 1
    for a in sorted(bad)[:30]:
        g, w = bad[a]
        layer = "BG3" if a >= 0x6000 else "BG1" if a >= 0x1000 else "BG2"
        base = 0x6000 if a >= 0x6000 else 0x1000 if a >= 0x1000 else 0
        i = a - base
        scr, k = divmod(i, 0x400)
        print(f"  {layer} word {a:04X} (screen {scr}, row {k // 32}, col {k % 32}): game {g:04X}, built {w:04X}")
    return 1 if bad else 0


def compare_shot(rom, name, shot):
    """The picture drawn from what was built (the tiles from the dump's video
    RAM: alttp_gfx.py checks those) against the game's own picture. The
    game's picture shows the sprites of the frame before (OAM is copied in
    the next vertical blank): give a shot taken one frame after the dump."""
    from PIL import Image
    ram = open(name + ".ram", "rb").read()
    v = open(name + ".vram", "rb").read()
    tiles = lambda w: v[(w & 0x7FFF) * 2] | v[(w & 0x7FFF) * 2 + 1] << 8
    im = render(Rom(rom.d), ram, tiles, build(rom, ram), sprites(rom, ram))
    pic = Image.open(shot).convert("RGB")
    a, b = im.load(), pic.load()
    bad = [(x, y) for y in range(224) for x in range(256) if a[x, y] != b[x, y]]
    print(f"{256 * 224 - len(bad)} of {256 * 224} pixels as the game's picture")
    if bad:
        xs, ys = [p[0] for p in bad], [p[1] for p in bad]
        print(f"  differing within x {min(xs)}-{max(xs)}, y {min(ys)}-{max(ys)}")
    return im, 1 if bad else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("rom")
    ap.add_argument("--check")
    ap.add_argument("--shot")
    ap.add_argument("--draw", nargs=2)
    a = ap.parse_args()
    rom = Rom(open(a.rom, "rb").read())
    if a.draw:
        name, out = a.draw
        ram = open(name + ".ram", "rb").read()
        v = open(name + ".vram", "rb").read()
        tiles = lambda w: v[(w & 0x7FFF) * 2] | v[(w & 0x7FFF) * 2 + 1] << 8
        render(rom, ram, tiles, build(rom, ram), sprites(rom, ram)).save(out)
        return 0
    r = check(rom, a.check)
    if a.shot:
        r |= compare_shot(rom, a.check, a.shot)[1]
    return r


if __name__ == "__main__":
    sys.exit(main())

# Super Metroid's pause map drawn from the ROM and a RAM dump, as the
# game draws it: the map layer (BG1, as LoadPauseMenuMapTilemap at
# $82:943D builds it) and the sprites on it (Samus's marker, the map
# icons, the gunship, the elevator labels). The reference the native
# renderer (SM-8) is checked against.
#
#   tools/sm_map.py ROM.sfc RAM.bin OUT.png [--marker N] [--check SHOT.png]
#
# ROM: the game as played (no copier header); RAM: 128 KB of work RAM
# (the harness's dumpram). OUT is the whole 512 x 256 layer with its
# sprites, in map pixels. --marker picks the marker's frame (0 to 3, the
# game's $0776 / 2; default: the one in RAM). --check compares OUT with
# a 256 x 224 shot of the game's pause map taken with that RAM, at the
# view in RAM ($B1, $B3), with the marker as the picture has it, and
# writes OUT's name with -diff.png.
#
# Addresses from P.JBoy's bank logs, as in
# github.com/insanefirebat/sm_disassembly. Checked Nov 21, 2026 against
# the pause map stitched from the game's own screen (Crateria, after the
# map station): every pixel of the layer matches. The SNES shows the
# layer's row (view y + 1) on the picture's first line, and a sprite
# from the line below its Y: so on the picture, map row y is line
# y - view y - 1, and a sprite placed at map y is drawn one row lower
# in map pixels (y + 1) than the game's numbers say.
import sys, argparse
from PIL import Image


def lorom(a): return ((a >> 16) & 0x7F) * 0x8000 + (a & 0x7FFF)


class Rom:
    def __init__(self, data): self.d = data
    def r8(self, a): return self.d[lorom(a)]
    def r16(self, a): o = lorom(a); return self.d[o] | self.d[o + 1] << 8
    def r24(self, a): o = lorom(a); return self.d[o] | self.d[o + 1] << 8 | self.d[o + 2] << 16
    def block(self, a, n): o = lorom(a); return self.d[o:o + n]


class Ram:
    def __init__(self, data): self.d = data
    def r8(self, a): return self.d[a & 0x1FFFF]
    def r16(self, a): a &= 0x1FFFF; return self.d[a] | self.d[a + 1] << 8


def palette(rom):
    """The pause screen's 256 colours ($B6:F000), BGR555 to RGB."""
    pal = []
    for i in range(256):
        c = rom.r16(0xB6F000 + i * 2)
        pal.append(tuple((v << 3) | (v >> 2) for v in (c & 31, (c >> 5) & 31, (c >> 10) & 31)))
    return pal


def tile4(tiles, n):
    """An 8 x 8 tile, 4 bits a pixel, as rows of colour numbers."""
    t = tiles[n * 32:(n + 1) * 32]
    px = [[0] * 8 for _ in range(8)]
    for y in range(8):
        p0, p1, p2, p3 = t[y * 2], t[y * 2 + 1], t[16 + y * 2], t[16 + y * 2 + 1]
        for x in range(8):
            b = 7 - x
            px[y][x] = ((p0 >> b) & 1) | ((p1 >> b) & 1) << 1 | ((p2 >> b) & 1) << 2 | ((p3 >> b) & 1) << 3
    return px


def area_of(ram):
    area = ram.r16(0x079F)
    return 0 if area >= 7 else area


def explored_bit(ram, i):
    """Map tile i (0..2047, left screen then right) explored? ($07F7)"""
    return (ram.r8(0x07F7 + (i >> 3)) >> (7 - (i & 7))) & 1


def layer_words(rom, ram):
    """The 2048 tilemap words of the map layer, as the game builds them."""
    area = area_of(ram)
    tm_ptr = rom.r24(0x82964A + area * 3)
    md_ptr = 0x820000 | rom.r16(0x829717 + area * 2)
    station = ram.r8(0xD908 + ram.r16(0x079F))
    words = []
    for i in range(2048):
        src = rom.r16(tm_ptr + i * 2)
        md = (rom.r8(md_ptr + (i >> 3)) >> (7 - (i & 7))) & 1 if station else 0
        words.append(src & 0xFBFF if explored_bit(ram, i) else (src if md else 0x001F))
    return words


def draw_layer(img, rom, ram, pal):
    P = img.load()
    tiles = rom.block(0xB68000, 0x4000)
    for i, w in enumerate(layer_words(rom, ram)):
        scr, j = divmod(i, 1024)
        cx, cy = scr * 32 + j % 32, j // 32
        t = tile4(tiles, w & 0x3FF); p = (w >> 10) & 7
        hf, vf = w & 0x4000, w & 0x8000
        for y in range(8):
            for x in range(8):
                v = t[7 - y if vf else y][7 - x if hf else x]
                if v:
                    P[cx * 8 + x, cy * 8 + y] = pal[p * 16 + v] + (255,)


# --- Sprites ---------------------------------------------------------------
# Menu spritemaps: pointer table $82:C569 (bank $82). Each: a count word,
# then 5-byte entries: word s000000xxxxxxxxx (s: 16 x 16, x: 9-bit signed),
# a signed Y byte, word YXppPPPttttttttt. AddSpritemapFrom_82C569_TableToOAM
# ($81:891F) replaces the entry's palette (PPP) with the caller's ($03).
# The pause screen's OBSEL is $01: 8 x 8 and 16 x 16 sprites, tiles from
# VRAM $4000, where $B6:C000 is loaded; sprite colours are 128 and up.

M_SAVE, M_BOSS, M_ENERGY, M_MISSILE = 0x08, 0x09, 0x0A, 0x0B
M_MAPSTATION, M_CROSS, M_GUNSHIP = 0x4E, 0x62, 0x63
MARKER_FRAMES = (0x5F, 0x60, 0x61, 0x60)   # $82:BA2D, 8, 4, 8, 4 frames
MARKER_DELAYS = (8, 4, 8, 4)               # $82:BA25


def marker_on_screen(ram):
    """The marker's frame on the picture of the frame just run. Sprites
    reach the screen a frame after the game places them (OAM is copied
    in the next vertical blank), so when the frame in RAM ($0776) has
    just started (its timer, $0778, one below its delay), the picture
    still has the one before."""
    f = (ram.r16(0x0776) >> 1) & 3
    return (f - 1) & 3 if ram.r16(0x0778) == MARKER_DELAYS[f] - 1 else f


def spritemap(rom, sid):
    """The entries of menu spritemap sid: (dx, dy, big, attr)."""
    a = 0x820000 | rom.r16(0x82C569 + sid * 2)
    n = rom.r16(a)
    out = []
    for k in range(n):
        e = a + 2 + k * 5
        w = rom.r16(e)
        dx = w & 0x1FF
        if dx & 0x100: dx -= 0x200
        dy = rom.r8(e + 2)
        if dy & 0x80: dy -= 0x100
        out.append((dx, dy, bool(w & 0x8000), rom.r16(e + 3)))
    return out


def map_sprites(rom, ram, marker=None):
    """The map screen's sprites, in OAM order (the first is on top), as
    PauseMenu_0_MapScreen ($82:9120) adds them: (x, y, spritemap ID,
    palette bits), x and y in map pixels. The scroll arrows and the
    L / R / START highlights are left out (SNES DS draws its own frame)."""
    area = area_of(ram)
    out = []
    # Samus's position ($82:B9C8).
    if marker is None:
        marker = (ram.r16(0x0776) >> 1) & 3
    x = ((ram.r16(0x0AF6) >> 8) + ram.r16(0x07A1)) * 8
    y = ((ram.r16(0x0AFA) >> 8) + ram.r16(0x07A3) + 1) * 8
    out.append((x & 0xFFFF, y & 0xFFFF, MARKER_FRAMES[marker], 0x0E00))

    def explored_at(x, y):
        # Check_if_MapPosition_isExplored ($82:B855): x, y in map pixels.
        i = ((x & 0xFF00) >> 1) + ((x & 0xFF) >> 6) + ((y & 0xFFF8) >> 1)
        return (ram.r8(0x07F7 + i) >> (7 - ((x >> 3) & 7))) & 1

    def table(ptrs):
        p = rom.r16(ptrs + area * 2)
        return 0x820000 | p if p else 0

    # Bosses ($82:B892): killed ones crossed out, the others shown while
    # the area's map is collected ($0789).
    t = table(0x82C7CB)
    if t:
        bosses = ram.r8(0xD828 + area)
        while rom.r16(t) != 0xFFFF:
            bx, by = rom.r16(t), rom.r16(t + 2)
            dead = bosses & 1
            bosses >>= 1
            if bx != 0xFFFE:
                if dead:
                    out.append((bx, by, M_CROSS, 0x0E00))
                    out.append((bx, by, M_BOSS, 0x0C00))
                elif ram.r16(0x0789):
                    out.append((bx, by, M_BOSS, 0x0E00))
            t += 4

    def icons(ptrs, sid, pal, mask):
        # Draw_MapIcons_ofGivenType ($82:B81C): an entry per bit of mask,
        # drawn only where its map square is explored.
        t = table(ptrs)
        if not t: return
        while not rom.r16(t) & 0x8000:
            if mask & 1:
                ix, iy = rom.r16(t), rom.r16(t + 2)
                if explored_at(ix, iy):
                    out.append((ix, iy, sid, pal))
            mask >>= 1
            t += 4

    icons(0x82C7DB, M_MISSILE, 0x0E00, 0xFFFF)
    icons(0x82C7EB, M_ENERGY, 0x0E00, 0xFFFF)
    icons(0x82C7FB, M_MAPSTATION, 0x0E00, 0xFFFF)
    # Save stations ($82:B798): only in the area last saved in, and only
    # the station the game was last loaded from, if it's been used.
    if area == ram.r16(0xD918):
        used = ram.r16(0xD8F8 + area * 2)
        bit = rom.r8(0x82B7C9 + ram.r16(0x078B))
        icons(0x82C80B, M_SAVE, 0x0400, (used & 0xFF00) | (used & bit & 0xFF))
    # The gunship, in Crateria, at its save station's position.
    if area == 0:
        out.append((rom.r16(0x82C853), rom.r16(0x82C855), M_GUNSHIP, 0x0E00))
    # Elevator destinations ($82:BB30), with the area's map station.
    if ram.r8(0xD908 + area):
        t = 0x820000 | rom.r16(0x82C74D + area * 2)
        while rom.r16(t) != 0xFFFF:
            out.append((rom.r16(t), rom.r16(t + 2), rom.r16(t + 4), 0))
            t += 6
    return out


def s16(v): return v - 0x10000 if v & 0x8000 else v


def draw_sprites(img, rom, sprites, pal, ox=0, oy=0):
    """Draws sprites (map pixels) onto img, the last one first so the
    first is on top; (ox, oy) is the map pixel at img's top left."""
    P = img.load()
    tiles = rom.block(0xB6C000, 0x2000)
    W, H = img.size
    for x0, y0, sid, palbits in reversed(sprites):
        p = 8 + ((palbits >> 9) & 7)
        for dx, dy, big, attr in reversed(spritemap(rom, sid)):
            n = attr & 0x1FF
            hf, vf = attr & 0x4000, attr & 0x8000
            size = 16 if big else 8
            for y in range(size):
                for x in range(size):
                    sx, sy = (size - 1 - x if hf else x), (size - 1 - y if vf else y)
                    # 16 x 16: tiles n, n+1 above n+16, n+17 (a row is 16 tiles).
                    tn = (n & ~0xF) | ((n + (sx >> 3)) & 0xF)
                    tn = (tn + (sy >> 3) * 16) & 0xFF
                    v = tile4(tiles, tn)[sy & 7][sx & 7]
                    if not v: continue
                    px = s16(x0) + dx + x - ox
                    py = s16(y0) + dy + y + 1 - oy
                    if 0 <= px < W and 0 <= py < H:
                        P[px, py] = pal[p * 16 + v] + (255,)


def render(rom, ram, marker=None):
    pal = palette(rom)
    img = Image.new('RGBA', (512, 256), (0, 0, 0, 0))
    draw_layer(img, rom, ram, pal)
    draw_sprites(img, rom, map_sprites(rom, ram, marker), pal)
    return img


def check(img, ram, shot_path, out_path):
    """Compares img (map pixels) with the game's own screen: the view is
    ($B1, $B3); map pixel (x, y) is on screen at (x - $B1, y - $B3 - 1).
    Only the map's window is compared (inside the frame, clear of the
    scroll arrows). The grid behind the map (another layer, colour
    42008C) counts as empty. Differences are drawn red."""
    shot = Image.open(shot_path).convert('RGB')
    vx, vy = s16(ram.r16(0x00B1)), s16(ram.r16(0x00B3))
    diff = shot.copy()
    S, I, D = shot.load(), img.load(), diff.load()
    bad = total = 0
    for sy in range(60, 176):
        for sx in range(30, 226):
            mx, my = (sx + vx) & 511, (sy + 1 + vy) & 255
            r, g, b, a = I[mx, my]
            want = (r, g, b) if a else (0, 0, 0)
            total += 1
            got = S[sx, sy]
            if got == (0x42, 0x00, 0x8C): got = (0, 0, 0)
            if got != want:
                bad += 1
                D[sx, sy] = (255, 0, 0)
    diff.save(out_path)
    print('check: %d of %d pixels differ (view %d, %d)' % (bad, total, vx, vy))
    return bad


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('rom'); ap.add_argument('ram'); ap.add_argument('out')
    ap.add_argument('--marker', type=int)
    ap.add_argument('--check')
    a = ap.parse_args()
    rom = Rom(open(a.rom, 'rb').read())
    ram = Ram(open(a.ram, 'rb').read())
    img = render(rom, ram, a.marker)
    img.save(a.out)
    print('area', area_of(ram), 'sprites', len(map_sprites(rom, ram, a.marker)))
    if a.check:
        if a.marker is None:
            img = render(rom, ram, marker_on_screen(ram))
        sys.exit(1 if check(img, ram, a.check, a.out.rsplit('.', 1)[0] + '-diff.png') else 0)


if __name__ == '__main__':
    main()

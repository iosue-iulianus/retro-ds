# Super Metroid's pause map (its BG1 layer) drawn from the ROM and a RAM
# dump, as the game's LoadPauseMenuMapTilemap ($82:943D) builds it: the
# reference the native renderer (SM-8) is checked against.
#
#   tools/sm_map.py ROM.sfc RAM.bin OUT.png
#
# ROM: the game as played (no copier header); RAM: 128 KB of work RAM
# (the harness's dumpram). Addresses from P.JBoy's bank logs, as in
# github.com/insanefirebat/sm_disassembly. Checked Nov 21, 2026 against
# the pause map stitched from the game's own screen (Crateria, after the
# map station): every pixel of the layer matches, one row down (the SNES
# shows BG row 0 on screen line 1).
import sys, struct
from PIL import Image
rom = open(sys.argv[1], 'rb').read()
ram = open(sys.argv[2], 'rb').read()
out = sys.argv[3]
def lorom(a): return ((a >> 16) & 0x7F) * 0x8000 + (a & 0x7FFF)
def r8(a): return rom[lorom(a)]
def r16(a): o = lorom(a); return rom[o] | rom[o+1] << 8
def r24(a): o = lorom(a); return rom[o] | rom[o+1] << 8 | rom[o+2] << 16
def w16(a): return ram[a] | ram[a+1] << 8  # a: WRAM offset
area = w16(0x079F)
area = 0 if area >= 7 else area
tm_ptr = r24(0x82964A + area * 3)
md_ptr = 0x820000 | r16(0x829717 + area * 2)
station = ram[0xD908 + w16(0x079F)] & 0xFF
explored = ram[0x07F7:0x07F7 + 0x100]
words = []
for i in range(2048):
    src = r16(tm_ptr + i * 2)
    ex = (explored[i >> 3] >> (7 - (i & 7))) & 1
    md = (rom[lorom(md_ptr) + (i >> 3)] >> (7 - (i & 7))) & 1 if station else 0
    words.append(src & 0xFBFF if ex else (src if md else 0x001F))
# palettes: 256 colours BGR555
pal = []
for i in range(256):
    c = r16(0xB6F000 + i * 2)
    pal.append(tuple(((v << 3) | (v >> 2)) for v in (c & 31, (c >> 5) & 31, (c >> 10) & 31)))
tiles = rom[lorom(0xB68000):lorom(0xB68000) + 0x4000]
def tile4(n):
    t = tiles[n * 32:(n + 1) * 32]
    px = [[0] * 8 for _ in range(8)]
    for y in range(8):
        p0, p1, p2, p3 = t[y * 2], t[y * 2 + 1], t[16 + y * 2], t[16 + y * 2 + 1]
        for x in range(8):
            b = 7 - x
            px[y][x] = ((p0 >> b) & 1) | ((p1 >> b) & 1) << 1 | ((p2 >> b) & 1) << 2 | ((p3 >> b) & 1) << 3
    return px
img = Image.new('RGBA', (512, 256), (0, 0, 0, 0))
P = img.load()
for i, w in enumerate(words):
    scr, j = divmod(i, 1024)
    cx, cy = scr * 32 + j % 32, j // 32
    t = tile4(w & 0x3FF); p = (w >> 10) & 7; hf = w & 0x4000; vf = w & 0x8000
    for y in range(8):
        for x in range(8):
            v = t[7 - y if vf else y][7 - x if hf else x]
            if v: P[cx * 8 + x, cy * 8 + y] = pal[p * 16 + v] + (255,)
img.save(out)
print('area', area, 'tilemap', hex(tm_ptr), 'station', station)

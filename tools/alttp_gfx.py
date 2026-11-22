#!/usr/bin/env python3
"""A Link to the Past's tile sheets, decompressed from the cartridge (M3).

The reference for the graphics SNES DS needs that aren't in video RAM while
Link plays: the dungeon map's tiles (ALTTP-5). The game keeps its tiles in
compressed sheets of 64 tiles at 3 bits a pixel and widens them to 4 bits
as it loads a tileset; this does the same, so a tileset can be built
without running the game, and checks the result against the video RAM of
a moment the game loaded it.

    alttp_gfx.py ROM --check NAME [--main N --aux N --sprites N]

NAME.ram / NAME.vram: a harness dump; the tilesets default to the ones in
its work RAM ($0AA1 main, $0AA2 aux, $0AA3 sprites). Compares the BG tiles
(VRAM words $2000-$3FFF) and the sprite tiles from the tileset
($5000-$5FFF) with the dump.

Formats and addresses: docs/games/a-link-to-the-past.md ("Tile sheets").
"""
import argparse
import sys

# GFXSheetPointers ($00:CF80...): bank, high and low bytes of each sheet
BG_BANK, BG_HIGH, BG_LOW = 0x00CF80, 0x00D05F, 0x00D13E
SPR_BANK, SPR_HIGH, SPR_LOW = 0x00CFF3, 0x00D0D2, 0x00D1B1
MAIN_TILESETS = 0x00E073   # 8 sheets per main tileset ($0AA1)
AUX_TILESETS = 0x00DD97    # 4 per aux tileset ($0AA2), 0 = the main one's
SPRITE_TILESETS = 0x00DB57  # 4 per sprite tileset ($0AA3)


def lorom(addr):
    return ((addr >> 16) & 0x7F) * 0x8000 + (addr & 0x7FFF)


def decompress(rom, addr):
    """The game's decompression ($00:E7DE): commands of 3 bits and a length
    (5 bits, or 10 with the extended header $E0): copy, fill a byte, fill a
    word, fill an increasing byte, copy from earlier output (16-bit offset,
    low byte first); $FF ends."""
    src = lorom(addr)
    out = bytearray()
    while True:
        cmd = rom[src]
        src += 1
        if cmd == 0xFF:
            return bytes(out)
        if (cmd & 0xE0) != 0xE0:
            n = (cmd & 0x1F) + 1
            op = cmd >> 5
        else:
            n = ((cmd & 3) << 8 | rom[src]) + 1
            src += 1
            op = (cmd >> 2) & 7
        if op == 0:
            out += rom[src:src + n]
            src += n
        elif op == 1:
            out += bytes([rom[src]]) * n
            src += 1
        elif op == 2:
            lo, hi = rom[src], rom[src + 1]
            src += 2
            out += bytes((lo, hi) * ((n + 1) // 2))[:n]
        elif op == 3:
            v = rom[src]
            src += 1
            out += bytes((v + i) & 0xFF for i in range(n))
        else:
            off = rom[src] | rom[src + 1] << 8
            src += 2
            for i in range(n):
                out.append(out[off + i])


def sheet_addr(rom, bank, high, low, i):
    return rom[lorom(bank) + i] << 16 | rom[lorom(high) + i] << 8 | rom[lorom(low) + i]


def bg_sheet(rom, i):
    return decompress(rom, sheet_addr(rom, BG_BANK, BG_HIGH, BG_LOW, i))


def sprite_sheet(rom, i):
    return decompress(rom, sheet_addr(rom, SPR_BANK, SPR_HIGH, SPR_LOW, i))


def widen(sheet, high):
    """3 bits a pixel (24 bytes a tile: planes 0-1 interleaved, then plane 2)
    to the SNES's 4 (32 bytes); `high`: the fourth plane set where any of
    the others is, so the tile uses a palette's upper 8 colours
    (Do3To4High, $00:E5AF; Do3To4Low, $00:E63C)."""
    out = bytearray()
    for t in range(len(sheet) // 24):
        s = sheet[t * 24:t * 24 + 24]
        lo = bytearray(s[:16])
        p2 = s[16:24]
        hi = bytearray(16)
        for y in range(8):
            hi[2 * y] = p2[y]
            if high:
                hi[2 * y + 1] = lo[2 * y] | lo[2 * y + 1] | p2[y]
        out += lo + hi
    return bytes(out)


def bg_tiles(rom, main, aux):
    """VRAM words $2000-$3FFF as InitializeTilesets ($00:E19B) loads them."""
    mt = rom[lorom(MAIN_TILESETS) + main * 8:lorom(MAIN_TILESETS) + main * 8 + 8]
    at = rom[lorom(AUX_TILESETS) + aux * 4:lorom(AUX_TILESETS) + aux * 4 + 4]
    sheets = [mt[0], mt[1], mt[2]] + [at[i] or mt[3 + i] for i in range(4)] + [mt[7]]
    out = bytearray()
    for slot, n in zip((7, 6, 5, 4, 3, 2, 1, 0), sheets):
        high = slot in (7, 2, 3, 4) if main >= 0x20 else slot >= 4
        out += widen(bg_sheet(rom, n), high)[:0x800]
    return bytes(out), sheets


HIGH_SPRITES = (0x52, 0x53, 0x5A, 0x5B, 0x5C, 0x5E, 0x5F)


def sprite_tiles(rom, index):
    """VRAM words $5000-$5FFF: the sprite tileset's four sheets."""
    st = rom[lorom(SPRITE_TILESETS) + index * 4:lorom(SPRITE_TILESETS) + index * 4 + 4]
    out = bytearray()
    for n in st:
        out += widen(sprite_sheet(rom, n), n in HIGH_SPRITES)[:0x800]
    return bytes(out), list(st)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("rom")
    ap.add_argument("--check", required=True)
    ap.add_argument("--main", type=lambda v: int(v, 0))
    ap.add_argument("--aux", type=lambda v: int(v, 0))
    ap.add_argument("--sprites", type=lambda v: int(v, 0))
    a = ap.parse_args()
    rom = open(a.rom, "rb").read()
    ram = open(a.check + ".ram", "rb").read()
    vram = open(a.check + ".vram", "rb").read()
    main_ = ram[0xAA1] if a.main is None else a.main
    aux = ram[0xAA2] if a.aux is None else a.aux
    spr = ram[0xAA3] if a.sprites is None else a.sprites
    bg, sheets = bg_tiles(rom, main_, aux)
    got = vram[0x4000:0x8000]
    bad = sum(x != y for x, y in zip(bg, got))
    print(f"BG tiles (main {main_:#04x}, aux {aux:#04x}, sheets {[hex(s) for s in sheets]}): "
          f"{bad} of {len(bg)} bytes differ")
    for k in range(8):
        b = sum(x != y for x, y in zip(bg[k * 0x800:(k + 1) * 0x800], got[k * 0x800:(k + 1) * 0x800]))
        if b:
            print(f"  slot at VRAM ${0x2000 + k * 0x400:04X}: {b} bytes differ")
    sp, ss = sprite_tiles(rom, spr)
    got = vram[0xA000:0xC000]
    bad2 = sum(x != y for x, y in zip(sp, got))
    print(f"sprite tiles (set {spr:#04x}, sheets {[hex(s) for s in ss]}): {bad2} of {len(sp)} bytes differ")
    for k in range(4):
        b = sum(x != y for x, y in zip(sp[k * 0x800:(k + 1) * 0x800], got[k * 0x800:(k + 1) * 0x800]))
        if b:
            print(f"  sheet at VRAM ${0x5000 + k * 0x400:04X}: {b} bytes differ")
    return 0 if bad == 0 and bad2 == 0 else 1


if __name__ == "__main__":
    sys.exit(main())

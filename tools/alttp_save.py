#!/usr/bin/env python3
"""Test saves for A Link to the Past (USA), for the harness (M3).

Makes a battery save (8 KB) with file 1 at a chosen point of the game, from
a file the game itself wrote (a work RAM dump of a game in progress: its
$7EF000-$7EF4FF is the file as the game saves it). Only the fields named
below change; the game's checksum is recomputed, so the file select takes
it. Test data only: never shipped, never written to a player's save.

    alttp_save.py RAM OUT.srm PRESET [FIELD=VALUE ...]

PRESET: castle (Zelda rescued, the opening's items), pendants (three
pendants, the Light World's items: Master Sword, flute with the bird,
mirror), dark (the Dark World, Agahnim beaten, every item), or none.
FIELD=VALUE sets one byte of the file by its RAM address (hex, e.g.
F3C8=01 to start at the sanctuary).

Save format (the game's SaveGameFile, $00:894A): file n (1 to 3) at cartridge
RAM (n - 1) x $500 and again at that + $F00; its last word makes the sum of
all its words $5A5A. $1FFE is the file in play (2, 4, 6), written by the
game itself.
"""
import sys

SLOT = 0x500

# Item bytes ($7EF340...) as the game counts them
OPENING = {0x359: 1, 0x35A: 1, 0x34A: 1, 0x341: 1}   # sword, shield, lamp, boomerang
LIGHT_WORLD = {
    0x340: 2, 0x342: 1, 0x343: 10, 0x344: 1, 0x345: 1, 0x346: 1,
    0x34B: 1, 0x34C: 3, 0x34D: 1, 0x34E: 1, 0x353: 2, 0x354: 1, 0x355: 1,
    0x356: 1, 0x357: 1, 0x359: 2, 0x35A: 2, 0x35C: 2, 0x377: 30, 0x36C: 0x50,
    0x36D: 0x50, 0x36E: 0x80, 0x347: 1, 0x348: 1,
}
EVERYTHING = {**LIGHT_WORLD, **{
    0x349: 1, 0x350: 1, 0x351: 1, 0x352: 1, 0x354: 2, 0x35B: 1,
    0x35D: 3, 0x35E: 4, 0x35F: 5,
}}

PRESETS = {
    "none": {},
    # progress 2: Zelda rescued, the sanctuary; map icons 1 (the X on the castle's way)
    "castle": {**OPENING, **{0x3C5: 2, 0x3C7: 2, 0x3C8: 1}},
    # three pendants (map icons 4: go to the Master Sword)
    "pendants": {**LIGHT_WORLD, **{0x3C5: 2, 0x3C7: 4, 0x3C8: 1, 0x374: 7}},
    # Agahnim beaten, starts at the pyramid
    "dark": {**EVERYTHING, **{0x3C5: 3, 0x3C7: 6, 0x3C8: 1, 0x3CA: 0x40, 0x374: 7}},
}


def checksum(block):
    t = 0x5A5A
    for i in range(0, 0x4FE, 2):
        t = (t - (block[i] | block[i + 1] << 8)) & 0xFFFF
    block[0x4FE] = t & 0xFF
    block[0x4FF] = t >> 8


def main():
    if len(sys.argv) < 4 or sys.argv[3] not in PRESETS:
        print(__doc__)
        return 2
    ram = open(sys.argv[1], "rb").read()
    block = bytearray(ram[0xF000:0xF000 + SLOT])
    for off, v in PRESETS[sys.argv[3]].items():
        block[off] = v
    for kv in sys.argv[4:]:
        k, v = kv.split("=")
        block[int(k, 16) - 0xF000] = int(v, 16)
    checksum(block)
    srm = bytearray(0x2000)
    srm[0:SLOT] = block
    srm[0xF00:0xF00 + SLOT] = block
    srm[0x1FFE] = 2
    open(sys.argv[2], "wb").write(srm)
    return 0


if __name__ == "__main__":
    sys.exit(main())

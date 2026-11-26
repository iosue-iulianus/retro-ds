"""A Dolphin save state file (.sNN: header, then LZ4 blocks) unpacked into the raw
buffer the libretro core's retro_unserialize takes (the app's states load there).
Research tool for the Wind Waker spike (docs/research/the-wind-waker.md).

    python3 dolphin_state.py GAME.s00 OUT.raw
"""
import struct, sys
def lz4_block(src, out):
    i = 0; n = len(src)
    while i < n:
        tok = src[i]; i += 1
        lit = tok >> 4
        if lit == 15:
            while True:
                b = src[i]; i += 1; lit += b
                if b != 255: break
        out += src[i:i+lit]; i += lit
        if i >= n: break
        off = src[i] | src[i+1] << 8; i += 2
        ml = tok & 15
        if ml == 15:
            while True:
                b = src[i]; i += 1; ml += b
                if b != 255: break
        ml += 4
        start = len(out) - off
        if off >= ml: out += out[start:start+ml]
        else:
            for k in range(ml): out.append(out[start+k])
d = open(sys.argv[1], "rb").read()
gid = d[0:6]; lzo = struct.unpack_from("<I", d, 8)[0]
cookie, vlen = struct.unpack_from("<II", d, 24)
ver = d[32:32+vlen].decode(); o = 32 + vlen
hv, ct, poff, usize = struct.unpack_from("<HHIQ", d, o); o += 16 + poff
print(gid, "lzo", lzo, "cookie", hex(cookie), "version", ver, "compression", ct, "raw size", usize)
out = bytearray()
if ct == 0: out = d[o:o+usize]
else:
    while len(out) < usize:
        cl = struct.unpack_from("<i", d, o)[0]; o += 4
        lz4_block(d[o:o+cl], out); o += cl
assert len(out) == usize, (len(out), usize)
open(sys.argv[2], "wb").write(out); print("wrote", len(out))

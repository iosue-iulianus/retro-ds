"""A GameCube disc image (.iso): its header and file table (FST), and files
read out of it. Research tool for the Wind Waker spike (docs/research/the-wind-waker.md).

    python3 gcdisc.py DISC.iso [WORD...]   list the files (only those containing a WORD)
"""
import struct, sys
def be32(b, o): return struct.unpack_from(">I", b, o)[0]
def fst(f):
    f.seek(0); h = f.read(0x440)
    dol, fsto, fsts = be32(h,0x420), be32(h,0x424), be32(h,0x428)
    f.seek(fsto); t = f.read(fsts)
    n = be32(t, 8); strt = n*12
    def name(o):
        e = t.index(b"\0", strt+o); return t[strt+o:e].decode("shift_jis")
    out = []; stack = [(n, "")]
    for i in range(1, n):
        while stack and i >= stack[-1][0]: stack.pop()
        w = be32(t, i*12); isdir = w >> 24; nm = name(w & 0xffffff)
        a, b = be32(t, i*12+4), be32(t, i*12+8)
        path = (stack[-1][1] if stack else "") + "/" + nm
        if isdir: stack.append((b, path))
        else: out.append((path, a, b))
    return h, dol, out
def read(f, path):
    for p, o, s in fst(f)[2]:
        if p == path: f.seek(o); return f.read(s)
if __name__ == "__main__":
    with open(sys.argv[1], "rb") as f:
        h, dol, files = fst(f)
        print(h[:6], "disc", h[6], "ver", h[7], "dol@", hex(dol), "files", len(files))
        for p, o, s in files:
            if len(sys.argv) < 3 or any(k in p for k in sys.argv[2:]): print(p, s)

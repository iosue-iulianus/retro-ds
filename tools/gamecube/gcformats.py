"""Yaz0 decompression, RARC archives and BTI textures (GameCube formats), written
from the public format descriptions. Research tool for the Wind Waker spike
(docs/research/the-wind-waker.md)."""
import struct, zlib
def be16(b,o): return struct.unpack_from(">H",b,o)[0]
def be32(b,o): return struct.unpack_from(">I",b,o)[0]
def yaz0(b):
    if b[:4] != b"Yaz0": return b
    n = be32(b,4); out = bytearray(); s = 16
    while len(out) < n:
        code = b[s]; s += 1
        for i in range(8):
            if len(out) >= n: break
            if code & (0x80 >> i): out.append(b[s]); s += 1
            else:
                b1, b2 = b[s], b[s+1]; s += 2
                dist = ((b1 & 0xf) << 8 | b2) + 1; cnt = b1 >> 4
                if cnt == 0: cnt = b[s] + 0x12; s += 1
                else: cnt += 2
                for _ in range(cnt): out.append(out[-dist])
    return bytes(out)
def rarc(b):
    b = yaz0(b); assert b[:4] == b"RARC", b[:4]
    doff = be32(b,0xc) + 0x20; info = 0x20
    nnodes, nodeo = be32(b,info), be32(b,info+4) + 0x20
    nent, ento = be32(b,info+8), be32(b,info+0xc) + 0x20
    stro = be32(b,info+0x14) + 0x20
    def s(o): e = b.index(b"\0", stro+o); return b[stro+o:e].decode("shift_jis","replace")
    files = {}
    def walk(ni, path):
        no = nodeo + ni*16
        nm = s(be32(b,no+4)); cnt = be16(b,no+0xa); first = be32(b,no+0xc)
        path = path + "/" + nm
        for k in range(cnt):
            eo = ento + (first+k)*20
            typ = b[eo+4]; en = s(be16(b,eo+6)); a, sz = be32(b,eo+8), be32(b,eo+12)
            if en in (".",".."): continue
            if typ & 2: walk(a, path)
            else: files[path + "/" + en] = b[doff+a:doff+a+sz]
    walk(0, "")
    return files
def rgb5a3(v):
    if v & 0x8000: return ((v>>10&31)*255//31, (v>>5&31)*255//31, (v&31)*255//31, 255)
    return ((v>>8&15)*17, (v>>4&15)*17, (v&15)*17, (v>>12&7)*255//7)
def rgb565(v): return ((v>>11&31)*255//31, (v>>5&63)*255//63, (v&31)*255//31, 255)
BLK = {0:(8,8,4),1:(8,4,8),2:(8,4,8),3:(4,4,16),4:(4,4,16),5:(4,4,16),6:(4,4,32),8:(8,8,4),9:(8,4,8),10:(4,4,16),14:(8,8,4)}
def palcolor(fmt, v):
    if fmt == 0: return (v&0xff, v&0xff, v&0xff, v>>8)
    if fmt == 1: return rgb565(v)
    return rgb5a3(v)
def texture(data, fmt, w, h, pal=None, palfmt=2):
    bw, bh, bpp = BLK[fmt]
    px = [[(0,0,0,0)]*w for _ in range(h)]
    o = 0
    def put(x,y,c):
        if x < w and y < h: px[y][x] = c
    for by in range(0, h, bh):
        for bx in range(0, w, bw):
            if fmt == 14:
                for sub in range(4):
                    sx, sy = bx + (sub&1)*4, by + (sub>>1)*4
                    c0, c1 = be16(data,o), be16(data,o+2); bits = be32(data,o+4); o += 8
                    a, b2 = rgb565(c0), rgb565(c1)
                    if c0 > c1: cs = [a,b2,tuple((2*a[i]+b2[i])//3 for i in range(3))+(255,), tuple((a[i]+2*b2[i])//3 for i in range(3))+(255,)]
                    else: cs = [a,b2,tuple((a[i]+b2[i])//2 for i in range(3))+(255,),(0,0,0,0)]
                    for i in range(16): put(sx+i%4, sy+i//4, cs[bits >> (30-2*i) & 3])
                continue
            if fmt == 6:
                for i in range(16):
                    ar = be16(data,o+2*i); gb = be16(data,o+32+2*i)
                    put(bx+i%4, by+i//4, (ar&0xff, gb>>8, gb&0xff, ar>>8))
                o += 64; continue
            for i in range(bw*bh):
                x, y = bx+i%bw, by+i//bw
                if bpp == 4:
                    v = data[o+i//2] >> (4 if i%2==0 else 0) & 15
                    if fmt == 0: c = (v*17,)*3+(255,)
                    elif fmt == 8: c = palcolor(palfmt, be16(pal, v*2))
                    put(x,y,c)
                elif bpp == 8:
                    v = data[o+i]
                    if fmt == 1: c = (v,v,v,255)
                    elif fmt == 2: c = ((v&15)*17,)*3+((v>>4)*17,)
                    elif fmt == 9: c = palcolor(palfmt, be16(pal, v*2))
                    put(x,y,c)
                else:
                    v = be16(data,o+2*i)
                    if fmt == 3: c = (v&0xff,)*3+(v>>8,)
                    elif fmt == 4: c = rgb565(v)
                    elif fmt == 5: c = rgb5a3(v)
                    put(x,y,c)
            o += bw*bh*bpp//8
    return px
def bti(b, base=0):
    fmt = b[base]; w, h = be16(b,base+2), be16(b,base+4)
    palfmt = b[base+9]; npal = be16(b,base+0xa); palo = be32(b,base+0xc); imgo = be32(b,base+0x1c)
    pal = b[base+palo:base+palo+npal*2] if npal else None
    return fmt, w, h, texture(b[base+imgo:], fmt, w, h, pal, palfmt)
def png(px, path):
    h, w = len(px), len(px[0])
    raw = b"".join(b"\0" + bytes(c for p in row for c in p) for row in px)
    def chunk(t, d): return struct.pack(">I",len(d)) + t + d + struct.pack(">I", zlib.crc32(t+d))
    open(path,"wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB",w,h,8,6,0,0,0)) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))

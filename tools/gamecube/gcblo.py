"""A J2D BLO1 screen layout drawn from the disc: pictures only (positions nested
by parent, default visibility, black/white tint, corner colours, alpha), no text
or windows. Research tool for the Wind Waker spike (docs/research/the-wind-waker.md).

    python3 gcblo.py DISC.iso ARC[,ARC...] LAYOUT.blo OUT.png
    e.g. /res/Msg/fmapres.arc,/res/Fmap/Fmap.arc f_map.blo chart.png (The Wind Waker's sea chart)
"""
import struct, sys
import gcdisc as gcfs, gcformats as gcfmt
from PIL import Image
def be16(b,o): return struct.unpack_from(">H",b,o)[0]
def s16(b,o): return struct.unpack_from(">h",b,o)[0]
def be32(b,o): return struct.unpack_from(">I",b,o)[0]
def parse(b):
    assert b[:8] == b"SCRNblo1"
    o = 0x20; root = {"kids": [], "x": 0, "y": 0, "vis": 1, "tag": "root"}; stack = [root]; last = root
    while o < len(b):
        tag = b[o:o+4].decode("ascii", "replace"); sz = be32(b, o+4)
        if tag == "EXT1" or sz == 0: break
        if tag == "BGN1": stack.append(last)
        elif tag == "END1": stack.pop()
        elif tag in ("PAN1", "PIC1", "WIN1", "TBX1"):
            p = o + 8; n = b[p]; vis = b[p+1]; ptag = b[p+4:p+8].decode("ascii", "replace")
            x, y, w, h = s16(b,p+8), s16(b,p+10), s16(b,p+12), s16(b,p+14)
            q = p + 16; n -= 6; alpha = 255
            if n: q += 2; n -= 1       # rotation
            if n: q += 1; n -= 1       # base position
            if n: alpha = b[q]; q += 1; n -= 1
            if n: q += 1; n -= 1
            q = (q + 3) & ~3
            pane = {"type": tag, "tag": ptag, "vis": vis, "x": x, "y": y, "w": w, "h": h, "alpha": alpha, "kids": []}
            if tag == "PIC1":
                m = b[q]; q += 1
                def ref():
                    nonlocal q
                    t, l = b[q], b[q+1]; name = b[q+2:q+2+l].decode("ascii", "replace"); q += 2 + l; return name
                pane["timg"] = ref(); pane["tlut"] = ref(); q += 1; m -= 3
                black, white, corners = 0x00000000, 0xFFFFFFFF, [0xFFFFFFFF]*4
                if m: q += 1; m -= 1
                if m: q += 1; m -= 1
                if m: black = be32(b,q); q += 4; m -= 1
                if m: white = be32(b,q); q += 4; m -= 1
                for i in range(4):
                    if m: corners[i] = be32(b,q); q += 4; m -= 1
                pane.update(black=black, white=white, corners=corners)
            stack[-1]["kids"].append(pane); last = pane
        o += sz
    return root
def rgba(c): return (c>>24&255, c>>16&255, c>>8&255, c&255)
def render(root, textures, size, show_hidden=False):
    img = Image.new("RGBA", size, (0,0,0,255)); drawn = [0]
    def walk(p, ox, oy, a):
        if not p["vis"] and not show_hidden and p is not root: return
        x, y = ox + p["x"], oy + p["y"]; al = a * p.get("alpha", 255) / 255
        if p.get("type") == "PIC1" and p["timg"] and p["w"] > 0 and p["h"] > 0:
            name = p["timg"].split("/")[-1]
            t = textures.get(name)
            if t is not None:
                bl, wh = rgba(p["black"]), rgba(p["white"]); cc = rgba(p["corners"][0])
                t = t.resize((p["w"], p["h"]))
                px = t.load(); out = Image.new("RGBA", t.size); po = out.load()
                for j in range(t.size[1]):
                    for i in range(t.size[0]):
                        r,g,bb,aa = px[i,j]
                        c = [ (bl[k] + (wh[k]-bl[k]) * v // 255) * cc[k] // 255 for k,v in enumerate((r,g,bb,aa)) ]
                        po[i,j] = (c[0], c[1], c[2], int(c[3] * al / 255))
                img.alpha_composite(out, (int(x), int(y))); drawn[0] += 1
        for k in p["kids"]: walk(k, x, y, al)
    walk(root, 0, 0, 255)
    return img, drawn[0]
if __name__ == "__main__":
    arcs = sys.argv[2].split(","); blo = sys.argv[3]; out = sys.argv[4]
    f = open(sys.argv[1], "rb"); textures = {}; blob = None
    for a in arcs:
        for path, data in gcfmt.rarc(gcfs.read(f, a)).items():
            if path.endswith(".bti"):
                try:
                    fmt, w, h, px = gcfmt.bti(gcfmt.yaz0(data))
                    im = Image.new("RGBA", (w, h)); im.putdata([c for row in px for c in row]); textures[path.split("/")[-1]] = im
                except Exception as e: pass
            if path.endswith(blo): blob = gcfmt.yaz0(data)
    root = parse(blob)
    def count(p): return 1 + sum(count(k) for k in p["kids"])
    img, n = render(root, textures, (640, 480))
    img.save(out); print("panes", count(root) - 1, "pictures drawn", n, "textures", len(textures))

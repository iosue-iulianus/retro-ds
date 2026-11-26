"""The textures in a disc's archives as PNGs (the files listed, the BTI textures
decoded). Research tool for the Wind Waker spike (docs/research/the-wind-waker.md).
The pictures are the player's game: never commit them.

    python3 gcdump.py DISC.iso OUTDIR ARC...
"""
import sys, os
import gcdisc as gcfs, gcformats as gcfmt
out = sys.argv[2]; os.makedirs(out, exist_ok=True)
with open(sys.argv[1], "rb") as f:
    for arc in sys.argv[3:]:
        files = gcfmt.rarc(gcfs.read(f, arc))
        for p, d in files.items():
            print(arc, p, len(d))
            if p.lower().endswith(".bti"):
                try:
                    fmt,w,h,px = gcfmt.bti(gcfmt.yaz0(d))
                    gcfmt.png(px, os.path.join(out, (arc.split("/")[-1]+p).replace("/","_")[:-4] + ".png"))
                except Exception as e: print("  fail", e)

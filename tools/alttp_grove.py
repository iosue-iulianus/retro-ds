import sys, struct
from PIL import Image
S=sys.argv[1]
ram=open(S+"/gv/grove.ram","rb").read(); vram=open(S+"/gv/grove.vram","rb").read()
def r8(a): return ram[a]
def r16(a): return ram[a]|ram[a+1]<<8
def vword(w): return vram[w*2]|vram[w*2+1]<<8
V_BG2_MAP,V_BG1_MAP,V_CHR=0x0000,0x1000,0x2000
def pix(mapBase,x,y):
    y&=511; lx=x&511; screen=(lx>>8)+((y>>8)<<1)
    e=vword(mapBase+screen*0x400+((y>>3)&31)*32+((lx>>3)&31))
    ty=7-(y&7) if e&0x8000 else y&7
    base=V_CHR+(e&0x3FF)*16; p0=vword(base+ty); p1=vword(base+8+ty)
    tx=lx&7; b=tx if e&0x4000 else 7-tx
    c=(p0>>b&1)|(p0>>(8+b)&1)<<1|(p1>>b&1)<<2|(p1>>(8+b)&1)<<3
    return ((e>>10)&7)*16+c if c else 0
def col(i): return r16(0xC500+i*2)
def rgb(c): return ((c&31)*255//31,(c>>5&31)*255//31,(c>>10&31)*255//31)
ws,asub=r8(0x99),r8(0x9A); print("TM %02x TS %02x CGWSEL %02x CGADSUB %02x COLDATA %02x %02x %02x"%(r8(0x1C),r8(0x1D),ws,asub,r8(0x9C),r8(0x9D),r8(0x9E)))
cx,cy=r16(0xE2),r16(0xE8)+1; sx,sy=r16(0xE0),r16(0xE6)+1
print("BG2 scroll",cx,cy,"BG1",sx,sy)
img=Image.new('RGB',(512,512))
P=img.load()
for y in range(512):
    for x in range(512):
        m=pix(V_BG2_MAP,x,y); main=col(m)
        # sub: BG1 at its own scroll, as far from it as BG2's
        s=pix(V_BG1_MAP,sx+(x-cx),sy+(y-cy)); sub=col(s) if s else col(0)
        layerOn = (asub&0x02) if m else (asub&0x20)
        if layerOn:
            out=[]
            for k in range(3):
                mm=main>>(5*k)&31; ss=sub>>(5*k)&31
                v=mm-ss if asub&0x80 else mm+ss
                if (asub&0x40) and s: v>>=1
                out.append(max(0,min(31,v)))
            main=out[0]|out[1]<<5|out[2]<<10
        P[x,y]=rgb(main)
img.save(S+"/gv/full.png")
shot=Image.open(S+"/gv/grove.png").convert('RGB')
crop=img.crop((cx,cy,cx+256,cy+224))
diff=sum(1 for yy in range(224) for xx in range(256) if max(abs(a-b) for a,b in zip(shot.getpixel((xx,yy)),crop.getpixel((xx,yy))))>16)
print("pixels differing from the game's screen:",diff,"of",256*224)
o=Image.new('RGB',(512+20+256,512),(40,40,40)); o.paste(img,(0,0)); o.paste(shot,(532,cy)); o.save(S+"/gv/compare.png")

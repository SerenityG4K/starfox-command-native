"""Compare des captures deux à deux, écran par écran : pixels qui diffèrent, et leur boîte.

Lit les PNG du portage sans dépendance (lire() sert aussi à recadrer.py et retours.py).

    python tools/diffpng.py build/film_0000.png build/film_0001.png ...
    python tools/diffpng.py build/frame_*.png        # des captures consécutives (SFC_DUMP=1)

Sous licence MIT.
"""
import sys, zlib, struct
from pathlib import Path

def lire(p):
    d=Path(p).read_bytes(); i=8; idat=b''; ct=6; w=h=0
    while i < len(d):
        n=struct.unpack('>I', d[i:i+4])[0]; t=d[i+4:i+8]
        if t==b'IHDR': w,h,bd,ct=struct.unpack('>IIBB', d[i+8:i+18])
        elif t==b'IDAT': idat+=d[i+8:i+8+n]
        i+=12+n
    raw=zlib.decompress(idat); c=4 if ct==6 else 3; st=w*c
    out=[]; prev=bytearray(st); pos=0
    for y in range(h):
        f=raw[pos]; pos+=1
        l=bytearray(raw[pos:pos+st]); pos+=st
        if f==1:
            for x in range(c,st): l[x]=(l[x]+l[x-c])&255
        elif f==2:
            for x in range(st): l[x]=(l[x]+prev[x])&255
        elif f==3:
            for x in range(st): l[x]=(l[x]+((l[x-c] if x>=c else 0)+prev[x])//2)&255
        elif f==4:
            for x in range(st):
                a=l[x-c] if x>=c else 0; b=prev[x]; cc=prev[x-c] if x>=c else 0
                p_=a+b-cc; pa=abs(p_-a); pb=abs(p_-b); pc=abs(p_-cc)
                l[x]=(l[x]+(a if pa<=pb and pa<=pc else b if pb<=pc else cc))&255
        out.append(bytes(l)); prev=l
    return w,h,c,out

def comparer(a,b,y0=0,y1=384,seuil=24):
    w,h,c,A=lire(a); _,_,c2,B=lire(b)
    n=0; bx0=by0=10**9; bx1=by1=-1
    for y in range(y0,y1):
        la,lb=A[y],B[y]
        for x in range(w):
            if max(abs(la[x*c+k]-lb[x*c2+k]) for k in range(3))>seuil:
                n+=1; bx0=min(bx0,x); bx1=max(bx1,x); by0=min(by0,y); by1=max(by1,y)
    return n,(bx0,by0,bx1,by1)

if __name__=='__main__':
    fichiers=sys.argv[1:]
    for a,b in zip(fichiers,fichiers[1:]):
        h=lire(a)[1]
        for ecran,y0,y1 in (('haut',0,h//2),('bas',h//2,h)):
            n,boite=comparer(a,b,y0,y1)
            print(f"{Path(a).name} -> {Path(b).name}, {ecran} : {n} pixels diffèrent" + (f", boîte {boite}" if n else ""))

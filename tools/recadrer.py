"""Planche de recadrages : la même zone de plusieurs captures, côte à côte.

    python tools/recadrer.py sortie.png x0 y0 x1 y1 colonnes a.png b.png ...

Sous licence MIT.
"""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from diffpng import lire
from nitro import png as pngwrite
sortie=sys.argv[1]; x0,y0,x1,y1,cols=map(int,sys.argv[2:7]); fichiers=sys.argv[7:]
L,H=x1-x0,y1-y0; M=4
rangs=(len(fichiers)+cols-1)//cols
W=cols*L+(cols-1)*M; HT=rangs*H+(rangs-1)*M
img=bytearray([20,20,20,255]*(W*HT))
for n,f in enumerate(fichiers):
    w,h,c,lignes=lire(f)
    ox=(n%cols)*(L+M); oy=(n//cols)*(H+M)
    for y in range(H):
        src=lignes[y0+y]
        for x in range(L):
            d=((oy+y)*W+ox+x)*4
            img[d:d+3]=src[(x0+x)*c:(x0+x)*c+3]
pngwrite.write_rgba(sortie,W,HT,bytes(img))
print(f"{len(fichiers)} recadrages -> {sortie} ({W}x{HT})")

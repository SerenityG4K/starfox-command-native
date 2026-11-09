"""Cherche, sur l'écran du bas d'un film, les images qui reviennent à celle d'avant-avant (un aller-retour).

Lit build/film_*.png et build/film.txt, que produit SFC_AFFICHAGE_FILM=n.

    python tools/retours.py

Sous licence MIT.
"""
import sys, glob
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from diffpng import lire
fs = sorted(glob.glob('build/film_*.png'))
emp = []
for f in fs:
    w, h, c, L = lire(f)
    emp.append(tuple(L[y][x*c] + 256*L[y][x*c+1] for y in range(384, 768, 6) for x in range(0, 512, 6)))
lignes = open('build/film.txt').read().split('\n')
def diff(a, b): return sum(1 for u, v in zip(a, b) if abs((u & 255) - (v & 255)) > 24 or abs((u >> 8) - (v >> 8)) > 24)
retours = 0
for k in range(2, len(emp)):
    d = diff(emp[k], emp[k-1])
    if d >= 3 and diff(emp[k], emp[k-2]) < 3:
        retours += 1
        print(f"  retour à l'image filmée {k} : {lignes[k] if k < len(lignes) else ''}")
print(f"{len(emp)} images filmées, {retours} allers-retours sur l'écran du bas")

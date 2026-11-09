#!/usr/bin/env python3
"""Rend un fond 2D du jeu en PNG : ses tuiles (.nbfc), sa palette (.nbfp) et sa carte d'écran (.nbfs).

Les trois fichiers portent le même nom ; chacun peut être compressé (« -cmp ») ou non, dans le dossier de
données ou dans une archive .srl (« Fr.srl:2D/… »). Formats, tels que les lit le moteur 2D de la console :
- palette : une suite de couleurs RGB555 sur 16 bits (bits 0-4 rouge, 5-9 vert, 10-14 bleu) ; la couleur 0
  de chaque palette de 16 est transparente ;
- tuiles : 8×8 points, 4 bits par point (32 octets la tuile), ou 8 bits avec --8bpp (64 octets) ;
- carte d'écran : une entrée de 16 bits par case de 8×8 — numéro de tuile (bits 0-9), retournements
  horizontal (10) et vertical (11), palette de 16 couleurs (12-15) ; 32 cases de large par défaut.

Usage :
    python tools/fond_2d.py data 2D/StrategyScene/missile_mode -o missile_mode.png
    python tools/fond_2d.py data "Fr.srl:2D/Menu/…/nom" --8bpp --largeur 64 -o nom.png
"""
from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from fichier_du_jeu import existe, lire_octets  # noqa: E402
from nitro.png import write_rgba  # noqa: E402


def lire_partie(dossier: Path, base: str, ext: str) -> bytes:
    for nom in (base + ext + "-cmp", base + ext):
        if existe(dossier, nom):
            return lire_octets(dossier, nom)
    raise SystemExit(f"{base}{ext} : introuvable, compressé ou non")


def rendre(tuiles: bytes, palette: bytes, ecran: bytes, huit_bits: bool, largeur: int) -> tuple[int, int, bytes]:
    couleurs = [struct.unpack_from("<H", palette, i)[0] for i in range(0, len(palette) - 1, 2)]
    rgb = [((c & 31) * 255 // 31, ((c >> 5) & 31) * 255 // 31, ((c >> 10) & 31) * 255 // 31) for c in couleurs]
    cases = len(ecran) // 2
    hauteur = (cases + largeur - 1) // largeur
    w, h = largeur * 8, hauteur * 8
    pix = bytearray(w * h * 4)
    taille_tuile = 64 if huit_bits else 32
    for k in range(cases):
        e = struct.unpack_from("<H", ecran, 2 * k)[0]
        t, hf, vf, pal = e & 0x3FF, (e >> 10) & 1, (e >> 11) & 1, e >> 12
        tx, ty = (k % largeur) * 8, (k // largeur) * 8
        for y in range(8):
            for x in range(8):
                sx, sy = (7 - x) if hf else x, (7 - y) if vf else y
                o = t * taille_tuile + (sy * 8 + sx if huit_bits else sy * 4 + sx // 2)
                if o >= len(tuiles):
                    continue
                v = tuiles[o] if huit_bits else (tuiles[o] >> (4 * (sx & 1))) & 15
                if v == 0:
                    continue                                   # transparent
                i = v if huit_bits else pal * 16 + v
                if i >= len(rgb):
                    continue
                p = ((ty + y) * w + tx + x) * 4
                pix[p:p + 4] = bytes(rgb[i]) + b"\xff"
    return w, h, bytes(pix)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dossier", help="le dossier de données (data/, ou une copie)")
    ap.add_argument("chemin", help="le chemin du fond sans extension, dans files/ ou « archive.srl:… »")
    ap.add_argument("-o", metavar="PNG")
    ap.add_argument("--8bpp", dest="huit", action="store_true", help="tuiles de 256 couleurs")
    ap.add_argument("--largeur", type=int, default=32, help="largeur de la carte, en cases de 8 points")
    args = ap.parse_args()
    dossier = Path(args.dossier)
    tuiles = lire_partie(dossier, args.chemin, ".nbfc")
    palette = lire_partie(dossier, args.chemin, ".nbfp")
    ecran = lire_partie(dossier, args.chemin, ".nbfs")
    w, h, pix = rendre(tuiles, palette, ecran, args.huit, args.largeur)
    sortie = args.o or Path(args.chemin.split(":")[-1]).name + ".png"
    write_rgba(sortie, w, h, pix)
    print(f"{args.chemin} -> {sortie} ({w}×{h}, {len(tuiles) // (64 if args.huit else 32)} tuiles, "
          f"{len(palette) // 2} couleurs)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

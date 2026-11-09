#!/usr/bin/env python3
"""L'icône du jeu, tirée de la cartouche du joueur : l'emblème de sa bannière, en fichier .ico.

La bannière d'une cartouche DS (son adresse est écrite en 0x68 de l'en-tête) porte l'icône que la console
montre dans son menu : 32×32 points, 4 bits par point rangés en tuiles de 8×8, et une palette de seize
couleurs en RGB555 dont la première est transparente. C'est l'emblème de Star Fox.

tools/preparer.py en fait l'icône de sfc.exe, construit chez le joueur : le portage ne la livre pas, elle
vient de sa cartouche comme le reste. Le jeu, lui, la relit dans la cartouche pour sa fenêtre et la barre
des tâches (runtime/window_win32.cpp), et le lanceur pour la sienne (launcher/images.c).

Le .ico contient l'icône à sa taille, 32×32, puis agrandie point pour point (64, 128, 256) : de grands
pixels nets plutôt qu'un flou, à toutes les tailles de l'Explorateur.

Usage :
    python tools/icone.py "<cartouche>.nds" -o icone.ico
    python tools/icone.py data -o icone.ico            (un dossier de données : system/banner.bin)

Sous licence MIT.
"""
from __future__ import annotations

import argparse
import struct
from pathlib import Path

TAILLES = (32, 64, 128, 256)


def banniere(source: Path) -> bytes:
    """Les 0x240 premiers octets de la bannière : version, sommes, puis l'icône et sa palette."""
    if source.is_dir():
        return (source / "system" / "banner.bin").read_bytes()[:0x240]
    with source.open("rb") as f:
        entete = f.read(0x200)
        decalage = struct.unpack_from("<I", entete, 0x68)[0]
        if not decalage:
            raise ValueError("cette cartouche n'a pas de bannière")
        f.seek(decalage)
        return f.read(0x240)


def points(b: bytes) -> list[tuple[int, int, int, int]]:
    """Les 32×32 points de l'icône, ligne par ligne depuis le haut : (rouge, vert, bleu, opacité)."""
    if len(b) < 0x240:
        raise ValueError("bannière tronquée")
    palette = []
    for i in range(16):
        c = struct.unpack_from("<H", b, 0x220 + 2 * i)[0]
        palette.append(((c & 31) * 255 // 31, (c >> 5 & 31) * 255 // 31, (c >> 10 & 31) * 255 // 31))
    image = [(0, 0, 0, 0)] * (32 * 32)
    for tuile in range(16):
        tx, ty = tuile % 4, tuile // 4
        for k in range(32):                      # 8×8 points, deux par octet, le premier dans les bits bas
            octet = b[0x20 + tuile * 32 + k]
            for moitie in range(2):
                indice = octet >> (4 * moitie) & 15
                x, y = tx * 8 + (k * 2 + moitie) % 8, ty * 8 + (k * 2 + moitie) // 8
                image[y * 32 + x] = (*palette[indice], 0 if indice == 0 else 255)
    return image


def ico(image: list[tuple[int, int, int, int]]) -> bytes:
    """Un .ico : chaque taille en BMP 32 bits (de bas en haut), avec son masque de transparence."""
    corps = []
    for t in TAILLES:
        f = t // 32
        couleurs = bytearray()
        masque = bytearray()
        pas_masque = ((t + 31) // 32) * 4
        for y in reversed(range(t)):
            ligne_masque = bytearray(pas_masque)
            for x in range(t):
                r, v, b, a = image[(y // f) * 32 + x // f]
                couleurs += bytes((b, v, r, a))
                if not a:
                    ligne_masque[x // 8] |= 0x80 >> (x % 8)
            masque += ligne_masque
        entete = struct.pack("<IiiHHIIiiII", 40, t, 2 * t, 1, 32, 0, len(couleurs) + len(masque), 0, 0, 0, 0)
        corps.append(entete + bytes(couleurs) + bytes(masque))
    sortie = bytearray(struct.pack("<HHH", 0, 1, len(TAILLES)))
    position = 6 + 16 * len(TAILLES)
    for t, donnees in zip(TAILLES, corps):
        sortie += struct.pack("<BBBBHHII", t % 256, t % 256, 0, 0, 1, 32, len(donnees), position)
        position += len(donnees)
    for donnees in corps:
        sortie += donnees
    return bytes(sortie)


def ecrire_icone(source: Path, destination: Path) -> None:
    destination.write_bytes(ico(points(banniere(source))))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("source", help="la cartouche (.nds) ou un dossier de données")
    ap.add_argument("-o", "--sortie", required=True, help="le fichier .ico à écrire")
    args = ap.parse_args()
    ecrire_icone(Path(args.source), Path(args.sortie))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Découpe la cartouche en un dossier de données que le portage lit directement.

Le jeu demande ses données par adresse, comme au lecteur de la console. On garde donc
cette correspondance, mais les octets viennent de fichiers séparés : l'arborescence du
jeu d'un côté, les zones système de l'autre. Plus besoin de l'image de cartouche, et
chaque fichier devient remplaçable.

Les fichiers sont écrits **tels qu'ils sont dans la cartouche**, sans les décompresser :
c'est le jeu qui s'en charge, et lui donner autre chose le tromperait.

Usage :
    python tools/build_data.py "<rom.nds>" [-o data]

Produit :
    <out>/files/…        l'arborescence du jeu, octets bruts
    <out>/system/…       en-tête, tables, binaires et overlays
    <out>/index.bin      où chaque zone se trouve dans l'espace d'adressage de la cartouche
"""
from __future__ import annotations

import argparse
import json
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from nitro.rom import NdsRom  # noqa: E402

MAGIE = b"SFCD"
VERSION = 1


def ajouter(zones: list, offset: int, taille: int, chemin: str) -> None:
    if taille > 0:
        zones.append((offset, taille, chemin))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("rom")
    ap.add_argument("-o", "--out", default="data")
    args = ap.parse_args()

    rom = NdsRom.load(args.rom)
    brut = rom.data
    out = Path(args.out)
    (out / "files").mkdir(parents=True, exist_ok=True)
    (out / "system").mkdir(parents=True, exist_ok=True)

    zones: list[tuple[int, int, str]] = []

    # --- zones système : tout ce qui n'est pas un fichier du jeu ---------------
    entete = brut[:0x4000]
    (out / "system" / "header.bin").write_bytes(entete)
    ajouter(zones, 0, len(entete), "system/header.bin")

    h = rom.header
    parts = [
        ("arm9.bin", h.arm9_rom_offset, h.arm9_size),
        ("arm7.bin", h.arm7_rom_offset, h.arm7_size),
        ("fnt.bin", h.fnt_offset, h.fnt_size),
        ("fat.bin", h.fat_offset, h.fat_size),
        ("ovt9.bin", h.arm9_ovt_offset, h.arm9_ovt_size),
        ("ovt7.bin", h.arm7_ovt_offset, h.arm7_ovt_size),
    ]
    for nom, off, taille in parts:
        if not taille:
            continue
        (out / "system" / nom).write_bytes(brut[off:off + taille])
        ajouter(zones, off, taille, f"system/{nom}")

    # La bannière — icône et titre du jeu dans les huit langues — est hors des tables :
    # sa taille dépend de sa version (1 : les six premières langues ; 2 : plus le chinois ;
    # 3 : plus le coréen).
    if h.banner_offset:
        version = int.from_bytes(brut[h.banner_offset:h.banner_offset + 2], "little")
        taille = {1: 0x840, 2: 0x940, 3: 0xA40}.get(version & 0xFF, 0xA40)
        (out / "system" / "banner.bin").write_bytes(brut[h.banner_offset:h.banner_offset + taille])
        ajouter(zones, h.banner_offset, taille, "system/banner.bin")

    # Douze octets suivent parfois le binaire ARM9 : le pied de module du SDK, hors de sa
    # taille déclarée. On le garde pour que la cartouche reste identique octet pour octet.
    fin9 = h.arm9_rom_offset + h.arm9_size
    if brut[fin9:fin9 + 4] == b"\x21\x06\xC0\xDE":
        (out / "system" / "arm9_footer.bin").write_bytes(brut[fin9:fin9 + 12])
        ajouter(zones, fin9, 12, "system/arm9_footer.bin")

    # --- les fichiers du jeu, y compris les overlays ---------------------------
    # La table d'allocation donne l'étendue de chacun dans la cartouche ; les overlays
    # y figurent comme les autres, ce qui évite d'en faire un cas particulier.
    ecrits = 0
    for e in rom.files:
        if e.end <= e.offset or e.end > len(brut):
            continue
        rel = "files/" + e.path.replace("\\", "/").lstrip("/")
        cible = out / rel
        cible.parent.mkdir(parents=True, exist_ok=True)
        cible.write_bytes(brut[e.offset:e.end])
        ajouter(zones, e.offset, e.size, rel)
        ecrits += 1

    # --- l'index : le moteur y cherche la zone qui couvre une adresse ----------
    zones.sort()
    with open(out / "index.bin", "wb") as f:
        chemins = "\n".join(z[2] for z in zones).encode("utf-8")
        f.write(MAGIE)
        f.write(struct.pack("<III", VERSION, len(zones), len(chemins)))
        pos = 0
        for offset, taille, chemin in zones:
            f.write(struct.pack("<III", offset, taille, pos))
            pos += len(chemin.encode("utf-8")) + 1
        f.write(chemins)

    total = sum(z[1] for z in zones)
    print(f"{ecrits} fichiers, {len(zones)} zones, {total / 1048576:.1f} Mo")
    print(f"-> {out}/index.bin")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

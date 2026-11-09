#!/usr/bin/env python3
"""Assembler une version publiable du portage — et s'assurer qu'aucun fichier du jeu n'y entre.

Le paquet contient ce que le dépôt versionne (git ls-files : ni la ROM, ni data/, ni le code généré, ni les
analyses, que .gitignore écarte), le lanceur compilé, et, si on le demande, les outils de construction
livrés (outils/ : Python, CMake, GCC) pour que le joueur n'ait rien à installer. Le joueur y désigne sa
cartouche, et « Préparer le jeu » construit le jeu chez lui (tools/preparer.py).

Avant d'écrire l'archive, chaque fichier est contrôlé : aucune image de cartouche ni archive du jeu, aucun
dossier généré ou préparé, aucun exécutable du jeu, et rien de gros hors des outils livrés et de la carte du
code (tools/carte/, des adresses). Une seule infraction, et rien n'est écrit.

Usage :
    python tools/paquet.py [--sortie build/paquet] [--avec-outils]

Sous licence MIT.
"""
from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path

RACINE = Path(__file__).resolve().parent.parent

INTERDITS_SUFFIXES = {".nds", ".srl", ".sdat", ".sadl", ".sav", ".bin", ".ntfq", ".mdl"}
INTERDITS_DOSSIERS = {"generated", "generated7", "analysis", "data", "jeu", "preparation", "sauvegarde", "build",
                      "assets"}
GROS = 1 << 20          # au-delà d'un mégaoctet, un fichier doit avoir une raison d'être là


def version() -> str:
    m = re.search(r'^VERSION = "([^"]+)"', (RACINE / "tools" / "preparer.py").read_text(encoding="utf-8"), re.M)
    return m.group(1) if m else "dev"


def controler(dossier: Path) -> list[str]:
    fautes = []
    for f in dossier.rglob("*"):
        if not f.is_file():
            continue
        rel = f.relative_to(dossier)
        dans_outils = rel.parts[0] == "outils"
        if dans_outils:
            continue                    # les outils de construction livrés : rien du jeu
        if f.suffix.lower() in INTERDITS_SUFFIXES:
            fautes.append(f"{rel} : un fichier du jeu ({f.suffix})")
        if INTERDITS_DOSSIERS & set(p.lower() for p in rel.parts[:-1]):
            fautes.append(f"{rel} : dans un dossier tiré de la cartouche")
        if f.suffix.lower() == ".exe" and rel.as_posix() != "lanceur.exe":
            fautes.append(f"{rel} : un exécutable autre que le lanceur")
        if f.stat().st_size > GROS and rel.parts[:2] != ("tools", "carte"):
            fautes.append(f"{rel} : {f.stat().st_size} octets, sans raison d'être si gros")
        with f.open("rb") as h:
            entete = h.read(0x10)
        if len(entete) == 0x10 and entete[0x0C:0x10] == b"ASFP":
            fautes.append(f"{rel} : commence comme la cartouche")
    return fautes


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--sortie", default=str(RACINE / "build" / "paquet"))
    ap.add_argument("--avec-outils", action="store_true", help="joindre outils/ (Python, CMake, GCC livrés)")
    args = ap.parse_args()

    nom = f"starfox-command-native-{version()}-windows-x64"
    sortie = Path(args.sortie).resolve()
    dossier = sortie / nom
    if dossier.exists():
        shutil.rmtree(dossier)
    dossier.mkdir(parents=True)

    suivis = subprocess.run(["git", "ls-files", "-z"], cwd=RACINE, capture_output=True, check=True).stdout
    for chemin in filter(None, suivis.decode("utf-8").split("\0")):
        cible = dossier / chemin
        cible.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(RACINE / chemin, cible)
    subprocess.run(["gcc", "-O2", "-municode", "-mwindows", "launcher/launcher.c", "launcher/habillage.c",
                    "launcher/images.c", "launcher/textes.c", "-o", str(dossier / "lanceur.exe"),
                    "-lcomctl32", "-lcomdlg32", "-lgdi32"], cwd=RACINE, check=True)
    if args.avec_outils:
        outils = RACINE / "outils"
        if not outils.is_dir():
            print("outils/ n'existe pas : le paquet se fait sans (le joueur installera Python, CMake et GCC)")
        else:
            shutil.copytree(outils, dossier / "outils", ignore=shutil.ignore_patterns("_archives"))
            manque = [d for d in ("python", "cmake", "mingw64") if not (dossier / "outils" / d).is_dir()]
            if manque:
                print("outils/ incomplet, il manque :", ", ".join(manque), "(python tools/outils.py)")
                shutil.rmtree(dossier)
                return 1
            # la notice : versions, sources officielles, licences — et où trouver les sources de GCC (GPL)
            shutil.copy2(RACINE / "docs" / "en" / "tools.md", dossier / "outils" / "README.md")
            shutil.copy2(RACINE / "docs" / "outils.md", dossier / "outils" / "LISEZMOI.md")

    fautes = controler(dossier)
    if fautes:
        for f in fautes:
            print(f"  refusé : {f}")
        shutil.rmtree(dossier)
        print("rien n'est écrit : le paquet contiendrait des fichiers qui n'ont rien à y faire")
        return 1
    archive = shutil.make_archive(str(sortie / nom), "zip", sortie, nom)
    print(f"{archive} ({Path(archive).stat().st_size // 1024} Ko) — contrôlé : rien du jeu")
    return 0


if __name__ == "__main__":
    sys.exit(main())

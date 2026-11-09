#!/usr/bin/env python3
"""Les outils de construction livrés avec une version publiée : les télécharger, les vérifier, les poser dans outils/.

Le joueur n'a rien à installer : « Préparer le jeu » trouve dans outils/ ce qu'il faut pour construire le jeu
depuis sa cartouche — Python (la version embarquable de python.org), CMake (Kitware) et GCC pour Windows
(WinLibs, MinGW-w64, UCRT). Les versions sont figées, et chaque archive est vérifiée par son empreinte SHA-256
avant d'être ouverte : ce sont exactement celles qui ont servi à éprouver la préparation (le même GCC que celui
du développement, dont le code compilé a donné des empreintes mémoire identiques).

outils/ n'est pas versionné ; tools/paquet.py --avec-outils le joint au paquet, avec sa notice
(docs/outils.md : versions, sources, licences).

Usage :
    python tools/outils.py                     télécharge, vérifie et décompresse les trois
    python tools/outils.py --depuis DOSSIER    prend les archives déjà téléchargées dans DOSSIER
    python tools/outils.py --seulement python  un seul des trois (python, cmake, gcc)

Sous licence MIT.
"""
from __future__ import annotations

import argparse
import hashlib
import shutil
import sys
import urllib.request
import zipfile
from pathlib import Path

RACINE = Path(__file__).resolve().parent.parent

OUTILS = {
    "python": {
        "url": "https://www.python.org/ftp/python/3.13.15/python-3.13.15-embed-amd64.zip",
        "sha256": "d1f04d990aee1253d8569e8e5104e30fa9f5fa830899f14843448872d936a2cf",
        "racine": None,                 # l'archive n'a pas de dossier racine
        "dossier": "python",
    },
    "cmake": {
        "url": "https://github.com/Kitware/CMake/releases/download/v4.4.5/cmake-4.4.5-windows-x86_64.zip",
        "sha256": "326c826486d46a23cf9d5a1c9ee3a567d427bdd02c4a34071937a2a675cff004",
        "racine": "cmake-4.4.5-windows-x86_64",
        "dossier": "cmake",
    },
    "gcc": {
        "url": "https://github.com/brechtsanders/winlibs_mingw/releases/download/16.2.0posix-14.0.0-ucrt-r2/"
               "winlibs-x86_64-posix-seh-gcc-16.2.0-mingw-w64ucrt-14.0.0-r2.zip",
        "sha256": "d5dbafc4a170e762ca6143151ec918fb9e2c72736fb14cd704abebc6bdd5276a",
        "racine": "mingw64",
        "dossier": "mingw64",
    },
}


def empreinte(chemin: Path) -> str:
    h = hashlib.sha256()
    with chemin.open("rb") as f:
        for bloc in iter(lambda: f.read(1 << 20), b""):
            h.update(bloc)
    return h.hexdigest()


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--depuis", help="dossier où les archives sont déjà (sous leur nom, ou dans un sous-dossier)")
    ap.add_argument("--seulement", choices=sorted(OUTILS), action="append")
    ap.add_argument("--sortie", default=str(RACINE / "outils"))
    args = ap.parse_args()
    sortie = Path(args.sortie)
    sortie.mkdir(parents=True, exist_ok=True)
    telechargements = sortie / "_archives"
    for nom in args.seulement or list(OUTILS):
        o = OUTILS[nom]
        fichier = o["url"].rsplit("/", 1)[1]
        archive = None
        if args.depuis:
            trouves = [p for p in Path(args.depuis).rglob("*.zip") if empreinte(p) == o["sha256"]]
            archive = trouves[0] if trouves else None
        if archive is None:
            telechargements.mkdir(exist_ok=True)
            archive = telechargements / fichier
            print(f"{nom} : téléchargement de {o['url']}", flush=True)
            with urllib.request.urlopen(o["url"]) as r, archive.open("wb") as f:
                shutil.copyfileobj(r, f, 1 << 20)
        vu = empreinte(archive)
        if vu != o["sha256"]:
            print(f"{nom} : empreinte inattendue ({vu}) : l'archive n'est pas celle attendue, rien n'est posé")
            return 1
        cible = sortie / o["dossier"]
        if cible.exists():
            shutil.rmtree(cible)
        print(f"{nom} : empreinte vérifiée, décompression dans {cible}", flush=True)
        with zipfile.ZipFile(archive) as z:
            if o["racine"]:
                z.extractall(sortie)
                if o["racine"] != o["dossier"]:
                    (sortie / o["racine"]).rename(cible)
            else:
                z.extractall(cible)
    if telechargements.exists():
        shutil.rmtree(telechargements)
    print("outils prêts :", ", ".join(sorted(p.name for p in sortie.iterdir() if p.is_dir())))
    return 0


if __name__ == "__main__":
    sys.exit(main())

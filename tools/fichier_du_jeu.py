#!/usr/bin/env python3
"""Lire et remplacer un fichier du jeu dans un dossier de données, archives .srl comprises.

Le jeu trouve ses fichiers par la table d'allocation de la cartouche (system/fat.bin) : un début et une
fin par fichier, dans l'espace d'adressage de la cartouche. Le dossier de données (tools/build_data.py)
sert ces adresses depuis des fichiers séparés, zone par zone (index.bin). Une archive .srl est elle-même
une petite cartouche (en-tête, noms, table d'allocation) : un fichier y a son début et sa fin de même.

Un chemin désigne un fichier du jeu tel qu'il est sous files/ (« Scripts/Missile/MissileBasic.txt-cmp »),
ou un fichier d'une archive, après deux points (« Fr_child.srl:2D/MessageText/battle_Fr.bmg-cmp »).

    liste  <dossier> [MOT] [--dans ARCHIVE.srl]   les fichiers dont le chemin contient MOT, avec zone et taille
    lire   <dossier> <chemin> [-o SORTIE]         copie le fichier ; un « -cmp » est décompressé (--brut : tel quel)
    poser  <dossier> <chemin> <fichier>           le remplace ; un « -cmp » est recompressé (--brut : tel quel)

En posant :
- un fichier qui garde sa taille, ou rapetisse, reste à sa place ; sa fin, dans la table et dans l'index,
  suit sa nouvelle taille ;
- un fichier qui grossit est rangé après le dernier, sur une frontière de 512 octets (celle des lectures de
  la cartouche), et son entrée est corrigée dans la table comme dans l'index ; sa place devient un trou ;
- dans une archive, de même dans la table de l'archive ; puis l'archive, qui a pu grossir, est posée à son
  tour.

Les zones système (en-tête, binaires, tables) et les overlays ne se remplacent pas : leur code est celui qui
a été traduit, et le changer ne changerait pas ce qui s'exécute — le comportement se modifie par les points
de modification (runtime/modifications.c). Travailler sur une copie du dossier : data/ reste la référence.

Exemples :
    python tools/fichier_du_jeu.py liste data battle --dans Fr_child.srl
    python tools/fichier_du_jeu.py lire data "Fr_child.srl:2D/MessageText/battle_Fr.bmg-cmp" -o battle_Fr.bmg
    python tools/fichier_du_jeu.py poser data_mod "Fr_child.srl:2D/MessageText/battle_Fr.bmg-cmp" battle_Fr.bmg
"""
from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from nitro.compress import lz77_compress, lz77_decompress  # noqa: E402
from nitro.rom import NdsRom  # noqa: E402

MAGIE = b"SFCD"
ALIGNEMENT = 0x200


# ------------------------------------------------------------------ le dossier de données
def lire_index(dossier: Path):
    d = (dossier / "index.bin").read_bytes()
    if d[:4] != MAGIE:
        raise SystemExit(f"{dossier}/index.bin n'est pas un index de dossier de données")
    version, nb, taille_noms = struct.unpack_from("<III", d, 4)
    if version != 1:
        raise SystemExit(f"index version {version} inconnue")
    zones = [list(struct.unpack_from("<III", d, 16 + 12 * i)) for i in range(nb)]
    noms = d[16 + 12 * nb:16 + 12 * nb + taille_noms]
    return zones, noms


def nom_de(noms: bytes, pos: int) -> str:
    fin = noms.find(b"\n", pos)
    return noms[pos:fin if fin >= 0 else len(noms)].decode("utf-8")


def ecrire_index(dossier: Path, zones, noms: bytes) -> None:
    with open(dossier / "index.bin", "wb") as f:
        f.write(MAGIE)
        f.write(struct.pack("<III", 1, len(zones), len(noms)))
        for z in sorted(zones):
            f.write(struct.pack("<III", *z))
        f.write(noms)


def normaliser(chemin: str) -> str:
    return chemin.replace("\\", "/").lstrip("/")


def poser_dans_dossier(dossier: Path, chemin: str, neuf: bytes) -> None:
    """Remplace un fichier du jeu dans le dossier : table d'allocation, index et fichier."""
    rel = "files/" + normaliser(chemin)
    if rel.lower().startswith("files/__unnamed__/"):   # les 26 overlays, sans nom dans la cartouche
        raise SystemExit("les overlays ne se remplacent pas : leur code est celui qui a été traduit")
    zones, noms = lire_index(dossier)
    k = next((i for i, z in enumerate(zones) if nom_de(noms, z[2]) == rel), None)
    if k is None:
        raise SystemExit(f"{chemin} : aucun fichier de ce nom dans le jeu (voir « liste »)")
    off, taille, pos = zones[k]
    fat = bytearray((dossier / "system" / "fat.bin").read_bytes())
    ids = [i for i in range(len(fat) // 8) if struct.unpack_from("<II", fat, 8 * i) == (off, off + taille)]
    if len(ids) != 1:
        raise SystemExit(f"{chemin} : {len(ids)} entrées de la table d'allocation lui correspondent — "
                         "ce dossier n'est plus celui de tools/build_data.py")
    n = len(neuf)
    if n <= taille:
        nouvel = off
        print(f"{chemin} : {taille} -> {n} octets, à sa place (0x{off:08X})")
    else:
        fin = max(z[0] + z[1] for z in zones)
        nouvel = (fin + ALIGNEMENT - 1) // ALIGNEMENT * ALIGNEMENT
        print(f"{chemin} : {taille} -> {n} octets, rangé en 0x{nouvel:08X} (il était en 0x{off:08X})")
    zones[k] = [nouvel, n, pos]
    struct.pack_into("<II", fat, 8 * ids[0], nouvel, nouvel + n)
    (dossier / rel).write_bytes(neuf)
    (dossier / "system" / "fat.bin").write_bytes(bytes(fat))
    ecrire_index(dossier, zones, noms)


# ------------------------------------------------------------------ les archives .srl
def ouvrir_archive(dossier: Path, archive: str) -> NdsRom:
    chemin = dossier / "files" / normaliser(archive)
    if not chemin.is_file():
        raise SystemExit(f"{archive} : pas d'archive de ce nom dans {dossier}/files")
    return NdsRom.load(chemin)


def entree_archive(rom: NdsRom, interne: str):
    interne = normaliser(interne)
    e = next((f for f in rom.files if f.path == interne), None)
    if e is None:
        raise SystemExit(f"{interne} : pas dans l'archive (voir « liste … --dans »)")
    return e


def poser_dans_archive(rom: NdsRom, interne: str, neuf: bytes) -> bytes:
    """L'archive, le fichier remplacé : en place s'il tient, sinon ajouté à la fin."""
    e = entree_archive(rom, interne)
    donnees = bytearray(rom.data)
    n = len(neuf)
    if n <= e.size:
        debut = e.offset
        donnees[debut:debut + n] = neuf
        print(f"  dans l'archive : {interne} : {e.size} -> {n} octets, à sa place")
    else:
        debut = (len(donnees) + ALIGNEMENT - 1) // ALIGNEMENT * ALIGNEMENT
        donnees += bytes(debut - len(donnees)) + neuf
        print(f"  dans l'archive : {interne} : {e.size} -> {n} octets, ajouté à la fin (0x{debut:X})")
    struct.pack_into("<II", donnees, rom.header.fat_offset + 8 * e.file_id, debut, debut + n)
    return bytes(donnees)


def separer(chemin: str):
    archive, deux_points, interne = chemin.partition(":")
    return (archive, interne) if deux_points else (None, chemin)


def lire_octets(dossier: Path, chemin: str, brut: bool = False) -> bytes:
    """Les octets d'un fichier du jeu, dans le dossier ou dans une archive ; décompressés s'il finit par -cmp."""
    archive, interne = separer(chemin)
    if archive:
        rom = ouvrir_archive(dossier, archive)
        donnees = rom.file_data(entree_archive(rom, interne))
    else:
        f = dossier / "files" / normaliser(interne)
        if not f.is_file():
            raise SystemExit(f"{interne} : pas de fichier de ce nom dans {dossier}/files")
        donnees = f.read_bytes()
    return donnees if brut or not interne.endswith("-cmp") else lz77_decompress(donnees)


def existe(dossier: Path, chemin: str) -> bool:
    archive, interne = separer(chemin)
    if archive:
        chemin_archive = dossier / "files" / normaliser(archive)
        return chemin_archive.is_file() and any(f.path == normaliser(interne) for f in NdsRom.load(chemin_archive).files)
    return (dossier / "files" / normaliser(interne)).is_file()


# ------------------------------------------------------------------ les commandes
def cmd_liste(args) -> int:
    dossier = Path(args.dossier)
    mot = (args.mot or "").lower()
    if args.dans:
        rom = ouvrir_archive(dossier, args.dans)
        for f in rom.files:
            if mot in f.path.lower():
                print(f"0x{f.offset:08X}  {f.size:9d}  {args.dans}:{f.path}")
        return 0
    zones, noms = lire_index(dossier)
    for off, taille, pos in sorted(zones):
        nom = nom_de(noms, pos)
        if nom.startswith("files/") and mot in nom.lower():
            print(f"0x{off:08X}  {taille:9d}  {nom[6:]}")
    return 0


def cmd_lire(args) -> int:
    dossier = Path(args.dossier)
    _, interne = separer(args.chemin)
    donnees = lire_octets(dossier, args.chemin, args.brut)
    nom = Path(normaliser(interne)).name
    decompresse = nom.endswith("-cmp") and not args.brut
    if decompresse:
        nom = nom[:-4]
    sortie = Path(args.o) if args.o else Path(nom)
    sortie.write_bytes(donnees)
    print(f"{args.chemin} -> {sortie} ({len(donnees)} octets{', décompressé' if decompresse else ''})")
    return 0


def cmd_poser(args) -> int:
    dossier = Path(args.dossier)
    if dossier.resolve() == (Path(__file__).resolve().parent.parent / "data").resolve() and not args.force:
        raise SystemExit("data/ est la référence : poser dans une copie (ou --force)")
    archive, interne = separer(args.chemin)
    neuf = Path(args.fichier).read_bytes()
    if normaliser(interne).endswith("-cmp") and not args.brut:
        neuf = lz77_compress(neuf)
    if archive:
        rom = ouvrir_archive(dossier, archive)
        poser_dans_dossier(dossier, archive, poser_dans_archive(rom, interne, neuf))
    else:
        poser_dans_dossier(dossier, interne, neuf)
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sous = ap.add_subparsers(dest="commande", required=True)
    p = sous.add_parser("liste", help="les fichiers dont le chemin contient un mot")
    p.add_argument("dossier")
    p.add_argument("mot", nargs="?")
    p.add_argument("--dans", metavar="ARCHIVE", help="chercher dans une archive .srl")
    p.set_defaults(fn=cmd_liste)
    p = sous.add_parser("lire", help="copier un fichier du jeu (décompressé s'il finit par -cmp)")
    p.add_argument("dossier")
    p.add_argument("chemin")
    p.add_argument("-o", metavar="SORTIE")
    p.add_argument("--brut", action="store_true", help="ne pas décompresser")
    p.set_defaults(fn=cmd_lire)
    p = sous.add_parser("poser", help="remplacer un fichier du jeu (recompressé s'il finit par -cmp)")
    p.add_argument("dossier")
    p.add_argument("chemin")
    p.add_argument("fichier")
    p.add_argument("--brut", action="store_true", help="poser tel quel, sans compresser")
    p.add_argument("--force", action="store_true", help="accepter de modifier data/ lui-même")
    p.set_defaults(fn=cmd_poser)
    args = ap.parse_args()
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())

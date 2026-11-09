#!/usr/bin/env python3
"""Les classes que le moteur enregistre : leur nom et leur taille, lus dans le code.

Chaque overlay déclare ses types au démarrage, par une longue suite d'appels tous
bâtis sur le même moule : on empile la taille de l'objet, deux pointeurs de table,
on charge le nom, et on appelle la fonction d'enregistrement.

    sp[0] = 0x80            la taille de l'objet
    sp[8] = 0x020D9588      sa table de méthodes
    r1    = "ActorGroup"    son nom
    r3    = 4               son alignement
    bl 0x02087484           enregistrer

Il suffit donc de suivre les valeurs posées dans les registres et sur la pile, puis
de les relever à chaque appel. Ce qu'on obtient est la carte des structures du jeu :
combien d'octets fait un acteur, une cellule de collision, une requête de proximité.

Usage :
    python tools/classes.py <rom.nds> [--overlay 0] [--fonction 0x021751C0]
                            [--enregistrer 0x02087484] [-o docs/classes.md]
"""
from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

RACINE = Path(__file__).resolve().parent

# « adresse  octets  opcode  arguments » puis, selon les cas, « = valeur "texte" »
# pour une constante du pool, ou « -> 0x… » pour la cible d'un appel.
LIGNE = re.compile(r'^([0-9A-F]{8})\s+[0-9a-f]+\s+(\S+)\s+(.*)$')
LITTERAL = re.compile(r'=\s*(0x[0-9A-Fa-f]+|\d+)')
CIBLE = re.compile(r'->\s*(0x[0-9A-Fa-f]+)')
TEXTE = re.compile(r'"([^"]*)"')


def desassembler(rom: str, adresse: int, longueur: int, overlay: int | None) -> list[str]:
    cmd = [sys.executable, str(RACINE / 'disasm.py'), rom, hex(adresse), '--length', str(longueur)]
    if overlay is not None:
        cmd += ['--overlay', str(overlay)]
    return subprocess.run(cmd, capture_output=True, text=True, check=True).stdout.splitlines()


def suivre(lignes: list[str], enregistrer: int):
    """Interprète ce qui pose des valeurs, et relève chaque appel d'enregistrement."""
    reg: dict[str, int] = {}
    nom_de: dict[int, str] = {}
    pile: dict[int, int] = {}
    trouvees = []

    for ligne in lignes:
        m = LIGNE.match(ligne.rstrip())
        if not m:
            continue
        _adr, op, reste = m.groups()
        args = reste.split('=')[0].split('->')[0].strip()
        lit = LITTERAL.search(reste)
        cible = CIBLE.search(reste)
        txt = TEXTE.search(reste)
        valeur = int(lit.group(1), 0) if lit else (int(cible.group(1), 0) if cible else None)
        if txt and lit:
            nom_de[int(lit.group(1), 0)] = txt.group(1)

        # ldr rX, [pc, …]  — une constante du pool
        mm = re.match(r'^(r\d+),\s*\[pc', args)
        if op == 'ldr' and mm and valeur is not None:
            reg[mm.group(1)] = valeur
            continue
        # movs rX, #imm
        mm = re.match(r'^(r\d+),\s*#(0x[0-9a-fA-F]+|\d+)$', args)
        if op in ('movs', 'mov') and mm:
            reg[mm.group(1)] = int(mm.group(2), 0)
            continue
        # str rX, [sp, #n]  /  str rX, [sp]
        mm = re.match(r'^(r\d+),\s*\[sp(?:,\s*#(0x[0-9a-fA-F]+|\d+))?\]$', args)
        if op == 'str' and mm:
            off = int(mm.group(2), 0) if mm.group(2) else 0
            if mm.group(1) in reg:
                pile[off] = reg[mm.group(1)]
            else:
                pile.pop(off, None)
            continue
        # l'appel qui enregistre
        if op == 'bl' and valeur == enregistrer:
            trouvees.append({
                'nom': nom_de.get(reg.get('r1', -1), ''),
                'taille': pile.get(0),
                'zero': pile.get(4),
                'table': pile.get(8),
                'seconde': pile.get(0xC),
                'alignement': reg.get('r3'),
            })
            continue
        # un appel quelconque brouille les registres d'argument
        if op in ('bl', 'blx'):
            for r in ('r0', 'r1', 'r2', 'r3'):
                reg.pop(r, None)
    return trouvees


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('rom')
    ap.add_argument('--overlay', type=int, default=0)
    ap.add_argument('--fonction', default='0x021751C0')
    ap.add_argument('--longueur', type=int, default=8312)
    ap.add_argument('--enregistrer', default='0x02087484')
    ap.add_argument('--codemap', default='analysis/codemap.json')
    ap.add_argument('--tout', action='store_true',
                    help="parcourir tous les modules, en cherchant qui enregistre")
    ap.add_argument('-o', '--out', default='')
    args = ap.parse_args()

    enregistrer = int(args.enregistrer, 0)
    travaux = []                      # (module, overlay, adresse, longueur)
    if args.tout:
        codemap = json.loads(Path(args.codemap).read_text(encoding='utf-8'))
        for module, fns in codemap.items():
            ovl = None if module == 'arm9' else int(module[3:])
            for f in fns:
                if any((a & ~1) == enregistrer for a in f['appels']):
                    travaux.append((module, ovl, f['entrée'], f['taille']))
    else:
        travaux.append((f'ovl{args.overlay}' if args.overlay >= 0 else 'arm9',
                        args.overlay if args.overlay >= 0 else None,
                        int(args.fonction, 0), args.longueur))

    par_module: dict[str, list] = {}
    for i, (module, ovl, adresse, longueur) in enumerate(travaux):
        for c in suivre(desassembler(args.rom, adresse, longueur, ovl), enregistrer):
            if c['nom']:
                c['ou'] = adresse
                par_module.setdefault(module, []).append(c)
        if args.tout and (i + 1) % 25 == 0:
            print(f'  … {i + 1}/{len(travaux)} fonctions lues', file=sys.stderr)

    def cle(nom):
        return (0, 0) if nom == 'arm9' else (1, int(nom[3:]))

    total = sum(len(v) for v in par_module.values())
    texte = ["# Les classes du moteur", "",
             "Chaque module déclare ses types au démarrage, en donnant à chacun son nom et sa",
             "taille. Ce tableau les relève dans le code lui-même : ce sont les structures du",
             "jeu, avec le nombre d'octets qu'il leur alloue.", "",
             f"Produit par `tools/classes.py --tout` ; {total} déclarations dans "
             f"{len(par_module)} modules.", ""]
    for module in sorted(par_module, key=cle):
        vues = {}
        for c in par_module[module]:
            vues.setdefault((c['nom'], c['taille']), c)
        texte += [f'## {module}', '', '| classe | taille | déclarée dans |', '|---|---:|---|']
        for (nom, taille), c in sorted(vues.items()):
            texte.append(f"| `{nom}` | {taille if taille is not None else '?'} | `0x{c['ou']:08X}` |")
        texte.append('')

    sortie = '\n'.join(texte) + '\n'
    if args.out:
        Path(args.out).write_text(sortie, encoding='utf-8')
        print(f'{total} déclarations -> {args.out}')
    else:
        print(sortie, end='')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())

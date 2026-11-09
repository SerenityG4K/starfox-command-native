#!/usr/bin/env python3
"""Ce que le binaire dit de lui-même : un annuaire des fonctions nommées.

Le jeu n'a pas de symboles, mais il n'est pas muet pour autant. Trois sources le
font parler, et chacune vaut par sa force de preuve :

* les **tables d'enregistrement** (`tools/arm9map.py`) : un nom et l'adresse de la
  fonction, côte à côte. C'est une certitude ;
* les **chaînes d'identifiants** que le code désigne — « StrategyScene »,
  « MainObjPlane », « CreatePlayer ». La fonction qui nomme un objet est presque
  toujours celle qui le construit, mais « presque » n'est pas « toujours » : on
  enregistre la mention, pas une conclusion ;
* les **assertions** restées dans le code, qui donnent le fichier source d'origine
  et situent donc des pans entiers du binaire.

Ce qui sort d'ici est un faisceau d'indices daté et traçable, pas un jeu de
symboles : chaque entrée porte sa source, pour qu'on sache toujours ce qu'on
sait et comment on l'a su.

Usage :
    python tools/symboles.py [--data data] [--codemap analysis/codemap.json]
                             [-o analysis/symboles.json]
"""
from __future__ import annotations

import argparse
import json
import re
import struct
import sys
from bisect import bisect_right
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from nitro import compress  # noqa: E402

# Un identifiant de programmeur : des lettres, des chiffres, des tirets bas, au
# moins une majuscule ou un tiret bas pour écarter les mots de la langue courante.
IDENT = re.compile(r'^[A-Za-z_][A-Za-z0-9_]{3,47}$')
CHAINE = re.compile(rb'[ -~]{4,120}\x00')
SOURCE = re.compile(r'\.(cpp|cc|hpp|h|c)$')


def modules(racine: Path):
    """(nom, adresse RAM, octets) pour le binaire ARM9 puis chaque overlay."""
    entete = (racine / 'system/header.bin').read_bytes()
    arm9 = compress.blz_decompress((racine / 'system/arm9.bin').read_bytes())
    yield 'arm9', struct.unpack_from('<I', entete, 0x28)[0], arm9

    ovt = (racine / 'system/ovt9.bin').read_bytes()
    for i in range(len(ovt) // 32):
        oid, ram, taille, _bss, _s1, _s2, fid, _dr = struct.unpack_from('<8I', ovt, i * 32)
        image = (racine / f'files/__unnamed__/{fid:04d}.bin').read_bytes()
        yield f'ovl{oid}', ram, image[:taille]


def fonctions_du_module(codemap: dict, nom: str):
    """Bornes triées des fonctions : (début, fin, entrée)."""
    liste = []
    for f in codemap.get(nom, []):
        liste.append((f['début'], f['fin'], f['entrée']))
    liste.sort()
    return liste


def fonction_contenant(bornes, debuts, adresse: int):
    """La fonction dont les bornes couvrent cette adresse, ou None.

    Les pools de littéraux vivent dans la fonction qui les utilise ; une adresse
    qui tombe hors de toute fonction n'est rattachée à personne, faute de preuve.
    """
    i = bisect_right(debuts, adresse) - 1
    if i < 0:
        return None
    debut, fin, entree = bornes[i]
    return entree if debut <= adresse < fin else None


def analyser(racine: Path, codemap: dict):
    entrees: dict[int, dict] = {}

    def noter(adresse: int, module: str, cle: str, valeur: str):
        e = entrees.setdefault(adresse, {'module': module, 'mentionne': [], 'source': [], 'nom': ''})
        if valeur not in e[cle]:
            e[cle].append(valeur)

    total_chaines = 0
    for nom, base, donnees in modules(racine):
        bornes = fonctions_du_module(codemap, nom)
        debuts = [b[0] for b in bornes]
        if not bornes:
            continue

        # les mots du module, pour savoir quelles chaînes le code désigne
        mots: dict[int, list[int]] = {}
        for i in range(0, len(donnees) - 3, 4):
            v = struct.unpack_from('<I', donnees, i)[0]
            if base <= v < base + len(donnees):
                mots.setdefault(v, []).append(base + i)

        for m in CHAINE.finditer(donnees):
            adresse = base + m.start()
            texte = m.group()[:-1].decode('latin1')
            refs = mots.get(adresse)
            if not refs:
                continue
            est_source = bool(SOURCE.search(texte))
            if not est_source and not IDENT.match(texte):
                continue
            if not est_source and texte.islower() and '_' not in texte:
                continue          # un mot ordinaire n'est pas un identifiant
            total_chaines += 1
            for ref in refs:
                cible = fonction_contenant(bornes, debuts, ref)
                if cible is not None:
                    noter(cible, nom, 'source' if est_source else 'mentionne', texte)

    return entrees, total_chaines


def fusionner_enregistrements(entrees: dict, carte: Path):
    """Les tables d'enregistrement donnent le nom d'une fonction sans ambiguïté."""
    if not carte.exists():
        return 0
    n = 0
    module = 'arm9'
    for ligne in carte.read_text(encoding='utf-8').splitlines():
        # Les overlays partagent leurs adresses : seul le titre de section dit
        # duquel on parle.
        titre = re.match(r'###\s+(ARM9|Overlay\s+(\d+))', ligne)
        if titre:
            module = 'arm9' if titre.group(2) is None else f'ovl{titre.group(2)}'
            continue
        m = re.match(r'\|\s*`([A-Za-z_][\w:]*)`\s*\|\s*`0x([0-9A-Fa-f]{8})`\s*\|\s*(ARM|Thumb)\s*\|', ligne)
        if not m:
            continue
        nom, adresse = m.group(1), int(m.group(2), 16)
        e = entrees.setdefault(adresse, {'module': module, 'mentionne': [], 'source': [], 'nom': ''})
        e['nom'] = nom
        e['module'] = e['module'] or module
        n += 1
    return n


def ecrire_markdown(entrees: dict, chemin: Path) -> None:
    """Le même contenu, mais lisible — et rangé par module, parce que la question
       qu'on se pose devant ce jeu est d'abord « à quoi sert cet overlay ? »."""
    par_module: dict[str, list] = {}
    for adresse, e in sorted(entrees.items()):
        par_module.setdefault(e['module'] or 'arm9', []).append((adresse, e))

    def cle(nom):
        return (0, 0) if nom == 'arm9' else (1, int(nom[3:]))

    lignes = ['# Annuaire du binaire',
              '',
              "Ce que le jeu dit de lui-même, par ordre de force de preuve. Rien ici n'est",
              "deviné : chaque ligne cite la chaîne ou la table d'où elle vient.",
              '',
              '- **Nom certain** : une table d\'enregistrement associe ce nom à cette adresse.',
              '- **Mention unique** : la fonction désigne un seul identifiant. C\'est presque',
              '  toujours celle qui crée l\'objet de ce nom — presque, pas toujours.',
              '- **Mentions multiples** : une fabrique, un registre ou un aiguillage ; le',
              '  faisceau dit de quoi la fonction s\'occupe, pas comment elle s\'appelle.',
              '',
              f"Produit par `tools/symboles.py` ; {sum(len(v) for v in par_module.values())} fonctions touchées.",
              '']

    for module in sorted(par_module, key=cle):
        fonctions = par_module[module]
        certains = [(a, e) for a, e in fonctions if e['nom']]
        uniques = [(a, e) for a, e in fonctions if not e['nom'] and len(e['mentionne']) == 1]
        multiples = [(a, e) for a, e in fonctions if not e['nom'] and len(e['mentionne']) > 1]
        sources = sorted({s for _, e in fonctions for s in e['source']})

        lignes.append(f'## {module}')
        lignes.append('')
        if sources:
            lignes.append('Fichiers source nommés par ses assertions : ' +
                          ', '.join(f'`{s}`' for s in sources) + '.')
            lignes.append('')

        if certains:
            lignes += ['### Noms certains', '', '| adresse | nom |', '|---|---|']
            lignes += [f'| `{a}` | `{e["nom"]}` |' for a, e in certains] + ['']
        if uniques:
            lignes += ['### Mention unique', '', '| adresse | mention |', '|---|---|']
            lignes += [f'| `{a}` | `{e["mentionne"][0]}` |' for a, e in uniques] + ['']
        if multiples:
            lignes += ['### Mentions multiples', '', '| adresse | mentions |', '|---|---|']
            for a, e in multiples:
                m = ', '.join(f'`{x}`' for x in e['mentionne'][:10])
                if len(e['mentionne']) > 10:
                    m += f' … (+{len(e["mentionne"]) - 10})'
                lignes.append(f'| `{a}` | {m} |')
            lignes.append('')

    chemin.write_text('\n'.join(lignes) + '\n', encoding='utf-8')


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--data', default='data')
    ap.add_argument('--codemap', default='analysis/codemap.json')
    ap.add_argument('--carte', default='docs/carte_binaire.md')
    ap.add_argument('-o', '--out', default='analysis/symboles.json')
    ap.add_argument('-m', '--markdown', default='docs/annuaire.md')
    args = ap.parse_args()

    codemap = json.loads(Path(args.codemap).read_text(encoding='utf-8'))
    entrees, chaines = analyser(Path(args.data), codemap)
    nommees = fusionner_enregistrements(entrees, Path(args.carte))

    sortie = {f'0x{a:08X}': e for a, e in sorted(entrees.items())}
    Path(args.out).write_text(json.dumps(sortie, indent=1, ensure_ascii=False), encoding='utf-8')
    if args.markdown:
        ecrire_markdown(sortie, Path(args.markdown))

    avec_source = sum(1 for e in entrees.values() if e['source'])
    print(f'{chaines} chaînes retenues, {len(entrees)} fonctions touchées')
    print(f'  {nommees} nommées par une table d\'enregistrement (certain)')
    print(f'  {avec_source} rattachées à un fichier source')
    print(f'-> {args.out}' + (f' et {args.markdown}' if args.markdown else ''))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())

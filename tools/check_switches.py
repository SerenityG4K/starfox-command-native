"""Tables de sauts et fonctions suspectes dans une carte (analysis/codemap.json).

Une table CodeWarrior saute à quelques dizaines d'octets ou de kilo-octets au plus, à
l'intérieur de sa fonction. Une table dont les cibles s'étalent sur des kilo-octets, ou qui
vise loin en arrière, a été lue trop loin : la fonction avale alors le code voisin, et une
adresse réellement appelée s'y retrouve sans étiquette (arrêt fatal à l'exécution).

Une alerte n'est pas un verdict : un vrai aiguillage géant existe (ovl3 0x021FD194, 14 cas
sur 20 Ko). Vérifier le code du saut avant de conclure.

Usage : python tools/check_switches.py [codemap.json] [--etendue N] [--taille N]
"""
import argparse
import io
import json
import sys

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8')


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('codemap', nargs='?', default='analysis/codemap.json')
    ap.add_argument('--etendue', type=lambda s: int(s, 0), default=0x2000,
                    help="étendue maximale des cibles d'une table (défaut 0x2000)")
    ap.add_argument('--taille', type=lambda s: int(s, 0), default=20000,
                    help='taille maximale plausible pour une fonction (défaut 20000)')
    args = ap.parse_args()
    cm = json.load(open(args.codemap, encoding='utf-8'))

    tables, big = [], []
    for region, fns in cm.items():
        for f in fns:
            if f['taille'] > args.taille:
                big.append((region, f['entrée'], f['taille'], f['blocs']))
            for site, targets in f.get('sauts', {}).items():
                if not targets:
                    continue
                lo, hi = min(targets), max(targets)
                site = int(site)
                if hi - lo > args.etendue or lo < site - args.etendue:
                    tables.append((region, f['entrée'], site, len(targets), lo, hi))

    for region, entry, site, n, lo, hi in sorted(tables):
        print(f"table étalée : {region} 0x{entry:08X}, saut 0x{site:08X} : {n} entrées, "
              f"0x{lo:08X}-0x{hi:08X} (étendue {hi - lo:#x})")
    for region, entry, size, blocks in sorted(big, key=lambda x: -x[2]):
        print(f"fonction géante : {region} 0x{entry:08X} : {size} octets, {blocks} blocs")
    print(f"{len(tables)} tables étalées, {len(big)} fonctions de plus de {args.taille} octets")
    return 1 if tables or big else 0


if __name__ == '__main__':
    raise SystemExit(main())

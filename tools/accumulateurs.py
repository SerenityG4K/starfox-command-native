"""Tire de l'étude des accumulateurs (SFC_ETUDE_ACC) la liste des sites que les tours d'aperçu mettent à
l'échelle : ceux qu'on a vus intégrer — une adresse écrite une fois par tour, d'un pas qui n'est pas une
simple unité. Les autres restent l'écriture d'origine : une construction (une adresse réécrite dans le tour),
un compteur qu'on avance d'un, un site jamais vu.

usage : python tools/accumulateurs.py etude1.txt [etude2.txt…] > tools/accumulateurs_inclus.txt"""
import sys

# redirigée dans un fichier, la sortie prendrait l'encodage de la console Windows (cp1252), et le
# générateur, qui relit la liste en UTF-8, s'arrêterait sur l'accent de l'en-tête
sys.stdout.reconfigure(encoding='utf-8')
sites = {}
for fichier in sys.argv[1:]:
    for l in open(fichier, encoding='utf-8'):
        if l.startswith('#'):
            continue
        p = l.split()
        s = sites.setdefault(int(p[0], 16), [0, 0, 0, 0.0])
        s[0] += int(p[1]); s[1] += int(p[2]); s[2] += int(p[3]); s[3] += float(p[4]) * int(p[1])
print('# sites mis à l\'échelle par les tours d\'aperçu — produit par tools/accumulateurs.py')
n = 0
for site, (ecr, tours, doubles, somme) in sorted(sites.items()):
    if doubles == 0 and ecr and somme / ecr > 1.0:
        print(f'{site:08X} {ecr} {tours} {somme / ecr:.1f}')
        n += 1
print(f'{n} sites sur {len(sites)}', file=sys.stderr)

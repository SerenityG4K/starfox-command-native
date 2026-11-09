"""Rattache les échantillons de SFC_ECHANTILLONS aux fonctions de l'exécutable.
usage : python tools/echantillons.py echantillons.txt build/runtime8/sfc.exe [n]

Deux classements : où le fil du jeu se trouvait (fonction de l'exécutable, ou module étranger entre
crochets), et, pour les échantillons pris hors de l'exécutable, la première fonction de l'exécutable
en remontant la pile — celle qui a appelé la bibliothèque ou le système."""
import bisect
import collections
import subprocess
import sys

fichier, exe = sys.argv[1], sys.argv[2]
n = int(sys.argv[3]) if len(sys.argv) > 3 else 40
lignes = open(fichier).read().split('\n')
base = int(lignes[0].split()[1], 16)
ech = []
for l in lignes[1:]:
    p = l.split()
    if p:
        ech.append((int(p[0], 16), int(p[1]), p[2] if len(p) > 2 else None))
syms = []
for l in subprocess.run(['nm', '-n', exe], capture_output=True, text=True).stdout.split('\n'):
    p = l.split()
    if len(p) == 3 and p[1] in 'tT':
        syms.append((int(p[0], 16), p[2]))
syms.sort()
adr = [a for a, _ in syms]
defaut = 0x140000000           # nm donne les adresses pour l'image chargée à sa base par défaut


def fonction(rip):
    i = bisect.bisect_right(adr, rip - base + defaut) - 1
    return syms[i][1] if i >= 0 else '?'


par = collections.Counter()
appelants = collections.Counter()
total = 0
for rip, c, module in ech:
    if module == '@appelant':
        appelants[fonction(rip)] += c
        continue
    total += c
    par['[' + module + ']' if module else fonction(rip)] += c
print(total, 'échantillons')
for nom, c in par.most_common(n):
    print('%6.2f %%  %6d  %s' % (100.0 * c / total, c, nom))
print()
print('hors de l\'exécutable, appelés depuis :')
for nom, c in appelants.most_common(n // 2):
    print('%6.2f %%  %6d  %s' % (100.0 * c / total, c, nom))

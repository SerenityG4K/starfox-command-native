#!/bin/bash
# Quelles fonctions du générateur, mises en service, changent les empreintes des N premières images
# d'un rejeu ? Dichotomie sur les rangs (SFC_NATIF_TRANCHE), répétée en écartant chaque coupable
# trouvé (SFC_NATIF_SANS) jusqu'à ce que les empreintes soient celles de la référence. À lancer
# depuis le dossier de build :
#
#     bash ../../tools/banc/dicho_tout.sh 900 auto-tout ../mission_anneaux.sav ../mission_anneaux.txt
#
# N mode sauvegarde entrées : le mode est celui de SFC_NATIF (auto : les prouvées ; auto-tout :
# toutes). Les adresses des coupables s'ajoutent à coupables.txt.
#
# La dichotomie suppose un monde reproductible. Elle a un jour « désigné » deux fonctions jamais
# appelées : c'était la référence elle-même qui changeait d'une exécution à l'autre, sous charge
# (le son se recalait sur la carte son). D'où les deux garde-fous : la référence est jouée deux
# fois avant de chercher, et tout coupable est rejugé seul.
export SFC_DATA="${SFC_DATA:-$(cd ../.. && pwd -W 2>/dev/null || pwd)/data}"
N=${1:-300}; MODE=${2:-auto-tout}; SAV=${3:-../mission_anneaux.sav}; ENTREES=${4:-../mission_anneaux.txt}
touch coupables.txt
SANS=$(paste -sd, coupables.txt)
jouer() {   # $1 : fichier d'empreintes ; le reste : variables d'environnement
  out=$1; shift
  if [ -f "$SAV" ]; then cp "$SAV" dicho.sav; else rm -f dicho.sav; fi
  env SFC_ARM7=1 SFC_NO_VIDEO=1 SFC_MAX_FRAMES=$N SFC_HASH=$out SFC_INPUT_REPLAY="$ENTREES" SFC_SAVE=dicho.sav "$@" ./sfc.exe 900 > dicho.log 2>&1
}
essai() { jouer dicho_h.txt SFC_NATIF=$MODE SFC_NATIF_TRANCHE=$1 SFC_NATIF_SANS="$SANS"; cmp -s dicho_h.txt dicho_ref.txt; }
jouer dicho_ref.txt SFC_NATIF=non            # la référence : la traduction pure, qui n'est plus le défaut
jouer dicho_ref2.txt SFC_NATIF=non
cmp -s dicho_ref.txt dicho_ref2.txt || { echo "la référence n'est pas reproductible : $(cmp dicho_ref.txt dicho_ref2.txt) — rien à chercher ici"; exit 3; }
while :; do
  a=0; b=40000
  if essai $a-$b; then echo "tout identique sur $N images, sans : $SANS"; exit 0; fi
  while [ $((b - a)) -gt 1 ]; do m=$(( (a + b) / 2 )); if essai $a-$m; then a=$m; else b=$m; fi; done
  adr=$(python -c "
import re
e = []
for k in range(16):
    e += re.findall(r'^    \{\"\w+\", (0x[0-9A-F]+)u, a_', open('../../generated/natif_auto_%d.c' % k, encoding='utf-8').read(), re.M)
print(e[$a])")
  echo "coupable : rang $a = $adr"
  if essai $a-$((a + 1)); then echo "… mais seule, elle ne change rien : une interaction, ou un monde qui n'est pas reproductible"; fi
  if grep -qx "$adr" coupables.txt; then echo "déjà écartée, et pourtant : on s'arrête"; exit 2; fi
  [ $(wc -l < coupables.txt) -ge 40 ] && { echo "trop de coupables"; exit 2; }
  echo "$adr" >> coupables.txt
  SANS=$(paste -sd, coupables.txt)
done

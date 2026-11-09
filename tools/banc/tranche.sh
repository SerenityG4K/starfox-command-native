#!/bin/bash
# Une tranche du banc à froid (SFC_VERIF_FROID), relancée tant que le garde écarte une fonction
# qui ne rend pas la main — une machine à états nourrie de valeurs tirées au sort. À lancer depuis
# le dossier de build (build/runtime8), autant de fois qu'il y a de tranches :
#
#     for k in $(seq 0 13); do bash ../../tools/banc/tranche.sh $k 14 $((k + 101)) [liste] & done
#
# k n graine [liste] : la tranche k sur n, la graine des tirages, et — pour une seconde passe —
# le fichier des fonctions à reprendre (analysis/feuilles_partielles.txt, qu'écrit le générateur).
# SFC_FROID_BAVARD=1 (à poser soi-même) nomme chaque fonction et chaque essai : un gigaoctet de
# journal pour une passe de 3 000 essais — à garder pour une enquête.
# Les verdicts vont dans froid/p<k>_<essai>.liste ; froid/p<k>.fini marque la fin.
export SFC_DATA="${SFC_DATA:-$(cd ../.. && pwd -W 2>/dev/null || pwd)/data}"
export SFC_FROID_EXCLURE=froid/exclues.txt SFC_ARM7=1 SFC_NO_VIDEO=1
k=$1; n=$2
export SFC_FROID_GRAINE=$3
[ -n "$4" ] && export SFC_FROID_LISTE="$4"
mkdir -p froid && touch froid/exclues.txt
essai=0
while :; do
  essai=$((essai + 1))
  rm -f froid/p${k}.sav
  SFC_VERIF_FROID=300:$k/$n SFC_VERIF_LISTE=froid/p${k}_$essai.liste SFC_SAVE=froid/p${k}.sav ./sfc.exe 3600 > froid/p${k}_$essai.log 2>&1
  [ $? -eq 3 ] || break
  [ $essai -ge 30 ] && break
done
touch froid/p${k}.fini

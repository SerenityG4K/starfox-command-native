#!/bin/bash
# Le générateur sur le coprocesseur : mêmes options que pour l'ARM9, sa carte et ses fichiers à lui.
# usage, depuis la racine du dépôt : bash tools/banc/generer7.sh [--moisson listes…]
python tools/decomp_feuilles.py --codemap analysis/codemap_arm7.json --suffixe 7 --parties 2 \
  --out generated7/natif_auto7.inc --json analysis/feuilles7.json \
  --verdicts analysis/feuilles7_verdicts.json --partielles analysis/feuilles7_partielles.txt \
  --milieux tools/entrees_milieu7.txt "$@"

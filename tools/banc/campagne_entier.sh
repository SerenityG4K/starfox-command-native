#!/bin/bash
# Longs rejeux en double : la traduction pure, puis le jeu entier en natif — empreintes et son comparés.
# usage : campagne_entier.sh nom...
export SFC_DATA="${SFC_DATA:-$(cd ../.. && pwd -W 2>/dev/null || pwd)/data}"
un() {  # nom mode
  local n=$1 m=$2 images=$(wc -l < ../replay_$1.txt) natif=non      # la référence : la traduction pure
  [ -n "$3" ] && natif=entier
  rm -f longs/${n}_$m.sav
  env SFC_ARM7=1 SFC_NO_VIDEO=1 SFC_MAX_FRAMES=$images SFC_HASH=longs/${n}_$m.txt SFC_WAV=longs/${n}_$m.wav \
      SFC_INPUT_REPLAY=../replay_$n.txt SFC_SAVE=longs/${n}_$m.sav SFC_NATIF=$natif ${3:+SFC_VERIF_LISTE=longs/${n}_entier.liste SFC_TRADUIT=longs/${n}_traduit.txt} \
      ${SFC_EXE:-./sfc.exe} 14400 > longs/${n}_$m.log 2>&1
}
for n in "$@"; do un $n ref & un $n entier 1 & done
wait
for n in "$@"; do
  e=DIFFERENT; cmp -s longs/${n}_ref.txt longs/${n}_entier.txt && e=identiques
  w=DIFFERENT; cmp -s longs/${n}_ref.wav longs/${n}_entier.wav && w=identique
  echo "$n : $(wc -l < longs/${n}_ref.txt) images ; empreintes $e ; son $w ($(stat -c %s longs/${n}_ref.wav) octets) ; $(grep -a '^traduction' longs/${n}_entier.log | cut -c1-70 | tr '\n' '|')"
  rm -f longs/${n}_ref.wav longs/${n}_entier.wav
done
echo "campagne finie"

# Audio — archive Nitro Composer `.sdat`

```bash
python tools/sdat.py assets/cooked/StarfoxDS_Eu.sdat -o assets/audio --names assets/cooked/sound_data.sadl
```

Les fichiers produits sous `assets/audio/` sont des données du jeu et restent
hors du dépôt.

## Ce que contient l'archive

`StarfoxDS_Eu.sdat` (3,6 Mo) est une archive Nitro Composer standard, à deux
particularités près : elle n'a **pas de bloc SYMB** (l'offset vaut zéro, donc
aucun nom), et `StarfoxDS_Uk.sdat` lui est **identique octet pour octet** — la
seconde copie est redondante.

| Fiches INFO | Nombre | Rôle |
|---|---:|---|
| `SEQ` | 137 | séquences musicales et jingles (`SSEQ`) |
| `SEQARC` | 183 | archives de séquences (`SSAR`), typiquement les effets sonores |
| `BANK` | 231 | banques d'instruments (`SBNK`) |
| `WAVEARC` | 5 | archives d'ondes (`SWAR`) : **376 échantillons** |
| `PLAYER`, `GROUP` | 10, 176 | lecteurs et groupes de chargement |
| `STRM` | **0** | aucun flux |

Conclusion : **toute la musique est séquencée**, à la manière standard du DS —
une séquence `SSEQ` pilote une banque `SBNK` qui puise dans les ondes `SWAR`.
La rejouer demande un séquenceur et un synthétiseur (enveloppes, courbures de
hauteur, modulation, canaux PSG et bruit) : c'est un chantier à part entière,
bien balisé par les outils communautaires existants.

## Ce qui est extrait

Les 376 échantillons `SWAV` des cinq archives, en WAV 16 bits : 265 secondes
au total, de 8 à 44,1 kHz. Ce sont les instruments de la bande-son et, très
probablement, les effets sonores eux-mêmes. Les fréquences irrégulières
(16 004 Hz, 22 053 Hz…) viennent de l'encodage de la cadence par le minuteur du
DS et sont exactes.

Les séquences et banques sont cataloguées (index, fichier, taille) dans
`assets/audio/catalogue.json`, sans être rendues.

## Le fichier `sound_data.sadl`

Ce n'est pas de l'audio : c'est un en-tête C de 1,4 Ko —
`#define STRM_SE_Laser 0`, … — qui numérote 46 identifiants du registre sonore,
dont trois musiques : `STRM_BGM_Arwing`, `STRM_BGM_Walker`,
`STRM_BGM_Greatfox`. Le préfixe `STRM` est historique : ces identifiants
renvoient à des séquences et des échantillons, pas à des flux. Les scripts les
déclarent par `RegistSound SE_Laser 1` (46 appels) et lancent la musique par
`startBgm 1`.

## Suite

1. Nommer les 376 échantillons en croisant les `RegistSound` des scripts, les
   identifiants du `.sadl` et les banques qui référencent chaque onde.
2. Rejouer les `SSEQ` : séquenceur + synthétiseur, ou export MIDI + SoundFont
   comme première approximation.

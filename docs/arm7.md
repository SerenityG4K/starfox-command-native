# Binaire ARM7 — structure, en vue de sa recompilation

Le composant ARM7 du NitroSDK (pilote son, écran tactile, horloge, alimentation,
sans-fil) n'est pas encore recompilé : `runtime/arm7_hle.c` répond à sa place
au protocole PXI. Sa recompilation avec la même chaîne (`tools/recomp`) est
la voie prévue pour le son bit-exact. Ce que la ROM en dit :

| Élément | Valeur |
|---|---|
| Image | 165 552 octets, chargée en `0x02380000` (RAM principale), entrée `0x02380000` |
| `ModuleParams` | `0x02380198` : liste autoload `0x023A8698`–`0x023A86B0` (2 sections), autoload/BSS en `0x023801B0` |
| Sections recopiées | dans la WRAM contiguë `0x037F8000`–`0x0380FFFF` (32 Ko de WRAM partagée + 64 Ko de WRAM ARM7) |
| `main` | `0x037F8468` (Thumb) |
| Gestionnaire d'IRQ | `0x037FB8F4`, installé en `0x0380FFFC` |
| Piles | SVC `0x0380FFC0`, IRQ `0x0380FF80`, système en dessous (`- 0x400`) |
| Zone partagée | 0x160 octets recopiés en `0x027FFA80` (informations OS, tampons RTC/tactile), 0x20 en `0x027FFBE0` |
| Version SDK | chaîne `2.74.00` (composant son ?) |

## Ce que son crt0 fait

1. `IME = 0` ; efface la WRAM ARM7 (`0x03800000`–`0x0380FF00`).
2. Pose les piles SVC, IRQ, système.
3. Recopie les tables partagées en RAM principale.
4. Autoload (`bl 0x02380100`) : sections vers `0x037F8000`.
5. Efface le BSS, initialisation statique (`bl 0x0238015C`).
6. Installe le gestionnaire d'IRQ et saute à `main` (`bx 0x037F8468`).

## Ce dont sa recompilation aura besoin

- Une image mémoire ARM7 pour la cartographie : RAM principale (image en
  `0x02380000`), WRAM (`0x037F8000`–`0x0380FFFF`) remplie par l'autoload —
  `tools/nitro/memory.py` à généraliser (le `ModuleParams` de l'ARM7 n'a pas
  le marqueur `0xDEC00621` ; ses champs sont à l'adresse fixe `0x02380198`).
- Les appels BIOS de l'ARM7 (`svc` Thumb 0–28) : outre CpuSet, Div, Sqrt et
  les décompresseurs, les tables du son (`0x1A` GetSineTable, `0x1B`
  GetPitchTable, `0x1C` GetVolumeTable) et `0x17` SoundBias, à recalculer
  par leurs formules.
- Les registres ARM7 : son (`0x04000400`–`0x0400051F`, seize voix ; un mixeur
  vers WASAPI), SPI (`0x040001C0` : tactile, firmware, gestion d'énergie),
  RTC (`0x04000138`), minuteurs, IPC (l'autre côté de `runtime/ipc.c`),
  sans-fil (`0x04808000`+, à laisser inerte).
- Un ordonnancement coopératif : l'ARM7 tourne dans sa propre fibre, servi à
  chaque VBlank et à chaque mot IPC envoyé par l'ARM9.

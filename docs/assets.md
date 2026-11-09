# Lire et modifier les assets du jeu

Cette procédure dit où sont les données du jeu, comment les lire, et comment en
remplacer une — un texte, un script, une couleur — pour que le jeu la prenne.
Toutes les commandes se lancent depuis la racine du dépôt, avec Python 3.11 ou
plus. Aucune donnée du jeu n'est dans le dépôt : tout part de votre copie de la
ROM.

## 1. Où sont les données

**`data/` est ce que le jeu lit.** `tools/build_data.py` le découpe dans la ROM,
une fois pour toutes :

```bash
python tools/build_data.py "Star Fox Command (Europe) (En,Fr,De,Es,It).nds" -o data
```

| Partie | Contenu |
|---|---|
| `data/files/` | les 3 556 fichiers du jeu, dans l'arborescence de la cartouche, **octets bruts** : un nom en `-cmp` est compressé (LZ10), comme dans la cartouche |
| `data/files/*.srl` | 21 archives : une par planète (`Corneria.srl`…), une par langue (`Fr.srl`, `Uk.srl`…) et leurs variantes `_child`. Chacune est une petite cartouche (en-tête, noms, table d'allocation) qui range ses propres fichiers : modèles, scripts, textures et textes d'une planète ou d'une langue |
| `data/files/__unnamed__/` | les 26 overlays : du code, **à ne pas toucher** |
| `data/system/` | l'en-tête, les binaires des deux processeurs, la table des noms (`fnt.bin`) et **la table d'allocation** (`fat.bin`) : le début et la fin de chaque fichier dans l'espace d'adressage de la cartouche |
| `data/index.bin` | où chaque morceau de cet espace se trouve sur le disque |

Le jeu demande ses fichiers par adresse, comme au lecteur de la console ; il
les trouve par la table d'allocation, et `runtime/data.c` sert chaque adresse
depuis le fichier qu'`index.bin` désigne. D'où la règle : un fichier qui change
de taille doit voir sa table et son index corrigés — c'est ce que fait
`tools/fichier_du_jeu.py` (§ 4).

**`assets/` est une copie d'étude, que le jeu ne lit jamais** : tout extrait,
décompressé, et exporté vers des formats courants (OBJ, PNG, WAV, JSON). La
chaîne qui la produit :

```bash
python tools/extract_rom.py "Star Fox Command (Europe) (En,Fr,De,Es,It).nds" -o assets/extracted
python tools/decompress_tree.py assets/extracted/files -o assets/cooked
python tools/extract_srl.py assets/cooked -o assets/cooked/_srl
python tools/bmg.py assets/cooked -o assets/text
python tools/export_models.py assets/cooked -o assets/export
python tools/export_collision.py assets/cooked -o assets/export
python tools/ntfq.py assets/cooked -o assets/export/_texanim
python tools/sdat.py assets/cooked/StarfoxDS_Eu.sdat -o assets/audio --names assets/cooked/sound_data.sadl
```

C'est là qu'on cherche et qu'on regarde ; c'est dans `data/` (une copie) qu'on
modifie.

## 2. Préparer un dossier de travail

Ne jamais modifier `data/` : il reste la référence, celle des parties
enregistrées et de leurs empreintes. On travaille sur une copie :

```bash
cp -r data data_mod
```

(`Copy-Item -Recurse data data_mod` en PowerShell ; 57 Mo.) `fichier_du_jeu.py`
refuse de poser dans `data/` lui-même, sauf `--force`.

Pour jouer avec ce dossier :

- **par le lanceur** : « Parcourir… », puis choisir `data_mod/index.bin` ;
- **en ligne de commande**, sans chemin de ROM (une ROM en argument ferait
  ignorer le dossier) :

```bash
SFC_DATA=data_mod SFC_ARM7=1 ./build/runtime8/sfc.exe
```

Avant d'essayer une modification qui touche au déroulement du jeu, copier sa
sauvegarde (`sauvegarde/starfox_command.sav`), ou jouer sur une copie avec
`SFC_SAVE=copie.sav`.

## 3. Lire

### Trouver un fichier

```bash
python tools/fichier_du_jeu.py liste data SetupShared
python tools/fichier_du_jeu.py liste data battle --dans Fr.srl
```

Chaque ligne donne l'adresse, la taille et le chemin. Un fichier d'archive se
désigne par `archive:chemin`, par exemple
`Fr.srl:2D/MessageText/battle_Fr.bmg-cmp`.

### Extraire un fichier

```bash
python tools/fichier_du_jeu.py lire data SetupShared.txt-cmp -o SetupShared.txt
python tools/fichier_du_jeu.py lire data "Fr.srl:2D/MessageText/battle_Fr.bmg-cmp" -o battle_Fr.bmg
```

Un fichier en `-cmp` sort décompressé (`--brut` le laisse tel quel).

### Selon le type

| Fichiers | Ce que c'est | Pour le lire | Fiche |
|---|---|---|---|
| `.bmg` | les textes : scénario, répliques, menus, par langue | `python tools/bmg.py --texte fichier.bmg -o fichier.json` | [format_bmg.md](format_bmg.md) |
| `.txt` | les scripts, en texte clair (Shift-JIS) : stages, ennemis, modèles, volumes de collision | un éditeur de texte | [scripts.md](scripts.md), [script_reference.md](script_reference.md) |
| `.nbfc`, `.nbfp`, `.nbfs` | un fond 2D : tuiles, palette, carte d'écran | `python tools/fond_2d.py data 2D/StrategyScene/missile_mode -o fond.png` (`--8bpp` pour 256 couleurs) | ci-dessous |
| `.ntfq` | textures des effets, du HUD, des menus | `python tools/ntfq.py assets/cooked -o assets/export/_texanim` | [format_ntfq.md](format_ntfq.md) |
| `.mdl`, `.tset` | modèles 3D et jeux de textures | `python tools/export_models.py assets/cooked -o assets/export` (OBJ, MTL, PNG) | [format_mdl.md](format_mdl.md) |
| `.hgt` | collision du terrain | `python tools/export_collision.py assets/cooked -o assets/export` | [format_hgt.md](format_hgt.md) |
| `.sdat` | musiques et bruitages | `python tools/sdat.py …` (échantillons en WAV, catalogue) | [format_audio.md](format_audio.md) |

Formats d'un fond 2D : la palette est une suite de couleurs RGB555 sur 16 bits
(rouge dans les bits 0-4, vert 5-9, bleu 10-14 ; la couleur 0 de chaque palette
de 16 est transparente) ; les tuiles font 8×8 points à 4 bits par point
(32 octets) ou 8 bits (64 octets) ; la carte d'écran a une entrée de 16 bits
par case de 8×8 — numéro de tuile (bits 0-9), retournements (10, 11), palette
(12-15).

Les exports d'`assets/export` datés d'avant le 12/09/2026 sont faux pour les 411
modèles qui emploient `VTX_DIFF` (les chasseurs ennemis) : relancer
`export_models.py`.

## 4. Modifier

### Le principe

On extrait le fichier (§ 3), on le modifie, on le pose :

```bash
python tools/fichier_du_jeu.py poser data_mod <chemin> <fichier modifié>
```

- **La compression est faite pour vous** : un chemin en `-cmp` reçoit le
  fichier décompressé et le recompresse en LZ10, sans référence à l'octet qui
  précède — la décompression vers la mémoire vidéo écrit par demi-mots
  (`--brut` pose un fichier déjà compressé tel quel).
- **La taille peut changer.** Un fichier qui garde sa taille, ou rapetisse, reste
  à sa place. Un fichier qui grossit est rangé après le dernier, sur une frontière
  de 512 octets, et son entrée est corrigée dans `system/fat.bin` et dans
  `index.bin`.
- **Dans une archive**, de même dans la table de l'archive ; l'archive, qui a
  pu grossir, est posée à son tour.
- On peut poser autant de fois qu'on veut : chaque pose repart de l'état du
  dossier.

### Exemple : un texte

La réplique de Dash « J'engage le combat contre le vaisseau mère ennemi! » est
le message 340 des répliques de combat. En français, le jeu lit les textes de
`Fr.srl`.

```bash
python tools/fichier_du_jeu.py lire data_mod "Fr.srl:2D/MessageText/battle_Fr.bmg-cmp" -o battle_Fr.bmg
python tools/bmg.py --texte battle_Fr.bmg -o battle_Fr.json
#   dans battle_Fr.json, changer le « texte » de l'id 340 ; \n passe à la ligne
python tools/bmg.py --reecrire battle_Fr.bmg battle_Fr.json -o battle_Fr_neuf.bmg
python tools/fichier_du_jeu.py poser data_mod "Fr.srl:2D/MessageText/battle_Fr.bmg-cmp" battle_Fr_neuf.bmg
```

- Garder les séquences `{groupe:type:arguments}` telles quelles : ce sont les
  couleurs, les pauses, le nom du joueur.
- Seuls s'affichent les caractères de la police du jeu : les lettres accentuées
  du français, oui ; le `°`, non.
- Rester dans la longueur des répliques d'origine : ce que la bulle fait d'un
  texte plus long n'a pas été essayé.
- Les numéros viennent de `bmg.py --texte` : les JSON d'`assets/text` produits
  avant le 29/09/2026 étaient décalés de quatre, les refaire.

### Exemple : un script

Les scripts déclarent modèles, volumes de collision, ennemis, trajectoires.
`SetupShared.txt` donne par exemple le volume de collision de l'Arwing
(`RegistVolume Arwing 4.0`).

```bash
python tools/fichier_du_jeu.py lire data_mod SetupShared.txt-cmp -o SetupShared.txt
#   modifier SetupShared.txt en gardant son encodage (Shift-JIS ; le plus souvent du pur ASCII)
python tools/fichier_du_jeu.py poser data_mod SetupShared.txt-cmp SetupShared.txt
```

`#` ouvre un commentaire. Les scripts d'une planète sont dans son archive
(`Corneria.srl:Scripts/…`) ; `python tools/fichier_du_jeu.py liste data Scripts --dans Corneria.srl`
les montre. Le sens des commandes est dans
[script_reference.md](script_reference.md).

### Exemple : une couleur

Une palette se modifie octet par octet, sans changer de taille. Ici, la couleur
1 de la palette des lignes de la carte de stratégie devient rouge :

```bash
python tools/fichier_du_jeu.py lire data_mod 2D/StrategyScene/Map_Line.nbfp -o Map_Line.nbfp
python -c "import struct; p = bytearray(open('Map_Line.nbfp','rb').read()); struct.pack_into('<H', p, 2 * 1, 31 | 0 << 5 | 0 << 10); open('Map_Line.nbfp','wb').write(p)"
python tools/fichier_du_jeu.py poser data_mod 2D/StrategyScene/Map_Line.nbfp Map_Line.nbfp
```

Une couleur vaut `rouge | vert << 5 | bleu << 10`, chaque composante de 0 à
31. `fond_2d.py` montre le résultat avant d'aller en jeu : ici, les lignes du
fond passent du vert au rouge.

### Ce qui ne se modifie pas encore

| Quoi | Où en est-on |
|---|---|
| overlays, `system/` | jamais : le code qui s'exécute est celui qui a été traduit, et le comportement se change par les points de modification (`runtime/modifications.c`, [reference.md](reference.md) § 5 ter) |
| modèles `.mdl`, `.tset` | lecture et export seulement ; les réécrire demanderait de régénérer display lists, offsets et correctifs ([format_mdl.md](format_mdl.md)) |
| textures `.ntfq` | à taille égale seulement (couleurs de palette, texels) : la taille du fichier dit la hauteur et le nombre de couleurs |
| sons `.sdat` | lecture seulement |
| nouveaux messages | on remplace un message, on n'en ajoute pas encore |

## 5. Vérifier

- Lancer avec le dossier modifié (§ 2) et aller jusqu'à l'endroit touché. Lancé
  par le lanceur, le jeu écrit son journal dans `journal.txt`, à la racine : il
  dit pourquoi il s'arrête, s'il s'arrête.
- Pour la 2D, `SFC_2D_IMAGES=image` écrit ce que composent les deux écrans à la
  fin de cette image (`build/essai/ram_2d_A_<image>.ppm`, `…_B_…`), même sans
  fenêtre (`SFC_NO_VIDEO=1`, avec une partie rejouée).
- Les parties enregistrées (`build/enregistrements/`) ne se rejouent fidèlement
  qu'avec les données d'origine : une modification qui change le déroulement du
  jeu les fait diverger.

Ce qui a été éprouvé en jeu (le 29/09/2026, partie `obs52` rejouée) :

- `SetupShared.txt`, grossi de lignes de commentaire (2 156 → 5 952 octets
  compressés), a été rangé plus loin. Le jeu l'a lu à sa nouvelle adresse dès
  l'entrée en combat, et la partie a suivi exactement le même chemin qu'avec
  l'original (1 440 relevés de caméra identiques).
- La réplique 340 de `battle_Fr.bmg` a été changée dans chacune de ses huit
  copies, chaque archive grossissant et changeant de place. L'écran a montré
  celle de `Fr.srl`.
- Aller-retour exact : le compresseur sur 155 fichiers du jeu (taille à 0,5 %
  de celle de Nintendo), la réécriture des textes sur les 53 fichiers des
  archives.

## 6. Revenir en arrière

Supprimer le dossier de travail et en refaire une copie de `data/`. Si `data/`
lui-même a été touché, le refaire depuis la ROM avec `tools/build_data.py`.

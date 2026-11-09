# Format `.mdl` / `.tset` / `.anim` / `.hgt` — conteneur « chnk » (MGL)

Rétro-ingénierie réalisée sur les 2 176 modèles, 166 jeux de textures, 136
séquences d'animation et 763 grilles de collision de la ROM européenne.

`MGL` est la bibliothèque graphique interne de Q-Games ; tous les assets 3D
partagent un unique conteneur à chunks typés.

## 1. En-tête de chunk (100 octets, `0x64`)

| Offset | Type | Champ |
|---|---|---|
| `0x00` | `char[4]` | magie `chnk` |
| `0x04` | `u32` | version (toujours `1`) |
| `0x08` | `u32` | `1` = contient des sous-chunks, `0` = feuille |
| `0x0C` | `u32` | taille totale du chunk, **en-tête compris** |
| `0x10` | `u32` | identifiant de classe (`1` = Model, `3` = Mesh) |
| `0x14` | `char[16]` | étiquette de type, terminée par `NUL` |
| `0x24` | `char[56]` | nom de l'asset, terminé par `NUL` |
| `0x5C` | `u32` | constante `0x4499B63B` |
| `0x60` | `u32` | empreinte de contenu |

Les sous-chunks se suivent immédiatement après l'en-tête et se parcourent en
avançant de `taille` en `taille` jusqu'à la fin du chunk parent.

### Étiquettes rencontrées

| Étiquette | Contenant | Rôle |
|---|---|---|
| `MGL Model` | `.mdl`, `.nsbmd` | racine d'un modèle |
| `MGL Mesh` | dans `MGL Model` | géométrie + matériaux |
| `MGL Texture` | `.tset`, `.mdl` | données de texels |
| `MGL Palette` | `.tset`, `.mdl` | palette associée |
| `MGL ModelSet` | `.tset` | racine d'un jeu de textures partagé |
| `MGL AnimSeq` | `.anim` | racine d'une séquence d'animation |
| `MGL AnimSeqInfo` | dans `MGL AnimSeq` | métadonnées de séquence |
| `MGL LKSqfix16` | dans `MGL AnimSeq` | pistes de clés en virgule fixe 4.12 |
| `MGL LKSqMGLRgb` | dans `MGL AnimSeq` | pistes de clés de couleur |
| `HGT Info` | `.hgt` | grille de hauteurs pour la collision sol |

## 2. Payload de `MGL Mesh`

```c
struct MglMesh {
    u32 id;                       // empreinte
    u32 n_materials;
    u32 n_textures;
    u32 n_shapes;
    u32 n_texture_patches;

    Material materials[n_materials];       // 32 octets chacun

    // Table de noms : n_textures chaînes, chacune préfixée par sa longueur
    // sur un octet (la longueur inclut le préfixe : 0x09 + "Ammonite" fait
    // bien 9 caractères).
    PascalString texture_names[n_textures];

    struct { u32 material_index; u32 dl_offset; } shapes[n_shapes];

    // Emplacements des commandes TEXIMAGE_PARAM à corriger au chargement,
    // quand le moteur connaît l'adresse VRAM réelle de chaque texture.
    struct { u32 texture_index; u32 dl_offset; } patches[n_texture_patches];

    u32 dl_size;
    u8  display_list[dl_size];    // liste d'affichage GX du DS
};
```

### `Material` (32 octets)

Un matériau est un fragment de display list pré-encodé, directement injectable
dans le FIFO du GPU :

| Offset | Type | Contenu |
|---|---|---|
| `0x00` | `u32` | paquet de commandes `0x00313029` : `POLYGON_ATTR`, `DIF_AMB`, `SPE_EMI`, `NOP` |
| `0x04` | `u32` | paramètre `POLYGON_ATTR` (faces, alpha, identifiant de polygone) |
| `0x08` | `u32` | paramètre `DIF_AMB` (diffus + ambiant, RGB555 ×2) |
| `0x0C` | `u32` | paramètre `SPE_EMI` (spéculaire + émission) |
| `0x10` | `u32` | paquet de commandes `0x00002B2A` : `TEXIMAGE_PARAM`, `PLTT_BASE`, `NOP`, `NOP` |
| `0x14` | `u32` | paramètre `TEXIMAGE_PARAM` (taille, format, répétition) |
| `0x18` | `u32` | paramètre `PLTT_BASE` |
| `0x1C` | `s32` | index de texture, `-1` si le matériau n'est pas texturé |

### Display list

Les `dl_size` octets sont une display list GX standard du Nintendo DS :
paquets de 4 octets de commande suivis de leurs paramètres sur 32 bits.

Tout ce qui précède le premier `shape` forme un **prologue** commun : `MTX_PUSH`,
un ou deux `MTX_SCALE`, `MTX_STORE 16`, `COLOR blanc`. Sa longueur varie sur le
corpus :

| Premier `shape` à | Modèles | Contenu du prologue |
|---|---:|---|
| `0x18` | 1 078 | `MTX_PUSH`, `MTX_SCALE`, `MTX_STORE 16`, `COLOR` |
| `0x28` | 922 | idem, avec un second `MTX_SCALE(1,1,1)` sans effet |
| `0x34` | 168 | variantes avec `MTX_STORE` supplémentaire |
| `0x40`, `0x50` | 8 | prologues avec `MTX_TRANS` |

**Le prologue s'exécute donc jusqu'à `min(shapes[i].dl_offset)`, jamais sur
une longueur fixe.** Le tronquer à 24 octets coupe le premier paquet en plein
milieu pour la moitié des modèles, et n'exécute alors rien : ces modèles
sortent à l'échelle 1 au lieu de 256 — une erreur commise puis corrigée dans ce
projet.

Chaque `shape` reprend ensuite à son offset avec `MTX_RESTORE 16`, applique son
matériau et émet ses primitives.

### Unités

Une fois le prologue exécuté, **les coordonnées sont directement en unités
monde**, celles des scripts. Vérifications :

- une tuile de terrain (`CityBlank_Gr`, échelle 256) fait 2 580 unités, soit une
  case de grille de 2 560 plus un léger recouvrement des coutures ;
- l'ennemi `FryEnemy001` (échelle 256) fait 300 unités, pour un volume de
  collision de rayon 50 déclaré par `SetVolume` ;
- le cube de test `DummyCubeS` (échelle 16) fait 128 unités pour un volume de
  100.

Exception connue : les Arwings des joueurs (`fox_main.mdl`, échelle 1.0) ne
font que 5 unités. Le moteur d'origine les dessine avec sa propre matrice, dont
le facteur reste à déterminer — aucun paramètre de configuration ne le porte.

## Vérification

Un décodeur de format retrouvé à tâtons ne vaut que s'il tient sur tous les
fichiers :

```bash
python tools/validate_models.py assets/cooked
```

Chaque modèle se décode, sa liste d'affichage est consommée en entier, et
aucune commande GX inconnue n'apparaît : **2 176 modèles, 0 erreur, 216 813
triangles** (relancé le 29 septembre 2026).

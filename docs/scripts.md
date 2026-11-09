# Le langage de script

Star Fox Command n'est pas un jeu écrit en dur : c'est un moteur piloté par
**1 405 scripts en texte clair**, restés dans la ROM parce que le système de
build d'origine (« mantabuilder », 26 septembre 2006) les y a copiés tels
quels. Le jeu les lit et les exécute avec son propre interpréteur, que le
portage fait tourner comme le reste de son code : modifier un script change le
jeu sans toucher à son code ([la procédure](assets.md)) — et la même grammaire
servira à écrire la suite de l'histoire.

Le vocabulaire complet — 205 commandes, 54 427 appels, avec signatures et
exemples réels — est recensé dans [`script_reference.md`](script_reference.md),
généré par `tools/analyze_scripts.py`.

## Syntaxe

Fichiers texte encodés en **Shift-JIS** (les commentaires des développeurs sont
en japonais), une commande par ligne :

```
CreateEnemyTemplate eHoverEnemy <0 850 3500> <0 0 0> <1 1 1> 0
```

- `#` introduit un commentaire. Attention : la grande majorité des lignes
  commentées sont des commandes **désactivées**, pas de la prose. Elles sont
  une mine d'informations sur le moteur, mais ne doivent jamais être exécutées.
- `<x y z>` est un vecteur. Certaines commandes écrivent pourtant leurs
  coordonnées sans chevrons, comme `ActorObject`.
- Les entiers s'écrivent en décimal ou en hexadécimal (`0x0080` pour les pentes
  de brouillard).

## Résolution des chemins

Les scripts désignent les fichiers par leur chemin dans la ROM :
`/3D/Corneria/GrndSetA/CityA.mdl`. Or ce fichier n'existe pas tel quel : il vit
dans l'archive `Corneria.srl`. Le jeu **monte chaque archive `.srl` sous
`/3D/<Planète>/`** : ce chemin-là s'écrit `Corneria.srl:GrndSetA/CityA.mdl-cmp`
pour `tools/fichier_du_jeu.py`, et se retrouve sous `assets/cooked/_srl/Corneria/`
une fois les archives extraites.

## Comment un stage est décrit

Un stage jouable tient en une poignée de commandes :

```
Include  /3D/Corneria/Scripts/StageSetupA.txt
RegistModelSet GrndSetA  /3D/Corneria/GrndSetA/GrndSetA.tset
RegistHeightModel CityA  /3D/Corneria/GrndSetA/CityA.mdl
ClearHeightManager
SetHeightGridSize 2560
LoadHeightMap CityA Convex /3D/Corneria/GrndSetA/CityA_Col.hgt
RegistHeightGridObject HeightPoly CityA 3 5
```

Le terrain est une **grille de tuiles de 2 560 unités**.
`RegistHeightGridObject` — la commande la plus appelée du jeu, 13 239 fois —
pose à la case (3, 5) le modèle enregistré sous le nom `CityA`, avec la tuile
de collision chargée par `LoadHeightMap`.

Deux conventions cohabitent selon les planètes : Corneria déclare ses tuiles
avec `RegistHeightModel`, Aquas se contente de `RegistModel`.

### L'échelle et l'origine

Une fois leur prologue exécuté (voir [`format_mdl.md`](format_mdl.md)), les
modèles sont **directement en unités monde** : une tuile `CityBlank_Gr` fait
2 580 unités, soit une case de 2 560 plus le recouvrement des coutures.

Les tuiles, centrées sur leur origine, se posent en
`((caseX − 4) × 2 560, 0, (caseZ − 4) × 2 560)` : la grille de 9 × 9 est
**centrée sur l'origine du monde**. Trois indices concordent — les positions
d'ennemis s'étalent sur ±5 200 unités autour de zéro, le joueur démarre en
`(0, 500, 6400)`, et les bornes du terrain reconstruit tombent à ±11 520.

Une première version de ce document affirmait un facteur 256 entre « unités
modèle » et unités monde. C'était l'artefact d'un bug du décodeur, qui
n'exécutait pas le prologue de la moitié des modèles.

### Couches de mission et ennemis

Un stage jouable est la superposition de deux scripts, enregistrés séparément
par `<Planète>Stage.txt` :

```
RegistStage      CityA    /3D/Corneria/Scripts/gCityA.txt      # terrain, décor
RegistStageLayer CityA_1  /3D/Corneria/Scripts/gCityA_1.txt    # ennemis, objectifs
```

La couche ne contient que des placements :

```
CreateEnemyTemplate eManbo <1850.026 257.2633 -208.3949> <0 0 0> <1 1 1> 1
```

Position et rotation (en degrés) sont en unités monde ; le dernier entier est
la difficulté. Le nom renvoie à un **gabarit** défini dans
`/Scripts/<Planète>/<Planète>Enemy.txt`, que le jeu charge d'après la planète
du stage :

```
RegistEnemy eHoverEnemy
SetModel HoverEnemy /3D/Enemy/FryEnemy001/FryEnemy001.mdl
SetVolume <50>
SetCallback EnemyHover2
SetLife 20 10 0
SetWeapon 0 10 RegularLaser
```

Tout ce qui suit un `RegistEnemy` jusqu'au suivant décrit ce gabarit : modèles
(le premier `SetModel` est le principal ; les alias `_Demo` servent aux
cinématiques), jeu de textures propre, volume de collision, comportement
(`SetCallback`), trajectoire (`AddPath`), points de vie et arme. Sur 157 noms
placés dans les stages, 141 ont un gabarit ; les seize autres sont des missiles
et des boss définis ailleurs.

### Trajectoires

`AddPath <x y z>` empile les points de passage d'un gabarit ; `SetPathLoop true`
referme le circuit. Les coordonnées sont **absolues**, en unités monde : les
gabarits `eGokai` et `eShrimp` partagent le même circuit (X −3675..4005,
Z −3675..4005) quels que soient leurs 175 et 133 points d'apparition, et
l'altitude des chemins (600 pour `eStarfish`) diffère de celle des apparitions
(650 à 850). Un ennemi apparaît donc à sa position de `CreateEnemyTemplate`,
puis rejoint son premier point de passage.

Quatre `AddPath` et quatre `SetVolume` séparent leurs composantes par des
virgules.

## Modifier un script

Un script est un fichier `.txt-cmp` — du texte compressé — dans le dossier de
données ou dans l'archive d'une planète. [La procédure](assets.md) dit comment
le lire, le modifier et le remettre en place, même s'il grossit ; son exemple,
`SetupShared.txt`, a été relu par le jeu à sa nouvelle place, la partie suivant
le même chemin qu'avec l'original.

Chaque commande, avec le nombre de ses appels, ses signatures et des exemples
réels, est dans [`script_reference.md`](script_reference.md).

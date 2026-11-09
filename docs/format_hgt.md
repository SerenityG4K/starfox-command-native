# Format `.hgt` — collision du sol (`HGT Info`)

763 tuiles de collision, une par modèle de terrain, **toutes décodées** :
62 055 sommets, 94 101 triangles. Vérification décisive : la tuile `CityD`
donne exactement les bornes de son modèle visuel — Y de −82 à 734, étendue
horizontale 964 — à l'unité près.

```bash
python tools/export_collision.py assets/cooked -o assets/export
```

## Structure

Même conteneur « chnk » que les modèles (en-tête de 100 octets, étiquette
`HGT Info`, sans sous-chunk). Le payload :

| Champ | Contenu |
|---|---|
| `u16` | version, toujours 1 |
| `u16` | empreinte |
| `u16` | 1 (0 ou 2 pour une poignée de tuiles) |
| `u16` | non élucidé — 157, 160, 212… — sans effet sur le décodage |
| `u32` | 0 |
| `u16` | **160** : côté de la tuile en unités HGT |
| `u16` | **8** : cellules par côté de la grille de recherche |
| `u16` | 0 |
| `u16` | **nombre de sommets** |
| `u16` | 0 |
| `Sommet[n]` | trois `fx32` 16.16 : **x, y (hauteur), z** |
| `u32` | nombre de triangles |
| `Triangle[m]` | trois `u16`, indices de sommets |
| `Cellule[64]` | `u32` hauteur 16.16, `u16` nombre, `u16` 0, `u16` indices de triangles |

Une unité HGT vaut **16 unités monde** : les coins d'une tuile tombent à ±80
(±1 280, la demi-case de 2 560), les toits de `CityD` à 44 (704).

## Ce que contiennent les tuiles

- une tuile vide (`Blank_Col`) : ses quatre coins et deux triangles ;
- une tuile de montagne (`YamaA_Col`) : une grille de 9 × 9 sommets — 81, soit
  8 × 8 cellules — et 128 triangles, hauteurs de 45 à 382 ;
- une tuile de ville (`CityD_Col`, déclarée `Convex` par `LoadHeightMap`) :
  232 sommets et 188 triangles, les enveloppes fermées des immeubles.

`LoadHeightMap <nom> Ground|Convex <chemin>` distingue donc un sol à suivre
d'obstacles à contourner ; la géométrie est du même type dans les deux cas.

Les enveloppes ne sont **pas fermées** : `CityD` compte 232 sommets pour 188
triangles. Les tuiles ne décrivent que des toits et des sols, et les murs sont
implicites. La sémantique est celle d'un champ de hauteurs — un « height
manager », le nom même que les scripts donnent au système : la hauteur du
terrain en (x, z) est celle du triangle qui recouvre le point, et tout ce qui
vole plus bas est dans l'obstacle. Un vol horizontal vers un immeuble ne
traverse aucun triangle ; il passe simplement sous la hauteur locale.

La table des 64 cellules est un accélérateur de recherche : pour un point
(x, z) de la tuile, la cellule `(⌊(x + 80) / 20⌋, ⌊(z + 80) / 20⌋)` liste les
triangles qui la traversent. Sa hauteur (16.16) est vraisemblablement la
hauteur maximale de la cellule — utile pour écarter d'emblée un objet qui vole
au-dessus.

## Erreur corrigée

Une première analyse voyait dans ces fichiers des normales en virgule fixe 1.15
et deux tableaux introuvables. Les « normales » étaient les parties
fractionnaires de coordonnées 16.16, et la difficulté venait d'un onzième mot
d'en-tête, toujours nul, qui décalait la lecture des sommets de deux octets.

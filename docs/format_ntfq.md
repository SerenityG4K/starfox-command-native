# Format `.ntfq` — textures des animations, du HUD, des effets et des menus

4 282 fichiers, les plus nombreux de la ROM. Un `.ntfq` ne contient que les
**texels bruts** d'une texture DS suivis de sa **palette**, de longueur
variable : seules les couleurs utilisées sont écrites (5 entrées pour la
munition `Muzzle`, 84 pour une image d'explosion).

```bash
python tools/ntfq.py assets/cooked -o assets/export/_texanim
```

## Ni dimensions ni format dans le fichier

Ce sont les scripts qui les donnent :

```
CreateTexAnimBase Face_Fox 64 64
LoadTexAnimPatt   PLTT16 /3D/Textures/Overlays/HUD/Face_Fox_SP

CreateTexAnimSeq  Explosion 64 64 COMP4x4 /3D/Textures/FX/Explosion/ExA_SP 16 1
```

La largeur et le format viennent de la déclaration ; la hauteur réelle se
déduit de la taille du fichier, et plusieurs images peuvent y être empilées
(`BoostBorder` : deux images de 64 × 8). Le nombre d'entrées de palette est
celui qui rend la hauteur entière et multiple de 8 — unique en pratique, alors
qu'une inférence sans la déclaration laisse des dizaines de solutions.

Formats rencontrés : `PLTT4` (418 déclarations), `PLTT16`, `A3I5`, `A5I3`,
`COMP4x4`. Pour le 4 × 4 compressé, texels puis index sont concaténés, comme
dans les chunks `MGL Texture`.

Une séquence nomme ses images `<chemin>01`, `<chemin>02`, … : l'explosion
standard compte seize images de 64 × 64 en 4 × 4 compressé, une boule de feu
qui enfle puis se disperse.

## Ce que ça ouvre

Les animations de texture portent les explosions, les tirs, le HUD (visages
des personnages, réticule, radar) et les menus. Dans le moteur, elles se
rendent en panneaux face caméra pour les effets et en superposition
orthographique pour l'interface.

Vérification visuelle : le visage de Fox (`Face_Fox_SP`, 64 × 64, 16 couleurs)
et le réticule (`Target_SP`, 32 × 32, 4 couleurs) sortent nets.

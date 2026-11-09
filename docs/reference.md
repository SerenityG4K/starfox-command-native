# Référence : le code du portage et ce qu'on sait du jeu

Ce document rassemble ce qui a été établi, et **seulement** ce qui a été vérifié.
Les adresses données ont été retrouvées par la mesure ou le désassemblage, pas
supposées. Quand une chose est une conjecture, c'est écrit.

Il se lit en trois parties : la chaîne de fabrication, le moteur que nous avons
écrit, et le jeu lui-même — cette dernière étant la plus précieuse, car elle ne
se retrouve qu'au prix d'une enquête.

---

## 1. La chaîne de fabrication

Le jeu n'est pas émulé : son binaire est **traduit une fois pour toutes** en C,
puis compilé en code natif. Trois étapes, à relancer dans cet ordre après tout
changement d'outillage :

```bash
python tools/codemap.py "<rom>"                                   # ~3 min
python tools/recomp/recomp.py "<rom>" --regions all --out generated   # ~2 min
cmake --build build/runtime7 -j 8                                  # ~8 min
```

| étape | rôle | sortie |
|---|---|---|
| `codemap.py` | trouve **où est le code** : part du point d'entrée, suit appels et sauts, délimite les fonctions | `analysis/codemap.json`, 29 692 fonctions |
| `recomp.py` | traduit chaque instruction ARM en C | `generated/*.c`, 8,3 M de lignes |
| CMake | compile et lie avec le moteur | `build/runtime7/sfc.exe`, ~110 Mo |

Le coprocesseur ARM7 suit la même chaîne avec `--arm7` et `--suffixe 7` (ses
symboles portent un 7 pour cohabiter), et `-DSFC_ARM7=ON` le lie.

**La cartographie est le point faible.** Un binaire ne dit nulle part « ici
commence une fonction » ; code et données sont mêlés. Quand elle se trompe d'une
frontière, une adresse se retrouve sans porte d'entrée et le moteur s'arrête.
Ces arrêts se réparent en un cycle : l'adresse est consignée dans
`analysis/amorces.txt`, que `codemap.py` relit comme amorce manuelle.

Deux causes de ce type ont été traitées à la racine dans `recomp.py` : le
générateur ouvre désormais un point d'entrée sur **toute adresse pointée depuis
la mémoire** (tables de rappels) et sur **toute cible d'appel relevée par la
carte**. Cela a supprimé 176 et 72 arrêts en puissance.

---

## 2. Le moteur (`runtime/`, 7 700 lignes)

C'est la seule partie que nous écrivons : elle réimplémente le matériel de la
console.

### Exécution

| fichier | rôle |
|---|---|
| `rt.c` | dispatch des appels, modes du processeur, hooks natifs, traces |
| `memory.c` | carte mémoire de l'ARM9 ; les accès en ligne (`cpu.h`) lisent directement la RAM principale et les piles — la DTCM de l'ARM9, la WRAM du coprocesseur —, tout le reste passe par ici |
| `irq.c` | interruptions, temps virtuel, minuteurs, entrées, boucle d'image |
| `threads.c` | threads du SDK portés sur des fibres Windows |
| `main.c` | amorçage : charge la ROM, place l'ARM9 comme le ferait le firmware |

### Matériel

| fichier | rôle |
|---|---|
| `mmio.c`, `mmio7.c` | registres des deux processeurs |
| `gx.c` | FIFO de commandes du moteur 3D |
| `gx_geom.c` | moteur géométrique : matrices, sommets, assemblage des primitives |
| `video.cpp` | rendu OpenGL des polygones, textures, couches 2D |
| `video2d.c` | moteurs 2D, composés en logiciel par bandes de lignes (l'atelier de `video.cpp` les répartit sur plusieurs cœurs) |
| `sound.c` | les seize canaux et leur mixage |
| `audio.c` | sortie de l'hôte (file de petits tampons) |
| `card.c`, `backup.c` | cartouche et mémoire de sauvegarde |
| `data.c` | la cartouche servie depuis un dossier de fichiers (`data/`) |
| `natif.c`, `natif.h` | les fonctions réécrites à la main, le banc qui prouve la fidélité — en jeu et à froid — et ce que partagent avec lui les fonctions du générateur |
| `ipc.c` | dialogue entre les deux processeurs |
| `bios.c` | appels système du BIOS |
| `arm7_run.c`, `threads7.c` | le coprocesseur traduit et ses threads |
| `arm7_hle.c` | son remplacement léger (`SFC_ARM7=0`) |

### Ce qu'il faut savoir avant d'y toucher

- **Le mixage sonore suit le temps réel**, pas l'avancement de la simulation :
  sur la console, l'ARM7 et le matériel audio sont indépendants de la cadence du
  jeu. On ne complète que lorsque la file de sortie passe sous sa cible.
- **Une ligne du rendu 2D ne doit dépendre que des registres et de la mémoire
  vidéo** : les deux écrans sont composés en parallèle, par bandes, sur plusieurs
  fils (`SFC_BANDES_VERIF=1` le vérifie octet par octet). Pour la même raison, les
  tampons de ligne ne peuvent pas être statiques.
- **La capture d'affichage se pose en mémoire vidéo d'un bloc, sous
  `g_mx_capture`**, que la composition du balayage tient : écrite point par point
  depuis le fil d'affichage, elle déchirait l'écran du bas.
- **En affichage libre, la capture se pose après coup**, quand le fil d'affichage
  voit l'image ; la console l'a faite pendant l'image, avant ce que le jeu écrit
  ensuite. Les écritures du jeu en mémoire vidéo se comptent donc par page de
  16 Ko (`g_vram_ecrit`, `memory.c`), chaque instantané garde les compteurs de son
  image, et la capture épargne les pages écrites depuis (`poser_capture`). Sans
  cela, à l'arrivée sur la carte de stratégie (image 3 009 de `obs52`), la capture
  d'un écran noir tombait sur la banque C que le jeu venait de reprendre pour les
  tuiles de l'écran du bas : la tuile 0, transparente, recevait des `0x8000`, un
  point de couleur 8 sur quatre, et BG0, qui en est pavé, rayait tout l'écran
  jusqu'au combat suivant.
- **Quitter depuis le fil du jeu passe par `rt_sortir`** : `exit()` détruit les
  objets statiques pendant que le fil d'affichage s'en sert encore. `rt_sortir`
  l'arrête d'abord.
- **Le rendu ne demande que le cœur d'OpenGL 3.3**, le même chez NVIDIA, AMD et
  Intel. L'interpolation sur le GPU s'éprouve elle-même au démarrage et revient
  au processeur au moindre écart (`docs/plan_framerate.md`) ; le journal commence
  par le pilote et la carte trouvés. `main.c` exporte `NvOptimusEnablement` et
  `AmdPowerXpressRequestHighPerformance` : sur un portable à deux cartes, les
  pilotes lancent alors le jeu sur la carte dédiée plutôt que sur l'intégrée.
- **Le budget d'arêtes (256) et le seuil du coprocesseur (8) ne se touchent
  pas sans vérifier le son** : voir la section 6.

### Les fils

| fil | ce qu'il fait |
|---|---|
| jeu | la simulation des deux processeurs, les tours d'aperçu (`natif.c`), la mise en instantané de chaque image, le son |
| affichage | la fenêtre et OpenGL : appariement des images du jeu, plan du chemin entre deux d'entre elles, dessin (affichage libre) ; l'interpolation elle-même se fait sur le GPU, dans un nuanceur de géométrie (`docs/plan_framerate.md`) |
| 2D 1 à 4 | l'atelier : les deux écrans 2D, en quatre bandes chacun ; le fil du jeu en prend sa part |
| manettes | XInput, DirectInput : lit la manette toutes les 4 ms et en cherche une quand Windows annonce un périphérique — moins d'un demi pour cent d'un cœur ; le fil du jeu en fait les commandes du cockpit |

Mesuré le 26/09/2026 en vol, réglages du lanceur (165 images par seconde, portée
double) : le fil du jeu prend 31 à 34 % d'un cœur — dont six dixièmes pour les
cent vingt tours d'aperçu d'une seconde, à 1,6 ms chacun —, l'affichage 19 à 20 %,
l'atelier 14 % à lui quatre ; en tout trois quarts d'un cœur. Débridé
(`SFC_IPS=0`), l'affichage monte à 2 600 – 2 700 images par seconde. `SFC_PROFILE=1`
dit cette répartition toutes les six cents images (ligne « fils : »).

---

## 3. Le jeu : ce qui a été identifié

Sans symboles, chaque adresse ci-dessous a demandé une enquête. Les noms entre
guillemets sont ceux que le binaire porte lui-même, dans ses tables.

### La boucle principale, et pourquoi le combat se pense à 30 Hz

`0x020A0E4C`, appelée sur l'objet global `0x0216BED4`, est un tour de boucle du jeu :

| champ de l'objet | rôle |
|---|---|
| `+0x108` | la pile des scènes (des pointeurs) |
| `+0x188` | sa hauteur — 1 dans les menus, 2 en vol |
| `+0x1A3` | **le limiteur** : nul dans les menus, levé en vol |
| `+0x1A4` | la durée du dernier tour, en millisecondes |
| `+0x1A8`, `+0x1AC` | le repère d'horloge, sur 64 bits |

Elle appelle la huitième méthode virtuelle de la scène du sommet, mesure le tour
— `(tiques << 6) / 33514`, la conversion du SDK — et, limiteur levé, relit
l'horloge jusqu'à vingt millisecondes. Vingt dépassant les seize d'une image, le
jeu ne fait alors qu'un tour toutes les deux images : **le combat se pense à
29,9 Hz**. Cette attente représente plus de la moitié des arêtes du processeur
principal en vol (`SFC_PROFILE_ARM9` avec `SFC_PROFILE_FROM`).

Remplacer l'attente par un simple écoulement du temps simulé
(`SFC_ATTENTE_DIRECTE=1`, qui laisse au jeu la dernière milliseconde et donc le
choix de l'instant de sortie) fait gagner **un quart** de la simulation seule :
12 000 images de la partie du joueur en 9,7 s au lieu de 12,7 (mesuré le
26/09/2026 ; c'était 3,7 % quand le reste coûtait plus cher). L'option reste
éteinte : elle change les empreintes, et en jeu le gain est faible — l'attente
ne revient qu'une fois par vrai tour, les tours d'aperçu s'arrêtent avant elle.
Chaque passage de l'attente fait avancer le temps simulé de 64 cycles (la
lecture du minuteur) : deux passages ne sont jamais identiques, on ne peut donc
pas en sauter au bit près.

Le pas de temps est une constante — 136 par tour, un trentième de seconde en
virgule fixe : abaisser le seuil (`SFC_PAS_MS=10`) double la vitesse du jeu, à
l'exception de quelques comptes à rebours calés sur l'horloge. Les conséquences
pour la cadence débloquée sont dans [`plan_framerate.md`](plan_framerate.md).

### Le module de collision (overlay des acteurs, `0x021B3800`–`0x021B6900`)

Cent cinq fonctions, presque toutes en ARM au milieu d'un overlay en Thumb : le
signe d'un code écrit pour la vitesse. C'est lui que le jeu appelle le plus en vol.

**`CollidableObject`, 188 octets.** L'identification tient par les tailles : son
constructeur (`0x021B5A24`) appelle celui de sa base (`0x021B47A4`), pose la table
de méthodes `0x021CFB0C`, puis initialise trois champs en `+0xB4`, `+0xB8` et
`+0xB9` — juste au-delà des 176 octets de `ColVolume`, et juste en deçà de 188.

| champ | rôle |
|---|---|
| `+0x00` | table de méthodes (14 entrées) |
| `+0x0C` | nœud dans la liste de tous les objets du monde |
| `+0x14` | nœud dans la liste des objets qui ont des partenaires |
| `+0x24` | type (la passe traite à part la valeur 8) |
| `+0x2C` | propriétaire ; ses drapeaux sont en `+0x9C` |
| `+0x34` | identifiant, qui ordonne une paire |
| `+0x48` | **partenaires : quatre pointeurs** |
| `+0x58` | leur nombre |
| `+0xB8` | octet d'état : nul veut dire « participe » |

| méthode | adresse | rôle |
|---|---|---|
| `[+0x24]` | `0x021B3FF4` | mise à jour du volume, à chaque image |
| `[+0x28]` | `0x021B594C` | « participe ? » — rend 1 si `+0xB8` est nul |
| `[+0x34]` | `0x021B580C` | notification : « tu touches celui-ci » |

| fonction | rôle |
|---|---|
| `0x021B61A8` | **la passe de chaque image** : met à jour tous les volumes, puis, pour chaque objet ayant des partenaires, prévient les deux côtés de chaque paire |
| `0x021B3B20` | enregistre une paire : range le partenaire chez celui des deux qui a le plus petit identifiant, sans doublon, et place cet objet dans la liste des objets à traiter |
| `0x021B3B18` | la capacité — `mov r0, #4 ; bx lr` |
| `0x021B3C4C` | le test entre deux volumes (592 octets) |
| `0x021B3E9C` | appelle le test dans l'ordre que dicte un drapeau du propriétaire |
| `0x021B678C` | le monde : neuf listes de huit octets, plus celle des objets à traiter |
| `0x021B3FD0` | premier élément d'une liste à nœuds inclus |

**Une limite de la console, écrite en dur : quatre partenaires par objet.** Elle
est double — la constante de `0x021B3B18`, et la place dans la structure, dont le
tableau occupe exactement `+0x48`–`+0x57`. Au-delà de quatre, la paire est
silencieusement ignorée. La lever demande d'agrandir l'objet, donc de le faire
changer de classe de taille dans l'allocateur : faisable, mais ce n'est pas une
constante à retoucher.

### Les primitives du système

Le jeu est bâti sur la bibliothèque de Nintendo, dont on retrouve les fonctions au
fil des lectures. Celles-ci sont établies — chacune a été désassemblée, et les
quatre premières ont une réécriture qui passe le banc :

| adresse | mode | ce qu'elle fait |
|---|---|---|
| `0x0203003C` | ARM | coupe les interruptions et rend l'état d'avant |
| `0x02030050` | ARM | rétablit l'état rendu par la précédente |
| `0x01FFEE38` | Thumb | prend un bloc dans la liste libre de sa classe de taille |
| `0x01FFEDE4` | Thumb | rend un bloc à sa liste, ou le passe à l'étage du dessus |
| `0x02088E50` | Thumb | l'étage du dessus, quand la liste est vide : il cherche ailleurs, puis se rabat sur le tas (« DynObject allocated from heap: pool %d ») |
| `0x02088DF8` | Thumb | son symétrique à la libération |
| `0x0208940C` | Thumb | rend une référence à un objet et le libère au dernier lâcher |
| `0x02087484` | Thumb | déclare une classe : nom, taille, alignement, et choisit sa classe de taille |
| `0x02087FC8` | Thumb | construit un objet « nom » compté par références |
| `0x02087FFC` | Thumb | y recopie la chaîne (56 caractères au plus, longueur à l'offset 0x3B) |
| `0x0202DE64` | ARM | réveille les threads d'une file d'attente |
| `0x0202DE30` | ARM | signale un objet de synchronisation et réveille ce qui l'attend |

Le jeu tient ses objets dans **dix-sept classes de taille**, chacune avec sa liste
de blocs libres ; c'est la raison pour laquelle les tailles relevées plus bas
comptent : elles décident du pool où chaque objet atterrit.

### Les pools d'objets : une limite de la console, chiffrée

Chaque entrée du tableau des classes fait 0x1C octets : tête de la liste libre
(`+0`), début de la zone (`+4`), **taille d'un bloc (`+0xC`)**, **nombre de blocs
(`+0x10`)**. Le code ne fait que multiplier ces deux derniers champs, l'ordre ne s'y
lit donc pas ; c'est le décompte des blocs libres qui le tranche. Relevé à l'image
600, dans les menus :

| taille | blocs | occupés | | taille | blocs | occupés |
|---:|---:|---:|---|---:|---:|---:|
| 8 | 4 500 | 500 | | 448 | 170 | 2 |
| 16 | 1 200 | 2 | | 512 | 70 | 0 |
| 32 | 1 100 | 17 | | 580 | 35 | 0 |
| 64 | 2 800 | 713 | | 768 | 25 | 2 |
| 128 | 1 300 | 365 | | 896 | 5 | 0 |
| 192 | 600 | 9 | | 1 024 | 4 | 0 |
| 256 | 110 | 32 | | 1 536 | 1 | 0 |
| 384 | 250 | 54 | | 2 048 | **0** | — |
| | | | | 3 072 | **0** | — |

En vol, dans la mission d'entraînement — la plus calme du jeu — l'occupation monte
déjà haut : **76 %** de la classe de 64 octets (2 139 blocs sur 2 800), 67 % de celle
de 128, 64 % de celle de 384, 58 % de celle de 8, et l'unique bloc de 1 536 est pris.
Un `CollidableObject` sur trente, en revanche, et aucun n'approche ses quatre
partenaires : ces limites-là se mesureront en combat chargé, pas à l'entraînement.

817 Ko réservés sur les 4 Mo de la console. Un `Actor` (428 octets) tombe dans la
classe de 448 : **170 blocs**. Les deux plus grandes classes n'en ont aucun — tout
objet de plus de 1,5 Ko part directement sur le tas. Au-delà, quand une classe est
pleine, l'allocateur cherche dans les suivantes, puis se rabat lui aussi sur le tas
(« DynObject allocated from heap »). C'est une des limites que le portage pourra
lever, maintenant qu'on sait où elle est écrite.

### Le système de fichiers, par où tout passe

Les fonctions que le jeu appelle le plus souvent par pointeur forment un groupe
cohérent, relevé par `SFC_STAT_CALL` sur neuf cents images de menus — c'est par là
que passent toutes les lectures, donc tout ce que sert `data.c` :

| adresse | appels | ce qu'elle fait |
|---|---:|---|
| `0x0202260C` | 42 174 | la méthode « écrire » d'un transfert qui vise la mémoire : elle réordonne ses arguments et appelle la copie |
| `0x02066184` | 27 622 | une méthode virtuelle vide — deux octets, `bx lr` |
| `0x02021C4C` | 21 132 | lit l'entrée suivante d'un répertoire : un octet de longueur dont le bit 7 dit « répertoire », le nom, puis douze bits d'identifiant |
| `0x02021DC4` | — | lance une lecture sur l'archive, par sa méthode virtuelle en `[archive+0x50]` |
| `0x02020030` | 11 276 | charge les registres de division du matériel (`0x04000290`, `0x04000298`) |
| `0x0202C3B4` | — | la copie mémoire du SDK, qui gère les adresses impaires |

Le jeu passe donc l'essentiel de son démarrage à parcourir son arborescence — celle
qu'on lui sert maintenant depuis `data/files/`.

### Les structures du jeu

Chaque module déclare ses types au démarrage — nom et taille — et
`tools/classes.py` relève ces déclarations dans le code : **1 602 classes**, dans
[`classes.md`](classes.md). On y lit la taille de tout ce que le jeu manipule :

| classe | octets | ce que c'est |
|---|---:|---|
| `Actor` | 428 | tout ce qui vit dans le monde |
| `ArwingDynamics` | 676 | le vol de l'Arwing : la plus grosse structure du pilotage |
| `ArwingVehicle` | 452 | l'appareil lui-même |
| `ProximityQuery` | 248 | une recherche de voisinage dans la grille |
| `CollidableObject` | 188 | ce qui peut être heurté |
| `CollisionCell` | 136 | une cellule de la grille de collision |
| `ScriptParser` | 76 | la lecture d'un script de niveau |
| `ColQuadNode` | 32 | un nœud de l'arbre de partition |

Les noms imbriqués (`ArwingChargeShotCallback::TargetInfo`) confirment qu'ils
viennent bien du code d'origine, avec sa hiérarchie de classes.

### À quoi sert chaque overlay

Le jeu nomme ses objets : chaque fonction qui en crée un désigne une chaîne, et
`tools/symboles.py` rassemble ces mentions dans [`annuaire.md`](annuaire.md). Le
tableau ci-dessous les résume — c'est une lecture, appuyée sur les identifiants
cités, non une certitude comme l'est une table d'enregistrement.

| module | ce qu'il nomme | lecture |
|---|---|---|
| ARM9 | `Life`, `Attack`, `Explosion`, `Spawn`, `Trigger`, `Radar`, `Motion` | le cœur : états d'ennemi, scripts, scènes |
| ovl0 | `Actor`, `ActorGroup`, `CollidableObject`, `CollisionCell`, `ColQuadNode`, `ColVolume`, `ProximityQuery` | acteurs et collision — le socle du monde |
| ovl2 | `ArwingVehicle`, `ArwingDynamics`, `ArwingCockpitModule`, `ShotActor`, `LaserReflect`, `SmartBomb` | **le pilotage de l'Arwing et le tir** |
| ovl3 | `Strategy`, `Strategy_Fog`, `FogMapSubScene`, `TransitionScreenSubScene` | la carte stratégique et son brouillard |
| ovl4 | `MenuBase`, `TitleSubScene`, `WiFiMenuScene`, `ok_button`, `back_button` | les menus et l'écran-titre |
| ovl5, ovl6, ovl7, ovl17 | `EnemyMothership`, `BeamLaser`, `MorasBlob`, `SearchBeam` | vaisseaux-mères et boss |
| ovl8, ovl12, ovl16, ovl18, ovl19, ovl20 | `BasicFlyVehicle`, `Patrol`, `Swoop`, `Charge`, `EnemyKingDodora`, `PorcupineSpike` | les comportements d'ennemis |
| ovl9 | `SLIPPY`, `OCTMAN`, `ZAZAN_2`, `ZazanIntroMsg`, `ZazanDeathMsg` | les dialogues et les personnages |
| ovl10 | `MissionClearScene`, `MissionFailedScene`, `RootMenu`, `button_wifi` | les écrans de fin de mission |
| ovl11 | `Pigma`, `PigmaA`–`PigmaD`, `ExposeCore` | le combat contre Pigma |
| ovl13 | `ShieldA`–`ShieldE`, `AChargeA`–`AChargeG` | boucliers et charges |
| ovl14 | `DetailBoard`, `DetailInfo`, `eWolf`, `eSandWorm`, `Arwing`, `_twin`, `_plasma` | le briefing et la fiche des unités |
| ovl15 | `ArwingConfig`, `TwinLaser_Item`, `SmartBomb_Item`, `Stealth_Item`, `NetworkBattle` | l'équipement de l'Arwing |
| ovl23 | `EnemyStorageData`, `CorneriaEnemyOverlay`, `AsteroidEnemyOverlay`, `AquasEnemyOverlay`… | **le chargeur des ennemis, planète par planète** (`EnemyStorage.cpp`) |
| ovl24 | `gpi*.c`, `gt2*.c`, `sb_server.c`, `hashtable.c` | la bibliothèque réseau GameSpy |
| ovl25 | `DownloadManager.cpp`, `loadArchive`, `info_board` | le téléchargement de contenu |

Les overlays 1, 21 et 22 ne nomment rien : 21 et 22 ne font que trente-deux
octets — des emplacements réservés, pas du code.

### Le mécanisme des scripts de niveau

Les niveaux sont décrits par des **scripts en texte clair**, présents dans la
ROM (`assets/cooked/_srl/<planète>/Scripts/`). L'interpréteur compare les noms
de commandes à une table.

**Table des commandes : `0x020A3FE0`**, entrées de 12 octets — pointeur de
chaîne, fonction, données. C'est le point d'entrée pour retrouver n'importe
quelle commande.

| commande | fonction |
|---|---|
| `SetHeightGridSize` | `0x02019C14` |
| `RegistHeightGridObject` | `0x02019838` |
| `RegistVolume` | `0x0201A27C` |

Commandes connues : `RegistModel`, `RegistModelSet`, `RegistHeightModel`,
`RegistHeightGridObject`, `RegistVolume`, `LoadHeightMap`, `SetHeightGridSize`,
`ClearHeightManager`, `CreateEnemy`, `CreateEnemyTemplate`, `CreateMarker`,
`SetEnemyState`, `SetConfigParam`, `SetStageMode`, `PushBackGround`,
`SetBackGroundColor`, `SetBackgroundEffect`, `Include`.

### Le terrain

**Gestionnaire : `0x021D1E94`**

| offset | contenu |
|---|---|
| +0x00, +0x04 | pointeurs sur les cellules |
| +0x08 | taille d'une cellule — 2 560,0 unités |
| +0x0C | sa moitié |
| +0x10, +0x14 | indice de la première cellule : −4 |
| +0x18, +0x20 | coordonnées correspondantes : −10 240,0 |
| +0x58… | tableau de pointeurs, neuf par ligne (grille 9×9) |

- **`0x021780F0`** place une cellule : `position = base + indice × taille`.
- **`0x02178320`** l'appelle en boucle, après avoir converti la position du
  joueur en indice de cellule.

La carte fait donc 9×9 cellules de 2 560, soit 23 040 unités. Le jeu en dessine
8 à 11 par image, dans une fenêtre qui glisse avec le joueur ; le décor atteint
alors 7 568 unités, pour une projection qui porte à 16 384.

### La distance d'affichage

**Ce qui décide qu'un objet est dessiné** : `0x01FFBF8C`, dans la mémoire rapide
(appelée par le relais `0x01FFBF84` depuis le rendu de scène `0x020A3620`). Elle
bâtit six plans à partir de la caméra — proche, quatre côtés, lointain —, les porte
dans le monde, puis parcourt la liste des objets de la scène (tête en
`0x027E14D8`, premier nœud en `+8`, fin en `+4` ; l'objet commence 0x4C octets
avant son nœud, sa position en `+0x10`). Un objet est écarté dès que son centre
est derrière un plan de plus que son rayon, que rend sa méthode virtuelle
`[[objet+0xC]]+0x10`. Les survivants sont rangés par profondeur dans 1 024 casiers
(`0x020CAB50`, masque en `+0x2000`), puis dessinés par `0x01FFB994` (translation,
rotation, échelle : commandes `0x1C`, `0x1A`, `0x1B`). Les cellules de terrain sont
des objets comme les autres.

**Les réglages qu'elle lit**, en mémoire rapide des données :

| adresse | contenu | valeur |
|---|---|---|
| `0x027E14F8` | octet : rang de la caméra | 1 |
| `0x027E14FC` | cosinus du demi-champ | 0,866 (30°) |
| `0x027E1500` | sinus du demi-champ | 0,5 |
| `0x027E1504` | **distance de coupure** | **4 000,0** |
| `0x027E1508` | rapport largeur / hauteur | 4/3 |
| `0x027E150C` | plan proche | 2,0 |

Tout est recopié à chaque image par la méthode de la caméra **`0x02066A34`**
(classe `Camera`, 396 octets) depuis ses champs `+0x170` à `+0x17C` — sauf la
distance de coupure, **une constante écrite en dur** (`0x00FA0000`). Quand l'octet
`+0x16C` est levé, la même méthode calcule aussi la projection (`0x020271A4`, dans
`+0xE4`) avec un plan lointain de 5 000,0 (`+0x180`) ; la matrice chargée est
normalisée au quart, et l'arrondi de ses termes place le plan lointain effectif à
16 384.

**Ce qui cache le lointain** : l'**anneau d'horizon**. `PushBackGround BG`,
`Clouds` et `Sun` (`0x020195F0`, scripts `StageSetup.txt`) empilent des modèles
d'arrière-plan que `0x0218DA9C` (overlay 0) dessine à chaque image en
(caméra.x, hauteur, caméra.z) — la hauteur étant celle de la caméra ou zéro, plus
un décalage du modèle (`+0x30`, et l'octet `+0x38` qui choisit) —, tournés par trois
rotations (`+0x10`, `+0x14`, `+0x18`), **sans mise à l'échelle**. Le modèle `BG` est
un bandeau de panorama (texture 512×64) posé en cercle vers 4 000 à 5 000 unités,
opaque : tout ce qui est plus loin est caché derrière lui. Le jeu dessine donc du
terrain jusqu'à 6 500 unités que personne ne voit. L'élément d'arrière-plan le plus
lointain est lui aussi à 6 500 unités.

C'est pourquoi repousser la seule distance de coupure ne change **aucun pixel**
(vérifié à ×3 : trois fois plus de cellules dessinées, 0 à 88 pixels différents) :
il faut reculer l'anneau du même facteur. L'agrandir autour de l'**œil** de la
caméra — hauteur éloignée de celle de l'œil, première rotation multipliée — le
laisse couvrir exactement la même part de l'écran : seule sa profondeur change.
Passé le facteur 2,4, l'arrière-plan franchirait le plan lointain de 16 384, qui
recule alors aussi. C'est la modification `SFC_PORTEE` (§ 5 ter) ; à ×2, sur le
rejeu des dernières missions, le sol de la station s'étend jusqu'à l'horizon là où
la portée d'origine le fondait dans la brume.

**Ce que la portée change au jeu.** Ni ses règles ni son rythme : le même tour à la même
image, à ×1, ×2 et ×3, et la caméra — donc le joueur — suit exactement la même trajectoire tant
que rien ne diverge. Mais le temps : dessiner coûte à la console, et le portage le compte (chaque
arête arrière avance l'horloge simulée). L'horloge d'un tour se décale de quelques centaines de
cycles dès le premier vol (`0x0216C078`, l'objet de la boucle principale : durée du tour et repère
en tiques), et ce que le jeu tire de son horloge fine s'en ressent : sur le rejeu de Titania rien
de visible (même chronologie, même trajectoire, quelques sons au volume légèrement différent) ;
sur les dernières missions, la même partie rejouée à ×2 ne rate plus la balise que le joueur avait
ratée, et la mission ne s'arrête plus au même moment. Ce n'est pas la visibilité qui agit sur la
logique — seul `0x01FFBF8C` lit la distance de coupure, et `rand()` (`0x02004210`, état en
`0x020AB9BC`) reste identique aux deux portées —, c'est le temps. Une partie enregistrée ne se
rejoue donc à l'identique qu'à la portée où elle a été jouée, et les empreintes d'un rejeu, qui
hachent la mémoire entière (liste de dessin comprise), ne peuvent pas servir de référence avec
elle.

**Pourquoi pas une seconde passe hors du temps.** Essayé : laisser la passe d'origine intacte
(4 000 unités), puis, au retour de `0x01FFBF84` (`0x020A3740`), relancer le même dessin horloge
figée (`g_temps_fige`, budget d'arêtes et ligne d'interruption rendus, processeur et pile rendus),
en écartant au plan lointain ce que la première passe a déjà dessiné. Les travaux de dessin
(`0x01FFAF54`) s'exécutent tous sur-le-champ en vol (`[0x027E0058]` = 1, la file n'est jamais
employée), si bien que la passe ne peut pas rester bloquée, et elle ne descend pas à plus de
1 696 octets sous la pile. Mais même quasi vide (facteur 1,001) elle décale le son dès l'image
584 : ses commandes allongent la liste que le jeu parcourt ensuite **avec son propre code** — un
distributeur logiciel (`0x01FFB758`, table de sauts par commande) —, et ce parcours coûte du temps.
Une portée parfaitement invisible exigerait de sortir ces commandes de la liste du jeu et de les
injecter directement dans le moteur 3D, au bon endroit du flux.

**Il n'y a pas de distance de simulation.** Deux relevés des objets de la liste de rendu à vingt
images d'écart (`SFC_DIAG_OBJETS`) : des objets à 5 000–6 400 unités, hors de portée d'affichage,
ont bougé de 166 unités. Le jeu fait vivre toute la mission. Les « portées » de `config.txt`
(poursuite `Chase_Range` 2 000, attaque `Max_Attack_Dist` 1 000, tourelles et missiles
`Enemy_LaserTurret_AttackLength` et `Enemy_TankEnemy_MissileRange` 2 000…) ne sont pas lues :
aucun de ces noms n'existe dans le code, ce sont les réglages d'ennemis d'un prototype (voir
« La portée des chasseurs »). Et aucun niveau n'emploie `CreateLODModel` (seulement des scripts
d'essai et des lignes en commentaire).

**Au-delà de ×2.** Un élément d'arrière-plan (troisième modèle, texture `1A83A79C`, translucide)
passe à travers l'œil de la caméra : d'origine, sa partie la plus proche est coupée par le plan
proche (8 unités). Agrandir l'arrière-plan d'un facteur k autour de l'œil fait franchir ce plan à
la partie comprise entre 8/k et 8 unités : invisible à ×2, un grand rectangle sombre à ×3 quand la
caméra regarde le vaisseau mère. Le lanceur s'en tient donc au double.

### La portée des chasseurs

Un tir va sa vitesse fois sa durée de vie. Voir deux fois plus loin sans pouvoir y tirer n'aurait
guère de sens : `SFC_PORTEE` allonge aussi les tirs (§ 5 ter).

**Le laser d'Arwing** — celui du joueur et de ses coéquipiers — est un objet de l'overlay 2 mis à
jour par **`0x021DF7A4`** : 4 500 unités par seconde (`Shot_Speed`, le seul réglage
d'`ArwingConfig` qui le concerne), un âge en `+0x48` qui croît de `0x88` (1/30 s) par tour, et la
mort passé **`0x1199` — 1,1 s, en dur** (littéral lu en `0x021DF97E`) ; en multijoueur la durée
vient de `MultiplayCfg` (`0x020562B8`, ou `0x020562AC` selon l'octet `+0x3C`). La mort passe par
`0x021E4C50`, qui fait lever l'octet `+0x14` de son effet. Mesuré : 33 tours, 66 images, environ
4 950 unités. Ce qu'on voit est un **effet de trait** de l'overlay 0 (`0x0217FD84`, classe
`0x021CCCA8`) : tête en `+0x68`, queue en `+0x50`, longueur plafonnée à `+0x5C` (150, un tour de
vol), posé à mi-longueur et étiré ; `0x021DEEBC` le crée et le place. Les effets sont des éléments
d'une liste que parcourt `0x02173354` : méthode `+0xC` pour la mise à jour, `+8` pour « fini ? »
(l'octet `+0x29`, que lève la fin d'une piste d'animation non bouclée, `0x02171120`), destruction
au tour suivant quand l'octet `+0x14` est levé.

**Les tirs ennemis** passent tous par la même initialisation, **`0x021E0DB4`**, appelée par 70
fonctions de 16 overlays d'ennemis. Elle reçoit une fiche : direction (`+4`), vitesse par tour
(`+0x10`), longueur du trait (`+0x14`), largeur (`+0x18`), **durée de vie** (`+0x1C`, en secondes
20.12, rangée en `+0x48` du tir), effet (`+0x20`). Le chasseur ennemi de l'overlay 7 tire à environ
1 500 unités par seconde pendant 1,5 s : 2 240 unités.

**D'où les ennemis tirent** : il n'y a pas de réglage commun. Les portées de `config.txt` sont
mortes — seul `Enemy_Shot_KillTime` existe dans le code, et sa ligne y est en commentaire. Chaque
comportement d'ennemi (les `SetCallback` des scripts, plus d'une centaine) a ses distances en dur
et une machine à états nommés : `0x02184C94(objet, fondu, « Default »)` change d'état, et chaque
état est une table de sept mots dont l'entrée, la sortie et la mise à jour (celle de Star Wolf est
en `0x022560DC`, overlay 9). Le code des ennemis compare environ 240 fois une longueur tout juste
calculée (`0x01FFED88`) à une constante — 155 cas distincts, les comportements étant recopiés d'un
overlay de mission à l'autre. Dans la chaîne du tir (les fonctions qui appellent `0x021E0DB4` et
leurs appelants), il y en a 24 : **17 portées maximales de tir**, de 1 500 à 8 000 unités — le
chasseur de l'overlay 7, par exemple, repasse à l'état « Default » sans tirer au-delà de 2 500 —,
et 7 bornes basses (« trop près pour tirer ») ou tests de proximité. Beaucoup d'ennemis tirent
enfin sur une minuterie une fois engagés, sans condition de distance : Star Wolf (overlay 9, état
de mise à jour `0x0224C284`) a tiré 172 des 195 tirs ennemis du rejeu des dernières missions.

**Le laser Plasma**, troisième niveau après `SingleLaser` et `TwinLaser` (qu'anime tous deux
`0x021DF7A4`, le second avec deux traits), a sa propre mise à jour, **`0x0221B47C`** : même âge en
`+0x48`, mort passé **`0x1CCC` — 1,8 s en dur** (littéral lu en `0x0221B5B6`), 2 200 à
3 200 unités par seconde, soit 3 900 à 5 600 unités. Sa taille (`0x0221B720`) vaut
1 − 5 × (âge − 1,6 s), bornée entre 0 et 1 : il rétrécit pendant ses deux derniers dixièmes de seconde.

**Le verrouillage de la charge n'a pas de portée maximale.** Toutes les 0,25 s (`[viseur+0x18C]`),
**`0x02210D04`** parcourt la liste 2 du monde de collision (`0x021D82A0`, neuf listes de huit
octets : les ennemis) et teste chaque ennemi verrouillable — en vie, au-dessus du sol, octets
`+0x96` et `+0xF8`, `[collisionneur+0x14]` — par **un cône** : de la position de l'Arwing vers le
point du viseur (`[viseur+0x50]`), `0x021B11D8` puis `0x021B0934`, qui touche si la distance entre
le centre de l'ennemi et l'axe ne dépasse pas son rayon plus t × tan 30° × 16/128 — t la distance le
long de l'axe, 16 le champ `+0x10` du nœud de verrouillage (`[pilote+0x208]`) : un demi-angle
d'environ 4°. Rien ne borne t. Le centre de l'ennemi doit aussi tomber à l'écran, à 16 pixels près
(`0x020663E4` projette sans plan lointain, `0x021AC29C` borne 256 × 192). Le candidat retenu
(`[viseur+0x178]`, le moins verrouillé puis le plus proche) est inscrit dans le groupe de
verrouillage (`0x021ACB40`, réserves `MultiLockGroup` et `MultiLockSight`, 64 de chaque) ; le
contact qui en naît déclenche le tir (`0x021EC7DC` crée le projectile, `0x021E6CEC` le lance, et
`0x021E6740` ajoute la cible au composant `[Arwing+0x80]` : cibles en `+0x94`, nombre en `+0xD4`,
niveau en `+0x10`, dégâts 20 × {0, 1, 2, 4, 8, 16} selon lui, `0x021E5EB8`). Mesuré sur le rejeu
des dernières missions : 524 ennemis testés entre 0 et 10 269 unités, un touché à 9 479 — bien au-delà
des 4 000 que la console dessine. Le projectile chargé (`0x021E69C0`, guidage `0x021E72C0`) file
dix tours tout droit à 6 000 unités par seconde (compteur `+0x64` de 15 à 0), puis suit sa cible à
2 300 unités par seconde jusqu'à 300 tours — dix secondes, plus de 20 000 unités. C'est donc ce que
l'on voit qui borne ce que l'on verrouille : l'affichage doublé double la portée utile sans rien
changer au verrouillage. `LockSightRange` (`ArwingConfig`, 24, de 1 à 256) est un réglage de débogage
que rien ne lit.

### Les anneaux (overlay 23)

Les anneaux sont des objets créés par script :
`CreateEnemy TriggerGate SeedTrigger BasicFlyVehicle ITEM_OBJECT …` avec
`RegistVolume TriggerGate 110`.

| élément | adresse |
|---|---|
| création de `SeedTrigger` | `0x02266924` |
| table de méthodes | `0x0226F148` |
| `SeedTriggerCallback` (rappel de passage) | `0x022668DC`, offset 0x20 de la table |
| `InvisibleTriggerCallback` | `0x02265F18` |
| mise à jour de l'objet | `0x02266468` |
| calcul de détection (machine à états) | `0x022652EC` |
| test final | `0x0226529C` |
| accesseur de position | `0x0216CB64` → objet + 0x168 |

**Le mécanisme** : la mise à jour appelle le calcul, puis le test. Le test
compare le champ **objet + 0x20** au seuil **0x1000** ; ce champ est incrémenté
de 0x88 par image dans l'état 2 de la machine à états, soit une demi-seconde
avant que le rappel se déclenche. Le calcul constate la proximité au seuil de
200,0 unités, chargé en `0x0226531A`.

Champs connus de l'objet : `+0x0C` seconde table de méthodes, `+0x10` état du
calcul, `+0x14`–`+0x1C` échelle, `+0x20` champ testé, `+0x24` rotation,
`+0x168` position.

À noter : ce jeu n'emploie jamais les commandes de test du moteur 3D
(`BOX_TEST`, `POS_TEST`, `VEC_TEST`) — ses collisions sont calculées en logique
pure.

### Fonctions coûteuses

Relevées par échantillonnage (`SFC_PROFILE_ARM9`, `SFC_PROFILE_ARM7`) :

| adresse | part | remarque |
|---|---|---|
| `0x020A0E4C` | 16,7 % | |
| `0x020732F8` | 13,9 % | même code que la suivante |
| `0x02073580` | 13,5 % | aux constantes près |
| `0x01FFEE38` | — | allocateur, appelé par les deux |
| `0x03804A54` (ARM7) | 70 % du coprocesseur | traitement des messages du pilote sonore |
| `0x038044F4` (ARM7) | — | sa boucle de réception de messages |

Les remplacer par du code natif (`rt_hook`) serait le prochain grand levier de
performance, mais demande de comprendre ce qu'elles font.

### Les commandes du vol, et la manette

Le jeu se pilote au stylet. En vol, à chaque tour, le **contrôle au stylet**
(`ArwingCockpitRelativeCtrl`, 144 octets, table de méthodes `0x02222BB8` ; sa
mise à jour est la méthode 3, `0x021E9D64`) tire du stylet les commandes du
**cockpit**, et les range dans un bloc de l'objet du cockpit — son `+0x68`, que
rend `0x021E9428` ; le tour du cockpit, `0x0220EFC4`, les lit aussitôt après :

| champ du bloc | ce qu'il porte | qui le pose |
|---|---|---|
| `+0x30`, `+0x32`, `+0x34` | tangage, lacet, inclinaison (demi-mots, `0x10000` le tour) | stylet décalé de (*dx*, *dy*) points depuis son point d'appui : −0,85·*dx* degrés de lacet, bornés à ±45 ; 0,85·*dy* (borné à ±25) fois 4/3 de tangage ; −1,2 fois le lacet d'inclinaison (`0x021EA55A`) |
| `+0x41`, `+0x42`, `+0x48` | tonneau à gauche, à droite ; 10 | la reconnaissance d'un cercle (`0x021EC1C4`) |
| `+0x43`, `+0x44`, `+0x46`, `+0x50` | turbo, frein, « en cours » ; minuterie de 5 | la double tape ; `0x021EBACC` décompte la minuterie et tient « en cours » ; ensuite `0x021EBBB8` compare « en cours » au stylet posé et, stylet levé, efface tout et lève `+0x58`, que le cockpit lit pour arrêter l'Arwing (`0x021EE294`) |
| `+0x59` | interrompre | le vol, quand l'Arwing ne peut plus tenir le turbo ou le frein |
| `+0x5A` | virage serré | le stylet au bord de l'écran (lacet × 1,5) |

Le contrôle ne lit le stylet que s'il « tient le manche » — l'octet
`0x02222C6C`, que l'écran tactile lève quand l'appui commence hors des boutons —
et que l'Arwing obéit aux commandes (ni pendant une manœuvre, ni pendant une
scène). Le looping et le demi-tour n'y passent pas : leurs boutons
(`0x02205440`) demandent à la mission s'ils sont permis (`0x021A209C`,
`0x021A2128`), puis appellent `0x021EE528` et `0x021EE550` sur la dynamique de
l'Arwing (`0x02188AD0`).

La poursuite d'un missile prend un autre contrôle, **sur rails**
(`ArwingCockpitRelRailsCtrl`, table de méthodes `0x02222C1C`, mise à jour
`0x021EAF90`), qu'aucune des deux parties de référence ne traverse. Il pilote
lui aussi depuis un point d'appui, le point du premier tour d'un appui, mais ne
prend que 0,3 du décalage (`0x4CC`) : −0,3·*dx* degrés de lacet, bornés à ±35 ;
0,3·*dy* (borné à ±45) fois 4/3 de tangage. Il ne demande pas si le stylet tient
le manche pour piloter, seulement s'il est posé (`0x021EB016`, r1), et n'y passe
que si l'Arwing obéit (`+0x94` du contrôle baissé) ; le manche ne sert qu'au
premier tour d'un appui (`0x021EB02A`), à choisir le point d'appui — le point lu
(`+0x10`) ou le dernier lu hors du manche (`+0x54`). Il reconnaît le cercle comme
l'autre (`0x021EB080`), et le turbo et le frein passent par les mêmes fonctions.
Il ne fait rien tant que la caméra active (`0x020CF430`, `+0x114`) n'est pas
celle de l'Arwing (`0x021FA7F8`). Le looping, le demi-tour et la bombe n'y sont
pas permis.

Tout combat, la poursuite comprise, s'ouvre sur un plan qui montre la cible de
la mission (`0x02046E58`, « DemoTarget… ») ; sa caméra passe devant celle de
l'Arwing, et aucun contrôle ne pilote. Passé deux secondes (son âge en `+0xD8`
au-delà de `0x2000`), il se referme dès qu'une touche vient d'être pressée
(masque `0xFFF`) ou que le stylet est posé (`0x0204863C`, r0) ; à quatre
(`0x4000`), de lui-même. Le plan fini, la fonction détruit sa caméra.

Relevé en rejouant la partie `obs52` jusqu'en plein vol, puis des gestes
fabriqués ; la caméra (`SFC_DIAG_CAMERA`), la mémoire à des images choisies
(`SFC_RAM_IMAGES`) et les points d'observation (`SFC_WATCH`) disent le reste :

| geste au stylet | ce que fait le jeu |
|---|---|
| décalé à l'horizontale | virage jusqu'à 69° par seconde ; vers le haut, l'Arwing monte |
| les trois premières images d'un appui | le point d'appui, borné à 32 points du centre en largeur et 24 en hauteur |
| tenu immobile au point d'appui | le virage s'arrête ; levé, l'appareil garde son élan |
| deux appuis, le second moins de 13 images après le premier, et tenu | turbo en haut de l'écran, frein en bas — 4 images posé et 4 levé suffisent, 3 et 3 non ; un premier appui long (40 images) ne compte pas |
| un peu plus d'un tour de cercle, 12 points de rayon ou plus | tonneau, dans un sens ou dans l'autre ; 8 points ne suffisent pas |

Le vol se reconnaît à deux choses : la pile des scènes (objet global
`0x0216BED4`, hauteur en `+0x188`) a deux étages — un sur la carte de stratégie
et dans les menus —, et aucune fenêtre n'est ouverte par-dessus. Le
gestionnaire des fenêtres (`0x0216C2E8` : leur liste, leur nombre en `+0x80`)
écrit en `+0x84` le nom de celle qui s'ouvre ainsi : « PauseWindow » à la
pause, rien pendant les trois vols de `obs52`. La pause garde ses deux étages,
et ses boutons Continuer, Rejouer, Quitter sont sur l'écran tactile.

**La manette** (`runtime/manette.c`) donne ces commandes directement, chacune à
son bouton — aucun geste fabriqué —, par dix points de modification (§ 5 ter) :

- le pilotage : aux deux endroits où la mise à jour demande si le stylet tient
  le manche et s'il est posé — pour piloter, et pour l'aide au verrouillage —,
  elle répond oui et place elle-même le point du stylet : au centre au premier
  tour, que le jeu prend pour point d'appui, puis décalé comme le stick (53
  points de côté et 29,5 en hauteur au bout de sa course : juste de quoi
  atteindre les bornes). L'échelle, les bornes, l'aide au verrouillage,
  l'inclinaison restent le code du jeu ; les doubles tapes, qui lisent le vrai
  stylet, ne peuvent pas naître par mégarde. Le cercle, lui, se reconnaîtrait
  sur ce stylet-là — tourner le stick en rond ferait un tonneau : le joueur ne
  le veut pas, et la fenêtre de reconnaissance (`+0x3C` du contrôle, huit tours)
  expire à chaque tour quand la manette pilote. Pendant la poursuite d'un
  missile, le contrôle sur rails reçoit la même réponse et le même point, à la
  moitié de ses bornes : 58,5 points de côté et 75,5 en hauteur au bout de la
  course, 17,5° de lacet et 30° de tangage (117 et 151 points toucheraient ses
  bornes de 35 et 45 : le joueur a trouvé la poursuite trop sensible ainsi) ;
- le plan d'ouverture d'un combat : quand la manette pilote, le stick poussé ou
  un bouton du vol tenu comptent comme le stylet posé — ce qui pilote l'Arwing
  referme le plan, passé ses deux secondes, comme le stylet sur la console ;
- le turbo et le frein, juste après la mise à jour : ce que pose la double tape,
  et la minuterie entretenue tant que le bouton est tenu ; relâché, le jeu
  arrête l'Arwing comme au stylet levé ; s'il l'a interrompu lui-même, il faut
  relâcher pour reprendre ;
- le tonneau : ce que pose la reconnaissance du cercle ;
- le looping et le demi-tour : les mêmes appels que leurs boutons ;
- la bombe : le même lancement que l'icône B glissée sur le radar, visé sur
  l'Arwing (ci-dessous).

**La bombe.** Lâchée sur le radar, l'icône B mène le gestionnaire de l'écran
tactile (`0x02205030`) à convertir le point du radar en position du monde
(`0x021ED228`), puis à la lancer : `0x021ECD8C(dynamique, point)`, qui n'en
prend que x et z — la hauteur, il la fixe à 3 000 — et fait livrer la bombe
intelligente par le ciel (« Livraison spéciale ! »). Le lancement refuse une
seconde bombe tant que la première vit (sa poignée en `+0xF4` de la dynamique),
mais ne regarde pas le stock : l'écran tactile n'offre l'icône que s'il en
reste. Le stock est dans la fiche du pilote — l'état de mission `0x021D68B8`
range ses fiches en `+4` (`0x021A2904`), le rang du pilote vient de
`0x021889A4` —, en `+0x70`, et l'écran tactile ne le décompte que pour ses
propres bombes, à la fin de la livraison (`0x022010AE`, hors partie en
réseau). La mission peut aussi interdire la bombe (`0x021A23B0`, l'octet `+0x75`
de l'état de mission, comme `+0x77` le looping et `+0x76` le demi-tour). La
manette vise l'Arwing — l'acteur du joueur (`0x02188B98`), position en `+0x3C`
—, demande la permission, lit le stock, lance, et décompte elle-même la bombe
qui part.

Mesuré : le turbo passe à 391 dès les vingt premières images au lieu de 215 au
stylet, et tient 498 ; le frein tient 83 ; le tonneau part huit images après
l'appui au lieu de vingt-trois ; le virage et la montée sont ceux du stylet
décalé d'autant, et répondent au stick en ligne droite — 6,9°/s de virage et
2,6° de montée par dixième de course, jusqu'à 69°/s et 26° (la montée retombe
ensuite d'elle-même, d'autant plus vite qu'elle est raide : c'est le jeu, au
stylet aussi). Au stick qui pilote, une zone morte ronde de 5 % et la commande
pleine à 95 % de la course ; la zone de 24 % que conseille Microsoft laissait
le premier quart sans effet. Deux bombes à Y : le stock de
Dash passe de 2 à 1 puis à 0, la troisième ne part pas, et l'icône B se vide.
Pendant la poursuite de missile de `obs3` (image 22 700) : le stick à droite
place le point en (186,5, 96), le lacet est à −17,5° et l'Arwing s'écarte de
638 unités en cent images (997 à la pleine échelle) ; à mi-course, −8,8° et
330 ; vers le haut, tangage −30° et il monte de 703 (815 à la pleine échelle,
où il bute plus tôt) ; le turbo et le frein tiennent ; RB fait le tonneau,
tourner le stick en rond non. Les poursuites se
trouvent dans `obs3`, `obs4`, `obs16` (vers l'image 22 400), `obs22` (63 596) et
`obs23` (112 145) ; sans manette, `obs3` rejouée par-delà le début de la
poursuite garde ses empreintes. Le stick poussé referme le plan d'ouverture
comme A ou le stylet : celui de la poursuite de `obs22` à l'image 63 836 au lieu
de 63 858, celui du premier combat de `obs52` à 6 534 au lieu de 6 654 — deux
secondes après son début, 6 398. Pendant un tour d'aperçu, les points donnent les commandes
tenues sans demander de manœuvre ni changer leur propre état : les empreintes
sont les mêmes avec et sans aperçus, turbo, looping, demi-tour et tonneau
compris. Un enregistrement note la manette à côté des touches (cinq colonnes :
sticks au bit près, boutons) et le rejeu la redonne : une partie jouée à la
manette se rejoue à l'identique. Sans manette, les points ne font rien : le jeu
est celui de la cartouche.

### Les pilotes, leurs armes et leur vie

Chaque pilote a un rang — celui de sa clé dans la table `0x021D77BC`, seize octets par clé : 0 `naus`,
1 `fox`, 2 `krystal`, 3 `falco`, 4 `slippy`, 5 `ash` (Dash), 6 `wolf`, 7 `leon`, 8 `panther`, 9 `octman`,
10 `bill`, 11 `lucy`, 12 `amanda`, 13 `cat` (Katt), 14 `peppy`, 15 `james`, 16 `jodie` — et une fiche, le
`PlayerState`, en `[0x021D68B8 + 4 + 4 × rang]` (`0x021A2914(0x021D68B8, rang)` rend fiche + 0x10). Au
démarrage, `SetupCharacters.txt` la remplit (ovl0 `0x021A2690`) :

| fiche | ce qu'elle porte |
|---|---|
| `+0x10` à `+0x1C` | les noms des modèles du vaisseau : la carlingue, les plaques de réacteur allumé et éteint, la flamme |
| `+0x20`, `+0x2C`, `+0x38` | la place des traînées, du laser simple, du laser double |
| `+0x44` | le laser : 0 aucun, 1 simple, 2 double, 3 plasma, 4 tir courbe (`ArcShot`), 5 `Zapper` |
| `+0x48` | le verrouillage : 0 aucun, 1 simple, 2 multiple, 3 large |
| `+0x4C`, `+0x70` | les bombes : capacité, stock |
| `+0x50`, `+0x54` | la taille du bouclier et de la jauge : petit, moyen, grand |
| `+0x68` | le bouclier qui reste, d'un combat au suivant |

Fox part avec le laser simple, le verrouillage simple, deux bombes, un bouclier moyen et une grande jauge.
L'histoire change son laser et son verrouillage (`SetMemberWeapon fox Laser Twin|Plasma`, `… Lock Multi` :
ovl10 `0x0223DDD0`) ; la sauvegarde les retient dans un octet, `0x021D9748` (le laser dans les quatre bits
bas, le verrouillage dans les hauts, `0xFF` sans changement), que `0x020449F4` recopie dans la fiche à chaque
chargement de la carte. Le vaisseau n'en garde pas de copie : le tir relit la fiche à chaque fois.

**Le tir** : `0x021FE1E4` (méthode `+0x48` du véhicule, le pilote en `+0x28`) choisit la fonction de tir du
type de laser — simple `0x021FFE94` (10 de dégâts), double `0x021FF840` (15, deux traits), plasma
`0x021FF338` (20, dix au plus en vol), tir courbe `0x021FEFC8` (30), Zapper `0x021FEB8C` (120) ;
`0x021FE178` en fixe la cadence (0,1 s ; 0,3 s pour le tir courbe ; 0,8 s pour le Zapper, plus 0,8 s
d'attente en `0x021FE140`). Les dégâts d'un tir sont en `+0xF0`.

**Le vaisseau** : quatre pièces par pilote (`3D/Starfox/…`), à la même échelle ; la collision est un volume
commun (`Arwing`). Au début d'un combat, la création du joueur (ovl0 `0x02188F1C`) fait charger les fichiers
de la seule clé du pilote (`0x0221F828`), puis le véhicule lit dans la fiche ses quatre noms de modèle et la
place de ses traînées (ovl2 `0x021FE61C` à `0x021FE78C`).

**La dynamique de l'Arwing** (`ArwingDynamics`, 676 octets) : la vie en `+0xB8` (20.12 ; 100 = `0x64000`), son
maximum en `+0x10` (60, 100 ou 130 selon le bouclier), recopiée à chaque image dans la fiche (`+0x68`) ; la
jauge du turbo, du frein et des tonneaux — une seule — en `+0xBC` (la part dépensée, 0 : pleine) sur `+0xC4`
(25, 50 ou 75 : la dynamique la demande en se construisant à la méthode `+0x14` du véhicule, `0x021FE9D8`,
qui la tire de la taille rangée dans la fiche, `+0x54`). `0x021EDBC4` la remplit de 0,5 par tour et y ajoute 1,0 par tour de turbo, de frein ou de
tonneau ; pleine, elle se verrouille trois tours (`+0x10C`) et le cockpit interrompt la commande. Le pilote
est en `+0x84`, l'acteur en `+0x20` ; l'acteur du joueur, `[[0x021D3260]] − 0x1C`.

**Les dégâts** passent par deux fonctions. Celle de l'Arwing, ovl2 `0x021F071C` (r0 la dynamique, r1 l'acteur
touché, r2 les dégâts) : tirs, missiles, chocs contre un ennemi, le sol ou un bâtiment, attaques des boss —
et c'est là seulement qu'il meurt. Celle des ennemis et des boss, ovl0 `0x0216CC8C` (r0 l'acteur, r1 les
dégâts, r2 l'attaquant, souvent nul) : la vie en `+0xE8` (celle de `SetLife`, fois 4 096), la mort par
`0x0216D344`. Les dégâts viennent du `+0xF0` de ce qui frappe ; le tir chargé fait 20 × {0, 1, 2, 4, 8, 16}
selon le niveau de sa cible (`0x021E5EB8`), la bombe 100. Les types de collision : 0 joueur, 1 tir du joueur,
2 ennemi, 3 tir ennemi, 4 objet du décor, 5 objet à ramasser, 6 bâtiment, 7 attaque du décor.

### Divers

- **`0x02075DDC`** : calcul vectoriel du SDK (multiplications-accumulations
  64 bits), en ARM, traduit correctement.
- La sauvegarde du jeu occupe **908 octets, de 0x09834 à 0x09BBF**, signature
  `Manta_2_0710a`. Le premier mot (0x09834) est l'en-tête que le jeu relit pour
  reconnaître une partie.
- Projection en combat : plan proche 8,0, plan lointain 16 384,0. **La matrice
  est normalisée au quart** (m11 = −1024) : sans corriger ce facteur, toutes les
  distances se lisent quatre fois trop courtes.

---

## 3 bis. Les registres matériels traités

Adresses relatives à `0x04000000`. Cette liste est celle du code : un registre
absent est un registre que le moteur ignore, ce qui peut être volontaire (le jeu
ne s'en sert pas) ou une lacune à combler si un défaut pointe vers lui.

### Affichage et balayage

| offset | nom | remarque |
|---|---|---|
| `000` / `1000` | DISPCNT | mode, couches actives ; `1000` pour le second moteur |
| `004` | DISPSTAT | masques d'interruption ; l'état du balayage est commun aux deux processeurs |
| `006` | VCOUNT | ligne courante |
| `008`–`00E` | BGxCNT | priorité, taille, base des tuiles |
| `010`–`01E` | BGxHOFS / BGxVOFS | défilement |
| `020`–`03E` | BGxPA–PD, BGxX/Y | fonds en rotation et mise à l'échelle |
| `040`–`04A` | WINxH/V, WININ, WINOUT | fenêtres |
| `050`–`054` | BLDCNT, BLDALPHA, BLDY | fondus et transparence |
| `06C` | MASTER_BRIGHT | luminosité |
| `064` | DISPCAPCNT | capture d'affichage vers la mémoire vidéo |
| `304` | POWCNT1 | alimentation ; son bit 15 dit quel moteur est en haut |

### Moteur 3D

| offset | nom | remarque |
|---|---|---|
| `060` | **DISP3DCNT** | brouillard, test alpha, anticrénelage. Piège corrigé : il était lu à `320`, où rien ne répond |
| `320`–`33E` | EDGE_COLOR, ALPHA_TEST_REF | |
| `350` / `354` | CLEAR_COLOR / CLEAR_DEPTH | |
| `358` / `35C` | FOG_COLOR / FOG_OFFSET | |
| `360`–`37F` | FOG_TABLE | 32 densités sur 7 bits |
| `380`–`3BF` | TOON_TABLE | |
| `400` | GXFIFO | flux de commandes, format empaqueté |
| `440`–`5FF` | commandes par registre | une adresse par commande |
| `600` | GXSTAT | piles de matrices, débordement, FIFO |
| `604` | RAM_COUNT | polygones et sommets du tampon en construction |
| `620`–`63C` | POS_RESULT, VEC_RESULT | résultats des tests — **jamais employés par ce jeu** |
| `640`–`6A3` | matrices lues | clip et vecteurs |

### Mémoire vidéo et DMA

| offset | nom |
|---|---|
| `240`–`249` | VRAMCNT_A–I, WRAMCNT ; `241` renvoie WRAMSTAT |
| `0B0`–`0DC` | DMAxSAD, DMAxDAD, DMAxCNT (quatre canaux) |
| `0E0`–`0EC` | DMA_FILL |

### Temps et interruptions

| offset | nom | remarque |
|---|---|---|
| `100`–`10E` | TM0–TM3 | recharge et contrôle ; **piège** : les deux demi-mots doivent être conservés ensemble, sinon la recharge se perd |
| `130` | KEYINPUT | touches |
| `132` | KEYCNT | |
| `136` | EXTKEYIN | touches X/Y et écran tactile (côté ARM7) |
| `208` | IME | autorisation générale |
| `210` | IE | interruptions activées |
| `214` | IF | interruptions en attente ; une écriture acquitte |
| `204` | EXMEMCNT | partage de la cartouche entre les processeurs |
| `300` / `301` | POSTFLG / HALTCNT | amorçage, mise en veille |

### Calcul

| offset | nom | remarque |
|---|---|---|
| `280` | DIVCNT | mode ; l'écriture relance le calcul |
| `290`–`29C` | DIV_NUMER, DIV_DENOM | |
| `2A0`–`2AC` | DIV_RESULT, DIVREM_RESULT | |
| `2B0` | SQRTCNT | |
| `2B4` | SQRT_RESULT | |
| `2B8`–`2BC` | SQRT_PARAM | l'écriture relance le calcul, y compris en 16 bits |

Vérifiés corrects : division et racine carrée donnent le même résultat que le
matériel, division par zéro comprise.

### Communication entre processeurs

| offset | nom |
|---|---|
| `180` | IPCSYNC |
| `184` | IPCFIFOCNT |
| `188` | IPCFIFOSEND |
| `100000` | IPCFIFORECV |

### Cartouche et sauvegarde

| offset | nom | remarque |
|---|---|---|
| `1A0` | AUXSPICNT | **piège** : le bit 6 annonce ce qu'il adviendra à la fin du transfert *suivant*. Refermer la transaction à son écriture faisait perdre un octet par transfert |
| `1A2` | AUXSPIDATA | un octet par écriture |
| `1A4` | ROMCTRL |
| `1A8`–`1AF` | commande de carte (huit octets) |
| `1C0` / `1C2` | SPICNT / SPIDATA | alimentation, firmware, écran tactile (ARM7) |

### Son (ARM7)

| offset | nom |
|---|---|
| `400`–`4FF` | seize canaux : contrôle, source, minuteur, point de reprise, longueur |
| `500` | SOUNDCNT | volume général, activation |
| `504` | SOUNDBIAS |

---

## 4. Les formats de données

| format | outil | remarque |
|---|---|---|
| modèles `.mdl` (MGL) | `tools/nitro/mgl.py`, `export_models.py` | 2 176 modèles, tous décodés sans erreur |
| listes d'affichage | `tools/nitro/displaylist.py` | mêmes commandes que le moteur 3D |
| scripts de niveau | texte clair | lisibles tels quels |
| textures | `SFC_DUMP_TEX`, `tools/ntfq.py` | 326 exportées |
| collision `.hgt` | `tools/export_collision.py` | cartes de hauteur, par cellule |
| fonds 2D `.nbfc`, `.nbfp`, `.nbfs` | `tools/fond_2d.py` | tuiles, palette RGB555, carte d'écran ; en PNG |
| textes | `tools/bmg.py` | lecture et réécriture (`--texte`, `--reecrire`) |
| son | `tools/sdat.py` | |
| compression `-cmp` | `tools/nitro/compress.py` | LZ10 dans les deux sens (`lz77_compress` : sans référence à distance 1) |
| index des données (`data/index.bin`) | `tools/build_data.py` | magie `SFCD`, puis version, nombre de zones et taille des noms (trois mots) ; une entrée `<offset, taille, position du nom>` par zone ; les chemins à la suite, séparés par des sauts de ligne |
| un fichier du jeu, dans `data/` ou une archive `.srl` | `tools/fichier_du_jeu.py` | `liste`, `lire`, `poser` ; un fichier qui grossit est rangé après le dernier, sa fin corrigée dans `fat.bin` et l'index — voir [la procédure](assets.md) |

**Piège du format des sommets** : `VTX_DIFF` ajoute ses dix bits signés **tels
quels** aux coordonnées 4.12, sans mise à l'échelle. Le facteur se mesure et ne
se devine pas : dans un maillage, les faces voisines partagent leurs arêtes,
donc un sommet posé en relatif doit retomber exactement sur un sommet absolu du
même modèle — c'est le cas de 76 % d'entre eux sans décalage, contre 3,6 %
décalé d'un cran. 411 modèles sur 2 176 emploient ce format, dont les chasseurs
ennemis et aucun Arwing du joueur.

---

## 4 bis. Les touches

Les touches du clavier se changent dans le lanceur, page « Contrôles » : un clic sur une commande, puis
sa nouvelle touche ; une touche déjà prise passe à l'autre commande, en échange. Le lanceur les garde dans
`lanceur.ini` (section `[touches]`) et les passe au jeu par `SFC_TOUCHES`. Celles d'origine :

| Touche | Ce qu'elle fait |
| --- | --- |
| `Z` `E` | A, B |
| `A` `S` | X, Y |
| `Q` `W` | L, R |
| flèches | croix directionnelle |
| `Espace` `Tab` | Start, Select |
| clic gauche dans la moitié basse | le stylet |
| manette | voir plus bas |
| **`F` tenue** | **l'avance rapide** : le jeu pense huit fois plus vite, pour passer une scène qui ne se saute pas (la fin du jeu). Rien d'autre ne change — c'est le même jeu, plus tôt ; on la relâche et il reprend son pas |
| `P` | **noter un défaut d'image** : les cent vingt dernières images montrées, écrites dans le journal (de quel tour, aperçu ou balayage, l'empreinte de chaque écran) — à presser juste après avoir vu le défaut |

### À la manette

Xbox (XInput) et PlayStation, Switch Pro ou générique (DirectInput) ; les
boutons se comptent par leur place. En vol, les commandes de Star Fox 64 3D :

| Xbox | PlayStation | en vol | ailleurs |
|---|---|---|---|
| stick gauche | stick gauche | piloter : poussé, l'Arwing monte ; tiré, il pique | la croix |
| A | ✕ | laser (A de la console) | A |
| X | □ | turbo, tenu | X |
| B | ○ | frein, tenu | B |
| RB, RT / LB, LT | R1, R2 / L1, L2 | tonneau à droite / à gauche | RB, LB : R, L |
| stick vers la montée + X | stick vers la montée + □ | looping | |
| stick vers la montée + B | stick vers la montée + ○ | demi-tour | |
| stick droit | stick droit | haut : looping ; bas : demi-tour ; côtés : tonneau | |
| Y | △ | bombe, lâchée sur l'Arwing, s'il en reste | Y |
| croix, Start, Back | croix, Options, Share | ceux de la console | ceux de la console |

Hors du vol — carte, menus, pause —, la manette ne touche pas au stylet : ce
qui ne se fait qu'au toucher se fait à la souris. Un bouton tenu au moment
d'entrer en vol ou d'en sortir ne compte qu'une fois relâché ; la souris garde
la main en vol dès qu'elle se pose. En hauteur, pousser le stick fait monter,
comme le stylet glissé vers le haut ; le lanceur donne le sens de Star Fox 64,
où pousser pique et tirer monte (lanceur, page Contrôles). Le looping et le demi-tour, qui
commencent en montant, se demandent le stick vers la montée. La bombe part à Y,
lâchée sur l'Arwing, s'il en reste au pilote.

## 4 ter. L'inventaire des accès (`tools/structures.py`)

Le code réécrit est fidèle mais sans noms : il manipule des décalages bruts. Pour modifier le jeu —
un vaisseau, une branche d'histoire —, il faut d'abord savoir ce qu'il touche. L'outil lit le code
produit par le générateur et relève, sans l'exécuter, chaque accès mémoire à un décalage.

| commande | ce qu'elle donne |
| --- | --- |
| `python tools/structures.py` | l'inventaire complet dans `analysis/acces.json` (20 683 fonctions, 266 462 champs) |
| `… --adresse 0x021D1E94` | toutes les fonctions qui touchent à cet objet et à ses deux cents octets suivants, décalage par décalage |
| `… --fonction 0x02178320` | ce que cette fonction touche, groupé par la base dont partent les décalages |
| `… --types` | les bases qui reçoivent exactement les mêmes décalages : vraisemblablement un même type |
| `… --objets` | croise l'inventaire avec un relevé d'exécution (`SFC_ETUDE_OBJETS=fichier`) : des objets réels, à leur adresse, avec leurs champs et les fonctions qui s'en servent |
| `… --dictionnaire` | **les types du jeu** : les objets que les mêmes fonctions manipulent, avec leur taille, leurs champs, ce que contiennent leurs quarante-huit premiers mots et leurs usages (`analysis/types.json`) |
| `… --nommer` | ce que la documentation sait déjà dire des types trouvés : un type manipulé par une fonction décrite hérite de sa description |
| `… --suivi` | ce que devient chaque champ d'un objet suivi image par image (`SFC_SUIVRE_OBJET`) |
| `… --document` | écrit [`docs/dictionnaire.md`](dictionnaire.md) : les soixante types les plus employés, avec leur forme et leurs fonctions |

Les relevés de plusieurs scènes se fondent : tout fichier `analysis/releves/*_par_objet.txt` est lu
avec le relevé courant. Une scène ne montre que ses objets — le vol ignore les menus, et l'inverse.
Sur dix relevés — un vol, une partie complète, les menus, le scénario, la stratégie, une longue
session menant deux missions jusqu'à leurs boss (Star Wolf, puis Solar), et trois explorations
automatiques (`tools/explorer.py`) : **43 720 objets vus, 422 types**, dont 26 héritent d'une
description.

Une remarque de méthode, mesurée : une exploration automatique rapporte **732 objets**, une session
jouée par quelqu'un qui sait où aller en rapporte **8 431**. L'explorateur sait ouvrir des portes,
pas finir une mission — il balaie les menus quand personne n'est là, rien de plus. Deux d'entre eux portent le jeu :

| objets | taille | fonctions | appels relevés |
|---|---|---|---|
| 2 232 | 128 o | **286** | 15 736 457 |
| 2 531 | 64 o | 171 | 2 981 090 |

Le second commence par quatre pointeurs et répète sa forme tous les seize mots — soit ses 64 octets.

Ce que `--types` a trouvé du premier coup : 1 719 familles, dont une de 151 bases dans l'overlay 11
aux décalages `+0x3C, +0x40, +0x44, +0x60, +0x62, +0x64` — trois mots puis trois demi-mots, la forme
d'une position suivie d'angles. C'est ainsi qu'on nomme un champ sans lire une ligne de code.

Ce que l'inventaire seul ne voit pas : sur *quoi* une fonction touche ces décalages. C'est ce que
`SFC_ETUDE_OBJETS=fichier` relève en jouant — pour chaque fonction, les adresses reçues en premier
argument, leur étendue et le nombre d'appels. Croisés (`--objets`), les deux donnent des objets
réels : sur la mission des anneaux, 29 zones de mémoire reconnues, dont un tas à `0x022F0000` où 121
objets espacés de douze octets au plus sont manipulés par 118 fonctions et portent 81 champs.

Le relevé garde aussi l'autre sens, par objet : les fonctions qui l'ont reçu. Deux objets que les
mêmes fonctions manipulent sont du même type ; `--dictionnaire` les groupe ainsi (d'abord sur
l'ensemble exact, puis en fondant les groupes qui partagent plus de la moitié de leurs fonctions),
et lit leur taille à l'écart le plus petit entre deux voisins.

Sur la mission des anneaux : **296 types, sur 3 411 objets vus**. Les plus employés :

| objets | taille | vers | fonctions | appels | champs |
|---|---|---|---|---|---|
| 289 | 64 o | `0x02290180` | 41 | 6 611 | 14 |
| 255 | 128 o | `0x022BE640` | 93 | 91 035 | 43 |
| 227 | 32 o | `0x022822A0` | 26 | 82 247 | 11 |
| 145 | 192 o | `0x022E8900` | 87 | 182 662 | 46 |

Le relevé regarde aussi, de loin en loin, ce que contiennent les quarante-huit premiers mots d'un
objet, et les classe : `p` un pointeur, `n` un petit entier, `x` une longueur du monde (20.12, avec
des décimales — position, vitesse, angle), `.` toujours nul, `?` de tout. Ainsi le type de 64 octets
vers `0x02290180` commence par deux pointeurs et porte trois longueurs du monde vers `+0x30`.

`--nommer` fait le premier pas : un type manipulé par une fonction que la documentation décrit hérite
de sa description. Six types en sortent nommés, dont celui de **192 octets, 145 objets** — touché par
`0x021B3E9C` et `0x021B594C`, c'est-à-dire par le système de **volumes de collision**. C'est donc une
entité physique du vol.

Le classement compte les sortes vues plutôt que de les additionner, et ne retient que celle qui
domine franchement — un champ de position vaut parfois zéro, parfois un petit nombre, et reste une
position. Les formes en sortent lisibles ; celle du type de 128 octets se répète au pas de sa
taille, ce qui la confirme :

```
type de 128 octets, 259 objets, 90 fonctions
pp.p ??.n ..n? x?nn ..n. .ppp .... ....   puis à nouveau  pp.p ??.n ..n? x?nn
```

### Nommer un champ en le regardant vivre

`SFC_SUIVRE_OBJET=0xADRESSE[,fichier]` relève les quarante-huit premiers mots d'un objet **à chaque
image**, et `tools/structures.py --suivi` dit ce que chacun devient : un champ qui ne bouge jamais est
une identité ou un pointeur ; un qui monte de un à chaque image est un compteur ; un qui varie d'un
peu à chaque image et de beaucoup sur la durée est une grandeur du monde. Trois d'affilée qui varient
du même ordre forment un vecteur.

**Trois pièges rencontrés du premier coup, et ce qu'ils ont appris** — ils valent plus que le premier
résultat, car chacun produisait une conclusion fausse et plausible :

- **Un objet a plusieurs vies.** `0x022E8900` dormait à zéro pendant 2 400 images, puis recevait une
  table de noms. Moyenné sur tout, son champ `+0x24` semblait « une grandeur du monde variant de 143
  par image » — la vitesse d'un vaisseau. C'était le passage de zéro à `0x72617453`, c'est-à-dire aux
  lettres `Star`, puis `fish` et `Shot`. On ne juge donc un champ que sur sa plus longue tranche
  d'images non nulles, et quatre octets lisibles sont désormais reconnus comme du texte.
- **Un tampon n'est pas une structure.** Choisir l'objet dont tous les champs portent des grandeurs
  donne un tampon de travail : les siens partaient tous de `0x11111111`, le motif dont le jeu remplit
  la mémoire neuve.
- **Un cadre de pile n'est pas un objet.** Les adresses en `0x027Exxxx` reçoivent des milliers
  d'appels et changent de forme à chaque passage : le relevé ne garde plus que `0x02000000`–`0x023FFFFF`.

Ce qu'il reste à faire : retrouver une vraie entité de vol — celle du type de 192 octets que les
fonctions de collision manipulent — et la suivre pendant un vol, en croisant sa position avec ce que
le moteur 3D dessine.
Ce que l'outil ne voit toujours pas : les objets atteints par un pointeur gardé ailleurs, jamais
passés en premier argument.

## 5. Les variables d'environnement

### Usage courant

| variable | effet |
|---|---|
| `SFC_ARM7=1` | fait tourner le vrai coprocesseur (nécessaire au son) |
| `SFC_DATA=dossier` | où trouver les données du jeu ; sinon `data/` à côté de l'exécutable |
| `SFC_TOUCHES=a=0x5A,b=0x45,…` | les touches du clavier, commande par commande, par leur code de touche virtuelle Windows : `a` `b` `x` `y` `l` `r` `start` `select` `haut` `bas` `gauche` `droite`, `rapide` (l'avance rapide) et `journal` (noter un défaut d'image) ; une commande absente garde sa touche d'origine, un code nul n'en donne aucune. Le lanceur la pose (page Contrôles), et le journal la recopie |
| `SFC_MOD_LASER=pilote:type,…` | le laser du vaisseau de chaque pilote nommé (`fox:plasma,slippy:double` ; types `simple`, `double`, `plasma`, `courbe`, `zapper` ; sans pilote : Fox), quel que soit celui que la partie lui donne (§ 5 ter, « Les mods du lanceur ») |
| `SFC_MOD_VERROUILLAGE=pilote:type,…` | son verrouillage (`simple`, `multiple`, `large`) |
| `SFC_MOD_JAUGE=pilote:taille,…` | la taille de sa jauge du turbo (`petite`, `moyenne`, `grande`) |
| `SFC_MOD_VAISSEAU=pilote:pilote,…` | l'apparence de son vaisseau : celle du pilote nommé ensuite (`slippy:falco`) ; les clés : `fox`, `krystal`, `falco`, `slippy`, `ash`, `wolf`, `leon`, `panther`, `octman`, `bill`, `lucy`, `amanda`, `cat`, `peppy`, `james`, `jodie` |
| `SFC_MOD_DEGATS=facteur\|un-coup`, `SFC_MOD_DEGATS_RECUS=facteur` | les dégâts que le joueur inflige, qu'il reçoit (facteur de 0,1 à 100) |
| `SFC_MOD_INVINCIBLE=1`, `SFC_MOD_TURBO=1`, `SFC_MOD_BOMBES=1`, `SFC_MOD_CHARGE=1` | invincible ; turbo et frein infinis ; bombes infinies ; tir chargé toujours au plus fort |
| `SFC_TRACE_FICHIER=1` | journalise chaque fichier et chaque overlay que le jeu demande au lecteur, avec son nom d'origine |
| `SFC_VERIF=0x…` | compare une réécriture native à la traduction, appel par appel (`tout` pour toutes) |
| `SFC_VERIF_MAX=n` | nombre d'appels comparés avant de laisser la traduction tranquille (2000 par défaut) |
| `SFC_NATIF=0x…` | fait remplacer la traduction par la réécriture, pour de bon |
| `SFC_SAVE=chemin` | déplace la sauvegarde — **à ne pas utiliser pour jouer** |
| `SFC_SCALE=1..4` | suréchantillonnage (anti-crénelage) |
| `SFC_FILTER=0..2` | filtrage des textures |
| `SFC_AFFICHAGE=libre` | l'affichage a son fil et interpole entre deux images du jeu, qui garde son rythme (`docs/plan_framerate.md`) |
| `SFC_IPS=n`, `vsync`, `0` | plafond d'images affichées par seconde en affichage libre : un nombre (240 par défaut), celui de l'écran, ou aucun |
| `SFC_APERCUS=0` | retire les tours d'aperçu : pendant que le jeu attend l'heure de son prochain tour (les vingt millisecondes du vol), il le calcule d'avance avec les commandes du moment, l'affichage glisse vers lui, puis tout est rendu — mémoire, registres, moteur 3D, liaison — et le vrai tour se joue comme sur la console |
| `SFC_LATENCE=0..1`, `auto` | en affichage libre, la part d'un tour de jeu gardée en retard : 0 montre la dernière image du jeu dès qu'elle arrive et prolonge son mouvement, 1 attend la suivante pour se placer entre deux (plus fluide, un tour de retard sur les commandes). Par défaut, `auto` : 1 dans les scènes à soixante tours par seconde (menus, stratégie), 0 en vol (trente) |
| `SFC_AFFICHAGE_BRUT=1` | affichage libre sans interpolation : l'image du jeu telle quelle |
| `SFC_PORTEE=facteur` | la portée d'affichage, de 1 à 4 : le jeu dessine jusqu'à ce facteur fois plus loin, et son anneau d'horizon recule d'autant (§ 3, « La distance d'affichage ») — 2 est la valeur du lanceur ; au-delà, un élément d'arrière-plan déborde. Les tirs portent aussi plus loin : lasers d'Arwing et tirs ennemis vivent ce facteur fois plus longtemps, et les ennemis dont le tir dépend de la distance tirent de ce facteur fois plus loin, sans dépasser la vue (§ 3, « La portée des chasseurs »). **Ce n'est plus le jeu d'origine** : les règles et le rythme restent, le minutage fin change, et une partie enregistrée ne se rejoue qu'à sa portée |
| `SFC_NO_AUDIO`, `SFC_NO_VIDEO` | coupent le son, l'affichage |
| `SFC_MAX_FRAMES=n` | arrête après n images (désactive le cadencement) |
| `SFC_INPUT_REPLAY=f`, `SFC_INPUT_RECORD=f` | rejeu et enregistrement des entrées — une ligne par image, « image touches stylet x y », et la manette en cinq colonnes de plus quand il y en a une |
| `SFC_MANETTE=0` | ignore les manettes |
| `SFC_MANETTE_INVERSE=1` | le stick en hauteur dans le sens de Star Fox 64 : pousser fait piquer, tirer fait monter (par défaut, pousser fait monter, comme le stylet) ; le lanceur le propose, page Contrôles |

### Mesure

| variable | effet |
|---|---|
| `SFC_PROFILE=1` | temps par phase, pics, compteurs de textures ; et toutes les six cents images, la part d'un cœur prise par chaque fil du portage (« fils : jeu, affichage, 2D 1 à 4 ») et par ceux du système et du pilote (« autres ») |
| `SFC_PROFILE_ARM9`, `SFC_PROFILE_ARM7` | échantillonnage par adresse de retour |
| `SFC_WAV=f`, `SFC_WAV_FROM=n` | enregistre le son — la preuve de référence |
| `SFC_STAT_CLIP` | distance du décor, découpage au plan lointain |
| `SFC_TRACE_PROJ`, `SFC_TRACE_TRANS`, `SFC_TRACE_TILES` | projection, cellules de terrain |

### Diagnostic

| variable | effet |
|---|---|
| `SFC_TRACE_CALL=0x…` | journalise les appels **indirects** d'une fonction et ce qu'elle rend |
| `SFC_TRACE_CALL_MAX=n` | plafond du journal |
| `SFC_DUMP_MEM=0xadr:n` | vide une zone régulièrement |
| `SFC_POKE=0xadr:0xval` | force un mot à chaque image, pour éprouver une hypothèse |
| `SFC_WATCH_VALUE=0x…` | journalise l'écriture d'une valeur, avec la pile d'appels |
| `SFC_FIND_VALUE=0x…` | balaie la RAM à la recherche d'une valeur |
| `SFC_DUMP=n`, `SFC_DUMP_FROM=n` | captures PNG |
| `SFC_RAM_IMAGES=image,image…` | la mémoire principale entière à la fin de ces images, dans `SFC_RAM_PREFIXE` + image + `.bin` (`build/essai/ram_` par défaut) : deux relevés que seule une commande sépare disent où le jeu la range |
| `SFC_2D_IMAGES=image,image…` | ce que composent les deux moteurs 2D à la fin de ces images, même sans fenêtre, dans `SFC_RAM_PREFIXE` + `2d_A_` / `2d_B_` + image + `.ppm` (sans la 3D) : sépare un défaut de la composition 2D d'un défaut de l'affichage |
| `SFC_MANETTE_ESSAI=f` | une manette scriptée, avec ou sans fenêtre : une ligne par changement, « image gx gy dx dy boutons » (sticks de −1 à 1, y vers le haut ; boutons `MANETTE_…` de `manette.h`, en hexadécimal). Sans fenêtre, elle s'ajoute au rejeu et joue seule quand il s'arrête ; en fenêtre, le rejeu la remplace tant qu'il dure |
| `SFC_TRACE_INPUT=1` | chaque changement des touches et du stylet ; avec la manette, chaque entrée en vol et chaque sortie (la hauteur de la pile des scènes, le nom de la fenêtre ouverte) |
| `SFC_AFFICHAGE_ESSAI=n` | l'épreuve de l'interpolation : à partir de l'image `SFC_DUMP_FROM`, n sous-images entre neuf images 3D consécutives, et ce que l'image change d'un pas au suivant (`SFC_AFFICHAGE_ESSAI_PNG=1` les enregistre) |
| `SFC_RAPIDE_JUSQUA=image` | un rejeu file sans cadence jusqu'à cette image, puis reprend le temps réel |
| `SFC_CADENCE=1` | garde la cadence de la console malgré `SFC_MAX_FRAMES` : un rejeu se déroule comme la partie qu'il rejoue (sinon le son n'est plus en temps réel et les appuis ne tombent plus au même endroit — un tracé de route au stylet rate d'une image et la mission ne part pas), et l'arrêt automatique reste utile |
| `SFC_DIAG_DEFORME=s` | à partir de l'image 3D numéro `s` (1 pour tout), les appariements qui déforment : le rapport entre l'arête la plus étirée et la plus rétrécie d'un polygone entre les deux images du jeu, sa place à l'écran, l'étage qui l'a apparié. Un mauvais pareil change chaque arête de son côté ; un rapprochement ou une rotation les change ensemble |
| `SFC_RAIDE=0` | retire le garde-fou : les paires qui déforment voient de nouveau leur forme interpolée, comme avant. Pour comparer |
| `SFC_REALIGNER=0` | retire le second tour de l'appariement par objets entiers (un objet reconnu rang par rang quand certains de ses polygones changent de forme sur place), pour comparer |
| `SFC_DIAG_COULEURS=1` | chaque polygone affiché prend la couleur du chemin que l'interpolation lui fait suivre : rouge sans pareil, bleu disparu, jaune raide, cyan retourné, magenta apparié par le voisinage, vert « grossit vite », orange apparié par sa seule forme, blanc apparié par son rang mais changé de forme ; ce qui garde ses couleurs a été reconnu avec son objet entier |
| `SFC_DIAG_BLOCS=s[:n]` | des images 3D s à s+n, les objets de huit polygones ou plus reconnus ou non d'un bloc, et pour ceux qui ne l'ont pas été, combien de leurs polygones ont changé de forme à rang égal ; et, partout, les liens où l'étage des objets entiers n'a presque rien reconnu |
| `SFC_DIAG_RANGS=s` | à l'image 3D s, le premier objet de plus de cent polygones qui n'a pas été reconnu d'un bloc, rang par rang : forme gardée ou changée, apparié à quoi et par quel étage, combien de polygones de même forme dans l'image d'avant et le plus proche |
| `SFC_DIAG_FILES=de[:a]` | chaque image que reçoit l'affichage libre entre ces images du jeu : son tour, l'écran où elle va, si c'est un aperçu ou une publication d'avance, et les dernières clés des deux files d'écran — pour voir une image rangée sur le mauvais écran |
| `SFC_2D_MONOTONE=0` | l'image dont l'affichage libre montre la 2D peut de nouveau reculer (comportement d'avant, pour comparer) : quand le jeu cale et que sa 3D passe pour immobile, l'écran sans 3D revenait d'une image en arrière à la reprise |
| `SFC_2D_VRAIES=0` | l'écran sans 3D peut de nouveau montrer la 2D d'un tour d'aperçu (comportement d'avant, pour comparer) ; par défaut il ne montre que de vraies images du jeu |
| `SFC_DIAG_VIVANTE=de[:a]` | chaque fois qu'un écran passe de « 3D vivante » (interpolée) à « immobile » et retour : clés, écart, âge de la dernière image, seuil, horloge calée ou non |
| `SFC_DIAG_BAS=de[:a]` | l'empreinte des couches 2D de l'écran du bas à chaque changement, sa source (vraie image ou aperçu), et le compte des allers-retours — un retour à l'empreinte d'avant-avant est un papillotement |
| `SFC_AFFICHAGE_FILM=n` | le film : les n images affichées à partir de l'image `SFC_DUMP_FROM`, toutes, à leur rythme (`build/film_nnnn.png`), et pour chacune l'heure, le tour et les images du jeu qu'elle montre (`build/film.txt`) |
| `SFC_APERCUS_ESSAI=k`, `SFC_APERCUS_ESSAI_N=n`, `SFC_APERCUS_ESSAI_FIXE=1` | sans cadence (un rejeu vérifié), n tours d'aperçu à chaque attente du jeu : l'épreuve qu'un aperçu s'efface tout entier — les empreintes ne doivent pas bouger |
| `SFC_SUIVRE_OBJET=0xADRESSE[,fichier]` | les quarante-huit premiers mots de cet objet à chaque image (`tools/structures.py --suivi` en dit ce que chaque champ devient) |
| `SFC_ETUDE_OBJETS=fichier` | en jouant, pour chaque fonction appelée : les adresses qu'elle reçoit en premier argument, leur étendue et le nombre d'appels. Croisé avec l'inventaire des accès (`tools/structures.py --objets`), cela donne les objets du jeu et leurs champs |
| `SFC_ETUDE_ACC=fichier` | observe chaque écriture accumulatrice (x = x + v) du jeu : combien de fois, dans combien de tours, avec quel pas, et si la même adresse est réécrite dans un tour (`tools/accumulateurs.py` en tire la liste des sites mis à l'échelle) |
| `SFC_APERCU_SOUPCONS=fichier` | quels sites mis à l'échelle étaient présents dans les aperçus abandonnés, et dans ceux qui ont abouti |
| `SFC_APERCUS_ESSAI_BUDGET=ms` | un garde-temps serré pour les aperçus de l'épreuve : beaucoup sont abandonnés en plein travail — l'épreuve que l'abandon rend l'état aussi bien que l'aboutissement |
| `SFC_APERCU_2D=0` | un tour d'aperçu reprend les couches 2D du dernier balayage au lieu de composer les siennes : ce qu'il dessine en 2D est une prédiction, et un élément qui clignote d'un tour à l'autre papillote quand elle se trompe |
| `SFC_DIAG_2D_APERCU=image` | à partir de cette image, la 2D d'un aperçu est comparée à celle que le vrai tour dessinera : la part de prédictions justes, et pour la première fausse, les points qui diffèrent par écran et par couche, plus les images (`build/2d_*.png`) ; dit aussi chaque fois qu'un aperçu annonce les deux écrans dans l'autre ordre |
| `SFC_APERCU_VBLANK=1` | essai : donner à l'aperçu l'interruption du balayage que sa séquence saute. En l'état elle arrête les aperçus tout net — à reprendre |
| `SFC_APERCU_ECARTER=0` | montre les images d'aperçu même quand l'affichage se remanie pendant leur tour (ordre des écrans qui cesse d'alterner, mode d'un moteur qui change) — c'est ce remaniement qui donnait deux fois le même écran, ou un décalage, le temps d'un éclair |
| `SFC_DIAG_ECRANS=1` | quand les deux écrans reçoivent la même géométrie 3D — ce qui n'arrive pas sur la console, où le moteur 3D n'en alimente qu'un à la fois : d'où chacun tirait son image, et une photo de l'écran (`build/jumeles_NN.png`) |
| `SFC_DIAG_OSCILLATION=1`, `SFC_DIAG_OSCILLATION_SUITE=image` | l'image qu'on s'apprête à montrer est-elle celle d'avant-avant, alors que celle d'avant était autre ? C'est un aller-retour, ce que l'œil appelle un papillotement ; la suite donne, image par image, l'empreinte des couches 2D et l'ordre des écrans. La passe de capture, dessinée sans être montrée, n'y compte pas |
| `SFC_DIAG_COMPTEURS=1` | ce qu'un tour d'aperçu ne prédit pas : la mémoire qu'il laisse est comparée à celle du vrai tour au même point — les mots qui y valent exactement un de plus (des compteurs qu'il n'a pas fait avancer, le balayage étant sauté), et le compte des écarts par zone |
| `SFC_DIAG_ACOUPS=ms` | les tours du jeu plus longs que ça (40 ms par défaut), avec ce que les tours d'aperçu y ont pris et ce qu'ils ont mordu sur le balayage |
| `SFC_APERCU_VERIF=1` | après chaque tour d'aperçu, la mémoire entière et les registres sont comparés à ce qu'ils étaient avant lui ; au premier octet qui a bougé, l'adresse, la zone et la page sont dites, et la partie s'arrête. Lent (cinq mégaoctets recopiés par aperçu), mais c'est l'épreuve qui dit qu'un aperçu ne laisse rien derrière lui |
| `SFC_DIAG_APERCUS=1`, `SFC_DIAG_APERCU_SIG=image` | le planificateur des aperçus ; la « position » de chaque image 3D, vraie ou d'aperçu |
| `SFC_ECHANTILLONS=fichier`, `SFC_ECHANTILLONS_DEPUIS=image` | où passe le temps de la machine hôte, par échantillonnage (`tools/echantillons.py`). Le temps d'horloge, attentes comprises : le sommeil apparaît sous `attente_dormir_ns`. Sous PowerShell, rediriger le journal par `Start-Process -RedirectStandardError` fait croire à un `vfprintf` coûteux : passer par `SFC_JOURNAL`, comme le lanceur |
| `SFC_ECHANTILLONS_FIL=affichage` | échantillonne le fil d'affichage au lieu de celui du jeu |
| `SFC_BANDES_VERIF=1` | chaque écran 2D, composé par bandes sur l'atelier, est recomposé d'un bloc et comparé octet par octet ; le journal compte les écrans différents (aucun sur la partie du joueur) |
| `SFC_NO_PARALLEL=1` | les deux écrans 2D se composent d'un bloc sur le fil du jeu, sans l'atelier |
| `SFC_INTERPOLATION=cpu` | l'interpolation entre deux images du jeu revient au processeur (`interpoler`), pour comparer au nuanceur de géométrie ; `SFC_DIAG_COULEURS` y revient aussi. `SFC_PROFILE` dit, toutes les cinq secondes, combien de lots et de polygones compte un écran et combien de plans se construisent |
| `SFC_INTERPOLATION_EPREUVE=echec` | simule un écart à l'épreuve de départ de l'interpolation sur le GPU : pour vérifier qu'elle revient bien au processeur, et que le journal le dit |
| `SFC_CHRONO_ARM7=1` | le temps hôte passé dans le coprocesseur |
| `SFC_AFFICHAGE_DIAG=3`, `4`, `5` | la chronologie des instantanés que le jeu dépose ; où passe le temps d'un tour du fil d'affichage de plus de 6 ms ; chaque attente de l'image suivante et chaque recalage de l'heure |
| `SFC_DUMP_POLYS=n`, `SFC_DUMP_TEX` | polygones, textures |
| `SFC_OBSERVER=0x…[,0x…][:max]` | chaque appel de ces fonctions (huit au plus), direct ou par le dispatch : l'image, qui appelle, r0 à r3, le haut de la pile, et ce qu'elle rend ; `max` borne le journal (2 000). C'est le premier levier pour trouver « où se décide X » : intercepter la fonction soupçonnée et regarder ce qu'elle reçoit. Le jeu en natif appelle parfois un relais plutôt que la fonction (`0x01FFBF84` pour `0x01FFBF8C`) : observer l'adresse que l'appelant nomme |
| `SFC_OBSERVER_DEPUIS=image` | ne commence qu'à cette image |
| `SFC_OBSERVER_CHAINE=1` | ajoute les adresses de retour des appels en cours, de la plus proche à la plus lointaine |
| `SFC_OBSERVER_LIRE=r2+4:3,0x027E14F8:6` | ajoute ce que pointent les arguments — ici trois mots à `r2+4` et six à une adresse fixe : une structure passée par adresse ne dit rien par son adresse seule. Des crochets suivent un pointeur, et se combinent : `[[r0+0xC]]+0x20:3` lit la position de l'acteur que désigne la référence rangée en `r0+0xC` |
| `SFC_DIAG_LOIN=image[:distance]`, `SFC_DIAG_LOIN_N=n` | à cette image, les polygones affichés plus lointains que la distance (7 800 par défaut) : combien, où ils tombent à l'écran, leur texture et leurs attributs (les n premiers) |
| `SFC_DIAG_OBJETS=image[,image…]` | la position de chaque objet de la liste que parcourt le rendu de scène, dessiné ou non : deux relevés disent quels objets bougent, et à quelle distance |
| `SFC_DIAG_CAMERA=n` | la position de la caméra toutes les n images (l'objet `Camera` courant : `[0x020CF430+0x10C]`, position en `+0x18`) — un témoin de la partie qui ne dépend ni des adresses ni de ce qui est dessiné, pour dater l'image où deux parties divergent |
| `SFC_TRACE_BACKUP`, `SFC_TRACE_IPC`, `SFC_TRACE_SOUND`, `SFC_TRACE_GX` | traces par sous-système |

**Limite à connaître** : `SFC_TRACE_CALL` ne voit que les appels **indirects**.
Le code traduit appelle directement quand la cible est connue, et ces appels
n'apparaissent pas. Pour observer une fonction appelée directement, il faut la
remplacer par un hook natif (`rt_hook`) — qui la remplace au lieu de
l'accompagner, donc il faut la reproduire.

### Réglages d'ordonnancement — à ne pas toucher à la légère

| variable | défaut | remarque |
|---|---|---|
| `SFC_EDGE_BUDGET` | 256 | au-delà, le coprocesseur devient muet |
| `SFC_ARM7_MIN` | 8 | 16 fait diverger le son, 32 le change entièrement |
| `SFC_ARM7_CPE` | 8 | cycles par arête du coprocesseur |

---

## 5 bis. Décompiler en le prouvant

Réécrire une fonction du jeu en C ne vaut que si l'on peut montrer qu'elle fait
exactement ce que fait la traduction du binaire. `runtime/natif.c` tient ce rôle :
sur chaque appel de la fonction visée, il retient l'état de la machine, exécute la
traduction, remet l'état d'avant, exécute la réécriture, puis compare registres,
RAM, DTCM, ITCM et registres matériels.

Le jeu garde toujours le résultat de la **traduction** : une réécriture fausse se
signale sans rien casser, et la vérification se fait donc en jouant.

Deux précautions apprises à l'usage :

* **Ce qui est sous le pointeur de pile ne compte pas.** La traduction y empile son
  adresse de retour, la réécriture n'a pas à le faire, et ces octets sont morts dès
  le retour. Les comparer ferait échouer toute réécriture correcte.
* **Le crochet doit pouvoir s'ôter le temps d'un appel**, sinon la traduction qu'il
  remplace se rappellerait elle-même. Comme le code généré appelle un crochet sans
  lui dire quelle fonction il remplace, chaque réécriture a le sien.

Le banc lui-même se vérifie : on y introduit une faute délibérée et on regarde s'il
la voit. Un banc qui ne dit jamais rien ne prouve rien.

### Ce qu'une réécriture doit reproduire

Une fonction ne rend pas qu'une valeur. Tout ce qui suit a été découvert en
regardant le banc refuser des réécritures qui semblaient justes :

| ce qu'il faut rendre | pourquoi |
|---|---|
| les registres, y compris `lr` | un `blx` écrit l'adresse de retour ; l'appelant peut la lire |
| les drapeaux **N Z C V** | l'appelant branche dessus ; `adds r0, r4, #0` en pose, même quand il ne calcule rien |
| le bit **T** à la sortie | `pop {pc}` rend à l'appelant le mode de son adresse de retour |
| le **cadre de pile entier** | voir ci-dessous : c'est ce qui a coûté le plus à comprendre |

### Le cadre de pile n'est pas une formalité

Une réécriture native n'a besoin ni de sauvegarder des registres ni de creuser un
cadre : elle a ses variables. Elle doit pourtant faire les deux, et refaire le
`pop` à la lettre.

* **Le `push` écrit vraiment.** Ces octets restent en mémoire après le retour, et
  le jeu recopie parfois un tampon sans l'avoir rempli en entier : des déchets
  différents finissent dans des données vivantes.
* **Le creux (`sub sp, #n`) décale tout ce qui suit.** Tant qu'il est ouvert, les
  fonctions appelées posent leur pile plus bas. Ne pas le creuser décale la pile
  de toute la suite de l'exécution.
* **Le `pop` relit.** Il ne rend pas les registres qu'on avait en entrant : il lit
  ce qui se trouve *maintenant* à leur place. Une interruption survenue pendant la
  fonction a pu écrire là — et c'est alors la valeur relue que le jeu utilise. De
  même pour l'adresse de retour : on repart par où la pile dit de repartir, via
  `rt_jump`, qui reconnaît un vrai retour et sinon suit le saut.

Ce dernier point s'est manifesté de la façon la plus déroutante qui soit : la
fonction était juste — quatre mille appels comparés sans un écart, pile morte
comprise — et le jeu divergeait quand même, onze images plus loin, sur un mot de
pile écrit en mode interruption. Les trois réécritures ne sont devenues
indiscernables qu'une fois le `pop` imité en entier : **6 000 images, empreintes
identiques, mémoire entière comprise**.

### Le troisième piège : ce que le banc ne restaure pas

Le singleton `0x021A9908` de l'overlay des acteurs — garde, construction au premier
passage, enregistrement du destructeur — passait 848 appels sur 849 et divergeait
au premier, d'un seul mot : le constructeur l'incrémentait deux fois chez nous, une
seule dans la traduction, avec les mêmes arguments et sans appel en trop.

La cause n'était pas dans la réécriture. Le constructeur envoie un message au
coprocesseur par la FIFO de communication (`0x04000184`, `IPCFIFOCNT`), et c'est la
réponse, traitée par le moteur, qui écrit ce mot. Le banc remet en place la mémoire
du jeu — **pas la file de communication, ni l'état du coprocesseur**, qui vivent
dans le moteur. La seconde exécution y trouvait encore la réponse de la première.

Règle : une fonction qui, de près ou de loin, parle au coprocesseur ne se juge pas
au banc. Elle se juge aux empreintes, qui n'exécutent qu'une fois. Celui-ci y passe :
les sept réécritures actives, **3 000 images identiques**.

### Un banc de mission

L'enquête sur les anneaux avait laissé une sauvegarde et un enregistrement faits en
vol ; `build/mission_anneaux.sav` et `build/mission_anneaux.txt` en sont la copie
stable. Rejoué, l'enregistrement reproduit la séquence d'overlays à l'image près —
stratégie et briefing à l'image 999, pilotage de l'Arwing à l'image 2 442 — et deux
exécutions donnent les mêmes empreintes. Les sept réécritures y restent
indiscernables sur 4 400 images.

C'est ce qui ouvre le code de combat : en vol, les cibles les plus appelées sont
celles de l'overlay des acteurs — `0x021B3FD0` quatre cent vingt-deux mille fois,
`0x021B594C` deux cent quarante-sept mille, `0x0216DADC` deux cent onze mille.

### Les outils que cette enquête a laissés

| variable | ce qu'elle donne |
|---|---|
| `SFC_SNAPSHOT=image:préfixe` | écrit la mémoire à cette image ; deux instantanés se comparent octet par octet |
| `SFC_VERIF_PILE=1` | le banc ne passe plus rien, même les octets morts sous le pointeur de pile |
| `SFC_TRACE_NATIF=n` | journalise l'entrée et la sortie des n premiers appels, dans les deux modes, pour comparer ligne à ligne |
| `SFC_WATCH=0x…` | journalise chaque écriture qui change ce mot, avec le mode et la pile du moment |
| `SFC_WATCH_DEPUIS=image`, `SFC_WATCH_MAX=n` | n'arme la surveillance qu'à cette image, et la retire après n écritures : sans eux la page reste protégée dès le démarrage, et une page de variables écrites sans cesse rend le jeu très lent avant même d'arriver au moment voulu |
| `SFC_WATCH_LECTURES=1` | surveille aussi les **lectures** du mot : chaque endroit qui le lit est dit une fois, avec la pile, puis toutes les mille lectures — pour trouver qui se sert d'un réglage. Sous la traduction (`SFC_NATIF=non`) : son code lit la mémoire en ligne et la fonction qui lit se déduit de l'adresse hôte, quand le code natif passe par des aides du runtime |
| `SFC_STAT_CALL=n` | les n fonctions les plus souvent atteintes par un appel indirect — ce que le jeu appelle vraiment |
| `SFC_PROFILE_FROM=image` | ne profile qu'à partir de cette image — un vol se lit mal sous deux mille images de menus |
| `SFC_PAS_MS=n` | remplace les 20 ms du limiteur de la boucle principale ; **ce n'est plus le jeu d'origine**, c'est une expérience |

Aucune de ces quatre n'aurait suffi seule : l'instantané dit *où*, l'observation
dit *qui*, la trace dit *quand*, et le banc dit *quoi*.

Et deux règles pour les appels que fait la réécriture :

* **appeler directement, sans passer par le dispatch** : celui-ci impose le bit T,
  alors que la traduction d'un `blx` immédiat le laisse tel quel — et le code appelé
  lit ce bit lorsqu'il consulte le registre d'état ;
* **poser l'adresse de retour** avant un appel qui, dans l'original, vient d'un `blx`.

Le banc, lui, doit remettre **tout** ce que la traduction a pu changer avant de
lancer la réécriture — le mode et le bit T compris. Sans cela, la réécriture part
d'un état que la référence a déjà modifié, et l'on passe un long moment à corriger
une fonction juste.

Réécritures établies :

| adresse | ce qu'elle fait | vérification |
|---|---|---|
| `0x0208940C` | rend une référence à un objet, et le confie à l'allocateur quand plus personne ne la tient | 30 000 appels, 0 divergence |
| `0x01FFEE38` | prend la tête de la liste libre de la classe de taille en cours | 15 000 appels, 0 divergence |
| `0x01FFEDE4` | rend un bloc à sa liste, ou le passe à l'étage du dessus | 13 000 appels, 0 divergence |
| `0x02088DF8` | cherche la classe dont la zone contient le bloc ; au-delà, rend au tas | jamais appelée dans les menus — non prouvée |
| `0x0202260C` | réordonne ses arguments et appelle la copie mémoire | 20 000 appels, 0 divergence |
| `0x021C80F4` | rend 1 si l'un de deux drapeaux d'octet est levé (overlay des acteurs) | 1 290 appels, 0 divergence |
| `0x021A9908` | singleton : construit son instance au premier appel, la rend ensuite (overlay des acteurs) | aux empreintes — il parle au coprocesseur |
| `0x021B3FD0` | premier élément de la liste à `+0x28` ; le nœud est inclus 0x1C octets après le début de l'élément | 30 000 appels en vol, 0 divergence |
| `0x021B594C` | l'octet à `+0xB8` est-il nul ? | 22 106 appels en vol, 0 divergence |
| `0x0216DADC` | l'octet `+0x4C` de l'objet désigné par le premier mot du champ `+0x24` | 18 630 appels en vol, 0 divergence |
| `0x020A0E4C` | **la boucle principale** : un tour de jeu, et l'attente de 20 ms en vol | aux empreintes — elle lit l'horloge : 4 400 images de mission identiques |

Les dix ensemble, en remplacement de la traduction : **4 400 images de mission aux
empreintes identiques**, mémoire entière comprise — et le même temps d'exécution,
à 0,3 % près. Les trois dernières sont les fonctions les plus appelées en vol ; les
réécrire fidèlement ne fait rien gagner, parce qu'elles font exactement le même
travail que leur traduction.

L'overlay des acteurs est chargé en permanence — il l'était depuis le début. Ce
qui m'avait trompé : la trace des overlays ne suit que trois emplacements sur
quatre, et celui des overlays 0 et 1 n'y figure pas. Une fonction de cet overlay
peut donc être décompilée sans attendre une mission ; encore faut-il en choisir
une qui soit appelée, ce que `SFC_STAT_CALL` dit et qu'un décompte de références
ne dit pas.

### Le générateur : le jeu entier réécrit par un programme

Près d'un tiers des fonctions du jeu sont des **feuilles** : quelques instructions
Thumb, ni appel ni boucle. Accesseurs (« rend `[arg0 + 0x160]` »), mutateurs,
prédicats (« si `[arg0 + 4] == 0` : rend 0 ; sinon : rend 1 »), bornages, relais
d'héritage multiple du C++ (`this` ramené à la sous-classe, puis saut à la vraie
méthode), appels terminaux par pointeur. À la main, elles ne se feraient jamais ;
elles ont toutes la même forme, et c'est un programme qui les réécrit — puis, la
même mécanique tenant, les fonctions qui appellent, et celles qui bouclent.

`tools/decomp_feuilles.py` **interprète symboliquement** chaque fonction. Il
suit ce que contient chaque registre en fonction des valeurs d'entrée, ce qui est
lu et écrit en mémoire, quels drapeaux restent posés ; à chaque branchement
conditionnel il se dédouble, et la fonction devient un arbre de `if` dont chaque
extrémité est un *chemin*. Une condition déjà tranchée sur le chemin n'est pas
reposée, une condition constante est tranchée d'avance, les sommes de constantes
se plient (« `sp - 4` », pas « `((sp - 0x10) - 0x20) + 0x2C` »). De l'état final
de chaque chemin sortent du C — lectures et écritures dans l'ordre d'origine,
registres et drapeaux posés à la fin — et une description en clair, qui ira
nourrir l'annuaire : ces milliers d'accesseurs sont autant de témoignages sur la
forme des objets.

Un **appel** est un point où l'état symbolique est écrit dans le processeur —
registres modifiés, drapeaux, adresse de retour — puis l'appelée tourne, et tout
ce qu'on savait est à relire : les registres et drapeaux d'après l'appel forment
une nouvelle génération de valeurs (« résultat de l'appel 1 », « r4 après
l'appel 1 »). L'appel emprunte le chemin de la traduction, direct par symbole ou
par le dispatch, selon la règle même de `recomp.py` — le dispatch pose le bit T,
et le code appelé le voit. Après l'appel, `c->redirect` est consulté comme dans
la traduction : un changement de thread peut reprendre la fonction ailleurs, et
c'est la traduction, qui a toutes ses étiquettes, qui continue alors.

Le **mode ARM** suit la même mécanique, avec ce qui lui est propre. Une instruction
sous condition qui ne termine rien devient un *choix de valeurs* — « si la
condition, la nouvelle valeur, sinon l'ancienne », pour chaque registre et chaque
drapeau qu'elle touche, ses accès mémoire sous un `if` — et non un chemin de plus :
`cmp ; movge ; movlt` reste une seule ligne droite. Celles qui terminent ou
appellent (`bxeq lr`, `ldmneia sp!, {pc}`, `blne`) dédoublent le chemin. S'y
ajoutent l'opérande décalé complet avec sa retenue, les adressages pré et
post-indexés, `ldm`/`stm` dans leurs quatre modes, les multiplications longues, les
instructions saturées du DSP (par les fonctions mêmes du runtime, qui posent Q),
`pc` lu comme constante, `mov lr, pc ; bx rX` reconnu comme appel. C'est ce qui
ouvre le module de collision, écrit en ARM pour la vitesse.

Trois règles de `recomp.py` sont **reproduites à l'identique**, parce que la
réécriture doit emprunter les mêmes chemins que la traduction :

* **la liaison des appels** — direct par symbole quand la cible est une fonction,
  ou un début de bloc, de l'ARM9 ou de la même région et dans le même mode ;
  sinon par le dispatch, qui pose le bit T ;
* **le `bl` employé comme branchement long** (CodeWarrior, en Thumb) — cible dans
  la fonction, hors entrée, reconnue par la cartographie ou à plus de 2 Ko avec
  l'instruction précédente qui y enchaîne ; tout autre `bl` interne est un appel,
  la récursion comprise ;
* **le découpage en plages** — on ne suit que les instructions que la
  cartographie attribue à la fonction ; un flot qui en sort « chute dans la
  fonction voisine », que la traduction appelle par son symbole, et la
  réécriture fait de même (plus bas). Les tables de sauts, que la traduction
  résout en `goto`, deviennent un `switch`.

Une **boucle** a une tête — la cible d'une arête arrière, repérée d'avance — où
l'état est écrit et relu à neuf, sous une étiquette C ; l'arête arrière écrit
l'état, avance le temps simulé exactement comme la traduction
(`if (--g_edge_budget < 0) rt_edge(c);`, avec ses interruptions) et saute à la
tête. Le corps est symbolique comme le reste ; seule la jonction est matérielle.

Les **grandes fonctions** ont demandé une idée de plus. En arbre pur, la suite d'un
`if` est recopiée dans chaque branche : c'est lisible, mais le nombre de chemins
double à chaque test, et une fonction de deux kilo-octets n'en finit pas. Quand
l'arbre dépasse trente-deux chemins, le générateur recommence avec des
**jonctions** : tout bloc que plusieurs chemins rejoignent devient, comme une tête
de boucle, un point où l'état est écrit et relu sous une étiquette — et n'est plus
émis qu'une fois. Rejoindre une jonction *en avançant* n'est pas une arête : seul
le saut arrière en est une. Une expression qui se nourrit d'elle-même — une somme
de contrôle suffit — double à chaque instruction ; passé cent soixante caractères,
elle devient un temporaire.

Les **tables de sauts** sont rendues comme la traduction les rend : un `switch`
dont chaque cas saute à une étiquette de la fonction, et dont le défaut passe par
`rt_jump`. Trois formes : `add rX, pc ; … ; bx rX` et `add pc, rX` en Thumb,
`addls pc, pc, rX, lsl #2` en ARM. Les cas sont émis l'un après l'autre, chacun
fermé par un retour — sans quoi celui qui rend la main tombe dans le suivant, et
le banc a vu une fonction rendre 1 au lieu de 0.

Le flot qui **quitte la fonction sans appel** — un `b` vers une autre, une chute
dans la voisine — suit la traduction : par le symbole de l'autre fonction, sans
empiler de retour. Les **instructions système** (`mrs`, `msr`, `mcr`, `mrc`,
`swi`, `bkpt`) écrivent l'état, laissent la ligne même du traducteur faire son
office, et tout se relit : le mode a pu changer, et avec lui la pile. Le code de
l'**ITCM** est lu là où le crt0 le prend, à la fin de l'image de l'ARM9.

« `mov lr, pc` », « `ldr lr, [pc, #n]` » ou « `add lr, pc, #n` » juste avant un
« `bx rX` » : pour le traducteur c'est un **appel par registre dont le retour est
choisi** — il appelle, puis reprend à cette adresse. C'est ainsi que le
gestionnaire d'interruptions du SDK appelle la routine d'une interruption puis
enchaîne sur le changement de thread. La règle est reprise mot pour mot ; sans
elle, c'était la seule fonction sur vingt-neuf mille à changer les empreintes. De
même le saut calculé de l'ARM, « `add pc, pc, rX` », dont toute instruction de la
fonction est une cible — les divisions déroulées du SDK y sautent n'importe où.

`stmia rX!, {…, rX, …}` — la base dans sa propre liste — suit la règle du
traducteur : première de la liste, c'est sa valeur d'origine qui est rangée ;
sinon, celle d'après l'écriture arrière.

Un appel direct lie **la fonction même** que la traduction lie par son symbole —
telle région, telle entrée —, retrouvée une fois par site d'appel et gardée
(`APPEL_DIRECT`). Chercher par adresse à l'exécution n'en rendait pas forcément la
même, deux fonctions se chevauchant parfois ; et cela épargne, à chaque appel, la
recherche et le test de l'overlay présent.

Ce C n'est pas la traduction recopiée : c'est son résultat, recalculé par une
autre voie. C'est ce qui donne un sens à la comparaison. Le générateur produit
seize fichiers `generated/natif_auto_N.c` compilés en parallèle avec le code
traduit — dans un seul fichier, le compilateur y passait une heure — et un
`natif_auto.inc` qui n'a que leurs tables ; `runtime/natif.h` porte ce qu'ils
partagent avec `natif.c`. Rien de tout cela ne se versionne.

**29 616 fonctions sur 29 694** sortent du générateur, la plus grande — vingt et un
kilo-octets, deux cent cinquante chemins — comprise. Ce qu'il refuse, il le dit et
le compte (`analysis/feuilles_refusees.txt`) : soixante-deux « fonctions » qui
butent sur des données — la cartographie les a prises pour du code —, et le cœur
du changement de contexte du SDK, avec ses registres de la banque utilisateur,
que le runtime remplace de toute façon. Un seul *cas* de table de sauts qu'il ne
sait pas écrire — presque toujours un mot du pool de constantes, que la règle
« toute instruction est une cible » range parmi les cibles et que rien n'atteint
jamais — ne condamne plus la fonction : ce cas-là est rendu à la traduction, qui
a cette étiquette.

La preuve se tient **par chemin**. Chaque chemin — et chaque arête arrière — note
son passage, et le banc sait lesquels il a comparés. Une fonction est prouvée
quand tous les siens l'ont été, sur elle-même (*instance*) ou sur des fonctions
dont le code ne diffère que par les constantes (*gabarit*). Un verdict porte
l'empreinte du code comparé : que le générateur change ce qu'il émet pour une
fonction, et sa preuve tombe. Une divergence ne s'efface pas, et un gabarit pris
une fois en faute ne prouve plus rien pour personne. `SFC_NATIF=auto` (ou `tout`)
ne met en service que ce qui est prouvé ; le reste garde sa traduction.

Il y a un troisième ordre de preuve, **aux empreintes**, pour ce que ni l'un ni
l'autre banc ne peut juger : les fonctions que le monde interrompt toujours, celles
qui parlent au matériel ou au processeur lui-même, les récursives. Mises en service
(`SFC_NATIF=auto-tout`) le temps d'un parcours dont les empreintes restent celles
de la référence, elles ont fait leur travail à l'identique pour les chemins
qu'elles ont pris — la liste (`SFC_VERIF_LISTE`) les donne, état « service ». Si
les empreintes bougent, la dichotomie (`SFC_NATIF_TRANCHE`) désigne la fautive.

### Le coprocesseur aussi

Le second processeur de la console — l'ARM7, qui porte le pilote son, l'horloge,
l'écran tactile, et sert la cartouche — a son propre binaire : 1 039 fonctions, du
SDK pour l'essentiel, traduites à part (`generated7/`). Le même générateur les lit
(`modules()` rend son binaire et les sections qu'il recopie dans sa WRAM, d'où il
exécute son pilote) avec sa carte à lui :

```bash
bash tools/banc/generer7.sh [--moisson listes…]     # analysis/codemap_arm7.json -> generated7/natif_auto7_N.c
```

**1 035 fonctions sur 1 039** en sortent, et cinq entrées secondaires ; les trois
refus sont, comme sur l'ARM9, le changement de contexte et ses registres de la
banque utilisateur. Un seul défaut du générateur est apparu, que l'ARM9 n'avait
jamais déclenché : sous condition, la retenue d'un décalage par registre était
hissée avant le `if` quand le drapeau C qu'elle lit restait déclaré dessous.

Ses réécritures suivent celles de l'ARM9 dans la table réunie — mêmes listes, mêmes
tranches, mêmes modes ; `NATIF_ARM7`, posé en tête de ses fichiers, fait compter
leurs rangs à partir de leur début. La région « arm7 » suffit au runtime pour
trouver sa table et ses crochets. Le banc à froid l'essaie avec un contexte à
lui — sa carte mémoire, ses registres —, et il a fallu peu de chose : sa WRAM de
64 Ko dans la mémoire suivie, ses accès aux registres comptés comme ceux de
l'ARM9, son arête figée comme le reste (elle passe la main à l'ARM9 par une
bascule de fibre — à froid, elle recharge son compte et revient), le bouchon
posé sur ses appelées aussi, et la carte du code tenue par adresse *hôte*, les
deux processeurs ne voyant pas la même mémoire aux mêmes adresses.

En jeu, l'ARM7 tourne sur sa fibre, hors de portée du banc : il se juge à froid,
puis **aux empreintes et au son**. `SFC_NATIF=auto7-tout` met en service lui seul ;
`SFC_WAV` enregistre ce que le mixeur produit, et deux enregistrements se
comparent octet par octet. Du premier coup : cinq parcours aux empreintes
identiques, et les dix mégaoctets de son de la mission identiques à l'octet près.

La mesure de ce qui passe encore par la traduction a été étendue au coprocesseur
— et son premier « zéro » était faux : l'option de construction n'atteignait que
la bibliothèque de l'ARM9, le compteur ne comptait rien, et c'est un témoin *sans
rien en service* qui l'a montré (il affichait zéro lui aussi). Un zéro ne vaut que
si l'on a vu l'instrument compter. Réparé, il a nommé une seule adresse,
`0x037FC394` : la reprise d'un thread dans l'ordonnanceur du coprocesseur — la
même que sur l'ARM9, cinq cent mille fois par série de parcours. Inscrite dans
`tools/entrees_milieu7.txt`, réécrite, prouvée à froid sur ses quatre chemins.

Résultat : **les deux processeurs de la console tournent entièrement en C natif**
— 30 811 réécritures en place (`SFC_NATIF=entier`), plus une seule fonction
traduite exécutée ni d'un côté ni de l'autre, empreintes et son identiques à ceux
de la traduction. Puis six longs rejeux, joués deux fois — la traduction pure, le
jeu entier en natif : **520 000 images, deux heures vingt-cinq de jeu, empreintes
identiques image par image et 1,1 Go de son identique à l'octet près**
(`tools/banc/campagne_entier.sh`). Côté coprocesseur, 871 réécritures prouvées
sur 1 040 — 82 % de ses chemins —, depuis que le matériel a son bouchon.

Depuis, **le natif est le comportement par défaut** du runtime : les deux
processeurs, toutes les réécritures, après qu'une vraie partie a confirmé ce que
disaient les bancs. `SFC_NATIF=non` rend la traduction pure, qui reste la
référence de toutes les comparaisons — les scripts de `tools/banc/` la demandent
explicitement.

### Le banc en jeu, maintenant que les fonctions appellent

Tant qu'il ne jugeait que des feuilles, le banc pouvait tout se permettre. Une
fonction qui appelle fait tourner le monde, et quatre précautions sont devenues
nécessaires — chacune trouvée par un plantage, aucune par raisonnement :

* **il ne juge que les exécutions atomiques.** Si une interruption a été livrée,
  un thread a changé, le coprocesseur a tourné ou un DMA a copié pendant la
  traduction (`g_perturbations`), ou si elle a touché un registre matériel
  (`g_acces_mmio` — un mot envoyé au coprocesseur partirait deux fois), remettre
  la mémoire en arrière casserait ce qui a avancé : la comparaison est
  abandonnée, la fonction garde le résultat de la traduction et compte pour
  « interrompue ». Les registres matériels ne sont ni restaurés ni comparés en
  jeu : le temps y écrit seul ;
* **la réécriture tourne dans un monde figé** (`g_temps_fige`) : le temps n'avance
  pas, aucune interruption n'est livrée, le coprocesseur ne prend pas la main. La
  traduction a déjà fait avancer tout cela une fois ; le budget d'arêtes fait
  partie de l'état restauré ;
* **les comparaisons s'imbriquent par niveaux** (douze) : une fonction de haut
  niveau — qui ne revient peut-être jamais — peut être en cours de comparaison
  pendant qu'on juge ce qu'elle appelle. Les appelées ne sont jugées que pendant
  la phase « traduction » de celle qui les appelle ; pendant sa phase
  « réécriture », elles tournent telles quelles. Sans cela chaque niveau double
  le travail — seize fois à quatre niveaux, quatre mille à douze : le jeu
  s'arrêtait à la vingtième image. Et l'on renonce vite à ce que le monde
  interrompt toujours (trente-deux tentatives sans une comparaison, puis une
  sur cinq) : ces fonctions-là sont le haut de la pile d'appels, et tant qu'on
  s'obstinait sur elles — huit mille tentatives — elles tenaient les niveaux ;
  avec les grandes fonctions générées, le banc ne jugeait plus que 355
  fonctions par parcours au lieu de 3 000. Chaque niveau a ses deux états, et
  **son interception du saut final** : celle du dehors est mise de côté le temps
  de juger celle du dedans, et seul un saut fait à la profondeur de pile fantôme
  de la fonction jugée compte — ses appelées reviennent par un saut, elles aussi ;
* **une seule fibre** : les niveaux sont ceux du thread du jeu ; dans les autres,
  la traduction tourne seule.

**Le registre d'état entier fait partie de l'état.** Le banc prenait et rendait
les registres, N, Z, C, V, T et le mode — pas I, F, Q, le `spsr` ni les banques
des autres modes. `OS_DisableInterrupts` (`mrs` ; `orr` ; `msr`) passait pour
fausse à chaque appel : la traduction posait I, et la réécriture, repartie de
là, lisait un I déjà posé. Tout cela est maintenant pris, rendu et comparé.

**Une interruption levée compte, même masquée.** Le banc ne comptait comme
perturbation que les interruptions *livrées*. Mais le temps avance pendant la
phase « traduction », et une interruption levée pendant que I est posé remet le
budget d'arêtes à zéro — ce que la réécriture, rejouée dans un monde figé, ne
revit pas : « il reste 256 arêtes au budget, attendu 0 », une fois sur quatre-vingt
mille, et la même divergence répétée dans les quatre fonctions imbriquées à cet
instant. La levée est maintenant une perturbation (`hw_check_irq`).

**Seules les pages écrites sont copiées.** Une comparaison prenait l'état entier
trois fois — cinq mégaoctets avant, autant après, autant à remettre — et le
comparait : deux millisecondes, quand une feuille touche une page ou deux.
Quand un banc est demandé, toute la mémoire que le jeu peut écrire tient dans
une seule réservation dont Windows suit les pages écrites (`MEM_WRITE_WATCH`,
`runtime/memory.c`) — par qui que ce soit : code traduit, réécriture, copie
native du runtime. Chaque état du banc est une image entière tenue à jour page
par page : le prendre, c'est recopier les pages écrites depuis la dernière
fois ; le rendre, les remettre. Et l'image d'après la traduction, prise avant
que l'état d'avant soit remis et que la réécriture tourne, a pour pages périmées
*exactement* celles que l'une ou l'autre a écrites : il n'y a rien d'autre à
comparer, ni à remettre. Six cents images de mission avec toutes les fonctions
surveillées : 655 secondes avant, 15 après, mêmes verdicts au chiffre près.
L'épreuve : `SFC_VERIF_SABOTAGE=0x…` abîme un octet de RAM, par un pointeur nu,
après chaque réécriture — 476 fonctions comparées sur 476 ont crié à chaque
comparaison, et les empreintes du parcours sont restées celles de la référence :
la comparaison voit tout, et la remise en état ne laisse rien.

**Le temps est un registre.** Le temps simulé se compte en arêtes arrière, et le
banc compare désormais ce qu'il en reste au budget. Il ne le faisait pas, et vingt
mille fonctions « prouvées » changeaient les empreintes dès la vingtième image :
une dichotomie sur les rangs mis en service (`SFC_NATIF_TRANCHE=a-b`, quinze essais
de quelques secondes) a désigné une fonction de calcul vectoriel dont la
réécriture comptait une arête de trop — elle rejoignait une tête de boucle *en
avançant*, par un autre chemin de l'arbre, et ce saut d'écriture n'est pas une
arête. Puis l'inverse : une condition déjà tranchée qui menait à un saut arrière
ne comptait plus la sienne. Aucun registre, aucun octet de mémoire ne montrait
l'un ni l'autre ; seul le moment où tombe l'interruption suivante changeait.

La traduction est appelée par son entrée de table exacte (`rt_entree_region`), non
par `rt_find`, qui choisirait d'après le mode courant : une fonction ARM appelée
depuis du Thumb par `blx` tourne avec le bit T de son appelant.

### Le banc à froid

`SFC_VERIF_FROID=image[:tranche/nombre]` essaie donc chaque fonction **hors de
toute partie** : à l'image dite, le jeu s'arrête ; pour chaque fonction — après
avoir posé en mémoire l'image de son overlay, car la traduction lit ses
constantes dans le code — des registres tirés au sort, un bac de 64 Ko rempli de
pointeurs vers lui-même, de zéros et de petites valeurs, et les deux versions
comparées par le même `verifier` qu'en jeu, pile morte comprise. Les constantes
auxquelles la fonction compare sont semées dans les tirages, sans quoi le chemin
« `r0 == 0x23` » ne serait jamais pris. L'horloge est figée : lire un registre de
balayage n'y fait plus avancer le temps, les accès hors carte ne sont plus
journalisés — il y en a des millions.

Certains essais **ne comptent pas**, et le banc le sait :

* la traduction a touché un registre matériel par un pointeur lu en mémoire, que
  le tirage ne pouvait pas prévoir ;
* elle a **écrasé son propre code** — une copie nourrie d'un compteur tiré au sort
  finit par balayer toute la mémoire, et la traduction relit ses constantes dans
  le code alors que la réécriture les connaît ; aucune fonction du jeu ne s'écrase
  elle-même ;
* elle n'en finit pas : au-delà de deux cents arêtes, ou de quatre cents appels
  imbriqués pour une récursion, `rt_edge` et `RT_CALL` reviennent au banc par
  `longjmp` ; un arrêt fatal — le tirage a mené là où le jeu n'irait jamais — y
  revient de même. Si c'est la réécriture seule qui n'en finit pas, c'est une
  divergence ;
* elle ne rend pas la main *sans franchir d'arête* : une machine à états, une
  boucle autour d'une table de sauts, que ni la traduction ni la réécriture ne
  comptent. Un garde veille ; après cinq secondes sans essai fini, il nomme la
  fonction dans `SFC_FROID_EXCLURE`, rend la liste de ce qui est fait et quitte
  avec le code 3 — la tranche est relancée, qui l'écartera
  (`tools/banc/tranche.sh`).

Ce que le runtime remplace déjà par ses propres fonctions — la commutation de
contexte des threads du SDK — n'est ni réécrit ni jugé : sa traduction ne tourne
jamais.

**Des essais qui se souviennent.** Tirer chaque essai à neuf prouve vite les
petites fonctions et bute sur les autres : quarante mille chemins restaient
jamais vus, gardés presque tous par des conditions simples — « `[r0 + 8] == 3` »,
« le résultat de l'appel vaut 0 » — mais empilées sur quatre à huit niveaux, ou
derrière l'index d'une table de sauts. Trois choses y répondent. Un essai est
tout entier dans sa **graine** : celui qui découvre un chemin devient une
*semence*, et l'on repart de lui en ne retouchant qu'une chose ou deux — six
conditions ne se tirent pas d'un coup, elles se franchissent une à une, chaque
chemin atteint servant de marchepied au suivant. Les retouches visent les
**sites** que le générateur lit dans le code qu'il vient d'émettre : de quelles
entrées dépendent les conditions — tel registre, tel mot de l'objet qu'il
désigne, un champ de l'objet dans l'objet, une globale, le champ d'un singleton,
la réponse d'une appelée. Treize registres fois quarante décalages, c'est cinq
cents endroits où écrire ; il y en a rarement plus de dix qui comptent. Enfin le
**bouchon obéit** : « la réponse du deuxième appel vaut 7 » est une retouche comme
une autre — et même « le deuxième appel écrase r4 », qu'une vraie appelée
préserverait : « `r4 = new` ; … ; `if (r4 == 0)` », le test de nullité que le
compilateur répète après chaque constructeur de base, ne se prend pas autrement,
et les deux versions doivent s'y accorder comme ailleurs. De même « `(r0 + 8) == 0` »,
la nullité d'un `this` ajusté, ne se prend qu'avec r0 = -8 : le générateur lit ces
valeurs-là dans les conditions et les donne au tirage. Une fonction du SDK —
deux `switch` de quinze cas, l'un sur la réponse d'une appelée, l'autre sur une
globale — plafonnait à dix-huit chemins sur trente ; elle les a tous en deux
mille essais. Rien de cela ne touche à la preuve : tout état d'entrée est bon
pour comparer deux exécutions, on ne fait que mieux les choisir.

Et un défaut du banc, que ces chemins manquants ont fini par montrer : pour ne pas
écraser de code, il refusait de semer une globale dans la zone de code de *tous*
les overlays — or ils se partagent leurs adresses, et les données de l'un tombent
presque toujours « dans le code » d'un autre. Aucune globale d'overlay n'était
jamais semée, ni aucun singleton. Seuls comptent le code de l'ARM9 et celui de
l'overlay qu'on essaie : quatre cents fonctions prouvées de plus à la passe
suivante.

Un second, du même ordre. Un essai qui « écrase du code » ne compte pas — la
traduction relit ses constantes dans le code, la réécriture les connaît. Mais le
banc exigeait que *rien* ne bouge, ni dans l'ARM9, ni dans l'overlay, ni dans
l'ITCM ; or les chemins qu'aucune partie ne prend — le `this` nul, le `r4 == 0`
d'après constructeur — écrivent justement à l'adresse 0, 4, 0x18, qui sont un
miroir de l'ITCM, et ils étaient tous annulés. Le seul code qui doive rester
intact est celui de la fonction qu'on juge : ce qu'un essai écrase ailleurs, les
deux versions le voient pareillement, et l'essai suivant le retrouve remis. Sept
cents fonctions de plus — et, sur ces chemins que le jeu ne prendra jamais, pas
une divergence. (Et ce qu'on protège du semis se compte mot par mot, fonction par
fonction : une plage du premier au dernier octet de code s'étire, à cause de
quelques fausses fonctions vues dans les données, par-dessus les globales.)

Les **appelées répondent par un bouchon** : un même crochet posé sur toutes les
fonctions traduites et pris aussi par le dispatch. Il note dans un journal ce
qu'on lui demande — la cible, les arguments, la pile, l'adresse de retour, le
mode — et répond par des registres et des drapeaux tirés d'une graine propre à
l'essai, les mêmes pour la traduction et pour la réécriture ; il écrit aussi
quelques mots dans l'objet reçu et sous la pile, comme le ferait une vraie
appelée. Les deux exécutions doivent laisser le même journal : appeler autre
chose, ou avec d'autres arguments, ou dans un autre ordre, est une divergence
comme une autre. Une fonction est ainsi prouvée équivalente à sa traduction
*quelles que soient* ses appelées.

**Le matériel répond par un bouchon, lui aussi.** Le banc écartait du froid les
fonctions qui nomment un registre matériel, et annulait tout essai qu'un pointeur
tiré au sort menait là : le matériel ne se rejoue pas. Mais à froid rien n'oblige à
le faire tourner : une lecture de registre reçoit une valeur tirée de la graine de
l'essai et du rang de l'accès — la même pour la traduction et pour la réécriture —,
une écriture est notée, adresse et valeur, et le vrai matériel ne voit rien : ni
DMA lancé, ni mot envoyé à l'autre processeur, ni horloge qui avance. Les deux
versions doivent laisser le même journal, comme pour les appelées. Cinq cent
cinquante fonctions de plus au banc — le pilote son du coprocesseur surtout —, et
pas une divergence. Seules les récursives restent à juger en jeu.

Le banc juge **à l'instant du saut** les fonctions qui finissent par sauter
ailleurs : `rt_jump` note la cible au lieu de la suivre, les deux états se
comparent, puis le saut est suivi une seule fois.

Le banc lui-même a été éprouvé des deux côtés par une faute délibérée
(`--faute` abîme `r12` dans chaque fonction) : il a crié sur toutes celles qu'il
voyait, 937 sur 937 en jeu, 616 sur 616 à froid. Et il a trouvé de vraies fautes
du générateur avant qu'elles ne comptent : une cible d'appel liée directement là
où la traduction passe par le dispatch (le bit T différait), un saut « arrière en
adresse » vers du code jamais parcouru pris pour une boucle, un appel récursif
pris pour un branchement long, une table de sauts suivie par `rt_jump` là où la
traduction fait un `goto`, un flot poursuivi dans la fonction voisine.

Bilan : à froid, douze processus, cent secondes pour tout le jeu — **28 788 fonctions
essayées, zéro divergence**. En jeu, cinq parcours : 4 672 fonctions
comparées sur des données réelles, zéro divergence — puis quatorze longs rejeux,
un million deux cent mille images, quatre-vingts millions de comparaisons : zéro. **26 689 réécritures
prouvées** (26 470 par instance, 187 par gabarit, 32 aux
empreintes), 3 072 en attente d'un chemin que ni le tirage ni la partie
n'ont su prendre — et aucune fautive. Surtout : avec **les 29 761 réécritures — 29 616 fonctions, 145 entrées secondaires —
toutes en service** à la place de la traduction, et les onze faites à la main
(`SFC_NATIF=entier` : le jeu entier en C natif), les cinq
parcours — 4 400 images de mission, quatre fois 15 000 de menus, de modes, de
carte stratégique et d'options — gardent des empreintes identiques à celles de
la traduction pure, mémoire entière comprise.

**Ce qui passe encore par la traduction.** Construit avec `-DSFC_COMPTE_TRADUIT=ON`,
le code traduit compte ses exécutions, passé le crochet, selon qu'on entre dans
une fonction par son entrée ou en son milieu (`SFC_TRADUIT=fichier`). Le jeu
entier en natif, la première mesure a nommé ce que le générateur oubliait sans le
dire : quarante-six fonctions de plus de quatre kilo-octets — les boucles de scène,
les machines à états des modes —, sautées par une limite de taille qui ne comptait
pas ses refus, et une fonction refusée pour un mot de données qui tournait bel et
bien. Celles-là générées, **plus aucune fonction traduite ne tourne depuis son
entrée** sur les cinq parcours. Restaient quatre à douze fonctions où l'on entre
*par le milieu* — la mesure note aussi par quelle adresse —, et trois raisons : la
**reprise d'un thread** — `OS_SaveContext` « revient deux fois », comme `setjmp`,
et l'ordonnanceur du SDK (`0x0202DD18`) reprend en `0x0202DD9C` à chaque
changement de thread, trois cent mille fois par parcours ; les **entrées
secondaires** — cent quarante adresses au milieu d'une fonction qu'une autre
rejoint par un `b` ou un `bl` : la division signée finit dans la non signée, cent
soixante mille fois ; et quelques **retours déroulés par le dispatch**.

D'où les **réécritures secondaires** : la même fonction, interprétée depuis une
autre adresse que son entrée. Tout ce qui, dans les règles du traducteur, parle de
« l'entrée de la fonction » continue de parler de celle de la fonction entière,
puisque c'est à sa traduction — prise au même endroit — qu'on la compare ; seule
change l'adresse d'où part l'interprétation. Les adresses rejointes par le code
émis, le générateur les connaît ; les autres ne se voient qu'à l'exécution, et
`tools/entrees_milieu.txt` garde celles que la mesure a nommées. Une secondaire
pouvant en rejoindre une autre, on recommence tant qu'il en vient. Le crochet
d'une fonction traduite ne jouant qu'à son entrée, ce sont les sites qui
retrouvent la secondaire — `SUITE_DIRECTE`, `APPEL_DIRECT`, la reprise après un
changement de thread (`natif_reprendre`), le dispatch quand un saut tombe au
milieu d'une fonction — par (région, entrée de la fonction entière, adresse) ; dans
les listes sa région s'écrit « `arm9@0202DD18` », deux fonctions qui se
chevauchent pouvant être rejointes à la même adresse. Cent quarante-cinq
secondaires, jugées comme les autres : la reprise de thread a ses cinq chemins
prouvés à froid.

Résultat : **sur les cinq parcours, plus une seule fonction traduite de l'ARM9 ne
s'exécute**, ni depuis son entrée ni depuis son milieu — 29 771 réécritures en
place, empreintes exactes. La traduction n'est plus que la référence à laquelle
le banc compare.

La campagne se refait ainsi :

```bash
python tools/decomp_feuilles.py                      # génère ; puis compiler
bash ../../tools/banc/tranche.sh 0 12 101                          # … une tranche à froid par processus
SFC_VERIF=auto SFC_VERIF_LISTE=jeu.txt ./sfc.exe                   # en jeu, sur un rejeu
python tools/decomp_feuilles.py --moisson froid/p*.liste jeu.txt   # verse les verdicts, régénère
```

| variable | ce qu'elle donne |
|---|---|
| `SFC_VERIF=auto` | compare toutes les fonctions du générateur à leur traduction, en jeu |
| `SFC_VERIF=auto-reste` | seulement celles qui ne sont pas encore prouvées |
| `SFC_VERIF_SABOTAGE=0x…` | abîme cet octet de RAM après chaque réécriture : le banc doit crier partout, et le jeu n'en rien garder |
| `SFC_VERIF_FROID=image[:k/n]` | les essaie toutes à froid à cette image, puis quitte |
| `SFC_VERIF_LISTE=fichier` | le verdict par fonction : région, adresse, état, appels, divergences, chemins vus, nombre de chemins, empreinte du code |
| *(rien)* | **le jeu tourne en natif** : sans `SFC_NATIF`, c'est `entier` — sauf pendant un banc (`SFC_VERIF`, `SFC_VERIF_FROID`), qui garde la traduction autour de ce qu'il juge |
| `SFC_NATIF=non` | la traduction pure : la référence de toutes les comparaisons |
| `SFC_NATIF=auto` / `tout` | met en service les fonctions **prouvées** (et, pour `tout`, les réécritures à la main) |
| `SFC_NATIF=auto-tout` | met en service toutes les fonctions non fautives, prouvées ou non — pour juger le lot aux empreintes |
| `SFC_NATIF=entier` | le jeu entier en natif : les réécritures à la main et toutes celles du générateur, sur les deux processeurs |
| `SFC_NATIF=auto7` / `auto7-tout` | le coprocesseur seul : ses réécritures prouvées, ou toutes |
| `SFC_TRADUIT=fichier` | (construction `-DSFC_COMPTE_TRADUIT=ON`) ce qui passe encore par la traduction : région, entrée, exécutions depuis l'entrée, depuis le milieu |
| `SFC_NATIF_SANS=0x…,0x…` | celles-là gardent leur traduction : les coupables déjà trouvés, pour que la dichotomie cherche le suivant (`tools/banc/dicho_tout.sh`) |
| `SFC_FROID_EXCLURE=fichier` | les fonctions que le garde du banc à froid a écartées, d'une relance à l'autre |
| `SFC_NATIF_TRANCHE=a-b` | ne met en service que les fonctions du générateur de rang a à b-1 : de quoi trouver par dichotomie celle qui change les empreintes |
| `SFC_FROID_LISTE=fichier` | à froid, ne reprend que les fonctions listées (« région adresse ») — `analysis/feuilles_partielles.txt`, qu'écrit le générateur |
| `SFC_FROID_GRAINE=n`, `SFC_FROID_ESSAIS=n` | d'autres tirages, plus d'essais (120 par défaut ; 3 000 pour une passe ciblée) : les chemins vus s'ajoutent d'une passe à l'autre |
| `SFC_FROID_SEUL=0x…`, `SFC_FROID_BAVARD=1`, `SFC_FROID_ARETES=n` | enquêter sur une seule fonction, nommer chacune, changer le plafond d'arêtes |

### Trois pièges du banc, qui n'ont rien à voir avec la fidélité

* **Le son écoutait la carte son.** Le mixage recale sa cadence d'un pour cent sur
  le remplissage de la file de sortie — il le faut pour jouer sans trous — et le
  jeu lit l'état des canaux : un canal qui se tait une image plus tôt, et un bit
  du masque des voix (`0x020B6188`) change. Au calme, rien ne se voyait ; douze
  processus du banc à froid en parallèle, et la *traduction pure* changeait
  d'empreinte sur une image vers la 862ᵉ, puis retombait juste. La dichotomie,
  qui suppose un monde reproductible, a « désigné » deux fonctions qu'un espion
  (`SFC_ESPION`) a montrées *jamais appelées*. Un coupable qui n'a pas pu agir
  accuse le banc, non le code : rejouer deux fois la référence, sous charge,
  avant de chercher. Pendant un banc (`SFC_MAX_FRAMES`, `SFC_HASH`,
  `SFC_NO_VIDEO`), la cadence du mixage est maintenant exacte. En jeu, le
  recalage demeure — le corriger vraiment, c'est rééchantillonner la sortie en
  aval sans toucher à l'état des canaux : à faire avec les modifications, sur la
  version stable.
* **Le coprocesseur avance pendant la comparaison.** Une fonction dont l'exécution
  contient une boucle fait avancer l'horloge, donc le coprocesseur, qui écrit en
  mémoire — et il le fait deux fois, une par exécution comparée. Le banc accuse
  alors une réécriture parfaitement juste. On le reconnaît à ce que les écarts se
  répètent à l'identique et qu'ils disparaissent avec `SFC_ARM7=0`, à faire dès
  qu'une divergence a l'air de ne pas dépendre de la fonction.
* **Une fonction jamais appelée ne prouve rien.** Le banc dit maintenant combien
  d'appels il a *vus*, et pas seulement combien il en a comparés — `0 appel vu`
  veut dire que le rejeu ne passe pas par là, pas que la réécriture est bonne.
  C'est ce qui distingue une fonction souvent *référencée* d'une fonction souvent
  *appelée* : `SFC_STAT_CALL=n` donne les secondes.

## 5 ter. Les modifications

Le jeu réécrit est la base ; une modification se pose dessus **sans réécrire de
fonction**, à un **point de modification** : un crochet appelé juste avant une
instruction du code d'origine. La liste vit dans `runtime/modifications.c` :

```c
#define LES_POINTS(P) \
    P(0, "arm9", 0x02066A8E, portee_coupure, "…") \
    …
```

— rang, région, adresse de l'instruction, crochet, ce qu'il y trouve. Le
générateur (`tools/decomp_feuilles.py --modifications`, lu par défaut) place à cet
endroit du code émis un `MODIF(c, rang)` ; l'état du processeur y est écrit avant et
relu après, exactement comme autour d'un appel. Le crochet lit et change registres
et mémoire comme le ferait le code du jeu. Tant qu'il est nul, la fonction fait ce
qu'elle faisait : le générateur dit combien de points il a posés, et les fonctions
qui en portent se jugent au banc comme les autres.

Ajouter un point : une ligne `P(…)`, `python tools/decomp_feuilles.py`, puis
compiler. Seul le jeu en natif — le défaut — connaît les modifications ; la
traduction pure (`SFC_NATIF=non`) reste le jeu d'origine.

| modification | variable | points |
|---|---|---|
| portée d'affichage | `SFC_PORTEE=facteur` (1 à 4) | distance de coupure (`0x02066A8E`, r1) ; plan lointain de la projection (`0x02066A46`, r0) ; hauteur de l'arrière-plan (`0x0218DACA`, r6) ; sa première rotation (`0x0218DB4A`, `sp+0x38`) |
| portée des tirs | la même | durée de vie des tirs ennemis (`0x021E0E82`, r0) ; du laser d'Arwing en solo (`0x021DF980`, r0) ; 17 portées de tir d'ennemis (points 6 à 22, r0), doublées sans dépasser la vue (4 000 × facteur) ; durée du laser Plasma (`0x0221B5B8`, r0) et début de son rétrécissement, repoussé d'autant (`0x0221B734`, r0) |
| la manette | `SFC_MANETTE=0` la retire ; sans manette, rien ne change | le stylet tient-il le manche, est-il posé : pour piloter (`0x021E9E84`, `0x021E9E96`, r0 ; r4 le contrôle, son point en `+0x10`) et pour l'aide au verrouillage (`0x021EA30A`, `0x021EA31A`) ; la fenêtre de reconnaissance du cercle, qui expire (`0x021E9F76`, `+0x3C` du contrôle) ; dans le contrôle sur rails des poursuites de missile, le stylet est-il posé (`0x021EB016`, r1), tient-il le manche au premier tour (`0x021EB02A`, r0), la fenêtre du cercle (`0x021EB080`) ; le plan d'ouverture d'un combat, le stylet est-il posé (`0x0204863C`, r0) ; les commandes du tour, avant que le cockpit ne les lise (`0x0220F01E`, r4 le cockpit) |
| les mods (page Mods du lanceur) | `SFC_MOD_…` (§ 5) | points 35 à 59 : voir « Les mods du lanceur » ci-dessous |

Preuves de la portée d'affichage, crochets éteints : `0x0218DA9C` sans écart sur
2 481 appels comparés en vol ; `0x02066A34`, qui calcule la projection avec le
diviseur du matériel et que le banc ne peut donc pas comparer en jeu, aux
empreintes — les 10 049 images du rejeu de Titania et 30 000 des dernières
missions identiques à la traduction. Avec la portée : les 88 436 images des
dernières missions à ×3, et 240 images affichées par seconde tenues à ×2 comme
à ×1.

Le plan lointain de la projection (point 1) ne recule qu'au-delà de 2,4 : à ×2 la
matrice de projection reste celle du jeu.

Preuves de la portée des tirs : à ×1, les mêmes empreintes (Titania, dernières missions) ; à ×2,
sur les dernières missions, les tirs ennemis vivent 3 s au lieu de 1,5, les lasers d'Arwing qui ne
touchent rien 66 tours au lieu de 33, les lasers Plasma 3,59 s au lieu de 1,79 (7 840 unités au lieu
de 3 880), leur taille ne tombant qu'à la fin (0,06 à 3,59 s, quand elle tombe à 1,79 s d'origine), et
le chasseur de l'overlay 7 tire 32 fois au lieu de 22, en commençant plus tôt.

### Les mods du lanceur

Des changements de règles, éteints par défaut (`runtime/mods.c`). Ils agissent au moment où le jeu se sert
d'une valeur, jamais sur la valeur rangée : un épisode de l'histoire qui change le laser de Fox ne défait pas
le mod, et la sauvegarde ne garde pas le laser imposé. Les bombes infinies et les dégâts reçus touchent
pourtant à ce qu'elle retient : le stock de bombes, le bouclier qui reste (§ 3, « Les pilotes, leurs armes et
leur vie »). Le vaisseau se règle pilote par pilote (`SFC_MOD_LASER=fox:plasma,slippy:double`) : chaque crochet
reconnaît le pilote par son rang, là où le jeu le lit.

| mod | variable | points |
|---|---|---|
| laser d'un pilote | `SFC_MOD_LASER` | le type lu au tir (`0x021FE246`, r0 ; le pilote du véhicule en `+0x28` de r4), pour la cadence (`0x021FE192`, `0x021FE15A` ; le pilote en `[sp]`) ; pour Fox seul, le jeu « ArwingDynamicsPlayerFox… » que `0x021F78C4` choisit selon son laser (`0x021F7A64`, r0) et l'image du briefing (ovl14 `0x0222C1A8`, r1) |
| verrouillage d'un pilote | `SFC_MOD_VERROUILLAGE` | le type lu par le véhicule (`0x021FE838`, `0x021FE8E0`, `0x021FE984`, r0 ; le pilote en `[sp]`) et par le cockpit (`0x0220F814`, r4 ; `0x0220FD0A`, r0 ; le pilote en `+0x48` et `+0x50` de r7) |
| jauge d'un pilote | `SFC_MOD_JAUGE` | la taille que lit la méthode `+0x14` du véhicule (`0x021FE9F2`, r0 : 0 petite, 1 moyenne, 2 grande ; le pilote en `[sp]`), d'où la capacité, 25, 50 ou 75 |
| apparence du vaisseau d'un pilote | `SFC_MOD_VAISSEAU` | la clé du pilote dont les fichiers se chargent (ovl0 `0x02188FC2`, seize octets en r1 ; le pilote en `sp+0xBC`) ; le pilote des six lectures de la fiche (`0x021FE626` à `0x021FE796`, r1) |
| turbo infini | `SFC_MOD_TURBO` | la dépense d'un tour de turbo et de frein (`0x021EDF10`, `0x021EDE44`, r0) ; les tonneaux gardent la leur, sans quoi un tonneau lancé à la manette ne finirait plus |
| bombes infinies | `SFC_MOD_BOMBES` | le stock diminué, avant d'être rangé : écran tactile (`0x022010B6`, r0), radar (`0x022052B4`, r1) ; la manette ne décompte plus les siennes |
| tir chargé au maximum | `SFC_MOD_CHARGE` | le niveau de la cible (`0x021E5F26`, r0 = 5) |
| invincible, dégâts reçus | `SFC_MOD_INVINCIBLE`, `SFC_MOD_DEGATS_RECUS` | à l'entrée de `0x021F071C`, r2 nul ou multiplié, pour le seul acteur du joueur — sauf la balise qu'il faut franchir à temps (le coup revient en `0x0226B611`) |
| dégâts infligés | `SFC_MOD_DEGATS` | à l'entrée de `0x0216CC8C`, r1 multiplié quand l'attaquant est le joueur ou n'est pas noté ; « un-coup » le porte à la vie qui reste |

Épreuves, sur la partie obs52 rejouée sans fenêtre : sans mod, les empreintes des 30 000 premières images
restent celles de la référence. Dans le combat de Fox, le laser imposé change la fonction de tir appelée
(44 tirs plasma deviennent 22 tirs de Zapper ; 67 tirs courbes ; 62 doubles) ; le verrouillage large y
déclenche 8 verrouillages, aucun d'origine ; le vaisseau de Falco y fait dessiner 16 polygones de moins à
chaque relevé — l'écart entre les deux carlingues —, le reste de la scène restant identique. Dans le premier
combat (Dash) : une manette qui tient le turbo le garde 550 images au lieu de 200, la jauge restant pleine ;
la vie reste à 100 avec l'invincibilité ; le stock de bombes reste à 2 ; les tirs chargés font 320 au lieu de
20 ou 40 ; dégâts × 10, le premier ennemi meurt à l'image 6 932 au lieu de 7 010. Les réglages de Dash, dans ce
combat : le Zapper remplace son laser double (9 tirs), sa jauge passe de 50 à 75, et le vaisseau de Falco fait
dessiner 48 polygones de plus quand on le voit ; ceux de Fox ne le touchent pas. Jodie (obs18, vers l'image
104 700), qui n'a pas de verrouillage, comme Slippy : avec le plasma, elle tire 87 plasmas au lieu de 90 doubles ;
avec un verrouillage simple, ou large et une grande jauge, le combat va à son terme sans faute — le joueur n'y
faisant aucun geste de verrouillage, il n'y en a pas. Aucune partie enregistrée n'a de combat de Slippy.

Pas encore fait : le vaisseau en 3D de la fiche des unités, sur la carte (ovl14 `0x02229460`), reste celui de
Fox ; la portée d'affichage n'allonge pas le tir courbe ni le Zapper (`0x3000` en `0x0221A938`).

## 6. Leçons de méthode

Elles ont toutes coûté cher, et elles se répètent.

**Une optimisation ne se juge pas sur la cadence.** Pousser le budget d'arêtes
ou le seuil du coprocesseur donne jusqu'à cinq fois plus d'images par seconde —
en mettant le coprocesseur hors jeu, le son devenant parfaitement muet. Toute
modification de l'ordonnancement se vérifie sur l'enregistrement sonore.

**Mesurer, ne pas déduire.** Le temps de simulation était calculé comme « tout
ce qui n'est pas le rendu » : il imputait au jeu tout ce qu'on avait oublié de
chronométrer. Mesuré directement, il change de nature.

**Un relevé cumulé ne dit rien d'une fenêtre glissante.** Compter les cellules
de terrain sur toute une session donnait trente cellules et la conclusion fausse
que le jeu dessinait toute la carte ; comptées par image, il y en a huit à onze.

**Une matrice de projection est définie à un facteur près.** Celle du jeu est
normalisée au quart : les distances se lisaient quatre fois trop courtes.

**La profondeur perspective n'est pas linéaire.** Un objet au dixième de la
portée se lit à 99 % de profondeur normalisée. C'est la distance qu'il faut
relever.

**Vérifier que le compilateur ne le fait pas déjà.** 30 % des calculs de
drapeaux du code généré sont morts — mais GCC les élimine, et les traquer dans
le traducteur n'aurait rien rapporté. De même, `-O2` sur le code généré
n'apporte rien : ses fonctions sont trop grandes.

**Un `pop {pc}` n'est pas toujours un retour.** Une petite fonction
intermédiaire peut empiler l'adresse d'une autre fonction et lui passer la main
par ce même `pop` — un appel de queue, pas un retour. Le traducteur les prenait
tous pour des retours et coupait l'exécution avant le saut : le jeu détectait
bien le passage dans un anneau, mais l'appel qui créait le bonus était sauté.
Les deux cas se distinguent par la pile fantôme, et `tools/recomp/test_return_dispatch.py`
les couvre désormais. Le prix de cette distinction est de 17 % sur la vitesse de
simulation, chaque `pop {pc}` passant par une comparaison au lieu d'un retour.

Cette classe d'erreur est la plus traître du projet : elle ne produit ni arrêt,
ni message, ni corruption — seulement une action qui n'a pas lieu. Chercher la
cause du côté des données ou du calcul, comme le bon sens y invite, mène très
loin du vrai coupable.

**Les pièges d'outillage** : les heredocs imbriqués cassent en shell ; les
ancres de remplacement doivent être uniques et vérifiées ; un exécutable en
cours d'exécution ne peut pas être relié sous Windows.

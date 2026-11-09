# Plan du portage natif

Objectif : jouer à *Star Fox Command* sur PC, sans émulateur, **exactement**
comme sur DS. Le jeu d'origine est traduit instruction par instruction en code
natif ; les appels au matériel et au système de la DS sont remplacés par une
couche PC. Les modifications (framerate, modèles, suite de l'histoire)
viennent ensuite, sur cette base fidèle.

## Ce qu'il y a dans la cartouche

| Binaire | Rôle | Taille | Compilateur |
|---|---|---:|---|
| ARM9 statique | bibliothèques NitroSDK (OS, FS, GX, SND côté ARM9, DWC), moteur MGL, cœur du jeu | 723 Ko + 31 Ko ITCM | Metrowerks CodeWarrior, C++ |
| 26 overlays ARM9 | modules chargés à la demande : overlay 0 = jeu permanent, 2 = Arwing et tirs, 3–4 = stratégie/carte, 5–22 = ennemis et boss, 23 = stockage ennemis, 24 = GameSpy (Wi-Fi), 25 = téléchargement (DS Download Play) | 1,3 Mo | idem |
| ARM7 | pilote son (Nitro Composer), écran tactile, RTC, alimentation, sans-fil | 165 Ko | NitroSDK, C |

Constats qui guident le plan (voir `carte_binaire.md`) :

- Le code est du **C++ sans RTTI** (seule la bibliothèque standard Metrowerks
  en a) : les noms de classes viendront des assertions, des chaînes et de la
  structure, pas des typeinfo.
- Le jeu **enregistre ses commandes de script et ses comportements d'ennemis
  par leur nom** au démarrage (tables de 12 octets dans l'ARM9, constructeurs
  statiques dans les overlays) : chaque commande a l'adresse de son
  implémentation, ce qui nomme d'emblée une centaine de fonctions.
- L'ARM9 n'appelle **aucune fonction du BIOS** (`svc`) : division et racine
  carrée passent par les registres matériels (`0x04000280`–`0x040002B8`),
  l'attente d'interruption par CP15. Le BIOS ne compte que pour l'ARM7.
- Le jeu utilise les **threads NitroSDK** (gestionnaire d'IRQ en ITCM à
  `0x01FF81BC`, table des threads en DTCM) : la couche PC doit fournir un
  changement de contexte (fibres Windows).
- Les assertions du jeu sont compilées : `expr`, fichier, ligne et **nom de
  fonction** sont écrits dans une structure puis `bkpt`. Une mine de noms.

## Véhicule : recompilation statique, puis décompilation progressive

Deux façons de « traduire les instructions » existent ; on les enchaîne.

### Phase 1 — Recompilation statique (jouable vite, fidèle par construction)

Un traducteur lit le code ARM/Thumb et écrit, pour chaque fonction, une
fonction C équivalente : les registres deviennent des variables, chaque
instruction sa traduction exacte (drapeaux compris), chaque bloc de base une
étiquette. Le tout se compile en `.exe` avec un compilateur PC. Aucune
interprétation à l'exécution : c'est du code natif.

Autour de ce code traduit, une **couche d'exécution** (`runtime/`) fournit ce
que le code attend de la DS :

| Besoin du code | Sur DS | Sur PC |
|---|---|---|
| Mémoire | 4 Mo RAM, ITCM, DTCM, VRAM, palettes, OAM | tableaux, accès direct ; les adresses `0x04xxxxxx` passent par un répartiteur de registres |
| Interruptions | VBlank, timers, DMA, IPC, carte | horloge à 59,83 Hz qui appelle le gestionnaire recompilé ; DMA exécuté à l'écriture du registre |
| Threads | `OS_SaveContext` / `OS_LoadContext` en assembleur | fibres Windows, une par thread du jeu |
| Système de fichiers | lecture de la carte par registres `ROMCTRL` | lecture de la ROM `.nds` de l'utilisateur |
| 3D | FIFO géométrique (`0x04000400`, listes d'affichage) | interpréteur des commandes GX (`runtime/gx.c`, `runtime/gx_geom.c`), rendu OpenGL à la résolution voulue |
| 2D | deux moteurs (fonds, sprites), écrans 256×192 | rasterisation logicielle fidèle, affichée en texture |
| Son | commandes IPC vers le pilote de l'ARM7 | ✅ l'ARM7 est traduit lui aussi (`SFC_ARM7=1`) ; mixeur des 16 voix vers la sortie de l'hôte |
| Entrées | boutons, écran tactile | clavier, manette, souris pour le tactile |
| Sans-fil | WM / DWC | absent (renvoie « pas de connexion ») |

Ce que ça donne : un exécutable qui **est** le jeu, bit pour bit dans sa
logique, rendu en haute résolution. Le code traduit est illisible (comme un
désassemblage) mais exact ; il sert de référence.

### Phase 2 — Décompilation progressive (lisible, modifiable)

Fonction par fonction, en partant de ce qui intéresse le projet (vol de
l'Arwing, ennemis, missions, menus), le code traduit est remplacé par du C++
écrit à la main, équivalent au code d'origine. Chaque remplacement se vérifie
en comparant les deux versions sur les mêmes entrées (même sauvegarde, mêmes
manettes enregistrées) : l'état mémoire doit rester identique.

C'est là que naissent les modifications : un framerate débloqué se fait dans
une boucle de jeu lisible, une suite de l'histoire dans un système de missions
compris.

## Ordre de marche

1. **Cartographie** (`tools/codemap.py`) — ✅ toutes les fonctions, leur mode,
   leurs blocs, leurs appels ; amorces à trois niveaux de confiance,
   `switch` CodeWarrior, thunks, appels et pointeurs croisés entre ARM9 et
   overlays (deux tours ; les amorces venues d'ailleurs ne sont semées que
   dans les trous, par adresse croissante, car une adresse d'un emplacement
   partagé ne désigne qu'un overlay ; un tour statique final reprend les
   appels et pointeurs des overlays vers l'ARM9, qui seuls atteignent
   certains accesseurs). Les pointeurs de données vers des chaînes ne
   deviennent pas des fonctions (une chaîne prise pour du code avale les
   vtables qui la suivent). Une passe finale répare les frontières
   orphelines (amorce refusée qui coupait une fonction) et coupe les blocs
   d'une fonction là où une voisine y chute ; les sorties de bloc
   qu'aucune étiquette ne peut recevoir sont listées dans
   `analysis/sans_etiquette.txt`. Elle se complète au fil des exécutions :
   un saut vers du code non cartographié arrête le programme avec l'adresse,
   et `--why 0xADDR` explique les décisions prises pour elle (et celles que
   prend la fonction qui commence là). Deux familles de fausses fonctions
   sont écartées : les **fantômes** (une amorce venue d'ailleurs, dans un
   emplacement partagé, sur laquelle une fonction de l'overlay tombe par
   simple chute ou qui est au milieu de son code : l'appel visait un autre
   overlay) et les **morceaux** (cibles de `bl` employés comme branchements
   longs à l'intérieur d'une grande fonction Thumb : hors de portée d'un
   `b`, sans autre appelant, sans prologue, et dont le code suppose le cadre
   déjà ouvert — lecture de r4-r7 avant écriture, pile lointaine, `pop` sans
   `push` —, ou atteints par le flot, ou qui reviennent dans le corps de
   l'appelant, ou qui partagent ses pools). Les morceaux sont absorbés dans
   leur fonction et exportés (`cibles_longues`) pour que le traducteur fasse
   un `goto` du `bl` ; traduits en appels, ils dépilaient le cadre de
   l'appelant puis « revenaient » dans son code avec les registres d'un autre.
2. **Traducteur** (`tools/recomp/`) — ✅ décodeurs ARM et Thumb (validés contre
   capstone), sémantique exacte, génération C ; 0 écart contre unicorn sur
   plus de 10 000 blocs. Un `bl` Thumb vers une cible de la même fonction que
   la cartographie a reconnue comme morceau (`cibles_longues`), ou à plus de
   2 Ko dont la cible est atteinte par chute, est un branchement long de
   CodeWarrior : émis en `goto`, pas en appel.
3. **Couche d'exécution** (`runtime/`) — ✅ première version : voir ci-dessous.
4. **Premier démarrage** — ✅ le crt0 recompilé décompresse l'ARM9, `OS_Init`
   passe (PXI, threads, tactile), `NitroMain` tourne, les overlays se chargent
   par lectures de carte, la sauvegarde est identifiée et lue (composant carte
   de l'ARM7 simulé), le son est acquitté, les logos Nintendo et Q-Games
   défilent avec leurs fondus, l'écran-titre s'affiche sur les deux écrans
   (3D capturée et alternée à chaque image), et après A puis un toucher le
   **menu principal** apparaît (Mode 1 joueur, Téléchargement DS, Mode Wi-Fi,
   Options ; l'équipe sur l'écran du bas), stable sur 2 400 images sans un
   accès hors carte (`SFC_INPUT_REPLAY=build/replay_title.txt`). Un toucher
   sur « Mode 1 joueur » lance l'**intro** d'une nouvelle partie (panneaux
   Andross, Star Fox, Pepper, Krystal…, texte qui s'écrit), qui se joue en
   entier, puis la **carte de stratégie** apparaît (système de Lylat en haut,
   passerelle avec ROB et Fox en bas, dialogue du tutoriel), 15 000 images
   sans accès hors carte ; avec les touchers du tutoriel (flèche, « Pass. »,
   « 2 Commencer ! »), le dialogue se déroule jusqu'à la **phase de
   stratégie** : la carte de Corneria sur l'écran tactile, le vaisseau de
   Fox, les groupes ennemis, le compte à rebours et les tours restants
   (run 138, 40 000 images sans un accès hors carte). La sauvegarde
   fonctionne : au lancement d'une partie, le jeu propose de reprendre. En
   traçant une route au stylet puis en lançant le tour, les vaisseaux se
   déplacent, le contact avec un ennemi ouvre le menu de combat, et le
   **combat aérien se joue** : vue 3D avec ciel, relief et vaisseaux, radar
   tactile, tir et pilotage ; à la fin du temps le jeu revient à la carte et
   décompte un chasseur. 20 000 images de combat sans un accès hors carte ni
   un déséquilibre de pile. ⬜ vérification longue durée, deuxième planète.

   **La sauvegarde tient d'une partie à l'autre**, validée en jouant. Elle ne
   tenait pas à cause du bit 6 d'AUXSPICNT : il annonce ce qu'il adviendra à la
   fin du transfert *suivant*, et le pilote du SDK l'abaisse avant d'écrire le
   dernier octet d'une transaction. La puce refermait la transaction dès ce
   signal ; l'octet suivant repartait comme une commande et se perdait. Un octet
   manquait ainsi à la fin de chaque lecture comme de chaque écriture : la
   sauvegarde se retrouvait trouée d'un octet par page, et son en-tête de quatre
   octets à 0x09834 — celui que le jeu relit pour reconnaître une partie —
   n'était jamais écrit. Une sauvegarde saine occupe 908 octets d'un seul tenant
   de 0x09834 à 0x09BBF ; une trouée en occupait 254. Le fichier vit dans
   `sauvegarde/starfox_command.sav`, hors du répertoire de compilation qu'on
   efface, et `SFC_SAVE` ne sert qu'aux essais.

   **Les modèles des ennemis se reconstruisent correctement.** Le déplacement
   relatif de VTX_DIFF s'ajoute tel quel aux coordonnées 4.12, sans mise à
   l'échelle. Le facteur se mesure plutôt qu'il ne se devine : dans un maillage
   les faces voisines partagent leurs arêtes, donc un sommet posé en relatif doit
   retomber exactement sur un sommet donné en absolu ailleurs dans le même
   modèle. Sur les 411 modèles du jeu qui emploient ce format, 76 % le font sans
   décalage, contre 3,6 % décalé d'un cran et 0 % divisé par huit. Ces 411
   modèles comptent les chasseurs ennemis et aucun Arwing du joueur : le défaut
   ne se voyait que sur les ennemis.
4 bis. **Son** — ✅ le moteur audio du SDK tourne sur le coprocesseur ARM7,
   et c'est son propre binaire, traduit comme celui de l'ARM9, qui l'exécute.
   `SFC_ARM7=1` le met aux commandes à la place de `runtime/arm7_hle.c`, qui
   acquittait ses messages sans produire de son ; la construction se fait avec
   `-DSFC_ARM7=ON`.

   Sa cartographie : son binaire tient en quelques centaines d'octets à
   l'adresse de chargement, et son crt0 recopie tout le reste ailleurs — la
   plus grosse part en WRAM, d'où tournent son gestionnaire d'interruption et
   son pilote son, le reste en fin de RAM principale. En cartographiant ces
   sections à l'adresse où elles vivent, la couverture passe de 66 % à 96 % et
   le nombre de fonctions de 740 à 1049. Son jeu d'instructions est un ARMv4 :
   sans `blx`, il appelle par registre en écrivant lui-même l'adresse de
   retour (`mov lr, pc` puis `bx rN`) ; la cartographie reconnaît cet idiome.

   Son exécution : il vit sur sa propre fibre, l'ARM9 mène l'horloge et lui
   cède à chaque ligne le temps qu'il vient de consommer ; ses attentes se
   paient sur son temps à lui. Il a sa carte mémoire (ni ITCM ni DTCM, sa WRAM
   de 64 Ko en 0x03800000, la part de WRAM partagée que WRAMCNT lui donne, la
   VRAM que l'ARM9 lui prête), ses registres (`runtime/mmio7.c`), ses
   minuteurs, sa liaison série vers l'écran tactile et le firmware, ses
   interruptions et son vecteur en fin de WRAM. Les registres de la cartouche,
   eux, sont ceux de l'ARM9 : le matériel n'en a qu'un jeu. Ses appels système
   ne portent pas les mêmes numéros, et son BIOS lui donne trois tables dont
   son pilote son ne peut pas se passer — reconstituées, ce BIOS n'étant pas
   dans la cartouche.

   Ses threads : le SDK en fait tourner sur lui comme sur l'ARM9, avec les
   mêmes trois primitives qui manipulent physiquement les registres. Elles
   sont remplacées de la même manière (`runtime/threads7.c`) ; sans cela il
   répondait à la première commande puis se taisait.

   Le mixage (`runtime/sound.c`) : seize canaux, PCM 8 et 16 bits et ADPCM,
   volume et panoramique, une image de son par image d'affichage à la
   fréquence du matériel, remise à la sortie de l'hôte (`runtime/audio.c`).
   `SFC_WAV=fichier` enregistre ce qui sort (`SFC_WAV_FROM=n` pour ne commencer
   qu'à l'image n), `SFC_NO_AUDIO` coupe la sortie.

   **Deux explorations de 120 000 images, deux graines, derrière le
   coprocesseur : pas un accès hors carte, pas un arrêt, pas un déséquilibre
   de pile** — autant que sans lui. Ces
   explorations traversent toute la boucle de jeu, jusqu'au combat aérien à
   3 600 polygones par image ; le jeu y tourne à 156 images par seconde, et le
   coprocesseur ne coûte que 30 % de temps machine. Le mélange ne sature
   presque jamais (47 échantillons sur 6,3 millions en combat).

   Le blocage qui arrêtait les explorations au-delà de 60 000 images venait
   des minuteurs : ils se programment en deux demi-mots, la recharge puis le
   contrôle, et le mot n'était pas gardé dans l'espace des registres — la
   seconde écriture ramenait donc la recharge à zéro. Le pilote son tournait
   vingt fois trop lentement, sa file de commandes débordait, l'ARM9
   l'attendait dans une section critique, sa propre file débordait faute
   d'être lue, et le coprocesseur se bloquait à son tour en tentant d'y
   écrire. Les deux processeurs avaient le même défaut.

   Reste : le mode Téléchargement DS n'affiche pas ses textes (le sans-fil
   n'est pas simulé), et la justesse du mixage n'a pas été comparée à la
   console — le mélange sature par moments.

5. **Symboles** — nommage des fonctions (assertions, registres, appels au
   SDK), séparation SDK / MGL / jeu, structures reconstituées.
6. **Décompilation** des systèmes visés, avec vérification différentielle.

## Comment la couche d'exécution fait tourner le code traduit

- **Une fonction C par fonction du jeu**, avec un `switch` d'entrée sur ses
  blocs : un saut indirect vers le milieu d'une fonction est possible.
- **Appels** : directs entre fonctions connues, sinon `rt_call` (table de
  dispatch, qui reconnaît l'overlay présent en RAM en comparant son image).
  Une pile fantôme des adresses de retour distingue `ldr pc, [sp], #4` (un
  retour) d'un vrai saut. Après chaque appel, une *redirection* permet à une
  primitive native de faire reprendre le code à une autre étiquette.
- **Remplacements natifs** (`rt_hook`) : chaque fonction générée teste un
  pointeur en tête ; c'est ainsi que sont remplacés `OS_SaveContext`,
  `OS_LoadContext` et la commutation de thread du gestionnaire d'IRQ — et
  que le code décompilé remplacera, fonction par fonction, le code traduit.
- **Threads du SDK** : un contexte processeur et une fibre Windows par thread ;
  l'ordonnanceur du SDK reste du code traduit, seule la commutation de
  registres est native.
- **Interruptions et temps** : le temps virtuel (560 190 cycles par image)
  avance aux attentes explicites (CP15, `svc`) et par quanta d'arêtes
  arrière : le code généré décrémente un budget à chaque boucle, et tous les
  256 passages le runtime compte 4 cycles par arête, puis livre les
  interruptions en attente (une interruption qui survient met le budget à
  zéro). Ainsi une boucle qui attend un compteur d'images ou une file
  remplie par une interruption aboutit. Le runtime joue le vecteur IRQ du
  BIOS (pile IRQ, `spsr`, retour d'exception).
- **Matériel** : registres (`mmio.c`), DMA, minuteurs, diviseur et racine,
  VRAM par banques, carte (`card.c`, lecture de la ROM), sauvegarde SPI
  (`backup.c`), BIOS (`bios.c`), moteur géométrique 3D (`gx_geom.c`), moteurs
  2D (`video2d.c`), fenêtre OpenGL et entrées (`video.cpp`).
- **Coprocesseur** : par défaut son rôle est tenu de l'extérieur (`arm7_hle.c` :
  poignée de main IPCSYNC, PXI, tactile, et le composant carte qui exécute pour
  l'ARM9 les accès à la sauvegarde selon le protocole `CARDiCommandArg` du SDK).
  Avec `SFC_ARM7=1`, c'est son binaire traduit qui tourne, sur sa fibre
  (`arm7_run.c`), avec ses registres (`mmio7.c`), ses threads (`threads7.c`),
  ses seize canaux sonores (`sound.c`) et la sortie audio de l'hôte
  (`audio.c`).
- **Diagnostics** : chien de garde avec pile fantôme, journal des threads
  (`SFC_TRACE_THREADS=1`), captures PNG (`SFC_DUMP=n`), assertions du jeu
  décodées (`bkpt`).

## Ce qui ne se versionne pas

Le code C généré depuis la ROM est une traduction du binaire du jeu : il
reste sur la machine de chacun (`generated/`, ignoré par git), comme la ROM
et les assets. Les outils, la couche d'exécution et le code décompilé à la
main sont sous licence MIT.

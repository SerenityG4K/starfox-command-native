# Débloquer la cadence d'affichage

> **Mise à jour du 19 septembre 2026.** Le jeu entier tourne désormais en C natif, et la
> demande s'est précisée : non pas une cadence fixe choisie dans une liste, mais un
> **affichage découplé à fréquence variable** — le jeu pense à son rythme, l'écran reçoit
> autant d'images que la machine en produit (60, 144, 240, ou ce qu'elle peut), et une
> machine lente ne ralentit plus le jeu. Ce qui suit reste valable pour l'essentiel ; trois
> points sont désormais mesurés et non supposés.
>
> **1. Le jeu avance par tour, pas par le temps — mesuré.** Le moteur a bien un pas de temps :
> une globale, `0x020D6E14`, que `0x02083FE8` recharge à 136 (un trentième de seconde en
> 20.12) à chaque tour. Mais deux fonctions seulement la lisent (`0x02084B80`, `0x02084F34` :
> les minuteries et animations scriptées). L'expérience : pas divisé par deux pendant une
> seconde de vol, mémoire comparée au bit près — **60 mots sur un million diffèrent**, tous des
> comptes à rebours. Positions, vitesses, IA, caméra : identiques. Faire penser le jeu plus
> souvent l'accélère donc d'autant, et aucun réglage ne le compense : il faudrait rééchelonner
> chaque vitesse et chaque compteur de milliers de fonctions. **Le découplage se fait à
> l'affichage** : interpoler entre deux états du jeu, à l'instant réel de chaque image.
>
> **2. Le budget, mesuré en vol** (mission, machine de développement, rendu ×1) : un rendu
> coûte 2,0 ms (2D 1,5 — composée en logiciel —, 3D 0,4) ; un tour de jeu coûte 20 ms de
> simulation, un tour toutes les deux images, avec des pointes à 45. À 240 Hz, huit rendus
> (16 ms) et un tour (20 ms) ne tiennent pas dans les 33 ms d'un tour sur un seul fil.
> **L'affichage doit avoir son fil** : le jeu dépose un instantané de ce qu'il y a à montrer à
> chaque balayage vertical, le fil d'affichage — seul à parler à OpenGL — interpole entre les
> deux derniers et présente, à son rythme.
>
> **3. La 3D des deux écrans.** En vol le jeu montre la 3D sur les deux écrans avec un seul
> moteur : il alterne, une image sur deux, et l'autre écran affiche la *capture* de l'image
> précédente, relue depuis OpenGL vers la mémoire vidéo (`display_capture`). C'est la raison
> de ses 30 Hz, et un retour de l'affichage vers la machine simulée que le fil d'affichage
> devra remplacer : chaque écran garde sa propre liste de polygones, et là où le jeu affiche
> une capture on redessine la scène, interpolée.
>
> Étapes revues : (1) l'instantané et le fil d'affichage, image répétée — rien ne doit changer
> à l'œil, ni aux empreintes ; (2) la 3D interpolée par écran, polygones appariés par une
> étiquette posée à la source ; (3) la 2D par ses registres ; (4) les entrées lues à la cadence
> d'affichage ; (5) les garde-fous (coupures, polygones non appariés).

> **Où l'on en est (19 septembre 2026, au soir).** Les étapes 1 et 2 sont faites
> (`SFC_AFFICHAGE=libre`, `SFC_IPS=n|vsync|0`, réglables dans le lanceur : « Images par s. »),
> et trois défauts que seul l'œil voyait — un dédoublement des ennemis, des explosions, des
> portraits des dialogues en vol — ont chacun trouvé leur cause :
>
> - **L'heure.** Les images du jeu n'arrivent pas régulièrement : le tour est long, le balayage
>   qui suit est en retard et le suivant le rattrape (on a mesuré 22 à 46 ms entre deux images
>   3D qui valent chacune le même pas de simulation), et une scène chargée présente ses images un
>   balayage sur deux, puis sur trois, puis sur un. Interpoler d'après l'heure d'arrivée rejouait
>   cette gigue, et à 240 images par seconde un à-coup se voit comme un double. **Le temps se compte
>   désormais en tours** — le numéro de série de l'image 3D — joués à pas égaux : la durée du tour
>   est mesurée sur une fenêtre d'arrivées, et l'heure idéale d'un tour est l'enveloppe des
>   arrivées (la plus tardive), de sorte qu'une image est là quand son heure sonne
>   (`HorlogeJeu`, `runtime/video.cpp`). Le jeu dépose ses instantanés dans une file : l'affichage
>   n'en manque plus aucun.
> - **L'appariement.** Un polygone sur sept restait sans pareil en vol, et sautait donc trente fois
>   par seconde dans une scène qui glissait à 240. La cause n'était pas l'identité des polygones mais
>   le critère de vraisemblance, écrit dans le repère de l'écran. Il l'est maintenant dans celui de
>   la caméra (`runtime/entre_images.cpp`) : on estime, sur les polygones sans équivoque, le
>   déplacement rigide que la caméra impose à tout ce qui ne bouge pas — par projection, moindres
>   carrés pondérés, les mobiles écartés tour après tour — puis on s'en sert deux fois. Pour
>   départager ceux qui se ressemblent (cent tuiles de sol, cinq anneaux : le bon pareil est celui
>   que ce mouvement prédit, ou, pour ce qui est attaché à la caméra, celui qui n'a pas bougé). Et
>   pour ceux qui n'ont vraiment pas de pareil — une particule née à l'instant — : ils suivent la
>   caméra au lieu de sauter avec elle. Une seconde passe apparie par le seul voisinage ce dont le
>   jeu recalcule les sommets ou change la vignette à chaque tour. En vol : 98 à 100 % d'appariés.
> - **Les textures.** L'interface du vol — portraits, texte, jauges — est dessinée par le moteur 3D,
>   et ses vignettes sont réécrites en mémoire vidéo quand un personnage parle. Or le cache de
>   textures décidait « rien n'a changé » d'après le compteur d'écritures de la machine *en direct*,
>   alors que l'affichage libre dessine un instantané vieux d'un tour ou deux : il gardait la
>   vignette d'avant, ou prenait celle d'après. Chaque instantané porte maintenant la version de
>   ses textures (partagées entre instantanés tant que le jeu n'écrit rien : 640 Ko de copie en
>   moins par balayage), et ce qui s'efface a son propre flux dans le cache.
>
> **L'épreuve** (`SFC_AFFICHAGE_ESSAI=n`, avec `SFC_DUMP_FROM=image`) : hors du temps, neuf images
> 3D consécutives, n sous-images entre chacune, et la mesure de ce que l'image change d'un pas au
> suivant. Un mouvement continu change d'autant à chaque pas ; un saut à mi-chemin ou aux images
> du jeu fait un pic. Sur la mission d'essai : 3,03 par pas, 3,03 au mi-chemin, 3,26 aux images du
> jeu (les textures animées du jeu, elles, changent bien une fois par tour). Le jeu reste identique
> aux empreintes : l'affichage ne touche à rien de ce qu'il lit.
>
> **Le sommeil de Windows (même jour, plus tard).** Le joueur voyait toujours double. Le « film »
> (`SFC_AFFICHAGE_FILM=n` : les n images affichées à partir de l'image `SFC_DUMP_FROM`, toutes, à leur
> rythme, avec l'heure et le tour du jeu que chacune montre) l'a montré en une mesure : les images ne
> sortaient pas toutes les 4,2 ms mais par paquets — quatre en 4 ms, puis un trou de 12. Le plafond
> d'images dormait par `sleep_for(1 ms)`, qui sous Windows dure jusqu'au prochain tic du minuteur du
> système, quinze millisecondes ; l'affichage sautait trois échéances et les rattrapait d'un coup.
> Soixante images utiles par seconde, irrégulières, au lieu de deux cent quarante. `timeBeginPeriod`
> n'y change rien sous Windows 11 pour une fenêtre qu'il juge masquée : les deux cadencements — celui du
> jeu, qui avait le même défaut et rendait ses balayages en retard puis les rattrapait, et celui de
> l'affichage — dorment désormais par un minuteur à haute résolution (`runtime/attente.c`), et un retard
> de plus d'une image n'est plus rattrapé. Mesuré : 4,2 ms entre deux images, de 2,9 à 5,5.
>
> **Les explosions en morceaux.** Une flamme est faite de deux moitiés, deux polygones qui se touchent.
> Appariées chacune pour soi, l'une partait vers la flamme voisine. L'appariement a maintenant trois
> niveaux : les objets entiers d'abord (une suite de polygones placés par la même matrice, qui soumet
> les mêmes formes dans le même ordre : il va d'un bloc, un ennemi ne se déchire plus) ; puis les
> polygones un à un, par leur forme — l'empreinte sans la texture, car une vignette qui change ne fait
> pas un autre carré ; puis, pour ce qui n'a pas de forme stable, des pièces de polygones qui se suivent
> et se touchent, appariées par le voisinage, ensemble ou pas du tout.
>
> **Trente images par seconde au début d'une mission.** Du menu au vol le tour passe de 17 à 33 ms sans
> pause. L'heure ramenait ses anciens relevés au tour courant avec la nouvelle durée, les projetait une
> demi-seconde dans le futur, prenait quinze tours de retard, et l'affichage restait collé à la plus
> vieille image qu'il gardait. La durée du tour se mesure maintenant en deux parts robustes (balayages
> par tour, d'après les numéros d'image ; durée du balayage, trous écartés), un changement de rythme
> fait repartir la mesure, l'enveloppe est bornée à un tour, et l'affichage ne traîne jamais plus d'un
> tour derrière ce qu'il pourrait montrer. Le relevé de `SFC_PROFILE` donne le retard moyen (20 à 28 ms
> en vol) et le nombre de recalages.
>
> Pour chercher un défaut que seul l'œil voit : le film, comparé image par image aux captures du mode
> normal (`SFC_DUMP=1`) de la même image du jeu — une zone qui ne ressemble à aucune image voisine est
> un défaut de l'affichage. Sur la mission d'essai, portraits et texte des dialogues : aucun écart.
>
> **Les yeux qui clignent, la bouche qui parle.** Sur la partie enregistrée du joueur, le film de la scène
> de dialogue (Fox, Krystal, Falco) montrait la visière de Krystal dédoublée au moment où elle clignait.
> Les portraits des scènes de discussion sont en 3D ; le jeu les anime en faisant glisser la texture d'un
> coup, d'une vignette à la suivante de sa planche, par la matrice de texture. L'interpolation faisait
> défiler la planche sous le carré. Ne s'interpolent plus que les textures qui glissent vraiment — trois
> texels au plus par tour — ; le reste change d'un coup, comme dans le jeu.
>
> **L'interface au pixel.** La flèche « suite » des boîtes de dialogue saute d'un pixel contre son ombre ;
> montrée à mi-chemin, l'ombre dépassait en un liseré noir. Pour ce que dessine une projection à plat, le
> chemin parcouru depuis l'image du jeu la plus proche est arrondi à des pixels entiers de la console : un
> saut reste un saut, un glissement va de pixel en pixel, et ce qui ne bouge pas reste où le jeu l'a mis.
> (Arrondir la position elle-même décalait d'un demi-pixel les étiquettes de la carte, que le jeu place entre
> deux pixels.)
>
> Sur la scène où le joueur voyait les glitchs, 400 images affichées comparées au mode normal : aucune
> sans équivalent, sur les deux écrans. `SFC_RAPIDE_JUSQUA=image` amène un rejeu à la scène voulue sans
> attendre, et le traverser d'une explosion (écran jaune puis blanc) n'est pas un défaut.
>
> **Le retard des commandes, et la prédiction.** Se placer entre deux images du jeu, c'est attendre la
> suivante : l'affichage montrait le jeu avec un tour de retard, 27 à 33 ms en vol, et le joueur le sentait.
> `SFC_LATENCE` règle la part de ce tour qu'on garde ; par défaut 0 : la plus récente est montrée dès
> qu'elle arrive, et son mouvement prolongé jusqu'à la suivante. Mesurée hors du temps
> (`SFC_AFFICHAGE_ESSAI_PREDICTION=1` : l'image prédite contre celle que l'interpolation montrerait un tour
> plus tard), la prédiction s'écarte de 2,1 en vol contre 5,8 à 7,3 pour l'image tenue, de 2,1 contre 2,4 en
> combat. Trois exceptions, chacune née d'une mesure : ce qui change vite de taille (une explosion qui gonfle
> puis s'arrête) ne prolonge que son déplacement ; ce qui n'est apparié que par le voisinage n'est pas
> prolongé ; l'interface à plat, qui avance par sauts de pixel, reste où le jeu l'a mise. En temps réel,
> l'affichage est maintenant de 4 à 14 ms *en avance* sur la dernière image du jeu, au lieu de 27 à 33 ms
> en retard.
>
> **Selon la scène.** Le joueur a préféré l'interpolation dans les menus, le mode stratégie et sous le stylet :
> l'interface y est surtout à plat, et la prédiction la laisse à la cadence du jeu. Là, le jeu pense soixante
> fois par seconde et un tour de retard ne coûte que seize millisecondes. La latence suit donc la scène par
> défaut : 1 quand le tour dure moins de 22 ms, 0 au-delà de 28 (le vol), et elle passe de l'une à l'autre en
> un tiers de seconde. Le lanceur la propose (« Réaction » : selon la scène, fluide, immédiate).
>
> **Le jeu calcule son tour suivant pendant qu'il attend (les tours d'aperçu).** La question posée était de
> synchroniser le comportement du jeu, et non l'image seule, sur la cadence choisie. Le jeu pense à pas fixe et
> ne peut pas penser plus souvent sans s'accélérer ; mais en vol il attend vingt millisecondes à chaque tour,
> et cette attente est du temps de machine perdu. On y rejoue donc la suite de sa boucle (0x0203DAE8) : C,
> l'échange, puis les méthodes 2, 1 et 0 de l'application et sa méthode 3, sans l'attente du balayage
> (0x0202C760, où le jeu dort). Ce tour-là est calculé **avec les commandes de l'instant**, son image est prise
> quand il arrive à son tour à l'attente, puis tout est rendu tel quel : mémoire page par page (le mécanisme
> du banc), registres et banques, moteur 3D et ses trois tampons, liaison entre processeurs, minuteries,
> registres matériels, correspondance des banques vidéo. Le vrai tour, lui, se joue ensuite comme sur la
> console.
>
> Garde-fous : un aperçu qui changerait de thread, attendrait une interruption, toucherait à la cartouche ou
> tournerait sans fin est abandonné et effacé de même ; aucune interruption ne lui est livrée, rien ne part
> vers le coprocesseur, rien n'est publié vers l'affichage, et ses sauts égarés n'entrent pas dans
> `analysis/amorces.txt`. Sur la mission d'essai, 2 900 aperçus pris, 63 abandons ; empreintes du jeu
> identiques (3 300 images) et son identique à l'octet près.
>
> L'affichage s'en sert comme du tour à venir : il glisse vers lui au lieu de l'attendre, et la latence tombe
> d'elle-même à zéro (`latence 0.00` dans les relevés) sans aucune extrapolation. Refait plusieurs fois dans
> le tour, l'aperçu tient compte des commandes les plus fraîches ; le nombre d'aperçus par tour suit la cadence
> demandée et le coût mesuré (environ une milliseconde), donc la puissance de la machine.
>
> Ce que l'aperçu **ne** fait **pas** : il ne mélange pas les règles du jeu. Tirs, apparitions, hasard et IA
> restent ceux des vrais tours, à trente par seconde. Le générateur marque bien les écritures de la forme
> x = x + v (`ECRIT_ACC*`, `tools/accumulateurs.py`) pour n'avancer un aperçu que d'une fraction de tour, mais
> la mesure a montré que les positions du jeu ne passent pas par elles : un aperçu calcule donc le tour entier,
> et c'est l'affichage qui se place entre les deux.
>
> Reste : la 2D par ses registres (étape 3), et décider quand ce mode devient celui par défaut.

Objectif : afficher à 120, 144, 165 ou 240 Hz. Pas de cadence libre — on choisit
dans une liste fermée et on s'y tient.

Ce document énumère les contraintes avant d'écrire la moindre ligne, parce que la
plupart d'entre elles se découvrent trop tard autrement.

## 1. Ce qui ne bougera pas : la logique, à 60 Hz dans les menus et **30 Hz en vol**

Le jeu est à pas fixe. Ce n'est pas un moteur à delta-temps qu'on accélérerait en
lui passant un intervalle plus court — et cela se mesure, voir plus bas.

Sa boucle principale (`0x020A0E4C`, décompilée dans `runtime/natif.c`) fait vivre
la scène courante puis, si l'octet `+0x1A3` de son objet est levé, **attend que
vingt millisecondes se soient écoulées** en relisant l'horloge. Dans les menus
l'octet est nul : un tour par image, 59,83 Hz. **En vol le jeu le lève lui-même** —
la console n'aurait pas tenu soixante — et vingt millisecondes dépassant les seize
d'une image, il ne fait plus qu'un tour toutes les deux images. Mesuré sur le banc
de mission : 973 tours pour 2 000 images.

L'expérience qui tranche (`SFC_PAS_MS=10`, qui abaisse le seuil) : les minuteries
du jeu, qui avancent de 136 par tour — un trentième de seconde en virgule fixe —
avancent alors de 8 160 par seconde au lieu de 4 080. **Le jeu va exactement deux
fois plus vite.** Le pas est une constante compilée, pas une mesure ; laisser le
jeu penser plus souvent l'accélère, rien d'autre.

Et ce portage est une traduction fidèle du binaire : on ne réécrit pas la boucle
du jeu. Accélérer l'horloge de la machine simulée ferait tourner le jeu en
accéléré, son compris.

**Donc toute cadence au-delà de 60 Hz est une cadence d'affichage.** Le jeu
continue de penser soixante fois par seconde ; c'est l'image qu'on produit plus
souvent, en interpolant entre deux états successifs. Rien ne change quant au
comportement du jeu : ni la difficulté, ni la vitesse, ni la granularité de la
commande.

## 2. Les cadences visées ne tombent pas juste

| cadence | images par trame logique, menus (59,83 Hz) | en vol (29,91 Hz) |
|---|---|---|
| 120 Hz | 2,0058 | 4,0116 |
| 144 Hz | 2,4070 | 4,8140 |
| 165 Hz | 2,7580 | 5,5160 |
| 240 Hz | 4,0116 | 8,0232 |

En vol il faut donc fabriquer de **trois à sept images** entre deux états du jeu,
et non une à trois : c'est là que l'interpolation sera la plus visible, en bien
comme en mal. En contrepartie, même à 60 Hz d'affichage, le combat y gagnerait —
il est aujourd'hui affiché à trente.

Aucune n'est entière. Même 120 et 240, si proches de 2 et 4, dérivent d'une image
toutes les cent soixante-dix trames environ — un hoquet visible toutes les trois
secondes si on se contente de compter.

Il faut donc un facteur d'interpolation continu, tiré de l'horloge et non d'un
compteur : à chaque image, on calcule où l'on se trouve entre la trame logique
précédente et la suivante, et l'on interpole à cette fraction.

## 3. Une trame de latence, et c'est un choix

Interpoler entre les trames N et N+1 suppose de connaître N+1. On affiche donc
avec une trame de retard : **+16,7 ms dans les menus, +33,4 ms en vol**, puisque
la trame logique y dure deux images.

L'autre voie serait d'extrapoler à partir de N-1 et N, sans latence, mais elle
tremble à chaque changement de direction, à chaque collision et à chaque
explosion — précisément les moments où l'œil regarde.

Décision : interpolation. À réexaminer si la latence se sent au stylet.

## 4. Apparier les objets d'une trame à l'autre

C'est le point dur, et celui qui décide de tout le reste.

Le moteur 3D du DS ne connaît pas d'objets : il reçoit un flux de commandes et
produit une liste de polygones. Pour interpoler, il faut savoir quel polygone de
la trame N est le même que tel polygone de la trame N+1.

Apparier par la géométrie (rang dans la liste, mêmes attributs, déplacement sous
un seuil) est fragile : le jeu trie ses objets par profondeur, et l'ordre change.

Meilleure piste : **étiqueter chaque polygone à la source**. Le jeu charge la
matrice d'un objet (`MTX_RESTORE` avec son index, ou une pile) avant de le
dessiner. En notant, dans `gx_geom.c`, l'index de matrice et le rang de l'objet
au moment où le polygone naît, on obtient une identité bien plus solide qu'une
ressemblance constatée après coup.

Un polygone non apparié n'est pas interpolé : il garde sa position de la trame la
plus récente. Ce qui apparaît ou disparaît ne doit surtout pas être mélangé.

**À vérifier en premier**, avant tout le reste : sur des captures réelles de
combat, ces étiquettes sont-elles stables d'une trame à l'autre ? Si elles ne le
sont pas, l'interpolation 3D est à repenser entièrement.

## 5. Le 2D s'interpole par ses registres, pas par ses pixels

Les fonds et les sprites sont composés par ligne depuis la VRAM. Interpoler
l'image finie floute le texte et les bords — inacceptable pour un jeu dont la
moitié de l'écran est une carte et un menu.

Il faut interpoler les **entrées** du rendu 2D, puis rendre normalement :

- défilement des fonds (`BGxHOFS`, `BGxVOFS`) ;
- matrices des fonds en rotation et mise à l'échelle (`BGxPA`–`PD`, `BGxX/Y`) ;
- positions des sprites dans l'OAM.

Chaque image intermédiaire est alors une vraie image, nette. C'est aussi la part
la plus coûteuse : un rendu 2D complet, deux moteurs, par image affichée.

## 6. Les effets par ligne de balayage ne s'interpolent pas

Certains effets réécrivent des registres à chaque HBlank : dégradés, fenêtres,
tremblements, fondus. Le rendu les capte au fil du balayage de la trame logique ;
une image intermédiaire n'a pas de balayage à elle.

On réutilisera les valeurs par ligne de la trame la plus proche, en n'interpolant
que les registres globaux. À éprouver sur les écrans qui en usent — transitions
et fondus surtout.

## 7. Le son ne doit rien sentir

Le coprocesseur ARM7 mixe au fil du balayage (`snd_mix_cycles`, appelé par ligne)
et la file de sortie se régule sur `snd_mix_frame`, une fois par trame logique.
La cadence d'affichage ne doit toucher ni l'un ni l'autre : la moindre influence
changerait la hauteur des notes.

Critère de recette : l'enregistrement d'une même séquence doit être identique à
60 et à 240 Hz.

## 8. La synchronisation à l'écran

Deux modes :

- **synchronisé** : intervalle d'échange à 1, la cadence choisie doit alors valoir
  celle du moniteur ;
- **libre** : attente au pas choisi, comme `pace_frame` aujourd'hui.

Si la cadence demandée ne correspond pas à celle de l'écran, on le dit clairement
au démarrage : une image sur deux sera doublée ou perdue, et aucune finesse
d'interpolation n'y changera rien.

## 9. Le budget de temps, à mesurer avant de promettre

Aujourd'hui : 156 images par seconde en combat aérien (3 618 polygones), soit
6,4 ms par trame, coprocesseur compris — il en prend 30 %.

À 240 Hz il faut trois rendus de plus par trame. Si un rendu complet (2D des deux
moteurs plus 3D) dépasse 3 ms, 240 Hz est hors de portée sans optimisation.

**Cette mesure conditionne la liste des cadences réellement offertes.** Palier de
repli, si le 2D coûte trop cher : le rendre à 60 Hz et n'interpoler que la 3D — le
HUD saccade, la scène est fluide.

## 10. Ce qui ne doit jamais être interpolé

Les coupures : changement d'écran, fondu, chargement d'overlay, entrée dans un
menu. Un fondu interpolé donne une image fantôme, bien plus laide qu'une image
répétée.

Garde-fou : si trop de polygones restent non appariés, ou si la matrice de
projection saute, on n'interpole pas cette trame et on répète l'image.

## 11. La latence d'entrée, elle, se gagne pour de bon

Indépendamment du reste : échantillonner les touches et l'écran tactile à la
cadence d'affichage, et remettre la valeur la plus fraîche à la trame logique
suivante. Le jeu lit toujours une fois par trame, mais il lit quelque chose de
plus récent. C'est un gain réel, sans contrepartie, et qui ne dépend d'aucune
interpolation.

## 12. Les essais doivent continuer de valoir

Le rejeu d'entrées, le banc différentiel et les explorations tournent sur la trame
logique. **Aucune donnée d'affichage ne doit revenir dans la machine simulée** :
c'est la condition pour qu'une partie rejouée à 240 Hz soit exactement la même
qu'à 60. À garantir par construction, puis à vérifier par une empreinte.

## Étapes

0. **Mesurer.** Coût d'un rendu seul ; stabilité de l'étiquetage des polygones
   d'une trame à l'autre. Ces deux mesures décident de la faisabilité de 240 Hz et
   de la méthode d'appariement. Rien ne s'écrit avant.
1. **Découpler.** Sortir du VBlank tout ce qui relève de l'affichage, introduire
   une horloge d'affichage et le choix de cadence. À ce stade l'image est
   simplement répétée : aucun gain visuel, mais l'ossature est en place, le coût
   est connu et l'absence de régression est vérifiable.
2. **Interpoler le 2D** par ses registres. Gain immédiat sur la carte tactile, le
   HUD et les fonds défilants.
3. **Interpoler la 3D** : étiquetage à la source, appariement, interpolation des
   sommets en espace de découpage et des couleurs.
4. **Entrées** à la cadence d'affichage.
5. **Garde-fous** : seuils, détection de coupure, extinction automatique dans les
   menus.

## Ce qui dira que c'est réussi

- Un panoramique en combat aérien, à 120 Hz, sans un tressautement.
- Le texte des menus aussi net qu'à 60 Hz.
- Le son identique, empreinte à l'appui.
- Une partie rejouée identique à 60 et à 240 Hz.
- Aucun arrêt sur une exploration longue à chaque cadence offerte.

## Où l'on en est (20 septembre 2026)

Le plan ci-dessus visait à *interpoler* entre deux images du jeu. Les tours d'aperçu font mieux :
ils *calculent*. Pendant que le jeu attend — l'attente du tour en vol, le sommeil du fil principal
ailleurs —, la suite de sa boucle est rejouée avec les commandes du moment, chaque accumulateur
n'avançant que de la part de tour écoulée ; l'image obtenue, 3D **et** 2D, est celle du jeu à cet
instant. Puis tout revient tel quel : mémoire page par page, registres, moteur 3D, liaison,
threads. Le jeu, lui, joue son vrai tour comme sur la console.

Ce qui reste interpolé : les images pour lesquelles la machine n'a pas eu le temps de jouer un
aperçu. C'est là qu'est l'adaptatif — le planificateur mesure ce que coûte un aperçu et les espace
de `max(1/ips, 1,3 × coût)`. Sur une machine rapide, presque tout est calculé ; sur une machine
lente, l'affichage glisse entre les tours comme avant, et le jeu ne cale jamais.

Mesuré sur la machine de développement, en vol, cadence demandée 240 : 212 images affichées par
seconde, 60 tours de jeu, 72 tours d'aperçu — un aperçu coûtant 4,4 ms, ses deux écrans 2D compris.

Ce qui le prouve : la partie du joueur rejouée entière, 120 000 images, 118 785 tours d'aperçu
joués puis effacés — empreintes identiques à celles d'un jeu qui n'en joue aucun ; son identique
octet par octet ; et `SFC_APERCU_VERIF=1`, qui compare la mémoire entière et les registres après
chaque aperçu et arrête la partie au premier octet qui a bougé.

## L'interpolation sur le GPU (26 septembre 2026)

Entre deux images du jeu (vraies ou d'aperçu), chaque image affichée était calculée par le
processeur : `interpoler` (`runtime/entre_images.cpp`) rendait la liste des polygones de l'instant,
`draw_polygons` en cherchait les textures une à une et envoyait un demi-mégaoctet de sommets. Le
calcul est maintenant fait par le GPU. Une fois par paire d'images, `planifier` dresse le plan du
chemin : pour chaque rang de la liste d'`interpoler`, les deux bouts, les décisions qui ne dépendent
que des deux images (voisinage, forme raide, taille qui change vite, texture qui défile, sens des
faces) et, quand la forme montrée à un instant ne porte pas l'état — texture, attributs — du
polygone de B, un second polygone qui ne se dessine que ces instants-là. Le plan part au GPU, en
lots de même état ; à chaque image affichée, un nuanceur de géométrie reçoit chaque polygone entier
(quatre sommets en « lignes adjacentes ») et refait `interpoler` à l'instant voulu — mi-chemin,
prédiction, retournement, calage au pixel de l'interface —, avec la même arithmétique entière : le
produit de 64 bits d'`entre` est pris par morceaux de 32. `SFC_INTERPOLATION=cpu` revient au
processeur.

Contre le processeur, sur 32 sous-images en vol et 21 images prédites, puis à six endroits de la
partie (menus, vol, scènes de 1 000 à 1 500 polygones) : identique, à 0 – 5 points de bord près —
ceux des trajets tout tracés (particules sans pareil), que le processeur calcule en double
précision. À 165 images par seconde, le fil d'affichage prend 17 à 19 % d'un cœur au lieu de 18,5 à
20,6 : il ne reste à chaque image que la présentation et, à chaque nouvelle image du jeu,
l'appariement (`apparier`, sept points) et le plan. Débridé, l'affichage tient 2 000 à 2 650 images
par seconde selon que le pilote répartit ou non son travail sur un second fil, et le GPU y est
occupé à 55 % (35 % quand le processeur interpolait) : c'est la présentation des images, non plus
leur calcul, qui borne.

Chaque constructeur — NVIDIA, AMD, Intel — a son compilateur de nuanceurs, et ce qu'il fait de
l'arithmétique entière ne se voit qu'en le lui demandant. Au démarrage, le jeu fait donc calculer au
GPU des polygones d'épreuve qui passent par tous les chemins du nuanceur (pareil ordinaire,
coordonnées géantes, interface à plat, forme raide, taille qui change vite, voisinage, changé de
forme, sans pareil, disparus, retournement, deux états, polygone invisible, translucide, sommet qui
passe derrière la caméra), à neuf instants, B vrai et aperçu ; il relit le résultat (*transform
feedback*) et le compare sommet par sommet à `interpoler`. Au moindre écart — ou s'il manque au pilote
une fonction d'OpenGL 3.0, ou que le nuanceur ne se lie pas —, l'interpolation reste au processeur, et
le journal dit pourquoi. Il dit aussi, dès la première ligne, quel pilote et quelle carte le jeu a
trouvés.

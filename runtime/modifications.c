/*
 * Les modifications du jeu d'origine. Chacune est éteinte par défaut, et le jeu est alors
 * exactement celui de la cartouche.
 *
 * Une modification ne réécrit pas de fonction : elle pose un crochet à un **point de
 * modification**, juste avant une instruction du code d'origine. Le générateur de réécritures
 * (tools/decomp_feuilles.py) lit la liste ci-dessous et place à cet endroit du code émis un appel
 * MODIF(c, rang) ; l'état du processeur y est écrit avant et relu après, comme autour d'un appel
 * de fonction. Le crochet lit et change registres et mémoire comme le ferait le code du jeu. Tant
 * qu'il n'est pas posé, la fonction fait ce qu'elle faisait, et le banc la juge contre la
 * traduction comme toutes les autres.
 *
 * Ajouter un point : une ligne P(...) ici, puis `python tools/decomp_feuilles.py` et recompiler.
 * Le générateur dit les points qu'il n'a pas pu poser (adresse qui n'est pas une instruction
 * d'une fonction réécrite).
 *
 * Seul le jeu en natif — le défaut — connaît les modifications : la traduction pure
 * (SFC_NATIF=non) reste le jeu d'origine, référence du banc.
 *
 * Sous licence MIT.
 */
#include "natif.h"
#include "mods.h"
#include <stdlib.h>
#include <string.h>

/* rang, région, adresse de l'instruction avant laquelle le crochet est appelé, crochet, ce qu'il y trouve */
#define LES_POINTS(P) \
    P(0, "arm9", 0x02066A8E, portee_coupure, \
      "caméra, 0x02066A34 : la distance au-delà de laquelle un objet n'est plus dessiné, 4 000,0 en dur (r1)") \
    P(1, "arm9", 0x02066A46, portee_projection, \
      "caméra, 0x02066A34 : le plan lointain passé au calcul de la projection, champ +0x180 (r0)") \
    P(2, "ovl0", 0x0218DACA, portee_fond_hauteur, \
      "arrière-plan, 0x0218DA9C : la hauteur où le modèle est posé (r6) ; r0 désigne la position de la caméra") \
    P(3, "ovl0", 0x0218DB4A, portee_fond_echelle, \
      "arrière-plan, 0x0218DA9C : la première rotation du modèle, en sp+0x38, avant d'être envoyée") \
    P(4, "ovl2", 0x021E0E82, portee_tir_duree, \
      "tir, 0x021E0DB4 : la durée de vie tirée de la fiche du tir (+0x1C), avant d'être rangée en +0x48 (r0)") \
    P(5, "ovl2", 0x021DF980, portee_laser_duree, \
      "laser d'Arwing, 0x021DF7A4 : la durée de vie en solo, 1,1 s en dur (r0), comparée à l'âge du laser") \
    P(6, "ovl5", 0x02235D36, portee_tir_ennemi, "ennemi, 0x02235B98 : tire si la cible est à moins de 8 000 (r0)") \
    P(7, "ovl6", 0x02235246, portee_tir_ennemi, "ennemi, 0x02235170 : tire si la cible est à moins de 1 500 (r0)") \
    P(8, "ovl6", 0x0224ABDE, portee_tir_ennemi, "ennemi, 0x0224AB90 : tire si la cible est à 3 000 au plus (r0)") \
    P(9, "ovl7", 0x02249192, portee_tir_ennemi, \
      "chasseur ennemi, 0x02249124 : au-delà de 2 500 (r0), revient à l'état « Default » sans tirer") \
    P(10, "ovl8", 0x0223C7D0, portee_tir_ennemi, \
      "ennemi, 0x0223C774 : tire si la cible est à moins de 3 000 (r0) et devant lui") \
    P(11, "ovl8", 0x0224FFCE, portee_tir_ennemi, "ennemi, 0x0224FF80 : tire si la cible est à 3 000 au plus (r0)") \
    P(12, "ovl9", 0x022273F4, portee_tir_ennemi, "ennemi, 0x02227170 : tire si la cible est à 5 000 au plus (r0)") \
    P(13, "ovl9", 0x0222C57C, portee_tir_ennemi, \
      "ennemi, 0x0222C2D0 : tire entre 1 000 et 5 500 ; la borne haute (r0)") \
    P(14, "ovl11", 0x0223BA7A, portee_tir_ennemi, "ennemi, 0x0223B824 : tire si la cible est à 5 000 au plus (r0)") \
    P(15, "ovl16", 0x02251178, portee_tir_ennemi, "ennemi, 0x02251128 : tire si la cible est à 3 000 au plus (r0)") \
    P(16, "ovl17", 0x0222F2B0, portee_tir_ennemi, \
      "ennemi, 0x0222F254 : tire si la cible est à moins de 3 000 (r0) et devant lui") \
    P(17, "ovl17", 0x022449EA, portee_tir_ennemi, \
      "chasseur ennemi, 0x0224497C : au-delà de 2 500 (r0), revient à l'état « Default » sans tirer") \
    P(18, "ovl18", 0x0222FB82, portee_tir_ennemi, "ennemi, 0x0222FA3C : agit entre 300 et 2 500 ; la borne haute (r0)") \
    P(19, "ovl18", 0x0222FC46, portee_tir_ennemi, "ennemi, 0x0222FA3C : agit entre 500 et 3 000 ; la borne haute (r0)") \
    P(20, "ovl19", 0x0223E6B8, portee_tir_ennemi, "ennemi, 0x0223E62C : agit si la cible est à moins de 2 500 (r0)") \
    P(21, "ovl19", 0x0223E902, portee_tir_ennemi, "ennemi, 0x0223E62C : tire si la cible est à moins de 2 300 (r0)") \
    P(22, "ovl20", 0x02235CFA, portee_tir_ennemi, \
      "ennemi, 0x02235C74 : à moins de 2 100 (r0), lève le drapeau qui permet de tirer (+0x28)") \
    P(23, "ovl2", 0x0221B5B8, portee_laser_duree, \
      "laser Plasma, 0x0221B47C : la durée de vie en solo, 1,8 s en dur (r0), comparée à l'âge du laser") \
    P(24, "ovl2", 0x0221B734, portee_plasma_fondu, \
      "laser Plasma, 0x0221B720 : l'âge où il commence à rétrécir avant de disparaître, 1,6 s (r0)") \
    P(25, "ovl2", 0x021E9E84, manette_crochet_manche, \
      "contrôle au stylet, 0x021E9D64 : le stylet tient-il le manche (r0, l'octet 0x02222C6C), l'Arwing obéissant aux commandes") \
    P(26, "ovl2", 0x0220F01E, manette_crochet_commandes, \
      "cockpit, 0x0220EFC4 : les commandes du tour viennent d'être tirées du stylet (r4 : le cockpit), le vol va les lire") \
    P(27, "ovl2", 0x021E9E96, manette_crochet_stylet, \
      "contrôle au stylet, 0x021E9D64 : le stylet est-il posé (r0) ; r4 : le contrôle, son point en +0x10") \
    P(28, "ovl2", 0x021EA30A, manette_crochet_manche, \
      "contrôle au stylet, 0x021E9D64 : l'aide au verrouillage — le stylet tient-il le manche (r0)") \
    P(29, "ovl2", 0x021EA31A, manette_crochet_appui, \
      "contrôle au stylet, 0x021E9D64 : l'aide au verrouillage — le stylet est-il posé (r0)") \
    P(30, "ovl2", 0x021E9F76, manette_crochet_cercle, \
      "contrôle au stylet, 0x021E9D64 : avant la reconnaissance du cercle (0x021EC1C4) ; r4 : le contrôle, sa fenêtre en +0x3C") \
    P(31, "ovl2", 0x021EB016, manette_crochet_rails, \
      "contrôle sur rails, 0x021EAF90 : le stylet est-il posé (r1), l'Arwing obéissant aux commandes ; r4 : le contrôle, son point en +0x10") \
    P(32, "ovl2", 0x021EB02A, manette_crochet_manche, \
      "contrôle sur rails, 0x021EAF90 : au premier tour d'un appui, le stylet tient-il le manche (r0, l'octet 0x02222C6C)") \
    P(33, "ovl2", 0x021EB080, manette_crochet_cercle, \
      "contrôle sur rails, 0x021EAF90 : avant la reconnaissance du cercle (0x021EC1C4) ; r4 : le contrôle, sa fenêtre en +0x3C") \
    P(34, "arm9", 0x0204863C, manette_crochet_ouverture, \
      "plan d'ouverture d'un combat, 0x02046E58 : le stylet est-il posé (r0) — passé 2 s, avec une touche pressée, il le referme") \
    P(35, "ovl2", 0x021FE246, mod_laser_tir, \
      "tir, 0x021FE1E4 : le type de laser lu dans la fiche du pilote (r0), avant la table de sauts ; r4 : le véhicule, son pilote en +0x28") \
    P(36, "ovl2", 0x021FE192, mod_laser_cadence, \
      "cadence du tir, 0x021FE178 : le type de laser (r0) ; le pilote en [sp]") \
    P(37, "ovl2", 0x021FE15A, mod_laser_cadence, \
      "attente de plus du Zapper, 0x021FE140 : le type de laser (r0) ; le pilote en [sp]") \
    P(38, "ovl2", 0x021F7A64, mod_laser_son, \
      "0x021F78C4, branche de Fox : son type de laser (r0), qui choisit un jeu « ArwingDynamicsPlayerFox… »") \
    P(39, "ovl14", 0x0222C1A8, mod_laser_briefing, \
      "briefing, 0x0222BCB4 : le type de laser de Fox (r1), qui choisit l'image de son vaisseau") \
    P(40, "ovl2", 0x021FE838, mod_verrou_vehicule, \
      "véhicule, 0x021FE824 : le type de verrouillage (r0) ; le pilote en [sp]") \
    P(41, "ovl2", 0x021FE8E0, mod_verrou_vehicule, \
      "véhicule, 0x021FE8CC : le type de verrouillage (r0) ; le pilote en [sp]") \
    P(42, "ovl2", 0x021FE984, mod_verrou_vehicule, \
      "véhicule, 0x021FE970 : le type de verrouillage (r0) ; le pilote en [sp]") \
    P(43, "ovl2", 0x0220F814, mod_verrou_cockpit_r4, \
      "cockpit, 0x0220F7FC : le type de verrouillage (r4) ; le pilote en +0x48 de r7") \
    P(44, "ovl2", 0x0220FD0A, mod_verrou_cockpit_r0, \
      "cockpit, 0x0220FCF2 : le type de verrouillage (r0) ; le pilote en +0x50 de r7") \
    P(45, "ovl0", 0x02188FC2, mod_vaisseau_chargement, \
      "création du joueur, 0x02188F1C : la clé du pilote (r1, sur la pile) dont les fichiers du vaisseau vont se charger ; le pilote en sp+0xBC") \
    P(46, "ovl2", 0x021FE796, mod_vaisseau_pieces, \
      "véhicule, 0x021FE78C : le nom du modèle de la carlingue, lu dans la fiche du pilote r1") \
    P(47, "ovl2", 0x021FE73E, mod_vaisseau_pieces, \
      "véhicule : le nom du modèle « réacteur allumé », lu dans la fiche du pilote r1") \
    P(48, "ovl2", 0x021FE6E6, mod_vaisseau_pieces, \
      "véhicule : le nom du modèle « réacteur éteint », lu dans la fiche du pilote r1") \
    P(49, "ovl2", 0x021FE68E, mod_vaisseau_pieces, \
      "véhicule : le nom du modèle de la flamme, lu dans la fiche du pilote r1") \
    P(50, "ovl2", 0x021FE656, mod_vaisseau_pieces, \
      "véhicule : la place d'une traînée, lue dans la fiche du pilote r1") \
    P(51, "ovl2", 0x021FE626, mod_vaisseau_pieces, \
      "véhicule : la place de l'autre traînée, lue dans la fiche du pilote r1") \
    P(52, "ovl2", 0x021EDF10, mod_turbo_depense, \
      "jauge, 0x021EDBC4 : la dépense d'un tour de turbo (r0), avant de l'ajouter ; r4 : la dynamique") \
    P(53, "ovl2", 0x021EDE44, mod_turbo_depense, \
      "jauge, 0x021EDBC4 : la dépense d'un tour de frein (r0), avant de l'ajouter ; r4 : la dynamique") \
    P(54, "ovl2", 0x022010B6, mod_bombes_r0, \
      "écran tactile, 0x02200C24 : le stock de bombes diminué (r0), avant d'être rangé en +0x70 de la fiche") \
    P(55, "ovl2", 0x022052B4, mod_bombes_r1, \
      "radar, 0x02205030 : le stock de bombes diminué (r1), avant d'être rangé en +0x70 de la fiche") \
    P(56, "ovl2", 0x021E5F26, mod_charge_max, \
      "tir chargé, 0x021E5EB8 : le niveau de la cible (r0, 0 à 5), qui fait les dégâts") \
    P(57, "ovl2", 0x021F071C, mod_degats_recus, \
      "dégâts de l'Arwing, 0x021F071C, à l'entrée : r0 la dynamique, r1 l'acteur touché, r2 les dégâts") \
    P(58, "ovl0", 0x0216CC8C, mod_degats_infliges, \
      "dégâts d'un ennemi, 0x0216CC8C, à l'entrée : r0 l'acteur, r1 les dégâts, r2 l'attaquant (ou zéro)") \
    P(59, "ovl2", 0x021FE9F2, mod_jauge, \
      "taille de la jauge, 0x021FE9D8 : la taille lue dans la fiche du pilote (r0 : 0 petite, 1 moyenne, 2 grande) ; le pilote en [sp]")

#define COMPTER(rang, region, adresse, crochet, quoi) +1
NativeHook g_modifs[0 LES_POINTS(COMPTER)];
#undef COMPTER

void manette_crochet_manche(Cpu *c);
void manette_crochet_stylet(Cpu *c);
void manette_crochet_appui(Cpu *c);
void manette_crochet_cercle(Cpu *c);
void manette_crochet_rails(Cpu *c);
void manette_crochet_ouverture(Cpu *c);
void manette_crochet_commandes(Cpu *c);
static void portee_coupure(Cpu *c);
static void portee_projection(Cpu *c);
static void portee_fond_hauteur(Cpu *c);
static void portee_fond_echelle(Cpu *c);
static void portee_tir_duree(Cpu *c);
static void portee_laser_duree(Cpu *c);
static void portee_tir_ennemi(Cpu *c);
static void portee_plasma_fondu(Cpu *c);

/* chaque point, son rang et son crochet : les mods posent les leurs d'après cette table (mods.c) */
#define TABLE(rang, region, adresse, crochet, quoi) {rang, crochet},
static const struct { int rang; NativeHook crochet; } g_points[] = { LES_POINTS(TABLE) };
#undef TABLE

/* ------------------------------------------------------------------ la portée d'affichage
 *
 * SFC_PORTEE=facteur, de 1 à 4 : le jeu dessine ce qui est jusqu'à ce facteur fois plus loin.
 *
 * Le jeu d'origine ne dessine pas un objet — cellule de terrain, bâtiment, tourelle — dont le centre
 * est plus loin que 4 000 unités plus son rayon (0x01FFBF8C, dans la mémoire rapide : six plans, le
 * dernier à cette distance). Les 4 000,0 sont une constante que la méthode de la caméra 0x02066A34
 * recopie à chaque image en 0x027E1504, que seul ce dessin lit. Le terrain est entièrement chargé
 * (neuf cellules sur neuf de 2 560) : il n'y a qu'à dessiner plus loin.
 *
 * Seulement, tout ce qui passe derrière l'anneau d'horizon reste invisible : le modèle d'arrière-
 * plan (BG.mdl, poussé par PushBackGround avec les nuages et le soleil) est un anneau de panorama
 * posé autour de la caméra vers 4 000 à 5 000 unités, opaque, qui cache le décor au-delà. Il recule
 * donc du même facteur — agrandi autour de l'œil de la caméra, si bien qu'il couvre exactement la
 * même part de l'écran : seule sa profondeur change. Son plus lointain élément est à 6 500 unités ;
 * passé le facteur 2,4 il franchirait le plan lointain de la projection (16 384), qui recule alors
 * lui aussi.
 *
 * Ce que la portée change au jeu lui-même : rien de ses règles ni de son rythme — le même tour à la
 * même image —, mais le temps. Dessiner coûte à la console, et le portage le compte : chaque boucle
 * du code avance l'horloge simulée, et la liste de commandes plus longue se parcourt plus longtemps.
 * L'horloge d'un tour se décale ainsi de quelques centaines de cycles, et ce que le jeu en tire au
 * cycle près — des tirages, l'instant d'un événement — peut différer d'une partie à portée d'origine,
 * comme sur une console qui aurait plus à dessiner. Une partie enregistrée ne se rejoue donc à
 * l'identique qu'à la portée où elle a été jouée. (Une seconde passe hors du temps n'y suffit pas :
 * ses commandes allongent quand même la liste que le jeu parcourt ensuite avec son propre code.)
 */
static int32_t g_portee = 0x1000;          /* le facteur, en 20.12 */

static int32_t fois_portee(int32_t v) { return (int32_t)(((int64_t)v * g_portee) >> 12); }

static void portee_coupure(Cpu *c)    { c->r[1] = (uint32_t)fois_portee((int32_t)c->r[1]); }
static void portee_projection(Cpu *c) { c->r[0] = (uint32_t)fois_portee((int32_t)c->r[0]); }

/* L'arrière-plan est posé en (caméra.x, hauteur, caméra.z) : la hauteur est celle de la caméra ou
   zéro, plus un décalage du modèle. L'agrandir autour de l'œil, c'est éloigner d'autant la hauteur
   de celle de l'œil… */
static void portee_fond_hauteur(Cpu *c) {
    int32_t oeil = (int32_t)ldr_word(c, c->r[0] + 4u);
    c->r[6] = (uint32_t)(oeil + fois_portee((int32_t)c->r[6] - oeil));
}

/* …et multiplier la première de ses trois rotations : une mise à l'échelle uniforme se glisse
   n'importe où parmi elles. Les modèles d'arrière-plan ne sont pas éclairés : que la matrice des
   normales grandisse aussi n'y change rien. */
static void portee_fond_echelle(Cpu *c) {
    uint32_t m = c->r[13] + 0x38u;
    for (uint32_t i = 0; i < 9; i++)
        mem_write32(c, m + 4u * i, (uint32_t)fois_portee((int32_t)ldr_word(c, m + 4u * i)));
}

/* La portée des chasseurs : un tir va vitesse × durée de vie. Allonger la durée du même facteur que la
   portée d'affichage laisse chacun atteindre ce qu'il voit, sans changer la vitesse des tirs.

   Les tirs ennemis — les overlays d'ennemis appellent tous la même initialisation, 0x021E0DB4 — reçoivent
   une fiche : direction, vitesse (+0x10), durée de vie (+0x1C, en secondes 20.12 ; 1,5 s pour les chasseurs
   ennemis, à 1 500 unités par seconde). Leur mise à jour (0x021E0EDC) les fait avancer tant que leur âge,
   qui croît de 1/30 s par tour, n'a pas dépassé cette durée.

   Le laser d'Arwing — le joueur et ses coéquipiers — est un autre objet (0x021DF7A4, dessiné par l'effet de
   trait 0x0217FD84) : il vole à 4 500 unités par seconde et meurt quand son âge (+0x48) passe 1,1 s, une
   constante en dur ; en multijoueur la durée vient de la configuration réseau, une autre branche. */
static void portee_tir_duree(Cpu *c)   { c->r[0] = (uint32_t)fois_portee((int32_t)c->r[0]); }
static void portee_laser_duree(Cpu *c) { c->r[0] = (uint32_t)fois_portee((int32_t)c->r[0]); }

/* Le laser Plasma — le troisième niveau de laser, après SingleLaser et TwinLaser qu'anime 0x021DF7A4 —
   a sa propre mise à jour (0x0221B47C) : un âge en +0x48, la mort passé 0x1CCC, 1,8 s en dur (point 23,
   le crochet du laser), à 2 200–3 200 unités par seconde. Mais il rétrécit avant : sa taille vaut
   1 − 5 × (âge − 1,6 s), bornée entre 0 et 1 (0x0221B720). Allonger sa vie sans repousser d'autant ce
   rétrécissement le ferait voler invisible ; il commence donc d'autant plus tard, et dure toujours 0,2 s.

   Le verrouillage de la charge, lui, n'a pas de portée à doubler : la cible est choisie par un cône
   lancé de l'Arwing vers le viseur (0x02210D04, test 0x021B0934), sans distance maximale, parmi les
   ennemis dont le centre tombe à l'écran — le jeu d'origine touche ainsi des ennemis à plus de 9 000
   unités ; et le tir chargé suit sa cible jusqu'à 300 tours, dix secondes. C'est ce que l'on voit qui
   borne ce que l'on vise : l'affichage doublé suffit. */
static void portee_plasma_fondu(Cpu *c) {
    c->r[0] += (uint32_t)(fois_portee(0x1CCC) - 0x1CCC);
}

/* D'où les ennemis tirent. Il n'y a pas de réglage commun : les portées de config.txt (Chase_Range,
   Max_Attack_Dist…) sont des restes d'un prototype que le code ne lit plus. Chaque comportement d'ennemi a
   ses distances en dur. Ne sont retenues ici que les portées de tir : dans les fonctions qui appellent
   l'initialisation des tirs (0x021E0DB4) ou leurs appelants, la distance à la cible — une longueur que
   0x01FFED88 vient de calculer — comparée à une constante qui décide du tir. Les bornes basses (« trop près
   pour tirer ») et les tests de proximité restent d'origine, comme les distances de poursuite et
   d'approche, dispersées dans les états de plus d'une centaine de comportements. Beaucoup d'ennemis tirent
   d'ailleurs sur une minuterie, une fois engagés (Star Wolf) : pour eux, seule la durée de leurs tirs change.

   La portée est multipliée par le facteur, sans dépasser celle de l'affichage (4 000 fois le facteur) :
   un ennemi ne tire pas de plus loin qu'on ne le voit, et une portée déjà plus longue que la vue reste
   la sienne. */
static void portee_tir_ennemi(Cpu *c) {
    int32_t v = (int32_t)c->r[0];
    int32_t w = fois_portee(v);
    int32_t vue = fois_portee(4000 << 12);
    if (w > vue) w = vue > v ? vue : v;
    c->r[0] = (uint32_t)w;
}

/* ------------------------------------------------------------------ la manette
 *
 * Elle donne ses commandes au cockpit comme le stylet (runtime/manette.c) : les deux crochets sont posés dès
 * le départ et ne font rien tant qu'aucune manette ne pilote — sans manette, le jeu est celui de la cartouche.
 * SFC_MANETTE=0 les retire. */

void modifications_init(void) {
    const char *m = getenv("SFC_MANETTE");
    if (!(m && (*m == '0' || !strcmp(m, "non")))) {
        g_modifs[25] = manette_crochet_manche;
        g_modifs[26] = manette_crochet_commandes;
        g_modifs[27] = manette_crochet_stylet;
        g_modifs[28] = manette_crochet_manche;
        g_modifs[29] = manette_crochet_appui;
        g_modifs[30] = manette_crochet_cercle;
        g_modifs[31] = manette_crochet_rails;
        g_modifs[32] = manette_crochet_manche;
        g_modifs[33] = manette_crochet_cercle;
        g_modifs[34] = manette_crochet_ouverture;
    }
    const char *p = getenv("SFC_PORTEE");
    if (p && *p) {
        double f = atof(p);
        if (f < 1.0) f = 1.0;
        if (f > 4.0) f = 4.0;
        g_portee = (int32_t)(f * 4096.0 + 0.5);
        if (g_portee != 0x1000) {
            g_modifs[0] = portee_coupure;
            if (f > 2.4) g_modifs[1] = portee_projection;
            g_modifs[2] = portee_fond_hauteur;
            g_modifs[3] = portee_fond_echelle;
            g_modifs[4] = portee_tir_duree;
            g_modifs[5] = portee_laser_duree;
            for (int i = 6; i <= 22; i++) g_modifs[i] = portee_tir_ennemi;
            g_modifs[23] = portee_laser_duree;
            g_modifs[24] = portee_plasma_fondu;
            rt_log("portée d'affichage : ×%.2f — ce n'est plus le jeu d'origine", f);
        }
    }
    /* les mods du lanceur (onglet Mods) : chacun pose les crochets qui sont à lui */
    mods_lire();
    for (size_t k = 0; k < sizeof g_points / sizeof *g_points; k++)
        if (mods_veut(g_points[k].crochet)) g_modifs[g_points[k].rang] = g_points[k].crochet;
}

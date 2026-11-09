/*
 * Les mods : des changements de règles que le joueur choisit dans le lanceur (onglet Mods). Chacun est éteint
 * par défaut ; allumé, ce n'est plus le jeu de la cartouche, et le journal le dit.
 *
 * Comme la portée d'affichage, ils passent par des points de modification (modifications.c) : un crochet juste
 * avant une instruction du jeu, qui change un registre comme le jeu l'aurait fait. Ils agissent au moment où le
 * jeu se sert d'une valeur — le tir, la dépense de la jauge, les dégâts, le chargement du modèle —, jamais sur
 * la valeur rangée : un épisode de l'histoire qui change le laser de Fox ne défait pas le mod, et la sauvegarde
 * ne garde pas le laser imposé. Deux mods touchent pourtant à ce qu'elle retient : les bombes infinies (le
 * stock ne baisse plus) et les dégâts reçus (le bouclier qui reste d'un combat au suivant).
 *
 * Les pilotes du jeu ont chacun un rang, celui de leur nom dans la table 0x021D77BC (naus, fox, krystal,
 * falco, slippy, ash, wolf, leon, panther, octman, bill, lucy, amanda, cat, peppy, james, jodie) et de leur
 * fiche dans l'état de mission ([0x021D68B8 + 4 + 4 × rang], le « PlayerState » que remplit
 * SetupCharacters.txt) : Fox est le 1, Slippy le 4. Le laser, le verrouillage, la jauge et l'apparence se
 * règlent pilote par pilote.
 *
 * Sous licence MIT.
 */
#include "mods.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FOX 1u
#define NB_PILOTES 17

/* pour chaque pilote, par son rang : -1, ce que la partie lui donne */
static int g_laser[NB_PILOTES], g_verrou[NB_PILOTES], g_jauge[NB_PILOTES], g_vaisseau[NB_PILOTES];
static int g_turbo, g_bombes, g_charge, g_invincible, g_un_coup;
static int32_t g_infliges = 0x1000, g_recus = 0x1000;   /* facteurs des dégâts, en 20.12 */

/* le réglage d'un pilote désigné par un mot du jeu ; -1 hors de la table */
static int de(const int *table, uint32_t pilote) { return pilote < NB_PILOTES ? table[pilote] : -1; }

/* L'acteur du joueur, comme le jeu le trouve (0x02188B98 de la scène 0x0218A340) : la poignée de la scène en
   0x021D3260 désigne l'acteur, à 0x1C près ; pas de joueur hors d'un combat. */
static uint32_t joueur(Cpu *c) {
    const uint32_t poignee = ldr_word(c, 0x021D3260u);
    const uint32_t objet = poignee ? ldr_word(c, poignee) : 0;
    return objet ? objet - 0x1Cu : 0;
}

static uint32_t fois(uint32_t v, int32_t facteur) {
    return (uint32_t)(int32_t)(((int64_t)(int32_t)v * facteur) >> 12);
}

/* ------------------------------------------------------------------ le laser
 *
 * La fiche d'un pilote porte son type de laser en +0x44 : 0 aucun, 1 simple, 2 double, 3 plasma, 4 tir courbe
 * (ArcShot), 5 Zapper. SetupCharacters.txt le donne au démarrage (Fox : simple, Slippy : plasma) ; l'histoire
 * change celui de Fox (SetMemberWeapon fox Laser Twin|Plasma, que la sauvegarde retient dans un octet,
 * 0x021D9748). Le vaisseau n'en garde pas de copie : le jeu relit la fiche à chaque tir. Le mod substitue donc
 * le type aux endroits où il est lu, sans rien écrire :
 * - le tir, 0x021FE1E4 (la méthode +0x48 du véhicule, r4, dont le pilote est en +0x28) : une table de sauts y
 *   choisit la fonction de tir du type — et avec elle les dégâts (10, 15, 20, 30, 120), la vitesse, la durée
 *   de vie, le double trait, l'objet à part du plasma ;
 * - la cadence, 0x021FE178 (0,1 s entre deux tirs, 0,3 s pour le tir courbe, 0,8 s pour le Zapper), et
 *   l'attente de plus du Zapper, 0x021FE140 — le pilote en [sp] ;
 * - pour Fox seul, le jeu que 0x021F78C4 choisit à chaque tour selon son laser (« ArwingDynamicsPlayerFoxSingle »,
 *   « …Twin », « …SinglePlasma » ; les autres pilotes ont chacun le leur, quel que soit leur laser — son rôle exact
 *   n'est pas établi), et l'image de son vaisseau dans le briefing (0x0222BCB4). */
void mod_laser_tir(Cpu *c) {
    const int t = de(g_laser, ldr_word(c, c->r[4] + 0x28u));
    if (t >= 0) c->r[0] = (uint32_t)t;
}
void mod_laser_cadence(Cpu *c) {
    const int t = de(g_laser, ldr_word(c, c->r[13]));
    if (t >= 0) c->r[0] = (uint32_t)t;
}
void mod_laser_son(Cpu *c)      { c->r[0] = (uint32_t)g_laser[FOX]; }
void mod_laser_briefing(Cpu *c) { c->r[1] = (uint32_t)g_laser[FOX]; }

/* ------------------------------------------------------------------ le verrouillage
 *
 * Le type de verrouillage est en +0x48 de la fiche : 0 aucun, 1 simple, 2 multiple, 3 large (Fox : simple ;
 * l'histoire lui donne le multiple, SetMemberWeapon fox Lock Multi ; Slippy : aucun). Il se lit comme le
 * laser : par trois méthodes du véhicule (0x021FE824, 0x021FE8CC, 0x021FE970 — le pilote en [sp]) et par le
 * cockpit (0x0220F7FC et 0x0220FCF2 — le pilote en +0x48 et +0x50 du contrôle, r7). */
void mod_verrou_vehicule(Cpu *c) {
    const int t = de(g_verrou, ldr_word(c, c->r[13]));
    if (t >= 0) c->r[0] = (uint32_t)t;
}
void mod_verrou_cockpit_r4(Cpu *c) {
    const int t = de(g_verrou, ldr_word(c, c->r[7] + 0x48u));
    if (t >= 0) c->r[4] = (uint32_t)t;
}
void mod_verrou_cockpit_r0(Cpu *c) {
    const int t = de(g_verrou, ldr_word(c, c->r[7] + 0x50u));
    if (t >= 0) c->r[0] = (uint32_t)t;
}

/* ------------------------------------------------------------------ la jauge
 *
 * La taille de la jauge du turbo, du frein et des tonneaux est en +0x54 de la fiche : 0 petite, 1 moyenne,
 * 2 grande (Fox : grande ; Slippy : petite). La dynamique de l'Arwing la demande au véhicule en se
 * construisant (sa méthode +0x14, 0x021FE9D8, le pilote en [sp]), qui la change en capacité : 25, 50 ou 75. */
void mod_jauge(Cpu *c) {
    const int t = de(g_jauge, ldr_word(c, c->r[13]));
    if (t >= 0) c->r[0] = (uint32_t)t;
}

/* ------------------------------------------------------------------ l'apparence
 *
 * Chaque pilote a son vaisseau en quatre pièces — la carlingue, deux plaques de réacteur (allumé, éteint) et la
 * flamme —, nommées dans sa fiche (+0x10 à +0x1C) et rangées par SetupPlayersModel.txt sous la clé du pilote.
 * Au début d'un combat, la création du joueur (0x02188F1C) fait charger les fichiers de cette seule clé
 * (0x0221F828, la clé recopiée sur la pile en r1, le pilote en sp+0xBC) ; puis six lectures de la fiche, chacune
 * par le pilote (0x021A2D14, r1), donnent au véhicule ses quatre noms de modèle et la place de ses deux
 * traînées. Le mod donne à ces deux étapes le pilote choisi : les fichiers de son vaisseau se chargent, et ce
 * sont eux que le véhicule dessine. Tout le reste est au pilote — sa fiche, son laser, son bouclier, sa jauge,
 * ses bombes, son portrait, la collision (le volume « Arwing », commun à tous). */
void mod_vaisseau_chargement(Cpu *c) {
    const int t = de(g_vaisseau, ldr_word(c, c->r[13] + 0xBCu));
    if (t < 0) return;
    for (uint32_t k = 0; k < 16; k += 4)
        mem_write32(c, c->r[1] + k, ldr_word(c, 0x021D77BCu + 16u * (uint32_t)t + k));
}
void mod_vaisseau_pieces(Cpu *c) {
    const int t = de(g_vaisseau, c->r[1]);
    if (t >= 0) c->r[1] = (uint32_t)t;
}

/* ------------------------------------------------------------------ le turbo infini
 *
 * Le turbo, le frein et les tonneaux puisent dans une seule jauge, dans la dynamique de l'Arwing : la part
 * dépensée en +0xBC (0 : pleine), la capacité en +0xC4. Chaque tour, 0x021EDBC4 la remplit de 0,5, puis y
 * ajoute 1,0 par tour de turbo, de frein ou de tonneau ; pleine, elle se verrouille trois tours et le cockpit
 * interrompt le turbo. Le mod retire la dépense du turbo et du frein (r0, avant de l'ajouter) — pour l'Arwing
 * du joueur seul, celui dont l'acteur (+0x20 de la dynamique) est l'acteur de la scène. Les tonneaux gardent la
 * leur : sans elle, un tonneau lancé à la manette, qui ne s'arrête qu'à la jauge vide, ne finirait plus. */
void mod_turbo_depense(Cpu *c) {
    if (ldr_word(c, c->r[4] + 0x20u) == ldr_word(c, 0x021D3260u)) c->r[0] = 0;
}

/* ------------------------------------------------------------------ les bombes infinies
 *
 * Le stock de bombes d'un pilote est en +0x70 de sa fiche. L'écran tactile le décompte à la fin de la livraison
 * d'une bombe (0x022010AE) et quand on glisse l'icône sur le radar (0x022052AC) : juste avant que la valeur
 * diminuée soit rangée, le mod la remet à ce qu'elle était. La manette décompte les siennes elle-même
 * (manette.c, qui demande mods_bombes_infinies()). */
void mod_bombes_r0(Cpu *c) { c->r[0] += 1u; }
void mod_bombes_r1(Cpu *c) { c->r[1] += 1u; }
int mods_bombes_infinies(void) { return g_bombes; }

/* ------------------------------------------------------------------ le tir verrouillé à pleine puissance
 *
 * Le projectile du tir chargé fait 20 × {0, 1, 2, 4, 8, 16} selon le niveau de sa cible (+0x10 du nœud de
 * verrouillage, 0 à 5 ; 0x021E5EB8, r0). Le mod le porte toujours au plus haut : 320. */
void mod_charge_max(Cpu *c) { c->r[0] = 5; }

/* ------------------------------------------------------------------ les dégâts
 *
 * Deux systèmes à part. L'Arwing a sa fonction de dégâts, 0x021F071C (r0 la dynamique, r1 l'acteur touché, r2
 * les dégâts en 20.12) : sa vie est en +0xB8 de la dynamique (100 = 0x64000), son maximum en +0x10 (60, 100 ou
 * 130 selon le bouclier), et elle est recopiée à chaque image dans la fiche du pilote (+0x68), d'où le
 * combat suivant repart. Tout y passe — tirs, missiles, choc contre un ennemi, le sol ou un bâtiment, les
 * attaques des boss — et c'est là seulement que l'Arwing meurt. À zéro dégât, la fonction repart sans rien
 * toucher : l'invincibilité. Une règle de mission emprunte aussi ce chemin et reste en vigueur : la balise,
 * qu'il faut franchir à temps (« sinon vous serez abattu », 0x0226B4DC, revenue en 0x0226B611).
 *
 * Les ennemis, boss et parties de boss ont la leur, 0x0216CC8C (r0 l'acteur, r1 les dégâts, r2 l'attaquant, nul
 * quand le jeu ne le note pas) : la vie en +0xE8 (celle de SetLife, fois 4 096). Les dégâts viennent toujours du
 * +0xF0 de ce qui frappe — le laser, le tir chargé, la bombe (100), l'attaque en tonneau. Le mod multiplie ceux du
 * joueur — ceux dont l'attaquant est le joueur ou n'est pas noté : sur les parties relevées, tous ceux-là
 * venaient de ses tirs. « Un coup » les porte à la vie qui reste. */
void mod_degats_recus(Cpu *c) {
    if (c->r[1] != joueur(c) || c->r[14] == 0x0226B611u) return;
    c->r[2] = g_invincible ? 0u : fois(c->r[2], g_recus);
}

void mod_degats_infliges(Cpu *c) {
    const uint32_t j = joueur(c);
    if (!j || (c->r[2] != j && c->r[2] != 0)) return;
    if (g_un_coup) {
        const int32_t vie = (int32_t)ldr_word(c, c->r[0] + 0xE8u);
        if ((int32_t)c->r[1] < vie) c->r[1] = (uint32_t)vie;
    } else {
        c->r[1] = fois(c->r[1], g_infliges);
    }
}

/* ------------------------------------------------------------------ les réglages
 *
 * Ceux d'un vaisseau se donnent pilote par pilote, « pilote:valeur » séparés par des virgules (un réglage sans
 * pilote vaut pour Fox) : SFC_MOD_LASER=fox:plasma,slippy:double (simple, double, plasma, courbe, zapper),
 * SFC_MOD_VERROUILLAGE (simple, multiple, large), SFC_MOD_JAUGE (petite, moyenne, grande), SFC_MOD_VAISSEAU
 * (la clé d'un autre pilote : slippy:falco). Les autres valent pour le joueur : SFC_MOD_TURBO=1,
 * SFC_MOD_BOMBES=1, SFC_MOD_CHARGE=1, SFC_MOD_INVINCIBLE=1, SFC_MOD_DEGATS=<facteur ou « un-coup »>,
 * SFC_MOD_DEGATS_RECUS=<facteur>. */
static const char *const LASERS[] = {"aucun", "simple", "double", "plasma", "courbe", "zapper"};
static const char *const VERROUS[] = {"aucun", "simple", "multiple", "large"};
static const char *const JAUGES[] = {"petite", "moyenne", "grande"};
static const char *const PILOTES[] = {"naus", "fox", "krystal", "falco", "slippy", "ash", "wolf", "leon", "panther",
                                      "octman", "bill", "lucy", "amanda", "cat", "peppy", "james", "jodie"};

static int parmi(const char *v, size_t n_v, const char *const *noms, int n) {
    if (!n_v) return -1;
    if (v[0] >= '0' && v[0] <= '9') { const int k = atoi(v); return k >= 0 && k < n ? k : -1; }
    for (int k = 0; k < n; k++) if (strlen(noms[k]) == n_v && !strncmp(v, noms[k], n_v)) return k;
    return -1;
}

/* « fox:plasma,slippy:double » : chaque valeur reconnue va au pilote nommé ; le plus petit réglage admis est
   `plancher` (le laser « aucun » n'est pas proposé, une apparence est celle d'un vrai pilote) */
static void par_pilote(const char *variable, int *table, const char *const *noms, int n, int plancher) {
    for (int p = 0; p < NB_PILOTES; p++) table[p] = -1;
    const char *v = getenv(variable);
    while (v && *v) {
        const char *fin = strchr(v, ',');
        const size_t long_ = fin ? (size_t)(fin - v) : strlen(v);
        const char *deux = memchr(v, ':', long_);
        int pilote = (int)FOX;
        const char *valeur = v;
        if (deux) {
            pilote = parmi(v, (size_t)(deux - v), PILOTES, NB_PILOTES);
            valeur = deux + 1;
        }
        const int k = parmi(valeur, (size_t)(v + long_ - valeur), noms, n);
        if (pilote >= 1 && k >= plancher) table[pilote] = k;
        else rt_log("mods : %s, « %.*s » ignoré", variable, (int)long_, v);
        v = fin ? fin + 1 : NULL;
    }
}

static int oui(const char *nom) {
    const char *v = getenv(nom);
    return v && *v && strcmp(v, "0") && strcmp(v, "non");
}

/* un facteur de 0,1 à 100, en 20.12 ; 0x1000 hors de là */
static int32_t facteur(const char *nom) {
    const char *v = getenv(nom);
    const double f = v && *v ? atof(v) : 1.0;
    return f >= 0.1 && f <= 100.0 ? (int32_t)(f * 4096.0 + 0.5) : 0x1000;
}

static int un_pilote(const int *table) {
    for (int p = 1; p < NB_PILOTES; p++) if (table[p] >= 0) return 1;
    return 0;
}

void mods_lire(void) {
    par_pilote("SFC_MOD_LASER", g_laser, LASERS, 6, 1);
    par_pilote("SFC_MOD_VERROUILLAGE", g_verrou, VERROUS, 4, 1);
    par_pilote("SFC_MOD_JAUGE", g_jauge, JAUGES, 3, 0);
    par_pilote("SFC_MOD_VAISSEAU", g_vaisseau, PILOTES, NB_PILOTES, 1);
    g_turbo = oui("SFC_MOD_TURBO");
    g_bombes = oui("SFC_MOD_BOMBES");
    g_charge = oui("SFC_MOD_CHARGE");
    g_invincible = oui("SFC_MOD_INVINCIBLE");
    const char *d = getenv("SFC_MOD_DEGATS");
    g_un_coup = d && !strcmp(d, "un-coup");
    g_infliges = g_un_coup ? 0x1000 : facteur("SFC_MOD_DEGATS");
    g_recus = facteur("SFC_MOD_DEGATS_RECUS");

    char dit[1024] = "";
    size_t n = 0;
#define DIRE(...) (n += (size_t)snprintf(dit + n, n < sizeof dit ? sizeof dit - n : 0, __VA_ARGS__))
    for (int p = 1; p < NB_PILOTES; p++) {
        if (g_laser[p] >= 0) DIRE(" laser de %s %s,", PILOTES[p], LASERS[g_laser[p]]);
        if (g_verrou[p] >= 0) DIRE(" verrouillage de %s %s,", PILOTES[p], VERROUS[g_verrou[p]]);
        if (g_jauge[p] >= 0) DIRE(" jauge de %s %s,", PILOTES[p], JAUGES[g_jauge[p]]);
        if (g_vaisseau[p] >= 0) DIRE(" vaisseau de %s : celui de %s,", PILOTES[p], PILOTES[g_vaisseau[p]]);
    }
    if (g_turbo) DIRE(" turbo infini,");
    if (g_bombes) DIRE(" bombes infinies,");
    if (g_charge) DIRE(" tir verrouillé à pleine puissance,");
    if (g_invincible) DIRE(" invincible,");
    else if (g_recus != 0x1000) DIRE(" dégâts reçus ×%.2f,", g_recus / 4096.0);
    if (g_un_coup) DIRE(" un coup suffit,");
    else if (g_infliges != 0x1000) DIRE(" dégâts infligés ×%.2f,", g_infliges / 4096.0);
#undef DIRE
    if (dit[0]) {
        dit[strlen(dit) - 1] = 0;
        rt_log("mods :%s — ce n'est plus le jeu d'origine", dit);
    }
}

int mods_veut(NativeHook h) {
    if (h == mod_laser_tir || h == mod_laser_cadence) return un_pilote(g_laser);
    if (h == mod_laser_son || h == mod_laser_briefing) return g_laser[FOX] >= 0;
    if (h == mod_verrou_vehicule || h == mod_verrou_cockpit_r4 || h == mod_verrou_cockpit_r0) return un_pilote(g_verrou);
    if (h == mod_jauge) return un_pilote(g_jauge);
    if (h == mod_vaisseau_chargement || h == mod_vaisseau_pieces) return un_pilote(g_vaisseau);
    if (h == mod_turbo_depense) return g_turbo;
    if (h == mod_bombes_r0 || h == mod_bombes_r1) return g_bombes;
    if (h == mod_charge_max) return g_charge;
    if (h == mod_degats_recus) return g_invincible || g_recus != 0x1000;
    if (h == mod_degats_infliges) return g_un_coup || g_infliges != 0x1000;
    return 0;
}

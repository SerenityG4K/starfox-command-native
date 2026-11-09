/*
 * Réécritures natives, et la preuve qu'elles sont fidèles.
 *
 * Décompiler, ici, ce n'est pas « écrire une fonction qui a l'air juste » : c'est écrire
 * une fonction dont on peut montrer qu'elle fait exactement ce que fait la traduction du
 * binaire, sur les états que le jeu produit vraiment.
 *
 * SFC_VERIF=0x0216CB64 (ou plusieurs adresses séparées par des virgules, ou « tout »)
 * installe, sur chaque fonction réécrite visée, un crochet qui :
 *
 *   1. retient l'état de la machine,
 *   2. exécute la traduction — c'est elle qui reste la référence, et son résultat est
 *      celui que le jeu garde,
 *   3. remet l'état d'avant,
 *   4. exécute la réécriture,
 *   5. compare registres et mémoire, et dit où elles divergent.
 *
 * Le jeu continue donc toujours avec le résultat de la traduction : une réécriture fausse
 * se signale sans rien casser. C'est ce qui permet de vérifier en jouant.
 *
 * Sans SFC_VERIF, les réécritures dont l'entrée figure dans SFC_NATIF remplacent la
 * traduction pour de bon.
 *
 * Sous licence MIT.
 */
#include "natif.h"
#include "arm7_run.h"
#include <windows.h>
#include <setjmp.h>
#include "gx_geom.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

/* ------------------------------------------------------------------ le registre */

static void verifier(Cpu *c, const Reecriture *r);
Surveillance *g_suivi_auto;
static int g_banc_froid;        /* SFC_VERIF_FROID : on essaie les feuilles hors de toute partie */
static int code_intact(void);   /* à froid : la traduction relit ses constantes dans le code */
static uint64_t g_journal;      /* le journal des appels d'une exécution à froid, et leur nombre */
static unsigned g_journal_n;
static unsigned g_journal_materiel;   /* et celui des accès aux registres, que le bouchon matériel note */

/* Une fonction du jeu laisse derrière elle des drapeaux, et son appelant branche dessus.
   Une réécriture qui les ignore est fausse, même quand ses registres sont justes. Ces deux
   aides reproduisent les deux instructions qui les posent le plus souvent. */
static void drapeaux_cmp0(Cpu *c, uint32_t v) {   /* cmp rX, #0 : une soustraction */
    c->nf = v >> 31; c->zf = !v; c->cf = 1; c->vf = 0;
}
static void drapeaux_adds0(Cpu *c, uint32_t v) {  /* adds rX, rY, #0 : une addition */
    c->nf = v >> 31; c->zf = !v; c->cf = 0; c->vf = 0;
}

/* Le prologue d'une fonction sauvegarde des registres sous le pointeur de pile, puis creuse
 * la place de ses variables locales. Une réécriture native n'a besoin de rien de tout cela —
 * mais elle doit le faire quand même, pour deux raisons :
 *
 *   — ces octets sauvegardés restent en mémoire après le retour, et le jeu recopie parfois un
 *     tampon sans l'avoir rempli en entier : des déchets différents finissent par se retrouver
 *     dans des données bien vivantes ;
 *   — tant que le cadre est ouvert, les fonctions appelées posent leur propre pile plus bas.
 *     Ne pas creuser décale toute la pile de ce qui suit.
 *
 * `valeurs` suit l'ordre du push : registres croissants, adresse de retour en dernier.
 * `creux` est ce que la fonction réserve en plus (le « sub sp, #n » de son prologue). */
static uint32_t cadre_ouvrir(Cpu *c, const uint32_t *valeurs, int combien, uint32_t creux) {
    uint32_t sp = c->r[13];
    for (int i = 0; i < combien; i++)
        mem_write32(c, sp - 4u * (uint32_t)(combien - i), valeurs[i]);
    c->r[13] = sp - 4u * (uint32_t)combien - creux;
    return sp;
}

/* Le pop qui ferme le cadre ne rend pas les registres qu'on avait en entrant : il **relit**
 * ce qui se trouve maintenant à leur place dans la pile. La nuance paraît byzantine, mais une
 * interruption survenue pendant la fonction a pu écrire là — et c'est alors la valeur relue
 * que la suite du jeu utilise. Une réécriture qui se contente de ne pas toucher à r4 garde
 * l'ancienne, et le jeu dérive quelques images plus tard, loin de la cause.
 *
 * `registres` énumère ce que le push avait sauvegardé, dans l'ordre, adresse de retour exclue. */
static void cadre_fermer_par(Cpu *c, uint32_t sp_entree, const int *registres, int combien,
                             int par_registre) {
    uint32_t base = sp_entree - 4u * (uint32_t)(combien + 1);
    for (int i = 0; i < combien; i++)
        c->r[registres[i]] = mem_read32(c, base + 4u * (uint32_t)i);
    uint32_t cible = mem_read32(c, base + 4u * (uint32_t)combien);
    /* Certaines fonctions ne dépilent pas dans pc : elles dépilent dans un registre, puis
       sautent dessus (« pop {r3} ; bx r3 »). Le registre garde alors l'adresse, et l'appelant
       peut la lire. */
    if (par_registre >= 0) c->r[par_registre] = cible;
    c->r[13] = sp_entree;
    c->r[15] = cible;
    /* Et l'on repart par où la pile dit de repartir, pas par où l'on croyait : rt_jump
       reconnaît un vrai retour et, sinon, suit le saut — c'est ainsi que le jeu se sert
       parfois d'un « pop {pc} » pour passer la main à une autre fonction. */
    rt_jump(c, cible);
}

static void cadre_fermer(Cpu *c, uint32_t sp_entree, const int *registres, int combien) {
    cadre_fermer_par(c, sp_entree, registres, combien, -1);
}

/* Appeler une fonction du jeu exactement comme le fait le code traduit.
 *
 * Trois choses, et aucune n'est cosmétique :
 *   — ne pas repasser par le dispatch, qui imposerait le bit Thumb, alors que la traduction
 *     d'un blx immédiat le laisse tel quel et que le code appelé lit ce bit ;
 *   — poser l'adresse de retour, ce que fait l'instruction d'appel ;
 *   — empiler cette adresse dans la pile fantôme (RT_CALL), car c'est elle qui permet de
 *     distinguer, plus bas, un « POP {PC} » qui revient d'un « POP {PC} » qui saute ailleurs.
 *     L'oublier ne se voit pas dans une comparaison d'état — et fait pourtant diverger le jeu
 *     dès la dixième image. */
void natif_appel_direct(Cpu *c, uint32_t adresse, uint32_t retour) {
    c->r[14] = retour;
    const RecompEntry *e = rt_find(c, adresse);
    if (!e) { RT_CALL(c, retour, rt_call(c, adresse)); return; }
    RT_CALL(c, retour, e->fn(c, adresse & ~1u));
}

/* Les réécritures secondaires de la table du générateur, rangées à part : il y en a cent
   cinquante, et la reprise d'un thread en cherche une trois cent mille fois par parcours. */
static const Reecriture **g_secondaires;
static size_t g_nb_secondaires;

const Reecriture *natif_secondaire(const char *region, uint32_t englobante, uint32_t adresse) {
    for (size_t k = 0; k < g_nb_secondaires; k++) {
        const Reecriture *r = g_secondaires[k];
        if (r->entree == adresse && r->englobante == englobante && !strcmp(r->region, region)) return r;
    }
    return NULL;
}

int natif_secondaire_lancer(Cpu *c, const Reecriture *r) {
    if (r->suivi->surveillee) { natif_verifier(c, r); return 1; }
    if (r->suivi->en_service) { r->fn(c); return 1; }
    return 0;
}

static int dispatch_milieu(Cpu *c, const RecompEntry *e, uint32_t adresse) {
    const Reecriture *secondaire = natif_secondaire(e->region, e->entry, adresse);
    return secondaire && natif_secondaire_lancer(c, secondaire);
}

/* SFC_OBSERVER=0xadresse[,0xadresse…][:max] — chaque appel de ces fonctions, direct ou par le dispatch :
   l'image, qui appelle, les quatre premiers arguments, et ce qu'elle rend. C'est le premier levier pour
   trouver « où se décide X » : intercepter la fonction soupçonnée et regarder ce qu'elle reçoit, plutôt
   que de lire son code. `max` borne le journal (2 000 appels par défaut). */
static uint32_t g_observees[8];
static int g_n_observees = -1;
static long g_observer_max = 2000, g_observer_n;
static int observee(uint32_t adresse) {
    if (g_n_observees < 0) {
        g_n_observees = 0;
        const char *v = getenv("SFC_OBSERVER");
        for (const char *p = v; p && *p && g_n_observees < 8;) {
            char *fin;
            unsigned long a = strtoul(p, &fin, 0);
            if (fin == p) break;
            g_observees[g_n_observees++] = (uint32_t)a & ~1u;
            p = fin;
            if (*p == ',') p++;
            else if (*p == ':') { g_observer_max = strtol(p + 1, NULL, 0); break; }
            else break;
        }
    }
    for (int i = 0; i < g_n_observees; i++) if (g_observees[i] == (adresse & ~1u)) return 1;
    return 0;
}
/* Une adresse de SFC_OBSERVER_LIRE : rN ou 0x…, ou [expression] — le mot qu'elle désigne —, suivie de
   décalages +n ou -n. */
static int observer_adresse(Cpu *c, const char **pp, uint32_t *valeur) {
    const char *p = *pp;
    char *fin;
    uint32_t v;
    if (*p == '[') {
        p++;
        if (!observer_adresse(c, &p, &v) || *p != ']') return 0;
        p++;
        v = ldr_word(c, v);
    } else if (*p == 'r') {
        long reg = strtol(p + 1, &fin, 10);
        if (fin == p + 1 || reg < 0 || reg > 15) return 0;
        v = c->r[reg];
        p = fin;
    } else {
        v = (uint32_t)strtoul(p, &fin, 0);
        if (fin == p) return 0;
        p = fin;
    }
    while (*p == '+' || *p == '-') {
        int moins = *p == '-';
        long d = (long)strtoul(p + 1, &fin, 0);
        if (fin == p + 1) return 0;
        v += moins ? (uint32_t)-d : (uint32_t)d;
        p = fin;
    }
    *pp = p;
    *valeur = v;
    return 1;
}

static int observer_avant(Cpu *c, uint32_t adresse, uint32_t retour) {
    extern int g_apercu_en_cours;
    if (!g_n_observees || !observee(adresse) || g_apercu_en_cours || g_observer_n >= g_observer_max) return 0;
    static long long depuis = -1;                                  /* SFC_OBSERVER_DEPUIS=image */
    if (depuis < 0) { const char *e = getenv("SFC_OBSERVER_DEPUIS"); depuis = e ? atoll(e) : 0; }
    if ((long long)hw_frames() < depuis) return 0;
    g_observer_n++;
    rt_log("observer : image %llu, 0x%08X appelée depuis 0x%08X ; r0=%08X r1=%08X r2=%08X r3=%08X ; pile %08X %08X %08X %08X",
           (unsigned long long)hw_frames(), adresse, retour, c->r[0], c->r[1], c->r[2], c->r[3],
           ldr_word(c, c->r[13]), ldr_word(c, c->r[13] + 4), ldr_word(c, c->r[13] + 8), ldr_word(c, c->r[13] + 12));
    /* SFC_OBSERVER_CHAINE=1 : les adresses de retour des appels en cours, de la plus proche à la plus lointaine */
    static int chaine = -1;
    if (chaine < 0) chaine = getenv("SFC_OBSERVER_CHAINE") != NULL;
    if (chaine) {
        char ligne[512];
        int n = 0;
        for (uint32_t i = c->depth; i > 0 && i + 16 > c->depth && n < (int)sizeof ligne - 12; i--)
            if (i - 1 < 512) n += snprintf(ligne + n, sizeof ligne - n, " %08X", c->shadow[i - 1]);
        ligne[n] = 0;
        rt_log("observer :    chaîne%s", ligne);
    }
    /* SFC_OBSERVER_LIRE=rN+décalage:mots[,…] — ce que pointent les arguments (une structure passée par
       adresse ne dit rien par son adresse seule). Une adresse fixe (0x…) vaut aussi, et des crochets
       suivent un pointeur : [[r0+0xC]]+0x20:3 lit trois mots 0x20 octets après ce que désigne le mot
       que désigne r0+0xC. */
    static const char *lire = (const char *)-1;
    if (lire == (const char *)-1) lire = getenv("SFC_OBSERVER_LIRE");
    for (const char *p = lire; p && *p;) {
        const char *debut = p;
        long mots = 4;
        uint32_t base;
        if (!observer_adresse(c, &p, &base)) break;
        if (*p == ':') { char *fin; mots = strtol(p + 1, &fin, 0); p = fin; }
        if (mots < 1) break;
        if (mots > 16) mots = 16;
        char ligne[256];
        int n = 0;
        for (long k = 0; k < mots; k++) n += snprintf(ligne + n, sizeof ligne - n, " %08X", ldr_word(c, base + 4 * (uint32_t)k));
        const char *fin_expr = strchr(debut, ':');
        if (!fin_expr || fin_expr > p) fin_expr = p;
        rt_log("observer :    %.*s (0x%08X) =%s", (int)(fin_expr - debut), debut, base, ligne);
        if (*p == ',') p++; else break;
    }
    return 1;
}

void natif_appel_entree(Cpu *c, const RecompEntry *cible, const Reecriture *secondaire,
                        uint32_t adresse, uint32_t retour) {
    if (g_n_observees && observer_avant(c, adresse, retour)) {
        c->r[14] = retour;
        if (secondaire && (secondaire->suivi->surveillee || secondaire->suivi->en_service)) {
            RT_CALL(c, retour, natif_secondaire_lancer(c, secondaire));
        } else if (!cible) { RT_CALL(c, retour, rt_call(c, adresse)); }
        else { RT_CALL(c, retour, cible->fn(c, adresse)); }
        rt_log("observer :    0x%08X rend r0=%08X r1=%08X", adresse, c->r[0], c->r[1]);
        return;
    }
    c->r[14] = retour;
    if (secondaire && (secondaire->suivi->surveillee || secondaire->suivi->en_service)) {
        RT_CALL(c, retour, natif_secondaire_lancer(c, secondaire));
        return;
    }
    if (!cible) { RT_CALL(c, retour, rt_call(c, adresse)); return; }
    RT_CALL(c, retour, cible->fn(c, adresse));
}

/* L'appel qui passe par le dispatch : celui d'un registre (blx rX), et celui d'une cible que
   la traduction ne lie pas directement — un autre overlay, ou l'autre mode. Le dispatch pose
   le bit T d'après l'adresse, et le code appelé le voit : il faut donc emprunter le même
   chemin que la traduction, et le générateur reproduit sa règle pour choisir. */
void natif_appel_dispatch(Cpu *c, uint32_t adresse, uint32_t retour) {
    if (g_n_observees && observer_avant(c, adresse, retour)) {
        c->r[14] = retour;
        RT_CALL(c, retour, rt_call(c, adresse));
        rt_log("observer :    0x%08X rend r0=%08X r1=%08X", adresse, c->r[0], c->r[1]);
        return;
    }
    c->r[14] = retour;
    RT_CALL(c, retour, rt_call(c, adresse));
}


/* Un changement de thread pendant un appel peut reprendre la fonction appelante ailleurs
   qu'après cet appel : le commutateur laisse dans c->redirect l'adresse de reprise. La
   traduction connaît toutes ses étiquettes ; si la reprise tombe dans la fonction, c'est elle
   qui continue, sinon on passe la main comme elle le ferait. Le mode Thumb reste celui que le
   commutateur a posé : on n'emprunte pas le dispatch, qui le déduirait de l'adresse. */
/* « Suite dans une autre fonction » : un `b` vers elle, ou une chute dans la voisine. La
   traduction l'appelle par son symbole, sans empiler de retour — ce n'est pas un appel. */
void natif_suite(Cpu *c, uint32_t adresse) {
    const RecompEntry *e = rt_find(c, adresse);
    if (e) { e->fn(c, adresse & ~1u); return; }
    c->r[15] = adresse;
    rt_jump(c, adresse);
}

void natif_reprendre(Cpu *c, uint32_t entree) {
    uint32_t ou = c->redirect;
    c->redirect = 0;
    const RecompEntry *e = rt_find(c, entree | 1u);
    if (e && e->start <= ou && ou < e->end) {
        /* un thread qui reprend retombe au milieu de la fonction : à la réécriture qui part de là */
        const Reecriture *secondaire = natif_secondaire(e->region, e->entry, ou);
        if (secondaire && natif_secondaire_lancer(c, secondaire)) return;
        e->fn(c, ou);
        return;
    }
    rt_jump(c, ou);
}

/* Le jeu compte les références de ses objets : un mot à l'offset 4 dit combien de gens
 * tiennent celui-ci. Rendre sa référence, c'est décrémenter ce mot, et rendre l'objet à son
 * allocateur quand plus personne ne le tient. C'est la fonction la plus appelée du jeu — six
 * cent dix-huit appels depuis le seul overlay des acteurs.
 *
 * 0208940C  push {lr} / sub sp,#4        le cadre est vide : elle n'a besoin de rien
 *           r1 = r0                      l'objet
 *           if (r1 == 0) -> fin
 *           r0 = [r1+4] - 1 ; [r1+4] = r0
 *           if (r0 != 0) -> fin
 *           r0 = [0x020E0F80]            l'allocateur, dans une variable globale
 *           bl 0x01FFEDE4                lui rendre l'objet
 *     fin:  add sp,#4 / pop {pc}
 */
static void objet_relacher(Cpu *c) {
    uint32_t objet = c->r[0];
    uint32_t empile[1] = {c->r[14]};
    uint32_t sp = cadre_ouvrir(c, empile, 1, 4);   /* push {lr} ; sub sp, #4 */
    c->r[1] = objet;
    if (!objet) { drapeaux_cmp0(c, objet); cadre_fermer(c, sp, NULL, 0); return; }   /* pop {pc} */
    uint32_t restantes = mem_read32(c, objet + 4) - 1;
    mem_write32(c, objet + 4, restantes);
    c->r[0] = restantes;
    if (restantes) { drapeaux_cmp0(c, restantes); cadre_fermer(c, sp, NULL, 0); return; }
    drapeaux_cmp0(c, objet);           /* le dernier test avant l'appel */
    c->r[0] = mem_read32(c, 0x020E0F80u);
    appel_direct(c, 0x01FFEDE4u | 1u, 0x0208942Fu);
    cadre_fermer(c, sp, NULL, 0);
}

/* L'allocateur du jeu tient, pour chaque classe de taille, une liste de blocs libres chaînés
 * par leur premier mot. Allouer, c'est prendre la tête de la liste ; si elle est vide, un
 * étage au-dessus va chercher de la place ailleurs. Le tout interruptions coupées : le
 * coprocesseur et les interruptions allouent aussi.
 *
 * 01FFEE38  push {r4,r5,lr}
 *           r4 = OS_DisableInterrupts()          0x0203003C, en ARM
 *           index = [gestionnaire+0x1B]          la classe de taille en cours
 *           base  = [gestionnaire+8]             le tableau des listes, 0x1C octets par classe
 *           bloc  = [base + 0x1C*index]          la tête de liste
 *           if (bloc)  [base + 0x1C*index] = [bloc]      la tête passe au suivant
 *           if (!bloc) bloc = 0x02088E50(base, index, 0x1C, &tête)
 *           OS_RestoreInterrupts(r4)             0x02030050, en ARM
 *           return bloc
 */
static void pool_allouer(Cpu *c) {
    uint32_t gestionnaire = c->r[0];
    uint32_t retour = c->r[14];
    uint32_t empile[3] = {c->r[4], c->r[5], retour};
    uint32_t sp = cadre_ouvrir(c, empile, 3, 4);   /* push {r4,r5,lr} ; sub sp, #4 */
    appel_direct(c, 0x0203003Cu, 0x01FFEE43u);     /* ARM ; le blx pose le retour */
    uint32_t garde = c->r[0];

    uint32_t index = mem_read8(c, gestionnaire + 0x1Bu);
    uint32_t base = mem_read32(c, gestionnaire + 8u);
    uint32_t tete = base + 0x1Cu * index;
    uint32_t bloc = mem_read32(c, tete);

    /* Les registres sont posés comme la traduction les laisse : ce qui suit les lit. */
    c->r[0] = base;
    c->r[1] = index;
    c->r[2] = 0x1Cu;
    c->r[3] = tete;
    if (bloc) {
        uint32_t suivant = mem_read32(c, bloc);
        mem_write32(c, tete, suivant);
        c->r[2] = suivant;
    } else {
        appel_direct(c, 0x02088E50u | 1u, 0x01FFEE67u);
        bloc = c->r[0];
    }

    c->r[0] = garde;
    drapeaux_adds0(c, garde);                      /* adds r0, r4, #0 : la garde passe par l'addition */
    appel_direct(c, 0x02030050u, 0x01FFEE6Fu);     /* ARM ; retour en Thumb, bit 0 levé */
    c->r[0] = bloc;
    drapeaux_adds0(c, bloc);                       /* adds r0, r5, #0 : la valeur rendue repasse par l'addition */
    { static const int sauves[] = {4, 5};
      cadre_fermer(c, sp, sauves, 2); }           /* pop {r4,r5,pc} */
}

/* La jumelle de l'allocateur : rendre un bloc. Le gestionnaire vérifie d'abord que le bloc
 * lui appartient — il connaît le début de sa zone, le nombre de blocs et leur taille — puis
 * le remet en tête de sa liste libre. Un bloc venu d'ailleurs est confié à l'étage du dessus.
 *
 * 01FFEDE4  push {r4,r5,r6,r7,lr}
 *           if (bloc == 0) -> fin
 *           r7 = OS_DisableInterrupts()
 *           classe = [gestionnaire+8] + 0x1C * [gestionnaire+0x1B]
 *           début  = [classe+4] ; fin = début + [classe+0xC] * [classe+0x10]
 *           if (début <= bloc && bloc < fin)  [bloc] = [classe] ; [classe] = bloc
 *           else                              0x02088DF8(base, bloc)
 *           OS_RestoreInterrupts(r7)
 */
static void pool_liberer(Cpu *c) {
    uint32_t gestionnaire = c->r[0];
    uint32_t bloc = c->r[1];
    uint32_t retour = c->r[14];
    uint32_t empile[5] = {c->r[4], c->r[5], c->r[6], c->r[7], retour};
    uint32_t sp = cadre_ouvrir(c, empile, 5, 4);   /* push {r4..r7,lr} ; sub sp, #4 */

    drapeaux_cmp0(c, bloc);                        /* cmp r4, #0 */
    if (!bloc) { static const int s0[] = {4, 5, 6, 7}; cadre_fermer(c, sp, s0, 4); return; }

    appel_direct(c, 0x0203003Cu, 0x01FFEDF5u);
    uint32_t garde = c->r[0];

    uint32_t index = mem_read8(c, gestionnaire + 0x1Bu);
    uint32_t base = mem_read32(c, gestionnaire + 8u);
    uint32_t classe = base + 0x1Cu * index;
    uint32_t debut = mem_read32(c, classe + 4u);

    /* les registres tels que le code les laisse en chemin : la suite les lit */
    c->r[0] = 0x1Cu;
    c->r[1] = 0x1Cu * index;
    c->r[2] = index;
    c->r[3] = debut;

    int rendu = 0;
    if (bloc >= debut) {
        uint32_t nombre = mem_read32(c, classe + 0xCu);
        uint32_t taille = mem_read32(c, classe + 0x10u);
        c->r[1] = nombre * taille;
        c->r[0] = debut + nombre * taille;
        if (bloc < debut + nombre * taille) {
            mem_write32(c, bloc, mem_read32(c, classe));   /* le bloc pointe l'ancienne tête */
            mem_write32(c, classe, bloc);                  /* et devient la tête */
            rendu = 1;
        }
    }
    if (!rendu) {
        c->r[0] = base;
        c->r[1] = bloc;
        appel_direct(c, 0x02088DF8u | 1u, 0x01FFEE2Du);
    }

    c->r[0] = garde;
    drapeaux_adds0(c, garde);                      /* adds r0, r7, #0 */
    appel_direct(c, 0x02030050u, 0x01FFEE33u);
    { static const int sauves[] = {4, 5, 6, 7};
      cadre_fermer(c, sp, sauves, 4); }           /* pop {r4,r5,r6,r7,pc} */
}

/* Quand un bloc ne vient pas de la classe qu'on croyait, on cherche parmi les suivantes
 * celle dont la zone le contient, et on l'y remet. Passé la dix-septième, il ne vient
 * d'aucune : c'est qu'il avait été pris sur le tas, et on l'y rend — non sans avoir formé
 * son nom, « DynObject:%d », qu'un journal disparu devait afficher.
 *
 * 02088DF8  push {r4..r7,lr} ; sub sp, #0x24
 *           classe = base + 0x1C*index ; boucle :
 *             classe += 0x1C ; index++
 *             si le bloc tombe dans [début, début + nombre*taille) : l'y chaîner, fini
 *             sinon, si index >= 16 : sprintf(tampon, "DynObject:%d", index) ; OS_Free(1, bloc)
 *             et l'on recommence
 */
static void pool_rendre_a_sa_classe(Cpu *c) {
    uint32_t base = c->r[0], bloc = c->r[1], index = c->r[2];
    uint32_t empile[5] = {c->r[4], c->r[5], c->r[6], c->r[7], c->r[14]};
    uint32_t sp = cadre_ouvrir(c, empile, 5, 0x24);
    uint32_t classe = base + 0x1Cu * index;

    for (int premier_tour = 1; ; premier_tour = 0) {
        if (!premier_tour && index >= 0x10u) {
            /* le bloc n'appartient à aucune classe : il vient du tas */
            c->r[0] = c->r[13];                    /* add r0, sp, #0 : le tampon du cadre */
            c->r[1] = 0x020B05FCu;                 /* "DynObject:%d" */
            c->r[2] = index;
            appel_direct(c, 0x0202D8E0u, 0x02088E19u);     /* le formatage, en ARM */
            c->r[0] = 1;                                   /* le tas numéro un */
            c->r[1] = bloc;
            appel_direct(c, 0x0203D7F4u | 1u, 0x02088E21u);
        }
        classe += 0x1Cu;
        index++;
        uint32_t debut = mem_read32(c, classe + 4u);
        c->r[2] = debut;
        int case_trouvee = 0;
        if (bloc >= debut) {
            uint32_t nombre = mem_read32(c, classe + 0xCu);
            uint32_t taille = mem_read32(c, classe + 0x10u);
            c->r[1] = nombre * taille;
            c->r[0] = debut + nombre * taille;
            if (bloc < debut + nombre * taille) {
                mem_write32(c, bloc, mem_read32(c, classe));
                mem_write32(c, classe, bloc);
                case_trouvee = 1;
            }
        }
        c->r[0] = case_trouvee ? 1u : 0u;
        drapeaux_cmp0(c, c->r[0]);
        if (case_trouvee) break;
    }

    { static const int sauves[] = {4, 5, 6, 7};
      cadre_fermer(c, sp, sauves, 4); }           /* pop {r4,r5,r6,r7,pc} */
}

/* La fonction que le jeu appelle le plus souvent par pointeur : quarante-deux mille fois en
 * neuf cents images. Elle ne fait qu'adapter des arguments avant une copie mémoire — c'est la
 * méthode « écrire » d'un flux qui vise la mémoire, là où d'autres visent une carte ou un
 * tampon.
 *
 * 0202260C  push {lr} ; sub sp, #4
 *           r0 = r2 ; r2 = r3          la source devient le premier argument, la taille le troisième
 *           blx 0x0202C3B4             la copie proprement dite
 *           r0 = 0                     elle rend toujours zéro
 *           add sp, #4 ; pop {r3} ; bx r3
 */
static void flux_memoire_ecrire(Cpu *c) {
    uint32_t empile[1] = {c->r[14]};
    uint32_t sp = cadre_ouvrir(c, empile, 1, 4);
    /* « adds » et non « mov » : ces deux transferts posent les drapeaux, et la copie qui suit
       est du code ARM, donc truffé d'instructions conditionnelles. Les oublier ne change pas
       les arguments — cela change ce que la copie fait de ses arguments. */
    c->r[0] = c->r[2];
    drapeaux_adds0(c, c->r[0]);
    c->r[2] = c->r[3];
    drapeaux_adds0(c, c->r[2]);
    appel_direct(c, 0x0202C3B4u, 0x02022619u);     /* ARM */
    c->r[0] = 0;
    c->nf = 0; c->zf = 1;                          /* movs r0, #0 : N et Z seulement */
    cadre_fermer_par(c, sp, NULL, 0, 3);           /* pop {r3} ; bx r3 */
}

/* Overlay des acteurs, mille deux cent quatre-vingt-huit appels : un prédicat sur deux
 * drapeaux d'un octet. Ce qu'ils signifient reste à établir — la fonction ne dit que leur
 * adresse — mais sa forme ne laisse aucun doute :
 *
 * 021C80F4  r0 = [[pc+0x14]] ; si r0 != 0 -> rendre 1
 *           r0 = [[pc+0x10]] ; si r0 == 0 -> rendre 0
 *           rendre 1
 *
 * Les deux adresses sont lues dans le pool de littéraux comme le fait la traduction, et non
 * écrites en dur : c'est la même chose tant que personne ne récrit le code, et ce n'est pas à
 * une réécriture d'en décider.
 */
static void ovl0_un_des_deux_drapeaux(Cpu *c) {
    c->r[0] = mem_read8(c, mem_read32(c, 0x021C810Cu));
    drapeaux_cmp0(c, c->r[0]);
    if (c->zf) {
        c->r[0] = mem_read8(c, mem_read32(c, 0x021C8110u));
        drapeaux_cmp0(c, c->r[0]);
        if (c->zf) {
            c->r[0] = 0;
            c->nf = 0; c->zf = 1;                  /* movs r0, #0 */
            c->r[15] = c->r[14];
            return;
        }
    }
    c->r[0] = 1;
    c->nf = 0; c->zf = 0;                          /* movs r0, #1 */
    c->r[15] = c->r[14];                           /* bx lr, que la traduction tient pour un retour */
}

/* Le motif que tout compilateur C++ produit pour une variable statique locale : un drapeau de
 * garde, une construction au premier passage, l'enregistrement du destructeur, puis on rend
 * toujours la même instance. Huit cent quarante-trois appels dans l'overlay des acteurs.
 *
 * 021A9908  si (garde & 1) -> rendre l'instance
 *           construire(instance)                                    0x021AA7F0
 *           enregistrer_destruction(instance, 0x021A9948, poignée)  0x020128A0, en ARM
 *           garde |= 1
 *           rendre l'instance
 *
 * Le banc ne peut pas la juger au premier appel : le constructeur envoie un message au
 * coprocesseur, et la file de communication vit hors de la mémoire que le banc restaure. La
 * seconde exécution y trouve la réponse de la première. La preuve vient des empreintes, qui
 * n'exécutent qu'une fois.
 */
static void ovl0_instance_unique(Cpu *c) {
    uint32_t empile[1] = {c->r[14]};
    uint32_t sp = cadre_ouvrir(c, empile, 1, 4);

    uint32_t garde = mem_read32(c, 0x021A9938u);        /* les littéraux, lus comme le fait */
    uint32_t instance = mem_read32(c, 0x021A993Cu);     /* la traduction */
    uint32_t etat = mem_read32(c, garde);

    c->r[0] = 1;
    c->r[1] = etat & 1u;
    c->nf = 0; c->zf = !(etat & 1u);                    /* ands r1, r0 */
    drapeaux_cmp0(c, c->r[1]);                          /* cmp r1, #0 */

    if (!(etat & 1u)) {
        c->r[0] = instance;
        appel_direct(c, 0x021AA7F0u | 1u, 0x021A991Fu);
        c->r[0] = instance;
        c->r[1] = mem_read32(c, 0x021A9940u);           /* le destructeur */
        c->r[2] = mem_read32(c, 0x021A9944u);           /* la poignée de module */
        appel_direct(c, 0x020128A0u, 0x021A9929u);      /* ARM */
        c->r[1] = garde;
        uint32_t nouveau = mem_read32(c, garde) | 1u;
        c->r[0] = 1;
        c->r[2] = nouveau;
        c->nf = nouveau >> 31; c->zf = !nouveau;        /* orrs r2, r0 */
        mem_write32(c, garde, nouveau);
    }

    c->r[0] = instance;
    cadre_fermer(c, sp, NULL, 0);
}

/* Les trois fonctions les plus appelées en vol, toutes dans l'overlay des acteurs. Ce sont des
 * accesseurs : ils ne calculent rien, mais ils disent la forme des objets.
 *
 * Pour les drapeaux, on reprend ici les fonctions mêmes du traducteur plutôt que les aides
 * du haut de ce fichier : ces trois-là posent des retenues d'addition, qu'il vaut mieux ne pas
 * recalculer à la main. */

/* 021B3FD0, en ARM, quatre cent vingt-deux mille appels : le premier élément d'une liste. La
 * liste est à l'offset 0x28 de l'objet ; chaque nœud est inclus dans son élément, 0x1C octets
 * après son début — d'où le « sub 0x1C » qui passe du nœud à l'élément. Rend zéro si la liste
 * n'existe pas ou si elle est vide. */
static void ovl0_premier_de_liste(Cpu *c) {
    c->r[0] = ldr_word(c, c->r[0] + 0x28u);
    flags_sub(c, c->r[0], 0u, 1u, c->r[0]);            /* cmp r0, #0 */
    if (c->zf) {
        c->r[0] = 0;                                   /* mov r0, #0 : pas de drapeaux en ARM */
        c->r[15] = c->r[14];
        return;
    }
    c->r[0] = ldr_word(c, c->r[0]);
    flags_sub(c, c->r[0], 0u, 1u, c->r[0]);
    if (!c->zf) c->r[0] -= 0x1Cu;                      /* subne : pas de drapeaux non plus */
    c->r[15] = c->r[14];
}

/* 021B594C, en Thumb, deux cent quarante-sept mille appels : l'octet à l'offset 0xB8 est-il nul ? */
static void ovl0_octet_b8_nul(Cpu *c) {
    uint32_t adresse = c->r[0] + 0xB8u;
    flags_add(c, c->r[0], 0xB8u, 0u, adresse);         /* adds r0, #0xB8 */
    c->r[0] = mem_read8(c, adresse);
    flags_sub(c, c->r[0], 0u, 1u, c->r[0]);            /* cmp r0, #0 */
    c->r[0] = c->zf ? 1u : 0u;
    SET_NZ(c, c->r[0]);                                /* movs : N et Z, la retenue reste */
    c->r[15] = c->r[14];
}

/* 0216DADC, en Thumb, deux cent onze mille appels : suivre deux pointeurs, puis lire un
 * octet — celui de l'offset 0x4C, dans l'objet que désigne le premier mot du champ 0x24. */
static void ovl0_octet_4c_indirect(Cpu *c) {
    uint32_t p = mem_read32(c, mem_read32(c, c->r[0] + 0x24u));
    uint32_t adresse = p + 0x4Cu;
    flags_add(c, p, 0x4Cu, 0u, adresse);               /* adds r0, #0x4C */
    c->r[0] = mem_read8(c, adresse);
    c->r[15] = c->r[14];
}

/* ------------------------------------------------------------------ la boucle principale
 *
 * 020A0E4C est le cœur battant du jeu : un tour de boucle. Elle prend la scène au sommet
 * d'une pile de scènes (« objet+0x108 », hauteur en « +0x188 »), appelle sa huitième méthode
 * virtuelle — celle qui fait vivre et dessine la scène —, puis mesure combien de temps le tour
 * a pris, en millisecondes, et le range en « +0x1A4 ».
 *
 * Et si l'octet « +0x1A3 » est levé, elle **attend** : elle relit l'horloge en boucle jusqu'à
 * ce que vingt millisecondes se soient écoulées depuis le repère de « +0x1A8 ». Dans les menus
 * l'octet est nul et le jeu suit le balayage, soixante tours par seconde. En vol le jeu le
 * lève lui-même — la console n'aurait pas tenu soixante — et vingt millisecondes dépassant les
 * seize d'une image, il ne fait plus qu'un tour toutes les deux images : le combat se pense
 * trente fois par seconde. Cette attente, faite de lectures d'horloge, occupe à elle seule plus
 * de la moitié du temps du processeur principal en vol.
 *
 * La conversion est celle du SDK : des tiques de 64 cycles d'un bus à 33 514 kHz, donc
 * millisecondes = (tiques << 6) / 33514, en soixante-quatre bits.
 *
 * SFC_PAS_MS=n remplace le seuil de vingt millisecondes. Ce n'est plus alors une réécriture
 * fidèle mais une expérience — la première question à poser avant de débloquer la cadence
 * étant de savoir ce que devient le jeu quand on le laisse penser plus souvent.
 */
static uint32_t g_pas_ms = 20;
/* SFC_ATTENTE_DIRECTE=1 : pendant l'attente, laisser passer le temps qui manque au lieu de
   relire l'horloge des milliers de fois. La dernière milliseconde reste au code du jeu, qui
   décide donc lui-même de l'instant de sortie. Le temps simulé s'écoule au même rythme ; ce
   qui change, c'est le travail que la machine hôte fait pendant ce temps — aucun. */
static int g_attente_directe;

/* la durée écoulée depuis le repère, telle que le code la calcule : registres et drapeaux */
static void boucle_mesurer(Cpu *c, uint32_t bas, uint32_t haut, uint32_t retour_division) {
    uint32_t res, sc;
    c->r[3] = ldr_word(c, c->r[5] + bas);
    c->r[2] = ldr_word(c, c->r[5] + haut);
    res = c->r[0] - c->r[3];
    flags_sub(c, c->r[0], c->r[3], 1u, res);
    c->r[3] = res;
    res = c->r[1] - c->r[2] - (1u - c->cf);
    flags_sub(c, c->r[1], c->r[2], c->cf, res);
    c->r[1] = res;
    sc = (c->r[3] >> 25) & 1u; res = c->r[3] >> 26; SET_NZ(c, res); c->cf = sc; c->r[0] = res;
    sc = (c->r[1] >> 26) & 1u; res = c->r[1] << 6;  SET_NZ(c, res); c->cf = sc; c->r[1] = res;
    sc = c->cf;                res = c->r[1] | c->r[0]; SET_NZ(c, res); c->cf = sc; c->r[1] = res;
    sc = (c->r[3] >> 26) & 1u; res = c->r[3] << 6;  SET_NZ(c, res); c->cf = sc; c->r[0] = res;
    (void)retour_division;
}

static int g_apercu_en_cours_decl_avant;   /* (voir plus bas : les tours d'aperçu) */
extern int g_apercu_en_cours;
static void apercu_capture_et_sortie(void);
static void apercu_attente_debut(Cpu *c);
static void apercu_attente_fin(void);

static void boucle_principale(Cpu *c) {
    uint32_t res, sc;
    extern uint64_t g_tour_jeu;
    (void)g_apercu_en_cours_decl_avant;
    if (!g_apercu_en_cours) {
        g_tour_jeu++;
        extern void apercu_etude_mouvement(void);
        apercu_etude_mouvement();
    }
    uint32_t empile[5] = {c->r[4], c->r[5], c->r[6], c->r[7], c->r[14]};
    uint32_t sp = cadre_ouvrir(c, empile, 5, 4);   /* push {r4-r7,lr} ; sub sp, #4 */

    res = c->r[0];
    flags_add(c, c->r[0], 0u, 0u, res);
    c->r[5] = res;                                 /* l'objet */

    /* la scène au sommet de la pile, et sa méthode */
    c->r[0] = ldr_word(c, c->r[5] + ldr_word(c, 0x020A0EC4u));          /* +0x188 : hauteur */
    res = c->r[0] - 1u;
    flags_sub(c, c->r[0], 1u, 1u, res);
    c->r[0] = res;
    sc = (c->r[0] >> 30) & 1u; res = c->r[0] << 2; SET_NZ(c, res); c->cf = sc; c->r[0] = res;
    res = c->r[5] + c->r[0];
    flags_add(c, c->r[5], c->r[0], 0u, res);
    c->r[1] = res;
    c->r[0] = ldr_word(c, c->r[1] + ldr_word(c, 0x020A0EC8u));          /* +0x108 : la pile */
    c->r[1] = ldr_word(c, ldr_word(c, c->r[0]) + 0x1Cu);                /* vtable[7] */
    {
        uint32_t methode = c->r[1];
        /* SFC_DIAG_TOUR=1 : ce que fait un tour — attentes d'interruption, commutations de threads, temps */
        static int diag = -1;
        if (diag < 0) diag = getenv("SFC_DIAG_TOUR") != NULL;
        extern uint64_t g_nb_commutations; extern uint64_t rt_nb_attentes(void);
        uint64_t com0 = g_nb_commutations, att0 = rt_nb_attentes(), cyc0 = hw_now();
        LARGE_INTEGER q0, q1, fq;
        if (diag) QueryPerformanceCounter(&q0);
        c->r[14] = 0x020A0E67u;
        RT_CALL(c, 0x020A0E67u, rt_call(c, methode));
        if (diag) {
            QueryPerformanceCounter(&q1); QueryPerformanceFrequency(&fq);
            static uint64_t n;
            if (++n % 60 == 0 || g_nb_commutations != com0 || rt_nb_attentes() != att0)
                rt_log("tour %llu (image %llu) : méthode 0x%08X, %.2f ms, %llu cycles simulés, %llu commutations, %llu attentes",
                       (unsigned long long)n, (unsigned long long)hw_frames(), methode,
                       (double)(q1.QuadPart - q0.QuadPart) * 1000.0 / (double)fq.QuadPart,
                       (unsigned long long)(hw_now() - cyc0), (unsigned long long)(g_nb_commutations - com0),
                       (unsigned long long)(rt_nb_attentes() - att0));
        }
    }

    c->r[0] = mem_read8(c, c->r[5] + ldr_word(c, 0x020A0ECCu));         /* +0x1A3 : limiteur */
    /* En vol le jeu a fini son image et va attendre vingt millisecondes : l'affichage libre la prend dès
       maintenant (video.cpp). Dans un tour d'aperçu, c'est l'image qu'on attendait : on la prend et l'on sort. */
    if (g_apercu_en_cours) apercu_capture_et_sortie();
    if (c->r[0]) { extern void video_publier_apercu(void); video_publier_apercu(); }
    {   /* SFC_DIAG_BRIDE=1 : l'image 3D en construction au moment où l'attente commence */
        static int diag = -1;
        if (diag < 0) diag = getenv("SFC_DIAG_BRIDE") != NULL;
        if (diag) {
            static uint64_t n;
            const GxFrame *b = gx_geom_building();
            if (++n % 30 == 0) rt_log("attente de tour (limiteur %u) à l'image %llu : %zu polygones déjà construits",
                                      c->r[0], (unsigned long long)hw_frames(), b->count);
        }
    }
    flags_sub(c, c->r[0], 0u, 1u, c->r[0]);
    if (!c->zf) {
        c->r[7] = ldr_word(c, 0x020A0ED0u);        /* 0x1A8 : repère, mot bas */
        c->r[6] = ldr_word(c, 0x020A0ED4u);        /* 0x1AC : repère, mot haut */
        c->r[4] = ldr_word(c, 0x020A0ED8u);        /* 0x1A4 : durée du tour */
        apercu_attente_debut(c);
        /* L'horloge et la division sont dans la partie fixe de l'ARM9 : leur fonction ne change jamais, on la
           cherche une fois. Ces deux appels se répètent des milliers de fois par image. */
        for (;;) {
            c->r[14] = 0x020A0E79u;
            { static const RecompEntry *e; if (!e) e = rt_find(c, 0x0202F904u);
              RT_CALL(c, 0x020A0E79u, e->fn(c, 0x0202F904u)); }        /* l'horloge */
            boucle_mesurer(c, c->r[7], c->r[6], 0);
            c->r[2] = ldr_word(c, 0x020A0EDCu);    /* 33514 */
            sc = c->cf; SET_NZ(c, 0u); c->cf = sc; c->r[3] = 0;
            c->r[14] = 0x020A0E91u;
            { static const RecompEntry *e; if (!e) e = rt_find(c, 0x0200FD48u);
              RT_CALL(c, 0x020A0E91u, e->fn(c, 0x0200FD48u)); }        /* la division */
            mem_write32(c, c->r[5] + c->r[4], c->r[0]);
            c->r[0] = ldr_word(c, c->r[5] + c->r[4]);
            res = c->r[0] - g_pas_ms;
            flags_sub(c, c->r[0], g_pas_ms, 1u, res);
            if (c->nf == c->vf) break;             /* assez attendu */
            if (g_attente_directe && c->r[0] + 1u < g_pas_ms)
                rt_laisser_passer(c, (uint64_t)(g_pas_ms - c->r[0] - 1u) * 33514u);
            if (--g_edge_budget < 0) rt_edge(c);   /* l'arête arrière : c'est elle qui fait passer le temps */
        }
        apercu_attente_fin();
    } else {
        c->r[14] = 0x020A0E9Fu;
        { static const RecompEntry *e; if (!e) e = rt_find(c, 0x0202F904u);
          RT_CALL(c, 0x020A0E9Fu, e->fn(c, 0x0202F904u)); }
        c->r[2] = ldr_word(c, 0x020A0ED0u);
        boucle_mesurer(c, c->r[2], ldr_word(c, 0x020A0ED4u), 0);
        /* le code charge r2 deux fois : d'abord 0x1A8, puis 0x1AC, avant d'y lire le mot haut —
           boucle_mesurer a déjà tout lu, il ne reste qu'à rendre à r2 sa dernière valeur */
        c->r[2] = ldr_word(c, 0x020A0EDCu);
        sc = c->cf; SET_NZ(c, 0u); c->cf = sc; c->r[3] = 0;
        c->r[14] = 0x020A0EBBu;
        { static const RecompEntry *e; if (!e) e = rt_find(c, 0x0200FD48u);
          RT_CALL(c, 0x020A0EBBu, e->fn(c, 0x0200FD48u)); }
        c->r[1] = ldr_word(c, 0x020A0ED8u);
        mem_write32(c, c->r[5] + c->r[1], c->r[0]);
    }

    { static const int sauves[] = {4, 5, 6, 7};
      cadre_fermer(c, sp, sauves, 4); }
}

/* Les réécritures établies, et rien d'autre : une ligne n'entre ici qu'accompagnée de ce
   qui la prouve. Le numéro de tête donne à chacune son crochet — le code traduit appelle un
   crochet sans lui dire quelle fonction il remplace, et un crochet par fonction règle la
   question sans rien changer aux huit millions de lignes du code généré. */
#define LES_REECRITURES(X) \
    X(0, "arm9", 0x0208940C, objet_relacher, \
      "décrémente [objet+4] et rend l'objet à l'allocateur quand le compte tombe à zéro") \
    X(1, "arm9", 0x01FFEE38, pool_allouer, \
      "prend la tête de la liste libre de la classe de taille en cours, interruptions coupées") \
    X(2, "arm9", 0x01FFEDE4, pool_liberer, \
      "remet le bloc en tête de sa liste s'il vient de la zone de cette classe, sinon le passe au-dessus") \
    X(3, "arm9", 0x02088DF8, pool_rendre_a_sa_classe, \
      "cherche parmi les classes suivantes celle dont la zone contient le bloc ; au-delà, rend au tas") \
    X(4, "arm9", 0x0202260C, flux_memoire_ecrire, \
      "réordonne ses arguments et appelle la copie mémoire ; rend zéro") \
    X(5, "ovl0", 0x021C80F4, ovl0_un_des_deux_drapeaux, \
      "rend 1 si l'un des deux drapeaux d'octet est levé, 0 sinon") \
    X(6, "ovl0", 0x021A9908, ovl0_instance_unique, \
      "construit son instance au premier appel, la rend ensuite — prouvée par les empreintes") \
    X(7, "ovl0", 0x021B3FD0, ovl0_premier_de_liste, \
      "premier élément de la liste à +0x28, nœud inclus 0x1C octets après le début de l'élément") \
    X(8, "ovl0", 0x021B594C, ovl0_octet_b8_nul, \
      "CollidableObject : participe ? — rend 1 si l'octet à +0xB8 est nul") \
    X(9, "ovl0", 0x0216DADC, ovl0_octet_4c_indirect, \
      "rend l'octet +0x4C de l'objet désigné par le premier mot du champ +0x24") \
    X(10, "arm9", 0x020A0E4C, boucle_principale, \
      "un tour de boucle : méthode 7 de la scène du sommet, mesure du tour, attente de 20 ms si +0x1A3")

#define COMPTER(i, region, adresse, fn, preuve) +1
static Surveillance g_suivi_manuel[0 LES_REECRITURES(COMPTER)];
#undef COMPTER

static const Reecriture g_reecritures[];
#define DEFINIR_CROCHET(i, region, adresse, fn, preuve)     static void crochet_##i(Cpu *c) { verifier(c, &g_reecritures[i]); }
LES_REECRITURES(DEFINIR_CROCHET)
#undef DEFINIR_CROCHET

static const Reecriture g_reecritures[] = {
#define LIGNE(i, region, adresse, fn, preuve) {region, adresse, fn, #fn, preuve, crochet_##i, &g_suivi_manuel[i], 0, 1, 0, 0, NULL, 0, 0, NULL, 0},
    LES_REECRITURES(LIGNE)
#undef LIGNE
    {NULL, 0, NULL, NULL, NULL, NULL, NULL, 0, 0, 0, 0, NULL, 0, 0, NULL, 0},
};

/* Les feuilles réécrites par le générateur (tools/decomp_feuilles.py) : des milliers de
   petites fonctions d'une même forme — accesseurs, constantes, relais d'héritage. Le fichier
   est produit à côté du code traduit, et comme lui ne se versionne pas. */
#if __has_include("natif_auto.inc")
#include "natif_auto.inc"          /* NATIF_AUTO_N, et les parties : g_auto_0[] … avec leur taille */
#else
#define NATIF_AUTO_N 0
#define NATIF_AUTO_PARTIES 0
static const Reecriture *const g_auto_parties[1] = {NULL};
static const size_t g_auto_zero = 0;
static const size_t *const g_auto_tailles[1] = {&g_auto_zero};
#endif
/* Celles du coprocesseur, s'il est lié : le même générateur, sa carte à lui (generated7/). */
#if defined(SFC_HAVE_ARM7) && __has_include("natif_auto7.inc")
#include "natif_auto7.inc"
#else
#define NATIF_AUTO7_N 0
#define NATIF_AUTO7_PARTIES 0
static const Reecriture *const g_auto7_parties[1] = {NULL};
static const size_t g_auto7_zero = 0;
static const size_t *const g_auto7_tailles[1] = {&g_auto7_zero};
#endif
const Reecriture *g_auto_reuni;     /* toutes les parties bout à bout, terminées par une entrée nulle */
const Reecriture *g_auto7_reuni;    /* là où commencent, dans la même table, celles du coprocesseur */
Surveillance *g_suivi_auto7;
#define g_auto g_auto_reuni

static void auto_reunir(void) {
    Reecriture *t = calloc(NATIF_AUTO_N + NATIF_AUTO7_N + 1, sizeof *t);
    g_suivi_auto = calloc(NATIF_AUTO_N + NATIF_AUTO7_N + 1, sizeof *g_suivi_auto);
    size_t n = 0;
    for (size_t p = 0; p < NATIF_AUTO_PARTIES; p++)
        for (size_t k = 0; k < *g_auto_tailles[p]; k++) t[n++] = g_auto_parties[p][k];
    g_auto7_reuni = t + n;
    g_suivi_auto7 = g_suivi_auto + n;
    for (size_t p = 0; p < NATIF_AUTO7_PARTIES; p++)
        for (size_t k = 0; k < *g_auto7_tailles[p]; k++) t[n++] = g_auto7_parties[p][k];
    for (size_t k = 0; k < n; k++) {
        t[k].suivi = &g_suivi_auto[k];
        g_suivi_auto[k].chemins = calloc((size_t)(t[k].nb_chemins + 8) / 8, 1);
    }
    g_auto_reuni = t;
    g_secondaires = calloc(n + 1, sizeof *g_secondaires);
    for (size_t k = 0; k < n; k++)
        if (t[k].englobante) g_secondaires[g_nb_secondaires++] = &t[k];
}

/* La région telle que les listes l'écrivent : celle d'une secondaire porte l'entrée de la
   fonction entière — « arm9@0202DD18 » —, deux fonctions qui se chevauchent pouvant être
   rejointes à la même adresse. */
static const char *region_dite(const Reecriture *r) {
    static char tampon[4][48];
    static unsigned tour;
    if (!r->englobante) return r->region;
    char *t = tampon[tour++ & 3u];
    snprintf(t, sizeof tampon[0], "%s@%08X", r->region, r->englobante);
    return t;
}

/* ------------------------------------------------------------------ l'état comparé */

/* Ce qu'une fonction du jeu peut modifier sans toucher au matériel. Les registres
   matériels sont comparés aussi : une réécriture qui écrit dans la mémoire vidéo alors que
   l'originale n'y touche pas doit se voir. */
typedef struct {
    uint32_t r[16];
    uint32_t cpsr_n, cpsr_z, cpsr_c, cpsr_v;
    uint32_t thumb, mode;      /* la traduction peut les changer : sans eux, la réécriture
                                  partirait d'un état que la référence a déjà modifié */
    uint32_t depth;
    /* Le reste du registre d'état — I, F, Q —, le spsr et les banques des autres modes : « mrs »,
       « msr » et les changements de mode les lisent et les écrivent. Sans eux la réécriture
       repartait du bit I que la traduction venait de poser, et les deux fonctions du SDK qui
       masquent les interruptions passaient pour fausses à chaque appel. */
    uint32_t qf;
    uint8_t systeme[offsetof(Cpu, shadow) - offsetof(Cpu, irq_off)];
    int32_t edge_budget;       /* les arêtes qui restent avant la prochaine avance du temps */
    uint32_t shadow[512];      /* la pile fantôme : c'est elle qui dit si un POP {PC} revient */
    /* La mémoire : une image entière, tenue à jour page par page. Le système suit les pages
       écrites (memory.c) ; « perime » marque celles qui l'ont été depuis que cette image a été
       prise ou rendue. Prendre, c'est recopier ces pages-là ; rendre, les remettre. Une feuille
       touche une page ou deux : la comparaison coûtait deux millisecondes de copies, elle en
       coûte cent fois moins. */
    uint8_t *image;            /* MEM_SUIVIE_TAILLE octets, dans l'ordre de la réservation */
    uint8_t *io;
    uint32_t perime[(MEM_SUIVIE_TAILLE / 4096u + 31u) / 32u];
} Etat;

#define TAILLE_RAM  0x400000u
#define TAILLE_ITCM 0x8000u
#define TAILLE_IO   0x2000u
#define PAGE_SUIVIE 4096u
#define NB_PAGES (MEM_SUIVIE_TAILLE / PAGE_SUIVIE)
#define NB_MOTS_PAGES ((NB_PAGES + 31u) / 32u)
/* les pages marquées d'un ensemble, une à une */
#define POUR_CHAQUE_PAGE(bits, p) \
    for (size_t m_ = 0; m_ < NB_MOTS_PAGES; m_++) \
        for (uint32_t b_ = (bits)[m_], p; b_ && (p = (uint32_t)m_ * 32u + (uint32_t)__builtin_ctz(b_)) < NB_PAGES; b_ &= b_ - 1u)

/* Les zones de la réservation, et l'adresse que le jeu leur donne. « pile » : une pile peut y
   vivre, et ce qui est sous son pointeur ne compte pas. */
static const struct { const char *nom; uint32_t offset, base; int pile; } g_zones_suivies[] = {
    {"RAM", 0u, 0x02000000u, 1},
    {"ITCM", MEM_SUIVIE_ITCM, 0x01FF8000u, 1},
    {"DTCM", MEM_SUIVIE_DTCM, 0x027E0000u, 1},
    {"WRAM partagée", MEM_SUIVIE_WRAM, 0x03000000u, 0},
    {"palette", MEM_SUIVIE_PALETTE, 0x05000000u, 0},
    {"OAM", MEM_SUIVIE_OAM, 0x07000000u, 0},
    {"VRAM", MEM_SUIVIE_VRAM, 0x06800000u, 0},
    {"WRAM du coprocesseur", MEM_SUIVIE_WRAM7, 0x03800000u, 1},
};
#define NB_ZONES_SUIVIES (sizeof g_zones_suivies / sizeof *g_zones_suivies)

static size_t zone_de_page(size_t page) {
    size_t z = NB_ZONES_SUIVIES - 1;
    while (z && g_zones_suivies[z].offset > page * PAGE_SUIVIE) z--;
    return z;
}

/* Tous les états vivants : chaque relevé leur dit quelles pages ont bougé. */
static Etat *g_etats[40];
static size_t g_nb_etats;

/* Demande au système les pages écrites depuis le dernier relevé, et les marque périmées dans
   tous les états — sauf celui qu'on vient de rendre, pour qui elles sont justement à jour. */
static void relever(const Etat *sauf) {
    static PVOID adresses[NB_PAGES];
    ULONG_PTR n = NB_PAGES;
    DWORD grain = 0;
    if (GetWriteWatch(WRITE_WATCH_FLAG_RESET, g_mem_suivie, MEM_SUIVIE_TAILLE, adresses, &n, &grain)
        || grain != PAGE_SUIVIE) {
        rt_log("banc : le suivi des pages écrites ne répond pas (pages de %lu octets)", (unsigned long)grain);
        rt_sortir(2);
    }
    for (ULONG_PTR i = 0; i < n; i++) {
        size_t page = (size_t)((uint8_t *)adresses[i] - g_mem_suivie) / PAGE_SUIVIE;
        for (size_t k = 0; k < g_nb_etats; k++)
            if (g_etats[k] != sauf) g_etats[k]->perime[page >> 5] |= 1u << (page & 31u);
    }
}

static void etat_allouer(Etat *e) {
    if (e->image) return;
    if (!g_mem_suivie) {
        rt_log("banc : la mémoire n'est pas suivie — SFC_VERIF ou SFC_VERIF_FROID se posent au lancement");
        rt_sortir(2);
    }
    e->image = malloc(MEM_SUIVIE_TAILLE);
    e->io = malloc(TAILLE_IO);
    memset(e->perime, 0xFF, sizeof e->perime);         /* rien n'est encore pris */
    g_etats[g_nb_etats++] = e;
}

static void etat_prendre(Etat *e, const Cpu *c) {
    memcpy(e->r, c->r, sizeof e->r);
    e->cpsr_n = c->nf; e->cpsr_z = c->zf; e->cpsr_c = c->cf; e->cpsr_v = c->vf;
    e->thumb = c->thumb; e->mode = c->mode;
    e->qf = c->qf;
    memcpy(e->systeme, &c->irq_off, sizeof e->systeme);
    relever(NULL);
    POUR_CHAQUE_PAGE(e->perime, page)
        memcpy(e->image + page * PAGE_SUIVIE, g_mem_suivie + page * PAGE_SUIVIE, PAGE_SUIVIE);
    memset(e->perime, 0, sizeof e->perime);
    memcpy(e->io, g_io, TAILLE_IO);
    e->depth = c->depth;
    e->edge_budget = g_edge_budget;
    memcpy(e->shadow, c->shadow, sizeof e->shadow);
}

static uint32_t g_pages_rendues[NB_MOTS_PAGES];   /* (vérification des aperçus) ce que la dernière restitution a remis */

static void etat_rendre(Etat *e, Cpu *c) {
    memcpy(c->r, e->r, sizeof e->r);
    c->nf = e->cpsr_n; c->zf = e->cpsr_z; c->cf = e->cpsr_c; c->vf = e->cpsr_v;
    c->thumb = e->thumb; c->mode = e->mode;
    c->qf = e->qf;
    memcpy(&c->irq_off, e->systeme, sizeof e->systeme);
    relever(NULL);
    memcpy(g_pages_rendues, e->perime, sizeof g_pages_rendues);
    POUR_CHAQUE_PAGE(e->perime, page)
        memcpy(g_mem_suivie + page * PAGE_SUIVIE, e->image + page * PAGE_SUIVIE, PAGE_SUIVIE);
    relever(e);              /* ce qu'on vient de remettre est périmé pour les autres, pas pour lui */
    memset(e->perime, 0, sizeof e->perime);
    if (g_banc_froid) memcpy(g_io, e->io, TAILLE_IO);   /* en jeu, le temps y écrit seul : on n'y touche pas */
    c->depth = e->depth;
    g_edge_budget = e->edge_budget;
    memcpy(c->shadow, e->shadow, sizeof e->shadow);
}

/* ------------------------------------------------------------------ les tours d'aperçu
 *
 * (Voir runtime/apercu.c.) En vol le jeu finit son tour puis attend vingt millisecondes dans la boucle
 * principale (0x020A0E4C, réécrite plus haut) : c'est là, pendant que la machine hôte attendrait l'heure du
 * balayage, qu'un tour d'aperçu est joué — la suite de la boucle du jeu (0x0203DAE8) jusqu'à ce que le tour
 * suivant, à son tour, ait fini son image : C, l'échange, puis F, G, H, I, A, B, sans l'attente du balayage.
 * Chaque accumulateur n'y avance que de la part de tour écoulée ; le temps de la machine est figé ; aucune
 * interruption n'est livrée, rien ne part vers le coprocesseur. Quand ce tour-là arrive à son tour à
 * l'attente, son image est prise, et l'on saute hors de tout : la mémoire (page par page), les registres, le
 * moteur 3D, la liaison, les minuteries reviennent tels qu'ils étaient. Une commutation de thread, une
 * attente d'interruption, un accès à la cartouche, une boucle sans fin : l'aperçu est abandonné, et effacé
 * de même. */
int g_apercu_en_cours;
static jmp_buf g_apercu_saut;
static Cpu *g_apercu_cpu;                 /* la boucle du jeu attend : c'est d'ici que partent les aperçus */
static uint64_t g_apercu_aretes;
static Etat g_etat_apercu;
static double g_apercu_tour;
static int g_apercu_etape;                /* (mesure) l'étape de la séquence en cours */
static uint64_t g_apercu_abandons_etape[12];
static uint64_t g_apercu_bilan[16];
static int g_apercu_verif = -1;           /* SFC_APERCU_VERIF : l'aperçu doit tout rendre, au dernier octet */
static uint8_t *g_apercu_verif_mem;
static Cpu g_apercu_verif_cpu;
static uint64_t g_apercu_verif_n;
static uint8_t *g_fin_apercu;                 /* la mémoire telle qu'un aperçu la laisse (SFC_DIAG_COMPTEURS) */
static int g_fin_apercu_pret, g_diag_compteurs = -1;
void capture_verrou_prendre(void); void capture_verrou_rendre(void); void capture_differee_poser(void);
extern int g_echelle_active;
extern int32_t g_echelle_q16;
void hw_apercu_sauver(void); void hw_apercu_rendre(void);
void gx_apercu_sauver(void); void gx_apercu_rendre(void);
void gx_geom_apercu_sauver(void); void gx_geom_apercu_rendre(void);
void ipc_apercu_sauver(void); void ipc_apercu_rendre(void);
void mem_apercu_sauver(void); void mem_apercu_rendre(void);
void video_deposer_apercu(double tour);
void video_apercu_fini(void);

void apercu_abandon(int raison) {
    if (g_apercu_en_cours) longjmp(g_apercu_saut, raison);
}

/* L'heure à laquelle un tour d'aperçu doit avoir rendu la main : le balayage du jeu passe avant lui. Sans
   cela, un tour qui tombe sur un travail rare et long — un ennemi abattu, un bonus ramassé — s'y attarde
   cent fois plus que d'ordinaire, et le jeu, qui l'attend, saute. */
static int64_t g_apercu_fin_max;

/* une arête arrière pendant un aperçu : au-delà d'un seuil, c'est une boucle qui attend le temps ; et de
   temps en temps, on regarde l'heure */
void apercu_arete(void) {
    if (!g_apercu_en_cours) return;
    if (++g_apercu_aretes > 400000) longjmp(g_apercu_saut, 7);
    if ((g_apercu_aretes & 1023u) == 0 && g_apercu_fin_max) {
        LARGE_INTEGER q; QueryPerformanceCounter(&q);
        if (q.QuadPart > g_apercu_fin_max) longjmp(g_apercu_saut, 10);
    }
}

extern Cpu *g_cpu_principal_endormi;
extern Cpu *g_cpu_en_halte;

/* D'où part un aperçu : de l'attente du tour, en vol (le jeu a fini son image et compte vingt
   millisecondes), ou du sommeil du fil principal, qui attend le balayage — dans les menus, le mode
   stratégie, et la seconde moitié d'un tour de vol. */
static Cpu *apercu_ou(int *depuis_attente) {
    if (g_apercu_cpu) { if (depuis_attente) *depuis_attente = 0; return g_apercu_cpu; }
    if (g_cpu_principal_endormi) { if (depuis_attente) *depuis_attente = 1; return g_cpu_principal_endormi; }
    if (g_cpu_en_halte && !g_cpu_en_halte->is_arm7) { if (depuis_attente) *depuis_attente = 1; return g_cpu_en_halte; }
    return NULL;
}

int natif_apercu_possible(void) {
    {   static int diag = -1; static uint64_t n, attente, sommeil, rien;
        if (diag < 0) diag = getenv("SFC_DIAG_APERCUS") != NULL;
        if (diag) {
            if (g_apercu_cpu) attente++; else if (g_cpu_principal_endormi || g_cpu_en_halte) sommeil++; else rien++;
            if (++n % 600 == 0) rt_log("points de départ des aperçus : %llu attentes de tour, %llu haltes, %llu ni l'un ni l'autre",
                                       (unsigned long long)attente, (unsigned long long)sommeil, (unsigned long long)rien);
        }
    }
    /* Pas d'aperçu dans les tours qui suivent un changement d'overlay : la scène est en train d'être
       remplacée, et rejouer le tour suivant fait exécuter du code qui n'est plus là. */
    {   extern uint64_t g_overlay_dernier_change;
        if (hw_frames() - g_overlay_dernier_change < 16) return 0;
    }
    return apercu_ou(NULL) != NULL && !g_apercu_en_cours && g_mem_suivie != NULL;
}

static void apercu_appel(Cpu *c, uint32_t fn, uint32_t retour) {
    c->r[14] = retour;
    const RecompEntry *e = rt_find(c, fn);
    if (!e) longjmp(g_apercu_saut, 8);
    RT_CALL(c, retour, e->fn(c, fn & ~1u));
}

static void apercu_methode(Cpu *c, uint32_t objet, uint32_t rang, uint32_t retour) {
    c->r[0] = objet;
    uint32_t m = ldr_word(c, ldr_word(c, objet) + rang);
    c->r[14] = retour;
    RT_CALL(c, retour, rt_call(c, m));
}

/* la suite de la boucle du jeu (0x0203DAE8, réécrite par le générateur), depuis l'attente d'un tour
   jusqu'à celle du suivant : A = méthode 0 de l'application, B = méthode 3 (qui mène 0x020A0E4C et son
   attente de vingt millisecondes), C = 0x0203003C (interruptions coupées), D = l'échange, E = 0x02030050
   (rendues), F = 0x0202C760(1, 1) (l'attente du balayage), G = méthode 2, H = 0x02026B2C, I = méthode 1 */
/* ESSAI (SFC_APERCU_VBLANK=1) : le balayage que l'aperçu n'attend pas — son interruption, livrée une fois,
   pour que ce que le jeu compte au balayage avance comme dans un vrai tour. En l'état, la livraison arrête
   les aperçus tout net ; à reprendre. */
static void apercu_vblank(Cpu *c) {
    static int actif = -1;
    if (actif < 0) { const char *e = getenv("SFC_APERCU_VBLANK"); actif = e && *e != '0'; }
    if (!actif) return;
    extern int g_apercu_vblank_a_livrer;
    hw_raise_irq(c, IRQ_VBLANK);
    g_apercu_vblank_a_livrer = 1;
    hw_deliver_pending(c);
    g_apercu_vblank_a_livrer = 0;
}

static void apercu_sequence(Cpu *c, int depuis_attente) {
    const uint32_t app = 0x020B610Cu;
    c->r[13] = (c->r[13] - 0x400u) & ~7u;
    if (depuis_attente) {
        /* le fil principal dort dans l'attente du balayage : son tour est fini, son image échangée ; il ne
           reste que ce qui suit l'attente */
        apercu_vblank(c);
        g_apercu_etape = 4; apercu_methode(c, app, 0x8, 0x0203DB3Du);     /* G */
        g_apercu_etape = 5; apercu_appel(c, 0x02026B2Cu, 0x0203DB41u);    /* H */
        g_apercu_etape = 6; apercu_methode(c, app, 0x4, 0x0203DB49u);     /* I */
        if (mem_read8(c, app + 4)) longjmp(g_apercu_saut, 6);
        c->r[1] = 0;
        g_apercu_etape = 7; apercu_methode(c, app, 0x0, 0x0203DB19u);     /* A */
        g_apercu_etape = 8; apercu_methode(c, app, 0xC, 0x0203DB21u);     /* B */
        return;
    }
    g_apercu_etape = 1; apercu_appel(c, 0x0203003Cu, 0x0203DB25u);    /* C */
    uint32_t ancien = c->r[0];
    g_apercu_etape = 2;
    mem_write32(c, 0x04000540u, 1u);                                    /* D : l'échange */
    mem_write8(c, 0x020B8044u, 1u);
    c->r[0] = ancien;
    g_apercu_etape = 3; apercu_appel(c, 0x02030050u, 0x0203DB2Du);    /* E */
    /* F, 0x0202C760(1, 1), attend l'interruption du balayage : c'est là que la boucle dort ; un aperçu ne
       dort pas, mais il reçoit l'interruption que l'attente guettait */
    apercu_vblank(c);
    g_apercu_etape = 4; apercu_methode(c, app, 0x8, 0x0203DB3Du);     /* G */
    g_apercu_etape = 5; apercu_appel(c, 0x02026B2Cu, 0x0203DB41u);    /* H */
    g_apercu_etape = 6; apercu_methode(c, app, 0x4, 0x0203DB49u);     /* I */
    if (mem_read8(c, app + 4)) longjmp(g_apercu_saut, 6);               /* le jeu quitte sa boucle */
    c->r[1] = 0;
    g_apercu_etape = 7; apercu_methode(c, app, 0x0, 0x0203DB19u);     /* A */
    g_apercu_etape = 8; apercu_methode(c, app, 0xC, 0x0203DB21u);     /* B : il s'arrête à l'attente */
}

/* ------------------------------------------------------------------ ce qui n'avance pas dans un aperçu
 *
 * SFC_DIAG_COMPTEURS=1 : un tour d'aperçu prédit le tour suivant, mais il saute le balayage — tout ce que
 * le jeu compte à ce moment-là reste donc en arrière, et ce qu'il en tire (un clignotement, l'ordre des
 * écrans) se trompe. Pour trouver ces compteurs : à la fin d'un aperçu, on garde la mémoire ; au même point
 * du vrai tour, on cherche les mots qui y valent exactement un de plus. Les adresses qui ressortent sont
 * les compteurs qu'un aperçu devrait faire avancer.
 */
#define COMPTEURS_CASES 4096
static struct { uint32_t adr; uint32_t fois, vues; } g_compteurs[COMPTEURS_CASES];

static void compteur_noter(uint32_t adr, int en_retard) {
    uint32_t h = (adr * 2654435761u) >> 12 & (COMPTEURS_CASES - 1);
    for (uint32_t k = 0; k < 64; k++) {
        uint32_t j = (h + k) & (COMPTEURS_CASES - 1);
        if (!g_compteurs[j].adr) g_compteurs[j].adr = adr;
        if (g_compteurs[j].adr == adr) { g_compteurs[j].vues++; g_compteurs[j].fois += en_retard != 0; return; }
    }
}

static void compteurs_comparer(void) {
    static uint64_t n;
    size_t retards = 0;
    static uint64_t par_zone[NB_ZONES_SUIVIES];
    for (size_t i = 0; i + 4 <= MEM_SUIVIE_TAILLE; i += 4) {
        uint32_t a, b;
        memcpy(&a, g_fin_apercu + i, 4);
        memcpy(&b, g_mem_suivie + i, 4);
        if (a == b) continue;
        par_zone[zone_de_page(i / PAGE_SUIVIE)]++;
        if (b == a + 1u) { compteur_noter((uint32_t)i, 1); retards++; }
        else compteur_noter((uint32_t)i, 0);
    }
    if (++n % 200 == 0) {
        rt_log("ce qu'un aperçu ne prédit pas, par zone (cumul sur %llu comparaisons) :", (unsigned long long)n);
        for (size_t z = 0; z < NB_ZONES_SUIVIES; z++)
            if (par_zone[z]) rt_log("   %s : %llu mots", g_zones_suivies[z].nom, (unsigned long long)par_zone[z]);
        rt_log("compteurs en retard d'un tour dans les aperçus : %zu mots à la dernière comparaison", retards);
        for (int tour = 0; tour < 12; tour++) {   /* les douze plus constants */
            uint32_t mieux = 0, ifois = 0;
            for (uint32_t j = 0; j < COMPTEURS_CASES; j++)
                if (g_compteurs[j].adr && g_compteurs[j].fois > ifois) { ifois = g_compteurs[j].fois; mieux = j; }
            if (!ifois) break;
            size_t off = g_compteurs[mieux].adr, z = zone_de_page(off / PAGE_SUIVIE);
            rt_log("   %s + 0x%X (adresse 0x%08X) : %u fois en retard sur %u", g_zones_suivies[z].nom,
                   (unsigned)(off - g_zones_suivies[z].offset),
                   (unsigned)(g_zones_suivies[z].base + (off - g_zones_suivies[z].offset)),
                   g_compteurs[mieux].fois, g_compteurs[mieux].vues);
            g_compteurs[mieux].fois = 0;      /* déjà dit : au suivant */
        }
    }
}

/* Un tour d'aperçu, à la part de tour k (16.16), avec ces touches. Rend 1 si une image a été prise. */
int natif_apercu_jouer(int32_t k_q16, double tour, uint32_t touches, double budget_s) {
    int depuis_attente = 0;
    Cpu *c = apercu_ou(&depuis_attente);
    if (!natif_apercu_possible()) return 0;
    if (g_apercu_verif < 0) g_apercu_verif = getenv("SFC_APERCU_VERIF") != NULL;
    LARGE_INTEGER q[5], fq;
    QueryPerformanceCounter(&q[0]);
    etat_allouer(&g_etat_apercu);
    if (g_diag_compteurs > 0 && g_fin_apercu_pret) { g_fin_apercu_pret = 0; compteurs_comparer(); }
    capture_verrou_prendre();            /* la capture d'affichage n'écrit pas pendant qu'on prend la mémoire */
    etat_prendre(&g_etat_apercu, c);
    QueryPerformanceCounter(&q[1]);
    static uint8_t io[TAILLE_IO];
    memcpy(io, g_io, TAILLE_IO);
    if (g_apercu_verif) {
        if (!g_apercu_verif_mem) g_apercu_verif_mem = malloc(MEM_SUIVIE_TAILLE);
        memcpy(g_apercu_verif_mem, g_mem_suivie, MEM_SUIVIE_TAILLE);
        g_apercu_verif_cpu = *c;
    }
    /* Le drapeau se lève sous le verrou : de la prise de la mémoire à sa restitution, la capture
       d'affichage écrit de côté et rien de ce qu'elle fait n'est perdu ni laissé derrière. */
    g_apercu_en_cours = 1;
    capture_verrou_rendre();
    hw_apercu_sauver(); gx_apercu_sauver(); gx_geom_apercu_sauver(); ipc_apercu_sauver(); mem_apercu_sauver();
    { extern void threads_apercu_sauver(void); threads_apercu_sauver(); }
    int fige = g_temps_fige;
    uint32_t irq_off = c->irq_off;
    g_temps_fige = 1;
    g_echelle_q16 = k_q16;
    g_apercu_tour = tour;
    g_apercu_aretes = 0;
    {   LARGE_INTEGER maintenant, f; QueryPerformanceCounter(&maintenant); QueryPerformanceFrequency(&f);
        g_apercu_fin_max = budget_s > 0 ? maintenant.QuadPart + (int64_t)(budget_s * (double)f.QuadPart) : 0; }
    hw_set_keys(touches);
    QueryPerformanceCounter(&q[2]);
    { extern void apercu_soupcons_debut(void); apercu_soupcons_debut(); }
    volatile int raison = setjmp(g_apercu_saut);
    if (raison == 0) {
        g_echelle_active = 1;
        apercu_sequence(c, depuis_attente);
        raison = 9;                                   /* allé au bout sans atteindre l'attente */
    }
    QueryPerformanceCounter(&q[3]);
    /* Le verrou d'abord, le drapeau ensuite : entre les deux, la capture d'affichage se croirait libre
       d'écrire en mémoire vidéo alors que l'aperçu ne l'a pas encore rendue. */
    capture_verrou_prendre();
    g_apercu_en_cours = 0;
    if (g_diag_compteurs < 0) g_diag_compteurs = getenv("SFC_DIAG_COMPTEURS") != NULL;
    if (g_diag_compteurs > 0 && raison == 1) {   /* la mémoire telle que l'aperçu la laisse, avant qu'on la rende */
        if (!g_fin_apercu) g_fin_apercu = malloc(MEM_SUIVIE_TAILLE);
        memcpy(g_fin_apercu, g_mem_suivie, MEM_SUIVIE_TAILLE);
        g_fin_apercu_pret = 1;
    }
    g_apercu_fin_max = 0;
    /* l'aperçu a pu renoncer en plein milieu de l'interruption qu'on lui donnait : le droit se referme ici */
    { extern int g_apercu_vblank_a_livrer; g_apercu_vblank_a_livrer = 0; }
    g_echelle_active = 0;
    g_temps_fige = fige;
    memcpy(g_io, io, TAILLE_IO);
    hw_apercu_rendre(); gx_apercu_rendre(); gx_geom_apercu_rendre(); ipc_apercu_rendre(); mem_apercu_rendre();
    { extern void threads_apercu_rendre(void); threads_apercu_rendre(); }
    etat_rendre(&g_etat_apercu, c);
    /* SFC_APERCU_VERIF=1 : après chaque aperçu, la mémoire entière et les registres doivent être exactement
       ceux d'avant. La moindre trace laissée ferait diverger le jeu — plus tard, et ailleurs. */
    if (g_apercu_verif) {
        {
            for (size_t i = 0; i < MEM_SUIVIE_TAILLE; i++)
                if (g_apercu_verif_mem[i] != g_mem_suivie[i]) {
                    size_t pg = i / PAGE_SUIVIE, z = zone_de_page(pg);
                    size_t ndiff = 0, npages = 0;
                    for (size_t p = 0; p < NB_PAGES; p++) {
                        size_t d = 0;
                        for (size_t j = 0; j < PAGE_SUIVIE; j++)
                            d += g_apercu_verif_mem[p * PAGE_SUIVIE + j] != g_mem_suivie[p * PAGE_SUIVIE + j];
                        ndiff += d; npages += d != 0;
                    }
                    {   /* cette page a-t-elle été écrite depuis la restitution ? (un autre fil, alors) */
                        PVOID une[1]; ULONG_PTR nu = 1; DWORD gr = 0;
                        int apres = GetWriteWatch(0, g_mem_suivie + pg * PAGE_SUIVIE, PAGE_SUIVIE, une, &nu, &gr) == 0 && nu > 0;
                        rt_log("aperçu : la page %llu a-t-elle été écrite depuis la restitution ? %s",
                               (unsigned long long)pg, apres ? "oui" : "non");
                    }
                    rt_log("aperçu : %llu octets sur %llu pages ont bougé ; la page %llu était %s remise",
                           (unsigned long long)ndiff, (unsigned long long)npages, (unsigned long long)pg,
                           (g_pages_rendues[pg >> 5] >> (pg & 31)) & 1u ? "bien" : "PAS");
                    rt_log("aperçu n° %llu (raison %d, étape %d) : la mémoire a bougé en %s + 0x%X (adresse 0x%08X) : 0x%02X -> 0x%02X",
                           (unsigned long long)g_apercu_verif_n, (int)raison, g_apercu_etape, g_zones_suivies[z].nom,
                           (unsigned)(i - g_zones_suivies[z].offset),
                           (unsigned)(g_zones_suivies[z].base + (i - g_zones_suivies[z].offset)),
                           g_apercu_verif_mem[i], g_mem_suivie[i]);
                    rt_sortir(3);
                }
            for (int k = 0; k < 16; k++)
                if (g_apercu_verif_cpu.r[k] != c->r[k]) {
                    rt_log("aperçu n° %llu (raison %d) : le registre r%d a bougé : 0x%08X -> 0x%08X",
                           (unsigned long long)g_apercu_verif_n, (int)raison, k, g_apercu_verif_cpu.r[k], c->r[k]);
                    rt_sortir(3);
                }
        }
        g_apercu_verif_n++;
    }
    capture_differee_poser();   /* la capture qui a eu lieu pendant l'aperçu prend sa place maintenant */
    capture_verrou_rendre();
    QueryPerformanceCounter(&q[4]); QueryPerformanceFrequency(&fq);
    {   static double somme[4]; static uint64_t n;
        for (int i = 0; i < 4; i++) somme[i] += (double)(q[i + 1].QuadPart - q[i].QuadPart) * 1000.0 / (double)fq.QuadPart;
        if (++n % 200 == 0) {
            rt_log("aperçu, coût moyen : prise %.2f ms, sauvegardes %.2f, tour %.2f, restitution %.2f", somme[0] / 200, somme[1] / 200, somme[2] / 200, somme[3] / 200);
            memset(somme, 0, sizeof somme);
        }
    }
    c->irq_off = irq_off;
    c->redirect = 0;
    video_apercu_fini();
    { extern void apercu_soupcons_fin(int); apercu_soupcons_fin(raison == 1); }
    g_apercu_bilan[raison & 15]++;
    {   static int n; if (raison != 1 && raison != 3 && n++ < 6) rt_log("aperçu abandonné : raison %d à l'étape %d", (int)raison, g_apercu_etape); }
    if (raison != 1) g_apercu_abandons_etape[g_apercu_etape % 12]++;
    {   static uint64_t n;
        if (++n % 600 == 0)
            rt_log("aperçus : %llu images prises ; abandons : %llu commutations, %llu attentes, %llu carte, %llu fin de boucle, %llu boucles, %llu sans attente, %llu autres",
                   (unsigned long long)g_apercu_bilan[1], (unsigned long long)g_apercu_bilan[3],
                   (unsigned long long)g_apercu_bilan[4], (unsigned long long)g_apercu_bilan[5],
                   (unsigned long long)g_apercu_bilan[6], (unsigned long long)g_apercu_bilan[7],
                   (unsigned long long)g_apercu_bilan[9],
                   (unsigned long long)(g_apercu_bilan[2] + g_apercu_bilan[8] + g_apercu_bilan[11]));
        if (n % 600 == 0)
            rt_log("   dont %llu abandonnés faute de temps : le balayage arrivait", (unsigned long long)g_apercu_bilan[10]);
        if (n % 600 == 0)
            rt_log("   abandons par étape : C %llu, échange %llu, E %llu, G %llu, H %llu, I %llu, A %llu, B %llu",
                   (unsigned long long)g_apercu_abandons_etape[1], (unsigned long long)g_apercu_abandons_etape[2],
                   (unsigned long long)g_apercu_abandons_etape[3], (unsigned long long)g_apercu_abandons_etape[4],
                   (unsigned long long)g_apercu_abandons_etape[5], (unsigned long long)g_apercu_abandons_etape[6],
                   (unsigned long long)g_apercu_abandons_etape[7], (unsigned long long)g_apercu_abandons_etape[8]);
    }
    return raison == 1;
}

static void apercu_capture_et_sortie(void) {
    video_deposer_apercu(g_apercu_tour);
    longjmp(g_apercu_saut, 1);
}
static void apercu_attente_debut(Cpu *c) { if (!g_apercu_en_cours) g_apercu_cpu = c; }
static void apercu_attente_fin(void) { if (!g_apercu_en_cours) g_apercu_cpu = NULL; }

/* Première différence entre deux zones, ou -1.
 *
 * Ce qui est sous le pointeur de pile ne compte pas : la traduction y empile son adresse de
 * retour, une réécriture native n'a pas à le faire, et ces octets sont morts dès le retour.
 * Les comparer ferait échouer toute réécriture correcte. */
static int g_compter_pile;    /* SFC_VERIF_PILE=1 : ne rien passer, même les octets morts */

static long premier_ecart(const uint8_t *a, const uint8_t *b, size_t n,
                          uint32_t base, uint32_t sp) {
    for (size_t i = 0; i < n; i++) {
        if (a[i] == b[i]) continue;
        uint32_t adresse = base + (uint32_t)i;
        if (!g_compter_pile && adresse < sp && sp - adresse < 0x400u) continue;   /* pile morte */
        return (long)i;
    }
    return -1;
}

/* ------------------------------------------------------------------ la comparaison */

/* Une fonction vérifiée en appelle d'autres qui le sont aussi : chaque comparaison a son
   niveau, avec ses deux états. Au-delà de la profondeur prévue, on se contente d'exécuter la
   traduction. C'est ce qui permet de juger le menu fretin pendant qu'une fonction de haut
   niveau — qui ne revient peut-être jamais — est elle-même en cours de comparaison. */
#define PROFONDEUR_BANC 12
static struct { Etat avant, apres; int pret; } g_niveaux[PROFONDEUR_BANC];
static int g_niveau;                 /* comparaisons imbriquées en cours */
/* Les niveaux sont ceux d'une seule fibre : le thread du jeu qui tournait quand le banc a été
   posé. Une comparaison commencée dans un thread et suspendue par un changement de thread
   laisserait son niveau à un autre, qui restaurerait un état qui n'est pas le sien. */
static void *g_fibre_du_banc;
/* Chaque comparaison exécute la fonction deux fois. Si chacune des deux rejugeait ses appelées,
   le coût doublerait à chaque niveau — seize fois à quatre niveaux, quatre mille à douze, et le
   jeu s'arrêtait à la vingtième image. Les appelées sont jugées pendant la phase « traduction » ;
   pendant la phase « réécriture », monde figé, elles tournent telles quelles. */
static int g_reecriture_en_cours;
#define g_avant (g_niveaux[g_niveau - 1].avant)
#define g_apres (g_niveaux[g_niveau - 1].apres)

static void niveaux_preparer(void) {
    for (int k = 0; k < PROFONDEUR_BANC; k++)
        if (!g_niveaux[k].pret) { etat_allouer(&g_niveaux[k].avant); etat_allouer(&g_niveaux[k].apres); g_niveaux[k].pret = 1; }
}

/* SFC_TRACE_NATIF=n : journalise l'entrée et la sortie des n premiers appels. Les mêmes
   lignes, produites une fois par la traduction et une fois par la réécriture, se comparent
   ligne à ligne : le premier écart dit à quel appel et sur quel registre elles se séparent. */
static uint64_t g_trace_natif;

static void tracer(const char *quoi, const char *nom, const Cpu *c) {
    if (!g_trace_natif) return;
    g_trace_natif--;
    rt_log("natif i%llu %s %s : r0=%08X r1=%08X r2=%08X r3=%08X r4=%08X r5=%08X r6=%08X r7=%08X sp=%08X lr=%08X NZCVT=%u%u%u%u%u",
           (unsigned long long)hw_frames(), nom, quoi, c->r[0], c->r[1], c->r[2], c->r[3],
           c->r[4], c->r[5], c->r[6], c->r[7], c->r[13], c->r[14],
           c->nf, c->zf, c->cf, c->vf, c->thumb);
}
static unsigned g_max_appels = 2000;      /* au-delà, on cesse de comparer : c'est lent */

static void comparer(const Reecriture *r, Cpu *c) {
    Surveillance *s = r->suivi;
    int faux = 0;
    /* Les drapeaux comptent autant que les registres : l'appelant branche dessus, et le
       code appelé les lit quand il consulte le registre d'état. */
    static const char *noms[5] = {"N", "Z", "C", "V", "T"};
    uint32_t vus[5] = {c->nf, c->zf, c->cf, c->vf, c->thumb};
    uint32_t attendus[5] = {g_apres.cpsr_n, g_apres.cpsr_z, g_apres.cpsr_c, g_apres.cpsr_v,
                            g_apres.thumb};
    for (int i = 0; i < 5; i++)
        if (vus[i] != attendus[i]) {
            rt_log("vérif %s : drapeau %s = %u, attendu %u", r->nom, noms[i], vus[i], attendus[i]);
            faux = 1;
        }
    if (c->qf != g_apres.qf || c->mode != g_apres.mode) {
        rt_log("vérif %s : Q = %u et mode %02X, attendus %u et %02X", r->nom, c->qf, c->mode,
               g_apres.qf, g_apres.mode);
        faux = 1;
    }
    if (memcmp(&c->irq_off, g_apres.systeme, sizeof g_apres.systeme)) {
        static Cpu ref;
        memcpy(&ref.irq_off, g_apres.systeme, sizeof g_apres.systeme);
        rt_log("vérif %s : I = %u, F = %u, spsr = 0x%08X, attendus %u, %u, 0x%08X — ou une banque diffère",
               r->nom, c->irq_off, c->fiq_off, c->spsr, ref.irq_off, ref.fiq_off, ref.spsr);
        faux = 1;
    }
    for (int i = 0; i < 16; i++) {
        if (i == 15) continue;                     /* pc : le dispatch le mène */
        if (c->r[i] != g_apres.r[i]) {
            rt_log("vérif %s : r%d = 0x%08X, attendu 0x%08X", r->nom, i, c->r[i], g_apres.r[i]);
            faux = 1;
        }
    }
    uint32_t sp = c->r[13];
    long o;
    /* La mémoire : l'image d'après la traduction a été prise avant qu'on ne remette l'état
       d'avant et que la réécriture ne tourne ; ses pages périmées sont donc exactement celles
       que l'une ou l'autre a écrites. Partout ailleurs, personne n'a rien touché. */
    relever(NULL);
    unsigned dites = 0;                                /* une ligne par zone suffit */
    POUR_CHAQUE_PAGE(g_apres.perime, page) {
        size_t z = zone_de_page(page);
        const uint8_t *ref = g_apres.image + page * PAGE_SUIVIE, *vu = g_mem_suivie + page * PAGE_SUIVIE;
        uint32_t adresse = g_zones_suivies[z].base + (uint32_t)(page * PAGE_SUIVIE) - g_zones_suivies[z].offset;
        o = g_zones_suivies[z].pile ? premier_ecart(ref, vu, PAGE_SUIVIE, adresse, sp)
                                    : premier_ecart(ref, vu, PAGE_SUIVIE, adresse, 0);
        if (o < 0) continue;
        faux = 1;
        if (dites >> z & 1u) continue;
        dites |= 1u << z;
        rt_log("vérif %s : %s diffère à 0x%08X — traduction 0x%02X, réécriture 0x%02X",
               r->nom, g_zones_suivies[z].nom, adresse + (uint32_t)o, ref[o], vu[o]);
    }
    if (g_banc_froid && (o = premier_ecart(g_apres.io, g_io, TAILLE_IO, 0x04000000u, 0)) >= 0) {
        rt_log("vérif %s : registres diffèrent à 0x%08X — traduction 0x%02X, réécriture 0x%02X",
               r->nom, 0x04000000u + (uint32_t)o, g_apres.io[o], g_io[o]);
        faux = 1;
    }
    /* Le temps simulé se compte en arêtes arrière : une réécriture qui en consomme plus ou moins
       que la traduction décale toute la suite du jeu, sans qu'aucun registre le montre. */
    if (g_edge_budget != g_apres.edge_budget) {
        rt_log("vérif %s : il reste %d arêtes au budget, attendu %d", r->nom, (int)g_edge_budget, (int)g_apres.edge_budget);
        faux = 1;
    }
    if (c->depth != g_apres.depth) {
        rt_log("vérif %s : pile fantôme à %u niveaux, attendu %u", r->nom,
               c->depth, g_apres.depth);
        faux = 1;
    } else if (c->depth < 512 && memcmp(c->shadow, g_apres.shadow, c->depth * sizeof c->shadow[0])) {
        rt_log("vérif %s : pile fantôme différente", r->nom);
        faux = 1;
    }
    if (faux) s->divergences++;
}

/* Exécute la traduction, crochet ôté le temps de l'appel — sans quoi elle se rappellerait
   elle-même. Le crochet est posé sur la fonction d'une seule région ; si c'est un autre
   overlay qui occupe l'adresse à cet instant, rt_find rend la sienne et l'on n'est pas
   concerné : le crochet n'aurait d'ailleurs pas été appelé. */
static void appeler_traduction(const Reecriture *r, Cpu *c) {
    Surveillance *s = r->suivi;
    NativeHook garde = *s->emplacement;
    *s->emplacement = NULL;
    s->traduction->fn(c, r->entree);
    *s->emplacement = garde;
}

/* Le crochet posé sur une fonction surveillée : les deux versions, puis le verdict. */
void natif_verifier(Cpu *c, const Reecriture *r) { verifier(c, r); }

static void verifier_niveau(Cpu *c, const Reecriture *r);

/* Remet un suivi à zéro en gardant le tampon de ses chemins. */
static void suivi_vider(Surveillance *s, int nb_chemins) {
    uint8_t *bits = s->chemins;
    int reservee = s->reservee;
    memset(s, 0, sizeof *s);
    s->chemins = bits;
    s->reservee = reservee;
    if (bits) memset(bits, 0, (size_t)(nb_chemins + 8) / 8);
}

/* Une comparaison imbriquée dans une autre : celle du dehors a peut-être armé l'interception du
   saut final ; on la met de côté le temps de juger celle du dedans, et on la remet. */
static void verifier(Cpu *c, const Reecriture *r) {
    int intercepte = g_jump_intercepte;
    uint32_t profondeur = g_jump_profondeur;
    g_jump_intercepte = 0;
    verifier_niveau(c, r);
    g_jump_intercepte = intercepte;
    g_jump_profondeur = profondeur;
}

static void verifier_niveau(Cpu *c, const Reecriture *r) {
    Surveillance *s = r->suivi;
    s->vus++;
    if (s->appels == (uint64_t)-1) {          /* trace de la réécriture seule */
        tracer("entre", r->nom, c);
        r->fn(c);
        tracer("sort ", r->nom, c);
        return;
    }
    /* Une feuille à plusieurs chemins n'est prouvée que lorsque chacun a été comparé : tant
       qu'il en manque, on continue de comparer bien au-delà du plafond ordinaire. */
    uint64_t plafond = g_max_appels;
    if (r->nb_chemins > 1 && s->chemins_vus < (uint32_t)r->nb_chemins) plafond *= 40u;
    if (g_banc_froid) plafond = (uint64_t)-2;
    /* Une fonction que le monde interrompt à chaque fois — elle attend, elle parle au matériel,
       elle contient une image entière — ne sera jamais comparée en jeu : après quelques
       tentatives on cesse de retenir l'état pour rien. Il le faut d'autant plus vite que ces
       fonctions-là sont le haut de la pile d'appels : tant qu'on s'obstine sur elles, elles
       tiennent les niveaux, et le menu fretin qu'elles appellent n'est jamais jugé. */
    if (g_reecriture_en_cours || s->appels >= plafond || g_niveau >= PROFONDEUR_BANC
        || (!g_banc_froid && s->interrompues >= 32u + 4u * s->appels)   /* à froid, la boucle d'essais compte */
        || (!g_banc_froid && GetCurrentFiber() != g_fibre_du_banc)) {
        tracer("entre", r->nom, c);
        appeler_traduction(r, c);
        tracer("sort ", r->nom, c);
        return;
    }

    s->appels++;
    g_niveau++;
    etat_prendre(&g_avant, c);
    /* Une fonction qui finit par sauter ailleurs — le relais d'héritage du C++, l'appel
       terminal — se juge à l'instant du saut : suivre la cible deux fois ferait avancer le
       temps et parler au coprocesseur deux fois, et ce ne serait plus elle qu'on jugerait. */
    uint32_t cible_ref = 0, cible = 0;
    int saute_ref = 0, saute = 0;
    /* À froid, un essai qui n'en finit pas — un compteur tiré au sort, une boucle qui tourne
       des milliards de fois — est abandonné au-delà d'un nombre d'arêtes, dans les deux
       versions pareillement : rt_edge revient ici. */
    volatile int phase = 0;
    volatile uint64_t aretes_ref = 0;
    if (g_banc_froid) {
        { static uint64_t max_aretes; if (!max_aretes) { const char *v = getenv("SFC_FROID_ARETES"); max_aretes = v ? strtoull(v, NULL, 0) : 200; }
          g_essai_aretes = 0; g_essai_aretes_max = max_aretes; }
        if (setjmp(g_essai_reprise)) {
            g_reecriture_en_cours = 0;
            g_essai_aretes_max = 0;
            g_jump_intercepte = 0;
            *s->emplacement = r->crochet;
            if (phase == 0) {                 /* la traduction n'en finissait pas : l'essai ne compte pas */
                s->appels--;
                s->interrompues++;
            } else {                          /* la réécriture n'en finit pas là où la traduction finissait */
                rt_log("vérif %s : la réécriture ne s'arrête pas où la traduction s'arrêtait (%llu arêtes)", r->nom,
                       (unsigned long long)aretes_ref);
                s->divergences++;
            }
            etat_rendre(&g_avant, c);
            g_niveau--;
            return;
        }
    }
    if (r->saut_final) { g_jump_intercepte = 1; g_jump_profondeur = c->depth; }
    g_journal = 0; g_journal_n = 0; g_journal_materiel = 0;
    uint64_t monde = g_perturbations, materiel = g_acces_mmio;
    tracer("entre", r->nom, c);
    appeler_traduction(r, c);
    tracer("trad ", r->nom, c);
    if (r->saut_final) { saute_ref = !g_jump_intercepte; cible_ref = g_jump_cible; g_jump_intercepte = 0; }
    uint64_t journal_ref = g_journal; unsigned appels_ref = g_journal_n;
    unsigned materiel_ref = g_journal_materiel;
    phase = 1;
    aretes_ref = g_essai_aretes;
    g_essai_aretes = 0;
    if ((g_acces_mmio != materiel && !g_mmio_bouchon) || (g_banc_froid && !code_intact())) {
        /* Elle a touché un registre matériel : le matériel ne se rejoue pas — un mot envoyé au
           coprocesseur partirait deux fois. En jeu, on garde le résultat et l'on ne compare pas ;
           ces fonctions se jugent aux empreintes. À froid, c'est un pointeur lu en mémoire, que
           le tirage ne pouvait pas prévoir — ou du code écrasé, et la traduction lit ses
           constantes dans le code ; l'essai ne compte pas. */
        s->appels--;
        s->interrompues++;
        g_essai_aretes_max = 0;
        if (g_banc_froid) etat_rendre(&g_avant, c);   /* avant de quitter le niveau : l'état est le sien */
        g_niveau--;
        if (!g_banc_froid && saute_ref) rt_jump(c, cible_ref);   /* en jeu, le résultat de la traduction reste */
        return;
    }
    if (g_perturbations != monde) {
        /* Une interruption, un autre thread ou le coprocesseur ont tourné pendant la traduction :
           remettre la mémoire en arrière casserait ce qu'ils ont fait, et la réécriture ne
           reverrait pas le même monde. On garde le résultat et l'on ne compare pas ; à froid,
           rien de tout cela n'existe. */
        s->appels--;
        s->interrompues++;
        g_niveau--;
        if (saute_ref) rt_jump(c, cible_ref);
        return;
    }
    etat_prendre(&g_apres, c);

    etat_rendre(&g_avant, c);
    if (r->saut_final) { g_jump_intercepte = 1; g_jump_profondeur = c->depth; }
    g_journal = 0; g_journal_n = 0; g_journal_materiel = 0;
    tracer("entre", r->nom, c);
    /* La réécriture tourne dans un monde figé : le temps n'avance pas, aucune interruption
       n'est livrée, le coprocesseur ne prend pas la main. La traduction a déjà fait avancer
       tout cela une fois ; le refaire ferait bouger le monde deux fois, et l'on comparerait
       la fonction à autre chose qu'elle-même. */
    int monde_fige = g_temps_fige;
    g_temps_fige = 1;
    g_reecriture_en_cours++;
    r->fn(c);
    g_reecriture_en_cours--;
    g_temps_fige = monde_fige;
    /* SFC_VERIF_SABOTAGE=0x… : abîme un octet de RAM après chaque réécriture, par un pointeur nu.
       Le banc doit crier à chaque comparaison, et le jeu n'en rien garder — c'est l'épreuve du
       suivi des pages, de la comparaison comme de la remise en état. */
    { static uint32_t ou = 1; if (ou == 1) { const char *v = getenv("SFC_VERIF_SABOTAGE"); ou = v ? (uint32_t)strtoul(v, NULL, 0) : 0; }
      if (ou) g_main_ram[ou & 0x3FFFFFu] ^= 0x40u; }
    tracer("réécr", r->nom, c);
    if (r->saut_final) { saute = !g_jump_intercepte; cible = g_jump_cible; g_jump_intercepte = 0; }
    comparer(r, c);
    if (g_banc_froid && getenv("SFC_FROID_BAVARD"))
        rt_log("vérif %s : %llu arêtes pour la traduction, %llu pour la réécriture ; %u et %u appels", r->nom,
               (unsigned long long)aretes_ref, (unsigned long long)g_essai_aretes, appels_ref, g_journal_n);
    if (g_banc_froid && g_journal_materiel != materiel_ref) {
        rt_log("vérif %s : %u accès aux registres matériels, attendu %u", r->nom, g_journal_materiel, materiel_ref);
        s->divergences++;
    } else
    if (g_banc_froid && (g_journal != journal_ref || g_journal_n != appels_ref)) {
        rt_log("vérif %s : %u appels (journal %016llX), attendu %u (%016llX)", r->nom, g_journal_n,
               (unsigned long long)g_journal, appels_ref, (unsigned long long)journal_ref);
        s->divergences++;
    }
    if (r->saut_final && (saute != saute_ref || (saute && cible != cible_ref))) {
        rt_log("vérif %s : saut final vers 0x%08X (%s), attendu 0x%08X (%s)", r->nom,
               cible, saute ? "pris" : "absent", cible_ref, saute_ref ? "pris" : "absent");
        s->divergences++;
    }

    etat_rendre(&g_apres, c);        /* le jeu garde le résultat de la référence */
    g_niveau--;
    g_essai_aretes_max = 0;
    if (saute_ref && !g_banc_froid) rt_jump(c, cible_ref);   /* et le saut, enfin suivi — une seule fois */
}

/* ------------------------------------------------------------------ le banc à froid */

/* En jeu, le banc ne juge que ce que la partie appelle : une feuille sur six, et rarement tous
 * ses chemins. Or une feuille — ni appel, ni boucle — ne dépend que de ses registres, de ses
 * drapeaux et de la mémoire qu'elle lit. On peut donc l'essayer hors de toute partie : des
 * registres tirés au sort, un bac de mémoire rempli de pointeurs vers lui-même, de zéros et de
 * petites valeurs, et les deux versions comparées par le même `verifier` qu'en jeu.
 *
 * SFC_VERIF_FROID=image[:tranche/nombre] : à cette image le jeu s'arrête de jouer, toutes les
 * feuilles du générateur sont essayées — celles d'un overlay après y avoir posé son image —
 * puis le programme rend la liste (SFC_VERIF_LISTE) et quitte. Les feuilles qui nomment un
 * registre matériel restent à juger en jeu : écrire au hasard dans le matériel ne prouve rien.
 * La tranche répartit le travail entre plusieurs processus. */
#define BAC_BASE   0x02300000u
#define BAC_TAILLE 0x10000u
static uint64_t g_alea = 0x9E3779B97F4A7C15ull;

static uint32_t alea(void) {                 /* xorshift : reproductible d'une exécution à l'autre */
    g_alea ^= g_alea << 13; g_alea ^= g_alea >> 7; g_alea ^= g_alea << 17;
    return (uint32_t)(g_alea >> 16);
}

static uint32_t alea_pointeur(void) {        /* dans le cœur du bac : les décalages restent dedans */
    return BAC_BASE + 0x4000u + (alea() % 0x8000u & ~3u);
}

/* Les constantes auxquelles la feuille en cours compare : sans elles, le chemin « r0 == 0x23 »
   ne serait jamais pris. On tire aussi leurs voisines, pour les inégalités. */
static const uint32_t *g_remarquables;
static int g_nb_remarquables;

static uint32_t alea_valeur(void) {
    uint32_t de = alea() % 100u;
    if (g_nb_remarquables && alea() % 100u < 35u)
        return g_remarquables[alea() % (uint32_t)g_nb_remarquables] + (alea() % 3u) - 1u;
    if (de < 40) return alea_pointeur();
    if (de < 60) return 0;
    if (de < 80) return alea() % 16u;
    if (de < 85) return (uint32_t)-(int32_t)(alea() % 8u);
    if (de < 93) return 0x02000000u + (alea() % 0x3F0000u);   /* ailleurs dans la RAM du jeu */
    return alea() | 0xC0000000u;             /* un grand nombre, jamais une adresse qui ait un sens :
                                                un pointeur qui s'en approche par décalage ne peut
                                                pas atteindre les registres matériels */
}

/* Le bouchon : ce qui répond à la place de toute fonction appelée pendant un essai à froid.
 * Il note dans un journal ce qu'on lui demande — la cible, les arguments, la pile, l'adresse
 * de retour — et répond par des registres et des drapeaux tirés d'une graine propre à
 * l'essai, les mêmes pour la traduction et pour la réécriture. Il écrit aussi quelques mots
 * dans l'objet reçu et sous la pile, comme le ferait une vraie appelée : une réécriture qui
 * garderait en registre ce qu'elle devrait relire après l'appel se trahit ainsi. Les deux
 * exécutions doivent laisser le même journal : appeler autre chose, ou avec d'autres
 * arguments, ou dans un autre ordre, est une divergence comme une autre. */
static uint64_t g_bouchon_graine;

static uint64_t melanger(uint64_t h, uint32_t v) {
    h ^= v; h *= 0x100000001B3ull; return h ^ (h >> 29);
}

/* Les réponses imposées : « le résultat du troisième appel vaut 0 » est une condition comme une
   autre, et le hasard seul la prend rarement en même temps que celles qui la précèdent. */
static struct { unsigned appel, registre; uint32_t valeur; } g_forces[8];
static unsigned g_nb_forces;

/* Le matériel, à froid, répond comme les appelées : par un bouchon. Une lecture de registre reçoit
   une valeur tirée de la graine de l'essai et de son rang, la même pour la traduction et pour la
   réécriture ; une écriture est notée, adresse et valeur ; et le vrai matériel ne voit rien —
   ni DMA lancé, ni mot envoyé au coprocesseur, ni horloge qui avance. Les deux versions doivent
   laisser le même journal. C'est ce qui ouvre au banc les fonctions qui nomment un registre,
   qu'il écartait, et les essais qu'un pointeur tiré au sort menait là, qu'il annulait. */
static uint32_t bouchon_materiel(Cpu *c, uint32_t addr, uint32_t valeur, int octets, int ecriture) {
    (void)c;
    g_journal = melanger(g_journal, addr);
    g_journal = melanger(g_journal, (uint32_t)octets | (uint32_t)ecriture << 8);
    g_journal_materiel++;
    if (ecriture) { g_journal = melanger(g_journal, valeur); return 0; }
    uint64_t sauve = g_alea;
    g_alea = (g_bouchon_graine ^ ((uint64_t)g_journal_materiel * 0xD6E8FEB86659FD93ull)) | 1u;
    uint32_t v = alea() % 100u < 50u ? alea_valeur() : alea();      /* des drapeaux d'état : tous les bits comptent */
    g_alea = sauve;
    return octets == 4 ? v : octets == 2 ? v & 0xFFFFu : v & 0xFFu;
}

static void bouchon(Cpu *c) {
    uint32_t cible = g_hook_entree;
    if (g_trace_natif) rt_log("bouchon : 0x%08X depuis lr=0x%08X r0=%08X r1=%08X sp=%08X", cible, c->r[14], c->r[0], c->r[1], c->r[13]);
    uint64_t h = g_journal;
    h = melanger(h, cible);
    for (int n = 0; n < 4; n++) h = melanger(h, c->r[n]);
    h = melanger(h, c->r[13]); h = melanger(h, c->r[14]); h = melanger(h, c->thumb);
    g_journal = h;
    g_journal_n++;
    uint64_t graine = g_bouchon_graine ^ ((uint64_t)g_journal_n * 0x9E3779B97F4A7C15ull);
    uint64_t sauve = g_alea;
    g_alea = graine | 1u;
    uint32_t objet = c->r[0], sp = c->r[13];
    if (objet - BAC_BASE < BAC_TAILLE - 64u)
        for (int k = 0; k < 4; k++) mem_write32(c, objet + 4u * (alea() % 12u), alea_valeur());
    if (sp - BAC_BASE < BAC_TAILLE && sp - BAC_BASE >= 64u)
        for (int k = 0; k < 4; k++) mem_write32(c, sp - 4u - 4u * (alea() % 12u), alea_valeur());
    for (int n = 0; n < 4; n++) c->r[n] = alea_valeur();
    c->r[12] = alea_valeur();
    c->nf = alea() & 1u; c->zf = alea() & 1u; c->cf = alea() & 1u; c->vf = alea() & 1u;
    for (unsigned k = 0; k < g_nb_forces; k++)     /* une réponse que l'essai a voulue, à tel appel */
        if (g_forces[k].appel == g_journal_n) c->r[g_forces[k].registre] = g_forces[k].valeur;
    g_alea = sauve;
    c->thumb = c->r[14] & 1u;
    c->r[15] = c->r[14];
}

static void bac_remplir(void) {
    uint8_t *bac = mem_resolve(BAC_BASE, BAC_TAILLE);
    for (uint32_t i = 0; i < BAC_TAILLE; i += 4) {
        uint32_t v = alea_valeur();
        memcpy(bac + i, &v, 4);
    }
}

/* Les zones de code des régions, d'après la table des fonctions traduites : arm9 et l'overlay
   de la fonction essayée. Un essai qui les modifie — une copie de mémoire nourrie d'un pointeur
   tiré au sort finit par balayer le code — ne compte pas : la traduction relit ses constantes
   dans ce code, la réécriture les connaît, et aucune fonction du jeu ne s'écrase elle-même. */
static Etat g_base_froid;
static struct { const char *region; uint32_t debut, fin; } g_zones_code[64];
static size_t g_nb_zones_code;
static const char *g_region_froid = "";

/* Le plan des essais a son propre hasard : celui des tirages est remis à la graine de chaque
   essai, pour qu'un essai se rejoue. */
static uint64_t g_plan = 0xD1B54A32D192ED03ull;

static uint64_t plan(void) {
    g_plan ^= g_plan << 13; g_plan ^= g_plan >> 7; g_plan ^= g_plan << 17;
    return g_plan;
}

#define MAX_RETOUCHES 6
#define MAX_SEMENCES 64
typedef struct { uint64_t graine; unsigned n, appels, appels_lors[MAX_RETOUCHES]; uint64_t retouches[MAX_RETOUCHES]; } Semence;
static unsigned g_appels_semence;       /* combien d'appelées a vues l'essai dont on repart */
static Semence g_semences[MAX_SEMENCES];
static unsigned g_nb_semences;

static int dans_le_bac(uint32_t adresse) { return adresse - BAC_BASE < BAC_TAILLE - 8u; }

/* Peut-on semer une valeur à cette adresse fixe ? Dans la RAM du jeu, et hors du code — celui
   de l'ARM9 et celui de l'overlay qu'on essaie, pas celui des autres : les overlays se partagent
   leurs adresses, et les données de l'un tombent presque toujours « dans le code » d'un autre.
   Tant qu'on regardait toutes les zones, aucune globale d'overlay n'était jamais semée, et les
   branches qui en dépendent jamais prises. */
/* Le code, mot par mot : un bit par mot de la RAM du jeu, levé là où vit une fonction de l'ARM9 ou
   de l'overlay qu'on essaie. Une plage du premier au dernier octet de fonction ne suffisait pas :
   les quelques « fonctions » que la cartographie a vues dans les données l'étirent par-dessus
   les globales, qu'on ne semait donc jamais. */
static uint32_t g_code_bits[(MEM_SUIVIE_TAILLE / 4u + 31u) / 32u];

static void code_marquer(const uint8_t *hote, uint32_t octets) {
    if (!hote || hote < g_mem_suivie || hote >= g_mem_suivie + MEM_SUIVIE_TAILLE) return;
    size_t mot = (size_t)(hote - g_mem_suivie) / 4u, fin = mot + (octets + 3u) / 4u;
    for (; mot < fin && mot < MEM_SUIVIE_TAILLE / 4u; mot++) g_code_bits[mot >> 5] |= 1u << (mot & 31u);
}

static void code_bits_refaire(const char *region) {
    memset(g_code_bits, 0, sizeof g_code_bits);
    for (size_t i = 0; i < g_recomp_table_size; i++) {
        const RecompEntry *e = &g_recomp_table[i];
        if (strcmp(e->region, "arm9") && strcmp(e->region, region)) continue;
        code_marquer(mem_resolve(e->start & ~3u, 4), e->end - (e->start & ~3u) + 0x40u);   /* et ses constantes, juste après */
    }
#ifdef SFC_HAVE_ARM7
    if (!strcmp(region, "arm7"))
        for (size_t i = 0; i < g_recomp7_table_size; i++) {
            const RecompEntry *e = &g_recomp7_table[i];
            code_marquer(mem_resolve7(e->start & ~3u, 4), e->end - (e->start & ~3u) + 0x40u);
        }
#endif
}

/* L'adresse telle que ce processeur la voit : la mémoire suivie, hors du bac, hors du code. */
static int semis_permis(Cpu *c, uint32_t adresse) {
    if (dans_le_bac(adresse)) return 0;
    const uint8_t *hote = c->is_arm7 ? mem_resolve7(adresse & ~3u, 4) : mem_resolve(adresse & ~3u, 4);
    if (!hote || hote < g_mem_suivie || hote >= g_mem_suivie + MEM_SUIVIE_TAILLE - 4u) return 0;
    size_t mot = (size_t)(hote - g_mem_suivie) / 4u;
    return !(g_code_bits[mot >> 5] >> (mot & 31u) & 1u);
}

/* Une retouche : une seule chose change dans l'état tiré. Elle est tout entière dans sa graine. */
static void froid_retoucher(Cpu *c, const Reecriture *r, uint64_t graine) {
    uint64_t sauve = g_alea;
    g_alea = graine | 1u;
    unsigned quoi = alea() % 100u;
    /* là où la fonction lit : ses propres décalages, le plus souvent */
    uint32_t d1 = r->nb_decalages && alea() % 100u < 80u ? r->decalages[alea() % (uint32_t)r->nb_decalages] : 4u * (alea() % 16u);
    uint32_t d2 = r->nb_decalages && alea() % 100u < 80u ? r->decalages[alea() % (uint32_t)r->nb_decalages] : 4u * (alea() % 16u);
    uint32_t pointeur = c->r[alea() % 13u];
    uint32_t valeur = alea_valeur();
    if (alea() % 100u < 30u) valeur = alea() % 32u;      /* l'index d'une table de sauts, un état, un compte */
    /* Le plus souvent, on retouche là où le générateur dit que les conditions regardent : tel
       registre, tel mot de l'objet qu'il désigne, la réponse d'une appelée. Treize registres fois
       quarante décalages, c'est cinq cents endroits où écrire ; il y en a rarement plus de dix
       qui comptent. */
    if (r->nb_sites && alea() % 100u < 70u) {
        const Site *site = &r->sites[alea() % (uint32_t)r->nb_sites];
        if (site->genre == 1u) c->r[site->registre] = valeur;
        else if (site->genre == 4u) {                /* ce qu'une appelée rend dans ce registre */
            unsigned appels = g_appels_semence ? g_appels_semence : 4u;
            if (g_nb_forces < 8u) {
                g_forces[g_nb_forces].appel = 1u + alea() % appels;
                g_forces[g_nb_forces].registre = site->registre;
                g_forces[g_nb_forces++].valeur = valeur;
            }
        } else {
            /* On suit la chaîne, maillon après maillon ; celui qui ne désigne rien du bac y est
               ramené — le registre, la globale, ou le mot qu'on vient de lire. */
            uint32_t pointeur = 0, d_ou = 0;         /* d'où l'on tient `pointeur` : une adresse, ou rien */
            int permis = 1;
            if (site->genre == 2u) pointeur = c->r[site->registre];
            else { d_ou = site->adresse & ~3u; permis = semis_permis(c, d_ou); if (permis) pointeur = mem_read32(c, d_ou); }
            for (unsigned k = 0; permis && k < site->n; k++) {
                uint32_t ou = pointeur + (uint32_t)(int32_t)site->d[k];
                if (site->genre == 3u && k == 0u) ou = d_ou;          /* la globale elle-même */
                else if (!dans_le_bac(ou)) {
                    pointeur = alea_pointeur();
                    if (d_ou) mem_write32(c, d_ou, pointeur); else c->r[site->registre] = pointeur;
                    ou = pointeur + (uint32_t)(int32_t)site->d[k];
                }
                if (k + 1u == site->n) {
                    if (site->genre == 3u && k == 0u) mem_write32(c, ou, valeur);
                    else for (unsigned o = 0; o < 4u; o++) mem_write8(c, ou + o, (uint8_t)(valeur >> (8u * o)));
                } else {
                    d_ou = ou & ~3u;
                    pointeur = mem_read32(c, d_ou);
                }
            }
        }
        g_alea = sauve;
        return;
    }
    if (quoi < 20u) c->r[alea() % 13u] = valeur;
    else if (quoi < 28u) { unsigned a = alea() % 13u, b = alea() % 13u; c->r[a] = c->r[b] + alea() % 3u - 1u; }
    else if (quoi < 58u) {                           /* un mot près de ce que désigne un registre */
        if (dans_le_bac(pointeur + d1))
            for (unsigned o = 0; o < 4u; o++) mem_write8(c, pointeur + d1 + o, (uint8_t)(valeur >> (8u * o)));
    } else if (quoi < 78u) {                         /* un mot près de ce que désigne un mot : l'objet dans l'objet */
        uint32_t second = dans_le_bac(pointeur + d1) ? mem_read32(c, (pointeur + d1) & ~3u) : 0;
        if (dans_le_bac(second + d2))
            for (unsigned o = 0; o < 4u; o++) mem_write8(c, second + d2 + o, (uint8_t)(valeur >> (8u * o)));
        else if (dans_le_bac(pointeur + d1))         /* il ne désignait rien : qu'il désigne le bac */
            mem_write32(c, (pointeur + d1) & ~3u, alea_pointeur());
    } else if (quoi < 92u) {                         /* la réponse d'une appelée */
        if (g_nb_forces < 8u) { g_forces[g_nb_forces].appel = 1u + alea() % 6u; g_forces[g_nb_forces].registre = 0; g_forces[g_nb_forces++].valeur = valeur; }
    } else if (quoi < 97u && r->nb_globales) {       /* une globale, hors du code */
        uint32_t ou = r->globales[alea() % (uint32_t)r->nb_globales] & ~3u;
        if (semis_permis(c, ou)) mem_write32(c, ou, valeur);
    } else { c->nf = alea() & 1u; c->zf = alea() & 1u; c->cf = alea() & 1u; c->vf = alea() & 1u; }
    g_alea = sauve;
}

static void zones_code_init(void) {
    for (size_t i = 0; i < g_recomp_table_size; i++) {
        const RecompEntry *e = &g_recomp_table[i];
        if (e->start < 0x02000000u || e->start >= 0x02400000u) continue;   /* l'ITCM n'est pas en RAM */
        size_t z;
        for (z = 0; z < g_nb_zones_code; z++) if (!strcmp(g_zones_code[z].region, e->region)) break;
        if (z == g_nb_zones_code) {
            if (z >= 64) continue;
            g_zones_code[z].region = e->region; g_zones_code[z].debut = e->start; g_zones_code[z].fin = e->end;
            g_nb_zones_code++;
        }
        if (e->start < g_zones_code[z].debut) g_zones_code[z].debut = e->start;
        if (e->end > g_zones_code[z].fin) g_zones_code[z].fin = e->end;
    }
}

/* Le code qui doit rester intact pendant un essai, c'est celui de la fonction qu'on juge — ses
   instructions, ses constantes, ses tables —, que la traduction relit quand la réécriture les
   connaît. Longtemps le banc a exigé que rien du code de l'ARM9, de l'overlay ni de l'ITCM ne
   bouge ; mais ce qu'un essai écrase ailleurs, les deux versions le voient pareillement, et
   l'essai suivant le retrouve remis. Or les chemins qu'aucune partie ne prend — un `this`
   ajusté qui vaudrait zéro, le `r4 == 0` retesté après chaque constructeur — écrivent
   justement à l'adresse 0, 4, 0x18 : le miroir de l'ITCM. Ils étaient tous annulés. */
static const Reecriture *g_jugee_a_froid;

static int code_intact(void) {
    const Reecriture *r = g_jugee_a_froid;
    if (!r || !r->suivi->traduction) return 1;
    uint32_t debut = r->suivi->traduction->start & ~3u, fin = r->suivi->traduction->end + 0x100u;
    uint8_t *vu = !strcmp(r->region, "arm7") ? mem_resolve7(debut, 4) : mem_resolve(debut, 4);
    if (!vu || vu < g_mem_suivie) return 1;
    size_t off = (size_t)(vu - g_mem_suivie);
    if (off + (fin - debut) > MEM_SUIVIE_TAILLE) return 1;
    return !memcmp(vu, g_base_froid.image + off, fin - debut);
}

/* Une seconde passe ne reprend que les fonctions dont il manque des chemins : SFC_FROID_LISTE
   nomme le fichier (« région adresse » par ligne, celui qu'écrit le générateur),
   SFC_FROID_GRAINE change les tirages, SFC_FROID_ESSAIS leur nombre. Les chemins vus
   s'ajoutent d'une passe à l'autre, à la moisson. */
static char *g_froid_liste;
static unsigned g_froid_essais = 120;

static int froid_dans_liste(const Reecriture *r) {
    char cle[64];
    snprintf(cle, sizeof cle, "\n%s 0x%08X\n", region_dite(r), r->entree);
    return strstr(g_froid_liste, cle) != NULL;
}

/* Une machine à états — une boucle autour d'une table de sauts — peut tourner sans jamais
   franchir d'arête arrière : ni la traduction ni la réécriture n'y comptent le temps, et nourrie
   de valeurs tirées au sort elle ne s'arrête pas. Un garde veille : si aucun essai ne finit en
   cinq secondes, il nomme la fonction dans SFC_FROID_EXCLURE, rend la liste de ce qui est fait
   et quitte avec le code 3 ; on relance la tranche, qui l'écartera. */
static volatile uint64_t g_froid_essais_faits;
static const Reecriture *volatile g_froid_en_cours;
static char *g_froid_exclues;

static DWORD WINAPI froid_garde(LPVOID rien) {
    (void)rien;
    uint64_t vu = g_froid_essais_faits;
    int immobile = 0;
    for (;;) {
        Sleep(1000);
        if (g_froid_essais_faits != vu) { vu = g_froid_essais_faits; immobile = 0; continue; }
        if (++immobile < 5) continue;
        const Reecriture *r = g_froid_en_cours;
        const char *ou = getenv("SFC_FROID_EXCLURE");
        if (r && ou) { FILE *f = fopen(ou, "a"); if (f) { fprintf(f, "%s 0x%08X\n", region_dite(r), r->entree); fclose(f); } }
        rt_log("banc à froid : %s ne rend pas la main — écartée, la tranche est à relancer", r ? r->nom : "?");
        natif_rapport();
        _exit(3);
    }
}

static int froid_exclue(const Reecriture *r) {
    char cle[64];
    if (!g_froid_exclues) return 0;
    snprintf(cle, sizeof cle, "\n%s 0x%08X", region_dite(r), r->entree);
    return strstr(g_froid_exclues, cle) != NULL;
}

static void froid_lire_options(void) {
    { const char *x = getenv("SFC_FROID_EXCLURE");
      FILE *fx = x ? fopen(x, "rb") : NULL;
      if (fx) { fseek(fx, 0, SEEK_END); long nx = ftell(fx); fseek(fx, 0, SEEK_SET);
                g_froid_exclues = calloc((size_t)nx + 3, 1); g_froid_exclues[0] = '\n';
                if (fread(g_froid_exclues + 1, 1, (size_t)nx, fx) != (size_t)nx) g_froid_exclues[1] = 0;
                fclose(fx); } }
    CreateThread(NULL, 0, froid_garde, NULL, 0, NULL);
    const char *v = getenv("SFC_FROID_GRAINE");
    if (v) g_alea ^= strtoull(v, NULL, 0) * 0x9E3779B97F4A7C15ull;
    v = getenv("SFC_FROID_ESSAIS");
    if (v && atoi(v) > 0) g_froid_essais = (unsigned)atoi(v);
    v = getenv("SFC_FROID_LISTE");
    FILE *f = v ? fopen(v, "rb") : NULL;
    if (!f) return;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    g_froid_liste = calloc((size_t)n + 3, 1);
    g_froid_liste[0] = '\n';
    if (fread(g_froid_liste + 1, 1, (size_t)n, f) != (size_t)n) { free(g_froid_liste); g_froid_liste = NULL; }
    else g_froid_liste[n + 1] = '\n';
    fclose(f);
}

void natif_banc_froid(Cpu *c9) {
    static int lu;
    static uint64_t quand;
    static unsigned tranche, nombre = 1;
    if (!lu) {
        lu = 1;
        const char *v = getenv("SFC_VERIF_FROID");
        if (v && *v) {
            char *fin;
            quand = strtoull(v, &fin, 0);
            if (*fin == ':') { tranche = (unsigned)strtoul(fin + 1, &fin, 0); if (*fin == '/') nombre = (unsigned)strtoul(fin + 1, NULL, 0); }
            if (!nombre) nombre = 1;
            if (!quand) quand = 1;
        }
    }
    if (!quand || hw_frames() < quand) return;

    static Etat depart;
    Etat *base_p = &g_base_froid;
    etat_allouer(&depart); etat_allouer(base_p);
    zones_code_init();
#define base (*base_p)
    niveaux_preparer();
    froid_lire_options();
    g_plan ^= g_alea * 0x9E3779B97F4A7C15ull;     /* SFC_FROID_GRAINE change aussi le plan */
    if (!g_plan) g_plan = 1;
    g_banc_froid = 1;
    g_temps_fige = 1;                        /* lire un registre de balayage n'y fait plus avancer l'horloge */
    g_compter_pile = 1;                      /* à froid, rien n'excuse un octet d'écart */
    rt_bouchon_partout(bouchon);             /* toutes les appelées répondent par le bouchon */
    g_mmio_bouchon = bouchon_materiel;       /* et le matériel aussi */
    c9->depth = 0;
    etat_prendre(&depart, c9);
    /* Les fonctions du coprocesseur s'essaient avec un contexte à lui — sa carte mémoire, ses
       registres matériels —, copie de celui qu'il avait quand le jeu s'est arrêté. */
    static Cpu c7;
#ifdef SFC_HAVE_ARM7
    c7 = *arm7_cpu();
#endif
    c7.is_arm7 = 1; c7.depth = 0; c7.redirect = 0; c7.in_irq = 0;

    size_t essayees = 0, ecartees = 0;
    const char *region = "";
    for (size_t k = 0; g_auto[k].fn; k++) {
        const Reecriture *r = &g_auto[k];
        Cpu *c = !strcmp(r->region, "arm7") ? &c7 : c9;
        if (k % nombre != tranche) continue;
        { const char *seul = getenv("SFC_FROID_SEUL");      /* une seule fonction, pour enquêter */
          if (seul && (uint32_t)strtoul(seul, NULL, 0) != r->entree) continue; }
        if (g_froid_liste && !froid_dans_liste(r)) continue;
        if (froid_exclue(r) || r->suivi->reservee) continue;
        if (r->materiel == 2) { ecartees++; continue; }      /* récursive : elle se juge en jeu */
        if (strcmp(region, r->region)) {     /* on change de module : son code doit être en mémoire */
            region = r->region;
            etat_rendre(&depart, c);
            if (!rt_overlay_installer(region)) { rt_log("banc à froid : %s introuvable", region); region = ""; continue; }
            g_region_froid = region;
            code_bits_refaire(region);
            bac_remplir();
            etat_prendre(&base, c);
        }
        /* une secondaire n'a pas de crochet dans la traduction : le banc l'appelle lui-même */
        static NativeHook sans_crochet;
        NativeHook *ou = r->englobante ? &sans_crochet : rt_hook_region(r->region, r->entree, r->crochet);
        if (!ou) continue;
        if (getenv("SFC_FROID_BAVARD")) rt_log("banc à froid : %s", r->nom);
        Surveillance *s = r->suivi;
        suivi_vider(s, r->nb_chemins);
        s->emplacement = ou;
        s->traduction = rt_entree_region(r->region, r->englobante ? r->englobante : r->entree);
        if (!s->traduction) continue;
        s->surveillee = 1;
        g_remarquables = r->remarquables;
        g_nb_remarquables = r->nb_remarquables;
        if (r->nb_chemins > 1) {             /* un bac à son goût : ses constantes y sont semées */
            etat_rendre(&base, c);
            g_alea = plan() | 1u;
            bac_remplir();
            etat_prendre(&base, c);
        }
        g_nb_semences = 0;
        for (unsigned essai = 0; essai < g_froid_essais; essai++) {
            if (essai >= 12u && s->chemins_vus >= (uint32_t)r->nb_chemins) break;
            if (s->divergences >= 3) break;
            etat_rendre(&base, c);
            /* Un essai est tout entier dans sa graine, et dans celles des retouches qu'on lui fait.
               Celui qui découvre un chemin devient une semence : on repartira de lui, en ne changeant
               qu'une chose ou deux — un registre, un mot là où la fonction lit, la réponse d'une
               appelée. Six conditions empilées ne se tirent pas d'un coup ; elles se franchissent
               une à une, chaque chemin atteint servant de marchepied au suivant. */
            Semence semence = {0};
            int neuve = essai < 24u || !g_nb_semences || plan() % 100u < 25u;
            if (neuve) semence.graine = plan();
            else {
                semence = g_semences[plan() % g_nb_semences];
                if (semence.n >= MAX_RETOUCHES) semence.n = MAX_RETOUCHES - 2;
            }
            g_nb_forces = 0;
            g_alea = semence.graine | 1u;
            for (int n = 0; n < 13; n++) c->r[n] = alea_valeur();
            for (int n = 1; n < 8; n++) if (alea() % 100u < 15u) c->r[n] = c->r[n - 1];   /* des égaux, pour les cmp */
            /* Les globales qu'elle lit gardent, à froid, la valeur du jeu à cet instant : la branche
               qui en dépend ne serait jamais prise. On y sème — hors du code, qu'on n'écrase pas. */
            for (int g = 0; g < r->nb_globales; g++) {
                uint32_t ou = r->globales[g] & ~3u;
                if (alea() % 100u < 30u) continue;
                if (semis_permis(c, ou)) mem_write32(c, ou, alea_valeur());
            }
            /* Des pointeurs croisés : un mot, près de ce que désigne un registre, qui vaut un autre
               registre plus une des constantes de la fonction — « le nœud est-il la sentinelle ? »
               ne se tire pas au hasard. */
            for (int n = 0; n < 8; n++) {
                if (c->r[n] - BAC_BASE >= BAC_TAILLE - 0x100u) continue;
                for (unsigned j = 0; j < 8; j++) {
                    if (alea() % 100u >= 12u) continue;
                    uint32_t k = r->nb_remarquables && (alea() & 1u) ? r->remarquables[alea() % (uint32_t)r->nb_remarquables] : 4u * (alea() % 8u);
                    mem_write32(c, (c->r[n] & ~3u) + 4u * j, c->r[alea() % 8u] + k);
                }
            }
            c->r[13] = BAC_BASE + 0xE000u;
            c->r[14] = 0x02000001u + (alea() % 0x1000u & ~1u);
            c->nf = alea() & 1u; c->zf = alea() & 1u; c->cf = alea() & 1u; c->vf = alea() & 1u;
            c->thumb = s->traduction->thumb;     /* le mode de la fonction, comme si on y arrivait par bx */
            c->depth = 0;
            g_bouchon_graine = ((uint64_t)alea() << 32) | alea();
            /* une retouche se rejoue telle qu'elle fut faite : avec le compte d'appels d'alors */
            for (unsigned k = 0; k < semence.n; k++) {
                g_appels_semence = semence.appels_lors[k];
                froid_retoucher(c, r, semence.retouches[k]);
            }
            if (!neuve)
                for (unsigned plus = 1u + plan() % 2u; plus--; ) {
                    uint64_t g = plan();
                    g_appels_semence = semence.appels;
                    froid_retoucher(c, r, g);
                    semence.appels_lors[semence.n] = semence.appels;
                    semence.retouches[semence.n++] = g;
                }
            g_froid_en_cours = r;
            g_jugee_a_froid = r;
            g_froid_essais_faits++;
            uint64_t avant = s->divergences;
            uint32_t chemins_avant = s->chemins_vus;
            verifier(c, r);
            semence.appels = g_journal_n;
            if (s->chemins_vus > chemins_avant)
                g_semences[g_nb_semences < MAX_SEMENCES ? g_nb_semences++ : plan() % MAX_SEMENCES] = semence;
            if (s->divergences != avant && s->divergences == 1)
                rt_log("banc à froid : %s (%s 0x%08X) diverge à l'essai %u", r->nom, r->region, r->entree, essai);
        }
        *ou = bouchon;                       /* elle redevient une appelée comme les autres */
        essayees++;
    }
#undef base
    rt_log("banc à froid : %zu feuilles essayées, %zu écartées parce qu'elles nomment le matériel", essayees, ecartees);
    natif_rapport();
    rt_sortir(0);
}

/* ------------------------------------------------------------------ mise en place */

static int adresse_visee(const char *liste, uint32_t adresse) {
    char attendu[16];
    snprintf(attendu, sizeof attendu, "%08X", adresse);
    for (const char *p = liste; *p; p++) {
        if ((p[0] == '0' && (p[1] | 32) == 'x')) p += 2;
        size_t n = strspn(p, "0123456789abcdefABCDEF");
        if (n && (uint32_t)strtoul(p, NULL, 16) == adresse) return 1;
        p += n;
        if (!*p) break;
    }
    return 0;
}

/* « tout » vise les deux tables, « manuel » et « auto » chacune la sienne ; sinon une liste
   d'adresses. Vérifier, on le peut de tout. Mettre en service, seulement de ce qui est prouvé :
   une feuille du générateur que le banc n'a jamais vue tourner, ni elle ni une autre de son
   gabarit, reste sur sa traduction — sauf à la nommer, ou à demander « auto-tout » pour juger
   le lot entier aux empreintes. */
static int visee(const char *liste, int table, const Reecriture *r, int pour_activer) {
    static const char *noms_tables[2] = {"manuel", "auto"};
    if (!liste || !*liste) return 0;
    /* « auto7 », « auto7-tout » : le coprocesseur seul, prouvé ou tout entier */
    if (!strncmp(liste, "auto7", 5)) {
        if (table != 1 || strcmp(r->region, "arm7")) return 0;
        return !strcmp(liste, "auto7-tout") ? r->prouvee >= 0 : !pour_activer || r->prouvee > 0;
    }
    if (table == 1 && !strcmp(liste, "auto-tout")) return r->prouvee >= 0;
    /* « entier » : le jeu entier en natif — les réécritures à la main et toutes celles du
       générateur qui ne sont pas fautives, prouvées ou non */
    if (!strcmp(liste, "entier")) return pour_activer && (table == 0 || r->prouvee >= 0);
    /* « auto-reste » : ne juger que ce qui n'est pas encore prouvé — cinq fois moins de
       fonctions surveillées, et des niveaux libres pour elles */
    if (table == 1 && !strcmp(liste, "auto-reste")) return !pour_activer && r->prouvee == 0;
    if (!strcmp(liste, "tout") || !strcmp(liste, noms_tables[table]))
        return !pour_activer || table == 0 || r->prouvee > 0;
    return adresse_visee(liste, r->entree);
}

/* ------------------------------------------------------------------ espionnage */

/* SFC_ESPION=0x…[,0x…] : journalise chaque appel de ces fonctions, sans rien changer. Savoir
 * si un code est seulement atteint vaut souvent mieux que de le lire longuement : c'est ainsi
 * qu'on apprend qu'un menu de mise au point n'est jamais construit, ou qu'une fonction très
 * référencée n'est jamais appelée. */
#define MAX_ESPIONS 16
#define MAX_EMPLACEMENTS 8
static struct {
    uint32_t entree;
    uint64_t appels;
    NativeHook *emplacements[MAX_EMPLACEMENTS];
    size_t nb_emplacements;
} g_espions[MAX_ESPIONS];
static size_t g_nb_espions;
static unsigned g_espion_bavard = 8;     /* les premiers appels sont détaillés, puis on compte */

static void espionner(size_t i, Cpu *c) {
    g_espions[i].appels++;
    /* SFC_ESPION_DEPUIS=image : la chronologie des appels à partir de cette image (40 lignes), cycle compris */
    static long long depuis = -2; static int lignes;
    extern uint64_t hw_edges(void);
    if (depuis == -2) { const char *d = getenv("SFC_ESPION_DEPUIS"); depuis = d ? atoll(d) : -1; }
    if (depuis >= 0 && (long long)hw_frames() >= depuis && lignes < 40) {
        lignes++;
        rt_log("espion 0x%08X : image %llu, ligne %u, cycle %llu, fil %p, in_irq=%u",
               g_espions[i].entree, (unsigned long long)hw_frames(), hw_vcount(), (unsigned long long)hw_now(),
               (void *)c, c->in_irq);
    }
    if (g_espions[i].appels <= g_espion_bavard)
        rt_log("espion 0x%08X : appel %llu à l'image %llu — r0=%08X r1=%08X r2=%08X r3=%08X lr=%08X",
               g_espions[i].entree, (unsigned long long)g_espions[i].appels,
               (unsigned long long)hw_frames(), c->r[0], c->r[1], c->r[2], c->r[3], c->r[14]);

    NativeHook gardes[MAX_EMPLACEMENTS];
    for (size_t k = 0; k < g_espions[i].nb_emplacements; k++) {
        gardes[k] = *g_espions[i].emplacements[k];
        *g_espions[i].emplacements[k] = NULL;
    }
    const RecompEntry *e = rt_find(c, g_espions[i].entree | (c->thumb & 1u));
    LARGE_INTEGER q0, q1, fq;
    uint64_t ar0 = hw_edges(), cy0 = hw_now(), fr0 = hw_frames();
    QueryPerformanceCounter(&q0);
    if (e) e->fn(c, g_espions[i].entree);
    QueryPerformanceCounter(&q1); QueryPerformanceFrequency(&fq);
    if (depuis >= 0 && (long long)fr0 >= depuis && lignes < 80) {
        lignes++;
        rt_log("   espion 0x%08X rendue : %.3f ms, %llu arêtes, %llu cycles, %llu images passées",
               g_espions[i].entree, (double)(q1.QuadPart - q0.QuadPart) * 1000.0 / (double)fq.QuadPart,
               (unsigned long long)(hw_edges() - ar0), (unsigned long long)(hw_now() - cy0),
               (unsigned long long)(hw_frames() - fr0));
    }
    for (size_t k = 0; k < g_espions[i].nb_emplacements; k++)
        *g_espions[i].emplacements[k] = gardes[k];
    if (!e) rt_fatal(c, "espion : aucune traduction à 0x%08X", g_espions[i].entree);
}

#define ESPION(i) static void espion_##i(Cpu *c) { espionner(i, c); }
ESPION(0) ESPION(1) ESPION(2) ESPION(3) ESPION(4) ESPION(5) ESPION(6) ESPION(7)
ESPION(8) ESPION(9) ESPION(10) ESPION(11) ESPION(12) ESPION(13) ESPION(14) ESPION(15)
#undef ESPION
static const NativeHook g_crochets_espion[MAX_ESPIONS] = {
    espion_0, espion_1, espion_2, espion_3, espion_4, espion_5, espion_6, espion_7,
    espion_8, espion_9, espion_10, espion_11, espion_12, espion_13, espion_14, espion_15,
};

static void espion_rapport(void) {
    for (size_t i = 0; i < g_nb_espions; i++)
        rt_log("espion 0x%08X : %llu appels", g_espions[i].entree,
               (unsigned long long)g_espions[i].appels);
}

static void espions_init(void) {
    const char *liste = getenv("SFC_ESPION");
    if (!liste || !*liste) return;
    atexit(espion_rapport);
    for (const char *p = liste; *p && g_nb_espions < MAX_ESPIONS; ) {
        char *fin;
        uint32_t adresse = (uint32_t)strtoul(p, &fin, 0);
        if (fin == p) break;
        size_t i = g_nb_espions++;
        g_espions[i].entree = adresse & ~1u;
        rt_hook(g_espions[i].entree, g_crochets_espion[i], "espion");
        g_espions[i].nb_emplacements =
            rt_hook_emplacements(g_espions[i].entree, g_espions[i].emplacements, MAX_EMPLACEMENTS);
        rt_log("espion : 0x%08X surveillée (%zu emplacements)", g_espions[i].entree,
               g_espions[i].nb_emplacements);
        p = (*fin == ',') ? fin + 1 : fin;
        if (!*p) break;
    }
}

void natif_init(void) {
    auto_reunir();
    modifications_init();
    g_dispatch_milieu = dispatch_milieu;
    g_fibre_du_banc = GetCurrentFiber();
    /* Ce que le runtime remplace déjà par ses propres fonctions — la commutation de contexte du
       SDK — n'est ni à réécrire ni à juger : sa traduction ne tourne jamais. */
    size_t reservees = 0;
    for (const Reecriture *r = g_auto; r->fn; r++)
        if (rt_hook_present(r->region, r->englobante ? r->englobante : r->entree)) { r->suivi->reservee = 1; reservees++; }
    if (reservees) rt_log("natif : %zu fonctions laissées au runtime, qui a déjà son crochet dessus", reservees);
    espions_init();
    atexit(natif_rapport);       /* le verdict doit sortir même quand le jeu s'arrête seul */
    const char *verif = getenv("SFC_VERIF");
    const char *actif = getenv("SFC_NATIF");
    /* Le jeu tourne en natif par défaut : les deux processeurs, toutes les réécritures. Elles ont
       tenu 520 000 images aux empreintes et au son de la traduction, et une vraie partie.
       SFC_NATIF=non rend la traduction pure — la référence de toutes les comparaisons. Un banc
       (SFC_VERIF, SFC_VERIF_FROID) garde la traduction autour de ce qu'il juge, sauf demande. */
    if (!actif && !verif && !getenv("SFC_VERIF_FROID")) actif = "entier";
    if (actif && (!strcmp(actif, "non") || !strcmp(actif, "0") || !strcmp(actif, "traduction"))) actif = NULL;
    rt_log("natif : %s", actif ? actif : "rien en service — la traduction pure");
    const char *max = getenv("SFC_VERIF_MAX");
    g_compter_pile = getenv("SFC_VERIF_PILE") != NULL;
    g_attente_directe = getenv("SFC_ATTENTE_DIRECTE") != NULL;
    { const char *pas = getenv("SFC_PAS_MS");
      if (pas) { g_pas_ms = (uint32_t)strtoul(pas, NULL, 0);
                 rt_log("boucle principale : seuil d'attente de %u ms au lieu de 20 — ce n'est plus le jeu d'origine", g_pas_ms); } }
    if (max) g_max_appels = (unsigned)strtoul(max, NULL, 0);
    { const char *t = getenv("SFC_TRACE_NATIF"); g_trace_natif = t ? strtoull(t, NULL, 0) * 2u : 0; }

    const Reecriture *tables[2] = {g_reecritures, g_auto};
    size_t poses = 0, absentes = 0;
    for (int t = 0; t < 2; t++) {
        for (const Reecriture *r = tables[t]; r->fn; r++) {
            int a_verifier = visee(verif, t, r, 0);
            int a_activer = visee(actif, t, r, 1);
            /* SFC_NATIF_TRANCHE=a-b : seules les fonctions du générateur de rang a à b-1 sont mises
               en service — de quoi chercher par dichotomie celle qui change les empreintes. */
            if (a_activer && t == 1) {
                static long de = -1, a_ = -1;
                if (de < 0) { const char *v = getenv("SFC_NATIF_TRANCHE"); de = 0; a_ = 0x7FFFFFFF;
                              if (v) { char *fin; de = strtol(v, &fin, 0); if (*fin == '-') a_ = strtol(fin + 1, NULL, 0); } }
                long rang = (long)(r - tables[t]);
                if (rang < de || rang >= a_) a_activer = 0;
                /* SFC_NATIF_SANS=0x…,0x… : celles-là gardent leur traduction — les coupables déjà
                   trouvés, pour que la dichotomie cherche le suivant */
                { const char *sans = getenv("SFC_NATIF_SANS");
                  if (sans && *sans && adresse_visee(sans, r->entree)) a_activer = 0; }
            }
            if (!a_verifier && !a_activer) continue;
            if (t == 1 && r->suivi->reservee) continue;
            if (a_verifier) niveaux_preparer();
            int par_crochet = a_verifier || g_trace_natif;
            /* une secondaire n'a pas de crochet dans la traduction, qui n'en a qu'à l'entrée des
               fonctions : ce sont les sites qui la rejoignent qui la retrouvent (natif_secondaire) */
            static NativeHook sans_crochet;
            NativeHook *ou = r->englobante ? &sans_crochet
                           : rt_hook_region(r->region, r->entree, par_crochet ? r->crochet : r->fn);
            if (!ou || (r->englobante && !rt_entree_region(r->region, r->englobante))) { absentes++; continue; }
            suivi_vider(r->suivi, r->nb_chemins);
            r->suivi->emplacement = ou;
            r->suivi->traduction = rt_entree_region(r->region, r->englobante ? r->englobante : r->entree);
            r->suivi->surveillee = par_crochet;
            r->suivi->en_service = !par_crochet;
            if (!a_verifier && g_trace_natif) r->suivi->appels = (uint64_t)-1;   /* tracer sans comparer */
            poses++;
            if (t == 0)
                rt_log("%s : %s (%s 0x%08X)", a_verifier ? "vérif" : "natif", r->nom, r->region, r->entree);
        }
    }
    if (poses || absentes)
        rt_log("natif : %zu réécritures en place%s, %zu sans traduction où s'accrocher",
               poses, verif ? " pour vérification" : "", absentes);
}

void natif_rapport(void) {
    static int dit;
    if (dit++) return;
    const Reecriture *tables[2] = {g_reecritures, g_auto};
    size_t surveillees = 0, prouvees = 0, muettes = 0, fausses = 0;
    uint64_t compares = 0, vus = 0, interrompus = 0;
    for (int t = 0; t < 2; t++)
        for (const Reecriture *r = tables[t]; r->fn; r++) {
            const Surveillance *s = r->suivi;
            if (!s->surveillee || s->appels == (uint64_t)-1) continue;
            surveillees++;
            compares += s->appels;
            vus += s->vus; interrompus += s->interrompues;
            if (s->divergences) {
                fausses++;
                rt_log("vérif %s (%s 0x%08X) : %llu divergences sur %llu appels comparés", r->nom, r->region,
                       r->entree, (unsigned long long)s->divergences, (unsigned long long)s->appels);
            } else if (!s->appels || !s->chemins_vus) {   /* commencée sans finir : elle ne revient pas */
                muettes++;
                if (t == 0) rt_log("vérif %s : %llu appels vus, aucun comparé%s — rien n'est prouvé", r->nom,
                                   (unsigned long long)s->vus, s->interrompues ? " (le monde bougeait)" : "");
            } else {
                prouvees++;
                if (t == 0) rt_log("vérif %s : %llu appels vus, %llu comparés, 0 divergence", r->nom,
                                   (unsigned long long)s->vus, (unsigned long long)s->appels);
            }
        }
    if (surveillees)
        rt_log("vérif : %zu fonctions surveillées — %zu sans écart, %zu fautives, %zu sans comparaison ; %llu appels comparés, "
               "%llu vus, %llu interrompus parce que le monde bougeait",
               surveillees, prouvees, fausses, muettes, (unsigned long long)compares,
               (unsigned long long)vus, (unsigned long long)interrompus);
    /* la liste de celles qui ont passé le banc, pour que le générateur sache lesquelles le sont */
    /* En service, sans banc : la liste dit quels chemins chaque fonction a pris. Si les empreintes
       du parcours sont celles de la référence, ces chemins-là ont fait leur travail à
       l'identique — c'est la preuve « aux empreintes », la seule que peuvent recevoir les
       fonctions que le monde interrompt toujours ou qui parlent au matériel. */
    const char *liste = getenv("SFC_VERIF_LISTE");
    size_t en_service = 0;
    for (const Reecriture *r = tables[1]; r->fn; r++) en_service += r->suivi->en_service && r->suivi->chemins_vus;
    if (liste && (surveillees || en_service)) {
        FILE *f = fopen(liste, "w");
        if (f) {
            for (int t = 0; t < 2; t++)
                for (const Reecriture *r = tables[t]; r->fn; r++) {
                    const Surveillance *s = r->suivi;
                    int service = t == 1 && s->en_service && s->chemins_vus;
                    if (!service && (!s->surveillee || s->appels == (uint64_t)-1)) continue;
                    fprintf(f, "%s 0x%08X %s %llu %llu ", region_dite(r), r->entree,
                            service ? "service" : s->divergences ? "FAUX" : s->appels && s->chemins_vus ? "ok" : "muette",
                            (unsigned long long)s->appels, (unsigned long long)s->divergences);
                    /* les chemins vus : un grand nombre en hexadécimal, le chemin 0 au bit de poids faible */
                    if (r->nb_chemins && s->chemins) {
                        int premier = 1;
                        for (int o = (r->nb_chemins + 7) / 8 - 1; o >= 0; o--) {
                            if (premier && !s->chemins[o] && o) continue;
                            fprintf(f, premier ? "%X" : "%02X", s->chemins[o]);
                            premier = 0;
                        }
                    } else fputs("1", f);
                    fprintf(f, " %d %08X\n", r->nb_chemins ? r->nb_chemins : 1, r->empreinte);
                }
            fclose(f);
        }
    }
}

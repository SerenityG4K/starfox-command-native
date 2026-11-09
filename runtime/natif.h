/*
 * Ce que partagent natif.c et les fonctions que le générateur écrit dans generated/.
 *
 * Le générateur produit des dizaines de milliers de fonctions ; dans un seul fichier, le
 * compilateur y passerait une heure. Elles sont donc réparties en parties compilées en
 * parallèle, et c'est ici que vivent les types de leur table, et les aides qu'elles appellent.
 *
 * Sous licence MIT.
 */
#ifndef SFC_NATIF_H
#define SFC_NATIF_H

#include "rt.h"
#include "hw.h"
#include "memory.h"

/* Ce que le banc retient d'une fonction qu'il surveille. */
typedef struct {
    uint64_t appels, divergences, vus;   /* `vus` compte aussi ce qu'on n'a pas comparé :
                                            une fonction jamais atteinte se reconnaît là */
    NativeHook *emplacement;             /* où le crochet est posé, pour l'ôter le temps d'un appel */
    int surveillee;
    int en_service;                      /* elle remplace sa traduction : on note les chemins qu'elle prend */
    int reservee;                        /* le runtime a déjà son crochet sur cette fonction : on n'y touche pas */
    const RecompEntry *traduction;       /* la fonction traduite, appelée telle quelle par le banc */
    uint8_t *chemins;                    /* fonctions du générateur : un bit par chemin qu'on a vu tourner */
    uint32_t chemins_vus;                /* combien de bits sont levés */
    uint64_t interrompues;               /* comparaisons abandonnées : le monde a bougé pendant la traduction */
} Surveillance;

/* De quelle entrée dépend une condition — ce que le banc à froid retouche de préférence. Genre 1, un
   registre d'entrée ; 2, un mot au bout d'une chaîne de décalages partie de ce registre (l'objet,
   l'objet dans l'objet, le nœud) ; 3, la même chaîne partie d'une globale ; 4, un registre tel
   qu'une appelée le rend. */
typedef struct { uint8_t genre, registre, n; int16_t d[3]; uint32_t adresse; } Site;

typedef struct {
    const char *region;         /* « arm9 », « ovl0 »… : plusieurs overlays partagent leurs adresses */
    uint32_t entree;            /* adresse d'origine dans le binaire du jeu */
    NativeHook fn;              /* la réécriture */
    const char *nom;
    const char *preuve;         /* ce qui établit ce qu'elle fait */
    NativeHook crochet;         /* le crochet de vérification qui lui est propre */
    Surveillance *suivi;
    int saut_final;             /* elle finit par un saut vers une autre fonction : le banc
                                   compare à l'instant du saut, sans suivre la cible deux fois */
    int prouvee;                /* table du générateur : 1 le banc l'a comparée sans écart, 2 il l'a
                                   fait pour une feuille de même gabarit, 0 rien encore, -1 en faute */
    int nb_chemins;             /* table du générateur : combien de chemins mènent à une sortie */
    int materiel;               /* elle nomme un registre matériel : on ne l'essaie pas à froid */
    const uint32_t *remarquables;   /* les constantes auxquelles elle compare, pour le banc à froid */
    int nb_remarquables;
    uint32_t empreinte;         /* du code émis : une preuve ne vaut que pour le code comparé */
    const uint32_t *globales;   /* les adresses fixes qu'elle lit : le banc à froid y sème des valeurs */
    int nb_globales;
    uint32_t englobante;        /* réécriture secondaire : `entree` est au milieu de la fonction dont
                                   voici l'entrée ; 0 pour une fonction prise à son entrée */
    const uint16_t *decalages;  /* les décalages auxquels elle lit : « [arg0 + 0x160] » ne se tire pas au
                                   hasard, le banc à froid écrit là quand il mute un essai */
    int nb_decalages;
    const Site *sites;          /* de quelles entrées dépendent ses conditions */
    int nb_sites;
} Reecriture;

/* Le banc : les deux versions, puis le verdict (natif.c). */
void natif_verifier(Cpu *c, const Reecriture *r);

/* Les appels, tels que la traduction les fait (natif.c). */
void natif_appel_direct(Cpu *c, uint32_t adresse, uint32_t retour);
void natif_appel_dispatch(Cpu *c, uint32_t adresse, uint32_t retour);
void natif_reprendre(Cpu *c, uint32_t entree);
void natif_suite(Cpu *c, uint32_t adresse);     /* la suite dans une autre fonction, sans appel */

/* L'appel direct lie la fonction même que la traduction lie par son symbole : celle de telle
   région, à telle entrée. Deux fonctions se chevauchent parfois, et chercher par adresse n'en
   rendrait pas forcément la même ; chaque site d'appel retrouve donc la sienne une fois, et la
   garde — ce qui épargne aussi, à chaque appel, la recherche et le test de l'overlay présent. */
/* On entre parfois dans une fonction ailleurs qu'à son entrée — la division signée finit dans la
   non signée, un thread qui reprend retombe au milieu de l'ordonnanceur. Le crochet d'une
   fonction traduite ne joue qu'à son entrée : les réécritures secondaires se retrouvent donc
   d'ici, par (région, entrée de la fonction entière, adresse), une fois par site. */
const Reecriture *natif_secondaire(const char *region, uint32_t englobante, uint32_t adresse);
int natif_secondaire_lancer(Cpu *c, const Reecriture *r);   /* 0 si elle n'est ni en service ni jugée */
void natif_appel_entree(Cpu *c, const RecompEntry *cible, const Reecriture *secondaire,
                        uint32_t adresse, uint32_t retour);
#define SECONDAIRE_(region, entree, adresse) \
    static const Reecriture *sec_; static int cherchee_; \
    if ((adresse) != (entree) && !cherchee_) { cherchee_ = 1; sec_ = natif_secondaire((region), (entree), (adresse)); }
#define APPEL_DIRECT(c, region, entree, adresse, retour) do { \
    static const RecompEntry *cible_; \
    SECONDAIRE_(region, entree, adresse) \
    if (!cible_) cible_ = rt_entree_region((region), (entree)); \
    natif_appel_entree((c), cible_, sec_, (adresse), (retour)); } while (0)
#define SUITE_DIRECTE(c, region, entree, adresse) do { \
    static const RecompEntry *cible_; \
    SECONDAIRE_(region, entree, adresse) \
    if (!cible_) cible_ = rt_entree_region((region), (entree)); \
    if (sec_ && natif_secondaire_lancer((c), sec_)) break; \
    if (cible_) cible_->fn((c), (adresse)); else natif_suite((c), (adresse)); } while (0)
#define appel_direct   natif_appel_direct
#define appel_dispatch natif_appel_dispatch
#define appel_indirect natif_appel_dispatch
#define reprendre      natif_reprendre

/* La table du générateur, toutes parties réunies, et le suivi de chaque entrée, indexés par
   la place de la fonction dans l'ensemble. Le crochet d'une fonction passe l'entrée réunie :
   c'est elle qui porte le suivi. */
extern const Reecriture *g_auto_reuni;
extern Surveillance *g_suivi_auto;
/* Les fonctions du coprocesseur suivent celles de l'ARM9 dans la table réunie ; leurs fichiers
   (generated7/, NATIF_ARM7) comptent leurs rangs à partir de leur propre début. */
extern const Reecriture *g_auto7_reuni;
extern Surveillance *g_suivi_auto7;
#ifdef NATIF_ARM7
#define g_auto_reuni g_auto7_reuni
#define g_suivi_auto g_suivi_auto7
#endif
/* Chaque chemin note son passage : le banc sait ainsi lesquels il a comparés. */
static inline void natif_chemin(Surveillance *s, unsigned numero) {
    uint8_t m = (uint8_t)(1u << (numero & 7u));
    if (s->chemins && !(s->chemins[numero >> 3] & m)) { s->chemins[numero >> 3] |= m; s->chemins_vus++; }
}
#define CHEMIN(feuille, numero) natif_chemin(&g_suivi_auto[feuille], (numero))

/* Les points de modification (runtime/modifications.c) : le générateur place MODIF(c, rang) avant
   l'instruction que nomme la liste ; le crochet reste nul tant que la modification n'est pas
   demandée, et la fonction fait alors exactement ce que fait la traduction. */
extern NativeHook g_modifs[];
#define MODIF(c, rang) do { if (g_modifs[rang]) g_modifs[rang](c); } while (0)
void modifications_init(void);

#endif

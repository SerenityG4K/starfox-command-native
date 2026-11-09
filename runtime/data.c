/*
 * Le dossier de données, lu à la place de la cartouche.
 *
 * Le jeu demande ses données par adresse, comme au lecteur de la console. Cette couche
 * garde cette correspondance mais sert les octets depuis des fichiers séparés : un index
 * dit quelle zone de l'espace d'adressage vit dans quel fichier, et l'on y lit à la volée.
 *
 * L'image de cartouche n'est alors plus nécessaire, et chaque fichier du jeu devient
 * remplaçable — c'est la première couche rendue native.
 *
 * L'index est produit par tools/build_data.py.
 *
 * Sous licence MIT.
 */
#include "rt.h"
#include "hw.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t offset;      /* début de la zone dans l'espace de la cartouche */
    uint32_t taille;
    uint32_t nom;         /* position du chemin dans la table des noms */
} Zone;

static Zone *g_zones;
static uint32_t g_nb;
static char *g_noms;
static char g_racine[512];
static int g_pret;
static uint32_t g_fin_systeme;   /* fin de la dernière zone « system/ » : le préambule s'arrête là */
static uint32_t g_fin_totale;

/* Les quelques derniers fichiers ouverts restent ouverts : le jeu lit ses données par
   petits morceaux consécutifs, et les rouvrir à chaque secteur coûterait bien plus que
   la lecture elle-même. */
#define CACHE 8
static struct { uint32_t zone; FILE *f; } g_cache[CACHE];
static int g_suivant;

static FILE *ouvrir(uint32_t i) {
    for (int k = 0; k < CACHE; k++)
        if (g_cache[k].f && g_cache[k].zone == i) return g_cache[k].f;
    char chemin[1024];
    snprintf(chemin, sizeof chemin, "%s/%s", g_racine, g_noms + g_zones[i].nom);
    FILE *f = fopen(chemin, "rb");
    if (!f) return NULL;
    if (g_cache[g_suivant].f) fclose(g_cache[g_suivant].f);
    g_cache[g_suivant].f = f;
    g_cache[g_suivant].zone = i;
    g_suivant = (g_suivant + 1) % CACHE;
    return f;
}

/* Charge l'index. Rend 0 si le dossier n'existe pas : on retombe alors sur la cartouche. */
int data_init(const char *racine) {
    if (g_pret) return 1;
    char chemin[512];
    snprintf(chemin, sizeof chemin, "%s/index.bin", racine);
    FILE *f = fopen(chemin, "rb");
    if (!f) return 0;
    char magie[4];
    uint32_t version, nb, taille_noms;
    if (fread(magie, 1, 4, f) != 4 || memcmp(magie, "SFCD", 4) ||
        fread(&version, 4, 1, f) != 1 || fread(&nb, 4, 1, f) != 1 ||
        fread(&taille_noms, 4, 1, f) != 1 || version != 1) {
        fclose(f);
        rt_log("données : %s n'est pas un index reconnu", chemin);
        return 0;
    }
    g_zones = malloc((size_t)nb * sizeof *g_zones);
    g_noms = malloc(taille_noms + 1u);
    if (!g_zones || !g_noms || fread(g_zones, sizeof *g_zones, nb, f) != nb ||
        fread(g_noms, 1, taille_noms, f) != taille_noms) {
        fclose(f);
        rt_log("données : index illisible");
        return 0;
    }
    fclose(f);
    g_noms[taille_noms] = 0;
    for (uint32_t i = 0; i < taille_noms; i++)
        if (g_noms[i] == '\n') g_noms[i] = 0;      /* un chemin par ligne, terminé par un zéro */
    g_nb = nb;
    snprintf(g_racine, sizeof g_racine, "%s", racine);
    for (uint32_t i = 0; i < nb; i++) {
        uint32_t fin = g_zones[i].offset + g_zones[i].taille;
        if (fin > g_fin_totale) g_fin_totale = fin;
        if (!strncmp(g_noms + g_zones[i].nom, "system/", 7) && fin > g_fin_systeme) g_fin_systeme = fin;
    }
    g_pret = 1;
    rt_log("données : %u zones lues dans %s (%.1f Mo, préambule %u Ko)", nb, racine,
           g_fin_totale / 1048576.0, g_fin_systeme / 1024u);
    return 1;
}

int data_disponible(void) { return g_pret; }
uint32_t data_taille(void) { return g_fin_totale; }

/* La zone qui couvre cette adresse, ou, à défaut, la première qui commence après elle :
   `couvre` dit lequel des deux cas. Les zones sont triées et ne se chevauchent pas. */
static uint32_t trouver(uint32_t adresse, int *couvre) {
    uint32_t bas = 0, haut = g_nb;
    while (bas < haut) {
        uint32_t milieu = (bas + haut) / 2;
        if (g_zones[milieu].offset > adresse) haut = milieu;
        else bas = milieu + 1;
    }
    if (bas && adresse < g_zones[bas - 1].offset + g_zones[bas - 1].taille) { *couvre = 1; return bas - 1; }
    *couvre = 0;
    return bas;                                     /* peut valoir g_nb : plus rien après */
}

/* Lit `taille` octets à `adresse`. Ce qui ne tombe dans aucune zone est le remplissage qui
   sépare les fichiers sur la cartouche : on le rend à zéro et on saute au morceau suivant. */
void data_read(uint32_t adresse, uint8_t *dest, uint32_t taille) {
    memset(dest, 0, taille);
    while (taille) {
        int couvre;
        uint32_t i = trouver(adresse, &couvre);
        if (i >= g_nb) return;                      /* au-delà de la dernière zone */
        if (!couvre) {                              /* trou : on avance jusqu'au morceau suivant */
            uint32_t saut = g_zones[i].offset - adresse;
            if (saut >= taille) return;
            adresse += saut; dest += saut; taille -= saut;
            continue;
        }
        uint32_t dans = adresse - g_zones[i].offset;
        uint32_t reste = g_zones[i].taille - dans;
        uint32_t n = reste < taille ? reste : taille;
        FILE *f = ouvrir(i);
        if (f && !fseek(f, (long)dans, SEEK_SET)) {
            size_t lu = fread(dest, 1, n, f);
            (void)lu;
        }
        adresse += n;
        dest += n;
        taille -= n;
    }
}

/* Le nom du fichier qui commence exactement à cet offset, ou NULL. Le jeu demande ses
   données par adresse ; reconnaître ces adresses, c'est lire par-dessus son épaule la
   liste de ce qu'il charge, avec les noms que ses auteurs leur ont donnés. */
const char *data_nom_a(uint32_t offset) {
    if (!g_pret) return NULL;
    int couvre;
    uint32_t i = trouver(offset, &couvre);
    if (couvre && g_zones[i].offset == offset) return g_noms + g_zones[i].nom;
    return NULL;
}

/* Le début de la cartouche — en-tête, binaires des deux processeurs, tables et overlays —
   est lu d'un coup : ces morceaux sont donnés au démarrage à du code qui attend une image
   contiguë, et ils tiennent dans quelques mégaoctets. Tout le reste se lit à la demande. */
uint8_t *data_preambule(size_t *taille) {
    if (!g_pret || !g_fin_systeme) return NULL;
    uint8_t *buf = malloc(g_fin_systeme);
    if (!buf) return NULL;
    data_read(0, buf, g_fin_systeme);
    *taille = g_fin_systeme;
    return buf;
}

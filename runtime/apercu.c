/*
 * Les tours d'aperçu : la logique du jeu à la cadence de l'affichage.
 *
 * Le jeu pense à pas fixe — trente tours par seconde en vol, soixante ailleurs — et chaque tour avance tout
 * ce qui bouge d'un pas entier : le faire penser plus souvent l'accélérerait d'autant. Entre deux vrais tours,
 * un tour d'aperçu rejoue la logique du jeu depuis l'état présent, avec les commandes du moment, mais n'avance
 * chaque accumulateur (x = x + v : positions, angles, vitesses — marqués par le générateur) que de la part
 * de tour écoulée. L'image qu'il dessine est celle du jeu à cet instant ; puis tout est rendu tel quel — la
 * mémoire, les registres, le moteur 3D —, et le vrai tour, lui, se joue comme sur la console. Les règles du
 * jeu (tirs, apparitions, hasard, IA) restent celles des vrais tours : un aperçu ne décide rien.
 *
 * Sous licence MIT.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void rt_log(const char *fmt, ...);

int g_echelle_active;                  /* 1 : un tour d'aperçu, les accumulateurs sont mis à l'échelle ; 2 : étude */
int32_t g_echelle_q16 = 65536;         /* la part de tour, en 16.16 */
uint64_t g_tour_jeu;                   /* les vrais tours, comptés par la boucle du jeu (natif.c) */

/* ------------------------------------------------------------------ l'étude des sites
 *
 * SFC_ETUDE_ACC=fichier : pendant la partie, chaque écriture accumulatrice est observée. Une intégration —
 * une position, un angle — écrit son adresse une fois par tour ; une construction — un index qui avance dans
 * une liste, une somme qu'on refait à chaque tour — la réécrit plusieurs fois dans le même tour. Mise à
 * l'échelle, la seconde casserait ce que l'aperçu dessine. Le fichier dit, site par site, ce qu'on a vu ;
 * tools/accumulateurs.py en tire la liste des exclus pour le générateur. */
#define ETUDE_CASES (1u << 20)
typedef struct { uint32_t site, ea; uint64_t tour; uint32_t fois; } Vu;
typedef struct { uint32_t site; uint64_t ecritures, tours, doubles; uint64_t dernier_tour; double delta; uint32_t nuls; } Site;
static Vu *g_vus;
static Site *g_sites;
static const char *g_etude;

static Site *site_de(uint32_t pc) {
    uint32_t h = (pc * 0x9E3779B1u) >> 12 & (ETUDE_CASES - 1);
    for (;;) {
        Site *s = &g_sites[h];
        if (s->site == pc) return s;
        if (!s->site) { s->site = pc; return s; }
        h = (h + 1) & (ETUDE_CASES - 1);
    }
}

static void etude_ecrire(void) {
    FILE *f = fopen(g_etude, "w");
    if (!f) return;
    fprintf(f, "# site écritures tours doubles delta_moyen nuls — %llu tours\n", (unsigned long long)g_tour_jeu);
    size_t n = 0;
    for (uint32_t k = 0; k < ETUDE_CASES; k++) {
        const Site *s = &g_sites[k];
        if (!s->site) continue;
        fprintf(f, "%08X %llu %llu %llu %.1f %u\n", s->site, (unsigned long long)s->ecritures, (unsigned long long)s->tours,
                (unsigned long long)s->doubles, s->ecritures ? s->delta / (double)s->ecritures : 0.0, s->nuls);
        n++;
    }
    fclose(f);
    rt_log("étude des accumulateurs : %zu sites vus sur %llu tours, dans %s", n, (unsigned long long)g_tour_jeu, g_etude);
}

static void etude_noter(uint32_t vieux, uint32_t neuf, unsigned bits, uint32_t ea, uint32_t site) {
    Site *s = site_de(site);
    s->ecritures++;
    if (s->dernier_tour != g_tour_jeu + 1) { s->tours++; s->dernier_tour = g_tour_jeu + 1; }
    int64_t d = bits == 32 ? (int32_t)(neuf - vieux) : bits == 16 ? (int16_t)(uint16_t)(neuf - vieux) : (int8_t)(uint8_t)(neuf - vieux);
    s->delta += (double)(d < 0 ? -d : d);
    if (!d) s->nuls++;
    /* la même adresse, par le même site, dans le même tour ? */
    uint32_t h = ((site * 0x9E3779B1u) ^ (ea * 0x85EBCA77u)) >> 12 & (ETUDE_CASES - 1);
    for (int i = 0; i < 32; i++) {
        Vu *v = &g_vus[(h + (uint32_t)i) & (ETUDE_CASES - 1)];
        if (v->site == site && v->ea == ea) {
            if (v->tour == g_tour_jeu) { if (++v->fois == 2) s->doubles++; }
            else { v->tour = g_tour_jeu; v->fois = 1; }
            return;
        }
        if (!v->site) { v->site = site; v->ea = ea; v->tour = g_tour_jeu; v->fois = 1; return; }
    }
}

void apercu_init(void) {
    extern const char *apercu_soupcons_init(void);
    apercu_soupcons_init();
    g_etude = getenv("SFC_ETUDE_ACC");
    if (g_etude) {
        g_vus = calloc(ETUDE_CASES, sizeof *g_vus);
        g_sites = calloc(ETUDE_CASES, sizeof *g_sites);
        g_echelle_active = 2;
        atexit(etude_ecrire);
        rt_log("étude des accumulateurs : chaque écriture accumulatrice est observée");
    }
}

/* ------------------------------------------------------------------ les sites soupçonnés
 *
 * SFC_APERCU_SOUPCONS=fichier : un aperçu abandonné a suivi un état que la mise à l'échelle a rendu
 * impossible. Chaque aperçu retient les sites qu'il a mis à l'échelle ; à l'arrivée, on compare ceux des
 * aperçus qui ont abouti à ceux des aperçus abandonnés. Un site qui n'apparaît que dans les seconds est
 * celui qu'il ne fallait pas toucher. */
#define SOUPCON_CASES (1u << 16)
static struct { uint32_t site; uint64_t vus, rates; } g_soupcons[SOUPCON_CASES];
static uint32_t g_vus_tour[4096];
static unsigned g_vus_n;
static const char *g_fichier_soupcons;

static void soupcon_noter_site(uint32_t site) {
    for (unsigned i = 0; i < g_vus_n; i++) if (g_vus_tour[i] == site) return;
    if (g_vus_n < 4096) g_vus_tour[g_vus_n++] = site;
}

void apercu_soupcons_debut(void) { g_vus_n = 0; }

void apercu_soupcons_fin(int reussi) {
    if (!g_fichier_soupcons) return;
    for (unsigned i = 0; i < g_vus_n; i++) {
        uint32_t h = (g_vus_tour[i] * 0x9E3779B1u) >> 16 & (SOUPCON_CASES - 1);
        for (unsigned k = 0; k < 64; k++) {
            uint32_t j = (h + k) & (SOUPCON_CASES - 1);
            if (!g_soupcons[j].site) g_soupcons[j].site = g_vus_tour[i];
            if (g_soupcons[j].site == g_vus_tour[i]) { g_soupcons[j].vus++; g_soupcons[j].rates += !reussi; break; }
        }
    }
}

const char *apercu_soupcons_init(void);

static void soupcons_ecrire(void) {
    FILE *f = fopen(g_fichier_soupcons, "w");
    if (!f) return;
    fprintf(f, "# site vus ratés\n");
    for (uint32_t k = 0; k < SOUPCON_CASES; k++)
        if (g_soupcons[k].site)
            fprintf(f, "%08X %llu %llu\n", g_soupcons[k].site, (unsigned long long)g_soupcons[k].vus,
                    (unsigned long long)g_soupcons[k].rates);
    fclose(f);
}

const char *apercu_soupcons_init(void) {
    g_fichier_soupcons = getenv("SFC_APERCU_SOUPCONS");
    if (g_fichier_soupcons) atexit(soupcons_ecrire);
    return g_fichier_soupcons;
}

/* ------------------------------------------------------------------ la mise à l'échelle */
/* les plages que la console adresse : RAM et ITCM, DTCM, WRAM et celle du coprocesseur, palette, VRAM, OAM,
   cartouche, registres */
static int adresse_plausible(uint32_t v) {
    return (v - 0x01FF8000u < 0x00408000u) || (v - 0x027E0000u < 0x00004000u) || (v - 0x03000000u < 0x00810000u)
        || (v - 0x04000000u < 0x00110000u) || (v - 0x05000000u < 0x00000800u) || (v - 0x06000000u < 0x01000000u)
        || (v - 0x07000000u < 0x00000800u) || (v - 0x08000000u < 0x02000000u);
}


uint32_t echelle_acc(uint32_t vieux, uint32_t neuf, unsigned bits, uint32_t ea, uint32_t site) {
    if (g_echelle_active == 2) { etude_noter(vieux, neuf, bits, ea, site); return neuf; }
    int64_t d;
    if (bits == 32) {
        d = (int32_t)(neuf - vieux);
        /* Une adresse ne se partage pas : à mi-chemin d'un objet à l'autre il n'y a rien, et le jeu suivrait
           ce pointeur-là. Une position en virgule fixe peut tomber dans ces plages, et elle y perdra sa
           fluidité : mieux vaut cela qu'un saut dans le vide. */
        if (adresse_plausible(vieux) && adresse_plausible(neuf)) return neuf;
    } else if (bits == 16) {
        d = (int16_t)(uint16_t)(neuf - vieux);
    } else {
        d = (int8_t)(uint8_t)(neuf - vieux);
    }
    /* Un petit pas est celui d'un compteur ou d'un index — une unité, un élément d'une liste —, pas celui d'une
       position (des milliers en 20.12), d'un angle ou d'une minuterie (136 par tour) : on ne partage pas un
       entier, et un index à demi avancé fait lire n'importe quoi. */
    if (d >= -16 && d <= 16) return neuf;
    if (g_fichier_soupcons) soupcon_noter_site(site);
    int64_t e = d * (int64_t)g_echelle_q16;
    e = (e + (e >= 0 ? 32768 : -32768)) / 65536;          /* au plus proche */
    return vieux + (uint32_t)e;
}

/* ------------------------------------------------------------------ ce qui bouge d'un tour à l'autre
 *
 * SFC_ETUDE_MOUVEMENT=image : à partir de cette image, la RAM est comparée d'un tour à l'autre, et l'on
 * retient les mots qui avancent d'un pas régulier — une position, une vitesse, un angle. Ce sont eux que les
 * tours d'aperçu devraient n'avancer que d'une fraction ; savoir où ils sont dit quelles écritures le
 * générateur devrait marquer. */
extern uint8_t *g_main_ram;
uint64_t hw_frames(void);

static uint32_t *g_ram_avant, *g_ram_deltas;
static long long g_etude_mouvement = -2;

void apercu_etude_mouvement(void) {
    if (g_etude_mouvement == -2) {
        const char *e = getenv("SFC_ETUDE_MOUVEMENT");
        g_etude_mouvement = e ? atoll(e) : -1;
    }
    if (g_etude_mouvement < 0 || (long long)hw_frames() < g_etude_mouvement) return;
    const uint32_t mots = 0x400000u / 4u;
    uint32_t *ram = (uint32_t *)g_main_ram;
    if (!g_ram_avant) {
        g_ram_avant = malloc(mots * 4);
        g_ram_deltas = malloc(mots * 4);
        memcpy(g_ram_avant, ram, mots * 4);
        memset(g_ram_deltas, 0, mots * 4);
        return;
    }
    static int tours;
    static struct { uint32_t adresse; int32_t d; } tenus[40];
    static int n_tenus;
    tours++;
    for (uint32_t i = 0; i < mots; i++) {
        int32_t d = (int32_t)(ram[i] - g_ram_avant[i]);
        int32_t dd = (int32_t)g_ram_deltas[i];
        /* un pas régulier : le même, à un quart près, que celui du tour précédent */
        if (d && dd && (d > 0) == (dd > 0)) {
            int64_t diff = (int64_t)d - dd, amp = d > 0 ? d : -d;
            if (amp > 100 && (diff < amp / 4 && diff > -amp / 4) && n_tenus < 40) {
                int vu = 0;
                for (int k = 0; k < n_tenus; k++) if (tenus[k].adresse == 0x02000000u + i * 4u) vu = 1;
                if (!vu) { tenus[n_tenus].adresse = 0x02000000u + i * 4u; tenus[n_tenus].d = d; n_tenus++; }
            }
        }
        g_ram_deltas[i] = (uint32_t)d;
        g_ram_avant[i] = ram[i];
    }
    if (tours == 6) {
        rt_log("ce qui avance d'un pas régulier, tour après tour (%d mots) :", n_tenus);
        for (int k = 0; k < n_tenus; k++)
            rt_log("   0x%08X : pas %d (%.2f unités en 20.12)", tenus[k].adresse, tenus[k].d, tenus[k].d / 4096.0);
    }
}

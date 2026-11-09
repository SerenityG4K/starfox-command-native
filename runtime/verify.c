/*
 * Mode de vérification : empreinte de l'état par image, enregistrement et
 * rejeu des entrées.
 *
 *   SFC_HASH=chemin          à chaque VBlank, écrit « image hachage » (RAM
 *                            principale, DTCM, registres matériels)
 *   SFC_INPUT_RECORD=chemin  écrit les entrées (boutons, stylet) par image
 *   SFC_INPUT_REPLAY=chemin  les relit à la place de la fenêtre
 *
 * Deux exécutions (référence traduite, version avec remplacements natifs)
 * rejouant les mêmes entrées doivent produire les mêmes empreintes ; la
 * première image qui diverge désigne le remplacement fautif.
 *
 * Sous licence MIT.
 */
#include "rt.h"
#include "hw.h"
#include "memory.h"
#include "verify.h"
#include "manette.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FILE *g_hash, *g_record, *g_replay;
static int g_init;
static uint32_t g_replay_keys, g_replay_touch;
static int g_replay_done;

static uint64_t fnv64(const uint8_t *p, size_t n, uint64_t h) {
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 0x100000001B3ull; }
    return h;
}

static void init(void) {
    if (g_init) return;
    g_init = 1;
    const char *hash = getenv("SFC_HASH"), *rec = getenv("SFC_INPUT_RECORD"), *rep = getenv("SFC_INPUT_REPLAY");
    if (hash) g_hash = fopen(hash, "w");
    if (rec) g_record = fopen(rec, "w");
    if (rep) g_replay = fopen(rep, "r");
    if (hash) rt_log("vérification : empreintes dans %s", hash);
    if (rep) rt_log("vérification : rejeu des entrées depuis %s", rep);
}

/* SFC_SNAPSHOT=image:préfixe — écrit la mémoire telle qu'elle est à cette image.
 *
 * Deux empreintes qui divergent disent qu'il s'est passé quelque chose, jamais quoi. Deux
 * instantanés pris au même instant dans deux exécutions se comparent octet par octet, et
 * l'adresse du premier écart désigne la structure en cause. */
static void snapshot(uint64_t frame) {
    static int lu;
    static uint64_t quand;
    static const char *prefixe;
    if (!lu) {
        lu = 1;
        const char *v = getenv("SFC_SNAPSHOT");
        if (v) {
            quand = strtoull(v, NULL, 0);
            const char *deuxpoints = strchr(v, ':');
            prefixe = deuxpoints ? deuxpoints + 1 : "snapshot";
        }
    }
    if (!prefixe || frame != quand) return;
    static const struct { const char *suffixe; uint8_t **zone; size_t taille; } zones[] = {
        {"ram", &g_main_ram, 0x400000}, {"dtcm", &g_dtcm, 0x4000},
        {"itcm", &g_itcm, 0x8000},
    };
    for (size_t i = 0; i < sizeof zones / sizeof *zones; i++) {
        char chemin[512];
        snprintf(chemin, sizeof chemin, "%s.%s.bin", prefixe, zones[i].suffixe);
        FILE *f = fopen(chemin, "wb");
        if (!f) continue;
        fwrite(*zones[i].zone, 1, zones[i].taille, f);
        fclose(f);
    }
    char chemin[512];
    snprintf(chemin, sizeof chemin, "%s.io.bin", prefixe);
    FILE *f = fopen(chemin, "wb");
    if (f) { fwrite(g_io, 1, 0x2000, f); fclose(f); }
    rt_log("instantané : mémoire écrite dans %s.*.bin à l'image %llu",
           prefixe, (unsigned long long)frame);
}

void verify_frame(uint64_t frame) {
    init();
    snapshot(frame);
    if (!g_hash) return;
    uint64_t h = 0xCBF29CE484222325ull;
    h = fnv64(g_main_ram, 0x400000, h);
    h = fnv64(g_dtcm, 0x4000, h);
    h = fnv64(g_io, 0x2000, h);
    fprintf(g_hash, "%llu %016llx\n", (unsigned long long)frame, (unsigned long long)h);
    if (frame % 60 == 0) fflush(g_hash);
}

/* Enregistre ou remplace les entrées de l'image ; renvoie 1 si un rejeu a fourni les valeurs. Une ligne par
   image : « image touches stylet x y », et quand une manette est branchée, cinq champs de plus — « gx gy dx
   dy boutons », les sticks écrits au bit près pour que le rejeu refasse exactement la même partie. */
int verify_input(uint64_t frame, uint32_t *held, int *touch_down, int *touch_x, int *touch_y, Manette *m) {
    init();
    if (g_replay && !g_replay_done) {
        char ligne[256];
        unsigned long long f; unsigned k; int d, x, y, n = 0;
        if (fgets(ligne, sizeof ligne, g_replay) && sscanf(ligne, "%llu %x %d %d %d%n", &f, &k, &d, &x, &y, &n) == 5) {
            *held = k; *touch_down = d; *touch_x = x; *touch_y = y;
            float gx, gy, dx, dy;
            unsigned b;
            memset(m, 0, sizeof *m);
            if (sscanf(ligne + n, "%f %f %f %f %x", &gx, &gy, &dx, &dy, &b) == 5) *m = (Manette){1, b, gx, gy, dx, dy};
            return 1;
        }
        g_replay_done = 1;
        rt_log("vérification : fin du rejeu à l'image %llu", (unsigned long long)frame);
    }
    if (g_record) {
        fprintf(g_record, "%llu %x %d %d %d", (unsigned long long)frame, *held, *touch_down, *touch_x, *touch_y);
        if (m->branchee) fprintf(g_record, " %.9g %.9g %.9g %.9g %x", m->gx, m->gy, m->dx, m->dy, m->boutons);
        fputc('\n', g_record);
    }
    return 0;
}

/* Sans fenêtre (SFC_NO_VIDEO), personne ne lit les entrées : un rejeu n'aurait alors aucun
   effet et le jeu resterait sur son écran-titre. On les applique donc ici, une fois par image,
   pour que les explorations puissent tourner sans rien afficher. */
void verify_input_headless(uint64_t frame) {
    init();
    uint32_t held = 0;
    int tdown = 0, tx = 0, ty = 0;
    Manette manette;
    memset(&manette, 0, sizeof manette);
    /* le rejeu ; le script d'essai, s'il y en a un, remplace la manette — et joue seul quand le rejeu est fini */
    const int rejeu = g_replay && !g_replay_done && verify_input(frame, &held, &tdown, &tx, &ty, &manette);
    const int essai = manette_script(&manette);
    if (!rejeu && !essai) return;
    manette_appliquer(&manette, &held, tdown);
    {   /* SFC_TRACE_INPUT=1 : les changements d'état, comme le fait la fenêtre */
        static int trace = -1;
        if (trace < 0) trace = getenv("SFC_TRACE_INPUT") != NULL;
        static uint32_t last_keys = 0; static int last_down = 0;
        if (trace && (held != last_keys || tdown != last_down)) {
            rt_log("entrée (sans fenêtre) : image %llu touches=%03X stylet=%d (%d,%d)",
                   (unsigned long long)frame, held, tdown, tx, ty);
            last_keys = held; last_down = tdown;
        }
    }
    hw_set_keys(held);
    hw_set_touch(tdown, tx, ty);
    hw_set_pen(tdown);
}

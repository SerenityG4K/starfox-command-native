/*
 * Moteur 3D : registres 0x04000320–0x040006FF et FIFO de commandes.
 *
 * Première version : la FIFO est consommée instantanément (jamais pleine), les
 * résultats des matrices sont calculés pour que les lectures du jeu soient
 * cohérentes ; le rendu viendra ensuite (mêmes commandes que l'interpréteur
 * de listes d'affichage du moteur).
 *
 * Sous licence MIT.
 */
#include "rt.h"
#include "hw.h"
#include "gx_geom.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static uint32_t g_gx_regs[0x100];        /* 0x320..0x6FF par mots */
static uint32_t g_gxstat = 0x04000000;   /* fifo vide, moins qu'à moitié */

/* Compteur de paramètres par commande de la FIFO */
static const uint8_t param_count[0x80] = {
    [0x10] = 1, [0x11] = 0, [0x12] = 1, [0x13] = 1, [0x14] = 1, [0x15] = 0, [0x16] = 16, [0x17] = 12,
    [0x18] = 16, [0x19] = 12, [0x1A] = 9, [0x1B] = 3, [0x1C] = 3, [0x20] = 1, [0x21] = 1, [0x22] = 1,
    [0x23] = 2, [0x24] = 1, [0x25] = 1, [0x26] = 1, [0x27] = 1, [0x28] = 1, [0x29] = 1, [0x2A] = 1,
    [0x2B] = 1, [0x30] = 1, [0x31] = 1, [0x32] = 1, [0x33] = 1, [0x34] = 32, [0x40] = 1, [0x41] = 0,
    [0x50] = 1, [0x60] = 1, [0x70] = 3, [0x71] = 2, [0x72] = 1,
};

static uint32_t g_packed, g_packed_left, g_params_left, g_current_cmd;
static uint32_t g_params[32];

void gx_command(Cpu *c, uint32_t cmd, const uint32_t *params, uint32_t n);

static void feed(Cpu *c, uint32_t word) {
    if (g_params_left) {
        g_params[(param_count[g_current_cmd] - g_params_left)] = word;
        g_params_left--;
        if (g_params_left == 0) {
            gx_command(c, g_current_cmd, g_params, param_count[g_current_cmd]);
            goto next_packed;
        }
        return;
    }
    if (g_packed_left == 0) {
        g_packed = word;
        g_packed_left = 4;
    }
next_packed:
    while (g_packed_left) {
        uint32_t cmd = g_packed & 0xFF;
        g_packed >>= 8;
        g_packed_left--;
        if (cmd == 0) continue;
        if (cmd >= 0x80) { if (!g_temps_fige) rt_log("GX : commande invalide %02X", cmd); continue; }
        g_current_cmd = cmd;
        g_params_left = param_count[cmd];
        if (g_params_left == 0) gx_command(c, cmd, NULL, 0);
        else return;
    }
}

uint32_t gx_read(Cpu *c, uint32_t off, uint32_t size) {
    (void)c; (void)size;
    if (off == 0x600) return (gx_geom_read(0x600) & ~0xC0000000u) | (g_gxstat & 0xC0000000u);
    if (off == 0x604) {
        /* RAM_COUNT : polygones et sommets du tampon en construction, remis à zéro par SWAP_BUFFERS.
           Le jeu n'échange ses écrans au VBlank que si le compte de sommets est nul (image vide). */
        const GxFrame *f = gx_geom_building();
        return (uint32_t)(f->count & 0xFFF) | ((f->vertex_count & 0x1FFF) << 16);
    }
    if (off >= 0x620 && off < 0x700) return gx_geom_read(off);
    return gx_geom_read(off);
}

void gx_write(Cpu *c, uint32_t off, uint32_t v, uint32_t size) {
    (void)size;
    if (off == 0x600) {
        if (v & 0x8000) g_gxstat &= ~0x8000u;      /* acquittement d'erreur */
        g_gxstat = (g_gxstat & ~0xC0000000u) | (v & 0xC0000000u);
        return;
    }
    if (off == 0x400) { feed(c, v); return; }
    if (off >= 0x440 && off < 0x600) {
        /* écriture directe d'une commande par son registre */
        uint32_t cmd = (off - 0x400) / 4;
        if (g_current_cmd != cmd || g_params_left == 0) {
            g_current_cmd = cmd;
            g_params_left = param_count[cmd];
            g_packed_left = 0;
        }
        if (g_params_left == 0) { gx_command(c, cmd, NULL, 0); return; }
        g_params[param_count[cmd] - g_params_left] = v;
        g_params_left--;
        if (g_params_left == 0) gx_command(c, cmd, g_params, param_count[cmd]);
        return;
    }
    g_gx_regs[(off - 0x320) / 4] = v;
    gx_geom_set_register(off, v);
}

/* ------------------------------------------------------------------ commandes */
static uint64_t g_gx_count, g_swaps;
static uint64_t g_cmd_count[0x80];

/* SFC_TRACE_GX=1 : à la sortie, combien de fois chaque commande a été exécutée. Sert surtout à
   savoir quels formats de sommets le jeu emploie réellement. */
static void gx_counts_report(void) {
    for (uint32_t i = 0; i < 0x80; i++)
        if (g_cmd_count[i]) rt_log("GX : commande %02X exécutée %llu fois", i, (unsigned long long)g_cmd_count[i]);
}

void gx_command(Cpu *c, uint32_t cmd, const uint32_t *params, uint32_t n) {
    (void)params; (void)n;
    g_gx_count++;
    /* SFC_TRACE_CELL=1 : qui place les cellules du terrain. Une translation d'un multiple exact
       de 2 560 unités est une cellule ; on note d'où vient la commande pour remonter à la boucle
       qui décide combien de cellules sont dessinées. */
    {
        static int on = -1;
        if (on < 0) on = getenv("SFC_TRACE_CELL") != NULL;
        if (on && cmd == 0x1C && params && n >= 3) {
            const int32_t cell = 2560 * 4096;
            int32_t x = (int32_t)params[0], z = (int32_t)params[2];
            if ((x || z) && x % cell == 0 && z % cell == 0) {
                static uint32_t seen[16];
                static int ns;
                uint32_t lr = c->r[14];
                int found = 0;
                for (int i = 0; i < ns; i++) if (seen[i] == lr) { found = 1; break; }
                if (!found && ns < 16) {
                    seen[ns++] = lr;
                    char pile[160] = {0};
                    for (uint32_t k = c->depth; k > 0 && k + 8 > c->depth; k--)
                        snprintf(pile + strlen(pile), sizeof pile - strlen(pile), " %08X", c->shadow[k - 1]);
                    rt_log("cellule : translation (%d, %d) posée depuis lr=0x%08X ; retours :%s",
                           x / cell, z / cell, lr, pile);
                }
            }
        }
    }
    if (cmd < 0x80) {
        /* La variable n'est lue qu'une fois. Relue à chaque commande tant qu'elle était absente — des
           dizaines de milliers de fois par tour —, cette seule lecture prenait les deux tiers du temps
           de la machine hôte en vol. */
        static int registered = -1;
        if (registered < 0) { registered = getenv("SFC_TRACE_GX") != NULL; if (registered) atexit(gx_counts_report); }
        g_cmd_count[cmd]++;
    }
    if (g_gx_count <= 8) rt_log("GX : commande %02X (%u paramètres)", cmd, n);
    if (cmd == 0x50) {   /* SWAP_BUFFERS : l'image est complète */
        {   static int diag = -1; static uint64_t n;
            if (diag < 0) diag = getenv("SFC_DIAG_BRIDE") != NULL;
            if (diag && ++n % 30 == 0) rt_log("échange à l'image %llu : %zu polygones", (unsigned long long)hw_frames(), gx_geom_building()->count); }
        /* SFC_DIAG_SWAP=image : la pile d'appels du jeu aux premiers échanges à partir de cette image */
        static long long diag = -2;
        if (diag == -2) { const char *e = getenv("SFC_DIAG_SWAP"); diag = e ? atoll(e) : -1; }
        if (diag >= 0 && (long long)hw_frames() >= diag) {
            static int n;
            if (n++ < 4) {
                char pile[400] = {0};
                for (uint32_t k = c->depth; k > 0 && k + 24 > c->depth; k--)
                    snprintf(pile + strlen(pile), sizeof pile - strlen(pile), " %08X", c->shadow[k - 1]);
                rt_log("échange 3D à l'image %llu : lr=0x%08X pc=0x%08X mode=%02X in_irq=%u ; retours :%s",
                       (unsigned long long)hw_frames(), c->r[14], c->r[15], c->mode, c->in_irq, pile);
                /* et la pile de la machine hôte : les fonctions réécrites y portent l'adresse d'origine */
                void *hote[62];
                unsigned short nh = RtlCaptureStackBackTrace(0, 62, hote, NULL);
                char l[1200] = {0};
                uintptr_t base = (uintptr_t)GetModuleHandleW(NULL);
                for (unsigned i = 0; i < nh; i++)
                    snprintf(l + strlen(l), sizeof l - strlen(l), " %llx", (unsigned long long)((uintptr_t)hote[i] - base));
                rt_log("   pile hôte (décalages dans l'exécutable) :%s", l);
            }
        }
        const GxFrame *f = gx_geom_swap(params ? params[0] : 0);
        { extern void video_publier_echange(const GxFrame *f); video_publier_echange(f); }
        g_swaps++;
        if (g_swaps <= 3 || g_swaps % 300 == 0) {
            rt_log("GX : swap %llu après %llu commandes, %zu polygones", (unsigned long long)g_swaps,
                   (unsigned long long)g_gx_count, f->count);
            if (f->count) {
                const GxPolygon *p = &f->polys[0];
                rt_log("GX : viewport=%08X clair=%08X disp3dcnt=%04X ; polygone 0 : %u sommets attr=%08X tex=%08X"
                       " v0=(%d,%d,%d,%d) rgb=%u,%u,%u v1=(%d,%d,%d,%d) v2=(%d,%d,%d,%d)",
                       f->viewport, f->clear_color, f->disp3dcnt & 0xFFFF, p->count, p->attr, p->texparam,
                       p->v[0].x, p->v[0].y, p->v[0].z, p->v[0].w, p->v[0].r, p->v[0].g, p->v[0].b,
                       p->v[1].x, p->v[1].y, p->v[1].z, p->v[1].w, p->v[2].x, p->v[2].y, p->v[2].z, p->v[2].w);
            }
        }
        return;   /* affichée au prochain VBlank (gx_vblank_present) */
    }
    gx_geom_command(cmd, params, n);
}

/* Au VBlank, après la composition de la trame écoulée : l'échange demandé pendant la trame
   prend effet, l'image devient celle des trames suivantes. */
void gx_vblank_present(void) { gx_present(gx_geom_present()); }

/* SFC_DUMP_POLYS=n : au VBlank de l'image n, chaque polygone de l'image 3D affichée (attributs,
   texture, palette, sommets en 20.12 et coordonnées de texture en texels). */
void gx_dump_polys(uint64_t frame) {
    {   /* SFC_DIAG_LOIN=image[:distance] : où tombent à l'écran les polygones plus lointains que la distance
           (7 800 par défaut, un peu au-delà de ce que le jeu dessine d'origine) */
        static int64_t loin = -1; static double seuil = 7800; static size_t detail = 6;
        if (loin < 0) { const char *e = getenv("SFC_DIAG_LOIN"); loin = e ? (int64_t)strtoull(e, NULL, 10) : 0;
                        const char *d = e ? strchr(e, ':') : NULL; if (d) seuil = atof(d + 1);
                        const char *n = getenv("SFC_DIAG_LOIN_N"); if (n) detail = (size_t)atoi(n); }
        if (loin && (int64_t)frame == loin) {
            const GxFrame *f = gx_geom_current();
            size_t n = 0, ecran = 0, transl = 0;
            double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9, zmin = 1e9, zmax = -1e9;
            for (size_t i = 0; i < f->count; i++) {
                const GxPolygon *p = &f->polys[i];
                int32_t m11 = p->proj < f->n_projs ? f->projs[p->proj][11] : -4096;
                double wmax = 0, wmin = 1e18;
                for (int k = 0; k < p->count; k++) {
                    double w = (double)p->v[k].w / 4096.0;
                    if (m11 < 0) w = w * -4096.0 / m11;
                    if (w > wmax) wmax = w;
                    if (w < wmin) wmin = w;
                }
                if (wmax < seuil) continue;
                n++;
                transl += p->translucent;
                double px0 = 1e9, py0 = 1e9, px1 = -1e9, py1 = -1e9;
                for (int k = 0; k < p->count; k++) {
                    double w = p->v[k].w ? p->v[k].w : 1;
                    double sx = ((double)p->v[k].x / w + 1) * 128, sy = (1 - (double)p->v[k].y / w) * 96;
                    double sz = (double)p->v[k].z / w;
                    if (sx < px0) px0 = sx; if (sx > px1) px1 = sx;
                    if (sy < py0) py0 = sy; if (sy > py1) py1 = sy;
                    if (sz < zmin) zmin = sz; if (sz > zmax) zmax = sz;
                }
                if (px1 >= 0 && px0 < 256 && py1 >= 0 && py0 < 192) {
                    ecran++;
                    if (px0 < x0) x0 = px0; if (px1 > x1) x1 = px1;
                    if (py0 < y0) y0 = py0; if (py1 > y1) y1 = py1;
                }
                if (n <= detail)
                    rt_log("loin : polygone %zu à %.0f unités (le plus proche %.0f), écran x %.1f–%.1f y %.1f–%.1f, attr=%08X tex=%08X translucide=%u",
                           i, wmax, wmin, px0, px1, py0, py1, p->attr, p->texparam, p->translucent);
            }
            rt_log("loin : image %llu, %zu polygones sur %zu au-delà de %.0f unités ; %zu dans l'écran (x %.1f–%.1f, y %.1f–%.1f), "
                   "%zu translucides ; profondeur z/w de %.4f à %.4f",
                   (unsigned long long)frame, n, f->count, seuil, ecran, x0, x1, y0, y1, transl, zmin, zmax);
        }
    }
    static int64_t want = -1;
    if (want < 0) { const char *e = getenv("SFC_DUMP_POLYS"); want = e ? (int64_t)strtoull(e, NULL, 10) : 0; }
    if (!want || (int64_t)frame != want) return;
    const GxFrame *f = gx_geom_current();
    rt_log("image %llu : %zu polygones affichés (clair=%08X disp3dcnt=%04X viewport=%08X)",
           (unsigned long long)frame, f->count, f->clear_color, f->disp3dcnt & 0xFFFF, f->viewport);
    for (size_t i = 0; i < f->count && i < 64; i++) {
        const GxPolygon *p = &f->polys[i];
        rt_log("  polygone %zu : %u sommets attr=%08X tex=%08X (format %u, %ux%u, adresse 0x%05X) pltt=%04X translucide=%u",
               i, p->count, p->attr, p->texparam, (p->texparam >> 26) & 7, 8 << ((p->texparam >> 20) & 7),
               8 << ((p->texparam >> 23) & 7), (p->texparam & 0xFFFF) << 3, p->pltt_base, p->translucent);
        for (int k = 0; k < p->count; k++)
            rt_log("    v%d=(%d,%d,%d,%d) rgb=%u,%u,%u st=(%d,%d)", k, p->v[k].x, p->v[k].y, p->v[k].z, p->v[k].w,
                   p->v[k].r, p->v[k].g, p->v[k].b, p->v[k].s >> 4, p->v[k].t >> 4);
    }
}



void gx_vblank(Cpu *c) {
    (void)c;
    if (g_gxstat & 0x40000000) hw_raise_irq(c, IRQ_GXFIFO);   /* fifo vide : IRQ si demandée */
}

/* ------------------------------------------------------------------ tours d'aperçu (apercu.c) */
static uint32_t g_gx_regs_s[0x100], g_gxstat_s, g_packed_s, g_packed_left_s, g_params_left_s, g_current_cmd_s, g_params_s[32];
static uint64_t g_gx_count_s, g_swaps_s;
void gx_apercu_sauver(void) {
    memcpy(g_gx_regs_s, g_gx_regs, sizeof g_gx_regs); g_gxstat_s = g_gxstat;
    g_packed_s = g_packed; g_packed_left_s = g_packed_left; g_params_left_s = g_params_left;
    g_current_cmd_s = g_current_cmd; memcpy(g_params_s, g_params, sizeof g_params);
    g_gx_count_s = g_gx_count; g_swaps_s = g_swaps;
}
void gx_apercu_rendre(void) {
    memcpy(g_gx_regs, g_gx_regs_s, sizeof g_gx_regs); g_gxstat = g_gxstat_s;
    g_packed = g_packed_s; g_packed_left = g_packed_left_s; g_params_left = g_params_left_s;
    g_current_cmd = g_current_cmd_s; memcpy(g_params, g_params_s, sizeof g_params);
    g_gx_count = g_gx_count_s; g_swaps = g_swaps_s;
}

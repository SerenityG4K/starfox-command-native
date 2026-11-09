/*
 * Moteur géométrique 3D de la DS : état, matrices, éclairage, assemblage des
 * polygones. Reçoit les commandes de la FIFO (gx.c) et produit, à chaque
 * SWAP_BUFFERS, la liste des polygones de l'image en espace de clipping.
 *
 * Sous licence MIT.
 */
#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GxVertex {
    int32_t x, y, z, w;        /* coordonnées de clipping, 20.12 */
    uint8_t r, g, b;           /* 0..31 */
    int16_t s, t;              /* texcoords 12.4 (texels) */
    uint32_t h;                /* empreinte du sommet tel que le jeu l'a donné, avant toute matrice */
} GxVertex;

typedef struct GxPolygon {
    GxVertex v[4];
    uint8_t count;             /* 3 ou 4 */
    uint32_t attr;             /* POLYGON_ATTR */
    uint32_t texparam;         /* TEXIMAGE_PARAM */
    uint32_t pltt_base;        /* PLTT_BASE (adresse palette / 16 ou / 8) */
    uint8_t translucent;       /* alpha < 31 ou texture translucide */
    /* Son identité d'une image à l'autre : l'empreinte de ses sommets dans le repère de l'objet et
       de sa texture. Un objet rigide, ou animé par ses os, soumet les mêmes sommets à chaque
       image ; seules ses matrices changent. C'est ce qui permet à l'affichage d'apparier deux
       images du jeu et d'interpoler entre elles (docs/plan_framerate.md). */
    uint32_t id;
    uint32_t forme;            /* la même empreinte sans la texture : une explosion, un chiffre de compteur changent de
                                  vignette d'un tour à l'autre sans cesser d'être le même carré au même endroit */
    int32_t ox, oy, oz;        /* l'origine de son objet dans le repère de la caméra : la translation de la
                                  matrice de position à sa naissance. Deux ennemis du même modèle, les deux
                                  ailes d'un même vaisseau, ont les mêmes polygones — pas la même origine. */
    uint8_t ancienne;          /* (affichage) il vient de l'image précédente, ses textures aussi */
    uint8_t proj;              /* le rang de sa projection dans GxFrame.projs (GX_PROJ_INCONNUE : table pleine) */
} GxPolygon;

#define GX_PROJS_MAX 8
#define GX_PROJ_INCONNUE 255

typedef struct GxFrame {
    GxPolygon *polys;
    size_t count, capacity;
    uint32_t swap_flags;       /* SWAP_BUFFERS : tri translucide auto, tampon W */
    uint32_t viewport;         /* VIEWPORT */
    uint32_t clear_color, clear_depth, fog_color, fog_offset, disp3dcnt;
    uint32_t edge_color[8], toon_table[32], fog_table[32];
    uint32_t vertex_count;
    uint32_t serial;           /* numéro de l'image 3D : il change à chaque SWAP_BUFFERS */
    /* Les projections employées dans l'image, dans l'ordre où le jeu s'en est servi : la scène en
       perspective, l'interface à plat. L'affichage en a besoin pour revenir du repère de découpage à
       celui de la caméra, où le mouvement d'une image à l'autre est rigide (runtime/entre_images.cpp). */
    int32_t projs[GX_PROJS_MAX][16];
    uint8_t n_projs;
} GxFrame;

void gx_geom_init(void);
void gx_geom_command(uint32_t cmd, const uint32_t *params, uint32_t n);
/* image terminée (SWAP_BUFFERS) : les polygones accumulés, à consommer avant la suivante */
const GxFrame *gx_geom_swap(uint32_t swap_params);
void gx_geom_clip_stats(uint64_t frame);
void gx_geom_cell_stats(uint64_t frame);
const GxFrame *gx_geom_current(void);
const GxFrame *gx_geom_present(void);   /* au VBlank : applique l'échange en attente, renvoie l'image affichée */
/* image en construction (RAM_COUNT : ses comptes, remis à zéro par SWAP_BUFFERS) */
const GxFrame *gx_geom_building(void);
/* l'image en construction avec l'en-tête et le numéro qu'elle aura à l'échange (ses polygones restent ceux
   du tampon : à copier avant la suite) */
GxFrame gx_geom_apercu(void);
/* registres 3D hors FIFO (0x04000320-0x040003FF) */
void gx_geom_set_register(uint32_t off, uint32_t v);
/* résultats de tests et lectures de matrices */
uint32_t gx_geom_read(uint32_t off);

#ifdef __cplusplus
}
#endif

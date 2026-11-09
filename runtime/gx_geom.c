/*
 * Moteur géométrique 3D de la DS, d'après le comportement documenté du
 * matériel (GBATEK). Tout est en virgule fixe : matrices et vecteurs 20.12,
 * produits sur 64 bits puis décalage de 12, comme le circuit.
 *
 * Sous licence MIT.
 */
#include "gx_geom.h"
#include "hw.h"
#include "rt.h"
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ matrices 4x4, 20.12 */
typedef struct { int32_t m[16]; } Mtx;   /* ligne-majeure : m[ligne*4 + colonne] */

static const Mtx IDENTITY = {{0x1000, 0, 0, 0, 0, 0x1000, 0, 0, 0, 0, 0x1000, 0, 0, 0, 0, 0x1000}};

static inline int32_t fx_mul_sum4(int64_t a0, int64_t b0, int64_t a1, int64_t b1, int64_t a2, int64_t b2, int64_t a3, int64_t b3) {
    return (int32_t)((a0 * b0 + a1 * b1 + a2 * b2 + a3 * b3) >> 12);
}

/* r = a * b (a puis b, comme MTX_MULT : courante = nouvelle * courante) */
static void mtx_mul(Mtx *r, const Mtx *a, const Mtx *b) {
    Mtx t;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            t.m[i * 4 + j] = fx_mul_sum4(a->m[i * 4 + 0], b->m[0 * 4 + j], a->m[i * 4 + 1], b->m[1 * 4 + j],
                                          a->m[i * 4 + 2], b->m[2 * 4 + j], a->m[i * 4 + 3], b->m[3 * 4 + j]);
    *r = t;
}

/* ------------------------------------------------------------------ état */
static struct {
    Mtx proj, pos, vec, tex;               /* matrices courantes */
    Mtx proj_stack[1], pos_stack[32], vec_stack[32], tex_stack[1];
    int proj_sp, pos_sp, tex_sp;
    int mode;                              /* MTX_MODE : 0 proj, 1 pos, 2 pos+vec, 3 tex */
    Mtx clip; int clip_dirty;
    int32_t normal[3];                     /* dernière normale (1.9 étendue) */
    uint8_t color[3];                      /* couleur courante 0..31 */
    int16_t texcoord[2];                   /* texcoords courantes 12.4 */
    int16_t texcoord_src[2];
    int32_t last_vtx[3];                   /* dernier sommet (4.12) pour VTX_XY/XZ/YZ/DIFF */
    uint32_t polygon_attr, polygon_attr_pending, texparam, pltt_base;
    uint32_t dif_amb, spe_emi, shininess[32]; int shininess_set;
    int32_t light_dir[4][3]; uint8_t light_color[4][3]; int32_t light_half[4][3];
    int begin_type, in_begin;
    GxVertex strip[4]; int strip_count, strip_total;
    int vp_x0, vp_y0, vp_x1, vp_y1;
    uint32_t regs[0x40];
    uint32_t pos_result[4], vec_result[3];
    int box_result;
    int stack_overflow;
} G;

/* Trois tampons : en construction (g_back), complet en attente du VBlank (g_pending, -1 si
   aucun), affiché (g_shown). SWAP_BUFFERS n'est appliqué qu'au VBlank, comme sur la console :
   l'image bâtie pendant une trame n'est visible qu'à la trame suivante, en phase avec POWCNT
   (écran haut/bas) et la capture d'affichage que le jeu règle pour cette trame-là. */
static GxFrame g_frames[3];
static int g_back = 0, g_pending = -1, g_shown = 1;

void gx_geom_init(void) {
    memset(&G, 0, sizeof G);
    G.proj = G.pos = G.vec = G.tex = IDENTITY;
    G.clip = IDENTITY;
    G.color[0] = G.color[1] = G.color[2] = 31;
    for (int i = 0; i < 3; i++) {
        g_frames[i].capacity = 8192;
        g_frames[i].polys = calloc(g_frames[i].capacity, sizeof(GxPolygon));
    }
}

static GxFrame *back(void) { return &g_frames[g_back]; }

const GxFrame *gx_geom_current(void) { return &g_frames[g_shown]; }

const GxFrame *gx_geom_present(void) {
    /* au VBlank : l'image en attente devient l'image affichée */
    if (g_pending >= 0) { g_shown = g_pending; g_pending = -1; }
    return &g_frames[g_shown];
}
const GxFrame *gx_geom_building(void) { return &g_frames[g_back]; }

/* ------------------------------------------------------------------ matrices : commandes */
static Mtx *current_matrix(void) {
    switch (G.mode) {
    case 0: return &G.proj;
    case 1: case 2: return &G.pos;
    default: return &G.tex;
    }
}

static void mark_clip_dirty(void) { G.clip_dirty = 1; }

static void update_clip(void) {
    if (!G.clip_dirty) return;
    mtx_mul(&G.clip, &G.pos, &G.proj);
    G.clip_dirty = 0;
}

static void mtx_load_from(Mtx *m, const uint32_t *p, int rows, int cols) {
    *m = IDENTITY;
    for (int i = 0; i < rows; i++)
        for (int j = 0; j < cols; j++)
            m->m[i * 4 + j] = (int32_t)p[i * cols + j];
}

static void apply_mult(const Mtx *n) {
    if (G.mode == 0) { mtx_mul(&G.proj, n, &G.proj); mark_clip_dirty(); }
    else if (G.mode == 1) { mtx_mul(&G.pos, n, &G.pos); mark_clip_dirty(); }
    else if (G.mode == 2) { mtx_mul(&G.pos, n, &G.pos); mtx_mul(&G.vec, n, &G.vec); mark_clip_dirty(); }
    else mtx_mul(&G.tex, n, &G.tex);
}

static void cmd_mtx_push(void) {
    if (G.mode == 0) { G.proj_stack[0] = G.proj; G.proj_sp = 1; }
    else if (G.mode == 3) { G.tex_stack[0] = G.tex; G.tex_sp = 1; }
    else {
        if (G.pos_sp < 31) { G.pos_stack[G.pos_sp] = G.pos; G.vec_stack[G.pos_sp] = G.vec; G.pos_sp++; }
        else G.stack_overflow = 1;
    }
}

static void cmd_mtx_pop(int32_t n) {
    if (G.mode == 0) { G.proj_sp = 0; G.proj = G.proj_stack[0]; mark_clip_dirty(); }
    else if (G.mode == 3) { G.tex_sp = 0; G.tex = G.tex_stack[0]; }
    else {
        n = (n << 26) >> 26;   /* 6 bits signés */
        G.pos_sp -= n;
        if (G.pos_sp < 0) { G.pos_sp = 0; G.stack_overflow = 1; }
        if (G.pos_sp > 31) { G.pos_sp = 31; G.stack_overflow = 1; }
        G.pos = G.pos_stack[G.pos_sp]; G.vec = G.vec_stack[G.pos_sp];
        mark_clip_dirty();
    }
}

static void cmd_mtx_store(int32_t n) {
    n &= 31;
    if (G.mode == 0) G.proj_stack[0] = G.proj;
    else if (G.mode == 3) G.tex_stack[0] = G.tex;
    else { G.pos_stack[n] = G.pos; G.vec_stack[n] = G.vec; }
}

static void cmd_mtx_restore(int32_t n) {
    n &= 31;
    if (G.mode == 0) { G.proj = G.proj_stack[0]; mark_clip_dirty(); }
    else if (G.mode == 3) G.tex = G.tex_stack[0];
    else { G.pos = G.pos_stack[n]; G.vec = G.vec_stack[n]; mark_clip_dirty(); }
}

static void cmd_mtx_identity(void) {
    Mtx *m = current_matrix();
    *m = IDENTITY;
    if (G.mode == 2) G.vec = IDENTITY;
    if (G.mode != 3) mark_clip_dirty();
}

static void cmd_mtx_load(const uint32_t *p, int rows, int cols) {
    Mtx n;
    mtx_load_from(&n, p, rows, cols);
    if (G.mode == 0) G.proj = n;
    else if (G.mode == 3) G.tex = n;
    else { G.pos = n; if (G.mode == 2) G.vec = n; }
    if (G.mode != 3) mark_clip_dirty();
}

static void cmd_mtx_scale(const uint32_t *p) {
    Mtx n = IDENTITY;
    n.m[0] = (int32_t)p[0]; n.m[5] = (int32_t)p[1]; n.m[10] = (int32_t)p[2];
    /* la mise à l'échelle ne touche pas la matrice des vecteurs */
    int mode = G.mode;
    if (mode == 2) G.mode = 1;
    apply_mult(&n);
    G.mode = mode;
}

/* SFC_TRACE_TRANS=1 : combien de cellules de terrain sont posées PAR IMAGE, et leur étendue.
   Compter sur toute une session ne dirait rien : le voisinage dessiné glisse avec le joueur. */
static int32_t g_cell_x[128], g_cell_z[128];
static int g_cell_n;

static void trace_trans(int32_t x, int32_t y, int32_t z) {
    static int on = -1;
    if (on < 0) on = getenv("SFC_TRACE_TRANS") != NULL;
    if (!on) return;
    (void)y;
    const int32_t cell = 2560 * 4096;
    if (x % cell || z % cell) return;          /* une cellule est un multiple exact */
    int32_t kx = x / cell, kz = z / cell;
    for (int i = 0; i < g_cell_n; i++)
        if (g_cell_x[i] == kx && g_cell_z[i] == kz) return;
    if (g_cell_n < 128) { g_cell_x[g_cell_n] = kx; g_cell_z[g_cell_n] = kz; g_cell_n++; }
}

void gx_geom_cell_stats(uint64_t frame) {
    static int on = -1;
    if (on < 0) on = getenv("SFC_TRACE_TRANS") != NULL;
    /* une image dessinée sur deux en vol : on relève la première qui a des cellules après chaque tranche
       de 120 images, pas l'image multiple de 120, qui tombait toujours sur un balayage sans dessin */
    static uint64_t dernier;
    if (on && g_cell_n && frame >= dernier + 120) {
        dernier = frame - frame % 120;
        int32_t xmin = g_cell_x[0], xmax = g_cell_x[0], zmin = g_cell_z[0], zmax = g_cell_z[0];
        for (int i = 1; i < g_cell_n; i++) {
            if (g_cell_x[i] < xmin) xmin = g_cell_x[i];
            if (g_cell_x[i] > xmax) xmax = g_cell_x[i];
            if (g_cell_z[i] < zmin) zmin = g_cell_z[i];
            if (g_cell_z[i] > zmax) zmax = g_cell_z[i];
        }
        rt_log("cellules : image %llu, %d posées, x de %d à %d, z de %d à %d",
               (unsigned long long)frame, g_cell_n, xmin, xmax, zmin, zmax);
    }
    g_cell_n = 0;
}

static void cmd_mtx_trans(const uint32_t *p) {
    trace_trans((int32_t)p[0], (int32_t)p[1], (int32_t)p[2]);
    Mtx n = IDENTITY;
    n.m[12] = (int32_t)p[0]; n.m[13] = (int32_t)p[1]; n.m[14] = (int32_t)p[2];
    apply_mult(&n);
}

/* ------------------------------------------------------------------ éclairage */
static inline int32_t sext10(uint32_t v) { return (int32_t)(v << 22) >> 22; }

static void transform_vec3(const Mtx *m, const int32_t *v, int32_t *out) {
    for (int j = 0; j < 3; j++)
        out[j] = (int32_t)(((int64_t)v[0] * m->m[0 * 4 + j] + (int64_t)v[1] * m->m[1 * 4 + j] + (int64_t)v[2] * m->m[2 * 4 + j]) >> 12);
}

static void cmd_light_vector(uint32_t p) {
    int light = p >> 30;
    int32_t v[3] = {sext10(p & 0x3FF) << 3, sext10((p >> 10) & 0x3FF) << 3, sext10((p >> 20) & 0x3FF) << 3};  /* 1.9 -> 4.12 */
    transform_vec3(&G.vec, v, G.light_dir[light]);
    /* demi-vecteur : (L + (0,0,-1)) / 2 */
    G.light_half[light][0] = G.light_dir[light][0] / 2;
    G.light_half[light][1] = G.light_dir[light][1] / 2;
    G.light_half[light][2] = (G.light_dir[light][2] - 0x1000) / 2;
}

static void cmd_normal(uint32_t p) {
    int32_t n[3] = {sext10(p & 0x3FF) << 3, sext10((p >> 10) & 0x3FF) << 3, sext10((p >> 20) & 0x3FF) << 3};
    int32_t nv[3];
    transform_vec3(&G.vec, n, nv);
    G.normal[0] = nv[0]; G.normal[1] = nv[1]; G.normal[2] = nv[2];
    /* texcoord par normale */
    if (((G.texparam >> 30) & 3) == 2) {
        int64_t s = ((int64_t)nv[0] * G.tex.m[0] + (int64_t)nv[1] * G.tex.m[4] + (int64_t)nv[2] * G.tex.m[8]) >> 21;
        int64_t t = ((int64_t)nv[0] * G.tex.m[1] + (int64_t)nv[1] * G.tex.m[5] + (int64_t)nv[2] * G.tex.m[9]) >> 21;
        G.texcoord[0] = (int16_t)(G.texcoord_src[0] + s);
        G.texcoord[1] = (int16_t)(G.texcoord_src[1] + t);
    }
    /* couleur éclairée */
    int emi_r = (G.spe_emi >> 16) & 31, emi_g = (G.spe_emi >> 21) & 31, emi_b = (G.spe_emi >> 26) & 31;
    int dif_r = G.dif_amb & 31, dif_g = (G.dif_amb >> 5) & 31, dif_b = (G.dif_amb >> 10) & 31;
    int amb_r = (G.dif_amb >> 16) & 31, amb_g = (G.dif_amb >> 21) & 31, amb_b = (G.dif_amb >> 26) & 31;
    int spe_r = G.spe_emi & 31, spe_g = (G.spe_emi >> 5) & 31, spe_b = (G.spe_emi >> 10) & 31;
    int r = emi_r, g = emi_g, b = emi_b;
    uint32_t lights = (G.polygon_attr) & 0xF;
    for (int l = 0; l < 4; l++) {
        if (!(lights & (1 << l))) continue;
        int64_t dot = -((int64_t)G.light_dir[l][0] * nv[0] + (int64_t)G.light_dir[l][1] * nv[1] + (int64_t)G.light_dir[l][2] * nv[2]);
        int32_t diffuse = (int32_t)(dot >> 12);            /* 4.12 */
        if (diffuse < 0) diffuse = 0;
        if (diffuse > 0x1000) diffuse = 0x1000;
        int64_t hdot = -((int64_t)G.light_half[l][0] * nv[0] + (int64_t)G.light_half[l][1] * nv[1] + (int64_t)G.light_half[l][2] * nv[2]);
        int32_t sh = (int32_t)(hdot >> 12);
        if (sh < 0) sh = 0;
        int32_t spec = (int32_t)(((int64_t)sh * sh) >> 12);   /* réflexion (h·n)^2 */
        if (spec > 0x1000) spec = 0x1000;
        if ((G.spe_emi >> 15) & 1) {                        /* table de brillance */
            int idx = (spec >> 5) & 0x7F;
            uint32_t tv = G.shininess[idx / 4] >> ((idx & 3) * 8);
            spec = (int32_t)((tv & 0xFF) << 4);
        }
        int lr = G.light_color[l][0], lg = G.light_color[l][1], lb = G.light_color[l][2];
        r += (spe_r * lr * spec) >> 17; g += (spe_g * lg * spec) >> 17; b += (spe_b * lb * spec) >> 17;
        r += (dif_r * lr * diffuse) >> 17; g += (dif_g * lg * diffuse) >> 17; b += (dif_b * lb * diffuse) >> 17;
        r += (amb_r * lr) >> 5; g += (amb_g * lg) >> 5; b += (amb_b * lb) >> 5;
    }
    G.color[0] = (uint8_t)(r > 31 ? 31 : r);
    G.color[1] = (uint8_t)(g > 31 ? 31 : g);
    G.color[2] = (uint8_t)(b > 31 ? 31 : b);
}

/* ------------------------------------------------------------------ sommets */
static void frame_push(GxFrame *f, const GxPolygon *p) {
    if (f->count >= f->capacity) {
        f->capacity *= 2;
        f->polys = realloc(f->polys, f->capacity * sizeof(GxPolygon));
    }
    f->polys[f->count++] = *p;
}

/* SFC_STAT_CLIP=1 : à chaque image, combien de polygones le plan lointain écarte ou coupe.
   Sert à savoir si repousser ce plan révélerait quoi que ce soit. */
static uint64_t g_clip_far, g_clip_cross, g_clip_total;
static int64_t g_depth_max, g_depth_record;

/* SFC_TRACE_PROJ=1 : la matrice de projection du jeu, et les plans proche et lointain qu'on en
 * déduit. Une projection en perspective range en m[10] = (F+N)/(N-F) et m[14] = 2FN/(N-F), d'où
 * F = m14/(m10+1) et N = m14/(m10-1) — c'est la distance de vue du jeu, en unités du monde. */
static void trace_projection(void) {
    static int on = -1;
    if (on < 0) on = getenv("SFC_TRACE_PROJ") != NULL;
    if (!on) return;
    static int32_t last10, last14;
    if (G.proj.m[10] == last10 && G.proj.m[14] == last14) return;
    last10 = G.proj.m[10]; last14 = G.proj.m[14];
    if (G.proj.m[11] == 0) {   /* colonne w nulle : projection orthographique, pas de plans */
        rt_log("projection orthographique : [%d %d %d %d / %d %d %d %d / %d %d %d %d / %d %d %d %d]",
               G.proj.m[0], G.proj.m[1], G.proj.m[2], G.proj.m[3],
               G.proj.m[4], G.proj.m[5], G.proj.m[6], G.proj.m[7],
               G.proj.m[8], G.proj.m[9], G.proj.m[10], G.proj.m[11],
               G.proj.m[12], G.proj.m[13], G.proj.m[14], G.proj.m[15]);
        return;
    }
    rt_log("projection : [%d %d %d %d / %d %d %d %d / %d %d %d %d / %d %d %d %d]",
           G.proj.m[0], G.proj.m[1], G.proj.m[2], G.proj.m[3],
           G.proj.m[4], G.proj.m[5], G.proj.m[6], G.proj.m[7],
           G.proj.m[8], G.proj.m[9], G.proj.m[10], G.proj.m[11],
           G.proj.m[12], G.proj.m[13], G.proj.m[14], G.proj.m[15]);
    double a = (double)G.proj.m[10] / 4096.0, b = (double)G.proj.m[14] / 4096.0;
    if (a <= -1.0001 || a >= -0.0001) {
        double far_ = (a + 1.0) != 0 ? b / (a + 1.0) : 0, near_ = (a - 1.0) != 0 ? b / (a - 1.0) : 0;
        rt_log("projection : m10=%d m14=%d -> proche %.2f, lointain %.2f (unités du monde)",
               G.proj.m[10], G.proj.m[14], near_, far_);
    } else {
        double far_ = b / (a + 1.0), near_ = b / (a - 1.0);
        rt_log("projection : m10=%d m14=%d -> proche %.3f, lointain %.2f (unités du monde)",
               G.proj.m[10], G.proj.m[14], near_, far_);
    }
}

void gx_geom_clip_stats(uint64_t frame) {
    trace_projection();
    static int on = -1;
    if (on < 0) on = getenv("SFC_STAT_CLIP") != NULL;
    if (!on || (frame % 300)) return;
    rt_log("découpage : image %llu, %llu polygones, %llu au-delà du plan lointain, %llu à cheval ;"
           " le plus lointain est à %lld unités du monde (record %lld)",
           (unsigned long long)frame, (unsigned long long)g_clip_total,
           (unsigned long long)g_clip_far, (unsigned long long)g_clip_cross,
           (long long)(g_depth_max / 4096),
           (long long)((g_depth_max > g_depth_record ? g_depth_max : g_depth_record) / 4096));
    g_clip_far = g_clip_cross = g_clip_total = 0;
    if (g_depth_max > g_depth_record) g_depth_record = g_depth_max;
    g_depth_max = 0;
}

static void emit_polygon(const GxVertex *v, int count) {
    GxPolygon p;
    memset(&p, 0, sizeof p);
    for (int i = 0; i < count; i++) p.v[i] = v[i];
    p.count = (uint8_t)count;
    p.attr = G.polygon_attr;
    p.texparam = G.texparam;
    p.pltt_base = G.pltt_base;
    uint32_t alpha = (G.polygon_attr >> 16) & 31;
    uint32_t fmt = (G.texparam >> 26) & 7;
    p.translucent = (alpha < 31 && alpha > 0) || fmt == 1 || fmt == 6;
    p.forme = (uint32_t)count * 0x27D4EB2Fu;
    for (int i = 0; i < count; i++) p.forme = (p.forme ^ v[i].h) * 0x01000193u + (uint32_t)i;
    p.id = p.forme ^ G.texparam * 0x165667B1u ^ G.pltt_base * 0x9E3779B1u;
    p.ox = G.pos.m[12]; p.oy = G.pos.m[13]; p.oz = G.pos.m[14];
    {
        GxFrame *f = back();
        int k = f->n_projs ? f->n_projs - 1 : -1;          /* presque toujours la dernière employée */
        if (k < 0 || memcmp(f->projs[k], G.proj.m, sizeof G.proj.m)) {
            for (k = 0; k < f->n_projs; k++) if (!memcmp(f->projs[k], G.proj.m, sizeof G.proj.m)) break;
            if (k == f->n_projs) {
                if (f->n_projs < GX_PROJS_MAX) memcpy(f->projs[f->n_projs++], G.proj.m, sizeof G.proj.m);
                else k = GX_PROJ_INCONNUE;
            }
        }
        p.proj = (uint8_t)k;
    }
    int beyond = 0;
    for (int i = 0; i < count; i++) {
        if (v[i].z > v[i].w) beyond++;
        /* La distance à la caméra, c'est w — mais une matrice de projection est définie à un
           facteur près, et celle du jeu est normalisée au quart (m11 vaut -1024 et non -4096).
           Sans corriger ce facteur, toutes les distances se lisent quatre fois trop courtes. */
        int64_t w = v[i].w;
        if (G.proj.m[11] < 0) w = w * -4096 / G.proj.m[11];
        if (w > g_depth_max) g_depth_max = w;
    }
    g_clip_total++;
    if (beyond == count) g_clip_far++;
    else if (beyond) g_clip_cross++;
    trace_projection();   /* la projection qui sert vraiment à dessiner, pas celle du VBlank */
    /* SFC_TRACE_TILES=1 : pour chaque matrice de position employée dans l'image, l'étendue des
       sommets qu'elle place et leur distance. Un sol fait d'une tuile unique s'y reconnaît à une
       étendue de la taille d'une tuile pour une seule translation. */
    {
        static int on = -1;
        if (on < 0) on = getenv("SFC_TRACE_TILES") != NULL;
        if (on) {
            static int32_t key[64][3];
            static int64_t xmin[64], xmax[64], zmin[64], zmax[64], wmax[64];
            static uint64_t npoly[64];
            static int nkey;
            int32_t tx = G.pos.m[12], ty = G.pos.m[13], tz = G.pos.m[14];
            int k = -1;
            for (int i = 0; i < nkey; i++)
                if (key[i][0] == tx && key[i][1] == ty && key[i][2] == tz) { k = i; break; }
            if (k < 0 && nkey < 64) {
                k = nkey++;
                key[k][0] = tx; key[k][1] = ty; key[k][2] = tz;
                xmin[k] = zmin[k] = (int64_t)1 << 40; xmax[k] = zmax[k] = -((int64_t)1 << 40);
                wmax[k] = 0; npoly[k] = 0;
            }
            if (k >= 0) {
                npoly[k]++;
                for (int i = 0; i < count; i++) {
                    if (v[i].x < xmin[k]) xmin[k] = v[i].x;
                    if (v[i].x > xmax[k]) xmax[k] = v[i].x;
                    if (v[i].z < zmin[k]) zmin[k] = v[i].z;
                    if (v[i].z > zmax[k]) zmax[k] = v[i].z;
                    if (v[i].w > wmax[k]) wmax[k] = v[i].w;
                }
            }
            static uint64_t seen;
            if (++seen % 400000 == 0) {
                for (int i = 0; i < nkey; i++)
                    if (npoly[i] > 200 && wmax[i] > 1000 * 4096)
                        rt_log("objet : %llu polygones, translation (%.0f %.0f %.0f), largeur %.0f, profondeur %.0f, le plus loin %.0f",
                               (unsigned long long)npoly[i], key[i][0] / 4096.0, key[i][1] / 4096.0, key[i][2] / 4096.0,
                               (xmax[i] - xmin[i]) / 4096.0, (zmax[i] - zmin[i]) / 4096.0, wmax[i] / 4096.0);
                nkey = 0;
            }
        }
    }
    frame_push(back(), &p);
}

static void add_vertex(int32_t x, int32_t y, int32_t z) {
    G.last_vtx[0] = x; G.last_vtx[1] = y; G.last_vtx[2] = z;
    if (!G.in_begin) return;
    update_clip();
    GxVertex v;
    const Mtx *m = &G.clip;
    v.x = (int32_t)(((int64_t)x * m->m[0] + (int64_t)y * m->m[4] + (int64_t)z * m->m[8] + ((int64_t)m->m[12] << 12)) >> 12);
    v.y = (int32_t)(((int64_t)x * m->m[1] + (int64_t)y * m->m[5] + (int64_t)z * m->m[9] + ((int64_t)m->m[13] << 12)) >> 12);
    v.z = (int32_t)(((int64_t)x * m->m[2] + (int64_t)y * m->m[6] + (int64_t)z * m->m[10] + ((int64_t)m->m[14] << 12)) >> 12);
    v.w = (int32_t)(((int64_t)x * m->m[3] + (int64_t)y * m->m[7] + (int64_t)z * m->m[11] + ((int64_t)m->m[15] << 12)) >> 12);
    v.r = G.color[0]; v.g = G.color[1]; v.b = G.color[2];
    /* …et ses coordonnées de texture telles que le jeu les donne, avant la matrice de texture : deux
       chiffres d'un compteur, deux boutons d'un menu sont le même carré, pas la même vignette */
    v.h = ((uint32_t)x * 0x9E3779B1u) ^ ((uint32_t)y * 0x85EBCA77u) ^ ((uint32_t)z * 0xC2B2AE3Du)
        ^ ((uint32_t)(uint16_t)G.texcoord_src[0] * 0x27D4EB2Fu) ^ ((uint32_t)(uint16_t)G.texcoord_src[1] * 0x165667B1u);
    if (((G.texparam >> 30) & 3) == 3) {
        /* texcoord par sommet : (x,y,z) * matrice texture */
        int64_t s = ((int64_t)x * G.tex.m[0] + (int64_t)y * G.tex.m[4] + (int64_t)z * G.tex.m[8]) >> 24;
        int64_t t = ((int64_t)x * G.tex.m[1] + (int64_t)y * G.tex.m[5] + (int64_t)z * G.tex.m[9]) >> 24;
        v.s = (int16_t)(G.texcoord_src[0] + s);
        v.t = (int16_t)(G.texcoord_src[1] + t);
    } else {
        v.s = G.texcoord[0]; v.t = G.texcoord[1];
    }
    back()->vertex_count++;
    switch (G.begin_type) {
    case 0:   /* triangles */
        G.strip[G.strip_count++] = v;
        if (G.strip_count == 3) { emit_polygon(G.strip, 3); G.strip_count = 0; }
        break;
    case 1:   /* quads */
        G.strip[G.strip_count++] = v;
        if (G.strip_count == 4) { emit_polygon(G.strip, 4); G.strip_count = 0; }
        break;
    case 2:   /* bande de triangles */
        if (G.strip_count < 2) { G.strip[G.strip_count++] = v; G.strip_total++; }
        else {
            GxVertex tri[3] = {G.strip[0], G.strip[1], v};
            /* orientation alternée : on garde l'ordre d'origine du matériel (0,1,2) puis (2,1,3)… */
            if (G.strip_total & 1) { tri[0] = G.strip[1]; tri[1] = G.strip[0]; }
            emit_polygon(tri, 3);
            G.strip[0] = G.strip[1]; G.strip[1] = v; G.strip_total++;
        }
        break;
    case 3:   /* bande de quads */
        G.strip[G.strip_count++] = v;
        if (G.strip_count == 4) {
            GxVertex quad[4] = {G.strip[0], G.strip[1], G.strip[3], G.strip[2]};
            emit_polygon(quad, 4);
            G.strip[0] = G.strip[2]; G.strip[1] = G.strip[3];
            G.strip_count = 2;
        }
        break;
    }
}

static inline int32_t sext16(uint32_t v) { return (int32_t)(int16_t)(v & 0xFFFF); }

static void cmd_vtx_16(const uint32_t *p) { add_vertex(sext16(p[0]), sext16(p[0] >> 16), sext16(p[1])); }
static void cmd_vtx_10(uint32_t p) {
    add_vertex(sext10(p & 0x3FF) << 6, sext10((p >> 10) & 0x3FF) << 6, sext10((p >> 20) & 0x3FF) << 6);
}
static void cmd_vtx_xy(uint32_t p) { add_vertex(sext16(p), sext16(p >> 16), G.last_vtx[2]); }
static void cmd_vtx_xz(uint32_t p) { add_vertex(sext16(p), G.last_vtx[1], sext16(p >> 16)); }
static void cmd_vtx_yz(uint32_t p) { add_vertex(G.last_vtx[0], sext16(p), sext16(p >> 16)); }
static void cmd_vtx_diff(uint32_t p) {
    /* Déplacements relatifs : dix bits signés qui s'ajoutent tels quels aux coordonnées 4.12,
       sans mise à l'échelle — ce sont les dix bits de poids faible d'une coordonnée VTX_16,
       donc une portée de plus ou moins un huitième d'unité.
     *
     * Le facteur se mesure plutôt qu'il ne se devine : sur les quatre cent onze modèles du jeu
     * qui emploient ce format, 76 % des sommets ainsi posés retombent exactement sur un sommet
     * donné en absolu ailleurs dans le même modèle — les faces voisines partagent leurs arêtes.
     * Décalé d'un cran, ce taux tombe à 3,6 %, et le modèle se disloque. */
    add_vertex(G.last_vtx[0] + sext10(p & 0x3FF), G.last_vtx[1] + sext10((p >> 10) & 0x3FF),
               G.last_vtx[2] + sext10((p >> 20) & 0x3FF));
}

static void cmd_texcoord(uint32_t p) {
    G.texcoord_src[0] = (int16_t)(p & 0xFFFF);
    G.texcoord_src[1] = (int16_t)(p >> 16);
    if (((G.texparam >> 30) & 3) == 1) {
        /* transformation des texcoords par la matrice texture (12.4 -> 20.12 : << 8) */
        int64_t s = ((int64_t)G.texcoord_src[0] * G.tex.m[0] + (int64_t)G.texcoord_src[1] * G.tex.m[4] + (int64_t)G.tex.m[8] + (int64_t)G.tex.m[12]) >> 12;
        int64_t t = ((int64_t)G.texcoord_src[0] * G.tex.m[1] + (int64_t)G.texcoord_src[1] * G.tex.m[5] + (int64_t)G.tex.m[9] + (int64_t)G.tex.m[13]) >> 12;
        G.texcoord[0] = (int16_t)s; G.texcoord[1] = (int16_t)t;
    } else {
        G.texcoord[0] = G.texcoord_src[0]; G.texcoord[1] = G.texcoord_src[1];
    }
}

/* ------------------------------------------------------------------ tests */
static void cmd_pos_test(const uint32_t *p) {
    int32_t x = sext16(p[0]), y = sext16(p[0] >> 16), z = sext16(p[1]);
    G.last_vtx[0] = x; G.last_vtx[1] = y; G.last_vtx[2] = z;
    update_clip();
    const Mtx *m = &G.clip;
    for (int j = 0; j < 4; j++)
        G.pos_result[j] = (uint32_t)(int32_t)(((int64_t)x * m->m[j] + (int64_t)y * m->m[4 + j] + (int64_t)z * m->m[8 + j] + ((int64_t)m->m[12 + j] << 12)) >> 12);
}

static void cmd_vec_test(uint32_t p) {
    int32_t v[3] = {sext10(p & 0x3FF) << 3, sext10((p >> 10) & 0x3FF) << 3, sext10((p >> 20) & 0x3FF) << 3};
    int32_t out[3];
    transform_vec3(&G.vec, v, out);
    for (int j = 0; j < 3; j++) G.vec_result[j] = (uint32_t)(int32_t)((int16_t)((out[j] >> 3) & 0xFFFF));
}

static void cmd_box_test(const uint32_t *p) {
    /* boîte (x,y,z,dx,dy,dz) 4.12 : visible si un sommet transformé est dans le volume */
    int32_t x = sext16(p[0]), y = sext16(p[0] >> 16), z = sext16(p[1]);
    int32_t dx = sext16(p[1] >> 16), dy = sext16(p[2]), dz = sext16(p[2] >> 16);
    update_clip();
    const Mtx *m = &G.clip;
    int inside = 0;
    for (int i = 0; i < 8 && !inside; i++) {
        int32_t vx = x + ((i & 1) ? dx : 0), vy = y + ((i & 2) ? dy : 0), vz = z + ((i & 4) ? dz : 0);
        int64_t cx = ((int64_t)vx * m->m[0] + (int64_t)vy * m->m[4] + (int64_t)vz * m->m[8] + ((int64_t)m->m[12] << 12)) >> 12;
        int64_t cy = ((int64_t)vx * m->m[1] + (int64_t)vy * m->m[5] + (int64_t)vz * m->m[9] + ((int64_t)m->m[13] << 12)) >> 12;
        int64_t cz = ((int64_t)vx * m->m[2] + (int64_t)vy * m->m[6] + (int64_t)vz * m->m[10] + ((int64_t)m->m[14] << 12)) >> 12;
        int64_t cw = ((int64_t)vx * m->m[3] + (int64_t)vy * m->m[7] + (int64_t)vz * m->m[11] + ((int64_t)m->m[15] << 12)) >> 12;
        if (cw > 0 && cx >= -cw && cx <= cw && cy >= -cw && cy <= cw && cz >= -cw && cz <= cw) inside = 1;
    }
    /* approximation : le matériel teste la boîte clipée ; un test par sommet suffit ici */
    G.box_result = inside;
}

/* SFC_TRACE_TEST=1 : les commandes de test du moteur 3D et leur résultat. Un jeu s'en sert
   pour savoir si un objet est visible ou si un point tombe quelque part ; si une collision ne
   se déclenche pas, il faut d'abord savoir si le jeu pose seulement la question. */
static void trace_test(const char *quoi, int32_t a, int32_t b, int32_t c, int32_t d) {
    static int on = -1;
    if (on < 0) on = getenv("SFC_TRACE_TEST") != NULL;
    if (!on) return;
    static uint64_t n;
    if (n++ < 40 || !(n % 500))
        rt_log("test %s : %d %d %d %d (image %llu)", quoi, a, b, c, d,
               (unsigned long long)hw_frames());
}

/* ------------------------------------------------------------------ dispatch */
void gx_geom_command(uint32_t cmd, const uint32_t *p, uint32_t n) {
    (void)n;
    switch (cmd) {
    case 0x10: G.mode = p[0] & 3; break;
    case 0x11: cmd_mtx_push(); break;
    case 0x12: cmd_mtx_pop((int32_t)p[0]); break;
    case 0x13: cmd_mtx_store((int32_t)p[0]); break;
    case 0x14: cmd_mtx_restore((int32_t)p[0]); break;
    case 0x15: cmd_mtx_identity(); break;
    case 0x16: cmd_mtx_load(p, 4, 4); break;
    case 0x17: cmd_mtx_load(p, 4, 3); break;
    case 0x18: { Mtx m; mtx_load_from(&m, p, 4, 4); apply_mult(&m); break; }
    case 0x19: { Mtx m; mtx_load_from(&m, p, 4, 3); apply_mult(&m); break; }
    case 0x1A: { Mtx m; mtx_load_from(&m, p, 3, 3); apply_mult(&m); break; }
    case 0x1B: cmd_mtx_scale(p); break;
    case 0x1C: cmd_mtx_trans(p); break;
    case 0x20: G.color[0] = p[0] & 31; G.color[1] = (p[0] >> 5) & 31; G.color[2] = (p[0] >> 10) & 31; break;
    case 0x21: cmd_normal(p[0]); break;
    case 0x22: cmd_texcoord(p[0]); break;
    case 0x23: cmd_vtx_16(p); break;
    case 0x24: cmd_vtx_10(p[0]); break;
    case 0x25: cmd_vtx_xy(p[0]); break;
    case 0x26: cmd_vtx_xz(p[0]); break;
    case 0x27: cmd_vtx_yz(p[0]); break;
    case 0x28: cmd_vtx_diff(p[0]); break;
    case 0x29: G.polygon_attr_pending = p[0]; break;
    case 0x2A: G.texparam = p[0]; break;
    case 0x2B: G.pltt_base = p[0] & 0x1FFF; break;
    case 0x30: G.dif_amb = p[0];
        if (p[0] & 0x8000) { G.color[0] = p[0] & 31; G.color[1] = (p[0] >> 5) & 31; G.color[2] = (p[0] >> 10) & 31; }
        break;
    case 0x31: G.spe_emi = p[0]; break;
    case 0x32: cmd_light_vector(p[0]); break;
    case 0x33: { int l = p[0] >> 30; G.light_color[l][0] = p[0] & 31; G.light_color[l][1] = (p[0] >> 5) & 31; G.light_color[l][2] = (p[0] >> 10) & 31; break; }
    case 0x34: memcpy(G.shininess, p, 32 * 4); G.shininess_set = 1; break;
    case 0x40:
        G.begin_type = p[0] & 3; G.in_begin = 1; G.strip_count = 0; G.strip_total = 0;
        G.polygon_attr = G.polygon_attr_pending;   /* POLYGON_ATTR prend effet au BEGIN */
        break;
    case 0x41: G.in_begin = 0; break;
    case 0x50: break;   /* SWAP_BUFFERS : traité par gx_geom_swap */
    case 0x60:
        G.vp_x0 = p[0] & 0xFF; G.vp_y0 = (p[0] >> 8) & 0xFF; G.vp_x1 = (p[0] >> 16) & 0xFF; G.vp_y1 = (p[0] >> 24) & 0xFF;
        back()->viewport = p[0];
        break;
    case 0x70:
        cmd_box_test(p);
        trace_test("boîte", (int32_t)G.box_result, 0, 0, 0);
        break;
    case 0x71:
        cmd_pos_test(p);
        trace_test("position", (int32_t)G.pos_result[0], (int32_t)G.pos_result[1],
                   (int32_t)G.pos_result[2], (int32_t)G.pos_result[3]);
        break;
    case 0x72:
        cmd_vec_test(p[0]);
        trace_test("vecteur", (int32_t)G.vec_result[0], (int32_t)G.vec_result[1],
                   (int32_t)G.vec_result[2], 0);
        break;
    default: break;
    }
}

static uint32_t g_serie;

/* l'en-tête d'une image : ce que les registres en disent au moment de la clore */
static void entete(GxFrame *f) {
    f->clear_color = G.regs[(0x350 - 0x320) / 4];
    f->clear_depth = G.regs[(0x354 - 0x320) / 4];
    f->fog_color = G.regs[(0x358 - 0x320) / 4];
    f->fog_offset = G.regs[(0x35C - 0x320) / 4];
    f->disp3dcnt = mmio_peek(0x060);
    memcpy(f->edge_color, &G.regs[(0x330 - 0x320) / 4], sizeof f->edge_color);
    memcpy(f->fog_table, &G.regs[(0x360 - 0x320) / 4], sizeof f->fog_table);
    memcpy(f->toon_table, &G.regs[(0x380 - 0x320) / 4], sizeof f->toon_table);
}

/* L'image en construction telle qu'elle sera à l'échange — son en-tête et le numéro qu'elle recevra —,
   sans rien échanger : le jeu l'a finie et attend, l'affichage peut la prendre sans attendre avec lui. */
GxFrame gx_geom_apercu(void) {
    GxFrame f = *back();
    entete(&f);
    f.serial = g_serie + 1;
    return f;
}

const GxFrame *gx_geom_swap(uint32_t swap_params) {
    GxFrame *f = back();
    f->serial = ++g_serie;
    f->swap_flags = swap_params;
    f->clear_color = G.regs[(0x350 - 0x320) / 4];
    f->clear_depth = G.regs[(0x354 - 0x320) / 4];
    f->fog_color = G.regs[(0x358 - 0x320) / 4];
    f->fog_offset = G.regs[(0x35C - 0x320) / 4];
    /* DISP3DCNT vit en 0x04000060, hors de la fenêtre 0x320-0x6FF du moteur de géométrie :
       le lire dans cette fenêtre rendait toujours zéro, et avec lui le test alpha et le
       brouillard passaient pour éteints quoi que le jeu en décide. */
    f->disp3dcnt = mmio_peek(0x060);
    memcpy(f->edge_color, &G.regs[(0x330 - 0x320) / 4], sizeof f->edge_color);
    memcpy(f->fog_table, &G.regs[(0x360 - 0x320) / 4], sizeof f->fog_table);
    memcpy(f->toon_table, &G.regs[(0x380 - 0x320) / 4], sizeof f->toon_table);
    int done = g_back;
    /* une image en attente jamais affichée (deux échanges dans une trame) libère son tampon */
    g_back = g_pending >= 0 ? g_pending : 3 - done - g_shown;
    g_pending = done;
    back()->count = 0;
    back()->vertex_count = 0;
    back()->n_projs = 0;
    back()->viewport = f->viewport;
    return f;
}

void gx_geom_set_register(uint32_t off, uint32_t v) {
    if (off >= 0x320 && off < 0x400) G.regs[(off - 0x320) / 4] = v;
}

uint32_t gx_geom_read(uint32_t off) {
    if (off >= 0x620 && off < 0x630) return G.pos_result[(off - 0x620) / 4];
    if (off >= 0x630 && off < 0x63C) return G.vec_result[(off - 0x630) / 4];
    if (off >= 0x640 && off < 0x680) { update_clip(); return (uint32_t)G.clip.m[(off - 0x640) / 4]; }
    if (off >= 0x680 && off < 0x6A4) { int i = (off - 0x680) / 4; return (uint32_t)G.vec.m[(i / 3) * 4 + i % 3]; }
    if (off == 0x600) {
        /* GXSTAT : box test (bit 1), piles (bits 8-12 pos, 13 proj), débordement (15), FIFO vide */
        return (G.box_result ? 2u : 0u) | ((uint32_t)G.pos_sp << 8) | ((uint32_t)G.proj_sp << 13) | (G.stack_overflow ? 0x8000u : 0u)
             | 0x04000000u;   /* FIFO vide, moins qu'à moitié */
    }
    if (off >= 0x320 && off < 0x400) return G.regs[(off - 0x320) / 4];
    return 0;
}

/* ------------------------------------------------------------------ tours d'aperçu (apercu.c) */
/* Le moteur géométrique tel qu'il était : son état, et ses trois tampons d'images. Un tampon peut avoir
   grandi pendant l'aperçu (realloc) : on garde le nouveau et l'on y recopie l'ancien contenu. */
static __typeof__(G) G_s;
static GxFrame g_frames_s[3];
static GxPolygon *g_polys_s[3];
static size_t g_polys_cap_s[3];
static int g_back_s, g_pending_s, g_shown_s;
static uint32_t g_serie_s;
void gx_geom_apercu_sauver(void) {
    G_s = G;
    for (int i = 0; i < 3; i++) {
        g_frames_s[i] = g_frames[i];
        if (g_polys_cap_s[i] < g_frames[i].count) {
            g_polys_cap_s[i] = g_frames[i].capacity;
            g_polys_s[i] = realloc(g_polys_s[i], g_polys_cap_s[i] * sizeof(GxPolygon));
        }
        if (g_frames[i].count) memcpy(g_polys_s[i], g_frames[i].polys, g_frames[i].count * sizeof(GxPolygon));
    }
    g_back_s = g_back; g_pending_s = g_pending; g_shown_s = g_shown; g_serie_s = g_serie;
}
void gx_geom_apercu_rendre(void) {
    G = G_s;
    for (int i = 0; i < 3; i++) {
        GxPolygon *polys = g_frames[i].polys;
        size_t cap = g_frames[i].capacity;
        g_frames[i] = g_frames_s[i];
        g_frames[i].polys = polys; g_frames[i].capacity = cap;
        if (g_frames_s[i].count > cap) {
            while (g_frames[i].capacity < g_frames_s[i].count) g_frames[i].capacity *= 2;
            g_frames[i].polys = realloc(g_frames[i].polys, g_frames[i].capacity * sizeof(GxPolygon));
        }
        if (g_frames_s[i].count) memcpy(g_frames[i].polys, g_polys_s[i], g_frames_s[i].count * sizeof(GxPolygon));
    }
    g_back = g_back_s; g_pending = g_pending_s; g_shown = g_shown_s; g_serie = g_serie_s;
}

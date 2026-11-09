/*
 * Moteurs 2D de la DS, rendu logiciel d'après GBATEK.
 *
 * Par ligne : chaque fond activé est rendu dans un tampon de ligne (indice de
 * palette résolu en couleur, transparence conservée), les sprites dans un
 * tampon avec leur priorité, puis la composition choisit par pixel la couche
 * visible (priorité la plus basse ; à égalité, sprite avant fond, BG0 avant
 * BG1…), applique le fondu (BLDCNT) et la luminosité maîtresse.
 *
 * La sortie est séparée en deux couches autour de la couche 3D (BG0 du
 * moteur A en mode 3D) pour que le rendu OpenGL insère les polygones entre.
 *
 * Sous licence MIT.
 */
#include "video2d.h"
#include "cpu.h"
#include "memory.h"
#include "hw.h"
#include "rt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TRANSPARENT 0x80000000u   /* marque « pas de pixel » dans les tampons de ligne */

typedef struct {
    int engine;
    uint32_t dispcnt;
    uint32_t base_io;          /* 0x000 ou 0x1000 */
    uint32_t vram_bg, vram_obj;  /* 0x06000000 / 0x06200000, 0x06400000 / 0x06600000 */
    uint32_t pal_bg, pal_obj;    /* 0x05000000 / 0x05000400 (+0x200 pour OBJ) */
    uint32_t oam;                /* 0x07000000 / 0x07000400 */
    int bg3d;                    /* BG0 = 3D */
    int mode;
} Engine;

static inline uint32_t reg32(const Engine *e, uint32_t off) { return mmio_peek(e->base_io + off); }
static inline uint16_t reg16(const Engine *e, uint32_t off) { return (uint16_t)(mmio_peek(e->base_io + (off & ~3u)) >> ((off & 2) * 8)); }

/* La mémoire vidéo telle que la voit l'ARM9, sans passer par mem_resolve : une page de 16 Ko par entrée de
   g_vram_map (memory.c), -1 où aucune banque n'est placée — exactement ce que mem_resolve fait pour
   0x06xxxxxx, seule plage que lit ce moteur. Appelé deux fois par pixel, l'appel lui-même coûtait autant
   que tout le reste. */
static inline const uint8_t *vram_ptr(uint32_t addr) {
    int32_t base = g_vram_map[(addr >> 14) & 1023u];
    return base < 0 ? NULL : g_vram + base + (addr & 0x3FFF);
}
static inline uint8_t vram8(uint32_t addr) {
    const uint8_t *p = vram_ptr(addr);
    return p ? *p : 0;
}
static inline uint16_t vram16(uint32_t addr) {
    const uint8_t *p = vram_ptr(addr & ~1u);
    if (!p) return 0;
    return (uint16_t)(p[0] | (p[1] << 8));
}
static inline uint16_t pal16(uint32_t addr) {
    return (uint16_t)(g_palette[addr & 0x7FE] | (g_palette[(addr & 0x7FE) + 1] << 8));
}

static inline uint32_t to_rgba(uint16_t c) {
    uint32_t r = (c & 31) * 255 / 31, g = ((c >> 5) & 31) * 255 / 31, b = ((c >> 10) & 31) * 255 / 31;
    return 0xFF000000u | (b << 16) | (g << 8) | r;
}

/* ------------------------------------------------------------------ palettes étendues */
/* Fonds du moteur A : banques E (MST 4 : 32 Ko, emplacements 0-3), F/G (MST 4 : 16 Ko, OFS.0 -> 0-1 / 2-3).
   Fonds du moteur B : H (MST 2 : 32 Ko). Sprites A : F/G MST 5 ; sprites B : I MST 3. */
static const uint32_t BANK_OFFSET[9] = {0, 0x20000, 0x40000, 0x60000, 0x80000, 0x90000, 0x94000, 0x98000, 0xA0000};

static uint16_t ext_pal_bg(const Engine *e, int slot, int palette, int index) {
    uint32_t off = (uint32_t)slot * 0x2000 + (uint32_t)palette * 512 + (uint32_t)index * 2;
    if (e->engine == 0) {
        uint8_t cnt_e = (uint8_t)(mmio_peek(0x244) & 0xFF);
        if ((cnt_e & 0x87) == 0x84 && off < 0x8000) return *(const uint16_t *)(g_vram + BANK_OFFSET[4] + off);
        for (int b = 5; b <= 6; b++) {
            uint8_t cnt = (uint8_t)(mmio_peek(0x244) >> ((b - 4) * 8));
            if ((cnt & 0x87) != 0x84) continue;
            uint32_t base = ((cnt >> 3) & 1) * 0x4000;
            if (off >= base && off < base + 0x4000) return *(const uint16_t *)(g_vram + BANK_OFFSET[b] + (off - base));
        }
    } else {
        uint8_t cnt_h = (uint8_t)(mmio_peek(0x248) & 0xFF);
        if ((cnt_h & 0x87) == 0x82 && off < 0x8000) return *(const uint16_t *)(g_vram + BANK_OFFSET[7] + off);
    }
    return 0;
}

static uint16_t ext_pal_obj(const Engine *e, int palette, int index) {
    uint32_t off = (uint32_t)palette * 512 + (uint32_t)index * 2;
    if (e->engine == 0) {
        for (int b = 5; b <= 6; b++) {
            uint8_t cnt = (uint8_t)(mmio_peek(0x244) >> ((b - 4) * 8));
            if ((cnt & 0x87) == 0x85 && off < 0x2000) return *(const uint16_t *)(g_vram + BANK_OFFSET[b] + off);
        }
    } else {
        uint8_t cnt_i = (uint8_t)(mmio_peek(0x248) >> 8);
        if ((cnt_i & 0x87) == 0x83 && off < 0x2000) return *(const uint16_t *)(g_vram + BANK_OFFSET[8] + off);
    }
    return 0;
}

/* ------------------------------------------------------------------ fonds texte */
static void render_text_bg(const Engine *e, int bg, int y, uint32_t *line) {
    uint16_t cnt = reg16(e, 0x08 + bg * 2);
    uint32_t char_base = ((cnt >> 2) & 15) * 0x4000, screen_base = ((cnt >> 8) & 31) * 0x800;
    if (e->engine == 0) {
        char_base += ((e->dispcnt >> 24) & 7) * 0x10000;
        screen_base += ((e->dispcnt >> 27) & 7) * 0x10000;
    }
    int colors256 = (cnt >> 7) & 1;
    int size = (cnt >> 14) & 3;
    int ext_pal = (e->dispcnt >> 30) & 1;
    int ext_slot = bg < 2 && ((cnt >> 13) & 1) ? bg + 2 : bg;
    uint32_t hofs = reg16(e, 0x10 + bg * 4) & 0x1FF, vofs = reg16(e, 0x12 + bg * 4) & 0x1FF;
    int width = (size & 1) ? 512 : 256, height = (size & 2) ? 512 : 256;
    int yy = (int)((y + vofs) & (uint32_t)(height - 1));      /* largeur et hauteur : 256 ou 512 */
    /* Les huit points d'une tuile partagent son entrée de carte et sa ligne de texels : on ne les relit que
       quand l'adresse change. Même lecture, mêmes octets — seulement moins souvent. */
    uint32_t map_prec = 0xFFFFFFFFu, ligne_prec = 0xFFFFFFFFu;
    uint16_t entry = 0;
    const uint8_t *ligne_tx = NULL;
    for (int x = 0; x < DS_W; x++) {
        int xx = (int)((x + hofs) & (uint32_t)(width - 1));
        /* bloc d'écran : 256x256 par bloc ; 512 de large -> blocs 0,1 ; 512 de haut -> +2 (ou +1 si 256 de large) */
        int block = (xx >> 8) + (yy >> 8) * (width >> 8);
        uint32_t map_addr = e->vram_bg + screen_base + (uint32_t)block * 0x800 + ((uint32_t)((yy & 255) >> 3) * 32 + (uint32_t)((xx & 255) >> 3)) * 2;
        if (map_addr != map_prec) { map_prec = map_addr; entry = vram16(map_addr); }
        uint32_t tile = entry & 0x3FF;
        int hflip = (entry >> 10) & 1, vflip = (entry >> 11) & 1, palette = entry >> 12;
        int tx = xx & 7, ty = yy & 7;
        if (hflip) tx = 7 - tx;
        if (vflip) ty = 7 - ty;
        uint32_t color = TRANSPARENT;
        /* une ligne de texels (4 ou 8 octets alignés) ne franchit jamais une page de 16 Ko */
        uint32_t ligne = e->vram_bg + char_base + (colors256 ? tile * 64 + (uint32_t)ty * 8 : tile * 32 + (uint32_t)ty * 4);
        if (ligne != ligne_prec) { ligne_prec = ligne; ligne_tx = vram_ptr(ligne); }
        if (colors256) {
            uint8_t idx = ligne_tx ? ligne_tx[tx] : 0;
            if (idx) color = to_rgba(ext_pal ? ext_pal_bg(e, ext_slot, palette, idx) : pal16(e->pal_bg + idx * 2));
        } else {
            uint8_t pair = ligne_tx ? ligne_tx[tx >> 1] : 0;
            uint8_t idx = (tx & 1) ? (pair >> 4) : (pair & 15);
            if (idx) color = to_rgba(pal16(e->pal_bg + (uint32_t)palette * 32 + idx * 2));
        }
        line[x] = color;
    }
}

/* ------------------------------------------------------------------ fonds affines et étendus */
static void render_affine_bg(const Engine *e, int bg, int y, uint32_t *line, int extended) {
    uint16_t cnt = reg16(e, 0x08 + bg * 2);
    uint32_t off = bg == 2 ? 0x20 : 0x30;
    int32_t pa = (int16_t)reg16(e, off), pb = (int16_t)reg16(e, off + 2), pc = (int16_t)reg16(e, off + 4), pd = (int16_t)reg16(e, off + 6);
    int32_t x0 = (int32_t)(reg32(e, off + 8) << 4) >> 4, y0 = (int32_t)(reg32(e, off + 12) << 4) >> 4;
    int size = (cnt >> 14) & 3;
    int wrap = (cnt >> 13) & 1;
    uint32_t char_base = ((cnt >> 2) & 15) * 0x4000, screen_base = ((cnt >> 8) & 31) * 0x800;
    if (e->engine == 0) {
        char_base += ((e->dispcnt >> 24) & 7) * 0x10000;
        screen_base += ((e->dispcnt >> 27) & 7) * 0x10000;
    }
    int bitmap = extended && ((cnt >> 7) & 1);
    int direct = bitmap && ((cnt >> 2) & 1);
    int w, h;
    if (bitmap) { static const int ws[4] = {128, 256, 512, 512}, hs[4] = {128, 256, 256, 512}; w = ws[size]; h = hs[size]; }
    else { w = h = 128 << size; }
    uint32_t bmp_base = ((cnt >> 8) & 31) * 0x4000;
    int ext_pal = (e->dispcnt >> 30) & 1;
    /* origine de la ligne : X + y*PB, Y + y*PD (les registres de référence sont recopiés par ligne) */
    int32_t cx = x0 + pb * y, cy = y0 + pd * y;
    for (int x = 0; x < DS_W; x++, cx += pa, cy += pc) {
        int px = cx >> 8, py = cy >> 8;
        uint32_t color = TRANSPARENT;
        if (wrap) { px &= w - 1; py &= h - 1; }
        else if (px < 0 || py < 0 || px >= w || py >= h) { line[x] = TRANSPARENT; continue; }
        if (bitmap) {
            if (direct) {
                uint16_t c = vram16(e->vram_bg + bmp_base + ((uint32_t)py * (uint32_t)w + (uint32_t)px) * 2);
                if (c & 0x8000) color = to_rgba(c);
            } else {
                uint8_t idx = vram8(e->vram_bg + bmp_base + (uint32_t)py * (uint32_t)w + (uint32_t)px);
                if (idx) color = to_rgba(pal16(e->pal_bg + idx * 2));
            }
        } else if (extended) {
            /* rotscale à entrées 16 bits : tuile 10 bits, retournements, palette */
            uint32_t map_addr = e->vram_bg + screen_base + ((uint32_t)(py >> 3) * (uint32_t)(w >> 3) + (uint32_t)(px >> 3)) * 2;
            uint16_t entry = vram16(map_addr);
            int tx = px & 7, ty = py & 7;
            if ((entry >> 10) & 1) tx = 7 - tx;
            if ((entry >> 11) & 1) ty = 7 - ty;
            uint8_t idx = vram8(e->vram_bg + char_base + (uint32_t)(entry & 0x3FF) * 64 + (uint32_t)ty * 8 + (uint32_t)tx);
            if (idx) color = to_rgba(ext_pal ? ext_pal_bg(e, bg, entry >> 12, idx) : pal16(e->pal_bg + idx * 2));
        } else {
            /* affine : entrées 8 bits, 256 couleurs */
            uint8_t tile = vram8(e->vram_bg + screen_base + (uint32_t)(py >> 3) * (uint32_t)(w >> 3) + (uint32_t)(px >> 3));
            uint8_t idx = vram8(e->vram_bg + char_base + (uint32_t)tile * 64 + (uint32_t)(py & 7) * 8 + (uint32_t)(px & 7));
            if (idx) color = to_rgba(pal16(e->pal_bg + idx * 2));
        }
        line[x] = color;
    }
}

/* ------------------------------------------------------------------ sprites */
static const int OBJ_W[4][4] = {{8, 16, 32, 64}, {16, 32, 32, 64}, {8, 8, 16, 32}, {0, 0, 0, 0}};
static const int OBJ_H[4][4] = {{8, 16, 32, 64}, {8, 8, 16, 32}, {16, 32, 32, 64}, {0, 0, 0, 0}};

static void render_sprites(const Engine *e, int y, uint32_t *line, uint8_t *prio, uint8_t *semi) {
    for (int x = 0; x < DS_W; x++) { line[x] = TRANSPARENT; prio[x] = 0xFF; semi[x] = 0; }
    if (!((e->dispcnt >> 12) & 1)) return;
    int mapping_1d = (e->dispcnt >> 4) & 1;
    uint32_t boundary = 32u << ((e->dispcnt >> 20) & 3);
    int bmp_1d = (e->dispcnt >> 6) & 1;
    uint32_t bmp_boundary = bmp_1d ? (128u << ((e->dispcnt >> 22) & 1)) : 0;
    int bmp_2d_256 = (e->dispcnt >> 5) & 1;
    int ext_pal = (e->dispcnt >> 31) & 1;
    for (int i = 0; i < 128; i++) {
        uint32_t base = e->oam + (uint32_t)i * 8;
        uint16_t a0 = (uint16_t)(g_oam[base - 0x07000000 + 0] | (g_oam[base - 0x07000000 + 1] << 8));
        uint16_t a1 = (uint16_t)(g_oam[base - 0x07000000 + 2] | (g_oam[base - 0x07000000 + 3] << 8));
        uint16_t a2 = (uint16_t)(g_oam[base - 0x07000000 + 4] | (g_oam[base - 0x07000000 + 5] << 8));
        int affine = (a0 >> 8) & 1;
        if (!affine && ((a0 >> 9) & 1)) continue;   /* désactivé */
        int mode = (a0 >> 10) & 3;
        if (mode == 2) continue;                    /* fenêtre : ignorée */
        int shape = (a0 >> 14) & 3, size = (a1 >> 14) & 3;
        int w = OBJ_W[shape][size], h = OBJ_H[shape][size];
        if (!w) continue;
        int oy = a0 & 0xFF, ox = a1 & 0x1FF;
        if (oy >= 192) oy -= 256;
        if (ox >= 256) ox -= 512;
        int double_size = affine && ((a0 >> 9) & 1);
        int bw = double_size ? w * 2 : w, bh = double_size ? h * 2 : h;
        if (y < oy || y >= oy + bh) continue;
        int priority = (a2 >> 10) & 3;
        int palette = a2 >> 12;
        uint32_t tile = a2 & 0x3FF;
        int colors256 = (a0 >> 13) & 1;
        int hflip = !affine && ((a1 >> 12) & 1), vflip = !affine && ((a1 >> 13) & 1);
        int32_t pa = 0x100, pb = 0, pc = 0, pd = 0x100;
        if (affine) {
            int idx = (a1 >> 9) & 31;
            uint32_t pbase = e->oam - 0x07000000 + (uint32_t)idx * 32;
            pa = (int16_t)(g_oam[pbase + 6] | (g_oam[pbase + 7] << 8));
            pb = (int16_t)(g_oam[pbase + 14] | (g_oam[pbase + 15] << 8));
            pc = (int16_t)(g_oam[pbase + 22] | (g_oam[pbase + 23] << 8));
            pd = (int16_t)(g_oam[pbase + 30] | (g_oam[pbase + 31] << 8));
        }
        int ly = y - oy;
        for (int lx = 0; lx < bw; lx++) {
            int sx = ox + lx;
            if (sx < 0 || sx >= DS_W) continue;
            if (prio[sx] <= priority && line[sx] != TRANSPARENT) continue;   /* un sprite antérieur l'emporte à priorité égale */
            int tx, ty;
            if (affine) {
                int32_t dx = lx - bw / 2, dy = ly - bh / 2;
                tx = ((pa * dx + pb * dy) >> 8) + w / 2;
                ty = ((pc * dx + pd * dy) >> 8) + h / 2;
                if (tx < 0 || ty < 0 || tx >= w || ty >= h) continue;
            } else {
                tx = hflip ? w - 1 - lx : lx;
                ty = vflip ? h - 1 - ly : ly;
            }
            uint32_t color = TRANSPARENT;
            if (mode == 3) {
                /* sprite bitmap : couleur directe, alpha = champ palette */
                uint32_t addr;
                if (bmp_1d) addr = e->vram_obj + tile * bmp_boundary + ((uint32_t)ty * (uint32_t)w + (uint32_t)tx) * 2;
                else {
                    /* carte 2D : le numéro de tuile donne X par pas de 8 points (16 octets) et Y par
                       pas de 8 lignes (8 × largeur × 2 octets) */
                    uint32_t stride = bmp_2d_256 ? 256 : 128;
                    uint32_t mask = bmp_2d_256 ? 0x1F : 0x0F;
                    addr = e->vram_obj + (tile & mask) * 16 + (tile >> (bmp_2d_256 ? 5 : 4)) * stride * 16
                         + ((uint32_t)ty * stride + (uint32_t)tx) * 2;
                }
                uint16_t c = vram16(addr);
                if (c & 0x8000) color = to_rgba(c);
            } else if (colors256) {
                uint32_t addr;
                if (mapping_1d) addr = e->vram_obj + tile * boundary + ((uint32_t)(ty >> 3) * (uint32_t)(w >> 3) + (uint32_t)(tx >> 3)) * 64;
                else addr = e->vram_obj + ((tile & ~1u) + (uint32_t)(ty >> 3) * 32 + (uint32_t)(tx >> 3) * 2) * 32;
                uint8_t idx = vram8(addr + (uint32_t)(ty & 7) * 8 + (uint32_t)(tx & 7));
                if (idx) color = to_rgba(ext_pal ? ext_pal_obj(e, palette, idx) : pal16(e->pal_obj + idx * 2));
            } else {
                uint32_t addr;
                if (mapping_1d) addr = e->vram_obj + tile * boundary + ((uint32_t)(ty >> 3) * (uint32_t)(w >> 3) + (uint32_t)(tx >> 3)) * 32;
                else addr = e->vram_obj + (tile + (uint32_t)(ty >> 3) * 32 + (uint32_t)(tx >> 3)) * 32;
                uint8_t pair = vram8(addr + (uint32_t)(ty & 7) * 4 + (uint32_t)((tx & 7) >> 1));
                uint8_t idx = (tx & 1) ? (pair >> 4) : (pair & 15);
                if (idx) color = to_rgba(pal16(e->pal_obj + (uint32_t)palette * 32 + idx * 2));
            }
            if (color == TRANSPARENT) continue;
            line[sx] = color;
            prio[sx] = (uint8_t)priority;
            semi[sx] = (uint8_t)(mode == 1 || mode == 3);
        }
    }
}

/* ------------------------------------------------------------------ composition */
static inline uint32_t blend(uint32_t a, uint32_t b, int eva, int evb) {
    uint32_t r = (((a & 0xFF) * eva + (b & 0xFF) * evb) >> 4), g = ((((a >> 8) & 0xFF) * eva + ((b >> 8) & 0xFF) * evb) >> 4),
             bl = ((((a >> 16) & 0xFF) * eva + ((b >> 16) & 0xFF) * evb) >> 4);
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (bl > 255) bl = 255;
    return 0xFF000000u | (bl << 16) | (g << 8) | r;
}

/* Fondu dont la deuxième cible est le calque 3D, inconnu de cette passe : on le confie au GPU.
   Couleur prémultipliée par eva/16, alpha = 1 - evb/16 ; dessinée en (ONE, ONE_MINUS_SRC_ALPHA),
   le résultat vaut première*eva/16 + dessous*evb/16, comme sur la console. */
static inline uint32_t premultiplied(uint32_t c, int eva, int evb) {
    uint32_t r = (c & 0xFF) * (uint32_t)eva / 16, g = ((c >> 8) & 0xFF) * (uint32_t)eva / 16,
             b = ((c >> 16) & 0xFF) * (uint32_t)eva / 16;
    uint32_t a = (uint32_t)(16 - evb) * 255 / 16;
    return (a << 24) | (b << 16) | (g << 8) | r;
}

static inline uint32_t brightness(uint32_t c, int level, int up) {
    uint32_t r = c & 0xFF, g = (c >> 8) & 0xFF, b = (c >> 16) & 0xFF;
    if (up) { r += (255 - r) * level / 16; g += (255 - g) * level / 16; b += (255 - b) * level / 16; }
    else { r -= r * level / 16; g -= g * level / 16; b -= b * level / 16; }
    return 0xFF000000u | (b << 16) | (g << 8) | r;
}

int video2d_a_on_top(void) { return (mmio_peek(0x304) >> 15) & 1; }

/* Les lignes [y0, y1) d'une image. Une ligne ne dépend que des registres, de la mémoire vidéo, des
   palettes et des attributs d'objets — jamais des autres lignes : plusieurs fils peuvent se partager
   l'image par bandes, et chaque point sort le même. La bande qui commence à la ligne 0 renseigne l'en-tête
   de la sortie (mode d'affichage, 3D, luminosité) et tient le journal ; les autres calculent les mêmes
   valeurs de leur côté, sans l'attendre. */
void video2d_render_lignes(int engine, Engine2dOutput *out, int y0, int y1) {
    Engine e;
    e.engine = engine;
    e.base_io = engine ? 0x1000 : 0;
    e.dispcnt = reg32(&e, 0);
    e.vram_bg = engine ? 0x06200000 : 0x06000000;
    e.vram_obj = engine ? 0x06600000 : 0x06400000;
    e.pal_bg = engine ? 0x05000400 : 0x05000000;
    e.pal_obj = e.pal_bg + 0x200;
    e.oam = engine ? 0x07000400 : 0x07000000;
    e.mode = e.dispcnt & 7;
    e.bg3d = engine == 0 && ((e.dispcnt >> 3) & 1);
    const int display_mode = (e.dispcnt >> 16) & 3;
    const int has_3d = e.bg3d && ((e.dispcnt >> 8) & 1) && display_mode == 1;
    uint16_t mbright = reg16(&e, 0x6C);
    int bmode = (mbright >> 14) & 3, blevel = mbright & 31;
    if (blevel > 16) blevel = 16;
    const int bright = bmode == 1 ? blevel : bmode == 2 ? -blevel : 0;
    if (y0 == 0) {
        out->display_mode = display_mode;
        out->has_3d = has_3d;
        out->brightness = bright;
    }

    /* SFC_TRACE_VIDEO=1 : l'état des registres d'affichage, au rythme des captures (SFC_DUMP) */
    static int trace = -1, every = 300, frames = 0;
    if (y0 == 0 && trace < 0) {
        const char *d = getenv("SFC_DUMP");
        if (d && atoi(d) > 0) every = atoi(d);
        trace = getenv("SFC_TRACE_VIDEO") != NULL;
    }
    if (y0 == 0 && trace > 0 && engine == 0) frames++;
    if (y0 == 0 && trace > 0 && (frames % every == 1 || every == 1)) {
        rt_log("vidéo %c (image %d) : DISPCNT=%08X mode=%u affichage=%u BG0..3=%u%u%u%u OBJ=%u 3D=%u "
               "BGCNT=%04X %04X %04X %04X BRIGHT=%04X BLDCNT=%04X BLDALPHA=%04X BLDY=%04X fond=%04X",
               engine ? 'B' : 'A', frames, e.dispcnt, e.mode, out->display_mode,
               (e.dispcnt >> 8) & 1, (e.dispcnt >> 9) & 1, (e.dispcnt >> 10) & 1, (e.dispcnt >> 11) & 1,
               (e.dispcnt >> 12) & 1, out->has_3d, reg16(&e, 0x08), reg16(&e, 0x0A), reg16(&e, 0x0C), reg16(&e, 0x0E),
               mbright, reg16(&e, 0x50), reg16(&e, 0x52), reg16(&e, 0x54), pal16(e.pal_bg));
        rt_log("vidéo %c : BG2 PA=%04X PB=%04X PC=%04X PD=%04X X=%08X Y=%08X ; BG3 PA=%04X PB=%04X PC=%04X PD=%04X X=%08X Y=%08X%s",
               engine ? 'B' : 'A', reg16(&e, 0x20), reg16(&e, 0x22), reg16(&e, 0x24), reg16(&e, 0x26), reg32(&e, 0x28), reg32(&e, 0x2C),
               reg16(&e, 0x30), reg16(&e, 0x32), reg16(&e, 0x34), reg16(&e, 0x36), reg32(&e, 0x38), reg32(&e, 0x3C), engine ? "" : " (A)");
        if (engine == 0) {
            rt_log("vidéo : DISPCAPCNT=%08X", mmio_peek(0x064));
            rt_log("vidéo : VRAMCNT=%08X %08X %02X POWCNT=%04X DISP3DCNT=%04X",
                   mmio_peek(0x240), mmio_peek(0x244), mmio_peek(0x248) & 0xFF, mmio_peek(0x304) & 0xFFFF, mmio_peek(0x060) & 0xFFFF);
            static const uint32_t sizes[9] = {0x20000, 0x20000, 0x20000, 0x20000, 0x10000, 0x4000, 0x4000, 0x8000, 0x4000};
            char line[160]; int n = 0;
            for (int b = 0; b < 9; b++) {
                uint32_t used = 0;
                for (uint32_t i = 0; i < sizes[b]; i++) used += g_vram[BANK_OFFSET[b] + i] != 0;
                n += snprintf(line + n, sizeof line - (size_t)n, " %c=%u%%", 'A' + b, used * 100 / sizes[b]);
            }
            rt_log("vidéo : octets non nuls par banque :%s", line);
        }
    }

    uint32_t backdrop = to_rgba(pal16(e.pal_bg));
    if (display_mode == 0) {
        for (int i = y0 * DS_W; i < y1 * DS_W; i++) { out->below[i] = 0xFFFFFFFFu; out->above[i] = 0; }
        return;
    }
    if (display_mode == 2) {
        /* affichage direct d'un bloc VRAM (A seulement) */
        uint32_t block = (e.dispcnt >> 18) & 3;
        for (int y = y0; y < y1; y++)
            for (int x = 0; x < DS_W; x++) {
                uint16_t c = *(const uint16_t *)(g_vram + BANK_OFFSET[block] + ((uint32_t)y * DS_W + (uint32_t)x) * 2);
                out->below[y * DS_W + x] = to_rgba(c);
                out->above[y * DS_W + x] = 0;
            }
        return;
    }
    uint16_t bldcnt = reg16(&e, 0x50), bldalpha = reg16(&e, 0x52), bldy = reg16(&e, 0x54);
    int bld_mode = (bldcnt >> 6) & 3;
    int eva = bldalpha & 31, evb = (bldalpha >> 8) & 31;
    if (eva > 16) eva = 16;
    if (evb > 16) evb = 16;
    int bld_level = bldy & 31; if (bld_level > 16) bld_level = 16;

    /* Ces tampons de ligne ne peuvent pas être statiques : les deux moteurs sont composés en
       parallèle, et ils y écriraient tous les deux — l'image se striait de bandes. Six kilooctets
       sur la pile, une fois par image et par moteur. */
    uint32_t bg_line[4][DS_W];
    uint32_t obj_line[DS_W];
    uint8_t obj_prio[DS_W], obj_semi[DS_W];
    int bg_enabled[4], bg_prio[4], bg_affine[4], bg_ext[4];
    for (int bg = 0; bg < 4; bg++) {
        bg_enabled[bg] = (e.dispcnt >> (8 + bg)) & 1;
        bg_prio[bg] = reg16(&e, 0x08 + bg * 2) & 3;
        bg_affine[bg] = bg_ext[bg] = 0;
    }
    switch (e.mode) {
    case 1: bg_affine[3] = 1; break;
    case 2: bg_affine[2] = bg_affine[3] = 1; break;
    case 3: bg_ext[3] = 1; break;
    case 4: bg_affine[2] = 1; bg_ext[3] = 1; break;
    case 5: bg_ext[2] = bg_ext[3] = 1; break;
    case 6: bg_enabled[0] = bg_enabled[1] = bg_enabled[3] = 0; bg_ext[2] = 1; break;
    default: break;
    }
    int p3d = bg_prio[0];
    for (int y = y0; y < y1; y++) {
        for (int bg = 0; bg < 4; bg++) {
            if (!bg_enabled[bg] || (bg == 0 && e.bg3d)) { for (int x = 0; x < DS_W; x++) bg_line[bg][x] = TRANSPARENT; continue; }
            if (bg_affine[bg] || bg_ext[bg]) render_affine_bg(&e, bg, y, bg_line[bg], bg_ext[bg]);
            else render_text_bg(&e, bg, y, bg_line[bg]);
        }
        render_sprites(&e, y, obj_line, obj_prio, obj_semi);
        for (int x = 0; x < DS_W; x++) {
            /* couches visibles par ordre : on trouve la première et la deuxième (pour le fondu) */
            uint32_t first = TRANSPARENT, second = TRANSPARENT;
            int first_is_obj = 0, first_layer = -1, second_layer = -1, first_prio = 4;
            int above_3d_first = 0;
            int first_semi = 0;
            for (int p = 0; p < 4 && second == TRANSPARENT; p++) {
                if (obj_line[x] != TRANSPARENT && obj_prio[x] == p) {
                    if (first == TRANSPARENT) { first = obj_line[x]; first_is_obj = 1; first_layer = 4; first_prio = p; first_semi = obj_semi[x];
                        above_3d_first = has_3d && p <= p3d; }
                    else if (second == TRANSPARENT) { second = obj_line[x]; second_layer = 4; }
                }
                if (has_3d && p == p3d && first != TRANSPARENT && second == TRANSPARENT && !(first_is_obj && first_prio == p)) {
                    /* la couche 3D vient ici : ce qui suit passe dessous */
                }
                for (int bg = 0; bg < 4 && second == TRANSPARENT; bg++) {
                    if (!bg_enabled[bg] || bg_prio[bg] != p || bg_line[bg][x] == TRANSPARENT) continue;
                    if (first == TRANSPARENT) { first = bg_line[bg][x]; first_layer = bg; first_prio = p;
                        above_3d_first = has_3d && (p < p3d); }
                    else { second = bg_line[bg][x]; second_layer = bg; }
                }
            }
            if (first == TRANSPARENT) { first = backdrop; first_layer = 5; first_prio = 4; }
            uint32_t color = first;
            /* fondu : cible 1 = couche du dessus, cible 2 = couche du dessous */
            int t1 = first_layer == 5 ? (bldcnt >> 5) & 1 : (bldcnt >> first_layer) & 1;
            int t2 = second == TRANSPARENT ? (bldcnt >> 13) & 1 : (bldcnt >> (8 + second_layer)) & 1;
            uint32_t under = second == TRANSPARENT ? backdrop : second;
            if (first_semi && t2) color = blend(first, under, eva, evb);
            else if (bld_mode == 1 && t1 && t2) color = blend(first, under, eva, evb);
            else if (bld_mode == 2 && t1) color = brightness(first, bld_level, 1);
            else if (bld_mode == 3 && t1) color = brightness(first, bld_level, 0);
            if (bright > 0) color = brightness(color, bright, 1);
            else if (bright < 0) color = brightness(color, -bright, 0);
            if (has_3d && above_3d_first) {
                /* la 3D est juste sous cette couche : si le fondu la prend pour deuxième cible
                   (bit 8 de BLDCNT, BG0), le mélange se fait au GPU */
                if (((bldcnt >> 8) & 1) && (first_semi || (bld_mode == 1 && t1)))
                    color = premultiplied(first, eva, evb);
                out->above[y * DS_W + x] = color;
                /* ce qui est sous la 3D : la deuxième couche visible ou le fond */
                uint32_t below = under;
                if (bright > 0) below = brightness(below, bright, 1);
                else if (bright < 0) below = brightness(below, -bright, 0);
                out->below[y * DS_W + x] = below;
            } else {
                out->below[y * DS_W + x] = color;
                out->above[y * DS_W + x] = 0;
            }
        }
    }
}

void video2d_render(int engine, Engine2dOutput *out) { video2d_render_lignes(engine, out, 0, DS_H); }

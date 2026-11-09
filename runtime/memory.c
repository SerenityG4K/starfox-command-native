/*
 * Carte mémoire de la DS vue par l'ARM9.
 *
 *   0x00000000–0x01FFFFFF  ITCM (32 Ko, en miroir)
 *   0x02000000–0x02FFFFFF  RAM principale (4 Mo, en miroir)
 *   0x027E0000–0x027E3FFF  DTCM (16 Ko) — prioritaire sur la RAM
 *   0x03000000–0x03FFFFFF  WRAM partagée (32 Ko, selon WRAMCNT)
 *   0x04000000–0x04FFFFFF  registres matériels (mmio.c)
 *   0x05000000–0x05FFFFFF  palettes (2 Ko)
 *   0x06000000–0x06FFFFFF  VRAM (banques A–I, selon VRAMCNT)
 *   0x07000000–0x07FFFFFF  OAM (2 Ko)
 *   0x08000000–0x09FFFFFF  cartouche GBA (absente : 0xFF)
 *   0xFFFF0000–            BIOS (lecture : zéros)
 *
 * Et la carte du coprocesseur ARM7, qui n'est pas la même :
 *
 *   0x00000000–0x00003FFF  son BIOS (absent : lectures nulles)
 *   0x02000000–0x02FFFFFF  la même RAM principale (sans fenêtre DTCM : il n'en a pas)
 *   0x03000000–0x037FFFFF  la part de WRAM partagée que WRAMCNT lui donne
 *   0x03800000–0x03FFFFFF  sa WRAM à lui (64 Ko, en miroir)
 *   0x04000000–0x04FFFFFF  ses registres — le même mot ne désigne pas la même chose des deux côtés
 *   0x06000000–0x0603FFFF  la VRAM que l'ARM9 lui prête (banques C/D en mode 2)
 *
 * Le chemin rapide (RAM principale) est dans cpu.h ; ici les autres régions.
 * Sous licence MIT.
 */
#include "cpu.h"
#include "memory.h"
#include "hw.h"
#include "arm7_run.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

uint8_t *g_main_ram;
uint8_t *g_itcm;
uint8_t *g_dtcm;
uint8_t *g_shared_wram;
uint8_t *g_palette;
uint8_t *g_oam;
uint8_t *g_vram;            /* 656 Ko : A(128) B(128) C(128) D(128) E(64) F(16) G(16) H(32) I(16) */
uint8_t *g_wram7;           /* 64 Ko en 0x03800000 : la mémoire propre du coprocesseur */

/* Table de mappage de la VRAM en pages de 16 Ko : 0x06000000 + n*16K -> offset dans g_vram ou -1 */
#define VRAM_PAGES (0x01000000 / 0x4000)
int32_t g_vram_map[VRAM_PAGES];
/* Les pages que l'ARM9 prête au coprocesseur (VRAMCNT_C/D en mode 2), en 0x06000000. */
int32_t g_vram7_map[VRAM7_PAGES];

uint8_t *g_mem_suivie;
static int g_apercus_demandes;

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

void mem_init(void) {
#ifdef _WIN32
    /* Un banc compare deux exécutions de la même fonction : il lui faut l'état d'avant, celui
       d'après, et les remettre. Copier cinq mégaoctets trois fois par comparaison coûtait deux
       millisecondes, et il y a des millions de comparaisons ; le système sait dire quelles pages
       ont été écrites, par qui que ce soit — code traduit, réécriture, copie native du runtime. */
    /* Les tours d'aperçu (apercu.c) prennent et rendent l'état page par page : ils ont besoin du même suivi
       que le banc. SFC_APERCUS=0 les retire, et le suivi avec. */
    { const char *a = getenv("SFC_APERCUS"); if (!a || (*a != '0' && strcmp(a, "non"))) g_apercus_demandes = 1; }
    if (getenv("SFC_VERIF") || getenv("SFC_VERIF_FROID") || g_apercus_demandes)
        g_mem_suivie = VirtualAlloc(NULL, MEM_SUIVIE_TAILLE, MEM_RESERVE | MEM_COMMIT | MEM_WRITE_WATCH,
                                    PAGE_READWRITE);
#endif
    if (g_mem_suivie) {
        g_main_ram = g_mem_suivie;
        g_itcm = g_mem_suivie + MEM_SUIVIE_ITCM;
        g_dtcm = g_mem_suivie + MEM_SUIVIE_DTCM;
        g_shared_wram = g_mem_suivie + MEM_SUIVIE_WRAM;
        g_palette = g_mem_suivie + MEM_SUIVIE_PALETTE;
        g_oam = g_mem_suivie + MEM_SUIVIE_OAM;
        g_vram = g_mem_suivie + MEM_SUIVIE_VRAM;
        g_wram7 = g_mem_suivie + MEM_SUIVIE_WRAM7;
        for (int i = 0; i < VRAM_PAGES; i++) g_vram_map[i] = -1;
        for (int i = 0; i < VRAM7_PAGES; i++) g_vram7_map[i] = -1;
        return;
    }
    g_main_ram = calloc(0x400000, 1);
    g_itcm = calloc(0x8000, 1);
    g_dtcm = calloc(0x4000, 1);
    g_shared_wram = calloc(0x8000, 1);
    g_palette = calloc(0x800, 1);
    g_oam = calloc(0x800, 1);
    g_vram = calloc(0xA4000, 1);
    g_wram7 = calloc(0x10000, 1);
    for (int i = 0; i < VRAM_PAGES; i++) g_vram_map[i] = -1;
    for (int i = 0; i < VRAM7_PAGES; i++) g_vram7_map[i] = -1;
}

/* ------------------------------------------------------------------ vue du coprocesseur */

/* WRAMCNT (0x04000247) partage les 32 Ko de WRAM : 0 tout à l'ARM9, 1 sa première moitié au
 * coprocesseur, 2 la seconde, 3 le tout. Quand rien ne revient au coprocesseur, il lit en
 * 0x03000000 un miroir de sa propre WRAM (`NULL` ici, l'appelant retombe dessus).
 * L'ARM9 privé de sa part lit sur console une valeur indéterminée ; plutôt que de crier à
 * l'accès hors carte on lui rend la zone, ce qui ne peut pas être pire que de l'ignorer. */
static uint8_t *shared_wram(uint32_t addr, int arm7) {
    uint32_t mode = g_io[0x247] & 3u;
    uint32_t off = addr & 0x7FFFu;
    if (mode == 0) return arm7 ? NULL : g_shared_wram + off;
    if (mode == 3) return g_shared_wram + off;
    /* moitiés de 16 Ko : chacun ne voit que la sienne, en miroir sur les 32 Ko */
    uint32_t half = (mode == 1) ? 0u : 0x4000u;       /* moitié donnée au coprocesseur */
    uint32_t mine = arm7 ? half : (half ^ 0x4000u);
    return g_shared_wram + mine + (off & 0x3FFFu);
}

uint8_t *mem_resolve7(uint32_t addr, uint32_t size) {
    uint32_t top = addr >> 24;
    if (top == 0x02) return g_main_ram + (addr & 0x3FFFFF);   /* pas de DTCM de ce côté */
    if (top == 0x03) {
        if (addr & 0x00800000u) return g_wram7 + (addr & 0xFFFF);
        uint8_t *p = shared_wram(addr, 1);
        return p ? p : g_wram7 + (addr & 0xFFFF);
    }
    if (top == 0x06) {
        int32_t base = g_vram7_map[(addr >> 14) & (VRAM7_PAGES - 1)];
        if (base < 0) return NULL;
        return g_vram + base + (addr & 0x3FFF);
    }
    (void)size;
    return NULL;
}

/* Résolution d'une adresse hors RAM principale vers un octet hôte, ou NULL (registres, absent). */
uint8_t *mem_resolve(uint32_t addr, uint32_t size) {
    uint32_t top = addr >> 24;
    if (top < 0x02) {
        return g_itcm + (addr & 0x7FFF);
    }
    if (top == 0x02) {
        uint32_t off = addr & 0x3FFFFF;
        if ((addr & 0xFFFFC000u) == 0x027E0000u) return g_dtcm + (addr & 0x3FFF);
        return g_main_ram + off;
    }
    if (top == 0x03) return shared_wram(addr, 0);
    if (top == 0x05) return g_palette + (addr & 0x7FF);
    if (top == 0x06) {
        int32_t base = g_vram_map[(addr >> 14) & (VRAM_PAGES - 1)];
        if (base < 0) return NULL;
        return g_vram + base + (addr & 0x3FFF);
    }
    if (top == 0x07) return g_oam + (addr & 0x7FF);
    (void)size;
    return NULL;
}

uint32_t mem_read32_slow(Cpu *c, uint32_t addr) {
    uint32_t top = addr >> 24;
    if (top == 0x04) return c->is_arm7 ? mmio7_read32(c, addr) : mmio_read32(c, addr);
    uint8_t *p = c->is_arm7 ? mem_resolve7(addr, 4) : mem_resolve(addr, 4);
    if (p) { uint32_t v; memcpy(&v, p, 4); return v; }
    if (top >= 0x08 && top <= 0x09) return 0xFFFFFFFFu;
    if (c->is_arm7 && top == 0) return 0u;               /* son BIOS : absent, pas une erreur */
    return mem_unmapped_read(c, addr, 4);
}
uint16_t mem_read16_slow(Cpu *c, uint32_t addr) {
    uint32_t top = addr >> 24;
    if (top == 0x04) return c->is_arm7 ? mmio7_read16(c, addr) : mmio_read16(c, addr);
    uint8_t *p = c->is_arm7 ? mem_resolve7(addr, 2) : mem_resolve(addr, 2);
    if (p) { uint16_t v; memcpy(&v, p, 2); return v; }
    if (top >= 0x08 && top <= 0x09) return 0xFFFFu;
    if (c->is_arm7 && top == 0) return 0u;
    return (uint16_t)mem_unmapped_read(c, addr, 2);
}
uint8_t mem_read8_slow(Cpu *c, uint32_t addr) {
    uint32_t top = addr >> 24;
    if (top == 0x04) return c->is_arm7 ? mmio7_read8(c, addr) : mmio_read8(c, addr);
    uint8_t *p = c->is_arm7 ? mem_resolve7(addr, 1) : mem_resolve(addr, 1);
    if (p) return *p;
    if (top >= 0x08 && top <= 0x09) return 0xFFu;
    if (c->is_arm7 && top == 0) return 0u;
    return (uint8_t)mem_unmapped_read(c, addr, 1);
}
/* Écritures en mémoire graphique — palettes, mémoire vidéo, attributs d'objets. Le chemin
 * rapide ne couvre que la RAM principale : tout ce qui touche au graphisme passe forcément ici
 * (le transfert direct y compris, qui écrit par ces mêmes fonctions), ce qui permet au rendu de
 * savoir qu'une texture n'a pas pu changer et de ne pas la relire pour rien. */
static uint64_t g_gfx_writes;
uint64_t mem_gfx_writes(void) { return g_gfx_writes; }
/* Palette et mémoire vidéo seulement : les attributs d'objets, eux, sont réécrits à chaque
   image et feraient croire à un changement permanent. */
static inline void note_gfx(uint32_t top) { if (top == 0x05 || top == 0x06) g_gfx_writes++; }
/* Les écritures du jeu en mémoire vidéo, page physique de 16 Ko par page : l'affichage libre fait les captures
   d'affichage après coup, et ne doit pas les poser par-dessus ce que le jeu a écrit depuis l'image capturée
   (video.cpp, poser_capture). Les tours d'aperçu les prennent et les rendent avec le reste. */
uint32_t g_vram_ecrit[VRAM_PAGES_PHYS];
static inline void note_vram(uint32_t top, const uint8_t *p) {
    if (top == 0x06 && p) g_vram_ecrit[(size_t)(p - g_vram) >> 14]++;
}

void mem_write32_slow(Cpu *c, uint32_t addr, uint32_t v) {
    uint32_t top = addr >> 24;
    note_gfx(top);
    if (top == 0x04) { if (c->is_arm7) mmio7_write32(c, addr, v); else mmio_write32(c, addr, v); return; }
    uint8_t *p = c->is_arm7 ? mem_resolve7(addr, 4) : mem_resolve(addr, 4);
    note_vram(top, p);
    if (p) { memcpy(p, &v, 4); return; }
    mem_unmapped_write(c, addr, 4, v);
}
void mem_write16_slow(Cpu *c, uint32_t addr, uint16_t v) {
    uint32_t top = addr >> 24;
    note_gfx(top);
    if (top == 0x04) { if (c->is_arm7) mmio7_write16(c, addr, v); else mmio_write16(c, addr, v); return; }
    uint8_t *p = c->is_arm7 ? mem_resolve7(addr, 2) : mem_resolve(addr, 2);
    note_vram(top, p);
    if (p) { memcpy(p, &v, 2); return; }
    mem_unmapped_write(c, addr, 2, v);
}
void mem_write8_slow(Cpu *c, uint32_t addr, uint8_t v) {
    uint32_t top = addr >> 24;
    note_gfx(top);
    if (top == 0x04) { if (c->is_arm7) mmio7_write8(c, addr, v); else mmio_write8(c, addr, v); return; }
    uint8_t *p = c->is_arm7 ? mem_resolve7(addr, 1) : mem_resolve(addr, 1);
    note_vram(top, p);
    if (p) { *p = v; return; }
    /* les écritures d'octet en VRAM sont ignorées par le matériel */
    if (top == 0x06) return;
    mem_unmapped_write(c, addr, 1, v);
}

/* ------------------------------------------------------------------ tours d'aperçu (apercu.c) */
static int32_t g_vram_map_s[VRAM_PAGES], g_vram7_map_s[VRAM7_PAGES];
static uint32_t g_vram_ecrit_s[VRAM_PAGES_PHYS];
void mem_apercu_sauver(void) {
    memcpy(g_vram_map_s, g_vram_map, sizeof g_vram_map_s); memcpy(g_vram7_map_s, g_vram7_map, sizeof g_vram7_map_s);
    memcpy(g_vram_ecrit_s, g_vram_ecrit, sizeof g_vram_ecrit_s);
}
void mem_apercu_rendre(void) {
    memcpy(g_vram_map, g_vram_map_s, sizeof g_vram_map_s); memcpy(g_vram7_map, g_vram7_map_s, sizeof g_vram7_map_s);
    memcpy(g_vram_ecrit, g_vram_ecrit_s, sizeof g_vram_ecrit_s);
}

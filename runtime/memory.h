/* Carte mémoire et registres : interface interne de la couche d'exécution. Sous licence MIT. */
#pragma once
#include "cpu.h"

#ifdef __cplusplus
extern "C" {
#endif

extern uint8_t *g_shared_wram, *g_palette, *g_oam, *g_vram;
extern uint8_t *g_wram7;               /* 64 Ko propres au coprocesseur, en 0x03800000 */
extern int32_t g_vram_map[];
uint64_t mem_gfx_writes(void);   /* compteur d'ecritures en palette, VRAM et OAM */
#define VRAM7_PAGES 16                 /* 0x06000000–0x0603FFFF en pages de 16 Ko */
extern int32_t g_vram7_map[VRAM7_PAGES];
#define VRAM_PAGES_PHYS (0xA4000 / 0x4000)          /* les banques A à I, en pages de 16 Ko */
extern uint32_t g_vram_ecrit[VRAM_PAGES_PHYS];      /* les écritures du jeu, page par page (memory.c) */

void     mem_init(void);
/* Quand un banc est demandé (SFC_VERIF, SFC_VERIF_FROID), toute la mémoire que le jeu peut
   écrire tient dans une seule réservation dont le système suit les pages écrites : RAM, ITCM,
   DTCM, WRAM partagée, palette, OAM, VRAM, dans cet ordre (MEM_SUIVIE_*). NULL sinon. */
extern uint8_t *g_mem_suivie;
#define MEM_SUIVIE_ITCM    0x400000u
#define MEM_SUIVIE_DTCM    0x408000u
#define MEM_SUIVIE_WRAM    0x40C000u
#define MEM_SUIVIE_PALETTE 0x414000u
#define MEM_SUIVIE_OAM     0x415000u
#define MEM_SUIVIE_VRAM    0x416000u
#define MEM_SUIVIE_WRAM7   0x4BA000u      /* les 64 Ko propres au coprocesseur : son pilote son y vit */
#define MEM_SUIVIE_TAILLE  0x4CA000u
uint8_t *mem_resolve(uint32_t addr, uint32_t size);    /* vue de l'ARM9 */
uint8_t *mem_resolve7(uint32_t addr, uint32_t size);   /* vue du coprocesseur */

/* registres matériels (mmio.c) */
uint32_t mmio_read32(Cpu *c, uint32_t addr);
uint16_t mmio_read16(Cpu *c, uint32_t addr);
uint8_t  mmio_read8(Cpu *c, uint32_t addr);
void     mmio_write32(Cpu *c, uint32_t addr, uint32_t v);
void     mmio_write16(Cpu *c, uint32_t addr, uint16_t v);
void     mmio_write8(Cpu *c, uint32_t addr, uint8_t v);

/* accès hors carte : diagnostic */
uint32_t mem_unmapped_read(Cpu *c, uint32_t addr, uint32_t size);
void     mem_unmapped_write(Cpu *c, uint32_t addr, uint32_t size, uint32_t v);

#ifdef __cplusplus
}
#endif

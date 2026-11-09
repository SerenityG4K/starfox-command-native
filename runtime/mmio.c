/*
 * Registres matériels de l'ARM9 (0x04000000–0x04FFFFFF).
 *
 * Chaque registre est un mot de l'espace `g_io` ; les écritures à effet
 * (DMA, diviseur, racine carrée, IME/IE/IF, VRAMCNT, IPC, carte) passent par
 * des fonctions dédiées. Les registres 3D (0x04000320–0x040006FF) sont
 * confiés à gx.c, les 2D (0x04000000–0x0400006F et 0x04001000–0x0400106F)
 * sont simplement mémorisés pour le rendu.
 *
 * Sous licence MIT.
 */
#include "rt.h"
#include "hw.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void rt_dump_dispatch(void);

uint8_t g_io[0x2000];          /* 0x04000000–0x04001FFF */

static inline uint32_t io32(uint32_t off) { uint32_t v; memcpy(&v, g_io + off, 4); return v; }
static inline uint16_t io16(uint32_t off) { uint16_t v; memcpy(&v, g_io + off, 2); return v; }
static inline void io32w(uint32_t off, uint32_t v) { memcpy(g_io + off, &v, 4); }
static inline void io16w(uint32_t off, uint16_t v) { memcpy(g_io + off, &v, 2); }

/* ------------------------------------------------------------------ diviseur et racine carrée */
static void div_update(void) {
    uint32_t cnt = io32(0x280);
    int64_t num, den;
    uint32_t mode = cnt & 3u;
    if (mode == 0) {
        num = (int32_t)io32(0x290);
        den = (int32_t)io32(0x298);
    } else if (mode == 1) {
        num = (int64_t)(((uint64_t)io32(0x294) << 32) | io32(0x290));
        den = (int32_t)io32(0x298);
    } else {
        num = (int64_t)(((uint64_t)io32(0x294) << 32) | io32(0x290));
        den = (int64_t)(((uint64_t)io32(0x29C) << 32) | io32(0x298));
    }
    int64_t q, r;
    uint32_t flags = cnt & 3u;
    if (den == 0) {
        /* division par zéro : quotient ±1 selon le signe du numérateur, reste = numérateur ;
           en mode 32 bits, le mot haut du quotient est inversé (comportement du matériel) */
        flags |= 0x4000u;
        q = (num < 0) ? 1 : -1;
        if (mode == 0) q ^= (int64_t)0xFFFFFFFF00000000ll;
        r = num;
    } else if (num == INT64_MIN && den == -1) {
        q = INT64_MIN; r = 0;
    } else {
        q = num / den;
        r = num % den;
    }
    io32w(0x2A0, (uint32_t)q); io32w(0x2A4, (uint32_t)((uint64_t)q >> 32));
    io32w(0x2A8, (uint32_t)r); io32w(0x2AC, (uint32_t)((uint64_t)r >> 32));
    io32w(0x280, flags);
}

static void sqrt_update(void) {
    uint32_t cnt = io32(0x2B0);
    uint64_t v = (cnt & 1u) ? (((uint64_t)io32(0x2BC) << 32) | io32(0x2B8)) : io32(0x2B8);
    uint64_t r = 0, bit = 1ull << 62;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; } else r >>= 1;
        bit >>= 2;
    }
    io32w(0x2B4, (uint32_t)r);
    io32w(0x2B0, cnt & 1u);
}

/* ------------------------------------------------------------------ VRAM */
/* Tailles des banques (Ko) et offsets dans g_vram */
static const uint32_t vram_size[9] = {128, 128, 128, 128, 64, 16, 16, 32, 16};
static uint32_t vram_offset(int bank) {
    uint32_t off = 0;
    for (int i = 0; i < bank; i++) off += vram_size[i] * 1024;
    return off;
}

static void vram_map_range(uint32_t addr, uint32_t size, int32_t offset) {
    for (uint32_t a = addr; a < addr + size; a += 0x4000)
        g_vram_map[(a >> 14) & (0x01000000 / 0x4000 - 1)] = offset < 0 ? -1 : (int32_t)(offset + (a - addr));
}

/* Les banques C et D en mode 2 quittent l'ARM9 : elles répondent en 0x06000000 chez le
 * coprocesseur (OFS choisit laquelle des deux fenêtres de 128 Ko). */
static void vram7_map_range(uint32_t addr, uint32_t size, int32_t offset) {
    for (uint32_t a = addr; a < addr + size; a += 0x4000) {
        uint32_t page = (a >> 14) & (VRAM7_PAGES - 1);
        g_vram7_map[page] = offset < 0 ? -1 : (int32_t)(offset + (a - addr));
    }
}

static void vram_remap(void) {
    for (int i = 0; i < 0x01000000 / 0x4000; i++) g_vram_map[i] = -1;
    for (int i = 0; i < VRAM7_PAGES; i++) g_vram7_map[i] = -1;
    /* MST/OFS de chaque banque : VRAMCNT_A..I en 0x240..0x249 (0x247 = WRAMCNT) */
    static const uint32_t regs[9] = {0x240, 0x241, 0x242, 0x243, 0x244, 0x245, 0x246, 0x248, 0x249};
    for (int b = 0; b < 9; b++) {
        uint8_t cnt = g_io[regs[b]];
        if (!(cnt & 0x80)) continue;
        uint32_t mst = cnt & 7u, ofs = (cnt >> 3) & 3u;
        uint32_t size = vram_size[b] * 1024;
        int32_t base = (int32_t)vram_offset(b);
        switch (b) {
        case 0: case 1: case 2: case 3:            /* A-D */
            if (mst == 0) vram_map_range(0x06800000 + b * 0x20000, size, base);
            else if (mst == 1) vram_map_range(0x06000000 + ofs * 0x20000, size, base);
            else if (mst == 2 && b < 2) vram_map_range(0x06400000 + (ofs & 1) * 0x20000, size, base);
            else if (mst == 4 && (b == 2 || b == 3)) vram_map_range(b == 2 ? 0x06200000 : 0x06600000, size, base);
            else if (mst == 2 && (b == 2 || b == 3)) vram7_map_range(0x06000000 + (ofs & 1) * 0x20000, size, base);
            /* mst 3 : texture — la banque sert au GPU, pas à une fenêtre d'adresses */
            break;
        case 4:                                     /* E */
            if (mst == 0) vram_map_range(0x06880000, size, base);
            else if (mst == 1) vram_map_range(0x06000000, size, base);
            else if (mst == 2) vram_map_range(0x06400000, size, base);
            break;
        case 5: case 6:                             /* F, G */
            if (mst == 0) vram_map_range(0x06890000 + (b - 5) * 0x4000, size, base);
            else if (mst == 1) vram_map_range(0x06000000 + (ofs & 1) * 0x4000 + (ofs >> 1) * 0x10000, size, base);
            else if (mst == 2) vram_map_range(0x06400000 + (ofs & 1) * 0x4000 + (ofs >> 1) * 0x10000, size, base);
            break;
        case 7:                                     /* H */
            if (mst == 0) vram_map_range(0x06898000, size, base);
            else if (mst == 1) vram_map_range(0x06200000, size, base);
            break;
        case 8:                                     /* I */
            if (mst == 0) vram_map_range(0x068A0000, size, base);
            else if (mst == 1) vram_map_range(0x06208000, size, base);
            else if (mst == 2) vram_map_range(0x06600000, size, base);
            break;
        }
    }
}

/* ------------------------------------------------------------------ DMA */
int g_in_dma = -1;                 /* canal en cours d exécution (diagnostic du point d observation) */
uint32_t g_dma_src, g_dma_dst, g_dma_cnt;
static void dma_run(Cpu *c, int ch) {
    g_perturbations++;                  /* une copie que rien dans la fonction jugée ne rejouera */
    uint32_t base = 0xB0 + ch * 12;
    g_in_dma = ch; g_dma_src = io32(base); g_dma_dst = io32(base + 4); g_dma_cnt = io32(base + 8);
    uint32_t src = io32(base) & 0x0FFFFFFF, dst = io32(base + 4) & 0x0FFFFFFF;
    uint32_t cnt = io32(base + 8);
    uint32_t count = cnt & 0x1FFFFF;
    if (count == 0) count = 0x200000;
    static unsigned traced;   /* les DMA « carte » d'un mot sont innombrables : on n'en trace que quelques-uns */
    if (card_tracing() && (((cnt >> 27) & 7) != 5 || traced++ < 16 || traced % 4096 == 0))
        rt_log("DMA%d : mode %u, %u x %u octets, 0x%08X -> 0x%08X, répétition=%u, IRQ=%u", ch, (cnt >> 27) & 7,
               count, ((cnt >> 26) & 1) ? 4 : 2, src, dst, (cnt >> 25) & 1, (cnt >> 30) & 1);
    uint32_t dst_ctl = (cnt >> 21) & 3, src_ctl = (cnt >> 23) & 3;
    int word = (cnt >> 26) & 1;
    uint32_t step = word ? 4 : 2;
    uint32_t fill = 0xE0 + ch * 4;
    int from_fill = (src >= 0x040000E0 && src < 0x040000F0);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t v;
        if (from_fill) v = io32(fill);
        else v = word ? mem_read32(c, src) : mem_read16(c, src);
        if (word) mem_write32(c, dst, v); else mem_write16(c, dst, (uint16_t)v);
        if (dst_ctl == 0 || dst_ctl == 3) dst += step; else if (dst_ctl == 1) dst -= step;
        if (src_ctl == 0) src += step; else if (src_ctl == 1) src -= step;
    }
    if (dst_ctl != 3) io32w(base + 4, dst);
    io32w(base, src);
    if (!((cnt >> 25) & 1)) io32w(base + 8, cnt & ~0x80000000u);   /* pas de répétition : désactivé */
    if ((cnt >> 30) & 1) hw_raise_irq(c, 8 + ch);
    g_in_dma = -1;
}

void dma_trigger(Cpu *c, int mode) {
    /* mode : 0 immédiat, 1 vblank, 2 hblank, 3 spécial (carte/GX) */
    for (int ch = 0; ch < 4; ch++) {
        uint32_t cnt = io32(0xB8 + ch * 12);
        if (!(cnt & 0x80000000u)) continue;
        int start = (cnt >> 27) & 7;
        if ((mode == 0 && start == 0) || (mode == 1 && start == 1) || (mode == 2 && start == 2)
            || (mode == 3 && start == 5) || (mode == 4 && start == 7))
            dma_run(c, ch);
    }
}

/* ------------------------------------------------------------------ accès */
static uint32_t read_reg(Cpu *c, uint32_t off, uint32_t size) {
    if (off >= 0x320 && off < 0x700) return gx_read(c, off, size);
    switch (off & ~3u) {
    case 0x004: {                                    /* DISPSTAT | VCOUNT */
        /* une boucle d'attente active doit voir le temps passer */
        hw_advance(c, 64);
        uint32_t v = (io16(0x004) & 0xFFB8u) | hw_dispstat_bits() | ((uint32_t)hw_vcount() << 16);
        return v;
    }
    case 0x130: return hw_keyinput() | ((uint32_t)io16(0x132) << 16);
    case 0x180: return ipc_read_sync(c);
    case 0x184: return ipc_read_fifocnt(c);
    case 0x1A0: return (io32(0x1A0) & 0xFFFFu) | (card_read_auxspidata(c) << 16);   /* AUXSPICNT | AUXSPIDATA */
    case 0x1A4: return card_read_romctrl(c);
    default: break;
    }
    if (off >= 0x100 && off < 0x110) { hw_advance(c, 64); return hw_timer_read(c, off); }
    if (off >= 0x280 && off < 0x2C0) return io32(off & ~3u);
    return io32(off & ~3u);
}

static void write_reg(Cpu *c, uint32_t off, uint32_t v, uint32_t size) {
    if (off >= 0x320 && off < 0x700) { gx_write(c, off, v, size); return; }
    if ((off >= 0x1A0 && off < 0x1C0) || off >= 0x100000) { extern void apercu_abandon(int); apercu_abandon(5); }   /* la cartouche */
    uint32_t aligned = off & ~3u;
    /* SFC_TRACE_VIDEO=1 : fondus (BLDCNT/BLDALPHA), luminosite et capture d affichage, avec l image */
    static int trace_video = -1;
    if (trace_video < 0) trace_video = getenv("SFC_TRACE_VIDEO") != NULL;
    if (trace_video && (aligned == 0x050 || aligned == 0x054 || aligned == 0x064 || aligned == 0x06C || aligned == 0x1050 || aligned == 0x106C || aligned == 0x304 || aligned == 0x240 || aligned == 0x244 || aligned == 0x248 || aligned == 0x000 || aligned == 0x1000)) {
        static int n;
        if (n++ < 400) rt_log("video : image %llu, ecriture 0x%03X = %08X (%u octets)", (unsigned long long)hw_frames(), off, v, size);
    }
    switch (aligned) {
    case 0x0B8: case 0x0C4: case 0x0D0: case 0x0DC: {   /* DMAxCNT */
        uint32_t old = io32(aligned);
        io32w(aligned, v);
        if ((v & 0x80000000u) && !(old & 0x80000000u)) {
            uint32_t start = (v >> 27) & 7;
            /* immédiat : tout de suite ; « carte » : tout de suite aussi si la commande est déjà
               lancée et ses données prêtes (le jeu arme souvent le DMA après ROMCTRL) */
            /* mode 7, FIFO géométrique : le matériel transfère quand la FIFO a de la place ;
               la nôtre n'a pas de limite, les commandes sont traitées dans l'ordre : d'un coup */
            if (start == 0 || start == 7) dma_run(c, (int)((aligned - 0xB8) / 12));
            else if (start == 5 && card_data_ready()) card_feed_dma(c);
        }
        return;
    }
    case 0x100: case 0x104: case 0x108: case 0x10C:
        /* La recharge et le contrôle s'écrivent en deux demi-mots : le mot doit rester ici,
           sinon la seconde écriture recompose la première à zéro. */
        io32w(aligned, v);
        hw_timer_write(c, aligned, v);
        return;
    case 0x180: ipc_write_sync(c, v); return;
    case 0x184: ipc_write_fifocnt(c, v); return;
    case 0x188: ipc_write_fifosend(c, v); return;
    case 0x1A0:
        if (size == 4 || (v & 0xFFFF) != (io32(0x1A0) & 0xFFFF)) card_write_auxspi(c, v & 0xFFFF);
        if (size == 4 || size == 2) { /* écriture de AUXSPIDATA (demi-mot haut) */ }
        return;
    case 0x1A4: card_write_romctrl(c, v); return;
    case 0x1A8: case 0x1AC: io32w(aligned, v); return;   /* commande carte (8 octets) */
    case 0x208: io32w(0x208, v & 1u); hw_check_irq(c); return;
    case 0x210: io32w(0x210, v); hw_check_irq(c); return;
    case 0x214: io32w(0x214, io32(0x214) & ~v); hw_check_irq(c); return;
    case 0x240: case 0x248:
        io32w(aligned, v); vram_remap(); return;
    case 0x244: {
        uint32_t old = io32(0x244);
        io32w(0x244, v);
        vram_remap();
        /* WRAMCNT (octet 0x247) redistribue les 32 Ko partagés : le coprocesseur y a ses
           sections, un changement lui déplace le sol sous les pieds. */
        if (((old ^ v) & 0x03000000u) && getenv("SFC_TRACE_ARM7"))
            rt_log("WRAMCNT : %u -> %u", (old >> 24) & 3u, (v >> 24) & 3u);
        return;
    }
    case 0x280: io32w(0x280, v); div_update(); return;
    case 0x290: case 0x294: case 0x298: case 0x29C: io32w(aligned, v); div_update(); return;
    case 0x2B0: io32w(0x2B0, v); sqrt_update(); return;
    case 0x2B8: case 0x2BC: io32w(aligned, v); sqrt_update(); return;
    case 0x304: io32w(0x304, v); return;              /* POWCNT1 */
    default: break;
    }
    io32w(aligned, v);
}

/* Les accès partiels sont recomposés sur le mot aligné. */
/* Compte les accès aux registres : le banc à froid annule un essai qui en fait, car le matériel
   ne se laisse ni remettre en arrière ni rejouer. */
uint64_t g_acces_mmio;
/* Le banc à froid tient lieu de matériel : une lecture reçoit une valeur tirée de la graine de
   l'essai, une écriture est notée, et le vrai matériel ne voit rien (natif.c). */
uint32_t (*g_mmio_bouchon)(Cpu *c, uint32_t addr, uint32_t valeur, int octets, int ecriture);

uint32_t mmio_read32(Cpu *c, uint32_t addr) {
    g_acces_mmio++;
    if (g_mmio_bouchon) return (uint32_t)g_mmio_bouchon(c, addr, 0, 4, 0);
    uint32_t off = addr & 0x00FFFFFFu;
    if (addr == 0x04100010u) return card_read_data(c);
    if (addr == 0x04100000u) return ipc_read_fiforecv(c);
    if (off >= sizeof g_io) return 0;
    return read_reg(c, off, 4);
}
uint16_t mmio_read16(Cpu *c, uint32_t addr) {
    g_acces_mmio++;
    if (g_mmio_bouchon) return (uint16_t)g_mmio_bouchon(c, addr, 0, 2, 0);
    uint32_t off = addr & 0x00FFFFFFu;
    if (off >= sizeof g_io) return 0;
    uint32_t v = read_reg(c, off & ~3u, 2);
    return (uint16_t)(v >> ((off & 2u) * 8u));
}
uint8_t mmio_read8(Cpu *c, uint32_t addr) {
    g_acces_mmio++;
    if (g_mmio_bouchon) return (uint8_t)g_mmio_bouchon(c, addr, 0, 1, 0);
    uint32_t off = addr & 0x00FFFFFFu;
    if (off >= sizeof g_io) return 0;
    uint32_t v = read_reg(c, off & ~3u, 1);
    return (uint8_t)(v >> ((off & 3u) * 8u));
}
void mmio_write32(Cpu *c, uint32_t addr, uint32_t v) {
    g_acces_mmio++;
    if (g_mmio_bouchon) { g_mmio_bouchon(c, addr, v, 4, 1); return; }
    uint32_t off = addr & 0x00FFFFFFu;
    if (off >= sizeof g_io) return;
    write_reg(c, off & ~3u, v, 4);
}
void mmio_write16(Cpu *c, uint32_t addr, uint16_t v) {
    g_acces_mmio++;
    if (g_mmio_bouchon) { g_mmio_bouchon(c, addr, v, 2, 1); return; }
    uint32_t off = addr & 0x00FFFFFFu;
    if (off >= sizeof g_io) return;
    uint32_t aligned = off & ~3u;
    if (aligned >= 0x320 && aligned < 0x700) { gx_write(c, off, v, 2); return; }
    uint32_t cur = io32(aligned);
    uint32_t shift = (off & 2u) * 8u;
    uint32_t merged = (cur & ~(0xFFFFu << shift)) | ((uint32_t)v << shift);
    /* IF : écriture d'un demi-mot = acquittement partiel */
    if (aligned == 0x214) { io32w(0x214, cur & ~((uint32_t)v << shift)); hw_check_irq(c); return; }
    if (off == 0x1A0) { card_write_auxspi(c, v); return; }
    if (off == 0x1A2) { card_write_auxspidata(c, v); return; }
    write_reg(c, aligned, merged, 2);
}
void mmio_write8(Cpu *c, uint32_t addr, uint8_t v) {
    g_acces_mmio++;
    if (g_mmio_bouchon) { g_mmio_bouchon(c, addr, v, 1, 1); return; }
    uint32_t off = addr & 0x00FFFFFFu;
    if (off >= sizeof g_io) return;
    uint32_t aligned = off & ~3u;
    uint32_t cur = io32(aligned);
    uint32_t shift = (off & 3u) * 8u;
    uint32_t merged = (cur & ~(0xFFu << shift)) | ((uint32_t)v << shift);
    if (aligned >= 0x1A8 && aligned < 0x1B0) { g_io[off] = v; return; }   /* octets de commande carte */
    if (off >= 0x1A0 && off < 0x1C0) { extern void apercu_abandon(int); apercu_abandon(5); }   /* la cartouche */
    if (off == 0x1A0 || off == 0x1A1) { g_io[off] = v; card_write_auxspi(c, io32(0x1A0) & 0xFFFF); return; }
    if (off == 0x1A2) { card_write_auxspidata(c, v); return; }
    write_reg(c, aligned, merged, 1);
}

uint32_t mmio_peek(uint32_t off) { return io32(off & ~3u); }
void mmio_poke(uint32_t off, uint32_t v) { io32w(off & ~3u, v); }

static void rafale_fin(void);

uint32_t mem_unmapped_read(Cpu *c, uint32_t addr, uint32_t size) {
    (void)c; (void)size;
    if (addr >= 0xFFFF0000u) return 0;              /* BIOS : illisible */
    if (g_temps_fige) return 0;                     /* le banc à froid lit au hasard : sans commentaire */
    rafale_fin();
    rt_log("lecture hors carte : 0x%08X", addr);
    { static int n; if (n++ < 3 && c) {
        rt_log("  image %llu, r0=%08X r1=%08X r2=%08X r3=%08X r4=%08X r5=%08X r6=%08X r7=%08X sp=%08X lr=%08X ; pile fantôme :",
               (unsigned long long)hw_frames(), c->r[0], c->r[1], c->r[2], c->r[3], c->r[4], c->r[5], c->r[6], c->r[7], c->r[13], c->r[14]);
        rt_dump_stack(c); rt_dump_dispatch();
        { extern int rt_overlay_loaded(uint32_t); rt_log("  overlays chargés : emplacement 2-4 -> %d, 5-22 -> %d, 23-25 -> %d", rt_overlay_loaded(0x021F0000u), rt_overlay_loaded(0x02228000u), rt_overlay_loaded(0x0225A000u)); }
        /* diagnostic : mémoire autour de r4, r5 et du mot pointé par [r5+4] */
        for (int k = 4; k <= 5; k++) {
            uint32_t base = c->r[k] & ~3u;
            if (base >= 0x02000000u && base < 0x02400000u) {
                char line[400]; int m = 0;
                for (int i = -4; i < 12; i++) m += snprintf(line + m, sizeof line - (size_t)m, " %08X", mem_read32(c, base + (uint32_t)i * 4));
                rt_log("  r%d = 0x%08X, mots [-4..+11] :%s", k, c->r[k], line);
            }
        }
        uint32_t h = (c->r[5] >= 0x02000000u && c->r[5] < 0x02400000u) ? mem_read32(c, c->r[5] + 4) : 0;
        if (h >= 0x02000000u && h < 0x02400000u) {
            char line[400]; int m = 0;
            for (int i = -4; i < 12; i++) m += snprintf(line + m, sizeof line - (size_t)m, " %08X", mem_read32(c, (h & ~3u) + (uint32_t)i * 4));
            rt_log("  détenteur [r5+4] = 0x%08X, mots [-4..+11] :%s", h, line);
        }
    } }
    return 0;
}
/* Le jeu efface parfois une zone de VRAM avant d'y affecter une banque ; la console perd
   ces écritures exactement de la même façon. Elles arrivent alors par milliers d'un coup :
   on n'annonce que le début de chaque rafale contiguë, avec l'état des banques, et on
   résume la queue. */
static struct { int actif; uint32_t suivante, debut; unsigned n; } g_rafale;

static void rafale_fin(void) {
    if (g_rafale.actif && g_rafale.n > 1)
        rt_log("  … et %u écritures contiguës jusqu'à 0x%08X", g_rafale.n - 1, g_rafale.suivante - 1);
    g_rafale.actif = 0;
}

void mem_unmapped_write(Cpu *c, uint32_t addr, uint32_t size, uint32_t v) {
    if (g_temps_fige) return;                       /* le banc à froid écrit au hasard : sans commentaire */
    if (g_rafale.actif && addr == g_rafale.suivante) { g_rafale.suivante += size; g_rafale.n++; return; }
    rafale_fin();
    g_rafale.actif = 1; g_rafale.debut = addr; g_rafale.suivante = addr + size; g_rafale.n = 1;
    rt_log("écriture hors carte : 0x%08X <- 0x%08X", addr, v);
    if ((addr >> 24) == 0x06)
        rt_log("  VRAMCNT A-I = %02X %02X %02X %02X %02X %02X %02X %02X %02X (bit 7 : banque active)",
               g_io[0x240], g_io[0x241], g_io[0x242], g_io[0x243], g_io[0x244],
               g_io[0x245], g_io[0x246], g_io[0x248], g_io[0x249]);
    { static int n; if (n++ < 3 && c) { rt_log("  image %llu, pile fantôme :", (unsigned long long)hw_frames()); rt_dump_stack(c); rt_dump_dispatch(); } }
}

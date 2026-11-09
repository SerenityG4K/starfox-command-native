/*
 * Fonctions du BIOS de l'ARM9 (appels `svc`), réimplémentées d'après leur
 * comportement documenté. Le NitroSDK s'en sert pour CpuSet, la division, la
 * racine carrée, l'attente d'interruption et les décompresseurs.
 *
 * En mode Thumb le numéro est sur 8 bits ; en mode ARM il occupe les bits
 * 16–23 du champ de commentaire.
 *
 * Sous licence MIT.
 */
#include "rt.h"
#include "hw.h"
#include "arm7_run.h"
#include <math.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

#define INTR_CHECK_BUF9 0x027E3FF8u   /* drapeaux d'interruption pour IntrWait (fin de la DTCM - 8) */
#define INTR_CHECK_BUF7 0x0380FFF8u  /* les siens : fin de sa WRAM - 8, juste sous son vecteur */

static void cpu_set(Cpu *c) {
    uint32_t src = c->r[0], dst = c->r[1], ctl = c->r[2];
    uint32_t count = ctl & 0x1FFFFF;
    int fill = (ctl >> 24) & 1, words = (ctl >> 26) & 1;
    if (words) {
        src &= ~3u; dst &= ~3u;
        uint32_t v = fill ? mem_read32(c, src) : 0;
        for (uint32_t i = 0; i < count; i++) {
            if (!fill) v = mem_read32(c, src + 4 * i);
            mem_write32(c, dst + 4 * i, v);
        }
    } else {
        src &= ~1u; dst &= ~1u;
        uint16_t v = fill ? mem_read16(c, src) : 0;
        for (uint32_t i = 0; i < count; i++) {
            if (!fill) v = mem_read16(c, src + 2 * i);
            mem_write16(c, dst + 2 * i, v);
        }
    }
}

static void cpu_fast_set(Cpu *c) {
    uint32_t src = c->r[0] & ~3u, dst = c->r[1] & ~3u, ctl = c->r[2];
    uint32_t count = ((ctl & 0x1FFFFF) + 7) & ~7u;
    int fill = (ctl >> 24) & 1;
    uint32_t v = fill ? mem_read32(c, src) : 0;
    for (uint32_t i = 0; i < count; i++) {
        if (!fill) v = mem_read32(c, src + 4 * i);
        mem_write32(c, dst + 4 * i, v);
    }
}

static void bios_div(Cpu *c) {
    int32_t num = (int32_t)c->r[0], den = (int32_t)c->r[1];
    int32_t q, r;
    if (den == 0) { q = num < 0 ? 1 : -1; r = num; }
    else if (num == INT32_MIN && den == -1) { q = INT32_MIN; r = 0; }
    else { q = num / den; r = num % den; }
    c->r[0] = (uint32_t)q;
    c->r[1] = (uint32_t)r;
    c->r[3] = (uint32_t)(q < 0 ? -q : q);
}

static void bios_sqrt(Cpu *c) {
    uint32_t v = c->r[0], r = 0, bit = 1u << 30;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; } else r >>= 1;
        bit >>= 2;
    }
    c->r[0] = r;
}

static void intr_wait(Cpu *c, uint32_t discard, uint32_t flags) {
    /* Les gestionnaires du SDK posent leurs drapeaux dans INTR_CHECK_BUF ; on attend
       qu'un des bits demandés y apparaisse, IRQ activées le temps de l'attente. Le
       coprocesseur a les siens, au même endroit relatif dans sa propre mémoire. */
    const uint32_t INTR_CHECK_BUF = c->is_arm7 ? INTR_CHECK_BUF7 : INTR_CHECK_BUF9;
    if (discard) mem_write32(c, INTR_CHECK_BUF, mem_read32(c, INTR_CHECK_BUF) & ~flags);
    uint32_t saved_irq_off = c->irq_off;
    c->irq_off = 0;
    for (int guard = 0; guard < 100000; guard++) {
        uint32_t have = mem_read32(c, INTR_CHECK_BUF);
        if (have & flags) {
            mem_write32(c, INTR_CHECK_BUF, have & ~flags);
            break;
        }
        rt_cpu_wait_interrupt(c);
    }
    c->irq_off = saved_irq_off;
}

/* ---- décompresseurs : en-tête u32 = type << 4 | taille << 8 ---- */
static void lz77(Cpu *c, int vram) {
    uint32_t src = c->r[0], dst = c->r[1];
    uint32_t header = mem_read32(c, src);
    uint32_t size = header >> 8;
    src += 4;
    uint32_t out = 0;
    uint16_t pending = 0; int have = 0;
    while (out < size) {
        uint8_t flags = mem_read8(c, src++);
        for (int b = 0; b < 8 && out < size; b++, flags <<= 1) {
            if (flags & 0x80) {
                uint8_t b1 = mem_read8(c, src++), b2 = mem_read8(c, src++);
                uint32_t len = (b1 >> 4) + 3, disp = (((b1 & 0xF) << 8) | b2) + 1;
                for (uint32_t k = 0; k < len && out < size; k++) {
                    uint8_t v = mem_read8(c, dst + out - disp);
                    if (vram) {
                        if (out & 1) { pending |= (uint16_t)v << 8; mem_write16(c, dst + out - 1, pending); }
                        else pending = v;
                    } else mem_write8(c, dst + out, v);
                    out++;
                }
            } else {
                uint8_t v = mem_read8(c, src++);
                if (vram) {
                    if (out & 1) { pending |= (uint16_t)v << 8; mem_write16(c, dst + out - 1, pending); }
                    else pending = v;
                } else mem_write8(c, dst + out, v);
                out++;
            }
        }
    }
    (void)have;
}

static void rl_uncomp(Cpu *c, int vram) {
    uint32_t src = c->r[0], dst = c->r[1];
    uint32_t size = mem_read32(c, src) >> 8;
    src += 4;
    uint32_t out = 0;
    uint16_t pending = 0;
    while (out < size) {
        uint8_t f = mem_read8(c, src++);
        uint32_t n = (f & 0x7F) + ((f & 0x80) ? 3 : 1);
        uint8_t v = (f & 0x80) ? mem_read8(c, src++) : 0;
        for (uint32_t k = 0; k < n && out < size; k++) {
            uint8_t b = (f & 0x80) ? v : mem_read8(c, src++);
            if (vram) {
                if (out & 1) { pending |= (uint16_t)b << 8; mem_write16(c, dst + out - 1, pending); }
                else pending = b;
            } else mem_write8(c, dst + out, b);
            out++;
        }
    }
}

static void huff_uncomp(Cpu *c) {
    uint32_t src = c->r[0], dst = c->r[1];
    uint32_t header = mem_read32(c, src);
    uint32_t bits = header & 0xF, size = header >> 8;
    uint32_t tree = src + 4;
    uint32_t tree_size = ((uint32_t)mem_read8(c, tree) + 1) * 2;
    uint32_t data = tree + tree_size;
    uint32_t out = 0, node = tree + 1, word = 0, acc = 0;
    int nbits = 0, sym_bits = 0;
    while (out < size) {
        if (nbits == 0) { word = mem_read32(c, data); data += 4; nbits = 32; }
        int bit = (word >> 31) & 1; word <<= 1; nbits--;
        uint8_t n = mem_read8(c, node);
        uint32_t base = node & ~1u;
        uint32_t next = base + ((uint32_t)(n & 0x3F) + 1) * 2 + (bit ? 1 : 0);
        int leaf = bit ? (n & 0x40) : (n & 0x80);
        node = next;
        if (leaf) {
            uint32_t sym = mem_read8(c, node);
            acc |= sym << sym_bits;
            sym_bits += (int)bits;
            if (sym_bits >= 32) {
                mem_write32(c, dst + out, acc);
                out += 4; acc = 0; sym_bits = 0;
            }
            node = tree + 1;
        }
    }
}

static void bit_unpack(Cpu *c) {
    uint32_t src = c->r[0], dst = c->r[1], info = c->r[2];
    uint32_t len = mem_read16(c, info), sw = mem_read8(c, info + 2), dw = mem_read8(c, info + 3);
    uint32_t data_off = mem_read32(c, info + 4);
    uint32_t offset = data_off & 0x7FFFFFFF;
    int zero_flag = (data_off >> 31) & 1;
    uint32_t out = 0; int out_bits = 0;
    for (uint32_t i = 0; i < len; i++) {
        uint8_t b = mem_read8(c, src + i);
        for (uint32_t bit = 0; bit < 8; bit += sw) {
            uint32_t v = (b >> bit) & ((1u << sw) - 1);
            if (v || zero_flag) v += offset;
            out |= v << out_bits;
            out_bits += (int)dw;
            if (out_bits >= 32) { mem_write32(c, dst, out); dst += 4; out = 0; out_bits = 0; }
        }
    }
}

/* CRC-16 du BIOS : polynôme 0xA001, poids faible d'abord, valeur initiale donnée par
   l'appelant. C'est celui dont le firmware protège ses réglages, et celui que le jeu emploie
   pour ses sauvegardes. */
uint16_t bios_crc16(uint16_t crc, const uint8_t *p, size_t n) {
    for (size_t i = 0; i < n; i++) {
        crc ^= p[i];
        for (int k = 0; k < 8; k++)
            crc = (crc & 1) ? (uint16_t)((crc >> 1) ^ 0xA001u) : (uint16_t)(crc >> 1);
    }
    return crc;
}

static void get_crc16(Cpu *c) {
    uint16_t crc = (uint16_t)c->r[0];
    uint32_t addr = c->r[1], len = c->r[2];
    for (uint32_t i = 0; i < len; i++) {
        uint8_t b = mem_read8(c, addr + i);
        crc = bios_crc16(crc, &b, 1);
    }
    c->r[0] = crc;
}

/* ------------------------------------------------------------------ tables sonores du BIOS
 *
 * Le pilote son demande au BIOS du coprocesseur trois tables : un sinus sur un tour, une
 * conversion de hauteur en période, une conversion de décibels en volume. Ces tables sont
 * dans le BIOS, qui n'est pas dans la cartouche et qu'on ne peut donc pas traduire — c'est du
 * matériel, au même titre que les décompresseurs plus haut. On les reconstitue d'après ce
 * qu'elles calculent : une octave vaut 768 pas de hauteur, et la plage de volume couvre
 * 72,3 décibels par dixièmes.
 */
/* SFC_TRACE_TABLES=1 : ce que le pilote son demande aux tables du BIOS. Elles sont
   reconstituées, faute d'avoir le BIOS : voir ce qu'il en fait aide à les juger. */
static int g_trace_tables;

static uint32_t bios_sine_table(uint32_t i) {
    static int16_t table[64];
    static int built;
    if (!built) {
        for (int k = 0; k < 64; k++) table[k] = (int16_t)lround(sin(k * 2.0 * 3.14159265358979323846 / 64.0) * 4096.0);
        built = 1;
    }
    return (uint32_t)(uint16_t)table[i & 63];
}

static uint32_t bios_pitch_table(uint32_t i) {
    static uint16_t table[768];
    static int built;
    if (!built) {
        for (int k = 0; k < 768; k++) {
            double f = 65536.0 * pow(2.0, k / 768.0) - 65536.0;
            table[k] = (uint16_t)(f > 65535.0 ? 65535.0 : lround(f));
        }
        built = 1;
    }
    return table[i < 768 ? i : 767];
}

static uint32_t bios_volume_table(uint32_t i) {
    /* La table convertit une atténuation en volume de canal. Son pas se déduit de ce qu'elle
       couvre : sept bits de volume valent 20·log(127) = 42 décibels de dynamique, et la table
       a 723 entrées — soit un seizième de décibel par entrée, 45 décibels en tout. Un pas plus
       grand la rendrait muette bien avant son extrémité, ce qui s'entend : le pilote demande
       couramment le milieu de la plage. */
    static uint8_t table[724];
    static int built;
    if (!built) {
        for (int k = 0; k < 724; k++) {
            double v = 127.0 * pow(10.0, ((double)k - 723.0) / 16.0 / 20.0);
            long r = lround(v);
            table[k] = (uint8_t)(r < 0 ? 0 : r > 127 ? 127 : r);
        }
        built = 1;
    }
    return table[i < 724 ? i : 723];
}

/* Les appels système du coprocesseur ne portent pas les mêmes numéros que ceux de l'ARM9 :
   là où celui-ci a les décompresseurs et les filtres d'image, celui-là a la mise en veille,
   le décalage de la sortie audio et les trois tables du BIOS dont se sert un pilote son. */
static void rt_swi7(Cpu *c, uint32_t n) {
    switch (n) {
    case 0x03: arm7_spin(c, (uint64_t)c->r[0] * 4u); break;                    /* WaitByLoop */
    case 0x04: intr_wait(c, c->r[0], c->r[1]); break;                          /* IntrWait */
    case 0x05: intr_wait(c, 1, 1); break;                                      /* VBlankIntrWait */
    case 0x06: arm7_halt(c); break;                                            /* Halt */
    case 0x07: arm7_halt(c); break;                                            /* Sleep : rien à éteindre */
    case 0x08:                                                                 /* SoundBias */
        /* Le matériel amène le décalage de la sortie audio vers 0 ou 0x200 par pas ; le
           résultat seul compte, on y va d'un coup. */
        mmio7_poke(0x504, c->r[0] ? 0x200u : 0u);
        break;
    case 0x09: bios_div(c); break;
    case 0x0B: cpu_set(c); break;
    case 0x0C: cpu_fast_set(c); break;
    case 0x0D: bios_sqrt(c); break;
    case 0x0E: get_crc16(c); break;
    case 0x0F: c->r[0] = 0; break;                                             /* IsDebugger */
    case 0x10: bit_unpack(c); break;
    case 0x12: lz77(c, 0); break;
    case 0x13: huff_uncomp(c); break;
    case 0x15: rl_uncomp(c, 0); break;
    case 0x1A: c->r[0] = bios_sine_table(c->r[0]); break;                       /* GetSineTable */
    case 0x1B:                                                                 /* GetPitchTable */
        if (g_trace_tables) { static int n; if (n++ < 60) rt_log("BIOS : hauteur[%u] = %u", c->r[0], bios_pitch_table(c->r[0])); }
        c->r[0] = bios_pitch_table(c->r[0]);
        break;
    case 0x1C:                                                                 /* GetVolumeTable */
        if (g_trace_tables) { static int n; if (n++ < 60) rt_log("BIOS : volume[%u] = %u", c->r[0], bios_volume_table(c->r[0])); }
        c->r[0] = bios_volume_table(c->r[0]);
        break;
    case 0x1F:                                                                 /* CustomHalt */
        if ((c->r[2] & 0xC0u) != 0xC0u) arm7_halt(c);
        else arm7_halt(c);
        break;
    default:
        rt_fatal(c, "appel BIOS svc 0x%X non pris en charge sur le coprocesseur", n);
    }
}

void rt_swi(Cpu *c, uint32_t n) {
    static int init;
    if (!init) { init = 1; g_trace_tables = getenv("SFC_TRACE_TABLES") != NULL; }
    if (n > 0xFF) n = (n >> 16) & 0xFF;
    if (c->is_arm7) { rt_swi7(c, n); return; }
    switch (n) {
    case 0x03:                                                               /* WaitByLoop */
        if (c->is_arm7) arm7_spin(c, (uint64_t)c->r[0] * 4u);
        else hw_advance(c, (uint64_t)c->r[0] * 4u);
        break;
    case 0x04: intr_wait(c, c->r[0], c->r[1]); break;                          /* IntrWait */
    case 0x05: intr_wait(c, 1, 1); break;                                      /* VBlankIntrWait */
    case 0x06: rt_cpu_wait_interrupt(c); break;                                /* Halt */
    case 0x08: bios_sqrt(c); break;
    case 0x09: bios_div(c); break;
    case 0x0B: cpu_set(c); break;
    case 0x0C: cpu_fast_set(c); break;
    case 0x0D: get_crc16(c); break;
    case 0x0E: c->r[0] = 0; break;                                             /* IsDebugger */
    case 0x0F: c->r[0] = 0; break;                                             /* GetSinTable / checksum : non utilisé */
    case 0x10: bit_unpack(c); break;
    case 0x11: lz77(c, 0); break;
    case 0x12: lz77(c, 1); break;
    case 0x13: huff_uncomp(c); break;
    case 0x14: rl_uncomp(c, 0); break;
    case 0x15: rl_uncomp(c, 1); break;
    default:
        rt_fatal(c, "appel BIOS svc 0x%X non pris en charge sur l'ARM9", n);
    }
}

/*
 * Registres matériels vus par le coprocesseur ARM7 (0x04000000–0x04FFFFFF).
 *
 * Le même mot ne désigne pas la même chose des deux côtés : l'ARM9 a le moteur
 * 3D, les deux moteurs 2D et la répartition de la VRAM ; le coprocesseur a les
 * seize canaux sonores, la liaison série qui dessert l'écran tactile, le
 * firmware et l'alimentation, ses propres minuteurs, ses propres canaux de
 * transfert direct et son propre jeu IME/IE/IF. Ils ne partagent que ce que le
 * matériel partage vraiment : le balayage (VCOUNT), les boutons, la mémoire, et
 * les deux bouts de la liaison entre processeurs.
 *
 * Tout ce qui n'a pas d'effet est simplement mémorisé : le pilote relit souvent
 * ce qu'il vient d'écrire.
 *
 * Sous licence MIT.
 */
#include "rt.h"
#include "hw.h"
#include "arm7_run.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint8_t g_io7[0x600];          /* 0x04000000–0x040005FF */

static inline uint32_t io32(uint32_t off) { uint32_t v; memcpy(&v, g_io7 + off, 4); return v; }
static inline void io32w(uint32_t off, uint32_t v) { memcpy(g_io7 + off, &v, 4); }

uint32_t mmio7_peek(uint32_t off) { return off < sizeof g_io7 - 3 ? io32(off & ~3u) : 0; }
void mmio7_poke(uint32_t off, uint32_t v) { if (off < sizeof g_io7 - 3) io32w(off & ~3u, v); }

/* ------------------------------------------------------------------ liaison série (SPI)
 *
 * Un octet par transfert, le périphérique choisi par les bits 8-9 de SPICNT :
 * 0 l'alimentation, 1 le firmware, 2 le convertisseur de l'écran tactile. Le
 * bit 11 (« garder la sélection ») dit qu'un autre octet suit ; à sa retombée
 * la transaction se termine et l'état du périphérique repart de zéro.
 */
enum { SPI_POWER = 0, SPI_FIRMWARE = 1, SPI_TOUCH = 2 };

static struct {
    int device;                /* périphérique de la transaction en cours */
    int index;                 /* rang de l'octet dans la transaction */
    uint32_t cmd;              /* premier octet */
    uint32_t addr;             /* adresse accumulée (firmware) */
    uint32_t pending;          /* valeur que l'écran tactile va rendre */
    uint8_t out;               /* octet à lire en retour */
} g_spi;

static uint8_t spi_touch(uint8_t v) {
    /* L'octet de commande porte le bit 7, le canal en bits 6-4 et le mode en bit 3 ; il ne rend
       rien lui-même. Ce sont les deux octets suivants qui sortent la mesure, douze bits cadrés
       à gauche sur seize, poids fort d'abord. */
    if (v & 0x80) {
        g_spi.pending = (arm7_touch_channel((v >> 4) & 7u) & 0xFFFu) << 3;
        g_spi.index = 0;
        return 0;
    }
    uint8_t out = (uint8_t)(g_spi.pending >> 8);
    g_spi.pending = (g_spi.pending << 8) & 0xFFFFu;
    return out;
}

static uint8_t spi_firmware(uint8_t v) {
    switch (g_spi.index) {
    case 0: g_spi.cmd = v; g_spi.addr = 0; return 0;
    case 1: case 2: case 3:
        if (g_spi.cmd == 0x03) { g_spi.addr = (g_spi.addr << 8) | v; return 0; }
        return 0;
    default:
        if (g_spi.cmd == 0x03) return arm7_firmware_byte(g_spi.addr++);
        if (g_spi.cmd == 0x05) return 0;             /* état : jamais occupé */
        return 0;
    }
}

static uint8_t spi_power(uint8_t v) {
    /* Un octet de commande (bit 7 : lecture), puis la donnée. Le pilote n'y écrit que le
       rétroéclairage et l'arrêt ; rien de tout cela ne se voit d'ici. */
    static uint8_t reg[4];
    if (g_spi.index == 0) { g_spi.cmd = v; return 0; }
    uint32_t idx = g_spi.cmd & 3u;
    if (g_spi.cmd & 0x80) return reg[idx];
    reg[idx] = v;
    return 0;
}

static void spi_write_data(Cpu *c, uint8_t v) {
    uint32_t cnt = io32(0x1C0);
    if (!(cnt & 0x8000)) return;                     /* liaison éteinte */
    int dev = (int)((cnt >> 8) & 3u);
    if (dev != g_spi.device) { g_spi.device = dev; g_spi.index = 0; }
    switch (dev) {
    case SPI_TOUCH:    g_spi.out = spi_touch(v); break;
    case SPI_FIRMWARE: g_spi.out = spi_firmware(v); break;
    default:           g_spi.out = spi_power(v); break;
    }
    static int trace = -1;
    if (trace < 0) trace = getenv("SFC_TRACE_SPI") != NULL;
    if (trace) { static int n; if (n++ < 300 || dev == SPI_TOUCH)
        rt_log("SPI : périphérique %d, octet %d : %02X -> %02X", dev, g_spi.index, v, g_spi.out); }
    g_spi.index++;
    if (!(cnt & 0x0800)) g_spi.index = 0;            /* sélection relâchée : transaction close */
    /* le transfert est instantané : le bit « occupé » ne se lève jamais */
    if (cnt & 0x4000) arm7_raise_irq(c, IRQ7_SPI);
}

/* ------------------------------------------------------------------ transferts directs */
static void dma7_run(Cpu *c, int ch) {
    uint32_t base = 0xB0 + ch * 12;
    uint32_t src = io32(base) & 0x0FFFFFFF, dst = io32(base + 4) & 0x0FFFFFFF;
    uint32_t cnt = io32(base + 8);
    uint32_t count = cnt & 0xFFFF;                   /* les canaux du coprocesseur : 16 bits */
    if (count == 0) count = (ch == 3) ? 0x10000u : 0x4000u;
    int wide = (cnt >> 26) & 1;
    uint32_t dst_mode = (cnt >> 21) & 3u, src_mode = (cnt >> 23) & 3u;
    int step = wide ? 4 : 2;
    for (uint32_t i = 0; i < count; i++) {
        if (wide) mem_write32(c, dst, mem_read32(c, src));
        else mem_write16(c, dst, mem_read16(c, src));
        dst += dst_mode == 0 ? step : dst_mode == 1 ? -step : 0;
        src += src_mode == 0 ? step : src_mode == 1 ? -step : 0;
    }
    if (!(cnt & 0x02000000u)) io32w(base + 8, cnt & ~0x80000000u);   /* pas de répétition : terminé */
    if (cnt & 0x40000000u) arm7_raise_irq(c, IRQ7_DMA0 + ch);
}

/* Les modes déclenchés : 1 au VBlank, 2 pour la file d'un canal sonore. */
void dma7_trigger(Cpu *c, int mode) {
    for (int ch = 0; ch < 4; ch++) {
        uint32_t cnt = io32(0xB0 + ch * 12 + 8);
        if (!(cnt & 0x80000000u)) continue;
        if ((int)((cnt >> 28) & 3u) == mode) dma7_run(c, ch);
    }
}

/* La carte n'a qu'un seul jeu de registres pour les deux processeurs : celui qui en a la main
   (EXMEMCNT en décide) parle au même matériel. On délègue donc à l'espace de l'ARM9 plutôt que
   d'en tenir une copie ici, ce qui laisserait card.c aveugle à ce que le coprocesseur demande. */
static int card_register(uint32_t off) { return off >= 0x1A0 && off < 0x1B0; }

/* ------------------------------------------------------------------ lecture */
static uint32_t read_reg(Cpu *c, uint32_t off, uint32_t size) {
    (void)size;
    if (card_register(off)) return mmio_read32(c, 0x04000000u + off);
    if (off >= 0x400 && off < 0x520) return snd_read(c, off);
    switch (off) {
    case 0x004: {   /* DISPSTAT : ses masques, l'état du balayage est commun */
        uint32_t line = hw_vcount();
        return (io32(0x004) & 0xFFFFFFC7u) | hw_dispstat_bits() | (line << 16);
    }
    case 0x006: return hw_vcount();
    case 0x130: return hw_keyinput() | (io32(0x130) & 0xFFFF0000u);
    /* EXTKEYIN est en 0x136, dans le mot de RCNT : les boutons X et Y, le stylet posé
       (bit 6) et le couvercle, que le coprocesseur seul peut lire. */
    case 0x134: return (io32(0x134) & 0xFFFFu) | (hw_extkeys() << 16);
    case 0x180: return ipc7_read_sync(c);
    case 0x184: return ipc7_read_fifocnt(c);
    case 0x1C0: return io32(0x1C0) | ((uint32_t)g_spi.out << 16);
    case 0x204: return io32(0x204);
    case 0x240: return (io32(0x240) & 0xFFFFFF00u) | (mmio_peek(0x247) & 3u) << 8;  /* WRAMSTAT en 0x241 */
    case 0x300: return io32(0x300);
    default: break;
    }
    if (off >= 0x100 && off < 0x110) return arm7_timer_read(off);
    return io32(off & ~3u);
}

/* ------------------------------------------------------------------ écriture */
static void write_reg(Cpu *c, uint32_t off, uint32_t v, uint32_t size) {
    if (off >= 0x400 && off < 0x520) { snd_write(c, off, v, size); return; }
    if (card_register(off)) { mmio_write32(c, 0x04000000u + off, v); return; }
    uint32_t aligned = off & ~3u;
    switch (aligned) {
    case 0x0B8: case 0x0C4: case 0x0D0: case 0x0DC: {
        uint32_t old = io32(aligned);
        io32w(aligned, v);
        if ((v & 0x80000000u) && !(old & 0x80000000u) && ((v >> 28) & 3u) == 0) dma7_run(c, (int)((aligned - 0xB8) / 12));
        return;
    }
    case 0x100: case 0x104: case 0x108: case 0x10C:
        /* Le pilote écrit la recharge puis le contrôle en deux demi-mots. Les accès partiels se
           recomposent sur le mot rangé ici : il faut donc l'y garder, sinon la seconde écriture
           efface la première et tous les minuteurs repartent d'une recharge nulle. */
        io32w(aligned, v);
        arm7_timer_write(c, aligned, v);
        return;
    case 0x180: ipc7_write_sync(c, v); return;
    case 0x184: ipc7_write_fifocnt(c, v); return;
    case 0x188: ipc7_write_fifosend(c, v); return;
    case 0x1C0:
        io32w(0x1C0, v & 0xCF03u);
        if (size == 4) spi_write_data(c, (uint8_t)(v >> 16));
        return;
    case 0x208: io32w(0x208, v & 1u); arm7_check_irq(c); return;
    case 0x210: io32w(0x210, v); arm7_check_irq(c); return;
    case 0x214: io32w(0x214, io32(0x214) & ~v); arm7_check_irq(c); return;
    case 0x300:
        /* POSTFLG en 0x300, HALTCNT en 0x301 : 0x80 met le processeur en attente
           d'interruption, 0xC0 endort la console. L'octet haut n'est touché que si
           l'écriture le couvre — `size` est le nombre d'octets écrits depuis 0x300. */
        io32w(0x300, v & 0xFFFFu);
        if (size >= 2 && ((v >> 8) & 0xC0u) == 0x80u) arm7_halt(c);
        return;
    default: break;
    }
    io32w(aligned, v);
}

/* ------------------------------------------------------------------ accès partiels */
uint32_t mmio7_read32(Cpu *c, uint32_t addr) {
    g_acces_mmio++;      /* le banc ne rejoue pas le matériel (mmio.c) */
    if (g_mmio_bouchon) return (uint32_t)g_mmio_bouchon(c, addr, 0, 4, 0);
    uint32_t off = addr & 0x00FFFFFFu;
    if (addr == 0x04100000u) return ipc7_read_fiforecv(c);
    if (addr == 0x04100010u) return card_read_data(c);
    if (off >= sizeof g_io7) return 0;
    return read_reg(c, off & ~3u, 4);
}
uint16_t mmio7_read16(Cpu *c, uint32_t addr) {
    g_acces_mmio++;      /* le banc ne rejoue pas le matériel (mmio.c) */
    if (g_mmio_bouchon) return (uint16_t)g_mmio_bouchon(c, addr, 0, 2, 0);
    uint32_t off = addr & 0x00FFFFFFu;
    if (off >= sizeof g_io7) return 0;
    if (card_register(off)) return mmio_read16(c, addr);
    uint32_t v = read_reg(c, off & ~3u, 2);
    return (uint16_t)(v >> ((off & 2u) * 8u));
}
uint8_t mmio7_read8(Cpu *c, uint32_t addr) {
    g_acces_mmio++;      /* le banc ne rejoue pas le matériel (mmio.c) */
    if (g_mmio_bouchon) return (uint8_t)g_mmio_bouchon(c, addr, 0, 1, 0);
    uint32_t off = addr & 0x00FFFFFFu;
    if (off >= sizeof g_io7) return 0;
    if (card_register(off)) return mmio_read8(c, addr);
    uint32_t v = read_reg(c, off & ~3u, 1);
    return (uint8_t)(v >> ((off & 3u) * 8u));
}
void mmio7_write32(Cpu *c, uint32_t addr, uint32_t v) {
    g_acces_mmio++;      /* le banc ne rejoue pas le matériel (mmio.c) */
    if (g_mmio_bouchon) { g_mmio_bouchon(c, addr, v, 4, 1); return; }
    uint32_t off = addr & 0x00FFFFFFu;
    if (off >= sizeof g_io7) return;
    write_reg(c, off & ~3u, v, 4);
}
void mmio7_write16(Cpu *c, uint32_t addr, uint16_t v) {
    g_acces_mmio++;      /* le banc ne rejoue pas le matériel (mmio.c) */
    if (g_mmio_bouchon) { g_mmio_bouchon(c, addr, v, 2, 1); return; }
    uint32_t off = addr & 0x00FFFFFFu;
    if (off >= sizeof g_io7) return;
    uint32_t aligned = off & ~3u;
    if (card_register(off)) { mmio_write16(c, 0x04000000u + off, (uint16_t)v); return; }
    if (off == 0x1C2) { spi_write_data(c, (uint8_t)v); return; }        /* SPIDATA seul */
    if (off == 0x300) { write_reg(c, 0x300, (io32(0x300) & 0xFFFF0000u) | v, 2); return; }
    uint32_t shift = (off & 2u) * 8u;
    uint32_t merged = (io32(aligned) & ~(0xFFFFu << shift)) | ((uint32_t)v << shift);
    write_reg(c, aligned, merged, shift ? 1 : 2);   /* l'octet 0x301 n'est couvert qu'en 0x300 */
}
void mmio7_write8(Cpu *c, uint32_t addr, uint8_t v) {
    g_acces_mmio++;      /* le banc ne rejoue pas le matériel (mmio.c) */
    if (g_mmio_bouchon) { g_mmio_bouchon(c, addr, v, 1, 1); return; }
    uint32_t off = addr & 0x00FFFFFFu;
    if (off >= sizeof g_io7) return;
    uint32_t aligned = off & ~3u;
    if (card_register(off)) { mmio_write8(c, 0x04000000u + off, v); return; }
    if (off == 0x1C2) { spi_write_data(c, v); return; }
    if (off == 0x301) {                                  /* HALTCNT */
        g_io7[0x301] = v;
        if ((v & 0xC0u) == 0x80u) arm7_halt(c);
        return;
    }
    uint32_t shift = (off & 3u) * 8u;
    uint32_t merged = (io32(aligned) & ~(0xFFu << shift)) | ((uint32_t)v << shift);
    write_reg(c, aligned, merged, 1);
}

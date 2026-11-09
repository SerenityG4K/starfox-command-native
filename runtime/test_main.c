/*
 * Banc d'essai différentiel : exécute des blocs recompilés sur commande.
 *
 * Protocole (texte, une commande par ligne sur stdin) :
 *   load <fichier>            charge une image de la RAM principale (4 Mo)
 *   itcm <fichier>            charge l'ITCM (32 Ko)
 *   regs r0 … r15 n z c v q   fixe les registres et les drapeaux (hex)
 *   run <index>               exécute le bloc de test numéro <index>
 *   dump                      écrit « regs … » puis « hash <fnv de la RAM> »
 *   quit
 *
 * Les blocs de test sont générés dans generated/test_blocks.c :
 *   void t_<index>(Cpu *c) ; extern const TestBlockFn g_test_blocks[] ; extern size_t g_test_block_count.
 * Sous licence MIT.
 */
#include "cpu.h"
#include "memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void (*TestBlockFn)(Cpu *c);
extern const TestBlockFn g_test_blocks[];
extern const size_t g_test_block_count;

/* couche d'exécution minimale : tout ce qui touche au flot ou au système est une erreur ici */
static void fail(const char *what, uint32_t a, uint32_t b) {
    printf("fail %s %08X %08X\n", what, a, b);
    fflush(stdout);
}
void rt_call(Cpu *c, uint32_t t) { (void)c; fail("rt_call", t, 0); }
void rt_jump(Cpu *c, uint32_t t) { (void)c; fail("rt_jump", t, 0); }
void rt_return_from_exception(Cpu *c) { (void)c; fail("rfe", 0, 0); }
void rt_swi(Cpu *c, uint32_t n) { (void)c; fail("swi", n, 0); }
void rt_bkpt(Cpu *c, uint32_t imm, uint32_t addr) { (void)c; fail("bkpt", imm, addr); }
void rt_undefined(Cpu *c, uint32_t addr, uint32_t raw) { (void)c; fail("undef", addr, raw); }
void rt_cp15_write(Cpu *c, uint32_t a, uint32_t b, uint32_t d, uint32_t e, uint32_t v) { (void)c; (void)a; (void)b; (void)d; (void)e; (void)v; }
uint32_t rt_cp15_read(Cpu *c, uint32_t a, uint32_t b, uint32_t d, uint32_t e) { (void)c; (void)a; (void)b; (void)d; (void)e; return 0; }
uint32_t cpsr_read(Cpu *c) {
    return (c->nf << 31) | (c->zf << 30) | (c->cf << 29) | (c->vf << 28) | (c->qf << 27)
         | (c->irq_off << 7) | (c->fiq_off << 6) | (c->thumb << 5) | c->mode;
}
void cpsr_write(Cpu *c, uint32_t v, uint32_t fields) {
    if (fields & 8) { c->nf = v >> 31; c->zf = (v >> 30) & 1; c->cf = (v >> 29) & 1; c->vf = (v >> 28) & 1; c->qf = (v >> 27) & 1; }
    if (fields & 1) { c->irq_off = (v >> 7) & 1; c->fiq_off = (v >> 6) & 1; c->mode = v & 0x1F; }
}
uint32_t spsr_read(Cpu *c) { return c->spsr; }
void spsr_write(Cpu *c, uint32_t v, uint32_t fields) { (void)fields; c->spsr = v; }
void cpu_switch_mode(Cpu *c, uint32_t mode) { c->mode = mode; }
void rt_trace(Cpu *c, uint32_t addr) { (void)c; (void)addr; }
uint32_t mmio_read32(Cpu *c, uint32_t a) { (void)c; return a * 0x9E3779B1u; }
uint16_t mmio_read16(Cpu *c, uint32_t a) { (void)c; return (uint16_t)(a * 0x9E3779B1u); }
uint8_t mmio_read8(Cpu *c, uint32_t a) { (void)c; return (uint8_t)(a * 0x9E3779B1u); }
void mmio_write32(Cpu *c, uint32_t a, uint32_t v) { (void)c; (void)a; (void)v; }
void mmio_write16(Cpu *c, uint32_t a, uint16_t v) { (void)c; (void)a; (void)v; }
void mmio_write8(Cpu *c, uint32_t a, uint8_t v) { (void)c; (void)a; (void)v; }
/* Ceux du coprocesseur, et l'espace de registres que sa carte mémoire consulte pour WRAMCNT :
   le banc ne compare que des blocs de calcul, aucun n'a besoin du vrai matériel. */
uint8_t g_io[0x2000];
uint32_t mmio7_read32(Cpu *c, uint32_t a) { (void)c; return a * 0x9E3779B1u; }
uint16_t mmio7_read16(Cpu *c, uint32_t a) { (void)c; return (uint16_t)(a * 0x9E3779B1u); }
uint8_t mmio7_read8(Cpu *c, uint32_t a) { (void)c; return (uint8_t)(a * 0x9E3779B1u); }
void mmio7_write32(Cpu *c, uint32_t a, uint32_t v) { (void)c; (void)a; (void)v; }
void mmio7_write16(Cpu *c, uint32_t a, uint16_t v) { (void)c; (void)a; (void)v; }
void mmio7_write8(Cpu *c, uint32_t a, uint8_t v) { (void)c; (void)a; (void)v; }
uint32_t mem_unmapped_read(Cpu *c, uint32_t a, uint32_t s) { (void)c; (void)s; return a * 0x85EBCA6Bu; }
void mem_unmapped_write(Cpu *c, uint32_t a, uint32_t s, uint32_t v) { (void)c; (void)a; (void)s; (void)v; }

/* CRC-32 (polynôme 0xEDB88320, comme zlib) */
static uint32_t crc_table[256];
static void crc_init(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        crc_table[i] = c;
    }
}
static uint32_t crc32_of(const uint8_t *p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) c = crc_table[(c ^ p[i]) & 0xFFu] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

static int load_file(const char *path, uint8_t *dst, size_t n) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    size_t got = fread(dst, 1, n, f);
    fclose(f);
    return got == n;
}

int main(void) {
    static Cpu cpu;
    Cpu *c = &cpu;
    mem_init();
    crc_init();
    char line[1024];
    /* sur Windows, un tube ne connaît pas le mode ligne : pas de tampon du tout */
    setvbuf(stdout, NULL, _IONBF, 0);
    while (fgets(line, sizeof line, stdin)) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        if (!strncmp(line, "load ", 5)) {
            printf(load_file(line + 5, g_main_ram, 0x400000) ? "ok\n" : "err\n");
        } else if (!strncmp(line, "itcm ", 5)) {
            printf(load_file(line + 5, g_itcm, 0x8000) ? "ok\n" : "err\n");
        } else if (!strncmp(line, "wram ", 5)) {
            /* la mémoire du coprocesseur : 32 Ko partagés puis ses 64 Ko propres, d'un bloc */
            static uint8_t buf[0x18000];
            int ok = load_file(line + 5, buf, sizeof buf);
            if (ok) { memcpy(g_shared_wram, buf, 0x8000); memcpy(g_wram7, buf + 0x8000, 0x10000); }
            printf(ok ? "ok\n" : "err\n");
        } else if (!strncmp(line, "regs ", 5)) {
            unsigned v[21];
            int n = 0;
            char *p = line + 5;
            while (n < 21 && *p) { v[n++] = (unsigned)strtoul(p, &p, 16); }
            for (int i = 0; i < 16; i++) c->r[i] = v[i];
            c->nf = v[16]; c->zf = v[17]; c->cf = v[18]; c->vf = v[19]; c->qf = v[20];
            printf("ok\n");
        } else if (!strncmp(line, "run ", 4)) {
            size_t idx = strtoul(line + 4, NULL, 10);
            if (idx < g_test_block_count) { g_test_blocks[idx](c); printf("ok\n"); }
            else printf("err\n");
        } else if (!strcmp(line, "dump")) {
            printf("regs");
            for (int i = 0; i < 16; i++) printf(" %08X", c->r[i]);
            printf(" %u %u %u %u %u\n", c->nf, c->zf, c->cf, c->vf, c->qf);
            printf("hash %08X %08X\n", crc32_of(g_main_ram, 0x400000), crc32_of(g_itcm, 0x8000));
        } else if (!strcmp(line, "quit")) {
            break;
        } else {
            printf("err\n");
        }
    }
    return 0;
}

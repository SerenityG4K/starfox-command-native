#!/usr/bin/env python3
"""Vérifie les retours du gestionnaire IRQ ARM9 contre le vrai runtime/irq.c.

Le gestionnaire simulé représente un PUSH {lr}/POP {pc} recompilé. Il vérifie
que le BIOS fournit une adresse de retour reconnue par la pile fantôme, puis
laisse son PC temporaire derrière lui comme le ferait le code recompilé.
Les autres services matériels sont des doublures ; aucun binaire du jeu n'est
nécessaire. Usage : python tools/recomp/test_irq_return.py [--compiler gcc]
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]

HARNESS = r'''
#include "hw.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned char ram[0x400000], dtcm[0x4000];
uint8_t *g_main_ram = ram;
int g_check_sp;
static uint32_t ime, ie, pending;
static unsigned calls;
static int resume_other_context;
static uint32_t expected_pc;
enum { HANDLER = 0x02010000u, RESUMED_PC = 0x020ABCDEu };

uint32_t mmio_peek(uint32_t off) {
    switch (off) {
    case 0x208: return ime;
    case 0x210: return ie;
    case 0x214: return pending;
    default: assert(!"unexpected MMIO read"); return 0;
    }
}
uint32_t mem_read32_slow(Cpu *c, uint32_t addr) {
    (void)c;
    assert(addr >= 0x027E0000u && addr <= 0x027E3FFCu);
    uint32_t value;
    memcpy(&value, dtcm + addr - 0x027E0000u, 4);
    return value;
}
void mem_write32_slow(Cpu *c, uint32_t addr, uint32_t value) {
    (void)c;
    assert(addr >= 0x027E0000u && addr <= 0x027E3FFCu);
    memcpy(dtcm + addr - 0x027E0000u, &value, 4);
}
void rt_sp_mismatch(Cpu *c, uint32_t ret, uint32_t before) {
    (void)c; (void)ret; (void)before;
    assert(!"stack mismatch");
}
void cpu_switch_mode(Cpu *c, uint32_t mode) {
    if (c->mode == mode) return;
    assert((mode == MODE_SYS || mode == MODE_IRQ) &&
           (c->mode == MODE_SYS || c->mode == MODE_IRQ));
    uint32_t *old = c->mode == MODE_IRQ ? c->bank_irq : c->bank_usr;
    uint32_t *next = mode == MODE_IRQ ? c->bank_irq : c->bank_usr;
    old[0] = c->r[13]; old[1] = c->r[14];
    if (c->mode == MODE_IRQ) old[2] = c->spsr;
    c->r[13] = next[0]; c->r[14] = next[1];
    c->spsr = mode == MODE_IRQ ? next[2] : 0;
    c->mode = mode;
}
uint32_t cpsr_read(Cpu *c) {
    return c->mode | (c->thumb << 5) | (c->fiq_off << 6) |
           (c->irq_off << 7) | (c->qf << 27) | (c->vf << 28) |
           (c->cf << 29) | (c->zf << 30) | (c->nf << 31);
}
void cpsr_write(Cpu *c, uint32_t value, uint32_t fields) {
    assert(fields == 15);
    cpu_switch_mode(c, value & 31);
    c->thumb = (value >> 5) & 1; c->fiq_off = (value >> 6) & 1;
    c->irq_off = (value >> 7) & 1; c->qf = (value >> 27) & 1;
    c->vf = (value >> 28) & 1; c->cf = (value >> 29) & 1;
    c->zf = (value >> 30) & 1; c->nf = value >> 31;
}
void rt_call(Cpu *c, uint32_t target) {
    assert(target == HANDLER);
    assert(c->mode == MODE_IRQ && c->in_irq && c->irq_off);
    assert(c->depth == 2 && c->shadow[0] == 0x02007777u);
    /* POP pc must be recognized as a return, not dispatched as guest code. */
    assert((c->r[14] & ~1u) == (c->shadow[c->depth - 1] & ~1u));
    /* The exception's LR remains saved separately for resuming the thread. */
    assert(mem_read32(c, c->r[13] + 20) == expected_pc + 4);
    ++calls;
    pending = 0;
    c->r[0] = 0xDEADBEEFu; c->r[1] = 0x10203040u;
    c->r[2] = 0xAABBCCDDu; c->r[3] = 0x99887766u;
    c->r[12] = 0xCAFE0000u;
    if (resume_other_context) {
        c->r[15] = RESUMED_PC;
        c->spsr = MODE_SYS | (1u << 30);
        c->irq_resumed = 1;
    } else {
        c->r[15] = c->r[14];
    }
}

int main(void) {
    for (int resumed = 0; resumed < 2; ++resumed) {
        Cpu c = {0};
        c.mode = MODE_SYS;
        c.thumb = 1; c.cf = 1; c.nf = 1;
        for (unsigned i = 0; i < 13; ++i) c.r[i] = 0x1000u + i;
        c.r[13] = 0x02030000u; c.r[14] = 0x02001235u;
        c.r[15] = expected_pc = 0x02005678u;
        c.bank_irq[0] = 0x027E3F80u;
        c.depth = 1; c.shadow[0] = 0x02007777u;
        uint32_t before_cpsr = cpsr_read(&c);
        mem_write32(&c, 0x027E3FFCu, HANDLER);
        ime = ie = pending = 1;
        resume_other_context = resumed;
        hw_deliver_pending(&c);
        assert(c.mode == MODE_SYS && !c.in_irq && !c.irq_resumed);
        assert(c.depth == 1 && c.shadow[0] == 0x02007777u);
        assert(c.r[13] == 0x02030000u && c.r[14] == 0x02001235u);
        assert(c.bank_irq[0] == 0x027E3F80u);
        if (resumed) {
            assert(c.r[15] == RESUMED_PC);
            assert(c.r[0] == 0xDEADBEEFu && c.r[12] == 0xCAFE0000u);
            assert(cpsr_read(&c) == (MODE_SYS | (1u << 30)));
        } else {
            assert(c.r[15] == expected_pc);
            for (unsigned i = 0; i < 13; ++i) assert(c.r[i] == 0x1000u + i);
            assert(cpsr_read(&c) == before_cpsr);
        }
        assert(g_irq_line == 0);
        unsigned before_calls = calls;
        hw_deliver_pending(&c);
        assert(calls == before_calls);
    }
    puts("IRQ return: sentinel, interrupted PC, resumed context and stack OK");
    return 0;
}
'''


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default=os.environ.get("CC", "gcc"))
    args = parser.parse_args()
    build = ROOT / "build" / "test_irq_return"
    build.mkdir(parents=True, exist_ok=True)
    source = build / "test_irq_return.c"
    source.write_text(HARNESS, encoding="utf-8")
    executable = build / ("test_irq_return.exe" if os.name == "nt" else "test_irq_return")
    subprocess.run([
        args.compiler, "-std=c11", "-O1", "-flto", "-DSFC_NO_WATCH_VALUE",
        "-ffunction-sections", "-fdata-sections", "-I", str(ROOT / "runtime"),
        str(source), str(ROOT / "runtime" / "irq.c"),
        "-Wl,--gc-sections", "-o", str(executable),
    ], check=True)
    subprocess.run([str(executable)], check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Régression des sauts de queue via POP/LDM PC, sans ROM ni dépendance externe.

Décode des petites fonctions synthétiques, traduit avec l'émetteur courant,
compile et exécute le C avec cpu.h et le vrai rt_jump de runtime/rt.c.
Le dispatch instrumenté vérifie le rappel, ses arguments et la pile fantôme.

Usage : python tools/recomp/test_return_dispatch.py [--compiler gcc]
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from recomp.armdec import decode_arm
from recomp.emit import Context, Emitter
from recomp.insn import ARM, THUMB
from recomp.thumbdec import decode_thumb

ROOT = Path(__file__).resolve().parents[2]

# Instructions synthétiques : le thunk conserve r2, ajuste this de -12 puis
# pousse la cible sur la pile. Son POP PC doit appeler cette cible une fois.
PROGRAMS = {
    "caller": (0x02000080, THUMB, [0xB510, 0xF000, 0xF83D, 0x2407, 0x6044, 0xBD10]),
    "thunk": (0x02000100, THUMB, [0xB404, 0x380C, 0xBC04, 0x4694, 0x4A01, 0xB404, 0x4662, 0xBD00]),
    "thumb_return": (0x02000200, THUMB, [0xB510, 0x2466, 0xBD10]),
    "callback": (0x02000300, THUMB, [0xB510, 0x2301, 0x6003, 0xBD10]),
    "arm_return": (0x02000400, ARM, [0xE92D4010, 0xE3A04066, 0xE8BD8010]),
    "arm_tail": (0x02000500, ARM, [0xE92D0004, 0xE8BD8000]),
}


def production_jump() -> str:
    """Garde le code de retour réel ; seul le choix de fonction est remplacé."""
    text = (ROOT / "runtime/rt.c").read_text(encoding="utf-8")
    start = text.index("void rt_jump(Cpu *c, uint32_t target) {")
    depth = 0
    for end in range(text.index("{", start), len(text)):
        if text[end] == "{":
            depth += 1
        elif text[end] == "}":
            depth -= 1
            if depth == 0:
                return text[start:end + 1]
    raise ValueError("rt_jump incomplet")


def emitted_programs() -> str:
    symbols = {addr: name for name, (addr, _, _) in PROGRAMS.items()}
    result = []
    for name, (addr, mode, words) in PROGRAMS.items():
        insns = []
        k = 0
        while k < len(words):
            if mode == THUMB:
                nxt = words[k + 1] if k + 1 < len(words) else None
                insn = decode_thumb(words[k], addr + k * 2, nxt)
                k += insn.size // 2
            else:
                insn = decode_arm(words[k], addr + k * 4)
                k += 1
            assert insn.op != "UNDEF", insn
            insns.append(insn)
        ctx = Context(labels={i.addr for i in insns}, symbol=lambda a, *_: symbols.get(a), mode=mode)
        emitter = Emitter(ctx)
        lines = [f"static void {name}(Cpu *c, uint32_t entry) {{", "    (void)entry;",
                 "    uint32_t res, op2, sc, ea, val; uint64_t wide;",
                 "    (void)res; (void)op2; (void)sc; (void)ea; (void)val; (void)wide;"]
        for insn in insns:
            lines.append(f"L_{insn.addr:08X}: ;")
            lines.extend("    " + line for line in emitter.emit(insn))
        lines.extend(["    return;", "redirect_dispatch:", '    assert(!"unexpected thread redirect");', "}"])
        result.append("\n".join(lines))
    return "\n\n".join(result)


HARNESS = r'''
#define SFC_NO_WATCH_VALUE
#include "cpu.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t ram[0x400000];
uint8_t *g_main_ram = ram;
int g_check_sp = 1;
static unsigned callbacks, dispatches;
static uint32_t callback_r0, callback_r2, callback_sp, callback_depth;

void rt_sp_mismatch(Cpu *c, uint32_t ret, uint32_t expected) {
    (void)c; (void)ret; (void)expected; assert(!"unbalanced guest stack");
}
uint32_t mem_read32_slow(Cpu *c, uint32_t addr) {
    (void)c; (void)addr; assert(!"read outside test RAM"); return 0;
}
void mem_write32_slow(Cpu *c, uint32_t addr, uint32_t val) {
    (void)c; (void)addr; (void)val; assert(!"write outside test RAM");
}

@PROTOTYPES@

static void dispatch(Cpu *c, uint32_t target) {
    dispatches++;
    c->thumb = target & 1u;
    if (target == 0x02000301u) {
        callbacks++;
        callback_r0 = c->r[0]; callback_r2 = c->r[2];
        callback_sp = c->r[13]; callback_depth = c->depth;
        callback(c, target & ~1u);
        return;
    }
    fprintf(stderr, "unexpected dispatch %08X (depth %u)\n", target, c->depth);
    assert(!"a genuine function return was dispatched as a new call");
}
void rt_call(Cpu *c, uint32_t target) { dispatch(c, target); }

@RT_JUMP@

@PROGRAMS@

static Cpu initial(void) {
    Cpu c = {0};
    memset(ram, 0, sizeof ram);
    callbacks = dispatches = 0;
    callback_r0 = callback_r2 = callback_sp = callback_depth = 0;
    c.mode = MODE_SYS; c.thumb = 1;
    c.r[0] = 0x0200100Cu; c.r[2] = 0x55667788u;
    c.r[4] = 0x12345678u; c.r[13] = 0x02003F00u;
    c.r[14] = 0x02000801u;
    mem_write32(&c, 0x02000110u, 0x02000301u);
    return c;
}

int main(void) {
    Cpu c = initial();
    RT_CALL(&c, 0x02000801u, thunk(&c, 0x02000100u));
    assert(callbacks == 1 && dispatches == 1);
    assert(callback_r0 == 0x02001000u && callback_r2 == 0x55667788u);
    assert(callback_sp == 0x02003F00u && callback_depth == 1);
    assert(mem_read32(&c, 0x02001000u) == 1);
    assert(c.r[13] == 0x02003F00u && c.r[4] == 0x12345678u && c.depth == 0);
    assert(c.r[15] == 0x02000801u);
    puts("PASS Thumb this-adjust thunk dispatches callback and preserves arguments");

    c = initial();
    RT_CALL(&c, 0x02000801u, thumb_return(&c, 0x02000200u));
    assert(dispatches == 0 && c.r[4] == 0x12345678u);
    assert(c.r[13] == 0x02003F00u && c.r[15] == 0x02000801u && c.depth == 0);
    puts("PASS ordinary Thumb POP PC returns without dispatch");

    c = initial(); c.thumb = 0; c.r[14] = 0x02000800u;
    RT_CALL(&c, 0x02000800u, arm_return(&c, 0x02000400u));
    assert(dispatches == 0 && c.r[4] == 0x12345678u);
    assert(c.r[13] == 0x02003F00u && c.r[15] == 0x02000800u && c.depth == 0);
    puts("PASS ordinary ARM LDM SP PC returns without dispatch");

    c = initial(); c.thumb = 0; c.r[0] -= 12; c.r[2] = 0x02000301u;
    RT_CALL(&c, 0x02000801u, arm_tail(&c, 0x02000500u));
    assert(callbacks == 1 && dispatches == 1 && mem_read32(&c, 0x02001000u) == 1);
    assert(c.r[13] == 0x02003F00u && c.r[15] == 0x02000801u && c.depth == 0);
    puts("PASS ARM LDM SP PC tail branch enters Thumb callback");

    c = initial();
    RT_CALL(&c, 0x02000801u, caller(&c, 0x02000080u));
    assert(callbacks == 1 && dispatches == 1);
    assert(callback_r0 == 0x02001000u && callback_r2 == 0x55667788u);
    assert(callback_sp == 0x02003EF8u && callback_depth == 2);
    assert(mem_read32(&c, 0x02001000u) == 1 && mem_read32(&c, 0x02001004u) == 7);
    assert(c.r[4] == 0x12345678u && c.r[13] == 0x02003F00u);
    assert(c.r[15] == 0x02000801u && c.depth == 0);
    puts("PASS nested BL resumes caller once after the thunk callback");
    return 0;
}
'''


def source() -> str:
    prototypes = "\n".join(f"static void {name}(Cpu *, uint32_t);" for name in PROGRAMS)
    return HARNESS.replace("@PROTOTYPES@", prototypes).replace("@RT_JUMP@", production_jump()).replace("@PROGRAMS@", emitted_programs())


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--compiler", default=os.environ.get("CC", "gcc"))
    args = ap.parse_args()
    with tempfile.TemporaryDirectory(prefix="sfc-return-dispatch-") as folder:
        build = Path(folder)
        src = build / "return_dispatch.c"
        exe = build / ("return_dispatch.exe" if os.name == "nt" else "return_dispatch")
        src.write_text(source(), encoding="utf-8")
        compile_result = subprocess.run([args.compiler, "-std=c11", "-O1", "-I", str(ROOT / "runtime"), str(src), "-o", str(exe)], capture_output=True, text=True)
        if compile_result.returncode:
            print(compile_result.stderr, file=sys.stderr)
            return compile_result.returncode
        run = subprocess.run([str(exe)], capture_output=True, text=True)
        print(run.stdout, end="")
        if run.stderr:
            print(run.stderr, end="", file=sys.stderr)
        return run.returncode


if __name__ == "__main__":
    raise SystemExit(main())

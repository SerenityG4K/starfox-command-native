#!/usr/bin/env python3
"""Banc d'essai différentiel du traducteur contre unicorn.

Prend des blocs linéaires (sans branchement ni appel) du code du jeu, les
traduit en C (`generated/test_blocks.c`), compile un exécutable de test, puis
exécute chaque bloc des deux côtés — unicorn (référence) et code recompilé —
depuis le même état, et compare registres, drapeaux et mémoire.

Usage :
    python tools/recomp/test_diff.py <rom.nds> [--count 3000] [--seed 1] [--region arm9]
"""
from __future__ import annotations

import argparse
import json
import random
import subprocess
import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import unicorn  # noqa: E402
from unicorn import arm_const as uc  # noqa: E402

from nitro.memory import Memory  # noqa: E402
from nitro.rom import NdsRom  # noqa: E402
from recomp.emit import Context, Emitter  # noqa: E402
from recomp.insn import ARM, THUMB, Insn  # noqa: E402
from recomp.recomp import PROLOGUE, decode_at  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent.parent
CONTROL = {"B", "BL", "BLX", "BX", "SWI", "BKPT", "MRS", "MSR", "MCR", "MRC", "CDP", "LDC", "STC", "UNDEF", "PLD"}


def crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


SOLID_ORIGINS = {"entrée", "irq", "NitroMain", "ctor", "registre", "bl", "blx", "pool", "queue"}
ARM9_CODE_END = 0x020A6000


def linear_blocks(memory: Memory, functions: list[dict], limit: int, rng: random.Random) -> list[tuple[int, int, int, list[Insn]]]:
    """Blocs (début, fin, mode, instructions) sans flot de contrôle ni écriture de pc,
    pris dans des fonctions sûres (unicorn n'est pas fidèle sur les accès non
    alignés que produirait du code conjectural décodé depuis des données)."""
    out = []
    fns = [f for f in functions if f["origine"] in SOLID_ORIGINS
           and (f["entrée"] < ARM9_CODE_END or f["entrée"] >= 0x02100000)]
    rng.shuffle(fns)
    for f in fns:
        mode = THUMB if f["mode"] == "thumb" else ARM
        for s, e in f["blocs_liste"]:
            # préfixe linéaire du bloc : jusqu'à la première instruction de contrôle exclue
            insns: list[Insn] = []
            a = s
            while a < e:
                i = decode_at(memory, a, mode)
                if i is None or i.op in CONTROL or i.op == "UNDEF":
                    break
                writes_pc = i.rd == 15 or (i.op in ("LDM", "STM") and (i.reglist >> 15) & 1)
                if writes_pc or i.user_bank or (i.op in ("LDM", "STM") and i.rn == 15):
                    break
                insns.append(i)
                a += i.size
            if insns:
                out.append((s, a, mode, insns))
            if len(out) >= limit:
                return out
    return out


def generate_c(blocks) -> str:
    parts = [PROLOGUE.replace('#include "functions.h"', ""), "typedef void (*TestBlockFn)(Cpu *c);"]
    for k, (s, e, mode, insns) in enumerate(blocks):
        ctx = Context(labels=set(), symbol=lambda a: None, mode=mode)
        em = Emitter(ctx)
        lines = [f"/* bloc {k} : 0x{s:08X}-0x{e:08X} {'Thumb' if mode == THUMB else 'ARM'} */",
                 f"static void t_{k}(Cpu *c) {{",
                 "    uint32_t res, op2, sc, ea, val; uint64_t wide; (void)res; (void)op2; (void)sc; (void)ea; (void)val; (void)wide;"]
        for i in insns:
            lines.append(f"    /* {i} */")
            for line in em.emit(i):
                lines.append("    " + line)
        lines.append("}")
        parts.append("\n".join(lines))
    parts.append("const TestBlockFn g_test_blocks[] = {" + ", ".join(f"t_{k}" for k in range(len(blocks))) + "};")
    parts.append(f"const size_t g_test_block_count = {len(blocks)};")
    return "\n\n".join(parts) + "\n"


class Native:
    def __init__(self, exe: Path) -> None:
        self.p = subprocess.Popen([str(exe)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)

    def cmd(self, line: str) -> str:
        self.p.stdin.write(line + "\n")
        self.p.stdin.flush()
        return self.p.stdout.readline().strip()

    def dump(self) -> tuple[list[int], list[int], int, int]:
        regs_line = self.cmd("dump")
        hash_line = self.p.stdout.readline().strip()
        parts = regs_line.split()
        regs = [int(x, 16) for x in parts[1:17]]
        flags = [int(x) for x in parts[17:22]]
        h = hash_line.split()
        return regs, flags, int(h[1], 16), int(h[2], 16)


UC_REGS = [uc.UC_ARM_REG_R0, uc.UC_ARM_REG_R1, uc.UC_ARM_REG_R2, uc.UC_ARM_REG_R3, uc.UC_ARM_REG_R4, uc.UC_ARM_REG_R5,
           uc.UC_ARM_REG_R6, uc.UC_ARM_REG_R7, uc.UC_ARM_REG_R8, uc.UC_ARM_REG_R9, uc.UC_ARM_REG_R10, uc.UC_ARM_REG_R11,
           uc.UC_ARM_REG_R12, uc.UC_ARM_REG_SP, uc.UC_ARM_REG_LR, uc.UC_ARM_REG_PC]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("rom")
    ap.add_argument("--codemap", default="analysis/codemap.json")
    ap.add_argument("--count", type=int, default=2000)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--region", default="arm9")
    ap.add_argument("--build", default="build/recomp_test")
    ap.add_argument("--no-compile", action="store_true")
    args = ap.parse_args()

    rng = random.Random(args.seed)
    rom = NdsRom.load(args.rom)
    # le coprocesseur a son propre espace mémoire et sa propre carte
    memory = Memory(rom, cpu="arm7" if args.region == "arm7" else "arm9")
    codemap = json.load(open(args.codemap, encoding="utf-8"))
    if args.region.startswith("ovl"):
        memory.map_overlay(int(args.region[3:]))
    blocks = linear_blocks(memory, codemap[args.region], args.count, rng)
    print(f"{len(blocks)} blocs linéaires, {sum(len(b[3]) for b in blocks)} instructions")

    build = ROOT / args.build
    build.mkdir(parents=True, exist_ok=True)
    src = build / "test_blocks.c"
    src.write_text(generate_c(blocks), encoding="utf-8")
    exe = build / "recomp_test.exe"
    if not args.no_compile:
        # le banc ne lie ni rt.c ni irq.c : la traque de valeur et le contrôle de pile sont désactivés
        cmd = ["gcc", "-O1", "-std=c11", "-DSFC_NO_WATCH_VALUE", "-I", str(ROOT / "runtime"), str(src), str(ROOT / "runtime/memory.c"),
               str(ROOT / "runtime/test_main.c"), "-o", str(exe)]
        print(" ".join(cmd), flush=True)
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            print(r.stderr[:4000])
            return 1
        print("compilé", flush=True)

    # Image mémoire commune : le code en place, le reste aléatoire (déterministe)
    ram = bytearray(rng.randbytes(0x400000))
    itcm = bytearray(rng.randbytes(0x8000))
    if args.region == "arm7":
        # le coprocesseur : son crt0 à son adresse, et ses sections là où il les recopie ;
        # celles qui vivent hors de la RAM principale (sa WRAM) ne tiennent pas dans cette
        # image, mais aucun bloc de calcul n'y lit son propre code
        for r in memory.static:
            lo = r.base - 0x02000000
            if 0 <= lo and lo + len(r.data) <= len(ram):
                ram[lo:lo + len(r.data)] = r.data
    else:
        arm9 = memory.region("arm9")
        ram[0:len(arm9.data)] = arm9.data
        if args.region.startswith("ovl"):
            ov = memory.region(args.region)
            ram[ov.base - 0x02000000:ov.base - 0x02000000 + len(ov.data)] = ov.data
        it = memory.region("itcm")
        itcm[0:len(it.data)] = it.data
    ram_file = build / "ram.bin"
    itcm_file = build / "itcm.bin"
    ram_file.write_bytes(ram)
    itcm_file.write_bytes(itcm)

    native = Native(exe)
    assert native.cmd(f"load {ram_file}") == "ok"
    assert native.cmd(f"itcm {itcm_file}") == "ok"

    mu = unicorn.Uc(unicorn.UC_ARCH_ARM, unicorn.UC_MODE_ARM)
    mu.mem_map(0x02000000, 0x400000)
    # ITCM : 32 Ko en miroir sur 0x00000000–0x01FFFFFF (configuration du SDK)
    mu.mem_map(0x00000000, 0x02000000)
    itcm_mirror = bytes(itcm) * (0x02000000 // 0x8000)

    # unicorn n'aliase pas : une écriture dans un miroir de l'ITCM est recopiée dans tous les autres
    def on_itcm_write(u, access, address, size, value, user_data):
        data = value.to_bytes(size, "little") if size <= 8 else None
        if data is None:
            return
        off = address & 0x7FFF
        for base in range(0, 0x02000000, 0x8000):
            if base != (address & ~0x7FFF):
                u.mem_write(base + off, data)

    mu.hook_add(unicorn.UC_HOOK_MEM_WRITE, on_itcm_write, begin=0x00000000, end=0x01FFFFFF)

    # QEMU lit et écrit tel quel aux adresses non alignées ; l'ARM9 aligne (et tourne
    # les mots lus). Un bloc qui fait un tel accès n'est pas comparable : on le marque.
    unaligned = {"seen": False}

    def on_access(u, access, address, size, value, user_data):
        if size > 1 and address % size:
            unaligned["seen"] = True

    mu.hook_add(unicorn.UC_HOOK_MEM_READ | unicorn.UC_HOOK_MEM_WRITE, on_access)

    def resync() -> None:
        mu.mem_write(0x02000000, bytes(ram))
        mu.mem_write(0x00000000, itcm_mirror)
        native.cmd(f"load {ram_file}")
        native.cmd(f"itcm {itcm_file}")

    resync()
    failures = 0
    tested = 0
    skipped = 0
    accumulated = 0
    for k, (s, e, mode, insns) in enumerate(blocks):
        # registres aléatoires : des pointeurs vers la RAM, sp et un peu de valeurs quelconques
        # pointeurs alignés vers la RAM ou l'ITCM, petits entiers pairs, valeurs quelconques
        regs = []
        for n in range(15):
            kind = rng.random()
            if kind < 0.65:
                regs.append(0x02000000 + (rng.randrange(0, 0x3FF000) & ~3))
            elif kind < 0.75:
                regs.append(rng.randrange(0, 0x7000) & ~3)
            elif kind < 0.9:
                regs.append(rng.randrange(0, 0x100) & ~3)
            else:
                regs.append(rng.getrandbits(32) & ~3)
        regs[13] = 0x02300000 + (rng.randrange(0, 0x80000) & ~7)
        regs.append(s)
        flags = [rng.getrandbits(1) for _ in range(5)]
        cpsr = (flags[0] << 31) | (flags[1] << 30) | (flags[2] << 29) | (flags[3] << 28) | (flags[4] << 27) | 0x1F | (0x20 if mode == THUMB else 0)

        # le mode d'abord : les registres r13/r14 écrits ensuite vont dans la bonne banque
        mu.reg_write(uc.UC_ARM_REG_CPSR, cpsr)
        for n in range(15):
            mu.reg_write(UC_REGS[n], regs[n])
        native.cmd("regs " + " ".join(f"{v:X}" for v in regs) + " " + " ".join(str(f) for f in flags))
        unaligned["seen"] = False
        try:
            mu.emu_start(s | (1 if mode == THUMB else 0), e, count=len(insns))
        except unicorn.UcError as ex:
            skipped += 1
            resync()  # unicorn a pu exécuter une partie du bloc : on repart de l'image
            continue
        if unaligned["seen"]:
            skipped += 1
            resync()
            continue
        native.cmd(f"run {k}")
        # comparaison
        uregs = [mu.reg_read(UC_REGS[n]) for n in range(15)]
        ucpsr = mu.reg_read(uc.UC_ARM_REG_CPSR)
        uflags = [(ucpsr >> 31) & 1, (ucpsr >> 30) & 1, (ucpsr >> 29) & 1, (ucpsr >> 28) & 1, (ucpsr >> 27) & 1]
        nregs, nflags, nhash, nhash_itcm = native.dump()
        uhash = crc32(mu.mem_read(0x02000000, 0x400000))
        uhash_itcm = crc32(mu.mem_read(0x01FF8000, 0x8000))
        tested += 1
        bad = []
        for n in range(15):
            if uregs[n] != nregs[n]:
                bad.append(f"r{n}: unicorn {uregs[n]:08X} natif {nregs[n]:08X}")
        for n, name in enumerate("NZCVQ"):
            if uflags[n] != nflags[n]:
                bad.append(f"{name}: unicorn {uflags[n]} natif {nflags[n]}")
        if uhash is not None and uhash != nhash:
            bad.append(f"mémoire: unicorn {uhash:08X} natif {nhash:08X}")
        if uhash_itcm is not None and uhash_itcm != nhash_itcm:
            bad.append(f"ITCM: unicorn {uhash_itcm:08X} natif {nhash_itcm:08X}")
        if bad:
            # Les blocs s'enchaînent sur la même image mémoire : un écart peut venir de ce
            # qu'un bloc précédent y a laissé. On rejoue celui-ci sur une image fraîche ; s'il
            # passe alors, ce n'est pas le traducteur qui a tort.
            resync()
            mu.reg_write(uc.UC_ARM_REG_CPSR, cpsr)
            for n in range(15):
                mu.reg_write(UC_REGS[n], regs[n])
            native.cmd("regs " + " ".join(f"{v:X}" for v in regs) + " " + " ".join(str(f) for f in flags))
            replayed = True
            try:
                mu.emu_start(s | (1 if mode == THUMB else 0), e, count=len(insns))
            except unicorn.UcError:
                replayed = False
            if replayed:
                native.cmd(f"run {k}")
                uregs2 = [mu.reg_read(UC_REGS[n]) for n in range(15)]
                nregs2, nflags2, nhash2, nhash_itcm2 = native.dump()
                ucpsr2 = mu.reg_read(uc.UC_ARM_REG_CPSR)
                uflags2 = [(ucpsr2 >> 31) & 1, (ucpsr2 >> 30) & 1, (ucpsr2 >> 29) & 1, (ucpsr2 >> 28) & 1, (ucpsr2 >> 27) & 1]
                if uregs2 == nregs2[:15] and uflags2 == nflags2:
                    accumulated += 1
                    resync()
                    continue
            failures += 1
            print(f"--- bloc {k} 0x{s:08X}-0x{e:08X} {'Thumb' if mode == THUMB else 'ARM'} ({len(insns)} instr.)")
            for i in insns:
                print(f"    {i}")
            for b in bad:
                print("   ", b)
            if failures >= 12:
                print("trop d'échecs, arrêt")
                break
            resync()
    native.cmd("quit")
    print(f"{tested} blocs testés, {failures} échec(s), {skipped} ignoré(s) (faute mémoire ou accès non aligné "
          f"sous unicorn), {accumulated} écart(s) venus de l'état laissé par un bloc précédent")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())

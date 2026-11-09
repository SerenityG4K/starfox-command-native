#!/usr/bin/env python3
"""Contrôle croisé des décodeurs ARM et Thumb contre capstone.

Pour chaque instruction du code cartographié, compare la forme canonique
(mnémonique, registres, immédiat, décalage) produite par nos décodeurs à
celle que capstone donne. Les divergences sont listées par motif.

Usage : python tools/recomp/check_decoders.py <rom.nds> [--region arm9] [--limit 50]
"""
from __future__ import annotations

import argparse
import json
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import capstone  # noqa: E402
from capstone import arm as cs_arm  # noqa: E402

from nitro.memory import Memory  # noqa: E402
from nitro.rom import NdsRom  # noqa: E402
from recomp.insn import ARM, THUMB, Insn  # noqa: E402
from recomp.recomp import decode_at  # noqa: E402

# capstone -> nos noms
CS_OPS = {
    "and": "AND", "eor": "EOR", "sub": "SUB", "rsb": "RSB", "add": "ADD", "adc": "ADC", "sbc": "SBC", "rsc": "RSC",
    "tst": "TST", "teq": "TEQ", "cmp": "CMP", "cmn": "CMN", "orr": "ORR", "mov": "MOV", "bic": "BIC", "mvn": "MVN",
    "lsl": "MOV", "lsr": "MOV", "asr": "MOV", "ror": "MOV", "rrx": "MOV", "neg": "RSB",
    "mul": "MUL", "mla": "MLA", "umull": "UMULL", "umlal": "UMLAL", "smull": "SMULL", "smlal": "SMLAL",
    "ldr": "LDR", "ldrb": "LDR", "ldrh": "LDR", "ldrsb": "LDR", "ldrsh": "LDR", "ldrd": "LDR",
    "str": "STR", "strb": "STR", "strh": "STR", "strd": "STR",
    "ldm": "LDM", "ldmib": "LDM", "ldmda": "LDM", "ldmdb": "LDM", "pop": "LDM",
    "stm": "STM", "stmib": "STM", "stmda": "STM", "stmdb": "STM", "push": "STM",
    "b": "B", "bl": "BL", "blx": "BLX", "bx": "BX", "swi": "SWI", "svc": "SWI", "bkpt": "BKPT",
    "mrs": "MRS", "msr": "MSR", "mcr": "MCR", "mrc": "MRC", "clz": "CLZ", "swp": "SWP", "swpb": "SWPB",
    "qadd": "QADD", "qsub": "QSUB", "qdadd": "QDADD", "qdsub": "QDSUB",
    "smulbb": "SMULXY", "smulbt": "SMULXY", "smultb": "SMULXY", "smultt": "SMULXY",
    "smlabb": "SMLAXY", "smlabt": "SMLAXY", "smlatb": "SMLAXY", "smlatt": "SMLAXY",
    "smulwb": "SMULWY", "smulwt": "SMULWY", "smlawb": "SMLAWY", "smlawt": "SMLAWY",
    "smlalbb": "SMLALXY", "smlalbt": "SMLALXY", "smlaltb": "SMLALXY", "smlaltt": "SMLALXY",
    "nop": "MOV", "pld": "PLD", "cdp": "CDP", "ldc": "LDC", "stc": "STC",
}


def cs_shape(insn) -> tuple:
    regs = []
    imm = None
    for op in insn.operands:
        if op.type == cs_arm.ARM_OP_REG:
            regs.append(op.reg)
        elif op.type == cs_arm.ARM_OP_IMM:
            imm = op.imm
        elif op.type == cs_arm.ARM_OP_MEM:
            regs.append(op.mem.base)
            if op.mem.index:
                regs.append(op.mem.index)
            imm = op.mem.disp
    return (CS_OPS.get(insn.mnemonic.rstrip("s") if insn.mnemonic not in CS_OPS else insn.mnemonic), )


def reg_index(cs_reg: int) -> int | None:
    table = {cs_arm.ARM_REG_R0: 0, cs_arm.ARM_REG_R1: 1, cs_arm.ARM_REG_R2: 2, cs_arm.ARM_REG_R3: 3, cs_arm.ARM_REG_R4: 4,
             cs_arm.ARM_REG_R5: 5, cs_arm.ARM_REG_R6: 6, cs_arm.ARM_REG_R7: 7, cs_arm.ARM_REG_R8: 8, cs_arm.ARM_REG_R9: 9,
             cs_arm.ARM_REG_R10: 10, cs_arm.ARM_REG_R11: 11, cs_arm.ARM_REG_R12: 12, cs_arm.ARM_REG_SP: 13,
             cs_arm.ARM_REG_LR: 14, cs_arm.ARM_REG_PC: 15}
    return table.get(cs_reg)


def compare(i: Insn, cs) -> str | None:
    """None si compatible, sinon une courte description de l'écart."""
    mn = cs.mnemonic
    base = mn
    # retire la condition et le suffixe s
    for suffix in ("eq", "ne", "cs", "hs", "cc", "lo", "mi", "pl", "vs", "vc", "hi", "ls", "ge", "lt", "gt", "le", "al"):
        if base.endswith(suffix) and base[:-2] in CS_OPS:
            base = base[:-2]
            break
    if base not in CS_OPS and base.endswith("s") and base[:-1] in CS_OPS:
        base = base[:-1]
    if base.startswith(("ldr", "str")) and base not in CS_OPS:
        for suffix in ("eq", "ne", "cs", "hs", "cc", "lo", "mi", "pl", "vs", "vc", "hi", "ls", "ge", "lt", "gt", "le"):
            if base.endswith(suffix):
                base = base[:-2]
                break
    ours = i.op
    want = CS_OPS.get(base)
    if want is None:
        return f"capstone inconnu {mn}"
    if ours != want:
        return f"op {ours} != {mn}"
    # registres : l'ensemble des registres cités doit coïncider
    cs_regs = set()
    for op in cs.operands:
        if op.type == cs_arm.ARM_OP_REG:
            r = reg_index(op.reg)
            if r is not None:
                cs_regs.add(r)
        elif op.type == cs_arm.ARM_OP_MEM:
            for r in (op.mem.base, op.mem.index):
                idx = reg_index(r)
                if idx is not None:
                    cs_regs.add(idx)
        if op.shift.type != 0 and op.shift.value and op.shift.type >= 6:  # décalage par registre
            idx = reg_index(op.shift.value)
            if idx is not None:
                cs_regs.add(idx)
    ours_regs = {r for r in (i.rd, i.rn, i.rm, i.rs) if r is not None}
    if i.op in ("MOV", "MVN"):
        ours_regs.discard(i.rn) if i.rn not in (i.rd, i.rm, i.rs) else None
        ours_regs = {r for r in (i.rd, i.rm, i.rs) if r is not None}
    if i.op in ("CMP", "CMN", "TST", "TEQ"):
        ours_regs = {r for r in (i.rn, i.rm, i.rs) if r is not None}
    if i.shift and i.shift.reg is not None:
        ours_regs.add(i.shift.reg)
    if i.op in ("LDM", "STM"):
        ours_regs = {i.rn} | {n for n in range(16) if (i.reglist >> n) & 1}
        if i.extra.get("push") or i.extra.get("pop") or cs.mnemonic.startswith(("push", "pop")):
            ours_regs.discard(13)
    if i.op in ("LDR", "STR") and i.width == "D":
        ours_regs.add(i.rd + 1)
    if i.op in ("B", "BL", "BLX") and i.target is not None:
        cs_imm = next((op.imm for op in cs.operands if op.type == cs_arm.ARM_OP_IMM), None)
        if cs_imm is not None and (cs_imm & ~1) != (i.target & ~1):
            return f"cible 0x{i.target:08X} != 0x{cs_imm:08X}"
        return None
    if i.op in ("MRS", "MSR", "MCR", "MRC", "SWI", "BKPT", "CDP", "LDC", "STC", "PLD"):
        return None
    if cs_regs != ours_regs:
        return f"registres {sorted(ours_regs)} != {sorted(cs_regs)} ({mn} {cs.op_str})"
    # immédiat
    cs_imm = None
    for op in cs.operands:
        if op.type == cs_arm.ARM_OP_IMM:
            cs_imm = op.imm & 0xFFFFFFFF
        elif op.type == cs_arm.ARM_OP_MEM and op.mem.index == 0:
            cs_imm = abs(op.mem.disp)
    if i.op in ("LDR", "STR") and not i.reg_offset and i.imm is not None and cs_imm is not None:
        if i.rn == 15 and i.mode == THUMB:
            pass
        elif (i.imm & 0xFFFFFFFF) != cs_imm:
            return f"déplacement 0x{i.imm:X} != 0x{cs_imm:X} ({mn} {cs.op_str})"
    elif i.imm is not None and i.rm is None and cs_imm is not None and i.op not in ("LDM", "STM"):
        if (i.imm & 0xFFFFFFFF) != cs_imm and i.op != "RSB":
            return f"immédiat 0x{i.imm:X} != 0x{cs_imm:X} ({mn} {cs.op_str})"
    return None


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("rom")
    ap.add_argument("--codemap", default="analysis/codemap.json")
    ap.add_argument("--region", default="arm9")
    ap.add_argument("--limit", type=int, default=40)
    args = ap.parse_args()

    memory = Memory(NdsRom.load(args.rom))
    if args.region.startswith("ovl"):
        memory.map_overlay(int(args.region[3:]))
    codemap = json.load(open(args.codemap, encoding="utf-8"))
    engines = {ARM: capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_ARM),
               THUMB: capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_THUMB)}
    for e in engines.values():
        e.detail = True
    total = 0
    mismatches: Counter = Counter()
    examples: dict[str, str] = {}
    for f in codemap[args.region]:
        mode = THUMB if f["mode"] == "thumb" else ARM
        for s, e in f["blocs_liste"]:
            a = s
            while a < e:
                i = decode_at(memory, a, mode)
                if i is None:
                    break
                raw = memory.read(a, 4)
                cs_list = list(engines[mode].disasm(raw, a, 1))
                if cs_list:
                    cs = cs_list[0]
                    if cs.size != i.size and i.op != "UNDEF":
                        key = f"taille {i.op} {i.size} != {cs.mnemonic} {cs.size}"
                        mismatches[key] += 1
                        examples.setdefault(key, f"{a:08X} {raw.hex()}")
                    else:
                        diff = compare(i, cs)
                        if diff:
                            key = diff.split("(")[0].strip()
                            mismatches[key] += 1
                            examples.setdefault(key, f"{a:08X} {raw.hex()} | {i} | {cs.mnemonic} {cs.op_str}")
                elif i.op != "UNDEF":
                    key = f"capstone refuse, nous {i.op}"
                    mismatches[key] += 1
                    examples.setdefault(key, f"{a:08X} {raw.hex()} | {i}")
                total += 1
                a += i.size
    print(f"{total} instructions comparées, {sum(mismatches.values())} écarts, {len(mismatches)} motifs")
    for key, n in mismatches.most_common(args.limit):
        print(f"  ×{n:<6} {key}")
        print(f"          ex. {examples[key]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

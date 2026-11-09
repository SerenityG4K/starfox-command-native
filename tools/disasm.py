#!/usr/bin/env python3
"""Désassembleur annoté pour l'ARM9 de Star Fox Command (capstone).

Désassemble une fonction ou une plage, en Thumb ou en ARM, et annote :
les cibles des appels, les littéraux chargés depuis le pool de constantes,
et les chaînes qu'ils désignent.

Usage :
    python tools/disasm.py <rom.nds> 0x02019838              # fonction Thumb
    python tools/disasm.py <rom.nds> 0x02019838 --arm        # forcer le mode ARM
    python tools/disasm.py <rom.nds> 0x0216C440 --overlay 0  # dans l'overlay 0
    python tools/disasm.py <rom.nds> 0x02019838 --length 256
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import capstone  # noqa: E402

from nitro.memory import Memory  # noqa: E402
from nitro.rom import NdsRom  # noqa: E402


def disassemble(memory: Memory, addr: int, length: int, thumb: bool,
                stop_at_return: bool) -> list[str]:
    code = memory.read(addr, length)
    if not code:
        return [f"aucune donnée en 0x{addr:08X} (overlay non mappé ?)"]
    engine = capstone.Cs(capstone.CS_ARCH_ARM,
                         capstone.CS_MODE_THUMB if thumb else capstone.CS_MODE_ARM)
    engine.detail = True
    out: list[str] = []
    for insn in engine.disasm(code, addr):
        note = ""
        # ldr rX, [pc, #imm] : littéral dans le pool de constantes
        if insn.mnemonic.startswith("ldr") and "pc" in insn.op_str:
            for op in insn.operands:
                if op.type == capstone.arm.ARM_OP_MEM and op.mem.base == capstone.arm.ARM_REG_PC:
                    pool = ((insn.address + (4 if thumb else 8)) & ~3) + op.mem.disp
                    value = memory.u32(pool)
                    if value is not None:
                        text = memory.cstring(value)
                        note = f"= 0x{value:08X}" + (f'  "{text}"' if text else "")
        # bl / blx : cible
        if insn.mnemonic in ("bl", "blx") and insn.operands:
            target = insn.operands[0].imm
            note = f"-> 0x{target:08X}"
        out.append(f"{insn.address:08X}  {insn.bytes.hex():<8}  {insn.mnemonic:<8} {insn.op_str:<28} {note}")
        if stop_at_return and insn.mnemonic == "pop" and "pc" in insn.op_str:
            break
        if stop_at_return and insn.mnemonic == "bx" and insn.op_str.strip() == "lr":
            break
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("rom")
    ap.add_argument("address", help="adresse RAM, hexadécimale")
    ap.add_argument("--overlay", type=int, default=None)
    ap.add_argument("--arm7", action="store_true", help="dans le binaire du coprocesseur")
    ap.add_argument("--arm", action="store_true", help="mode ARM (Thumb par défaut)")
    ap.add_argument("--length", type=int, default=512)
    ap.add_argument("--all", action="store_true", help="ne pas s'arrêter au retour de fonction")
    args = ap.parse_args()

    memory = Memory(NdsRom.load(args.rom), cpu="arm7" if args.arm7 else "arm9")
    if args.overlay is not None:
        memory.map_overlay(args.overlay)
    addr = int(args.address, 16)
    thumb = not args.arm and bool(addr & 1 or True)
    addr &= ~1
    for line in disassemble(memory, addr, args.length, thumb, not args.all):
        print(line)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

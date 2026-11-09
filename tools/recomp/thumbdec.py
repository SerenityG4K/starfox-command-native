"""Décodeur Thumb (ARMv5T : Thumb-1 plus `blx`).

Produit la même `Insn` que le décodeur ARM. Conventions :
- les opérations de données Thumb mettent les drapeaux (s=True) sauf les
  formes « registre haut » (add/mov/cmp avec r8–r15 : cmp met les drapeaux,
  add et mov non) et les additions à sp/pc ;
- `pc` lu comme opérande vaut `addr + 4` ; pour `ldr rd, [pc, #imm]` et
  `add rd, pc, #imm`, il est en plus aligné sur 4 : le décodeur pose
  `extra["pc_aligned"]` ;
- `bl` occupe deux demi-mots : le décodeur reçoit les deux et rend size=4.
"""
from __future__ import annotations

from .insn import ASR, LSL, LSR, ROR, THUMB, Insn, Shift

ALU_OPS = ["AND", "EOR", "LSL", "LSR", "ASR", "ADC", "SBC", "ROR",
           "TST", "NEG", "CMP", "CMN", "ORR", "MUL", "BIC", "MVN"]


def _bits(v: int, hi: int, lo: int) -> int:
    return (v >> lo) & ((1 << (hi - lo + 1)) - 1)


def _bit(v: int, n: int) -> bool:
    return bool((v >> n) & 1)


def _sext(v: int, bits: int) -> int:
    if v & (1 << (bits - 1)):
        return v - (1 << bits)
    return v


def decode_thumb(hw: int, addr: int, hw2: int | None = None) -> Insn:
    i = Insn(addr=addr, size=2, mode=THUMB, op="UNDEF", raw=hw)
    top = _bits(hw, 15, 13)

    if top == 0b000:
        if _bits(hw, 12, 11) != 0b11:
            # lsl/lsr/asr rd, rm, #imm
            kind = [LSL, LSR, ASR][_bits(hw, 12, 11)]
            amount = _bits(hw, 10, 6)
            i.op = "MOV"
            i.s = True
            i.rd = _bits(hw, 2, 0)
            i.rm = _bits(hw, 5, 3)
            if amount == 0 and kind != LSL:
                amount = 32
            i.shift = Shift(kind, amount)
            return i
        # add/sub rd, rn, rm | #imm3
        i.op = "SUB" if _bit(hw, 9) else "ADD"
        i.s = True
        i.rd = _bits(hw, 2, 0)
        i.rn = _bits(hw, 5, 3)
        if _bit(hw, 10):
            i.imm = _bits(hw, 8, 6)
        else:
            i.rm = _bits(hw, 8, 6)
            i.shift = Shift(LSL, 0)
        return i

    if top == 0b001:
        # mov/cmp/add/sub rd, #imm8
        op = _bits(hw, 12, 11)
        i.op = ["MOV", "CMP", "ADD", "SUB"][op]
        i.s = True
        rd = _bits(hw, 10, 8)
        i.imm = _bits(hw, 7, 0)
        if op == 0:
            i.rd = rd
        elif op == 1:
            i.rn = rd
        else:
            i.rd = rd
            i.rn = rd
        return i

    if top == 0b010:
        if _bits(hw, 12, 10) == 0b000:
            # opérations ALU registre-registre
            op = _bits(hw, 9, 6)
            name = ALU_OPS[op]
            rd = _bits(hw, 2, 0)
            rm = _bits(hw, 5, 3)
            i.s = True
            if name in (LSL, LSR, ASR, ROR):
                i.op = "MOV"
                i.rd = rd
                i.rm = rd
                i.rs = rm
                i.shift = Shift(name, None, rm)
            elif name == "NEG":
                i.op = "RSB"
                i.rd = rd
                i.rn = rm
                i.imm = 0
            elif name == "MUL":
                i.op = "MUL"
                i.rd = rd
                i.rm = rd
                i.rs = rm
            elif name in ("TST", "CMP", "CMN"):
                i.op = name
                i.rn = rd
                i.rm = rm
                i.shift = Shift(LSL, 0)
            elif name == "MVN":
                i.op = "MVN"
                i.rd = rd
                i.rm = rm
                i.shift = Shift(LSL, 0)
            else:
                i.op = name
                i.rd = rd
                i.rn = rd
                i.rm = rm
                i.shift = Shift(LSL, 0)
            return i
        if _bits(hw, 12, 10) == 0b001:
            # registres hauts : add/cmp/mov/bx/blx
            op = _bits(hw, 9, 8)
            rd = _bits(hw, 2, 0) | (_bits(hw, 7, 7) << 3)
            rm = _bits(hw, 6, 3)
            if op == 0:
                i.op = "ADD"
                i.rd = rd
                i.rn = rd
                i.rm = rm
                i.shift = Shift(LSL, 0)
            elif op == 1:
                i.op = "CMP"
                i.s = True
                i.rn = rd
                i.rm = rm
                i.shift = Shift(LSL, 0)
            elif op == 2:
                i.op = "MOV"
                i.rd = rd
                i.rm = rm
                i.shift = Shift(LSL, 0)
            else:
                i.op = "BLX" if _bit(hw, 7) else "BX"
                i.link = _bit(hw, 7)
                i.rm = rm
            return i
        if not _bit(hw, 12):
            # 01001 rd imm8 : ldr rd, [pc, #imm8*4]
            i.op = "LDR"
            i.load = True
            i.rd = _bits(hw, 10, 8)
            i.rn = 15
            i.imm = _bits(hw, 7, 0) * 4
            i.extra["pc_aligned"] = True
            return i
        # 0101 ooo rm rn rd : ldr/str à décalage registre, bits 11..9
        op = _bits(hw, 11, 9)
        i.rd = _bits(hw, 2, 0)
        i.rn = _bits(hw, 5, 3)
        i.rm = _bits(hw, 8, 6)
        i.reg_offset = True
        i.shift = Shift(LSL, 0)
        table = {0: ("STR", "W", False), 1: ("STR", "H", False), 2: ("STR", "B", False), 3: ("LDR", "SB", True),
                 4: ("LDR", "W", True), 5: ("LDR", "H", True), 6: ("LDR", "B", True), 7: ("LDR", "SH", True)}
        i.op, i.width, i.load = table[op]
        return i

    if top == 0b011:
        # ldr/str rd, [rn, #imm5] mot ou octet
        i.op = "LDR" if _bit(hw, 11) else "STR"
        i.load = _bit(hw, 11)
        i.width = "B" if _bit(hw, 12) else "W"
        i.rd = _bits(hw, 2, 0)
        i.rn = _bits(hw, 5, 3)
        i.imm = _bits(hw, 10, 6) * (1 if i.width == "B" else 4)
        return i

    if top == 0b100:
        if not _bit(hw, 12):
            # ldrh/strh rd, [rn, #imm5*2]
            i.op = "LDR" if _bit(hw, 11) else "STR"
            i.load = _bit(hw, 11)
            i.width = "H"
            i.rd = _bits(hw, 2, 0)
            i.rn = _bits(hw, 5, 3)
            i.imm = _bits(hw, 10, 6) * 2
            return i
        # ldr/str rd, [sp, #imm8*4]
        i.op = "LDR" if _bit(hw, 11) else "STR"
        i.load = _bit(hw, 11)
        i.rd = _bits(hw, 10, 8)
        i.rn = 13
        i.imm = _bits(hw, 7, 0) * 4
        return i

    if top == 0b101:
        if not _bit(hw, 12):
            # add rd, pc|sp, #imm8*4
            i.op = "ADD"
            i.rd = _bits(hw, 10, 8)
            i.rn = 13 if _bit(hw, 11) else 15
            i.imm = _bits(hw, 7, 0) * 4
            if i.rn == 15:
                i.extra["pc_aligned"] = True
            return i
        if _bits(hw, 11, 8) == 0b0000:
            # add/sub sp, #imm7*4
            i.op = "SUB" if _bit(hw, 7) else "ADD"
            i.rd = 13
            i.rn = 13
            i.imm = _bits(hw, 6, 0) * 4
            return i
        if _bits(hw, 11, 9) == 0b010:
            # push {rlist, lr?}
            i.op = "STM"
            i.pre = True
            i.up = False
            i.writeback = True
            i.rn = 13
            i.reglist = _bits(hw, 7, 0) | (0x4000 if _bit(hw, 8) else 0)
            i.extra["push"] = True
            return i
        if _bits(hw, 11, 9) == 0b110:
            # pop {rlist, pc?}
            i.op = "LDM"
            i.load = True
            i.pre = False
            i.up = True
            i.writeback = True
            i.rn = 13
            i.reglist = _bits(hw, 7, 0) | (0x8000 if _bit(hw, 8) else 0)
            i.extra["pop"] = True
            return i
        if _bits(hw, 11, 8) == 0b1110:
            i.op = "BKPT"
            i.imm = _bits(hw, 7, 0)
            return i
        return i

    if top == 0b110:
        if not _bit(hw, 12):
            # stmia/ldmia rn!, {rlist}
            i.op = "LDM" if _bit(hw, 11) else "STM"
            i.load = _bit(hw, 11)
            i.pre = False
            i.up = True
            i.rn = _bits(hw, 10, 8)
            i.reglist = _bits(hw, 7, 0)
            # ldmia sans writeback si rn dans la liste
            i.writeback = not (i.load and (i.reglist >> i.rn) & 1)
            return i
        cond = _bits(hw, 11, 8)
        if cond == 0xF:
            i.op = "SWI"
            i.swi = _bits(hw, 7, 0)
            return i
        if cond == 0xE:
            return i  # undefined
        i.op = "B"
        i.cond = cond
        i.target = (addr + 4 + (_sext(_bits(hw, 7, 0), 8) << 1)) & 0xFFFFFFFF
        return i

    # top == 0b111
    h = _bits(hw, 12, 11)
    if h == 0b00:
        i.op = "B"
        i.target = (addr + 4 + (_sext(_bits(hw, 10, 0), 11) << 1)) & 0xFFFFFFFF
        return i
    if h == 0b10:
        # première moitié de bl/blx : nécessite hw2
        if hw2 is None or _bits(hw2, 15, 13) != 0b111 or _bits(hw2, 12, 11) == 0b10:
            i.op = "UNDEF"
            return i
        i.size = 4
        i.raw = (hw << 16) | hw2
        i.link = True
        hi = _sext(_bits(hw, 10, 0), 11) << 12
        lo = _bits(hw2, 10, 0) << 1
        target = (addr + 4 + hi + lo) & 0xFFFFFFFF
        if _bits(hw2, 12, 11) == 0b11:
            i.op = "BL"
            i.target = target
        else:
            i.op = "BLX"
            i.exchange = True
            i.target = target & ~3
        return i
    # h == 01 (blx suffix seul) ou 11 (bl suffix seul) : ne devrait pas arriver isolé
    return i

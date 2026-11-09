"""Décodeur ARM (ARMv5TE, jeu d'instructions 32 bits).

Chaque mot devient une `Insn`. Les opérations de données gardent leur opérande
décalé tel quel (`Shift`) : c'est le traducteur qui applique la sémantique du
shifter (résultat et retenue). Les immédiats ARM sont donnés déjà tournés,
avec la rotation à part pour la retenue.
"""
from __future__ import annotations

from .insn import ARM, ASR, LSL, LSR, ROR, RRX, Insn, Shift

DATA_OPS = ["AND", "EOR", "SUB", "RSB", "ADD", "ADC", "SBC", "RSC",
            "TST", "TEQ", "CMP", "CMN", "ORR", "MOV", "BIC", "MVN"]
SHIFT_KINDS = [LSL, LSR, ASR, ROR]


def _bits(v: int, hi: int, lo: int) -> int:
    return (v >> lo) & ((1 << (hi - lo + 1)) - 1)


def _bit(v: int, n: int) -> bool:
    return bool((v >> n) & 1)


def _sext(v: int, bits: int) -> int:
    if v & (1 << (bits - 1)):
        return v - (1 << bits)
    return v


def _shift_imm(word: int) -> Shift:
    """Opérande `rm, <shift> #imm` (bits 11..4)."""
    kind = SHIFT_KINDS[_bits(word, 6, 5)]
    amount = _bits(word, 11, 7)
    if amount == 0:
        if kind == LSL:
            return Shift(LSL, 0)
        if kind == ROR:
            return Shift(RRX, None)
        return Shift(kind, 32)  # LSR #0 et ASR #0 signifient #32
    return Shift(kind, amount)


def decode_arm(word: int, addr: int) -> Insn:
    i = Insn(addr=addr, size=4, mode=ARM, op="UNDEF", raw=word, cond=_bits(word, 31, 28))
    cond = i.cond
    b27_25 = _bits(word, 27, 25)

    # ---- espace inconditionnel (cond = NV) : blx imm, pld
    if cond == 0xF:
        if _bits(word, 27, 25) == 0b101:
            i.op = "BLX"
            i.link = True
            i.exchange = True
            offset = _sext(_bits(word, 23, 0), 24) << 2
            i.target = (addr + 8 + offset + (2 if _bit(word, 24) else 0)) & 0xFFFFFFFF
            i.cond = 14
            return i
        if _bits(word, 27, 24) == 0b0101 and _bits(word, 22, 20) & 0b101 == 0b101:
            i.op = "PLD"
            i.cond = 14
            return i
        return i

    # ---- classe 000 : données (registre), multiplications, transferts demi-mot, divers
    if b27_25 == 0b000:
        b7_4 = _bits(word, 7, 4)
        # Multiplications : bits 7..4 = 1001
        if b7_4 == 0b1001:
            if _bits(word, 24, 23) == 0b00:
                # mul / mla
                i.op = "MLA" if _bit(word, 21) else "MUL"
                i.s = _bit(word, 20)
                i.rd = _bits(word, 19, 16)
                i.rn = _bits(word, 15, 12) if i.op == "MLA" else None   # accumulateur
                i.rs = _bits(word, 11, 8)
                i.rm = _bits(word, 3, 0)
                return i
            if _bits(word, 24, 23) == 0b01:
                # umull / umlal / smull / smlal
                signed = _bit(word, 22)
                acc = _bit(word, 21)
                i.op = ("S" if signed else "U") + ("MLAL" if acc else "MULL")
                i.s = _bit(word, 20)
                i.rd = _bits(word, 19, 16)   # RdHi
                i.rn = _bits(word, 15, 12)   # RdLo
                i.rs = _bits(word, 11, 8)
                i.rm = _bits(word, 3, 0)
                return i
            if _bits(word, 24, 23) == 0b10:
                # swp / swpb
                i.op = "SWPB" if _bit(word, 22) else "SWP"
                i.rn = _bits(word, 19, 16)
                i.rd = _bits(word, 15, 12)
                i.rm = _bits(word, 3, 0)
                return i
            return i
        # Transferts demi-mot / signés / double : bit 7 = 1 et bit 4 = 1, bits 6..5 != 00
        if _bit(word, 7) and _bit(word, 4) and _bits(word, 6, 5) != 0:
            i.pre = _bit(word, 24)
            i.up = _bit(word, 23)
            imm_form = _bit(word, 22)
            i.writeback = _bit(word, 21) or not i.pre
            load = _bit(word, 20)
            i.rn = _bits(word, 19, 16)
            i.rd = _bits(word, 15, 12)
            sh = _bits(word, 6, 5)
            if imm_form:
                i.imm = (_bits(word, 11, 8) << 4) | _bits(word, 3, 0)
                i.reg_offset = False
            else:
                i.rm = _bits(word, 3, 0)
                i.reg_offset = True
                i.shift = Shift(LSL, 0)
            if load:
                i.load = True
                i.width = {1: "H", 2: "SB", 3: "SH"}[sh]
                i.op = "LDR"
            else:
                if sh == 1:
                    i.op, i.width = "STR", "H"
                elif sh == 2:
                    i.op, i.width, i.load = "LDR", "D", True
                else:
                    i.op, i.width = "STR", "D"
            return i
        # Divers (bits 24..23 = 10, S = 0) : mrs, msr, bx, blx reg, clz, qadd…, bkpt, smul…
        if _bits(word, 24, 23) == 0b10 and not _bit(word, 20):
            if b7_4 == 0b0000:
                if _bit(word, 21):
                    i.op = "MSR"
                    i.spsr = _bit(word, 22)
                    i.fields = _bits(word, 19, 16)
                    i.rm = _bits(word, 3, 0)
                else:
                    i.op = "MRS"
                    i.spsr = _bit(word, 22)
                    i.rd = _bits(word, 15, 12)
                return i
            if b7_4 == 0b0001:
                if _bits(word, 22, 21) == 0b01:
                    i.op = "BX"
                    i.rm = _bits(word, 3, 0)
                elif _bits(word, 22, 21) == 0b11:
                    i.op = "CLZ"
                    i.rd = _bits(word, 15, 12)
                    i.rm = _bits(word, 3, 0)
                return i
            if b7_4 == 0b0011 and _bits(word, 22, 21) == 0b01:
                i.op = "BLX"
                i.link = True
                i.rm = _bits(word, 3, 0)
                return i
            if b7_4 == 0b0101:
                i.op = ["QADD", "QSUB", "QDADD", "QDSUB"][_bits(word, 22, 21)]
                i.rn = _bits(word, 19, 16)
                i.rd = _bits(word, 15, 12)
                i.rm = _bits(word, 3, 0)
                return i
            if b7_4 == 0b0111 and _bits(word, 22, 21) == 0b01:
                i.op = "BKPT"
                i.imm = (_bits(word, 19, 8) << 4) | _bits(word, 3, 0)
                return i
            if _bit(word, 7) and not _bit(word, 4):
                # smla<x><y>, smlaw<y>/smulw<y>, smlal<x><y>, smul<x><y>
                kind = _bits(word, 22, 21)
                i.x = _bit(word, 5)
                i.y = _bit(word, 6)
                i.rd = _bits(word, 19, 16)
                i.rn = _bits(word, 15, 12)
                i.rs = _bits(word, 11, 8)
                i.rm = _bits(word, 3, 0)
                if kind == 0:
                    i.op = "SMLAXY"
                elif kind == 1:
                    i.op = "SMULWY" if i.x else "SMLAWY"
                elif kind == 2:
                    i.op = "SMLALXY"
                else:
                    i.op = "SMULXY"
                return i
            return i
        # Opération de données, opérande registre
        i.op = DATA_OPS[_bits(word, 24, 21)]
        i.s = _bit(word, 20)
        i.rn = _bits(word, 19, 16)
        i.rd = _bits(word, 15, 12)
        i.rm = _bits(word, 3, 0)
        if _bit(word, 4):
            # décalage par registre
            i.rs = _bits(word, 11, 8)
            i.shift = Shift(SHIFT_KINDS[_bits(word, 6, 5)], None, i.rs)
        else:
            i.shift = _shift_imm(word)
        return i

    # ---- classe 001 : données (immédiat), msr immédiat
    if b27_25 == 0b001:
        opcode = _bits(word, 24, 21)
        if _bits(word, 24, 23) == 0b10 and not _bit(word, 20):
            if _bit(word, 21):
                i.op = "MSR"
                i.spsr = _bit(word, 22)
                i.fields = _bits(word, 19, 16)
                rot = _bits(word, 11, 8) * 2
                v = _bits(word, 7, 0)
                i.imm = ((v >> rot) | (v << (32 - rot))) & 0xFFFFFFFF if rot else v
                return i
            return i  # undefined
        i.op = DATA_OPS[opcode]
        i.s = _bit(word, 20)
        i.rn = _bits(word, 19, 16)
        i.rd = _bits(word, 15, 12)
        rot = _bits(word, 11, 8) * 2
        v = _bits(word, 7, 0)
        i.imm = ((v >> rot) | (v << (32 - rot))) & 0xFFFFFFFF if rot else v
        i.imm_rotate = rot
        return i

    # ---- classes 010 / 011 : ldr/str mot et octet
    if b27_25 in (0b010, 0b011):
        if b27_25 == 0b011 and _bit(word, 4):
            return i  # undefined (media, ARMv6)
        i.pre = _bit(word, 24)
        i.up = _bit(word, 23)
        i.width = "B" if _bit(word, 22) else "W"
        i.writeback = _bit(word, 21) or not i.pre
        i.load = _bit(word, 20)
        i.op = "LDR" if i.load else "STR"
        i.rn = _bits(word, 19, 16)
        i.rd = _bits(word, 15, 12)
        if b27_25 == 0b011:
            i.rm = _bits(word, 3, 0)
            i.reg_offset = True
            i.shift = _shift_imm(word)
        else:
            i.imm = _bits(word, 11, 0)
            i.reg_offset = False
        # post-indexé avec W : accès en mode utilisateur (ldrt/strt) — traité comme normal
        if not i.pre and _bit(word, 21):
            i.extra["user"] = True
        return i

    # ---- classe 100 : ldm/stm
    if b27_25 == 0b100:
        i.op = "LDM" if _bit(word, 20) else "STM"
        i.load = _bit(word, 20)
        i.pre = _bit(word, 24)
        i.up = _bit(word, 23)
        i.user_bank = _bit(word, 22)
        i.writeback = _bit(word, 21)
        i.rn = _bits(word, 19, 16)
        i.reglist = _bits(word, 15, 0)
        return i

    # ---- classe 101 : b / bl
    if b27_25 == 0b101:
        i.op = "BL" if _bit(word, 24) else "B"
        i.link = _bit(word, 24)
        offset = _sext(_bits(word, 23, 0), 24) << 2
        i.target = (addr + 8 + offset) & 0xFFFFFFFF
        return i

    # ---- classe 110 : ldc/stc
    if b27_25 == 0b110:
        i.op = "LDC" if _bit(word, 20) else "STC"
        i.cp = _bits(word, 11, 8)
        return i

    # ---- classe 111 : cdp/mcr/mrc, swi
    if b27_25 == 0b111:
        if _bit(word, 24):
            i.op = "SWI"
            i.swi = _bits(word, 23, 0)
            return i
        i.cp = _bits(word, 11, 8)
        i.opc1 = _bits(word, 23, 21)
        i.crn = _bits(word, 19, 16)
        i.rd = _bits(word, 15, 12)
        i.crm = _bits(word, 3, 0)
        i.opc2 = _bits(word, 7, 5)
        if _bit(word, 4):
            i.op = "MRC" if _bit(word, 20) else "MCR"
        else:
            i.op = "CDP"
            i.opc1 = _bits(word, 23, 20)
        return i
    return i

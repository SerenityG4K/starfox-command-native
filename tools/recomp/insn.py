"""Représentation unifiée d'une instruction ARMv5TE, en mode ARM ou Thumb.

Le décodeur Thumb produit la même structure que le décodeur ARM : le
traducteur ne connaît qu'une sémantique. Les différences de mode (valeur lue
dans `pc`, drapeaux implicites) sont portées par les champs.
"""
from __future__ import annotations

from dataclasses import dataclass, field

ARM = 0
THUMB = 1

# Conditions (codage ARM)
COND_NAMES = ["EQ", "NE", "CS", "CC", "MI", "PL", "VS", "VC", "HI", "LS", "GE", "LT", "GT", "LE", "AL", "NV"]
AL = 14

# Décalages
LSL, LSR, ASR, ROR, RRX = "LSL", "LSR", "ASR", "ROR", "RRX"

PC = 15
LR = 14
SP = 13


@dataclass
class Shift:
    kind: str = LSL            # LSL, LSR, ASR, ROR, RRX
    amount: int | None = 0     # décalage immédiat (None si par registre)
    reg: int | None = None     # registre de décalage (ARM seulement)


@dataclass
class Insn:
    addr: int
    size: int                  # 2 ou 4
    mode: int                  # ARM ou THUMB
    op: str                    # mnémonique canonique (voir emit.py)
    raw: int = 0
    cond: int = AL
    s: bool = False            # met à jour les drapeaux
    rd: int | None = None
    rn: int | None = None
    rm: int | None = None
    rs: int | None = None
    imm: int | None = None     # immédiat déjà « décodé » (rotation appliquée, déplacement signé…)
    imm_rotate: int = 0        # rotation de l'immédiat ARM (pour la retenue du shifter)
    shift: Shift | None = None # opérande décalé (rm)
    # accès mémoire
    width: str = "W"           # W, B, H, SB, SH, D
    load: bool = False
    pre: bool = True
    up: bool = True
    writeback: bool = False
    reg_offset: bool = False   # décalage par registre (rm[, shift]) sinon imm
    # transferts multiples
    reglist: int = 0
    user_bank: bool = False    # bit S de ldm/stm (^)
    # branchements
    target: int | None = None  # cible absolue (b, bl, blx imm)
    link: bool = False
    exchange: bool = False     # blx imm : changement de mode
    # msr/mrs
    spsr: bool = False
    fields: int = 0            # masque c,x,s,f
    # multiplications DSP
    x: bool = False
    y: bool = False
    # coprocesseur
    cp: int = 0
    opc1: int = 0
    crn: int = 0
    crm: int = 0
    opc2: int = 0
    # divers
    swi: int = 0
    extra: dict = field(default_factory=dict)

    @property
    def cond_name(self) -> str:
        return COND_NAMES[self.cond]

    def __str__(self) -> str:
        parts = [f"{self.addr:08X}", f"{self.op}{'' if self.cond == AL else self.cond_name}{'S' if self.s else ''}"]
        regs = [f"{k}={v}" for k, v in (("rd", self.rd), ("rn", self.rn), ("rm", self.rm), ("rs", self.rs)) if v is not None]
        parts.extend(regs)
        if self.imm is not None:
            parts.append(f"imm=0x{self.imm & 0xFFFFFFFF:X}")
        if self.shift and (self.shift.amount or self.shift.reg is not None or self.shift.kind == RRX):
            parts.append(f"{self.shift.kind} {self.shift.amount if self.shift.reg is None else 'r%d' % self.shift.reg}")
        if self.op in ("LDM", "STM", "PUSH", "POP"):
            parts.append(f"list=0x{self.reglist:04X}")
        if self.target is not None:
            parts.append(f"-> 0x{self.target:08X}")
        return " ".join(parts)

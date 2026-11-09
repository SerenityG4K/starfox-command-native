"""Traduction d'une instruction (`Insn`) en C, contre `runtime/cpu.h`.

Conventions du code généré :
- `c` est le `Cpu *` ; les registres sont `c->r[n]` (macro `R(n)`) ;
- une instruction conditionnelle est enveloppée dans `if (COND) { … }` ;
- le flot de contrôle est rendu par `goto L_xxxxxxxx` (cible dans la
  fonction), un appel direct `f_xxxxxxxx(c, 0x…)` (cible connue), ou
  `rt_call`/`rt_jump` (cible en registre) ;
- une instruction qui écrit `pc` termine le bloc : le traducteur émet
  `return` après le saut de queue.

Ce module ne connaît pas les fonctions : `Context` lui dit, pour une adresse,
si c'est une étiquette locale, une fonction connue, ou rien.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Callable

from .insn import AL, ARM, ASR, LSL, LSR, PC, ROR, RRX, THUMB, Insn

COND_EXPR = {
    0: "c->zf", 1: "!c->zf", 2: "c->cf", 3: "!c->cf", 4: "c->nf", 5: "!c->nf", 6: "c->vf", 7: "!c->vf",
    8: "(c->cf && !c->zf)", 9: "(!c->cf || c->zf)", 10: "(c->nf == c->vf)", 11: "(c->nf != c->vf)",
    12: "(!c->zf && c->nf == c->vf)", 13: "(c->zf || c->nf != c->vf)",
}


@dataclass
class Context:
    """Ce que le traducteur doit savoir de l'entourage d'une instruction."""
    labels: set[int]                                  # adresses des étiquettes locales
    symbol: Callable[..., "str | None"]               # (adresse[, mode]) -> nom C appelable directement
    switch_targets: dict[int, list[int]] = field(default_factory=dict)  # adresse du saut -> cibles
    mode: int = THUMB
    lr_before: dict[int, int] = field(default_factory=dict)   # adresse d'un bx rN -> valeur de lr posée juste avant
    long_branches: dict[int, int] = field(default_factory=dict)  # adresse d'un bl -> cible : branchement long, pas un appel


class Emitter:
    def __init__(self, ctx: Context) -> None:
        self.ctx = ctx
        self.tmp = 0

    # ------------------------------------------------------------------ aides
    @staticmethod
    def reg(n: int) -> str:
        return f"c->r[{n}]"

    def pc_value(self, i: Insn, extra: int = 0) -> int:
        if i.mode == ARM:
            return (i.addr + 8 + extra) & 0xFFFFFFFF
        v = i.addr + 4
        if i.extra.get("pc_aligned"):
            v &= ~3
        return v & 0xFFFFFFFF

    def rval(self, i: Insn, n: int, pc_extra: int = 0) -> str:
        if n == PC:
            return f"0x{self.pc_value(i, pc_extra):08X}u"
        return self.reg(n)

    def cond_wrap(self, i: Insn, body: list[str]) -> list[str]:
        if i.cond == AL or i.cond == 15:
            return body
        return [f"if ({COND_EXPR[i.cond]}) {{"] + ["    " + line for line in body] + ["}"]

    # ------------------------------------------------------------------ shifter
    def shifter(self, i: Insn, lines: list[str], want_carry: bool) -> str:
        """Émet le calcul de l'opérande décalé ; renvoie l'expression du résultat.
        Si want_carry, la retenue du shifter est laissée dans `sc`."""
        if i.imm is not None and i.rm is None:
            if want_carry:
                if i.imm_rotate:
                    lines.append(f"sc = {(i.imm >> 31) & 1}u;")
                else:
                    lines.append("sc = c->cf;")
            return f"0x{i.imm & 0xFFFFFFFF:08X}u"
        sh = i.shift
        rm = self.rval(i, i.rm, 4 if (sh and sh.reg is not None) else 0)
        if sh is None or (sh.kind == LSL and sh.amount == 0 and sh.reg is None):
            if want_carry:
                lines.append("sc = c->cf;")
            return rm
        if sh.reg is not None:
            fn = {LSL: "sh_lsl_reg", LSR: "sh_lsr_reg", ASR: "sh_asr_reg", ROR: "sh_ror_reg"}[sh.kind]
            lines.append("sc = c->cf;")
            lines.append(f"op2 = {fn}({rm}, {self.rval(i, sh.reg)}, &sc);")
            return "op2"
        n = sh.amount
        if sh.kind == RRX:
            if want_carry:
                lines.append(f"sc = {rm} & 1u;")
                lines.append(f"op2 = (c->cf << 31) | ({rm} >> 1);")
            else:
                lines.append(f"op2 = (c->cf << 31) | ({rm} >> 1);")
            return "op2"
        if sh.kind == LSL:
            if want_carry:
                lines.append(f"sc = ({rm} >> {32 - n}) & 1u;")
            return f"({rm} << {n})"
        if sh.kind == LSR:
            if n == 32:
                if want_carry:
                    lines.append(f"sc = {rm} >> 31;")
                return "0u"
            if want_carry:
                lines.append(f"sc = ({rm} >> {n - 1}) & 1u;")
            return f"({rm} >> {n})"
        if sh.kind == ASR:
            if n == 32:
                if want_carry:
                    lines.append(f"sc = {rm} >> 31;")
                return f"(uint32_t)((int32_t){rm} >> 31)"
            if want_carry:
                lines.append(f"sc = ({rm} >> {n - 1}) & 1u;")
            return f"(uint32_t)((int32_t){rm} >> {n})"
        if sh.kind == ROR:
            if want_carry:
                lines.append(f"sc = ({rm} >> {n - 1}) & 1u;")
            return f"ror32({rm}, {n})"
        raise ValueError(sh)

    # ------------------------------------------------------------------ écriture de pc
    def write_pc(self, i: Insn, expr: str, lines: list[str], interwork: bool, tail: bool = True) -> None:
        """`pc = expr` : saut local si la cible est connue, sinon rt_jump + return."""
        targets = self.ctx.switch_targets.get(i.addr)
        if targets:
            lines.append(f"switch ({expr}) {{")
            for t in sorted(set(targets)):
                if t in self.ctx.labels:
                    lines.append(f"    case 0x{t:08X}u: goto L_{t:08X};")
            lines.append(f"    default: c->r[15] = {expr}; rt_jump(c, {expr}); return;")
            lines.append("}")
            return
        lines.append(f"c->r[15] = {expr};")
        lines.append(f"rt_jump(c, {expr}); return;")

    # ------------------------------------------------------------------ point d'entrée
    def emit(self, i: Insn) -> list[str]:
        method = getattr(self, "op_" + i.op.lower(), None)
        if method is None:
            return [f"rt_undefined(c, 0x{i.addr:08X}u, 0x{i.raw:08X}u); return; /* {i.op} */"]
        body = method(i)
        return self.cond_wrap(i, body)

    # ------------------------------------------------------------------ données
    def _alu(self, i: Insn, kind: str) -> list[str]:
        lines: list[str] = []
        want_carry = i.s and kind in ("logic", "mov")
        op2 = self.shifter(i, lines, want_carry)
        rn = self.rval(i, i.rn, 4 if (i.shift and i.shift.reg is not None) else 0) if i.rn is not None else None
        op = i.op
        dest = i.rd
        if op in ("AND", "EOR", "ORR", "BIC"):
            expr = {"AND": f"{rn} & {op2}", "EOR": f"{rn} ^ {op2}", "ORR": f"{rn} | {op2}", "BIC": f"{rn} & ~{op2}"}[op]
            lines.append(f"res = {expr};")
            if i.s:
                lines.append("SET_NZ(c, res); c->cf = sc;")
        elif op in ("TST", "TEQ"):
            lines.append(f"res = {rn} {'&' if op == 'TST' else '^'} {op2};")
            lines.append("SET_NZ(c, res); c->cf = sc;")
            return lines
        elif op in ("MOV", "MVN"):
            lines.append(f"res = {'~' if op == 'MVN' else ''}{op2};")
            if i.s:
                lines.append("SET_NZ(c, res); c->cf = sc;")
        elif op in ("ADD", "ADC", "SUB", "SBC", "RSB", "RSC", "CMP", "CMN"):
            if op in ("ADD", "CMN"):
                lines.append(f"res = {rn} + {op2};")
                if i.s:
                    lines.append(f"flags_add(c, {rn}, {op2}, 0u, res);")
            elif op == "ADC":
                lines.append(f"res = {rn} + {op2} + c->cf;")
                if i.s:
                    lines.append(f"flags_add(c, {rn}, {op2}, c->cf, res);")
            elif op in ("SUB", "CMP"):
                lines.append(f"res = {rn} - {op2};")
                if i.s:
                    lines.append(f"flags_sub(c, {rn}, {op2}, 1u, res);")
            elif op == "SBC":
                lines.append(f"res = {rn} - {op2} - (1u - c->cf);")
                if i.s:
                    lines.append(f"flags_sub(c, {rn}, {op2}, c->cf, res);")
            elif op == "RSB":
                lines.append(f"res = {op2} - {rn};")
                if i.s:
                    lines.append(f"flags_sub(c, {op2}, {rn}, 1u, res);")
            elif op == "RSC":
                lines.append(f"res = {op2} - {rn} - (1u - c->cf);")
                if i.s:
                    lines.append(f"flags_sub(c, {op2}, {rn}, c->cf, res);")
            if op in ("CMP", "CMN"):
                return lines
        else:
            raise ValueError(op)
        if dest == PC:
            if i.s:
                # retour d'exception : cpsr = spsr
                lines.append("c->r[15] = res; rt_return_from_exception(c); return;")
            else:
                self.write_pc(i, "res", lines, interwork=False)
            return lines
        lines.append(f"{self.reg(dest)} = res;")
        return lines

    def op_and(self, i): return self._alu(i, "logic")
    def op_eor(self, i): return self._alu(i, "logic")
    def op_orr(self, i): return self._alu(i, "logic")
    def op_bic(self, i): return self._alu(i, "logic")
    def op_tst(self, i): return self._alu(i, "logic")
    def op_teq(self, i): return self._alu(i, "logic")
    def op_mov(self, i): return self._alu(i, "mov")
    def op_mvn(self, i): return self._alu(i, "mov")
    def op_add(self, i): return self._alu(i, "arith")
    def op_adc(self, i): return self._alu(i, "arith")
    def op_sub(self, i): return self._alu(i, "arith")
    def op_sbc(self, i): return self._alu(i, "arith")
    def op_rsb(self, i): return self._alu(i, "arith")
    def op_rsc(self, i): return self._alu(i, "arith")
    def op_cmp(self, i): return self._alu(i, "arith")
    def op_cmn(self, i): return self._alu(i, "arith")

    # ------------------------------------------------------------------ multiplications
    def op_mul(self, i):
        lines = [f"res = {self.reg(i.rm)} * {self.reg(i.rs)};"]
        if i.s:
            lines.append("SET_NZ(c, res);")
        lines.append(f"{self.reg(i.rd)} = res;")
        return lines

    def op_mla(self, i):
        lines = [f"res = {self.reg(i.rm)} * {self.reg(i.rs)} + {self.reg(i.rn)};"]
        if i.s:
            lines.append("SET_NZ(c, res);")
        lines.append(f"{self.reg(i.rd)} = res;")
        return lines

    def _mull(self, i, signed: bool, acc: bool):
        cast = "(int64_t)(int32_t)" if signed else "(uint64_t)"
        lines = [f"wide = (uint64_t)({cast}{self.reg(i.rm)} * {cast}{self.reg(i.rs)});"]
        if acc:
            lines.append(f"wide += ((uint64_t){self.reg(i.rd)} << 32) | {self.reg(i.rn)};")
        lines.append(f"{self.reg(i.rn)} = (uint32_t)wide; {self.reg(i.rd)} = (uint32_t)(wide >> 32);")
        if i.s:
            lines.append("c->nf = (uint32_t)(wide >> 63); c->zf = (wide == 0);")
        return lines

    def op_umull(self, i): return self._mull(i, False, False)
    def op_umlal(self, i): return self._mull(i, False, True)
    def op_smull(self, i): return self._mull(i, True, False)
    def op_smlal(self, i): return self._mull(i, True, True)

    def _half(self, reg: str, high: bool) -> str:
        return f"(int32_t)(int16_t)({reg} >> 16)" if high else f"(int32_t)(int16_t){reg}"

    def op_smulxy(self, i):
        return [f"{self.reg(i.rd)} = (uint32_t)({self._half(self.reg(i.rm), i.x)} * {self._half(self.reg(i.rs), i.y)});"]

    def op_smlaxy(self, i):
        return [f"res = (uint32_t)({self._half(self.reg(i.rm), i.x)} * {self._half(self.reg(i.rs), i.y)});",
                f"{self.reg(i.rd)} = add_setq(c, res, {self.reg(i.rn)});"]

    def op_smulwy(self, i):
        return [f"{self.reg(i.rd)} = (uint32_t)(((int64_t)(int32_t){self.reg(i.rm)} * {self._half(self.reg(i.rs), i.y)}) >> 16);"]

    def op_smlawy(self, i):
        return [f"res = (uint32_t)(((int64_t)(int32_t){self.reg(i.rm)} * {self._half(self.reg(i.rs), i.y)}) >> 16);",
                f"{self.reg(i.rd)} = add_setq(c, res, {self.reg(i.rn)});"]

    def op_smlalxy(self, i):
        return [f"wide = (uint64_t)(int64_t)({self._half(self.reg(i.rm), i.x)} * {self._half(self.reg(i.rs), i.y)});",
                f"wide += ((uint64_t){self.reg(i.rd)} << 32) | {self.reg(i.rn)};",
                f"{self.reg(i.rn)} = (uint32_t)wide; {self.reg(i.rd)} = (uint32_t)(wide >> 32);"]

    def op_qadd(self, i): return [f"{self.reg(i.rd)} = sat_add(c, {self.reg(i.rm)}, {self.reg(i.rn)});"]
    def op_qsub(self, i): return [f"{self.reg(i.rd)} = sat_sub(c, {self.reg(i.rm)}, {self.reg(i.rn)});"]
    def op_qdadd(self, i): return [f"{self.reg(i.rd)} = sat_add(c, {self.reg(i.rm)}, sat_double(c, {self.reg(i.rn)}));"]
    def op_qdsub(self, i): return [f"{self.reg(i.rd)} = sat_sub(c, {self.reg(i.rm)}, sat_double(c, {self.reg(i.rn)}));"]
    def op_clz(self, i): return [f"{self.reg(i.rd)} = clz32({self.reg(i.rm)});"]

    # ------------------------------------------------------------------ mémoire
    def address(self, i: Insn, lines: list[str]) -> str:
        """Calcule l'adresse effective dans `ea` et applique l'écriture arrière ; renvoie 'ea'."""
        base = self.rval(i, i.rn)
        if i.reg_offset:
            off = self.shifter(i, lines, False)
        else:
            off = f"0x{(i.imm or 0):X}u"
        sign = "+" if i.up else "-"
        if i.pre:
            lines.append(f"ea = {base} {sign} {off};")
            if i.writeback and i.rn != PC:
                lines.append(f"{self.reg(i.rn)} = ea;")
        else:
            lines.append(f"ea = {base};")
            if i.rn != PC:
                lines.append(f"{self.reg(i.rn)} = ea {sign} {off};")
        return "ea"

    def op_ldr(self, i):
        lines: list[str] = []
        ea = self.address(i, lines)
        w = i.width
        if w == "W":
            load = f"ldr_word(c, {ea})"
        elif w == "B":
            load = f"(uint32_t)mem_read8(c, {ea})"
        elif w == "H":
            load = f"ldr_h(c, {ea})"
        elif w == "SB":
            load = f"(uint32_t)(int32_t)(int8_t)mem_read8(c, {ea})"
        elif w == "SH":
            load = f"ldr_sh(c, {ea})"
        elif w == "D":
            lines.append(f"{self.reg(i.rd)} = mem_read32(c, {ea});")
            lines.append(f"{self.reg(i.rd + 1)} = mem_read32(c, {ea} + 4u);")
            return lines
        else:
            raise ValueError(w)
        if i.rd == PC:
            lines.append(f"res = {load};")
            self.write_pc(i, "res", lines, interwork=True)
            return lines
        lines.append(f"{self.reg(i.rd)} = {load};")
        return lines

    def op_str(self, i):
        lines: list[str] = []
        # la valeur est lue avant l'écriture arrière ; pc stocké vaut pc+12 en ARM
        src = self.rval(i, i.rd, 4) if i.rd == PC else self.reg(i.rd)
        if i.rd == i.rn and i.writeback:
            lines.append(f"val = {src};")
            src = "val"
        ea = self.address(i, lines)
        w = i.width
        if w == "W":
            lines.append(f"mem_write32(c, {ea}, {src});")
        elif w == "B":
            lines.append(f"mem_write8(c, {ea}, (uint8_t){src});")
        elif w == "H":
            lines.append(f"mem_write16(c, {ea}, (uint16_t){src});")
        elif w == "D":
            lines.append(f"mem_write32(c, {ea}, {self.reg(i.rd)});")
            lines.append(f"mem_write32(c, {ea} + 4u, {self.reg(i.rd + 1)});")
        else:
            raise ValueError(w)
        return lines

    def op_swp(self, i):
        return [f"ea = {self.reg(i.rn)};", f"res = ldr_word(c, ea);",
                f"mem_write32(c, ea, {self.reg(i.rm)});", f"{self.reg(i.rd)} = res;"]

    def op_swpb(self, i):
        return [f"ea = {self.reg(i.rn)};", f"res = mem_read8(c, ea);",
                f"mem_write8(c, ea, (uint8_t){self.reg(i.rm)});", f"{self.reg(i.rd)} = res;"]

    def _reglist(self, i: Insn) -> list[int]:
        return [n for n in range(16) if (i.reglist >> n) & 1]

    def op_ldm(self, i):
        regs = self._reglist(i)
        count = len(regs)
        lines: list[str] = []
        base = self.reg(i.rn)
        # adresse de départ selon le mode
        if i.up:
            lines.append(f"ea = {base}{' + 4u' if i.pre else ''};")
            final = f"{base} + {4 * count}u"
        else:
            lines.append(f"ea = {base} - {4 * count}u{' + 4u' if not i.pre else ''};")
            final = f"{base} - {4 * count}u"
        if i.writeback and i.rn not in regs:
            lines.append(f"{base} = {final};")
        user = i.user_bank and PC not in regs
        pc_loaded = False
        for k, n in enumerate(regs):
            if n == PC:
                lines.append(f"res = mem_read32(c, ea + {4 * k}u);")
                pc_loaded = True
            elif user and n in (13, 14):
                lines.append(f"user_reg_write(c, {n}, mem_read32(c, ea + {4 * k}u));")
            else:
                lines.append(f"{self.reg(n)} = mem_read32(c, ea + {4 * k}u);")
        if pc_loaded:
            if i.user_bank:
                lines.append("c->r[15] = res; rt_return_from_exception(c); return;")
            else:
                # POP/LDM vers pc est presque toujours un retour, mais peut aussi dépiler une
                # cible d'appel de queue — une petite fonction intermédiaire qui passe la main
                # à une autre. rt_jump les distingue par la pile fantôme : un vrai retour rend
                # la main à l'appelant, sinon la cible est exécutée. Les traiter tous comme des
                # retours coupait l'exécution avant le saut.
                self.write_pc(i, "res", lines, interwork=True)
        return lines

    def op_stm(self, i):
        regs = self._reglist(i)
        count = len(regs)
        lines: list[str] = []
        base = self.reg(i.rn)
        if i.up:
            lines.append(f"ea = {base}{' + 4u' if i.pre else ''};")
            final = f"{base} + {4 * count}u"
        else:
            lines.append(f"ea = {base} - {4 * count}u{' + 4u' if not i.pre else ''};")
            final = f"{base} - {4 * count}u"
        # si rn est dans la liste et n'est pas le premier, c'est la valeur écrite arrière qui est stockée
        wb_first = i.writeback and i.rn in regs and regs[0] != i.rn
        if wb_first:
            lines.append(f"val = {base};")
        for k, n in enumerate(regs):
            if n == PC:
                src = f"0x{self.pc_value(i, 4):08X}u"
            elif n == i.rn and wb_first:
                src = final
            elif i.user_bank and n in (13, 14):
                src = f"user_reg_read(c, {n})"
            else:
                src = self.reg(n)
            lines.append(f"mem_write32(c, ea + {4 * k}u, {src});")
        if i.writeback:
            lines.append(f"{base} = {final};")
        return lines

    # ------------------------------------------------------------------ branchements
    def op_b(self, i):
        t = i.target
        if t in self.ctx.labels:
            # arête arrière : point de livraison des interruptions en attente
            if t <= i.addr:
                # arête arrière : le temps virtuel avance par quanta d'arêtes, et les interruptions
                # en attente sont livrées (une interruption remet le budget à zéro)
                return ["if (--g_edge_budget < 0) rt_edge(c);", f"goto L_{t:08X};"]
            return [f"goto L_{t:08X};"]
        thumb = 1 if i.mode == THUMB else 0
        sym = self.ctx.symbol(t)
        if sym:
            return [f"{sym}(c, 0x{t:08X}u); return;"]
        return [f"c->r[15] = 0x{t | thumb:08X}u; rt_jump(c, 0x{t | thumb:08X}u); return;"]

    def _call(self, i: Insn, target: int, thumb_target: bool) -> list[str]:
        ret = (i.addr + i.size) | (1 if i.mode == THUMB else 0)
        lines = [f"c->r[14] = 0x{ret:08X}u;"]
        t = target & ~1
        sym = self.ctx.symbol(t, THUMB if thumb_target else ARM)
        if sym:
            lines.append(f"RT_CALL(c, 0x{ret:08X}u, {sym}(c, 0x{t:08X}u));")
        else:
            lines.append(f"RT_CALL(c, 0x{ret:08X}u, rt_call(c, 0x{t | (1 if thumb_target else 0):08X}u));")
        lines.append("if (c->redirect) goto redirect_dispatch;")
        return lines

    def op_bl(self, i):
        t = self.ctx.long_branches.get(i.addr)
        if t is not None:
            # `bl` employé comme branchement long (CodeWarrior, Thumb : `b` ne porte qu'à ±2 Ko) :
            # lr reçoit l'adresse de retour comme sur le vrai processeur, mais on continue en place
            ret = (i.addr + i.size) | 1
            lines = [f"c->r[14] = 0x{ret:08X}u;"]
            if t <= i.addr:
                lines.append("if (--g_edge_budget < 0) rt_edge(c);")
            lines.append(f"goto L_{t:08X};")
            return lines
        return self._call(i, i.target, i.mode == THUMB)

    def op_blx(self, i):
        if i.target is not None:
            return self._call(i, i.target, i.mode == ARM)  # blx imm change de mode
        ret = (i.addr + i.size) | (1 if i.mode == THUMB else 0)
        return [f"res = {self.rval(i, i.rm)};", f"c->r[14] = 0x{ret:08X}u;",
                f"RT_CALL(c, 0x{ret:08X}u, rt_call(c, res));",
                "if (c->redirect) goto redirect_dispatch;"]

    def op_bx(self, i):
        if i.rm == 14:
            return ["c->r[15] = c->r[14]; return;"]
        lines = [f"res = {self.rval(i, i.rm)};"]
        targets = self.ctx.switch_targets.get(i.addr)
        if targets:
            self.write_pc(i, "res & ~1u", lines, interwork=True)
            return lines
        lr = self.ctx.lr_before.get(i.addr)
        if lr is not None:
            # appel par registre avec adresse de retour choisie : on continue à lr
            lines.append(f"RT_CALL(c, 0x{lr:08X}u, rt_call(c, res));")
            lines.append("if (c->redirect) goto redirect_dispatch;")
            t = lr & ~1
            if t in self.ctx.labels:
                lines.append(f"goto L_{t:08X};")
            else:
                lines.append(f"c->r[15] = 0x{lr:08X}u; rt_jump(c, 0x{lr:08X}u); return;")
            return lines
        lines.append("c->r[15] = res; rt_jump(c, res); return;")
        return lines

    # ------------------------------------------------------------------ système
    def op_mrs(self, i):
        return [f"{self.reg(i.rd)} = {'spsr_read' if i.spsr else 'cpsr_read'}(c);"]

    def op_msr(self, i):
        src = f"0x{i.imm:08X}u" if i.imm is not None else self.reg(i.rm)
        return [f"{'spsr_write' if i.spsr else 'cpsr_write'}(c, {src}, {i.fields}u);"]

    def op_swi(self, i):
        return [f"rt_swi(c, 0x{i.swi:X}u);"]

    def op_bkpt(self, i):
        return [f"rt_bkpt(c, 0x{i.imm or 0:X}u, 0x{i.addr:08X}u);"]

    def op_mcr(self, i):
        if i.cp != 15:
            return [f"rt_undefined(c, 0x{i.addr:08X}u, 0x{i.raw:08X}u);"]
        return [f"rt_cp15_write(c, {i.opc1}u, {i.crn}u, {i.crm}u, {i.opc2}u, {self.rval(i, i.rd)});"]

    def op_mrc(self, i):
        if i.cp != 15:
            return [f"rt_undefined(c, 0x{i.addr:08X}u, 0x{i.raw:08X}u);"]
        if i.rd == PC:
            return [f"res = rt_cp15_read(c, {i.opc1}u, {i.crn}u, {i.crm}u, {i.opc2}u);",
                    "c->nf = res >> 31; c->zf = (res >> 30) & 1u; c->cf = (res >> 29) & 1u; c->vf = (res >> 28) & 1u;"]
        return [f"{self.reg(i.rd)} = rt_cp15_read(c, {i.opc1}u, {i.crn}u, {i.crm}u, {i.opc2}u);"]

    def op_pld(self, i):
        return ["/* pld */"]

    def op_nop(self, i):
        return ["/* nop */"]

    def op_undef(self, i):
        return [f"rt_undefined(c, 0x{i.addr:08X}u, 0x{i.raw:08X}u); return;"]

    def op_cdp(self, i): return self.op_undef(i)
    def op_ldc(self, i): return self.op_undef(i)
    def op_stc(self, i): return self.op_undef(i)

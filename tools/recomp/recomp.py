#!/usr/bin/env python3
"""Pilote de recompilation : cartographie + ROM → fichiers C.

Pour chaque région (arm9, itcm, ovlN), un fichier `generated/<région>.c`
contenant une fonction C par fonction cartographiée, plus `generated/tables.c`
(table de dispatch adresse → fonction) et `generated/functions.h`.

Le code généré est une traduction du binaire du jeu : il n'est pas versionné.

Usage :
    python tools/recomp/recomp.py <rom.nds> [--codemap analysis/codemap.json] [--out generated]
    python tools/recomp/recomp.py <rom.nds> --only 0x02019838   # une fonction, sur la sortie standard
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from nitro.memory import Memory  # noqa: E402
from nitro.rom import NdsRom  # noqa: E402
from recomp.armdec import decode_arm  # noqa: E402
from recomp.emit import Context, Emitter  # noqa: E402
from recomp.insn import ARM, THUMB, Insn  # noqa: E402
from recomp.thumbdec import decode_thumb  # noqa: E402

PROLOGUE = """/* Généré par tools/recomp — traduction du binaire du jeu, ne pas versionner. */
#include "cpu.h"
#include "functions.h"
#pragma GCC diagnostic ignored "-Wunused-label"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
"""


def decode_at(memory: Memory, addr: int, mode: int) -> Insn | None:
    if mode == ARM:
        w = memory.u32(addr)
        if w is None:
            return None
        return decode_arm(w, addr)
    hw = memory.u16(addr)
    if hw is None:
        return None
    hw2 = memory.u16(addr + 2)
    return decode_thumb(hw, addr, hw2)


def symbol_name(region: str, entry: int) -> str:
    """Nom C d'une fonction : les overlays sont préfixés (plusieurs partagent une adresse)."""
    if region.startswith("ovl"):
        return f"f_{region}_{entry:08X}"
    return f"f_{entry:08X}"


class FunctionTranslator:
    def __init__(self, memory: Memory, region: str, static_known: dict[int, int], region_known: dict[int, int],
                 names: dict[int, str], verbose: bool = False,
                 static_blocks: dict[int, tuple[int, int]] | None = None,
                 region_blocks: dict[int, tuple[int, int]] | None = None) -> None:
        self.memory = memory
        self.region = region
        self.static_known = static_known      # arm9 + itcm : entrée -> mode, toujours présents
        self.region_known = region_known      # fonctions de cette région : entrée -> mode
        self.static_blocks = static_blocks or {}   # début de bloc -> (entrée, mode), arm9 + itcm
        self.region_blocks = region_blocks or {}   # idem pour cette région
        self.names = names
        self.verbose = verbose                # un commentaire par instruction (fichiers bien plus gros)
        self.long_branch_count = 0   # bl employés comme branchements longs
        # Les deux processeurs se traduisent en deux jeux de tables qui cohabitent dans le même
        # exécutable : un suffixe distingue leurs symboles globaux.
        self.hooks_name = "g_fn_hooks"
        self.mode = THUMB
        self._code_pointers: set[int] | None = None
        self.call_targets: set[int] = set()   # cibles de bl/blx relevées par la cartographie

    def code_pointers(self) -> set[int]:
        """Adresses Thumb pointées par un mot de la mémoire visible.

        Une table de rappels, une table de méthodes, un pointeur rangé dans une structure : le
        jeu appelle ces adresses par registre. Beaucoup tombent au milieu d'une fonction générée
        — la cartographie les a absorbées dans une voisine — et sans étiquette à cet endroit le
        moteur n'a nulle part où entrer et s'arrête. Les étiqueter toutes ne change rien au code
        produit : cela ouvre seulement une porte d'entrée de plus, et supprime d'un coup une
        classe entière d'arrêts qu'on ne découvrait autrement qu'en jouant."""
        if self._code_pointers is None:
            found: set[int] = set()
            for reg in self.memory.regions:
                d = reg.data
                for off in range(0, len(d) - 3, 4):
                    v = int.from_bytes(d[off:off + 4], "little")
                    if v & 1:
                        found.add(v & ~1)
            self._code_pointers = found
        return self._code_pointers

    def symbol(self, addr: int, mode: int | None = None) -> str | None:
        """Nom appelable directement (même mode que l'appelant), ou None : dispatch à l'exécution."""
        if mode is None:
            mode = self.mode
        if addr in self.static_known and self.static_known[addr] == mode:
            return f"f_{addr:08X}"
        if self.region.startswith("ovl") and addr in self.region_known and self.region_known[addr] == mode:
            return symbol_name(self.region, addr)
        # début de bloc d'une fonction connue (deux fonctions qui se partagent du code, chute
        # dans la voisine) : on y entre directement par son `switch (entry)`, sans dispatch
        b = self.static_blocks.get(addr)
        if b is not None and b[1] == mode:
            return f"f_{b[0]:08X}"
        if self.region.startswith("ovl"):
            b = self.region_blocks.get(addr)
            if b is not None and b[1] == mode:
                return symbol_name(self.region, b[0])
        return None

    def translate(self, fn: dict, index: int = -1) -> str:
        entry = fn["entrée"]
        mode = THUMB if fn["mode"] == "thumb" else ARM
        self.mode = mode
        ranges = sorted((s, e) for s, e in fn["blocs_liste"])
        # fusion des plages qui se chevauchent
        merged: list[list[int]] = []
        for s, e in ranges:
            if merged and s <= merged[-1][1]:
                merged[-1][1] = max(merged[-1][1], e)
            else:
                merged.append([s, e])
        # décodage linéaire de chaque plage ; les débuts de bloc sont des ancrages
        # d'alignement : si une instruction de 4 octets (bl Thumb) enjambait un début
        # de bloc, on repart de ce début (deux alignements incompatibles : le premier
        # est du code mal attribué)
        block_starts = sorted({s for s, _ in ranges} | {entry})
        insns: list[Insn] = []
        for s, e in merged:
            a = s
            while a < e:
                i = decode_at(self.memory, a, mode)
                if i is None:
                    break
                nxt = next((b for b in block_starts if a < b < a + i.size), None)
                if nxt is not None:
                    a = nxt
                    continue
                insns.append(i)
                a += i.size
        def inside(a: int) -> bool:
            return any(lo <= a < hi for lo, hi in merged)

        insn_addrs = {i.addr for i in insns}
        labels = {s for s, _ in ranges}
        labels.add(entry)
        # cibles de branchements internes (un `b` hors de la fonction est un appel de queue)
        for i in insns:
            if i.target is not None and i.op == "B" and inside(i.target):
                labels.add(i.target)
        # adresses de retour d'appel : cibles possibles d'une redirection (reprise de thread)
        lr_before: dict[int, int] = {}
        prev: Insn | None = None
        for i in insns:
            if i.op in ("BL", "BLX") or (i.op == "BX" and i.rm != 14):
                labels.add(i.addr + i.size)
            if i.op == "BX" and i.rm != 14 and prev is not None and prev.addr + prev.size == i.addr:
                # mov lr, pc / ldr lr, [pc, #imm] / add lr, pc, #imm juste avant : appel par registre
                if prev.op == "MOV" and prev.rd == 14 and prev.rm == 15 and prev.imm is None:
                    lr_before[i.addr] = (prev.addr + 8) if prev.mode == ARM else (prev.addr + 4)
                elif prev.op == "LDR" and prev.rd == 14 and prev.rn == 15 and not prev.reg_offset:
                    base = (prev.addr + (8 if prev.mode == ARM else 4)) & ~3
                    v = self.memory.u32(base + (prev.imm or 0))
                    if v is not None:
                        lr_before[i.addr] = v
                elif prev.op == "ADD" and prev.rd == 14 and prev.rn == 15 and prev.imm is not None:
                    lr_before[i.addr] = (prev.addr + (8 if prev.mode == ARM else 4)) + prev.imm
            prev = i
        for v in lr_before.values():
            if inside(v & ~1):
                labels.add(v & ~1)
        switch_targets: dict[int, list[int]] = {}
        for i in insns:
            if i.op in ("BX", "ADD", "MOV", "LDR", "LDM") and i.addr in fn.get("switch", {}):
                switch_targets[i.addr] = fn["switch"][i.addr]
        # cibles de switch fournies par la cartographie (successeurs multiples des blocs)
        for s, targets in fn.get("sauts", {}).items():
            switch_targets[int(s)] = targets
            labels.update(t for t in targets if inside(t))
        # `add pc, pc, rX, lsl #2` en ARM : saut calculé, éventuellement en arrière (boucles
        # déroulées des routines de division) : toute instruction de la fonction est une cible
        for i in insns:
            if i.mode == ARM and i.op == "ADD" and i.rd == 15 and i.rn == 15 and i.rm is not None:
                labels |= insn_addrs
                switch_targets[i.addr] = sorted(insn_addrs)
        # Adresses appelées de l'extérieur : par registre depuis une table ou une structure, ou
        # directement par un `bl` que la cartographie a relevé. Les unes comme les autres doivent
        # pouvoir servir d'entrée même quand la cartographie les a absorbées dans cette fonction —
        # sinon le moteur n'a nulle part où entrer et s'arrête à la première qui est appelée.
        labels |= {p for p in self.code_pointers() if inside(p)}
        labels |= {t for t in self.call_targets if inside(t)}
        # une étiquette doit tomber sur une instruction décodée
        labels &= insn_addrs
        labels.add(entry)
        # `bl` employé comme branchement long (CodeWarrior, en Thumb, quand `b` ne porte pas) :
        # la cible est une instruction de cette même fonction, hors entrée, à plus de 2 Ko, et
        # l'instruction qui la précède y enchaîne par chute sans être un appel (une entrée de
        # fonction voisine fusionnée ne suivrait qu'un appel sans retour ou une fin de fonction)
        long_branches: dict[int, int] = {}
        prev_by_addr: dict[int, Insn] = {}
        prev = None
        for i in insns:
            prev_by_addr[i.addr] = prev
            prev = i
        # cibles que la cartographie a déjà reconnues (absorbées : tous leurs appelants sont cette
        # fonction, aucun `bl` à portée d'un `b`, pas de prologue)
        long_targets = {t for t in fn.get("cibles_longues", []) if t in insn_addrs and t != entry}
        for i in insns:
            if i.op == "BL" and i.mode == THUMB and i.target is not None:
                t = i.target & ~1
                if t in long_targets:
                    long_branches[i.addr] = t
                    labels.add(t)
                elif t in insn_addrs and t != entry and abs(t - i.addr) > 0x800:
                    p_ = prev_by_addr.get(t)
                    if p_ is not None and p_.addr + p_.size == t and p_.op not in ("BL", "BLX", "BX", "B", "SWI"):
                        long_branches[i.addr] = t
                        labels.add(t)
        self.long_branch_count += len(long_branches)
        ctx = Context(labels=labels, symbol=self.symbol, switch_targets=switch_targets, mode=mode,
                      lr_before=lr_before, long_branches=long_branches)
        em = Emitter(ctx)
        name = self.names.get(entry, "")
        out = [f"/* 0x{entry:08X} {'Thumb' if mode == THUMB else 'ARM'} {name} */",
               f"void {symbol_name(self.region, entry)}(Cpu *c, uint32_t entry) {{",
               "    uint32_t res, op2, sc, ea, val; uint64_t wide; (void)res; (void)op2; (void)sc; (void)ea; (void)val; (void)wide;"]
        if index >= 0:
            # Remplacement natif possible (HLE du SDK, code décompilé) : une charge, une
            # comparaison, un branchement. La comparaison n'est pas facultative — une fonction
            # a souvent plusieurs points d'entrée, et un crochet ne remplace que la fonction,
            # jamais un saut qui tombe en son milieu. Sans elle, le remplacement s'exécute à la
            # place d'un bout de code qui n'a rien à voir, et le jeu dérive sans rien signaler.
            hooks = self.hooks_name
            # Le crochet ne reçoit que le processeur : on lui laisse dans g_hook_entree
            # l'adresse qu'il remplace. Un crochet commun à mille fonctions — le bouchon du
            # banc à froid, qui tient lieu de toutes les appelées — sait ainsi laquelle.
            out.append(f"    if ({hooks}[{index}] && entry == 0x{entry:08X}u) "
                       f"{{ g_hook_entree = 0x{entry:08X}u; {hooks}[{index}](c); return; }}")
            # Passé le crochet, c'est la traduction qui tourne : on le compte, si la construction le
            # demande (SFC_COMPTE_TRADUIT), en distinguant l'entrée de la fonction d'une entrée en
            # son milieu. Avec tout le jeu en natif, c'est la mesure de ce qui dépend encore d'elle.
            if hooks == "g_fn_hooks":
                out.append(f"    RT_TRADUIT({index}, entry == 0x{entry:08X}u);")
            elif hooks == "g_fn7_hooks":
                out.append(f"    RT_TRADUIT7({index}, entry == 0x{entry:08X}u);")
        local_labels = sorted(l for l in labels if inside(l))
        out.append("    switch (entry) {")
        for l in local_labels:
            out.append(f"    case 0x{l:08X}u: goto L_{l:08X};")
        out.append("    default: rt_unlabeled(c, entry); return;")
        out.append("    }")
        out.append("    if (0) { redirect_dispatch: entry = c->redirect; c->redirect = 0; switch (entry) {")
        for l in local_labels:
            out.append(f"        case 0x{l:08X}u: goto L_{l:08X};")
        out.append("        default: rt_jump(c, entry); return; } }")
        # Une plage qui se termine sans instruction de contrôle enchaîne dans une autre
        # fonction (entrée qui a coupé le bloc, ou chute dans la fonction suivante) :
        # on continue par un saut de queue, résolu par le dispatch si besoin.
        def continue_at(addr: int) -> str:
            sym = self.symbol(addr)
            if sym:
                return f"    {sym}(c, 0x{addr:08X}u); return; /* suite dans une autre fonction */"
            return f"    c->r[15] = 0x{addr | (1 if mode == THUMB else 0):08X}u; rt_jump(c, 0x{addr | (1 if mode == THUMB else 0):08X}u); return;"

        prev_end = None
        for i in insns:
            if prev_end is not None and i.addr != prev_end:
                out.append(continue_at(prev_end))
            if i.addr in labels:
                out.append(f"L_{i.addr:08X}:")
            if self.verbose:
                out.append(f"    PC(c, 0x{i.addr:08X}u); /* {i} */")
            else:
                out.append(f"    PC(c, 0x{i.addr:08X}u);")
            for line in em.emit(i):
                out.append("    " + line)
            prev_end = i.addr + i.size
        out.append(continue_at(prev_end or entry))
        out.append("}")
        return "\n".join(out)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("rom")
    ap.add_argument("--codemap", default="analysis/codemap.json")
    ap.add_argument("--out", default="generated")
    ap.add_argument("--only", default=None, help="adresse d'une seule fonction (hex)")
    ap.add_argument("--regions", default="all")
    ap.add_argument("--verbose", action="store_true", help="un commentaire par instruction")
    ap.add_argument("--suffixe", default="",
                    help="suffixe des symboles de tables (ex. 7 : g_recomp7_table, g_fn7_hooks) — "
                         "permet de lier côte à côte la traduction des deux processeurs")
    args = ap.parse_args()

    rom = NdsRom.load(args.rom)
    memory = Memory(rom)
    codemap = json.load(open(args.codemap, encoding="utf-8"))
    # le coprocesseur a son propre espace mémoire : une carte qui le contient se traduit avec
    # la mémoire de l'ARM7, jamais avec celle de l'ARM9
    memory7 = Memory(rom, cpu="arm7") if "arm7" in codemap else None
    names: dict[int, str] = {}
    static_known: dict[int, int] = {}
    region_known: dict[str, dict[int, int]] = {}
    static_blocks: dict[int, tuple[int, int]] = {}
    region_blocks: dict[str, dict[int, tuple[int, int]]] = {}
    for region, fns in codemap.items():
        region_known[region] = {f["entrée"]: (THUMB if f["mode"] == "thumb" else ARM) for f in fns}
        blocks: dict[int, tuple[int, int]] = {}
        for f in fns:
            m = THUMB if f["mode"] == "thumb" else ARM
            for start, _end in f.get("blocs_liste", []):
                blocks.setdefault(start, (f["entrée"], m))
        region_blocks[region] = blocks
        if not region.startswith("ovl"):
            static_known.update(region_known[region])
            for k, v in blocks.items():
                static_blocks.setdefault(k, v)
        for f in fns:
            if f.get("nom"):
                names[f["entrée"]] = f["nom"]

    if args.only:
        addr = int(args.only, 16)
        wanted = None if args.regions == "all" else set(args.regions.split(","))
        for region, fns in codemap.items():
            if wanted is not None and region not in wanted:
                continue  # plusieurs overlays partagent une adresse : --regions choisit
            for f in fns:
                if f["entrée"] == addr:
                    if region.startswith("ovl"):
                        memory.map_overlay(int(region[3:]))
                    print(FunctionTranslator(memory, region, static_known, region_known[region], names, True,
                                             static_blocks, region_blocks[region]).translate(f))
                    return 0
        print("fonction inconnue", file=sys.stderr)
        return 1

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    regions = list(codemap) if args.regions == "all" else args.regions.split(",")
    sfx = args.suffixe
    hooks_name, table_name = f"g_fn{sfx}_hooks", f"g_recomp{sfx}_table"
    header = ["/* Généré par tools/recomp. */", "#pragma once", '#include "cpu.h"',
              "typedef void (*NativeHook)(Cpu *c);", f"extern NativeHook {hooks_name}[];"]
    table = ["/* Généré par tools/recomp. */", '#include "cpu.h"', '#include "functions.h"',
             "typedef struct { uint32_t start, end, entry; RecompFn fn; const char *region; uint8_t thumb; } RecompEntry;",
             f"const RecompEntry {table_name}[] = {{"]
    total = 0
    for region in regions:
        fns = codemap[region]
        mem = memory
        if region == "arm7":
            mem = memory7
        elif region.startswith("ovl"):
            memory.map_overlay(int(region[3:]))
        else:
            memory.unmap_overlay()
        tr = FunctionTranslator(mem, region, static_known, region_known[region], names, args.verbose,
                                static_blocks, region_blocks[region])
        tr.hooks_name = hooks_name
        # Cibles d'appel relevées par la cartographie. Beaucoup sont déjà des entrées de
        # fonction ; celles qu'une voisine a absorbées n'ont sans cela aucune porte d'entrée.
        tr.call_targets = {t & ~1 for f in fns for t in f.get("appels", ())}
        chunks = [PROLOGUE]
        for f in sorted(fns, key=lambda f: f["entrée"]):
            chunks.append(tr.translate(f, total))
            sym = symbol_name(region, f["entrée"])
            header.append(f"void {sym}(Cpu *c, uint32_t entry);")
            table.append(f'    {{0x{f["début"]:08X}u, 0x{f["fin"]:08X}u, 0x{f["entrée"]:08X}u, {sym}, "{region}", {1 if f["mode"] == "thumb" else 0}}},')
            total += 1
        (out / f"{region}.c").write_text("\n\n".join(chunks) + "\n", encoding="utf-8")
        print(f"{region}: {len(fns)} fonctions, {tr.long_branch_count} bl-branchements longs -> {out / (region + '.c')}")
    table.append("};")
    table.append(f"const size_t {table_name}_size = sizeof({table_name}) / sizeof({table_name}[0]);")
    # l'ordre de la table est celui des indices des crochets ; le dispatch trie à part
    table.append(f"NativeHook {hooks_name}[{total}];")
    (out / "functions.h").write_text("\n".join(header) + "\n", encoding="utf-8")
    (out / "tables.c").write_text("\n".join(table) + "\n", encoding="utf-8")
    print(f"{total} fonctions au total")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Cartographie du code de Star Fox Command : fonctions ARM/Thumb de l'ARM9,
de l'ITCM et de chaque overlay.

Usage :
    python tools/codemap.py <rom.nds> [-o analysis/codemap.json]
"""
from __future__ import annotations

import argparse
import json
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from nitro.codemap import ARM, LEVEL, THUMB, CodeMap  # noqa: E402
from nitro.memory import Memory  # noqa: E402
from nitro.rom import NdsRom  # noqa: E402

ARM9_ENTRY = 0x02000800
IRQ_HANDLER = 0x01FF81BC
NITRO_MAIN = 0x02000C24
ARM9_CTORS = 0x020AB584
# Fin approximative du code de l'ARM9 : au-delà, ce sont les données (.rodata/.data).
ARM9_CODE_END = 0x020A6000


def map_arm7(rom: NdsRom, args, manual: dict, why: int | None) -> int:
    """Cartographie du binaire du coprocesseur ARM7.

    Il n'a pas d'overlay, mais il a des sections : son crt0 tient en quelques centaines
    d'octets à l'adresse de chargement, et recopie le reste ailleurs — l'essentiel en WRAM,
    d'où tournent son gestionnaire d'interruption et son pilote son, le reste en fin de RAM
    principale. Toutes cohabitent : on les explore ensemble, et la carte n'en fait qu'une
    région puisque rien ne s'y recouvre.

    Son point d'entrée est la seule amorce certaine : le saut vers le code recopié se fait
    par registre, donc les amorces manuelles (analysis/amorces.txt, que l'exécution alimente)
    prennent le relais."""
    memory = Memory(rom, cpu="arm7")
    cm = CodeMap(memory)
    cm.watch = why
    cm.seed(rom.header.arm7_entry & ~1, THUMB if rom.header.arm7_entry & 1 else ARM, "entrée")
    cm.names[rom.header.arm7_entry & ~1] = "_start"
    for a in manual.get("arm7", set()):
        cm.seed(a & ~1, THUMB if a & 1 else ARM, "manuel")
    t0 = time.time()
    cm.run([r.name for r in memory.static])
    if why is not None and cm.in_code(why):
        cm.explain(why)
    result = {"arm7": export(cm, "arm7")}
    total = cov = pools = 0
    lines = []
    for r in memory.static:
        c, p = coverage(cm, r.base, r.end)
        cov += c
        pools += p
        total += len(r.data)
        lines.append(f"  {r.name:10s} 0x{r.base:08X} : {c} + {p} sur {len(r.data)}")
    print(f"ARM7 : {len(cm.functions)} fonctions en {time.time() - t0:.1f}s ; "
          f"couverture {cov} octets d'instructions + {pools} de pools sur {total}")
    print("\n".join(lines))
    Path(args.out).parent.mkdir(parents=True, exist_ok=True)
    Path(args.out).write_text(json.dumps(result, ensure_ascii=False), encoding="utf-8")
    print(f"-> {args.out}")
    report = Path(args.out).with_name("sans_etiquette_arm7.txt")
    report.write_text("\n".join(unlabeled_report(cm, "arm7")) + "\n", encoding="utf-8")
    return 0


def seed_static(cm: CodeMap, memory: Memory, registries: dict) -> None:
    cm.seed(ARM9_ENTRY, ARM, "entrée")
    cm.seed(IRQ_HANDLER, ARM, "irq")
    cm.seed(NITRO_MAIN, THUMB, "NitroMain")
    cm.names[NITRO_MAIN] = "NitroMain"
    cm.names[IRQ_HANDLER] = "OS_IrqHandler"
    cm.names[ARM9_ENTRY] = "_start"
    addr = ARM9_CTORS
    while (v := memory.u32(addr)):
        cm.seed(v, None, "ctor")
        addr += 4
    for rec in registries.get("ARM9", []):
        cm.seed(rec["fonction"] | (1 if rec["thumb"] else 0), None, "registre")
        cm.names[rec["fonction"]] = "cmd_" + rec["nom"]


def export(cm: CodeMap, label: str) -> list[dict]:
    out = []
    for fn in sorted(cm.functions.values(), key=lambda f: f.entry):
        out.append({
            "entrée": fn.entry, "mode": "thumb" if fn.mode == THUMB else "arm",
            "début": fn.start, "fin": fn.end, "taille": fn.size, "blocs": len(fn.blocks),
            "origine": fn.origin, "nom": cm.names.get(fn.entry, ""),
            "appels": sorted(fn.calls),
            "blocs_liste": sorted((b.start, b.end) for b in fn.blocks.values()),
            "sauts": {str(k): v for k, v in fn.switches.items()},
            "cibles_longues": sorted(fn.long_targets),   # cibles de `bl` employés comme branchements longs
        })
    return out


def unlabeled_report(cm: CodeMap, region: str, lo: int = 0, hi: int = 1 << 32) -> list[str]:
    """Sorties de bloc que le code généré ne saura pas résoudre (arrêt fatal si exécutées) :
    fonction, bloc, cible, et ce qu'il y a à la cible."""
    out = []
    for entry, block, target, owner in sorted(cm.unlabeled):
        if not (lo <= entry < hi):
            continue
        fn = cm.functions.get(entry)
        if owner is None:
            what = "non cartographié"
        else:
            o = cm.functions.get(owner)
            if o is None:
                what = f"propriétaire 0x{owner:08X} absent"
            elif o.mode != fn.mode if (fn := cm.functions.get(entry)) else False:
                what = f"dans {o.origin} 0x{owner:08X} (autre mode)"
            else:
                what = f"dans {o.origin} 0x{owner:08X} (hors phase)"
        mode = "thumb" if fn and fn.mode == THUMB else "arm"
        out.append(f"{region:<6} 0x{entry:08X} ({fn.origin if fn else '?'}, {mode}) bloc 0x{block:08X} -> 0x{target:08X} : {what}")
    return out


def coverage(cm: CodeMap, base: int, end: int) -> tuple[int, int]:
    covered = sum(2 for a in cm.covered if base <= a < end)
    pools = sum(4 for p in cm.pools if base <= p < end)
    return covered, pools


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("rom")
    ap.add_argument("-o", "--out", default="analysis/codemap.json")
    ap.add_argument("--registries", default="docs/registres.json")
    ap.add_argument("--overlays", default="all", help="'all', 'none' ou liste 0,3,5")
    ap.add_argument("--arm7", action="store_true",
                    help="cartographier le binaire du coprocesseur ARM7 au lieu de l'ARM9 et des overlays")
    ap.add_argument("--why", default=None, help="adresse (hexa) dont on veut expliquer la couverture")
    ap.add_argument("--seeds", default="analysis/amorces.txt",
                    help="amorces manuelles « région 0xADDR » (bit 0 : Thumb), écrites par le runtime à chaque arrêt")
    args = ap.parse_args()
    manual: dict[str, set[int]] = {}
    if Path(args.seeds).is_file():
        for line in Path(args.seeds).read_text(encoding="utf-8").splitlines():
            parts = line.split()
            if len(parts) == 2 and parts[1].startswith("0x"):
                manual.setdefault(parts[0], set()).add(int(parts[1], 16))
        print(f"  {sum(len(v) for v in manual.values())} amorces manuelles lues dans {args.seeds}")
    why = (int(args.why, 16) & ~1) if args.why else None

    rom = NdsRom.load(args.rom)
    if args.arm7:
        return map_arm7(rom, args, manual, why)
    memory = Memory(rom)
    registries = json.load(open(args.registries, encoding="utf-8")) if Path(args.registries).is_file() else {}

    t0 = time.time()
    cm = CodeMap(memory)
    cm.watch = why
    seed_static(cm, memory, registries)
    for a in manual.get("arm9", set()) | manual.get("itcm", set()):
        cm.seed(a & ~1, THUMB if a & 1 else ARM, "manuel")
    cm.run(["arm9", "itcm"], {"arm9": ARM9_CODE_END - 0x02000000})
    if why is not None and cm.in_code(why):
        cm.explain(why)
    result = {"arm9": export(cm, "arm9")}
    stats = cm.summary()
    print(f"ARM9+ITCM : {len(cm.functions)} fonctions en {time.time() - t0:.1f}s")
    for name, s in stats.items():
        print(f"  {name:<5} {s['fonctions']:5d} fonctions ({s['arm']} ARM, {s['thumb']} Thumb, "
              f"{s['faibles']} conjecturales), {s['octets']} octets")
    cov, pools = coverage(cm, 0x02000000, ARM9_CODE_END)
    print(f"  couverture ARM9 code : {cov} octets d'instructions + {pools} de pools sur {ARM9_CODE_END - 0x02000000}")
    cov, pools = coverage(cm, 0x01FF8000, 0x01FFFB40)
    print(f"  couverture ITCM : {cov} + {pools} sur {0x7B40}")
    print(f"  {len(cm.switch_sites)} sites de saut par registre")
    print(f"  réparation : {cm.repaired[0]} fonctions ré-explorées, {cm.repaired[1]} blocs coupés, "
          f"{cm.repaired[2]} sorties sans étiquette")

    unlabeled_overlays: list[str] = []
    static_calls: set[int] = set()      # appels des overlays vers le code statique (bit 0 : Thumb)
    static_functions = dict(cm.functions)
    static_covered = dict(cm.covered)
    static_owner = dict(cm.owner)
    static_pools = dict(cm.pools)
    static_entries = set(cm.entries)
    # appels directs (bl/blx) du code statique vers des adresses d'overlay : le lieur les a
    # résolus, ce sont des amorces sûres pour l'overlay qui les contient
    external_calls = {t for f in static_functions.values() for t in f.calls if not cm.in_code(t & ~1)}
    print(f"  {len(external_calls)} appels directs vers des overlays")

    if args.overlays != "none":
        wanted = None if args.overlays == "all" else {int(x) for x in args.overlays.split(",")}
        overlays = [ov for ov in rom.overlays9 if wanted is None or ov.overlay_id in wanted]
        images = {ov.overlay_id: rom.overlay_data(ov) for ov in overlays}

        ranges = {ov.overlay_id: (ov.ram_address, ov.ram_address + ov.ram_size) for ov in overlays}

        def pointers_into(ov, exclude_id):
            """Mots des autres overlays qui pointent dans `ov` (bit 0 : Thumb). Les overlays du
            même emplacement (plages qui se chevauchent) sont exclus : leurs pointeurs internes
            tombent dans la même plage sans la viser."""
            lo, hi = ov.ram_address, ov.ram_address + ov.ram_size
            found = set()
            for oid, img in images.items():
                olo, ohi = ranges[oid]
                if oid == exclude_id or (olo < hi and lo < ohi):
                    continue
                for off in range(0, len(img) - 3, 4):
                    v = struct.unpack_from("<I", img, off)[0]
                    if lo <= (v & ~1) < hi:
                        found.add(v)
            return found

        # deux tours : les appels et pointeurs d'un overlay vers un autre ne sont connus
        # qu'après la première passe sur celui qui les contient
        cross_calls: set[int] = set(external_calls)
        cross_ptrs: dict[int, set[int]] = {}
        for round_no in range(2):
            new_calls: set[int] = set()
            for ov in overlays:
                memory.map_overlay(ov.overlay_id)
                ocm = CodeMap(memory)
                # emplacement partagé : les plages des autres overlays qui chevauchent celui-ci
                ocm.shared = [(o.ram_address, o.ram_address + o.ram_size) for o in rom.overlays9
                              if o.overlay_id != ov.overlay_id and o.ram_address < ov.ram_address + ov.ram_size
                              and ov.ram_address < o.ram_address + o.ram_size]
                ocm.functions = dict(static_functions)
                ocm.covered = dict(static_covered)
                ocm.owner = dict(static_owner)
                ocm.pools = dict(static_pools)
                ocm.entries = set(static_entries)
                ocm.names = dict(cm.names)
                for a in range(ov.static_init_start, ov.static_init_end, 4):
                    v = memory.u32(a)
                    if v:
                        ocm.seed(v, None, "ctor")
                for rec in registries.get(f"Overlay {ov.overlay_id}", []):
                    ocm.seed(rec["fonction"] | (1 if rec["thumb"] else 0), None, "registre")
                for a in manual.get(f"ovl{ov.overlay_id}", set()):
                    ocm.seed(a & ~1, THUMB if a & 1 else ARM, "manuel")
                lo, hi = ov.ram_address, ov.ram_address + ov.ram_size
                if why is not None and lo <= why < hi:
                    ocm.watch = why
                    print(f"  --- ovl{ov.overlay_id}, tour {round_no + 1} : explication de 0x{why:08X}")
                t1 = time.time()
                # 1. le noyau : constructeurs et registres de l'overlay, et tout ce qu'ils atteignent
                ocm.code_end = {"arm9": ARM9_CODE_END - 0x02000000}
                ocm.drain()
                # 2. les amorces venues d'ailleurs : appels croisés, pointeurs des autres overlays,
                # pointeurs de l'ARM9 (tables de rappels, vtables). Une adresse d'un emplacement
                # partagé (overlays 2-4, 5-22, 23-25) ne désigne qu'un seul overlay : dans les autres,
                # elle tombe au milieu d'une fonction, voire d'une instruction. On les sème donc par
                # adresse croissante et seulement dans les trous : une fonction explorée depuis son
                # vrai début couvre les fausses amorces qui la suivent avant qu'elles ne la coupent.
                external: list[tuple[int, int, str]] = []
                for t in cross_calls:
                    if lo <= (t & ~1) < hi:
                        external.append((t & ~1, THUMB if t & 1 else ARM, "bl"))
                if round_no == 1:
                    if ov.overlay_id not in cross_ptrs:
                        cross_ptrs[ov.overlay_id] = pointers_into(ov, ov.overlay_id)
                    for v in cross_ptrs[ov.overlay_id]:
                        if v & 1:
                            external.append((v & ~1, THUMB, "ptr"))
                        else:
                            ocm.arm_candidates.add(v)
                ocm.seed_pointers(["arm9"], collect=external)
                for addr, mode, origin in sorted(set(external), key=lambda e: (e[0], -LEVEL[e[2]], e[1])):
                    ocm.seed(addr, mode, origin, gap_only=True)
                    ocm.drain()
                # 3. pointeurs internes, candidats ARM, prologues dans les trous
                ocm.run([f"ovl{ov.overlay_id}"])
                if ocm.watch is not None:
                    ocm.explain(why)
                new = {a: f for a, f in ocm.functions.items() if a not in static_functions}
                in_ovl = {a: f for a, f in new.items() if lo <= a < hi}
                for f in in_ovl.values():
                    for t in f.calls:
                        if cm.in_code(t & ~1):
                            static_calls.add(t)          # vers l'ARM9 ou l'ITCM
                        elif not (lo <= (t & ~1) < hi):
                            new_calls.add(t)
                ocm.functions = in_ovl
                result[f"ovl{ov.overlay_id}"] = export(ocm, f"ovl{ov.overlay_id}")
                if round_no == 1:
                    unlabeled_overlays += unlabeled_report(ocm, f"ovl{ov.overlay_id}", lo, hi)
                # octets réellement couverts : l'union des blocs (la taille fin - début d'une
                # fonction dont un morceau est loin engloberait le code voisin)
                spans = sorted((b.start, b.end) for f in in_ovl.values() for b in f.blocks.values())
                total = 0
                cur_s = cur_e = None
                for bs, be in spans:
                    if cur_e is None or bs > cur_e:
                        if cur_e is not None:
                            total += cur_e - cur_s
                        cur_s, cur_e = bs, be
                    else:
                        cur_e = max(cur_e, be)
                if cur_e is not None:
                    total += cur_e - cur_s
                print(f"tour {round_no + 1} ovl{ov.overlay_id:<2} : {len(in_ovl):5d} fonctions, {total:7d} octets sur {ov.ram_size:7d} "
                      f"({100 * total // max(1, ov.ram_size):3d} %), {time.time() - t1:.1f}s ; réparation "
                      f"{ocm.repaired[0]} ré-explorées, {ocm.repaired[1]} blocs coupés, {ocm.repaired[2]} sans étiquette ; "
                      f"{len(ocm.phantoms)} amorces écartées dont {sum(len(f.long_targets) for f in in_ovl.values())} cibles de bl longs")
            added = new_calls - cross_calls
            print(f"  tour {round_no + 1} : {len(added)} nouveaux appels croisés entre overlays")
            cross_calls |= new_calls
            if round_no == 0 and not added and False:
                break

        # tour statique final : les fonctions de l'ARM9 et de l'ITCM que seuls les overlays
        # appellent ou désignent (accesseurs, méthodes de classes de base, rappels) n'étaient
        # atteintes par rien lors de la cartographie statique, faite avant celle des overlays
        static_ptrs: set[int] = set()
        for img in images.values():
            for off in range(0, len(img) - 3, 4):
                v = struct.unpack_from("<I", img, off)[0]
                if v & 1 and cm.in_code(v & ~1):
                    static_ptrs.add(v)
                elif v and not v & 3 and cm.in_code(v):
                    cm.arm_candidates.add(v)
        memory.unmap_overlay()
        before = len(cm.functions)
        for t in sorted(static_calls):
            cm.seed(t & ~1, THUMB if t & 1 else ARM, "bl")
        for v in sorted(static_ptrs):
            cm.seed(v & ~1, THUMB, "ptr")
        cm.unlabeled = []
        cm.run(["arm9", "itcm"], {"arm9": ARM9_CODE_END - 0x02000000})
        if why is not None and cm.in_code(why):
            cm.explain(why)
        result["arm9"] = export(cm, "arm9")
        print(f"tour final ARM9+ITCM : {len(cm.functions) - before} fonctions ajoutées "
              f"({len(static_calls)} appels et {len(static_ptrs)} pointeurs venus des overlays) ; "
              f"{len(cm.functions)} au total")

    unlabeled = unlabeled_report(cm, "arm9") + unlabeled_overlays
    Path(args.out).parent.mkdir(parents=True, exist_ok=True)
    Path(args.out).write_text(json.dumps(result, ensure_ascii=False), encoding="utf-8")
    print(f"-> {args.out}")
    report = Path(args.out).with_name("sans_etiquette.txt")
    report.write_text("\n".join(unlabeled) + "\n", encoding="utf-8")
    print(f"-> {report} ({len(unlabeled)} sorties de bloc sans étiquette possible)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

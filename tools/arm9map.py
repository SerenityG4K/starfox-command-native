#!/usr/bin/env python3
"""Carte du binaire de Star Fox Command : mémoire, overlays, registres.

Le jeu enregistre ses commandes de script et ses comportements d'ennemis dans
des tables d'enregistrements de douze octets : pointeur vers le nom, pointeur
vers la fonction (bit 0 levé pour du Thumb), pointeur de chaînage. L'ARM9 en
porte soixante ; chaque overlay enregistre les siens.

Ces tables donnent, pour chaque commande de script et chaque callback, l'adresse
de son implémentation d'origine : le point de départ de la décompilation.

Usage :
    python tools/arm9map.py <rom.nds> -o docs/carte_binaire.md
"""
from __future__ import annotations

import argparse
import json
import sys
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from nitro.memory import Memory  # noqa: E402
from nitro.rom import NdsRom  # noqa: E402

RECORD = 12


def find_registries(memory: Memory, region_name: str) -> list[dict]:
    """Enregistrements {nom, fonction, lien} dans une région, groupés en tables."""
    region = next(r for r in memory.regions if r.name == region_name)
    records = []
    for offset in range(0, len(region.data) - RECORD, 4):
        addr = region.base + offset
        name_ptr = memory.u32(addr)
        fn_ptr = memory.u32(addr + 4)
        if name_ptr is None or fn_ptr is None:
            continue
        if not memory.is_code(fn_ptr) or not memory.region_of(name_ptr):
            continue
        name = memory.cstring(name_ptr)
        if not name or len(name) < 3 or not (name[0].isalpha() or name[0] == "_"):
            continue
        records.append({"addr": addr, "name": name, "fn": fn_ptr, "link": memory.u32(addr + 8)})

    # Regroupe les enregistrements contigus (pas de 12 octets) en tables.
    tables: list[list[dict]] = []
    for rec in records:
        if tables and rec["addr"] - tables[-1][-1]["addr"] == RECORD:
            tables[-1].append(rec)
        else:
            tables.append([rec])
    return [t for t in tables if len(t) >= 3]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("rom")
    ap.add_argument("-o", "--out", default="docs/carte_binaire.md")
    ap.add_argument("--json", default="docs/registres.json")
    args = ap.parse_args()

    rom = NdsRom.load(args.rom)
    memory = Memory(rom)
    h = rom.header

    lines = ["# Carte du binaire — Star Fox Command (Europe)", "",
             "Générée par `tools/arm9map.py`. Adresses en RAM, telles que le jeu les voit.", "",
             "## Binaires", "",
             "| Binaire | RAM | Taille | Entrée |", "|---|---|---:|---|",
             f"| ARM9 (décompressé) | `0x{h.arm9_ram_address:08X}` | {len(rom.arm9())} | `0x{h.arm9_entry:08X}` |",
             f"| ARM7 | `0x{h.arm7_ram_address:08X}` | {h.arm7_size} | `0x{h.arm7_entry:08X}` |", "",
             "## Overlays ARM9", "",
             "Quatre emplacements ; les overlays d'un même emplacement s'excluent.", "",
             "| Overlay | RAM | Taille | BSS | Init statique | Fichier |", "|---:|---|---:|---:|---|---:|"]
    for ov in rom.overlays9:
        lines.append(f"| {ov.overlay_id} | `0x{ov.ram_address:08X}` | {ov.ram_size} | {ov.bss_size} | "
                     f"`0x{ov.static_init_start:08X}`–`0x{ov.static_init_end:08X}` | {ov.file_id} |")

    all_entries: dict[str, list[dict]] = defaultdict(list)
    summary = []

    def dump_tables(label: str, region_name: str) -> None:
        tables = find_registries(memory, region_name)
        total = sum(len(t) for t in tables)
        summary.append((label, len(tables), total))
        if not tables:
            return
        lines.extend(["", f"### {label} — {total} enregistrements dans {len(tables)} table(s)", ""])
        for table in tables:
            lines.append(f"Table `0x{table[0]['addr']:08X}`–`0x{table[-1]['addr']:08X}` ({len(table)}) :")
            lines.append("")
            lines.append("| Nom | Fonction | Mode |")
            lines.append("|---|---|---|")
            for rec in table:
                mode = "Thumb" if rec["fn"] & 1 else "ARM"
                lines.append(f"| `{rec['name']}` | `0x{rec['fn'] & ~1:08X}` | {mode} |")
                all_entries[label].append({"nom": rec["name"], "fonction": rec["fn"] & ~1,
                                           "thumb": bool(rec["fn"] & 1), "enregistrement": rec["addr"]})
            lines.append("")

    lines.extend(["", "## Registres de commandes et de callbacks", "",
                  "Enregistrements de 12 octets : pointeur vers le nom, pointeur vers la fonction "
                  "(bit 0 levé pour du Thumb), pointeur de chaînage.", ""])
    dump_tables("ARM9", "arm9")
    for ov in rom.overlays9:
        memory.map_overlay(ov.overlay_id)
        dump_tables(f"Overlay {ov.overlay_id}", f"ovl{ov.overlay_id}")

    lines.extend(["", "## Résumé", "", "| Binaire | Tables | Enregistrements |", "|---|---:|---:|"])
    for label, tables, total in summary:
        lines.append(f"| {label} | {tables} | {total} |")

    Path(args.out).write_text("\n".join(lines) + "\n", encoding="utf-8")
    Path(args.json).write_text(json.dumps(all_entries, indent=1, ensure_ascii=False), encoding="utf-8")
    grand = sum(t for _, _, t in summary)
    print(f"{grand} enregistrements dans {sum(t for _, t, _ in summary)} tables ; {args.out}")
    for label, tables, total in summary:
        if total:
            print(f"  {label:<12} {total:>4} enregistrements, {tables} table(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

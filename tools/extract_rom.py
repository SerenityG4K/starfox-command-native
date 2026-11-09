#!/usr/bin/env python3
"""Extraction complète d'une ROM Star Fox Command (.nds) vers un arbre de fichiers.

Usage :
    python tools/extract_rom.py <rom.nds> [-o assets/extracted]

Produit :
    <out>/files/...        arborescence du système de fichiers de la ROM
    <out>/arm9.bin         binaire ARM9 décompressé (BLZ)
    <out>/arm7.bin         binaire ARM7
    <out>/overlays/...     overlays ARM9 décompressés
    <out>/manifest.json    inventaire typé de tous les fichiers
"""
from __future__ import annotations

import argparse
import hashlib
import json
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from nitro import compress, formats  # noqa: E402
from nitro.rom import NdsRom  # noqa: E402


def human(n: int) -> str:
    for unit in ("o", "Ko", "Mo", "Go"):
        if n < 1024 or unit == "Go":
            return f"{n:.1f} {unit}" if unit != "o" else f"{n} {unit}"
        n /= 1024.0
    return f"{n} o"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("rom", help="chemin de la ROM .nds")
    ap.add_argument("-o", "--out", default="assets/extracted", help="dossier de sortie")
    ap.add_argument("--no-write", action="store_true",
                    help="analyse seule, n'écrit aucun fichier extrait")
    args = ap.parse_args()

    rom = NdsRom.load(args.rom)
    h = rom.header
    out = Path(args.out)

    print("=" * 68)
    print(f"  {h.title}   [{h.game_code}]  éditeur {h.maker_code}  rev {h.version}")
    print("=" * 68)
    print(f"  ROM              : {human(len(rom.data))} (déclarée {human(h.rom_size)})")
    print(f"  SHA-1            : {hashlib.sha1(rom.data).hexdigest()}")
    print(f"  ARM9             : ROM 0x{h.arm9_rom_offset:06X}  RAM 0x{h.arm9_ram_address:08X}"
          f"  {human(h.arm9_size)}  entrée 0x{h.arm9_entry:08X}")
    print(f"  ARM7             : ROM 0x{h.arm7_rom_offset:06X}  RAM 0x{h.arm7_ram_address:08X}"
          f"  {human(h.arm7_size)}  entrée 0x{h.arm7_entry:08X}")
    print(f"  Overlays ARM9    : {len(rom.overlays9)}")
    print(f"  Overlays ARM7    : {len(rom.overlays7)}")
    print(f"  Fichiers (FAT)   : {len(rom.files)}")
    print()

    manifest: dict[str, object] = {
        "rom": {
            "fichier": Path(args.rom).name,
            "titre": h.title,
            "code_jeu": h.game_code,
            "sha1": hashlib.sha1(rom.data).hexdigest(),
            "taille": len(rom.data),
            "arm9": {"rom": h.arm9_rom_offset, "ram": h.arm9_ram_address,
                     "taille": h.arm9_size, "entree": h.arm9_entry},
            "arm7": {"rom": h.arm7_rom_offset, "ram": h.arm7_ram_address,
                     "taille": h.arm7_size, "entree": h.arm7_entry},
        },
        "overlays": [],
        "fichiers": [],
    }

    # ---------------------------------------------------------------- binaires
    if not args.no_write:
        out.mkdir(parents=True, exist_ok=True)
        arm9 = rom.arm9()
        (out / "arm9.bin").write_bytes(arm9)
        (out / "arm9_raw.bin").write_bytes(rom.arm9(decompress=False))
        (out / "arm7.bin").write_bytes(rom.arm7())
        print(f"  ARM9 décompressé : {human(h.arm9_size)} -> {human(len(arm9))}")

    ov_dir = out / "overlays"
    if not args.no_write and (rom.overlays9 or rom.overlays7):
        ov_dir.mkdir(parents=True, exist_ok=True)

    for ov in rom.overlays9 + rom.overlays7:
        data = rom.overlay_data(ov)
        name = f"{ov.processor}_ovl_{ov.overlay_id:03d}.bin"
        if not args.no_write:
            (ov_dir / name).write_bytes(data)
        manifest["overlays"].append({
            "id": ov.overlay_id, "processeur": ov.processor,
            "ram": ov.ram_address, "taille_ram": ov.ram_size, "bss": ov.bss_size,
            "file_id": ov.file_id, "compresse": ov.is_compressed,
            "taille_extraite": len(data), "fichier": name,
        })

    # ---------------------------------------------------------------- fichiers
    types: Counter[str] = Counter()
    total_bytes = 0
    for entry in rom.files:
        data = rom.file_data(entry)
        kind, desc = formats.identify(data)

        inner_kind = None
        if kind.startswith("COMP:"):
            plain = compress.decompress(data)
            if plain is not data:
                inner_kind = formats.identify(plain)[0]

        types[kind] += 1
        total_bytes += len(data)

        if not args.no_write:
            dest = out / "files" / entry.path
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(data)

        manifest["fichiers"].append({
            "id": entry.file_id, "chemin": entry.path, "offset": entry.offset,
            "taille": entry.size, "type": kind, "description": desc,
            "type_interne": inner_kind,
        })

    print(f"  Octets extraits  : {human(total_bytes)}")
    print()
    print("  Répartition par type :")
    for kind, count in types.most_common():
        desc = formats.MAGIC_TABLE.get(kind.encode()[:4], ("", ""))[1]
        print(f"    {kind:<10} {count:>5}   {desc}")

    if not args.no_write:
        (out / "manifest.json").write_text(
            json.dumps(manifest, indent=2, ensure_ascii=False), encoding="utf-8")
        print(f"\n  Manifeste écrit  : {out / 'manifest.json'}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

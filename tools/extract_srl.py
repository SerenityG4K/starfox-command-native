#!/usr/bin/env python3
"""Extrait les archives .srl de Star Fox Command.

Chaque .srl est en réalité une image ROM Nitro complète (en-tête NDS dont le
titre a été remplacé par « STARFOXARC ») utilisée comme conteneur de données :
une archive par planète, plus une archive par langue.

Usage :
    python tools/extract_srl.py assets/cooked -o assets/cooked/_srl
"""
from __future__ import annotations

import argparse
import json
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from nitro import compress, formats  # noqa: E402
from nitro.rom import NdsRom  # noqa: E402


def unpack(srl: Path, dest: Path) -> tuple[int, int, list[dict]]:
    rom = NdsRom.load(srl)
    entries: list[dict] = []
    count = total = 0
    for entry in rom.files:
        data = rom.file_data(entry)
        name = entry.path
        if name.endswith("-cmp"):
            name = name[:-4]
            for _ in range(3):
                if not compress.looks_compressed(data):
                    break
                plain = compress.decompress(data)
                if plain is data:
                    break
                data = plain
        out = dest / name
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_bytes(data)
        entries.append({
            "chemin": f"{srl.stem}/{name}",
            "taille": len(data),
            "type": formats.identify(data)[0],
        })
        count += 1
        total += len(data)
    return count, total, entries


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("src", help="dossier contenant les .srl")
    ap.add_argument("-o", "--out", default=None, help="dossier de sortie")
    args = ap.parse_args()

    src = Path(args.src)
    out = Path(args.out) if args.out else src / "_srl"

    srls = sorted(src.glob("*.srl"))
    if not srls:
        print(f"aucune archive .srl trouvée dans {src}")
        return 1

    index: list[dict] = []
    kinds: Counter[str] = Counter()
    grand_total = grand_count = 0

    print(f"{len(srls)} archive(s) .srl\n")
    for srl in srls:
        dest = out / srl.stem
        count, total, entries = unpack(srl, dest)
        for e in entries:
            kinds[Path(e["chemin"]).suffix.lstrip(".").lower() or "(sans ext)"] += 1
        index.extend(entries)
        grand_count += count
        grand_total += total
        print(f"  {srl.name:<20} {count:>5} fichiers   {total/1048576:>6.2f} Mo")

    print(f"\n  TOTAL{' ':<15} {grand_count:>5} fichiers   {grand_total/1048576:>6.2f} Mo")
    print("\nExtensions extraites :")
    for k, v in kinds.most_common():
        print(f"  {k:<14} {v:>6}")

    (out / "srl_index.json").write_text(
        json.dumps(index, indent=1, ensure_ascii=False), encoding="utf-8")
    print(f"\nIndex écrit : {out / 'srl_index.json'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

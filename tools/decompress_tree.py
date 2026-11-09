#!/usr/bin/env python3
"""Décompresse récursivement l'arbre extrait de la ROM.

Le système de build d'origine suffixe « -cmp » les fichiers compressés ; on
retire ce suffixe et on décompresse le contenu vers un arbre « cooked ».

Usage :
    python tools/decompress_tree.py assets/extracted/files -o assets/cooked
"""
from __future__ import annotations

import argparse
import json
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from nitro import compress, formats  # noqa: E402


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("src", help="racine de l'arbre extrait")
    ap.add_argument("-o", "--out", default="assets/cooked", help="dossier de sortie")
    args = ap.parse_args()

    src = Path(args.src)
    out = Path(args.out)

    stats: Counter[str] = Counter()
    types: Counter[str] = Counter()
    index: list[dict] = []
    total_in = total_out = 0
    failures: list[str] = []

    for path in sorted(src.rglob("*")):
        if not path.is_file():
            continue
        rel = path.relative_to(src)
        data = path.read_bytes()
        total_in += len(data)

        name = rel.name
        if name.endswith("-cmp"):
            name = name[:-4]

        # Quelques fichiers empilent deux couches (LZ77 sur LZ11) ; on itère
        # jusqu'à stabilisation. Un échec après au moins une couche réussie est
        # presque toujours un faux positif : des données de tuiles commençant
        # par 0x10/0x11 ressemblent à un en-tête de compression.
        layers = 0
        for _ in range(4):
            if rel.parts[0] == "__unnamed__":
                break  # overlays ARM9, traités séparément en BLZ
            if not compress.looks_compressed(data):
                break
            plain = compress.decompress(data)
            if plain is data:
                if layers == 0 and rel.name.endswith("-cmp"):
                    failures.append(str(rel))
                    stats["échec"] += 1
                break
            data = plain
            layers += 1

        was_compressed = layers > 0
        if layers == 0:
            stats["brut"] += 1
        elif layers == 1:
            stats["décompressé (1 couche)"] += 1
        else:
            stats[f"décompressé ({layers} couches)"] += 1

        kind, _ = formats.identify(data)
        types[f"{rel.suffix.lstrip('.').lower() or '(sans ext)'} -> {kind}"] += 1
        total_out += len(data)

        dest = out / rel.parent / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(data)

        index.append({
            "chemin": str((rel.parent / name).as_posix()),
            "source": str(rel.as_posix()),
            "compresse": was_compressed,
            "taille": len(data),
            "type": kind,
        })

    print(f"Fichiers traités : {sum(stats.values())}")
    for k, v in stats.most_common():
        print(f"  {k:<32} {v:>6}")
    print(f"\nOctets : {total_in/1048576:.1f} Mo -> {total_out/1048576:.1f} Mo")

    print("\nCorrespondance extension -> type détecté (top 30) :")
    for k, v in types.most_common(30):
        print(f"  {k:<44} {v:>6}")

    if failures:
        print(f"\n{len(failures)} échec(s) de décompression :")
        for f in failures[:20]:
            print("  ", f)

    (out.parent / "cooked_index.json").write_text(
        json.dumps(index, indent=1, ensure_ascii=False), encoding="utf-8")
    print(f"\nIndex écrit : {out.parent / 'cooked_index.json'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

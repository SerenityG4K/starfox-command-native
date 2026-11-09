#!/usr/bin/env python3
"""Rapport de couverture d'une cartographie (`analysis/codemap.json`).

Usage : python tools/codemap_report.py [analysis/codemap.json] [region]
"""
from __future__ import annotations

import json
import sys
from collections import Counter

CODE_RANGES = {"arm9": (0x02000000, 0x020A6000), "itcm": (0x01FF8000, 0x01FFFB40)}


def merged_ranges(functions: list[dict]) -> list[list[int]]:
    merged: list[list[int]] = []
    for s, e in sorted((b[0], b[1]) for f in functions for b in f["blocs_liste"]):
        if merged and s <= merged[-1][1]:
            merged[-1][1] = max(merged[-1][1], e)
        else:
            merged.append([s, e])
    return merged


def main() -> int:
    path = sys.argv[1] if len(sys.argv) > 1 else "analysis/codemap.json"
    region = sys.argv[2] if len(sys.argv) > 2 else "arm9"
    data = json.load(open(path, encoding="utf-8"))
    fns = data[region]
    merged = merged_ranges(fns)

    def cov(lo: int, hi: int) -> int:
        return sum(max(0, min(e, hi) - max(s, lo)) for s, e in merged)

    if region == "arm9":
        for name, (lo, hi) in CODE_RANGES.items():
            print(f"{name:<5} {cov(lo, hi):7d} / {hi - lo:7d} octets couverts ({100 * cov(lo, hi) // (hi - lo)} %)")
        lo, hi = CODE_RANGES["arm9"]
    else:
        lo, hi = min(f["début"] for f in fns), max(f["fin"] for f in fns)
        print(f"{region}: {cov(lo, hi)} / {hi - lo} octets entre la première et la dernière fonction")
    gaps = []
    prev = lo
    for s, e in merged:
        if s >= hi:
            break
        if s - prev >= 64:
            gaps.append((s - prev, prev, s))
        prev = max(prev, e)
    gaps.sort(reverse=True)
    print(f"{len(gaps)} trous >= 64 octets, {sum(g[0] for g in gaps)} octets au total ; les 12 plus grands :")
    for n, a, b in gaps[:12]:
        print(f"  0x{a:08X}-0x{b:08X} {n:6d}")
    print(dict(Counter(f["origine"] for f in fns)))
    for f in sorted(fns, key=lambda f: -f["taille"])[:5]:
        print(f"  grande fonction 0x{f['entrée']:08X} {f['mode']} taille {f['taille']} blocs {f['blocs']} origine {f['origine']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

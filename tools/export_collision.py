#!/usr/bin/env python3
"""Exporte les tuiles de collision `.hgt` en OBJ, en unités monde.

Chaque tuile sort à côté de l'export de son modèle visuel, suffixée `_Col`,
pour comparer les deux dans Blender.

Usage :
    python tools/export_collision.py assets/cooked -o assets/export
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from nitro import hgt  # noqa: E402


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("root")
    ap.add_argument("-o", "--out", default="assets/export")
    args = ap.parse_args()

    root = Path(args.root)
    out = Path(args.out)
    done = failed = vertices = triangles = 0
    for path in sorted(root.rglob("*.hgt")):
        try:
            tile = hgt.parse(path.read_bytes())
        except Exception as exc:  # noqa: BLE001
            print(f"  {path.relative_to(root).as_posix()} : {exc}")
            failed += 1
            continue
        dest = out / path.relative_to(root).with_suffix(".obj")
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(hgt.to_obj(tile), encoding="utf-8")
        done += 1
        vertices += len(tile.vertices)
        triangles += len(tile.triangles)

    print(f"{done} tuile(s) exportée(s), {failed} échec(s) ; "
          f"{vertices} sommets, {triangles} triangles")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())

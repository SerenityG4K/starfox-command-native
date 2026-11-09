#!/usr/bin/env python3
"""Valide le parseur `.mdl` sur l'intégralité du corpus extrait.

Un parseur de format rétro-conçu ne vaut que s'il tient sur tous les fichiers :
ce script vérifie que chaque modèle se décode, que la display list est
intégralement consommée et qu'aucune commande GX inconnue n'apparaît.

Usage :
    python tools/validate_models.py assets/cooked
"""
from __future__ import annotations

import argparse
import sys
import traceback
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from nitro import mgl  # noqa: E402
from nitro.displaylist import DisplayListRunner  # noqa: E402


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("root")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    root = Path(args.root)
    files = sorted(root.rglob("*.mdl"))

    ok = 0
    errors: list[tuple[str, str]] = []
    unknown_ops: Counter[int] = Counter()
    empty: list[str] = []
    total_tris = total_verts = 0
    max_model = ("", 0)
    tex_missing: Counter[str] = Counter()

    for path in files:
        rel = path.relative_to(root).as_posix()
        try:
            _root, mesh = mgl.load_model(path.read_bytes())
            if mesh is None:
                errors.append((rel, "aucun chunk MGL Mesh"))
                continue

            runner = DisplayListRunner()
            runner.run(mesh.display_list)
            for op in runner.unknown_commands:
                unknown_ops[op] += 1

            tris = verts = 0
            for shape, prims in mesh.build():
                verts += sum(len(p.vertices) for p in prims)
                tris += sum(len(p.triangles) for p in prims)
                mat = (mesh.materials[shape.material_index]
                       if shape.material_index < len(mesh.materials) else None)
                if mat and mat.texture_index >= 0 and \
                        mat.texture_index >= len(mesh.texture_names):
                    tex_missing[rel] += 1

            if tris == 0:
                empty.append(rel)
            total_tris += tris
            total_verts += verts
            if tris > max_model[1]:
                max_model = (rel, tris)
            ok += 1
        except Exception as exc:  # noqa: BLE001
            errors.append((rel, f"{type(exc).__name__}: {exc}"))
            if args.verbose:
                traceback.print_exc()

    print(f"Modèles analysés   : {len(files)}")
    print(f"  décodés          : {ok}")
    print(f"  en erreur        : {len(errors)}")
    print(f"  sans géométrie   : {len(empty)}")
    print(f"Triangles au total : {total_tris}")
    print(f"Sommets au total   : {total_verts}")
    print(f"Plus gros modèle   : {max_model[0]} ({max_model[1]} triangles)")

    if unknown_ops:
        print("\nCommandes GX inconnues rencontrées :")
        for op, n in unknown_ops.most_common():
            print(f"  0x{op:02X}  dans {n} modèle(s)")
    else:
        print("\nAucune commande GX inconnue : la display list est entièrement couverte.")

    if tex_missing:
        print(f"\n{len(tex_missing)} modèle(s) référencent une texture hors table :")
        for rel, n in tex_missing.most_common(10):
            print(f"  {rel} ×{n}")

    if errors:
        print(f"\n{len(errors)} erreur(s) :")
        for rel, msg in errors[:25]:
            print(f"  {rel}: {msg}")
    if empty and args.verbose:
        print(f"\n{len(empty)} modèle(s) sans géométrie :")
        for rel in empty[:25]:
            print(f"  {rel}")

    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())

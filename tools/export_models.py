#!/usr/bin/env python3
"""Exporte les modèles `.mdl` vers OBJ + MTL, et les textures vers PNG.

Sortie directement ouvrable dans Blender : c'est la base de travail pour la
refonte des modèles 3D et des textures.

Les textures d'un modèle sont cherchées, dans l'ordre :
  1. dans les chunks `MGL Texture` embarqués dans le `.mdl` lui-même ;
  2. dans les `.tset` du même répertoire ;
  3. dans les `.tset` du répertoire parent (jeux de textures partagés).

Usage :
    python tools/export_models.py assets/cooked -o assets/export
    python tools/export_models.py assets/cooked -o assets/export --filter Arwing
"""
from __future__ import annotations

import argparse
import json
import os
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from nitro import mgl, png, texture  # noqa: E402


class TextureLibrary:
    """Index paresseux des textures et palettes de l'arbre d'assets."""

    def __init__(self, root: Path) -> None:
        self.root = root
        self._by_dir: dict[Path, dict[str, tuple[texture.Texture, texture.Palette | None]]] = {}
        self._written: dict[tuple[str, int, int, int], str] = {}
        self._global: dict[str, tuple[texture.Texture, texture.Palette | None]] | None = None

    @staticmethod
    def _collect(data: bytes) -> dict[str, tuple[texture.Texture, texture.Palette | None]]:
        palettes: dict[str, texture.Palette] = {}
        textures: dict[str, texture.Texture] = {}
        for chunk in mgl.parse_chunks(data):
            payload = lambda k: data[k.payload_offset:k.payload_offset + k.payload_size]  # noqa: E731
            for k in chunk.find_all("MGL Palette"):
                palettes[k.name] = texture.Palette.parse(k.name, payload(k))
            for k in chunk.find_all("MGL Texture"):
                textures[k.name] = texture.Texture.parse(k.name, payload(k))
        return {name: (tex, palettes.get(tex.palette_name))
                for name, tex in textures.items()}

    def _dir_index(self, directory: Path) -> dict[str, tuple[texture.Texture, texture.Palette | None]]:
        if directory not in self._by_dir:
            merged: dict[str, tuple[texture.Texture, texture.Palette | None]] = {}
            for tset in sorted(directory.glob("*.tset")):
                merged.update(self._collect(tset.read_bytes()))
            self._by_dir[directory] = merged
        return self._by_dir[directory]

    def _global_index(self) -> dict[str, tuple[texture.Texture, texture.Palette | None]]:
        """Index de secours sur tous les `.tset` de l'arbre.

        Les scripts peuvent charger un jeu de textures partagé depuis n'importe
        où (`SetTextureSet`), donc la proximité de répertoire ne suffit pas.
        """
        if self._global is None:
            self._global = {}
            for tset in sorted(self.root.rglob("*.tset")):
                for key, value in self._collect(tset.read_bytes()).items():
                    self._global.setdefault(key, value)
        return self._global

    def resolve(self, name: str, model_path: Path,
                embedded: dict[str, tuple[texture.Texture, texture.Palette | None]]):
        if name in embedded:
            return embedded[name]
        directory = model_path.parent
        for candidate in (directory, directory.parent):
            if candidate == self.root.parent:
                break
            hit = self._dir_index(candidate).get(name)
            if hit is not None:
                return hit
        return self._global_index().get(name)

    def emit_png(self, name: str, tex: texture.Texture,
                 pal: texture.Palette | None, out_dir: Path) -> str | None:
        """Écrit la texture en PNG (dédupliquée) et renvoie son nom de fichier."""
        key = (name, tex.width, tex.height, tex.format)
        if key in self._written:
            return self._written[key]
        try:
            pixels = tex.to_rgba(pal)
        except Exception:  # noqa: BLE001
            return None
        safe = "".join(c if c.isalnum() or c in "._-" else "_" for c in name)
        filename = f"{safe}_{tex.width}x{tex.height}.png"
        out_dir.mkdir(parents=True, exist_ok=True)
        png.write_rgba(out_dir / filename, tex.width, tex.height, pixels)
        self._written[key] = filename
        return filename


def export_model(path: Path, root: Path, out_root: Path,
                 library: TextureLibrary, stats: Counter) -> dict | None:
    data = path.read_bytes()
    _chunk, mesh = mgl.load_model(data)
    if mesh is None:
        return None

    rel = path.relative_to(root)
    obj_path = out_root / rel.with_suffix(".obj")
    obj_path.parent.mkdir(parents=True, exist_ok=True)
    mtl_path = obj_path.with_suffix(".mtl")
    tex_dir = out_root / "_textures"

    embedded = TextureLibrary._collect(data)

    obj: list[str] = [
        f"# Exporté depuis {rel.as_posix()} par tools/export_models.py",
        f"# Star Fox Command — modèle « {mesh.name} »",
        f"mtllib {mtl_path.name}",
        "",
    ]
    mtl: list[str] = ["# Matériaux générés depuis les commandes GX du DS", ""]

    base = 0
    written_materials: set[int] = set()
    used_textures: list[str] = []

    for shape_index, (shape, prims) in enumerate(mesh.build()):
        mat = (mesh.materials[shape.material_index]
               if shape.material_index < len(mesh.materials) else None)
        mat_name = f"mat_{shape.material_index}"

        tex_file = None
        tex_size = (1, 1)
        if mat and 0 <= mat.texture_index < len(mesh.texture_names):
            tex_name = mesh.texture_names[mat.texture_index]
            found = library.resolve(tex_name, path, embedded)
            if found is not None:
                tex, pal = found
                tex_size = (tex.width, tex.height)
                tex_file = library.emit_png(tex_name, tex, pal, tex_dir)
                if tex_file:
                    used_textures.append(tex_name)
                    stats["textures liées"] += 1
            else:
                stats["textures introuvables"] += 1

        if mat and shape.material_index not in written_materials:
            written_materials.add(shape.material_index)
            r, g, b = mat.diffuse
            ar, ag, ab = mat.ambient
            mtl += [
                f"newmtl {mat_name}",
                f"Kd {r:.4f} {g:.4f} {b:.4f}",
                f"Ka {ar:.4f} {ag:.4f} {ab:.4f}",
                f"d {mat.alpha:.4f}" if mat.alpha > 0 else "d 1.0",
                "illum 2",
            ]
            if tex_file:
                relative = os.path.relpath(tex_dir / tex_file, obj_path.parent)
                mtl.append(f"map_Kd {Path(relative).as_posix()}")
            mtl.append("")

        obj.append(f"o {mesh.name}_shape{shape_index}")
        if mat:
            obj.append(f"usemtl {mat_name}")

        for prim in prims:
            for v in prim.vertices:
                obj.append(f"v {v.position[0]:.6f} {v.position[1]:.6f} {v.position[2]:.6f}")
            for v in prim.vertices:
                u = v.texcoord[0] / tex_size[0]
                t = 1.0 - v.texcoord[1] / tex_size[1]
                obj.append(f"vt {u:.6f} {t:.6f}")
            for v in prim.vertices:
                obj.append(f"vn {v.normal[0]:.6f} {v.normal[1]:.6f} {v.normal[2]:.6f}")
            for a, b_, c in prim.triangles:
                ia, ib, ic = base + a + 1, base + b_ + 1, base + c + 1
                obj.append(f"f {ia}/{ia}/{ia} {ib}/{ib}/{ib} {ic}/{ic}/{ic}")
            base += len(prim.vertices)

    obj_path.write_text("\n".join(obj) + "\n", encoding="utf-8")
    mtl_path.write_text("\n".join(mtl) + "\n", encoding="utf-8")

    return {
        "modele": rel.as_posix(),
        "nom": mesh.name,
        "shapes": len(mesh.shapes),
        "materiaux": len(mesh.materials),
        "textures": used_textures,
        "sommets": base,
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("root")
    ap.add_argument("-o", "--out", default="assets/export")
    ap.add_argument("--filter", default=None,
                    help="n'exporte que les chemins contenant cette sous-chaîne")
    args = ap.parse_args()

    root = Path(args.root)
    out = Path(args.out)
    files = sorted(root.rglob("*.mdl"))
    if args.filter:
        files = [f for f in files if args.filter.lower() in f.as_posix().lower()]

    library = TextureLibrary(root)
    stats: Counter[str] = Counter()
    index: list[dict] = []
    errors: list[tuple[str, str]] = []

    for path in files:
        try:
            info = export_model(path, root, out, library, stats)
            if info:
                index.append(info)
                stats["modèles exportés"] += 1
        except Exception as exc:  # noqa: BLE001
            errors.append((path.relative_to(root).as_posix(), f"{type(exc).__name__}: {exc}"))

    print(f"Modèles traités  : {len(files)}")
    for k, v in stats.most_common():
        print(f"  {k:<24} {v}")
    print(f"  PNG écrits           {len(library._written)}")
    if errors:
        print(f"\n{len(errors)} erreur(s) :")
        for rel, msg in errors[:15]:
            print(f"  {rel}: {msg}")

    out.mkdir(parents=True, exist_ok=True)
    (out / "index.json").write_text(json.dumps(index, indent=1, ensure_ascii=False),
                                   encoding="utf-8")
    print(f"\nIndex : {out / 'index.json'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

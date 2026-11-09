#!/usr/bin/env python3
"""Décode les textures `.ntfq` — animations de texture, HUD, effets, menus.

Un `.ntfq` contient les texels bruts d'une texture DS suivis de sa palette,
de longueur variable (seules les couleurs utilisées sont écrites). Ni les
dimensions ni le format n'y figurent : ce sont les scripts qui les donnent,

    CreateTexAnimBase <nom> <largeur> <hauteur>
    LoadTexAnimPatt   <format> <chemin sans extension>
    CreateTexAnimSeq  <nom> <largeur> <hauteur> <format> <chemin> <images> <?>

et la hauteur réelle du fichier se déduit de sa taille — plusieurs images
peuvent y être empilées. Le nombre d'entrées de palette est celui qui rend la
hauteur entière ; il est unique en pratique.

Usage :
    python tools/ntfq.py assets/cooked -o assets/export/_texanim
"""
from __future__ import annotations

import argparse
import json
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from nitro import png, texture  # noqa: E402

FORMATS = {"PLTT4": 2, "PLTT16": 3, "PLTT256": 4, "A3I5": 1, "A5I3": 6,
           "COMP4x4": 5, "DIRECT": 7}
# Bits par texel ; le 4×4 compressé pèse 2 bits de texel et 1 bit d'index.
BITS = {2: 2, 3: 4, 4: 8, 1: 8, 6: 8, 5: 3, 7: 16}
MAX_PALETTE = {2: 4, 3: 16, 4: 256, 1: 32, 6: 8, 5: 32768, 7: 0}


def resolve(root: Path, rom_path: str) -> Path | None:
    rel = rom_path.replace("rom:", "").lstrip("/")
    for candidate in (root / (rel + ".ntfq"), root / rel):
        if candidate.is_file():
            return candidate
    if rel.startswith("3D/"):
        alt = root / "_srl" / (rel[3:] + ".ntfq")
        if alt.is_file():
            return alt
    return None


def height_for(size: int, fmt: int, width: int) -> tuple[int, int] | None:
    """(entrées de palette, hauteur) rendant la taille exacte, ou None."""
    bits = BITS[fmt]
    for entries in range(0, MAX_PALETTE[fmt] + 1):
        texel_bytes = size - 2 * entries
        if texel_bytes <= 0:
            break
        if (texel_bytes * 8) % (width * bits) == 0:
            height = texel_bytes * 8 // (width * bits)
            if height % 8 == 0:
                return entries, height
    return None


def decode(data: bytes, fmt: int, width: int, name: str) -> tuple[int, bytes] | None:
    fit = height_for(len(data), fmt, width)
    if fit is None:
        return None
    entries, height = fit
    colours = list(struct.unpack_from(f"<{entries}H", data, len(data) - 2 * entries)) if entries else []
    palette = texture.Palette(name, colours)
    tex = texture.Texture(name=name, width=width, height=height, format=fmt, flags=1,
                          palette_name=name, texels=data[:len(data) - 2 * entries])
    return height, tex.to_rgba(palette)


def collect_declarations(root: Path) -> dict[str, tuple[str, int, int, int]]:
    """chemin ROM -> (base, format, largeur, images) depuis tous les scripts."""
    declared: dict[str, tuple[str, int, int, int]] = {}
    for script in root.rglob("*.txt"):
        current: tuple[str, int, int] | None = None
        for line in script.read_bytes().decode("shift_jis", "replace").splitlines():
            parts = line.split()
            if not parts or parts[0].startswith("#"):
                continue
            if parts[0] == "CreateTexAnimBase" and len(parts) >= 4:
                current = (parts[1], int(parts[2]), int(parts[3]))
            elif parts[0] == "LoadTexAnimPatt" and len(parts) >= 3 and current:
                fmt = FORMATS.get(parts[1])
                if fmt is not None:
                    declared.setdefault(parts[2], (current[0], fmt, current[1], 1))
            elif parts[0] == "CreateTexAnimSeq" and len(parts) >= 7:
                fmt = FORMATS.get(parts[4])
                if fmt is not None:
                    frames = int(parts[6]) if parts[6].isdigit() else 1
                    declared.setdefault(parts[5], (parts[1], fmt, int(parts[2]), frames))
                    current = (parts[1], int(parts[2]), int(parts[3]))
            elif parts[0] == "RegistTexture":
                # RegistTexture <nom> <chemin sous /3D/Textures/> <l h> <format>
                match = re.match(r"RegistTexture\s+(\S+)\s+(\S+)\s+<\s*(\d+)\s+(\d+)\s*>\s+(\S+)",
                                 line.strip())
                if match:
                    fmt = FORMATS.get(match.group(5))
                    if fmt is not None:
                        path = "/3D/Textures/" + match.group(2)
                        declared.setdefault(path, (match.group(1), fmt, int(match.group(3)), 1))
    return declared


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("root")
    ap.add_argument("-o", "--out", default="assets/export/_texanim")
    args = ap.parse_args()

    root = Path(args.root)
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    declared = collect_declarations(root)
    print(f"{len(declared)} motifs déclarés par les scripts")

    index: list[dict] = []
    done = missing = unfit = 0
    for rom_path, (base, fmt, width, frames) in sorted(declared.items()):
        # Une séquence nomme ses images <chemin>01, <chemin>02, … ou <chemin>0, 1, …
        candidates = [resolve(root, rom_path)]
        if frames > 1:
            for k in range(0, frames + 1):
                for suffix in (f"{k:02d}", f"{k}"):
                    candidates.append(resolve(root, rom_path + suffix))
        files = [c for c in candidates if c is not None]
        files = list(dict.fromkeys(files))
        if not files:
            missing += 1
            continue
        for path in files:
            result = decode(path.read_bytes(), fmt, width, base)
            if result is None:
                unfit += 1
                print(f"  taille inexpliquée : {path.relative_to(root).as_posix()} "
                      f"({texture.FORMAT_NAMES[fmt]}, largeur {width})")
                continue
            height, pixels = result
            dest = out / (path.stem + f"_{width}x{height}.png")
            png.write_rgba(dest, width, height, pixels)
            done += 1
            index.append({"base": base, "fichier": path.relative_to(root).as_posix(),
                          "format": texture.FORMAT_NAMES[fmt], "largeur": width,
                          "hauteur": height, "png": dest.name})

    print(f"{done} image(s) écrite(s), {missing} motif(s) sans fichier, {unfit} taille(s) inexpliquée(s)")
    (out / "index.json").write_text(json.dumps(index, indent=1, ensure_ascii=False),
                                    encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

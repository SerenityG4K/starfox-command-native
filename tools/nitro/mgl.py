"""Lecture des conteneurs « chnk » de Star Fox Command (bibliothèque MGL).

Voir `docs/format_mdl.md` pour la description du format.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass, field

from .displaylist import DisplayListRunner, Primitive

CHUNK_MAGIC = b"chnk"
CHUNK_HEADER_SIZE = 0x64
CHUNK_CONSTANT = 0x4499B63B


@dataclass
class Chunk:
    offset: int
    size: int
    tag: str
    name: str
    class_id: int
    has_children: bool
    data: bytes = field(repr=False, default=b"")
    children: list["Chunk"] = field(default_factory=list, repr=False)

    @property
    def payload_offset(self) -> int:
        return self.offset + CHUNK_HEADER_SIZE

    @property
    def payload_size(self) -> int:
        return self.size - CHUNK_HEADER_SIZE

    def find(self, tag: str) -> "Chunk | None":
        if self.tag == tag:
            return self
        for c in self.children:
            hit = c.find(tag)
            if hit is not None:
                return hit
        return None

    def find_all(self, tag: str) -> list["Chunk"]:
        out = [self] if self.tag == tag else []
        for c in self.children:
            out.extend(c.find_all(tag))
        return out


def parse_chunks(data: bytes, offset: int = 0, end: int | None = None,
                 depth: int = 0) -> list[Chunk]:
    """Parcourt une suite de chunks frères."""
    end = len(data) if end is None else end
    out: list[Chunk] = []
    pos = offset
    while pos + CHUNK_HEADER_SIZE <= end:
        if data[pos:pos + 4] != CHUNK_MAGIC:
            break
        version, has_children, size, class_id = struct.unpack_from("<4I", data, pos + 4)
        if size < CHUNK_HEADER_SIZE or pos + size > end:
            break
        tag = data[pos + 0x14:pos + 0x24].split(b"\0")[0].decode("ascii", "replace")
        name = data[pos + 0x24:pos + 0x5C].split(b"\0")[0].decode("shift_jis", "replace")
        chunk = Chunk(offset=pos, size=size, tag=tag, name=name,
                      class_id=class_id, has_children=bool(has_children),
                      data=data)
        if has_children and depth < 8:
            chunk.children = parse_chunks(data, pos + CHUNK_HEADER_SIZE,
                                          pos + size, depth + 1)
        out.append(chunk)
        pos += size
    return out


# ---------------------------------------------------------------------- mesh
@dataclass
class Material:
    index: int
    polygon_attr: int
    dif_amb: int
    spe_emi: int
    teximage_param: int
    pltt_base: int
    texture_index: int

    @property
    def diffuse(self) -> tuple[float, float, float]:
        v = self.dif_amb & 0x7FFF
        return ((v & 0x1F) / 31.0, ((v >> 5) & 0x1F) / 31.0, ((v >> 10) & 0x1F) / 31.0)

    @property
    def ambient(self) -> tuple[float, float, float]:
        v = (self.dif_amb >> 16) & 0x7FFF
        return ((v & 0x1F) / 31.0, ((v >> 5) & 0x1F) / 31.0, ((v >> 10) & 0x1F) / 31.0)

    @property
    def alpha(self) -> float:
        return ((self.polygon_attr >> 16) & 0x1F) / 31.0

    @property
    def cull_mode(self) -> str:
        back = bool(self.polygon_attr & (1 << 6))
        front = bool(self.polygon_attr & (1 << 7))
        if front and back:
            return "aucun"
        if front:
            return "arriere"
        if back:
            return "avant"
        return "les_deux"

    @property
    def texture_size(self) -> tuple[int, int]:
        w = 8 << ((self.teximage_param >> 20) & 7)
        h = 8 << ((self.teximage_param >> 23) & 7)
        return w, h

    @property
    def texture_format(self) -> int:
        return (self.teximage_param >> 26) & 7


@dataclass
class Shape:
    material_index: int
    dl_offset: int
    dl_end: int = 0


@dataclass
class Mesh:
    name: str
    materials: list[Material]
    texture_names: list[str]
    shapes: list[Shape]
    patches: list[tuple[int, int]]
    display_list: bytes = field(repr=False, default=b"")

    @property
    def prologue_end(self) -> int:
        """Fin du prologue commun : tout ce qui précède le premier shape.

        Le prologue fixe l'échelle du modèle (MTX_SCALE) et la range dans le
        slot 16, que chaque shape restaure ensuite. Sa longueur varie : 24
        octets pour la moitié du corpus, 40 quand un second MTX_SCALE suit le
        premier, jusqu'à 80 dans quelques cas. Le tronquer à une taille fixe
        coupe un paquet en plein milieu et n'exécute alors rien du tout.
        """
        return min((sh.dl_offset for sh in self.shapes), default=0)

    def build(self) -> list[tuple[Shape, list[Primitive]]]:
        """Exécute la display list de chaque shape et renvoie sa géométrie."""
        out = []
        prologue_end = self.prologue_end
        for shape in self.shapes:
            runner = DisplayListRunner()
            runner.run(self.display_list, 0, prologue_end)
            runner.run(self.display_list, shape.dl_offset, shape.dl_end)
            out.append((shape, [p for p in runner.primitives if p.triangles]))
        return out


def _read_pascal_strings(data: bytes, pos: int, count: int) -> tuple[list[str], int]:
    """Lit `count` chaînes précédées de leur longueur sur un octet."""
    names: list[str] = []
    for _ in range(count):
        if pos >= len(data):
            break
        length = data[pos]
        names.append(data[pos + 1:pos + 1 + length].decode("shift_jis", "replace"))
        pos += 1 + length
    return names, pos


def parse_mesh(chunk: Chunk) -> Mesh:
    data = chunk.data
    pos = chunk.payload_offset
    _id, n_mat, n_tex, n_shape, n_patch = struct.unpack_from("<5I", data, pos)
    pos += 20

    materials: list[Material] = []
    for i in range(n_mat):
        v = struct.unpack_from("<8I", data, pos)
        tex = struct.unpack_from("<i", data, pos + 28)[0]
        materials.append(Material(index=i, polygon_attr=v[1], dif_amb=v[2],
                                  spe_emi=v[3], teximage_param=v[5],
                                  pltt_base=v[6], texture_index=tex))
        pos += 32

    texture_names, pos = _read_pascal_strings(data, pos, n_tex)

    shapes: list[Shape] = []
    for _ in range(n_shape):
        mat, off = struct.unpack_from("<2I", data, pos)
        shapes.append(Shape(material_index=mat, dl_offset=off))
        pos += 8

    patches: list[tuple[int, int]] = []
    for _ in range(n_patch):
        tex, off = struct.unpack_from("<2I", data, pos)
        patches.append((tex, off))
        pos += 8

    dl_size = struct.unpack_from("<I", data, pos)[0]
    pos += 4
    display_list = data[pos:pos + dl_size]

    # Chaque shape s'arrête là où commence le suivant.
    bounds = sorted({sh.dl_offset for sh in shapes} | {dl_size})
    for sh in shapes:
        nxt = [b for b in bounds if b > sh.dl_offset]
        sh.dl_end = nxt[0] if nxt else dl_size

    return Mesh(name=chunk.name, materials=materials, texture_names=texture_names,
                shapes=shapes, patches=patches, display_list=display_list)


def load_model(data: bytes) -> tuple[Chunk, Mesh | None]:
    """Charge un `.mdl` : renvoie le chunk racine et son mesh."""
    roots = parse_chunks(data)
    if not roots:
        raise ValueError("aucun chunk « chnk » au début du fichier")
    root = roots[0]
    mesh_chunk = root.find("MGL Mesh")
    mesh = parse_mesh(mesh_chunk) if mesh_chunk else None
    return root, mesh

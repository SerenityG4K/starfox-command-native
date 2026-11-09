"""Lecture des tuiles de collision `.hgt` (chunk « HGT Info »).

Payload, après l'en-tête de chunk de 100 octets :

    u16 version = 1
    u16 empreinte
    u16 1
    u16 champ (157, 160, 212… — non élucidé, sans effet sur le décodage)
    u32 0
    u16 160        taille de la tuile en unités HGT (×16 = 2 560 unités monde)
    u16 8          cellules par côté de la grille de recherche
    u16 0
    u16 nVertices
    u16 0
    Vertex[nVertices]     trois fx32 16.16 : x, y (hauteur), z
    u32 nTriangles
    Triangle[nTriangles]  trois u16
    Cell[64]              u32 hauteur 16.16, u16 n, u16 0, u16 triangle[n]

Une unité HGT vaut 16 unités monde : les coins d'une tuile sont à ±80, soit
±1 280, et les toits de CityD à 44, soit 704.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass, field

from . import mgl

HEADER_SIZE = 22
WORLD_PER_UNIT = 16.0


@dataclass
class HeightMap:
    name: str
    tile_size: int
    cells_per_side: int
    vertices: list[tuple[float, float, float]]          # unités HGT
    triangles: list[tuple[int, int, int]]
    cells: list[tuple[float, list[int]]] = field(default_factory=list)

    def world_vertices(self) -> list[tuple[float, float, float]]:
        s = WORLD_PER_UNIT
        return [(x * s, y * s, z * s) for x, y, z in self.vertices]


def _fx32(data: bytes, offset: int) -> float:
    return struct.unpack_from("<i", data, offset)[0] / 65536.0


def parse(data: bytes) -> HeightMap:
    chunks = mgl.parse_chunks(data)
    if not chunks or chunks[0].tag != "HGT Info":
        raise ValueError("chunk « HGT Info » attendu")
    chunk = chunks[0]
    base = chunk.payload_offset
    end = chunk.offset + chunk.size

    version, _hash, _one, _field, _zero, tile_size, cells_per_side, _z2, n_vertices, _z3 = \
        struct.unpack_from("<HHHHIHHHHH", data, base)
    if version != 1:
        raise ValueError(f"version {version} inattendue")

    pos = base + HEADER_SIZE
    vertices = []
    for _ in range(n_vertices):
        vertices.append((_fx32(data, pos), _fx32(data, pos + 4), _fx32(data, pos + 8)))
        pos += 12

    n_triangles = struct.unpack_from("<I", data, pos)[0]
    pos += 4
    triangles = []
    for _ in range(n_triangles):
        a, b, c = struct.unpack_from("<3H", data, pos)
        if max(a, b, c) >= n_vertices:
            raise ValueError("indice de sommet hors table")
        triangles.append((a, b, c))
        pos += 6

    cells = []
    for _ in range(cells_per_side * cells_per_side):
        if pos + 8 > end:
            raise ValueError("table des cellules tronquée")
        height = _fx32(data, pos)
        count = struct.unpack_from("<H", data, pos + 4)[0]
        indices = list(struct.unpack_from(f"<{count}H", data, pos + 8))
        if indices and max(indices) >= n_triangles:
            raise ValueError("indice de triangle hors table")
        cells.append((height, indices))
        pos += 8 + 2 * count
    if pos != end:
        raise ValueError(f"{end - pos} octet(s) inexpliqué(s) en fin de payload")

    return HeightMap(name=chunk.name, tile_size=tile_size, cells_per_side=cells_per_side,
                     vertices=vertices, triangles=triangles, cells=cells)


def to_obj(hm: HeightMap) -> str:
    """Maillage de collision en OBJ, en unités monde."""
    lines = [f"# collision « {hm.name} » : {len(hm.vertices)} sommets, "
             f"{len(hm.triangles)} triangles"]
    for x, y, z in hm.world_vertices():
        lines.append(f"v {x:.3f} {y:.3f} {z:.3f}")
    for a, b, c in hm.triangles:
        lines.append(f"f {a + 1} {b + 1} {c + 1}")
    return "\n".join(lines) + "\n"

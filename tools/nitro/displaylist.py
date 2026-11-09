"""Interpréteur de display list du GPU Nintendo DS (commandes GX).

Une display list est une suite de paquets : 4 octets de commande, puis les
paramètres 32 bits de chaque commande dans l'ordre. Les commandes sans
paramètre occupent quand même un emplacement dans le paquet.

L'interpréteur exécute la liste et produit des triangles indexés, prêts à être
exportés vers un format moderne (OBJ, glTF).
"""
from __future__ import annotations

from dataclasses import dataclass, field

# Nombre de paramètres 32 bits par commande GX.
PARAM_COUNT = {
    0x00: 0,                                        # NOP
    0x10: 1, 0x11: 0, 0x12: 1, 0x13: 1, 0x14: 1,    # MTX_MODE/PUSH/POP/STORE/RESTORE
    0x15: 0, 0x16: 16, 0x17: 12, 0x18: 16,          # IDENTITY, LOAD_4x4/4x3, MULT_4x4
    0x19: 12, 0x1A: 9, 0x1B: 3, 0x1C: 3,            # MULT_4x3/3x3, SCALE, TRANS
    0x20: 1, 0x21: 1, 0x22: 1,                      # COLOR, NORMAL, TEXCOORD
    0x23: 2, 0x24: 1, 0x25: 1, 0x26: 1,             # VTX_16, VTX_10, VTX_XY, VTX_XZ
    0x27: 1, 0x28: 1,                               # VTX_YZ, VTX_DIFF
    0x29: 1, 0x2A: 1, 0x2B: 1,                      # POLYGON_ATTR, TEXIMAGE_PARAM, PLTT_BASE
    0x30: 1, 0x31: 1, 0x32: 1, 0x33: 1, 0x34: 32,   # DIF_AMB, SPE_EMI, LIGHT_*, SHININESS
    0x40: 1, 0x41: 0,                               # BEGIN_VTXS, END_VTXS
    0x50: 1, 0x60: 1,                               # SWAP_BUFFERS, VIEWPORT
    0x70: 3, 0x71: 2, 0x72: 1,                      # BOX_TEST, POS_TEST, VEC_TEST
}

COMMAND_NAMES = {
    0x00: "NOP", 0x10: "MTX_MODE", 0x11: "MTX_PUSH", 0x12: "MTX_POP",
    0x13: "MTX_STORE", 0x14: "MTX_RESTORE", 0x15: "MTX_IDENTITY",
    0x16: "MTX_LOAD_4x4", 0x17: "MTX_LOAD_4x3", 0x18: "MTX_MULT_4x4",
    0x19: "MTX_MULT_4x3", 0x1A: "MTX_MULT_3x3", 0x1B: "MTX_SCALE",
    0x1C: "MTX_TRANS", 0x20: "COLOR", 0x21: "NORMAL", 0x22: "TEXCOORD",
    0x23: "VTX_16", 0x24: "VTX_10", 0x25: "VTX_XY", 0x26: "VTX_XZ",
    0x27: "VTX_YZ", 0x28: "VTX_DIFF", 0x29: "POLYGON_ATTR",
    0x2A: "TEXIMAGE_PARAM", 0x2B: "PLTT_BASE", 0x30: "DIF_AMB",
    0x31: "SPE_EMI", 0x32: "LIGHT_VECTOR", 0x33: "LIGHT_COLOR",
    0x34: "SHININESS", 0x40: "BEGIN_VTXS", 0x41: "END_VTXS",
    0x50: "SWAP_BUFFERS", 0x60: "VIEWPORT", 0x70: "BOX_TEST",
    0x71: "POS_TEST", 0x72: "VEC_TEST",
}

PRIM_TRIANGLES, PRIM_QUADS, PRIM_TRI_STRIP, PRIM_QUAD_STRIP = range(4)


def s(value: int, bits: int) -> int:
    """Extension de signe d'un entier de `bits` bits."""
    value &= (1 << bits) - 1
    return value - (1 << bits) if value & (1 << (bits - 1)) else value


def fx32(v: int) -> float:
    """Virgule fixe 20.12 vers flottant."""
    return s(v, 32) / 4096.0


Matrix = tuple  # 12 flottants : 4 lignes de 3 (matrice 4x3 en ligne majeure)

IDENTITY: Matrix = (1.0, 0.0, 0.0,
                    0.0, 1.0, 0.0,
                    0.0, 0.0, 1.0,
                    0.0, 0.0, 0.0)


def mat_mul(a: Matrix, b: Matrix) -> Matrix:
    """Produit a×b de deux matrices 4x3 (translation sur la dernière ligne)."""
    out = []
    for r in range(4):
        for c in range(3):
            v = sum(a[r * 3 + k] * b[k * 3 + c] for k in range(3))
            if r == 3:
                v += b[9 + c]
            out.append(v)
    return tuple(out)


def transform(m: Matrix, x: float, y: float, z: float) -> tuple[float, float, float]:
    return (m[0] * x + m[3] * y + m[6] * z + m[9],
            m[1] * x + m[4] * y + m[7] * z + m[10],
            m[2] * x + m[5] * y + m[8] * z + m[11])


def transform_dir(m: Matrix, x: float, y: float, z: float) -> tuple[float, float, float]:
    return (m[0] * x + m[3] * y + m[6] * z,
            m[1] * x + m[4] * y + m[7] * z,
            m[2] * x + m[5] * y + m[8] * z)


@dataclass
class Vertex:
    position: tuple[float, float, float]
    normal: tuple[float, float, float] = (0.0, 0.0, 0.0)
    texcoord: tuple[float, float] = (0.0, 0.0)
    color: tuple[float, float, float] = (1.0, 1.0, 1.0)


@dataclass
class Primitive:
    """Un lot de triangles partageant le même état de rendu."""
    vertices: list[Vertex] = field(default_factory=list)
    triangles: list[tuple[int, int, int]] = field(default_factory=list)
    polygon_attr: int = 0
    teximage_param: int = 0
    pltt_base: int = 0


class DisplayListRunner:
    """Exécute une display list et accumule la géométrie produite."""

    def __init__(self, scale_texcoords: bool = True) -> None:
        self.matrix: Matrix = IDENTITY
        self.stack: list[Matrix] = []
        self.slots: dict[int, Matrix] = {}
        self.primitives: list[Primitive] = []

        self._current: Primitive | None = None
        self._strip: list[int] = []
        self._prim_type = PRIM_TRIANGLES
        self._pos = (0.0, 0.0, 0.0)
        self._normal = (0.0, 0.0, 0.0)
        self._texcoord = (0.0, 0.0)
        self._color = (1.0, 1.0, 1.0)
        self._polygon_attr = 0
        self._teximage_param = 0
        self._pltt_base = 0
        self._scale_texcoords = scale_texcoords
        self.unknown_commands: set[int] = set()

    # ------------------------------------------------------------------ état
    def _emit_vertex(self) -> None:
        assert self._current is not None
        p = transform(self.matrix, *self._pos)
        n = transform_dir(self.matrix, *self._normal)
        self._current.vertices.append(
            Vertex(p, n, self._texcoord, self._color))
        idx = len(self._current.vertices) - 1
        self._strip.append(idx)
        self._flush_primitive()

    def _flush_primitive(self) -> None:
        """Convertit les sommets accumulés en triangles selon la primitive."""
        assert self._current is not None
        tris = self._current.triangles
        st = self._strip
        if self._prim_type == PRIM_TRIANGLES:
            if len(st) == 3:
                tris.append((st[0], st[1], st[2]))
                st.clear()
        elif self._prim_type == PRIM_QUADS:
            if len(st) == 4:
                tris.append((st[0], st[1], st[2]))
                tris.append((st[0], st[2], st[3]))
                st.clear()
        elif self._prim_type == PRIM_TRI_STRIP:
            if len(st) >= 3:
                a, b, c = st[-3], st[-2], st[-1]
                # une bande alterne l'orientation à chaque sommet
                if (len(st) - 3) % 2 == 0:
                    tris.append((a, b, c))
                else:
                    tris.append((b, a, c))
        elif self._prim_type == PRIM_QUAD_STRIP:
            if len(st) >= 4 and len(st) % 2 == 0:
                a, b, c, d = st[-4], st[-3], st[-2], st[-1]
                tris.append((a, b, d))
                tris.append((a, d, c))

    def _begin(self, prim_type: int) -> None:
        self._prim_type = prim_type & 3
        self._strip = []
        self._current = Primitive(
            polygon_attr=self._polygon_attr,
            teximage_param=self._teximage_param,
            pltt_base=self._pltt_base)
        self.primitives.append(self._current)

    def _end(self) -> None:
        self._strip = []
        self._current = None

    # -------------------------------------------------------------- exécution
    def run(self, data: bytes, start: int = 0, end: int | None = None) -> None:
        # Des offsets aberrants (fichier tronqué, mauvais décodage) doivent
        # sortir immédiatement plutôt que de faire tourner la boucle à vide.
        end = len(data) if end is None else min(end, len(data))
        pos = max(0, start)
        if pos >= end:
            return
        while pos + 4 <= end:
            packet = data[pos:pos + 4]
            pos += 4
            commands = []
            total = 0
            valid = True
            for op in packet:
                if op not in PARAM_COUNT:
                    self.unknown_commands.add(op)
                    valid = False
                    break
                commands.append(op)
                total += PARAM_COUNT[op]
            if not valid:
                break
            if pos + total * 4 > end:
                break
            params: list[int] = []
            for i in range(total):
                params.append(int.from_bytes(data[pos + i * 4:pos + i * 4 + 4], "little"))
            pos += total * 4

            k = 0
            for op in commands:
                n = PARAM_COUNT[op]
                self._execute(op, params[k:k + n])
                k += n

    def _execute(self, op: int, p: list[int]) -> None:
        if op == 0x00:
            return
        # ------------------------------------------------------- matrices
        if op == 0x11:                                    # MTX_PUSH
            self.stack.append(self.matrix)
        elif op == 0x12:                                  # MTX_POP
            count = s(p[0] & 0x3F, 6)
            for _ in range(max(1, abs(count))):
                if self.stack:
                    self.matrix = self.stack.pop()
        elif op == 0x13:                                  # MTX_STORE
            self.slots[p[0] & 0x1F] = self.matrix
        elif op == 0x14:                                  # MTX_RESTORE
            self.matrix = self.slots.get(p[0] & 0x1F, IDENTITY)
        elif op == 0x15:                                  # MTX_IDENTITY
            self.matrix = IDENTITY
        elif op == 0x17:                                  # MTX_LOAD_4x3
            self.matrix = tuple(fx32(v) for v in p)
        elif op == 0x19:                                  # MTX_MULT_4x3
            self.matrix = mat_mul(tuple(fx32(v) for v in p), self.matrix)
        elif op == 0x1A:                                  # MTX_MULT_3x3
            m = tuple(fx32(v) for v in p) + (0.0, 0.0, 0.0)
            self.matrix = mat_mul(m, self.matrix)
        elif op == 0x1B:                                  # MTX_SCALE
            sx, sy, sz = (fx32(v) for v in p)
            self.matrix = mat_mul(
                (sx, 0.0, 0.0, 0.0, sy, 0.0, 0.0, 0.0, sz, 0.0, 0.0, 0.0),
                self.matrix)
        elif op == 0x1C:                                  # MTX_TRANS
            tx, ty, tz = (fx32(v) for v in p)
            self.matrix = mat_mul(
                IDENTITY[:9] + (tx, ty, tz), self.matrix)
        elif op == 0x16:                                  # MTX_LOAD_4x4
            vals = [fx32(v) for v in p]
            self.matrix = tuple(vals[0:3] + vals[4:7] + vals[8:11] + vals[12:15])
        elif op == 0x18:                                  # MTX_MULT_4x4
            vals = [fx32(v) for v in p]
            m = tuple(vals[0:3] + vals[4:7] + vals[8:11] + vals[12:15])
            self.matrix = mat_mul(m, self.matrix)
        # ------------------------------------------------------- attributs
        elif op == 0x20:                                  # COLOR (RGB555)
            v = p[0]
            self._color = ((v & 0x1F) / 31.0,
                           ((v >> 5) & 0x1F) / 31.0,
                           ((v >> 10) & 0x1F) / 31.0)
        elif op == 0x21:                                  # NORMAL (3 × 1.9)
            v = p[0]
            self._normal = (s(v & 0x3FF, 10) / 512.0,
                            s((v >> 10) & 0x3FF, 10) / 512.0,
                            s((v >> 20) & 0x3FF, 10) / 512.0)
        elif op == 0x22:                                  # TEXCOORD (12.4)
            v = p[0]
            u = s(v & 0xFFFF, 16) / 16.0
            t = s((v >> 16) & 0xFFFF, 16) / 16.0
            self._texcoord = (u, t)
        elif op == 0x29:
            self._polygon_attr = p[0]
        elif op == 0x2A:
            self._teximage_param = p[0]
        elif op == 0x2B:
            self._pltt_base = p[0]
        # ------------------------------------------------------- primitives
        elif op == 0x40:                                  # BEGIN_VTXS
            self._begin(p[0])
        elif op == 0x41:                                  # END_VTXS
            self._end()
        # --------------------------------------------------------- sommets
        elif op in (0x23, 0x24, 0x25, 0x26, 0x27, 0x28):
            x, y, z = self._pos
            if op == 0x23:                                # VTX_16
                x = s(p[0] & 0xFFFF, 16) / 4096.0
                y = s((p[0] >> 16) & 0xFFFF, 16) / 4096.0
                z = s(p[1] & 0xFFFF, 16) / 4096.0
            elif op == 0x24:                              # VTX_10
                x = (s(p[0] & 0x3FF, 10) << 6) / 4096.0
                y = (s((p[0] >> 10) & 0x3FF, 10) << 6) / 4096.0
                z = (s((p[0] >> 20) & 0x3FF, 10) << 6) / 4096.0
            elif op == 0x25:                              # VTX_XY
                x = s(p[0] & 0xFFFF, 16) / 4096.0
                y = s((p[0] >> 16) & 0xFFFF, 16) / 4096.0
            elif op == 0x26:                              # VTX_XZ
                x = s(p[0] & 0xFFFF, 16) / 4096.0
                z = s((p[0] >> 16) & 0xFFFF, 16) / 4096.0
            elif op == 0x27:                              # VTX_YZ
                y = s(p[0] & 0xFFFF, 16) / 4096.0
                z = s((p[0] >> 16) & 0xFFFF, 16) / 4096.0
            else:                                         # VTX_DIFF
                # Dix bits signés ajoutés tels quels aux coordonnées 4.12 : ce sont les bits de
                # poids faible d'une coordonnée VTX_16, pas une valeur à remettre à l'échelle.
                x += s(p[0] & 0x3FF, 10) / 4096.0
                y += s((p[0] >> 10) & 0x3FF, 10) / 4096.0
                z += s((p[0] >> 20) & 0x3FF, 10) / 4096.0
            self._pos = (x, y, z)
            if self._current is not None:
                self._emit_vertex()

    # ---------------------------------------------------------------- résultat
    @property
    def triangle_count(self) -> int:
        return sum(len(p.triangles) for p in self.primitives)

    @property
    def vertex_count(self) -> int:
        return sum(len(p.vertices) for p in self.primitives)


def disassemble(data: bytes, start: int = 0, end: int | None = None,
                limit: int = 200) -> list[str]:
    """Désassemble une display list en texte, pour inspection."""
    end = len(data) if end is None else end
    out: list[str] = []
    pos = start
    while pos + 4 <= end and len(out) < limit:
        packet = data[pos:pos + 4]
        base = pos
        pos += 4
        ops = []
        total = 0
        for op in packet:
            if op not in PARAM_COUNT:
                out.append(f"{base:06X}: commande inconnue 0x{op:02X}")
                return out
            ops.append(op)
            total += PARAM_COUNT[op]
        params = [int.from_bytes(data[pos + i * 4:pos + i * 4 + 4], "little")
                  for i in range(total)]
        pos += total * 4
        k = 0
        for op in ops:
            n = PARAM_COUNT[op]
            args = " ".join(f"{v:08X}" for v in params[k:k + n])
            if op != 0x00:
                out.append(f"{base:06X}: {COMMAND_NAMES[op]:<16} {args}")
            k += n
    return out

"""Décodage des textures Nintendo DS stockées dans les chunks `MGL Texture`.

Payload de `MGL Texture` (en-tête de 76 octets) :

| Offset | Type | Champ |
|---|---|---|
| `0x00` | `u32` | `size_s` — largeur = `8 << size_s` |
| `0x04` | `u32` | `size_t` — hauteur = `8 << size_t` |
| `0x08` | `u32` | format de texture DS (0..7) |
| `0x0C` | `u32` | drapeaux (bit 0 : couleur 0 transparente) |
| `0x10` | `char[60]` | nom du chunk `MGL Palette` associé |
| `0x4C` | `u8[]` | texels |

Payload de `MGL Palette` : `u32` nombre d'entrées, puis autant de `u16` RGB555.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass

TEXTURE_HEADER_SIZE = 0x4C

FORMAT_NAMES = {
    0: "aucune",
    1: "A3I5 (32 couleurs, alpha 3 bits)",
    2: "palette 4 couleurs",
    3: "palette 16 couleurs",
    4: "palette 256 couleurs",
    5: "compressé 4×4",
    6: "A5I3 (8 couleurs, alpha 5 bits)",
    7: "couleur directe RGBA5551",
}


def data_size(fmt: int, width: int, height: int) -> int:
    """Taille en octets des texels pour un format et des dimensions donnés."""
    n = width * height
    return {0: 0, 1: n, 2: n // 4, 3: n // 2, 4: n,
            5: n * 3 // 8, 6: n, 7: n * 2}[fmt]


def rgb555(value: int, alpha: int = 255) -> tuple[int, int, int, int]:
    r = (value & 0x1F) * 255 // 31
    g = ((value >> 5) & 0x1F) * 255 // 31
    b = ((value >> 10) & 0x1F) * 255 // 31
    return r, g, b, alpha


@dataclass
class Palette:
    name: str
    colors: list[int]           # entrées RGB555 brutes

    @classmethod
    def parse(cls, name: str, payload: bytes) -> "Palette":
        count = struct.unpack_from("<I", payload, 0)[0]
        count = min(count, (len(payload) - 4) // 2)
        colors = list(struct.unpack_from(f"<{count}H", payload, 4))
        return cls(name=name, colors=colors)


@dataclass
class Texture:
    name: str
    width: int
    height: int
    format: int
    flags: int
    palette_name: str
    texels: bytes

    @property
    def color0_transparent(self) -> bool:
        return bool(self.flags & 1)

    @classmethod
    def parse(cls, name: str, payload: bytes) -> "Texture":
        size_s, size_t, fmt, flags = struct.unpack_from("<4I", payload, 0)
        pal = payload[0x10:0x4C].split(b"\0")[0].decode("shift_jis", "replace")
        return cls(name=name, width=8 << (size_s & 7), height=8 << (size_t & 7),
                   format=fmt & 7, flags=flags, palette_name=pal,
                   texels=payload[TEXTURE_HEADER_SIZE:])

    # ------------------------------------------------------------- décodage
    def to_rgba(self, palette: Palette | None) -> bytes:
        """Décode la texture en RGBA8 (width × height × 4 octets)."""
        w, h, fmt = self.width, self.height, self.format
        pal = palette.colors if palette else []
        out = bytearray(w * h * 4)
        data = self.texels

        def put(i: int, rgba: tuple[int, int, int, int]) -> None:
            out[i * 4:i * 4 + 4] = bytes(rgba)

        def entry(idx: int, alpha: int = 255) -> tuple[int, int, int, int]:
            if idx < len(pal):
                return rgb555(pal[idx], alpha)
            return (255, 0, 255, alpha)      # magenta : entrée manquante

        if fmt == 0:
            return bytes(out)

        if fmt in (2, 3, 4):                                   # palettisé
            bits = {2: 2, 3: 4, 4: 8}[fmt]
            per_byte = 8 // bits
            mask = (1 << bits) - 1
            for i in range(w * h):
                byte_index = i // per_byte
                if byte_index >= len(data):
                    break
                idx = (data[byte_index] >> (bits * (i % per_byte))) & mask
                alpha = 0 if (idx == 0 and self.color0_transparent) else 255
                put(i, entry(idx, alpha))

        elif fmt == 1:                                         # A3I5
            for i in range(min(w * h, len(data))):
                v = data[i]
                idx, a3 = v & 0x1F, (v >> 5) & 7
                put(i, entry(idx, a3 * 255 // 7))

        elif fmt == 6:                                         # A5I3
            for i in range(min(w * h, len(data))):
                v = data[i]
                idx, a5 = v & 7, (v >> 3) & 0x1F
                put(i, entry(idx, a5 * 255 // 31))

        elif fmt == 7:                                         # couleur directe
            for i in range(min(w * h, len(data) // 2)):
                v = struct.unpack_from("<H", data, i * 2)[0]
                put(i, rgb555(v, 255 if v & 0x8000 else 0))

        elif fmt == 5:                                         # compressé 4×4
            self._decode_4x4(out, data, pal)

        return bytes(out)

    def _decode_4x4(self, out: bytearray, data: bytes, pal: list[int]) -> None:
        """Format compressé 4×4 : 4 octets de texels + 2 octets d'index par bloc.

        Les deux flux sont concaténés dans le fichier : d'abord tous les blocs
        de texels, puis toutes les données de palette.
        """
        w, h = self.width, self.height
        blocks_x, blocks_y = w // 4, h // 4
        n_blocks = blocks_x * blocks_y
        index_base = n_blocks * 4

        def color(idx: int, alpha: int = 255) -> tuple[int, int, int, int]:
            return rgb555(pal[idx], alpha) if idx < len(pal) else (255, 0, 255, alpha)

        def blend(a: int, b: int, wa: int, wb: int) -> tuple[int, int, int, int]:
            ca = color(a)
            cb = color(b)
            return tuple(  # type: ignore[return-value]
                [(ca[k] * wa + cb[k] * wb) // (wa + wb) for k in range(3)] + [255])

        for by in range(blocks_y):
            for bx in range(blocks_x):
                bi = by * blocks_x + bx
                if index_base + bi * 2 + 2 > len(data):
                    return
                texel = struct.unpack_from("<I", data, bi * 4)[0]
                info = struct.unpack_from("<H", data, index_base + bi * 2)[0]
                offset = (info & 0x3FFF) * 2       # en entrées de palette
                mode = (info >> 14) & 3

                if mode == 0:
                    table = [color(offset), color(offset + 1),
                             color(offset + 2), (0, 0, 0, 0)]
                elif mode == 1:
                    table = [color(offset), color(offset + 1),
                             blend(offset, offset + 1, 1, 1), (0, 0, 0, 0)]
                elif mode == 2:
                    table = [color(offset), color(offset + 1),
                             color(offset + 2), color(offset + 3)]
                else:
                    table = [color(offset), color(offset + 1),
                             blend(offset, offset + 1, 5, 3),
                             blend(offset, offset + 1, 3, 5)]

                for ty in range(4):
                    for tx in range(4):
                        idx = (texel >> (2 * (ty * 4 + tx))) & 3
                        px = bx * 4 + tx
                        py = by * 4 + ty
                        p = py * w + px
                        out[p * 4:p * 4 + 4] = bytes(table[idx])

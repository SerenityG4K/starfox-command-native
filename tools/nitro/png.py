"""Encodeur PNG minimal (RGBA 8 bits), sans dépendance externe."""
from __future__ import annotations

import struct
import zlib


def _chunk(tag: bytes, payload: bytes) -> bytes:
    return (struct.pack(">I", len(payload)) + tag + payload
            + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))


def write_rgba(path, width: int, height: int, pixels: bytes) -> None:
    """Écrit une image RGBA8 ; `pixels` fait width*height*4 octets."""
    if len(pixels) != width * height * 4:
        raise ValueError(
            f"{len(pixels)} octets fournis pour {width}×{height} (attendu "
            f"{width * height * 4})")
    raw = bytearray()
    stride = width * 4
    for y in range(height):
        raw.append(0)                      # filtre « None » pour la ligne
        raw += pixels[y * stride:(y + 1) * stride]

    data = (b"\x89PNG\r\n\x1a\n"
            + _chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
            + _chunk(b"IDAT", zlib.compress(bytes(raw), 9))
            + _chunk(b"IEND", b""))
    with open(path, "wb") as fh:
        fh.write(data)

"""Identification des conteneurs Nitro (NTR) et formats courants d'une ROM DS."""
from __future__ import annotations

import struct

# Les conteneurs Nitro écrivent leur signature en little-endian, ce qui donne
# des magies « à l'envers » une fois lues comme des octets (RGCN = NCGR).
MAGIC_TABLE: dict[bytes, tuple[str, str]] = {
    b"NARC": ("NARC", "Archive Nitro (conteneur de fichiers)"),
    b"BMD0": ("NSBMD", "Modèle 3D Nitro"),
    b"BTX0": ("NSBTX", "Banque de textures 3D Nitro"),
    b"BCA0": ("NSBCA", "Animation squelettique 3D"),
    b"BTP0": ("NSBTP", "Animation de motif de texture"),
    b"BTA0": ("NSBTA", "Animation de translation de texture"),
    b"BMA0": ("NSBMA", "Animation de matériau"),
    b"BVA0": ("NSBVA", "Animation de visibilité"),
    b"RGCN": ("NCGR", "Tuiles graphiques 2D"),
    b"RLCN": ("NCLR", "Palette 2D"),
    b"RCSN": ("NSCR", "Plan de fond 2D (screen)"),
    b"RECN": ("NCER", "Définition de cellules OAM"),
    b"RNAN": ("NANR", "Animation de cellules"),
    b"RTFN": ("NFTR", "Police bitmap Nitro"),
    b"SDAT": ("SDAT", "Archive audio Nitro Composer"),
    b"SSEQ": ("SSEQ", "Séquence musicale"),
    b"SSAR": ("SSAR", "Archive de séquences"),
    b"SBNK": ("SBNK", "Banque d'instruments"),
    b"SWAR": ("SWAR", "Archive de formes d'onde"),
    b"STRM": ("STRM", "Flux audio (streaming)"),
    b"RIFF": ("RIFF", "Conteneur RIFF/WAVE"),
    b"BNBL": ("NCBL", "Table de blocs"),
    b"RESD": ("NSBTX?", "Ressource Nitro générique"),
    b"BUCD": ("BUCD", "Conteneur inconnu"),
}

COMPRESSION_TABLE: dict[int, str] = {
    0x10: "LZ77 (0x10)",
    0x11: "LZ11 (0x11)",
    0x24: "Huffman 4 bits (0x24)",
    0x28: "Huffman 8 bits (0x28)",
    0x30: "RLE (0x30)",
}


def identify(data: bytes) -> tuple[str, str]:
    """Renvoie (type_court, description) pour un blob quelconque."""
    if len(data) < 4:
        return ("VIDE", "fichier vide ou trop court")

    magic = data[:4]
    if magic in MAGIC_TABLE:
        return MAGIC_TABLE[magic]

    # En-tête de compression BIOS : type + taille décompressée sur 24 bits
    ctype = data[0]
    if ctype in COMPRESSION_TABLE:
        size = struct.unpack_from("<I", data, 0)[0] >> 8
        if 0 < size < 0x2000000:
            return (f"COMP:{ctype:02X}", f"Compressé {COMPRESSION_TABLE[ctype]}")

    if data[:2] == b"BM":
        return ("BMP", "Image bitmap Windows")
    if data[:8] == b"\x89PNG\r\n\x1a\n":
        return ("PNG", "Image PNG")

    printable = sum(1 for b in data[:256] if 0x20 <= b < 0x7F or b in (9, 10, 13))
    if printable >= len(data[:256]) * 0.95:
        return ("TEXTE", "Données textuelles")

    return ("BIN", "Données binaires non identifiées")


def nitro_sections(data: bytes) -> list[tuple[str, int, int]]:
    """Liste les sections (magie, offset, taille) d'un conteneur Nitro standard.

    En-tête commun : magie(4) BOM(2) version(2) taille_fichier(4)
    taille_entête(2) nb_sections(2), puis une table d'offsets de sections.
    """
    if len(data) < 16:
        return []
    header_size, section_count = struct.unpack_from("<HH", data, 12)
    if not (8 <= header_size <= 256) or not (0 < section_count < 64):
        return []
    out = []
    for i in range(section_count):
        pos = 16 + i * 4
        if pos + 4 > len(data):
            break
        offset = struct.unpack_from("<I", data, pos)[0]
        if offset + 8 > len(data):
            continue
        name = data[offset:offset + 4].decode("ascii", "replace")
        size = struct.unpack_from("<I", data, offset + 4)[0]
        out.append((name, offset, size))
    return out

"""Décompresseurs des formats de compression Nintendo NTR (Nintendo DS).

Couvre les en-têtes standard du BIOS ARM (LZ77/LZ11, Huffman, RLE) ainsi que
le BLZ (LZ inversé) utilisé pour les binaires ARM9 et les overlays.
"""
from __future__ import annotations

import struct


class CompressionError(Exception):
    pass


def _hdr(data: bytes) -> tuple[int, int]:
    """Retourne (type, taille_décompressée) depuis l'en-tête de 4 octets."""
    if len(data) < 4:
        raise CompressionError("données trop courtes pour un en-tête NTR")
    value = struct.unpack_from("<I", data, 0)[0]
    return value & 0xFF, value >> 8


def lz77_decompress(data: bytes) -> bytes:
    """LZ77 type 0x10 (fenêtre 4 Ko, longueurs 3..18)."""
    ctype, size = _hdr(data)
    if ctype != 0x10:
        raise CompressionError(f"type LZ77 attendu 0x10, trouvé 0x{ctype:02X}")
    out = bytearray()
    pos = 4
    n = len(data)
    while len(out) < size and pos < n:
        flags = data[pos]
        pos += 1
        for bit in range(8):
            if len(out) >= size:
                break
            if flags & (0x80 >> bit):
                if pos + 1 >= n:
                    raise CompressionError("flux LZ77 tronqué")
                b0, b1 = data[pos], data[pos + 1]
                pos += 2
                length = (b0 >> 4) + 3
                disp = (((b0 & 0x0F) << 8) | b1) + 1
                if disp > len(out):
                    raise CompressionError("déplacement LZ77 hors limites")
                start = len(out) - disp
                for i in range(length):
                    out.append(out[start + i])
            else:
                if pos >= n:
                    raise CompressionError("flux LZ77 tronqué")
                out.append(data[pos])
                pos += 1
    if len(out) < size:
        raise CompressionError("sortie LZ77 incomplète")
    return bytes(out[:size])


def lz11_decompress(data: bytes) -> bytes:
    """LZ11 type 0x11 (longueurs étendues jusqu'à 0x10110)."""
    ctype, size = _hdr(data)
    if ctype != 0x11:
        raise CompressionError(f"type LZ11 attendu 0x11, trouvé 0x{ctype:02X}")
    out = bytearray()
    pos = 4
    n = len(data)
    if size == 0:  # taille sur 32 bits étendue
        size = struct.unpack_from("<I", data, pos)[0]
        pos += 4
    while len(out) < size and pos < n:
        flags = data[pos]
        pos += 1
        for bit in range(8):
            if len(out) >= size:
                break
            if not flags & (0x80 >> bit):
                if pos >= n:
                    raise CompressionError("flux LZ11 tronqué")
                out.append(data[pos])
                pos += 1
                continue
            if pos + 1 >= n:
                raise CompressionError("flux LZ11 tronqué")
            indicator = data[pos] >> 4
            if indicator == 0:      # longueur codée sur 8 bits
                length = ((data[pos] & 0x0F) << 4) | (data[pos + 1] >> 4)
                length += 0x11
                disp = ((data[pos + 1] & 0x0F) << 8) | data[pos + 2]
                pos += 3
            elif indicator == 1:    # longueur codée sur 16 bits
                length = ((data[pos] & 0x0F) << 12) | (data[pos + 1] << 4)
                length |= data[pos + 2] >> 4
                length += 0x111
                disp = ((data[pos + 2] & 0x0F) << 8) | data[pos + 3]
                pos += 4
            else:                   # longueur codée sur 4 bits
                length = indicator + 1
                disp = ((data[pos] & 0x0F) << 8) | data[pos + 1]
                pos += 2
            disp += 1
            if disp > len(out):
                raise CompressionError("déplacement LZ11 hors limites")
            start = len(out) - disp
            for i in range(length):
                out.append(out[start + i])
    if len(out) < size:
        raise CompressionError("sortie LZ11 incomplète")
    return bytes(out[:size])


def rle_decompress(data: bytes) -> bytes:
    """RLE type 0x30."""
    ctype, size = _hdr(data)
    if ctype != 0x30:
        raise CompressionError(f"type RLE attendu 0x30, trouvé 0x{ctype:02X}")
    out = bytearray()
    pos = 4
    n = len(data)
    while len(out) < size and pos < n:
        flag = data[pos]
        pos += 1
        if flag & 0x80:
            length = (flag & 0x7F) + 3
            out.extend(bytes([data[pos]]) * length)
            pos += 1
        else:
            length = (flag & 0x7F) + 1
            out.extend(data[pos:pos + length])
            pos += length
    if len(out) < size:
        raise CompressionError("sortie RLE incomplète")
    return bytes(out[:size])


def huffman_decompress(data: bytes) -> bytes:
    """Huffman type 0x24 (4 bits/symbole) ou 0x28 (8 bits/symbole)."""
    ctype, size = _hdr(data)
    bits = ctype & 0x0F
    if (ctype & 0xF0) != 0x20 or bits not in (4, 8):
        raise CompressionError(f"type Huffman invalide 0x{ctype:02X}")
    tree_size = (data[4] + 1) * 2
    tree = data[4:4 + tree_size]
    pos = 4 + tree_size
    out = bytearray()
    nibbles: list[int] = []

    root = 1                     # l'octet 0 de `tree` est la taille de l'arbre
    node = root
    while len(out) < size and pos + 3 < len(data):
        word = struct.unpack_from("<I", data, pos)[0]
        pos += 4
        for i in range(31, -1, -1):
            if len(out) >= size:
                break
            bit = (word >> i) & 1
            current = tree[node]
            offset = current & 0x3F
            child = ((node - 1) & ~1) + offset * 2 + 2 + bit + 1
            leaf_mask = 0x80 if bit == 0 else 0x40
            if child >= len(tree):
                raise CompressionError("arbre Huffman incohérent")
            if current & leaf_mask:
                value = tree[child]
                if bits == 8:
                    out.append(value)
                else:
                    nibbles.append(value & 0x0F)
                    if len(nibbles) == 2:
                        out.append(nibbles[0] | (nibbles[1] << 4))
                        nibbles.clear()
                node = root
            else:
                node = child
    if len(out) < size:
        raise CompressionError("sortie Huffman incomplète")
    return bytes(out[:size])


def blz_decompress(data: bytes) -> bytes:
    """BLZ (« backwards LZ ») utilisé pour ARM9 et les overlays compressés.

    Les 8 derniers octets forment le pied : taille de l'en-tête compressé et
    longueur encodée (u32), puis le delta de taille (u32). Le flux se lit de la
    fin vers le début.
    """
    if len(data) < 8:
        raise CompressionError("données trop courtes pour du BLZ")
    enc_len_and_hdr, inc_len = struct.unpack_from("<II", data, len(data) - 8)
    hdr_size = enc_len_and_hdr >> 24
    enc_len = enc_len_and_hdr & 0x00FFFFFF
    if not (8 <= hdr_size <= 0x0B) or enc_len == 0 or enc_len > len(data):
        raise CompressionError("pied BLZ invalide")

    dec_len = len(data) + inc_len
    buf = bytearray(data) + bytearray(dec_len - len(data))

    src = len(data) - hdr_size          # curseur de lecture (décroissant)
    dst = dec_len                       # curseur d'écriture (décroissant)
    end = len(data) - enc_len           # limite basse de la zone compressée

    mask = 0
    flags = 0
    while dst > end:
        if mask == 0:
            src -= 1
            if src < 0:
                raise CompressionError("flux BLZ tronqué")
            flags = buf[src]
            mask = 0x80
        if flags & mask:
            src -= 2
            if src < 0:
                raise CompressionError("flux BLZ tronqué")
            # Le flux se lit à rebours : le premier octet rencontré (adresse
            # haute) porte les bits de poids fort du couple longueur/distance.
            pair = (buf[src + 1] << 8) | buf[src]
            length = (pair >> 12) + 3
            disp = (pair & 0x0FFF) + 3
            for _ in range(length):
                dst -= 1
                if dst + disp >= dec_len:
                    raise CompressionError("référence BLZ hors limites")
                buf[dst] = buf[dst + disp]
        else:
            src -= 1
            dst -= 1
            buf[dst] = buf[src]
        mask >>= 1
    return bytes(buf[:dec_len])


_DISPATCH = {
    0x10: lz77_decompress,
    0x11: lz11_decompress,
    0x24: huffman_decompress,
    0x28: huffman_decompress,
    0x30: rle_decompress,
}


# Facteur d'expansion maximal réellement atteignable par chaque codec, utilisé
# comme pré-filtre bon marché avant de tenter une décompression complète.
_MAX_RATIO = {0x10: 12, 0x11: 2048, 0x24: 12, 0x28: 12, 0x30: 140}


def looks_compressed(data: bytes) -> bool:
    """Heuristique : en-tête NTR connu et taille décompressée plausible."""
    if len(data) < 8:
        return False
    ctype, size = _hdr(data)
    if ctype not in _DISPATCH:
        return False
    if not (0 < size < 0x2000000):
        return False
    return size <= len(data) * _MAX_RATIO[ctype]


def decompress(data: bytes) -> bytes:
    """Décompresse si un en-tête NTR connu est présent, sinon renvoie tel quel."""
    if not looks_compressed(data):
        return data
    ctype, _ = _hdr(data)
    try:
        return _DISPATCH[ctype](data)
    except Exception:
        return data


def lz77_compress(data: bytes) -> bytes:
    """LZ77 type 0x10, le format de tous les fichiers « -cmp » du jeu.

    Le résultat n'est pas celui du compresseur de Nintendo, octet pour octet, mais le jeu le lit de même.
    Aucune référence ne vise l'octet qui précède immédiatement : la décompression vers la mémoire vidéo
    (LZ77UnCompVram) écrit par demi-mots et ne l'aurait pas encore posé. La sortie est complétée à un
    multiple de 4 octets, comme les fichiers d'origine.
    """
    n = len(data)
    if n >= 1 << 24:
        raise CompressionError("LZ77 : 16 Mo au plus")
    out = bytearray(struct.pack("<I", 0x10 | (n << 8)))
    chaines: dict[bytes, list[int]] = {}      # les positions déjà vues de chaque suite de trois octets
    pos = 0
    while pos < n:
        drapeaux_pos = len(out)
        out.append(0)
        drapeaux = 0
        for bit in range(8):
            if pos >= n:
                break
            meilleure_long, meilleur_depl = 0, 0
            if pos + 3 <= n:
                cle = data[pos:pos + 3]
                candidats = chaines.get(cle, ())
                limite = min(18, n - pos)
                for debut in reversed(candidats[-64:]):      # les plus proches d'abord
                    depl = pos - debut
                    if depl > 0x1000:
                        break
                    if depl < 2:
                        continue
                    long_ = 3
                    while long_ < limite and data[debut + long_] == data[pos + long_]:
                        long_ += 1
                    if long_ > meilleure_long:
                        meilleure_long, meilleur_depl = long_, depl
                        if long_ == limite:
                            break
            avance = meilleure_long if meilleure_long >= 3 else 1
            if meilleure_long >= 3:
                drapeaux |= 0x80 >> bit
                d = meilleur_depl - 1
                out.append(((meilleure_long - 3) << 4) | (d >> 8))
                out.append(d & 0xFF)
            else:
                out.append(data[pos])
            for p in range(pos, min(pos + avance, n - 2)):
                chaines.setdefault(data[p:p + 3], []).append(p)
            pos += avance
        out[drapeaux_pos] = drapeaux
    while len(out) % 4:
        out.append(0)
    return bytes(out)

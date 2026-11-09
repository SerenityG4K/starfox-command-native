#!/usr/bin/env python3
"""Catalogue et extrait une archive audio Nitro Composer (`.sdat`).

Blocs d'une archive :
    SYMB  noms (absent dans Star Fox Command : l'offset vaut zéro)
    INFO  huit listes de fiches — séquences, archives de séquences, banques,
          archives d'ondes, lecteurs, groupes, lecteurs de flux, flux
    FAT   table des fichiers (offset absolu, taille)
    FILE  les fichiers eux-mêmes : SSEQ, SBNK, SWAR, STRM

Extraits en WAV : les flux STRM (PCM 8/16 bits ou IMA-ADPCM par blocs) et les
ondes SWAV contenues dans les archives SWAR. Les séquences SSEQ demandent un
synthétiseur ; elles sont cataloguées, pas rendues.

Usage :
    python tools/sdat.py assets/cooked/StarfoxDS_Eu.sdat -o assets/audio
"""
from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path

WAVE_TYPES = {0: "PCM8", 1: "PCM16", 2: "IMA-ADPCM"}

ADPCM_INDEX_TABLE = [-1, -1, -1, -1, 2, 4, 6, 8]
ADPCM_STEP_TABLE = [
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
    253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
    1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
    3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
    11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767]


def adpcm_decode(data: bytes, predictor: int, index: int) -> list[int]:
    """Décode un bloc IMA-ADPCM du DS (demi-octet de poids faible d'abord)."""
    out: list[int] = []
    for byte in data:
        for nibble in (byte & 0x0F, byte >> 4):
            step = ADPCM_STEP_TABLE[index]
            diff = step >> 3
            if nibble & 1:
                diff += step >> 2
            if nibble & 2:
                diff += step >> 1
            if nibble & 4:
                diff += step
            predictor = predictor - diff if nibble & 8 else predictor + diff
            predictor = max(-32768, min(32767, predictor))
            index = max(0, min(88, index + ADPCM_INDEX_TABLE[nibble & 7]))
            out.append(predictor)
    return out


def write_wav(path: Path, channels: list[list[int]], sample_rate: int) -> None:
    frames = min(len(c) for c in channels)
    payload = bytearray()
    for i in range(frames):
        for channel in channels:
            payload += struct.pack("<h", channel[i])
    n = len(channels)
    header = b"RIFF" + struct.pack("<I", 36 + len(payload)) + b"WAVE"
    header += b"fmt " + struct.pack("<IHHIIHH", 16, 1, n, sample_rate,
                                    sample_rate * n * 2, n * 2, 16)
    header += b"data" + struct.pack("<I", len(payload))
    path.write_bytes(header + payload)


def decode_samples(wave_type: int, data: bytes, adpcm_header: bool) -> list[int]:
    if wave_type == 0:
        return [(b - 256 if b > 127 else b) * 256 for b in data]
    if wave_type == 1:
        return list(struct.unpack_from(f"<{len(data) // 2}h", data, 0))
    if wave_type == 2:
        if adpcm_header and len(data) >= 4:
            predictor, index = struct.unpack_from("<hH", data, 0)
            return adpcm_decode(data[4:], predictor, min(index, 88))
        return adpcm_decode(data, 0, 0)
    return []


def extract_strm(data: bytes) -> tuple[list[list[int]], int, dict]:
    if data[:4] != b"STRM":
        raise ValueError("magie STRM absente")
    head = data.find(b"HEAD")
    if head < 0:
        raise ValueError("bloc HEAD absent")
    (wave_type, loop, channels, sample_rate, _time, _loop_offset, num_samples,
     data_offset, num_blocks, block_length, samples_per_block, last_block_length,
     last_block_samples) = struct.unpack_from("<BBHHHIIIIIIII", data, head + 8)

    output: list[list[int]] = [[] for _ in range(channels)]
    pos = data_offset
    for block in range(num_blocks):
        last = block == num_blocks - 1
        length = last_block_length if last else block_length
        for channel in range(channels):
            chunk = data[pos:pos + length]
            pos += length
            samples = decode_samples(wave_type, chunk, adpcm_header=True)
            wanted = last_block_samples if last else samples_per_block
            output[channel].extend(samples[:wanted])

    info = {"type": WAVE_TYPES.get(wave_type, str(wave_type)), "canaux": channels,
            "frequence": sample_rate, "echantillons": num_samples, "boucle": bool(loop),
            "duree_s": round(num_samples / sample_rate, 2) if sample_rate else 0}
    return output, sample_rate, info


def extract_swar(data: bytes) -> list[tuple[list[int], int, dict]]:
    if data[:4] != b"SWAR":
        raise ValueError("magie SWAR absente")
    block = data.find(b"DATA")
    if block < 0:
        return []
    count = struct.unpack_from("<I", data, block + 40)[0]
    offsets = struct.unpack_from(f"<{count}I", data, block + 44)
    waves = []
    for i, offset in enumerate(offsets):
        wave_type, loop, sample_rate, _time, loop_start, length_words = \
            struct.unpack_from("<BBHHHI", data, offset)
        end = offset + 12 + (loop_start + length_words) * 4
        samples = decode_samples(wave_type, data[offset + 12:end], adpcm_header=True)
        waves.append((samples, sample_rate,
                      {"type": WAVE_TYPES.get(wave_type, str(wave_type)),
                       "frequence": sample_rate, "boucle": bool(loop),
                       "echantillons": len(samples)}))
    return waves


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("sdat")
    ap.add_argument("-o", "--out", default="assets/audio")
    ap.add_argument("--names", default=None,
                    help="en-tête #define des flux (sound_data.sadl) pour nommer les STRM")
    ap.add_argument("--no-wav", action="store_true", help="catalogue seul")
    args = ap.parse_args()

    data = Path(args.sdat).read_bytes()
    if data[:4] != b"SDAT":
        print("magie SDAT absente")
        return 1
    (symb_off, symb_size, info_off, info_size, fat_off, fat_size,
     file_off, file_size) = struct.unpack_from("<8I", data, 0x10)

    # ------------------------------------------------------------------ FAT
    fat_count = struct.unpack_from("<I", data, fat_off + 8)[0]
    fat = [struct.unpack_from("<II", data, fat_off + 12 + i * 16) for i in range(fat_count)]

    # ----------------------------------------------------------------- INFO
    list_offsets = struct.unpack_from("<8I", data, info_off + 8)
    kinds = ["SEQ", "SEQARC", "BANK", "WAVEARC", "PLAYER", "GROUP", "PLAYER2", "STRM"]
    records: dict[str, list[int]] = {}
    for kind, rel in zip(kinds, list_offsets):
        base = info_off + rel
        count = struct.unpack_from("<I", data, base)[0]
        entries = struct.unpack_from(f"<{count}I", data, base + 4)
        ids = []
        for entry in entries:
            if entry == 0:
                ids.append(None)
                continue
            ids.append(struct.unpack_from("<H", data, info_off + entry)[0])
        records[kind] = ids

    strm_names: dict[int, str] = {}
    if args.names and Path(args.names).exists():
        for line in Path(args.names).read_text(errors="replace").splitlines():
            parts = line.split()
            if len(parts) >= 3 and parts[0] == "#define":
                try:
                    strm_names[int(parts[2])] = parts[1]
                except ValueError:
                    pass

    print(f"SDAT : {len(data)} octets, SYMB {'présent' if symb_off else 'absent'}, "
          f"{fat_count} fichiers")
    for kind in kinds:
        ids = records[kind]
        print(f"  {kind:<8} {sum(1 for i in ids if i is not None):>4} fiche(s)")

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    catalogue: list[dict] = []

    # ------------------------------------------------------------- flux STRM
    strm_dir = out / "strm"
    for index, file_id in enumerate(records["STRM"]):
        if file_id is None or file_id >= fat_count:
            continue
        offset, size = fat[file_id]
        name = strm_names.get(index, f"strm_{index:03d}")
        try:
            channels, rate, info = extract_strm(data[offset:offset + size])
        except Exception as exc:  # noqa: BLE001
            print(f"  STRM {index} : {exc}")
            continue
        entry = {"type": "STRM", "index": index, "nom": name, "fichier": file_id,
                 "taille": size, **info}
        catalogue.append(entry)
        if not args.no_wav:
            strm_dir.mkdir(parents=True, exist_ok=True)
            write_wav(strm_dir / f"{name}.wav", channels, rate)

    # ------------------------------------------------------ archives d'ondes
    swar_dir = out / "swar"
    total_waves = 0
    for index, file_id in enumerate(records["WAVEARC"]):
        if file_id is None or file_id >= fat_count:
            continue
        offset, size = fat[file_id]
        try:
            waves = extract_swar(data[offset:offset + size])
        except Exception as exc:  # noqa: BLE001
            print(f"  SWAR {index} : {exc}")
            continue
        total_waves += len(waves)
        catalogue.append({"type": "SWAR", "index": index, "fichier": file_id,
                          "taille": size, "ondes": len(waves)})
        if not args.no_wav:
            folder = swar_dir / f"swar_{index:03d}"
            folder.mkdir(parents=True, exist_ok=True)
            for k, (samples, rate, _info) in enumerate(waves):
                if samples:
                    write_wav(folder / f"wave_{k:03d}.wav", [samples], rate)

    for kind in ("SEQ", "BANK"):
        for index, file_id in enumerate(records[kind]):
            if file_id is None or file_id >= fat_count:
                continue
            catalogue.append({"type": kind, "index": index, "fichier": file_id,
                              "taille": fat[file_id][1]})

    strm_total = sum(e.get("duree_s", 0) for e in catalogue if e["type"] == "STRM")
    print(f"\nFlux STRM : {sum(1 for e in catalogue if e['type']=='STRM')} "
          f"({strm_total/60:.1f} min au total)")
    print(f"Ondes SWAV : {total_waves} dans {sum(1 for e in catalogue if e['type']=='SWAR')} archives")
    print(f"Séquences SSEQ : {sum(1 for e in catalogue if e['type']=='SEQ')}, "
          f"banques : {sum(1 for e in catalogue if e['type']=='BANK')}")
    (out / "catalogue.json").write_text(json.dumps(catalogue, indent=1, ensure_ascii=False),
                                        encoding="utf-8")
    print(f"Catalogue : {out / 'catalogue.json'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

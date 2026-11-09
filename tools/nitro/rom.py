"""Lecture d'une ROM Nintendo DS (format NTR/.nds) : en-tête, FNT, FAT, overlays.

Aucune donnée du jeu n'est distribuée avec ce dépôt : les outils opèrent sur la
copie personnelle de l'utilisateur.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass, field
from pathlib import Path

from . import compress


@dataclass
class RomHeader:
    title: str
    game_code: str
    maker_code: str
    unit_code: int
    version: int
    arm9_rom_offset: int
    arm9_entry: int
    arm9_ram_address: int
    arm9_size: int
    arm7_rom_offset: int
    arm7_entry: int
    arm7_ram_address: int
    arm7_size: int
    fnt_offset: int
    fnt_size: int
    fat_offset: int
    fat_size: int
    arm9_ovt_offset: int
    arm9_ovt_size: int
    arm7_ovt_offset: int
    arm7_ovt_size: int
    banner_offset: int
    rom_size: int
    header_size: int

    @classmethod
    def parse(cls, data: bytes) -> "RomHeader":
        u32 = lambda off: struct.unpack_from("<I", data, off)[0]  # noqa: E731
        return cls(
            title=data[0x00:0x0C].decode("ascii", "replace").rstrip("\0 "),
            game_code=data[0x0C:0x10].decode("ascii", "replace"),
            maker_code=data[0x10:0x12].decode("ascii", "replace"),
            unit_code=data[0x12],
            version=data[0x1E],
            arm9_rom_offset=u32(0x20), arm9_entry=u32(0x24),
            arm9_ram_address=u32(0x28), arm9_size=u32(0x2C),
            arm7_rom_offset=u32(0x30), arm7_entry=u32(0x34),
            arm7_ram_address=u32(0x38), arm7_size=u32(0x3C),
            fnt_offset=u32(0x40), fnt_size=u32(0x44),
            fat_offset=u32(0x48), fat_size=u32(0x4C),
            arm9_ovt_offset=u32(0x50), arm9_ovt_size=u32(0x54),
            arm7_ovt_offset=u32(0x58), arm7_ovt_size=u32(0x5C),
            banner_offset=u32(0x68),
            rom_size=u32(0x80), header_size=u32(0x84),
        )


@dataclass
class Overlay:
    overlay_id: int
    ram_address: int
    ram_size: int
    bss_size: int
    static_init_start: int
    static_init_end: int
    file_id: int
    compressed_size: int
    compressed_flag: int
    processor: str = "arm9"

    @property
    def is_compressed(self) -> bool:
        return bool(self.compressed_flag & 1)


@dataclass
class FileEntry:
    file_id: int
    path: str
    offset: int
    end: int

    @property
    def size(self) -> int:
        return self.end - self.offset


@dataclass
class NdsRom:
    path: Path
    data: bytes
    header: RomHeader
    files: list[FileEntry] = field(default_factory=list)
    overlays9: list[Overlay] = field(default_factory=list)
    overlays7: list[Overlay] = field(default_factory=list)

    @classmethod
    def load(cls, path: str | Path) -> "NdsRom":
        path = Path(path)
        data = path.read_bytes()
        header = RomHeader.parse(data)
        rom = cls(path=path, data=data, header=header)
        rom._parse_fat_fnt()
        rom._parse_overlays()
        return rom

    # ------------------------------------------------------------------ FAT/FNT
    def _fat_entries(self) -> list[tuple[int, int]]:
        h = self.header
        count = h.fat_size // 8
        out = []
        for i in range(count):
            start, end = struct.unpack_from("<II", self.data, h.fat_offset + i * 8)
            out.append((start, end))
        return out

    def _parse_fat_fnt(self) -> None:
        h = self.header
        fat = self._fat_entries()
        if h.fnt_size == 0 or not fat:
            return

        base = h.fnt_offset

        def dir_entry(dir_id: int) -> tuple[int, int, int]:
            off = base + (dir_id & 0x0FFF) * 8
            sub_off, first_file, parent = struct.unpack_from("<IHH", self.data, off)
            return sub_off, first_file, parent

        entries: list[FileEntry] = []

        def walk(dir_id: int, prefix: str) -> None:
            sub_off, file_id, _ = dir_entry(dir_id)
            pos = base + sub_off
            while True:
                kind = self.data[pos]
                pos += 1
                if kind == 0x00:
                    break
                name_len = kind & 0x7F
                name = self.data[pos:pos + name_len].decode("shift_jis", "replace")
                pos += name_len
                if kind & 0x80:  # sous-répertoire
                    sub_id = struct.unpack_from("<H", self.data, pos)[0]
                    pos += 2
                    walk(sub_id, f"{prefix}{name}/")
                else:
                    if file_id < len(fat):
                        start, end = fat[file_id]
                        entries.append(FileEntry(file_id, f"{prefix}{name}", start, end))
                    file_id += 1

        walk(0xF000, "")
        entries.sort(key=lambda e: e.file_id)
        self.files = entries

        # Fichiers présents dans la FAT mais absents du FNT (overlays notamment)
        named = {e.file_id for e in entries}
        for fid, (start, end) in enumerate(fat):
            if fid not in named and end > start:
                self.files.append(FileEntry(fid, f"__unnamed__/{fid:04d}.bin", start, end))

    # ----------------------------------------------------------------- overlays
    def _parse_overlays(self) -> None:
        for offset, size, proc, target in (
            (self.header.arm9_ovt_offset, self.header.arm9_ovt_size, "arm9", self.overlays9),
            (self.header.arm7_ovt_offset, self.header.arm7_ovt_size, "arm7", self.overlays7),
        ):
            if not offset or not size:
                continue
            for i in range(size // 32):
                vals = struct.unpack_from("<8I", self.data, offset + i * 32)
                target.append(Overlay(
                    overlay_id=vals[0], ram_address=vals[1], ram_size=vals[2],
                    bss_size=vals[3], static_init_start=vals[4],
                    static_init_end=vals[5], file_id=vals[6],
                    compressed_size=vals[7] & 0x00FFFFFF,
                    compressed_flag=vals[7] >> 24, processor=proc,
                ))

    # ------------------------------------------------------------------ accès
    def file_data(self, entry: FileEntry) -> bytes:
        return self.data[entry.offset:entry.end]

    def file_by_id(self, file_id: int) -> FileEntry | None:
        for e in self.files:
            if e.file_id == file_id:
                return e
        return None

    def overlay_data(self, ov: Overlay, decompress: bool = True) -> bytes:
        entry = self.file_by_id(ov.file_id)
        if entry is None:
            return b""
        raw = self.file_data(entry)
        if decompress and ov.is_compressed and ov.compressed_size:
            try:
                return compress.blz_decompress(raw[:ov.compressed_size])
            except compress.CompressionError:
                return raw
        return raw

    def arm9(self, decompress: bool = True) -> bytes:
        h = self.header
        raw = self.data[h.arm9_rom_offset:h.arm9_rom_offset + h.arm9_size]
        if not decompress:
            return raw
        try:
            return compress.blz_decompress(raw)
        except compress.CompressionError:
            return raw

    def arm7(self) -> bytes:
        h = self.header
        return self.data[h.arm7_rom_offset:h.arm7_rom_offset + h.arm7_size]

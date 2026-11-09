"""Image mémoire de l'ARM9 telle que le jeu la voit à l'exécution.

Le binaire statique est chargé à son adresse de base ; ses sections « autoload »
(ITCM en 0x01FF8000, DTCM en 0x027E0000) sont recopiées au démarrage depuis la
fin de l'image, d'après la liste que décrit `ModuleParams`. Un overlay, chargé
à la demande par le jeu, est mappé à la sienne. Les overlays partagent quatre
emplacements et ne cohabitent pas : on n'en mappe qu'un à la fois.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass

from .rom import NdsRom, Overlay

MODULE_PARAMS_MAGIC = b"\x21\x06\xC0\xDE"  # nitro_code_be, à +0x1C de la structure


@dataclass
class ModuleParams:
    autoload_list_start: int
    autoload_list_end: int
    autoload_start: int
    static_bss_start: int
    static_bss_end: int
    compressed_static_end: int
    sdk_version: int

    @classmethod
    def find(cls, arm9: bytes) -> "ModuleParams | None":
        index = arm9.find(MODULE_PARAMS_MAGIC)
        if index < 0x1C:
            return None
        return cls(*struct.unpack_from("<7I", arm9, index - 0x1C))


@dataclass
class Region:
    name: str
    base: int
    data: bytes
    bss: int = 0

    @property
    def end(self) -> int:
        return self.base + len(self.data)

    def contains(self, addr: int) -> bool:
        return self.base <= addr < self.end


def arm7_autoload(data: bytes, base: int) -> tuple[int, int, int] | None:
    """Retrouve la liste des sections que le coprocesseur recopie au démarrage.

    Son binaire n'a pas la signature `nitro_code_be` de celui de l'ARM9, mais il a la même
    structure : trois mots (début et fin de la liste, début des données) que son crt0 charge
    depuis un pool. On la reconnaît à ce qu'elle décrit : une liste d'entrées de douze octets
    dont les tailles cumulées mènent exactement du début des données au début de la liste."""
    end = base + len(data)
    for off in range(0, len(data) - 12, 4):
        list_start, list_end, auto_start = struct.unpack_from("<3I", data, off)
        if not (base <= auto_start < list_start <= list_end <= end):
            continue
        if (list_end - list_start) % 12 or list_end == list_start:
            continue
        total, entry = 0, list_start - base
        ok = True
        while entry < list_end - base:
            addr, size, _bss = struct.unpack_from("<3I", data, entry)
            if size == 0 or size > len(data) or addr < 0x02000000:
                ok = False
                break
            total += size
            entry += 12
        if ok and auto_start + total == list_start:
            return list_start, list_end, auto_start
    return None


def arm7_regions(rom: NdsRom) -> list[Region]:
    """Le binaire du coprocesseur tel qu'il est une fois en place : ce qui reste à son adresse
    de chargement, et chacune des sections qu'il recopie ailleurs — la plus grosse en WRAM, d'où
    il exécute son gestionnaire d'interruption et son pilote son."""
    data = rom.arm7()
    base = rom.header.arm7_ram_address
    found = arm7_autoload(data, base)
    if found is None:
        return [Region("arm7", base, data)]
    list_start, list_end, auto_start = found
    regions = [Region("arm7", base, data[: auto_start - base])]
    entry, source = list_start - base, auto_start - base
    n = 0
    while entry < list_end - base:
        addr, size, bss = struct.unpack_from("<3I", data, entry)
        regions.append(Region(f"arm7auto{n}", addr, data[source : source + size], bss))
        entry += 12
        source += size
        n += 1
    return regions


class Memory:
    def __init__(self, rom: NdsRom, cpu: str = "arm9") -> None:
        """`cpu` : "arm9" (avec ses autoload ITCM/DTCM et ses overlays) ou "arm7" (un seul bloc,
        le binaire du coprocesseur, qui porte entre autres le moteur de son du SDK)."""
        self.rom = rom
        self.cpu = cpu
        if cpu == "arm7":
            self.params = None
            self.static = arm7_regions(rom)
            self.regions = list(self.static)
            self.overlay = None
            return
        arm9 = rom.arm9()
        base = rom.header.arm9_ram_address
        self.params = ModuleParams.find(arm9)
        self.static: list[Region] = []
        if self.params:
            p = self.params
            static_end = p.autoload_start - base
            self.static.append(Region("arm9", base, arm9[:static_end], p.static_bss_end - p.static_bss_start))
            entry = p.autoload_list_start - base
            source = static_end
            while entry < p.autoload_list_end - base:
                addr, size, bss = struct.unpack_from("<3I", arm9, entry)
                entry += 12
                name = "itcm" if addr < 0x02000000 else "dtcm"
                self.static.append(Region(name, addr, arm9[source:source + size], bss))
                source += size
        else:
            self.static.append(Region("arm9", base, arm9))
        self.regions: list[Region] = list(self.static)
        self.overlay: Overlay | None = None

    def map_overlay(self, overlay_id: int) -> Overlay:
        overlay = next(o for o in self.rom.overlays9 if o.overlay_id == overlay_id)
        data = self.rom.overlay_data(overlay)
        self.regions = self.static + [Region(f"ovl{overlay_id}", overlay.ram_address, data, overlay.bss_size)]
        self.overlay = overlay
        return overlay

    def unmap_overlay(self) -> None:
        self.regions = list(self.static)
        self.overlay = None

    def region(self, name: str) -> Region:
        return next(r for r in self.regions if r.name == name)

    def region_of(self, addr: int) -> Region | None:
        for region in self.regions:
            if region.contains(addr):
                return region
        return None

    def read(self, addr: int, size: int) -> bytes:
        region = self.region_of(addr)
        if region is None:
            return b""
        start = addr - region.base
        return region.data[start:start + size]

    def u32(self, addr: int) -> int | None:
        raw = self.read(addr, 4)
        return struct.unpack("<I", raw)[0] if len(raw) == 4 else None

    def u16(self, addr: int) -> int | None:
        raw = self.read(addr, 2)
        return struct.unpack("<H", raw)[0] if len(raw) == 2 else None

    def cstring(self, addr: int, limit: int = 96) -> str | None:
        """Chaîne ASCII imprimable terminée par NUL, ou None."""
        raw = self.read(addr, limit)
        if not raw:
            return None
        end = raw.find(b"\0")
        if end <= 0:
            return None
        text = raw[:end]
        if all(0x20 <= b < 0x7F for b in text):
            return text.decode("ascii")
        return None

    # Adresses de code plausibles : dans l'ARM9, l'ITCM ou l'overlay mappé.
    def is_code(self, addr: int) -> bool:
        region = self.region_of(addr & ~1)
        return region is not None and region.name != "dtcm"

    # Adresse de donnée plausible : n'importe quelle région, BSS compris.
    def is_data(self, addr: int) -> bool:
        for region in self.regions:
            if region.base <= addr < region.end + region.bss:
                return True
        return False

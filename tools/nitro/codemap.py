"""Découverte du code : fonctions, blocs de base, modes ARM/Thumb.

Descente récursive depuis des amorces. Les amorces **fortes** viennent de
faits certains : point d'entrée, listes de constructeurs, tables
d'enregistrement, cibles de `bl`/`blx`, pointeurs Thumb (bit 0 levé) lus dans
les données ou dans les pools de constantes. Les amorces **faibles** sont des
conjectures : prologues et stubs du SDK (`ldr ip, =cible ; bx ip`, rappels
d'interruption numérotés atteints par un pointeur calculé) trouvés dans les
trous, mots alignés qui désassemblent proprement en ARM, pointeurs lus au
milieu du code (une instruction ARM `andeq r0, r1, #imm` s'encode
`0x020100xx` et ressemble à un pointeur).

Toute amorce, forte ou faible, doit d'abord être plausible : ni une chaîne de
caractères (les pools, les données et même les tables d'enregistrements
pointent aussi vers des noms, et une chaîne prise pour du code avale les
vtables qui la suivent), ni des octets qui ne se décodent pas. Seule une
amorce manuelle, adresse réellement exécutée, échappe à ce filtre.

Une fonction faible ne peut ni recouvrir ni évincer une fonction forte ; une
amorce forte évince une fonction faible qui occupait sa place. Les pools de
constantes et les amorces qu'une fonction a produits lui appartiennent : si
elle est évincée, ils disparaissent avec elle. Un vrai pool n'est jamais dans
le code d'une autre fonction : quand le flot d'une fonction mène sur le
« pool » d'une fonction moins sûre, ou née d'une amorce venue d'un autre
overlay (ses mots de pool y étaient des instructions), le flot l'emporte ;
et une fonction née d'ailleurs dont toutes les instructions sont les mots de
pool d'une fonction acceptée est un fantôme de ces constantes : évincée.

Les amorces venues d'ailleurs (appels et pointeurs d'un autre overlay ou de
l'ARM9 vers un overlay) ont une réserve : plusieurs overlays partagent le
même emplacement mémoire, et une adresse ne désigne qu'un seul d'entre eux.
Dans les autres, elle tombe au milieu d'une fonction, voire d'une
instruction. Le pilote les sème donc après le noyau interne de l'overlay,
par adresse croissante et seulement dans les trous (`seed(..., gap_only=True)`) :
une fonction explorée depuis son vrai début couvre les fausses amorces qui
la suivent avant qu'elles ne la coupent. Une fonction née d'ailleurs dont
l'entrée tombe au milieu d'une instruction d'une fonction de l'overlay est
déphasée : elle est refusée, ou évincée si elle existait déjà, et les pools
qu'elle partage avec une fonction de l'overlay ne comptent pas contre celle-ci.
De même, aucune fonction ne commence juste après une instruction ordinaire
(ni branchement, ni appel, ni retour) : une amorce née d'ailleurs, dans un
emplacement partagé, sur laquelle une fonction de l'overlay tombe par simple
chute est un fantôme (l'appel visait un autre overlay) ; elle est écartée, ou
évincée, et la fonction continue à travers elle. Elle redevient candidate si
la fonction qui l'a écartée disparaît.

En Thumb, `b` ne porte qu'à ±2 Ko : au-delà, le compilateur emploie `bl`
comme branchement long à l'intérieur d'une grande fonction (vers son
épilogue, typiquement). La cible d'un tel `bl` n'est pas une fonction : c'est
le cas quand tous ses appelants sont la fonction qui l'atteint aussi par le
flot — un `b`, ou une simple chute, jamais seulement la suite d'un `bl` (un
appel sans retour peut précéder une vraie fonction) —, qu'aucun de ces `bl`
n'était à portée d'un `b`, et qu'elle ne commence pas par un prologue. Beaucoup
de morceaux ne sont atteints que par ces `bl` (les cas d'un grand aiguillage,
chacun revenant à l'épilogue commun) : on les reconnaît à leur code, qui suppose
le cadre d'une fonction déjà ouverte (lecture de r4-r7 avant toute écriture,
accès à la pile loin de son sommet, `pop` ou `add sp` sans `push`), ou à leurs
branchements qui reviennent dans le corps de la fonction appelante, ou au
code qu'ils partagent avec elle (le flot de l'une entre dans le code de
l'autre). Un morceau est absorbé (`Function.long_targets`, exporté pour que le
traducteur fasse un `goto` du `bl`) et ses blocs deviennent ceux de la fonction.
`--why 0xADDR` explique, pour une adresse, chaque décision qui l'a concernée.

Un `push {…, lr}` n'existe qu'en tête de fonction : rencontré ailleurs qu'à
l'entrée, il arrête le bloc et devient une amorce (le flot était tombé dans
la fonction suivante après un appel sans retour, ou un pool de constantes
décodé comme du code y enchaînait).

Les `switch` de CodeWarrior (tables de déplacements en Thumb, `add pc, pc,
rX, lsl #2` ou `ldr pc, [pc, rX, lsl #2]` en ARM) sont suivis.

Le résultat sert au recompilateur : chaque fonction devient une fonction C,
chaque bloc une étiquette ; un saut indirect vers une adresse connue se
résout par une table.
"""
from __future__ import annotations

import os
import struct
from dataclasses import dataclass, field

import capstone
from capstone import arm as cs_arm

from .memory import Memory

ARM = 0
THUMB = 1

# Niveaux de confiance d'une amorce : 2 certain, 1 probable, 0 conjectural.
# 2 certain, 1 probable, 0 conjecture directe, -1 conjecture dérivée d'une conjecture
TRACE = bool(os.environ.get("CODEMAP_TRACE"))   # journal détaillé de la file d'exploration (diagnostic)

LEVEL = {"entrée": 2, "irq": 2, "NitroMain": 2, "ctor": 2, "registre": 2, "bl": 2, "blx": 2, "pool": 2,
         "manuel": 2, "queue": 2, "ptr": 1, "prologue": 0, "ptr-arm": 0, "ptr-code": 0,
         "pool-faible": -1, "bl-faible": -1, "blx-faible": -1, "queue-faible": -1}


@dataclass
class Block:
    start: int
    end: int            # exclusif
    mode: int
    successors: list[int] = field(default_factory=list)
    calls: list[int] = field(default_factory=list)      # cibles bl/blx (bit 0 = Thumb)
    pools: list[int] = field(default_factory=list)      # mots de constantes lus (adresses alignées)


@dataclass
class Function:
    entry: int
    mode: int
    origin: str
    blocks: dict[int, Block] = field(default_factory=dict)
    insns: dict[int, list] = field(default_factory=dict)                     # instructions par bloc
    switches: dict[int, list[int]] = field(default_factory=dict)             # saut par registre -> cibles
    seeds: list[tuple[int, int | None, str]] = field(default_factory=list)   # amorces produites
    arm_candidates: list[int] = field(default_factory=list)
    name: str = ""
    external: bool = False    # née d'une amorce venue d'ailleurs (autre overlay, ARM9) : peut viser un autre overlay
    long_targets: set[int] = field(default_factory=set)   # cibles de ses `bl` employés comme branchements longs

    @property
    def level(self) -> int:
        return LEVEL[self.origin]

    @property
    def strong(self) -> bool:
        return self.level == 2

    @property
    def start(self) -> int:
        return min(self.blocks) if self.blocks else self.entry

    @property
    def end(self) -> int:
        return max(b.end for b in self.blocks.values()) if self.blocks else self.entry

    @property
    def size(self) -> int:
        return self.end - self.start

    @property
    def calls(self) -> set[int]:
        out: set[int] = set()
        for b in self.blocks.values():
            out.update(b.calls)
        return out

    def addresses(self):
        for b in self.blocks.values():
            yield from range(b.start, b.end, 2)


class CodeMap:
    def __init__(self, memory: Memory) -> None:
        self.memory = memory
        self.functions: dict[int, Function] = {}
        self.names: dict[int, str] = {}
        self.covered: dict[int, int] = {}       # adresse (pas de 2) -> mode
        self.owner: dict[int, int] = {}         # adresse (pas de 2) -> entrée propriétaire
        self.pools: dict[int, int] = {}         # mot de constante (aligné) -> entrée propriétaire
        self.entries: set[int] = set()          # amorces certaines ou probables : frontières de fonction
        self.pending: list[tuple[int, int, str, bool]] = []
        self.queued: set[int] = set()           # adresses présentes dans `pending` (une seule fois chacune)
        self.seen_seeds: dict[int, tuple[int, str]] = {}   # adresse -> (mode, meilleure origine) jamais proposée
        self.phantoms: dict[int, int] = {}      # amorce écartée (fantôme, cible de branchement long) -> fonction qui l'absorbe
        self.call_sites: dict[int, set[tuple[int, int, str]]] = {}   # cible -> {(site, fonction appelante, "bl" | "queue")}
        self.reexplored: set[tuple[int, int]] = set()   # (amorce, appelant) : appelant déjà ré-exploré pour l'absorber
        self.dirty: set[int] = set()            # fonctions à ré-explorer (des morceaux leur reviennent), quand la file est vide
        self.long_pieces: dict[int, int] = {}   # morceau reconnu (cible de `bl` longs) -> fonction dont il fait partie
        self.explored_counts: dict[int, int] = {}   # amorce -> nombre de reprises (garde-fou contre les boucles)
        self.orphans_retried = 0                # amorces orphelines reprises en fin de cartographie
        self.shared: list[tuple[int, int]] = [] # plages d'adresses que d'autres overlays occupent aussi (emplacement partagé)
        self.arm_candidates: set[int] = set()
        self.switch_sites: list[tuple[int, int]] = []
        self.code_end: dict[str, int] = {}
        self.watch: int | None = None           # adresse à expliquer (diagnostic --why)
        self.repaired = (0, 0, 0)               # (fonctions ré-explorées, blocs coupés, sorties sans étiquette)
        self.unlabeled: list[tuple[int, int, int, int | None]] = []   # (fonction, bloc, cible, propriétaire de la cible)
        self.cs_arm = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_ARM)
        self.cs_thumb = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_THUMB)
        for engine in (self.cs_arm, self.cs_thumb):
            engine.detail = True
        self.code_regions = [r for r in memory.regions if r.name != "dtcm"]

    # ------------------------------------------------------------------ utilitaires
    def in_code(self, addr: int) -> bool:
        return any(r.contains(addr) for r in self.code_regions)

    def region_name(self, addr: int) -> str:
        r = self.memory.region_of(addr)
        return r.name if r else "?"

    def disasm(self, addr: int, mode: int, count: int = 1):
        code = self.memory.read(addr, 4 * count + 4)
        engine = self.cs_arm if mode == ARM else self.cs_thumb
        return list(engine.disasm(code, addr, count))

    def seed(self, addr: int, mode: int | None = None, origin: str = "manuel", gap_only: bool = False) -> None:
        """Propose une amorce. `gap_only` : l'amorce vient d'ailleurs (autre overlay, ARM9) et peut
        viser un autre overlay du même emplacement ; on ne la retient que si l'adresse n'est pas
        déjà dans une fonction ou un pool de cette carte."""
        if mode is None:
            mode = THUMB if addr & 1 else ARM
        addr &= ~1
        if mode == ARM and addr & 3:
            return
        if not self.in_code(addr) or addr in self.functions:
            return
        if origin not in self.FLOW_ORIGINS and not self.plausible_target(addr, mode):
            return  # une chaîne de caractères, ou des octets qui ne se décodent pas
        if mode == THUMB and addr & 3 and self.covered.get(addr) == ARM and self.covered.get(addr & ~3) == ARM:
            # au milieu d'une instruction ARM connue : rien ne peut commencer là (un mot de données
            # d'un overlay qui ressemble à un pointeur Thumb, 0x02099907 par exemple)
            self._why(addr, self.owner.get(addr, 0), f"amorce {origin} refusée : au milieu d'une instruction ARM")
            return
        if gap_only and (self.owner.get(addr) in self.functions or (addr & ~3) in self.pools):
            self._why(addr, self.owner.get(addr, 0), f"amorce externe {origin} ignorée : adresse déjà occupée")
            return
        if origin == "ptr" and self.neighbourhood_mode(addr) == ARM and mode == THUMB:
            origin = "ptr-code"  # un pointeur Thumb au milieu de code ARM connu : douteux
        if LEVEL[origin] >= 1:
            self.entries.add(addr)
        previous = self.seen_seeds.get(addr)
        # « née d'ailleurs » tant que toutes les propositions viennent d'ailleurs
        external = gap_only and (previous is None or previous[2])
        if previous is None or LEVEL[previous[1]] < LEVEL[origin]:
            self.seen_seeds[addr] = (mode, origin, external)
        else:
            self.seen_seeds[addr] = (previous[0], previous[1], external)
        self._enqueue(addr, mode, origin, external)

    def _enqueue(self, addr: int, mode: int, origin: str, external: bool) -> None:
        """Met une amorce en file, une seule fois : dix `bl` vers la même cible ne valent qu'une
        exploration (l'origine la meilleure est relue dans `seen_seeds` à la sortie de la file)."""
        if addr in self.queued:
            return
        self.queued.add(addr)
        self.pending.append((addr, mode, origin, external))

    # amorces que le flot désigne (appels, branchements, points d'entrée, constructeurs, adresses
    # exécutées) : jamais filtrées. Les petits accesseurs Thumb (`ldr r0, [r0, #n] ; bx lr`,
    # soit « npG ») ressembleraient à des noms ; les faux `bl` naissent de pseudo-fonctions
    # de chaînes, que le filtre des pointeurs de données écarte à la racine.
    FLOW_ORIGINS = {"entrée", "irq", "NitroMain", "ctor", "bl", "blx", "queue", "manuel",
                    "bl-faible", "blx-faible", "queue-faible"}   # appels d'une conjecture : des appels quand même

    def text_like(self, fn: Function) -> bool:
        """Une « fonction » Thumb née d'un pointeur de données dont aucun octet n'atteint 0x80 est
        du texte : toute vraie fonction de plus de quelques instructions contient un push (0xB5),
        un pop (0xBD), un branchement (0xD0-0xE7) ou un bl (0xF0-0xFF). Les délimiteurs d'un
        analyseur (« \t », « , ») échappent au filtre des noms ; pas à celui-ci."""
        if fn.mode != THUMB or fn.size <= 24:
            return False
        n = hi = 0
        for insns in fn.insns.values():
            for i in insns:
                n += len(i.bytes)
                hi += sum(1 for b in i.bytes if b >= 0x80)
        # les chaînes européennes ont quelques accents (octets >= 0x80) : moins de 4,5 %, contre
        # 20 à 45 % pour le code (mesuré sur la carte)
        return hi < 0.045 * n

    def pointer_table_like(self, fn: Function) -> bool:
        """Les octets de `fn` sont une table de pointeurs, pas du code : leurs mots alignés sont
        des pointeurs Thumb vers du code (bit 0 levé) ou des zéros, et il y en a plusieurs. Une
        conjecture née d'un pool avale volontiers ces tables, et les pointeurs qu'elles portent
        ne sont alors plus semés — l'accesseur qu'elles désignent manque à la carte."""
        lo, hi = fn.start, fn.end
        if hi - lo < 16:
            return False
        words = ptrs = zeros = 0
        for a in range(lo & ~3, hi, 4):
            v = self.memory.u32(a)
            if v is None:
                return False
            words += 1
            if v == 0:
                zeros += 1
            elif v & 1 and self.in_code(v & ~1):
                ptrs += 1
        return ptrs >= 3 and words and (ptrs + zeros) >= 0.75 * words

    def plausible_target(self, addr: int, mode: int) -> bool:
        """Un pointeur (pool, données, table d'enregistrements) n'est pas forcément un pointeur
        de fonction : les adresses de chaînes de caractères (noms d'objets, de commandes) y sont
        tout aussi fréquentes, et une chaîne prise pour du code avale les vtables et les tables
        qui la suivent. Un nom est fait de lettres, de chiffres et de `_` ; les octets d'une
        petite fonction Thumb peuvent aussi être imprimables (`bx lr` s'écrit « pG »), mais
        rarement sans ponctuation."""
        if mode == THUMB:
            # un `bx lr` dans les deux premières instructions : un accesseur (`movs r0, #0 ; bx lr`
            # se lit « ␣pGpG », que le filtre des noms prendrait pour un nom)
            for i in self.disasm(addr, THUMB, 2):
                if i.id == cs_arm.ARM_INS_BX and i.operands and i.operands[0].type == cs_arm.ARM_OP_REG \
                        and i.operands[0].reg == cs_arm.ARM_REG_LR:
                    return True
        # le pointeur d'une chaîne est souvent impair : la chaîne commence alors à addr | 1
        for a in ((addr, addr | 1) if mode == THUMB else (addr,)):
            text = self.memory.cstring(a, 64)
            if text is None:
                continue
            # lettres, chiffres et la ponctuation des noms, chemins et formats (pas l'espace :
            # `movs r0, #0 ; bx lr` se lit « ␣pG ») ; un nom est surtout en minuscules, ou
            # entièrement en majuscules (« OK »), pas une alternance « pGpG »
            name_like = sum(1 for ch in text if ch.isalnum() or ch in "_/.%-:,()")
            lower = sum(1 for ch in text if ch.islower() or ch.isdigit() or ch in "_/.%-:,()")
            wordy = lower >= 0.5 * len(text) or text.upper() == text
            if len(text) >= 4 and name_like >= 0.8 * len(text) and wordy:
                return False
            # « OK », « Yes » : un nom court, précédé du NUL qui termine le nom précédent
            if a == addr | 1 and 2 <= len(text) < 4 and name_like == len(text):
                before = self.memory.read(addr, 1)
                if before == b"\0":
                    return False
        return len(self.disasm(addr, mode, 2)) == 2

    # ------------------------------------------------------------------ diagnostic
    def _why(self, addr: int, entry: int, reason: str) -> None:
        if self.watch is not None and addr == self.watch:
            print(f"    [why 0x{addr:08X}] fonction 0x{entry:08X} : {reason}")
        elif self.watch is not None and entry == self.watch:
            print(f"    [why 0x{self.watch:08X}] en 0x{addr:08X} : {reason}")

    def _why_fn(self, fn: "Function", reason: str) -> None:
        if self.watch is not None and (fn.entry == self.watch or self.watch in fn.addresses()):
            print(f"    [why 0x{self.watch:08X}] fonction 0x{fn.entry:08X} ({fn.origin}, niveau {fn.level}, "
                  f"0x{fn.start:08X}-0x{fn.end:08X}) : {reason}")

    def explain(self, addr: int) -> None:
        """État de la carte autour de `addr`, en fin de cartographie."""
        owner = self.owner.get(addr)
        pool = self.pools.get(addr & ~3)
        print(f"  [why 0x{addr:08X}] couvert={self.covered.get(addr)} propriétaire="
              f"{'aucun' if owner is None else hex(owner)} pool={'aucun' if pool is None else hex(pool)} "
              f"amorce-frontière={addr in self.entries} amorce-vue={self.seen_seeds.get(addr)}")
        for a in range(addr - 8, addr + 10, 2):
            o = self.owner.get(a)
            if o is not None:
                f = self.functions.get(o)
                if f is not None:
                    print(f"    0x{a:08X} : fonction 0x{o:08X} ({f.origin}, niveau {f.level}, 0x{f.start:08X}-0x{f.end:08X})")
                else:
                    print(f"    0x{a:08X} : propriétaire 0x{o:08X} absent de la carte")
            if a % 4 == 0 and (pl := self.pools.get(a)) is not None:
                f = self.functions.get(pl)
                print(f"    0x{a:08X} : mot de pool de 0x{pl:08X} ({f.origin if f else 'fonction absente'})")

    # ------------------------------------------------------------------ exploration
    CONTROL_IDS = {cs_arm.ARM_INS_B, cs_arm.ARM_INS_BL, cs_arm.ARM_INS_BLX, cs_arm.ARM_INS_BX, cs_arm.ARM_INS_BXJ,
                   cs_arm.ARM_INS_SVC, cs_arm.ARM_INS_UDF, cs_arm.ARM_INS_CBZ, cs_arm.ARM_INS_CBNZ}

    @classmethod
    def is_plain(cls, insn) -> bool:
        """Ni branchement, ni appel, ni retour, ni écriture de pc : l'instruction suivante s'exécute
        toujours après elle (une fonction ne peut donc pas commencer juste après)."""
        if insn.id in cls.CONTROL_IDS:
            return False
        return not any(op.type == cs_arm.ARM_OP_REG and op.reg == cs_arm.ARM_REG_PC for op in insn.operands)

    def ambiguous(self, addr: int) -> bool:
        """L'adresse est dans un emplacement que plusieurs overlays occupent : une amorce venue
        d'ailleurs peut y viser un autre overlay."""
        return any(lo <= addr < hi for lo, hi in self.shared)

    def falls_into(self, other: Function, addr: int) -> bool:
        """`other` atteint `addr` par simple chute : l'instruction qui précède `addr` dans `other`
        est ordinaire."""
        for insns in other.insns.values():
            for i in insns:
                if i.address + i.size == addr:
                    return self.is_plain(i)
        return False

    CALL_ORIGINS = {"bl", "bl-faible", "queue", "queue-faible"}
    FRAME_REGS = {cs_arm.ARM_REG_R4, cs_arm.ARM_REG_R5, cs_arm.ARM_REG_R6, cs_arm.ARM_REG_R7,
                  cs_arm.ARM_REG_R8, cs_arm.ARM_REG_R9, cs_arm.ARM_REG_R10, cs_arm.ARM_REG_R11}

    def long_branch_target(self, fn: Function, addr: int) -> bool:
        """`addr`, amorce née d'appels ou de sauts de queue, peut n'être qu'un morceau de `fn`
        (Thumb) : elle n'a d'appelant vivant que `fn`, aucun des `bl` de `fn` qui la visent n'était
        à portée d'un `b` (±2 Ko), et elle ne commence pas par un prologue."""
        if fn.mode != THUMB or addr <= fn.entry or self.misphased_in(fn, addr):
            return False
        other = self.functions.get(addr)
        seed = self.seen_seeds.get(addr)
        origin = other.origin if other is not None else (seed[1] if seed is not None else None)
        if origin not in self.CALL_ORIGINS:
            return False
        if self.long_pieces.get(addr, fn.entry) != fn.entry:
            return False  # morceau déjà reconnu d'une autre fonction
        # une amorce venue d'ailleurs ne vaut preuve de fonction que hors d'un emplacement partagé
        # (ailleurs, elle peut viser un autre overlay)
        if other is not None and (other.external and not self.ambiguous(addr) or other.mode != THUMB):
            return False
        if other is None and seed is not None and seed[2] and not self.ambiguous(addr):
            return False
        sites = self.call_sites.get(addr, ())
        # un morceau atteint par les branchements longs de la fonction s'appelle aussi lui-même :
        # ces sites-là ne comptent pas comme appelants extérieurs
        if any(e != fn.entry and e != addr and e in self.functions for _, e, _ in sites):
            return False
        if any(k == "bl" and e == fn.entry and abs(addr - site) <= 0x800 for site, e, k in sites):
            return False  # un `b` aurait porté : c'est un vrai appel
        head = self.disasm(addr, THUMB, 1)
        return bool(head) and not self.is_prologue(head[0])

    def piece_of(self, piece: Function, fn: Function) -> bool:
        """`piece`, explorée comme fonction (acceptée ou candidate), est un morceau de `fn` : `fn`
        seule l'appelle, de loin, elle n'a pas de prologue, et elle est atteinte par le flot de
        `fn` (un `b`, une simple chute), ou son code suppose le cadre d'une fonction ouverte, ou
        elle branche dans le corps de `fn`, ou son flot entre dans le code de `fn`."""
        if (piece.entry, fn.entry) in self.reexplored or not self.long_branch_target(fn, piece.entry):
            return False
        o = fn.entry
        reached = any(piece.entry in b.successors and fn.insns[b.start][-1].id
                      not in (cs_arm.ARM_INS_BL, cs_arm.ARM_INS_BLX) for b in fn.blocks.values())
        back = any((t & ~1) != o and self.owner.get(t & ~1) == o for b in piece.blocks.values() for t in b.calls)
        shared = any(s != o and self.owner.get(s) == o for b in piece.blocks.values() for s in b.successors)
        frame = self.frame_dependent(piece.entry)
        if self.watch == piece.entry:
            print(f"    [why 0x{piece.entry:08X}] morceau de 0x{o:08X} ? atteint={reached} retour={back} "
                  f"partagé={shared} cadre={frame}")
        return reached or back or shared or frame

    def frame_dependent(self, addr: int) -> bool:
        """Le code Thumb en `addr` suppose le cadre d'une fonction déjà ouverte : il lit un registre
        préservé (r4-r11) avant de l'écrire, adresse la pile loin de son sommet (au-delà des quelques
        arguments passés en pile), ou dépile (`pop`, `add sp`) sans avoir empilé. Une fonction ne
        commence pas ainsi : c'est un morceau d'une fonction."""
        written: set[int] = set()
        # lecture linéaire : à travers les appels (qui reviennent) et les branchements conditionnels
        # (leur chute), jusqu'au premier saut inconditionnel ou retour
        for insn in self.disasm(addr, THUMB, 40):
            if insn.id == cs_arm.ARM_INS_PUSH:
                return False
            ops = insn.operands
            if insn.id in (cs_arm.ARM_INS_SUB, cs_arm.ARM_INS_ADD) and ops \
                    and ops[0].type == cs_arm.ARM_OP_REG and ops[0].reg == cs_arm.ARM_REG_SP:
                return insn.id == cs_arm.ARM_INS_ADD   # `add sp, #n` dépile ; `sub sp, #n` ouvre un cadre
            if insn.id == cs_arm.ARM_INS_POP:
                return True
            try:
                regs_read, regs_write = insn.regs_access()
            except Exception:
                return False
            if any(r in self.FRAME_REGS and r not in written for r in regs_read):
                return True
            for op in ops:
                if op.type == cs_arm.ARM_OP_MEM and op.mem.base == cs_arm.ARM_REG_SP and op.mem.disp >= 0x40:
                    return True
            if insn.id == cs_arm.ARM_INS_ADD and len(ops) == 3 and ops[1].type == cs_arm.ARM_OP_REG \
                    and ops[1].reg == cs_arm.ARM_REG_SP and ops[2].type == cs_arm.ARM_OP_IMM and ops[2].imm >= 0x40:
                return True
            written.update(regs_write)
            # capstone compte pc parmi les registres écrits d'un `bne` ou d'un `bl` : on ne s'arrête
            # qu'aux sauts inconditionnels, retours et écritures explicites de pc
            branch = insn.id in (cs_arm.ARM_INS_B, cs_arm.ARM_INS_BL, cs_arm.ARM_INS_BLX)
            if (cs_arm.ARM_REG_PC in regs_write and not branch)                     or insn.id in (cs_arm.ARM_INS_BX, cs_arm.ARM_INS_SVC, cs_arm.ARM_INS_UDF)                     or (insn.id == cs_arm.ARM_INS_B and insn.cc in (cs_arm.ARM_CC_AL, cs_arm.ARM_CC_INVALID)):
                break
        return False

    def is_piece(self, fn: Function, target: int, site: int) -> bool:
        """`bl site -> target` est un branchement long de `fn` vers l'un de ses morceaux : hors de
        portée d'un `b`, dans la même région, sans autre appelant vivant, sans prologue, et dont le
        code suppose le cadre de `fn` (ou déjà reconnu comme tel)."""
        if fn.mode != THUMB or target <= fn.entry or abs(target - site) <= 0x800:
            return False
        if self.long_pieces.get(target) == fn.entry:
            return True
        if not self.in_code(target) or self.region_name(target) != self.region_name(fn.entry):
            return False
        other = self.functions.get(target)
        if other is not None and (other.external and not self.ambiguous(target) or other.mode != THUMB
                                  or other.origin not in self.CALL_ORIGINS):
            return False
        seed = self.seen_seeds.get(target)
        if other is None and seed is not None and (seed[2] and not self.ambiguous(target)
                                                   or seed[1] not in self.CALL_ORIGINS):
            return False
        if any(e != fn.entry and e in self.functions for _, e, _ in self.call_sites.get(target, ())):
            return False
        head = self.disasm(target, THUMB, 1)
        if not head or self.is_prologue(head[0]):
            return False
        return self.frame_dependent(target)

    @staticmethod
    def misphased_in(fn: Function, addr: int) -> bool:
        """`addr` est à l'intérieur d'une instruction déjà décodée de `fn` (pas à son début) : une
        amorce déphasée par rapport à cette fonction, qui ne peut y ouvrir ni bloc ni morceau."""
        for start, b in fn.blocks.items():
            if b.start <= addr < b.end:
                return all(i.address != addr for i in fn.insns.get(start, ()))
        return False

    def absorb_kind(self, fn: Function, addr: int, last, flow: bool) -> str | None:
        """`fn` atteint l'amorce `addr` après l'instruction `last` (None : par un branchement) ;
        `flow` : par un `b` ou une simple chute, pas seulement à la suite d'un `bl`. Raison pour
        laquelle cette amorce n'est pas une entrée de fonction, ou None."""
        if addr == fn.entry or addr in fn.blocks:
            return None
        other = self.functions.get(addr)
        seed = self.seen_seeds.get(addr)
        external = other.external if other is not None else (seed is not None and seed[2])
        if external and last is not None and self.is_plain(last) and self.ambiguous(addr):
            return "née d'ailleurs, atteinte par simple chute"
        if other is not None and other.mode != fn.mode and other.level < 2 and last is not None                 and self.neighbourhood_mode(addr, other.entry) in (None, fn.mode):
            # le flot ne change pas de mode sans `bx` : une conjecture de l'autre mode sur notre
            # chemin (saut de queue Thumb au milieu d'une fonction ARM) n'est pas une fonction —
            # sauf si c'est le voisinage qui lui donne raison, et à nous tort
            return "conjecture d'un autre mode sur notre chemin"
        if flow and self.long_branch_target(fn, addr):
            return "cible de branchements longs"
        return None

    def absorb(self, fn: Function, addr: int, kind: str) -> None:
        """Écarte l'amorce `addr` (et la fonction qui en est née, s'il y en a une) : `fn` continue à
        travers elle."""
        self._why(addr, fn.entry, f"amorce écartée ({kind}) : 0x{fn.entry:08X} continue à travers")
        if kind.startswith("cible") and not self.misphased_in(fn, addr):
            fn.long_targets.add(addr)
            self.long_pieces[addr] = fn.entry
        self.phantoms[addr] = fn.entry
        self.entries.discard(addr)
        if addr in self.functions:
            self._why_fn(self.functions[addr], f"évincée : {kind} de 0x{fn.entry:08X}")
            self.evict(addr)
        else:
            o = self.owner.get(addr)
            if o is not None and o != fn.entry and o in self.functions and kind.startswith("cible") \
                    and self.long_branch_target(fn, o):
                # le morceau est au milieu d'une fonction que nous seuls appelons, de loin : elle
                # aussi est un morceau de la nôtre
                self.absorb(fn, o, "cible de branchements longs, code partagé")

    def release_phantoms(self, entry: int) -> None:
        """La fonction `entry` disparaît (évincée ou refusée) : les amorces qu'elle avait écartées
        parce qu'elle y tombait par simple chute redeviennent candidates."""
        for p, e in list(self.phantoms.items()):
            if e == entry:
                del self.phantoms[p]
                seed = self.seen_seeds.get(p)
                if seed is not None:
                    self._enqueue(p, seed[0], seed[1], seed[2])
                    if LEVEL[seed[1]] >= 1:
                        self.entries.add(p)

    def explore(self, entry: int, mode: int, origin: str, external: bool = False) -> Function | None:
        """Suit le flot de contrôle depuis `entry` ; renvoie la fonction ou None."""
        fn = Function(entry, mode, origin, external=external)
        level = fn.level
        work = [entry]
        seen: set[int] = set()
        deferred: set[int] = set()   # entrées atteintes par le flot : peut-être des cibles de branchements longs
        flow_reached: set[int] = set()   # adresses atteintes par un `b` ou une simple chute (pas la suite d'un `bl`)
        pushed_pieces: set[int] = set()  # morceaux absorbés déjà mis au travail
        engine = self.cs_arm if mode == ARM else self.cs_thumb
        while True:
            if not work:
                # file vide : le balayage (cibles différées, fantômes, morceaux absorbés) peut la
                # regarnir ; il doit tourner même si le dernier élément a été écarté d'un `continue`
                # les `bl` de la fonction ne sont connus qu'une fois tous ses blocs explorés : une
                # entrée écartée comme appel de queue peut s'avérer la cible de ses branchements longs
                again = [t for t in deferred if t not in fn.blocks and t in flow_reached
                         and self.long_branch_target(fn, t)]
                deferred.difference_update(again)
                for t in again:
                    self.absorb(fn, t, "cible de branchements longs")
                # une amorce née d'ailleurs (emplacement partagé) au milieu de notre code (nous avons
                # des blocs avant et après, atteints par nos propres branchements) : un fantôme,
                # aucune fonction ne vit à l'intérieur d'une autre
                if fn.blocks:
                    ghosts = [t for t in deferred if t not in fn.blocks
                              and fn.start < t < fn.end and self.ambiguous(t)
                              and ((o := self.functions.get(t)) is not None and o.external
                                   or o is None and (sd := self.seen_seeds.get(t)) is not None and sd[2])]
                    deferred.difference_update(ghosts)
                    for t in ghosts:
                        self.absorb(fn, t, "née d'ailleurs, au milieu de notre code")
                        seen.discard(t)
                        work.append(t)
                # et chaque morceau absorbé (par un `bl`, ou parce que notre flot entrait dans son
                # code) doit être exploré comme nos blocs
                if TRACE and fn.long_targets and (self.watch is None or self.watch in fn.long_targets or self.watch == entry):
                    print(f"    trace balayage 0x{entry:08X} cibles={[hex(t) for t in sorted(fn.long_targets)]} "
                          f"déjà poussées={[hex(t) for t in sorted(pushed_pieces)]} dans blocs={[hex(t) for t in sorted(fn.long_targets) if t in fn.blocks]}", flush=True)
                for t in sorted(fn.long_targets):
                    if self.misphased_in(fn, t):
                        fn.long_targets.discard(t)   # au milieu d'une de nos instructions : pas un morceau
                        continue
                    if t not in fn.blocks and t not in pushed_pieces and self.in_code(t):
                        if TRACE:
                            print(f"    trace morceau 0x{t:08X} de 0x{entry:08X} blocs={len(fn.blocks)}", flush=True)
                        pushed_pieces.add(t)
                        seen.discard(t)
                        work.append(t)
                if not work:
                    break
            start = work.pop()
            if start in seen or start in fn.blocks or not self.in_code(start):
                continue
            if self.misphased_in(fn, start):
                self._why(start, entry, "au milieu d'une de nos instructions : pas de bloc")
                continue
            seen.add(start)
            if start != entry and start in self.entries:
                kind = self.absorb_kind(fn, start, None, start in flow_reached)
                if kind is None:
                    self._why(start, entry, "appel de queue vers une autre entrée")
                    deferred.add(start)
                    continue
                self.absorb(fn, start, kind)
            block = Block(start, start, mode)
            addr = start
            block_insns = []
            while True:
                owner = self.owner.get(addr)
                if owner is not None and owner != entry:
                    other = self.functions.get(owner)
                    if other is not None and addr == other.entry:
                        # née d'ailleurs et atteinte par simple chute (l'appel visait un autre overlay
                        # du même emplacement), ou cible de nos branchements longs : pas une fonction
                        kind = self.absorb_kind(fn, addr, block_insns[-1] if block_insns else None,
                                                self.is_plain(block_insns[-1]) if block_insns else start in flow_reached)
                        if kind is not None:
                            self.absorb(fn, addr, kind)
                            other = None
                    elif other is not None and self.long_branch_target(fn, other.entry):
                        # notre flot entre au milieu d'une fonction que nous seuls appelons, de loin :
                        # c'est un morceau de la nôtre, qui partage son code avec nous
                        self.absorb(fn, other.entry, "cible de branchements longs, code partagé")
                        other = None
                    if other is not None and other.level >= level:
                        block.successors = [addr] if self.covered.get(addr) == mode else []
                        self._why(addr, entry, f"code de la fonction 0x{owner:08X} (niveau {other.level} >= {level})")
                        break  # on entre dans le code d'une autre fonction
                    # fonction moins sûre sur notre chemin : elle sera évincée à l'acceptation
                if addr in self.pools and self.pools[addr] != entry:
                    # un vrai pool n'est jamais dans le code d'une autre fonction : si le flot mène
                    # ici, c'est le « pool » d'une fonction moins sûre, ou née d'une amorce venue
                    # d'un autre overlay (ses mots de pool y étaient des instructions), qui a tort
                    other = self.functions.get(self.pools[addr])
                    if other is None or (other.level >= level and not other.external):
                        self._why(addr, entry, f"mot de pool de la fonction 0x{self.pools[addr]:08X}")
                        break
                    self._why(addr, entry, f"mot de pool de 0x{self.pools[addr]:08X} ignoré (fonction externe ou moins sûre)")
                code = self.memory.read(addr, 4)
                if len(code) < 2:
                    break
                insns = list(engine.disasm(code, addr, 1))
                if not insns:
                    self._why(addr, entry, "instruction invalide")
                    break
                insn = insns[0]
                if addr != entry and self.is_prologue(insn) and (start != entry or len(block_insns) >= 2):
                    # `push {…, lr}` n'existe qu'en tête de fonction (après un éventuel `mov ip, sp`
                    # en ARM) : le flot est tombé dans la fonction suivante (appel sans retour, ou
                    # pool de constantes décodé comme du code) ; on s'arrête avant et cette adresse
                    # devient une amorce
                    self._why(addr, entry, "prologue d'une autre fonction : le bloc s'arrête avant")
                    fn.seeds.append((addr, mode, "prologue"))
                    break
                addr += insn.size
                block.end = addr
                block_insns.append(insn)
                terminal, successors = self.analyse(insn, mode, block, fn, block_insns)
                if terminal or successors:
                    self._why(addr, entry, f"fin de bloc en 0x{block_insns[-1].address:08X} "
                              f"(terminal={terminal}, successeurs={[hex(s) for s in successors]})")
                    block.successors = successors
                    break
                if addr not in fn.blocks and addr in self.entries:
                    kind = self.absorb_kind(fn, addr, insn, self.is_plain(insn))
                    if kind is not None:
                        self.absorb(fn, addr, kind)   # la fonction continue à travers l'amorce
                if addr in fn.blocks or addr in self.entries:
                    self._why(addr, entry, "frontière : " + ("bloc déjà exploré" if addr in fn.blocks else "amorce"))
                    block.successors = [addr]
                    break
            self._why(start, entry, f"bloc exploré : 0x{block.start:08X}-0x{block.end:08X} ({len(block_insns)} instructions)")
            if block.end > block.start:
                fn.blocks[start] = block
                fn.insns[start] = block_insns
                if block_insns[-1].id not in (cs_arm.ARM_INS_BL, cs_arm.ARM_INS_BLX):
                    flow_reached.update(block.successors)
                for s in block.successors:
                    if self.in_code(s) and s not in fn.blocks:
                        work.append(s)
        # un `bl` de la fonction vers un de ses propres blocs, hors de portée d'un `b` (±2 Ko) et
        # qui ne commence pas par un prologue, est un branchement long : CodeWarrior s'en sert pour
        # atteindre le code lointain, et le traducteur doit en faire un `goto`. Sans cela l'épilogue
        # du morceau dépile le cadre entier de la fonction au retour de ce faux appel.
        if fn.mode == THUMB:
            for insns in fn.insns.values():
                for i in insns:
                    if i.id != cs_arm.ARM_INS_BL or not i.operands or i.operands[0].type != cs_arm.ARM_OP_IMM:
                        continue
                    t = i.operands[0].imm & ~1
                    if t == entry or t not in fn.blocks or abs(t - i.address) <= 0x800:
                        continue
                    head = self.disasm(t, THUMB, 1)
                    if head and not self.is_prologue(head[0]):
                        fn.long_targets.add(t)
        # une cible longue qui n'a pas fini par ouvrir un bloc (déphasée par les blocs explorés
        # entre-temps, ou hors code) n'en est pas une : exportée, le traducteur n'aurait pas
        # d'étiquette où sauter et rendrait le `bl` à un appel vers une adresse non cartographiée
        fn.long_targets = {t for t in fn.long_targets if t in fn.blocks}
        return fn if fn.blocks else None

    @staticmethod
    def is_prologue(insn) -> bool:
        """`push {…, lr}` (Thumb) ou `stmdb sp!, {…, lr}` (ARM) : un prologue de fonction."""
        if insn.id == cs_arm.ARM_INS_STMDB:
            if not insn.operands or insn.operands[0].type != cs_arm.ARM_OP_REG or insn.operands[0].reg != cs_arm.ARM_REG_SP:
                return False
            if not getattr(insn, "writeback", False):
                return False
        elif insn.id != cs_arm.ARM_INS_PUSH:
            return False
        return any(op.type == cs_arm.ARM_OP_REG and op.reg == cs_arm.ARM_REG_LR for op in insn.operands)

    # ------------------------------------------------------------------ switch
    @staticmethod
    def _reg(op) -> int | None:
        return op.reg if op.type == cs_arm.ARM_OP_REG else None

    LOWER_CC = {cs_arm.ARM_CC_LT, cs_arm.ARM_CC_LO, cs_arm.ARM_CC_MI, cs_arm.ARM_CC_LE}

    def _preceding_blocks(self, block_insns, fn: "Function | None", levels: int = 3) -> list:
        """Instructions des blocs qui précèdent celui-ci (par le flot ou physiquement), du plus
        proche au plus lointain : entre le `cmp ; bls` et la table se glisse souvent le `b défaut`."""
        out = []
        if fn is None or not block_insns:
            return out
        frontier = [block_insns[0].address]
        seen: set[int] = set()
        for _ in range(levels):
            preds = []
            for start in frontier:
                for b_start, b in fn.blocks.items():
                    if b_start in seen or b_start == block_insns[0].address:
                        continue
                    if start in b.successors or b.end == start:
                        preds.append(b_start)
            for b_start in preds:
                seen.add(b_start)
                out.append(fn.insns.get(b_start, []))
            if not preds:
                break
            frontier = preds
        return out

    def _with_physical_predecessors(self, block_insns, fn: "Function | None", levels: int = 2) -> list:
        """Le bloc précédé de ceux qui le touchent physiquement : un motif de table de sauts
        (`adds rI, rI, rI ; add rY, pc ; ldrh …`) est coupé en deux dès qu'une amorce ouvre un
        bloc au milieu, et la table devient invisible."""
        if fn is None or not block_insns:
            return list(block_insns)
        head = list(block_insns)
        for _ in range(levels):
            start = head[0].address
            prev = next((b_start for b_start, b in fn.blocks.items() if b.end == start and b_start != start), None)
            if prev is None:
                break
            insns = fn.insns.get(prev, [])
            if not insns:
                break
            head = list(insns) + head
        return head

    def _cmp_limit(self, block_insns, fn: "Function | None" = None, reg: int | None = None,
                   cut: int | None = None) -> int | None:
        """Nombre de cas d'une table de sauts moins un, en suivant le registre d'index `reg` :

            cmp rX, #N ; bgt défaut ; adds rI, rX, #0 ; subs rI, #M ; … ; add pc, rI

        donne N - M. Seule une garde de borne *supérieure* (bhi, bgt, bhs, bge) compte ; un
        `cmp rI, #0 ; blt défaut` est la garde inférieure et ne dit rien de la taille. Sans
        registre connu, on prend le dernier `cmp rX, #N` de borne supérieure (ancien comportement)."""
        off = 0
        first = True
        for insns in [block_insns] + self._preceding_blocks(block_insns, fn):
            # `cut` borne la recherche dans le bloc courant (avant le doublement de l'index) sans
            # empêcher de remonter aux blocs précédents
            start_i = (len(insns) if cut is None else min(cut, len(insns))) if first else len(insns)
            first = False
            for i in range(start_i - 1, -1, -1):
                insn = insns[i]
                ops = insn.operands
                dst = self._reg(ops[0]) if ops and ops[0].type == cs_arm.ARM_OP_REG else None
                if insn.id == cs_arm.ARM_INS_CMP and len(ops) == 2 and ops[1].type == cs_arm.ARM_OP_IMM \
                        and (reg is None or dst == reg):
                    if ops[1].imm == 0:
                        continue      # `cmp rI, #0` : garde de borne inférieure
                    nxt = insns[i + 1] if i + 1 < len(insns) else None
                    if nxt is not None and nxt.id == cs_arm.ARM_INS_B and nxt.cc in self.LOWER_CC:
                        continue      # la branche part quand rX est *plus petit* : borne inférieure
                    n = ops[1].imm - off
                    if nxt is not None and nxt.id == cs_arm.ARM_INS_B and nxt.cc in (cs_arm.ARM_CC_GE, cs_arm.ARM_CC_HS):
                        n -= 1        # `bge défaut` : les cas vont de 0 à N-1
                    return n if n >= 0 else None
                if reg is None or dst != reg:
                    continue
                # le registre d'index est écrit : on suit le décalage et les copies
                if insn.id == cs_arm.ARM_INS_SUB and len(ops) >= 2 and ops[-1].type == cs_arm.ARM_OP_IMM:
                    off += ops[-1].imm
                    if len(ops) == 3 and ops[1].type == cs_arm.ARM_OP_REG:
                        reg = self._reg(ops[1])
                elif insn.id == cs_arm.ARM_INS_ADD and len(ops) >= 2 and ops[-1].type == cs_arm.ARM_OP_IMM:
                    off -= ops[-1].imm
                    if len(ops) == 3 and ops[1].type == cs_arm.ARM_OP_REG:
                        reg = self._reg(ops[1])
                elif insn.id == cs_arm.ARM_INS_MOV and len(ops) == 2 and ops[1].type == cs_arm.ARM_OP_REG:
                    reg = self._reg(ops[1])
                else:
                    return None       # écrit autrement (chargé, calculé) : la borne nous échappe
        return None

    def switch_thumb(self, insn, block_insns, block: Block, fn: "Function | None" = None) -> list[int]:
        """Table de sauts CodeWarrior en Thumb :

            cmp rX, #N ; bhi défaut ; adds rY, rX, rX ; add rY, pc ; ldrh rY, [rY, #K]
            (extension de signe) ; add rZ, pc ; bx rZ      ou      add pc, rY
        """
        table = None
        entry_size = 2
        idx_reg = None
        block_insns = self._with_physical_predecessors(block_insns, fn)
        cut = len(block_insns)
        for i, prev in enumerate(block_insns[:-1]):
            if prev.id in (cs_arm.ARM_INS_LDRH, cs_arm.ARM_INS_LDRB) and prev.operands[1].type == cs_arm.ARM_OP_MEM and i > 0:
                before = block_insns[i - 1]
                if (before.id == cs_arm.ARM_INS_ADD and len(before.operands) == 2
                        and self._reg(before.operands[1]) == cs_arm.ARM_REG_PC):
                    table = before.address + 4 + prev.operands[1].mem.disp
                    entry_size = 2 if prev.id == cs_arm.ARM_INS_LDRH else 1
                    # registre d'index : celui que double l'instruction qui précède le `add rY, pc`
                    # (`adds rY, rI, rI` ou `lsls rY, rI, #1`), la table étant en demi-mots
                    base = self._reg(before.operands[0])
                    idx_reg, cut = None, i - 1
                    if i >= 2:
                        d = block_insns[i - 2]
                        dops = d.operands
                        if dops and dops[0].type == cs_arm.ARM_OP_REG and self._reg(dops[0]) == base:
                            if d.id == cs_arm.ARM_INS_ADD and len(dops) == 3 and dops[1].type == dops[2].type == cs_arm.ARM_OP_REG \
                                    and self._reg(dops[1]) == self._reg(dops[2]):
                                idx_reg, cut = self._reg(dops[1]), i - 2
                            elif d.id == cs_arm.ARM_INS_LSL and len(dops) == 3 and dops[2].type == cs_arm.ARM_OP_IMM \
                                    and dops[2].imm == 1 and dops[1].type == cs_arm.ARM_OP_REG:
                                idx_reg, cut = self._reg(dops[1]), i - 2
        if table is None:
            return []
        limit = self._cmp_limit(block_insns, fn, idx_reg, cut)
        if limit is not None and limit > 512:
            return []
        base = insn.address + 4
        if insn.id == cs_arm.ARM_INS_BX:
            for prev in reversed(block_insns[:-1]):
                if prev.id == cs_arm.ARM_INS_ADD and len(prev.operands) == 2 and self._reg(prev.operands[1]) == cs_arm.ARM_REG_PC:
                    base = prev.address + 4
                    break
        targets = []
        count = limit + 1 if limit is not None else 256
        for i in range(count):
            if entry_size == 2:
                raw = self.memory.u16(table + 2 * i)
                if raw is None:
                    break
                offset = raw - 0x10000 if raw & 0x8000 else raw
            else:
                b = self.memory.read(table + i, 1)
                if not b:
                    break
                offset = b[0]
            target = (base + offset) & ~1
            if limit is None:
                # borne inconnue : on s'arrête à la première entrée invraisemblable
                if not self.in_code(target) or abs(offset) > 0x2000 or target < table + 2 * i and target > table - 0x2000 and target >= insn.address - 0x800 and target <= insn.address and False:
                    break
                if not (insn.address - 0x1000 <= target <= insn.address + 0x2000):
                    break
            targets.append(target)
        for a in range(table, table + entry_size * len(targets), 2):
            block.pools.append(a & ~3)
        return targets

    def switch_arm(self, insn, block_insns, block: Block, fn: "Function | None" = None) -> list[int]:
        """`add pc, pc, rX, lsl #2` (tableau de `b`) ou `ldr pc, [pc, rX, lsl #2]` (adresses)."""
        # La borne du `cmp` qui précède n'est pas fiable : CodeWarrior écrit
        # `cmp rX, #N ; bgt défaut ; cmp rX, #0 ; addge pc, pc, rX, lsl #2`, et c'est le
        # `cmp rX, #0` (la garde du `addge`) que l'on trouve en dernier. La table se lit
        # d'elle-même : des `b` inconditionnels consécutifs (add), ou des adresses de code
        # consécutives (ldr). La borne du `cmp` ne sert que de minimum.
        limit = self._cmp_limit(block_insns, fn)
        if limit is not None and limit > 512:
            return []
        base = insn.address + 8
        scanned = 0
        if insn.id == cs_arm.ARM_INS_ADD:
            while scanned < 512:
                entry = self.disasm(base + 4 * scanned, ARM, 1)
                if not entry or entry[0].id != cs_arm.ARM_INS_B or entry[0].cc not in (cs_arm.ARM_CC_AL, cs_arm.ARM_CC_INVALID):
                    break
                scanned += 1
        elif insn.id == cs_arm.ARM_INS_LDR:
            while scanned < 512:
                v = self.memory.u32(base + 4 * scanned)
                if v is None or not self.in_code(v) or v & 3 or not (insn.address - 0x4000 <= v <= insn.address + 0x4000):
                    break
                scanned += 1
        count = max(scanned, (limit + 1) if limit is not None else 0)
        if count == 0:
            count = 64 if limit is None else limit + 1
        targets = []
        if insn.id == cs_arm.ARM_INS_ADD:
            for i in range(count):
                targets.append(base + 4 * i)
        elif insn.id == cs_arm.ARM_INS_LDR:
            for i in range(count):
                v = self.memory.u32(base + 4 * i)
                if v is None or not self.in_code(v):
                    break
                targets.append(v)
                block.pools.append(base + 4 * i)
        return targets

    # ------------------------------------------------------------------ sémantique de contrôle
    LOADS = {cs_arm.ARM_INS_LDR, cs_arm.ARM_INS_LDRB, cs_arm.ARM_INS_LDRH, cs_arm.ARM_INS_LDRSB,
             cs_arm.ARM_INS_LDRSH, cs_arm.ARM_INS_LDRD}
    LDM = {cs_arm.ARM_INS_LDM, cs_arm.ARM_INS_LDMDA, cs_arm.ARM_INS_LDMDB, cs_arm.ARM_INS_LDMIB, cs_arm.ARM_INS_POP}
    PC_WRITERS = {cs_arm.ARM_INS_MOV, cs_arm.ARM_INS_LDR, cs_arm.ARM_INS_ADD, cs_arm.ARM_INS_SUB,
                  cs_arm.ARM_INS_ORR, cs_arm.ARM_INS_EOR, cs_arm.ARM_INS_BIC, cs_arm.ARM_INS_AND}

    def call_by_register(self, insn, block_insns) -> bool:
        """`bx rN` précédé d'une instruction qui pose lr sur l'adresse suivante : c'est un appel.

        Le coprocesseur est un ARMv4 : il n'a pas `blx rN`, et appelle par registre en écrivant
        lui-même l'adresse de retour, `mov lr, pc` puis `bx rN` (ou `add lr, pc, #imm`, ou un
        `ldr lr, [pc, #imm]` sur un littéral qui vaut l'adresse d'après). Le flot reprend donc
        après le `bx`, là où un saut ordinaire l'aurait laissé tomber."""
        if len(block_insns) < 2 or block_insns[-1].address != insn.address:
            return False
        prev = block_insns[-2]
        if prev.address + prev.size != insn.address:
            return False
        pops = prev.operands
        if len(pops) < 2 or self._reg(pops[0]) != cs_arm.ARM_REG_LR:
            return False
        after = insn.address + insn.size
        if prev.id == cs_arm.ARM_INS_MOV:
            return self._reg(pops[1]) == cs_arm.ARM_REG_PC
        if prev.id == cs_arm.ARM_INS_ADD and len(pops) >= 3:
            if self._reg(pops[1]) != cs_arm.ARM_REG_PC or pops[2].type != cs_arm.ARM_OP_IMM:
                return False
            return prev.address + 8 + pops[2].imm == after
        if prev.id == cs_arm.ARM_INS_LDR and pops[1].type == cs_arm.ARM_OP_MEM:
            if pops[1].mem.base != cs_arm.ARM_REG_PC or pops[1].mem.index != 0:
                return False
            value = self.memory.u32(((prev.address + 8) & ~3) + pops[1].mem.disp)
            return value is not None and (value & ~1) == after
        return False

    def analyse(self, insn, mode: int, block: Block, fn: Function, block_insns) -> tuple[bool, list[int]]:
        """Interprète l'instruction pour le flot de contrôle. (terminal, successeurs).

        Capstone met la condition dans le mnémonique (`bne`, `popeq`) : on
        raisonne sur `insn.id`, jamais sur le texte.
        """
        iid = insn.id
        ops = insn.operands
        next_addr = insn.address + insn.size
        cond = insn.cc not in (cs_arm.ARM_CC_AL, cs_arm.ARM_CC_INVALID)
        origin_pool = "pool" if fn.strong else "pool-faible"

        # Pool de constantes : ldr rX, [pc, #imm]
        if iid in self.LOADS:
            for op in ops:
                if op.type == cs_arm.ARM_OP_MEM and op.mem.base == cs_arm.ARM_REG_PC and op.mem.index == 0:
                    base = (insn.address + (4 if mode == THUMB else 8)) & ~3
                    pool = base + op.mem.disp
                    block.pools.append(pool)
                    value = self.memory.u32(pool)
                    if value is not None and self.in_code(value & ~1):
                        if value & 1:
                            fn.seeds.append((value & ~1, THUMB, origin_pool))
                        elif not value & 3:
                            fn.arm_candidates.append(value)

        if iid == cs_arm.ARM_INS_BL:
            target = ops[0].imm
            block.calls.append(target | (1 if mode == THUMB else 0))
            if mode == THUMB and self.is_piece(fn, target, insn.address):
                # `bl` employé comme branchement long vers un morceau de cette fonction : on continue
                # dans le morceau (et après le `bl`, que le flot atteint autrement)
                self.absorb(fn, target, "cible de branchements longs")
                return False, [target, next_addr]
            fn.seeds.append((target, mode, "bl"))
            self.call_sites.setdefault(target, set()).add((insn.address, fn.entry, "bl"))
            return False, []
        if iid == cs_arm.ARM_INS_BLX:
            if ops and ops[0].type == cs_arm.ARM_OP_IMM:
                target = ops[0].imm
                other = ARM if mode == THUMB else THUMB
                block.calls.append(target | (1 if other == THUMB else 0))
                fn.seeds.append((target, other, "blx"))
            return False, []  # blx rN : appel indirect

        if iid == cs_arm.ARM_INS_B:
            target = ops[0].imm
            if cond:
                return False, [target, next_addr]
            if self.is_tail_call(target, block, block_insns, fn):
                block.calls.append(target | (1 if mode == THUMB else 0))
                fn.seeds.append((target, mode, "queue"))
                self.call_sites.setdefault(target, set()).add((insn.address, fn.entry, "queue"))
                return True, []
            return True, [target]
        if iid == cs_arm.ARM_INS_BX:
            if cond:
                return False, [next_addr]
            if self._reg(ops[0]) != cs_arm.ARM_REG_LR and self.call_by_register(insn, block_insns):
                return False, [next_addr]
            if mode == THUMB and self._reg(ops[0]) != cs_arm.ARM_REG_LR:
                targets = self.switch_thumb(insn, block_insns, block, fn)
                if targets:
                    fn.switches[insn.address] = targets
                    return True, targets
            return True, []
        if iid in (cs_arm.ARM_INS_CBZ, cs_arm.ARM_INS_CBNZ):
            return False, [ops[1].imm, next_addr]

        if iid in self.LDM:
            if any(self._reg(op) == cs_arm.ARM_REG_PC for op in ops):
                return (False, [next_addr]) if cond else (True, [])
            return False, []
        if iid in self.PC_WRITERS and ops and self._reg(ops[0]) == cs_arm.ARM_REG_PC:
            if iid == cs_arm.ARM_INS_MOV and len(ops) == 2 and self._reg(ops[1]) == cs_arm.ARM_REG_LR:
                return (False, [next_addr]) if cond else (True, [])
            targets = (self.switch_thumb(insn, block_insns, block, fn) if mode == THUMB
                       else self.switch_arm(insn, block_insns, block, fn))
            if not targets:
                self.switch_sites.append((insn.address, mode))
            else:
                fn.switches[insn.address] = targets
            return (False, [next_addr] + targets) if cond else (True, targets)
        return False, []

    STACK_OPS = {cs_arm.ARM_INS_PUSH, cs_arm.ARM_INS_STMDB, cs_arm.ARM_INS_STM}

    def is_tail_call(self, target: int, block: Block, block_insns, fn: Function) -> bool:
        """Un `b` inconditionnel est un saut de queue si sa cible précède l'entrée
        de la fonction, ou s'il clôt un premier bloc minuscule sans cadre de
        pile (thunk C++ : `ldr ip, [r0] ; ldr ip, [ip, #-0xC] ; add r0, r0, ip ; b f`)."""
        if target < fn.entry:
            return True
        if block.start == fn.entry and len(block_insns) <= 6:
            for insn in block_insns:
                if insn.id in self.STACK_OPS:
                    return False
                if insn.id == cs_arm.ARM_INS_SUB and insn.operands and self._reg(insn.operands[0]) == cs_arm.ARM_REG_SP:
                    return False
            return True
        return False

    # ------------------------------------------------------------------ acceptation / éviction
    def pool_ghosts(self, fn: Function) -> set[int]:
        """Fonctions nées d'une amorce venue d'un autre overlay dont toutes les instructions sont
        des mots de pool (ou du code) de `fn` : ce sont les constantes de `fn` décodées comme des
        instructions, elles sont fausses."""
        fn_pools = {p for b in fn.blocks.values() for p in b.pools}
        fn_addrs = set(fn.addresses())
        ghosts: set[int] = set()
        for p in fn_pools:
            o = self.pools.get(p)
            if o is None or o == fn.entry or o in ghosts:
                continue
            other = self.functions.get(o)
            if other is not None and other.external and \
                    all((a & ~3) in fn_pools or a in fn_addrs for a in other.addresses()):
                ghosts.add(o)
        return ghosts

    def misphased_externals(self, fn: Function) -> set[int]:
        """Fonctions dont l'entrée tombe au milieu d'une instruction de `fn` : les deux ne peuvent
        être vraies. Nées d'ailleurs (l'amorce visait un autre overlay du même emplacement), ou
        simples conjectures (niveau < 2 : un saut de queue Thumb au milieu du `bx lr` d'un accesseur
        ARM) — à condition que l'entrée de `fn` ne soit pas elle-même dans leur code."""
        starts = {i.address for insns in fn.insns.values() for i in insns}
        addrs = set(fn.addresses())
        out: set[int] = set()
        for a in fn.addresses():
            o = self.owner.get(a)
            if o is None or o == fn.entry or o in out:
                continue
            other = self.functions.get(o)
            if other is None or other.entry not in addrs or other.entry in starts:
                continue
            if other.external or (other.level < 2 and fn.entry not in set(other.addresses())):
                out.add(o)
        return out

    def pool_conflicts(self, fn: Function) -> set[int]:
        """Propriétaires étrangers des pools de `fn`, hors fantômes de ses propres constantes et
        hors fonctions nées d'ailleurs : une amorce d'un autre overlay peut désigner le milieu de
        la fonction que `fn` commence (les deux moitiés partagent alors les mêmes constantes)."""
        ghosts = self.pool_ghosts(fn)
        out: set[int] = set()
        for b in fn.blocks.values():
            for p in b.pools:
                o = self.pools.get(p)
                if o is None or o == fn.entry or o in ghosts:
                    continue
                other = self.functions.get(o)
                if other is not None and other.external:
                    continue
                out.add(o)
        return out

    def accept(self, fn: Function) -> None:
        entry = fn.entry
        self._why_fn(fn, "acceptée")
        # Évince les fonctions moins sûres que l'on recouvre, et les fantômes de nos constantes.
        victims = {self.owner[a] for a in fn.addresses() if a in self.owner and self.owner[a] != entry}
        for v in victims:
            other = self.functions.get(v)
            if other is not None and other.level < fn.level:
                self._why_fn(other, f"évincée : recouverte par 0x{entry:08X} ({fn.origin}, niveau {fn.level})")
                self.evict(v)
        for g in self.pool_ghosts(fn):
            other = self.functions.get(g)
            if other is not None:
                self._why_fn(other, "évincée : ses instructions sont les mots de pool d'une autre fonction")
                self.evict(g)
        for m in self.misphased_externals(fn):
            other = self.functions.get(m)
            if other is not None:
                self._why_fn(other, "évincée : son entrée est au milieu d'une de nos instructions")
                self.evict(m)
        self.functions[entry] = fn
        for a in fn.addresses():
            self.covered[a] = fn.mode
            self.owner[a] = entry
        for b in fn.blocks.values():
            for p in b.pools:
                self.pools.setdefault(p, entry)
        # les amorces d'une conjecture sont des conjectures de rang inférieur : elles ne peuvent
        # ni couper ni évincer une conjecture directe (prologue, candidat ARM)
        # ... de même pour celles d'une fonction née d'ailleurs dans un emplacement partagé : son
        # appel visait peut-être un autre overlay, et un fantôme évincé ensuite laisserait derrière
        # lui des sauts de queue de niveau 2 qui évincent de vraies fonctions
        weak = fn.level < 2 or (fn.external and self.ambiguous(fn.entry))
        for addr, mode, origin in fn.seeds:
            if weak and origin in ("bl", "blx", "queue"):
                origin += "-faible"
            self.seed(addr, mode, origin)
        self.arm_candidates.update(fn.arm_candidates)
        # un morceau exploré comme fonction avant nous (notre unique `bl` lointain le visait) :
        # absorbé, et nous serons ré-explorée quand la file sera vide
        if fn.mode == THUMB:
            for t in sorted({c & ~1 for c in fn.calls}):
                piece = self.functions.get(t)
                if piece is not None and t != entry and self.piece_of(piece, fn):
                    self.reexplored.add((t, entry))
                    self.absorb(fn, t, "cible de branchements longs")
                    self.dirty.add(entry)

    def evict(self, entry: int, keep_phantoms: bool = False) -> None:
        """Retire la fonction ; ses amorces redeviennent candidates, son entrée la première.
        `keep_phantoms` : elle ne fait que se ré-explorer, ce qu'elle a absorbé lui reste."""
        fn = self.functions.pop(entry, None)
        if fn is None:
            return
        self._why_fn(fn, "évincée")
        requeue = []
        for a in fn.addresses():
            if self.owner.get(a) == entry:
                self.owner.pop(a, None)
                self.covered.pop(a, None)
                # une amorce écartée parce que cette fonction occupait la place redevient candidate
                seed = self.seen_seeds.get(a)
                if seed is not None:
                    requeue.append((a, seed[0], seed[1], seed[2]))
        # l'entrée de la fonction en dernier : reprise la première, elle ré-absorbe ce qui lui
        # revient avant que les amorces de son corps ne soient explorées
        for r in requeue:
            if r[0] != entry:
                self._enqueue(*r)
        for r in requeue:
            if r[0] == entry:
                self._enqueue(*r)
        for p, o in list(self.pools.items()):
            if o == entry:
                del self.pools[p]
        if not keep_phantoms:
            self.release_phantoms(entry)

    def drain(self) -> None:
        """Vide la file d'amorces ; puis ré-explore, une fois chacune, les fonctions auxquelles des
        morceaux sont revenus entre-temps, et recommence tant qu'il y en a."""
        while True:
            self._drain_pending()
            if not self.dirty:
                break
            for e in sorted(self.dirty):
                f = self.functions.get(e)
                if f is None:
                    continue
                self._why_fn(f, "ré-explorée : des morceaux lui reviennent")
                if e not in self.seen_seeds:
                    self.seen_seeds[e] = (f.mode, f.origin, f.external)
                self.evict(e, keep_phantoms=True)
            self.dirty.clear()

    def _drain_pending(self) -> None:
        while self.pending:
            addr, mode, origin, external = self.pending.pop()
            self.queued.discard(addr)
            best = self.seen_seeds.get(addr)
            if best is not None and LEVEL[best[1]] > LEVEL[origin]:
                origin = best[1]   # une proposition plus sûre est arrivée pendant l'attente
            if addr in self.functions:
                continue
            if addr in self.phantoms:
                self._why(addr, addr, f"amorce ignorée : écartée par 0x{self.phantoms[addr]:08X}")
                self.entries.discard(addr)
                continue
            level = LEVEL[origin]
            owner = self.owner.get(addr)
            if owner is not None:
                other = self.functions.get(owner)
                # une conjecture de l'autre mode occupe la place, mais le voisinage est du nôtre :
                # c'est elle qui a tort (un saut de queue Thumb tombé dans une zone ARM avale
                # volontiers les accesseurs qui s'y trouvent)
                if other is not None and other.level < 2 and other.mode != mode                         and self.neighbourhood_mode(addr, other.entry) == mode and addr != other.entry:
                    self._why_fn(other, f"évincée : conjecture d'un autre mode que son voisinage (amorce {origin})")
                    self.evict(owner)
                    other = self.functions.get(self.owner.get(addr, -1))
                if other is not None and other.level >= level:
                    continue  # déjà un bloc d'une fonction au moins aussi sûre
            # garde-fou : une amorce explorée sans fin (évictions et ré-explorations qui se
            # répondent) est abandonnée, et signalée
            n = self.explored_counts[addr] = self.explored_counts.get(addr, 0) + 1
            if TRACE:
                print(f"    trace drain 0x{addr:08X} {origin} ext={external} exploration={n} file={len(self.pending)}", flush=True)
            if n > 40:
                if n == 41:
                    print(f"  ! boucle : l'amorce 0x{addr:08X} ({origin}) a été explorée 40 fois, abandonnée")
                    self._why(addr, addr, "abandonnée : explorée 40 fois")
                self.entries.discard(addr)
                continue
            fn = self.explore(addr, mode, origin, external)
            if fn is None:
                self._why(addr, addr, f"exploration vide ({origin})")
                self.entries.discard(addr)
                self.release_phantoms(addr)
                continue
            # un pointeur de données, ou un appel venu d'un autre overlay du même emplacement (qui
            # visait le code d'un autre), qui tombe sur du texte
            if (origin in ("pool", "pool-faible", "ptr", "ptr-code", "ptr-arm") or external) and self.text_like(fn):
                self._why_fn(fn, "refusée : du texte (moins de 4,5 % d'octets >= 0x80 sur plus de 24 octets)")
                self.entries.discard(addr)
                self.release_phantoms(addr)
                continue
            # les origines de données (un pool ou un mot qui pointe ici) ne prouvent pas du code :
            # une vtable ou une table de rappels en est la cible la plus fréquente
            if (level < 2 or origin in ("pool", "ptr", "ptr-code", "ptr-arm", "prologue"))                     and self.pointer_table_like(fn):
                self._why_fn(fn, "refusée : une table de pointeurs, pas du code")
                self.entries.discard(addr)
                self.release_phantoms(addr)
                continue
            if external:
                # une amorce venue d'un autre overlay qui tombe au milieu d'une instruction d'une
                # fonction de celui-ci visait un autre overlay du même emplacement
                o = self.owner.get(addr)
                other = self.functions.get(o) if o is not None else None
                if other is not None and addr not in {i.address for insns in other.insns.values() for i in insns}:
                    self._why_fn(fn, f"refusée : née d'ailleurs, au milieu d'une instruction de 0x{o:08X}")
                    self.entries.discard(addr)
                    self.release_phantoms(addr)
                    continue
                if other is not None and self.ambiguous(addr) and self.falls_into(other, addr):
                    self._why_fn(fn, f"refusée : née d'ailleurs, 0x{o:08X} y tombe par simple chute")
                    self.phantoms[addr] = o
                    self.entries.discard(addr)
                    self.release_phantoms(addr)
                    continue
            if origin in self.CALL_ORIGINS and mode == THUMB:
                # la fonction qui précède l'amorce, ou qui la vise par un `b`, l'a peut-être pour seule
                # cible de branchements longs (elle a été explorée avant que les appelants de l'amorce
                # ne soient tous connus, ou évincés) : on l'écarte et on ré-explore cette fonction
                # (le seul candidat est l'unique appelant vivant de l'amorce) ; l'appelant sera
                # ré-exploré quand la file sera vide, une fois pour tous ses morceaux
                callers = {e for _, e, _ in self.call_sites.get(addr, ()) if e in self.functions}
                if self.watch == addr:
                    print(f"    [why 0x{addr:08X}] appelants vivants : {[hex(c) for c in callers]} "
                          f"(sites : {[(hex(s_), hex(e), k) for s_, e, k in self.call_sites.get(addr, ())]})")
                piece = False
                for o in (callers if len(callers) == 1 else ()):
                    pf = self.functions[o]
                    if self.piece_of(fn, pf):
                        self._why_fn(fn, f"refusée : morceau de 0x{o:08X} (cible de ses branchements longs), qui sera ré-explorée")
                        self.reexplored.add((addr, o))
                        self.absorb(pf, addr, "cible de branchements longs")
                        self.dirty.add(o)
                        piece = True
                if piece:
                    continue
            # On ne recouvre que des fonctions moins sûres que soi (ou nées d'ailleurs et déphasées).
            misphased = self.misphased_externals(fn)
            stronger = sorted({o for a in fn.addresses()
                               if (o := self.owner.get(a)) is not None and o != addr and o in self.functions
                               and o not in misphased and self.functions[o].level >= level})
            if stronger:
                self._why_fn(fn, "refusée : recouvre une fonction au moins aussi sûre ("
                             + ", ".join(f"0x{o:08X} ({self.functions[o].origin}, 0x{self.functions[o].start:08X}-0x{self.functions[o].end:08X})" for o in stronger) + ")")
                self.entries.discard(addr)
                self.release_phantoms(addr)
                continue
            if level < 2 and (conflicts := self.pool_conflicts(fn)):
                # deux fonctions ne partagent jamais un pool : une fonction qui lit les mêmes mots
                # que nous et que nous seuls appelons, de loin, est un morceau de nous (explorée
                # comme fonction pendant que nous étions évincés) ; on l'absorbe et on se ré-explore
                pieces = [o for o in sorted(conflicts) if (o, addr) not in self.reexplored
                          and o in self.functions and self.long_branch_target(fn, o)]
                if pieces:
                    for o in pieces:
                        self._why_fn(fn, f"ré-explorée : 0x{o:08X}, qui partage ses pools, est un morceau d'elle")
                        self.reexplored.add((o, addr))
                        self.absorb(fn, o, "cible de branchements longs, pools partagés")
                    self._enqueue(addr, mode, origin, external)
                    continue
                self._why_fn(fn, "refusée : ses pools appartiennent à une autre fonction ("
                             + ", ".join(f"0x{o:08X}" for o in sorted(conflicts)) + ")")
                self.entries.discard(addr)
                self.release_phantoms(addr)
                continue
            if level < 2 and fn.size > 1024 and not fn.calls and len(fn.blocks) < fn.size // 512:
                self._why_fn(fn, "refusée : kilo-octets sans appel (table)")
                self.entries.discard(addr)
                self.release_phantoms(addr)
                continue  # des kilo-octets sans appel ni branchement : une table, pas du code
            # une amorce refusée ne doit plus couper les fonctions qui la traversent : elle
            # redeviendra une frontière si elle est proposée de nouveau (`seed`)
            self.accept(fn)

    # ------------------------------------------------------------------ amorces conjecturales
    def seed_pointers(self, region_names: list[str], collect: list | None = None) -> None:
        """Mots mémoire qui ressemblent à des pointeurs de code.

        Dans les données (au-delà de la fin du code), un pointeur Thumb est
        sûr. Dans le code, c'est une conjecture. Avec `collect`, les amorces
        Thumb sont rendues dans la liste au lieu d'être semées (les candidats
        ARM restent notés : ils ne sont jugés que dans les trous).
        """
        def propose(addr: int, origin: str) -> None:
            if collect is not None:
                collect.append((addr, THUMB, origin))
            else:
                self.seed(addr, THUMB, origin)

        for name in region_names:
            reg = self.memory.region(name)
            d = reg.data
            code_end = self.code_end.get(name, len(d))
            # suites d'au moins trois mots consécutifs qui pointent vers du code en Thumb : une
            # vtable ou une table de rappels. Leurs pointeurs valent même si une conjecture a
            # recouvert la zone (une grande fausse fonction née d'un pool, par exemple) : sans
            # cela, toute une classe de méthodes disparaît de la carte.
            vtable: set[int] = set()
            run: list[int] = []
            for off in range(0, len(d) - 3, 4):
                v = struct.unpack_from("<I", d, off)[0]
                if v & 1 and self.in_code(v & ~1):
                    run.append(off)
                else:
                    if len(run) >= 3:
                        vtable.update(run)
                    run = []
            if len(run) >= 3:
                vtable.update(run)
            for off in range(0, len(d) - 3, 4):
                v = struct.unpack_from("<I", d, off)[0]
                addr = reg.base + off
                in_data = off >= code_end
                if v & 1 and self.in_code(v & ~1):
                    if in_data or addr in self.pools or off in vtable:
                        propose(v & ~1, "ptr")
                    elif addr not in self.covered:
                        propose(v & ~1, "ptr-code")
                elif v and not v & 3 and self.in_code(v) and (in_data or addr not in self.covered):
                    self.arm_candidates.add(v)

    def looks_like_arm_function(self, addr: int) -> bool:
        if addr in self.covered or addr in self.pools:
            return False
        insns = self.disasm(addr, ARM, 6)
        if len(insns) < 6:
            return False
        first = insns[0]
        bad_first = {"svc", "bkpt", "udf", "pop", "ldmfd", "ldmda", "ldmdb", "ldmib", "stmda", "stmib", "swi",
                     "cdp", "ldc", "stc", "mrs", "msr", "smc", "bl", "blx", "nop"}
        base = first.mnemonic
        if base in bad_first or first.id == 0:
            return False
        if first.cc not in (cs_arm.ARM_CC_AL, cs_arm.ARM_CC_INVALID) and first.mnemonic not in ("b", "bx"):
            return False
        odd = sum(1 for i in insns if i.cc in (cs_arm.ARM_CC_VS, cs_arm.ARM_CC_VC) and i.mnemonic != "b")
        return odd == 0

    def neighbourhood_mode(self, addr: int, skip_owner: int | None = None) -> int | None:
        """Mode du code qui entoure l'adresse. `skip_owner` : ignorer une fonction (celle dont on
        met justement le mode en doute — sinon elle se donne raison à elle-même)."""
        def mode_at(a: int) -> int | None:
            if skip_owner is not None and self.owner.get(a) == skip_owner:
                return None
            return self.covered.get(a)
        before = after = None
        for delta in range(2, 0x400, 2):
            if before is None and (m := mode_at(addr - delta)) is not None:
                before = m
            if after is None and (m := mode_at(addr + delta)) is not None:
                after = m
            if before is not None and after is not None:
                break
        if before is None:
            return after
        if after is None:
            return before
        return before if before == after else None

    def sweep_prologues(self, region_names: list[str]) -> int:
        """Prologues dans les trous, dans le mode du voisinage :
        `push {…, lr}` en Thumb, `stmfd sp!, {…, lr}` en ARM."""
        added = 0
        for name in region_names:
            reg = self.memory.region(name)
            d = reg.data
            limit = min(self.code_end.get(name, len(d)), len(d))
            for off in range(0, limit - 3, 2):
                addr = reg.base + off
                if addr in self.covered or (addr & ~3) in self.pools or addr in self.entries:
                    continue
                mode = self.neighbourhood_mode(addr)
                if d[off + 1] == 0xB5 and mode == THUMB and len(self.disasm(addr, THUMB, 4)) == 4:
                    self.seed(addr, THUMB, "prologue")
                    added += 1
                elif (off % 4 == 0 and mode == ARM and d[off + 3] == 0xE9 and d[off + 2] == 0x2D
                      and d[off + 1] & 0x40):
                    self.seed(addr, ARM, "prologue")
                    added += 1
                elif off % 4 == 0 and mode == ARM and self.is_arm_thunk(d, off):
                    self.seed(addr, ARM, "prologue")
                    added += 1
        return added

    def is_arm_thunk(self, d: bytes, off: int) -> bool:
        """Stub du SDK atteint par un pointeur calculé (rappels d'interruption numérotés) :
        `ldr ip, [pc, #4] ; mov r0, #n ; bx ip ; .word cible` ou `ldr ip, [pc, #0] ; bx ip ; .word cible`."""
        if off + 16 > len(d):
            return False
        w = [struct.unpack_from("<I", d, off + 4 * i)[0] for i in range(4)]
        if w[0] == 0xE59FC004 and w[2] == 0xE12FFF1C and (w[1] & 0xFFFFF000) == 0xE3A00000:
            target = w[3]
        elif w[0] == 0xE59FC000 and w[1] == 0xE12FFF1C:
            target = w[2]
        else:
            return False
        return self.in_code(target & ~1) and (target & 1 or target % 4 == 0)

    # ------------------------------------------------------------------ réparation
    def split_block(self, fn: Function, at: int) -> bool:
        """Coupe en deux le bloc de `fn` qui contient `at`, si `at` y est une instruction :
        le code généré aura une étiquette à cette adresse."""
        for start, b in fn.blocks.items():
            if b.start < at < b.end:
                insns = fn.insns[start]
                idx = next((k for k, i in enumerate(insns) if i.address == at), None)
                if idx is None:
                    return False  # pas en phase avec les instructions de cette fonction
                tail = Block(at, b.end, b.mode)
                tail.successors = b.successors
                b.end, b.successors = at, [at]
                fn.blocks[at] = tail
                fn.insns[start], fn.insns[at] = insns[:idx], insns[idx:]
                return True
        return False

    def repair(self) -> tuple[int, int, int]:
        """Toute sortie d'un bloc vers l'extérieur de sa fonction doit tomber sur une entrée
        de fonction ou sur un début de bloc d'une fonction connue : c'est ce que le code
        généré sait résoudre. Deux accidents de l'exploration l'en empêchent :

        - une **frontière orpheline** : une amorce a coupé le bloc (`entries`) sans jamais
          devenir une fonction (refusée, ou évincée). On retire la frontière et on ré-explore
          les fonctions coupées ;
        - une **chute au milieu d'un bloc** d'une autre fonction (voisines qui se chevauchent).
          On coupe ce bloc à l'adresse atteinte.

        Renvoie (fonctions ré-explorées, blocs coupés, sorties restées sans étiquette)."""
        reexplored = split = 0
        for _ in range(4):
            # une chute (le bloc s'arrête juste avant `s`) vers une adresse qui n'est ni une
            # fonction ni le code d'une autre fonction : la frontière qui a coupé ce bloc n'a
            # pas donné de fonction (refusée, ou évincée), qu'elle soit encore dans `entries` ou non
            orphans: set[int] = set()
            cut: dict[int, Function] = {}
            for fn in self.functions.values():
                for b in fn.blocks.values():
                    for s in b.successors:
                        if (s == b.end and s not in fn.blocks and s not in self.functions
                                and self.in_code(s) and self.owner.get(s) in (None, fn.entry)):
                            orphans.add(s)
                            cut[fn.entry] = fn
            if not orphans:
                break
            self.entries -= orphans
            for fn in cut.values():
                self._why_fn(fn, "ré-explorée : une frontière orpheline la coupait")
                self.evict(fn.entry)
                new = self.explore(fn.entry, fn.mode, fn.origin, fn.external)
                if new is not None:
                    self.accept(new)
                    reexplored += 1
            self.drain()
        unresolved = 0
        for fn in list(self.functions.values()):
            for b in list(fn.blocks.values()):
                for s in b.successors:
                    if s in fn.blocks or s in self.functions or not self.in_code(s):
                        continue
                    other = self.functions.get(self.owner.get(s, -1))
                    if other is not None and other.mode == fn.mode:
                        if s in other.blocks:
                            continue  # début de bloc d'une fonction connue : le code généré y entre
                        if self.split_block(other, s):
                            split += 1
                            continue
                    unresolved += 1
                    self.unlabeled.append((fn.entry, b.start, s, self.owner.get(s)))
                    self._why(s, fn.entry, "sortie de bloc sans étiquette possible")
        return reexplored, split, unresolved

    # ------------------------------------------------------------------ pilotage
    def run(self, region_names: list[str], code_end: dict[str, int] | None = None, rounds: int = 4) -> None:
        self.code_end = code_end or {}
        self.drain()                      # amorces fortes fournies, et tout ce qu'elles atteignent
        self.seed_pointers(region_names)
        self.drain()
        for _ in range(rounds):
            before = len(self.functions)
            for cand in sorted(self.arm_candidates):
                if cand not in self.functions and self.looks_like_arm_function(cand):
                    self.seed(cand, ARM, "ptr-arm")
            self.drain()
            self.sweep_prologues(region_names)
            self.drain()
            if len(self.functions) == before:
                break
        self.repaired = self.repair()
        # amorces orphelines : proposées par le flot (appels, sauts de queue) puis laissées sans
        # fonction — leur propriétaire d'alors a été évincé, refusé ou coupé — reprises une fois
        orphans = [(a, s) for a, s in self.seen_seeds.items()
                   if a not in self.functions and self.owner.get(a) is None and a not in self.phantoms
                   and s[1] in self.FLOW_ORIGINS and self.in_code(a)]
        self.orphans_retried = len(orphans)
        if orphans:
            for a, (m, o, e) in orphans:
                self._enqueue(a, m, o, e)
            self.drain()
            r = self.repair()
            self.repaired = tuple(x + y for x, y in zip(self.repaired, r))

    # ------------------------------------------------------------------ résultats
    def summary(self) -> dict:
        per_region: dict[str, dict] = {}
        for fn in self.functions.values():
            name = self.region_name(fn.entry)
            s = per_region.setdefault(name, {"fonctions": 0, "arm": 0, "thumb": 0, "octets": 0, "faibles": 0})
            s["fonctions"] += 1
            s["arm" if fn.mode == ARM else "thumb"] += 1
            s["octets"] += fn.size
            if not fn.strong:
                s["faibles"] += 1
        return per_region

    def coverage(self, base: int, end: int) -> int:
        """Octets couverts par des instructions ou des pools dans [base, end)."""
        insn_bytes = 0
        for a, mode in self.covered.items():
            if base <= a < end:
                insn_bytes += 2
        pool_bytes = sum(4 for p in self.pools if base <= p < end)
        return insn_bytes + pool_bytes

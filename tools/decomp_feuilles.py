#!/usr/bin/env python3
"""Réécrit d'un coup les petites feuilles du jeu : accesseurs, prédicats, relais.

Près d'un tiers des fonctions du jeu tiennent en quelques instructions sans appel ni
boucle : « rends le mot à +0x160 », « écris r1 en +0x10 », « rends 1 si l'octet +0xB8 est
nul », ou le relais d'héritage multiple du C++ qui décale `this` avant de sauter à la vraie
méthode. Les réécrire une à une n'aurait aucun sens ; elles ont toutes la même forme.

Ce générateur les **interprète symboliquement** : il suit, instruction par instruction,
ce que contient chaque registre en fonction des valeurs d'entrée, ce qui est lu et écrit
en mémoire, et quels drapeaux restent posés. À chaque branchement conditionnel il se
dédouble, et la fonction devient un arbre de `if` dont chaque feuille est un chemin. De
l'état final de chaque chemin il tire deux choses :

  * du C qui produit cet état directement — lectures et écritures dans l'ordre d'origine,
    registres et drapeaux posés à la fin. Ce n'est pas la traduction recopiée : c'est son
    résultat, recalculé par une autre voie, et c'est ce qui donne un sens à la comparaison
    que fait ensuite le banc (SFC_VERIF=auto) ;
  * une description de l'effet — « rend [arg0 + 0x160] » — qui nourrit l'annuaire, car
    ces milliers d'accesseurs sont autant de témoignages sur la forme des objets.

Les constantes du pool de littéraux sont prises pour ce qu'elles sont : des constantes.

La preuve se tient par chemin. Le banc note quels chemins il a vus tourner ; une feuille
est prouvée quand tous les siens l'ont été, sur elle-même (« instance ») ou sur d'autres
feuilles dont le code ne diffère que par les constantes (« gabarit »).

Usage :
    python tools/decomp_feuilles.py                       # génère generated/natif_auto.inc
    python tools/decomp_feuilles.py --moisson liste.txt   # verse les verdicts du banc, régénère
    python tools/decomp_feuilles.py --faute               # abîme tout : le banc doit crier partout
"""
from __future__ import annotations

import argparse
import json
import re
import struct
import sys
import zlib
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from nitro import compress  # noqa: E402
from recomp.armdec import decode_arm  # noqa: E402
from recomp.insn import ASR, COND_NAMES, LSL, LSR, ROR, RRX  # noqa: E402
from recomp.thumbdec import decode_thumb  # noqa: E402

TAILLE_MAX = 32768       # la plus grande fonction du jeu fait 21 638 octets
CHEMINS_MAX = 32          # en arbre pur : au-delà, on recommence avec des jonctions
CHEMINS_MAX_JONCTIONS = 1024
PARTIES = 16
# --faute : chaque feuille abîme r12. Un banc qui ne crie pas sur toutes celles qu'il voit
# tourner ne compare pas ce qu'on croit ; on l'éprouve ainsi avant de le croire.
FAUTE = False


class Refus(Exception):
    """La fonction sort du cadre : on ne la réécrit pas, on ne devine rien."""


# ------------------------------------------------------------------ valeurs symboliques

class Val:
    """Une valeur : son expression C, ce qu'elle vaut si c'est une constante, et sa
    description lisible."""

    def __init__(self, c: str, desc: str, const: int | None = None, connu: "Val | None" = None,
                 racine: "Val | None" = None, offset: int = 0):
        self.c, self.desc, self.const = c, desc, const
        # ce qu'on sait de la valeur sans avoir le droit de s'en servir dans le code : un mot
        # relu sur la pile est relu, même quand on vient de l'y pousser
        self.connu = connu
        # « racine + offset » : les sommes de constantes se plient, et « (sp - 0x10) + 0xC »
        # s'écrit « sp - 4 » — dans le code comme dans la description
        self.racine, self.offset = racine, offset

    @staticmethod
    def constante(v: int) -> "Val":
        v &= 0xFFFFFFFF
        return Val(f"0x{v:X}u", f"0x{v:X}" if v > 9 else str(v), v)

    @staticmethod
    def nommee(c: str, desc: str) -> "Val":
        """Une valeur qui porte un nom — registre d'entrée, mot lu : elle est sa propre racine."""
        v = Val(c, desc)
        v.racine = v
        return v

    @staticmethod
    def decalee(racine: "Val", offset: int) -> "Val":
        offset &= 0xFFFFFFFF
        if offset == 0:
            return racine
        signe, k = ('-', (1 << 32) - offset) if offset >> 31 else ('+', offset)
        return Val(f"({racine.c} {signe} 0x{k:X}u)", f"({racine.desc} {signe} {'0x%X' % k if k > 9 else k})",
                   None, None, racine, offset)


def nu(desc: str) -> str:
    """Sans les parenthèses du tour, quand elles font bien le tour."""
    if desc.startswith('(') and desc.endswith(')'):
        niveau = 0
        for k, car in enumerate(desc):
            niveau += car == '('
            niveau -= car == ')'
            if niveau == 0 and k < len(desc) - 1:
                return desc
        return desc[1:-1]
    return desc


def binaire(op: str, a: Val, b: Val, calcul) -> Val:
    if a.const is not None and b.const is not None:
        return Val.constante(calcul(a.const, b.const))
    if op in ('+', '-') and b.const is not None and a.racine is not None:
        return Val.decalee(a.racine, a.offset + (b.const if op == '+' else -b.const))
    if op == '+' and a.const is not None and b.racine is not None:
        return Val.decalee(b.racine, b.offset + a.const)
    if op == '+' and b.const == 0:
        return a
    if op == '+' and a.const == 0:
        return b
    if op == '-' and b.const == 0:
        return a
    return Val(f"({a.c} {op} {b.c})", f"({a.desc} {op} {b.desc})")


def decale(src: Val, sens: str, n: int) -> Val:
    """Un décalage immédiat. La quantité reste écrite en clair : elle fait partie du gabarit,
    pas des constantes qu'on efface pour comparer deux feuilles."""
    if src.const is not None:
        return Val.constante(src.const << n if sens == '<<' else src.const >> n)
    return Val(f"({src.c} {sens} {n})", f"({src.desc} {sens} {n})")


# ------------------------------------------------------------------ l'état d'un chemin

class Etat:
    """Ce qu'on sait à un point du code, le long d'un chemin."""

    def __init__(self):
        self.reg: dict[int, Val] = {}
        self.drapeaux: list[tuple[frozenset, str]] = []   # (drapeaux posés, code C), dans l'ordre
        self.flag: dict[str, Val] = {}                    # l'expression de chaque drapeau
        self.prov: tuple | None = None                    # ('sub', a, b) si les quatre viennent d'un même cmp
        self.pile: dict[str, Val] = {}                    # ce qu'on a poussé, par adresse : pour décrire
        self.effets: list[str] = []
        self.faits: dict[str, bool] = {}                  # les conditions déjà tranchées sur ce chemin
        self.generation = 0                               # combien de fois tout a été relu sur ce chemin
        self.origine = 'appel'                            # pourquoi : un appel, ou une tête de boucle
        self.appels: list[str] = []                       # ce qu'on a appelé, pour décrire
        self.sp_avant: Val | None = None                  # sp à l'instant de l'appel : le même qu'après

    def copie(self) -> "Etat":
        e = Etat()
        e.reg, e.drapeaux, e.flag = dict(self.reg), list(self.drapeaux), dict(self.flag)
        e.faits, e.generation, e.appels = dict(self.faits), self.generation, list(self.appels)
        e.origine, e.sp_avant = self.origine, self.sp_avant
        e.prov, e.pile, e.effets = self.prov, dict(self.pile), list(self.effets)
        return e


# ------------------------------------------------------------------ l'interprète

class Resolution:
    """Comment la traduction appelle une cible : directement par son symbole quand c'est une
    fonction — ou un début de bloc — de l'ARM9 ou de la même région, dans le même mode ;
    sinon par le dispatch, qui pose le bit T d'après l'adresse. La réécriture doit faire le
    même choix, car le code appelé voit ce bit. C'est la règle de recomp.py, reproduite."""

    def __init__(self, codemap: dict):
        from recomp.insn import ARM, THUMB
        self.static_known: dict[int, int] = {}
        self.region_known: dict[str, dict[int, int]] = {}
        self.static_blocks: dict[int, tuple[int, int]] = {}
        self.region_blocks: dict[str, dict[int, tuple[int, int]]] = {}
        # le code toujours présent : « arm9 » dans la carte de l'ARM9, « arm7 » dans celle du coprocesseur
        self.statique = next((r for r in codemap if not r.startswith("ovl")), "arm9")
        for region, fns in codemap.items():
            self.region_known[region] = {f["entrée"]: (THUMB if f["mode"] == "thumb" else ARM) for f in fns}
            blocks: dict[int, tuple[int, int]] = {}
            for f in fns:
                m = THUMB if f["mode"] == "thumb" else ARM
                for start, _end in f.get("blocs_liste", []):
                    blocks.setdefault(start, (f["entrée"], m))
            self.region_blocks[region] = blocks
            if not region.startswith("ovl"):
                self.static_known.update(self.region_known[region])
                for k, v in blocks.items():
                    self.static_blocks.setdefault(k, v)

    def directe(self, region: str, addr: int, mode: int) -> tuple[str, int] | None:
        """La fonction que la traduction lie par son symbole pour cette cible — (région, entrée) —
        ou None quand elle passe par le dispatch. Deux fonctions peuvent se chevaucher : chercher
        par adresse à l'exécution n'en rendrait pas forcément la même."""
        if self.static_known.get(addr) == mode:
            return (self.statique, addr)
        if region.startswith("ovl") and self.region_known.get(region, {}).get(addr) == mode:
            return (region, addr)
        b = self.static_blocks.get(addr)
        if b is not None and b[1] == mode:
            return (self.statique, b[0])
        if region.startswith("ovl"):
            b = self.region_blocks.get(region, {}).get(addr)
            if b is not None and b[1] == mode:
                return (region, b[0])
        return None


class Feuille:
    resolution: "Resolution | None" = None
    noms: dict[int, str] = {}                # les noms certains de l'annuaire, pour décrire
    # Les points de modification (runtime/modifications.c) : (région, adresse) -> (rang, nom). Avant
    # l'instruction nommée, le code émis appelle un crochet que le runtime pose ou non ; sans lui, la
    # fonction fait exactement ce qu'elle faisait.
    points: dict[tuple[str, int], tuple[int, str]] = {}
    points_poses: set = set()
    # Les entrées secondaires : (région, entrée de la fonction, adresse) que le code émis rejoint
    # au milieu d'une fonction — un `b` vers le corps d'une autre, un `bl` hors de son entrée.
    visees: set = set()

    def __init__(self, module: str, base: int, donnees: bytes, fn: dict, indice: int = 0, jonctions: bool = False,
                 depart: int | None = None):
        self.module, self.base, self.donnees, self.fn = module, base, donnees, fn
        # On entre parfois dans une fonction ailleurs qu'à son entrée : la division signée finit
        # dans la non signée, un thread qui reprend retombe au milieu de l'ordonnanceur. Une
        # réécriture secondaire part de là ; tout ce qui parle de « l'entrée de la fonction »
        # — les règles du traducteur — continue de parler de celle de la fonction entière,
        # puisque c'est à sa traduction qu'on la compare.
        self.depart = fn['entrée'] if depart is None else depart
        self.secondaire = self.depart != fn['entrée']
        # En arbre pur, la suite d'un `if` est recopiée dans chaque branche : lisible, mais le
        # nombre de chemins double à chaque test. Avec des jonctions, un bloc que plusieurs
        # chemins rejoignent est un point où l'état est écrit et relu : il n'est émis qu'une fois.
        self.jonctions = jonctions
        self.chemins_max = CHEMINS_MAX_JONCTIONS if jonctions else CHEMINS_MAX
        self.arm = fn['mode'] == 'arm'
        self.pc = 0                          # ce que vaut pc lu comme opérande, pour l'instruction en cours
        self.indice = indice                 # sa place dans la table, pour noter les chemins vus
        self.entrees: set[int] = set()       # registres dont la valeur d'entrée sert
        self.temp = 0
        # chaque mot lu en mémoire : son temporaire, l'adresse d'où il vient et sa largeur — une écriture à
        # la même adresse dont la valeur le contient repart de sa propre valeur (x = x + v) : c'est un
        # accumulateur, qu'un tour d'aperçu mettra à l'échelle de la part de tour (runtime/apercu.c)
        self.lus: dict[str, tuple[str, str]] = {}
        self.chemins = 0                     # chemins terminés
        self.saute = False                   # un chemin au moins finit par un saut
        self.arbre: list = []
        self.description = ""
        self.remarquables: set[int] = set()  # les constantes auxquelles elle compare : le banc à
                                             # froid les glisse dans ses tirages, sans quoi le
                                             # chemin « r0 == 0x23 » ne serait jamais pris
        self.recursive = False
        self.aiguillages = 0                 # tables de sauts rencontrées
        self.rendus = 0                      # cas de ces tables rendus à la traduction
        self.generation_unique = 0
        self.globales: set[int] = set()      # les adresses fixes qu'elle lit : à froid elles gardent
                                             # la valeur du jeu à l'instant de l'essai, et la branche
                                             # qui dépend d'elles ne serait jamais prise — le banc y sème
        self.tetes: dict[int, bool] = {}     # têtes de boucle (cibles d'arêtes arrière) : émises ?
        self.reperer_boucles()

    def decoder(self, adresse: int):
        fin = self.fn['fin']
        if self.arm:
            if adresse + 4 > fin:
                return None
            return decode_arm(struct.unpack_from('<I', self.donnees, adresse - self.base)[0], adresse)
        if adresse + 2 > fin:
            return None
        hw = struct.unpack_from('<H', self.donnees, adresse - self.base)[0]
        hw2 = struct.unpack_from('<H', self.donnees, adresse + 2 - self.base)[0] if adresse + 4 <= fin else None
        return decode_thumb(hw, adresse, hw2)

    def reperer_boucles(self) -> None:
        """Un premier passage, bloc par bloc comme la cartographie les a délimités : on retient
        chaque instruction — c'est ce qui permet de reconnaître un `bl` employé comme branchement
        long, par la règle même de recomp.py — et les cibles des branchements vers l'arrière, qui
        sont les têtes de boucle. Une tête que rien n'atteint ne coûte rien."""
        debut, fin = self.fn['début'], self.fn['fin']
        self.instructions: dict[int, object] = {}
        self.precedente: dict[int, object] = {}
        self.indecodables: set[int] = set()
        plages = sorted((a, b) for a, b in self.fn.get('blocs_liste', [])) or [(debut, fin)]
        fusion: list[list[int]] = []
        for a, b in plages:
            if fusion and a <= fusion[-1][1]:
                fusion[-1][1] = max(fusion[-1][1], b)
            else:
                fusion.append([a, b])
        debuts = sorted({a for a, _ in plages} | {self.fn['entrée']})
        self.tables_de_sauts = {int(k): sorted(set(v)) for k, v in self.fn.get('sauts', {}).items()}
        for bloc_debut, bloc_fin in fusion:
            adresse, prec = bloc_debut, None
            while adresse < bloc_fin:
                i = self.decoder(adresse)
                if i is None or i.op == "UNDEF":
                    self.indecodables.add(adresse)   # des données prises pour du code : on n'ira pas
                    break
                # une instruction de quatre octets qui enjamberait un début de bloc : on repart de là
                suivant = next((b for b in debuts if adresse < b < adresse + i.size), None)
                if suivant is not None:
                    adresse = suivant
                    continue
                self.instructions[adresse] = i
                self.precedente[adresse] = prec
                prec = i
                adresse += i.size
        entree = self.fn['entrée']
        # Les tables que la carte range sous « switch », et le saut calculé de l'ARM — « add pc,
        # pc, rX » : les routines de division déroulées y sautent n'importe où, en arrière
        # compris ; pour le traducteur, toute instruction de la fonction est alors une cible.
        for k, v in self.fn.get('switch', {}).items():
            self.tables_de_sauts.setdefault(int(k), sorted(set(v)))
        for i in self.instructions.values():
            if self.arm and i.op == "ADD" and i.rd == 15 and i.rn == 15 and i.rm is not None:
                self.tables_de_sauts[i.addr] = sorted(self.instructions)
        # « mov lr, pc », « ldr lr, [pc, #n] » ou « add lr, pc, #n » juste avant un « bx rX » : un
        # appel par registre dont l'adresse de retour est choisie. La traduction appelle, puis
        # reprend à cette adresse ; c'est sa règle, mot pour mot.
        self.lr_avant: dict[int, int] = {}
        for i in self.instructions.values():
            prec = self.precedente.get(i.addr)
            if i.op != "BX" or i.rm == 14 or prec is None or prec.addr + prec.size != i.addr:
                continue
            pc_prec = prec.addr + (8 if self.arm else 4)
            if prec.op == "MOV" and prec.rd == 14 and prec.rm == 15 and prec.imm is None:
                self.lr_avant[i.addr] = pc_prec
            elif prec.op == "LDR" and prec.rd == 14 and prec.rn == 15 and not prec.reg_offset:
                try:
                    self.lr_avant[i.addr] = self.mot((pc_prec & ~3) + (prec.imm or 0))
                except Refus:
                    pass
            elif prec.op == "ADD" and prec.rd == 14 and prec.rn == 15 and prec.imm is not None:
                self.lr_avant[i.addr] = pc_prec + prec.imm
        for retour in self.lr_avant.values():
            if (retour & ~1) in self.instructions:
                self.tetes.setdefault(retour & ~1, False)     # on y reprend : une jonction
        for cibles in self.tables_de_sauts.values():
            for t in cibles:
                if t in self.instructions:
                    self.tetes.setdefault(t, False)
        self.cibles_longues = {t for t in self.fn.get('cibles_longues', []) if t in self.instructions and t != entree}
        if self.jonctions:
            arrivees: Counter = Counter()
            for i in self.instructions.values():
                suivante = i.addr + i.size
                saute = i.op == "B" or (i.op == "BL" and self.branchement_long_brut(i))
                if saute and i.target is not None and (i.target & ~1) in self.instructions:
                    arrivees[i.target & ~1] += 1
                termine = (i.op == "B" and i.cond == 14) or (i.op == "BX" and i.cond == 14) \
                    or (i.op == "LDM" and (i.reglist >> 15) & 1 and i.cond == 14) \
                    or (i.op == "BL" and i.cond == 14 and self.branchement_long_brut(i))
                if not termine and suivante in self.instructions:
                    arrivees[suivante] += 1
            for adresse, combien in arrivees.items():
                if combien >= 2 and adresse != entree:
                    self.tetes.setdefault(adresse, False)
        for i in self.instructions.values():
            if i.op == "B" and i.target is not None and debut <= i.target <= i.addr:
                self.tetes[i.target] = False
            if i.op == "BL" and i.target is not None and self.branchement_long(i) and i.target <= i.addr:
                self.tetes[i.target & ~1] = False

    def branchement_long_brut(self, i) -> bool:
        """Comme branchement_long, utilisable pendant qu'on dresse la carte des arrivées."""
        if self.arm or i.op != "BL" or i.target is None:
            return False
        t = i.target & ~1
        if t in self.cibles_longues:
            return True
        if t in self.instructions and t != self.fn['entrée'] and abs(t - i.addr) > 0x800:
            p_ = self.precedente.get(t)
            return p_ is not None and p_.addr + p_.size == t and p_.op not in ("BL", "BLX", "BX", "B", "SWI")
        return False

    def branchement_long(self, i) -> bool:
        """CodeWarrior emploie `bl` comme branchement long en Thumb, quand `b` ne porte pas. La
        règle de recomp.py : cible dans la fonction, hors entrée, déjà reconnue par la
        cartographie ou à plus de 2 Ko avec l'instruction qui la précède enchaînant par chute."""
        if self.arm or i.op != "BL" or i.target is None:
            return False
        t = i.target & ~1
        if t in self.cibles_longues:
            return True
        if t in self.instructions and t != self.fn['entrée'] and abs(t - i.addr) > 0x800:
            p_ = self.precedente.get(t)
            if p_ is not None and p_.addr + p_.size == t and p_.op not in ("BL", "BLX", "BX", "B", "SWI"):
                return True
        return False

    def a_neuf(self, e: Etat, origine: str = 'appel') -> None:
        """Après un point de jonction, tout est à relire : nouvelle génération."""
        if self.tables_de_sauts:
            # Les cas d'une table de sauts sont émis côte à côte, dans la même portée : leurs
            # générations doivent porter des numéros que personne d'autre n'a.
            self.generation_unique += 1
            e.generation = 100000 + self.generation_unique
        else:
            e.generation += 1
        e.origine = origine
        e.reg, e.flag, e.prov, e.pile, e.faits = {}, {}, None, {}, {}
        e.drapeaux = []

    def suite_ailleurs(self, e: Etat, sortie: list, adresse: int) -> str:
        """Le flot quitte la fonction sans appel : un `b` vers une autre, ou une chute dans la
        voisine. La traduction y va par le symbole de l'autre fonction quand elle le connaît —
        sans empiler de retour : ce n'est pas un appel —, sinon par rt_jump. On fait de même."""
        if self.chemins >= self.chemins_max:
            raise Refus("trop de chemins")
        numero = self.chemins
        self.chemins += 1
        description = self.decrire_chemin(e, None)
        self.materialiser(e, sortie)
        if FAUTE:
            sortie.append("c->r[12] ^= 1u;      /* faute délibérée : le banc doit la voir partout */")
        sortie.append(f"CHEMIN({self.indice}, {numero});")
        mode = 0 if self.arm else 1
        liee = self.resolution.directe(self.module, adresse, mode) if self.resolution else None
        if liee:
            if liee[1] != adresse:
                Feuille.visees.add((liee[0], liee[1], adresse))
            sortie.append(f'SUITE_DIRECTE(c, "{liee[0]}", 0x{liee[1]:08X}u, 0x{adresse:08X}u);')
            sortie.append("return;")
        else:
            self.saute = True
            sortie += [f"c->r[15] = 0x{adresse | mode:08X}u;", f"rt_jump(c, 0x{adresse | mode:08X}u);", "return;"]
        nom = self.noms.get(adresse, f"0x{adresse:08X}")
        return (description + " ; " if description != "ne fait rien" else "") + f"continue dans {nom}"

    def aiguiller(self, e: Etat, sortie: list, adresse: int, valeur: Val, masque: bool) -> str:
        """Une table de sauts : la traduction en fait un `switch` dont chaque cas saute à une
        étiquette de la fonction, et dont le défaut passe par rt_jump. Les cibles sont des têtes ;
        celles que rien n'a encore émises le sont à la suite."""
        self.materialiser(e, sortie)
        expr = f"({valeur.c} & ~1u)" if masque else valeur.c
        cibles = [t for t in self.tables_de_sauts[adresse] if t in self.instructions]
        self.saute = True                      # le défaut saute pour de bon
        sortie.append(('switch', expr, cibles))
        morceaux = []
        for rang, cible in enumerate(cibles):
            if self.tetes.get(cible):
                continue
            neuf = Etat()
            self.generation_unique += 1
            neuf.generation = 100000 + self.generation_unique               # des noms qui ne se marchent pas dessus
            neuf.origine = 'boucle'
            garde = (len(sortie), self.chemins, dict(self.tetes), self.saute)
            try:
                morceaux.append(f"cas 0x{cible:08X} : " + self.suivre(cible, neuf, sortie))
            except Refus as raison:
                if str(raison) == "trop de chemins":
                    raise
                # Un cas qu'on ne sait pas écrire — presque toujours un mot du pool de constantes,
                # que la règle « toute instruction est une cible » range parmi les cibles et que rien
                # n'atteint jamais — est rendu à la traduction, qui a cette étiquette. Rien à prouver
                # là : c'est elle qui tourne. Ce qu'on avait commencé d'émettre pour lui est retiré.
                del sortie[garde[0]:]
                self.chemins, self.tetes, self.saute = garde[1], garde[2], garde[3]
                self.tetes[cible] = True
                sortie.append(('label', f"L_{cible:08X}"))
                sortie.append(f"natif_suite(c, 0x{cible | (0 if self.arm else 1):08X}u);")
                morceaux.append(f"cas 0x{cible:08X} : rendu à la traduction ({raison})")
                self.rendus += 1
            # Les cas sont émis l'un après l'autre, au même niveau : sans ce retour, celui qui
            # finit par rendre la main tomberait dans le suivant.
            sortie.append("return;")
        self.aiguillages += 1
        return f"selon {nu(valeur.desc)} — " + " ; ".join(morceaux) if morceaux else f"selon {nu(valeur.desc)}"

    def reprendre_boucle(self, e: Etat, sortie: list, tete: int, description: str, arete: bool = True) -> str:
        """L'arête arrière : l'état est écrit, le temps simulé avance comme dans la traduction,
        et l'on saute à la tête — dont l'étiquette a été émise au premier passage. Quand on
        rejoint la tête en avançant — un autre chemin de l'arbre qui y tombe —, le saut n'est
        qu'un artifice de l'écriture en arbre : la traduction n'y compte pas d'arête, nous non
        plus. Le temps du jeu se compte en arêtes ; une de trop décale toute la suite."""
        if self.chemins >= self.chemins_max:
            raise Refus("trop de chemins")
        numero = self.chemins
        self.chemins += 1
        self.materialiser(e, sortie)
        sortie.append(f"CHEMIN({self.indice}, {numero});")
        if arete:
            sortie.append("if (--g_edge_budget < 0) rt_edge(c);")
        if not self.tetes.get(tete):
            # arrière en adresse seulement : la cible n'a pas encore été parcourue — un saut
            # par-dessus l'a laissée de côté. Le temps a avancé comme dans la traduction ; on
            # y va, et l'étiquette sera posée à l'arrivée.
            return self.suivre(tete, e, sortie)
        sortie.append(f"goto L_{tete:08X};")
        return description + f" ; reprend la boucle en 0x{tete:08X}"

    # -- registres
    def lire(self, e: Etat, n: int, sortie: list | None = None) -> Val:
        if n == 15:
            return Val.constante(self.pc)
        if n not in e.reg:
            if e.generation == 0:
                self.entrees.add(n)
                nom = {13: "sp", 14: "lr"}.get(n, f"arg{n}" if n < 4 else f"r{n}")
                e.reg[n] = Val.nommee(f"e{n}", nom)
            else:
                # après un appel, un registre qu'on n'a pas encore réécrit vaut ce que l'appelée
                # y a laissé : on le relit maintenant, il n'a pas bougé depuis
                assert sortie is not None
                sortie.append(f"const uint32_t e{n}_{e.generation} = c->r[{n}];")
                if e.origine == 'appel':
                    nom = f"résultat de l'appel {e.generation}" if n == 0 else \
                          {13: "sp", 14: "lr"}.get(n, f"r{n} après l'appel {e.generation}")
                else:
                    nom = {13: "sp", 14: "lr"}.get(n, f"r{n}")
                e.reg[n] = Val.nommee(f"e{n}_{e.generation}", nom)
        return e.reg[n]

    @staticmethod
    def ecrire(e: Etat, n: int, v: Val) -> None:
        if n == 15:
            raise Refus("écriture dans pc")
        e.reg[n] = v

    @staticmethod
    def connu_pile(e: Etat, ea: Val) -> Val | None:
        """Ce qu'on sait d'un mot de la pile : par son adresse telle quelle, ou par la même
        adresse comptée depuis le sp d'avant l'appel — le sp relu après vaut celui-là."""
        v = e.pile.get(ea.c)
        if v is not None or e.sp_avant is None or ea.racine is None:
            return v
        sp = e.reg.get(13)
        if sp is None or ea.racine is not sp.racine:
            return None
        return e.pile.get(binaire('+', e.sp_avant, Val.constante(ea.offset), lambda x, y: x + y).c)

    def nouveau_temp(self, sortie: list, expr: str, desc: str, connu: Val | None = None) -> Val:
        nom = f"t{self.temp}"
        self.temp += 1
        sortie.append(f"const uint32_t {nom} = {expr};")
        v = Val.nommee(nom, connu.desc if connu else desc)
        v.connu = connu
        return v

    def mot(self, adresse: int) -> int:
        off = adresse - self.base
        if off < 0 or off + 4 > len(self.donnees):
            raise Refus("littéral hors du module")
        return struct.unpack_from('<I', self.donnees, off)[0]

    # -- drapeaux
    @staticmethod
    def poser_nz(e: Etat, v: Val, retenue: str | None = None) -> None:
        e.drapeaux.append((frozenset("NZ"), f"SET_NZ(c, {v.c});"))
        e.flag['N'] = Val(f"({v.c} >> 31)", f"{v.desc} négatif")
        e.flag['Z'] = Val(f"({v.c} == 0u)", f"{nu(v.desc)} == 0")
        e.prov = ('nz', v)
        if retenue is not None:
            e.drapeaux.append((frozenset("C"), f"c->cf = {retenue};"))
            e.flag['C'] = Val(f"({retenue})", "retenue du décalage")

    @staticmethod
    def poser_sub(e: Etat, a: Val, b: Val, res: Val) -> None:
        e.drapeaux.append((frozenset("NZCV"), f"flags_sub(c, {a.c}, {b.c}, 1u, {res.c});"))
        e.flag['N'] = Val(f"(({a.c} - {b.c}) >> 31)", "")
        e.flag['Z'] = Val(f"({a.c} == {b.c})", "")
        e.flag['C'] = Val(f"({a.c} >= {b.c})", "")
        e.flag['V'] = Val(f"((({a.c} ^ {b.c}) & ({a.c} ^ ({a.c} - {b.c}))) >> 31)", "")
        e.prov = ('sub', a, b)

    @staticmethod
    def poser_add(e: Etat, a: Val, b: Val, res: Val) -> None:
        e.drapeaux.append((frozenset("NZCV"), f"flags_add(c, {a.c}, {b.c}, 0u, {res.c});"))
        e.flag['N'] = Val(f"(({a.c} + {b.c}) >> 31)", "")
        e.flag['Z'] = Val(f"(({a.c} + {b.c}) == 0u)", "")
        e.flag['C'] = Val(f"((uint32_t)({a.c} + {b.c}) < {a.c})", "")
        e.flag['V'] = Val(f"(((~({a.c} ^ {b.c})) & ({a.c} ^ ({a.c} + {b.c}))) >> 31)", "")
        e.prov = ('add', a, b)

    def drapeau(self, e: Etat, nom: str, sortie: list) -> str:
        if nom in e.flag:
            return e.flag[nom].c
        if e.generation == 0:
            self.entrees.add(-ord(nom))      # un drapeau d'entrée : fN, fZ, fC, fV
            return f"f{nom}"
        champs = {'N': 'nf', 'Z': 'zf', 'C': 'cf', 'V': 'vf'}
        sortie.append(f"const uint32_t f{nom}_{e.generation} = c->{champs[nom]};")
        e.flag[nom] = Val(f"f{nom}_{e.generation}", f"drapeau {nom} après l'appel {e.generation}")
        return e.flag[nom].c

    def condition(self, e: Etat, cond: int, sortie: list) -> Val:
        """La condition d'un branchement, en C et en clair. Constante quand elle l'est."""
        nom = COND_NAMES[cond]
        if e.prov and e.prov[0] == 'sub':
            a, b = e.prov[1], e.prov[2]
            sans_signe = {'EQ': '==', 'NE': '!=', 'CS': '>=', 'CC': '<', 'HI': '>', 'LS': '<='}
            signees = {'GE': '>=', 'LT': '<', 'GT': '>', 'LE': '<='}
            if a.const is not None and b.const is not None and (nom in sans_signe or nom in signees):
                x, y = a.const, b.const
                if nom in signees:
                    x, y = (x - (1 << 32) if x >> 31 else x), (y - (1 << 32) if y >> 31 else y)
                op = (sans_signe | signees)[nom]
                verite = {'==': x == y, '!=': x != y, '>=': x >= y, '<': x < y, '>': x > y, '<=': x <= y}[op]
                return Val.constante(1 if verite else 0)
            if nom in sans_signe:
                return Val(f"({a.c} {sans_signe[nom]} {b.c})", f"{nu(a.desc)} {sans_signe[nom]} {nu(b.desc)}")
            if nom in signees:
                return Val(f"((int32_t){a.c} {signees[nom]} (int32_t){b.c})",
                           f"{nu(a.desc)} {signees[nom]} {nu(b.desc)} (signé)")
        if e.prov and e.prov[0] == 'nz' and nom in ('EQ', 'NE', 'MI', 'PL'):
            v = e.prov[1]
            if v.const is not None:
                return Val.constante({'EQ': v.const == 0, 'NE': v.const != 0,
                                      'MI': v.const >> 31 != 0, 'PL': v.const >> 31 == 0}[nom])
            return {'EQ': Val(f"({v.c} == 0u)", f"{nu(v.desc)} == 0"),
                    'NE': Val(f"({v.c} != 0u)", f"{nu(v.desc)} != 0"),
                    'MI': Val(f"(({v.c} >> 31) != 0u)", f"{nu(v.desc)} < 0"),
                    'PL': Val(f"(({v.c} >> 31) == 0u)", f"{nu(v.desc)} >= 0")}[nom]
        n, z, cf, v = (lambda: self.drapeau(e, 'N', sortie)), (lambda: self.drapeau(e, 'Z', sortie)), \
                      (lambda: self.drapeau(e, 'C', sortie)), (lambda: self.drapeau(e, 'V', sortie))
        expr = {'EQ': lambda: f"({z()} != 0u)", 'NE': lambda: f"({z()} == 0u)",
                'CS': lambda: f"({cf()} != 0u)", 'CC': lambda: f"({cf()} == 0u)",
                'MI': lambda: f"({n()} != 0u)", 'PL': lambda: f"({n()} == 0u)",
                'VS': lambda: f"({v()} != 0u)", 'VC': lambda: f"({v()} == 0u)",
                'HI': lambda: f"({cf()} != 0u && {z()} == 0u)", 'LS': lambda: f"({cf()} == 0u || {z()} != 0u)",
                'GE': lambda: f"({n()} == {v()})", 'LT': lambda: f"({n()} != {v()})",
                'GT': lambda: f"({z()} == 0u && {n()} == {v()})",
                'LE': lambda: f"({z()} != 0u || {n()} != {v()})"}.get(nom)
        if expr is None:
            raise Refus("condition inconnue")
        return Val(expr(), f"condition {nom.lower()}")

    def operande(self, i, e: Etat, sortie: list, retenue_voulue: bool) -> tuple[Val, str | None]:
        """Le second opérande d'une opération de données ou le décalage d'un accès mémoire, avec
        la retenue que le décaleur laisse — celle du traducteur, exactement."""
        if i.imm is not None and i.rm is None:
            retenue = None
            if retenue_voulue and getattr(i, 'imm_rotate', 0):
                retenue = f"{(i.imm >> 31) & 1}u"
            return Val.constante(i.imm), retenue
        sh = i.shift
        if sh is not None and sh.reg is not None:
            fn = {LSL: "sh_lsl_reg", LSR: "sh_lsr_reg", ASR: "sh_asr_reg", ROR: "sh_ror_reg"}.get(sh.kind)
            if fn is None:
                raise Refus("décalage inconnu")
            if 15 in (i.rm, i.rn, sh.reg):
                raise Refus("pc et décalage par registre")
            src, qte = self.lire(e, i.rm, sortie), self.lire(e, sh.reg, sortie)
            retenue = f"s{self.temp}"
            sortie.append(f"uint32_t {retenue} = {self.drapeau(e, 'C', sortie)};")
            signe = {LSL: '<<', LSR: '>>', ASR: '>>s', ROR: 'ror'}[sh.kind]
            return self.nouveau_temp(sortie, f"{fn}({src.c}, {qte.c}, &{retenue})",
                                     f"({src.desc} {signe} {qte.desc})"), retenue
        src = self.lire(e, i.rm, sortie)
        if sh is None or (sh.kind == LSL and sh.amount == 0):
            return src, None
        n = sh.amount
        if sh.kind == RRX:
            cf = self.drapeau(e, 'C', sortie)
            return Val(f"(({cf} << 31) | ({src.c} >> 1))", f"rrx({src.desc})"), f"{src.c} & 1u"
        if sh.kind == LSL:
            return decale(src, '<<', n), f"({src.c} >> {32 - n}) & 1u"
        if sh.kind == LSR:
            if n == 32:
                return Val.constante(0), f"{src.c} >> 31"
            return decale(src, '>>', n), f"({src.c} >> {n - 1}) & 1u"
        if sh.kind == ASR:
            m = 31 if n == 32 else n
            v = Val(f"(uint32_t)((int32_t){src.c} >> {m})", f"({src.desc} >>s {m})",
                    None if src.const is None else
                    ((src.const - (1 << 32) if src.const >> 31 else src.const) >> m) & 0xFFFFFFFF)
            return v, (f"{src.c} >> 31" if n == 32 else f"({src.c} >> {n - 1}) & 1u")
        if sh.kind == ROR:
            v = Val(f"ror32({src.c}, {n})", f"ror({src.desc}, {n})",
                    None if src.const is None else ((src.const >> n) | (src.const << (32 - n))) & 0xFFFFFFFF)
            return v, f"({src.c} >> {n - 1}) & 1u"
        raise Refus("décalage inconnu")

    @staticmethod
    def poser_add_retenue(e: Etat, a: Val, b: Val, cin: str, res: Val, soustraction: bool) -> None:
        """adc / sbc / rsc : les drapeaux d'une addition ou d'une soustraction avec retenue."""
        fn = "flags_sub" if soustraction else "flags_add"
        e.drapeaux.append((frozenset("NZCV"), f"{fn}(c, {a.c}, {b.c}, {cin}, {res.c});"))
        e.flag['N'] = Val(f"({res.c} >> 31)", "")
        e.flag['Z'] = Val(f"({res.c} == 0u)", "")
        if soustraction:
            e.flag['C'] = Val(f"((uint32_t)((((uint64_t){a.c} - (uint64_t){b.c} - (uint64_t)(1u - {cin})) >> 32) & 1u) ^ 1u)", "")
            e.flag['V'] = Val(f"(((({a.c} ^ {b.c}) & ({a.c} ^ {res.c})) >> 31) & 1u)", "")
        else:
            e.flag['C'] = Val(f"((uint32_t)(((uint64_t){a.c} + (uint64_t){b.c} + (uint64_t){cin}) >> 32))", "")
            e.flag['V'] = Val(f"(((({a.c} ^ {res.c}) & ({b.c} ^ {res.c})) >> 31) & 1u)", "")
        e.prov = None

    # -- une instruction ordinaire
    def executer(self, i, e: Etat, sortie: list) -> None:
        op = i.op
        if self.arm or op in ("ADC", "SBC"):         # la retenue : une seule écriture pour les deux modes
            return self.executer_arm(i, e, sortie)

        if op in ("MOV", "MVN") and i.shift is not None and i.shift.reg is not None:
            # lsl r0, r1 : la quantité n'est connue qu'à l'exécution, et la retenue en dépend de
            # cinq façons. On s'en remet à la fonction du runtime, comme pour flags_add.
            fn = {LSL: "sh_lsl_reg", LSR: "sh_lsr_reg", ASR: "sh_asr_reg", ROR: "sh_ror_reg"}.get(i.shift.kind)
            if fn is None or op == "MVN":
                raise Refus("décalage par registre")
            src, qte = self.lire(e, i.rm, sortie), self.lire(e, i.shift.reg, sortie)
            retenue = f"s{self.temp}"
            sortie.append(f"uint32_t {retenue} = {self.drapeau(e, 'C', sortie)};")
            signe = {LSL: '<<', LSR: '>>', ASR: '>>s', ROR: 'ror'}[i.shift.kind]
            v = self.nouveau_temp(sortie, f"{fn}({src.c}, {qte.c}, &{retenue})", f"({src.desc} {signe} {qte.desc})")
            if i.s:
                self.poser_nz(e, v, retenue)
            self.ecrire(e, i.rd, v)
            return

        if op in ("MOV", "MVN"):
            if i.imm is not None and i.rm is None:
                v, retenue = Val.constante(i.imm), None
            else:
                src = self.lire(e, i.rm, sortie)
                n = i.shift.amount if i.shift else 0
                kind = i.shift.kind if i.shift else LSL
                if n == 0:
                    v, retenue = src, None
                elif kind == LSL:
                    v = decale(src, '<<', n)
                    retenue = f"({src.c} >> {32 - n}) & 1u"
                elif kind == LSR:
                    v = Val.constante(0) if n == 32 else decale(src, '>>', n)
                    retenue = f"{src.c} >> 31" if n == 32 else f"({src.c} >> {n - 1}) & 1u"
                elif kind == ASR:
                    m = 31 if n == 32 else n
                    v = Val(f"(uint32_t)((int32_t){src.c} >> {m})", f"({src.desc} >>s {m})",
                            None if src.const is None else
                            ((src.const - (1 << 32) if src.const >> 31 else src.const) >> m) & 0xFFFFFFFF)
                    retenue = f"{src.c} >> 31" if n == 32 else f"({src.c} >> {n - 1}) & 1u"
                else:
                    raise Refus("décalage inconnu")
            if op == "MVN":
                v = Val(f"(~{v.c})", f"~{v.desc}", None if v.const is None else (~v.const) & 0xFFFFFFFF)
            if i.s:
                self.poser_nz(e, v, retenue)
            self.ecrire(e, i.rd, v)
            return

        if op in ("ADD", "SUB", "RSB", "CMP", "CMN"):
            if i.rn == 15:
                if op != "ADD" or i.imm is None:
                    raise Refus("pc comme opérande")
                a = Val.constante(((i.addr + 4) & ~3))
            else:
                a = self.lire(e, i.rn, sortie)
            b = Val.constante(i.imm) if i.imm is not None and i.rm is None else self.lire(e, i.rm, sortie)
            for v in (a, b):
                if v.const is not None and op in ("CMP", "CMN", "SUB", "RSB"):
                    self.remarquables.add(v.const)
            if op in ("ADD", "CMN"):
                res = binaire('+', a, b, lambda x, y: x + y)
                if i.s:
                    self.poser_add(e, a, b, res)
            elif op == "RSB":
                res = binaire('-', b, a, lambda x, y: x - y)
                if i.s:
                    self.poser_sub(e, b, a, res)
            else:
                res = binaire('-', a, b, lambda x, y: x - y)
                if i.s:
                    self.poser_sub(e, a, b, res)
            if op not in ("CMP", "CMN"):
                self.ecrire(e, i.rd, res)
            return

        if op in ("AND", "ORR", "EOR", "BIC", "TST"):
            if i.shift is not None and (i.shift.reg is not None or i.shift.amount):
                raise Refus("opérande décalé")
            a, b = self.lire(e, i.rn, sortie), self.lire(e, i.rm, sortie)
            signe = {"AND": '&', "TST": '&', "ORR": '|', "EOR": '^', "BIC": '& ~'}[op]
            calc = {"AND": lambda x, y: x & y, "TST": lambda x, y: x & y, "ORR": lambda x, y: x | y,
                    "EOR": lambda x, y: x ^ y, "BIC": lambda x, y: x & ~y}[op]
            res = binaire(signe, a, b, calc)
            self.poser_nz(e, res)
            if op != "TST":
                self.ecrire(e, i.rd, res)
            return

        if op == "MUL":
            a, b = self.lire(e, i.rm, sortie), self.lire(e, i.rs, sortie)
            res = binaire('*', a, b, lambda x, y: x * y)
            self.poser_nz(e, res)
            self.ecrire(e, i.rd, res)
            return

        if op in ("LDR", "STR"):
            if i.writeback or not i.pre:
                raise Refus("adressage avec écriture arrière")
            if i.rn == 15:
                if not i.load or i.width != "W":
                    raise Refus("accès relatif à pc")
                adresse = ((i.addr + 4) & ~3) + i.imm
                self.ecrire(e, i.rd, Val.constante(self.mot(adresse)))     # une constante du pool
                return
            base = self.lire(e, i.rn, sortie)
            if i.reg_offset:
                dep = self.lire(e, i.rm, sortie)
                ea = binaire('+' if i.up else '-', base, dep, (lambda x, y: x + y) if i.up else (lambda x, y: x - y))
            else:
                ea = binaire('+' if i.up else '-', base, Val.constante(i.imm or 0),
                             (lambda x, y: x + y) if i.up else (lambda x, y: x - y))
            w = i.width
            if i.load:
                lecture = {"W": "ldr_word(c, {ea})", "B": "(uint32_t)mem_read8(c, {ea})", "H": "ldr_h(c, {ea})",
                           "SB": "(uint32_t)(int32_t)(int8_t)mem_read8(c, {ea})", "SH": "ldr_sh(c, {ea})"}.get(w)
                if lecture is None:
                    raise Refus("largeur de chargement")
                nom = {"W": "", "B": "octet ", "H": "demi-mot ", "SB": "octet signé ", "SH": "demi-mot signé "}[w]
                if ea.const is not None:
                    self.globales.add(ea.const)
                lu = self.nouveau_temp(sortie, lecture.format(ea=ea.c), f"{nom}[{nu(ea.desc)}]",
                                       self.connu_pile(e, ea) if w == "W" else None)
                self.lus[lu.c] = (ea.c, {"W": "W", "B": "B", "SB": "B", "H": "H", "SH": "H"}[w])
                self.ecrire(e, i.rd, lu)
            else:
                src = self.lire(e, i.rd, sortie)
                ecriture = {"W": "mem_write32(c, {ea}, {v});", "B": "mem_write8(c, {ea}, (uint8_t){v});",
                            "H": "mem_write16(c, {ea}, (uint16_t){v});"}.get(w)
                if ecriture is None:
                    raise Refus("largeur de rangement")
                ligne = ecriture.format(ea=ea.c, v=src.c)
                if i.rn != 13 and not re.search(r"\be13", ea.c):
                    lus = [t for t in dict.fromkeys(re.findall(r"\bt\d+\b", src.c)) if t in self.lus]
                    meme = [t for t in lus if self.lus[t] == (ea.c, w) and somme_avec(src.c, t)]
                    if meme:
                        ligne += f" /* acc {meme[0]} 0x{i.addr:08X} */"
                    else:
                        lus = [t for t in lus if somme_avec(src.c, t)]
                        # la même adresse par deux chemins — VEC_Add(&pos, &v, &pos) : on ne le saura qu'à
                        # l'exécution ; deux lectures candidates au plus, de même largeur, hors de la pile
                        cands = [t for t in lus if self.lus[t][1] == w and not re.search(r"\be13", self.lus[t][0])][:2]
                        if cands:
                            cands = (cands * 2)[:2]
                            ligne += " /* accd " + "|".join(f"{t}@{self.lus[t][0]}" for t in cands) + f" 0x{i.addr:08X} */"
                sortie.append(ligne)
                if i.rn != 13:
                    e.pile.clear()               # elle a pu écrire n'importe où
                elif w == "W":
                    e.pile[ea.c] = src
                nom = {"W": "", "B": "octet ", "H": "demi-mot "}[w]
                e.effets.append(f"{nom}[{nu(ea.desc)}] = {nu(src.desc)}")
            return

        if op == "STM":                                      # push, ou stmia rn!
            regs = [n for n in range(16) if (i.reglist >> n) & 1]
            if 15 in regs or not i.writeback:
                raise Refus("transfert multiple inhabituel")
            base = self.lire(e, i.rn, sortie)
            if i.pre and not i.up:                           # vers le bas : push
                bas = binaire('-', base, Val.constante(4 * len(regs)), lambda x, y: x - y)
                final = bas
            elif not i.pre and i.up:                         # vers le haut : stmia
                bas = base
                final = binaire('+', base, Val.constante(4 * len(regs)), lambda x, y: x + y)
            else:
                raise Refus("transfert multiple inhabituel")
            if i.rn != 13:
                e.pile.clear()
            # la base dans la liste : première, c'est sa valeur d'origine qui est rangée ; sinon
            # celle d'après l'écriture arrière — la règle du traducteur, telle quelle
            base_finale = i.rn in regs and regs[0] != i.rn
            for k, n in enumerate(regs):
                ou = binaire('+', bas, Val.constante(4 * k), lambda x, y: x + y)
                v = final if (n == i.rn and base_finale) else self.lire(e, n, sortie)
                sortie.append(f"mem_write32(c, {ou.c}, {v.c});")
                if i.rn == 13:
                    e.pile[ou.c] = v
                else:
                    e.effets.append(f"[{nu(ou.desc)}] = {nu(v.desc)}")
            self.ecrire(e, i.rn, final)
            return

        raise Refus(f"instruction {op}")

    # -- tout ce qu'on a différé est écrit dans le processeur : avant un appel, à la fin
    @staticmethod
    def materialiser(e: Etat, sortie: list) -> None:
        for n, v in sorted(e.reg.items()):
            if v.c != (f"e{n}" if e.generation == 0 else f"e{n}_{e.generation}"):
                sortie.append(f"c->r[{n}] = {v.c};")
        # seuls comptent les derniers à poser chaque drapeau, dans l'ordre où ils l'ont fait
        restants, gardes = set("NZCV"), []
        for lesquels, code in reversed(e.drapeaux):
            if lesquels & restants:
                gardes.append(code)
                restants -= lesquels
        sortie += reversed(gardes)

    def appeler(self, e: Etat, sortie: list, cible: Val | None, cible_thumb: bool, retour: int,
                indirect: bool = False) -> None:
        """Un appel : l'état est écrit dans le processeur, l'appelée tourne, et tout ce qu'on
        savait des registres et des drapeaux est à relire."""
        arguments = ", ".join(f"arg{n} = {nu(e.reg[n].desc)}" for n in range(4)
                              if n in e.reg and e.reg[n].c != (f"e{n}" if e.generation == 0 else f"e{n}_{e.generation}"))
        self.materialiser(e, sortie)
        sortie.append(f"c->r[14] = 0x{retour:08X}u;")
        if cible is None:
            raise Refus("appel sans cible")
        if cible.const is not None and (cible.const & ~1) == self.fn['entrée']:
            # Elle s'appelle elle-même. À froid, la traduction se rappellerait pour de bon — le
            # banc ôte son crochet le temps de l'appel — quand la réécriture retomberait sur le
            # banc : les deux ne verraient pas le même monde. Elle se juge en jeu.
            self.recursive = True
        if indirect:
            ou = nu(cible.desc)
            sortie.append(f"appel_indirect(c, {cible.c}, 0x{retour:08X}u);")
        elif cible.const is not None:
            ou = self.noms.get(cible.const & ~1, f"0x{cible.const & ~1:08X}")
            adresse = (cible.const & ~1) | (1 if cible_thumb else 0)
            liee = self.resolution.directe(self.module, cible.const & ~1, 1 if cible_thumb else 0) if self.resolution else None
            if cible.const & 1:
                raise Refus("cible d'appel impaire")
            if liee:
                if liee[1] != cible.const & ~1:
                    Feuille.visees.add((liee[0], liee[1], cible.const & ~1))
                sortie.append(f'APPEL_DIRECT(c, "{liee[0]}", 0x{liee[1]:08X}u, 0x{cible.const & ~1:08X}u, 0x{retour:08X}u);')
            else:
                sortie.append(f"appel_dispatch(c, 0x{adresse:08X}u, 0x{retour:08X}u);")
        else:
            ou = nu(cible.desc)
            sortie.append(f"appel_indirect(c, {cible.c}, 0x{retour:08X}u);")
        # Un changement de thread pendant l'appel peut reprendre cette fonction ailleurs qu'ici :
        # la traduction, qui a toutes ses étiquettes, sait où.
        sortie.append(f"if (c->redirect) {{ reprendre(c, 0x{self.fn['entrée']:08X}u); return; }}")
        e.appels.append(f"{ou}({arguments})")
        pile, sp = e.pile, e.reg.get(13)           # la pile suivie reste : l'appelée travaille dessous
        self.a_neuf(e)
        e.pile, e.sp_avant = pile, sp

    def executer_arm(self, i, e: Etat, sortie: list) -> None:
        op = i.op
        logiques = {"AND": '&', "EOR": '^', "ORR": '|', "BIC": '& ~', "TST": '&', "TEQ": '^'}
        if op in logiques or op in ("MOV", "MVN"):
            veut = bool(i.s)
            op2, retenue = self.operande(i, e, sortie, veut)
            if op in ("MOV", "MVN"):
                res = op2 if op == "MOV" else Val(f"(~{op2.c})", f"~{op2.desc}",
                                                  None if op2.const is None else (~op2.const) & 0xFFFFFFFF)
            else:
                rn = self.lire(e, i.rn, sortie)
                calc = {'&': lambda x, y: x & y, '^': lambda x, y: x ^ y, '|': lambda x, y: x | y,
                        '& ~': lambda x, y: x & ~y}[logiques[op]]
                res = binaire(logiques[op], rn, op2, calc)
            if i.s or op in ("TST", "TEQ"):
                self.poser_nz(e, res, retenue)           # sans décalage, la retenue reste
            if op in ("TST", "TEQ"):
                return
            if i.rd == 15:
                raise Refus("écriture dans pc")          # traitée dans le parcours
            self.ecrire(e, i.rd, res)
            return

        if op in ("ADD", "SUB", "RSB", "CMP", "CMN", "ADC", "SBC", "RSC"):
            op2, _r = self.operande(i, e, sortie, False)
            rn = self.lire(e, i.rn, sortie)
            for v in (rn, op2):
                if v.const is not None and op in ("CMP", "CMN", "SUB", "RSB"):
                    self.remarquables.add(v.const)
            if op in ("ADD", "CMN"):
                res = binaire('+', rn, op2, lambda x, y: x + y)
                if i.s:
                    self.poser_add(e, rn, op2, res)
            elif op in ("SUB", "CMP"):
                res = binaire('-', rn, op2, lambda x, y: x - y)
                if i.s:
                    self.poser_sub(e, rn, op2, res)
            elif op == "RSB":
                res = binaire('-', op2, rn, lambda x, y: x - y)
                if i.s:
                    self.poser_sub(e, op2, rn, res)
            else:
                cin = self.drapeau(e, 'C', sortie)
                if op == "ADC":
                    res = Val(f"({rn.c} + {op2.c} + {cin})", f"({rn.desc} + {op2.desc} + retenue)")
                    if i.s:
                        self.poser_add_retenue(e, rn, op2, cin, res, False)
                elif op == "SBC":
                    res = Val(f"({rn.c} - {op2.c} - (1u - {cin}))", f"({rn.desc} - {op2.desc} - emprunt)")
                    if i.s:
                        self.poser_add_retenue(e, rn, op2, cin, res, True)
                else:
                    res = Val(f"({op2.c} - {rn.c} - (1u - {cin}))", f"({op2.desc} - {rn.desc} - emprunt)")
                    if i.s:
                        self.poser_add_retenue(e, op2, rn, cin, res, True)
            if op in ("CMP", "CMN"):
                return
            if i.rd == 15:
                raise Refus("écriture dans pc")
            self.ecrire(e, i.rd, res)
            return

        if op in ("MUL", "MLA"):
            a, b = self.lire(e, i.rm, sortie), self.lire(e, i.rs, sortie)
            res = binaire('*', a, b, lambda x, y: x * y)
            if op == "MLA":
                res = binaire('+', res, self.lire(e, i.rn, sortie), lambda x, y: x + y)
            if i.s:
                self.poser_nz(e, res)
            self.ecrire(e, i.rd, res)
            return

        if op in ("UMULL", "SMULL", "UMLAL", "SMLAL"):
            a, b = self.lire(e, i.rm, sortie), self.lire(e, i.rs, sortie)
            cast = "(int64_t)(int32_t)" if op[0] == 'S' else "(uint64_t)"
            w = f"w{self.temp}"
            expr = f"(uint64_t)({cast}{a.c} * {cast}{b.c})"
            if op.endswith("MLAL"):
                hi, lo = self.lire(e, i.rd, sortie), self.lire(e, i.rn, sortie)
                expr += f" + (((uint64_t){hi.c} << 32) | {lo.c})"
            sortie.append(f"const uint64_t {w} = {expr};")
            self.temp += 1
            bas = self.nouveau_temp(sortie, f"(uint32_t){w}", f"bas({a.desc} * {b.desc})")
            haut = self.nouveau_temp(sortie, f"(uint32_t)({w} >> 32)", f"haut({a.desc} * {b.desc})")
            self.ecrire(e, i.rn, bas)
            self.ecrire(e, i.rd, haut)
            if i.s:
                e.drapeaux.append((frozenset("NZ"), f"c->nf = (uint32_t)({w} >> 63); c->zf = ({w} == 0);"))
                e.flag['N'] = Val(f"((uint32_t)({w} >> 63))", "")
                e.flag['Z'] = Val(f"({w} == 0)", "")
                e.prov = None
            return

        if op in ("SMULXY", "SMLAXY", "SMULWY", "SMLAWY", "SMLALXY"):
            a, b = self.lire(e, i.rm, sortie), self.lire(e, i.rs, sortie)
            def moitie(v: Val, haut: bool) -> str:
                return f"(int32_t)(int16_t)({v.c} >> 16)" if haut else f"(int32_t)(int16_t){v.c}"
            if op == "SMULXY":
                res = Val(f"(uint32_t)({moitie(a, i.x)} * {moitie(b, i.y)})", f"({a.desc} *16 {b.desc})")
            elif op == "SMLAXY":
                acc = self.lire(e, i.rn, sortie)
                res = self.nouveau_temp(sortie, f"add_setq(c, (uint32_t)({moitie(a, i.x)} * {moitie(b, i.y)}), {acc.c})",
                                        f"({a.desc} *16 {b.desc} + {acc.desc})")
            elif op == "SMULWY":
                res = Val(f"(uint32_t)(((int64_t)(int32_t){a.c} * {moitie(b, i.y)}) >> 16)", f"({a.desc} *w {b.desc})")
            elif op == "SMLAWY":
                acc = self.lire(e, i.rn, sortie)
                res = self.nouveau_temp(sortie, f"add_setq(c, (uint32_t)(((int64_t)(int32_t){a.c} * {moitie(b, i.y)}) >> 16), {acc.c})",
                                        f"({a.desc} *w {b.desc} + {acc.desc})")
            else:
                raise Refus("instruction SMLALXY")
            self.ecrire(e, i.rd, res)
            return

        if op in ("QADD", "QSUB", "QDADD", "QDSUB"):
            a, b = self.lire(e, i.rm, sortie), self.lire(e, i.rn, sortie)
            fn = {"QADD": "sat_add(c, {a}, {b})", "QSUB": "sat_sub(c, {a}, {b})",
                  "QDADD": "sat_add(c, {a}, sat_double(c, {b}))", "QDSUB": "sat_sub(c, {a}, sat_double(c, {b}))"}[op]
            self.ecrire(e, i.rd, self.nouveau_temp(sortie, fn.format(a=a.c, b=b.c), f"{op.lower()}({a.desc}, {b.desc})"))
            return

        if op in ("SWP", "SWPB"):
            base, src = self.lire(e, i.rn, sortie), self.lire(e, i.rm, sortie)
            if op == "SWP":
                v = self.nouveau_temp(sortie, f"ldr_word(c, {base.c})", f"[{nu(base.desc)}]")
                sortie.append(f"mem_write32(c, {base.c}, {src.c});")
            else:
                v = self.nouveau_temp(sortie, f"(uint32_t)mem_read8(c, {base.c})", f"octet [{nu(base.desc)}]")
                sortie.append(f"mem_write8(c, {base.c}, (uint8_t){src.c});")
            e.pile.clear()
            e.effets.append(f"échange [{nu(base.desc)}] et {nu(src.desc)}")
            self.ecrire(e, i.rd, v)
            return

        if op in ("PLD", "NOP"):
            return

        if op == "CLZ":
            a = self.lire(e, i.rm, sortie)
            self.ecrire(e, i.rd, Val(f"clz32({a.c})", f"clz({a.desc})"))
            return

        if op in ("LDR", "STR"):
            if i.rn == 15 and i.writeback:
                raise Refus("écriture arrière dans pc")
            base = self.lire(e, i.rn, sortie)
            if i.reg_offset:
                dep, _r = self.operande(i, e, sortie, False)
            else:
                dep = Val.constante(i.imm or 0)
            calc = (lambda x, y: x + y) if i.up else (lambda x, y: x - y)
            decalee = binaire('+' if i.up else '-', base, dep, calc)
            if i.pre:
                ea = decalee
                if i.writeback:
                    self.ecrire(e, i.rn, ea)
            else:
                ea = base
                self.ecrire(e, i.rn, decalee)
            if i.rn != 13:
                e.pile.clear()
            w = i.width
            if i.load:
                if w == "D":
                    self.ecrire(e, i.rd, self.nouveau_temp(sortie, f"mem_read32(c, {ea.c})", f"[{nu(ea.desc)}]"))
                    self.ecrire(e, i.rd + 1, self.nouveau_temp(sortie, f"mem_read32(c, {ea.c} + 4u)", f"[{nu(ea.desc)} + 4]"))
                    return
                lecture = {"W": "ldr_word(c, {ea})", "B": "(uint32_t)mem_read8(c, {ea})", "H": "ldr_h(c, {ea})",
                           "SB": "(uint32_t)(int32_t)(int8_t)mem_read8(c, {ea})", "SH": "ldr_sh(c, {ea})"}[w]
                nom = {"W": "", "B": "octet ", "H": "demi-mot ", "SB": "octet signé ", "SH": "demi-mot signé "}[w]
                if i.rn == 15 and w == "W" and not i.reg_offset:
                    self.ecrire(e, i.rd, Val.constante(self.mot(ea.const)))    # une constante du pool
                    return
                if ea.const is not None:
                    self.globales.add(ea.const)
                v = self.nouveau_temp(sortie, lecture.format(ea=ea.c), f"{nom}[{nu(ea.desc)}]",
                                      self.connu_pile(e, ea) if w == "W" else None)
                if i.rd == 15:
                    raise Refus("écriture dans pc")
                self.ecrire(e, i.rd, v)
                return
            if i.rd == 15:
                raise Refus("rangement de pc")
            src = self.lire(e, i.rd, sortie)
            if w == "D":
                src2 = self.lire(e, i.rd + 1, sortie)
                sortie.append(f"mem_write32(c, {ea.c}, {src.c});")
                sortie.append(f"mem_write32(c, {ea.c} + 4u, {src2.c});")
                e.effets.append(f"[{nu(ea.desc)}] = {nu(src.desc)}, {nu(src2.desc)}")
                return
            ecriture = {"W": "mem_write32(c, {ea}, {v});", "B": "mem_write8(c, {ea}, (uint8_t){v});",
                        "H": "mem_write16(c, {ea}, (uint16_t){v});"}[w]
            sortie.append(ecriture.format(ea=ea.c, v=src.c))
            if i.rn == 13 and w == "W":
                e.pile[ea.c] = src
            nom = {"W": "", "B": "octet ", "H": "demi-mot "}[w]
            e.effets.append(f"{nom}[{nu(ea.desc)}] = {nu(src.desc)}")
            return

        if op == "LDM":                                      # sans pc : avec, c'est une fin de chemin
            regs = [n for n in range(16) if (i.reglist >> n) & 1]
            if i.user_bank or 15 in regs:
                raise Refus("transfert multiple inhabituel")
            base = self.lire(e, i.rn, sortie)
            n4 = 4 * len(regs)
            if i.up:
                bas = binaire('+', base, Val.constante(4), lambda x, y: x + y) if i.pre else base
                final = binaire('+', base, Val.constante(n4), lambda x, y: x + y)
            else:
                bas = binaire('-', base, Val.constante(n4 - (0 if i.pre else 4)), lambda x, y: x - y)
                final = binaire('-', base, Val.constante(n4), lambda x, y: x - y)
            if i.writeback and i.rn not in regs:
                self.ecrire(e, i.rn, final)
            for k, n in enumerate(regs):
                ou = binaire('+', bas, Val.constante(4 * k), lambda x, y: x + y)
                self.ecrire(e, n, self.nouveau_temp(sortie, f"mem_read32(c, {ou.c})", f"[{nu(ou.desc)}]",
                                                    self.connu_pile(e, ou)))
            return

        if op == "STM":
            regs = [n for n in range(16) if (i.reglist >> n) & 1]
            if i.user_bank or 15 in regs:
                raise Refus("transfert multiple inhabituel")
            base = self.lire(e, i.rn, sortie)
            n4 = 4 * len(regs)
            if i.up:
                bas = binaire('+', base, Val.constante(4), lambda x, y: x + y) if i.pre else base
                final = binaire('+', base, Val.constante(n4), lambda x, y: x + y)
            else:
                bas = binaire('-', base, Val.constante(n4 - (0 if i.pre else 4)), lambda x, y: x - y)
                final = binaire('-', base, Val.constante(n4), lambda x, y: x - y)
            if i.rn != 13:
                e.pile.clear()
            base_finale = i.writeback and i.rn in regs and regs[0] != i.rn    # comme en Thumb
            for k, n in enumerate(regs):
                ou = binaire('+', bas, Val.constante(4 * k), lambda x, y: x + y)
                v = final if (n == i.rn and base_finale) else self.lire(e, n, sortie)
                sortie.append(f"mem_write32(c, {ou.c}, {v.c});")
                if i.rn == 13:
                    e.pile[ou.c] = v
                else:
                    e.effets.append(f"[{nu(ou.desc)}] = {nu(v.desc)}")
            if i.writeback:
                self.ecrire(e, i.rn, final)
            return

        raise Refus(f"instruction {op}")

    def suivre_ldm(self, i, adresse: int, e: Etat, sortie: list, seulement: bool = False):
        """ldm dans tous ses modes. Avec pc dans la liste, c'est la fin du chemin : rend sa
        description ; sinon rend None (ou, si `seulement` est faux, poursuit après)."""
        if i.user_bank:
            raise Refus("transfert multiple inhabituel")
        regs = [n for n in range(16) if (i.reglist >> n) & 1]
        base = self.lire(e, i.rn, sortie)
        n4 = 4 * len(regs)
        if i.up:
            bas = binaire('+', base, Val.constante(4), lambda x, y: x + y) if i.pre else base
            final = binaire('+', base, Val.constante(n4), lambda x, y: x + y)
        else:
            bas = binaire('-', base, Val.constante(n4 - (0 if i.pre else 4)), lambda x, y: x - y)
            final = binaire('-', base, Val.constante(n4), lambda x, y: x - y)
        if i.writeback and i.rn not in regs:
            self.ecrire(e, i.rn, final)
        saut = None
        for k, n in enumerate(regs):
            ou = binaire('+', bas, Val.constante(4 * k), lambda x, y: x + y)
            v = self.nouveau_temp(sortie, f"mem_read32(c, {ou.c})", f"[{nu(ou.desc)}]", self.connu_pile(e, ou))
            if n == 15:
                saut = v
            else:
                self.ecrire(e, n, v)
        if saut is not None:
            return self.terminer(e, sortie, saut)
        if seulement:
            return None
        return self.suivre(adresse + i.size, e, sortie)

    def executer_arm_vers_pc(self, i, e: Etat, sortie: list) -> None:
        """La même instruction, mais sa destination est pc : on la calcule dans un registre 15
        symbolique, que le parcours transforme en saut."""
        if i.op == "LDR":
            base = self.lire(e, i.rn, sortie)
            dep = self.operande(i, e, sortie, False)[0] if i.reg_offset else Val.constante(i.imm or 0)
            calc = (lambda x, y: x + y) if i.up else (lambda x, y: x - y)
            decalee = binaire('+' if i.up else '-', base, dep, calc)
            ea = decalee if i.pre else base
            if i.writeback or not i.pre:
                self.ecrire(e, i.rn, decalee if i.pre else decalee)
            e.reg[15] = self.nouveau_temp(sortie, f"ldr_word(c, {ea.c})", f"[{nu(ea.desc)}]", self.connu_pile(e, ea))
            return
        op2, _r = self.operande(i, e, sortie, False)
        if i.op == "MOV":
            e.reg[15] = op2
        else:
            rn = self.lire(e, i.rn, sortie)
            e.reg[15] = binaire('+' if i.op == "ADD" else '-', rn, op2,
                                (lambda x, y: x + y) if i.op == "ADD" else (lambda x, y: x - y))

    def suivre_instruction(self, i, adresse: int, e: Etat, sortie: list) -> str:
        """Exécute cette seule instruction — terminale ou appel — puis poursuit après elle."""
        if i.op == "BX":
            if i.rm == 14:
                return self.terminer(e, sortie, None)
            if adresse in self.tables_de_sauts:
                return self.aiguiller(e, sortie, adresse, self.lire(e, i.rm, sortie), True)
            return self.terminer(e, sortie, self.lire(e, i.rm, sortie))
        if i.op in ("BL", "BLX"):
            retour = adresse + i.size
            if i.target is not None:
                self.appeler(e, sortie, Val.constante(i.target), i.op == "BLX", retour)
            else:
                self.appeler(e, sortie, self.lire(e, i.rm, sortie), True, retour, indirect=True)
            return self.suivre(adresse + i.size, e, sortie)
        if i.op == "LDM":
            return self.suivre_ldm(i, adresse, e, sortie)
        if i.op in ("MRS", "MSR", "MCR", "MRC", "SWI", "BKPT"):
            self.systeme(i, e, sortie)
            return self.suivre(adresse + i.size, e, sortie)
        if i.op in ("MOV", "ADD", "SUB", "LDR"):
            self.executer_arm_vers_pc(i, e, sortie)
            if adresse in self.tables_de_sauts:          # « addls pc, pc, r0, lsl #2 » : le switch de l'ARM
                return self.aiguiller(e, sortie, adresse, e.reg.pop(15), False)
            return self.terminer(e, sortie, e.reg.pop(15))
        raise Refus(f"instruction {i.op}")

    def systeme(self, i, e: Etat, sortie: list) -> None:
        """mrs, msr, mcr, mrc, swi, bkpt : elles agissent sur le processeur lui-même — son mode,
        ses interruptions, son coprocesseur, son BIOS. L'état symbolique est écrit, la ligne
        même du traducteur fait son office, et tout est à relire : le mode a pu changer, et
        avec lui la pile. Ces fonctions-là ne s'essaient pas à froid."""
        self.recursive = True                  # « à juger en jeu » : le banc à froid les écarte
        self.materialiser(e, sortie)
        op = i.op
        if op == "MRS":
            sortie.append(f"c->r[{i.rd}] = {'spsr_read' if i.spsr else 'cpsr_read'}(c);")
            e.effets.append(f"r{i.rd} = {'spsr' if i.spsr else 'cpsr'}")
        elif op == "MSR":
            src = f"0x{i.imm:08X}u" if i.imm is not None else f"c->r[{i.rm}]"
            sortie.append(f"{'spsr_write' if i.spsr else 'cpsr_write'}(c, {src}, {i.fields}u);")
            e.effets.append(f"{'spsr' if i.spsr else 'cpsr'} = {src if i.imm is not None else 'r%d' % i.rm}")
        elif op == "MCR":
            if i.cp != 15 or i.rd == 15:
                raise Refus("coprocesseur inconnu")
            sortie.append(f"rt_cp15_write(c, {i.opc1}u, {i.crn}u, {i.crm}u, {i.opc2}u, c->r[{i.rd}]);")
            e.effets.append(f"cp15 c{i.crn},c{i.crm},{i.opc2} = r{i.rd}")
        elif op == "MRC":
            if i.cp != 15:
                raise Refus("coprocesseur inconnu")
            lecture = f"rt_cp15_read(c, {i.opc1}u, {i.crn}u, {i.crm}u, {i.opc2}u)"
            if i.rd == 15:
                nom = f"t{self.temp}"
                self.temp += 1
                sortie.append(f"const uint32_t {nom} = {lecture};")
                sortie.append(f"c->nf = {nom} >> 31; c->zf = ({nom} >> 30) & 1u; c->cf = ({nom} >> 29) & 1u; c->vf = ({nom} >> 28) & 1u;")
            else:
                sortie.append(f"c->r[{i.rd}] = {lecture};")
            e.effets.append(f"lit cp15 c{i.crn},c{i.crm},{i.opc2}")
        elif op == "SWI":
            sortie.append(f"rt_swi(c, 0x{i.swi:X}u);")
            e.effets.append(f"appel du BIOS 0x{i.swi:X}")
        elif op == "BKPT":
            sortie.append(f"rt_bkpt(c, 0x{i.imm or 0:X}u, 0x{i.addr:08X}u);")
            e.effets.append("point d'arrêt")
        pile = e.pile
        self.a_neuf(e, 'boucle')
        e.pile = pile

    def registres_touches(self, i) -> list[int]:
        """Les registres qu'une instruction lit ou écrit — pour les relire avant de la prédiquer."""
        regs = [n for n in (i.rd, i.rn, i.rm, i.rs) if n is not None and n != 15]
        if i.op in ("LDM", "STM"):
            regs += [n for n in range(15) if (i.reglist >> n) & 1]
        if i.width == "D" and i.op in ("LDR", "STR") and i.rd is not None:
            regs.append(i.rd + 1)
        if i.shift is not None and i.shift.reg is not None:
            regs.append(i.shift.reg)
        return sorted(set(regs))

    def executer_predique(self, i, e: Etat, sortie: list, cnd: Val) -> None:
        """Une instruction ARM sous condition, sans branche : ce qu'elle change devient un
        choix — « si la condition, la nouvelle valeur, sinon l'ancienne » — pour chaque
        registre, chaque drapeau ; ses accès mémoire se font sous un if."""
        for n in self.registres_touches(i):
            self.lire(e, n, sortie)
        if i.s or i.op in ("CMP", "CMN", "TST", "TEQ"):
            for d in "NZCV":
                self.drapeau(e, d, sortie)
        e2 = e.copie()
        bloc: list = []
        self.executer(i, e2, bloc)
        # les temporaires nés dans le bloc doivent survivre au if : déclarés avant, affectés dedans
        declarations, corps = [], []
        for ligne in bloc:
            m = re.match(r"const uint32_t (t\d+) = (.*);$", ligne) or re.match(r"const uint64_t (w\d+) = (.*);$", ligne)
            if m:
                declarations.append(f"uint{'64' if ligne.startswith('const uint64') else '32'}_t {m.group(1)} = 0u;")
                corps.append(f"{m.group(1)} = {m.group(2)};")
            elif ligne.startswith("uint32_t s"):
                # La retenue d'un décalage, hissée avant le if, peut lire un drapeau d'entrée que
                # l'instruction vient de relire dans le bloc : sa déclaration monte avec elle, et
                # l'état d'avant la retient pour ne pas la refaire.
                for decl in [x for x in corps if re.match(r"const uint32_t f[NZCV]_\d+ = c->\w+;$", x)]:
                    nom, lettre = decl.split()[2], decl.split()[2][1]
                    if nom in ligne:
                        corps.remove(decl)
                        declarations.append(decl)
                        e.flag[lettre] = e2.flag[lettre]
                declarations.append(ligne)
            else:
                corps.append(ligne)
        sortie += declarations
        if corps:
            sortie.append(('if', cnd.c, corps, []))
        for n, v in e2.reg.items():
            ancien = self.lire(e, n, sortie)
            if ancien is not v and ancien.c != v.c:
                e.reg[n] = Val(f"({cnd.c} ? {v.c} : {ancien.c})", f"(si {cnd.desc} : {nu(v.desc)}, sinon {nu(ancien.desc)})")
        drapeaux_changes = [d for d in "NZCV" if e2.flag.get(d) is not e.flag.get(d)]
        if drapeaux_changes:
            for d in drapeaux_changes:
                ancien = e.flag[d].c if d in e.flag else self.drapeau(e, d, sortie)   # le drapeau d'entrée
                e.flag[d] = Val(f"({cnd.c} ? {e2.flag[d].c} : {ancien})", "")
            champs = {'N': 'nf', 'Z': 'zf', 'C': 'cf', 'V': 'vf'}
            e.drapeaux.append((frozenset(drapeaux_changes),
                               " ".join(f"c->{champs[d]} = {e.flag[d].c};" for d in drapeaux_changes)))
            e.prov = None
        e.effets += [f"si {cnd.desc} : {x}" for x in e2.effets[len(e.effets):]]
        if e2.pile != e.pile:
            e.pile.clear()

    # -- la fin d'un chemin
    def terminer(self, e: Etat, sortie: list, saut: Val | None) -> str:
        if self.chemins >= self.chemins_max:
            raise Refus("trop de chemins")
        numero = self.chemins
        self.chemins += 1
        self.materialiser(e, sortie)
        if FAUTE:
            sortie.append("c->r[12] ^= 1u;      /* faute délibérée : le banc doit la voir partout */")
        sortie.append(f"CHEMIN({self.indice}, {numero});")
        if saut is not None:
            self.saute = True
            sortie += [f"c->r[15] = {saut.c};", f"rt_jump(c, {saut.c});"]
        else:
            lr = e.reg.get(14)
            sortie.append(f"c->r[15] = {lr.c if lr else 'c->r[14]'};")
        return self.decrire_chemin(e, saut)

    @staticmethod
    def registre_intact(e: Etat, n: int) -> bool:
        return n in e.reg and e.reg[n].c == (f"e{n}" if e.generation == 0 else f"e{n}_{e.generation}")

    @staticmethod
    def decrire_chemin(e: Etat, saut: Val | None) -> str:
        r0 = e.reg.get(0)
        morceaux = list(e.effets)
        if e.appels:
            morceaux.insert(0, "appelle " + ", puis ".join(e.appels))
        e0 = "e0" if e.generation == 0 else f"e0_{e.generation}"
        if saut is not None:
            cible = saut.connu if saut.connu is not None else saut
            if cible.desc == "lr":
                saut = None                  # pop {…, pc} après push {…, lr} : un retour ordinaire
        if saut is not None:
            cible = saut.connu if saut.connu is not None else saut
            ou = f"0x{cible.const & ~1:08X}" if cible.const is not None else nu(cible.desc)
            decalage = re.fullmatch(r"\((?:e0 \+ (0x[0-9A-F]+)u|(0x[0-9A-F]+)u \+ e0)\)", r0.c) if r0 else None
            if decalage:
                # le relais d'héritage multiple : `this` ramené à la sous-classe, puis la vraie méthode
                k = int(decalage.group(1) or decalage.group(2), 16)
                k = k - (1 << 32) if k >> 31 else k
                morceaux.append(f"relais d'héritage : this {'+' if k >= 0 else '-'} 0x{abs(k):X}, puis {ou}")
            else:
                passes = [f"arg{n} = {nu(e.reg[n].desc)}" for n in range(4)
                          if n in e.reg and not Feuille.registre_intact(e, n) and e.reg[n] is not cible
                          and e.reg[n] is not saut]
                morceaux.append(f"appel terminal de {ou}" + (f" avec {', '.join(passes)}" if passes else ""))
        elif r0 is not None and r0.c != e0:
            morceaux.append(f"rend {nu(r0.desc)}")
        elif e.generation and r0 is None:
            morceaux.append(f"rend le résultat de l'appel {e.generation}")
        return " ; ".join(morceaux) if morceaux else "ne fait rien"

    # -- le parcours du code, chemin par chemin
    def suivre(self, adresse: int, e: Etat, sortie: list) -> str:
        debut, fin = self.fn['début'], self.fn['fin']
        while True:
            if adresse in self.tetes:
                if self.tetes[adresse]:                      # déjà émise : on y saute, sans arête
                    return self.reprendre_boucle(e, sortie, adresse, self.decrire_chemin(e, None), arete=False)
                # première arrivée : l'état est écrit, l'étiquette posée, et tout se relit
                self.materialiser(e, sortie)
                sortie.append(('label', f"L_{adresse:08X}"))
                self.tetes[adresse] = True
                self.a_neuf(e, 'boucle')
            point = Feuille.points.get((self.module, adresse))
            if point is not None and adresse in self.instructions:
                # Un point de modification : le crochet lit et écrit le processeur, et peut toucher à
                # la mémoire, pile comprise. L'état y est donc écrit avant, comme pour un appel, et
                # tout se relit après.
                self.materialiser(e, sortie)
                sortie.append(f"MODIF(c, {point[0]});   /* {point[1]} */")
                e.appels.append(f"[modification {point[1]}]")
                Feuille.points_poses.add((self.module, adresse))
                self.a_neuf(e, 'modification')
            i = self.instructions.get(adresse)
            if i is None:
                if adresse in self.indecodables:
                    raise Refus("instruction inconnue")
                # le flot sort des instructions de la fonction : la suite est dans une autre
                return self.suite_ailleurs(e, sortie, adresse)
            self.pc = adresse + (8 if self.arm else 4)
            if self.arm and i.cond == 15:
                raise Refus("instruction inconditionnelle spéciale")

            if i.op == "B" and i.target not in self.instructions:
                # un `b` vers une autre fonction : l'appel terminal du compilateur
                if i.cond == 14:
                    return self.suite_ailleurs(e, sortie, i.target)
                cnd = self.condition(e, i.cond, sortie)
                if cnd.const is not None:
                    if cnd.const:
                        return self.suite_ailleurs(e, sortie, i.target)
                    adresse += i.size
                    continue
                vrai, faux = [], []
                d_vrai = self.suite_ailleurs(e.copie(), vrai, i.target)
                d_faux = self.suivre(adresse + i.size, e, faux)
                sortie.append(('if', cnd.c, vrai, faux))
                return f"si {cnd.desc} : {d_vrai} ; sinon : {d_faux}"
            if i.op == "B":
                arriere = i.target <= adresse
                if i.cond == 14:
                    if arriere:
                        return self.reprendre_boucle(e, sortie, i.target, self.decrire_chemin(e, None))
                    adresse = i.target
                    continue
                cnd = self.condition(e, i.cond, sortie)
                if cnd.const is not None:                    # tranchée d'avance : une seule suite
                    if cnd.const and arriere:
                        return self.reprendre_boucle(e, sortie, i.target, self.decrire_chemin(e, None))
                    adresse = i.target if cnd.const else adresse + i.size
                    continue
                contraire = self.condition(e, i.cond ^ 1, sortie)
                # Le code d'origine reteste souvent ce qu'il vient de tester. Les valeurs d'un
                # chemin ne changent pas : la même expression y a la même issue, et l'autre
                # branche n'existe pas — inutile d'en faire un chemin que rien ne prendra jamais.
                if cnd.c in e.faits:
                    if e.faits[cnd.c] and arriere:           # tranchée, mais le saut reste une arête
                        return self.reprendre_boucle(e, sortie, i.target, self.decrire_chemin(e, None))
                    adresse = i.target if e.faits[cnd.c] else adresse + i.size
                    continue
                vrai, faux = [], []
                e_vrai = e.copie()
                e_vrai.faits[cnd.c], e_vrai.faits[contraire.c] = True, False
                e.faits[cnd.c], e.faits[contraire.c] = False, True
                if arriere:
                    d_vrai = self.reprendre_boucle(e_vrai, vrai, i.target, self.decrire_chemin(e_vrai, None))
                else:
                    d_vrai = self.suivre(i.target, e_vrai, vrai)
                d_faux = self.suivre(adresse + i.size, e, faux)
                sortie.append(('if', cnd.c, vrai, faux))
                if d_vrai == d_faux:
                    return d_vrai
                return f"si {cnd.desc} : {d_vrai} ; sinon : {d_faux}"
            if i.cond != 14:
                if not self.arm:
                    raise Refus("instruction conditionnelle")
                # ARM : une instruction sous condition. Celles qui terminent ou appellent
                # dédoublent le chemin ; les autres deviennent un choix de valeurs.
                cnd = self.condition(e, i.cond, sortie)
                terminale = i.op in ("BX", "BL", "BLX") or i.op in ("MRS", "MSR", "MCR", "MRC", "SWI", "BKPT") \
                    or (i.op == "LDM" and (i.reglist >> 15) & 1) \
                    or (i.op in ("MOV", "ADD", "SUB", "LDR") and i.rd == 15 and i.op != "STR")
                if cnd.const is not None:
                    if not cnd.const:
                        adresse += i.size
                        continue
                    # la condition est vraie : on l'exécute comme si elle n'en avait pas
                elif terminale:
                    contraire = self.condition(e, i.cond ^ 1, sortie)
                    vrai, faux = [], []
                    e_vrai = e.copie()
                    e_vrai.faits[cnd.c], e_vrai.faits[contraire.c] = True, False
                    e.faits[cnd.c], e.faits[contraire.c] = False, True
                    d_vrai = self.suivre_instruction(i, adresse, e_vrai, vrai)
                    d_faux = self.suivre(adresse + i.size, e, faux)
                    sortie.append(('if', cnd.c, vrai, faux))
                    return d_vrai if d_vrai == d_faux else f"si {cnd.desc} : {d_vrai} ; sinon : {d_faux}"
                else:
                    self.executer_predique(i, e, sortie, cnd)
                    adresse += i.size
                    continue

            if i.op == "BL" and self.branchement_long(i):    # bl employé comme branchement long
                self.ecrire(e, 14, Val.constante((adresse + i.size) | (0 if self.arm else 1)))
                if i.target <= adresse:
                    return self.reprendre_boucle(e, sortie, i.target, self.decrire_chemin(e, None))
                adresse = i.target
                continue
            if i.op in ("BL", "BLX"):
                retour = (adresse + i.size) | (0 if self.arm else 1)
                if i.target is not None:
                    # bl garde le mode de l'appelant ; blx immédiat passe à l'autre
                    cible_thumb = (i.op == "BL") != self.arm
                    self.appeler(e, sortie, Val.constante(i.target), cible_thumb, retour)
                else:
                    self.appeler(e, sortie, self.lire(e, i.rm, sortie), True, retour, indirect=True)
                adresse += i.size
                continue

            if i.op == "BX" and adresse in self.tables_de_sauts:
                return self.aiguiller(e, sortie, adresse, self.lire(e, i.rm, sortie), True)
            if adresse in self.tables_de_sauts and not self.arm and i.op in ("ADD", "MOV") and i.rd == 15:
                # le switch de CodeWarrior en Thumb : add pc, rX — pc vaut ici l'adresse plus quatre
                v = self.lire(e, i.rm, sortie)
                if i.op == "ADD":
                    v = binaire('+', Val.constante(self.pc), v, lambda x, y: x + y)
                return self.aiguiller(e, sortie, adresse, v, False)
            if adresse in self.tables_de_sauts and not (self.arm and i.op in ("MOV", "ADD", "SUB", "LDR") and i.rd == 15):
                raise Refus("table de sauts")
            if i.op == "BX":
                if i.rm == 14:
                    return self.terminer(e, sortie, None)
                if adresse in self.lr_avant:
                    # un appel par registre dont le retour est choisi : on appelle, puis on reprend là
                    retour = self.lr_avant[adresse]
                    self.appeler(e, sortie, self.lire(e, i.rm, sortie), True, retour, indirect=True)
                    if (retour & ~1) in self.instructions:
                        adresse = retour & ~1
                        continue
                    return self.terminer(e, sortie, Val.constante(retour))
                # le retour à l'ancienne (pop {r3}; bx r3) ou l'appel terminal par pointeur
                return self.terminer(e, sortie, self.lire(e, i.rm, sortie))

            if self.arm and i.op in ("MOV", "ADD", "SUB", "LDR") and i.rd == 15 and i.op != "STR":
                if i.s or (i.op == "LDR" and not i.load):
                    raise Refus("retour d'exception")
                # pc écrit par une opération : un saut, que rt_jump reconnaît comme retour ou suit
                self.executer_arm_vers_pc(i, e, sortie)
                if adresse in self.tables_de_sauts:
                    return self.aiguiller(e, sortie, adresse, e.reg.pop(15), False)
                return self.terminer(e, sortie, e.reg.pop(15))

            if i.op == "LDM":                                # pop, ou ldm rn!
                fini = self.suivre_ldm(i, adresse, e, sortie, seulement=True)
                if fini is not None:
                    return fini
            elif i.op in ("MRS", "MSR", "MCR", "MRC", "SWI", "BKPT"):
                self.systeme(i, e, sortie)
            else:
                self.executer(i, e, sortie)
                self.compacter(e, sortie)
            adresse += i.size

    def compacter(self, e: Etat, sortie: list) -> None:
        """Une expression qui se nourrit d'elle-même double à chaque instruction — un calcul de
        somme de contrôle suffit. Passé une certaine longueur, elle devient un temporaire : le
        code reste lisible, et l'interprète ne s'y noie pas."""
        for n, v in list(e.reg.items()):
            if len(v.c) > 160 and v.const is None:
                desc = v.desc if len(v.desc) <= 120 else v.desc[:117] + "…"
                e.reg[n] = self.nouveau_temp(sortie, v.c, desc)
        for d, v in list(e.flag.items()):
            if len(v.c) > 400:
                nom = f"d{self.temp}"
                self.temp += 1
                sortie.append(f"const uint32_t {nom} = {v.c};")
                e.flag[d] = Val(nom, "")
                e.prov = None

    def interpreter(self) -> None:
        if self.depart not in self.instructions:
            raise Refus("départ hors des instructions")
        self.description = self.suivre(self.depart, Etat(), self.arbre)

    # -- ce qu'on en tire
    @staticmethod
    def gabarit(code: str) -> str:
        """Le code émis, constantes effacées : deux feuilles de même gabarit ne diffèrent que
        par leurs nombres, et ce qui est prouvé pour l'une vaut pour l'autre."""
        corps = code.split("\n", 2)[2]                  # sans le commentaire ni le nom
        corps = re.sub(r"CHEMIN\(\d+, ", "CHEMIN(", corps)
        corps = re.sub(r'_DIRECTE?\(c, "\w+"', '_DIRECT(c, "R"', corps)
        return re.sub(r"0x[0-9A-F]+u", "K", corps)

    def emettre(self, symbole: str) -> str:
        def rendre(arbre: list, marge: str) -> list[str]:
            lignes = []
            for element in arbre:
                if isinstance(element, tuple) and element[0] == 'label':
                    lignes.append(f"{element[1]}:;")
                elif isinstance(element, tuple) and element[0] == 'switch':
                    _sw, expr, cibles = element
                    lignes.append(f"{marge}switch ({expr}) {{")
                    lignes += [f"{marge}case 0x{t:08X}u: goto L_{t:08X};" for t in cibles]
                    lignes.append(f"{marge}default: c->r[15] = {expr}; rt_jump(c, {expr}); return;")
                    lignes.append(f"{marge}}}")
                elif isinstance(element, tuple):
                    _si, cnd, vrai, faux = element
                    lignes.append(f"{marge}if {cnd} {{")
                    lignes += rendre(vrai, marge + "    ")
                    lignes.append(f"{marge}}} else {{")
                    lignes += rendre(faux, marge + "    ")
                    lignes.append(f"{marge}}}")
                else:
                    lignes.append(marge + element)
            return lignes

        corps = rendre(self.arbre, "    ")
        suite = "\n".join(corps)
        # une valeur d'entrée lue puis écrasée sans avoir servi n'a pas à être retenue
        entrees = [f"    const uint32_t e{n} = c->r[{n}];" for n in sorted(x for x in self.entrees if x >= 0)
                   if re.search(rf"\be{n}\b", suite)]
        champs = {'N': 'nf', 'Z': 'zf', 'C': 'cf', 'V': 'vf'}
        entrees += [f"    const uint32_t f{d} = c->{champs[d]};" for d in "NZCV"
                    if -ord(d) in self.entrees and re.search(rf"\bf{d}\b", suite)]
        description = self.description if len(self.description) < 400 else self.description[:397] + "…"
        ou = f"0x{self.fn['entrée']:08X}" + (f", prise en 0x{self.depart:08X}" if self.secondaire else "")
        return "\n".join([f"/* {self.module} {ou} — {description} */",
                          f"static void {symbole}(Cpu *c) {{"] + entrees + corps + ["}"])


# ------------------------------------------------------------------ le parcours

def modules(racine: Path):
    entete = (racine / 'system/header.bin').read_bytes()
    arm9 = compress.blz_decompress((racine / 'system/arm9.bin').read_bytes())
    base = struct.unpack_from('<I', entete, 0x28)[0]
    yield 'arm9', base, arm9
    # Le code rapide de l'ARM9 vit en ITCM : le crt0 l'y recopie depuis la fin de l'image, d'après
    # la liste d'autochargement. La carte range ces fonctions avec celles de l'ARM9.
    from nitro.memory import ModuleParams
    params = ModuleParams.find(arm9)
    if params:
        source, entree = params.autoload_start - base, params.autoload_list_start - base
        while entree < params.autoload_list_end - base:
            adresse, taille, _bss = struct.unpack_from('<3I', arm9, entree)
            entree += 12
            if adresse < 0x02000000:
                yield 'arm9', adresse, arm9[source:source + taille]
            source += taille
    ovt = (racine / 'system/ovt9.bin').read_bytes()
    for k in range(len(ovt) // 32):
        oid, ram, taille, _b, _s1, _s2, fid, _d = struct.unpack_from('<8I', ovt, k * 32)
        yield f'ovl{oid}', ram, (racine / f'files/__unnamed__/{fid:04d}.bin').read_bytes()[:taille]
    # Le coprocesseur : ce qui reste à son adresse de chargement, et les sections qu'il recopie
    # ailleurs — la plus grosse en WRAM, d'où il exécute son pilote son. Sa carte est à part
    # (analysis/codemap_arm7.json) ; ses fonctions portent toutes le module « arm7 ».
    from nitro.memory import arm7_autoload
    arm7 = (racine / 'system/arm7.bin').read_bytes()
    base7 = struct.unpack_from('<I', entete, 0x38)[0]
    trouve = arm7_autoload(arm7, base7)
    if trouve is None:
        yield 'arm7', base7, arm7
    else:
        debut_liste, fin_liste, debut_auto = trouve
        yield 'arm7', base7, arm7[:debut_auto - base7]
        entree, source = debut_liste - base7, debut_auto - base7
        while entree < fin_liste - base7:
            adresse, taille, _bss = struct.unpack_from('<3I', arm7, entree)
            entree += 12
            yield 'arm7', adresse, arm7[source:source + taille]
            source += taille


def moissonner(verdicts: dict, listes: list[str]) -> None:
    """Verse dans les verdicts ce que le banc a vu : l'état, et les chemins comparés. Une
    divergence ne s'efface pas : une feuille prise une fois en faute reste fautive jusqu'à
    ce qu'on ait compris pourquoi."""
    for liste in listes:
        for ligne in Path(liste).read_text(encoding='utf-8').splitlines():
            mots = ligne.split()
            if len(mots) < 3 or mots[2] == 'muette':
                continue
            cle = f"{mots[0]} {mots[1]}"
            chemins = int(mots[5], 16) if len(mots) > 5 else 1
            empreinte = mots[7] if len(mots) > 7 else '0'
            ancien = verdicts.get(cle)
            if not ancien or ancien.get('empreinte') != empreinte:
                ancien = {'etat': 'ok', 'chemins': 0}        # un autre code : l'ancienne preuve ne vaut plus
            if mots[2] == 'service':
                # en service pendant un parcours aux empreintes intactes : ces chemins-là ont fait
                # leur travail à l'identique — une preuve d'un autre ordre, rangée à part
                verdicts[cle] = dict(ancien, empreinte=empreinte,
                                     service=ancien.get('service', 0) | chemins)
                continue
            verdicts[cle] = dict(ancien, etat='FAUX' if mots[2] == 'FAUX' or ancien['etat'] == 'FAUX' else 'ok',
                                 chemins=ancien['chemins'] | chemins, empreinte=empreinte)


_LECTURE = re.compile(r"^(?:\(uint32_t\))?(?:\(int32_t\)\(int8_t\))?(?:ldr_word|mem_read32|ldr_h|ldr_sh|mem_read8)"
                      r"\(c, (.*)\)$")
_ADRESSE = re.compile(r"^\(?(e\d+|t\d+)(?: ([+-]) 0x([0-9A-F]+)u)?\)?$")
_FIXE = re.compile(r"^0x(2[0-3][0-9A-F]{5}|3[78][0-9A-F]{5})u$")   # la RAM du jeu, la WRAM du coprocesseur


def _accumulateurs_inclus() -> set[int] | None:
    """tools/accumulateurs_inclus.txt (tools/accumulateurs.py) ; SFC_ETUDE_ACC=1 à la génération : aucune liste."""
    import os
    f = Path(__file__).with_name("accumulateurs_inclus.txt")
    if os.environ.get("SFC_ETUDE_ACC") or not f.exists():
        return None
    return {int(l.split()[0], 16) for l in f.read_text(encoding="utf-8").split("\n") if l.strip() and not l.startswith("#")}


ACCUMULATEURS_INCLUS = _accumulateurs_inclus()


def somme_avec(valeur: str, temp: str) -> bool:
    """La valeur rangée est-elle « ce qu'on a lu, plus (ou moins) autre chose » — un mouvement — plutôt qu'un
    masque de bits, un alignement ou une avancée d'un pas fixe (un tas, un index) ? Ce que les tours d'aperçu
    mettent à l'échelle doit être une vitesse ajoutée à une position, jamais une taille de bloc."""
    v = re.sub(r"\(uint(?:8|16)_t\)", "", valeur).strip()
    if re.search(r"[|&^~]|<<|>>", v):
        return False
    m = re.fullmatch(r"\((.+) ([+-]) (.+)\)", v)
    if not m:
        return False
    a, autre = m.group(1), m.group(3)
    if a != temp and autre != temp:
        return False
    if a == temp and re.fullmatch(r"0x[0-9A-Fa-f]+u?|\d+u?", autre):
        return False              # un pas fixe : un allocateur, un index
    return True


def site_acc(module: str, pc: int) -> int:
    """Le nom d'un site : son adresse, et son module dans l'octet haut — les overlays se partagent les
    mêmes adresses. arm9 : 0 ; ovlN : N + 1."""
    idx = 0 if module == "arm9" else int(module[3:]) + 1 if module.startswith("ovl") else 0xFF
    return (idx << 24) | (pc & 0xFFFFFF)


def accumulateurs(code: str, module: str) -> str:
    """mem_writeN(c, EA, V); /* acc tK 0xPC */  ->  ECRIT_ACCN(c, EA, V, tK, site) — sauf les sites que l'étude
    a vus construire plutôt qu'intégrer (tools/accumulateurs_exclus.txt : ils réécrivent une même adresse
    plusieurs fois par tour — un index de liste, une somme), qui restent l'écriture même."""
    def remplacer(m):
        site = site_acc(module, int(m.group(4), 16))
        if not accumulateur_retenu(site):
            return f"mem_write{m.group(1)}(c, {m.group(2)});"
        return f"ECRIT_ACC{m.group(1)}(c, {m.group(2)}, {m.group(3)}, 0x{site:08X}u);"

    def remplacer_d(m):
        site = site_acc(module, int(m.group(5), 16))
        if not accumulateur_retenu(site):
            return f"mem_write{m.group(1)}(c, {m.group(2)}, {m.group(3)});"
        (t1, a1), (t2, a2) = (c.split("@", 1) for c in m.group(4).split("|"))
        return f"ECRIT_ACCD{m.group(1)}(c, {m.group(2)}, {m.group(3)}, {t1}, {a1}, {t2}, {a2}, 0x{site:08X}u);"
    code = re.sub(r"mem_write(\d+)\(c, (.*)\); /\* acc (t\d+) (0x[0-9A-F]+) \*/", remplacer, code)
    return re.sub(r"mem_write(\d+)\(c, (.*?), (.*)\); /\* accd (.*) (0x[0-9A-F]+) \*/", remplacer_d, code)


def accumulateur_retenu(site: int) -> bool:
    """Sans liste (l'étude n'a pas encore eu lieu), tous les sites sont émis, pour qu'elle les voie ; avec elle,
    seuls ceux qu'elle a retenus."""
    return ACCUMULATEURS_INCLUS is None or site in ACCUMULATEURS_INCLUS


def remarquables_du_code(code: str) -> set[int]:
    """Les valeurs qui font basculer une condition sans y figurer telles quelles. « (r0 + 8) == 0 » —
    le test de nullité d'un `this` ajusté, de l'adresse d'un élément de tableau — ne se prend
    qu'avec r0 = -8 ; « (x - 4) > 2 » bascule entre 4 et 7 ; « ~0 » s'écrit ainsi dans le code."""
    valeurs: set[int] = set()
    for cond in re.findall(r"^\s*if \((.*)\) \{$", code, re.M):
        egal = re.search(r"[=!]= 0x([0-9A-F]+)u$", cond)
        if egal:
            for k in re.findall(r" \+ 0x([0-9A-F]+)u\)", cond):
                valeurs.add((int(egal.group(1), 16) - int(k, 16)) & 0xFFFFFFFF)
        for k, n in re.findall(r"\(\w+ - 0x([0-9A-F]+)u\) (?:>|>=|<|<=) 0x([0-9A-F]+)u", cond):
            valeurs.update({int(k, 16), (int(k, 16) + int(n, 16)) & 0xFFFFFFFF})
        for k in re.findall(r"\(~ ?0x([0-9A-F]+)u\)", cond):
            valeurs.add(~int(k, 16) & 0xFFFFFFFF)
    return valeurs


def sites_du_code(code: str) -> list[tuple]:
    """De quelles entrées dépendent les conditions. Un site : (genre, registre, adresse, décalages).
    Genre 1, un registre d'entrée ; 2, un mot au bout d'une chaîne partie de ce registre — l'objet,
    l'objet dans l'objet, le nœud de la liste, trois décalages au plus ; 3, la même chaîne partie
    d'une globale, dont le premier décalage est nul : le singleton et ses champs ; 4, un registre
    tel qu'une appelée le rend — r0, sa réponse, mais aussi r4 : « r4 = new ; … ; if (r4 == 0) »
    ne se prend que si une appelée écrase ce qu'elle devait préserver, et les deux versions
    doivent alors s'accorder comme ailleurs."""
    defs = dict(re.findall(r"const uint32_t (t\d+) = (.*);", code))
    lieu: dict[str, tuple] = {}                      # temporaire -> (genre, registre, adresse, décalages)
    for nom, expr in defs.items():
        m = _LECTURE.match(expr)
        if not m:
            continue
        fixe = _FIXE.match(m.group(1))
        if fixe:
            lieu[nom] = (3, 0, int(fixe.group(1), 16), (0,))
            continue
        a = _ADRESSE.match(m.group(1))
        if not a:
            continue
        base = a.group(1)
        d = int(a.group(3) or "0", 16) * (-1 if a.group(2) == '-' else 1)
        if not -0x800 < d < 0x1000:
            continue
        if base[0] == 'e' and int(base[1:]) <= 12:
            lieu[nom] = (2, int(base[1:]), 0, (d,))
        elif base in lieu and len(lieu[base][3]) < 3:    # définis dans l'ordre : le maillon d'avant est connu
            genre, registre, adresse, chaine = lieu[base]
            lieu[nom] = (genre, registre, adresse, chaine + (d,))
    sites: list[tuple] = []

    def noter(site: tuple):
        if site not in sites:
            sites.append(site)

    def fouiller(expr: str, profondeur: int = 0):
        for mot in re.findall(r"\b(?:t\d+|e\d+(?:_\d+)?)\b", expr):
            if mot[0] == 't':
                if mot in lieu:
                    noter(lieu[mot])
                elif mot in defs and profondeur < 3:
                    fouiller(defs[mot], profondeur + 1)
            elif '_' not in mot:
                if int(mot[1:]) <= 12:
                    noter((1, int(mot[1:]), 0, ()))
            elif int(mot[1:].split('_')[0]) <= 12:
                noter((4, int(mot[1:].split('_')[0]), 0, ()))
    for cond in re.findall(r"^\s*(?:if|switch) \((.*)\) \{$", code, re.M):
        if 'redirect' not in cond and 'g_edge_budget' not in cond:
            fouiller(cond)
    return sites[:48]


def main() -> int:
    sys.setrecursionlimit(20000)
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--data', default='data')
    ap.add_argument('--codemap', default='analysis/codemap.json')
    ap.add_argument('-o', '--out', default='generated/natif_auto.inc')
    ap.add_argument('--json', default='analysis/feuilles.json')
    ap.add_argument('--verdicts', default='analysis/feuilles_verdicts.json',
                    help="ce que le banc a établi, campagne après campagne")
    ap.add_argument('--moisson', nargs='*', default=[],
                    help="listes écrites par le banc (SFC_VERIF_LISTE) à verser dans les verdicts")
    ap.add_argument('--oublier', action='store_true',
                    help="repartir de verdicts vides : à faire quand le générateur change ce qu'il émet")
    ap.add_argument('--natif', default='runtime/natif.c', help="le registre des réécritures faites à la main")
    ap.add_argument('--modifications', default='runtime/modifications.c',
                    help="le registre des points de modification : un crochet avant l'instruction nommée")
    ap.add_argument('--symboles', default='analysis/symboles.json', help="l'annuaire : ses noms certains servent à décrire")
    ap.add_argument('--partielles', default='analysis/feuilles_partielles.txt',
                    help="où écrire la liste des fonctions sans preuve complète (SFC_FROID_LISTE)")
    ap.add_argument('--milieux', default='tools/entrees_milieu.txt',
                    help="les adresses où l'on entre au milieu d'une fonction, observées à l'exécution")
    ap.add_argument('--suffixe', default='',
                    help="suffixe des tables (7 pour le coprocesseur : g_auto7_N, natif_auto7_N.c, NATIF_AUTO7_N)")
    ap.add_argument('--parties', type=int, default=PARTIES, help="en combien de fichiers répartir les fonctions")
    ap.add_argument('--faute', action='store_true', help="abîme chaque feuille, pour éprouver le banc")
    args = ap.parse_args()
    global FAUTE
    FAUTE = args.faute

    codemap = json.loads(Path(args.codemap).read_text(encoding='utf-8'))
    Feuille.resolution = Resolution(codemap)
    if Path(args.symboles).exists():
        Feuille.noms = {int(k, 16): v['nom'] for k, v in
                        json.loads(Path(args.symboles).read_text(encoding='utf-8')).items() if v.get('nom')}
    chemin_verdicts = Path(args.verdicts)
    verdicts = {}
    if chemin_verdicts.exists() and not args.oublier:
        verdicts = json.loads(chemin_verdicts.read_text(encoding='utf-8'))
    moissonner(verdicts, args.moisson)
    if args.moisson or args.oublier:
        chemin_verdicts.write_text(json.dumps(verdicts, indent=0, sort_keys=True), encoding='utf-8')

    # ce qu'on a déjà réécrit à la main, avec ses raisons, n'est pas à refaire ici
    a_la_main = {(m.group(1), int(m.group(2), 16)) for m in
                 re.finditer(r'X\(\d+, "(\w+)", (0x[0-9A-Fa-f]+),', Path(args.natif).read_text(encoding='utf-8'))}
    # les points de modification, s'il y en a : P(rang, "région", adresse, nom, "ce qu'il change")
    if args.modifications and Path(args.modifications).exists():
        Feuille.points = {(m.group(2), int(m.group(3), 16)): (int(m.group(1)), m.group(4)) for m in
                          re.finditer(r'P\((\d+), "(\w+)", (0x[0-9A-Fa-f]+), (\w+),',
                                      Path(args.modifications).read_text(encoding='utf-8'))}

    feuilles = []
    refus = Counter()
    refusees: list[str] = []

    def traiter(module: str, base: int, donnees: bytes, f: dict, depart: int | None = None) -> None:
        """Interprète une fonction — depuis son entrée, ou depuis `depart` pour une réécriture
        secondaire — et la range parmi les feuilles, ou parmi les refus avec sa raison."""
        secondaire = depart is not None and depart != f['entrée']
        # la région d'une secondaire porte l'entrée de la fonction entière : deux fonctions qui se
        # chevauchent peuvent être rejointes à la même adresse, et ce sont deux codes
        region = f"{module}@{f['entrée']:08X}" if secondaire else module
        ou = depart if secondaire else f['entrée']
        feuille = Feuille(module, base, donnees, f, len(feuilles), depart=depart)
        try:
            try:
                feuille.interpreter()
            except (Refus, RecursionError) as e:
                if str(e) != "trop de chemins" and not isinstance(e, RecursionError):
                    raise
                feuille = Feuille(module, base, donnees, f, len(feuilles), jonctions=True, depart=depart)
                feuille.interpreter()
        except Refus as e:
            refus[str(e)] += 1
            refusees.append(f"{region} 0x{ou:08X} {e}")
            return
        except RecursionError:
            refus["trop profonde pour l'interprète"] += 1
            refusees.append(f"{region} 0x{ou:08X} trop profonde pour l'interprète")
            return
        feuille.region = region
        feuille.symbole = f"a_{module}_{f['entrée']:08X}" + (f"_{depart:08X}" if secondaire else "")
        feuille.code = feuille.emettre(feuille.symbole)
        feuille.forme = feuille.gabarit(feuille.code)
        feuille.cle = f"{region} 0x{ou:08X}"
        # une preuve vaut pour le code qu'on a comparé, pas pour celui que le générateur
        # émettra demain : l'empreinte voyage avec la feuille jusque dans la liste du banc
        neutre = re.sub(r"CHEMIN\(\d+, ", "CHEMIN(", feuille.code.split("\n", 1)[1])
        neutre = re.sub(r" /\* accd? .*? 0x[0-9A-F]+ \*/", "", neutre)
        feuille.empreinte = zlib.crc32(neutre.encode()) & 0xFFFFFFFF
        # une constante dans la plage des registres matériels : on ne l'essaiera pas à froid
        # 1, elle nomme un registre matériel : à froid un bouchon lui répond ; 2, elle s'appelle
        # elle-même : elle se juge en jeu
        feuille.materiel = 2 if feuille.recursive else 1 if re.search(r"0x4[0-9A-F]{6}u", feuille.code) else 0
        # Les décalages auxquels elle accède — « [arg0 + 0x160] » : le banc à froid, quand il retouche
        # un essai, écrit là plutôt qu'au hasard. Les plus petits d'abord, quarante-huit au plus.
        feuille.decalages = sorted({0} | {int(d, 16) for d in re.findall(r" \+ 0x([0-9A-F]+)u\)", feuille.code)
                                          if int(d, 16) < 0x1000})[:48]
        feuille.sites = sites_du_code(feuille.code)
        feuille.remarquables |= remarquables_du_code(feuille.code)
        feuilles.append(feuille)

    # le code ITCM de l'ARM9 vit hors de l'image ; on s'en tient à ce que l'image contient
    les_modules = list(modules(Path(args.data)))
    for module, base, donnees in les_modules:
        for f in codemap.get(module, []):
            if f['taille'] > TAILLE_MAX:
                refus["plus de %d octets" % TAILLE_MAX] += 1
                refusees.append(f"{module} 0x{f['entrée']:08X} plus de {TAILLE_MAX} octets")
                continue
            if not (base <= f['début'] and f['fin'] <= base + len(donnees)):
                continue
            if (module, f['entrée']) in a_la_main:
                refus["déjà réécrite à la main"] += 1
                continue
            traiter(module, base, donnees, f)

    # Les entrées secondaires. Celles que le code émis rejoint, on les connaît ; les autres — un
    # thread qui reprend au retour d'OS_SaveContext, un retour par le dispatch — ne se voient
    # qu'à l'exécution : la mesure (-DSFC_COMPTE_TRADUIT=ON, SFC_TRADUIT) les nomme, et le
    # fichier des entrées observées les garde. Une secondaire peut en rejoindre une autre : on
    # recommence tant qu'il en vient de nouvelles.
    observees = set()
    if Path(args.milieux).exists():
        for ligne in Path(args.milieux).read_text(encoding='utf-8').splitlines():
            mots = ligne.split('#')[0].split()
            adresses = [m for m in mots if m.lower().startswith('0x')]
            if not adresses:
                continue
            adresse = int(adresses[0], 16)
            for module, base, donnees in les_modules:
                for f in codemap.get(module, []):
                    if f['début'] <= adresse < f['fin'] and adresse != f['entrée'] \
                            and base <= f['début'] and f['fin'] <= base + len(donnees):
                        observees.add((module, f['entrée'], adresse))
    par_entree = {(module, f['entrée']): (base, donnees, f)
                  for module, base, donnees in les_modules for f in codemap.get(module, [])
                  if base <= f['début'] and f['fin'] <= base + len(donnees)}
    faites: set = set()
    while True:
        a_faire = sorted((Feuille.visees | observees) - faites)
        if not a_faire:
            break
        for module, entree, adresse in a_faire:
            faites.add((module, entree, adresse))
            if (module, entree) in a_la_main or (module, entree) not in par_entree:
                continue
            base, donnees, f = par_entree[(module, entree)]
            if f['taille'] <= TAILLE_MAX:
                traiter(module, base, donnees, f, depart=adresse)

    # La preuve, chemin par chemin. Par instance : le banc a comparé tous les chemins de cette
    # feuille à sa traduction, sans écart. Par gabarit : il l'a fait sur des feuilles dont le
    # code ne diffère que par les constantes. Un gabarit pris une seule fois en faute ne prouve
    # plus rien pour personne.
    vus_par_gabarit, gabarits_faux = defaultdict(int), set()
    for feuille in feuilles:                # un verdict rendu sur un autre code est sans valeur
        v = verdicts.get(feuille.cle)
        if v and v.get('empreinte') != f"{feuille.empreinte:08X}":
            del verdicts[feuille.cle]
    for feuille in feuilles:
        v = verdicts.get(feuille.cle)
        if v and v['etat'] == 'ok':
            vus_par_gabarit[feuille.forme] |= v['chemins']
        elif v:
            gabarits_faux.add(feuille.forme)
    bilan = Counter()
    for feuille in feuilles:
        v = verdicts.get(feuille.cle)
        tous = (1 << feuille.chemins) - 1
        if v and v['etat'] == 'FAUX':
            feuille.prouvee = -1
        elif feuille.forme in gabarits_faux:
            feuille.prouvee = 0
        elif v and v['chemins'] & tous == tous:
            feuille.prouvee = 1
        elif vus_par_gabarit[feuille.forme] & tous == tous:
            feuille.prouvee = 2
        elif v and (v['chemins'] | v.get('service', 0) | vus_par_gabarit[feuille.forme]) & tous == tous:
            feuille.prouvee = 3              # le reste de ses chemins, aux empreintes
        else:
            feuille.prouvee = 0
        bilan[feuille.prouvee] += 1

    n = len(feuilles)
    # Dans un seul fichier, le compilateur passerait une heure sur ces dizaines de milliers de
    # fonctions : elles sont réparties en parties, compilées en parallèle avec le code traduit.
    # natif.c inclut natif_auto.inc, qui ne contient que les tables des parties, et les réunit.
    dossier = Path(args.out).parent
    dossier.mkdir(parents=True, exist_ok=True)
    sfx, parties = args.suffixe, args.parties
    # Les fonctions du coprocesseur suivent celles de l'ARM9 dans la table réunie : NATIF_ARM7 fait
    # pointer CHEMIN et le crochet de vérification sur leur tranche à elles (natif.h).
    tete = ["/* Généré par tools/decomp_feuilles.py — ne pas modifier à la main. */"] + \
           (["#define NATIF_ARM7 1"] if sfx else []) + ['#include "natif.h"', ""]
    for partie in range(parties):
        # des tranches contiguës : réunies dans l'ordre, elles redonnent à chaque fonction sa place
        taille = (n + parties - 1) // parties
        lot = feuilles[partie * taille:(partie + 1) * taille]
        sortie = list(tete) + [f"/* partie {partie} : {len(lot)} fonctions */", ""]
        sortie += [accumulateurs(f.code, f.module) for f in lot]
        sortie.append("")
        for f in lot:
            sortie.append(f"static void v_{f.symbole}(Cpu *c) {{ natif_verifier(c, &g_auto_reuni[{f.indice}]); }}")
            if f.remarquables:
                valeurs = ", ".join(f"0x{v:X}u" for v in sorted(f.remarquables))
                sortie.append(f"static const uint32_t q_{f.symbole}[] = {{{valeurs}}};")
            if f.globales:
                valeurs = ", ".join(f"0x{v:X}u" for v in sorted(f.globales))
                sortie.append(f"static const uint32_t g_{f.symbole}[] = {{{valeurs}}};")
            if len(f.decalages) > 1:
                valeurs = ", ".join(f"0x{v:X}" for v in f.decalages)
                sortie.append(f"static const uint16_t d_{f.symbole}[] = {{{valeurs}}};")
            if f.sites:
                valeurs = ", ".join(f"{{{g}, {n}, {len(ch)}, {{{', '.join(str(d) for d in ch) or '0'}}}, 0x{a:08X}u}}"
                                    for g, n, a, ch in f.sites)
                sortie.append(f"static const Site s_{f.symbole}[] = {{{valeurs}}};")
        sortie.append(f"const size_t g_auto{sfx}_{partie}_n = {len(lot)};")
        sortie.append(f"const Reecriture g_auto{sfx}_{partie}[{max(len(lot), 1)}] = {{")
        for f in lot:
            propre = f.description.replace('\\', '\\\\').replace('"', '\\"')
            if len(propre) > 200:
                propre = propre[:197] + "…"
            remarquables = f"q_{f.symbole}, {len(f.remarquables)}" if f.remarquables else "NULL, 0"
            globales = f"g_{f.symbole}, {len(f.globales)}" if f.globales else "NULL, 0"
            englobante = f"0x{f.fn['entrée']:08X}u" if f.secondaire else "0"
            decalages = f"d_{f.symbole}, {len(f.decalages)}" if len(f.decalages) > 1 else "NULL, 0"
            sites = f"s_{f.symbole}, {len(f.sites)}" if f.sites else "NULL, 0"
            sortie.append(f'    {{"{f.module}", 0x{f.depart:08X}u, {f.symbole}, '
                          f'"{f.symbole}", "{propre}", v_{f.symbole}, NULL, '
                          f'{1 if f.saute else 0}, {f.prouvee}, {f.chemins}, '
                          f'{f.materiel}, {remarquables}, 0x{f.empreinte:08X}u, {globales}, {englobante}, '
                          f'{decalages}, {sites}}},')
        if not lot:
            sortie.append("    {NULL, 0, NULL, NULL, NULL, NULL, NULL, 0, 0, 0, 0, NULL, 0, 0, NULL, 0, 0, NULL, 0, NULL, 0},")
        sortie.append("};")
        (dossier / f"natif_auto{sfx}_{partie}.c").write_text("\n".join(sortie) + "\n", encoding='utf-8')
    inc = ["/* Généré par tools/decomp_feuilles.py — ne pas modifier à la main.",
           f" * {n} fonctions réécrites par interprétation symbolique, en {parties} parties. */",
           f"#define NATIF_AUTO{sfx}_N {n}", f"#define NATIF_AUTO{sfx}_PARTIES {parties}"]
    for partie in range(parties):
        inc.append(f"extern const Reecriture g_auto{sfx}_{partie}[]; extern const size_t g_auto{sfx}_{partie}_n;")
    inc.append(f"static const Reecriture *const g_auto{sfx}_parties[] = {{" +
               ", ".join(f"g_auto{sfx}_{k}" for k in range(parties)) + "};")
    inc.append(f"static const size_t *const g_auto{sfx}_tailles[] = {{" +
               ", ".join(f"&g_auto{sfx}_{k}_n" for k in range(parties)) + "};")
    Path(args.out).write_text("\n".join(inc) + "\n", encoding='utf-8')
    noms_preuve = {1: 'instance', 2: 'gabarit', 3: 'empreintes', 0: 'aucune', -1: 'FAUX'}
    Path(args.json).write_text(json.dumps(
        [{'module': f.region, 'entrée': f"0x{f.depart:08X}", 'effet': f.description,
          'chemins': f.chemins, 'preuve': noms_preuve[f.prouvee]} for f in feuilles],
        indent=1, ensure_ascii=False), encoding='utf-8')

    # celles dont il manque des chemins : la liste que reprend une seconde passe à froid
    Path(args.partielles).write_text("".join(f"{f.cle}\n" for f in feuilles if f.prouvee == 0),
                                     encoding='utf-8', newline='\n')
    # et celles qu'il refuse, chacune avec sa raison : ce qui reste à couvrir
    Path(args.partielles).with_name(Path(args.partielles).name.replace('partielles', 'refusees')).write_text(
        "".join(ligne + "\n" for ligne in refusees), encoding='utf-8', newline='\n')
    print(f"{n} feuilles réécrites -> {args.out}")
    print(f"  prouvées par instance : {bilan[1]}, par gabarit : {bilan[2]}, aux empreintes : {bilan[3]}, "
          f"sans preuve complète : {bilan[0]}, fautives : {bilan[-1]}")
    print(f"  {len(set(f.forme for f in feuilles))} gabarits distincts, "
          f"dont {sum(1 for v in vus_par_gabarit.values() if v)} vus par le banc et {len(gabarits_faux)} pris en faute")
    print(f"  chemins : {dict(sorted(Counter(f.chemins for f in feuilles).items()))}")
    print(f"  dont {sum(1 for f in feuilles if f.fn['appels'])} fonctions qui appellent")
    print("laissées de côté :")
    for raison, combien in refus.most_common(14):
        print(f"  {combien:5d}  {raison}")
    effets = Counter(f.description for f in feuilles)
    print("effets les plus fréquents :")
    for d, combien in effets.most_common(10):
        print(f"  {combien:5d}  {d}")
    if Feuille.points:
        print(f"points de modification : {len(Feuille.points_poses)} posés sur {len(Feuille.points)}")
        for (module, adresse), (rang, nom) in sorted(Feuille.points.items(), key=lambda p: p[1][0]):
            if (module, adresse) not in Feuille.points_poses:
                print(f"  ABSENT : {nom} ({module} 0x{adresse:08X}) — aucune instruction de fonction réécrite à cette adresse")
    return 0


if __name__ == '__main__':
    raise SystemExit(main())

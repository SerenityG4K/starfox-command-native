"""L'inventaire des accès du jeu : quels champs, à quels décalages, lus ou écrits par quelles fonctions.

Le code réécrit est fidèle mais sans noms : il manipule des décalages bruts. Pour modifier le jeu, il
faut d'abord savoir ce qu'il touche. Cet outil lit le code produit par le générateur et en tire deux
choses :

- pour chaque fonction, les décalages qu'elle lit et écrit, groupés par la base dont ils partent : une
  base qui reçoit les décalages 0x18, 0x24 et 0x3C décrit un objet à trois champs connus ;
- l'inverse : pour une adresse absolue (0x021D1E94, le gestionnaire du terrain), toutes les fonctions
  qui y touchent — ce qui répond à « qui décide cela » sans lire une ligne.

Deux bases différentes qui reçoivent exactement les mêmes décalages sont vraisemblablement du même
type : c'est ce rapprochement que l'outil propose, à charge pour l'œil de le confirmer.

    python tools/structures.py                       inventaire complet -> analysis/acces.json
    python tools/structures.py --adresse 0x021D1E94  qui touche à cet objet
    python tools/structures.py --fonction 0x02178320 ce que cette fonction touche
    python tools/structures.py --types               les bases qui se ressemblent, par famille

Sous licence MIT.
"""
import argparse
import json
import re
from collections import defaultdict
from pathlib import Path

RACINE = Path(__file__).resolve().parent.parent
GENERE = RACINE / "generated"
SORTIE = RACINE / "analysis" / "acces.json"

# ldr_word(c, (e5_18 + 0x3Cu))      lecture d'un mot à un décalage
# mem_write32(c, (e13_18 + 0x30u), …)   écriture
LECTURE = re.compile(r"\b(ldr_word|ldr_sh|ldr_sb|ldr_h|ldr_b)\(c,\s*\(([A-Za-z_][A-Za-z0-9_]*)\s*\+\s*0x([0-9A-Fa-f]+)u\)\)")
ECRITURE = re.compile(r"\bmem_write(32|16|8)\(c,\s*\(([A-Za-z_][A-Za-z0-9_]*)\s*\+\s*0x([0-9A-Fa-f]+)u\)")
# les accès à une adresse fixe : ldr_word(c, 0x21D1E94u)
LECTURE_FIXE = re.compile(r"\b(ldr_word|ldr_sh|ldr_sb|ldr_h|ldr_b)\(c,\s*0x([0-9A-Fa-f]+)u\)")
ECRITURE_FIXE = re.compile(r"\bmem_write(32|16|8)\(c,\s*0x([0-9A-Fa-f]+)u")
# /* ovl0 0x02178320 — … */ ou /* arm9 0x0202E644 … */
ENTETE = re.compile(r"^/\*\s*(arm9|arm7|ovl\d+)\s+0x([0-9A-Fa-f]{8})\s")

TAILLES = {"ldr_word": 4, "ldr_sh": 2, "ldr_h": 2, "ldr_sb": 1, "ldr_b": 1, "32": 4, "16": 2, "8": 1}


def lire():
    """Parcourt le code généré : rend, par fonction, ses accès."""
    fonctions = {}
    for fichier in sorted(GENERE.glob("*.c")):
        courante = None
        for ligne in fichier.read_text(encoding="utf-8", errors="replace").splitlines():
            entete = ENTETE.match(ligne)
            if entete:
                courante = f"{entete.group(1)} 0x{entete.group(2).upper()}"
                fonctions.setdefault(courante, {"bases": defaultdict(set), "fixes": {}, "fichier": fichier.name})
                continue
            if courante is None:
                continue
            f = fonctions[courante]
            for quoi, base, off in LECTURE.findall(ligne):
                f["bases"][base].add((int(off, 16), TAILLES[quoi], "lu"))
            for taille, base, off in ECRITURE.findall(ligne):
                f["bases"][base].add((int(off, 16), TAILLES[taille], "écrit"))
            for quoi, adr in LECTURE_FIXE.findall(ligne):
                a = int(adr, 16)
                if a >= 0x02000000:
                    f["fixes"].setdefault(a, set()).add(("lu", TAILLES[quoi]))
            for taille, adr in ECRITURE_FIXE.findall(ligne):
                a = int(adr, 16)
                if a >= 0x02000000:
                    f["fixes"].setdefault(a, set()).add(("écrit", TAILLES[taille]))
    return fonctions


def champs_d_une_base(acces):
    """Les champs d'une base, triés par décalage : (décalage, taille, lu/écrit/les deux)."""
    par_offset = defaultdict(lambda: [0, set()])
    for off, taille, sens in acces:
        par_offset[off][0] = max(par_offset[off][0], taille)
        par_offset[off][1].add(sens)
    return [(off, t, "+".join(sorted(s))) for off, (t, s) in sorted(par_offset.items())]


def ecrire_inventaire(fonctions):
    SORTIE.parent.mkdir(parents=True, exist_ok=True)
    plat = {}
    for nom, f in fonctions.items():
        bases = {b: champs_d_une_base(a) for b, a in f["bases"].items() if a}
        fixes = {f"0x{a:08X}": sorted({s for s, _ in v}) for a, v in f["fixes"].items()}
        if bases or fixes:
            plat[nom] = {"fichier": f["fichier"], "bases": bases, "fixes": fixes}
    SORTIE.write_text(json.dumps(plat, indent=1, ensure_ascii=False), encoding="utf-8")
    champs = sum(len(c) for f in plat.values() for c in f["bases"].values())
    print(f"{len(plat)} fonctions, {champs} champs relevés -> {SORTIE.relative_to(RACINE)}")


def qui_touche(fonctions, adresse):
    """Les fonctions qui lisent ou écrivent à cette adresse fixe, et à quels décalages autour."""
    trouve = []
    for nom, f in fonctions.items():
        for a, v in f["fixes"].items():
            if adresse <= a < adresse + 0x200:
                trouve.append((nom, a, sorted({s for s, _ in v})))
    if not trouve:
        print(f"personne ne touche 0x{adresse:08X} par une adresse fixe")
        print("  (le jeu y accède peut-être par un pointeur : chercher la fonction qui le pose)")
        return
    print(f"{len(trouve)} accès à 0x{adresse:08X} et ses deux cents octets suivants :")
    for nom, a, sens in sorted(trouve, key=lambda t: (t[1], t[0])):
        print(f"   0x{a:08X} (+0x{a - adresse:X}) {'/'.join(sens):12s} par {nom}")


def ce_que_touche(fonctions, nom_cherche):
    cherche = nom_cherche.upper().lstrip("0X").lstrip("0x")
    for nom, f in fonctions.items():
        if nom.upper().endswith(cherche):
            print(f"{nom} ({f['fichier']})")
            for base, acces in sorted(f["bases"].items()):
                champs = champs_d_une_base(acces)
                if len(champs) < 2:
                    continue
                print(f"   base {base} : " + ", ".join(f"+0x{o:X} ({t} o, {s})" for o, t, s in champs))
            if f["fixes"]:
                fixes = sorted(f["fixes"])[:12]
                print("   adresses fixes : " + ", ".join(f"0x{a:08X}" for a in fixes)
                      + (" …" if len(f["fixes"]) > 12 else ""))
            return
    print(f"fonction {nom_cherche} introuvable")


def familles(fonctions, minimum=4):
    """Les bases qui reçoivent exactement les mêmes décalages : vraisemblablement un même type."""
    par_signature = defaultdict(list)
    for nom, f in fonctions.items():
        for base, acces in f["bases"].items():
            champs = champs_d_une_base(acces)
            if len(champs) < minimum:
                continue
            par_signature[tuple(o for o, _, _ in champs)].append(f"{nom}:{base}")
    familles = sorted(((sig, qui) for sig, qui in par_signature.items() if len(qui) > 1),
                      key=lambda t: -len(t[1]))
    print(f"{len(familles)} familles de bases partageant les mêmes champs (au moins {minimum}) :")
    for sig, qui in familles[:25]:
        print(f"   {len(qui):3d} bases : " + ", ".join(f"+0x{o:X}" for o in sig[:10])
              + (" …" if len(sig) > 10 else ""))
        print(f"        par exemple {', '.join(qui[:3])}")


def objets(fonctions, releve=RACINE / "analysis" / "objets.txt"):
    """Croise le relevé d'exécution (SFC_ETUDE_OBJETS) avec l'inventaire : des objets réels et leurs champs.

    Le relevé dit sur quelles adresses une fonction a été appelée ; l'inventaire dit quels décalages
    elle touche à partir de son premier argument. Ensemble : telle zone de mémoire porte tels champs.
    Les objets sont groupés par tranche de 64 Ko — les tas du jeu s'y distinguent bien.
    """
    if not releve.exists():
        print(f"pas de relevé : lancer le jeu avec SFC_ETUDE_OBJETS={releve.relative_to(RACINE)}")
        return
    par_fonction = {}
    for nom, f in fonctions.items():
        adr = nom.split()[-1].upper()
        # les champs vus depuis le premier argument : les bases nommées e0, e0_1, e0_2…
        champs = set()
        for base, acces in f["bases"].items():
            if base == "e0" or base.startswith("e0_"):
                champs |= {(o, t) for o, t, _ in champs_d_une_base(acces)}
        if champs:
            par_fonction[adr] = champs
    tas = defaultdict(lambda: {"champs": set(), "fonctions": set(), "objets": set(), "appels": 0})
    for ligne in releve.read_text(encoding="utf-8", errors="replace").splitlines():
        if ligne.startswith("#") or not ligne.strip():
            continue
        m = ligne.split()
        fn, appels, bases = m[0].upper(), int(m[1]), [int(x, 16) for x in m[4:]]
        champs = par_fonction.get(fn)
        if not champs or not bases:
            continue
        for b in bases:
            t = tas[b >> 16 << 16]
            t["champs"] |= champs
            t["fonctions"].add(fn)
            t["objets"].add(b)
            t["appels"] += appels
    familles = sorted(tas.items(), key=lambda kv: -len(kv[1]["champs"]))
    print(f"{len(familles)} zones de mémoire portant des objets reconnus :")
    for base, t in familles[:14]:
        champs = sorted(t["champs"])
        ecarts = sorted(t["objets"])
        pas = min((b - a for a, b in zip(ecarts, ecarts[1:])), default=0)
        print(f"   0x{base:08X} : {len(t['objets'])} objets (le plus petit écart {pas} octets), "
              f"{len(t['fonctions'])} fonctions, {len(champs)} champs")
        print("        champs : " + ", ".join(f"+0x{o:X}({t2})" for o, t2 in champs[:16])
              + (" …" if len(champs) > 16 else ""))


SYMBOLES = [(8, "x"), (2, "p"), (4, "n"), (16, "?"), (1, ".")]


def forme_du_type(membres):
    """Ce que contiennent les quarante-huit premiers mots, vu sur tous les objets du type.

    x une longueur du monde (20.12, avec des décimales : position, vitesse, angle)
    p un pointeur     n un petit entier     . toujours nul     ? autre chose
    """
    votes = [defaultdict(int) for _ in range(48)]
    for _, _, forme in membres:
        for i, ch in enumerate(forme[:48]):
            if ch != " ":
                votes[i][ch] += 1
    if not any(votes):
        return ""
    sortie = []
    for i, v in enumerate(votes):
        if i and i % 4 == 0:
            sortie.append(" ")
        if not v:
            sortie.append(" ")
            continue
        lettre, combien = max(v.items(), key=lambda kv: kv[1])
        # ce que la plupart des objets de ce type portent là ; sinon, on ne conclut pas
        sortie.append(lettre if combien * 2 > sum(v.values()) else "?")
    return "".join(sortie) + "   (+0x00 à +0xBC, par mots de quatre)"


def releves_disponibles(releve):
    """Tous les relevés d'exécution : celui qu'on nomme, et ceux d'analysis/releves/.

    Une scène ne montre que ses objets — le vol ignore les menus, et l'inverse. On les fond donc.
    """
    trouves = [f for f in sorted((RACINE / "analysis" / "releves").glob("*_par_objet.txt"))]
    if releve.exists():
        trouves.append(releve)
    return trouves


def dictionnaire(fonctions, releve=RACINE / "analysis" / "objets_par_objet.txt"):
    """Les types du jeu : des objets que les mêmes fonctions manipulent, avec leurs champs.

    Deux objets sont du même type quand les mêmes fonctions les reçoivent. On groupe d'abord sur
    l'ensemble exact, puis on fond ensemble les groupes qui partagent plus de la moitié de leurs
    fonctions — un objet n'est pas toujours passé partout. La taille d'un type se lit à l'écart
    le plus fréquent entre deux de ses objets voisins.
    """
    fichiers = releves_disponibles(releve)
    if not fichiers:
        print("pas de relevé : lancer le jeu avec SFC_ETUDE_OBJETS=analysis/objets.txt")
        return
    par_fonction = {}
    for nom, f in fonctions.items():
        adr = nom.split()[-1].upper()
        champs = set()
        for base, acces in f["bases"].items():
            if base == "e0" or base.startswith("e0_"):
                champs |= {(o, t) for o, t, _ in champs_d_une_base(acces)}
        if champs:
            par_fonction[adr] = champs

    objets = []
    lignes_toutes = []
    for f in fichiers:
        lignes_toutes += f.read_text(encoding="utf-8", errors="replace").splitlines()
    print(f"{len(fichiers)} relevé(s) : " + ", ".join(f.stem for f in fichiers))
    for ligne in lignes_toutes:
        if ligne.startswith("#") or not ligne.strip():
            continue
        m = ligne.split()
        gauche, _, forme = ligne.partition("|")
        m = gauche.split()
        objets.append((int(m[0], 16), int(m[1]), frozenset(x.upper() for x in m[2:]), forme.strip()))

    groupes = defaultdict(list)
    for adr, appels, fns, forme in objets:
        groupes[fns].append((adr, appels, forme))

    # fondre les groupes qui se ressemblent
    types = []
    for fns, membres in sorted(groupes.items(), key=lambda kv: -len(kv[1])):
        for t in types:
            commun = len(fns & t["fns"])
            if commun * 2 > min(len(fns), len(t["fns"])):
                t["membres"] += membres
                t["fns"] |= fns
                break
        else:
            types.append({"fns": set(fns), "membres": list(membres)})

    types.sort(key=lambda t: -len(t["membres"]))
    # le dictionnaire sur disque, pour qui veut s'en servir
    sortie = RACINE / "analysis" / "types.json"
    dehors = []
    for t in types:
        adrs = sorted(a for a, _, _ in t["membres"])
        ecarts = [b - a for a, b in zip(adrs, adrs[1:]) if 0 < b - a < 0x4000]
        champs = sorted(set().union(*(par_fonction.get(f, set()) for f in t["fns"])) or set())
        dehors.append({
            "objets": len(adrs),
            "taille": min(ecarts) if ecarts else 0,
            "premier": f"0x{adrs[0]:08X}",
            "appels": sum(n for _, n, _ in t["membres"]),
            "contenu": forme_du_type(t["membres"]),
            "champs": [{"decalage": f"0x{o:X}", "taille": t2} for o, t2 in champs],
            "fonctions": sorted(t["fns"]),
            "adresses": [f"0x{a:08X}" for a in adrs[:40]],
        })
    sortie.write_text(json.dumps(dehors, indent=1, ensure_ascii=False), encoding="utf-8")
    print(f"{len(types)} types reconnus, sur {len(objets)} objets vus -> {sortie.relative_to(RACINE)} :")
    for t in types[:16]:
        adrs = sorted(a for a, _, _ in t["membres"])
        ecarts = [b - a for a, b in zip(adrs, adrs[1:]) if 0 < b - a < 0x4000]
        taille = min(ecarts) if ecarts else 0
        champs = sorted(set().union(*(par_fonction.get(f, set()) for f in t["fns"])) or {(0, 0)})
        appels = sum(n for _, n, _ in t["membres"])
        print(f"   {len(adrs):4d} objets de ~{taille} octets vers 0x{adrs[0]:08X}, "
              f"{len(t['fns'])} fonctions, {appels} appels, {len(champs)} champs")
        print("        champs : " + ", ".join(f"+0x{o:X}" for o, _ in champs[:20])
              + (" …" if len(champs) > 20 else ""))
        print("        fonctions : " + ", ".join(sorted(t["fns"])[:6])
              + (" …" if len(t["fns"]) > 6 else ""))
        forme = forme_du_type(t["membres"])
        if forme:
            print("        contenu  : " + forme)


CONNUES = RACINE / "docs" / "reference.md"


def utile(description):
    """Une description qui dit quelque chose : pas un nombre, pas un mot isolé sorti d'un tableau."""
    d = description.strip(" *`")
    if len(d) < 8 or d.replace(" ", "").replace(" ", "").replace(",", "").isdigit():
        return False
    return len(d.split()) >= 2


def fonctions_nommees():
    """Les adresses que la documentation décrit déjà : le peu qu'on sait, mais nommé."""
    noms = {}
    texte = CONNUES.read_text(encoding="utf-8", errors="replace")
    for ligne in texte.splitlines():
        # | `0x021B3FF4` | mise à jour du volume, à chaque image |
        m = re.match(r"\|\s*\*{0,2}`?(0x02[0-9A-Fa-f]{6})`?\*{0,2}\s*\|\s*(.{4,90}?)\s*\|", ligne)
        if m and utile(m.group(2)):
            noms.setdefault(m.group(1).upper(), m.group(2).strip(" *`"))
            continue
        # **`0x021780F0`** place une cellule : …
        m = re.match(r"[-\s]*\*{0,2}`(0x02[0-9A-Fa-f]{6})`\*{0,2}\s*:?\s*(.{4,90})", ligne)
        if m and utile(m.group(2)):
            noms.setdefault(m.group(1).upper(), m.group(2).strip(" *`").rstrip("."))
    return noms


def nommer(fonctions, types_json=RACINE / "analysis" / "types.json"):
    """Ce que la documentation sait déjà dire des types trouvés.

    Un type manipulé par une fonction déjà décrite hérite de sa description : c'est le premier
    fil pour le nommer. Le reste attend qu'on l'observe.
    """
    if not types_json.exists():
        print("pas de dictionnaire : lancer d'abord --dictionnaire")
        return
    noms = fonctions_nommees()
    print(f"{len(noms)} adresses décrites dans la documentation")
    types = json.loads(types_json.read_text(encoding="utf-8"))
    nommes = 0
    for i, t in enumerate(types):
        indices = [(f, noms[f.upper()]) for f in t["fonctions"] if f.upper() in noms]
        if not indices:
            continue
        nommes += 1
        if nommes > 14:
            continue
        print(f"   type {i} : {t['objets']} objets de {t['taille']} octets vers {t['premier']}, "
              f"{len(t['champs'])} champs")
        for adr, quoi in indices[:4]:
            print(f"        {adr} — {quoi}")
    print(f"{nommes} types sur {len(types)} touchent au moins une fonction déjà décrite")


def suivi(chemin=RACINE / "analysis" / "suivi.txt"):
    """Ce que devient chaque champ d'un objet au fil des images : c'est là qu'un champ prend son nom.

    Un champ qui ne bouge jamais est une identité ou un pointeur ; un qui monte de un à chaque image
    est un compteur ; un qui varie d'un peu à chaque image et de beaucoup sur la durée est une
    grandeur du monde — et trois d'affilée qui varient ensemble sont une position.
    """
    if not chemin.exists():
        print("pas de suivi : lancer le jeu avec SFC_SUIVRE_OBJET=0xADRESSE,analysis/suivi.txt")
        return
    lignes = [l.split() for l in chemin.read_text(encoding="utf-8").splitlines() if not l.startswith("#")]
    if len(lignes) < 8:
        print("trop peu d'images relevées")
        return
    n = len(lignes[0]) - 1
    colonnes = [[int(l[i + 1], 16) for l in lignes] for i in range(n)]

    def signe(v):
        return v - (1 << 32) if v >= (1 << 31) else v

    def texte(v):
        """Quatre octets lisibles : du texte, et non la grandeur qu'on y aurait vue."""
        return all(0x20 <= (v >> (8 * i)) & 0xFF <= 0x7E for i in range(4))

    def mot(v):
        return "".join(chr((v >> (8 * i)) & 0xFF) for i in range(4))

    print(f"{len(lignes)} images relevées, {n} champs :")
    verdicts = []
    for i, col_brute in enumerate(colonnes):
        # Un objet est recyclé : il dort à zéro, sert, puis reçoit autre chose. Moyenner sur tout
        # mélange ces vies et invente des mouvements qui n'ont pas eu lieu. On ne garde que la plus
        # longue tranche d'images où le champ n'est pas nul.
        col, debut, meilleure = [], None, (0, 0, 0)
        for k, v in enumerate(col_brute + [0]):
            if v and debut is None:
                debut = k
            elif not v and debut is not None:
                if k - debut > meilleure[0]:
                    meilleure = (k - debut, debut, k)
                debut = None
        if meilleure[0] < 8:
            verdicts.append("nul, ou trop peu vivant pour conclure")
            if i < 16:
                print(f"   +0x{i*4:02X} : {verdicts[-1]}")
            continue
        col = col_brute[meilleure[1]:meilleure[2]]
        vie = f" (sur {meilleure[0]} images, à partir de la {meilleure[1]}e)"
        distinctes = len(set(col))
        if distinctes == 1:
            v = col[0]
            quoi = ("nul" if v == 0 else
                    f"constante 0x{v:08X}" + (" (un pointeur)" if 0x02000000 <= v < 0x02400000 else ""))
        elif sum(1 for v in col if texte(v)) * 2 > len(col):
            exemples = sorted({mot(v) for v in col if texte(v)})
            quoi = f"du texte : {', '.join(repr(e) for e in exemples[:4])}" + (" …" if len(exemples) > 4 else "")
        else:
            vals = [signe(v) for v in col]
            pas = [b - a for a, b in zip(vals, vals[1:])]
            monte = all(0 <= d <= 2 for d in pas) and vals[-1] > vals[0]
            bouge = sum(1 for d in pas if d) / max(1, len(pas))
            moyen = sum(abs(d) for d in pas) / max(1, len(pas))
            if monte:
                quoi = f"compteur (+{(vals[-1] - vals[0]) / max(1, len(vals) - 1):.2f} par image)"
            elif all(abs(v) < 0x10000 for v in vals):
                quoi = f"petit nombre, {distinctes} valeurs (change {bouge*100:.0f} % des images)"
            else:
                quoi = (f"grandeur du monde : {vals[0]/4096:.1f} -> {vals[-1]/4096:.1f}, "
                        f"{moyen/4096:.2f} par image")
        verdicts.append(quoi + vie)
        if distinctes > 1 or i < 16:
            print(f"   +0x{i*4:02X} : {verdicts[-1]}")

    # trois champs d'affilée qui bougent du même ordre : une position, une vitesse, un angle
    amplitude = []
    for col in colonnes:
        vals = [signe(v) for v in col if v]
        pas = [abs(b - a) for a, b in zip(vals, vals[1:])]
        amplitude.append(sum(pas) / max(1, len(pas)) if len(vals) > 8 else 0)
    pris = set()
    triplets = []
    for i in range(n - 2):
        a3 = amplitude[i:i + 3]
        if min(a3) < 4096 / 8:                       # trop immobile pour conclure
            continue
        if max(a3) > min(a3) * 4:                    # des ordres trop différents : pas un même vecteur
            continue
        if not all("grandeur du monde" in verdicts[i + k] for k in range(3)):
            continue
        triplets.append((sum(a3), i))
    for _, i in sorted(triplets, reverse=True):
        if pris & {i, i + 1, i + 2}:
            continue
        pris |= {i, i + 1, i + 2}
        m = sum(amplitude[i:i + 3]) / 3 / 4096
        print(f"   -> +0x{i*4:02X}, +0x{(i+1)*4:02X}, +0x{(i+2)*4:02X} bougent ensemble de {m:.1f} "
              f"par image : vraisemblablement un vecteur du monde (position, vitesse ou angle)")


def document(fonctions):
    """Écrit docs/dictionnaire.md : ce que le projet sait des objets du jeu, sous une forme lisible."""
    types_json = RACINE / "analysis" / "types.json"
    if not types_json.exists():
        print("lancer d'abord --dictionnaire")
        return
    types = json.loads(types_json.read_text(encoding="utf-8"))
    noms = fonctions_nommees()
    lignes = [
        "# Le dictionnaire des objets du jeu",
        "",
        "Ce document est **produit** par `python tools/structures.py --document`, à partir de",
        "l'inventaire des accès du code réécrit et des relevés d'exécution (`SFC_ETUDE_OBJETS`).",
        "Il ne remplace pas `reference.md`, qui porte ce qu'on a compris ; il porte ce qu'on a mesuré.",
        "",
        "Un **type** réunit les objets que les mêmes fonctions manipulent. Sa **taille** se lit à l'écart",
        "le plus petit entre deux de ses objets. Sa **forme** dit ce que contiennent ses quarante-huit",
        "premiers mots, vus de loin en loin pendant le jeu :",
        "",
        "| lettre | ce que le champ porte |",
        "| --- | --- |",
        "| `p` | un pointeur vers la mémoire vive |",
        "| `x` | une grandeur du monde (20.12, avec des décimales : position, vitesse, angle) |",
        "| `n` | un petit entier |",
        "| `t` | du texte |",
        "| `.` | toujours nul |",
        "| `?` | de tout : rien ne domine |",
        "",
        "Trois pièges à garder en tête en le lisant : un objet a plusieurs vies (la mémoire est",
        "recyclée), un tampon de travail n'est pas une structure, et ce document ne voit que les objets",
        "passés en premier argument d'une fonction.",
        "",
        f"État : **{len(types)} types**, sur les scènes relevées (un vol et une partie complète).",
        "",
    ]
    montres = [t for t in types if t["objets"] >= 3 and t["taille"] >= 8][:60]
    lignes += ["## Les types les plus employés", ""]
    for i, t in enumerate(montres):
        decrites = [(f, noms[f.upper()]) for f in t["fonctions"] if f.upper() in noms]
        titre = f"### Type {i} — {t['objets']} objets de {t['taille']} octets"
        lignes += [titre, ""]
        lignes += [f"Premier vu à `{t['premier']}`, {len(t['fonctions'])} fonctions, "
                   f"{t['appels']} appels relevés.", ""]
        if t.get("contenu"):
            lignes += ["```", t["contenu"].replace("   (+0x00 à +0xBC, par mots de quatre)", ""),
                       "```", "", "*(les quarante-huit premiers mots, de `+0x00` à `+0xBC`)*", ""]
        if decrites:
            lignes += ["Ce que la documentation dit de ses fonctions :", ""]
            for adr, quoi in decrites[:5]:
                lignes.append(f"- `{adr.replace('0X', '0x')}` — {quoi}")
            lignes.append("")
        lignes += ["Fonctions : " + ", ".join(f"`{f.replace('0X', '0x')}`" for f in t["fonctions"][:12])
                   + (" …" if len(t["fonctions"]) > 12 else ""), ""]
    sortie = RACINE / "docs" / "dictionnaire.md"
    sortie.write_text("\n".join(lignes) + "\n", encoding="utf-8")
    print(f"{len(montres)} types écrits -> {sortie.relative_to(RACINE)}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--adresse", help="qui touche à cet objet (0x021D1E94)")
    ap.add_argument("--fonction", help="ce que cette fonction touche (0x02178320)")
    ap.add_argument("--types", action="store_true", help="les bases qui se ressemblent")
    ap.add_argument("--objets", action="store_true", help="croise avec le relevé d'exécution (SFC_ETUDE_OBJETS)")
    ap.add_argument("--dictionnaire", action="store_true", help="les types du jeu : objets groupés par les fonctions qui les manipulent")
    ap.add_argument("--nommer", action="store_true", help="ce que la documentation sait déjà dire des types trouvés")
    ap.add_argument("--suivi", action="store_true", help="ce que devient chaque champ d'un objet suivi (SFC_SUIVRE_OBJET)")
    ap.add_argument("--document", action="store_true", help="écrit docs/dictionnaire.md")
    a = ap.parse_args()
    if a.suivi:
        suivi()
        return
    fonctions = lire()
    if a.adresse:
        qui_touche(fonctions, int(a.adresse, 0))
    elif a.fonction:
        ce_que_touche(fonctions, a.fonction)
    elif a.types:
        familles(fonctions)
    elif a.objets:
        objets(fonctions)
    elif a.dictionnaire:
        dictionnaire(fonctions)
    elif a.nommer:
        nommer(fonctions)
    elif a.suivi:
        suivi()
    elif a.document:
        document(fonctions)
    else:
        ecrire_inventaire(fonctions)


if __name__ == "__main__":
    main()

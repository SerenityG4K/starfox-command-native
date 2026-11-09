#!/usr/bin/env python3
"""Extrait les dialogues des archives de messages `.bmg` (format Nintendo).

Structure d'un fichier :

    en-tête (32 octets) : magie « MESGbmg1 », taille du fichier, nombre de
                          sections, encodage (1 CP1252, 2 UTF-16, 3 Shift-JIS,
                          4 UTF-8)
    INF1 : table des messages — nombre, taille d'entrée, puis une entrée par
           message dont les 4 premiers octets sont l'offset dans DAT1
    DAT1 : les chaînes, terminées par un caractère nul

Les fichiers de langue de la ROM (sous `_srl/<Langue>/2D/MessageText/`) sont
petit-boutistes ; le `corneria.bmg` de la racine, vestige d'outils GameCube,
est gros-boutiste. Les deux sont gérés, le boutisme étant détecté sur le
champ de taille.

Les séquences de contrôle (couleur, pause, nom du joueur…) commencent par le
caractère 0x1A suivi de leur longueur ; elles sont conservées sous la forme
`{groupe:type:arguments}` pour ne rien perdre.

L'en-tête d'INF1 fait 16 octets (étiquette, taille, nombre, taille d'entrée,
numéro du fichier, couleur) : les entrées suivent. Un message se désigne par
son rang dans INF1, à partir de 0.

Usage :
    python tools/bmg.py assets/cooked -o assets/text          tout extraire, en JSON
    python tools/bmg.py --texte battle_Fr.bmg -o battle_Fr.json
    python tools/bmg.py --reecrire battle_Fr.bmg battle_Fr.json -o battle_Fr_neuf.bmg

`--reecrire` part du fichier d'origine et y remplace les messages du JSON dont le
texte a changé ; ceux qui n'y figurent pas restent tels quels. Le texte nouveau est
ajouté à la fin de DAT1, et l'entrée du message le désigne : tout le reste du
fichier est gardé octet pour octet.
"""
from __future__ import annotations

import argparse
import json
import re
import struct
from collections import Counter
from pathlib import Path

ENCODINGS = {1: "cp1252", 2: "utf-16", 3: "shift_jis", 4: "utf-8"}


def parse(data: bytes) -> tuple[list[str], dict]:
    if data[:8] != b"MESGbmg1":
        raise ValueError("magie MESGbmg1 absente")
    endian = "<" if struct.unpack_from("<I", data, 8)[0] == len(data) else ">"
    size, section_count = struct.unpack_from(endian + "II", data, 8)
    encoding_id = data[16]

    sections: dict[str, tuple[int, int]] = {}
    pos = 32
    for _ in range(section_count):
        if pos + 8 > len(data):
            break
        tag = data[pos:pos + 4].decode("ascii", "replace")
        section_size = struct.unpack_from(endian + "I", data, pos + 4)[0]
        sections[tag] = (pos, section_size)
        if section_size < 8:
            break
        pos += section_size

    if "INF1" not in sections or "DAT1" not in sections:
        raise ValueError("sections INF1 / DAT1 absentes")

    inf_pos, _ = sections["INF1"]
    count, entry_size = struct.unpack_from(endian + "HH", data, inf_pos + 8)
    dat_pos, dat_size = sections["DAT1"]
    base = dat_pos + 8
    end = dat_pos + dat_size

    wide = encoding_id == 2
    codec = ENCODINGS.get(encoding_id, "utf-16")
    if wide:
        codec = "utf-16-le" if endian == "<" else "utf-16-be"
    unit = 2 if wide else 1

    def read_char(at: int) -> int:
        if wide:
            return struct.unpack_from(endian + "H", data, at)[0]
        return data[at]

    def decode(offset: int) -> str:
        out: list[str] = []
        i = base + offset
        raw = bytearray()

        def flush() -> None:
            if raw:
                out.append(raw.decode(codec, "replace"))
                raw.clear()

        while i + unit <= end:
            ch = read_char(i)
            if ch == 0:
                break
            if ch == 0x1A:
                # Séquence de contrôle : longueur totale (en octets, à partir
                # du 0x1A), groupe, type, puis arguments.
                flush()
                length = data[i + unit] if i + unit < end else 0
                if length < unit + 1:
                    break
                body = data[i + unit + 1:i + length]
                group = body[0] if body else 0
                kind = struct.unpack_from(endian + "H", body, 1)[0] if len(body) >= 3 else 0
                args = body[3:].hex()
                out.append(f"{{{group}:{kind}:{args}}}" if args else f"{{{group}:{kind}}}")
                i += length
                continue
            raw += data[i:i + unit]
            i += unit
        flush()
        return "".join(out)

    messages: list[str] = []
    for k in range(count):
        entry = inf_pos + 16 + k * entry_size
        if entry + 4 > len(data):
            break
        offset = struct.unpack_from(endian + "I", data, entry)[0]
        messages.append(decode(offset))

    info = {"boutisme": "little" if endian == "<" else "big", "encodage": codec,
            "entrees": count, "taille_entree": entry_size, "taille": size}
    return messages, info


CONTROLE = re.compile(r"\{(\d+):(\d+)(?::([0-9a-fA-F]*))?\}")


def encoder(texte: str, codec: str, endian: str, wide: bool) -> bytes:
    """Un message au format de DAT1, terminé : le texte dans l'encodage du fichier, les séquences
    `{groupe:type:arguments}` redevenues 0x1A, longueur, groupe, type, arguments."""
    unit = 2 if wide else 1
    out = bytearray()
    pos = 0
    for m in CONTROLE.finditer(texte):
        out += texte[pos:m.start()].encode(codec)
        args = bytes.fromhex(m.group(3) or "")
        out += struct.pack(endian + "H", 0x1A) if wide else b"\x1A"
        out.append(unit + 4 + len(args))
        out.append(int(m.group(1)))
        out += struct.pack(endian + "H", int(m.group(2)))
        out += args
        pos = m.end()
    out += texte[pos:].encode(codec)
    out += bytes(unit)
    return bytes(out)


def reecrire(data: bytes, nouveaux: dict[int, str]) -> bytes:
    """Le fichier, les messages de `nouveaux` remplacés quand leur texte a changé."""
    anciens, info = parse(data)
    endian = "<" if info["boutisme"] == "little" else ">"
    wide = info["encodage"].startswith("utf-16")
    unit = 2 if wide else 1
    sections: list[tuple[bytes, int, int]] = []
    pos = 32
    for _ in range(struct.unpack_from(endian + "I", data, 12)[0]):
        taille = struct.unpack_from(endian + "I", data, pos + 4)[0]
        sections.append((data[pos:pos + 4], pos, taille))
        pos += taille
    inf_pos = next(p for t, p, _ in sections if t == b"INF1")
    entree = struct.unpack_from(endian + "H", data, inf_pos + 10)[0]
    dat_pos, dat_taille = next((p, t) for tag, p, t in sections if tag == b"DAT1")
    inf1 = bytearray(data[inf_pos:inf_pos + next(t for tag, _, t in sections if tag == b"INF1")])
    dat1 = bytearray(data[dat_pos:dat_pos + dat_taille])
    changes = 0
    for k, texte in sorted(nouveaux.items()):
        if not 0 <= k < len(anciens):
            raise ValueError(f"message {k} : le fichier en a {len(anciens)}")
        if texte == anciens[k]:
            continue
        while (len(dat1) - 8) % unit:
            dat1.append(0)
        struct.pack_into(endian + "I", inf1, 16 + k * entree, len(dat1) - 8)
        dat1 += encoder(texte, info["encodage"], endian, wide)
        changes += 1
    while len(dat1) % 32:
        dat1.append(0)
    struct.pack_into(endian + "I", dat1, 4, len(dat1))
    out = bytearray(data[:32])
    for tag, p, t in sections:
        out += inf1 if tag == b"INF1" else dat1 if tag == b"DAT1" else data[p:p + t]
    struct.pack_into(endian + "I", out, 8, len(out))
    print(f"{changes} message(s) remplacé(s)")
    return bytes(out)


def vers_json(chemin: Path, data: bytes) -> dict:
    messages, info = parse(data)
    return {"source": chemin.name, **info,
            "messages": [{"id": i, "texte": m} for i, m in enumerate(messages) if m.strip()]}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("root", nargs="?", help="racine des assets décompressés")
    ap.add_argument("-o", "--out", default=None)
    ap.add_argument("--texte", metavar="BMG", help="un seul fichier, en JSON à modifier")
    ap.add_argument("--reecrire", nargs=2, metavar=("BMG", "JSON"),
                    help="le fichier d'origine, les messages du JSON remplacés")
    args = ap.parse_args()

    if args.texte:
        chemin = Path(args.texte)
        sortie = Path(args.out) if args.out else chemin.with_suffix(".json")
        sortie.write_text(json.dumps(vers_json(chemin, chemin.read_bytes()), indent=1, ensure_ascii=False),
                          encoding="utf-8")
        print(f"{chemin} -> {sortie}")
        return 0
    if args.reecrire:
        bmg, js = Path(args.reecrire[0]), Path(args.reecrire[1])
        nouveaux = {m["id"]: m["texte"] for m in json.loads(js.read_text(encoding="utf-8"))["messages"]}
        sortie = Path(args.out) if args.out else bmg.with_name(bmg.stem + "_neuf.bmg")
        sortie.write_bytes(reecrire(bmg.read_bytes(), nouveaux))
        print(f"-> {sortie}")
        return 0
    if not args.root:
        ap.error("donner la racine des assets, ou --texte / --reecrire")
    root = Path(args.root)
    out = Path(args.out or "assets/text")
    out.mkdir(parents=True, exist_ok=True)

    per_language: Counter[str] = Counter()
    summary: list[dict] = []
    failures: list[str] = []

    for path in sorted(root.rglob("*.bmg")):
        rel = path.relative_to(root)
        try:
            messages, info = parse(path.read_bytes())
        except Exception as exc:  # noqa: BLE001
            failures.append(f"{rel.as_posix()} : {exc}")
            continue

        # `_srl/Fr/2D/MessageText/scenario_Fr.bmg` -> langue Fr
        language = rel.parts[1] if rel.parts[0] == "_srl" and len(rel.parts) > 1 else "racine"
        non_empty = sum(1 for m in messages if m.strip())
        per_language[language] += non_empty

        # Une archive peut ranger le même nom deux fois — `Fr_child` a `2D/MessageText/battle_Fr.bmg` et
        # `Fr/2D/MessageText/battle_Fr.bmg` : ce qui précède `2D/` garde les deux copies à part.
        interne = rel.parts[2:] if language != "racine" else rel.parts
        avant = interne[:interne.index("2D")] if "2D" in interne else ()
        dest = out.joinpath(language, *avant, path.stem + ".json")
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(json.dumps(
            {"source": rel.as_posix(), **info,
             "messages": [{"id": i, "texte": m} for i, m in enumerate(messages) if m.strip()]},
            indent=1, ensure_ascii=False), encoding="utf-8")

        summary.append({"fichier": rel.as_posix(), "langue": language,
                        "messages": non_empty, "entrees": info["entrees"],
                        "boutisme": info["boutisme"], "encodage": info["encodage"]})
        print(f"  {rel.as_posix():<52} {non_empty:>5} messages  ({info['boutisme']}, {info['encodage']})")

    print(f"\n{len(summary)} archive(s), {sum(per_language.values())} messages non vides")
    for language, n in per_language.most_common():
        print(f"  {language:<10} {n:>6}")
    if failures:
        print(f"\n{len(failures)} échec(s) :")
        for f in failures:
            print("  ", f)

    (out / "index.json").write_text(json.dumps(summary, indent=1, ensure_ascii=False),
                                    encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

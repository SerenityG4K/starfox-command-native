#!/usr/bin/env python3
"""Recense le vocabulaire du langage de script de Star Fox Command.

Les scripts sont des fichiers texte Shift-JIS, une commande par ligne :
    Commande arg1 arg2 <vec.x vec.y vec.z> ...
« # » commence un commentaire (souvent une ligne désactivée).

Produit un inventaire des commandes, de leur arité, de leurs occurrences et
d'exemples réels — la spécification de facto à réimplémenter dans le moteur.

Usage :
    python tools/analyze_scripts.py assets/cooked -o docs/script_reference.md
"""
from __future__ import annotations

import argparse
import json
import re
from collections import Counter, defaultdict
from pathlib import Path

TOKEN_RE = re.compile(r"<[^>]*>|\"[^\"]*\"|\S+")


def classify(token: str) -> str:
    """Étiquette grossière d'un argument, pour deviner les signatures."""
    if token.startswith("<"):
        return "VEC"
    if token.startswith('"'):
        return "STR"
    if re.fullmatch(r"[-+]?0[xX][0-9a-fA-F]+", token):
        return "HEX"
    if re.fullmatch(r"[-+]?\d+", token):
        return "INT"
    if re.fullmatch(r"[-+]?\d*\.\d+", token):
        return "FLOAT"
    if "/" in token and "." in token:
        return "PATH"
    return "IDENT"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("root", help="racine des assets décompressés")
    ap.add_argument("-o", "--out", default="docs/script_reference.md")
    ap.add_argument("--json", default="docs/script_commands.json")
    args = ap.parse_args()

    root = Path(args.root)
    files = [p for p in root.rglob("*.txt") if p.is_file()]

    counts: Counter[str] = Counter()
    commented: Counter[str] = Counter()
    signatures: defaultdict[str, Counter[str]] = defaultdict(Counter)
    examples: defaultdict[str, list[str]] = defaultdict(list)
    files_using: defaultdict[str, set[str]] = defaultdict(set)
    total_lines = 0

    for path in files:
        raw = path.read_bytes()
        text = raw.decode("shift_jis", "replace")
        rel = path.relative_to(root).as_posix()
        for line in text.splitlines():
            line = line.strip()
            if not line:
                continue
            total_lines += 1
            is_comment = line.startswith("#")
            probe = line.lstrip("#").strip() if is_comment else line
            if not probe:
                continue
            tokens = TOKEN_RE.findall(probe)
            if not tokens:
                continue
            cmd = tokens[0]
            # Un commentaire n'est une commande désactivée que s'il ressemble à
            # un identifiant CamelCase suivi d'arguments.
            if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", cmd):
                continue
            if is_comment:
                if not re.match(r"^[A-Z][a-z]", cmd) or len(tokens) < 2:
                    continue
                commented[cmd] += 1
            else:
                counts[cmd] += 1
                files_using[cmd].add(rel)
                sig = " ".join(classify(t) for t in tokens[1:])
                signatures[cmd][sig] += 1
                if len(examples[cmd]) < 3 and len(probe) < 160:
                    examples[cmd].append(probe)

    print(f"{len(files)} scripts, {total_lines} lignes non vides")
    print(f"{len(counts)} commandes actives distinctes "
          f"({sum(counts.values())} appels)")
    print(f"{len(set(commented) - set(counts))} commandes vues uniquement en commentaire\n")

    print(f"{'COMMANDE':<34}{'APPELS':>8}{'FICHIERS':>10}  SIGNATURE DOMINANTE")
    print("-" * 100)
    for cmd, n in counts.most_common():
        sig, _ = signatures[cmd].most_common(1)[0]
        print(f"{cmd:<34}{n:>8}{len(files_using[cmd]):>10}  {sig[:44]}")

    # ------------------------------------------------------------- documents
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "# Référence du langage de script — Star Fox Command",
        "",
        "Généré par `tools/analyze_scripts.py` à partir des scripts extraits de la ROM.",
        "",
        f"- **{len(files)}** fichiers de script analysés",
        f"- **{len(counts)}** commandes distinctes, **{sum(counts.values())}** appels",
        "",
        "Les scripts sont encodés en **Shift-JIS**. Une commande par ligne ;",
        "`#` introduit un commentaire (fréquemment une ligne désactivée).",
        "Les vecteurs s'écrivent `<x y z>`.",
        "",
        "## Commandes",
        "",
        "| Commande | Appels | Fichiers | Signature(s) |",
        "|---|---:|---:|---|",
    ]
    for cmd, n in counts.most_common():
        sigs = "<br>".join(
            f"`{s or '(aucun argument)'}` ×{c}"
            for s, c in signatures[cmd].most_common(3))
        lines.append(f"| `{cmd}` | {n} | {len(files_using[cmd])} | {sigs} |")

    lines += ["", "## Exemples par commande", ""]
    for cmd, _ in counts.most_common():
        lines.append(f"### `{cmd}`")
        lines.append("")
        lines.append("```")
        lines.extend(examples[cmd])
        lines.append("```")
        lines.append("")

    orphans = sorted(set(commented) - set(counts))
    if orphans:
        lines += ["## Commandes vues uniquement en commentaire", "",
                  "Vestiges de développement : probablement supportées par le moteur "
                  "d'origine mais inutilisées dans la version finale.", ""]
        lines += [f"- `{c}` (×{commented[c]})" for c in orphans]
        lines.append("")

    out.write_text("\n".join(lines), encoding="utf-8")

    Path(args.json).parent.mkdir(parents=True, exist_ok=True)
    Path(args.json).write_text(json.dumps({
        "commandes": {
            cmd: {
                "appels": n,
                "fichiers": len(files_using[cmd]),
                "signatures": dict(signatures[cmd]),
                "exemples": examples[cmd],
            } for cmd, n in counts.most_common()
        },
        "commentees_seulement": {c: commented[c] for c in orphans},
    }, indent=1, ensure_ascii=False), encoding="utf-8")

    print(f"\nÉcrit : {out} et {args.json}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

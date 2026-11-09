#!/usr/bin/env python3
"""Préparer le jeu : de la cartouche du joueur à sfc.exe, sur sa machine.

Le portage ne distribue rien du jeu — ni ses données, ni son code, sous aucune forme. Ce que le joueur
reçoit, ce sont des outils et une couche d'exécution ; ce programme les fait travailler sur **sa** copie
de la cartouche, une fois, et laisse le jeu construit chez lui :

  1. la cartouche est reconnue : la version européenne, octet pour octet (une copie réduite à ses données
     utiles — « trimmed » — convient aussi) ;
  2. elle est découpée en fichiers dans le dossier de préparation (tools/build_data.py) ;
  3. le code des deux processeurs est traduit en C (tools/recomp/recomp.py), d'après la carte du code
     livrée dans tools/carte/ — des adresses seulement : où commence chaque fonction, ses blocs, ses
     appels, ses tables de sauts ; pas un octet du jeu ;
  4. l'annuaire des noms que le binaire donne de lui-même (tools/symboles.py) ;
  5. chaque fonction est réécrite en C natif (tools/decomp_feuilles.py), points de modification compris ;
  6. le tout est compilé (CMake, GCC) en sfc.exe, posé dans jeu/, avec pour icône l'emblème de la cartouche
     (tools/icone.py) ; le dossier de préparation est effacé.

Ce qui en sort reste chez le joueur : le code généré, et le jeu construit avec, sont pour son usage
personnel — ils ne se partagent pas plus que la cartouche.

Le lanceur lance ce programme et lit sa sortie : une ligne « ÉTAPE k/n : … » à chaque étape,
« AVANCE p » (pour cent, pendant la compilation), « PRÊT » à la fin, ou « ERREUR … » et un code de sortie
non nul. Ces mots-clés ne changent pas ; ce qui les suit se dit en anglais, en français ou en japonais
(--langue en|fr|ja, l'anglais par défaut : le lanceur passe la sienne). Les journaux de chaque étape restent
dans le dossier de préparation quand une étape échoue.

Outils nécessaires : Python 3.10 ou plus récent (rien d'autre que sa bibliothèque standard), CMake, et
GCC pour Windows (MinGW-w64, avec mingw32-make ou Ninja). Ils sont cherchés dans outils/ à côté du
dépôt (outils/python, outils/mingw64/bin, outils/cmake/bin), dans SFC_OUTILS, puis dans le PATH.

Usage :
    python tools/preparer.py "<cartouche>.nds" [--langue en|fr|ja] [--jeu jeu] [--dossier preparation] [--coeurs N]
                             [--garder]

Sous licence MIT.
"""
from __future__ import annotations

import argparse
import gzip
import hashlib
import importlib.util
import os
import re
import shutil
import struct
import subprocess
import sys
import time
from pathlib import Path

RACINE = Path(__file__).resolve().parent.parent
VERSION = "1.2"

# La cartouche sur laquelle le portage a été construit : Star Fox Command (Europe) (En,Fr,De,Es,It),
# code de jeu ASFP. L'empreinte porte sur ses données utiles (la taille écrite en 0x80 de l'en-tête) :
# une copie complète (64 Mo, du remplissage à zéro au-delà) et une copie réduite donnent la même.
CODE_DE_JEU = b"ASFP"
TAILLE_UTILE = 0x0393467C
SHA1_UTILE = "16abd9f61c0a34d76a1800511457b32ac663cd57"


# Ce que la préparation dit, dans les trois langues du lanceur.
MESSAGES = {
    "petit": {"en": "{nom} is too small to be a ROM.",
              "fr": "{nom} est trop petit pour être une cartouche.",
              "ja": "{nom} はROMとしては小さすぎます。"},
    "version": {"en": "{nom} is not Star Fox Command (Europe): its game code is {code}, and the port was built on "
                      "version ASFP. Another version would not work.",
                "fr": "{nom} n'est pas Star Fox Command (Europe) : son code de jeu est {code}, le portage a été "
                      "construit sur la version ASFP. Une autre version ne marcherait pas.",
                "ja": "{nom} は「スターフォックス コマンド」欧州版ではありません（ゲームコード：{code}）。この移植版は "
                      "ASFP 版を元に作られており、他のバージョンでは動作しません。"},
    "tronquee": {"en": "{nom} is truncated: {taille} bytes, at least {utile} are needed.",
                 "fr": "{nom} est tronquée : {taille} octets, il en faut au moins {utile}.",
                 "ja": "{nom} は途中で切れています（{taille} バイト。{utile} バイト以上必要です）。"},
    "illisible": {"en": "the ROM cannot be read: {e}",
                  "fr": "la cartouche ne se lit pas : {e}",
                  "ja": "ROMを読み込めません：{e}"},
    "forcee": {"en": "  (fingerprint differs from the expected ROM: going on anyway, --forcer)",
               "fr": "  (empreinte différente de la cartouche attendue : on continue quand même, --forcer)",
               "ja": "  （ROMの指紋が想定と異なりますが、--forcer により続行します）"},
    "differente": {"en": "this is the European version, but its contents differ from the original ROM (modified, "
                         "translated, patched or damaged copy). An intact copy is needed.",
                   "fr": "c'est bien la version européenne, mais son contenu diffère de la cartouche d'origine "
                         "(copie modifiée, traduite, patchée ou abîmée). Il faut une copie intacte.",
                   "ja": "欧州版ですが、内容が元のROMと異なります（改造・翻訳・パッチ適用済み、または破損したコピー）。"
                         "無改造のコピーが必要です。"},
    "outils": {"en": "tools are missing to build the game: {manque}. They ship in outils/, or can be installed "
                     "separately (MinGW-w64, CMake).",
               "fr": "il manque des outils pour construire le jeu : {manque}. Ils sont livrés dans outils/, ou "
                     "s'installent à part (MinGW-w64, CMake).",
               "ja": "ゲームの構築に必要なツールがありません：{manque}。outils/ に同梱されています。別途インストール"
                     "することもできます（MinGW-w64、CMake）。"},
    "demarrage": {"en": "step “{nom}” could not start {programme}: {e}",
                  "fr": "l'étape « {nom} » n'a pas pu démarrer {programme} : {e}",
                  "ja": "「{nom}」で {programme} を起動できませんでした：{e}"},
    "echec": {"en": "step “{nom}” failed (code {code}); its log: {journal}",
              "fr": "l'étape « {nom} » a échoué (code {code}) ; son journal : {journal}",
              "ja": "「{nom}」に失敗しました（コード {code}）。ログ：{journal}"},
    "sans_icone": {"en": "sfc.exe keeps the Windows icon: the ROM's emblem could not be read",
                   "fr": "sfc.exe garde l'icône de Windows : l'emblème de la cartouche ne se lit pas",
                   "ja": "ROMのエンブレムを読み込めないため、sfc.exe はWindowsのアイコンのままです"},
    "python": {"en": "Python 3.10 or newer is needed.",
               "fr": "il faut Python 3.10 ou plus récent.",
               "ja": "Python 3.10 以降が必要です。"},
    "etape1": {"en": "the ROM", "fr": "la cartouche", "ja": "ROMの確認"},
    "etape2": {"en": "the ROM split into files", "fr": "la cartouche découpée en fichiers", "ja": "ROMをファイルに展開"},
    "etape3": {"en": "both processors' code translated to C", "fr": "le code des deux processeurs traduit en C",
               "ja": "2つのプロセッサのコードをCに変換"},
    "etape4": {"en": "the names the binary gives itself", "fr": "les noms que le binaire se donne",
               "ja": "バイナリ内の名前を収集"},
    "etape5": {"en": "each function rewritten in native C", "fr": "chaque fonction réécrite en C natif",
               "ja": "各関数をネイティブなCに書き直し"},
    "etape6": {"en": "compiling (the longest step)", "fr": "la compilation (la plus longue)",
               "ja": "コンパイル（最も時間がかかります）"},
    "decoupage": {"en": "splitting", "fr": "découpage", "ja": "展開"},
    "traduction": {"en": "translation", "fr": "traduction", "ja": "変換"},
    "traduction7": {"en": "coprocessor translation", "fr": "traduction du coprocesseur", "ja": "コプロセッサの変換"},
    "annuaire": {"en": "names", "fr": "annuaire", "ja": "名前の収集"},
    "reecriture": {"en": "rewriting", "fr": "réécriture", "ja": "書き直し"},
    "reecriture7": {"en": "coprocessor rewriting", "fr": "réécriture du coprocesseur", "ja": "コプロセッサの書き直し"},
    "configuration": {"en": "configuration", "fr": "configuration", "ja": "構成"},
    "compilation": {"en": "compilation", "fr": "compilation", "ja": "コンパイル"},
}
LANGUE = "en"


def msg(cle: str, **valeurs) -> str:
    textes = MESSAGES[cle]
    return textes.get(LANGUE, textes["en"]).format(**valeurs)


def dire(ligne: str) -> None:
    print(ligne, flush=True)


def echouer(message: str) -> None:
    dire(f"ERREUR {message}")
    raise SystemExit(1)


def verifier_cartouche(chemin: Path, forcer: bool) -> None:
    try:
        taille = chemin.stat().st_size
        with chemin.open("rb") as f:
            entete = f.read(0x200)
            if len(entete) < 0x200:
                echouer(msg("petit", nom=chemin.name))
            code = entete[0x0C:0x10]
            if code != CODE_DE_JEU:
                lisible = "".join(chr(o) if 0x20 <= o < 0x7F else "?" for o in code)
                echouer(msg("version", nom=chemin.name, code=lisible))
            if taille < TAILLE_UTILE:
                echouer(msg("tronquee", nom=chemin.name, taille=taille, utile=TAILLE_UTILE))
            f.seek(0)
            h = hashlib.sha1()
            reste = TAILLE_UTILE
            while reste:
                bloc = f.read(min(reste, 1 << 20))
                if not bloc:
                    break
                h.update(bloc)
                reste -= len(bloc)
    except OSError as e:
        echouer(msg("illisible", e=e))
    if h.hexdigest() != SHA1_UTILE:
        if forcer:
            dire(msg("forcee"))
            return
        echouer(msg("differente"))


def chercher_outils(env: dict) -> dict:
    """Place les outils livrés devant le PATH, puis vérifie qu'on a tout."""
    dossiers = []
    if env.get("SFC_OUTILS"):
        base = Path(env["SFC_OUTILS"])
        dossiers += [base / "mingw64" / "bin", base / "cmake" / "bin", base / "bin", base]
    outils = RACINE / "outils"
    dossiers += [outils / "mingw64" / "bin", outils / "cmake" / "bin"]
    dossiers = [str(d) for d in dossiers if d.is_dir()]
    if dossiers:
        env["PATH"] = os.pathsep.join(dossiers + [env.get("PATH", "")])
    trouve = {nom: shutil.which(nom, path=env["PATH"]) for nom in ("cmake", "gcc", "g++", "mingw32-make", "ninja")}
    manque = [nom for nom in ("cmake", "gcc", "g++") if not trouve[nom]]
    if not trouve["mingw32-make"] and not trouve["ninja"]:
        manque.append("mingw32-make / ninja")
    if manque:
        echouer(msg("outils", manque=", ".join(manque)))
    return trouve


def lancer(nom: str, commande: list, journal: Path, env: dict, avance=None) -> subprocess.Popen:
    """Démarre une étape ; sa sortie va dans son journal (et, ligne à ligne, à `avance` si on la suit)."""
    journal.parent.mkdir(parents=True, exist_ok=True)
    sortie = journal.open("w", encoding="utf-8", errors="replace")
    # Sous Windows, le programme se cherche dans le PATH de ce processus-ci, pas dans celui qu'on passe à
    # l'enfant : les outils livrés (outils\) se donnent donc par leur chemin complet (chercher_outils).
    try:
        p = subprocess.Popen([str(c) for c in commande], cwd=RACINE, env=env,
                             stdout=subprocess.PIPE if avance else sortie, stderr=subprocess.STDOUT,
                             text=avance is not None, encoding="utf-8" if avance else None,
                             errors="replace" if avance else None)
    except OSError as e:
        sortie.close()
        echouer(msg("demarrage", nom=msg(nom), programme=commande[0], e=e))
    p.nom, p.journal, p.fichier, p.avance = nom, journal, sortie, avance
    return p


def attendre(*processus: subprocess.Popen) -> None:
    for p in processus:
        if p.avance:
            for ligne in p.stdout:
                p.fichier.write(ligne)
                p.avance(ligne)
        p.wait()
        p.fichier.close()
    for p in processus:
        if p.returncode:
            fin = p.journal.read_text(encoding="utf-8", errors="replace").splitlines()[-25:]
            for ligne in fin:
                dire(f"  | {ligne}")
            echouer(msg("echec", nom=msg(p.nom), code=p.returncode, journal=p.journal))


def main() -> int:
    for flux in (sys.stdout, sys.stderr):
        try:
            flux.reconfigure(encoding="utf-8")
        except (AttributeError, ValueError):
            pass
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cartouche", help="votre copie de Star Fox Command (Europe), un fichier .nds")
    ap.add_argument("--jeu", default=str(RACINE / "jeu"), help="où poser sfc.exe (défaut : jeu/)")
    ap.add_argument("--dossier", default=str(RACINE / "preparation"), help="dossier de travail, effacé à la fin")
    ap.add_argument("--coeurs", type=int, default=os.cpu_count() or 4, help="compilations en parallèle")
    ap.add_argument("--garder", action="store_true", help="garder le dossier de travail (code généré, journaux)")
    ap.add_argument("--forcer", action="store_true", help="accepter une cartouche dont l'empreinte diffère")
    ap.add_argument("--langue", choices=("en", "fr", "ja"), default="en",
                    help="la langue des messages (en, fr, ja ; anglais par défaut)")
    args = ap.parse_args()
    global LANGUE
    LANGUE = args.langue
    if sys.version_info < (3, 10):
        echouer(msg("python"))

    debut = time.time()
    cartouche = Path(args.cartouche).resolve()
    prep = Path(args.dossier).resolve()
    jeu = Path(args.jeu).resolve()
    env = dict(os.environ, PYTHONUTF8="1", PYTHONIOENCODING="utf-8")
    py = sys.executable
    N = 6

    dire(f"ÉTAPE 1/{N} : {msg('etape1')}")
    verifier_cartouche(cartouche, args.forcer)
    outils = chercher_outils(env)
    if prep.exists():
        shutil.rmtree(prep)
    analyse = prep / "analysis"
    analyse.mkdir(parents=True)
    # la carte du code, livrée compressée : des adresses, rien du jeu
    for nom in ("codemap.json", "codemap_arm7.json"):
        (analyse / nom).write_bytes(gzip.decompress((RACINE / "tools" / "carte" / f"{nom}.gz").read_bytes()))
    carte9, carte7 = analyse / "codemap.json", analyse / "codemap_arm7.json"
    donnees, gen9, gen7 = prep / "data", prep / "generated", prep / "generated7"

    dire(f"ÉTAPE 2/{N} : {msg('etape2')}")
    attendre(lancer("decoupage", [py, "tools/build_data.py", cartouche, "-o", donnees], prep / "journal_decoupage.txt", env))

    dire(f"ÉTAPE 3/{N} : {msg('etape3')}")
    attendre(lancer("traduction", [py, "tools/recomp/recomp.py", cartouche, "--codemap", carte9, "--out", gen9,
                                   "--regions", "all"], prep / "journal_traduction.txt", env),
             lancer("traduction7", [py, "tools/recomp/recomp.py", cartouche, "--codemap", carte7,
                                                   "--out", gen7, "--suffixe", "7"],
                    prep / "journal_traduction7.txt", env))

    dire(f"ÉTAPE 4/{N} : {msg('etape4')}")
    symboles = analyse / "symboles.json"
    attendre(lancer("annuaire", [py, "tools/symboles.py", "--data", donnees, "--codemap", carte9, "-o", symboles,
                                 "-m", analyse / "annuaire.md"], prep / "journal_annuaire.txt", env))

    dire(f"ÉTAPE 5/{N} : {msg('etape5')}")
    commun = ["--data", donnees, "--symboles", symboles]
    attendre(lancer("reecriture", [py, "tools/decomp_feuilles.py", *commun, "--codemap", carte9,
                                   "-o", gen9 / "natif_auto.inc", "--json", analyse / "feuilles.json",
                                   "--verdicts", analyse / "feuilles_verdicts.json",
                                   "--partielles", analyse / "feuilles_partielles.txt"],
                    prep / "journal_reecriture.txt", env),
             lancer("reecriture7", [py, "tools/decomp_feuilles.py", *commun, "--codemap", carte7,
                                                   "--suffixe", "7", "--parties", "2",
                                                   "-o", gen7 / "natif_auto7.inc", "--json", analyse / "feuilles7.json",
                                                   "--verdicts", analyse / "feuilles7_verdicts.json",
                                                   "--partielles", analyse / "feuilles7_partielles.txt",
                                                   "--milieux", "tools/entrees_milieu7.txt"],
                    prep / "journal_reecriture7.txt", env))
    shutil.rmtree(donnees)                  # plus rien n'en a besoin : le jeu lit la cartouche elle-même

    dire(f"ÉTAPE 6/{N} : {msg('etape6')}")
    construction = prep / "build"
    # L'icône de sfc.exe : l'emblème de la cartouche du joueur (tools/icone.py) ; sans elle, l'icône de Windows. Le
    # module se charge par son chemin : le Python embarquable livré (outils/python) n'ajoute pas le dossier du
    # script à ses chemins d'import.
    icone = prep / "icone.ico"
    try:
        spec = importlib.util.spec_from_file_location("icone", RACINE / "tools" / "icone.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        module.ecrire_icone(cartouche, icone)
        option_icone = [f"-DSFC_ICONE={icone}"]
    except (OSError, ValueError, ImportError, AttributeError) as e:
        dire(f"  ({msg('sans_icone')} : {e})")
        option_icone = []
    if outils["mingw32-make"]:
        generateur = ["-G", "MinGW Makefiles", f"-DCMAKE_MAKE_PROGRAM={outils['mingw32-make']}"]
    else:
        generateur = ["-G", "Ninja", f"-DCMAKE_MAKE_PROGRAM={outils['ninja']}"]
    attendre(lancer("configuration", [outils["cmake"], "-S", "runtime", "-B", construction, *generateur,
                                      f"-DCMAKE_C_COMPILER={outils['gcc']}", f"-DCMAKE_CXX_COMPILER={outils['g++']}",
                                      "-DSFC_ARM7=ON", f"-DGENERATED_DIR={gen9}", f"-DGENERATED7_DIR={gen7}",
                                      *option_icone],
                    prep / "journal_configuration.txt", env))
    vu = [-1]

    def suivre(ligne: str) -> None:
        m = re.match(r"\[\s*(\d+)%\]", ligne)
        if m and int(m.group(1)) != vu[0]:
            vu[0] = int(m.group(1))
            dire(f"AVANCE {vu[0]}")

    attendre(lancer("compilation", [outils["cmake"], "--build", construction, "-j", str(max(1, args.coeurs)), "--target", "sfc"],
                    prep / "journal_compilation.txt", env, avance=suivre))

    jeu.mkdir(parents=True, exist_ok=True)
    shutil.copy2(construction / "sfc.exe", jeu / "sfc.exe")
    duree = int(time.time() - debut)
    (jeu / "preparation.txt").write_text(
        f"version={VERSION}\ncartouche={cartouche}\nempreinte={SHA1_UTILE}\n"
        f"date={time.strftime('%Y-%m-%d %H:%M')}\nduree={duree}\n"
        "# Ce jeu a été construit sur cette machine, depuis cette cartouche : il est pour votre usage\n"
        "# personnel et ne se partage pas plus qu'elle.\n", encoding="utf-8")
    if not args.garder:
        shutil.rmtree(prep, ignore_errors=True)
    dire(f"PRÊT {jeu / 'sfc.exe'} ({duree // 60} min {duree % 60:02d} s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

# Les outils de construction livrés

Une version publiée du portage contient, dans `outils/`, de quoi construire le jeu sur la machine du
joueur, depuis sa cartouche (« Préparer le jeu » dans le lanceur, `tools/preparer.py`) : le joueur n'a
rien à installer. Ces outils sont ceux de leurs auteurs, tels qu'ils les publient, sans aucune
modification ; ils ne contiennent rien du jeu. `tools/outils.py` les retélécharge et vérifie chaque
archive par son empreinte avant de la décompresser ; `tools/paquet.py --avec-outils` les joint au
paquet, avec cette notice.

| Dossier | Outil | Version | Source officielle | Empreinte SHA-256 de l'archive | Licence |
|---|---|---|---|---|---|
| `outils/python` | Python, version embarquable | 3.13.15 | [python.org](https://www.python.org/downloads/release/python-31315/) — `python-3.13.15-embed-amd64.zip` | `d1f04d990aee1253d8569e8e5104e30fa9f5fa830899f14843448872d936a2cf` | Python Software Foundation License (`outils/python/LICENSE.txt`) |
| `outils/cmake` | CMake | 4.4.5 | [Kitware, GitHub](https://github.com/Kitware/CMake/releases/tag/v4.4.5) — `cmake-4.4.5-windows-x86_64.zip` | `326c826486d46a23cf9d5a1c9ee3a567d427bdd02c4a34071937a2a675cff004` | BSD 3 clauses (`outils/cmake/doc/cmake/LICENSE.rst`) |
| `outils/mingw64` | GCC 16.2.0 pour Windows (MinGW-w64 14.0.0, UCRT), distribution WinLibs, révision r2 | 16.2.0 r2 | [WinLibs, GitHub](https://github.com/brechtsanders/winlibs_mingw/releases/tag/16.2.0posix-14.0.0-ucrt-r2) — `winlibs-x86_64-posix-seh-gcc-16.2.0-mingw-w64ucrt-14.0.0-r2.zip` | `d5dbafc4a170e762ca6143151ec918fb9e2c72736fb14cd704abebc6bdd5276a` | GCC et binutils : GNU GPL v3, avec l'exception des bibliothèques d'exécution de GCC ; MinGW-w64 : licences libres de ses composants (voir `outils/mingw64/share` et [WinLibs](https://winlibs.com/)) |

**Les sources de GCC et de ses compagnons.** GCC, binutils et une partie de MinGW-w64 sont sous
GNU GPL : qui les redistribue doit permettre d'en obtenir le code source correspondant. Ce sont
ceux de la distribution WinLibs ci-dessus, construite depuis les sources publiées par leurs
projets — GCC 16.2.0 ([gcc.gnu.org](https://gcc.gnu.org/)), MinGW-w64 14.0.0
([mingw-w64.org](https://www.mingw-w64.org/)), binutils ([gnu.org](https://www.gnu.org/software/binutils/)) —,
dont `outils/mingw64/version_info.txt` donne la liste et les versions exactes, et que la page de
WinLibs recense. Une version publiée du portage qui joint `outils/` doit garder ces indications à côté,
ou joindre elle-même ces sources.

Le code compilé avec ce GCC n'est pas soumis à la GPL : l'exception des bibliothèques d'exécution de
GCC (*GCC Runtime Library Exception*) le permet expressément. Le jeu construit chez le joueur reste,
lui, pour son usage personnel : il dérive de sa cartouche.

Pourquoi ces versions-là : le GCC est exactement celui du développement — c'est avec lui qu'une
préparation partie des seuls fichiers du dépôt a donné un code identique au nôtre et 30 000 images
aux empreintes mémoire identiques. Python est la version du développement ; CMake, la dernière
publiée au moment du choix (8 novembre 2026). Changer l'une d'elles demande de refaire cette épreuve.

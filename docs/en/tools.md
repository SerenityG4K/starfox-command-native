# The bundled build tools

*French original: [outils.md](../outils.md).*

A published version of the port contains, in `outils/`, everything needed to build the game on the
player's machine from their own ROM ("Build the game" in the launcher, `tools/preparer.py`): the player
has nothing to install. These tools are their authors' own, as they publish them, without any
modification; they contain nothing from the game. `tools/outils.py` downloads them again and checks each
archive against its fingerprint before extracting it; `tools/paquet.py --avec-outils` adds them to the
package, together with this notice.

| Folder | Tool | Version | Official source | SHA-256 of the archive | License |
|---|---|---|---|---|---|
| `outils/python` | Python, embeddable version | 3.13.15 | [python.org](https://www.python.org/downloads/release/python-31315/) — `python-3.13.15-embed-amd64.zip` | `d1f04d990aee1253d8569e8e5104e30fa9f5fa830899f14843448872d936a2cf` | Python Software Foundation License (`outils/python/LICENSE.txt`) |
| `outils/cmake` | CMake | 4.4.5 | [Kitware, GitHub](https://github.com/Kitware/CMake/releases/tag/v4.4.5) — `cmake-4.4.5-windows-x86_64.zip` | `326c826486d46a23cf9d5a1c9ee3a567d427bdd02c4a34071937a2a675cff004` | BSD 3-Clause (`outils/cmake/doc/cmake/LICENSE.rst`) |
| `outils/mingw64` | GCC 16.2.0 for Windows (MinGW-w64 14.0.0, UCRT), WinLibs distribution, revision r2 | 16.2.0 r2 | [WinLibs, GitHub](https://github.com/brechtsanders/winlibs_mingw/releases/tag/16.2.0posix-14.0.0-ucrt-r2) — `winlibs-x86_64-posix-seh-gcc-16.2.0-mingw-w64ucrt-14.0.0-r2.zip` | `d5dbafc4a170e762ca6143151ec918fb9e2c72736fb14cd704abebc6bdd5276a` | GCC and binutils: GNU GPL v3, with the GCC Runtime Library Exception; MinGW-w64: the free licenses of its components (see `outils/mingw64/share` and [WinLibs](https://winlibs.com/)) |

**The sources of GCC and its companions.** GCC, binutils and part of MinGW-w64 are under the GNU GPL:
whoever redistributes them must make the corresponding source code available. They are those of the
WinLibs distribution above, built from the sources published by their projects — GCC 16.2.0
([gcc.gnu.org](https://gcc.gnu.org/)), MinGW-w64 14.0.0 ([mingw-w64.org](https://www.mingw-w64.org/)),
binutils ([gnu.org](https://www.gnu.org/software/binutils/)) —, whose exact list and versions are given
in `outils/mingw64/version_info.txt` and listed on the WinLibs page. A published version of the port
that includes `outils/` must keep these pointers alongside it, or include the sources itself.

Code compiled with this GCC is not subject to the GPL: the GCC Runtime Library Exception expressly
allows it. The game built on the player's machine remains for their personal use: it derives from their
ROM.

Why these versions: the GCC is exactly the one used for development — with it, a build started from the
repository's files alone produced code identical to ours, and 30,000 frames with identical memory
fingerprints. Python is the development version; CMake, the latest release at the time of the choice
(November 8, 2026). Changing any of them means running that test again.

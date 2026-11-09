# Reading and modifying the game's assets

This procedure explains where the game's data is, how to read it, and how to
replace a piece of it — a text, a script, a color — so that the game picks it
up. All commands are run from the repository root, with Python 3.11 or later.
None of the game's data is in the repository: everything starts from your copy
of the ROM.

## 1. Where the data is

**`data/` is what the game reads.** `tools/build_data.py` cuts it out of the
ROM, once and for all:

```bash
python tools/build_data.py "Star Fox Command (Europe) (En,Fr,De,Es,It).nds" -o data
```

| Part | Contents |
|---|---|
| `data/files/` | the game's 3,556 files, in the cartridge's directory tree, as **raw bytes**: a name ending in `-cmp` is compressed (LZ10), as on the cartridge |
| `data/files/*.srl` | 21 archives: one per planet (`Corneria.srl`…), one per language (`Fr.srl`, `Uk.srl`…) and their `_child` variants. Each one is a small cartridge (header, names, allocation table) that holds its own files: the models, scripts, textures and texts of a planet or a language |
| `data/files/__unnamed__/` | the 26 overlays: code, **not to be touched** |
| `data/system/` | the header, the binaries of the two processors, the name table (`fnt.bin`) and **the allocation table** (`fat.bin`): the start and end of each file in the cartridge's address space |
| `data/index.bin` | where each piece of that space is located on disk |

The game requests its files by address, as it would from the console's
cartridge reader; it finds them through the allocation table, and
`runtime/data.c` serves each address from the file that `index.bin` points to.
Hence the rule: a file that changes size needs its table and index entries
corrected — which is what `tools/fichier_du_jeu.py` does (§ 4).

**`assets/` is a study copy, which the game never reads**: everything
extracted, decompressed, and exported to common formats (OBJ, PNG, WAV, JSON).
The chain that produces it:

```bash
python tools/extract_rom.py "Star Fox Command (Europe) (En,Fr,De,Es,It).nds" -o assets/extracted
python tools/decompress_tree.py assets/extracted/files -o assets/cooked
python tools/extract_srl.py assets/cooked -o assets/cooked/_srl
python tools/bmg.py assets/cooked -o assets/text
python tools/export_models.py assets/cooked -o assets/export
python tools/export_collision.py assets/cooked -o assets/export
python tools/ntfq.py assets/cooked -o assets/export/_texanim
python tools/sdat.py assets/cooked/StarfoxDS_Eu.sdat -o assets/audio --names assets/cooked/sound_data.sadl
```

That is where you search and look at things; it is in `data/` (a copy of it)
that you make changes.

## 2. Preparing a working folder

Never modify `data/`: it remains the reference, the one for the recorded
sessions and their fingerprints. Work on a copy:

```bash
cp -r data data_mod
```

(`Copy-Item -Recurse data data_mod` in PowerShell; 57 MB.) `fichier_du_jeu.py`
refuses to put files into `data/` itself, except with `--force`.

To play with this folder:

- **from the launcher**: “Parcourir…” (Browse…), then choose
  `data_mod/index.bin`;
- **from the command line**, without a ROM path (a ROM passed as an argument
  would make the game ignore the folder):

```bash
SFC_DATA=data_mod SFC_ARM7=1 ./build/runtime8/sfc.exe
```

Before trying a modification that affects the course of the game, copy your
save file (`sauvegarde/starfox_command.sav`), or play on a copy with
`SFC_SAVE=copie.sav`.

## 3. Reading

### Finding a file

```bash
python tools/fichier_du_jeu.py liste data SetupShared
python tools/fichier_du_jeu.py liste data battle --dans Fr.srl
```

Each line gives the address, the size and the path. A file inside an archive
is designated as `archive:path`, for example
`Fr.srl:2D/MessageText/battle_Fr.bmg-cmp`.

### Extracting a file

```bash
python tools/fichier_du_jeu.py lire data SetupShared.txt-cmp -o SetupShared.txt
python tools/fichier_du_jeu.py lire data "Fr.srl:2D/MessageText/battle_Fr.bmg-cmp" -o battle_Fr.bmg
```

A file ending in `-cmp` comes out decompressed (`--brut` leaves it as is).

### By type

| Files | What it is | How to read it | Documentation |
|---|---|---|---|
| `.bmg` | the texts: story, dialogue lines, menus, per language | `python tools/bmg.py --texte file.bmg -o file.json` | [format_bmg.md](format_bmg.md) |
| `.txt` | the scripts, in plain text (Shift-JIS): stages, enemies, models, collision volumes | a text editor | [scripts.md](scripts.md), [script_reference.md](../script_reference.md) (in French) |
| `.nbfc`, `.nbfp`, `.nbfs` | a 2D background: tiles, palette, screen map | `python tools/fond_2d.py data 2D/StrategyScene/missile_mode -o fond.png` (`--8bpp` for 256 colors) | below |
| `.ntfq` | textures for effects, the HUD, menus | `python tools/ntfq.py assets/cooked -o assets/export/_texanim` | [format_ntfq.md](format_ntfq.md) |
| `.mdl`, `.tset` | 3D models and texture sets | `python tools/export_models.py assets/cooked -o assets/export` (OBJ, MTL, PNG) | [format_mdl.md](format_mdl.md) |
| `.hgt` | terrain collision | `python tools/export_collision.py assets/cooked -o assets/export` | [format_hgt.md](format_hgt.md) |
| `.sdat` | music and sound effects | `python tools/sdat.py …` (samples as WAV, catalog) | [format_audio.md](format_audio.md) |

Formats of a 2D background: the palette is a sequence of 16-bit RGB555 colors
(red in bits 0-4, green 5-9, blue 10-14; color 0 of each 16-color palette is
transparent); tiles are 8×8 pixels at 4 bits per pixel (32 bytes) or 8 bits
(64 bytes); the screen map has one 16-bit entry per 8×8 cell — tile number
(bits 0-9), flips (10, 11), palette (12-15).

Exports in `assets/export` dated before 2026-09-12 are wrong for the 411
models that use `VTX_DIFF` (the enemy fighters): rerun `export_models.py`.

## 4. Modifying

### The principle

You extract the file (§ 3), modify it, and put it back:

```bash
python tools/fichier_du_jeu.py poser data_mod <chemin> <fichier modifié>
```

- **Compression is done for you**: for a path ending in `-cmp` you give the
  decompressed file, and it is recompressed as LZ10, without any reference to
  the immediately preceding byte — decompression into video memory writes in
  halfwords (`--brut` puts an already compressed file as is).
- **The size can change.** A file that keeps its size, or shrinks, stays where
  it is. A file that grows is placed after the last one, on a 512-byte
  boundary, and its entry is corrected in `system/fat.bin` and in `index.bin`.
- **Inside an archive**, the same is done in the archive's table; the
  archive, which may have grown, is then put back in turn.
- You can put files as many times as you like: each put starts from the
  folder's current state.

### Example: a text

Dash's line “J'engage le combat contre le vaisseau mère ennemi!” (I'm engaging
the enemy mothership!) is message 340 of the battle lines. When playing in
French, the game reads its texts from `Fr.srl`.

```bash
python tools/fichier_du_jeu.py lire data_mod "Fr.srl:2D/MessageText/battle_Fr.bmg-cmp" -o battle_Fr.bmg
python tools/bmg.py --texte battle_Fr.bmg -o battle_Fr.json
#   in battle_Fr.json, change the "texte" of id 340; \n starts a new line
python tools/bmg.py --reecrire battle_Fr.bmg battle_Fr.json -o battle_Fr_neuf.bmg
python tools/fichier_du_jeu.py poser data_mod "Fr.srl:2D/MessageText/battle_Fr.bmg-cmp" battle_Fr_neuf.bmg
```

- Keep the `{group:type:arguments}` sequences as they are: they are the
  colors, the pauses, the player's name.
- Only the characters in the game's font are displayed: French accented
  letters, yes; `°`, no.
- Stay within the length of the original lines: what the speech bubble does
  with a longer text has not been tried.
- The numbers come from `bmg.py --texte`: the JSON files in `assets/text`
  produced before 2026-09-29 were off by four; regenerate them.

### Example: a script

Scripts declare models, collision volumes, enemies, trajectories.
`SetupShared.txt`, for example, gives the Arwing's collision volume
(`RegistVolume Arwing 4.0`).

```bash
python tools/fichier_du_jeu.py lire data_mod SetupShared.txt-cmp -o SetupShared.txt
#   edit SetupShared.txt, keeping its encoding (Shift-JIS; most often pure ASCII)
python tools/fichier_du_jeu.py poser data_mod SetupShared.txt-cmp SetupShared.txt
```

`#` starts a comment. A planet's scripts are in its archive
(`Corneria.srl:Scripts/…`); `python tools/fichier_du_jeu.py liste data Scripts --dans Corneria.srl`
lists them. The meaning of the commands is in
[script_reference.md](../script_reference.md).

### Example: a color

A palette is modified byte by byte, without changing its size. Here, color 1
of the palette for the strategy map's lines becomes red:

```bash
python tools/fichier_du_jeu.py lire data_mod 2D/StrategyScene/Map_Line.nbfp -o Map_Line.nbfp
python -c "import struct; p = bytearray(open('Map_Line.nbfp','rb').read()); struct.pack_into('<H', p, 2 * 1, 31 | 0 << 5 | 0 << 10); open('Map_Line.nbfp','wb').write(p)"
python tools/fichier_du_jeu.py poser data_mod 2D/StrategyScene/Map_Line.nbfp Map_Line.nbfp
```

A color is `red | green << 5 | blue << 10`, each component from 0 to 31.
`fond_2d.py` shows the result before going in-game: here, the background's
lines turn from green to red.

### What cannot be modified yet

| What | Where things stand |
|---|---|
| overlays, `system/` | never: the code that runs is the code that was translated, and behavior is changed through the modification points (`runtime/modifications.c`, [reference.md](reference.md) § 5 ter) |
| `.mdl`, `.tset` models | reading and export only; rewriting them would require regenerating display lists, offsets and patches ([format_mdl.md](format_mdl.md)) |
| `.ntfq` textures | at the same size only (palette colors, texels): the file size determines the height and the number of colors |
| `.sdat` sounds | reading only |
| new messages | a message can be replaced, but none can be added yet |

## 5. Checking

- Launch with the modified folder (§ 2) and go as far as the place you
  changed. When started from the launcher, the game writes its log to
  `journal.txt`, at the root: it says why it stops, if it stops.
- For 2D, `SFC_2D_IMAGES=frame` writes out the two screens as composed at the
  end of that frame (`build/essai/ram_2d_A_<frame>.ppm`, `…_B_…`), even
  without a window (`SFC_NO_VIDEO=1`, with a replayed session).
- Recorded sessions (`build/enregistrements/`) only replay faithfully with the
  original data: a modification that changes the course of the game makes
  them diverge.

What has been tested in-game (on 2026-09-29, with session `obs52` replayed):

- `SetupShared.txt`, enlarged with comment lines (2,156 → 5,952 compressed
  bytes), was relocated further along. The game read it at its new address as
  soon as combat began, and the session followed exactly the same path as with
  the original (1,440 identical camera readings).
- Line 340 of `battle_Fr.bmg` was changed in each of its eight copies, with
  each archive growing and moving. The screen showed the one from `Fr.srl`.
- Exact round trip: the compressor on 155 of the game's files (size within
  0.5% of Nintendo's), the text rewriting on the 53 files in the archives.

## 6. Reverting

Delete the working folder and make a fresh copy of `data/`. If `data/` itself
has been touched, rebuild it from the ROM with `tools/build_data.py`.

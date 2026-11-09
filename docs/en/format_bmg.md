# The `.bmg` format — the game's dialogue

66 message archives in Nintendo's `MESGbmg1` format, i.e. **38,415 non-empty
messages**: the whole story, the combat lines, the strategy screens, the enemy
names and the promotional texts, in five languages.

```bash
python tools/bmg.py assets/cooked -o assets/text                        # extract everything
python tools/bmg.py --texte battle_Fr.bmg -o battle_Fr.json               # one file, to edit
python tools/bmg.py --reecrire battle_Fr.bmg battle_Fr.json -o neuf.bmg   # rewrite it
```

The files produced (`assets/text/<Langue>/<archive>.json`) contain
copyrighted text: they stay out of the repository, like everything that comes
out of the ROM. When an archive stores the same name twice — `Fr_child` has
`2D/MessageText/battle_Fr.bmg` and `Fr/2D/MessageText/battle_Fr.bmg` — what
precedes `2D/` is added to the path (`Fr_child/Fr/battle_Fr.json`).

To change a text in the game, see [the procedure](assets.md).

## Where the texts are

| Location | Content |
|---|---|
| `_srl/<Langue>/2D/MessageText/scenario_<L>.bmg` | the story: 1,652 messages per language |
| `…/battle_<L>.bmg` | the combat lines: 610 |
| `…/strategy_<L>.bmg` | the strategy screen: 521 |
| `…/enemy_<L>.bmg`, `…/advertise_<L>.bmg` | enemy names, demo texts |
| `_srl/<Langue>_child/…` | 2,456 messages per language, a subset |
| `2D/MessageText/*_Du.bmg` | a variant at the root (3,004 messages) |
| `corneria.bmg` | 23 big-endian messages in Shift-JIS: a development leftover |

The languages are `Fr`, `De`, `Es`, `It` and `Uk`; each language `.srl`
archive is a ROM image mounted by the game, like those of the planets.
**The game in French reads `Fr.srl`**: a combat line changed in each of the
eight copies of `battle_Fr.bmg` (in `Fr.srl`, `Fr_child.srl` twice,
`child.srl` and the other `_child.srl`) was displayed, in the replayed `obs52`
session, with the text of the copy in `Fr.srl`.

## Structure

| Area | Content |
|---|---|
| header, 32 bytes | magic `MESGbmg1`, file size, number of sections, encoding at byte 16 |
| `INF1` | 16-byte header — tag, size, number of messages (`u16`), entry size (`u16`), 4 bytes — then one entry per message, whose first 4 bytes are the offset into `DAT1` |
| `DAT1` | the strings, null-terminated; the first one, empty, serves the empty messages |

Sections are padded to a multiple of 32 bytes. A message is designated by its
rank in `INF1`, starting from 0: that is the `id` in the JSON files.
(Until 2026-09-29, the extractor read the entries 16 bytes too far: it skipped
the first four messages and numbered the others four too low. JSON files
produced before that date have to be regenerated.)

Encodings: `1` CP1252, `2` UTF-16, `3` Shift-JIS, `4` UTF-8. The language
archives are in **little-endian UTF-16**; `corneria.bmg`, produced by a
GameCube toolchain, is **big-endian** — the endianness is detected by
comparing the size field with the file's actual size.

### Control sequences

The character `0x1A` opens a sequence: its total length in bytes, a group, a
type as a `u16`, then arguments. They carry the colors, the pauses, the
insertion of the player's name. The extractor keeps them as they are, in the
form `{group:type:arguments}`, so as to lose nothing, and rewriting gives them
back identically; their semantics remain to be established by comparing the
occurrences with what the game displays.

## Rewriting

`--reecrire` starts from the original file and only changes the messages of
the JSON whose text differs: the new text is appended at the end of `DAT1`,
and the message's entry points to it. Everything else is kept byte for byte —
a round trip without changes gives back the archives' 53 files byte for byte,
and the 175 messages with control sequences read back identically. Only the
characters that the game's font (`.nftr`) knows are displayed: French accented
letters, yes; the `°`, no.

## For continuing the story

Lines are addressed by their index in the archive; these indices are what the
script commands `MemberTalks`, `PutMessage` and `SetBattleMessage` refer to.
Writing a new chapter will come down to adding messages to `scenario_<L>.bmg`
and calling them from new mission scripts; adding messages (and not merely
replacing them) remains to be written.

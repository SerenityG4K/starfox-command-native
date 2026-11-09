# Audio — the Nitro Composer `.sdat` archive

```bash
python tools/sdat.py assets/cooked/StarfoxDS_Eu.sdat -o assets/audio --names assets/cooked/sound_data.sadl
```

The files produced under `assets/audio/` are game data and stay out of the
repository.

## What the archive contains

`StarfoxDS_Eu.sdat` (3.6 MB) is a standard Nitro Composer archive, with two
peculiarities: it has **no SYMB block** (the offset is zero, hence no names),
and `StarfoxDS_Uk.sdat` is **byte-for-byte identical** to it — the second copy
is redundant.

| INFO records | Count | Role |
|---|---:|---|
| `SEQ` | 137 | music sequences and jingles (`SSEQ`) |
| `SEQARC` | 183 | sequence archives (`SSAR`), typically the sound effects |
| `BANK` | 231 | instrument banks (`SBNK`) |
| `WAVEARC` | 5 | wave archives (`SWAR`): **376 samples** |
| `PLAYER`, `GROUP` | 10, 176 | players and loading groups |
| `STRM` | **0** | no streams |

Conclusion: **all the music is sequenced**, the standard DS way — an `SSEQ`
sequence drives an `SBNK` bank that draws on the `SWAR` waves. Playing it back
requires a sequencer and a synthesizer (envelopes, pitch bends, modulation,
PSG and noise channels): that is a project in its own right, well charted by
existing community tools.

## What is extracted

The 376 `SWAV` samples of the five archives, as 16-bit WAV: 265 seconds in
total, from 8 to 44.1 kHz. They are the soundtrack's instruments and, very
probably, the sound effects themselves. The irregular frequencies
(16,004 Hz, 22,053 Hz…) come from the way the DS timer encodes the rate, and
are exact.

The sequences and banks are cataloged (index, file, size) in
`assets/audio/catalogue.json`, without being rendered.

## The `sound_data.sadl` file

It is not audio: it is a 1.4 KB C header — `#define STRM_SE_Laser 0`, … — that
numbers 46 identifiers of the sound registry, including three music tracks:
`STRM_BGM_Arwing`, `STRM_BGM_Walker`, `STRM_BGM_Greatfox`. The `STRM` prefix
is historical: these identifiers refer to sequences and samples, not to
streams. The scripts declare them with `RegistSound SE_Laser 1` (46 calls) and
start the music with `startBgm 1`.

## Next steps

1. Name the 376 samples by cross-referencing the scripts' `RegistSound`
   commands, the `.sadl` identifiers and the banks that reference each wave.
2. Play back the `SSEQ` sequences: sequencer + synthesizer, or MIDI export +
   SoundFont as a first approximation.

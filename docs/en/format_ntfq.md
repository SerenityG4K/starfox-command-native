# The `.ntfq` format — textures for animations, the HUD, effects and menus

4,282 files, the most numerous in the ROM. A `.ntfq` file contains only the
**raw texels** of a DS texture followed by its **palette**, of variable
length: only the colors used are written (5 entries for the `Muzzle`
ammunition, 84 for an explosion frame).

```bash
python tools/ntfq.py assets/cooked -o assets/export/_texanim
```

## Neither dimensions nor format in the file

It is the scripts that provide them:

```
CreateTexAnimBase Face_Fox 64 64
LoadTexAnimPatt   PLTT16 /3D/Textures/Overlays/HUD/Face_Fox_SP

CreateTexAnimSeq  Explosion 64 64 COMP4x4 /3D/Textures/FX/Explosion/ExA_SP 16 1
```

The width and the format come from the declaration; the actual height is
deduced from the file size, and several frames can be stacked in it
(`BoostBorder`: two 64 × 8 frames). The number of palette entries is the one
that makes the height a whole number and a multiple of 8 — unique in practice,
whereas an inference without the declaration leaves dozens of solutions.

Formats encountered: `PLTT4` (418 declarations), `PLTT16`, `A3I5`, `A5I3`,
`COMP4x4`. For compressed 4 × 4, the texels then the indices are
concatenated, as in the `MGL Texture` chunks.

A sequence names its frames `<path>01`, `<path>02`, …: the standard
explosion has sixteen 64 × 64 frames in compressed 4 × 4, a fireball that
swells and then scatters.

## What this opens up

Texture animations carry the explosions, the shots, the HUD (the characters'
faces, reticle, radar) and the menus. In the engine, they are rendered as
camera-facing billboards for the effects and as an orthographic overlay for
the interface.

Visual check: Fox's face (`Face_Fox_SP`, 64 × 64, 16 colors) and the reticle
(`Target_SP`, 32 × 32, 4 colors) come out sharp.

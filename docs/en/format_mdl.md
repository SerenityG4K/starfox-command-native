# The `.mdl` / `.tset` / `.anim` / `.hgt` format — the “chnk” container (MGL)

Reverse engineering carried out on the European ROM's 2,176 models, 166
texture sets, 136 animation sequences and 763 collision grids.

`MGL` is Q-Games' in-house graphics library; all 3D assets share a single
container made of typed chunks.

## 1. Chunk header (100 bytes, `0x64`)

| Offset | Type | Field |
|---|---|---|
| `0x00` | `char[4]` | magic `chnk` |
| `0x04` | `u32` | version (always `1`) |
| `0x08` | `u32` | `1` = contains sub-chunks, `0` = leaf |
| `0x0C` | `u32` | total size of the chunk, **header included** |
| `0x10` | `u32` | class identifier (`1` = Model, `3` = Mesh) |
| `0x14` | `char[16]` | type tag, `NUL`-terminated |
| `0x24` | `char[56]` | asset name, `NUL`-terminated |
| `0x5C` | `u32` | constant `0x4499B63B` |
| `0x60` | `u32` | content fingerprint |

Sub-chunks follow one another immediately after the header and are walked by
advancing each time by the chunk's total size (field `0x0C`), until the end of the
parent chunk.

### Tags encountered

| Tag | Container | Role |
|---|---|---|
| `MGL Model` | `.mdl`, `.nsbmd` | root of a model |
| `MGL Mesh` | in `MGL Model` | geometry + materials |
| `MGL Texture` | `.tset`, `.mdl` | texel data |
| `MGL Palette` | `.tset`, `.mdl` | associated palette |
| `MGL ModelSet` | `.tset` | root of a shared texture set |
| `MGL AnimSeq` | `.anim` | root of an animation sequence |
| `MGL AnimSeqInfo` | in `MGL AnimSeq` | sequence metadata |
| `MGL LKSqfix16` | in `MGL AnimSeq` | key tracks in 4.12 fixed point |
| `MGL LKSqMGLRgb` | in `MGL AnimSeq` | color key tracks |
| `HGT Info` | `.hgt` | height grid for ground collision |

## 2. The `MGL Mesh` payload

```c
struct MglMesh {
    u32 id;                       // fingerprint
    u32 n_materials;
    u32 n_textures;
    u32 n_shapes;
    u32 n_texture_patches;

    Material materials[n_materials];       // 32 bytes each

    // Name table: n_textures strings, each prefixed with its length
    // on one byte (the length includes the prefix: 0x09 + "Ammonite" does
    // come to 9 characters).
    PascalString texture_names[n_textures];

    struct { u32 material_index; u32 dl_offset; } shapes[n_shapes];

    // Locations of the TEXIMAGE_PARAM commands to patch at load time,
    // when the engine knows the actual VRAM address of each texture.
    struct { u32 texture_index; u32 dl_offset; } patches[n_texture_patches];

    u32 dl_size;
    u8  display_list[dl_size];    // DS GX display list
};
```

### `Material` (32 bytes)

A material is a pre-encoded display list fragment, ready to be injected
directly into the GPU's FIFO:

| Offset | Type | Content |
|---|---|---|
| `0x00` | `u32` | command packet `0x00313029`: `POLYGON_ATTR`, `DIF_AMB`, `SPE_EMI`, `NOP` |
| `0x04` | `u32` | `POLYGON_ATTR` parameter (faces, alpha, polygon ID) |
| `0x08` | `u32` | `DIF_AMB` parameter (diffuse + ambient, RGB555 ×2) |
| `0x0C` | `u32` | `SPE_EMI` parameter (specular + emission) |
| `0x10` | `u32` | command packet `0x00002B2A`: `TEXIMAGE_PARAM`, `PLTT_BASE`, `NOP`, `NOP` |
| `0x14` | `u32` | `TEXIMAGE_PARAM` parameter (size, format, repeat) |
| `0x18` | `u32` | `PLTT_BASE` parameter |
| `0x1C` | `s32` | texture index, `-1` if the material is not textured |

### Display list

The `dl_size` bytes are a standard Nintendo DS GX display list: packets of 4
command bytes followed by their 32-bit parameters.

Everything that comes before the first `shape` forms a common **prologue**:
`MTX_PUSH`, one or two `MTX_SCALE`, `MTX_STORE 16`, `COLOR blanc`. Its length
varies across the corpus:

| First `shape` at | Models | Prologue content |
|---|---:|---|
| `0x18` | 1,078 | `MTX_PUSH`, `MTX_SCALE`, `MTX_STORE 16`, `COLOR` |
| `0x28` | 922 | same, with a second `MTX_SCALE(1,1,1)` that has no effect |
| `0x34` | 168 | variants with an extra `MTX_STORE` |
| `0x40`, `0x50` | 8 | prologues with `MTX_TRANS` |

**The prologue therefore runs up to `min(shapes[i].dl_offset)`, never over a
fixed length.** Truncating it to 24 bytes cuts the first packet right in the
middle for half of the models, and then executes nothing: those models come
out at scale 1 instead of 256 — a mistake made, then corrected, in this
project.

Each `shape` then resumes at its offset with `MTX_RESTORE 16`, applies its
material and emits its primitives.

### Units

Once the prologue has run, **the coordinates are directly in world units**,
those of the scripts. Checks:

- a terrain tile (`CityBlank_Gr`, scale 256) measures 2,580 units, i.e. a
  2,560 grid cell plus a slight overlap at the seams;
- the enemy `FryEnemy001` (scale 256) measures 300 units, for a collision
  volume of radius 50 declared by `SetVolume`;
- the test cube `DummyCubeS` (scale 16) measures 128 units for a volume
  of 100.

Known exception: the players' Arwings (`fox_main.mdl`, scale 1.0) measure
only 5 units. The original engine draws them with its own matrix, whose factor
remains to be determined — no configuration parameter carries it.

## Verification

A format decoder worked out by trial and error is only worth something if it
holds up on every file:

```bash
python tools/validate_models.py assets/cooked
```

Every model decodes, its display list is consumed in full, and no unknown GX
command appears: **2,176 models, 0 errors, 216,813 triangles** (rerun on
September 29, 2026).

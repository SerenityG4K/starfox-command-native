# The `.hgt` format — ground collision (`HGT Info`)

763 collision tiles, one per terrain model, **all decoded**: 62,055 vertices,
94,101 triangles. Decisive check: the `CityD` tile gives exactly the bounds of
its visual model — Y from −82 to 734, horizontal extent 964 — down to the
unit.

```bash
python tools/export_collision.py assets/cooked -o assets/export
```

## Structure

The same “chnk” container as the models (100-byte header, tag `HGT Info`, no
sub-chunk). The payload:

| Field | Content |
|---|---|
| `u16` | version, always 1 |
| `u16` | fingerprint |
| `u16` | 1 (0 or 2 for a handful of tiles) |
| `u16` | not figured out — 157, 160, 212… — no effect on decoding |
| `u32` | 0 |
| `u16` | **160**: side of the tile in HGT units |
| `u16` | **8**: cells per side of the lookup grid |
| `u16` | 0 |
| `u16` | **number of vertices** |
| `u16` | 0 |
| `Vertex[n]` | three 16.16 `fx32`: **x, y (height), z** |
| `u32` | number of triangles |
| `Triangle[m]` | three `u16`, vertex indices |
| `Cell[64]` | `u32` 16.16 height, `u16` count, `u16` 0, `u16` triangle indices |

One HGT unit is worth **16 world units**: a tile's corners fall at ±80
(±1,280, half of the 2,560 cell), the roofs of `CityD` at 44 (704).

## What the tiles contain

- an empty tile (`Blank_Col`): its four corners and two triangles;
- a mountain tile (`YamaA_Col`): a grid of 9 × 9 vertices — 81, i.e. 8 × 8
  cells — and 128 triangles, heights from 45 to 382;
- a city tile (`CityD_Col`, declared `Convex` by `LoadHeightMap`): 232
  vertices and 188 triangles, the closed shells of the buildings.

`LoadHeightMap <name> Ground|Convex <path>` thus distinguishes a ground to
follow from obstacles to go around; the geometry is of the same kind in both
cases.

The shells are **not closed**: `CityD` has 232 vertices for 188 triangles.
The tiles only describe roofs and ground, and the walls are implicit. The
semantics are those of a height field — a “height manager”, the very name the
scripts give the system: the terrain height at (x, z) is that of the triangle
covering the point, and anything flying lower is inside the obstacle. Level
flight toward a building crosses no triangle; it simply passes below the local
height.

The table of 64 cells is a lookup accelerator: for a point (x, z) of the tile,
the cell `(⌊(x + 80) / 20⌋, ⌊(z + 80) / 20⌋)` lists the triangles that cross
it. Its height (16.16) is probably the cell's maximum height — useful for
ruling out right away an object flying above it.

## Corrected mistake

A first analysis saw in these files 1.15 fixed-point normals and two arrays
that could not be found. The “normals” were the fractional parts of 16.16
coordinates, and the difficulty came from an eleventh header word, always
zero, that shifted the reading of the vertices by two bytes.

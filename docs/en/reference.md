# Reference: the port's code and what we know about the game

This document gathers what has been established, and **only** what has been
verified. The addresses given were found by measurement or disassembly, not
assumed. When something is a conjecture, it says so.

It reads in three parts: the build chain, the engine we wrote, and the game
itself — the last being the most valuable, since it can only be recovered
through investigation.

---

## 1. The build chain

The game is not emulated: its binary is **translated once and for all** into C,
then compiled into native code. Three steps, to be rerun in this order after
any change to the tooling:

```bash
python tools/codemap.py "<rom>"                                   # ~3 min
python tools/recomp/recomp.py "<rom>" --regions all --out generated   # ~2 min
cmake --build build/runtime7 -j 8                                  # ~8 min
```

| step | role | output |
|---|---|---|
| `codemap.py` | finds **where the code is**: starts from the entry point, follows calls and jumps, delimits the functions | `analysis/codemap.json`, 29,692 functions |
| `recomp.py` | translates each ARM instruction into C | `generated/*.c`, 8.3 M lines |
| CMake | compiles and links with the engine | `build/runtime7/sfc.exe`, ~110 MB |

The ARM7 coprocessor goes through the same chain with `--arm7` and `--suffixe 7`
(its symbols carry a 7 so that the two can coexist), and `-DSFC_ARM7=ON` links
it in.

**The code mapping is the weak point.** Nowhere does a binary say “a function
starts here”; code and data are intermixed. When the mapping gets a boundary
wrong, an address ends up with no entry point and the engine stops. These stops
are fixed in one cycle: the address is recorded in `analysis/amorces.txt`, which
`codemap.py` reads back as a manual seed.

Two causes of this kind have been dealt with at the root in `recomp.py`: the
generator now opens an entry point at **every address pointed to from memory**
(callback tables) and at **every call target recorded by the map**. This removed
176 and 72 potential stops respectively.

---

## 2. The engine (`runtime/`, 7,700 lines)

It is the only part we write: it reimplements the console's hardware.

### Execution

| file | role |
|---|---|
| `rt.c` | call dispatch, processor modes, native hooks, tracing |
| `memory.c` | the ARM9's memory map; inline accesses (`cpu.h`) read main RAM and the stacks — the ARM9's DTCM, the coprocessor's WRAM — directly; everything else goes through here |
| `irq.c` | interrupts, virtual time, timers, inputs, frame loop |
| `threads.c` | SDK threads ported onto Windows fibers |
| `main.c` | boot: loads the ROM, sets up the ARM9 the way the firmware would |

### Hardware

| file | role |
|---|---|
| `mmio.c`, `mmio7.c` | the registers of both processors |
| `gx.c` | the 3D engine's command FIFO |
| `gx_geom.c` | geometry engine: matrices, vertices, primitive assembly |
| `video.cpp` | OpenGL rendering of polygons, textures, 2D layers |
| `video2d.c` | 2D engines, composited in software in bands of lines (the `video.cpp` worker pool spreads them over several cores) |
| `sound.c` | the sixteen channels and their mixing |
| `audio.c` | host output (a queue of small buffers) |
| `card.c`, `backup.c` | cartridge and save memory |
| `data.c` | the cartridge served from a folder of files (`data/`) |
| `natif.c`, `natif.h` | the hand-rewritten functions, the test bench that proves fidelity — in-game and cold — and what the generator's functions share with it |
| `ipc.c` | dialogue between the two processors |
| `bios.c` | BIOS system calls |
| `arm7_run.c`, `threads7.c` | the translated coprocessor and its threads |
| `arm7_hle.c` | its lightweight replacement (`SFC_ARM7=0`) |

### What to know before touching it

- **Sound mixing follows real time**, not the progress of the simulation: on
  the console, the ARM7 and the audio hardware are independent of the game's
  pace. Mixing only tops up when the output queue drops below its target.
- **A line of 2D rendering must depend only on the registers and video
  memory**: the two screens are composited in parallel, in bands, on several
  threads (`SFC_BANDES_VERIF=1` checks this byte by byte). For the same reason,
  the line buffers cannot be static.
- **Display capture is written to video memory in one block, under
  `g_mx_capture`**, which the VBlank compositing holds: written pixel by pixel
  from the display thread, it used to tear the bottom screen.
- **In free display, the capture is written after the fact**, when the display
  thread sees the frame; the console made it during the frame, before whatever
  the game writes next. So the game's writes to video memory are counted per
  16 KB page (`g_vram_ecrit`, `memory.c`), each snapshot keeps its frame's
  counters, and the capture spares the pages written since (`poser_capture`).
  Without this, on arriving at the strategy map (frame 3,009 of `obs52`), the
  capture of a black screen landed on bank C, which the game had just taken
  back for the bottom screen's tiles: tile 0, transparent, received `0x8000`
  values, one pixel in four of color 8, and BG0, which is tiled with it,
  striped the whole screen until the next battle.
- **Quitting from the game thread goes through `rt_sortir`**: `exit()` destroys
  the static objects while the display thread is still using them. `rt_sortir`
  stops that thread first.
- **Rendering requires only the OpenGL 3.3 core profile**, the same on NVIDIA,
  AMD and Intel. The GPU interpolation tests itself at startup and falls back
  to the CPU at the slightest discrepancy (`docs/plan_framerate.md`); the log
  begins with the driver and graphics card found. `main.c` exports
  `NvOptimusEnablement` and `AmdPowerXpressRequestHighPerformance`: on a laptop
  with two graphics cards, the drivers then run the game on the dedicated card
  rather than the integrated one.
- **The edge budget (256) and the coprocessor threshold (8) must not be touched
  without checking the sound**: see section 6.

### The threads

| thread | what it does |
|---|---|
| game | the simulation of both processors, the preview ticks (`natif.c`), snapshotting each frame, the sound |
| display | the window and OpenGL: matching up the game's frames, planning the path between two of them, drawing (free display); the interpolation itself is done on the GPU, in a geometry shader (`docs/plan_framerate.md`) |
| 2D 1 to 4 | the worker pool: the two 2D screens, in four bands each; the game thread takes its share |
| gamepads | XInput, DirectInput: reads the gamepad every 4 ms and looks for one when Windows announces a device — less than half a percent of a core; the game thread turns it into cockpit commands |

Measured on 2026-09-26 in flight, with the launcher's settings (165 frames per
second, double draw distance): the game thread takes 31 to 34% of a core (six
tenths of it for the hundred and twenty preview ticks of each second, at 1.6 ms
each), the display thread 19 to 20%, the worker pool 14% for all four of its
threads; three quarters of a core in all. Uncapped (`SFC_IPS=0`), the display
climbs to 2,600 – 2,700 frames per second. `SFC_PROFILE=1` reports this
breakdown every six hundred frames (the “fils :” line).

---

## 3. The game: what has been identified

With no symbols, every address below had to be investigated. The names in
quotation marks are the ones the binary itself carries, in its tables.

### The main loop, and why combat thinks at 30 Hz

`0x020A0E4C`, called on the global object `0x0216BED4`, is one tick of the game
loop:

| object field | role |
|---|---|
| `+0x108` | the scene stack (pointers) |
| `+0x188` | its height — 1 in the menus, 2 in flight |
| `+0x1A3` | **the limiter**: zero in the menus, set in flight |
| `+0x1A4` | the duration of the last tick, in milliseconds |
| `+0x1A8`, `+0x1AC` | the clock reference, on 64 bits |

It calls the eighth virtual method of the scene on top, times the tick —
`(tiques << 6) / 33514`, the SDK's conversion — and, with the limiter set, keeps
reading the clock until twenty milliseconds are up. As twenty exceeds the
sixteen of a frame, the game then runs only one tick every two frames: **combat
thinks at 29.9 Hz**. In flight, this wait accounts for more than half of the
main processor's edges (`SFC_PROFILE_ARM9` with `SFC_PROFILE_FROM`).

Replacing the wait with a simple passing of simulated time
(`SFC_ATTENTE_DIRECTE=1`, which leaves the game the last millisecond, and hence
the choice of the moment it exits) saves **a quarter** on the simulation alone:
12,000 frames of the player's session in 9.7 s instead of 12.7 (measured on
2026-09-26; it was 3.7% when the rest cost more). The option stays off: it
changes the fingerprints, and in play the gain is small — the wait only comes
around once per real tick; the preview ticks stop before it. Each pass through
the wait advances simulated time by 64 cycles (the timer read): no two passes
are ever identical, so none of them can be skipped bit-exactly.

The time step is a constant — 136 per tick, a thirtieth of a second in fixed
point: lowering the threshold (`SFC_PAS_MS=10`) doubles the game's speed, except
for a few countdowns tied to the clock. The consequences for the unlocked frame
rate are in [`plan_framerate.md`](plan_framerate.md).

### The collision module (actors overlay, `0x021B3800`–`0x021B6900`)

A hundred and five functions, nearly all in ARM in the middle of a Thumb
overlay: the mark of code written for speed. It is what the game calls most in
flight.

**`CollidableObject`, 188 bytes.** The identification rests on the sizes: its
constructor (`0x021B5A24`) calls its base's (`0x021B47A4`), installs the method
table `0x021CFB0C`, then initializes three fields at `+0xB4`, `+0xB8` and
`+0xB9` — just past the 176 bytes of `ColVolume`, and just short of 188.

| field | role |
|---|---|
| `+0x00` | method table (14 entries) |
| `+0x0C` | node in the list of all objects in the world |
| `+0x14` | node in the list of objects that have partners |
| `+0x24` | type (the pass handles the value 8 separately) |
| `+0x2C` | owner; its flags are at `+0x9C` |
| `+0x34` | identifier, which orders a pair |
| `+0x48` | **partners: four pointers** |
| `+0x58` | their count |
| `+0xB8` | state byte: zero means “takes part” |

| method | address | role |
|---|---|---|
| `[+0x24]` | `0x021B3FF4` | volume update, every frame |
| `[+0x28]` | `0x021B594C` | “takes part?” — returns 1 if `+0xB8` is zero |
| `[+0x34]` | `0x021B580C` | notification: “you are touching this one” |

| function | role |
|---|---|
| `0x021B61A8` | **the per-frame pass**: updates all the volumes, then, for each object that has partners, notifies both sides of each pair |
| `0x021B3B20` | registers a pair: stores the partner with whichever of the two has the smaller identifier, without duplicates, and puts that object on the list of objects to process |
| `0x021B3B18` | the capacity — `mov r0, #4 ; bx lr` |
| `0x021B3C4C` | the test between two volumes (592 bytes) |
| `0x021B3E9C` | calls the test in the order dictated by a flag of the owner |
| `0x021B678C` | the world: nine eight-byte lists, plus the list of objects to process |
| `0x021B3FD0` | first element of a list with embedded nodes |

**A console limit, hard-coded: four partners per object.** It is twofold — the
constant in `0x021B3B18`, and the room in the structure, whose array occupies
exactly `+0x48`–`+0x57`. Beyond four, the pair is silently ignored. Lifting it
means enlarging the object, and therefore moving it to another size class in
the allocator: feasible, but it is not just a constant to tweak.

### System primitives

The game is built on Nintendo's library, whose functions turn up as we read
through the code. The following are established — each one has been
disassembled, and the first four have a rewrite that passes the test bench:

| address | mode | what it does |
|---|---|---|
| `0x0203003C` | ARM | disables interrupts and returns the previous state |
| `0x02030050` | ARM | restores the state returned by the previous one |
| `0x01FFEE38` | Thumb | takes a block from the free list of its size class |
| `0x01FFEDE4` | Thumb | returns a block to its list, or passes it on to the level above |
| `0x02088E50` | Thumb | the level above, when the list is empty: it looks elsewhere, then falls back on the heap (“DynObject allocated from heap: pool %d”) |
| `0x02088DF8` | Thumb | its counterpart for freeing |
| `0x0208940C` | Thumb | releases a reference to an object and frees it on the last release |
| `0x02087484` | Thumb | declares a class: name, size, alignment, and picks its size class |
| `0x02087FC8` | Thumb | constructs a reference-counted “name” object |
| `0x02087FFC` | Thumb | copies the string into it (56 characters at most, length at offset 0x3B) |
| `0x0202DE64` | ARM | wakes up the threads of a wait queue |
| `0x0202DE30` | ARM | signals a synchronization object and wakes up whatever is waiting on it |

The game keeps its objects in **seventeen size classes**, each with its own list
of free blocks; that is why the sizes recorded further down matter: they decide
which pool each object lands in.

### Object pools: a console limit, quantified

Each entry of the class table is 0x1C bytes: head of the free list (`+0`), start
of the area (`+4`), **block size (`+0xC`)**, **number of blocks (`+0x10`)**. The
code does nothing but multiply these last two fields, so their order cannot be
read from it; the count of free blocks is what settles it. Recorded at frame
600, in the menus:

| size | blocks | used | | size | blocks | used |
|---:|---:|---:|---|---:|---:|---:|
| 8 | 4,500 | 500 | | 448 | 170 | 2 |
| 16 | 1,200 | 2 | | 512 | 70 | 0 |
| 32 | 1,100 | 17 | | 580 | 35 | 0 |
| 64 | 2,800 | 713 | | 768 | 25 | 2 |
| 128 | 1,300 | 365 | | 896 | 5 | 0 |
| 192 | 600 | 9 | | 1,024 | 4 | 0 |
| 256 | 110 | 32 | | 1,536 | 1 | 0 |
| 384 | 250 | 54 | | 2,048 | **0** | — |
| | | | | 3,072 | **0** | — |

In flight, in the training mission — the calmest in the game — occupancy already
climbs high: **76%** of the 64-byte class (2,139 blocks out of 2,800), 67% of the
128 one, 64% of the 384 one, 58% of the 8 one, and the single 1,536 block is
taken. One `CollidableObject` in thirty, on the other hand, and none comes near
its four partners: those limits will be measured in a busy battle, not in
training.

817 KB reserved out of the console's 4 MB. An `Actor` (428 bytes) falls into the
448 class: **170 blocks**. The two largest classes have none — any object over
1.5 KB goes straight to the heap. Beyond that, when a class is full, the
allocator searches the following ones, then it too falls back on the heap
(“DynObject allocated from heap”). This is one of the limits the port will be
able to lift, now that we know where it is written.

### The file system, through which everything passes

The functions the game calls most often through a pointer form a coherent
group, recorded by `SFC_STAT_CALL` over nine hundred frames of menus — every
read goes through them, and therefore everything `data.c` serves:

| address | calls | what it does |
|---|---:|---|
| `0x0202260C` | 42,174 | the “write” method of a transfer that targets memory: it reorders its arguments and calls the copy |
| `0x02066184` | 27,622 | an empty virtual method — two bytes, `bx lr` |
| `0x02021C4C` | 21,132 | reads the next entry of a directory: a length byte whose bit 7 means “directory”, the name, then a twelve-bit identifier |
| `0x02021DC4` | — | starts a read on the archive, through its virtual method at `[archive+0x50]` |
| `0x02020030` | 11,276 | loads the hardware division registers (`0x04000290`, `0x04000298`) |
| `0x0202C3B4` | — | the SDK's memory copy, which handles odd addresses |

So the game spends most of its startup walking its directory tree — the one we
now serve it from `data/files/`.

### The game's structures

Each module declares its types at startup — name and size — and
`tools/classes.py` picks these declarations out of the code: **1,602 classes**,
in [`classes.md`](../classes.md) (in French). In it you can read the size of
everything the game handles:

| class | bytes | what it is |
|---|---:|---|
| `Actor` | 428 | everything that lives in the world |
| `ArwingDynamics` | 676 | the Arwing's flight: the largest piloting structure |
| `ArwingVehicle` | 452 | the craft itself |
| `ProximityQuery` | 248 | a neighborhood search in the grid |
| `CollidableObject` | 188 | what can be hit |
| `CollisionCell` | 136 | a cell of the collision grid |
| `ScriptParser` | 76 | reading a level script |
| `ColQuadNode` | 32 | a node of the partition tree |

The nested names (`ArwingChargeShotCallback::TargetInfo`) confirm that they do
come from the original code, with its class hierarchy.

### What each overlay is for

The game names its objects: every function that creates one refers to a
string, and `tools/symboles.py` gathers these mentions in
[`annuaire.md`](../annuaire.md) (in French). The table below summarizes them —
it is an interpretation, based on the identifiers cited, not a certainty the
way a registration table is.

| module | what it names | interpretation |
|---|---|---|
| ARM9 | `Life`, `Attack`, `Explosion`, `Spawn`, `Trigger`, `Radar`, `Motion` | the core: enemy states, scripts, scenes |
| ovl0 | `Actor`, `ActorGroup`, `CollidableObject`, `CollisionCell`, `ColQuadNode`, `ColVolume`, `ProximityQuery` | actors and collision — the foundation of the world |
| ovl2 | `ArwingVehicle`, `ArwingDynamics`, `ArwingCockpitModule`, `ShotActor`, `LaserReflect`, `SmartBomb` | **Arwing piloting and shooting** |
| ovl3 | `Strategy`, `Strategy_Fog`, `FogMapSubScene`, `TransitionScreenSubScene` | the strategy map and its fog |
| ovl4 | `MenuBase`, `TitleSubScene`, `WiFiMenuScene`, `ok_button`, `back_button` | the menus and the title screen |
| ovl5, ovl6, ovl7, ovl17 | `EnemyMothership`, `BeamLaser`, `MorasBlob`, `SearchBeam` | motherships and bosses |
| ovl8, ovl12, ovl16, ovl18, ovl19, ovl20 | `BasicFlyVehicle`, `Patrol`, `Swoop`, `Charge`, `EnemyKingDodora`, `PorcupineSpike` | enemy behaviors |
| ovl9 | `SLIPPY`, `OCTMAN`, `ZAZAN_2`, `ZazanIntroMsg`, `ZazanDeathMsg` | dialogues and characters |
| ovl10 | `MissionClearScene`, `MissionFailedScene`, `RootMenu`, `button_wifi` | the end-of-mission screens |
| ovl11 | `Pigma`, `PigmaA`–`PigmaD`, `ExposeCore` | the fight against Pigma |
| ovl13 | `ShieldA`–`ShieldE`, `AChargeA`–`AChargeG` | shields and charges |
| ovl14 | `DetailBoard`, `DetailInfo`, `eWolf`, `eSandWorm`, `Arwing`, `_twin`, `_plasma` | the briefing and the unit info sheets |
| ovl15 | `ArwingConfig`, `TwinLaser_Item`, `SmartBomb_Item`, `Stealth_Item`, `NetworkBattle` | the Arwing's equipment |
| ovl23 | `EnemyStorageData`, `CorneriaEnemyOverlay`, `AsteroidEnemyOverlay`, `AquasEnemyOverlay`… | **the enemy loader, planet by planet** (`EnemyStorage.cpp`) |
| ovl24 | `gpi*.c`, `gt2*.c`, `sb_server.c`, `hashtable.c` | the GameSpy network library |
| ovl25 | `DownloadManager.cpp`, `loadArchive`, `info_board` | content downloading |

Overlays 1, 21 and 22 name nothing: 21 and 22 are only thirty-two bytes long —
reserved slots, not code.

### The level script mechanism

Levels are described by **plain-text scripts**, present in the ROM
(`assets/cooked/_srl/<planet>/Scripts/`). The interpreter compares command
names against a table.

**Command table: `0x020A3FE0`**, 12-byte entries — string pointer, function,
data. It is the entry point for finding any command.

| command | function |
|---|---|
| `SetHeightGridSize` | `0x02019C14` |
| `RegistHeightGridObject` | `0x02019838` |
| `RegistVolume` | `0x0201A27C` |

Known commands: `RegistModel`, `RegistModelSet`, `RegistHeightModel`,
`RegistHeightGridObject`, `RegistVolume`, `LoadHeightMap`, `SetHeightGridSize`,
`ClearHeightManager`, `CreateEnemy`, `CreateEnemyTemplate`, `CreateMarker`,
`SetEnemyState`, `SetConfigParam`, `SetStageMode`, `PushBackGround`,
`SetBackGroundColor`, `SetBackgroundEffect`, `Include`.

### The terrain

**Manager: `0x021D1E94`**

| offset | contents |
|---|---|
| +0x00, +0x04 | pointers to the cells |
| +0x08 | size of a cell — 2,560.0 units |
| +0x0C | half of it |
| +0x10, +0x14 | index of the first cell: −4 |
| +0x18, +0x20 | corresponding coordinates: −10,240.0 |
| +0x58… | array of pointers, nine per row (9×9 grid) |

- **`0x021780F0`** places a cell: `position = base + index × size`.
- **`0x02178320`** calls it in a loop, after converting the player's position
  into a cell index.

So the map is 9×9 cells of 2,560, i.e. 23,040 units. The game draws 8 to 11 of
them per frame, in a window that slides along with the player; the scenery then
reaches 7,568 units, for a projection that extends to 16,384.

### The draw distance

**What decides whether an object is drawn**: `0x01FFBF8C`, in fast memory
(called through the relay `0x01FFBF84` from the scene renderer `0x020A3620`). It
builds six planes from the camera — near, four sides, far —, brings them into
world space, then walks the list of the scene's objects (head at
`0x027E14D8`, first node at `+8`, end at `+4`; the object starts 0x4C bytes
before its node, its position at `+0x10`). An object is culled as soon as its center
is behind a plane by more than its radius, which its virtual method
`[[object+0xC]]+0x10` returns. The survivors are sorted by depth into 1,024 buckets
(`0x020CAB50`, mask at `+0x2000`), then drawn by `0x01FFB994` (translation,
rotation, scale: commands `0x1C`, `0x1A`, `0x1B`). Terrain cells are
objects like any other.

**The settings it reads**, in fast data memory:

| address | content | value |
|---|---|---|
| `0x027E14F8` | byte: camera index | 1 |
| `0x027E14FC` | cosine of the half field of view | 0.866 (30°) |
| `0x027E1500` | sine of the half field of view | 0.5 |
| `0x027E1504` | **cutoff distance** | **4,000.0** |
| `0x027E1508` | width / height ratio | 4/3 |
| `0x027E150C` | near plane | 2.0 |

All of it is copied every frame by the camera method **`0x02066A34`**
(class `Camera`, 396 bytes) from its fields `+0x170` to `+0x17C` — except the
cutoff distance, **a hard-coded constant** (`0x00FA0000`). When byte
`+0x16C` is set, the same method also computes the projection (`0x020271A4`, into
`+0xE4`) with a far plane of 5,000.0 (`+0x180`); the loaded matrix is
normalized to a quarter, and the rounding of its terms puts the effective far plane at
16,384.

**What hides the distance**: the **horizon ring**. `PushBackGround BG`,
`Clouds` and `Sun` (`0x020195F0`, `StageSetup.txt` scripts) stack up background
models that `0x0218DA9C` (overlay 0) draws every frame at
(camera.x, height, camera.z) — the height being the camera's or zero, plus
a model offset (`+0x30`, and byte `+0x38`, which chooses) —, turned by three
rotations (`+0x10`, `+0x14`, `+0x18`), **with no scaling**. The `BG` model is
a panorama band (512×64 texture) laid in a circle at around 4,000 to 5,000 units,
opaque: everything farther away is hidden behind it. So the game draws
terrain, out to 6,500 units, that nobody sees. The farthest background element
is also at 6,500 units.

That is why pushing back the cutoff distance alone changes **not a single pixel**
(checked at ×3: three times as many cells drawn, 0 to 88 differing pixels):
the ring must be moved back by the same factor. Enlarging it around the camera's
**eye** — height moved away from the eye's, first rotation multiplied — leaves it
covering exactly the same part of the screen: only its depth changes.
Beyond a factor of 2.4, the background would cross the far plane at 16,384, which
is then moved back as well. This is the `SFC_PORTEE` modification (§ 5 ter); at ×2, on the
replay of the final missions, the station's ground stretches to the horizon where
the original draw distance faded it into the fog.

**What the draw distance changes in the game.** Neither its rules nor its pace: the same tick on
the same frame, at ×1, ×2 and ×3, and the camera — hence the player — follows exactly the same
trajectory as long as nothing diverges. But time: drawing has a cost on the console, and the port
counts it (every back edge advances the simulated clock). A tick's clock shifts by a few hundred
cycles from the very first flight (`0x0216C078`, the main loop's object: tick duration and
reference time in timer ticks), and whatever the game derives from its fine clock is affected: on
the Titania replay, nothing visible (same timeline, same trajectory, a few sounds at a slightly
different volume); on the final missions, the same session replayed at ×2 no longer misses the
beacon the player had missed, and the mission no longer ends at the same moment. It is not
visibility that acts on the logic — only `0x01FFBF8C` reads the cutoff distance, and `rand()`
(`0x02004210`, state at `0x020AB9BC`) stays identical at both draw distances —, it is time. A
recorded session therefore replays identically only at the draw distance it was played at, and
the fingerprints of a replay, which hash the entire memory (draw list included), cannot serve as a
reference at a different draw distance.

**Why not a second pass outside of time.** Tried: leaving the original pass intact
(4,000 units), then, on return from `0x01FFBF84` (`0x020A3740`), rerunning the same drawing with
the clock frozen (`g_temps_fige`, edge budget and interrupt line restored, processor and stack
restored), culling at the far plane what the first pass had already drawn. The drawing jobs
(`0x01FFAF54`) all run on the spot in flight (`[0x027E0058]` = 1, the queue is never
used), so the pass cannot get stuck, and it never goes more than
1,696 bytes down the stack. But even nearly empty (factor 1.001) it shifts the sound from frame
584 on: its commands lengthen the list that the game then walks **with its own code** — a
software dispatcher (`0x01FFB758`, a jump table indexed by command) —, and that walk costs time.
A perfectly invisible draw distance would require taking these commands out of the game's list and
injecting them directly into the 3D engine, at the right place in the stream.

**There is no simulation distance.** Two dumps of the render list's objects twenty
frames apart (`SFC_DIAG_OBJETS`): objects at 5,000–6,400 units, out of draw distance,
had moved 166 units. The game keeps the whole mission alive. The “ranges” in `config.txt`
(chase `Chase_Range` 2,000, attack `Max_Attack_Dist` 1,000, turrets and missiles
`Enemy_LaserTurret_AttackLength` and `Enemy_TankEnemy_MissileRange` 2,000…) are not read:
none of these names exists in the code; they are a prototype's enemy settings (see
“The fighters' range”). And no level uses `CreateLODModel` (only test
scripts and commented-out lines).

**Beyond ×2.** A background element (the third model, texture `1A83A79C`, translucent)
passes through the camera's eye: originally, its nearest part is clipped by the near
plane (8 units). Enlarging the background by a factor k around the eye pushes the part lying
between 8/k and 8 units across that plane: invisible at ×2, a large dark rectangle at ×3 when the
camera looks at the mothership. So the launcher stops at double.

### The fighters' range

A shot travels its speed times its lifetime. Seeing twice as far without being able to shoot that
far would make little sense: `SFC_PORTEE` also lengthens the shots (§ 5 ter).

**The Arwing laser** — the player's and their wingmen's — is an overlay 2 object
updated by **`0x021DF7A4`**: 4,500 units per second (`Shot_Speed`, the only
`ArwingConfig` setting that concerns it), an age at `+0x48` that grows by `0x88` (1/30 s) per tick,
and death past **`0x1199` — 1.1 s, hard-coded** (literal read at `0x021DF97E`); in multiplayer the
lifetime comes from `MultiplayCfg` (`0x020562B8`, or `0x020562AC` depending on byte `+0x3C`).
Death goes through `0x021E4C50`, which has byte `+0x14` of its effect set. Measured: 33 ticks,
66 frames, about 4,950 units. What you see is a **streak effect** from overlay 0 (`0x0217FD84`,
class `0x021CCCA8`): head at `+0x68`, tail at `+0x50`, length capped by `+0x5C` (150, one tick of
flight), positioned at mid-length and stretched; `0x021DEEBC` creates and places it. Effects are
elements of a list walked by `0x02173354`: method `+0xC` for the update, `+8` for “finished?”
(byte `+0x29`, set by the end of a non-looping animation track, `0x02171120`), destruction
on the next tick when byte `+0x14` is set.

**Enemy shots** all go through the same initialization, **`0x021E0DB4`**, called by 70
functions in 16 enemy overlays. It receives a descriptor: direction (`+4`), speed per tick
(`+0x10`), streak length (`+0x14`), width (`+0x18`), **lifetime** (`+0x1C`, in seconds as
20.12 fixed point, stored at the shot's `+0x48`), effect (`+0x20`). The overlay 7 enemy fighter
fires at about 1,500 units per second for 1.5 s: 2,240 units.

**From how far enemies fire**: there is no common setting. The ranges in `config.txt` are
dead — only `Enemy_Shot_KillTime` exists in the code, and its line there is commented out. Each
enemy behavior (the scripts' `SetCallback`s, more than a hundred) has its distances hard-coded
and a state machine with named states: `0x02184C94(object, fade, "Default")` changes state, and
each state is a table of seven words, including the entry, the exit and the update (Star Wolf's
is at `0x022560DC`, overlay 9). The enemy code compares a freshly computed length
(`0x01FFED88`) with a constant about 240 times — 155 distinct cases, since behaviors are copied
from one mission overlay to the next. In the firing chain (the functions that call `0x021E0DB4` and
their callers), there are 24 of them: **17 maximum firing ranges**, from 1,500 to 8,000 units — the
overlay 7 fighter, for example, goes back to the “Default” state without firing beyond 2,500 —,
and 7 lower bounds (“too close to fire”) or proximity tests. Finally, many enemies fire
on a timer once engaged, with no distance condition: Star Wolf (overlay 9, update state
`0x0224C284`) fired 172 of the 195 enemy shots in the replay of the final missions.

**The Plasma laser**, the third level after `SingleLaser` and `TwinLaser` (both animated by
`0x021DF7A4`, the second with two streaks), has its own update, **`0x0221B47C`**: same age at
`+0x48`, death past **`0x1CCC` — 1.8 s, hard-coded** (literal read at `0x0221B5B6`), 2,200 to
3,200 units per second, i.e. 3,900 to 5,600 units. Its size (`0x0221B720`) is
1 − 5 × (age − 1.6 s), clamped between 0 and 1: it shrinks during its last two tenths of a second.

**The charged-shot lock-on has no maximum range.** Every 0.25 s (`[viseur+0x18C]`),
**`0x02210D04`** walks list 2 of the collision world (`0x021D82A0`, nine lists of eight
bytes: the enemies) and tests each lockable enemy — alive, above ground, bytes
`+0x96` and `+0xF8`, `[collisionneur+0x14]` — against **a cone**: from the Arwing's position toward the
reticle point (`[viseur+0x50]`), `0x021B11D8` then `0x021B0934`, which hits if the distance between
the enemy's center and the axis does not exceed its radius plus t × tan 30° × 16/128 — t being the
distance along the axis, 16 the field `+0x10` of the lock-on node (`[pilote+0x208]`): a half-angle
of about 4°. Nothing bounds t. The enemy's center must also fall on screen, to within 16 pixels
(`0x020663E4` projects without a far plane, `0x021AC29C` bounds it to 256 × 192). The chosen candidate
(`[viseur+0x178]`, the least locked, then the nearest) is entered into the lock-on
group (`0x021ACB40`, pools `MultiLockGroup` and `MultiLockSight`, 64 of each); the
resulting contact triggers the shot (`0x021EC7DC` creates the projectile, `0x021E6CEC` launches it, and
`0x021E6740` adds the target to the component `[Arwing+0x80]`: targets at `+0x94`, count at `+0xD4`,
level at `+0x10`, damage 20 × {0, 1, 2, 4, 8, 16} depending on the level, `0x021E5EB8`). Measured on the
replay of the final missions: 524 enemies tested between 0 and 10,269 units, one hit at 9,479 — well beyond
the 4,000 that the console draws. The charged projectile (`0x021E69C0`, guidance `0x021E72C0`) flies
straight for ten ticks at 6,000 units per second (counter `+0x64` from 15 to 0), then follows its target at
2,300 units per second for up to 300 ticks — ten seconds, more than 20,000 units. So it is what
you see that bounds what you lock on to: the doubled draw distance doubles the useful range without
changing anything about the lock-on. `LockSightRange` (`ArwingConfig`, 24, from 1 to 256) is a debug setting
that nothing reads.

### The rings (overlay 23)

Rings are objects created by script:
`CreateEnemy TriggerGate SeedTrigger BasicFlyVehicle ITEM_OBJECT …` with
`RegistVolume TriggerGate 110`.

| element | address |
|---|---|
| creation of `SeedTrigger` | `0x02266924` |
| method table | `0x0226F148` |
| `SeedTriggerCallback` (fly-through callback) | `0x022668DC`, offset 0x20 in the table |
| `InvisibleTriggerCallback` | `0x02265F18` |
| object update | `0x02266468` |
| detection computation (state machine) | `0x022652EC` |
| final test | `0x0226529C` |
| position accessor | `0x0216CB64` → object + 0x168 |

**The mechanism**: the update calls the computation, then the test. The test
compares the field **object + 0x20** with the threshold **0x1000**; this field is incremented
by 0x88 per frame in state 2 of the state machine, i.e. half a second
before the callback fires. The computation detects proximity against the threshold of
200.0 units, loaded at `0x0226531A`.

Known fields of the object: `+0x0C` second method table, `+0x10` computation
state, `+0x14`–`+0x1C` scale, `+0x20` tested field, `+0x24` rotation,
`+0x168` position.

Note: this game never uses the 3D engine's test commands
(`BOX_TEST`, `POS_TEST`, `VEC_TEST`) — its collisions are computed purely
in software.

### Costly functions

Found by sampling (`SFC_PROFILE_ARM9`, `SFC_PROFILE_ARM7`):

| address | share | remark |
|---|---|---|
| `0x020A0E4C` | 16.7% | |
| `0x020732F8` | 13.9% | same code as the next one |
| `0x02073580` | 13.5% | except for the constants |
| `0x01FFEE38` | — | allocator, called by both |
| `0x03804A54` (ARM7) | 70% of the coprocessor | processing of the sound driver's messages |
| `0x038044F4` (ARM7) | — | its message receive loop |

Replacing them with native code (`rt_hook`) would be the next big performance
lever, but requires understanding what they do.

### Flight controls, and the gamepad

The game is flown with the stylus. In flight, every tick, the **stylus control**
(`ArwingCockpitRelativeCtrl`, 144 bytes, method table `0x02222BB8`; its
update is method 3, `0x021E9D64`) derives the **cockpit** commands from the stylus
and stores them in a block of the cockpit object — its `+0x68`, returned by
`0x021E9428`; the cockpit's tick, `0x0220EFC4`, reads them right afterwards:

| block field | what it carries | who sets it |
|---|---|---|
| `+0x30`, `+0x32`, `+0x34` | pitch, yaw, bank (halfwords, `0x10000` per revolution) | stylus offset by (*dx*, *dy*) points from its anchor point: −0.85·*dx* degrees of yaw, clamped to ±45; 0.85·*dy* (clamped to ±25) times 4/3 of pitch; −1.2 times the yaw as bank (`0x021EA55A`) |
| `+0x41`, `+0x42`, `+0x48` | barrel roll left, right; 10 | circle recognition (`0x021EC1C4`) |
| `+0x43`, `+0x44`, `+0x46`, `+0x50` | boost, brake, “in progress”; timer of 5 | the double tap; `0x021EBACC` counts down the timer and maintains “in progress”; then `0x021EBBB8` compares “in progress” with the stylus being down and, once the stylus is lifted, clears everything and sets `+0x58`, which the cockpit reads to stop the Arwing (`0x021EE294`) |
| `+0x59` | interrupt | the flight, when the Arwing can no longer keep up the boost or the brake |
| `+0x5A` | tight turn | the stylus at the edge of the screen (yaw × 1.5) |

The control reads the stylus only if the stylus “holds the stick” — byte
`0x02222C6C`, which the touch screen sets when the press starts outside the buttons —
and the Arwing is obeying the controls (neither during a maneuver nor during a
cutscene). The loop and the U-turn do not go through it: their buttons
(`0x02205440`) ask the mission whether they are allowed (`0x021A209C`,
`0x021A2128`), then call `0x021EE528` and `0x021EE550` on the Arwing's
dynamics (`0x02188AD0`).

A missile chase uses a different control, **on rails**
(`ArwingCockpitRelRailsCtrl`, method table `0x02222C1C`, update
`0x021EAF90`), which neither of the two reference sessions goes through. It too
steers from an anchor point, the point of the first tick of a press, but
takes only 0.3 of the offset (`0x4CC`): −0.3·*dx* degrees of yaw, clamped to ±35;
0.3·*dy* (clamped to ±45) times 4/3 of pitch. It does not ask whether the stylus holds
the stick in order to steer, only whether it is down (`0x021EB016`, r1), and only gets there
if the Arwing is obeying (the control's `+0x94` cleared); the stick only matters on the
first tick of a press (`0x021EB02A`), to choose the anchor point — the point read
(`+0x10`) or the last one read while not holding the stick (`+0x54`). It recognizes the circle like
the other one (`0x021EB080`), and the boost and the brake go through the same functions.
It does nothing as long as the active camera (`0x020CF430`, `+0x114`) is not
the Arwing's (`0x021FA7F8`). The loop, the U-turn and the bomb are not
allowed there.

Every battle, the chase included, opens on a shot showing the mission's
target (`0x02046E58`, “DemoTarget…”); its camera takes precedence over the
Arwing's, and no control steers. After two seconds (its age at `+0xD8`
beyond `0x2000`), it closes as soon as a key has just been pressed
(mask `0xFFF`) or the stylus is down (`0x0204863C`, r0); at four
(`0x4000`), on its own. Once the shot is over, the function destroys its camera.

Observed by replaying the `obs52` session into full flight, then with synthetic
gestures; the camera (`SFC_DIAG_CAMERA`), memory at chosen frames
(`SFC_RAM_IMAGES`) and watchpoints (`SFC_WATCH`) tell the rest:

| stylus gesture | what the game does |
|---|---|
| offset horizontally | a turn of up to 69° per second; upward, the Arwing climbs |
| the first three frames of a press | the anchor point, clamped to 32 points from the center horizontally and 24 vertically |
| held still at the anchor point | the turn stops; lifted, the craft keeps its momentum |
| two presses, the second less than 13 frames after the first, and held | boost at the top of the screen, brake at the bottom — 4 frames down and 4 up are enough, 3 and 3 are not; a long first press (40 frames) does not count |
| a little more than one full circle, with a radius of 12 points or more | barrel roll, in either direction; 8 points are not enough |

Flight is recognized by two things: the scene stack (global object
`0x0216BED4`, height at `+0x188`) has two levels — one on the strategy map
and in the menus —, and no window is open on top of it. The
window manager (`0x0216C2E8`: their list, their count at `+0x80`)
writes at `+0x84` the name of the one that opens this way: “PauseWindow” on
pause, nothing during the three flights of `obs52`. The pause keeps its two levels,
and its “Continuer” (Continue), “Rejouer” (Retry), “Quitter” (Quit) buttons are on the touch screen.

**The gamepad** (`runtime/manette.c`) gives these commands directly, each on
its own button — no synthetic gesture —, through ten modification points (§ 5 ter):

- steering: at the two places where the update asks whether the stylus holds
  the stick and whether it is down — for steering, and for the lock-on assist —,
  the gamepad answers yes and places the stylus point itself: at the center on the first
  tick, which the game takes as its anchor point, then offset as the stick is (53
  points sideways and 29.5 vertically at the end of its travel: just enough to
  reach the bounds). The scale, the bounds, the lock-on assist,
  the bank remain the game's code; double taps, which read the real
  stylus, cannot arise by accident. The circle, however, would be recognized
  on that stylus — turning the stick in a circle would do a barrel roll: the player does not
  want that, and the recognition window (the control's `+0x3C`, eight ticks)
  expires every tick when the gamepad is steering. During a
  missile chase, the on-rails control gets the same answer and the same point, at
  half its bounds: 58.5 points sideways and 75.5 vertically at the end of the
  travel, 17.5° of yaw and 30° of pitch (117 and 151 points would reach its
  bounds of 35 and 45: the player found the chase too sensitive that way);
- a battle's opening shot: when the gamepad is steering, the stick pushed or
  a flight button held counts as the stylus being down — whatever steers the Arwing
  closes the shot, once its two seconds are up, as the stylus does on the console;
- boost and brake, right after the update: what the double tap sets,
  and the timer kept refreshed as long as the button is held; on release, the game
  stops the Arwing as when the stylus is lifted; if the game interrupted it itself, you have to
  release to resume;
- the barrel roll: what the circle recognition sets;
- the loop and the U-turn: the same calls as their buttons;
- the bomb: the same launch as the B icon dragged onto the radar, aimed at
  the Arwing (below).

**The bomb.** Dropped on the radar, the B icon leads the touch screen
handler (`0x02205030`) to convert the radar point into a world position
(`0x021ED228`), then to launch the bomb: `0x021ECD8C(dynamique, point)`, which only
takes the point's x and z — it sets the height to 3,000 — and has the smart
bomb delivered from the sky (“Livraison spéciale !” (Special delivery!)). The launch refuses a
second bomb as long as the first is alive (its handle at `+0xF4` of the dynamics),
but does not look at the stock: the touch screen only offers the icon if there are
any left. The stock is in the pilot's record — the mission state `0x021D68B8`
keeps its records at `+4` (`0x021A2904`), the pilot's index comes from
`0x021889A4` —, at `+0x70`, and the touch screen only decrements it for its
own bombs, at the end of the delivery (`0x022010AE`, outside network
play). The mission can also forbid the bomb (`0x021A23B0`, byte `+0x75`
of the mission state, just as `+0x77` does for the loop and `+0x76` for the U-turn). The
gamepad aims at the Arwing — the player's actor (`0x02188B98`), position at `+0x3C`
—, asks for permission, reads the stock, launches, and itself deducts the bomb
that leaves.

Measured: the boost reaches 391 within the first twenty frames instead of 215 with the
stylus, and holds 498; the brake holds 83; the barrel roll starts eight frames after
the press instead of twenty-three; the turn and the climb are those of the stylus
offset by the same amount, and follow the stick linearly — 6.9°/s of turn and
2.6° of climb per tenth of travel, up to 69°/s and 26° (the climb then falls back
on its own, the faster the steeper it is: that is the game, with the
stylus too). On the steering stick, a round 5% dead zone and the full
command at 95% of travel; the 24% zone that Microsoft recommends left
the first quarter with no effect. Two bombs with Y: Dash's
stock goes from 2 to 1 then to 0, the third does not go off, and the B icon empties.
During the missile chase of `obs3` (frame 22,700): the stick to the right
puts the point at (186.5, 96), the yaw is at −17.5° and the Arwing moves
638 units sideways in a hundred frames (997 at full scale); at half travel, −8.8° and
330; upward, pitch −30° and it climbs 703 (815 at full scale,
where it hits its limit sooner); the boost and the brake hold; RB does the barrel roll,
turning the stick in a circle does not. The chases are
found in `obs3`, `obs4`, `obs16` (around frame 22,400), `obs22` (63,596) and
`obs23` (112,145); without a gamepad, `obs3` replayed past the start of the
chase keeps its fingerprints. Pushing the stick closes the opening shot
as A or the stylus does: the one for the `obs22` chase at frame 63,836 instead
of 63,858, the one for the first battle of `obs52` at 6,534 instead of 6,654 — two
seconds after its start, 6,398. During a preview tick, the points supply the held
commands without requesting a maneuver or changing their own state: the fingerprints
are the same with and without previews, boost, loop, U-turn and barrel roll
included. A recording logs the gamepad next to the keys (five columns:
sticks bit-exact, buttons) and the replay feeds it back: a session played with the
gamepad replays identically. Without a gamepad, the points do nothing: the game
is the cartridge's.

### Pilots, their weapons and their health

Each pilot has a rank — that of its key in the table `0x021D77BC`, sixteen bytes per key: 0 `naus`, 1 `fox`,
2 `krystal`, 3 `falco`, 4 `slippy`, 5 `ash` (Dash), 6 `wolf`, 7 `leon`, 8 `panther`, 9 `octman`, 10 `bill`,
11 `lucy`, 12 `amanda`, 13 `cat` (Katt), 14 `peppy`, 15 `james`, 16 `jodie` — and a record, the `PlayerState`,
at `[0x021D68B8 + 4 + 4 × rank]` (`0x021A2914(0x021D68B8, rank)` returns record + 0x10). At startup,
`SetupCharacters.txt` fills it (ovl0 `0x021A2690`):

| record | what it holds |
|---|---|
| `+0x10` to `+0x1C` | the names of the ship's models: the body, the exhaust plates on and off, the flame |
| `+0x20`, `+0x2C`, `+0x38` | where the contrails, the single laser and the twin laser come out |
| `+0x44` | the laser: 0 none, 1 single, 2 twin, 3 plasma, 4 curved shot (`ArcShot`), 5 `Zapper` |
| `+0x48` | the lock-on: 0 none, 1 single, 2 multi, 3 wide |
| `+0x4C`, `+0x70` | bombs: capacity, stock |
| `+0x50`, `+0x54` | the size of the shield and of the gauge: small, middle, large |
| `+0x68` | the shield left, from one battle to the next |

Fox starts with the single laser, single lock-on, two bombs, a middle shield and a large gauge. The story
changes his laser and lock-on (`SetMemberWeapon fox Laser Twin|Plasma`, `… Lock Multi`: ovl10 `0x0223DDD0`);
the save file keeps them in one byte, `0x021D9748` (the laser in the low four bits, the lock-on in the high
ones, `0xFF` for no change), which `0x020449F4` copies into the record each time the map loads. The ship keeps
no copy of its own: firing reads the record every time.

**Firing**: `0x021FE1E4` (method `+0x48` of the vehicle, the pilot at `+0x28`) picks the fire function of the
laser type — single `0x021FFE94` (10 damage), twin `0x021FF840` (15, two traces), plasma `0x021FF338` (20, at
most ten in flight), curved shot `0x021FEFC8` (30), Zapper `0x021FEB8C` (120); `0x021FE178` sets the fire rate
(0.1 s; 0.3 s for the curved shot; 0.8 s for the Zapper, plus a 0.8 s wait at `0x021FE140`). A shot's damage
is at `+0xF0`.

**The ship**: four parts per pilot (`3D/Starfox/…`), all at the same scale; collision uses one shared volume
(`Arwing`). At the start of a battle, the player creation (ovl0 `0x02188F1C`) loads the files of the pilot's
key only (`0x0221F828`), then the vehicle reads its four model names and its contrail positions from the
record (ovl2 `0x021FE61C` to `0x021FE78C`).

**The Arwing's dynamics** (`ArwingDynamics`, 676 bytes): health at `+0xB8` (20.12; 100 = `0x64000`), its
maximum at `+0x10` (60, 100 or 130 depending on the shield), copied into the record every frame (`+0x68`); the
gauge of boost, brake and barrel rolls — a single one — at `+0xBC` (the part used, 0: full) out of `+0xC4`
(25, 50 or 75: the dynamics asks for it while being built, through the vehicle's method `+0x14`, `0x021FE9D8`,
which derives it from the size stored in the record, `+0x54`). `0x021EDBC4` refills it by 0.5 per tick and adds 1.0 per tick of boost, brake or roll; when
full it locks for three ticks (`+0x10C`) and the cockpit interrupts the command. The pilot is at `+0x84`, the
actor at `+0x20`; the player's actor is `[[0x021D3260]] − 0x1C`.

**Damage** goes through two functions. The Arwing's, ovl2 `0x021F071C` (r0 the dynamics, r1 the actor hit,
r2 the damage): shots, missiles, collisions with an enemy, the ground or a building, boss attacks — and only
there does it die. That of enemies and bosses, ovl0 `0x0216CC8C` (r0 the actor, r1 the damage, r2 the
attacker, often zero): health at `+0xE8` (the `SetLife` value times 4,096), death through `0x0216D344`. Damage
comes from the `+0xF0` of whatever hits; the charged shot does 20 × {0, 1, 2, 4, 8, 16} depending on its
target's level (`0x021E5EB8`), the bomb 100. Collision types: 0 player, 1 player shot, 2 enemy, 3 enemy shot,
4 scenery object, 5 item, 6 building, 7 scenery attack.

### Miscellaneous

- **`0x02075DDC`**: SDK vector math (64-bit multiply-accumulates),
  in ARM mode, translated correctly.
- The game's save file takes up **908 bytes, from 0x09834 to 0x09BBF**, signature
  `Manta_2_0710a`. The first word (0x09834) is the header the game reads back to
  recognize a saved game.
- Projection in combat: near plane 8.0, far plane 16,384.0. **The matrix
  is normalized to a quarter** (m11 = −1024): without correcting this factor, all
  distances read four times too short.

---

## 3 bis. The hardware registers handled

Addresses relative to `0x04000000`. This list mirrors the code: a register
that is absent is one the engine ignores, which may be deliberate (the game
does not use it) or a gap to fill if a defect points to it.

### Display and VBlank

| offset | name | note |
|---|---|---|
| `000` / `1000` | DISPCNT | mode, active layers; `1000` for the second engine |
| `004` | DISPSTAT | interrupt masks; the VBlank state is shared by both processors |
| `006` | VCOUNT | current scanline |
| `008`–`00E` | BGxCNT | priority, size, tile base |
| `010`–`01E` | BGxHOFS / BGxVOFS | scrolling |
| `020`–`03E` | BGxPA–PD, BGxX/Y | rotation and scaling backgrounds |
| `040`–`04A` | WINxH/V, WININ, WINOUT | windows |
| `050`–`054` | BLDCNT, BLDALPHA, BLDY | fades and transparency |
| `06C` | MASTER_BRIGHT | brightness |
| `064` | DISPCAPCNT | display capture to video memory |
| `304` | POWCNT1 | power; its bit 15 says which engine is on the top screen |

### 3D engine

| offset | name | note |
|---|---|---|
| `060` | **DISP3DCNT** | fog, alpha test, anti-aliasing. Fixed trap: it used to be read at `320`, where nothing responds |
| `320`–`33E` | EDGE_COLOR, ALPHA_TEST_REF | |
| `350` / `354` | CLEAR_COLOR / CLEAR_DEPTH | |
| `358` / `35C` | FOG_COLOR / FOG_OFFSET | |
| `360`–`37F` | FOG_TABLE | 32 seven-bit densities |
| `380`–`3BF` | TOON_TABLE | |
| `400` | GXFIFO | command stream, packed format |
| `440`–`5FF` | per-register commands | one address per command |
| `600` | GXSTAT | matrix stacks, overflow, FIFO |
| `604` | RAM_COUNT | polygons and vertices of the buffer being built |
| `620`–`63C` | POS_RESULT, VEC_RESULT | test results — **never used by this game** |
| `640`–`6A3` | read-back matrices | clip and vector |

### Video memory and DMA

| offset | name |
|---|---|
| `240`–`249` | VRAMCNT_A–I, WRAMCNT; `241` returns WRAMSTAT |
| `0B0`–`0DC` | DMAxSAD, DMAxDAD, DMAxCNT (four channels) |
| `0E0`–`0EC` | DMA_FILL |

### Time and interrupts

| offset | name | note |
|---|---|---|
| `100`–`10E` | TM0–TM3 | reload and control; **trap**: the two halfwords must be kept together, otherwise the reload is lost |
| `130` | KEYINPUT | keys |
| `132` | KEYCNT | |
| `136` | EXTKEYIN | X/Y keys and touch screen (ARM7 side) |
| `208` | IME | master enable |
| `210` | IE | enabled interrupts |
| `214` | IF | pending interrupts; a write acknowledges them |
| `204` | EXMEMCNT | cartridge sharing between the processors |
| `300` / `301` | POSTFLG / HALTCNT | boot, sleep |

### Math

| offset | name | note |
|---|---|---|
| `280` | DIVCNT | mode; writing it restarts the computation |
| `290`–`29C` | DIV_NUMER, DIV_DENOM | |
| `2A0`–`2AC` | DIV_RESULT, DIVREM_RESULT | |
| `2B0` | SQRTCNT | |
| `2B4` | SQRT_RESULT | |
| `2B8`–`2BC` | SQRT_PARAM | writing it restarts the computation, 16-bit writes included |

Verified correct: division and square root give the same result as the
hardware, division by zero included.

### Inter-processor communication

| offset | name |
|---|---|
| `180` | IPCSYNC |
| `184` | IPCFIFOCNT |
| `188` | IPCFIFOSEND |
| `100000` | IPCFIFORECV |

### Cartridge and save

| offset | name | note |
|---|---|---|
| `1A0` | AUXSPICNT | **trap**: bit 6 announces what will happen at the end of the *next* transfer. Closing the transaction as soon as it was written lost one byte per transfer |
| `1A2` | AUXSPIDATA | one byte per write |
| `1A4` | ROMCTRL |
| `1A8`–`1AF` | card command (eight bytes) |
| `1C0` / `1C2` | SPICNT / SPIDATA | power, firmware, touch screen (ARM7) |

### Sound (ARM7)

| offset | name |
|---|---|
| `400`–`4FF` | sixteen channels: control, source, timer, loop point, length |
| `500` | SOUNDCNT | master volume, enable |
| `504` | SOUNDBIAS |

---

## 4. Data formats

| format | tool | note |
|---|---|---|
| `.mdl` models (MGL) | `tools/nitro/mgl.py`, `export_models.py` | 2,176 models, all decoded without error |
| display lists | `tools/nitro/displaylist.py` | same commands as the 3D engine |
| level scripts | plain text | readable as is |
| textures | `SFC_DUMP_TEX`, `tools/ntfq.py` | 326 exported |
| `.hgt` collision | `tools/export_collision.py` | height maps, per cell |
| 2D backgrounds `.nbfc`, `.nbfp`, `.nbfs` | `tools/fond_2d.py` | tiles, RGB555 palette, screen map; as PNG |
| texts | `tools/bmg.py` | reading and rewriting (`--texte`, `--reecrire`) |
| sound | `tools/sdat.py` | |
| `-cmp` compression | `tools/nitro/compress.py` | LZ10 in both directions (`lz77_compress`: no reference at distance 1) |
| data index (`data/index.bin`) | `tools/build_data.py` | magic `SFCD`, then version, number of zones and size of the names (three words); one `<offset, size, name position>` entry per zone; the paths follow, separated by line breaks |
| a game file, in `data/` or a `.srl` archive | `tools/fichier_du_jeu.py` | `liste`, `lire`, `poser`; a file that grows is placed after the last one, its end corrected in `fat.bin` and the index — see [the procedure](assets.md) |

**Vertex format trap**: `VTX_DIFF` adds its ten signed bits **as they
are** to the 4.12 coordinates, without scaling. The factor has to be measured, not
guessed: in a mesh, neighboring faces share their edges, so a vertex placed
relatively must land exactly on an absolute vertex of the same
model — that is the case for 76% of them with no shift, against 3.6%
shifted by one notch. 411 models out of 2,176 use this format, including the enemy
fighters and none of the player's Arwings.

---

## 4 bis. The keys

The keyboard keys can be changed in the launcher, on the Controls page: click a command, then
press its new key; a key already in use moves to the other command, swapped. The launcher keeps them in
`lanceur.ini` (section `[touches]`) and passes them to the game through `SFC_TOUCHES`. The original ones:

| Key | What it does |
| --- | --- |
| `Z` `E` | A, B |
| `A` `S` | X, Y |
| `Q` `W` | L, R |
| arrow keys | D-pad |
| `Espace` (Space) `Tab` | Start, Select |
| left click in the bottom half | the stylus |
| gamepad | see below |
| **`F` held** | **fast-forward**: the game thinks eight times faster, to get through a scene that cannot be skipped (the ending). Nothing else changes — it is the same game, sooner; release it and the game returns to its own pace |
| `P` | **note a display glitch**: the last hundred and twenty frames shown, written to the log (which tick, preview or VBlank, the fingerprint of each screen) — press it right after seeing the glitch |

### With a gamepad

Xbox (XInput) and PlayStation, Switch Pro or generic (DirectInput); buttons
are identified by their position. In flight, the controls of Star Fox 64 3D:

| Xbox | PlayStation | in flight | elsewhere |
|---|---|---|---|
| left stick | left stick | steer: pushed, the Arwing climbs; pulled, it dives | the D-pad |
| A | ✕ | laser (the console's A) | A |
| X | □ | boost, held | X |
| B | ○ | brake, held | B |
| RB, RT / LB, LT | R1, R2 / L1, L2 | barrel roll right / left | RB, LB: R, L |
| stick toward climb + X | stick toward climb + □ | loop | |
| stick toward climb + B | stick toward climb + ○ | U-turn | |
| right stick | right stick | up: loop; down: U-turn; sides: barrel roll | |
| Y | △ | bomb, dropped at the Arwing's position, if any are left | Y |
| D-pad, Start, Back | D-pad, Options, Share | the console's | the console's |

Outside flight — map, menus, pause — the gamepad does not drive the stylus: what
can only be done by touch is done with the mouse. A button held at the moment of
entering or leaving flight only counts once released; in flight, the mouse takes
precedence as soon as it touches down. Vertically, pushing the stick climbs,
like the stylus dragged upward; the launcher offers the Star Fox 64 direction,
where pushing dives and pulling climbs (launcher, Controls page). The loop and the U-turn, which
begin by climbing, are requested with the stick toward climb. The bomb is launched with Y,
dropped at the Arwing's position, if the pilot has any left.

## 4 ter. The access inventory (`tools/structures.py`)

The rewritten code is faithful but nameless: it handles raw offsets. To modify the game —
a ship, a story branch — you first need to know what it touches. The tool reads the code
produced by the generator and lists, without running it, every memory access at an offset.

| command | what it gives |
| --- | --- |
| `python tools/structures.py` | the complete inventory in `analysis/acces.json` (20,683 functions, 266,462 fields) |
| `… --adresse 0x021D1E94` | all the functions that touch this object and the two hundred bytes after it, offset by offset |
| `… --fonction 0x02178320` | what this function touches, grouped by the base the offsets start from |
| `… --types` | the bases that receive exactly the same offsets: most likely one and the same type |
| `… --objets` | crosses the inventory with a runtime record (`SFC_ETUDE_OBJETS=file`): real objects, at their address, with their fields and the functions that use them |
| `… --dictionnaire` | **the game's types**: the objects that the same functions handle, with their size, their fields, what their first forty-eight words contain and their uses (`analysis/types.json`) |
| `… --nommer` | what the documentation can already say about the types found: a type handled by a described function inherits its description |
| `… --suivi` | what becomes of each field of an object tracked frame by frame (`SFC_SUIVRE_OBJET`) |
| `… --document` | writes [`docs/dictionnaire.md`](../dictionnaire.md) (in French): the sixty most used types, with their shape and their functions |

Records from several scenes are merged: every `analysis/releves/*_par_objet.txt` file is read
along with the current record. A scene only shows its own objects — flight ignores the menus, and vice versa.
Over ten records — a flight, a complete session, the menus, the story, strategy mode, a long
session taking two missions up to their bosses (Star Wolf, then Solar), and three automatic
explorations (`tools/explorer.py`): **43,720 objects seen, 422 types**, 26 of which inherit a
description.

A note on method, measured: an automatic exploration reports **732 objects**, a session
played by someone who knows where to go reports **8,431**. The explorer can open doors,
not finish a mission — it sweeps through the menus when nobody is there, nothing more. Two of them carry the game:

| objects | size | functions | calls recorded |
|---|---|---|---|
| 2,232 | 128 B | **286** | 15,736,457 |
| 2,531 | 64 B | 171 | 2,981,090 |

The second begins with four pointers and repeats its shape every sixteen words — that is, its 64 bytes.

What `--types` found on the first try: 1,719 families, including one of 151 bases in overlay 11
at offsets `+0x3C, +0x40, +0x44, +0x60, +0x62, +0x64` — three words then three halfwords, the shape
of a position followed by angles. That is how you name a field without reading a line of code.

What the inventory alone does not see: *what* a function applies these offsets to. That is what
`SFC_ETUDE_OBJETS=file` records during play — for each function, the addresses received as first
argument, their range and the number of calls. Crossed (`--objets`), the two give real objects:
on the rings mission, 29 memory zones recognized, including a heap at `0x022F0000` where 121
objects spaced at most twelve bytes apart are handled by 118 functions and carry 81 fields.

The record also keeps the other direction, per object: the functions that received it. Two objects that the
same functions handle are of the same type; `--dictionnaire` groups them that way (first on
the exact set, then by merging the groups that share more than half of their functions),
and reads their size from the smallest gap between two neighbors.

On the rings mission: **296 types, out of 3,411 objects seen**. The most used:

| objects | size | near | functions | calls | fields |
|---|---|---|---|---|---|
| 289 | 64 B | `0x02290180` | 41 | 6,611 | 14 |
| 255 | 128 B | `0x022BE640` | 93 | 91,035 | 43 |
| 227 | 32 B | `0x022822A0` | 26 | 82,247 | 11 |
| 145 | 192 B | `0x022E8900` | 87 | 182,662 | 46 |

The record also looks, every so often, at what the first forty-eight words of an
object contain, and classifies them: `p` a pointer, `n` a small integer, `x` a world length (20.12, with
a fractional part — position, speed, angle), `.` always zero, `?` anything. So the 64-byte type
near `0x02290180` begins with two pointers and carries three world lengths around `+0x30`.

`--nommer` takes the first step: a type handled by a function that the documentation describes inherits
its description. Six types come out named, including the one of **192 bytes, 145 objects** — touched by
`0x021B3E9C` and `0x021B594C`, that is, by the **collision volumes** system. So it is a physical
entity of flight.

The classification counts the kinds seen rather than adding them up, and only keeps the one that
clearly dominates — a position field is sometimes zero, sometimes a small number, and remains a
position. The shapes come out readable; that of the 128-byte type repeats at the stride of its
size, which confirms it:

```
128-byte type, 259 objects, 90 functions
pp.p ??.n ..n? x?nn ..n. .ppp .... ....   then again  pp.p ??.n ..n? x?nn
```

### Naming a field by watching it live

`SFC_SUIVRE_OBJET=0xADDRESS[,file]` records the first forty-eight words of an object **at every
frame**, and `tools/structures.py --suivi` tells what becomes of each one: a field that never moves is
an identifier or a pointer; one that goes up by one at each frame is a counter; one that varies a little
at each frame and a lot over time is a world quantity. Three in a row that vary
by the same order of magnitude form a vector.

**Three traps met on the first try, and what they taught** — they are worth more than the first
result, because each one produced a false yet plausible conclusion:

- **An object has several lives.** `0x022E8900` sat at zero for 2,400 frames, then received a
  table of names. Averaged over everything, its field `+0x24` looked like “a world quantity varying by 143
  per frame” — a ship's speed. It was the jump from zero to `0x72617453`, that is, to the
  letters `Star`, then `fish` and `Shot`. So a field is only judged on its longest stretch
  of non-zero frames, and four readable bytes are now recognized as text.
- **A buffer is not a structure.** Picking the object whose fields all carry quantities
  yields a work buffer: its fields all started from `0x11111111`, the pattern the game fills
  fresh memory with.
- **A stack frame is not an object.** Addresses at `0x027Exxxx` receive thousands
  of calls and change shape on every pass: the record now only keeps `0x02000000`–`0x023FFFFF`.

What remains to be done: find a real flight entity — one of the 192-byte type that the
collision functions handle — and track it during a flight, crossing its position with what
the 3D engine draws.
What the tool still does not see: objects reached through a pointer kept elsewhere, never
passed as first argument.

## 5. Environment variables

### Everyday use

| variable | effect |
|---|---|
| `SFC_ARM7=1` | runs the real coprocessor (needed for sound) |
| `SFC_DATA=folder` | where to find the game data; otherwise `data/` next to the executable |
| `SFC_TOUCHES=a=0x5A,b=0x45,…` | the keyboard keys, command by command, by their Windows virtual-key code: `a` `b` `x` `y` `l` `r` `start` `select` `haut` (up) `bas` (down) `gauche` (left) `droite` (right), `rapide` (fast-forward) and `journal` (note a display glitch); a missing command keeps its original key, a zero code gives it none. The launcher sets it (Controls page), and the log repeats it |
| `SFC_MOD_LASER=pilot:type,…` | the laser of each named pilot's ship (`fox:plasma,slippy:double`; types `simple` (single), `double` (twin), `plasma`, `courbe` (curved shot), `zapper`; without a pilot: Fox), whatever the game gives him (§ 5 ter, “The launcher's mods”) |
| `SFC_MOD_VERROUILLAGE=pilot:type,…` | his lock-on (`simple` single, `multiple` multi, `large` wide) |
| `SFC_MOD_JAUGE=pilot:size,…` | the size of his boost gauge (`petite` small, `moyenne` middle, `grande` large) |
| `SFC_MOD_VAISSEAU=pilot:pilot,…` | the look of his ship: that of the pilot named second (`slippy:falco`); the keys: `fox`, `krystal`, `falco`, `slippy`, `ash`, `wolf`, `leon`, `panther`, `octman`, `bill`, `lucy`, `amanda`, `cat`, `peppy`, `james`, `jodie` |
| `SFC_MOD_DEGATS=factor\|un-coup`, `SFC_MOD_DEGATS_RECUS=factor` | the damage the player deals, receives (factor from 0.1 to 100; `un-coup`: one hit kills) |
| `SFC_MOD_INVINCIBLE=1`, `SFC_MOD_TURBO=1`, `SFC_MOD_BOMBES=1`, `SFC_MOD_CHARGE=1` | invincible; infinite boost and brake; infinite bombs; charged shot always at its strongest |
| `SFC_TRACE_FICHIER=1` | logs every file and every overlay the game requests from the card reader, with its original name |
| `SFC_VERIF=0x…` | compares a native rewrite with the translation, call by call (`tout` for all of them) |
| `SFC_VERIF_MAX=n` | number of calls compared before leaving the translation alone (2000 by default) |
| `SFC_NATIF=0x…` | has the rewrite replace the translation, for good |
| `SFC_SAVE=path` | moves the save file — **not to be used for playing** |
| `SFC_SCALE=1..4` | supersampling (anti-aliasing) |
| `SFC_FILTER=0..2` | texture filtering |
| `SFC_AFFICHAGE=libre` | the display gets its own thread and interpolates between two game frames, while the game keeps its pace (`docs/plan_framerate.md`) |
| `SFC_IPS=n`, `vsync`, `0` | cap on frames displayed per second in free display: a number (240 by default), the screen's rate, or none |
| `SFC_APERCUS=0` | removes the preview ticks: while the game waits for the time of its next tick (the twenty milliseconds of flight), it computes that tick ahead of time with the current controls, the display glides toward it, then everything is restored — memory, registers, 3D engine, link — and the real tick plays out as on the console |
| `SFC_LATENCE=0..1`, `auto` | in free display, the fraction of a game tick held back as delay: 0 shows the latest game frame as soon as it arrives and extends its motion, 1 waits for the next one so as to sit between the two (smoother, one tick of delay on the controls). By default, `auto`: 1 in scenes at sixty ticks per second (menus, strategy), 0 in flight (thirty) |
| `SFC_AFFICHAGE_BRUT=1` | free display without interpolation: the game frame as it is |
| `SFC_PORTEE=factor` | the draw distance, from 1 to 4: the game draws up to that many times farther, and its horizon ring moves back by as much (§ 3, “The draw distance”) — 2 is the launcher's value; beyond that, a background element overflows. Shots also carry farther: Arwing lasers and enemy shots live that many times longer, and enemies whose firing depends on distance fire from that many times farther, without going beyond the view (§ 3, “The fighters' range”). **This is no longer the original game**: the rules and the pace remain, the fine timing changes, and a recorded session only replays at its own draw distance |
| `SFC_NO_AUDIO`, `SFC_NO_VIDEO` | turn off the sound, the display |
| `SFC_MAX_FRAMES=n` | stops after n frames (disables pacing) |
| `SFC_INPUT_REPLAY=f`, `SFC_INPUT_RECORD=f` | input replay and recording — one line per frame, “frame keys stylus x y”, and the gamepad in five more columns when there is one |
| `SFC_MANETTE=0` | ignores gamepads |
| `SFC_MANETTE_INVERSE=1` | the stick's vertical axis in the Star Fox 64 direction: pushing dives, pulling climbs (by default, pushing climbs, like the stylus); the launcher offers it, Controls page |

### Measurement

| variable | effect |
|---|---|
| `SFC_PROFILE=1` | time per phase, spikes, texture counters; and every six hundred frames, the share of a core taken by each of the port's threads (“fils : jeu, affichage, 2D 1 à 4”, i.e. threads: game, display, 2D 1 to 4) and by those of the system and the driver (“autres”, i.e. others) |
| `SFC_PROFILE_ARM9`, `SFC_PROFILE_ARM7` | sampling by return address |
| `SFC_WAV=f`, `SFC_WAV_FROM=n` | records the sound — the reference proof |
| `SFC_STAT_CLIP` | scenery distance, far-plane clipping |
| `SFC_TRACE_PROJ`, `SFC_TRACE_TRANS`, `SFC_TRACE_TILES` | projection, terrain cells |

### Diagnostics

| variable | effect |
|---|---|
| `SFC_TRACE_CALL=0x…` | logs a function's **indirect** calls and what it returns |
| `SFC_TRACE_CALL_MAX=n` | log cap |
| `SFC_DUMP_MEM=0xadr:n` | dumps a zone at regular intervals |
| `SFC_POKE=0xadr:0xval` | forces a word at every frame, to test a hypothesis |
| `SFC_WATCH_VALUE=0x…` | logs the writing of a value, with the call stack |
| `SFC_FIND_VALUE=0x…` | scans the RAM for a value |
| `SFC_DUMP=n`, `SFC_DUMP_FROM=n` | PNG captures |
| `SFC_RAM_IMAGES=frame,frame…` | the whole main memory at the end of these frames, in `SFC_RAM_PREFIXE` + frame + `.bin` (`build/essai/ram_` by default): two dumps that only one command sets apart tell where the game stores it |
| `SFC_2D_IMAGES=frame,frame…` | what the two 2D engines compose at the end of these frames, even without a window, in `SFC_RAM_PREFIXE` + `2d_A_` / `2d_B_` + frame + `.ppm` (without the 3D): separates a defect in the 2D composition from a defect in the display |
| `SFC_MANETTE_ESSAI=f` | a scripted gamepad, with or without a window: one line per change, “frame lx ly rx ry buttons” (sticks from −1 to 1, y up; buttons `MANETTE_…` from `manette.h`, in hexadecimal). Without a window, it is added to the replay and plays alone once the replay stops; in a window, the replay replaces it for as long as it lasts |
| `SFC_TRACE_INPUT=1` | every change of the keys and the stylus; with the gamepad, every entry into flight and every exit (the depth of the scene stack, the name of the open window) |
| `SFC_AFFICHAGE_ESSAI=n` | the interpolation test: from frame `SFC_DUMP_FROM` on, n sub-frames between nine consecutive 3D frames, and what changes in the image from one step to the next (`SFC_AFFICHAGE_ESSAI_PNG=1` saves them) |
| `SFC_RAPIDE_JUSQUA=frame` | a replay runs unpaced up to this frame, then returns to real time |
| `SFC_CADENCE=1` | keeps the console's pacing despite `SFC_MAX_FRAMES`: a replay unfolds like the session it replays (otherwise the sound is no longer real-time and presses no longer land at the same point — a route traced with the stylus misses by one frame and the mission does not start), and the automatic stop remains useful |
| `SFC_DIAG_DEFORME=s` | from 3D frame number `s` on (1 for everything), the pairings that deform: the ratio between the most stretched and the most shrunk edge of a polygon between the two game frames, its place on screen, the stage that paired it. A wrong match changes each edge its own way; an approach or a rotation changes them together |
| `SFC_RAIDE=0` | removes the safeguard: pairs that deform get their shape interpolated again, as before. For comparison |
| `SFC_REALIGNER=0` | removes the second pass of whole-object pairing (an object recognized index by index when some of its polygons change shape in place), for comparison |
| `SFC_DIAG_COULEURS=1` | each displayed polygon takes the color of the path the interpolation makes it follow: red unmatched, blue disappeared, yellow rigid, cyan flipped, magenta paired by neighborhood, green “grows fast”, orange paired by its shape alone, white paired by its index but changed in shape; whatever keeps its own colors was recognized along with its whole object |
| `SFC_DIAG_BLOCS=s[:n]` | over 3D frames s to s+n, the objects of eight or more polygons, recognized as a whole or not, and for those that were not, how many of their polygons changed shape at the same index; and, everywhere, the links where the whole-object stage recognized almost nothing |
| `SFC_DIAG_RANGS=s` | at 3D frame s, the first object of more than a hundred polygons that was not recognized as a whole, index by index: shape kept or changed, paired with what and by which stage, how many polygons of the same shape in the previous frame, and the nearest one |
| `SFC_DIAG_FILES=de[:a]` | every frame the free display receives between these game frames: its tick, the screen it goes to, whether it is a preview or an early publication, and the last keys of the two screen queues — to spot a frame filed under the wrong screen |
| `SFC_2D_MONOTONE=0` | the frame whose 2D the free display shows can go backward again (former behavior, for comparison): when the game stalls and its 3D passes for still, the screen without 3D used to go back one frame on resuming |
| `SFC_2D_VRAIES=0` | the screen without 3D can again show the 2D of a preview tick (former behavior, for comparison); by default it only shows real game frames |
| `SFC_DIAG_VIVANTE=de[:a]` | every time a screen switches from “live 3D” (interpolated) to “still” and back: keys, gap, age of the last frame, threshold, clock locked or not |
| `SFC_DIAG_BAS=de[:a]` | the fingerprint of the bottom screen's 2D layers at each change, its source (real frame or preview), and the count of back-and-forths — a return to the fingerprint from two changes back is a flicker |
| `SFC_AFFICHAGE_FILM=n` | the film: the n frames displayed from frame `SFC_DUMP_FROM` on, every one of them, at their own pace (`build/film_nnnn.png`), and for each one the time, the tick and the game frames it shows (`build/film.txt`) |
| `SFC_APERCUS_ESSAI=k`, `SFC_APERCUS_ESSAI_N=n`, `SFC_APERCUS_ESSAI_FIXE=1` | unpaced (a verified replay), n preview ticks at each of the game's waits: the test that a preview erases itself completely — the fingerprints must not move |
| `SFC_SUIVRE_OBJET=0xADDRESS[,file]` | the first forty-eight words of this object at every frame (`tools/structures.py --suivi` tells from them what becomes of each field) |
| `SFC_ETUDE_OBJETS=file` | during play, for each function called: the addresses it receives as first argument, their range and the number of calls. Crossed with the access inventory (`tools/structures.py --objets`), this gives the game's objects and their fields |
| `SFC_ETUDE_ACC=file` | observes each accumulating write (x = x + v) of the game: how many times, in how many ticks, with what step, and whether the same address is written again within a tick (`tools/accumulateurs.py` derives from it the list of scaled sites) |
| `SFC_APERCU_SOUPCONS=file` | which scaled sites were present in the abandoned previews, and in those that completed |
| `SFC_APERCUS_ESSAI_BUDGET=ms` | a tight time limit for the test's previews: many are abandoned mid-work — the test that abandoning restores the state as well as completing does |
| `SFC_APERCU_2D=0` | a preview tick reuses the 2D layers of the last VBlank instead of composing its own: what it draws in 2D is a prediction, and an element that blinks from one tick to the next flickers when the prediction is wrong |
| `SFC_DIAG_2D_APERCU=frame` | from this frame on, the 2D of a preview is compared with the one the real tick will draw: the share of correct predictions, and for the first wrong one, the pixels that differ per screen and per layer, plus the images (`build/2d_*.png`); also reports each time a preview announces the two screens in the other order |
| `SFC_APERCU_VBLANK=1` | experiment: give the preview the VBlank interrupt that its sequence skips. As it stands, it stops the previews dead — to be revisited |
| `SFC_APERCU_ECARTER=0` | shows preview frames even when the display gets rearranged during their tick (screen order that stops alternating, an engine's mode that changes) — this rearrangement is what gave the same screen twice, or an offset, for a split second |
| `SFC_DIAG_ECRANS=1` | when both screens receive the same 3D geometry — which does not happen on the console, where the 3D engine only feeds one at a time: where each one took its image from, and a screenshot (`build/jumeles_NN.png`) |
| `SFC_DIAG_OSCILLATION=1`, `SFC_DIAG_OSCILLATION_SUITE=frame` | is the frame about to be shown the one from two frames back, while the previous one was different? That is a back-and-forth, what the eye calls a flicker; the follow-up gives, frame by frame, the fingerprint of the 2D layers and the order of the screens. The capture pass, drawn without being shown, does not count |
| `SFC_DIAG_COMPTEURS=1` | what a preview tick does not predict: the memory it leaves is compared with that of the real tick at the same point — the words that are exactly one higher there (counters it did not advance, since the VBlank is skipped), and the count of differences per zone |
| `SFC_DIAG_ACOUPS=ms` | game ticks longer than that (40 ms by default), with how much of them the preview ticks took and how far they bit into the VBlank |
| `SFC_APERCU_VERIF=1` | after each preview tick, the whole memory and the registers are compared with what they were before it; at the first byte that moved, the address, the zone and the page are reported, and the session stops. Slow (five megabytes copied per preview), but it is the test that shows a preview leaves nothing behind |
| `SFC_DIAG_APERCUS=1`, `SFC_DIAG_APERCU_SIG=frame` | the preview scheduler; the “position” of each 3D frame, real or preview |
| `SFC_ECHANTILLONS=file`, `SFC_ECHANTILLONS_DEPUIS=frame` | where the host machine's time goes, by sampling (`tools/echantillons.py`). Wall-clock time, waits included: sleeping shows up under `attente_dormir_ns`. Under PowerShell, redirecting the log with `Start-Process -RedirectStandardError` makes `vfprintf` look costly: go through `SFC_JOURNAL`, as the launcher does |
| `SFC_ECHANTILLONS_FIL=affichage` | samples the display thread instead of the game thread |
| `SFC_BANDES_VERIF=1` | each 2D screen, composed in bands on the worker pool, is recomposed in one piece and compared byte by byte; the log counts the screens that differ (none on the player's session) |
| `SFC_NO_PARALLEL=1` | the two 2D screens are composed in one piece on the game thread, without the worker pool |
| `SFC_INTERPOLATION=cpu` | interpolation between two game frames goes back to the CPU (`interpoler`), for comparison with the geometry shader; `SFC_DIAG_COULEURS` goes back to it too. `SFC_PROFILE` reports, every five seconds, how many batches and polygons a screen has and how many plans get built |
| `SFC_INTERPOLATION_EPREUVE=echec` | simulates a mismatch in the startup test of GPU interpolation: to check that it does fall back to the CPU, and that the log says so |
| `SFC_CHRONO_ARM7=1` | the host time spent in the coprocessor |
| `SFC_AFFICHAGE_DIAG=3`, `4`, `5` | the timeline of the snapshots the game drops off; where the time goes in a display-thread iteration of more than 6 ms; each wait for the next frame and each readjustment of the clock |
| `SFC_DUMP_POLYS=n`, `SFC_DUMP_TEX` | polygons, textures |
| `SFC_OBSERVER=0x…[,0x…][:max]` | every call to these functions (eight at most), direct or through the dispatch: the frame, the caller, r0 to r3, the top of the stack, and what it returns; `max` caps the log (2,000). It is the first lever for finding “where X is decided”: intercept the suspected function and look at what it receives. Running native, the game sometimes calls a relay rather than the function (`0x01FFBF84` for `0x01FFBF8C`): observe the address the caller names |
| `SFC_OBSERVER_DEPUIS=frame` | only starts at this frame |
| `SFC_OBSERVER_CHAINE=1` | adds the return addresses of the calls in progress, from the nearest to the farthest |
| `SFC_OBSERVER_LIRE=r2+4:3,0x027E14F8:6` | adds what the arguments point to — here three words at `r2+4` and six at a fixed address: a structure passed by address says nothing through its address alone. Brackets follow a pointer, and can be combined: `[[r0+0xC]]+0x20:3` reads the position of the actor designated by the reference stored at `r0+0xC` |
| `SFC_DIAG_LOIN=frame[:distance]`, `SFC_DIAG_LOIN_N=n` | at this frame, the displayed polygons farther away than the distance (7,800 by default): how many, where they fall on screen, their texture and their attributes (the first n) |
| `SFC_DIAG_OBJETS=frame[,frame…]` | the position of each object in the list the scene rendering walks through, drawn or not: two records tell which objects move, and at what distance |
| `SFC_DIAG_CAMERA=n` | the camera position every n frames (the current `Camera` object: `[0x020CF430+0x10C]`, position at `+0x18`) — a witness of the session that depends neither on addresses nor on what is drawn, to date the frame at which two sessions diverge |
| `SFC_TRACE_BACKUP`, `SFC_TRACE_IPC`, `SFC_TRACE_SOUND`, `SFC_TRACE_GX` | per-subsystem traces |

**Limit to be aware of**: `SFC_TRACE_CALL` only sees **indirect** calls.
The translated code calls directly when the target is known, and those calls
do not show up. To observe a function that is called directly, you have to
replace it with a native hook (`rt_hook`) — which replaces it instead of
accompanying it, so you have to reproduce it.

### Scheduling settings — not to be touched lightly

| variable | default | note |
|---|---|---|
| `SFC_EDGE_BUDGET` | 256 | beyond that, the coprocessor goes silent |
| `SFC_ARM7_MIN` | 8 | 16 makes the sound diverge, 32 changes it entirely |
| `SFC_ARM7_CPE` | 8 | coprocessor cycles per edge |

---

## 5 bis. Decompiling with proof

Rewriting one of the game's functions in C is only worth something if you can
show that it does exactly what the translation of the binary does.
`runtime/natif.c` plays that role: on every call to the targeted function, it
saves the machine state, runs the translation, restores the previous state,
runs the rewrite, then compares registers, RAM, DTCM, ITCM and hardware
registers.

The game always keeps the result of the **translation**: a wrong rewrite
reports itself without breaking anything, so the checking is done by playing.

Two precautions learned in practice:

* **What lies below the stack pointer does not count.** The translation pushes
  its return address there, the rewrite does not have to, and those bytes are
  dead as soon as the function returns. Comparing them would make every correct
  rewrite fail.
* **The hook must be able to remove itself for the duration of a call**,
  otherwise the translation it replaces would call itself again. Since the
  generated code calls a hook without telling it which function it replaces,
  each rewrite has its own.

The bench itself gets tested: you introduce a deliberate fault and watch
whether it sees it. A bench that never says anything proves nothing.

### What a rewrite must reproduce

A function does not return just a value. Everything below was discovered by
watching the bench reject rewrites that seemed correct:

| what must be returned | why |
|---|---|
| the registers, including `lr` | a `blx` writes the return address; the caller may read it |
| the **N Z C V** flags | the caller branches on them; `adds r0, r4, #0` sets them, even when it computes nothing |
| the **T** bit on exit | `pop {pc}` hands the caller back the mode of its return address |
| the **entire stack frame** | see below: this is what took the most effort to understand |

### The stack frame is not a formality

A native rewrite needs neither to save registers nor to carve out a frame: it
has its variables. Yet it must do both, and redo the `pop` to the letter.

* **The `push` really writes.** Those bytes stay in memory after the return,
  and the game sometimes copies a buffer without having filled it entirely:
  different garbage ends up in live data.
* **The carve-out (`sub sp, #n`) shifts everything that follows.** While it is
  open, the callees set up their stack lower down. Not carving it out shifts
  the stack for the whole rest of the execution.
* **The `pop` reads back.** It does not give back the registers as they were on
  entry: it reads whatever is *now* in their place. An interrupt that occurred
  during the function may have written there — and it is then the value read
  back that the game uses. The same goes for the return address: execution
  returns to wherever the stack says to return to, via `rt_jump`, which
  recognizes a genuine return and otherwise follows the jump.

This last point showed up in the most baffling way possible: the function was
correct — four thousand calls compared without a single difference, dead stack
included — and yet the game diverged, eleven frames later, on a stack word
written in interrupt mode. The three rewrites only became indistinguishable
once the `pop` was imitated in full: **6,000 frames, identical fingerprints,
entire memory included**.

### The third trap: what the bench does not restore

The singleton `0x021A9908` in the actors overlay — guard, construction on the
first pass, registration of the destructor — passed 848 calls out of 849 and
diverged on the first one, by a single word: the constructor incremented it
twice on our side, only once in the translation, with the same arguments and no
extra call.

The cause was not in the rewrite. The constructor sends a message to the
coprocessor through the communication FIFO (`0x04000184`, `IPCFIFOCNT`), and it
is the reply, handled by the engine, that writes this word. The bench restores
the game's memory — **not the communication queue, nor the coprocessor's
state**, which live in the engine. The second run still found the first one's
reply there.

Rule: a function that, directly or indirectly, talks to the coprocessor is not
judged on the bench. It is judged by the fingerprints, which run it only once.
This one passes that test: with the seven rewrites active, **3,000 identical
frames**.

### A mission bench

The investigation into the rings had left behind a save file and a recording
made in flight; `build/mission_anneaux.sav` and `build/mission_anneaux.txt` are
their stable copy. Replayed, the recording reproduces the sequence of overlays
to the frame — strategy and briefing at frame 999, flying the Arwing at frame
2,442 — and two runs give the same fingerprints. The seven rewrites remain
indistinguishable there over 4,400 frames.

This is what opens up the combat code: in flight, the most-called targets are
those of the actors overlay — `0x021B3FD0` four hundred and twenty-two thousand
times, `0x021B594C` two hundred and forty-seven thousand, `0x0216DADC` two
hundred and eleven thousand.

### The tools this investigation left behind

| variable | what it gives |
|---|---|
| `SFC_SNAPSHOT=frame:prefix` | writes out memory at that frame; two snapshots can be compared byte by byte |
| `SFC_VERIF_PILE=1` | the bench no longer overlooks anything, not even the dead bytes below the stack pointer |
| `SFC_TRACE_NATIF=n` | logs the entry and exit of the first n calls, in both modes, to compare line by line |
| `SFC_WATCH=0x…` | logs every write that changes this word, with the mode and the stack at that moment |
| `SFC_WATCH_DEPUIS=frame`, `SFC_WATCH_MAX=n` | arms the watch only from that frame on, and removes it after n writes: without them the page stays protected from startup, and a page of constantly written variables makes the game very slow before it even reaches the moment you want |
| `SFC_WATCH_LECTURES=1` | also watches **reads** of the word: each place that reads it is reported once, with the stack, then every thousand reads — to find out who uses a setting. Use it under the translation (`SFC_NATIF=non`): its code reads memory inline and the reading function can be deduced from the host address, whereas the native code goes through runtime helpers |
| `SFC_STAT_CALL=n` | the n functions most often reached through an indirect call — what the game really calls |
| `SFC_PROFILE_FROM=frame` | profiles only from that frame on — a flight is hard to read under two thousand frames of menus |
| `SFC_PAS_MS=n` | replaces the 20 ms of the main loop's limiter; **this is no longer the original game**, it is an experiment |

None of these four would have been enough alone: the snapshot says *where*, the
watch says *who*, the trace says *when*, and the bench says *what*.

And two rules for the calls the rewrite makes:

* **call directly, without going through the dispatch**: the dispatch forces the
  T bit, whereas the translation of an immediate `blx` leaves it as it is — and
  the called code reads that bit when it consults the status register;
* **set the return address** before a call that, in the original, comes from a
  `blx`.

The bench, for its part, must restore **everything** the translation may have
changed before launching the rewrite — the mode and the T bit included. Without
that, the rewrite starts from a state the reference has already modified, and
you spend a long time fixing a correct function.

Established rewrites:

| address | what it does | verification |
|---|---|---|
| `0x0208940C` | releases a reference to an object, and hands the object to the allocator when nobody holds a reference any more | 30,000 calls, 0 divergences |
| `0x01FFEE38` | takes the head of the free list of the current size class | 15,000 calls, 0 divergences |
| `0x01FFEDE4` | returns a block to its list, or passes it up to the level above | 13,000 calls, 0 divergences |
| `0x02088DF8` | looks for the class whose area contains the block; beyond them, returns it to the heap | never called in the menus — not proven |
| `0x0202260C` | reorders its arguments and calls the memory copy | 20,000 calls, 0 divergences |
| `0x021C80F4` | returns 1 if either of two byte flags is set (actors overlay) | 1,290 calls, 0 divergences |
| `0x021A9908` | singleton: builds its instance on the first call, returns it afterwards (actors overlay) | by fingerprints — it talks to the coprocessor |
| `0x021B3FD0` | first element of the list at `+0x28`; the node is embedded 0x1C bytes after the start of the element | 30,000 calls in flight, 0 divergences |
| `0x021B594C` | is the byte at `+0xB8` zero? | 22,106 calls in flight, 0 divergences |
| `0x0216DADC` | byte `+0x4C` of the object pointed to by the first word of field `+0x24` | 18,630 calls in flight, 0 divergences |
| `0x020A0E4C` | **the main loop**: one game tick, and the 20 ms wait in flight | by fingerprints — it reads the clock: 4,400 identical mission frames |

The ten together, replacing the translation: **4,400 mission frames with
identical fingerprints**, entire memory included — and the same execution time,
to within 0.3%. The last three are the most-called functions in flight;
rewriting them faithfully gains nothing, because they do exactly the same work
as their translation.

The actors overlay is loaded permanently — it had been all along. What had
misled me: the overlay trace only follows three slots out of four, and the one
for overlays 0 and 1 is not among them. A function of this overlay can
therefore be decompiled without waiting for a mission; you still have to pick
one that actually gets called, which `SFC_STAT_CALL` tells you and a count of
references does not.

### The generator: the whole game rewritten by a program

Nearly a third of the game's functions are **leaves**: a few Thumb
instructions, no call and no loop. Accessors (“returns `[arg0 + 0x160]`”),
mutators, predicates (“if `[arg0 + 4] == 0`: returns 0; otherwise: returns 1”),
clamps, C++ multiple-inheritance thunks (`this` adjusted to the subclass, then a
jump to the real method), tail calls through a pointer. By hand, they would
never get done; they all have the same shape, and it is a program that rewrites
them — then, since the same machinery held up, the functions that make calls,
and those that loop.

`tools/decomp_feuilles.py` **symbolically interprets** each function. It tracks
what each register holds in terms of the input values, what is read from and
written to memory, which flags remain set; at each conditional branch it splits
in two, and the function becomes a tree of `if`s whose every end is a *path*. A
condition already decided along the path is not tested again, a constant
condition is decided in advance, sums of constants are folded (“`sp - 4`”, not
“`((sp - 0x10) - 0x20) + 0x2C`”). Out of the final state of each path comes C
— reads and writes in the original order, registers and flags set at the end —
and a plain-language description, which will go on to feed the directory: these
thousands of accessors are so many witnesses to the shape of the objects.

A **call** is a point where the symbolic state is written into the processor —
modified registers, flags, return address — then the callee runs, and
everything that was known has to be read again: the registers and flags after
the call form a new generation of values (“result of call 1”, “r4 after
call 1”). The call takes the translation's route, direct by symbol or through
the dispatch, following the very rule of `recomp.py` — the dispatch sets the T
bit, and the called code sees it. After the call, `c->redirect` is checked as in
the translation: a thread switch may resume the function elsewhere, and it is
then the translation, which has all its labels, that carries on.

**ARM mode** follows the same machinery, with its own particulars. A
conditional instruction that terminates nothing becomes a *choice of values* —
“if the condition, the new value, otherwise the old one”, for each register and
each flag it touches, its memory accesses under an `if` — and not one more
path: `cmp ; movge ; movlt` remains a single straight line. Those that
terminate or call (`bxeq lr`, `ldmneia sp!, {pc}`, `blne`) split the path in
two. On top of that come the full shifted operand with its carry, pre- and
post-indexed addressing, `ldm`/`stm` in their four modes, long multiplications,
the DSP's saturating instructions (through the runtime's own functions, which
set Q), `pc` read as a constant, `mov lr, pc ; bx rX` recognized as a call. This
is what opens up the collision module, written in ARM for speed.

Three rules of `recomp.py` are **reproduced identically**, because the rewrite
must take the same routes as the translation:

* **call linking** — direct by symbol when the target is a function, or the
  start of a block, of the ARM9 or of the same region and in the same mode;
  otherwise through the dispatch, which sets the T bit;
* **`bl` used as a long branch** (CodeWarrior, in Thumb) — a target inside the
  function, other than its entry, recognized by the code mapping or more than
  2 KB away with the instruction before it falling through into it; any other
  internal `bl` is a call, recursion included;
* **splitting into ranges** — only the instructions that the code mapping
  assigns to the function are followed; flow that leaves them “falls into the
  neighboring function”, which the translation calls by its symbol, and the
  rewrite does the same (below). Jump tables, which the translation resolves
  into `goto`s, become a `switch`.

A **loop** has a head — the target of a back edge, identified in advance —
where the state is written out and read back afresh, under a C label; the back
edge writes out the state, advances simulated time exactly as the translation
does (`if (--g_edge_budget < 0) rt_edge(c);`, with its interrupts) and jumps to
the head. The body is symbolic like the rest; only the junction is
materialized.

**Large functions** called for one more idea. As a pure tree, whatever follows
an `if` is copied into each branch: it is readable, but the number of paths
doubles with each test, and a two-kilobyte function never gets done. When the
tree exceeds thirty-two paths, the generator starts over with **junctions**:
any block that several paths reach becomes, like a loop head, a point where the
state is written out and read back under a label — and is then emitted only
once. Reaching a junction *going forward* is not an edge: only the backward
jump is one. An expression that feeds on itself — a checksum is enough —
doubles with each instruction; past one hundred and sixty characters, it
becomes a temporary.

**Jump tables** are rendered the way the translation renders them: a `switch`
whose every case jumps to a label in the function, and whose default goes
through `rt_jump`. Three forms: `add rX, pc ; … ; bx rX` and `add pc, rX` in
Thumb, `addls pc, pc, rX, lsl #2` in ARM. The cases are emitted one after the
other, each closed by a return — otherwise the one that hands back control
falls into the next, and the bench saw a function return 1 instead of 0.

Flow that **leaves the function without a call** — a `b` to another one, a
fall-through into the neighbor — follows the translation: through the other
function's symbol, without pushing a return. **System instructions** (`mrs`,
`msr`, `mcr`, `mrc`, `swi`, `bkpt`) write out the state, let the translator's
very own line do its job, and everything is read back: the mode may have
changed, and the stack with it. **ITCM** code is read from where the crt0 takes
it, at the end of the ARM9 image.

“`mov lr, pc`”, “`ldr lr, [pc, #n]`” or “`add lr, pc, #n`” just before a
“`bx rX`”: to the translator this is a **register call with a chosen return** —
it calls, then resumes at that address. This is how the SDK's interrupt handler
calls an interrupt's routine and then moves straight on to the thread switch.
The rule is carried over word for word; without it, this was the only function
out of twenty-nine thousand to change the fingerprints. Likewise the ARM
computed jump, “`add pc, pc, rX`”, for which every instruction in the function
is a target — the SDK's unrolled divisions jump anywhere within them.

`stmia rX!, {…, rX, …}` — the base in its own list — follows the translator's
rule: if it is first in the list, its original value is stored; otherwise, the
value after writeback.

A direct call links **the very function** that the translation links by its
symbol — that region, that entry —, looked up once per call site and kept
(`APPEL_DIRECT`). Looking it up by address at run time did not necessarily
yield the same one, since two functions sometimes overlap; and it saves, on
every call, the lookup and the check of which overlay is loaded.

This C is not the translation copied out: it is its result, recomputed by
another route. That is what gives the comparison its meaning. The generator
produces sixteen files `generated/natif_auto_N.c` compiled in parallel with the
translated code — in a single file, the compiler spent an hour on it — and a
`natif_auto.inc` that holds only their tables; `runtime/natif.h` carries what
they share with `natif.c`. None of this is versioned.

**29,616 functions out of 29,694** come out of the generator, the largest —
twenty-one kilobytes, two hundred and fifty paths — included. Whatever it
refuses, it reports and counts (`analysis/feuilles_refusees.txt`): sixty-two
“functions” that run into data — the code mapping took them for code —, and the
core of the SDK's context switch, with its user-bank registers, which the
runtime replaces anyway. A single jump-table *case* it cannot write — almost
always a word from the constant pool, which the rule “every instruction is a
target” counts among the targets and which nothing ever reaches — no longer
condemns the function: that case is handed back to the translation, which has
that label.

Proof is kept **per path**. Each path — and each back edge — records that it
was taken, and the bench knows which ones it has compared. A function is proven
when all of its paths have been, on itself (*instance*) or on functions whose
code differs only in the constants (*template*). A verdict carries the
fingerprint of the code compared: should the generator change what it emits for
a function, its proof falls. A divergence is never erased, and a template
caught at fault once no longer proves anything for anyone. `SFC_NATIF=auto` (or
`tout`) puts into service only what is proven; the rest keeps its translation.

There is a third order of proof, **by fingerprints**, for what neither bench
can judge: functions that the outside world always interrupts, those that talk
to the hardware or to the processor itself, recursive ones. Put into service
(`SFC_NATIF=auto-tout`) for the length of a run whose fingerprints stay those
of the reference, they did their work identically on the paths they took — the
list (`SFC_VERIF_LISTE`) gives them, with status “service”. If the fingerprints
move, bisection (`SFC_NATIF_TRANCHE`) points to the culprit.

### The coprocessor too

The console's second processor — the ARM7, which carries the sound driver, the
clock, the touch screen, and serves the cartridge — has its own binary: 1,039
functions, mostly SDK, translated separately (`generated7/`). The same
generator reads them (`modules()` returns its binary and the sections it copies
into its WRAM, from which it runs its driver) with its own code map:

```bash
bash tools/banc/generer7.sh [--moisson listes…]     # analysis/codemap_arm7.json -> generated7/natif_auto7_N.c
```

**1,035 functions out of 1,039** come out of it, plus five secondary entry
points; the three refusals are, as on the ARM9, the context switch and its
user-bank registers. A single generator defect showed up, one the ARM9 had
never triggered: under a condition, the carry of a register-specified shift was
hoisted above the `if` while the C flag it reads remained declared beneath it.

Its rewrites follow the ARM9's in the combined table — same lists, same slices,
same modes; `NATIF_ARM7`, set at the top of its files, makes their ranks count
from their own start. The “arm7” region is all the runtime needs to find its
table and its hooks. The cold bench tries it out with a context of its own —
its memory map, its registers —, and it took very little: its 64 KB of WRAM in
the tracked memory, its register accesses counted like the ARM9's, its edge
frozen like everything else (it hands over to the ARM9 through a fiber switch —
when cold, it reloads its count and comes back), the stub placed on its callees
too, and the code map kept by *host* address, since the two processors do not
see the same memory at the same addresses.

In game, the ARM7 runs on its fiber, out of the bench's reach: it is judged
cold, then **by fingerprints and by sound**. `SFC_NATIF=auto7-tout` puts it
alone into service; `SFC_WAV` records what the mixer produces, and two
recordings can be compared byte by byte. On the first try: five runs with
identical fingerprints, and the mission's ten megabytes of sound identical down
to the byte.

The measurement of what still goes through the translation was extended to the
coprocessor — and its first “zero” was wrong: the build option only reached the
ARM9's library, the counter counted nothing, and it was a control run *with
nothing in service* that showed it (it displayed zero too). A zero is only worth
something if you have seen the instrument count. Once fixed, it named a single
address, `0x037FC394`: the resumption of a thread in the coprocessor's
scheduler — the same as on the ARM9, five hundred thousand times per series of
runs. Entered in `tools/entrees_milieu7.txt`, rewritten, proven cold on its four
paths.

Result: **both of the console's processors run entirely in native C** — 30,811
rewrites in place (`SFC_NATIF=entier`), not a single translated function
executed any more on either side, fingerprints and sound identical to the
translation's. Then six long replays, each played twice — pure translation,
whole game native: **520,000 frames, two hours and twenty-five minutes of play,
fingerprints identical frame by frame and 1.1 GB of sound identical down to the
byte** (`tools/banc/campagne_entier.sh`). On the coprocessor side, 871 rewrites
proven out of 1,040 — 82% of its paths —, since the hardware got its stub.

Since then, **native is the runtime's default behavior**: both processors, all
the rewrites, after a real play session confirmed what the benches were saying.
`SFC_NATIF=non` gives back the pure translation, which remains the reference
for all comparisons — the scripts in `tools/banc/` request it explicitly.

### The in-game bench, now that functions make calls

As long as it only judged leaf functions, the bench could get away with anything. A
function that makes calls sets the world in motion, and four precautions became
necessary — each one found through a crash, none through reasoning:

* **it only judges atomic executions.** If an interrupt was delivered, a thread
  switched, the coprocessor ran or a DMA copied during the translation
  (`g_perturbations`), or if it touched a hardware register (`g_acces_mmio` — a
  word sent to the coprocessor would go out twice), rolling memory back would
  break what has moved forward: the comparison is abandoned, the function keeps
  the translation's result and counts as “interrupted”. Hardware registers are
  neither restored nor compared in-game: only time writes to them;
* **the rewrite runs in a frozen world** (`g_temps_fige`): time does not
  advance, no interrupt is delivered, the coprocessor does not take over. The
  translation has already moved all of that forward once; the edge budget is
  part of the restored state;
* **comparisons nest in levels** (twelve): a high-level function — which may
  never return — can be in the middle of a comparison while what it calls is
  being judged. Callees are judged only during the “translation” phase of the
  function that calls them; during its “rewrite” phase, they run as they are.
  Without this, each level doubles the work — sixteen times over at four
  levels, four thousand at twelve: the game stalled at the twentieth frame. And
  we quickly give up on what the world always interrupts (thirty-two attempts
  without a single comparison, then one in five): those functions are the top
  of the call stack, and as long as we kept at them — eight thousand attempts —
  they held the levels; with the large generated functions, the bench was
  judging only 355 functions per run instead of 3,000. Each level has its two
  states, and **its own interception of the final jump**: the outer one is set
  aside while the inner one is judged, and only a jump made at the shadow-stack
  depth of the function being judged counts — its callees return through a
  jump too;
* **a single fiber**: the levels are those of the game thread; in the others,
  the translation runs alone.

**The whole status register is part of the state.** The bench saved and
restored the registers, N, Z, C, V, T and the mode — not I, F, Q, the `spsr` or
the banks of the other modes. `OS_DisableInterrupts` (`mrs`; `orr`; `msr`)
looked wrong on every call: the translation set I, and the rewrite, starting
again from there, read an I that was already set. All of this is now saved,
restored and compared.

**A raised interrupt counts, even when masked.** The bench counted only
*delivered* interrupts as perturbations. But time advances during the
“translation” phase, and an interrupt raised while I is set resets the edge
budget to zero — which the rewrite, replayed in a frozen world, does not live
through again: “il reste 256 arêtes au budget, attendu 0” (256 edges left in the
budget, expected 0), once in eighty thousand, and the same divergence repeated
in the four functions nested at that moment. Raising one is now a perturbation
(`hw_check_irq`).

**Only written pages are copied.** A comparison took the whole state three
times — five megabytes before, as much after, as much to put back — and
compared it: two milliseconds, when a leaf function touches a page or two.
When a bench is requested, all the memory the game can write fits in a single
reservation whose written pages Windows tracks (`MEM_WRITE_WATCH`,
`runtime/memory.c`) — whoever writes them: translated code, rewrite, native copy
by the runtime. Each bench state is a whole memory image kept up to date page
by page: taking it means copying the pages written since last time; restoring
it, putting them back. And the image taken after the translation, before the
earlier state is put back and the rewrite runs, has as its stale pages
*exactly* those that one or the other wrote: there is nothing else to compare,
or to put back. Six hundred mission frames with every function monitored: 655
seconds before, 15 after, the same verdicts down to the digit. The test:
`SFC_VERIF_SABOTAGE=0x…` corrupts one byte of RAM, through a raw pointer, after
each rewrite — 476 compared functions out of 476 raised the alarm at every
comparison, and the run's fingerprints stayed those of the reference: the
comparison sees everything, and the restoration leaves nothing behind.

**Time is a register.** Simulated time is counted in back edges, and the bench
now compares how much of it remains in the budget. It did not use to, and twenty
thousand “proven” functions changed the fingerprints from the twentieth frame
on: a bisection over the ranks put into service (`SFC_NATIF_TRANCHE=a-b`,
fifteen tries of a few seconds each) pointed to a vector-math function whose
rewrite counted one edge too many — it reached a loop header *going forward*,
through another path of the tree, and that jump, an artifact of how the code is
written, is not an edge. Then the reverse: an already-settled condition that
led to a backward jump no longer counted its own. No register, no byte of
memory showed either one; only the moment at which the next interrupt fires
changed.

The translation is called through its exact table entry (`rt_entree_region`),
not through `rt_find`, which would choose based on the current mode: an ARM
function called from Thumb code via `blx` runs with its caller's T bit.

### The cold bench

So `SFC_VERIF_FROID=frame[:slice/count]` tries each function **outside of
any game session**: at the given frame, the game stops; for each function —
after placing its overlay's image in memory, since the translation reads its
constants from the code — randomly drawn registers, a 64 KB scratch area filled
with pointers into itself, zeros and small values, and the two versions
compared by the same `verifier` as in-game, dead stack included. The constants
the function compares against are seeded into the draws, otherwise the path
“`r0 == 0x23`” would never be taken. The clock is frozen: reading a VBlank
register no longer advances time there, and accesses outside the memory map are
no longer logged — there are millions of them.

Some trials **do not count**, and the bench knows it:

* the translation touched a hardware register through a pointer read from
  memory, which the draw could not foresee;
* it **overwrote its own code** — a copy fed a randomly drawn count ends up
  sweeping through all of memory, and the translation rereads its constants
  from the code whereas the rewrite knows them; no function of the game
  overwrites itself;
* it never finishes: beyond two hundred edges, or four hundred nested calls for
  a recursion, `rt_edge` and `RT_CALL` return to the bench via `longjmp`; a
  fatal stop — the draw led where the game would never go — returns there the
  same way. If it is the rewrite alone that never finishes, that is a
  divergence;
* it keeps control *without crossing an edge*: a state machine, a loop around a
  jump table, which neither the translation nor the rewrite counts. A guard
  keeps watch; after five seconds without a finished trial, it names the
  function in `SFC_FROID_EXCLURE`, writes out the list of what has been done and
  exits with code 3 — the slice is relaunched, and leaves it out
  (`tools/banc/tranche.sh`).

What the runtime already replaces with its own functions — the context switching
of the SDK's threads — is neither rewritten nor judged: its translation never
runs.

**Trials that remember.** Drawing each trial from scratch quickly proves the
small functions and stalls on the others: forty thousand paths remained unseen,
almost all of them guarded by simple conditions — “`[r0 + 8] == 3`”, “the
call's result is 0” — but stacked four to eight levels deep, or behind the index
of a jump table. Three things address this. A trial is entirely contained in its
**seed**: one that discovers a path becomes *seed stock*, and we start again from
it, tweaking only one or two things — six conditions cannot be drawn in one go,
they are cleared one at a time, each path reached serving as a stepping stone
to the next. The tweaks aim at the **sites** that the generator reads in the
code it has just emitted: which inputs the conditions depend on — a given
register, a given word of the object it points to, a field of the object within
the object, a global, a singleton's field, a callee's answer. Thirteen
registers times forty offsets makes five hundred places to write to; rarely do
more than ten of them matter. Finally, the **stub obeys**: “the second call's
answer is 7” is a tweak like any other — and so is “the second call overwrites
r4”, even though a real callee would preserve it: “`r4 = new`; …;
`if (r4 == 0)`”, the null test the compiler repeats after every base
constructor, cannot be taken any other way, and the two versions must agree on
it as anywhere else. Likewise “`(r0 + 8) == 0`”, an adjusted `this` being null,
is only taken with r0 = -8: the generator reads those values in the
conditions and hands them to the draw. An SDK function — two fifteen-case
`switch` statements, one on a callee's answer, the other on a global — topped
out at eighteen paths out of thirty; it gets all of them in two thousand
trials. None of this touches the proof: any input state is good for comparing
two executions, we are only choosing them better.

And a flaw in the bench, which these missing paths ended up revealing: so as
not to overwrite code, it refused to seed a global in the code area of *all* the
overlays — but they share their addresses, and one overlay's data almost always
falls “in the code” of another. No overlay global was ever seeded, nor any
singleton. Only the ARM9's code and that of the overlay being tried matter: four
hundred more functions proven on the next pass.

A second one, of the same kind. A trial that “overwrites code” does not count —
the translation rereads its constants from the code, the rewrite knows them. But
the bench required that *nothing* change, whether in the ARM9, in the overlay or
in the ITCM; yet the paths that no game session takes — the null `this`, the
post-constructor `r4 == 0` — write precisely to address 0, 4, 0x18, which are a
mirror of the ITCM, and they were all cancelled. The only code that must stay
intact is that of the function being judged: whatever a trial overwrites
elsewhere, both versions see it the same way, and the next trial finds it
restored. Seven hundred more functions — and, on those paths the game will
never take, not a single divergence. (And what is protected from seeding is
counted word by word, function by function: a range from the first to the last
byte of code stretches, because of a few false functions seen in the data, over
the globals.)

**Callees answer with a stub**: one and the same hook placed on all the
translated functions and also taken by the dispatch. It records in a log what
it is asked — the target, the arguments, the stack, the return address, the
mode — and answers with registers and flags drawn from a seed specific to the
trial, the same for the translation and for the rewrite; it also writes a few
words into the object it receives and below the stack, as a real callee would.
The two executions must leave the same log: calling something else, or with
other arguments, or in a different order, is a divergence like any other. A
function is thus proven equivalent to its translation *whatever* its callees.

**The hardware answers with a stub too.** The bench kept functions that name a
hardware register out of the cold bench, and cancelled any trial that a randomly
drawn pointer led there: hardware cannot be replayed. But cold, nothing
requires running it: a register read receives a value drawn from the trial's
seed and the rank of the access (the same for the translation and for the
rewrite), a write is logged, address and value, and the real hardware sees
nothing: no DMA started, no word sent to the other processor, no clock
advancing. The two versions must leave the same log, as with callees. Five
hundred and fifty more functions on the bench — mostly the coprocessor's sound
driver — and not a single divergence. Only recursive functions remain to be
judged in-game.

The bench judges functions that end by jumping elsewhere **at the moment of the
jump**: `rt_jump` records the target instead of following it, the two states
are compared, then the jump is followed just once.

The bench itself was tested on both sides with a deliberate fault (`--faute`
corrupts `r12` in every function): it raised the alarm on every function it
saw, 937 out of 937 in-game, 616 out of 616 cold. And it found real faults in
the generator before they mattered: a call target bound directly where the
translation goes through the dispatch (the T bit differed), a jump “backward in
address” to code never visited taken for a loop, a recursive call taken for a
long branch, a jump table followed by `rt_jump` where the translation does a
`goto`, a flow carried on into the neighboring function.

Outcome: cold, twelve processes, a hundred seconds for the whole game — **28,788
functions tried, zero divergences**. In-game, five runs: 4,672 functions
compared on real data, zero divergences — then fourteen long replays, one
million two hundred thousand frames, eighty million comparisons: zero. **26,689
rewrites proven** (26,470 by instance, 187 by template, 32 by
fingerprints), 3,072 awaiting a path that neither the draw nor the game session
managed to take — and none faulty. Above all: with **all 29,761 rewrites — 29,616
functions, 145 secondary entries — in service** in place of the translation,
plus the eleven done by hand (`SFC_NATIF=entier`: the whole game in native C),
the five runs — 4,400 mission frames, four times 15,000 of menus, modes,
strategy map and options — keep fingerprints identical to those of the pure
translation, entire memory included.

**What still goes through the translation.** Built with
`-DSFC_COMPTE_TRADUIT=ON`, the translated code counts its executions, past the
hook, according to whether a function is entered at its entry point or in its
middle (`SFC_TRADUIT=file`). With the whole game native, the first
measurement named what the generator was leaving out without saying so:
forty-six functions of more than four kilobytes — the scene loops, the modes'
state machines — skipped by a size limit that did not count its refusals, and
one function refused over a data word that did in fact run. Once those were
generated, **no translated function runs from its entry point any more** on the
five runs. There remained four to twelve functions entered *through the middle*
— the measurement also records at which address — and three reasons:
**resuming a thread** — `OS_SaveContext` “returns twice”, like `setjmp`, and
the SDK scheduler (`0x0202DD18`) resumes at `0x0202DD9C` on every thread switch,
three hundred thousand times per run; **secondary entries** — a hundred and
forty addresses in the middle of a function that another one reaches through a
`b` or a `bl`: signed division ends in the unsigned one, a hundred and sixty
thousand times; and a few **returns unwound by the dispatch**.

Hence the **secondary rewrites**: the same function, interpreted from an
address other than its entry point. Everything in the translator's rules that
speaks of “the function's entry” keeps referring to that of the whole
function, since it is against its translation — taken at the same place — that
it is compared; only the address from which interpretation starts changes. The
addresses reached by the emitted code, the generator knows; the others only
show up at run time, and `tools/entrees_milieu.txt` keeps those the measurement
has named. Since a secondary can reach another one, the process is repeated as
long as new ones turn up. As a translated function's hook only fires at its
entry point, it is the sites that find the secondary — `SUITE_DIRECTE`,
`APPEL_DIRECT`, resuming after a thread switch (`natif_reprendre`), the dispatch
when a jump lands in the middle of a function — by (region, entry of the whole
function, address); in the lists its region is written “`arm9@0202DD18`”, since
two overlapping functions can be reached at the same address. A hundred and
forty-five secondaries, judged like the others: thread resumption has its five
paths proven cold.

Result: **on the five runs, not a single translated ARM9 function executes any
more**, neither from its entry point nor from its middle — 29,771 rewrites in
place, exact fingerprints. The translation is now nothing more than the
reference the bench compares against.

The campaign is rerun like this:

```bash
python tools/decomp_feuilles.py                      # generates; then compile
bash ../../tools/banc/tranche.sh 0 12 101                          # … one cold slice per process
SFC_VERIF=auto SFC_VERIF_LISTE=jeu.txt ./sfc.exe                   # in-game, on a replay
python tools/decomp_feuilles.py --moisson froid/p*.liste jeu.txt   # merges the verdicts, regenerates
```

| variable | what it does |
|---|---|
| `SFC_VERIF=auto` | compares all of the generator's functions with their translation, in-game |
| `SFC_VERIF=auto-reste` | only those not yet proven |
| `SFC_VERIF_SABOTAGE=0x…` | corrupts this byte of RAM after each rewrite: the bench must raise the alarm everywhere, and the game must keep no trace of it |
| `SFC_VERIF_FROID=frame[:k/n]` | tries them all cold at this frame, then exits |
| `SFC_VERIF_LISTE=file` | the verdict per function: region, address, state, calls, divergences, paths seen, number of paths, code fingerprint |
| *(nothing)* | **the game runs native**: without `SFC_NATIF`, it is `entier` — except during a bench (`SFC_VERIF`, `SFC_VERIF_FROID`), which keeps the translation around what it judges |
| `SFC_NATIF=non` | the pure translation: the reference for all comparisons |
| `SFC_NATIF=auto` / `tout` | puts the **proven** functions into service (and, for `tout`, the hand-written rewrites) |
| `SFC_NATIF=auto-tout` | puts into service all functions that are not faulty, proven or not — to judge the batch by fingerprints |
| `SFC_NATIF=entier` | the whole game native: the hand-written rewrites and all of the generator's, on both processors |
| `SFC_NATIF=auto7` / `auto7-tout` | the coprocessor alone: its proven rewrites, or all of them |
| `SFC_TRADUIT=file` | (`-DSFC_COMPTE_TRADUIT=ON` build) what still goes through the translation: region, entry, executions from the entry, from the middle |
| `SFC_NATIF_SANS=0x…,0x…` | those keep their translation: the culprits already found, so that the bisection looks for the next one (`tools/banc/dicho_tout.sh`) |
| `SFC_FROID_EXCLURE=file` | the functions the cold bench's guard has set aside, from one relaunch to the next |
| `SFC_NATIF_TRANCHE=a-b` | puts into service only the generator's functions of rank a to b-1: a way to find by bisection the one that changes the fingerprints |
| `SFC_FROID_LISTE=file` | cold, only retries the listed functions (“region address”) — `analysis/feuilles_partielles.txt`, which the generator writes |
| `SFC_FROID_GRAINE=n`, `SFC_FROID_ESSAIS=n` | other draws, more trials (120 by default; 3,000 for a targeted pass): the paths seen add up from one pass to the next |
| `SFC_FROID_SEUL=0x…`, `SFC_FROID_BAVARD=1`, `SFC_FROID_ARETES=n` | investigate a single function, name each one, change the edge cap |

### Three bench traps that have nothing to do with fidelity

* **Sound was listening to the sound card.** The mixer adjusts its rate by one
  percent according to how full the output queue is — it has to, to play
  without gaps — and the game reads the channels' state: a channel that goes
  silent one frame earlier, and a bit of the voice mask (`0x020B6188`) changes.
  With the machine idle, nothing showed; twelve cold-bench processes in
  parallel, and the *pure translation* changed fingerprint on one frame around
  the 862nd, then came out right again. The bisection, which assumes a
  reproducible world, “pointed to” two functions that a spy (`SFC_ESPION`)
  showed were *never called*. A culprit that could not have acted incriminates
  the bench, not the code: replay the reference twice, under load, before
  searching. During a bench (`SFC_MAX_FRAMES`, `SFC_HASH`, `SFC_NO_VIDEO`), the
  mixer's rate is now exact. In-game, the adjustment remains — truly fixing it
  means resampling the output downstream without touching the channels' state:
  to be done along with the modifications, on the stable version.
* **The coprocessor advances during the comparison.** A function whose execution
  contains a loop advances the clock, and hence the coprocessor, which writes to
  memory — and it does so twice, once per compared execution. The bench then
  blames a perfectly correct rewrite. You can recognize it because the
  discrepancies repeat identically and disappear with `SFC_ARM7=0`, something to
  try as soon as a divergence seems not to depend on the function.
* **A function that is never called proves nothing.** The bench now says how
  many calls it has *seen*, not just how many it has compared — `0 appel vu`
  (0 calls seen) means that the replay does not go through there, not that the
  rewrite is correct. This is what tells a frequently *referenced* function
  from a frequently *called* one: `SFC_STAT_CALL=n` gives the latter.

## 5 ter. Modifications

The rewritten game is the base; a modification is grafted onto it **without
rewriting any function**, at a **modification point**: a hook called just
before an instruction of the original code. The list lives in
`runtime/modifications.c`:

```c
#define LES_POINTS(P) \
    P(0, "arm9", 0x02066A8E, portee_coupure, "…") \
    …
```

— rank, region, address of the instruction, hook, what it finds there. The
generator (`tools/decomp_feuilles.py --modifications`, read by default) places
a `MODIF(c, rang)` at that spot in the emitted code; the processor state is
written there before and read back after, exactly as around a call. The hook
reads and changes registers and memory as the game's code would. As long as it
is null, the function does what it used to do: the generator says how many
points it has placed, and the functions that carry some are judged on the bench
like the others.

Adding a point: one `P(…)` line, `python tools/decomp_feuilles.py`, then
compile. Only the native game — the default — knows about the modifications;
the pure translation (`SFC_NATIF=non`) remains the original game.

| modification | variable | points |
|---|---|---|
| draw distance | `SFC_PORTEE=factor` (1 to 4) | cutoff distance (`0x02066A8E`, r1); far plane of the projection (`0x02066A46`, r0); height of the background (`0x0218DACA`, r6); its first rotation (`0x0218DB4A`, `sp+0x38`) |
| shot range | the same | lifetime of enemy shots (`0x021E0E82`, r0); of the Arwing laser in single-player (`0x021DF980`, r0); 17 enemy firing ranges (points 6 to 22, r0), doubled without exceeding the view (4,000 × factor); lifetime of the Plasma laser (`0x0221B5B8`, r0) and the start of its shrinking, pushed back by the same amount (`0x0221B734`, r0) |
| the gamepad | `SFC_MANETTE=0` removes it; without a gamepad, nothing changes | is the stylus holding the control stick, is it down: for flying (`0x021E9E84`, `0x021E9E96`, r0; r4 the control, its point at `+0x10`) and for the lock-on assist (`0x021EA30A`, `0x021EA31A`); the circle-recognition window, which expires (`0x021E9F76`, `+0x3C` of the control); in the on-rails control of missile chases, is the stylus down (`0x021EB016`, r1), is it holding the control stick on the first tick (`0x021EB02A`, r0), the circle window (`0x021EB080`); the opening shot of a battle, is the stylus down (`0x0204863C`, r0); the tick's commands, before the cockpit reads them (`0x0220F01E`, r4 the cockpit) |
| the mods (launcher's Mods page) | `SFC_MOD_…` (§ 5) | points 35 to 59: see “The launcher's mods” below |

Proofs for the draw distance, hooks off: `0x0218DA9C` without a discrepancy over
2,481 calls compared in flight; `0x02066A34`, which computes the projection with
the hardware divider and which the bench therefore cannot compare in-game, by
fingerprints — the 10,049 frames of the Titania replay and 30,000 of the last
missions identical to the translation. With the draw distance on: the 88,436
frames of the last missions at ×3, and 240 displayed frames per second held at
×2 as at ×1.

The far plane of the projection (point 1) only moves back beyond 2.4: at ×2 the
projection matrix remains the game's own.

Proofs for the shot range: at ×1, the same fingerprints (Titania, last
missions); at ×2, on the last missions, enemy shots live 3 s instead of 1.5,
Arwing lasers that hit nothing 66 ticks instead of 33, Plasma lasers 3.59 s
instead of 1.79 (7,840 units instead of 3,880), their size only dropping at the
end (0.06 at 3.59 s, whereas originally it drops at 1.79 s), and the fighter
from overlay 7 fires 32 times instead of 22, starting earlier.

### The launcher's mods

Rule changes, off by default (`runtime/mods.c`). They act at the moment the game uses a value, never on the
stored value: a story event that changes Fox's laser does not undo the mod, and the save file does not keep
the imposed laser. Infinite bombs and received damage do touch what it keeps, though: the bomb stock and the
shield left (§ 3, “Pilots, their weapons and their health”). The ship is set pilot by pilot
(`SFC_MOD_LASER=fox:plasma,slippy:double`): each hook recognises the pilot by its rank, where the game reads it.

| mod | variable | points |
|---|---|---|
| a pilot's laser | `SFC_MOD_LASER` | the type read when firing (`0x021FE246`, r0; the vehicle's pilot at `+0x28` of r4), for the fire rate (`0x021FE192`, `0x021FE15A`; the pilot at `[sp]`); for Fox only, the “ArwingDynamicsPlayerFox…” set that `0x021F78C4` picks from his laser (`0x021F7A64`, r0) and the briefing picture (ovl14 `0x0222C1A8`, r1) |
| a pilot's lock-on | `SFC_MOD_VERROUILLAGE` | the type read by the vehicle (`0x021FE838`, `0x021FE8E0`, `0x021FE984`, r0; the pilot at `[sp]`) and by the cockpit (`0x0220F814`, r4; `0x0220FD0A`, r0; the pilot at `+0x48` and `+0x50` of r7) |
| a pilot's gauge | `SFC_MOD_JAUGE` | the size read by the vehicle's method `+0x14` (`0x021FE9F2`, r0: 0 small, 1 middle, 2 large; the pilot at `[sp]`), hence the capacity, 25, 50 or 75 |
| look of a pilot's ship | `SFC_MOD_VAISSEAU` | the key of the pilot whose files get loaded (ovl0 `0x02188FC2`, sixteen bytes at r1; the pilot at `sp+0xBC`); the pilot of the six record reads (`0x021FE626` to `0x021FE796`, r1) |
| infinite boost | `SFC_MOD_TURBO` | the cost of one tick of boost and of brake (`0x021EDF10`, `0x021EDE44`, r0); barrel rolls keep theirs, otherwise a roll started with the gamepad would never end |
| infinite bombs | `SFC_MOD_BOMBES` | the decreased stock, before it is stored: touch screen (`0x022010B6`, r0), radar (`0x022052B4`, r1); the gamepad no longer counts its own down |
| charged shot at maximum | `SFC_MOD_CHARGE` | the target's level (`0x021E5F26`, r0 = 5) |
| invincible, received damage | `SFC_MOD_INVINCIBLE`, `SFC_MOD_DEGATS_RECUS` | at the entry of `0x021F071C`, r2 zeroed or multiplied, for the player's actor only — except the beacon that must be passed in time (that hit returns to `0x0226B611`) |
| damage dealt | `SFC_MOD_DEGATS` | at the entry of `0x0216CC8C`, r1 multiplied when the attacker is the player or is not recorded; “un-coup” raises it to the health left |

Tests, on the session obs52 replayed without a window: without mods, the fingerprints of the first 30,000
frames stay those of the reference. In Fox's battle, the imposed laser changes the fire function called (44
plasma shots become 22 Zapper shots; 67 curved shots; 62 twin); wide lock-on triggers 8 lock-ons there, none in
the original; Falco's ship makes 16 fewer polygons drawn at every sample — the difference between the two
bodies —, the rest of the scene staying identical. In the first battle (Dash): a gamepad holding boost keeps
it 550 frames instead of 200, the gauge staying full; health stays at 100 with invincibility; the bomb stock
stays at 2; charged shots do 320 instead of 20 or 40; with damage × 10, the first enemy dies at frame 6,932
instead of 7,010. Dash's own settings, in that battle: the Zapper replaces his twin laser (9 shots), his gauge
goes from 50 to 75, and Falco's ship makes 48 more polygons drawn whenever it is in view; Fox's settings leave
him alone. Jodie (obs18, around frame 104,700), who has no lock-on, like Slippy: with the plasma, she fires
87 plasma shots instead of 90 twin ones; with single lock-on, or wide lock-on and a large gauge, the battle runs
to its end without a fault — the player makes no lock-on gesture there, so there is none. No recorded session
has a battle flown by Slippy.

Not done yet: the 3D ship of the unit sheet, on the map (ovl14 `0x02229460`), stays Fox's; the draw distance
does not lengthen the curved shot nor the Zapper (`0x3000` at `0x0221A938`).

## 6. Lessons in method

They all cost dearly, and they recur.

**An optimization is not judged by frame rate.** Raising the edge budget or the
coprocessor threshold gives up to five times more frames per second — by
taking the coprocessor out of the game, the sound becoming perfectly silent.
Any change to scheduling is checked against the sound recording.

**Measure, don't deduce.** Simulation time was computed as “everything that
isn't rendering”: it charged to the game everything we had forgotten to time.
Measured directly, it changes in nature.

**A cumulative count says nothing about a sliding window.** Counting terrain
cells over a whole session gave thirty cells and the false conclusion that the
game drew the whole map; counted per frame, there are eight to eleven.

**A projection matrix is defined up to a factor.** The game's is normalized to
a quarter: distances read four times too short.

**Perspective depth is not linear.** An object at a tenth of the range reads at
99% normalized depth. It is the distance that must be measured.

**Check that the compiler isn't already doing it.** 30% of the flag
computations in the generated code are dead — but GCC eliminates them, and
hunting them down in the translator would have gained nothing. Likewise, `-O2`
on the generated code brings nothing: its functions are too large.

**A `pop {pc}` is not always a return.** A small intermediate function can
push another function's address and hand control to it through that same
`pop` — a tail call, not a return. The translator took them all for returns
and cut execution short before the jump: the game did detect flying through a
ring, but the call that created the bonus was skipped. The two cases are told
apart by the shadow stack, and `tools/recomp/test_return_dispatch.py` now
covers them. The price of this distinction is 17% of simulation speed, each
`pop {pc}` going through a comparison instead of a return.

This class of error is the most treacherous in the project: it produces no
crash, no message, no corruption — only an action that does not happen.
Looking for the cause in the data or the computation, as common sense
suggests, leads very far from the real culprit.

**Tooling traps**: nested heredocs break in the shell; replacement anchors must
be unique and checked; a running executable cannot be relinked under Windows.

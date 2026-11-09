# Native port plan

Goal: play *Star Fox Command* on PC, without an emulator, **exactly** as on
DS. The original game is translated instruction by instruction into native
code; the calls to the DS hardware and system are replaced by a PC layer.
Modifications (framerate, models, continuing the story) come afterwards, on
this faithful base.

## What's in the cartridge

| Binary | Role | Size | Compiler |
|---|---|---:|---|
| Static ARM9 | NitroSDK libraries (OS, FS, GX, ARM9-side SND, DWC), MGL engine, game core | 723 KB + 31 KB ITCM | Metrowerks CodeWarrior, C++ |
| 26 ARM9 overlays | modules loaded on demand: overlay 0 = permanent game code, 2 = Arwing and shots, 3–4 = strategy/map, 5–22 = enemies and bosses, 23 = enemy storage, 24 = GameSpy (Wi-Fi), 25 = download (DS Download Play) | 1.3 MB | same |
| ARM7 | sound driver (Nitro Composer), touch screen, RTC, power, wireless | 165 KB | NitroSDK, C |

Findings that guide the plan (see `carte_binaire.md`, in French):

- The code is **C++ without RTTI** (only the Metrowerks standard library has
  any): class names will come from the assertions, the strings and the
  structure, not from typeinfo.
- The game **registers its script commands and its enemy behaviors by name**
  at startup (12-byte tables in the ARM9, static constructors in the
  overlays): each command has the address of its implementation, which names
  about a hundred functions right away.
- The ARM9 calls **no BIOS function** (`svc`): division and square root go
  through the hardware registers (`0x04000280`–`0x040002B8`), waiting for an
  interrupt through CP15. The BIOS only matters for the ARM7.
- The game uses **NitroSDK threads** (IRQ handler in ITCM at `0x01FF81BC`,
  thread table in DTCM): the PC layer must provide context switching (Windows
  fibers).
- The game's assertions are compiled in: `expr`, file, line and **function
  name** are written into a structure, then `bkpt`. A gold mine of names.

## Approach: static recompilation, then gradual decompilation

There are two ways to “translate the instructions”; we chain them.

### Phase 1 — Static recompilation (playable quickly, faithful by construction)

A translator reads the ARM/Thumb code and writes, for each function, an
equivalent C function: registers become variables, each instruction its exact
translation (flags included), each basic block a label. The whole thing
compiles into an `.exe` with a PC compiler. No interpretation at run time: it
is native code.

Around this translated code, a **runtime layer** (`runtime/`) provides what
the code expects from the DS:

| What the code needs | On DS | On PC |
|---|---|---|
| Memory | 4 MB RAM, ITCM, DTCM, VRAM, palettes, OAM | arrays, direct access; the `0x04xxxxxx` addresses go through a register dispatcher |
| Interrupts | VBlank, timers, DMA, IPC, card | a 59.83 Hz clock that calls the recompiled handler; DMA executed when the register is written |
| Threads | `OS_SaveContext` / `OS_LoadContext` in assembly | Windows fibers, one per game thread |
| File system | card reads through the `ROMCTRL` registers | reading the user's `.nds` ROM |
| 3D | geometry FIFO (`0x04000400`, display lists) | GX command interpreter (`runtime/gx.c`, `runtime/gx_geom.c`), OpenGL rendering at the chosen resolution |
| 2D | two engines (backgrounds, sprites), 256×192 screens | faithful software rasterization, displayed as a texture |
| Sound | IPC commands to the ARM7's driver | ✅ the ARM7 is translated too (`SFC_ARM7=1`); mixer of the 16 voices to the host's output |
| Input | buttons, touch screen | keyboard, gamepad, mouse for the touch screen |
| Wireless | WM / DWC | absent (returns “no connection”) |

The result: an executable that **is** the game, bit for bit in its logic,
rendered in high resolution. The translated code is unreadable (like a
disassembly) but exact; it serves as the reference.

### Phase 2 — Gradual decompilation (readable, modifiable)

Function by function, starting from what interests the project (Arwing
flight, enemies, missions, menus), the translated code is replaced with
hand-written C++, equivalent to the original code. Each replacement is checked
by comparing the two versions on the same inputs (same save file, same
recorded gamepad input): the memory state must remain identical.

That is where modifications are born: an unlocked framerate is done in a
readable game loop, a continuation of the story in a mission system that is
understood.

## Order of work

1. **Code mapping** (`tools/codemap.py`) — ✅ all the functions, their mode,
   their blocks, their calls; seeds at three confidence levels, CodeWarrior
   `switch` statements, thunks, cross calls and pointers between the ARM9 and
   the overlays (two rounds; seeds coming from elsewhere are only sown in the
   gaps, by increasing address, because an address in a shared slot
   designates only one overlay; a final static round picks up the calls and
   pointers from the overlays to the ARM9, which alone reach certain
   accessors). Data pointers to strings do not become functions (a string
   mistaken for code swallows the vtables that follow it). A final pass
   repairs orphan boundaries (a rejected seed that split a function) and
   splits a function's blocks where a neighbor falls through into them; the
   block exits that no label can receive are listed in
   `analysis/sans_etiquette.txt`. The code map fills in run after run: a jump
   to unmapped code stops the program with the address, and `--why 0xADDR`
   explains the decisions made for it (and those made by the function that
   starts there). Two families of false functions are ruled out: **ghosts** (a
   seed coming from elsewhere, in a shared slot, that a function of the
   overlay runs into by simple fall-through or that sits in the middle of its
   code: the call was aimed at another overlay) and **fragments** (targets of
   `bl` used as long branches inside a large Thumb function: out of reach of a
   `b`, with no other caller, no prologue, and whose code assumes the frame is
   already open — r4-r7 read before being written, distant stack accesses,
   `pop` without `push` — or reached by the control flow, or that return into
   the caller's body, or that share its pools). Fragments are absorbed into
   their function and exported (`cibles_longues`) so that the translator
   turns the `bl` into a `goto`; translated as calls, they popped the caller's
   frame and then “returned” into its code with someone else's registers.
2. **Translator** (`tools/recomp/`) — ✅ ARM and Thumb decoders (validated
   against capstone), exact semantics, C generation; 0 discrepancies against
   unicorn over more than 10,000 blocks. A Thumb `bl` to a target in the same
   function that the code mapping recognized as a fragment (`cibles_longues`),
   or more than 2 KB away with its target reached by fall-through, is a
   CodeWarrior long branch: emitted as a `goto`, not as a call.
3. **Runtime layer** (`runtime/`) — ✅ first version: see below.
4. **First boot** — ✅ the recompiled crt0 decompresses the ARM9, `OS_Init`
   gets through (PXI, threads, touch), `NitroMain` runs, the overlays load
   through card reads, the save file is identified and read (the ARM7's card
   component simulated), sound is acknowledged, the Nintendo and Q-Games
   logos go by with their fades, the title screen appears on both screens (3D
   captured and alternated every frame), and after A then a tap the **main
   menu** appears (“Mode 1 joueur” (1-player mode), “Téléchargement DS” (DS
   Download Play), “Mode Wi-Fi” (Wi-Fi mode), “Options”; the team on the
   bottom screen), stable over 2,400 frames without a single unmapped access
   (`SFC_INPUT_REPLAY=build/replay_title.txt`). A tap on “Mode 1 joueur”
   starts the **intro** of a new game (panels of Andross, Star Fox, Pepper,
   Krystal…, text that writes itself out), which plays in full, then the
   **strategy map** appears (the Lylat system at the top, the bridge with ROB
   and Fox at the bottom, the tutorial dialogue), 15,000 frames without an
   unmapped access; with the tutorial's taps (arrow, “Pass.” (skip),
   “2 Commencer !” (2 Start!)), the dialogue unfolds up to the **strategy
   phase**: the map of Corneria on the touch screen, Fox's ship, the enemy
   groups, the countdown and the remaining turns (run 138, 40,000 frames
   without a single unmapped access). The save file works: when a game is
   launched, the game offers to resume. By drawing a route with the stylus and
   then launching the turn, the ships move, contact with an enemy opens the
   battle menu, and **air combat plays**: 3D view with sky, terrain and ships,
   touch-screen radar, shooting and steering; when time runs out the game
   returns to the map and deducts one fighter. 20,000 frames of combat without
   a single unmapped access or stack imbalance. ⬜ long-duration check, second
   planet.

   **The save file persists from one game to the next**, validated by
   playing. It did not persist because of bit 6 of AUXSPICNT: it announces
   what will happen at the end of the *next* transfer, and the SDK driver
   lowers it before writing the last byte of a transaction. The chip closed
   the transaction as soon as that signal came; the next byte went out again
   as a command and was lost. One byte was thus missing at the end of every
   read as well as every write: the save file ended up with a one-byte hole
   per page, and its four-byte header at 0x09834 — the one the game reads
   back to recognize a saved game — was never written. A healthy save file
   occupies 908 contiguous bytes from 0x09834 to 0x09BBF; a holed one
   occupied 254. The file lives in `sauvegarde/starfox_command.sav`, outside
   the build directory that gets wiped, and `SFC_SAVE` is only for tests.

   **Enemy models are rebuilt correctly.** VTX_DIFF's relative displacement
   is added as-is to the 4.12 coordinates, without scaling. The factor is
   measured rather than guessed: in a mesh, neighboring faces share their
   edges, so a vertex placed relatively must land exactly on a vertex given
   absolutely elsewhere in the same model. Of the game's 411 models that use
   this format, 76% do so with no shift, against 3.6% shifted by one notch and
   0% divided by eight. These 411 models include the enemy fighters and none
   of the player's Arwings: the defect only showed on enemies.
4 bis. **Sound** — ✅ the SDK's audio engine runs on the ARM7 coprocessor,
   and it is the coprocessor's own binary, translated like the ARM9's, that
   executes it. `SFC_ARM7=1` puts it in charge in place of
   `runtime/arm7_hle.c`, which acknowledged its messages without producing
   any sound; the build is done with `-DSFC_ARM7=ON`.

   Its code mapping: its binary takes up only a few hundred bytes at the load
   address, and its crt0 copies all the rest elsewhere — the largest part into
   WRAM, from which its interrupt handler and its sound driver run, the rest
   to the end of main RAM. By mapping these sections at the address where
   they live, coverage goes from 66% to 96% and the number of functions from
   740 to 1049. Its instruction set is ARMv4: having no `blx`, it calls
   through a register by writing the return address itself (`mov lr, pc`
   then `bx rN`); the code mapping recognizes this idiom.

   Its execution: it lives on its own fiber; the ARM9 drives the clock and,
   at each scanline, hands it the time the ARM9 has just consumed; its waits
   are paid for out of its own time. It has its own memory map (no ITCM or
   DTCM, its 64 KB of WRAM at 0x03800000, the share of shared WRAM that
   WRAMCNT gives it, the VRAM that the ARM9 lends it), its registers
   (`runtime/mmio7.c`), its timers, its serial link to the touch screen and
   the firmware, its interrupts and its vector at the end of WRAM. The
   cartridge registers, on the other hand, are the ARM9's: the hardware has
   only one set of them. Its system calls do not have the same numbers, and
   its BIOS gives it three tables that its sound driver cannot do without —
   reconstructed, since that BIOS is not in the cartridge.

   Its threads: the SDK runs threads on it just as on the ARM9, with the same
   three primitives that physically manipulate the registers. They are
   replaced the same way (`runtime/threads7.c`); without this it answered the
   first command and then fell silent.

   The mixing (`runtime/sound.c`): sixteen channels, 8- and 16-bit PCM and
   ADPCM, volume and panning, one sound frame per display frame at the
   hardware's rate, handed over to the host's output (`runtime/audio.c`).
   `SFC_WAV=file` records what comes out (`SFC_WAV_FROM=n` to start only
   at frame n), `SFC_NO_AUDIO` mutes the output.

   **Two explorations of 120,000 frames, two seeds, with the coprocessor
   running: not one unmapped access, not one halt, not one stack
   imbalance** — the same as without it. These explorations go through the
   whole game loop, up to air combat at 3,600 polygons per frame; the game
   runs there at 156 frames per second, and the coprocessor costs only 30% of
   machine time. The mix almost never clips (47 samples out of 6.3 million in
   combat).

   The hang that stopped the explorations beyond 60,000 frames came from the
   timers: they are programmed in two halfwords, the reload then the control,
   and the word was not kept in the register space — so the second write
   brought the reload back to zero. The sound driver ran twenty times too
   slowly, its command queue overflowed, the ARM9 waited for it in a critical
   section, its own queue overflowed for lack of being read, and the
   coprocessor in turn hung trying to write to it. Both processors had the
   same defect.

   Remaining: the DS Download Play mode does not display its texts (wireless
   is not simulated), and the accuracy of the mixing has not been compared
   with the console — the mix clips at times.

5. **Symbols** — function naming (assertions, registers, SDK calls),
   SDK / MGL / game separation, reconstructed structures.
6. **Decompilation** of the targeted systems, with differential verification.

## How the runtime layer runs the translated code

- **One C function per game function**, with an entry `switch` over its
  blocks: an indirect jump into the middle of a function is possible.
- **Calls**: direct between known functions, otherwise `rt_call` (a dispatch
  table, which recognizes the overlay present in RAM by comparing its image).
  A shadow stack of return addresses tells `ldr pc, [sp], #4` (a return) apart
  from a real jump. After each call, a *redirection* lets a native primitive
  make the code resume at another label.
- **Native replacements** (`rt_hook`): each generated function tests a pointer
  at its start; this is how `OS_SaveContext`, `OS_LoadContext` and the IRQ
  handler's thread switch are replaced — and how the decompiled code will
  replace the translated code, function by function.
- **SDK threads**: one processor context and one Windows fiber per thread;
  the SDK's scheduler remains translated code, only the register switch is
  native.
- **Interrupts and time**: virtual time (560,190 cycles per frame) advances
  at explicit waits (CP15, `svc`) and by quanta of back edges: the generated
  code decrements a budget on every loop, and every 256 passes the runtime
  counts 4 cycles per edge, then delivers the pending interrupts (an
  interrupt that occurs sets the budget to zero). This way a loop that waits
  for a frame counter or for a queue filled by an interrupt completes. The
  runtime plays the BIOS's IRQ vector (IRQ stack, `spsr`, exception return).
- **Hardware**: registers (`mmio.c`), DMA, timers, divider and square root,
  banked VRAM, card (`card.c`, reading the ROM), SPI save (`backup.c`), BIOS
  (`bios.c`), 3D geometry engine (`gx_geom.c`), 2D engines (`video2d.c`),
  OpenGL window and inputs (`video.cpp`).
- **Coprocessor**: by default its role is played from the outside
  (`arm7_hle.c`: IPCSYNC handshake, PXI, touch, and the card component that
  performs the save accesses for the ARM9 according to the SDK's
  `CARDiCommandArg` protocol). With `SFC_ARM7=1`, it is its translated binary
  that runs, on its fiber (`arm7_run.c`), with its registers (`mmio7.c`), its
  threads (`threads7.c`), its sixteen sound channels (`sound.c`) and the
  host's audio output (`audio.c`).
- **Diagnostics**: watchdog with shadow stack, thread log
  (`SFC_TRACE_THREADS=1`), PNG captures (`SFC_DUMP=n`), decoded game
  assertions (`bkpt`).

## What stays out of version control

The C code generated from the ROM is a translation of the game's binary: it
stays on each person's machine (`generated/`, ignored by git), like the ROM
and the assets. The tools, the runtime layer and the hand-decompiled code are
under the MIT license.

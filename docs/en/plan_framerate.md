# Unlocking the display frame rate

> **Update of September 19, 2026.** The whole game now runs as native C, and the request has
> become more precise: not a fixed rate chosen from a list, but a **decoupled display at a
> variable rate** — the game thinks at its own pace, the screen receives as many frames as the
> machine produces (60, 144, 240, or whatever it can), and a slow machine no longer slows the
> game down. What follows remains valid for the most part; three points are now measured rather
> than assumed.
>
> **1. The game advances by tick, not by time — measured.** The engine does have a time step:
> a global, `0x020D6E14`, which `0x02083FE8` reloads with 136 (one thirtieth of a second in
> 20.12) on every tick. But only two functions read it (`0x02084B80`, `0x02084F34`: the
> scripted timers and animations). The experiment: the step halved for one second of flight,
> memory compared bit for bit — **60 words out of a million differ**, all of them countdowns.
> Positions, speeds, AI, camera: identical. Making the game think more often therefore speeds it
> up by as much, and no setting compensates for it: every speed and every counter in thousands
> of functions would have to be rescaled. **The decoupling happens at the display**:
> interpolating between two game states, at the real instant of each frame.
>
> **2. The budget, measured in flight** (mission, development machine, rendering at ×1): a
> render costs 2.0 ms (2D 1.5 — composited in software —, 3D 0.4); a game tick costs 20 ms of
> simulation, one tick every two frames, with peaks at 45. At 240 Hz, eight renders (16 ms) and
> one tick (20 ms) do not fit into the 33 ms of a tick on a single thread. **The display must
> have its own thread**: at each VBlank the game drops off a snapshot of what there is to show,
> and the display thread — the only one that talks to OpenGL — interpolates between the last two
> and presents, at its own pace.
>
> **3. 3D on both screens.** In flight the game shows 3D on both screens with a single engine:
> it alternates, every other frame, and the other screen displays the *capture* of the previous
> frame, read back from OpenGL into video memory (`display_capture`). That is the reason for its
> 30 Hz, and a path from the display back into the simulated machine that the display thread
> will have to replace: each screen keeps its own polygon list, and where the game displays a
> capture, the scene is redrawn, interpolated.
>
> Revised steps: (1) the snapshot and the display thread, with the frame repeated — nothing must
> change to the eye, nor in the fingerprints; (2) 3D interpolated per screen, polygons matched by
> a tag set at the source; (3) 2D through its registers; (4) inputs read at the display rate;
> (5) the safeguards (cuts, unmatched polygons).

> **Where things stand (September 19, 2026, evening).** Steps 1 and 2 are done
> (`SFC_AFFICHAGE=libre`, `SFC_IPS=n|vsync|0`, adjustable in the launcher: “Images par s.”
> (frames per second)), and three defects that only the eye could see — a doubling of the
> enemies, of the explosions, of the dialogue portraits in flight — have each been traced to
> their cause:
>
> - **The clock.** The game's frames do not arrive regularly: the tick is long, the VBlank that
>   follows is late and the next one catches up (we measured 22 to 46 ms between two 3D frames
>   that are each worth the same simulation step), and a busy scene presents its frames every
>   second VBlank, then every third, then every one. Interpolating by arrival time replayed this
>   jitter, and at 240 frames per second a hitch looks like a double image. **Time is now counted
>   in ticks** — the serial number of the 3D frame — played out at equal steps: the tick duration
>   is measured over a window of arrivals, and the ideal time of a tick is the envelope of the
>   arrivals (the latest one), so that a frame is there when its time comes (`HorlogeJeu`,
>   `runtime/video.cpp`). The game drops its snapshots into a queue: the display no longer
>   misses a single one.
> - **Matching.** In flight, one polygon in seven was left without a counterpart, and so jumped
>   thirty times per second in a scene gliding along at 240. The cause was not the polygons'
>   identity but the plausibility criterion, written in screen space. It is now written in camera
>   space (`runtime/entre_images.cpp`): from the unambiguous polygons, we estimate the rigid
>   motion that the camera imposes on everything that does not move — by projection, weighted
>   least squares, moving objects discarded tick after tick — then use it twice. To tell apart
>   those that look alike (a hundred ground tiles, five rings: the right counterpart is the one
>   this motion predicts, or, for what is attached to the camera, the one that has not moved).
>   And for those that truly have no counterpart — a particle born that very instant —: they
>   follow the camera instead of jumping with it. A second pass matches, by neighborhood alone,
>   anything whose vertices the game recomputes, or whose texture image it changes, on every
>   tick. In flight: 98 to 100% matched.
> - **Textures.** The flight interface — portraits, text, gauges — is drawn by the 3D engine,
>   and its texture images are rewritten in video memory when a character speaks. But the
>   texture cache decided “nothing has changed” from the write counter of the *live* machine,
>   whereas the free display draws a snapshot one or two ticks old: it kept the previous texture
>   image, or took the next one. Each snapshot now carries the version of its textures (shared
>   between snapshots as long as the game writes nothing: 640 KB less copying per VBlank), and
>   what gets erased has its own stream in the cache.
>
> **The test** (`SFC_AFFICHAGE_ESSAI=n`, with `SFC_DUMP_FROM=frame`): offline, nine consecutive
> 3D frames, n sub-frames between each, and a measurement of how much the picture changes from
> one step to the next. Continuous motion changes by the same amount at every step; a jump at
> the midpoint or at the game's frames makes a spike. On the test mission: 3.03 per step, 3.03
> at the midpoint, 3.26 at the game's frames (the game's animated textures, for their part,
> really do change once per tick). The game stays identical in its fingerprints: the display
> touches nothing that the game reads.
>
> **Windows sleep (same day, later).** The player was still seeing double. The “film”
> (`SFC_AFFICHAGE_FILM=n`: the n frames displayed from frame `SFC_DUMP_FROM` onward, every one
> of them, at their own pace, with the time and the game tick each one shows) revealed it in a
> single measurement: the frames did not come out every 4.2 ms but in bursts — four in 4 ms,
> then a gap of 12. The frame limiter slept with `sleep_for(1 ms)`, which under Windows lasts
> until the next tick of the system timer, fifteen milliseconds; the display missed three
> deadlines and caught up on them all at once. Sixty useful frames per second, irregular,
> instead of two hundred and forty. `timeBeginPeriod` makes no difference under Windows 11 for a
> window it considers hidden: both pacing mechanisms — the game's, which had the same defect and
> delivered its VBlanks late then caught up on them, and the display's — now sleep on a
> high-resolution timer (`runtime/attente.c`), and a delay of more than one frame is no longer
> made up. Measured: 4.2 ms between two frames, ranging from 2.9 to 5.5.
>
> **Explosions in pieces.** A flame is made of two halves, two polygons that touch each other.
> Matched each on its own, one of them would drift off toward the neighboring flame. Matching now
> has three levels: whole objects first (a run of polygons placed by the same matrix, which
> submits the same shapes in the same order: it moves as one block, and an enemy no longer tears
> apart); then polygons one by one, by their shape — the fingerprint without the texture, since a
> changing texture image does not make a different square; then, for whatever has no stable
> shape, pieces made of consecutive polygons that touch each other, matched by neighborhood,
> together or not at all.
>
> **Thirty frames per second at the start of a mission.** From the menu to flight, the tick goes
> from 17 to 33 ms without a pause. The clock mapped its old readings onto the current tick using
> the new duration, projected them half a second into the future, fell fifteen ticks behind, and
> the display stayed stuck on the oldest frame it was holding. The tick duration is now measured
> in two robust parts (VBlanks per tick, from the frame numbers; the VBlank period, gaps
> discarded), a change of pace restarts the measurement, the envelope is capped at one tick, and
> the display never lags more than one tick behind what it could show. The `SFC_PROFILE` report
> gives the average delay (20 to 28 ms in flight) and the number of resyncs.
>
> To hunt for a defect that only the eye can see: the film, compared frame by frame with the
> normal-mode captures (`SFC_DUMP=1`) of the same game frame — an area that resembles no
> neighboring frame is a display defect. On the test mission, dialogue portraits and text: no
> discrepancy.
>
> **Blinking eyes, talking mouth.** On the player's recorded session, the film of the dialogue
> scene (Fox, Krystal, Falco) showed Krystal's visor doubled at the moment she blinked. The
> portraits in conversation scenes are in 3D; the game animates them by shifting the texture in
> one go, from one image on its sheet to the next, through the texture matrix. The interpolation
> made the sheet scroll beneath the square. Now only textures that really slide are interpolated
> — three texels per tick at most —; the rest changes in one go, as in the game.
>
> **The interface, to the pixel.** The “next” arrow of the dialogue boxes jumps by one pixel
> against its shadow; shown halfway, the shadow stuck out as a black fringe. For whatever a flat
> projection draws, the distance traveled since the nearest game frame is rounded to whole
> console pixels: a jump remains a jump, a glide goes from pixel to pixel, and what does not move
> stays where the game put it. (Rounding the position itself shifted the map labels, which the
> game places between two pixels, by half a pixel.)
>
> On the scene where the player saw the glitches, 400 displayed frames compared with normal mode:
> not one without an equivalent, on either screen. `SFC_RAPIDE_JUSQUA=frame` brings a replay to
> the desired scene without waiting, and passing through an explosion (yellow, then white screen)
> is not a defect.
>
> **Input lag, and prediction.** Placing yourself between two game frames means waiting for the
> next one: the display showed the game one tick late, 27 to 33 ms in flight, and the player felt
> it. `SFC_LATENCE` sets how much of that tick is kept; by default 0: the most recent frame is
> shown as soon as it arrives, and its motion extended until the next one. Measured offline
> (`SFC_AFFICHAGE_ESSAI_PREDICTION=1`: the predicted frame against the one the interpolation would
> show one tick later), the prediction is off by 2.1 in flight versus 5.8 to 7.3 for the held
> frame, and by 2.1 versus 2.4 in combat. Three exceptions, each born of a measurement: whatever
> changes size quickly (an explosion that swells then stops) extends only its displacement;
> whatever is matched only by neighborhood is not extended; the flat interface, which moves in
> pixel jumps, stays where the game put it. In real time, the display is now 4 to 14 ms *ahead*
> of the latest game frame, instead of 27 to 33 ms behind.
>
> **Depending on the scene.** The player preferred interpolation in the menus, in strategy mode
> and under the stylus: the interface there is mostly flat, and prediction leaves it at the
> game's rate. There, the game thinks sixty times per second and one tick of delay costs only
> sixteen milliseconds. So by default the latency follows the scene: 1 when the tick lasts less
> than 22 ms, 0 above 28 (flight), and it moves from one to the other over a third of a second.
> The launcher offers it under “Réaction” (responsiveness): “Selon la scène” (depending on the
> scene), “Fluide” (smooth), “Immédiate” (immediate).
>
> **The game computes its next tick while it waits (preview ticks).** The question asked was how
> to synchronize the game's behavior, and not just the picture, with the chosen rate. The game
> thinks at a fixed step and cannot think more often without speeding up; but in flight it waits
> twenty milliseconds every tick, and that wait is wasted machine time. So the rest of its loop
> (0x0203DAE8) is replayed during it: C, the swap, then the application's methods 2, 1 and 0 and
> its method 3, without the VBlank wait (0x0202C760, where the game sleeps). That tick is
> computed **with the inputs of the moment**, its frame is captured when it in turn reaches the
> wait, then everything is put back exactly as it was: memory page by page (the bench's
> mechanism), registers and banks, the 3D engine and its three buffers, the inter-processor link,
> timers, hardware registers, the video bank mapping. The real tick is then played as on the
> console.
>
> Safeguards: a preview that would switch threads, wait for an interrupt, touch the cartridge or
> run forever is abandoned and erased in the same way; no interrupt is delivered to it, nothing
> goes to the coprocessor, nothing is published to the display, and its stray jumps do not go
> into `analysis/amorces.txt`. On the test mission, 2,900 previews taken, 63 abandoned; game
> fingerprints identical (3,300 frames) and sound identical to the byte.
>
> The display uses it as the upcoming tick: it glides toward it instead of waiting for it, and
> the latency drops to zero on its own (`latence 0.00` in the reports) without any
> extrapolation. Redone several times within the tick, the preview takes the freshest inputs into
> account; the number of previews per tick follows the requested rate and the measured cost
> (about one millisecond), and hence the power of the machine.
>
> What the preview does **not** do: it does not tamper with the game's rules. Shots, spawns,
> randomness and AI remain those of the real ticks, at thirty per second. The generator does mark
> writes of the form x = x + v (`ECRIT_ACC*`, `tools/accumulateurs.py`) so as to advance a
> preview by only a fraction of a tick, but measurement showed that the game's positions do not
> go through them: a preview therefore computes the whole tick, and it is the display that places
> itself between the two.
>
> Still to do: 2D through its registers (step 3), and deciding when this mode becomes the
> default.

Goal: display at 120, 144, 165 or 240 Hz. No arbitrary rate — we pick from a closed list and
stick to it.

This document lists the constraints before a single line is written, because most of them are
otherwise discovered too late.

## 1. What will not change: the logic, at 60 Hz in the menus and **30 Hz in flight**

The game runs at a fixed step. It is not a delta-time engine that could be made to run faster by
passing it a shorter interval — and this can be measured, see below.

Its main loop (`0x020A0E4C`, decompiled in `runtime/natif.c`) runs the current scene, then, if
byte `+0x1A3` of its object is set, **waits until twenty milliseconds have elapsed** by rereading
the clock. In the menus the byte is zero: one tick per frame, 59.83 Hz. **In flight the game sets
it itself** — the console could not have sustained sixty — and since twenty milliseconds exceed
the sixteen of a frame, it only does one tick every two frames. Measured on the mission bench:
973 ticks for 2,000 frames.

The deciding experiment (`SFC_PAS_MS=10`, which lowers the threshold): the game's timers, which
advance by 136 per tick — one thirtieth of a second in fixed point — then advance by 8,160 per
second instead of 4,080. **The game runs exactly twice as fast.** The step is a compiled-in
constant, not a measurement; letting the game think more often speeds it up, nothing else.

And this port is a faithful translation of the binary: the game's loop is not rewritten.
Speeding up the simulated machine's clock would make the game run fast-forward, sound included.

**So any rate above 60 Hz is a display rate.** The game keeps thinking sixty times per second;
it is the picture that is produced more often, by interpolating between two successive states.
Nothing changes in the game's behavior: neither the difficulty, nor the speed, nor the
granularity of control.

## 2. The target rates do not divide evenly

| rate | frames per logic frame, menus (59.83 Hz) | in flight (29.91 Hz) |
|---|---|---|
| 120 Hz | 2.0058 | 4.0116 |
| 144 Hz | 2.4070 | 4.8140 |
| 165 Hz | 2.7580 | 5.5160 |
| 240 Hz | 4.0116 | 8.0232 |

In flight, then, **three to seven frames** have to be produced between two game states, not one
to three: that is where interpolation will be most visible, for better and for worse. On the
other hand, even with a 60 Hz display, combat would gain from it — it is currently displayed at
thirty.

None of them is a whole number. Even 120 and 240, so close to 2 and 4, drift by one frame
roughly every hundred and seventy frames — a visible hiccup every three seconds if one merely
counts.

So a continuous interpolation factor is needed, derived from the clock and not from a counter:
at each frame, we compute where we stand between the previous logic frame and the next one, and
interpolate at that fraction.

## 3. One frame of latency, and it is a choice

Interpolating between frames N and N+1 requires knowing N+1. So the display runs one frame
behind: **+16.7 ms in the menus, +33.4 ms in flight**, since the logic frame lasts two frames
there.

The other route would be to extrapolate from N-1 and N, with no latency, but it wobbles at every
change of direction, every collision and every explosion — precisely the moments the eye is
watching.

Decision: interpolation. To be revisited if the latency can be felt with the stylus.

## 4. Matching objects from one frame to the next

This is the hard part, and the one that decides everything else.

The DS 3D engine knows nothing of objects: it receives a stream of commands and produces a list
of polygons. To interpolate, one has to know which polygon of frame N is the same as a given
polygon of frame N+1.

Matching by geometry (position in the list, same attributes, displacement below a threshold) is
fragile: the game sorts its objects by depth, and the order changes.

A better lead: **tag each polygon at the source**. The game loads an object's matrix
(`MTX_RESTORE` with its index, or a stack) before drawing it. By recording, in `gx_geom.c`, the
matrix index and the object's rank at the moment the polygon is born, we get an identity far
more solid than a resemblance observed after the fact.

An unmatched polygon is not interpolated: it keeps its position from the most recent frame.
Whatever appears or disappears must above all not be blended.

**To be checked first**, before anything else: on real combat captures, are these tags stable
from one frame to the next? If they are not, 3D interpolation has to be rethought entirely.

## 5. 2D is interpolated through its registers, not through its pixels

Backgrounds and sprites are composited line by line from VRAM. Interpolating the finished image
blurs text and edges — unacceptable for a game in which half the screen is a map and a menu.

We have to interpolate the **inputs** of the 2D render, then render normally:

- background scrolling (`BGxHOFS`, `BGxVOFS`);
- the matrices of rotation-and-scaling backgrounds (`BGxPA`–`PD`, `BGxX/Y`);
- sprite positions in OAM.

Each intermediate frame is then a real, sharp image. It is also the most expensive part: a full
2D render, both engines, per displayed frame.

## 6. Per-scanline effects cannot be interpolated

Some effects rewrite registers at every HBlank: gradients, windows, shakes, fades. The renderer
picks them up as the logic frame is scanned out; an intermediate frame has no scan of its own.

We will reuse the per-line values of the nearest frame, interpolating only the global
registers. To be tried out on the screens that use them — transitions and fades above all.

## 7. The sound must not feel a thing

The ARM7 coprocessor mixes as the scan progresses (`snd_mix_cycles`, called per line) and the
output queue regulates itself on `snd_mix_frame`, once per logic frame. The display rate must
touch neither one: the slightest influence would change the pitch of the notes.

Acceptance criterion: the recording of the same sequence must be identical at 60 and at 240 Hz.

## 8. Syncing to the screen

Two modes:

- **synchronized**: swap interval at 1; the chosen rate must then equal the monitor's;
- **free**: waiting at the chosen step, like `pace_frame` today.

If the requested rate does not match the screen's, this is stated clearly at startup: every
other frame will be doubled or dropped, and no amount of interpolation finesse will change that.

## 9. The time budget, to be measured before making promises

Today: 156 frames per second in aerial combat (3,618 polygons), i.e. 6.4 ms per frame,
coprocessor included — it takes 30% of that.

At 240 Hz, three more renders are needed per frame. If a full render (2D for both engines plus
3D) exceeds 3 ms, 240 Hz is out of reach without optimization.

**This measurement determines the list of rates actually offered.** Fallback tier, if 2D costs
too much: render it at 60 Hz and interpolate only the 3D — the HUD stutters, the scene is
smooth.

## 10. What must never be interpolated

Cuts: screen change, fade, overlay load, entering a menu. An interpolated fade produces a ghost
image, far uglier than a repeated frame.

Safeguard: if too many polygons remain unmatched, or if the projection matrix jumps, that frame
is not interpolated and the picture is repeated.

## 11. Input latency, on the other hand, can be cut for good

Independently of the rest: sample the buttons and the touch screen at the display rate, and hand
the freshest value to the next logic frame. The game still reads once per frame, but it reads
something more recent. It is a real gain, with no downside, and one that does not depend on any
interpolation.

## 12. The tests must remain valid

Input replay, the differential bench and the explorations run on the logic frame. **No display
data may flow back into the simulated machine**: that is the condition for a session replayed at
240 Hz to be exactly the same as at 60. To be guaranteed by construction, then checked with a
fingerprint.

## Steps

0. **Measure.** Cost of a single render; stability of polygon tagging from one frame to the
   next. These two measurements decide the feasibility of 240 Hz and the matching method.
   Nothing gets written before then.
1. **Decouple.** Move everything display-related out of the VBlank, introduce a display clock
   and the choice of rate. At this stage the frame is simply repeated: no visual gain, but the
   skeleton is in place, the cost is known and the absence of regressions can be verified.
2. **Interpolate 2D** through its registers. Immediate gain on the touch-screen map, the HUD and
   the scrolling backgrounds.
3. **Interpolate 3D**: tagging at the source, matching, interpolation of vertices in clip space
   and of colors.
4. **Inputs** at the display rate.
5. **Safeguards**: thresholds, cut detection, automatic switch-off in the menus.

## How we will know it has worked

- A pan in aerial combat, at 120 Hz, without a single judder.
- Menu text as sharp as at 60 Hz.
- Identical sound, with a fingerprint to back it up.
- A replayed session identical at 60 and at 240 Hz.
- No halt during a long exploration, at each of the rates offered.

## Where things stand (September 20, 2026)

The plan above aimed to *interpolate* between two game frames. Preview ticks do better: they
*compute*. While the game waits — the tick wait in flight, the main thread's sleep elsewhere —,
the rest of its loop is replayed with the inputs of the moment, each accumulator advancing only
by the elapsed share of the tick; the resulting frame, 3D **and** 2D, is the game's frame at
that instant. Then everything goes back exactly as it was: memory page by page, registers, 3D
engine, link, threads. The game itself plays its real tick as on the console.

What remains interpolated: the frames for which the machine did not have time to play a
preview. That is where the adaptive part lies — the scheduler measures what a preview costs and
spaces them `max(1/fps, 1.3 × cost)` apart. On a fast machine, almost everything is computed; on
a slow machine, the display glides between ticks as before, and the game never stalls.

Measured on the development machine, in flight, requested rate 240: 212 frames displayed per
second, 60 game ticks, 72 preview ticks — a preview costing 4.4 ms, its two 2D screens included.

What proves it: the player's session replayed in full, 120,000 frames, 118,785 preview ticks
played then erased — fingerprints identical to those of a game that plays none; sound identical
byte for byte; and `SFC_APERCU_VERIF=1`, which compares the entire memory and the registers after
each preview and stops the session at the first byte that has moved.

## Interpolation on the GPU (September 26, 2026)

Between two game frames (real or preview), each displayed frame used to be computed by the CPU:
`interpoler` (`runtime/entre_images.cpp`) returned the list of polygons for that instant,
`draw_polygons` looked up their textures one by one and sent half a megabyte of vertices. The
computation is now done by the GPU. Once per pair of frames, `planifier` draws up the plan of the
path: for each position in `interpoler`'s list, the two ends, the decisions that depend only on
the two frames (neighborhood, stiff shape, fast-changing size, scrolling texture, face
orientation) and, when the shape shown at a given instant does not carry the state — texture,
attributes — of B's polygon, a second polygon that is drawn only at those instants. The plan goes
to the GPU in batches sharing the same state; at each displayed frame, a geometry shader receives
each polygon whole (four vertices as “lines adjacency”) and redoes `interpoler` at the desired
instant — midpoint, prediction, flip, pixel snapping of the interface —, with the same integer
arithmetic: the 64-bit product in `entre` is taken in 32-bit pieces. `SFC_INTERPOLATION=cpu`
goes back to the CPU.

Compared with the CPU, on 32 sub-frames in flight and 21 predicted frames, then at six places in
the session (menus, flight, scenes of 1,000 to 1,500 polygons): identical, to within 0–5 edge
pixels — those of the predetermined paths (unmatched particles), which the CPU computes in double
precision. At 165 frames per second, the display thread takes 17 to 19% of a core instead of
18.5 to 20.6: all that remains per frame is the presentation and, at each new game frame,
matching (`apparier`, seven points) and the plan. Uncapped, the display sustains 2,000 to 2,650
frames per second depending on whether or not the driver spreads its work over a second thread,
and the GPU is then 55% busy (35% when the CPU was interpolating): it is presenting the frames,
no longer computing them, that sets the limit.

Each vendor — NVIDIA, AMD, Intel — has its own shader compiler, and what it does with integer
arithmetic can only be seen by asking it. At startup, the game therefore has the GPU compute
test polygons that go through every path in the shader (ordinary counterpart, huge coordinates,
flat interface, stiff shape, fast-changing size, neighborhood, changed shape, no counterpart,
vanished, flip, two states, invisible polygon, translucent, vertex passing behind the camera),
at nine instants, with B both real and a preview; it reads the result back (*transform
feedback*) and compares it vertex by vertex with `interpoler`. At the slightest discrepancy — or
if the driver lacks an OpenGL 3.0 function, or the shader fails to link —, interpolation stays
on the CPU, and the log says why. It also says, from the very first line, which driver and which
card the game found.

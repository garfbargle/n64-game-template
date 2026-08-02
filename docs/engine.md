# The engine

What `engine/` gives you, and the handful of rules the hardware imposes that
are not obvious from the function names.

The headers are the reference — every function is documented where it is
declared, and that is the copy kept true. This is the orientation: what exists,
why it is shaped the way it is, and the things that will bite you.

## The contract

A game defines three functions and nothing else:

```c
void gameInit(void);          /* once, after the engine is up */
void gameUpdate(float delta); /* every graphics callback */
void gameDraw(void);          /* callbacks that actually build a frame */
```

`engine/src/boot.c` calls them. There is no other entry point; a game never
writes `mainproc`.

### Why update and draw are separate

NuSystem calls the graphics callback once per video retrace, whether or not the
previous frame has finished drawing. The engine runs your logic on every one of
those and builds a frame only when the RSP is idle.

That is not an optimisation, it is a correctness requirement. One heavy task
can outlive a retrace; queuing a second in that case lets NuSystem rotate
framebuffers while the RDP is still writing the older one, which shows up as
tearing followed by corrupt UI. Submitting only after the previous task has
drained lets the display pace itself to the real cost.

So on a heavy scene the callback runs at 60 Hz and draws at 20, and `delta` is
how the game is told the difference. It is measured in 60 Hz frames — `1.0` at
retrace rate, `~2.0` at 30 fps — so constants written against it mean the same
thing on NTSC and PAL.

**Multiply everything that moves by `delta`.** A game that advances by a fixed
amount per call speeds up when the scene gets simpler, which is the single most
common way an N64 game ends up feeling wrong.

**Do not change state in `gameDraw`.** It is skipped on busy callbacks, so
anything advanced there advances at a rate that depends on how hard the scene
is to render.

## The frame

```c
gfxBeginFrame(r, g, b);   /* pick the buffer, set up the RCP, clear */
  /* 3D */
  gfxSelectViewport(0);
  gfxLoadPerspective(60.f, 10.f, 4000.f);
  gfxLookAt(...);
  gfxBeginTextured();     /* or gfxBeginShaded() */
  /* ... */
  gfxSelectFullViewport();
  gfxBeginFills();        /* every flat-colour rectangle */
  gfxBeginTextures();     /* every glyph */
gfxEndFrame();            /* hand it to the RSP */
```

### The two 2D phases

**This is the rule that will lock a real console if you break it.** The RDP
cannot be reconfigured between fill mode and texture mode in the middle of a
card. A screen draws *all* of its fills, then *all* of its text. Text, then a
button, then more text is the hazard.

It does not fail on emulators, and on hardware whether it bites depends on how
busy the pipe still is — so it tracks scene complexity and presents as an
intermittent lock-up in whatever you were doing at the time. See
[hardware.md](hardware.md).

This is why every UI helper documents which phase it belongs to, why a legend
is drawn by two calls (`drawLegendIcons`, `drawLegendLabels`) rather than one,
and why `game/src/hud.c` writes each screen twice off shared layout constants.
There is deliberately no call that does both halves, because there is no phase
in which it would be legal.

### Matrices and double buffering

Anything the RSP reads must be double-buffered on `dl_no`, because the task
drawing the previous frame may still be walking the list that points at it:

```c
static Mtx crate_matrix[NUM_DISPLAY_LISTS];

modelMatrix(&crate_matrix[dl_no], 0.f, spin, 0.f, x, y, z);
gfxPushMatrix(&crate_matrix[dl_no]);
```

Getting this wrong produces geometry that flickers between two poses — a
symptom worth memorising, because it looks like a physics bug.

`modelMatrix` composes a rotation and a translation into **one** `Mtx` and one
`gSPMatrix`. Vertices go through row-vector, so the pair composes to `rotation *
translation`, and a rotation's bottom row is `(0, 0, 0, 1)`: the product is the
rotation with its bottom row replaced. Half the matrix memory and half the
commands, for no loss.

### Precision

The N64 matrix format is s15.16, so a translation loses sub-unit precision past
about ±32000 units of the origin. A game whose world is larger than that has to
keep a render origin and subtract it at the moment a world position becomes an
`Mtx`. Pick a world scale early and write it down.

The depth buffer is heavily non-linear and almost all of its precision sits near
the near plane. Z-fighting in the distance is usually asking for a *further near
plane*, not a closer far one.

### Lighting

Off by default, and worth leaving off. Without it a vertex's colour slot is its
colour, so light baked in at authoring time costs the RSP nothing per frame.
With `gfxSetLighting(TRUE)`, that same slot is read as a **normal** and the RSP
computes the colour per vertex.

Geometry authored for one path draws as nonsense under the other, so switch per
pass and keep each pass's models to one convention.

## Drawing 2D

| | |
| --- | --- |
| `text.h` | `drawString`, `drawCenteredString`, `drawLargeString`, `drawUnsigned`, `charWidth`, `stringWidth` |
| `ui.h` | button icons, legends, check marks, meters, bars, panels |

The font has **no lowercase** — `i` lands on the colon — so capitalise UI text
or draw the missing glyphs in `generate_assets.py`.

`beginText` writes each glyph cell whole, so a word arrives in its own opaque
box: invisible on a flat card, and the only thing that looks pasted on when
there is a scene behind it. `beginTextBlended` cuts the glyph out of its cell
using the alpha the I4 format already carries. The starter game uses the plain
one on its title card and the blended one over the 3D scene, which is the
distinction worth copying.

Sprites are run-length lists of horizontal spans, which is how a 13×13 button
costs a dozen rectangles instead of a texture load. Every group is drawn in
passes over the whole set — every shell, then every face, then every glyph — so
a row costs three fill colours rather than three per button. That grouping is
not tidiness: `gDPSetFillColor` is an RDP attribute change and needs the pipe
drained first, or it lands on spans of a primitive still in flight.

## Input

`inputUpdate()` runs before your logic; after it, `inputHeld`, `inputPressed`
and `inputReleased` answer for the frame.

The stick is shaped, and this matters more than it sounds. A real N64 stick
rests a few units off centre, wears with age, and reaches maybe 68 of its
nominal 80 units at full deflection — every controller differs. `inputStickX`
and `inputStickY` apply a dead zone, rescale so the edge of the gate reads as
1.0, and clamp to the unit circle so a diagonal is not 1.41× faster than a
straight push. `STICK_DEAD_ZONE` and `STICK_RANGE` are the constants worth
retuning against real hardware.

`inputMenuVertical` and `inputMenuHorizontal` give a held direction that
repeats, answering to the stick and the D-pad together — what a player expects
and what nothing gives you for free.

Hot-plugging is not detected; the console does not report it. A game with
drop-in co-op polls for a START on an unused port.

## Saving

`storage.h` writes to a **flashcart SD card** via libcart and FatFS — not a
Controller Pak, not cartridge SRAM. That buys real files at real sizes, at the
cost of only working on a flashcart.

**Every call answers honestly when there is no card, and your game must stay
playable when `saving_available` is FALSE.** Emulators generally have no cart,
so that is the common case, not the edge case. `storageStatusText()` gives a
short line for a title screen, and distinguishes a missing cart from an exFAT
card — on hardware, that distinction is the whole diagnosis.

Writes are transactional: a temporary file, synced, then a rotate and a rename.
Power lost at any point leaves either the old save or the new one, never half
of either. Deleting renames rather than erases, so undo survives a power cycle.

`storageSave`/`storageLoad` block. That is honest for a few KB from a menu. For
anything larger use `storageBeginSave`/`storageStepSave` and draw a progress bar
between steps, or a successful save is indistinguishable from a crash.

## When it freezes

A real N64 that locks up shows a still picture and nothing else: no console, no
debugger, no core dump. `diag.h` is what makes that still picture say something.

A high-priority watchdog thread, woken by a hardware timer, keeps running when
the graphics thread is stuck in a loop *and* when it has been stopped by a CPU
exception. Once the frame heartbeat goes stale it paints the last recorded phase
colour into both framebuffers, forever — so the colour left on screen names the
subsystem that died — and writes a post-mortem to the SD card if one is there.

Call `diagPaintPhase` around anything that can take more than a frame, and use
the `DIAG_PHASE_GAME_*` colours so yours cannot be confused with the engine's.
`diagSetReportHook` adds your own lines to the post-mortem; it runs from the
watchdog thread after the console is already dead, so it may read globals and
must do nothing else.

`tools/resolve_freeze.sh` turns the recorded PC and RA into source lines against
the `.out` symbol map.

## What the engine deliberately does not have

No scene graph, no entity system, no allocator, no collision, no animation
system, no asset manager. Those are game decisions, they are cheap to write
badly in a way that is expensive to undo, and a 4 MiB machine punishes generic
solutions. The engine stops at the hardware boundary on purpose.

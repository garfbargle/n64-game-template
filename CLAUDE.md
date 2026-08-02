# Working in this repository

## The layout

`engine/` is the reusable N64 runtime — boot, frame loop, graphics, text, UI
sprites, input, audio, flashcart saving, freeze diagnostics. `game/` is the
game built on it. Code that would be useful to a different game belongs in
`engine/`; code about *this* game belongs in `game/`. When in doubt it goes in
`game/`, because moving it down later is easy and moving it back up is not.

[docs/engine.md](docs/engine.md) is the orientation; the headers under
`engine/include` are the reference, and they are the copy kept true.

## Look at things offline before reaching for the emulator

`tools/preview` draws the project's own data on this machine in about a quarter
of a second:

```sh
tools/preview/buttons.py --legend --crt
```

It reads the `UiSpan` tables out of `engine/src/ui.c` and the legend table out
of `game/src/hud.c`, and `render.py` reads vertex arrays out of the game's own
sources — so these answer questions about pose, framing, occlusion and whether
a sprite survives composite video against the real data rather than a copy of
it. Writing a ten-line script against `tools/preview/render.py` is the normal
way to answer a question it has no flag for; see
[docs/offline-preview.md](docs/offline-preview.md).

`tools/emu` is for the interface and for behaviour that needs the game running:
menus, HUD flows, loading. It costs a ROM build and an emulator run, and a
script has to walk to whatever it wants to look at. That price is worth paying
for a menu layout. It is not worth paying to find out which way a model faces.

Neither tool settles performance or RDP behaviour. Those belong on hardware —
see [docs/hardware.md](docs/hardware.md).

## Rules the hardware imposes

These are not style preferences. Each has a failure mode that appears only on a
real console, or only sometimes.

- **Never interleave fill-mode sprites and text.** Draw every rectangle, then
  every glyph. Reconfiguring the RDP between the two mid-screen locks real
  hardware and does nothing on emulators. This is why a legend is
  `drawLegendIcons` and `drawLegendLabels` rather than one call, and why each
  screen in `game/src/hud.c` is written twice off shared layout constants.
- **Double-buffer anything the RSP reads**, indexed on `dl_no`. The task
  drawing the previous frame may still be walking it. The symptom is geometry
  flickering between two poses, which reads as a logic bug.
- **Multiply anything that moves by `delta`.** `gameUpdate` runs per retrace,
  `gameDraw` only when the RSP is idle; a fixed step per call runs faster on
  simpler scenes.
- **Group fill colours into passes.** `gDPSetFillColor` needs the pipe drained
  first, or it lands on spans of a primitive still in flight — a half-dark
  sprite on hardware, solid on an emulator.
- **The game must stay playable with no flashcart.** `saving_available` is
  FALSE on most emulators and on every bare cartridge.

## Verify by building

The build is the test. It runs `tools/check_ram.py`, which fails loudly if the
image would overrun the framebuffers:

```sh
docker run --rm --platform linux/amd64 -v "$PWD:/work" -w /work n64-nusys-build:local make -j4
```

Build the audio variant too (`make audio`) after touching anything it compiles
differently — it has its own spec file and its own RAM ceiling.

Kill stray `mupen64plus` processes before an emulator run. A leftover instance
holds the GL context, and the next run will appear to hang.

## Comments

Comments here explain *why*, and are worth writing at length where the reason
is not recoverable from the code — a hardware hazard, a measured number, a
choice that looks arbitrary and is not. Match that. A comment restating what
the line does is worse than none.

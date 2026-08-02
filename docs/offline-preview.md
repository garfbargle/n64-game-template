# Offline preview

Most questions about how something looks are questions about arithmetic, and
none of that arithmetic needs an N64. A model is a handful of boxes and quads
run through `guRotateRPY` and `guPerspective`; a button is a list of fill
rectangles. `tools/preview` reads the real data out of the source, does the
same maths the RSP and RDP would, and writes a PNG in about a quarter of a
second.

The same question asked through the emulator costs a ROM build, a boot, and a
scripted timeline that has to walk to whatever it wants to look at. Reach for
the preview first, and keep the emulator for what only it can show: the
interface in its real framebuffer, and behaviour that needs the game running.

Neither settles performance or RDP behaviour. Those belong on hardware — see
[hardware.md](hardware.md).

## Sprites: `buttons.py`

Buttons, meters and check marks are fill-rectangle sprites, so the same trick
applies twice over: read the `UiSpan` tables and `ButtonStyle` initialisers out
of `engine/src/ui.c` and replay the rectangles onto a bitmap. The sprite in the
picture is the sprite in the ROM.

```sh
tools/preview/buttons.py                 # every button, zoomed
tools/preview/buttons.py --legend        # a legend row, icons and labels
tools/preview/buttons.py --legend --crt  # the same, through fake composite
```

`--legend` is the picture worth checking. A button is only good if it still
reads at 1× with a label beside it, and `--crt` is the cheap way to find out
whether a one-pixel outline survives a composite signal — it fakes
full-bandwidth luma and smeared chroma, which is the part of a television that
destroys UI. It is a sanity check, not a television.

The legend it draws is `play_legend` from `game/src/hud.c`, read from the same
table the ROM walks, so a control that moves in the game moves here too.

## Models: `render.py`

A software rasteriser that reproduces the parts of the pipeline that decide
framing:

- `guRotateRPY`'s matrix, row-vector (`v' = v * M`), X then Y then Z — the same
  layout `modelMatrix` builds.
- `guPerspective` at the game's FOV, 320×240, and its near plane.
- `G_ZBUFFER`, `G_SHADE | G_SHADING_SMOOTH` and `G_CULL_BACK`: a z-buffer,
  per-vertex colours interpolated across the triangle, and back faces dropped
  by screen-space winding.

That last one earns its keep on its own. Back-face culling is why a floor drawn
with the wrong winding is invisible from above and perfectly solid from
underneath, and this will show you that in a quarter of a second.

What it does not do: textures, fog, and the RDP's exact fixed-point rounding.
It answers questions about pose, framing and occlusion.

It reads every `.c` under `game/src` and `engine/src`, plus the headers, so it
finds models wherever they live. It understands the `ENGINE_VERTEX` convention
— any macro whose name ends in `VERTEX` is parsed as `(x, y, z, r, g, b)` —
which is what `ENGINE_SHADED_BOX` expands to. Textured models, whose vertices
carry texture coordinates where a shaded one carries a colour, are not
understood; name their macro something that does not end in `VERTEX`, as
`game/src/scene.c` does, or the tool will read a texture coordinate as red.

There is no command-line interface, because the useful questions are all
different. Write a ten-line script:

```python
import sys; sys.path.insert(0, "tools/preview")
from render import Geometry, Scene, rpy

g = Geometry.load()
s = Scene()
s.add(g.mesh("pedestal_verts", "shaded_box_display_list"), rpy(0, 35, 0), (0, 0, 0))
s.ground(y=0)
s.look_at((0, 10, 0), distance=320, yaw=35, pitch=18)
s.save("build/preview.png")
```

`Geometry.load()` parses the sources; `mesh(verts, display_list)` pairs a vertex
array with the triangle order that walks it; `Scene.add` takes a rotation matrix
and a translation; `look_at` frames it; `save` writes the PNG.

`Scene.image_with_crosshair()` draws the screen centre over the result, which is
the question to ask about anything that has to line up with where the player is
aiming.

## Shared machinery: `sprites.py`

`buttons.py` is built on `sprites.py`, and so is anything else you write that
wants to replay the engine's 2D drawing offline:

| | |
| --- | --- |
| `source()` | `engine/src/ui.c` as text |
| `spans(src, name)` | a `UiSpan` table, parsed |
| `meter_style(src, name)` | a `UiMeterStyle`, parsed |
| `Canvas` | a bitmap with `gDPFillRectangle`'s inclusive-corner semantics |
| `draw_spans` / `draw_meter` | the same passes `ui.c` makes, in the same order |
| `crt(img)` | the fake composite pass |
| `zoom(img, n)` | nearest-neighbour, so pixels stay pixels |

Parsing the source rather than restating its numbers is the whole point: a
preview that carries its own copy of a layout constant will eventually disagree
with the ROM, and it will do so silently.

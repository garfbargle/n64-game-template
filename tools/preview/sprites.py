#!/usr/bin/env python3
"""Draw the engine's UI sprites on this machine, without a console.

Buttons, check marks and meters are pixel sprites made of axis-aligned fill
rectangles, and none of that needs an N64 to evaluate.  This reads the span
tables, the styles and the layout constants straight out of engine/src/ui.c
and replays the same rectangles onto a bitmap -- so the picture is the sprite
the ROM draws, rather than a drawing of what it is supposed to be.

Import it from a script to preview a card you are laying out:

    from sprites import Canvas, source, spans, draw_spans, crt, zoom

What it does not reproduce: the VI's anti-alias and de-flicker filters, and
composite video.  `crt()` fakes the latter well enough to judge whether a
one-pixel outline survives; it is a sanity check, not a television.
"""

import os
import re

import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
UI_C = os.path.join(ROOT, "engine", "src", "ui.c")


def source(path=None):
    with open(path or UI_C) as handle:
        return handle.read()


def define(src, name):
    m = re.search(r"^#define\s+%s\s+(\d+)" % name, src, re.M)
    if not m:
        raise SystemExit("no #define %s in %s" % (name, UI_C))
    return int(m.group(1))


def spans(src, name):
    """Pull one `static const UiSpan <name>[] = { ... }` out of the source."""
    m = re.search(
        r"static const UiSpan\s+%s\s*\[\]\s*=\s*\{(.*?)\};" % name, src, re.S
    )
    if not m:
        raise SystemExit("no UiSpan table named %s in %s" % (name, UI_C))
    return [tuple(int(v) for v in row) for row in
            re.findall(r"\{\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\}",
                       m.group(1))]


def meter_style(src, name):
    """Pull one `const UiMeterStyle <name> = { ... }` apart."""
    m = re.search(
        r"(?:static )?const UiMeterStyle\s+%s\s*=\s*\{(.*?)\};" % name, src, re.S
    )
    if not m:
        raise SystemExit("no UiMeterStyle named %s in %s" % (name, UI_C))
    body = m.group(1)
    head = body.split("{", 1)[0]
    names = re.findall(r"[a-z_]+_spans", head)
    colors = [tuple(int(v) for v in c) for c in
              re.findall(r"\{\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\}", body)]
    # outline_spans, inner_spans, width, height, pitch
    counts = [int(v) for v in re.findall(r"\d+", head)][:5]
    return {
        # A style may pass NULL for its outline and draw one silhouette in two
        # colours, so only the inner table is named.
        "outline": spans(src, names[0]) if counts[0] > 0 else [],
        "inner": spans(src, names[-1]),
        "outline_spans": counts[0],
        "inner_spans": counts[1],
        "width": counts[2],
        "height": counts[3],
        "pitch": counts[4],
        "outline_color": colors[0],
        "empty_color": colors[1],
        "fill_color": colors[2],
    }


def rgba5551(color):
    """The framebuffer keeps five bits a channel; quantise like the RDP."""
    return tuple((c >> 3) * 255 // 31 for c in color)


class Canvas:
    def __init__(self, width, height, background=(24, 28, 44)):
        self.px = np.zeros((height, width, 3), np.uint8)
        self.px[:, :] = background

    def fill_rect(self, x0, y0, x1, y1, color):
        """gDPFillRectangle: inclusive on both corners."""
        h, w = self.px.shape[:2]
        x0, y0 = max(0, x0), max(0, y0)
        x1, y1 = min(w - 1, x1), min(h - 1, y1)
        if x1 >= x0 and y1 >= y0:
            self.px[y0:y1 + 1, x0:x1 + 1] = rgba5551(color)


def draw_spans(canvas, table, count, x, y, clip_width, color):
    for span in table[:count]:
        x0, y0, x1, y1 = span
        if x0 >= clip_width:
            continue
        if x1 >= clip_width:
            x1 = clip_width - 1
        canvas.fill_rect(x + x0, y + y0, x + x1, y + y1, color)


def draw_meter(canvas, style, x, y, value, max_value):
    """The three passes drawUiMeter makes, in the same order."""
    units = max_value // 2
    for i in range(units):
        draw_spans(canvas, style["outline"], style["outline_spans"],
                   x + i * style["pitch"], y, style["width"],
                   style["outline_color"])
    for i in range(units):
        if value < (i + 1) * 2:
            draw_spans(canvas, style["inner"], style["inner_spans"],
                       x + i * style["pitch"], y, style["width"],
                       style["empty_color"])
    for i in range(units):
        unit = min(2, value - i * 2) if value > i * 2 else 0
        if unit > 0:
            draw_spans(canvas, style["inner"], style["inner_spans"],
                       x + i * style["pitch"], y,
                       style["width"] if unit == 2
                       else (style["width"] + 1) // 2,
                       style["fill_color"])


def meter_width(style, max_value):
    return (max_value // 2 - 1) * style["pitch"] + style["width"]

def crt(img):
    """A crude composite pass: full-bandwidth luma, chroma smeared sideways."""
    a = np.asarray(img).astype(np.float32)
    y = a @ np.array([0.299, 0.587, 0.114], np.float32)
    cb, cr = a[:, :, 2] - y, a[:, :, 0] - y
    k = np.array([0.15, 0.2, 0.3, 0.2, 0.15], np.float32)
    for ch in (cb, cr):
        pad = np.pad(ch, ((0, 0), (2, 2)), mode="edge")
        ch[:] = sum(k[i] * pad[:, i:i + ch.shape[1]] for i in range(5))
    ky = np.array([0.25, 0.5, 0.25], np.float32)
    pad = np.pad(y, ((0, 0), (1, 1)), mode="edge")
    y = sum(ky[i] * pad[:, i:i + y.shape[1]] for i in range(3))
    out = np.stack([y + cr, y - 0.344 * cb - 0.714 * cr, y + cb], -1)
    return Image.fromarray(out.clip(0, 255).astype(np.uint8))


def zoom(img, scale):
    return img.resize((img.width * scale, img.height * scale), Image.NEAREST)

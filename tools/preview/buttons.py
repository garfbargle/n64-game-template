#!/usr/bin/env python3
"""Draw the controller-button sprites on this machine, without a console.

Buttons are fill-rectangle sprites like the meters and check marks, so the
same trick applies: read the `UiSpan` tables and `ButtonStyle` initialisers
out of engine/src/ui.c and replay the rectangles onto a bitmap.  The sprite in
the picture is the sprite in the ROM.

    tools/preview/buttons.py                # every button, zoomed
    tools/preview/buttons.py --crt          # through fake composite
    tools/preview/buttons.py --legend       # a legend row, at 1x

`--legend` is the picture worth checking: a button is only good if it still
reads at 1x with a label beside it, on a television.
"""

import argparse
import os
import re

import numpy as np
from PIL import Image

import sprites
from sprites import Canvas, crt, draw_spans, zoom, source, spans

ROOT = sprites.ROOT

# The full set, in ButtonIconId order.
ORDER = ["button_a", "button_b", "button_start", "button_c_up", "button_c_down",
         "button_c_left", "button_c_right", "button_l", "button_r", "button_z",
         "button_stick", "button_dpad"]


def _shell_color(src):
    m = re.search(r"button_shell_color\[3\]\s*=\s*\{([^}]*)\}", src)
    return tuple(int(v) for v in re.findall(r"\d+", m.group(1)))


def _macro_shape(src, kind):
    """The ROUND_BUTTON / WIDE_BUTTON macro bodies carry the span counts and
    the sprite size, so read them rather than restating them here."""
    m = re.search(r"#define %s\(([^)]*)\)((?:.*\\\n)*.*)" % kind, src)
    body = m.group(2).replace("\\\n", " ")
    names = re.findall(r"button_\w+_spans", body)
    nums = [int(v) for v in re.findall(r"(?<![\w/])\d+(?![\w])", body)]
    # shell_spans, face_spans, then (glyph_x, glyph_y,) width, height
    return names, nums


def load_styles():
    src = source()
    styles = {}
    shapes = {kind: _macro_shape(src, "%s_BUTTON" % kind)
              for kind in ("ROUND", "WIDE", "CROSS")}
    shell = _shell_color(src)

    for name in ORDER:
        m = re.search(
            r"static const ButtonStyle\s+%s\s*=\s*\{(.*?)\};" % name, src, re.S)
        body = m.group(1)
        macro = re.search(r"(ROUND|WIDE|CROSS)_BUTTON\(([^)]*)\)", body)
        kind = macro.group(1)
        args = [a.strip() for a in macro.group(2).split(",")]
        glyph_name = args[0]
        names, nums = shapes[kind]
        shell_name, face_name = names[0], names[1]
        shell_n, face_n = nums[0], nums[1]
        if kind == "WIDE":
            # WIDE_BUTTON fixes its glyph offset in the macro body.
            gx, gy, w, h = nums[2], nums[3], nums[4], nums[5]
        else:
            gx, gy = int(args[1]), int(args[2])
            w, h = nums[2], nums[3]

        colors = _resolve_colors(src, body[macro.end():])
        styles[name] = {
            "shell": spans(src, shell_name)[:shell_n],
            "face": spans(src, face_name)[:face_n],
            "glyph": spans(src, glyph_name),
            "glyph_x": gx, "glyph_y": gy,
            "width": w, "height": h,
            "shell_color": shell,
            "face_color": colors[0],
            "glyph_color": colors[1],
        }
    return styles


def _resolve_colors(src, tail):
    """The two colours after the macro are either a literal brace triple or a
    BUTTON_* #define that expands to one."""
    out = []
    for token in tail.split(","):
        token = token.strip().strip("{}").strip()
        if not token:
            continue
        if re.match(r"^BUTTON_[A-Z_]+$", token):
            m = re.search(r"#define %s\s*\{([^}]*)\}" % token, src)
            out.append(tuple(int(v) for v in re.findall(r"\d+", m.group(1))))
        else:
            out.append(token)
    # Literal triples arrive as three separate numeric tokens.
    flat, nums = [], []
    for item in out:
        if isinstance(item, tuple):
            if nums:
                flat.append(tuple(nums))
                nums = []
            flat.append(item)
        else:
            nums.append(int(item))
            if len(nums) == 3:
                flat.append(tuple(nums))
                nums = []
    return flat


def draw_button(canvas, style, x, y):
    draw_spans(canvas, style["shell"], len(style["shell"]), x, y,
               255, style["shell_color"])
    draw_spans(canvas, style["face"], len(style["face"]), x, y,
               255, style["face_color"])
    draw_spans(canvas, style["glyph"], len(style["glyph"]),
               x + style["glyph_x"], y + style["glyph_y"], 255,
               style["glyph_color"])


def sheet(styles):
    pad = 5
    width = pad + sum(styles[n]["width"] + pad for n in ORDER)
    height = 11 + pad * 2
    canvas = Canvas(width, height, background=(24, 29, 34))
    x = pad
    for name in ORDER:
        style = styles[name]
        draw_button(canvas, style, x, pad + (11 - style["height"]) // 2)
        x += style["width"] + pad
    return canvas


FONT_5X7 = {
    "A": ["01110", "10001", "10001", "11111", "10001", "10001", "10001"],
    "B": ["11110", "10001", "10001", "11110", "10001", "10001", "11110"],
    "C": ["01110", "10001", "10000", "10000", "10000", "10001", "01110"],
    "E": ["11111", "10000", "10000", "11110", "10000", "10000", "11111"],
    "I": ["11111", "00100", "00100", "00100", "00100", "00100", "11111"],
    "J": ["00111", "00010", "00010", "00010", "10010", "10010", "01100"],
    "K": ["10001", "10010", "10100", "11000", "10100", "10010", "10001"],
    "M": ["10001", "11011", "10101", "10101", "10001", "10001", "10001"],
    "N": ["10001", "11001", "10101", "10101", "10011", "10001", "10001"],
    "P": ["11110", "10001", "10001", "11110", "10000", "10000", "10000"],
    "R": ["11110", "10001", "10001", "11110", "10100", "10010", "10001"],
    "S": ["01111", "10000", "10000", "01110", "00001", "00001", "11110"],
    "T": ["11111", "00100", "00100", "00100", "00100", "00100", "00100"],
    "U": ["10001", "10001", "10001", "10001", "10001", "10001", "01110"],
    "D": ["11110", "10001", "10001", "10001", "10001", "10001", "11110"],
    "F": ["11111", "10000", "10000", "11110", "10000", "10000", "10000"],
    "G": ["01110", "10001", "10000", "10111", "10001", "10001", "01110"],
    "H": ["10001", "10001", "10001", "11111", "10001", "10001", "10001"],
    "L": ["10000", "10000", "10000", "10000", "10000", "10000", "11111"],
    "O": ["01110", "10001", "10001", "10001", "10001", "10001", "01110"],
    "V": ["10001", "10001", "10001", "10001", "10001", "01010", "00100"],
    "W": ["10001", "10001", "10001", "10101", "10101", "11011", "10001"],
    "X": ["10001", "10001", "01010", "00100", "01010", "10001", "10001"],
    "Y": ["10001", "10001", "01010", "00100", "00100", "00100", "00100"],
    "Z": ["11111", "00001", "00010", "00100", "01000", "10000", "11111"],
}


def text(canvas, string, x, y, color=(224, 228, 219)):
    for chr_index, chr in enumerate(string):
        rows = FONT_5X7.get(chr)
        if not rows:
            continue
        for row_index, row in enumerate(rows):
            for col, bit in enumerate(row):
                if bit == "1":
                    canvas.fill_rect(x + chr_index * 7 + col, y + row_index + 1,
                                     x + chr_index * 7 + col, y + row_index + 1,
                                     color)


CHAR_WIDTH = {"i": 3, ":": 3, ".": 3, " ": 3, "'": 3, ",": 3,
              "l": 4, "!": 4, "t": 5, "k": 6}


def string_width(s):
    """engine/src/text.c's charWidth, so a legend measures the same here as
    it does on screen."""
    return sum(CHAR_WIDTH.get(c, 7) for c in s)


def legend(styles):
    """A legend row from game/src/hud.c, both phases, off the same table the
    ROM walks -- so a control that moves in the game moves here too."""
    hud_c = open(os.path.join(ROOT, "game", "src", "hud.c")).read()
    ui_h = open(os.path.join(ROOT, "engine", "include", "ui.h")).read()
    d = {}
    for m in re.finditer(r"^#define (LEGEND_\w+)\s+(\d+)$", ui_h, re.M):
        d[m.group(1)] = int(m.group(2))

    table = re.search(r"play_legend\[\]\s*=\s*\{(.*?)\};", hud_c, re.S)
    entries = re.findall(
        r"\{\s*BUTTON_ICON_(\w+)\s*,\s*BUTTON_ICON_(\w+)\s*,\s*\"([^\"]*)\"\s*\}",
        table.group(1))

    def style_of(icon):
        return styles.get("button_" + icon.lower())

    widths = []
    for icon, icon2, label in entries:
        width = style_of(icon)["width"]
        if style_of(icon2) is not None:
            width += d["LEGEND_PAIR_GAP"] + style_of(icon2)["width"]
        widths.append(width + d["LEGEND_ICON_GAP"] + string_width(label))
    total = sum(widths) + d["LEGEND_ENTRY_GAP"] * (len(entries) - 1)

    canvas = Canvas(320, d["LEGEND_ROW_HEIGHT"] + 6, background=(24, 28, 44))
    x = (320 - total) // 2
    y = 3
    for (icon, icon2, label), width in zip(entries, widths):
        style = style_of(icon)
        icon_width = style["width"]

        draw_button(canvas, style, x,
                    y + (d["LEGEND_ROW_HEIGHT"] - style["height"]) // 2)
        if style_of(icon2) is not None:
            second = style_of(icon2)
            draw_button(canvas, second, x + icon_width + d["LEGEND_PAIR_GAP"],
                        y + (d["LEGEND_ROW_HEIGHT"] - second["height"]) // 2)
            icon_width += d["LEGEND_PAIR_GAP"] + second["width"]
        text(canvas, label, x + icon_width + d["LEGEND_ICON_GAP"],
             y + d["LEGEND_LABEL_DROP"], color=(200, 206, 224))
        x += width + d["LEGEND_ENTRY_GAP"]
    return canvas


def main():
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--legend", action="store_true",
                    help="a legend row, icons and labels together")
    ap.add_argument("--crt", action="store_true")
    ap.add_argument("--zoom", type=int, default=6)
    ap.add_argument("-o", "--out", default="buttons.png")
    args = ap.parse_args()

    styles = load_styles()
    if args.legend:
        canvas = legend(styles)
    else:
        canvas = sheet(styles)
    img = Image.fromarray(canvas.px)
    if args.crt:
        img = crt(img)
    img = zoom(img, args.zoom)
    img.save(args.out)
    print("%s  %dx%d" % (args.out, img.width, img.height))


if __name__ == "__main__":
    main()

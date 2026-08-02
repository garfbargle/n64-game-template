#include "ui.h"
#include "gfx.h"
#include "text.h"

void setFillColor(u8 r, u8 g, u8 b) {
  u32 color = GPACK_RGBA5551(r, g, b, 1);
  gDPSetFillColor(dlp++, (color << 16) | color);
}

/* `clip_width` keeps only the leftmost columns of the sprite, which is how a
   half unit of a meter is drawn: the same spans, cut down the middle. */
void drawUiSpans(const UiSpan *spans, u8 count, u32 x, u32 y,
    u8 clip_width) {
  u8 i;

  for (i = 0; i < count; i++) {
    u8 x1 = spans[i].x1;

    if (spans[i].x0 >= clip_width) {
      continue;
    }
    if (x1 >= clip_width) {
      x1 = clip_width - 1;
    }
    gDPFillRectangle(dlp++, x + spans[i].x0, y + spans[i].y0, x + x1,
      y + spans[i].y1);
  }
}

/*
 * Controller buttons, as the controller wears them: A blue, B green, the four
 * C buttons yellow with an arrow, START red, and the shoulders long and grey.
 * Naming a button by drawing it is shorter than naming it in words, and it
 * survives a player who has never read the manual.
 *
 * Same UiSpan machinery as drawUiMeter below, and the same three passes:
 * shell, face, glyph.  A button is a fill-mode sprite, so it must be drawn in
 * a screen's fills phase and never between two runs of text -- swapping the
 * RDP back and forth mid-card is the hazard that locks the console (see
 * docs/hardware.md).
 */

/* Diameter thirteen.  Eleven was tried first and is the wrong answer: it
   leaves a seven-pixel interior, which a five-by-seven letter fills edge to
   edge, and a letter with no face showing around it stops looking like a
   button and starts looking like a smudge.  Thirteen buys two pixels of face
   on every side, which is the whole difference. */
static const UiSpan button_round_shell_spans[] = {
  {4, 0, 8, 0},
  {2, 1, 10, 1},
  {1, 2, 11, 3},
  {0, 4, 12, 8},
  {1, 9, 11, 10},
  {2, 11, 10, 11},
  {4, 12, 8, 12}
};

static const UiSpan button_round_face_spans[] = {
  {4, 1, 8, 1},
  {2, 2, 10, 3},
  {1, 4, 11, 8},
  {2, 9, 10, 10},
  {4, 11, 8, 11}
};

/* The shoulders are the pair that is not a circle: nineteen by eleven, long
   the way the moulding is long. */
static const UiSpan button_wide_shell_spans[] = {
  {2, 0, 16, 0},
  {1, 1, 17, 1},
  {0, 2, 18, 8},
  {1, 9, 17, 9},
  {2, 10, 16, 10}
};

static const UiSpan button_wide_face_spans[] = {
  {2, 1, 16, 1},
  {1, 2, 17, 8},
  {2, 9, 16, 9}
};

/* Five by seven letters, matching the UI font's proportions. */
static const UiSpan glyph_a_spans[] = {
  {1, 0, 3, 0}, {0, 1, 0, 2}, {4, 1, 4, 2}, {0, 3, 4, 3},
  {0, 4, 0, 6}, {4, 4, 4, 6}
};

static const UiSpan glyph_b_spans[] = {
  {0, 0, 3, 0}, {0, 1, 0, 2}, {4, 1, 4, 2}, {0, 3, 3, 3},
  {0, 4, 0, 5}, {4, 4, 4, 5}, {0, 6, 3, 6}
};

static const UiSpan glyph_l_spans[] = {
  {0, 0, 0, 5}, {0, 6, 4, 6}
};

static const UiSpan glyph_r_spans[] = {
  {0, 0, 3, 0}, {0, 1, 0, 2}, {4, 1, 4, 2}, {0, 3, 3, 3},
  {0, 4, 0, 6}, {3, 4, 3, 4}, {4, 5, 4, 6}
};

static const UiSpan glyph_s_spans[] = {
  {1, 0, 4, 0}, {0, 1, 0, 2}, {1, 3, 3, 3}, {4, 4, 4, 5}, {0, 6, 3, 6}
};

static const UiSpan glyph_z_spans[] = {
  {0, 0, 4, 0}, {4, 1, 4, 1}, {3, 2, 3, 2}, {2, 3, 2, 3},
  {1, 4, 1, 4}, {0, 5, 0, 5}, {0, 6, 4, 6}
};

/* The D-pad is the one control that is not a disc or a bar.  A fat plus,
   eroded by one for its face, which leaves a border everywhere including the
   six inside corners. */
static const UiSpan button_cross_shell_spans[] = {
  {4, 0, 8, 3},
  {0, 4, 12, 8},
  {4, 9, 8, 12}
};

static const UiSpan button_cross_face_spans[] = {
  {5, 1, 7, 11},
  {1, 5, 11, 7}
};

/* Arrows: seven by five lying down, five by seven standing up, both with a
   two-row base so they stay solid triangles rather than thin darts. */
static const UiSpan glyph_up_spans[] = {
  {3, 0, 3, 0}, {2, 1, 4, 1}, {1, 2, 5, 2}, {0, 3, 6, 4}
};

static const UiSpan glyph_down_spans[] = {
  {0, 0, 6, 1}, {1, 2, 5, 2}, {2, 3, 4, 3}, {3, 4, 3, 4}
};

static const UiSpan glyph_left_spans[] = {
  {3, 0, 4, 0}, {2, 1, 4, 1}, {1, 2, 4, 2}, {0, 3, 4, 3},
  {1, 4, 4, 4}, {2, 5, 4, 5}, {3, 6, 4, 6}
};

static const UiSpan glyph_right_spans[] = {
  {0, 0, 1, 0}, {0, 1, 2, 1}, {0, 2, 3, 2}, {0, 3, 4, 3},
  {0, 4, 3, 4}, {0, 5, 2, 5}, {0, 6, 1, 6}
};

/* The stick's dished top, seen from above: a dark disc in a grey one. */
static const UiSpan glyph_knob_spans[] = {
  {1, 0, 3, 0}, {0, 1, 4, 3}, {1, 4, 3, 4}
};

/* The D-pad's pivot. */
static const UiSpan glyph_pivot_spans[] = {
  {0, 0, 2, 2}
};

typedef struct {
  const UiSpan *shell;
  const UiSpan *face;
  const UiSpan *glyph;
  u8 shell_spans;
  u8 face_spans;
  u8 glyph_spans;
  u8 glyph_x;
  u8 glyph_y;
  u8 width;
  u8 height;
  u8 face_color[3];
  u8 glyph_color[3];
} ButtonStyle;

/* One near-black shell under every button, so the whole set reads as one
   family and the shell pass is a single fill colour however many are up. */
static const u8 button_shell_color[3] = {10, 10, 12};

/* The shape half of a style: everything up to the two colours.  Colours stay
   spelled out at each button because they are the part worth reading. */
#define ROUND_BUTTON(glyph, gx, gy) \
  button_round_shell_spans, button_round_face_spans, glyph, \
  7, 5, sizeof (glyph) / sizeof (UiSpan), gx, gy, 13, 13

#define WIDE_BUTTON(glyph) \
  button_wide_shell_spans, button_wide_face_spans, glyph, \
  5, 3, sizeof (glyph) / sizeof (UiSpan), 7, 2, 19, 11

#define CROSS_BUTTON(glyph, gx, gy) \
  button_cross_shell_spans, button_cross_face_spans, glyph, \
  3, 2, sizeof (glyph) / sizeof (UiSpan), gx, gy, 13, 13

#define BUTTON_WHITE {238, 240, 245}
#define BUTTON_YELLOW {232, 190, 46}
#define BUTTON_GREY {168, 170, 175}
#define BUTTON_ARROW_DARK {38, 30, 6}
#define BUTTON_SHOULDER_DARK {32, 33, 36}

static const ButtonStyle button_a = {
  ROUND_BUTTON(glyph_a_spans, 4, 3), {52, 104, 198}, BUTTON_WHITE
};

static const ButtonStyle button_b = {
  ROUND_BUTTON(glyph_b_spans, 4, 3), {56, 158, 84}, BUTTON_WHITE
};

static const ButtonStyle button_start = {
  ROUND_BUTTON(glyph_s_spans, 4, 3), {198, 52, 48}, BUTTON_WHITE
};

static const ButtonStyle button_c_up = {
  ROUND_BUTTON(glyph_up_spans, 3, 4), BUTTON_YELLOW, BUTTON_ARROW_DARK
};

static const ButtonStyle button_c_down = {
  ROUND_BUTTON(glyph_down_spans, 3, 4), BUTTON_YELLOW, BUTTON_ARROW_DARK
};

static const ButtonStyle button_c_left = {
  ROUND_BUTTON(glyph_left_spans, 4, 3), BUTTON_YELLOW, BUTTON_ARROW_DARK
};

static const ButtonStyle button_c_right = {
  ROUND_BUTTON(glyph_right_spans, 4, 3), BUTTON_YELLOW, BUTTON_ARROW_DARK
};

static const ButtonStyle button_l = {
  WIDE_BUTTON(glyph_l_spans), BUTTON_GREY, BUTTON_SHOULDER_DARK
};

static const ButtonStyle button_r = {
  WIDE_BUTTON(glyph_r_spans), BUTTON_GREY, BUTTON_SHOULDER_DARK
};

static const ButtonStyle button_z = {
  WIDE_BUTTON(glyph_z_spans), BUTTON_GREY, BUTTON_SHOULDER_DARK
};

/* The stick and the D-pad are not buttons, but every place that names one
   names buttons in the same breath, so they belong to the same family and
   the same lane.  Both are the controller's own dark grey. */
static const ButtonStyle button_stick = {
  ROUND_BUTTON(glyph_knob_spans, 4, 4), BUTTON_GREY, {74, 77, 82}
};

static const ButtonStyle button_dpad = {
  CROSS_BUTTON(glyph_pivot_spans, 5, 5), {104, 107, 112}, {58, 60, 64}
};

typedef struct {
  const ButtonStyle *style;
  u16 x;
  u16 y;
} ButtonPlacement;

/*
 * Three passes over the whole group, for the reason drawUiMeter has: every
 * fill colour costs a pipe sync, and a shell-face-glyph loop per button would
 * pay nine for a row of three.  Passing over the group instead pays one for
 * the shells and one more only where consecutive buttons actually differ, so
 * the four C buttons of a cluster cost three between them.
 */
static void drawButtonPass(const ButtonPlacement *list, u8 count, u8 pass) {
  s16 last[3];
  u8 i;

  last[0] = last[1] = last[2] = -1;
  for (i = 0; i < count; i++) {
    const ButtonStyle *style = list[i].style;
    const u8 *color = button_shell_color;
    const UiSpan *spans = style->shell;
    u8 spans_count = style->shell_spans;
    u32 x = list[i].x;
    u32 y = list[i].y;

    if (pass == 1) {
      color = style->face_color;
      spans = style->face;
      spans_count = style->face_spans;
    } else if (pass == 2) {
      color = style->glyph_color;
      spans = style->glyph;
      spans_count = style->glyph_spans;
      x += style->glyph_x;
      y += style->glyph_y;
    }
    if (color[0] != last[0] || color[1] != last[1] || color[2] != last[2]) {
      gDPPipeSync(dlp++);
      setFillColor(color[0], color[1], color[2]);
      last[0] = color[0];
      last[1] = color[1];
      last[2] = color[2];
    }
    drawUiSpans(spans, spans_count, x, y, UI_SPAN_NO_CLIP);
  }
}

/* Caller must already have the RDP in G_CYC_FILL. */
static void drawButtonIcons(const ButtonPlacement *list, u8 count) {
  drawButtonPass(list, count, 0);
  drawButtonPass(list, count, 1);
  drawButtonPass(list, count, 2);
}

static const ButtonStyle *const button_icons[BUTTON_ICON_COUNT] = {
  &button_a,
  &button_b,
  &button_start,
  &button_c_up,
  &button_c_down,
  &button_c_left,
  &button_c_right,
  &button_l,
  &button_r,
  &button_z,
  &button_stick,
  &button_dpad
};

/*
 * The one-button entry point, for callers outside this file.  Drawing a row
 * through drawButtonIcons costs fewer pipe syncs than calling this per button,
 * so prefer a placement list where a caller has several; this is for the menus
 * and panels that have one.
 */
void drawButtonIcon(ButtonIconId id, u32 x, u32 y) {
  ButtonPlacement icon;

  if ((u32) id >= BUTTON_ICON_COUNT) {
    return;
  }
  icon.style = button_icons[id];
  icon.x = x;
  icon.y = y;
  drawButtonIcons(&icon, 1);
}

u32 buttonIconWidth(ButtonIconId id) {
  return (u32) id < BUTTON_ICON_COUNT ? button_icons[id]->width : 0;
}

u32 buttonIconHeight(ButtonIconId id) {
  return (u32) id < BUTTON_ICON_COUNT ? button_icons[id]->height : 0;
}

/*
 * A legend row -- the controls, then what each one does -- is the shape every
 * screen that explains itself ends up wanting, so it lives here once.
 *
 * The row is drawn twice, from opposite ends of a screen's two phases: the
 * icons are fill sprites and the labels are textured rectangles, and the RDP
 * cannot be swapped between them mid-card.  Both walks measure the row by the
 * same arithmetic, which is the only thing keeping the words on their icons
 * when the two passes are fifty lines apart in the source.
 */

u32 legendEntryWidth(const LegendEntry *entry) {
  u32 width = buttonIconWidth(entry->icon);

  if (entry->icon2 != BUTTON_ICON_NONE) {
    width += LEGEND_PAIR_GAP + buttonIconWidth(entry->icon2);
  }
  return width + LEGEND_ICON_GAP + stringWidth(entry->label);
}

u32 legendWidth(const LegendEntry *entries, u8 count) {
  u32 width = 0;
  u8 i;

  for (i = 0; i < count; i++) {
    width += legendEntryWidth(&entries[i]);
    if (i + 1 < count) {
      width += LEGEND_ENTRY_GAP;
    }
  }
  return width;
}

/* Fills phase.  The shoulders are shorter than the round buttons, so an icon
   is centred against the row rather than hung from its top. */
void drawLegendIcons(const LegendEntry *entries, u8 count, u32 x, u32 y) {
  ButtonPlacement icons[LEGEND_MAX_ICONS];
  u8 placed = 0;
  u8 i;

  for (i = 0; i < count && placed < LEGEND_MAX_ICONS; i++) {
    u32 slot = x;
    u8 pair;

    for (pair = 0; pair < 2 && placed < LEGEND_MAX_ICONS; pair++) {
      ButtonIconId id = pair == 0 ? entries[i].icon : entries[i].icon2;

      if (id == BUTTON_ICON_NONE) {
        continue;
      }
      icons[placed].style = button_icons[id];
      icons[placed].x = slot;
      icons[placed].y = y + (LEGEND_ROW_HEIGHT - buttonIconHeight(id)) / 2;
      placed++;
      slot += buttonIconWidth(id) + LEGEND_PAIR_GAP;
    }
    x += legendEntryWidth(&entries[i]) + LEGEND_ENTRY_GAP;
  }
  /* One grouped pass over the whole row: the shells are a single fill colour
     however many buttons are in it, and only a change of face or glyph colour
     between neighbours costs another sync. */
  drawButtonIcons(icons, placed);
}

/* The width of one entry's icons, without its label: what a column has to
   indent every label by if their left edges are to line up. */
static u32 legendIconCellWidth(const LegendEntry *entry) {
  u32 width = buttonIconWidth(entry->icon);

  if (entry->icon2 != BUTTON_ICON_NONE) {
    width += LEGEND_PAIR_GAP + buttonIconWidth(entry->icon2);
  }
  return width;
}

u32 legendColumnIconWidth(const LegendEntry *entries, u8 count) {
  u32 widest = 0;
  u8 i;

  for (i = 0; i < count; i++) {
    u32 width = legendIconCellWidth(&entries[i]);

    if (width > widest) {
      widest = width;
    }
  }
  return widest;
}

u32 legendColumnWidth(const LegendEntry *entries, u8 count) {
  u32 indent = legendColumnIconWidth(entries, count) + LEGEND_ICON_GAP;
  u32 widest = 0;
  u8 i;

  for (i = 0; i < count; i++) {
    u32 width = stringWidth(entries[i].label);

    if (width > widest) {
      widest = width;
    }
  }
  return indent + widest;
}

/*
 * A column's icons.  Each entry's icons are right-aligned within the shared
 * indent, so a one-button row and a two-button row both finish at the same
 * place and the labels start there.
 */
void drawLegendColumnIcons(const LegendEntry *entries, u8 count, u32 x, u32 y,
    u32 pitch) {
  ButtonPlacement icons[LEGEND_MAX_ICONS];
  u32 indent = legendColumnIconWidth(entries, count);
  u8 placed = 0;
  u8 i;

  for (i = 0; i < count && placed < LEGEND_MAX_ICONS; i++) {
    u32 slot = x + indent - legendIconCellWidth(&entries[i]);
    u32 row = y + i * pitch;
    u8 pair;

    for (pair = 0; pair < 2 && placed < LEGEND_MAX_ICONS; pair++) {
      ButtonIconId id = pair == 0 ? entries[i].icon : entries[i].icon2;

      if (id == BUTTON_ICON_NONE) {
        continue;
      }
      icons[placed].style = button_icons[id];
      icons[placed].x = slot;
      icons[placed].y = row + (LEGEND_ROW_HEIGHT - buttonIconHeight(id)) / 2;
      placed++;
      slot += buttonIconWidth(id) + LEGEND_PAIR_GAP;
    }
  }
  drawButtonIcons(icons, placed);
}

void drawLegendColumnLabels(const LegendEntry *entries, u8 count, u32 x, u32 y,
    u32 pitch) {
  u32 label_x = x + legendColumnIconWidth(entries, count) + LEGEND_ICON_GAP;
  u8 i;

  for (i = 0; i < count; i++) {
    drawString(entries[i].label, label_x,
      y + i * pitch + LEGEND_LABEL_DROP);
  }
}

/*
 * Option switches.
 *
 * The old pair drew a one-pixel border and a three-step diagonal, and changed
 * fill colour between them without draining the pipe -- which on hardware is
 * the same attribute hazard that made a full heart come out half dark, on
 * features far too thin to survive losing any of.  The radio was worse than
 * that: it had no border at all and its body was (24, 27, 23) on a (16, 17,
 * 15) panel, eight levels of luminance apart, which a composite cable simply
 * does not carry.  It looked fine in an emulator because an emulator hands
 * you the exact pixel values.
 *
 * So: a bright border rather than a dark one -- the panel is already
 * near-black, and a mark can only be found against it by being lighter -- a
 * dark well inside it, and a mark two pixels thick everywhere.
 */
static const UiSpan check_box_shell_spans[] = {
  {0, 0, 8, 8}
};

static const UiSpan check_box_face_spans[] = {
  {1, 1, 7, 7}
};

/*   .....XX
 *   ....XX.
 *   XX.XX..
 *   .XXXX..
 *   ..XX...   */
static const UiSpan check_tick_spans[] = {
  {5, 0, 6, 0},
  {4, 1, 5, 1},
  {0, 2, 1, 2}, {3, 2, 4, 2},
  {1, 3, 4, 3},
  {2, 4, 3, 4}
};

/* One of a group gets a diamond, so a choice never looks like a toggle. */
static const UiSpan check_radio_shell_spans[] = {
  {4, 0, 4, 0},
  {3, 1, 5, 1},
  {2, 2, 6, 2},
  {1, 3, 7, 3},
  {0, 4, 8, 4},
  {1, 5, 7, 5},
  {2, 6, 6, 6},
  {3, 7, 5, 7},
  {4, 8, 4, 8}
};

static const UiSpan check_radio_face_spans[] = {
  {4, 1, 4, 1},
  {3, 2, 5, 2},
  {2, 3, 6, 3},
  {1, 4, 7, 4},
  {2, 5, 6, 5},
  {3, 6, 5, 6},
  {4, 7, 4, 7}
};

static const UiSpan check_dot_spans[] = {
  {3, 0, 5, 0},
  {2, 1, 6, 1},
  {3, 2, 5, 2}
};

#define CHECK_MARK_SIZE 9
#define CHECK_TICK_X 1
#define CHECK_TICK_Y 2
#define CHECK_DOT_X 0
#define CHECK_DOT_Y 3

u32 checkMarkSize(void) {
  return CHECK_MARK_SIZE;
}

/*
 * Grouped by colour for the same reason every other sprite here is: eleven
 * switches drawn one at a time would be thirty-odd unsynced fill-colour
 * changes, which is exactly what they were before.
 */
void drawCheckMarks(const CheckMarkPlacement *list, u8 count) {
  s16 last[3];
  u8 pass;
  u8 i;

  for (pass = 0; pass < 3; pass++) {
    last[0] = last[1] = last[2] = -1;
    for (i = 0; i < count; i++) {
      u8 radio = list[i].kind == CHECK_MARK_RADIO;
      u8 dim = list[i].dim;
      const UiSpan *spans;
      u8 spans_count;
      u32 x = list[i].x;
      u32 y = list[i].y;
      u8 color[3];

      if (pass == 0) {
        spans = radio ? check_radio_shell_spans : check_box_shell_spans;
        spans_count = radio ? 9 : 1;
        color[0] = dim ? 70 : 150;
        color[1] = dim ? 73 : 155;
        color[2] = dim ? 66 : 140;
      } else if (pass == 1) {
        spans = radio ? check_radio_face_spans : check_box_face_spans;
        spans_count = radio ? 7 : 1;
        color[0] = 20;
        color[1] = 23;
        color[2] = 19;
      } else {
        if (!list[i].on) {
          continue;
        }
        spans = radio ? check_dot_spans : check_tick_spans;
        spans_count = radio ? 3 : 6;
        x += radio ? CHECK_DOT_X : CHECK_TICK_X;
        y += radio ? CHECK_DOT_Y : CHECK_TICK_Y;
        color[0] = dim ? 96 : 232;
        color[1] = dim ? 99 : 196;
        color[2] = dim ? 88 : 79;
      }
      if (color[0] != last[0] || color[1] != last[1] || color[2] != last[2]) {
        gDPPipeSync(dlp++);
        setFillColor(color[0], color[1], color[2]);
        last[0] = color[0];
        last[1] = color[1];
        last[2] = color[2];
      }
      drawUiSpans(spans, spans_count, x, y, UI_SPAN_NO_CLIP);
    }
  }
}

/* Text phase, walking the same entries by the same arithmetic. */
void drawLegendLabels(const LegendEntry *entries, u8 count, u32 x, u32 y) {
  u8 i;

  for (i = 0; i < count; i++) {
    u32 label_x = x + buttonIconWidth(entries[i].icon) + LEGEND_ICON_GAP;

    if (entries[i].icon2 != BUTTON_ICON_NONE) {
      label_x += LEGEND_PAIR_GAP + buttonIconWidth(entries[i].icon2);
    }
    drawString(entries[i].label, label_x, y + LEGEND_LABEL_DROP);
    x += legendEntryWidth(&entries[i]) + LEGEND_ENTRY_GAP;
  }
}

/*
 * A row of discrete symbols standing for a quantity.
 *
 *   .XX..XX.
 *   XXXXXXXX
 *   XXXXXXXX
 *   XXXXXXXX
 *   .XXXXXX.
 *   ..XXXX..
 *   ...XX...
 */
static const UiSpan heart_outline_spans[] = {
  {1, 0, 2, 0}, {5, 0, 6, 0},
  {0, 1, 7, 3},
  {1, 4, 6, 4},
  {2, 5, 5, 5},
  {3, 6, 4, 6}
};

static const UiSpan heart_inner_spans[] = {
  {1, 1, 2, 1}, {5, 1, 6, 1},
  {1, 2, 6, 3},
  {2, 4, 5, 4},
  {3, 5, 4, 5}
};

const UiMeterStyle ui_heart_style = {
  heart_outline_spans, heart_inner_spans, 6, 5, 8, 7, 9,
  {26, 8, 10}, {94, 33, 35}, {228, 60, 56}
};

/* Values are counted in halves, so a style's max of 20 is ten symbols. */
#define UI_METER_UNITS(max) ((max) / 2)

u32 uiMeterWidth(const UiMeterStyle *style, u8 max_value) {
  return (UI_METER_UNITS(max_value) - 1) * style->pitch + style->width;
}

/*
 * Drawn in three passes over the whole row -- every outline, then every empty
 * interior, then every fill -- so the row costs three fill colours instead of
 * two per symbol.
 *
 * That grouping is not just tidiness.  gDPSetFillColor is an RDP attribute
 * change, and like the mode changes in docs/hardware.md it needs the pipe
 * drained first or it lands on spans of a primitive still in flight.  Code
 * that alternated dark and bright forty times a row with no sync at all drew
 * a full heart half dark on hardware while emulators drew it solid.
 */
void drawUiMeter(const UiMeterStyle *style, u32 x, u32 y, u8 value,
    u8 max_value) {
  u8 units = UI_METER_UNITS(max_value);
  u8 i;

  if (style->outline_spans > 0) {
    gDPPipeSync(dlp++);
    setFillColor(style->outline_color[0], style->outline_color[1],
      style->outline_color[2]);
    for (i = 0; i < units; i++) {
      drawUiSpans(style->outline, style->outline_spans,
        x + i * style->pitch, y, style->width);
    }
  }

  gDPPipeSync(dlp++);
  setFillColor(style->empty_color[0], style->empty_color[1],
    style->empty_color[2]);
  for (i = 0; i < units; i++) {
    /* A full unit's fill covers the interior outright, so only partial and
       empty units need painting underneath it. */
    if (value < (i + 1) * 2) {
      drawUiSpans(style->inner, style->inner_spans, x + i * style->pitch, y,
        style->width);
    }
  }

  gDPPipeSync(dlp++);
  setFillColor(style->fill_color[0], style->fill_color[1],
    style->fill_color[2]);
  for (i = 0; i < units; i++) {
    u8 unit = value > i * 2 ? (value - i * 2 > 2 ? 2 : value - i * 2) : 0;

    if (unit > 0) {
      drawUiSpans(style->inner, style->inner_spans, x + i * style->pitch, y,
        unit == 2 ? style->width : (style->width + 1) / 2);
    }
  }
}

/*
 * One continuous quantity rather than a row of symbols, for the readings where
 * "how close is the next one" matters more than how many there are.  Same
 * three passes, same reason.
 */
void drawUiBar(u32 x, u32 y, u32 width, u32 height, u32 filled,
    const u8 outline_color[3], const u8 empty_color[3],
    const u8 fill_color[3]) {
  if (filled > width) {
    filled = width;
  }
  gDPPipeSync(dlp++);
  setFillColor(outline_color[0], outline_color[1], outline_color[2]);
  gDPFillRectangle(dlp++, x - 1, y - 1, x + width, y + height);

  gDPPipeSync(dlp++);
  setFillColor(empty_color[0], empty_color[1], empty_color[2]);
  gDPFillRectangle(dlp++, x, y, x + width - 1, y + height - 1);

  if (filled > 0) {
    gDPPipeSync(dlp++);
    setFillColor(fill_color[0], fill_color[1], fill_color[2]);
    gDPFillRectangle(dlp++, x, y, x + filled - 1, y + height - 1);
  }
}

/*
 * The flat card every menu is built on: a filled rectangle with a one-pixel
 * rim.  Two fill colours, so two pipe syncs.
 */
void drawUiPanel(u32 left, u32 top, u32 right, u32 bottom,
    const u8 fill_color[3], const u8 rim_color[3]) {
  gDPPipeSync(dlp++);
  setFillColor(rim_color[0], rim_color[1], rim_color[2]);
  gDPFillRectangle(dlp++, left, top, right, bottom);

  gDPPipeSync(dlp++);
  setFillColor(fill_color[0], fill_color[1], fill_color[2]);
  gDPFillRectangle(dlp++, left + 1, top + 1, right - 1, bottom - 1);
}

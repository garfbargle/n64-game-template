#ifndef ENGINE_UI_H
#define ENGINE_UI_H

#include <nusys.h>

/*
 * Flat-colour sprites, drawn as RDP fill rectangles rather than textures.
 *
 * Everything in this file is a fills-phase call: the RDP must already be in
 * G_CYC_FILL (gfxBeginFills), and none of it may be interleaved with text.
 * See the phase note in gfx.h -- putting a button between two runs of text is
 * the hazard that locks real hardware while emulators shrug.
 *
 * A sprite is a run-length list of horizontal spans, which is how a 13x13
 * button costs a dozen rectangles instead of a texture load.
 */
typedef struct {
  u8 x0, y0, x1, y1;
} UiSpan;

#define UI_SPAN_NO_CLIP 255

/* `clip_width` keeps only the leftmost columns of the sprite, which is how a
   half unit of a meter is drawn: the same spans, cut down the middle. */
void drawUiSpans(const UiSpan *spans, u8 count, u32 x, u32 y, u8 clip_width);
void setFillColor(u8 r, u8 g, u8 b);

/*
 * Controller buttons as the controller wears them, for naming a control
 * without spelling it out: A blue, B green, the four C buttons yellow with an
 * arrow, START red, the shoulders long and grey.  Naming a button by drawing
 * it is shorter than naming it in words, and it survives a player who has
 * never read the manual.
 */
typedef enum {
  BUTTON_ICON_A,
  BUTTON_ICON_B,
  BUTTON_ICON_START,
  BUTTON_ICON_C_UP,
  BUTTON_ICON_C_DOWN,
  BUTTON_ICON_C_LEFT,
  BUTTON_ICON_C_RIGHT,
  BUTTON_ICON_L,
  BUTTON_ICON_R,
  BUTTON_ICON_Z,
  /* Not buttons, but every screen that names one names buttons alongside it. */
  BUTTON_ICON_STICK,
  BUTTON_ICON_DPAD,
  BUTTON_ICON_COUNT
} ButtonIconId;

/* For a legend entry whose control is a single button: drawing it is a no-op
   and it measures zero, so the second slot simply disappears. */
#define BUTTON_ICON_NONE BUTTON_ICON_COUNT

void drawButtonIcon(ButtonIconId id, u32 x, u32 y);
/* The round buttons are 13x13, the shoulders 19x11 and the D-pad 13x13; ask
   rather than assume, so a label can be laid out beside any of them. */
u32 buttonIconWidth(ButtonIconId id);
u32 buttonIconHeight(ButtonIconId id);

/*
 * A legend row: each control, then what pressing it does.  `icon2` is for the
 * controls that are a pair -- C left and C right moving one cursor -- and is
 * BUTTON_ICON_NONE otherwise.
 *
 * A row is drawn twice, once per phase: drawLegendIcons from the screen's
 * fills, drawLegendLabels from its text.  Give both the same entries, x and
 * y, and the words land on their icons; there is deliberately no single call
 * that does both, because there is no phase in which it would be legal.
 */
typedef struct {
  ButtonIconId icon;
  ButtonIconId icon2;
  const char *label;
} LegendEntry;

#define LEGEND_ROW_HEIGHT 13
#define LEGEND_ICON_GAP 3
#define LEGEND_PAIR_GAP 2
#define LEGEND_ENTRY_GAP 11
/* Centres an 8px glyph box against a 13px button. */
#define LEGEND_LABEL_DROP 2
/* Icons drawn in one grouped pass.  Anything longer is truncated rather than
   split; raise it if a screen genuinely needs a longer legend. */
#define LEGEND_MAX_ICONS 24

#define LEGEND_COUNT(table) ((u8) (sizeof (table) / sizeof (LegendEntry)))

u32 legendEntryWidth(const LegendEntry *entry);
u32 legendWidth(const LegendEntry *entries, u8 count);
void drawLegendIcons(const LegendEntry *entries, u8 count, u32 x, u32 y);
void drawLegendLabels(const LegendEntry *entries, u8 count, u32 x, u32 y);

/*
 * The same entries stacked instead of strung out: one control per line, every
 * label starting at the same x.  A row wants the icons packed tight against
 * their words; a column wants the words in a straight edge, or a list of a
 * dozen controls reads as a dozen unrelated fragments.
 *
 * legendColumnIconWidth is that shared indent -- the widest icon cell in the
 * table -- so a caller can centre the whole block.
 */
u32 legendColumnIconWidth(const LegendEntry *entries, u8 count);
u32 legendColumnWidth(const LegendEntry *entries, u8 count);
void drawLegendColumnIcons(const LegendEntry *entries, u8 count, u32 x, u32 y,
  u32 pitch);
void drawLegendColumnLabels(const LegendEntry *entries, u8 count, u32 x, u32 y,
  u32 pitch);

/*
 * Option switches, on the same fill-sprite machinery as the buttons and for
 * the same reasons: a bright border a near-black panel cannot swallow, a mark
 * two pixels thick, and one grouped pass so eleven switches cost a handful of
 * pipe syncs rather than thirty.
 *
 * `dim` is a row the current state cannot offer.  Fills phase only.
 */
#define CHECK_MARK_BOX 0    /* an independent toggle */
#define CHECK_MARK_RADIO 1  /* one of a mutually exclusive group */

typedef struct {
  u8 kind;
  u8 on;
  u8 dim;
  u16 x;
  u16 y;
} CheckMarkPlacement;

void drawCheckMarks(const CheckMarkPlacement *list, u8 count);
u32 checkMarkSize(void);

/*
 * A row of discrete symbols standing for a quantity -- hearts, pips, charges.
 * `value` is counted in halves, so a style whose max is 20 draws ten symbols
 * and a value of 3 fills one and a half of them.
 *
 * Drawn in three passes over the whole row (every outline, then every empty
 * interior, then every fill) so the row costs three fill colours instead of
 * two per symbol.  That grouping is not tidiness: gDPSetFillColor is an RDP
 * attribute change and needs the pipe drained first, or it lands on spans of a
 * primitive still in flight.
 */
typedef struct {
  const UiSpan *outline;
  const UiSpan *inner;
  u8 outline_spans;
  u8 inner_spans;
  u8 width;
  u8 height;
  u8 pitch;
  /* Every border here is a luminance step, not a hue step.  Composite video
     carries luma at full bandwidth and chroma at a fraction of it, so a
     near-black outline survives the trip to a CRT where two saturated colours
     of similar brightness would smear into each other. */
  u8 outline_color[3];
  u8 empty_color[3];
  u8 fill_color[3];
} UiMeterStyle;

u32 uiMeterWidth(const UiMeterStyle *style, u8 max_value);
void drawUiMeter(const UiMeterStyle *style, u32 x, u32 y, u8 value,
  u8 max_value);

/* A ready-made heart row, as an example of a UiMeterStyle and because most
   games that want one want this one.  8x7 symbols on a 9px pitch. */
extern const UiMeterStyle ui_heart_style;

/*
 * A filled bar: one continuous quantity rather than a row of symbols, for the
 * readings where "how close is the next one" matters more than how many there
 * are.  Same three passes, same reason.
 */
void drawUiBar(u32 x, u32 y, u32 width, u32 height, u32 filled,
  const u8 outline_color[3], const u8 empty_color[3],
  const u8 fill_color[3]);

/* A flat panel with a one-pixel rim, which is what every card in a menu is
   built on.  Fills phase. */
void drawUiPanel(u32 left, u32 top, u32 right, u32 bottom,
  const u8 fill_color[3], const u8 rim_color[3]);

#endif /* ENGINE_UI_H */

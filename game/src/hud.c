#include "game.h"

/*
 * The 2D layers: title screen, in-game HUD, pause card.
 *
 * Every screen here is written twice -- once for the fills phase and once for
 * the text phase -- off the same layout constants.  That is not duplication
 * for its own sake: the RDP cannot be swapped between fill mode and texture
 * mode in the middle of a card without risking a hardware lock (see
 * docs/hardware.md), so all the rectangles go down, then all the glyphs.  The
 * two passes agreeing is what keeps a label on its button, and the way to keep
 * them agreeing is to measure both from the same numbers.
 */

static const u8 panel_fill[3] = {18, 20, 32};
static const u8 panel_rim[3] = {96, 104, 132};
static const u8 highlight_fill[3] = {44, 52, 84};

/* ------------------------------------------------------------------ title */

#define TITLE_SCALE 3
#define TITLE_Y 28
#define SLOT_LIST_Y 92
#define SLOT_ROW_HEIGHT 18
#define SLOT_PANEL_LEFT 74
#define SLOT_PANEL_RIGHT 246

/*
 * A legend is a table, not a series of draw calls: written once here and
 * walked by both passes, so a control cannot be drawn without its label or
 * described without its button.
 */
static const LegendEntry title_legend[] = {
  { BUTTON_ICON_STICK, BUTTON_ICON_DPAD, "CHOOSE" },
  { BUTTON_ICON_A, BUTTON_ICON_NONE, "START" },
  { BUTTON_ICON_Z, BUTTON_ICON_NONE, "DELETE" }
};

static u32 titleLegendX(void) {
  return (SCREEN_WD - legendWidth(title_legend, LEGEND_COUNT(title_legend)))
    / 2;
}

#define TITLE_LEGEND_Y 196

void drawTitleFills(void) {
  u8 i;

  for (i = 0; i < STORAGE_MAX_SLOTS; i++) {
    u32 top = SLOT_LIST_Y + i * SLOT_ROW_HEIGHT;

    drawUiPanel(SLOT_PANEL_LEFT, top, SLOT_PANEL_RIGHT,
      top + SLOT_ROW_HEIGHT - 4,
      i == gameSelectedSlot() ? highlight_fill : panel_fill, panel_rim);
  }
  drawLegendIcons(title_legend, LEGEND_COUNT(title_legend), titleLegendX(),
    TITLE_LEGEND_Y);
}

void drawTitleText(void) {
  u8 i;

  setTextColor(236, 240, 250);
  drawCenteredLargeString(GAME_TITLE, TITLE_Y, TITLE_SCALE);

  for (i = 0; i < STORAGE_MAX_SLOTS; i++) {
    u32 top = SLOT_LIST_Y + i * SLOT_ROW_HEIGHT + 4;

    if (i == gameSelectedSlot()) {
      setTextColor(255, 255, 255);
    } else {
      setTextColor(150, 158, 178);
    }
    drawString(slot_names[i], SLOT_PANEL_LEFT + 8, top);
    /* Say what the slot is, not just what it is called: "EMPTY" and the offer
       to undo a delete are the two things a player needs before pressing A. */
    if (files_present[i]) {
      drawString("SAVED", SLOT_PANEL_RIGHT - 48, top);
    } else if (deleted_files_present[i]) {
      drawString("DELETED", SLOT_PANEL_RIGHT - 58, top);
    } else {
      drawString("EMPTY", SLOT_PANEL_RIGHT - 48, top);
    }
  }

  setTextColor(200, 206, 224);
  drawLegendLabels(title_legend, LEGEND_COUNT(title_legend), titleLegendX(),
    TITLE_LEGEND_Y);

  /* Storage is a flashcart feature; on an emulator or a bare cartridge there
     is none, and a title screen that says so is worth more than a save button
     that silently does nothing. */
  if (!saving_available) {
    setTextColor(220, 170, 90);
    drawCenteredString(storageStatusText(), 168);
  }
  if (gameMessage() != NULL) {
    setTextColor(255, 226, 140);
    drawCenteredString(gameMessage(), 152);
  }
}

/* -------------------------------------------------------------------- hud */

static const LegendEntry play_legend[] = {
  { BUTTON_ICON_STICK, BUTTON_ICON_NONE, "LOOK" },
  { BUTTON_ICON_L, BUTTON_ICON_R, "ZOOM" },
  { BUTTON_ICON_B, BUTTON_ICON_NONE, "SAVE" },
  { BUTTON_ICON_START, BUTTON_ICON_NONE, "PAUSE" }
};

#define HUD_LEGEND_Y 218
#define HEART_X 12
#define HEART_Y 12
#define HEART_MAX 10

static u32 playLegendX(void) {
  return (SCREEN_WD - legendWidth(play_legend, LEGEND_COUNT(play_legend))) / 2;
}

void drawHudFills(void) {
  /* A meter with a fixed value, because the demo has no health to lose -- it
     is here so the machinery is in front of you when you need it. */
  drawUiMeter(&ui_heart_style, HEART_X, HEART_Y, 7, HEART_MAX);
  drawLegendIcons(play_legend, LEGEND_COUNT(play_legend), playLegendX(),
    HUD_LEGEND_Y);
}

void drawHudText(void) {
  u32 x;

  setTextColor(200, 206, 224);
  drawLegendLabels(play_legend, LEGEND_COUNT(play_legend), playLegendX(),
    HUD_LEGEND_Y);

  setTextColor(236, 240, 250);
  x = drawUnsigned(save_data.spins, SCREEN_WD - 52, HEART_Y + 1);
  (void) x;
  drawString("SPINS", SCREEN_WD - 52, HEART_Y + 12);

  if (gameMessage() != NULL) {
    setTextColor(255, 226, 140);
    drawCenteredString(gameMessage(), 60);
  }
}

/* ------------------------------------------------------------------ pause */

#define PAUSE_LEFT 60
#define PAUSE_TOP 64
#define PAUSE_RIGHT 260
#define PAUSE_BOTTOM 168
#define PAUSE_ROW_Y 104
#define PAUSE_ROW_PITCH 22
#define PAUSE_PIP_X 170
#define PAUSE_PIP_PITCH 12

static const char *const pause_rows[] = {"MUSIC", "SOUND"};
#define PAUSE_ROW_COUNT 2

static const LegendEntry pause_legend[] = {
  { BUTTON_ICON_C_LEFT, BUTTON_ICON_C_RIGHT, "ADJUST" },
  { BUTTON_ICON_B, BUTTON_ICON_NONE, "BACK" }
};

/*
 * The volume rows draw as check marks rather than a number: a row of pips is
 * countable at a glance from a sofa, and "3" is not.
 */
void drawPauseFills(void) {
  CheckMarkPlacement pips[PAUSE_ROW_COUNT * AUDIO_VOLUME_STEPS];
  u8 count = 0;
  u8 row;
  u8 step;

  drawUiPanel(PAUSE_LEFT, PAUSE_TOP, PAUSE_RIGHT, PAUSE_BOTTOM, panel_fill,
    panel_rim);

  for (row = 0; row < PAUSE_ROW_COUNT; row++) {
    u8 volume = row == 0 ? musicVolume() : soundVolume();

    for (step = 1; step < AUDIO_VOLUME_STEPS; step++) {
      pips[count].kind = CHECK_MARK_BOX;
      pips[count].on = volume >= step;
      /* A build with no audio still draws the rows, dimmed, rather than
         hiding a control that will exist in the audio build. */
      pips[count].dim = row != gamePauseRow();
      pips[count].x = PAUSE_PIP_X + (step - 1) * PAUSE_PIP_PITCH;
      pips[count].y = PAUSE_ROW_Y + row * PAUSE_ROW_PITCH;
      count++;
    }
  }
  drawCheckMarks(pips, count);
  drawLegendIcons(pause_legend, LEGEND_COUNT(pause_legend), PAUSE_LEFT + 14,
    PAUSE_BOTTOM - 24);
}

void drawPauseText(void) {
  u8 row;

  setTextColor(236, 240, 250);
  drawCenteredString("PAUSED", PAUSE_TOP + 12);

  for (row = 0; row < PAUSE_ROW_COUNT; row++) {
    if (row == gamePauseRow()) {
      setTextColor(255, 255, 255);
    } else {
      setTextColor(150, 158, 178);
    }
    drawString(pause_rows[row], PAUSE_LEFT + 18,
      PAUSE_ROW_Y + row * PAUSE_ROW_PITCH);
  }

  setTextColor(200, 206, 224);
  drawLegendLabels(pause_legend, LEGEND_COUNT(pause_legend), PAUSE_LEFT + 14,
    PAUSE_BOTTOM - 24);
}

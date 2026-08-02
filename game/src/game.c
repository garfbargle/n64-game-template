#include "game.h"

/*
 * The game's three entry points, and the screen machine that connects them.
 *
 * Read this file first: it is the whole contract with the engine.  gameInit
 * runs once, gameUpdate runs on every graphics callback, gameDraw runs on the
 * ones that actually build a frame.  Nothing else is required of a game.
 */

GameScreen current_screen = SCREEN_TITLE;
SaveData save_data;

/* Which save slot the title screen is standing on. */
static u8 selected_slot;
/* Frames left to keep a status line up, counted down in gameUpdate so it runs
   at the same speed however hard the scene is to draw. */
static float message_timer;
static const char *message_text;

static void postMessage(const char *text) {
  message_text = text;
  message_timer = 120.f;
}

const char *gameMessage(void) {
  return message_timer > 0.f ? message_text : NULL;
}

u8 gameSelectedSlot(void) {
  return selected_slot;
}

/*
 * Saving.
 *
 * The payload here is three fields, which is small enough that the blocking
 * storageSave is honest -- it stops the console for a few hundred
 * milliseconds and the player sees a line saying so.  Anything larger wants
 * storageBeginSave and a progress bar, or a successful save is
 * indistinguishable from a crash.
 */
static void saveToSlot(u8 slot) {
  save_data.music_volume = musicVolume();
  save_data.sound_volume = soundVolume();
  if (storageSave(slot, &save_data, sizeof (save_data))) {
    postMessage("SAVED");
  } else {
    postMessage(saving_available ? "SAVE FAILED" : "NO SAVE DEVICE");
  }
}

static void loadFromSlot(u8 slot) {
  if (storageLoad(slot, &save_data, sizeof (save_data)) ==
      sizeof (save_data)) {
    setMusicVolume(save_data.music_volume);
    setSoundVolume(save_data.sound_volume);
    postMessage("LOADED");
  }
}

/*
 * The freeze post-mortem's game half.  Called from the watchdog thread after
 * the console is already dead, so it reads globals and does nothing else --
 * see diagSetReportHook.
 */
static u32 reportFreeze(char *out, u32 limit) {
  char *start = out;

  (void) limit;
  out = diagReportHex(out, "SCREEN", current_screen);
  out = diagReportHex(out, "SPINS", save_data.spins);
  return (u32) (out - start);
}

void gameInit(void) {
  initScene();
  diagSetReportHook(reportFreeze);
  playMusic(MUSIC_TITLE);
}

/*
 * Title screen: pick a slot, then start.
 *
 * inputMenuVertical answers to the stick and the D-pad together and repeats
 * while held, which is what a player expects and what nothing gives you for
 * free.
 */
static void updateTitle(void) {
  s8 step = inputMenuVertical(0);

  if (step < 0 && selected_slot > 0) {
    selected_slot--;
    playSound(SOUND_CLICK);
  } else if (step > 0 && selected_slot + 1 < STORAGE_MAX_SLOTS) {
    selected_slot++;
    playSound(SOUND_CLICK);
  }

  if (inputPressed(0, A_BUTTON | START_BUTTON)) {
    loadFromSlot(selected_slot);
    current_screen = SCREEN_PLAY;
    playMusic(MUSIC_GAME);
    playSound(SOUND_CHIME);
  } else if (inputPressed(0, Z_TRIG) && files_present[selected_slot]) {
    postMessage(storageDeleteSlot(selected_slot) ? "DELETED" : "DELETE FAILED");
  } else if (inputPressed(0, Z_TRIG) && deleted_files_present[selected_slot]) {
    postMessage(storageRestoreSlot(selected_slot) ? "RESTORED" : "FAILED");
  }
}

static void updatePlay(float delta) {
  updateScene(delta);

  if (inputPressed(0, START_BUTTON)) {
    current_screen = SCREEN_PAUSE;
    /* A paused game still draws -- something has to put the menu on screen --
       but gameUpdate is called with a delta of zero, so everything written
       against delta stands still without needing to know it is paused. */
    engineSetPaused(TRUE);
    playSound(SOUND_THUMP);
  }
  if (inputPressed(0, B_BUTTON)) {
    saveToSlot(selected_slot);
  }
  /* Z + D-pad up toggles the diagnostics overlay, which is the chord this
     engine's own instrumentation uses; keep it, or a freeze on hardware has
     nothing to say for itself. */
  if (inputHeld(0, Z_TRIG) && inputPressed(0, U_JPAD)) {
    diagnostics_visible = !diagnostics_visible;
  }
}

/* Which option row the pause card is standing on; the card draws it. */
static u8 pause_row;

u8 gamePauseRow(void) {
  return pause_row;
}

static void updatePause(void) {
  s8 step = inputMenuHorizontal(0);
  s8 row = inputMenuVertical(0);

  if (row < 0 && pause_row > 0) {
    pause_row--;
  } else if (row > 0 && pause_row < 1) {
    pause_row++;
  }
  if (step != 0) {
    u8 volume = pause_row == 0 ? musicVolume() : soundVolume();

    if (step < 0 && volume > 0) {
      volume--;
    } else if (step > 0 && volume + 1 < AUDIO_VOLUME_STEPS) {
      volume++;
    }
    if (pause_row == 0) {
      setMusicVolume(volume);
    } else {
      setSoundVolume(volume);
      playSound(SOUND_CHIME);
    }
  }

  if (inputPressed(0, START_BUTTON | B_BUTTON)) {
    current_screen = SCREEN_PLAY;
    engineSetPaused(FALSE);
  }
}

void gameUpdate(float delta) {
  if (message_timer > 0.f) {
    message_timer -= delta;
  }

  switch (current_screen) {
    case SCREEN_TITLE:
      updateTitle();
      break;
    case SCREEN_PLAY:
      updatePlay(delta);
      break;
    case SCREEN_PAUSE:
      updatePause();
      break;
  }
}

/*
 * One frame.
 *
 * The order is the same on every screen and it is not arbitrary: 3D, then
 * every flat-colour rectangle, then every glyph.  The RDP has to be
 * reconfigured between the last two and doing it twice -- text, a button, more
 * text -- is the hazard that locks real hardware while emulators shrug.  See
 * docs/hardware.md.
 */
void gameDraw(void) {
  gfxBeginFrame(24, 28, 44);

  if (current_screen != SCREEN_TITLE) {
    drawScene();
  }

  gfxSelectFullViewport();
  gfxBeginFills();
  switch (current_screen) {
    case SCREEN_TITLE:
      drawTitleFills();
      break;
    case SCREEN_PLAY:
      drawHudFills();
      break;
    case SCREEN_PAUSE:
      drawHudFills();
      drawPauseFills();
      break;
  }

  /*
   * Two text modes, and the choice is visible on screen.  The plain one writes
   * each glyph cell whole, so a word arrives in its own opaque box -- invisible
   * on a flat card, and the only thing that looks pasted on when there is a
   * scene behind it.  The blended one cuts the glyph out of its cell.  Blended
   * costs the blender; plain does not.
   */
  if (current_screen == SCREEN_TITLE) {
    gfxBeginTextures();
  } else {
    beginTextBlended();
  }
  switch (current_screen) {
    case SCREEN_TITLE:
      drawTitleText();
      break;
    case SCREEN_PLAY:
      drawHudText();
      break;
    case SCREEN_PAUSE:
      drawHudText();
      drawPauseText();
      break;
  }
  drawDiagnosticsOverlay();

  gfxEndFrame();
}

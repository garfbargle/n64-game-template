#include "engine.h"

/*
 * Boot, and the frame loop.
 *
 * This is the only place NuSystem is driven from.  A game defines gameInit,
 * gameUpdate and gameDraw (see engine.h) and never writes mainproc.
 */

float engine_time;

static u8 engine_paused;

void engineSetPaused(u8 paused) {
  engine_paused = paused;
}

u8 enginePaused(void) {
  return engine_paused;
}

/*
 * The graphics callback.
 *
 * NuSystem calls this once per video retrace, whether or not the previous
 * frame has finished drawing.  `pendingGfx` is how many tasks are still in
 * flight, and it is the whole reason update and draw are separate:
 *
 *   - Game logic runs on every callback.  It is CPU work, it does not touch
 *     the display list, and skipping it would make the simulation run slower
 *     whenever the scene got harder to draw.
 *
 *   - A frame is built only when nothing is in flight.  One cinematic task can
 *     outlive a retrace; queuing a second in that case lets NuSystem rotate
 *     framebuffers while the RDP is still writing the older one, which
 *     presents as tearing followed by corrupt UI.  Submitting only after the
 *     previous task has completely drained lets the display pace itself to the
 *     real RSP/RDP cost.
 *
 * So on a heavy scene this runs at 60 Hz and draws at 20, and `delta` is what
 * tells the game the difference.
 */
static void callbackGfx(int pendingGfx) {
  static OSTime last_update_time;
  OSTime now;
  float delta;

  diagFrameStart(pendingGfx);

  now = osGetTime();
  if (last_update_time == 0) {
    delta = 1.f;
  } else {
    /* In 60 Hz frames, so a game's constants are written in units of "per
       frame at retrace rate" and mean the same thing on PAL. */
    delta = (float) OS_CYCLES_TO_USEC(now - last_update_time) / 16667.f;
    /* A load or a long stall must not teleport everything that moves.  Past
       about four frames the simulation is better off losing time than
       stepping through a wall. */
    if (delta > 4.f) {
      delta = 4.f;
    }
  }
  last_update_time = now;

  inputUpdate();

  diagPaintPhase(DIAG_PHASE_UPDATE);
  if (engine_paused) {
    gameUpdate(0.f);
  } else {
    engine_time += delta;
    gameUpdate(delta);
  }

  if (pendingGfx == 0) {
    OSTime draw_start = osGetTime();

    diagPaintPhase(DIAG_PHASE_DRAW);
    gameDraw();
    diagNoteGatedWork((u32) OS_CYCLES_TO_USEC(osGetTime() - draw_start));
  }

  diagPaintPhase(DIAG_PHASE_AUDIO);
  updateAudio();

  diagFrameEnd();
}

/*
 * A reset button press.  The console gives roughly half a second between the
 * press and the actual NMI; switching the display off and putting the VI back
 * to a standard scale is what keeps the next ROM from starting against a
 * half-configured video mode.
 */
static void callbackPreNMI(void) {
  nuGfxDisplayOff();
  osViSetYScale(1);
  osAfterPreNMI();
}

static void initVideo(void) {
  osCreateViManager(OS_PRIORITY_VIMGR);

  /*
   * One framebuffer layout for all three television standards.  PAL has 288
   * visible lines to NTSC's 240, so rather than render more of them -- which
   * would cost fill rate the console does not have -- the VI scales the same
   * 240-line picture up.  MPAL is NTSC timing with PAL colour and needs
   * neither.
   */
  if (osTvType == OS_TV_NTSC) {
    osViSetMode(&osViModeNtscLan1);
  } else if (osTvType == OS_TV_PAL) {
    osViSetMode(&osViModeFpalLan1);
    osViSetYScale(0.833);
  } else if (osTvType == OS_TV_MPAL) {
    osViSetMode(&osViModeMpalLan1);
  }

  osViSetSpecialFeatures(OS_VI_GAMMA_OFF);
  nuPreNMIFuncSet((NUScPreNMIFunc) callbackPreNMI);
}

void mainproc(void) {
  initVideo();
  initGraphics();
  initInput();
  /*
   * Storage owns the flashcart and the PI bus during startup.  Everything that
   * probes hardware happens before the watchdog starts caring about the
   * heartbeat, and before audio touches the PI at all.
   */
  initStorage();
  initDiagnostics();
  initAudio();

  gameInit();

  nuGfxFuncSet((NUGfxFunc) callbackGfx);

  /*
   * NuSystem runs the game from its own threads from here.  This one has
   * nothing left to do and must not exit -- returning from mainproc would
   * unwind into the boot code.
   */
  while (1) {
    ;
  }
}

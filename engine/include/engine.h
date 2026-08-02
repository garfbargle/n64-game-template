#ifndef ENGINE_H
#define ENGINE_H

/*
 * The engine's whole public surface, and the contract a game implements.
 *
 * Include this from game code; it pulls in everything below it.  The engine
 * owns boot, video, the frame loop, the freeze watchdog, input, the font, the
 * UI sprites, audio and the flashcart filesystem.  The game owns what happens
 * between gameUpdate and gameDraw and nothing else.
 */

#include <nusys.h>

#include "gfx.h"
#include "text.h"
#include "ui.h"
#include "diag.h"
#include "input.h"
#include "vecmath.h"
#include "audio.h"
#include "storage.h"

/*
 * The four functions a game must define.  engine/src/boot.c calls them and
 * nothing else; there is no other entry point, and a game never writes
 * mainproc.
 *
 * gameInit runs once, after video, input, storage and audio are up and before
 * the first frame.  Anything that reads the cartridge belongs here rather than
 * in a static initialiser -- storage is not mounted until the engine says so.
 *
 * gameUpdate runs once per graphics callback, which is not once per drawn
 * frame: a callback that finds the RSP still busy updates without drawing.
 * `delta` is that gap in 60 Hz frames (1.0 at retrace rate, ~2.0 at 30 fps),
 * so movement written against it runs at the same speed on NTSC and PAL and
 * does not accelerate when the scene gets simpler.  Never advance anything by
 * a fixed amount per call.
 *
 * gameDraw runs only when a frame is actually being built, between
 * gfxBeginFrame and gfxEndFrame.  Draw, and do not change game state here: it
 * is skipped on busy callbacks, so anything advanced here advances at a rate
 * that depends on how hard the scene is to render.
 *
 * gameReportFreeze is optional -- see diagSetReportHook.  Leave it out if the
 * default post-mortem is enough.
 */
void gameInit(void);
void gameUpdate(float delta);
void gameDraw(void);

/*
 * How long the engine has been running, in 60 Hz frames.  Cheaper than
 * threading a clock through everything that wants to animate.
 */
extern float engine_time;

/*
 * Stop the world.  A paused game still gets gameDraw (the pause screen has to
 * be drawn by something) but gameUpdate is called with a delta of zero, so
 * anything written against delta simply stands still without needing to know
 * the game is paused.
 */
void engineSetPaused(u8 paused);
u8 enginePaused(void);

#endif /* ENGINE_H */

#ifndef GAME_H
#define GAME_H

#include "engine.h"

/*
 * Your game.
 *
 * Everything under game/ is yours to delete.  What is here is a working
 * demonstration of the engine's surface -- a title screen, a 3D scene, a HUD,
 * a pause menu and a save slot -- sized so you can read all of it before you
 * start replacing it.  The engine under it is what you keep.
 */

/* The name that appears on the title screen and in the ROM header.  Change
   this first; STORAGE_DIR in the Makefile is the matching save directory. */
#define GAME_TITLE "N64 GAME"

/*
 * Screens.  A game this size does not need a state machine framework: an enum
 * and a switch in gameUpdate is legible for a long time, and the point at
 * which it stops being legible is a good point to reach for something else.
 */
typedef enum {
  SCREEN_TITLE,
  SCREEN_PLAY,
  SCREEN_PAUSE
} GameScreen;

extern GameScreen current_screen;

/* The scene: one spinning crate over a ground plane, drawn by scene.c. */
void initScene(void);
void updateScene(float delta);
void drawScene(void);
/* Player-controlled camera state the scene draws from and the HUD reports. */
extern float camera_yaw;
extern float camera_pitch;
extern Vector3 camera_target;

/* The 2D layers, drawn by hud.c.  Each is split into a fills pass and a text
   pass, because the RDP cannot be swapped between the two mid-screen -- see
   the phase note in engine/include/gfx.h. */
void drawTitleFills(void);
void drawTitleText(void);
void drawHudFills(void);
void drawHudText(void);
void drawPauseFills(void);
void drawPauseText(void);

/* What the demo saves, so the storage path is exercised rather than merely
   described.  See game.c. */
typedef struct {
  u32 spins;
  u8 music_volume;
  u8 sound_volume;
} SaveData;

extern SaveData save_data;

/* What the HUD needs to know about the rest of the game, kept to the smallest
   set that works: a transient status line, the highlighted save slot, and the
   pause card's cursor. */
const char *gameMessage(void);
u8 gameSelectedSlot(void);
u8 gamePauseRow(void);

#endif /* GAME_H */

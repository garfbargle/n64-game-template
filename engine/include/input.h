#ifndef ENGINE_INPUT_H
#define ENGINE_INPUT_H

#include <nusys.h>

/*
 * Controllers.
 *
 * NuSystem reads the pads; this adds the two things every game then writes for
 * itself -- edge detection, and a stick that behaves.
 *
 * Call inputUpdate() once per graphics callback, before any game logic reads a
 * pad.  After it, inputHeld/inputPressed/inputReleased answer for the frame.
 */
#define MAX_CONTROLLERS 4

void initInput(void);
void inputUpdate(void);

/* Buttons are the NuSystem/libultra masks: A_BUTTON, B_BUTTON, START_BUTTON,
   U_CBUTTONS, L_TRIG, R_TRIG, Z_TRIG, U_JPAD and so on. */
u8 inputHeld(u8 controller, u16 buttons);
/* TRUE only on the frame the button went down. */
u8 inputPressed(u8 controller, u16 buttons);
u8 inputReleased(u8 controller, u16 buttons);

/* TRUE when a controller is plugged in and answering. */
u8 inputConnected(u8 controller);
/* How many are, counted from controller 0 upward -- what a game asks before
   offering split screen. */
u8 inputControllerCount(void);

/*
 * The analog stick, shaped.
 *
 * The raw stick is not usable as-is: a real N64 stick rests a few units off
 * centre, wears with age, and reaches maybe 70 of its nominal 80 units at full
 * deflection -- and every controller differs.  These apply a dead zone, then
 * rescale what is left so the edge of the gate reads as 1.0 rather than 0.85.
 * The result is in [-1, 1] on each axis.
 *
 * Diagonals are clamped to the unit circle, so a stick held corner-to-corner
 * does not travel 1.41x faster than one held straight.
 */
float inputStickX(u8 controller);
float inputStickY(u8 controller);
/* Deflection in [0, 1], for a game that wants speed from how far the stick is
   pushed rather than a direction alone. */
float inputStickMagnitude(u8 controller);

/*
 * The two constants above, exposed because they are the ones worth retuning
 * against real hardware.  Rest the stick to read its centre error, then roll it
 * round the gate to read how far it actually travels; the diagnostics overlay
 * shows both.
 */
#define STICK_DEAD_ZONE 8.f
#define STICK_RANGE 68.f

/*
 * A held direction that repeats, which is what every menu wants and nothing
 * gives you: TRUE on the press, then again every `INPUT_REPEAT_INTERVAL`
 * frames once `INPUT_REPEAT_DELAY` have passed.  Pass the stick axis as a
 * direction (-1, 0, 1) so a menu answers to the stick and the D-pad alike.
 */
#define INPUT_REPEAT_DELAY 20
#define INPUT_REPEAT_INTERVAL 6
s8 inputMenuVertical(u8 controller);
s8 inputMenuHorizontal(u8 controller);

#endif /* ENGINE_INPUT_H */

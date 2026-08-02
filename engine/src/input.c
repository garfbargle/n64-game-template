#include "input.h"
#include "vecmath.h"

/*
 * Controllers.
 *
 * NuSystem reads the pads into nuContData; this keeps the previous frame's
 * buttons so a press can be told from a hold, and shapes the stick so a game
 * never has to.
 */

static NUContData pads[MAX_CONTROLLERS];
static u16 held[MAX_CONTROLLERS];
static u16 last_held[MAX_CONTROLLERS];
static u8 connected[MAX_CONTROLLERS];
static u8 connected_pattern;

/* Menu auto-repeat, per controller and per axis. */
static u8 repeat_timer[MAX_CONTROLLERS][2];
static s8 repeat_last[MAX_CONTROLLERS][2];

void initInput(void) {
  /* One bit per port that answered at boot.  Hot-plugging a pad after this is
     not detected -- the console does not report it -- which is why a game that
     offers drop-in co-op has to poll for a START on an unused port instead. */
  connected_pattern = nuContInit();
}

void inputUpdate(void) {
  u8 i;

  nuContDataGetExAll(pads);
  for (i = 0; i < MAX_CONTROLLERS; i++) {
    last_held[i] = held[i];
    connected[i] = (connected_pattern & (1 << i)) != 0;
    /* A pad that is absent or erroring reads as nothing pressed rather than
       as garbage: an unplugged controller must not look like every button at
       once. */
    held[i] = (connected[i] && pads[i].errno == 0) ? pads[i].button : 0;
  }
}

u8 inputConnected(u8 controller) {
  return controller < MAX_CONTROLLERS ? connected[controller] : FALSE;
}

u8 inputControllerCount(void) {
  u8 i;
  u8 count = 0;

  for (i = 0; i < MAX_CONTROLLERS; i++) {
    if (connected[i]) {
      count++;
    }
  }
  return count;
}

u8 inputHeld(u8 controller, u16 buttons) {
  return controller < MAX_CONTROLLERS &&
    (held[controller] & buttons) != 0;
}

u8 inputPressed(u8 controller, u16 buttons) {
  return controller < MAX_CONTROLLERS &&
    (held[controller] & buttons) != 0 &&
    (last_held[controller] & buttons) == 0;
}

u8 inputReleased(u8 controller, u16 buttons) {
  return controller < MAX_CONTROLLERS &&
    (held[controller] & buttons) == 0 &&
    (last_held[controller] & buttons) != 0;
}

/*
 * The stick, shaped.
 *
 * A real N64 stick rests a few units off centre, wears with age, and reaches
 * maybe 68 of its nominal 80 units at full deflection -- and every controller
 * differs.  Reading the raw value gives a game that drifts on one pad and
 * cannot reach full speed on another.
 *
 * So: subtract a dead zone, then rescale what is left so the edge of the gate
 * reads as 1.0.  The magnitude is clamped to the unit circle before either
 * axis is scaled, which is what stops a stick held corner-to-corner from
 * travelling 1.41 times faster than one held straight.
 */
static void shapedStick(u8 controller, float *out_x, float *out_y) {
  float x, y, magnitude;

  *out_x = 0.f;
  *out_y = 0.f;
  if (controller >= MAX_CONTROLLERS || !connected[controller]) {
    return;
  }
  x = (float) pads[controller].stick_x;
  y = (float) pads[controller].stick_y;
  magnitude = sqrtf(x * x + y * y);
  if (magnitude <= STICK_DEAD_ZONE) {
    return;
  }
  {
    float shaped = (magnitude - STICK_DEAD_ZONE) /
      (STICK_RANGE - STICK_DEAD_ZONE);

    if (shaped > 1.f) {
      shaped = 1.f;
    }
    /* Scale the direction, not each axis independently. */
    *out_x = x / magnitude * shaped;
    *out_y = y / magnitude * shaped;
  }
}

float inputStickX(u8 controller) {
  float x, y;

  shapedStick(controller, &x, &y);
  return x;
}

float inputStickY(u8 controller) {
  float x, y;

  shapedStick(controller, &x, &y);
  return y;
}

float inputStickMagnitude(u8 controller) {
  float x, y;

  shapedStick(controller, &x, &y);
  return sqrtf(x * x + y * y);
}

/*
 * A held direction that repeats.  The stick and the D-pad answer together,
 * because a player who has just used one to move a cursor will try the other
 * without thinking about it.
 *
 * The stick threshold is deliberately high: a menu that steps on a stick only
 * half pushed skips rows while the player is still deciding.
 */
#define MENU_STICK_THRESHOLD 0.5f

static s8 repeatAxis(u8 controller, u8 axis, s8 direction) {
  if (controller >= MAX_CONTROLLERS) {
    return 0;
  }
  if (direction == 0) {
    repeat_timer[controller][axis] = 0;
    repeat_last[controller][axis] = 0;
    return 0;
  }
  if (direction != repeat_last[controller][axis]) {
    repeat_last[controller][axis] = direction;
    repeat_timer[controller][axis] = 0;
    return direction;
  }
  repeat_timer[controller][axis]++;
  if (repeat_timer[controller][axis] < INPUT_REPEAT_DELAY) {
    return 0;
  }
  if ((repeat_timer[controller][axis] - INPUT_REPEAT_DELAY) %
      INPUT_REPEAT_INTERVAL != 0) {
    return 0;
  }
  return direction;
}

s8 inputMenuVertical(u8 controller) {
  float y = inputStickY(controller);
  s8 direction = 0;

  if (inputHeld(controller, U_JPAD) || y > MENU_STICK_THRESHOLD) {
    direction = -1;   /* up the list */
  } else if (inputHeld(controller, D_JPAD) || y < -MENU_STICK_THRESHOLD) {
    direction = 1;
  }
  return repeatAxis(controller, 0, direction);
}

s8 inputMenuHorizontal(u8 controller) {
  float x = inputStickX(controller);
  s8 direction = 0;

  if (inputHeld(controller, L_JPAD) || x < -MENU_STICK_THRESHOLD) {
    direction = -1;
  } else if (inputHeld(controller, R_JPAD) || x > MENU_STICK_THRESHOLD) {
    direction = 1;
  }
  return repeatAxis(controller, 1, direction);
}

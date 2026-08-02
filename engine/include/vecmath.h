#ifndef ENGINE_VECMATH_H
#define ENGINE_VECMATH_H

#include <nusys.h>

/*
 * The N64 has no FPU library worth linking and libultra's math is patchy, so
 * the engine carries its own.  Everything here is float and single precision:
 * the CPU is configured -mfp32 and doubles are emulated.
 */

typedef struct {
  float x;
  float y;
  float z;
} Vector3;

typedef struct {
  int x;
  int y;
  int z;
} Vector3i;

/*
 * The general-purpose RNG state.  Every random() call advances it, so it is a
 * stream, not a value: nothing that has to be reproducible may read it.  A
 * game that generates content from a seed should keep that seed separately and
 * derive from it by coordinate, so the same input always gives the same
 * output whatever order things are asked for in.
 */
extern u32 seed;

u32 random(u32 limit);
/* Seed the stream.  Anything derived from the boot count or the player's first
   input makes a serviceable entropy source on a console with no clock. */
void randomSeed(u32 value);

float min(float a, float b);
float max(float a, float b);
int floor(float v);
float tanf(float angle);

/* Heading, in [0, 360), that makes an entity's forward vector point along
   (x, z).  See the definition for why this is an approximation. */
float directionYaw(float x, float z);

/* The equivalent angle in (-180, 180]; the short way round. */
float wrapDegrees(float angle);

/* Turn `from` toward `to` by at most `step` degrees, taking the short way and
   staying in [0, 360). */
float approachAngle(float from, float to, float step);

float *at(Vector3 *v, int i);
int *ati(Vector3i *v, int i);

Vector3 add(Vector3 a, Vector3 b);
Vector3i addi(Vector3i a, Vector3i b);
Vector3 mul(Vector3 a, float b);
Vector3 div(Vector3 a, float b);
Vector3i divToInt(Vector3 a, float b);
float dot(Vector3 a, Vector3 b);
Vector3 rotateX(Vector3 v, float angle);
Vector3 rotateY(Vector3 v, float angle);

/*
 * One axis of a DDA ray march over a uniform grid of `cell_size` cells:
 * advances `t` to the next cell boundary along `axis` and steps the cell
 * index.  A grid game's targeting raycast is a loop over the three axes
 * picking the smallest `t`.
 */
void rayStepAxis(float origin, float direction, int block, float *t,
  Vector3i *step, int axis, float cell_size);

#endif /* ENGINE_VECMATH_H */

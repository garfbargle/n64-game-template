#include "game.h"
#include "texture_data.h"

/*
 * The 3D half of the demo: a crate turning over a ground plane, with a camera
 * the player can walk around it.
 *
 * Small on purpose.  What it is really showing is the shape of a frame -- a
 * projection, a camera, a matrix per model, one texture bind per material --
 * because that shape is the same whether the scene is one crate or a city.
 */

float camera_yaw = 30.f;
float camera_pitch = 18.f;
Vector3 camera_target = {0.f, 40.f, 0.f};

static float camera_distance = 300.f;
static float crate_spin;

/* World units.  Nothing forces a scale on you, but pick one and write it
   down: vertex positions are s16 and matrices are s15.16, so a game that
   drifts between "1 unit is a centimetre" and "1 unit is a metre" runs out of
   precision in one place and out of range in the other. */
#define CRATE_SIZE 40
#define GROUND_SIZE 400

/*
 * The crate: 24 vertices, six faces, each with its own texture coordinates.
 * Texture coordinates are in 10.5 fixed point, so one full 16-texel tile is
 * 16 << 5 = 512.
 */
#define TILE (16 << 5)
/* Deliberately not named *VERTEX: tools/preview/render.py parses any macro
   whose name ends in VERTEX as (x, y, z, r, g, b), and would read this one's
   texture coordinates as a colour.  Shaded models use ENGINE_VERTEX and are
   understood by that tool; textured ones are not. */
#define TEXTURED_VTX(x, y, z, s, t, shade) \
  {{{x, y, z}, 0, {s, t}, {shade, shade, shade, 255}}}

static Vtx crate_verts[] = {
  /* -Z, the face toward the camera at rest.  Each face gets a flat shade of
     its own, which is what gives an untextured-looking box its form without
     paying for a light. */
  TEXTURED_VTX(-CRATE_SIZE,  CRATE_SIZE, -CRATE_SIZE, 0,    0,    170),
  TEXTURED_VTX( CRATE_SIZE,  CRATE_SIZE, -CRATE_SIZE, TILE, 0,    170),
  TEXTURED_VTX( CRATE_SIZE, -CRATE_SIZE, -CRATE_SIZE, TILE, TILE, 170),
  TEXTURED_VTX(-CRATE_SIZE, -CRATE_SIZE, -CRATE_SIZE, 0,    TILE, 170),
  /* +Z */
  TEXTURED_VTX( CRATE_SIZE,  CRATE_SIZE,  CRATE_SIZE, 0,    0,    150),
  TEXTURED_VTX(-CRATE_SIZE,  CRATE_SIZE,  CRATE_SIZE, TILE, 0,    150),
  TEXTURED_VTX(-CRATE_SIZE, -CRATE_SIZE,  CRATE_SIZE, TILE, TILE, 150),
  TEXTURED_VTX( CRATE_SIZE, -CRATE_SIZE,  CRATE_SIZE, 0,    TILE, 150),
  /* -X */
  TEXTURED_VTX(-CRATE_SIZE,  CRATE_SIZE,  CRATE_SIZE, 0,    0,    130),
  TEXTURED_VTX(-CRATE_SIZE,  CRATE_SIZE, -CRATE_SIZE, TILE, 0,    130),
  TEXTURED_VTX(-CRATE_SIZE, -CRATE_SIZE, -CRATE_SIZE, TILE, TILE, 130),
  TEXTURED_VTX(-CRATE_SIZE, -CRATE_SIZE,  CRATE_SIZE, 0,    TILE, 130),
  /* +X */
  TEXTURED_VTX( CRATE_SIZE,  CRATE_SIZE, -CRATE_SIZE, 0,    0,    190),
  TEXTURED_VTX( CRATE_SIZE,  CRATE_SIZE,  CRATE_SIZE, TILE, 0,    190),
  TEXTURED_VTX( CRATE_SIZE, -CRATE_SIZE,  CRATE_SIZE, TILE, TILE, 190),
  TEXTURED_VTX( CRATE_SIZE, -CRATE_SIZE, -CRATE_SIZE, 0,    TILE, 190),
  /* +Y, the lid: brightest, because that is where the light would be. */
  TEXTURED_VTX(-CRATE_SIZE,  CRATE_SIZE,  CRATE_SIZE, 0,    0,    220),
  TEXTURED_VTX( CRATE_SIZE,  CRATE_SIZE,  CRATE_SIZE, TILE, 0,    220),
  TEXTURED_VTX( CRATE_SIZE,  CRATE_SIZE, -CRATE_SIZE, TILE, TILE, 220),
  TEXTURED_VTX(-CRATE_SIZE,  CRATE_SIZE, -CRATE_SIZE, 0,    TILE, 220),
  /* -Y */
  TEXTURED_VTX(-CRATE_SIZE, -CRATE_SIZE, -CRATE_SIZE, 0,    0,    90),
  TEXTURED_VTX( CRATE_SIZE, -CRATE_SIZE, -CRATE_SIZE, TILE, 0,    90),
  TEXTURED_VTX( CRATE_SIZE, -CRATE_SIZE,  CRATE_SIZE, TILE, TILE, 90),
  TEXTURED_VTX(-CRATE_SIZE, -CRATE_SIZE,  CRATE_SIZE, 0,    TILE, 90)
};

/*
 * The ground: one quad, with its texture coordinates run well past the tile so
 * it repeats.  Wrapping is free -- it is a bit in the tile descriptor -- which
 * is why a 160-byte texture can cover a whole floor.
 */
#define GROUND_REPEAT (TILE * 8)

/*
 * Wound the same way as the crate's lid, and that is not cosmetic: the pipeline
 * runs with G_CULL_BACK, so a quad wound the other way is invisible from above
 * and perfectly visible from underneath.  An upward-facing surface goes
 * (-x, +z), (+x, +z), (+x, -z), (-x, -z) -- if a floor disappears, this is the
 * first thing to check.
 */
static Vtx ground_verts[] = {
  TEXTURED_VTX(-GROUND_SIZE, 0,  GROUND_SIZE, 0, GROUND_REPEAT, 140),
  TEXTURED_VTX( GROUND_SIZE, 0,  GROUND_SIZE, GROUND_REPEAT, GROUND_REPEAT,
    140),
  TEXTURED_VTX( GROUND_SIZE, 0, -GROUND_SIZE, GROUND_REPEAT, 0, 140),
  TEXTURED_VTX(-GROUND_SIZE, 0, -GROUND_SIZE, 0, 0, 140)
};

static Gfx ground_display_list[] = {
  gsSP2Triangles(0, 1, 2, 0, 0, 2, 3, 0),
  gsSPEndDisplayList()
};

/*
 * The pedestal the crate turns over: the other kind of box.
 *
 * Eight shared corners instead of twenty-four, no texture, and its light baked
 * into two vertex colours -- the lit top and the shaded sides.  A third of the
 * vertices to transform, which is the right trade for anything that does not
 * need a texture, and the reason a game built out of dozens of small props
 * should reach for this one first.
 *
 * It is also what tools/preview/render.py understands: that tool parses
 * ENGINE_VERTEX models straight out of this file, so a model declared this way
 * can be posed and framed on your machine without building a ROM.
 */
#define PEDESTAL_HALF 70
#define PEDESTAL_HEIGHT 20

ENGINE_SHADED_BOX(pedestal_verts,
  -PEDESTAL_HALF, 0, -PEDESTAL_HALF,
  PEDESTAL_HALF, PEDESTAL_HEIGHT, PEDESTAL_HALF,
  126, 132, 152, 74, 80, 98);

/*
 * Matrices are double-buffered, like every other structure the RSP reads: the
 * task drawing the previous frame may still be walking the list that points at
 * them, so the one being built must write into the other copy.  Getting this
 * wrong produces geometry that flickers between two poses -- see NUM_DISPLAY_LISTS.
 */
static Mtx crate_matrix[NUM_DISPLAY_LISTS];
static Mtx ground_matrix[NUM_DISPLAY_LISTS];
static Mtx pedestal_matrix[NUM_DISPLAY_LISTS];

void initScene(void) {
  crate_spin = 0.f;
}

void updateScene(float delta) {
  float move_x = inputStickX(0);
  float move_y = inputStickY(0);

  /* Degrees per 60 Hz frame.  Everything that moves is multiplied by delta, so
     it runs at the same speed whether the console is drawing 60 frames a
     second or 20. */
  crate_spin += 1.2f * delta;
  if (crate_spin >= 360.f) {
    crate_spin -= 360.f;
    save_data.spins++;
  }

  camera_yaw += move_x * 2.5f * delta;
  camera_pitch -= move_y * 1.5f * delta;
  if (camera_pitch > 80.f) {
    camera_pitch = 80.f;
  } else if (camera_pitch < -20.f) {
    camera_pitch = -20.f;
  }

  if (inputHeld(0, R_TRIG)) {
    camera_distance -= 4.f * delta;
  } else if (inputHeld(0, L_TRIG)) {
    camera_distance += 4.f * delta;
  }
  if (camera_distance < 120.f) {
    camera_distance = 120.f;
  } else if (camera_distance > 600.f) {
    camera_distance = 600.f;
  }
}

void drawScene(void) {
  Vector3 eye;
  float pitch_radians = camera_pitch * M_DTOR;
  float yaw_radians = camera_yaw * M_DTOR;
  float horizontal = cosf(pitch_radians) * camera_distance;

  eye.x = camera_target.x + sinf(yaw_radians) * horizontal;
  eye.y = camera_target.y + sinf(pitch_radians) * camera_distance;
  eye.z = camera_target.z + cosf(yaw_radians) * horizontal;

  gfxSelectViewport(0);
  /*
   * Near and far are worth thinking about rather than copying.  The N64's
   * depth buffer is heavily non-linear, so almost all of its precision sits
   * near the near plane: pushing `near` out from 1 to 10 buys more usable
   * depth range than any change to `far` does, and a scene with z-fighting in
   * the distance is usually asking for a further near plane rather than a
   * closer far one.
   */
  gfxLoadPerspective(60.f, 10.f, 4000.f);
  gfxLookAt(eye.x, eye.y, eye.z,
    camera_target.x, camera_target.y, camera_target.z, 0.f);

  gfxBeginTextured();

  /* One bind per material, then everything that shares it.  The engine skips a
     redundant load, so drawing all the crates together and all the ground
     together costs two loads however many there are. */
  gfxLoadTexture(&ground_texture);
  modelMatrix(&ground_matrix[dl_no], 0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
  gfxPushMatrix(&ground_matrix[dl_no]);
  gSPVertex(dlp++, ground_verts, 4, 0);
  gSPDisplayList(dlp++, ground_display_list);
  gfxPopMatrix();

  gfxLoadTexture(&crate_texture);
  /* Rotate about the model's own origin, then move it into the world -- one
     matrix and one gSPMatrix, not two.  See modelMatrix. */
  modelMatrix(&crate_matrix[dl_no], 0.f, crate_spin, 0.f, 0.f,
    PEDESTAL_HEIGHT + CRATE_SIZE + 20.f, 0.f);
  gfxPushMatrix(&crate_matrix[dl_no]);
  gSPVertex(dlp++, crate_verts, ENGINE_BOX_VERTEX_COUNT, 0);
  gSPDisplayList(dlp++, box_display_list);
  gfxPopMatrix();

  /* Untextured geometry last, and behind its own combiner: without
     gfxBeginShaded it would draw modulated against whatever tile the crate
     left bound. */
  gfxBeginShaded();
  modelMatrix(&pedestal_matrix[dl_no], 0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
  gfxPushMatrix(&pedestal_matrix[dl_no]);
  gSPVertex(dlp++, pedestal_verts, ENGINE_SHADED_BOX_VERTEX_COUNT, 0);
  gSPDisplayList(dlp++, shaded_box_display_list);
  gfxPopMatrix();
}

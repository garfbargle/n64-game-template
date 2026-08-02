#include <assert.h>

#include "gfx.h"
#include "text.h"
#include "diag.h"
#include "vecmath.h"

Gfx *dlp;
Gfx frame_display_lists[NUM_DISPLAY_LISTS][FRAME_DISPLAY_LIST_SIZE];
u32 dl_no;
u32 frame_overflows;

static Gfx *frame_start;
static u32 frame_command_peak;

u32 gfxCommandsUsed(void) {
  return frame_command_peak;
}

/*
 * The RCP's opening state for a frame.  Segment 0 is the identity mapping, so
 * every pointer the game hands the RSP is a plain address.
 */
static Gfx frame_setup_display_list[] = {
  gsSPSegment(0, 0x0),
  gsDPSetCombineMode(G_CC_SHADE, G_CC_SHADE),
  gsDPSetScissor(G_SC_NON_INTERLACE, 0, 0, SCREEN_WD, SCREEN_HT),
  gsSPEndDisplayList()
};

/*
 * The mode a textured, z-buffered 3D pass wants.
 *
 * Note what is *not* here: G_LIGHTING.  By default a vertex's colour slot is
 * its colour, interpolated across the triangle -- which on this machine is
 * usually the better deal, because baking the light into the vertices at
 * authoring time costs the RSP nothing per frame and a real light costs a
 * normal transform per vertex.  Call gfxSetLighting(TRUE) for the other path;
 * see the note there about what it does to the colour slot.
 */
static Gfx model_setup_display_list[] = {
  gsDPPipeSync(),
  gsDPSetCycleType(G_CYC_1CYCLE),
  gsDPSetRenderMode(G_RM_ZB_OPA_SURF, G_RM_ZB_OPA_SURF2),
  gsSPClearGeometryMode(0xFFFFFFFF),
  gsSPSetGeometryMode(G_ZBUFFER | G_CULL_BACK | G_SHADE | G_SHADING_SMOOTH),
  gsSPTexture(0x8000, 0x8000, 0, G_TX_RENDERTILE, G_ON),
  gsDPSetTexturePersp(G_TP_PERSP),
  gsDPSetCombineMode(G_CC_MODULATERGB, G_CC_MODULATERGB),
  gsDPSetTextureLUT(G_TT_RGBA16),
  gsSPEndDisplayList()
};

/*
 * A box from six quads over 24 vertices, laid out -Z, +Z, -X, +X, +Y, -Y with
 * four vertices per face.  Four per face rather than eight shared corners
 * because each face needs its own texture coordinates -- a shared corner
 * belongs to three faces that each want a different (s, t) there.
 */
Gfx box_display_list[] = {
  gsSP2Triangles(0, 1, 2, 0, 0, 2, 3, 0),
  gsSP2Triangles(4, 5, 6, 0, 4, 6, 7, 0),
  gsSP2Triangles(8, 9, 10, 0, 8, 10, 11, 0),
  gsSP2Triangles(12, 13, 14, 0, 12, 14, 15, 0),
  gsSP2Triangles(16, 17, 18, 0, 16, 18, 19, 0),
  gsSP2Triangles(20, 21, 22, 0, 20, 22, 23, 0),
  gsSPEndDisplayList()
};

/*
 * The same box from eight shared corners, for untextured geometry.
 *
 * A third of the vertices to transform and a third of the RDRAM, which is why
 * it is worth having as well: an entity built from a dozen flat-shaded boxes
 * pays for this one, not the one above.  Vertex colours only -- there is
 * nowhere to put per-face texture coordinates.
 */
Gfx shaded_box_display_list[] = {
  gsSP2Triangles(0, 1, 2, 0, 0, 2, 3, 0),
  gsSP2Triangles(4, 5, 6, 0, 4, 6, 7, 0),
  gsSP2Triangles(1, 4, 7, 0, 1, 7, 2, 0),
  gsSP2Triangles(5, 0, 3, 0, 5, 3, 6, 0),
  gsSP2Triangles(5, 4, 1, 0, 5, 1, 0, 0),
  gsSP2Triangles(3, 2, 7, 0, 3, 7, 6, 0),
  gsSPEndDisplayList()
};

/*
 * Untextured geometry needs the combiner pointed at shade rather than at a
 * texture, or it draws with whatever tile the last pass happened to leave
 * bound -- which makes a model's brightness depend on what else is on screen.
 */
void gfxBeginShaded(void) {
  gDPPipeSync(dlp++);
  gDPSetCombineMode(dlp++, G_CC_SHADE, G_CC_SHADE);
}

void gfxBeginTextured(void) {
  gDPPipeSync(dlp++);
  gDPSetCombineMode(dlp++, G_CC_MODULATERGB, G_CC_MODULATERGB);
  gfxInvalidateTexture();
}

/*
 * Real lighting, when baked vertex colours will not do -- an object that turns
 * under a fixed light, a day/night cycle over static geometry.
 *
 * With lighting on, the RSP reads a vertex's colour slot as its *normal*
 * (signed, -128..127 per axis) and computes the colour itself.  Geometry
 * authored for one path therefore draws as nonsense under the other, so switch
 * this per pass and keep each pass's models to one convention.
 */
void gfxSetLighting(u8 on) {
  if (on) {
    gSPSetGeometryMode(dlp++, G_LIGHTING);
  } else {
    gSPClearGeometryMode(dlp++, G_LIGHTING);
  }
}

/*
 * Matrices live in per-display-list pools, because the RSP may still be
 * walking the previous frame's list: a matrix an in-flight task can still read
 * must never be rewritten.  Everything here is written into the buffer the
 * frame being built owns, and the two alternate.
 */
static Mtx projection_matrix[NUM_DISPLAY_LISTS];
static Mtx viewing_matrix[NUM_DISPLAY_LISTS];
static Lights1 frame_lights[NUM_DISPLAY_LISTS];

/*
 * Viewports.  N64 viewport coordinates are 10.2 fixed point, hence the doubled
 * dimensions and centres.  All of these fill the same framebuffer as solo
 * play, so a split screen does not cost extra fill rate.
 */
static Vp full_viewport = {
  SCREEN_WD * 2, SCREEN_HT * 2, G_MAXZ / 2, 0,
  SCREEN_WD * 2, SCREEN_HT * 2, G_MAXZ / 2, 0
};

static Vp two_player_viewports[2] = {
  {SCREEN_WD * 2, SCREEN_HT, G_MAXZ / 2, 0,
   SCREEN_WD * 2, SCREEN_HT, G_MAXZ / 2, 0},
  {SCREEN_WD * 2, SCREEN_HT, G_MAXZ / 2, 0,
   SCREEN_WD * 2, SCREEN_HT * 3, G_MAXZ / 2, 0}
};

static Vp four_player_viewports[GFX_MAX_VIEWPORTS] = {
  {SCREEN_WD, SCREEN_HT, G_MAXZ / 2, 0,
   SCREEN_WD, SCREEN_HT, G_MAXZ / 2, 0},
  {SCREEN_WD, SCREEN_HT, G_MAXZ / 2, 0,
   SCREEN_WD * 3, SCREEN_HT, G_MAXZ / 2, 0},
  {SCREEN_WD, SCREEN_HT, G_MAXZ / 2, 0,
   SCREEN_WD, SCREEN_HT * 3, G_MAXZ / 2, 0},
  {SCREEN_WD, SCREEN_HT, G_MAXZ / 2, 0,
   SCREEN_WD * 3, SCREEN_HT * 3, G_MAXZ / 2, 0}
};

static u8 viewport_count = 1;

static u8 usesQuadrants(void) {
  return viewport_count >= 3;
}

void gfxSetViewports(u8 count) {
  viewport_count = count < 1 ? 1 :
    (count > GFX_MAX_VIEWPORTS ? GFX_MAX_VIEWPORTS : count);
}

u8 gfxViewportCount(void) {
  return viewport_count;
}

u32 gfxViewportWidth(void) {
  return usesQuadrants() ? SCREEN_WD / 2 : SCREEN_WD;
}

u32 gfxViewportHeight(void) {
  return viewport_count > 1 ? SCREEN_HT / 2 : SCREEN_HT;
}

u32 gfxViewportX(u8 index) {
  return usesQuadrants() ? (index & 1) * (SCREEN_WD / 2) : 0;
}

u32 gfxViewportY(u8 index) {
  return viewport_count > 1 ?
    (usesQuadrants() ? index / 2 : index) * (SCREEN_HT / 2) : 0;
}

void gfxSelectViewport(u8 index) {
  u32 x = gfxViewportX(index);
  u32 y = gfxViewportY(index);

  if (viewport_count == 1) {
    gSPViewport(dlp++, &full_viewport);
  } else if (usesQuadrants()) {
    gSPViewport(dlp++, &four_player_viewports[index]);
  } else {
    gSPViewport(dlp++, &two_player_viewports[index]);
  }
  gDPSetScissor(dlp++, G_SC_NON_INTERLACE, x, y,
    x + gfxViewportWidth(), y + gfxViewportHeight());
}

void gfxSelectFullViewport(void) {
  gSPViewport(dlp++, &full_viewport);
  gDPSetScissor(dlp++, G_SC_NON_INTERLACE, 0, 0, SCREEN_WD, SCREEN_HT);
}

/*
 * The projection.  The aspect ratio comes from the current viewport, so a
 * split-screen half is not stretched; guPerspective also hands back a scale
 * for gSPPerspNormalize, which is what keeps w-buffered depth from losing
 * precision at the far end.
 */
static float projection_far = 14000.f;

void gfxLoadPerspective(float fovy, float near, float far) {
  u16 perspective_normal;
  float aspect = (float) gfxViewportWidth() / (float) gfxViewportHeight();

  projection_far = far;
  guPerspective(&projection_matrix[dl_no], &perspective_normal, fovy, aspect,
    near, far, 1.f);
  gSPPerspNormalize(dlp++, perspective_normal);
  gSPMatrix(dlp++, &projection_matrix[dl_no],
    G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
}

void gfxLoadOrtho(void) {
  guOrtho(&projection_matrix[dl_no], 0.f, (float) SCREEN_WD,
    (float) SCREEN_HT, 0.f, -1.f, 1.f, 1.f);
  gSPMatrix(dlp++, &projection_matrix[dl_no],
    G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
}

void gfxLookAt(float eye_x, float eye_y, float eye_z,
    float at_x, float at_y, float at_z, float roll) {
  /* Up is +Y, tilted by roll.  guLookAtReflect and friends want a full up
     vector; deriving it from a roll angle is what a game actually has. */
  float up_x = sinf(roll * M_DTOR);
  float up_y = cosf(roll * M_DTOR);

  guLookAt(&viewing_matrix[dl_no], eye_x, eye_y, eye_z, at_x, at_y, at_z,
    up_x, up_y, 0.f);
  gSPMatrix(dlp++, &viewing_matrix[dl_no],
    G_MTX_PROJECTION | G_MTX_MUL | G_MTX_NOPUSH);
}

/*
 * One matrix per part, where there would otherwise be two.
 *
 * A model that loads a translation and then multiplies a rotation onto it
 * costs two Mtx of RDRAM per part -- doubled again for the double buffer --
 * and two gSPMatrix for the RSP to walk.  It never needs to.  Vertices go
 * through row-vector, so the pair composes to `rotation * translation`, and a
 * rotation's bottom row is (0, 0, 0, 1): the product is the rotation with its
 * bottom row replaced by the translation.  No multiply, one conversion instead
 * of two, and half the matrix memory.
 *
 * This holds for any linear part whose bottom row is (0, 0, 0, 1) -- a
 * rotation, a scale, or the two already combined.  The camera is unaffected:
 * it lives on the projection stack, and the modelview is always loaded fresh.
 */
void modelMatrixFrom(Mtx *out, float linear[4][4], float x, float y,
    float z) {
  linear[3][0] = x;
  linear[3][1] = y;
  linear[3][2] = z;
  linear[3][3] = 1.f;
  guMtxF2L(linear, out);
}

void modelMatrix(Mtx *out, float pitch, float yaw, float roll, float x,
    float y, float z) {
  float linear[4][4];

  guRotateRPYF(linear, pitch, yaw, roll);
  modelMatrixFrom(out, linear, x, y, z);
}

void gfxPushMatrix(Mtx *matrix) {
  gSPMatrix(dlp++, matrix, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_PUSH);
}

void gfxPopMatrix(void) {
  gSPPopMatrix(dlp++, G_MTX_MODELVIEW);
}

void gfxSetLight(u8 ambient_r, u8 ambient_g, u8 ambient_b,
    u8 light_r, u8 light_g, u8 light_b, float dir_x, float dir_y,
    float dir_z) {
  Lights1 *lights = &frame_lights[dl_no];
  float length = sqrtf(dir_x * dir_x + dir_y * dir_y + dir_z * dir_z);

  /* gdSPDefLights1 is an initialiser, not an expression, so the fields are
     written out rather than assigned through it.  Ambient is stored twice
     because the microcode reads one copy and the RDP the other. */
  lights->a.l.col[0] = ambient_r;
  lights->a.l.col[1] = ambient_g;
  lights->a.l.col[2] = ambient_b;
  lights->a.l.colc[0] = ambient_r;
  lights->a.l.colc[1] = ambient_g;
  lights->a.l.colc[2] = ambient_b;

  lights->l[0].l.col[0] = light_r;
  lights->l[0].l.col[1] = light_g;
  lights->l[0].l.col[2] = light_b;
  lights->l[0].l.colc[0] = light_r;
  lights->l[0].l.colc[1] = light_g;
  lights->l[0].l.colc[2] = light_b;

  /* Light directions are s8 per axis.  Normalising to 120 rather than 127
     leaves headroom so a component that rounds up cannot wrap negative. */
  if (length < 1e-6f) {
    length = 1.f;
  }
  lights->l[0].l.dir[0] = (s8) (dir_x / length * 120.f);
  lights->l[0].l.dir[1] = (s8) (dir_y / length * 120.f);
  lights->l[0].l.dir[2] = (s8) (dir_z / length * 120.f);

  gSPSetLights1(dlp++, (*lights));
}

/*
 * Fog is a function of *screen* depth rather than world distance: gbi.h wants
 * a start/end pair in a compressed 0..1000 space whose mapping depends on the
 * projection's far plane.  FOG_START_MIN..FOG_START_MAX spans roughly the
 * outer half of the view; parking at the maximum is free, because every
 * primitive then classifies as unfogged.
 *
 * Fog also forces its pass into two-cycle mode -- roughly half the RDP's pixel
 * rate -- so switch it off for passes that cannot reach the band rather than
 * leaving it on for the whole frame.
 */
#define FOG_BAND 4

void gfxSetFog(u8 enabled, u16 start, u8 r, u8 g, u8 b) {
  if (!enabled) {
    gDPPipeSync(dlp++);
    gSPClearGeometryMode(dlp++, G_FOG);
    gDPSetCycleType(dlp++, G_CYC_1CYCLE);
    gDPSetRenderMode(dlp++, G_RM_ZB_OPA_SURF, G_RM_ZB_OPA_SURF2);
    return;
  }
  if (start < FOG_START_MIN) {
    start = FOG_START_MIN;
  }
  if (start > FOG_START_MAX) {
    start = FOG_START_MAX;
  }
  gDPPipeSync(dlp++);
  gSPSetGeometryMode(dlp++, G_FOG);
  gDPSetCycleType(dlp++, G_CYC_2CYCLE);
  gDPSetRenderMode(dlp++, G_RM_FOG_SHADE_A, G_RM_ZB_OPA_SURF2);
  gDPSetFogColor(dlp++, r, g, b, 255);
  gSPFogPosition(dlp++, start, 1000);
}

/*
 * Texture binding, with the "already loaded" check that turns a pass over many
 * models sharing one texture into a single load.
 */
static Texture *loaded_texture;

void gfxInvalidateTexture(void) {
  loaded_texture = NULL;
}

static void loadTextureWith(Texture *texture, u32 wrap_t) {
  if (texture == loaded_texture) {
    return;
  }
  loaded_texture = texture;
  gDPLoadTLUT_pal16(dlp++, 0, texture->pallet);
  gDPLoadTextureBlock_4b(dlp++, texture->color_indices, G_IM_FMT_CI,
    16, 16, 0, G_TX_WRAP, wrap_t, 4, 4, G_TX_NOLOD, G_TX_NOLOD);
}

void gfxLoadTexture(Texture *texture) {
  loadTextureWith(texture, G_TX_WRAP);
}

void gfxLoadTextureClamped(Texture *texture) {
  loadTextureWith(texture, G_TX_CLAMP);
}

/*
 * The two 2D phases.  See the hazard note in gfx.h: a screen draws all of its
 * fills, then all of its text, and never alternates.
 */
void gfxBeginFills(void) {
  gDPPipeSync(dlp++);
  gDPSetCycleType(dlp++, G_CYC_FILL);
  gDPSetRenderMode(dlp++, G_RM_NOOP, G_RM_NOOP2);
  gSPClearGeometryMode(dlp++, 0xFFFFFFFF);
}

void gfxBeginTextures(void) {
  beginText();
}

/*
 * Clearing.  Depth first, into the z-buffer aliased as a colour image, then
 * the framebuffer -- both in fill mode, which writes two pixels a cycle.
 */
static void clearBuffers(u16 background) {
  gDPSetDepthImage(dlp++, OS_K0_TO_PHYSICAL(nuGfxZBuffer));
  gDPSetCycleType(dlp++, G_CYC_FILL);
  gDPSetColorImage(dlp++, G_IM_FMT_RGBA, G_IM_SIZ_16b, SCREEN_WD,
    OS_K0_TO_PHYSICAL(nuGfxZBuffer));
  gDPSetFillColor(dlp++,
    (GPACK_ZDZ(G_MAXFBZ, 0) << 16 | GPACK_ZDZ(G_MAXFBZ, 0)));
  gDPFillRectangle(dlp++, 0, 0, SCREEN_WD - 1, SCREEN_HT - 1);
  gDPPipeSync(dlp++);

  gDPSetColorImage(dlp++, G_IM_FMT_RGBA, G_IM_SIZ_16b, SCREEN_WD,
    osVirtualToPhysical(nuGfxCfb_ptr));
  gDPSetFillColor(dlp++, (background << 16 | background));
  gDPFillRectangle(dlp++, 0, 0, SCREEN_WD - 1, SCREEN_HT - 1);
  gDPPipeSync(dlp++);
}

void gfxBeginFrame(u8 clear_r, u8 clear_g, u8 clear_b) {
  frame_start = &frame_display_lists[dl_no][0];
  dlp = frame_start;
  gfxInvalidateTexture();
  gSPDisplayList(dlp++, frame_setup_display_list);
  clearBuffers(GPACK_RGBA5551(clear_r, clear_g, clear_b, 1));
  gSPDisplayList(dlp++, model_setup_display_list);
}

void gfxEndFrame(void) {
  if ((u32) (dlp - frame_start) > frame_command_peak) {
    frame_command_peak = (u32) (dlp - frame_start);
  }

  if (dlp > frame_start + FRAME_DISPLAY_LIST_SIZE - 2) {
    /*
     * Submit a harmless empty frame rather than hand the RSP a list that has
     * already run past its buffer.  assert() is compiled out by -DNDEBUG in a
     * release build, so it cannot be the only thing standing here -- and the
     * failure it would otherwise let through is silent corruption of whatever
     * sits after the buffer, which surfaces as a wandering freeze.
     */
    frame_overflows++;
    diagnostics_visible = TRUE;
    dlp = frame_start;
  }
  gDPFullSync(dlp++);
  gSPEndDisplayList(dlp++);
  assert(dlp - frame_start <= FRAME_DISPLAY_LIST_SIZE);
  nuGfxTaskStart(frame_start, (s32) (dlp - frame_start) * sizeof (Gfx),
    NU_GFX_UCODE_F3DEX, NU_SC_SWAPBUFFER);
  /* The one place a frame is handed to the RSP, and so the only honest place
     to count displayed frames from. */
  diagNoteFrameSubmitted();

  dl_no ^= 1;
}

void initGraphics(void) {
  nuGfxInit();
  /* nuGfxInit registers its own FIFO at the SDK's compile-time size; re-point
     it at ours before any task is submitted. */
  engineSetRDPFifo();
  nuGfxDisplayOn();
}

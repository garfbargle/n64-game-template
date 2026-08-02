#ifndef ENGINE_GFX_H
#define ENGINE_GFX_H

#include <nusys.h>

/*
 * The framebuffer the console actually scans out.  Every UI coordinate in the
 * engine is in these units -- there is no scaling layer, because on this
 * machine there is no spare fill rate to run one.
 */
#define SCREEN_WD 320
#define SCREEN_HT 240

/*
 * The engine supplies its own RDP command FIFO instead of NuSystem's 128 KiB
 * default; see rdp_fifo.c for why, and raise it if a frame ever reports an
 * overflow.  Must be a multiple of 16.
 */
#define ENGINE_RDP_FIFO_SIZE (64 * 1024)
void engineSetRDPFifo(void);

/*
 * One display list per framebuffer: the RSP may still be walking the list that
 * built the frame on screen while the CPU writes the next one.
 *
 * FRAME_DISPLAY_LIST_SIZE is the whole frame's command budget -- world, HUD and
 * everything else share it.  Overrunning it corrupts the frame rather than
 * failing loudly, so gfxCommandsUsed() is worth watching while a game grows;
 * `frame_overflows` counts frames that hit the ceiling.
 */
#define NUM_DISPLAY_LISTS 2
#define FRAME_DISPLAY_LIST_SIZE 6656

extern Gfx *dlp;
extern Gfx frame_display_lists[NUM_DISPLAY_LISTS][FRAME_DISPLAY_LIST_SIZE];
extern u32 dl_no;
extern u32 frame_overflows;

/* Commands written into the frame's list so far, against
   FRAME_DISPLAY_LIST_SIZE. */
u32 gfxCommandsUsed(void);

void initGraphics(void);

/*
 * A frame.  gfxBeginFrame picks this frame's display list, sets up the RCP and
 * clears the colour and depth buffers to `clear_rgb`; gfxEndFrame closes the
 * list and hands it to NuSystem.  Everything a game draws goes between them.
 */
void gfxBeginFrame(u8 clear_r, u8 clear_g, u8 clear_b);
void gfxEndFrame(void);

/*
 * Split screen.  gfxSetViewports(n) lays out 1, 2 or 4 viewports; a game then
 * calls gfxSelectViewport(i) before drawing player i's view.  The viewport
 * also sets the scissor, so UI drawn inside one is clipped to it.
 */
#define GFX_MAX_VIEWPORTS 4
void gfxSetViewports(u8 count);
u8 gfxViewportCount(void);
void gfxSelectViewport(u8 index);
u32 gfxViewportX(u8 index);
u32 gfxViewportY(u8 index);
u32 gfxViewportWidth(void);
u32 gfxViewportHeight(void);
/* The full screen again, for HUD or menus drawn over a split view. */
void gfxSelectFullViewport(void);

/*
 * The perspective the 3D passes are drawn through.  Call once per viewport,
 * before any model matrix.  `fovy` is in degrees; `near`/`far` are in world
 * units.  The far plane also sets where fog lands -- see gfxSetFog.
 */
void gfxLoadPerspective(float fovy, float near, float far);
/* An orthographic pass in screen coordinates, for 2D drawn through the
   triangle pipeline rather than as rectangles. */
void gfxLoadOrtho(void);

/*
 * The camera.  Position and target are in world units; `roll` is degrees.
 * Loads the viewing matrix on top of whichever projection is current.
 */
void gfxLookAt(float eye_x, float eye_y, float eye_z,
  float at_x, float at_y, float at_z, float roll);

/*
 * A part's whole modelview: rotate about the model's own origin, then move it
 * into the world, in one matrix and one gSPMatrix.  Angles are degrees.
 *
 * modelMatrixFrom takes a linear transform already built -- a scale, or a
 * rotation with a scale folded in -- and writes into it, so the caller's
 * scratch must be a local rather than a table it wants to keep.
 *
 * The N64 matrix format is s15.16, so a translation loses sub-unit precision
 * past about +-32000 units of the origin.  A game whose world is larger than
 * that has to keep a render origin and subtract it here; see docs/engine.md.
 */
void modelMatrix(Mtx *out, float pitch, float yaw, float roll, float x,
  float y, float z);
void modelMatrixFrom(Mtx *out, float linear[4][4], float x, float y, float z);

/* Push a built matrix as this draw's modelview. */
void gfxPushMatrix(Mtx *matrix);
void gfxPopMatrix(void);

/*
 * What a 3D pass draws with.
 *
 * gfxBeginTextured points the combiner at the bound texture modulated by
 * shade; gfxBeginShaded points it at shade alone.  Untextured geometry that
 * skips this draws with whatever tile the previous pass left bound, which
 * makes a model's brightness depend on what else happens to be on screen.
 */
void gfxBeginTextured(void);
void gfxBeginShaded(void);

/*
 * Lighting: one directional light plus ambient, which is what the fixed
 * function pipeline gives for free.  `dir` points from the surface toward the
 * light and need not be normalised.
 *
 * Off by default, and worth leaving off.  Without it a vertex's colour slot is
 * its colour, so light baked in at authoring time costs the RSP nothing per
 * frame; with it, that same slot is read as a *normal* and the RSP computes
 * the colour per vertex.  Turn it on for geometry that has to turn under a
 * fixed light or sit under a day/night cycle -- and keep each pass's models to
 * one convention, because geometry authored for one path draws as nonsense
 * under the other.
 */
void gfxSetLighting(u8 on);
void gfxSetLight(u8 ambient_r, u8 ambient_g, u8 ambient_b,
  u8 light_r, u8 light_g, u8 light_b, float dir_x, float dir_y, float dir_z);

/*
 * Distance fog, in screen depth rather than world units: `start` runs from
 * FOG_START_MIN (near) to FOG_START_MAX (at the far plane).  Fog costs the
 * second RDP cycle, so a pass with fog off runs at roughly twice the pixel
 * rate -- switch it off for anything that cannot reach the band.
 */
#define FOG_START_MIN 985
#define FOG_START_MAX 999
void gfxSetFog(u8 enabled, u16 start, u8 r, u8 g, u8 b);

/*
 * Boxes, which is most of what a low-polygon N64 model is made of.
 *
 * `box_display_list` walks a 24-vertex array -- six quads of four, in the
 * order -Z, +Z, -X, +X, +Y, -Y -- so every face carries its own texture
 * coordinates.  `shaded_box_display_list` walks eight shared corners instead:
 * a third of the vertices to transform and no room for per-face texture
 * coordinates, which is the right trade for untextured geometry.
 *
 * ENGINE_SHADED_BOX declares the eight-corner array from a min/max corner pair
 * and two colours, the second being the shaded side -- baking a light into the
 * vertices costs nothing per frame.  Vertex positions are s16, so a model is
 * authored in whatever units its matrix scales from.
 */
extern Gfx box_display_list[];
extern Gfx shaded_box_display_list[];
#define ENGINE_BOX_VERTEX_COUNT 24
#define ENGINE_SHADED_BOX_VERTEX_COUNT 8

#define ENGINE_VERTEX(x, y, z, r, g, b) {x, y, z, 0, 0, 0, r, g, b, 255}

#define ENGINE_SHADED_BOX(name, x0, y0, z0, x1, y1, z1, \
    r1, g1, b1, r2, g2, b2) \
  static Vtx name[] = { \
    ENGINE_VERTEX(x0, y1, z1, r1, g1, b1), \
    ENGINE_VERTEX(x1, y1, z1, r1, g1, b1), \
    ENGINE_VERTEX(x1, y0, z1, r1, g1, b1), \
    ENGINE_VERTEX(x0, y0, z1, r1, g1, b1), \
    ENGINE_VERTEX(x1, y1, z0, r2, g2, b2), \
    ENGINE_VERTEX(x0, y1, z0, r2, g2, b2), \
    ENGINE_VERTEX(x0, y0, z0, r2, g2, b2), \
    ENGINE_VERTEX(x1, y0, z0, r2, g2, b2) \
  }

/*
 * A texture: 16x16, four bits per texel, indexing a 16-entry RGBA5551 palette.
 *
 * That is 160 bytes each, which is why a game can afford a lot of them on a
 * console with 4 MB and no texture streaming -- and it is small enough to sit
 * in the RDP's 4 KB TMEM alongside its palette without tiling.  The type is
 * generated by generate_assets.py into assets/texture_data.h; see
 * docs/custom-textures.md for the art-to-cartridge path.
 */
typedef struct {
  u32 color_indices[32];
  u16 pallet[16];
} __attribute__((aligned(8))) Texture;

/* Binds the palette and the texel block, and skips the work when the texture
   is already the one loaded -- so a pass that draws everything sharing a
   texture together costs one load rather than one per model. */
void gfxLoadTexture(Texture *texture);
/* Clamp instead of wrap, for a texture whose edge must not bleed. */
void gfxLoadTextureClamped(Texture *texture);
/* Drop the cached "already bound" state -- call after anything that changes
   the RDP tile behind the engine's back. */
void gfxInvalidateTexture(void);

/*
 * The RDP mode the 2D passes expect.  A frame's fills and its text cannot be
 * interleaved: swapping the RDP between fill mode and texture mode mid-card is
 * a hardware hazard that locks the console (see docs/hardware.md), so a screen
 * draws all of its fills, then all of its text.  These two calls are that
 * switch, and every UI helper documents which phase it belongs to.
 */
void gfxBeginFills(void);
void gfxBeginTextures(void);

#endif /* ENGINE_GFX_H */

#ifndef ENGINE_TEXT_H
#define ENGINE_TEXT_H

#include <nusys.h>

/*
 * The UI font: an original 8x8 bitmap atlas generated at build time by
 * generate_assets.py into assets/font.h.  Glyphs are proportional -- charWidth
 * is the advance, not the cell -- and the atlas covers ASCII from space up.
 *
 * Everything here is a textures-phase call: the RDP must be in texture mode
 * (gfxBeginTextures) and text may not be interleaved with fill sprites.  See
 * the phase note in gfx.h.
 *
 * The atlas has no lowercase 'i' -- it lands on a colon -- so a mixed-case
 * string containing one reads wrong on the console.  Capitalise those.
 */

/* Bind the font and set text mode.  Called by gfxBeginTextures; call it again
   after anything that binds a different texture mid-phase. */
void beginText(void);

/*
 * Text drawn through the blender rather than straight into the framebuffer.
 *
 * beginText writes the glyph cell whole: an I4 texel of 0 is black, so every
 * word arrives in its own opaque box.  That is invisible on a dark card, but
 * over a live 3D scene the boxes are the only thing on screen that looks
 * pasted on.  This takes the alpha the I format already carries -- the atlas is
 * a hard 0-or-15 bitmap, so it is a clean cutout with nothing to antialias --
 * and leaves memory alone wherever that alpha is zero.
 */
void beginTextBlended(void);

void drawChar(char chr, u32 x, u32 y);
void drawString(const char *text, u32 x, u32 y);
void drawCenteredString(const char *text, u32 y);
/* The same font at a whole-integer scale, for the rare line that has to carry
   a screen on its own -- a title, the word for having died.  A glyph is 8x8
   before scaling and charWidth(c) * scale wide after it. */
void drawLargeString(const char *text, u32 x, u32 y, u8 scale);
void drawCenteredLargeString(const char *text, u32 y, u8 scale);

u32 charWidth(char chr);
u32 stringWidth(const char *text);
u32 largeStringWidth(const char *text, u8 scale);

/* Decimal, no padding.  Returns the x the next glyph would start at, so a
   caller can run a number into a label without measuring it first. */
u32 drawUnsigned(u32 value, u32 x, u32 y);
/* Six-pixel digits, for counts that sit in the corner of an icon. */
void drawCompactDigit(char digit, u32 x, u32 y);

void setTextColor(u8 red, u8 green, u8 blue);

#endif /* ENGINE_TEXT_H */

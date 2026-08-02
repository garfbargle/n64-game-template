#include "text.h"
#include "gfx.h"
#include "font.h"

/*
 * The font atlas: 16 glyphs per row, 8x8 cells, I4 (one nibble of luminance
 * per texel).  ASCII from space up, so a glyph's cell is (c - ' ').  The two
 * blank rows at the top of the 128x64 image are why every lookup adds 2 to the
 * row -- generate_assets.py leaves them for a game that wants to add its own
 * symbols without moving the printable set.
 */
static Gfx text_setup_display_list[] = {
  /*
   * Text can follow 3D primitives, a FILL-mode clear, or a UI panel, and every
   * one of those leaves the RDP mid-pipe with different attributes.  Changing
   * cycle type, render mode, combine mode and the loaded tile while a
   * primitive is still in flight is an RDP hazard: emulators tolerate it,
   * hardware locks up, and whether it bites depends on how busy the pipe still
   * is -- which is why it tracks scene complexity and looks intermittent.
   */
  gsDPPipeSync(),
  gsDPSetCycleType(G_CYC_1CYCLE),
  gsDPSetRenderMode(G_RM_NOOP, G_RM_NOOP2),
  gsDPSetCombineMode(G_CC_MODULATEI_PRIM, G_CC_MODULATEI_PRIM),
  gsDPSetPrimColor(0, 0, 255, 255, 255, 255),
  gsDPSetTexturePersp(G_TP_NONE),
  gsDPSetTextureLUT(G_TT_NONE),
  gsDPLoadTextureTile_4b(font_texture, G_IM_FMT_I, 128, 64,
        0, 0, 32 << 2, 16 << 2,
        0, G_TX_CLAMP, G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK,
        G_TX_NOLOD, G_TX_NOLOD),
  gsSPEndDisplayList()
};

/*
 * The same font, drawn through the blender instead of straight into the
 * framebuffer.
 *
 * The list above writes the glyph cell whole: an I4 texel of 0 is black, so
 * every word arrives in its own opaque box.  That is invisible on a dark card,
 * but over a live 3D scene the boxes are the only thing on screen that looks
 * pasted on.  MODULATEIA takes the alpha the I format already carries -- the
 * atlas is a hard 0-or-15 bitmap, so it is a clean cutout with nothing to
 * antialias -- and XLU_SURF leaves memory alone wherever that alpha is zero.
 * No alpha compare: the blender needs no register the rest of the frame would
 * have to put back.
 */
static Gfx text_blend_display_list[] = {
  gsSPDisplayList(text_setup_display_list),
  /* The tile load above is still in the pipe; the hazard the setup list
     documents applies just as much to a mode change chasing it. */
  gsDPPipeSync(),
  gsDPSetCombineMode(G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM),
  gsDPSetRenderMode(G_RM_XLU_SURF, G_RM_XLU_SURF2),
  gsSPEndDisplayList()
};

void beginText(void) {
  gSPDisplayList(dlp++, text_setup_display_list);
  gfxInvalidateTexture();
}

void beginTextBlended(void) {
  gSPDisplayList(dlp++, text_blend_display_list);
  gfxInvalidateTexture();
}

/*
 * Proportional advances.  The atlas cells are all 8 wide; these are how much
 * of that cell each glyph actually uses, so words do not sit in a monospaced
 * grid with holes around the thin letters.
 */
u32 charWidth(char chr) {
  if (chr == 'i' || chr == ':' || chr == '.' || chr == ' ' ||
      chr == '\'' || chr == ',') {
    return 3;
  } else if (chr == 'l' || chr == '!') {
    return 4;
  } else if (chr == 't') {
    return 5;
  } else if (chr == 'k') {
    return 6;
  } else {
    return 7;
  }
}

void drawChar(char chr, u32 x, u32 y) {
  u8 idx = chr - ' ';
  u32 cx = idx % 16;
  u32 cy = (idx / 16) + 2;

  gSPTextureRectangle(dlp++,
    x << 2, y << 2,
    ((x + 8) << 2) - 2, ((y + 8) << 2) - 2,
    G_TX_RENDERTILE,
    (cx * 8) << 5, (cy * 8) << 5,
    1 << 10, 1 << 10);
}

/* Space advances but draws nothing: an I4 zero is opaque black in the
   unblended mode, so a drawn space would punch a hole in whatever is
   underneath it. */
void drawString(const char *text, u32 x, u32 y) {
  while (*text) {
    if (*text != ' ') {
      drawChar(*text, x, y);
    }
    x += charWidth(*text);
    text++;
  }
}

static void drawLargeChar(char chr, u32 x, u32 y, u8 scale) {
  u8 idx = chr - ' ';
  u32 cx = idx % 16;
  u32 cy = (idx / 16) + 2;

  gSPTextureRectangle(dlp++,
    x << 2, y << 2,
    ((x + 8 * scale) << 2) - 2, ((y + 8 * scale) << 2) - 2,
    G_TX_RENDERTILE,
    (cx * 8) << 5, (cy * 8) << 5,
    (1 << 10) / scale, (1 << 10) / scale);
}

void drawLargeString(const char *text, u32 x, u32 y, u8 scale) {
  while (*text) {
    if (*text != ' ') {
      drawLargeChar(*text, x, y, scale);
    }
    x += charWidth(*text) * scale;
    text++;
  }
}

u32 stringWidth(const char *text) {
  u32 width = 0;

  while (*text) {
    width += charWidth(*text);
    text++;
  }
  return width;
}

u32 largeStringWidth(const char *text, u8 scale) {
  return stringWidth(text) * scale;
}

void drawCenteredString(const char *text, u32 y) {
  drawString(text, (SCREEN_WD - stringWidth(text)) / 2, y);
}

void drawCenteredLargeString(const char *text, u32 y, u8 scale) {
  drawLargeString(text, (SCREEN_WD - largeStringWidth(text, scale)) / 2, y,
    scale);
}

/* Six-pixel digits, for counts that sit against the rim of an icon rather than
   masking it. */
void drawCompactDigit(char digit, u32 x, u32 y) {
  u8 index = digit - ' ';
  u32 source_x = index % 16;
  u32 source_y = (index / 16) + 2;

  gSPTextureRectangle(dlp++,
    x << 2, y << 2,
    ((x + 6) << 2) - 2, ((y + 6) << 2) - 2,
    G_TX_RENDERTILE,
    (source_x * 8) << 5, (source_y * 8) << 5,
    (8 << 10) / 6, (8 << 10) / 6);
}

u32 drawUnsigned(u32 value, u32 x, u32 y) {
  char digits[10];
  u8 count = 0;
  u8 i;

  do {
    digits[count++] = '0' + value % 10;
    value /= 10;
  } while (value > 0 && count < sizeof (digits));
  for (i = 0; i < count; i++) {
    char digit = digits[count - i - 1];

    drawChar(digit, x, y);
    x += charWidth(digit);
  }
  return x;
}

void setTextColor(u8 red, u8 green, u8 blue) {
  gDPSetPrimColor(dlp++, 0, 0, red, green, blue, 255);
}

#ifndef GFX_H
#define GFX_H
#include <stdint.h>
#include "fonts.h"

#define RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

// Palette
#define C_BG       RGB(16, 20, 28)
#define C_PANEL    RGB(34, 40, 52)
#define C_BUTTON   RGB(52, 62, 80)
#define C_PRESSED  RGB(90, 110, 140)
#define C_TEXT     RGB(235, 238, 242)
#define C_DIM      RGB(140, 150, 165)
#define C_ACCENT   RGB(64, 156, 255)
#define C_GREEN    RGB(46, 184, 92)
#define C_YELLOW   RGB(240, 190, 40)
#define C_ORANGE   RGB(245, 130, 40)
#define C_RED      RGB(220, 60, 60)
#define C_BLACK    RGB(0, 0, 0)
#define C_WHITE    RGB(255, 255, 255)

enum { ALIGN_LEFT, ALIGN_CENTER, ALIGN_RIGHT };

void gfx_fill(int x, int y, int w, int h, uint16_t c);
void gfx_frame(int x, int y, int w, int h, int t, uint16_t c);
void gfx_round(int x, int y, int w, int h, int r, uint16_t c, uint16_t bg);
// Draws text with an opaque background (no flicker on redraw). Returns width drawn.
int gfx_text(const font_t * f, int x, int y, const char * s, uint16_t fg, uint16_t bg);
int gfx_text_width(const font_t * f, const char * s);
// Text inside a box, vertically centred, box background filled.
void gfx_text_box(const font_t * f, int x, int y, int w, int h, int align, const char * s, uint16_t fg, uint16_t bg);

#endif

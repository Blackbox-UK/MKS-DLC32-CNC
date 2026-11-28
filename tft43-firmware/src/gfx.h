#ifndef GFX_H
#define GFX_H
#include <stdint.h>
#include <stdbool.h>
#include "fonts.h"

#define RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

// Palette: dark steel with shaded controls
#define C_BG       RGB(18, 22, 30)
#define C_BAR      RGB(40, 48, 62)
#define C_PANEL    RGB(30, 36, 48)
#define C_BUTTON   RGB(62, 74, 96)
#define C_TAB      RGB(44, 52, 68)
#define C_TEXT     RGB(236, 240, 245)
#define C_DIM      RGB(140, 152, 170)
#define C_ACCENT   RGB(56, 150, 250)
#define C_GREEN    RGB(40, 175, 90)
#define C_YELLOW   RGB(240, 190, 40)
#define C_ORANGE   RGB(236, 125, 40)
#define C_RED      RGB(212, 56, 56)
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
// Shaded ("less flat") drawing
uint16_t gfx_mix(uint16_t a, uint16_t b, int t);  // t = 0..255: a -> b
uint16_t gfx_lighten(uint16_t c, int amount);     // amount 0..255 towards white
uint16_t gfx_darken(uint16_t c, int amount);      // amount 0..255 towards black
void gfx_vgradient(int x, int y, int w, int h, uint16_t top, uint16_t bottom);
// Rounded box with a vertical gradient, top highlight, dark rim and a soft drop shadow.
void gfx_bevel(int x, int y, int w, int h, int r, uint16_t base, uint16_t bg, bool pressed);
void gfx_bevel_face(uint16_t base, bool pressed, uint16_t * top, uint16_t * bottom);  // face colours of gfx_bevel
void gfx_inset_face(uint16_t base, uint16_t * top, uint16_t * bottom);  // face colours of gfx_inset
// Recessed panel (cards and readouts).
void gfx_inset(int x, int y, int w, int h, int r, uint16_t base, uint16_t bg);
// 24x24 4-bit icon, blended onto a vertical gradient so it matches gfx_bevel faces.
void gfx_icon(int icon, int x, int y, uint16_t fg, uint16_t bg_top, uint16_t bg_bottom, int face_y, int face_h);
// Text over a gradient face (same arguments as the face it sits on).
void gfx_text_on(const font_t * f, int x, int y, const char * s, uint16_t fg, uint16_t top, uint16_t bottom, int face_y, int face_h);

// gfx_text_box on a gradient face that spans face_y..face_y+face_h
void gfx_text_box_g(const font_t * f, int x, int y, int w, int h, int align, const char * s, uint16_t fg,
                    uint16_t top, uint16_t bottom, int face_y, int face_h);
void gfx_text_box(const font_t * f, int x, int y, int w, int h, int align, const char * s, uint16_t fg, uint16_t bg);

#endif

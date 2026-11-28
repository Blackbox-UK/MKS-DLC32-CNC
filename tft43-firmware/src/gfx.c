#include <string.h>
#include "board.h"
#include "lcd.h"
#include "gfx.h"

static int clip(int * x, int * y, int * w, int * h)
{
  if (*x < 0) { *w += *x; *x = 0; }
  if (*y < 0) { *h += *y; *y = 0; }
  if (*x + *w > LCD_WIDTH) *w = LCD_WIDTH - *x;
  if (*y + *h > LCD_HEIGHT) *h = LCD_HEIGHT - *y;
  return *w > 0 && *h > 0;
}

void gfx_fill(int x, int y, int w, int h, uint16_t c)
{
  if (!clip(&x, &y, &w, &h)) return;
  lcd_window(x, y, x + w - 1, y + h - 1);
  lcd_fill_n(c, (uint32_t)w * h);
}

void gfx_frame(int x, int y, int w, int h, int t, uint16_t c)
{
  gfx_fill(x, y, w, t, c);
  gfx_fill(x, y + h - t, w, t, c);
  gfx_fill(x, y + t, t, h - 2 * t, c);
  gfx_fill(x + w - t, y + t, t, h - 2 * t, c);
}

void gfx_round(int x, int y, int w, int h, int r, uint16_t c, uint16_t bg)
{
  // Rounded rectangle: corner rows inset by a circle profile.
  for (int i = 0; i < r; i++) {
    int dy = r - i;
    int inset = 0;
    while ((r - inset) * (r - inset) + dy * dy > r * r + r) inset++;
    gfx_fill(x, y + i, inset, 1, bg);
    gfx_fill(x + inset, y + i, w - 2 * inset, 1, c);
    gfx_fill(x + w - inset, y + i, inset, 1, bg);
    gfx_fill(x, y + h - 1 - i, inset, 1, bg);
    gfx_fill(x + inset, y + h - 1 - i, w - 2 * inset, 1, c);
    gfx_fill(x + w - inset, y + h - 1 - i, inset, 1, bg);
  }
  gfx_fill(x, y + r, w, h - 2 * r, c);
}

static const uint8_t * glyph(const font_t * f, char ch)
{
  const char * p = memchr(f->chars, ch, f->count);
  if (!p) p = memchr(f->chars, ' ', f->count);
  if (!p) return 0;
  return f->data + (uint32_t)(p - f->chars) * (f->w * f->h / 2);
}

static uint16_t blend(uint16_t fg, uint16_t bg, uint8_t a)  // a = 0..15
{
  uint32_t rb = ((fg & 0xF81F) * a + (bg & 0xF81F) * (15 - a)) / 15;
  uint32_t g = ((fg & 0x07E0) * a + (bg & 0x07E0) * (15 - a)) / 15;
  return (uint16_t)((rb & 0xF81F) | (g & 0x07E0));
}

int gfx_text_width(const font_t * f, const char * s)
{
  return (int)strlen(s) * f->w;
}

int gfx_text(const font_t * f, int x, int y, const char * s, uint16_t fg, uint16_t bg)
{
  uint16_t lut[16];
  for (int i = 0; i < 16; i++) lut[i] = blend(fg, bg, i);

  int x0 = x;
  for (; *s; s++, x += f->w) {
    if (x < 0 || y < 0 || x + f->w > LCD_WIDTH || y + f->h > LCD_HEIGHT) continue;
    const uint8_t * g = glyph(f, *s);
    lcd_window(x, y, x + f->w - 1, y + f->h - 1);
    if (!g) { lcd_fill_n(bg, (uint32_t)f->w * f->h); continue; }
    uint32_t n = (uint32_t)f->w * f->h;
    for (uint32_t i = 0; i < n; i += 2) {
      uint8_t b = *g++;
      lcd_pixel(lut[b >> 4]);
      if (i + 1 < n) lcd_pixel(lut[b & 15]);
    }
  }
  return x - x0;
}

void gfx_text_box(const font_t * f, int x, int y, int w, int h, int align, const char * s, uint16_t fg, uint16_t bg)
{
  int tw = gfx_text_width(f, s);
  if (tw > w) tw = w;
  int tx = (align == ALIGN_LEFT) ? x : (align == ALIGN_RIGHT) ? x + w - tw : x + (w - tw) / 2;
  int ty = y + (h - f->h) / 2;
  gfx_fill(x, y, tx - x, h, bg);
  gfx_fill(tx + tw, y, x + w - tx - tw, h, bg);
  gfx_fill(tx, y, tw, ty - y, bg);
  gfx_fill(tx, ty + f->h, tw, y + h - ty - f->h, bg);
  // draw only the characters that fit
  char buf[64];
  int maxc = w / f->w;
  int n = (int)strlen(s);
  if (n > maxc) n = maxc;
  if (n > (int)sizeof(buf) - 1) n = sizeof(buf) - 1;
  memcpy(buf, s, n);
  buf[n] = 0;
  gfx_text(f, tx, ty, buf, fg, bg);
}

// ---------------------------------------------------------------- shading
#include "icons.h"

uint16_t gfx_mix(uint16_t a, uint16_t b, int t)
{
  int ar = a >> 11, ag = (a >> 5) & 63, ab = a & 31;
  int br = b >> 11, bg = (b >> 5) & 63, bb = b & 31;
  int r = ar + ((br - ar) * t + 127) / 255;
  int g = ag + ((bg - ag) * t + 127) / 255;
  int bl = ab + ((bb - ab) * t + 127) / 255;
  return (uint16_t)((r << 11) | (g << 5) | bl);
}

uint16_t gfx_lighten(uint16_t c, int amount) { return gfx_mix(c, 0xFFFF, amount); }
uint16_t gfx_darken(uint16_t c, int amount) { return gfx_mix(c, 0x0000, amount); }

static uint16_t grad_at(uint16_t top, uint16_t bottom, int y, int y0, int h)
{
  if (h <= 1) return top;
  int t = (y - y0) * 255 / (h - 1);
  if (t < 0) t = 0;
  if (t > 255) t = 255;
  return gfx_mix(top, bottom, t);
}

void gfx_vgradient(int x, int y, int w, int h, uint16_t top, uint16_t bottom)
{
  for (int i = 0; i < h; i++) gfx_fill(x, y + i, w, 1, grad_at(top, bottom, y + i, y, h));
}

// corner inset for row i of a radius-r rounded rectangle
static int corner_inset(int r, int i)
{
  if (i >= r) return 0;
  int dy = r - i;
  int inset = 0;
  while ((r - inset) * (r - inset) + dy * dy > r * r + r) inset++;
  return inset;
}

static void round_rows(int x, int y, int w, int h, int r, uint16_t top, uint16_t bottom, uint16_t bg,
                       uint16_t rim_top, uint16_t rim_bottom, bool rim)
{
  for (int i = 0; i < h; i++) {
    int in = corner_inset(r, i);
    int in2 = corner_inset(r, h - 1 - i);
    if (in2 > in) in = in2;
    uint16_t c = grad_at(top, bottom, y + i, y, h);
    gfx_fill(x, y + i, in, 1, bg);
    gfx_fill(x + w - in, y + i, in, 1, bg);
    if (rim && (i == 0 || i == h - 1)) {
      gfx_fill(x + in, y + i, w - 2 * in, 1, i == 0 ? rim_top : rim_bottom);
    } else if (rim) {
      uint16_t rc = grad_at(rim_top, rim_bottom, y + i, y, h);
      gfx_fill(x + in, y + i, 1, 1, rc);
      gfx_fill(x + in + 1, y + i, w - 2 * in - 2, 1, c);
      gfx_fill(x + w - in - 1, y + i, 1, 1, rc);
    } else {
      gfx_fill(x + in, y + i, w - 2 * in, 1, c);
    }
  }
}

void gfx_bevel_face(uint16_t base, bool pressed, uint16_t * top, uint16_t * bottom)
{
  *top = pressed ? gfx_darken(base, 60) : gfx_lighten(base, 70);
  *bottom = pressed ? gfx_lighten(base, 20) : gfx_darken(base, 55);
}

void gfx_bevel(int x, int y, int w, int h, int r, uint16_t base, uint16_t bg, bool pressed)
{
  // drop shadow: 2 px below, softened
  gfx_fill(x + 2, y + h, w - 2, 1, gfx_darken(bg, 110));
  gfx_fill(x + 3, y + h + 1, w - 4, 1, gfx_darken(bg, 50));
  uint16_t top, bottom;
  gfx_bevel_face(base, pressed, &top, &bottom);
  uint16_t rim_top = pressed ? gfx_darken(base, 120) : gfx_lighten(base, 140);
  uint16_t rim_bottom = gfx_darken(base, 130);
  round_rows(x, y, w, h, r, top, bottom, bg, rim_top, rim_bottom, true);
}

void gfx_inset_face(uint16_t base, uint16_t * top, uint16_t * bottom)
{
  *top = gfx_darken(base, 40);
  *bottom = gfx_lighten(base, 12);
}

void gfx_inset(int x, int y, int w, int h, int r, uint16_t base, uint16_t bg)
{
  uint16_t t, b;
  gfx_inset_face(base, &t, &b);
  round_rows(x, y, w, h, r, t, b, bg, gfx_darken(base, 140),
             gfx_lighten(base, 60), true);
}

void gfx_icon(int icon, int x, int y, uint16_t fg, uint16_t bg_top, uint16_t bg_bottom, int face_y, int face_h)
{
  if (icon < 0 || icon >= ICON_COUNT) return;
  const uint8_t * d = icon_data[icon];
  lcd_window(x, y, x + ICON_SIZE - 1, y + ICON_SIZE - 1);
  for (int row = 0; row < ICON_SIZE; row++) {
    uint16_t bg = grad_at(bg_top, bg_bottom, y + row, face_y, face_h);
    for (int col = 0; col < ICON_SIZE; col += 2) {
      uint8_t b = *d++;
      lcd_pixel(blend(fg, bg, b >> 4));
      lcd_pixel(blend(fg, bg, b & 15));
    }
  }
}

void gfx_text_on(const font_t * f, int x, int y, const char * s, uint16_t fg, uint16_t top, uint16_t bottom, int face_y, int face_h)
{
  for (; *s; s++, x += f->w) {
    if (x < 0 || y < 0 || x + f->w > LCD_WIDTH || y + f->h > LCD_HEIGHT) continue;
    const uint8_t * g = glyph(f, *s);
    lcd_window(x, y, x + f->w - 1, y + f->h - 1);
    for (int row = 0; row < f->h; row++) {
      uint16_t bg = grad_at(top, bottom, y + row, face_y, face_h);
      for (int col = 0; col < f->w; col++) {
        int i = row * f->w + col;
        uint8_t a = g ? ((i & 1) ? (g[i >> 1] & 15) : (g[i >> 1] >> 4)) : 0;
        lcd_pixel(a ? blend(fg, bg, a) : bg);
      }
    }
  }
}

void gfx_text_box_g(const font_t * f, int x, int y, int w, int h, int align, const char * s, uint16_t fg,
                    uint16_t top, uint16_t bottom, int face_y, int face_h)
{
  for (int i = 0; i < h; i++) gfx_fill(x, y + i, w, 1, grad_at(top, bottom, y + i, face_y, face_h));
  char buf[64];
  int maxc = w / f->w;
  int n = (int)strlen(s);
  if (n > maxc) n = maxc;
  if (n > (int)sizeof(buf) - 1) n = sizeof(buf) - 1;
  memcpy(buf, s, n);
  buf[n] = 0;
  int tw = n * f->w;
  int tx = (align == ALIGN_LEFT) ? x : (align == ALIGN_RIGHT) ? x + w - tw : x + (w - tw) / 2;
  gfx_text_on(f, tx, y + (h - f->h) / 2, buf, fg, top, bottom, face_y, face_h);
}

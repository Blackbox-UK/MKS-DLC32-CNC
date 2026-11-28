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

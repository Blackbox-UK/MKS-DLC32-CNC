#ifndef FONTS_H
#define FONTS_H
#include <stdint.h>

// Monospace, 4 bits per pixel (two pixels per byte, high nibble first).
typedef struct {
  uint8_t w, h;          // cell size in pixels
  uint8_t count;         // number of glyphs
  const char * chars;    // characters in glyph order
  const uint8_t * data;  // count * w * h / 2 bytes
} font_t;

extern const font_t font_small;   //  9 x 18, ASCII
extern const font_t font_medium;  // 13 x 26, ASCII
extern const font_t font_big;     // 26 x 46, digits . - + X Y Z

#endif

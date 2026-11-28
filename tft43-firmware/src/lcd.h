#ifndef LCD_H
#define LCD_H
#include <stdint.h>

// Low-level panel access. lcd_stm32.c drives the SSD1963 over FSMC;
// sim/lcd_sim.c draws into a framebuffer for screenshots on a PC.
void lcd_init(void);
void lcd_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);  // inclusive, then write pixels
void lcd_pixel(uint16_t rgb565);
void lcd_fill_n(uint16_t rgb565, uint32_t n);

#endif

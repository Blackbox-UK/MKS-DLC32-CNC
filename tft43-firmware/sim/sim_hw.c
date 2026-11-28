// PC stand-ins for the hardware layer, so ui.c / grbl.c / gfx.c run unchanged.
#include <stdio.h>
#include <string.h>
#include "board.h"
#include "hw.h"
#include "lcd.h"
#include "uart.h"

// ---- time ----
uint32_t sim_ms = 1000;
void hw_init(void) {}
uint32_t millis(void) { return sim_ms; }
void delay_us(uint32_t us) { (void)us; }
void delay_ms(uint32_t ms) { sim_ms += ms; }
void beep(uint16_t ms) { (void)ms; }
void gpio_mode(uint8_t p, uint8_t m, uint8_t a) { (void)p; (void)m; (void)a; }
void gpio_set(uint8_t p, bool l) { (void)p; (void)l; }
bool gpio_get(uint8_t p) { (void)p; return 1; }

// ---- display ----
uint16_t fb[LCD_HEIGHT][LCD_WIDTH];
static int wx0, wy0, wx1, wy1, cx, cy;
void lcd_init(void) {}
void lcd_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
  wx0 = x0; wy0 = y0; wx1 = x1; wy1 = y1; cx = x0; cy = y0;
}
void lcd_pixel(uint16_t c)
{
  if (cx < LCD_WIDTH && cy < LCD_HEIGHT) fb[cy][cx] = c;
  if (++cx > wx1) { cx = wx0; if (++cy > wy1) cy = wy0; }
}
void lcd_fill_n(uint16_t c, uint32_t n) { while (n--) lcd_pixel(c); }

void sim_save_ppm(const char * path)
{
  FILE * f = fopen(path, "wb");
  fprintf(f, "P6\n%d %d\n255\n", LCD_WIDTH, LCD_HEIGHT);
  for (int y = 0; y < LCD_HEIGHT; y++)
    for (int x = 0; x < LCD_WIDTH; x++) {
      uint16_t c = fb[y][x];
      unsigned char rgb[3] = {(unsigned char)((c >> 8) & 0xF8), (unsigned char)((c >> 3) & 0xFC), (unsigned char)((c << 3) & 0xF8)};
      fwrite(rgb, 1, 3, f);
    }
  fclose(f);
}

// ---- serial ----
static char rx[8192];
static int rxh, rxt;
char sim_tx[8192];
int sim_txn;
void uart_init(uint32_t baud) { (void)baud; }
int uart_getc(void) { return rxt == rxh ? -1 : (unsigned char)rx[rxt++]; }
void uart_putc(uint8_t c) { if (sim_txn < (int)sizeof(sim_tx) - 1) sim_tx[sim_txn++] = c; sim_tx[sim_txn] = 0; }
void uart_puts(const char * s) { while (*s) uart_putc(*s++); }
void sim_rx(const char * s) { size_t n = strlen(s); memcpy(rx + rxh, s, n); rxh += n; }

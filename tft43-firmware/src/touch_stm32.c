// XPT2046 resistive touch on bit-banged SPI, as in BTT Hal/xpt2046.c and
// API/Touch_Screen.c. The calibration is stored in the same place and format
// as the BTT firmware (flash sector 1, sign 0x20200512, 7 big-endian words), so a
// screen already calibrated under the stock firmware works straight away.
#include <stdio.h>
#include "stm32f2xx.h"
#include "board.h"
#include "hw.h"
#include "gfx.h"
#include "touch.h"

#define CMD_RDX 0xD0
#define CMD_RDY 0x90
#define TSC_SIGN 0x20200512u

static int32_t cal[7];  // A B C D E F K: x = (A*tx + B*ty + C) / K, y = (D*tx + E*ty + F) / K
static bool cal_ok;

static uint32_t be_word(const uint8_t * p) { return ((uint32_t)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }

static void load_calibration(void)
{
  const uint8_t * p = (const uint8_t *)PARAM_ADDR;
  if (be_word(p + 4) != TSC_SIGN) return;
  for (int i = 0; i < 7; i++) cal[i] = (int32_t)be_word(p + 8 + 4 * i);
  cal_ok = cal[6] != 0;
}

static void save_calibration(void)
{
  uint8_t buf[40] = {0};
  uint32_t words[8] = {TSC_SIGN};
  for (int i = 0; i < 7; i++) words[i + 1] = (uint32_t)cal[i];
  for (int i = 0; i < 8; i++) {
    buf[4 + 4 * i] = words[i] >> 24; buf[5 + 4 * i] = words[i] >> 16;
    buf[6 + 4 * i] = words[i] >> 8;  buf[7 + 4 * i] = words[i];
  }
  __disable_irq();
  FLASH_Unlock();
  FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR | FLASH_FLAG_PGAERR |
                  FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);
  if (FLASH_EraseSector(FLASH_Sector_1, VoltageRange_3) == FLASH_COMPLETE) {
    for (unsigned i = 0; i < sizeof(buf); i += 4) {
      uint32_t w = buf[i] | (buf[i + 1] << 8) | (buf[i + 2] << 16) | ((uint32_t)buf[i + 3] << 24);
      FLASH_ProgramWord(PARAM_ADDR + i, w);
    }
  }
  FLASH_Lock();
  __enable_irq();
}

// Same 32-bit wrap-around arithmetic as the BTT firmware, so stored values behave identically.
static void apply_cal(uint16_t tx, uint16_t ty, int * x, int * y)
{
  uint32_t sx = (uint32_t)cal[0] * tx + (uint32_t)cal[1] * ty + (uint32_t)cal[2];
  uint32_t sy = (uint32_t)cal[3] * tx + (uint32_t)cal[4] * ty + (uint32_t)cal[5];
  *x = (int32_t)sx / cal[6];
  *y = (int32_t)sy / cal[6];
}

// ---- XPT2046 ----
static void spi_delay(void) { delay_us(2); }

static uint8_t spi_byte(uint8_t d)  // SPI mode 3, MSB first (BTT uses _SPI_MODE3)
{
  uint8_t r = 0;
  for (int i = 0; i < 8; i++) {
    gpio_set(PIN_TP_MOSI, d & 0x80);
    d <<= 1;
    gpio_set(PIN_TP_SCK, 0); spi_delay();
    r = (r << 1) | gpio_get(PIN_TP_MISO);
    gpio_set(PIN_TP_SCK, 1); spi_delay();
  }
  return r;
}

static uint16_t read_ad(uint8_t cmd)
{
  gpio_set(PIN_TP_CS, 0);
  spi_byte(cmd);
  uint16_t v = spi_byte(0xFF) << 8;
  v |= spi_byte(0xFF);
  gpio_set(PIN_TP_CS, 1);
  return v >> 4;
}

static uint16_t read_avg(uint8_t cmd)  // 5 samples, drop min and max
{
  uint16_t b[5];
  for (int i = 0; i < 5; i++) b[i] = read_ad(cmd);
  for (int i = 0; i < 4; i++)
    for (int j = i + 1; j < 5; j++)
      if (b[i] > b[j]) { uint16_t t = b[i]; b[i] = b[j]; b[j] = t; }
  return (b[1] + b[2] + b[3]) / 3;
}

static bool read_raw(uint16_t * tx, uint16_t * ty)
{
  if (!touch_pen_down()) return false;
  uint16_t x1 = read_avg(CMD_RDX), y1 = read_avg(CMD_RDY);
  uint16_t x2 = read_avg(CMD_RDX), y2 = read_avg(CMD_RDY);
  if (!touch_pen_down()) return false;
  if ((x1 > x2 ? x1 - x2 : x2 - x1) > 50 || (y1 > y2 ? y1 - y2 : y2 - y1) > 50) return false;
  *tx = (x1 + x2) / 2;
  *ty = (y1 + y2) / 2;
  return true;
}

void touch_init(void)
{
  gpio_mode(PIN_TP_PEN, GPIO_IN_PU, 0);
  gpio_set(PIN_TP_CS, 1);
  gpio_mode(PIN_TP_CS, GPIO_OUT, 0);
  gpio_mode(PIN_TP_SCK, GPIO_OUT, 0);
  gpio_mode(PIN_TP_MOSI, GPIO_OUT, 0);
  gpio_mode(PIN_TP_MISO, GPIO_IN, 0);
  load_calibration();
}

bool touch_has_calibration(void) { return cal_ok; }
bool touch_pen_down(void) { return gpio_get(PIN_TP_PEN) == 0; }

bool touch_read(int * x, int * y)
{
  uint16_t tx, ty;
  if (!cal_ok || !read_raw(&tx, &ty)) return false;
  apply_cal(tx, ty, x, y);
  return true;
}

static void crosshair(int x, int y, uint16_t c)
{
  gfx_fill(x - 12, y - 1, 25, 3, c);
  gfx_fill(x - 1, y - 12, 3, 25, c);
}

static void wait_tap(uint16_t * tx, uint16_t * ty)
{
  for (;;) {
    while (!touch_pen_down()) {}
    delay_ms(30);
    if (read_raw(tx, ty)) break;
  }
  beep(40);
  while (touch_pen_down()) {}
  delay_ms(100);
}

void touch_calibrate(void)
{
  const int64_t lx[3] = {40, LCD_WIDTH - 40, LCD_WIDTH - 40};
  const int64_t ly[3] = {40, 40, LCD_HEIGHT - 40};
  for (;;) {
    int64_t px[3], py[3];
    gfx_fill(0, 0, LCD_WIDTH, LCD_HEIGHT, C_BG);
    gfx_text_box(&font_medium, 0, 90, LCD_WIDTH, 30, ALIGN_CENTER, "Touch calibration", C_TEXT, C_BG);
    gfx_text_box(&font_small, 0, 125, LCD_WIDTH, 24, ALIGN_CENTER, "Tap the centre of each cross", C_DIM, C_BG);
    for (int i = 0; i < 3; i++) {
      crosshair(lx[i], ly[i], C_YELLOW);
      uint16_t tx, ty;
      wait_tap(&tx, &ty);
      px[i] = tx; py[i] = ty;
      crosshair(lx[i], ly[i], C_BG);
    }
    int64_t K = (px[0] - px[2]) * (py[1] - py[2]) - (px[1] - px[2]) * (py[0] - py[2]);
    if (K == 0) continue;
    cal[0] = (lx[0] - lx[2]) * (py[1] - py[2]) - (lx[1] - lx[2]) * (py[0] - py[2]);
    cal[1] = (px[0] - px[2]) * (lx[1] - lx[2]) - (lx[0] - lx[2]) * (px[1] - px[2]);
    cal[2] = py[0] * (px[2] * lx[1] - px[1] * lx[2]) + py[1] * (px[0] * lx[2] - px[2] * lx[0]) +
             py[2] * (px[1] * lx[0] - px[0] * lx[1]);
    cal[3] = (ly[0] - ly[2]) * (py[1] - py[2]) - (ly[1] - ly[2]) * (py[0] - py[2]);
    cal[4] = (px[0] - px[2]) * (ly[1] - ly[2]) - (ly[0] - ly[2]) * (px[1] - px[2]);
    cal[5] = py[0] * (px[2] * ly[1] - px[1] * ly[2]) + py[1] * (px[0] * ly[2] - px[2] * ly[0]) +
             py[2] * (px[1] * ly[0] - px[0] * ly[1]);
    cal[6] = (int32_t)K;
    cal_ok = true;

    // check with a fourth point in the centre
    int cx = LCD_WIDTH / 2, cy = LCD_HEIGHT / 2 + 50;
    gfx_fill(0, 90, LCD_WIDTH, 60, C_BG);
    gfx_text_box(&font_small, 0, 100, LCD_WIDTH, 24, ALIGN_CENTER, "Tap the cross to check", C_DIM, C_BG);
    crosshair(cx, cy, C_GREEN);
    uint16_t tx, ty;
    wait_tap(&tx, &ty);
    int x, y;
    apply_cal(tx, ty, &x, &y);
    if (x > cx - 15 && x < cx + 15 && y > cy - 15 && y < cy + 15) break;
    gfx_text_box(&font_medium, 0, 100, LCD_WIDTH, 30, ALIGN_CENTER, "Missed, let's try again", C_RED, C_BG);
    delay_ms(1200);
  }
  save_calibration();
}

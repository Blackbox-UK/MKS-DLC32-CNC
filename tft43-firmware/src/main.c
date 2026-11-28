// TFT43 Grbl pendant: firmware for the BigTreeTech TFT43 V3.0 that talks to a
// Grbl controller (MKS DLC32 / Grbl_Esp32) over the TFT serial header.
#include "board.h"
#include "hw.h"
#include "lcd.h"
#include "gfx.h"
#include "touch.h"
#include "uart.h"
#include "grbl.h"
#include "ui.h"

static void splash(void)
{
  gfx_fill(0, 0, LCD_WIDTH, LCD_HEIGHT, C_BG);
  gfx_text_box(&font_medium, 0, 100, LCD_WIDTH, 30, ALIGN_CENTER, "TFT43 Grbl", C_TEXT, C_BG);
  gfx_text_box(&font_small, 0, 134, LCD_WIDTH, 22, ALIGN_CENTER, "v" FW_VERSION "  hold the screen now to recalibrate", C_DIM, C_BG);
}

int main(void)
{
  hw_init();
  lcd_init();
  touch_init();
  splash();

  // Calibrate on first start, or when the screen is held during the splash.
  uint32_t t0 = millis();
  bool held = false;
  while (millis() - t0 < 1200) if (touch_pen_down()) held = true;
  if (!touch_has_calibration() || held) {
    while (touch_pen_down()) {}
    touch_calibrate();
  }

  uart_init(GRBL_BAUD);
  ui_init();

  uint32_t last_touch = 0;
  bool down = false;
  int x = 0, y = 0;
  for (;;) {
    grbl_poll();
    uint32_t now = millis();
    if (now - last_touch >= 10) {
      last_touch = now;
      int tx, ty;
      if (touch_read(&tx, &ty)) { down = true; x = tx; y = ty; }
      else if (!touch_pen_down()) down = false;  // keep "down" through a noisy sample
    }
    ui_step(down, x, y);
    if (ui_request_calibration) {
      ui_request_calibration = false;
      touch_calibrate();
      ui_show(SCR_DRO);
    }
  }
}

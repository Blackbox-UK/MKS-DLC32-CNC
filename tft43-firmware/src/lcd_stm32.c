// SSD1963 over FSMC bank 1 (16-bit). Init sequence and FSMC timing follow the
// BTT TouchScreenFirmware (Hal/LCD_Driver/SSD1963.c, Hal/stm32f2_f4xx/lcd.c).
#include "stm32f2xx.h"
#include "board.h"
#include "hw.h"
#include "lcd.h"

typedef struct { volatile uint16_t REG; volatile uint16_t RAM; } lcd_bus_t;
#define LCD_BUS ((lcd_bus_t *)(0x60000000u | 0x00FFFFFEu))  // RS on FSMC_A23 (PE2)
#define WR_REG(v)  (LCD_BUS->REG = (v))
#define WR_DATA(v) (LCD_BUS->RAM = (v))

#define HT  (LCD_WIDTH + SSD_HOR_BACK_PORCH + SSD_HOR_FRONT_PORCH)
#define HPS (SSD_HOR_BACK_PORCH)
#define VT  (LCD_HEIGHT + SSD_VER_BACK_PORCH + SSD_VER_FRONT_PORCH)
#define VPS (SSD_VER_BACK_PORCH)

static void fsmc_init(void)
{
  static const uint8_t pins[] = {
    PIN('D', 0), PIN('D', 1), PIN('D', 8), PIN('D', 9), PIN('D', 10), PIN('D', 14), PIN('D', 15),
    PIN('E', 7), PIN('E', 8), PIN('E', 9), PIN('E', 10), PIN('E', 11), PIN('E', 12), PIN('E', 13),
    PIN('E', 14), PIN('E', 15),
    PIN('D', 4) /* NOE */, PIN('D', 5) /* NWE */, PIN('E', 2) /* A23 = RS */, PIN('D', 7) /* NE1 */,
  };
  for (unsigned i = 0; i < sizeof(pins); i++) gpio_mode(pins[i], GPIO_AF, 12);  // AF12 = FSMC

  RCC->AHB3ENR |= RCC_AHB3ENR_FSMCEN;

  FSMC_NORSRAMTimingInitTypeDef rt = {0}, wt = {0};
  rt.FSMC_AddressSetupTime = 0x01;
  rt.FSMC_DataSetupTime = 0x0f;
  rt.FSMC_AccessMode = FSMC_AccessMode_A;
  wt.FSMC_AddressSetupTime = 0x00;
  wt.FSMC_DataSetupTime = LCD_FSMC_DATASET;
  wt.FSMC_AccessMode = FSMC_AccessMode_A;

  FSMC_NORSRAMInitTypeDef s = {0};
  s.FSMC_Bank = FSMC_Bank1_NORSRAM1;
  s.FSMC_DataAddressMux = FSMC_DataAddressMux_Disable;
  s.FSMC_MemoryType = FSMC_MemoryType_NOR;
  s.FSMC_MemoryDataWidth = FSMC_MemoryDataWidth_16b;
  s.FSMC_BurstAccessMode = FSMC_BurstAccessMode_Disable;
  s.FSMC_WaitSignalPolarity = FSMC_WaitSignalPolarity_Low;
  s.FSMC_WrapMode = FSMC_WrapMode_Disable;
  s.FSMC_AsynchronousWait = FSMC_AsynchronousWait_Disable;
  s.FSMC_WaitSignalActive = FSMC_WaitSignalActive_BeforeWaitState;
  s.FSMC_WaitSignal = FSMC_WaitSignal_Disable;
  s.FSMC_WriteBurst = FSMC_WriteBurst_Disable;
  s.FSMC_WriteOperation = FSMC_WriteOperation_Enable;
  s.FSMC_ExtendedMode = FSMC_ExtendedMode_Enable;
  s.FSMC_ReadWriteTimingStruct = &rt;
  s.FSMC_WriteTimingStruct = &wt;
  FSMC_NORSRAMInit(&s);
  FSMC_NORSRAMCmd(FSMC_Bank1_NORSRAM1, ENABLE);
}

static void ssd1963_init(void)
{
  WR_REG(0xE2);  // PLL: 25 MHz crystal * 24 / 5 = 120 MHz
  WR_DATA(0x17); WR_DATA(0x04); WR_DATA(0x54);
  WR_REG(0xE0); WR_DATA(0x01);  // enable PLL
  delay_ms(10);
  WR_REG(0xE0); WR_DATA(0x03);  // lock PLL as system clock
  delay_ms(10);
  WR_REG(0x01);                 // soft reset
  delay_ms(100);

  uint32_t fpr = (SSD_DCLK_FREQUENCY * 1048576u) / 120 - 1;  // pixel clock
  WR_REG(0xE6); WR_DATA((fpr >> 16) & 0xFF); WR_DATA((fpr >> 8) & 0xFF); WR_DATA(fpr & 0xFF);

  WR_REG(0xB0);  // LCD mode: 16-bit, TFT, resolution
  WR_DATA(0x00); WR_DATA(0x00);
  WR_DATA((LCD_WIDTH - 1) >> 8); WR_DATA((LCD_WIDTH - 1) & 0xFF);
  WR_DATA((LCD_HEIGHT - 1) >> 8); WR_DATA((LCD_HEIGHT - 1) & 0xFF);
  WR_DATA(0x00);

  WR_REG(0xB4);  // horizontal period
  WR_DATA((HT - 1) >> 8); WR_DATA((HT - 1) & 0xFF);
  WR_DATA(HPS >> 8); WR_DATA(HPS & 0xFF);
  WR_DATA(SSD_HOR_PULSE_WIDTH - 1);
  WR_DATA(0x00); WR_DATA(0x00); WR_DATA(0x00);

  WR_REG(0xB6);  // vertical period
  WR_DATA((VT - 1) >> 8); WR_DATA((VT - 1) & 0xFF);
  WR_DATA(VPS >> 8); WR_DATA(VPS & 0xFF);
  WR_DATA(SSD_VER_FRONT_PORCH - 1);
  WR_DATA(0x00); WR_DATA(0x00);

  WR_REG(0xF0); WR_DATA(0x03);  // pixel data interface: 16-bit RGB565
  WR_REG(0xBC);                 // post processor (BTT values, reduce banding)
  WR_DATA(0x34); WR_DATA(0x77); WR_DATA(0x48); WR_DATA(0x01);
  WR_REG(0x29);                 // display on
  WR_REG(0x36); WR_DATA(0x00);  // address mode: 0 degrees (landscape, as BTT default)
}

void lcd_init(void)
{
  gpio_set(PIN_LCD_LED, 0);
  gpio_mode(PIN_LCD_LED, GPIO_OUT, 0);
  fsmc_init();
  ssd1963_init();
  lcd_window(0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1);
  lcd_fill_n(0x0000, (uint32_t)LCD_WIDTH * LCD_HEIGHT);
  delay_ms(120);
  gpio_set(PIN_LCD_LED, 1);  // backlight full on
}

void lcd_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
  WR_REG(0x2A); WR_DATA(x0 >> 8); WR_DATA(x0 & 0xFF); WR_DATA(x1 >> 8); WR_DATA(x1 & 0xFF);
  WR_REG(0x2B); WR_DATA(y0 >> 8); WR_DATA(y0 & 0xFF); WR_DATA(y1 >> 8); WR_DATA(y1 & 0xFF);
  WR_REG(0x2C);
}

void lcd_pixel(uint16_t c) { WR_DATA(c); }

void lcd_fill_n(uint16_t c, uint32_t n)
{
  while (n--) WR_DATA(c);
}

#ifndef BOARD_H
#define BOARD_H
// BigTreeTech TFT43 V3.0 (STM32F207VCT6), pin map taken from the BTT
// TouchScreenFirmware variants pin_TFT43_V3_0.h -> pin_TFT35_E3_V3_0.h -> pin_TFT35_V3_0.h.

#define LCD_WIDTH  480
#define LCD_HEIGHT 272

// SSD1963 panel timing (pin_TFT43_V3_0.h)
#define SSD_DCLK_FREQUENCY  12  // MHz
#define SSD_HOR_PULSE_WIDTH 1
#define SSD_HOR_BACK_PORCH  43
#define SSD_HOR_FRONT_PORCH 2
#define SSD_VER_PULSE_WIDTH 1
#define SSD_VER_BACK_PORCH  12
#define SSD_VER_FRONT_PORCH 1
#define LCD_FSMC_DATASET    0x10  // FSMC write data setup time; SSD1963 needs it slow

// Pins are encoded as (port << 4) | pin, port A = 0
#define PIN(port, n) ((((port) - 'A') << 4) | (n))

#define PIN_LCD_LED   PIN('D', 12)  // backlight
#define PIN_BUZZER    PIN('D', 13)
#define PIN_W25Q_CS   PIN('B', 6)   // SPI flash, kept deselected

#define PIN_TP_CS     PIN('E', 6)   // XPT2046 touch, bit-banged SPI
#define PIN_TP_SCK    PIN('E', 5)
#define PIN_TP_MISO   PIN('E', 4)
#define PIN_TP_MOSI   PIN('E', 3)
#define PIN_TP_PEN    PIN('C', 13)

// "TFT" serial header to the mainboard: USART2, TX PA2, RX PA3 (BTT SERIAL_PORT)
#define PIN_UART_TX   PIN('A', 2)
#define PIN_UART_RX   PIN('A', 3)
#define GRBL_BAUD     115200

// Flash layout: BTT bootloader in sector 0 (0x08000000), BTT parameters in
// sector 1 (0x08004000, we keep its touch calibration format), app from 0x08008000.
#define PARAM_ADDR    0x08004000u

#endif

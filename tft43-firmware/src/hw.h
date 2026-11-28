#ifndef HW_H
#define HW_H
#include <stdint.h>
#include <stdbool.h>

void hw_init(void);
uint32_t millis(void);
void delay_us(uint32_t us);
void delay_ms(uint32_t ms);

enum { GPIO_IN = 0, GPIO_IN_PU, GPIO_OUT, GPIO_AF };
void gpio_mode(uint8_t pin, uint8_t mode, uint8_t af);
void gpio_set(uint8_t pin, bool level);
bool gpio_get(uint8_t pin);

void beep(uint16_t ms);  // short key click on the buzzer

#endif

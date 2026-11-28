#ifndef UART_H
#define UART_H
#include <stdint.h>
#include <stdbool.h>

void uart_init(uint32_t baud);
int uart_getc(void);             // -1 if nothing received
void uart_putc(uint8_t c);       // queued, blocks only if the TX buffer is full
void uart_puts(const char * s);

#endif

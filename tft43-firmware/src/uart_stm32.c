// USART2 (PA2 TX, PA3 RX) to the CNC controller, interrupt driven ring buffers.
#include "stm32f2xx.h"
#include "board.h"
#include "hw.h"
#include "uart.h"

#define RXSZ 2048
#define TXSZ 512
static volatile uint8_t rxb[RXSZ], txb[TXSZ];
static volatile uint16_t rxh, rxt, txh, txt;

void uart_init(uint32_t baud)
{
  RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
  USART_InitTypeDef u;
  USART_StructInit(&u);
  u.USART_BaudRate = baud;
  u.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
  USART_Init(USART2, &u);
  USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);
  NVIC_SetPriority(USART2_IRQn, 1);
  NVIC_EnableIRQ(USART2_IRQn);
  USART_Cmd(USART2, ENABLE);
  // pins last, so no glitch byte is sent at power-on (as BTT does)
  gpio_mode(PIN_UART_TX, GPIO_AF, 7);
  gpio_mode(PIN_UART_RX, GPIO_AF, 7);
}

void USART2_IRQHandler(void)
{
  uint16_t sr = USART2->SR;
  if (sr & (USART_SR_RXNE | USART_SR_ORE)) {
    uint8_t c = USART2->DR;  // reading DR also clears ORE
    uint16_t n = (rxh + 1) % RXSZ;
    if (n != rxt) { rxb[rxh] = c; rxh = n; }
  }
  if ((USART2->CR1 & USART_CR1_TXEIE) && (sr & USART_SR_TXE)) {
    if (txt != txh) { USART2->DR = txb[txt]; txt = (txt + 1) % TXSZ; }
    else USART2->CR1 &= ~USART_CR1_TXEIE;
  }
}

int uart_getc(void)
{
  if (rxt == rxh) return -1;
  uint8_t c = rxb[rxt];
  rxt = (rxt + 1) % RXSZ;
  return c;
}

void uart_putc(uint8_t c)
{
  uint16_t n = (txh + 1) % TXSZ;
  while (n == txt) {}
  txb[txh] = c;
  txh = n;
  USART2->CR1 |= USART_CR1_TXEIE;
}

void uart_puts(const char * s)
{
  while (*s) uart_putc((uint8_t)*s++);
}

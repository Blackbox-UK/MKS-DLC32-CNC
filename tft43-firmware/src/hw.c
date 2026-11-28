// Clock, 1 ms tick, microsecond delay, GPIO helpers and buzzer.
#include "stm32f2xx.h"
#include "board.h"
#include "hw.h"

#define CPU_HZ 120000000u  // SetSysClock: 8 MHz / 8 * 240 / 2

static volatile uint32_t ms_ticks;
static volatile uint16_t beep_left;

static GPIO_TypeDef * const ports[] = {GPIOA, GPIOB, GPIOC, GPIOD, GPIOE};

// Startup code calls SystemInit before main; clocks are set in hw_init instead.
void SystemInit(void) {}

void hw_init(void)
{
  __disable_irq();  // the bootloader may leave interrupts on; vectors move below
  SystemClockInit();       // HSE 8 MHz -> 120 MHz (CMSIS system_stm32f2xx.c, PLL_M = HSE/1MHz)
  SCB->VTOR = 0x08008000;  // app vector table, after the BTT bootloader

  RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN |
                  RCC_AHB1ENR_GPIODEN | RCC_AHB1ENR_GPIOEEN;
  NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);

  // DWT cycle counter for delay_us
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= 1;

  SysTick_Config(CPU_HZ / 1000);
  NVIC_SetPriority(SysTick_IRQn, 3);

  gpio_set(PIN_W25Q_CS, 1);
  gpio_mode(PIN_W25Q_CS, GPIO_OUT, 0);
  gpio_set(PIN_BUZZER, 0);
  gpio_mode(PIN_BUZZER, GPIO_OUT, 0);

  __enable_irq();
}

void SysTick_Handler(void)
{
  ms_ticks++;
  if (beep_left) {
    beep_left--;
    gpio_set(PIN_BUZZER, beep_left & 1);  // 500 Hz square wave
  }
}

uint32_t millis(void) { return ms_ticks; }

void delay_us(uint32_t us)
{
  uint32_t start = DWT->CYCCNT;
  uint32_t cycles = us * (CPU_HZ / 1000000);
  while ((DWT->CYCCNT - start) < cycles) {}
}

void delay_ms(uint32_t ms)
{
  uint32_t start = ms_ticks;
  while ((ms_ticks - start) < ms) {}
}

void beep(uint16_t ms) { beep_left = ms; }

void gpio_mode(uint8_t pin, uint8_t mode, uint8_t af)
{
  GPIO_TypeDef * p = ports[pin >> 4];
  uint32_t n = pin & 15;
  uint32_t moder = (mode == GPIO_OUT) ? 1 : (mode == GPIO_AF) ? 2 : 0;
  p->MODER = (p->MODER & ~(3u << (n * 2))) | (moder << (n * 2));
  p->OSPEEDR |= (3u << (n * 2));  // 100 MHz
  p->OTYPER &= ~(1u << n);        // push-pull
  p->PUPDR = (p->PUPDR & ~(3u << (n * 2))) | ((mode == GPIO_IN_PU ? 1u : 0u) << (n * 2));
  if (mode == GPIO_AF) {
    volatile uint32_t * afr = &p->AFR[n >> 3];
    *afr = (*afr & ~(15u << ((n & 7) * 4))) | ((uint32_t)af << ((n & 7) * 4));
  }
}

void gpio_set(uint8_t pin, bool level)
{
  GPIO_TypeDef * p = ports[pin >> 4];
  if (level) p->BSRRL = 1u << (pin & 15);
  else       p->BSRRH = 1u << (pin & 15);
}

bool gpio_get(uint8_t pin)
{
  return (ports[pin >> 4]->IDR >> (pin & 15)) & 1;
}

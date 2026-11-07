#ifndef BOARD_H
#define BOARD_H
#include <stdint.h>
#include "tracker_config.h"
typedef volatile uint32_t reg32;
#define REG32(a) (*(reg32 *)(a))
#define GPIOA_BASE 0x40010800u
#define GPIOB_BASE 0x40010C00u
#define GPIOC_BASE 0x40011000u
typedef struct { uint32_t base; } gpio_t;
static const gpio_t gpio_a={GPIOA_BASE},gpio_b={GPIOB_BASE},gpio_c={GPIOC_BASE};
extern volatile uint32_t g_millis;
extern volatile uint8_t g_io_ready;
static inline uint32_t millis(void){return g_millis;}
static inline void pin_high(gpio_t p,unsigned pin){REG32(p.base+0x10u)=1u<<pin;}
static inline void pin_low(gpio_t p,unsigned pin){REG32(p.base+0x14u)=1u<<pin;}
static inline uint8_t pin_read(gpio_t p,unsigned pin){return (uint8_t)((REG32(p.base+8u)>>pin)&1u);}
static inline uint32_t irq_lock(void){uint32_t x;__asm volatile("mrs %0, primask\n cpsid i":"=r"(x)::"memory");return x;}
static inline void irq_restore(uint32_t x){__asm volatile("msr primask, %0"::"r"(x):"memory");}
void delay_ms(uint32_t n);
void board_init(void);
int board_adc_init(void);
int board_adc_read(unsigned channel,uint16_t *value);
void board_coils(uint8_t pan,uint8_t tilt);
void board_led(int on);
void board_watchdog_start(void);
void board_watchdog_feed(void);
int board_uart_get(void);
void board_uart_put(char c);
void board_fault(void);
#endif


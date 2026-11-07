#include "board.h"
#define RCC_CR REG32(0x40021000u)
#define RCC_CFGR REG32(0x40021004u)
#define RCC_APB2ENR REG32(0x40021018u)
#define FLASH_ACR REG32(0x40022000u)
#define AFIO_MAPR REG32(0x40010004u)
#define ADC_SR REG32(0x40012400u)
#define ADC_CR1 REG32(0x40012404u)
#define ADC_CR2 REG32(0x40012408u)
#define ADC_SMPR2 REG32(0x40012410u)
#define ADC_SQR1 REG32(0x4001242Cu)
#define ADC_SQR3 REG32(0x40012434u)
#define ADC_DR REG32(0x4001244Cu)
#define USART_SR REG32(0x40013800u)
#define USART_DR REG32(0x40013804u)
#define USART_BRR REG32(0x40013808u)
#define USART_CR1 REG32(0x4001380Cu)
#define MOTOR_MASK ((1u<<0)|(1u<<1)|(1u<<3)|(1u<<4)|(1u<<5)|(1u<<6)|(1u<<7)|(1u<<8))
volatile uint32_t g_millis;
volatile uint8_t g_io_ready;
static void mode(gpio_t p,unsigned n,unsigned cfg)
{
 reg32 *cr=(reg32 *)(p.base+(n<8u?0u:4u));unsigned shift=(n&7u)*4u;
 *cr=(*cr&~(15u<<shift))|((cfg&15u)<<shift);
}
void delay_ms(uint32_t n){uint32_t start=millis();while((uint32_t)(millis()-start)<n){__asm volatile("nop");}}
void board_coils(uint8_t pan,uint8_t tilt)
{
 const uint8_t pp[4]={PAN_IN1_PIN,PAN_IN2_PIN,PAN_IN3_PIN,PAN_IN4_PIN};
 const uint8_t tp[4]={TILT_IN1_PIN,TILT_IN2_PIN,TILT_IN3_PIN,TILT_IN4_PIN};
 uint32_t set=0;for(unsigned i=0;i<4u;++i){if(pan&(1u<<i))set|=1u<<pp[i];if(tilt&(1u<<i))set|=1u<<tp[i];}
 REG32(GPIOB_BASE+0x10u)=set|((MOTOR_MASK&~set)<<16);
}
void board_led(int on){if(on)pin_low(gpio_c,13u);else pin_high(gpio_c,13u);}
void board_fault(void)
{
 __asm volatile("cpsid i":::"memory");
 REG32(GPIOB_BASE+0x14u)=MOTOR_MASK;
 while(1){__asm volatile("nop");}
}
void board_init(void)
{
 RCC_APB2ENR|=(1u<<0)|(1u<<2)|(1u<<3)|(1u<<4)|(1u<<9)|(1u<<14);(void)RCC_APB2ENR;
 board_coils(0,0);
 const unsigned mp[8]={PAN_IN1_PIN,PAN_IN2_PIN,PAN_IN3_PIN,PAN_IN4_PIN,TILT_IN1_PIN,TILT_IN2_PIN,TILT_IN3_PIN,TILT_IN4_PIN};
 for(unsigned i=0;i<8u;++i)mode(gpio_b,mp[i],2u);
 AFIO_MAPR=(AFIO_MAPR&~(7u<<24))|(2u<<24); /* JTAG off; SWD preserved */
 for(unsigned i=0;i<5u;++i)mode(gpio_a,i,0u);
 const unsigned kp[5]={KEY_LEFT_PIN,KEY_DOWN_PIN,KEY_RIGHT_PIN,KEY_UP_PIN,KEY_MODE_PIN};
 for(unsigned i=0;i<5u;++i){pin_high(gpio_a,kp[i]);mode(gpio_a,kp[i],8u);}
 const unsigned lp[6]={LCD_SCK_PIN,LCD_MOSI_PIN,LCD_RESET_PIN,LCD_DC_PIN,LCD_CS_PIN,LCD_BACKLIGHT_PIN};
 for(unsigned i=0;i<6u;++i){pin_low(gpio_b,lp[i]);mode(gpio_b,lp[i],2u);}
 pin_high(gpio_b,LCD_CS_PIN);pin_high(gpio_b,LCD_RESET_PIN);pin_high(gpio_b,LCD_BACKLIGHT_PIN);
 pin_high(gpio_c,13u);mode(gpio_c,13u,2u);
 mode(gpio_a,9u,0xBu);mode(gpio_a,10u,4u);
 RCC_CR|=1u;while(!(RCC_CR&2u)){}
 FLASH_ACR=(1u<<4)|2u;
 RCC_CFGR=(4u<<8)|(2u<<14)|(14u<<18); /* APB1 /2, ADC /6; HSI/2 *16=64 MHz */
 RCC_CR|=1u<<24;
 for(uint32_t wait=0;!(RCC_CR&(1u<<25));++wait)if(wait>1000000u)board_fault();
 RCC_CFGR=(RCC_CFGR&~3u)|2u;while(((RCC_CFGR>>2)&3u)!=2u){}
 REG32(0xE000ED08u)=0x08000000u; /* VTOR */
 g_io_ready=1u;
 REG32(0xE000E014u)=CORE_HZ/1000u-1u;REG32(0xE000E018u)=0;REG32(0xE000E010u)=7u;
 USART_BRR=(CORE_HZ+57600u)/115200u;USART_CR1=(1u<<13)|(1u<<3)|(1u<<2);
}
static int wait_clear(reg32 *reg,uint32_t mask)
{
 uint32_t start=millis();while(*reg&mask){if((uint32_t)(millis()-start)>10u)return 0;}return 1;
}
int board_adc_init(void)
{
 ADC_CR1=0u;ADC_CR2=1u|(7u<<17)|(1u<<20);ADC_SMPR2=0x7FFFu;
 delay_ms(2u);ADC_CR2|=1u<<3;if(!wait_clear((reg32 *)0x40012408u,1u<<3))return 0;
 ADC_CR2|=1u<<2;return wait_clear((reg32 *)0x40012408u,1u<<2);
}
int board_adc_read(unsigned channel,uint16_t *value)
{
 ADC_SQR1=0u;ADC_SQR3=channel&31u;ADC_SR=0u;ADC_CR2|=1u<<22;
 uint32_t start=millis();while(!(ADC_SR&2u)){if((uint32_t)(millis()-start)>3u)return 0;}
 *value=(uint16_t)(ADC_DR&4095u);return 1;
}
int board_uart_get(void)
{
 uint32_t sr=USART_SR;
 if(sr&((1u<<3)|(1u<<5))){uint32_t data=USART_DR;if(sr&(1u<<5))return (int)(data&0xFFu);}
 return -1;
}
void board_uart_put(char c)
{
 uint32_t start=millis();while(!(USART_SR&(1u<<7)))if((uint32_t)(millis()-start)>3u)return;
 USART_DR=(uint8_t)c;
}
void board_watchdog_start(void)
{
 /* Start, unlock, configure, wait, reload: ST STM32F1 HAL ordering. */
 REG32(0x40021024u)|=1u;
 uint32_t start=millis();
 while(!(REG32(0x40021024u)&2u)){if((uint32_t)(millis()-start)>100u)board_fault();}
 REG32(0x40003000u)=0xCCCCu;
 REG32(0x40003000u)=0x5555u;
 REG32(0x40003004u)=4u;
 REG32(0x40003008u)=1250u;
 start=millis();
 while(REG32(0x4000300Cu)){if((uint32_t)(millis()-start)>100u)board_fault();}
 REG32(0x40003000u)=0xAAAAu;
 REG32(0x40003000u)=0xCCCCu;
}
void board_watchdog_feed(void){REG32(0x40003000u)=0xAAAAu;}



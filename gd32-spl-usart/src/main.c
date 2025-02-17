#if defined(GD32F10x)
#include "gd32f10x.h"
#elif defined(GD32F1x0)
#include "gd32f1x0.h"
#elif defined (GD32F20x)
#include "gd32f20x.h"
#elif defined(GD32F3x0)
#include "gd32f3x0.h"
#elif defined(GD32F30x)
#include "gd32f30x.h"
#elif defined(GD32F4xx)
#include "gd32f4xx.h"
#elif defined(GD32F403)
#include "gd32f403.h"
#elif defined(GD32E10X)
#include "gd32e10x.h"
#elif defined(GD32E23x)
#include "gd32e23x.h"
#elif defined(GD32E50X)
#include "gd32e50x.h"
#elif defined(GD32L23x)
#include "gd32l23x.h"
#elif defined(GD32W51x)
#include "gd32w51x.h"
#else
#error "Unknown chip series"
#endif
#include <stdio.h>

#ifndef USE_ALTERNATE_USART0_PINS
/* settings for used USART (UASRT0) and pins, TX = PA9, RX = PA10 */
#define RCU_GPIO            RCU_GPIOA
#define RCU_UART            RCU_USART0
#define USART               USART0
#define UART_TX_RX_GPIO     GPIOA
#define UART_TX_GPIO_PIN    GPIO_PIN_9
#define UART_RX_GPIO_PIN    GPIO_PIN_10

/* only for certain series: set pin to alternate function x for UART */
#if defined(GD32F3x0) || defined(GD32F1x0) || defined(GD32F4xx) || defined(GD32E23x) || defined(GD32L23x) || defined(GD32W51x)
#define UART_TX_AF  GPIO_AF_1
#define UART_RX_AF  GPIO_AF_1
#endif
#else
/* settings for USART0 alternate settings, TX = PB6, RX = PB7 */
#define RCU_GPIO            RCU_GPIOB
#define RCU_UART            RCU_USART0
#define USART               USART0
#define UART_TX_RX_GPIO     GPIOB
#define UART_TX_GPIO_PIN    GPIO_PIN_6
#define UART_RX_GPIO_PIN    GPIO_PIN_7

/* only for certain series: set pin to alternate function x for UART */
#if defined(GD32F3x0) || defined(GD32F1x0) || defined(GD32F4xx) || defined(GD32E23x) || defined(GD32L23x) || defined(GD32W51x)
#define UART_TX_AF  GPIO_AF_0 /* PB6 AF0 is USART0_TX */
#define UART_RX_AF  GPIO_AF_0 /* PB7 AF0 is USART0_RX */
#endif
#endif 

void systick_config(void);
void delay_1ms(uint32_t count);

int main(void)
{
    /* configure systick for more precise delays */
    systick_config();

    /* enable GPIO clock */
    rcu_periph_clock_enable(RCU_GPIO);

    /* enable USART clock */
    rcu_periph_clock_enable(RCU_UART);

    /* connect port to USARTx_Tx and USARTx_Rx  */
#if defined(GD32F3x0) || defined(GD32F1x0) || defined(GD32F4xx) || defined(GD32E23x) || defined(GD32L23x) || defined(GD32W51x)
    gpio_af_set(UART_TX_RX_GPIO, UART_TX_AF, UART_TX_GPIO_PIN);
    gpio_af_set(UART_TX_RX_GPIO, UART_RX_AF, UART_RX_GPIO_PIN);

    gpio_mode_set(UART_TX_RX_GPIO, GPIO_MODE_AF, GPIO_PUPD_PULLUP, UART_TX_GPIO_PIN);
    gpio_output_options_set(UART_TX_RX_GPIO, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, UART_TX_GPIO_PIN);
    gpio_mode_set(UART_TX_RX_GPIO, GPIO_MODE_AF, GPIO_PUPD_PULLUP, UART_RX_GPIO_PIN);
    gpio_output_options_set(UART_TX_RX_GPIO, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, UART_RX_GPIO_PIN);
#else /* valid for GD32F10x, GD32F30x */
    gpio_init(UART_TX_RX_GPIO, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, UART_TX_GPIO_PIN);
    gpio_init(UART_TX_RX_GPIO, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_50MHZ, UART_RX_GPIO_PIN);
#endif

    /* USART configure 115200 8N1 */
    usart_deinit(USART);
    usart_word_length_set(USART, USART_WL_8BIT);
    usart_stop_bit_set(USART, USART_STB_1BIT);
    usart_parity_config(USART, USART_PM_NONE);
    usart_baudrate_set(USART, 115200U);
    usart_receive_config(USART, USART_RECEIVE_ENABLE);
    usart_transmit_config(USART, USART_TRANSMIT_ENABLE);
    usart_enable(USART);

    /* short delay to make sure that the serial monitor was started before we print something */
    delay_1ms(500);

    int i = 0;
    while (1)
    {
        printf("a usart transmit test example! iteration %d\n", i++);
        delay_1ms(500);
    }
}

/* retarget the gcc's C library printf function to the USART */
#include <errno.h>
#include <sys/unistd.h> // STDOUT_FILENO, STDERR_FILENO
int _write(int file, char *data, int len)
{
    if ((file != STDOUT_FILENO) && (file != STDERR_FILENO))
    {
        errno = EBADF;
        return -1;
    }

    for (int i = 0; i < len; i++)
    {
        usart_data_transmit(USART, (uint8_t)data[i]);
        while (RESET == usart_flag_get(USART, USART_FLAG_TBE))
            ;
    }

    // return # of bytes written - as best we can tell
    return len;
}

/* code for delaying using the SysTick */

volatile static uint32_t delay;

void systick_config(void)
{
    /* setup systick timer for 1000Hz interrupts */
    if (SysTick_Config(SystemCoreClock / 1000U))
    {
        /* capture error */
        while (1)
        {
        }
    }
    /* configure the systick handler priority */
    NVIC_SetPriority(SysTick_IRQn, 0x00U);
}

void delay_1ms(uint32_t count)
{
    delay = count;

    while (0U != delay)
    {
    }
}

void delay_decrement(void)
{
    if (0U != delay)
    {
        delay--;
    }
}

void SysTick_Handler(void)
{
    delay_decrement();
}

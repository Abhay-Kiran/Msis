/*
 * fm_uart.c -- register-level USART2. See fm_uart.h.
 *
 * Registers used (RM0390 chapter 30):
 *   SR  status : RXNE (byte received), TXE (data register empty),
 *                TC (last bit sent), ORE/FE/NE/PE (errors)
 *   DR  data   : write = send a byte, read = take the received byte
 *   BRR baud rate divider
 *   CR1 control: UE (enable), TE/RE (transmitter/receiver enable),
 *                RXNEIE/TXEIE/TCIE (interrupt enables)
 */
#include "fm_uart.h"
#include "app_config.h"
#include "fm_common.h"
#include "fm_trace.h"

#include "main.h"
#include "cmsis_os.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static osMessageQueueId_t s_rx_queue;
/* One line at a time. A binary semaphore, not a mutex: if recovery deletes
 * a task in the middle of a print, fm_uart_forget_task() can give the lock
 * back on its behalf (a FreeRTOS mutex can only be released by its owner,
 * so a deleted owner would lock the console forever). */
static osSemaphoreId_t    s_tx_mutex;
static volatile void     *s_tx_owner;      /* task holding s_tx_mutex      */
static osSemaphoreId_t    s_tx_done;       /* given by the TC interrupt    */
static uint8_t            s_tx_buf[FM_UART_TX_BUF_LEN];
static const uint8_t     *volatile s_tx_ptr;   /* next byte for the ISR    */
static volatile uint32_t  s_tx_len;            /* bytes the ISR still owes */

/* ---- Hardware configuration ---------------------------------------- */
static uint32_t pclk1_hz(void)
{
    /* USART2 sits on APB1. Its clock = core clock / AHB prescaler / APB1
     * prescaler; the prescaler tables come from the CMSIS startup code.
     * (With the CubeMX clock tree this is 84 MHz / 1 / 2 = 42 MHz.) */
    uint32_t ppre1 = (RCC->CFGR & RCC_CFGR_PPRE1) >> RCC_CFGR_PPRE1_Pos;
    return SystemCoreClock >> APBPrescTable[ppre1];
}

static void uart_hw_config(void)
{
    /* 1. Clocks: GPIOA (the pins) and USART2 itself. */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
    (void)RCC->APB1ENR;

    /* 2. Pins: PA2 = TX, PA3 = RX, both alternate function 7 (= USART2).
     *    MODER 10 = alternate function. PA2 gets the fastest slew rate;
     *    PA3 a pull-up so the idle line stays high if nothing drives it. */
    GPIOA->MODER   = (GPIOA->MODER & ~((3U << 4) | (3U << 6)))
                   | (2U << 4) | (2U << 6);
    GPIOA->OSPEEDR |= (3U << 4);
    GPIOA->PUPDR   = (GPIOA->PUPDR & ~((3U << 4) | (3U << 6))) | (1U << 6);
    GPIOA->AFR[0]  = (GPIOA->AFR[0] & ~((0xFU << 8) | (0xFU << 12)))
                   | (7U << 8) | (7U << 12);

    /* 3. USART: switch off while configuring, then set the baud rate.
     *    With 16x oversampling BRR = fck / baud (mantissa in bits 15:4,
     *    1/16 fraction in bits 3:0), so 42 MHz / 115200 = 364.6 -> 365. */
    USART2->CR1 = 0;
    USART2->CR2 = 0;                          /* 1 stop bit                  */
    USART2->CR3 = 0;                          /* no flow control, 3-sample   */
    USART2->BRR = (pclk1_hz() + FM_UART_BAUD / 2U) / FM_UART_BAUD;

    /* 4. Enable: UE (USART on), TE + RE (transmitter and receiver),
     *    RXNEIE (interrupt for every received byte). 8N1: M=0, PCE=0. */
    USART2->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE;

    /* Priority 7: numerically >= 5, so the ISR may use FreeRTOS "FromISR"
     * calls; lower urgency than the ADC interrupt (6). */
    NVIC_SetPriority(USART2_IRQn, 7U);
    NVIC_EnableIRQ(USART2_IRQn);
}

static void tx_abort(void)
{
    USART2->CR1 &= ~(USART_CR1_TXEIE | USART_CR1_TCIE);
    s_tx_len = 0;
}

void fm_uart_init(void)
{
    s_rx_queue = osMessageQueueNew(FM_UART_RX_QUEUE_LEN, sizeof(uint8_t), NULL);
    s_tx_mutex = osSemaphoreNew(1, 1, NULL);
    s_tx_done  = osSemaphoreNew(1, 0, NULL);
    uart_hw_config();
}

/* USART2 configuration bits that must never change while running. The
 * interrupt-enable bits are left out: they are switched on and off for
 * every transmitted line. */
static const fm_reg_check_t s_uart_regs[] = {
    { &USART2->BRR, 0xFFFFFFFFUL },
    { &USART2->CR1, USART_CR1_UE | USART_CR1_M | USART_CR1_WAKE | USART_CR1_PCE |
                    USART_CR1_PS | USART_CR1_TE | USART_CR1_RE | USART_CR1_OVER8 },
    { &USART2->CR2, USART_CR2_STOP | USART_CR2_LINEN | USART_CR2_CLKEN },
    { &USART2->CR3, USART_CR3_ONEBIT | USART_CR3_CTSE | USART_CR3_RTSE |
                    USART_CR3_HDSEL | USART_CR3_SCEN | USART_CR3_IREN },
};

void fm_uart_register(void)
{
    fm_register_periph(COMP_PER_UART, "USART2", s_uart_regs,
                       (uint8_t)(sizeof(s_uart_regs) / sizeof(s_uart_regs[0])),
                       fm_uart_reinit, true);
    /* one interrupt per byte: signature only, kept out of the history */
    fm_register_isr(COMP_ISR_USART2, "uart_isr", false, fm_uart_reinit);
}

/* ---- Interrupt handler ------------------------------------------------ */
void fm_uart_irq_handler(void)
{
    uint32_t sr  = USART2->SR;
    uint32_t cr1 = USART2->CR1;

    /* Receive side. A byte arrived (RXNE) or an error occurred. Reading SR
     * (above) and then DR clears the error flags, so DR is read in both
     * cases. A byte received with an error is discarded and reported. */
    if (sr & (USART_SR_RXNE | USART_SR_ORE | USART_SR_FE | USART_SR_NE | USART_SR_PE)) {
        uint8_t  b   = (uint8_t)USART2->DR;
        uint16_t err = 0;
        if (sr & USART_SR_ORE) { err |= FM_COMM_ORE; }
        if (sr & USART_SR_FE)  { err |= FM_COMM_FE;  }
        if (sr & USART_SR_NE)  { err |= FM_COMM_NE;  }
        if (sr & USART_SR_PE)  { err |= FM_COMM_FE;  }
        if (err != 0U) {
            fm_comm_error(COMP_PER_UART, err);
        } else {
            fm_uart_rx_isr_byte(b);
        }
    }

    /* Transmit side. TXE: the data register is empty, load the next byte.
     * After the last byte, trade the TXE interrupt for the TC interrupt,
     * which fires when the final bit has really left the pin. */
    if ((sr & USART_SR_TXE) && (cr1 & USART_CR1_TXEIE)) {
        if (s_tx_len > 0U) {
            USART2->DR = *s_tx_ptr++;
            s_tx_len--;
        }
        if (s_tx_len == 0U) {
            USART2->CR1 = (USART2->CR1 & ~USART_CR1_TXEIE) | USART_CR1_TCIE;
        }
    }
    if ((sr & USART_SR_TC) && (cr1 & USART_CR1_TCIE)) {
        USART2->CR1 &= ~USART_CR1_TCIE;
        osSemaphoreRelease(s_tx_done);        /* wake the task that is printing */
    }
}

void fm_uart_rx_isr_byte(uint8_t byte)
{
    osMessageQueuePut(s_rx_queue, &byte, 0, 0);   /* full: drop, never block */
}

void fm_uart_inject_rx(const char *s)
{
    while (*s) {
        uint8_t b = (uint8_t)*s++;
        osMessageQueuePut(s_rx_queue, &b, 0, 0);
    }
}

int fm_uart_getc(uint32_t timeout_ms)
{
    uint8_t b;
    if (osMessageQueueGet(s_rx_queue, &b, NULL, timeout_ms) != osOK) {
        return -1;
    }
    return b;
}

/* ---- Transmit ----------------------------------------------------------- */
static void blocking_send(const uint8_t *data, uint32_t len)
{
    /* boot banner, before the scheduler runs: wait for TXE before every
     * byte, and for TC after the last one. */
    for (uint32_t i = 0; i < len; i++) {
        while ((USART2->SR & USART_SR_TXE) == 0U) {
        }
        USART2->DR = data[i];
    }
    while ((USART2->SR & USART_SR_TC) == 0U) {
    }
}

void fm_uart_write(const uint8_t *data, uint32_t len)
{
    if (osKernelGetState() != osKernelRunning || s_tx_mutex == NULL) {
        blocking_send(data, len);
        return;
    }
    if (osSemaphoreAcquire(s_tx_mutex, FM_UART_TX_TIMEOUT_MS) != osOK) {
        return;        /* UART wedged: drop the line */
    }
    s_tx_owner = osThreadGetId();
    while (len > 0U) {
        uint32_t n = len > sizeof(s_tx_buf) ? sizeof(s_tx_buf) : len;
        memcpy(s_tx_buf, data, n);
        osSemaphoreAcquire(s_tx_done, 0);              /* clear stale give */
        s_tx_ptr = s_tx_buf;
        s_tx_len = n;
        USART2->CR1 |= USART_CR1_TXEIE;                /* ISR does the rest */
        if (osSemaphoreAcquire(s_tx_done, FM_UART_TX_TIMEOUT_MS) != osOK) {
            tx_abort();
            break;
        }
        data += n;
        len  -= n;
    }
    s_tx_owner = NULL;
    osSemaphoreRelease(s_tx_mutex);
}

void fm_uart_printf(const char *fmt, ...)
{
    char    line[FM_UART_TX_BUF_LEN];
    va_list args;

    va_start(args, fmt);
    int n = vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    if (n < 0) {
        return;
    }
    if ((uint32_t)n >= sizeof(line)) {
        n = sizeof(line) - 1;       /* truncated, still send what fit */
    }
    fm_uart_write((uint8_t *)line, (uint32_t)n);
}

/* ---- Recovery ------------------------------------------------------------ */
void fm_uart_reinit(uint8_t comp_id)
{
    (void)comp_id;
    bool locked = (osSemaphoreAcquire(s_tx_mutex, FM_UART_TX_TIMEOUT_MS) == osOK);
    NVIC_DisableIRQ(USART2_IRQn);
    tx_abort();
    RCC->APB1RSTR |=  RCC_APB1RSTR_USART2RST;   /* every register back to its */
    RCC->APB1RSTR &= ~RCC_APB1RSTR_USART2RST;   /* reset value               */
    uart_hw_config();                           /* then the boot configuration */
    if (locked) {
        osSemaphoreRelease(s_tx_mutex);
    }
}

void fm_uart_forget_task(void *task_handle)
{
    uint32_t pm = __get_PRIMASK();
    __disable_irq();
    bool held = (task_handle != NULL && s_tx_owner == task_handle);
    if (held) {
        s_tx_owner = NULL;
    }
    __set_PRIMASK(pm);
    if (held) {
        tx_abort();
        osSemaphoreRelease(s_tx_mutex);
    }
}

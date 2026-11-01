/*
 * fm_uart.h -- USART2 driver for the shell, telemetry and alerts,
 * written at REGISTER level (no HAL).
 *
 * Pins : PA2 = USART2_TX, PA3 = USART2_RX (alternate function 7). On the
 *        Nucleo these go to the ST-LINK virtual COM port: the same USB
 *        cable that flashes the board. 115200 baud, 8 data bits, no
 *        parity, 1 stop bit.
 * RX   : one interrupt per byte (RXNE) into a FreeRTOS queue; the shell
 *        task blocks on it (fm_uart_getc).
 * TX   : interrupt driven. The calling task sleeps on a semaphore while
 *        the line goes out, so a long print costs almost no CPU (a
 *        busy-wait transmit would look exactly like a task stuck in a loop
 *        to the fault monitor). A lock keeps lines from different tasks
 *        whole.
 * Errors: overrun / framing / noise flags are read from the status
 *        register in the ISR and reported to the framework as
 *        communication errors on COMP_PER_UART; '$' frames with a bad
 *        checksum are reported by the shell.
 */
#ifndef FM_UART_H
#define FM_UART_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Configures the pins and USART2, creates the queue and semaphores. */
void fm_uart_init(void);

/* Registers USART2 (peripheral) and its interrupt with the framework. */
void fm_uart_register(void);

/* The USART2 interrupt handler body (called from USART2_IRQHandler). */
void fm_uart_irq_handler(void);

/* Puts a received byte into the RX queue. ISR-safe, never blocks. */
void fm_uart_rx_isr_byte(uint8_t byte);

/* Feeds bytes into the RX queue as if received (fault injection). ISR-safe. */
void fm_uart_inject_rx(const char *s);

/* Next received byte, or -1 after timeout_ms. Shell task only. */
int  fm_uart_getc(uint32_t timeout_ms);

void fm_uart_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void fm_uart_write(const uint8_t *data, uint32_t len);

/* Recovery: reset USART2 through RCC and configure it again. */
void fm_uart_reinit(uint8_t comp_id);

/* Call before deleting a task (recovery): if it was in the middle of a
 * print, the console lock is given back so other tasks can print. */
void fm_uart_forget_task(void *task_handle);

#ifdef __cplusplus
}
#endif
#endif /* FM_UART_H */

/*
 * uart_shell.h -- UART command shell. Owner: Abhishek.
 *
 * One task reads lines from the ST-LINK virtual COM port (115200-8-N-1)
 * and dispatches on the first character, the command letter. Modules
 * claim letters with fm_shell_register() (the full map is in
 * fm_types.h), so no module has to edit this file to add a command.
 *
 * Two line formats:
 *   plain   "T 55"             for people typing in a terminal
 *   framed  "$T 55*hh"         for scripts: hh = XOR of the characters
 *                              between '$' and '*', in hex (NMEA style).
 *                              A wrong checksum is rejected and counted
 *                              as a communication error.
 *
 * The shell is also a monitored task: it wakes every 100 ms even with no
 * input, so it can be a fault-injection target like the others.
 */
#ifndef UART_SHELL_H
#define UART_SHELL_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Handler receives the line with the command letter stripped and leading
 * spaces trimmed (may be an empty string). */
typedef void (*fm_shell_handler_t)(const char *args);

/* Claims a command letter. Returns false if taken or the table is full.
 * Call AFTER uart_shell_init() (which resets the table). */
bool fm_shell_register(char cmd_letter, fm_shell_handler_t handler);

/* Creates the shell task. Call after fm_uart_init(). */
void uart_shell_init(void);

/* Recovery: delete and re-create the shell task (fm_recover_fn_t). */
void uart_shell_restart(uint8_t comp_id);
void *uart_shell_task_handle(void);

#ifdef __cplusplus
}
#endif

#endif /* UART_SHELL_H */

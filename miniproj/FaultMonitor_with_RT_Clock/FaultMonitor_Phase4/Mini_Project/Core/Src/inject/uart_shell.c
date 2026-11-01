/*
 * uart_shell.c -- see uart_shell.h. Owner: Abhishek.
 */
#include "uart_shell.h"
#include "fm_uart.h"
#include "fault_inject.h"
#include "fm_common.h"
#include "app_config.h"
#include "fm_trace.h"

#include "cmsis_os.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define FM_SHELL_MAX_HANDLERS 24U
#define FM_SHELL_LINE_LEN     96U
#define FM_SHELL_POLL_MS     100U

typedef struct {
    char               cmd_letter;
    fm_shell_handler_t handler;
} shell_entry_t;

static shell_entry_t s_handlers[FM_SHELL_MAX_HANDLERS];
static uint32_t      s_handler_count;
static osThreadId_t  s_task;

static const osThreadAttr_t s_attr = {
    .name = "shell", .stack_size = STACK_SHELL * 4, .priority = PRIO_SHELL,
};

bool fm_shell_register(char cmd_letter, fm_shell_handler_t handler)
{
    if (s_handler_count >= FM_SHELL_MAX_HANDLERS) {
        return false;
    }
    for (uint32_t i = 0; i < s_handler_count; i++) {
        if (s_handlers[i].cmd_letter == cmd_letter) {
            return false;
        }
    }
    s_handlers[s_handler_count].cmd_letter = cmd_letter;
    s_handlers[s_handler_count].handler    = handler;
    s_handler_count++;
    return true;
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    c = (char)toupper((unsigned char)c);
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* "$<body>*hh": checks the XOR checksum, strips the framing in place.
 * Returns false (and reports a comm error) on a bad frame. */
static bool unframe(char *line)
{
    char *star = strrchr(line, '*');
    if (star == NULL || hexval(star[1]) < 0 || hexval(star[2]) < 0) {
        fm_comm_error(COMP_PER_UART, FM_COMM_CRC);
        return false;
    }
    uint8_t cs = 0;
    for (char *p = line + 1; p < star; p++) {
        cs ^= (uint8_t)*p;
    }
    uint8_t want = (uint8_t)(hexval(star[1]) * 16 + hexval(star[2]));
    if (cs != want) {
        fm_comm_error(COMP_PER_UART, FM_COMM_CRC);
        return false;
    }
    *star = '\0';
    memmove(line, line + 1, strlen(line));
    return true;
}

static void dispatch(char *line)
{
    if (line[0] == '$' && !unframe(line)) {
        fm_uart_printf("ERR frame checksum\r\n");
        return;
    }
    char cmd = (char)toupper((unsigned char)line[0]);
    const char *args = line + 1;
    while (*args == ' ') {
        args++;
    }
    for (uint32_t i = 0; i < s_handler_count; i++) {
        if (s_handlers[i].cmd_letter == cmd) {
            s_handlers[i].handler(args);
            return;
        }
    }
    fm_uart_printf("ERR unknown command '%c' (H for help)\r\n", line[0]);
}

static void shell_task_fn(void *arg)
{
    (void)arg;
    char     line[FM_SHELL_LINE_LEN];
    uint32_t len = 0;

    for (;;) {
        int c = fm_uart_getc(FM_SHELL_POLL_MS);
        inject_point(COMP_SHELL);
        if (c < 0) {
            continue;                       /* idle tick */
        }
        if (c == '\r' || c == '\n') {
            if (len > 0U) {
                line[len] = '\0';
                fm_uart_write((const uint8_t *)"\r\n", 2);
                dispatch(line);
                len = 0;
            }
            continue;
        }
        if ((c == 0x08 || c == 0x7F) && len > 0U) {   /* backspace */
            len--;
            fm_uart_write((const uint8_t *)"\b \b", 3);
            continue;
        }
        if (len < FM_SHELL_LINE_LEN - 1U && c >= 0x20 && c < 0x7F) {
            line[len++] = (char)c;
            uint8_t ch = (uint8_t)c;
            fm_uart_write(&ch, 1);           /* echo */
        }
    }
}

/* 'H' -- help */
static void cmd_help(const char *args)
{
    (void)args;
    fm_uart_printf("--- application ---\r\n"
                   " T <mV>         alarm limit 100..3200 mV (button B1 cycles 1000/1650/2000/2500)\r\n"
                   " Y 1|0          telemetry line on/off\r\n"
                   " Q [1|0]        sensor plausibility check: PA0 at 0 V/3.3 V > 1 s = SENSOR FAULT\r\n");
    fm_uart_printf("--- fault monitor ---\r\n"
                   " S              status: live per-component signature + overhead\r\n"
                   " V 1|0          stream one SIG line per 100 ms window (CSV)\r\n"
                   " D              dump the execution trace ring buffer (CSV)\r\n");
    fm_uart_printf(" L [n]          learn normal behaviour from n windows (default 100)\r\n"
                   " B              print the learned baseline\r\n"
                   " R [seq]        list stored fault reports / one with its history\r\n"
                   " C              clear the fault log in flash\r\n");
    fm_uart_printf(" M 0|1          localizer: 0 rule-based, 1 statistical\r\n"
                   " K <sigma>      detector sensitivity (default 4)\r\n");
    fm_uart_printf("--- fault injection / examiner ---\r\n"
                   " I              list faults;  I <fault> <comp> [delay_ms]\r\n"
                   " X              cancel a pending injection\r\n"
                   " G              ground-truth log of injections (CSV)\r\n");
    fm_uart_printf(" W <addr> <val> write a 32-bit word anywhere (hex), e.g. W 40000028 53\r\n"
                   " P <comp> <prio> change a task's priority (8 low .. 48 realtime)\r\n"
                   " $<cmd>*hh      framed command with XOR checksum\r\n");
}

void *uart_shell_task_handle(void) { return s_task; }

void uart_shell_restart(uint8_t comp_id)
{
    (void)comp_id;
    if (s_task) {
        fm_uart_forget_task(s_task);
        osThreadTerminate(s_task);
    }
    s_task = osThreadNew(shell_task_fn, NULL, &s_attr);
    fm_register_task(COMP_SHELL, s_task, "shell", uart_shell_restart);
}

void uart_shell_init(void)
{
    /* no table reset here: modules (controller T/Y) may have registered
     * their commands before the shell task is created */
    fm_shell_register('H', cmd_help);
    s_task = osThreadNew(shell_task_fn, NULL, &s_attr);
    fm_register_task(COMP_SHELL, s_task, "shell", uart_shell_restart);
}

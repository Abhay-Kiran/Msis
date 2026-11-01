/*
 * idle_stats.c -- see idle_stats.h.
 *
 * Integration: define vApplicationIdleHook() exactly once in the project
 * (here: main.c, USER CODE 4) calling idle_stats_on_idle_hook().
 */
#include "idle_stats.h"
#include "fm_types.h"      /* FM_ENABLE_MONITOR */
#include "fm_uart.h"
#include "uart_shell.h"
#include "cmsis_os.h"

static volatile uint32_t s_idle_count;
static uint32_t          s_start_tick;

void idle_stats_init(void)
{
    s_idle_count = 0;
    s_start_tick = osKernelGetTickCount();
}

void idle_stats_on_idle_hook(void)
{
    s_idle_count++;
}

uint32_t idle_stats_get_count(void)
{
    return s_idle_count;
}

uint32_t idle_stats_get_uptime_ms(void)
{
    return osKernelGetTickCount() - s_start_tick;
}

/* With the monitor compiled out (FM_ENABLE_MONITOR=0) the framework's 'S'
 * command does not exist, but the overhead comparison needs the IDLE line
 * from THIS build too. So the reference build gets a minimal 'S'. */
#if !FM_ENABLE_MONITOR
static void cmd_idle_status(const char *args)
{
    (void)args;
    fm_uart_printf("monitor: OFF (overhead reference build)\r\n");
    fm_uart_printf("IDLE,%lu,%lu\r\n", (unsigned long)idle_stats_get_count(),
                   (unsigned long)idle_stats_get_uptime_ms());
}

void idle_stats_register_shell(void)
{
    fm_shell_register('S', cmd_idle_status);
}
#else
void idle_stats_register_shell(void)
{
    /* monitor build: 'S' (fm_sigout.c) already prints the IDLE line */
}
#endif

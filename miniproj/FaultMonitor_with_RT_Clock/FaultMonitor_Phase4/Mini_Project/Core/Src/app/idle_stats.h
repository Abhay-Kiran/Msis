/*
 * idle_stats.h -- lightweight CPU-headroom counter for the INJ-7 /
 * Phase 5 overhead comparison (FM_ENABLE_MONITOR on vs off).
 *
 * Standard RTOS technique: the idle hook increments a counter as fast
 * as possible whenever no task is ready to run. More spare CPU time ->
 * more idle-hook calls per second. It is a *relative* measure, not a
 * calibrated percentage, so:
 *
 *   1. Flash the FM_ENABLE_MONITOR=0 build, run for a fixed window
 *      (e.g. 30 s), read the idle count via the 'S' shell command.
 *   2. Flash the FM_ENABLE_MONITOR=1 build, same window, read again.
 *   3. Overhead % ~= 100 * (idle_off - idle_on) / idle_off.
 *
 * tools/fm_metrics.py --overhead automates step 3 given the two numbers.
 */
#ifndef IDLE_STATS_H
#define IDLE_STATS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Call once from main(), any time before osKernelStart(). */
void idle_stats_init(void);

/* Must be called from vApplicationIdleHook() (configUSE_IDLE_HOOK must
 * be 1 in FreeRTOSConfig.h -- CubeMX exposes this under FREERTOS ->
 * Config parameters -> USE_IDLE_HOOK). */
void idle_stats_on_idle_hook(void);

uint32_t idle_stats_get_count(void);
uint32_t idle_stats_get_uptime_ms(void);

/* Registers a minimal 'S' (IDLE line only) in the FM_ENABLE_MONITOR=0
 * build. Call after uart_shell_init(). No-op in the monitor build. */
void idle_stats_register_shell(void);

#ifdef __cplusplus
}
#endif

#endif /* IDLE_STATS_H */

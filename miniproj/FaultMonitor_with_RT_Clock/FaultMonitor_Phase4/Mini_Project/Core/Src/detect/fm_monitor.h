/*
 * fm_monitor.h -- the Monitor task: the framework's heartbeat. Owner: Arfan.
 *
 * Every FM_WINDOW_MS (100 ms) it:
 *   1. closes the capture window -> one sig_vector_t (fm_trace.c)
 *   2. adds each task's free stack (fm_capture.c)
 *   3. checks in with the watchdog (fm_watchdog.c)
 *   4. LEARNING  : feeds the window to the baseline learner (fm_learn.c);
 *                  after N windows saves the baseline to flash sector 6
 *      MONITORING: runs both localizers (fm_detect.c) on the window
 *   5. on a fault: builds a fault_report_t (type, component, confidence,
 *      execution history, PC), appends it to the flash black box
 *      (fm_blackbox.c), prints an alert, runs the recovery ladder
 *      (fm_recover.c)
 *   6. publishes the window for the 'S' / 'V' commands (fm_sigout.c)
 *
 * At boot, before the first window, it turns anything left in .noinit RAM
 * (a trap's crash record, a watchdog reset) into a stored report.
 *
 * It runs at osPriorityHigh, above every application task. That is a
 * design requirement: if a high-priority task gets stuck in a loop, a
 * monitor below it would never run again and could never report it.
 *
 * Shell commands: L [n] learn, B baseline, R [seq] reports, C clear log,
 *                 M [0|1] localizer, K [sigma] sensitivity.
 */
#ifndef FM_MONITOR_H
#define FM_MONITOR_H

#include <stdint.h>
#include "fm_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FM_LEARN_WINDOWS_DEFAULT 100U   /* 10 s of normal operation      */
#define FM_WARMUP_WINDOWS         10U   /* ignored after boot (1 s)      */
#define FM_GRACE_WINDOWS           3U   /* ignored after a recovery      */

typedef enum {
    FM_MODE_WARMUP = 0,
    FM_MODE_LEARN,
    FM_MODE_MONITOR,
} fm_mode_t;

/* Creates the monitor task and registers it as component self_comp_id.
 * Call after every application component is registered, before
 * osKernelStart(). */
void fm_monitor_init(uint8_t self_comp_id);

/* Used by the 'S' command. */
void     fm_monitor_print_status(void);
uint32_t fm_monitor_ram_bytes(void);

#ifdef __cplusplus
}
#endif

#endif /* FM_MONITOR_H */

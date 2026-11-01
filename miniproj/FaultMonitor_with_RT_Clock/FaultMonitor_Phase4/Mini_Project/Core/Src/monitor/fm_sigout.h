/*
 * fm_sigout.h -- signature output over UART. Owner: Abhay.
 *
 *   S      print the latest window as a readable table (one-shot)
 *   V 1    start streaming every window as one CSV line ("SIG,...")
 *   V 0    stop streaming
 *
 * Printing is done by a LOW-priority task fed through a queue, never by
 * the high-priority monitor task: a 300-character line takes ~25 ms at
 * 115200 baud, and the monitor must never be held up by the UART. If the
 * printer falls behind, windows are dropped from the stream (counted and
 * reported as "SIGDROP,n") -- detection itself is unaffected.
 *
 * CSV format (one line per window, only registered components):
 *   SIG,<window>,<t_len_cycles>,<comp>:<runs>:<cpu>:<max_slice>:<max_gap>:<stack_free>,...,
 *       T:<i>><j>=<n>;...,E:<comm>:<periph>:<trap>,P:<comp>=<errs>;...,
 *       R:<comp>:<fault_type>:<conf>,Q:<comp>:<fault_type>:<conf>,L:<comp>:<fault_id>,M:<mode>
 *   R = rule-based verdict, Q = statistical verdict (255 = no fault),
 *   L = ground-truth label (the injected fault still active, 255 = none),
 *   M = monitor mode (0 warm-up, 1 learning, 2 monitoring).
 * tools/fm_dump.py and tools/fm_ml.py parse this.
 */
#ifndef FM_SIGOUT_H
#define FM_SIGOUT_H

#include "fm_types.h"

#if FM_ENABLE_MONITOR

#ifdef __cplusplus
extern "C" {
#endif

/* Creates the printer task and registers 'S' and 'V'. Call after
 * uart_shell_init(), before the scheduler starts. */
void fm_sigout_init(void);

/* Everything the monitor knows about one window. */
typedef struct {
    sig_vector_t v;
    loc_result_t rule;          /* rule-based verdict                    */
    loc_result_t stat;          /* statistical verdict                   */
    uint8_t      label_comp;    /* ground truth (FM_COMP_NONE = normal)  */
    uint8_t      label_fault;
    uint8_t      mode;
    uint8_t      reserved;
} fm_pub_t;

/* Implemented by the monitor (weak defaults in fm_sigout.c). */
void     fm_monitor_print_status(void);
uint32_t fm_monitor_ram_bytes(void);

/* Called by the monitor task once per closed window. Never blocks. */
void fm_sigout_publish(const fm_pub_t *p);

#ifdef __cplusplus
}
#endif

#endif /* FM_ENABLE_MONITOR */
#endif /* FM_SIGOUT_H */

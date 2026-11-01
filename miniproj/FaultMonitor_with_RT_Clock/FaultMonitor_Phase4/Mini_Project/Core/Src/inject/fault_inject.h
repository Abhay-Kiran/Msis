/*
 * fault_inject.h -- Fault injection: the ground truth. Owner: Abhishek.
 *
 * A deployed device does not get faults on command, so how do we prove
 * the monitor works? We create REAL faults inside the running firmware
 * and record exactly what we did and when (the ground truth). The
 * evaluation compares that log with what the monitor concluded.
 *
 * Every fault is real, not a flag: a stuck loop really spins, a stack
 * overflow really recurses into the MPU guard, a register really gets
 * scrambled. The monitor is never told what was injected -- it only
 * sees the effects, exactly as with a genuine bug.
 *
 *   I <fault> <comp> [delay_ms]       e.g. "I 1 0" = stuck loop in ctrl
 *
 * Task faults fire inside the target task at its next inject_point()
 * (on its own stack, in its own context). Peripheral / ISR / UART faults
 * fire from the 1 kHz TIM6 tick. One fault armed at a time.
 *
 * Examiner commands, for unscripted faults:
 *   W <hex addr> <hex value>   write any word (a register, the protected
 *                              config, a NULL pointer...)
 *   P <comp> <priority>        change a task's priority at run time
 */
#ifndef FAULT_INJECT_H
#define FAULT_INJECT_H

#include "fm_common.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void fault_inject_init(void);
/* Registers I, X, G, W, P. Call after uart_shell_init(). */
void fault_inject_register_shell(void);

bool inject_arm(fault_id_t id, uint8_t comp_id, uint16_t delay_ms);
void inject_clear(void);

/* Every application task calls this once per loop. Fires a pending task
 * fault aimed at this task. Returns a fault the TASK must act out itself
 * (FAULT_SEQ_REPEAT in the control task), else FAULT_NONE. */
fault_id_t inject_point(uint8_t comp_id);

/* From the TIM6 interrupt (1 kHz): fires pending peripheral/ISR/UART faults. */
void fault_inject_on_tick(void);

uint32_t inject_log_count(void);
const inject_log_t *inject_log_entry(uint32_t index);

#ifdef __cplusplus
}
#endif

#endif /* FAULT_INJECT_H */

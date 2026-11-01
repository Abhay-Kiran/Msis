/*
 * fm_recover.h -- Recovery ladder. Owner: Arfan.
 *
 * Decides WHAT to do about a localized fault, then does it:
 *
 *   level 0  the component's own recovery: restart the task, or
 *            re-initialise the peripheral / the ISR's source
 *   level 1  same again, plus re-initialise every peripheral
 *   level 2  system reset (NVIC_SystemReset)
 *
 * The level goes up when the SAME component needs recovery again within
 * FM_ESCALATE_MS -- the cheap fix didn't hold, so try a bigger one. Faults
 * that already caused a reset (MPU traps, watchdog) are not recovered
 * here: the reset was the recovery.
 *
 * The recovery functions themselves belong to the application (it knows
 * how to restart its own tasks); they are handed in at registration
 * (fm_register_task / _isr / _periph in fm_trace.h).
 */
#ifndef FM_RECOVER_H
#define FM_RECOVER_H

#include <stdint.h>
#include "fm_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FM_ESCALATE_MS   10000U

/* Chooses the action for this fault (does not execute it). */
fm_action_t fm_recover_plan(uint8_t comp_id, uint8_t fault_type, uint32_t now_ms);
/* Executes it. ACT_SYSTEM_RESET does not return. */
void fm_recover_execute(uint8_t comp_id, fm_action_t act);
uint8_t fm_recover_level(uint8_t comp_id);

#ifdef __cplusplus
}
#endif

#endif /* FM_RECOVER_H */

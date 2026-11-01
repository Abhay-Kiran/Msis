/*
 * controller.h -- the application: a real-time sensor monitor with an
 * over-limit alarm.
 *
 * Pipeline (the problem statement's A -> B -> C -> D -> E):
 *
 *   sensor task, every 10 ms, woken by the ADC/DMA interrupt:
 *     A acquire : take the latest block of 10 ADC samples
 *     B filter  : average the block, smooth it, convert to millivolts
 *     C compare : is the value above the limit? (with hysteresis)
 *     D actuate : switch the alarm LED
 *   report task, every 100 ms:
 *     E report  : status line with checksum (printed once per second)
 *
 * Deadline: a new block arrives every 10 ms. If the sensor task is late
 * or stuck, blocks are lost and the alarm is late. These are exactly the
 * misbehaviours the monitor is built to catch.
 *
 * Inputs: button B1 cycles the limit 1000/1650/2000/2500 mV; the shell
 * command "T <mV>" sets any limit; "Y 0/1" telemetry off/on.
 * Wire a jumper from A0 to GND (low) or 3V3 (high) to test the alarm.
 */
#ifndef CONTROLLER_H
#define CONTROLLER_H

#include <stdint.h>
#include "fm_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Read-only configuration, protected by an MPU region after boot: any
 * write to it is a memory-access fault. Exactly 32 bytes, 32-aligned. */
typedef struct {
    uint32_t hyst_mv;          /* alarm clears this far BELOW the limit     */
    uint32_t vref_mv;          /* ADC reference voltage (3300 on the Nucleo)*/
    uint32_t adc_max;          /* 4095 for a 12-bit ADC                      */
    uint32_t alpha_q8;         /* smoothing factor in 1/256 units            */
    uint32_t limit_min_mv;     /* smallest limit the user may set            */
    uint32_t limit_max_mv;     /* largest limit the user may set             */
    uint32_t block_len;        /* samples per block                          */
    uint32_t reserved;
} app_cfg_t;
_Static_assert(sizeof(app_cfg_t) == 32, "MPU region needs exactly 32 bytes");

/* Creates the sensor and report tasks, registers tasks and stages with the
 * framework, protects the configuration. Call after app_periph_init(). */
void controller_init(void);

/* Recovery: delete and re-create one of the two tasks. */
void controller_restart(uint8_t comp_id);

uint32_t app_task_period_ms(uint8_t comp_id);
void    *app_task_handle(uint8_t comp_id);
volatile app_cfg_t *app_cfg(void);

#ifdef __cplusplus
}
#endif
#endif /* CONTROLLER_H */

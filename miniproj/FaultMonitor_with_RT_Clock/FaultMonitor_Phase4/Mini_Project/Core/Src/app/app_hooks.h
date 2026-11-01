/*
 * app_hooks.h -- what the framework's fault injector needs from the
 * application (implemented in controller.c and app_periph.c).
 * Owner: Abhishek.
 *
 * This is the whole "porting surface" of fault_inject.c: nothing in
 * /inject, /monitor or /detect names an application-specific register or
 * task, so the framework can be reused with another application by
 * re-implementing only these functions.
 */
#ifndef APP_HOOKS_H
#define APP_HOOKS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Banner text, e.g. "sensor monitor". */
const char *app_name(void);

/* Normal period of a periodic task (MISSED_DEADLINE sleeps 10x this). */
uint32_t app_task_period_ms(uint8_t comp_id);

/* FreeRTOS handle of an application task ('P' command), NULL if none. */
void *app_task_handle(uint8_t comp_id);

/* A word inside the application's MPU read-only configuration region:
 * MEM_RO_WRITE writes 0 here and must take a MemManage fault. */
volatile uint32_t *app_protected_word(void);

/* PERIPH_CORRUPT: is `comp` a peripheral this application lets you
 * corrupt, and corrupt one of its checksummed configuration registers. */
bool app_periph_injectable(uint8_t comp);
void app_corrupt_periph(uint8_t comp);

/* Shared TIM6 1 kHz tick (app_tick.c). */
void     app_tick_isr(void);
uint32_t app_ms(void);
void     app_tim6_storm(bool on);
void     app_tim6_reinit(uint8_t comp_id);

#ifdef __cplusplus
}
#endif
#endif /* APP_HOOKS_H */

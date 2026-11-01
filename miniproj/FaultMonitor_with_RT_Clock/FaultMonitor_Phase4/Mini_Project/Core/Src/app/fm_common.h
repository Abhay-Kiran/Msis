/*
 * fm_common.h -- APPLICATION-side shared IDs. Owner: Abhishek.
 *
 * The framework (Core/Inc/fm_types.h) knows components only as numbers
 * 0..FM_MAX_COMP-1 and stages as 0..FM_MAX_STAGES-1. This file says what
 * those numbers mean for THIS application (the sensor monitor), and
 * lists the faults the injector can create (the ground truth used by the
 * evaluation). Changing the application means changing this file, not
 * the framework.
 */
#ifndef FM_COMMON_H
#define FM_COMMON_H

#include <stdint.h>
#include "fm_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Components (what can be blamed) ------------------------------- */
typedef enum {
    /* tasks */
    COMP_CTRL = 0,      /* sensor task: stages A..D every 10 ms           */
    COMP_REPORT,        /* report task: stage E (telemetry) every 100 ms   */
    COMP_SHELL,         /* UART command shell                              */
    COMP_MONITOR,       /* the framework's own monitor task                */
    COMP_WATCHDOG,      /* the framework's watchdog-feeding task           */
    /* interrupts */
    COMP_ISR_ADC,       /* DMA2 Stream0: 10-sample block ready (100 Hz)    */
    COMP_ISR_TIM6,      /* 1 kHz system tick of the application            */
    COMP_ISR_USART2,    /* UART receive                                    */
    COMP_ISR_BUTTON,    /* user button B1                                  */
    /* peripherals (checked by register CRC + status flags) */
    COMP_PER_ADC,       /* ADC1 + its DMA stream                           */
    COMP_PER_TIM,       /* TIM2: the 1 kHz ADC trigger                     */
    COMP_PER_GPIO,      /* GPIOA pins PA0 (analog in) and PA5 (LED out)    */
    COMP_PER_UART,      /* USART2                                          */
    COMP_COUNT
} app_comp_id_t;
_Static_assert(COMP_COUNT <= FM_MAX_COMP, "too many components for FM_MAX_COMP");

/* ---- Pipeline stages: the problem statement's A -> B -> C -> D -> E -- */
typedef enum {
    STAGE_A_ACQUIRE = 0,   /* take the 10-sample block                      */
    STAGE_B_FILTER,        /* average + smooth, convert to millivolts       */
    STAGE_C_COMPARE,       /* compare with the limit (with hysteresis)      */
    STAGE_D_ACTUATE,       /* drive the alarm LED                           */
    STAGE_E_REPORT,        /* telemetry frame (separate task)               */
    STAGE_COUNT
} app_stage_t;

/* ---- Injectable faults (ground truth) --------------------------------
 * FAULT_NONE must stay 0. Each fault lists what the framework should
 * conclude, which the evaluation script uses to score localization.
 *                                      expected detection        blame */
typedef enum {
    FAULT_NONE = 0,
    FAULT_STUCK_LOOP,       /* task spins forever        EXEC_OVERRUN    target task */
    FAULT_STACK_OVERFLOW,   /* deep recursion            STACK_OVERFLOW  target task */
    FAULT_MISSED_DEADLINE,  /* task oversleeps 10x       LIVENESS        target task */
    FAULT_ISR_STORM,        /* TIM6 rate x10 + heavy ISR ISR_RATE        TIM6 ISR    */
    FAULT_MEM_RO_WRITE,     /* write to MPU-protected config  MEM_ACCESS target task */
    FAULT_NULL_DEREF,       /* write through NULL         MEM_ACCESS     target task */
    FAULT_PERIPH_CORRUPT,   /* scramble a config register PERIPH         target periph */
    FAULT_UART_CORRUPT,     /* corrupted command frame    COMM           UART        */
    FAULT_WDG_HANG,         /* IRQs off + spin           WATCHDOG        target task */
    FAULT_SEQ_REPEAT,       /* stage C runs 3x: A,B,C,C,C SEQUENCE       control task */
    NUM_FAULT_IDS
} fault_id_t;

extern const char *const g_fault_names[NUM_FAULT_IDS];
extern const char *const g_comp_names[COMP_COUNT];
extern const char *const g_stage_names[STAGE_COUNT];

/* ---- Injection log record (ground truth, 'G' command) -------------- */
typedef struct __attribute__((packed)) {
    uint32_t   cycle_ts;       /* DWT->CYCCNT when the fault was armed/fired */
    uint8_t    comp_id;        /* target component                           */
    uint8_t    fault_id;
    uint16_t   delay_ms;       /* requested arm-to-fire delay, 0 on the fire record */
} inject_log_t;

#ifdef __cplusplus
}
#endif

#endif /* FM_COMMON_H */

/*
 * fm_capture.h -- Capture layer, hardware side. Owner: Abhay.
 *
 * Everything that needs Cortex-M4 / STM32F446 registers rather than the
 * RTOS lives here (this is the "architecture + chip port" layer):
 *
 *   - Reset cause    : RCC_CSR read once at boot (watchdog events signal)
 *   - Crash record   : a struct in .noinit RAM that survives a reset, so a
 *                      fault caught in an exception handler can be turned
 *                      into a flash report after the reboot
 *   - Fault traps    : HardFault / MemManage / BusFault / UsageFault entry
 *                      (memory-access anomalies + stack overflow signals)
 *   - MPU            : null-pointer guard, read-only config region, and a
 *                      32-byte no-access guard at the bottom of each
 *                      monitored task's stack (stack usage signal)
 *   - Stack scan     : free-stack measurement that skips the guard
 *
 * Porting to another Cortex-M3/M4/M7 chip means revisiting this file;
 * fm_trace.c and everything in detect/ stay as they are.
 */
#ifndef FM_CAPTURE_H
#define FM_CAPTURE_H

#include <stdint.h>
#include <stdbool.h>
#include "fm_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Which exception trapped (fm_crash_t.trap). */
typedef enum {
    FM_TRAP_NONE = 0,
    FM_TRAP_HARDFAULT,
    FM_TRAP_MEMMANAGE,
    FM_TRAP_BUSFAULT,
    FM_TRAP_USAGEFAULT,
    FM_TRAP_STACK_HOOK,     /* FreeRTOS vApplicationStackOverflowHook */
} fm_trap_t;

/* Survives a reset (not a power cycle). Written by the trap handler and
 * by the trace hooks; read once by the monitor after the reboot. */
#define FM_NOINIT_MAGIC 0x464D4E49UL   /* "FMNI" */
typedef struct {
    uint32_t magic;
    /* continuously updated while running -- used after a watchdog reset */
    uint8_t  cur_comp;          /* component running at the last switch   */
    uint8_t  last_stage;        /* last pipeline stage entered            */
    uint8_t  suspect_comp;      /* monitor's top suspect in last window   */
    uint8_t  planned_reset;     /* 1 = the monitor itself reset the MCU   */
    uint32_t inject_ts;         /* last injection (ground truth)          */
    uint8_t  inject_comp;
    uint8_t  inject_fault;
    uint8_t  inject_valid;
    uint8_t  reserved1;
    /* crash record -- valid when crash_valid == 1 */
    uint8_t  crash_valid;
    uint8_t  trap;              /* fm_trap_t                              */
    uint8_t  fault_type;        /* fm_fault_type_t decided in the handler */
    uint8_t  comp_id;           /* component running when it trapped      */
    uint32_t cycle_ts;
    uint32_t pc;                /* stacked PC (0 if not recoverable)      */
    uint32_t lr;                /* stacked LR                             */
    uint32_t cfsr;              /* SCB->CFSR                              */
    uint32_t hfsr;
    uint32_t fault_addr;        /* MMFAR / BFAR when valid                */
    event_t  history[FM_HISTORY_LEN];
    uint32_t boot_count;
} fm_noinit_t;

extern fm_noinit_t g_fm_noinit;

/* ---- Boot ---------------------------------------------------------- */
/* Call first thing after HAL_Init(): reads and clears RCC_CSR, validates
 * the .noinit block (a power-on reset invalidates it). */
void     fm_capture_boot(void);
uint16_t fm_reset_cause(void);          /* FM_RST_* flags of this boot    */
const char *fm_reset_cause_str(uint16_t flags);

/* ---- Fault traps ---------------------------------------------------- */
/* Called from the USER CODE section at the very top of each fault
 * handler in stm32f4xx_it.c with the EXC_RETURN value (LR on entry).
 * Records a crash record in .noinit and resets the MCU. Never returns. */
void fm_trap(fm_trap_t kind, uint32_t exc_return) __attribute__((noreturn));

/* Called from vApplicationStackOverflowHook (FreeRTOS's own check). */
void fm_trap_stack_hook(void *task_handle) __attribute__((noreturn));

/* ---- MPU ------------------------------------------------------------- */
/* Enable MemManage/BusFault/UsageFault, relocate VTOR to flash, set the
 * null guard. Call once before the scheduler starts. */
void fm_mpu_init(void);
/* Make [addr, addr+size) read-only. size: power of two >= 32, addr
 * aligned to size. Returns false if no region is free or args invalid. */
bool fm_mpu_protect_ro(const void *addr, uint32_t size);
/* (Re)place the stack guard for a registered task. Called by
 * fm_register_task(); safe to call again after a task restart. */
void fm_mpu_guard_task(uint8_t comp_id, void *task_handle);
/* Remove a task's guard before the task is deleted (its stack goes back
 * to the heap, and the next allocation must be able to write there). */
void fm_mpu_unguard_task(uint8_t comp_id);
/* Free stack of a task in words, measured from just above its guard. */
uint32_t fm_stack_free_words(uint8_t comp_id);

/* PC where a (preempted) task currently is, read from the context that
 * FreeRTOS saved on its stack. For a stuck task this is the address of
 * the loop it is stuck in -- arm-none-eabi-addr2line turns it into a
 * file:line. Returns 0 if it can't be read safely. */
uint32_t fm_task_pc(void *task_handle);

#ifdef __cplusplus
}
#endif

#endif /* FM_CAPTURE_H */

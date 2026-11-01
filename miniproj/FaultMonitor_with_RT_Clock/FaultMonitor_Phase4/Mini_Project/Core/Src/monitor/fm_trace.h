/*
 * fm_trace.h -- Capture layer: component registry, trace hooks and the
 * application-facing instrumentation API. Owner: Abhay.
 *
 * This is the only header the application needs in order to be monitored:
 *
 *     fm_trace_init();                                      // once, first
 *     fm_register_task (COMP_CTRL, handle, "ctrl", restart_fn);
 *     fm_register_isr  (COMP_ISR_ADC, "ADC_DMA", true, NULL);
 *     fm_register_periph(COMP_PER_ADC, "ADC1", regs, n, reinit_fn);
 *     fm_register_stage(STAGE_C_CONTROL, COMP_CTRL, "pid_update");
 *     FM_ISR_ENTER(COMP_ISR_ADC); ... FM_ISR_EXIT(COMP_ISR_ADC);
 *     FM_STAGE(STAGE_C_CONTROL);
 *
 * Task switches are captured automatically by FreeRTOS's
 * traceTASK_SWITCHED_IN/OUT macros (FreeRTOSConfig.h, USER CODE Defines)
 * which call fm_hook_task_in/out() with the task's tag.
 *
 * Every event does two O(1) things, safe from any context:
 *   1. it is written into the RAM ring buffer -- the rolling execution
 *      history ("flight recorder") that a fault report copies from;
 *   2. it updates this window's live counters, which fm_sig_close_window()
 *      turns into one sig_vector_t every FM_WINDOW_MS.
 * Detection never depends on draining the ring buffer, so the buffer is
 * free to overwrite its oldest events.
 *
 * With FM_ENABLE_MONITOR == 0 every function here becomes an empty
 * inline, so the application builds and runs with zero monitoring cost.
 */
#ifndef FM_TRACE_H
#define FM_TRACE_H

#include <stdint.h>
#include <stdbool.h>
#include "fm_types.h"
#include "ring_buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Recovery callback: restart a task / re-initialise a peripheral. */
typedef void (*fm_recover_fn_t)(uint8_t comp_id);

/* One register to include in a peripheral's configuration checksum. Only
 * the bits in `mask` are checked, so status bits that legitimately change
 * (enable flags, counters) are left out. */
typedef struct {
    volatile uint32_t *reg;
    uint32_t           mask;
} fm_reg_check_t;

typedef struct {
    fm_kind_t             kind;
    const char           *name;
    void                 *handle;      /* TaskHandle_t for tasks           */
    bool                  history;     /* write events to the ring buffer  */
    uint8_t               priority;    /* FreeRTOS priority (tasks)        */
    fm_recover_fn_t       recover;     /* NULL = no component-level recovery */
    const fm_reg_check_t *regs;        /* peripherals: registers to checksum */
    uint8_t               n_regs;
    bool                  is_comm;     /* errors count as communication errors */
} fm_comp_info_t;

typedef struct {
    uint8_t     owner;                 /* component that runs the stage    */
    const char *name;                  /* function name shown in reports   */
} fm_stage_info_t;

/* Last injection seen (ground truth marker, for latency and labels). */
typedef struct {
    uint32_t cycle_ts;
    uint8_t  comp_id;
    uint8_t  fault_id;
    bool     valid;
} fm_injection_t;

extern ring_buffer_t   g_fm_rb;
extern fm_comp_info_t  g_fm_comp[FM_MAX_COMP];
extern fm_stage_info_t g_fm_stage[FM_MAX_STAGES];

/* CRC-32 (IEEE 802.3, reflected, poly 0xEDB88320). Shared utility used
 * for peripheral checksums, the baseline and the fault reports. */
uint32_t fm_crc32(uint32_t crc, const void *data, uint32_t len);

#if FM_ENABLE_MONITOR

void fm_trace_init(void);
void fm_register_task(uint8_t comp_id, void *task_handle, const char *name,
                      fm_recover_fn_t restart);
void fm_register_isr(uint8_t comp_id, const char *name, bool history,
                     fm_recover_fn_t reinit);
void fm_register_periph(uint8_t comp_id, const char *name,
                        const fm_reg_check_t *regs, uint8_t n_regs,
                        fm_recover_fn_t reinit, bool is_comm);
void fm_register_stage(uint8_t stage, uint8_t owner, const char *name);

void fm_trace_event(uint8_t comp_id, uint8_t type, uint16_t extra);
void fm_isr_enter(uint8_t comp_id);
void fm_isr_exit(uint8_t comp_id);
void fm_stage(uint8_t stage);

/* Peripheral status-flag error (ADC overrun, DMA transfer error...). */
void fm_periph_error(uint8_t comp_id, uint16_t flags);
/* Communication error (UART ORE/FE/NE, frame checksum). */
void fm_comm_error(uint8_t comp_id, uint16_t flags);
/* Ground truth: the injector calls this when a fault actually fires. */
void fm_note_injection(uint8_t comp_id, uint8_t fault_id);
fm_injection_t fm_last_injection(void);

void fm_hook_task_in(uint32_t tag);
void fm_hook_task_out(uint32_t tag);

void fm_trace_freeze(bool frozen);
void fm_sig_close_window(sig_vector_t *out);
uint8_t fm_current_comp(void);
uint8_t fm_last_stage(void);
void fm_trace_register_shell(void);

/* Reset all window counters and the stage tracker (after a recovery, so
 * the half-window before it isn't judged). */
void fm_trace_restart_window(void);

#define FM_ISR_ENTER(id)  fm_isr_enter((uint8_t)(id))
#define FM_ISR_EXIT(id)   fm_isr_exit((uint8_t)(id))
#define FM_STAGE(s)       fm_stage((uint8_t)(s))

#else  /* FM_ENABLE_MONITOR == 0: everything compiles to nothing */

static inline void fm_trace_init(void) {}
static inline void fm_register_task(uint8_t a, void *b, const char *c, fm_recover_fn_t d) { (void)a; (void)b; (void)c; (void)d; }
static inline void fm_register_isr(uint8_t a, const char *b, bool c, fm_recover_fn_t d) { (void)a; (void)b; (void)c; (void)d; }
static inline void fm_register_periph(uint8_t a, const char *b, const fm_reg_check_t *c, uint8_t d, fm_recover_fn_t e, bool f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; }
static inline void fm_register_stage(uint8_t a, uint8_t b, const char *c) { (void)a; (void)b; (void)c; }
static inline void fm_trace_event(uint8_t a, uint8_t b, uint16_t c) { (void)a; (void)b; (void)c; }
static inline void fm_periph_error(uint8_t a, uint16_t b) { (void)a; (void)b; }
static inline void fm_comm_error(uint8_t a, uint16_t b) { (void)a; (void)b; }
static inline void fm_note_injection(uint8_t a, uint8_t b) { (void)a; (void)b; }
static inline void fm_trace_register_shell(void) {}
static inline uint8_t fm_current_comp(void) { return FM_COMP_OTHER; }
#define FM_ISR_ENTER(id)  ((void)0)
#define FM_ISR_EXIT(id)   ((void)0)
#define FM_STAGE(s)       ((void)0)

#endif /* FM_ENABLE_MONITOR */

#ifdef __cplusplus
}
#endif

#endif /* FM_TRACE_H */

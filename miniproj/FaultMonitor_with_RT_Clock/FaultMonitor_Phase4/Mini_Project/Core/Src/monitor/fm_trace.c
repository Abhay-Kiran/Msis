/*
 * fm_trace.c -- see fm_trace.h. Owner: Abhay.
 *
 * Concurrency: the task hooks run inside the FreeRTOS context switch
 * (BASEPRI-masked), ISR hooks run in interrupt context and can nest, and
 * fm_sig_close_window() runs in the monitor task. Every function that
 * touches the live counters does so inside a short PRIMASK critical
 * section (save/restore, never a blind enable), so any of them can
 * interrupt any other safely. Each section is a handful of instructions.
 */
#include "fm_trace.h"
#include "fm_capture.h"

#include "main.h"          /* DWT, __get_PRIMASK, SystemCoreClock */
#include "FreeRTOS.h"
#include "task.h"

#include <string.h>

/* ---- CRC-32 (always built: the black box uses it too) -------------- */
uint32_t fm_crc32(uint32_t crc, const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    crc = ~crc;
    while (len--) {
        crc ^= *p++;
        for (int b = 0; b < 8; b++) {
            crc = (crc >> 1) ^ (0xEDB88320UL & (0U - (crc & 1U)));
        }
    }
    return ~crc;
}

#if FM_ENABLE_MONITOR

#include "uart_shell.h"
#include "fm_uart.h"

/* The ring buffer lives in .noinit too: after a watchdog reset the last
 * events before the hang are still there to be copied into the report. */
ring_buffer_t   g_fm_rb __attribute__((section(".noinit")));
fm_comp_info_t  g_fm_comp[FM_MAX_COMP];
fm_stage_info_t g_fm_stage[FM_MAX_STAGES];

/* ---- Live counters for the current window -------------------------- */
typedef struct {
    uint32_t runs;
    uint32_t cpu_cycles;
    uint32_t max_slice;
    uint32_t max_gap;
} acc_t;

static acc_t    s_acc[FM_MAX_COMP];
static uint32_t s_slice_start[FM_MAX_COMP]; /* task switch-in / ISR enter ts  */
static uint32_t s_last_act[FM_MAX_COMP];    /* last activation ts, for gaps   */
static uint8_t  s_seen[FM_MAX_COMP];        /* activated at least once        */
static uint16_t s_periph_err[FM_MAX_COMP];
static uint16_t s_comm_err_count;
static uint16_t s_comm_err_flags;

static uint8_t  s_trans[FM_MAX_STAGES][FM_MAX_STAGES];
static uint8_t  s_last_stage = 0xFF;               /* any task, for reports */
/* Last stage run BY EACH component: transitions are counted per task, so
 * a pipeline split over two tasks (A-D in ctrl, E in report) is judged
 * task by task and their interleaving never looks like a fault.
 * Index FM_MAX_COMP = unregistered code. */
static uint8_t  s_last_stage_by[FM_MAX_COMP + 1U];

static uint8_t  s_cur_task = FM_COMP_OTHER;
#define ISR_NEST_MAX 8U
static uint8_t  s_isr_stack[ISR_NEST_MAX];
static uint8_t  s_isr_depth;

static uint32_t s_window_start;
static uint32_t s_window_idx;
static volatile bool s_frozen;
static fm_injection_t s_inject;

static inline uint32_t now(void) { return DWT->CYCCNT; }

static inline uint32_t enter_critical(void)
{
    uint32_t pm = __get_PRIMASK();
    __disable_irq();
    return pm;
}

static inline void exit_critical(uint32_t pm) { __set_PRIMASK(pm); }

static inline bool valid(uint8_t id) { return id < FM_MAX_COMP; }

static void record(uint32_t ts, uint8_t comp, uint8_t type, uint16_t extra)
{
    if (s_frozen) {
        return;
    }
    if (valid(comp) && !g_fm_comp[comp].history) {
        return;
    }
    event_t e = { .cycle_ts = ts, .comp_id = comp, .type = type, .extra = extra };
    rb_push(&g_fm_rb, &e);
}

/* Activation bookkeeping shared by tasks and ISRs. Caller holds PRIMASK. */
static void activate(uint8_t id, uint32_t ts)
{
    if (s_seen[id]) {
        uint32_t gap = ts - s_last_act[id];
        if (gap > s_acc[id].max_gap) {
            s_acc[id].max_gap = gap;
        }
    }
    s_seen[id]        = 1;
    s_last_act[id]    = ts;
    s_slice_start[id] = ts;
    s_acc[id].runs++;
}

static uint32_t finish_slice(uint8_t id, uint32_t ts)
{
    uint32_t slice = ts - s_slice_start[id];
    s_acc[id].cpu_cycles += slice;
    if (slice > s_acc[id].max_slice) {
        s_acc[id].max_slice = slice;
    }
    return slice;
}

/* ---- Init / registry ------------------------------------------------ */

void fm_trace_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;  /* enable DWT       */
    DWT->CTRL        |= DWT_CTRL_CYCCNTENA_Msk;      /* start CYCCNT      */

    /* Watchdog reset (not a trap, which already saved its own history):
     * salvage the history that survived in RAM before clearing it. */
    if ((fm_reset_cause() & FM_RST_IWDG) && !g_fm_noinit.crash_valid) {
        rb_copy_last(&g_fm_rb, g_fm_noinit.history, FM_HISTORY_LEN);
    }
    rb_init(&g_fm_rb);
    memset(g_fm_comp, 0, sizeof(g_fm_comp));
    memset(g_fm_stage, 0, sizeof(g_fm_stage));
    for (uint32_t i = 0; i < FM_MAX_STAGES; i++) {
        g_fm_stage[i].owner = FM_COMP_NONE;
    }
    memset(s_acc, 0, sizeof(s_acc));
    memset(s_seen, 0, sizeof(s_seen));
    memset(s_trans, 0, sizeof(s_trans));
    memset(s_periph_err, 0, sizeof(s_periph_err));
    memset(&s_inject, 0, sizeof(s_inject));
    s_comm_err_count = 0;
    s_comm_err_flags = 0;
    s_last_stage     = 0xFF;
    memset(s_last_stage_by, 0xFF, sizeof(s_last_stage_by));
    s_cur_task       = FM_COMP_OTHER;
    s_isr_depth      = 0;
    s_window_idx     = 0;
    s_window_start   = now();
    s_frozen         = false;
}

void fm_register_task(uint8_t comp_id, void *task_handle, const char *name,
                      fm_recover_fn_t restart)
{
    if (!valid(comp_id) || task_handle == NULL) {
        return;
    }
    fm_comp_info_t *c = &g_fm_comp[comp_id];
    c->kind     = FM_KIND_TASK;
    c->name     = name;
    c->handle   = task_handle;
    c->history  = true;
    c->recover  = restart;
    c->priority = (uint8_t)uxTaskPriorityGet((TaskHandle_t)task_handle);
    /* The tag the trace macros see: 0 means "unregistered". */
    vTaskSetTaskNumber((TaskHandle_t)task_handle, (UBaseType_t)comp_id + 1U);
    /* (Re)place the MPU guard at the bottom of this task's stack. */
    fm_mpu_guard_task(comp_id, task_handle);
}

void fm_register_isr(uint8_t comp_id, const char *name, bool history,
                     fm_recover_fn_t reinit)
{
    if (!valid(comp_id)) {
        return;
    }
    fm_comp_info_t *c = &g_fm_comp[comp_id];
    c->kind     = FM_KIND_ISR;
    c->name     = name;
    c->handle   = NULL;
    c->history  = history;
    c->recover  = reinit;
    c->priority = 0xFF;      /* above every task */
}

void fm_register_periph(uint8_t comp_id, const char *name,
                        const fm_reg_check_t *regs, uint8_t n_regs,
                        fm_recover_fn_t reinit, bool is_comm)
{
    if (!valid(comp_id)) {
        return;
    }
    fm_comp_info_t *c = &g_fm_comp[comp_id];
    c->kind     = FM_KIND_PERIPH;
    c->name     = name;
    c->history  = true;
    c->recover  = reinit;
    c->regs     = regs;
    c->n_regs   = n_regs;
    c->is_comm  = is_comm;
    c->priority = 0xFF;
}

void fm_register_stage(uint8_t stage, uint8_t owner, const char *name)
{
    if (stage < FM_MAX_STAGES) {
        g_fm_stage[stage].owner = owner;
        g_fm_stage[stage].name  = name;
    }
}

/* ---- Hooks ------------------------------------------------------------ */

static inline uint8_t tag_to_comp(uint32_t tag)
{
    return (tag == 0U || tag > FM_MAX_COMP) ? FM_COMP_OTHER : (uint8_t)(tag - 1U);
}

void fm_hook_task_in(uint32_t tag)
{
    uint8_t  id = tag_to_comp(tag);
    uint32_t ts = now();
    uint32_t pm = enter_critical();

    s_cur_task = id;
    g_fm_noinit.cur_comp = id;          /* survives a watchdog reset */
    if (valid(id)) {
        activate(id, ts);
    }
    record(ts, id, EVT_TASK_IN, 0);

    exit_critical(pm);
}

void fm_hook_task_out(uint32_t tag)
{
    uint8_t  id = tag_to_comp(tag);
    uint32_t ts = now();
    uint32_t pm = enter_critical();

    if (valid(id) && s_seen[id]) {
        finish_slice(id, ts);
    }
    record(ts, id, EVT_TASK_OUT, 0);
    s_cur_task = FM_COMP_OTHER;

    exit_critical(pm);
}

void fm_isr_enter(uint8_t comp_id)
{
    uint32_t ts = now();
    uint32_t pm = enter_critical();

    if (s_isr_depth < ISR_NEST_MAX) {
        s_isr_stack[s_isr_depth] = comp_id;
    }
    s_isr_depth++;
    if (valid(comp_id)) {
        activate(comp_id, ts);
    }
    record(ts, comp_id, EVT_ISR_ENTER, 0);

    exit_critical(pm);
}

void fm_isr_exit(uint8_t comp_id)
{
    uint32_t ts = now();
    uint32_t pm = enter_critical();

    uint32_t us = 0;
    if (valid(comp_id) && s_seen[comp_id]) {
        uint32_t slice = finish_slice(comp_id, ts);
        us = slice / (SystemCoreClock / 1000000U);
    }
    if (s_isr_depth > 0U) {
        s_isr_depth--;
    }
    record(ts, comp_id, EVT_ISR_EXIT, (uint16_t)(us > 0xFFFFU ? 0xFFFFU : us));

    exit_critical(pm);
}

void fm_trace_event(uint8_t comp_id, uint8_t type, uint16_t extra)
{
    uint32_t ts = now();
    uint32_t pm = enter_critical();
    record(ts, comp_id, type, extra);
    exit_critical(pm);
}

void fm_stage(uint8_t stage)
{
    if (stage >= FM_MAX_STAGES) {
        return;
    }
    uint32_t ts = now();
    uint32_t pm = enter_critical();

    uint8_t cur  = fm_current_comp();
    uint8_t slot = valid(cur) ? cur : (uint8_t)FM_MAX_COMP;
    uint8_t prev = s_last_stage_by[slot];
    if (prev < FM_MAX_STAGES && s_trans[prev][stage] < 0xFFU) {
        s_trans[prev][stage]++;
    }
    s_last_stage_by[slot] = stage;
    s_last_stage = stage;
    g_fm_noinit.last_stage = stage;
    record(ts, cur, EVT_STAGE, stage);

    exit_critical(pm);
}

void fm_periph_error(uint8_t comp_id, uint16_t flags)
{
    uint32_t ts = now();
    uint32_t pm = enter_critical();
    if (valid(comp_id) && s_periph_err[comp_id] < 0xFFFFU) {
        s_periph_err[comp_id]++;
    }
    record(ts, comp_id, EVT_PERIPH_ERR, flags);
    exit_critical(pm);
}

void fm_comm_error(uint8_t comp_id, uint16_t flags)
{
    uint32_t ts = now();
    uint32_t pm = enter_critical();
    if (valid(comp_id) && s_periph_err[comp_id] < 0xFFFFU) {
        s_periph_err[comp_id]++;
    }
    if (s_comm_err_count < 0xFFFFU) {
        s_comm_err_count++;
    }
    s_comm_err_flags |= flags;
    record(ts, comp_id, EVT_COMM_ERR, flags);
    exit_critical(pm);
}

void fm_note_injection(uint8_t comp_id, uint8_t fault_id)
{
    uint32_t ts = now();
    uint32_t pm = enter_critical();
    s_inject.cycle_ts = ts;
    s_inject.comp_id  = comp_id;
    s_inject.fault_id = fault_id;
    s_inject.valid    = true;
    g_fm_noinit.inject_ts    = ts;
    g_fm_noinit.inject_comp  = comp_id;
    g_fm_noinit.inject_fault = fault_id;
    g_fm_noinit.inject_valid = 1U;
    /* bypass the history filter: ground truth always goes in */
    if (!s_frozen) {
        event_t e = { .cycle_ts = ts, .comp_id = comp_id, .type = EVT_INJECT, .extra = fault_id };
        rb_push(&g_fm_rb, &e);
    }
    exit_critical(pm);
}

fm_injection_t fm_last_injection(void)
{
    uint32_t pm = enter_critical();
    fm_injection_t r = s_inject;
    exit_critical(pm);
    return r;
}

uint8_t fm_current_comp(void)
{
    if (s_isr_depth > 0U && s_isr_depth <= ISR_NEST_MAX) {
        return s_isr_stack[s_isr_depth - 1U];
    }
    return s_cur_task;
}

uint8_t fm_last_stage(void)
{
    return s_last_stage;
}

void fm_trace_freeze(bool frozen)
{
    s_frozen = frozen;
}

/* ---- Window close: live counters -> signature vector ----------------- */

static uint32_t periph_hash(const fm_comp_info_t *c)
{
    uint32_t crc = 0;
    for (uint32_t r = 0; r < c->n_regs; r++) {
        uint32_t v = *c->regs[r].reg & c->regs[r].mask;
        crc = fm_crc32(crc, &v, sizeof(v));
    }
    return crc;
}

void fm_sig_close_window(sig_vector_t *out)
{
    uint32_t ts = now();
    uint32_t pm = enter_critical();

    memset(out, 0, sizeof(*out));
    out->window_idx = s_window_idx++;
    out->t_start    = s_window_start;
    out->t_len      = ts - s_window_start;

    for (uint32_t i = 0; i < FM_MAX_COMP; i++) {
        comp_sig_t *c = &out->comp[i];
        c->runs       = (uint16_t)(s_acc[i].runs > 0xFFFFU ? 0xFFFFU : s_acc[i].runs);
        c->cpu_cycles = s_acc[i].cpu_cycles;
        c->max_slice  = s_acc[i].max_slice;
        c->max_gap    = s_acc[i].max_gap;
        /* A component that has gone quiet shows no gap inside the window,
         * only silence at its end -- count that too, or a hung task would
         * look perfectly calm. */
        if (s_seen[i]) {
            uint32_t silence = ts - s_last_act[i];
            if (silence > c->max_gap) {
                c->max_gap = silence;
            }
        }
        c->stack_free_min  = 0xFFFFU;
        out->periph_err[i] = s_periph_err[i];
    }
    memcpy(out->trans, s_trans, sizeof(s_trans));
    out->comm_err_count = s_comm_err_count;
    uint32_t perr = 0;
    for (uint32_t i = 0; i < FM_MAX_COMP; i++) {
        perr += s_periph_err[i];            /* status-flag + comm errors */
    }
    out->periph_err_count = (uint16_t)(perr > 0xFFFFU ? 0xFFFFU : perr);
    out->comm_err_flags = s_comm_err_flags;

    memset(s_acc, 0, sizeof(s_acc));
    memset(s_trans, 0, sizeof(s_trans));
    memset(s_periph_err, 0, sizeof(s_periph_err));
    s_comm_err_count = 0;
    s_comm_err_flags = 0;
    s_window_start = ts;

    exit_critical(pm);

    /* Register reads are done outside the critical section: they are
     * slow-ish (CRC) and nothing else writes these registers normally. */
    for (uint32_t i = 0; i < FM_MAX_COMP; i++) {
        if (g_fm_comp[i].kind == FM_KIND_PERIPH && g_fm_comp[i].n_regs) {
            out->periph_hash[i] = periph_hash(&g_fm_comp[i]);
        }
    }
}

void fm_trace_restart_window(void)
{
    uint32_t ts = now();
    uint32_t pm = enter_critical();
    memset(s_acc, 0, sizeof(s_acc));
    memset(s_trans, 0, sizeof(s_trans));
    memset(s_periph_err, 0, sizeof(s_periph_err));
    s_comm_err_count = 0;
    s_comm_err_flags = 0;
    s_last_stage     = 0xFF;
    memset(s_last_stage_by, 0xFF, sizeof(s_last_stage_by));
    for (uint32_t i = 0; i < FM_MAX_COMP; i++) {
        if (s_seen[i]) {
            s_last_act[i] = ts;   /* restarted components start fresh */
        }
    }
    s_window_start = ts;
    exit_critical(pm);
}

/* ---- 'D' shell command: dump the ring buffer as CSV ----------------- */

static void cmd_dump_rb(const char *args)
{
    (void)args;
    fm_trace_freeze(true);

    uint32_t n = rb_count(&g_fm_rb);
    fm_uart_printf("#FMDUMP,v%u,clk=%lu,n=%lu,overwritten=%lu\r\n",
                   (unsigned)FM_IFACE_VERSION, (unsigned long)SystemCoreClock,
                   (unsigned long)n, (unsigned long)g_fm_rb.dropped);
    for (uint32_t i = 0; i < FM_MAX_COMP; i++) {
        if (g_fm_comp[i].kind != FM_KIND_UNUSED) {
            fm_uart_printf("#C,%lu,%u,%s\r\n", (unsigned long)i,
                           (unsigned)g_fm_comp[i].kind, g_fm_comp[i].name);
        }
    }
    for (uint32_t i = 0; i < FM_MAX_STAGES; i++) {
        if (g_fm_stage[i].owner != FM_COMP_NONE) {
            fm_uart_printf("#S,%lu,%u,%s\r\n", (unsigned long)i,
                           g_fm_stage[i].owner, g_fm_stage[i].name);
        }
    }
    fm_uart_printf("idx,cycle_ts,comp,type,extra\r\n");

    event_t e;
    for (uint32_t i = 0; i < n; i++) {
        if (!rb_peek(&g_fm_rb, i, &e)) {
            break;
        }
        fm_uart_printf("%lu,%lu,%u,%u,%u\r\n", (unsigned long)i,
                       (unsigned long)e.cycle_ts, e.comp_id, e.type, e.extra);
    }
    fm_uart_printf("END\r\n");

    fm_trace_freeze(false);
}

void fm_trace_register_shell(void)
{
    fm_shell_register('D', cmd_dump_rb);
}

#endif /* FM_ENABLE_MONITOR */

/*
 * fault_inject.c -- see fault_inject.h. Owner: Abhishek.
 */
#include "fault_inject.h"
#include "app_config.h"
#include "app_hooks.h"
#include "fm_trace.h"
#include "fm_uart.h"
#include "uart_shell.h"

#include "main.h"
#include "cmsis_os.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *const g_fault_names[NUM_FAULT_IDS] = {
    [FAULT_NONE]            = "NONE",
    [FAULT_STUCK_LOOP]      = "STUCK_LOOP",
    [FAULT_STACK_OVERFLOW]  = "STACK_OVERFLOW",
    [FAULT_MISSED_DEADLINE] = "MISSED_DEADLINE",
    [FAULT_ISR_STORM]       = "ISR_STORM",
    [FAULT_MEM_RO_WRITE]    = "MEM_RO_WRITE",
    [FAULT_NULL_DEREF]      = "NULL_DEREF",
    [FAULT_PERIPH_CORRUPT]  = "PERIPH_CORRUPT",
    [FAULT_UART_CORRUPT]    = "UART_CORRUPT",
    [FAULT_WDG_HANG]        = "WDG_HANG",
    [FAULT_SEQ_REPEAT]      = "SEQ_REPEAT",
};

/* Which targets make sense for each fault, and what the monitor should
 * conclude (printed by 'I' so the audience knows what to expect).
 *
 *   TGT_TASK      any application task: 0 sensor, 1 report, 2 shell
 *   TGT_PERIODIC  a PERIODIC task only: 0 sensor, 1 report. The shell is
 *                 event-driven, so the detector has no liveness rule for
 *                 it and a missed deadline there could never be detected.
 *   TGT_CTRL      only task 0 (it owns the pipeline / state-machine stages)
 *   TGT_TIM6      only the TIM6 interrupt component (6)
 *   TGT_PERIPH    a peripheral the application allows (app_periph_injectable():
 *                 9 ADC1, 10 TIM2, 11 GPIOA, 12 USART2)
 *   TGT_UART      only USART2 (12)
 *
 * The framework's own tasks (3 monitor, 4 wdg) are never valid targets:
 * the monitor does not judge itself (see configure_detector()). */
typedef enum { TGT_TASK, TGT_PERIODIC, TGT_CTRL, TGT_TIM6, TGT_PERIPH, TGT_UART } tgt_t;
static const struct { tgt_t tgt; const char *expect; } s_info[NUM_FAULT_IDS] = {
    [FAULT_NONE]            = { TGT_TASK,     "-" },
    [FAULT_STUCK_LOOP]      = { TGT_TASK,     "EXEC_OVERRUN, restart task" },
    [FAULT_STACK_OVERFLOW]  = { TGT_TASK,     "STACK_OVERFLOW via MPU guard, reset + report" },
    [FAULT_MISSED_DEADLINE] = { TGT_PERIODIC, "LIVENESS, restart task" },
    [FAULT_ISR_STORM]       = { TGT_TIM6,     "ISR_RATE on tim6_isr, reinit TIM6" },
    [FAULT_MEM_RO_WRITE]    = { TGT_TASK,     "MEM_ACCESS (MPU read-only), reset + report" },
    [FAULT_NULL_DEREF]      = { TGT_TASK,     "MEM_ACCESS (MPU null guard), reset + report" },
    [FAULT_PERIPH_CORRUPT]  = { TGT_PERIPH,   "PERIPH config CRC, reinit peripheral" },
    [FAULT_UART_CORRUPT]    = { TGT_UART,     "COMM frame checksum, reinit USART2" },
    [FAULT_WDG_HANG]        = { TGT_TASK,     "WATCHDOG reset (~4 s), report after reboot" },
    [FAULT_SEQ_REPEAT]      = { TGT_CTRL,     "SEQUENCE (stage out of order), restart task 0" },
};

/* Is fault f compiled in / armable in this build at all? */
static bool fault_enabled(fault_id_t f)
{
    if (f <= FAULT_NONE || f >= NUM_FAULT_IDS) {
        return false;
    }
#if FM_PHASE3_ONLY
    /* MVP-2 scope: only the two faults agreed for Phase 3 are armable.
     * This is a safety gate, not merely a shorter help menu. */
    if (f != FAULT_STUCK_LOOP && f != FAULT_STACK_OVERFLOW) {
        return false;
    }
#endif
    return true;
}

/* Is `comp` a valid target for this target class? (single source of
 * truth: used by the check AND by the 'I' listing / error messages) */
static bool tgt_accepts(tgt_t t, uint8_t comp)
{
    switch (t) {
    case TGT_TASK:     return comp == COMP_CTRL || comp == COMP_REPORT || comp == COMP_SHELL;
    case TGT_PERIODIC: return comp == COMP_CTRL || comp == COMP_REPORT;
    case TGT_CTRL:     return comp == COMP_CTRL;
    case TGT_TIM6:     return comp == COMP_ISR_TIM6;
    case TGT_PERIPH:   return app_periph_injectable(comp);
    case TGT_UART:     return comp == COMP_PER_UART;
    }
    return false;
}

static bool target_ok(fault_id_t f, uint8_t comp)
{
    return fault_enabled(f) && comp < COMP_COUNT && tgt_accepts(s_info[f].tgt, comp);
}

/* Prints the valid component IDs for fault f, e.g. "0=sensor 1=report". */
static void print_targets(fault_id_t f)
{
    for (uint8_t c = 0; c < COMP_COUNT; c++) {
        if (tgt_accepts(s_info[f].tgt, c)) {
            fm_uart_printf(" %u=%s", c, g_comp_names[c]);
        }
    }
}

/* First valid component for fault f (used for the example command). */
static uint8_t first_target(fault_id_t f)
{
    for (uint8_t c = 0; c < COMP_COUNT; c++) {
        if (tgt_accepts(s_info[f].tgt, c)) {
            return c;
        }
    }
    return 0;
}

/* ---- Armed fault (shared by tasks and the TIM6 ISR: PRIMASK guarded) - */
typedef struct {
    bool       pending;
    fault_id_t fault_id;
    uint8_t    comp_id;
    uint32_t   fire_at;          /* osKernelGetTickCount() */
} armed_fault_t;

static volatile armed_fault_t s_armed;

/* ---- Ground-truth log ---------------------------------------------- */
#define INJECT_LOG_CAPACITY 64U
static inject_log_t s_log[INJECT_LOG_CAPACITY];
static uint32_t     s_log_head, s_log_count;

static void log_append(fault_id_t id, uint8_t comp, uint16_t delay_ms)
{
    uint32_t pm = __get_PRIMASK();
    __disable_irq();
    inject_log_t *e = &s_log[s_log_head];
    e->cycle_ts = DWT->CYCCNT;
    e->comp_id  = comp;
    e->fault_id = (uint8_t)id;
    e->delay_ms = delay_ms;
    s_log_head = (s_log_head + 1U) % INJECT_LOG_CAPACITY;
    if (s_log_count < INJECT_LOG_CAPACITY) {
        s_log_count++;
    }
    __set_PRIMASK(pm);
}

uint32_t inject_log_count(void) { return s_log_count; }

const inject_log_t *inject_log_entry(uint32_t index)
{
    if (index >= s_log_count) {
        return NULL;
    }
    uint32_t start = (s_log_count < INJECT_LOG_CAPACITY) ? 0U : s_log_head;
    return &s_log[(start + index) % INJECT_LOG_CAPACITY];
}

void fault_inject_init(void)
{
    /* DWT->CYCCNT is the shared clock of every timestamp; fm_trace_init()
     * enables it too, this keeps the monitor-off build working. */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL        |= DWT_CTRL_CYCCNTENA_Msk;
    memset((void *)&s_armed, 0, sizeof(s_armed));
    s_log_head = s_log_count = 0;
}

bool inject_arm(fault_id_t id, uint8_t comp, uint16_t delay_ms)
{
    if (id <= FAULT_NONE || id >= NUM_FAULT_IDS || !target_ok(id, comp)) {
        return false;
    }
    bool ok = false;
    uint32_t pm = __get_PRIMASK();
    __disable_irq();
    if (!s_armed.pending) {
        s_armed.pending  = true;
        s_armed.fault_id = id;
        s_armed.comp_id  = comp;
        s_armed.fire_at  = osKernelGetTickCount() + delay_ms;
        ok = true;
    }
    __set_PRIMASK(pm);
    if (ok) {
        log_append(id, comp, delay_ms);       /* the arm record */
    }
    return ok;
}

void inject_clear(void)
{
    uint32_t pm = __get_PRIMASK();
    __disable_irq();
    s_armed.pending = false;
    __set_PRIMASK(pm);
}

/* Takes the armed fault if it is for `comp` and due. */
static fault_id_t take(uint8_t comp)
{
    fault_id_t f = FAULT_NONE;
    uint32_t pm = __get_PRIMASK();
    __disable_irq();
    if (s_armed.pending && s_armed.comp_id == comp &&
        (int32_t)(osKernelGetTickCount() - s_armed.fire_at) >= 0) {
        f = s_armed.fault_id;
        s_armed.pending = false;
    }
    __set_PRIMASK(pm);
    if (f != FAULT_NONE) {
        log_append(f, comp, 0);               /* the fire record */
        fm_note_injection(comp, (uint8_t)f);  /* ground truth into the trace */
    }
    return f;
}

/* ---- The faults (task faults run on the target task's own stack) ---- */

static void __attribute__((noinline)) do_stuck_loop(void)
{
    for (;;) {
        __NOP();         /* interrupts stay on: only this task is stuck */
    }
}

static volatile uint32_t s_sink;
/* Infinite on purpose: it ends when the stack hits the MPU guard. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winfinite-recursion"
static uint32_t __attribute__((noinline)) do_stack_overflow(uint32_t depth)
{
    volatile uint8_t eat[64];           /* 64 bytes of stack per level */
    for (uint32_t i = 0; i < sizeof(eat); i++) {
        eat[i] = (uint8_t)depth;
    }
    s_sink = do_stack_overflow(depth + 1U) + eat[0];   /* not a tail call */
    return s_sink;
}
#pragma GCC diagnostic pop

static void do_wdg_hang(void)
{
    __disable_irq();     /* nothing else can run, not even the monitor */
    for (;;) {
        __NOP();
    }
}

fault_id_t inject_point(uint8_t comp)
{
    fault_id_t f = take(comp);
    switch (f) {
    case FAULT_STUCK_LOOP:      do_stuck_loop();                                   break;
    case FAULT_STACK_OVERFLOW:  (void)do_stack_overflow(0);                        break;
    case FAULT_MISSED_DEADLINE: osDelay(10U * app_task_period_ms(comp));           break;
    case FAULT_MEM_RO_WRITE:    *app_protected_word() = 0U;                        break;
    case FAULT_NULL_DEREF:      *(volatile uint32_t *)0x00000010UL = 0xDEADBEEFUL; break;
    case FAULT_WDG_HANG:        do_wdg_hang();                                     break;
    case FAULT_SEQ_REPEAT:      return f;          /* acted out by the task */
    default: break;
    }
    return FAULT_NONE;
}

void fault_inject_on_tick(void)
{
    if (!s_armed.pending) {
        return;
    }
    uint8_t comp = s_armed.comp_id;
    tgt_t   t    = s_info[s_armed.fault_id].tgt;
    if (t != TGT_TIM6 && t != TGT_PERIPH && t != TGT_UART) {
        return;                                    /* a task fault */
    }
    fault_id_t f = take(comp);
    switch (f) {
    case FAULT_ISR_STORM:      app_tim6_storm(true);              break;
    case FAULT_PERIPH_CORRUPT: app_corrupt_periph(comp);          break;
    case FAULT_UART_CORRUPT:   fm_uart_inject_rx("$T 55*00\r");   break; /* bad checksum */
    default: break;
    }
}

/* ---- Shell commands ------------------------------------------------- */

/* 'I' -- list; 'I <fault> <comp> [delay_ms]' -- arm */
static void cmd_inject(const char *args)
{
    int f = -1, c = -1, d = 0;
    int n = sscanf(args, "%d %d %d", &f, &c, &d);
    if (n < 2) {
        fm_uart_printf("faults:  I <fault> <comp> [delay_ms]   (delay 0..60000, default 0)\r\n");
        for (int i = 1; i < NUM_FAULT_IDS; i++) {
            if (!fault_enabled((fault_id_t)i)) {
                continue;            /* hidden in the MVP-2 (FM_PHASE3_ONLY) build */
            }
            fm_uart_printf(" %2d %-16s -> %s\r\n", i, g_fault_names[i], s_info[i].expect);
            fm_uart_printf("      valid comp:");
            print_targets((fault_id_t)i);
            fm_uart_printf("   e.g. I %d %u 1000\r\n", i, first_target((fault_id_t)i));
        }
        fm_uart_printf("components:");
        for (uint8_t k = 0; k < COMP_COUNT; k++) {
            fm_uart_printf(" %u=%s", k, g_comp_names[k]);
        }
        fm_uart_printf("\r\n(3=monitor and 4=wdg belong to the framework and cannot be targeted)\r\n");
        return;
    }
    if (f <= FAULT_NONE || f >= NUM_FAULT_IDS) {
        fm_uart_printf("ERR unknown fault %d (valid 1..%d, type I for the list)\r\n",
                       f, NUM_FAULT_IDS - 1);
        return;
    }
    if (!fault_enabled((fault_id_t)f)) {
        fm_uart_printf("ERR fault %d %s is disabled in this build (FM_PHASE3_ONLY=1)\r\n",
                       f, g_fault_names[f]);
        return;
    }
    if (d < 0 || d > 60000) {
        fm_uart_printf("ERR delay %d ms out of range 0..60000\r\n", d);
        return;
    }
    if (c < 0 || c >= COMP_COUNT || !target_ok((fault_id_t)f, (uint8_t)c)) {
        fm_uart_printf("ERR fault %d %s cannot target comp %d (%s); valid comp:", f,
                       g_fault_names[f], c,
                       (c >= 0 && c < COMP_COUNT) ? g_comp_names[c] : "unknown");
        print_targets((fault_id_t)f);
        fm_uart_printf("\r\n");
        return;
    }
    if (!inject_arm((fault_id_t)f, (uint8_t)c, (uint16_t)d)) {
        fm_uart_printf("ERR a fault is already pending (X cancels it)\r\n");
        return;
    }
    fm_uart_printf("OK armed %s on %s in %d ms; expect: %s\r\n", g_fault_names[f],
                   g_comp_names[c], d, s_info[f].expect);
}

static void cmd_cancel(const char *args)
{
    (void)args;
    inject_clear();
    fm_uart_printf("OK cancelled\r\n");
}

/* 'G' -- ground-truth log as CSV */
static void cmd_ground_truth(const char *args)
{
    (void)args;
    fm_uart_printf("cycle_ts,comp_id,fault_id,delay_ms\r\n");
    for (uint32_t i = 0; i < inject_log_count(); i++) {
        const inject_log_t *e = inject_log_entry(i);
        fm_uart_printf("%lu,%u,%u,%u\r\n", (unsigned long)e->cycle_ts, e->comp_id,
                       e->fault_id, e->delay_ms);
    }
    fm_uart_printf("END\r\n");
}

/* 'W <hex addr> <hex value>' -- examiner: write any 32-bit word */
static void cmd_write(const char *args)
{
    char *end;
    uint32_t addr = strtoul(args, &end, 16);
    uint32_t val  = strtoul(end, NULL, 16);
    if (end == args || (addr & 3U)) {
        fm_uart_printf("ERR usage: W <hex addr, 4-aligned> <hex value>\r\n");
        return;
    }
    fm_uart_printf("OK writing 0x%08lx to 0x%08lx\r\n", (unsigned long)val, (unsigned long)addr);
    osDelay(20);                                  /* let the line go out */
    *(volatile uint32_t *)addr = val;
}

/* 'P <comp> <priority>' -- examiner: change a task's priority */
static void cmd_priority(const char *args)
{
    int c = -1, p = -1;
    if (sscanf(args, "%d %d", &c, &p) != 2 || c < 0 || c >= COMP_COUNT ||
        p < osPriorityLow || p > osPriorityRealtime || app_task_handle((uint8_t)c) == NULL) {
        fm_uart_printf("ERR usage: P <0 ctrl|1 report|2 shell> <8..48>\r\n");
        return;
    }
    osThreadSetPriority((osThreadId_t)app_task_handle((uint8_t)c), (osPriority_t)p);
#if FM_ENABLE_MONITOR
    g_fm_comp[c].priority = (uint8_t)p;   /* victim suppression uses it */
#endif
    fm_uart_printf("OK %s priority %d\r\n", g_comp_names[c], p);
}

void fault_inject_register_shell(void)
{
    fm_shell_register('I', cmd_inject);
    fm_shell_register('X', cmd_cancel);
    fm_shell_register('G', cmd_ground_truth);
    fm_shell_register('W', cmd_write);
    fm_shell_register('P', cmd_priority);
}

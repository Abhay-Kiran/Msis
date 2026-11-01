/*
 * fm_sigout.c -- see fm_sigout.h. Owner: Abhay.
 */
#include "fm_sigout.h"

#if FM_ENABLE_MONITOR
#include "fm_trace.h"
#include "fm_uart.h"
#include "uart_shell.h"

#include "main.h"
#include "cmsis_os.h"

#include "idle_stats.h"
#include "fm_capture.h"
#include "FreeRTOS.h"
#include "task.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Provided by the monitor (detect/fm_monitor.c) when it is built in; these
 * weak defaults keep the capture layer buildable and testable on its own. */
__attribute__((weak)) void fm_monitor_print_status(void)
{
    fm_uart_printf("monitor: capture only (detection not built in)\r\n");
}
__attribute__((weak)) uint32_t fm_monitor_ram_bytes(void)
{
    return (uint32_t)(sizeof(g_fm_rb) + sizeof(g_fm_noinit) + sizeof(g_fm_comp));
}

#define SIGOUT_QUEUE_LEN  4U
#define SIGOUT_LINE_LEN   640U

static osMessageQueueId_t s_q;
static fm_pub_t           s_latest;       /* for 'S'                        */
static osMutexId_t        s_latest_mtx;
static volatile bool      s_stream;
static volatile uint32_t  s_dropped;
static char               s_line[SIGOUT_LINE_LEN];

static uint32_t cyc_to_us(uint32_t cyc)
{
    return cyc / (SystemCoreClock / 1000000U);
}

/* Appends with bounds checking; returns new length. */
static size_t append(char *buf, size_t len, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));
static size_t append(char *buf, size_t len, const char *fmt, ...)
{
    if (len >= SIGOUT_LINE_LEN - 1U) {
        return len;
    }
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + len, SIGOUT_LINE_LEN - len, fmt, ap);
    va_end(ap);
    if (n < 0) {
        return len;
    }
    len += (size_t)n;
    return len < SIGOUT_LINE_LEN ? len : SIGOUT_LINE_LEN - 1U;
}

static void print_csv(const fm_pub_t *p)
{
    const sig_vector_t *v = &p->v;
    size_t len = 0;
    len = append(s_line, len, "SIG,%lu,%lu", (unsigned long)v->window_idx,
                 (unsigned long)v->t_len);
    for (uint32_t i = 0; i < FM_MAX_COMP; i++) {
        if (g_fm_comp[i].kind == FM_KIND_UNUSED) {
            continue;
        }
        const comp_sig_t *c = &v->comp[i];
        len = append(s_line, len, ",%lu:%u:%lu:%lu:%lu:%u", (unsigned long)i,
                     c->runs, (unsigned long)c->cpu_cycles,
                     (unsigned long)c->max_slice, (unsigned long)c->max_gap,
                     c->stack_free_min);
    }
    len = append(s_line, len, ",T:");
    for (uint32_t i = 0; i < FM_MAX_STAGES; i++) {
        for (uint32_t j = 0; j < FM_MAX_STAGES; j++) {
            if (v->trans[i][j]) {
                len = append(s_line, len, "%lu>%lu=%u;", (unsigned long)i,
                             (unsigned long)j, v->trans[i][j]);
            }
        }
    }
    /* trap_count is always 0 while running: a trap resets the MCU and is
     * reported at the next boot instead. Kept so the CSV format is fixed. */
    len = append(s_line, len, ",E:%u:%u:%u,P:", v->comm_err_count,
                 v->periph_err_count, v->trap_count);
    for (uint32_t i = 0; i < FM_MAX_COMP; i++) {
        if (v->periph_err[i]) {
            len = append(s_line, len, "%lu=%u;", (unsigned long)i, v->periph_err[i]);
        }
    }
    len = append(s_line, len, ",R:%u:%u:%u,Q:%u:%u:%u,L:%u:%u,M:%u\r\n",
                 p->rule.comp_id, p->rule.fault_type, p->rule.confidence,
                 p->stat.comp_id, p->stat.fault_type, p->stat.confidence,
                 p->label_comp, p->label_fault, p->mode);
    fm_uart_write((const uint8_t *)s_line, (uint32_t)len);
}

static void printer_task_fn(void *arg)
{
    (void)arg;
    static fm_pub_t v;
    for (;;) {
        if (osMessageQueueGet(s_q, &v, NULL, osWaitForever) != osOK) {
            continue;
        }
        if (!s_stream) {
            continue;
        }
        uint32_t d = s_dropped;
        if (d) {
            s_dropped = 0;
            fm_uart_printf("SIGDROP,%lu\r\n", (unsigned long)d);
        }
        print_csv(&v);
    }
}

void fm_sigout_publish(const fm_pub_t *p)
{
    if (osMutexAcquire(s_latest_mtx, 0) == osOK) {
        s_latest = *p;
        osMutexRelease(s_latest_mtx);
    }
    if (s_stream && osMessageQueuePut(s_q, p, 0, 0) != osOK) {
        s_dropped++;
    }
}

/* 'S' -- readable table of the latest window. */
static void cmd_status(const char *args)
{
    (void)args;
    static sig_vector_t v;     /* shell stack is small: keep it static */
    osMutexAcquire(s_latest_mtx, osWaitForever);
    v = s_latest.v;
    osMutexRelease(s_latest_mtx);

    fm_monitor_print_status();

    fm_uart_printf("window %lu  (%lu us)  core %lu MHz  heap free %u B\r\n",
                   (unsigned long)v.window_idx, (unsigned long)cyc_to_us(v.t_len),
                   (unsigned long)(SystemCoreClock / 1000000U),
                   (unsigned)xPortGetFreeHeapSize());
    fm_uart_printf("id name        kind runs  cpu_us  max_us  gap_us  stack_free\r\n");
    for (uint32_t i = 0; i < FM_MAX_COMP; i++) {
        if (g_fm_comp[i].kind == FM_KIND_UNUSED) {
            continue;
        }
        const comp_sig_t *c = &v.comp[i];
        if (g_fm_comp[i].kind == FM_KIND_PERIPH) {
            fm_uart_printf("%2lu %-10s  per cfg_crc=%08lx errors=%u\r\n",
                           (unsigned long)i, g_fm_comp[i].name,
                           (unsigned long)v.periph_hash[i], v.periph_err[i]);
            continue;
        }
        fm_uart_printf("%2lu %-10s %4s %5u %7lu %7lu %7lu  %5u\r\n",
                       (unsigned long)i, g_fm_comp[i].name,
                       g_fm_comp[i].kind == FM_KIND_TASK ? "task" : "isr",
                       c->runs, (unsigned long)cyc_to_us(c->cpu_cycles),
                       (unsigned long)cyc_to_us(c->max_slice),
                       (unsigned long)cyc_to_us(c->max_gap),
                       c->stack_free_min);
    }
    fm_uart_printf("ring: %lu events held, %lu overwritten\r\n",
                   (unsigned long)rb_count(&g_fm_rb), (unsigned long)g_fm_rb.dropped);
    /* Idle-hook count: the CPU-overhead measurement (see idle_stats.h). */
    fm_uart_printf("IDLE,%lu,%lu\r\n", (unsigned long)idle_stats_get_count(),
                   (unsigned long)idle_stats_get_uptime_ms());
    /* Overhead figures for the evaluation table. */
    uint32_t mon = 0, mon_cyc = 0;
    for (uint32_t i = 0; i < FM_MAX_COMP; i++) {
        if (g_fm_comp[i].kind == FM_KIND_TASK && g_fm_comp[i].recover == NULL) {
            mon_cyc += v.comp[i].cpu_cycles;      /* framework's own tasks */
        }
    }
    mon = v.t_len ? (uint32_t)(((uint64_t)mon_cyc * 10000U) / v.t_len) : 0U;
    fm_uart_printf("OVH,cpu_monitor=%lu.%02lu%%,ram_framework=%lu B,trace_storage=%lu B,report=%u B\r\n",
                   (unsigned long)(mon / 100U), (unsigned long)(mon % 100U),
                   (unsigned long)fm_monitor_ram_bytes(),
                   (unsigned long)sizeof(g_fm_rb), (unsigned)sizeof(fault_report_t));
}

/* 'V 1' / 'V 0' -- signature stream on/off. */
static void cmd_stream(const char *args)
{
    s_stream = (atoi(args) != 0);
    s_dropped = 0;
    fm_uart_printf("OK stream %s\r\n", s_stream ? "on" : "off");
}

void fm_sigout_init(void)
{
    s_q          = osMessageQueueNew(SIGOUT_QUEUE_LEN, sizeof(fm_pub_t), NULL);
    s_latest_mtx = osMutexNew(NULL);
    memset(&s_latest, 0, sizeof(s_latest));
    s_stream  = false;
    s_dropped = 0;

    static const osThreadAttr_t attr = {
        .name = "sigout", .stack_size = 512 * 4, .priority = osPriorityLow,
    };
    osThreadId_t h = osThreadNew(printer_task_fn, NULL, &attr);
    /* FreeRTOS does not zero uxTaskNumber (our trace tag) for a new task:
     * make sure the printer is "not a monitored component" (tag 0) */
    if (h != NULL) {
        vTaskSetTaskNumber((TaskHandle_t)h, 0U);
    }

    fm_shell_register('S', cmd_status);
    fm_shell_register('V', cmd_stream);
}

#endif /* FM_ENABLE_MONITOR */

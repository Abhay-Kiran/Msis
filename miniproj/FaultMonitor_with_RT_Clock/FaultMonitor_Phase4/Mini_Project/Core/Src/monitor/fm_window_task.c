/*
 * fm_window_task.c -- see fm_window_task.h. Owner: Abhay.
 */
#include "fm_window_task.h"
#include "fm_trace.h"
#include "fm_capture.h"
#include "fm_sigout.h"
#include "fm_uart.h"

#include "cmsis_os.h"
#include <string.h>

#if FM_ENABLE_MONITOR && !FM_ENABLE_DETECT

static fm_pub_t s_pub;
static uint32_t s_label_ts, s_label_windows;

/* Capture-only builds have no monitor to write a boot report, so print
 * the reset cause and any crash record that survived in .noinit here. */
static void boot_print(void)
{
    uint16_t cause = fm_reset_cause();
    fm_uart_printf("BOOT #%lu reset cause: %s\r\n",
                   (unsigned long)g_fm_noinit.boot_count, fm_reset_cause_str(cause));
    if (!g_fm_noinit.crash_valid) {
        return;
    }
    fm_uart_printf("!TRAP trap=%u type=%u comp=%u pc=0x%08lx cfsr=0x%08lx addr=0x%08lx\r\n",
                   g_fm_noinit.trap, g_fm_noinit.fault_type, g_fm_noinit.comp_id,
                   (unsigned long)g_fm_noinit.pc, (unsigned long)g_fm_noinit.cfsr,
                   (unsigned long)g_fm_noinit.fault_addr);
    g_fm_noinit.crash_valid = 0;
}

static void window_task_fn(void *arg)
{
    (void)arg;
    boot_print();
    uint32_t next = osKernelGetTickCount();
    fm_sig_close_window(&s_pub.v);              /* drop the boot partial window */
    for (;;) {
        next += FM_WINDOW_MS;
        osDelayUntil(next);
        fm_sig_close_window(&s_pub.v);
        for (uint32_t i = 0; i < FM_MAX_COMP; i++) {
            if (g_fm_comp[i].kind == FM_KIND_TASK) {
                uint32_t w = fm_stack_free_words((uint8_t)i);
                s_pub.v.comp[i].stack_free_min = (uint16_t)(w > 0xFFFFU ? 0xFFFFU : w);
            }
        }
        memset(&s_pub.rule, 0, sizeof(s_pub.rule));
        memset(&s_pub.stat, 0, sizeof(s_pub.stat));
        s_pub.rule.comp_id = FM_COMP_NONE;
        s_pub.stat.comp_id = FM_COMP_NONE;
        /* ground-truth label: the last injection, for 10 s (100 windows) */
        fm_injection_t inj = fm_last_injection();
        if (inj.valid && inj.cycle_ts != s_label_ts) {
            s_label_ts      = inj.cycle_ts;
            s_label_windows = 0;
        }
        bool label = inj.valid && s_label_windows++ < 100U;
        s_pub.label_comp  = label ? inj.comp_id : FM_COMP_NONE;
        s_pub.label_fault = label ? inj.fault_id : 0U;
        s_pub.mode = 0;
        fm_sigout_publish(&s_pub);
    }
}

void fm_window_task_init(uint8_t self_comp_id)
{
    static const osThreadAttr_t attr = {
        .name = "window", .stack_size = 512 * 4, .priority = osPriorityHigh,
    };
    osThreadId_t h = osThreadNew(window_task_fn, NULL, &attr);
    fm_register_task(self_comp_id, h, "window", NULL);
}

#endif /* FM_ENABLE_MONITOR && !FM_ENABLE_DETECT */

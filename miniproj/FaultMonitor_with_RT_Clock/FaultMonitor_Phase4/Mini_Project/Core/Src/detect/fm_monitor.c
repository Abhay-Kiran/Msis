/*
 * fm_monitor.c -- see fm_monitor.h. Owner: Arfan.
 */
#include "fm_monitor.h"
#include "fm_learn.h"
#include "fm_detect.h"
#include "fm_blackbox.h"
#include "fm_recover.h"
#include "fm_watchdog.h"

#include "fm_trace.h"
#include "fm_capture.h"
#include "fm_sigout.h"
#include "fm_uart.h"
#include "uart_shell.h"

#include "main.h"
#include "cmsis_os.h"
#include "FreeRTOS.h"
#include "task.h"

#include <stdlib.h>
#include <string.h>

#if FM_ENABLE_MONITOR && FM_ENABLE_DETECT

#define MONITOR_STACK_WORDS 1024U

/* ---- State (static: far too big for a task stack) ------------------- */
static fm_pub_t        s_pub;
static baseline_t      s_bl;
static fm_learn_t      s_learn;
static fault_report_t  s_rep;
static bool            s_have_bl;

static volatile fm_mode_t s_mode = FM_MODE_WARMUP;
static uint32_t        s_warmup;
static uint32_t        s_grace;
static uint32_t        s_learn_target = FM_LEARN_WINDOWS_DEFAULT;
static volatile uint8_t s_loc_sel;            /* 0 rule, 1 statistical */
static const localizer_t *const s_loc[2] = { &fm_localizer_rule, &fm_localizer_stat };

/* requests from the shell, executed by the monitor at a window boundary
 * (flash erases must never surprise the detector) */
static volatile uint32_t s_req_learn;         /* 0 = none, else windows */
static volatile bool     s_req_clear;

/* ground truth tracking (for latency and the 'L:' label) */
static uint32_t s_last_inj_ts;
static bool     s_label_active;
static uint8_t  s_label_comp = FM_COMP_NONE, s_label_fault;
static uint32_t s_label_ts, s_label_windows;

static uint32_t s_n_faults, s_n_recoveries;
static uint8_t  s_self;

static inline uint32_t cyc_per_us(void) { return SystemCoreClock / 1000000U; }

static const char *comp_name(uint8_t c)
{
    if (c < FM_MAX_COMP && g_fm_comp[c].name) return g_fm_comp[c].name;
    if (c == FM_COMP_OTHER) return "other";
    return "none";
}

static const char *evt_name(uint8_t t)
{
    static const char *const n[EVT_COUNT] = {
        "TASK_IN", "TASK_OUT", "ISR_ENTER", "ISR_EXIT", "-", "-", "STAGE",
        "CHECKIN", "STACK_FAULT", "MEM_FAULT", "PERIPH_ERR", "COMM_ERR",
        "RESET_CAUSE", "INJECT", "FAULT", "RECOVERY", "MARK",
    };
    return t < EVT_COUNT ? n[t] : "?";
}

uint32_t fm_monitor_ram_bytes(void)
{
    return (uint32_t)(sizeof(g_fm_rb) + sizeof(g_fm_noinit) + sizeof(g_fm_comp) +
                      sizeof(g_fm_stage) + sizeof(s_pub) + sizeof(s_bl) +
                      sizeof(s_learn) + sizeof(s_rep) + MONITOR_STACK_WORDS * 4U);
}

/* ---- Detector configuration from the registry ----------------------- */
static void configure_detector(void)
{
    fm_detect_ctx_t *ctx = fm_detect_ctx();
    for (uint32_t i = 0; i < FM_MAX_COMP; i++) {
        ctx->kind[i]     = (uint8_t)g_fm_comp[i].kind;
        ctx->priority[i] = g_fm_comp[i].priority;
        ctx->is_comm[i]  = g_fm_comp[i].is_comm ? 1U : 0U;
    }
    /* the framework does not judge itself: if the monitor stalls, the
     * watchdog notices (an observer cannot observe its own absence) */
    ctx->kind[s_self] = FM_KIND_UNUSED;
    for (uint32_t s = 0; s < FM_MAX_STAGES; s++) {
        ctx->stage_owner[s] = g_fm_stage[s].owner;
    }
    ctx->cycles_per_ms = SystemCoreClock / 1000U;
    ctx->window_ms     = FM_WINDOW_MS;
}

/* ---- Printing ---------------------------------------------------------- */
static void print_history(const event_t *h, uint32_t n, uint32_t ref_ts)
{
    for (uint32_t i = 0; i < n; i++) {
        const event_t *e = &h[i];
        if (e->cycle_ts == 0U && e->type == 0U && e->comp_id == 0U) {
            continue;
        }
        /* Signed difference: an event recorded a few cycles AFTER the
         * reference (e.g. the MEM_FAULT event written just after the
         * crash timestamp) must print as +0 us, not as -51 s (= -2^32
         * cycles at 84 MHz, the old unsigned wrap). */
        int32_t us = (int32_t)(e->cycle_ts - ref_ts) / (int32_t)cyc_per_us();
        if (e->type == EVT_STAGE && e->extra < FM_MAX_STAGES && g_fm_stage[e->extra].name) {
            fm_uart_printf("!HIST %8ld us  %-8s STAGE %s\r\n", (long)us,
                           comp_name(e->comp_id), g_fm_stage[e->extra].name);
        } else {
            fm_uart_printf("!HIST %8ld us  %-8s %s %u\r\n", (long)us,
                           comp_name(e->comp_id), evt_name(e->type), e->extra);
        }
    }
}

static void print_report(const fault_report_t *r, long latency_ms, bool history)
{
    fm_uart_printf("\r\n!FAULT seq=%lu type=%s comp=%s(%u) conf=%u%% loc=%s action=%s"
                   " rules=0x%04x pc=0x%08lx uptime=%lums latency_ms=%ld\r\n",
                   (unsigned long)r->seq, fm_fault_type_str(r->fault_type),
                   comp_name(r->comp_id), r->comp_id, r->confidence,
                   r->localizer ? "stat" : "rule", fm_action_str(r->action),
                   r->rules_hit, (unsigned long)r->pc, (unsigned long)r->uptime_ms,
                   latency_ms);
    if (history) {
        print_history(r->history, FM_HISTORY_LEN, r->cycle_ts);
    }
}

static void store(fault_report_t *r)
{
    bool erased = false;
    fm_watchdog_checkin();
    if (!fm_bb_append(r, &erased)) {
        fm_uart_printf("!WARN black box write failed\r\n");
    }
    fm_watchdog_checkin();
    if (erased) {
        s_grace = FM_GRACE_WINDOWS;
    }
}

/* ---- Boot: turn what survived the reset into reports ------------------ */
static void boot_report(void)
{
    uint16_t cause = fm_reset_cause();
    fm_uart_printf("BOOT #%lu reset cause: %s (0x%02x)\r\n",
                   (unsigned long)g_fm_noinit.boot_count, fm_reset_cause_str(cause), cause);

    bool reported = false;
    memset(&s_rep, 0, sizeof(s_rep));
    s_rep.reset_cause = cause;
    s_rep.cycle_ts    = DWT->CYCCNT;
    s_rep.uptime_ms   = 0;
    s_rep.action      = ACT_SYSTEM_RESET;

    if (g_fm_noinit.crash_valid) {
        s_rep.fault_type = g_fm_noinit.fault_type;
        s_rep.comp_id    = g_fm_noinit.comp_id;
        s_rep.confidence = (s_rep.fault_type == FT_STACK_OVERFLOW) ? 99U : 95U;
        s_rep.pc         = g_fm_noinit.pc;
        s_rep.rules_hit  = FM_RULE_TRAP;
        memcpy(s_rep.history, g_fm_noinit.history, sizeof(s_rep.history));
        store(&s_rep);
        fm_uart_printf("!TRAP before reset: trap=%u cfsr=0x%08lx hfsr=0x%08lx addr=0x%08lx lr=0x%08lx\r\n",
                       g_fm_noinit.trap, (unsigned long)g_fm_noinit.cfsr,
                       (unsigned long)g_fm_noinit.hfsr, (unsigned long)g_fm_noinit.fault_addr,
                       (unsigned long)g_fm_noinit.lr);
        print_report(&s_rep, -1, false);
        print_history(s_rep.history, FM_HISTORY_LEN, g_fm_noinit.cycle_ts);
        g_fm_noinit.crash_valid = 0;
        reported = true;
    } else if (cause & FM_RST_IWDG) {
        uint8_t c = g_fm_noinit.cur_comp;
        s_rep.fault_type = FT_WATCHDOG;
        s_rep.comp_id    = c;
        s_rep.confidence = (c < FM_MAX_COMP) ? 80U : 40U;
        s_rep.rules_hit  = FM_RULE_WATCHDOG;
        memcpy(s_rep.history, g_fm_noinit.history, sizeof(s_rep.history));
        store(&s_rep);
        fm_uart_printf("!WATCHDOG reset: the CPU stopped serving the monitor; "
                       "last running: %s, last stage: %u\r\n",
                       comp_name(c), g_fm_noinit.last_stage);
        print_report(&s_rep, -1, false);
        uint32_t ref = s_rep.history[FM_HISTORY_LEN - 1U].cycle_ts;
        print_history(s_rep.history, FM_HISTORY_LEN, ref);
        reported = true;
    } else if ((cause & FM_RST_SW) && g_fm_noinit.planned_reset) {
        fm_uart_printf("!RECOVERED by system reset (escalation for %s)\r\n",
                       comp_name(g_fm_noinit.suspect_comp));
    }
    g_fm_noinit.planned_reset = 0;

    if (reported && g_fm_noinit.inject_valid) {
        fm_uart_printf("!GT injected fault %u on comp %u before the reset",
                       g_fm_noinit.inject_fault, g_fm_noinit.inject_comp);
        if (s_rep.rules_hit == FM_RULE_TRAP) {
            /* both timestamps were taken before the reset, same CYCCNT epoch */
            uint32_t d = g_fm_noinit.cycle_ts - g_fm_noinit.inject_ts;
            fm_uart_printf(", trap latency %lu us", (unsigned long)(d / cyc_per_us()));
        }
        fm_uart_printf("\r\n");
    }
    g_fm_noinit.inject_valid = 0;
    if (reported) {
        s_n_faults++;
    }
}

/* ---- Fault handling ---------------------------------------------------- */
static void handle_fault(const loc_result_t *res)
{
    uint8_t comp = res->comp_id;
    fm_action_t act = fm_recover_plan(comp, res->fault_type, osKernelGetTickCount());

    memset(&s_rep, 0, sizeof(s_rep));
    s_rep.cycle_ts    = DWT->CYCCNT;
    s_rep.uptime_ms   = osKernelGetTickCount();
    s_rep.fault_type  = res->fault_type;
    s_rep.comp_id     = comp;
    s_rep.confidence  = res->confidence;
    s_rep.action      = (uint8_t)act;
    s_rep.rules_hit   = res->rules_hit;
    s_rep.reset_cause = fm_reset_cause();
    s_rep.localizer   = s_loc_sel;
    if (g_fm_comp[comp].kind == FM_KIND_TASK) {
        s_rep.pc = fm_task_pc(g_fm_comp[comp].handle);   /* where it is stuck */
    }
    fm_trace_event(comp, EVT_FAULT, res->fault_type);
    rb_copy_last(&g_fm_rb, s_rep.history, FM_HISTORY_LEN);
    store(&s_rep);
    s_n_faults++;

    long latency_ms = -1;
    if (s_label_active) {
        latency_ms = (long)((s_rep.cycle_ts - s_label_ts) / (SystemCoreClock / 1000U));
    }
    print_report(&s_rep, latency_ms, true);
    if (s_label_active) {
        fm_uart_printf("!GT injected=%u on %s -> %s\r\n", s_label_fault,
                       comp_name(s_label_comp),
                       s_label_comp == comp ? "LOCALIZED CORRECTLY" : "DIFFERENT COMPONENT");
    }

    if (act == ACT_SYSTEM_RESET) {
        fm_uart_printf("!RECOVERY level %u: system reset\r\n", fm_recover_level(comp));
        osDelay(20);                    /* let the UART drain */
    }
    fm_recover_execute(comp, act);      /* does not return for a reset */

    s_n_recoveries++;
    fm_trace_restart_window();
    fm_detect_reset();
    s_grace = FM_GRACE_WINDOWS;
    s_label_active = false;
    s_label_comp = FM_COMP_NONE;
    fm_uart_printf("!RECOVERED comp=%s action=%s level=%u\r\n", comp_name(comp),
                   fm_action_str(act), fm_recover_level(comp));
}

/* ---- Learning ---------------------------------------------------------- */
static void start_learning(uint32_t windows)
{
    fm_learn_reset(&s_learn);
    s_learn_target = windows;
    s_mode = FM_MODE_LEARN;
    fm_uart_printf("LEARN start: %lu windows (%lu s) of normal operation -- "
                   "please do not inject faults now\r\n",
                   (unsigned long)windows, (unsigned long)(windows * FM_WINDOW_MS / 1000U));
}

static void finish_learning(void)
{
    fm_learn_finish(&s_learn, &s_bl, SystemCoreClock);
    fm_watchdog_checkin();
    bool ok = fm_bb_save_baseline(&s_bl);
    fm_watchdog_checkin();
    s_have_bl = true;
    s_mode = FM_MODE_MONITOR;
    s_grace = FM_GRACE_WINDOWS;
    fm_trace_restart_window();
    fm_detect_reset();
    fm_uart_printf("LEARN done: baseline from %lu windows %s flash sector 6. Monitoring.\r\n",
                   (unsigned long)s_bl.n_windows, ok ? "saved to" : "NOT saved to");
}

/* A stored baseline only fits this firmware if it was learned with the
 * same components: every registered task ran while learning and every
 * registered peripheral has a stored register CRC, and nothing else did.
 * (A changed application, or an optional sensor that is now missing or
 * newly present, would otherwise give false alarms forever.) */
static bool baseline_matches_components(const baseline_t *bl)
{
    for (uint32_t i = 0; i < FM_MAX_COMP; i++) {
        if (i == s_self) {
            continue;
        }
        fm_kind_t k = g_fm_comp[i].kind;
        bool ran  = bl->comp[i].runs.mean > 0.0f;
        bool hash = bl->periph_hash[i] != 0U;
        if ((k == FM_KIND_TASK) != ran && (k == FM_KIND_TASK || k == FM_KIND_UNUSED)) {
            return false;
        }
        if ((k == FM_KIND_PERIPH) != hash) {
            return false;
        }
    }
    return true;
}

/* ---- The task -------------------------------------------------------- */
static void track_ground_truth(void)
{
    fm_injection_t inj = fm_last_injection();
    if (inj.valid && inj.cycle_ts != s_last_inj_ts) {
        s_last_inj_ts   = inj.cycle_ts;
        s_label_active  = true;
        s_label_comp    = inj.comp_id;
        s_label_fault   = inj.fault_id;
        s_label_ts      = inj.cycle_ts;
        s_label_windows = 0;
    }
    if (s_label_active && ++s_label_windows > 100U) {   /* 10 s: not detected */
        s_label_active = false;
        s_label_comp   = FM_COMP_NONE;
    }
}

static void monitor_task_fn(void *arg)
{
    (void)arg;
    configure_detector();
    fm_bb_init();
    boot_report();

    s_have_bl = fm_bb_load_baseline(&s_bl, SystemCoreClock);
    if (s_have_bl && !baseline_matches_components(&s_bl)) {
        s_have_bl = false;
        fm_uart_printf("baseline in flash was learned with different components: re-learning\r\n");
    }
    fm_uart_printf("black box: %lu stored reports; baseline: %s\r\n",
                   (unsigned long)fm_bb_count(),
                   s_have_bl ? "loaded from flash" : "none -> will learn after warm-up");
    s_warmup = FM_WARMUP_WINDOWS;
    s_mode   = FM_MODE_WARMUP;

    uint32_t next = osKernelGetTickCount();
    fm_sig_close_window(&s_pub.v);      /* discard the boot partial window */

    for (;;) {
        next += FM_WINDOW_MS;
        uint32_t now = osKernelGetTickCount();
        if ((int32_t)(next - now) <= 0) {
            next = now + FM_WINDOW_MS;      /* fell behind (flash stall): resync */
        }
        osDelayUntil(next);

        fm_sig_close_window(&s_pub.v);
        for (uint32_t i = 0; i < FM_MAX_COMP; i++) {
            fm_detect_ctx()->priority[i] = g_fm_comp[i].priority;  /* 'P' may change it */
            if (g_fm_comp[i].kind == FM_KIND_TASK) {
                uint32_t w = fm_stack_free_words((uint8_t)i);
                s_pub.v.comp[i].stack_free_min = (uint16_t)(w > 0xFFFFU ? 0xFFFFU : w);
            }
        }
        fm_watchdog_checkin();
        track_ground_truth();

        memset(&s_pub.rule, 0, sizeof(s_pub.rule));
        memset(&s_pub.stat, 0, sizeof(s_pub.stat));
        s_pub.rule.comp_id = FM_COMP_NONE;
        s_pub.stat.comp_id = FM_COMP_NONE;

        /* shell requests, at a window boundary */
        if (s_req_clear) {
            s_req_clear = false;
            fm_watchdog_checkin();
            bool ok = fm_bb_clear();
            fm_watchdog_checkin();
            fm_uart_printf("OK fault log %s\r\n", ok ? "cleared" : "clear FAILED");
            s_grace = FM_GRACE_WINDOWS;
            fm_trace_restart_window();
        }
        if (s_req_learn) {
            uint32_t n = s_req_learn;
            s_req_learn = 0;
            start_learning(n);
            s_grace = 1;
        }

        if (s_grace > 0U) {
            s_grace--;
            if (s_grace == 0U) {
                fm_trace_restart_window();
            }
        } else {
            switch (s_mode) {
            case FM_MODE_WARMUP:
                if (s_warmup > 0U) {
                    s_warmup--;
                }
                if (s_warmup == 0U) {
                    if (s_have_bl) {
                        s_mode = FM_MODE_MONITOR;
                        fm_uart_printf("MONITOR running (localizer: %s)\r\n",
                                       s_loc[s_loc_sel]->name);
                    } else {
                        start_learning(FM_LEARN_WINDOWS_DEFAULT);
                    }
                }
                break;
            case FM_MODE_LEARN:
                fm_learn_add(&s_learn, &s_pub.v);
                if (s_learn.n_windows >= s_learn_target) {
                    finish_learning();
                }
                break;
            case FM_MODE_MONITOR: {
                fm_localizer_rule.localize(&s_pub.v, &s_bl, &s_pub.rule);
                fm_localizer_stat.localize(&s_pub.v, &s_bl, &s_pub.stat);
                const loc_result_t *act = s_loc_sel ? &s_pub.stat : &s_pub.rule;
                g_fm_noinit.suspect_comp = act->comp_id;
                if (act->comp_id != FM_COMP_NONE) {
                    /* shadow mode: show what the other localizer said */
                    const loc_result_t *oth = s_loc_sel ? &s_pub.rule : &s_pub.stat;
                    s_pub.label_comp  = s_label_active ? s_label_comp : FM_COMP_NONE;
                    s_pub.label_fault = s_label_active ? s_label_fault : 0U;
                    s_pub.mode = (uint8_t)s_mode;
                    fm_sigout_publish(&s_pub);
                    fm_uart_printf("!SHADOW %s says: %s %s conf=%u%%\r\n",
                                   s_loc[s_loc_sel ^ 1U]->name,
                                   oth->comp_id == FM_COMP_NONE ? "-" : comp_name(oth->comp_id),
                                   fm_fault_type_str(oth->fault_type), oth->confidence);
                    handle_fault(act);
                    continue;
                }
                break;
            }
            default:
                break;
            }
        }

        s_pub.label_comp  = s_label_active ? s_label_comp : FM_COMP_NONE;
        s_pub.label_fault = s_label_active ? s_label_fault : 0U;
        s_pub.mode        = (uint8_t)s_mode;
        fm_sigout_publish(&s_pub);
    }
}

/* ---- Shell commands ------------------------------------------------- */

static void print_x10(const char *label, float x)
{
    long v = (long)(x * 10.0f + (x >= 0 ? 0.5f : -0.5f));
    fm_uart_printf(" %s=%ld.%ld", label, v / 10, labs(v % 10));
}

/* 'L [n]' -- (re)learn the baseline from n windows (default 100) */
static void cmd_learn(const char *args)
{
    int n = atoi(args);
    if (n <= 0) n = (int)FM_LEARN_WINDOWS_DEFAULT;
    if (n < 20) n = 20;
    s_req_learn = (uint32_t)n;
    fm_uart_printf("OK learning will start at the next window\r\n");
}

/* 'B' -- print the baseline */
static void cmd_baseline(const char *args)
{
    (void)args;
    if (!s_have_bl) {
        fm_uart_printf("no baseline yet (mode %u)\r\n", (unsigned)s_mode);
        return;
    }
    uint32_t cpu = SystemCoreClock / 1000000U;
    fm_uart_printf("baseline: %lu windows, core %lu MHz, crc %08lx\r\n",
                   (unsigned long)s_bl.n_windows, (unsigned long)(s_bl.core_hz / 1000000U),
                   (unsigned long)s_bl.crc32);
    for (uint32_t i = 0; i < FM_MAX_COMP; i++) {
        const fm_comp_info_t *c = &g_fm_comp[i];
        if (c->kind == FM_KIND_UNUSED) continue;
        const comp_baseline_t *b = &s_bl.comp[i];
        fm_uart_printf("%2lu %-8s", (unsigned long)i, c->name);
        if (c->kind == FM_KIND_PERIPH) {
            fm_uart_printf(" cfg_crc=%08lx\r\n", (unsigned long)s_bl.periph_hash[i]);
            continue;
        }
        print_x10("runs", b->runs.mean);
        print_x10("sd", b->runs.sigma);
        print_x10("cpu_us", b->cpu_cycles.mean / (float)cpu);
        print_x10("max_slice_us", b->max_slice.max / (float)cpu);
        fm_uart_printf(" max_gap_ms=%lu stack_min=%u %s\r\n",
                       (unsigned long)(b->max_gap_seen / (cpu * 1000U)),
                       b->stack_free_min,
                       fm_detect_is_periodic(&s_bl, (uint8_t)i) ? "periodic" : "event-driven");
    }
    fm_uart_printf("allowed stage transitions:");
    for (uint32_t a = 0; a < FM_MAX_STAGES; a++) {
        for (uint32_t b = 0; b < FM_MAX_STAGES; b++) {
            if (s_bl.trans_allowed[a] & (1U << b)) {
                fm_uart_printf(" %c>%c", 'A' + (int)a, 'A' + (int)b);
            }
        }
    }
    fm_uart_printf("\r\n");
}

/* 'R' -- list the black box; 'R <seq>' -- one report with its history */
static void cmd_reports(const char *args)
{
    int want = atoi(args);
    static fault_report_t r;
    fm_uart_printf("#R,seq,uptime_ms,type,comp,conf,action,loc,rules,pc,reset_cause\r\n");
    uint32_t shown = 0;
    for (uint32_t i = 0; i < FM_LOG_SLOTS; i++) {
        if (!fm_bb_read(i, &r)) {
            if (shown >= fm_bb_count()) break;
            continue;
        }
        shown++;
        if (want > 0 && r.seq != (uint32_t)want) continue;
        fm_uart_printf("R,%lu,%lu,%s,%s,%u,%s,%s,0x%04x,0x%08lx,%s\r\n",
                       (unsigned long)r.seq, (unsigned long)r.uptime_ms,
                       fm_fault_type_str(r.fault_type), comp_name(r.comp_id), r.confidence,
                       fm_action_str(r.action), r.localizer ? "stat" : "rule", r.rules_hit,
                       (unsigned long)r.pc, fm_reset_cause_str(r.reset_cause));
        if (want > 0) {
            uint32_t ref = r.cycle_ts;
            if (r.rules_hit & (FM_RULE_TRAP | FM_RULE_WATCHDOG)) {
                ref = r.history[FM_HISTORY_LEN - 1U].cycle_ts;
            }
            print_history(r.history, FM_HISTORY_LEN, ref);
        }
    }
    fm_uart_printf("END %lu reports\r\n", (unsigned long)fm_bb_count());
}

/* 'C' -- clear the fault log (done by the monitor at the next window) */
static void cmd_clear(const char *args)
{
    (void)args;
    s_req_clear = true;
    fm_uart_printf("OK clearing at the next window\r\n");
}

/* 'M 0' rule-based, 'M 1' statistical */
static void cmd_mode(const char *args)
{
    if (*args) {
        s_loc_sel = (atoi(args) != 0) ? 1U : 0U;
        fm_detect_reset();
    }
    fm_uart_printf("OK localizer: %s (the other runs in shadow mode)\r\n",
                   s_loc[s_loc_sel]->name);
}

/* 'K <sigma>' -- sensitivity of both localizers */
static void cmd_sigma(const char *args)
{
    if (*args) {
        float k = strtof(args, NULL);
        if (k >= 1.0f && k <= 20.0f) {
            fm_detect_ctx()->k_sigma = k;
        }
    }
    long v = (long)(fm_detect_ctx()->k_sigma * 10.0f);
    fm_uart_printf("OK k_sigma=%ld.%ld\r\n", v / 10, v % 10);
}

void fm_monitor_print_status(void)
{
    static const char *const m[] = { "warm-up", "learning", "monitoring" };
    fm_uart_printf("monitor: %s", m[s_mode]);
    if (s_mode == FM_MODE_LEARN) {
        fm_uart_printf(" (%lu/%lu)", (unsigned long)s_learn.n_windows,
                       (unsigned long)s_learn_target);
    }
    fm_uart_printf(" | localizer %s | faults %lu | recoveries %lu | stored %lu | boot #%lu\r\n",
                   s_loc[s_loc_sel]->name, (unsigned long)s_n_faults,
                   (unsigned long)s_n_recoveries, (unsigned long)fm_bb_count(),
                   (unsigned long)g_fm_noinit.boot_count);
}

void fm_monitor_init(uint8_t self_comp_id)
{
    s_self = self_comp_id;
    static const osThreadAttr_t attr = {
        .name = "monitor", .stack_size = MONITOR_STACK_WORDS * 4,
        .priority = osPriorityHigh,
    };
    osThreadId_t h = osThreadNew(monitor_task_fn, NULL, &attr);
    fm_register_task(self_comp_id, h, "monitor", NULL);

    fm_shell_register('L', cmd_learn);
    fm_shell_register('B', cmd_baseline);
    fm_shell_register('R', cmd_reports);
    fm_shell_register('C', cmd_clear);
    fm_shell_register('M', cmd_mode);
    fm_shell_register('K', cmd_sigma);
}

#endif /* FM_ENABLE_MONITOR && FM_ENABLE_DETECT */

/*
 * fm_detect.c -- see fm_detect.h. Owner: Arfan.
 *
 * How the rule-based localizer decides, in order:
 *   1. Hard evidence (sequence / peripheral config / error flags) for any
 *      component -> that component is the answer, timing is ignored
 *      (slow tasks are a CONSEQUENCE of a broken peripheral, not a cause).
 *   2. Timing rules per component: overrun, liveness, ISR rate, stack.
 *      Each produces a "margin" = measured / threshold (> 1 = violation).
 *   3. Debounce: a timing violation must hold for 2 windows in a row,
 *      unless its margin is >= 3 (too big to be jitter).
 *   4. Victim suppression: if a component is hogging the CPU (overrun or
 *      ISR rate), lower-priority tasks that went silent are its victims,
 *      not culprits, so their liveness violations are dropped.
 *   5. The largest remaining margin wins. Confidence starts from the
 *      evidence class and drops when other suspects remain.
 */
#include "fm_detect.h"
#include "fault_threshold.h"
#include "fm_common.h"      /* COMP_SHELL */

#include <math.h>
#include <string.h>

static fm_detect_ctx_t s_ctx = {
    .k_sigma = 4.0f, .cycles_per_ms = 84000U, .window_ms = FM_WINDOW_MS,
};

/* Debounce state: consecutive windows with a timing violation. */
static uint8_t s_streak_rule[FM_MAX_COMP];
static uint8_t s_streak_stat[FM_MAX_COMP];

void fm_detect_configure(const fm_detect_ctx_t *ctx) { s_ctx = *ctx; }
fm_detect_ctx_t *fm_detect_ctx(void) { return &s_ctx; }

void fm_detect_reset(void)
{
    memset(s_streak_rule, 0, sizeof(s_streak_rule));
    memset(s_streak_stat, 0, sizeof(s_streak_stat));
}

const char *fm_fault_type_str(uint8_t ft)
{
    static const char *const n[FT_COUNT] = {
        "NONE", "EXEC_OVERRUN", "STARVED", "LIVENESS", "SEQUENCE", "ISR_RATE",
        "STACK_OVERFLOW", "MEM_ACCESS", "PERIPH", "COMM", "WATCHDOG",
    };
    return ft < FT_COUNT ? n[ft] : "?";
}

const char *fm_action_str(uint8_t act)
{
    static const char *const n[ACT_COUNT] = {
        "NONE", "LOG_ONLY", "RESTART_TASK", "REINIT_PERIPH", "SYSTEM_RESET",
    };
    return act < ACT_COUNT ? n[act] : "?";
}

static inline float fmaxf3(float a, float b, float c) { return fmaxf(a, fmaxf(b, c)); }

bool fm_detect_is_periodic(const baseline_t *bl, uint8_t i)
{
    const comp_baseline_t *b = &bl->comp[i];
    float limit = 5.0f * (float)s_ctx.window_ms * (float)s_ctx.cycles_per_ms;
    return b->runs.mean > 0.0f && b->max_gap_seen > 0U && (float)b->max_gap_seen < limit;
}

/* ---- Candidates ------------------------------------------------------ */
typedef struct {
    uint8_t  ft;       /* FT_NONE = no candidate                     */
    bool     hard;     /* hard evidence (no debounce, beats timing)  */
    float    margin;   /* >= 1 means violation                       */
    uint16_t rules;
} cand_t;

static void offer(cand_t *c, uint8_t ft, bool hard, float margin, uint16_t rule)
{
    c->rules |= rule;
    /* keep the strongest: hard beats timing, then larger margin */
    if (c->ft == FT_NONE || (hard && !c->hard) || (hard == c->hard && margin > c->margin)) {
        c->ft = ft;
        c->hard = hard;
        c->margin = margin;
    }
}

/* Shared by both localizers: evidence that is simply true or false. */
static bool hard_evidence(const sig_vector_t *v, const baseline_t *bl, cand_t *c)
{
    bool any = false;

    /* Execution sequence: a stage transition never seen while learning,
     * e.g. C -> C in A -> B -> C -> C -> C. Blame the owner of the stage
     * that ran out of order. */
    for (uint32_t a = 0; a < FM_MAX_STAGES; a++) {
        for (uint32_t b = 0; b < FM_MAX_STAGES; b++) {
            if (v->trans[a][b] && !(bl->trans_allowed[a] & (1U << b))) {
                uint8_t owner = s_ctx.stage_owner[b];
                if (owner < FM_MAX_COMP) {
                    offer(&c[owner], FT_SEQUENCE, true, 1.0f + v->trans[a][b], FM_RULE_SEQUENCE);
                    any = true;
                }
            }
        }
    }

    for (uint32_t i = 0; i < FM_MAX_COMP; i++) {
        uint8_t ft = s_ctx.is_comm[i] ? FT_COMM : FT_PERIPH;
        /* Peripheral status: configuration registers no longer match the
         * CRC learned at commissioning. */
        if (s_ctx.kind[i] == FM_KIND_PERIPH && bl->periph_hash[i] != 0U &&
            v->periph_hash[i] != bl->periph_hash[i]) {
            offer(&c[i], FT_PERIPH, true, 5.0f, FM_RULE_PERIPH_CFG);
            any = true;
        }
        /* Peripheral / communication error flags (none expected at all). */
        if (v->periph_err[i] > 0U) {
            offer(&c[i], ft, true, 1.0f + v->periph_err[i],
                  s_ctx.is_comm[i] ? FM_RULE_COMM : FM_RULE_PERIPH_ERR);
            any = true;
        }
    }
    return any;
}

/* ---- Rule-based localizer -------------------------------------------- */

static void timing_rules(const sig_vector_t *v, const baseline_t *bl, cand_t *c)
{
    const float k  = s_ctx.k_sigma;
    const float ms = (float)s_ctx.cycles_per_ms;
    const float W  = (float)v->t_len;

    for (uint32_t i = 0; i < FM_MAX_COMP; i++) {
        uint8_t kind = s_ctx.kind[i];
        if (kind != FM_KIND_TASK && kind != FM_KIND_ISR) {
            continue;
        }
        const comp_baseline_t *b = &bl->comp[i];
        const comp_sig_t      *x = &v->comp[i];
        bool periodic = fm_detect_is_periodic(bl, (uint8_t)i);

        if (kind == FM_KIND_TASK) {
            /* Execution time overrun */
            float cpu_hi, slice_hi;
            if (periodic) {
                cpu_hi   = fmaxf3(b->cpu_cycles.mean + k * b->cpu_cycles.sigma,
                                  b->cpu_cycles.max * 1.5f, 0.05f * W);
                slice_hi = fmaxf3(b->max_slice.mean + k * b->max_slice.sigma,
                                  b->max_slice.max * 2.0f, 1.0f * ms);
            } else {
                /* event-driven task: only "eats half the CPU" counts */
                cpu_hi   = fmaxf(0.5f * W, b->cpu_cycles.max * 1.5f);
                slice_hi = fmaxf(0.5f * W, b->max_slice.max * 2.0f);
            }
            if (i == COMP_SHELL) {
                /* console printing is legitimate bursty CPU use; a stuck
                 * shell still trips the slice check below */
                cpu_hi = fmaxf(cpu_hi, 0.5f * W);
            }
            float m = fmaxf((float)x->cpu_cycles / cpu_hi, (float)x->max_slice / slice_hi);
            if (m > 1.0f) {
                offer(&c[i], FT_EXEC_OVERRUN, false, m, FM_RULE_OVERRUN);
            }
            /* Stack: nearly exhausted and lower than ever seen */
            if (x->stack_free_min != 0xFFFFU && b->stack_free_min != 0xFFFFU &&
                x->stack_free_min < 16U && x->stack_free_min < b->stack_free_min) {
                offer(&c[i], FT_STACK_OVERFLOW, true, 2.0f, FM_RULE_STACK);
            }
        } else {
            /* ISR: rate, single duration, total time */
            float runs_hi, slice_hi, cpu_hi;
            if (periodic) {
                runs_hi = fmaxf(b->runs.mean + k * b->runs.sigma, b->runs.max * 1.5f) + 2.0f;
            } else {
                runs_hi = fmaxf(b->runs.max * 2.0f, 2000.0f);
            }
            slice_hi = fmaxf3(b->max_slice.mean + k * b->max_slice.sigma,
                              b->max_slice.max * 3.0f, 0.05f * ms);
            cpu_hi   = fmaxf3(b->cpu_cycles.mean + k * b->cpu_cycles.sigma,
                              b->cpu_cycles.max * 2.0f, 0.02f * W);
            if (!periodic) {
                /* traffic-driven ISR (UART TX/RX during console output):
                 * only flag if it takes half the CPU */
                cpu_hi = fmaxf(cpu_hi, 0.5f * W);
            }
            float m = fmaxf3((float)x->runs / runs_hi, (float)x->max_slice / slice_hi,
                             (float)x->cpu_cycles / cpu_hi);
            if (m > 1.0f) {
                offer(&c[i], FT_ISR_RATE, false, m, FM_RULE_ISR_RATE);
            }
        }

        /* Liveness: silent for longer than ever seen (+ margin). For an
         * ISR only if it was regular while learning (a timer, the ADC),
         * not traffic-driven like the UART. */
        bool regular = (kind == FM_KIND_TASK) || (b->runs.sigma <= 0.2f * b->runs.mean);
        if (periodic && regular) {
            uint32_t gap_hi = 2U * b->max_gap_seen + 2U * s_ctx.cycles_per_ms;
            if (threshold_liveness_violation(x->max_gap, gap_hi)) {
                offer(&c[i], FT_LIVENESS, false, (float)x->max_gap / (float)gap_hi,
                      FM_RULE_LIVENESS);
            }
        }
    }
}

/* Debounce timing candidates: keep one if it held 2 windows or is huge. */
static void debounce(cand_t *c, uint8_t *streak)
{
    for (uint32_t i = 0; i < FM_MAX_COMP; i++) {
        if (c[i].ft == FT_NONE || c[i].hard) {
            streak[i] = c[i].ft == FT_NONE ? 0U : streak[i];
            continue;
        }
        if (streak[i] < 0xFFU) {
            streak[i]++;
        }
        if (streak[i] < 2U && c[i].margin < 3.0f) {
            c[i].ft = FT_NONE;   /* not yet */
        }
    }
}

static void finish(const cand_t *c, loc_result_t *out, bool stat, float stat_thr)
{
    uint32_t survivors = 0;
    int      best = -1;
    float    best_score = 0.0f;
    for (uint32_t i = 0; i < FM_MAX_COMP; i++) {
        float s = 0.0f;
        if (c[i].ft != FT_NONE) {
            s = (c[i].hard ? 1000.0f : 0.0f) + c[i].margin;
            survivors++;
            if (best < 0 || s > best_score) {
                best = (int)i;
                best_score = s;
            }
        }
        float sc = s * 100.0f;
        out->score[i] = (uint16_t)(sc > 65535.0f ? 65535.0f : sc);
        out->rules_hit |= c[i].rules;
    }
    if (best < 0) {
        return;
    }
    const cand_t *w = &c[best];
    out->comp_id    = (uint8_t)best;
    out->fault_type = w->ft;
    float conf;
    if (w->hard) {
        conf = (w->ft == FT_SEQUENCE || w->ft == FT_COMM) ? 90.0f : 95.0f;
    } else if (stat) {
        float S = w->margin * stat_thr;          /* margin = S / T */
        conf = 100.0f * S / (S + stat_thr);
    } else {
        conf = 55.0f + 15.0f * log2f(w->margin);
    }
    conf -= 10.0f * (float)(survivors - 1U);
    if (conf > 99.0f) conf = 99.0f;
    if (conf < 30.0f) conf = 30.0f;
    out->confidence = (uint8_t)conf;
}

static void rule_localize(const sig_vector_t *v, const baseline_t *bl, loc_result_t *out)
{
    cand_t c[FM_MAX_COMP];
    memset(c, 0, sizeof(c));
    memset(out, 0, sizeof(*out));
    out->comp_id = FM_COMP_NONE;

    bool hard = hard_evidence(v, bl, c);
    timing_rules(v, bl, c);
    debounce(c, s_streak_rule);

    if (hard) {
        /* step 1: timing anomalies are consequences of the hard evidence */
        for (uint32_t i = 0; i < FM_MAX_COMP; i++) {
            if (!c[i].hard) c[i].ft = FT_NONE;
        }
    } else {
        /* step 4: victim suppression */
        int aggressor_prio = -1;
        for (uint32_t i = 0; i < FM_MAX_COMP; i++) {
            if (c[i].ft == FT_EXEC_OVERRUN || c[i].ft == FT_ISR_RATE) {
                if ((int)s_ctx.priority[i] > aggressor_prio) {
                    aggressor_prio = s_ctx.priority[i];
                }
            }
        }
        if (aggressor_prio >= 0) {
            for (uint32_t i = 0; i < FM_MAX_COMP; i++) {
                if ((c[i].ft == FT_LIVENESS || c[i].ft == FT_STARVED) &&
                    (int)s_ctx.priority[i] < aggressor_prio) {
                    c[i].ft = FT_NONE;
                }
            }
        }
    }
    finish(c, out, false, 0.0f);
}

const localizer_t fm_localizer_rule = { "rule", rule_localize };

/* ---- Statistical localizer ------------------------------------------- */

static float zsq(float x, const fm_stat_t *s)
{
    float sd = fmaxf(s->sigma, fmaxf(0.05f * fabsf(s->mean), 1.0f));
    float z  = (x - s->mean) / sd;
    if (z > 100.0f)  z = 100.0f;
    if (z < -100.0f) z = -100.0f;
    return z * z;
}

static void stat_localize(const sig_vector_t *v, const baseline_t *bl, loc_result_t *out)
{
    cand_t c[FM_MAX_COMP];
    memset(c, 0, sizeof(c));
    memset(out, 0, sizeof(*out));
    out->comp_id = FM_COMP_NONE;

    const float k = s_ctx.k_sigma;
    const float T = 2.0f * (2.0f * k) * (2.0f * k); /* score threshold: 128 at k=4 */

    if (!hard_evidence(v, bl, c)) {
        for (uint32_t i = 0; i < FM_MAX_COMP; i++) {
            uint8_t kind = s_ctx.kind[i];
            if (kind != FM_KIND_TASK && kind != FM_KIND_ISR) {
                continue;
            }
            const comp_baseline_t *b = &bl->comp[i];
            const comp_sig_t      *x = &v->comp[i];
            float zr = zsq((float)x->runs, &b->runs);
            float zc = zsq((float)x->cpu_cycles, &b->cpu_cycles);
            float zs = zsq((float)x->max_slice, &b->max_slice);
            float zg = 0.0f;
            if (fm_detect_is_periodic(bl, (uint8_t)i) && b->max_gap_seen) {
                float g = (float)x->max_gap / (float)b->max_gap_seen;
                if (g > 1.0f) {
                    zg = (g - 1.0f) * 2.0f * k;
                    zg *= zg;
                }
            }
            float S = zr + zc + zs + zg;
            if (S > T) {
                uint8_t ft;
                if (zg >= zr && zg >= zc && zg >= zs) {
                    ft = FT_LIVENESS;
                } else if (kind == FM_KIND_ISR) {
                    ft = FT_ISR_RATE;
                } else {
                    ft = ((float)x->cpu_cycles >= b->cpu_cycles.mean) ? FT_EXEC_OVERRUN : FT_STARVED;
                }
                offer(&c[i], ft, false, S / T, FM_RULE_STAT);
            }
        }
    }
    /* same debounce as the rule-based one: 2 windows unless S >= 3T */
    debounce(c, s_streak_stat);
    finish(c, out, true, T);
}

const localizer_t fm_localizer_stat = { "stat", stat_localize };

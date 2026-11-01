/*
 * fm_learn.c -- see fm_learn.h. Owner: Arfan.
 */
#include "fm_learn.h"

#include <math.h>
#include <string.h>

void fm_welford_add(fm_welford_t *w, float x)
{
    /* Welford (1962): update mean and M2 with one new sample. */
    w->n++;
    float d = x - w->mean;
    w->mean += d / (float)w->n;
    w->m2   += d * (x - w->mean);
    if (w->n == 1U || x > w->max) {
        w->max = x;
    }
}

fm_stat_t fm_welford_stat(const fm_welford_t *w)
{
    fm_stat_t s;
    s.mean  = w->mean;
    s.sigma = (w->n > 1U) ? sqrtf(w->m2 / (float)(w->n - 1U)) : 0.0f;
    s.max   = w->max;
    return s;
}

void fm_learn_reset(fm_learn_t *l)
{
    memset(l, 0, sizeof(*l));
    for (uint32_t i = 0; i < FM_MAX_COMP; i++) {
        l->stack_min[i] = 0xFFFFU;
    }
}

void fm_learn_add(fm_learn_t *l, const sig_vector_t *v)
{
    l->n_windows++;
    for (uint32_t i = 0; i < FM_MAX_COMP; i++) {
        const comp_sig_t *c = &v->comp[i];
        fm_welford_add(&l->runs[i],  (float)c->runs);
        fm_welford_add(&l->cpu[i],   (float)c->cpu_cycles);
        fm_welford_add(&l->slice[i], (float)c->max_slice);
        if (c->max_gap > l->max_gap[i]) {
            l->max_gap[i] = c->max_gap;
        }
        if (c->stack_free_min < l->stack_min[i]) {
            l->stack_min[i] = c->stack_free_min;
        }
        l->periph_hash[i] = v->periph_hash[i];
    }
    for (uint32_t a = 0; a < FM_MAX_STAGES; a++) {
        for (uint32_t b = 0; b < FM_MAX_STAGES; b++) {
            if (v->trans[a][b]) {
                l->trans_allowed[a] |= (uint8_t)(1U << b);
            }
        }
    }
}

void fm_learn_finish(const fm_learn_t *l, baseline_t *out, uint32_t core_hz)
{
    memset(out, 0, sizeof(*out));
    out->magic     = FM_BASELINE_MAGIC;
    out->version   = FM_IFACE_VERSION;
    out->n_windows = l->n_windows;
    out->core_hz   = core_hz;
    for (uint32_t i = 0; i < FM_MAX_COMP; i++) {
        out->comp[i].runs           = fm_welford_stat(&l->runs[i]);
        out->comp[i].cpu_cycles     = fm_welford_stat(&l->cpu[i]);
        out->comp[i].max_slice      = fm_welford_stat(&l->slice[i]);
        out->comp[i].max_gap_seen   = l->max_gap[i];
        out->comp[i].stack_free_min = l->stack_min[i];
        out->periph_hash[i]         = l->periph_hash[i];
    }
    memcpy(out->trans_allowed, l->trans_allowed, sizeof(out->trans_allowed));
    out->crc32 = 0;
}

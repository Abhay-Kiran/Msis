/*
 * fm_learn.h -- On-chip baseline learning. Owner: Arfan.
 *
 * Pure C (no HAL, no RTOS): it only sees sig_vector_t in and baseline_t
 * out, so it builds and is unit-tested on a PC (see tests/ in Part 2).
 *
 * "Learning" = watching N normal windows and remembering, per component,
 * what normal looks like:
 *   - runs, cpu_cycles, max_slice : mean, standard deviation, maximum
 *                                    (Welford's online algorithm: one
 *                                    pass, no sample storage, stable)
 *   - max_gap                      : the largest gap ever seen
 *   - stack_free_min               : the lowest free stack ever seen
 *   - trans_allowed                : which stage i -> stage j transitions
 *                                    ever happened (A->B, B->C, ...)
 *   - periph_hash                  : the configuration-register CRC of
 *                                    every peripheral
 */
#ifndef FM_LEARN_H
#define FM_LEARN_H

#include <stdint.h>
#include "fm_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Welford accumulator for one feature. */
typedef struct {
    uint32_t n;
    float    mean;
    float    m2;      /* sum of squared differences from the mean */
    float    max;
} fm_welford_t;

typedef struct {
    uint32_t     n_windows;
    fm_welford_t runs[FM_MAX_COMP];
    fm_welford_t cpu[FM_MAX_COMP];
    fm_welford_t slice[FM_MAX_COMP];
    uint32_t     max_gap[FM_MAX_COMP];
    uint16_t     stack_min[FM_MAX_COMP];
    uint8_t      trans_allowed[FM_MAX_STAGES];
    uint32_t     periph_hash[FM_MAX_COMP];
} fm_learn_t;

void fm_welford_add(fm_welford_t *w, float x);
fm_stat_t fm_welford_stat(const fm_welford_t *w);

void fm_learn_reset(fm_learn_t *l);
void fm_learn_add(fm_learn_t *l, const sig_vector_t *v);
/* Turns the accumulators into a baseline (crc32 left at 0: the black
 * box computes it when saving). */
void fm_learn_finish(const fm_learn_t *l, baseline_t *out, uint32_t core_hz);

#ifdef __cplusplus
}
#endif

#endif /* FM_LEARN_H */

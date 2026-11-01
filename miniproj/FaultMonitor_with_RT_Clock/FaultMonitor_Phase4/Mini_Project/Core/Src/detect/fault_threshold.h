/*
 * fault_threshold.h -- DET-1: the threshold rule.
 *
 * Pure, stateless comparison logic: given a measured value and a
 * baseline band (or a liveness gap limit), decide whether it's a
 * violation and how confident that call is. No knowledge of events,
 * tasks, or the ring buffer -- fm_detect.c is the only
 * caller, and wires this into its event-processing loop. Kept
 * separate from DET-2 so the threshold math itself is independently
 * unit-tested (see pc_tests/test_fault_threshold.c), and so it can be
 * reused as-is if a future rule (e.g. the deferred transition rule)
 * needs the same band/confidence shape.
 *
 * Confidence formula (three-sigma / empirical rule): a value exactly
 * at the band edge (3 sigma from the mean) scores confidence 100;
 * closer to the mean scores lower, linearly. sigma == 0 (a task with
 * literally zero observed jitter across all clean runs) is treated as
 * maximum confidence for any violation, since ANY deviation from a
 * zero-jitter mean is anomalous by definition.
 */
#ifndef FAULT_THRESHOLD_H
#define FAULT_THRESHOLD_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t mean;   /* cycles: sample mean task duration, from clean-run CSVs */
    uint32_t sigma;  /* cycles: sample std-dev, ddof=1 (matches pandas default) */
    uint32_t lo;     /* cycles: mean - k*sigma, precomputed offline by DET-3   */
    uint32_t hi;     /* cycles: mean + k*sigma, precomputed offline by DET-3   */
} task_band_t;

/* True if `duration` falls outside [band->lo, band->hi]. */
bool threshold_band_violation(uint32_t duration, const task_band_t *band);

/* 0..100 confidence for a band violation, using the three-sigma rule.
 * Only meaningful when threshold_band_violation() is true for the
 * same (duration, band) -- returns 0 otherwise, by definition, not
 * because 0 duration deviation was measured. */
uint8_t threshold_band_confidence(uint32_t duration, const task_band_t *band);

/* True if `gap` (cycles since a task was last seen) exceeds its
 * liveness limit `max_gap`. */
bool threshold_liveness_violation(uint32_t gap, uint32_t max_gap);

#ifdef __cplusplus
}
#endif

#endif /* FAULT_THRESHOLD_H */

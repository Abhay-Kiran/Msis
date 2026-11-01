/*
 * fault_threshold.c -- DET-1 implementation. See fault_threshold.h for
 * the contract.
 */
#include "fault_threshold.h"

bool threshold_band_violation(uint32_t duration, const task_band_t *band)
{
    return duration > band->hi || duration < band->lo;
}

uint8_t threshold_band_confidence(uint32_t duration, const task_band_t *band)
{
    if (!threshold_band_violation(duration, band)) {
        return 0;
    }

    uint32_t dev = (duration > band->mean)
                       ? (duration - band->mean)
                       : (band->mean - duration);
    uint32_t conf = band->sigma
                        ? (uint32_t)(((uint64_t)dev * 100u) / (3u * (uint64_t)band->sigma))
                        : 100u;
    if (conf > 100u) conf = 100u;
    return (uint8_t)conf;
}

bool threshold_liveness_violation(uint32_t gap, uint32_t max_gap)
{
    return gap > max_gap;
}

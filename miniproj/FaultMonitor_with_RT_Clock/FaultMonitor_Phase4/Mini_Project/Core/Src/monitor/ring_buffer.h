/*
 * ring_buffer.h -- CAP-2: ISR-safe ring buffer of event_t.
 *
 * event_t comes from Core/Inc/fm_types.h (frozen v2). This module only copies event_t by value; it has no
 * dependency on its internal fields, so it compiles against whatever
 * the frozen struct actually contains.
 */
#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stdint.h>
#include "fm_types.h"   /* event_t */

#ifdef __cplusplus
extern "C" {
#endif

/* Power of 2 -- required for the mask trick. 1024 x 8 B = 8 KB of RAM,
 * about 0.5 s of history at the Day-1 workload's event rate. */
#define RB_CAPACITY 1024U

typedef struct {
    event_t           buf[RB_CAPACITY];
    volatile uint32_t head;      /* next write index */
    volatile uint32_t tail;      /* next read index (read only by the D dump and rb_copy_last (detection uses the window counters)) */
    volatile uint32_t dropped;   /* count of events overwritten before being read */
} ring_buffer_t;

void     rb_init(ring_buffer_t *rb);

/* ISR-safe: disables interrupts around the head/tail update since this
 * may be called from trace hooks or ISR context (CAP-3/CAP-6, later). */
void     rb_push(ring_buffer_t *rb, const event_t *e);

/* Not ISR-safe by design -- only the single consumer (Detect's drain
 * call) should pop. Returns 0 if empty, 1 on success. */
int      rb_pop(ring_buffer_t *rb, event_t *out);

uint32_t rb_count(const ring_buffer_t *rb);

/* Non-destructive read: i = 0 is the oldest stored event. Returns 0 if i
 * is out of range. Used by the 'D' dump (with recording frozen). */
int      rb_peek(const ring_buffer_t *rb, uint32_t i, event_t *out);

/* Copy the newest n events (oldest first) into out[]; returns how many
 * were copied (< n if the buffer holds fewer). ISR-safe. Used to attach
 * "relevant execution history" to a fault report. */
uint32_t rb_copy_last(const ring_buffer_t *rb, event_t *out, uint32_t n);

#ifdef __cplusplus
}
#endif

#endif /* RING_BUFFER_H */

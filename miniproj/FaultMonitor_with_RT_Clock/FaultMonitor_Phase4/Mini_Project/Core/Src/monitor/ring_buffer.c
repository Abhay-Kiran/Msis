/*
 * ring_buffer.c -- see ring_buffer.h.
 *
 * rb_push masks interrupts around the index update: trace hooks run
 * inside the scheduler and ISR events can nest, so pushes may race.
 * PRIMASK is saved and restored (not blindly re-enabled) because the
 * caller may already be running with interrupts masked, e.g. the
 * FreeRTOS context switch.
 */
#include "ring_buffer.h"
#include "cmsis_gcc.h"

#define RB_MASK (RB_CAPACITY - 1U)

void rb_init(ring_buffer_t *rb)
{
    rb->head    = 0;
    rb->tail    = 0;
    rb->dropped = 0;
}

void rb_push(ring_buffer_t *rb, const event_t *e)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    uint32_t next_head = (rb->head + 1U) & RB_MASK;
    if (next_head == (rb->tail & RB_MASK)) {
        /* full: drop the oldest to make room, count it */
        rb->tail = (rb->tail + 1U) & RB_MASK;
        rb->dropped++;
    }
    rb->buf[rb->head & RB_MASK] = *e;
    rb->head = next_head;

    __set_PRIMASK(primask);
}

int rb_pop(ring_buffer_t *rb, event_t *out)
{
    if (rb_count(rb) == 0U) {
        return 0;
    }
    *out = rb->buf[rb->tail & RB_MASK];
    rb->tail = (rb->tail + 1U) & RB_MASK;
    return 1;
}

uint32_t rb_count(const ring_buffer_t *rb)
{
    return (rb->head - rb->tail) & RB_MASK;
}

int rb_peek(const ring_buffer_t *rb, uint32_t i, event_t *out)
{
    if (i >= rb_count(rb)) {
        return 0;
    }
    *out = rb->buf[(rb->tail + i) & RB_MASK];
    return 1;
}

uint32_t rb_copy_last(const ring_buffer_t *rb, event_t *out, uint32_t n)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    uint32_t count = rb_count(rb);
    if (n > count) {
        n = count;
    }
    uint32_t start = (rb->head - n) & RB_MASK;
    for (uint32_t i = 0; i < n; i++) {
        out[i] = rb->buf[(start + i) & RB_MASK];
    }

    __set_PRIMASK(primask);
    return n;
}

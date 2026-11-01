/*
 * app_tick.c -- the TIM6 1 kHz application tick (kept separate from the
 * sensor peripherals in app_periph.c). Owner: Abhishek.
 *
 * TIM6 is configured by CubeMX (MX_TIM6_Init in main.c): 84 MHz / 8400 /
 * 10 = 1 kHz. Its interrupt is wrapped in FM_ISR_ENTER/EXIT(COMP_ISR_TIM6)
 * in stm32f4xx_it.c and calls app_tick_isr() + fault_inject_on_tick().
 */
#include "app_hooks.h"
#include "main.h"

static volatile uint32_t s_ms;
static volatile bool     s_storm;

void app_tick_isr(void)
{
    s_ms++;
    if (s_storm) {
        /* the injected ISR storm: extra work on every (10x faster) tick */
        for (volatile uint32_t i = 0; i < 300U; i++) {
        }
    }
}

uint32_t app_ms(void) { return s_ms; }

void app_tim6_storm(bool on)
{
    s_storm = on;
    /* normal: 84 MHz / 8400 / 10 = 1 kHz; storm: prescaler /840 -> 10 kHz
     * (ARR stays 9: a basic timer with ARR = 0 stops counting) */
    TIM6->PSC = on ? 839U : 8399U;
    TIM6->ARR = 9U;
    TIM6->EGR = TIM_EGR_UG;
}

/* Recovery for the tim6_isr component: back to the normal 1 kHz tick. */
void app_tim6_reinit(uint8_t comp_id)
{
    (void)comp_id;
    app_tim6_storm(false);
}

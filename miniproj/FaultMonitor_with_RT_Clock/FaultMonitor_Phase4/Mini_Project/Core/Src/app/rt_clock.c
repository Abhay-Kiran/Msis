/*
 * rt_clock.c -- add-on application: REAL-TIME SAMPLE CLOCK with heartbeat.
 *
 * WHY: injected faults are detected and recovered in ~0.1-0.3 s, so their
 * effect on the sensor application vanishes before anyone can see it. This
 * add-on keeps the evidence: a clock that keeps time from the real-time
 * sampling hardware and shows, after every fault, how much time / how many
 * deadlines the application lost.
 *
 * NO EXISTING FILE IS CHANGED. The task starts itself through a GCC
 * constructor (runs from __libc_init_array in the startup file, before
 * main()) and uses only public functions the project already provides:
 *   app_adc_block().seq   hardware count of 10 ms sample blocks (DMA ISR)
 *   osKernelGetTickCount  FreeRTOS 1 kHz SysTick = independent reference
 *   fm_uart_printf        output;  fm_shell_register  the 'Z' command
 * Its task control block and stack are static, so it takes nothing from
 * the FreeRTOS heap the existing tasks use.
 *
 * ---- What it does every 10 ms (priority BelowNormal) -------------------
 *   clock time  += 10 ms for every sample block the hardware delivered
 *   reference   =  SysTick milliseconds since the clock started
 *   drift       =  clock time - reference          (0 when all is well)
 *   missed      += every 10 ms period in which this task could not run
 *   heartbeat LED on D12 (PA6) toggles every 0.5 s -> blinks at 1 Hz
 * Once per second:  CLK 00:01:23 | ref 00:01:23 | drift +0 ms | missed 0
 * Immediately, when something goes wrong:
 *   CLK! missed 14 deadline(s): task blocked ~150 ms
 *   CLK! drift -120 ms: sample blocks lost (sampling stopped)
 *   CLK! drift +950 ms: sample blocks arriving too fast
 *
 * ---- How the faults show up --------------------------------------------
 *   stuck loop (I 1 0)      CPU hogged above us: missed deadlines, LED pauses
 *   ADC trigger (I 7 9)     hardware stops sampling: drift goes negative
 *   TIM2 prescaler (I 7 10) hardware samples 10x faster: drift goes positive
 *   watchdog hang (I 9 0)   everything frozen 4 s: LED stops, then reboot
 *   reset faults (2,5,6)    clock restarts at 00:00:00 after the reboot
 *   ISR storm, GPIOA/USART2 corruption, UART frame, sequence repeat:
 *                           drift 0, missed 0 -> fault caught, no damage
 *   missed deadline (I 3 0) the sensor task oversleeps but the hardware and
 *                           the CPU carry on, so this clock sees nothing
 *
 * ---- Commands ('Z' is free in the shell) --------------------------------
 *   Z        status now           Z R   resync (drift, missed back to 0)
 *   Z 0/1    per-second CLK line off/on (the CLK! event lines always print)
 *
 * ---- Hardware ------------------------------------------------------------
 *   LED + 330 ohm: D12 (PA6) -> resistor -> LED long leg; short leg -> GND.
 *   PA6 is not used by the project and is outside the GPIOA bits the
 *   monitor checksums (PA0, PA5), so it does not disturb fault detection.
 *
 * Not a monitored component: faults cannot target it; it observes damage.
 */
#include "app_periph.h"      /* app_adc_block()           (existing, unchanged) */
#include "fm_uart.h"         /* fm_uart_printf()          (existing, unchanged) */
#include "uart_shell.h"      /* fm_shell_register()       (existing, unchanged) */

#include "main.h"
#include "cmsis_os.h"
#include "FreeRTOS.h"
#include "task.h"

#include <stdbool.h>
#include <stdint.h>

#define RTC_PERIOD_MS      10U      /* one sample block                       */
#define RTC_BLOCK_MS       10U      /* time represented by one block          */
#define RTC_HB_TOGGLE      50U      /* periods per LED toggle (0.5 s -> 1 Hz) */
#define RTC_PRINT_EVERY    100U     /* periods per CLK line (1 s)             */
#define RTC_DRIFT_EVENT_MS 30       /* drift change that is reported          */
#define RTC_HB_PIN         6U       /* PA6 = Arduino D12                      */
#define RTC_STACK_WORDS    512U

/* ---- State ----------------------------------------------------------------- */
static volatile bool     s_print_off;      /* .bss (zero): keeps .data, and so
                                            * the MPU-protected config address,
                                            * exactly as before this add-on */
static volatile bool     s_resync;
static volatile uint32_t s_blocks;          /* sample blocks counted           */
static volatile uint32_t s_ref0;            /* SysTick ms at (re)sync          */
static volatile uint32_t s_missed;          /* deadlines this task missed      */
static volatile uint32_t s_max_block_ms;    /* longest time this task was held */
static volatile int32_t  s_drift_ms;

/* ---- Static task (no FreeRTOS heap used) ---------------------------------- */
static StaticTask_t s_tcb;
static StackType_t  s_stack[RTC_STACK_WORDS] __attribute__((aligned(8)));
static const osThreadAttr_t s_attr = {
    .name       = "rtclock",
    .cb_mem     = &s_tcb,
    .cb_size    = sizeof(s_tcb),
    .stack_mem  = s_stack,
    .stack_size = sizeof(s_stack),
    .priority   = osPriorityBelowNormal,   /* below sensor and report tasks */
};

/* ======================================================================= */
/* Heartbeat LED (PA6)                                                      */
/* ======================================================================= */
static void hb_pin_ensure_output(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    (void)RCC->AHB1ENR;
    /* Re-applied every toggle: if a recovery ever reset GPIOA, the
     * heartbeat comes back by itself. Touches only the PA6 bits. */
    if ((GPIOA->MODER & (3UL << (RTC_HB_PIN * 2U))) != (1UL << (RTC_HB_PIN * 2U))) {
        GPIOA->MODER = (GPIOA->MODER & ~(3UL << (RTC_HB_PIN * 2U))) | (1UL << (RTC_HB_PIN * 2U));
        GPIOA->OTYPER &= ~(1UL << RTC_HB_PIN);
    }
}

static void hb_set(bool on)
{
    GPIOA->BSRR = on ? (1UL << RTC_HB_PIN) : (1UL << (RTC_HB_PIN + 16U));
}

/* ======================================================================= */
/* Output                                                                   */
/* ======================================================================= */
static void hms(uint32_t ms, uint32_t *h, uint32_t *m, uint32_t *s)
{
    uint32_t t = ms / 1000U;
    *h = t / 3600U;
    *m = (t / 60U) % 60U;
    *s = t % 60U;
}

static void print_line(const char *tag, uint32_t now)
{
    uint32_t ch, cm, cs, rh, rm, rs;
    uint32_t ref = now - s_ref0;
    hms(s_blocks * RTC_BLOCK_MS, &ch, &cm, &cs);
    hms(ref, &rh, &rm, &rs);
    fm_uart_printf("%s %02lu:%02lu:%02lu | ref %02lu:%02lu:%02lu | drift %+ld ms | missed %lu\r\n",
                   tag, (unsigned long)ch, (unsigned long)cm, (unsigned long)cs,
                   (unsigned long)rh, (unsigned long)rm, (unsigned long)rs,
                   (long)s_drift_ms, (unsigned long)s_missed);
}

/* 'Z' -- status, 'Z 0' / 'Z 1' -- per-second line off/on, 'Z R' -- resync */
static void cmd_clock(const char *args)
{
    while (*args == ' ') {
        args++;
    }
    if (*args == '0') {
        s_print_off = true;
    } else if (*args == '1') {
        s_print_off = false;
    } else if (*args == 'R' || *args == 'r') {
        s_resync = true;
        fm_uart_printf("OK clock resync at the next period\r\n");
        return;
    }
    print_line("CLK", osKernelGetTickCount());
    fm_uart_printf("clock line %s, longest block of this task %lu ms\r\n",
                   s_print_off ? "off" : "on", (unsigned long)s_max_block_ms);
}

/* ======================================================================= */
/* The task                                                                 */
/* ======================================================================= */
static void rt_clock_task(void *arg)
{
    (void)arg;
    fm_shell_register('Z', cmd_clock);   /* shell is initialised before the scheduler */
    hb_pin_ensure_output();
    hb_set(false);

    osDelay(500U);                       /* let the boot banner finish */
    fm_uart_printf("rt_clock: real-time sample clock started (Z for status, heartbeat on D12)\r\n");

    uint32_t seq_prev  = app_adc_block().seq;
    uint32_t next      = osKernelGetTickCount();
    uint32_t periods   = 0;
    int32_t  drift_rep = 0;              /* last drift that was reported */
    uint32_t drift_evt = 0;              /* SysTick ms of the last drift event */
    bool     hb        = false;
    s_ref0   = next;
    s_blocks = 0;

    for (;;) {
        next += RTC_PERIOD_MS;
        if ((int32_t)(next - osKernelGetTickCount()) > 0) {
            osDelayUntil(next);
        }
        uint32_t now = osKernelGetTickCount();

        /* ---- deadline check: were we kept from running? ---- */
        int32_t late = (int32_t)(now - next);
        if (late >= (int32_t)RTC_PERIOD_MS) {
            uint32_t lost = (uint32_t)late / RTC_PERIOD_MS;
            s_missed += lost;
            if ((uint32_t)late > s_max_block_ms) {
                s_max_block_ms = (uint32_t)late;
            }
            next = now;                  /* resynchronise the period */
            fm_uart_printf("CLK! missed %lu deadline(s): task blocked ~%ld ms\r\n",
                           (unsigned long)lost, (long)late);
        }

        /* ---- clock: one block = 10 ms of time ---- */
        uint32_t seq = app_adc_block().seq;
        uint32_t d   = seq - seq_prev;
        seq_prev = seq;
        if (d > 5000U) {
            d = 0U;                      /* counter restarted: ignore the jump */
        }
        s_blocks += d;

        if (s_resync) {
            s_resync       = false;
            s_blocks       = 0U;
            s_ref0         = now;
            s_missed       = 0U;
            s_max_block_ms = 0U;
            drift_rep      = 0;
        }

        s_drift_ms = (int32_t)(s_blocks * RTC_BLOCK_MS) - (int32_t)(now - s_ref0);

        /* ---- drift events: report a real jump, not the +-10 ms jitter ---- */
        /* at most one event line per 200 ms; the CLK line shows the final value */
        if (((s_drift_ms - drift_rep) >= RTC_DRIFT_EVENT_MS ||
             (drift_rep - s_drift_ms) >= RTC_DRIFT_EVENT_MS) &&
            (now - drift_evt) >= 200U) {
            drift_evt = now;
            fm_uart_printf("CLK! drift %+ld ms: %s\r\n", (long)s_drift_ms,
                           (s_drift_ms < drift_rep) ? "sample blocks lost (sampling stopped)"
                                                    : "sample blocks arriving too fast");
            drift_rep = s_drift_ms;
        }

        /* ---- heartbeat and the once-per-second line ---- */
        periods++;
        if ((periods % RTC_HB_TOGGLE) == 0U) {
            hb = !hb;
            hb_pin_ensure_output();
            hb_set(hb);
        }
        if ((periods % RTC_PRINT_EVERY) == 0U && !s_print_off) {
            print_line("CLK", now);
        }
    }
}

/* ======================================================================= */
/* Self-start: runs before main() via the startup code's __libc_init_array */
/* ======================================================================= */
/* Creating a FreeRTOS task before the scheduler starts is allowed (main()
 * does the same for every other task). The task first runs when
 * osKernelStart() is called at the end of main(). */
__attribute__((constructor)) static void rt_clock_autostart(void)
{
    (void)osThreadNew(rt_clock_task, NULL, &s_attr);
}

/*
 * controller.c -- the sensor monitor's tasks. See controller.h.
 */
#include "app_config.h"
#include "controller.h"
#include "app_periph.h"
#include "app_hooks.h"
#include "fault_inject.h"
#include "fm_uart.h"
#include "uart_shell.h"
#include "fm_trace.h"
#include "fm_capture.h"

#include "main.h"
#include "cmsis_os.h"
#include "FreeRTOS.h"
#include "task.h"

#include <stdlib.h>
#include <stdio.h>

/* ---- Names the framework prints (indexed by app_comp_id_t / stage) ---- */
const char *const g_comp_names[COMP_COUNT] = {
    [COMP_CTRL] = "sensor", [COMP_REPORT] = "report", [COMP_SHELL] = "shell",
    [COMP_MONITOR] = "monitor", [COMP_WATCHDOG] = "wdg",
    [COMP_ISR_ADC] = "adc_isr", [COMP_ISR_TIM6] = "tim6_isr",
    [COMP_ISR_USART2] = "uart_isr", [COMP_ISR_BUTTON] = "btn_isr",
    [COMP_PER_ADC] = "ADC1", [COMP_PER_TIM] = "TIM2", [COMP_PER_GPIO] = "GPIOA",
    [COMP_PER_UART] = "USART2",
};
const char *const g_stage_names[STAGE_COUNT] = {
    [STAGE_A_ACQUIRE] = "A_acquire", [STAGE_B_FILTER] = "B_filter",
    [STAGE_C_COMPARE] = "C_compare", [STAGE_D_ACTUATE] = "D_actuate",
    [STAGE_E_REPORT] = "E_report",
};

/* ---- Configuration (MPU read-only after controller_init) -------------- */
static app_cfg_t s_cfg __attribute__((aligned(32))) = {
    .hyst_mv = 100U, .vref_mv = 3300U, .adc_max = 4095U, .alpha_q8 = 77U, /* ~0.3 */
    .limit_min_mv = 100U, .limit_max_mv = 3200U,
    .block_len = APP_BLOCK_LEN, .reserved = 0U,
};

volatile app_cfg_t *app_cfg(void) { return &s_cfg; }

/* app_hooks.h: the word MEM_RO_WRITE tries to overwrite, and the banner */
volatile uint32_t *app_protected_word(void) { return &s_cfg.hyst_mv; }
const char *app_name(void) { return "sensor monitor"; }

/* ---- State ------------------------------------------------------------- */
static volatile int32_t s_limit_mv = 2000;     /* alarm threshold (runtime)  */
static int32_t          s_avg_q8;              /* smoothed input, 1/256 mV   */
static volatile int32_t s_avg_mv;              /* same, rounded to mV        */
static volatile bool    s_alarm;               /* LED / alarm state          */
static volatile bool    s_tel_on = true;
static volatile bool    s_limit_changed;
static uint32_t         s_last_btn_ms;
static uint32_t         s_frame_no;

/* ---- Sensor plausibility check ('Q 1' on, 'Q 0' off; default off) -------
 * A real sensor never sits exactly at 0 V or 3.3 V: that means a broken
 * wire (input pulled to a rail) or a short. With the check on, PA0 held
 * at a rail for SENSOR_RAIL_MS is a SENSOR FAULT:
 *   - reported ONCE per episode to the framework as a peripheral error on
 *     ADC1 (the monitor then detects, localizes, re-inits ADC, reports);
 *   - the alarm is forced ON (fail-safe: a dead sensor must not look like
 *     "all fine", e.g. a short to GND would otherwise read as 0 mV);
 *   - cleared when the reading is back inside the valid band for
 *     SENSOR_OK_MS.
 * Default OFF because with it on, a jumper to GND or 3V3 -- the normal
 * way to drive the alarm in the earlier tests -- becomes a fault. */
#define SENSOR_RAIL_LO   40U      /* counts, ~32 mV                         */
#define SENSOR_RAIL_HI   4060U    /* counts, ~3.27 V                        */
#define SENSOR_RAIL_MS   1000U    /* at a rail this long  -> fault          */
#define SENSOR_OK_MS     300U     /* back in band this long -> cleared      */
#define BLOCK_MS         (1000U / APP_BLOCK_HZ)
static volatile bool     s_sensor_check;
static volatile bool     s_sensor_fault;
static volatile uint8_t  s_sensor_rail;          /* 1 = low, 2 = high       */
static volatile bool     s_sensor_news;          /* report task prints it   */
static uint32_t          s_rail_ms, s_ok_ms;

/* Called from stage B with the raw block mean (before smoothing, so the
 * check sees the real input, not the filtered one). */
static void sensor_check(uint32_t mean_counts)
{
    if (!s_sensor_check) {
        s_rail_ms = s_ok_ms = 0;
        return;
    }
    bool lo = mean_counts <= SENSOR_RAIL_LO;
    bool hi = mean_counts >= SENSOR_RAIL_HI;
    if (lo || hi) {
        s_ok_ms = 0;
        if (s_rail_ms < SENSOR_RAIL_MS) {
            s_rail_ms += BLOCK_MS;
        } else if (!s_sensor_fault) {
            s_sensor_fault = true;
            s_sensor_rail  = hi ? 2U : 1U;
            s_sensor_news  = true;
            fm_periph_error(COMP_PER_ADC, APP_PERR_SENSOR);   /* once per episode */
        }
    } else {
        s_rail_ms = 0;
        if (s_sensor_fault) {
            s_ok_ms += BLOCK_MS;
            if (s_ok_ms >= SENSOR_OK_MS) {
                s_sensor_fault = false;
                s_sensor_news  = true;
            }
        }
    }
}

static osThreadId_t s_ctrl_h, s_report_h;

static const osThreadAttr_t s_ctrl_attr = {
    .name = "sensor", .stack_size = STACK_CTRL * 4, .priority = PRIO_CTRL,
};
static const osThreadAttr_t s_report_attr = {
    .name = "report", .stack_size = STACK_REPORT * 4, .priority = PRIO_REPORT,
};

/* ---- Stage C: threshold with hysteresis ---------------------------------
 * Switch ON above the limit, switch OFF only after dropping below
 * (limit - hysteresis). Without the gap, a value hovering around the
 * limit would make the LED flicker. */
static void compare_step(void)
{
    int32_t avg   = s_avg_mv;
    int32_t limit = s_limit_mv;
    if (s_sensor_fault) {
        s_alarm = true;           /* fail-safe while the sensor is not plausible */
        return;
    }
    if (!s_alarm && avg > limit) {
        s_alarm = true;
    } else if (s_alarm && avg < (limit - (int32_t)s_cfg.hyst_mv)) {
        s_alarm = false;
    }
}

/* ---- Sensor task: stages A-D, woken by every completed block --------- */
static void ctrl_task_fn(void *arg)
{
    (void)arg;
    app_set_ctrl_task(xTaskGetCurrentTaskHandle());

    for (;;) {
        /* Sleep until the DMA interrupt says a block is ready. 50 ms =
         * five missed blocks: then the sampling chain itself is broken. */
        uint32_t got = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50));
        fault_id_t f = inject_point(COMP_CTRL);
        if (got == 0U) {
            fm_periph_error(COMP_PER_ADC, APP_PERR_TIMEOUT);
        }

        FM_STAGE(STAGE_A_ACQUIRE);
        app_block_t blk = app_adc_block();

        FM_STAGE(STAGE_B_FILTER);
        uint32_t sum = 0;
        for (uint32_t i = 0; i < s_cfg.block_len && i < APP_BLOCK_LEN; i++) {
            sum += blk.s[i];
        }
        uint32_t mean_counts = sum / s_cfg.block_len;
        sensor_check(mean_counts);
        int32_t  mv = (int32_t)((mean_counts * s_cfg.vref_mv) / s_cfg.adc_max);
        /* Smoothing: avg += alpha * (new - avg), alpha = alpha_q8 / 256.
         * The average is kept in 1/256 mV units: with whole millivolts the
         * integer division would round the last few mV away and the value
         * would stop 3 mV short of the real input. */
        s_avg_q8 += ((mv * 256 - s_avg_q8) * (int32_t)s_cfg.alpha_q8) / 256;
        s_avg_mv  = (s_avg_q8 + 128) / 256;           /* round to nearest mV */

        FM_STAGE(STAGE_C_COMPARE);
        compare_step();
        if (f == FAULT_SEQ_REPEAT) {
            /* the injected sequence fault: A -> B -> C -> C -> C */
            FM_STAGE(STAGE_C_COMPARE);
            compare_step();
            FM_STAGE(STAGE_C_COMPARE);
            compare_step();
        }

        FM_STAGE(STAGE_D_ACTUATE);
        app_led_set(s_alarm);
    }
}

/* ---- Report task: stage E ---------------------------------------------- */
static void report_task_fn(void *arg)
{
    (void)arg;
    char line[96];
    uint32_t next = osKernelGetTickCount();
    for (;;) {
        next += PERIOD_REPORT_MS;
        uint32_t now = osKernelGetTickCount();
        if ((int32_t)(next - now) <= 0) {
            next = now + PERIOD_REPORT_MS;
        }
        osDelayUntil(next);
        inject_point(COMP_REPORT);

        FM_STAGE(STAGE_E_REPORT);
        /* TEL,frame,ms,limit_mV,input_mV,alarm  (same TEL prefix as before
         * so the Python tools keep working) */
        int n = snprintf(line, sizeof(line), "TEL,%lu,%lu,%ld,%ld,%d",
                         (unsigned long)s_frame_no, (unsigned long)now,
                         (long)s_limit_mv, (long)s_avg_mv, s_alarm ? 1 : 0);
        uint8_t cs = 0;                       /* NMEA-style XOR checksum */
        for (int i = 0; i < n; i++) {
            cs ^= (uint8_t)line[i];
        }
        s_frame_no++;
        if (s_sensor_news) {
            s_sensor_news = false;
            if (s_sensor_fault) {
                fm_uart_printf("SENSOR FAULT: input stuck %s for >%u ms, alarm forced ON\r\n",
                               s_sensor_rail == 2U ? "HIGH (3.3 V)" : "LOW (0 V)",
                               (unsigned)SENSOR_RAIL_MS);
            } else {
                fm_uart_printf("SENSOR OK: input back in range, normal alarm logic\r\n");
            }
        }
        if (s_limit_changed) {
            s_limit_changed = false;
            fm_uart_printf("LIMIT %ld mV\r\n", (long)s_limit_mv);
        }
        if (s_tel_on && (s_frame_no % 10U) == 0U) {
            fm_uart_printf("%s*%02X\r\n", line, cs);
        }
    }
}

/* ---- Inputs --------------------------------------------------------- */
/* User button B1 (PC13, EXTI line 13): step the limit through 4 values. */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    static const int32_t steps[] = { 1000, 1650, 2000, 2500 };
    if (GPIO_Pin != GPIO_PIN_13) {
        return;
    }
    uint32_t t = app_ms();
    if ((t - s_last_btn_ms) < 250U) {
        return;                              /* contact bounce */
    }
    s_last_btn_ms = t;
    int32_t next = steps[0];
    for (uint32_t i = 0; i < sizeof(steps) / sizeof(steps[0]); i++) {
        if (steps[i] > s_limit_mv) {
            next = steps[i];
            break;
        }
    }
    s_limit_mv = next;
    s_limit_changed = true;
}

/* 'T <mV>' */
static void cmd_limit(const char *args)
{
    int v = atoi(args);
    if (v < (int)s_cfg.limit_min_mv || v > (int)s_cfg.limit_max_mv) {
        fm_uart_printf("ERR usage: T <%lu..%lu>\r\n",
                       (unsigned long)s_cfg.limit_min_mv, (unsigned long)s_cfg.limit_max_mv);
        return;
    }
    s_limit_mv = v;
    s_limit_changed = true;
    fm_uart_printf("OK limit %d mV\r\n", v);
}

/* 'Y 1' / 'Y 0' */
static void cmd_telemetry(const char *args)
{
    s_tel_on = (atoi(args) != 0);
    fm_uart_printf("OK telemetry %s\r\n", s_tel_on ? "on" : "off");
}

/* 'Q 1' / 'Q 0' -- sensor plausibility check on/off, 'Q' -- status */
static void cmd_sensor_check(const char *args)
{
    while (*args == ' ') {
        args++;
    }
    if (*args != '\0') {
        s_sensor_check = (atoi(args) != 0);
        if (!s_sensor_check) {
            s_sensor_fault = false;
        }
    }
    fm_uart_printf("OK sensor check %s (valid band %u..%u counts = ~%u..%u mV), sensor %s\r\n",
                   s_sensor_check ? "ON" : "OFF", (unsigned)SENSOR_RAIL_LO + 1U,
                   (unsigned)SENSOR_RAIL_HI - 1U,
                   (unsigned)((SENSOR_RAIL_LO * 3300U) / 4095U),
                   (unsigned)((SENSOR_RAIL_HI * 3300U) / 4095U),
                   s_sensor_fault ? "FAULT" : "ok");
}

/* ---- Recovery: restart a task ---------------------------------------- */
void controller_restart(uint8_t comp_id)
{
    if (comp_id == COMP_CTRL) {
        /* the DMA ISR notifies this task: keep it quiet while the
         * handle is invalid */
        NVIC_DisableIRQ(DMA2_Stream0_IRQn);
        app_set_ctrl_task(NULL);
        if (s_ctrl_h) {
            fm_uart_forget_task(s_ctrl_h);
            osThreadTerminate(s_ctrl_h);
        }
        s_ctrl_h = osThreadNew(ctrl_task_fn, NULL, &s_ctrl_attr);
        fm_register_task(COMP_CTRL, s_ctrl_h, "sensor", controller_restart);
        NVIC_EnableIRQ(DMA2_Stream0_IRQn);
    } else if (comp_id == COMP_REPORT) {
        if (s_report_h) {
            fm_uart_forget_task(s_report_h);
            osThreadTerminate(s_report_h);
        }
        s_report_h = osThreadNew(report_task_fn, NULL, &s_report_attr);
        fm_register_task(COMP_REPORT, s_report_h, "report", controller_restart);
    }
}

uint32_t app_task_period_ms(uint8_t comp_id)
{
    switch (comp_id) {
    case COMP_CTRL:   return 1000U / APP_BLOCK_HZ;
    case COMP_REPORT: return PERIOD_REPORT_MS;
    default:          return 100U;
    }
}

void *app_task_handle(uint8_t comp_id)
{
    switch (comp_id) {
    case COMP_CTRL:   return s_ctrl_h;
    case COMP_REPORT: return s_report_h;
    case COMP_SHELL:  return uart_shell_task_handle();
    default:          return NULL;
    }
}

void controller_init(void)
{
    s_ctrl_h   = osThreadNew(ctrl_task_fn, NULL, &s_ctrl_attr);
    s_report_h = osThreadNew(report_task_fn, NULL, &s_report_attr);
    fm_register_task(COMP_CTRL,   s_ctrl_h,   "sensor", controller_restart);
    fm_register_task(COMP_REPORT, s_report_h, "report", controller_restart);

    for (uint32_t s = 0; s < STAGE_COUNT; s++) {
        fm_register_stage((uint8_t)s, s == STAGE_E_REPORT ? COMP_REPORT : COMP_CTRL,
                          g_stage_names[s]);
    }
    /* "memory-access anomaly" target: the configuration can never change
     * at run time, so make it read-only for the CPU itself */
    fm_mpu_protect_ro(&s_cfg, sizeof(s_cfg));

    fm_shell_register('T', cmd_limit);
    fm_shell_register('Y', cmd_telemetry);
    fm_shell_register('Q', cmd_sensor_check);
}

/*
 * fm_watchdog.c -- see fm_watchdog.h. Owner: Arfan.
 */
#include "fm_watchdog.h"
#include "fm_trace.h"

#include "main.h"
#include "cmsis_os.h"

#if FM_ENABLE_MONITOR && FM_ENABLE_DETECT

#define IWDG_KEY_ENABLE  0xCCCCU
#define IWDG_KEY_RELOAD  0xAAAAU
#define IWDG_KEY_ACCESS  0x5555U

static volatile uint32_t s_last_checkin;

static void iwdg_start(void)
{
    /* Stop the IWDG while the core is halted by the debugger, otherwise a
     * breakpoint longer than 4 s resets the board. */
    DBGMCU->APB1FZ |= DBGMCU_APB1_FZ_DBG_IWDG_STOP;

    IWDG->KR  = IWDG_KEY_ENABLE;          /* starts LSI + counter          */
    IWDG->KR  = IWDG_KEY_ACCESS;          /* unlock PR / RLR               */
    IWDG->PR  = 4U;                       /* /64 -> 32 kHz / 64 = 500 Hz   */
    IWDG->RLR = (FM_WDG_TIMEOUT_MS * 500U) / 1000U;   /* 2000 -> 4 s       */
    while (IWDG->SR != 0U) {              /* wait for the update to land   */
    }
    IWDG->KR  = IWDG_KEY_RELOAD;
}

static void feeder_task_fn(void *arg)
{
    (void)arg;
    for (;;) {
        osDelay(FM_WDG_FEED_MS);
        if ((osKernelGetTickCount() - s_last_checkin) < FM_WDG_STALE_MS) {
            IWDG->KR = IWDG_KEY_RELOAD;
            fm_trace_event(fm_current_comp(), EVT_CHECKIN, 0);
        }
    }
}

void fm_watchdog_checkin(void)
{
    s_last_checkin = osKernelGetTickCount();
}

void fm_watchdog_init(uint8_t comp_id)
{
    s_last_checkin = 0;
    static const osThreadAttr_t attr = {
        .name = "wdg", .stack_size = 192 * 4, .priority = osPriorityRealtime,
    };
    osThreadId_t h = osThreadNew(feeder_task_fn, NULL, &attr);
    fm_register_task(comp_id, h, "wdg", NULL);
    iwdg_start();
}

#endif /* FM_ENABLE_MONITOR && FM_ENABLE_DETECT */

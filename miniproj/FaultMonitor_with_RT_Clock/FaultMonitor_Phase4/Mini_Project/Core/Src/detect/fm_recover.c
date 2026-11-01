/*
 * fm_recover.c -- see fm_recover.h. Owner: Arfan.
 */
#include "fm_recover.h"
#include "fm_trace.h"
#include "fm_capture.h"

#include "main.h"

#if FM_ENABLE_MONITOR && FM_ENABLE_DETECT

static uint32_t s_last_ms[FM_MAX_COMP];
static uint8_t  s_level[FM_MAX_COMP];
static uint8_t  s_used[FM_MAX_COMP];

uint8_t fm_recover_level(uint8_t comp_id)
{
    return comp_id < FM_MAX_COMP ? s_level[comp_id] : 0U;
}

fm_action_t fm_recover_plan(uint8_t comp, uint8_t ft, uint32_t now_ms)
{
    if (comp >= FM_MAX_COMP) {
        return ACT_SYSTEM_RESET;
    }
    if (ft == FT_MEM_ACCESS || ft == FT_WATCHDOG) {
        return ACT_LOG_ONLY;           /* handled by the reset itself */
    }

    /* escalation: again within FM_ESCALATE_MS -> one level up */
    if (s_used[comp] && (now_ms - s_last_ms[comp]) < FM_ESCALATE_MS) {
        if (s_level[comp] < 2U) {
            s_level[comp]++;
        }
    } else {
        s_level[comp] = 0;
    }
    s_used[comp]    = 1;
    s_last_ms[comp] = now_ms;

    const fm_comp_info_t *c = &g_fm_comp[comp];
    if (s_level[comp] >= 2U || c->recover == NULL) {
        return ACT_SYSTEM_RESET;
    }
    return (c->kind == FM_KIND_TASK) ? ACT_RESTART_TASK : ACT_REINIT_PERIPH;
}

void fm_recover_execute(uint8_t comp, fm_action_t act)
{
    if (act == ACT_SYSTEM_RESET) {
        g_fm_noinit.planned_reset = 1U;
        g_fm_noinit.suspect_comp  = comp;
        __DSB();
        NVIC_SystemReset();
    }
    if (act != ACT_RESTART_TASK && act != ACT_REINIT_PERIPH) {
        return;
    }
    fm_trace_event(comp, EVT_RECOVERY, (uint16_t)act);
    if (g_fm_comp[comp].kind == FM_KIND_TASK) {
        fm_mpu_unguard_task(comp);    /* its stack is about to be freed */
    }
    g_fm_comp[comp].recover(comp);

    if (s_level[comp] == 1U) {
        /* level 1: also re-initialise every peripheral */
        for (uint32_t i = 0; i < FM_MAX_COMP; i++) {
            if (i != comp && g_fm_comp[i].kind == FM_KIND_PERIPH && g_fm_comp[i].recover) {
                g_fm_comp[i].recover((uint8_t)i);
            }
        }
    }
}

#endif /* FM_ENABLE_MONITOR && FM_ENABLE_DETECT */

/*
 * fm_detect.h -- Detection + localization. Owner: Arfan.
 *
 * Pure C (no HAL, no RTOS): input is one sig_vector_t and the learned
 * baseline_t, output is a loc_result_t ("which component, what kind of
 * fault, how sure"). Two localizers plug into the same localizer_t
 * interface from fm_types.h, so the monitor can switch between them at
 * run time ('M' command) and log both side by side:
 *
 *   fm_localizer_rule  -- explainable rules with causal knowledge
 *                         (priorities, victim suppression, debounce)
 *   fm_localizer_stat  -- per-component anomaly score: the sum of squared
 *                         z-scores of its features (a diagonal Mahalanobis
 *                         distance), no rules, no causal knowledge
 *
 * Both first apply the same "hard evidence" checks (things that are
 * either true or false, not a matter of degree): an unseen stage
 * transition, a changed peripheral configuration, a peripheral/
 * communication error flag. The difference between them is how they
 * judge timing, which is where the comparison is interesting.
 *
 * Adding a third localizer (e.g. a decision tree exported from the
 * offline ML notebook) means writing one more localize() function with
 * the same signature -- nothing else in the framework changes.
 */
#ifndef FM_DETECT_H
#define FM_DETECT_H

#include <stdint.h>
#include <stdbool.h>
#include "fm_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* loc_result_t.rules_hit / fault_report_t.rules_hit bits */
#define FM_RULE_OVERRUN     0x0001U   /* cpu or slice far above baseline     */
#define FM_RULE_LIVENESS    0x0002U   /* silent longer than learned max gap  */
#define FM_RULE_ISR_RATE    0x0004U   /* ISR rate / duration far above        */
#define FM_RULE_STACK       0x0008U   /* free stack nearly exhausted          */
#define FM_RULE_SEQUENCE    0x0010U   /* stage transition never seen          */
#define FM_RULE_PERIPH_CFG  0x0020U   /* peripheral config register changed   */
#define FM_RULE_PERIPH_ERR  0x0040U   /* peripheral status error flag         */
#define FM_RULE_COMM        0x0080U   /* UART error flag / frame checksum     */
#define FM_RULE_TRAP        0x0100U   /* MPU / bus / usage fault (after reset) */
#define FM_RULE_WATCHDOG    0x0200U   /* IWDG reset (after reset)             */
#define FM_RULE_STAT        0x0400U   /* statistical score above threshold    */

/* What the detector needs to know about the application. Filled by the
 * monitor from the component registry (fm_trace.h); filled by hand in
 * the PC unit tests. */
typedef struct {
    uint8_t  kind[FM_MAX_COMP];          /* fm_kind_t                       */
    uint8_t  priority[FM_MAX_COMP];      /* task priority, 0xFF = ISR/periph */
    uint8_t  is_comm[FM_MAX_COMP];       /* errors are comm errors          */
    uint8_t  stage_owner[FM_MAX_STAGES]; /* component that runs each stage  */
    float    k_sigma;                    /* sensitivity, default 4          */
    uint32_t cycles_per_ms;              /* SystemCoreClock / 1000          */
    uint32_t window_ms;                  /* FM_WINDOW_MS                    */
} fm_detect_ctx_t;

void fm_detect_configure(const fm_detect_ctx_t *ctx);
fm_detect_ctx_t *fm_detect_ctx(void);
/* Forget debounce history (after a recovery or a mode change). */
void fm_detect_reset(void);

/* A component counts as periodic if, while learning, it never went quiet
 * for more than 5 windows. Event-driven components (shell, button,
 * UART RX) are not periodic and get no liveness check. */
bool fm_detect_is_periodic(const baseline_t *bl, uint8_t comp);

extern const localizer_t fm_localizer_rule;
extern const localizer_t fm_localizer_stat;

const char *fm_fault_type_str(uint8_t ft);
const char *fm_action_str(uint8_t act);

#ifdef __cplusplus
}
#endif

#endif /* FM_DETECT_H */

/*
 * fm_types.h -- Fault Monitor framework: SHARED, FROZEN interface (v2).
 *
 * Every module (monitor / detect / app / inject / tools) builds against
 * this one header. Agreed by all three on Day 1 (19 Oct 2026). Any change
 * after that must be announced to the whole team, because the Python
 * tools parse these layouts byte-for-byte.
 *
 *   Owners:  event_t, sig_vector_t ......... Abhay   (capture)
 *            fault_report_t, baseline_t,
 *            localizer_t, flash layout ..... Arfan   (detect)
 *            component IDs (app side),
 *            shell letters ................. Abhishek (app/inject/tools)
 *
 * The framework never hard-codes the application: components are small
 * integers (0..FM_MAX_COMP-1) that the application assigns and registers
 * at boot (see fm_trace.h). That is what keeps the framework reusable on
 * a different application or a different Cortex-M3/M4/M7 board.
 */
#ifndef FM_TYPES_H
#define FM_TYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FM_IFACE_VERSION        2U

/* Build switch: 0 compiles the whole framework out (hooks become empty,
 * no monitor task). Used (a) to measure CPU/RAM overhead, monitor on vs
 * off, and (b) as Abhishek's standalone application test. Set it with
 * -DFM_ENABLE_MONITOR=0 in Project Properties -> C/C++ Build -> Settings
 * -> MCU GCC Compiler -> Preprocessor. */
#ifndef FM_ENABLE_MONITOR
#define FM_ENABLE_MONITOR       1
#endif

/* Second switch, only meaningful with FM_ENABLE_MONITOR = 1:
 * 0 = capture only (signatures, 'S' 'V' 'D', MPU traps; no learning,
 * detection, black box, recovery or watchdog). Used while integrating
 * (capture merged, detection not yet) and to split the overhead into
 * "capture cost" and "detection cost". */
#ifndef FM_ENABLE_DETECT
#define FM_ENABLE_DETECT        1
#endif

/* ---- Sizing ------------------------------------------------------------ */
#define FM_MAX_COMP             16U   /* tasks + ISRs + peripherals          */
#define FM_MAX_STAGES            8U   /* pipeline stages (A..E used)         */
#define FM_HISTORY_LEN          16U   /* events copied into a fault report   */
#define FM_WINDOW_MS           100U   /* signature window length             */

/* Special component IDs (never registered by the application). */
#define FM_COMP_OTHER         0xFEU   /* idle, timer service, unregistered   */
#define FM_COMP_NONE          0xFFU

/* ---- Component kinds --------------------------------------------------- */
typedef enum {
    FM_KIND_UNUSED = 0,
    FM_KIND_TASK,
    FM_KIND_ISR,
    FM_KIND_PERIPH,
} fm_kind_t;

/* ---- Event record: 8 bytes, the "custom event encoder" ----------------- */
typedef struct __attribute__((packed)) {
    uint32_t cycle_ts;   /* DWT->CYCCNT at the event                         */
    uint8_t  comp_id;    /* component (0..FM_MAX_COMP-1) or FM_COMP_*        */
    uint8_t  type;       /* fm_event_type_t                                  */
    uint16_t extra;      /* type-specific payload                            */
} event_t;
_Static_assert(sizeof(event_t) == 8, "event_t is a frozen 8-byte wire format");

typedef enum {
    EVT_TASK_IN = 0,     /* task switched in                                 */
    EVT_TASK_OUT,        /* task switched out                                */
    EVT_ISR_ENTER,
    EVT_ISR_EXIT,        /* extra = duration in us (saturated)               */
    EVT_RESERVED_4,      /* (was IDLE_ENTER in v1, unused)                   */
    EVT_RESERVED_5,      /* (was IDLE_EXIT  in v1, unused)                   */
    EVT_STAGE,           /* extra = stage id (A=0 .. E=4)                    */
    EVT_CHECKIN,         /* component reported "alive" to the supervisor     */
    EVT_STACK_FAULT,     /* MPU guard hit, extra = low 16 bits of CFSR       */
    EVT_MEM_FAULT,       /* MemManage/BusFault/UsageFault, extra = CFSR bits */
    EVT_PERIPH_ERR,      /* extra = peripheral-specific flags                */
    EVT_COMM_ERR,        /* extra = FM_COMM_* flags                          */
    EVT_RESET_CAUSE,     /* logged at boot, extra = FM_RST_* flags           */
    EVT_INJECT,          /* ground-truth marker, extra = injected fault id   */
    EVT_FAULT,           /* detector fired, extra = fm_fault_type_t          */
    EVT_RECOVERY,        /* recovery executed, extra = fm_action_t           */
    EVT_MARK,            /* free-form marker for debugging                   */
    EVT_COUNT
} fm_event_type_t;

/* Communication error flags (EVT_COMM_ERR.extra, sig.comm_err_flags). */
#define FM_COMM_ORE   0x0001U   /* UART overrun   */
#define FM_COMM_FE    0x0002U   /* UART framing   */
#define FM_COMM_NE    0x0004U   /* UART noise     */
#define FM_COMM_CRC   0x0008U   /* frame CRC fail */

/* Reset cause flags, decoded from RCC_CSR at boot (EVT_RESET_CAUSE.extra). */
#define FM_RST_POR    0x0001U
#define FM_RST_PIN    0x0002U
#define FM_RST_SW     0x0004U
#define FM_RST_IWDG   0x0008U
#define FM_RST_WWDG   0x0010U
#define FM_RST_LPWR   0x0020U
#define FM_RST_BOR    0x0040U

/* ---- Signature vector: one per FM_WINDOW_MS window --------------------- */
/* Feature-to-component ownership is implicit: comp[i] belongs to
 * component i; trans[][] belongs to the stage owners; the globals are
 * attributed by the localizer using the component registry. Both the
 * rule-based and the statistical localizer read exactly this struct, and
 * the 'V' command streams it as one CSV line per window. */
typedef struct {
    uint16_t runs;            /* task: switch-ins, ISR: entries              */
    uint16_t stack_free_min;  /* task: min free stack seen, words (0xFFFF=n/a) */
    uint32_t cpu_cycles;      /* total cycles executing in this window       */
    uint32_t max_slice;       /* longest single run / ISR duration, cycles   */
    uint32_t max_gap;         /* longest time between activations, cycles
                                 (includes "still silent at window end")    */
} comp_sig_t;

typedef struct {
    uint32_t   window_idx;
    uint32_t   t_start;                           /* DWT at window start   */
    uint32_t   t_len;                             /* window length, cycles */
    comp_sig_t comp[FM_MAX_COMP];
    uint8_t    trans[FM_MAX_STAGES][FM_MAX_STAGES]; /* stage i -> j counts, saturating */
    uint16_t   comm_err_flags;                    /* OR of FM_COMM_*        */
    uint16_t   comm_err_count;
    uint16_t   periph_err_count;
    uint16_t   trap_count;                        /* MPU/bus/usage faults   */
    uint32_t   periph_hash[FM_MAX_COMP];          /* config-register CRC per
                                                     peripheral component,
                                                     0 for non-peripherals */
    uint16_t   periph_err[FM_MAX_COMP];           /* status-flag errors per
                                                     component (ADC OVR, DMA
                                                     TE, UART ORE/FE/NE...) */
} sig_vector_t;

/* ---- Fault types: what the DETECTOR concludes ------------------------- */
/* (Different from the injector's fault IDs in fm_common.h, which are the
 *  ground truth. The evaluation script maps one onto the other.) */
typedef enum {
    FT_NONE = 0,
    FT_EXEC_OVERRUN,     /* ran far longer than baseline                     */
    FT_STARVED,          /* ran far less / later than baseline               */
    FT_LIVENESS,         /* silent past its learned max gap (stuck / hung)   */
    FT_SEQUENCE,         /* stage transition never seen in baseline          */
    FT_ISR_RATE,         /* ISR rate or duration outside baseline            */
    FT_STACK_OVERFLOW,   /* MPU stack guard                                  */
    FT_MEM_ACCESS,       /* MPU / bus / usage fault outside stack guard      */
    FT_PERIPH,           /* peripheral status or config register deviation   */
    FT_COMM,             /* UART error flags / CRC failures                  */
    FT_WATCHDOG,         /* reset by IWDG (reported after reboot)            */
    FT_COUNT
} fm_fault_type_t;

/* ---- Recovery actions -------------------------------------------------- */
typedef enum {
    ACT_NONE = 0,
    ACT_LOG_ONLY,
    ACT_RESTART_TASK,
    ACT_REINIT_PERIPH,
    ACT_SYSTEM_RESET,
    ACT_COUNT
} fm_action_t;

/* ---- Fault report: stored in the flash black box ----------------------- */
#define FM_REPORT_MAGIC   0x464D5250UL   /* "FMRP" */
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t seq;                 /* monotonically increasing across resets */
    uint32_t cycle_ts;            /* DWT at detection                       */
    uint32_t uptime_ms;           /* kernel ticks at detection              */
    uint8_t  fault_type;          /* fm_fault_type_t                        */
    uint8_t  comp_id;             /* suspected component                    */
    uint8_t  confidence;          /* 0..100                                 */
    uint8_t  action;              /* fm_action_t taken                      */
    uint32_t pc;                  /* faulting / stuck PC, 0 if unknown      */
    uint16_t rules_hit;           /* bitmask of detector rules that fired   */
    uint16_t reset_cause;         /* FM_RST_* at the boot this happened in  */
    uint8_t  localizer;           /* 0 = rule-based, 1 = statistical, ...   */
    uint8_t  reserved[3];
    event_t  history[FM_HISTORY_LEN];
    uint32_t crc32;               /* over everything above                  */
} fault_report_t;
_Static_assert(sizeof(fault_report_t) == 164, "fault_report_t is a frozen flash format");

/* ---- Baseline signature: learned on-chip, stored in flash -------------- */
#define FM_BASELINE_MAGIC 0x464D424CUL   /* "FMBL" */
typedef struct {
    float    mean;               /* of the feature over learning windows     */
    float    sigma;              /* sample standard deviation                */
    float    max;                /* largest value seen while learning        */
} fm_stat_t;

typedef struct {
    fm_stat_t runs;
    fm_stat_t cpu_cycles;
    fm_stat_t max_slice;
    uint32_t  max_gap_seen;      /* largest gap ever seen while learning     */
    uint16_t  stack_free_min;    /* lowest free stack seen while learning    */
    uint16_t  reserved;
} comp_baseline_t;

typedef struct {
    uint32_t        magic;
    uint32_t        version;      /* FM_IFACE_VERSION                        */
    uint32_t        n_windows;    /* windows learned from                    */
    uint32_t        core_hz;      /* SystemCoreClock when learned            */
    comp_baseline_t comp[FM_MAX_COMP];
    uint8_t         trans_allowed[FM_MAX_STAGES]; /* bit j of [i]: i->j seen */
    uint32_t        periph_hash[FM_MAX_COMP];     /* expected config CRC    */
    uint32_t        crc32;                        /* over everything above  */
} baseline_t;

/* ---- Localizer plug-in interface (rule-based now, statistical later) -- */
typedef struct {
    uint8_t  comp_id;                 /* winner, FM_COMP_NONE if no fault   */
    uint8_t  fault_type;              /* fm_fault_type_t                    */
    uint8_t  confidence;              /* 0..100                             */
    uint8_t  reserved;
    uint16_t rules_hit;
    uint16_t score[FM_MAX_COMP];      /* per-component score, for logging   */
} loc_result_t;

typedef struct {
    const char *name;
    void (*localize)(const sig_vector_t *v, const baseline_t *bl, loc_result_t *out);
} localizer_t;

/* ---- Flash layout (STM32F446RE, 512 KB) -------------------------------- *
 * Sectors 0-5 (0x08000000-0x0803FFFF, 256 KB): firmware. The linker
 * script's FLASH length is cut to 256K so code can never land here:
 * Sector 6 (0x08040000, 128 KB): baseline signature
 * Sector 7 (0x08060000, 128 KB): fault-report black box (append-only)  */
#define FM_FLASH_BASELINE_ADDR   0x08040000UL
#define FM_FLASH_BASELINE_SECTOR 6U
#define FM_FLASH_LOG_ADDR        0x08060000UL
#define FM_FLASH_LOG_SECTOR      7U
#define FM_FLASH_SECTOR_SIZE     (128UL * 1024UL)
#define FM_LOG_SLOT_SIZE         256U   /* one fault_report_t per slot     */

/* ---- Shell command letters (single source of truth) ------------------ *
 *  D  dump ring buffer (raw events, CSV)        Abhay
 *  V  stream signature vectors on/off (CSV)     Abhay
 *  S  status: per-component live stats          Abhay
 *  L  learn baseline (commissioning)            Arfan
 *  B  print baseline                            Arfan
 *  R  list stored fault reports                 Arfan
 *  C  clear fault log                           Arfan
 *  M  select localizer (0 rule, 1 statistical)  Arfan
 *  I  inject fault  (I<f> <comp> [delay])       Abhishek
 *  X  cancel a pending injection                Abhishek
 *  G  dump injection ground-truth log           Abhishek
 *  W  examiner poke: W <hex addr> <hex value>   Abhishek
 *  P  examiner: P <comp> <priority>             Abhishek
 *  K  detector sensitivity: K <sigma>          Arfan
 *  T  setpoint (application): T <deg C>         Abhishek
 *  Y  telemetry print on/off: Y 1 / Y 0         Abhishek
 *  H  help                                      Abhishek
 */

#ifdef __cplusplus
}
#endif

#endif /* FM_TYPES_H */

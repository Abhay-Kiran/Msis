# 03 · File by file: what every line of our code does

**Project:** Autonomous Firmware Fault Localization Using Runtime Execution Signatures
**Board:** STM32 Nucleo-F446RE (Cortex-M4, 84 MHz) · FreeRTOS 10.3.1 through CMSIS-RTOS v2
**Team:** Abhay (capture, `Core/Src/monitor`), Arfan (detection, `Core/Src/detect`), Abhishek (application `Core/Src/app`, UART/shell/injection `Core/Src/inject`, Python `tools/`)

---

## Table of contents

- [How to read this](#how-to-read-this)
- [The whole system on one page](#the-whole-system-on-one-page)
- [Part 1 · Shared](#part-1--shared)
  - [Core/Inc/fm_types.h](#coreincfm_typesh)
- [Part 2 · Abhay: capture layer (`Core/Src/monitor`)](#part-2--abhay-capture-layer)
  - [ring_buffer.h / ring_buffer.c](#ring_bufferh--ring_bufferc)
  - [fm_trace.h](#fm_traceh)
  - [fm_trace.c](#fm_tracec)
  - [fm_capture.h](#fm_captureh)
  - [fm_capture.c](#fm_capturec)
  - [fm_sigout.h / fm_sigout.c](#fm_sigouth--fm_sigoutc)
  - [fm_window_task.h / fm_window_task.c](#fm_window_taskh--fm_window_taskc)
- [Part 3 · Arfan: detection layer (`Core/Src/detect`)](#part-3--arfan-detection-layer)
  - [fault_threshold.h / fault_threshold.c](#fault_thresholdh--fault_thresholdc)
  - [fm_learn.h / fm_learn.c](#fm_learnh--fm_learnc)
  - [fm_detect.h / fm_detect.c](#fm_detecth--fm_detectc)
  - [fm_blackbox.h / fm_blackbox.c](#fm_blackboxh--fm_blackboxc)
  - [fm_recover.h / fm_recover.c](#fm_recoverh--fm_recoverc)
  - [fm_watchdog.h / fm_watchdog.c](#fm_watchdogh--fm_watchdogc)
  - [fm_monitor.h / fm_monitor.c](#fm_monitorh--fm_monitorc)
- [Part 4 · Abhishek: application, UART, injection, tools](#part-4--abhishek-application-uart-injection-tools)
  - [app/fm_common.h](#appfm_commonh)
  - [app/app_config.h](#appapp_configh)
  - [app/app_periph.h / app_periph.c](#appapp_periphh--app_periphc)
  - [app/controller.h / controller.c](#appcontrollerh--controllerc)
  - [app/idle_stats.h / idle_stats.c](#appidle_statsh--idle_statsc)
  - [inject/fm_uart.h / fm_uart.c](#injectfm_uarth--fm_uartc)
  - [inject/uart_shell.h / uart_shell.c](#injectuart_shellh--uart_shellc)
  - [inject/fault_inject.h / fault_inject.c](#injectfault_injecth--fault_injectc)
  - [tools/*.py](#the-python-tools)
- [Part 5 · CubeMX-generated files we edited](#part-5--cubemx-generated-files-we-edited)
  - [Core/Inc/FreeRTOSConfig.h](#coreincfreertosconfigh)
  - [Core/Src/main.c](#coresrcmainc)
  - [Core/Src/stm32f4xx_it.c](#coresrcstm32f4xx_itc)
  - [STM32F446RETX_FLASH.ld](#stm32f446retx_flashld)
- [Numbers cheat sheet](#numbers-cheat-sheet)
- [Glossary](#glossary)

---

## How to read this

1. This is the study guide for the **code itself**. The GUIDEBOOK tells you how to run the demo; this file tells you what each line does and why.
2. Files are grouped by owner. Inside each group they are in "bottom-up" order: a file only uses things explained before it.
3. Every file starts with a box: **path, owner, what it is, why it matters, what would break without it.**
4. Then a **Walkthrough**: the file from top to bottom. Short code excerpts are quoted, followed by a plain-English explanation.
5. Critical short functions are explained **literally line by line** (numbered lists that match the lines of the excerpt).
6. Register-level code always names the register, the bit field, the value and what it does physically.
7. Each file ends with **Design choices** (why this way, what the alternative was) and **How to test it alone**.
8. Jargon is defined the first time it appears and again in the [Glossary](#glossary) at the end.
9. "Problem statement items" means the list in our brief: task execution time, ISR timing, stack usage, function execution sequence, peripheral status, communication errors, watchdog events, memory-access anomalies, then detect, localize, report with history, store, recover.
10. Numbers (sizes, clocks, timeouts) were checked against the code. If you change a constant, re-check the [Numbers cheat sheet](#numbers-cheat-sheet).

---

## The whole system on one page

```
  APPLICATION (Abhishek)                   FRAMEWORK
  ----------------------                   ---------
  TIM2 100 Hz --TRGO--> ADC1 --DMA2 S0-->  ISR  ---FM_ISR_ENTER/EXIT--->  CAPTURE (Abhay)
                                  |                                       fm_trace.c
  ctrl task  A->B->C->D  ---FM_STAGE()------------------------------->   - ring buffer of events
  report task  E         ---FM_STAGE()------------------------------->   - live counters per component
  every task switch  ----traceTASK_SWITCHED_IN/OUT (FreeRTOS)-------->   fm_capture.c
  faults / errors  ------fm_periph_error / fm_comm_error / traps----->   - MPU guards, traps, reset cause
                                                                              |
                                              every 100 ms: fm_sig_close_window()
                                                                              v
                                                                      one sig_vector_t
                                                                              |
                                                     DETECTION (Arfan) fm_monitor.c task
                                          learn (fm_learn) | detect + localize (fm_detect)
                                          store (fm_blackbox) | recover (fm_recover) | IWDG (fm_watchdog)
                                                                              |
                                                       UART shell / alerts (Abhishek, fm_uart + uart_shell)
                                                       Python tools on the PC (tools/*.py)
```

The key idea: **every component** (task, interrupt, peripheral) is just a small number `0..15`. The application says what the numbers mean (`fm_common.h`). The framework only counts, compares and blames numbers. That is why the framework is reusable.

---

# Part 1 · Shared

## Core/Inc/fm_types.h

| | |
|---|---|
| **Path** | `Mini_Project/Core/Inc/fm_types.h` |
| **Owner** | Shared. Sections owned by Abhay (event, signature), Arfan (report, baseline, localizer, flash layout), Abhishek (shell letters). |
| **What it is** | The single "contract" header that every module and the Python tools agree on. It defines the 8-byte event record, the per-window signature vector, the fault types, the recovery actions, the fault-report format stored in flash, the baseline format, the plug-in interface for localizers and the flash memory map. It contains no code, only types and constants. |
| **Why it matters** | It is the spine of the whole problem statement: `event_t` is the execution trace, `sig_vector_t` is the "runtime execution signature", `fault_report_t` is the stored report (what, which component, confidence, history), `baseline_t` is "normal behaviour". |
| **What would break without it** | Nothing would compile. Worse, if two modules had their own copies, a small difference would silently corrupt flash records or make the Python parser read garbage. |

### Walkthrough

**Header guard and C++ guard.**

```c
#ifndef FM_TYPES_H
#define FM_TYPES_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
```

- `#ifndef / #define`: if this file is included twice, the second copy is skipped. This is called a *header guard*.
- `<stdint.h>` gives exact-size integer types: `uint8_t` (1 byte), `uint16_t` (2), `uint32_t` (4).
- `extern "C"` only matters if a C++ file includes us: it stops C++ from renaming the functions.

**Version and build switches.**

```c
#define FM_IFACE_VERSION        2U
#ifndef FM_ENABLE_MONITOR
#define FM_ENABLE_MONITOR       1
#endif
#ifndef FM_ENABLE_DETECT
#define FM_ENABLE_DETECT        1
#endif
```

- `FM_IFACE_VERSION = 2`: stored inside the flash baseline. If the format changes, the version number changes and an old baseline is refused (see `fm_bb_load_baseline`). The `U` suffix makes it an unsigned constant.
- `FM_ENABLE_MONITOR`: the big on/off switch. `0` compiles the whole framework out. Used to measure overhead (monitor ON vs OFF) and to test the application alone. `#ifndef` means a value passed on the compiler command line (`-DFM_ENABLE_MONITOR=0`) wins.
- `FM_ENABLE_DETECT`: second switch, only meaningful when the monitor is on. `0` = "capture only": signatures, `S`/`V`/`D` commands and MPU traps work, but there is no learning, detection, black box, recovery or watchdog. Used while integrating and to split the overhead into "capture cost" and "detection cost".

**Sizing constants.**

```c
#define FM_MAX_COMP             16U
#define FM_MAX_STAGES            8U
#define FM_HISTORY_LEN          16U
#define FM_WINDOW_MS           100U
#define FM_COMP_OTHER         0xFEU
#define FM_COMP_NONE          0xFFU
```

- Up to 16 components (the app uses 13), up to 8 pipeline stages (the app uses 5, A to E).
- A fault report carries the last 16 events ("relevant execution history").
- A signature window is 100 ms long. Ten windows per second.
- Two special IDs that the application never uses: `0xFE` = "other" (idle task, timer task, anything unregistered) and `0xFF` = "none" (no component, no fault).

**Component kinds.** `fm_kind_t` is an enum: `UNUSED`, `TASK`, `ISR`, `PERIPH`. The detector treats each kind differently (tasks get overrun and stack checks, ISRs get rate checks, peripherals get register checksums).

**The event record, 8 bytes.**

```c
typedef struct __attribute__((packed)) {
    uint32_t cycle_ts;
    uint8_t  comp_id;
    uint8_t  type;
    uint16_t extra;
} event_t;
_Static_assert(sizeof(event_t) == 8, "event_t is a frozen 8-byte wire format");
```

1. `__attribute__((packed))` tells GCC not to insert padding bytes. Here the fields already line up, so it just guarantees the layout.
2. `cycle_ts`: the value of the CPU cycle counter (`DWT->CYCCNT`) at the moment of the event. At 84 MHz one count is about 11.9 ns, and the 32-bit counter wraps every 2^32 / 84 000 000 = about 51 s. Differences between two stamps are still correct across one wrap because unsigned subtraction wraps too.
3. `comp_id`: which component (0..15, or 0xFE/0xFF).
4. `type`: what happened (`fm_event_type_t` below).
5. `extra`: a 16-bit payload whose meaning depends on the type (for example ISR duration in microseconds, or a stage number).
6. `_Static_assert` is a compile-time check: if anyone changes the struct so that it is no longer 8 bytes, the build fails. The Python tools rely on 8 bytes.

**Event types.** `EVT_TASK_IN`, `EVT_TASK_OUT` (task switched in/out), `EVT_ISR_ENTER`, `EVT_ISR_EXIT` (extra = duration in µs, capped at 65535), two reserved numbers kept so the numbering stays stable, `EVT_STAGE` (extra = stage id), `EVT_CHECKIN` (watchdog feed), `EVT_STACK_FAULT`, `EVT_MEM_FAULT` (extra = low 16 bits of the fault status register), `EVT_PERIPH_ERR`, `EVT_COMM_ERR`, `EVT_RESET_CAUSE`, `EVT_INJECT` (ground truth marker), `EVT_FAULT` (detector fired), `EVT_RECOVERY`, `EVT_MARK`. (In the current code `EVT_RESET_CAUSE` and `EVT_MARK` are defined but never emitted.)

**Flag sets.** `FM_COMM_ORE/FE/NE/CRC` (0x1, 0x2, 0x4, 0x8): UART overrun, framing error, noise, bad frame checksum. `FM_RST_POR/PIN/SW/IWDG/WWDG/LPWR/BOR`: why the chip reset, decoded at boot from the RCC_CSR register.

**The signature vector: the heart of the project.**

```c
typedef struct {
    uint16_t runs;
    uint16_t stack_free_min;
    uint32_t cpu_cycles;
    uint32_t max_slice;
    uint32_t max_gap;
} comp_sig_t;
```

Per component, per 100 ms window:

- `runs`: how many times it started (task switch-ins, or ISR entries).
- `stack_free_min`: free stack in 4-byte words (0xFFFF = not applicable, e.g. an ISR).
- `cpu_cycles`: total CPU cycles it spent running in the window.
- `max_slice`: its longest single run, in cycles.
- `max_gap`: the longest time between two of its activations, **including** "still silent at the end of the window". That last part is what makes a hung task visible.

```c
typedef struct {
    uint32_t   window_idx, t_start, t_len;
    comp_sig_t comp[FM_MAX_COMP];
    uint8_t    trans[FM_MAX_STAGES][FM_MAX_STAGES];
    uint16_t   comm_err_flags, comm_err_count, periph_err_count, trap_count;
    uint32_t   periph_hash[FM_MAX_COMP];
    uint16_t   periph_err[FM_MAX_COMP];
} sig_vector_t;
```

- `window_idx`, `t_start`, `t_len`: window number, start time and length in cycles (about 8.4 million at 84 MHz for 100 ms).
- `comp[16]`: the per-component block above.
- `trans[i][j]`: how many times stage *i* was followed by stage *j* in this window (saturates at 255). This is the "function execution sequence" item: normal is A→B, B→C, C→D, D→A, E→E.
- `comm_err_flags/count`: UART problems seen.
- `periph_err_count`: total of `periph_err[]` this window (status-flag and comm errors of all components). `trap_count`: always 0 while running, because a trap resets the MCU and is reported at the next boot; kept so the format stays fixed.
- `periph_hash[i]`: a CRC-32 of the configuration registers of peripheral *i* ("peripheral status").
- `periph_err[i]`: count of status-flag errors per component (ADC overrun, DMA transfer error, UART errors...).
- Total size: 436 bytes.

**Fault types** (`fm_fault_type_t`): what the detector concludes. `EXEC_OVERRUN`, `STARVED`, `LIVENESS` (silent too long), `SEQUENCE`, `ISR_RATE`, `STACK_OVERFLOW`, `MEM_ACCESS`, `PERIPH`, `COMM`, `WATCHDOG`. These are different from the injector's fault IDs in `fm_common.h` (those are the ground truth, i.e. what we actually did).

**Recovery actions** (`fm_action_t`): `NONE`, `LOG_ONLY`, `RESTART_TASK`, `REINIT_PERIPH`, `SYSTEM_RESET`.

**The fault report (stored in flash), 164 bytes.**

```c
#define FM_REPORT_MAGIC   0x464D5250UL   /* "FMRP" */
typedef struct __attribute__((packed)) {
    uint32_t magic, seq, cycle_ts, uptime_ms;
    uint8_t  fault_type, comp_id, confidence, action;
    uint32_t pc;
    uint16_t rules_hit, reset_cause;
    uint8_t  localizer;
    uint8_t  reserved[3];
    event_t  history[FM_HISTORY_LEN];
    uint32_t crc32;
} fault_report_t;
_Static_assert(sizeof(fault_report_t) == 164, ...);
```

- `magic`: the 4 ASCII letters "FMRP". Erased flash reads 0xFFFFFFFF, so a valid magic says "a report starts here".
- `seq`: report number, never goes backwards, even across resets.
- `cycle_ts`, `uptime_ms`: when it was detected.
- `fault_type`, `comp_id`, `confidence` (0..100), `action`: what failed, who is blamed, how sure, what we did.
- `pc`: program counter (the code address) where the blamed task was, or where the crash happened. 0 if unknown. The PC tool turns it into file:line.
- `rules_hit`: bitmask of which detector rules fired (see `FM_RULE_*` in `fm_detect.h`).
- `reset_cause`: the `FM_RST_*` flags of the boot this happened in.
- `localizer`: 0 = rule-based, 1 = statistical.
- `history[16]`: the last 16 events before detection (16 × 8 = 128 bytes).
- `crc32`: CRC over everything before it. A report half-written during a power cut fails the CRC and is skipped.

**The baseline (learned "normal"), 796 bytes.**

```c
typedef struct { float mean, sigma, max; } fm_stat_t;
typedef struct {
    fm_stat_t runs, cpu_cycles, max_slice;
    uint32_t  max_gap_seen;
    uint16_t  stack_free_min, reserved;
} comp_baseline_t;
typedef struct {
    uint32_t magic, version, n_windows, core_hz;
    comp_baseline_t comp[FM_MAX_COMP];
    uint8_t  trans_allowed[FM_MAX_STAGES];
    uint32_t periph_hash[FM_MAX_COMP];
    uint32_t crc32;
} baseline_t;
```

- For three features (runs, cpu_cycles, max_slice) we keep mean, standard deviation (sigma) and maximum seen while learning.
- `max_gap_seen`: the longest silence ever seen while learning. `stack_free_min`: the lowest free stack ever seen.
- `trans_allowed[i]` is a bitmask: bit *j* set means "stage i → stage j was seen while learning, so it is allowed".
- `periph_hash[i]`: the expected configuration CRC of each peripheral.
- `core_hz`: the clock it was learned at. Cycle counts mean nothing at another clock, so a baseline from a different clock is refused.

**Localizer plug-in.**

```c
typedef struct {
    uint8_t  comp_id, fault_type, confidence, reserved;
    uint16_t rules_hit;
    uint16_t score[FM_MAX_COMP];
} loc_result_t;
typedef struct {
    const char *name;
    void (*localize)(const sig_vector_t *v, const baseline_t *bl, loc_result_t *out);
} localizer_t;
```

- `loc_result_t` is the verdict: the winner component (0xFF if no fault), fault type, confidence, which rules fired, and a score per component (for logging).
- `localizer_t` is a name plus a **function pointer**: "a variable that holds the address of a function". Any function with this signature can be plugged in. We have two: rule-based and statistical. A third (for example a decision tree exported from Python) only needs one more function.

**Flash layout.**

```c
#define FM_FLASH_BASELINE_ADDR   0x08040000UL
#define FM_FLASH_BASELINE_SECTOR 6U
#define FM_FLASH_LOG_ADDR        0x08060000UL
#define FM_FLASH_LOG_SECTOR      7U
#define FM_FLASH_SECTOR_SIZE     (128UL * 1024UL)
#define FM_LOG_SLOT_SIZE         256U
```

The F446RE has 512 KB of flash in 8 sectors: four of 16 KB, one of 64 KB, three of 128 KB. Sectors 0 to 5 add up to 256 KB and hold the program. Sector 6 (starts 0x08040000) holds the baseline. Sector 7 (starts 0x08060000) is the fault log, cut into 256-byte slots (128 KB / 256 = 512 slots, each holding one 164-byte report). The linker script is cut to 256 KB so code can never be placed in sectors 6 and 7.

**Shell letters.** A comment block that is the single source of truth for which module owns which command letter (D, V, S, L, B, R, C, M, I, X, G, W, P, K, T, Y, H).

### Design choices in this file

- **One frozen header, not one per module.** The alternative (each module defines its own structs) is how formats drift apart. The `_Static_assert` lines make the frozen sizes enforceable.
- **8-byte event.** A larger event (with a 64-bit timestamp or a task name) would halve how much history fits in the 8 KB ring buffer. 32-bit cycles plus a 51 s wrap is fine because we only ever compare nearby events.
- **Floats in the baseline.** Mean and sigma need fractions. The Cortex-M4F has a hardware FPU, so floats are cheap. The alternative, fixed-point integers, is harder to read for little gain on this chip.
- **Components as numbers.** The alternative (pointers to task handles everywhere) would tie the framework to FreeRTOS and to this application.

### How to test it alone

Build the project: the two `_Static_assert`s check the frozen sizes. On a PC, `printf("%zu", sizeof(sig_vector_t))` should print 436 and `sizeof(baseline_t)` 796.

---
# Part 2 · Abhay: capture layer

Folder `Core/Src/monitor`. Its job: **watch the firmware run and turn what happens into numbers**, as cheaply as possible, from any context (task, interrupt, even a crash handler).

## ring_buffer.h / ring_buffer.c

| | |
|---|---|
| **Path** | `Core/Src/monitor/ring_buffer.h`, `ring_buffer.c` |
| **Owner** | Abhay |
| **What it is** | A fixed-size circular list of the last 1024 events (8 KB). New events overwrite the oldest when it is full. Think of an aircraft "flight recorder" that always holds the last few hundred milliseconds. |
| **Why it matters** | It provides the "relevant execution history" that every fault report must contain, and the raw trace for the `D` command. |
| **What would break without it** | Fault reports would have no history, and the `D` dump would not exist. Detection itself would still work (it uses counters, not the buffer). |

### Walkthrough

**Header.**

```c
#define RB_CAPACITY 1024U
typedef struct {
    event_t           buf[RB_CAPACITY];
    volatile uint32_t head;
    volatile uint32_t tail;
    volatile uint32_t dropped;
} ring_buffer_t;
```

- 1024 slots × 8 bytes = 8192 bytes, plus three counters = 8204 bytes.
- 1024 is a power of two on purpose: "index modulo 1024" becomes "index AND 1023", a single fast instruction (the *mask trick*).
- `head`: where the next event is written. `tail`: the oldest stored event. `dropped`: how many old events were overwritten.
- `volatile` means "this can change behind the compiler's back (from an interrupt), always read it from memory".

The header declares six functions: `rb_init`, `rb_push` (ISR-safe), `rb_pop` (single consumer only), `rb_count`, `rb_peek` (read event *i* without removing it), `rb_copy_last` (copy the newest *n*, ISR-safe).

**`rb_init`** sets head, tail and dropped to 0. An empty buffer is "head == tail".

**`rb_push`**, line by line:

```c
uint32_t primask = __get_PRIMASK();
__disable_irq();
uint32_t next_head = (rb->head + 1U) & RB_MASK;
if (next_head == (rb->tail & RB_MASK)) {
    rb->tail = (rb->tail + 1U) & RB_MASK;
    rb->dropped++;
}
rb->buf[rb->head & RB_MASK] = *e;
rb->head = next_head;
__set_PRIMASK(primask);
```

1. Read PRIMASK, the CPU bit that says "interrupts are currently disabled". We save it so we can put it back exactly as it was.
2. Disable all maskable interrupts. Now nothing can interrupt us halfway through updating the indexes.
3. Compute where head will be after this write, wrapping at 1024.
4. If that position is the tail, the buffer is full...
5. ...so throw away the oldest event by moving the tail forward...
6. ...and count it as dropped.
7. Copy the 8-byte event into the head slot.
8. Move head forward.
9. Restore PRIMASK. If interrupts were already off when we were called (for example inside the FreeRTOS context switch), they stay off. A blind `__enable_irq()` would have turned them on at the wrong moment.

Because one slot is always kept empty to tell "full" from "empty", the buffer holds at most 1023 events.

**`rb_pop`** returns 0 if empty, otherwise copies the tail event out and advances the tail. It has no interrupt protection, so only one consumer may call it (nothing in the current firmware pops).

**`rb_count`** = `(head - tail) & 1023`, the number of stored events.

**`rb_peek(rb, i, out)`** copies the event `i` places after the tail (0 = oldest) without removing it. Used by `D` while recording is frozen.

**`rb_copy_last(rb, out, n)`**: with interrupts masked, clamps *n* to the number stored, computes `start = (head - n) & 1023`, and copies the *n* newest events, oldest first, into `out`. Used when building a fault report and by the crash handler.

### Design choices

- **Overwrite oldest, never block.** A recorder that stops when full would lose exactly the events just before a fault, which are the ones we want.
- **PRIMASK save/restore instead of a FreeRTOS critical section.** FreeRTOS critical sections only mask interrupts up to priority 5; our hooks run inside the scheduler and in faults, so we need the hard PRIMASK mask, and it must nest safely.
- **Detection does not depend on draining the buffer.** The counters in `fm_trace.c` are the real input to detection. The buffer is only history.

### How to test it alone

On a PC: push 1500 events with increasing timestamps, check `rb_count` is 1023, `dropped` is 477, and `rb_copy_last(..., 16)` returns timestamps 1484..1499.

---

## fm_trace.h

| | |
|---|---|
| **Path** | `Core/Src/monitor/fm_trace.h` |
| **Owner** | Abhay |
| **What it is** | The only header an application needs to be monitored. It declares the component registry (who is who), the instrumentation calls (`FM_ISR_ENTER`, `FM_STAGE`...) and the shared CRC function. With the monitor switched off, every call becomes an empty inline function, so the app compiles unchanged at zero cost. |
| **Why it matters** | It is how task timing, ISR timing, execution sequence, peripheral and communication errors enter the framework. |
| **What would break without it** | The application could not register components or report stages; the framework would see nothing. |

### Walkthrough

```c
typedef void (*fm_recover_fn_t)(uint8_t comp_id);
```
A *recovery callback* type: a pointer to an application function like `controller_restart(comp_id)`. The framework stores it and calls it when it decides to recover that component. This keeps "how to restart my task" knowledge in the application.

```c
typedef struct {
    volatile uint32_t *reg;
    uint32_t           mask;
} fm_reg_check_t;
```
One entry of a peripheral's "registers to checksum" list: the register address and a mask. Only bits set in the mask are included, so bits that change legitimately (a "busy" flag, an interrupt enable that the driver toggles) do not cause false alarms.

```c
typedef struct {
    fm_kind_t kind; const char *name; void *handle;
    bool history; uint8_t priority; fm_recover_fn_t recover;
    const fm_reg_check_t *regs; uint8_t n_regs; bool is_comm;
} fm_comp_info_t;
```
One row of the component registry: kind, printable name, task handle (tasks only), whether its events go into the ring buffer (`history`), its FreeRTOS priority (0xFF for ISRs and peripherals, meaning "above every task"), its recovery function, its register list, and whether its errors count as communication errors.

`fm_stage_info_t` = owner component + function name of one pipeline stage. `fm_injection_t` = timestamp, component and fault of the last injected fault (ground truth).

```c
extern ring_buffer_t   g_fm_rb;
extern fm_comp_info_t  g_fm_comp[FM_MAX_COMP];
extern fm_stage_info_t g_fm_stage[FM_MAX_STAGES];
uint32_t fm_crc32(uint32_t crc, const void *data, uint32_t len);
```
The global ring buffer, registry and stage table (defined in `fm_trace.c`), and the CRC-32 used everywhere (peripheral hashes, baseline, reports).

Then, under `#if FM_ENABLE_MONITOR`, the real API: `fm_trace_init`, `fm_register_task/isr/periph/stage`, `fm_trace_event`, `fm_isr_enter/exit`, `fm_stage`, `fm_periph_error`, `fm_comm_error`, `fm_note_injection`, `fm_last_injection`, the two task hooks, `fm_trace_freeze`, `fm_sig_close_window`, `fm_current_comp`, `fm_last_stage`, `fm_trace_register_shell`, `fm_trace_restart_window`, and three macros:

```c
#define FM_ISR_ENTER(id)  fm_isr_enter((uint8_t)(id))
#define FM_ISR_EXIT(id)   fm_isr_exit((uint8_t)(id))
#define FM_STAGE(s)       fm_stage((uint8_t)(s))
```

Under `#else` (monitor off), each function is `static inline` with an empty body and the macros become `((void)0)`, "do nothing". `fm_current_comp()` returns `FM_COMP_OTHER`.

### Design choices

- **Macros for the hot calls** so the monitor-off build removes them completely. The alternative (`#if` around every call in the app) would clutter the application.
- **Registration at run time, not a fixed table.** The application calls `fm_register_*` at boot. The framework never names "ctrl" or "ADC1" in its own code.

### How to test it alone

Compile a file that includes only `fm_trace.h` with `-DFM_ENABLE_MONITOR=0`: it must build and the calls must disappear from the disassembly.

---

## fm_trace.c

| | |
|---|---|
| **Path** | `Core/Src/monitor/fm_trace.c` |
| **Owner** | Abhay |
| **What it is** | The engine of the capture layer. It receives every task switch, every interrupt entry/exit, every stage marker and error report, and does two quick things for each: (1) write an event to the ring buffer, (2) update the live counters of the current 100 ms window. Every 100 ms the monitor calls `fm_sig_close_window()`, which turns the counters into one `sig_vector_t` and starts a new window. It also holds the `D` (dump) shell command. |
| **Why it matters** | It measures task execution time, ISR timing, execution sequence, peripheral and communication errors: most of the "capture" half of the problem statement. |
| **What would break without it** | No signatures, so no learning and no detection. |

### Walkthrough

**CRC-32 (always compiled, the black box needs it).**

```c
uint32_t fm_crc32(uint32_t crc, const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    crc = ~crc;
    while (len--) {
        crc ^= *p++;
        for (int b = 0; b < 8; b++) {
            crc = (crc >> 1) ^ (0xEDB88320UL & (0U - (crc & 1U)));
        }
    }
    return ~crc;
}
```

1. Treat the data as bytes.
2. Invert the starting value (standard CRC-32 starts from all ones).
3. For each byte: XOR it into the low bits.
4. Process the 8 bits one at a time: shift right; if the bit that fell out was 1, XOR with the polynomial 0xEDB88320. `0U - (crc & 1U)` is a trick: it is 0x00000000 when the bit is 0 and 0xFFFFFFFF when it is 1, so no `if` is needed.
5. Invert at the end. Result: the same CRC-32 as Ethernet, zip and Python's `zlib.crc32`.

Bit-by-bit is slow (about 8 loop steps per byte) but needs no 1 KB lookup table, and we only CRC a few hundred bytes at a time.

**Globals.**

```c
ring_buffer_t   g_fm_rb __attribute__((section(".noinit")));
fm_comp_info_t  g_fm_comp[FM_MAX_COMP];
fm_stage_info_t g_fm_stage[FM_MAX_STAGES];
```

The ring buffer is placed in the `.noinit` RAM section (see the linker script), which the startup code does **not** clear. After a watchdog reset, the events just before the hang are still in RAM and can be copied into the report.

**Live counters.**

```c
typedef struct { uint32_t runs, cpu_cycles, max_slice, max_gap; } acc_t;
static acc_t    s_acc[FM_MAX_COMP];
static uint32_t s_slice_start[FM_MAX_COMP];
static uint32_t s_last_act[FM_MAX_COMP];
static uint8_t  s_seen[FM_MAX_COMP];
static uint16_t s_periph_err[FM_MAX_COMP];
static uint16_t s_comm_err_count, s_comm_err_flags;
static uint8_t  s_trans[FM_MAX_STAGES][FM_MAX_STAGES];
static uint8_t  s_last_stage = 0xFF;
static uint8_t  s_last_stage_by[FM_MAX_COMP + 1U];
static uint8_t  s_cur_task = FM_COMP_OTHER;
#define ISR_NEST_MAX 8U
static uint8_t  s_isr_stack[ISR_NEST_MAX];
static uint8_t  s_isr_depth;
static uint32_t s_window_start, s_window_idx;
static volatile bool s_frozen;
static fm_injection_t s_inject;
```

- `s_acc`: this window's runs / CPU / longest slice / longest gap per component (32-bit here, cut to 16-bit `runs` only at window close).
- `s_slice_start[i]`: when component *i* last started running. `s_last_act[i]`: when it was last activated (for gaps). `s_seen[i]`: has it ever run (no gap before the first run).
- `s_trans`: stage transition counts. `s_last_stage_by[c]`: the last stage run **by component c** (one extra slot for unregistered code). Transitions are counted per task, so "A-D in ctrl" and "E in report" interleaving never looks like a wrong sequence.
- `s_cur_task`: which task is running. `s_isr_stack` / `s_isr_depth`: which interrupts are running, nested up to 8 deep.
- `s_frozen`: when true, nothing is written to the ring buffer (used while `D` reads it).

**Small helpers.**

```c
static inline uint32_t now(void) { return DWT->CYCCNT; }
static inline uint32_t enter_critical(void) { uint32_t pm = __get_PRIMASK(); __disable_irq(); return pm; }
static inline void exit_critical(uint32_t pm) { __set_PRIMASK(pm); }
static inline bool valid(uint8_t id) { return id < FM_MAX_COMP; }
```

`now()` reads the DWT cycle counter (DWT = Data Watchpoint and Trace unit, a debug block in every Cortex-M3/M4/M7 that has a free-running 32-bit cycle counter). The critical-section helpers are the same PRIMASK save/disable/restore pattern as `rb_push`.

**`record()`** writes one event unless recording is frozen or the component was registered with `history = false` (the 1 kHz TIM6 interrupt and the per-byte UART interrupt are excluded, or they would flood the 1024-slot buffer in under a second).

**`activate()` and `finish_slice()`** (caller holds PRIMASK):

```c
static void activate(uint8_t id, uint32_t ts)
{
    if (s_seen[id]) {
        uint32_t gap = ts - s_last_act[id];
        if (gap > s_acc[id].max_gap) s_acc[id].max_gap = gap;
    }
    s_seen[id] = 1; s_last_act[id] = ts; s_slice_start[id] = ts;
    s_acc[id].runs++;
}
static uint32_t finish_slice(uint8_t id, uint32_t ts)
{
    uint32_t slice = ts - s_slice_start[id];
    s_acc[id].cpu_cycles += slice;
    if (slice > s_acc[id].max_slice) s_acc[id].max_slice = slice;
    return slice;
}
```

- `activate`: if this component ran before, the time since its last activation is a gap; keep the largest. Then note "now" as last activation and slice start, and count one run.
- `finish_slice`: the slice is now minus start. Add it to the CPU total and keep the longest.

**`fm_trace_init()`**

```c
CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
DWT->CTRL        |= DWT_CTRL_CYCCNTENA_Msk;
if ((fm_reset_cause() & FM_RST_IWDG) && !g_fm_noinit.crash_valid) {
    rb_copy_last(&g_fm_rb, g_fm_noinit.history, FM_HISTORY_LEN);
}
rb_init(&g_fm_rb);
```

1. `DEMCR.TRCENA` (bit 24 of the Debug Exception and Monitor Control Register) powers the trace blocks, including DWT.
2. `DWT_CTRL.CYCCNTENA` (bit 0) starts the cycle counter.
3. If this boot was caused by the independent watchdog and no crash record was written (a crash writes its own history), copy the last 16 events that survived in `.noinit` RAM into the crash-record area, before...
4. ...clearing the ring buffer for the new run.

The rest zeroes the registry, sets every stage owner to "none", clears all counters, sets "last stage" to 0xFF (none), and stamps the window start.

**Registration.**

`fm_register_task(comp_id, handle, name, restart)` fills the registry row (kind TASK, history on, recover = restart), reads the task's FreeRTOS priority with `uxTaskPriorityGet`, then:

```c
vTaskSetTaskNumber((TaskHandle_t)task_handle, (UBaseType_t)comp_id + 1U);
fm_mpu_guard_task(comp_id, task_handle);
```

- Every FreeRTOS task has a spare number field, `uxTaskNumber`. We store `comp_id + 1` there, so the switch hooks can tell which component a task is with one memory read. 0 keeps meaning "not registered".
- Then it places the MPU stack guard at the bottom of this task's stack (see `fm_capture.c`). Called again after a task restart, the guard moves to the new stack.

`fm_register_isr` fills kind ISR, `history` as asked, priority 0xFF. `fm_register_periph` fills kind PERIPH, the register list, the recovery function and `is_comm`. `fm_register_stage(stage, owner, name)` records who runs a stage.

**`tag_to_comp(tag)`**: tag 0 or > 16 → `FM_COMP_OTHER`, else `tag - 1`.

**`fm_hook_task_in`** (called by FreeRTOS each time a task is switched in):

```c
void fm_hook_task_in(uint32_t tag)
{
    uint8_t  id = tag_to_comp(tag);
    uint32_t ts = now();
    uint32_t pm = enter_critical();
    s_cur_task = id;
    g_fm_noinit.cur_comp = id;
    if (valid(id)) {
        activate(id, ts);
    }
    record(ts, id, EVT_TASK_IN, 0);
    exit_critical(pm);
}
```

1. Turn the task's tag into a component id.
2. Read the cycle counter first (as close to the real switch as possible).
3. Mask interrupts.
4. Remember which task is now running.
5. Also write it into `.noinit` RAM: if the CPU later hangs and the watchdog resets it, this tells us who was running.
6. If it is a registered component...
7. ...count an activation (gap, run count, slice start).
8. Put a `TASK_IN` event in the ring buffer.
9. Restore interrupts.

**`fm_hook_task_out`**: same pattern; if the component is registered and has run, `finish_slice` adds the CPU time; record `TASK_OUT`; set current task to "other".

Note: a task's slice is measured from switch-in to switch-out, so interrupt time that happened in between is counted in the task's slice too.

**`fm_isr_enter`**:

```c
uint32_t ts = now();
uint32_t pm = enter_critical();
if (s_isr_depth < ISR_NEST_MAX) {
    s_isr_stack[s_isr_depth] = comp_id;
}
s_isr_depth++;
if (valid(comp_id)) {
    activate(comp_id, ts);
}
record(ts, comp_id, EVT_ISR_ENTER, 0);
exit_critical(pm);
```

1. Timestamp. 2. Mask. 3-4. Push this ISR on the small "which ISR is running" stack (only if there is room). 5. Count depth anyway, so enter/exit stay balanced. 6-7. Count an activation. 8. Event. 9. Unmask.

**`fm_isr_exit`**:

```c
uint32_t us = 0;
if (valid(comp_id) && s_seen[comp_id]) {
    uint32_t slice = finish_slice(comp_id, ts);
    us = slice / (SystemCoreClock / 1000000U);
}
if (s_isr_depth > 0U) {
    s_isr_depth--;
}
record(ts, comp_id, EVT_ISR_EXIT, (uint16_t)(us > 0xFFFFU ? 0xFFFFU : us));
```

1. Duration in µs defaults to 0. 2-3. Close the slice (CPU cycles, longest). 4. Convert cycles to µs: `SystemCoreClock / 1000000` = 84 cycles per µs. 5-6. Pop the ISR stack. 7. Record the exit with the duration in `extra`, capped at 65535 µs.

**`fm_trace_event`** = timestamp + mask + `record` + unmask. A generic way to log any event.

**`fm_stage`** (the execution-sequence capture):

```c
if (stage >= FM_MAX_STAGES) return;
uint32_t ts = now();
uint32_t pm = enter_critical();
uint8_t cur  = fm_current_comp();
uint8_t slot = valid(cur) ? cur : (uint8_t)FM_MAX_COMP;
uint8_t prev = s_last_stage_by[slot];
if (prev < FM_MAX_STAGES && s_trans[prev][stage] < 0xFFU) {
    s_trans[prev][stage]++;
}
s_last_stage_by[slot] = stage;
s_last_stage = stage;
g_fm_noinit.last_stage = stage;
record(ts, cur, EVT_STAGE, stage);
exit_critical(pm);
```

1. Ignore impossible stage numbers. 2-3. Timestamp, mask.
4. Who is running (the innermost ISR if any, else the current task).
5. Unregistered code uses the extra slot 16.
6. What stage did **this same component** run last?
7-8. If there was one, count the transition prev → stage (saturating at 255).
9. Remember this stage for this component.
10-11. Also remember it globally and in `.noinit` (printed in a watchdog report).
12. Event with `extra` = stage. 13. Unmask.

**`fm_periph_error(comp, flags)`** increments `s_periph_err[comp]` (saturating) and records `EVT_PERIPH_ERR`. **`fm_comm_error(comp, flags)`** does the same and also counts a comm error and ORs the flags into the window's comm flags, then records `EVT_COMM_ERR`.

**`fm_note_injection(comp, fault)`**: stores the ground truth in `s_inject` and in `.noinit` (so it survives a reset), and pushes an `EVT_INJECT` event straight into the ring buffer, bypassing the per-component history filter, because the ground truth must always be there. `fm_last_injection()` returns a copy of `s_inject` under PRIMASK.

**`fm_current_comp()`**: if 1..8 ISRs are nested, return the innermost; else the current task. **`fm_last_stage()`** and **`fm_trace_freeze()`** are one-liners.

**`periph_hash(c)`**: for each register in the component's list, read it, AND with its mask, and feed the 4 bytes into the CRC.

**`fm_sig_close_window(out)`** (called by the monitor every 100 ms):

```c
uint32_t ts = now();
uint32_t pm = enter_critical();
memset(out, 0, sizeof(*out));
out->window_idx = s_window_idx++;
out->t_start    = s_window_start;
out->t_len      = ts - s_window_start;
for (uint32_t i = 0; i < FM_MAX_COMP; i++) {
    comp_sig_t *c = &out->comp[i];
    c->runs       = (uint16_t)(s_acc[i].runs > 0xFFFFU ? 0xFFFFU : s_acc[i].runs);
    c->cpu_cycles = s_acc[i].cpu_cycles;
    c->max_slice  = s_acc[i].max_slice;
    c->max_gap    = s_acc[i].max_gap;
    if (s_seen[i]) {
        uint32_t silence = ts - s_last_act[i];
        if (silence > c->max_gap) c->max_gap = silence;
    }
    c->stack_free_min  = 0xFFFFU;
    out->periph_err[i] = s_periph_err[i];
}
memcpy(out->trans, s_trans, sizeof(s_trans));
out->comm_err_count = s_comm_err_count;
out->comm_err_flags = s_comm_err_flags;
memset(s_acc, 0, sizeof(s_acc));
memset(s_trans, 0, sizeof(s_trans));
memset(s_periph_err, 0, sizeof(s_periph_err));
s_comm_err_count = 0; s_comm_err_flags = 0;
s_window_start = ts;
exit_critical(pm);
for (uint32_t i = 0; i < FM_MAX_COMP; i++) {
    if (g_fm_comp[i].kind == FM_KIND_PERIPH && g_fm_comp[i].n_regs) {
        out->periph_hash[i] = periph_hash(&g_fm_comp[i]);
    }
}
```

1. Timestamp the end of the window. 2. Mask interrupts (the copy and reset must be atomic, or an event could be counted in neither window or in both).
3. Start from a zeroed vector.
4-6. Window number (then increment), start time, length in cycles.
7-12. Copy each component's counters; `runs` is clipped to 16 bits.
13-16. **The key line for hangs:** if the component has ever run, the time since its last activation counts as a gap too. A task that stopped 300 ms ago has had no gap *inside* the window, but `silence` is 300 ms, so `max_gap` reports it.
17. Stack is "not measured" here; the monitor fills it in afterwards.
18. Copy per-component error counts.
19-21. Copy stage transitions and comm errors.
22-25. Reset every counter for the next window (note: `s_last_act`, `s_seen` and `s_last_stage_by` are **not** reset, so gaps and transitions continue across the boundary).
26. The new window starts now. 27. Unmask.
28-32. Outside the critical section (CRC is slower), compute the configuration hash of every peripheral.

**`fm_trace_restart_window()`**: after a recovery, clear all counters and stage memory, and set `s_last_act[i] = now` for every component that has run, so a restarted task does not look "silent since before the restart". Start a new window.

**`cmd_dump_rb`** (the `D` command): freeze recording, print a header line `#FMDUMP,v2,clk=...,n=...,overwritten=...`, one `#C,id,kind,name` line per registered component, one `#S,stage,owner,name` per stage, then `idx,cycle_ts,comp,type,extra` and one CSV line per event using `rb_peek`, then `END`, then unfreeze. `fm_trace_register_shell()` registers it as letter `D`.

### Design choices

- **Counters, not trace replay.** Detection reads the per-window counters, which are O(1) to update. The alternative (the monitor drains and parses the ring buffer) would cost far more CPU and would fail whenever the buffer overflows.
- **Hooks inside the FreeRTOS context switch** (`traceTASK_SWITCHED_IN/OUT`). The alternative, asking every task to call "I started / I ended", would miss a task that is stuck (it never calls "I ended") and would need app changes.
- **Per-task transition tracking** (`s_last_stage_by`). A single global "last stage" would see `D` (ctrl) followed by `E` (report) and then `A` (ctrl) as random orders depending on scheduling.
- **Silence at window end counts as a gap.** Without it, the liveness rule could not see a hung task.

### How to test it alone

Build with `FM_ENABLE_DETECT=0` (capture only), type `S`: each task should show runs around 10 for ctrl per window (100 Hz × 0.1 s) and 1 for report. Type `D` and run `tools/fm_dump.py parse-dump` on the file.

---
## fm_capture.h

| | |
|---|---|
| **Path** | `Core/Src/monitor/fm_capture.h` |
| **Owner** | Abhay |
| **What it is** | The "hardware side" of capture: everything that touches Cortex-M4 or STM32F446 registers rather than the RTOS. Reset cause, the crash record that survives a reset, the fault-trap entry points, the MPU (memory protection unit) set-up and stack guards, free-stack measurement, and reading where a task is stuck. |
| **Why it matters** | Stack usage, memory-access anomalies and watchdog events, three problem-statement items, are captured here. |
| **What would break without it** | A crash would just hang in an infinite loop with no report; a stack overflow would silently corrupt memory. |

### Walkthrough

`fm_trap_t`: which exception trapped: `NONE`, `HARDFAULT`, `MEMMANAGE`, `BUSFAULT`, `USAGEFAULT`, `STACK_HOOK` (FreeRTOS's own stack checker).

**The crash record in `.noinit` RAM** (176 bytes):

```c
#define FM_NOINIT_MAGIC 0x464D4E49UL   /* "FMNI" */
typedef struct {
    uint32_t magic;
    uint8_t  cur_comp, last_stage, suspect_comp, planned_reset;
    uint32_t inject_ts;
    uint8_t  inject_comp, inject_fault, inject_valid, reserved1;
    uint8_t  crash_valid, trap, fault_type, comp_id;
    uint32_t cycle_ts, pc, lr, cfsr, hfsr, fault_addr;
    event_t  history[FM_HISTORY_LEN];
    uint32_t boot_count;
} fm_noinit_t;
```

- `magic`: "FMNI". If it does not match after boot, the RAM is random (power-on) and the block is cleared.
- Updated continuously while running: `cur_comp` (who ran at the last switch), `last_stage`, `suspect_comp` (monitor's top suspect), `planned_reset` (1 = the monitor itself reset the chip), and the last injection (ground truth).
- Written by a trap handler just before reset: `crash_valid`, `trap` kind, the decided `fault_type`, the blamed `comp_id`, when, the faulting `pc` and `lr` (link register = return address), the fault status registers `cfsr` and `hfsr`, the bad address, and 16 history events.
- `boot_count`: incremented every boot that is not a power-on.

Functions declared: `fm_capture_boot`, `fm_reset_cause`, `fm_reset_cause_str`, `fm_trap` and `fm_trap_stack_hook` (both `noreturn`: the compiler knows they never come back), `fm_mpu_init`, `fm_mpu_protect_ro`, `fm_mpu_guard_task`, `fm_mpu_unguard_task`, `fm_stack_free_words`, `fm_task_pc`.

---

## fm_capture.c

| | |
|---|---|
| **Path** | `Core/Src/monitor/fm_capture.c` |
| **Owner** | Abhay |
| **What it is** | The implementation of everything in `fm_capture.h`, written directly against the registers (references in the file: RM0390 §6.3.21 for RCC_CSR, PM0214 §4.2 MPU and §4.4.14-4.4.19 fault registers). When the monitor is compiled out, every function becomes a stub that behaves like the default (fault handlers just loop). |
| **Why it matters** | Turns crashes into reports (memory-access anomaly, stack overflow), records reset cause (watchdog events), measures stack, and gives the PC for "down to the source line" localization. |
| **What would break without it** | No MPU guards, no crash reports, no reset cause, no stack figures, no PC in reports. |

### Walkthrough

**The crash record variable.**

```c
fm_noinit_t g_fm_noinit __attribute__((section(".noinit")));
static uint16_t s_reset_cause;
```

Placed in `.noinit` so the C startup code does not zero it.

**`fm_capture_boot()`** (called first thing after `HAL_Init()`):

```c
uint32_t csr = RCC->CSR;
uint16_t f = 0;
if (csr & RCC_CSR_IWDGRSTF) f |= FM_RST_IWDG;
if (csr & RCC_CSR_WWDGRSTF) f |= FM_RST_WWDG;
if (csr & RCC_CSR_SFTRSTF)  f |= FM_RST_SW;
if (csr & RCC_CSR_LPWRRSTF) f |= FM_RST_LPWR;
if (csr & RCC_CSR_PORRSTF)  f |= FM_RST_POR;
if (csr & RCC_CSR_BORRSTF)  f |= FM_RST_BOR;
if (csr & RCC_CSR_PINRSTF)  f |= FM_RST_PIN;
s_reset_cause = f;
RCC->CSR |= RCC_CSR_RMVF;
if ((f & (FM_RST_POR | FM_RST_BOR)) || g_fm_noinit.magic != FM_NOINIT_MAGIC) {
    memset(&g_fm_noinit, 0, sizeof(g_fm_noinit));
    g_fm_noinit.magic    = FM_NOINIT_MAGIC;
    g_fm_noinit.cur_comp = FM_COMP_OTHER;
}
g_fm_noinit.boot_count++;
```

1. Read RCC_CSR (Reset and Clock Control, Control/Status Register). Its top bits are sticky "reset happened because..." flags: bit 29 IWDGRSTF (independent watchdog), 30 WWDGRSTF (window watchdog), 28 SFTRSTF (software, e.g. `NVIC_SystemReset`), 31 LPWRRSTF (low-power), 27 PORRSTF (power-on), 25 BORRSTF (brown-out = supply dipped), 26 PINRSTF (NRST pin).
2-9. Translate each into our `FM_RST_*` flag.
10. Keep it for later.
11. Write 1 to RMVF (bit 24, "remove flags") to clear all those sticky bits, so the next boot reports only its own cause.
12-16. After a power-on or brown-out, RAM content is random, so clear the crash record and set a fresh magic. Same if the magic is wrong.
17. Count the boot.

**`fm_reset_cause_str()`** returns a readable name, most specific first. Every reset also pulses the NRST pin, so "reset button" is only reported when nothing more specific is set.

**MPU constants.** The MPU (memory protection unit) lets us mark up to 8 address ranges ("regions") as no-access, read-only, or no-execute. A violation raises the MemManage exception before the bad access happens.

```c
#define MPU_REGION_NULL      0U
#define MPU_REGION_RO        1U
#define MPU_REGION_GUARD0    2U
#define MPU_NUM_GUARDS       6U
#define GUARD_SIZE           32U
#define GUARD_SIZE_FIELD     4U
```

Region 0 = null-pointer guard, region 1 = read-only config, regions 2 to 7 = six stack guards of 32 bytes each. The RASR SIZE field encodes size as 2^(SIZE+1), so SIZE = 4 gives 32 bytes (the smallest allowed).

```c
#define RASR(ap, size_field, xn) \
    (((uint32_t)(xn) << MPU_RASR_XN_Pos) | ((uint32_t)(ap) << MPU_RASR_AP_Pos) | \
     (1UL << MPU_RASR_S_Pos) | (1UL << MPU_RASR_C_Pos) | \
     ((uint32_t)(size_field) << MPU_RASR_SIZE_Pos) | MPU_RASR_ENABLE_Msk)
```

Builds a value for MPU_RASR (Region Attribute and Size Register): XN (bit 28, execute never), AP (bits 26:24, access permission: 000 = no access, 110 = read-only for everyone), S (bit 18, shareable) and C (bit 17, cacheable) for normal RAM attributes, SIZE (bits 5:1), ENABLE (bit 0).

**`mpu_set(region, base, rasr)`**: with PRIMASK set, write MPU_RNR (region number), MPU_RBAR (base address, low bits masked off), MPU_RASR, then `__DSB()` (data synchronisation barrier: wait until the write is done) and `__ISB()` (instruction barrier: refetch so the new rules apply to the very next instruction).

**`size_field(size)`**: finds the smallest *f* with 2^(f+1) ≥ size. For 1024 it returns 9; for 32 it returns 4.

**`fm_mpu_init()`**:

```c
SCB->VTOR = FLASH_BASE;
__DSB();
MPU->CTRL = 0;
for (uint32_t r = 0; r < 8U; r++) { MPU->RNR = r; MPU->RASR = 0; }
MPU->RNR  = MPU_REGION_NULL;
MPU->RBAR = 0x00000000UL;
MPU->RASR = RASR(0U, size_field(1024U), 1U);
MPU->CTRL = MPU_CTRL_PRIVDEFENA_Msk | MPU_CTRL_ENABLE_Msk;
SCB->SHCSR |= SCB_SHCSR_MEMFAULTENA_Msk | SCB_SHCSR_BUSFAULTENA_Msk | SCB_SHCSR_USGFAULTENA_Msk;
SCB->CCR |= SCB_CCR_DIV_0_TRP_Msk;
__DSB(); __ISB();
```

1. VTOR (Vector Table Offset Register) = 0x08000000. The vector table is also visible at address 0 (boot alias); pointing VTOR at the flash copy means our null guard at address 0 can never block the CPU from fetching an exception vector.
3. MPU off while we reprogram it.
4. Disable all 8 regions.
5-7. Region 0: base 0x00000000, 1 KB, no access, no execute. Any read or write through a NULL pointer (or `ptr->field` up to 1 KB) traps.
8. MPU_CTRL: PRIVDEFENA (bit 2) = "for addresses not in any region, privileged code uses the normal memory map", ENABLE (bit 0) = MPU on. So only our regions change anything.
9. SHCSR (System Handler Control and State Register): enable the MemManage (bit 16), BusFault (bit 17) and UsageFault (bit 18) handlers. Without this, all three escalate to HardFault and we lose the information of which one it was.
10. CCR (Configuration and Control Register) DIV_0_TRP (bit 4): integer divide by zero becomes a UsageFault instead of silently returning 0.
11. Barriers.

**`fm_mpu_protect_ro(addr, size)`**: refuses if region 1 is already used, size < 32, size not a power of two, or address not aligned to size (MPU rules). Otherwise sets region 1 to AP = 110 (read-only), XN. Used by the controller on its 32-byte gains struct.

**`fm_mpu_guard_task(comp_id, handle)`**:

```c
TaskStatus_t st;
vTaskGetInfo((TaskHandle_t)task_handle, &st, pdFALSE, eInvalid);
uint32_t stack_base = (uint32_t)st.pxStackBase;
s_stack_base[comp_id] = stack_base;
int slot = -1;
for (...) if (s_guard_comp[i] == comp_id) { slot = i; break; }
if (slot < 0) for (...) if (s_guard_comp[i] == FM_COMP_NONE) { slot = i; break; }
if (slot < 0) return;
uint32_t guard = (stack_base + 16U + (GUARD_SIZE - 1U)) & ~(GUARD_SIZE - 1U);
s_guard_comp[slot] = comp_id;
s_guard_base[slot] = guard;
mpu_set(MPU_REGION_GUARD0 + (uint32_t)slot, guard, RASR(0U, GUARD_SIZE_FIELD, 1U));
```

1-2. Ask FreeRTOS for the task's info; `pxStackBase` is the **lowest** address of its stack. Stacks grow downwards on ARM, so an overflow runs into this end.
3-4. Remember it (for free-stack measurement).
5-7. Reuse this component's slot if it already has one (task restart), else take a free one.
8. No free MPU region: this task gets no guard (6 are available; we use 5).
9. Guard address: skip the lowest 16 bytes (FreeRTOS's own overflow check reads them) and round **up** to a 32-byte boundary, because an MPU region must be aligned to its size.
10-12. Record and program a 32-byte no-access region there. A stack that grows into it causes a MemManage fault at the first write.

**`guard_slot_of`** finds a component's slot. **`fm_mpu_unguard_task(comp)`**: switches the region off (RASR = 0) and frees the slot. Must be called before a task is deleted: its stack memory returns to the heap and the next owner must be able to write there.

**`fm_stack_free_words(comp)`**: FreeRTOS fills a new stack with 0xA5 bytes. Starting just above the guard (reading inside the guard would trap), count consecutive words still equal to 0xA5A5A5A5, up to 4096. That count is the free stack in words that the task has never touched (a "high-water mark"). Returns 0xFFFF for unknown components.

**`fm_task_pc(handle)`**: where is a (not running) task right now?

```c
if (task_handle == NULL || task_handle == xTaskGetCurrentTaskHandle()) return 0;
TaskStatus_t st;
vTaskGetInfo((TaskHandle_t)task_handle, &st, pdFALSE, eInvalid);
uint32_t lo = (uint32_t)st.pxStackBase;
const uint32_t *top = *(const uint32_t *const *)task_handle;
uint32_t t = (uint32_t)top;
if (t < lo || (t & 3U) || t > lo + 0x10000U) return 0;
uint32_t exc_return = top[8];
uint32_t idx = 9U + (((exc_return & 0x10U) == 0U) ? 16U : 0U) + 6U;
return top[idx];
```

1. Can't read our own saved context (we are running, nothing is saved), so return 0.
2-4. Get the stack's lowest address.
5. A FreeRTOS task handle points to its TCB (task control block), whose **first** field is `pxTopOfStack`, the saved stack pointer. Read it.
6-7. Sanity check: inside the stack, 4-byte aligned, not absurdly far. Otherwise 0.
8. When FreeRTOS (ARM_CM4F port) switched this task out, it pushed r4-r11 (8 words, `top[0..7]`) and then EXC_RETURN (`top[8]`). EXC_RETURN bit 4 = 0 means the task used the FPU, so 16 more words (s16-s31) follow.
9. After those comes the frame the hardware pushed on exception entry: r0, r1, r2, r3, r12, lr, **pc**, xpsr. PC is at offset 6 of that frame. So the index is 9 (+16 if FPU) + 6.
10. Return the saved PC: for a task stuck in a loop, the address of that loop. `arm-none-eabi-addr2line` turns it into file:line.

**`addr_in_guard(addr)`**: is a fault address inside any active guard?

**`finish_crash_record(type, comp)`** (never returns):

```c
g_fm_noinit.fault_type = type;
g_fm_noinit.comp_id    = comp;
g_fm_noinit.cycle_ts   = DWT->CYCCNT;
fm_trace_event(comp, type == FT_STACK_OVERFLOW ? EVT_STACK_FAULT : EVT_MEM_FAULT,
               (uint16_t)(g_fm_noinit.cfsr & 0xFFFFU));
rb_copy_last(&g_fm_rb, g_fm_noinit.history, FM_HISTORY_LEN);
g_fm_noinit.crash_valid = 1U;
__DSB();
NVIC_SystemReset();
for (;;) { }
```

1-3. Store the verdict and the time. 4-5. Log the fault itself as the newest event (extra = low half of CFSR). 6. Copy the last 16 events into the crash record. 7. Mark it valid (last, so a half-written record is never "valid"). 8. Make sure the writes reached RAM. 9. Reset the chip (sets the SFTRSTF flag). 10. Never reached.

**`fm_trap(kind, exc_return)`**: called from the top of each fault handler:

```c
__disable_irq();
uint32_t cfsr = SCB->CFSR;
uint32_t hfsr = SCB->HFSR;
uint32_t addr = 0;
if (cfsr & SCB_CFSR_MMARVALID_Msk)      addr = SCB->MMFAR;
else if (cfsr & SCB_CFSR_BFARVALID_Msk) addr = SCB->BFAR;
uint32_t pc = 0, lr = 0;
bool from_task = (exc_return & 0x4U) != 0U;
if (from_task && !(cfsr & SCB_CFSR_MSTKERR_Msk)) {
    const uint32_t *frame = (const uint32_t *)__get_PSP();
    lr = frame[5];
    pc = frame[6];
}
uint8_t type = FT_MEM_ACCESS;
if ((cfsr & SCB_CFSR_MSTKERR_Msk) || addr_in_guard(addr)) {
    type = FT_STACK_OVERFLOW;
}
g_fm_noinit.trap = (uint8_t)kind; /* ... pc, lr, cfsr, hfsr, fault_addr ... */
finish_crash_record(type, fm_current_comp());
```

1. Interrupts off: nothing may run while we write the record.
2. CFSR (Configurable Fault Status Register) says what went wrong: its low byte is MemManage status (e.g. DACCVIOL = data access violation), the next byte is BusFault status, the top half UsageFault (e.g. DIVBYZERO).
3. HFSR (HardFault Status Register): e.g. FORCED = an escalated fault.
4-6. If MMARVALID (CFSR bit 7) is set, MMFAR holds the address that violated the MPU; else if BFARVALID (bit 15), BFAR holds the bad bus address.
7-8. EXC_RETURN bit 2 = 1 means the exception came from thread mode using the process stack (PSP), i.e. from a FreeRTOS task.
9-12. If so, and stacking itself did not fail (MSTKERR = "the exception frame could not be pushed"), the hardware-pushed frame is at PSP: word 5 = LR, word 6 = PC of the faulting instruction.
13-16. Default verdict: memory-access fault. It becomes a stack overflow if stacking failed or the bad address lies inside a stack guard.
17. Copy everything into the crash record.
18. Blame whoever is running (innermost ISR or current task), save history and reset.

**`fm_trap_stack_hook(handle)`**: called from FreeRTOS's `vApplicationStackOverflowHook`. Reads the task's tag (`uxTaskGetTaskNumber`), converts it to a component, clears pc/lr/status fields, and finishes the crash record as `FT_STACK_OVERFLOW`.

**Monitor-off stubs** (`#else` branch): `fm_trap` and `fm_trap_stack_hook` just loop forever (like the CubeMX default handlers); the MPU and stack functions do nothing; `fm_stack_free_words` returns 0xFFFF and `fm_task_pc` returns 0.

### Design choices

- **Record and reset, report after reboot.** In a fault handler the system is in an unknown state (the stack may be broken, the UART may be half-way through a transfer). Writing flash or printing there is risky. Writing a small RAM record and resetting is safe; the monitor turns it into a flash report on the next boot. The alternative, trying to recover inside the handler, can turn one fault into a lockup.
- **MPU stack guard vs FreeRTOS's pattern check.** FreeRTOS method 2 checks the stack's last 16 bytes only at a context switch, which is after the damage. The MPU guard traps on the exact write. We keep both (the FreeRTOS one as backup).
- **32-byte guard.** Smallest MPU region. A function with a large local array could in principle jump over 32 bytes and write below the guard; a bigger guard costs more RAM per task.
- **PC from the saved context** gives "which line", not only "which task", at almost no cost.

### How to test it alone

Capture-only build (`-DFM_ENABLE_DETECT=0`), type `I 6 2` (NULL write from the shell task): after the reboot the window task prints `!TRAP trap=2 type=7 comp=2 ... addr=0x00000010` (trap 2 = MEMMANAGE, type 7 = MEM_ACCESS). In the full build the same test produces a stored `MEM_ACCESS` report.

---

## fm_sigout.h / fm_sigout.c

| | |
|---|---|
| **Path** | `Core/Src/monitor/fm_sigout.h`, `fm_sigout.c` |
| **Owner** | Abhay |
| **What it is** | The output side of capture: the `S` command (a readable table of the latest window, plus CPU/RAM overhead figures) and the `V` command (stream every window as one CSV line). Printing is done by a separate low-priority "sigout" task fed through a queue. |
| **Why it matters** | Makes the signatures visible to humans and to the Python tools (offline ML, overhead measurement). Not needed for on-chip detection. |
| **What would break without it** | No `S`/`V`, no `SIG` stream for `fm_ml.py`, no `OVH`/`IDLE` lines for `fm_metrics.py`. |

### Walkthrough

**`fm_pub_t`** (516 bytes): one window's vector `v`, the two verdicts `rule` and `stat`, the ground-truth label (`label_comp`, `label_fault`) and the monitor `mode` (0 warm-up, 1 learning, 2 monitoring). The header also declares `fm_monitor_print_status()` and `fm_monitor_ram_bytes()` (implemented by the monitor) and `fm_sigout_publish()`.

**Weak defaults.**

```c
__attribute__((weak)) void fm_monitor_print_status(void) { fm_uart_printf("monitor: capture only ..."); }
__attribute__((weak)) uint32_t fm_monitor_ram_bytes(void) { return sizeof(g_fm_rb) + sizeof(g_fm_noinit) + sizeof(g_fm_comp); }
```

`weak` means "use this only if no other file defines a function with the same name". With detection built in, `fm_monitor.c` provides the real ones; in a capture-only build these keep the link working.

**State.** `SIGOUT_QUEUE_LEN 4`, `SIGOUT_LINE_LEN 640`. A 4-deep queue of `fm_pub_t` (about 2 KB of heap), a copy of the latest window for `S` protected by a mutex, the stream on/off flag, a dropped-window counter, and a static 640-byte line buffer.

**`cyc_to_us`** divides cycles by 84. **`append(buf, len, fmt, ...)`** is a bounds-checked `snprintf` that appends at `len` and never runs past 640 bytes.

**`print_csv(p)`** builds one line:

`SIG,<window>,<t_len>` then for each registered component `,id:runs:cpu:max_slice:max_gap:stack_free`, then `,T:` and each non-zero transition `i>j=n;`, then `,E:comm:periph:trap,P:` and each non-zero `id=errors;`, then `,R:comp:type:conf,Q:comp:type:conf,L:comp:fault,M:mode`. It sends it with `fm_uart_write`.

**`printer_task_fn`**: forever, wait on the queue; if streaming is off, discard; if windows were dropped, print `SIGDROP,n` first; print the CSV line.

**`fm_sigout_publish(p)`** (called by the monitor every window, must never block):

```c
if (osMutexAcquire(s_latest_mtx, 0) == osOK) {
    s_latest = *p;
    osMutexRelease(s_latest_mtx);
}
if (s_stream && osMessageQueuePut(s_q, p, 0, 0) != osOK) {
    s_dropped++;
}
```

1-4. Try (timeout 0) to update the copy for `S`; if the shell is reading it right now, skip this window.
5-7. If streaming, try to queue it (timeout 0); if the queue is full (printer behind), count a drop. The monitor never waits for the UART.

**`cmd_status` (`S`)**: copies the latest vector under the mutex (into a `static` so it does not eat the shell's stack), calls `fm_monitor_print_status()`, prints window number, length in µs, core MHz and free heap, then one row per registered component (tasks/ISRs: runs, CPU µs, longest slice µs, longest gap µs, free stack; peripherals: config CRC and errors), the ring buffer fill, an `IDLE,<count>,<uptime_ms>` line (from `idle_stats.c`) and an `OVH,...` line. The overhead line sums the CPU cycles of the framework's own tasks (identified as tasks registered with no recovery function: monitor and wdg) and expresses them in hundredths of a percent of the window, plus framework RAM, trace storage (8204 B) and report size (164 B).

**`cmd_stream` (`V 1` / `V 0`)**: sets the stream flag, resets the drop counter, prints `OK stream on/off`.

**`fm_sigout_init()`**: creates the queue and mutex, clears state, creates the "sigout" task (512 words of stack, `osPriorityLow`), registers `S` and `V`.

### Design choices

- **Printing in a low-priority task.** A 300-character line takes about 26 ms at 115200 baud (11 520 bytes/s). If the high-priority monitor printed it, it would be held up for a quarter of its window. Decoupling with a queue and dropping (and counting) when behind keeps detection timing clean.
- **The sigout task is not registered as a component**, so its CPU time appears under "other" and is not part of the `OVH` CPU figure.

### How to test it alone

Capture-only build, `V 1`, then `python tools/fm_dump.py --port COMx stream -t 10` and open the CSV: about 100 rows, no `SIGDROP`.

---

## fm_window_task.h / fm_window_task.c

| | |
|---|---|
| **Path** | `Core/Src/monitor/fm_window_task.h`, `fm_window_task.c` |
| **Owner** | Abhay |
| **What it is** | A stand-in for the monitor task in the capture-only build (`FM_ENABLE_DETECT = 0`). Every 100 ms it closes the window, adds free-stack figures and publishes the window for `S`/`V`. No learning, detection, storage or recovery. In the normal build the whole file compiles to nothing. |
| **Why it matters** | Lets capture be tested and its overhead measured without detection. |
| **What would break without it** | The capture-only build would have no windows: `S` would show zeros and `V` would print nothing. |

### Walkthrough

Capture-only builds have no monitor to file a boot report, so the task first runs `boot_print()`: it prints `BOOT #n reset cause: ...` and, if a crash record survived in `.noinit`, one `!TRAP trap=.. type=.. comp=.. pc=.. cfsr=.. addr=..` line, then marks the record consumed. Nothing is written to flash.

```c
boot_print();
uint32_t next = osKernelGetTickCount();
fm_sig_close_window(&s_pub.v);
for (;;) {
    next += FM_WINDOW_MS;
    osDelayUntil(next);
    fm_sig_close_window(&s_pub.v);
    /* stack_free_min for each task via fm_stack_free_words() */
    /* rule/stat verdicts = "none" (comp 0xFF) */
    fm_injection_t inj = fm_last_injection();
    s_pub.label_comp  = inj.valid ? inj.comp_id : FM_COMP_NONE;
    s_pub.label_fault = inj.valid ? inj.fault_id : 0U;
    s_pub.mode = 0;
    fm_sigout_publish(&s_pub);
}
```

- Print the boot line (and any crash record); start time; close and throw away the partial boot window.
- Every 100 ms exactly (`osDelayUntil` sleeps until an absolute tick, so delays do not accumulate drift): close the window, fill the stack column, publish.
- The label is the last injection ever made (it is not cleared after some time, unlike in the real monitor).

`fm_window_task_init(comp)` creates the task ("window", 512 words, `osPriorityHigh`) and registers it with no recovery function.

### Design choices

A separate small file keeps `fm_monitor.c` untouched; the alternative (many `#if` inside the monitor) would make the main file harder to read.

### How to test it alone

Build with `-DFM_ENABLE_DETECT=0`; the banner says `monitor CAPTURE ONLY`; `S` shows live numbers.

---
# Part 3 · Arfan: detection layer

Folder `Core/Src/detect`. Its job: **learn what normal looks like, notice when a window is not normal, decide which component is to blame and how sure we are, store a report, and recover.** `fm_learn.c`, `fm_detect.c` and `fault_threshold.c` are pure C (no HAL, no RTOS) so they can be unit-tested on a PC.

## fault_threshold.h / fault_threshold.c

| | |
|---|---|
| **Path** | `Core/Src/detect/fault_threshold.h`, `fault_threshold.c` |
| **Owner** | Arfan |
| **What it is** | Three tiny comparison functions from an earlier design ("DET-1"): is a value outside a band, how confident is that, and is a gap longer than its limit. |
| **Why it matters** | `threshold_liveness_violation` is used by the liveness rule in `fm_detect.c`. The two band functions are kept for unit tests and are not called by the firmware today. |
| **What would break without it** | `fm_detect.c` would not compile (it calls the liveness function). |

### Walkthrough

```c
typedef struct { uint32_t mean, sigma, lo, hi; } task_band_t;
```
A "normal band" in cycles: mean, standard deviation, and the low/high limits (mean ± k·sigma) precomputed offline.

```c
bool threshold_band_violation(uint32_t duration, const task_band_t *band)
{ return duration > band->hi || duration < band->lo; }
```
Outside the band = violation.

```c
uint8_t threshold_band_confidence(uint32_t duration, const task_band_t *band)
{
    if (!threshold_band_violation(duration, band)) return 0;
    uint32_t dev = (duration > band->mean) ? (duration - band->mean) : (band->mean - duration);
    uint32_t conf = band->sigma ? (dev * 100u) / (3u * band->sigma) : 100u;
    if (conf > 100u) conf = 100u;
    return (uint8_t)conf;
}
```
1. Inside the band: confidence 0 by definition. 2. Distance from the mean. 3. Confidence = distance as a percentage of 3 sigma (the "three-sigma rule"); zero sigma means any deviation is certain. 4. Cap at 100.

```c
bool threshold_liveness_violation(uint32_t gap, uint32_t max_gap) { return gap > max_gap; }
```
Silent longer than allowed = violation.

### Design choices

Keeping the threshold maths separate means it can be unit-tested in isolation. The header comment still refers to an older caller name (`fault_detect.c`); the real caller is `fm_detect.c`.

### How to test it alone

On a PC: `threshold_band_confidence(130, &(task_band_t){100,10,70,130})` returns 0 (130 is not > 130); 140 returns 100.

---

## fm_learn.h / fm_learn.c

| | |
|---|---|
| **Path** | `Core/Src/detect/fm_learn.h`, `fm_learn.c` |
| **Owner** | Arfan |
| **What it is** | The on-chip learner. It watches N normal windows (100 by default = 10 s) and remembers, per component, the mean, standard deviation and maximum of runs / CPU cycles / longest slice, the longest gap, the lowest free stack, which stage transitions happened, and each peripheral's configuration CRC. Then it produces a `baseline_t`. |
| **Why it matters** | "Normal" is learned on the real board, not typed in by hand. That is what makes detection work on a new application without re-tuning. |
| **What would break without it** | No baseline, so nothing to compare against: no detection. |

### Walkthrough

```c
typedef struct { uint32_t n; float mean; float m2; float max; } fm_welford_t;
```
One running accumulator per feature: count, running mean, running "sum of squared differences from the mean" (m2), and maximum.

`fm_learn_t` (940 bytes) holds one accumulator per component for runs, cpu and slice, plus max gap, min stack, allowed transitions and peripheral hashes.

**`fm_welford_add`** (Welford's 1962 algorithm), line by line:

```c
w->n++;
float d = x - w->mean;
w->mean += d / (float)w->n;
w->m2   += d * (x - w->mean);
if (w->n == 1U || x > w->max) {
    w->max = x;
}
```

1. One more sample.
2. How far the new value is from the **old** mean.
3. Move the mean a fraction 1/n of the way towards the new value. (After n samples this equals the ordinary average.)
4. Add `d × (distance from the **new** mean)` to m2. This product is the exact update of the sum of squared deviations, without ever storing the samples.
5-7. Track the maximum (the first sample always sets it).

Why not just keep "sum" and "sum of squares"? With floats, `sum_sq/n - mean²` subtracts two big, nearly equal numbers and loses precision (CPU cycles are millions, their squares are 10^13). Welford stays accurate and needs no sample storage.

**`fm_welford_stat`**: mean, sample standard deviation `sqrt(m2 / (n - 1))` (0 if n ≤ 1), max.

**`fm_learn_reset`**: zero everything and set every `stack_min` to 0xFFFF ("nothing seen yet").

**`fm_learn_add(l, v)`**: count the window; for each component add runs, cpu and slice to their accumulators, keep the largest `max_gap`, keep the smallest `stack_free_min`, copy the peripheral hash (the last window's value wins); for every non-zero `trans[a][b]` set bit b of `trans_allowed[a]`.

**`fm_learn_finish(l, out, core_hz)`**: fill the baseline: magic "FMBL", version 2, number of windows, clock; per component the three `fm_stat_t`, max gap, min stack, peripheral hash; the allowed-transition bitmasks; CRC left 0 (the black box computes it when saving).

### Design choices

- **Online statistics** (one pass, constant memory) instead of storing 100 windows × 436 bytes = 43 KB of samples, which would not fit comfortably in 128 KB of RAM.
- **Max and sigma both kept.** Sigma alone is fragile when normal behaviour is almost constant (sigma near 0 means any jitter looks huge), so the detector uses "max of (mean + k·sigma, a multiple of max, an absolute floor)".

### How to test it alone

On a PC, add the samples 2, 4, 4, 4, 5, 5, 7, 9: mean 5, sample sigma about 2.14, max 9.

---

## fm_detect.h / fm_detect.c

| | |
|---|---|
| **Path** | `Core/Src/detect/fm_detect.h`, `fm_detect.c` |
| **Owner** | Arfan |
| **What it is** | The two localizers. Input: one window's `sig_vector_t` and the baseline. Output: a `loc_result_t` = "which component, what kind of fault, how sure". The **rule-based** localizer uses explainable rules plus causal knowledge (priorities, victim suppression, debounce). The **statistical** localizer computes an anomaly score per component (sum of squared z-scores) with no rules. Both share the same "hard evidence" checks. |
| **Why it matters** | This is "detect and localize" in the problem statement, and the rule vs statistical comparison is our extension. |
| **What would break without it** | The monitor would collect windows but never conclude anything. |

### Walkthrough: fm_detect.h

**Rule bits** (stored in `rules_hit`): `OVERRUN 0x001`, `LIVENESS 0x002`, `ISR_RATE 0x004`, `STACK 0x008`, `SEQUENCE 0x010`, `PERIPH_CFG 0x020`, `PERIPH_ERR 0x040`, `COMM 0x080`, `TRAP 0x100`, `WATCHDOG 0x200`, `STAT 0x400`. A report's `rules=0x....` tells you exactly which checks fired.

**`fm_detect_ctx_t`**: what the detector must know about the application, filled by the monitor from the registry: kind and priority of each component, whether its errors are communication errors, the owner of each stage, the sensitivity `k_sigma` (default 4), cycles per ms, window length.

Declared: `fm_detect_configure`, `fm_detect_ctx`, `fm_detect_reset`, `fm_detect_is_periodic`, the two `localizer_t` objects, and the name helpers.

### Walkthrough: fm_detect.c

**Context and debounce memory.**

```c
static fm_detect_ctx_t s_ctx = { .k_sigma = 4.0f, .cycles_per_ms = 84000U, .window_ms = FM_WINDOW_MS };
static uint8_t s_streak_rule[FM_MAX_COMP];
static uint8_t s_streak_stat[FM_MAX_COMP];
```
Defaults (84 000 cycles per ms at 84 MHz). The streak arrays count consecutive windows in which a component had a timing violation, one per localizer.

`fm_detect_configure` copies a context in; `fm_detect_ctx` returns a pointer to it (the `K` command writes `k_sigma` through it); `fm_detect_reset` clears both streak arrays; `fm_fault_type_str` / `fm_action_str` turn numbers into names.

**`fm_detect_is_periodic(bl, i)`**:

```c
float limit = 5.0f * (float)s_ctx.window_ms * (float)s_ctx.cycles_per_ms;
return b->runs.mean > 0.0f && b->max_gap_seen > 0U && (float)b->max_gap_seen < limit;
```
A component is "periodic" if it ran while learning and never went quiet for 5 windows (5 × 100 ms × 84 000 = 42 million cycles = 0.5 s). Event-driven components (button, UART receive) are not periodic and get no liveness check.

**Candidates.**

```c
typedef struct { uint8_t ft; bool hard; float margin; uint16_t rules; } cand_t;
static void offer(cand_t *c, uint8_t ft, bool hard, float margin, uint16_t rule)
{
    c->rules |= rule;
    if (c->ft == FT_NONE || (hard && !c->hard) || (hard == c->hard && margin > c->margin)) {
        c->ft = ft; c->hard = hard; c->margin = margin;
    }
}
```
Each component can have one candidate fault. `margin` = "how far past the threshold" (measured / threshold, so > 1 is a violation). `offer` always records the rule bit, and replaces the candidate if there was none, or the new one is hard evidence and the old was not, or both are the same class and the new margin is bigger.

**`hard_evidence(v, bl, c)`**: things that are simply true or false.

```c
for (a...) for (b...) {
    if (v->trans[a][b] && !(bl->trans_allowed[a] & (1U << b))) {
        uint8_t owner = s_ctx.stage_owner[b];
        if (owner < FM_MAX_COMP) {
            offer(&c[owner], FT_SEQUENCE, true, 1.0f + v->trans[a][b], FM_RULE_SEQUENCE);
            any = true;
        }
    }
}
for (i...) {
    uint8_t ft = s_ctx.is_comm[i] ? FT_COMM : FT_PERIPH;
    if (s_ctx.kind[i] == FM_KIND_PERIPH && bl->periph_hash[i] != 0U &&
        v->periph_hash[i] != bl->periph_hash[i]) {
        offer(&c[i], FT_PERIPH, true, 5.0f, FM_RULE_PERIPH_CFG);
        any = true;
    }
    if (v->periph_err[i] > 0U) {
        offer(&c[i], ft, true, 1.0f + v->periph_err[i], s_ctx.is_comm[i] ? FM_RULE_COMM : FM_RULE_PERIPH_ERR);
        any = true;
    }
}
return any;
```

1-2. For every stage pair that happened in this window...
3. ...but was never seen while learning (bit b of `trans_allowed[a]` is 0), for example C→C in A→B→C→C→C...
4-7. ...blame the owner of the stage that ran out of order, as hard evidence, margin 1 + count.
8. For every component:
9. its error type is COMM if it is a communication peripheral, else PERIPH.
10-13. Peripheral configuration CRC differs from the learned one: hard PERIPH evidence, margin 5.
14-17. Any status-flag error at all (none are expected in normal running): hard evidence, margin 1 + count.
18. Report whether anything hard was found.

**`timing_rules(v, bl, c)`** (rule-based localizer only): for each task and ISR:

```c
const float k = s_ctx.k_sigma, ms = (float)s_ctx.cycles_per_ms, W = (float)v->t_len;
/* tasks */
if (periodic) {
    cpu_hi   = fmaxf3(b->cpu_cycles.mean + k * b->cpu_cycles.sigma, b->cpu_cycles.max * 1.5f, 0.05f * W);
    slice_hi = fmaxf3(b->max_slice.mean + k * b->max_slice.sigma, b->max_slice.max * 2.0f, 1.0f * ms);
} else {
    cpu_hi   = fmaxf(0.5f * W, b->cpu_cycles.max * 1.5f);
    slice_hi = fmaxf(0.5f * W, b->max_slice.max * 2.0f);
}
float m = fmaxf((float)x->cpu_cycles / cpu_hi, (float)x->max_slice / slice_hi);
if (m > 1.0f) offer(&c[i], FT_EXEC_OVERRUN, false, m, FM_RULE_OVERRUN);
```

- **Execution-time overrun.** The limit is the largest of three things: statistics (mean + 4 sigma), experience (1.5 × the biggest CPU total seen, 2 × the longest slice seen), and an absolute floor (5 % of the window = 5 ms of CPU, or a 1 ms slice). The floor stops tiny, very regular tasks from alarming on normal jitter.
- For event-driven tasks only "ate half the window" counts.
- The margin is the worse of the two ratios. Example: a stuck ctrl task uses about 8.4 million cycles in a window; its limit is at least 420 000 (5 %), so margin is about 20.

```c
if (x->stack_free_min != 0xFFFFU && b->stack_free_min != 0xFFFFU &&
    x->stack_free_min < 16U && x->stack_free_min < b->stack_free_min) {
    offer(&c[i], FT_STACK_OVERFLOW, true, 2.0f, FM_RULE_STACK);
}
```
- **Stack nearly exhausted**: fewer than 16 free words (64 bytes) and lower than ever seen while learning. Hard evidence.

```c
/* ISRs */
runs_hi  = periodic ? fmaxf(b->runs.mean + k * b->runs.sigma, b->runs.max * 1.5f) + 2.0f
                    : fmaxf(b->runs.max * 2.0f, 2000.0f);
slice_hi = fmaxf3(b->max_slice.mean + k * b->max_slice.sigma, b->max_slice.max * 3.0f, 0.05f * ms);
cpu_hi   = fmaxf3(b->cpu_cycles.mean + k * b->cpu_cycles.sigma, b->cpu_cycles.max * 2.0f, 0.02f * W);
float m = fmaxf3(x->runs / runs_hi, x->max_slice / slice_hi, x->cpu_cycles / cpu_hi);
if (m > 1.0f) offer(&c[i], FT_ISR_RATE, false, m, FM_RULE_ISR_RATE);
```
- **ISR rate/duration**: too many entries (a periodic ISR gets +2 of slack; an event-driven one may go up to 2000 per window), one entry too long (floor 50 µs), or too much total time (floor 2 % of the window). Example: the TIM6 storm raises TIM6 from about 100 to about 1000 entries per window, margin about 6.

```c
bool regular = (kind == FM_KIND_TASK) || (b->runs.sigma <= 0.2f * b->runs.mean);
if (periodic && regular) {
    uint32_t gap_hi = 2U * b->max_gap_seen + 2U * s_ctx.cycles_per_ms;
    if (threshold_liveness_violation(x->max_gap, gap_hi)) {
        offer(&c[i], FT_LIVENESS, false, (float)x->max_gap / (float)gap_hi, FM_RULE_LIVENESS);
    }
}
```
- **Liveness**: for periodic tasks, and periodic ISRs whose run count is steady (sigma ≤ 20 % of mean, e.g. a timer, not UART traffic), "silent longer than twice the longest silence ever seen, plus 2 ms" is a violation. Example: ctrl normally wakes every 10 ms, so the limit is about 22 ms; a 100 ms oversleep gives margin about 4.5.

**`debounce(c, streak)`**:

```c
for (i...) {
    if (c[i].ft == FT_NONE || c[i].hard) {
        streak[i] = c[i].ft == FT_NONE ? 0U : streak[i];
        continue;
    }
    if (streak[i] < 0xFFU) streak[i]++;
    if (streak[i] < 2U && c[i].margin < 3.0f) c[i].ft = FT_NONE;
}
```
1-4. No candidate: reset the streak. Hard evidence: leave it, never debounced.
5. Timing candidate: one more window in a row.
6. If this is only the first window and the margin is below 3, drop it for now. So a timing fault must last 2 windows (200 ms) unless it is huge (3× the limit), which is too big to be jitter.

**`finish(c, out, stat, stat_thr)`** picks the winner and computes confidence:

```c
for (i...) {
    float s = 0.0f;
    if (c[i].ft != FT_NONE) {
        s = (c[i].hard ? 1000.0f : 0.0f) + c[i].margin;
        survivors++;
        if (best < 0 || s > best_score) { best = i; best_score = s; }
    }
    out->score[i] = (uint16_t)min(s * 100, 65535);
    out->rules_hit |= c[i].rules;
}
if (best < 0) return;
out->comp_id = best; out->fault_type = c[best].ft;
if (w->hard)      conf = (ft == FT_SEQUENCE || ft == FT_COMM) ? 90 : 95;
else if (stat)    { S = margin * T; conf = 100 * S / (S + T); }
else              conf = 55 + 15 * log2f(margin);
conf -= 10 * (survivors - 1);
clamp conf to 30..99;
```
- Score = 1000 for hard evidence (so it always beats timing) + margin. Highest score wins.
- Confidence: hard evidence 95 (90 for sequence and comm, which have more benign explanations). Rule timing: 55 at margin 1, +15 per doubling (margin 2 → 70, 4 → 85, 8 → 100 → capped 99). Statistical: S/(S+T), so 50 % at the threshold, 75 % at 3× the threshold.
- Each other surviving suspect costs 10 points ("someone else might be to blame").

**`rule_localize`**:

```c
memset(c, 0, sizeof(c)); memset(out, 0, sizeof(*out)); out->comp_id = FM_COMP_NONE;
bool hard = hard_evidence(v, bl, c);
timing_rules(v, bl, c);
debounce(c, s_streak_rule);
if (hard) {
    for (i...) if (!c[i].hard) c[i].ft = FT_NONE;
} else {
    int aggressor_prio = -1;
    for (i...) if (c[i].ft == FT_EXEC_OVERRUN || c[i].ft == FT_ISR_RATE)
                   if (s_ctx.priority[i] > aggressor_prio) aggressor_prio = s_ctx.priority[i];
    if (aggressor_prio >= 0)
        for (i...) if ((c[i].ft == FT_LIVENESS || c[i].ft == FT_STARVED) && s_ctx.priority[i] < aggressor_prio)
                       c[i].ft = FT_NONE;
}
finish(c, out, false, 0.0f);
```
1. Start clean; "no fault" is the default answer.
2-4. Gather hard evidence, timing candidates, debounce.
5-6. **Step 1 of the causal reasoning:** if there is hard evidence anywhere, timing anomalies are consequences (a broken ADC makes ctrl wait; ctrl is the victim, not the cause), so drop every non-hard candidate.
7-12. **Victim suppression:** otherwise, find the highest-priority component that is hogging the CPU (overrun or ISR storm). Any lower-priority component that went silent is its victim (it could not run), so its liveness candidate is dropped. ISRs have priority 0xFF, above every task.
13. Pick the winner.

Example: stuck loop in ctrl (priority 32). ctrl gets EXEC_OVERRUN; report (24) and shell (8) go silent and get LIVENESS; both are below 32 and are suppressed. Verdict: ctrl, EXEC_OVERRUN, one survivor, confidence 99.

`fm_localizer_rule = { "rule", rule_localize }`.

**Statistical localizer.**

```c
static float zsq(float x, const fm_stat_t *s)
{
    float sd = fmaxf(s->sigma, fmaxf(0.05f * fabsf(s->mean), 1.0f));
    float z  = (x - s->mean) / sd;
    clamp z to -100..100;
    return z * z;
}
```
A z-score is "how many standard deviations from the mean". The standard deviation is floored at 5 % of the mean (and at 1) so a feature that never varied while learning cannot produce an infinite score. Clamped and squared.

```c
const float T = 2.0f * (2.0f * k) * (2.0f * k);   /* 128 at k = 4 */
if (!hard_evidence(v, bl, c)) {
    for each task/ISR i:
        zr = zsq(runs); zc = zsq(cpu); zs = zsq(slice);
        zg = 0;
        if (periodic && max_gap_seen) {
            g = max_gap / max_gap_seen;
            if (g > 1) { zg = (g - 1) * 2k; zg *= zg; }
        }
        S = zr + zc + zs + zg;
        if (S > T) {
            ft = (zg is the largest) ? FT_LIVENESS
               : (ISR) ? FT_ISR_RATE
               : (cpu >= mean) ? FT_EXEC_OVERRUN : FT_STARVED;
            offer(&c[i], ft, false, S / T, FM_RULE_STAT);
        }
}
debounce(c, s_streak_stat);
finish(c, out, true, T);
```
- Score S = sum of squared z-scores of runs, CPU and slice, plus a gap term (how many times longer than the longest learned gap, scaled). This is a "diagonal Mahalanobis distance": distance from normal, measured in standard deviations, treating features as independent.
- Threshold T = 2 × (2k)², 128 at k = 4: roughly "two features each 8 sigma away".
- The fault type is a guess from which term dominates. It has no victim suppression and no priority knowledge: that is the point of the comparison.
- Hard evidence still short-circuits it, and the same debounce applies.

### Design choices

- **Hard evidence first.** A changed register or a never-seen transition is a fact; timing is a symptom. Without this order, a corrupted ADC would be blamed on the ctrl task that waits for it.
- **Margins instead of yes/no.** Lets the winner be "the most abnormal" and gives a natural confidence.
- **Debounce of 2 windows, bypassed at 3× margin**: fewer false alarms from a single jittery window, but big faults are still caught in one window (100 ms).
- **Two localizers behind one interface**: the monitor runs both every window and logs the other one in "shadow mode", so they can be compared on identical data.
- Alternative considered: a trained classifier on the chip. That needs labelled fault data for every application; the statistical localizer needs only the normal baseline. `fm_ml.py` explores the trained approach offline.

### How to test it alone

On a PC: build a baseline where component 0 has cpu mean 100 000, sigma 1000, max 105 000; feed a window with cpu 8 000 000 twice: the rule localizer returns comp 0, `FT_EXEC_OVERRUN`, confidence 99 on the first window already (margin > 3).

---

## fm_blackbox.h / fm_blackbox.c

| | |
|---|---|
| **Path** | `Core/Src/detect/fm_blackbox.h`, `fm_blackbox.c` |
| **Owner** | Arfan |
| **What it is** | Non-volatile storage. Sector 7 of flash is an append-only log of fault reports (512 slots of 256 bytes); sector 6 holds the learned baseline. Both survive reset and power-off, and a CubeIDE re-flash (which only erases the sectors it programs). |
| **Why it matters** | "Where and how are faults stored?": this is the answer. |
| **What would break without it** | Reports would vanish at the next reset; the baseline would have to be re-learned at every boot (10 s blind each time). |

### Walkthrough

`FM_LOG_SLOTS = 128 KB / 256 = 512`. Functions: init (scan once at boot), count, next sequence number, append, read slot *i*, clear, load/save/erase baseline.

**State**: `s_next_slot` (first free slot), `s_count` (valid reports), `s_next_seq`.

**`slot_ptr(i)`** = `0x08060000 + i × 256`. Flash is memory-mapped, so reading a report is just reading memory.

**`report_valid(r)`**: magic is "FMRP" and the CRC of everything before the `crc32` field (computed with `offsetof`, the byte offset of that field = 160) matches.

**`fm_bb_init()`**: start with "full" (512), count 0, next seq 1. Walk the slots: the first one whose magic reads 0xFFFFFFFF (erased) is where the log ends. For every valid one, count it and track the highest `seq`, so numbering continues after a reboot.

**`erase_sector(sector)`**:

```c
FLASH_EraseInitTypeDef e = { .TypeErase = FLASH_TYPEERASE_SECTORS, .Sector = sector,
                             .NbSectors = 1, .VoltageRange = FLASH_VOLTAGE_RANGE_3 };
HAL_FLASH_Unlock();
__HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                       FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);
HAL_StatusTypeDef st = HAL_FLASHEx_Erase(&e, &bad);
HAL_FLASH_Lock();
```
1. Erase one whole sector. Voltage range 3 (2.7 to 3.6 V, the Nucleo runs at 3.3 V) lets the flash erase and program 32 bits at a time. 2. Unlock the flash control register (write the two key values, done by HAL). 3. Clear stale error flags from earlier operations, or the next operation would fail. 4. Erase (sets every byte to 0xFF; a 128 KB sector takes on the order of a second, during which the CPU cannot fetch code from flash and simply stalls). 5. Lock again so a stray write cannot modify flash.

**`program_words(addr, data, len)`**: unlock, clear flags, then for each 4-byte chunk copy it into a word (padding the last partial word with 0xFF), `HAL_FLASH_Program(WORD, addr + off, w)`, stop on the first error; lock. Flash can only change bits from 1 to 0, which is why the target must be erased first.

**`fm_bb_append(r, &erased)`**:

```c
if (s_next_slot >= FM_LOG_SLOTS) {
    if (!erase_sector(FM_FLASH_LOG_SECTOR)) return false;
    s_next_slot = 0; s_count = 0;
    if (erased) *erased = true;
}
r->magic = FM_REPORT_MAGIC;
r->seq   = s_next_seq;
r->crc32 = fm_crc32(0, r, offsetof(fault_report_t, crc32));
bool ok = program_words(FM_FLASH_LOG_ADDR + s_next_slot * FM_LOG_SLOT_SIZE, r, sizeof(*r));
s_next_slot++;
if (ok) { s_count++; s_next_seq++; }
return ok;
```
1-5. Log full: erase the sector and start over (tell the caller, so the monitor can ignore the windows disturbed by the long stall).
6-8. Stamp magic, sequence number and CRC.
9. Write the 164 bytes (41 words).
10. Move on even if the write failed: a half-written slot is no longer erased, so it cannot be reused.
11. Only a good write counts.

**`fm_bb_read(i, out)`**: only slots below `s_next_slot` that pass `report_valid` are copied out.

**`fm_bb_clear()`**: erase sector 7, reset slot and count, **keep** `s_next_seq` (sequence numbers never go backwards, so old printouts are never confused with new ones).

**`fm_bb_load_baseline(out, core_hz)`**: the baseline in flash is used only if magic is "FMBL", version is 2, it was learned at the current clock, and the CRC matches. Then it is copied to RAM.

**`fm_bb_save_baseline(bl)`**: compute the CRC, erase sector 6, program the 796 bytes. **`fm_bb_erase_baseline`** erases sector 6 (not called by the current firmware).

### Design choices

- **Append-only slots with a CRC** instead of a file system: simple, power-cut safe (a torn write fails the CRC), and wears the flash evenly (one erase per 512 reports).
- **Erase only at moments the monitor chooses** (learn commit, `C`, log full) because an erase stalls the CPU.
- **Clock in the baseline**: cycle counts at 84 MHz are meaningless at 168 MHz; refusing is safer than misjudging.

### How to test it alone

`C` then `I 1 0` three times, `R` lists seq n, n+1, n+2; power-cycle; `R` lists them again.

---

## fm_recover.h / fm_recover.c

| | |
|---|---|
| **Path** | `Core/Src/detect/fm_recover.h`, `fm_recover.c` |
| **Owner** | Arfan |
| **What it is** | The recovery ladder. Level 0: the component's own fix (restart the task, re-initialise the peripheral). Level 1: the same plus re-initialise every peripheral. Level 2: system reset. The level goes up when the **same** component needs recovery again within 10 s. |
| **Why it matters** | "Recover" in the problem statement. |
| **What would break without it** | Faults would be reported but the system would stay broken. |

### Walkthrough

```c
#define FM_ESCALATE_MS   10000U
static uint32_t s_last_ms[FM_MAX_COMP];
static uint8_t  s_level[FM_MAX_COMP];
static uint8_t  s_used[FM_MAX_COMP];
```
Per component: when it was last recovered, its current ladder level, and whether it was ever recovered.

**`fm_recover_plan(comp, ft, now_ms)`** chooses, does not act:

```c
if (comp >= FM_MAX_COMP) return ACT_SYSTEM_RESET;
if (ft == FT_MEM_ACCESS || ft == FT_WATCHDOG) return ACT_LOG_ONLY;
if (s_used[comp] && (now_ms - s_last_ms[comp]) < FM_ESCALATE_MS) {
    if (s_level[comp] < 2U) s_level[comp]++;
} else {
    s_level[comp] = 0;
}
s_used[comp] = 1; s_last_ms[comp] = now_ms;
const fm_comp_info_t *c = &g_fm_comp[comp];
if (s_level[comp] >= 2U || c->recover == NULL) return ACT_SYSTEM_RESET;
return (c->kind == FM_KIND_TASK) ? ACT_RESTART_TASK : ACT_REINIT_PERIPH;
```
1. Unknown culprit: only a reset is safe.
2. Faults that already caused a reset (MPU trap, watchdog) are just logged: the reset was the recovery.
3-7. Again within 10 s: one level up (max 2). Otherwise back to level 0.
8. Remember this recovery.
9-10. Level 2, or no recovery function registered: reset.
11. Otherwise restart a task, or re-init a peripheral/ISR source.

**`fm_recover_execute(comp, act)`**:

```c
if (act == ACT_SYSTEM_RESET) {
    g_fm_noinit.planned_reset = 1U;
    g_fm_noinit.suspect_comp  = comp;
    __DSB();
    NVIC_SystemReset();
}
if (act != ACT_RESTART_TASK && act != ACT_REINIT_PERIPH) return;
fm_trace_event(comp, EVT_RECOVERY, (uint16_t)act);
if (g_fm_comp[comp].kind == FM_KIND_TASK) fm_mpu_unguard_task(comp);
g_fm_comp[comp].recover(comp);
if (s_level[comp] == 1U) {
    for (i...) if (i != comp && kind == PERIPH && recover) g_fm_comp[i].recover(i);
}
```
1-6. Reset: note in `.noinit` that it was planned and who was suspected (the next boot prints `!RECOVERED by system reset`), then reset.
7. Nothing else to do for LOG_ONLY.
8. Log the recovery event.
9. Before a task is deleted, remove its MPU guard (its stack returns to the heap).
10. Call the application's own recovery function.
11-13. Level 1: also re-initialise every other peripheral.

### Design choices

An escalating ladder is the standard pattern: try the cheapest, least disruptive fix first, and only reset the whole system when cheap fixes do not hold. The recovery *functions* belong to the application, the *policy* to the framework.

### How to test it alone

`I 1 0` three times within 10 s: the `!RECOVERED ... level=` line shows 0, then 1, then the board resets with `!RECOVERED by system reset (escalation for ctrl)`.

---

## fm_watchdog.h / fm_watchdog.c

| | |
|---|---|
| **Path** | `Core/Src/detect/fm_watchdog.h`, `fm_watchdog.c` |
| **Owner** | Arfan |
| **What it is** | Supervision with the IWDG (independent watchdog), a hardware down-counter running from its own 32 kHz LSI oscillator. If it is not reloaded within 4 s it resets the chip, even if the CPU is stuck with interrupts off. A small top-priority task reloads it every 250 ms, but **only** if the monitor task has checked in within the last 1.5 s. |
| **Why it matters** | "Watchdog events": the one failure software cannot catch by itself (everything frozen) is caught by hardware, and reported after the reboot. |
| **What would break without it** | A fault with interrupts disabled (or a stuck high-priority ISR) would freeze the board forever with no report. |

### Walkthrough

Constants: `FM_WDG_TIMEOUT_MS 4000`, `FM_WDG_FEED_MS 250`, `FM_WDG_STALE_MS 1500`.

**`iwdg_start()`** (register level, no HAL driver):

```c
DBGMCU->APB1FZ |= DBGMCU_APB1_FZ_DBG_IWDG_STOP;
IWDG->KR  = IWDG_KEY_ENABLE;
IWDG->KR  = IWDG_KEY_ACCESS;
IWDG->PR  = 4U;
IWDG->RLR = (FM_WDG_TIMEOUT_MS * 500U) / 1000U;
while (IWDG->SR != 0U) { }
IWDG->KR  = IWDG_KEY_RELOAD;
```
1. DBGMCU_APB1_FZ bit DBG_IWDG_STOP: freeze the watchdog while the debugger halts the core, otherwise a breakpoint longer than 4 s would reset the board.
2. Key register KR = 0xCCCC: start the IWDG (this also starts the LSI oscillator). Once started it cannot be stopped except by a reset.
3. KR = 0x5555: unlock the prescaler and reload registers.
4. PR = 4: divide by 64. 32 kHz / 64 = 500 Hz, one count every 2 ms.
5. RLR (reload value) = 4000 × 500 / 1000 = 2000 counts × 2 ms = 4 s. (RLR is 12 bits, max 4095.)
6. Wait until the status register shows the prescaler/reload updates have reached the slow LSI clock domain (PVU and RVU bits back to 0).
7. KR = 0xAAAA: reload the counter now.

**`feeder_task_fn`**:

```c
for (;;) {
    osDelay(FM_WDG_FEED_MS);
    if ((osKernelGetTickCount() - s_last_checkin) < FM_WDG_STALE_MS) {
        IWDG->KR = IWDG_KEY_RELOAD;
        fm_trace_event(fm_current_comp(), EVT_CHECKIN, 0);
    }
}
```
Every 250 ms: if the monitor checked in less than 1.5 s ago, reload the watchdog and log a CHECKIN event. If the monitor has stopped (or this task cannot run), reloading stops and 4 s later the chip resets.

**`fm_watchdog_checkin()`**: store the current tick. Called by the monitor every window and around long flash operations.

**`fm_watchdog_init(comp)`**: create the "wdg" task (192 words, `osPriorityRealtime` = 48, the highest), register it with no recovery function, start the IWDG.

### Design choices

- **Feed from a dedicated task, gated by the monitor's check-in**, so the watchdog answers "is the fault monitor alive?", not merely "is any code running?". The alternative, feeding from the tick interrupt, would keep feeding even when every task is dead.
- **IWDG rather than WWDG**: the window watchdog runs from the main clock and has a much shorter maximum timeout; the IWDG keeps working even if the main clock fails.

### How to test it alone

`I 9 0` (interrupts off + spin in ctrl): about 4 s of silence, then the board reboots and prints `BOOT ... reset cause: IWDG watchdog` and `!WATCHDOG reset ... last running: ctrl`.

---
## fm_monitor.h / fm_monitor.c

| | |
|---|---|
| **Path** | `Core/Src/detect/fm_monitor.h`, `fm_monitor.c` |
| **Owner** | Arfan |
| **What it is** | The monitor task: the heartbeat of the framework. Every 100 ms it closes the capture window, adds stack figures, checks in with the watchdog, and then either learns (feeds the learner) or monitors (runs both localizers). On a fault it builds the report, stores it, prints the alert with its history, and runs the recovery ladder. At boot it turns whatever survived a reset (crash record, watchdog reset) into a stored report. It also owns the shell commands `L B R C M K`. |
| **Why it matters** | It is where all the parts meet: capture → signature → detect → localize → report → store → recover. |
| **What would break without it** | Everything after capture. |

### Walkthrough: fm_monitor.h

Constants: `FM_LEARN_WINDOWS_DEFAULT 100` (10 s), `FM_WARMUP_WINDOWS 10` (1 s ignored after boot, while things settle), `FM_GRACE_WINDOWS 3` (300 ms ignored after a recovery or flash erase). Modes: `WARMUP`, `LEARN`, `MONITOR`. `fm_monitor_init(self_comp_id)` creates the task. The header comment states a design rule: **the monitor runs at `osPriorityHigh` (40), above every application task**, because a monitor below a stuck high-priority task would never run again.

### Walkthrough: fm_monitor.c

The whole file is inside `#if FM_ENABLE_MONITOR && FM_ENABLE_DETECT`.

**State** (all `static`, far too big for a task stack): `s_pub` (the window being published), `s_bl` (baseline in RAM), `s_learn` (learner), `s_rep` (report being built), `s_have_bl`; the mode, warm-up and grace counters, learn target, selected localizer (0 rule, 1 statistical), the table `s_loc[2]` of both localizers; shell requests `s_req_learn` / `s_req_clear` (executed by the monitor at a window boundary, so a flash erase never surprises the detector); ground-truth tracking; fault and recovery counters; and `s_self`, the monitor's own component id.

**Helpers.** `cyc_per_us()` = 84. `comp_name(c)` gives the registered name, "other" or "none". `evt_name(t)` names event types. `fm_monitor_ram_bytes()` adds up the framework's static RAM: ring buffer 8204 + crash record 176 + registry + stage table + `s_pub` 516 + baseline 796 + learner 940 + report 164 + monitor stack 4096 bytes (by our count about 15.4 KB).

**`configure_detector()`** copies kind, priority and `is_comm` of every component from the registry into the detector context, then:

```c
ctx->kind[s_self] = FM_KIND_UNUSED;
```
The monitor does not judge itself: if it stalls, the watchdog notices ("an observer cannot observe its own absence"). Then it copies the stage owners and sets cycles per ms from the real clock.

**Printing.** `print_history(h, n, ref_ts)` prints each non-empty event as `!HIST <µs before ref> <component> <event or stage name> <extra>`; the time is `-(ref - event_ts) / 84`. `print_report(r, latency, history)` prints the one-line `!FAULT seq=... type=... comp=name(id) conf=..% loc=rule|stat action=... rules=0x.... pc=0x........ uptime=...ms latency_ms=...` and optionally the history.

**`store(r)`**: check in with the watchdog, append to the black box (warn if it fails), check in again; if the log sector had to be erased, start a grace period.

**`boot_report()`**: runs once at task start.

```c
uint16_t cause = fm_reset_cause();
fm_uart_printf("BOOT #%lu reset cause: %s (0x%02x)\r\n", ...);
memset(&s_rep, 0, sizeof(s_rep));
s_rep.reset_cause = cause; s_rep.cycle_ts = DWT->CYCCNT; s_rep.uptime_ms = 0;
s_rep.action = ACT_SYSTEM_RESET;
if (g_fm_noinit.crash_valid) {
    s_rep.fault_type = g_fm_noinit.fault_type;
    s_rep.comp_id    = g_fm_noinit.comp_id;
    s_rep.confidence = (s_rep.fault_type == FT_STACK_OVERFLOW) ? 99U : 95U;
    s_rep.pc         = g_fm_noinit.pc;
    s_rep.rules_hit  = FM_RULE_TRAP;
    memcpy(s_rep.history, g_fm_noinit.history, sizeof(s_rep.history));
    store(&s_rep);
    /* print !TRAP line with trap kind, CFSR, HFSR, fault address, LR, then the report and history */
    g_fm_noinit.crash_valid = 0;
    reported = true;
} else if (cause & FM_RST_IWDG) {
    uint8_t c = g_fm_noinit.cur_comp;
    s_rep.fault_type = FT_WATCHDOG; s_rep.comp_id = c;
    s_rep.confidence = (c < FM_MAX_COMP) ? 80U : 40U;
    s_rep.rules_hit  = FM_RULE_WATCHDOG;
    memcpy(s_rep.history, g_fm_noinit.history, sizeof(s_rep.history));
    store(&s_rep);
    /* print !WATCHDOG line: last running component and last stage */
    reported = true;
} else if ((cause & FM_RST_SW) && g_fm_noinit.planned_reset) {
    fm_uart_printf("!RECOVERED by system reset (escalation for %s)\r\n", ...);
}
g_fm_noinit.planned_reset = 0;
```
1-2. Print the boot number and reset cause.
3-5. Prepare a report: action is "system reset" (that already happened).
6-15. **A trap left a crash record**: copy type, blamed component, PC and history from `.noinit`. Confidence 99 for a stack overflow (the MPU guard is unambiguous), 95 for other memory faults. Store, print, mark the record consumed.
16-23. **Watchdog reset, no crash record**: blame whoever was running at the last task switch (confidence 80, or 40 if it was "other"). Store and print.
24-26. **Our own planned reset**: just say so.
27. Clear the planned flag.
Then, if a report was filed and an injection was recorded before the reset, print the ground truth (`!GT injected fault ... before the reset`), clear the injection flag, and count the fault.

**`handle_fault(res)`**:

```c
uint8_t comp = res->comp_id;
fm_action_t act = fm_recover_plan(comp, res->fault_type, osKernelGetTickCount());
memset(&s_rep, 0, sizeof(s_rep));
s_rep.cycle_ts = DWT->CYCCNT; s_rep.uptime_ms = osKernelGetTickCount();
s_rep.fault_type = res->fault_type; s_rep.comp_id = comp; s_rep.confidence = res->confidence;
s_rep.action = (uint8_t)act; s_rep.rules_hit = res->rules_hit;
s_rep.reset_cause = fm_reset_cause(); s_rep.localizer = s_loc_sel;
if (g_fm_comp[comp].kind == FM_KIND_TASK) s_rep.pc = fm_task_pc(g_fm_comp[comp].handle);
fm_trace_event(comp, EVT_FAULT, res->fault_type);
rb_copy_last(&g_fm_rb, s_rep.history, FM_HISTORY_LEN);
store(&s_rep);
s_n_faults++;
/* latency = (detection cycle - injection cycle) / cycles per ms, if a label is active */
print_report(&s_rep, latency_ms, true);
/* !GT line: LOCALIZED CORRECTLY or DIFFERENT COMPONENT */
if (act == ACT_SYSTEM_RESET) { print "!RECOVERY level ..."; osDelay(20); }
fm_recover_execute(comp, act);
s_n_recoveries++;
fm_trace_restart_window(); fm_detect_reset();
s_grace = FM_GRACE_WINDOWS;
s_label_active = false; s_label_comp = FM_COMP_NONE;
fm_uart_printf("!RECOVERED comp=%s action=%s level=%u\r\n", ...);
```
1-2. Decide the recovery first (so the report records it).
3-7. Fill the report from the verdict.
8. For a task, read where it is right now (the stuck loop's address).
9. Log the detection in the trace, then...
10. ...copy the last 16 events (so the `FAULT` event is the newest one in the history).
11-12. Store in flash and count.
13-15. Print the alert, the history and the ground-truth comparison. The latency is measured on the chip, from the injection's cycle stamp to detection.
16. Before a reset, wait 20 ms so the UART can finish sending.
17. Execute the recovery (never returns for a reset).
18-21. Count it, start a fresh window, forget debounce history, ignore the next 3 windows, end the ground-truth label.
22. Print the result.

**Learning.** `start_learning(n)` resets the learner, sets the target and mode, and asks the user not to inject faults. `finish_learning()` turns the accumulators into `s_bl`, saves it to sector 6 (with watchdog check-ins around the erase), switches to MONITOR with a grace period, restarts the window and debounce, and prints `LEARN done ... saved to / NOT saved to flash sector 6`.

**`baseline_matches_components(bl)`**: a stored baseline only fits this firmware if it was learned with the same components. For every component except the monitor itself:

```c
fm_kind_t k = g_fm_comp[i].kind;
bool ran  = bl->comp[i].runs.mean > 0.0f;
bool hash = bl->periph_hash[i] != 0U;
if ((k == FM_KIND_TASK) != ran && (k == FM_KIND_TASK || k == FM_KIND_UNUSED)) return false;
if ((k == FM_KIND_PERIPH) != hash) return false;
```
1. What is registered at this index now. 2. Did it run while learning? 3. Does the baseline hold a register CRC for it?
4. A registered task that never ran while learning, or an unused slot that did run: the application changed, reject. (ISRs are not checked, because event-driven ones may legitimately not fire during learning.)
5. A peripheral without a stored CRC, or a CRC for something that is no longer a peripheral: reject.
Without this check, a changed application (or an optional part that is now missing) would cause false alarms forever.

**`track_ground_truth()`**: if the injector recorded a new injection (new timestamp), make it the active label; a label not detected within 100 windows (10 s) is dropped.

**`monitor_task_fn`**, the main loop:

```c
configure_detector();
fm_bb_init();
boot_report();
s_have_bl = fm_bb_load_baseline(&s_bl, SystemCoreClock);
if (s_have_bl && !baseline_matches_components(&s_bl)) { s_have_bl = false; /* print "re-learning" */ }
/* print "black box: N stored reports; baseline: loaded / none" */
s_warmup = FM_WARMUP_WINDOWS; s_mode = FM_MODE_WARMUP;
uint32_t next = osKernelGetTickCount();
fm_sig_close_window(&s_pub.v);
for (;;) {
    next += FM_WINDOW_MS;
    uint32_t now = osKernelGetTickCount();
    if ((int32_t)(next - now) <= 0) next = now + FM_WINDOW_MS;
    osDelayUntil(next);
    fm_sig_close_window(&s_pub.v);
    for each task i: s_pub.v.comp[i].stack_free_min = min(fm_stack_free_words(i), 0xFFFF);
    fm_watchdog_checkin();
    track_ground_truth();
    /* verdicts default to "none" */
    if (s_req_clear) { erase log, print OK, grace, restart window }
    if (s_req_learn) { start_learning(n); s_grace = 1; }
    if (s_grace > 0U) {
        s_grace--;
        if (s_grace == 0U) fm_trace_restart_window();
    } else switch (s_mode) {
    case FM_MODE_WARMUP:  count down; then MONITOR if a baseline exists, else start learning
    case FM_MODE_LEARN:   fm_learn_add(); when n_windows >= target: finish_learning()
    case FM_MODE_MONITOR:
        fm_localizer_rule.localize(&s_pub.v, &s_bl, &s_pub.rule);
        fm_localizer_stat.localize(&s_pub.v, &s_bl, &s_pub.stat);
        act = s_loc_sel ? &s_pub.stat : &s_pub.rule;
        g_fm_noinit.suspect_comp = act->comp_id;
        if (act->comp_id != FM_COMP_NONE) {
            publish the window; print "!SHADOW <other> says: ...";
            handle_fault(act);
            continue;
        }
    }
    /* fill label and mode, fm_sigout_publish(&s_pub) */
}
```
1-3. Load the registry into the detector, scan the black box, file any boot report.
4-6. Load the baseline (if valid for this clock **and** learned with the same components), start in warm-up.
7-8. Remember the start tick, discard the partial boot window.
9-13. **Timing:** the next deadline is exactly 100 ms after the previous one. If we fell behind (a flash erase stalled the CPU for a second), resync instead of running many windows back-to-back. `osDelayUntil` sleeps until that absolute tick.
14. Close the window → one `sig_vector_t`.
15. Fill the free-stack column for every task.
16. Tell the watchdog "monitor alive".
17. Update the ground-truth label.
18-19. Shell requests are executed here, at a window boundary.
20-22. Grace period: do nothing but count down; at the end, start a clean window.
23-24. Warm-up: wait 1 s after boot, then monitor (baseline loaded) or learn (no baseline).
25. Learning: add the window; after the target count, save the baseline and start monitoring.
26-33. Monitoring: run **both** localizers on the same window. The selected one decides; the other is printed as `!SHADOW` for comparison. On a fault, publish this window, handle the fault and skip the normal publish (`continue`).
34. Publish the window for `S`/`V`.

**Shell commands.**
- `L [n]` (`cmd_learn`): n default 100, minimum 20; sets `s_req_learn`.
- `B` (`cmd_baseline`): prints windows, clock, CRC; per component mean runs, sigma, mean CPU µs, longest slice µs, longest gap ms, lowest stack, periodic/event-driven; peripherals' CRC; and the allowed transitions as letters (`A>B B>C C>D D>A E>E`). Floats are printed with one decimal by `print_x10` (multiply by 10, round, print integer part and remainder) because `printf("%f")` is not enabled in newlib-nano by default.
- `R [seq]` (`cmd_reports`): a CSV header, one `R,...` line per valid stored report; with a sequence number, only that report plus its history. For trap and watchdog reports, the history times are shown relative to the last history event (the detection timestamp belongs to the next boot).
- `C` (`cmd_clear`): sets `s_req_clear`.
- `M 0|1` (`cmd_mode`): selects the localizer and resets debounce.
- `K <sigma>` (`cmd_sigma`): sets `k_sigma` if 1 ≤ k ≤ 20 and prints it.
- `fm_monitor_print_status()` (used by `S`): mode (with learning progress), localizer, faults, recoveries, stored reports, boot number.

**`fm_monitor_init(self)`**: create the "monitor" task (1024 words = 4 KB stack, `osPriorityHigh`), register it as a task with no recovery function, register the six shell letters.

### Design choices

- **Fixed 100 ms windows driven by `osDelayUntil`** rather than "process events as they arrive": constant, predictable CPU cost, and every feature is per-window so learning and detection compare like with like.
- **Shell requests deferred to the window boundary**: the shell task runs at low priority and could be pre-empted halfway through a flash erase; the monitor does it at a moment it controls and grants a grace period.
- **Both localizers run every window** (shadow mode): the comparison costs one extra function call and gives perfectly paired data.
- **Warm-up and grace windows**: the first second after boot and the windows around a recovery are not "normal" and would cause false alarms.

### How to test it alone

Fresh board: watch `LEARN start`, then after 10 s `LEARN done`, then `S` shows `monitor: monitoring`. With `FM_ENABLE_DETECT=0` this file compiles to nothing and `fm_window_task.c` takes over.

---
# Part 4 · Abhishek: application, UART, injection, tools

Folders `Core/Src/app`, `Core/Src/inject` and `tools/`. The application is a real closed-loop **temperature controller** whose "plant" (a heater in a room) is simulated inside the chip and closed through a real analog round trip on pin PA4. The injection module creates real faults and records the ground truth; the tools run the evaluation from a PC.

## app/fm_common.h

| | |
|---|---|
| **Path** | `Core/Src/app/fm_common.h` |
| **Owner** | Abhishek |
| **What it is** | The application's dictionary: what component numbers 0..12 and stage numbers 0..4 mean for **this** application, and the list of injectable faults (the ground truth). |
| **Why it matters** | It is the only place where the application's structure meets the framework's numbers. Porting to a new application means rewriting this file, not the framework. |
| **What would break without it** | Every app file, the shell's `I` command and the Python tools' component numbers. |

### Walkthrough

**Components** (`app_comp_id_t`): tasks `COMP_CTRL 0`, `COMP_REPORT 1`, `COMP_SHELL 2`, `COMP_MONITOR 3`, `COMP_WATCHDOG 4`; interrupts `COMP_ISR_ADC 5` (DMA2 Stream0, 100 Hz), `COMP_ISR_TIM6 6` (1 kHz tick), `COMP_ISR_USART2 7`, `COMP_ISR_BUTTON 8`; peripherals `COMP_PER_ADC 9` (ADC1 + its DMA), `COMP_PER_PWM 10` (TIM2), `COMP_PER_DAC 11`, `COMP_PER_UART 12`; `COMP_COUNT 13`. `_Static_assert(COMP_COUNT <= FM_MAX_COMP)` fails the build if we ever exceed 16.

**Stages** (`app_stage_t`): `A_ACQUIRE 0`, `B_FILTER 1`, `C_CONTROL 2`, `D_ACTUATE 3` (all in the ctrl task), `E_REPORT 4` (report task).

**Faults** (`fault_id_t`), with what the monitor should conclude:

| id | name | what it does | expected verdict |
|---|---|---|---|
| 1 | STUCK_LOOP | task spins forever | EXEC_OVERRUN, target task |
| 2 | STACK_OVERFLOW | infinite recursion into the MPU guard | STACK_OVERFLOW, target task |
| 3 | MISSED_DEADLINE | task oversleeps 10 periods | LIVENESS, target task |
| 4 | ISR_STORM | TIM6 rate × 10 plus extra work | ISR_RATE, tim6_isr |
| 5 | MEM_RO_WRITE | write to the MPU read-only config | MEM_ACCESS, target task |
| 6 | NULL_DEREF | write through address 0x10 | MEM_ACCESS, target task |
| 7 | PERIPH_CORRUPT | scramble a config register | PERIPH, target peripheral |
| 8 | UART_CORRUPT | inject a command frame with a bad checksum | COMM, USART2 |
| 9 | WDG_HANG | interrupts off + spin | WATCHDOG, target task |
| 10 | SEQ_REPEAT | stage C runs 3 times (A,B,C,C,C) | SEQUENCE, ctrl |

`inject_log_t` (packed, 8 bytes): cycle stamp, component, fault, requested delay (0 on the "fired" record).

### Design choices

Separate enums for "what we injected" (`fault_id_t`) and "what the detector concluded" (`fm_fault_type_t`): the detector must never know what was injected, and the evaluation maps one onto the other.

### How to test it alone

It is only definitions; it is tested by compiling (the static assert).

---

## app/app_config.h

| | |
|---|---|
| **Path** | `Core/Src/app/app_config.h` |
| **Owner** | Abhishek |
| **What it is** | All application knobs in one place: UART settings, report period, task priorities and stack sizes. |
| **Why it matters** | Changing any of these changes what "normal" looks like, so the comment reminds you to re-learn (`L`). |
| **What would break without it** | The app files would not compile. |

### Walkthrough

- UART: `FM_UART_HANDLE huart2`, 115200 baud, 96-byte RX queue, 256-byte TX line buffer, 500 ms TX timeout. USART2 on PA2/PA3 is wired to the ST-LINK virtual COM port, the same USB cable used for flashing.
- `PERIOD_REPORT_MS 100`. The ctrl task has no period of its own: the ADC (100 Hz) wakes it.
- Priorities: ctrl `osPriorityAboveNormal` (32), report `osPriorityNormal` (24), shell `osPriorityLow` (8). (wdg 48 and monitor 40 are set in their own files.)
- Stacks in words: ctrl 384 (1.5 KB), report 512 (2 KB), shell 512 (2 KB).

### Design choices / test

A single config header avoids magic numbers scattered across files. Test: change `PERIOD_REPORT_MS`, rebuild, and `B` after `L` shows the new runs per window for report.

---

## app/app_periph.h / app_periph.c

| | |
|---|---|
| **Path** | `Core/Src/app/app_periph.h`, `app_periph.c` |
| **Owner** | Abhishek |
| **What it is** | The application's hardware, programmed register by register (no HAL drivers for ADC/DAC in this project): TIM2 makes a 100 Hz PWM on PA5 (LED LD2 brightness = heater power) and triggers the ADC; ADC1 converts PA4 and the internal reference VREFINT; DMA2 Stream0 copies both results to RAM and interrupts; DAC1 drives PA4. Also: re-init functions (recovery actions), register-check tables (what the framework checksums), and the TIM6 1 kHz tick with its "ISR storm" mode. |
| **Why it matters** | Provides the "peripheral status" component checks, the ISR the monitor times, and the real analog loop that faults disturb. |
| **What would break without it** | No sensor, no actuator, nothing to control or monitor. |

### Walkthrough: app_periph.h

```
TIM2 (100 Hz) --TRGO--> ADC1 scan [PA4, VREFINT] --DMA2 S0--> RAM
   +-- CH1 PWM -> PA5 (LED LD2)
DAC1 OUT1 -> PA4  <---- plant model (control task) <-- PID
```
PA4 is both DAC_OUT1 and ADC1_IN4, so the ADC measures the real voltage the DAC puts on the pin.

Constants: `APP_ADC_HZ 100`, `APP_PWM_TOP 1000` (duty resolution), `APP_VREFINT_LO/HI 1200/1800` (plausible raw readings of the 1.21 V internal reference at 3.3 V: about 1500). Error flags for `fm_periph_error`: `ADC_OVR 0x1`, `DMA_TE 0x2`, `VREF 0x4`, `TIMEOUT 0x8`. `app_samples_t`: latest PA4 raw value, VREFINT raw value, scan counter.

### Walkthrough: app_periph.c

**State**: `s_dma_buf[2]` (DMA target: [0] PA4, [1] VREFINT), `s_samples`, `s_ctrl_task` (who to wake), `s_ms` (1 kHz counter), `s_storm`.

**`gpio_init()`**:

```c
RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
(void)RCC->AHB1ENR;
GPIOA->MODER |= (3U << (4U * 2U));
GPIOA->PUPDR &= ~(3U << (4U * 2U));
```
1. Turn on the clock of GPIO port A (a peripheral ignores writes while its clock is off).
2. Read back: a short delay that guarantees the clock is running before the next access.
3. MODER bits 9:8 (pin 4) = 11: analog mode, needed for both the DAC output and the ADC input.
4. PUPDR pin 4 = 00: no pull-up/down resistor (it would distort the analog voltage).

**`app_pwm_pin_init()`**: PA5 MODER bits 11:10 = 10 (alternate function), AFR[0] bits 23:20 = 0001 (AF1 = TIM2_CH1). Called by the ctrl task at start because `BSP_LED_Init()` in `main()` turns PA5 into a plain output.

**`tim2_config()`**:

```c
RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
uint32_t duty = TIM2->CCR1;
TIM2->CR1   = 0;
TIM2->PSC   = 839U;
TIM2->ARR   = APP_PWM_TOP - 1U;
TIM2->CCMR1 = (6U << TIM_CCMR1_OC1M_Pos) | TIM_CCMR1_OC1PE;
TIM2->CCER  = TIM_CCER_CC1E;
TIM2->CR2   = (2U << TIM_CR2_MMS_Pos);
TIM2->CCR1  = duty;
TIM2->EGR   = TIM_EGR_UG;
TIM2->CR1   = TIM_CR1_ARPE | TIM_CR1_CEN;
```
1. Clock on. 2. Keep the current duty (matters on re-init). 3. Stop the timer.
4. PSC = 839: the timer clock (84 MHz: APB1 runs at 42 MHz and timers on a divided APB get ×2) is divided by 840 → 100 kHz, one count every 10 µs.
5. ARR = 999: count 0..999 = 1000 counts → 100 Hz period (10 ms). Also gives 0..1000 duty steps.
6. CCMR1 OC1M (bits 6:4) = 110: PWM mode 1 (output high while counter < CCR1). OC1PE: duty changes take effect at the next period, never mid-pulse.
7. CCER CC1E: connect channel 1 to the pin.
8. CR2 MMS (bits 6:4) = 010: TRGO output = update event, i.e. one trigger pulse to the ADC at the start of every period.
9. Restore duty. 10. EGR UG: force an update so PSC/ARR load now. 11. CR1: ARPE (buffered ARR) and CEN (start counting).

**`dac_config()`**: clock on, `DAC->CR = DAC_CR_EN1`: channel 1 enabled, output buffer on (BOFF1 = 0), no trigger (TEN1 = 0), so a value written to DHR12R1 appears on the pin about one bus cycle later.

**`adc_dma_config()`**:

```c
RCC->AHB1ENR |= RCC_AHB1ENR_DMA2EN;  RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;
NVIC_DisableIRQ(DMA2_Stream0_IRQn);
ADC1->CR2 = 0;
DMA2_Stream0->CR &= ~DMA_SxCR_EN;  while (DMA2_Stream0->CR & DMA_SxCR_EN) { }
DMA2->LIFCR = 0x3DU;
DMA2_Stream0->PAR  = (uint32_t)&ADC1->DR;
DMA2_Stream0->M0AR = (uint32_t)s_dma_buf;
DMA2_Stream0->NDTR = 2U;
DMA2_Stream0->FCR  = 0;
DMA2_Stream0->CR   = (0U << DMA_SxCR_CHSEL_Pos) | (2U << DMA_SxCR_PL_Pos) |
                     (1U << DMA_SxCR_MSIZE_Pos) | (1U << DMA_SxCR_PSIZE_Pos) |
                     DMA_SxCR_MINC | DMA_SxCR_CIRC | DMA_SxCR_TCIE | DMA_SxCR_TEIE;
DMA2_Stream0->CR  |= DMA_SxCR_EN;
ADC->CCR    = (1U << ADC_CCR_ADCPRE_Pos) | ADC_CCR_TSVREFE;
ADC1->SR    = 0;
ADC1->CR1   = ADC_CR1_SCAN;
ADC1->SMPR2 = (4U << ADC_SMPR2_SMP4_Pos);
ADC1->SMPR1 = (7U << ADC_SMPR1_SMP17_Pos);
ADC1->SQR1  = (1U << ADC_SQR1_L_Pos);
ADC1->SQR3  = (4U << ADC_SQR3_SQ1_Pos) | (17U << ADC_SQR3_SQ2_Pos);
ADC1->CR2   = ADC_CR2_ADON | ADC_CR2_DMA | ADC_CR2_DDS |
              (1U << ADC_CR2_EXTEN_Pos) | (6U << ADC_CR2_EXTSEL_Pos);
NVIC_SetPriority(DMA2_Stream0_IRQn, 6U);
NVIC_EnableIRQ(DMA2_Stream0_IRQn);
```
1. Clocks for DMA2 and ADC1. 2. Keep the DMA interrupt quiet while reconfiguring. 3. ADC off.
4. Disable the DMA stream and wait until the hardware confirms it is off (a stream can only be configured while disabled).
5. LIFCR = 0x3D: clear all stream-0 flags (bits 0 FEIF, 2 DMEIF, 3 TEIF, 4 HTIF, 5 TCIF).
6. Source = the ADC data register. 7. Destination = our 2-element buffer. 8. 2 transfers per cycle. 9. FCR = 0: direct mode (no FIFO).
10-12. CR: CHSEL = 0 (channel 0 of stream 0 is ADC1), PL = 10 (high priority), MSIZE = PSIZE = 01 (16-bit half-words), MINC (memory address increments: [0] then [1]), CIRC (circular: restart at [0] after 2 transfers, forever), TCIE (interrupt on transfer complete), TEIE (interrupt on transfer error). Direction bits 0 = peripheral to memory.
13. Enable the stream.
14. ADC common CCR: ADCPRE = 01 → ADC clock = PCLK2 (84 MHz) / 4 = 21 MHz (the ADC's maximum is 36 MHz). TSVREFE: switch on the internal reference (channel 17).
15. Clear status. 16. CR1 SCAN: convert a list of channels, not just one.
17. SMPR2 SMP4 = 100: sample PA4 for 84 ADC cycles. 18. SMPR1 SMP17 = 111: 480 cycles for VREFINT (about 23 µs at 21 MHz; the datasheet asks for at least 10 µs).
19. SQR1 L = 1: the list has 2 conversions. 20. SQR3: first channel 4 (PA4), second channel 17 (VREFINT).
21-22. CR2: ADON (ADC on), DMA (use DMA), DDS (keep issuing DMA requests after the first round, required for circular DMA), EXTEN = 01 (start on the rising edge of an external trigger), EXTSEL = 0110 (that trigger is TIM2 TRGO).
23-24. DMA interrupt priority 6 (numerically ≥ 5, so it may call FreeRTOS "FromISR" functions), enable.

A full scan takes about (84 + 12 + 480 + 12) / 21 MHz ≈ 28 µs, once every 10 ms.

**`app_periph_init()`**: GPIO, DAC, ADC+DMA, then TIM2 last (so the first trigger finds the ADC ready).

**Register-check tables.**

```c
static const fm_reg_check_t s_adc_regs[] = {
    { &ADC1->CR1, 0xFFFFFFFF }, { &ADC1->CR2, ~(ADC_CR2_SWSTART | ADC_CR2_JSWSTART) },
    { &ADC1->SMPR1, ~0 }, { &ADC1->SMPR2, ~0 }, { &ADC1->SQR1, ~0 }, { &ADC1->SQR3, ~0 },
    { &ADC->CCR, ~0 }, { &DMA2_Stream0->CR, ~DMA_SxCR_CT },
    { &DMA2_Stream0->PAR, ~0 }, { &DMA2_Stream0->M0AR, ~0 },
};
```
Everything that defines how the ADC and its DMA are set up. Masked out: the "start conversion" bits (they self-clear) and DMA CT ("current target", it toggles in double-buffer mode). TIM2: CR1, CR2, PSC, ARR, CCMR1, CCER (not CCR1: the duty changes every 10 ms legitimately, nor CNT). DAC: CR.

**`app_periph_register()`**: registers the three peripherals with their tables and re-init functions, and three ISRs: `adc_isr` (history on, recovery = re-init ADC), `tim6_isr` (history **off**: 1000 events/s would flood the ring buffer; recovery = stop the storm), `btn_isr` (history on, no recovery).

**Recovery functions.** Each first pulses the peripheral's reset line in RCC, which returns **every** register to its reset value (including ones we never write), then configures it exactly as at boot:
- `app_adc_reinit`: DMA IRQ off; RCC APB2RSTR.ADCRST pulse (resets all ADCs and the common registers); AHB1RSTR.DMA2RST pulse (resets the whole DMA2 controller, which only this stream uses); `adc_dma_config()`.
- `app_pwm_reinit`: save the duty; APB1RSTR.TIM2RST pulse; restore duty; `tim2_config()`; `app_pwm_pin_init()`.
- `app_dac_reinit`: save the output code (DHR12R1); APB1RSTR.DACRST pulse; `dac_config()`; restore the code.
- `app_tim6_reinit`: `app_tim6_storm(false)`.

**Runtime helpers.** `app_adc_latest()` copies the three sample fields with interrupts disabled so they belong to the same scan. `app_pwm_set(d)` writes CCR1 (clamped to 1000). `app_dac_set(c)` writes DHR12R1 (clamped to 4095). `app_set_ctrl_task(h)` stores who to wake.

**`DMA2_Stream0_IRQHandler`** (the ADC "scan done" interrupt):

```c
void DMA2_Stream0_IRQHandler(void)
{
    FM_ISR_ENTER(COMP_ISR_ADC);
    uint32_t isr = DMA2->LISR;
    DMA2->LIFCR = 0x3DU;
    if (isr & DMA_LISR_TEIF0) {
        fm_periph_error(COMP_PER_ADC, APP_PERR_DMA_TE);
    }
    if (ADC1->SR & ADC_SR_OVR) {
        ADC1->SR &= ~ADC_SR_OVR;
        fm_periph_error(COMP_PER_ADC, APP_PERR_ADC_OVR);
    }
    if (isr & DMA_LISR_TCIF0) {
        s_samples.pv_raw   = s_dma_buf[0];
        s_samples.vref_raw = s_dma_buf[1];
        s_samples.seq++;
        TaskHandle_t t = s_ctrl_task;
        if (t != NULL) {
            BaseType_t woken = pdFALSE;
            vTaskNotifyGiveFromISR(t, &woken);
            portYIELD_FROM_ISR(woken);
        }
    }
    FM_ISR_EXIT(COMP_ISR_ADC);
}
```
1. Tell the framework this ISR started (timing, rate).
2. Read DMA2's low interrupt status register (streams 0-3).
3. Clear stream 0's flags right away (otherwise the interrupt fires again immediately).
4-6. Transfer error (TEIF0): report a peripheral error on ADC1.
7-10. ADC overrun (OVR, a conversion was lost because DMA was late): clear it and report.
11. Transfer complete (TCIF0): both values have arrived...
12-14. ...copy them out and count the scan.
15-16. If the ctrl task is known...
17-18. ...give it a "task notification" (a lightweight binary semaphore built into every task) from interrupt context. `woken` becomes true if ctrl now outranks the interrupted task...
19. ...in which case request a context switch at the end of the interrupt (PendSV), so ctrl runs immediately after this ISR.
20. Tell the framework the ISR ended (duration).

This handler lives here, not in `stm32f4xx_it.c`, because CubeMX does not know about this DMA stream (it is configured by hand).

**TIM6 tick.** `app_tick_isr()` increments `s_ms`; in storm mode it also burns time in a 300-iteration empty loop on every tick. `app_ms()` returns the counter.

```c
void app_tim6_storm(bool on)
{
    s_storm = on;
    TIM6->PSC = on ? 839U : 8399U;
    TIM6->ARR = 9U;
    TIM6->EGR = TIM_EGR_UG;
}
```
Normal: 84 MHz / 8400 = 10 kHz, / (9 + 1) = 1 kHz. Storm: 84 MHz / 840 = 100 kHz, / 10 = 10 kHz, ten times the interrupt rate. (ARR stays 9 because a basic timer with ARR = 0 stops counting.) `EGR.UG` loads the new prescaler immediately.

### Design choices

- **Register-level drivers** instead of HAL: every bit is visible and documented, the re-init functions are exact, and the register-check tables can list precisely the bits that matter.
- **Hardware-in-the-loop plant on one pin**: real conversion noise, timing and failure modes without external parts. A pure software plant would never see an ADC or DMA fault.
- **ADC triggered by TIM2 and delivered by DMA**: the sampling rate is set by hardware, so jitter in software does not change it, and the CPU only wakes once per scan.

### How to test it alone

Monitor-off build (`-DFM_ENABLE_MONITOR=0`): LED LD2 brightness follows the heater power, and the `TEL` line's measured value settles at the setpoint. With a debugger, `s_samples.vref_raw` should be about 1500 and `seq` should grow by 100 per second.

---

## app/controller.h / controller.c

| | |
|---|---|
| **Path** | `Core/Src/app/controller.h`, `controller.c` |
| **Owner** | Abhishek |
| **What it is** | The application: a closed-loop temperature controller. The ctrl task (woken by every ADC scan, 100 Hz) runs stages A acquire → B filter → C PID → D actuate. The report task (every 100 ms) runs stage E, a telemetry frame with a checksum, printed once per second. The plant is a first-order thermal model written to the DAC. Also the button and the `T`/`Y` commands, and the task-restart recovery. |
| **Why it matters** | It is the "application under observation": the pipeline A→E is the problem statement's own example of an execution sequence. |
| **What would break without it** | Nothing to monitor. |

### Walkthrough: controller.h

```c
typedef struct {
    float kp, ki, kd;
    float tau_s, gain_c, t_amb_c;
    float dt_s, filt_alpha;
} app_cfg_t;
_Static_assert(sizeof(app_cfg_t) == 32, "MPU region needs exactly 32 bytes");
```
Eight floats = 32 bytes, the smallest MPU region, so it can be made read-only after boot. Writing to it is a memory-access fault (fault 5 does exactly that).

Declared: `controller_init`, `controller_restart` (recovery), `app_task_period_ms`, `app_task_handle`, `app_cfg`.

### Walkthrough: controller.c

**Name tables**: `g_comp_names[]` and `g_stage_names[]` (`A_acquire`, `B_filter`, `C_pid`, `D_actuate`, `E_report`), indexed by the enums with designated initialisers.

**Configuration**:

```c
static app_cfg_t s_cfg __attribute__((aligned(32))) = {
    .kp = 0.05f, .ki = 0.04f, .kd = 0.0f,
    .tau_s = 1.5f, .gain_c = 70.0f, .t_amb_c = 25.0f,
    .dt_s = 0.01f, .filt_alpha = 0.3f,
};
```
`aligned(32)`: an MPU region must start at a multiple of its size. PID gains (the derivative gain is 0 and not used by `pid_step`), plant time constant 1.5 s, heater gain 70 °C at full power, ambient 25 °C, step 10 ms, filter weight 0.3.

**State**: setpoint (50 °C at boot), measured value, heater power, plant temperature (starts at 25 °C), filter state, integrator, telemetry on/off, "setpoint changed" flag, last button time, frame counter, the two task handles and their attributes.

**`pid_step()`** (stage C), line by line:

```c
float e = s_setpoint - s_pv_filt;
float i = s_integ + s_cfg.ki * e * s_cfg.dt_s;
float u = s_cfg.kp * e + i;
if (u > 1.0f) {
    u = 1.0f;
} else if (u < 0.0f) {
    u = 0.0f;
} else {
    s_integ = i;
}
s_u = u;
```
1. Error = how far below the setpoint we are (°C).
2. Candidate integral: add ki × error × 10 ms.
3. Output = proportional part + integral, as a fraction 0..1 of heater power.
4-5. Above 100 %: clamp...
6-7. ...below 0 % (a heater cannot cool): clamp...
8-9. ...only when not saturated, accept the new integral. This is *conditional integration*, an anti-windup method: while the output is clamped, the integral stops growing, so the controller does not overshoot badly when it comes out of saturation.
10. Publish the output.

**`ctrl_task_fn`** (stages A to D):

```c
app_pwm_pin_init();
app_set_ctrl_task(xTaskGetCurrentTaskHandle());
for (;;) {
    uint32_t got = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50));
    fault_id_t f = inject_point(COMP_CTRL);
    if (got == 0U) {
        fm_periph_error(COMP_PER_ADC, APP_PERR_TIMEOUT);
    }
    FM_STAGE(STAGE_A_ACQUIRE);
    app_samples_t smp = app_adc_latest();
    if (smp.vref_raw < APP_VREFINT_LO || smp.vref_raw > APP_VREFINT_HI) {
        fm_periph_error(COMP_PER_ADC, APP_PERR_VREF);
    }
    FM_STAGE(STAGE_B_FILTER);
    float pv = (float)smp.pv_raw * (100.0f / 4095.0f);
    s_pv_filt += s_cfg.filt_alpha * (pv - s_pv_filt);
    s_pv = s_pv_filt;
    FM_STAGE(STAGE_C_CONTROL);
    pid_step();
    if (f == FAULT_SEQ_REPEAT) {
        FM_STAGE(STAGE_C_CONTROL); pid_step();
        FM_STAGE(STAGE_C_CONTROL); pid_step();
    }
    FM_STAGE(STAGE_D_ACTUATE);
    app_pwm_set((uint32_t)(s_u * (float)APP_PWM_TOP));
    s_plant_c += (s_cfg.t_amb_c + s_cfg.gain_c * s_u - s_plant_c) * (s_cfg.dt_s / s_cfg.tau_s);
    app_dac_set((uint32_t)(s_plant_c * (4095.0f / 100.0f)));
}
```
1. Give PA5 back to TIM2 (the BSP made it a plain output).
2. Tell the DMA ISR which task to wake.
3. Forever:
4. Sleep until the ADC ISR notifies us (`pdTRUE` = clear the count), or 50 ms (5 periods) pass.
5. Fault-injection hook: a pending fault aimed at ctrl fires **here, in this task's context and stack**.
6-8. Woken by timeout instead of data: report "no sample" as an ADC peripheral error.
9. Stage A marker. 10. Read the latest scan.
11-13. Sensor health: VREFINT should read about 1500; outside 1200..1800 something is wrong with the ADC (or the supply).
14. Stage B. 15. Raw 0..4095 → 0..100 °C.
16. First-order low-pass filter: move 30 % of the way towards the new reading (smooths ADC noise).
17. Publish.
18-19. Stage C, PID.
20-23. The injected sequence fault: run C twice more, so the trace shows A→B→C→C→C. C→C was never seen while learning.
24. Stage D. 25. Heater power → PWM duty 0..1000 (LED brightness).
26. **Plant model** (Euler step of dT/dt = (T_amb + G·u − T) / tau): the temperature moves towards "ambient + 70 °C × power" with a 1.5 s time constant. At full power it would settle at 95 °C.
27. Temperature → DAC code (0..100 °C → 0..4095), which appears on PA4 and is measured by the next ADC scan.

**`report_task_fn`** (stage E):

```c
uint32_t next = osKernelGetTickCount();
for (;;) {
    next += PERIOD_REPORT_MS;
    uint32_t now = osKernelGetTickCount();
    if ((int32_t)(next - now) <= 0) next = now + PERIOD_REPORT_MS;
    osDelayUntil(next);
    inject_point(COMP_REPORT);
    FM_STAGE(STAGE_E_REPORT);
    /* build "TEL,<frame>,<ms>,<setpoint>,<pv>,<power %>" with one decimal each */
    /* XOR checksum of the line; s_frame_no++ */
    if (s_sp_changed) { s_sp_changed = false; print "SETPOINT x.x C"; }
    if (s_tel_on && (s_frame_no % 10U) == 0U) print "<line>*<checksum hex>";
}
```
A fixed 100 ms period with drift-free `osDelayUntil` (resyncing if it fell behind), the injection hook, the stage E marker, the frame built with `snprintf` and printed every 10th frame (1 Hz) with an NMEA-style XOR checksum. `x10()` rounds a float to tenths so it can be printed without `%f`.

**`HAL_GPIO_EXTI_Callback(pin)`** (button B1, PC13, from the EXTI interrupt): ignore presses within 250 ms of the last one (contact bounce), then step the setpoint +10 °C, wrapping from 70 back to 40.

**`cmd_setpoint` (`T <deg>`)**: accepts 20..90 °C. **`cmd_telemetry` (`Y 1|0`)**: telemetry on/off.

**`controller_restart(comp)`** (the recovery action for ctrl and report):

```c
if (comp_id == COMP_CTRL) {
    NVIC_DisableIRQ(DMA2_Stream0_IRQn);
    app_set_ctrl_task(NULL);
    if (s_ctrl_h) osThreadTerminate(s_ctrl_h);
    s_integ = 0.0f;
    s_ctrl_h = osThreadNew(ctrl_task_fn, NULL, &s_ctrl_attr);
    fm_register_task(COMP_CTRL, s_ctrl_h, "ctrl", controller_restart);
    NVIC_EnableIRQ(DMA2_Stream0_IRQn);
} else if (comp_id == COMP_REPORT) { /* terminate, recreate, re-register */ }
```
1-3. The DMA ISR notifies ctrl; while the old handle is being destroyed it must not be used, so silence the interrupt and clear the handle.
4. Delete the old task (FreeRTOS frees its stack and TCB).
5. Reset the integrator (a stuck task may have left it wound up).
6. Create a fresh task.
7. Register it again: new tag, new MPU guard on the new stack, new priority.
8. Interrupt back on.

**`app_task_period_ms`**: ctrl 10 ms, report 100 ms, others 100 ms (used by the MISSED_DEADLINE fault). **`app_task_handle`**: handles for ctrl, report, shell.

**`controller_init()`**: create both tasks and register them; register stages A-D as owned by ctrl and E by report; make `s_cfg` read-only with `fm_mpu_protect_ro`; register shell letters `T` and `Y`.

### Design choices

- **ctrl woken by the ADC, not by a timer**: the control loop is locked to the sampling, and "no data" becomes a detectable symptom (timeout → peripheral error), which helps localize ADC faults to the ADC rather than to ctrl.
- **Telemetry in a separate, lower-priority task**: printing must never delay control. It also gives the pipeline a second owner, which makes per-task sequence tracking necessary and realistic.
- **Restart by delete-and-recreate**: the only way to get a task out of an infinite loop; FreeRTOS cannot "reset" a running task in place.

### How to test it alone

Monitor-off build, `T 70`: the `TEL` measured value rises towards 70 within about 5 s and the LED gets brighter. Press B1: `SETPOINT` changes 60 → 70 → 40 → 50 (it starts at 50).

> **Fixed 19 Oct:** `main.c` calls `controller_init()` (which registers `T` and `Y`) before `uart_shell_init()`. `uart_shell_init()` used to reset the command table, which silently dropped `T` and `Y`; it no longer does (the table is a zero-initialised static), so registration order does not matter.

---

## app/idle_stats.h / idle_stats.c

| | |
|---|---|
| **Path** | `Core/Src/app/idle_stats.h`, `idle_stats.c` |
| **Owner** | Abhishek |
| **What it is** | A CPU-headroom counter: the FreeRTOS idle hook (a function called over and over whenever no task wants the CPU) increments a counter. More spare CPU means more increments per second. |
| **Why it matters** | Measures the framework's CPU overhead (monitor ON build vs OFF build), one of the evaluation numbers. |
| **What would break without it** | No `IDLE` line in `S`, so no CPU overhead figure in `fm_metrics.py`. |

### Walkthrough

`idle_stats_init()` zeroes the counter and stores the start tick. `idle_stats_on_idle_hook()` does `s_idle_count++` (called from `vApplicationIdleHook` in `main.c`). `idle_stats_get_count()` and `idle_stats_get_uptime_ms()` return the count and the ms since init. Overhead % ≈ 100 × (idle rate OFF − idle rate ON) / idle rate OFF.

### Design choices

The idle-counter method needs no extra hardware or FreeRTOS run-time-stats timer. It is relative, not a calibrated percentage, which is why it is always used as an ON/OFF comparison. (The header mentions `tools/overhead.py`; that calculation is actually in `tools/fm_metrics.py --overhead`. The `.c` comment says the hook lives in `freertos.c`; it is in `main.c`.)

### How to test it alone

Type `S` twice 10 s apart: the `IDLE` count grows, and uptime grows by about 10 000.

---
## inject/fm_uart.h / fm_uart.c

| | |
|---|---|
| **Path** | `Core/Src/inject/fm_uart.h`, `fm_uart.c` |
| **Owner** | Abhishek |
| **What it is** | The USART2 driver used by everything that prints or reads: shell, telemetry, alerts, `V` stream. Receive: one byte per interrupt into a FreeRTOS queue. Transmit: interrupt-driven, the calling task sleeps on a semaphore while its line goes out; a mutex keeps lines from different tasks whole. UART error flags are reported to the framework as communication errors. |
| **Why it matters** | It is the system's voice, and the source of the "communication errors" signal. USART2 is also a monitored peripheral (config CRC) and a monitored ISR. |
| **What would break without it** | No console, no reports printed, no shell. |

### Walkthrough

**State**: RX queue (96 bytes), the TX lock (a binary semaphore) and the task holding it, TX-done semaphore, the 1-byte HAL receive buffer, a 256-byte TX buffer.

**`fm_uart_init()`**: create the queue, the TX lock (a binary semaphore starting at 1, see the design choices for why not a mutex) and the TX-done semaphore starting at 0, then `HAL_UART_Receive_IT(..., 1)` to receive the first byte by interrupt.

**Register checks for USART2**: BRR (baud rate) fully; CR1 only UE, M, WAKE, PCE, PS, TE, RE, OVER8 (frame format and enables); CR2 STOP, LINEN, CLKEN; CR3 ONEBIT, CTSE, RTSE, HDSEL, SCEN, IREN. The interrupt-enable bits are left out because HAL turns them on and off for every transfer.

**`fm_uart_register()`**: USART2 as a peripheral with `is_comm = true` (its errors are COMM faults), recovery `fm_uart_reinit`; the USART2 interrupt as an ISR with history **off** (one interrupt per byte would flood the ring buffer).

**`fm_uart_rx_isr_byte(b)`** (from the RX-complete callback): put the byte in the queue with timeout 0 (if full, drop: never block in an ISR), and re-arm reception of the next byte.

**`fm_uart_inject_rx(s)`**: push a string into the RX queue as if it had been received. Used by fault 8 to inject a corrupted frame.

**`fm_uart_getc(timeout)`**: wait for a byte or return -1.

**`HAL_UART_TxCpltCallback`**: when HAL finishes sending, release the semaphore (wakes the sending task).

**`HAL_UART_ErrorCallback`**: map HAL's error code to our flags (ORE overrun, FE framing, NE noise; parity errors are mapped to FE too), report `fm_comm_error(COMP_PER_UART, flags)`, and re-arm reception (HAL stops receiving after an error).

**`fm_uart_write(data, len)`**, line by line:

```c
if (osKernelGetState() != osKernelRunning || s_tx_mutex == NULL) {
    HAL_UART_Transmit(&FM_UART_HANDLE, (uint8_t *)data, (uint16_t)len, 100);
    return;
}
if (osSemaphoreAcquire(s_tx_mutex, FM_UART_TX_TIMEOUT_MS) != osOK) {
    return;
}
s_tx_owner = osThreadGetId();
while (len > 0U) {
    uint32_t n = len > sizeof(s_tx_buf) ? sizeof(s_tx_buf) : len;
    memcpy(s_tx_buf, data, n);
    osSemaphoreAcquire(s_tx_done, 0);
    if (HAL_UART_Transmit_IT(&FM_UART_HANDLE, s_tx_buf, (uint16_t)n) != HAL_OK ||
        osSemaphoreAcquire(s_tx_done, FM_UART_TX_TIMEOUT_MS) != osOK) {
        HAL_UART_AbortTransmit(&FM_UART_HANDLE);
        break;
    }
    data += n; len -= n;
}
s_tx_owner = NULL;
osSemaphoreRelease(s_tx_mutex);
```
1-4. Before the scheduler runs (the boot banner), there are no tasks to sleep: use HAL's simple blocking (polling) transmit with a 100 ms timeout.
5-8. Take the UART for this whole line and remember which task holds it; if it cannot be had within 500 ms (UART wedged), drop the line rather than hang.
8. Send in chunks of at most 256 bytes:
9-10. copy the chunk into our own buffer (the caller's buffer may be on its stack);
11. clear any stale "done" signal;
12-13. start the interrupt-driven transmit and **sleep** until the TX-complete callback releases the semaphore, at most 500 ms;
14-15. on failure, abort the transfer and give up on the rest;
17. advance. 19-20. Forget the holder and release the UART.

**`fm_uart_forget_task(handle)`** (new, called by every restart function just before it deletes a task): if that task holds the TX lock, abort its transfer and release the lock for it.

**`fm_uart_printf(fmt, ...)`**: format into a 256-byte local buffer with `vsnprintf`, truncate if needed, `fm_uart_write`. Note each call uses 256 bytes of the **caller's** stack.

**`fm_uart_reinit(comp)`** (recovery for USART2 and its ISR): take the TX lock if possible, abort any transfer, `HAL_UART_DeInit`, pulse the USART2 reset line in RCC (every register back to reset value), `HAL_UART_Init` (boot configuration from `huart2.Init`), re-arm reception, release the lock.

### Design choices

- **Interrupt-driven TX with a sleeping caller.** A busy-wait transmit of a 100-character line keeps the CPU busy for almost 9 ms; to the monitor that looks exactly like a task stuck in a loop. Sleeping makes printing nearly free in CPU terms.
- **A binary semaphore, not a mutex, plus `fm_uart_forget_task()`.** FreeRTOS lets only the owner release a mutex, so a task deleted by a recovery while printing would lock the console forever (every later line, including the monitor's alerts, would wait 500 ms and be dropped). A semaphore can be released by anyone, so the restart function hands the lock back. The price is losing priority inheritance: the monitor may wait for the rest of a lower-priority task's line (at most 256 bytes, about 22 ms at 115200 baud), which is acceptable for console output. Changed on 19 Oct after a review found the stranded-mutex case.

### How to test it alone

Monitor-off build: type characters, they echo; `H` prints the help; paste a 200-character line: no characters lost (the queue holds 96, the shell drains it quickly).

---

## inject/uart_shell.h / uart_shell.c

| | |
|---|---|
| **Path** | `Core/Src/inject/uart_shell.h`, `uart_shell.c` |
| **Owner** | Abhishek |
| **What it is** | The command shell: a task that reads lines from the UART and dispatches on the first character. Modules claim letters with `fm_shell_register`. Two line formats: plain (`T 55`) for people, framed (`$T 55*hh`, XOR checksum) for scripts; a bad checksum is rejected and counted as a communication error. The shell wakes every 100 ms even without input, so it is a periodic, monitorable task and an injection target. |
| **Why it matters** | Human and script access to every feature; source of the "frame checksum" communication-error signal. |
| **What would break without it** | No commands: no learning on request, no injection, no reports read-out. |

### Walkthrough

`FM_SHELL_MAX_HANDLERS 24`, `FM_SHELL_LINE_LEN 96`, `FM_SHELL_POLL_MS 100`. The handler table holds (letter, function pointer) pairs.

**`fm_shell_register(letter, handler)`**: refuses if the table is full or the letter is taken, else appends.

**`hexval(c)`**: '0'..'9' → 0..9, 'a'..'f'/'A'..'F' → 10..15, else -1.

**`unframe(line)`** (validates `$<body>*hh`), line by line:

```c
char *star = strrchr(line, '*');
if (star == NULL || hexval(star[1]) < 0 || hexval(star[2]) < 0) {
    fm_comm_error(COMP_PER_UART, FM_COMM_CRC);
    return false;
}
uint8_t cs = 0;
for (char *p = line + 1; p < star; p++) {
    cs ^= (uint8_t)*p;
}
uint8_t want = (uint8_t)(hexval(star[1]) * 16 + hexval(star[2]));
if (cs != want) {
    fm_comm_error(COMP_PER_UART, FM_COMM_CRC);
    return false;
}
*star = '\0';
memmove(line, line + 1, strlen(line));
return true;
```
1. Find the **last** `*`.
2-5. No star, or not followed by two hex digits: a malformed frame, report a comm error (flag CRC) and reject. (If `star[1]` is the end of the string, `hexval` returns -1 and `star[2]` is never read.)
6-9. XOR every character between `$` and `*`.
10. The two hex digits as a number.
11-14. Mismatch: comm error, reject.
15. Cut the line at the star.
16. Shift left by one to drop the `$` (`strlen(line)` bytes starting at `line+1` include the terminating zero).
17. Valid.

Example: `T 55` → 'T' (0x54) ^ ' ' (0x20) ^ '5' (0x35) ^ '5' (0x35) = 0x74, so the valid frame is `$T 55*74`. The injected fault sends `$T 55*00`.

**`dispatch(line)`**: unframe if it starts with `$` (print `ERR frame checksum` on failure); upper-case the first character; skip spaces; call the matching handler with the rest; else `ERR unknown command`.

**`shell_task_fn`**:

```c
for (;;) {
    int c = fm_uart_getc(FM_SHELL_POLL_MS);
    inject_point(COMP_SHELL);
    if (c < 0) continue;
    if (c == '\r' || c == '\n') { if (len) { line[len] = 0; write "\r\n"; dispatch(line); len = 0; } continue; }
    if ((c == 0x08 || c == 0x7F) && len > 0U) { len--; write "\b \b"; continue; }
    if (len < FM_SHELL_LINE_LEN - 1U && c >= 0x20 && c < 0x7F) { line[len++] = c; echo c; }
}
```
Wait up to 100 ms for a byte; run the injection hook (so faults aimed at the shell fire even when nobody types); on end of line, terminate and dispatch; backspace/delete erases on screen (`\b \b` = back, space, back); printable characters are stored and echoed; anything else is ignored. Lines are capped at 95 characters.

**`cmd_help` (`H`)**: prints the full command list in five `fm_uart_printf` calls (each stays under the 256-byte line buffer).

**`uart_shell_restart(comp)`** (recovery): terminate, recreate and re-register the shell task. **`uart_shell_init()`**: reset the table to empty, register `H`, create the shell task (512 words, `osPriorityLow`) and register it as `COMP_SHELL`.

### Design choices

- **Registration table** instead of one big `switch`: each module adds its own letters without editing this file.
- **Framed commands with a checksum** for scripts, so line noise cannot turn into a wrong command, and corrupted frames become a measurable signal.
- **100 ms poll** makes the shell periodic, so liveness can be checked on it and it can be a fault target like any task.

### How to test it alone

Type `H`; type `$H*48` ('H' = 0x48): help again; type `$H*00`: `ERR frame checksum`, and with the monitor on, a COMM fault on USART2.

---

## inject/fault_inject.h / fault_inject.c

| | |
|---|---|
| **Path** | `Core/Src/inject/fault_inject.h`, `fault_inject.c` |
| **Owner** | Abhishek |
| **What it is** | The fault injector: it creates **real** faults inside the running firmware (a real infinite loop, a real stack overflow, a real register corruption...) and records exactly what it did and when (the ground truth). The monitor is never told; it only sees the effects. Task faults fire inside the target task at its next `inject_point()`; peripheral, ISR and UART faults fire from the 1 kHz TIM6 tick. Also the examiner commands `W` (write any word) and `P` (change a task priority). |
| **Why it matters** | Without it we could not prove detection works or measure accuracy and latency. |
| **What would break without it** | No evaluation, no demo. |

### Walkthrough

**Fault names and target rules.** `g_fault_names[]`; `s_info[]` gives each fault a target class and the expected outcome text: `TGT_TASK` (ctrl, report or shell), `TGT_CTRL` (ctrl only), `TGT_TIM6`, `TGT_PERIPH` (ADC1, TIM2, DAC1, USART2), `TGT_UART`. `target_ok(f, comp)` checks the pair.

**The armed fault** (`s_armed`: pending, fault, component, fire time in ticks) is shared by tasks and the TIM6 ISR, so it is always touched with PRIMASK.

**Ground-truth log**: 64 entries in a circular array. `log_append` stamps the cycle counter, component, fault and delay under PRIMASK. `inject_log_entry(i)` returns entries oldest first (start at 0 until the log wraps, then at the head).

**`fault_inject_init()`**: enables the DWT cycle counter (so timestamps work even in the monitor-off build), clears the armed fault and the log.

**`inject_arm(id, comp, delay)`**: validates; under PRIMASK, if nothing is pending, stores the fault with `fire_at = now + delay`; then logs the "arm" record. One fault at a time.

**`inject_clear()`**: cancels the pending fault.

**`take(comp)`** (the heart of firing), line by line:

```c
fault_id_t f = FAULT_NONE;
uint32_t pm = __get_PRIMASK();
__disable_irq();
if (s_armed.pending && s_armed.comp_id == comp &&
    (int32_t)(osKernelGetTickCount() - s_armed.fire_at) >= 0) {
    f = s_armed.fault_id;
    s_armed.pending = false;
}
__set_PRIMASK(pm);
if (f != FAULT_NONE) {
    log_append(f, comp, 0);
    fm_note_injection(comp, (uint8_t)f);
}
return f;
```
1. Default: nothing to do.
2-3. Mask interrupts (the TIM6 ISR could take the same fault).
4-5. Is a fault pending, aimed at this component, and due? `(int32_t)(now - fire_at) >= 0` is the wrap-safe way to say "now ≥ fire_at".
6-7. Take it and clear it, so it fires exactly once.
8. Unmask.
9-11. Log the "fire" record (delay 0) and give the ground truth to the framework (ring buffer `EVT_INJECT` and `.noinit`).
12. Return what to do.

**The faults** (each runs in the target's own context):

- `do_stuck_loop()`: `for (;;) __NOP();` with interrupts on: only this task is stuck; everything above it still runs. `noinline` keeps it a separate function so the report's PC points at it.
- `do_stack_overflow(depth)`: a 64-byte `volatile` local array filled on every call, then a recursive call whose result is used (`+ eat[0]`) so the compiler cannot turn it into a loop. It recurses until the stack reaches the MPU guard. The `#pragma` silences GCC's "infinite recursion" warning, because that is the point.
- `do_wdg_hang()`: `__disable_irq()` then spin: nothing else can run, not the monitor, not the tick. Only the IWDG can end it.
- `corrupt_periph(comp)`: ADC1: `CR2 ^= 1 << EXTSEL_Pos` (EXTSEL 0110 → 0111, a different trigger source, so conversions stop); TIM2: `PSC = 83` (10× faster PWM and ADC trigger); DAC1: clear EN1 (output off); USART2: set CR3.ONEBIT (different sampling method). Each changes a register in that peripheral's check table, so its CRC changes.

**`inject_point(comp)`** (called by every application task once per loop):

```c
fault_id_t f = take(comp);
switch (f) {
case FAULT_STUCK_LOOP:      do_stuck_loop();                                   break;
case FAULT_STACK_OVERFLOW:  (void)do_stack_overflow(0);                        break;
case FAULT_MISSED_DEADLINE: osDelay(10U * app_task_period_ms(comp));           break;
case FAULT_MEM_RO_WRITE:    app_cfg()->kp = 1.0f;                              break;
case FAULT_NULL_DEREF:      *(volatile uint32_t *)0x00000010UL = 0xDEADBEEFUL; break;
case FAULT_WDG_HANG:        do_wdg_hang();                                     break;
case FAULT_SEQ_REPEAT:      return f;
default: break;
}
return FAULT_NONE;
```
1. Is there a fault for me now?
2. Act it out:
3. spin forever;
4. recurse into the guard (MemManage → reset → STACK_OVERFLOW report);
5. sleep 10 periods (100 ms for ctrl, 1 s for report or shell) → LIVENESS;
6. write the read-only gains (MemManage, DACCVIOL, MMFAR = the config address → MEM_ACCESS);
7. write to address 0x10, inside the 1 KB null guard (MemManage → MEM_ACCESS);
8. interrupts off forever (IWDG reset → WATCHDOG);
9. sequence fault: the ctrl task itself must act it out (run stage C three times), so return it;
10. otherwise nothing.

**`fault_inject_on_tick()`** (from the TIM6 interrupt, 1 kHz): if a fault is pending and its target is TIM6, a peripheral or the UART, `take()` it and: start the ISR storm, corrupt the peripheral, or push `"$T 55*00\r"` (bad checksum) into the RX queue.

**Shell commands.**
- `I` alone: list faults and valid component numbers. `I <fault> <comp> [delay_ms]`: validate (delay 0..60 000 ms) and arm; print the expected verdict.
- `X`: cancel.
- `G`: the ground-truth log as CSV (`cycle_ts,comp_id,fault_id,delay_ms`).
- `W <hex addr> <hex value>`: parse two hex numbers, refuse unaligned addresses, print what will happen, wait 20 ms for the line to go out, then write the word. An examiner can corrupt any register, the protected config, or address 0, unscripted.
- `P <comp> <prio>`: for ctrl, report or shell, set the FreeRTOS priority (8 to 48). Used to show why the monitor must outrank application tasks.

`fault_inject_register_shell()` registers `I X G W P`.

### Design choices

- **Real faults, not flags.** A flag like "pretend ctrl is slow" would test the detector against our own assumptions. A real infinite loop tests the whole chain, including the MPU, the scheduler and recovery.
- **Faults fire in the target's own context** (via `inject_point`) so the blamed component really is the one misbehaving, on its own stack.
- **Ground truth logged at the moment of firing** (not arming), so latency is measured from the real start of the fault.

### How to test it alone

`I 1 1 500` then `G`: two records (arm with delay 500, fire with delay 0). With the monitor on: `!FAULT ... type=EXEC_OVERRUN comp=report(1)` and `!GT ... LOCALIZED CORRECTLY`.

---

## The Python tools

All in `tools/`, run on the PC. They talk to the board over the ST-LINK virtual COM port (115200 8-N-1) using `pyserial`.

### tools/fm_serial.py

| | |
|---|---|
| **Owner** | Abhishek |
| **What it is** | Shared helpers: open the port, send a command, read lines for a while, frame a command, parse `key=value` tokens. |
| **Why it matters / without it** | Used by `fm_campaign.py` and `fm_report.py`; without it they would each duplicate serial code. |

Walkthrough:
- `open_port(port, baud, timeout)`: imports `serial` lazily (a clear message if missing), opens the port, waits 200 ms, flushes old input.
- `send(s, cmd)`: writes the command plus `\r` **one character at a time with 2 ms gaps**, so the shell's per-character echo keeps up and the 96-byte RX queue never overflows.
- `framed(cmd)`: XOR of the characters → `$cmd*HH`.
- `lines(s, seconds, log)`: a generator: yields each decoded, stripped line until time runs out, optionally writing `<time> <line>` to a log file.
- `parse_kv(line)`: `"!FAULT seq=3 type=X comp=ctrl(0)"` → `{"seq": "3", "type": "X", "comp": "ctrl(0)"}`.

Test: `python -c "from fm_serial import framed; print(framed('T 55'))"` prints `$T 55*74`.

### tools/fm_dump.py

| | |
|---|---|
| **Owner** | Abhishek |
| **What it is** | The capture-layer checker: `dump` sends `D` and saves the ring buffer as a CSV with names plus a summary; `stream -t N` sends `V 1`, collects `SIG` lines for N seconds into a wide CSV (one column per feature); `status` prints the `S` output; `parse-dump` re-analyses a saved dump offline. |
| **Why it matters** | Proves capture works; produces the dataset for `fm_ml.py`. |

Walkthrough:
- `EVT_NAMES`: event-type names in the same order as `fm_event_type_t`.
- `read_until_end`: collects lines until `END` (15 s timeout).
- `parse_dump_lines`: `#FMDUMP` gives the clock, `#C,` lines give component names, lines starting with a digit are events `idx,ts,comp,type,extra`.
- `summarize`: time span (`& 0xFFFFFFFF` handles a counter wrap), per-component counts of each event type, and the last 15 events in µs.
- `parse_sig_line`: splits a `SIG` line on commas; `T:` keeps the transition string, `E:` gives comm/periph/trap counts, `P:` per-component errors, `R:`/`Q:` the two verdicts, `L:` the label, `M:` the mode; every other field `id:runs:cpu:slice:gap:stack` becomes columns `c<id>_runs`, `c<id>_cpu`, ...
- `cmd_stream` always sends `V 0` at the end (`finally`), even after Ctrl-C.

Test: `python fm_dump.py --port COM5 dump` prints "saved events_....csv" and a summary where ctrl has many `STAGE` events.

### tools/fm_campaign.py

| | |
|---|---|
| **Owner** | Abhishek |
| **What it is** | The automated evaluation: first a fault-free period (counts false alarms), then every (fault, target) pair N times: arm it, wait for the verdict, record one CSV row per trial. |
| **Why it matters** | Produces the detection rate, localization accuracy, false positives/negatives and latency for the report. |

Walkthrough:
- `FAULTS`: fault id → (name, valid targets, expected detector type, whether it resets the board). It mirrors `s_info` in `fault_inject.c`.
- `run_trial`: sends `I <fault> <target> <delay>`; waits 6 s (20 s for faults that reset); on `!SHADOW` keeps the other localizer's opinion; on `!FAULT` parses type, component id (the number in parentheses), confidence, action, localizer and latency. If the board reports no latency (a reset fault, `latency_ms=-1`), it estimates it from PC time minus the arm delay. For non-reset faults it waits for `!RECOVERED`; for reset faults it waits for `MONITOR running` after the reboot. Then 2 s to settle.
- `normal_period`: counts `!FAULT` lines while nothing is injected.
- `main`: selects the localizer (`M 0/1`), turns telemetry off (`Y 0`), runs the normal period, the trials, writes `<out>.csv`, `<out>.log` and `<out>_fp.txt`, and turns telemetry back on.

Test: `python fm_campaign.py --port COM5 --only 1 --reps 1 --normal 10` runs three trials (stuck loop on ctrl, report, shell).

### tools/fm_metrics.py

| | |
|---|---|
| **Owner** | Abhishek |
| **What it is** | Turns a campaign CSV into the evaluation table (Markdown): trials, detection rate, false negatives, localization accuracy, type accuracy, false positives per hour, latency min/median/p95/max, a per-fault table, and optionally CPU overhead from two saved `S` outputs. |

Walkthrough: reads rows; `det` = detected, `loc` = detected and blamed the injected component, `typ` = expected fault type; latencies sorted for percentiles (p95 = the element at index `int(0.95 × n)`, clipped). False positives come from `_fp.txt`. Overhead: `idle_rate` reads the last `IDLE,<count>,<ms>` line from each file and computes count per ms; CPU overhead = 100 × (off − on) / off. Writes `<campaign>_metrics.md`.

Test: run on a hand-made 3-row CSV and check the percentages.

### tools/fm_report.py

| | |
|---|---|
| **Owner** | Arfan (per its header) |
| **What it is** | Reads the black box (`R` over the port, or a saved text file) and runs `arm-none-eabi-addr2line` on each report's PC, printing the function and `file:line`. |
| **Why it matters** | Localization "down to the source line": a stuck loop shows as `do_stuck_loop @ fault_inject.c:...`. |

Walkthrough: `addr2line(tool, elf, pc)` runs `addr2line -f -C -e <elf> <pc>` (function name, demangled, from this ELF) and returns "function @ file:line". `main` gets the `R,...` lines, splits the 10 fields, clears bit 0 of the PC (the Thumb bit) and prints one block per report; a PC of 0 is shown as "no PC: not a task fault".

Test: `python fm_report.py --file saved_R.txt --elf ../Mini_Project/Debug/Mini_Project.elf`.

### tools/fm_ml.py

| | |
|---|---|
| **Owner** | Arfan (model) with Abhishek (data), per its header |
| **What it is** | The offline "lightweight machine learning" study. Input: a `SIG` stream CSV recorded while faults are injected (each window carries its ground-truth label). It trains an **Isolation Forest** (unsupervised detection, learns from normal windows only, like our baseline) and a **decision tree** (supervised localization, small enough to export as C `if/else`), and compares both with the on-chip rule and statistical verdicts in the same lines. |
| **Why it matters** | Answers "could a learned model do better?", and `--export-c` shows how a tree could become a third on-chip localizer. |

Walkthrough:
- Keeps monitoring windows (`mode == 2`). Features: every `c<id>_...` column plus `comm_err`, `periph_err`. Time features (cpu, slice, gap) are divided by the window length so they become fractions.
- Label: `label_comp` (255 = normal).
- Isolation Forest (100 trees) fitted on normal windows; detection rate = anomalies among faulty windows; false-positive rate = anomalies among normal windows.
- Decision tree (default depth 5) on a 70/30 split (stratified if every class has at least 2 samples); prints accuracy and a per-class report.
- On-chip comparison: how many faulty windows each on-chip localizer flagged, how many normal ones, and localization accuracy. The script itself notes that per-window counts understate the on-chip localizers (they act on the first faulty window and then recover).
- `export_c`: walks the tree recursively and writes `uint8_t fm_tree_localize(const float *x)` as nested `if (x[k] <= threshold)` blocks returning the class.

Test: `python fm_ml.py stream.csv --export-c fm_tree.c`, then compile `fm_tree.c` with `gcc -c`.

### Design choices for the tools

- **Text protocol over the console** (CSV-like lines with prefixes like `SIG,`, `!FAULT`) rather than a binary protocol: humans can read the same stream the scripts parse.
- **Parsing tolerant of other output**: every parser keys on a prefix, so telemetry lines in between do no harm.

---
# Part 5 · CubeMX-generated files we edited

STM32CubeMX generates these files from `Mini_Project.ioc`. **Only code between `/* USER CODE BEGIN ... */` and `/* USER CODE END ... */` survives a "Generate Code"**; anything outside is overwritten. All our edits are inside those blocks (the linker script is not regenerated by CubeMX, but CubeIDE may recreate it if deleted).

## Core/Inc/FreeRTOSConfig.h

| | |
|---|---|
| **Path** | `Mini_Project/Core/Inc/FreeRTOSConfig.h` |
| **Owner** | CubeMX-generated; the `USER CODE Defines` block is Abhay's (capture hooks) |
| **What it is** | FreeRTOS's settings file. We added the two trace hooks that call our capture code at every task switch, and turned on FreeRTOS's own stack-overflow check. |
| **Why it matters** | Task execution time and gaps (the main timing signals) come from these hooks. |
| **What would break without it** | No task timing at all: tasks would never appear in the signature. |

### Walkthrough: the settings we rely on (generated part)

- `configUSE_PREEMPTION 1`: a higher-priority task that becomes ready runs immediately.
- `configTICK_RATE_HZ 1000`: 1 ms tick, so `osDelay(100)` = 100 ms.
- `configMAX_PRIORITIES 56`: the CMSIS-RTOS2 priority numbers (8, 24, 32, 40, 48) fit.
- `configTOTAL_HEAP_SIZE 32768`: 32 KB heap (heap_4) for all task stacks, queues and mutexes.
- `configUSE_TRACE_FACILITY 1`: needed for `uxTaskNumber` (our tag) and `vTaskGetInfo`.
- `configUSE_IDLE_HOOK 1`: calls `vApplicationIdleHook` (idle counter).
- `configUSE_MUTEXES 1`, `INCLUDE_vTaskDelete`, `INCLUDE_uxTaskPriorityGet`, `INCLUDE_xTaskGetCurrentTaskHandle`, `INCLUDE_vTaskDelayUntil`: used by recovery, registration and the periodic tasks.
- `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5`: interrupts with priority number 5 or higher (numerically, i.e. less urgent) may call FreeRTOS `FromISR` functions. TIM6 (5), USART2 (5) and DMA2 Stream0 (6) respect this. Shifted by `8 - 4` bits: BASEPRI value 0x50.
- `configASSERT`: on failure, disable interrupts and stop (the watchdog will then reset the board).

### Walkthrough: USER CODE Defines

```c
#if !defined(FM_ENABLE_MONITOR) || FM_ENABLE_MONITOR
#if defined(__GNUC__) && !defined(__ASSEMBLER__)
void fm_hook_task_in(uint32_t tag);
void fm_hook_task_out(uint32_t tag);
#endif
#define traceTASK_SWITCHED_IN()   fm_hook_task_in((uint32_t)pxCurrentTCB->uxTaskNumber)
#define traceTASK_SWITCHED_OUT()  fm_hook_task_out((uint32_t)pxCurrentTCB->uxTaskNumber)
#endif
```
1. Only if the monitor is not switched off. (This file does not include `fm_types.h`, so "not defined" means "on", matching the default there.)
2. Declare the two hook functions for the C compiler only (this header is also read by the assembler for the port's `.s` code, which must not see C declarations).
3-4. Prototypes.
6-7. FreeRTOS calls `traceTASK_SWITCHED_OUT()` just before choosing the next task and `traceTASK_SWITCHED_IN()` just after, inside `vTaskSwitchContext()` in `tasks.c`. There, `pxCurrentTCB` is the task being switched. We pass its `uxTaskNumber`, which `fm_register_task` set to `comp_id + 1`.

```c
#ifndef configCHECK_FOR_STACK_OVERFLOW
#define configCHECK_FOR_STACK_OVERFLOW 2
#endif
```
Method 2: at every switch-out FreeRTOS checks the stack pointer and that the lowest 16 bytes of the stack still hold the 0xA5 fill pattern. If not, it calls `vApplicationStackOverflowHook` (in `main.c`). This is the backup; the MPU guard is the primary detector.

### Design choices

Using FreeRTOS's official trace macros means no kernel source is modified, so a FreeRTOS upgrade keeps working. The alternative, per-task "I started / I ended" calls, would miss stuck tasks.

### How to test it alone

Capture-only build: `D` shows `TASK_IN` / `TASK_OUT` pairs for every task; with `-DFM_ENABLE_MONITOR=0` the hooks vanish.

---

## Core/Src/main.c

| | |
|---|---|
| **Path** | `Mini_Project/Core/Src/main.c` |
| **Owner** | CubeMX-generated; USER CODE blocks by the team (init order: all three) |
| **What it is** | The program's entry point: chip and clock set-up, the CubeMX peripherals (TIM6, USART2, GPIO clocks), then **the boot order of the whole system**, then the scheduler start. Also four small callbacks/hooks that connect HAL and FreeRTOS to our modules. |
| **Why it matters** | The order of initialisation is part of the design: wrong order means missing registrations or guards. |
| **What would break without it** | Nothing would start. |

### Walkthrough

**Includes (USER CODE Includes).** The application headers, then `fm_trace.h` and `fm_capture.h` always, then `fm_sigout.h` if the monitor is on, then either the detection headers (`detect/fm_monitor.h`, `detect/fm_watchdog.h`) or `fm_window_task.h` (capture only).

**Clock (generated `SystemClock_Config`).**

```c
RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
RCC_OscInitStruct.PLL.PLLM = 16;  PLLN = 336;  PLLP = RCC_PLLP_DIV4;
RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
AHBCLKDivider = DIV1; APB1CLKDivider = DIV2; APB2CLKDivider = DIV1;
HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2);
```
HSI (the 16 MHz internal RC oscillator) ÷ M 16 = 1 MHz, × N 336 = 336 MHz (PLL VCO), ÷ P 4 = **84 MHz** system clock. AHB 84 MHz, APB1 42 MHz (its timers TIM2/TIM6 run at 2 × 42 = 84 MHz), APB2 84 MHz (ADC clock source). 2 flash wait states are required at 84 MHz and 3.3 V. Voltage scale 3 (lowest-power regulator setting) supports this frequency.

**TIM6 (generated `MX_TIM6_Init`).** Prescaler 8399 → 84 MHz / 8400 = 10 kHz; period 9 → 10 counts → **1 kHz** update interrupt. TIM6 is a "basic timer" (no pins), used only as the application tick and for fault injection.

**USART2 (generated).** 115200 baud, 8 data bits, 1 stop bit, no parity, TX and RX, no flow control, 16× oversampling.

**`main()` USER CODE Init.**

```c
HAL_Init();
fm_capture_boot();
```
Right after `HAL_Init()` (and before the clock is configured), read and clear the reset cause and validate the `.noinit` crash record. Doing it first means nothing can overwrite or confuse those flags.

**USER CODE 2: the boot order.**

```c
fm_trace_init();
fm_mpu_init();
fault_inject_init();
idle_stats_init();
fm_uart_init();
fm_uart_register();
app_periph_init();
app_periph_register();
controller_init();
uart_shell_init();
fault_inject_register_shell();
#if FM_ENABLE_MONITOR
fm_trace_register_shell();
fm_sigout_init();
#if FM_ENABLE_DETECT
fm_monitor_init(COMP_MONITOR);
fm_watchdog_init(COMP_WATCHDOG);
#else
fm_window_task_init(COMP_MONITOR);
#endif
#endif
/* banner: "=== Fault Monitor v2 | process controller | core 84 MHz | monitor ON ===" */
HAL_TIM_Base_Start_IT(&htim6);
```
1. Capture first: start the cycle counter, salvage history after a watchdog reset, clear the registry (it must exist before anything registers).
2. MPU next: the null guard, fault handlers on, and it must be ready before tasks get their stack guards.
3. Injector (also enables the cycle counter for the monitor-off build).
4. Idle counter.
5-6. UART driver and its registration (peripheral + ISR).
7-8. Application peripherals and their registration.
9. Application tasks (ctrl, report), stages, read-only config, and shell letters `T`/`Y`.
10. The shell: **resets the command table**, registers `H`, creates the shell task.
11-13. Modules that add commands: `I X G W P`, `D`, `S V`.
14-15. The monitor (`L B R C M K`) and the watchdog last, so they see every registered component. The watchdog starts the IWDG here: from now on the board must be fed within 4 s.
16-17. Capture-only build: the window task instead.
18. Banner (polling transmit, the scheduler is not running yet).
19. TIM6 interrupt last of all, so the injector's tick never runs before everything exists.

Note on step 9: `uart_shell_init()` does not clear the command table, so the `T` and `Y` letters registered earlier by `controller_init()` stay (this was a bug, fixed 19 Oct).

Note: once FreeRTOS objects have been created (the first queue or task), FreeRTOS keeps interrupts of priority 5 and below in urgency (TIM6, USART2, DMA) masked until the scheduler starts, so these interrupts effectively begin at `osKernelStart()`.

**Generated tail of `main()`**: `osKernelInitialize()`, creation of the CubeMX `defaultTask`, `BSP_LED_Init(LED2)` (makes PA5 a plain output; the ctrl task later gives it back to TIM2), `BSP_PB_Init(BUTTON_USER, BUTTON_MODE_EXTI)` (button B1 on PC13 as an interrupt), `osKernelStart()`, which never returns.

**USER CODE 4: the glue callbacks.**

```c
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART2) fm_uart_rx_isr_byte(huart->pRxBuffPtr[-1]);
}
```
HAL calls this from the USART2 interrupt when the 1-byte receive is complete. HAL has already advanced `pRxBuffPtr` past the byte it stored, so `[-1]` is that byte.

```c
void vApplicationIdleHook(void) { idle_stats_on_idle_hook(); }
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{ (void)pcTaskName; fm_trap_stack_hook(xTask); }
```
The FreeRTOS idle hook feeds the idle counter; FreeRTOS's stack checker hands the offending task to the capture layer, which records a crash and resets.

**USER CODE 5 (`StartDefaultTask`)**: CubeMX insists on a default task; ours calls `osThreadExit()` immediately, so it does not wake 1000 times a second for nothing.

**`HAL_TIM_PeriodElapsedCallback`**: TIM1 is the HAL time base (generated, increments `uwTick`). In USER CODE Callback 1 we add: for TIM6, `app_tick_isr()` (1 kHz counter, storm work) and `fault_inject_on_tick()` (fires peripheral/ISR/UART faults).

### Design choices

- **HAL time base on TIM1, not SysTick**: FreeRTOS owns SysTick for its 1 ms tick; CubeMX moves HAL's tick to a timer to avoid a conflict.
- **One explicit boot order with a comment** rather than self-initialising modules: dependencies (registry before registration, MPU before guards, shell table before commands) are visible in one place.

### How to test it alone

After reset the banner and `BOOT #n reset cause: ...` appear, then `black box: ...`; `S` lists 13 components.

---

## Core/Src/stm32f4xx_it.c

| | |
|---|---|
| **Path** | `Mini_Project/Core/Src/stm32f4xx_it.c` |
| **Owner** | CubeMX-generated; USER CODE blocks by Abhay (traps and ISR markers) |
| **What it is** | The interrupt and exception handlers CubeMX knows about. We added: a call to `fm_trap()` at the top of the four fault handlers, and `FM_ISR_ENTER/EXIT` around the USART2, button and TIM6 interrupt handlers. |
| **Why it matters** | Memory-access anomalies and stack overflows become reports through here; ISR timing for three interrupts is captured here. |
| **What would break without it** | Faults would hang in `while (1)` with no report; three ISRs would be invisible to the monitor. |

### Walkthrough

**Fault handlers** (HardFault, MemManage, BusFault, UsageFault all follow the same pattern):

```c
void MemManage_Handler(void)
{
  uint32_t exc_return;
  __asm volatile ("mov %0, lr" : "=r" (exc_return));
  fm_trap(FM_TRAP_MEMMANAGE, exc_return);
  while (1) { }
}
```
1. A local variable for EXC_RETURN.
2. Inline assembly: copy the LR register into it. On exception entry the CPU puts a special value (EXC_RETURN) in LR that says which stack the interrupted code was using (bit 2) and whether the frame includes FPU registers (bit 4). It must be read before any function call overwrites LR.
3. Record the crash and reset (never returns).
4. The generated infinite loop, now unreachable.

`NMI_Handler` and `DebugMon_Handler` are left as generated.

**Peripheral interrupt handlers.**

```c
void USART2_IRQHandler(void)
{
  FM_ISR_ENTER(COMP_ISR_USART2);
  HAL_UART_IRQHandler(&huart2);
  FM_ISR_EXIT(COMP_ISR_USART2);
}
```
Mark entry, let HAL do its work (which calls our RX/TX/error callbacks), mark exit. The same for `EXTI15_10_IRQHandler` (button B1 on line 13, `COMP_ISR_BUTTON`) and `TIM6_DAC_IRQHandler` (`COMP_ISR_TIM6`). `TIM1_UP_TIM10_IRQHandler` (HAL time base) is not instrumented. The DMA2 Stream0 handler is in `app_periph.c` (not known to CubeMX).

### Design choices

Putting the markers in USER CODE blocks 0 and 1 means the HAL handler runs between them, so the measured duration includes all the callback work, which is what we want to time.

### How to test it alone

`S` shows non-zero runs for `tim6_isr` (about 100 per window) and `uart_isr` while you type.

---

## STM32F446RETX_FLASH.ld

| | |
|---|---|
| **Path** | `Mini_Project/STM32F446RETX_FLASH.ld` |
| **Owner** | Generated by CubeIDE; two edits by the team (Arfan: FLASH length; Abhay: `.noinit`) |
| **What it is** | The linker script: tells the linker where flash and RAM are and where each section of the program goes. |
| **Why it matters** | Protects the black box from the program, and gives us RAM that survives a reset. |
| **What would break without it** | Without the FLASH cut, a large program could be placed in sectors 6-7 and then erased by the black box. Without `.noinit`, the crash record and the ring buffer would be zeroed at every boot, so trap and watchdog reports would be empty. |

### Walkthrough

```ld
MEMORY
{
  RAM    (xrw) : ORIGIN = 0x20000000, LENGTH = 128K
  FLASH  (rx)  : ORIGIN = 0x8000000,  LENGTH = 256K
}
```
RAM is the full 128 KB. FLASH is cut from 512K to **256K** (sectors 0-5): the linker will refuse to build a program that does not fit, instead of silently using sectors 6 and 7.

```ld
.noinit (NOLOAD) :
{
  . = ALIGN(4);
  *(.noinit)
  *(.noinit*)
  . = ALIGN(4);
} >RAM
```
A RAM section placed after `.bss`. `NOLOAD` means it has no initial content in the program file, and because it is neither `.data` (copied from flash at boot) nor `.bss` (zeroed at boot), the startup code does not touch it. Variables marked `__attribute__((section(".noinit")))` (`g_fm_noinit`, `g_fm_rb`) keep their values across a reset. After a power cut, RAM content is random, which is why `fm_capture_boot()` checks a magic number.

The rest is standard: `.isr_vector`, `.text`, `.rodata`, `.data` (`>RAM AT> FLASH`), `.bss`, a minimum heap of 0x200 and stack of 0x400 bytes for `main()` before the scheduler starts.

### Design choices

Cutting the FLASH region is a compile-time guarantee; the alternative (a runtime check of the program size) would only catch the problem after the damage.

### How to test it alone

After a build, `arm-none-eabi-nm Mini_Project.elf | grep g_fm_noinit` shows an address in RAM after `_ebss`; `arm-none-eabi-size` shows text + data below 256 KB.

---

# Numbers cheat sheet

| What | Value | Where |
|---|---|---|
| Core clock | 84 MHz (HSI 16 / 16 × 336 / 4) | `main.c` |
| Cycle counter wrap | 2^32 / 84 MHz ≈ 51 s | DWT |
| Window | 100 ms ≈ 8.4 M cycles | `FM_WINDOW_MS` |
| Components / stages | 16 max (13 used) / 8 max (5 used) | `fm_types.h`, `fm_common.h` |
| Event | 8 bytes | `event_t` |
| Ring buffer | 1024 slots (1023 usable), 8204 bytes, in `.noinit` | `ring_buffer.h` |
| Signature vector | 436 bytes | `sig_vector_t` |
| Published window | 516 bytes; sigout queue 4 deep | `fm_pub_t` |
| Baseline | 796 bytes in flash sector 6 (0x08040000) | `baseline_t` |
| Learner state | 940 bytes | `fm_learn_t` |
| Fault report | 164 bytes, 512 slots of 256 B in sector 7 (0x08060000) | `fault_report_t` |
| Crash record | 176 bytes in `.noinit` | `fm_noinit_t` |
| Learning | 100 windows = 10 s (min 20 via `L`) | `fm_monitor.h` |
| Warm-up / grace | 10 windows (1 s) / 3 windows (300 ms) | `fm_monitor.h` |
| Debounce | 2 windows, or 1 if margin ≥ 3 | `fm_detect.c` |
| Default sensitivity | k = 4 (range 1..20 via `K`) | `fm_detect.c` |
| Statistical threshold | T = 2 (2k)² = 128 at k = 4 | `fm_detect.c` |
| Periodic if | max gap seen < 5 windows (0.5 s) | `fm_detect_is_periodic` |
| Escalation window | 10 s | `FM_ESCALATE_MS` |
| IWDG | LSI 32 kHz / 64 = 500 Hz, reload 2000 → 4 s; fed every 250 ms if monitor checked in within 1.5 s | `fm_watchdog` |
| MPU regions | 0 null guard (1 KB at 0x0), 1 read-only config (32 B), 2-7 stack guards (32 B each) | `fm_capture.c` |
| TIM2 | 84 MHz / 840 = 100 kHz, ARR 999 → 100 Hz PWM + ADC trigger | `app_periph.c` |
| TIM6 | 84 MHz / 8400 / 10 = 1 kHz (storm: /840 → 10 kHz) | `main.c`, `app_periph.c` |
| ADC | 21 MHz; PA4 84 cycles, VREFINT 480 cycles; ~28 µs per scan | `app_periph.c` |
| Task priorities | wdg 48, monitor 40, ctrl 32, report 24, shell 8, sigout 8 | various |
| Task stacks (words) | monitor 1024, ctrl 384, report 512, shell 512, sigout 512, wdg 192 | various |
| NVIC priorities | TIM6 5, USART2 5, DMA2 S0 6, TIM1 (HAL tick) 15 | MSP, `app_periph.c` |
| FreeRTOS heap | 32 KB (heap_4) | `FreeRTOSConfig.h` |
| UART | 115200 8-N-1, ≈ 11.5 KB/s; RX queue 96 B; TX line 256 B; timeout 500 ms | `app_config.h` |
| Plant | T_amb 25 °C, gain 70 °C, tau 1.5 s, step 10 ms; DAC 0..4095 = 0..100 °C | `controller.c` |
| PID | kp 0.05, ki 0.04, kd 0 (unused), output 0..1 | `controller.c` |

---

# Glossary

- **ADC** (analog-to-digital converter): measures a voltage and returns a number (0..4095 here, 12 bits).
- **AP field**: MPU "access permission" bits of a region (000 = no access, 110 = read-only).
- **Baseline**: the learned description of normal behaviour (mean, sigma, max per feature).
- **BASEPRI / PRIMASK**: CPU registers that mask interrupts. PRIMASK masks all of them; BASEPRI masks those at or below a priority level (FreeRTOS uses BASEPRI).
- **Black box**: our flash fault log, by analogy with an aircraft flight recorder.
- **CFSR / HFSR / MMFAR / BFAR**: Cortex-M fault status and fault address registers.
- **Component**: anything that can be blamed: a task, an interrupt or a peripheral, identified by a number 0..15.
- **Context switch**: FreeRTOS saving one task's registers and restoring another's (done in the PendSV exception).
- **CRC-32**: a 32-bit checksum; any small change in the data changes it.
- **DAC** (digital-to-analog converter): turns a number into a voltage on a pin.
- **Debounce**: requiring a condition to last a little while before acting on it.
- **DMA** (direct memory access): hardware that copies data between a peripheral and RAM without the CPU.
- **DWT CYCCNT**: a 32-bit counter of CPU clock cycles in the debug unit; our timestamp.
- **EXC_RETURN**: the special value in LR during an exception; tells which stack and frame type to return to.
- **Flash sector**: the smallest block of flash that can be erased at once.
- **FPU**: floating-point unit (hardware for `float` maths).
- **HAL**: ST's Hardware Abstraction Layer library.
- **Hard evidence**: a yes/no fact (wrong register value, never-seen transition, error flag), as opposed to a timing measurement.
- **ISR** (interrupt service routine): a function the hardware calls when an event happens (a timer expires, a byte arrives).
- **IWDG**: independent watchdog: a hardware timer on its own clock that resets the chip unless it is reloaded in time.
- **Liveness**: "is it still alive", i.e. did it run recently enough.
- **Localization**: deciding which component caused a fault.
- **LSI**: the low-speed internal 32 kHz oscillator that clocks the IWDG.
- **Margin**: measured value ÷ threshold; above 1 means a violation, bigger means worse.
- **MPU** (memory protection unit): hardware that traps accesses to address ranges we mark as forbidden or read-only.
- **Mutex**: a lock that only one task can hold at a time.
- **`.noinit`**: a RAM section the startup code does not clear, so it survives a reset (not a power cut).
- **PC** (program counter): the address of the instruction being executed.
- **PendSV**: the low-priority exception FreeRTOS uses to switch tasks.
- **Periodic / event-driven**: runs at a regular rate vs runs only when something happens.
- **PLL**: phase-locked loop: multiplies a clock to a higher frequency.
- **Queue / semaphore / task notification**: FreeRTOS ways to pass data or "wake up" signals between tasks and ISRs.
- **Ring buffer**: a fixed-size array used in a circle; the newest data overwrites the oldest.
- **Sigma (σ)**: standard deviation, how much a value usually varies around its mean.
- **Signature (runtime execution signature)**: our `sig_vector_t`: the numbers that describe how the firmware ran in one 100 ms window.
- **Stage**: one step of the application pipeline (A acquire, B filter, C PID, D actuate, E report).
- **TCB** (task control block): FreeRTOS's record for one task; its first field is the saved stack pointer.
- **TRGO**: a timer's "trigger output", used here to start ADC conversions.
- **Victim suppression**: not blaming a low-priority task for going silent when a higher-priority one is hogging the CPU.
- **Welford's algorithm**: a numerically stable one-pass way to compute mean and variance.
- **Window**: the 100 ms period over which one signature is collected.
- **z-score**: (value − mean) / sigma: how many standard deviations away from normal.

# DESIGN — what we built, why this way, how it works, and what else we could have done

## 0. The question we were given, in one paragraph

Firmware fails in ways that are hard to see from outside: a task stops running,
an interrupt fires too often, a stack grows into other memory, a register gets the
wrong value, a message arrives corrupted. The problem statement asks for a system
that **watches the firmware run** (execution sequence, ISR timing, task time, stack,
peripheral status, watchdog, memory access, communication), **notices** when the
pattern stops looking normal, **names the component** responsible with a
**confidence**, keeps the **execution history**, and **recovers** — and then asks us
to **measure** how well that works (detection rate, localization accuracy, false
positives and negatives, overhead, latency) and to compare a **rule-based** approach
with a **statistical / machine-learning** one.

## 1. What we built (the WHAT)

Two things that run together on one STM32F446RE:

```
┌──────────────────────────── the APPLICATION (the patient) ───────────────────────────┐
│  closed-loop temperature controller, real peripherals, no external parts             │
│  TIM2 ─PWM─> LED (heater)      TIM2 ─trigger─> ADC1 ─DMA─> RAM ─ISR─> ctrl task       │
│  ctrl: A acquire → B filter → C PID → D actuate ──> plant model ──> DAC ──> PA4 ─┐    │
│  report: E telemetry (checksummed)                       ADC reads PA4 <─────────┘    │
│  inputs: button B1, UART commands                                                     │
└───────────────────────────────────────────────────────────────────────────────────────┘
          ▲ hooks (context switch, ISR enter/exit, stage marks, error flags)
┌──────────────────────────── the FRAMEWORK (the doctor) ──────────────────────────────┐
│ CAPTURE (Abhay)     events → ring buffer (history) + per-window counters → signature   │
│ DETECT  (Arfan)     learn baseline on chip → rule-based / statistical localizer        │
│                     → fault report (flash black box) → recovery ladder → watchdog     │
│ INJECT  (Abhishek)  real faults on command + ground truth, examiner tools, PC tools   │
└───────────────────────────────────────────────────────────────────────────────────────┘
```

**Framework** here means: reusable code that does not know what the application
does. It knows components only as numbers 0..15 that the application registers
(`fm_register_task/_isr/_periph/_stage`). Swap the controller for a motor drive or a
data logger and the framework is unchanged; only `fm_common.h` and the registration
calls differ. That is the difference between a framework and a demo.

## 2. The four layers (and why they make it portable)

| layer | files | depends on | changes when you port to … |
|---|---|---|---|
| core logic (pure C) | fm_learn.c, fm_detect.c, fault_threshold.c | nothing but `fm_types.h` | nothing; runs on a PC too (unit tests) |
| RTOS adapter | fm_trace.c hooks, fm_monitor.c, fm_watchdog.c task | FreeRTOS / CMSIS-RTOS v2 | another RTOS (Zephyr, ThreadX): rewrite the hook glue |
| Cortex-M architecture | fm_capture.c: MPU, CFSR/MMFAR/BFAR, EXC_RETURN, DWT | ARMv7-M | M3/M4/M7: nothing. M33: new MPU register format. M0/M0+: no DWT cycle counter, no CFSR, so use a timer and HardFault only |
| chip port | RCC_CSR reset flags, IWDG, flash sectors, ADC/DAC/TIM registers | STM32F446 | another STM32: flash sector numbers, IWDG is identical, peripherals differ |

## 3. Why each decision (the WHY) — with the alternative we rejected

### 3.1 Watch "signatures per window", not individual events
* **What:** every 100 ms the hooks' counters become one `sig_vector_t`: per component
  runs, CPU cycles, longest single run, longest gap, free stack; stage-transition
  counts; per-peripheral register CRC and error counts.
* **Why:** an event stream of thousands of events per second cannot be judged one event at a time in
  real time on a 84 MHz MCU, and storing it all is impossible (we have 128 KB RAM).
  A window summary is a fixed ~500-byte vector, cheap to compute inside the hooks
  (O(1) per event), and is exactly the feature vector a statistical or ML model wants.
* **Alternative:** stream raw events to a PC (what SystemView/Tracealyzer do) and
  analyse there. Rejected for the main path because a deployed device has no PC
  attached; we still keep the raw stream as a ring buffer for the history.

### 3.2 Learn "normal" on the chip
* **What:** after the first boot the monitor watches 100 normal windows (10 s),
  computes mean / standard deviation / maximum per feature with Welford's algorithm,
  records which stage transitions happen and the CRC of every peripheral's
  configuration, and stores this baseline in flash sector 6.
* **Why:** thresholds depend on the clock, compiler options and the application;
  hard-coding them (as in Phase 1) breaks when anything changes (we found exactly
  that: Phase 1 assumed 168 MHz, the board runs at 84 MHz). Learning on the device
  is how a deployed product would be commissioned.
* **Alternative:** learn on a PC from recorded CSVs and compile the numbers in.
  Works, but needs a re-flash after every change.

### 3.3 Two localizers behind one interface
* **What:** `localizer_t { name, localize(sig, baseline, result) }`.
  `fm_localizer_rule` and `fm_localizer_stat` both run every window; `M 0/1` picks
  which one acts, the other is printed as `!SHADOW`.
* **Why:** the extension asks to compare them later "without rework". With one
  interface the comparison is a command, not a rewrite, and a third localizer (a
  decision tree exported by `tools/fm_ml.py --export-c`) is one more function.
* **Rule-based** encodes causal knowledge: hard evidence first (a peripheral CRC
  mismatch explains slow tasks, not the other way round), victim suppression (a
  task hogging the CPU starves lower-priority tasks — those are victims), debounce
  (2 windows unless the violation is ≥ 3× the threshold). Explainable: every report
  has a `rules_hit` bitmask.
* **Statistical** is the classic anomaly detector: per component the sum of squared
  z-scores of its features (a diagonal Mahalanobis distance), pick the largest above a
  threshold. No knowledge of priorities, so in cascades it may blame a victim —
  that is the interesting result of the comparison.

### 3.4 Capture by hooks, not by polling
* Task timing: FreeRTOS calls `traceTASK_SWITCHED_IN/OUT` at every context switch;
  we put `fm_hook_task_in/out` there and identify the task by a tag
  (`uxTaskNumber` = component + 1). Exact, and costs on the order of a microsecond per switch.
* ISR timing: two macros at the top and bottom of each handler.
* Time: the DWT cycle counter (1 count = 11.9 ns at 84 MHz).
* **Alternative:** sample the running task from a timer interrupt (statistical
  profiling). Cheaper but misses short events and cannot measure gaps exactly.

### 3.5 Memory faults: let the hardware catch them (MPU)
* Region 0: 1 KB no-access at address 0 → any NULL-pointer access traps.
* Region 1: the controller's 32-byte configuration read-only → a stray write traps.
* Regions 2–7: a 32-byte no-access guard at the bottom of each monitored task's
  stack → the first overflowing write traps, before it corrupts anything else.
* The fault handlers call `fm_trap()`: it records type, component running, PC, CFSR,
  fault address and the last 16 events in a RAM area the startup code does not clear
  (`.noinit`), then resets. After the reboot the monitor files the report.
* **Why reset:** after a memory fault the system state cannot be trusted; restarting
  is the safe recovery, and recording before resetting keeps the evidence.
* **Alternative:** FreeRTOS's own stack check only (we keep it as backup): it checks
  at context switches, so it detects an overflow after the damage is done.

### 3.6 The watchdog watches the watcher
* IWDG (independent 32 kHz oscillator, 4 s). A Realtime-priority task feeds it only
  if the monitor checked in within 1.5 s. So: monitor alive ⇒ fed; interrupts off,
  hung ISR or dead monitor ⇒ reset ⇒ the boot code reads the IWDG flag and blames the
  last running component (kept in `.noinit`).
* **Alternative:** feed the watchdog from a timer interrupt — would keep feeding even
  when every task is dead; useless (see Ganssle, "Great watchdog timers").

### 3.7 Store faults where they survive everything
* Flash sector 7: an append-only log of 164-byte reports in 256-byte slots, each with
  a CRC-32, sequence number and reset cause; sector 6: the baseline. The linker
  script stops the firmware at 256 KB so code can never land there.
* Flash erase stalls the CPU for 1–2 s, so it happens only at moments the monitor
  chooses (learn commit, clear, log full) and the monitor ignores the windows around it.
* **Alternatives:** backup SRAM (needs a battery on VBAT, lost on power-off without
  it), external EEPROM/SD (extra parts), RAM only (lost at reset).

### 3.8 Recovery ladder
`restart the task / re-init the peripheral` → (same component again within 10 s)
`also re-init all peripherals` → (again) `system reset`. Peripheral re-init pulses the
peripheral's reset line in RCC and reconfigures it, so even registers we never write
come back to known values.

### 3.9 The application: why a process controller
It is real-time (10 ms loop, hard deadline), uses every peripheral class the problem
statement names (timer, ADC, DMA, DAC, UART, GPIO interrupt), has an execution
pipeline A→B→C→D→E that matches the problem statement's example literally, and it is
**observable**: a fault visibly disturbs the LED and the telemetry. The plant is
simulated inside the chip but measured through the real ADC, so the controller sees
real noise and real failure modes. No external components needed.

### 3.10 Monitor priority
The monitor runs above every application task. If it ran below, a stuck
high-priority task would starve it and nothing would ever be reported. Demo 17 in the
GUIDEBOOK shows what happens if you break this rule.

## 4. How it works, end to end (the HOW)

1. **Boot:** `fm_capture_boot()` reads why we reset; `fm_trace_init()` and
   `fm_mpu_init()`; drivers; the app registers its tasks, ISRs, peripherals and
   stages; the shell; the monitor and watchdog.
2. **Every event:** a hook writes 8 bytes to the ring buffer and updates the window
   counters (both inside a few-instruction critical section).
3. **Every 100 ms (monitor):** close the window → add free stack → check in with the
   watchdog → learning: add to Welford accumulators; monitoring: run both localizers.
4. **Fault:** build `fault_report_t` (type, component, confidence, action, rules,
   PC where a stuck task is, reset cause, 16 history events, CRC) → flash → print
   `!FAULT` and `!HIST` → recover → ignore 3 windows → continue.
5. **Trap / watchdog:** record in `.noinit` → reset → step 4 happens at boot.

## 5. How ours differs from what exists

| tool / technique | what it is | what it gives | what it does not do (that we do) |
|---|---|---|---|
| SEGGER SystemView, Percepio Tracealyzer | RTOS event recorders streaming to a PC | beautiful timelines for a developer at a desk | no on-device judgement, needs a debugger/PC, no localization or recovery |
| Percepio DevAlert, Memfault | cloud crash/fault reporting for fleets | core dumps, crash analytics across devices | rely on a connection and server-side analysis; detect crashes, not slow drifts in timing; commercial |
| ETM/ITM/SWO hardware trace | CPU-level instruction/data trace | complete truth at the instruction level | needs a trace probe (and ETM pins the Nucleo doesn't break out); offline only |
| Watchdog alone | reset when not fed | recovery from total hangs | no idea what failed; no partial faults |
| FreeRTOS stack check / malloc-failed hooks | kernel self-checks | catches some stack overflows late | one fault class, no timing, no localization |
| Academic anomaly detection on timing | ML models on traces | high accuracy offline | usually offline/PC-side; rarely localize + recover on a small MCU |
| **This project** | on-chip framework | learns its own baseline, detects 8 parameter classes, names a component with a confidence, keeps history in flash, recovers, measures itself, compares rule vs statistical | not a replacement for a trace probe during development; thresholds are only as good as the learning period |

## 6. Problem statement coverage

See README §5 (item → file) and GUIDEBOOK §4 (item → demo). Metrics: GUIDEBOOK §5.

## 7. Known limits (say these before the examiner does)

* The baseline must be learned during representative normal operation; behaviour
  never seen while learning (e.g. a rarely used command that prints a lot) can look
  abnormal. Remedy: longer learning (`L 300`), sensitivity `K`.
* One fault at a time is injected, so the evaluation measures single faults.
* Window = 100 ms, so detection latency is typically 100–300 ms (timing faults) and
  ~0 ms + reboot time for traps; the watchdog takes up to 4 s by design.
* A task's MPU guard covers 32 bytes: a single function with a stack frame larger
  than that could jump over it (FreeRTOS's own check is the backup).
* Confidence is a calibrated heuristic (rule class + margin), not a probability.
* It has not been run on hardware at the time of writing (see GUIDEBOOK §6).

## 8. Other ways we could have built the framework

1. **PC-side analysis of streamed traces** — easier ML, no on-chip learning, but
   needs a PC permanently. Good for development, not deployment.
2. **Instrument every function** (`-finstrument-functions` entry/exit hooks) —
   gives a full call trace and true function-level sequence, but costs a lot of CPU
   and RAM; we instrument stages instead.
3. **Hardware performance counters only** (DWT CPI, exception counters) — tiny
   overhead, but only CPU-level symptoms, nothing about which task.
4. **Heartbeat-only supervision** (each task checks in, a supervisor checks
   deadlines) — simple and common in industry (it is part of what we do), but it
   cannot tell overrun from starvation or find peripheral/memory/comm faults.
5. **Model-based** (a state machine of allowed sequences per task) — precise for
   sequences; our transition matrix is the learned, lightweight version of that.

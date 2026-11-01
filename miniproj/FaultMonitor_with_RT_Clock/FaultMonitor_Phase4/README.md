# Fault Monitor — complete project (Part 1)


> **Phase 3 / MVP-2 entry point:** For the phase-scoped build and the required stuck-task + stack-overflow tests, start with [`README_PHASE3.md`](README_PHASE3.md), then follow [`docs/07_PHASE3_TEST_AND_DEBUG.md`](docs/07_PHASE3_TEST_AND_DEBUG.md) and [`docs/08_INPUTS_AND_EXPECTED_BEHAVIOUR.md`](docs/08_INPUTS_AND_EXPECTED_BEHAVIOUR.md). The Phase 3 build restricts normal UART fault injection to IDs 1 and 2.

**Autonomous Firmware Fault Localization Using Runtime Execution Signatures**
STM32 Nucleo-F446RE · Cortex-M4 · FreeRTOS (CMSIS-RTOS v2) · STM32CubeIDE
Team: **Abhay** (capture), **Arfan** (detection), **Abhishek** (application, injection, tools)

A real-time process controller runs on the board. A framework running beside it
watches how the firmware executes (task timing, ISR timing, stack, execution
sequence, peripheral registers, UART errors, watchdog, memory faults), learns what
"normal" looks like, and when something goes wrong it says **what** failed,
**which component** is to blame, **how sure** it is, shows the **execution history**,
stores a report in **flash**, and **recovers** — with no PC attached.

> Status (honest): all three builds compile with zero warnings (monitor ON 98.3 KB flash,
> capture-only 64.9 KB, OFF 54.3 KB) and the detection logic passes its PC unit tests.
> It has **not** been run on a board yet, because none was available while writing it.
> Chapter 6 of the GUIDEBOOK is the first-power-on checklist.
>
> Fixed on 19 Oct after a full code review: the `T` and `Y` commands were silently
> unregistered; a task restarted while printing could lock the console (the UART lock
> is now a semaphore that recovery hands back); a stored baseline from different
> components is now re-learned; `P` priority changes now reach victim suppression;
> the `E:` error count is filled; BusFault stacking errors count as stack overflow.

---

## 1. Flash it in 2 minutes

**Option A — no build (drag and drop):** plug the Nucleo in; a USB drive called
`NODE_F446RE` appears. Copy `prebuilt/FaultMonitor_monitor_on.bin` onto it. The
board flashes itself and restarts.

**Option B — STM32CubeIDE:** *File → Import → General → Existing Projects into
Workspace* → select the `Mini_Project` folder → *Project → Build* → *Run → Debug As
→ STM32 C/C++ Application*.

**Then:** open a serial terminal (PuTTY / Tera Term / CubeIDE console) on the
ST-LINK COM port, **115200 8-N-1**. Press the black RESET button. You should see:

```
=== Fault Monitor v2 | process controller | core 84 MHz | monitor ON ===
type H for commands
BOOT #1 reset cause: power-on (0x03)
black box: 0 stored reports; baseline: none -> will learn after warm-up
LEARN start: 100 windows (10 s) of normal operation -- please do not inject faults now
TEL,9,1000,50.0,31.2,100.0*hh      (frame, ms, setpoint, measured, heater %, checksum)
...
LEARN done: baseline from 100 windows saved to flash sector 6. Monitoring.
```

The green LED (LD2) brightness is the heater power. Press the blue button B1 to
change the setpoint (40 → 50 → 60 → 70 °C).

## 2. First demo (5 commands)

| type | what happens |
|---|---|
| `I` | lists the 10 injectable faults and what the monitor should conclude |
| `I 1 0` | stuck loop in the control task → `!FAULT type=EXEC_OVERRUN comp=ctrl(0) ...`, history, `!RECOVERED` |
| `I 10 0` | stage C repeats (A→B→C→C→C) → `!FAULT type=SEQUENCE comp=ctrl(0)` |
| `I 2 1` | stack overflow in the report task → MPU guard trap → reset → boot report `STACK_OVERFLOW comp=report(1)` |
| `R` | every stored fault report, read back from flash (they survive power-off) |

All commands: type `H`. Full walkthrough with expected output: `docs/01_GUIDEBOOK.md`.

## 3. Folder map

```
Mini_Project/                      the STM32CubeIDE project (import this)
  Core/Inc/fm_types.h              shared, frozen data formats (all three)
  Core/Inc/FreeRTOSConfig.h        + trace hook macros (USER CODE Defines)
  Core/Src/main.c                  init order (USER CODE blocks only)
  Core/Src/stm32f4xx_it.c          fault handlers -> fm_trap(), ISR hooks
  Core/Src/monitor/   ABHAY        capture layer
  Core/Src/detect/    ARFAN        learning, detection, black box, recovery, watchdog
  Core/Src/app/       ABHISHEK     the process-control application
  Core/Src/inject/    ABHISHEK     UART, shell, fault injection
  STM32F446RETX_FLASH.ld           firmware limited to 256 KB; .noinit section
prebuilt/                          ready .bin/.hex/.elf: monitor ON, CAPTURE ONLY, OFF
tools/              ABHISHEK/ARFAN PC scripts (Python 3 + pyserial)
docs/  01 guidebook · 02 design (why/what/how, vs market) · 03 file-by-file
       04 learning book (placement + viva) · 05 references (web + YouTube) · 06 team plan
```

## 4. Every source file, its owner and why it exists

| file | owner | what it does (one line) | why it matters |
|---|---|---|---|
| `Core/Inc/fm_types.h` | all | event record, signature vector, fault report, baseline, localizer interface, flash map, command letters | the contract: every module and the Python tools agree on these byte layouts |
| `monitor/fm_trace.h/.c` | Abhay | component registry, task-switch / ISR / stage hooks, per-window counters, 'D' dump | turns execution into numbers: the "runtime execution signature" |
| `monitor/fm_capture.h/.c` | Abhay | reset cause, crash record in .noinit RAM, fault traps, MPU (null guard, read-only config, stack guards), free-stack scan, stuck-task PC | the chip-specific layer: memory faults, stack overflow, watchdog evidence |
| `monitor/ring_buffer.h/.c` | Abhay | 1024-event flight recorder (8 KB), ISR-safe | the "execution history" attached to every report |
| `monitor/fm_sigout.h/.c` | Abhay | 'S' status table + overhead, 'V' CSV stream (low-priority printer) | makes the signature visible; feeds the ML dataset |
| `detect/fm_learn.h/.c` | Arfan | Welford mean / sigma / max per feature, allowed transitions, register CRCs | on-chip learning of "normal" (no PC needed) |
| `detect/fm_detect.h/.c` | Arfan | rule-based and statistical localizers behind one interface | decides fault type, suspect component, confidence |
| `detect/fault_threshold.h/.c` | Arfan | band / liveness threshold helpers (Phase 1, reused) | small, unit-tested math |
| `detect/fm_blackbox.h/.c` | Arfan | fault log in flash sector 7, baseline in sector 6, CRC-protected | answers "where and how are faults stored" |
| `detect/fm_recover.h/.c` | Arfan | recovery ladder: restart task → re-init peripherals → system reset | the "recovery action" output |
| `detect/fm_watchdog.h/.c` | Arfan | IWDG (4 s) fed only while the monitor is alive | catches what software cannot: interrupts off, hung ISR |
| `detect/fm_monitor.h/.c` | Arfan | the 100 ms monitor task: learn / detect / report / recover; boot report; L B R C M K | the brain that ties everything together |
| `app/app_periph.h/.c` | Abhishek | TIM2 PWM + ADC trigger, ADC1 + DMA, DAC, register checks, re-init functions | real peripherals with real failure modes |
| `app/controller.h/.c` | Abhishek | control task (stages A–D, PID, plant model), report task (E, telemetry), button, T/Y | the live real-time application being monitored |
| `app/fm_common.h` | Abhishek | component, stage and fault IDs of this application | the only place the framework learns app names |
| `app/app_config.h` | Abhishek | priorities, stacks, periods, UART settings | one place to tune |
| `app/idle_stats.h/.c` | Abhishek | idle-hook counter | CPU overhead measurement |
| `inject/fm_uart.h/.c` | Abhishek | interrupt-driven UART, comm-error reporting, re-init | console + communication-error source |
| `inject/uart_shell.h/.c` | Abhishek | command shell, `$...*hh` checksummed frames, help | the operator/examiner interface |
| `inject/fault_inject.h/.c` | Abhishek | 10 real faults, ground-truth log, examiner W/P commands | how we prove it works |
| `tools/fm_dump.py` | Abhishek | dump trace, record signature stream, status | data capture on the PC |
| `tools/fm_campaign.py` | Abhishek | automated injection campaign → CSV | evaluation data |
| `tools/fm_metrics.py` | Abhishek | detection rate, localization accuracy, FP/FN, latency, overhead | the evaluation table |
| `tools/fm_ml.py` | Arfan | Isolation Forest + decision tree vs on-chip localizers | the ML comparison (extension) |
| `tools/fm_report.py` | Arfan | reads the black box, PC → file:line with addr2line | pinpoints the faulty source line |

Every source file begins with a comment block saying what it does and why;
`docs/03_FILE_BY_FILE.md` walks through each one block by block in plain language.

## 5. Problem statement → where it is handled

| asked for | observed by | file |
|---|---|---|
| function execution sequence | `FM_STAGE()` stage transitions (A→B→C→D, E) | fm_trace.c, controller.c |
| ISR timing | ISR enter/exit hooks: rate, duration | fm_trace.c, stm32f4xx_it.c, app_periph.c |
| task execution time | context-switch hooks + DWT cycle counter | fm_trace.c, FreeRTOSConfig.h |
| stack usage | free-stack scan + MPU guard + FreeRTOS check | fm_capture.c |
| peripheral status | config-register CRC + status flags (ADC OVR, DMA TE, VREFINT) | fm_trace.c, app_periph.c, controller.c |
| watchdog events | IWDG reset cause + noinit record | fm_watchdog.c, fm_capture.c, fm_monitor.c |
| memory-access anomalies | MPU null guard, read-only region, CFSR/MMFAR/BFAR | fm_capture.c, stm32f4xx_it.c |
| communication errors | UART ORE/FE/NE, frame checksum | fm_uart.c, uart_shell.c |
| fault detected / suspected component / confidence / history / recovery | `!FAULT` line + flash report | fm_monitor.c, fm_detect.c, fm_blackbox.c, fm_recover.c |
| detection rate, localization accuracy, FP, FN, latency, CPU/RAM/trace overhead | campaign + metrics tools, 'S' OVH line | tools/ |
| rule-based vs statistical / ML | `M 0` / `M 1` on chip (+ shadow mode), fm_ml.py offline | fm_detect.c, tools/fm_ml.py |

## 6. Build switches (three builds, all in `prebuilt/`)

Set in Project Properties → C/C++ Build → Settings → MCU GCC Compiler →
Preprocessor → Defined symbols.

| build | symbols | what runs | use |
|---|---|---|---|
| `FaultMonitor_monitor_on` | (none, default) | application + full framework | the product, the demo |
| `FaultMonitor_monitor_capture_only` | `FM_ENABLE_DETECT=0` | application + capture ('S' 'V' 'D', traps) | integration step 2; capture overhead alone |
| `FaultMonitor_monitor_off` | `FM_ENABLE_MONITOR=0` | application, shell, injector only | baseline for overhead; Abhishek's standalone test |

CPU overhead of capture = OFF vs CAPTURE ONLY; of detection = CAPTURE ONLY vs ON.

# GUIDEBOOK — build, run, demonstrate, evaluate

Read this with the board in front of you. Every command, what you should see,
what it proves, and what to do when it doesn't happen.

---

## 1. What is running on the board

```
 priority      task / ISR        what it does                               period
 ─────────────────────────────────────────────────────────────────────────────────
 Realtime 48   wdg               feeds the IWDG if the monitor checked in     250 ms
 High     40   monitor           closes a window, learns / detects, recovers  100 ms
 AboveN.  32   ctrl              A acquire → B filter → C PID → D actuate      10 ms (ADC)
 Normal   24   report            E telemetry frame (printed every 10th)       100 ms
 Low       8   shell             commands; also wakes every 100 ms            event
 Low       8   sigout            prints the 'V' stream                         event
 ISR prio 5    TIM6              1 kHz application tick, injector             1 ms
 ISR prio 5    USART2            one interrupt per byte in/out                 event
 ISR prio 6    DMA2 Stream0      "ADC scan done" → wakes ctrl                  10 ms
 ISR prio 15   EXTI15_10         user button B1                               event
```

The process: TIM2 runs at 100 Hz. Each period it (1) sets the PWM duty on PA5
(LED LD2 = heater power) and (2) triggers the ADC, which converts PA4 and the
internal reference. DMA moves both results to RAM and interrupts; the control
task wakes, filters the reading, runs the PID, sets the PWM, advances the
plant model (a heater in a room, tau 1.5 s) and writes the plant temperature to
the DAC on PA4 — which the ADC measures at the next period. A closed analog loop,
entirely inside the chip.

## 2. The serial console

ST-LINK virtual COM port, **115200 8-N-1**, no flow control. Windows: Device
Manager → Ports → "STMicroelectronics STLink Virtual COM Port (COMx)". Linux:
`/dev/ttyACM0`. The shell echoes what you type; backspace works; finish with Enter.

Lines starting with `!` are the monitor talking:

| prefix | meaning |
|---|---|
| `!FAULT` | a fault was detected: seq, type, comp(id), conf %, localizer, action, rules bitmask, pc, uptime, latency (if injected) |
| `!HIST` | the execution history attached to that report: 16 events, µs before detection |
| `!SHADOW` | what the other localizer concluded for the same window |
| `!GT` | ground truth: what was really injected, and whether localization was right |
| `!RECOVERED` | recovery done (action and ladder level) |
| `!TRAP` / `!WATCHDOG` | after a reset: the crash record that survived in RAM |

## 3. Commands

| cmd | owner | example | what you see |
|---|---|---|---|
| `H` | Abhishek | `H` | the command list |
| `T` | Abhishek | `T 65` | `OK setpoint 65 C`; telemetry PV moves to 65 within ~5 s |
| `Y` | Abhishek | `Y 0` | telemetry off (cleaner screen for demos) |
| `S` | Abhay | `S` | monitor mode, faults, recoveries; one row per component (runs, CPU µs, longest run, longest gap, free stack, peripheral CRC); `IDLE,` and `OVH,` lines |
| `V` | Abhay | `V 1` / `V 0` | one `SIG,...` CSV line per 100 ms window |
| `D` | Abhay | `D` | the trace ring buffer as CSV (`idx,cycle_ts,comp,type,extra`), ~1000 lines |
| `L` | Arfan | `L` or `L 300` | re-learn from 100 (or 300) windows; `LEARN done` when saved |
| `B` | Arfan | `B` | learned baseline per component; periodic / event-driven; allowed transitions `A>B B>C C>D D>A E>E` |
| `R` | Arfan | `R`, `R 3` | all stored reports (CSV) / report #3 with its history |
| `C` | Arfan | `C` | erases the fault log (sector 7) |
| `M` | Arfan | `M 1` | statistical localizer active (rule-based in shadow); `M 0` back |
| `K` | Arfan | `K 5` | sensitivity: higher = fewer alarms, slower detection (default 4) |
| `I` | Abhishek | `I`, `I 1 0 500` | list of faults; arm fault 1 on component 0 in 500 ms |
| `X` | Abhishek | `X` | cancel a pending injection |
| `G` | Abhishek | `G` | ground-truth log CSV |
| `W` | Abhishek | `W 40000028 53` | examiner: write any word (here TIM2 PSC) |
| `P` | Abhishek | `P 1 40` | examiner: make the report task priority 40 |
| `$...*hh` | Abhishek | `$T 50*71` | framed command; wrong checksum → comm error |

Component numbers: 0 ctrl, 1 report, 2 shell, 3 monitor, 4 wdg, 5 adc_isr,
6 tim6_isr, 7 uart_isr, 8 btn_isr, 9 ADC1, 10 TIM2, 11 DAC1, 12 USART2.

## 4. Demo test cases (the evaluation script)

Do these in order after `LEARN done`. Turn telemetry off first (`Y 0`).
Each row says which problem-statement item it proves.

| # | type | target | expected | proves |
|---|---|---|---|---|
| 1 | `S` | — | every component listed with live numbers; `OVH` line | capture of all parameters, overhead |
| 2 | `I 1 0` | stuck loop, ctrl | `EXEC_OVERRUN comp=ctrl(0)` conf ≥ 80, `pc=` inside `do_stuck_loop`, `!RECOVERED ... RESTART_TASK`, telemetry continues | task execution time, localization, recovery |
| 3 | `I 3 1` | missed deadline, report | `LIVENESS comp=report(1)` | task timing / deadlines |
| 4 | `I 10 0` | A→B→C→C→C | `SEQUENCE comp=ctrl(0)` | function execution sequence (the problem statement's own example) |
| 5 | `I 4 6` | ISR storm | `ISR_RATE comp=tim6_isr(6)`, `REINIT_PERIPH` | ISR timing |
| 6 | `I 7 9` | ADC trigger scrambled | `PERIPH comp=ADC1(9)`, ADC reset and reconfigured, PV recovers | peripheral status |
| 7 | `I 7 11` | DAC switched off | `PERIPH comp=DAC1(11)` | peripheral status |
| 8 | `I 8 12` | corrupted frame | `COMM comp=USART2(12)` | communication errors |
| 9 | `I 2 1` | stack overflow | reset → `!TRAP` → `STACK_OVERFLOW comp=report(1)` conf 99 | stack usage |
| 10 | `I 5 0` | write to read-only config | reset → `MEM_ACCESS comp=ctrl(0)`, addr = config | memory-access anomaly |
| 11 | `I 6 2` | NULL pointer write | reset → `MEM_ACCESS comp=shell(2)`, addr 0x10 | memory-access anomaly |
| 12 | `I 9 0` | interrupts off + spin | ~4 s silence → reset → `!WATCHDOG` → `WATCHDOG comp=ctrl(0)` | watchdog events |
| 13 | `R` | — | all of the above, read back from flash | storage |
| 14 | power off/on, `R` | — | still there | non-volatile black box |
| 15 | `M 1`, repeat 2–6 | — | statistical verdicts; `!SHADOW` shows the rule-based one | extension: rule vs statistical |
| 16 | examiner: `W 40000028 53` | — | TIM2 prescaler changed → `PERIPH comp=TIM2(10)` | unscripted fault |
| 17 | examiner: `P 1 48` then `I 1 1` | — | report now outranks the monitor: watch what happens (watchdog) | the reason the monitor must have high priority |

Faults that reset the board (9–12) print their report during the next boot,
then the monitor resumes with the stored baseline (no re-learning).

## 5. Running the evaluation (numbers for the report)

```
pip install pyserial pandas scikit-learn
cd tools
python fm_campaign.py --port COM5 --reps 5 --normal 300 --out rule.csv
python fm_campaign.py --port COM5 --reps 5 --normal 300 --localizer stat --out stat.csv
python fm_metrics.py rule.csv
python fm_metrics.py stat.csv
```
Overhead: flash `prebuilt/FaultMonitor_monitor_off.bin`, wait 60 s, `S`, save the
text as `off.txt`; same with `_on.bin` → `on.txt`;
`python fm_metrics.py rule.csv --overhead on.txt off.txt`.
ML: in one terminal `python fm_dump.py --port COM5 stream -t 900 --out stream.csv`
is not possible at the same time as the campaign on one port, so record the
stream while injecting by hand (`I ...` typed every ~10 s), then
`python fm_ml.py stream.csv --export-c fm_tree.c`.
Black box with source lines: `python fm_report.py --port COM5 --elf ../prebuilt/FaultMonitor_monitor_on.elf`.

## 6. First power-on checklist (not yet done on hardware)

1. Banner appears → UART, clocks, scheduler OK. Nothing at all → wrong COM port
   or baud; press RESET.
2. `TEL` lines with PV rising towards 50 → ADC, DMA, DAC, TIM2, ctrl task OK.
   PV stuck at 0 or 100 → check PA4 isn't wired to anything on the headers;
   `S` shows `adc_isr runs 10`? If 0, the ADC is not being triggered.
3. LED LD2 brightness follows heater power → PA5 in PWM mode.
4. `LEARN done` after ~11 s → monitor, flash write OK. `NOT saved` → flash
   write failed: check `STM32F446RETX_FLASH.ld` FLASH LENGTH is 256K.
5. Leave it 2 minutes: no `!FAULT` lines → no false alarms. If one component
   keeps raising false alarms, `B` and `S` show which feature; re-learn
   longer (`L 300`) or raise `K`.
6. Run demo 2. If it says `DIFFERENT COMPONENT`, save `R <seq>` output — the
   history shows why.

## 7. Troubleshooting

| symptom | likely cause | fix |
|---|---|---|
| resets every ~4 s, `WATCHDOG comp=monitor` | monitor blocked (e.g. UART stuck) | `R`, look at history; reflash; report it |
| `!FAULT` right after learning | learned while something unusual was happening | `L` again with nothing typed |
| `ERR unknown command` for `$` frames | wrong checksum: XOR of chars between `$` and `*` | tools/fm_serial.py `framed()` computes it |
| no `TEL` lines | `Y 0` was sent | `Y 1` |
| baseline lost after flashing | IDE set to full-chip erase | CubeIDE debug config → Debugger → "Erase: sector"; or just let it re-learn |
| board stuck after a debugger breakpoint | IWDG frozen in debug: fine; but after detaching it runs again | normal |
| `HardFault` loops with the monitor OFF build | expected: without the framework, faults are not caught | use the ON build |

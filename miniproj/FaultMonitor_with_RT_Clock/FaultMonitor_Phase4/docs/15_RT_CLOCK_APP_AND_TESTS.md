# Real-time sample clock add-on: wiring, commands and full test sequence

## What it is
`Core/Src/app/rt_clock.c` (one new file; no existing file changed). A real-time task
(priority BelowNormal, every 10 ms) that keeps a clock from the sampling hardware:
every 10 ms ADC sample block delivered by the DMA = 10 ms of clock time. It compares
the clock with the independent FreeRTOS SysTick time and counts its own missed deadlines.
A heartbeat LED on D12 blinks at 1 Hz while the system runs.

Faults are still detected and recovered in ~0.1-0.3 s by the monitor; the clock keeps
the EVIDENCE of what the fault did to the real-time application:
- **drift** = clock time - real time. Negative: samples were lost. Positive: samples came too fast.
- **missed** = 10 ms periods in which the task could not run (CPU taken by a faulty task).
Both stay visible after recovery until you type `Z R`.

It starts itself (GCC constructor, called by the startup code before main) and uses a
static stack, so it takes no FreeRTOS heap. It is not a monitored component: faults
cannot target it; it observes the damage.

## Wiring (board unplugged)
```
D12 (PA6) --[330 ohm]--> LED long leg (+)   LED short leg (-) --> GND
```
PA6 is unused by the project and outside the GPIOA bits the monitor checks (PA0, PA5).

## Build and flash
1. Unzip, File > Import > Existing Projects into Workspace > `Mini_Project`.
2. F5 (refresh), Project > Clean, Build, flash.
3. PuTTY: ST-LINK COM port, 115200 8-N-1, no flow control, session logging ON.

## Commands added
| Command | Effect |
|---|---|
| `Z` | Print clock status now |
| `Z R` | Resync: drift and missed back to 0 (do this before each trial) |
| `Z 0` / `Z 1` | Per-second CLK line off / on (CLK! event lines always print) |
Tip: `Y 0` turns the TEL line off so the CLK lines are easier to read.

## Output format
```
CLK 00:01:23 | ref 00:01:23 | drift +0 ms | missed 0       once per second
CLK! missed 14 deadline(s): task blocked ~150 ms             when it happens
CLK! drift -120 ms: sample blocks lost (sampling stopped)
CLK! drift +950 ms: sample blocks arriving too fast
```
Normal: drift between -10 and +10 ms (one block of jitter), missed 0, LED blinking.

---------------------------------------------------------------------------

# Test sequence

Rules: one fault at a time; `R` after each fault; `Z R` before the next; wait at least
11 s before injecting into the same component again (recovery escalation, by design).
Keep the sensor check off (`Q 0`, the default) for Parts 1-4.

## Part 1 - Boot (5 min)
| # | Action | Expected | Pass if |
|---|---|---|---|
| 1 | Reset the board | Banner `... sensor monitor ...`, `BOOT #n ...` | Boots |
| 2 | Wait 2 s | `rt_clock: real-time sample clock started ...`, `MONITOR running` | Both appear |
| 3 | Watch | `CLK ...` every second, LED on D12 blinks 1 Hz | drift within +-10 ms, missed 0 |
| 4 | `Y 0` | `OK telemetry off` | TEL lines stop, CLK lines remain |

## Part 2 - New baseline (the clock task adds a little CPU) (5 min)
| # | Command | Expected | Pass if |
|---|---|---|---|
| 5 | `C` | Black box cleared | |
| 6 | PA0 to GND, `L 100`, hands off | `LEARN start` ... ~10 s ... `LEARN done` | No !FAULT |
| 7 | `B` | Baseline table, transitions A>B B>C C>D D>A | Present |
| 8 | Wait 60 s | Only CLK lines | No !FAULT, drift +-10, missed 0 (no false alarms) |
| 9 | `S`, `I`, `G`, `R` | Status, fault catalogue, empty logs | Save as reference |

## Part 3 - Application input (no fault expected) (3 min)
| # | Action | Expected | Pass if |
|---|---|---|---|
| 10 | PA0 to 3V3, `Y 1`, wait 2 s | TEL input ~3290, alarm 1, LD2 on | No !FAULT, clock unaffected |
| 11 | PA0 to GND | TEL ~0 mV, alarm 0, LD2 off | No !FAULT |
| 12 | `Y 0`, `Z R` | `OK clock resync ...` | drift 0, missed 0 |

## Part 4 - Faults without reset (PA0 on GND) (25 min)
For each row: `Z R`, then the command, wait 3 s, then `Z` and `R`.

| # | Command | Expected !FAULT (monitor) | Expected clock (application damage) |
|---|---|---|---|
| 13 | `I 1 0 1000` | EXEC_OVERRUN sensor(0), RESTART_TASK | `CLK! missed N deadline(s): task blocked ~100-300 ms`; drift stays ~0 (hardware kept sampling) |
| 14 | `I 1 1 1000` | EXEC_OVERRUN report(1), RESTART_TASK | missed deadlines (report task outranks the clock) |
| 15 | `I 7 9 1000` | PERIPH ADC1(9), REINIT_PERIPH | `CLK! drift -... ms: sample blocks lost`; drift stays negative (about -100 to -300 ms) |
| 16 | `I 7 10 1000` | PERIPH TIM2(10), REINIT_PERIPH | `CLK! drift +... ms: sample blocks arriving too fast`; drift stays positive (about +0.5 to +2 s) |
| 17 | `W 40000028 8` (TIM2 prescaler written directly) | PERIPH TIM2(10), REINIT_PERIPH | Same as 16: a fault not created by `I` |
| 18 | `I 3 0 1000` | LIVENESS sensor(0), RESTART_TASK | No change (sensor oversleeps; hardware and CPU unaffected) |
| 19 | `I 10 0 1000` | SEQUENCE sensor(0), RESTART_TASK | No change: caught before any damage |
| 20 | `I 4 6 1000` | ISR_RATE tim6_isr(6), REINIT_PERIPH | No change: non-critical component, application protected |
| 21 | `I 7 11 1000` | PERIPH GPIOA(11), REINIT_PERIPH | No change: configuration error caught before it shows |
| 22 | `I 8 12 1000` | COMM USART2(12), REINIT_PERIPH | No change; alarm limit not altered by the corrupted frame |
| 23 | `I 7 12 1000` (last; terminal may glitch) | PERIPH USART2(12), REINIT_PERIPH | No change |

Pass for each = `!GT ... LOCALIZED CORRECTLY` + the expected action + the expected clock line.
Rows 13-17 show damage the application suffered; rows 18-23 show faults caught with no damage.

## Part 5 - Faults with reset (15 min)
| # | Command | Expected | Clock / LED |
|---|---|---|---|
| 24 | `I 5 0 1000` | Reboot, `!TRAP ... cfsr=0x00000082`, `!FAULT type=MEM_ACCESS comp=sensor(0) conf=95%`, history ends `INJECT 5`, `MEM_FAULT 130` at 0 us, `!GT ... trap latency N us` | Clock restarts at 00:00:00 (the application lost its time) |
| 25 | `I 6 0 1000` | As 24 with addr=0x00000010 | Restarts |
| 26 | `I 2 0 1000` | `!FAULT type=STACK_OVERFLOW comp=sensor(0) conf=99%` | Restarts |
| 27 | `I 9 0 1000` | ~4 s freeze, reboot, `!WATCHDOG reset ... last running: sensor`, `!FAULT type=WATCHDOG ... 80%` | **LED stops blinking for ~4 s**, then the clock restarts |
| 28 | `R` | All reports, also those from before the resets | Reports survived the resets |

Visible combined demo (row 27 variant): PA0 to 3V3 (LD2 on), `I 9 0 1000`, move PA0 to GND
within 4 s: LD2 stays ON and the heartbeat stops (application frozen), then reboot, LD2 off.

## Part 6 - Sensor fault via PA0 (optional) (10 min)
Divider (10k + 10k from 3V3 to GND, middle to PA0, ~1650 mV), `L 100`, `Q 1`, PA0 to 3V3 for 2 s:
`SENSOR FAULT: input stuck HIGH ...` and `!FAULT type=PERIPH comp=ADC1(9) ... rules=0x0040`.
Back to the divider: `SENSOR OK`. Clock: no significant drift. `Q 0` at the end.

## Part 7 - Final checks (5 min)
| # | Command | Expected |
|---|---|---|
| 29 | `M 1`, `Z R`, `I 1 0 1000`, then `M 0` | `loc=stat` + `!SHADOW` line; clock shows missed deadlines |
| 30 | `I 1 0 5000`, `X` within 5 s | `OK cancelled`, no fault, clock unaffected |
| 31 | 2 min no injection, then `Z`, `S` | No !FAULT, drift +-10 ms, missed 0 |
| 32 | `D`, `G` | Save for the report |

## Trial record
test | command | !FAULT type | component | confidence | action | localized | clock drift | missed | pass/fail

## Notes
- The CPU overhead measurement (FM_ENABLE_MONITOR=0 vs 1) includes the clock task in both
  builds, so the difference is still the monitor's cost.
- More task switches are recorded now (the clock wakes every 10 ms), so the 16-event history
  of a report shows "other" entries for it; the ring buffer covers a slightly shorter time.
- The clock and SysTick come from the same oscillator: drift measures lost/extra samples,
  not crystal accuracy.

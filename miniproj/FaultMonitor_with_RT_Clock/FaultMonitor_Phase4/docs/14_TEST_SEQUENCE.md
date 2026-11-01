> **With the real-time clock add-on, use `15_RT_CLOCK_APP_AND_TESTS.md` instead: it contains this whole sequence plus the expected clock output for every fault.**

# Complete test sequence (sensor-monitoring application)

Setup: PuTTY on the ST-LINK COM port, 115200 8-N-1, no flow control, session logging ON.
PA0 = A0: connect only to GND or 3V3 header pins, never 5V. For Part 6: two equal resistors
(e.g. 10k + 10k) from 3V3 to GND with the middle to PA0 (~1650 mV), or a potentiometer.
Rules: one fault at a time; after each fault run R (and G); wait >= 11 s before injecting into
the same component again (otherwise recovery escalates, by design).

## Part 1 - Build and boot
1. CubeIDE: F5, Project > Clean, Build (0 errors). Flash, press reset.
2. Expect `=== Fault Monitor v2 | sensor monitor | core 84 MHz | monitor ON ===`, `BOOT #n ...`,
   `black box ... baseline ...`, then `MONITOR running (localizer: rule)`.
3. PA0 to GND: `TEL,...,2000,0..10,0*xx` once per second.

## Part 2 - Commands and clean baseline
| Cmd | Expected | Pass if |
|---|---|---|
| H | command list incl. Q | Q listed |
| I | every fault with `valid comp:` + example | fault 4 shows 6=tim6_isr, fault 9 shows 0,1,2 |
| I 9 3 1000 | `ERR fault 9 WDG_HANG cannot target comp 3 (monitor); valid comp: 0=sensor 1=report 2=shell` | clear error |
| C | black box cleared | |
| L 100 (PA0 on GND, hands off) | `LEARN start` ... ~10 s ... `LEARN done` | no fault while learning |
| B | baseline table, transitions A>B B>C C>D D>A | values present |
| S | status, IDLE and OVH lines | save as reference |
| V 1, 3 s, V 0 | SIG lines then stop | `M:2` |
| D | event rows + end marker | rows present |
| G | empty | no injections |
| R | no reports | clean start |

## Part 3 - Application input (no fault expected)
| Action | Expected | Pass if |
|---|---|---|
| PA0 to 3V3 | `TEL,...,2000,~3290,1`, LD2 on | no !FAULT |
| PA0 to GND | ~0 mV, alarm 0, LD2 off | no !FAULT |
| T 1500 / T 2000 | `OK limit ...`, `LIMIT ... mV` | limit field changes |
| button B1 | `LIMIT 2500 mV`, `LIMIT 1000 mV` ... | cycles |

## Part 4 - Faults without reset (PA0 on GND; R after each; 11 s apart)
| Cmd | Effect on the application | Expected !FAULT | Action |
|---|---|---|---|
| I 1 0 1000 | pipeline frozen | EXEC_OVERRUN sensor(0) | RESTART_TASK |
| I 3 0 1000 | ten 10 ms deadlines missed | LIVENESS sensor(0) | RESTART_TASK |
| I 3 1 1000 | telemetry stalls | LIVENESS report(1) | RESTART_TASK |
| I 10 0 1000 | compare stage repeats C>C | SEQUENCE sensor(0), rules 0x0010 | RESTART_TASK |
| I 7 9 1000 | ADC trigger corrupted, sampling stops | PERIPH ADC1(9), rules 0x0020 (+0x0040) | REINIT_PERIPH |
| I 7 10 1000 | ADC trigger timer 10x too fast | PERIPH TIM2(10) | REINIT_PERIPH |
| I 7 11 1000 | PA0 no longer analog | PERIPH GPIOA(11) | REINIT_PERIPH |
| I 8 12 1000 | corrupted command frame | COMM USART2(12), rules 0x0080 | REINIT_PERIPH |
| I 4 6 1000 | timer interrupt storm | ISR_RATE tim6_isr(6), rules 0x0004 | REINIT_PERIPH |
| I 7 12 1000 (last; terminal may glitch) | UART config changed | PERIPH USART2(12) | REINIT_PERIPH |
Pass = injected (!GT / G) + detected (!FAULT) + `LOCALIZED CORRECTLY` + action + TEL continues.

## Part 5 - Faults with reset
| Cmd | Expected after reboot |
|---|---|
| I 5 0 1000 | `!TRAP ... trap=2 cfsr=0x00000082 addr=0x20000000`, `!FAULT type=MEM_ACCESS comp=sensor(0) conf=95%`, history ends `INJECT 5` then `MEM_FAULT 130` at 0 us, `!GT ... trap latency N us` |
| I 6 0 1000 | same with addr=0x00000010 |
| I 2 0 1000 | `!FAULT type=STACK_OVERFLOW comp=sensor(0) conf=99%` |
| PA0 3V3 (LD2 on), I 9 0 1000, within 4 s PA0 to GND | LD2 stays ON (frozen) ~4 s, reboot, `!WATCHDOG reset ... last running: sensor`, `!FAULT type=WATCHDOG ... 80%`, then LD2 off |
| R | all reports incl. those before the resets |

## Part 6 - Sensor fault via PA0 (Q)
| Action | Expected | Pass if |
|---|---|---|
| divider on PA0 | TEL ~1650 mV | mid-range |
| L 100 | LEARN done | |
| Q 1 | `OK sensor check ON ...` | |
| PA0 to 3V3, wait 2 s | `SENSOR FAULT: input stuck HIGH (3.3 V) ...` + `!FAULT type=PERIPH comp=ADC1(9) ... rules=0x0040 action=REINIT_PERIPH` | reported once |
| PA0 back to divider | `SENSOR OK ...` | clears |
| wait 11 s, PA0 to GND 2 s | `SENSOR FAULT: input stuck LOW (0 V)`, !FAULT ADC1, LD2 ON at 0 mV | fail-safe alarm |
| divider, Q 0 | `SENSOR OK`, `OK sensor check OFF` | |

## Part 7 - Extra features
| Cmd | Expected |
|---|---|
| W 40000028 8 | PERIPH TIM2(10), REINIT_PERIPH (fault not from I) |
| M 1, I 1 0 1000 | `loc=stat` + `!SHADOW` line; then M 0 |
| I 1 0 5000, X within 5 s | `OK cancelled`, no fault |
| I 1 0 1000 twice quickly | second: `ERR a fault is already pending` |
| 2 min no injection, then S | no !FAULT (false-positive check) |
| D, G | save for the report |

## Part 8 - Evaluation (close PuTTY first)
```
python fm_campaign.py --port COMx --quick
python fm_campaign.py --port COMx --reps 5 --normal 120 --out campaign.csv
python fm_metrics.py campaign.csv
python fm_metrics.py campaign.csv --overhead on_status.txt off_status.txt
```
Overhead: build with FM_ENABLE_MONITOR=0, S 60 s after reset -> off_status.txt; normal build,
S 60 s after reset -> on_status.txt.

## Trial record
test no | command | !FAULT type | component | confidence | action | localized | latency | pass/fail

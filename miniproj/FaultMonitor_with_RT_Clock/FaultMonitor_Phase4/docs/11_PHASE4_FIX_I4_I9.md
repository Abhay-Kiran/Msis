# Phase 4 fix: "ERR invalid fault/target" for I 4 / I 9

## Symptom
```
I 9 3 1000
ERR invalid fault/target (type I for the list)
```

## Root cause
Not a firmware crash and not a disabled fault. `target_ok()` in
`Core/Src/inject/fault_inject.c` only accepts certain component IDs per fault:

* `WDG_HANG` (9) is a *task* fault: valid targets are 0 sensor, 1 report, 2 shell.
  Component 3 is the framework's own **monitor** task, which is deliberately
  never a target (the monitor does not judge itself).
* `ISR_STORM` (4) targets the TIM6 interrupt only: component **6** (`tim6_isr`).

The real defect was usability: the `I` listing never printed the valid
component IDs (it only had a stale "MVP-2 targets: 0 sensor task" line), and
the error message did not say which IDs were valid, so 3 / task IDs were guessed.

## Changes (source)
| File | Change |
|---|---|
| `inject/fault_inject.c` | `I` now lists, for each fault, the valid component IDs and an example command, plus the full component table. Rejections now say *why*: unknown fault, fault disabled by `FM_PHASE3_ONLY`, delay out of range, or "cannot target comp N; valid comp: ...". Target rules are one table (`tgt_accepts()`) shared by the check and the listing so they can never disagree again. |
| `inject/fault_inject.c` | `MISSED_DEADLINE` (3) now only accepts periodic tasks 0 and 1. The shell (2) is event-driven, so the detector has no liveness rule for it and the fault could never be detected there. |
| `inject/fault_inject.c` | Component ID bounds are checked before the `uint8_t` cast (e.g. `I 1 256` used to wrap to 0). |
| `inject/uart_shell.c` | Help text for `T` corrected: it is an alarm limit in mV (100..3200), not a temperature setpoint. |
| `PHASE4_COMMAND_TEST_GUIDE.md` | Added the exact fault/component table; fixed the `T` examples. |

No interface (`event_t`, `fault_report_t`, component IDs, shell letters) changed.

## Correct commands
```
I 4 6 1000     ISR storm on tim6_isr
I 9 0 1000     watchdog hang in the sensor task (board resets after ~4 s)
```

## After updating
Rebuild in CubeIDE (Project > Clean, then Build). The `Debug/` outputs and the
files in `prebuilt/` are from before this fix and do not contain it.

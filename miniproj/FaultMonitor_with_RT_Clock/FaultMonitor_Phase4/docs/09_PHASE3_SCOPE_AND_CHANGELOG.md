# Phase 3 scope and change log

## Source basis

- `Problem_Statement.docx`: project objective and monitored signals.
- `Project Briefing.txt`: authoritative three-stage architecture, team interfaces, MVP definition, evaluation plan and scope notes.
- `Team_Task_Lists.docx`: MVP-1 and MVP-2 exit milestones and per-person responsibilities.
- `Project_Phases_flow.txt`: Phase 0–5 flow; Phase 3 explicitly covers stack guard, localization, recovery and reporting.
- `Mini_Project_Approach_Rev2.pdf`: capture → on-chip compare → localize/recover flow.
- `FaultMonitor_Part1_complete_latest_modified .zip`: code baseline used for this package.

## Phase 3 included

- Existing Nucleo-F446RE CubeIDE/FreeRTOS application.
- Register-level ADC1/DMA2/TIM2 and GPIO setup used by the sensor application.
- Existing runtime event capture, DWT timestamps, ring buffer and trace signatures.
- Existing baseline learning/loading and rule-based localization.
- Existing fault report/black-box storage and UART report/dump commands.
- Existing MPU/fault-trap/stack guard path.
- Two manual-armed, automatically-fired MVP faults: stuck task and stack overflow.
- Step-by-step import/build, test, expected-output and debugging documentation.

## Deliberately out of scope for Phase 3 demonstration

- Full trials for all five additional fault types.
- Full ISR enter/exit timing validation and all remaining Phase 4 capture work.
- Full fault-free/faulted campaign, final metrics and overhead characterization.
- Optional offline Isolation Forest comparison.

These modules may remain in the supplied source tree for continuity, but the Phase 3 injection gate prevents the normal `I` command from arming faults other than IDs 1 and 2.

## Source change

1. `Core/Src/app/app_config.h`: added `FM_PHASE3_ONLY`, default `1`.
2. `Core/Src/inject/fault_inject.c`: `target_ok()` rejects fault IDs other than `FAULT_STUCK_LOOP` and `FAULT_STACK_OVERFLOW` while `FM_PHASE3_ONLY` is enabled. The `I` listing is shortened to the two Phase 3 faults.
3. Added this phase-specific README and detailed test/debug/input guides.

## Important technical cautions

- The package was not physically flashed/tested from this environment. It must be clean-built in the team's STM32CubeIDE installation and validated on the target board.
- The stack-overflow test is intentionally destructive and may reset the board.
- The existing source includes both HAL startup/RTOS integration and direct register-level peripheral configuration. Do not describe the entire application as HAL-free.
- The ADC test on PA0 is a sensor-path validation, not one of the two MVP-2 software faults.
- Ground-truth injection logs must not be used as the sole detection input; the detector should decide from captured runtime evidence.

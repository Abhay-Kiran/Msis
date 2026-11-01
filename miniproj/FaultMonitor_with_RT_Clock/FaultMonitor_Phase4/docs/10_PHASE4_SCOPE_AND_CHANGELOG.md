# Phase 4 scope and change log

## Phase boundary from the supplied project flow
Phase 4 follows MVP-2. The phase flow assigns Abhay CAP-6/CAP-7/CAP-8 (ISR enter/exit timing, reset cause/UART error/peripheral status events, ring-buffer sizing/overwrite and CPU/RAM overhead checks), Arfan threshold tuning, and Abhishek INJ-7 (remaining fault types and 15–20 trials per fault plus fault-free runs). Phase 5 is the consolidated metrics/overhead report and optional offline Isolation Forest comparison.

## Changes in this package
- `FM_PHASE3_ONLY` defaults to `0` in `Mini_Project/Core/Src/app/app_config.h`, enabling the implemented later-phase injection IDs. Set it to `1` to return to the guarded MVP-2 injection set.
- Added Phase-4 README and detailed test-command guide (Markdown and DOCX).
- Removed Python `__pycache__` artifacts from the package.
- Kept the original firmware modules and interfaces instead of replacing them with a newly invented implementation.

## Fault catalogue found in the source
1 STUCK_LOOP; 2 STACK_OVERFLOW; 3 MISSED_DEADLINE; 4 ISR_STORM; 5 MEM_RO_WRITE; 6 NULL_DEREF; 7 PERIPH_CORRUPT; 8 UART_CORRUPT; 9 WDG_HANG; 10 SEQ_REPEAT.

The command `I` prints the active fault catalogue, valid target component IDs and expected localization/action hints. Always trust that runtime output if it differs from this document.

## Verification status
Source and documentation packaging is complete. The MCU firmware has not been built with the user's local CubeIDE toolchain or tested on their board in this environment. Prebuilt artifacts in the inherited archive are retained for reference only and may not reflect this Phase-4 configuration. Do a clean CubeIDE build and confirm each fault case experimentally before claiming it passes.


## Fix 2026-10-25: I 4 / I 9 rejected
See `11_PHASE4_FIX_I4_I9.md`. `I` now prints valid component IDs per fault; error messages name the valid targets; MISSED_DEADLINE limited to periodic tasks; `T` help corrected.

## Added 2026-10-25: second application (traffic light)
`APP_SELECT=2` builds `app/traffic_app.c`. To make `/inject` application-independent,
`app_hooks.h` now declares what the injector needs from the application
(`app_corrupt_periph`, `app_periph_injectable`, `app_protected_word`, `app_task_period_ms`,
`app_task_handle`, `app_name`); the TIM6 tick moved from `app_periph.c` to the shared
`app_tick.c`. Sensor-app behaviour is unchanged. See `12_TRAFFIC_LIGHT_APP.md`.

## Fixed 2026-10-25: evaluation tooling
- `fm_campaign.py`: new `--gap` (default 11 s) between non-reset trials. Without it, repeated
  trials on the same component within FM_ESCALATE_MS (10 s) escalated recovery to a system
  reset every 3rd trial and the next trial was sent into the reboot. Escalation-to-reset is now
  also waited for.
- Monitor-off build (`FM_ENABLE_MONITOR=0`) now has a minimal `S` printing the `IDLE` line,
  which the CPU-overhead comparison (`fm_metrics.py --overhead`) needs from both builds.

## Fixed 2026-10-25: history timestamps after a trap
`print_history()` computed `-(ref - ts)` unsigned, so the MEM_FAULT/STACK_FAULT event (written a
few cycles after the crash timestamp) printed as -51130562 us (= -2^32 cycles at 84 MHz). Now a
signed difference: it prints as 0 us. The `!GT` line after a trap also prints the injection-to-trap
latency in microseconds (both timestamps are taken before the reset).

## Added 2026-10-26: sensor plausibility check ('Q')
Sensor app: `Q 1` enables a stuck-at-rail check on PA0 (fault after 1 s at <~32 mV or >~3.27 V),
reported once per episode as `fm_periph_error(ADC1, APP_PERR_SENSOR)`; alarm forced ON while it
lasts. Default OFF, so earlier behaviour is unchanged. See `13_PA0_SENSOR_TESTS.md`.

## Removed 2026-10-26: traffic-light application
The project uses one monitored workload only: the soft real-time sensor-monitoring application
(1 kHz hardware-timed ADC with DMA, 10 ms four-stage pipeline, 100 ms telemetry, FreeRTOS).
`traffic_app.c`, the `APP_SELECT` switch, the campaign's `--app` option and doc 12 were removed.
`app_hooks.h` and `app_tick.c` stay: they keep the injector free of application-specific code.

## Added 2026-10-26: real-time sample clock add-on
New file `app/rt_clock.c` only (no existing file changed): self-starting real-time task (GCC
constructor, static stack) that keeps a clock from the 10 ms ADC sample blocks, reports drift
against SysTick and its own missed deadlines, blinks a heartbeat LED on D12 (PA6) and adds the
`Z` command. Makes the application-level damage of each injected fault visible after recovery.
See `15_RT_CLOCK_APP_AND_TESTS.md`.

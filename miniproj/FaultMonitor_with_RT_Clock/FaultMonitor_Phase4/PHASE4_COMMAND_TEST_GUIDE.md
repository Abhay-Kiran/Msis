# Phase 4 command and test guide — PuTTY

**Target:** STM32 Nucleo-F446RE. USART2 via ST-LINK virtual COM. PuTTY: 115200, 8 data bits, no parity, 1 stop bit, no flow control. Type a command and press Enter. Commands are case-insensitive in this shell.

## Before testing
- Save the current known-good firmware image and keep ST-LINK available for recovery.
- Do not connect PA0 directly to 3.3 V if the board's analog supply/reference is not known; use board 3V3 only, never 5 V.
- Keep PA0 input conditions stable while learning the baseline.
- Run `H`, `I`, `S` first. The `I` output is the source-of-truth for IDs/targets in the firmware you built.
- Expected text below describes intended observations; exact numerical timing and scores depend on workload and build.

## Command reference

| Command | Why/what it does | Expected observation |
|---|---|---|
| `H` | Print registered shell commands | Help sections for app, monitor, injection and examiner commands. |
| `T 1500` | Set the alarm limit to 1500 mV (application input, not a fault) | `OK limit 1500 mV`. Valid range 100–3200 mV. |
| `T 2000` | Restore the default 2000 mV limit | `OK limit 2000 mV`; `TEL` lines show 2000 in the limit column. |
| `Y 1` / `Y 0` | Enable/disable application telemetry | Periodic `TEL,...` lines start/stop. |
| `S` | Read monitor signature/status and overhead | Component/signature status, monitor state and overhead fields. |
| `L 100` | Learn a normal baseline over 100 windows | Learning start/completion message. Keep workload and inputs stable; if command syntax says default 100, `L` alone requests default. |
| `B` | Print baseline information | Baseline loaded/learned and its statistics/status. |
| `V 1` | Stream one signature (`SIG`) line per 100 ms window | Repeated `SIG` CSV records. This is telemetry, not itself a fault. |
| `V 0` | Stop signature streaming | `SIG` stream stops. |
| `D` | Dump runtime event ring buffer | CSV header followed by timestamp/component/event/payload rows and an end marker. Save terminal output to a file. |
| `I` | List compiled-in fault IDs and valid target hints | Fault ID/name, expected detector/action and target guidance. Check this before every injection. |
| `I <fault> <comp> [delay_ms]` | Arm one fault for a target component after a delay | `OK armed ...`; this means scheduled, not yet detected. The fault fires when its trigger point is reached. |
| `X` | Cancel pending injection | `OK cancelled`. It does not undo a fault that has already fired. |
| `G` | Print injection ground-truth log | CSV rows containing cycle timestamp, component ID, fault ID and delay. Save for evaluation. |
| `R` | List stored fault reports | Report sequence/type, suspected component, confidence, recovery/action and summary. |
| `R <seq>` | Print one report and its history | Detailed report/history if that sequence exists. Use the sequence shown by `R`. |
| `C` | Clear fault log | Confirmation that stored fault log was cleared. Save any evidence first. |
| `M 0` / `M 1` | Select rule-based/statistical localizer | Mode acknowledgement. Compare outputs only after a baseline is learned. |
| `K 4` | Set detector sensitivity in sigma units | Acknowledgement of threshold; higher threshold generally means fewer alerts, lower threshold more sensitivity. Relearn/retest consistently. |
| `P <comp> <prio>` | Change task priority (valid task IDs/range printed in help) | Priority acknowledgement. This intentionally changes normal timing; restore by reset/reflash or known configuration. |
| `W <hex_addr> <hex_val>` | Arbitrary 32-bit memory/register write | **Dangerous: do not use in routine tests.** Can corrupt hardware state or crash/reset the MCU. |
| `$<cmd>*hh` | Send a framed command with XOR checksum | Valid frame is dispatched; invalid checksum should produce `ERR frame checksum` and a communication-error event. Verify exact framing against shell implementation. |


## Exact fault and component IDs (this firmware)

Component IDs: `0 sensor`, `1 report`, `2 shell`, `3 monitor`, `4 wdg`, `5 adc_isr`, `6 tim6_isr`, `7 uart_isr`, `8 btn_isr`, `9 ADC1`, `10 TIM2`, `11 GPIOA`, `12 USART2`.
Components 3 (monitor) and 4 (wdg) are the framework itself and are **never** valid targets, so `I 9 3 1000` is rejected by design.

| Fault | Name | Valid `<comp>` | Example | Expected detector result |
|---|---|---|---|---|
| 1 | STUCK_LOOP | 0, 1, 2 | `I 1 0 1000` | EXEC_OVERRUN on the task, RESTART_TASK |
| 2 | STACK_OVERFLOW | 0, 1, 2 | `I 2 0 1000` | MPU guard trap, reset, STACK_OVERFLOW report after boot |
| 3 | MISSED_DEADLINE | 0, 1 | `I 3 1 1000` | LIVENESS on the task, RESTART_TASK |
| 4 | ISR_STORM | **6 only** | `I 4 6 1000` | ISR_RATE on tim6_isr, REINIT_PERIPH (TIM6 back to 1 kHz) |
| 5 | MEM_RO_WRITE | 0, 1, 2 | `I 5 0 1000` | MemManage, reset, MEM_ACCESS report after boot |
| 6 | NULL_DEREF | 0, 1, 2 | `I 6 0 1000` | MemManage, reset, MEM_ACCESS report after boot |
| 7 | PERIPH_CORRUPT | 9, 10, 11, 12 | `I 7 10 1000` | PERIPH (config CRC) on that peripheral, REINIT_PERIPH |
| 8 | UART_CORRUPT | **12 only** | `I 8 12 1000` | COMM on USART2, REINIT_PERIPH |
| 9 | WDG_HANG | **0, 1, 2** | `I 9 0 1000` | board freezes ~4 s, IWDG reset, WATCHDOG report after boot |
| 10 | SEQ_REPEAT | **0 only** | `I 10 0 1000` | SEQUENCE C->C on sensor, RESTART_TASK |

The `I` command now prints the valid component list and an example for every fault, and a rejected command says exactly which components are valid.

## Phase 4 test order

### Test 0 — startup and shell (safe)
1. Reset board, open PuTTY, wait for startup banner.
2. Enter `H`, `I`, `S`.
3. Expected: startup/reset-cause line; help; fault catalogue; monitor status. If shell does not respond, check COM port, 115200 8-N-1, USB cable, and whether the firmware flashed successfully.

### Test 1 — baseline and signature capture (safe)
1. Connect PA0 to GND for the first ADC check, then separately to board 3V3 for the high-input check. Do not short unrelated pins.
2. Keep one condition steady and run `L 100`, then `B`.
3. Run `V 1` for 3–5 seconds, then `V 0`; run `D` and save the CSV.
4. Expected: stable signatures for steady conditions; ADC-derived telemetry near the relevant input level; high voltage crosses the configured 2000 mV threshold if that setting remains unchanged. `TEL` alarm is the sensor threshold, not proof of an execution fault.

### Test 2 — MVP-1/MVP-2 stuck task (controlled, can hang/recover task)
1. Enter `I` and confirm the `STUCK_LOOP` ID and valid component IDs. In this source, its ID is 1. The help says component 0 is the control/sensor task for the MVP-2 examples; confirm the compiled list.
2. Enter `I 1 0 1000` only if `I` confirms that target mapping.
3. Expected immediate line: `OK armed STUCK_LOOP ... in 1000 ms ...`. Then wait for the monitor response; look for a detector fault, suspected component, confidence and recovery action.
4. Enter `R`, `D`, and `G` to inspect report, history and ground truth.
5. Pass criteria: injection ground-truth record exists; monitor independently detects the deviation; localized component is consistent with the target; recovery is reported; report/history is retrievable. “OK armed” is not a pass.

### Test 3 — missed deadline (controlled delay)
1. Confirm ID 3 and target component from `I`.
2. Use a target the `I` command says is valid; for task-targeted faults this is usually a registered task such as component 0, 1 or 2.
3. Example: `I 3 0 1000` if the runtime catalogue confirms target 0 is valid.
4. Expected: task delay, a liveness/timing anomaly and an attempted task recovery. Verify `R`, `D`, `G`. This test may disturb application timing while active.

### Test 4 — peripheral corruption (moderate risk)
1. Confirm ID 7 and valid peripheral component IDs from `I`.
2. Arm only a peripheral listed as supported by the firmware. Do not guess a component ID.
3. Expected: peripheral configuration/status deviation and a reinitialization action if that detector rule is enabled. Verify `R` and `D`.

### Test 5 — UART corruption (moderate risk)
1. Confirm ID 8 and UART component ID from `I`.
2. Example: use the exact component ID printed by the list: `I 8 <uart_comp_id> 1000`.
3. Expected: an intentionally bad framed/checksum message, communication-error evidence, and a communication-related report/reinitialization if supported. Shell output can be noisy because the injected fault affects the communication path.

### Test 6 — ISR storm (high risk)
Fault ID 4 deliberately increases TIM6 interrupt activity. Run only on a bench setup with a recovery/reflash plan. Use `I` to confirm the ISR target ID, then arm the fault. Expected: ISR-rate anomaly and a TIM6 reinitialization attempt. If the shell becomes unresponsive, reset/reflash the board; do not keep repeating the test.

### Test 7 — stack overflow / memory faults (high risk)
- ID 2 `STACK_OVERFLOW`: trigger deep recursion on a valid task target. Expected MPU/stack-fault handling and a reset/report path depending on configuration.
- ID 5 `MEM_RO_WRITE` and ID 6 `NULL_DEREF`: memory protection fault tests; can enter fault handlers or reset. Use only after the stack guard and report path are validated.
- ID 9 `WDG_HANG`: intentionally stops normal progress until watchdog reset. Expect reboot and a reset-cause indication. Do not assume volatile RAM reports survive reset.
These are destructive. Save `D`, `G`, and `R` output before testing and run one fault per reset.

### Test 8 — sequence repeat (moderate risk)
1. Confirm ID 10 and its target (control component) from `I`.
2. Arm only after baseline learning and the simpler fault tests pass.
3. Expected repeated execution-stage/transition signature, sequence anomaly and control-task recovery attempt.

## Phase 4 trial plan
- Run a clean baseline with no injected fault and save `D`/`V` output.
- For each implemented fault type, perform 15–20 controlled trials where the board can be recovered safely. Record trial number, fault ID, target, injection timestamp (`G`), report sequence (`R`), detected/not detected, localization correctness, recovery, and detection latency if available.
- Include fault-free runs to measure false positives.
- Do not run destructive fault IDs in a loop without manually confirming the board has rebooted and the shell works after each attempt.
- Phase 4 includes overhead checks in the phase-flow document. Compare builds with monitor enabled and disabled only through documented CubeIDE preprocessor configuration; do not compare binaries built with different workload/baseline settings.

## Troubleshooting
- **No output:** verify ST-LINK virtual COM port, terminal settings, USB connection and reset; confirm USART2 is selected.
- **`ERR invalid fault/target`:** type `I` and use the exact fault ID and component ID printed by this firmware.
- **`ERR a fault is already pending`:** type `X`, then arm one fault only.
- **Fault armed but no event:** target may not reach its injection point, the selected component may be wrong, or the monitor/baseline is not ready. Check `I`, `S`, `B`, `D` and `G` before trying again.
- **Too many detections during clean run:** stabilize PA0/workload, learn a fresh baseline with `L 100`, and review sensitivity `K`; do not hide the issue by raising the threshold without recording it.
- **Board reset/unresponsive after injection:** this can be expected for destructive faults. Reopen PuTTY, record boot/reset cause, inspect `R` if persistent, and reflash known-good firmware if needed.
- **Build error:** clean project and rebuild; use first compiler error, not later cascading messages. Verify target MCU, FreeRTOS/CMSIS settings, include paths and linker script.

## Honest reporting
Record observed output separately from expected output. Do not claim a fault is detected merely because the injector prints `OK armed`; prove detection using a detector report and compare it with the ground-truth log. Hardware execution is required to confirm that a given fault is successfully detected and recovered.

# Phase 3 test plan and debugging guide (MVP-2)

This guide is for testing the supplied STM32CubeIDE project in understandable steps. Use it alongside `README_PHASE3.md`. The exact numeric output can vary with timing, baseline, and flash history; compare behaviour and report fields rather than expecting byte-for-byte identical logs.

## 1. Before powering the board

- Confirm the target is the **NUCLEO-F446RE**.
- Confirm PA0 is not driven above 3.3 V. For ADC tests use GND or 3.3 V only.
- Connect ST-LINK USB. Identify the ST-LINK virtual COM port in the OS.
- Open PuTTY Serial with 115200, 8-N-1, no flow control.
- Ensure you have the original ZIP and this extracted project as backups.
- Build from source in CubeIDE. A prebuilt ELF/HEX from an earlier revision will not include the Phase 3-only injector gate.

## 2. Boot and command-shell test

1. Flash and run the project.
2. Observe the startup banner, reset cause, black-box count/baseline state, and monitor status.
3. Type `H` and press Enter.
4. Type `I` and press Enter.

Expected:

- The board responds to the shell and shows help.
- The `I` list shows `1 STUCK_LOOP` and `2 STACK_OVERFLOW` only in the Phase 3-only build.
- A command for any other fault ID is rejected as an invalid fault/target.

If there is no output, see **Debugging decision tree** below before changing the firmware.

## 3. ADC input test (not a software fault injection test)

The application samples PA0 / ADC1_IN0 using ADC1, DMA2 Stream 0 and TIM2. It calculates a mean from a group of samples and converts ADC counts to millivolts using the nominal 3300 mV reference.

### Test A — PA0 connected to GND

1. Keep the board running normally.
2. Connect PA0 to GND.
3. Observe the `TEL` line's input millivolt field.

Expected: a value close to 0 mV. A small offset/noise is normal. The alarm should normally be clear for a 2000 mV threshold.

### Test B — PA0 connected to 3.3 V

1. Connect PA0 to the board's 3.3 V pin (not 5 V).
2. Observe the input millivolt field.

Expected: a value close to 3300 mV, and alarm `1` because the configured threshold is 2000 mV.

This demonstrates analog acquisition and threshold behaviour. It does not show task-fault detection, localization or recovery. Avoid shorting 3.3 V to GND while moving the wire.

## 4. Baseline setup

The detector compares current execution signatures with a baseline. A baseline learned with a different task set, clock, task period, priority or stack size may create false alarms.

1. Start with a stable workload and fixed PA0 input (GND is easiest for repeatability).
2. Use the `H` output to find the baseline-learning command supported by this build. The monitor's documented learning command is `L [n]`; `L` starts learning and optional `n` specifies the number of windows.
3. Wait for the completion message and confirm the monitor enters monitoring mode.
4. Do not change PA0, task settings or injected-fault state while learning.
5. Reboot and confirm the baseline is loaded from flash if persistence is enabled.

If the firmware reports that the baseline is absent or mismatched, repeat learning under stable conditions. Do not invent a baseline by copying the sample PuTTY log.

## 5. MVP-2 fault 1 — stuck task

**Risk:** the selected task intentionally stops progressing. Use the designated application/sensor task (component ID 0) and keep a way to reset the board.

1. Wait for the monitor to be in monitoring mode and confirm no fault is currently armed.
2. Type `I 1 0 1000` and press Enter. This requests fault ID 1 on component 0 after a 1000 ms delay. If this build's shell reports a different component mapping, stop and resolve the mapping before proceeding.
3. Expected immediate response: `OK armed STUCK_LOOP ...`.
4. After the delay, the target task reaches its injection point and enters an intentional infinite loop. The TIM6 interrupt remains enabled.
5. Observe whether the monitor flags an execution overrun/liveness deviation, names the sensor/control task, logs a report, and performs the configured recovery.
6. Type `R` to display reports, `D` to dump events, and `G` to dump the injection ground-truth records. Save the full PuTTY session.

Pass condition: the monitor's decision is based on runtime signature/timing evidence and results in a report and recovery. An `OK armed` line alone is not a pass. A ground-truth entry marks when the injector was armed/fired; it is not the detector result.

If the task never fires, check that the selected component calls `inject_point()` in its loop and that the chosen target ID matches the component registry. If the task fires but the monitor does not detect it, verify baseline quality, monitor priority, window duration, liveness/overrun thresholds, and whether the target task is registered in `fm_trace.c`.

## 6. MVP-2 fault 2 — stack overflow

**Risk:** deliberately exhausts the target task's stack. The MPU/fault handler may reset the MCU. Do this only after the basic boot, baseline, reporting and recovery path work.

1. Reboot and ensure the board is in monitoring mode with a valid baseline.
2. Type `I 2 0 1000` and press Enter.
3. Expected immediate response: `OK armed STACK_OVERFLOW ...`.
4. After the delay, deep recursion consumes stack space until the configured stack guard or FreeRTOS stack-overflow mechanism traps the fault.
5. The firmware is expected to record trap details in `.noinit` RAM and reset, according to the existing fault-handler design.
6. After reboot, observe the reset-cause/crash-report messages and type `R` to inspect the retained report.

Pass condition: a stack-fault record identifies the affected component and the post-reset report is available. A reset without a retained report is only a partial pass; investigate the exception handler, `.noinit` linker section, reset sequence and report-import-at-boot path.

Do not assume `.noinit` data survives a power cycle. It is intended to survive a reset while RAM remains powered.

## 7. Evidence to save for the project report

For each test save:

- Firmware revision and build configuration.
- Baseline state and learning conditions.
- Exact UART command, selected fault ID, component ID and delay.
- UART output before, during and after injection.
- `G` ground-truth CSV and `D` event dump.
- `R` fault report including suspected component, confidence, action and history.
- Whether recovery succeeded; whether a reset occurred; time from injection to detection if measurable.
- Any false positive, missed detection, repeated reset or unexpected behaviour.

Do not report detection rate, false-positive rate or latency from one demonstration. Those require repeated trials and a documented denominator.

## 8. Debugging decision tree

### A. CubeIDE build fails

1. Read the first compiler error, not only the final `make` failure.
2. Confirm the selected MCU is STM32F446RE and the project includes the matching startup file and linker script.
3. Run Project → Clean, then Build Project.
4. Confirm `Core/Src/app/app_config.h` is included by the injector source (it supplies `FM_PHASE3_ONLY`).
5. If an error is in generated HAL/CMSIS/FreeRTOS code, do not blindly patch vendor code; verify include paths and the CubeIDE toolchain version first.

### B. Firmware flashes but PuTTY is blank

1. Select the correct ST-LINK Virtual COM Port device.
2. Use 115200 baud, 8 data bits, no parity, 1 stop bit, no flow control.
3. Press the board reset button once after PuTTY is open.
4. Confirm the firmware is built for NUCLEO-F446RE and USART2 is configured for PA2/PA3.
5. Check the terminal's local echo setting only if characters are not echoed; local echo does not prove the MCU received the command.

### C. `I` lists later fault IDs

You are probably running an old binary or `FM_PHASE3_ONLY` is disabled. Rebuild from this source tree, clean the project, flash the new ELF, then run `I` again. Do not use destructive later-phase faults during MVP-2.

### D. `ERR invalid fault/target`

Check command syntax: `I <fault_id> <component_id> [delay_ms]`. For the Phase 3 demo, use `I 1 0 1000` or `I 2 0 1000`. Do not enter the human-readable task name in place of the numeric ID.

### E. `OK armed` appears but nothing happens

- Check whether a fault is pending; use `X` to cancel and retry.
- Verify component 0 is the task that calls `inject_point(COMP_CTRL)` or its current equivalent.
- Check whether the selected task is running and reaches the injection point.
- Verify the scheduler has started and the tick count advances.

### F. Stuck-loop happens but no fault report appears

- Confirm a baseline exists and is compatible with the current component registry.
- Check that the monitor task priority is above application tasks and that the monitor is enabled (`FM_ENABLE_MONITOR=1`, `FM_ENABLE_DETECT=1`).
- Check the monitor window and liveness/overrun thresholds in `fm_monitor.h`, `fm_detect.c` and `fault_threshold.c`.
- Verify the stuck task is included in the baseline and the detector's component kind/periodic classification is correct.
- Compare `D` and `G`: the injector ground truth proves the fault fired, while the event dump shows what the detector could observe.

### G. Stack overflow resets but report is absent

- Confirm the `MemManage_Handler`, `HardFault_Handler` and `vApplicationStackOverflowHook` call the capture trap path as intended.
- Inspect `.noinit` placement in the linker script and ensure boot code does not clear it before `fm_capture_boot()` validates it.
- Confirm the trap handler writes the crash record before reset and the monitor imports the record into the black box after boot.
- Save the reset output; do not repeatedly trigger the overflow without diagnosing the missing record.

### H. ADC stays near 0 or full scale

- Confirm PA0 is actually connected to GND or 3.3 V and common ground is shared.
- Ensure PA0 is in analog mode and no other circuit is driving it.
- Confirm ADC1 channel 0, DMA2 Stream 0 and TIM2 trigger setup are active.
- Remember the conversion assumes a nominal 3300 mV reference. Calibrate if accuracy matters.
- A stable 3.3 V input with alarm `1` is expected at a 2000 mV threshold; it is not evidence of a firmware fault.

## 9. Safety and limitations

- Do not use arbitrary memory-write command `W` during normal tests.
- Do not inject watchdog hang, null dereference, read-only memory write, peripheral corruption or ISR storm in the Phase 3 demonstration.
- The stack-overflow injection is intentionally destructive and may reset the board.
- This test guide describes expected behaviour from source inspection. Mark a test passed only after observing it on the actual board.

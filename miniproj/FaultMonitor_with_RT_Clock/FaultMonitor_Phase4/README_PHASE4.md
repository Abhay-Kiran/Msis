# FaultMonitor — Phase 4 package (STM32 Nucleo-F446RE)

## Scope
This package is based on the supplied `FaultMonitor_Part1_complete_latest_modified.zip` project and is configured for Phase 4 of `Project_Phases_flow.txt`. It retains the existing STM32CubeIDE/FreeRTOS project and turns off the Phase-3-only injection gate (`FM_PHASE3_ONLY=0`) so the implemented fault catalogue can be exercised.

Phase 4 means: Phase-3 MVP-2 flow plus the remaining implemented capture/fault types and repeated trials. Phase 5 metrics/overhead tuning and optional Isolation Forest work are not claimed complete by this archive.

## Hardware and build
- Target: NUCLEO-F446RE / STM32F446RE, USART2 through ST-LINK virtual COM port.
- Terminal: 115200 baud, 8-N-1, no flow control.
- Open `Mini_Project` as an existing STM32CubeIDE project; build the Debug configuration and inspect all warnings/errors.
- Flash the newly built ELF/HEX. **Do not flash the old `prebuilt/` binaries for Phase 4**: those binaries predate the Phase-4 source switch and may still enforce Phase-3 behaviour.
- Keep a copy of the working Phase-3 binary so you can recover the board after destructive fault experiments.

## First-run sequence
1. Reset board and open PuTTY.
2. Enter `H`, then `I` to inspect the command list and fault IDs actually compiled in.
3. Run `S`; check that tasks/components appear and the monitor is running.
4. Run `L 100` with workload and PA0 stable; wait for completion, then `B`.
5. Run `V 1` briefly to inspect `SIG` telemetry; stop with `V 0`.
6. Run safe test cases first. Use the separate `PHASE4_COMMAND_TEST_GUIDE.docx` and `.md` for commands, expected observations, risk levels and troubleshooting.

## Important safety note
The injection interface contains faults that can hang a task, corrupt peripheral configuration, cause a fault exception, or reset the MCU. Never run arbitrary `W <addr> <val>` writes. Do not run memory/watchdog/ISR-storm faults until ordinary shell, trace, detector and reporting tests are working. Some fault reports are RAM/flash-backed depending on the code path; verify persistence by reading `R` after reboot rather than assuming every report survives every reset.

## What is and is not verified
The archive has been structurally prepared and the Python tools can be syntax-checked. This environment cannot physically flash the Nucleo board or certify a first-run hardware result. Build in CubeIDE and use the test guide as the acceptance checklist. Record actual results rather than treating expected output as observed output.

# What each input means and what happens behind the scenes

This note explains the inputs used in the Phase 3 demonstration and why they are useful.

## 1. PA0 connected to GND

**Why do this?** It gives the ADC a known low input level for a simple sanity check.

**Behind the scenes:** PA0 is configured as ADC1 channel 0. TIM2 periodically triggers an ADC conversion. DMA transfers the conversion result into a RAM buffer. The application averages the sample group and converts the ADC count to millivolts using a nominal 3300 mV reference.

**Expected result:** close to 0 mV and alarm clear for a 2000 mV threshold. Small noise/offset is possible.

**What this proves:** the analog input path can observe a low voltage. It does not prove software-fault detection.

## 2. PA0 connected to 3.3 V

**Why do this?** It gives the ADC a known high input level without using a signal generator.

**Behind the scenes:** the ADC count should approach full scale (4095 for an ideal 12-bit converter). The conversion formula maps that to approximately 3300 mV. Since 3300 mV is greater than the configured 2000 mV threshold, the alarm becomes active.

**Expected result:** close to 3300 mV and alarm `1`.

**What this proves:** the high-input and threshold path works. The alarm is an application-level threshold response, not by itself an execution anomaly.

## 3. UART command `I`

**Why do this?** It lists available fault IDs and the component IDs accepted by the injector, so the test is repeatable.

**Behind the scenes:** the UART shell dispatches the command to `cmd_inject()`. With no numeric arguments, it prints the available Phase 3 faults and target component. It should not inject a fault just by listing the menu.

**Expected result:** in this Phase 3 build, IDs 1 and 2 are shown.

## 4. UART command `I 1 0 1000`

**Why do this?** It deliberately creates a reproducible lack-of-progress fault for MVP-2.

- `I`: call the fault-injection command.
- `1`: select `STUCK_LOOP`.
- `0`: select the sensor/control task component in this project's mapping.
- `1000`: wait approximately 1000 ms before firing at the target's injection point.

**Behind the scenes:** the firmware validates the ID/target, arms one pending fault and prints confirmation. When the selected task next reaches `inject_point()`, the injection code records ground truth and enters an infinite loop. The detector must infer the abnormal behaviour from execution timing/sequence; the command confirmation is not the detection result.

**Expected result:** first an `OK armed` message, then a monitor alert/report if the detection rule catches it, followed by the configured recovery action. Exact alert wording depends on the monitor state and selected localizer.

## 5. UART command `I 2 0 1000`

**Why do this?** It tests the stack guard and post-reset reporting path required by MVP-2.

- `2`: select `STACK_OVERFLOW`.
- `0`: select the sensor/control task.
- `1000`: schedule the injection after about one second.

**Behind the scenes:** the task enters recursive calls that consume stack space. The configured MPU guard / fault trap should detect the invalid stack access, save a crash record and reset. On reboot, capture/monitor startup should recognize the retained crash record and store/report it.

**Expected result:** an arm confirmation, then likely a reset, followed by a post-reset fault report. The test is incomplete if the board resets but no fault report is retained.

## 6. UART commands `R`, `D`, and `G`

- `R`: retrieve stored fault reports. Use it to inspect the suspected component, confidence, action and recent history.
- `D`: dump recorded runtime events. Use it to see the evidence available to the detector.
- `G`: dump injection ground truth. Use it to confirm which fault was armed/fired and when. This is for evaluation, not the detector's independent evidence.

## 7. Why separate the ADC test from fault injection?

The project observes both application behaviour and execution behaviour. A high ADC reading can be a valid sensor input and can legitimately activate an alarm. A stuck task or stack overflow is a deliberately abnormal execution condition. Testing these separately prevents the team from incorrectly reporting a normal threshold alarm as autonomous fault localization.

# PA0 (sensor input) tests and the sensor plausibility check

PA0 = Arduino A0. Only 0..3.3 V: connect to the GND or 3V3 header pins, NEVER 5V.

## 1. Normal application behaviour (check OFF, default)
| PA0 | TEL input field | alarm | LD2 | Monitor |
|---|---|---|---|---|
| GND | ~0-10 mV | 0 | off | no fault (valid input) |
| 3V3 | ~3280-3300 mV | 1 | on | no fault (valid input) |
| open (floating) | random, drifting | may toggle | may flicker | no fault, but avoid: noisy |
The value settles within ~0.1 s (smoothing alpha 0.3 per 10 ms block).

## 2. Sensor fault via PA0 ('Q 1')
A real sensor never sits exactly on a rail; a broken wire or short does.
With `Q 1`, PA0 held at <~32 mV or >~3.27 V for more than 1 s is a SENSOR FAULT:
reported once to the framework as a peripheral error on ADC1 (rule PERIPH_ERR 0x0040),
the alarm is forced ON (fail-safe), and the fault clears after 0.3 s back in range.

With the check ON the normal input must be mid-range: use a potentiometer, or two
equal resistors (e.g. 10 k + 10 k) from 3V3 to GND with the middle to PA0 (~1650 mV).
Learn the baseline (`L 100`) with that mid-range input.

Procedure and expected output:
```
Q 1
OK sensor check ON (valid band 41..4059 counts = ~32..3271 mV), sensor ok
            <- move PA0 from the divider to 3V3
SENSOR FAULT: input stuck HIGH (3.3 V) for >1000 ms, alarm forced ON
!FAULT seq=.. type=PERIPH comp=ADC1(9) conf=..% loc=rule action=REINIT_PERIPH rules=0x0040 ...
!HIST ...
            <- PA0 back to the divider
SENSOR OK: input back in range, normal alarm logic
```
Same with PA0 to GND (`stuck LOW (0 V)`): the alarm is forced ON although the input reads 0 mV.
The SENSOR FAULT and !FAULT lines can appear in either order.
Toggling faster than every 10 s on the same component escalates recovery (by design).

## 3. Making an injected fault visible on the application
Most faults are detected and recovered within ~0.1-0.3 s, faster than you can see:
the evidence is in `R` (history shows the pipeline stopping) and the latency.
The watchdog hang freezes the application for 4 s and is visible:
1. PA0 to 3V3: LD2 on.
2. `I 9 0 1000`, then within 4 s move PA0 to GND: LD2 stays ON (application frozen).
3. Watchdog reset after ~4 s: banner, `!WATCHDOG reset ... last running: sensor`,
   and after warm-up LD2 follows PA0 (off).

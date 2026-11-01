#!/usr/bin/env python3
"""
fm_campaign.py -- automated fault-injection campaign. Owner: Abhishek.

Runs every (fault, target) pair N times on the live board, waits for the
monitor's verdict, and writes one CSV row per trial. Also runs a fault-free
period and counts false alarms. tools/fm_metrics.py turns the CSV into the
evaluation table (detection rate, localization accuracy, FP, FN, latency).

  python fm_campaign.py --port COM5 --reps 5 --normal 120 --out campaign.csv
  python fm_campaign.py --port COM5 --only 1,4,7 --reps 3     # some faults only
  python fm_campaign.py --quick                                # 1-2 min smoke test, port auto-detected

The board must be MONITORING (baseline learned) before you start: run it
once for ~12 s after flashing, or send 'L' and wait for "LEARN done".
Faults that reset the board (stack overflow, memory access, watchdog)
take longer: the tool waits for the reboot and reads the boot report.

Needs: pip install pyserial
"""
import argparse
import csv
import re
import sys
import time

from fm_serial import open_port, send, lines, parse_kv

# fault id -> (name, targets, expected detector type, resets the board?)
FAULTS = {
    1:  ("STUCK_LOOP",      [0, 1, 2],        "EXEC_OVERRUN",   False),
    2:  ("STACK_OVERFLOW",  [0, 1, 2],        "STACK_OVERFLOW", True),
    3:  ("MISSED_DEADLINE", [0, 1],           "LIVENESS",       False),  # shell is event-driven: no liveness rule
    4:  ("ISR_STORM",       [6],              "ISR_RATE",       False),
    5:  ("MEM_RO_WRITE",    [0, 1, 2],        "MEM_ACCESS",     True),
    6:  ("NULL_DEREF",      [0, 1, 2],        "MEM_ACCESS",     True),
    7:  ("PERIPH_CORRUPT",  [9, 10, 11, 12],  "PERIPH",         False),
    8:  ("UART_CORRUPT",    [12],             "COMM",           False),
    9:  ("WDG_HANG",        [0, 1, 2],        "WATCHDOG",       True),
    10: ("SEQ_REPEAT",      [0],              "SEQUENCE",       False),
}
COMP_NAMES = {0: "ctrl", 1: "report", 2: "shell", 6: "tim6_isr", 9: "ADC1",
              10: "TIM2", 11: "GPIOA", 12: "USART2"}



def find_stlink_port():
    """Returns the ST-LINK virtual COM port name, or None if not found."""
    try:
        from serial.tools import list_ports
    except ImportError:
        return None
    for p in list_ports.comports():
        text = f"{p.description} {p.manufacturer}".lower()
        if "stlink" in text or "st-link" in text or "stmicroelectronics" in text:
            return p.device
    return None


def comp_id_of(field):
    m = re.search(r"\((\d+)\)", field or "")
    return int(m.group(1)) if m else -1


def run_trial(s, log, fault, target, delay_ms, resets):
    send(s, f"I {fault} {target} {delay_ms}")
    t_cmd = time.time()
    result = {"detected": 0, "det_comp": -1, "det_type": "", "conf": "",
              "latency_ms": "", "action": "", "loc": "", "shadow": ""}
    wait = 20.0 if resets else 6.0
    for line in lines(s, wait, log):
        if line.startswith("ERR"):
            result["action"] = "REJECTED: " + line.strip()   # never armed
            break
        if line.startswith("!SHADOW"):
            result["shadow"] = line.split("says:", 1)[-1].strip()
        if line.startswith("!FAULT"):
            kv = parse_kv(line)
            result.update(detected=1, det_comp=comp_id_of(kv.get("comp")),
                          det_type=kv.get("type", ""), conf=kv.get("conf", "").rstrip("%"),
                          action=kv.get("action", ""), loc=kv.get("loc", ""))
            lat = kv.get("latency_ms", "-1")
            if lat not in ("-1", ""):
                result["latency_ms"] = lat
            elif resets:
                result["latency_ms"] = f"{(time.time() - t_cmd) * 1000 - delay_ms:.0f}"
            if not resets:
                if result["action"] == "SYSTEM_RESET":
                    # recovery escalated to a reset: wait for the reboot so
                    # the next trial is not sent into the boot sequence
                    for l2 in lines(s, 20.0, log):
                        if l2.startswith("MONITOR running"):
                            break
                    break
                # wait for the recovery line, then let things settle
                for l2 in lines(s, 3.0, log):
                    if l2.startswith("!RECOVERED"):
                        break
                break
            # after a reset, the report comes from the boot code; let the
            # monitor finish its warm-up before the next trial
        if resets and line.startswith("MONITOR running"):
            break
    time.sleep(2.0)                 # grace windows + settle
    s.reset_input_buffer()
    return result


def normal_period(s, log, seconds):
    """Counts !FAULT lines while nothing is injected (false positives)."""
    fps = 0
    for line in lines(s, seconds, log):
        if line.startswith("!FAULT"):
            fps += 1
            print("  false alarm:", line)
    return fps


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="COM port (default: auto-detect the ST-LINK port)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--reps", type=int, default=3)
    ap.add_argument("--delay", type=int, default=300, help="arm-to-fire delay, ms")
    ap.add_argument("--gap", type=float, default=11.0,
                    help="extra seconds after a non-reset trial. The firmware escalates "
                         "recovery (restart -> restart+reinit -> system reset) when the SAME "
                         "component faults again within FM_ESCALATE_MS = 10 s; a gap > 10 s "
                         "keeps every trial independent. Use 0 to test escalation itself.")
    ap.add_argument("--normal", type=float, default=60.0, help="fault-free seconds for FP count")
    ap.add_argument("--only", help="comma-separated fault ids")
    ap.add_argument("--localizer", choices=["rule", "stat"], default="rule")
    ap.add_argument("--out", default=time.strftime("campaign_%Y%m%d_%H%M%S.csv"))
    ap.add_argument("--quick", action="store_true",
                    help="smoke test: faults 1 and 10, 1 rep, 20 s fault-free period")
    args = ap.parse_args()

    if args.quick:
        args.only, args.reps, args.normal = "1,10", 1, 20.0
    if not args.port:
        args.port = find_stlink_port()
        if not args.port:
            sys.exit("ST-LINK COM port not found: close PuTTY, check the cable, "
                     "or pass --port COM5")
        print(f"using port {args.port}")

    s = open_port(args.port, args.baud)
    log = open(args.out.replace(".csv", ".log"), "w")
    send(s, "M " + ("1" if args.localizer == "stat" else "0"))
    send(s, "Y 0")
    time.sleep(0.5)
    s.reset_input_buffer()

    only = {int(x) for x in args.only.split(",")} if args.only else set(FAULTS)
    rows = []
    fps = 0
    try:
        print(f"normal operation for {args.normal:.0f} s (false-positive count)...")
        fps = normal_period(s, log, args.normal)

        for fault, (name, targets, expect, resets) in FAULTS.items():
            if fault not in only:
                continue
            for target in targets:
                for rep in range(args.reps):
                    r = run_trial(s, log, fault, target, args.delay, resets)
                    if not resets and r["action"] != "SYSTEM_RESET" and args.gap > 0:
                        time.sleep(args.gap)          # no escalation between trials
                        s.reset_input_buffer()
                    ok_loc = int(r["det_comp"] == target)
                    print(f"{name:16} -> {COMP_NAMES[target]:9} rep {rep + 1}: "
                          f"{'DETECTED' if r['detected'] else 'missed  '} "
                          f"{r['det_type']:15} comp={r['det_comp']:>3} "
                          f"{'OK' if ok_loc else '--'} lat={r['latency_ms']} ms")
                    rows.append(dict(fault=fault, fault_name=name, target=target,
                                     target_name=COMP_NAMES[target], expected_type=expect,
                                     rep=rep + 1, localized=ok_loc,
                                     type_ok=int(r["det_type"] == expect), **r))
    except KeyboardInterrupt:
        print("\nstopped by user: saving the trials done so far")
    finally:
        # always cancel any armed fault and turn telemetry back on
        try:
            send(s, "X")
            time.sleep(0.3)
            send(s, "Y 1")
        except Exception:
            pass

    with open(args.out, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()) if rows else ["fault"])
        w.writeheader()
        w.writerows(rows)
    with open(args.out.replace(".csv", "_fp.txt"), "w") as f:
        f.write(f"normal_seconds={args.normal}\nfalse_alarms={fps}\n")
    send(s, "Y 1")
    print(f"\nsaved {args.out} ({len(rows)} trials), false alarms in "
          f"{args.normal:.0f} s of normal operation: {fps}")
    print(f"next: python fm_metrics.py {args.out}")


if __name__ == "__main__":
    main()

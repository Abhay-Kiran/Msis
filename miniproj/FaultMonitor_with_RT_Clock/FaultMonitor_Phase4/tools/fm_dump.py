#!/usr/bin/env python3
"""
fm_dump.py -- host tool for the Fault Monitor. Owner: Abhishek.

Talks to the board over the ST-LINK virtual COM port and saves what it
sends, so you can check the capture layer is working.

  python fm_dump.py --port COM5 dump            # 'D': ring buffer -> events_<time>.csv + summary
  python fm_dump.py --port COM5 stream -t 30    # 'V 1' for 30 s -> sig_<time>.csv
  python fm_dump.py --port COM5 status          # 'S': print the live table
  python fm_dump.py parse-dump events.csv       # re-analyse a saved dump offline

Linux/macOS ports look like /dev/ttyACM0 or /dev/tty.usbmodemXXXX.
Needs: pip install pyserial
"""
import argparse
import csv
import sys
import time
from collections import defaultdict

EVT_NAMES = ["TASK_IN", "TASK_OUT", "ISR_ENTER", "ISR_EXIT", "RSV4", "RSV5",
             "STAGE", "CHECKIN", "STACK_FAULT", "MEM_FAULT", "PERIPH_ERR",
             "COMM_ERR", "RESET_CAUSE", "INJECT", "FAULT", "RECOVERY", "MARK"]


def open_port(port, baud):
    try:
        import serial  # pyserial
    except ImportError:
        sys.exit("pyserial missing: pip install pyserial")
    s = serial.Serial(port, baud, timeout=0.5)
    time.sleep(0.2)
    s.reset_input_buffer()
    return s


def send(s, cmd):
    s.write((cmd + "\r\n").encode())


def read_until_end(s, timeout=15.0):
    lines, t0 = [], time.time()
    while time.time() - t0 < timeout:
        raw = s.readline()
        if not raw:
            continue
        line = raw.decode(errors="replace").strip()
        if line == "END":
            return lines
        lines.append(line)
    sys.exit("timeout waiting for END -- is the firmware running and the port right?")


def parse_dump_lines(lines):
    """Returns (clk_hz, comp_names, events) from the 'D' output."""
    clk, names, events = None, {}, []
    for line in lines:
        if line.startswith("#FMDUMP"):
            for part in line.split(","):
                if part.startswith("clk="):
                    clk = int(part[4:])
        elif line.startswith("#C,"):
            _, cid, _kind, name = line.split(",", 3)
            names[int(cid)] = name
        elif line and line[0].isdigit():
            idx, ts, comp, typ, extra = (int(x) for x in line.split(","))
            events.append((idx, ts, comp, typ, extra))
    return clk, names, events


def summarize(clk, names, events):
    if not events:
        print("no events -- check the trace hooks in FreeRTOSConfig.h")
        return
    clk = clk or 84_000_000
    t0 = events[0][1]
    span_us = ((events[-1][1] - t0) & 0xFFFFFFFF) * 1e6 / clk
    print(f"{len(events)} events over {span_us/1000:.1f} ms (core {clk/1e6:.0f} MHz)")
    counts = defaultdict(lambda: defaultdict(int))
    for _, _, comp, typ, _ in events:
        counts[comp][EVT_NAMES[typ] if typ < len(EVT_NAMES) else typ] += 1
    print(f"{'comp':>4} {'name':<10} counts")
    for comp in sorted(counts):
        name = names.get(comp, "other" if comp == 0xFE else "?")
        print(f"{comp:>4} {name:<10} {dict(counts[comp])}")
    print("\nlast 15 events:")
    for idx, ts, comp, typ, extra in events[-15:]:
        t_us = ((ts - t0) & 0xFFFFFFFF) * 1e6 / clk
        name = names.get(comp, "other" if comp == 0xFE else str(comp))
        tname = EVT_NAMES[typ] if typ < len(EVT_NAMES) else str(typ)
        print(f"  {t_us:12.1f} us  {name:<10} {tname:<11} {extra}")


def cmd_dump(args):
    s = open_port(args.port, args.baud)
    send(s, "D")
    lines = read_until_end(s)
    clk, names, events = parse_dump_lines(lines)
    out = args.out or time.strftime("events_%Y%m%d_%H%M%S.csv")
    with open(out, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["idx", "cycle_ts", "comp", "comp_name", "type", "type_name", "extra"])
        for idx, ts, comp, typ, extra in events:
            w.writerow([idx, ts, comp, names.get(comp, "other"), typ,
                        EVT_NAMES[typ] if typ < len(EVT_NAMES) else typ, extra])
    print(f"saved {out}")
    summarize(clk, names, events)


def parse_sig_line(line):
    """'SIG,win,tlen,id:runs:cpu:slice:gap:stack,...,T:...,E:a:b:c' -> dict"""
    parts = line.split(",")
    row = {"window": int(parts[1]), "t_len": int(parts[2])}
    for p in parts[3:]:
        if p.startswith("T:"):
            row["trans"] = p[2:]
        elif p.startswith("E:"):
            c, pe, tr = p[2:].split(":")
            row.update(comm_err=int(c), periph_err=int(pe), traps=int(tr))
        elif p.startswith("P:"):
            for item in filter(None, p[2:].split(";")):
                cid, n = item.split("=")
                row[f"c{cid}_errs"] = int(n)
        elif p[:2] in ("R:", "Q:"):
            comp, ft, conf = (int(x) for x in p[2:].split(":"))
            k = "rule" if p[0] == "R" else "stat"
            row.update({f"{k}_comp": comp, f"{k}_type": ft, f"{k}_conf": conf})
        elif p.startswith("L:"):
            comp, fault = (int(x) for x in p[2:].split(":"))
            row.update(label_comp=comp, label_fault=fault)
        elif p.startswith("M:"):
            row["mode"] = int(p[2:])
        else:
            cid, runs, cpu, sl, gap, stk = p.split(":")
            for k, v in (("runs", runs), ("cpu", cpu), ("max_slice", sl),
                         ("max_gap", gap), ("stack_free", stk)):
                row[f"c{cid}_{k}"] = int(v)
    return row


def cmd_stream(args):
    s = open_port(args.port, args.baud)
    send(s, "V 1")
    rows, drops, t0 = [], 0, time.time()
    try:
        while time.time() - t0 < args.time:
            line = s.readline().decode(errors="replace").strip()
            if line.startswith("SIG,"):
                rows.append(parse_sig_line(line))
            elif line.startswith("SIGDROP,"):
                drops += int(line.split(",")[1])
    finally:
        send(s, "V 0")
    if not rows:
        sys.exit("no SIG lines received")
    out = args.out or time.strftime("sig_%Y%m%d_%H%M%S.csv")
    cols = sorted({k for r in rows for k in r}, key=lambda k: (k != "window", k))
    with open(out, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=cols)
        w.writeheader()
        w.writerows(rows)
    print(f"saved {len(rows)} windows to {out} ({drops} dropped by the board)")


def cmd_status(args):
    s = open_port(args.port, args.baud)
    send(s, "S")
    t0 = time.time()
    while time.time() - t0 < 2.0:
        line = s.readline().decode(errors="replace").rstrip()
        if line:
            print(line)


def cmd_parse(args):
    with open(args.file) as f:
        r = csv.DictReader(f)
        events = [(int(x["idx"]), int(x["cycle_ts"]), int(x["comp"]),
                   int(x["type"]), int(x["extra"])) for x in r]
    names = {}
    with open(args.file) as f:
        for x in csv.DictReader(f):
            names[int(x["comp"])] = x["comp_name"]
    summarize(None, names, events)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="e.g. COM5 or /dev/ttyACM0")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--out", help="output file name")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("dump")
    st = sub.add_parser("stream")
    st.add_argument("-t", "--time", type=float, default=10.0, help="seconds")
    sub.add_parser("status")
    pp = sub.add_parser("parse-dump")
    pp.add_argument("file")
    args = ap.parse_args()
    if args.cmd in ("dump", "stream", "status") and not args.port:
        ap.error("--port is required")
    {"dump": cmd_dump, "stream": cmd_stream, "status": cmd_status,
     "parse-dump": cmd_parse}[args.cmd](args)


if __name__ == "__main__":
    main()

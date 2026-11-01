#!/usr/bin/env python3
"""
fm_metrics.py -- evaluation metrics from a campaign. Owner: Abhishek.

  python fm_metrics.py campaign.csv
  python fm_metrics.py campaign.csv --overhead on_status.txt off_status.txt

Prints (and saves as <campaign>_metrics.md):
  detection rate      = detected trials / all trials
  localization acc.   = trials blamed on the injected component / detected
  type accuracy       = trials with the expected fault type / detected
  false negatives     = trials not detected
  false positives     = alarms during the fault-free period (+ per hour)
  latency             = injection -> detection, ms (min / median / p95 / max)
  overhead            = from two 'S' outputs, monitor ON vs OFF build:
                        CPU from the idle-hook counter, RAM/flash from the
                        OVH line and the build sizes you enter
"""
import argparse
import csv
import re
import statistics
import sys
from collections import defaultdict


def pct(a, b):
    return f"{100.0 * a / b:.1f} %" if b else "n/a"


def idle_rate(path):
    """Idle-hook calls per ms from the IDLE,<count>,<uptime_ms> line of 'S'."""
    txt = open(path).read()
    m = re.findall(r"IDLE,(\d+),(\d+)", txt)
    if not m:
        sys.exit(f"{path}: no IDLE line (save the output of 'S')")
    c, ms = (int(x) for x in m[-1])
    return c / ms


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("campaign")
    ap.add_argument("--overhead", nargs=2, metavar=("ON_S", "OFF_S"),
                    help="saved 'S' outputs of the monitor-on and monitor-off builds")
    args = ap.parse_args()

    rows = list(csv.DictReader(open(args.campaign)))
    if not rows:
        sys.exit("empty campaign")
    out = []
    p = out.append
    n = len(rows)
    det = [r for r in rows if r["detected"] == "1"]
    loc = [r for r in det if r["localized"] == "1"]
    typ = [r for r in det if r["type_ok"] == "1"]
    lats = sorted(float(r["latency_ms"]) for r in det if r["latency_ms"] not in ("", "-1"))

    p("# Fault monitor evaluation\n")
    p("| metric | value |\n|---|---|")
    p(f"| trials | {n} |")
    p(f"| detection rate | {pct(len(det), n)} ({len(det)}/{n}) |")
    p(f"| false negatives | {n - len(det)} |")
    p(f"| localization accuracy | {pct(len(loc), len(det))} ({len(loc)}/{len(det)}) |")
    p(f"| fault-type accuracy | {pct(len(typ), len(det))} |")
    try:
        fp = dict(l.strip().split("=") for l in open(args.campaign.replace(".csv", "_fp.txt")))
        secs, fa = float(fp["normal_seconds"]), int(fp["false_alarms"])
        p(f"| false positives | {fa} in {secs:.0f} s ({fa * 3600 / secs:.2f} per hour) |")
    except (OSError, KeyError, ValueError):
        p("| false positives | (no _fp.txt) |")
    if lats:
        p(f"| latency min / median / p95 / max | {lats[0]:.0f} / {statistics.median(lats):.0f} / "
          f"{lats[min(len(lats) - 1, int(0.95 * len(lats)))]:.0f} / {lats[-1]:.0f} ms |")

    p("\n## Per fault\n")
    p("| fault | target | trials | detected | localized | type ok | median latency ms |")
    p("|---|---|---|---|---|---|---|")
    groups = defaultdict(list)
    for r in rows:
        groups[(r["fault_name"], r["target_name"])].append(r)
    for (f, t), g in groups.items():
        d = [r for r in g if r["detected"] == "1"]
        l = [float(r["latency_ms"]) for r in d if r["latency_ms"] not in ("", "-1")]
        p(f"| {f} | {t} | {len(g)} | {len(d)} | {sum(r['localized'] == '1' for r in d)} | "
          f"{sum(r['type_ok'] == '1' for r in d)} | {statistics.median(l):.0f} |" if l else
          f"| {f} | {t} | {len(g)} | {len(d)} | {sum(r['localized'] == '1' for r in d)} | "
          f"{sum(r['type_ok'] == '1' for r in d)} | - |")

    if args.overhead:
        on, off = idle_rate(args.overhead[0]), idle_rate(args.overhead[1])
        p("\n## Overhead\n")
        p(f"CPU overhead (idle-hook method): {100.0 * (off - on) / off:.2f} %")
        m = re.findall(r"OVH,.*", open(args.overhead[0]).read())
        if m:
            p(f"Board says: `{m[-1]}`")
        p("Flash: compare `arm-none-eabi-size` of both .elf files (text+data).")

    text = "\n".join(out)
    print(text)
    path = args.campaign.replace(".csv", "_metrics.md")
    open(path, "w").write(text + "\n")
    print(f"\nsaved {path}")


if __name__ == "__main__":
    main()

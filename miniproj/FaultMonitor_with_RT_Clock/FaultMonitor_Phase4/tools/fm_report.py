#!/usr/bin/env python3
"""
fm_report.py -- read the black box and point at the source line.
Owner: Arfan.

Sends 'R', parses the stored fault reports, and runs arm-none-eabi-addr2line
on each report's PC so a stuck loop or a bad memory access is shown as
file:line and function -- the "suspected component" down to the line.

  python fm_report.py --port COM5 --elf ../Mini_Project/Debug/Mini_Project.elf
  python fm_report.py --file saved_R_output.txt --elf Mini_Project.elf

addr2line comes with STM32CubeIDE (plugins/...gnu-tools-for-stm32.../tools/bin)
or the Arm GNU toolchain; put it on PATH or pass --addr2line.
"""
import argparse
import shutil
import subprocess
import sys
import time


def addr2line(tool, elf, pc):
    try:
        r = subprocess.run([tool, "-f", "-C", "-e", elf, hex(pc)], capture_output=True,
                           text=True, timeout=10)
        fn, loc = (r.stdout.strip().splitlines() + ["?", "?"])[:2]
        return f"{fn} @ {loc}"
    except (OSError, subprocess.SubprocessError):
        return "(addr2line failed)"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port")
    ap.add_argument("--file")
    ap.add_argument("--elf", required=True)
    ap.add_argument("--addr2line", default=shutil.which("arm-none-eabi-addr2line") or "arm-none-eabi-addr2line")
    args = ap.parse_args()
    if args.port:
        from fm_serial import open_port, send, lines
        s = open_port(args.port)
        send(s, "R")
        text = []
        for line in lines(s, 10):
            text.append(line)
            if line.startswith("END"):
                break
    elif args.file:
        text = open(args.file).read().splitlines()
    else:
        sys.exit("--port or --file")

    for line in text:
        if not line.startswith("R,"):
            continue
        f = line.split(",")
        seq, up, typ, comp, conf, act, loc, rules, pc, cause = f[1:11]
        pcv = int(pc, 16)
        where = addr2line(args.addr2line, args.elf, pcv & ~1) if pcv else "(no PC: not a task fault)"
        print(f"#{seq:>3} t={int(up)/1000:8.1f}s {typ:15} {comp:9} conf {conf:>3}% "
              f"{act:14} [{loc}] reset={cause}\n      pc={pc} -> {where}")


if __name__ == "__main__":
    main()

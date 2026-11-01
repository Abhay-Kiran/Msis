"""
fm_serial.py -- shared serial helpers for the tools. Owner: Abhishek.
"""
import sys
import time


def open_port(port, baud=115200, timeout=0.2):
    try:
        import serial  # pyserial
    except ImportError:
        sys.exit("pyserial missing: pip install pyserial")
    s = serial.Serial(port, baud, timeout=timeout)
    time.sleep(0.2)
    s.reset_input_buffer()
    return s


def send(s, cmd):
    """Sends a command slowly enough for the shell's echo to keep up."""
    for ch in (cmd + "\r"):
        s.write(ch.encode())
        time.sleep(0.002)


def framed(cmd):
    """'T 55' -> '$T 55*hh' with the XOR checksum the shell checks."""
    cs = 0
    for c in cmd:
        cs ^= ord(c)
    return f"${cmd}*{cs:02X}"


def lines(s, seconds, log=None):
    """Yields decoded lines for `seconds`, optionally appending to a log."""
    t0 = time.time()
    while time.time() - t0 < seconds:
        raw = s.readline()
        if not raw:
            continue
        line = raw.decode(errors="replace").strip()
        if log:
            log.write(f"{time.time():.3f} {line}\n")
        yield line


def parse_kv(line):
    """'!FAULT seq=3 type=X comp=ctrl(0) ...' -> dict"""
    out = {}
    for tok in line.split():
        if "=" in tok:
            k, v = tok.split("=", 1)
            out[k] = v
    return out

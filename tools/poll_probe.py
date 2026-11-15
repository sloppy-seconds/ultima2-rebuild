#!/usr/bin/env python3
"""Find the safe memory-API polling rate on a live Spice86.

Why: the 3 Hz working limit came from a race in the windowed CFG view model
(CfgCpuViewModel.cs:459), not from the memory endpoint. A change-triggered
recorder needs to poll a small signature as fast as possible, so we need to know
whether that 3 Hz figure actually applies to plain memory reads.

Ramps the request rate and reports latency and errors per step. Stops on the
first failure so the session survives. Read-only: it never writes memory.

Usage: tools/poll_probe.py [port] [--max 40]
"""
import json
import sys
import time
import urllib.request
import urllib.error

DS = 0x08BE
SIG_OFF = 0x50      # food + a few neighbours: the turn odometer lives here
SIG_LEN = 24


def read(port, off, n):
    url = f"http://127.0.0.1:{port}/api/memory/{DS * 16 + off}/range/{n}"
    t0 = time.perf_counter()
    with urllib.request.urlopen(url, timeout=5) as r:
        json.load(r)
    return (time.perf_counter() - t0) * 1000.0


def step(port, rate, dur):
    """Issue `rate` requests per second for `dur` seconds."""
    period = 1.0 / rate
    lat, errs = [], 0
    end = time.time() + dur
    while time.time() < end:
        t0 = time.time()
        try:
            lat.append(read(port, SIG_OFF, SIG_LEN))
        except Exception as e:                      # noqa: BLE001
            errs += 1
            print(f"    ERROR at {rate} req/s: {type(e).__name__}: {e}")
            break
        sleep = period - (time.time() - t0)
        if sleep > 0:
            time.sleep(sleep)
    if lat:
        lat.sort()
        return len(lat), lat[len(lat) // 2], lat[-1], errs
    return 0, 0, 0, errs


def main():
    # `-h` answers with this file's own docstring. Without it `--help` was read as a port number
    # and the tool died in int() with a traceback, which is what tools/doc_command_gate.py now
    # catches for every script a doc points at.
    if len(sys.argv) > 1 and sys.argv[1] in ("-h", "--help"):
        print((__doc__ or "").strip())
        return 0
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 20180
    rates = [3, 5, 8, 12, 20, 30, 40, 60]
    print(f"probing memory API on port {port}, {SIG_LEN}-byte reads")
    print(f"{'rate':>6} {'ok':>5} {'p50 ms':>8} {'max ms':>8} {'errs':>5}")
    for r in rates:
        n, p50, mx, errs = step(port, r, 3.0)
        print(f"{r:>6} {n:>5} {p50:>8.1f} {mx:>8.1f} {errs:>5}")
        if errs:
            print(f"\nstopping: failures at {r} req/s")
            break
        if p50 > 200:
            print(f"\nstopping: latency collapsed at {r} req/s")
            break
    # confirm the emulator is still alive and responsive
    try:
        read(port, SIG_OFF, 1)
        print("\nAPI still responsive after probe")
    except Exception as e:                          # noqa: BLE001
        print(f"\nAPI DEAD after probe: {e}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())

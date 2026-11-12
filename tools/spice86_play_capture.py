#!/usr/bin/env python3
"""spice86_play_capture.py — the canonical sidecar for a gameplay capture.

READ THIS BEFORE WRITING ANOTHER AD-HOC SHOTLOOP.

The recurring failure: a gameplay session is captured with a screenshot loop
and a teardown CFG harvest, and then a one-shot event (a kill, a teleport, a
fade that writes a sector) cannot be answered afterwards. Two independent
reasons, both measured on captures/spice86/minax-fight-live (2026-11-12):

  * A teardown `ExecutionFlow` harvest cannot hold a one-shot event. The ring
    buffer held 3,782 instructions and ZERO hits for the victory (0x74A0), the
    teleport gate (0x2C2D) or the Minax strike (0x0F8E). The trace rolls in
    seconds, so a dump taken after the kill records the idle loop, not the kill.
  * Frames record what the player SAW and nothing about what was WRITTEN. A
    persist question ("does the ending save the sector?") is invisible to pixels.

The primitives that DO survive a rolling trace are what this tool runs at once,
alongside the owner playing (or a seeded headless run):

  1. a change-triggered DS-field logger   -> <out>/ds.jsonl   (memory persists)
  2. periodic md5s of the run-dir files    -> <out>/md5.log    (a write is a diff)
  3. breakpoints armed BEFORE the event    -> <out>/bp-<ip>.log (fires on hit)
  4. optional 1/sec frames                 -> <out>/frames/    (what the player saw)

It COMPOSES the existing tools and re-derives nothing: frames come from
tools/spice86_screenshot.py, breakpoints from tools/bp_read.py, and the DS reads
use the same /api/memory endpoint as tools/spice86_type.py --peek.

Ports and the run dir are read from tmp/.spice86-session.env when not passed,
so the usual call is just:

    tools/spice86_play_capture.py --out /var/tmp/u2/cap --bp 61a6 --frames

Write --out under /var/tmp, never /tmp (tmpfs = RAM taken from the model).

Exit is on --duration elapsing or SIGINT/SIGTERM. A startup probe read fails
loudly if no live session is reachable, so an empty capture is never mistaken
for a good one.
"""
import argparse
import hashlib
import json
import os
import signal
import subprocess
import sys
import threading
import time
import urllib.request

DS_SEG = 0x08BE          # the game's DS segment (see tools/poll_probe.py)
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STATE_FILE = os.path.join(ROOT, "tmp", ".spice86-session.env")

# Default DS field set: name=off:len (hex), all offsets within the default span.
# These are the fields the handoff recipes keep re-deriving by hand; they are the
# ones a one-shot event moves. Extend per-capture with --fields.
DEFAULT_FIELDS = [
    ("pos_x",      0x00, 1),
    ("pos_y",      0x01, 1),
    ("mode",       0x12, 1),   # DS:12 site/mode sentinel
    ("vehicle",    0x13, 1),   # DS:13 vehicle code (0x28 = rocket)
    ("target_x",   0x23, 1),   # asm/2E3D target triple
    ("target_y",   0x24, 1),
    ("target_kind",0x25, 1),
    ("sector_hi",  0x49, 1),   # sector = hi*10 + lo (see AGENTS.md § 3)
    ("sector_lo",  0x4A, 1),
    ("food",       0x50, 2),
    ("hp",         0x51, 2),
    ("mon_x",      0x137, 32),
    ("mon_y",      0x157, 32),
    ("mon_hp",     0x177, 32),
    ("mon_type",   0x197, 32),
    ("mon_kind",   0x1B7, 32),
    ("mon_role",   0x1D7, 32),
]


def read_state_file():
    env = {}
    try:
        with open(STATE_FILE) as f:
            for line in f:
                line = line.strip()
                if "=" in line and not line.startswith("#"):
                    k, v = line.split("=", 1)
                    env[k] = v
    except FileNotFoundError:
        pass
    return env


def read_span(http_port, seg, off, n):
    """One /api/memory read; returns bytes. Same endpoint as spice86_type --peek."""
    phys = seg * 16 + off
    url = "http://127.0.0.1:%d/api/memory/%d/range/%d" % (http_port, phys, n)
    with urllib.request.urlopen(url, timeout=5) as r:
        return bytes(json.load(r)["values"])


def slice_fields(buf, fields):
    """Slice the named fields out of a contiguous DS span buffer."""
    out = {}
    base_off = fields_base(fields)
    for name, off, ln in fields:
        s = off - base_off
        out[name] = buf[s:s + ln].hex()
    return out


def fields_base(fields):
    return min(off for _, off, _ in fields) & ~0xF  # round down to a 16-byte line


def fields_span(fields):
    return max(off + ln for _, off, ln in fields) - fields_base(fields)


class Capture:
    def __init__(self, args, env):
        self.args = args

        def _port(a, k):
            v = a if a is not None else env.get(k)
            return int(v) if v not in (None, "") else None

        self.http = _port(args.http_port, "S86_HTTP_PORT")
        self.mcp = _port(args.mcp_port, "S86_MCP_PORT")
        self.gdb = _port(args.gdb_port, "S86_GDB_PORT")
        self.run_dir = args.md5_dir or env.get("S86_RUN_DIR")
        self.out = args.out
        self.fields = self._parse_fields(args.fields)
        self.span_off = fields_base(self.fields)
        self.span_len = max(args.span, fields_span(self.fields))
        self.stop = threading.Event()
        self.n_polls = 0
        self.n_changes = 0
        self.n_frames = 0
        self.n_md5 = 0
        self.bp_procs = []

    @staticmethod
    def _parse_fields(spec):
        if not spec:
            return list(DEFAULT_FIELDS)
        out = []
        for item in spec.split(","):
            name, rest = item.split("=")
            off, ln = rest.split(":")
            out.append((name.strip(), int(off, 16), int(ln, 16)))
        return out

    def log(self, msg):
        line = "%s %s" % (time.strftime("%H:%M:%S"), msg)
        with open(os.path.join(self.out, "capture.log"), "a") as f:
            f.write(line + "\n")
        print(line, flush=True)

    # ---- collector 1: change-triggered DS field logger -------------------
    def poll_loop(self):
        last = None
        hb = 0.0
        period = 1.0 / self.args.hz
        with open(os.path.join(self.out, "ds.jsonl"), "a") as f:
            while not self.stop.is_set():
                t0 = time.time()
                try:
                    buf = read_span(self.http, DS_SEG, self.span_off, self.span_len)
                except Exception as e:
                    self.log("DS poll error: %r" % (e,))
                    self.stop.wait(1.0)
                    continue
                cur = slice_fields(buf, self.fields)
                self.n_polls += 1
                changed = None
                if cur != last:
                    changed = [k for k in cur if last is None or cur[k] != last[k]]
                    self.n_changes += 1
                    last = cur
                now = time.time()
                if changed is not None or (now - hb) >= self.args.heartbeat:
                    hb = now
                    rec = {"t": round(now, 3), "seq": self.n_polls}
                    if changed is not None:
                        rec["changed"] = changed
                        rec["fields"] = cur
                    else:
                        rec["hb"] = True
                    f.write(json.dumps(rec) + "\n")
                    f.flush()
                self.stop.wait(max(0.0, period - (time.time() - t0)))

    # ---- collector 2: periodic content-file md5s -------------------------
    @staticmethod
    def _is_content(name):
        # Only the game's own files matter for a write/persist question. The
        # emulator's spice86.log grows every second and would flood the log.
        keep = ("mapx", "monx", "tlkx", "pic", "player", "PLAYER",
                "monsters", "ultimaii.exe", "u2")
        return any(name.startswith(k) or name == k for k in keep)

    def md5_loop(self):
        if not self.run_dir or not os.path.isdir(self.run_dir):
            self.log("md5: no run dir (%r); skipping" % (self.run_dir,))
            return
        last = {}
        while not self.stop.is_set():
            cur = {}
            for name in sorted(os.listdir(self.run_dir)):
                p = os.path.join(self.run_dir, name)
                if os.path.isfile(p) and (self.args.md5_all or self._is_content(name)):
                    try:
                        with open(p, "rb") as fh:
                            cur[name] = hashlib.md5(fh.read()).hexdigest()
                    except OSError:
                        pass
            changed = {k: (last.get(k), v) for k, v in cur.items() if last.get(k) != v}
            if changed and last:
                self.n_md5 += 1
                rec = {"t": round(time.time(), 3), "changed":
                       {k: {"was": w, "now": v} for k, (w, v) in changed.items()}}
                with open(os.path.join(self.out, "md5.log"), "a") as f:
                    f.write(json.dumps(rec) + "\n")
                self.log("md5 changed: %s" % ", ".join(changed))
            elif not last:
                with open(os.path.join(self.out, "md5.log"), "a") as f:
                    f.write(json.dumps({"t": round(time.time(), 3),
                                         "baseline": cur}) + "\n")
            last = cur
            self.stop.wait(self.args.md5_interval)

    # ---- collector 3: frames ---------------------------------------------
    def frame_loop(self):
        d = os.path.join(self.out, "frames")
        os.makedirs(d, exist_ok=True)
        i = 0
        while not self.stop.is_set():
            i += 1
            path = os.path.join(d, "%04d.png" % i)
            cmd = [sys.executable, os.path.join(ROOT, "tools", "spice86_screenshot.py"),
                   path]
            if self.mcp:
                cmd += ["--mcp-port", str(self.mcp)]
            try:
                subprocess.run(cmd, stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL, timeout=15)
                if os.path.exists(path):
                    self.n_frames += 1
            except Exception as e:
                self.log("frame error: %r" % (e,))
            self.stop.wait(self.args.frame_interval)

    # ---- collector 4: breakpoints (armed before the event) ---------------
    def arm_breakpoints(self):
        for ip in self.args.bp:
            logp = os.path.join(self.out, "bp-%s.log" % ip)
            cmd = [sys.executable, os.path.join(ROOT, "tools", "bp_read.py"),
                   "--ip", ip, "--cs", self.args.cs,
                   "--samples", str(self.args.bp_samples),
                   "--state", "--ret", "--dump", self.args.bp_dump]
            if self.http:
                cmd += ["--http-port", str(self.http)]
            if self.gdb:
                cmd += ["--gdb-port", str(self.gdb)]
            fh = open(logp, "a")
            self.log("arming breakpoint at %s:%s -> %s" % (self.args.cs, ip, logp))
            self.bp_procs.append(subprocess.Popen(cmd, stdout=fh, stderr=fh))

    def run(self):
        os.makedirs(self.out, exist_ok=True)
        # Fail loudly if there is no live session to read.
        try:
            read_span(self.http, DS_SEG, 0, 1)
        except Exception as e:
            sys.exit("no live Spice86 on http port %s (%r); start one with "
                     "tools/spice86_start.sh first" % (self.http, e))
        self.log("capture start: out=%s http=%s mcp=%s gdb=%s run_dir=%s "
                 "span=%x:%x hz=%g" % (self.out, self.http, self.mcp, self.gdb,
                                        self.run_dir, self.span_off,
                                        self.span_len, self.args.hz))
        self.arm_breakpoints()
        threads = [threading.Thread(target=self.poll_loop, daemon=True)]
        if self.args.frames:
            threads.append(threading.Thread(target=self.frame_loop, daemon=True))
        threads.append(threading.Thread(target=self.md5_loop, daemon=True))
        for t in threads:
            t.start()
        end = time.time() + self.args.duration if self.args.duration else None
        try:
            while not self.stop.is_set():
                if end and time.time() >= end:
                    break
                time.sleep(0.2)
        except KeyboardInterrupt:
            pass
        self.stop.set()
        for p in self.bp_procs:
            try:
                p.terminate()
            except Exception:
                pass
        self.log("capture done: polls=%d changes=%d frames=%d md5_rounds=%d"
                 % (self.n_polls, self.n_changes, self.n_frames, self.n_md5))


def selftest():
    """Offline control: field parsing, slicing and change-detection, no network.

    This is the demonstrable control for the tool: a wrong span base or a wrong
    slice would make these asserts fail, so the DS logger cannot silently emit
    misaligned fields.
    """
    fields = [("a", 0x00, 1), ("b", 0x10, 2)]
    base = fields_base(fields)
    assert base == 0x00, base
    buf = bytes(range(0x20))
    got = slice_fields(buf, fields)
    assert got["a"] == "00", got
    assert got["b"] == "1011", got
    # change detection
    f2 = [("x", 0x00, 1)]
    b1 = bytes([0x01])
    b2 = bytes([0x02])
    assert slice_fields(b1, f2) != slice_fields(b2, f2)
    assert slice_fields(b1, f2) == slice_fields(bytes([0x01]), f2)
    # default set must fit inside the default span
    dspan = fields_span(DEFAULT_FIELDS)
    assert dspan <= 0x200, dspan
    print("selftest ok: %d default fields, span=%d bytes, slicing+change verified"
          % (len(DEFAULT_FIELDS), dspan))


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", help="output dir (write under /var/tmp)")
    ap.add_argument("--http-port", type=int)
    ap.add_argument("--mcp-port", type=int)
    ap.add_argument("--gdb-port", type=int)
    ap.add_argument("--fields", help="name=off:len (hex), comma list; default = u2 state set")
    ap.add_argument("--span", type=lambda x: int(x, 16), default=0x200,
                    help="DS span length in hex to read per poll (default 0x200)")
    ap.add_argument("--hz", type=float, default=20.0, help="DS poll rate (default 20)")
    ap.add_argument("--heartbeat", type=float, default=5.0,
                    help="seconds between heartbeat lines when nothing changed")
    ap.add_argument("--frames", action="store_true", help="also grab 1/sec frames")
    ap.add_argument("--frame-interval", type=float, default=1.0)
    ap.add_argument("--md5-dir", help="dir to md5 (default: run dir from session env)")
    ap.add_argument("--md5-all", action="store_true",
                    help="md5 every file in the dir, not just game content files")
    ap.add_argument("--md5-interval", type=float, default=2.0)
    ap.add_argument("--bp", action="append", default=[],
                    help="runtime IP offset to arm a breakpoint on (repeatable)")
    ap.add_argument("--cs", default="17D", help="code segment for --bp (default 17D)")
    ap.add_argument("--bp-samples", type=int, default=8)
    ap.add_argument("--bp-dump", default="23:3",
                    help="DS OFF:LEN to dump at each breakpoint hit (default 23:3)")
    ap.add_argument("--duration", type=float, default=0,
                    help="stop after N seconds (default: run until killed)")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()

    if args.selftest:
        selftest()
        return
    if not args.out:
        ap.error("--out is required (or use --selftest)")
    env = read_state_file()
    cap = Capture(args, env)
    signal.signal(signal.SIGTERM, lambda *_: cap.stop.set())
    cap.run()


if __name__ == "__main__":
    main()

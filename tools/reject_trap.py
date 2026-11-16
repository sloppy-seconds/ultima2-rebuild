#!/usr/bin/env python3
"""
reject_trap.py — arm and detect the game's reject path, precisely and countably.

Item G0 of docs/needs-gameplay.md. The problem this solves
---------------------------------------------------------
A passability probe must tell three outcomes apart after sending a movement key:
the step MOVED, the step was REJECTED, or the keystroke was DROPPED. Position
alone cannot: a rejected step and a dropped key both leave the character where it
was. Conflating them is what invalidated the first passability sweep — nine
capture directories of confidently wrong terrain data.

The discriminator is the reject path itself. asm/0A02_invalid_move_msg prints
`--INVALID MOVE!`; tools/cfg_printed.py confirms that call genuinely executes on
rejected steps. Trap it and "rejected" stops being an inference.

Four things measured on 2026-10-22 that shaped this tool
-------------------------------------------------------
1. **`ExecutedInstructions` is a deduplicated address set, not a trace.** The
   reject offset appears exactly ONCE in every dump examined (mountain-walk,
   play-test-live, create-test-live), regardless of how many rejects occurred. So
   post-hoc per-step reject counting from existing dumps is impossible. The trap
   must be live.
2. **`DS:20` is not a reject signal.** engine/src/core/state.h says so explicitly
   ("Treat as scratch; do not use as a reject signal"). turn_recorder.py was
   using it and has been corrected.
3. **gdb(1) with a piped stdin emits nothing at all** — no banner, no prompt, no
   output — so a subprocess driver cannot tell whether anything happened, and a
   pty would be needed. Worse, `gdb -batch` cannot arm and wait anyway: the target
   is RUNNING on attach and gdb refuses `continue` with "Cannot execute this
   command while the target is running". So this module speaks the remote protocol
   directly. Four packet types is all it takes.
4. **`monitor breakCsIp` parses its arguments as HEX while /api/status reports
   DECIMAL.** Passing the API's decimal cs/ip silently arms a trap on an address
   that can never execute, and every subsequent "no reject observed" becomes
   meaningless. This is the single easiest way to get a confident wrong answer
   here, so the conversion is done in one place, below, and asserted.

Validated 2026-10-22: arming on the address the CPU was already executing returned
a stop (`TRAP FIRED: True`) with no gameplay involved — a positive control for the
mechanism itself.

Read-only with respect to guest memory: it uses the emulator's own breakCsIp and
never patches guest bytes, so it cannot corrupt the byte-exact comparison the
project depends on.
"""
from __future__ import annotations

import re
import socket
import time

# Reject print site. Verified by locating the file bytes at original/ultimaii.exe
# offset 0x0C02 inside spice86dumpMemoryDump.bin of three separate captures:
# linear 0x21D2 every time, i.e. CS 0x17D : IP 0x0A02.
REJECT_CS_HEX = "17D"
REJECT_IP_HEX = "0A02"

DS_SEG = 0x08BE


def cs_ip_from_status(cs_dec: int, ip_dec: int) -> tuple[str, str]:
    """Convert /api/status decimal cs/ip to the hex form breakCsIp requires.

    Kept as its own function and asserted because the mismatch is silent: feed it
    decimal and the trap never fires, which reads exactly like 'no rejects
    happened'.
    """
    cs_hex, ip_hex = f"{cs_dec:X}", f"{ip_dec:X}"
    assert int(cs_hex, 16) == cs_dec and int(ip_hex, 16) == ip_dec, "conversion broke"
    return cs_hex, ip_hex


class RSP:
    """Just enough GDB remote serial protocol for monitor commands and stops."""

    def __init__(self, port: int, host: str = "127.0.0.1", timeout: float = 10.0):
        self.s = socket.create_connection((host, port), timeout=timeout)
        self.s.settimeout(2.0)
        self.buf = b""

    @staticmethod
    def _frame(payload: str) -> bytes:
        return f"${payload}#{sum(payload.encode()) & 0xFF:02x}".encode()

    def _send(self, payload: str) -> None:
        self.s.sendall(self._frame(payload))

    def _recv_packet(self, timeout: float):
        self.s.settimeout(timeout)
        try:
            while True:
                try:
                    chunk = self.s.recv(4096)
                except socket.timeout:
                    return None
                if not chunk:
                    return None
                self.buf += chunk
                while b"$" in self.buf:
                    i = self.buf.index(b"$")
                    if b"#" not in self.buf[i:]:
                        break
                    j = self.buf.index(b"#", i)
                    data = self.buf[i + 1:j].decode("latin-1")
                    self.buf = self.buf[j + 3:]
                    self.s.sendall(b"+")     # ack
                    return data
        except socket.timeout:
            return None

    def drain_pending(self, quiet: float = 1.0, max_packets: int = 64) -> list[str]:
        """Read and discard whatever is already in flight on the socket.

        A caller that learns about a stop from /api/status instead of the RSP stream
        never consumes the `T...` stop reply, and it sits in the socket buffer. Send a
        monitor command straight after that and `monitor()` returns the stale stop
        packet as if it were the reply — which decodes to garbage, not to an error.
        Draining first is what makes a monitor reply trustworthy after an HTTP-detected
        stop.

        `quiet` is how long to wait for a packet before deciding nothing is pending.
        """
        out: list[str] = []
        self.s.settimeout(quiet)
        while len(out) < max_packets:
            p = self._recv_packet(quiet)
            if p is None:
                break
            out.append(p)
        return out

    def monitor(self, cmd: str, drain: float = 2.0) -> str:
        """Send `monitor <cmd>`. Spice86 returns monitor text hex-encoded.

        Fine while the target is RUNNING and only a short ack is expected (that is how
        breakCsIp is armed). Do not use it on a halted target — see monitor_collect.
        """
        self._send("qRcmd," + cmd.encode().hex())
        out = []
        t0 = time.time()
        while time.time() - t0 < drain:
            p = self._recv_packet(0.5)
            if p is None:
                break
            out.append(p)
            if p.startswith(("OK", "T", "S", "E")):
                break
        return " | ".join(out)

    def monitor_collect(self, cmd: str, timeout: float = 8.0, quiet: float = 1.2,
                        nudge: bool = True) -> list[str]:
        """Send `monitor <cmd>` against a HALTED target and collect the whole reply.

        `monitor()` is wrong once the VM is stopped at a breakpoint, and the wrongness is
        silent: the halt emits stop notifications (`T05`, `S05`) which `monitor()` reads
        as its terminator, so the real monitor text arrives after the call returns and is
        attributed to the *next* command. Measured 2026-10-28: `state` answered `S05` and
        `callStack` came back holding the register dump. A hex-encoded payload can also
        begin with `E`, which `monitor()` treats as an error reply.

        **Spice86 only writes monitor output when it next reads from the link.** Traced
        2026-10-28 on a VM halted at 17D:0CEE: `qRcmd,state` produced nothing for 20 s;
        a bare `?` packet 4 s later produced the register dump immediately; `c` produced
        the reply to the `peekRet` sent before it. So every reply is one packet late, and
        the fix is to send a nudge packet after the command and read until quiet.
        (`?` is safe only because the target is already halted; while running it would
        pause the VM, hence the flag.)
        """
        self._send("qRcmd," + cmd.encode().hex())
        out: list[str] = []
        t0 = time.time()
        last = t0
        nudged = False
        while time.time() - t0 < timeout:
            p = self._recv_packet(0.2)
            if p is None:
                if nudge and not nudged and time.time() - last > 1.0:
                    self._send("?")
                    nudged = True
                    continue
                if out and time.time() - last > quiet:
                    break
                continue
            last = time.time()
            if p == "OK":
                break
            if re.fullmatch(r"[TS][0-9A-Fa-f]{2}", p):
                continue
            out.append(p)
        return out

    @staticmethod
    def decode_packets(packets: list[str]) -> str:
        """Decode a monitor reply: Spice86 sends the text hex-encoded, one packet per line."""
        text = []
        for p in packets:
            try:
                text.append(bytes.fromhex(p).decode("utf-8", "replace"))
            except ValueError:
                text.append(p)
        return "\n".join(text).strip()

    @staticmethod
    def decode_monitor(reply: str) -> str:
        """qRcmd replies carry hex-encoded monitor output."""
        text = []
        for part in reply.split(" | "):
            try:
                text.append(bytes.fromhex(part).decode("utf-8", "replace").strip())
            except ValueError:
                text.append(part)
        return "\n".join(t for t in text if t)

    def continue_(self) -> None:
        self._send("c")

    def wait_stop(self, timeout: float) -> bool:
        """True if the target reported a stop within timeout, else False."""
        t0 = time.time()
        while time.time() - t0 < timeout:
            p = self._recv_packet(max(0.1, timeout - (time.time() - t0)))
            if p and p.startswith(("T", "S")):
                return True
        return False

    def close(self) -> None:
        try:
            self.s.close()
        except Exception:
            pass


def arm_reject_trap(conn: RSP) -> bool:
    """Trap the reject print site. Returns True if the emulator accepted it."""
    reply = conn.monitor(f"breakCsIp {REJECT_CS_HEX} {REJECT_IP_HEX}", 2.0)
    return "Breakpoint added" in conn.decode_monitor(reply)


if __name__ == "__main__":
    import argparse
    import json
    import urllib.error
    import urllib.request
    import sys

    ap = argparse.ArgumentParser(description="arm and validate the reject trap")
    ap.add_argument("--http-port", type=int, default=None,
                    help="default: the live session's recorded port (see tools/s86_ports.py)")
    ap.add_argument("--gdb-port", type=int, default=None,
                    help="default: the live session's recorded port (see tools/s86_ports.py)")
    ap.add_argument("--selftest", action="store_true",
                    help="prove the trap mechanism fires, using the address the "
                         "CPU is already executing. Skips cleanly with no emulator.")
    a = ap.parse_args()
    if a.http_port is None or a.gdb_port is None:
        import s86_ports          # sys.path carries tools/ when run as tools/<name>.py
        a.http_port = s86_ports.resolve("http", a.http_port)
        a.gdb_port = s86_ports.resolve("gdb", a.gdb_port)

    if not a.selftest:
        print("nothing to do offline; use --selftest against a live run "
              "(tools/spice86_start.sh --headless --gdb-port ...)")
        sys.exit(0)

    try:
        with urllib.request.urlopen(
                f"http://127.0.0.1:{a.http_port}/api/status", timeout=4) as r:
            st = json.load(r)
    except (urllib.error.URLError, OSError):
        # No emulator: skip, do not fail. A control that cannot run must not turn
        # the gate red, but it must say it did not run.
        print("SELFTEST SKIPPED: no Spice86 on port "
              f"{a.http_port}; start one with tools/spice86_start.sh --headless")
        sys.exit(0)

    cs_hex, ip_hex = cs_ip_from_status(st["cs"], st["ip"])
    c = RSP(a.gdb_port)
    try:
        # Positive control: trap where the CPU already is. It must stop almost
        # immediately, and this needs no gameplay and no judgement.
        armed = "Breakpoint added" in c.decode_monitor(
            c.monitor(f"breakCsIp {cs_hex} {ip_hex}", 2.0))
        c.continue_()
        fired = c.wait_stop(6.0)
        ok = armed and fired
        print(f"SELFTEST {'PASSED' if ok else 'FAILED'}: armed={armed} "
              f"trap_fired={fired} (cs:ip {cs_hex}:{ip_hex})")
        sys.exit(0 if ok else 1)
    finally:
        c.close()

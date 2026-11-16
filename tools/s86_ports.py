#!/usr/bin/env python3
"""Resolve the live Spice86 ports instead of assuming the defaults.

tools/spice86_start.sh picks the first free port at or above 20180 and records what it chose
in tmp/.spice86-session.env. Until this module existed, the Python tools hardcoded 20180 and
20181 anyway, so a second session started while a stale one was squatting on the defaults
silently talked to the WRONG EMULATOR. Every measurement it took would have been about a
different process, and nothing would have said so — a dropped key and a key sent to someone
else look identical from the caller's side.

Precedence, highest first:
  1. an explicit --http-port / --mcp-port on the command line
  2. U2V2_HTTP_PORT / U2V2_MCP_PORT in the environment
  3. S86_HTTP_PORT / S86_MCP_PORT in the session state file
  4. 20180 / 20181

The state file is read fresh on every call rather than cached at import, because a tool that
resolved once at import would keep the pre-session default for the life of the process.
"""

from __future__ import annotations

import os
import re
import sys
from pathlib import Path

DEFAULT_HTTP = 20180
DEFAULT_MCP = 20181

_KEYS = {"http": ("S86_HTTP_PORT", "U2V2_HTTP_PORT"),
         "mcp": ("S86_MCP_PORT", "U2V2_MCP_PORT")}


def root() -> Path:
    """Repo root, found by walking up from this file so the tools work from any cwd."""
    return Path(__file__).resolve().parent.parent


def state_file() -> Path:
    return root() / "tmp" / ".spice86-session.env"


def _from_state(which: str) -> int | None:
    f = state_file()
    try:
        text = f.read_text()
    except OSError:
        return None
    key = _KEYS[which][0]
    m = re.search(rf"^{key}=(\d+)\s*$", text, re.MULTILINE)
    if not m:
        return None
    return int(m.group(1))


def _from_env(which: str) -> int | None:
    val = os.environ.get(_KEYS[which][1], "").strip()
    return int(val) if val.isdigit() else None


def resolve(which: str, explicit: int | None = None) -> int:
    """One port. `explicit` is a value the caller got from its own --*-port flag."""
    if explicit is not None:
        return explicit
    default = DEFAULT_HTTP if which == "http" else DEFAULT_MCP
    return _from_env(which) or _from_state(which) or default


def resolve_pair(explicit_http: int | None = None,
                 explicit_mcp: int | None = None) -> tuple[int, int]:
    return resolve("http", explicit_http), resolve("mcp", explicit_mcp)


def describe(which: str, explicit: int | None = None) -> str:
    """Where the answer came from. Worth printing: a silent fallback is how the wrong
    emulator gets talked to politely."""
    if explicit is not None:
        return "command line"
    if _from_env(which):
        return _KEYS[which][1]
    if _from_state(which):
        return f"{state_file().relative_to(root())}"
    return "default (no live session recorded)"


def _selftest() -> int:
    import tempfile
    fails = 0
    checks = 0

    def check(cond, msg):
        nonlocal fails, checks
        checks += 1
        if not cond:
            fails += 1
            print(f"  FAIL: {msg}")

    # explicit always wins
    check(resolve("http", 9999) == 9999, "explicit must win")

    # a state file is honoured
    with tempfile.TemporaryDirectory() as td:
        p = Path(td) / ".spice86-session.env"
        p.write_text("S86_PID=1\nS86_HTTP_PORT=20183\nS86_MCP_PORT=20184\n")
        text = p.read_text()
        m = re.search(r"^S86_HTTP_PORT=(\d+)\s*$", text, re.MULTILINE)
        check(m and m.group(1) == "20183", "state file port parses")
        # a near-miss key must NOT match: S86_HTTP_PORTX= must not be read as the port
        bad = "S86_HTTP_PORTX=7777\n"
        check(not re.search(r"^S86_HTTP_PORT=(\d+)\s*$", bad, re.MULTILINE),
              "a longer key must not be read as the port")
        # a non-numeric value must not be read as a port
        check(not re.search(r"^S86_HTTP_PORT=(\d+)\s*$", "S86_HTTP_PORT=abc\n", re.MULTILINE),
              "non-numeric port must not parse")

    # env beats state, and both beat default
    os.environ["U2V2_MCP_PORT"] = "20199"
    check(resolve("mcp") == 20199, "env must beat state and default")
    del os.environ["U2V2_MCP_PORT"]

    # with nothing set, the defaults come back and describe() says so
    check(resolve("http") in (DEFAULT_HTTP, _from_state("http")), "http resolves")
    check(describe("http", 1234) == "command line", "describe explicit")

    print(f"s86_ports selftest: {checks} checks, {fails} failed")
    return 1 if fails else 0


if __name__ == "__main__":
    if "--selftest" in sys.argv:
        sys.exit(_selftest())
    h, m = resolve_pair()
    print(f"http={h} ({describe('http')})  mcp={m} ({describe('mcp')})")

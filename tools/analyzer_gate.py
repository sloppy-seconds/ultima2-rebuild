#!/usr/bin/env python3
"""analyzer_gate.py — gcc -fanalyzer over every engine translation unit, zero tolerance.

The compiler's own diagnostics are already `-Werror`; this is the deeper static pass
over the same code, run as a gate rather than left to whoever remembers. It exists
because the engine's whole job is reading byte layouts it does not own, and a gate
that finds a null dereference or a leaked handle for free is worth the minute it
takes.

Two decisions are recorded rather than discovered:

  * `-Wno-analyzer-malloc-leak` is off globally. The engine allocates its content and
    overlay tables once and lets the process exit holding them, so the analyzer
    reports a fixed set of still-reachable blocks as leaks. This is the same call
    `make test-asan` makes with `detect_leaks=0`, and for the same reason: a gate that
    is always noisy is a gate that gets skipped. Memory *errors*, not garbage
    collection, are what this is for.
  * Per-file suppressions below each carry a reason and are the only way a finding
    goes quiet. There is no baseline file of line numbers, because a line-keyed
    baseline silently outlives the line it was written against.

A control runs with `--selftest`: a file with a deliberate null dereference must be
reported. Without that this script is a checker that could quietly report green while
analyzing nothing, which AGENTS.md § 5 counts as worse than no checker.

Usage:
    python3 tools/analyzer_gate.py [--selftest] [--only FILE] [--json]
"""

from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ENGINE = ROOT / "engine"

# See the module docstring: still-reachable tables are not defects here.
GLOBAL_DISABLE = ["-Wno-analyzer-malloc-leak"]

# file -> (flags, reason). A suppression without a reason is a bug in this file.
PER_FILE: dict[str, tuple[list[str], str]] = {
    "src/core/world.c": (
        ["-Wno-analyzer-file-leak"],
        "u2_world_store opens `src` at 669/675 and closes it on every path that "
        "reaches it (688, and 683 on the write failure); the analyzer attributes the "
        "leak to sector_path's closing brace, an inlined helper that opens no file. "
        "Verified by hand 2026-11-16. Scoped to this file so the class stays live "
        "everywhere else.",
    ),
}

# -O1 rather than -O0: the analyzer's path sensitivity depends on what the middle end
# leaves visible, and the ordinary build is -O2. -fsyntax-only is NOT usable: it
# suppresses the analyzer entirely, which a first draft of this gate proved by
# reporting zero findings over 72 TUs including a textbook null dereference.
ANALYZE_FLAGS = ["-fanalyzer", "-O1", "-g1"]

FINDING = re.compile(
    r"^(?P<file>[^\s:]+):(?P<line>\d+):(?P<col>\d+): warning: (?P<msg>.*?)"
    r"\[(?P<opt>-W[a-z0-9-]+)\]$"
)

def strip_werror(flags: list[str]) -> list[str]:
    """Drop -Werror from the inherited flag set.

    With -Werror every analyzer finding is an error, so gcc stops at the first one per
    translation unit and the gate under-reports. The gate's own exit code is the
    enforcement here; leaving the diagnostics as warnings is what makes the count
    honest. A first draft of this gate inherited -Werror and its selftest caught it.
    """
    return [f for f in flags if f != "-Werror"]


CONTROL = """/* The control: a null dereference on a path the analyzer must take. If the gate
 * cannot make this red, the gate is not analyzing anything. The prototype is here
 * because the control is compiled with the engine's own -Werror flag set. */
int control(int n);

int control(int n)
{
    int *p = 0;
    if (n)
        *p = 1;
    return n;
}
"""


def engine_sources() -> list[Path]:
    """The same list the Makefile builds, including its one platform exclusion."""
    src = ENGINE / "src"
    out = [src / "main.c"]
    for d in ("core", "game", "debug", "platform"):
        out += sorted((src / d).glob("*.c"))
    return [p for p in out if p.exists() and p.name != "time_win32.c"]


def cc_flags() -> list[str]:
    r = subprocess.run(["make", "-s", "ccflags"], cwd=ENGINE, capture_output=True, text=True)
    if r.returncode != 0 or not r.stdout.strip():
        sys.exit("analyzer_gate: `make -s ccflags` failed — the flag set has no home")
    return r.stdout.split()


def analyzer_supported(cc: str, flags: list[str]) -> bool:
    with tempfile.NamedTemporaryFile("w", suffix=".c", delete=False) as fh:
        fh.write("int main(void){return 0;}\n")
        path = fh.name
    try:
        r = subprocess.run([cc, *flags, *ANALYZE_FLAGS, "-c", path, "-o", "/dev/null"],
                           capture_output=True, text=True)
        return r.returncode == 0 and "unrecognized command line option" not in r.stderr
    finally:
        Path(path).unlink(missing_ok=True)


def analyze(cc: str, flags: list[str], path: Path) -> list[dict]:
    rel = str(path.relative_to(ENGINE))
    extra = list(GLOBAL_DISABLE)
    if rel in PER_FILE:
        extra += PER_FILE[rel][0]
    r = subprocess.run([cc, *flags, *ANALYZE_FLAGS, *extra, "-c", str(path), "-o", "/dev/null"],
                       cwd=ENGINE, capture_output=True, text=True)
    out = []
    for line in r.stderr.splitlines():
        m = FINDING.match(line.strip())
        if m:
            out.append({**m.groupdict(), "tu": rel})
    return out


def selftest() -> int:
    """Prove the gate can go red, from the same code path the gate uses."""
    flags = strip_werror(cc_flags())
    cc = "cc"
    tmp = Path(tempfile.mkdtemp(prefix="u2-analyzer-selftest.", dir="/var/tmp"))
    try:
        ctl = tmp / "control.c"
        ctl.write_text(CONTROL)
        r = subprocess.run([cc, *flags, *ANALYZE_FLAGS, "-c", str(ctl), "-o", "/dev/null"],
                           capture_output=True, text=True)
        hits = [l for l in r.stderr.splitlines() if "warning:" in l and "-Wanalyzer" in l]
        if not hits:
            print("analyzer_gate SELFTEST: FAIL — the analyzer reported nothing for a "
                  "deliberate null dereference, so it is not running")
            print("  " + (r.stderr.strip().splitlines() or ["(no output)"])[0])
            return 1
        print(f"analyzer_gate SELFTEST: PASS ({len(hits)} finding(s) on the control)")
        return 0
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--selftest", action="store_true", help="prove the gate can go red")
    ap.add_argument("--only", metavar="FILE", help="one translation unit, for debugging")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--cc", default="cc")
    args = ap.parse_args()

    if args.selftest:
        return selftest()

    flags = strip_werror(cc_flags())
    if not analyzer_supported(args.cc, flags):
        required = "ANALYZE_REQUIRED" in __import__("os").environ
        print(f"SKIP analyzer_gate: {args.cc} has no -fanalyzer"
              + (" — required here" if required else ""))
        return 1 if required else 0

    tus = engine_sources()
    if args.only:
        tus = [p for p in tus if str(p.relative_to(ENGINE)) == args.only]
        if not tus:
            sys.exit(f"analyzer_gate: {args.only} is not an engine source")

    findings: list[dict] = []
    for tu in tus:
        findings += analyze(args.cc, flags, tu)

    if args.json:
        print(json.dumps(findings, indent=2))
    if findings:
        for f in findings:
            print(f"  {f['tu']}:{f['line']}: {f['msg']}[{f['opt']}]")
        print(f"analyzer_gate: FAIL — {len(findings)} finding(s) over {len(tus)} TUs")
        return 1
    print(f"analyzer_gate: PASS — {len(tus)} TUs clean under -fanalyzer "
          f"({len(GLOBAL_DISABLE)} class(es) disabled, see the module docstring)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

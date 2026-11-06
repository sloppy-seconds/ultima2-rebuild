#!/usr/bin/env python3
"""demo.py — inspect, verify, diff and convert .demo files.

Tooling, not the engine: the engine records and plays, and everything a person does
*after* that belongs here (docs/demo-record-playback.md § 6). The digests a demo
carries are sha256 over bytes the engine defines, and this file reproduces the
state digest only where it can do so honestly — it never re-derives a verdict the
engine is better placed to give, which is why `verify` runs the engine rather than
reimplementing playback in Python. Reimplementing it here would be a second copy of
the rules, and a second copy of the rules is what this project keeps refusing.

Subcommands
  inspect      the header, the record summary, and whether the file is self-consistent
  verify       run the engine's playback and report its verdict
  against-trace  W3: the recorded keys are exactly the keys the engine delivered
  diff         first divergence between two demos, or between a demo and a trace
  export-keys  the key script and the checkpoint, as a .keys file plus a .ds file
  from-playlog convert a playlog.py report's key list into a .keys script
  corpus       W14: replay every demo in a directory and report per file

Usage: demo.py SUBCOMMAND ...      (each subcommand takes -h)
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys

MAGIC = "udemo"
REC_RE = re.compile(r"^@rec (\d+) kind=(\w+) keys=([0-9a-f:,]*) rolls=(\d+) sha=([0-9a-f]{64})")


class Demo:
    """A parsed .demo. Deliberately shallow: the engine is the authority on what the
    records mean, and this only needs to answer questions about the text."""

    def __init__(self, path):
        self.path = path
        with open(path) as f:
            self.lines = f.readlines()
        self.header = {}
        self.records = []          # (n, kind, [(ch, scan)], rolls, sha)
        self.checkpoint = b""
        self.checkpoint_sha = ""
        self.end = {}
        self.overlays = []
        self.malformed = []
        self._parse()

    def _parse(self):
        in_ckpt = False
        payload = []
        for line in self.lines:
            if line.startswith("@checkpoint "):
                in_ckpt = True
                payload = []
                for k, v in re.findall(r"(\w+)=(\S*)", line):
                    self.header[k] = v
                continue
            if line.startswith("@checkpoint-sha "):
                self.checkpoint_sha = line.split()[1]
                self.checkpoint = bytes.fromhex("".join(p.strip() for p in payload))
                in_ckpt = False
                continue
            if in_ckpt and line.startswith("  "):
                payload.append(line)
                continue
            if line.startswith("@rec "):
                m = REC_RE.match(line.rstrip("\n"))
                if not m:
                    # Recorded, not raised: an edited file is the case this tool has
                    # to be able to talk about, and a traceback is not a verdict.
                    self.malformed.append(line.rstrip("\n"))
                    continue
                keys = []
                if m.group(3):
                    for pair in m.group(3).split(","):
                        keys.append((int(pair[:2], 16), int(pair[3:5], 16)))
                self.records.append((int(m.group(1)), m.group(2), keys,
                                     int(m.group(4)), m.group(5)))
                continue
            if line.startswith("@end"):
                self.end = dict(re.findall(r"(\w+)=(\S+)", line))
                continue
            parts = line.split(None, 1)
            if len(parts) == 2 and not line.startswith(("@", " ", "#")):
                if parts[0] == "overlay":
                    self.overlays.append((parts[1].split()[0], parts[1].split()[1]))
                self.header.setdefault(parts[0], parts[1].strip())

    def framing_ok(self):
        """@end's sha covers every byte above it. Two digests, two jobs: this one is
        the file's integrity, @checkpoint-sha is the payload's, and a record's sha is
        a state claim. Checking only one of the three is a file where a corrupted
        checkpoint replays as a divergent demo."""
        if not self.end.get("sha"):
            return None
        h = hashlib.sha256()
        for line in self.lines:
            if line.startswith("@end"):
                break
            h.update(line.encode())
        return h.hexdigest() == self.end["sha"]

    def checkpoint_ok(self):
        if not self.checkpoint_sha:
            return None
        return hashlib.sha256(self.checkpoint).hexdigest() == self.checkpoint_sha

    def keys(self):
        out = []
        for _, kind, keys, _, _ in self.records:
            if kind == "key":
                out.extend(ch for ch, _ in keys)
        return out


# --------------------------------------------------------------------------

def cmd_inspect(args):
    d = Demo(args.demo)
    print(f"file            {d.path}")
    for key in ("engine", "digest-version", "content", "mode", "sector", "span",
                "exclude", "art", "theme", "enhanced"):
        if key in d.header:
            print(f"{key:<16} {d.header[key]}")
    print("overlay         " + (", ".join(f"{n} {s[:12]}" for n, s in d.overlays)
                                or "(none)"))
    kinds = {}
    for _, kind, _, _, _ in d.records:
        kinds[kind] = kinds.get(kind, 0) + 1
    print(f"records         {len(d.records)} " +
          "(" + ", ".join(f"{k}={v}" for k, v in sorted(kinds.items())) + ")")
    print(f"checkpoint      {len(d.checkpoint)} bytes, "
          f"self-check {'ok' if d.checkpoint_ok() else 'FAILED'}")
    fr = d.framing_ok()
    print(f"@end framing    {'ok' if fr else 'FAILED' if fr is False else 'absent'}")
    if d.malformed:
        print(f"malformed      {len(d.malformed)} record line(s) this version cannot "
              f"parse, first: {d.malformed[0][:60]}")
    if fr is False or d.checkpoint_ok() is False or d.malformed:
        print("VERDICT: the file does not match its own digests. It is corrupt, and "
              "the engine will refuse it.")
        return 1
    return 0


def cmd_verify(args):
    """Ask the engine. A verdict computed here would be a second implementation of
    playback, and the whole point of the digest is that only one implementation gets
    to say whether the state matched."""
    cmd = [args.engine, "--headless", "--playback", args.demo]
    if args.data_dir:
        cmd += ["--data-dir", args.data_dir]
    if args.salvage:
        cmd.append("--salvage")
    cmd += list(args.engine_arg)
    proc = subprocess.run(cmd, capture_output=True, text=True)
    sys.stdout.write(proc.stdout)
    sys.stderr.write(proc.stderr)
    tail = [l for l in proc.stderr.splitlines() if "DEMO" in l or "DIVERGED" in l]
    if tail:
        print(tail[-1])
    return proc.returncode


def cmd_against_trace(args):
    """W3: the demo must hold exactly the keys the engine delivered, in order, and
    must not hold one the game never consumed."""
    d = Demo(args.demo)
    trace = open(args.trace).read()
    # The trace prints the byte with %#X, so the hex letters come back upper case.
    delivered = [int(m, 16) for m in
                 re.findall(r"key_(?:poll|wait).*?ready=1 ch=0x([0-9a-fA-F]{2})", trace)]
    recorded = d.keys()
    if not recorded:
        print("the demo holds no key records")
        return 1
    # The fixture's script is exactly the recording window, so the two lists must be
    # equal and not merely compatible: a demo that dropped a delivered key, or invented
    # one, fails here. WAIT tokens make the seam answer "no key this poll" and must not
    # appear on either side, which is the engine's version of "a key the game never
    # consumed" — the measured case in docs/plan-cross-game-replay.md § 7a.
    if recorded != delivered:
        print(f"recorded keys are not the delivered ones: {recorded} vs {delivered}")
        return 1
    if 0x13 in recorded:
        print("the demo recorded 0x13, which the seam swallows before game logic")
        return 1
    print(f"{len(recorded)} recorded keys, exactly the {len(delivered)} the engine "
          f"delivered, in order")
    return 0


def cmd_diff(args):
    a = Demo(args.demo)
    if args.against:
        b = Demo(args.against)
        n = max(len(a.records), len(b.records))
        for i in range(n):
            ra = a.records[i] if i < len(a.records) else None
            rb = b.records[i] if i < len(b.records) else None
            if ra is None or rb is None:
                print(f"record {i+1}: present in one file only "
                      f"({len(a.records)} vs {len(b.records)})")
                return 1
            if ra[4] != rb[4]:
                print(f"record {i+1}: first divergence")
                print(f"  {os.path.basename(a.path)}: kind={ra[1]} keys={ra[2]} rolls={ra[3]}")
                print(f"  {os.path.basename(b.path)}: kind={rb[1]} keys={rb[2]} rolls={rb[3]}")
                return 1
        print(f"no divergence: {len(a.records)} records agree")
        return 0
    for _, kind, keys, rolls, sha in a.records:
        ks = ",".join(f"{c:02x}:{s:02x}" for c, s in keys)
        print(f"{kind:<5} keys={ks:<12} rolls={rolls:<6} {sha[:16]}")
    return 0


def cmd_export_keys(args):
    d = Demo(args.demo)
    with open(args.keys_out, "w") as f:
        f.write("# Exported from %s by engine/tools/demo.py.\n" % os.path.basename(d.path))
        f.write("# This replays the recorded input only. The starting state is the\n")
        f.write("# demo's checkpoint, not this file; load it with --load-ds.\n")
        for _, kind, keys, _, _ in d.records:
            if kind == "auto":
                f.write("WAIT\n")     # the closest a script can come to an idle turn
                continue
            for ch, _ in keys:
                f.write(f"0x{ch:02x}\n")
    with open(args.ds_out, "w") as f:
        f.write(d.checkpoint.hex())
    print(f"wrote {args.keys_out} ({len(d.keys())} keys) and {args.ds_out} "
          f"({len(d.checkpoint)} bytes of checkpoint)")
    return 0


def cmd_from_playlog(args):
    """Convert a playlog.py report's key list to a .keys script.

    The refusal is the whole point of the control: a report whose key count does not
    match its own steps is a report that was truncated or hand-edited, and converting
    it anyway would produce a script that looks derived from the evidence and is not.
    """
    rep = json.load(open(args.report))
    keys = rep.get("keys") or []
    steps = rep.get("steps") or []
    if rep.get("steps_truncated"):
        print("refusing: the report says its steps were truncated, so its key list is "
              "not the one that produced them", file=sys.stderr)
        return 1
    if keys:
        last = max(int(s.get("at_key", 0)) for s in steps) if steps else 0
        if last and last > len(keys):
            print(f"refusing: a step claims it happened at key {last} but the report "
                  f"lists only {len(keys)} keys", file=sys.stderr)
            return 1
    with open(args.keys_out, "w") as f:
        for tok in keys:
            f.write(tok + "\n")
    print(f"wrote {args.keys_out} ({len(keys)} keys)")
    return 0


def cmd_corpus(args):
    """W14: replay a directory of demos and report each one.

    This is a tool for a person, not a CI gate, and it says so: a corpus of
    playthroughs of copyrighted content cannot be committed, and a gate that silently
    skips when the corpus is absent is the defect AGENTS.md § 3 warns about. So it
    refuses to run on an empty or absent directory rather than reporting success.
    """
    if not os.path.isdir(args.dir):
        print(f"corpus: {args.dir} is not a directory. This is a local regression set a\n"
              f"person assembles; it is not built in CI and it will not pretend to.",
              file=sys.stderr)
        return 2
    files = sorted(f for f in os.listdir(args.dir) if f.endswith(".demo"))
    if not files:
        print(f"corpus: {args.dir} holds no .demo files", file=sys.stderr)
        return 2
    bad = 0
    for name in files:
        path = os.path.join(args.dir, name)
        cmd = [args.engine, "--headless", "--playback", path]
        if args.data_dir:
            cmd += ["--data-dir", args.data_dir]
        cmd += list(args.engine_arg)
        proc = subprocess.run(cmd, capture_output=True, text=True)
        # A refusal is as much an outcome as a divergence, and it is the one a person
        # needs named: "exit 2" on its own says nothing about which file or why.
        line = [l for l in proc.stderr.splitlines()
                if "DEMO" in l or l.startswith("u2: demo ") or "needs " in l]
        verdict = line[-1].replace("u2: ", "") if line else f"exit {proc.returncode}"
        if proc.returncode == 6:
            verdict = "unverifiable (digest scope): " + verdict
        elif proc.returncode != 0:
            bad += 1
        print(f"{name:<34} {verdict}")
    print(f"{len(files)} demos, {bad} that did not replay clean")
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("inspect"); p.add_argument("demo"); p.set_defaults(fn=cmd_inspect)

    p = sub.add_parser("verify", help="run the engine's playback and report")
    p.add_argument("demo")
    p.add_argument("--engine", default="./u2")
    p.add_argument("--data-dir")
    p.add_argument("--salvage", action="store_true")
    p.add_argument("--engine-arg", action="append", default=[],
                   help="repeatable: one extra argument for the engine")
    p.set_defaults(fn=cmd_verify)

    p = sub.add_parser("against-trace", help="W3: recorded keys vs the engine's trace")
    p.add_argument("demo"); p.add_argument("trace"); p.set_defaults(fn=cmd_against_trace)

    p = sub.add_parser("diff")
    p.add_argument("demo")
    p.add_argument("--against")
    p.set_defaults(fn=cmd_diff)

    p = sub.add_parser("export-keys")
    p.add_argument("demo")
    p.add_argument("--keys", dest="keys_out", default="out.keys")
    p.add_argument("--ds", dest="ds_out", default="out.ds")
    p.set_defaults(fn=cmd_export_keys)

    p = sub.add_parser("from-playlog")
    p.add_argument("report")
    p.add_argument("--keys", dest="keys_out", default="out.keys")
    p.set_defaults(fn=cmd_from_playlog)

    p = sub.add_parser("corpus", help="W14: replay a directory of demos")
    p.add_argument("dir")
    p.add_argument("--engine", default="./u2")
    p.add_argument("--data-dir")
    p.add_argument("--engine-arg", action="append", default=[],
                   help="repeatable: one extra argument for the engine, e.g. "
                        "--engine-arg --cleanroom. A REMAINDER positional would swallow "
                        "the flags before it, which is how the first version of this "
                        "handed the engine its own option names.")
    p.set_defaults(fn=cmd_corpus)

    args = ap.parse_args()
    return args.fn(args)


if __name__ == "__main__":
    raise SystemExit(main())

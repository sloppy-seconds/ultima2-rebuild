#!/usr/bin/env python3
"""playlog.py — verify a u2 playthrough report.

The engine emits reports (u2 --replay-report FILE); this tool re-runs the same
seed and key script and checks the recorded digests still come out the same.
Keeping verification in tooling rather than the engine means the engine stays a
pure player and the C build keeps its zero-dependency rule.

A report is commit-safe evidence: digests and numbers, no game text.

Usage:
  playlog.py verify REPORT            # re-run and compare; exit 0 if identical
  playlog.py show REPORT              # human summary (keys, step count, digests)
  playlog.py digest REPORT            # single digest of the whole playthrough
"""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))     # engine/tools
ENGINE = os.path.abspath(os.path.join(HERE, ".."))   # engine
BIN = os.path.join(ENGINE, "u2")


def load(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def rerun(rep, out_path):
    """Replay the report's keys and seed into a fresh report file.

    The save directory has to be empty for every run, not just the first one.
    Since CP3 the route reaches 'P', whose whole behaviour is "read the player
    record": against a directory left behind by a previous run it loads a
    character instead of asking for a disk, and the digests diverge for reasons
    that have nothing to do with the engine. A playthrough transcript is only
    evidence if it is self-contained, so verification always starts from a blank
    disk.
    """
    keys = rep.get("keys", [])
    tmp = tempfile.mkdtemp(prefix="u2-playlog-", dir="/var/tmp")
    kpath = os.path.join(tmp, "keys")
    savedir = os.path.join(tmp, "disk")
    try:
        with open(kpath, "w", encoding="utf-8") as f:
            f.write("\n".join(keys) + ("\n" if keys else ""))
        cmd = [BIN, "--headless", "--seed", str(rep.get("seed", 0)),
               "--script", kpath, "--save-dir", savedir,
               "--replay-report", out_path]
        # Replay under the mode the evidence was recorded under. Enhanced became the
        # engine's default on 2026-11-17, and every committed report predates that,
        # so an absent "mode" field means classic rather than "use the default":
        # defaulting to the current default would silently re-record the baseline
        # under different rules and then report a divergence as an engine bug.
        if rep.get("mode") != "enhanced":
            cmd.insert(1, "--classic")
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
        if proc.returncode != 0:
            sys.exit(f"playlog: engine exit {proc.returncode}: {proc.stderr.strip()[:200]}")
        return load(out_path)
    finally:
        # Recursive, because the save directory now holds a slot: disk/PLAYER/PLAYER
        # is two levels deep, and the one-level unlink-and-rmdir this replaced
        # raised IsADirectoryError on it. The rule it was protecting — every run
        # starts from a blank disk — is unchanged.
        shutil.rmtree(tmp, ignore_errors=True)


def cmd_verify(args):
    rep = load(args.report)
    mine = rerun(rep, args.report + ".rerun")
    try:
        want = [(s["screen"], s["state"], s["rolls"], s["at_key"]) for s in rep["steps"]]
        got = [(s["screen"], s["state"], s["rolls"], s["at_key"]) for s in mine["steps"]]
    except KeyError as exc:
        sys.exit(f"playlog: report malformed, missing {exc}")
    same = want == got
    print(f"steps recorded={len(want)} replayed={len(got)}")
    if not same:
        for i, (a, b) in enumerate(zip(want, got)):
            if a != b:
                print(f"first mismatch at step {i}:\n  recorded {a}\n  replayed {b}")
                break
        if len(want) != len(got):
            print(f"step count differs: {len(want)} vs {len(got)}")
    print("REPLAY: " + ("REPRODUCES" if same else "DIVERGES"))
    os.unlink(args.report + ".rerun")
    return 0 if same else 1


def cmd_show(args):
    rep = load(args.report)
    print(f"engine={rep.get('engine_version')} seed={rep.get('seed')} "
          f"keys={len(rep.get('keys', []))} steps={len(rep.get('steps', []))}")
    print(f"content={rep.get('content_source')}")
    for s in rep["steps"][: args.limit]:
        print(f"  at_key={s['at_key']:>3} rolls={s['rolls']:>4} screen={s['screen'][:12]} "
              f"state={s['state'][:12]}")
    return 0


def cmd_digest(args):
    rep = load(args.report)
    core = {k: rep[k] for k in ("engine_version", "seed", "keys", "steps", "content_source")
            if k in rep}
    blob = json.dumps(core, sort_keys=True).encode()
    print(hashlib.sha256(blob).hexdigest())
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mode", choices=["verify", "show", "digest"])
    ap.add_argument("report")
    ap.add_argument("--limit", type=int, default=40)
    args = ap.parse_args()
    if not os.path.exists(args.report):
        sys.exit(f"playlog: no such report: {args.report}")
    return {"verify": cmd_verify, "show": cmd_show, "digest": cmd_digest}[args.mode](args)


if __name__ == "__main__":
    sys.exit(main())

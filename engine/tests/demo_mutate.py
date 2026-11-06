#!/usr/bin/env python3
"""Mutate one record digest in a .demo and repair the @end framing digest so the
file still reads as a well-formed recording.

This exists so the demo gate's central control can go red for the RIGHT reason.
Flipping a digest byte without repairing the framing makes the reader refuse the
file as corrupt, which is a different check passing, not the one under test. With
the framing repaired the file is structurally perfect and the only thing wrong
with it is that a record claims a state the engine will not produce — which is
exactly the divergence D6 says must fail the run and name the record.

Usage: demo_mutate.py IN.demo OUT.demo [record-number]
"""
import hashlib
import sys


def framing(lines):
    h = hashlib.sha256()
    for line in lines:
        h.update(line.encode())
    return h.hexdigest()


def main(argv):
    if len(argv) < 3:
        print("usage: demo_mutate.py IN.demo OUT.demo [record-number]", file=sys.stderr)
        return 2
    src, dst = argv[1], argv[2]
    which = int(argv[3]) if len(argv) > 3 else 1

    with open(src) as f:
        lines = f.readlines()

    seen = 0
    hit = False
    for i, line in enumerate(lines):
        if not line.startswith("@rec "):
            continue
        seen += 1
        if seen != which:
            continue
        at = line.index("sha=") + 4
        flip = "1" if line[at] == "0" else "0"
        lines[i] = line[:at] + flip + line[at + 1:]
        hit = True
        break
    if not hit:
        print(f"demo_mutate: no record {which} in {src}", file=sys.stderr)
        return 2

    for i, line in enumerate(lines):
        if line.startswith("@end"):
            at = line.index(" sha=") + 5
            lines[i] = line[:at] + framing(lines[:i]) + "\n"
            break

    with open(dst, "w") as f:
        f.writelines(lines)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))

#!/usr/bin/env python3
"""Build deliberately-wrong .demo fixtures for the demo gate.

Every subcommand writes a file that is structurally perfect — the @end framing
digest is repaired so the reader cannot refuse it as corrupt — and wrong in exactly
one way. That is the point: a control that makes the gate red by breaking the file
format proves the format check, not the behaviour check. The three shapes here are
the ones docs/demo-record-playback.md § 9 names as controls.

  digest   IN OUT N      flip one hex digit of record N's digest, repair framing
  rawdigest IN OUT N     the same flip with the framing digest left alone, so the
                         file is detectably edited rather than subtly wrong
  strip    IN OUT KIND   drop every record of KIND (key|auto), repair framing
  cut      IN OUT N       keep the first N records, repair framing
  short    IN OUT        truncate the checkpoint payload to 0x280 and drop @end,
                         so the only thing wrong is a short mirror
  noend    IN OUT        drop the @end line and nothing else
  digestver IN OUT N    rewrite the header's digest-version to N, repair framing.
                        This is W14's control: the engine compares its own constant
                        against the file's, so a file claiming another scope exercises
                        exactly the path a build with a bumped scope would.

Usage: demo_fixture.py SUBCOMMAND IN OUT [arg]
"""
import hashlib
import sys


def read(path):
    with open(path) as f:
        return f.readlines()


def write(path, lines):
    with open(path, "w") as f:
        f.writelines(lines)


def framing(lines):
    h = hashlib.sha256()
    for line in lines:
        h.update(line.encode())
    return h.hexdigest()


def repair_end(lines):
    """Recompute @end's framing digest over everything above it."""
    for i, line in enumerate(lines):
        if line.startswith("@end"):
            at = line.index(" sha=") + 5
            lines[i] = line[:at] + framing(lines[:i]) + "\n"
            return lines
    return lines


def split_checkpoint(lines):
    """Return (header, payload_lines, tail) around the checkpoint hex block."""
    start = next(i for i, l in enumerate(lines) if l.startswith("@checkpoint "))
    end = next(i for i, l in enumerate(lines) if l.startswith("@checkpoint-sha"))
    return lines[: start + 1], lines[start + 1 : end], lines[end:]


def cmd_digest(argv):
    lines = read(argv[0])
    which = int(argv[2]) if len(argv) > 2 else 1
    seen = 0
    for i, line in enumerate(lines):
        if not line.startswith("@rec "):
            continue
        seen += 1
        if seen != which:
            continue
        at = line.index("sha=") + 4
        flip = "1" if line[at] == "0" else "0"
        lines[i] = line[:at] + flip + line[at + 1 :]
        break
    else:
        print(f"demo_fixture: no record {which}", file=sys.stderr)
        return 2
    write(argv[1], repair_end(lines))
    return 0


def cmd_rawdigest(argv):
    """Flip a record digest and leave the framing digest alone."""
    lines = read(argv[0])
    which = int(argv[2]) if len(argv) > 2 else 1
    seen = 0
    for i, line in enumerate(lines):
        if not line.startswith("@rec "):
            continue
        seen += 1
        if seen != which:
            continue
        at = line.index("sha=") + 4
        lines[i] = line[:at] + ("1" if line[at] == "0" else "0") + line[at + 1 :]
        break
    write(argv[1], lines)
    return 0


def cmd_strip(argv):
    lines = read(argv[0])
    kind = argv[2]
    kept = [l for l in lines if not (l.startswith("@rec ") and f"kind={kind}" in l)]
    if len(kept) == len(lines):
        print(f"demo_fixture: no kind={kind} records to strip", file=sys.stderr)
        return 2
    write(argv[1], repair_end(kept))
    return 0


def cmd_cut(argv):
    lines = read(argv[0])
    keep = int(argv[2])
    seen = 0
    out = []
    for line in lines:
        if line.startswith("@rec "):
            if seen >= keep:
                break
            seen += 1
        if line.startswith("@end"):
            continue
        out.append(line)
    write(argv[1], repair_end(out))
    return 0


def cmd_short(argv):
    lines = read(argv[0])
    head, payload, tail = split_checkpoint(lines)
    cut, nbytes = [], 0
    for line in payload:
        if nbytes >= 0x280:
            break
        cut.append(line)
        nbytes += len(line.strip()) // 2
    body = bytes.fromhex("".join(l.strip() for l in cut))
    sha = hashlib.sha256(body).hexdigest()
    out = head + cut + [f"@checkpoint-sha {sha}\n"]
    for line in tail:
        if line.startswith("@checkpoint-sha") or line.startswith("@end"):
            continue
        if line.startswith("  "):
            continue
        out.append(line)
    write(argv[1], out)
    return 0


def cmd_noend(argv):
    lines = [l for l in read(argv[0]) if not l.startswith("@end")]
    write(argv[1], lines)
    return 0


def cmd_digestver(argv):
    lines = read(argv[0])
    want = argv[2]
    for i, line in enumerate(lines):
        if line.startswith("digest-version "):
            lines[i] = f"digest-version {want}\n"
            break
    else:
        print("demo_fixture: no digest-version line", file=sys.stderr)
        return 2
    write(argv[1], repair_end(lines))
    return 0


COMMANDS = {
    "digest": cmd_digest,
    "rawdigest": cmd_rawdigest,
    "strip": cmd_strip,
    "cut": cmd_cut,
    "short": cmd_short,
    "noend": cmd_noend,
    "digestver": cmd_digestver,
}


def main(argv):
    if len(argv) < 4 or argv[1] not in COMMANDS:
        print(__doc__, file=sys.stderr)
        return 2
    return COMMANDS[argv[1]](argv[2:])


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))

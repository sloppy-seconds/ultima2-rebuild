#!/usr/bin/env python3
"""Read one recorded DS byte out of a .demo's @snap block.

    demo_field.py DEMO RECORD OFFSET [EXPECT]

RECORD is the 1-based record number; OFFSET is decimal or 0x-prefixed. With EXPECT the
script exits 1 when the byte disagrees, so it can stand alone as a gate leg.

Why this exists rather than another inline parse: the payload is 51 lines of 32 bytes, and
every hand-written reader of it in this project's history has gotten that wrong in a way
that produced a confident answer about the wrong bytes. The block has to be reassembled
across consecutive hex lines and stopped at the first line that is not hex, and the record's
snapshot is the state at the seam BEFORE its own key is applied — so a caller that wants
"the state after record N's key" has to ask for N+1.

Exits 2 for a file with no snapshots (recorded without --demo-snapshots) rather than
reporting a zero, because "absent" and "zero" are different answers and only one of them
means the world is fine.
"""
import sys


def _is_hex(line):
    return bool(line) and all(c in "0123456789abcdef" for c in line) and len(line) % 2 == 0


def snapshots(path):
    """Return {record_number: bytes} for every @rec block that carries a snapshot.

    Index-walked rather than iterator-nested on purpose. The first version consumed the
    following lines with a nested `for extra in f`, and the line that ended a block — always
    the next `@rec` — was eaten and never seen by the outer loop, so every other record
    silently reported "no snapshot". A reader of this format that drops records is worse than
    no reader, because the records it does report look perfectly fine.
    """
    with open(path, "r", errors="replace") as f:
        lines = [raw.strip() for raw in f]
    out = {}
    i = 0
    while i < len(lines):
        line = lines[i]
        i += 1
        # The snapshot hangs off its own `@snap N` line, which follows the `@rec` it belongs
        # to. A playback's sidecar writes `@snap N rolls=...` with no preceding @rec, so the
        # number has to come from @snap and not from the record line.
        if not line.startswith("@snap "):
            continue
        try:
            rec = int(line.split()[1])
        except (IndexError, ValueError):
            continue
        blob = []
        while i < len(lines) and _is_hex(lines[i]):
            blob.append(lines[i])
            i += 1
        if blob:
            out[rec] = bytes.fromhex("".join(blob))
    return out


def main(argv):
    if len(argv) not in (3, 4):
        print(__doc__, file=sys.stderr)
        return 2
    path, rec = argv[0], int(argv[1])
    off = int(argv[2], 0) if argv[2].lower().startswith("0x") else int(argv[2])

    snaps = snapshots(path)
    if not snaps:
        print("demo_field: %s carries no @snap blocks (record with --demo-snapshots)"
              % path, file=sys.stderr)
        return 2
    if rec not in snaps:
        print("demo_field: record %d has no snapshot (records present: %d..%d)"
              % (rec, min(snaps), max(snaps)), file=sys.stderr)
        return 2
    data = snaps[rec]
    if off >= len(data):
        print("demo_field: offset %d past a %d-byte snapshot" % (off, len(data)),
              file=sys.stderr)
        return 2
    got = "%02x" % data[off]
    if len(argv) == 4:
        want = argv[3].lower()
        if got != want:
            print("demo_field: record %d offset %d is %s, expected %s" % (rec, off, got, want),
                  file=sys.stderr)
            return 1
    print(got)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))

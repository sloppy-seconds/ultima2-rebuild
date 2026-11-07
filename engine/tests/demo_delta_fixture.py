#!/usr/bin/env python3
"""demo_delta_fixture.py — make a seeded world DIRTY at the slot and print the
exact @world delta the recorder must emit.

usage: demo_delta_fixture.py WORLD_DIR            -> the expected delta string
       demo_delta_fixture.py WORLD_DIR slot-sha   -> sha256 of the slot file's first
                                                    4096 bytes, what a correct apply
                                                    must reproduce

The three cells are written in DESCENDING file order on purpose. A writer that emitted
cells in the order it noticed them would produce a different string from the canonical
ascending one printed here, and that is the control: the assertion in run_demo.sh is an
exact string match, so an unordered writer goes red rather than passing on a comparison
that is blind to ordering.

Every byte here is engine-authored filler from demo_seed.py. No game content.
"""
import hashlib
import os
import sys

# (x, y) in descending file order: (63,63) is the last byte, (10,0) near the first.
CELLS = [(63, 63), (3, 4), (10, 0)]
MARK = 0x44
TILES = 4096


def main() -> int:
    if len(sys.argv) not in (2, 3):
        print("usage: demo_delta_fixture.py WORLD_DIR [slot-sha]", file=sys.stderr)
        return 2
    world = sys.argv[1]
    want = sys.argv[2] if len(sys.argv) == 3 else "delta"

    src = None
    for root, _dirs, files in os.walk(os.path.join(world, "content")):
        for name in files:
            if name.startswith("mapx70"):
                src = os.path.join(root, name)
                break
        if src:
            break
    if not src:
        print("no mapx70 in the seeded content", file=sys.stderr)
        return 1

    raw = bytearray(open(src, "rb").read())
    # The original byte every edited cell must be reported as coming FROM.
    origins = {}
    for x, y in CELLS:
        i = y * 64 + x
        if i >= len(raw):
            print(f"cell ({x},{y}) is past the sector", file=sys.stderr)
            return 1
        origins[(x, y)] = raw[i]
        raw[i] = MARK

    slot = os.path.join(world, "save", "PLAYER")
    os.makedirs(slot, exist_ok=True)
    with open(os.path.join(slot, "mapx70"), "wb") as f:
        f.write(bytes(raw))

    if want == "slot-sha":
        # What a correct apply has to reproduce: the recorder's own sector bytes.
        print(hashlib.sha256(bytes(raw[:TILES])).hexdigest()[:8])
        return 0

    # Canonical order is ascending by y*64+x, regardless of the order written above.
    parts = [f"{x},{y}:{origins[(x, y)]:02x}->{MARK:02x};" for x, y in
             sorted(CELLS, key=lambda c: c[1] * 64 + c[0])]
    print("".join(parts))
    return 0


if __name__ == "__main__":
    sys.exit(main())

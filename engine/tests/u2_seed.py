#!/usr/bin/env python3
"""Seed an engine DS checkpoint with intent, not with hand-copied offsets.

Why this exists
---------------
A test that needs the player somewhere in particular has to write the DS block, and
until now each one did it by pasting a hex offset out of core/state.h. Two problems,
both of the "a second hand-maintained copy" family AGENTS.md keeps writing down:

  * The offset drifts. state.h is the DS segment and the save format, and it moves
    when a decode lands. A pasted 0x051 does not move with it and silently writes
    into whatever byte now lives there.
  * The encoding is not obvious. HP is one packed-BCD byte, gold is two BCD bytes
    whose names are the wrong way round on purpose (see the naming wart at
    state.h:251), and the sector is two decimal digits rather than a number. A
    test that writes 50 into HP writes 0x50, which the game reads as fifty, and a
    test that writes 5000 into gold has to know which of gold_lo/gold_hi is the
    high byte.

So offsets come from state.h at runtime, and the encodings live here once.

What it refuses to do
---------------------
It seeds state. It does not simulate. It will not pathfind, will not synthesise a
legal world, and will not check that the cell you asked for is walkable — a seed
that quietly moved you to the nearest legal cell would make a test pass for a
reason that has nothing to do with what it is testing. World geometry is
cp6_commerce_seed.py's job (it authors map files); this authors the state block.

Usage
-----
    u2_seed.py offsets [--name FILTER]
    u2_seed.py show FILE [FIELD...]
    u2_seed.py set FILE [--at HI,LO] [--where X,Y] [--hp N] [--gold N] [--exp N]
                        [--food N] [--torches N] [--keys N] [--tools N]
                        [--name S] [--class N] [--race N] [--sex N]
                        [--view-mode N] [--world N] [--set FIELD=VALUE]...
                        [--out OTHER.ds]
    u2_seed.py --selftest

The DS file is the hex text the engine's own --dump-ds writes and --load-ds reads.
"""

import argparse
import json
import os
import re
import sys

STATE_H = os.path.normpath(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src", "core", "state.h"))

# --------------------------------------------------------------------------
# offsets


def parse_offsets(path=STATE_H):
    """Return {field: offset} plus a note of how each was derived.

    state.h carries the offset twice, in two independent forms: a `/* 0xNN */`
    comment beside each member, and a `_Static_assert(U2_OFF(name) == 0xNNN)` that
    the compiler proves. The comments cover every field; the asserts cover most.
    Reading only one of them would inherit that field's blind spot, so this reads
    both and the selftest fails if they ever disagree.
    """
    text = open(path, encoding="utf-8").read()
    comment = {}
    for m in re.finditer(r"/\*\s*0x([0-9A-Fa-f]{2,4})\s*\*/\s*(?:uint\d+_t|int)\s+([a-z_0-9]+)",
                         text):
        comment[m.group(2)] = int(m.group(1), 16)
    asserted = {}
    for m in re.finditer(r"_Static_assert\(\s*U2_OFF\(([a-z_0-9]+)\)\s*==\s*0x([0-9A-Fa-f]+)",
                         text):
        asserted[m.group(1)] = int(m.group(2), 16)
    conflicts = {k: (comment[k], asserted[k])
                 for k in asserted if k in comment and comment[k] != asserted[k]}
    if conflicts:
        raise ValueError("state.h disagrees with itself: %s" % conflicts)
    return dict(asserted, **comment), asserted


# --------------------------------------------------------------------------
# encodings


def to_bcd(value, digits):
    """Pack VALUE into `digits` BCD bytes, most significant byte first.

    The engine's numeric fields are packed decimal, not binary: HP 45 is the byte
    0x45. Anything that is not a decimal digit pair is a caller error rather than
    something to clamp — a clamped seed reads as a passing test.
    """
    if value < 0:
        raise ValueError("negative value %r" % value)
    s = str(value)
    if len(s) > digits:
        raise ValueError("%d does not fit in %d BCD digits" % (value, digits))
    s = s.zfill(digits if digits % 2 == 0 else digits + 1)
    return bytes(int(s[i:i + 2], 16) for i in range(0, len(s), 2))


def from_bcd(raw):
    return int("".join("%02X" % b for b in raw), 10)


# Intent name -> (offset key(s), BCD digit count or 0 for raw, byte length).
#
# gold_lo/exp_lo are the HIGH byte despite the name — state.h:243 and :252 record
# the wart and the asm site that proves it. Encoding through this table is the only
# place that has to know.
INTENTS = {
    "hp":       ("hp", 2),
    "food":     ("food", 2),
    "torches":  ("torches", 2),
    "keys":     ("keys", 2),
    "tools":    ("tools", 2),
    "gold":     (("gold_lo", "gold_hi"), 4),
    "exp":      (("exp_lo", "exp_hi"), 4),
    "class":    ("pclass", 0),
    "race":     ("race", 0),
    "sex":      ("sex", 0),
    "view-mode": ("view_mode", 0),
    "world":    ("world", 0),
}


class DsBlock:
    """A DS mirror in memory: the hex text --dump-ds writes."""

    def __init__(self, data, source="<memory>"):
        self.data = bytearray(data)
        self.source = source
        self.offsets, self.asserted = parse_offsets()

    @classmethod
    def load(cls, path):
        raw = open(path, encoding="utf-8").read().strip()
        try:
            return cls(bytearray.fromhex(raw), path), raw
        except ValueError as exc:
            raise SystemExit("%s is not a hex DS dump: %s" % (path, exc))

    def save(self, path):
        with open(path, "w", encoding="utf-8") as fh:
            fh.write(bytes(self.data).hex() + "\n")

    def _off(self, key):
        try:
            return self.offsets[key]
        except KeyError:
            raise SystemExit(
                "no field %r in state.h. `u2_seed.py offsets` lists what there is; "
                "guessing an offset here is the failure this module exists to stop." % key)

    def get(self, key):
        off = self._off(key)
        return self.data[off]

    def set_raw(self, key, value):
        self.data[self._off(key)] = value & 0xFF

    def set_bcd(self, keys, digits, value):
        if isinstance(keys, str):
            keys = (keys,)
        raw = to_bcd(value, digits)
        if len(raw) != len(keys):
            raise SystemExit("%s is %d byte(s); %r needs %d"
                             % (value, len(raw), keys, len(keys)))
        # to_bcd returns most significant byte first, and the first name in the
        # tuple is the high byte — which for gold and exp is the one called _lo.
        for name, byte in zip(keys, raw):
            self.data[self._off(name)] = byte

    def set_name(self, text):
        off = self._off("name")
        raw = text.encode("ascii", "strict")[:16]
        self.data[off:off + 16] = raw + bytes(16 - len(raw))

    def set_at(self, hi, lo):
        self.set_raw("sector_first", hi)
        self.set_raw("sector_second", lo)

    def set_where(self, x, y):
        self.set_raw("sector_x", x)
        self.set_raw("sector_y", y)


# --------------------------------------------------------------------------
# CLI


def _selftest():
    fails = []

    def check(cond, msg):
        if not cond:
            fails.append(msg)

    offs, asserted = parse_offsets()
    check(len(offs) > 60, "only %d offsets parsed from state.h — the parser has "
                          "stopped matching the file's shape" % len(offs))
    check(len(asserted) > 60, "only %d asserts parsed" % len(asserted))
    # The cross-check is the point: two independent forms in one header, and a
    # drift between them is a bug in the header, not in this parser.
    check(all(offs[k] == v for k, v in asserted.items()),
          "comment offsets disagree with the _Static_asserts")

    blk = DsBlock(bytearray(256))
    blk.set_bcd("hp", 2, 45)
    check(blk.data[offs["hp"]] == 0x45, "hp 45 did not land as 0x45")
    blk.set_bcd(("gold_lo", "gold_hi"), 4, 1234)
    check(blk.data[offs["gold_lo"]] == 0x12 and blk.data[offs["gold_hi"]] == 0x34,
          "gold 1234: gold_lo must be the HIGH byte (0x12), gold_hi the low (0x34)")
    check(from_bcd(bytes([blk.data[offs["gold_lo"]], blk.data[offs["gold_hi"]]])) == 1234,
          "gold did not round-trip")
    blk.set_name("SIRIL")
    check(bytes(blk.data[offs["name"]:offs["name"] + 5]) == b"SIRIL"
          and blk.data[offs["name"] + 5] == 0, "name did not write and pad")
    blk.set_at(3, 2)
    blk.set_where(38, 5)
    check(blk.get("sector_first") == 3 and blk.get("sector_second") == 2
          and blk.get("sector_x") == 38 and blk.get("sector_y") == 5,
          "sector/position setters are wrong")

    # Controls: the refusals have to refuse.
    for bad, why in ((100, "hp 100 does not fit two BCD digits"),
                     (99999, "gold 99999 does not fit four")):
        try:
            to_bcd(bad, 2 if bad == 100 else 4)
            fails.append("no error for %s (%s)" % (bad, why))
        except ValueError:
            pass
    try:
        blk._off("no_such_field")
        fails.append("an unknown field was accepted instead of refused")
    except SystemExit:
        pass

    if fails:
        for f in fails:
            print("  FAIL  %s" % f)
        print("u2_seed: %d failure(s)" % len(fails))
        return 1
    print("u2_seed selftest: %d offsets, %d asserted, encodings and refusals ok"
          % (len(offs), len(asserted)))
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(prog="u2_seed.py", description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--selftest", action="store_true")
    sub = ap.add_subparsers(dest="cmd")

    p_off = sub.add_parser("offsets")
    p_off.add_argument("--name", default=None)
    p_off.add_argument("--json", action="store_true")

    p_show = sub.add_parser("show")
    p_show.add_argument("file")
    p_show.add_argument("fields", nargs="*")

    p_set = sub.add_parser("set")
    p_set.add_argument("file")
    p_set.add_argument("--at"), p_set.add_argument("--where")
    p_set.add_argument("--name")
    p_set.add_argument("--out")
    p_set.add_argument("--set", action="append", default=[], metavar="FIELD=VALUE")
    for intent in sorted(INTENTS):
        p_set.add_argument("--" + intent, type=int)

    args = ap.parse_args(argv)
    if args.selftest:
        return _selftest()

    if args.cmd == "offsets":
        offs, asserted = parse_offsets()
        names = sorted(n for n in offs if not args.name or args.name in n)
        if args.json:
            print(json.dumps({n: {"offset": offs[n],
                                  "asserted": n in asserted} for n in names}, indent=1))
        else:
            for n in names:
                print("0x%03X  %-22s %s" % (offs[n], n,
                                            "assert" if n in asserted else "comment"))
        return 0

    if args.cmd == "show":
        blk, _ = DsBlock.load(args.file)
        fields = args.fields or sorted(blk.offsets)
        for f in fields:
            print("%-22s 0x%03X  %d" % (f, blk._off(f), blk.get(f)))
        return 0

    if args.cmd == "set":
        blk, _ = DsBlock.load(args.file)
        if args.at:
            hi, lo = (int(v, 0) for v in args.at.split(","))
            blk.set_at(hi, lo)
        if args.where:
            x, y = (int(v, 0) for v in args.where.split(","))
            blk.set_where(x, y)
        if args.name:
            blk.set_name(args.name)
        for intent in sorted(INTENTS):
            value = getattr(args, intent.replace("-", "_"))
            if value is None:
                continue
            key, digits = INTENTS[intent]
            if digits:
                blk.set_bcd(key, digits, value)
            else:
                blk.set_raw(key, value)
        for pair in args.set:
            if "=" not in pair:
                raise SystemExit("--set wants FIELD=VALUE, got %r" % pair)
            k, v = pair.split("=", 1)
            blk.set_raw(k, int(v, 0))
        blk.save(args.out or args.file)
        print("wrote %s" % (args.out or args.file))
        return 0

    ap.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())

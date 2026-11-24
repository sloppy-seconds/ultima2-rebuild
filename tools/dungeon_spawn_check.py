#!/usr/bin/env python3
"""A4: prove the dungeon spawn formula against every dungeon dump on disk.

Reads the monster record out of `captures/spice86/*/spice86dumpMemoryDump.bin`
and checks it against what `asm/147F_dungeon_monster_spawn.asm` computes.

The routine, read instruction by instruction (runtime addresses, file = +0x200):

  L_0016d1  mov %di,%ax ; clc ; rcr $1,%al ; mov %al,0x1b7(%di)
            -> [slot+0x1B7] = slot >> 1
  L_0016df  call RNG ; and $0x7,%al ; cmp $0x7 ; je retry     -> v = rand&7, v != 7
  L_0016f0  add %al,%al ; cmp 0x1b7(%di),%al ; je ok ; jb ok  -> accept iff 2v <= kind
  L_0016fd  clc ; rcr $1,%al ; adc $0x1,%al ; mov %al,0x197  -> type = v + 1
  L_001706  add %al,%al ; adc 0x1b7(%di),%al ; add x3 ; or $0x10 ; mov %al,0x177
            -> HP = ((2*type + kind) * 8) | 0x10

A dungeon sector is detected by the record shape, not by the directory name:
`docs/monster-spawn.md` records that `airport32` is a dungeon with a stale name.

Read-only. Exit codes are a verdict, not just a status:

  0   every dungeon-shaped slot satisfies the formula
  1   at least one slot contradicts the formula -- a real failure
  77  cannot run: no capture directory, or no dungeon-shaped capture in it.
      Absence of evidence is not evidence of a regression, and a gate that reports
      the two identically gets its first false red and is then ignored.

Usage: python3 tools/dungeon_spawn_check.py [--verbose] [--selftest]
                                          [--capture-root DIR]
"""
import os
import pathlib
import sys

DS_SEG = 0x08BE
DS_BASE = DS_SEG * 16
SLOTS = 32
OFF_X, OFF_Y, OFF_HP, OFF_TYPE, OFF_KIND = 0x137, 0x157, 0x177, 0x197, 0x1B7

# The overworld type range, from docs/monster-spawn.md: 0x2C..0xFC with 0x00 and
# 0x40 (the empty sentinel) excluded. A dungeon record holds small indices instead.
DUNGEON_TYPES = set(range(0x01, 0x08))


def hp_formula(type_, kind):
    """HP as asm/147F computes it. `*8` is the three `add %al,%al` after the adc."""
    return (((2 * type_ + kind) * 8) | 0x10) & 0xFF


def read_record(path):
    b = path.read_bytes()
    if len(b) < DS_BASE + OFF_KIND + SLOTS:
        return None
    g = lambda off: [b[DS_BASE + off + i] for i in range(SLOTS)]
    return {'x': g(OFF_X), 'y': g(OFF_Y), 'hp': g(OFF_HP),
            'type': g(OFF_TYPE), 'kind': g(OFF_KIND)}


def is_dungeon(rec):
    """Dungeon by encoding, not by name: every active slot holds a small index
    and every slot's kind equals slot>>1. Both are required -- the overworld
    filter `type not in (0, 0x40)` would wave these through as garbage."""
    act = [i for i in range(SLOTS) if rec['type'][i]]
    if not act:
        return False
    if not all(rec['type'][i] in DUNGEON_TYPES for i in act):
        return False
    return all(rec['kind'][i] == (i >> 1) for i in act)


def check_record(rec, verbose=False, name=''):
    """Return (slots_checked, slots_failing) for one decoded monster record."""
    total = fails = 0
    for i in range(SLOTS):
        t, k, h = rec['type'][i], rec['kind'][i], rec['hp'][i]
        if not t:
            continue
        total += 1
        want = hp_formula(t, k)
        guard = 2 * (t - 1) <= k          # accept iff 2v <= kind, v = type-1
        if want != h or not guard:
            fails += 1
            print('FAIL %s slot %d: type 0x%02X kind 0x%02X hp %d, formula %d, '
                  'guard %s' % (name, i, t, k, h, want, 'ok' if guard else 'BROKEN'))
        elif verbose:
            print('  ok %s slot %2d  x=%2d y=%2d  type 0x%02X kind 0x%02X  hp %d'
                  % (name, i, rec['x'][i], rec['y'][i], t, k, h))
    return total, fails


def synthetic_dungeon():
    """A dungeon record built from the formula itself, for the selftest.

    Every slot is populated with the largest type the guard admits at that floor,
    so a checker that ignores the guard or the floor cannot pass on this input.
    """
    rec = {'x': [0] * SLOTS, 'y': [0] * SLOTS, 'hp': [0] * SLOTS,
           'type': [0] * SLOTS, 'kind': [0] * SLOTS}
    for i in range(SLOTS):
        kind = i >> 1
        # largest v with 2v <= kind, then type = v + 1, capped at the dungeon range
        v = min(kind // 2, max(DUNGEON_TYPES) - 1)
        t = v + 1
        rec['kind'][i] = kind
        rec['type'][i] = t
        rec['hp'][i] = hp_formula(t, kind)
        rec['x'][i] = i % 16
        rec['y'][i] = (i // 16) * 3
    return rec


def selftest():
    """Controls. The point is proving the gate can go red, and that it does not
    call an overworld record a dungeon."""
    bad = 0

    def check(label, got, want):
        nonlocal bad
        ok = got == want
        if not ok:
            bad += 1
        print('  %s %-52s got %r want %r' % ('ok  ' if ok else 'FAIL', label, got, want))

    # The formula, at the two extremes of the legal input range.
    check('hp_formula(type 1, kind 0)', hp_formula(1, 0), 0x10)
    check('hp_formula(type 7, kind 15)', hp_formula(7, 15), 0xF8)
    # The `& 0xFF` in hp_formula is defensive, not load-bearing: the legal range
    # cannot overflow one byte. Assert that rather than pretend a wrap is reachable,
    # because a mask that never fires hides a wrong multiplier as easily as it fixes one.
    fits = all(hp_formula(t, k) < 256
               for t in range(1, 8) for k in range(0, 16))
    check('every legal (type, kind) HP fits one byte', fits, True)

    # Positive control: a record built from the formula passes clean.
    good = synthetic_dungeon()
    t, f = check_record(good)
    check('synthetic dungeon: slots seen', t, SLOTS)
    check('synthetic dungeon: slots failing', f, 0)

    # Negative control: one HP off by one must be caught. Without this the gate
    # has never been shown capable of failing.
    off = dict((k, list(v)) for k, v in good.items())
    off['hp'][7] = (off['hp'][7] + 1) & 0xFF
    t, f = check_record(off, name='ctl')
    check('one HP perturbed by 1: slots failing', f, 1)

    # A guard violation must be caught even when HP matches the formula.
    guard = dict((k, list(v)) for k, v in good.items())
    guard['type'][0] = 7          # slot 0 is floor 0; 2*(7-1) = 12 > 0
    guard['hp'][0] = hp_formula(7, 0)
    t, f = check_record(guard, name='ctl')
    check('type 7 on floor 0: slots failing', f, 1)

    # The dungeon detector must not wave an overworld record through.
    over = synthetic_dungeon()
    over['type'] = [0x2C if i % 3 else 0 for i in range(SLOTS)]
    check('overworld-shaped record is not a dungeon', is_dungeon(over), False)
    check('synthetic record is a dungeon', is_dungeon(good), True)
    empty = {'x': [0] * SLOTS, 'y': [0] * SLOTS, 'hp': [0] * SLOTS,
             'type': [0] * SLOTS, 'kind': [0] * SLOTS}
    check('all-empty record is not a dungeon', is_dungeon(empty), False)

    # A missing capture root is "cannot run", not "failed".
    check('missing capture root exits 77', main(['x', '--capture-root',
          '/var/tmp/definitely-not-a-capture-dir']), 77)

    print('SELFTEST: %s' % ('PASS' if bad == 0 else '%d control(s) FAILED' % bad))
    return 0 if bad == 0 else 1


def main(argv):
    verbose = '--verbose' in argv
    if '--selftest' in argv:
        return selftest()
    root = pathlib.Path('captures/spice86')
    if '--capture-root' in argv:
        root = pathlib.Path(argv[argv.index('--capture-root') + 1])
    if not root.is_dir():
        print('no %s directory -- nothing to check, this is not a failure'
              % root, file=sys.stderr)
        return 77
    total = fails = 0
    seen = []
    for d in sorted(root.iterdir()):
        p = d / 'spice86dumpMemoryDump.bin'
        if not p.is_file():
            continue
        rec = read_record(p)
        if rec is None or not is_dungeon(rec):
            continue
        seen.append(d.name)
        t, f = check_record(rec, verbose, d.name)
        total += t
        fails += f
    print('%d dungeon capture(s): %s' % (len(seen), ', '.join(seen) or 'none'))
    print('%d/%d monster slots satisfy HP = ((2*type + slot>>1)*8) | 0x10 and the '
          '2*(type-1) <= slot>>1 guard' % (total - fails, total))
    if not total:
        print('nothing to check -- no dungeon-shaped record found')
        return 77
    if fails:
        print('FORMULA FAILS on %d slot(s)' % fails)
        return 1
    print('DUNGEON SPAWN FORMULA: PASS')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))

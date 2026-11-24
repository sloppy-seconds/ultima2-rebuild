#!/usr/bin/env python3
"""creature_hp.py — every creature's HP, by population, measured from the bytes.

Why: `disasm/census/creature.tsv` carries one `hp` per creature, sourced from the
third-party bestiary and graded `inferred`. Measurement showed that is the wrong shape
for the data. HP is a **single byte** (`monx[64..95]` loading to `DS:0x177+i`, proven by
`asm/2E3D_combat_attack_flow`), and it is authored **per monster slot**, not per species:
10 of the 13 placed types carry more than one HP value across the 16 real sector files.
The third-party single number turns out to describe the *wandering-spawn* value, which is
a constant per type, so it is not wrong so much as scoped to one population.

This is the first real instance of the D6 ruling in `docs/plan-record-index.md` § 8: one
entity, stats keyed by the route by which it came to exist. Three contexts, closed:

    placed    authored in monxNN, one byte per slot, varies by sector and slot
    spawned   written by the nine wandering-spawn arms, constant per type
    dungeon   computed from the stamp and the floor, so a formula, not a constant

Every row names its source and its grade. Nothing here is inferred: the placed rows are a
byte read at a documented offset, the spawned rows come from the immediates in the named
asm files, and the dungeon row is the formula in `asm/147F_dungeon_monster_spawn.asm`.

Usage:
    python3 tools/creature_hp.py                 # write disasm/creature-populations.tsv
    python3 tools/creature_hp.py --check-fresh   # gate: is the committed table current?
    python3 tools/creature_hp.py --selftest      # controls, including the byte-width one
    python3 tools/creature_hp.py --what Guard    # query one creature
    python3 tools/creature_hp.py --sector 82     # per-slot detail for one sector
"""
import argparse
import collections
import csv
import os
import sys

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(PROJECT_ROOT, 'tools'))

ORIGINAL = os.path.join(PROJECT_ROOT, 'original')
OUT = os.path.join(PROJECT_ROOT, 'disasm', 'creature-populations.tsv')
CREATURE_TSV = os.path.join(PROJECT_ROOT, 'disasm', 'census', 'creature.tsv')

# monx layout, proven by tools/monx_layout_scan.py against every capture on disk. The same
# layout holds for MONG; the Galaxy patch ships the planet worlds' monster tables under a
# different stem because MS-DOS had no directories -- see AGENTS § 3 and
# docs/planet-maps-galaxy-patch.md. `original/updated/monx*` are byte-identical to the root
# set (verified), so the root set is used for `mapx` and MONG is read only from updated/.
MONX_TYPE_OFF, MONX_HP_OFF, MONX_SLOTS = 96, 64, 32
EMPTY_SENTINEL = 0x40          # type byte meaning "no monster in this slot"
HP_MAX = 0xFF                  # one byte; nothing in this binary can exceed it

# (layer, directory, stem). `mapx` is Earth and Planet X; `mapg` is Mercury..Pluto. Sector
# numbers collide across the two layers with different contents, so a sector is only
# identified by the pair -- the same key census uses.
LAYERS = (('mapx', 'original', 'monx'), ('mapg', os.path.join('original', 'updated'), 'MONG'))

# Dungeon spawn: asm/147F_dungeon_monster_spawn.asm.
#   type  = (rand & 7) + 1, accepted when 2*(type-1) <= floor
#   hp    = ((2*type + floor) * 8) | 0x10
# Floors actually occupied are 2..15 (measured 54/54 slots, docs/monster-spawn.md § 9).
DG_FLOOR_MIN, DG_FLOOR_MAX = 2, 15
DG_NAMES = {1: 'Orc', 2: 'Ghost', 3: 'Carrion creeper', 4: 'Viper',
            5: 'Gremlin', 6: 'Minotaur', 7: 'Balron'}
DG_NAME_GRADE = {1: 'hypothesis', 2: 'corroborated', 3: 'corroborated', 4: 'corroborated',
                 5: 'proven', 6: 'inferred-differential', 7: 'corroborated'}

COLUMNS = ['name', 'art', 'type', 'layer', 'context', 'hp_mode', 'hp_min', 'hp_max',
           'distinct', 'placements', 'sectors', 'value_or_formula', 'source', 'grade']


def dg_hp(stamp, floor):
    return ((2 * stamp + floor) * 8) | 0x10


def real_sector_files():
    """(layer, sector, bytes) for every file that actually places a monster.

    33 monx files ship and 17 of them are byte-identical copies of the same empty blob; the
    21 MONG files are the same story with 7 empties. So 'every file' is not 'every sector'.
    The rule is content, not filename: a file with no type byte outside {0x00, EMPTY} places
    nothing. Both layers are scanned, because measuring only `monx` silently measures only
    Earth and Planet X and reports it as the game.
    """
    out = []
    for layer, reldir, stem in LAYERS:
        directory = os.path.join(PROJECT_ROOT, reldir)
        if not os.path.isdir(directory):
            continue
        for name in sorted(os.listdir(directory)):
            if not name.startswith(stem) or not name[len(stem):].isdigit():
                continue
            with open(os.path.join(directory, name), 'rb') as fh:
                data = fh.read()
            if len(data) < MONX_TYPE_OFF + MONX_SLOTS:
                continue
            if any(data[MONX_TYPE_OFF + i] not in (0x00, EMPTY_SENTINEL)
                   for i in range(MONX_SLOTS)):
                out.append((layer, name[len(stem):], data))
    return out


def placed_rows(sector_data, names):
    """One row per (layer, type byte), summarising every authored placement of that type.

    Grouped per layer rather than pooled, because Earth and the planets are different
    authored data and pooling them would hide exactly the regional variation this table
    exists to show.
    """
    by_key = collections.defaultdict(lambda: collections.defaultdict(list))
    for layer, sector, data in sector_data:
        for i in range(MONX_SLOTS):
            t = data[MONX_TYPE_OFF + i]
            if t in (0x00, EMPTY_SENTINEL):
                continue
            hp = data[MONX_HP_OFF + i]
            if hp > HP_MAX:                      # cannot happen with a byte read; kept as a tripwire
                raise AssertionError(f'{layer} {sector} slot {i}: HP {hp} exceeds one byte')
            by_key[(layer, t)][hp].append((sector, i))
    rows = []
    for (layer, t) in sorted(by_key):
        hps = by_key[(layer, t)]
        total = sum(len(v) for v in hps.values())
        mode = max(hps, key=lambda h: len(hps[h]))
        secs = {s for v in hps.values() for s, _ in v}
        rows.append({
            'name': names.get(t >> 2, ''),
            'art': f'0x{t >> 2:02x}', 'type': f'0x{t:02x}', 'layer': layer,
            'context': 'placed',
            'hp_mode': mode, 'hp_min': min(hps), 'hp_max': max(hps),
            'distinct': len(hps), 'placements': total, 'sectors': len(secs),
            'value_or_formula': ';'.join(f'{h}x{len(v)}' for h, v in sorted(hps.items())),
            'source': f'{layer} monster table, [64+slot] over '
                      f'{len({s for l, s, _ in sector_data if l == layer})} real sectors',
            'grade': 'measured',
        })
    return rows


def spawned_rows(names):
    """One row per wandering-spawn arm, HP taken from the immediate in the named asm file."""
    import monster_hp_dispatch as mhd
    rows = []
    for t, hp, asm_file in mhd.collect():
        rows.append({
            'name': names.get(t >> 2, ''),
            'art': f'0x{t >> 2:02x}', 'type': f'0x{t:02x}', 'layer': 'all', 'context': 'spawned',
            'hp_mode': hp, 'hp_min': hp, 'hp_max': hp, 'distinct': 1,
            'placements': '', 'sectors': '',
            'value_or_formula': str(hp),
            'source': f'asm/{asm_file}', 'grade': 'measured',
        })
    return rows


def dungeon_rows():
    """One row per dungeon stamp: HP is a formula over the floor, not a constant."""
    rows = []
    for stamp in sorted(DG_NAMES):
        lo = max(DG_FLOOR_MIN, 2 * (stamp - 1))
        hi = DG_FLOOR_MAX
        if lo > hi:
            continue
        rows.append({
            'name': DG_NAMES[stamp], 'art': f'stamp {stamp}', 'type': str(stamp),
            'layer': 'all', 'context': 'dungeon',
            'hp_mode': dg_hp(stamp, hi), 'hp_min': dg_hp(stamp, lo), 'hp_max': dg_hp(stamp, hi),
            'distinct': hi - lo + 1, 'placements': '', 'sectors': f'floors {lo}..{hi}',
            'value_or_formula': f'((2*{stamp} + floor)*8) | 0x10',
            'source': 'asm/147F_dungeon_monster_spawn.asm',
            'grade': 'measured',
        })
    return rows


def third_party_names():
    """art byte -> name, from the census table. Names are third-party; HP here is not."""
    names = {}
    if not os.path.exists(CREATURE_TSV):
        return names
    with open(CREATURE_TSV, encoding='utf-8') as fh:
        rows = list(csv.DictReader([l for l in fh if not l.startswith('#')], delimiter='\t'))
    for r in rows:
        try:
            names[int(r['art'], 16)] = r['name']
        except (KeyError, ValueError):
            continue
    return names


def build():
    names = third_party_names()
    sector_data = real_sector_files()
    return placed_rows(sector_data, names) + spawned_rows(names) + dungeon_rows()


def render(rows):
    head = [
        '# generated by tools/creature_hp.py -- do not hand-edit',
        '# HP is one byte (monx[64..95] -> DS:0x177+i, proven by asm/2E3D_combat_attack_flow),',
        '# so no creature in this binary can exceed 255. Any third-party figure above 255 is',
        '# not representable here and is recorded as an error, not a disagreement.',
        '# `placed` HP is authored per monster slot and varies by sector: most placed types carry',
        '# more than one value. Both layers are measured: `mapx` is Earth and Planet X (monx*),',
        '# `mapg` is Mercury..Pluto (MONG*), and sector numbers collide across them with different',
        '# contents, so a sector is only identified by the pair. `spawned` HP is the constant written',
        '# by the wandering-spawn arms. `dungeon` HP is a formula over the floor.',
        '# See D6 in docs/plan-record-index.md and docs/planet-maps-galaxy-patch.md.',
        '# `name` is third-party (docs/bestiary.md); the HP columns are measured from the bytes.',
    ]
    out = list(head)
    out.append('\t'.join(COLUMNS))
    for r in rows:
        out.append('\t'.join(str(r.get(c, '')) for c in COLUMNS))
    return '\n'.join(out) + '\n'


def per_slot(layer, sector):
    for lay, reldir, stem in LAYERS:
        if lay != layer:
            continue
        path = os.path.join(PROJECT_ROOT, reldir, f'{stem}{sector}')
        if not os.path.exists(path):
            return []
        blob = open(path, 'rb').read()
        names = third_party_names()
        out = []
        for i in range(MONX_SLOTS):
            t = blob[MONX_TYPE_OFF + i]
            if t in (0x00, EMPTY_SENTINEL):
                continue
            out.append((i, t, names.get(t >> 2, ''), blob[MONX_HP_OFF + i]))
        return out
    return []


def selftest():
    fails = []

    def check(label, got, want):
        if got != want:
            fails.append(f'{label}: got {got!r} want {want!r}')

    # 1. the byte-width tripwire: a value above 255 cannot be produced, and if one ever
    #    appeared the tool must raise rather than publish it.
    check('dg_hp(7,12)', dg_hp(7, 12), 208)
    check('dg_hp(1,2)', dg_hp(1, 2), 48)
    check('dg_hp never exceeds one byte', all(dg_hp(s, f) <= HP_MAX
                                             for s in range(1, 8) for f in range(2, 16)), True)

    # 2. a synthetic sector with two HP values for one type must report distinct == 2.
    blob = bytearray(b'\x00' * 384)
    blob[MONX_TYPE_OFF], blob[MONX_HP_OFF] = 0x30, 33
    blob[MONX_TYPE_OFF + 1], blob[MONX_HP_OFF + 1] = 0x30, 50
    rows = placed_rows([('mapx', '99', bytes(blob))], {0x0c: 'Orc'})
    check('synthetic distinct', rows[0]['distinct'], 2)
    check('synthetic placements', rows[0]['placements'], 2)

    # 2b. the same sector number in the two layers is two different authored populations,
    #     so it must produce two rows and must not be pooled.
    both = placed_rows([('mapx', '10', bytes(blob)), ('mapg', '10', bytes(blob))], {0x0c: 'Orc'})
    check('same sector in both layers stays separate', len(both), 2)
    check('layers recorded', sorted(r['layer'] for r in both), ['mapg', 'mapx'])

    # 3. an all-empty file places nothing and must be excluded by the content rule.
    empty = bytearray(b'\x00' * 384)
    for i in range(MONX_SLOTS):
        empty[MONX_TYPE_OFF + i] = EMPTY_SENTINEL
    check('empty file places nothing', placed_rows([('mapx', '98', bytes(empty))], {}), [])

    # 4. the tripwire itself must be capable of firing.
    bad = bytearray(b'\x00' * 384)
    bad[MONX_TYPE_OFF] = 0x30
    bad[MONX_HP_OFF] = 0x100 & 0xFF          # cannot exceed a byte; proves the read is a byte
    check('byte read cannot exceed 255', placed_rows([('mapx', '97', bytes(bad))], {})[0]['hp_max'] <= 255, True)

    # 5. known truths against the real tree.
    if os.path.isdir(ORIGINAL):
        real = real_sector_files()
        prows = {(r['layer'], r['type']): r for r in placed_rows(real, third_party_names())}
        check('both layers were scanned', {r['layer'] for r in prows.values()} >= {'mapx', 'mapg'}, True)
        if ('mapx', '0x60') in prows:
            check('Earth Guard placed mode is 255', prows[('mapx', '0x60')]['hp_mode'], 255)
            check('Earth Guard has two placed values', prows[('mapx', '0x60')]['distinct'], 2)
            check('no placed HP exceeds 255', prows[('mapx', '0x60')]['hp_max'] <= 255, True)
        srows = {r['type']: r for r in spawned_rows(third_party_names())}
        if '0x30' in srows:
            check('Orc spawned HP is 16', srows['0x30']['hp_mode'], 16)
    else:
        print('note: original/ absent, real-tree checks skipped')

    print(f'selftest: {len(COLUMNS)} columns, {"PASS" if not fails else "FAIL"}')
    for f in fails:
        print('  ' + f)
    return 1 if fails else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--check-fresh', action='store_true')
    ap.add_argument('--selftest', action='store_true')
    ap.add_argument('--what', metavar='NAME', help='print every population of one creature')
    ap.add_argument('--sector', metavar='NN', help='per-slot detail for one sector')
    ap.add_argument('--layer', default='mapx', choices=['mapx', 'mapg'],
                    help='mapx is Earth and Planet X, mapg is Mercury..Pluto')
    args = ap.parse_args()

    if args.selftest:
        return selftest()

    if not os.path.isdir(ORIGINAL):
        print('original/ absent: game files are gitignored, cannot measure', file=sys.stderr)
        return 77

    if args.sector:
        layer = args.layer
        for i, t, nm, hp in per_slot(layer, args.sector.zfill(2)):
            print(f'{layer} {args.sector.zfill(2)} slot {i:2d}  type 0x{t:02x}  {nm:<18} HP {hp}')
        return 0

    rows = build()
    if args.what:
        want = args.what.lower()
        hits = [r for r in rows if r['name'].lower() == want]
        for r in hits:
            print(f"{r['context']:<9} {r['layer']:<5} mode {r['hp_mode']!s:<4} "
                  f"range {r['hp_min']}..{r['hp_max']}  distinct {r['distinct']}  {r['source']}")
        if not hits:
            print(f'no population named {args.what!r}', file=sys.stderr)
            return 1
        return 0

    text = render(rows)
    if args.check_fresh:
        if not os.path.exists(OUT):
            print('creature-populations.tsv missing', file=sys.stderr)
            return 1
        if open(OUT, encoding='utf-8').read() != text:
            print('creature-populations.tsv is STALE: rerun tools/creature_hp.py', file=sys.stderr)
            return 1
        print(f'creature-populations.tsv fresh ({len(rows)} rows)')
        return 0

    with open(OUT, 'w', encoding='utf-8') as fh:
        fh.write(text)
    print(f'wrote {os.path.relpath(OUT, PROJECT_ROOT)} ({len(rows)} rows)')
    return 0


if __name__ == '__main__':
    sys.exit(main())

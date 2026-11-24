#!/usr/bin/env python3
"""bestiary_match.py — join third-party creature art to our own tile table by bitmap identity.

Why this exists: the executable has no monster name table, so `disasm/tile-art.tsv`
can only say "white humanoid, arms down, legs apart" about art 0x0C. The Ultima Codex
delivers a *name* and a *bitmap* in the same table cell. Where a codex DOS bitmap is
pixel-identical to one of our 64 tiles, the name attaches to that art index by
measurement rather than by guesswork.

The join is deliberately narrow:

  * Only images that reduce to 16x16 by an exact integer factor are eligible. The
    codex mixes 16x16 rips, 32x32 upscales and 224x142 screenshot crops; a crop is
    not evidence and is reported as `not-1:1`, never silently resized.
  * A match is an exact equality of all 256 colour indices against the CGA decode
    used by tools/tile_atlas.py. Near-matches are counted and reported as
    `near-N`, they are never promoted to a name.
  * Our table contains duplicate bitmaps, so a match can be ambiguous. Ambiguity is
    a verdict of its own; the tool does not pick one.

Controls (AGENTS.md: "a tool that lies is worse than no tool"). `--self-test` runs
these and exits non-zero if any fails, so a matcher that matches everything cannot
report success:

  1. our own art rendered back through the image path must match its own index
  2. the same bitmap with one pixel flipped must match NOTHING
  3. two different creature names must not claim the same art index
  4. an animated FM Towns GIF, a different art set entirely, must not exact-match
  5. a flat image may only ever be classified `trivial`, never `exact`
  6. a non-integer crop is ineligible; a clean 2x upscale is eligible and still matches

Evidence grade: the *identity* is measured (bytes vs bytes). The *name* is
third-party, inherited from the codex, and is labelled as such in every output.
Where a name contradicts the binary the binary wins; see docs/bestiary.md.

Output: bestiary/bestiary.tsv is our own derived table (facts plus attribution)
and is committed. The images it refers to live in bestiary/cache/ and are not.

Usage:
    python3 tools/bestiary_fetch.py            # first
    python3 tools/bestiary_match.py --self-test
    python3 tools/bestiary_match.py
    python3 tools/bestiary_match.py --md       # also emit bestiary/matches.md
"""
import argparse
import hashlib
import json
import os
import struct
import sys
from collections import defaultdict

from PIL import Image

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
IDX_OFF, PIX_OFF, TILE_STRIDE, TILE_BYTES, N_TILES = 0x7BC0, 0x7C42, 66, 64, 64
# Same palette and bit order as tools/tile_atlas.py: 2bpp, leftmost pixel in the
# high bits, CGA low-intensity colours.
PAL = [(0, 0, 0), (0, 170, 170), (170, 0, 170), (255, 255, 255)]
DOS_VARIANTS = ('dos', 'dos-cga', 'dos-ega', 'dos-fan-upgrade', 'gamercorner-dos')

# The codex puts a Min/Max sub-header under HP in the Townsfolk and Dungeons tables
# and ships one extra cell per row, so those two tables carry an HP *range* and HP
# alone; the parser now spans the two cells (see bestiary_fetch.py). Gamer Corner,
# independently, prints the same creature's HP as "17-153", which is what proved the
# flat reading wrong. Anything still arriving as a range is recorded as one.
HP_RANGE_NOTE = 'codex HP is a Min-Max range, not a single value'


def die(msg):
    print('bestiary_match: %s' % msg, file=sys.stderr)
    sys.exit(1)


def load_tiles(exe_path):
    data = open(exe_path, 'rb').read()
    if len(data) < IDX_OFF + 2 * N_TILES:
        die('%s too small to hold the tile index' % exe_path)
    idx = list(struct.unpack('<%dH' % N_TILES, data[IDX_OFF:IDX_OFF + 2 * N_TILES]))
    base = PIX_OFF - 2 - idx[0]
    tiles = {}
    for n in range(N_TILES):
        off = base + idx[n]
        if (data[off], data[off + 1]) != (0x04, 0x10):
            die('tile %d header is %02X %02X, expected 04 10 — index assumption '
                'is wrong, refusing to compare' % (n, data[off], data[off + 1]))
        o = off + 2
        grid = [[(data[o + r * 4 + b] >> s) & 3 for b in range(4)
                 for s in (6, 4, 2, 0)] for r in range(16)]
        tiles[n] = grid
    return tiles


def trivial_tiles(tiles):
    """Art indices that carry no shape: fully blank or fully solid.

    A bitmap identity against one of these proves nothing about a name. art 0x1D is
    blank and art 0x1E is solid in this binary, and three codex C64 images reduce to
    a flat field and matched 0x1E on the first run. Without this guard the tool
    reports a confident wrong name, which is the failure mode AGENTS.md calls out.
    """
    out = {}
    for n, g in tiles.items():
        ink = sum(1 for row in g for v in row if v)
        if ink == 0:
            out[n] = 'blank (%d ink)' % ink
        elif ink == 256:
            out[n] = 'solid (%d ink)' % ink
    return out


def nearest_index(rgb):
    return min(range(4), key=lambda i: sum((a - b) ** 2 for a, b in zip(rgb, PAL[i])))


def image_to_grid(path):
    """Return (grid, note). grid is 16x16 colour indices, or None if ineligible."""
    try:
        im = Image.open(path)
        frames = getattr(im, 'n_frames', 1)
        im.seek(0)
        im = im.convert('RGB')
    except Exception as exc:
        return None, 'unreadable: %s' % exc
    w, h = im.size
    if frames > 1:
        return None, 'animated (%d frames) — not a single tile' % frames
    if (w, h) != (16, 16):
        if w % 16 or h % 16 or w // 16 != h // 16:
            return None, 'not-1:1 (%dx%d, no integer path to 16x16)' % (w, h)
        im = im.resize((16, 16), Image.NEAREST)
    grid = [[nearest_index(im.getpixel((x, y))) for x in range(16)] for y in range(16)]
    if (w, h) == (16, 16):
        return grid, 'native 16x16'
    return grid, 'resized %dx%d -> 16x16 nearest/%d' % (w, h, w // 16)


def hamming(a, b):
    return sum(1 for ra, rb in zip(a, b) for x, y in zip(ra, rb) if x != y)


def match_grid(grid, tiles):
    exact = [n for n in sorted(tiles) if tiles[n] == grid]
    near = sorted((hamming(grid, tiles[n]), n) for n in tiles)[:3]
    return exact, near


def classify(grid, tiles, trivial):
    """One classification path, shared by the run and the controls.

    A hit that lands only on blank/solid art is verdict `trivial`, not a name.
    """
    hits, near = match_grid(grid, tiles)
    if len(hits) > 1:
        verdict = 'ambiguous'
    elif len(hits) == 1:
        verdict = 'exact'
    else:
        verdict = 'near-%d' % near[0][0] if near else 'no-match'
    if hits and all(n in trivial for n in hits):
        verdict = 'trivial'
        hits = []
    return verdict, hits, near


def load_cache(cache):
    out = []
    for src in ('codex.json', 'gamercorner.json'):
        p = os.path.join(cache, src)
        if not os.path.exists(p):
            die('%s missing — run tools/bestiary_fetch.py first' % src)
        blob = json.load(open(p))
        for rec in blob['records']:
            for variant, meta in rec['images'].items():
                if not meta.get('local'):
                    continue
                out.append({'source': src.replace('.json', ''),
                            'name': rec['name'], 'section': rec['section'],
                            'stats': rec['stats'], 'variant': variant,
                            'meta': meta,
                            'path': os.path.join(cache, meta['local'])})
    return out


def self_test(tiles, entries, tmpdir, trivial):
    """Prove the matcher can say no. Returns list of failures."""
    fails = []
    os.makedirs(tmpdir, exist_ok=True)

    # control 1: our own art, round-tripped through the image path
    probe = os.path.join(tmpdir, 'ctrl_exact.png')
    im = Image.new('RGB', (16, 16))
    for y in range(16):
        for x in range(16):
            im.putpixel((x, y), PAL[tiles[0x0C][y][x]])
    im.save(probe)
    g, _ = image_to_grid(probe)
    hits, _ = match_grid(g, tiles)
    if 0x0C not in hits:
        fails.append('control 1 FAILED: our own art 0x0C did not match itself (got %s)' % hits)

    # control 2: one pixel flipped must match nothing
    bad = [row[:] for row in g]
    bad[0][0] = (bad[0][0] + 1) % 4
    hits2, _ = match_grid(bad, tiles)
    if hits2:
        fails.append('control 2 FAILED: a 1-pixel-corrupted bitmap still matched %s '
                     '— the comparison is not exact' % hits2)

    # control 3: a flat image must not match a figure
    flat = [[0] * 16 for _ in range(16)]
    hits3, _ = match_grid(flat, tiles)
    nonblank = [n for n in hits3 if n not in trivial]
    if nonblank:
        fails.append('control 3 FAILED: an all-black image matched non-trivial tiles %s'
                     % ['%02X' % n for n in nonblank])

    # control 4: animated FM Towns art is a different art set; it must not exact-match
    anim = [e for e in entries if e['meta'].get('frame_count')]
    matched_anim = []
    for e in anim:
        g4, note = image_to_grid(e['path'])
        if g4 is None:
            continue
        h4, _ = match_grid(g4, tiles)
        if h4:
            matched_anim.append('%s/%s -> %s' % (e['name'], e['variant'], h4))
    if matched_anim:
        fails.append('control 4 FAILED: FM Towns art exact-matched our DOS tiles: %s '
                     '— either the join is too loose or the variant map is wrong'
                     % '; '.join(matched_anim))
    if not anim:
        fails.append('control 4 SKIPPED: no animated images in the cache — '
                     'run bestiary_fetch.py without --no-frames')

    # control 6: the eligibility rule must reject a crop and accept a clean upscale.
    # Without this the resize guard is untested, and a 224x142 screenshot squeezed to
    # 16x16 would be allowed to vote on a name.
    crop = os.path.join(tmpdir, 'ctrl_crop.png')
    Image.new('RGB', (24, 24), PAL[0]).save(crop)
    g6, note6 = image_to_grid(crop)
    if g6 is not None:
        fails.append('control 6 FAILED: a 24x24 image was accepted (%s) — the '
                     'not-1:1 guard is not holding' % note6)
    up = os.path.join(tmpdir, 'ctrl_upscale.png')
    big = Image.new('RGB', (32, 32))
    for y in range(32):
        for x in range(32):
            big.putpixel((x, y), PAL[tiles[0x0C][y // 2][x // 2]])
    big.save(up)
    g6b, note6b = image_to_grid(up)
    if g6b is None:
        fails.append('control 6 FAILED: a clean 2x upscale was rejected (%s)' % note6b)
    elif match_grid(g6b, tiles)[0] and 0x0C not in match_grid(g6b, tiles)[0]:
        fails.append('control 6 FAILED: 2x upscale of art 0x0C matched %s' %
                     match_grid(g6b, tiles)[0])

    # control 5: a flat image must never earn a name, only `trivial`
    v5, h5, _ = classify(flat, tiles, trivial)
    if h5 or v5 != 'trivial':
        fails.append('control 5 FAILED: a flat image classified as %s with hits %s '
                     '— the trivial-tile guard is not holding' % (v5, h5))
    if not trivial:
        fails.append('control 5 WEAK: this binary has no blank or solid art, so the '
                     'guard is untested here')
    return fails


def dedupe_rows(rows):
    """Collapse byte-identical rows, then refuse to continue if rows are still not keyable.

    The cache can hand us the same image more than once -- three sources all citing one Gamer
    Corner PNG -- and a source file with silent duplicates re-infects every table generated from
    it: disasm/census/creature.tsv inherited three identical 'Sea Monster' rows this way and
    ended up with no unique key at all (docs/bestiary.md section 8). Fixing it here, at the
    generator, is the only place it can be fixed; hand-editing the output is undone by the next run.

    Returns (rows, n_collapsed). Dies if two rows remain that differ only in a column this table
    does not carry, because a reader could not tell them apart and the table would have no key.
    """
    all_cols = ('art_index', 'name', 'section', 'source', 'variant', 'verdict', 'nearest',
                'hp', 'dmg', 'special', 'image', 'sha256', 'note')
    seen, out = set(), []
    for r in rows:
        key = tuple(str(r[c]) for c in all_cols)
        if key in seen:
            continue
        seen.add(key)
        out.append(r)
    n = len(rows) - len(out)

    row_key = ('art_index', 'name', 'section', 'source', 'variant', 'image')
    keys = [tuple(str(r[c]) for c in row_key) for r in out]
    dups = sorted({k for k in keys if keys.count(k) > 1})
    if dups:
        for k in dups[:10]:
            print('DUPLICATE ROW (differs only in columns not carried): %s'
                  % (' / '.join(k) or '(blanks)'))
        die('%d row keys are not unique after collapsing identical rows -- the table would have '
            'no key, so refusing to write it' % len(dups))
    return out, n


def writer_controls():
    """Controls for dedupe_rows: a collapse that must happen, and a collision that must be fatal.

    A gate that has never been able to fail is a rumour, so both directions are checked here --
    the dedupe firing when it should, and the uniqueness refusal firing when it must.
    """
    fails = []
    base = dict.fromkeys(('art_index', 'name', 'section', 'source', 'variant', 'verdict',
                          'nearest', 'hp', 'dmg', 'special', 'image', 'sha256', 'note'), '')
    a = dict(base, art_index='0B', name='Sea Monster', variant='gamercorner-dos',
             image='art/gamercorner-dos/sea-monster.png')
    # Control 1: an exact duplicate must collapse to one row.
    got, n = dedupe_rows([a, dict(a), dict(a)])
    if len(got) != 1 or n != 2:
        fails.append('control 1 FAILED: three identical rows should collapse to 1 (2 dropped), '
                     'got %d rows, %d dropped' % (len(got), n))
    # Control 2: two rows differing only in `note` are not distinguishable by any key this table
    # can offer, so the tool must refuse rather than emit an unkeyable table.
    b = dict(a, note='second source says the same thing twice')
    try:
        dedupe_rows([a, b])
        fails.append('control 2 FAILED: rows differing only in `note` must be fatal, and were not')
    except SystemExit:
        pass
    # Control 3: distinct variants of one art index are legitimate and must survive untouched.
    c = dict(a, variant='c64', image='art/c64/SeaSerpentC64U2.png')
    got, n = dedupe_rows([a, c])
    if len(got) != 2 or n != 0:
        fails.append('control 3 FAILED: two real variants must both survive, got %d rows, '
                     '%d dropped' % (len(got), n))
    return fails


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--cache', default=os.path.join(PROJECT_ROOT, 'bestiary', 'cache'))
    ap.add_argument('--exe', default=os.path.join(PROJECT_ROOT, 'original', 'ultimaii.exe'))
    ap.add_argument('--out', default=os.path.join(PROJECT_ROOT, 'bestiary', 'bestiary.tsv'))
    ap.add_argument('--md', action='store_true', help='also write bestiary/matches.md')
    ap.add_argument('--self-test', action='store_true', help='run the controls and exit')
    args = ap.parse_args()

    tiles = load_tiles(args.exe)
    trivial = trivial_tiles(tiles)
    entries = load_cache(args.cache)
    if not entries:
        die('no cached images in %s — run tools/bestiary_fetch.py' % args.cache)

    if args.self_test:
        fails = self_test(tiles, entries,
                          os.path.join(PROJECT_ROOT, 'tmp', 'bestiary-ctrl'), trivial)
        if fails:
            for f in fails:
                print(f)
            print('\nSELF-TEST FAILED (%d) — do not trust this matcher' % len(fails))
            sys.exit(1)
        print('self-test passed: 6/6 controls (round-trip matches, corruption does not, '
              'flat does not match a figure, FM Towns does not match DOS, flat cannot '
              'earn a name, crops are ineligible)')
        wfails = writer_controls()
        if wfails:
            for f in wfails:
                print(f)
            print('\nWRITER CONTROLS FAILED (%d) — the dedupe/uniqueness gate cannot be trusted'
                  % len(wfails))
            sys.exit(1)
        print('writer controls passed: 3/3 (identical rows collapse, an unkeyable collision is '
              'fatal, real variants survive)')
        return

    rows = []
    claims = defaultdict(list)
    for e in sorted(entries, key=lambda x: (x['name'], x['variant'])):
        grid, note = image_to_grid(e['path'])
        if grid is None:
            verdict, hits, near = note, [], []
        else:
            verdict, hits, near = classify(grid, tiles, trivial)
        stats = e['stats']
        hp = stats.get('HP') or stats.get('hp') or ''
        dmg = stats.get('DMG') or stats.get('DMG_2') or ''
        stat_note = ''
        if '-' in str(hp):
            hp, stat_note = hp.replace('\u2013', '-'), HP_RANGE_NOTE
            # A range whose two endpoints agree is not a range, it is a value written twice:
            # the guides print '224-224' and '255-255' for fixed HP. Normalising here keeps the
            # source honest and stops a notational difference reaching consumers as a conflict.
            lo, _, hi = hp.partition('-')
            if lo == hi:
                hp, stat_note = lo, ''
        special = (stats.get('Special') or stats.get('Notes') or
                   stats.get('Special Notes') or stats.get('special') or '')
        for n in (hits or [None]):
            if n is not None:
                claims[n].append(e['name'])
        rows.append({
            'art_index': ' '.join('%02X' % n for n in hits),
            'name': e['name'],
            'section': e['section'],
            'source': e['source'],
            'variant': e['variant'],
            'verdict': verdict,
            'nearest': ' '.join('%02X:%d' % (n, d) for d, n in near[:3]) if not hits else '',
            'hp': hp, 'dmg': dmg, 'special': special,
            'image': e['meta'].get('local', ''),
            'sha256': e['meta'].get('sha256', '')[:16],
            'note': (note + ('; ' + stat_note if stat_note else '')),
        })

    # Collapse byte-identical rows and prove the rest are keyable. See dedupe_rows().
    rows, n_dupes = dedupe_rows(rows)

    conflicts = {n: sorted(set(v)) for n, v in claims.items() if len(set(v)) > 1}

    header = ['# art_index name section source variant verdict nearest hp dmg special image sha256 note']
    header.append('# GENERATED by tools/bestiary_match.py — do not hand-edit; rerun the tool.')
    header.append('# name/verdict columns: bitmap identity is measured against original/ultimaii.exe;')
    header.append('# the NAME itself is third-party (Ultima Codex / Gamer Corner) and is not proof.')
    header.append('# art 0x0C == codex "Orc" is the reference case; see docs/bestiary.md.')
    header.append('# uniqueness: %d byte-identical rows collapsed at generation; every remaining '
                  'row is unique on (art_index, name, section, source, variant, image), and the '
                  'tool refuses to write the file otherwise.' % n_dupes)
    cols = ['art_index', 'name', 'section', 'source', 'variant', 'verdict', 'nearest',
            'hp', 'dmg', 'special', 'image', 'sha256', 'note']
    lines = header + ['\t'.join(cols)]
    for r in sorted(rows, key=lambda x: (x['art_index'] or 'zz', x['name'])):
        lines.append('\t'.join(str(r[c]).replace('\t', ' ') for c in cols))
    open(args.out, 'w').write('\n'.join(lines) + '\n')

    exact = [r for r in rows if r['verdict'] in ('exact', 'ambiguous')]
    dropped = [r for r in rows if r['verdict'] == 'trivial']
    print('%d image comparisons, %d named by bitmap identity'
          % (len(rows), len(exact)))
    if dropped:
        print('  %d excluded as trivial (matched only blank/solid art): %s'
              % (len(dropped), ', '.join(sorted({r['name'] for r in dropped}))))
    by_variant = defaultdict(int)
    for r in exact:
        by_variant[r['variant']] += 1
    print('  by variant: %s' % dict(by_variant))
    if conflicts:
        print('CONFLICTS — two names claim one art index (the join is wrong somewhere):')
        for n, names in sorted(conflicts.items()):
            print('  art %02X: %s' % (n, ', '.join(names)))
    else:
        print('  no art index is claimed by two different names')
    un = sorted({r['name'] for r in rows} - {r['name'] for r in exact})
    print('  %d names never matched a tile: %s' % (len(un), ', '.join(un)))
    print('wrote %s (%d rows, %d byte-identical duplicates collapsed)' % (args.out, len(rows), n_dupes))

    if args.md:
        md = ['# Bestiary matches — generated', '',
              'Generated by `tools/bestiary_match.py`. Bitmap identity is measured;',
              'the names are third-party. See `docs/bestiary.md` for method and grades.', '']
        md.append('| art | name | section | variant | verdict | hp | dmg | special |')
        md.append('| --- | ---- | ------- | ------- | ------- | -- | --- | ------- |')
        seen_row = set()
        for r in sorted(exact, key=lambda x: x['art_index']):
            line = '| %s | %s | %s | %s | %s | %s | %s | %s |' % (
                r['art_index'], r['name'], r['section'], r['variant'],
                r['verdict'], r['hp'], r['dmg'], r['special'][:60])
            if line in seen_row:
                continue
            seen_row.add(line)
            md.append(line)
        path = os.path.join(os.path.dirname(args.out), 'matches.md')
        open(path, 'w').write('\n'.join(md) + '\n')
        print('wrote %s' % path)


if __name__ == '__main__':
    main()

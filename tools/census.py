#!/usr/bin/env python3
"""census.py — the one-stop lookup for places, names, people, tiles and monsters.

WHY THIS EXISTS. The facts were already in the repo, spread across six generated tables and two
hand-curated wiki crosswalks, with two more (time-door cells, the vehicle census) existing only
as program output or as prose in a findings document. That layout makes every question a join
you have to remember how to do, and it let a generated file sit stale unnoticed. This tool does
the join once, in one place, and writes it down.

THE POLICY, ruled by the owner 2026-11-09:

    **bytes decide identity; the wiki decides names.**

Which sector a place is, what tile it is made of, what a monster drops, where a time door
lands -- those come from the executable and the content files, and nothing external overrides
them. What a place is *called* comes from the third-party tables, because the game often does
not name its own villages: its tiles spell shop signs instead. A name read off the bytes still
outranks a name read off a wiki, and every row says which it got, in `name_source` and `grade`.

Provenance is kept on purpose. "Definitive" does not mean "unattributed": the distinction between
`[bytes]` and `[inferred]` is what caught that the game spells TOMMERSVILLE itself, and that its
tiles say PIRATES COVE where the wiki says Pirates Harbour. Flatten that column and those
catches stop being possible.

WHAT IT READS (generated tables and curated inputs only -- it invents nothing):

    disasm/place-index.tsv        sectors, world, era, kind, byte-name, tile text   (place_index.py)
    disasm/planet-index.tsv       worlds and hyperjump coordinates                 (planet_index.py)
    disasm/npc-index.tsv          speakers per sector, with quest role              (npc_index.py)
    disasm/tile-art.tsv           the 64 art entries                                 (tile_atlas.py)
    disasm/place-wiki-names.tsv   third-party place names, hand-curated, graded
    disasm/npc-wiki-names.tsv     third-party NPC names, hand-curated, graded
    disasm/source-gamercorner-locations.csv   vendored upstream table
    bestiary/bestiary.tsv         creature names joined by bitmap identity
    original/ultimaii.exe         the time-door cell table, read at 0x2998
    original/mapx*, updated/MAPG* tile instances (vehicles, racks)

WHAT IT WRITES: disasm/census/<entity>.tsv. This tool is the ONLY writer of that directory.

USAGE
    python3 tools/census.py                       # build
    python3 tools/census.py --verify              # build and assert the invariants
    python3 tools/census.py --check-fresh         # fail if the committed census is stale
    python3 tools/census.py --what place --world pluto
"""

import argparse
import csv
import glob
import io
import json
import os
import re
import subprocess
import sys
import tempfile

# The project root, derived rather than assumed: census.py lives in tools/, and the tool is run
# from both the repo root and engine/, so a relative 'asm' would silently find nothing when
# invoked from the wrong place -- the same class of failure as a gate with no files to read.
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
D = lambda *p: os.path.join(ROOT, *p)

CENSUS_DIR = D('disasm', 'census')
GRID = 64

# Vehicle map bytes. `byte >> 1` is the gate value and equals these mode constants:
# asm/2FAD dispatches 0x22 horse / 0x24 ship / 0x26 plane / 0x28 rocket (docs/handoff-vehicle-
# state-machine.md). The art index is `byte >> 2` and is a different quantity -- see
# docs/art-index.md, closed as G1 at 256/256 pixel match.
VEHICLES = {0x44: 'horse', 0x48: 'ship', 0x4C: 'plane', 0x50: 'rocket'}
RACKS = {0x54: 'armour rack', 0x58: 'weapon rack'}

# The guide's World column uses its own era labels. This is the only mapping in this file that
# is not read from bytes, so it is stated as a table rather than derived from a string match.
GUIDE_WORLD = {
    'Legends': ('EARTH', 0), 'Pangea': ('EARTH', 1), 'B.C.': ('EARTH', 2),
    'A.D.': ('EARTH', 3), 'Aftermath': ('EARTH', 4),
    'Mercury': ('MERCURY', 1), 'Venus': ('VENUS', 2), 'Mars': ('MARS', 3),
    'Jupiter': ('JUPITER', 4), 'Saturn': ('SATURN', 5), 'Uranus': ('URANUS', 6),
    'Neptune': ('NEPTUNE', 7), 'Pluto': ('PLUTO', 8), 'X': ('PLANET X', 9),
}
GUIDE_TYPE = {'Village': 'village', 'Town': 'town', 'Castle': 'castle',
              'Tower': 'tower', 'Dungeon': 'dungeon'}


def read_tsv(path, skip_comments=1):
    """Read a generated TSV into dicts keyed by its header row."""
    if not os.path.exists(path):
        return []
    lines = [l.rstrip('\n') for l in open(path) if l.strip() and not l.startswith('#')]
    if not lines:
        return []
    hdr = lines[0].split('\t')
    out = []
    for line in lines[1:]:
        f = line.split('\t')
        if len(f) < len(hdr):
            f += [''] * (len(hdr) - len(f))
        out.append(dict(zip(hdr, f)))
    return out


def read_guide(path):
    """The vendored Gamer Corner CSV, minus its comment banner."""
    if not os.path.exists(path):
        return []
    body = '\n'.join(l for l in open(path) if not l.startswith('#'))
    return list(csv.DictReader(io.StringIO(body)))


def load_maps():
    """(layer, sector) -> tile bytes, for both content layers."""
    out = {}
    for path in sorted(glob.glob(D('original', 'mapx*'))):
        n = os.path.basename(path)
        out[('mapx', n[4:])] = open(path, 'rb').read()[:GRID * GRID]
    upd = D('original', 'updated')
    if os.path.isdir(upd):
        for path in sorted(glob.glob(os.path.join(upd, 'MAPG*'))):
            n = os.path.basename(path)
            out[('mapg', n[4:])] = open(path, 'rb').read()[:GRID * GRID]
    return out


def gate_rows(exe):
    """Time-door cells and destinations, from tools/gate_graph.py's own functions."""
    sys.path.insert(0, D('tools'))
    try:
        import gate_graph
    except Exception as e:                       # noqa: BLE001 - degrade, do not guess
        print('  gate table unavailable (%s)' % e)
        return []
    cells = gate_graph.cells(exe)
    if not cells:
        print('  gate cell table not readable at %#07x' % gate_graph.CELLS_OFF)
        return []
    g = gate_graph.graph()
    rows = []
    for e in range(5):
        for k in range(4):
            x, y = cells[e][k]
            rows.append({'era': gate_graph.ERAS[e], 'era_index': e, 'gate_index': k,
                         'x': x, 'y': y, 'dest_era': gate_graph.ERAS[g[e][k]],
                         'dest_era_index': g[e][k],
                         'source': 'asm/26B7+26C3+26C6 rule; cells at exe 0x%04x'
                                   % gate_graph.CELLS_OFF,
                         'grade': 'measured'})
    return rows


def build():
    places = read_tsv(D('disasm', 'place-index.tsv'))
    worlds = read_tsv(D('disasm', 'planet-index.tsv'))
    npcs = read_tsv(D('disasm', 'npc-index.tsv'))
    tiles = read_tsv(D('disasm', 'tile-art.tsv'))
    npc_wiki = read_tsv(D('disasm', 'npc-wiki-names.tsv'))
    guide = read_guide(D('disasm', 'source-gamercorner-locations.csv'))
    bestiary = read_tsv(D('bestiary', 'bestiary.tsv'))
    maps = load_maps()

    # ---- world: one row per world, plus Earth's five eras as their own rows ---------------
    wrows = []
    for w in worlds:
        idx = int(w['world'])
        earth = idx == 0
        wrows.append({
            'index': idx, 'name': w['name'],
            'kind': 'earth' if earth else 'planet',
            'era_index': '' if earth else idx,
            'era_label': '' if earth else '',
            'landing_sector': w['sector'],
            'xeno': w.get('xeno', ''), 'yako': w.get('yako', ''), 'zebo': w.get('zebo', ''),
            'map_layer': 'mapx' if earth else ('mapx+mapg' if idx >= 5 else 'mapg'),
            'source': 'world names at exe 0x072b9; hyperjump table at exe 0x07212',
            'grade': 'measured'})
        if earth:
            for e, lab in ((0, 'Time of Legends'), (1, 'Pangea'), (2, '1423 B.C.'),
                           (3, '1990 A.D.'), (4, '2112 A.D. Aftermath')):
                wrows.append({'index': 0, 'name': 'EARTH', 'kind': 'era', 'era_index': e,
                              'era_label': lab, 'landing_sector': '%d0' % e,
                              'xeno': '', 'yako': '', 'zebo': '', 'map_layer': 'mapx',
                              'source': 'era strings in asm/3546 (ANOS: lines)',
                              'grade': 'measured'})

    # ---- sector: identity from bytes, name from bytes-then-wiki ----------------------------
    srows = []
    for p in places:
        layer, sec = p['layer'], p['sector']
        cand = (p.get('candidate') or '').split(';')[0].strip()
        wiki = (p.get('wiki') or '').split(';')[0].strip()
        wname, wgrade = (wiki.split(' [', 1) + [''])[:2] if wiki else ('', '')
        if cand:
            name, src, grade = cand, 'bytes', 'measured'
        elif wname:
            name, src, grade = wname, 'wiki', wgrade.rstrip(']') or 'inferred'
        else:
            name, src, grade = '', 'unnamed', ''
        tiles_here = maps.get((layer, sec), b'')
        srows.append({
            'layer': layer, 'sector': sec,
            'world': p['world'], 'era': p['era'].split(' (ANOS')[0],
            'kind': p['kind'],
            'name': name, 'name_source': src, 'grade': grade,
            'guide_name': '', 'guide_treasure': '',
            'tile_text': p.get('rooms', ''),
            'glyphs': p.get('glyphs', ''), 'tlkx_lines': p.get('dialogue', ''),
            'vehicles': '', 'note': p.get('note', ''),
            'source': 'identity: place_index.py from mapx/MAPG bytes; name: %s' % src})

    # ---- place: the guide's 27 rows, joined onto sectors by (world, era, type) -------------
    by_key = {}
    for r in srows:
        tens = r['sector'][0]
        by_key.setdefault((r['world'], r['era'], r['kind'], r['layer']), []).append(r)
    prows = []
    for g in guide:
        if not g.get('Name'):
            continue
        world, era_index = GUIDE_WORLD.get(g['World'], ('', -1))
        kind = GUIDE_TYPE.get(g['Type'], '')
        hits = [r for r in srows
                if r['world'] == world and r['kind'] == kind
                and (era_index < 0 or r['sector'][0] == str(era_index))]
        joined = '; '.join('%s/%s' % (h['layer'], h['sector']) for h in hits)
        for h in hits:
            h['guide_name'] = g['Name']
            h['guide_treasure'] = g.get('Treasure', '')
        prows.append({
            'guide_name': g['Name'], 'guide_world': g['World'], 'guide_type': g['Type'],
            'treasure': g.get('Treasure', ''),
            'sectors': joined, 'resolved': 'yes' if hits else 'NO',
            'source': 'disasm/source-gamercorner-locations.csv',
            'grade': 'inferred',
            'note': '' if hits else 'no sector of that world/era and kind exists'})

    # ---- vehicle: every vehicle and rack tile, which was prose until now -------------------
    vrows = []
    for (layer, sec), data in sorted(maps.items()):
        counts = {}
        for i, v in enumerate(data):
            what = VEHICLES.get(v) or RACKS.get(v)
            if what:
                vrows.append({'layer': layer, 'sector': sec, 'x': i % GRID, 'y': i // GRID,
                              'byte': '0x%02x' % v, 'what': what,
                              'art': '0x%02x' % (v >> 2), 'gate_value': '0x%02x' % (v >> 1),
                              'source': 'mapx/MAPG tile bytes', 'grade': 'observed'})
                counts[what] = counts.get(what, 0) + 1
        if counts:
            for r in srows:
                if r['layer'] == layer and r['sector'] == sec:
                    r['vehicles'] = '; '.join('%s x%d' % (k, n) for k, n in sorted(counts.items()))

    # ---- tile: the 64 art entries, with how often each actually occurs ---------------------
    inst = {}
    for data in maps.values():
        for v in data:
            inst[v >> 2] = inst.get(v >> 2, 0) + 1
    trows = []
    asm_names = asm_tile_names(os.path.join(ROOT, 'asm'))
    for t in tiles:
        a = (t.get('id') or '').strip()
        if not a:
            continue
        try:
            ai = int(a, 16)
        except ValueError:
            continue
        mb = ai << 2
        # The name comes from the proof layer if the proof layer has one. tile-art.tsv carries
        # a pixel DESCRIPTION and never claimed to name the tile; conflating the two is what
        # let a wrong name ride in wearing a `measured` badge.
        named = asm_names.get(mb)
        fname = font_name(ai)
        # The vehicle and rack bytes are already named by the dispatch that consumes them, so
        # leaving those tiles `unnamed` would be a gap the census chose not to close.
        vlabel = VEHICLES.get(mb) or RACKS.get(mb)
        # Vehicles are named by the dispatch that boards them; racks by the cmd_G arm that
        # empties them. Citing the wrong routine is a wrong citation, not a near miss.
        vsrc = ('asm/2FAD_vehicle_board_dispatch.asm'
                if mb in VEHICLES else 'asm/363B_cmd_G.asm')
        eff = TILE_EFFECTS.get(mb)
        own = OWNER_NAMES.get(ai)
        role = tile_role(ai, mb, t.get('kind', ''))
        # art 0x28 keeps role 'site door' and name 'I' at once: map 0xA0 >> 2 == 0x28, so the
        # door genuinely IS the letter-I glyph reused. That is a real collision, not a mapping
        # bug, and the owner confirmed it -- so it carries both and neither is hidden.
        if role == 'letter' or ai in SIGN_PARTS:
            role = 'sign'
        sign_part = SIGN_PARTS.get(ai, 'glyph' if role == 'sign' else '')
        trows.append({'art': '0x%02x' % ai, 'map_byte': '0x%02x' % mb,
                      'kind': t.get('kind', ''), 'description': t.get('description', ''),
                      'instances': inst.get(ai, 0),
                      'role': role,
                      'sign_part': sign_part,
                      'gate_value': '0x%02x' % ((mb >> 1) & 0x7F),
                      'walkable': foot_ladder_walkable(mb),
                      # asm proof outranks the owner list, which outranks the structural font
                      # mapping; none of the three is a guess about appearance.
                      'name': (named[0] if named else
                               (own[0] if own else (fname or (vlabel or '')))),
                      'name_source': (('asm/%s' % named[1]) if named else
                                      (own[1] + ' -- ' + own[2]) if own else
                                      ('structural: art 0x20-0x39 is A-Z, 0x30 excluded'
                                       if fname else (vsrc if vlabel else ''))),
                      'name_grade': ('measured' if named else own[1] if own else
                                     'measured' if (fname or vlabel) else 'unnamed'),
                      'entry_effect': eff[0] if eff else '',
                      'entry_message': eff[1] if eff else '',
                      'boardable_by': BOARDABLE.get(mb, ''),
                      'note': (t.get('note') or '')[:200],
                      'source': 'disasm/tile-art.tsv (tile_atlas.py); art = map_byte >> 2',
                      'grade': 'measured'})

    # ---- npc: speakers, with the wiki name preferred where one exists ----------------------
    wk = {(r['layer'], r['sector'], r['record']): r['wiki_name'] for r in npc_wiki}
    nrows = []
    for n in npcs:
        key = (n['layer'], n['sector'], n['index'])
        wname = wk.get(key, '')
        nrows.append({
            'layer': n['layer'], 'sector': n['sector'], 'world': n['world'],
            'era': n['era'].split(' (ANOS')[0], 'kind': n['kind'],
            'record_index': n['index'], 'role': n['role'],
            'game_speaker': n['speaker'], 'name': wname or n['speaker'],
            'name_source': 'wiki' if wname else ('bytes' if n['speaker'] else 'unnamed'),
            'line': n['line'], 'quest': n['quest'],
            'source': 'tlkxNN record position = quest role (npc_index.py)'})

    # ---- creature: bestiary, joined to art by bitmap identity ------------------------------
    crows = []
    for b in bestiary:
        crows.append({'art': '0x%02x' % int(b['art_index'], 16) if b.get('art_index') else '',
                      'name': b.get('name', ''), 'hp': b.get('hp', ''), 'damage': b.get('dmg', ''),
                      'special': b.get('special', ''), 'verdict': b.get('verdict', ''),
                      'source': 'bestiary/bestiary.tsv (third-party art, joined by bitmap identity)',
                      'grade': 'inferred'})

    # Join creature characteristics onto the tile rows. The bestiary names are third-party and
    # joined by bitmap identity, so they carry `inferred` and a source even though they land in
    # a tile row whose other columns are measured -- the grades must not flatten on the join.
    by_art = {c['art']: c for c in crows if c['art']}
    for r in trows:
        c = by_art.get(r['art'])
        if c:
            r['creature_hp'] = c['hp']
            r['creature_damage'] = c['damage']
            r['creature_special'] = c['special']
            if not r['name'] and c['name']:
                r['name'] = c['name']
                r['name_source'] = c['source']
                r['name_grade'] = 'inferred (third-party)'

    return {'world': wrows, 'sector': srows, 'place': prows, 'vehicle': vrows,
            'tile': trows, 'npc': nrows, 'creature': crows,
            'gate': gate_rows(D('original', 'ultimaii.exe'))}


COLUMNS = {
    'world': ['index', 'name', 'kind', 'era_index', 'era_label', 'landing_sector',
              'xeno', 'yako', 'zebo', 'map_layer', 'source', 'grade'],
    'sector': ['layer', 'sector', 'world', 'era', 'kind', 'name', 'name_source', 'grade',
               'guide_name', 'guide_treasure', 'vehicles', 'tile_text', 'glyphs',
               'tlkx_lines', 'note', 'source'],
    'place': ['guide_name', 'guide_world', 'guide_type', 'treasure', 'sectors',
              'resolved', 'grade', 'source', 'note'],
    'vehicle': ['layer', 'sector', 'x', 'y', 'byte', 'what', 'art', 'gate_value',
                'source', 'grade'],
    'tile': ['art', 'map_byte', 'kind', 'role', 'sign_part', 'name', 'name_source', 'name_grade',
             'gate_value', 'walkable', 'entry_effect', 'entry_message', 'boardable_by',
             'creature_hp', 'creature_damage', 'creature_special',
             'instances', 'description', 'note', 'source', 'grade'],
    'npc': ['layer', 'sector', 'world', 'era', 'kind', 'record_index', 'role',
            'game_speaker', 'name', 'name_source', 'line', 'quest', 'source'],
    'creature': ['art', 'name', 'hp', 'damage', 'special', 'verdict', 'source', 'grade'],
    'gate': ['era', 'era_index', 'gate_index', 'x', 'y', 'dest_era', 'dest_era_index',
             'source', 'grade'],
}


def write_tables(tables, outdir):
    os.makedirs(outdir, exist_ok=True)
    for name, rows in tables.items():
        path = os.path.join(outdir, '%s.tsv' % name)
        with open(path, 'w') as f:
            f.write('# generated by tools/census.py -- do not hand-edit\n')
            f.write('# %s\n' % json.dumps({'rows': len(rows), 'entity': name}))
            f.write('\t'.join(COLUMNS[name]) + '\n')
            for r in rows:
                f.write('\t'.join(str(r.get(c, '')) for c in COLUMNS[name]) + '\n')


def foot_ladder_walkable(map_byte):
    """On-foot passability, transcribed from asm/2423 -- the same ladder the engine runs.

    The gate variable is `map_byte >> 1` (the mode), NOT the art index. Getting that wrong is
    the root of every passability regression this project has had, so it is stated here in the
    same words `overworld.c` uses. Verbatim ladder: mode 0x00 and 0x08 reject; a short accept
    list; then `>= 0x16` rejects; everything below that accepts.

    This exists because passability is the ARBITER for land-vs-water naming, and the census
    previously joined seven sources and none of them was this one. Art 0x01 was carried as
    `grade=measured` while the spec called it "shallow / rough water" -- and it is walkable,
    which is dispositive. See docs/passability-findings.md F14.
    """
    mode = (map_byte >> 1) & 0x7F
    if mode in (0x00, 0x08):
        return 'no'                      # water, mountain
    if mode in (0x22, 0x24, 0x26, 0x28, 0x2A, 0x2C, 0x2E, 0x38):
        return 'yes'                     # vehicles, racks, force field, town paving
    if mode >= 0x16:
        return 'no'
    return 'yes'


def asm_tile_names(asm_dir):
    """Tile names read out of the proof layer, so the census cannot drift from it.

    `asm/248B_swamp_damage_gate.asm` carries, in its header: "fires for map byte 0x04 and for
    nothing else among the common tiles: 0x00->0x00 water, 0x04->0x02 SWAMP, 0x08->0x04 grass,
    0x0C->0x06, 0x10->0x08 mountain". That is a byte-cited name table sitting in the
    byte-exact reconstruction -- the most authoritative place a tile name can live, and the
    one nobody grepped when the art-01 argument happened.

    Returns {map_byte: (name, citing_file)}. A name is only kept if the citing file still
    exists and still contains it, so a rename or a header edit cannot leave a stale name
    behind; `verify` re-checks the citation.
    """
    out = {}
    if not os.path.isdir(asm_dir):
        return out
    pat = re.compile(r'(0x[0-9A-Fa-f]{2})->(0x[0-9A-Fa-f]{2})\s+([A-Za-z]{3,20})')
    for fn in sorted(os.listdir(asm_dir)):
        if not fn.endswith('.asm'):
            continue
        try:
            text = open(os.path.join(asm_dir, fn)).read()
        except OSError:
            continue
        # Headers wrap, and the citation wraps mid-entry: asm/248B writes
        # "... 0x10->0x08" and puts "mountain." on the next line. Matching line by line
        # silently loses exactly the entry that matters most, so the header block is joined
        # into one string first. Bodies are still excluded -- only comments are naming evidence.
        header = ' '.join(l.lstrip('#').strip() for l in text.splitlines() if l.startswith('#'))
        for mb, _mode, word in pat.findall(header):
            word = word.strip().lower().rstrip('.,;').strip()
            # the header writes SWAMP in caps when it is the point of the line
            if word in ('water', 'swamp', 'grass', 'forest', 'mountain'):
                out.setdefault(int(mb, 16), (word, fn))
    return out


# Tile characteristics that are proven but were sitting in a prose table in
# docs/handoff-tile-effects.md instead of in the census -- the same failure as passability: a
# curated, byte-cited table the join walked past. Keyed on MAP BYTE (not art, not mode), because
# that is what the effects ladder tests. Each entry cites the routine that proves it.
TILE_EFFECTS = {
    0x00: ('reject: open sea',   '--INVALID MOVE!',              'asm/2423'),
    0x04: ('-5 HP on entry',     'none (silent)',                'asm/2495_swamp_damage'),
    0x08: ('none (grass)',       '',                             'asm/2423'),
    0x0C: ('none',               '',                             'asm/2423'),
    0x10: ('reject: mountain',   '--INVALID MOVE!',              'asm/2423'),
    0x44: ('board horse',        '',                             'asm/2FAD'),
    0x48: ('board ship',         '',                             'asm/2FAD'),
    0x4C: ('board plane',        '',                             'asm/2FAD'),
    0x50: ('board rocket',       '',                             'asm/2FAD'),
    0x5C: ('-1000 HP unless ring protected',
                               'FIELD CAUSES 1000 DAMAGE! / RING PROTECTS FROM FIELD!',
                                'asm/24B1_force_field_gate'),
    0x70: ('town paving',        '',                             'asm/2423'),
}
BOARDABLE = {0x44: 'horse', 0x48: 'ship', 0x4C: 'plane', 0x50: 'rocket'}


# Names the owner supplied on 2026-11-09 after reviewing the census page. Each entry records
# whether bytes independently prove it (`measured`, and says which routine) or whether it stands
# on the owner's knowledge of the game (`recalled (owner)`). The grades must not flatten just
# because both arrive in the same column -- that flattening is what let "shallow water" through.
# Sources are prefixed 'proven:' / 'owner:' deliberately: a bare 'asm/...' here would be read as
# a file citation by verify's rot check, and these are reason strings, not citations.
OWNER_NAMES = {
    0x03: ('forest', 'measured', 'proven: tile-art-spec §7; asm/248B leaves map 0x0C unnamed'),
    0x05: ('village entrance', 'measured', 'proven: site_for_tile case 5 (overworld.c)'),
    0x06: ('town entrance', 'measured', 'proven: site_for_tile case 6, label U2_S_SITE_TOWN'),
    0x07: ('tower entrance', 'measured', 'proven: site_for_tile case 7'),
    0x08: ('castle entrance', 'measured', 'proven: site_for_tile case 8'),
    # DISPUTES tile-art-spec § 7, which named this "mountain pass" from the arch in the lattice.
    # The owner's reading and site_for_tile agree: the arch is a cave mouth in the hillside.
    0x09: ('dungeon entrance', 'measured',
           'proven: site_for_tile case 9; DISPUTES tile-art-spec §7 "mountain pass"'),
    0x0A: ('signpost', 'measured', 'proven: tile-art-spec §7'),
    # The census already carried this row's -1000 HP effect from the same byte; the name was
    # derivable and was left blank anyway.
    0x17: ('force field', 'measured', 'proven: asm/24B1 force-field gate on map 0x5C'),
    0x1C: ('brick road', 'measured',
           'proven: asm/2423 town paving on map 0x70; resolves the "marsh" note as wrong'),
    0x1D: ('solid black', 'measured', 'proven: tile-art.tsv, 256 black px'),
    0x1E: ('solid white', 'measured', 'proven: tile-art.tsv, 256 white px'),
    0x1F: ('sign counter', 'recalled (owner)', 'owner: a space or plain counter in a sign board'),
    0x30: ('moongate', 'measured', 'proven: map 0xC0; also the time door'),
    0x3A: ('sign border right', 'recalled (owner)', 'owner: right border cap of a sign board'),
    0x3B: ('sign border left', 'recalled (owner)', 'owner: left border cap of a sign board'),
}

# The signage family. The letters are not bare glyphs: tile-art.tsv describes them as "capital A
# with bars above and below", and tile-art-spec § 7a records that treating those bars as optional
# made every letter tile come out wrong. The owner's framing explains why -- a letter tile is a
# small SIGN PANEL bearing a letter, and it has to join edge-to-edge with the counter and the two
# border caps. The bars are the frame, not decoration.
SIGN_PARTS = {0x1F: 'counter', 0x3A: 'border right', 0x3B: 'border left'}


def tile_role(art_index, map_byte, kind):
    """What the tile IS, as distinct from what it looks like.

    Derived, not transcribed: vehicles and racks come from the same byte tables the vehicle
    scan uses, the letter range is structural, and the rest falls out of `kind`. A tile can
    only have one role, so the order below is the precedence and it is deliberate -- a moongate
    that happens to sit in the letter range is a moongate (art 0x30 is NOT the letter Q).
    """
    if map_byte in VEHICLES:
        return 'vehicle'
    if map_byte in RACKS:
        return 'rack'
    if art_index == 0x30:
        return 'moongate'
    if map_byte == 0xA0:
        return 'site door'
    if kind == 'font':
        return 'letter'
    if kind == 'creature':
        return 'creature'
    if kind == 'block':
        return 'block'
    if kind == 'terrain':
        return 'terrain'
    return 'object'


def font_name(art_index):
    """The 25 letter tiles are nameable without interpretation, so name them.

    art 0x20-0x2F are A-P and 0x31-0x39 are R-Z. 0x30 is skipped: it is the moongate, and the
    absent Q is the proof that the range is a font and not a coincidence -- see
    docs/place-index.md. This is deterministic, so it is `measured`, not inferred: 25 of the 60
    previously-unnamed tiles stop being guesses the moment the mapping is written down.
    """
    if 0x20 <= art_index <= 0x2F:
        return chr(ord('A') + (art_index - 0x20))
    if 0x31 <= art_index <= 0x39:
        return chr(ord('R') + (art_index - 0x31))
    return ''


def verify(tables):
    """The invariants. Each one names what it caught, because a check without a story rots."""
    bad = []
    s = tables['sector']
    unnamed = [r for r in s if r['name_source'] == 'unnamed']
    # A sector with no name is allowed, but only as a visible `unnamed`, never as a blank.
    if any(r['name'] for r in unnamed):
        bad.append('a sector marked unnamed still carries a name')
    for r in tables['place']:
        if r['resolved'] != 'yes':
            bad.append('guide row %r (%s %s) joined to no sector'
                       % (r['guide_name'], r['guide_world'], r['guide_type']))
    seen = set()
    for r in s:
        k = (r['layer'], r['sector'])
        if k in seen:
            bad.append('duplicate sector key %s' % (k,))
        seen.add(k)
    if not tables['gate']:
        bad.append('gate table empty: the exe cell table was not read')
    if not any(r['what'] == 'rocket' for r in tables['vehicle']):
        bad.append('no rocket tiles found: the vehicle scan is not working')
    rockets = [r for r in tables['vehicle'] if r['what'] == 'rocket']
    if len(rockets) != 4:
        bad.append('expected 4 rocket tiles, found %d' % len(rockets))
    named = sum(1 for r in s if r['name'])
    if named < 30:
        bad.append('only %d sectors named; the wiki join has regressed' % named)

    # ---- tile naming vs passability: the check that would have caught art 01 ----------------
    # Water is impassable on foot. That is not a convention, it is asm/2423, and it makes
    # "walkable" a disproof of any name containing "water". The census previously joined seven
    # sources and none of them was passability, so a wrong name arrived already wearing a
    # `measured` badge and nothing argued with it.
    for r in tables['tile']:
        nm = (r.get('name', '') + ' ' + r.get('description', '')).lower()
        if 'water' in nm and r.get('walkable') == 'yes':
            bad.append('tile %s is named/described as water but the foot ladder accepts it '
                       '(a walkable "water" is a naming error, not a terrain type)'
                       % r['art'])
        if 'mountain' in nm and r.get('walkable') == 'yes':
            bad.append('tile %s is named/described as mountain but the foot ladder accepts it'
                       % r['art'])
        # A cited proof must still say what it is cited for, or the rename rotted silently.
        src = r.get('name_source', '')
        if src.startswith('asm/') and r.get('name'):
            # citations may carry a trailing gloss: 'asm/2FAD_... (mode dispatch)'
            stem = src.split()[0]
            if not stem.endswith('.asm'):
                stem += '.asm'
            path = os.path.join(ROOT, stem)
            if not os.path.exists(path):
                bad.append('tile %s cites %s for its name and that file is gone'
                           % (r['art'], src))
            elif r['name'].split()[0].lower() not in open(path).read().lower():
                # the first word is the claim; multi-word labels like 'armour rack' are the
                # census's own phrasing, so requiring the whole phrase would fail on a
                # citation that is actually correct.
                bad.append('tile %s cites %s for name %r and the file no longer contains it'
                           % (r['art'], src, r['name']))
    # The four common ground tiles must be named by proof, not left to interpretation.
    for mb, what in ((0x00, 'water'), (0x04, 'swamp'), (0x08, 'grass'), (0x10, 'mountain')):
        row = next((r for r in tables['tile'] if r['map_byte'] == '0x%02x' % mb), None)
        if row and row.get('name') and row['name'] != what:
            bad.append('tile %s is named %r but asm/248B proves %r'
                       % (row['art'], row['name'], what))
    # ---- tile: role coverage and the naming gap must stay visible ---------------------------
    t = tables['tile']
    if any(not r.get('role') for r in t):
        bad.append('a tile has no role: tile_role() is not covering the kind set')
    signs = [r for r in t if r.get('role') == 'sign']
    glyphs = [r for r in signs if r.get('sign_part') == 'glyph']
    # 24 glyphs, not 25: art 0x30 is classified `font` by tile-art.tsv but it is the MOONGATE,
    # and the absent Q is the proof the range is a font. Asserting 25 would assert the old
    # misreading. 27 signs total = 24 glyphs + counter + two border caps.
    if len(glyphs) != 24:
        bad.append('expected 24 sign glyphs (A-P, R-Z; 0x30 is the moongate), found %d'
                   % len(glyphs))
    if len(signs) != 27:
        bad.append('expected 27 tiles in the signage family (24 glyphs + counter + 2 caps), '
                   'found %d' % len(signs))
    if any(not r.get('name') for r in signs):
        bad.append('a sign tile is unnamed: the signage mapping is not being applied')
    # Every entry effect we claim must still cite a routine that exists.
    for r in t:
        if r.get('entry_effect'):
            cite = TILE_EFFECTS.get(int(r['map_byte'], 16), (None, None, ''))[2]
            # cites are stored as 'asm/2423'; the glob must not double the prefix
            stem = cite.split('/', 1)[-1] if cite.startswith('asm/') else cite
            if cite and not glob.glob(os.path.join(ROOT, 'asm', stem + '*.asm')):
                bad.append('tile %s cites %s for its entry effect and no such asm file exists'
                           % (r['art'], cite))
    return bad, named, len(unnamed)


def render(tables, what, filt):
    rows = tables.get(what, [])
    if filt:
        k, _, v = filt.partition('=')
        rows = [r for r in rows if v.lower() in str(r.get(k, '')).lower()]
    out = io.StringIO()
    w = csv.writer(out, delimiter='\t')
    if rows:
        w.writerow(COLUMNS[what])
        for r in rows:
            w.writerow([r.get(c, '') for c in COLUMNS[what]])
    return out.getvalue()


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--verify', action='store_true', help='build in memory and assert invariants')
    ap.add_argument('--check-fresh', action='store_true',
                    help='fail if the committed census differs from a fresh build')
    ap.add_argument('--what', default=None, help='print one entity: world|sector|place|npc|tile|creature|gate|vehicle')
    ap.add_argument('--where', default=None, help='filter as column=value, substring match')
    ap.add_argument('--out', default=CENSUS_DIR)
    a = ap.parse_args()

    tables = build()

    if a.what:
        sys.stdout.write(render(tables, a.what, a.where))
        return

    if a.check_fresh:
        with tempfile.TemporaryDirectory() as td:
            write_tables(tables, td)
            stale = []
            for name in COLUMNS:
                new = open(os.path.join(td, '%s.tsv' % name)).read()
                old_path = os.path.join(a.out, '%s.tsv' % name)
                old = open(old_path).read() if os.path.exists(old_path) else ''
                if new != old:
                    stale.append(name)
            if stale:
                print('CENSUS STALE: %s differ from a fresh build' % ', '.join(sorted(stale)))
                print('  fix: python3 tools/census.py')
                sys.exit(1)
            print('CENSUS FRESH: %d tables match the generator' % len(COLUMNS))
        return

    if a.verify:
        bad, named, unnamed = verify(tables)
        if bad:
            for b in bad:
                print('VERIFY FAIL:', b)
            sys.exit(1)
        print('VERIFY OK: %d sectors (%d named, %d explicitly unnamed), %d guide rows all joined, '
              '%d vehicle tiles, %d tiles, %d npcs, %d creatures, %d gate cells'
              % (len(tables['sector']), named, unnamed, len(tables['place']),
                 len(tables['vehicle']), len(tables['tile']), len(tables['npc']),
                 len(tables['creature']), len(tables['gate'])))
        return

    write_tables(tables, a.out)
    for name in sorted(COLUMNS):
        print('  %-9s %4d rows' % (name, len(tables[name])))


main()

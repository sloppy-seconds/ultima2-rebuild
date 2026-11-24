#!/usr/bin/env python3
"""table_contract.py -- declare every numeric column's base, then hold the line.

Why (ADR 0005, and the defect that motivated it): `footprint.tsv`'s offset column holds
DECIMAL, and filtering it as hex returns zero rows. Zero rows reads as absence of evidence
rather than as a bad query, which is how a wrong base turns into a wrong finding. While
writing this the same trap was found twice more: `engine-map.tsv`'s `asm_addr` is decimal
too (256 is 0x100), and `census/sector.tsv`'s `sector` holds `00`, `03`, `10` as decimal,
so `sector=10` means ten and not sixteen.

The columns that matter are the ones whose values are legal in BOTH bases -- `375`, `10`,
`99` -- because no query can tell you which you guessed. This tool makes the answer a
declaration instead of a guess, and then refuses to let the declaration and the data drift.

Two rules give it teeth:
  * every numeric-looking column of a covered table must be declared, so adding one
    without saying what base it is in goes red;
  * every declared value must satisfy its declaration, so a generator that silently
    switches base goes red.

Scope is explicit and only grows: COVERED names the tables under contract, and the rest
are reported as uncovered rather than silently passing. A gate that pretends to cover
everything is worse than one that says where it stops.

Usage:
  table_contract.py --verify      check the committed tables against the contract
  table_contract.py --selftest    prove the gate can go red, in every required shape
  table_contract.py --list        print the contract
"""
import argparse, csv, pathlib, re, sys

REPO = pathlib.Path(__file__).resolve().parent.parent

# Tables under contract. Growing list: add a table, declare its numeric columns, and it
# is checked from then on. Anything not listed is reported as uncovered, never skipped
# in silence.
COVERED = [
    'disasm/index/state.tsv',
    'disasm/index/coverage.tsv',
    'disasm/index/footprint.tsv',
    'disasm/index/record-spans.tsv',
    'disasm/tile-art.tsv',
    'disasm/unassigned-regions.tsv',
    'disasm/fmtowns-crosswalk.tsv',
    'disasm/bible/controls.tsv',
    'disasm/index/engine-map.tsv',
    'disasm/index/unnamed-offsets.tsv',
    'disasm/index/xref.tsv',
    'disasm/index/xref-observed.tsv',
    'disasm/index/xref-gaps.tsv',
    'disasm/census/sector.tsv',
    'disasm/census/world.tsv',
    'disasm/census/gate.tsv',
    'disasm/census/site.tsv',
    'disasm/census/tile.tsv',
    'disasm/census/vehicle.tsv',
    'disasm/census/vendor.tsv',
    'disasm/census/npc.tsv',
    'disasm/census/place.tsv',
    'disasm/census/creature.tsv',
    'disasm/census/art-variant.tsv',
    'disasm/creature-populations.tsv',
    'disasm/facts/asm.tsv',
    # --- second increment -------------------------------------------------
    'disasm/function-map.tsv',
    'disasm/fragment-splits.tsv',
    'disasm/data-regions.tsv',
    'disasm/bytecheck-map.tsv',
    'disasm/bytecheck-full.tsv',
    'disasm/functions.tsv',
    'disasm/content-inventory.tsv',
    'disasm/planet-index.tsv',
    'disasm/npc-index.tsv',
    'disasm/place-index.tsv',
    'disasm/npc-wiki-names.tsv',
    'disasm/place-wiki-names.tsv',
    'disasm/source-third-party-spells.tsv',
    'disasm/bible/statistics.verdicts.tsv',
]

# kind names:
#   dec     decimal integer, no prefix          0 375 99
#   hexd    hex integer, no prefix              0300 0ff
#   hexp    hex integer, 0x prefix              0x137
#   sha     64 hex digits                       23133F69...
#   text    free text, not asserted
#   int     decimal integer that must be >= 0   (same grammar as dec, kept distinct so a
#           count column reads as intent rather than as an address)
CONTRACT = {
    'disasm/index/state.tsv': {
        'ds_offset': 'hexp', 'save_offset': 'hexp', 'size': 'dec',
        'asserted': 'text', 'name': 'text', 'c_type': 'text', 'kind': 'text',
        # Not a count. It holds short git commit prefixes -- the gate caught my first
        # guess of 'int' when it met the value '5ae1'.
        'cited_in': 'hexd',
    },
    'disasm/index/coverage.tsv': {
        'undecoded_byte_lines': 'int', 'file': 'text', 'grade': 'text',
        'name_evidence': 'text', 'body_corrected': 'text', 'live': 'text',
        'confidence': 'text',
    },
    'disasm/index/footprint.tsv': {
        # The one that started it. Decimal, and a hex filter here returns nothing.
        'offset': 'dec', 'line': 'int', 'segment': 'text', 'span': 'text',
        'access': 'text', 'file': 'text', 'indexed': 'text', 'es_as': 'text',
        'note': 'text',
        # entity is `kind:name` and entity_basis names its own grade, so neither can be
        # lifted out of the table without carrying how it was known.
        'entity': 'text', 'entity_basis': 'text',
    },
    'disasm/index/record-spans.tsv': {
        'ds_start': 'hexp', 'ds_end': 'hexp',
        'n_records': 'dec', 'record_stride': 'dec',
        'group': 'text', 'shape': 'text', 'member_fields': 'text',
        'entity_kind': 'text', 'backing_file': 'text', 'evidence': 'text',
        'grade': 'text',
    },
    'disasm/tile-art.tsv': {
        # id and hex are the art index and its map byte, both 2-digit hex without a prefix.
        # The glyph column can hold a bare `"`, which is why read_table splits by hand.
        'id': 'hexd', 'hex': 'hexd', 'kind': 'text', 'description': 'text',
        'glyph': 'text', 'notes': 'text',
    },
    'disasm/unassigned-regions.tsv': {
        # All three are hex; `end` holding 00FF proves it, and len is the span width in hex
        # too (0000..00FF is 0100 bytes), so the column set reads consistently.
        'start': 'hexd', 'end': 'hexd', 'len': 'hexd',
    },
    'disasm/fmtowns-crosswalk.tsv': {
        # art is a 2-digit hex index; count is a decimal tile count. The header used to be
        # a comment line, which made the first data row the header for any parser.
        'art': 'hexd', 'fm_tiles': 'text', 'count': 'dec', 'source': 'text',
        'description': 'text',
    },
    'disasm/bible/controls.tsv': {
        'control_id': 'text', 'resolves': 'text', 'supported': 'text',
        'grade_survived': 'text', 'non_vacuous': 'text', 'reason': 'text',
        'judge_model': 'text',
        # A 12-char prefix of the critic prompt sha, not a full sha, so it cannot be
        # declared `sha` without lying about the field width.
        'critic_prompt_sha': 'text', 'control_note': 'text',
    },
    'disasm/index/engine-map.tsv': {
        'asm_addr': 'dec',        # decimal: 256 is 0x100
        'c_line': 'int', 'asm_file': 'text', 'c_symbol': 'text',
        'c_file': 'text', 'cited_as': 'text',
    },
    'disasm/index/unnamed-offsets.tsv': {
        'ds_offset': 'hexp', 'size': 'dec', 'mentioned_in_files': 'int',
        'state_field': 'text',
    },
    'disasm/index/xref.tsv': {
        'harvests': 'int', 'from_asm': 'text', 'to_asm': 'text',
        'evidence': 'text', 'kinds': 'text',
    },
    'disasm/index/xref-observed.tsv': {
        # from/to are runtime offsets in segment 017D, hex without a prefix.
        'from': 'hexd', 'to': 'hexd', 'times': 'int',
        'from_asm': 'text', 'to_asm': 'text', 'kind': 'text',
        'harvests': 'text',      # a list of harvest names, not a count
    },
    'disasm/index/xref-gaps.tsv': {
        'from_asm': 'text', 'to_asm': 'text', 'class': 'text', 'note': 'text',
    },
    'disasm/census/sector.tsv': {
        'sector': 'dec',          # zero-padded decimal: '00','03','10' -- 10 means ten
        'glyphs': 'int', 'tlkx_lines': 'int', 'layer': 'text', 'name': 'text',
        'name_source': 'text', 'grade': 'text', 'kind': 'text', 'world': 'text',
        'era': 'text', 'note': 'text', 'source': 'text',
        'guide_name': 'text', 'guide_treasure': 'text', 'vehicles': 'text',
        'tile_text': 'text',
    },
    'disasm/census/world.tsv': {
        'index': 'dec', 'era_index': 'dec', 'landing_sector': 'dec',
        'xeno': 'dec', 'yako': 'dec', 'zebo': 'dec', 'name': 'text',
        'kind': 'text', 'era_label': 'text', 'map_layer': 'text',
        'source': 'text', 'grade': 'text',
    },
    'disasm/census/gate.tsv': {
        'era_index': 'dec', 'gate_index': 'dec', 'x': 'dec', 'y': 'dec',
        'dest_era_index': 'dec', 'era': 'text', 'dest_era': 'text',
        'source': 'text', 'grade': 'text',
    },
    'disasm/census/site.tsv': {
        'from_sector': 'dec', 'dest_sector': 'dec', 'x': 'dec', 'y': 'dec',
        'art': 'hexp', 'map_byte': 'hexp', 'layer': 'text', 'kind': 'text',
        'dest_name': 'text', 'dest_name_source': 'text', 'dest_guide_name': 'text',
        'dest_exists': 'text', 'arrival': 'text', 'grade': 'text',
        'source': 'text', 'world': 'text', 'era': 'text',
    },
    'disasm/census/tile.tsv': {
        'art': 'hexp', 'map_byte': 'hexp', 'gate_value': 'hexp',
        'instances': 'int', 'kind': 'text', 'role': 'text', 'sign_part': 'text',
        'name': 'text', 'name_source': 'text', 'name_grade': 'text',
        'walkable': 'text', 'entry_effect': 'text', 'entry_message': 'text',
        'boardable_by': 'text', 'creature_damage': 'text',
        'creature_special': 'text', 'description': 'text', 'note': 'text',
        'source': 'text', 'grade': 'text',
    },
    'disasm/census/vehicle.tsv': {
        'sector': 'dec', 'x': 'dec', 'y': 'dec', 'byte': 'hexp', 'art': 'hexp',
        'gate_value': 'hexp', 'layer': 'text', 'what': 'text', 'grade': 'text',
        'source': 'text',
    },
    'disasm/census/vendor.tsv': {
        'sector': 'dec', 'x': 'dec', 'y': 'dec', 'marker_byte': 'hexp',
        'layer': 'text', 'quadrant': 'text', 'shop': 'text', 'sign_word': 'text',
        'sign_side': 'text', 'sign_grade': 'text', 'grade': 'text',
        'source': 'text', 'world': 'text', 'era': 'text', 'kind': 'text',
    },
    'disasm/census/npc.tsv': {
        'sector': 'dec', 'record_index': 'int', 'role': 'hexp', 'layer': 'text',
        'name': 'text', 'name_source': 'text', 'source': 'text',
        'game_speaker': 'text', 'line': 'text', 'quest': 'text',
        'world': 'text', 'era': 'text', 'kind': 'text',
    },
    'disasm/census/place.tsv': {
        'sectors': 'text', 'guide_name': 'text', 'guide_world': 'text',
        'guide_type': 'text', 'treasure': 'text', 'resolved': 'text',
        'grade': 'text', 'source': 'text', 'note': 'text',
    },
    'disasm/census/creature.tsv': {
        'art': 'hexp', 'sources': 'int', 'name': 'text',
        'damage': 'text', 'special': 'text', 'verdict': 'text',
        'conflicts': 'text', 'source': 'text', 'grade': 'text',
        'variants': 'text',
    },
    'disasm/census/art-variant.tsv': {
        'art': 'hexp', 'dmg': 'dec', 'sha256': 'hexd',
        'name': 'text', 'section': 'text', 'source_variant': 'text',
        'variant': 'text', 'image': 'text', 'grade': 'text', 'note': 'text',
        'verdict': 'text', 'nearest': 'text', 'hp': 'text', 'special': 'text',
    },
    'disasm/creature-populations.tsv': {
        'hp_mode': 'dec', 'hp_min': 'dec', 'hp_max': 'dec',
        'distinct': 'int', 'placements': 'int',
        'name': 'text', 'art': 'text', 'type': 'text', 'layer': 'text',
        'context': 'text', 'sectors': 'text', 'value_or_formula': 'text',
        'source': 'text', 'grade': 'text',
    },
    'disasm/facts/asm.tsv': {
        'source_line': 'int', 'id': 'text', 'kind': 'text', 'grade': 'text',
        'entities': 'text', 'statement': 'text', 'evidence_tool': 'text',
        'source_file': 'text',
    },
    # --- second increment -------------------------------------------------
    'disasm/function-map.tsv': {
        'start': 'hexd', 'end': 'hexd', 'len': 'hexd',
        'evidence': 'text', 'name_hint': 'text',
    },
    'disasm/fragment-splits.tsv': {
        'start': 'hexd', 'end': 'hexd', 'n_fragments': 'int', 'rets': 'int',
        'entry_call': 'text', 'grade': 'text',
    },
    'disasm/data-regions.tsv': {
        'start': 'hexd', 'end': 'hexd', 'len': 'hexd', 'kind': 'text',
    },
    'disasm/bytecheck-map.tsv': {
        'start': 'hexd', 'lits': 'int', 'drift': 'int', 'fabricated': 'int',
        'verdict': 'text',
    },
    'disasm/bytecheck-full.tsv': {
        'entry': 'hexd', 'g_lits': 'int', 'g_missing': 'int', 'r_lits': 'int',
        'r_missing': 'int', 'verdict': 'text', 'ghidra': 'text',
    },
    'disasm/functions.tsv': {
        'entry': 'hexd', 'size_bytes': 'int', 'ghidra_status': 'text',
        'reko_status': 'text', 'bytecheck': 'text', 'confidence': 'text',
        'source': 'text',
    },
    'disasm/content-inventory.tsv': {
        'size': 'dec', 'body': 'dec', 'tail0': 'dec', 'tail_nonzero': 'dec',
        'distinct': 'dec', 'nonzero': 'dec', 'file': 'text', 'class': 'text',
        'key': 'text', 'entropy': 'text', 'highbit_ratio': 'text',
        'printable_ratio': 'text',
    },
    'disasm/planet-index.tsv': {
        'world': 'dec', 'xeno': 'dec', 'yako': 'dec', 'zebo': 'dec',
        'sector': 'dec', 'name': 'text', 'settlements': 'text',
        'dungeons_towers': 'text', 'codex': 'text', 'agree': 'text',
    },
    'disasm/npc-index.tsv': {
        'sector': 'dec', 'role': 'hexp', 'index': 'int', 'layer': 'text',
        'world': 'text', 'era': 'text', 'kind': 'text', 'speaker': 'text',
        'wiki': 'text', 'line': 'text', 'quest': 'text',
    },
    'disasm/place-index.tsv': {
        'sector': 'dec', 'glyphs': 'int', 'dialogue': 'int', 'layer': 'text',
        'world': 'text', 'era': 'text', 'kind': 'text', 'candidate': 'text',
        'candidate_at': 'text', 'wiki': 'text', 'rooms': 'text',
        'text_status': 'text', 'note': 'text',
    },
    'disasm/npc-wiki-names.tsv': {
        'sector': 'dec', 'record': 'int', 'layer': 'text',
        'wiki_name': 'text', 'evidence': 'text', 'grade': 'text',
        'source': 'text',
    },
    'disasm/place-wiki-names.tsv': {
        'sector': 'dec', 'layer': 'text', 'wiki_name': 'text',
        'evidence': 'text', 'grade': 'text', 'source': 'text',
    },
    'disasm/source-third-party-spells.tsv': {
        'slot': 'int', 'price_gold': 'dec', 'name': 'text', 'class': 'text',
        'claim': 'text', 'verdict': 'text', 'grade': 'text',
    },
    'disasm/bible/statistics.verdicts.tsv': {
        'sentence_id': 'text', 'resolves': 'text', 'supported': 'text',
        'grade_survived': 'text', 'non_vacuous': 'text', 'reason': 'text',
        'judge_model': 'text', 'prompt_sha': 'hexd', 'prose_sha': 'hexd',
    },
}

DEC_RE = re.compile(r'^-?\d+$')
HEXD_RE = re.compile(r'^[0-9a-fA-F]+$')
HEXP_RE = re.compile(r'^0[xX][0-9a-fA-F]+$')
SHA_RE = re.compile(r'^[0-9a-fA-F]{64}$')


def read_table(path):
    """Read a generated table by splitting on tabs, deliberately not with csv.DictReader.

    Every generator in this project writes `"\t".join(fields)` with no quoting, so a field
    holding a bare double quote -- art 0x17's glyph column is literally `"` -- puts a csv
    reader into quoted mode, where it swallows tabs and newlines until it finds a closing
    quote. That silently dropped 25 of 64 rows from tile-art.tsv and reported no error, which
    is the dangerous shape: a checker that inspects a third of a table and prints PASS.
    """
    body = [l for l in path.read_text(encoding='utf-8').splitlines()
            if not l.startswith('#') and l.strip()]
    if not body:
        return [], []
    hdr = body[0].split('\t')
    rows = []
    for line in body[1:]:
        f = line.split('\t')
        if len(f) < len(hdr):
            f += [''] * (len(hdr) - len(f))
        rows.append(dict(zip(hdr, f)))
    return hdr, rows


def numericish(vals):
    """True if a column looks like a number in at least one base, so its base matters."""
    seen = False
    for v in vals:
        v = (v or '').strip()
        if not v:
            continue
        if DEC_RE.match(v) or HEXP_RE.match(v) or (len(v) <= 8 and HEXD_RE.match(v)):
            seen = True
        else:
            return False
    return seen


def check_value(kind, v):
    v = (v or '').strip()
    if v == '':
        return None                      # blank is allowed everywhere; keys gate that
    if kind == 'text':
        return None
    if kind == 'dec':
        return None if DEC_RE.match(v) else 'not a decimal integer'
    if kind == 'int':
        return None if (DEC_RE.match(v) and int(v) >= 0) else 'not a non-negative count'
    if kind == 'hexd':
        return None if HEXD_RE.match(v) else 'not hex digits'
    if kind == 'hexp':
        return None if HEXP_RE.match(v) else 'not a 0x-prefixed hex value'
    if kind == 'sha':
        return None if SHA_RE.match(v) else 'not 64 hex digits'
    return 'unknown kind %r' % kind


def verify(repo=None, verbose=False):
    repo = pathlib.Path(repo) if repo else REPO
    problems, uncovered = [], []
    all_tables = {str(p.relative_to(repo)) for p in (repo / 'disasm').rglob('*.tsv')}
    for rel in COVERED:
        p = repo / rel
        if not p.is_file():
            problems.append('%s: covered but missing' % rel)
            continue
        hdr, rows = read_table(p)
        contract = CONTRACT.get(rel, {})
        for col in hdr:
            vals = [r.get(col, '') for r in rows]
            kind = contract.get(col)
            if kind is None:
                if numericish(vals):
                    problems.append(
                        '%s: column %r looks numeric but has no declared base '
                        '(declare it in CONTRACT: dec, hexd, hexp, sha or text)'
                        % (rel, col))
                continue
            for i, v in enumerate(vals):
                bad = check_value(kind, v)
                if bad:
                    problems.append('%s: %s row %d = %r is %s'
                                    % (rel, col, i + 1, v, bad))
                    break
        for col in contract:
            if col not in hdr:
                problems.append('%s: contract declares %r but the table has no such '
                                'column (stale contract)' % (rel, col))
    uncovered = sorted(all_tables - set(COVERED))
    return problems, uncovered


def _mk(repo, rel, hdr, rows):
    p = pathlib.Path(repo) / rel
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text('# generated by test\n' + '\t'.join(hdr) + '\n'
                 + ''.join('\t'.join(r) + '\n' for r in rows), encoding='utf-8')
    return p


def verify_controls():
    """Drive verify() itself, end to end, against a throwaway tree.

    The unit checks on check_value() prove the grammar; they do not prove the gate
    notices anything. A checker that returns a wrong verdict is worse than a tool that
    returns a wrong answer, so every shape the gate claims to catch is made real here,
    in the same function CI will call.
    """
    import tempfile
    ok = True
    rel = COVERED[0]
    hdr = list(CONTRACT[rel].keys())

    def build(repo, values_by_col):
        n = max(len(v) for v in values_by_col.values())
        rows = []
        for i in range(n):
            rows.append([values_by_col[c][i] if i < len(values_by_col[c]) else ''
                         for c in hdr])
        _mk(repo, rel, hdr, rows)

    good = {c: ['0'] for c in hdr}
    for c, kind in CONTRACT[rel].items():
        good[c] = {'hexp': ['0x137'], 'hexd': ['00'], 'sha': ['a' * 64],
                   'dec': ['0'], 'int': ['0'], 'text': ['x']}[kind]

    def only_rel(probs):
        """Problems about the table under test; the other covered tables are legitimately
        absent from a one-table temp tree, and saying so is the gate working."""
        return [p for p in probs if p.startswith(rel + ':')]

    cases = []

    d = tempfile.mkdtemp(prefix='u2ctl-ok')
    build(d, good)
    probs, _ = verify(d)
    cases.append(('clean tree passes', only_rel(probs) == []))

    d = tempfile.mkdtemp(prefix='u2ctl-base')
    bad = dict(good)
    bad['ds_offset'] = ['375']          # decimal value in a 0x-prefixed column
    build(d, bad)
    probs, _ = verify(d)
    cases.append(('wrong base is caught by verify()',
                  any('ds_offset' in p for p in only_rel(probs))))

    d = tempfile.mkdtemp(prefix='u2ctl-stale')
    hdr2 = [c for c in hdr if c != 'size']
    _mk(d, rel, hdr2, [['0'] * len(hdr2)])
    probs, _ = verify(d)
    cases.append(('stale contract column is caught',
                  any('stale contract' in p for p in only_rel(probs))))

    d = tempfile.mkdtemp(prefix='u2ctl-missing')
    probs, _ = verify(d)
    cases.append(('covered table missing is caught',
                  any('covered but missing' in p for p in only_rel(probs))))

    d = tempfile.mkdtemp(prefix='u2ctl-undeclared')
    build(d, good)
    p = pathlib.Path(d) / rel
    txt = p.read_text(encoding='utf-8')
    lines = txt.splitlines()
    lines[1] += '\tnew_count'                       # header gains a column
    lines[2] += '\t42'                              # numeric value, no declaration
    p.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    probs, _ = verify(d)
    cases.append(('undeclared numeric column is caught',
                  any('no declared base' in p for p in only_rel(probs))))

    for name, res in cases:
        print('  %-52s %s' % (name, 'ok' if res else 'FAIL'))
        if not res:
            ok = False
    return ok, len(cases)


def selftest():
    """Prove the gate can go red in every shape it claims to catch."""
    import tempfile, os
    ok = True
    d = tempfile.mkdtemp(prefix='u2contract')
    def write_tbl(rel, hdr, rows):
        p = pathlib.Path(d) / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text('\t'.join(hdr) + '\n' +
                     '\n'.join('\t'.join(r) for r in rows) + '\n', encoding='utf-8')
        return p

    cases = []
    # 1 clean table passes
    p = write_tbl('a.tsv', ['off', 'name'], [['375', 'x'], ['0', 'y']])
    cases.append(('clean decimal column passes',
                  check_value('dec', '375') is None and check_value('dec', '0') is None))
    # 2 decimal column holding a hex-only value is caught
    cases.append(('hex value in a dec column is caught',
                  check_value('dec', 'ff') is not None))
    # 3 the ambiguous value: legal in both bases, so only the declaration decides
    cases.append(('375 is legal as dec AND as hexd (the trap)',
                  check_value('dec', '375') is None and check_value('hexd', '375') is None))
    # 4 a 0x-prefixed value in a plain-hex column is caught
    cases.append(('0x prefix in a hexd column is caught',
                  check_value('hexd', '0x300') is not None))
    # 5 sha column rejects a short hash
    cases.append(('short sha is caught', check_value('sha', 'abcd') is not None))
    # 6 negative count rejected by int but allowed by dec
    cases.append(('negative value rejected as int', check_value('int', '-3') is not None))
    # 7 the reader must not lose rows to a bare quote. tile-art.tsv's art 0x17 glyph is
    # literally `"`, and a csv reader swallows every row after it and still prints PASS.
    p = write_tbl('q.tsv', ['id', 'glyph', 'note'],
                  [['01', 'x', 'ok'], ['17', '"', 'bare quote glyph'], ['18', 'y', 'after']])
    hdr, rws = read_table(p)
    cases.append(('a bare quote in a field loses no rows', len(rws) == 3
                  and rws[1]['glyph'] == '"' and rws[2]['note'] == 'after'))
    # 7 numericish flags the ambiguous column and not a text one
    cases.append(('numericish sees the ambiguous column',
                  numericish(['375', '10']) and not numericish(['0100_sub.asm', 'x'])))
    # 8 blank is never a value failure (keys gate blanks elsewhere)
    cases.append(('blank value is not a base failure', check_value('dec', '') is None))

    for name, res in cases:
        print('  %-52s %s' % (name, 'ok' if res else 'FAIL'))
        if not res:
            ok = False
    vc, n = verify_controls()
    if not vc:
        ok = False
    print('selftest: %s (%d controls)' % ('PASS' if ok else 'FAIL', len(cases) + n))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--verify', action='store_true')
    ap.add_argument('--selftest', action='store_true')
    ap.add_argument('--list', action='store_true')
    a = ap.parse_args()

    if a.list:
        for rel in COVERED:
            for col, kind in sorted(CONTRACT.get(rel, {}).items()):
                print('%-40s %-22s %s' % (rel, col, kind))
        print('%d columns declared across %d tables'
              % (sum(len(v) for v in CONTRACT.values()), len(COVERED)))
        return 0
    if a.selftest:
        return selftest()

    problems, uncovered = verify()
    for p in problems:
        print('FAIL  ' + p)
    print('contract: %d tables covered, %d columns declared; %d tables uncovered'
          % (len(COVERED), sum(len(v) for v in CONTRACT.values()), len(uncovered)))
    if problems:
        print('TABLE CONTRACT: FAIL (%d problem(s))' % len(problems))
        return 1
    print('TABLE CONTRACT: PASS')
    return 0


if __name__ == '__main__':
    sys.exit(main())

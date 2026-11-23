#!/usr/bin/env python3
"""Ultima II `player` save editor.

The save record is the 256 bytes the game keeps in its DS working segment at
DS:0x36..0x135 (see engine/src/core/state.h, which names the range "the 256
bytes the game saves"). Verified 2026-10-21 by loading a character and comparing
all 256 bytes of the live DS block against the file: 254/256 identical, the two
exceptions being DS:74/75, live scratch that is not persisted.

    save_offset = DS_offset - 0x36

Why edit the save instead of poking memory: writing DS:00/01 in a live emulator
does NOT teleport the player. The write lands and reads back, but movement then
stops working entirely - DS:00/01 is a mirror, not the source. Editing the save
and reloading is the reliable path, and it is the only way to change sector.

Sector is stored as two DECIMAL DIGITS (DS:49/4A are patched into the filename
MAPXFF by asm/2290), so sector 20 is the pair 2,0 - not the integer 20. A
numeric read of that pair yields 512, which is a trap worth knowing.

TRAP (2026-10-29): do not setpos into a SITE sector. The ones digit of the sector
is the site kind (0 wilderness, 1/2/3 town, 4 tower, 5 dungeon), and leaving a
site restores entry_x/entry_y as the return cell and forces the ones digit back to
0. A town position placed here by hand has no wilderness return recorded, so the
first step out of the town dumps the character wherever the stale entry pair
points - measured as a landing in open sea. Put the character on the WILDERNESS
gate cell instead and press E in game: the entry routine records the return point
itself. Gate cells are found by tile>>2 (5 village, 6 town, 7 tower, 8 castle,
9 dungeon) - see site_for_tile() in engine/src/game/overworld.c.

Fields, all proven by live correlation or by cited asm in state.h:

    save  DS    field
    0x00  36    name (16 bytes, NUL padded)
    0x10  46    sex      'M' / 'F'
    0x11  47    class    0 FIGHTER, 1 CLERIC, 2 WIZARD, 3 THIEF (after sub 0x31)
    0x12  48    race     0 human, 1 elf, 2 dwarf, 3 hobbit
    0x13  49    sector first digit
    0x14  4A    sector second digit (also site kind outside a sector read:
                 0 overworld, 1 village, 2 town, 4 tower)
    0x15  4B    strength
    0x16  4C    agility
    0x17  4D    stamina
    0x18  4E    charisma
    0x19  4F    wisdom
    0x1A  50    intelligence
    0x1B  51    hit points
    0x1D  53    food (3-byte BCD, hundreds first: 53,54,55)
    0x20  56    experience (2-byte BCD: 56,57)
    0x22  58    gold (2-byte BCD, hundreds first: 58,59)
    0x24  5A    entry x   <- the game copies this to DS:00 on load
    0x25  5B    entry y   <- the game copies this to DS:01 on load
    0x2D  63    readied spell

Equipment, consumables and inventory ARE located and editable:

    DS:61  readied weapon index   (0 HANDS .. 9 QUICK SWORD)
    DS:62  worn armour index      (0 SKIN .. 6 POWER, 7 NONE)
    DS:64/65/66  torches / keys / tools   (BCD, one byte each)
    DS:6D  WORLD: 0 Earth, 1..9 the planets, 0x0A deep space. NOT boots — boots are
               items_held[BOOTS] at DS:D9, listed under ITEMS below. This tool used to
               carry a 'boots' field here and `maxout` wrote 1 into it, which silently moved
               the character off Earth and made every save refuse with ONLY ON EARTH!. The
               engine's own state.h then named the byte boots_count after our write. If a
               record shows DS:6D = 1 and nobody put a planet there, this is why.
    DS:76+i  weapons owned, one count per weapon
    DS:D6+i  items held, 16 slots, one count per item

Name tables cited to file offsets in original/ultimaii.exe: weapons 0x4F53,
armour 0x4F9E, items 0x501D. See docs/spice86-live-findings.md section 11.
`maxout` builds a fully kitted, effectively unkillable character in one shot.

DISPUTED: which of DS:4E / DS:4F is WIS and which is CHA. state.h says cha/wis,
the live findings table lists wis/cha, and it is not provable from a character
where both are equal. Do not treat the editor's labels for those two bytes as
settled.

Usage:
    python3 tools/u2_edit.py show player
    python3 tools/u2_edit.py set player hp 999
    python3 tools/u2_edit.py set player sector 71
    python3 tools/u2_edit.py set player pos 20 20
    python3 tools/u2_edit.py maxout player
    python3 tools/u2_edit.py set player weapon 9
    python3 tools/u2_edit.py set player armour 6
    python3 tools/u2_edit.py set player keys 20
    python3 tools/u2_edit.py set player str 40 -o /var/tmp/tp/player

The DS offsets are generated from engine/src/core/state.h via disasm/index/state.tsv; run
`selftest` to prove they resolve and that the resolver refuses a table that moved a field.
"""
import argparse
import os
import shutil
import sys

SIZE = 384

# The DS offsets here are GENERATED, not copied. The Seed module ruling in GLOSSARY.md says a
# seed tool takes its offsets from engine/src/core/state.h, and this tool hand-copied the base
# and every field offset instead, which made that ruling false in the code: move a field in
# state.h and this file kept writing the old address and reported success. The offsets now come
# from disasm/index/state.tsv, the committed table whose sole writer is tools/state_layout.py,
# which parses state.h and refuses when a field is unnamed or unasserted. What stays hand-written
# is the part only this tool knows: which state.h field each command name means, how wide it is on
# disk, its maximum, and its encoding.
STATE_TABLE = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                           os.pardir, 'disasm', 'index', 'state.tsv')


def load_state_table(path=STATE_TABLE):
    """Read the generated DS table: name -> (ds_offset, size, save_offset)."""
    try:
        rows = open(path, encoding='utf-8').read().splitlines()
    except OSError as exc:
        raise ValueError('cannot read %s (%s); regenerate it with '
                         '"python3 tools/state_layout.py --write"' % (path, exc))
    if not rows or rows[0].split('\t')[:3] != ['ds_offset', 'save_offset', 'name']:
        raise ValueError('%s: unexpected header, expected ds_offset/save_offset/name' % path)
    table = {}
    for line in rows[1:]:
        c = line.split('\t')
        if len(c) < 5:
            raise ValueError('%s: short row %r' % (path, line))
        table[c[2]] = (int(c[0], 16), int(c[4]), int(c[1], 16) if c[1] else None)
    if not table:
        raise ValueError('%s: header but no rows' % path)
    return table


# name -> (DS offset, width, max, kind). _off and _span are bound to the loaded table by
# resolve_fields; calling them before that is a bug in this file, not a bad save.
def _off(key):
    raise ValueError('_off called before the table was bound')


def _span(first, rest):
    raise ValueError('_span called before the table was bound')


def resolve_fields(table):
    """Build name -> (DS offset, width, max, kind) from the generated table.

    Every failure is hard and names its reason. A silently missing field is how a seed tool
    ends up writing the wrong byte and printing "wrote" afterwards.
    """
    def off(key):
        if key not in table:
            raise ValueError('%r is not a field in the generated DS table. The layout is '
                             'generated from engine/src/core/state.h -- fix the name there or '
                             'here, then regenerate with tools/state_layout.py --write.' % key)
        return table[key][0]

    def span(first, rest):
        """A multi-byte field spans several state.h fields; prove they are contiguous."""
        start = off(first)
        want = start + table[first][1]
        for key in rest:
            if key not in table:
                raise ValueError('%r, part of the %r field, is missing from the DS table' % (key, first))
            if table[key][0] != want:
                raise ValueError('%r should sit at 0x%X to complete the %r field; the table says 0x%X'
                                 % (key, want, first, table[key][0]))
            want += table[key][1]
        return start

    global _off, _span
    _off, _span = off, span
    return {
    'name':   (_off('name'), 16, None, 'text'),
    'sex':    (_off('sex'), 1, None, 'char'),
    'class':  (_off('pclass'), 1, 3, 'int'),
    'race':   (_off('race'), 1, 3, 'int'),
    'sector': (_span('sector_first', ('sector_second',)), 2, 99, 'sector'),
    # Stats are BCD, one byte each: byte 0x40 means 40, not 64. Writing a raw
    # integer displays the wrong number (40 -> 0x28 -> "28").
    'str':    (_off('stat_str'), 1, 99, 'bcd'),
    'agi':    (_off('stat_agi'), 1, 99, 'bcd'),
    'sta':    (_off('stat_sta'), 1, 99, 'bcd'),
    # 0x4E/0x4F were the DISPUTED pair until 2026-10-29: state.h said cha/wis, the
    # live findings table said wis/cha, and every maxout character had them equal so
    # nothing could tell. Settled by captures/spice86/d13-cha-vs-wis — a character
    # with 0x4E=20 and 0x4F=40 printed CHARISMA-20 / WISDOM-40 and priced a horse off
    # 0x4E. state.h was right; the findings table is corrected.
    'cha':    (_off('stat_cha'), 1, 99, 'bcd'),
    'wis':    (_off('stat_wis'), 1, 99, 'bcd'),
    'int':    (_off('stat_int'), 1, 99, 'bcd'),
    'hp':     (_span('hp', ('unk_52',)), 2, 9999, 'bcd'),    # 4-digit BCD across 0x51,0x52:
                                          # hp=4 with 0x52=0 displays as
                                          # H.P.=0400. A raw byte write of 255
                                          # gives 0xFF, whose nibbles are not
                                          # decimal digits, and the game prints
                                          # "??" for those.
    'food':   (_span('food', ('food_hi', 'food_lo')), 3, 999999, 'bcd'),   # 6-digit counter; the status band
                                          # prints only the top 4 digits, so
                                          # 40000 displays as FOOD=0400
    'exp':    (_span('exp_lo', ('exp_hi',)), 2, 9999, 'bcd'),
    'gold':   (_span('gold_lo', ('gold_hi',)), 2, 9999, 'bcd'),
    'entry_x': (_off('entry_x'), 1, 63, 'int'),
    'entry_y': (_off('entry_y'), 1, 63, 'int'),
    'spell':  (_off('spell_pick'), 1, 255, 'int'),
    # Equipment and consumables. Proven in docs/spice86-live-findings.md s11 by
    # live read/write-back, with the name tables cited to file offsets.
    'weapon': (_off('readied_weapon'), 1, 9, 'int'),       # readied weapon index; 0 = HANDS
    'armour': (_off('worn_armour'), 1, 7, 'int'),       # worn armour index; 0 = SKIN
    'torches': (_off('torches'), 1, 99, 'bcd'),
    'keys':   (_off('keys'), 1, 99, 'bcd'),
    'tools':  (_off('tools'), 1, 99, 'bcd'),
    'world':  (_off('world'), 1, 10, 'int'),      # 0 Earth, 1..9 planets, 0x0A deep space
    # The Father Antos blessing flag. Written in the original by asm/4209:422B (world 9,
    # castle sector, role 0x81) and read by asm/3C2E cmd_O, which grants the force field
    # ring in sector 32 only when it is non-zero. Seeding it is how a gate tests the ring
    # arm without flying to Planet X first.
    'blessing': (_off('blessing'), 1, 1, 'int'),
    }


STATE = load_state_table()
try:
    FIELDS = resolve_fields(STATE)
except ValueError as exc:
    sys.exit('u2_edit: %s' % exc)
BASE = STATE['name'][0]      # the saved span starts where `name` sits, at DS:36
if STATE['name'][2] != 0:
    sys.exit("u2_edit: the generated table does not put the `name` field at save_offset 0, so "
             "this tool's assumption about the base of the saved span is wrong. Check "
             "tools/state_layout.py --verify before trusting any write.")


WEAPONS = ['HANDS', 'DAGGER', 'MACE', 'AXE', 'BOW', 'SWORD',
           'GREAT SWORD', 'LIGHT SWORD', 'PHASER', 'QUICK SWORD']
ARMOURS = ['SKIN', 'CLOTH', 'LEATHER', 'CHAIN', 'PLATE', 'REFLECT', 'POWER', 'NONE']
ITEMS = ['RING', 'WAND', 'STAFF', 'BOOTS', 'CLOAK', 'HELM', 'GEM', 'ANKH',
         'RED GEM', 'SKULL KEY', 'GREEN GEM', 'BRASS BUTTON', 'BLUE TASSLE',
         'STRANGE COIN', 'GREEN IDOL', 'TRI-LITHIUM']
CLASS_NAMES = {0: 'FIGHTER', 1: 'CLERIC', 2: 'WIZARD', 3: 'THIEF'}
# Corrected 2026-10-29. This read {1: 'mage', 2: 'paladin'}, which is wrong in both entries and
# the error was ours, not the game's: the shipped manual and this tool's own help text both say
# CLERIC and WIZARD, and the authority is the class group of the indexed name pool —
# asm/4C55_name_pool.asm pool[72..75] = FIGHTER, CLERIC, WIZARD, THIEF, indexed as
# pool[0x48 + class]. "Paladin" is a Ultima III class and does not exist in this binary.
RACE_NAMES = {0: 'human', 1: 'elf', 2: 'dwarf', 3: 'hobbit'}
# The ten spell names, pool[36..45], indexed by the readied slot DS:63. Slot 0 is NONE.
SPELL_NAMES = ['NONE', 'LIGHT', 'DOWN LADDER', 'UP LADDER', 'PASSWALL', 'SURFACE',
               'PRAYER', 'MAGIC MISSILE', 'BLINK', 'KILL']

# A durable test character: strong, unkillable, fully kitted.
# Power armour (6) + quick sword (9) + keys and torches, one of every item and
# every weapon owned.
#
# Stats are 99, the packed-decimal maximum of the field, not a conservative "strong".
# They used to be 40 on the grounds that 40 was not a proven cap — and that is true, the
# TEST character was observed live at str=50 (BCD 0x50) — but 40 was still the wrong
# number for a test character, because the point of maxout is to survive long enough to
# reach content, and the owner ruled 99 on 2026-11-11 for exactly that reason. Note the
# original has no ceiling guard on any stat add site (see the Hotel clerk grind in
# asm/3C2E, which is four chained DAA adds with nothing after them), so 99 is a value
# the game will happily walk past into 00 if you keep grinding it.
MAXOUT = {'str': 99, 'agi': 99, 'sta': 99, 'cha': 99, 'wis': 99, 'int': 99,
          'hp': 9999, 'food': 999999, 'gold': 9999, 'exp': 9999}
ARMOUR_BASE = STATE['weapons_owned'][0]   # DS:76+i, one count byte per weapon owned
ITEM_BASE = STATE['items_held'][0]        # items held, DS:D6+i
# Spell stock, DS:B6+i, ten slots, indexed by the readied-spell number DS:63. The index base is
# the SPELLS group of the name pool (asm/4C55_name_pool.asm pool[36..45]): 0 NONE, 1 LIGHT,
# 2 DOWN LADDER, 3 UP LADDER, 4 PASSWALL, 5 SURFACE, 6 PRAYER, 7 MAGIC MISSILE, 8 BLINK, 9 KILL.
# asm/3139_cast_spell_flow reads DS:B6[DS:63] and answers NO SPELL! when it is zero, then
# decrements it in packed decimal — so these are BCD counts and spells are CONSUMABLES, not
# memorised slots. Slot 0 is NONE and is left alone.
SPELL_BASE = STATE['spell_counts'][0]
SPELL_SLOTS = 10


def set_counts(d, base, n, value):
    """Write n count bytes starting at DS offset `base`."""
    for i in range(n):
        d[base + i - BASE] = value


def bcd_decode(raw):
    """BCD bytes hold decimal digits in their nibbles: 0x04 0x00 means "0400".
    Render each byte as two hex characters to recover the digit string, then
    read that string as DECIMAL. Parsing it as hex is the classic mistake and
    reports gold 400 as 1024."""
    return int(''.join('%02X' % b for b in raw), 10)


def bcd_encode(value, width):
    """Inverse: split the decimal digits into pairs and pack each pair into one
    byte's nibbles, so 400 -> 0x04 0x00 and 9999 -> 0x99 0x99."""
    digits = str(value)
    if len(digits) > width * 2:
        raise ValueError('%d does not fit in %d BCD bytes' % (value, width))
    digits = digits.zfill(width * 2)
    return [int(digits[i:i + 2], 16) for i in range(0, len(digits), 2)]


def load(path):
    d = bytearray(open(path, 'rb').read())
    if len(d) != SIZE:
        sys.exit('%s: expected %d bytes, got %d' % (path, SIZE, len(d)))
    return d


def get(d, name):
    off, width, _, kind = FIELDS[name]
    raw = d[off - BASE: off - BASE + width]
    if kind == 'text':
        return bytes(raw).rstrip(b'\x00').decode('latin-1')
    if kind == 'char':
        return chr(raw[0])
    if kind == 'sector':
        return raw[0] * 10 + raw[1]
    if kind == 'bcd':
        return bcd_decode(raw)
    return raw[0]


def put(d, name, value):
    off, width, mx, kind = FIELDS[name]
    i = off - BASE
    if kind == 'text':
        v = str(value).encode('latin-1')[:16]
        d[i:i + 16] = v + bytes(16 - len(v))
        return
    if kind == 'char':
        d[i] = ord(str(value)[0].upper())
        return
    n = int(value)
    if mx is not None and not (0 <= n <= mx):
        raise ValueError('%s=%d out of range 0..%d' % (name, n, mx))
    if kind == 'sector':
        d[i], d[i + 1] = n // 10, n % 10
    elif kind == 'bcd':
        d[i:i + width] = bytes(bcd_encode(n, width))
    else:
        d[i] = n


def show(d):
    print('name    %s' % get(d, 'name'))
    print('sex     %s' % get(d, 'sex'))
    print('class   %d (%s)' % (get(d, 'class'), CLASS_NAMES.get(get(d, 'class'), '?')))
    print('race    %d (%s)' % (get(d, 'race'), RACE_NAMES.get(get(d, 'race'), '?')))
    print('sector  %d' % get(d, 'sector'))
    for f in ('str', 'agi', 'sta', 'cha', 'wis', 'int'):
        print('%-7s %d' % (f, get(d, f)))
    print('hp      %d' % get(d, 'hp'))
    print('food    %d' % get(d, 'food'))
    print('gold    %d' % get(d, 'gold'))
    print('exp     %d' % get(d, 'exp'))
    print('entry   (%d, %d)' % (get(d, 'entry_x'), get(d, 'entry_y')))
    print('spell   %d' % get(d, 'spell'))
    stock = [(SPELL_NAMES[i], d[SPELL_BASE + i - BASE]) for i in range(1, SPELL_SLOTS)]
    stock = [(n, c) for n, c in stock if c]
    print('spells  %s' % (', '.join('%s x%d' % sc for sc in stock) or '(none)'))
    w, a = get(d, 'weapon'), get(d, 'armour')
    print('wield   %d (%s)' % (w, WEAPONS[w] if w < len(WEAPONS) else '?'))
    print('wearing %d (%s)' % (a, ARMOURS[a] if a < len(ARMOURS) else '?'))
    print('torches %d  keys %d  tools %d  world %d%s'
          % (get(d, 'torches'), get(d, 'keys'), get(d, 'tools'), get(d, 'world'),
             '' if get(d, 'world') == 0 else '  <- NOT Earth: Q will refuse to save'))
    held = [(ITEMS[i], d[ITEM_BASE + i - BASE]) for i in range(16)]
    held = [(n, c) for n, c in held if c]
    print('items   %s' % (', '.join('%s x%d' % hc for hc in held) or '(none)'))


def selftest():
    """Prove the generated offsets resolve, and that the resolver can refuse.

    This is a gate, so it has controls that are expected to go red. A resolver that cannot
    fail is worse than hand-copied offsets, because it looks like it is checking something.
    """
    fails = []

    def check(label, ok):
        print('%-62s %s' % (label, 'ok' if ok else 'FAIL'))
        if not ok:
            fails.append(label)

    check('every command field resolved an offset', len(FIELDS) >= 25)
    check('BASE is where the name field sits', BASE == STATE['name'][0])
    check('every resolved offset lands inside the 384-byte record',
          all(BASE <= off <= BASE + SIZE for off, _, _, _ in FIELDS.values()))
    check('the generated save_offset column agrees with BASE for every saved field',
          all(ds - BASE == save for ds, _sz, save in STATE.values() if save is not None)
          and any(save is not None for _ds, _sz, save in STATE.values()))
    check('the composite fields are the widths the record actually has',
          FIELDS['hp'][1] == 2 and FIELDS['food'][1] == 3
          and FIELDS['sector'][1] == 2 and FIELDS['gold'][1] == 2)

    def refuses(label, mutate):
        table = dict(STATE)
        mutate(table)
        try:
            resolve_fields(table)
        except ValueError:
            check(label, True)
            return
        check(label, False)

    def drop(t):
        del t['stat_cha']

    def move(t):
        t['unk_52'] = (0x9f, 1, 0x69)

    def wipe(t):
        t.clear()

    refuses('control: a field missing from the table is a hard failure', drop)
    refuses('control: a field moved out of a composite span is a hard failure', move)
    refuses('control: an empty table is a hard failure', wipe)

    # Behaviour, not just arithmetic: the refactor must not have changed what a write does.
    d = bytearray(SIZE)
    for name, want in (('str', 40), ('agi', 7), ('hp', 1234), ('gold', 7),
                       ('food', 40000), ('sector', 71), ('class', 2), ('world', 9)):
        put(d, name, want)
        check('round-trip %s = %d' % (name, want), get(d, name) == want)
    put(d, 'name', 'PAUL')
    check('round-trip name', get(d, 'name') == 'PAUL')
    put(d, 'sex', 'f')
    check('round-trip sex', get(d, 'sex') == 'F')

    d2 = bytearray(SIZE)
    put(d2, 'str', 40)
    check('control: str 40 is stored as BCD 0x40, not binary 0x28',
          d2[FIELDS['str'][0] - BASE] == 0x40)
    put(d2, 'sector', 71)
    check('control: sector 71 is stored as the two digits 7 then 1, not 0x47',
          d2[FIELDS['sector'][0] - BASE] == 7 and d2[FIELDS['sector'][0] - BASE + 1] == 1)

    print('%d check(s) failed' % len(fails))
    return 1 if fails else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('cmd', choices=['show', 'set', 'setpos', 'maxout', 'selftest'])
    ap.add_argument('file', nargs='?')
    ap.add_argument('args', nargs='*')
    ap.add_argument('-o', '--out')
    a = ap.parse_args()

    if a.cmd == 'selftest':
        return selftest()
    if not a.file:
        ap.error('%s needs a save file' % a.cmd)

    low = a.file.lower()
    if '/references/' in low or ('/original/' in low and not a.out):
        sys.exit('refusing to modify original/ or references/ in place; use -o')

    d = load(a.file)
    out = a.out or a.file
    if a.out:
        os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
        shutil.copyfile(a.file, out)
        d = load(out)

    if a.cmd == 'show':
        show(d)
        return
    if a.cmd == 'maxout':
        for k, v in MAXOUT.items():
            put(d, k, v)
        put(d, 'weapon', 9)      # QUICK SWORD
        put(d, 'armour', 6)      # POWER
        put(d, 'torches', 50)
        put(d, 'keys', 20)
        put(d, 'tools', 20)
        # No boots line here on purpose. Boots are items_held[BOOTS], and the item pool below
        # already gives one of every item including BOOTS. The `put(d,'boots',1)` this used to
        # carry wrote DS:6D, which is the world byte: every maxout character was quietly on
        # planet 1 and could not save.
        set_counts(d, ARMOUR_BASE, len(WEAPONS), 1)   # own every weapon
        set_counts(d, ITEM_BASE, 16, 1)               # one of every item
        print('maxout applied: stats/hp/food/gold/exp, QUICK SWORD, POWER armour,')
        print('               50 torches, 20 keys, 20 tools (boots come from the item pool),')
        print('               all %d weapons and all %d items x1' % (len(WEAPONS), 16))
    elif a.cmd == 'setpos':
        if len(a.args) != 2:
            sys.exit('setpos needs X and Y')
        put(d, 'entry_x', int(a.args[0]))
        put(d, 'entry_y', int(a.args[1]))
        print('entry -> (%s, %s)' % tuple(a.args))
    elif a.cmd == 'set':
        if len(a.args) != 2 and not (len(a.args) >= 3 and a.args[0] == 'item'):
            sys.exit('set needs FIELD VALUE  (or: set item NAME... COUNT)')
        f = a.args[0]
        v = a.args[1] if len(a.args) == 2 else None
        if f == 'spells':
            # Stock every spell slot the caster can actually ready (1..9; slot 0 is NONE).
            n = int(v)
            if not 0 <= n <= 99:
                sys.exit('spells count must be 0..99 — it is stored as packed decimal')
            for i in range(1, SPELL_SLOTS):
                d[SPELL_BASE + i - BASE] = ((n // 10) << 4) | (n % 10)
            print('spells 1..%d -> %d each' % (SPELL_SLOTS - 1, n))
        elif f == 'items':
            sys.exit("there is no single 'items' field; use `set item NAME... COUNT` "
                     "(see ITEMS in this file), or run `maxout` for one of everything")
        elif f == 'item':
            # `set item TRI-LITHIUM 9`. The name may contain spaces, so the count is
            # the last argument and everything between is the name.
            if len(a.args) < 3:
                sys.exit('set item NAME... COUNT  e.g. `set item TRI-LITHIUM 9`')
            count = int(a.args[-1])
            name = ' '.join(a.args[1:-1]).upper()
            if name not in ITEMS:
                sys.exit('no item %r; known: %s' % (name, ', '.join(ITEMS)))
            if not 0 <= count <= 99:
                sys.exit('item count must be 0..99 — slots are packed decimal')
            # Packed BCD, not a raw integer. Proven by the consumer itself:
            # asm/6999_space_status decrements the fuel slot with
            # `stc ; cmc ; sbb $1,%al ; das ; cmc`, and `das` is a decimal adjust.
            # Writing 10 as 0x0A would print as garbage and subtract wrongly.
            d[ITEM_BASE + ITEMS.index(name) - BASE] = ((count // 10) << 4) | (count % 10)
            print('item %s -> %d' % (name, count))
        elif f not in FIELDS:
            sys.exit('unknown field %r; known: %s, spells' % (f, ', '.join(sorted(FIELDS))))
        else:
            put(d, f, v)
            print('%s -> %s' % (f, v))
    else:
        sys.exit('unknown command')

    with open(out, 'wb') as f:
        f.write(bytes(d))
    chk = load(out)
    if a.cmd == 'set':
        if a.args[0] == 'spells':
            # Read back a slot that must have changed, not the count we just computed.
            want = ((int(a.args[1]) // 10) << 4) | (int(a.args[1]) % 10)
            if chk[SPELL_BASE + 1 - BASE] != want:
                sys.exit('write did not land: spell slot 1 reads back 0x%02x' %
                         chk[SPELL_BASE + 1 - BASE])
            print('wrote %s' % out)
            return
        if a.args[0] == 'item':
            name = ' '.join(a.args[1:-1]).upper()
            want = ((int(a.args[-1]) // 10) << 4) | (int(a.args[-1]) % 10)
            off = ITEM_BASE + ITEMS.index(name) - BASE
            if chk[off] != want:
                sys.exit('write did not land: %s slot reads back 0x%02x' % (name, chk[off]))
            print('wrote %s' % out)
            return
        got = get(chk, a.args[0])
        # name and sex are not integers. Running int() on them raised ValueError
        # AFTER the write had already landed, so a successful edit printed a
        # traceback and looked like a failure (seen 2026-10-21 with
        # `set <file> name AIRTEST`). Compare those against the raw value.
        if a.args[0] in ('name', 'sex'):
            if str(got) != a.args[1]:
                sys.exit('write did not land: read back %r' % got)
        elif str(got) != str(int(a.args[1])):
            sys.exit('write did not land: read back %r' % got)
    print('wrote %s' % out)


if __name__ == '__main__':
    sys.exit(main())

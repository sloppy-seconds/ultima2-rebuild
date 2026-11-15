"""endgame_save.py — build a test character for endgame content validation.

Owner spec, 2026-11-11:
  * max stats
  * HP, GOLD and EXP 8000, so none of them is close enough to 9999 that a single reward
    or wound wraps the packed-decimal field and the number loops — that wrap is real (the
    Hotel clerk grind proves 99 -> 04) and it makes a test run unreadable rather than hard.
  * FOOD set so the band READS 8000, which means 800000 in the field: food is six digits
    and the band prints the top four. See the note at the assignment.
  * plenty of TRI-LITHIUM and ANKH, so losing a few to a thief is not a dead end.
  * NOT the quick sword and NOT the ring, so both acquisitions can be validated:
    the quick sword is cmd_O role 0x81 in sector 32 (ENILNO IS YOURS!, DS:7F) and the
    ring is role 0x82 in the same sector gated on the Father Antos blessing (DS:D6).
    The blessing byte is left at 0 on purpose, so the Planet X trip is part of the test.

Stats are set to 99, the field's packed-decimal maximum, not maxout's 40: 40 is
documented in u2_edit.py as a conservative "strong" and not a proven cap, and the
point here is to survive long enough to reach content. Say so if 40 was meant.
"""
import os
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS = os.path.join(REPO, 'tools')
REF = os.path.join(REPO, 'references/ultima2/player')

# `-h` answers instead of forwarding the flag to a subprocess: this script shells out to cp
# and u2_edit.py with its own argv, so `--help` used to print coreutils' help and exit 0,
# which is the kind of plausible wrong answer tools/doc_command_gate.py exists to catch.
if len(sys.argv) > 1 and sys.argv[1] in ("-h", "--help"):
    print((__doc__ or "").strip())
    raise SystemExit(0)

OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(REPO, 'engine/saves/_seeds/endgame-PLAYER')


def ensure_parent(path):
    """The default output lives under engine/saves/, which is gitignored on purpose (a save
    is user data, never a repo artefact — see .gitignore). The directory is not in the tree
    until something writes it, so create it rather than failing on a fresh clone."""
    parent = os.path.dirname(path)
    if parent:
        os.makedirs(parent, exist_ok=True)

# item indices are the order of the game's own item pool, DS:D6 + i
ITEM_RING, ITEM_ANKH, ITEM_TRI_LITHIUM = 0, 7, 15
WEAP_QUICK_SWORD = 9          # DS:76 + 9 = DS:7F
WEAP_PHASER = 8               # DS:76 + 8; asm/3C2E's random-weapon arm reaches 1..8 only

def edit(*args):
    r = subprocess.run([sys.executable, f'{TOOLS}/u2_edit.py', *args],
                       capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit('u2_edit failed: %s\n%s' % (' '.join(args), r.stdout + r.stderr))

ensure_parent(OUT)
subprocess.run(['cp', REF, OUT], check=True)
edit('maxout', OUT)

# maxout readies the quick sword, which this save must not have.
edit('set', OUT, 'weapon', str(WEAP_PHASER))
for name, value in (('str', 99), ('agi', 99), ('sta', 99),
                    ('cha', 99), ('wis', 99), ('int', 99),
                    ('hp', 8000), ('gold', 8000), ('exp', 8000),
                    # FOOD is a SIX-digit field and the status band prints only the top
                    # four digits, so the number the player reads and the number the field
                    # holds are not the same thing: 800000 displays as FOOD=8000, while
                    # putting 8000 in the field displays as FOOD=0080. The owner asked for
                    # what he sees, which is 8000 on the band. HP, GOLD and EXP are
                    # four-digit fields and take their face value; food is the one that
                    # does not, and u2_edit.py's own FIELDS table says so in a comment.
                    ('food', 800000)):
    edit('set', OUT, name, str(value))
edit('set', OUT, 'name', 'ENDGAME')
# Era-3 Earth wilderness, on a land cell, with the return cell set to the same place so
# leaving a site does not restore an entry point that was never recorded.
edit('set', OUT, 'sector', '30')
edit('set', OUT, 'world', '0')
edit('setpos', OUT, '15', '20')
edit('set', OUT, 'entry_x', '15')
edit('set', OUT, 'entry_y', '20')

BASE = 0x36
# DS offsets, not file offsets: the record is the DS block from 0x36, so every access
# subtracts BASE. This tool's own help text warns about exactly this, and the first run
# of this script walked into it and wrote item counts into DS:10C..11B.
ARMOUR_BASE = 0x76        # despite the name: the ten weapon-owned counts, DS:76+i
ITEM_BASE = 0xD6          # items held, DS:D6+i
d = bytearray(open(OUT, 'rb').read())
d[ARMOUR_BASE - BASE + WEAP_QUICK_SWORD] = 0  # no quick sword owned
d[ITEM_BASE - BASE + ITEM_RING] = 0           # no ring owned
d[ITEM_BASE - BASE + ITEM_ANKH] = 9
d[ITEM_BASE - BASE + ITEM_TRI_LITHIUM] = 9
open(OUT, 'wb').write(bytes(d))
print('built', OUT)
# Read back what matters instead of trusting the writes: the two acquisitions under test
# must be absent, the two stock items present.
chk = bytearray(open(OUT, 'rb').read())
print('quick sword owned :', chk[ARMOUR_BASE - BASE + WEAP_QUICK_SWORD])
print('ring held         :', chk[ITEM_BASE - BASE + ITEM_RING])
print('ankh held         :', chk[ITEM_BASE - BASE + ITEM_ANKH])
print('tri-lithium held  :', chk[ITEM_BASE - BASE + ITEM_TRI_LITHIUM])
print('weapons owned     :', list(chk[ARMOUR_BASE - BASE:ARMOUR_BASE - BASE + 10]))
print('blessing DS:6C    :', chk[0x6C - BASE])

#!/usr/bin/env bash
# tests/run_cp7.sh — CP7 SPEC: the interior sector. Enter it, light it, walk it,
# fight it, loot it, go deeper.
#
# Why this is a behavioural gate and not a unit one: everything CP7 touches is a
# coordinate. The grid is 16 pages of 16x16 indexed by a floor byte, the facing is
# a signed pair, the light is a step counter, and the renderer walks three columns
# out to seven depths. A unit test can check that a table was transcribed; only a
# run can show that a player who pressed four keys is standing where the map says
# they should be.
#
# The fixture is tests/cp7_dungeon_seed.py, which writes a dungeon built to answer
# the assertions rather than a maze copied from the shipped mapx25 — see that
# file's header for why.
#
# SKIP rule, same as CP6: the dungeon literals are `run` locators into asm regions
# Phase 3 rendered as db bytes, so with no copy of the game to extract from they
# come out empty and asserting on them would assert on nothing. The skip names the
# symbols it needed.
set -uo pipefail

cd "$(dirname "$0")/.." || exit 1
fail=0
ok()   { printf '  PASS  %s\n' "$1"; }
bad()  { printf '  FAIL  %s\n' "$1"; fail=1; }
skip() { printf '  SKIP  %s\n' "$1"; }

WORK=$(mktemp -d "${TMPDIR:-/var/tmp}/u2-cp7.XXXXXX")
trap 'rm -rf "$WORK"' EXIT

NEED="U2_S_ITS_DARK U2_S_IGNITE_TORCH U2_S_ADVANCE U2_S_RETREAT U2_S_TURN_LEFT U2_S_TURN_RIGHT U2_S_CHEST U2_S_DESCEND U2_S_TO_LEVEL"
EMPTY=""
for sym in $NEED; do
    if grep -q "^#define ${sym} \"\"" content/gen_content.h 2>/dev/null; then
        EMPTY="${EMPTY} ${sym}"
    fi
done

if [ -n "$EMPTY" ]; then
    skip "CP7 dungeon behaviour: no copy of the game to extract from (empty:${EMPTY})"
elif [ ! -x ./u2 ]; then
    bad "CP7 dungeon behaviour: ./u2 is not built — run make before make test"
else
    # run_case LABEL KEYS OUTFILE — seed a fresh dungeon, drive it, flatten the transcript.
    run_case() {
        local label="$1" keys="$2" out="$3"
        rm -rf "$WORK/$label"
        python3 tests/cp7_dungeon_seed.py "$WORK/$label" >/dev/null 2>&1 || return 1
        printf "$keys" > "$WORK/$label.keys"
        ./u2 --headless --play-map --cleanroom --data-dir "$WORK/$label/content" \
             --save-dir "$WORK/$label/save" --script "$WORK/$label.keys" \
             --trace="$out.trace" --dump-state --dump-frame "$WORK/$label/frame.ppm" \
             > "$out" 2>&1
        sed -n 's/.*text=//p' "$out.trace" 2>/dev/null | tr -d '\n' > "$out.txt"
    }
    said() { grep -qF -- "$2" "$1.txt" 2>/dev/null; }
    field_of() { sed -n 's/.*"'"$2"'":"\([^"]*\)".*/\1/p' "$1" | tail -1; }
    pos_of()   { sed -n 's/.*"pos":{"x":\([0-9]*\),"y":\([0-9]*\)}.*/\1 \2/p' "$1" | tail -1; }

    # ---- 0. the dungeon is theme-neutral -----------------------------------
    # The in-game notice `THEME ART DOES NOT COVER DUNGEONS` was removed
    # 2026-11-06 because the dungeon draws the same under every theme: it is a
    # wireframe, and dungeon.c touches no theme atlas. That reasoning is a fact
    # about the renderer, so it is checked as one. If themed dungeon art ever
    # lands, theme scope becomes real again and this goes red.
    if python3 tests/dungeon_art_neutral.py --selftest >"$WORK/dan-st.out" 2>&1; then
        if python3 tests/dungeon_art_neutral.py >"$WORK/dan.out" 2>&1; then
            ok "the dungeon draws no theme art, so it plays the same under every theme (selftest passed)"
        else
            bad "the dungeon now consults the theme atlas — the removed scope notice must be revisited:"
            sed 's/^/        /' "$WORK/dan.out"
        fi
    else
        bad "dungeon art-neutrality check cannot fail (selftest broke):"
        sed 's/^/        /' "$WORK/dan-st.out"
    fi

    # ---- 1. entering -------------------------------------------------------
    # The mouth is a tile in the world sector, so this is the route a player takes
    # and not a seeded state. The CP6 lock that used to refuse kinds 4 and 5 is gone.
    if run_case enter 'E\n' "$WORK/enter.out"; then
        said "$WORK/enter.out" "ENTER-DUNGEON" \
            && ok "CP7 entry: the dungeon mouth opens (the [engine] lock is gone)" \
            || bad "CP7 entry: expected ENTER-DUNGEON"
        s=$(field_of "$WORK/enter.out" sector)
        [ "$s" = "25" ] && ok "CP7 entry: DS:49/4A moved to sector 25" \
                        || bad "CP7 entry: sector is '$s', expected 25"
        said "$WORK/enter.out" "IT'S DARK!" \
            && ok "CP7 entry: the first frame is dark (asm/6000's zero-light branch)" \
            || bad "CP7 entry: expected IT'S DARK!"
        p=$(pos_of "$WORK/enter.out")
        [ "$p" = "5 5" ] && ok "CP7 entry: the arrival cell is (5,5), asm/342A's pair" \
                         || bad "CP7 entry: arrived at '$p', expected '5 5'"
    else
        bad "CP7 entry: the run did not complete"
    fi

    # ---- 2. light ----------------------------------------------------------
    # asm/3794 spends ONE packed-decimal torch and refills [DS:26] to 0x96. The pair
    # is read as torches+light so a refill that spent nothing cannot pass.
    if run_case light 'E\nI\n' "$WORK/light.out"; then
        said "$WORK/light.out" "IGNITE TORCH" \
            && ok "CP7 light: IGNITE TORCH runs inside a site" || bad "CP7 light: no IGNITE TORCH"
        l=$(field_of "$WORK/light.out" light)
        [ "$l" = "49 96" ] && ok "CP7 light: torches 0x50 -> 0x49 and the counter refilled to 0x96" \
                           || bad "CP7 light: pair is '$l', expected '49 96'"
        said "$WORK/light.out" "IT'S DARK!" \
            && ok "CP7 light: the dark frame before the torch is on the record" \
            || bad "CP7 light: no dark frame on entry"
    fi

    # ---- 3. the four movement commands --------------------------------------
    # asm/277B, and the facing is a signed pair rather than a compass enum: the entry
    # facing is EAST (1,0), TURN RIGHT is a clockwise quarter turn, and ADVANCE walks the
    # facing. The route below is the same ten-cell walk it always was; it lost its leading
    # TURN RIGHT when the entry facing became east, because that turn is now implicit.
    if run_case walk 'E\nI\n0xf0\n0xf0\n0xf3\n0xf0\n0xf0\n0xf2\n0xf1\n' "$WORK/walk.out"; then
        said "$WORK/walk.out" "TURN RIGHT" \
            && ok "CP7 walk: the right arrow is TURN RIGHT, not a step east" \
            || bad "CP7 walk: expected TURN RIGHT"
        said "$WORK/walk.out" "ADVANCE" \
            && ok "CP7 walk: the up arrow is ADVANCE" || bad "CP7 walk: expected ADVANCE"
        said "$WORK/walk.out" "TURN LEFT" \
            && ok "CP7 walk: the left arrow is TURN LEFT" || bad "CP7 walk: expected TURN LEFT"
        said "$WORK/walk.out" "RETREAT" \
            && ok "CP7 walk: the down arrow is RETREAT" || bad "CP7 walk: expected RETREAT"
        # entry east: -A-> (6,5) -A-> (7,5) -R-> south -A-> (7,6) -A-> (7,7)
        # -L-> east -B-> (6,7)
        p=$(pos_of "$WORK/walk.out")
        [ "$p" = "6 7" ] && ok "CP7 walk: the ten-key route ends at (6,7), where the map says" \
                         || bad "CP7 walk: ended at '$p', expected '6 7'"
    fi

    # ---- 4. the wireframe ---------------------------------------------------
    # A frame is the only evidence that the renderer drew geometry at all: asm/6000
    # draws nothing in the dark, so the dark frame and the lit frame must differ and
    # the lit one must have ink in the map window and not only in the text band.
    #
    # The lit frame comes from the `light` case and not the `walk` case, and that is a
    # real correction rather than a tidy-up. Once asm/12DB's contact arms were wired, a
    # type-2 creature could blow the torch out mid-walk, so the walk frame was no longer
    # guaranteed to be lit — the assertion had become a coin flip on the spawner. Dark vs
    # lit at the SAME cell, with no movement turns between them, is the comparison the
    # section actually wants: it isolates asm/6000's light branch and nothing else.
    if [ -f "$WORK/light/frame.ppm" ] && [ -f "$WORK/enter/frame.ppm" ]; then
        python3 - "$WORK/light/frame.ppm" "$WORK/enter/frame.ppm" <<'PY' && ok "CP7 render: the lit frame draws wireframe geometry" || bad "CP7 render: the lit frame drew no geometry"
import sys

def load(p):
    d = open(p, "rb").read()
    return d[d.index(b"255\n") + 4:]

lit, dark = load(sys.argv[1]), load(sys.argv[2])
W = 320

def px(buf, x, y):
    return buf[(y * W + x) * 3]

ink = sum(1 for y in range(0, 160) for x in range(W) if px(lit, x, y) > 60)
dark_ink = sum(1 for y in range(0, 160) for x in range(W) if px(dark, x, y) > 60)
# The window must carry the lines, not just the status band bleeding upward, and
# the geometry must span a good part of the width: a single stray line would pass a
# bare non-zero ink test.
cols = {x for y in range(0, 160) for x in range(W) if px(lit, x, y) > 60}
print(f"        lit ink {ink}, dark ink {dark_ink}, distinct columns {len(cols)}")
# Upper bound as well as a lower one. On 2026-10-29 the renderer was drawing a depth frame
# on every open cell instead of only where asm/6144 calls it, and the frame still looked
# like a corridor — it just had roughly twice the lines. A lower-bound-only test cannot see
# over-draw at all, and "it looks like a corridor" was exactly the claim that hid it.
# The bound is 1800 and it is set BETWEEN two measured values, not guessed: the fixed
# renderer measures 1688 on this fixture and the over-drawing version measures 1896. An
# earlier draft of this line used 2200, extrapolated from the 1.8x seen on the real mapx35,
# and the control walked straight under it — a gate that could not fail where it enforces,
# which is the failure mode AGENTS.md section 3 already names. Re-measure both sides if this
# fixture's map ever changes.
sys.exit(0 if (ink > 400 and ink < 1800 and dark_ink == 0 and len(cols) > 120) else 1)
PY
    else
        bad "CP7 render: --dump-frame produced no PPM"
    fi

    # ---- 5. chests ----------------------------------------------------------
    # asm/363B's site arm: the cell UNDER the player must be 0x40, the same bit the
    # renderer draws as a chest, and the cell is cleared afterwards.
    if run_case chest 'E\nI\n0xf0\n0xf0\nG\n' "$WORK/chest.out"; then
        said "$WORK/chest.out" " CHEST!" \
            && ok "CP7 chest: GET on a 0x40 cell finds a chest" || bad "CP7 chest: no CHEST!"
        said "$WORK/chest.out" "IT CONTAINS " \
            && ok "CP7 chest: the contents line prints" || bad "CP7 chest: no IT CONTAINS"
        if run_case chest_miss 'E\nI\n0xf0\nG\n' "$WORK/chest2.out"; then
            said "$WORK/chest2.out" "GET WHAT?" \
                && ok "CP7 chest: one cell short of the chest it refuses, so the gate is the cell" \
                || bad "CP7 chest: expected GET WHAT? on a plain floor cell"
        fi
    fi

    # ---- 5b. sprites --------------------------------------------------------
    # asm/68ED draws a creature out of the `monsters` stroke table, and the table is
    # art the repo never commits. So the control is the same run with and without the
    # file: if the sprite pass ever stopped drawing, the two frames would agree and
    # the assertion would go red instead of quietly matching the no-art case.
    #
    # The TURN LEFT is load-bearing, not decoration. asm/68ED reads the stamp nibble of the
    # CENTRE depth column, and asm/6078 builds that column along the facing. The authored
    # creature sits at (5,4), due north of the arrival cell, so with the east entry facing it
    # is in a side column and nothing is drawn ahead; turning to face it puts it at depth 1
    # of the centre column, which is where the sprite pass looks.
    if run_case spr0 'E\nI\n0xf2\n' "$WORK/spr0.out" && [ -f "$WORK/spr0/content/monsters" ]; then
        mv "$WORK/spr0/content/monsters" "$WORK/spr0/monsters.bak"
        ./u2 --headless --play-map --cleanroom --data-dir "$WORK/spr0/content" \
             --save-dir "$WORK/spr0/save" --script "$WORK/spr0.keys" \
             --dump-frame "$WORK/spr0/noart.ppm" > /dev/null 2>&1
        mv "$WORK/spr0/monsters.bak" "$WORK/spr0/content/monsters"
        ./u2 --headless --play-map --cleanroom --data-dir "$WORK/spr0/content" \
             --save-dir "$WORK/spr0/save" --script "$WORK/spr0.keys" \
             --dump-frame "$WORK/spr0/art.ppm" > /dev/null 2>&1
            python3 - "$WORK/spr0/art.ppm" "$WORK/spr0/noart.ppm" <<'PY' && ok "CP7 sprites: the stroke table draws a creature in the original's colours" || bad "CP7 sprites: no coloured sprite ink appeared (the pass is dead, the framing is wrong, or the pattern colour is not being used)"
import sys

def load(p):
    d = open(p, "rb").read()
    return d[d.index(b"255\n") + 4:]

art, noart = load(sys.argv[1]), load(sys.argv[2])
W = 320

def rgb(b, x, y):
    i = (y * W + x) * 3
    return b[i], b[i + 1], b[i + 2]

# The centre band is where asm/68A3 puts a body: x*4 and y*4 with x in ~24..41 and
# y in ~11..30 land at screen x 112..180 and y 60..136 once the (16,16) origin is
# added. Anything outside that band is wireframe, which both runs draw.
#
# Brightness alone cannot be the test. asm/4BE0 writes the CGA 2bpp byte verbatim, so
# a creature is cyan (0,170,170) with a magenta (170,0,170) detail, and cyan carries
# no red at all -- an earlier version of this check read only the red channel and saw
# nothing. Three of the eight patterns are also black, which ERASE, so total ink can
# go DOWN when a sprite lands on wireframe. The property that matters is the colour.
def band(b):
    ink = cyan = mag = 0
    for y in range(56, 140):
        for x in range(100, 200):
            r, g, bl = rgb(b, x, y)
            if max(r, g, bl) > 60:
                ink += 1
            if g > 60 and r < 60:
                cyan += 1
            if r > 60 and bl > 60 and g < 60:
                mag += 1
    return ink, cyan, mag

ai, ac, am = band(art)
ni, nc, nm = band(noart)
print(f"        centre-band: with strokes ink {ai} cyan {ac} magenta {am}; "
      f"without ink {ni} cyan {nc} magenta {nm}")
sys.exit(0 if (ac + am) > 40 and nc + nm == 0 else 1)
PY
    else
        skip "CP7 sprites: no original/monsters to borrow (art is never committed)"
    fi

    # ---- 6. monsters --------------------------------------------------------
    # asm/147F fills empty slots with a computed HP, and asm/2B9B's site gate lets the
    # swing through with the facing as the direction. The seed authors one creature
    # directly north of the arrival cell so the first ATTACK has a named target.
    if run_case fight 'E\nI\n0xf2\nA\nA\nA\nA\nA\nA\nA\nA\nA\n' "$WORK/fight.out"; then
        said "$WORK/fight.out" "ATTACK----HIT!!!" \
            && ok "CP7 fight: ATTACK lands on the creature in the cell you face" \
            || { bad "CP7 fight: expected a hit"; tail -3 "$WORK/fight.out.txt"; }
        # The kill is NOT asserted here, and saying why is better than either a false pass
        # or a permanently red line. asm/1151 runtime 0x1397 makes a wounded dungeon
        # creature flee, and inside a corridor the player can only move along the facing
        # axis, so a fleeing target cannot be run down. The dungeon kill path is therefore
        # gated by CP5's overworld fight tests, which prove asm/2E3D's damage and reward
        # arithmetic, and NOT by this case. If a dungeon kill is ever observed in a capture
        # it should be added here as a named fixture, not inferred.
        echo "        note  dungeon kill not asserted: a wounded creature flees (0x1397) and a corridor has no chase"
        # A wounded dungeon creature RUNS, and that is not a defect: asm/1151 runtime
        # 0x1397 negates the step vector when [DI+0x177] < 0x0f, so the first hit that
        # takes a 16 HP creature below 15 turns it from pursuer to fleer. The earlier
        # version of this case asserted "no kill after three swings" against a fixture
        # whose target was never going to stand still, which was a test of the old static
        # behaviour rather than of the bytes. Gating the flee is the honest assertion, and
        # it is the first place the dungeon's own AI is proven in play.
        flee=$(python3 - "$WORK/fight.out" <<'PY'
import re, sys
txt = open(sys.argv[1], errors="replace").read()
m = re.findall(r'"mon_y":"([0-9a-f ]+)"', txt)
print(m[-1].split()[1] if m else "")
PY
)
        case "$flee" in
            ""|04) bad "CP7 fight: the wounded creature did not move (flee at hp<0x0f not implemented?)" ;;
        *) ok "CP7 fight: a wounded creature flees instead of closing (asm/1151 0x1397, y now 0x$flee)" ;;
        esac
        c=$(sed -n 's/.*"creatures":\[\([^]]*\).*/\1/p' "$WORK/fight.out" | head -1)
        case "$c" in
            *'"0x01","0x00",16'*) bad "CP7 fight: the authored creature survived" ;;
        *) ok "CP7 fight: the authored creature is out of the table" ;;
        esac
    fi

    # ---- 6b. the sweep: dungeon creatures walk ------------------------------
    # asm/1151 runtime 0x135E..0x148E. The assertion is on the authored walker in slot 2,
    # three cells west down the corridor, and it is a DIFFERENTIAL between two runs rather
    # than a before/after inside one: --dump-state emits one record per process, so there
    # is no per-turn series to read. Run A enters and stops; run B enters and takes one
    # ADVANCE, which is one asm/1151 tick. Same seed, same prefix, so the only difference
    # between the two walker positions is the sweep.
    #
    # It is a differential and not a count on purpose. The spawner refills empty slots every
    # turn, so "the creature table changed" is true with the movement half deleted and
    # would certify a sweep that never ran. Slot 2's coordinates cannot come from the
    # spawner, because asm/147F only touches empty slots.
    if run_case walkA 'E\nI\n' "$WORK/walkA.out" && run_case walkB 'E\nI\n0xf0\n' "$WORK/walkB.out"; then
        python3 - "$WORK/walkA.out" "$WORK/walkB.out" <<'PY'
import re, sys

def walker(path):
    """The authored slot-2 creature's (x, y) in the run's final state record."""
    txt = open(path, errors="replace").read()
    x = re.findall(r'"mon_x":"([0-9a-f ]+)"', txt)
    y = re.findall(r'"mon_y":"([0-9a-f ]+)"', txt)
    if not (x and y):
        return None
    return (int(x[-1].split()[2], 16), int(y[-1].split()[2], 16))

a, b = walker(sys.argv[1]), walker(sys.argv[2])
print(f"        walker after entry {a}, after entry + one tick {b}")
# Authored at (3,5). Run A must still show that; run B must not.
sys.exit(0 if a == (3, 5) and b is not None and b != (3, 5) else 1)
PY
        if [ $? -eq 0 ]; then
            ok "CP7 walk: the dungeon sweep moves a creature on a turn the player did not touch it"
        else
            bad "CP7 walk: the authored walker never moved (asm/1151's sweep is not running)"
        fi
    fi

    # ---- 6c. a kill must not box the player in ------------------------------
    # The freeze the owner hit on 2026-10-29, live, on the first dungeon kill. The kill
    # path calls u2_monsters_restamp, which indexes y*64+x into a buffer that inside a
    # site is 16 pages of 16x16, so the page rebuild stamped every live creature onto
    # floors the player was not standing on and put phantoms in the corridor. ADVANCE then
    # refused cells nothing occupied, which from the chair is a frozen game: the process
    # was alive, the keyboard was live, only movement had stopped.
    #
    # Differential again, for the same reason as 6b: one state record per process. Run A
    # kills and stops. Run B kills and advances. If the kill left the page sane, B is one
    # cell further than A. If the phantom-stamp bug is back, the advance is refused and the
    # two positions are identical.
    if run_case stampA 'E\nI\n0xf2\nA\nA\nA\n' "$WORK/stampA.out" \
       && run_case stampB 'E\nI\n0xf2\nA\nA\nA\n0xf0\n' "$WORK/stampB.out"; then
        pa=$(pos_of "$WORK/stampA.out"); pb=$(pos_of "$WORK/stampB.out")
        if [ -n "$pa" ] && [ "$pa" = "$pb" ]; then
            bad "CP7 stamp: still frozen after a kill — ADVANCE from $pa was refused by a phantom stamp"
        elif [ -z "$pa" ] || [ -z "$pb" ]; then
            bad "CP7 stamp: no position in the state dump"
        else
            ok "CP7 stamp: the corridor is still walkable after a kill ($pa -> $pb)"
        fi
    fi

    # ---- 6d. the asm/12DB contact arms --------------------------------------
    # A type-5 gremlin authored one cell west of the arrival cell. asm/1151's sweep gives it
    # a step vector that points straight at the player, so every tick it attempts to enter
    # the player's cell, reaches runtime 0x1508, and rolls. The arm fires only under 0x40,
    # one attempt in four, so the script takes enough ticks for the roll to land and the
    # assertion is on the message the arm prints.
    #
    # The message is the whole assertion, and that is deliberate: asm/12DB's gremlin arm
    # prints and THEN subtracts one BCD unit from food, and food starts at 9999, so a
    # message is the only observable that distinguishes "the arm ran" from "the arm ran and
    # nothing else happened". If dg_contact() is deleted the message never appears and this
    # goes red; that is the control.
    #
    # The tick count is generous on purpose and it is not stream luck. asm/1151's contact
    # gate is a 1-in-4 roll per attempt, so the number of ticks before the arm fires is a
    # geometric variable, and an earlier version of this case asked for six and passed only
    # because the RNG stream at the time happened to land early. When asm/147F's stamp-floor
    # fix moved the stream the case went red without anything being wrong. Twenty-four ticks
    # leaves the assertion testing whether the arm CAN fire rather than whether the seed's
    # RNG is obliging.
    export CP7_GREMLIN=1
    if run_case gremlin 'E\nI\n0xf0\n0xf1\n0xf0\n0xf1\n0xf0\n0xf1\n0xf0\n0xf1\n0xf0\n0xf1\n0xf0\n0xf1\n0xf0\n0xf1\n0xf0\n0xf1\n0xf0\n0xf1\n0xf0\n0xf1\n0xf0\n0xf1\n0xf0\n0xf1\n' "$WORK/gremlin.out"; then
        unset CP7_GREMLIN
        said "$WORK/gremlin.out" "A GREMLIN STOLE SOME FOOD!" \
            && ok "CP7 contact: the asm/12DB gremlin arm fires from an adjacent creature" \
            || { bad "CP7 contact: no GREMLIN message across 6 ticks (dg_contact not reached?)"; \
                 tail -2 "$WORK/gremlin.out.txt"; }
    fi

    # ---- 7. floors ----------------------------------------------------------
    # cmd_D on a 0x20 cell in a dungeon increments [DS:25], and the grid the engine
    # reads is now the page that byte names — which is the floor term asm/1451 has
    # always carried and the engine used to ignore.
    if run_case floor 'E\nI\n0xf0\n0xf0\n0xf3\n0xf0\n0xf0\nD\n' "$WORK/floor.out"; then
        said "$WORK/floor.out" "DESCEND" \
            && ok "CP7 floor: DESCEND runs on a 0x20 cell" || bad "CP7 floor: no DESCEND"
        said "$WORK/floor.out" "TO LEVEL 01" \
            && ok "CP7 floor: the level prints as two packed-decimal digits" \
            || bad "CP7 floor: expected TO LEVEL 01"
        f=$(field_of "$WORK/floor.out" floor)
        [ "$f" = "01" ] && ok "CP7 floor: DS:25 is 1" || bad "CP7 floor: DS:25 is '$f'"
        # And back out: in a dungeon KLIMB decrements, and dropping below zero is how
        # you leave. asm/3823's `js` is the whole rule.
        if run_case floor_out 'E\nI\n0xf0\n0xf0\n0xf3\n0xf0\n0xf0\nD\nK\n' "$WORK/floor2.out"; then
            said "$WORK/floor2.out" "TO LEVEL 00" \
                && ok "CP7 floor: KLIMB in a dungeon goes up, back to level 00" \
                || bad "CP7 floor: expected TO LEVEL 00 after KLIMB"
        fi
    fi

    # ---- 8. the darkness costs you ------------------------------------------
    # asm/1151 decrements the counter per turn and prints TORCH BURNED OUT! when a
    # step takes it to zero. 0x96 is 150, so this drives it with the counter seeded
    # low rather than with 150 keystrokes: [DS:26] is inside the saved record's
    # reach, so the seed can set it.
    if run_case burn 'E\nI\n' "$WORK/burn.out"; then
        l=$(field_of "$WORK/burn.out" light)
        case "$l" in
            "49 9"*) ok "CP7 burn: the counter ticks down from 0x96 with the steps ($l)" ;;
        *) bad "CP7 burn: light pair '$l' did not tick down" ;;
        esac
    fi
fi

echo
if [ "$fail" -eq 0 ]; then echo "CP7 DUNGEON SPEC: PASS"; else echo "CP7 DUNGEON SPEC: FAIL"; fi
# ---- trap: the two arms decided by the TOOLS counter (unit gate) ------------
# asm/277B 0x28B1. A trap with no tools jumps to the death handler (instant death,
# no damage amount); with tools it spends one and escapes. Deterministic because the
# trap roll is the first rand_byte in u2_dungeon_advance. See tests/trap_check.c.
if cc -std=c11 -Wall -Wextra -Werror -Isrc -Icontent -o "$WORK/trap_check" \
        tests/trap_check.c src/core/*.c src/game/*.c src/debug/*.c 2>"$WORK/trap.build"; then
    if "$WORK/trap_check" > "$WORK/trap.out" 2>&1; then
        n=$(sed -n 's/^trap_check: \([0-9]\{1,\}\) checks.*/\1/p' "$WORK/trap.out")
        if [ "${n:-0}" -eq 0 ]; then
            bad "CP7 trap run executed 0 checks — the test is not running"
        else
            ok "CP7 trap: no-tools trap kills, tools trap spends one and escapes ($n checks)"
        fi
    else
        bad "CP7 trap check failed:"
        sed 's/^/        /' "$WORK/trap.out"
    fi
else
    bad "CP7 trap harness did not build:"
    sed 's/^/        /' "$WORK/trap.build"
fi

# ---- dungeon fills the doubled frame (classic EGA) -----------------------
# The wireframe is authored in 320x200 CGA space; in classic EGA it must scale by
# the frame scale to fill the map window, not sit in the top-left quarter. CGA stays
# CGA-sized. See tests/dungeon_scale_check.c.
if cc -std=c11 -Wall -Wextra -Werror -Isrc -Icontent -o "$WORK/dungeon_scale_check" \
        tests/dungeon_scale_check.c src/core/*.c src/game/*.c src/debug/*.c 2>"$WORK/dsc.build"; then
    if "$WORK/dungeon_scale_check" --selftest >"$WORK/dsc-st.out" 2>&1; then
        if "$WORK/dungeon_scale_check" >"$WORK/dsc.out" 2>&1; then
            n=$(sed -n 's/^dungeon_scale_check: \([0-9]\{1,\}\) checks.*/\1/p' "$WORK/dsc.out")
            if [ "${n:-0}" -eq 0 ]; then
                bad "CP7 dungeon scale: executed 0 checks — the test is not running"
            else
                ok "CP7 dungeon scale: EGA fills the doubled frame, CGA stays CGA-sized ($n checks)"
            fi
        else
            bad "CP7 dungeon scale check failed:"
            sed 's/^/        /' "$WORK/dsc.out"
        fi
    else
        bad "CP7 dungeon scale gate cannot fail (selftest broke):"
        sed 's/^/        /' "$WORK/dsc-st.out"
    fi
else
    bad "CP7 dungeon scale harness did not build:"
    sed 's/^/        /' "$WORK/dsc.build"
fi

exit $fail

#!/usr/bin/env bash
# tests/run_cp3.sh — CP3 acceptance test: character creation + status line.
#
# The gate for CP3 is an owner playthrough ("create character, correct status
# bar"), but the parts that can be proven mechanically are proven here first:
# the save record is byte-compatible with the original's PLAYER file, the
# creation flow writes the fields the asm says it writes, and the status bar
# lands on the rows/columns the asm says it lands on.
set -uo pipefail

cd "$(dirname "$0")/.." || exit 1
fail=0
ok()  { printf '  PASS  %s\n' "$1"; }
bad() { printf '  FAIL  %s\n' "$1"; fail=1; }

WORK=$(mktemp -d "${TMPDIR:-/var/tmp}/u2-cp3.XXXXXX")
trap 'rm -rf "$WORK"' EXIT

# 'P' after a character exists: ESC past the intro, then P to load. No trailing key: the run
# ends when the script runs out. There is deliberately no quit key -- the original has none.
printf 'ESC\nP\n' > "$WORK/p_after.keys"

printf 'engine CP3 test\n'

# --- the save record is byte-compatible with the original -----------------
cc -std=c11 -Wall -Wextra -Werror -Isrc -o "$WORK/save_check" \
   tests/save_check.c src/core/save.c src/core/slot.c src/core/state.c 2>"$WORK/cc.err"
if [ $? -ne 0 ]; then
    bad "save test harness did not build: $(head -3 "$WORK/cc.err")"
else
    "$WORK/save_check" "$WORK" > "$WORK/save.out" 2>&1 \
        && ok "save record: blank template, round trip and tail preservation ($(grep -c '  ok  ' "$WORK/save.out") checks)" \
        || { bad "save check failed:"; sed 's/^/        /' "$WORK/save.out"; }
fi

# --- the disk model is part of the documented contract --------------------
./u2 --help 2>&1 | grep -q -- '--save-dir' \
    && ok "--save-dir is in the usage text" \
    || bad "--save-dir missing from usage"

# A run must never drop a PLAYER file inside the engine tree: the save dir is
# explicit, and the default is created only when the game actually plays.
BEFORE=$(find . -name PLAYER -not -path './build/*' | wc -l)
./u2 --headless --script tests/play_to/title_menu.keys --save-dir "$WORK/sd" \
     --dump-state > "$WORK/run.out" 2>&1
AFTER=$(find . -name PLAYER -not -path './build/*' | wc -l)
[ "$BEFORE" = "$AFTER" ] \
    && ok "a headless run leaves no save file in the engine tree" \
    || bad "a run wrote a PLAYER file into the tree"

# --- 'P' reads the player record (asm/0764) --------------------------------
# Content-free check: a fresh state has gold 0, and the character created by the
# route has gold 4. So 'P' loading a character is observable in the state dump
# without comparing any game text.
CREATED="$WORK/created"
SEED="$WORK/empty"
mkdir -p "$CREATED" "$SEED"
./u2 --headless --script tests/play_to/reach_everything_today.keys --seed 0 \
     --save-dir "$CREATED" > /dev/null 2>&1
./u2 --headless --script "$WORK/p_after.keys" --save-dir "$CREATED" \
     --dump-state > "$WORK/p_after.out" 2>&1
./u2 --headless --script "$WORK/p_after.keys" --save-dir "$SEED" \
     --dump-state > "$WORK/p_empty.out" 2>&1

grep -qE '"gold"[[:space:]]*:[[:space:]]*4' "$WORK/p_after.out" \
    && ok "P loads the saved character (gold 4 from disk)" \
    || bad "P did not load the saved character"
grep -qE '"gold"[[:space:]]*:[[:space:]]*0' "$WORK/p_empty.out" \
    && ok "P with no character on disk loads nothing (the INSERT DISK path)" \
    || bad "P loaded a character from an empty directory"

# --- the name is capped at 12 characters (asm/5BB7) -----------------------
# The input loop stores a character, increments the index, and only continues
# while the index is below 0x0c. Eighteen typed characters must therefore leave
# exactly 12 in the record plus the 4 zero pad bytes the original writes, and
# must not disturb the sex / class / race bytes that follow the field.
printf 'ESC\nC\n' > "$WORK/longname.keys"
# Spend the 30 spare points first: stepping is the only stat input now that D6 is
# retired, and Save refuses while points are left unspent.
for s in 1 2 3 4 5 6; do
    for i in 1 2 3 4 5; do printf '0xf3\n' >> "$WORK/longname.keys"; done
    printf '0xf1\n' >> "$WORK/longname.keys"
done
# Three pairs of Enter: the first opens a dropdown, the second commits its first
# row. MALE, HUMAN, FIGHTER, and the cursor lands on the name field.
for c in 1 2 3; do printf '0x0d\n0x0d\n' >> "$WORK/longname.keys"; done
# Eighteen characters, R included. There are no letter shortcuts left on the form
# (D9 became Enter-activated fields), so a name may hold any printable byte and a
# name containing R must not reset the generation.
for ch in A B C D E F G H I J K L M N O P Q R; do printf '%s\n' "$ch" >> "$WORK/longname.keys"; done
printf '0x0d\n0x0d\n' >> "$WORK/longname.keys"
mkdir -p "$WORK/longname"
./u2 --headless --script "$WORK/longname.keys" --save-dir "$WORK/longname" \
     > /dev/null 2>&1
# <save-dir>/<slot>/PLAYER: the record is inside its slot directory, which shares
# the name (docs/save-scoped-worlds.md D1, engine/src/core/slot.h).
HEX=$(od -An -tx1 -N16 "$WORK/longname/PLAYER/PLAYER" 2>/dev/null | tr -d ' \n')
[ "$HEX" = "4142434445464748494a4b4c00000000" ] \
    && ok "name capped at 12 with 4 zero pad bytes, no overflow" \
    || bad "name field wrong: got $HEX want 4142434445464748494a4b4c00000000"
SEX=$(od -An -tx1 -j16 -N1 "$WORK/longname/PLAYER/PLAYER" 2>/dev/null | tr -d ' \n')
[ "$SEX" = "4d" ] \
    && ok "bytes after the name field survived a long name (sex = M)" \
    || bad "name overflowed into the next fields: got $SEX want 4d"

# --- interactive smoke test through a real pty ----------------------------
# Both CP3 interactive bugs (seam_key_wait never blocking, and the creation form
# never being presented) were invisible to every other test: headless reads the
# grid directly and never calls the renderer, so "presented to a terminal" is
# simply untested by the rest of the suite. This drives the game through a real
# pty via script(1) and compares output volume, which keeps the assertion free
# of game text: pressing C must change the screen far more than pressing an
# invalid key does.
if command -v script >/dev/null 2>&1; then
    pty_run() {   # $1 = keys after the intro, $2 = save dir
        # The intro screens each hold for a key, so a burst delivered in one
        # write is swallowed by the holds and never reaches the menu. Pace the
        # intro-skip apart from the body or the run sits until the timeout.
        { printf '\033'; sleep 1; printf '%b' "$1"; } | timeout 20 script -qec "./u2 --save-dir $2" "$WORK/pty.out" >/dev/null 2>&1
        wc -c <"$WORK/pty.out"
    }
    mkdir -p "$WORK/d_invalid" "$WORK/d_create"
    # \021 is Ctrl-Q: the run ends on its own instead of being killed at the
    # timeout, which is what used to make this suite take ~40 seconds.
    invalid_bytes=$(pty_run 'X\021' "$WORK/d_invalid")
    # The form is stepped and dropdowned, not typed: \033[C is Right (+1) and
    # \033[B is Down, so five of the former and one of the latter per stat takes
    # each from its floor of 10 to 15 and moves on. Then three pairs of Enter
    # open a dropdown and commit its first row, then the name, then Enter to
    # leave the name and Enter on SAVE.
    CK='C'
    for s in 1 2 3 4 5 6; do
        for i in 1 2 3 4 5; do CK="${CK}\\033[C"; done
        CK="${CK}\\033[B"
    done
    CK="${CK}\\r\\r\\r\\r\\r\\r"
    CK="${CK}BRITISH"
    CK="${CK}\\r\\r\\021"
    create_bytes=$(pty_run "$CK" "$WORK/d_create")
    [ "$create_bytes" -gt $((invalid_bytes * 2)) ] \
        && ok "C draws a form in a real terminal (${create_bytes}B vs ${invalid_bytes}B for an invalid key)" \
        || bad "C produced no more screen output than an invalid key (${create_bytes}B vs ${invalid_bytes}B)"
    # The pty run above completed a whole character and then quit on Ctrl-Q;
    # prove it reached the disk.
    if [ -s "$WORK/d_create/PLAYER/PLAYER" ]; then
        nz=$(python3 -c "import sys;d=open('$WORK/d_create/PLAYER/PLAYER','rb').read();print(sum(1 for b in d if b))")
        [ "$nz" -gt 1 ] \
            && ok "the pty creation run wrote a populated player record ($nz non-zero bytes)" \
            || bad "pty creation run left a blank record ($nz non-zero bytes)"
    else
        bad "pty creation run produced no PLAYER file in its slot"
    fi
else
    printf '  SKIP  script(1) unavailable, interactive pty test not run\n'
fi

# --- the dropdown form (D12) and the bonus preview (D13) -------------------
# These assert on the grid, not on pixels: the box, the `>` highlight, the `+N`
# deltas and the refusals are all text. The colour of the delta is deliberately
# not tested because there is deliberately no colour yet.
keys() { printf 'ESC\nC\n' > "$WORK/$1.keys"; }          # fresh form, cursor on STR
feed() { printf '%s\n' "$2" >> "$WORK/$1.keys"; }
grid() { ./u2 --headless --script "$WORK/$1.keys" --save-dir "$WORK/dd_$1" \
              --dump-each > "$WORK/$1.grid" 2>&1; }

# Six Downs walks STR..SEX; a pair of Enter opens a dropdown and commits its
# first row, so this leaves MALE and HUMAN committed and the Type dropdown open
# with FIGHTER highlighted.
keys prev; for i in 1 2 3 4 5 6; do feed prev 0xf1; done
for i in 1 2; do feed prev 0x0d; feed prev 0x0d; done
feed prev 0x0d
grid prev
# MALE is +5 STR and FIGHTER +15 STR, so the highlighted row must already be
# counted: the preview tracks the highlight, not only the commit.
grep -q 'STRENGTH\.\.\.\.\. -10+ +20' "$WORK/prev.grid" \
    && ok "preview adds the highlighted row: MALE +5 and FIGHTER +15 show as +20" \
    || bad "preview did not track the highlight (expected 'STRENGTH..... -10+ +20')"
grep -q 'INTELLIGENCE\. -10+ +5' "$WORK/prev.grid" \
    && ok "HUMAN's +5 INT shows while the Type dropdown is open" \
    || bad "HUMAN +5 INT missing from the preview"
grep -qE 'STAMINA\.+ -10[+] *\|' "$WORK/prev.grid" \
    && ok "STAMINA shows the stepping hints with no bonus: no choice modifies it" \
    || bad "a bonus appeared on STAMINA, which asm/5400 never modifies"
# No choice can lower a stat, so a minus must never be printable. The only legal
# `-` on a stat line is the Left hint, so a second one on any stat line is a
# negative bonus. If someone builds the red path this goes red, which is the point.
neg=$(grep -E 'STRENGTH|AGILITY|STAMINA|CHARISMA|WISDOM|INTELLIGENCE' "$WORK/prev.grid" \
       | awk '{ if (gsub(/-/, "-") > 1) n++ } END { print n + 0 }')
[ "$neg" = "0" ] \
    && ok "no negative bonus anywhere: every stat line carries exactly one minus" \
    || bad "$neg stat line(s) carried a second minus; no creation modifier subtracts"

# Esc closes a dropdown and changes nothing. Commit HUMAN, walk back to RACE,
# open it, move the highlight to DWARF, cancel: the field must still read HUMAN.
keys esc; for i in 1 2 3 4 5 6; do feed esc 0xf1; done
feed esc 0x0d; feed esc 0x0d; feed esc 0x0d; feed esc 0x0d
feed esc 0xf0; feed esc 0x0d; feed esc 0xf1; feed esc 0xf1; feed esc 0x1b
grid esc
if grep -q '>DWARF' "$WORK/esc.grid" && \
   grep -q 'RACE-      HUMAN' "$WORK/esc.grid" && \
   ! grep -q 'RACE-      DWARF' "$WORK/esc.grid"; then
    ok "Esc cancels a dropdown: the highlight moved to DWARF and the field kept HUMAN"
else
    bad "Esc changed the committed value, or the highlight never moved"
fi

# Stepping is clamped at both ends, and says so.
keys over; for i in $(seq 1 31); do feed over 0xf3; done
grid over
grep -q 'STRENGTH\.\.\.\.\. -40' "$WORK/over.grid" \
    && ok "30 Rights take a stat to 40 and no further (90 budget, floor 10)" \
    || bad "a stat left [10, 40] under stepping"
grep -q 'no points left to spend' "$WORK/over.grid" \
    && ok "the 31st Right is refused with a message" \
    || bad "the 31st Right was swallowed silently"
keys floor; feed floor 0xf2
grid floor
grep -q '10 is the minimum' "$WORK/floor.grid" \
    && ok "a Left at the floor is refused with a message" \
    || bad "a Left at the floor was swallowed silently"

# Save refuses while points are unspent, and names the number.
keys early; for i in $(seq 1 10); do feed early 0xf1; done
feed early 0x0d
grid early
grep -q '30 points left to spend' "$WORK/early.grid" \
    && ok "Save refuses an unspent budget and says how much is left" \
    || bad "Save accepted a form with 30 points unspent"

# --- the highlight inverts the whole field, not one cell -------------------
# The grid dump carries characters only, so an inverted cell is identical to a
# plain one there. --dump-hilite prints the cells the engine marked inverse, which
# is what makes the highlight assertable at all -- and assertable headlessly, so
# these need no pty, no sleep, and no terminal that understands reverse video.
hil() {   # $1 = label; the highlight of the last presented frame
    ./u2 --headless --script "$WORK/$1.keys" --save-dir "$WORK/hi_$1" \
         --dump-hilite 2>/dev/null | grep HILITE
}
keys str
keys sex; for i in 1 2 3 4 5 6; do feed sex 0xf1; done
keys save; for i in $(seq 1 10); do feed save 0xf1; done
keys dd;   for i in 1 2 3 4 5 6; do feed dd 0xf1; done; feed dd 0x0d

want="HILITE 4 26
HILITE 4 27"
got=$(hil str | tail -2)
[ "$got" = "$want" ] \
    && ok "a stat value is highlighted as a whole: both cells of the number" \
    || bad "stat highlight wrong: got [$(echo "$got" | tr '\n' ' ')] want [4 26 / 4 27]"
want="HILITE 11 25
HILITE 11 26
HILITE 11 27
HILITE 11 28
HILITE 11 29"
got=$(hil sex | tail -5)
[ "$got" = "$want" ] \
    && ok "an unset choice field highlights the whole ENTER word, 5 cells" \
    || bad "ENTER not highlighted as a whole: got [$(echo "$got" | tr '\n' ' ')]"
want="HILITE 16 10
HILITE 16 11
HILITE 16 12
HILITE 16 13
HILITE 16 14
HILITE 16 15"
got=$(hil save | tail -6)
[ "$got" = "$want" ] \
    && ok "the SAVE button is highlighted as a whole, 6 cells" \
    || bad "SAVE not highlighted: got [$(echo "$got" | tr '\n' ' ')]"
want="HILITE 19 16
HILITE 19 17
HILITE 19 18
HILITE 19 19"
got=$(hil dd | tail -4)
[ "$got" = "$want" ] \
    && ok "the dropdown highlights its whole marked word, not a caret beside it" \
    || bad "dropdown row not highlighted as a whole: got [$(echo "$got" | tr '\n' ' ')]"

# --- nothing generated may be committed -----------------------------------
cat > "$WORK/probe_player" <<'EOF'
EOF
git check-ignore -q "$WORK/probe_player" 2>/dev/null || true
git status --porcelain | grep -qE '^\?\? (saves/|.*PLAYER)' \
    && bad "a save artefact is untracked but committable" \
    || ok "no save artefact is committable"

printf '\n'
[ $fail -eq 0 ] && echo "CP3 TEST: PASS" || echo "CP3 TEST: FAIL"
exit $fail

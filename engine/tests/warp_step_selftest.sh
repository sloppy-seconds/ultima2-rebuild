#!/usr/bin/env bash
# warp_step_selftest.sh — prove warp_step_check can actually fail.
#
# A gate that only ever prints ok is worse than no gate, and this project has been
# bitten more than once by a checker that could not go red where it counted. Each
# control here mutates ONE decoded fact out of the step rule, rebuilds the test
# against the mutant, and requires a non-zero exit. A mutant that still passes means
# the check is not asserting what it claims.
#
# Every mutation is verified to have landed before the mutant is trusted, because
# str.replace() returns the string unchanged when the pattern misses and a no-op and a
# success are otherwise indistinguishable.
set -u
cd "$(dirname "$0")/.." || exit 1

SRC=src/game/space_view.c
WORK="${WORK:-/var/tmp/u2/warp-step-selftest}"
mkdir -p "$WORK"
fails=0
controls=0

OBJS=$(find build/src -name '*.o' ! -name 'main.o' | tr '\n' ' ')
if [ -z "$OBJS" ]; then
    echo "SKIP: no build/src objects — run 'make -C engine' first"
    exit 0
fi

# run_control <name> <python-mutator-file>
run_control() {
    local name="$1" mut="$2"
    controls=$((controls + 1))
    local mfile="$WORK/mutated.c"
    rm -f "$mfile"
    python3 "$mut" "$SRC" "$mfile" || { echo "  BAD  $name: mutator failed"; fails=$((fails+1)); return; }
    if cmp -s "$SRC" "$mfile"; then
        echo "  BAD  $name: mutant is identical to the source — the pattern no longer matches"
        fails=$((fails+1)); return
    fi
    if cc -std=c11 -Wall -Wextra -Werror -Isrc -Icontent -o "$WORK/mut" \
        tests/warp_step_check.c "$mfile" $(find build/src -name '*.o' ! -name 'main.o' \
            ! -path '*space_view*' | tr '\n' ' ') 2>"$WORK/mut.build"; then
        if "$WORK/mut" > "$WORK/mut.out" 2>&1; then
            echo "  FAIL $name: mutant passed — the check does not assert it"
            fails=$((fails+1))
        else
            echo "  ok   $name: mutant went red"
        fi
    else
        echo "  BAD  $name: mutant did not build"
        sed 's/^/         /' "$WORK/mut.build" | head -3
        fails=$((fails+1))
    fi
}

# Control 1: the step stops growing with distance. This is the single fact that makes the
# field radial, so if the test cannot catch its absence the test is decorative.
cat > "$WORK/m1.py" <<'PY'
import sys, pathlib
src, dst = sys.argv[1], sys.argv[2]
s = pathlib.Path(src).read_text()
out = s.replace("            step = di + 1;\n            sx[i] = (uint8_t)(old + step);          /* wraps, stored unconditionally */",
                "            step = 1;\n            sx[i] = (uint8_t)(old + step);")
if out == s: sys.exit(1)
pathlib.Path(dst).write_text(out)
PY
run_control "step no longer grows with distance" "$WORK/m1.py"

# Control 2: both arms add, so stars no longer move away from the ship on the left.
cat > "$WORK/m2.py" <<'PY'
import sys, pathlib
src, dst = sys.argv[1], sys.argv[2]
s = pathlib.Path(src).read_text()
out = s.replace("            if (old >= step) sx[i] = (uint8_t)(old - step);",
                "            if (old >= step) sx[i] = (uint8_t)(old + step);")
if out == s: sys.exit(1)
pathlib.Path(dst).write_text(out)
PY
run_control "left arm steps toward the ship" "$WORK/m2.py"

# Control 3: the y add arm's sign gate is removed, so a result at or past 0x80 is stored.
# asm/6B3D puts the store AFTER `jns` on this arm, so the gate is the byte-verified fact.
cat > "$WORK/m3.py" <<'PY'
import sys, pathlib
src, dst = sys.argv[1], sys.argv[2]
s = pathlib.Path(src).read_text()
out = s.replace("            if ((old + step) < 0x80) sy[i] = (uint8_t)(old + step);",
                "            sy[i] = (uint8_t)(old + step);")
if out == s: sys.exit(1)
pathlib.Path(dst).write_text(out)
PY
run_control "y add-arm sign gate removed" "$WORK/m3.py"

# Control 4: the x add arm's store is put back under an overflow guard. This is not a
# hypothetical -- it is the exact mistake the first version of this function made, and it is
# what made the field drain to the x edges while every individual step still looked right.
# The original stores at L_006d98 BEFORE the `jae`, so the wrap is kept.
cat > "$WORK/m4.py" <<'PY'
import sys, pathlib
src, dst = sys.argv[1], sys.argv[2]
s = pathlib.Path(src).read_text()
out = s.replace("            sx[i] = (uint8_t)(old + step);          /* wraps, stored unconditionally */",
                "            if (old + step <= 0xff) sx[i] = (uint8_t)(old + step);")
if out == s: sys.exit(1)
pathlib.Path(dst).write_text(out)
PY
run_control "x add-arm store re-gated on overflow" "$WORK/m4.py"

echo "warp_step selftest: $controls controls, $fails bad"
[ "$fails" -eq 0 ] || exit 1
exit 0

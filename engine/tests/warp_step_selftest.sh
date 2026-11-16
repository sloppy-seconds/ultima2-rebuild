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

# Control 1: the two x arms are swapped. This is not hypothetical — it is the exact mistake
# the first version of this function made, because `L_006d55 jb L_006d7f` branches on
# old < ship, so the FALL-THROUGH is the >= case and the fall-through is the subtract arm.
# Swapping them makes the field run away from the ship instead of toward it.
cat > "$WORK/m1.py" <<'PY'
import sys, pathlib
src, dst = sys.argv[1], sys.argv[2]
s = pathlib.Path(src).read_text()
out = s.replace("        if (old >= ship_x) {", "        if (old < ship_x) {")
if out == s: sys.exit(1)
pathlib.Path(dst).write_text(out)
PY
run_control "x arms swapped" "$WORK/m1.py"

# Control 2: the bucket complement dropped. `L_006d57 xor $0xff,%al` before the four rcr is
# what makes the step GROW as a star approaches the ship; without it the step shrinks and
# the field is a drift, not a radial pull.
cat > "$WORK/m2.py" <<'PY'
import sys, pathlib
src, dst = sys.argv[1], sys.argv[2]
s = pathlib.Path(src).read_text()
out = s.replace("            d = (old - ship_x) & 0xff;\n            di = ((~d) & 0xff) >> 4;",
                "            d = (old - ship_x) & 0xff;\n            di = d >> 4;")
if out == s: sys.exit(1)
pathlib.Path(dst).write_text(out)
PY
run_control "x bucket no longer complemented" "$WORK/m2.py"

# Control 3: the y add arm's sign gate is removed, so a result at or past 0x80 is stored.
# asm/6B3D puts the store AFTER `jns` on this arm (L_006de9 adc, jns, L_006df3 mov), so the
# gate is the byte-verified fact.
cat > "$WORK/m3.py" <<'PY'
import sys, pathlib
src, dst = sys.argv[1], sys.argv[2]
s = pathlib.Path(src).read_text()
out = s.replace("            if (nv < 0x80) sy[i] = (uint8_t)nv;",
                "            sy[i] = (uint8_t)nv;")
if out == s: sys.exit(1)
pathlib.Path(dst).write_text(out)
PY
run_control "y add-arm sign gate removed" "$WORK/m3.py"

# Control 4: the x add arm's store is put back under an overflow guard. The original stores
# at L_006d98 BEFORE the `jae`, so the wrap is kept — gating it is what makes a field drain
# to the edges while every individual step still looks right.
cat > "$WORK/m4.py" <<'PY'
import sys, pathlib
src, dst = sys.argv[1], sys.argv[2]
s = pathlib.Path(src).read_text()
out = s.replace("            sx[i] = (uint8_t)((old + step) & 0xff);",
                "            if (old + step <= 0xff) sx[i] = (uint8_t)((old + step) & 0xff);")
if out == s: sys.exit(1)
pathlib.Path(dst).write_text(out)
PY
run_control "x add-arm store re-gated on overflow" "$WORK/m4.py"

echo "warp_step selftest: $controls controls, $fails bad"
[ "$fails" -eq 0 ] || exit 1
exit 0

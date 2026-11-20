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
#
# The patterns are source TEXT, so a rewrite of the step rule has to update this file in
# the same commit. cd13d2a rewrote the x arm from the capture census and left controls 1
# and 2 pointing at code that no longer existed; they reported BAD for weeks afterwards,
# which is the selftest telling the truth and nobody listening. If a control goes BAD with
# "mutator failed", the code moved — fix the pattern, do not delete the control.
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

# Control 1: the x arm's near/far split is inverted. cd13d2a rebuilt this arm from the
# capture census: it is bucket-based now, `di = d >> 4`, and `di < 8` is the near side that
# pushes a star away from the crosshair while the far side pulls it back. The pattern this
# control used to match — an `old >= ship_x` comparison — is gone, because the arm no longer
# compares against the ship at all. Inverting the split is the same failure the original
# swapped-arms mistake was: the field runs the wrong way.
cat > "$WORK/m1.py" <<'PY'
import sys, pathlib
src, dst = sys.argv[1], sys.argv[2]
s = pathlib.Path(src).read_text()
out = s.replace("        if (di < 8) {", "        if (di >= 8) {")
if out == s: sys.exit(1)
pathlib.Path(dst).write_text(out)
PY
run_control "x near/far split inverted" "$WORK/m1.py"

# Control 2: the y arm's bucket complement dropped. `L_006d57 xor $0xff,%al` before the four
# rcr is what makes the step GROW as a star approaches the ship; without it the step shrinks
# and the field is a drift, not a radial pull. The complement lives on the y arm only — the x
# arm gets the equivalent effect from the near/far split above — which is why this names the
# complement line rather than the ship_x pair it used to look for.
cat > "$WORK/m2.py" <<'PY'
import sys, pathlib
src, dst = sys.argv[1], sys.argv[2]
s = pathlib.Path(src).read_text()
out = s.replace("            di = ((~d) & 0xff) >> 4;",
                "            di = d >> 4;")
if out == s: sys.exit(1)
pathlib.Path(dst).write_text(out)
PY
run_control "y bucket no longer complemented" "$WORK/m2.py"

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

# Control 5: the x arm's near-side magnitude is off by one. The direction controls prove the
# field points the right way; this proves the check also pins HOW FAR a star moves, which is
# the fact cd13d2a actually measured (buckets 0..7 give +1..+8, and the census behind that is
# hundreds of samples per bucket). One pixel per frame is invisible in a single step and
# obvious over a census, so it belongs under the gate.
cat > "$WORK/m5.py" <<'PY'
import sys, pathlib
src, dst = sys.argv[1], sys.argv[2]
s = pathlib.Path(src).read_text()
out = s.replace("            step = di + 1;", "            step = di + 2;")
if out == s: sys.exit(1)
pathlib.Path(dst).write_text(out)
PY
run_control "x near-side step off by one" "$WORK/m5.py"

echo "warp_step selftest: $controls controls, $fails bad"
[ "$fails" -eq 0 ] || exit 1
exit 0

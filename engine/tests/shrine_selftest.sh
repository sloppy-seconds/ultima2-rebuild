#!/usr/bin/env bash
# Mutation controls for the cleric shrine's greeting word.
#
# Two ways this feature can be wrong, and each one looks like an improvement:
#   * the fidelity guard is dropped, so classic mode starts saying SISTER. That is a
#     fidelity regression wearing the costume of a bug fix -- asm/1869's `cmp $0x1,%al`
#     against a byte that only ever holds 'M'/'F' is dead, and the original calls every
#     visitor BROTHER.
#   * the gender test copies the original's broken immediate (`== 1`) instead of the value
#     the field actually holds ('F'). That compiles, passes a one-directional test, and
#     changes nothing.
# So each control mutates one of those and requires commerce_check to go RED on the
# assertion that names it.
#
# Mirrors warp_step_selftest.sh: mutate in place, rebuild, run, restore. A no-op mutation is
# reported as BAD rather than silently passing, because a control whose pattern no longer
# matches is worse than no control.
set -u
cd "$(dirname "$0")/.." || exit 1
SRC=src/game/commerce.c
WORK="${TMPDIR:-/var/tmp}/u2-shrine-selftest.$$"
mkdir -p "$WORK"
trap 'cp "$SRC" "$WORK/restore" 2>/dev/null; rm -rf "$WORK"' EXIT
cp "$SRC" "$WORK/pristine"

fails=0
controls=0

run_control() {
    local name="$1" mut="$2" want="$3"
    controls=$((controls + 1))
    cp "$WORK/pristine" "$SRC"
    python3 "$mut" "$SRC" || { echo "  BAD  $name: mutator failed"; fails=$((fails+1)); return; }
    cmp -s "$SRC" "$WORK/pristine" && { echo "  BAD  $name: mutant identical to source"; fails=$((fails+1)); return; }
    if ! make -C . >/dev/null 2>&1; then
        echo "  BAD  $name: mutant did not compile (a build error is not a caught bug)"
        fails=$((fails+1)); return
    fi
    if make -C . test >"$WORK/out" 2>&1; then
        echo "  BAD  $name: mutant passed -- the check does not assert it"
        fails=$((fails+1)); return
    fi
    if ! grep -qF "$want" "$WORK/out"; then
        echo "  BAD  $name: went red, but not on the expected assertion (wanted: $want)"
        fails=$((fails+1)); return
    fi
    echo "  ok   $name: red on \"$want\""
}

cat > "$WORK/m1.py" <<'PY'
import sys, pathlib
p = pathlib.Path(sys.argv[1]); s = p.read_text()
out = s.replace("    if (fidelity != U2_FIDELITY_ENHANCED)\n        return 0;\n",
                "    (void)fidelity;\n")
if out == s: sys.exit(1)
p.write_text(out)
PY
run_control "fidelity guard removed" "$WORK/m1.py" \
    "classic mode must say BROTHER even to a female character"

cat > "$WORK/m2.py" <<'PY'
import sys, pathlib
p = pathlib.Path(sys.argv[1]); s = p.read_text()
out = s.replace("    return sex == 'F' || sex == 'f';", "    return sex == 1;")
if out == s: sys.exit(1)
p.write_text(out)
PY
run_control "gender test copies the original's broken immediate" "$WORK/m2.py" \
    "enhanced mode: a female character is SISTER"

cp "$WORK/pristine" "$SRC"
make -C . >/dev/null 2>&1
echo "shrine selftest: $controls controls, $fails bad"
[ "$fails" -eq 0 ] || exit 1

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
# Mirrors warp_step_selftest.sh: mutate a copy, compile the one check that names the
# assertion, run it, require red. A no-op mutation is reported as BAD rather than silently
# passing, because a control whose pattern no longer matches is worse than no control.
#
# It used to run `make -C . test` per control, which made this the most expensive gate in
# tools/check_all.sh — 77s of a 197s suite, to prove two assertions that live in one file.
# Both controls below name the assertion they expect to go red, so compiling commerce_check
# alone is strictly the same test, and warp_step_selftest.sh runs ten controls this way in
# under a second.
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
    local mfile="$WORK/mutated.c"
    cp "$WORK/pristine" "$mfile"
    python3 "$mut" "$mfile" || { echo "  BAD  $name: mutator failed"; fails=$((fails+1)); return; }
    cmp -s "$mfile" "$WORK/pristine" && { echo "  BAD  $name: mutant identical to source"; fails=$((fails+1)); return; }
    # Prefer the built objects (one TU plus a link); fall back to compiling from sources the
    # way run_cp6.sh does, so a tree that never ran `make` still gets a real control instead
    # of a skip that would look like a pass.
    local objs
    objs=$(find build/src -name '*.o' ! -name 'main.o' ! -path '*commerce*' 2>/dev/null | tr '\n' ' ')
    if [ -n "$objs" ]; then
        cc -std=c11 -Wall -Wextra -Werror -Isrc -Icontent -o "$WORK/mut" \
            tests/commerce_check.c "$mfile" $objs 2>"$WORK/build.err"
    else
        cc -std=c11 -Wall -Wextra -Werror -Isrc -Icontent -o "$WORK/mut" \
            tests/commerce_check.c src/core/*.c "$mfile" \
            $(ls src/game/*.c | grep -v '/commerce\.c$') src/debug/*.c 2>"$WORK/build.err"
    fi
    if [ $? -ne 0 ]; then
        echo "  BAD  $name: mutant did not compile (a build error is not a caught bug)"
        sed 's/^/         /' "$WORK/build.err" | head -3
        fails=$((fails+1)); return
    fi
    if "$WORK/mut" >"$WORK/out" 2>&1; then
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

echo "shrine selftest: $controls controls, $fails bad"
[ "$fails" -eq 0 ] || exit 1

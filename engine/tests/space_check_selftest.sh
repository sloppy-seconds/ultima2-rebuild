#!/usr/bin/env bash
# Prove the space gate can go red where it enforces it.
#
# A checker that returns a wrong verdict is worse than a tool that returns a wrong
# answer, so every mutant is run from the same directory and the same build the
# real gate uses, and the clean run must also be asserted green — a selftest that
# only checks the mutants can pass while the gate itself is broken.
set -u
cd "$(dirname "$0")/.." || exit 1

WORK=$(mktemp -d /var/tmp/u2-space-selftest.XXXXXX)
trap 'rm -rf "$WORK"' EXIT

cc -std=c11 -Wall -Wextra -Werror -Isrc -o "$WORK/space_check" \
   tests/space_check.c src/game/space.c 2>"$WORK/cc.err"
if [ $? -ne 0 ]; then
    echo "SPACE SELFTEST: FAIL — harness did not build"
    head -5 "$WORK/cc.err"
    exit 1
fi

fails=0
total=0

# The clean run must pass, or the mutants prove nothing.
total=$((total+1))
if "$WORK/space_check" >"$WORK/clean.out" 2>&1; then
    echo "  ok    clean run passes ($(grep -c '  PASS  ' "$WORK/clean.out") checks)"
else
    echo "  BAD   clean run FAILED — the gate is broken, not the mutants"
    sed 's/^/          /' "$WORK/clean.out" | tail -5
    fails=$((fails+1))
fi

for m in gate-pre earth-00 tile-any step-4; do
    total=$((total+1))
    if "$WORK/space_check" --mutant "$m" >/dev/null 2>&1; then
        echo "  BAD   mutant $m was not caught — the gate cannot fail on it"
        fails=$((fails+1))
    else
        echo "  ok    mutant $m caught"
    fi
done

# A control that must stay quiet: an unknown mutant is a usage error (exit 2),
# not a silent pass.
total=$((total+1))
"$WORK/space_check" --mutant nonsense >/dev/null 2>&1
if [ $? -eq 0 ]; then
    echo "  BAD   an unknown mutant exited 0"
    fails=$((fails+1))
else
    echo "  ok    unknown mutant refuses instead of passing"
fi

# The space_view_gate.py legs, wired 2026-11-15. That gate runs in CP4, but its control
# lived only as prose in the gate's own header — and a control nobody executes is a comment,
# which is the exact defect docs/outstanding.md § 2 named. So: the clean gate must pass, and
# the same gate run against a commands.c with the dive wait deleted must go red.
total=$((total+1))
if python3 tests/space_view_gate.py >/dev/null 2>&1; then
    echo "  ok    space_view_gate clean run passes"
else
    echo "  BAD   space_view_gate clean run FAILED — the gate is broken, not the mutant"
    fails=$((fails+1))
fi

total=$((total+1))
grep -v 'seam_wait_ms(ctx, U2_DIVE_STEP_MS);' src/game/commands.c > "$WORK/commands_nowait.c"
# A mutation that fails to mutate is the classic false green, so the deletion is proved
# before the mutant is trusted.
if [ "$(wc -l < "$WORK/commands_nowait.c")" -ge "$(wc -l < src/game/commands.c)" ]; then
    echo "  BAD   the nowait mutant is identical to the source — it proves nothing"
    fails=$((fails+1))
elif U2_DIVE_GATE_SRC="$WORK/commands_nowait.c" python3 tests/space_view_gate.py \
        >/dev/null 2>&1; then
    echo "  BAD   the dive-wait mutant was not caught — the pacing leg cannot fail"
    fails=$((fails+1))
else
    echo "  ok    dive-wait mutant caught by space_view_gate"
fi

total=$((total+1))
grep -v 'space_warp_phase(ctx, ow, 128, 0, 0)' src/game/commands.c > "$WORK/commands_nowarp.c"
if [ "$(wc -l < "$WORK/commands_nowarp.c")" -ge "$(wc -l < src/game/commands.c)" ]; then
    echo "  BAD   the nowarp mutant is identical to the source — it proves nothing"
    fails=$((fails+1))
elif U2_DIVE_GATE_SRC="$WORK/commands_nowarp.c" python3 tests/space_view_gate.py \
        >/dev/null 2>&1; then
    echo "  BAD   a dropped warp phase was not caught — the warp-count leg cannot fail"
    fails=$((fails+1))
else
    echo "  ok    dropped warp phase caught by space_view_gate (counts are bytes)"
fi

# The trail. An unconditional u2_frame_clear is what a renderer does by default, and deleting
# the keep_frame guard looks like a simplification rather than a regression — but it is the
# difference between the warp reading as streaks and reading as a shimmer. So the guard must
# be removable and the gate must notice.
total=$((total+1))
grep -v 'if (!s->keep_frame)' src/game/space_view.c > "$WORK/space_view_noclear.c"
if [ "$(wc -l < "$WORK/space_view_noclear.c")" -ge "$(wc -l < src/game/space_view.c)" ]; then
    echo "  BAD   the noclear mutant is identical to the source — it proves nothing"
    fails=$((fails+1))
elif U2_VIEW_GATE_SRC="$WORK/space_view_noclear.c" python3 tests/space_view_gate.py \
        >/dev/null 2>&1; then
    echo "  BAD   the unconditional-clear mutant was not caught — the trail leg cannot fail"
    fails=$((fails+1))
else
    echo "  ok    unconditional frame clear caught by space_view_gate (mode 0 must accumulate)"
fi

if [ "$fails" -ne 0 ]; then
    echo "SPACE SELFTEST: FAIL ($fails of $total)"
    exit 1
fi
echo "SPACE SELFTEST: PASS ($total controls)"

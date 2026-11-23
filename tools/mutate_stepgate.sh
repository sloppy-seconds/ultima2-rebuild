#!/usr/bin/env bash
# Mutation-test the new asm/2199 step-gate block in ai_check.c.
#
# A test that passes on the first run proves nothing — AGENTS.md: "demonstrate a gate failing
# before you trust it passing". Each mutation breaks one clause of the gate in a COPY of ai.c
# under /var/tmp (the repo file is never touched), rebuilds, and requires ai_check to go red.
# A mutation that leaves the test green means the table is not actually covering that clause.
set -uo pipefail
cd "$(dirname "$0")/../engine"

SRCS=$(ls src/core/*.c src/game/*.c src/debug/*.c | grep -v 'game/ai.c')
# Compiled with the engine's own compiler and flag set, exactly as engine/tests/mutate_speed.sh
# does. This script used to hardcode `cc -std=c11 -Wall -Wextra -Werror -Isrc -Icontent`, a weaker
# and narrower bar than the code under test is held to, and on CI that difference was the whole
# failure: the speed harness (which uses U2_CC/U2_CCFLAGS) caught 9/9 while this one reported every
# mutant as "would not compile". A check program held to a different bar than its subject is the
# defect this project names everywhere except, until now, here.
U2_CCFLAGS="${U2_CCFLAGS:-$(make -s ccflags)}"
U2_CC="${U2_CC:-cc}"
pass=0; fail=0

mutate() {
    local name="$1" from="$2" to="$3"
    cp src/game/ai.c /var/tmp/ai_mutant.c
    python3 - "$from" "$to" <<'PY'
import sys
p='/var/tmp/ai_mutant.c'
s=open(p).read()
old,new=sys.argv[1],sys.argv[2]
if old not in s:
    print("MUTATION-NOT-APPLIED"); sys.exit(2)
open(p,'w').write(s.replace(old,new,1))
PY
    [ $? -ne 0 ] && { echo "  SKIP  $name (pattern not found)"; fail=$((fail+1)); return; }
    if ! "$U2_CC" $U2_CCFLAGS -o /var/tmp/ai_mut \
            tests/ai_check.c /var/tmp/ai_mutant.c $SRCS >/var/tmp/ai_mut.build 2>&1; then
        # A mutant that will not compile tested nothing, and a mutation that tests nothing is
        # a miss, not a skip. The first version returned here without counting it, so a
        # missing content/gen_content.h — which a checkout that lost a tracked file would
        # produce, and where the hardcoded path this script used to cd into does not exist
        # either — made
        # every mutation "skip", the totals came out `caught 0, MISSED 0`, and the harness
        # printed PASS while checking nothing at all.
        echo "  MISSED  $name (mutant would not compile — is engine/build built?)"
        # The compiler's stderr went to /dev/null, so CI said "would not compile" nine times and
        # never said why while the same script passed locally. A gate that cannot name its reason
        # gets rerun until it goes green by accident; the first four lines of the build log are now
        # part of the verdict.
        sed 's/^/          /' /var/tmp/ai_mut.build | head -4
        fail=$((fail+1)); return
    fi
    if /var/tmp/ai_mut >/var/tmp/ai_mut.out 2>&1; then
        echo "  NOT CAUGHT  $name — the table does not cover this clause"
        fail=$((fail+1))
    else
        echo "  caught      $name  ($(grep -c FAIL /var/tmp/ai_mut.out) failing checks)"
        pass=$((pass+1))
    fi
}

echo "mutations the gate table must catch:"
mutate "drop the 0x5C force-field clause" \
       'if (tile != 0x5c && !tile_enterable_by_default(tile)) return 0;' \
       'if (!tile_enterable_by_default(tile)) return 0;'
mutate "water kinds accept anything" \
       'if (tile != 0x00) return 0;' \
       'if (tile == 0xff) return 0;'
mutate "drop the DS:26A freeze" \
       'if (ctx->state.counter_26A != 0) return 0;

    const uint8_t tile' \
       'const uint8_t tile'
mutate "invert the DS:26A freeze" \
       'if (ctx->state.counter_26A != 0) return 0;' \
       'if (ctx->state.counter_26A != 0) return 1;'
mutate "move the 0x78 boundary to 0x79" \
       'if (type < 0x78) {' \
       'if (type < 0x79) {'
mutate "accept the explicit refusals" \
       'if (tile == 0x10 || tile == 0x04 || tile == 0x00) return 0;' \
       'if (tile == 0x10 || tile == 0x04) return 0;'
mutate "drop the 0x70 town-ground accept" \
       'if (tile == 0x70) return 1;' \
       'if (tile == 0x71) return 1;'
mutate "drop the site guard" \
       'if (ctx->state.sector_second != 0 && (nx == 0 || ny == 0)) return 0;
    return 1;' \
       'return 1;'
mutate "guard ignores row 0" \
       '(nx == 0 || ny == 0)) return 0;' \
       '(nx == 0)) return 0;'

echo
echo "caught $pass, MISSED $fail"
# The verdict has to be the exit status, not a word on stdout. Both mutation harnesses used
# to end on `[ $fail -eq 0 ] && echo PASS || echo FAIL`, and `echo` always succeeds: the
# script exited 0 while printing FAIL, so nothing downstream could ever go red on it.
if [ "$fail" -eq 0 ] && [ "$pass" -gt 0 ]; then
    echo "MUTATION TEST: PASS ($pass mutations caught)"
    exit 0
fi
echo "MUTATION TEST: FAIL (caught $pass, missed $fail)"
exit 1

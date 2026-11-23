#!/usr/bin/env bash
# Mutation-test speed_check.c the same way tools/mutate_stepgate.sh does the step gate.
#
# A test whose control is a live dump is only as good as its ability to go red when the
# model drifts from that dump. Each mutation breaks one clause of asm/06ED's arithmetic in
# a COPY of speed.c (the repo file is never touched) and requires speed_check to fail.
# A mutation that leaves it green means the 20-slot table does not actually cover that clause.
set -uo pipefail
cd "$(dirname "$0")/.."
# Compiled with the engine's own flag set; engine/Makefile is the only home for it.
# A check program must never be held to a weaker bar than the code it checks.
U2_CCFLAGS="${U2_CCFLAGS:-$(make -s ccflags)}"
U2_CC="${U2_CC:-cc}"

SRCS=$(ls src/core/*.c src/game/*.c src/debug/*.c | grep -v 'game/speed.c')
pass=0; fail=0

mutate() {
    local name="$1" from="$2" to="$3"
    cp src/game/speed.c /var/tmp/speed_mut.c
    python3 - "$from" "$to" <<'PY'
import sys
p='/var/tmp/speed_mut.c'; s=open(p).read()
old,new=sys.argv[1],sys.argv[2]
if old not in s:
    print("MUTATION-NOT-APPLIED"); sys.exit(2)
open(p,'w').write(s.replace(old,new,1))
PY
    if [ $? -ne 0 ]; then echo "  MISSED  $name (pattern not found)"; fail=$((fail+1)); return; fi
    if ! "$U2_CC" $U2_CCFLAGS -o /var/tmp/speed_mut \
            tests/speed_check.c /var/tmp/speed_mut.c $SRCS >/var/tmp/speed_mut.build 2>&1; then
        # Not a skip: a mutant that will not compile checked nothing. Usual cause is an
        # unbuilt engine — the committed header is content/gen_content.h.
        #
        # The compiler's stderr used to go to /dev/null, so CI reported nine missed mutants with no
        # reason at all while the same script caught 9/9 locally. A gate that cannot say why it is
        # red is a gate that gets rerun until it goes green by accident; the first four lines of the
        # build log are now printed, which is what turns "MISSED" into a diagnosis.
        echo "  MISSED  $name (mutant would not compile — is engine/build built?)"
        sed 's/^/          /' /var/tmp/speed_mut.build | head -4
        fail=$((fail+1)); return
    fi
    if /var/tmp/speed_mut >/var/tmp/speed_mut.out 2>&1; then
        echo "  NOT CAUGHT  $name — the table does not cover this clause"; fail=$((fail+1))
    else
        echo "  caught      $name  ($(grep -c FAIL /var/tmp/speed_mut.out) failing checks)"
        pass=$((pass+1))
    fi
}

echo "mutations the speed table must catch:"
mutate "drop the below-baseline clamp" \
       '        if (ax < baseline) {                 /* jae not taken -> mov $0x0,%ax */
            s->speed_delay[i] = 0;
            continue;
        }' \
       '        /* mutation: clamp removed */'
mutate "truncate MUL to 16 bits" \
       'uint32_t prod = (uint32_t)ax * (uint32_t)s->speed_mult[i];   /* mulw: 32-bit */' \
       'uint32_t prod = (uint32_t)(uint16_t)((uint32_t)ax * (uint32_t)s->speed_mult[i]);'
mutate "off-by-one in the multiplier index" \
       's->speed_mult[i]' 's->speed_mult[(i + 1) % U2_SPEED_SLOTS]'
mutate "divide by the multiplier instead of the baseline" \
       'uint32_t q    = prod / baseline;                             /* divw         */' \
       'uint32_t q    = prod / (s->speed_mult[i] ? s->speed_mult[i] : 1);'
mutate "ceiling one too high" \
       'if (next > U2_SPEED_CEILING)' 'if (next > U2_SPEED_CEILING + 1)'
mutate "floor test uses the wrong bound" \
       'if (next < ctx->state.speed_baseline)' 'if (next < 1)'
mutate "rescale even when the key was rejected" \
       'if (next > U2_SPEED_CEILING)
        return 0;' \
       'if (next > U2_SPEED_CEILING) {
            u2_speed_rescale(ctx);
            return 0;
        }'
mutate "overflow sentinel is not the clamp value" \
       '            q = 0xFFFFu;' '            q = 0xFFFEu;'
mutate "index not halved in the accessor" \
       'return ctx->state.speed_delay[index];' 'return ctx->state.speed_delay[index ? index - 1 : 0];'

echo
echo "caught $pass, MISSED $fail"
# The verdict has to be the exit status, not a word on stdout. Both mutation harnesses used
# to end on `[ $fail -eq 0 ] && echo PASS || echo FAIL`, and `echo` always succeeds: the
# script exited 0 while printing FAIL, so nothing downstream could ever go red on it.
if [ "$fail" -eq 0 ] && [ "$pass" -gt 0 ]; then
    echo "SPEED MUTATION TEST: PASS ($pass mutations caught)"
    exit 0
fi
echo "SPEED MUTATION TEST: FAIL (caught $pass, missed $fail)"
exit 1

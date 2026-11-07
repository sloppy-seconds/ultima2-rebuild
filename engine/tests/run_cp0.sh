#!/usr/bin/env bash
# tests/run_cp0.sh — CP0 acceptance test.
#
#   make && ./u2 --headless --script tests/title.keys --dump-state
#
# prints a screen grid + JSON and exits 0. This script re-checks that claim
# objectively: exit status, grid present, JSON parses, the seam probes ran,
# the run is reproducible, and --trace cites asm addresses.
set -uo pipefail

cd "$(dirname "$0")/.." || exit 1
fail=0
ok()   { printf '  PASS  %s\n' "$1"; }
bad()  { printf '  FAIL  %s\n' "$1"; fail=1; }

# Do not "tidy" the greps below back into `printf '%s\n' "$x" | grep -q ...`.
# This script runs under `set -uo pipefail`, and `grep -q` exits at the first
# match and closes the pipe; the writer then dies of SIGPIPE with 141, pipefail
# reports the pipeline as failed, and the assertion goes red although grep found
# the text. It is a race, so it passes on small payloads and fails on big ones —
# --licenses at 17 KB lost it five times out of five while the same grep on a
# 200-byte payload never did. A herestring has no separate writer process, so
# there is nothing to signal. The same shape was fixed in
# asm_body_guard_selftest.sh and sdl_probe_selftest.sh.
SCRIPT="tests/selftest.keys"
ARGS=(--headless --selftest --script "$SCRIPT" --dump-state)

printf 'engine CP0 test\n'

out1=$(./u2 "${ARGS[@]}")
rc=$?

[ "$rc" = "0" ] && ok "exit status 0" || bad "exit status $rc"

grep -q 'CP0 SKELETON' <<< "$out1" \
    && ok "screen grid rendered (banner row present)" \
    || bad "banner row missing"

grep -q 'rand_byte(5217)' <<< "$out1" \
    && ok "rand_byte seam exercised" || bad "rand_byte probe missing"

grep -q 'speaker_freq(5CD2)' <<< "$out1" \
    && ok "speaker_freq seam exercised" || bad "speaker_freq probe missing"

grep -q '"engine"' <<< "$out1" \
    && ok "state dump emitted" || bad "state dump missing"

printf '%s\n' "$out1" | sed -n '/^{/,$p' | python3 -c '
import json,sys
d=json.load(sys.stdin)
assert d["mode"]=="headless", d["mode"]
assert d["screen"]["cols"]==40 and d["screen"]["rows"]==25, d["screen"]
assert d["platform"]["renderer"]=="null", d["platform"]
assert d["platform"]["input"]=="script", d["platform"]
assert "state" in d and "rng" in d["state"], d
' >/dev/null 2>&1 \
    && ok "JSON parses with the expected schema" \
    || bad "JSON does not parse or schema mismatch"

out2=$(./u2 "${ARGS[@]}")
[ "$out1" = "$out2" ] && ok "headless run is reproducible (two runs identical)" \
                      || bad "headless run differs between runs"

tr=$(mktemp "${TMPDIR:-/var/tmp}/u2-cp0-trace.XXXXXX")
./u2 "${ARGS[@]}" --trace="$tr" >/dev/null
grep -q 'seam=str_out_at asm=51EB' "$tr" && grep -q 'seam=rand_byte asm=5217' "$tr" \
    && ok "--trace logs seam calls with asm citations" \
    || bad "--trace output missing or uncited"
rm -f "$tr"

# The input seam's answer alphabet, driven by a fake backend.
#
# This one cannot be reached by a scripted run: the script backend reports a spent
# run as 0 and the terminal reports EOF as 0, so the only backend that answers -1 is
# SDL, for a window close or a SIGINT. That is how "closing the window on the
# creation screen acts like the Down arrow" survived to be reported from play — no
# capture could produce the third answer. So the contract is pinned here instead,
# with a backend that can say all three.
if cc -std=c11 -Wall -Wextra -Werror -Isrc -Icontent -o "${WORK:-/var/tmp}/keywait_check" \
        tests/keywait_check.c src/core/seam.c src/core/vram.c src/core/state.c \
        src/core/demo.c src/core/world.c src/core/roots.c src/core/slot.c \
        src/core/font.c src/core/frame.c src/core/view.c src/core/rng.c \
        src/core/timer.c src/core/replay.c src/core/sha256.c src/core/version.c \
        src/debug/trace.c src/debug/dump_state.c 2>"${WORK:-/var/tmp}/keywait.build"; then
    if "${WORK:-/var/tmp}/keywait_check" > "${WORK:-/var/tmp}/keywait.out" 2>&1; then
        n=$(sed -n 's/^  ok  \([0-9]\{1,\}\) checks.*/\1/p' "${WORK:-/var/tmp}/keywait.out")
        if [ "${n:-0}" -eq 0 ]; then
            bad "CP0 key-wait run executed 0 checks — the test is not running"
        else
            ok "seam_key_wait: a quit request is never read as a key ($n checks)"
        fi
    else
        bad "seam_key_wait contract violated:"
        sed 's/^/        /' "${WORK:-/var/tmp}/keywait.out"
    fi
else
    bad "key-wait test harness did not build:"
    sed 's/^/        /' "${WORK:-/var/tmp}/keywait.build" | head -5
fi

# The PRNG is a shift register (asm/5217 + asm/522B + asm/5236), seeded once at
# launch from the 0x40:6C byte (asm/0100). Vector derived by hand-decoding those
# three fragments together; it guards regressions and documents the decode.
vec=$(./u2 --rng-sequence 14)
[ "$vec" = "2,3,4,6,10,16,24,35,52,79,120,180,12,144" ] \
    && ok "rand_byte sequence matches the decoded shift register" \
    || bad "rand_byte sequence is $vec"

vec2=$(./u2 --seed 0x20 --rng-sequence 6)
[ "$vec2" = "98,163,228,39,171,178" ] \
    && ok "--seed changes the sequence as the seed model predicts" \
    || bad "--seed 0x20 gave $vec2"

# Resuming the register from a captured state (docs/plan-cross-game-replay.md phase 2).
# asm/0100 can only reach 256 of 2^48 register states, so a replay that has to pick up mid-session
# needs a raw load. The harness carries its own corrupt-register control: a load that silently did
# nothing would still "roll something", and that is the failure this has to catch.
if cc -std=c11 -Wall -Wextra -Werror -Isrc -Icontent -o "${WORK:-/var/tmp}/rng_check" \
        tests/rng_check.c src/core/rng.c src/core/state.c 2>"${WORK:-/var/tmp}/rng.build"; then
    if "${WORK:-/var/tmp}/rng_check" > "${WORK:-/var/tmp}/rng.out" 2>&1; then
        n=$(sed -n 's/^rng_check: \([0-9]\{1,\}\) checks.*/\1/p' "${WORK:-/var/tmp}/rng.out")
        if [ "${n:-0}" -eq 0 ]; then
            bad "CP0 rng resume run executed 0 checks — the test is not running"
        else
            ok "CP0 rng resume: load reproduces the stream, controls fire ($n checks)"
        fi
    else
        bad "CP0 rng resume check failed:"
        sed 's/^/        /' "${WORK:-/var/tmp}/rng.out"
    fi
else
    bad "CP0 rng resume harness did not build:"
    sed 's/^/        /' "${WORK:-/var/tmp}/rng.build"
fi

# --trace-rolls: the per-site roll log the roll-allocation diagnosis depends on.
# It must be non-empty, numbered from 1, and strictly increasing — a trace that silently stopped
# numbering would read exactly like "no rolls happened", which is the failure shape this project
# keeps meeting. The named sites are what make it more than a count.
rt="${WORK:-/var/tmp}/cp0-rolls.txt"
if ./u2 "${ARGS[@]}" --trace-rolls "$rt" >/dev/null 2>&1 && [ -s "$rt" ]; then
    bad=$(awk 'NR != $1 { print NR; exit }' "$rt")
    sites=$(awk '{print $2}' "$rt" | sort -u | wc -l)
    if [ -z "$bad" ] && [ "$sites" -ge 1 ]; then
        ok "--trace-rolls: $(wc -l < "$rt") rolls logged from $sites distinct call sites, numbered from 1"
    else
        bad "--trace-rolls numbering broken at line ${bad:-?} or no sites recorded"
    fi
else
    bad "--trace-rolls produced no output (or the fixture run failed)"
fi

# --rng-state must reject malformed input rather than quietly defaulting: a mis-parsed register makes
# a resumed replay diverge for a reason that looks like an engine bug.
if ./u2 --rng-state 8282ff --rng-sequence 3 >/dev/null 2>&1; then
    bad "--rng-state 8282ff was accepted; a short register must exit non-zero"
else
    ok "--rng-state rejects a short register (exit non-zero, not a silent default)"
fi
if ./u2 --rng-state 8282ffc7cdzz --rng-sequence 3 >/dev/null 2>&1; then
    bad "--rng-state with non-hex was accepted"
else
    ok "--rng-state rejects non-hex input"
fi

base_seq=$(./u2 --rng-sequence 6)
loaded_seq=$(./u2 --rng-state 8282ffc7cda4 --rng-sequence 6)
if [ -n "$loaded_seq" ] && [ "$loaded_seq" != "$base_seq" ]; then
    ok "--rng-state changes the sequence (default $base_seq -> loaded $loaded_seq)"
else
    bad "--rng-state did not change the sequence: base=$base_seq loaded=$loaded_seq"
fi

# The engine must never change the bytes the reconstruction produces.
#
# original/ and references/ are game content: they must be untouched, full stop.
#
# asm/ is different. Per AGENTS.md § 3 the asm tree IS the project's one
# lookup table for address -> behaviour, so discovering a name REQUIRES renaming
# an asm file and editing its header in the same commit. A blanket "asm/ must be
# clean" guard therefore blocks the rule it was meant to protect. What actually
# has to be invariant is the ASSEMBLABLE CONTENT, so that is what is checked
# here: comment lines (leading '#') may change, everything else may not.
#
# The check is deliberately NOT per-file. A per-file md5 plus "no deletions" was
# correct while the only legal asm/ edit was a rename, and it became actively
# wrong the first time function boundaries moved: merging seven fragments into one
# routine deletes six files, and the old guard reported that as vandalism. The
# conserved quantity is the whole tree's body content, ordered by each file's
# declared start address -- which renames, merges and splits all preserve and which
# any body edit breaks. See engine/tests/asm_body_guard.py.
# The end-to-end case is covered independently by tools/assemble_clean.sh, which
# rebuilds the whole binary and compares it against the known md5.
if git rev-parse --git-dir >/dev/null 2>&1; then
    dirty=$(git status --porcelain -- ../original ../references 2>/dev/null)
    [ -z "$dirty" ] && ok "original/ and references/ untouched" \
                   || bad "protected paths dirty: $dirty"

    # The guard compares a tree to a ref. With no base it uses HEAD, which on a clean
    # checkout means comparing HEAD to itself -- a PASS that checked nothing. The guard
    # now exits 2 when it spots that, and CP0 reports it as a SKIP rather than a pass or
    # a false "content changed". The real CI check lives in .github/workflows/
    # asm-checksum.yml, which passes the merge base. Set ASM_GUARD_BASE to do the same
    # here (e.g. before a rebase, ASM_GUARD_BASE=origin/main make -C engine test).
    guard_rc=0
    guard=$(python3 tests/asm_body_guard.py ${ASM_GUARD_BASE:+--base "$ASM_GUARD_BASE"} 2>&1) || guard_rc=$?
    case "$guard_rc" in
        0) ok "$guard" ;;
        2) echo "  SKIP  asm/ body guard: $guard" ;;
        *) bad "asm/ body content changed: $(printf '%s' "$guard" | tr '\n' ' ')" ;;
    esac
fi

# --- renderer selection: a flag that cannot be honoured must refuse ----------
# This is the terminal (cc-only) build, so --renderer=sdl is a request this binary
# cannot serve. It used to be ignored in silence, which made the wrong binary plus
# the right flag look exactly like a window that failed to open -- and that is how
# a player meets it now that the SDL build defaults to the window. Every branch
# here is a control for itself: a wrong exit code fails the gate.
if ./u2 --renderer=sdl --headless --script "$SCRIPT" >/dev/null 2>"${WORK:-/var/tmp}/cp0-renl.err"; then
    bad "--renderer=sdl was accepted by a build with no SDL backend"
else
    grep -q "without an SDL backend" "${WORK:-/var/tmp}/cp0-renl.err" \
        && ok "--renderer=sdl in a non-SDL build refuses and says why" \
        || bad "--renderer=sdl failed, but not with the message a player can act on"
fi

if ./u2 --renderer=bogus >/dev/null 2>&1; then
    bad "--renderer=bogus was accepted"
else
    ok "--renderer rejects a value it does not implement"
fi

### --- --version: a binary has to say what it is -------------------------------
# docs/sdl2-vs-sdl3.md § 7: a dev build and a shipped build can be built against
# different implementations of the same SDL2 API, both run, and nothing else about
# them distinguishes them. Every argument in that document would have been a
# one-line answer with this flag present, so the flag is the thing being gated.
# "unstamped" is checked for by name: if the Makefile ever stops passing the -D
# flags, the fallbacks in core/version.c keep it compiling and the binary quietly
# becomes unidentifiable, which is the silent version of this failure.
if ver=$(./u2 --version 2>&1); then
    case "$ver" in
        *unstamped*) bad "--version says 'unstamped': the Makefile stopped passing the build stamp ($ver)" ;;
        *) ok "--version is stamped (not the unstamped fallback)" ;;
    esac
    grep -q 'build=terminal' <<< "$ver" \
        && ok "--version names the build target" \
        || bad "--version does not name the build target: $ver"
    grep -q 'sdl=none' <<< "$ver" \
        && ok "--version says this cc-only build has no SDL" \
        || bad "--version claims SDL in a build compiled without it: $ver"
    grep -q 'artifact=' <<< "$ver" \
        && ok "--version declares whether it is the shipped artifact" \
        || bad "--version omits the artifact field: $ver"
else
    bad "--version exited non-zero; a binary that cannot identify itself is the defect this flag exists to fix"
fi

### --- --licenses: the attribution record travels inside the binary ------------
# The release is one file with no accompanying documents, so the licence record has
# to be printed by the thing itself. A list that silently lost an entry would look
# exactly like a complete list, which is the only failure mode of this feature worth
# gating for, so the checks are about presence and byte-fidelity rather than style.
if lic=$(./u2 --licenses 2>&1); then
    grep -q "Apache License 2.0" <<< "$lic" \
        && ok "--licenses carries our own Apache-2.0 notice" \
        || bad "--licenses omits the project's own licence"
    grep -q "Sam Lantinga" <<< "$lic" \
        && ok "--licenses names the SDL3 copyright holder" \
        || bad "--licenses omits SDL3"
    grep -q "font8x8" <<< "$lic" \
        && ok "--licenses records the font's provenance" \
        || bad "--licenses omits font8x8"
    grep -q "cleanroom" <<< "$lic" \
        && ok "--licenses records how the substitute art was made" \
        || bad "--licenses omits the tile-art provenance"

    # Byte-fidelity, not a keyword grep: a notice that paraphrases SDL's licence is
    # worse than none, because it reads as compliance. The printed text must equal
    # the committed source file.
    if printf '%s\n' "$lic" | grep -Fq "This notice may not be removed or altered from any source distribution."; then
        ok "--licenses reproduces the zlib clause verbatim"
    else
        bad "--licenses does not reproduce the zlib text verbatim"
    fi

    nsec=$(printf '%s\n' "$lic" | grep -c "^  source in the repository:")
    want=$(python3 -c "import pathlib,sys; sys.path.insert(0,'tools');
import gen_licenses as g; print(len(g.ENTRIES))" 2>/dev/null)
    if [ -n "$want" ] && [ "$nsec" = "$want" ]; then
        ok "--licenses prints all $nsec entries (none silently dropped)"
    else
        bad "--licenses printed $nsec entries, expected ${want:-unknown}"
    fi
else
    bad "--licenses exited non-zero"
fi

# The generated header must still match its sources. Committed output with no build
# rule, same discipline as the content header: a build step that could regenerate
# this on a machine missing an input would ship a shorter list quietly.
if drift=$(python3 tools/gen_licenses.py --verify 2>&1); then
    ok "licenses_data.h matches its committed sources"
else
    bad "licenses_data.h is stale: $drift"
fi

# A key script is read only by the headless input backend. Silently dropping it is
# how a scripted run ends up waiting for a human, so it is refused out loud.
if timeout -s KILL 10 ./u2 --script "$SCRIPT" >/dev/null 2>"${WORK:-/var/tmp}/cp0-script.err"; then
    bad "--script was accepted without --headless, and would have been ignored"
else
    grep -q -- "--script needs --headless" "${WORK:-/var/tmp}/cp0-script.err" \
        && ok "--script without --headless refuses instead of ignoring the keys" \
        || bad "--script without --headless failed without the explanatory message"
fi

[ "$fail" = "0" ] && { echo "CP0 TEST: PASS"; exit 0; } || { echo "CP0 TEST: FAIL"; exit 1; }

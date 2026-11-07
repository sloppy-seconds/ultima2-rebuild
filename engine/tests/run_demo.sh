#!/usr/bin/env bash
# tests/run_demo.sh — the demo record/playback gate. docs/demo-record-playback.md
# § 9, items W1 through W9 plus W13 and W15.
#
# D9 is the reason this file records its own fixture instead of replaying a
# committed one: a demo encodes a playthrough of copyrighted content exactly as a
# save file does, so `demos/` is gitignored and nothing here may depend on a file
# that is not in git. The world the fixture plays in is engine-authored filler from
# tests/demo_seed.py, the same shape cp5_fight_seed.py uses.
#
# Every check is paired with the control that makes it red, because a gate that
# cannot fail where it is enforced is the defect AGENTS.md § 3 says this project has
# shipped three times. Where a control needs the world or the file to be wrong, the
# wrong thing is built (tests/demo_fixture.py) and the run is expected to say so.
set -uo pipefail

cd "$(dirname "$0")/.." || exit 1
fail=0
skips=0
ok()   { printf '  PASS  %s\n' "$1"; }
bad()  { printf '  FAIL  %s\n' "$1"; fail=1; }
skip() { printf '  SKIP  %s\n' "$1"; skips=$((skips + 1)); }

WORK=$(mktemp -d "${TMPDIR:-/var/tmp}/u2-demo.XXXXXX")
trap 'rm -rf "$WORK"' EXIT
U2=./u2
FIX="python3 tests/demo_fixture.py"

printf 'engine demo record/playback test\n'
[ -x "$U2" ] || { bad "engine not built: run make first"; exit 1; }

# --- W1: the framing, in one process, with its own controls ------------------
if cc -std=c11 -Wall -Wextra -Werror -Isrc -Icontent -o "$WORK/demo_check" \
       tests/demo_check.c src/core/demo.c src/core/state.c src/core/vram.c \
       src/core/font.c src/core/frame.c src/core/view.c src/core/rng.c \
       src/core/timer.c src/core/sha256.c src/core/version.c src/core/replay.c \
       src/core/world.c src/core/roots.c src/core/slot.c \
       src/game/monsters.c src/debug/dump_state.c src/debug/trace.c 2>"$WORK/cc.err"; then
    cout=$("$WORK/demo_check" 2>&1)
    n=$(printf '%s\n' "$cout" | sed -n 's/^ *\([0-9]\{1,\}\) checks.*/\1/p')
    if printf '%s\n' "$cout" | grep -q "NOT OK"; then
        bad "W1: demo_check reported a failure: $(printf '%s\n' "$cout" | grep 'NOT OK' | head -2)"
    elif [ "${n:-0}" -eq 0 ]; then
        bad "W1: demo_check executed 0 checks — the test is not running"
    else
        ok "W1: framing round-trip, digest scope and their controls ($n checks)"
    fi
else
    bad "W1: demo_check did not build: $(head -3 "$WORK/cc.err")"
fi

# --- the fixture --------------------------------------------------------------
# One seeded world per recording, so a run that writes a sector overlay cannot
# change what a later run reads.
seed() { python3 tests/demo_seed.py "$1" >/dev/null 2>&1; }

printf '0x12\nE\n0xf1\n0xf1\nE\n0x12\n' > "$WORK/site.keys"
# Input the game must never record: WAIT is "no key this poll", the seam answers 0,
# and nothing is delivered. (0x13 is deliberately NOT in here: with no pixels behind
# it the screenshot hook is NULL and the byte reaches the game as the control byte it
# is, which seam.c documents. The swallowed-byte case belongs to a renderer that has
# one.)
printf '0x12\nWAIT\nE\n0xf1\nWAIT\n0xf1\nE\n0x12\n' > "$WORK/noise.keys"
# Idle turns. --auto-pass-polls is what lets a headless run record a kind=auto
# record at all; without it the idle arm is skipped in headless by design.
printf '0x12\nWAIT\nWAIT\nWAIT\nWAIT\n0x12\n' > "$WORK/idle.keys"
# The same session with no toggle at all, and the toggle with no session.
printf 'E\n0xf1\n0xf1\nE\n' > "$WORK/notoggle.keys"
printf '0x12\nE\n0xf1\n0xf1\nE\n' > "$WORK/lone.keys"

record() {   # record NAME WORLD SCRIPT [extra args...]
    local name="$1" world="$2" script="$3"; shift 3
    mkdir -p "$WORK/d_$name"
    "$U2" --headless --play-map --cleanroom \
         --data-dir "$world/content" --save-dir "$world/save" \
         --script "$script" --demo-dir "$WORK/d_$name" --demo-name "$name" \
         "$@" > "$WORK/$name.out" 2> "$WORK/$name.err"
}
replay() {   # replay WORLD NAME [extra args...] -> sets RC and OUT
    local world="$1" name="$2"; shift 2
    RC=0
    OUT=$("$U2" --headless --cleanroom --data-dir "$world/content" \
               --playback "$WORK/d_$name/$name.demo" "$@" 2>&1) || RC=$?
}

seed "$WORK/w";  record site  "$WORK/w" "$WORK/site.keys"
seed "$WORK/wn"; record noise "$WORK/wn" "$WORK/noise.keys"
seed "$WORK/wi"; record idle  "$WORK/wi" "$WORK/idle.keys" --auto-pass-polls 2
DEMO="$WORK/d_site/site.demo"

# --- W2: the toggle -----------------------------------------------------------
[ -s "$DEMO" ] && ok "W2: Ctrl-R in a scripted run writes a demo file" \
               || bad "W2: no demo file was written"

seed "$WORK/wnt"; mkdir -p "$WORK/d_nt"
"$U2" --headless --play-map --cleanroom --data-dir "$WORK/wnt/content" \
      --save-dir "$WORK/wnt/save" --script "$WORK/notoggle.keys" \
      --demo-dir "$WORK/d_nt" --demo-name nt > "$WORK/nt.out" 2>&1
[ -z "$(ls -A "$WORK/d_nt" 2>/dev/null)" ] && ok "W2 control: no toggle, no file" \
                                           || bad "W2 control: a run with no Ctrl-R wrote a demo"
grep -qi "ILLEGAL" "$WORK/nt.out" && bad "W2 control: 0x12 reached the command dispatcher" \
                                   || ok "W2 control: 0x12 is swallowed at the seam"

seed "$WORK/wlone"
"$U2" --headless --play-map --cleanroom --data-dir "$WORK/wlone/content" \
      --save-dir "$WORK/wlone/save" --script "$WORK/lone.keys" > "$WORK/lone.out" 2>&1
grep -qi "ILLEGAL" "$WORK/lone.out" && bad "W2 control: a lone 0x12 was an illegal command" \
                                     || ok "W2 control: a lone 0x12 displaces no shortcut"

# The way INTO play goes through title-flow waits, so a Ctrl-R pressed on the way in
# defers with a message rather than refusing outright. The completion of that deferral
# is asserted in demo_check, where the boundary sequence can be driven exactly; here
# the shell leg covers the two things a scripted run can show honestly: the deferral
# note, and that the cancel it promises works.
seed "$WORK/wtitle"
printf 'P\n0x12\n0xf1\n0xf1\n0x12\n' > "$WORK/via_title.keys"
"$U2" --headless --cleanroom --data-dir "$WORK/wtitle/content" \
      --save-dir "$WORK/wtitle/save" --script "$WORK/via_title.keys" \
      --demo-dir "$WORK/d_title" --demo-name title > "$WORK/title.out" 2>&1
grep -q "will start when you reach" "$WORK/title.out" \
    && ok "W2: a Ctrl-R on the way into play defers with a message, not a refusal" \
    || bad "W2: the title toggle refused instead of deferring"
printf 'P\n0x12\n0x12\n0xf1\n' > "$WORK/cancel.keys"
rm -rf "$WORK/d_cancel"; mkdir -p "$WORK/d_cancel"
"$U2" --headless --cleanroom --data-dir "$WORK/wtitle/content" \
      --save-dir "$WORK/wtitle/save" --script "$WORK/cancel.keys" \
      --demo-dir "$WORK/d_cancel" --demo-name c > "$WORK/cancel.out" 2>&1
if [ -z "$(ls -A "$WORK/d_cancel" 2>/dev/null)" ] && grep -q "recording cancelled" "$WORK/cancel.out"; then
    ok "W2 control: a second Ctrl-R before the world cancels, and writes nothing"
else
    bad "W2 control: the cancel the message promises does not happen"
fi

# --- W7: the round trip, and the control that makes it red --------------------
replay "$WORK/w" site
[ "$RC" -eq 0 ] && printf '%s\n' "$OUT" | grep -q "DEMO PLAYED" \
    && ok "W7: a recorded session replays with every digest matching" \
    || bad "W7: playback failed (exit $RC): $(printf '%s\n' "$OUT" | tail -2)"

$FIX digest "$DEMO" "$WORK/mut.demo" 2
RC=0
OUT=$("$U2" --headless --cleanroom --data-dir "$WORK/w/content" \
           --playback "$WORK/mut.demo" 2>&1) || RC=$?
[ "$RC" -ne 0 ] && printf '%s\n' "$OUT" | grep -q "DIVERGED at record 2" \
    && ok "W7 control: a mutated digest fails the run and names the record" \
    || bad "W7 control: a mutated digest did not go red (exit $RC)"

# And the framing digest, unchecked, would let a plain edit pass as a divergence.
sed 's/^\(@rec 2 .*\)sha=./\1X/' "$DEMO" > "$WORK/edit.demo"
RC=0
"$U2" --headless --cleanroom --data-dir "$WORK/w/content" --playback "$WORK/edit.demo" \
      >/dev/null 2>&1 || RC=$?
[ "$RC" -ne 0 ] && ok "hazard 4: an edited file is refused as corrupt, not played as divergent" \
                || bad "hazard 4: an edited demo file was accepted"

# --- W3: the record list is exactly what the game consumed ---------------------
"$U2" --headless --play-map --cleanroom --data-dir "$WORK/wn/content" \
      --save-dir "$WORK/wn/save" --script "$WORK/noise.keys" --trace="$WORK/noise.trace" \
      --demo-dir "$WORK/d_noise2" --demo-name noise >/dev/null 2>&1
if python3 tools/demo.py against-trace "$WORK/d_noise2/noise.demo" \
       "$WORK/noise.trace" > "$WORK/w3.out" 2>&1; then
    ok "W3: $(tail -1 "$WORK/w3.out")"
else
    bad "W3: $(tail -2 "$WORK/w3.out" | head -1)"
fi

# --- W4: the checkpoint is the whole mirror ------------------------------------
span=$(grep '^span ' "$DEMO" | cut -d' ' -f2)
[ "$span" = "1616" ] && ok "W4: the checkpoint span is 0x650, not --dump-ds's 0x280" \
                     || bad "W4: span is $span, expected 1616"
# The control: a 0x280 checkpoint. The creature arrays live at DS:137..0x236, inside
# that shorter span, so a loader that accepted it would still look like it worked
# while the speed and tick fields sat at whatever the process happened to hold.
$FIX short "$DEMO" "$WORK/short.demo"
RC=0
"$U2" --headless --salvage --cleanroom --data-dir "$WORK/w/content" \
      --playback "$WORK/short.demo" >/dev/null 2>"$WORK/short.err" || RC=$?
[ "$RC" -ne 0 ] && grep -q "checkpoint is 640 bytes" "$WORK/short.err" \
    && ok "W4 control: a 0x280 checkpoint is refused by name, not half-applied" \
    || bad "W4 control: a short checkpoint was not refused as short (exit $RC)"

# --- W5: the idle auto-PASS is a record, and playback does not invent one -------
grep -q "kind=auto" "$WORK/d_idle/idle.demo" \
    && ok "W5: an idle turn is recorded as kind=auto" \
    || bad "W5: no kind=auto record in a run that idled"
replay "$WORK/wi" idle
[ "$RC" -eq 0 ] && ok "W5: the auto turns replay" || bad "W5: a demo with auto turns did not replay (exit $RC)"
# The control: strip them. Playback then performs no PASS where the recording did.
$FIX strip "$WORK/d_idle/idle.demo" "$WORK/noauto.demo" auto
RC=0
"$U2" --headless --cleanroom --data-dir "$WORK/wi/content" --playback "$WORK/noauto.demo" \
      >/dev/null 2>&1 || RC=$?
[ "$RC" -ne 0 ] && ok "W5 control: a demo with its auto turns removed goes red" \
                 || bad "W5 control: dropping the auto records changed nothing"

# --- W6: playback cannot touch the player's saves ------------------------------
seed "$WORK/w6"
record w6 "$WORK/w6" "$WORK/site.keys"
before=$(find "$WORK/w6/save" -type f -exec sha256sum {} \; | sort)
"$U2" --headless --cleanroom --data-dir "$WORK/w6/content" --save-dir "$WORK/w6/save" \
      --playback "$WORK/d_w6/w6.demo" >/dev/null 2>&1
after=$(find "$WORK/w6/save" -type f -exec sha256sum {} \; | sort)
[ "$before" = "$after" ] && ok "W6: playback left the player's save directory byte-identical" \
                         || bad "W6: playback wrote into the player's save directory"
# The control: aim the scratch at the player's own directory and the same run must
# dirty it, which is what proves the redirect is doing work rather than being inert.
"$U2" --headless --cleanroom --data-dir "$WORK/w6/content" \
      --demo-dir "$WORK/w6/save" --playback "$WORK/d_w6/w6.demo" >/dev/null 2>&1
dirty=$(find "$WORK/w6/save" -type f -exec sha256sum {} \; | sort)
[ "$dirty" != "$after" ] && ok "W6 control: with the redirect aimed at the saves directory it dirties" \
                         || bad "W6 control: the redirect is a no-op, so nothing was proven"

# --- W8: the renderer cannot reach state ---------------------------------------
replay "$WORK/w" site --renderer=terminal
[ "$RC" -eq 0 ] && printf '%s\n' "$OUT" | grep -q "DEMO PLAYED" \
    && ok "W8: the same demo verifies under the terminal renderer as under the null one" \
    || bad "W8: the terminal renderer disagreed (exit $RC): $(printf '%s\n' "$OUT" | tail -1)"
# The real thing, not a stand-in: the static-SDL3 player binary, driven through its
# own event loop with no --headless, so the SDL renderer, the host clock and the
# 16 ms seam waits are all in the path. That is the configuration W8 is about, and
# the terminal leg above is a weaker version of it rather than a substitute.
# Build it with deploy/linuxbuild/build.sh (a podman container with static SDL3);
# the artifact is gitignored, so this leg skips out loud where it has not been built.
SDLBIN=""
for cand in u2-sdl-static build-sdl/u2-sdl; do
    [ -x "$cand" ] || continue
    # The leading ./ is not decoration: a path with no slash is looked up in $PATH
    # when it is run, so `[ -x ]` passes and the invocation then silently fails. The
    # first version of this leg reported "does not report sdl3" for exactly that reason.
    case "$cand" in */*) SDLBIN="$cand" ;; *) SDLBIN="./$cand" ;; esac
    break
done
if [ -n "$SDLBIN" ]; then
    # Assert the artifact is what it claims before trusting its verdict: a binary that
    # silently fell back to a non-SDL build would pass this gate while proving nothing.
    if "$SDLBIN" --version 2>&1 | grep -q "sdl3"; then
        RC=0
        SDL_VIDEODRIVER=dummy timeout 120 "$SDLBIN" --cleanroom \
            --data-dir "$WORK/w/content" --playback "$DEMO" >/dev/null 2>&1 || RC=$?
        [ "$RC" -eq 0 ] && ok "W8: the same demo verifies under SDL3 ($SDLBIN, dummy driver, full event loop)" \
                        || bad "W8: SDL playback disagreed (exit $RC)"
    else
        bad "W8: $SDLBIN does not report sdl3 — refusing to count it as the SDL leg"
    fi
else
    skip "W8 SDL leg: no SDL build found (build it with deploy/linuxbuild/build.sh). The terminal leg above ran."
fi

# --- W9: truncation and salvage -------------------------------------------------
$FIX noend "$DEMO" "$WORK/trunc.demo"
RC=0
"$U2" --headless --cleanroom --data-dir "$WORK/w/content" --playback "$WORK/trunc.demo" \
      >/dev/null 2>"$WORK/trunc.err" || RC=$?
[ "$RC" -ne 0 ] && grep -q "no @end" "$WORK/trunc.err" && grep -q "records" "$WORK/trunc.err" \
    && ok "W9: a file with no @end is refused, and says how many records it holds" \
    || bad "W9: a truncated file was not refused (exit $RC)"
RC=0
"$U2" --headless --salvage --cleanroom --data-dir "$WORK/w/content" \
      --playback "$WORK/trunc.demo" >/dev/null 2>&1 || RC=$?
[ "$RC" -eq 0 ] && ok "W9: --salvage plays the records that are present" \
                 || bad "W9: --salvage refused a salvageable file (exit $RC)"

# --- W13: one demo crosses modes and still verifies -----------------------------
nrec=$(grep -c '^@rec' "$DEMO")
[ "$nrec" -ge 4 ] && ok "W13: one demo carries the overworld, a site entry and the walk back out ($nrec records)" \
                  || bad "W13: the crossing demo has only $nrec records"
# The control: cut it at the mode boundary. Playback must fail rather than stop
# quietly at the last record it recognises.
$FIX cut "$DEMO" "$WORK/cut.demo" 3
RC=0
"$U2" --headless --cleanroom --data-dir "$WORK/w/content" --playback "$WORK/cut.demo" \
      >/dev/null 2>&1 || RC=$?
[ "$RC" -ne 0 ] && ok "W13 control: a demo cut at the mode boundary fails instead of stopping quietly" \
                 || bad "W13 control: the cut demo replayed clean, so the boundary is not checked"

# --- W15: pacing is applied, and is presentation only ---------------------------
t0=$(date +%s%N); replay "$WORK/w" site;                 t1=$(date +%s%N)
fast=$(( (t1 - t0) / 1000000 )); unpaced_rc=$RC
t2=$(date +%s%N); replay "$WORK/w" site --playback-pace; t3=$(date +%s%N)
slow=$(( (t3 - t2) / 1000000 ))
[ "$slow" -gt "$fast" ] && ok "W15: --playback-pace slows a headless run (${fast}ms unpaced, ${slow}ms paced)" \
                         || bad "W15: pacing changed nothing (${fast}ms vs ${slow}ms) — it is a no-op"
[ "$RC" -eq 0 ] && [ "$unpaced_rc" -eq 0 ] \
    && ok "W15: the paced run reached the same verdict, so pacing did not reach state" \
    || bad "W15: the paced and unpaced runs disagreed"
# W15's acceptance is "a demo takes visibly longer under SDL than headless", and now
# that the SDL artifact can be built that is measured rather than approximated: SDL
# playback paces with no flag at all, because pacing lives in the mode's clock and the
# SDL mode has the host one.
if [ -n "$SDLBIN" ]; then
    s0=$(date +%s%N)
    SDL_VIDEODRIVER=dummy timeout 120 "$SDLBIN" --cleanroom \
        --data-dir "$WORK/w/content" --playback "$DEMO" >/dev/null 2>&1
    s1=$(date +%s%N)
    sdlms=$(( (s1 - s0) / 1000000 ))
    [ "$sdlms" -gt "$fast" ] && ok "W15: SDL playback paces by default (${fast}ms headless, ${sdlms}ms under SDL)" \
                             || bad "W15: SDL playback did not pace (${fast}ms vs ${sdlms}ms)"
fi

# --- the refusal that keeps a demo honest about the world -----------------------
# A demo whose sector files differ from the recording's must refuse and name the
# file, because those bytes reach the world. Tile art must not: that is § 4's
# correction, and refusing on it would make demos unshareable for no gain.
seed "$WORK/wm"
printf '\xff' | dd of="$WORK/wm/content/mapx70" bs=1 seek=10 count=1 conv=notrunc status=none
RC=0
"$U2" --headless --cleanroom --data-dir "$WORK/wm/content" --playback "$DEMO" \
      >/dev/null 2>"$WORK/wm.err" || RC=$?
[ "$RC" -ne 0 ] && grep -q "mapx70" "$WORK/wm.err" \
    && ok "content: a sector file that differs from the recording's is refused by name" \
    || bad "content: a mutated mapx70 was not refused (exit $RC)"

# The override that keeps a demo shareable when the content cannot be reproduced:
# same mutated mapx70, refused by default, played when a human says they know.
RC=0
OUT=$("$U2" --headless --cleanroom --playback-ignore-content \
           --data-dir "$WORK/wm/content" --playback "$DEMO" 2>&1) || RC=$?
# The verdict here is success, and that is the point of the leg rather than an
# accident: the mutated cell is nowhere on the recorded path, so the digests still
# agree. What is being asserted is that the override (a) named the disagreement on the
# way in and (b) was not a refusal — exit 2 is the refusal code, and the same file
# without the flag gets it in the leg above.
if printf '%s\n' "$OUT" | grep -q "playback-ignore-content" && [ "$RC" -ne 2 ]; then
    ok "content: --playback-ignore-content plays past the refusal and says what it cost"
else
    bad "content: the override was still a refusal (exit $RC)"
fi

# --- W10: the tool, each subcommand, each with a known-bad input ----------------
python3 tools/demo.py inspect "$DEMO" > "$WORK/inspect.out" 2>&1 \
    && grep -q "records         4" "$WORK/inspect.out" \
    && ok "W10: demo.py inspect reads the header and counts the records" \
    || bad "W10: demo.py inspect failed on a good file"

$FIX rawdigest "$DEMO" "$WORK/raw.demo" 2
RC=0
python3 tools/demo.py inspect "$WORK/raw.demo" >"$WORK/inspect2.out" 2>&1 || RC=$?
[ "$RC" -ne 0 ] && grep -q "corrupt" "$WORK/inspect2.out" \
    && ok "W10 control: inspect refuses a file whose framing digest disagrees" \
    || bad "W10 control: inspect reported a corrupt file as fine"

$FIX cut "$DEMO" "$WORK/cut2.demo" 2
RC=0
python3 tools/demo.py diff "$DEMO" --against "$WORK/cut2.demo" >"$WORK/diff.out" 2>&1 || RC=$?
[ "$RC" -ne 0 ] && grep -q "present in one file only" "$WORK/diff.out" \
    && ok "W10: diff reports where two demos stop agreeing" \
    || bad "W10: diff did not report the truncation: $(tail -1 "$WORK/diff.out")"

python3 tools/demo.py export-keys "$DEMO" --keys "$WORK/x.keys" --ds "$WORK/x.ds" \
       >/dev/null 2>&1 \
    && [ "$(grep -cv '^#' "$WORK/x.keys")" = "4" ] && [ "$(wc -c < "$WORK/x.ds")" = "3232" ] \
    && ok "W10: export-keys writes the key script and the checkpoint" \
    || bad "W10: export-keys wrote the wrong shape"

# The conversion control: a playlog report whose key count disagrees with its own
# steps must be refused, not converted into a script that looks derived from evidence.
printf '{"keys": ["A", "B"], "steps": [{"at_key": 9}], "steps_truncated": false}\n' \
    > "$WORK/badrep.json"
RC=0
python3 tools/demo.py from-playlog "$WORK/badrep.json" --keys "$WORK/should.not.exist" \
    >"$WORK/fp.out" 2>&1 || RC=$?
[ "$RC" -ne 0 ] && [ ! -e "$WORK/should.not.exist" ] \
    && ok "W10 control: a report whose key count disagrees is refused" \
    || bad "W10 control: a bad playlog report was converted anyway"

# --- W14: cross-version playback is a downgrade, not a refusal ------------------
# D13: an old demo still plays, because the stimulus does not depend on the engine
# version; what is impossible is verifying it. The control is a file that claims a
# different digest scope, which is exactly what a demo written by another build looks
# like to this one.
$FIX digestver "$DEMO" "$WORK/oldver.demo" 2
RC=0
OUT=$("$U2" --headless --cleanroom --data-dir "$WORK/w/content" \
           --playback "$WORK/oldver.demo" 2>&1) || RC=$?
if [ "$RC" = "6" ] && printf '%s\n' "$OUT" | grep -q "UNVERIFIED"; then
    ok "W14: a demo from another digest scope plays unverified, with its own exit code"
else
    bad "W14: a scope mismatch did not downgrade cleanly (exit $RC)"
fi
mkdir -p "$WORK/corpus" && cp "$DEMO" "$WORK/oldver.demo" "$WORK/corpus/"
RC=0
python3 tools/demo.py corpus "$WORK/corpus" --engine ./u2 --data-dir "$WORK/w/content" \
    --engine-arg=--cleanroom >"$WORK/corpus.out" 2>&1 || RC=$?
if grep -q "unverifiable" "$WORK/corpus.out" && grep -q "2 demos" "$WORK/corpus.out"; then
    ok "W14: demo.py corpus reports match, divergence and unverifiable per file"
else
    bad "W14: corpus did not classify the two files (exit $RC): $(tail -2 "$WORK/corpus.out")"
fi
RC=0
python3 tools/demo.py corpus "$WORK" --engine ./u2 >/dev/null 2>&1 || RC=$?
[ "$RC" -ne 0 ] && ok "W14 control: corpus refuses a directory with no demos instead of passing" \
                 || bad "W14 control: corpus reported success on an empty corpus"

# --- W18: the @world delta is canonical, and the base is the content layer ------
# § 13.3's format, gated. The recorder's world is DIRTY at the slot and clean at
# content, which is the only shape that produces a delta at all: a clean-install
# recording emits delta=none and would pass every assertion below by carrying nothing.
# The fixture writes its cells in descending file order, so an unordered writer emits a
# different string than the canonical one and the exact match below catches it. That is
# the control, and it is why the expected string is spelled out rather than compared
# against a second run of the same code.
seed "$WORK/wd"
EXPECT=$(python3 tests/demo_delta_fixture.py "$WORK/wd")
if [ -z "$EXPECT" ]; then
    bad "W18: the dirty-slot fixture did not run"
else
    record dirty  "$WORK/wd" "$WORK/site.keys"
    record dirty2 "$WORK/wd" "$WORK/site.keys"
    GOT=$(grep '^@world mapx70' "$WORK/d_dirty/dirty.demo" | sed 's/.*delta=//')
    if [ "$GOT" = "$EXPECT" ]; then
        ok "W18: the delta is canonical and names exactly the edited cells"
    else
        bad "W18: delta was '$GOT', expected '$EXPECT'"
    fi

    # base must be the CONTENT digest, not the overlay's. That is the whole reason
    # § 13.1's refusal became satisfiable: two people with the same release pack match
    # on base, and the delta is the only thing that has to travel.
    CB=$(grep '^@world mapx70' "$WORK/d_dirty/dirty.demo" | sed 's/.*base=\([0-9a-f]*\).*/\1/')
    OB=$(grep '^overlay-slot mapx70' "$WORK/d_dirty/dirty.demo" | awk '{print $3}')
    if [ -n "$CB" ] && [ -n "$OB" ] && [ "$CB" != "$OB" ]; then
        ok "W18: base is the content digest, and differs from the overlay's"
    else
        bad "W18: base did not distinguish content from the overlay (base=$CB overlay=$OB)"
    fi

    if [ "$(grep '^@world' "$WORK/d_dirty/dirty.demo")" = \
         "$(grep '^@world' "$WORK/d_dirty2/dirty2.demo")" ]; then
        ok "W18: two recordings of one edit emit byte-identical @world lines"
    else
        bad "W18: two recordings of the same edit differ, so the delta is not canonical"
    fi
fi

# --- W16: the delta is APPLIED at load, not merely parsed ----------------------
# W18 gates the writer, and that turned out not to be enough. The reader was storing a
# fixed 23-character slice of the whole line as the sector name, so no sector ever
# matched its own delta and every one was silently read as absent -- and W18 stayed
# green throughout, because writing a correct line and reading it back are two
# contracts. The failure was invisible by construction: "this sector has no delta" is
# the normal case on a clean install, so the broken path looked exactly like the common
# one. These legs replay a DIRTY recording into a CLEAN world, the only arrangement
# where a delta has to do work for the run to be right.
seed "$WORK/wc"
REC_SHA=$(python3 tests/demo_delta_fixture.py "$WORK/wd" slot-sha)
RC=0
OUT=$("$U2" --headless --cleanroom --data-dir "$WORK/wc/content" \
           --save-dir "$WORK/wc/save" --playback "$WORK/d_dirty/dirty.demo" 2>&1) || RC=$?
if printf '%s\n' "$OUT" | grep -q "rebuilt from the demo.s recorded delta" \
   && printf '%s\n' "$OUT" | grep -q "sector now $REC_SHA"; then
    ok "W16: the rebuilt sector hashes to the recorder's own bytes, not just to a parsed delta"
else
    bad "W16: the delta did not land (exit $RC, expected sector $REC_SHA): $(printf '%s\n' "$OUT" | tail -2)"
fi
[ "$RC" -ne 2 ] && ok "W16: replaying a dirty recording into a clean world is not a refusal" \
                || bad "W16: it refused: $(printf '%s\n' "$OUT" | tail -2)"

# Control: the base. A viewer holding a different pack must be told the demo cannot
# rebuild their sector. This is the § 13.1 refusal, and it is now satisfiable: the
# delta describes the recording's bytes, and nothing else.
# The byte is written OUTSIDE the delta's cells, so only the base can catch it.
printf '\xfe' | dd of="$WORK/wc/content/mapx70" bs=1 seek=20 count=1 conv=notrunc status=none
RC=0
"$U2" --headless --cleanroom --data-dir "$WORK/wc/content" --save-dir "$WORK/wc/save" \
      --playback "$WORK/d_dirty/dirty.demo" >/dev/null 2>"$WORK/wc.err" || RC=$?
if [ "$RC" -eq 2 ] && grep -q 'not the one the recording was made against' "$WORK/wc.err"; then
    ok "W16 control: a base that is not the recording's is refused, and names both digests"
else
    bad "W16 control: a mismatched base was not refused (exit $RC): $(tail -2 "$WORK/wc.err")"
fi

# And the override must SKIP the delta rather than apply it to foreign bytes. If the
# base check were decorative, the rebuild line would still appear here.
RC=0
OUT=$("$U2" --headless --cleanroom --playback-ignore-content --data-dir "$WORK/wc/content" \
           --save-dir "$WORK/wc/save" --playback "$WORK/d_dirty/dirty.demo" 2>&1) || RC=$?
if [ "$RC" -ne 2 ] && ! printf '%s\n' "$OUT" | grep -q 'rebuilt from' \
   && printf '%s\n' "$OUT" | grep -q 'NOT applied'; then
    ok "W16 control: with the override the delta is skipped out loud, not applied blind"
else
    bad "W16 control: the override applied or skipped the delta silently (exit $RC)"
fi

# --- W19: the version range, both edges ----------------------------------------
# § 13.5 rules cross-version playback a supported use case, and the bump from 1 to 2
# was the moment that mattered: the reader's check used to be equality, which would have
# made every existing file unplayable. The fixture repairs the @end framing digest, so a
# refusal here is the version check and not the corruption check.
python3 tests/demo_fixture.py version "$DEMO" "$WORK/v1.demo" 1
RC=0
"$U2" --headless --cleanroom --data-dir "$WORK/w/content" --save-dir "$WORK/w/save" \
      --playback "$WORK/v1.demo" >/dev/null 2>&1
[ "$RC" -eq 0 ] && ok "W19: a v1 file plays under the v2 reader, so the bump is not destructive" \
                || bad "W19: a v1 file was refused (exit $RC) — the version range is broken"

python3 tests/demo_fixture.py version "$DEMO" "$WORK/v99.demo" 99
RC=0
"$U2" --headless --cleanroom --data-dir "$WORK/w/content" --save-dir "$WORK/w/save" \
      --playback "$WORK/v99.demo" >/dev/null 2>&1 || RC=$?
[ "$RC" -ne 0 ] && ok "W19 control: a file claiming a version past this build is refused" \
                || bad "W19 control: udemo 99 was accepted, so nothing bounds the range"

echo
if [ "$fail" -ne 0 ]; then
    echo "DEMO TEST: FAIL"
    exit 1
fi
[ "$skips" -ne 0 ] && echo "DEMO TEST: PASS ($skips leg(s) skipped, named above)" || echo "DEMO TEST: PASS"

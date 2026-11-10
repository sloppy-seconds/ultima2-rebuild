#!/usr/bin/env bash
# tests/run_cp5.sh — CP5 combat acceptance SPEC (tickets C1, C2, C6).
#
# src/game/combat.c exists and implements C1/C2/C6, so the behavioural run is the
# main event. The SKIP path is kept for the case where combat.c is removed again
# (a revert, a branch bisect) so the spec can still land ahead of the code. This
# runner does three things:
#
#   1. Always syntax-check tests/combat_check.c, so the specification is proven
#      to be well-formed C and its API contract is pinned. This runs in CI.
#   2. Always run the two fixture controls, so the spec is proven to discriminate
#      whether or not combat.c is present.
#   3. Run the behavioural spec against src/game/combat.c. If that file is
#      absent, each ticket is reported SKIP with the exact missing hook named and
#      the suite stays green (a skipped spec must not turn the gate red).
#
# The spec (combat_check.c) is written to DISCRIMINATE: a boolean-immunity
# resist, a wrong roll threshold, a fixed (non-random) counter, or a theft that
# flags rather than deletes each fail at least one assertion. See the file's
# header for the byte citations behind every expectation.
#
# That discrimination is PROVEN, not asserted. Two fixture models ship with the
# spec: tests/combat_model_faithful.c (must pass) and
# tests/combat_model_boolean_wrong.c (must fail). They run on every `make test`,
# whether or not combat.c exists, so the spec cannot quietly go soft. The original
# overnight proof used throwaway models in /var/tmp that were gone by morning and
# could not be re-run — see AGENTS.md § 5 on a control nobody can re-run.
set -uo pipefail

cd "$(dirname "$0")/.." || exit 1
fail=0
ok()   { printf '  PASS  %s\n' "$1"; }
bad()  { printf '  FAIL  %s\n' "$1"; fail=1; }
skip() { printf '  SKIP  %s\n' "$1"; }

WORK=$(mktemp -d "${TMPDIR:-/var/tmp}/u2-cp5.XXXXXX")
trap 'rm -rf "$WORK"' EXIT

# Every harness linked here shares combat_check.c's main, so all three print the
# same summary line: "combat_check: N checks, M failures". That line is the only
# trustworthy check count: the CHECK macro prints on FAILURE only, so counting
# per-check output is structurally impossible (an earlier version grepped for
# '  ok  ' and reported 0 forever). Empty output means no summary line at all.
summary_checks() { sed -n 's/^combat_check: \([0-9]\{1,\}\) checks, [0-9]\{1,\} failures$/\1/p' "$1" | tail -1; }

printf 'engine CP5 spec\n'

# --- 0. the strings this spec asserts on must actually exist -----------------
# Every message below is compared as text, and `strstr(line, "")` always matches:
# an empty literal does not make an assertion fail, it makes it pass while nothing
# is printed. That is not hypothetical — CI has no original/ultimaii.exe, the
# extractor's `run` locators resolved to "" there, and the first death-check run in
# CI failed only because the row-22 assertion happened to be strict enough to
# notice. So the dependency is now stated where it can be checked.
EMPTY_STRINGS=""
for sym in U2_S_IS_DEAD U2_S_KILLED U2_S_EXP_PLUS U2_S_HIT U2_S_MISS \
           U2_S_LEGS_PARALYZED U2_S_ARMS_PARALYZED U2_S_SLEEP_SPELL \
           U2_S_MAGIC_MISSILE U2_S_THIEF_STOLE U2_S_SITE_TOWN U2_S_ENTER \
           U2_S_PASS; do
    if grep -q "^#define ${sym} \"\"" content/gen_content.h 2>/dev/null; then
        EMPTY_STRINGS="${EMPTY_STRINGS} ${sym}"
    fi
done
if [ -n "$EMPTY_STRINGS" ]; then
    bad "CP5 generated strings are empty — the message assertions below would pass vacuously:${EMPTY_STRINGS}"
else
    ok "CP5 generated strings are non-empty (no assertion can pass on an empty literal)"
fi

# --- 1. the spec is well-formed C (always runs, guards the contract) --------
cc -std=c11 -Wall -Wextra -Werror -Wshadow -Wstrict-prototypes -Wmissing-prototypes \
   -Isrc -fsyntax-only tests/combat_check.c 2>"$WORK/syn.err"
if [ $? -ne 0 ]; then
    bad "CP5 spec did not syntax-check:"
    sed 's/^/        /' "$WORK/syn.err"
else
    ok "CP5 spec syntax-checks clean (C1/C2/C6 contract is well-formed)"
fi

# --- 1b. the controls, always: this is what proves the spec has teeth --------
# A spec that nothing ever implements can pass while being unable to detect the
# very mistake it was written to prevent. So the spec is run against a faithful
# reference model (must be clean) and a deliberately wrong boolean-immunity model
# (must fail) on every run, before combat.c exists at all.
run_model() {
    local m="$1" want="$2"
    if ! cc -std=c11 -Wall -Wextra -Werror -Isrc -Icontent -o "$WORK/$m" \
            tests/combat_check.c "tests/$m.c" src/game/kill_drop.c \
            src/core/rng.c src/core/state.c \
            2>"$WORK/$m.build"; then
        bad "CP5 control $m did not build:"
        sed 's/^/        /' "$WORK/$m.build"
        return
    fi
    local rc=0
    "$WORK/$m" > "$WORK/$m.log" 2>&1 || rc=$?
    local summary; summary=$(tail -1 "$WORK/$m.log")
    # Exit code alone cannot tell "every check passed" from "no check ran", and a
    # silent main() would report the faithful control as a pass. Guard the count
    # before the verdict — same class of defect as the old unconditional `exit 0`
    # and the always-zero grep below (AGENTS.md: a gate that cannot go red).
    local n; n=$(summary_checks "$WORK/$m.log")
    if [ "${n:-0}" -eq 0 ]; then
        bad "CP5 control $m executed 0 checks — the spec is not running (last line: ${summary:-<no output>})"
        return
    fi
    if [ "$want" = "pass" ] && [ "$rc" -ne 0 ]; then
        bad "CP5 POSITIVE control failed — the spec itself is wrong, not the future impl ($summary)"
        sed 's/^/        /' "$WORK/$m.log" | head -20
    elif [ "$want" = "fail" ] && [ "$rc" -eq 0 ]; then
        bad "CP5 NEGATIVE control PASSED — the spec has gone soft and cannot catch boolean immunity"
    elif [ "$want" = "pass" ]; then
        ok "CP5 positive control (faithful model): $summary"
    else
        ok "CP5 negative control (boolean-immunity model): $summary"
    fi
}

run_model combat_model_faithful pass
run_model combat_model_boolean_wrong fail

# --- 1c. self-test: prove the zero-check guard can fire ---------------------
# The guards below go red when a run reports 0 checks. That is only worth
# anything if the parser can tell "0 checks" from "no summary line", so build a
# deliberately empty run and check the parse lands on a literal 0. If this ever
# prints FAIL, the guard is broken and every PASS above is unproven.
if [ -x "$WORK/combat_model_faithful" ]; then
    CP5_SPEC_NO_TESTS=1 "$WORK/combat_model_faithful" > "$WORK/zero.log" 2>&1
    z=$(summary_checks "$WORK/zero.log")
    if [ "$z" = "0" ]; then
        ok "CP5 zero-check self-test: guard reads a real 0 ($(tail -1 "$WORK/zero.log"))"
    elif [ -z "$z" ]; then
        bad "CP5 zero-check self-test: summary line did not parse — the 0-check guard cannot be trusted"
    else
        bad "CP5 zero-check self-test: expected 0 checks from the empty run, got '$z'"
    fi
else
    bad "CP5 zero-check self-test: faithful control binary missing, guard unproven"
fi

# --- 1d. the creature table (CP5 W1) ----------------------------------------
# Independent of combat.c: this is the monx -> DS mapping everything else in CP5
# reads from. Runs whether or not combat exists, and takes the real monx31 when
# the player's content is present, because that is the only input that can
# falsify the offset table rather than check the copy.
if cc -std=c11 -Wall -Wextra -Werror -Isrc -Icontent -o "$WORK/monsters_check" \
        tests/monsters_check.c src/game/monsters.c src/core/world.c src/core/roots.c src/core/slot.c src/core/state.c \
        src/core/save.c src/core/rng.c 2>"$WORK/mon.build"; then
    MONREAL=""
    [ -f ../original/monx31 ] && MONREAL="../original/monx31"
    if "$WORK/monsters_check" "$WORK" $MONREAL > "$WORK/mon.out" 2>&1; then
        n=$(sed -n 's/^monsters_check: \([0-9]\{1,\}\) checks.*/\1/p' "$WORK/mon.out")
        if [ "${n:-0}" -eq 0 ]; then
            bad "CP5 creature-table run executed 0 checks — the spec is not running"
        else
            ok "CP5 W1 creature table: monx -> DS mapping ($n checks, $(tail -1 "$WORK/mon.out"))"
        fi
    else
        bad "CP5 creature-table check failed:"
        sed 's/^/        /' "$WORK/mon.out"
    fi
else
    bad "CP5 creature-table harness did not build:"
    sed 's/^/        /' "$WORK/mon.build"
fi

# --- 1f. the wandering spawn (docs/monster-spawn.md 15.4) -------------------
# The selector table was pinned by forcing DS:60 in a live emulator; this is the
# gate that keeps the engine's copy of it honest, plus the spawner's refusals —
# which matter more than its successes, because a wrong refusal rate is a wrong
# monster density and nothing else would notice.
if cc -std=c11 -Wall -Wextra -Werror -Isrc -Icontent -o "$WORK/wander_check" \
        tests/wander_check.c src/game/monsters.c src/core/world.c src/core/roots.c src/core/slot.c src/core/state.c \
        src/core/save.c src/core/rng.c 2>"$WORK/wander.build"; then
    if "$WORK/wander_check" > "$WORK/wander.out" 2>&1; then
        n=$(sed -n 's/^wander_check: \([0-9]\{1,\}\) checks.*/\1/p' "$WORK/wander.out")
        if [ "${n:-0}" -eq 0 ]; then
            bad "CP5 wander run executed 0 checks — the test is not running"
        else
            ok "CP5 wandering spawn: selector table and spawner refusals ($n checks)"
        fi
    else
        bad "CP5 wandering spawn check failed:"
        sed 's/^/        /' "$WORK/wander.out"
    fi
else
    bad "CP5 wandering spawn harness did not build:"
    sed 's/^/        /' "$WORK/wander.build"
fi

# --- 1e. the attack (CP5 W3) ------------------------------------------------
# Direct test of combat.c, not part of combat_check.c: the attack is implemented,
# so it is tested against the real thing rather than specified against fixtures.
if cc -std=c11 -Wall -Wextra -Werror -Isrc -Icontent -o "$WORK/attack_check" \
        tests/attack_check.c src/game/combat.c src/game/kill_drop.c src/game/monsters.c \
        src/core/rng.c src/core/state.c 2>"$WORK/atk.build"; then
    if "$WORK/attack_check" > "$WORK/atk.out" 2>&1; then
        n=$(sed -n 's/^attack_check: \([0-9]\{1,\}\) checks.*/\1/p' "$WORK/atk.out")
        if [ "${n:-0}" -eq 0 ]; then
            bad "CP5 attack run executed 0 checks — the test is not running"
        else
            ok "CP5 W3 attack: hit roll, damage, kill, BCD rewards ($n checks, $(tail -1 "$WORK/atk.out"))"
        fi
    else
        bad "CP5 attack check failed:"
        sed 's/^/        /' "$WORK/atk.out"
    fi
else
    bad "CP5 attack harness did not build:"
    sed 's/^/        /' "$WORK/atk.build"
fi

# --- 1f. the death tail (CP5 W8) -------------------------------------------
# asm/0C62..0C99 as one span, checked against the captured frame in
# captures/spice86/cp5-weak-fight/death1.png. It links the whole game core rather
# than one file because the tail prints through the seam and repaints the status
# band, and a stub of either would let the test pass on a half-implemented death.
if cc -std=c11 -Wall -Wextra -Werror -Isrc -Icontent -o "$WORK/death_check" \
        tests/death_check.c src/core/*.c src/game/*.c src/debug/*.c 2>"$WORK/death.build"; then
    if "$WORK/death_check" "$WORK/death-save" > "$WORK/death.out" 2>&1; then
        n=$(sed -n 's/^death_check: \([0-9]\{1,\}\) checks.*/\1/p' "$WORK/death.out")
        if [ "${n:-0}" -eq 0 ]; then
            bad "CP5 death run executed 0 checks — the test is not running"
        else
            ok "CP5 W8 death: zero DS:51..59, repaint, '<NAME> IS DEAD!' ($n checks, $(tail -1 "$WORK/death.out"))"
        fi
    else
        bad "CP5 death check failed:"
        sed 's/^/        /' "$WORK/death.out"
    fi
else
    bad "CP5 death harness did not build:"
    sed 's/^/        /' "$WORK/death.build"
fi

# --- 1h. the shipped menu path reloads the last save on death -----------------
# "press any key on death exits the game, i want it to load the last save game". The
# entry a player actually uses is the menu's 'P', and that branch only runs when the
# renderer can present pixels -- so until --script was allowed on the SDL renderer there
# was no scripted route to it at all and this ruling shipped with nothing behind it but
# a unit test of the wait. SDL_VIDEODRIVER=dummy makes it a real, headless,
# reproducible run: the frame composes and uploads and the keys come from a file.
#
# Four leading keys because the opening presentation eats one per screen (four for the
# seed's story_flag: splash, the best-seller card, the title card, the poem) and the
# count is not the interesting part; if it is wrong the world never runs and the death
# count below goes red, which is the point of asserting a count rather than a line. It
# has to be the cleanroom art because the seed writes no executable, and the engine
# refuses to enter a world it cannot draw -- which is the right refusal, and was the
# first thing this gate went red for.
SDL_BIN=""
if   [ -x ./u2-sdl-static ]; then SDL_BIN=./u2-sdl-static
elif [ -x ./u2-sdl ];        then SDL_BIN=./u2-sdl
fi
if [ -z "$SDL_BIN" ]; then
    printf '  SKIP  no SDL build present, so the menu path has no scripted route (build with make sdl-static)\n'
elif ! python3 tests/cp5_fight_seed.py "$WORK/deathsdl" 2>"$WORK/deathsdl.seed.err"; then
    bad "CP5 menu-reload gate: the synthetic content did not build:"
    sed 's/^/        /' "$WORK/deathsdl.seed.err"
else
    python3 ../tools/u2_edit.py set "$WORK/deathsdl/save/PLAYER/PLAYER" food 0 \
        2>"$WORK/deathsdl.food.err" \
    || bad "CP5 menu-reload gate: could not zero the seed's food:"
    printf 'x\nx\nx\nx\nP\n0xf0\nq\n0xf0\nq\n' > "$WORK/deathsdl.keys"
    SDL_VIDEODRIVER=dummy timeout -s KILL 60 "$SDL_BIN" --renderer=sdl --cleanroom \
         --data-dir "$WORK/deathsdl/content" --save-dir "$WORK/deathsdl/save" \
         --script "$WORK/deathsdl.keys" --trace="$WORK/deathsdl.trace" \
         --dump-each > "$WORK/deathsdl.out" 2>&1
    nd=$(grep -c 'IS DEAD' "$WORK/deathsdl.trace")
    np=$(grep -c 'PRESS ANY KEY' "$WORK/deathsdl.trace")
    if [ "$nd" -ne 2 ] || [ "$np" -ne 2 ]; then
        bad "CP5 menu-reload gate: expected 2 deaths and 2 prompts (the world must run, die, reload, run and die again), got $nd and $np"
        tail -14 "$WORK/deathsdl.out" | sed 's/^/        /'
    else
        ok "CP5 menu reload: died, reloaded and died again in one session ($nd deaths, $np prompts)"
    fi
    # The restore, not just the death: the band has to read the record's HP again AFTER
    # the announcement. A reload that printed the prompt and stayed dead would pass the
    # counts above.
    first_dead=$(grep -n 'IS DEAD' "$WORK/deathsdl.trace" | head -1 | cut -d: -f1)
    if [ -n "$first_dead" ] && tail -n "+$first_dead" "$WORK/deathsdl.trace" \
         | grep -q 'text=H.P.=9997'; then
        ok "CP5 menu reload: H.P.=9997 is repainted after the death, so the record came back off the disk"
    else
        bad "CP5 menu reload: the band never came back to the record's HP after the death"
    fi
    case "$(cat "$WORK/deathsdl.out")" in
      *"CMD: S ANY KEY)"*)
          bad "CP5 menu reload: the reloaded world drew its prompt on top of (PRESS ANY KEY)" ;;
      *)
          ok "CP5 menu reload: no corpse text survives into the new world" ;;
    esac
fi

# --- 1i. the development entry stays observable after death -------------------
# The shipped entries reload the last save when a death unwinds the loop (title.c's 'P'
# loop, game.c's enhanced-mode handoff). --play-map must NOT, and this is the gate that
# keeps it that way: CP4 G2's four arms and tests/swamp_damage.py's death case assert
# on the state a run leaves behind, so a reload on this entry would replace a corpse
# with a live character and turn three gates green while they proved nothing.
#
# The seed's character carries food 999999; zeroing it makes the FIRST step underflow,
# which is the cheapest death a scripted run can produce.
if ! python3 tests/cp5_fight_seed.py "$WORK/death" 2>"$WORK/death.seed.err"; then
    bad "CP5 dev-entry death gate: the synthetic content did not build:"
    sed 's/^/        /' "$WORK/death.seed.err"
else
    REC="$WORK/death/save/PLAYER/PLAYER"
    if ! python3 ../tools/u2_edit.py set "$REC" food 0 2>"$WORK/death.food.err"; then
        bad "CP5 dev-entry death gate: could not zero the seed's food:"
        sed 's/^/        /' "$WORK/death.food.err"
    else
        printf '0xf0\nq\n' > "$WORK/death.keys"
        ./u2 --headless --play-map --cleanroom \
             --data-dir "$WORK/death/content" --save-dir "$WORK/death/save" \
             --script "$WORK/death.keys" --dump-state > "$WORK/death.reload.out" 2>&1
        case "$(cat "$WORK/death.reload.out")" in
          *"IS DEAD!"*)
              ok "CP5 dev entry: the death is still what the run leaves behind (no reload on --play-map)" ;;
          *)
              bad "CP5 dev entry: the run did not end dead — a reload here would blind CP4 G2 and swamp_damage.py"
              tail -12 "$WORK/death.reload.out" | sed 's/^/        /' ;;
        esac
        case "$(cat "$WORK/death.reload.out")" in
          *"H.P.=0000"*)
              ok "CP5 dev entry: the band still reads H.P.=0000 at exit, so the state is the corpse's" ;;
          *)
              bad "CP5 dev entry: the final state is not the dead one:"
              tail -12 "$WORK/death.reload.out" | sed 's/^/        /' ;;
        esac
    fi
fi

# --- 1g. the creature turn (CP5 W6/W6b) -------------------------------------
# asm/0A6D..0AB5 and asm/2122..2260. Links the whole game core for the same
# reason the death check does: the turn prints, rolls, and moves bytes that the
# renderer and the passability test both read.
if cc -std=c11 -Wall -Wextra -Werror -Isrc -Icontent -o "$WORK/ai_check" \
        tests/ai_check.c src/core/*.c src/game/*.c src/debug/*.c 2>"$WORK/ai.build"; then
    if "$WORK/ai_check" > "$WORK/ai.out" 2>&1; then
        n=$(sed -n 's/^ai_check: \([0-9]\{1,\}\) checks.*/\1/p' "$WORK/ai.out")
        if [ "${n:-0}" -eq 0 ]; then
            bad "CP5 creature-turn run executed 0 checks — the test is not running"
        else
            ok "CP5 W6/W6b creature turn: pursue, flee, species gate, specials ($n checks, $(tail -1 "$WORK/ai.out"))"
        fi
    else
        bad "CP5 creature-turn check failed:"
        sed 's/^/        /' "$WORK/ai.out"
    fi
else
    bad "CP5 creature-turn harness did not build:"
    sed 's/^/        /' "$WORK/ai.build"
fi

# --- 1g2. the speed model (asm/06ED rescale, F7/F8) ---------------------------
# The control is a live memory dump, not a hand-written expectation: captures/spice86/
# g30-clockcal/ gives DS:74, DS:554 and the DS:558 multiplier table, and the engine must
# reproduce the DS:580 slots the original had actually stored. Model only -- nothing in
# the engine calls it yet, because what each of the 20 slots gates is still unknown.
if cc -std=c11 -Wall -Wextra -Werror -Isrc -Icontent -o "$WORK/speed_check" \
        tests/speed_check.c src/core/*.c src/game/*.c src/debug/*.c 2>"$WORK/speed.build"; then
    if "$WORK/speed_check" > "$WORK/speed.out" 2>&1; then
        n=$(sed -n 's/^speed_check: \([0-9]\{1,\}\) checks.*/\1/p' "$WORK/speed.out")
        if [ "${n:-0}" -eq 0 ]; then
            bad "CP5 speed run executed 0 checks — the test is not running"
        else
            ok "CP5 speed model: rescale reproduces the live dump, F7/F8 bounds ($n checks, $(tail -1 "$WORK/speed.out"))"
        fi
    else
        bad "CP5 speed check failed:"
        sed 's/^/        /' "$WORK/speed.out"
    fi
else
    bad "CP5 speed harness did not build:"
    sed 's/^/        /' "$WORK/speed.build"
fi

# --- 1h. the world-map site gate (towns) ------------------------------------
# asm/3389_cmd_E's dispatch. The table is checked on its own and, when the player's
# content is present, against every gate cell in every world sector: each must name
# a sector file that exists. Without a content dir the data half SKIPs and the
# table half still runs.
if cc -std=c11 -Wall -Wextra -Werror -Isrc -Icontent -o "$WORK/site_check" \
        tests/site_check.c src/core/*.c src/game/*.c src/debug/*.c 2>"$WORK/site.build"; then
    SITEDATA=""
    for d in ../original ../../original; do
        [ -f "$d/mapx00" ] && SITEDATA="$d" && break
    done
    if "$WORK/site_check" $SITEDATA > "$WORK/site.out" 2>&1; then
        n=$(sed -n 's/^site_check: \([0-9]\{1,\}\) checks.*/\1/p' "$WORK/site.out")
        if [ "${n:-0}" -eq 0 ]; then
            bad "CP5 site-gate run executed 0 checks — the test is not running"
        else
            ok "CP5 site gate: tile -> sector digit, and every gate cell resolves ($n checks, $(tail -1 "$WORK/site.out"))"
        fi
    else
        bad "CP5 site-gate check failed:"
        sed 's/^/        /' "$WORK/site.out"
    fi
else
    bad "CP5 site-gate harness did not build:"
    sed 's/^/        /' "$WORK/site.build"
fi

# --- 2. the behavioural run, only when the combat hook exists ---------------
if [ ! -f src/game/combat.c ]; then
    skip "C1 three independent counters DS:26B/26C/26D (0..15) — needs src/game/combat.c"
    skip "C2 ~75% resist roll on a live item slot (DS:D9/DA/E4) — needs src/game/combat.c"
    skip "C6 theft deletes from the item array, nothing restored — needs src/game/combat.c"
    # Must honour $fail. This branch used to `exit 0` unconditionally, which
    # swallowed every earlier failure in the script — a syntax-check break or a
    # control regression printed FAIL and still reported PASS to CI. A gate that
    # cannot go red is worse than no gate.
    printf '\n'
    if [ "$fail" -eq 0 ]; then
        printf 'CP5 SPEC: PASS (spec landed; combat not implemented — by design)\n'
    else
        printf 'CP5 SPEC: FAIL (controls or spec contract broken; behavioural run skipped)\n'
    fi
    exit "$fail"
fi

cc -std=c11 -Wall -Wextra -Werror -Isrc -Icontent -o "$WORK/combat_check" \
   tests/combat_check.c src/game/combat.c src/game/kill_drop.c src/core/rng.c src/core/state.c \
   2>"$WORK/cc.err"
if [ $? -ne 0 ]; then
    bad "CP5 combat harness did not build (combat.c present but does not link):"
    sed 's/^/        /' "$WORK/cc.err"
elif "$WORK/combat_check" > "$WORK/combat.out" 2>&1; then
    # The count is parsed from the summary line and asserted non-zero. A run that
    # executed zero checks used to print "0 checks" and still report PASS, which
    # is indistinguishable from the old cosmetic quirk — the failure mode this
    # line exists to catch.
    n=$(summary_checks "$WORK/combat.out")
    if [ "${n:-0}" -eq 0 ]; then
        bad "CP5 behavioural run executed 0 checks — the spec is not running (last line: $(tail -1 "$WORK/combat.out"))"
    else
        ok "CP5 spec: C1/C2/C6 behaviour ($n checks, $(tail -1 "$WORK/combat.out"))"
    fi
else
    bad "CP5 spec failed:"
    sed 's/^/        /' "$WORK/combat.out"
fi

# --- 3. W9: the fight gate, end to end, with no game files ------------------
# The owner's bar for CP5 is "a rendered fight in the game", which no unit test
# above actually proves: they test combat.c, not the command loop that prints into
# the CMD window. This drives ./u2 the way a player is driven — a key script into
# the real input path, the screen read back — against a synthetic content directory
# built by cp5_fight_seed.py. Cleanroom tiles and engine-authored mapx/monx/PLAYER
# bytes, so it runs in CI where the game files are not present.
# A SKIP here would be a lie, because nothing it needs is optional.
if [ ! -x ./u2 ]; then
    bad "CP5 fight gate: ./u2 is not built — run make before make test"
elif ! python3 tests/cp5_fight_seed.py "$WORK/w9" 2>"$WORK/w9.seed.err"; then
    bad "CP5 fight gate: the synthetic content did not build:"
    sed 's/^/        /' "$WORK/w9.seed.err"
else
    # The save is seeded with agility 0x80, which asm/2E3D makes a guaranteed hit
    # (the roll is rand>>1, so it maxes at 127), and EXP/GOLD at 9999 so the kill
    # reward has to wrap. Both halves are asserted: the line and the wrap.
    printf 'A\n0xf3\n' > "$WORK/w9.keys"
    ./u2 --headless --play-map --cleanroom \
         --data-dir "$WORK/w9/content" --save-dir "$WORK/w9/save" \
         --script "$WORK/w9.keys" --dump-state > "$WORK/w9.out" 2>&1
    W9ALL=$(cat "$WORK/w9.out")   # the whole dump: the CMD lines and the status band
    case "$W9ALL" in
      *"CMD: ATTACK--EAST--HIT!!!"*) ok "CP5 W9: the swing renders as ATTACK--EAST--HIT!!! in the CMD window" ;;
      *) bad "CP5 W9: no HIT line. Screen was:"; printf '%s\n' "$W9ALL" | sed 's/^/        /' ;;
    esac
    case "$W9ALL" in
      *"CMD: KILLED--GOLD+"*"--EXP.+"*) ok "CP5 W9: the kill renders as KILLED--GOLD+n--EXP.+m" ;;
      *) bad "CP5 W9: no KILLED line with both rewards — the creature survived or the reward path is silent" ;;
    esac
    # The BCD wrap the kickoff listed as evidence item 9: 9999 + a reward is not a
    # clamp and not a 16-bit overflow. GOLD and EXP are separate lines, so each is
    # matched on its own rather than with one pattern that could borrow the other's.
    case "$W9ALL" in
      *"EXP.=000"*) ok "CP5 W9: EXP wrapped past 9999 instead of clamping (EXP.=000n)" ;;
      *) bad "CP5 W9: EXP did not wrap — expected EXP.=000n after a kill at 9999" ;;
    esac
    case "$W9ALL" in
      *"GOLD=000"*) ok "CP5 W9: GOLD wrapped past 9999 on the same kill (GOLD=000n)" ;;
      *) bad "CP5 W9: GOLD did not wrap — expected GOLD=000n after a kill at 9999" ;;
    esac

    # Control: with no creature in the sector the same two keys must print MISS and
    # must not print a kill. Without this, a gate that always claimed a hit would
    # look identical above.
    : > "$WORK/w9/content/monx00"
    printf 'A\n0xf3\n' > "$WORK/w9.keys"
    ./u2 --headless --play-map --cleanroom \
         --data-dir "$WORK/w9/content" --save-dir "$WORK/w9/save" \
         --script "$WORK/w9.keys" --dump-state > "$WORK/w9ctl.out" 2>&1
    if grep -q "ATTACK--EAST--MISS" "$WORK/w9ctl.out" \
       && ! grep -q "KILLED--" "$WORK/w9ctl.out"; then
        ok "CP5 W9 control: an empty sector gives MISS and no reward line"
    else
        bad "CP5 W9 control failed: the fight gate would pass with nothing to fight"
        sed 's/^/        /' "$WORK/w9ctl.out"
    fi

    # --- and the fight has to be a fight: the creature must be able to answer ----
    # A plain melee type (0x30, no special) at role 2 — alerted, so it walks in and
    # swings and casts nothing. Six passes and the seeded H.P.=9997 must be lower.
    # This is the only CI proof that the melee chain reaches the screen rather than
    # stopping inside ai.c, which is exactly where it would silently not work.
    python3 tests/cp5_fight_seed.py "$WORK/w9m" >/dev/null 2>&1
    python3 - "$WORK/w9m/content/monx00" <<'PYEOF'
import sys
p = sys.argv[1]
d = bytearray(open(p, 'rb').read())
d[64 + 0] = 40      # hp: too much for the scripted swing to end it in one hit
d[96 + 0] = 0x30    # type: Fighter — a plain melee attacker, no special at all
d[160 + 0] = 2      # role: alerted, so it closes and swings
open(p, 'wb').write(bytes(d))
PYEOF
    for i in 1 2 3 4 5 6; do printf 'SP\n'; done > "$WORK/w9m.keys"
    ./u2 --headless --play-map --cleanroom \
         --data-dir "$WORK/w9m/content" --save-dir "$WORK/w9m/save" \
         --script "$WORK/w9m.keys" --dump-state > "$WORK/w9m.out" 2>&1
    # The seed is H.P.=9997, so "fell below the seed" is exactly "does not read
    # 9997". A pattern is the wrong tool here and the first draft had one.
    HP_LINE=$(grep -oE "H\.P\.=[0-9]{4}" "$WORK/w9m.out" | tail -1)
    if [ -n "$HP_LINE" ] && [ "$HP_LINE" != "H.P.=9997" ]; then
        ok "CP5 W9: a plain melee creature wounds the player — H.P. reads ${HP_LINE#H.P.=}, not the seeded 9997"
    else
        bad "CP5 W9: the melee chain did not reach the screen — H.P. is still 9997 after six turns"
        grep -E "H\.P\.|CMD" "$WORK/w9m.out" | sed 's/^/        /'
    fi
fi

# --- C3: legs paralysis is a movement rejection ----------------------------
# asm/2520 sits inside the passability routine: if DS:26B is set it prints
# "--PARALIZED!" and takes the same reject return an impassable tile uses. The
# counter is reached the only way it is reached in play — a Daemon (type 0x34,
# dispatched by asm/0AD3) casting it during the fight — so no debug seam and no
# cheat flag are needed to gate this.
#
# The control matters more than the case. The same script against an Orc (type
# 0x30, no special at all) must NOT produce the word; without that half this gate
# could not fail, which is the failure mode this project has been bitten by twice.
python3 tests/cp5_fight_seed.py "$WORK/c3" >/dev/null 2>&1
for kind in daemon orc; do
    python3 - "$WORK/c3/content/monx00" "$kind" <<'PYEOF'
import sys
p, kind = sys.argv[1], sys.argv[2]
d = bytearray(open(p, 'rb').read())
d[64 + 0]  = 200                       # hp: it has to survive the whole script
d[96 + 0]  = 0x34 if kind == 'daemon' else 0x30
d[128 + 0] = 0x08
d[160 + 0] = 1                         # role 1: the AI pass that casts specials
open(p, 'wb').write(bytes(d))
PYEOF
    # Interleaved, and many attempts. asm/0C33 ticks once per turn, so a paralysis of at most 15
    # clears within 15 passes -- the old "16 passes then one step" let it expire unseen and the
    # word never printed, which is why this test passed before and failed after the fix. A west
    # attempt has to land inside the short window where the counter is still non-zero, so the
    # attempt count is what makes it observable, not idle time. 0xf2 is U2_KEY_LEFT (platform.h);
    # the script input takes hex tokens verbatim.
    { for i in $(seq 1 60); do printf 'SP\n0xf2\n'; done; } > "$WORK/c3-$kind.keys"
    ./u2 --headless --play-map --cleanroom          --data-dir "$WORK/c3/content" --save-dir "$WORK/c3/save"          --script "$WORK/c3-$kind.keys" --dump-each > "$WORK/c3-$kind.out" 2>&1
done

if grep -q -- "--PARALIZED!" "$WORK/c3-daemon.out"; then
    if grep -q -- "--PARALIZED!" "$WORK/c3-orc.out"; then
        bad "CP5 C3: the control Orc also printed --PARALIZED! — the gate cannot fail"
    else
        ok "CP5 C3: a Daemon's leg paralysis rejects the next step with --PARALIZED!, and a plain melee Orc does not"
    fi
else
    bad "CP5 C3: the Daemon fight never produced --PARALIZED! — the special or the reject is not wired"
    sed 's/^/        /' "$WORK/c3-daemon.out" | tail -6
fi

# C3b: paralysis must EXPIRE, and it must expire on the open world.
# The tick at asm/0C33 is reached by BOTH arms of asm/0C1F: the site branch `jne L_000e33`
# jumps OVER the world-map block and LANDS ON the tick, it does not gate it. An earlier engine
# reading took it as a gate and wrapped the decrement in `if (sector_second != 0)`, which made a
# 0..15 turn paralysis permanent on any sector whose ones digit is 0 -- including sector 00,
# which is exactly this fixture. The owner found it by playing, not by reading.
# Re-apply the Daemon patch: the loop above left the content holding an Orc.
python3 - "$WORK/c3/content/monx00" <<'PYEOF'
import sys
d = bytearray(open(sys.argv[1], 'rb').read())
d[64 + 0] = 200; d[96 + 0] = 0x34; d[128 + 0] = 0x08; d[160 + 0] = 1
open(sys.argv[1], 'wb').write(bytes(d))
PYEOF
{ for i in $(seq 1 60); do printf 'SP\n0xf2\n'; done; } > "$WORK/c3-expire.keys"
./u2 --headless --play-map --cleanroom \
     --data-dir "$WORK/c3/content" --save-dir "$WORK/c3/save" \
     --script "$WORK/c3-expire.keys" --dump-each > "$WORK/c3-expire.out" 2>&1
par=$(grep -c -- "--PARALIZED!" "$WORK/c3-expire.out")
# WHY THERE WAS NO AUTOMATED DECAY GATE HERE, and what replaced it. An earlier version of this
# block asserted "refusals must be a minority of attempts" as the decay test. It was run against a
# build with the tick deleted entirely and it still PASSED -- 2 of 24 refused either way -- because
# a successful west step walks the player away from the Daemon, so neither build reaches a terminal
# state and the counts cannot separate them. A gate that cannot fail is the failure mode
# AGENTS.md § 3 and § 5 both call out, so the assertion was removed rather than kept for comfort,
# and the gap was named instead: a rig that sets the counter directly. That rig now exists as
# CP5 C3e below, so this block's only job is what it can honestly prove -- that the special
# reaches play at all (the count below), with C3's plain-Orc half as the control that it is not
# just noise.
if [ "$par" -gt 0 ]; then
    ok "CP5 C3b: leg paralysis is applied on the open world (sector 00, $par of 60 attempts refused)"
else
    bad "CP5 C3b: the Daemon never paralyzed on the open world -- the special is not reaching play"
    sed 's/^/        /' "$WORK/c3-expire.out" | tail -5
fi
echo "  NOTE  CP5 C3b: decay is gated separately -- see CP5 C3e below (deterministic rig)"

# --- C3e: the decay itself, on a rig that can actually fail -------------------------------
#
# C3b could not gate decay: its refusal count was measured against a Daemon, and a successful
# west step walks the player away from it, so the assertion passed even against a build with the
# tick deleted. tests/paralysis_decay.py replaces that with a rig that sets DS:26B directly from
# a captured DS mirror (--dump-ds, patch, --load-ds) and presses LEFT. No monster, nothing to
# walk away from, nothing random. Two runs: a short one proving the paralysis applies and each
# refusal gives a count back, and a long one proving the counter reaches zero and the remaining
# presses land as steps.
#
# The control was run: with status_tick() removed from the paralysis reject the gate reports
# "still 15 means the reject path does not tick" and "x=2, want 61" -- the owner's stranding,
# reproduced mechanically.
if U2_BIN=./u2 python3 tests/paralysis_decay.py > "$WORK/c3e.log" 2>&1; then
    ok "CP5 C3e: leg paralysis decays on the open world under refused steps (rig: --load-ds)"
else
    sed 's/^/        /' "$WORK/c3e.log" | tail -8
    bad "CP5 C3e: paralysis does not expire when the player cannot move -- the stranding bug"
fi

# --- C3c: a refused step is a completed command, so the world takes its turn ----
#
# asm/0A02 (the --INVALID MOVE! reject) falls through to asm/0A15_invalid_move_to_cmd_done,
# which jumps to asm/0A30 and thence into asm/0A33_cmd_done and asm/0A38_post_command_tail.
# So bumping into a wall runs the creature sweep and the wandering-spawn attempt. The engine
# used to `continue` without creature_turn(), which froze our creatures while the player stood
# against a wall while the original's kept moving. Proven from bytes 2026-10-26 and corroborated
# live in sector 03: 16 refused direction keys, creature coordinates changed on 12 of them.
#
# The signature is the turn log. --debug cli writes one dump per RESOLVED turn, and a refused
# step used to resolve nothing, so a build without the fix writes ZERO dumps for a script made
# entirely of refusals. That is what makes this a gate that fails on the behaviour it replaced,
# which is the only kind worth having. The player must also stay put, or the refusals are not
# refusals and the count would pass for the wrong reason.
python3 tests/cp5_fight_seed.py "$WORK/c3c" >/dev/null 2>&1
python3 - "$WORK/c3c/content/mapx00" <<'PYEOF'
import sys
p = sys.argv[1]
d = bytearray(open(p, 'rb').read())
d[0 * 64 + 1] = 0x10          # mountain at (1,0), directly west of the entry at (2,0)
open(p, 'wb').write(bytes(d))
PYEOF
for i in $(seq 1 5); do printf '0xf2\n'; done > "$WORK/c3c.keys"   # 0xf2 = U2_KEY_LEFT
rm -rf "$WORK/c3c/dbg"
./u2 --headless --play-map --cleanroom --data-dir "$WORK/c3c/content" --save-dir "$WORK/c3c/save" \
     --script "$WORK/c3c.keys" --debug cli --debug-path "$WORK/c3c/dbg" --debug-limit 0 \
     >/dev/null 2>&1
dumps=$(find "$WORK/c3c/dbg" -name 'turn-*.json' 2>/dev/null | wc -l)
last=$(find "$WORK/c3c/dbg" -name 'turn-*.json' 2>/dev/null | sort | tail -1)
stay=$(python3 -c "
import json, sys
p = sys.argv[1]
print(json.load(open(p))['state']['raw']['pos'] if p else 'none')" "$last" 2>/dev/null)
if [ "$dumps" -eq 5 ] && [ "$stay" = "02 00" ]; then
    ok "CP5 C3c: a refused step takes the world turn ($dumps dumps for 5 refusals, player held at 02 00)"
else
    bad "CP5 C3c: $dumps dumps for 5 refused steps (expect 5), player at '$stay' (expect '02 00') — the reject path is not taking the world turn"
fi

# --- C3d: the arms-paralysis ATTACK refusal is a completed command too --------------
#
# asm/2BA5 refuses the swing when [DS:26C] is non-zero, and asm/2BAD ends that refusal with
# `jmp L_000c30` -- asm/0A30, which falls through to asm/0A33_cmd_done and the post-command tail.
# Same target as asm/0A15, the terrain refusal proven in C3c above. So a refused attack takes the
# world turn and ticks the status counters.
#
# The engine had `if (paral_arms == 0) creature_turn(...)` there, which made arm paralysis
# PERMANENT for a player who kept trying to swing: the refusal resolved nothing, so the counter
# never decayed. That is the same stranding the owner reported for legs, on the other command.
# Corroborated without a new capture, in captures/spice86/fight-20261021-castle03/turns (3000
# per-turn DS dumps): DS:26C fell 6 times, every one with the food odometer UNCHANGED, and zero
# times with food changed.
#
# --dump-ds exists for this gate: it writes the DS mirror, the test sets DS:26C=15, and the run
# must drive it to 0. Control, run 2026-10-29: with the old guard restored the same script writes
# ZERO dumps, because a refusal that resolves nothing also logs nothing (against 16 here).
python3 tests/cp5_fight_seed.py "$WORK/c3d" >/dev/null 2>&1
printf '0xf2\n' > "$WORK/c3d/k0"
./u2 --headless --play-map --cleanroom --data-dir "$WORK/c3d/content" --save-dir "$WORK/c3d/save" \
     --script "$WORK/c3d/k0" --dump-ds "$WORK/c3d/base.hex" >/dev/null 2>&1
python3 - "$WORK/c3d/base.hex" "$WORK/c3d/arms.hex" <<'PYEOF'
import sys
b = bytearray(bytes.fromhex(open(sys.argv[1]).read()))
b[0x26C] = 0x0F                      # arms paralysis, 15 turns
open(sys.argv[2], 'w').write(b.hex())
PYEOF
for i in $(seq 1 16); do printf '0x41\n0xf0\n'; done > "$WORK/c3d/atk.keys"   # A then UP, x16
rm -rf "$WORK/c3d/dbg"
./u2 --headless --play-map --cleanroom --data-dir "$WORK/c3d/content" --save-dir "$WORK/c3d/save" \
     --load-ds "$WORK/c3d/arms.hex" --script "$WORK/c3d/atk.keys" \
     --debug cli --debug-path "$WORK/c3d/dbg" --debug-limit 0 >/dev/null 2>&1
c3d=$(find "$WORK/c3d/dbg" -name 'turn-*.json' 2>/dev/null | wc -l)
c3dlast=$(find "$WORK/c3d/dbg" -name 'turn-*.json' 2>/dev/null | sort | tail -1)
c3dpara=$(python3 -c "
import json,sys
p=sys.argv[1]
print(json.load(open(p))['state']['raw']['paralysis'] if p else 'none')" "$c3dlast" 2>/dev/null)
# One turn per attack, not two: `A` only arms the pending command, the direction completes it.
if [ "$c3d" -eq 16 ] && [ "$c3dpara" = "00 00 00 00" ]; then
    ok "CP5 C3d: a refused attack takes the world turn and decays arm paralysis (16 dumps, DS:26C 15->0)"
else
    bad "CP5 C3d: $c3d dumps for 16 refused attacks (expect 16), paralysis '$c3dpara' (expect '00 00 00 00') — the arms refusal is not taking the turn, and arm paralysis is permanent"
fi

# --- P1: the on-foot passability ladder, checked tile by tile -------------------------
#
# The engine used to classify tiles on the ART INDEX (map_byte >> 2). asm/2423 classifies on
# map_byte >> 1, and its rule is a threshold plus a short list, not a table. That mismatch is the
# root of every passability regression here: shallow water, town gates, town paving and the force
# field were each wrong in turn, and each was fixed by adding a row to a table that should not
# exist. See docs/passability-findings.md F14.
#
# The gate drives the real engine and asks whether the player actually moved, because the failure it
# guards is a shape error — someone reinstating a per-tile table that agrees on whichever rows
# whoever wrote it thought of. 16 tiles, each in a fresh seed (the engine rewrites the save in
# place, so a reused dir reports a wrong answer, and already did once this session).
if U2_BIN=./u2 python3 tests/passability_ladder.py > "$WORK/p1.log" 2>&1; then
    ok "CP5 P1: passability matches asm/2423 on foot (16 tiles) and on all four travel modes (12 cases)"
else
    sed 's/^/        /' "$WORK/p1.log" | tail -6
    bad "CP5 P1: passability ladder disagrees with the bytes"
fi

# --- E1: the two HP-costing tiles of asm/2423's effect ladder -------------------------
#
# Entering map byte 0x04 (mode 0x02, swamp) costs 5 HP silently (asm/248B -> asm/2495), and
# entering map byte 0x5C (mode 0x2E, force field) costs 1000 unless [DS:D6] holds a ring
# (asm/24B1 -> asm/24B5). The whole chain is gated on [DS:4A] == 0, the sector's ones digit.
#
# The shape this gate exists to catch is the naive one: a charge for STANDING on the swamp,
# or for LEAVING it, passes only the safe->swamp case, and the safe->swamp case is the one
# a reviewer would think sufficient. All three owner controls run (PASS free, enter -5,
# exit free), plus a grass step and a no-input arm so the gate cannot pass by drifting, a
# death arm for the BCD underflow, and both rungs of the field. Both controls were run red
# before this was trusted: a 6-HP charge turns `enter` red, and firing the ladder on the
# tile you stand on instead of the tile you enter turns five of eight red.
if U2_BIN=./u2 python3 tests/swamp_damage.py > "$WORK/e1.log" 2>&1; then
    ok "CP5 E1: tile effects — swamp -5 on entry only (3 owner controls), underflow is death, field -1000 and its ring gate (9 cases, silent in original / announced in enhanced)"
else
    sed 's/^/        /' "$WORK/e1.log" | tail -10
    bad "CP5 E1: the tile-effect ladder disagrees with asm/248B/2495/24B1/24B5"
fi

# --- E2: the wound flash, both directions ----------------------------------------------
#
# The ring (asm/5255 -> asm/4C1E, XOR on, tone, XOR off) marks the VICTIM'S cell:
# the player's cell on a wound, the attacked creature's cell when a swing lands.
# First observed live 2026-11-02 in captures/spice86/wound-flash-20261102 — three
# player-cell flashes each coinciding with an HP drop, and one creature-cell flash
# on the turn CMD: ATTACK-- entered the log. The engine's flash leaves the frame
# bit-identical, so the gate reads the diag flash counter from the turn dumps and
# correlates it against the HP pair turn by turn: wound flashes must equal wounded
# turns, and any flash WITHOUT a wound can only be an attack flash.
#
# Control run red before trusting: deleting the attack hook leaves the wound arm
# green and turns the attack arm red (11 flashes vs 11 wounded turns) — the two
# hooks are proven independently, and the naive "flashes > 0" gate that the counter
# tempts you to write would have passed on counter-hit flashes alone.
if U2_BIN=./u2 python3 tests/wound_flash.py > "$WORK/e2.log" 2>&1; then
    ok "CP5 E2: wound flash fires on the victim's cell — per wounded turn, on landed attacks, never on a quiet map (3 arms)"
else
    sed 's/^/        /' "$WORK/e2.log" | tail -8
    bad "CP5 E2: the wound flash does not follow the victim"
fi

# --- V1: BOARD, and what one step costs in each travel mode ---------------------------
#
# asm/2F6F cmd_B prints "BOARD" and falls into asm/2FAD, which dispatches on [DS:14] (the tile
# under the player, shifted right once) and sets [DS:13]. Each arm is gated on an item slot:
# DS:DD/DF/E1/E2/E5 are items_held[7/9/11/12/15] = ANKH, SKULL KEY, BRASS BUTTON, BLUE TASSLE,
# TRI-LITHIUM (slot names from docs/spice86-live-findings.md § 11), and each
# refusal message names its own item. asm/253B is the other half: one 19-food charge on foot, two
# mounted (asm/2626 subtracts and then calls the foot tail, which subtracts again), none aboard the
# ship, none in the rocket.
#
# Controls, both run and confirmed red before this was trusted: making the rocket arm accept takes
# the ladder's two rocket cases and the rocket food case with it, and making the mounted charge 1
# instead of 2 turns horse and plane red. The tile each arm leaves behind IS asserted, by saving
# and reading the sector back -- which only became possible once u2_world_store stopped writing a
# dirty map to the monx filename (docs/phase4-state.md, closed 2026-10-29). The gate also checks
# the save left monx00's size alone, so that defect cannot come back quietly.
if U2_BIN=./u2 python3 tests/vehicle_board.py > "$WORK/v1.log" 2>&1; then
    ok "CP5 V1: BOARD sets the mode and costs the mode's food (13 cases, fresh seed each)"
else
    sed 's/^/        /' "$WORK/v1.log" | tail -8
    bad "CP5 V1: BOARD or the per-mode food charge disagrees with asm/2FAD / asm/253B"
fi

# --- V2: a site transition writes no sector file, and the enhanced knob writes one ----
#
# The original persists the player record and nothing else: measured 2026-10-29 over noVNC in
# sector 31, where mapx31, monx31, mapx30 and monx30 all came back byte-identical to pristine
# after several town entries and exits (docs/save-format.md § 10, docs/map-format.md § 10).
# The engine used to flush both files on entering and leaving a site on a mis-cited routine
# (asm/22FD is a boots gate, not a store), which made it remember a world the game forgets.
# The control ran red first: BOARD dirties the map, the transition wrote it, and the paved cell
# was on disk. --keep-site-state is the owner's opt-in for the opposite behaviour and is
# refused in original mode, so both directions of this gate are load-bearing.
if U2_BIN=./u2 python3 tests/site_state.py > "$WORK/v2.log" 2>&1; then
    ok "CP5 V2: leaving a site writes no sector file; --enhanced --keep-site-state does"
else
    sed 's/^/        /' "$WORK/v2.log" | tail -8
    bad "CP5 V2: sector persistence on a site transition disagrees with the measurement"
fi

# --- V3: the three Q guards, and enhanced mode dropping them -------------------------
#
# cmd_Q's chain, read off the bytes at file 0x3FC7: DS:4A (indoors) -> ONLY OUTDOORS!,
# DS:6D -> ONLY ON EARTH!, DS:13 < 0x78 (aboard a vehicle) -> ONLY ON FOOT!, else
# ONE MOMENT PLEASE! and the save runs. The engine had the first one and had stubbed the other
# two away as "cannot happen until CP6/CP7" — and both had become possible: vehicles exist now,
# and DS:6D is a live byte that refuses the save for any character holding boots, which is
# exactly how a persistence measurement came out wrong on 2026-10-29. Enhanced mode drops all
# three (owner's call): the difference is a policy struct, not a branch in the save path.
if U2_BIN=./u2 python3 tests/save_gate.py > "$WORK/v3.log" 2>&1; then
    ok "CP5 V3: Q refuses indoors / off-earth / mounted; --enhanced saves anywhere"
else
    sed 's/^/        /' "$WORK/v3.log" | tail -10
    bad "CP5 V3: the save guards disagree with cmd_Q at file 0x3FC7"
fi

# --- V4: X-IT is the dismount BOARD's twin -------------------------------------------
#
# asm/44D4 writes 2*[DS:13] back through the cell pointer and restores DS:13 to 0x78 + 2*pclass.
# That is the inverse of BOARD's shift right, and it is what makes the boarding rule "the tile
# is carried" rather than "the tile is destroyed". The gate runs the reachable arms end to end
# (on foot, horse on grass, ship on water, plane on grass) and site_check.c covers the arm no
# scripted foot start can reach (a non-ship vehicle over water) plus the class-dependent stamp.
# The dismount arms are byte-proven and unplayed: the three harvests that ran cmd_X pressed
# every letter on a foot character, so only the " WHAT?" arm has ever executed.
if U2_BIN=./u2 python3 tests/vehicle_dismount.py > "$WORK/v4.log" 2>&1; then
    ok "CP5 V4: X-IT returns the vehicle tile and the class-dependent foot stamp"
else
    sed 's/^/        /' "$WORK/v4.log" | tail -10
    bad "CP5 V4: the dismount disagrees with asm/44D4"
fi

# --- V5: the on-foot stamp at world open is class-dependent ------------------------------
#
# asm/080D computes 0x78 + 2*pclass and asm/44D4 restores the same expression, but the engine
# hardcoded 0x78 at world open. A Fighter's stamp IS 0x78, so every seeded test character in the
# project -- all Fighters -- passed against the constant and the defect was invisible rather than
# merely untested. Closed from the original on 2026-10-29: a class-2 Wizard read DS:13 = 0x7A on
# foot (captures/spice86/shop-window-transport, docs/needs-gameplay.md G41).
# Control, run 2026-10-29: with the constant restored the gate reports FIGHTER ok and CLERIC /
# WIZARD / THIEF all "got 0x78" -- the exact shape of how this hid.
if U2_BIN=./u2 python3 tests/foot_stamp.py > "$WORK/v5.log" 2>&1; then
    ok "CP5 V5: all four classes enter the world with the class-dependent on-foot stamp"
else
    sed 's/^/        /' "$WORK/v5.log" | tail -8
    bad "CP5 V5: DS:13 at world open disagrees with asm/080D for some class"
fi

# --- V5b: the drawn player art follows DS:13, so boarding shows the vehicle ----------
#
# The staging buffer holds DS:13 at the player's cell and staging halves to art, so the
# sprite at the player's position is stamp>>1. The engine passed the literal 60 instead,
# which is exactly what 0x78>>1 gives for a Fighter on foot -- every capture and every
# seeded character this project has used -- and so it was invisible until the owner
# boarded a rocket on 2026-11-10 and still saw a person.
# Control, run 2026-11-10: with the constant put back at one of the two map draws, the
# gate reports that draw FAIL and exits 1; restored, it passes.
if python3 tests/board_stamp.py > "$WORK/v5b.log" 2>&1; then
    ok "CP5 V5b: the map draws take their art from player_stamp, so a boarded vehicle is drawn"
else
    sed 's/^/        /' "$WORK/v5b.log" | tail -8
    bad "CP5 V5b: the player art is hardcoded again, so boarding draws a person"
fi

# --- C5: the force field is traversable and costs 1000 HP; a ring stops it -----------
#
# asm/24B5_force_field_event, reached from inside the passability routine asm/2423. Tile 0x5C is
# walkable (mode byte 0x2E = 0x5C >> 1 — the units error that made an earlier phase search mapx
# for 0x2E and conclude the tile could not exist), and stepping on it subtracts 1000 HP unless
# items_held[0] (DS:D6, the RING slot) is non-zero. Measured: captures/spice86/g29-field, six
# crossings at exactly 1000 each and nothing with the ring held; fight-20261021-castle03 turn 2909.
#
# Two runs, same seeded world, differing only in DS:D6. Without the gate the engine treated 0x5C
# as a wall via default-deny, so the first run would refuse the step and the player would never
# move — which is the failure this replaces, and it is caught by the position assertion, not only
# the HP one.
python3 tests/cp5_fight_seed.py "$WORK/c5" >/dev/null 2>&1
python3 - "$WORK/c5/content/mapx00" <<'PYEOF'
import sys
p = sys.argv[1]
d = bytearray(open(p, 'rb').read())
d[0 * 64 + 3] = 0x5C          # force field directly east of the seeded entry at (2,0)
open(p, 'wb').write(bytes(d))
PYEOF
# The seeded monx00 puts its one creature at (3,0), and u2_monsters_clear_spawn_cells legitimately
# writes 0x70 over a spawn cell (that is the C6 fix, and the original does it too). So the field and
# the creature cannot share a cell in this fixture: move the creature far away rather than move the
# field, which is what the test is about.
python3 - "$WORK/c5/content/monx00" <<'PYEOF'
import sys
p = sys.argv[1]
d = bytearray(open(p, 'rb').read())
d[0] = 30; d[32] = 30         # slot 0 x,y -> (30,30)
open(p, 'wb').write(bytes(d))
PYEOF
printf '0xf2\n' > "$WORK/c5/k0"
./u2 --headless --play-map --cleanroom --data-dir "$WORK/c5/content" --save-dir "$WORK/c5/save" \
     --script "$WORK/c5/k0" --dump-ds "$WORK/c5/base.hex" >/dev/null 2>&1
# HP high byte 0x99 (= 9900..9999, comfortably above the death threshold), no ring, then with one.
python3 - "$WORK/c5/base.hex" "$WORK/c5/noring.hex" "$WORK/c5/ring.hex" <<'PYEOF'
import sys
b = bytearray(bytes.fromhex(open(sys.argv[1]).read()))
b[0x51] = 0x99; b[0x52] = 0x99; b[0xD6] = 0x00
open(sys.argv[2], 'w').write(b.hex())
b[0xD6] = 0x01
open(sys.argv[3], 'w').write(b.hex())
PYEOF
# Two steps: the warm-up key above leaves the player at (1,0), so RIGHT once returns to the entry
# cell (2,0) and RIGHT again steps onto the field at (3,0). Only the second one is charged.
printf '0xf3\n0xf3\n' > "$WORK/c5/east.keys"       # 0xf3 = U2_KEY_RIGHT
c5read() {   # $1 = debug dir -> prints "pos hp"
  local last
  last=$(find "$1" -name 'turn-*.json' 2>/dev/null | sort | tail -1)
  python3 -c "
import json,sys
p=sys.argv[1]
if not p: print('none none'); raise SystemExit
s=json.load(open(p))['state']['raw']
print(s['pos'], s['hp'])" "$last"
}
rm -rf "$WORK/c5/dbg_a" "$WORK/c5/dbg_b"
./u2 --headless --play-map --cleanroom --data-dir "$WORK/c5/content" --save-dir "$WORK/c5/save" \
     --load-ds "$WORK/c5/noring.hex" --script "$WORK/c5/east.keys" \
     --debug cli --debug-path "$WORK/c5/dbg_a" --debug-limit 0 >/dev/null 2>&1
no_ring=$(c5read "$WORK/c5/dbg_a")
rm -rf "$WORK/c5/save2" && cp -r "$WORK/c5/save" "$WORK/c5/save2"
./u2 --headless --play-map --cleanroom --data-dir "$WORK/c5/content" --save-dir "$WORK/c5/save2" \
     --load-ds "$WORK/c5/ring.hex" --script "$WORK/c5/east.keys" \
     --debug cli --debug-path "$WORK/c5/dbg_b" --debug-limit 0 >/dev/null 2>&1
with_ring=$(c5read "$WORK/c5/dbg_b")
case "$no_ring" in
  "03 00 89 99") ok "CP5 C5: stepping on the force field moves the player and costs 1000 HP (9999 -> 8999)" ;;
  *) bad "CP5 C5: no-ring step gave '$no_ring' (expect '03 00 89 99' — moved onto 0x5C, HP high byte 99 -> 89)" ;;
esac
case "$with_ring" in
  "03 00 99 99") ok "CP5 C5: with the ring held the field costs nothing (HP unchanged, still moved)" ;;
  *) bad "CP5 C5: with-ring step gave '$with_ring' (expect '03 00 99 99' — ring must absorb the field)" ;;
esac

# --- the jump table is reachable: a letter key must land in its handler ------
# u2_cmd_implemented() reports what the table CLAIMS, and nothing until now proved the
# loop routes a letter into it at all. The Q migration is what exposed the gap: putting a
# handler in the wrong table slot compiles clean, keeps every other gate green, prints
# nothing, and still reports itself as implemented. One run per letter, so a misfiled slot
# names itself instead of arriving as a mystery.
for pair in "H:HYPER WHAT?" "J:JUMP...WHEE..." "Q:ONE MOMENT PLEASE!" "P:CMD: PASS" "Z:A LEVEL" "E:CMD: ENTER WHAT?" "A:CMD: ATTACK--" "U:CMD: UNLOCK DIRECTION-" "M:MAGIC SPELL READY #1"; do
    letter=${pair%%:*}; want=${pair#*:}
    python3 tests/cp5_fight_seed.py "$WORK/tab-$letter" >/dev/null 2>&1
    # Two of these block for a second key inside the handler: Z waits for a dismissing key,
    # M waits for a spell number. Z is given NO dismiss key on purpose: the original blocks with
    # the page on screen, so an undismissed Z leaves the page as the final state and that is the
    # only moment the page is observable from a scripted run. The dismiss path is gated separately
    # below, where the assertion is that the screen comes back clean.
    case "$letter" in
      Z) printf 'Z
' > "$WORK/tab-$letter.keys" ;;
      M) printf 'M\n1\n'  > "$WORK/tab-$letter.keys" ;;
      *) printf '%s\n' "$letter" > "$WORK/tab-$letter.keys" ;;
    esac
    ./u2 --headless --play-map --cleanroom \
         --data-dir "$WORK/tab-$letter/content" --save-dir "$WORK/tab-$letter/save" \
         --script "$WORK/tab-$letter.keys" --dump-state > "$WORK/tab-$letter.out" 2>&1
    if grep -qF -- "$want" "$WORK/tab-$letter.out"; then
        ok "dispatch: letter $letter reaches its handler in cmd_table ($want on screen)"
    else
        bad "dispatch: $letter printed nothing — the slot is empty, misfiled, or the loop is not calling u2_cmd_dispatch"
        sed 's/^/        /' "$WORK/tab-$letter.out" | tail -5
    fi
done

# --- Y1: the yell is a freeform line input, and it is the loop the letter run misread ----------
#
# asm/4542 falls into asm/4551..asm/4567, which is a complete read-echo loop terminated by Enter.
# For four days this was logged as cmd_Y "not returning to the prompt", because the range sat cut
# into seven fragments each headed "behavior not yet reviewed" and nobody read it as one routine.
# tests/cmd_yell.py asserts the prompt, the echo, Enter handing control back, the swallow of a
# letter typed before Enter (the observation that was misread), that J on its own still dispatches
# so the swallow is the loop's doing, and that no DS field moves at all -- the byte claim "nothing
# in the range writes memory" made testable.
# Control, run 2026-10-29: with the loop replaced by print-and-return the gate reports 6 failures
# including the swallow case, which is the property a prompt-and-echo check alone would miss.
if U2_BIN=./u2 python3 tests/cmd_yell.py > "$WORK/y1.log" 2>&1; then
    ok "CP5 Y1: cmd_Y echoes freeform input until Enter and writes nothing (asm/4551..4567)"
else
    sed 's/^/        /' "$WORK/y1.log" | tail -8
    bad "CP5 Y1: the yell loop is wrong -- prompt, echo, Enter-termination or a state write"
fi

# Z takes the WHOLE screen. asm/4568 opens with `call L_004c1c`, a full clear, so the original's
# Z page has no status band on it at all -- proven against the original with the same maxed save
# in captures/spice86/z-page-maxed/readings.md. The engine used to leave the band in place, so a
# character owning many items had its ITEMS list run into GOLD= and overwrite H.P./FOOD/EXP..
# The old Z assertion only checked "A LEVEL" at the top, which is why it shipped: the defect was
# entirely below the line the test looked at.
if grep -qE "H\.P\.=|FOOD=|EXP\.=" "$WORK/tab-Z.out"; then
    bad "regression: the Z page still draws the status band -- asm/4568 clears the whole screen"
    grep -nE "H\.P\.=|FOOD=|EXP\.=" "$WORK/tab-Z.out" | head -3 | sed 's/^/        /'
else
    ok "Z page is a full-screen takeover: no status band, matching asm/4568's clear"
fi

# ...and the overlay has to come back DOWN clean. Z draws into the persistent text grid, which is
# the same grid the compositor reads for the status band and the CMD prompt, so the handler snapshots
# the grid and puts the snapshot back on a real keypress. Before that, u2_overworld_draw composited
# vram rows 20..24 without clearing them and the tail of the ITEMS list stayed printed over the band
# -- the owner saw `UMS-01 PASS` / `TRI-LITHI` stuck under the map with no H.P. line anywhere.
# Headless could not show the layering, and this is the half that leaves the screen sane.
python3 tests/cp5_fight_seed.py "$WORK/z-restore" >/dev/null 2>&1
printf 'Z\nSP\n' > "$WORK/z-restore.keys"
./u2 --headless --play-map --cleanroom --data-dir "$WORK/z-restore/content" \
     --save-dir "$WORK/z-restore/save" --script "$WORK/z-restore.keys" \
     --dump-state > "$WORK/z-restore.out" 2>&1
if grep -qE "H\.P\.=|FOOD=" "$WORK/z-restore.out"; then
    ok "a keypress dismisses the Z page and the status band comes back"
else
    bad "regression: after dismissing Z the status band never returns"
    sed 's/^/        /' "$WORK/z-restore.out" | tail -6
fi
# The stronger property, and the one the owner actually asked for: the overlay leaves NO trace.
# Comparing against a run that never opened Z proves the screen is restored rather than plausibly
# re-drawn. This is the gate that survives the failure mode the text-grep version did not: with the
# teardown removed the leftover shows up as the page still being up, not as stray item text, so
# grepping for item names passed a broken build. Verified as a control -- with the restore removed
# this diff goes non-empty.
printf 'S\n' > "$WORK/z-baseline.keys"
printf 'Z\nx\nS\n' > "$WORK/z-trace.keys"
./u2 --headless --play-map --cleanroom --data-dir "$WORK/z-restore/content" \
     --save-dir "$WORK/z-restore/save" --script "$WORK/z-baseline.keys" \
     --dump-state 2>&1 | grep '^|' > "$WORK/z-baseline.out"
./u2 --headless --play-map --cleanroom --data-dir "$WORK/z-restore/content" \
     --save-dir "$WORK/z-restore/save" --script "$WORK/z-trace.keys" \
     --dump-state 2>&1 | grep '^|' > "$WORK/z-trace.out"
if diff -q "$WORK/z-baseline.out" "$WORK/z-trace.out" >/dev/null; then
    ok "Z leaves no trace: post-dismiss screen is byte-identical to a run that never opened Z"
else
    bad "regression: the Z overlay is still visible after dismissal"
    diff "$WORK/z-baseline.out" "$WORK/z-trace.out" | head -8 | sed 's/^/        /'
fi

# M's second half: the echo is the spell NAME, not a number. That is the whole point of the
# 2026-10-25 correction to asm/3B92_cmd_M.asm, so it gets its own assertion — and it is
# mode-aware, because with no copy of the game the pool names are blank by design (see
# status.c's Z_NAME) and only the prompt half is checkable.
if grep -q "U2_T_SPELLS_N" build/gen_content.h 2>/dev/null; then
    if grep -qF -- "READY => LIGHT" "$WORK/tab-M.out"; then
        ok "dispatch: cmd_M echoes the spell NAME from the pool (READY => LIGHT, not a digit)"
    else
        bad "dispatch: cmd_M did not echo the pool name for slot 1 — expected LIGHT"
        sed 's/^/        /' "$WORK/tab-M.out" | tail -5
    fi
else
    ok "dispatch: cmd_M pool-name assertion skipped with the pool absent (prompt half already passed)"
fi

# --- no quit key, and --dev is the only thing allowed to add one -------------------------
# The original has no quit key anywhere: nothing in asm/ compares against 0x1b (Escape) or
# 0x11 (Ctrl-Q). An earlier revision broke the loop on Escape and had cmd_Z return a quit code
# on it, both as --play-map conveniences, which read as fidelity and were not the game. Those
# are gone; --dev is the sanctioned home, and the rule on it is that it may add observation and
# an exit and may NEVER change game behaviour.
#
# These are behavioural, not greps. --dump-each prints one dump per key, so "did the run
# survive this key" is countable: two PASS lines means both P's ran, one means the key between
# them ended the loop.
quit_count() {   # $1 = extra flags, $2 = key script
    ./u2 --headless $1 --play-map --cleanroom \
         --data-dir "$WORK/quit/content" --save-dir "$WORK/quit/save" \
         --script "$2" --dump-each 2>&1 | grep -c "CMD: PASS"
}
python3 tests/cp5_fight_seed.py "$WORK/quit" >/dev/null 2>&1
printf 'P\nESC\nP\n'     > "$WORK/quit-esc.keys"
printf 'P\n0x11\nP\n'    > "$WORK/quit-ctrlq.keys"

if [ "$(quit_count '' "$WORK/quit-esc.keys")" = "2" ]; then
    ok "no quit key: Escape in the overworld is an ordinary key, the run continues"
else
    bad "regression: Escape ended the overworld run -- the original has no quit key"
fi
if [ "$(quit_count '--dev' "$WORK/quit-esc.keys")" = "2" ]; then
    ok "--dev does not make Escape special either"
else
    bad "--dev must not restore the Escape quit; only Ctrl-Q is gated on --dev"
fi
if [ "$(quit_count '' "$WORK/quit-ctrlq.keys")" = "2" ]; then
    ok "Ctrl-Q is inert without --dev: default mode implements the game and nothing else"
else
    bad "Ctrl-Q ended the run without --dev -- dev affordance leaked into the default path"
fi
if [ "$(quit_count '--dev' "$WORK/quit-ctrlq.keys")" = "1" ]; then
    ok "--dev: Ctrl-Q ends the run (the development exit the owner asked for, on a free byte)"
else
    bad "--dev: Ctrl-Q did not end the run -- the dev exit is not wired"
fi

# Structural half: a command HANDLER must never be able to end the session at all, in any mode.
# That is where the Escape version went wrong, and a behaviour test would not catch it coming
# back inside cmd_Z if the loop grew another path.
if grep -rq "U2_CMD_QUIT" src/ 2>/dev/null; then
    bad "regression: a command-level quit path is back in src/ -- handlers must not end sessions"
    grep -rn "U2_CMD_QUIT" src/ | head -3 | sed 's/^/        /'
else
    ok "no command handler can end the session, in any mode"
fi

# --- W and V: wear armour and view, all three refusal branches --------------
# cmd_W was static-only until 2026-10-26 and cmd_V's render is still not modelled, so the gate
# asserts the parts that ARE established: the menu, the armour-name echo, and each exit. The name
# echo matters — the original prints "CLOTH READY." as one line, and an engine that skipped the
# pool print would still print "READY." and look fine to a test that only greps for it.
python3 tests/cp5_fight_seed.py "$WORK/wv" >/dev/null 2>&1
patch_player() {   # patch_player FILE OFFSET VALUE   (file[i] == DS[0x36+i])
    python3 - "$1" "$2" "$3" <<'PYEOF'
import sys
p, off, val = sys.argv[1], int(sys.argv[2], 0), int(sys.argv[3], 0)
d = bytearray(open(p, 'rb').read())
d[off] = val
open(p, 'wb').write(bytes(d))
# Verify the write landed. A helper that fails and exits non-zero while the shell carries on is the
# exact silent-failure shape this project keeps recording: the first version of this function used
# int() on a 0x-prefixed value, raised ValueError, patched nothing, and the strength-gate assertion
# below failed for a reason that had nothing to do with the engine.
if bytearray(open(p, 'rb').read())[off] != val:
    sys.exit(f'patch_player: write to {off} did not land')
PYEOF
}
PLAYER="$WORK/wv/save/PLAYER/PLAYER"   # saves/<slot>/PLAYER; engine/src/core/slot.h
run_wv() {   # run_wv EXTRA_FLAGS SCRIPT OUTFILE
    ./u2 --headless $1 --play-map --cleanroom --data-dir "$WORK/wv/content" \
         --save-dir "$WORK/wv/save" --script "$2" --dump-each > "$3" 2>&1
}

# (a) own CLOTH, strong enough -> name then READY.   DS:97 -> file 0x61, DS:4B -> file 0x15
patch_player "$PLAYER" $((0x61)) 1
patch_player "$PLAYER" $((0x15)) 0x40
printf 'W\n1\n' > "$WORK/wv-ok.keys"
run_wv "" "$WORK/wv-ok.keys" "$WORK/wv-ok.out"
if grep -q "CLOTH READY\." "$WORK/wv-ok.out"; then
    ok "CP5 W/V: WEAR ARMOUR echoes the armour NAME from the pool before READY."
else
    bad "CP5 W/V: expected 'CLOTH READY.' — the pool echo or the success path is broken"
    sed 's/^/        /' "$WORK/wv-ok.out" | tail -5
fi

# (b) own nothing -> the menu still lists all six, and the refusal names the type
patch_player "$PLAYER" $((0x61)) 0
printf 'W\n2\n' > "$WORK/wv-no.keys"
run_wv "" "$WORK/wv-no.keys" "$WORK/wv-no.out"
if grep -q "LEATHER NOT OWNED!" "$WORK/wv-no.out"; then
    ok "CP5 W/V: unowned armour is refused AFTER the choice, naming the type"
else
    bad "CP5 W/V: expected 'LEATHER NOT OWNED!'"
    sed 's/^/        /' "$WORK/wv-no.out" | tail -5
fi

# (c) own it but too weak. digit*8 vs the PACKED byte: 1*8 >= 0x08 refuses.
patch_player "$PLAYER" $((0x61)) 1
patch_player "$PLAYER" $((0x15)) 0x08
printf 'W\n1\n' > "$WORK/wv-weak.keys"
run_wv "" "$WORK/wv-weak.keys" "$WORK/wv-weak.out"
# This one SKIPs without the player's copy, and the reason is worth the comment because it
# is not laziness: asm/43F3 assembles the refusal from TWO db literals with a 0x0d between
# them (`" <-THOU ART NOT",0x0d,"STRONG ENOUGH TO WEAR!"`), so it cannot be written as one
# `run` locator with a working asm fallback and resolves to "" when there is no exe. The
# engine therefore prints nothing at all in CI, and no grep on the screen can recover a
# string that was never emitted. Saying "uncheckable here" is the honest verdict; a red that
# actually means "CI has no disk" is the one this file's own guard exists to prevent.
if grep -q '^#define U2_S_NOT_STRONG_ENOUGH ""' build/gen_content.h 2>/dev/null; then
    skip "CP5 W/V: strength-gate refusal text needs the player's copy (asm/43F3 splits it across two db literals)"
elif grep -q "STRONG ENOUGH TO WEAR!" "$WORK/wv-weak.out"; then
    ok "CP5 W/V: the strength gate refuses against the packed strength byte, not the decimal"
else
    bad "CP5 W/V: expected 'STRONG ENOUGH TO WEAR!' with DS:4B = 0x08"
    sed 's/^/        /' "$WORK/wv-weak.out" | tail -5
fi
patch_player "$PLAYER" $((0x15)) 0x40

# (d) enhanced lists only what is owned; fidelity lists all six.
printf 'W\n1\n' > "$WORK/wv-enh.keys"
run_wv "--enhanced" "$WORK/wv-enh.keys" "$WORK/wv-enh.out"
if grep -q "6-POWER" "$WORK/wv-enh.out"; then
    bad "CP5 W/V: enhanced mode still lists unowned armour (found 6-POWER)"
else
    ok "CP5 W/V: enhanced mode lists only owned armour, keeping the original numbering"
fi
if grep -q "1-CLOTH" "$WORK/wv-enh.out"; then
    ok "CP5 W/V: the owned entry keeps menu number 1, so the digit still indexes the table"
else
    bad "CP5 W/V: enhanced mode dropped the owned CLOTH entry or renumbered it"
fi

# (e) V with no helm held -> VIEW WHAT?. items_held[5] is DS:DB; U2_T_ITEMS[5] is HELM.
printf 'V\n' > "$WORK/wv-v.keys"
run_wv "" "$WORK/wv-v.keys" "$WORK/wv-v.out"
if grep -q "VIEW WHAT?" "$WORK/wv-v.out"; then
    ok "CP5 W/V: VIEW with no HELM held prints VIEW WHAT?"
else
    bad "CP5 W/V: expected 'VIEW WHAT?' with an empty inventory"
    sed 's/^/        /' "$WORK/wv-v.out" | tail -5
fi

printf '\n'
if [ "$fail" -eq 0 ]; then
    printf 'CP5 SPEC: PASS\n'
else
    printf 'CP5 SPEC: FAIL\n'
fi
exit "$fail"

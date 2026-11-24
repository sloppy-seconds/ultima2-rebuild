#!/usr/bin/env bash
# check_all.sh — run every gate in the project, one verdict per line.
#
# This exists because the gates are the project's safety property and they are scattered:
# assemble_clean, the engine suite, and six tool selftests that each have to be remembered.
# For unattended work that is the difference between "committed a plausible wrong answer" and
# "committed nothing". A gate you have to remember to run is a gate you will not run.
#
#   tools/check_all.sh            # everything, including the slow proof and the engine suite
#   tools/check_all.sh --fast     # skip assemble_clean and make test (the sub-minute gates)
#   tools/check_all.sh --ci       # what CI runs: implies --fast, and a gate that is REQUIRED
#                                 # but cannot run counts as a failure instead of a skip.
#                                 # Gates whose inputs are gitignored are declared optional
#                                 # and reported as SKIP* so the log says so out loud.
#
# Rules this honours, all from AGENTS.md:
#   * A SKIP IS NOT A PASS. A gate whose inputs are gitignored (original/, references/apple/,
#     the Ghidra output under /var/tmp) reports SKIP and names the missing input, and the
#     script still exits non-zero if any gate that COULD run failed. It never reports success
#     for a check that did not happen — a skip that can hide a failure is the thing that bit
#     this project with run_cp5.sh.
#   * In --ci, that rule is enforced rather than merely described: a required gate that skips
#     fails the run. A gate that silently cannot run in CI is how asm_body_guard spent its
#     whole life reporting a pass it had not earned.
#   * Nothing here regenerates anything. These are checks; --emit flags are deliberately not
#     called, so a stale committed artefact shows up as a failure rather than being silently
#     rewritten.
set -uo pipefail
cd "$(dirname "$0")/.."

FAST=0
CI_MODE=0
case "${1:-}" in
    --fast) FAST=1 ;;
    --ci)   CI_MODE=1; FAST=1 ;;
esac

fails=0
skips=0
opt_skips=0
cannot=0
OPT=0

# run <label> <required-path...> -- <command...>
# opt_run ... : same, but a missing input is expected in CI and is not fatal.
#
# A declared path must be the thing the gate actually reads, not a directory that merely
# contains it. `captures/spice86` exists in CI and is empty, so a gate declared against the
# directory passed its precondition and then failed inside the tool -- which is how gate-suite
# went red for two weeks with six gates that could not have run anywhere else. Globs are
# allowed, and a glob that matches nothing counts as missing.
has_input() {
    local pat="$1" f
    for f in $pat; do
        [ -e "$f" ] && return 0
    done
    return 1
}

run() {
    local label="$1"; shift
    local optional="$OPT"; OPT=0
    while [ "$1" != "--" ]; do
        if ! has_input "$1"; then
            if [ "$CI_MODE" = 1 ] && [ "$optional" = 0 ]; then
                printf '  FAIL  %-26s REQUIRED gate cannot run: missing %s\n' "$label" "$1"
                fails=$((fails + 1))
            elif [ "$optional" = 1 ]; then
                printf '  SKIP* %-26s missing %s (declared optional)\n' "$label" "$1"
                opt_skips=$((opt_skips + 1))
            else
                printf '  SKIP  %-26s missing %s\n' "$label" "$1"
                skips=$((skips + 1))
            fi
            return 0
        fi
        shift
    done
    shift
    local out rc
    out=$("$@" 2>&1); rc=$?
    if [ $rc -eq 0 ]; then
        printf '  PASS  %-26s %s\n' "$label" "$(printf '%s' "$out" | tail -1 | cut -c1-72)"
    elif [ $rc -eq 77 ]; then
        # automake's "cannot run". A gate that lacks its input is not a regression,
        # and reporting it as one is how a gate gets ignored after its first false red.
        printf '  SKIP  %-26s cannot run (exit 77): %s\n' "$label" "$(printf '%s' "$out" | tail -1 | cut -c1-60)"
        cannot=$((cannot + 1))
    else
        printf '  FAIL  %-26s %s\n' "$label" "$(printf '%s' "$out" | tail -3 | tr '\n' '|' | cut -c1-160)"
        fails=$((fails + 1))
    fi
}
opt_run() { OPT=1; run "$@"; }

echo "== reconstruction proof =="
if [ "$FAST" = 1 ]; then
    echo "  SKIP  assemble_clean.sh          (--fast)"
    echo "  SKIP  engine test suite          (--fast)"
else
    run "assemble_clean.sh" original/ultimaii.exe -- bash tools/assemble_clean.sh
    run "engine test suite" engine/Makefile -- make -C engine test
fi

echo "== gates over tracked inputs (CI-runnable) =="
# The content header is committed game text, so the only thing standing between a
# silent fallback and a blank character-creation screen is the drift gate — and a
# gate that cannot go red has not been proven. Controls first, then the gate.
run "content_header selftest" engine/content/gen_content.h -- python3 engine/tests/content_header_selftest.py
run "content_header gate" engine/content/gen_content.h -- python3 engine/tools/extract/extractor.py --source asm --verify -o engine/content/gen_content.h
# The licence record compiled into the binaries. Committed generated output with no
# build rule, so the only thing keeping it honest is this: if a notice file changes
# and the header is not regenerated, the shipped binary prints a stale licence.
run "licenses_data fresh" engine/src/core/licenses_data.h -- python3 engine/tools/gen_licenses.py --verify
# The fourth committed generated header, and the only one whose generator is
# host-dependent: it uses rembg and pngquant when present and in-file fallbacks
# when not, so emitted art differs by machine. That is why this is a structural
# check of the generator's SPRITES/EDGE/PAL_N tables against the emitted header
# rather than a byte comparison, which would fail on a clean checkout and get
# ignored. engine/tools/check_dungeon_sprites.py carries the full reasoning.
run "dungeon_sprites_gen consistent" engine/src/game/dungeon_sprites_gen.h -- python3 engine/tools/check_dungeon_sprites.py
run "cmd_handlers" asm disasm/jump-table.txt -- python3 tools/cmd_handlers.py --check
# The derived half. captures/spice86/ is gitignored, so this is optional by declaration and
# SKIPs in CI rather than pretending to have checked. It is the gate that makes `exec` generated
# rather than hand-kept: the numbers in each handler header must equal what the captures say.
opt_run "cmd_provenance_derived" captures/spice86/*/spice86dumpExecutionFlow.json -- python3 tools/cmd_handlers.py --verify-provenance
# The owner's command-loop ruling (AGENTS.md § 3) needs teeth: the twelve handlers no capture
# has seen run must carry an UNPROVEN marker, and nothing may be called code-complete while one
# stands. Selftest first -- 9 controls, both polarities, including the one that proves the gate
# refuses rather than passing on a tree it cannot see.
# The engine's control-byte availability proof (docs/demo-record-playback.md D10,
# W11). Required, not optional: it reads only asm/, which CI has, and its whole job
# is to notice when a claim the demo toggle rests on has stopped being true.
run "demo_key_gate selftest" tools/demo_key_gate.py -- python3 tools/demo_key_gate.py --selftest
run "demo_key_gate" asm -- python3 tools/demo_key_gate.py

# The `# calls:` line is hand-editable prose duplicating a fact the body owns, and 601 files
# carry one. Selftest first -- 8 controls, both polarities, including the wrapped continuation
# and the lying `(+N more)` cases; the naive `call`-text rule called 212 of 601 files drift, so
# the rule is the load-bearing part and the controls are what keep it honest.
run "calls_gate selftest" tools/calls_gate.py -- python3 tools/calls_gate.py --selftest
run "calls_gate" asm -- python3 tools/calls_gate.py

# The DS offset table, generated from state.h (docs/plan-code-index.md § 6 item 2). Required, not
# optional: it reads only tracked inputs, and it is what makes "THIS STRUCT IS THE ONE TABLE FOR DS
# OFFSETS" true in code rather than asserted in a comment. Selftest first -- 6 controls, including
# the one that proves a field-shaped line the parser cannot read is a failure and not a skip.
run "state_layout selftest" tools/state_layout.py -- python3 tools/state_layout.py --selftest
run "state_layout verify" engine/src/core/state.h -- python3 tools/state_layout.py --verify
run "state_layout fresh" disasm/index/state.tsv -- python3 tools/state_layout.py --check-fresh

# The save editor takes its DS offsets from the generated table now, not from hand-copied
# numbers. Its selftest is the gate: it proves every command field resolves, that the generated
# save_offset column agrees with the base this tool assumes, and -- the part that makes it a gate
# rather than a report -- that the resolver refuses a table with a field missing, a field moved
# out of a composite span, or no rows at all. A wrong base is the dangerous failure here, because
# get() and put() share it, so a round-trip still looks fine; the base check is what catches it.
run "u2_edit selftest" disasm/index/state.tsv -- python3 tools/u2_edit.py selftest

# The coverage grade, derived from markers rather than the bulk-assigned `confidence:` stamp, plus
# the unnamed-offset queue. Required: reads only asm/ and state.h. Selftest first -- 7 controls
# over the grade derivation, including the one proving a commented forced-keep does NOT cap a
# grade; the naive reading of D3 would have capped 146 files for the wrong reason.
run "index_coverage selftest" tools/index_coverage.py -- python3 tools/index_coverage.py --selftest
run "index_coverage verify" asm disasm/index/state.tsv -- python3 tools/index_coverage.py --verify
run "index_coverage fresh" disasm/index/coverage.tsv disasm/index/unnamed-offsets.tsv -- python3 tools/index_coverage.py --check-fresh

# The `# fact:` schema and extractor (docs/plan-game-bible.md § 3). It ships with ZERO facts on
# purpose -- the thing that can fail comes before the thing that fills it -- so the 13 controls are
# the gate today, and they include the end-to-end duplicate-id case and the malformed-line case
# that must never become a silent skip.
run "index_facts selftest" tools/index_facts.py -- python3 tools/index_facts.py --selftest
run "index_facts verify" asm disasm/census -- python3 tools/index_facts.py --verify
run "index_facts fresh" disasm/facts/asm.tsv -- python3 tools/index_facts.py --check-fresh

# The footprint index (docs/plan-code-index.md § 3.1). --verify is deliberately NOT wired yet: it
# is red on four misdecode candidates (SSE and x87 mnemonics an 8086 cannot execute) and four files
# whose ES is set by a caller, and all eight need a ruling before they can be closed. A gate that
# is red on day one trains everyone to ignore it, so it goes in when it can be true.
run "index_footprint selftest" tools/index_footprint.py -- python3 tools/index_footprint.py --selftest
# Wired 2026-11-23, once the four `es:` sites were ruled rather than merely known. It had been left
# unwired while red on day one, which trains everyone to ignore the gate; the ruling (ES is a calling
# convention established above the entry point, recorded per file with its reason in ES_CONVENTION)
# is what made it safe to turn on. The controls prove it can still go red: an unexempted `es:` site
# fails, and so does a convention entry that no longer matches anything.
run "index_footprint verify" tools/index_footprint.py -- python3 tools/index_footprint.py --verify
run "index_footprint fresh" disasm/index/footprint.tsv -- python3 tools/index_footprint.py --check-fresh

# The engine map (docs/plan-code-index.md § 3.2): which C implements which asm. The citation check
# lives in xref_provenance --c rather than here on purpose -- two gates that both resolve asm/XXXX
# drift, and the rename hazard AGENTS.md names is what they would catch inconsistently.
run "xref --c" engine/src asm -- python3 tools/xref_provenance.py --c
run "index_engine_map selftest" engine/src asm -- python3 tools/index_engine_map.py --selftest
run "index_engine_map verify" engine/src asm -- python3 tools/index_engine_map.py --verify
run "index_engine_map fresh" disasm/index/engine-map.tsv -- python3 tools/index_engine_map.py --check-fresh

# The runtime call graph. Only the selftest is wired: --verify and --merge re-read all 292
# ExecutionFlow harvests, which is minutes of work for a check that only changes when a capture
# lands, and a gate that makes every commit wait teaches people to stop running the gate.
run "index_xref selftest" asm captures -- python3 tools/index_xref.py --selftest

# The filename-is-runtime / label-is-file convention. It exists because mixing the two produced a
# confident wrong header about a three-line routine on 2026-11-23; see docs/needs-gameplay.md G95.
run "label_offset_gate" asm -- python3 tools/label_offset_gate.py --verify
run "label_offset_gate selftest" asm -- python3 tools/label_offset_gate.py --selftest
# The dungeon stroke decoder. The selftest needs no game file; the read needs original/monsters,
# which is gitignored by policy, so that one is an opt_run and exits 77 when absent.
run "dungeon_strokes selftest" tools/dungeon_strokes.py -- python3 tools/dungeon_strokes.py --selftest
opt_run "dungeon_strokes reads monsters" original/monsters -- python3 tools/dungeon_strokes.py

# The dungeon spawn formula, measured 54/54 against asm/147F on the capture corpus.
# Decoded in 2026-10-29 and cited in docs/monster-spawn.md section 9, but wired into
# no runner until now, so nothing could ever go red here.
run "dungeon_spawn_check selftest" tools/dungeon_spawn_check.py -- python3 tools/dungeon_spawn_check.py --selftest
opt_run "dungeon_spawn formula" "captures/spice86/*/spice86dumpMemoryDump.bin" -- python3 tools/dungeon_spawn_check.py

# The bible loop (docs/plan-game-bible.md § 4-5). No model runs in CI (ADR 0003), so these assert
# the committed artefacts instead: every sentence has a verdict, no prose moved under a verdict,
# the recorded pair still matches the prompt files, and the three judge controls are still red. A
# prompt or model-id edit therefore turns this red until every page is re-judged -- on purpose.
run "bible selftest" tools/bible.py -- python3 tools/bible.py --selftest
run "bible verify" disasm/bible -- python3 tools/bible.py --verify

# The renderer runs no model, so this is a smoke test and not a freshness gate: it proves the joins
# behind the internals tables still resolve and that at least one page renders. The HTML itself is
# deliberately not freshness-gated (docs/plan-game-bible.md § 6) -- the gates operate on the fact
# and prose stores, because stale HTML says nothing wrong about the tree.
run "bible_render smoke" disasm/bible disasm/census -- python3 tools/bible_render.py --out /var/tmp/u2bible-render-check

run "cmd_marker_gate selftest" tools/cmd_marker_gate.py -- python3 tools/cmd_marker_gate.py --selftest
run "cmd_marker_gate" engine/src docs/phase4-state.md -- python3 tools/cmd_marker_gate.py
# Which SDL a build links is decided by this probe, and a wrong verdict is a build
# that compiles, runs, and is not the artifact the release ships. The selftest runs
# against fixtures, so it works on a machine with no SDL installed at all — which is
# the CI runner, and the place the probe is most likely to be wrong unnoticed.
run "sdl_probe selftest" engine/tools/sdl_probe.sh -- bash engine/tests/sdl_probe_selftest.sh
run "space_check selftest" engine/tests/space_check_selftest.sh -- bash engine/tests/space_check_selftest.sh
run "warp_step selftest" engine/tests/warp_step_selftest.sh -- bash engine/tests/warp_step_selftest.sh
run "shrine selftest" engine/tests/shrine_selftest.sh -- bash engine/tests/shrine_selftest.sh
# The seed module derives its offsets from state.h at runtime and cross-checks the two
# independent forms the header carries, so the selftest is the gate that keeps a drift in
# state.h from becoming a test that writes into the wrong byte and passes.
run "u2_seed selftest" engine/tests/u2_seed.py -- python3 engine/tests/u2_seed.py --selftest
# The modifier table in the key injector. A wrong control byte does not error, it sends a
# different command to the game and the capture reads a plausible wrong answer, so the
# conventions are pinned against externally checkable values (Ctrl-Z = 0x1A is the DOS EOF
# byte; Ctrl-S/Ctrl-Q are XOFF/XON).
run "spice86_type selftest" tools/spice86_type.py -- python3 tools/spice86_type.py --selftest
run "s86_ports selftest" tools/s86_ports.py -- python3 tools/s86_ports.py --selftest
# The two-target bot adapter: one DS decoder (the engine's), two executables, and an
# undeliverable key that refuses instead of pretending the game ignored it. Needs no
# emulator, which is what makes it gateable — the live proof is bot/play_original.py.
opt_run "bot target adapter" engine/u2 bot/target_check.py -- python3 bot/target_check.py
run "bot policy rule" bot/policy.py -- python3 bot/policy.py
opt_run "bot passmap (engine answers passability)" engine/u2 bot/passmap_check.py -- python3 bot/passmap_check.py
run "bot nav (search only, no rules)" bot/nav.py -- python3 bot/nav.py
run "bot census loader" bot/census.py -- python3 bot/census.py
# The mode discriminator against the corpus, not just against itself. mode_check.c (CP0)
# proves the rule is self-consistent; these prove it is TRUE of the original. The ordering
# bug it caught — a dungeon's staging pair is also all-0xFF, so eighteen dungeon frames read
# as space — was invisible to the synthetic test, which is the whole reason both exist.
run "mode vs capture corpus" bot/mode_probe.py -- python3 bot/mode_probe.py
opt_run "mode vs orbit capture" captures/spice86/w6-orbit-dive/PAD.txt -- python3 bot/mode_orbit_check.py
# Whether a container image still matches its Containerfile. The old test was "does
# an image exist", so an edited recipe was silently ignored — which cost a full
# SDL3 container cycle to a message blaming the Makefile. Runs against a podman
# stub, so it needs no daemon and no images.
run "image_fresh selftest" deploy/image_fresh.sh -- bash deploy/image_fresh_selftest.sh
# The theme builder is now `--install --themes-dir` in the shipped binary
# (engine/src/core/themes.c); the Python tool that used to do it was deleted rather
# than kept beside it, because two builders of one format is two definitions of it.
# Its gate is the engine's: install_check and install_controls run under
# `make test`, which this script runs below and CI runs in engine-tests.yml.
# No check counts are quoted here on purpose: they moved twice within a day of
# this line being written, and the authority is the suite's own output.
# The FM Towns mapping the shipped binary carries is generated from a committed TSV,
# so unlike the text header it can always be re-derived and the check is exact rather
# than best-effort. If this goes red, someone edited disasm/fmtowns-crosswalk.tsv or
# the header by hand; regenerate the header, never patch it.
run "fmtowns map header" disasm/fmtowns-crosswalk.tsv -- python3 engine/tools/fmtowns_map2h.py --verify -o engine/content/fmtowns_map.h
# The release workflow's shell is only parsed by the runner, minutes after a tag is
# pushed, and two v0.0.8 attempts died on things a local `bash -n` catches instantly
# (a missing `fi`, and an unquoted substitution). This renders every run: block with
# the matrix values filled in and syntax-checks both variants.
run "workflow shell parses" .github/workflows/release-builds.yml -- python3 tools/workflow_shellcheck.py
run "fragment_splits" disasm/function-map.tsv -- python3 tools/fragment_splits.py --selftest
run "xref_provenance" asm -- python3 tools/xref_provenance.py --strict
# Only meaningful when the binaries exist, so they are the required paths and a tree that
# never built simply skips rather than passing vacuously. This is the tripwire for the
# stale-SDL-object class of bug: two deploys today shipped binaries that did not match
# source, and neither `make` nor any gate said so.
# Declared optional, not required: gate-suite.yml has no build step, so as a required gate
# this could only ever be red there — a permanent failure that trains everyone to ignore the
# suite. It is enforced in engine-tests.yml, where a build actually exists.
opt_run "engine build fresh" engine/u2 -- make -C engine fresh
if [ "$CI_MODE" = 1 ] && [ -z "${ASM_GUARD_BASE:-}" ]; then
    # The guard compares a tree to a ref; with no base it can only pass vacuously, and it
    # now refuses rather than doing that. asm-checksum.yml is its real home because that
    # workflow passes a merge base. Skipping here is honest, not silent: --ci turns an
    # unexplained skip of a required gate into a failure, so this one explains itself.
    echo "  SKIP  asm_body_guard             no ASM_GUARD_BASE; enforced in asm-checksum.yml"
    skips=$((skips + 1))
elif [ -n "${ASM_GUARD_BASE:-}" ]; then
    run "asm_body_guard" asm -- python3 engine/tests/asm_body_guard.py --base "$ASM_GUARD_BASE"
else
    run "asm_body_guard" asm -- python3 engine/tests/asm_body_guard.py
fi
if [ "$FAST" = 1 ]; then
    echo "  SKIP  guard_selftest             (--fast: builds a throwaway worktree)"
else
    run "guard_selftest" engine/tests/asm_body_guard_selftest.sh -- bash engine/tests/asm_body_guard_selftest.sh
fi

echo "== documentation currency =="
# The docs are load-bearing: AGENTS.md tells every session to consult docs/spice86-capture.md and
# docs/tooling-procedures.md BEFORE writing a script. A page that names a renamed tool, or a tool
# whose --help prints someone else's usage, sends the session into a failure and then a
# re-derivation — the exact cost the pages exist to prevent. Two such drifts were found by accident
# on 2026-11-15, which is why this is a gate and not a wish. Selftest first, because the gate's own
# first selftest passed while checking nothing.
run "doc_command_gate selftest" tools/doc_command_gate.py -- python3 tools/doc_command_gate.py --selftest
run "doc_command_gate" docs/tooling-procedures.md -- python3 tools/doc_command_gate.py

echo "== mutation harnesses (prove the model tests can go red) =="
if [ "$FAST" = 1 ]; then
    # Deliberate: these compile the engine nine times each and need the committed
    # content header plus a build tree, which
    # only a build produces. Their CI home is engine-tests.yml, where `make -C engine` has
    # already run — not gate-suite, which has no build and would only ever skip them.
    echo "  SKIP  mutate_speed               (--fast: needs the engine build; runs in engine-tests.yml)"
    echo "  SKIP  mutate_stepgate            (--fast: needs the engine build; runs in engine-tests.yml)"
else
    run "mutate_speed" engine/content/gen_content.h -- bash engine/tests/mutate_speed.sh
    run "mutate_stepgate" engine/content/gen_content.h -- bash tools/mutate_stepgate.sh
fi

echo "== static analyzer (gcc -fanalyzer over every engine TU) =="
# Measured 9.5 s for all 72 translation units, so this is a --fast gate after all; the
# estimate of ~90 s came from a scan script that also ran the full warning set with
# -Werror. Selftest first, because the gate's first selftest passed while the analyzer
# was not running at all.
run "analyzer_gate selftest" tools/analyzer_gate.py -- python3 tools/analyzer_gate.py --selftest
run "analyzer_gate" tools/analyzer_gate.py -- python3 tools/analyzer_gate.py

echo "== DOS string atlas (needs original/, gitignored by policy) =="
opt_run "strings_atlas selftest" original/ultimaii.exe asm -- python3 tools/strings_atlas.py --selftest
opt_run "str_out_audit selftest" original/ultimaii.exe asm -- python3 tools/str_out_audit.py --selftest
opt_run "str_out_audit check" original/ultimaii.exe disasm/str-out-rendering-gap.tsv -- python3 tools/str_out_audit.py --check
# Six controls, three known-true and three known-false, all hand-verified against the bytes.
# Until now nothing ran them, which is how `begins-mid-insn` shipped a headline number the
# tool's own docstring called inflated.
opt_run "forced_keep_audit selftest" original/ultimaii.exe asm -- python3 tools/forced_keep_audit.py --selftest
# recut_bytes turns `db`-rendered code spans back into mnemonics by re-assembling each one
# with gas and comparing bytes. Its two controls pin both answers: a forward call must encode
# (it silently did not, first time, because .org cannot seek backwards) and `mov %bx,%di` must
# NOT (gas emits different bytes for it). A verifier that always says yes and one that always
# says no look identical until you test one of each.
opt_run "recut_bytes selftest" original/ultimaii.exe -- python3 tools/recut_bytes.py --selftest

# The Apple II 6502 side is deliberately NOT gated (owner ruling 2026-11-23).
# references/apple/ and the Ghidra decompilation of it are a reference corpus -- kept in case the
# lineage claim needs re-checking, not a surface this project has to keep green. Gating it bought
# nothing and produced noise: its inputs are gitignored by design, so the gates skipped in CI by
# accident and false-alarred STALE in a clean tree. The tools stay and still run by hand; see
# docs/apple2-reference.md for the ruling and what was derived with them.

# The place index needs the player's content to decode, so it is an opt leg like the other
# content-dependent gates. --verify is a control with four ways to fail: the letter table, the
# no-Q wall tile, the decoded-volume floor, and the two-signs-on-mapx31 gap check. All four
# were exercised before this line was added, because the first version of the control passed
# vacuously when Z was clipped from the mapping.
opt_run "place_index" original/mapx22 -- python3 tools/place_index.py --verify
# The census joins every generated table into one lookup. Three gates, because they catch
# different failures: --selftest proves the key gate can still go red, --verify asserts the
# invariants of the data, --check-fresh asserts that the committed tables are what the generator
# produces. The second one exists because place-index.tsv sat stale with five wiki names unjoined
# and --verify could not see it: --verify re-derives in memory and never compares against the
# committed artefact, so it was checking the generator and not the file everyone reads. The third
# exists because census asserted row uniqueness for exactly one of its eleven tables while
# creature.tsv had none at all -- and a gate that checks one table in eleven is a rumour.
run "census selftest" tools/census.py -- python3 tools/census.py --selftest
opt_run "census verify" disasm/place-index.tsv disasm/source-gamercorner-locations.csv original/ultimaii.exe -- python3 tools/census.py --verify
run "creature_hp selftest" tools/creature_hp.py -- python3 tools/creature_hp.py --selftest
opt_run "creature populations fresh" disasm/creature-populations.tsv original/monx00 "original/updated/MONG10" -- python3 tools/creature_hp.py --check-fresh
# One gate covers every committed generated table, including the census: regenerate each to a
# temp dir and byte-compare. This caught a second stale file the moment it was written --
# planet-index.tsv joins settlement names out of place-index, so it went stale when the wiki
# names landed there, and nothing noticed.
opt_run "generated fresh" tools/census.py tools/place_index.py original/ultimaii.exe -- bash tools/generated_fresh.sh
opt_run "npc_index" original/tlkx32 -- python3 tools/npc_index.py --verify
opt_run "planet_index" original/ultimaii.exe -- python3 tools/planet_index.py --verify
opt_run "gate_graph" original/ultimaii.exe -- python3 tools/gate_graph.py --verify

echo
echo "== FM Towns crosswalk (needs gitignored media) =="
# Disc-free check: validates the committed crosswalk's structure, completeness and
# letter rule. Controls proven red first: a dropped row, a broken letter mapping
# and an unknown bank all exit 1. See docs/fmtowns-graphics.md.
opt_run "fmtowns crosswalk" disasm/fmtowns-crosswalk.tsv -- python3 tools/fmtowns_xwalk.py --check disasm/fmtowns-crosswalk.tsv
opt_run "fmtowns crosswalk reproducible" disasm/fmtowns-owner-mapping.json references/fmtowns -- python3 tools/fmtowns_xwalk.py --export disasm/fmtowns-owner-mapping.json /var/tmp/u2fmt/regen.tsv

echo
if [ "$fails" -gt 0 ]; then
    echo "CHECK-ALL: FAIL ($fails gate(s) red, $skips skipped, $opt_skips optional-skipped)"
    exit 1
fi
if [ "$CI_MODE" = 1 ]; then
    echo "CHECK-ALL: PASS (CI mode; $opt_skips optional gate(s) could not run — see SKIP* above)"
    exit 0
fi
if [ "$skips" -gt 0 ]; then
    echo "CHECK-ALL: PASS ($skips gate(s) skipped for missing inputs — see above)"
    exit 0
fi
if [ "$cannot" -gt 0 ]; then
    echo "CHECK-ALL: PASS ($cannot gate(s) reported exit 77: input present but unusable — see SKIP above)"
    exit 0
fi
echo "CHECK-ALL: PASS"

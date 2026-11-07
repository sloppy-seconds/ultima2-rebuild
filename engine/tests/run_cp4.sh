#!/usr/bin/env bash
# tests/run_cp4.sh — CP4 acceptance test: overworld + movement.
#
# The gate for CP4 is an owner playthrough ("walk around a map"). What can be
# proven mechanically first is the sector model: the map is runtime data read
# from the player's own files, so the loader has to get sizes, indexing, naming
# and write-back right without a single game byte in the repository.
set -uo pipefail

cd "$(dirname "$0")/.." || exit 1
fail=0
ok()  { printf '  PASS  %s\n' "$1"; }
bad() { printf '  FAIL  %s\n' "$1"; fail=1; }

WORK=$(mktemp -d "${TMPDIR:-/var/tmp}/u2-cp4.XXXXXX")
trap 'rm -rf "$WORK"' EXIT

printf 'engine CP4 test\n'

# --- the sector loader ----------------------------------------------------
cc -std=c11 -Wall -Wextra -Werror -Isrc -o "$WORK/world_check" \
   tests/world_check.c src/core/world.c src/core/roots.c src/core/slot.c 2>"$WORK/cc.err"
if [ $? -ne 0 ]; then
    bad "world test harness did not build: $(head -5 "$WORK/cc.err")"
else
    "$WORK/world_check" "$WORK" > "$WORK/world.out" 2>&1 \
        && ok "sector model: sizes, indexing, naming, write-back ($(grep -c '  ok  ' "$WORK/world.out") checks)" \
        || { bad "world check failed:"; sed 's/^/        /' "$WORK/world.out"; }
fi

# --- the time doors -------------------------------------------------------
# The gate table, the two observed era pairings, and the rotation. The spec is written
# to reject invention: slots 0 and 3 have never been transited and must report unknown.
# Evidence: docs/moongates.md, captures/spice86/moongate-cycle.
cc -std=c11 -Wall -Wextra -Werror -Isrc -o "$WORK/gate_check" \
   tests/gate_check.c src/game/gates.c src/core/world.c src/core/roots.c src/core/slot.c src/core/state.c 2>"$WORK/cc-gate.err"
if [ $? -ne 0 ]; then
    bad "gate test harness did not build: $(head -5 "$WORK/cc-gate.err")"
else
    GATE_EXE=""
    for c in ../original/ultimaii.exe ../original/ULTIMAII.EXE; do
        if [ -r "$c" ]; then GATE_EXE="$c"; break; fi
    done
    "$WORK/gate_check" "$GATE_EXE" > "$WORK/gate.out" 2>&1 \
        && ok "time doors: table, pairings, rotation ($(grep -c '  ok  ' "$WORK/gate.out") checks)" \
        || { bad "gate check failed:"; sed 's/^/        /' "$WORK/gate.out"; }
fi

# --- the tile extractor ---------------------------------------------------
# The art is game content and is never committed, so it has to arrive from the
# player's own executable at run time. The synthetic-image half of this test
# always runs and proves the assertions reject a wrong layout; the real-binary
# half SKIPs when no copy is here, so CI stays green and honest.
cc -std=c11 -Wall -Wextra -Werror -Isrc -o "$WORK/tiles_check" \
   tests/tiles_check.c src/core/tiles.c src/core/roots.c src/core/sha256.c 2>"$WORK/cc-tiles.err"
if [ $? -ne 0 ]; then
    bad "tiles test harness did not build: $(head -5 "$WORK/cc-tiles.err")"
else
    REAL_EXE=""
    for c in ../original/ultimaii.exe ../original/ULTIMAII.EXE; do
        if [ -r "$c" ]; then REAL_EXE="$c"; break; fi
    done
    if "$WORK/tiles_check" "$WORK" "$REAL_EXE" > "$WORK/tiles.out" 2>&1; then
        n=$(grep -c '  ok  ' "$WORK/tiles.out")
        if [ -n "$REAL_EXE" ]; then
            ok "tile extractor: invariants, cache round-trip, real binary ($n checks)"
        else
            ok "tile extractor: invariants and cache round-trip ($n checks, real-binary SKIP)"
        fi
    else
        bad "tiles check failed:"
        sed 's/^/        /' "$WORK/tiles.out"
    fi
fi

# --- the generated cleanroom art ---------------------------------------------
# Two separate guarantees, because a generated file fails quietly. First the
# committed header must still match the SVGs it came from: hand-editing it, or
# changing the art and forgetting to regenerate, both leave a header that looks
# fine and ships stale bytes. Second the bytes must decode through the engine's
# own u2_tile_pixel() into the pixels the SVGs actually contain, which size
# assertions alone would not catch.
ART_SRC=assets/tiles/cleanroom/tiles
python3 tools/tiles2h.py --src "$ART_SRC" -o "$WORK/tiles_cleanroom.h" \
    >"$WORK/gen-art.out" 2>&1
if [ $? -ne 0 ]; then
    bad "cleanroom art generator failed: $(tail -3 "$WORK/gen-art.out")"
elif ! cmp -s "$WORK/tiles_cleanroom.h" src/core/tiles_cleanroom.h; then
    bad "src/core/tiles_cleanroom.h does not match the SVG sources. Regenerate:"
    echo "        (cd engine && python3 tools/tiles2h.py --src assets/tiles/cleanroom/tiles -o src/core/tiles_cleanroom.h)" >&2
    diff "$WORK/tiles_cleanroom.h" src/core/tiles_cleanroom.h | head -10 >&2
else
    ok "cleanroom art: committed header matches the SVG sources (no drift)"
fi

cc -std=c11 -Wall -Wextra -Werror -Isrc -o "$WORK/tiles_art_check" \
   tests/tiles_art_check.c src/core/tiles_builtin.c 2>"$WORK/cc-art.err"
if [ $? -ne 0 ]; then
    bad "cleanroom art harness did not build: $(head -5 "$WORK/cc-art.err")"
elif "$WORK/tiles_art_check" > "$WORK/art.out" 2>&1; then
    ok "cleanroom art: $(tail -1 "$WORK/art.out")"
else
    bad "cleanroom art check failed:"
    sed 's/^/        /' "$WORK/art.out"
fi

# --- the tile rules gate -------------------------------------------------------
# Structural conformance of the substitute art to the requirements the owner stated in
# docs/tile-art-spec.md § 7a: a ring must be hollow, a pass must be open at the bottom, a
# signpost must have one crossbar rather than two, a letter tile must carry its rules.
# Geometry the eye judges badly at 16x16 -- detached fragments that read as confetti, seams
# that break a repeating pattern -- is exactly what these checks are good at.
#
# This gate is NOT shown to whoever draws the art. Its bands were fitted to the originals, so
# reading it would be reading the answer key; see § 2 of the spec and PROVENANCE.md.
# Tiles awaiting a clean redraw are listed in PENDING_REDRAW and reported as visible debt
# rather than failure, so the suite is green now and goes red if a redraw breaks a rule that
# tile already passes. The pending list is hardcoded on purpose: deriving it from the art
# would let every failing tile classify itself as pending and make the gate vacuous.
if python3 ../tools/tile_rules.py --src "$ART_SRC" > "$WORK/rules.out" 2>&1; then
    ok "$(head -1 "$WORK/rules.out")"
    grep 'NOTE' "$WORK/rules.out" | sed 's/^/        /' || true
else
    bad "tile rules gate failed:"
    sed 's/^/        /' "$WORK/rules.out"
fi

# --- the viewport mapping -------------------------------------------------
# The viewport is where a map byte becomes something drawable, so the whole CP4
# render contract rests on these rules: geometry, the index formula, the
# off-sector fallback and the underlay suppression.
cc -std=c11 -Wall -Wextra -Werror -Isrc -o "$WORK/view_check" \
   tests/view_check.c src/core/view.c 2>"$WORK/cc-view.err"
if [ $? -ne 0 ]; then
    bad "viewport test harness did not build: $(head -5 "$WORK/cc-view.err")"
else
    if "$WORK/view_check" > "$WORK/view.out" 2>&1; then
        ok "viewport: geometry, index formula, off-sector, suppression ($(grep -c '  ok  ' "$WORK/view.out") checks)"
    else
        bad "viewport check failed:"
        sed 's/^/        /' "$WORK/view.out"
    fi
fi

# --- window scaling -------------------------------------------------------

# The SDL backend fits the 320x200 frame into a resized window by integer scale.
# The fit is pure integer arithmetic in core/scale.c, so it is tested here in the
# cc-only build with no SDL and no display, on exactly the code the backend calls.
cc -std=c11 -Wall -Wextra -Werror -Isrc -o "$WORK/scale_check" \
   tests/scale_check.c src/core/scale.c 2>"$WORK/cc-scale.err"
if [ $? -ne 0 ]; then
    bad "scale test harness did not build: $(head -5 "$WORK/cc-scale.err")"
else
    if "$WORK/scale_check" > "$WORK/scale.out" 2>&1; then
        ok "window scaling: integer fit, aspect, centring, degenerate sizes ($(grep -c '  ok  ' "$WORK/scale.out") checks)"
    else
        bad "scale check failed:"
        sed 's/^/        /' "$WORK/scale.out"
    fi
fi

# --- the world coordinate model ----------------------------------------------

# Orbit, hyperwarp and dive. Pure integer rules in game/space.c with no SDL and no
# display, so the same code a command handler will call is what runs here. The
# evidence is captures/spice86/g39-rocket-flight, the first session ever inside a
# rocket, and the descent assertions are the interesting ones: that rule was written
# from bytes before anyone had launched, and then predicted both live landings to the
# cell. Mutants are driven by tests/space_check_selftest.sh.
cc -std=c11 -Wall -Wextra -Werror -Isrc -o "$WORK/space_check" \
   tests/space_check.c src/game/space.c 2>"$WORK/cc-space.err"
if [ $? -ne 0 ]; then
    bad "space test harness did not build: $(head -5 "$WORK/cc-space.err")"
else
    if "$WORK/space_check" > "$WORK/space.out" 2>&1; then
        ok "space model: world tables, off-course gate, dive sector, descent ($(grep -c '  PASS  ' "$WORK/space.out") checks)"
    else
        bad "space check failed:"
        sed 's/^/        /' "$WORK/space.out"
    fi
fi

# --- the framebuffer ------------------------------------------------------
# Every display backend sits behind this, so a mistake here reaches all of them.
cc -std=c11 -Wall -Wextra -Werror -Isrc -o "$WORK/frame_check" \
   tests/frame_check.c src/core/frame.c src/core/view.c 2>"$WORK/cc-frame.err"
if [ $? -ne 0 ]; then
    bad "framebuffer test harness did not build: $(head -5 "$WORK/cc-frame.err")"
else
    if "$WORK/frame_check" > "$WORK/frame.out" 2>&1; then
        ok "framebuffer: blit, clipping, compositing, PPM/BMP ($(grep -c '  ok  ' "$WORK/frame.out") checks)"
    else
        bad "framebuffer check failed:"
        sed 's/^/        /' "$WORK/frame.out"
    fi
fi

# --- the engine font -------------------------------------------------------
# CP4b: text in the framebuffer is drawn with the vendored public-domain
# 8x8 font, so every string screen (status band, menu, creation) rests on
# these glyph semantics.
cc -std=c11 -Wall -Wextra -Werror -Isrc -o "$WORK/font_check" \
   tests/font_check.c src/core/font.c src/core/frame.c src/core/view.c 2>"$WORK/cc-font.err"
if [ $? -ne 0 ]; then
    bad "font test harness did not build: $(head -5 "$WORK/cc-font.err")"
else
    if "$WORK/font_check" > "$WORK/font.out" 2>&1; then
        ok "engine font: glyphs, grid, clipping, determinism ($(grep -c '  ok  ' "$WORK/font.out") checks)"
    else
        bad "font check failed:"
        sed 's/^/        /' "$WORK/font.out"
    fi
fi

# --- the status band and the CMD message window ----------------------------
# CP4b task 1: the owner's model of the block — four rows, live prompt at the
# bottom, completed commands scrolling up, status block outside the scroll
# rect — as tested behaviour, not folklore.
cc -std=c11 -Wall -Wextra -Werror -Isrc -Icontent -o "$WORK/status_check" \
   tests/status_check.c src/game/status.c src/core/seam.c src/core/vram.c \
   src/core/state.c src/core/demo.c src/core/font.c src/core/frame.c src/core/view.c \
   src/core/world.c src/core/roots.c src/core/slot.c \
   src/core/rng.c src/core/timer.c src/core/replay.c src/core/sha256.c \
   src/debug/trace.c src/debug/dump_state.c 2>"$WORK/cc-status.err"
if [ $? -ne 0 ]; then
    bad "status test harness did not build: $(head -5 "$WORK/cc-status.err")"
else
    if "$WORK/status_check" > "$WORK/status.out" 2>&1; then
        ok "status band + CMD window: BCD lines, prompt, scroll history ($(grep -c '  ok  ' "$WORK/status.out") checks)"
    else
        bad "status check failed:"
        sed 's/^/        /' "$WORK/status.out"
    fi
fi

# --- the Q save round trip --------------------------------------------------
# The owner's test: walk, Q saves the live position into the record, restart,
# P lands where the player stood. Plus the outdoors guard and the CP11
# save-anywhere policy hook.
cc -std=c11 -Wall -Wextra -Werror -Isrc -Icontent -o "$WORK/save_q_check" \
   tests/save_q_check.c src/game/overworld.c src/core/panel.c src/game/commands.c src/game/space.c src/debug/turnlog.c src/game/ai.c src/game/combat.c src/game/monsters.c src/game/gates.c src/game/commerce.c src/game/dungeon.c src/game/flash.c src/game/feedback.c src/core/tiles_builtin.c src/game/status.c src/core/world.c src/core/roots.c src/core/slot.c \
   src/core/save.c src/core/tiles.c src/core/tiles_ega.c src/core/ini.c src/core/sha256.c src/core/seam.c src/core/demo.c \
   src/core/vram.c src/core/state.c src/core/font.c src/core/frame.c \
   src/core/view.c src/core/rng.c src/core/timer.c src/core/replay.c \
   src/debug/trace.c src/debug/dump_state.c 2>"$WORK/cc-saveq.err"
if [ $? -ne 0 ]; then
    bad "save-Q test harness did not build: $(head -5 "$WORK/cc-saveq.err")"
else
    if "$WORK/save_q_check" "$WORK/saveq" > "$WORK/saveq.out" 2>&1; then
        ok "Q save round trip: guards, dirty rule, policy hook, P restore ($(grep -c '  ok  ' "$WORK/saveq.out") checks)"
    else
        bad "save-Q check failed:"
        sed 's/^/        /' "$WORK/saveq.out"
    fi
fi

# --- time passes: the PASS action spends food, starvation kills -----------
# file 0x0B0D spends 10 food per PASS and file 0x0B2E sends a borrow to the
# death handler at runtime 0x0E62 (asm/0C62), which zeroes DS:51..59.
cc -std=c11 -Wall -Wextra -Werror -Isrc -Icontent -o "$WORK/food_check" \
   tests/food_check.c src/game/overworld.c src/core/panel.c src/game/commands.c src/game/space.c src/debug/turnlog.c src/game/ai.c src/game/combat.c src/game/monsters.c src/game/gates.c src/game/commerce.c src/game/dungeon.c src/game/flash.c src/game/feedback.c src/core/tiles_builtin.c src/game/status.c src/core/world.c src/core/roots.c src/core/slot.c \
   src/core/save.c src/core/tiles.c src/core/tiles_ega.c src/core/ini.c src/core/sha256.c src/core/seam.c src/core/demo.c \
   src/core/vram.c src/core/state.c src/core/font.c src/core/frame.c \
   src/core/view.c src/core/rng.c src/core/timer.c src/core/replay.c \
   src/debug/trace.c src/debug/dump_state.c 2>"$WORK/cc-food.err"
if [ $? -ne 0 ]; then
    bad "food test harness did not build: $(head -5 "$WORK/cc-food.err")"
else
    if "$WORK/food_check" > "$WORK/food.out" 2>&1; then
        ok "food tick: PASS spends 10, underflow zeroes every stat ($(tail -1 "$WORK/food.out"))"
    else
        bad "food check failed:"
        sed 's/^/        /' "$WORK/food.out"
    fi
fi

# --- G2: the per-step charge is inside the passability call, so refusals pay --
# asm/2599 and asm/2626 subtract 19 from the food triple ahead of the tile verdict
# inside asm/2423, so `--INVALID MOVE!` is not a free refusal. Measured 2026-10-25
# on the maxed seed in sector 31: one arrow into a blocked cell, DS:00/01 unchanged,
# food down exactly 19, screen `CMD: WEST--INVALID MOVE!`.
#
# The observable is death, not the FOOD digits: the band is only repainted at the
# sites asm/5129 is called from, so a quiet one-unit charge never reaches the
# screen. Thirteen food units and one step makes the charge itself observable — and
# it discriminates the amount, because the port once passed decimal 19 where the
# asm's immediate 0x19 was meant and charged thirteen, which survives.
for arm in water-poor grass-poor water-rich none-poor; do
    python3 tests/cp5_fight_seed.py "$WORK/g2-$arm" >/dev/null 2>&1
    python3 tests/g2_step_food.py "$WORK/g2-$arm" "$arm" >/dev/null 2>&1
    : > "$WORK/g2-$arm.keys"
    if [ "$arm" != "none-poor" ]; then
        printf '0xf2\n' > "$WORK/g2-$arm.keys"    # 0xf2 is U2_KEY_LEFT: step west
    fi
    ./u2 --headless --play-map --cleanroom \
         --data-dir "$WORK/g2-$arm/content" --save-dir "$WORK/g2-$arm/save" \
         --script "$WORK/g2-$arm.keys" --dump-state > "$WORK/g2-$arm.out" 2>&1
done
if grep -q "IS DEAD!" "$WORK/g2-water-poor.out"; then
    if grep -q "IS DEAD!" "$WORK/g2-grass-poor.out"; then
        if grep -q -- "WEST--INVALID MOVE!" "$WORK/g2-water-rich.out" \
           && ! grep -q "IS DEAD!" "$WORK/g2-water-rich.out"; then
            if grep -q "IS DEAD!" "$WORK/g2-none-poor.out"; then
                bad "CP4 G2 control: the no-input arm died — the gate cannot fail"
            else
                ok "step charge: an accepted and a refused step both cost 19 (13 units dies either way); the control survives"
            fi
        else
            bad "CP4 G2: the water arm did not read as a refused-but-surviving step — the reject path is not what the cost arms travelled"
            sed 's/^/        /' "$WORK/g2-water-rich.out" | tail -6
        fi
    else
        bad "CP4 G2: the accepted step did not kill on 13 food — the move charge is missing or under-sized"
    fi
else
    bad "CP4 G2: a refused step cost no food — asm/2423 charges ahead of the verdict and the engine must too"
    sed 's/^/        /' "$WORK/g2-water-poor.out" | tail -6
fi

# --- the map actually renders, when the player has their files ------------
# The point of the whole CP4 chain: sector + tiles + viewport + framebuffer
# producing real pixels. Skips without the content, so CI stays honest.
MAPDIR=""
for c in ../original ../original/updated; do
    if [ -r "$c/ultimaii.exe" ] && [ -r "$c/mapx30" ]; then MAPDIR="$c"; break; fi
done
if [ -n "$MAPDIR" ]; then
    if ./u2 --data-dir "$MAPDIR" --save-dir "$WORK/saves" \
            --dump-map "$WORK/map.ppm" >"$WORK/dump.err" 2>&1; then
        sz=$(wc -c < "$WORK/map.ppm" 2>/dev/null || echo 0)
        if [ "$sz" -eq 192015 ]; then
            ok "--dump-map renders the real sector to 320x200 pixels ($sz B)"
        else
            bad "--dump-map wrote $sz bytes, expected 192015 for a 320x200 PPM"
        fi
        # Second run must come from the cache the first run created.
        if grep -q 'tiles from cache' <(./u2 --data-dir "$MAPDIR" \
                --save-dir "$WORK/saves" --dump-map "$WORK/map2.ppm" 2>&1); then
            ok "second run serves tiles from the cache it bootstrapped"
            cmp -s "$WORK/map.ppm" "$WORK/map2.ppm" \
                && ok "cached and extracted frames are byte-identical" \
                || bad "cached frame differs from the extracted one"
        else
            bad "second run re-extracted: the cache did not bootstrap"
        fi
    else
        bad "--dump-map failed: $(tail -2 "$WORK/dump.err")"
    fi
else
    printf '  SKIP  no player content: --dump-map not exercised\n'
fi

# --- the PIC unpack -----------------------------------------------------------
# The original reads its splash pictures straight into CGA video memory, so the engine's
# unpack has to get the even/odd field interleave exactly right — and a global colour
# census cannot tell the right layout from a swapped one, which is why the checks are
# row- and quadrant-level. Needs the player's own picdra, so it skips without the content.
if [ -r "$MAPDIR/picdra" ] || [ -r "$MAPDIR/PICDRA" ]; then
    cc -std=c11 -Wall -Wextra -Werror -Isrc -Icontent -o "$WORK/pic_check" \
       tests/pic_check.c src/core/pic.c src/core/roots.c src/core/frame.c src/core/view.c src/debug/trace.c \
        >"$WORK/cc-pic.err" 2>&1 \
        && "$WORK/pic_check" >"$WORK/pic.out" 2>&1 \
        && ok "PIC unpack: 16 checks, interleave pinned by scanline and quadrant census" \
        || { bad "PIC unpack failed:"; tail -12 "$WORK/pic.out" 2>/dev/null | sed 's/^/        /'; tail -4 "$WORK/cc-pic.err" | sed 's/^/        /'; }
else
    printf '  SKIP  no player content: PIC unpack not exercised\n'
fi

# --- the EGA 16-colour pipeline ----------------------------------------------
# --ega adds a 16-colour path: the EGA palette, a 4bpp tile blit, the EGATILES
# atlas loader and the linear PIC*-E loader. The pure-pipeline checks (palette
# table, blit decode, 16-entry writer) always run; the atlas and pic checks SKIP
# inside the binary when the fan pack is absent (CI has no references/ or
# original/updated/), so this block runs unconditionally and never goes red for a
# missing pack. See docs/ega-graphics-plan.md.
cc -std=c11 -Wall -Wextra -Werror -Isrc -Icontent -o "$WORK/ega_check" \
   tests/ega_check.c src/core/frame.c src/core/view.c src/core/tiles_ega.c src/core/ini.c \
   src/core/pic_ega.c src/core/pic.c src/core/roots.c src/debug/trace.c \
    >"$WORK/cc-ega.err" 2>&1 \
    && "$WORK/ega_check" >"$WORK/ega.out" 2>&1 \
    && ok "EGA pipeline: palette, 4bpp blit, atlas + PIC*-E loaders ($(grep -c '^  ok' "$WORK/ega.out") checks)" \
    || { bad "EGA pipeline failed:"; tail -16 "$WORK/ega.out" 2>/dev/null | sed 's/^/        /'; tail -4 "$WORK/cc-ega.err" | sed 's/^/        /'; }

# Format v2 (docs/handoff-themes-builder.md § 4): a theme with a manifest, so the
# loader has geometry, a tile pool, an indirection table and a palette to get
# wrong. Most of this check is controls — six themes that MUST be refused — because
# a format that accepts a bad file and draws a guess is how a plausible wrong
# screen reaches a player. The fixtures are written under /var/tmp, so the check
# runs in CI with no media; only the FM Towns letter-row assertions skip when
# themes/fmtowns has not been built, and they say so out loud.
cc -std=c11 -Wall -Wextra -Werror -Isrc -Icontent -o "$WORK/theme_v2_check" \
   tests/theme_v2_check.c src/core/frame.c src/core/view.c src/core/tiles_ega.c src/core/ini.c \
   src/core/font.c src/core/vram.c src/debug/trace.c \
    >"$WORK/cc-v2.err" 2>&1 \
    && "$WORK/theme_v2_check" >"$WORK/v2.out" 2>&1 \
    && ok "theme format v2: geometry, map indirection, palette, 6 refusals ($(grep -c '^  ok' "$WORK/v2.out") checks)" \
    || { bad "theme format v2 failed:"; tail -20 "$WORK/v2.out" 2>/dev/null | sed 's/^/        /'; tail -4 "$WORK/cc-v2.err" | sed 's/^/        /'; }

# --- which art a run gets when it says nothing -----------------------------
# An --install tree holding themes used to start in CGA unless the player typed
# --ega, so the better art sat on disk unused; and with both the derived FM Towns
# theme and the fan pack's `default` installed, `default` won, which is backwards.
# The gate drives both lookup paths (a configured dir, and `themes` beside the
# launch directory) and both properties that could do damage: it must not fire when
# the player named anything, and it must not fire in a tree with no themes, because
# the CP2 byte-comparison baseline is measured in original CGA mode. Fixtures are
# v1 EGATILES files, so this needs no art and no media and runs in CI.
rm -rf "$WORK/artauto"
mkdir -p "$WORK/artauto"
if cc -std=c11 -Wall -Wextra -Werror -Isrc -Icontent -o "$WORK/artauto_check" \
   tests/artauto_check.c src/core/tiles_ega.c src/core/ini.c src/core/frame.c \
   src/core/view.c src/core/state.c src/core/rng.c >"$WORK/cc-artauto.err" 2>&1 \
   && "$WORK/artauto_check" "$WORK/artauto" >"$WORK/artauto.out" 2>&1; then
    n=$(sed -n 's/^  ok  \([0-9]\{1,\}\) checks.*/\1/p' "$WORK/artauto.out")
    [ "${n:-0}" -gt 0 ] \
        && ok "art auto-selection: fmtowns preferred, explicit wins, empty stays CGA ($n checks)" \
        || bad "art auto-selection run executed 0 checks — the test is not running"
else
    bad "art auto-selection check failed:"
    tail -20 "$WORK/artauto.out" 2>/dev/null | sed 's/^/        /'
    tail -4 "$WORK/cc-artauto.err" | sed 's/^/        /'
fi

# ...and the same rule through config.ini, which the unit test above cannot see
# because it drives the theme layer and the defect lives in main()'s argument order.
# A config that names a theme must select themed art, exactly as --ega-theme does;
# the first version set only the name, so `theme = fmtowns` produced CGA -- worse
# than silence. This was found on a deployed tree, not in a fixture.
rm -rf "$WORK/cfgtheme"; mkdir -p "$WORK/cfgtheme/themes/default"
# A real v2 theme, copied from the repo's own tree (../themes, not ./themes: the
# script's cwd is engine/). An empty fixture directory makes the strict launch
# check refuse, and a refusal prints no art-mode line, which would fail the gate
# for a fixture reason rather than the one under test.
cp ../themes/default/THEME.INI ../themes/default/TILES \
   "$WORK/cfgtheme/themes/default/" 2>/dev/null
if [ ! -s "$WORK/cfgtheme/themes/default/TILES" ]; then
    printf '  SKIP  no ../themes/default to build a config fixture from\n'
else
printf '[themes]\ndir = themes\ntheme = default\n' > "$WORK/cfgtheme/config.ini"
U2_ABS="$PWD/u2"   # the checks below cd into the fixture, so ./u2 would not resolve
cfg_line=$( (cd "$WORK/cfgtheme" && "$U2_ABS" --headless --config config.ini 2>&1 | grep '^u2: art mode' | head -1) )
case "$cfg_line" in
    *ega*) ok "config.ini naming a theme selects the EGA axis: $cfg_line" ;;
    *)     bad "config.ini named a theme but the run is not EGA: '${cfg_line:-no art-mode line}'" ;;
esac
# And a config that names no theme, in a tree with no themes at all, must stay on
# CGA. The directory matters: pointed at the fixture above it would correctly find
# themes/default and select EGA, which is the auto-detection working, not failing.
rm -rf "$WORK/cfgbare"; mkdir -p "$WORK/cfgbare"
printf '[paths]\ndata = .\n' > "$WORK/cfgbare/config.ini"
cfg_bare=$( (cd "$WORK/cfgbare" && "$U2_ABS" --headless --config config.ini 2>&1 | grep '^u2: art mode' | head -1) )
case "$cfg_bare" in
    *cga*) ok "a config naming no theme leaves the run on CGA: $cfg_bare" ;;
    *)     bad "a themeless config should not have selected EGA: '${cfg_bare:-no art-mode line}'" ;;
esac
fi

# --- config.ini -------------------------------------------------------------
# The one place a shipped binary learns where content lives, so the property under
# test is that it cannot lie: an unknown key, a misplaced key, a duplicated key, a
# path that is not a directory and an over-long value are all refusals rather than
# silent defaults, and precedence is flag > ini > nothing. The fixtures are written
# under $WORK, so this runs in CI with no media. docs/install-design.md § 4.
cc -std=c11 -Wall -Wextra -Werror -Isrc -o "$WORK/config_check" \
   tests/config_check.c src/core/config.c src/core/ini.c >"$WORK/cc-cfg.err" 2>&1 \
    && "$WORK/config_check" "$WORK" >"$WORK/cfg.out" 2>&1 \
    && ok "config.ini: precedence, path resolution, 8 refusals ($(grep -c '^  ok' "$WORK/cfg.out") checks)" \
    || { bad "config.ini check failed:"; tail -20 "$WORK/cfg.out" 2>/dev/null | sed 's/^/        /'; tail -4 "$WORK/cc-cfg.err" | sed 's/^/        /'; }

# --- --install ---------------------------------------------------------------
# The gate from docs/install-design.md § 7, run against synthesised release and
# patch fixtures so it works in CI with no media. It asserts the installed tree is
# exactly the expected file set, every copy is byte-identical, the sources come out
# untouched, a re-run is a no-op, --check goes red on a missing file, a stray and a
# changed non-sector file, and --check treats a played-in sector as drift (D6).
# The assertion that matters most is the last: the engine pointed at the installed
# tree with no other flags loads Earth AND an outer planet, which is the only one
# that catches "copied everything, the planets still do not work".
cc -std=c11 -Wall -Wextra -Werror -Isrc -Icontent -o "$WORK/install_check" \
   tests/install_check.c src/core/install.c src/core/themes.c src/core/fmtowns.c \
   src/core/copy.c src/core/roots.c src/core/sha256.c \
   src/core/world.c src/core/slot.c src/core/config.c src/core/ini.c >"$WORK/cc-inst.err" 2>&1 \
    && "$WORK/install_check" "$WORK" >"$WORK/inst.out" 2>&1 \
    && ok "--install: layout, manifest, idempotence, --check refusals ($(grep -c '^  ok' "$WORK/inst.out") checks)" \
    || { bad "--install check failed:"; tail -25 "$WORK/inst.out" 2>/dev/null | sed 's/^/        /'; tail -4 "$WORK/cc-inst.err" | sed 's/^/        /'; }

# --- the controls that prove the two gates above can go red --------------------
# Each breaks one property in a throwaway copy of config.c or install.c, rebuilds
# the real check against it, and requires the check to go red on exactly the
# assertions that own that property. A control that does not compile, crashes, or
# goes red for a fixture reason is reported as broken rather than as a pass.
python3 tests/install_controls.py "$WORK/controls" >"$WORK/ctl.out" 2>&1 \
    && ok "$(cat "$WORK/ctl.out")" \
    || { bad "installer controls are not all able to fail:"; sed 's/^/        /' "$WORK/ctl.out"; }

# --- content is immutable once installed ---------------------------------------
# docs/save-scoped-worlds.md § 5 calls this the property worth gating hardest, and
# it is the one nothing else can see: the write-back is faithful to asm/2305, so
# what changed is only *where* the bytes land, and a wrong destination is invisible
# on screen. Hash the content tree, play a session that writes a sector, hash again.
# The gate carries its own red control and runs it every time, because a comparison
# that cannot fail is the failure mode AGENTS.md § 3 exists to stop.
python3 tests/content_immutable.py "$WORK/immutable" >"$WORK/imm.out" 2>&1 \
    && ok "content immutable after install: a played session writes only the save slot" \
    || { bad "content-immutability gate failed:"; sed 's/^/        /' "$WORK/imm.out"; }

# The v2 player marker gets the same treatment for the same reason. The raw-blit
# bug passed every check the format had, because every theme in existence at the
# time had an identity map; the assertion that would have caught it is only worth
# having if it can still fail, so this restores the bug and requires red.
python3 tests/overlay_map_control.py "$WORK/overlay" >"$WORK/ovl.out" 2>&1 \
    && ok "$(cat "$WORK/ovl.out")" \
    || { bad "overlay map control is not able to fail:"; sed 's/^/        /' "$WORK/ovl.out"; }

# --- the FM Towns derivation ---------------------------------------------------
# core/fmtowns.c is a port of the Python derivation, and the only proof a generator
# port has is byte equality. The disc is gitignored, so the gate is split: THEME.INI,
# MAP.TSV and PALETTE.RGB depend only on the crosswalk and the palette, so a
# synthetic ISO carrying filler banks must reproduce them at the same sizes and the
# pool must be numbered identically -- that runs everywhere. TILES depends on the
# art, so it is checked structurally here and byte-for-byte against the Python
# output when references/fmtowns is present, and it says so out loud when it skips.
cc -std=c11 -Wall -Wextra -Werror -Isrc -Icontent -o "$WORK/fmtowns_check" \
   tests/fmtowns_check.c src/core/fmtowns.c src/core/copy.c src/core/sha256.c \
    >"$WORK/cc-fm.err" 2>&1 \
    && "$WORK/fmtowns_check" "$WORK" ../references/fmtowns ../themes/fmtowns \
         >"$WORK/fm.out" 2>&1 \
    && ok "FM Towns derivation: synthetic ISO, pool order, palette, refusals ($(grep -c '^  ok' "$WORK/fm.out") checks)" \
    || { bad "FM Towns derivation check failed:"; tail -25 "$WORK/fm.out" 2>/dev/null | sed 's/^/        /'; tail -4 "$WORK/cc-fm.err" | sed 's/^/        /'; }
if grep -q '^  SKIP' "$WORK/fm.out" 2>/dev/null; then
    echo "  info  $(grep '^  SKIP' "$WORK/fm.out" | sed 's/^  SKIP  //')"
fi

# --- the site viewport gate -------------------------------------------------
# A site sector must not draw the world map's wrap, and the mode that decides it is
# derived from the sector's ones digit rather than from the act of entering one, so
# a save loaded inside a town has to come back in site mode. The gate builds its own
# Port Boniface save, renders it, and checks the three rows the original draws past
# the edge. Its selftest runs whether or not the content is present, so the checker
# is never silently unexercised; the render itself SKIPs loudly without original/.
if python3 tests/site_viewport_gate.py --selftest >"$WORK/sitegate.self" 2>&1; then
    ok "site viewport gate selftest: all five controls can go red"
else
    bad "site viewport gate selftest failed:"
    sed 's/^/        /' "$WORK/sitegate.self"
fi
if python3 tests/site_viewport_gate.py >"$WORK/sitegate.out" 2>&1; then
    if grep -q '^SKIP' "$WORK/sitegate.out"; then
        printf '  SKIP  %s\n' "$(tail -1 "$WORK/sitegate.out" | sed 's/^SKIP  //')"
    else
        ok "$(tail -1 "$WORK/sitegate.out" | sed 's/^PASS  //')"
    fi
else
    bad "site viewport gate: $(tail -1 "$WORK/sitegate.out" | sed 's/^FAIL  //')"
fi

# --- --data-dir is part of the documented contract ----------------------
./u2 --help 2>&1 | grep -q -- '--data-dir' \
    && ok "--data-dir is in the usage text" \
    || bad "--data-dir missing from usage"

# --- zero game content committed ----------------------------------------
# The whole point of the CP4 decision: the map never enters the repo, so the
# loader must carry no tile tables and no baked sector bytes.
if grep -qE '0x[0-9a-fA-F]{2},[[:space:]]*0x[0-9a-fA-F]{2},[[:space:]]*0x' src/core/world.c src/core/world.h src/core/tiles.c src/core/tiles.h; then
    bad "core/world.[ch] or core/tiles.[ch] contains an inline byte table — map and tile bytes must come from the player's files"
else
    ok "no tile bytes baked into the sector model"
fi

if find . -path ./build -prune -o -name 'mapx*' -print -o -name 'monx*' -print | grep -q .; then
    bad "a mapx*/monx* file exists inside engine/ — world content must never be committed"
else
    ok "no world content files inside engine/"
fi

# --- the engine never invents a world ------------------------------------
GHOST="$WORK/ghost"
./u2 --headless --script tests/title.keys --data-dir "$GHOST" >/dev/null 2>&1
if [ -e "$GHOST" ]; then
    bad "a run created --data-dir $GHOST; the content directory is the player's, not ours to make"
else
    ok "a run with an absent --data-dir creates nothing"
fi

# --- the shipped content still loads, when the player has it -------------
CONTENT=""
for d in ../original ../references/ultima2; do
    [ -f "$d/mapx00" ] && CONTENT="$d" && break
done
if [ -n "$CONTENT" ]; then
    SIZE=$(wc -c < "$CONTENT/mapx00" | tr -d ' ')
    [ "$SIZE" = "4224" ] \
        && ok "player content found at $CONTENT (mapx00 is 4224 B: 4096 tiles + 128 tail)" \
        || bad "$CONTENT/mapx00 is $SIZE B, expected 4224 (4096 tiles + unread tail)"
    ./u2 --headless --script tests/title.keys --data-dir "$CONTENT" >/dev/null 2>&1 \
        && ok "a run against the player's own content directory exits clean" \
        || bad "run against $CONTENT failed"
else
    printf '  SKIP  no player content present (pass --data-dir to test against it)\n'
fi

# --- the SDL path proves itself headlessly, when an SDL build exists ------
# Standing directive: the cell -> font -> upload chain must be testable without
# a display. SDL_VIDEODRIVER=dummy gives exactly that. It used to hang after
# writing the frame (T6: --dump-screen set dump_screen_done, the poll turned
# that into input_exhausted, and wait_choice only tested exit_code), so this
# test is also the regression guard for that. Skipped when the SDL build was
# not requested, because SDL2 is an owner-install dependency.
#
# It prefers u2-sdl-static over u2-sdl on purpose. The dev binary is linked against
# whatever the host calls sdl2, which on this machine is sdl2-compat over SDL3, so
# testing only u2-sdl means the one gate that touches the SDL backend exercises an
# implementation we do not ship. Which one was used is printed, because a gate that
# quietly tests the wrong artifact is the failure mode docs/sdl2-vs-sdl3.md § 7
# names.
SDL_BIN=""
if   [ -x ./u2-sdl-static ]; then SDL_BIN=./u2-sdl-static
elif [ -x ./u2-sdl ];        then SDL_BIN=./u2-sdl
fi
if [ -n "$SDL_BIN" ]; then
    printf '  info  SDL gate is driving %s\n' "$SDL_BIN"
    rm -f "$WORK/sdl.ppm"
    SDL_VIDEODRIVER=dummy timeout -s KILL 20 "$SDL_BIN" --sdl --dump-screen "$WORK/sdl.ppm" \
        >/dev/null 2>&1
    sdl_rc=$?
    if [ "$sdl_rc" -ne 0 ]; then
        bad "headless SDL run did not exit cleanly (rc=$sdl_rc; a hang here is the T6 defect)"
    elif [ ! -s "$WORK/sdl.ppm" ]; then
        bad "headless SDL run wrote no frame"
    else
        ppm_size=$(wc -c < "$WORK/sdl.ppm" | tr -d ' ')
        [ "$ppm_size" = "192015" ] \
            && ok "headless SDL: dummy driver presents a 320x200 frame ($ppm_size B)" \
            || bad "headless SDL frame is $ppm_size B, expected 192015 (P6 320x200)"
        # Ink check: the title screen is text on black, so a presented frame
        # must contain non-black pixels. A blanked frame is the failure mode
        # this guards (the same class the screenshot diff guards for the map).
        ink=$(python3 - "$WORK/sdl.ppm" <<'PY'
import sys
raw = open(sys.argv[1], 'rb').read()
end = 0
for _ in range(3):                      # P6, dims, maxval
    end = raw.index(b'\n', end) + 1
px = raw[end:]
print(sum(1 for i in range(0, len(px), 3) if px[i:i+3] != b'\x00\x00\x00'))
PY
)
        [ -n "$ink" ] && [ "$ink" -gt 100 ] \
            && ok "headless SDL frame carries ink ($ink non-black pixels, not a blank upload)" \
            || bad "headless SDL frame is blank or unreadable (ink=$ink)"
    fi
    # The default, with no flag at all. This is the player's path: the downloaded
    # binary is double-clicked or run bare, with no terminal and no idea that
    # --sdl exists. --dump-screen is implemented only by the SDL renderer, so a
    # frame here is proof the default resolved to sdl and not to the text grid.
    rm -f "$WORK/sdl-default.ppm"
    SDL_VIDEODRIVER=dummy timeout -s KILL 20 "$SDL_BIN" --dump-screen "$WORK/sdl-default.ppm" \
        >/dev/null 2>&1
    def_size=$( [ -s "$WORK/sdl-default.ppm" ] && wc -c < "$WORK/sdl-default.ppm" | tr -d ' ' )
    if [ "$def_size" = "192015" ]; then
        ok "SDL build defaults to the window: a flagless run wrote the 320x200 frame"
    else
        bad "SDL build did not default to the SDL renderer (flagless --dump-screen gave '${def_size:-nothing}')"
    fi

    # The enhanced overworld, presented for real. This is the case every other
    # SDL check missed: they all present the title frame, which is a scaled
    # 320x200, so a renderer whose staging buffer is sized from a constant
    # instead of from the frame looks fine forever. The enhanced frame is
    # 1024x512, and when it was, filling a 640x400 staging buffer with it was a
    # 1 MB heap overflow that aborted on the next free() the first time a player
    # pressed P at the menu and entered the world. Needs sector files, so it
    # skips out loud where the media is absent.
    if [ -d ../original ] && [ -f ../original/mapx20 ]; then
        rm -rf "$WORK/cp4th"; mkdir -p "$WORK/cp4th"
        # A v1 theme is a bare EGATILES: 128 tiles of 16x16 2bpp = 128 B each.
        python3 -c "open('$WORK/cp4th/EGATILES','wb').write(bytes(128*128))" 2>/dev/null \
          || head -c 16384 /dev/zero > "$WORK/cp4th/EGATILES"
        rm -f "$WORK/enh.ppm"
        SDL_VIDEODRIVER=dummy timeout -s KILL 25 "$SDL_BIN" --sdl --play-map --enhanced \
            --ega-theme default --ega-theme-dir "$WORK/cp4th" \
            --data-dir ../original --save-dir saves --dump-screen "$WORK/enh.ppm" \
            >/dev/null 2>&1
        enh_rc=$?
        enh_size=$( [ -s "$WORK/enh.ppm" ] && wc -c < "$WORK/enh.ppm" | tr -d ' ' )
        if [ "$enh_rc" -ne 0 ]; then
            bad "enhanced overworld present exited $enh_rc (heap corruption here shows as an abort on free)"
        elif [ "$enh_size" = "1572880" ]; then
            ok "headless SDL: enhanced overworld presents a 1024x512 frame"
        else
            bad "enhanced SDL frame is ${enh_size:-nothing}, expected 1572880 (P6 1024x512)"
        fi
    else
        printf '  SKIP  no ../original sector files — enhanced present path not exercised\n'
    fi

    # The cursor, in pixels, under a theme that brings its own palette.
    #
    # Not a grid assertion, because the cursor is not a character: it is the same
    # glyph repainted inverse video by sdl_flush, so every text-level check passes
    # whether the block is drawn or not. Under the FM Towns theme it was not drawn
    # at all -- index 3 of that palette is a second black and sdl_flush repainted
    # the cell with U2_CGA_WHITE, which IS index 3 -- and the character under the
    # cursor was repainted in black as well. Reported from play on 2026-11-06 as
    # "the cursor is missing, so it is hard to see what line you are editing".
    #
    # The skeleton screen is the vehicle because --dump-screen saves the FIRST
    # presented frame and it is the only screen a test can reach on a first present;
    # its cursor is placed at column 16, row 12, over the last character of
    # "press Q to quit". Both themes run, because the fix has to leave the ordinary
    # EGA path exactly as it was as well as repair the themed one.
    for th in fmtowns default; do
        rm -f "$WORK/cursor-$th.ppm"
        SDL_VIDEODRIVER=dummy timeout -s KILL 20 "$SDL_BIN" --sdl --selftest \
            --ega --ega-theme "$th" --ega-theme-dir ../themes \
            --dump-screen "$WORK/cursor-$th.ppm" >/dev/null 2>&1
        cur_rc=$?
        if [ "$cur_rc" -ne 0 ] || [ ! -s "$WORK/cursor-$th.ppm" ]; then
            bad "cursor check ($th): the dumped run failed (rc=$cur_rc)"
        elif python3 tests/cursor_visible.py "$WORK/cursor-$th.ppm" 12 16 >/dev/null 2>"$WORK/cursor-$th.err"; then
            ok "cursor is visible under the $th theme (inverse block in the theme's ink)"
        else
            bad "cursor invisible under the $th theme: $(sed 's/^/        /' "$WORK/cursor-$th.err")"
        fi
    done
else
    printf '  SKIP  no u2-sdl or u2-sdl-static built — headless SDL path not exercised\n'
fi

# --- the screenshot differential, when the evidence is on disk ------------
# Standing directive: validate the engine against the owner's play screenshots
# as a test. The screenshots are gitignored (derived from copyrighted art), so
# this SKIPs with a named reason when they or PIL are absent, and goes live
# the moment they are present. It compares the status-block region only; the
# map region differs by sector/position and is out of scope (see the tool's
# header). The engine frame is rendered here so both sides come from one run.
SHOT="../captures/screenshots/move-after.png"
if python3 -c "import PIL" 2>/dev/null && [ -f "$SHOT" ] && [ -x ./u2 ]; then
    ./u2 --headless --dump-map "$WORK/t7-engine.ppm" --data-dir "$CONTENT" \
        >/dev/null 2>&1 || true
    if [ ! -s "$WORK/t7-engine.ppm" ] && [ -z "$CONTENT" ]; then
        printf '  SKIP  no content dir for the engine frame in the screenshot diff\n'
    else
        if python3 ../tools/screenshot_diff.py --screenshot "$SHOT" \
               --engine-ppm "$WORK/t7-engine.ppm" >"$WORK/t7.out" 2>&1; then
            ok "screenshot diff: status block agrees with the play shot ($(grep agreement "$WORK/t7.out" | awk '{print $3}') ink on both sides)"
        else
            bad "screenshot diff failed:"
            sed 's/^/        /' "$WORK/t7.out"
        fi
    fi
else
    printf '  SKIP  screenshot diff not run (needs PIL + captures/screenshots/move-after.png)\n'
fi

# --- T5: the creation tail, under both fidelities -------------------------
# The original's tail (asm/5400) prints "TO PLAY ULTIMA II", then "INSERT
# PROGRAM MASTER", spins on key_poll and calls the program startup routine at
# runtime 0x0100 — a full restart, so finishing creation lands back on the
# splash and the player presses P. Enhanced mode drops that CP/M dead end and
# enters the world directly. Needs a content dir, because the overworld cannot
# load a sector without mapxNN.
if [ -n "$CONTENT" ]; then
    T5KEYS="$WORK/t5.keys"
    # The form is stepped and dropdowned, not typed: 0xf3 is Right (+1) and 0xf1
    # is Down, so five of the former and one of the latter per stat takes each from
    # its floor of 10 to 15 and moves on. Three pairs of Enter then open a dropdown
    # and commit its first row: MALE, HUMAN, FIGHTER.
    printf 'ESC\nC\n' > "$T5KEYS"
    for s in 1 2 3 4 5 6; do
        for i in 1 2 3 4 5; do printf '0xf3\n' >> "$T5KEYS"; done
        printf '0xf1\n' >> "$T5KEYS"
    done
    for i in 1 2 3 4 5 6; do printf '0x0d\n' >> "$T5KEYS"; done
    printf 'B\nR\nI\nT\nI\nS\nH\n0x0d\n0x0d\n0x0d\n0x0d\n' >> "$T5KEYS"

    rm -rf "$WORK/t5orig"; mkdir -p "$WORK/t5orig"
    ./u2 --headless --data-dir "$CONTENT" --save-dir "$WORK/t5orig" \
         --script "$T5KEYS" --dump-each > "$WORK/t5orig.out" 2>&1
    grep -q "CMD:" "$WORK/t5orig.out" \
        && bad "original mode entered the world after creation; it must restart to the title" \
        || ok "original mode: completed creation restarts to the title, not the world"

    rm -rf "$WORK/t5enh"; mkdir -p "$WORK/t5enh"
    ./u2 --headless --enhanced --data-dir "$CONTENT" --save-dir "$WORK/t5enh" \
         --script "$T5KEYS" --dump-each --dump-state > "$WORK/t5enh.out" 2>&1
    grep -q "CMD:" "$WORK/t5enh.out" \
        && ok "enhanced mode: completed creation drops straight into the world" \
        || bad "enhanced mode did not enter the world after creation"

    # Entering the world is not enough; it has to be the SAME world the 'P' path
    # would have loaded. The tail used to call u2_overworld_run, which is the probe
    # entry and seeds the position from U2_OVERWORLD_PROBE_* (33,23) instead of the
    # record's entry pair, so every enhanced-mode start landed in sector 30 while
    # the state still claimed sector 20. Creation writes 20,20 at asm/5BDC..5BE8.
    grep -q '"pos":{"x":20,"y":20},"sector":"20"' "$WORK/t5enh.out" \
        && ok "enhanced mode: the fresh character spawns on the record's entry pair, not the probe" \
        || bad "enhanced mode spawned at the wrong place; want pos 20,20 sector 20, got $(grep -o '"pos":{"x":[0-9]*,"y":[0-9]*},"sector":"[0-9]*"' "$WORK/t5enh.out" | tail -1)"

    # An abandoned creation must not run the tail at all.
    rm -rf "$WORK/t5ab"; mkdir -p "$WORK/t5ab"
    printf 'ESC\nC\n' > "$WORK/t5ab.keys"
    ./u2 --headless --data-dir "$CONTENT" --save-dir "$WORK/t5ab" \
         --script "$WORK/t5ab.keys" --dump-each > "$WORK/t5ab.out" 2>&1
    grep -q "CMD:" "$WORK/t5ab.out" \
        && bad "abandoning creation still ran the restart tail" \
        || ok "abandoned creation (ESC) does not run the tail"
else
    printf '  SKIP  T5 creation-tail checks (no content dir holding mapx00)\n'
fi

printf '\n'
if [ "$fail" -eq 0 ]; then
    printf 'CP4 TEST: PASS\n'
else
    printf 'CP4 TEST: FAIL\n'
fi
exit "$fail"

#!/usr/bin/env bash
# drive-and-validate.sh — record several real sessions, then verify every one.
#
# The owner's ask, 2026-11-08: "drive a few games with recording and then validate
# the recording ... you can do it locally in a container with the SDL binary."
#
# Shape of one lap, and why each leg is here:
#
#   1. export the keys and the checkpoint out of an existing .demo
#      (tools/demo.py export-keys). The checkpoint is what lets a fresh session start
#      where a real playthrough started, on real content, without a human at the keys.
#   2. re-record it headless with --script, so the run is deterministic and re-runnable.
#      --script is refused outside headless on purpose (main.c), so recording is a
#      headless job and validation is where the other backends get their turn.
#   3. replay the NEW file three ways: headless, the terminal renderer, and the static
#      SDL3 binary under the dummy driver. Same file, three clocks and three renderers;
#      if any of them disagrees the digests say so.
#
# Scratch lives under /var/tmp, never /tmp (that is RAM here) and never inside engine/
# (a sector file under engine/ is what tests/content_immutable.py fails the build on).
set -uo pipefail

cd "$(dirname "$0")/.." || exit 1
ENGINE=$PWD
U2=$ENGINE/u2
SDL=$ENGINE/u2-sdl-static
S=${SCRATCH:-/var/tmp/u2drive}
SRC_DEMOS=${SRC_DEMOS:-$ENGINE/demos}
SAVE_SOURCE=${SAVE_SOURCE:-}

# Accepts a directory (the default) or one or more files, so a single demo can be
# re-run while iterating without moving it out of the corpus.
DEMOS=()
if [ -d "$SRC_DEMOS" ]; then
    while IFS= read -r -d '' f; do DEMOS+=("$f"); done \
        < <(find "$SRC_DEMOS" -maxdepth 1 -name '*.demo' -print0 | sort -z)
else
    for a in "$SRC_DEMOS" "$@"; do [ -f "$a" ] && DEMOS+=("$a"); done
fi
[ "${#DEMOS[@]}" -gt 0 ] || { echo "no .demo files under $SRC_DEMOS"; exit 1; }
printf 'drive-and-validate: %d recording(s), backends headless/term/%s\n' \
    "${#DEMOS[@]}" "$([ -x "$SDL" ] && echo sdl || echo 'sdl:absent')"

pass=0; fail=0; skipped=0
ok()  { printf '  PASS  %s\n' "$1"; pass=$((pass+1)); }
bad() { printf '  FAIL  %s\n' "$1"; fail=$((fail+1)); }

[ -x "$U2" ]  || { echo "build engine/u2 first"; exit 1; }

# Seed the scratch from the player's own content if it is not there yet, so `make drive`
# is one command rather than a ritual. The save slot starts empty on purpose: a fresh slot
# is what makes the recorder emit a delta against content at all, which is the only shape
# where the @world machinery does real work (a clean-install recording carries delta=none
# and would pass every delta assertion by carrying nothing).
SRC_CONTENT=${SRC_CONTENT:-$ENGINE/../original}
if [ ! -d "$S/content" ]; then
    [ -f "$SRC_CONTENT/mapx20" ] || { echo "drive: no $SRC_CONTENT to record against, and $S is not seeded"; exit 1; }
    mkdir -p "$S/content" "$S/save/PLAYER"
    cp "$SRC_CONTENT"/* "$S/content/" 2>/dev/null
    printf 'seeded %s from %s\n' "$S" "$SRC_CONTENT"
fi

# The save is applied every run, not only on first seeding: a scratch can outlive the save it
# was built with, and a stale slot producing eight silent no-ops is the worst possible failure.
if [ -n "$SAVE_SOURCE" ]; then
    [ -d "$SAVE_SOURCE/PLAYER" ] || { echo "drive: SAVE_SOURCE=$SAVE_SOURCE does not contain a PLAYER/ directory"; exit 1; }
    rm -rf "$S/save"; mkdir -p "$S/save"
    cp -r "$SAVE_SOURCE/PLAYER" "$S/save/"
    printf 'seeded save from %s\n' "$SAVE_SOURCE"
fi
# A re-recorded route needs the save the original session started from. --load-ds pins the
# DS mirror but not the sector digits the world opens from, so recording against the stock
# release `player` silently starts the character in sector 00 and the exported keys drive a
# different game in a different place -- which records nothing and looks like an engine
# fault. SAVE_SOURCE must point at a directory holding PLAYER/ (PLAYER, mapxNN, monxNN).
if [ ! -f "$S/save/PLAYER/PLAYER" ]; then
    cat >&2 <<'NEEDS'
drive: these recordings start from a save this machine does not have.

--load-ds pins the DS mirror but not the sector the world opens from, so re-recording
against the stock release `player` drives a different character in a different sector and
records nothing. Point SAVE_SOURCE at the directory that CONTAINS a `PLAYER/` slot (inside
it: PLAYER, mapxNN, monxNN) -- the saves directory the original session used:

  SAVE_SOURCE=/path/to/saves make drive

On the owner's machine that is ~/Games/u2/saves; copy it or mount it.
NEEDS
    exit 1
fi

[ -d "$S/content" ] || { echo "scratch $S not seeded"; exit 1; }

for demo in "${DEMOS[@]}"; do
    name=$(basename "$demo" .demo)
    # A gate fixture records against a synthetic world (`content none`, sector 70) that no
    # real content directory contains, so replaying one here would refuse for a reason that
    # says nothing about the engine. Skip them by name rather than by size, and say how many.
    if [ "${FORCE:-0}" != 1 ] && grep -q '^content none$' "$demo" 2>/dev/null; then
        skipped=$((skipped+1)); continue
    fi
    rm -f "$S/$name.keys" "$S/$name.ds"
    python3 tools/demo.py export-keys "$demo" --keys "$S/$name.keys" --ds "$S/$name.ds" \
        >/dev/null 2>&1 || { bad "$name: export-keys failed"; continue; }

    for mode in original enhanced; do
        flag=""; [ "$mode" = enhanced ] && flag="--enhanced"
        out="$S/out_$name"_$mode; rm -rf "$out"; mkdir -p "$out"
        { echo "0x12"; grep -v '^#' "$S/$name.keys"; echo "0x12"; } > "$out/run.keys"

        "$U2" --headless --play-map --cleanroom $flag \
              --data-dir "$S/content" --save-dir "$S/save" \
              --load-ds "$S/$name.ds" --script "$out/run.keys" \
              --demo-dir "$out" --demo-name r --demo-snapshots \
              > "$out/rec.log" 2>&1
        nrec=$(grep -c '^@rec' "$out/r.demo" 2>/dev/null || echo 0)
        if [ "$nrec" -eq 0 ]; then
            bad "$name/$mode: recorded nothing: $(tail -1 "$out/rec.log")"; continue
        fi

        verdict=""
        for backend in headless term sdl; do
            case $backend in
              headless) bin="$U2";  set -- --headless ;;
              term)     bin="$U2";  set -- --headless --renderer=terminal ;;
              sdl)
                  [ -x "$SDL" ] || { verdict+=" $backend:skip"; continue; }
                  # No --headless here: that flag selects the null renderer, and the
                  # whole point of this leg is the real SDL event loop, real clock and
                  # the 16 ms seam waits, with the dummy video driver standing in for a
                  # window.
                  bin="$SDL"; set --
                  export SDL_VIDEODRIVER=dummy ;;
            esac
            rc=0
            "$bin" "$@" --cleanroom --data-dir "$S/content" --save-dir "$S/save" \
                   --playback "$out/r.demo" --demo-snapshots \
                   > "$out/$backend.log" 2>&1 || rc=$?
            [ "$backend" = sdl ] && unset SDL_VIDEODRIVER
            if [ "$rc" -eq 0 ]; then verdict+=" $backend:ok"
            else verdict+=" $backend:rc$rc($(grep -o 'DIVERGED at record [0-9]*' "$out/$backend.log" | head -1))"; fi
        done

        fid=$(grep -o '^fidelity .*' "$out/r.demo")
        if printf '%s' "$verdict" | grep -q ':rc'; then
            bad "$name/$mode ($nrec rec, $fid):$verdict"
        else
            ok "$name/$mode ($nrec rec, $fid):$verdict"
        fi
    done
done

printf '\ndrive-and-validate: %d passed, %d failed, %d synthetic fixture(s) skipped\n' \
    "$pass" "$fail" "$skipped"
[ "$fail" -eq 0 ]

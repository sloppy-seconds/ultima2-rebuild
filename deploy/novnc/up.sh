#!/usr/bin/env bash
# Build, verify and start the ENGINE over noVNC on ai: our reimplementation, watched
# from a browser. This is the entry point AGENTS.md claims exists.
#
#   ./up.sh                 # rebuild the static engine, verify it, start at the splash/title
#   ./up.sh --load-save     # skip the intro and boot the saved character into the world
#   ./up.sh --cleanroom     # deliberately ship the substitute art instead
#   ./up.sh --port N        # host port (default 6082; 6081 is taken by another container)
#   ./up.sh --down          # stop and remove the container
#   ./up.sh --status        # report what the running container is actually drawing
#   ./up.sh --name LABEL    # container name (default u2-novnc)
#   ./up.sh --no-verify     # skip the smoke test (NOT recommended; see below)
#
# Why this script exists at all, and why the verification steps are not optional.
#
# On 2026-10-29 an owner playtest came back as "you started the game with the cleanroom
# graphics, which I did not ask for". It was true. The deploy step was a chain of
# one-liners in which the build ran behind `>/dev/null 2>&1`, so a build that did not do
# what it was told produced no signal, and the binary that got copied into the image was
# not the binary the current source produces. Nothing between `make` and the running
# container looked at the artifact.
#
# So every stage here checks the ARTIFACT rather than trusting the step that produced it:
#   1. the build runs with its output visible and set -e in force,
#   2. the binary must be newer than every source file it is built from,
#   3. the binary is RUN and must announce the art mode it was asked for,
#   4. after the container starts, its own log must say the same thing.
# Step 3 is the one that matters: it catches a stale binary, a mis-set default, a wrong
# flag, or a build system that quietly did nothing, and it does so without caring which.
# A check that only proves the build exited 0 would have passed on 2026-10-29.
#
# A skip is reported as a skip and never as a pass. If there is no copy of the game to
# point at, or no saved character to boot into the world, the smoke test says so out loud
# and the script exits non-zero, because a deployment that shipped unverified art at a
# player is exactly the failure this file was written to stop.
set -euo pipefail

IMAGE=u2-novnc
CONTAINER=u2-novnc
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
HOST_PORT=6082
SAVES_HOST=/var/tmp/u2/novnc-saves
STAGED="$HERE/u2-sdl"

CLEANROOM=0
LOADSAVE=0
DOWN=0
STATUS=0
VERIFY=1

while [ $# -gt 0 ]; do
    case "$1" in
        --cleanroom) CLEANROOM=1; shift ;;
        --load-save) LOADSAVE=1; shift ;;
        --down)      DOWN=1; shift ;;
        --status)    STATUS=1; shift ;;
        --no-verify) VERIFY=0; shift ;;
        --port)      HOST_PORT=$2; shift 2 ;;
        --name)      CONTAINER=$2; shift 2 ;;
        -h|--help)   sed -n '2,32p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

say() { printf 'up.sh: %s\n' "$*"; }
die() { printf 'up.sh: ERROR: %s\n' "$*" >&2; exit 1; }

# What the engine prints about its own art, and what we expect it to print.
#   original art : "overworld: sector 20 via mapx, tiles extracted, ..."
#   substitute   : "u2: cleanroom assets: tiles from builtin:cleanroom"
#                  "overworld: sector 20 via mapx, cleanroom tiles, ..."
# "cleanroom" in the log is therefore the discriminator, and it comes from the code path
# guarded by the flag rather than from anything I inferred.
art_expect() { [ "$CLEANROOM" = 1 ] && echo cleanroom || echo original; }

verify_art()
{
    # verify_art <label> <text-file>
    # Asserts the run announced the art mode we asked for. Fails on absence of any art
    # line at all: "could not tell" is not "looks fine".
    local label=$1 file=$2 line
    # The entrypoint echoes its own argument list, and that line contains the flag name
    # without saying anything about what got drawn. Matching on it would let a build that
    # ignored --cleanroom still pass the check, so the echo is excluded and only the
    # engine's own announcement counts.
    line=$(grep -iE "cleanroom|tiles extracted|tiles from cache" "$file" \
        | grep -v "novnc-engine:" | head -1 || true)
    [ -n "$line" ] || die "$label produced no art-mode line; cannot verify what it draws"
    case "$line" in
        *cleanroom*)
            [ "$CLEANROOM" = 1 ] || die "$label drew CLEANROOM substitute art but --cleanroom was not asked for. Line: $line"
            say "$label: substitute art, as requested. ($line)" ;;
        *)
            [ "$CLEANROOM" = 0 ] || die "$label drew original art but --cleanroom WAS asked for. Line: $line"
            say "$label: player's own art. ($line)" ;;
    esac
}

if [ "$STATUS" = 1 ]; then
    podman ps -a --filter "name=$CONTAINER" --format '{{.Names}}  {{.Status}}  {{.Ports}}'
    podman logs "$CONTAINER" 2>&1 \
        | grep -iE "cleanroom|tiles extracted|tiles from cache" | tail -3 \
        || say "no art-mode line in the log yet"
    exit 0
fi

if [ "$DOWN" = 1 ]; then
    podman rm -f "$CONTAINER" >/dev/null 2>&1 && say "stopped and removed $CONTAINER" \
        || say "no container named $CONTAINER"
    exit 0
fi

cd "$ROOT"
[ -d engine ] || die "not at project root: no engine/ (script lives in deploy/novnc/)"

# ---------------------------------------------------------------- 1. build
# Output goes to the terminal on purpose. The bug this script exists to stop was a build
# whose output was thrown away.
# The art choice is a runtime flag, not a compile-time one, so the build is identical
# either way. What differs is what the container is told to run with, below.
say "building the static SDL3 engine in the linuxbuild container (output visible)"
# The host no longer builds this binary, and that is deliberate rather than
# convenience. `make sdl` used to ask `sdl2-config`, which on Bazzite resolves to
# sdl2-compat: the SDL2 API implemented over SDL3. That build compiled, ran, and
# looked like success while linking something the release does not ship, so
# tools/sdl_probe.sh was made to refuse it. Refusing was correct; it also removed
# the only build path this script had, and nothing here was repointed, so the
# deploy path sat broken from 2026-11-03 with a stale staged binary still in the
# image and no signal at all. A gate that rejects the working path must name the
# replacement, and the replacement is this container: same source, pinned static
# SDL3, and its own artifact gate on the video drivers.
bash deploy/linuxbuild/build.sh

BIN=engine/u2-sdl-static
[ -x "$BIN" ] || die "build.sh reported success but $BIN is not executable"

# ---------------------------------------------------------------- 2. freshness
# If make decided nothing had changed while a source file is newer than the binary, the
# binary is not the current source. The engine Makefile owns this check as `make fresh`,
# so there is one definition rather than two that can drift.
if ! make -C engine fresh >/dev/null 2>&1; then
    say "binary is stale -- forcing a clean rebuild:"
    make -C engine fresh 2>&1 | sed -n '2,7p' | sed 's/^/up.sh:   /'
    make -C engine clean
    bash deploy/linuxbuild/build.sh
    make -C engine fresh >/dev/null 2>&1 || die "still stale after a clean rebuild"
fi
say "binary is newer than every source it is built from"

# ---------------------------------------------------------------- 3. smoke test
# Run the BUILT binary the way the container will run it, and make it say what it draws.
# This happens before staging on purpose: an earlier draft copied the binary into deploy/
# first, so a refused deploy left the bad artifact sitting in the image's build context.
# The control that found this was a planted wrapper printing the cleanroom line; up.sh
# correctly refused and had already staged it.
if [ "$VERIFY" = 1 ]; then
    TMP=$(mktemp -d /var/tmp/u2/upsh.XXXXXX)
    # /var/tmp, never /tmp: /tmp is tmpfs here and a frame dump takes RAM from the model.
    trap 'rm -rf "$TMP"' EXIT
    mkdir -p "$TMP/save" "$TMP/dbg"

    SAVE_SRC="$SAVES_HOST/PLAYER"
    [ -r "$SAVE_SRC" ] || SAVE_SRC="original/player"
    if [ -r "$SAVE_SRC" ]; then
        cp "$SAVE_SRC" "$TMP/save/PLAYER"
    elif [ -r "references/ultima2/player" ]; then
        cp "references/ultima2/player" "$TMP/save/PLAYER"
    fi

    if [ ! -d original ]; then
        die "no original/ to point --data-dir at, so the smoke test cannot run. A deployment nobody verified is how substitute art reached a player; fix the inputs rather than passing --no-verify."
    fi
    if [ ! -r "$TMP/save/PLAYER" ]; then
        die "no saved character to boot into the world, so the smoke test cannot run. Point one at $SAVES_HOST/PLAYER rather than passing --no-verify."
    fi

    # No --script here on purpose: the engine refuses --script outside --headless, because
    # no interactive backend reads a key script, so passing it to an --sdl run is a
    # refusal rather than a no-op. --play-map reaches the overworld and announces its art
    # before it ever waits for input, which is all this check needs; the process is killed
    # a few seconds later regardless.
    ARGS=(--sdl --play-map --data-dir original --save-dir "$TMP/save"
          --debug-path "$TMP/dbg")
    [ "$CLEANROOM" = 1 ] && ARGS+=(--cleanroom)

    say "smoke-testing the built binary (art mode must match: $(art_expect))"
    SDL_VIDEODRIVER=dummy setsid "$ROOT/$BIN" "${ARGS[@]}" >"$TMP/out.txt" 2>&1 &
    SPID=$!
    sleep 4
    kill -KILL -- "-$SPID" 2>/dev/null || kill -KILL "$SPID" 2>/dev/null || true
    wait "$SPID" 2>/dev/null || true
    verify_art "built binary" "$TMP/out.txt"
else
    say "smoke test SKIPPED by --no-verify: this deployment is UNVERIFIED" >&2
fi

# ---------------------------------------------------------------- 4. stage
# Only reached once the artifact has proven itself.
cp "$BIN" "$STAGED"
say "staged $(md5sum "$STAGED" | cut -c1-12) -> deploy/novnc/u2-sdl"

# ---------------------------------------------------------------- 5. image
say "building image $IMAGE:latest"
podman build -t "$IMAGE:latest" "$HERE" >/dev/null

# ---------------------------------------------------------------- 6. run
# The entrypoint's own U2_ARGS default carries no --cleanroom, so asking for substitute
# art means overriding it here or the flag silently goes nowhere -- which is precisely the
# class of bug this script was written to catch, relocated one layer down.
U2_ARGS="--sdl --debug-path /saves/debug"
# Default is the splash screen and title flow, because that is what a player meets and
# what the original shows. --play-map is the engine's development entry: it jumps
# straight into the overworld, skipping the intro, the title and character creation, so
# it is opt-in now via --load-save. The smoke test below still uses it unconditionally,
# because verifying the art mode needs a run that reaches the overworld.
[ "$LOADSAVE" = 1 ] && U2_ARGS="$U2_ARGS --play-map"
[ "$CLEANROOM" = 1 ] && U2_ARGS="$U2_ARGS --cleanroom"

podman rm -f "$CONTAINER" >/dev/null 2>&1 || true
# The cache moved out of the save directory (docs/save-scoped-worlds.md D4), so a
# tiles.u2tl here is a leftover from the old layout rather than anything the engine
# reads. Delete it anyway: the cleanroom path never overwrites it, and a stale one
# sitting beside a save has already misled one investigation.
rm -f "$SAVES_HOST/tiles.u2tl"
mkdir -p "$SAVES_HOST"
podman run -d --name "$CONTAINER" -p "$HOST_PORT:6081" \
    -e U2_ARGS="$U2_ARGS" \
    -v "$ROOT/original:/content:ro,z" \
    -v "$SAVES_HOST:/saves:z" \
    "$IMAGE:latest" >/dev/null

say "waiting for the engine to draw its first frame"
line=""
for _ in 1 2 3 4 5 6 7 8 9 10; do
    sleep 2
    line=$(podman logs "$CONTAINER" 2>&1 \
        | grep -iE "cleanroom|tiles extracted|tiles from cache" \
        | grep -v "novnc-engine:" | head -1 || true)
    [ -n "$line" ] && break
done
[ -n "$line" ] || die "container started but never announced an art mode; check: podman logs $CONTAINER"

# ---------------------------------------------------------------- 7. verify deployed
TMPD=$(mktemp -d /var/tmp/u2/upsh-log.XXXXXX)
trap 'rm -rf "$TMPD"' EXIT
podman logs "$CONTAINER" >"$TMPD/log.txt" 2>&1
verify_art "running container" "$TMPD/log.txt"

say "ready -> http://ai.lan:$HOST_PORT/vnc.html?resize=scale&autoconnect=true"

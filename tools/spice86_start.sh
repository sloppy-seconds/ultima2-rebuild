#!/usr/bin/env bash
# spice86_start.sh — launch Ultima II under the vendored Spice86, and harvest
# the runtime CFG.
#
# Two jobs, one entry point:
#
#   spice86_start.sh                 start the game in a window you can watch
#   spice86_start.sh --harvest       dump the CFG of the session that is
#                                    already running, without stopping it
#
# Why --harvest is separate from the stop script: Spice86 writes its CFG dump on
# clean exit, so a harvest normally comes for free when you stop. But the
# interesting captures are the ones taken mid-play (standing at a sector edge,
# in a shop, mid-combat), and those have to be pulled from a live emulator. That
# goes through the GDB port with `monitor dumpall`, which dumps and keeps
# running.
#
# The run never happens in place: the game rewrites its own `player` file, so
# every launch copies the pristine references/ultima2/ backup into a fresh
# timestamped directory under the project tmp/ (disk-backed, gitignored). The
# system /tmp is tmpfs on this machine and is deliberately not used.
#
# See docs/spice86-capture.md for the memory-API details and the reason
# --AllowIvtAddress0 is mandatory.

set -euo pipefail

# shellcheck source=spice86_common.sh
. "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/spice86_common.sh"

usage() {
    cat <<'EOF'
usage: spice86_start.sh [options]

  --watch            windowed run, you play/watch it (default)
  --headless         no window (Spice86 Minimal headless mode)
  --cycles N         emulated CPU cycles per ms (default 3000; approximates the
                     1983 machine the game calibrates against)
  --name LABEL       label for the run and the harvest directory
  --player FILE      seed the run with an existing player file instead of the
                     pristine blank (e.g. tmp/<run>/player from an earlier
                     session). Lets 'P' load a character you already made.
  --harvest          dump the CFG of the running session and exit
  --auto-play        after the API is ready, send ESC then P so a seeded player
                     loads and the run enters the world. Works HEADLESS: keys go
                     through Spice86's own send_keyboard_key MCP tool, not XTEST,
                     so no window and no display is needed. Pair with --player.
                     Sequence is configurable: AUTO_PLAY_KEYS="Escape,p".
                     Always confirm it worked by reading memory (tools/spice86_type.py
                     --probe); a dropped key looks exactly like a no-op.
  --patch-arrows     patch the RUN COPY so real arrow keys work, letting
                     tools/spice86_keys.py drive it with Up/Down/Left/Right
                     instead of the numpad. Never touches original/ or
                     references/ — see tools/patch_arrows.py.
  --seed-map PATH    copy PATH (basename must be mapxNN) into the run copy before
                     launch, replacing that sector's map. Generate one with
                     tools/u2_mkprobe.py. Must be installed pre-launch: the game
                     reads mapxNN at startup.
  --http-port P      HTTP API port (default: first free at/above 20180)
  --mcp-port P       MCP port (default: first free at/above 20181)
  --gdb-port P       GDB port (default: first free at/above 20190)
  -h, --help         this text
EOF
}

MODE=watch
CYCLES=3000
LABEL=""
DO_HARVEST=0
AUTO_PLAY=0
AUTO_PLAY_KEYS="${AUTO_PLAY_KEYS:-Escape,p}"
PLAYER_FILE=""
PATCH_ARROWS=0
SEED_MAP=""
HTTP_PORT=""
MCP_PORT=""
GDB_PORT=""

while [ $# -gt 0 ]; do
    case "$1" in
        --watch)     MODE=watch; shift ;;
        --headless)  MODE=headless; shift ;;
        --cycles)    CYCLES=$2; shift 2 ;;
        --name)      LABEL=$2; shift 2 ;;
        --player)    PLAYER_FILE=$2; shift 2 ;;
        --harvest)   DO_HARVEST=1; shift ;;
        --auto-play) AUTO_PLAY=1; shift ;;
        --patch-arrows) PATCH_ARROWS=1; shift ;;
        --seed-map)  SEED_MAP=$2; shift 2 ;;
        --http-port) HTTP_PORT=$2; shift 2 ;;
        --mcp-port)  MCP_PORT=$2; shift 2 ;;
        --gdb-port)  GDB_PORT=$2; shift 2 ;;
        -h|--help)   usage; exit 0 ;;
        *) echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

ROOT=$(s86_root)
DLL=$(s86_dll)
REFS=$(s86_refs)

[ -f "$DLL" ]  || { echo "Spice86.dll not found at $DLL" >&2; exit 1; }
[ -d "$REFS" ] || { echo "pristine game files not found at $REFS" >&2; exit 1; }
command -v dotnet >/dev/null || { echo "dotnet not on PATH" >&2; exit 1; }
command -v curl   >/dev/null || { echo "curl not on PATH" >&2; exit 1; }

# ---------------------------------------------------------------- harvest --
if [ "$DO_HARVEST" -eq 1 ]; then
    s86_state_read || { echo "no running session recorded; start one first" >&2; exit 1; }
    if ! kill -0 "${S86_PID:-0}" 2>/dev/null; then
        echo "recorded pid ${S86_PID:-?} is not alive; run spice86_stop.sh to harvest the exit dump" >&2
        exit 1
    fi
    echo "harvesting live session pid=$S86_PID via gdb 127.0.0.1:${S86_GDB_PORT}"
    if ! s86_dumpall_live "$S86_GDB_PORT"; then
        echo "live dump failed (is gdb installed and the GDB port open?)" >&2
        echo "the CFG will still be harvested on a clean spice86_stop.sh" >&2
        exit 1
    fi
    DEST=$(s86_harvest "$S86_DUMP_DIR" "${S86_LABEL}-live") || exit 1
    if s86_snapshot_ds "$S86_HTTP_PORT" "$DEST/live-ds.bin"; then
        echo "  data segment         $(wc -c < "$DEST/live-ds.bin") B     live-ds.bin"
    fi
    echo "harvest -> $DEST"
    s86_harvest_summary "$DEST"
    exit 0
fi

# ----------------------------------------------------------------- start ---
if s86_state_read && kill -0 "${S86_PID:-0}" 2>/dev/null; then
    echo "a session is already running (pid $S86_PID, label ${S86_LABEL:-?})" >&2
    echo "stop it first: $(dirname "$0")/spice86_stop.sh" >&2
    exit 1
fi

STAMP=$(date +%Y%m%d-%H%M%S)
[ -n "$LABEL" ] || LABEL="cfg-$STAMP"
RUN_DIR="$(s86_tmp_root)/$STAMP"
DUMP_DIR="$RUN_DIR/dump"
mkdir -p "$RUN_DIR" "$DUMP_DIR"

echo "copying pristine game files into $RUN_DIR"
cp -- "$REFS"/ultimaii.exe "$REFS"/player "$RUN_DIR"/
cp -- "$REFS"/mapx*  "$REFS"/monx*  "$REFS"/tlkx*  "$RUN_DIR"/ 2>/dev/null || true
cp -- "$REFS"/pic*   "$REFS"/monsters "$RUN_DIR"/ 2>/dev/null || true

if [ "$PATCH_ARROWS" = 1 ]; then
    # Patch the run copy only. The tool refuses if the expected gate bytes are
    # not present, so a wrong binary fails here rather than mid-capture.
    python3 "$ROOT/tools/patch_arrows.py" "$RUN_DIR/ultimaii.exe" \
        || { echo "--patch-arrows failed" >&2; exit 1; }
fi

if [ -n "$SEED_MAP" ]; then
    # Install a synthetic sector map into the run copy BEFORE the emulator is
    # launched. The game reads mapxNN during startup, so writing it after launch
    # silently leaves the real terrain in memory and every probe result is wrong.
    # Basename must be mapxNN so the sector is unambiguous.
    MAP_BASE=$(basename -- "$SEED_MAP")
    case "$MAP_BASE" in
        mapx??) [ -f "$SEED_MAP" ] || { echo "--seed-map: no such file: $SEED_MAP" >&2; exit 1; }
                cp -- "$SEED_MAP" "$RUN_DIR/$MAP_BASE" ;;
        *) echo "--seed-map: file must be named mapxNN (got $MAP_BASE)" >&2; exit 1 ;;
    esac
fi

if [ -n "$PLAYER_FILE" ]; then
    # Overwrite the blank with a previously created character. The game only
    # accepts the blank template for *creation*; an existing record is what 'P'
    # loads. Copied, never moved: the source run dir stays usable.
    [ -f "$PLAYER_FILE" ] || { echo "--player: no such file: $PLAYER_FILE" >&2; exit 1; }
    cp -- "$PLAYER_FILE" "$RUN_DIR/player"
    echo "seeding player from $PLAYER_FILE"
fi

: "${HTTP_PORT:=$(s86_pick_port 20180)}"
: "${MCP_PORT:=$(s86_pick_port 20181 "$HTTP_PORT")}"
: "${GDB_PORT:=$(s86_pick_port 20190 "$HTTP_PORT" "$MCP_PORT")}"

# Belt and braces, and it is the check that would have caught the collision instead of
# letting it surface as a wrong answer three tools later. A free port is not the same as a
# distinct port, and two APIs on one socket is not a configuration, it is a silent failure.
if [ "$HTTP_PORT" = "$MCP_PORT" ] || [ "$HTTP_PORT" = "$GDB_PORT" ] || [ "$MCP_PORT" = "$GDB_PORT" ]; then
    echo "FATAL: API ports are not distinct (http=$HTTP_PORT mcp=$MCP_PORT gdb=$GDB_PORT)."
    echo "       Two APIs on one socket means one of them does not exist, and tools that"
    echo "       connect will silently reach some OTHER emulator on that port."
    echo "       Stale Spice86 processes: $(pgrep -cf Spice86 || echo 0)"
    exit 1
fi

s86_export_display
[ "$MODE" = "watch" ] && {
    if [ -z "${WAYLAND_DISPLAY:-}" ] && [ -z "${DISPLAY:-}" ]; then
        echo "no Wayland or X display available; use --headless" >&2
        exit 1
    fi
}

LOG="$RUN_DIR/spice86.log"
# -r pins where Spice86 writes its dumps. Without it the emulator creates a
# subdirectory named after the SHA-256 of the emulated exe under the cwd, which
# works but makes the harvest path depend on a hash; pinning it keeps the stop
# script's search honest. The SHA-256 dir is still searched, as a fallback.
ARGS=( -e ultimaii.exe
       -r "$DUMP_DIR"
       --HttpApiPort "$HTTP_PORT"
       --McpHttpPort "$MCP_PORT"
       --GdbPort     "$GDB_PORT"
       --AllowIvtAddress0
       --Cycles      "$CYCLES" )
[ "$MODE" = "headless" ] && ARGS+=( --HeadlessMode Minimal )

echo "launching Spice86 (mode=$MODE cycles=$CYCLES http=$HTTP_PORT mcp=$MCP_PORT gdb=$GDB_PORT)"
( cd "$RUN_DIR" && setsid --fork dotnet "$DLL" "${ARGS[@]}" >"$LOG" 2>&1 & )

# setsid --fork reparents the emulator, so $! is the short-lived setsid and not
# the process we must signal. Find the real pid by the port we just picked: it
# is unique, because s86_pick_port only returns a free one.
PID=""
for _ in 1 2 3 4 5 6 7 8 9 10; do
    PID=$(pgrep -f "Spice86.*HttpApiPort ${HTTP_PORT}" | head -1 || true)
    [ -n "$PID" ] && break
    sleep 0.5
done
if [ -z "$PID" ] || ! kill -0 "$PID" 2>/dev/null; then
    echo "Spice86 did not stay up; last log lines:" >&2
    tail -20 "$LOG" >&2 || true
    exit 1
fi
echo "$PID" > "$RUN_DIR/.pid"
PGID=$(ps -o pgid= -p "$PID" | tr -d ' ')

s86_state_write "$PID" "$PGID" "$RUN_DIR" "$DUMP_DIR" "$LABEL" "$HTTP_PORT" "$MCP_PORT" "$GDB_PORT"

if s86_wait_api "$HTTP_PORT" 90; then
    echo "HTTP API ready: http://127.0.0.1:$HTTP_PORT/api"
    # Say where the ports live, because the read tools resolve from this file and a reader
    # who assumes 20180 will silently talk to whatever is squatting on it.
    echo "ports recorded in $(s86_state_file) -- read tools resolve them via tools/s86_ports.py"
    curl -fsS --max-time 5 "http://127.0.0.1:$HTTP_PORT/api/status" || true
    echo
else
    echo "warning: HTTP API did not answer within 90s; see $LOG" >&2
fi

if [ "$AUTO_PLAY" = 1 ]; then
    # ESC dismisses the title splash, P loads the previous character from ./player.
    # A fixed sleep rather than a state probe: the title has no cheap memory
    # signature that separates it from the poem screen, and a key sent too early is
    # silently swallowed. Verify afterwards, never here.
    echo "auto-play: sending $AUTO_PLAY_KEYS via the MCP endpoint"
    sleep 6
    python3 "$ROOT/tools/spice86_type.py" \
        --http-port "$HTTP_PORT" --mcp-port "$MCP_PORT" \
        --keys "$AUTO_PLAY_KEYS" --delay 1.5 \
        || echo "auto-play failed; the run is still at the title" >&2
fi

cat <<EOF

running
  pid          $PID  (pgid ${PGID:-?})
  run dir      $RUN_DIR
  dump dir     $DUMP_DIR
  log          $LOG
  harvest now  $(dirname "$0")/spice86_start.sh --harvest
  stop + dump  $(dirname "$0")/spice86_stop.sh

  MCP endpoint http://127.0.0.1:$MCP_PORT  (install it to drive the emulator)
  NOTE the game rewrites ./player in the run dir; that is why it is a copy.
EOF

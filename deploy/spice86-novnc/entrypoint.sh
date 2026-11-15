#!/bin/sh
# Xvfb + Spice86 (original Ultima II) + x11vnc + websockify, in one container.
#
# Shape follows deploy/novnc/entrypoint.sh (the engine container) on purpose, so the two
# are recognisably the same thing pointed at different binaries. Differences that matter:
#
#   * The run copy is made INSIDE the container, from the read-only /content mount. The
#     game rewrites its own `player` file, so it is never run in place — same rule the
#     host's tools/spice86_start.sh honours, enforced here by copying rather than by
#     convention.
#   * The CFG dump lands in /project/tmp/runs/<stamp>/dump, a host directory. Stopping the
#     container therefore leaves the harvest behind instead of losing it.
#   * Windowed mode, not --HeadlessMode: the point of this container is a person watching
#     and typing into the real emulator GUI over VNC.
#
# Deliberately supervisor-free. If Spice86 exits, everything exits and podman reports the
# container stopped, which is the honest signal.
set -eu

: "${DISPLAY_NUM:=99}"
: "${GEOMETRY:=1440x900x24}"
: "${VNC_PORT:=5902}"
: "${NOVNC_PORT:=6082}"
: "${HTTP_PORT:=20180}"
: "${MCP_PORT:=20181}"
: "${GDB_PORT:=20190}"
: "${CYCLES:=3000}"
: "${CONTENT_DIR:=/content}"
: "${SPICE86_DIR:=/opt/spice86}"
: "${RUNS_DIR:=/project/tmp/runs}"
: "${PATCH_ARROWS:=1}"
: "${PLAYER_FILE:=}"
: "${SEED_MAP:=}"
: "${EXE_NAME:=ultimaii.exe}"
: "${SDL_AUDIODRIVER:=dummy}"
export SDL_AUDIODRIVER

DISPLAY=":${DISPLAY_NUM}"
export DISPLAY

# --- display ---------------------------------------------------------------
# 24-bit minimum. 8-bit ("320x200x8", the obvious choice for a four-colour game) makes
# SDL refuse the visual with "Palettized video modes are no longer supported".
#
# At least as big as the Spice86 window. X places an oversized window anyway and you see
# a cropped picture with no error anywhere — the same trap deploy/novnc/README.md records
# for the engine.
#
# The lock file: `podman restart` reuses the container's writable layer, a surviving
# /tmp/.X99-lock makes Xvfb die with the fatal "Server is already active for display 99",
# and the container looks quiet rather than broken. This is our own ephemeral /tmp and the
# lock belongs to the server we are about to start.
rm -f "/tmp/.X${DISPLAY_NUM}-lock" "/tmp/.X11-unix/X${DISPLAY_NUM}"
echo "spice86-novnc: Xvfb :${DISPLAY_NUM} at ${GEOMETRY}"
Xvfb ":${DISPLAY_NUM}" -screen 0 "${GEOMETRY}" -nolisten tcp &
XVFB_PID=$!
# Fixed wait rather than an xdpyinfo poll: xdpyinfo is another package and the container
# has no reason to carry X client utilities to answer "is the socket up yet".
sleep 2

# --- run copy --------------------------------------------------------------
[ -d "$CONTENT_DIR" ] || { echo "no ${CONTENT_DIR} mount (game files)" >&2; exit 1; }
[ -f "${SPICE86_DIR}/Spice86" ] || { echo "no ${SPICE86_DIR}/Spice86 mount (GPL emulator build)" >&2; exit 1; }

STAMP=$(date +%Y%m%d-%H%M%S)
RUN_DIR="${RUNS_DIR}/${STAMP}"
DUMP_DIR="${RUN_DIR}/dump"
mkdir -p "$RUN_DIR" "$DUMP_DIR"
echo "spice86-novnc: copying ${CONTENT_DIR} -> ${RUN_DIR}"
# The same file set tools/spice86_start.sh copies: the 37 KB engine plus its sidecar
# sector maps, monster data, NPC dialogue and pictures. Ultima II is not one file.
cp -- "${CONTENT_DIR}/${EXE_NAME}" "${CONTENT_DIR}/player" "$RUN_DIR"/ 2>/dev/null \
    || { echo "missing ${EXE_NAME} or player in ${CONTENT_DIR}" >&2; exit 1; }
cp -- "${CONTENT_DIR}"/mapx*  "$RUN_DIR"/ 2>/dev/null || true
cp -- "${CONTENT_DIR}"/monx*  "$RUN_DIR"/ 2>/dev/null || true
cp -- "${CONTENT_DIR}"/tlkx*  "$RUN_DIR"/ 2>/dev/null || true
cp -- "${CONTENT_DIR}"/pic*   "$RUN_DIR"/ 2>/dev/null || true
cp -- "${CONTENT_DIR}"/monsters "$RUN_DIR"/ 2>/dev/null || true

if [ "${PATCH_ARROWS}" = "1" ]; then
    # Real arrow keys instead of the 8/2/4/6 numpad. The tool refuses if the expected
    # gate bytes are absent, so a wrong binary fails here rather than mid-play.
    echo "spice86-novnc: patching arrow keys into the run copy"
    python3 /project/tools/patch_arrows.py "${RUN_DIR}/${EXE_NAME}" \
        || { echo "patch_arrows failed" >&2; exit 1; }
fi

if [ -n "$PLAYER_FILE" ]; then
    # An existing record is what 'P' loads; the pristine 384-byte blank is only the
    # template for *creation*. Path is inside the container (e.g. /project/tmp/runs/seed-player).
    [ -f "$PLAYER_FILE" ] || { echo "PLAYER_FILE not found: ${PLAYER_FILE}" >&2; exit 1; }
    cp -- "$PLAYER_FILE" "${RUN_DIR}/player"
    echo "spice86-novnc: seeded player from ${PLAYER_FILE}"
fi

if [ -n "$SEED_MAP" ]; then
    # Must be installed before launch: the game reads mapxNN at startup, so writing it
    # after the emulator is up silently leaves the real terrain in memory.
    MAP_BASE=$(basename -- "$SEED_MAP")
    case "$MAP_BASE" in
        mapx??) [ -f "$SEED_MAP" ] || { echo "SEED_MAP not found: ${SEED_MAP}" >&2; exit 1; }
                cp -- "$SEED_MAP" "${RUN_DIR}/${MAP_BASE}" ;;
        *) echo "SEED_MAP must be named mapxNN (got ${MAP_BASE})" >&2; exit 1 ;;
    esac
fi

# --- resume overlay --------------------------------------------------------
# Set by up.sh --resume. The previous run's own files go on top of the pristine copy, so a
# hard restart lands back in the same game rather than a fresh one. Sector files are
# included deliberately: resuming "exactly the last game" means the world the last run
# left behind, and the list is printed so a restored map is visible instead of silent.
# See the death-is-terminal note in up.sh; this exists because the game gives no other way
# back to the menu.
if [ -n "${RESUME_DIR:-}" ]; then
    if [ ! -d "$RESUME_DIR" ]; then
        echo "spice86-novnc: RESUME_DIR not found: ${RESUME_DIR}" >&2
        exit 1
    fi
    echo "spice86-novnc: resuming from ${RESUME_DIR}"
    restored=0
    for f in "$RESUME_DIR"/player "$RESUME_DIR"/mapx* "$RESUME_DIR"/monx* "$RESUME_DIR"/tlkx*; do
        [ -f "$f" ] || continue
        base=$(basename -- "$f")
        # cksum, not cmp: this image has no `cmp`, and a missing binary exits non-zero, so
        # `cmp -s` reported every file as differing and the log claimed 82 files restored
        # when three had changed. And cksum prints the path, so only the first two fields
        # (CRC and byte count) are comparable across two different directories.
        if [ -f "${RUN_DIR}/${base}" ] &&
           [ "$(cksum "$f" | cut -d' ' -f1,2)" = "$(cksum "${RUN_DIR}/${base}" | cut -d' ' -f1,2)" ]; then
            continue
        fi
        cp -- "$f" "${RUN_DIR}/${base}"
        echo "spice86-novnc:   restored ${base}"
        restored=$((restored + 1))
    done
    echo "spice86-novnc: ${restored} file(s) restored over the pristine copy"
fi

# --- emulator --------------------------------------------------------------
echo "spice86-novnc: Spice86 -e ${EXE_NAME} (cycles ${CYCLES}, http ${HTTP_PORT}, mcp ${MCP_PORT}, gdb ${GDB_PORT})"
# --AllowIvtAddress0 is mandatory for this game (see AGENTS.md / spice86_start.sh).
# -r pins the dump directory; without it Spice86 writes under a SHA-256-named subdir of
# the cwd and the harvest path depends on a hash.
( cd "$RUN_DIR" \
  && "${SPICE86_DIR}/Spice86" \
        -e "$EXE_NAME" \
        -r "$DUMP_DIR" \
        --HttpApiPort "$HTTP_PORT" \
        --McpHttpPort "$MCP_PORT" \
        --GdbPort "$GDB_PORT" \
        --AllowIvtAddress0 \
        --Cycles "$CYCLES" \
        >"${RUN_DIR}/spice86.log" 2>&1 ) &
U2_PID=$!

# --- vnc -------------------------------------------------------------------
# -localhost: the raw VNC port stays inside the container's network namespace. What the
# LAN sees is the noVNC web socket, which is the owner's standing call for this box
# ("it's just our LAN") recorded in deploy/novnc/README.md; adding a password later is a
# one-flag change here and not a re-plumb.
x11vnc -display ":${DISPLAY_NUM}" -forever -shared -localhost -rfbport "$VNC_PORT" \
    >/tmp/x11vnc.log 2>&1 &
VNC_PID=$!

websockify --web=/usr/share/novnc "${NOVNC_PORT}" "localhost:${VNC_PORT}" \
    >/tmp/websockify.log 2>&1 &
WS_PID=$!

# --- readiness -------------------------------------------------------------
# Best effort, and it says so. The API binding to loopback vs 0.0.0.0 decides whether the
# host's tools can reach this emulator, so the log line is the answer, not a guess.
i=0
while [ $i -lt 60 ]; do
    curl -fsS --max-time 2 "http://127.0.0.1:${HTTP_PORT}/api/status" >/dev/null 2>&1 && break
    kill -0 "$U2_PID" 2>/dev/null || break
    i=$((i + 1)); sleep 0.5
done
if ! kill -0 "$U2_PID" 2>/dev/null; then
    # Fail loudly. A container that comes up with a dead emulator and a live VNC port shows
    # a person a black screen and no explanation; the reason is always in the log.
    echo "spice86-novnc: Spice86 exited during startup; log tail:" >&2
    tail -25 "${RUN_DIR}/spice86.log" >&2 || true
    kill "$WS_PID" "$VNC_PID" "$XVFB_PID" 2>/dev/null || true
    exit 1
fi
if curl -fsS --max-time 2 "http://127.0.0.1:${HTTP_PORT}/api/status" >/dev/null 2>&1; then
    echo "spice86-novnc: HTTP API up on ${HTTP_PORT}"
    # Give the emulator window X input focus, or nothing typed in the browser goes nowhere.
    #
    # windowfocus, not windowactivate: there is no window manager in this container, and
    # xdotool says so ("Your windowmanager claims not to support _NET_ACTIVE_WINDOW").
    # Measured: with no focus set, a key sent over X was swallowed and the screen did not
    # change at all; `xdotool windowfocus --sync <win> key d` moved the game straight into
    # the demonstration. --sync because SDL takes a moment to map the window.
    i=0
    while [ $i -lt 20 ]; do
        WIN=$(xdotool search --name Spice86 2>/dev/null | head -1) && [ -n "$WIN" ] && break
        i=$((i + 1)); sleep 0.5
    done
    if [ -n "${WIN:-}" ]; then
        xdotool windowfocus --sync "$WIN" >/dev/null 2>&1 || true
        echo "spice86-novnc: focused window $WIN"
    else
        echo "spice86-novnc: warning: no Spice86 window found; click the noVNC canvas before typing" >&2
    fi
else
    echo "spice86-novnc: warning: HTTP API not answering on ${HTTP_PORT}; see ${RUN_DIR}/spice86.log" >&2
fi

cat <<EOF

  noVNC     http://<host>:${NOVNC_PORT}/vnc.html   (no password)
  run dir   ${RUN_DIR}
  dump dir  ${DUMP_DIR}   (harvested when the run exits cleanly)
  log       ${RUN_DIR}/spice86.log

  From the host, with --network host, the existing tools drive this emulator:
    python3 tools/spice86_screenshot.py /var/tmp/u2.png --mcp-port ${MCP_PORT}
    python3 tools/spice86_type.py --keys Escape,p --mcp-port ${MCP_PORT}

EOF

# If Spice86 exits (window closed, game ended, or a crash), tear everything down so the
# container stops instead of sitting on a dead X display. The `|| STATUS=$?` is not
# decoration: under `set -e` a bare `wait` returning the child's non-zero status would end
# the script before the teardown and the summary ever ran.
wait "$U2_PID" && STATUS=0 || STATUS=$?
echo "spice86-novnc: Spice86 exited (${STATUS}); shutting down"
echo "spice86-novnc: dump contents:"
ls -la "$DUMP_DIR" 2>/dev/null | tail -8 || true
kill "$WS_PID" "$VNC_PID" "$XVFB_PID" 2>/dev/null || true
exit "$STATUS"

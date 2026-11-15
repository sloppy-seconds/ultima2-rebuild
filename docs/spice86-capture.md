# Running Ultima II in Spice86 to capture data

How to start the game under emulation and read its live memory. Everything referenced here
lives inside this project directory.

## What you need

| Item | Path |
| --- | --- |
| Game binary and data (source) | `references/ultima2/` |
| `ultimaii.exe` | `references/ultima2/ultimaii.exe` (37,344 bytes) |
| Blank player disk | `references/ultima2/player` (384 bytes) |
| Map, monster, dialog, picture data | `references/ultima2/mapx*`, `monx*`, `tlkx*`, `pic*`, `monsters` |

Gameplay mutates files (the game rewrites `player`), so the run always copies from the pristine
`references/ultima2/` backup into a fresh, timestamped scratch directory under the project's
`tmp/` folder. Nothing is ever run in place.

External requirements:

- `dotnet` on `PATH`, .NET 8 or newer (the vendored build is self-contained .NET 10, but the
  `dotnet Spice86.dll` invocation below uses the host `dotnet`).
- A Spice86 build. **Vendored in this project** at `tools/spice86/` — set
  `SPICE86_DLL="$PWD/tools/spice86/Spice86.dll"` from the project root. See `tools/spice86/README.md`
  for provenance.
- A graphical session. Spice86 draws a window, so it needs `DISPLAY` or `WAYLAND_DISPLAY`.

## The blank player disk matters

`references/ultima2/player` is 384 bytes, all zero except the byte `0x1a` at offset `0x100`.
That exact content is what makes the game accept a new character. Any other content and it
prints `NOT A BLANK PLAYER DISK` and refuses to start.

The game writes progress back into the `player` file in its working directory. **Always copy
the data into a fresh scratch directory and run there.** The launch below copies from the
pristine `references/ultima2/` backup into `tmp/<timestamp>/`, so neither the backup nor the
`original/` working copy is ever mutated.

## Headed or headless — decide before you launch

This cannot be changed after launch, and it determines what you are able to observe.

| | `--watch` (headed) | `--headless` |
| --- | --- | --- |
| window on a screen | yes, you can watch it | none (`--HeadlessMode Minimal`) |
| keystrokes | `tools/spice86_keys.py` (XTEST, needs focus) | **`tools/spice86_type.py`** — MCP `send_keyboard_key`, no window needed |
| screenshot | `tools/vga_ring.py`, `tools/screenshot_diff.py` | MCP `screenshot` tool returns PNG base64 inline |
| HTTP memory reads | yes | yes |
| MCP memory writes | yes | yes |
| save seeding with `--player` | yes | yes |
| CFG harvest | on clean exit, or live `dumpAll` | **live `dumpAll` only** |
| teardown | close the window, or `spice86_stop.sh` | `spice86_stop.sh` |

**Headless is not input-blind.** Spice86's MCP endpoint exposes 63 tools including
`send_keyboard_key` (args `key`, `isPressed`; a full press is down then up) and
`screenshot`. `tools/spice86_type.py` wraps the MCP streamable-HTTP handshake so keys
are one command, and `--auto-play` uses it. This was discovered late, and three
captures were written off as needing a human before it was found — check here first.

The claim that keystrokes require a display was true only of the **xdotool path**. The
game reads the keyboard through INT 16h at exactly one site (`asm/4F4D_key_poll`,
AH=01h then AH=00h), which is the BIOS typeahead ring, so a key is reachable by any
route that reaches the emulator — no X server required.

**The supported path is `send_keyboard_key` over MCP, and it is the only one that works on
this build.** Poking the ring at `0040:001E` directly — kept as `enqueue_bios` in
`tools/spice86_type.py` — **does not deliver here**, measured 2026-11-16: the live head and
tail are `0x043C` and `0x2000`, both outside the `0x1E..0x3E` the module documents, so the
game is not reading that buffer. The old code silently reset an out-of-range tail to `0x1E`
and wrote anyway; the write landed, read back correct and printed `ok`, and the key was
consumed by nobody. It now **refuses with an explanation** instead, because a key that
vanishes is indistinguishable from a key the game ignored, and a capture driven through it
would have written down a confident wrong answer. Note the module's own docstring already
recorded `head == tail == 0x041E`, which contradicts its constants two inches away — the
observation was in the prose and the code trusted the constants.

**...and modifiers cannot go through MCP at all.** `send_keyboard_key` takes a single
`PcKeyboardKey` and has no modifier concept, so `ctrl-y` sent that way arrives as a plain
`Y`. A modified key needs the ring, which is the blocker G30 is still sitting on: the
modifier encoding is solved and self-tested (`--keys ctrl-y` resolves to scan `0x15`,
ascii `0x19`; `--selftest` pins Ctrl+A..Z to `0x01..0x1A`, so Ctrl-Z is `0x1A`, the DOS
end-of-file byte, and `0x18` is Ctrl-X), but there is nowhere to deliver it. **Ctrl-Y is
`0x19`, not `0x18`.** `--keymap` and `--selftest` resolve and check the table without a
running emulator, and `--selftest` is wired into `check_all.sh`.

**Measured on this build (2026-10-21): headless Spice86 ignores both SIGTERM and
SIGINT** — it sat through each until SIGKILL. So in headless mode no signal produces
the exit dump and the harvest must never depend on one. `tools/spice86_stop.sh`
therefore runs `monitor dumpAll` over the GDB port *first*; that is what guarantees
the CFG is written. The signal sequence after it is teardown, not flush.

**An agent shell usually has no display.** On this host `WAYLAND_DISPLAY` and
`DISPLAY` are both unset in an agent context and **`Xvfb` is not installed**, so
there is no way to conjure a focusable window on the host itself. `--headless` is
the default for unattended work.

**That is not a keystroke limitation, and it used to be written as one.** An earlier
draft of this paragraph said keystroke-driven captures were "simply unavailable", and
that sentence was false from the moment `deploy/spice86-novnc/` landed — and arguably
before, given `spice86_type.py` two paragraphs above. Two working routes exist:

* **No window at all** — `tools/spice86_type.py --keys A,Right` over the MCP endpoint.
  This is the path `--auto-play` uses and it works headless.
* **A real virtual display** — `deploy/spice86-novnc/up.sh` builds and starts a container
  holding Xvfb + Spice86 + x11vnc + websockify, `DISPLAY=:99` inside, noVNC published on
  host `:6085`. `--player FILE` seeds a character, `--no-patch` leaves the arrow keys
  unpatched, `--down` tears it down. Read `deploy/spice86-novnc/README.md` before using it.
* **Injecting keys into that container crashes Spice86** — `InvalidOperationException: Collection
  was modified` in `CfgCpuViewModel.RegenerateGraphFromNodeAsync`, racing the pause
  `send_keyboard_key` causes. Memory **reads** and screenshots are safe, so the working split is:
  the human types in the browser, the host only watches. Use `--mcp-port 20181` on the read tools.

So a capture that genuinely needs keypresses is not blocked. Say which route you used.

**Headless is not a dead end, because keystrokes are rarely the right tool.**
Position and sector are set by editing the save and relaunching with `--player`, not
by walking. Poking `DS:00/01` in a live emulator writes and reads back, but that pair
is a mirror and not the source, and movement then stops working entirely — see the
`tools/u2_edit.py` docstring, which records this the hard way. Plan a capture around
**seeding state** rather than driving input and most of them become headless.

## Launch

**Use the scripts.** They copy the pristine `references/ultima2/` backup into a
fresh timestamped run directory, pick free ports, write the session state file the
stop script needs, honour `U2V2_SPICE86_DLL` for A/B-testing a patched build, and
harvest on the way out. Hand-rolling `dotnet` skips every one of those, and the
harvest loss is silent. The raw invocation is documented further down only so the
scripts can be read, not as the way to launch.

```bash
tools/spice86_start.sh -h                       # every option, always current

tools/spice86_start.sh --watch --patch-arrows --name myrun    # headed, real arrows
tools/spice86_start.sh --headless --name myrun                # headless

# seed a character instead of creating one in-game (the only way to change sector)
python3 tools/u2_edit.py maxout  tmp/seed/player
python3 tools/u2_edit.py set     tmp/seed/player sector 0
python3 tools/u2_edit.py setpos  tmp/seed/player 30 32     # X and Y only
tools/spice86_start.sh --headless --player tmp/seed/player --name seeded

tools/spice86_start.sh --headless --player tmp/seed/player --auto-play --name seeded

# install a synthetic sector before launch (the game reads mapxNN at startup)
tools/spice86_start.sh --headless --seed-map /var/tmp/probe/mapx20 --name probe

# drive / inspect a running session, no window needed
python3 tools/spice86_type.py --probe            # CS:IP, key ring, player name
python3 tools/spice86_type.py --list            # the 63 MCP tools and their args
python3 tools/spice86_type.py --keys Escape,p   # ESC then P (title -> world)

tools/spice86_start.sh --harvest    # dump the CFG of the live session, keep running
tools/spice86_stop.sh               # dumpAll -> teardown -> harvest to captures/spice86/<label>/
```

One session at a time, deliberately: the state file is singular and two emulators
fighting over the same ports is the failure mode this avoids.

### What the script actually runs

```bash
# Fresh, timestamped scratch dir inside the project, sourced from the pristine backup.
RUN="tmp/$(date +%Y%m%d-%H%M%S)"
mkdir -p "$RUN"
cp references/ultima2/ultimaii.exe references/ultima2/player "$RUN"/
cp references/ultima2/mapx* references/ultima2/monx* "$RUN"/
cp references/ultima2/tlkx* references/ultima2/pic* references/ultima2/monsters "$RUN"/

cd "$RUN"
export DOTNET_ROLL_FORWARD=Major
dotnet "$SPICE86_DLL" \
    -e ultimaii.exe \
    --HttpApiPort 20180 \
    --McpHttpPort 20181 \
    --AllowIvtAddress0 \
    --Cycles 3000
```

`--HeadlessMode Minimal` is added when `--headless` is passed.

`tmp/` here is the project-local scratch folder (disk-backed), created fresh per run and named
by timestamp so runs never collide and each is reproducible from the backup. It is **not** the
system `/tmp`, which on this machine is a tmpfs — do not use `/tmp` for run directories or
captures, because every byte written there is taken from RAM.

### Why each flag

| Flag | Reason |
| --- | --- |
| `-e ultimaii.exe` | the entry executable, relative to the working directory |
| `--HttpApiPort 20180` | the read API used for capture, see below |
| `--McpHttpPort 20181` | the MCP server, which is the only way to write memory |
| `--AllowIvtAddress0` | required, see below |
| `--Cycles 3000` | approximates the 1983 machine the game calibrates itself against |

**`--AllowIvtAddress0` is not optional.** The game does not install an INT 0 handler, so the
divide-error vector still points at `0:0`. Spice86 refuses to jump there and stops with
`Interrupt vector 0x00 points to 0:0 (uninitialized)`. That message reads like a crash and is
not one. It is the emulator being cautious about a real program's loose initialisation.

`DOTNET_ROLL_FORWARD=Major` lets a .NET 8 or 9 build run on a .NET 10 runtime.

### Display environment

A shell that was not started from the desktop session does not inherit the display
environment. Set it explicitly:

```bash
export WAYLAND_DISPLAY=wayland-0
export DISPLAY=:0
export XAUTHORITY=/run/user/1000/xauth_<your-file>
export XDG_RUNTIME_DIR=/run/user/1000
```

Find the right values for your session with `ls /run/user/1000/` and `ls /tmp/.X11-unix/`.
Wayland first is preferable, since the compositor may prefer it for the Avalonia window.

### Ports

A killed Spice86 process can leave its previous port bound for a while, and the defaults
20000 and 8081 are often taken. Probe for free ports before launching rather than assuming.

## Which tool for which job

This table exists because the tooling was re-derived from scratch several times.
Check it before writing anything new against a live session. It covers **capture**; for
building an artifact, playing either binary in a browser, or seeding a save at a specific
entry location, see `docs/tooling-procedures.md` § "Build, play, seed" — same reason, same
kind of table, different three jobs.

| job | tool | needs a display? |
| --- | --- | --- |
| launch / label / seed a run | `tools/spice86_start.sh` (`-h` for options) | no (`--headless`) |
| **play the original in a browser** | **`deploy/spice86-novnc/up.sh`** | **no** — brings its own Xvfb + noVNC on :6085 |
| **play our engine in a browser** | **`deploy/novnc/up.sh`** | **no** — same shape pointed at the Phase 4 binary, :6082 |
| **send keys to a headless run** | **`tools/spice86_type.py --keys Escape,p`** | **no** — MCP |
| enter the world unattended | `tools/spice86_start.sh --auto-play` | no |
| list what the emulator can do | `tools/spice86_type.py --list` (63 MCP tools) | no |
| screenshot, headless | MCP `screenshot` (PNG base64) | no |
| stop the CPU at an address and read state there | `tools/bp_read.py --ip 0CEE --cs 17D --state --ret --stack 40` | no — RSP on the gdb port |
| **read a live byte/word/block** | `tools/spice86_type.py --peek 8be:60:1` (hex `SEG:OFF[:LEN]`) | no — HTTP |
| **force a live value to build a scenario** | `tools/spice86_type.py --poke 8be:60=00` (hex `SEG:OFF=BYTES`, always prints the read-back) | no — MCP |
| **prove what the player saw** | `tools/spice86_screenshot.py NAME.png`, **before and after** the action | no — MCP |
| **capture a one-shot event during play** | **`tools/spice86_play_capture.py --out /var/tmp/u2/cap --bp <ip> --frames`** — the sidecar: DS-field log + content md5s + breakpoints + frames, all at once | no — HTTP + RSP + MCP |

**A teardown CFG harvest cannot hold a one-shot event, and a screenshot loop records no writes.**
This is the reason `spice86_play_capture.py` exists, and it is measured, not folklore: the
`minax-fight-live` owner session (2026-11-12) was captured with a 1/sec screenshot loop plus a
`dumpAll` at teardown, and the resulting `ExecutionFlow` held only **3,782 instructions with zero
hits** for the victory (`0x74A0`), the teleport gate (`0x2C2D`) or the Minax strike (`0x0F8E`) — the
trace ring rolls in seconds, so a dump taken after the kill records the idle loop, not the kill.
Frames show what the player saw but say nothing about what was *written*, so a persist question
("does the ending save the sector?") is invisible to both. For any one-shot transition — a kill, a
teleport, a mode change, a sector write — run `spice86_play_capture.py` alongside the play: it logs
DS fields on change (memory persists even when the trace rolls), md5s the content files (a write is
a diff), and arms breakpoints *before* the event (they fire on hit regardless of the ring). Do not
hand-roll another shotloop.

**Pair the frame with the claim.** A memory read and a trace say what changed and whether it ran;
only a frame says what the player saw, and the two can disagree — `cmd_V`'s helm branch executed and
consumed its charge while rendering something nothing in the bytes predicted. So a capture that
establishes visible behaviour names its frame, and the state that gates the action is read *before*
the press as well as after: a post-hoc reading of a consumed counter looks exactly like a reading of
an absent one. See AGENTS.md § 3, "A behavioural claim needs a frame".

`bp_read` is the only thing that speaks `monitor` for capture purposes, and the monitor
surface is bigger than the HTTP API: `help` lists `dumpAll`, `breakCycles`, `breakCsIp`,
`breakStop`, `callStack`, `peekRet`, `state` and `ramx`. `state` is the only way to get
registers at all — the HTTP API reports `cs`/`ip` and nothing else, no `SS`, no `SP`, no
`DI`. Full findings in `docs/monster-spawn.md` § 15.2.

`deploy/spice86-novnc/` and `tools/vnc/` are the two display paths and they are not the same
thing: `tools/vnc/` containerises only the screen and runs Spice86 on the host, while
`deploy/spice86-novnc/` runs the emulator inside the container too, publishes the HTTP/MCP/GDB
APIs on the host network, and leaves its CFG dump in `/var/tmp/u2/spice86-runs/<stamp>/dump`
instead of in a container. Read `deploy/spice86-novnc/README.md` before using either.

### Reading the screen headless — the recipe that works

`tools/spice86_screenshot.py out.png` calls the MCP `screenshot` tool and writes the PNG.
It needs no display. `read_video_state` reports **320x200, BufferSize 64000**, so the
renderer exposes a linear 8-bpp framebuffer.

**Do not try to reconstruct the screen from memory.** Two attempts failed and each looked
plausible:

* reading `0xB8000` as a text page returns box-drawing noise — the game draws its text as
  **glyphs in graphics mode**, so there is no character cell to read;
* reading `0xB8000` as CGA mode-4 planes produced a structured but wrong image (content in
  the top half, black below). That structure is what makes it dangerous — it looks like a
  partial success. The framebuffer is 64000 bytes, not 16384.
* a turn-recorder `turns/NNNNN.vga` is **not a screen at all**. It is 6400 bytes — the span
  at `VGA_BASE` the game's own screen-invert routine touches, per `VGA_LEN` in
  `tools/turn_recorder.py` — not the 64000-byte framebuffer, and decoding it as text or as
  planes yields confident noise. For a historical recording, read the paired `.ds` dump for
  state and re-run live if pixels are needed.

**The MCP tool list is 63 entries.** An earlier check truncated the JSON at 4000 characters,
showed 4 tools, and `screenshot` appeared to be missing — which sent a whole detour off
into hand-decoding video memory. `spice86_screenshot.py --list-tools` now prints the full
list and counts it.

### The `CMD:` log is the feedback channel

The bottom of the screen keeps a rolling command log, and it is the fastest way to learn
what the game made of an injected key:

| log line | meaning |
| --- | --- |
| `CMD: TRANSACT-PASS` | `T` was received, the handler ran, and it decided to do nothing |
| `CMD: - ILLEGAL COMMAND!` | the key is not a command the game accepts |

**Arrow keys are rejected as illegal commands.** `Left, Left, Up` produced three
`ILLEGAL COMMAND!` lines and the player did not move. So every earlier conclusion of the
form "the player stood at X and pressed T" was really "the player was teleported to X and
never moved", and the facing hypothesis was never tested at all. Movement keys need to be
worked out before any position-dependent experiment means anything.

### Two things that must be right before any `T` experiment means anything

**1. Arrow keys need `--patch-arrows` at launch.** It is opt-in in `spice86_start.sh`. Without
it the game's AL-gate at runtime `4F85` rejects real arrow keys and every one returns
`ILLEGAL COMMAND!`. `tools/spice86_keys.py` documents this (mode `real` requires the patch);
read it before hand-rolling key injection. The symptom of forgetting it is indistinguishable
from "the game ignores arrows", and it cost several wasted runs.

**2. `T` and the direction must be queued together.** The `TRANSACT` handler reads a pending
direction and gives up immediately if the buffer is empty. With a human-scale inter-key gap
the log shows `CMD: TRANSACT-PASS` — the handler ran, found nothing, and returned before the
arrow arrived. Sent as one batch:

```bash
python3 tools/spice86_type.py --keys T,Up --delay 0.02
# -> CMD: TRANSACT-NORTH      (not PASS)
```

**This is a harness trap with a silent failure mode.** Any driver that injects keys one at a
a time with a normal delay will see `PASS` forever and conclude the feature is broken. The
proven-good setup that produced `TRANSACT-NORTH`: `--headless --patch-arrows`, player at
(48,27) in sector 31, jester (`0x64`) confirmed adjacent at (48,26) by a live read.

Still open: `TRANSACT-NORTH` printed no dialogue. Either the jester is not a transact
target, or the response renders somewhere other than the `CMD:` log.
| stop and harvest the CFG | `tools/spice86_stop.sh` | no |
| harvest mid-run without stopping | `tools/spice86_start.sh --harvest` | no |
| read memory | HTTP API `GET /api/memory/<phys>/range/<len>` | no |
| write memory (build a scenario) | MCP endpoint `write_memory` on `--McpHttpPort` | no |
| find the safe polling rate | `tools/poll_probe.py` | no |
| record state on each turn | `tools/turn_recorder.py` | no |
| set sector / position / stats | `tools/u2_edit.py` (`show\|set\|setpos\|maxout`) | no |
| synthesise a probe sector | `tools/u2_mkprobe.py` + `--seed-map` | no |
| render the text screen from a dump | `tools/vga_screen.py` | no — reads memory, not the window |
| render the overworld from a dump | `tools/map_render.py` | no |
| continuous frame ring / screenshots | `tools/vga_ring.py`, `tools/screenshot_diff.py` | **yes** |
| send keystrokes, headed | `tools/spice86_keys.py` (xdotool/XTEST) | **yes** |
| screenshots, headed | `tools/vga_ring.py`, `tools/screenshot_diff.py` | **yes** |
| dungeon-specific probe | `tools/dungeon_capture.py` | no |
| prove a routine executed | `tools/cfg_printed.py` (reads `ExecutedInstructions`) | no |

**`ExecutedInstructions` is a rolling window, not a session log, and it is small.** Measured
2026-11-12 in `deploy/spice86-novnc`: a harvest taken seconds after a one-shot command held
2241 game-segment instructions, all distinct, and the command's own addresses were absent —
while the command tail, which runs on every turn, was present. Dumping immediately after the
event does not fix it; the window had already scrolled past within seconds of ordinary play.
So the row above is true for "did this ever run in a long-running session" and false for
"did this run just now". For a one-shot — a command pressed once, a branch taken once — the
reliable witness is a **memory read of the effect**, taken over the MCP endpoint while the
session is live. That is how `cmd_N` was finally confirmed: `DS:E3` went `09 -> 08 -> 07` and
`DS:26A` read `0x0F` (set to `0x14`, decayed five commands), while every `cmd_N` address in
the same dump was absent. Two claims were written from block lists and both were wrong; the
bytes in memory were right the first time.

Two things worth internalising from that table. **The screen is a memory read, not a
screenshot** — `vga_screen.py` reconstructs the text mode from the VGA buffer in a
dump, so "what did the game display" is answerable headless even without the MCP
`screenshot` tool. And **input is not window-bound either**: the only rows left that
truly need a display are the xdotool path and pixel-diff screenshots, both of which
have a headless equivalent above. If a capture seems to require walking somewhere,
prefer seeding a save (`--player` + `u2_edit.py set sector`); if it requires a
keypress, `spice86_type.py` has it.

## Reading memory: the HTTP API

The API is **read-only**. It answers two shapes of request.

### Status

```bash
curl -s http://127.0.0.1:20180/api/status
```

Reports `CS` and `IP` plus the cycle count. It does **not** report `DS`, so you must know the
data segment in advance.

### Memory

```bash
curl -s http://127.0.0.1:20180/api/memory/<physical>/range/<length>
```

The path takes a **physical address**, so compute it yourself:

```
physical = segment * 16 + offset
```

The response is JSON with a `values` array of byte integers, not a raw binary body:

```json
{"values": [138, 190, 0, 4, ...]}
```

### Address model: file offset, runtime label, and linear address are three different things

**Linear = file offset + `0x17D0`.** Verified by disassembly on 2026-10-23: `asm/0CC0_sub_0cc0.asm`
(file offset `0x0CC0`) lives at linear `9360`, and `read_disassembly` there returns exactly
that file's body. The CFG's "segment `017D`" is the same base expressed as a segment; segment
`0x249` offset `0` is the same linear address.

Two traps, both walked into:

* The `L_000ec0` label numbering inside the asm files suggests `runtime = file + 0x200`. It is
  **not** an address you can compute breakpoints from — `0x17D:0xEC0` is string data (`ARMS P…`).
  Labels are anchors, not a second address space.
* `search_memory` with a hand-assembled multi-instruction pattern can miss while a short fragment
  matches everywhere, because common idioms (`mov bl,[0x1f]` / `mov di,bx` matched eight times)
  are not unique. Search a fragment, then disassemble each hit and compare the body.

For breakpoints, prefer `CPU_EXECUTION_ADDRESS` on a code linear address over `MEMORY_WRITE` on
a data address whenever the effective address is computed at run time — a data breakpoint on a
fixed slot offset silently misses every write that goes through an index register.

### The data segment

For this game, once the main body is running, **`DS = 0x08BE`**. Confirmed by the player name
appearing at `DS:0x36`.

To dump the whole data segment:

```bash
curl -s "http://127.0.0.1:20180/api/memory/$((0x08BE * 16))/range/65536"
```

A 512-byte window covers the player record and globals. The monster arrays sit above that
window, so a full 64 KiB read is needed for them.

## Writing memory: the MCP server

The HTTP API cannot write. Setting a value, which is how you construct a test scenario, goes
through the MCP endpoint at `--McpHttpPort`, using the `write_memory` tool with arguments
`segment`, `offset` and `data` as a hex string.

This is the only reliable way to build a controlled before-and-after: write a known value,
take an action, read the result.

**Use `tools/spice86_type.py --poke` rather than speaking MCP by hand.** It wraps `write_mem`
and reads the bytes back in the same invocation, and it exits non-zero when the read-back
disagrees, which is the failure mode a silent write has. `--peek` is the matching read and
needs no MCP session. Forcing an input is often the whole experiment: docs/monster-spawn.md
§ 15.4 pinned the spawn selector in one afternoon only once `DS:60` could be poked, where
waiting for the game to produce each value had failed twice.

## Capture pattern that actually works

The game prints text far faster than it can be read from the window, and clears it quickly.
Reading the screen is not a dependable capture method. Two approaches that are:

**1. Diff around one action.** Snapshot, perform exactly one in-game action, snapshot again,
diff. The bytes that changed are the state that action touched.

**2. Read constants out of the binary.** For anything the game only ever displays, extract it
from `references/ultima2/ultimaii.exe` directly. Scanning for printable ASCII runs of five or
more bytes recovers the whole text table.

**3. Hold a value against the game's own tick, in-process.** The site tick rewrites
`mon_x`/`mon_y` continuously, so "poke, then press the key" loses: three scripted casts each hit a
creature that had already wandered out of range. A `--poke` through `tools/spice86_type.py` costs a
process spawn (~150 ms) and is still too slow. Import it instead — `sys.path.insert(0, "tools")`
then `import spice86_type as st`, one `st.Mcp()` session, and `st.write_mem` in a loop held ~2 200
writes/s, which is enough that the value is still there when the handler reads it. Confirm it was
there with pattern 4, not by reading afterwards.

**4. When a routine scans memory, stop at its entry and dump there.** `tools/bp_read.py --ip <runtime
offset> --dump OFF:LEN` halts the CPU at the first instruction of the routine and reads DS while it
is stopped, which is the only way to know what the scan actually saw rather than what it left behind.
Arm it in the background, drive the key from another shell, read the log. This is what turned a
statically misread operand into a proven one — see `asm/330C_cast_target_finder.asm`'s TRAP note.

Save the captures inside the current run directory (under `tmp/<timestamp>/`) and name them by
cycle count so a later run can be lined up against them:

```bash
curl -s "http://127.0.0.1:20180/api/memory/$((0x08BE * 16))/range/512" \
  | python3 -c "import json,sys,datetime; d=json.load(sys.stdin); \
open('u2-snap-%d.bin'%datetime.datetime.now().timestamp(),'wb').write(bytes(d['values']))"
```

### Headless `T` works — the gap between the two keys is the whole problem

The standing note in `docs/commerce.md` § 5b and G23 was that a vendor transaction could not be
driven headlessly: `T` registered but the direction fell through and the band printed
`CMD: TRANSACT-PASS`. **It is reproducible.** Stand on a cell that the **foot ladder over the raw
`mapxNN`** admits, exactly **one** cell from a merchant standing on a `0x68` marker, then send `T`
and the direction with a **~50 ms** gap: `tools/spice86_type.py --keys T,Down --delay 0.05`. At
0.12 s, 0.2 s and 0.3 s the direction is drained before `cmd_T` reads it and you get
`TRANSACT-PASS`; at 0.05 s it lands and the band reads `TRANSACT-NORTH` / `TRANSACT-SOUTH`. Same
timing window `tools/spice86_keys.py` documents for the windowed path, never pinned down for the MCP
path until 2026-10-29 (`captures/spice86/spell-slot-map`).

Two dead ends, so nobody retries them. Holding `DS:23/24` and `DS:19/1A` at the values the ray model
wants does nothing, because `cmd_T` **recomputes** the cursor from the player position and the
direction key (its own header at `asm/4050` says so). And the stamped grid at `DS:1800` is a bad
pathability predicate — `0x08` accepts in some cells and refuses in others — so path over `mapxNN`
with `foot_ladder`, not over the stamp.

## Useful offsets, once you have a dump

The player record starts at `DS:0x36`, so a save-file offset `i` corresponds to `DS:0x36 + i`.
The low page holds live state that the save file does not.

| DS offset | Meaning |
| --- | --- |
| `0x00`, `0x01` | player x, y. **Not in the save file.** |
| `0x13` | player tile id |
| `0x19`, `0x1A` | direction delta x, y |
| `0x46`, `0x47`, `0x48` | gender, class, race |
| `0x4B`..`0x50` | strength, agility, stamina, charisma, wisdom, intelligence |
| `0x51`, `0x52` | hit points |
| `0x53`, `0x54` | provisions |
| `0x58`, `0x59` | gold |
| `0x61`, `0x62`, `0x63` | readied weapon, worn armour, readied spell |
| `0x64`, `0x65`, `0x66` | torches, keys, tools |
| `0x76` | weapons array, 10 slots |
| `0x96` | armour array, 7 slots |
| `0xB6` | magic array |
| `0xD6` | items array |
| `0x137` | monster **x**, stride 1, 32 slots — see the correction below |
| `0x157` | monster **y**, stride 1 |
| `0x197` | monster **kind**, stride 1 — drives loot and behaviour; the reward switch branches on `0x60`/`0xFC`/`0xF0`/`0xF8` |
| `0x1B7` | monster **tile**, stride 1 — the byte stamped into the sector map for appearance |
| `0x1900` | town map base |

### Counters are packed decimal, not binary

Every stat and currency field is BCD. A byte of `0x99` means 99, not 153. Two-byte fields
store the **hundreds byte first**: `0x99 0x97` is 9997.

```python
def bcd(v):
    return (v >> 4) * 10 + (v & 0x0F)

def bcd16(lo, hi):
    return bcd(lo) * 100 + bcd(hi)
```

Reading these as binary is the most common way to get a wrong number out of a correct dump.

## Reading the save file

A player file is 384 bytes and is the same record as `DS:0x36` onward, so it decodes with the
same offsets minus `0x36`:

```python
b = open("player", "rb").read()
name = b[0:16].split(b"\0")[0].decode("latin1")
world, map_id = b[0x13], b[0x14]
hp   = bcd16(b[0x1b], b[0x1c])
food = bcd16(b[0x1d], b[0x1e])
gold = bcd16(b[0x22], b[0x23])
```

`map_id` values: 0 overworld, 1 village, 2 town, 3 castle, 4 tower, 5 dungeon.

Position is **not** in the save file. It lives only in the zero page at `DS:0x00` and `DS:0x01`,
so a loaded character is placed by the map loader rather than restored to a saved spot.

## Gotchas

- **Never run in place.** Copy from `references/ultima2/` into a fresh `tmp/<timestamp>/` and run there; the game rewrites `player` in its working directory.
- **System `/tmp` is RAM.** Use the project-local `tmp/` (or `/var/tmp`) for run directories and captures.
- **The API is read-only.** Writes need the MCP endpoint.
- **`DS` is not reported by the API.** Hardcode `0x08BE` or find it before reading.
- **Physical, not logical, addresses.** Multiply the segment by 16 yourself.
- **BCD everywhere.** Binary reads give plausible-looking wrong values.
- **`--AllowIvtAddress0` or it stops** with a message that looks like a crash.
- **Headless ignores SIGTERM and SIGINT.** Measured on this build. No signal produces
  the exit dump, so never kill a headless run to get the CFG — use
  `tools/spice86_stop.sh`, which does `monitor dumpAll` over the GDB port first.
- **`monitor dumpAll` is camelCase.** A lowercase `dumpall` is silently ignored and
  looks like a successful call that produced no files (measured 2026-10-21).
- **Opening the gdb/RSP port halts the VM, and it stays halted.** Measured 2026-10-28:
  after an RSP client connects and closes without a `continue`, `/api/status` reports
  `isCpuRunning: true, isPaused: true` with a frozen cycle count and every key sent
  afterwards is swallowed. A frozen run looks exactly like an idle game. Always send `c`
  before closing (which `bp_read` now does on every exit path).
- **On a halted target the monitor reply lags the command.** `qRcmd,state` answered nothing
  for 20 s; a bare `?` produced the register dump immediately; `continue` produced the
  answer to the `peekRet` sent before it. Send the batch, then read and attribute each
  packet by content, never by which command was in flight.
- **`breakCsIp` is multi-shot and cannot be removed.** One arm produced a second stop after
  a resume with no re-arm (measured). Re-arming the same address stacks duplicates, so a
  capture that must see every pass should arm once and drive the resume itself.
- **No display does not mean no capture.** An agent shell here has no
  `WAYLAND_DISPLAY`/`DISPLAY` and `Xvfb` is not installed, so there is no host window to
  focus — but keystrokes, state reads, memory writes, save seeding and the screen-as-memory
  renderers all work. Keys go in via `spice86_type.py` over MCP, or via
  `deploy/spice86-novnc/up.sh` for a real display. Reach for `--player` and `--seed-map`
  anyway: seeding state is still cheaper than driving input.
- **Poking `DS:00/01` to teleport does not work.** It writes, it reads back, and then
  movement stops entirely; it is a mirror. Change sector by editing the save and
  relaunching: `u2_edit.py set <file> sector NN`, then `setpos <file> X Y` — note
  `setpos` takes **X and Y only**, not a sector.
- **`DS:4A` is two different things.** It is the second decimal digit of the sector
  during a sector read, and the **site kind** (1 village / 2 town / 4 tower) outside
  one. A `cmpb $0x4,[DS:4A]` therefore means "are we at a tower" only in the second
  sense, and reading it in the wrong context gives a confident wrong answer. Sector is
  stored as two decimal digits patched into the filename `MAPXFF`, so sector 20 is the
  pair `2,0` — a numeric read of that pair yields 512.
- **CFG blocks are not proof of execution.** `spice86dumpCfgBlocks.json` lists
  *discovered* blocks. The authority for "this ran" is `ExecutedInstructions` in
  `spice86dumpExecutionFlow.json`; use `tools/cfg_printed.py`.
- **`ExecutedInstructions` answers *whether*, never *how many times*.** Measured across all 147
  harvests that carry a flow dump: 214,116 entries, 864 duplicates — **0.40%**. The list is
  effectively a *set of executed addresses*, not a per-execution trace, and two unrelated harvests
  can share an entry count exactly. So it is a sound authority for "did this address run" and
  useless for loop iteration counts, per-turn entry rates, or anything that needs a frequency.
  Asking it for a count returns `1` and looks like an answer. For counting hits, use
  `tools/bp_read.py --ip <ADDR> --samples N`, which arms a multi-shot `breakCsIp` and resumes
  between samples; note that whatever drives the game has to be running separately, because the
  target runs between samples.

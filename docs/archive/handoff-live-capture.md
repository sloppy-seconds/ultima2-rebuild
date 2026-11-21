# Handoff — 2026-10-21 (read this first after a compaction)

> **ARCHIVED 2026-10-22.** Superseded that same evening by `handoff-2026-10-21-evening.md`,
> and now by `docs/archive/handoff-2026-10-22.md`. Kept as the record of what was known at
> midday, including claims later corrected. Not an entry point: the live queues are
> `docs/phase4-state.md` and `docs/needs-gameplay.md`. Anything asserted here that
> was not re-measured is suspect by default — note the `>>1` vs `>>2` art-index
> question below, which was still open as of item G1.

Supersedes the earlier same-day version, which was already stale by lunchtime.
Everything below is committed and pushed; both CI workflows green; nothing
running; ports free.

## Read in this order

1. `AGENTS.md` — isolation, licensing, script rules, evidence discipline. Hard law.
2. `docs/phase4-state.md` — resume notes, Backlog, gate log. **Note: this has not
   been updated for today's tooling yet.**
3. `docs/passability-findings.md` — F1..F12. F8 is refuted, F10/F11/F12 are the
   current truth. Read the refutations, not just the findings.
4. `docs/save-format.md` — the `player` record, proven fields, teleporting.
5. `docs/map-format.md` — sector map format, authoring probe maps, the traps.
6. `docs/tooling-procedures.md` — driving the emulator, the vendored build and
   its local patch, the screenshot path, the Wayland input finding.
7. `docs/spice86-live-findings.md` — §7–§11 evidence: combat, loot, provenance,
   screen reader, and the **offset tables in §11, which include the item and
   equipment slots**.
8. `docs/archive/handoff-live-capture.md` (this file) — index and running state.

## Running state

* **HEAD `07e32fd` on `main`, synced with `origin/main`, clean tree.**
* **No emulator running, ports 20180/20181/20190 free.**
* **CI green**: `asm-checksum` (byte-exact proof) and `engine-tests` (CP0..CP4).
* **Pushing needs `GH_TOKEN`.** `set -a; . ./.env; set +a; git push`. Without it
  you get a confusing `ksshaskpass died of signal 6`.
* **`tools/spice86/` is now a patched build, not stock upstream.** Upstream
  `OpenRakis/Spice86` @ `6f27fcd` plus one local commit fixing the MCP screenshot
  (raw PNG bytes where base64 was required). Provenance and the patch are recorded
  in `docs/tooling-procedures.md`. The previous vendored copy had been copied out
  of a sibling project, which the isolation rule forbids; this one is built from
  `~/Projects/ai/spice86`.

## What today actually unlocked

Four things that were blocking work for sessions, all now working:

1. **We can see the screen.** `local-mcp_screenshot` works against the patched
   build. Until today every claim about what a tile *is* had been inferred from a
   decode without ever looking. That was the root error behind F8.
2. **Headless + MCP input.** `tools/spice86_start.sh --headless` plus MCP
   `send_keyboard_key`. No window, no desktop noise. **xdotool does not work on
   this host** — it is KDE on Wayland, and XWayland refuses synthetic focus, so
   every layer reports success while keys vanish.
3. **Teleport anywhere, any sector.** Edit the save and reload:
   `tools/u2_edit.py set <save> sector 71` and `setpos <save> X Y`. Sector is two
   decimal digits at `DS:49/4A`, not an integer.
4. **Synthetic probe sectors.** `tools/u2_mkprobe.py` writes a map with one probe
   tile per byte value beside a grass corridor, so a whole table is one walk
   instead of hunting rare bytes through real maps.

## Terrain: what is proven

| byte | art (`>>2`) | terrain | walking | how |
| --- | --- | --- | --- | --- |
| `0x00` | 0 | water, magenta waves | **blocks** | blocked step + owner ID + probe |
| `0x04` | 1 | faint dots | passes | probe |
| `0x08` | 2 | grass, sparse cyan dots | passes | walked on, probe |
| `0x0C` | 3 | forest, cyan double-dash | passes | walked on, probe |
| `0x10` | 4 | mountain, white diamond lattice | **blocks** | walked into at `(11,20)`, probe |
| `0x14`..`0x28` | 5..A | `%`, four-pane, tower, gate, lattice, cross | passes | probe |

**`art = map_byte >> 2`**, settled three ways (see F12). The `view.h` contradiction
is resolved; the `>>1` reading is wrong and should be fixed in the engine.

**The passability rule is still unknown.** Six bytes of behaviour, not a rule. The
ladder at runtime `0x25C0` is confirmed *not* the gate — it accepts `0x10`, which
blocks. The decision is made upstream by whatever fills `DS:2CB`/`DS:2DE`, still
unidentified.

## Claims that were made and are now dead

Keep these listed so nobody resurrects them.

* **F8: mountain is `0x08`.** Refuted. `0x08` is grass. F8 had the right *art*
  index (`0x04`) and the wrong *byte*, which is exactly what a `>>1`/`>>2` slip
  produces.
* **`0x1A` is a rare terrain byte.** No. It is the first byte of the 128-byte tail
  on every sector map, and in the save file it is the DOS EOF marker. Not data.
* **"Items are not located."** Said in an early draft of `save-format.md` today.
  Wrong — they were proven in `spice86-live-findings.md` §11 and I had not looked.
  See §5 of `save-format.md` for the correction.
* **`DS:29` is the destination tile.** No, shared scratch with four writers.
* **`DS:13` is terrain.** No, the player stamp (`0x78` on foot).
* **Stats are plain bytes.** No, **BCD**. `DS:4B = 0x50` is strength 50, not 80.
  **HP is BCD too** — a raw `255` renders as `??`.

## Standing facts from earlier today, still valid, do not re-derive

* Loot: every kill gives gold `(rand&0x17)|1` and exp `(rand&3)+1`, minimum 1
  each; the **item drop is gated on monster kind `0xFC`**.
* `A THIEF STOLE SOMETHING!` prints from the same block that decrements
  `DS:D6+i` — theft proven, not inferred.
* `--PARALIZED!` lives at runtime `0x2527`, **inside the passability routine**, so
  paralysis is a movement rejection — which is why the owner saw `--INVALID
  MOVE!` while paralyzed.
* Monster arrays are **stride 1**: `x=0x137+i`, `y=0x157+i`, `kind=0x197+i`,
  `tile=0x1B7+i`.
* RNG seed = BIOS tick low byte at `0x40:6C`, read once at startup into
  `DS:23F..242` — hence deterministic monsters in a fresh emulator.
* `Z` pauses on the status screen; writes land instantly but the screen is frozen
  and no turns occur while paused.
* Auto-PASS is a **poll count, not a clock** — no `INT 1Ah` exists in the corpus
  and the only `0x40:6C` read is the startup RNG seed. Threshold 256 is a
  documented placeholder, not proven.

## Traps that cost time today

* **Memory reads pause the CPU.** `WhilePaused` pauses for every HTTP read and
  resumes after, but keys sent into that window are silently lost. Settle ~0.5-1 s
  after a read before injecting a key, and verify every step.
* **Writing `DS:00/01` live does not teleport** — it breaks movement entirely.
  Edit the save instead.
* **The world wraps.** Viewport is toroidal; a scripted walk that assumes a flat
  grid desynchronises near an edge. Start probe walks mid-map.
* **`range` reads are read-only.** Writes need `PUT /api/memory/<addr>/byte` with
  body `{"value": N}`, one byte at a time.
* **`pgrep -f Spice86` matches its own shell.** Check ports.
* **The MCP endpoint is `/mcp`** and the wire tool name is `screenshot`, not
  `local-mcp_screenshot`.
* **Screenshots land in `/tmp/spice86-mcp-screenshots/`**, which is tmpfs here, so
  they cost RAM. Copy what you want into `tmp/screens/`.

## Tools

| tool | status |
| --- | --- |
| `tools/u2_edit.py` | **new** — save editor: stats, hp/food/gold/exp (BCD), sector, position, weapon, armour, items; `maxout` builds a fully kitted character |
| `tools/u2_mkprobe.py` | **new** — synthetic probe sector generator |
| `tools/tile_atlas.py` | works — 64 tiles from the exe, hex-named `00..3F` |
| `tools/patch_arrows.py` | works — two-byte gate bypass so arrow keys register |
| `tools/spice86_keys.py` | **unusable on this host** — xdotool path, blocked by Wayland |
| `tools/cfg_printed.py` | fixed and validated — gated on `ExecutedInstructions` |
| `tools/vga_screen.py` | works — text-mode screen from a memory dump |
| `tools/map_render.py` | works — 64×64 sector with player and monsters |
| `tools/redecode.py` | fixed — string heuristic no longer eats code bytes |
| `tools/poll_probe.py` | works — memory API sustains ~250 req/s |
| `tools/vga_ring.py` | **UNTESTED** — never run against a live emulator |

## Open work, in priority order

1. **Verify the kitted character in-game.** The editor writes weapon/armour/items
   and they round-trip through the file, but the in-game inventory display was
   never opened — `I` is not the command and the right key is unrecorded. Find it,
   confirm POWER armour and QUICK SWORD show, and confirm combat actually uses
   them.
2. **Fix `engine/src/core/view.h`** to `art = map_byte >> 2` and remove the
   contradictory comment.
3. **Find the passability rule.** The `DS:2CB`/`DS:2DE` writer is the only
   surviving candidate. Breakpoint capture at runtime `0x2423` is the route.
4. **Locate towns.** Which sector holds which town is unrecorded. `DS:4A`'s site
   kind (1 village, 2 town, 4 tower) plus map bytes give a search. Then teleport
   next to one, walk in, and watch the state transitions — that is the expedition
   that makes the remaining tables fall out.
5. **Settle WIS vs CHA** (`DS:4E`/`DS:4F`) with a character whose values differ.
   `state.h` and the findings table disagree.
6. **Blocked-step food cost.** One sample suggests a rejected move still drains
   food. Needs a clean before/after before it becomes a finding.
7. **`docs/phase4-state.md` is stale** — none of today's tooling is in it.
8. **9 untracked `disasm/redecoded/range_*.txt`** still need a commit-or-ignore
   verdict.
9. **T9 blog/retro** — still unblocked and still good material.

## What to write about (T9 raw material)

* Hours of confident reasoning about what tiles *are*, when a screenshot tool had
  been available the whole time. The fix was to look.
* A hypothesis (F8) that survived two corrections and died only when measured —
  and it died because it had the right art index and the wrong byte.
* A vendored build whose provenance was unrecoverable because it had been copied
  out of a sibling project. Now built from source at a named commit, with a local
  patch recorded.
* An upstream bug found by reading the error carefully: PNG bytes run through a
  UTF-8 decode, so the image is destroyed rather than mis-encoded, and no
  client-side trick recovers it.
* Saying "items are not located" about a format that was already documented in
  this repo. The corpus-listing rule exists for exactly this and I broke it.

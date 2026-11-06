# Phase 4 State — checkpoint table

Plan: `docs/phase4-plan.md`. Log: `docs/phase4-implementation.md`.
Kickoff: `docs/phase4-kickoff.md`.
Statuses: todo | in-progress | done | blocked | awaiting-gate

| CP | Title | Status | Notes | Date |
| --- | --- | --- | --- | --- |
| CP0 | Skeleton + automation substrate 🔒 | done (owner approved 2026-10-27) | `engine/` builds with cc only; term + null renderers, term + script input, beep + null audio, posix + script clocks; seam layer with asm-cited `--trace`; `--dump-state` screen grid + JSON; `make test` PASS incl. reproducibility; selfmod question resolved from 45 existing runtime captures (`disasm/selfmod-capture.md`) — cluster is not patched, CP3 unblocked | 2026-10-27 |
| CP1 | Extractor + title screen 🔒 | **awaiting-gate (owner eyeball vs DOSBox)** | `tools/extract/extractor.py` (exe source + asm db-line fallback + `--check`/`--emit text`); `game/title.c` = intro, title, menu, 'D' poem, CP3/CP4 stubs; `make test` = CP0 9/9 + CP1 9/9; **architecture correction: the grid is 40x25, not 80x25** (evidence in `engine/README.md`); `--selftest` keeps the CP0 substrate probe testable | 2026-10-27 |
| CP2 | Play contract + evidence 🔒 | done (owner route change 2026-10-27) | MCP wrapper replaced by the CLI contract per owner decision; `docs/playing.md` is the agent interface; in-tree SHA-256 + `--replay-report` + `tools/playlog.py verify`; committed transcript `evidence/cp2-playthrough.json` (14 keys, 23 frames) reproduces; CP2 test 12/12 | 2026-10-27 |
| CP2.5 | Re-decode tooling (scoped, owner-approved) | done 2026-10-27 | `tools/redecode.py` + `disasm/redecoded/` for the five blob functions; save path and creation flow decoded; `asm/` untouched so the byte-exact invariant is unaffected | 2026-10-27 |
| CP3 | Character creation + status line | **done, awaiting owner gate** | (a) save/disk model + byte-compatible player record. (b) creation flow + status block. (c) form rework: field-addressable with D1-D4 (Tab/arrows, editing backspace, refuse-in-place, visible cursor); rules and state unchanged. CP3 test 8/8 + 23-assertion `save_check`. **CP2 evidence baseline moved three times deliberately** — see gate log | 2026-10-27 |
| CP11 | Mod surface + original/enhanced modes (owner-scheduled, deferred from CP3) | **tabled by the owner 2026-10-28e** — not lost | two modes, sparse-diff overrides, content + rules surfaces, mandatory provenance; save-record layout non-overridable; no `.pat` reimplementation. The open input was "what do existing U2 mods actually patch"; the owner's answer was to table it until it becomes blocking, which makes it a decision rather than an oversight | 2026-10-28e |
| CP4 | Overworld + movement | **in-progress (CP4b overnight: T1,T3,T4,T6,T7,T8 done/passed; T2 half; T5 held)** | kickoff § 2 items 1 and 2 **settled from the bytes**: the name is `MAPX` + two digits self-patched into the code stream (`asm/2290`, file `0x24b7`), and a tile is **one byte** in a **64x64** sector read with `AH=27h, CX=0x1000` into `[DS:0x472]` (`asm/50B3_map_tile_ptr.asm`). New finding: `asm/2305` **writes the sector back**, so world content is mutable. EGA question answered: `original/updated/` is the Exodus Project upgrade and EGA is a **separate binary** (`U2EGA.EXE`), so the accommodation is a tile-ID seam plus data-driven naming, not a mode flag. Direction keys are **arrows**, proven via a scancode table at `CS:0x4F09`. Movement is proven: arrows via a scancode table at `CS:0x4F09`, direction codes as data at `DS:26E..271`, a rejected step is undone before `--INVALID MOVE!`, and crossing x/y >= 64 reloads the sector (`asm/242D`/`2434`/`243E`) rather than straddling. **Viewport now measured from owner screenshots**: tile = 16x16 px = 2x2 text cells, window 20x10 tiles at rows 0-19 full width, player at window tile (9,5), row 24 blank. Dungeons are first-person 3D, so the tile renderer does not cover them (CP7). **CP4b overnight (2026-10-21):** the passability premise was disproven — `asm/2423` never reads a tile, so on-foot walking is never rejected on the overworld (T1 PASSED, write-up in implementation log § CP4b); tile IDs decoded (0=water,4=land,8=plane,0x78=player); sector crossing is the `DS:6E` arrival system, not a foot step. T3/T4/T6 fixed one root-cause class (loops testing `exit_code` but not `input_exhausted`): window-close, SIGTERM, and dummy-driver `--dump-screen` all now stop cleanly. T2 shipped the proven half (PASS spends 10 food, underflow zeroes DS:51..59 = instant death; no gradual HP drain) **but its "no per-step tick — the walk loop calls no drain" is RETRACTED 2026-10-25**: all four walk arms `call 0x2423`, and `asm/2599_food_drain_walk.asm` / `asm/2626_food_drain_mounted.asm` subtract `0x19` inside it ahead of the verdict, so every step attempt costs 19 and a refusal costs it too (`captures/spice86/g2-step-food/`, `docs/needs-gameplay.md` G2/G34). The engine was charging 13, not 19, because the port passed decimal 19 where the asm's immediate `0x19` was meant. T7 screenshot differential harness landed (status-block only, poison-checked). T5 held for owner: it moves the CP2 baseline and its pre-approval flag is unresolved. Full detail: `docs/archive/phase4-overnight-cp4b.md § Morning report` | 2026-10-21 |
| CP5 | Combat | **in progress — scope ruled 2026-10-23** | Tickets C1..C7 in § CP5 below. **Owner scope ruling: the larger scope** — not just the `combat_check` spec tickets but the wiring that makes combat *reachable* in the engine (encounter, turn loop, continuous world). Status per ticket: **C1, C2, C4, C5, C6 and C7 implemented or resolved**, and **C3 implemented 2026-10-28** — the paralysis-as-movement-rejection coupling to CP4 passability. It was wiring, not mechanics, and it needed no state seam: the counter is reached the way it is reached in play, by a Daemon (type `0x34`, dispatched by `asm/0AD3`) casting it during a scripted fight, so the gate seeds the creature rather than the status byte. The CI counts are printed by `run_cp5.sh` on every run (do not copy them here; a hand-kept number goes stale, and this row's did twice). C5 is no longer "blocked on a capture" — the field turn is on record (fight turn 2909); what remains is *placement*, which belongs to passability G4, not combat. **Owner recollection on the status effects, recorded as `recalled` and treated as a pointer, not a conclusion:** legs paralysed -> can still fight but not move; arms paralysed -> the reverse; paralysis lasts "some amount of time or moves"; and **"there's not really a leaves-combat state"** — see the design note below. Two owner decisions were open there; **both are now closed (2026-10-28e)**: the acceptance bar is the plan's own *"owner wins a fight"*, kept as an eyeball gate, and C5 placement is confirmed out of CP5 scope — it was solved on 2026-10-28 and it is tile `0x5C`. | 2026-10-23 |
| CP6 | Towns + full command set | **in progress — command dispatch landed 2026-10-25** | The dispatch is built in the shape the bytes have it: `engine/src/game/commands.c` owns a 26-slot table indexed like `cs:0730`, the A-Z range test sits before it (asm/09FB then asm/0A18) so "not a command letter" stays a distinct outcome from "a handler that declined", and `U2_CMD_UNKNOWN` prints nothing because the illegal-command site is a different routine and is not wired. `u2_overworld_loop` no longer handles any command letter inline — the six that were there (A, E, P, Q, U, Z) migrated one at a time under the tests, joining H and J, and A/U needed the pending-aim state to move onto `u2_overworld` first because the original consumes the direction key inside the handler chain. Do not copy a count of what is implemented into this cell: `python3 tools/cmd_marker_gate.py` prints it, and a hand-kept number here would be the second copy AGENTS.md forbids. What CP6 still needs is not the dispatch but the subsystems the empty slots name — spell state (C, M), town NPCs and the merchant tables (O, T — **T's shop half landed 2026-10-29 in `engine/src/game/commerce.c`; what is left there is the talking half, which needs a `tlkx` loader**), the steal effect wired to a world direction (S), an inventory view (V), armour ownership and the strength gate (W), vehicles (B, L) — plus the four letters with no execution evidence at all, which are parked by owner decision. | 2026-10-29 |
| CP7 | Time travel + space + dungeons | **partly informed 2026-10-21** | Dungeons are sectors, not a separate format: entering a cave moved `DS:49/4A` from `3,0` to `3,5` and loaded `mapx35`, with no video mode change (stays CGA mode 4). Rendered as a first-person wireframe. Light is a **step counter** at `DS:26`, zeroed on entry, ~150 on `I`, decremented per move, and `mapx25`/`35`/`45` are byte-identical. Command dispatch is mode-aware: `A` is ATTACK outside and ADVANCE inside. Full detail: `docs/spice86-live-findings.md` § 13. **Space layer shipped 2026-11-04:** `engine/src/game/space.c` (world tables, orbit lookup, hyperwarp gate, dive sector, descent step, tile verdict, sun check — pure, RNG is an argument) and the nested in-space command loop in `commands.c:launch_rocket` (launch → readout/CMD:/arrows/H/L → dive → descent), closing G62's "faithful half." `make test-cp` gates it via `space_check` (37 checks, 4 mutants). | 2026-11-04 |
| CP8 | Endgame + full playthrough 🔒 | todo | | |
| CP9 | SDL renderer (planned as "SDL2 CGA renderer") | **largely delivered 2026-11-03, not signed off.** Shipped: SDL3 bitmap front end with the open-licensed 8x8 font, the four mode-4 palette indices, a resizable window with integer scaling, the terminal backend retained, `speaker_freq` wired to SDL audio, and the intro picture files. **Still open: the `0x8d` / `][` glyph mapping**, which the plan said would be decided here and never was. The milestone's own spec (640x200, 16 colours) was corrected in `docs/phase4-plan.md` | owner approved installing system packages; SDL3 chosen 2026-11-03 | 2026-11-03 |
| CP10 | Sign-off 🔒 | todo | | |

## Resume notes (read this first after a compaction)

- **2026-10-21 correction to the T2 closure — RESOLVED 2026-10-28d, no capture needed.** T2
  recorded "no `INT 1Ah` exists in the corpus". That was true of the decoded `asm/` tree and
  **false of the binary**: `CD 1A` occurs at file `0x8AE`, `0x8C0`, `0x8CE` and `0x8FD0`
  (runtime `0x6AE`, `0x6C0`, `0x6CE`, `0x8DD0`). The re-proof asked for was reachability, and
  the answer came out of the existing harvests rather than a new one: the first three execute
  in **136 of 136** `ExecutedInstructions` sets, the fourth in **0 of 136**. The three that run
  are one routine, now decoded and named `asm/06AC_clock_tick_wait` — a 20-tick window that
  counts busy passes into `DS:74` for what reads as machine-speed calibration (inferred).
  **T2's conclusion survives and is re-based**: the auto-PASS timeout is a poll count because
  the PASS loop's own bytes at runtime `0x8D6` say `incb DS:21 ; jne`, not because no clock is
  read. Full write-up where the claim was made: `docs/spice86-live-findings.md § Addendum`,
  corrected in the same commit. Same lesson as F6, restated because it has now bitten three
  times: a negative result from a probe over a partially decoded tree is not a negative result
  about the binary.

- **2026-10-21 passability re-decode: read `docs/passability-findings.md`.** The
  full routine (file `0x2623..0x287F`) was decoded contiguously and it contains
  **no read of the target tile** — its single `map_tile_ptr` call writes `0x50`.
  A second, previously unread ladder at runtime `0x25C0..0x25F6` decides on
  `DS:29 & 0x7F` alone. The mode chain is `DS:2CB -> DS:15` and `DS:2DE -> DS:18`
  (runtime `0x5B77/0x5B83`), and `DS:2CB`/`DS:2DE` have **no direct writers
  anywhere in the exe**, so the tile dependence enters through an indirect table
  write the static route cannot attribute. `DS:13 = 0x78` in every in-world
  sample, so it is the player stamp, not terrain — which retires the "travel
  mode" reading of `DS:13` in `state.h`. `create-test-live` is a clean controlled
  experiment proving the decision is tile-dependent. Also: `tools/redecode.py`
  mis-decodes 21 bytes of real code at file `0x27D2` as a printable string, so
  any `STR` line inside a code span in `disasm/redecoded/` must be re-checked
  before being cited. Next step is one breakpoint capture at runtime `0x2423`.

- **Entry point is `docs/handoff-2026-10-28d.md`.** The 2026-10-23 handoff named below is
  superseded; it is kept as the record of what was closed that day (G1, G8,
  G18, G20, the dungeon encoding, the price operand) and the triage of the open list of the
  time. The CP4b
  T1 finding and part of T2 were **retracted** on 2026-10-21 — see
  `docs/phase4-implementation.md § CP4b → RETRACTION` and
  `docs/spice86-live-findings.md`.

- **CP4 is mid-flight and everything is committed. To take over, read
  `docs/handoff-2026-10-23.md` for current state, then
  `docs/phase4-kickoff-cp4b.md` for the CP4 work order itself.** The headline it must not
  miss:
  the original has **two display paths** — tiles for the map and banners, and
  INT 10h teletype text (`str_out_at` runtime `0x51EB` -> `str_out_core`
  runtime `0x51FD`) for the status band, menu and creation form. There are no
  digit glyphs in the 64-record tile table (verified: zero `04 10` headers in the
  1312 bytes after the last record), so the status band must be drawn through the
  text path, not the tile path. That also explains the `CS:0x48DC` table
  alternating `B800+5k`/`BA00+5k` — character plane and attribute plane of text
  mode 0. And **the staging byte is `2 x art`**: `u2_view_at()` returns the
  staging byte, `u2_view_art()` halves it; skipping the halving doubles every
  tile, which is the bug the owner caught from a screenshot after every
  structural check had passed.
Done and green: kickoff § 2 items 1-2 (name = `MAPX` + two digits
  self-patched at file `0x24b7`; tile = **1 byte**, `y*64+x`, 64x64 sector, read
  `AH=27h CX=0x1000` into `[DS:0x472]`); the sector **write-back** (`asm/2305`) with
  the owner's decision (a) faithful in `original` mode, (b) copy-on-write for
  `enhanced`; movement and the **arrow keys** (scancode table at `CS:0x4F09`,
  direction codes as data at `DS:26E..271`, space = PASS, rejected step undone then
  `--INVALID MOVE!`); the **viewport measured** from owner screenshots (tile = 16x16
  px = 2x2 cells, window 20x10 tiles at rows 0-19 full width, player at window tile
  (9,5), row 24 blank); the **game-data inventory** as a reusable tool
  (`tools/content_inventory.py` + committed `disasm/content-inventory.tsv`); the
  **tile art found in the exe** (index at file `0x7BC0` = DS:0x5B0, 64 words, deltas
  exactly 66, `04 10` header, pixels from `0x7C42`, 16x16 2bpp —
  `tools/extract_tiles.py` proves and renders it); and **CP4(a) engine code**:
  `core/world.[ch]` + `--data-dir` + `tests/world_check.c` (42 checks) +
  `tests/run_cp4.sh`, all green under `make test`.
- **Next task, in order.** (1) Read `asm/4A74`, `asm/4D35`, `asm/4D5D` and the callers
  of `asm/4AC3` — that is the draw loop, and it also answers the map byte -> tile
  entry mapping and what the `04 10` header means (suspect: the mode-4 attribute
  byte). (2) Build the tile cache: self-bootstrapping, extracted from the user's exe
  on first run, gitignored, stamped with the source SHA-256 so a stale cache cannot
  silently survive a swapped game copy. (3) Viewport renderer behind a **tile-ID
  seam** (renderer chooses the glyph/sprite; game logic never sees a glyph), then
  movement wiring and encounter rolls. (4) Hand to the owner to walk.
- **Standing directives from the owner, recorded in plan § CP4.** Validate the engine
  against the screenshots **as a test** as it is built (SKIP with a named reason when
  the images are absent, so CI stays honest). Use **REA for engine validation, not for
  re-deriving the asm** — byte-identity is already proven better by `check_func.py`, and
  REA's provider here is Ghidra, which found fewer routine starts than the vendored
  Reko (7 vs 11 of 46). `references/u2upgrade/` is a **secondary source only** and is
  explicitly not trusted; it is an upgrade, not the original.
- **Two self-corrections to remember.** The `and $0x3f` "torus" claim was wrong (that
  mask is the rocket/plane launch auto-move; the overworld reloads the sector at
  `>= 0x40`). And "no bitmap table in the exe" was wrong twice over — my own scan
  printed `file 0x7c40` and I dismissed it as zero padding when it is two bytes from
  the tile table. A negative result from a probe not designed to detect the thing is
  not a negative result.
- **Everything is committed.** Working tree clean at the commit that folded D6-D13
  into the plan. No in-flight edits.
- **CP3 form state**: stats prefill at 10 and are a two-digit shift register (D6,
  D10), the budget may go negative and saving requires exactly 00 (D7), name is
  capped at 12 with the high bit masked (asm/5BB7, asm/5207, D8), SAVE/RESET are
  navigated to rather than hotkeys (D9), ESC abandons creation (D11) and the demo
  (D12), and `P` prints the record it read back (D13). **All thirteen deviations
  are registered in plan § 4a**, and the three evidence-backed corrections from
  this stretch are in plan § 7.
- **All suites green**: CP0 0.04s, CP1 0.12s, CP2 0.32s, CP3 4.41s (~4.9s total,
  measured 2026-10-27). CP3's cost is two `sleep 1` waits in the pty tests, which
  pace the intro-skip apart from the body because a key burst is swallowed by the
  intro's key-holds. Ctrl-Q **is** passed and **does** terminate the runs.
- **Phase 3 invariants green**: `make test` runs them (clean rebuild byte-match
  37344 B, `original/` md5, `asm/` untouched).
- **Open for the owner at the CP3 gate**:
  1. ~~Binary vs packed decimal for HP/FOOD/GOLD~~ **resolved**: packed decimal,
     4 digits, and the original prints the two bytes in the wrong order so food 3
     displays `0300`. See plan § 7 and `engine/src/game/status.h`.
  2. **CP3 polish deferred by the owner** ("good enough to move on, we can polish
     later"): nothing is recorded as broken, so if a later checkpoint exposes a CP3
     defect it goes here rather than being silently fixed in passing.
  3. Whether the CP2 evidence baseline should keep being regenerated in place or
     frozen with new files per checkpoint. It has moved six times, each
     deliberately and each recorded in the gate log.
  3. Play the form: `make play`, press `C`. Confirm Tab/arrows, re-editing, and
     that the field positions look right against the original.
- **Not yet done**: the CP11 cheap accommodations (constant registry, accessor
  reads, random-creation seam), and the branch audit as a standing gate rule is
  written in `docs/phase4-acceptance.md` but not yet applied retroactively to
  CP1/CP2.

## Next checkpoint

**This section was stale for a week and pointed at CP4 although CP5 shipped.** Rewritten
2026-10-28e. It deliberately carries no counts: every number it could name is printed by a
tool, and a hand-kept number here has gone stale before.

**Fresh agent: read `AGENTS.md`, then `docs/handoff-2026-10-28e.md`.** That handoff is the
entry point; the kickoff docs below are historical work orders, not the queue.

**CP5 — combat.** Implemented or resolved ticket by ticket (see § CP5 above and
`docs/phase4-kickoff-cp5.md` for the build order as executed). What remains is an
**owner gate**, not code. The acceptance bar was in fact set in `docs/phase4-plan.md` § CP5 —
*"owner wins a fight"* — and it stands (owner, 2026-10-28e: asked whether an eyeball gate is
needed at all, and the answer is yes, it costs two minutes and it is the only check that can catch
mechanics-correct-but-nothing-drawn). Combat entry is now observed rather than designed, so the
gate is concrete: stand beside a creature, press `A` and a direction, and read one `CMD:` line
(`ATTACK--EAST--HIT!!!` then `KILLED--GOLD+1--EXP.+02`). The second open item there — whether C5
force-field *placement* is out of CP5 scope — is resolved and no longer needs the owner:
placement was solved 2026-10-28 and it is tile `0x5C`, measured in `captures/spice86/g29-field`.

**CP6 — towns + the full command set is the next checkpoint, and its owner decision is now made.**
The engine has no command loop, and the command loop is upstream of era display, speed wiring,
towns and time travel. **Owner ruling 2026-10-28e: implement all 26 handlers**, with any letter no
capture has ever seen run carrying an in-source `UNPROVEN:` marker, and **no checkpoint or phase may
be called code-complete while any marker stands**. The rule lives in `AGENTS.md` § 3, not only here,
because a handoff gets superseded and a working rule must not.

`python3 tools/cmd_handlers.py` is the authority for which letters are proven and which are not; it
prints the crosswalk, its `exec` column is the evidence, and the gate fails when the jump table, the
filenames and the provenance lines disagree. **This section deliberately names no letters.** An
earlier version hardcoded "twelve unproven / fourteen proven" and went stale twice without anyone
noticing, because the corpus grows every time a capture is harvested and prose does not. Which
handlers are still `NULL`, and the specific thing each one lacks, is in the table comment in
`engine/src/game/commands.c` — also not duplicated here on purpose.

**Owner-independent work that does not need the above.** In rough value order, each with the
tool that measures it rather than a number copied from a diary:

1. What each speed delay slot gates — half the slots still have no caller found
   (`asm/0711_speed_delay_lookup`).
2. Two data tables recorded with shape but no meaning: `asm/5287` and `asm/0720`. Naming them
   needs a read of `asm/4C1E`'s consumers or a capture.
3. The remaining fragment splits, which are parked deliberately
   (`disasm/fragment-splits.tsv`, `disasm/phase2-summary.md`).

**Closed 2026-10-28e, so nobody re-does it:** the forced-keep correction queue (**36 → 0**,
and three of the last five items turned out to be the checker's own desync or a wrong
disposition rather than work), the rendering-gap list (now empty — every printable `str_out`
site is rendered as data), the body guard's blindness to renames, wiring the mutation
harnesses into CI, and repointing this section.

## Gate log

- **2026-11-06 (owner-approved): CP2 baseline moved 42 → 70 steps, 39 → 66 keys.**
  `feat/fmtowns-char-creation` replaced the typed creation form with stepping and dropdowns
  (T1/T2/T3, `docs/char-creation-ui-plan.md`), so the committed evidence's stored keystrokes no
  longer describe the form they were recorded against. Re-recorded with
  `tests/play_to/reach_everything_today.keys`, which was rewritten for the new model in the same
  branch, and verified to reproduce: `steps recorded=70 replayed=70 / REPLAY: REPRODUCES`.

  **The first attempt at this move produced a transcript that did not reproduce against itself, and
  the cause was the save directory, not behaviour.** `--save-dir /var/tmp/x/disk` where `/var/tmp/x`
  does not exist is not a fresh disk: `u2_slot_ensure` makes two plain `mkdir`s (`core/slot.h`), the
  first of which fails on a missing parent, so the save path answers `U2_SAVE_NOT_READY` and the `P`
  branch prints `INSERT PLAYER DISK` instead of loading a blank seeded record. `tools/playlog.py
  verify` always creates its temp directory, so it replays the *other* game. Measured: parent absent
  gave 112 steps and diverged at step 15 against a 70-step replay of the same 66 tokens; parent
  present gave 70 steps and reproduced 70/70. **The recipe's `<fresh>` has to be `mkdir -p`'d
  first.**

  This is the second baseline move in this log whose cause was the save directory rather than a
  behaviour change — the 2026-10-21 entry records the same class from a stale default `saves/` — and
  the two failed in opposite directions: one recorded against a dirty disk, this one against a disk
  that did not exist. Neither is visible in the transcript; the reproduce check is what catches them.

- **2026-10-21 (owner-approved): T5 shipped, CP2 baseline moved 40 → 42 steps.**
  A fidelity axis was added — `ctx.fidelity` with `--enhanced`, independent of
  `u2_mode`, which is a renderer axis. The creation tail is now faithful in
  `original` mode: `asm/5400`'s tail prints `TO PLAY ULTIMA ][`, then
  `INSERT PROGRAM MASTER`, spins on `key_poll`, and calls the program startup
  routine at runtime `0x0100` — a **full restart**, which is why the owner saw
  the splash reappear and had to press `P`. The engine previously shortcut this,
  so `original` mode was wrong in a way nobody had noticed until the owner
  described the splash. `enhanced` mode skips the CP/M disk-swap dead end and
  enters the world at the created entry point. Both are asserted in CP4, plus a
  third check that an **abandoned** creation (ESC) does not run the tail —
  `u2_creation_flow` returns 0 for abandoned and 1 for completed, and treating
  any non-negative return as success made ESC-rejected creation restart the
  program. Baseline moved because the tail legitimately paints two more frames;
  re-recorded with the documented fresh-`--save-dir` command and verified to
  reproduce. Two strings added through the extractor (`U2_S_TO_PLAY`,
  `U2_S_INSERT_PROGRAM`); both are byte lists in the listing rather than quoted
  `db`, and the disassembler had mis-split `TO PLAY ULTIMA ][` into data
  fragments, so offsets came from the binary. CP1's fallback expectation moves
  64/27 → 66/29. `--auto-pass-polls` also landed, wiring the earlier
  `auto_pass_polls` knob so enhanced mode can tune or disable the idle PASS.

- **2026-10-21 (owner-approved): CP2 baseline regenerated, and CI split.**
  `engine/evidence/cp2-playthrough.json` was re-recorded. The recorded step count
  is unchanged at **40** — the apparent 40→41 drift was not a behaviour change at
  all but a **stale `--save-dir`**: the baseline had been recorded against the
  default `saves/` directory holding state from earlier runs, while
  `tools/playlog.py verify` always replays into a fresh temp save dir. Recording
  under a fresh save dir reproduces exactly. Root cause of the CI red was this
  same coupling, not the overworld changes, which turned out not to move the
  trajectory. Regenerate command, now the documented one:
  `./u2 --headless --save-dir <fresh> --script tests/play_to/reach_everything_today.keys --seed 0 --replay-report evidence/cp2-playthrough.json`.
  Verified to reproduce under both the exe header and the asm-fallback header.
  Separately, `build-and-checksum` was split into **`asm-checksum`** (the
  byte-exact proof) and **`engine-tests`** (Phase 4 WIP) with independent badges,
  because a failing engine test was making the reconstruction's badge read as
  though the proof had regressed — which is exactly the wrong signal to send.

- **2026-10-27 (owner): fidelity policy.** The engine reproduces the original's
  rules and state exactly, but may improve **interaction** where the original was
  frustrating — "spirit of the game", and these improvements live in `original`
  mode, not behind a mode flag. Hard line: UX ergonomics yes, game rules no, and
  the save-record layout never. Deviation register D1-D5 (field navigation,
  editing backspace, refuse-in-place instead of full restart, visible cursor,
  Ctrl-C quit) is in `docs/phase4-plan.md` § 4a. This also scopes what "faithful"
  may be claimed to mean: rules and state match the asm; interaction need not.
- **2026-10-27 — CP2 evidence baseline moved deliberately (owner should note).
  The approved artefact was regenerated, not silently patched.** `C` at the menu
  no longer runs the CP2 stub, so the recorded run could not reproduce; the
  tamper check is what surfaced it, which is the whole point of committing it.
  `tests/play_to/reach_everything_today.keys` now completes a real character, so
  the evidence covers the creation screens too. Digest
  `76a0b3de7556c2b1…` -> `3e5d0492c946fb5b…`, 23 steps/14 keys -> 21 steps/39
  keys. If a baseline must stay frozen instead, say so and it is one revert.
- **2026-10-27 (owner): mod surface scheduled as CP11, deferred out of CP3.**
  Two game modes (`original` hardcoded, `enhanced` customisable), sparse-diff
  overrides, content + rules surfaces, mandatory provenance, mod packs as
  directories with manifests, `key=value` not JSON. Hard constraints: the
  save-record layout is never overridable, no `.pat` reimplementation, no
  committed content. Full design in `docs/phase4-plan.md` § CP11. Also recorded
  there: the sibling `../ultima2` checkout stays off limits as a source of
  conclusions under `AGENTS.md` §0, so the "what do people actually patch"
  question comes from the owner or public research, not from that repo.
- **2026-10-27 (owner): save/disk model approved.** Engine maps the original's
  "insert a disk" onto `--save-dir` (default `./saves`) with a byte-compatible
  `PLAYER` file; missing file is seeded from the pristine blank template;
  unusable path shows the INSERT prompt rather than inventing a location;
  `(INSERT GALACTIC DISK)` is a documented deviation because content is loaded
  at startup. Quicksaves/slots deferred as a filename layer over the same
  record format. Full design in `docs/phase4-plan.md` § CP3 design.
- **2026-10-27 (owner): save-format decision, binding for CP3 onward.** Engine
  save games must be **byte-compatible with the original player record**,
  so `rng_state` (and anything else that is not in the original
  record) must **not** go into the save file. Character creation proceeds on
  that basis. If extra state ever needs persisting, the fallback is a separate
  sidecar meta file next to the save, and only once there is a strong reason —
  not by default. Correction after this decision was recorded: the record the
  game writes is **256 bytes at `DS:36..135`**, not 384 — the *file* is 384
  bytes (proven at CP2.5, three independent write/read sites). The decision is
  unaffected; only the size was wrong. Breadcrumbs to revisit are left in
  `engine/src/core/save.h` (`DEFERRED` block) and in
  `docs/phase4-state.md` § Deferred decisions.
- **2026-10-27 (owner):** CP2 route change **approved in conversation**: the CLI
  is the contract, an MCP wrapper can be added later on top of it. Also recorded:
  **no subagents for this project** — a second local model contends for the GPU;
  work stays in-process. Plan §2/CP2/acceptance-#5 amended and §7 added with
  every evidence-backed correction so far.
- **2026-10-27 — correction logged (no gate needed):** `rand_byte` is a
  six-byte feedback shift register at `DS:0x23F..0x244`, seeded once at program
  launch from the BIOS keyboard flags at `0x40:6C` — not a tick-counter sampler
  as plan §1 and the CP0 log said. Phase 3 had split the routine across
  `asm/5217`/`asm/522B`/`asm/5236`. Engine rewritten to match; `--seed` and
  `--rng-sequence` added; vectors asserted. Consequence for the plan: replay
  determinism comes from the PRNG itself, so the CP2 play-API needs no clock
  trickery, and monster spawns/encounters are reproducible by construction. The
  seed is per launch, not per new game, and is not part of the saved player
  record. Details in `docs/phase4-implementation.md`.
- **2026-10-27 — CP1 owner review round 1.** Owner feedback applied: the echoed
  choice now lands on the `CHOICE:` line (title draw order was wrong —
  `asm/0394` prints at the cursor the last title line leaves, and `CHOICE:` is
  drawn last at row 19 col 5); choice dispatch order corrected against a clean
  decode of runtime `0x389..0x3B5` (`D`→poem, `P`→`asm/0764`→poem,
  `C`→`asm/5400`→`asm/0764`→poem); the poem clears the screen first
  (`asm/03B0`→`asm/4A3E`) and sets the story flag there (`asm/03B3`). Owner
  accepted `][` as-is. Owner **declined a live DOSBox/Spice86 capture** unless a
  real decision point needs one, so the 40-column grid stands on static evidence
  (40-space row clear `asm/53A0`, margins `asm/456B`/`48A7`/`4A2F`, scroll
  window `asm/5025`, mode 4 `asm/4A3E`) — revisit if anything contradicts it.
  Added `--dump-each` (frame-by-frame grid log) so screens can be eyeballed
  without running the game, and an echo-position assertion in the CP1 test.
  CP1 test now 10/10, CP0 9/9.
- **2026-10-27 — CP1 🔒 AWAITING OWNER EYEBALL.** The plan's CP1 acceptance is
  "green test + owner eyeball vs DOSBox screenshot (one-off)". The test is
  green; the screenshot comparison is the owner's. Things worth looking at:
  1. **Grid width.** The engine draws 40x25, not the 80x25 in plan §2. The asm
     is unambiguous (40-space row clear, 0x26/0x28 margins, video mode 4 =
     320x200). If the DOSBox screenshot disagrees, this is the thing to fix.
  2. **`][` = "II".** The title strings really contain `]` `[` bytes
     (`ULTIMA ][`, `PART ][`, `...GAME OF ULTIMA ][`). No font redefinition
     exists anywhere in the binary, so this is a glyph question for CP9; the
     engine renders the bytes as extracted.
  3. **`TYPE -` at row 11 col 5** is a genuine extracted line from
     `asm/02E6`'s region; nothing in the engine fills in a value after it, and
     no code was found that does. If DOSBox shows more text there, that is a
     missing producer and CP1 needs another pass.
  4. **Intro hold durations** (1.5 s / 2 s) are a choice, not a fact; the
     original burns a cycle loop whose real duration is machine-dependent.
  5. **Not implemented yet, on purpose:** the intro's PIC picture loads (CP9),
     'P' prompt path `asm/0764` (load path ahead of us), 'C' creation
     `asm/5400` (CP3). Both stubs print engine-authored notices naming their
     checkpoint.
- **2026-10-27 (owner):** CP0 **approved** ("sure"). Seam layer as the only
  platform boundary, offset-asserted DS state, determinism by clock
  substitution, selfmod premise retired, audio stub until CP9, CI engine job.
- **2026-10-27 — CP0 🔒 AWAITING OWNER ARCHITECTURE APPROVAL.** Built and
  tested; nothing here is irreversible and no gate criterion was loosened.
  Decisions needing sign-off before CP1 builds on them:
  1. **Seam layer as the only platform boundary** — `src/core/seam.h` wraps the
     four vtables in `src/platform/platform.h`; game code never touches a
     backend directly. Every seam call traces with its asm address.
  2. **DS state as an offset-asserted struct** (`src/core/state.h`) rather than
     a bag of accessors; CS-side statics stay with their owning module.
     Reference correction found while building it: `DS:0x2E` is the **column**
     and `DS:0x2F` the **row** (`asm/51FD_str_out_core.asm` loads DL from
     0x2E, DH from 0x2F, BH=0); plan §1 lists them as "page/col".
  3. **Determinism by clock substitution** — headless swaps in a virtual clock,
     which makes `rand_byte` (tick-derived in the original) replayable. No RNG
     is invented; the unproven tail of `rand_byte` is deferred to its first
     caller.
  4. **Selfmod premise retired** — the cluster's executed bytes match the
     static image in all 45 captures; CP3 no longer depends on a live dump.
     A live DOSBox/Spice86 dump is available as extra confirmation on request.
  5. **Audio stays a stub until CP9** — terminal bell in term mode, silent in
     headless; no system package installed.
  6. **CI gains an engine job** (build + `make test`); Phase 3 jobs unchanged
     and still green.

- **2026-10-23 — CP0 asm guard restated (gate change, owner-approved direction).**
  The guard md5'd each changed `asm/` file's non-comment lines against HEAD and failed on
  **any** deletion. That was right while a rename was the only legal `asm/` edit and it made
  the G22 merges impossible: merging sixteen fragments into three routines deletes thirteen
  files, and the gate would have called legitimate maintenance vandalism. The invariant is
  now the tree's **assemblable content**, not the file boundaries —
  `engine/tests/asm_body_guard.py` concatenates every `asm/*.asm`'s non-comment lines in
  start-address order and compares against HEAD. Renames, merges and splits preserve that
  stream; any body edit, insertion, deletion or reorder breaks it. **No criterion was
  loosened — the check is strictly stronger than what it replaced**, catching whole-file
  deletion by content loss rather than by git status, and localising the first divergent
  line. Demonstrated failing first: a one-line `das`→`daa` in `asm/2052` and a removed file
  both went red with useful messages; both were reverted. `assemble_clean.sh` remains the
  end-to-end proof and still reports `37344 bytes identical` after the merges.

## Deferred decisions (with the breadcrumb location)

| decision | status | breadcrumb |
| --- | --- | --- |
| Cross-sector attacks: the original whiffs any swing that crosses a sector edge, in both directions, because the attack-target lookup adds `pos + delta` unmasked while the movement accept path masks with `0x3F` | **DECIDED by owner 2026-10-21** — **original mode reproduces the restriction, enhanced mode fixes it.** Not a fidelity question: the owner called it a defect but chose to keep `original` meaning byte-faithful, so the fix is an enhancement and belongs to that mode. Consequence for the CP2 differential: a cross-sector hit is a legitimate mismatch in `original` mode and must not be reported as an engine regression | this section; mechanism confirmed against the harvest (target lookup runtime `0x23E6` versus accept path runtime `0x2664`) |
| `PICCAS` is loaded by the demonstration (`asm/03AC`) but **is not in `original/`** — the pack ships `picout pictwn picdng picspa picmin picdra` and no `piccas`; the fan pack supplies it | **open, new finding 2026-10-27** — one of the six demo pictures cannot load from shipped content. Affects CP1's demo and CP9's picture work; recorded so it is not discovered at a gate | `docs/phase4-implementation.md` § CP4 "Game data inventory" |
| `tlkxNN` street-NPC dialogue is **high-bit-set ASCII** (Apple II convention) — masking `0x80` yields readable text | **open extractor requirement** — the extractor must mask the high bit for talk files or every string arrives as mojibake. CP6 territory, recorded now so it is not rediscovered there | same section, evidence: `tlkx03` decodes to readable `<NPC> <VERB>: <LINE>` dialogue once `0x80` is masked |
| What reads `monsters` (2176 B)? No inline FCB name in the exe matches it | **open** | same section |
| What do the `mapx` 128-byte tails hold? Byte 0 is `0x1A` in all 82 data files; for `player` and `mapx00` the rest is zero, but most map tails carry ~127 non-zero bytes the 4096-byte read never touches | **partly answered** — the CP3 "what is the `0x1A` tail" question resolves to a DOS EOF marker for `player`, but the map tails are real unread data and remain unexplained | same section |
| Does `--data-dir` get written to, given the original rewrites its own sector files? | **RESOLVED by owner 2026-10-27** — (a) faithful write-back into the player's own files in `original` mode; (b) copy-on-write is the candidate for `enhanced` mode, and belongs with CP11's content surface. Binding consequences: the content dir must be writable, a not-ready dir must fail loudly like `U2_SAVE_NOT_READY`, writes are atomic and only for dirty sectors | `docs/phase4-implementation.md` § CP4 |
| The play screenshot the CP4 viewport numbers rest on is **not in the project** — it was pasted into a chat and only the prose survived, so the tile size and the 9-left/10-right split cannot be re-measured | **CLOSED 2026-10-27** — owner supplied overworld, town, plane and dungeon screenshots; stored at `captures/screenshots/` (gitignored) and measured. Tile = 16x16 px = 2x2 cells, viewport 20x10 tiles, player at window tile (9,5), row 24 blank | `docs/phase4-implementation.md` § CP4 "Viewport: resolved" |
| Persist the PRNG seed/state for replayable saves | **rejected for the save file by owner** — engine saves stay byte-compatible with the original player record (256 bytes at `DS:36..135`), which does not store `DS:0x23F..0x244`. A sidecar meta file is the only fallback, and only with a strong reason. Revisit when CP3's save work lands | this section, and `docs/phase4-implementation.md` § CP3 |
| Exact saved span and write path of the player record | **RESOLVED at CP2.5** — FCB random read/write (AH=27h/28h) via the `asm/52CA` DOS wrapper, 256 bytes at `DS:36..135`, FCB name `PLAYER  ` | `docs/phase4-implementation.md` § CP2.5 |
| What the 128-byte tail of the 384-byte player file is (one non-zero byte, `0x1a` at offset 256) | **open question** — no write path found; do not guess | `docs/phase4-implementation.md` § CP2.5 |
| Are the status pairs (HP/FOOD/EXP/GOLD) binary little-endian or packed decimal? | **CLOSED 2026-10-27 — packed decimal, most-significant-pair-first.** `asm/55B5` emits one byte as its two nibbles each OR'd with `'0'` with no zero suppression, and `asm/53A5` calls it on the low address first, so `[lo]` holds the thousands+hundreds and `[hi]` the tens+units. A gameplay screenshot settled the direction (`GOLD=0038` for 38 gold; a byte-swapped display would read 3800), and the ceiling is 9999, not 255. Consequence worth remembering: a fresh character's `0400` is **400 gold, not 4**. This row sat here as "open, needs the owner's eyes" for a day after the question was already answered, which is the two-hand-kept-copies failure AGENTS § 3 names — the fix was to delete the claim, not reconcile it. | `engine/src/game/status.h` (the citation), `engine/src/game/status.c` `draw_line()`, `engine/src/debug/dump_state.c` prints both `gold_bcd` and the decoded `gold` so the two can no longer disagree silently |
| After creation the engine returns to the title menu, which redraws over the status block | **known gap** — the owner cannot see the status bar at the gate until CP4 gives it a permanent home, or the flow pauses on a key first | `engine/src/game/creation.c` tail |
| `][` glyph mapping ("II") | **still open.** It was deferred to CP9 as an "SDL2 font decision"; CP9 arrived, the renderer shipped, and the mapping was never decided. The SDL2 half of that label is now doubly stale — SDL2 left this tree on 2026-11-03. Someone has to decide what the original's `0x8d` byte renders as, and it is not blocked on anything | `engine/README.md` § Known limits |
| PIC picture files during the intro | **CLOSED 2026-11-03.** The deferral said "no bitmap pipeline before then", and the pipeline turned out to be a non-event: the `pic` files are literal images of CGA mode-4 video memory, so the decoder is a planar unpack. `PICDRA` now shows behind the title splash and `PICSPA`/`PICMIN` behind the demonstration poem. See `docs/phase4-state.md` § The splash pictures | `engine/src/core/pic.c`, `engine/src/game/title.c` |
| Live (non-replay) play API / MCP wrapper | optional, build only if a harness needs it; the CLI contract is the substrate | `docs/phase4-plan.md` §7 |
| **D9** — the command crosswalk's `exec` column was a hand-kept copy of a derivable fact and had drifted wrong eight times | **DECIDED by owner 2026-10-28 (option a) and built.** `tools/cmd_handlers.py` now derives `exec` from `captures/spice86/*/spice86dumpExecutionFlow.json`. Numbers are generated (`--write-provenance` rewrites them; it had fallen behind on all 26 handlers, several counts simply wrong, e.g. A 15→28 and U 3→8). **Verdicts are not auto-rewritten** — a line claiming executed where the captures say never is reported and refused, because the sentence around a number is prose a person wrote. `--verify-provenance` is the gate, wired into `check_all.sh` as declared-optional because `captures/` is gitignored and CI cannot re-derive; it exits 2 rather than passing quiet when the captures are absent. The committed `# provenance:` line stays, so CI still consumes it — what changed is that a human no longer types the digits | this section; controls run: a false verdict went red on verify and was refused by write; a missing captures dir exited 2 |
| **D15 — CLOSED 2026-11-03** | **the decision is discharged: `0.1.0` is unblocked and cutting it is an owner action, not a work item.** All three outstanding items are closed and each was verified rather than assumed. (1) *CHANGELOG*: exists, and has been the source of every release note since. (2) *UNPROVEN markers*: `grep -rn "UNPROVEN:" engine/src --include=*.c` returns nothing, `tools/cmd_handlers.py` shows all 26 letters with execution evidence, and `cmd_marker_gate` passes. (3) *Starting location*: proven and observed — sector 20, cell (20,20), `captures/spice86/startloc-20261102`. Since 2026-11-02 the project has also shipped v0.0.8 (SDL3) and v0.0.9 (attribution), so the tag is not waiting on anything except a decision about whether `0.1.0` should also wait for CP8. **That is the live question: `0.1.0` before or after the endgame.** My reading: after. A point-zero-one implies you can finish the game, and today you cannot | `docs/phase4-state.md` § Backlog; `python3 tools/cmd_handlers.py` |
| **D15 (original, 2026-11-02)** — release version and when to tag | **DECIDED by owner 2026-11-02: the first tag will be `0.1.0`.** Deliberate, not an oversight: `v1.0-phase3` is the only tag and it predates the README re-aim, which moved the headline from the reconstruction to the reproduction. The existing tag stays as the Phase 3 marker rather than being moved or deleted. | see the row above |
| **D15 progress (2026-11-02, same day)** | **two of the three outstanding items are now closed.** (1) *Starting location*: **proven and observed — sector 20, cell (20,20)**. The creation commit writes it unconditionally at file `0x5BDC..0x5BE8` (`entry_x=entry_y=$0x14`, `sector_first=$0x02`, HP/food/gold high bytes `$0x04`), `creation.c` already implements exactly that, and a fresh headless creation run confirmed it live: `DS:00/01 = 14 14`, `DS:49/4A = 02 00`, HP 0400, with frames — `captures/spice86/startloc-20261102` (the run also confirms the post-creation self-restart to the ORIGIN splash is the `call L_000300` reboot, and surfaced a real tool bug: `spice86_type.py` was sending PcKeyboardKey `Return`, which the MCP rejects; the correct spelling is `Enter`, fixed same day). (2) *UNPROVEN markers*: **stale as a blocker** — `tools/cmd_handlers.py` shows all 26 letters with execution evidence (H and J closed 2026-10-25 by `captures/spice86/cmd-letters-20261025`), the markers are gone from the source, and `cmd_marker_gate` passes; what remains of that row is this sentence correcting the record. (3) *CHANGELOG*: written same day, see `CHANGELOG.md` — at which point D15's outstanding list is empty and cutting `0.1.0` is an owner decision, not a work item |

## Backlog

- [ ] **Improve the cleanroom substitute tile art. NO PRIORITY — deliberately parked.**
      47 of 64 tiles still fail their structural rules and sit in `PENDING_REDRAW` inside
      `tools/tile_rules.py`; running `python3 tools/tile_rules.py` prints the current list and
      the specific reason per tile, so this item needs no separate tracking list and must not
      acquire one. The set is usable and shipped; it is unfinished, and `make test` says so on
      every run.

      **The ones the owner judged worst, in case the list is long:** `06` emblem (drawn as five
      disconnected pieces, not one 4-fold-symmetric mark), `0A` signpost (four pieces), `0B`
      sea monster (water is not a stipple and the creature is not two components), `30`
      moongate (no top rule), `00`/`01` water (still chunky, wrong column parity), `03` forest
      (six components where three were asked for). The 25 letter tiles are close — they now
      carry their sign rules and fail only because the glyph touches the gutter rows.

      **If this is picked up, read `docs/cleanroom-procedure.md` first.** The redraw must be
      done by a fresh implementer working from `docs/tile-art-spec.md` alone. Whoever does it
      must not read `docs/tile-art-evidence.md` or `tools/tile_rules.py` — both are answer keys
      holding structure measured from the originals, and reading them would void the provenance
      chain that document exists to protect. Promote fixed tiles out of `PENDING_REDRAW` in the
      same commit that fixes them.

- [ ] **`docs/needs-gameplay.md` is the single tick-list for everything blocked on
      gameplay** (opened 2026-10-22, thirteen items). It carries a who-can-do-it and
      confidence verdict per item, and the standing hazards list that every capture
      session must read first. Several items previously labelled "owner must walk"
      turned out to be agent-drivable; **G0 (harden the probe so a dropped keystroke
      is never read as terrain) blocks the rejection-based items and should land
      first.** The five-question passability capture below is items G1–G5.

- [ ] **Queued for the next unattended batch — see `docs/overnight-harness.md`.**
      Build the generic binary-overlap probe (`H1`), inventory the game constants
      blocking the profile refactor (`H2`), and feed the result back into
      `docs/harness-portability.md` (`H3`). The Ultima III comparison itself (`H4`)
      is **BLOCKED on owner input**: a Ultima III DOS executable placed inside this
      project at a path the owner names. Nothing in this repo matches `*u3*`,
      `*ultima*iii*` or `*exodus*`, and `AGENTS.md` § 0 forbids going to look for
      one. The tool is calibratable offline against binaries already in `original/`,
      so only the comparison waits. Added 2026-10-22; context in
      `docs/harness-portability.md` § 8.

- [ ] **Decode the dungeon map format and reproduce the wireframe renderer.**
      **PARKED by the owner 2026-10-28: overworld and town first.** A dungeon is a
      first-person wireframe with its own byte semantics, so nothing measured there
      is evidence about the 2D world and nothing measured here may be carried in
      without re-measuring; the rule is in `AGENTS.md` § 3. This is
      the largest unexplained surface in the game and it blocks any real dungeon crawl.
      **It also owns the `original/monsters` question** — the 64×34 table at `DS:2900`
      is stroke data whose consumer is this renderer, so T9's "how do the coordinate
      pairs become shapes" cannot be answered until the renderer is decoded (see
      § Still unexplained). Not solved here on purpose; these are leads measured on
      2026-10-21 so the next session starts from evidence rather than from a blank
      page.

      **Already known, do not re-derive:**
      * Dungeons are sectors. Entering moved `DS:49/4A` from `3,0` to `3,5` and loaded
        `mapx35`. No video mode change; it stays CGA mode 4.
      * `mapx35` uses only **7 distinct bytes**, all multiples of `0x10`: `0x00` 46%,
        `0x80` 42%, `0xC0` 6%, `0xE0` 2%, and `0x40`/`0x10`/`0x20` at 15-19 cells each.
        Overworld maps use 30-50 distinct bytes. Seven aligned values is a flag or
        small-enum encoding of geometry, not an art index.
      * Only 7 of the 16 combinations a free 4-bit mask would allow actually occur, so
        it is not independent per-direction wall bits. Start from that constraint.
      * **Objects are runtime-written into the grid as small integers.** Disk vs live
        mid-visit: 32 cells differ, and every addition is a value `0x01..0x07` stamped
        into a cell that was `0x00` on disk (`0x1` x8, `0x2` x8, `0x3` x6, `0x4` x2,
        `0x5` x2, `0x6` x4, `0x7` x1). So placement is generated at load and
        representation is a tile - the same mechanism as overworld vehicles, where
        boarding stamps a cell and landing moves it. Answer to "random or tiles": both.
      * `mapx25`, `mapx35` and `mapx45` are **byte-identical** (md5 `e274897a...`). Any
        decode must explain or accept three sector numbers sharing one map, and any
        "which sector am I in" check must use the digits, not the bytes.
      * Light is a step counter at `DS:26`: zeroed on entry, ~150 on `I`, decremented
        per move, zeroed again on re-entry. `vga_ink` is a reliable dark/lit signal
        (exactly 0 when dark).
      * Command dispatch is mode-aware: `A` ATTACK outside, ADVANCE inside; `I`
        inventory outside, IGNITE TORCH inside; `K` is the rope exit; `G` looted a box.

      **Open, in the order worth attacking:**
      1. What the seven static bytes mean. The `A`-`Z` letter tiles are the foothold -
         in sectors that spell words they are legible ASCII, so a byte-to-glyph mapping
         is already known and may share the table.
      2. Which routine draws the corridor. The wireframe is mode-4 line primitives; find
         the line drawer and the table it indexes with the tile byte. `tools/vga_ring.py`
         and `tools/vga_screen.py` are the existing frame-reading tools.
      3. How the 64x64 grid maps to the crawl. The dungeon viewport is not a top-down
         window, so the (x, y) to "what is in front of you" relation is unproven.
      4. **The rope is not the tile under the player.** Climbed out at (5,5) and that
         cell is `0x80`, the same byte as 32 of 48 dungeon positions. Either it is a
         facing-relative property or a property of the entry point. Untested; do not
         write it up as a tile until measured.

      **Method that worked:** drive the owner into a known cell and dump grid plus frame,
      correlating rather than reading the listing cold. `tools/dungeon_capture.py` does
      this and re-reads the grid every sample on purpose - a cached grid produced stale
      labels and one fabricated finding. Evidence: `docs/spice86-live-findings.md` § 13
      and `captures/spice86/dungeon-entry/` (55 events, full DS + VGA each).

- [ ] **Standardised capture tooling: one recorder for gameplay, memory and CFG.**
      Today three ad-hoc probes were written in one session (`tools/turn_recorder.py`
      existed and was used; `tools/dungeon_capture.py` was written for the dungeon
      transition; two throwaway pollers went to `/var/tmp` and are now gone). Each
      re-solved the same problems and each got some of them wrong. A single tool should
      own: change-triggered sampling on a caller-supplied signature; full DS + VGA
      snapshots per event so history can be re-rendered without replaying; a
      **live** grid read every sample, never cached, because the map mutates during
      play and a cached grid caused a fabricated "vehicle depot" to be written into
      the docs and then retracted; the BIOS video mode at physical `0x449`, not
      `0x449*16`; and a VGA ink metric, which is what proved darkness was a real
      blank frame rather than a dim render. It should also subsume the CFG harvest
      and immediately cross-check `ExecutedInstructions` rather than relying on
      `tools/cfg_printed.py`, which silently dropped a genuinely executed print site
      (`LAND--NOT HERE!!!!!`, runtime `0x3B72`). Requirements: read-only against the
      emulator, output into `captures/spice86/<label>/`, and a negative control run
      per `AGENTS.md` before any result is cited. Fold in the known defects: the
      recorder's `screen` field is junk in CGA graphics mode, and `turn_recorder.py`
      logged 76 odometer anomalies in 3000 turns so single-turn boundaries in dense
      combat are not trustworthy.

- [ ] **Five-question Spice86 capture — passability, the art transform, food on a
      rejected step, the mountain tile, and the mode-byte writers.** Owner must
      walk; reads are passive. Amended 2026-10-22 after T8: question 2 was stale
      (it asked whether the ladder reads a tile, which F10 refuted live and T8
      killed statically) and a fifth question was added for the `DS:2CB`/`DS:2DE`
      attribution that is now the whole of C5. Full spec in
      `docs/passability-findings.md § Open`. In one session in sector
      20: (1) dump the staging buffer at `DS:272` against the sector map to settle
      whether `art = map_byte >> 1` or `>> 2` — `engine/src/core/view.h` currently
      claims both and they are mutually exclusive; (2) break at runtime `0x2423`
      and dump `AL, DS:13, DS:15, DS:18, DS:29, DS:2CB, DS:2DE` plus the
      destination tile on one ordinary step and one step onto a force field, to
      find what writes mode `0x2e` into `DS:29` and whether that write depends on
      the tile at all — **not** the old tile-vs-`AL` test, which is settled; (3) read
      `DS:53..55` before and after a deliberately blocked step to find out whether
      a rejected move costs 19 food, which the block ordering implies but does not
      prove; (4) **stand next to a visible mountain, walk into it, and read the
      destination tile byte** — this settles F9, where the engine currently marks
      `0x08` passable while F8 calls it the mountain, and it would be the first
      honest measurement of a blocked tile other than water; (5) capture
      `DS:2CB`/`DS:2DE` before and after sector entry, sector crossing, boarding
      and alighting, and stepping onto a field — whichever event moves them owns
      the tile→mode mapping, and the instruction pointer at the moment of change
      would close F3, C5 and T8 in one datum.

- [ ] **`tools/u2_edit.py` — save-file and live-memory editor.** Owner request,
      2026-10-21. Must do two jobs with one set of tables: edit a `player` save
      file on disk, and patch a live session through the Spice86 MCP
      (`local-mcp_write_memory`, segment/offset as decimal integers). Needs the
      lookup tables below, must handle packed BCD rather than binary, and must
      warn the user that the status band is a **cached render** — writes land in
      memory instantly but the screen only repaints on the game's own status
      draw, so "it still says 50" does not mean the write failed. Refuse to write
      an out-of-range BCD nibble (e.g. `0x9A`), which would corrupt a counter.
      Tables and offsets: `docs/spice86-live-findings.md` § 11.

- [x] **`tools/cfg_printed.py` fixed 2026-10-21.** It had two faults, both worth
      remembering. It read `CfgBlocks`, which is *discovered* blocks — Spice86
      statically expands branch successors — so it reported `MINAX IS DEAD!!`,
      `RING PROTECTS FROM FIELD!` and `FIELD CAUSES 1000 DAMAGE!` for a session
      that met none of them; it now gates on `ExecutedInstructions` in
      `spice86dumpExecutionFlow.json` (a list of `{Offset, Segment, Linear}`
      dicts, ~2400 entries) and refuses outright if that file is missing or
      empty. And messages carry `0x0D`/`0x8D` control bytes on **both** ends, so
      stripping only the leading ones silently dropped `LEGS PARALIZED!` and
      `--PARALIZED!` — the exact evidence being hunted. Address model, verified:
      the inline string sits at `runtime_offset + instr_offset + 3 + 0x200` in the
      file; do **not** add the segment base as well, which finds nothing silently.
      Validated across three harvests with a pre-fight negative control.
      Lesson kept because it generalises: a tool that confidently lies is worse
      than no tool, and a silent filter is the hardest lie to notice.

      **Follow-up, 2026-10-22 (overnight T1): the 2026-10-21 fix was still
      incomplete and dropped a real site.** It gated the *executed test* on
      `ExecutedInstructions` but still *discovered* sites by walking CFG block asm
      and still split inline bytes on the first NUL. `LAND--NOT HERE!!!!!` (runtime
      `0x3B72`) is in `ExecutedInstructions` yet was not reported: its inline bytes
      are `LAND--NOT HERE!!!!!\rCMD: \x00` — the message is terminated by `\r` and
      the next string follows before any NUL, so the NUL-split run
      `LAND--NOT HERE!!!!!\rCMD: ` failed the printable check and `extract()`
      returned `None` silently. The tool now (a) derives sites from
      `ExecutedInstructions` as the primary source (scan executed code-segment
      instructions for `E8` near-calls to str_out `0x4FDF`), with CFG blocks only a
      secondary seed for `--all`; (b) reads a message up to the first
      non-printable byte, not the first NUL; (c) fails loudly on an unrecognized
      dump shape instead of returning an empty list; and (d) carries
      `--selftest`, a positive/negative control (`LAND--NOT HERE!!!!!` present in
      `dungeon-35-live`; `MINAX CRIES: DIE FOOL!` and all combat strings absent in
      `dungeon-35-live` and `create-test-live`). Same lesson, sharper: fixing the
      *executed gate* was not enough — the *site-discovery* path and the *terminator*
      were the second and third silent filters hiding behind the first.

- [ ] **`tools/replay.py` — turn replay from the recorder snapshots.** Pure
      offline work on data already on disk, no emulator needed. Read
      `/var/tmp/u2-turns/turns/NNNNN.ds` plus `index.jsonl`, render the zoomed
      viewport or the full 64x64 map per turn, annotate each frame with detected
      events, and emit an animated sequence or a text log. Proven feasible: 151
      turns of the vga-test fight already yield the timeline, including the
      monster adjacent at turn 149 and gone at turn 150 with `+13g +4xp`. Reuse
      the glyph and monster-kind maps from `tools/map_render.py`.

- [ ] **Ordered message log.** The CFG gives the *set* of messages a session
      printed, never the order — `ExecutedInstructions` is a set of distinct
      addresses with no timestamps and no counts. So "the thief stole it, then I
      attacked" is not recoverable from a harvest. Two candidate fixes, neither
      tried: MCP `add_breakpoint` on str_out at runtime `0x4FDF` to capture an
      ordered print log (cheap, but pausing may upset game timing), or
      `tools/vga_ring.py` sampling the framebuffer continuously.

- [ ] **Run `tools/vga_ring.py` for the first time.** Written after the session
      ended; syntax-checked only. It exists because combat text is drawn and
      overwritten inside a single turn, so turn-boundary sampling provably cannot
      catch it — 151 turns of a real fight yielded zero readable framebuffer text
      while memory proved a kill.

- [ ] **Fix `tools/turn_recorder.py`'s two known flaws.** Both cosmetic, neither
      blocking, but they make the log harder to read. `DS:20` is scratch, not a
      pure invalid-move counter — the harvest shows it decremented in the walk
      loop and overwritten at `0x2BC7` — so including it in the change signature
      fabricates `[reject]` rows. And position and food update non-atomically, so
      one move can split across two samples and show as a false odometer
      anomaly. Only 12 of 151 turns classified as a clean event.

- [ ] **Screen ring for the recorder.** See `docs/spice86-live-findings.md` § 10.

### Proven from the harvests, for whoever picks this up

The CFG-plus-`ExecutedInstructions` technique settled several things without any
new capture. Executed: `ATTACK--`, `--MISS`, `--HIT!!!`, `--EXP.+`,
`KILLED--GOLD+`, `--INVALID MOVE!`, `LEGS PARALIZED!`, `--PARALIZED!`, and
`A THIEF STOLE SOMETHING!`.

**Update, 2026-10-21: the "not executed" list from that earlier pass is stale and
was removed rather than left to rot.** `RING PROTECTS FROM FIELD!`,
`FIELD CAUSES 1000 DAMAGE!`, `ARMS PARALIZED!` and `SAVED BY MAGICAL BOOTS!` have
since all been observed executing in `captures/spice86/castle-03-live`, together
with `SAVED BY MAGICAL CLOAK`, `SAVED BY IDOL!`, `MAGIC MISSILE!`, `SLEEP SPELL!`,
`-CASTLE` and a level-up. Only `MINAX IS DEAD!!` and `MINAX CRIES: DIE FOOL!` are
still unexecuted, and the sector-00 compound is now known to be a caster gauntlet
rather than the Minax fight. The `create-test-live` negative control still stands.

Two consequences worth keeping. `A THIEF STOLE SOMETHING!` is printed from the
same block that decrements `DS:D6+i`, so the theft of the staff is proven rather
than inferred. And `--PARALIZED!` lives at runtime `0x2527`, *inside* the
passability routine — so paralysis is enforced in the movement check itself and a
paralyzed step takes the same reject path as walking into a mountain. That is why
`--INVALID MOVE!` is what the owner saw while paralyzed, and it means the engine
must model paralysis as a movement rejection, not a combat overlay.

- [ ] **Publish the map viewer to GitHub Pages with cleanroom art. PARKED by owner
      2026-11-04, pending a decision — do not start without one.** The ask was a public
      Pages build of `tools/map_viewer.py` using only the cleanroom tiles, with a
      disclaimer that the graphics are substitutes and not the original art.

      **The blocker is not the art, it is the map data.** Cleanroom tiles settle the
      tile-art half. The viewer's actual payload is the `mapxNN` sector bytes, and the
      protected thing in a sector map is the *layout* — which tile sits at each of the
      4096 cells. Re-encoding that as a rendered PNG changes the medium, not the work:
      a faithful render of the real layout reproduces it, exactly as a traced redraw
      would. So "we ship the image, not the bytes" does not clear it. The owner's
      observation that fan maps and viewers are widely published online is true and was
      weighed; the counter is that prevalence is tolerance, not licence, and that
      `AGENTS.md` already answers this for us — the fan upgrade pack sits in
      `references/` and is deliberately gitignored, and release artifacts are "binaries
      only, never a data pack."

      **What is unambiguously ours and publishable today:** the viewer code, the
      cleanroom SVGs, and our own `disasm/tile-art.tsv` descriptions. Note also that
      the viewer links to `tile-review.html`, an original-vs-cleanroom sheet built from
      the originals — a public build must strip that link.

      **Open decision for the owner.** Either pick a variant that ships no game data:
      (A) load-your-own file input, parsed client-side, nothing uploaded, plus a save-PNG
      button so a visitor renders their own sectors; (B) a synthetic demo sector we
      author; (C) both — the recommended shape, and it delivers the workflow the owner
      was after on their own data; (D) tile atlas only, no map view. Or (E) explicitly
      amend the `AGENTS.md` data rule to allow publishing derived map renders, which is
      a policy decision to record with its authorisation, not an implementation detail
      to route around via the file format.

      **Mechanical notes for whoever picks it up:** `map_viewer.py` already has
      `--cleanroom` and an optional `--tiles`, so a `--public` mode that omits the
      original PNGs entirely is small; `load_maps()` currently hard-exits when no
      `mapx*` is present and needs a no-data path. There is no PNG export today (no
      `toDataURL`/`download` anywhere in the file) — the canvas is live only, so the
      save-image button is new work, roughly five lines. A Pages workflow would be the
      repo's first; see `.github/workflows/release-builds.yml` for the existing pattern.

## CP5 — combat tickets (scoped 2026-10-21; mechanics landed 2026-10-22, wiring in progress)

Scope rule for this checkpoint: **no stubs.** A stub for "resist paralysis" encodes
boolean immunity, passes the tests that do not exist yet, and looks finished. Every
ticket below is either a proven behaviour with a byte citation, or an explicitly
named unknown. Where the input is unproven the ticket is `blocked`, not guessed.

Full evidence: `docs/spice86-live-findings.md` § 12, and the function headers in
`asm/` for `0AAA`, `0AD3`, `0E70`, `0EC2`, `0F12`, `0F48`, `0FCD`, `24B5`, `2FAD`.

| ID | Ticket | Status | Why |
| --- | --- | --- | --- |
| C1 | Three independent status counters as persistent state: legs `DS:26B`, arms `DS:26C`, sleep `DS:26D`, each set to `rand & 0xF` (0..15 turns) and stored in the player record | **implemented 2026-10-22** (counters); **persistence question CLOSED 2026-10-23 — they are not persisted and cannot be** | `asm/0E70`, `asm/0EC2`, `asm/0F48`. Arms and legs are separate counters with separate items, so a character can be paralysed in one and free in the other; both were observed non-zero simultaneously (9 and 8) alongside sleep 8. **Corrected 2026-10-22 (overnight T10): "stored in the player record" is not byte-proven and is probably wrong.** `DS:26B/26C/26D` are 619..621, outside the proven save record `DS:36..135` (54..309) per `docs/save-format.md`. The counters are proven to exist and to be independent; their persistence is not. `engine/tests/combat_check.c` asserts they live outside the record rather than asserting unsupported persistence. **Settled empirically 2026-10-23**: poked `07 09 08 08`, pressed `Q`, the bytes are absent from the 384-byte file and read back `00 00 00 00` on reload — `captures/spice86/cp5-g24-save/`, `docs/spice86-live-findings.md` § 17. A fourth counter `DS:26A` ticks with them. |
| C2 | Resist is a **~75% roll on a live item-array slot**, not immunity: `[DS:D9]` BOOTS, `[DS:DA]` CLOAK, `[DS:E4]` GREEN IDOL; with the item the `L_005417` roll must be `>= 0x40`, without it the effect lands with **no roll at all** | **implemented 2026-10-22** | This is the single easiest thing to get wrong, and the pre-existing docs had it wrong as a boolean gate. Needs a distribution test, not a present/absent test. |
| C3 | Paralysis is a **movement rejection**, not a combat overlay | **implemented 2026-10-28** — `overworld.c` rejects the step with `--PARALIZED!` ahead of the terrain test, gated by a Daemon fight with an Orc control | `--PARALIZED!` is at runtime `0x2527`, inside the passability routine, so a paralysed step takes the same reject path as walking into a mountain. Couples to the CP4 passability work and to the `--INVALID MOVE!` path. |
| C4 | Magic missile: **no save path exists** | **implemented 2026-10-22** | `asm/0F12` has no inventory gate and no roll, unlike the other three. Now asserted in `combat_check.c` two ways: the missile applies whatever the inventory holds, **and it consumes no random byte** (`u2_rng_rolls` unchanged) — the second is the sharp one, since a boolean-immunity impl both rolls and skips. The accumulation into `DS:20/21/22` is still **not modelled**: its BCD weight against the HP subtract is unproven and `DS:20` is separately claimed as `move_scratch` (disputed). The spec asserts the proven property and stays silent on magnitude. |ing as a named unknown. |
| C5 | Force field: ring gate `[DS:D6]`, exactly **-1000** via `stc; sbb al,0x10; das` on `[DS:51]`, and a death branch when HP < 1000 | **implemented 2026-10-22** (behaviour; placement still open) | `asm/24B5`. Damage, gate and death branch are proven and **both branches have executed** — the live force-field turn is on record (fight turn 2909: HP −1000, `[DS:D6]=0`, `[DS:15]=[DS:18]=0x2e`), and the `24B5` region executed in 2 captures. ~~blocked on a capture~~ **Reclassified 2026-10-22: the field was captured.** What remains open is only *placement*: the trigger is a **mode** byte (`[DS:29]&0x7f == 0x2e`), not a tile byte — `0x2e` occurs 0× in `mapx00` — and the tile→mode mapping is the unresolved indirect store into `DS:2CB/DS:2DE`. That belongs to the passability ticket (G4), **not to combat**. Implement the behaviour; do not wire C5 to a tile byte. **SUPERSEDED 2026-10-28 — placement is
   solved and it IS a tile: `0x5C`.** Measured in `captures/spice86/g29-field`: stepping onto a
   `0x5C` cell in sector 03 costs exactly 1000 HP with no ring, six times over, and nothing with
   the ring held; a plain `0x70` step costs nothing. The instruction above was wrong because of a
   units error — it searched `mapx00` for `0x2e`, but `0x2e` is the **mode** byte and the mode is
   `map_byte >> 1`, so the tile is `0x5C` and `0x2e` could never appear as a map byte (map bytes
   are multiples of 4). The `0x5C -> 0x2E` sample was already recorded in
   `asm/2FAD_vehicle_board_dispatch`. C5 can now be wired to tile `0x5C`. |
| C6 | Theft **deletes** from the item array; killing the thief restores nothing | **implemented 2026-10-22** — premise corrected the same day | ~~thief is monster type `0x40`~~ **wrong on two counts.** `asm/0FCD` contains exactly one `cmp $0x40` (file `0x11D1`, not `0x1244`, which is outside the span) and it compares a **random value** — a ~25% chance of attempting a theft, not a type test. The routine never reads a monster record; which monster reaches it is the caller `asm/0AD3` dispatching on the **kind index** `[DI+0x1B7]`. Mechanics to implement: roll `<0x40`; pick slot `DS:D6 + (rand & 0xF)`; if that slot is **zero, do nothing and print nothing** (a failed theft is silent); else BCD-decrement it by 1 and print `A THIEF STOLE SOMETHING!`. Measured: 8 items stolen, 0 returned, kill paid a random drop like any other. Because the C2 gates read the item array live, a flagged-but-present item would wrongly keep the resist. |
| C7 | Map byte -> vehicle code transform for BOARD | **resolved 2026-10-21** | `[DS:14] = map_byte >> 1`, measured on 53 of 54 walk samples across four terrain bytes. So vehicle tiles are `0x44` horse, `0x48` ship, `0x4C` plane, `0x50` rocket. Confirmed by the game: `B` on a `0x4C` tile set `[DS:13] = 0x26` and printed `' PLANE'`. An earlier note here claimed the plane was `0x34`; that was a `%02d`-against-hex-filenames render bug and `0x34` is art `0x0D`, a humanoid figure. ~~Still unexplained: the `y*16 + x` indexing at file `0x6AC0` against a 64-wide grid.~~ **Resolved 2026-10-22 (overnight T7):** `asm/68C0` indexes the **dungeon** grid (`word[DS:472] + [DS:25]*256 + y*16 + x`, a 16-wide paged view of the DS:1800 buffer), not the overworld sector grid; reached only from the dungeon path. See `docs/spice86-live-findings.md § 12`. |

**Combat entry is now observed, not designed (2026-10-23).** `A` + a direction attacks the
adjacent cell; the fight is drawn as one line of the world view's `CMD:` log
(`ATTACK--EAST--HIT!!!`, then `KILLED--GOLD+1--EXP.+02`) with no battle screen and no mode
switch. A creature also *blocks* movement — stepping onto one prints `EAST--INVALID MOVE!`
over a walkable tile. Full evidence and the live screenshots: `docs/spice86-live-findings.md`
§ 14, capture `captures/spice86/cp5-fight/`. Two consequences: the encounter wiring is a
command handler plus an adjacency test, not a subsystem; and the acceptance bar "a rendered
fight" is now a concrete target rather than a vibe.

**The status tick is time, not moves** — measured from the existing
`fight-20261021-castle03` recording, where `DS:26B/26C/26D` decremented while the player
never moved. The tick routine is `asm/0C33..0C53`, and it ticks a **fourth, unnamed counter
at `DS:26A`** (zero throughout that fight). So in the engine the tick belongs to the main
loop, not to `overworld` movement — which is what C3's "one movement system" needs anyway.

**The turn expires on its own, and the mechanism is a poll counter (2026-10-23, owner-ruled,
then measured, then corrected against the bytes).** Each turn has a budget; when it expires
with no action the game performs a PASS for you, and a half-typed command expires as
`ATTACK--PASS`. Observed with nothing sent at all: an empty log filled itself with `CMD: PASS`
lines, ~7.5 s apart (counted by the food a pass costs). The engine **already implements
this** — `overworld.c`'s idle auto-PASS with `U2_AUTO_PASS_POLLS 256`, because the bytes say
`incb DS:21; jne <loop>` (runtime `0x08D6`): a poll count, not a clock, and there is no
`INT 1Ah` in the corpus. So the ~7.5 s is what that count costs at `--Cycles 3000`, and it
must not be hard-coded as milliseconds — a prior session measured ~7.6 s and refused to
encode it for exactly that reason. The owner's enhanced-mode knob already exists as
`--auto-pass-polls` (`-1` = no timeout = true turn-based). Creature turns are per-creature
but **mostly no-ops**: 7% of (creature, turn) pairs moved anything, movers shift 2 cells,
and 26 of 32 slots never budged. **And the status counters do not tick per turn** — 15
decrements in ~0.7 s against exactly one PASS; `asm/0C33..0C53`'s caller is still
undiscovered, so no per-turn decrement may be wired until it is found. Evidence:
`docs/spice86-live-findings.md` § 18 and its addendum, capture
`captures/spice86/cp5-turn-timeout/`; build order in `docs/phase4-kickoff-cp5.md` W6b/W7.

**W5, W6, W6b and W9 are done (2026-10-27), and the CP5 bar is met in the scripted
sense.** `engine/src/game/ai.c` is the creature turn: the `mon_role` dispatch
(inert at 0 and at `0x81..0x85`, full pass at 1, alerted-and-moving at 2, and
deliberately nothing at 3 because `asm/1086` is unread), the unit step from
`asm/4F11` with its torus sign trick, the retreat at 14 HP, the per-species tile
rule, and the four specials behind `asm/0AAA`'s proximity-plus-1-in-8 gate.

> **Corrected 2026-10-26, and now fixed:** `asm/1086` **is** read — it is the role-3 wander, a sticky
> random walk re-rolled one turn in four — and the engine was freezing those slots. Role 3 is 179 of
> the shipped `monx` role bytes, so this was the majority of town creatures. Implemented in `e1db4f2`;
> `ai_check` 44 → 49 with three controls that each go red. The plan and the measured outcome, including
> the negative result on the thief, are in **`docs/plan-role3-wander.md`**.

> **Highest-value item on the board (owner, 2026-10-26):** **`docs/plan-cross-game-replay.md`** —
> record the original's starting save, RNG register, key sequence and per-turn DS segment, replay the
> same inputs on our engine, and diff the two state mirrors turn by turn. It is feasible here and
> usually is not, because `state.h` is a byte-for-byte mirror of the original's DS block and the
> original's PRNG is a clock-free 6-byte shift register seeded from `0x40:6C`. Phase 0 is a
> one-afternoon determinism probe that de-risks everything else. The CP2 digest-scope decision (§ 5)
> is a prerequisite, not a side quest.

`u2_ai_alarm()` is `asm/113A`: a landed hit inside a site sets role 1 on slots 7..0,
which is § 16's sector-wide guard response found in the static tree eight days after
it was measured. W9 is a gate that runs in CI with no game files at all —
`tests/cp5_fight_seed.py` emits a synthetic mapx, monx and PLAYER, and `run_cp5.sh`
drives `./u2` through the real input path and asserts `ATTACK--EAST--HIT!!!`,
`KILLED--GOLD+n--EXP.+m`, and that GOLD and EXP both wrapped past 9999 instead of
clamping. Its control empties the sector and requires MISS. Full write-ups:
`docs/monster-spawn.md` § 13 and § 14, `docs/sector-rulesets.md` § 6.

**W8 is done (2026-10-27), and the span was the finding.** `asm/0C62` had been left as
`sub_0c62 — behavior not yet reviewed` because each of its four fragments looks like
nothing alone. Laid end to end, `0C62..0C99` is the death handler: nine zero stores into
`DS:51..59`, a repaint, three newlines, the name from `DS:36` via `asm/507A`, then
` IS DEAD!` and a newline. Every site that can kill jumps into it — the food borrow-outs,
the force field, Minax, three dungeon-event sites and `cmd_L` — which is why the engine got
one function (`u2_death`) rather than three lines inside the food subtract. The captured
frame `captures/spice86/cp5-weak-fight/death1.png` settled two things the bytes left open:
all four status fields repaint (the routine itself calls only the H.P. line), and the
announcement sits on row 22 with the bottom row bare, because its own trailing newline
scrolls it up. `engine/tests/death_check.c` pins both, plus a control that a survivable
food spend announces nothing; the gate was mutation-tested by removing the announcement and
watching four checks go red. What the game does *after* the announcement is still unknown
and is G27 on `docs/needs-gameplay.md`, marked agent-drivable.

One design constraint that outlives the checkpoint: the original keeps **two** copies
of some item counts and one goes stale. `DS:6D` still read 1 after a thief stole the
boots, while the live gate `DS:D9` correctly went to 0. The engine needs a single
source of truth for item counts, and any resist check must read it rather than a
cached boolean.

> **`DS:6D` is disputed, 2026-10-23 — do not build on the paragraph above until this is
> resolved.** `asm/3DFA_offer_only_outdoors_earth` tests `[DS:6D]` and prints
> `ONLY ON EARTH!` when it is non-zero, which is a location gate observed live while
> pressing `Q`, not an item count (`docs/spice86-live-findings.md` § 17). The byte is
> inside the saved record and the value `1` seen after the theft is exactly what an
> on-Earth flag looks like. Either the byte is genuinely reused or the boots reading was a
> coincidence of value. The *conclusion* (one source of truth for item counts) is sound on
> its own merits and `DS:D9` is still the live gate; it is the *evidence* that is now in
> doubt. Resolving it: steal the boots, then read `DS:6D` and `DS:D9` together and check
> whether `DS:6D` also flips the save gate.

**Second design constraint, from the owner on 2026-10-23, and it is architectural.**
Asked whether paralysis survives leaving combat, the answer was *"I don't know if that
survives SAVE, but there's not really a leaves-combat state."* That is a statement about
the original's shape, not about a detail: combat is not a separate mode you enter and
exit, it is the same continuous space with a different thing happening in it. Three
consequences, all of which the scope ruling now commits us to:

* **It independently corroborates C3.** If there is no separate combat mode, then
  paralysis being a *movement rejection* rather than a combat overlay is not a quirk to
  emulate, it is the only shape that makes sense — a paralysed step takes the same reject
  path as walking into a mountain, because there is one movement system, not two.
* **It contradicts a likely engine assumption.** If the reimplementation grew a battle
  screen or a combat-mode flag, that is a deviation to surface, not a feature. Check this
  before writing the encounter wiring, because it is much cheaper to discover now.
* **It makes the SAVE question the only persistence question.** If nothing is entered and
  left, then the only way a status counter can outlive its context is by being saved — and
  C1 already says the counters sit at `DS:26B..26D`, **outside** the proven save record
  `DS:36..135`. So either they are not persisted at all, or the save record is bigger
  than `docs/save-format.md` currently proves. Both are testable headlessly: seed
  non-zero counters, save, reload, read back. Registered as a capture ticket.

The legs/arms asymmetry the owner described — **legs out means you can fight but not move,
arms out means you can move but not fight** — is consistent with C1's three independent
counters and with C2's separate item gates (`[DS:D9]` boots, `[DS:DA]` cloak), and is the
behaviour the CP5 acceptance should assert, since a single boolean "paralysed" flag cannot
reproduce it.

**C2's discrimination is now CI-enforced, not asserted.** `engine/tests/combat_check.c`
runs on every `make test` against two shipped fixture models: `combat_model_faithful.c`
(must pass) and `combat_model_boolean_wrong.c` (must fail). A third control, added
2026-10-23, runs the same harness with `CP5_SPEC_NO_TESTS=1` so it reports 0 checks,
and asserts the gate reads that as a real zero and goes red — the guard that reports
"the spec is not running" is itself now proven rather than asserted. If the spec ever
stops rejecting the boolean-immunity model, `test-cp5` goes red. Building this also exposed that
`run_cp5.sh` was exiting 0 unconditionally while `combat.c` was absent, so the gate
could not fail at all — fixed in the same commit.

## BCD counter tickets — wrap vs cap (scoped 2026-10-22, not started)

T4 proved one instance; `tools/bcd_census.py` proves it is the house style. Every
numeric field the original keeps in packed decimal behaves the same way, and the
behaviour is a property of the **storage width**, not of any individual counter.

Census (regenerate with `python3 tools/bcd_census.py --counters-only`; do not copy
these numbers, they will drift):

| op | known-counter sites | guards found |
| --- | --- | --- |
| `daa` add | 30 | 28 none, 1 branch-only, 1 clamp |
| `das` sub | 31 | 13 borrow-test, 18 none (read per chain, see below) |

The rule that falls out:

* **Additions do not guard the ceiling.** `adc`/`daa` per BCD byte with the carry out
  of the top byte discarded, so every counter wraps at its digit capacity.
* **Multi-byte subtractions test the borrow once, on the top byte.** The idiom is
  `cmc; sbb; das; cmc` — the second `cmc` deliberately re-exposes the borrow in CF so
  a following `jb` can branch on it (`asm/2599` food, `asm/2495` HP, `asm/2052`
  gold). A `cmp`/`test` search misses this entirely, which is why an earlier
  heuristic under-counted the guards.
* **Single-byte subtractions test nothing.** `torches` (`asm/37AE`), `keys`
  (`asm/435E`), `tools` (`asm/277B`) and the item slots (`asm/0FCD` theft, `asm/6999`,
  `asm/6DFD`) store the adjusted byte with no borrow test. Under BCD underflow that
  byte becomes `0x99` — **99, not 0**.

Ceilings, from the proven widths in `docs/save-format.md`:

| field | DS | width | ceiling | at ceiling, +1 | at zero, −1 |
| --- | --- | --- | --- | --- | --- |
| `str agi sta cha wis int` | `4B..50` | 1 BCD | 99 | 00 | 99 (unguarded) |
| torches / keys / tools | `64/65/66` | 1 BCD | 99 | 00 | 99 (unguarded) |
| weapons / armour / item counts | `76+i` / `96+i` / `D6+i` | 1 BCD | 99 | 00 | 99 (unguarded) |
| HP | `51..52` | 2 BCD | 9999 | 0000 | borrow-tested |
| EXP / GOLD | `56..57` / `58..59` | 2 BCD | 9999 | 0000 | borrow-tested |
| food | `53..55` | 3 BCD | 999999 | 000000 | borrow-tested |

| ID | Ticket | Status |
| --- | --- | --- |
| B1 | **Original mode reproduces the wrap.** Re-scoped 2026-10-22 after checking: the clamp this ticket was written against (`u2_bcd16_encode()`) had **zero callers** — dead code, not a live deviation. The live BCD arithmetic (`bcd_sbb()` in `engine/src/game/overworld.c`) was already faithful: it implements `das` including the AF half-borrow, tests the borrow out of the **top** byte and branches to the `asm/0C62` starvation zeroing — exactly what the census says multi-byte fields do — and was verified against a reference model (a fresh character dies on exactly the 4001st PASS). Done now: the clamping encoder is **deleted**, with a note in `core/state.c` on why it must not return. Remaining: when CP5 first adds gold/exp, write `bcd_adc()` as the deliberate mirror of `bcd_sbb()` — discard the carry, cite `asm/2DC1`/`asm/2DEE`, pin with the measured `9999 + 15 -> 14`. | **decided by owner 2026-10-22** (original mode reproduces the wrap). Trap removed; the add helper lands with CP5, its first caller |
| B2 | **Enhanced mode clamps at the ceiling and says so.** Catch the carry/borrow that original mode discards, clamp at the field's ceiling, and print a message (`NO ROOM` / `DENIED AT MAX`) in the existing message window. The same flag serves both modes, which is why honouring the BCD mechanics needs no policy layer. The message must be **enhanced-only**: the original prints nothing when gold wraps. | todo |
| B3 | **Cap override is downward-only.** The ceiling is a property of the storage width, so a value above it is unrepresentable in the save format — the save record is `DS:36..135` copied verbatim (`engine/src/core/save.c:103,129`). An override above the ceiling must be refused, not silently truncated. Raising a cap would require an enhanced-mode save format change, which is a separate and much larger decision. Follow the `auto_pass_polls` precedent in `engine/src/core/ctx.h` for the enhanced-knob shape. | todo |
| B4 | **Never normalise loaded BCD.** Invalid nibbles are reachable: BCD underflow manufactures them (`0x10-0x0B -> 0xFF`, `0x30-0x1F -> 0x0B`). A decode/encode round-trip **repairs** them — measured: `9A 99 -> 10099 -> 99 99`, and `1B 05 -> 21 05` with no clamp involved. **Downgraded from a fix to a standing rule 2026-10-22:** the only encoder that could round-trip has been deleted (see B1), and `engine/src/core/save.c` loads and writes the record by verbatim `memcpy` (lines 103/129), so no normalising path exists. Keep it that way: arithmetic mutates BCD bytes in place, never through an int round-trip. | **invariant, not a task** — held by the absence of an encoder and the note in `core/state.c` |
| B5 | **`state.h` has no names for these counters.** `engine/src/core/state.h:168` lumps `0x64..0x238` into `reserved_64[0x1D5]`, so torches, keys, tools, weapon/armour/item counts and the boots slot are all unnamed — while `docs/save-format.md` documents them as proven. Name the fields and add the `_Static_assert`s so the wrap tickets have something to attach to. | **done 2026-10-22.** Split the blob into `torches`/`keys`/`tools`/`boots_count`/`weapons_owned[10]`/`items_held[16]`, each with its asm add/sub sites cited in the comment. Two extras found while doing it: `readied_weapon` (0x61) and `worn_armour` (0x62) were proven but buried inside `reserved_5C[7]` — now named. `boots_count` carries the stale-copy warning (it kept reading 1 after theft while the live gate `DS:D9` went to 0) so nobody wires a resist to the wrong byte. **`0x96` deliberately left unnamed**: the census inference that it is an owned-armour count table rests only on three asm sites doing BCD arithmetic on `0x96(%di)` beside `0x76(%di)` — no display site, no live read/write-back. Marked CANDIDATE, NOT PROVEN. Added `sizeof(u2_state) == 0x650` as a guard so a future split cannot silently lose or gain bytes; proven to fire by perturbing one offset. |

Not yet proven, and deliberately not guessed:

* **Reachability of the single-byte underflow.** The bytes show no borrow test; they do
  not show that the game lets a counter reach zero and be decremented anyway. A
  `keys == 0` guard upstream of `asm/435E` would make `0 -> 99` unreachable. Needs a
  capture or a caller walk.
* **The `das` results are inferred from documented 8086 semantics, not observed.** The
  `daa` side I did validate against a real observation (T4). Nothing in `captures/`
  shows a BCD underflow, so `0 - 1 -> 99` is a property of the format, not a
  witnessed bug.
* **26 non-counter `daa`/`das` sites are excluded** from the census tally as unresolved
  targets or mis-decoded data (e.g. `asm/025E`, the "REVENGE" title printer, whose
  `das` is followed by the nonsense `add %al,(%bp,%di)`). Do not cite them.

---

## Paralysis stranding on the open world — FIXED, one sub-question open (owner-found, 2026-10-26)

**Symptom, reported by the owner playing the noVNC build:** the player is stuck with legs
paralyzed and never recovers. The owner's statement was flat — *the original 100% does not strand
you when paralyzed* — and that is what overturned two confident readings here.

**What was wrong, in order of discovery:**

1. **A bogus site gate in the engine.** `overworld.c` wrapped the counter decrement in
   `if (ctx->state.sector_second != 0)`, sourced from `asm/0C1F`'s header claiming the tick ran
   "only when [DS:4A] != 0 ... never on the world map", at `confidence: high`. That claim is
   **false**: `jne L_000e33` jumps *over* the world-map block and *lands on* the tick, so both
   arms reach it. The gate is removed and the header corrected.
2. **Removing the gate was necessary and is not sufficient.** The new CP5 C3b proves it: on sector
   00 the last of ten west steps is still refused. The engine ticks the counters only on the
   damage path, and a refused step does not advance the turn, so a paralyzed player is never
   wounded again and therefore never ticks. **A deadlock the site gate was hiding, not causing.**

**What is known about the original's shape:** the tick chain is
`0x0BAF -> wound loop 0x0BB2 -> 0x0BD1 -> 0x0C02 -> 0x0C1C -> 0x0C1F -> tick 0x0C33`, and
**fourteen** effect routines tail-jump into `0x0BAF` (`0x0CAD`, `0x0CBD`, `0x0CD9`, `0x0D61`,
`0x0D81`, `0x0DA1`, `0x0DC1`, `0x0DC8`, `0x0DF0`, `0x0E0F`, `0x0E2E`, `0x0E4D`, `0x0E6D`, `0x1078`).
So the tick is entered per effect, not per turn and not per wound — a different shape from the
engine's "once per wound", and probably the reason the original never strands anyone.

### ANSWERED FROM THE EXISTING CORPUS — the engine's model is wrong in a third way

No new capture was needed. `captures/spice86/fight-20261021-castle03/turns/` holds **3000 per-turn
full DS dumps** from a real castle fight, and the paralysis counters are plain bytes in them
(`DS:26B` legs, `DS:26C` arms, `DS:26D` sleep). Reading the series settles it:

| turn | counter transition | HP delta | player moved |
| --- | --- | --- | --- |
| 59 | legs 0→2 | 0 | no |
| 63 | legs 2→1 | −43 | no |
| 87 | legs 1→0 | −1 | **yes, 30→31** |
| 103 | legs 0→4 | 0 | no |
| 110 | legs 4→3 | **0** | no |
| 125 | legs 3→2 | −1 | no |
| 253 | arms 1→0, sleep 9→8 | **0** | no |
| 279 | arms 1→0, sleep 4→3 | **0** | no |

Three conclusions, all measured rather than inferred:

1. **The counters decay with no wound at all.** Turns 110, 253 and 279 decrement while HP is
   unchanged. So the tick is *not* on the damage path, and the engine's "once per wound" model is
   wrong — as is the "once per effect" theory put in its place earlier today.
2. **The player recovered while unable to move.** `legs` reached 0 at turns 87 and 146; max `legs`
   seen across the fight was 15; the player sat at `(30,32)` for essentially the whole capture and
   shook it off anyway. This is the owner's claim, confirmed by bytes.
3. **Monsters kept acting while the player was stuck** — HP falls at 63, 125, 135 and 146 with the
   position never changing. So turns advance even when the player's own step is refused.

**The remaining divergence, and it is a turn-semantics question:** the engine's paralysis reject
`continue`s without calling `creature_turn()`, so a stuck player never advances a turn and never
ticks — the deadlock. The corpus says the original does advance turns while the player is pinned.
Changing that is a turn-semantics change, not a constant tweak, so it is proposed rather than
assumed: tick the counters once per turn, and let a paralyzed reject still advance the turn so the
monsters act as they demonstrably did. The food-charge evidence (asm/2520 returning before the
mode dispatch) is untouched by that — no food on a reject, but the turn still passes.

### THE MECHANISM, measured twice and corrected twice — the tick is on the input path, not the turn

**Three readings have now been made of this one counter, and two of them were wrong.** Recorded in
full because the errors are the instructive part.

1. *Once per wound.* Dead: the corpus shows the counters decaying on samples where HP never changed.
2. *Once per turn.* Dead, and this is the one to learn from. A multi-shot breakpoint on the tick
   while the game sat idle showed **food −10 at every stop**, and −10 is a PASS, so it looked like
   one tick per turn. The observation was right and the inference was not: **an idle game
   auto-passes, so the tick coincided with the pass without being caused by it.**
3. *On the input-handling path.* Current, and it separates the two cases directly. Forcing
   `DS:26B=15` and pressing a direction key repeatedly — where **every press is refused and a
   refusal spends no food** — gave **15 decrements, every one with food unchanged**. The same force
   with no input gave 15 held and one decay, that one coinciding with a real auto-pass.

| hypothesis | verdict | what settled it |
| --- | --- | --- |
| gated on site (`DS:4A != 0`) | false | `asm/0C1F`'s `jne` skips *over* the world block and lands *on* the tick |
| once per wound | false | counters decay with HP unchanged |
| once per turn | false | **15 decrements with food unchanged** while every press was refused |
| **once per handled input** | **current** | refused steps tick (no food spent); idle passes tick (food −10) |

Capture on disk at `captures/spice86/paralysis-two-arm/readings.md` (gitignored, like all
captures — the method is therefore written out here as well). Run label `paratwo`. Method: no daemon
and no RNG — `spice86_type.py --poke 8be:26b=0f` forces the counter and reads the write back, and
food is the discriminator (move 19, pass 10, refusal 0).

One instrument trap worth keeping: the first probe parsed the tool's whole stdout for any two-char
hex token and so read the keyboard typeahead ring (`ring=e0 4d e0 4d`) out of an unrelated status
line as the counter value, reporting 1 and 77. 77 is outside the documented 0..15 range, which is
the only reason that was noticed instead of believed.

**Why the corpus analysis misled me, which is a general lesson.** The recorder writes a snapshot on
*signature change* at about 2.9 effective samples per second, so a burst of auto-passes between two
samples collapses into one food delta and one counter delta. "0 of 203 decrements fell on a
food-changing sample" was an artifact of the instrument. A change-triggered recorder cannot answer
per-event questions; the breakpoint and the forced-poke probe can.

**Engine consequence, and it is the owner's bug.** The engine's refused step does `continue` without
`creature_turn()`, so a per-turn tick left a mashing player stranded — which is exactly what was
reported. The tick now runs in `creature_turn()` **and** on the paralysis reject path, via
`status_tick()`. With the per-turn version alone the stranding survived; that is how the second
error was caught.

**Still unproven, and deliberately not guessed:** whether a terrain-blocked INVALID MOVE reject also
ticks. Two attempts to measure it were confounded (all four directions passable; then the player
drifted and spent food). The engine ticks the paralysis reject only. See the capture's "Not settled".

**Tried and rejected, so the next reader does not repeat it:** moving the tick from per-wound to
**per turn** was attempted on the strength of the table above. It does not fix the stranding — a
paralyzed reject still never reaches `creature_turn()`, so nothing ticks — and it breaks CP5 C3,
whose script applies paralysis early and checks it many turns later, so a faster decay makes the
assertion vanish before the step. Reverted. The corpus proves what the tick is **not** (not
wound-triggered, not site-gated); it does not pin what it **is**, because the recorder's notion of
a "turn" is not the game's and the samples-per-decrement varies from 4 to 24.

**So the honest open item is narrower than it was, and it is one question, not two.** The
stranding itself is **fixed and now gated**: `status_tick()` runs on the paralysis reject, so a
player who keeps pressing directions decays `DS:26B` to zero and walks again, and
`tests/paralysis_decay.py` (CP5 C3e) proves it on a rig that sets the counter directly from a
captured DS mirror — `--dump-ds`, patch byte `0x26B`, `--load-ds`, press LEFT. The rig itself is
borrowed from CP5 C3d, which already does it for **arm** paralysis on the attack path and
already fixed that path's own stranding (`if (paral_arms == 0) creature_turn(...)` made arm
paralysis permanent for a player who kept swinging). What C3e closes is the legs half, the one
C3b could not gate because its Daemon fixture let the player walk away. With no monster in the
rig there is nothing to walk away from, so the assertion cannot pass by accident. The control was
run — with the tick removed the gate reports `still 15 means the reject path does not tick` and
`x=2, want 61`, which is the owner's bug reproduced mechanically.

What remains open is the **turn-semantics** question only: does a paralyzed reject advance the
creature sweep in the original? The corpus says monsters kept acting while the player was
pinned, which strongly suggests yes, but the byte chain that puts the sweep on a refused step
runs through `asm/0A02`, which is the *terrain* reject — the paralysis path prints a different
string and its route to `asm/0A33_cmd_done` has never been traced. The engine deliberately does
not call `creature_turn()` there, and says so at the call site. Settling it needs the breakpoint
count on `L_000e33` with a live fight, which is bigger than the "few minutes" it was two
estimates ago. The decrement rate per turn is no longer open: it is one per refused step, gated
by C3e.

**Process note worth keeping:** two confident, documented, high-confidence readings were wrong in
the same paragraph, and both were overturned by the owner playing the game. The rule that owner
observation outranks agent inference is not a courtesy — it is the cheapest bug detector this
project has.

---

## Enhanced mode — feedback effects (owner, 2026-10-26, scoped not built)

The owner's framing: an effect on the player should be **visible as an effect** — a hit animation,
a sound, a line in the status area, or a small red `-11 HP` popping over the HP readout. Original
mode stays byte-faithful, so all of that is enhanced-mode work, under the same split already
ruled for cross-sector attacks.

**Correction to an earlier version of this section.** It said the contact sound was "the one thing
that is NOT enhanced", citing `asm/5255` played twice per wound. That was wrong — `asm/5255` is an
XOR-decode routine and the loop it was attributed to is not the melee path. Whether the original
plays anything when you are hit is **open**, and E1 below is blocked on that trace rather than on a
decode. The instinct behind the paragraph still stands: if the original does have feedback for a
wound, it is owed to original mode and must not get lost by being filed under "enhanced effects".

For what enemies can actually do to the player, the authority is `asm/0AD3_combat_special_attack_dispatch.asm`
and its gate `asm/0AAA` — the dispatch table, the four specials with their resistances, and the
12.5% attempt roll are all recorded there and are not duplicated in this document on purpose.

| id | item | mode | what it needs | honest constraint |
| --- | --- | --- | --- | --- |
| **E1** | the wound tone and flash, faithful to the original | **original mode, not enhanced** | wire `seam_tone` at the wound site with the two divisors above, and a video-XOR equivalent for the flash. The routines are now named: `asm/5EAC`, `asm/5EF2`, `asm/5DA9_tone_player`, `asm/5255_wound_video_flash`, `asm/4C1E_video_xor_patch` | the tone is gated on the sound flag at `DS:54D`, so the engine must respect that too. The flash needs a video-XOR path the engine does not have — it composites text into a frame rather than XOR-ing CGA. **Owner ruled a sound substitutes for a message, so the tone alone satisfies the rule**; the flash is the nicer half and can follow |
| **E2** | damage number pop (`-11`) over the HP field | enhanced | per-cell colour exists — `u2_vram_poke(row, col, ch, attr)` — so the pixels are not the problem | **colour is settled, timing is not.** Owner ruled 2026-10-26 that enhanced mode may go full colour, so the CGA 4-colour limit does not bind here and red is available. The remaining blocker is structural: the engine is **turn-driven** — frames advance on player input — so a transient pop needs an animation clock that does not exist yet. That clock is the real work item, not the colour |
| **E3** | a status-log line for a wound (`YOU ARE HIT FOR 11`) | enhanced | a `u2_feedback()` seam called at each effect site, emitting nothing in original mode | **the seam is built (2026-11-02): `game/feedback.c`, one function, original mode resolves to nothing, gated by CP5 E1's enhanced arm — first caller is the swamp (`[swamp -5 HP]`). The wound line itself is still parked: the damage amount is a BCD value from the same subtract that is already computed, so the number is available. The wording is invented — it must be visibly engine-authored, not dressed up as game text, which is why the bracketed lower-case convention is written into feedback.h |
| **E4** | hit animation on the monster sprite | enhanced | a per-cell or per-tile flash on the creature's cell | same turn-driven problem as E2 |
| **E5** | the same treatment for the other silent effects: gold change, item gain/loss, paralysis, stat change, death | enhanced | audit each against "Silent player effects" and give each a `u2_feedback()` call | several of these are currently silent because the original is silent, which is fine in original mode and is exactly what enhanced mode is for |
| **E6** | the melee **range asymmetry**: a creature can hit you from a diagonal, you cannot hit it back | enhanced | owner ruled 2026-10-26 to park this here rather than "fix" it, because the bytes say the original is lopsided and original mode must stay that way | **measured, not inferred.** A creature's contact test is Chebyshev: `asm/0B25/0B39` stages the unit step toward the player — which for diagonal adjacency *is* the diagonal — and a candidate cell equal to the player routes to `asm/0B4D`, swing instead of step. The player's `cmd_A` prints `ATTACK--` and awaits a **direction**, then `asm/23E6_find_creature_at_cursor` scans the single cell at `player + delta` — one of four orthogonal cells. So a diagonally adjacent monster is in range against you and out of range for you. Two candidate fixes, both engine-authored: let ATTACK accept a diagonal aim, or make a diagonally-adjacent creature close to an orthogonal cell instead of swinging. **Pick one; do not do both** — doing both silently removes the asymmetry instead of softening it |

**The design decision worth making before any of it is built:** put the whole thing behind **one**
seam, called at the effect site, that resolves to nothing in original mode. If feedback is instead
sprinkled as `if (ctx->fidelity == U2_FIDELITY_ENHANCED)` at each call site, the two modes diverge
in twenty places and the CP2 differential stops meaning anything. One seam, one place to audit.

**Deferred by the owner on 2026-10-26: do not start E2–E5 now.** They are recorded so the ideas are
not lost, not queued. E1 is the only one on the critical path, because it is owed to original mode.
E6 was added the same day, also parked, and it is a ruling rather than a task: the asymmetry is
faithful and stays faithful until enhanced mode takes it.

**Partially un-deferred by the owner on 2026-11-02.** E1 is built: the ring flashes on the
victim's cell (player on a wound, creature on a landed swing) with the `asm/5EF2` tone as
its timer, gated by CP5 E2. The E3 seam is built as `game/feedback.c` and wired to the
swamp announcement (`[swamp -5 HP]`, enhanced only), gated by CP5 E1's enhanced arm —
which also gained the **enhanced swamp ring**: enhanced mode flashes the wound ring on
the player's cell when the swamp bites, an enhancement the owner asked for the same day
the original was proven NOT to flash the swamp (captures/spice86/swamp-flash-20261102);
original mode stays silent and flashless, and both directions are asserted by CP5 E1.
E2 (the floating damage number) and E4 (sprite hit animation) stay parked — the animation
clock they need is still the blocker; E5's remaining effects (gold, items, stats) stay
parked too, now with a seam to slot into.

**Claimed and retracted the same day:** an earlier note here said the original wraps its item lists
about three columns earlier than the engine, and inferred from that that `asm/4568`'s right margin
is not the `0x26` the engine uses. It is. `asm/456B` does `movb $0x26,0x31`, which is exactly what
the engine sets. The "three columns" came from counting characters off a 360x400 screenshot, which
is the eyeball error `AGENTS.md` § 5 exists to prevent. The band collision was real and is fixed;
the wrap difference is measurement noise and is not a defect.

---

## Silent player effects (owner rule 2026-10-26)

**The rule is in `AGENTS.md` § 3: every action by the player and every effect on the player must
produce a message.** This section is the audit against it, with the evidence grade per row and the
routine that has not been read where the grade is not `done`. It came out of a live noVNC session:
HP fell 9999 → 9952 while a monster stood adjacent, and the log showed nothing but `CMD: PASS`.

| effect on / by the player | engine today | grade | what closes it |
| --- | --- | --- | --- |
| monster melee wound | **prints nothing** | **answered: the original gives a tone and a screen flash, no text** | the wound loop is runtime `0x0BB2..0x0BCF`, driven by `DS:20` (contacts) and `DS:21` (hits), both set upstream at `0x0B4D`/`0x0B79`. Per iteration it calls `0x5EAC` (a tone, divisor `0x300`, about 1.5 kHz) and, once hits remain, `0x5255` twice with `0x5EF2` (a tone, divisor `0x800`, about 580 Hz) between them. `asm/5255` → `asm/4C1E` **XORs a pattern table into CGA video** — the segment table it loads holds `0xB8C8`, `0xBAC8`, `0xB8CD`, `0xBACD`…, all inside `0xB800`. Calling an XOR twice restores the screen, which is exactly why it is called twice. So the feedback is **sound plus a flash**, and the absence of text is real. **Wired 2026-11-02 (E1, CP5 E2)**: `game/flash.c` XORs the generated ring onto the victim's cell, sounds the `asm/5EF2` tone (divisor from the bytes, duration from the capture), XORs back; hooks at the wound application in `creature_turn` and the landed swing in `u2_overworld_attack_dir`. The `asm/5EAC` tone is NOT wired — divisor proven, duration neither decoded nor measured, and a guessed tone is worse than none; the `DS:54D` sound flag is not modelled (backend choice is the user's knob). **Upgraded 2026-11-02 from "never observed": the loop executed and the flash was caught on video-memory frames — `captures/spice86/wound-flash-20261102` (white disc + inverted player at screen centre, ~150-180 ms, mask matches the table at runtime 0x5288 record for record). Owner's both-directions claim **resolved the same day**: the ring marks the hit unit's cell — frame 00257 of the same capture shows the identical ring five scanlines above the player when the player's attack lands on the orc. E1's wiring must therefore hook both the wound path and the player's landed-attack path, each drawing at the victim's cell |
| force-field damage |**not wired at all** — `u2_combat_force_field` is decoded (`asm/24B5`, −1000 HP, ring protects) but only the test calls it | **VIOLATION, different kind** | needs the tile trigger wired into the step path, then its message comes with it. Do not report this as a missing message; it is a missing effect |
| player death | prints | ok | `asm/0C82` announcement, already in `u2_death` |
| paralysis on a refused step | prints `--PARALYZED!` | ok | CP5 C3 asserts it |
| per-step food drain | silent | **needs a citation, not a fix** | the drain is measured (`G2`, 19 per step on foot) and no message was seen in that capture, so silence is probably faithful — but the rule requires the comment at the mutation site to say so and cite the capture. Until it does, this row stays open |
| player commands (A–Z) | every implemented handler prints | ok | the CP5 dispatch gate asserts a distinct string per letter, which is what makes silence in a command impossible to miss |

**Two live-session bugs this audit turned up, both reproduced headless:**

1. **The Z page overflows into the status band.** With a character owning many items the `ITEMS:`
   list runs from row 18 past row 19 and overwrites the band — `H.P.`, `FOOD` and `EXP.` vanish
   and `GOLD=` collides mid-row. The lists also wrap mid-word (`B` / `OWS-01` for BOWS).
   The CP5 gate missed it because it asserts `A LEVEL` at the top of the page and never looked at
   the bottom, and the test character owns nothing. Fix needs `asm/4568`'s real row budget.
2. **The silent melee wound above**, seen directly: a monster adjacent, HP dropping, no message.

---

## `--dev` — the sanctioned home for development affordances (decided 2026-10-26, owner)

**The rule: `--dev` may add observation and an exit path. It may never change game behaviour.**
Not a damage number, not a spawn, not a message, not which tile is passable. Anything that would
alter play is a fidelity question and belongs in a decision about fidelity, not behind this flag.

Why it exists. The engine had two behaviours nobody asked for and the game does not have: the
overworld loop broke on Escape outright, and `cmd_Z` returned a quit code on Escape. Both were
development conveniences for `--play-map`, and both read as fidelity because there was nowhere
legitimate for a convenience to live. The owner's response to that was the useful one — give it a
flag rather than argue about each instance.

The shape:

- `ctx.dev`, set by `--dev`, off by default. Without it the engine implements the game and nothing
  else, and that is what every test and the CP2 baseline run against.
- **Independent of `--play-map` on purpose.** Entering the world directly is not the same as
  accepting development keys; implying one from the other would make every play-map test silently
  run under different rules.
- Dev behaviour is checked in the loop or the seam, **never in a command handler**. A handler that
  can end a session is the specific mistake being undone, and it is why the handler cannot be the
  place a quit lives.
- First tenant is **Ctrl-Q (`0x11`)**, chosen by the owner because the game does not use it. That
  is confirmed rather than trusted: nothing in `asm/` compares against `0x11`, and for the record
  nothing compares against `0x1b` either, which is the same evidence that said Escape was never
  special.

What enforces it, all in `run_cp5.sh`, all behavioural rather than greps because `--dump-each`
makes "did the run survive this key" countable: Escape is an ordinary key in the overworld in
both modes; Ctrl-Q is inert without `--dev` and ends the run with it; and no command handler can
end the session in any mode. The `--dev` leak control was run — dropping the `ctx->dev` test made
"Ctrl-Q is inert without --dev" go red, then restored.

---

## Z page: a full-screen overlay that leaves no trace (owner-found, 2026-10-26)

Two halves, and the second one is the blocker.

**Half 1, fixed and committed:** the Z page drew over the map instead of replacing it. Cause is the
compositor: `u2_overworld_draw` owns pixel rows 0..159 for the map window and composites only vram
rows `U2_MSG_TOP`..24 into the frame, so a full-screen text page had its top twenty rows painted
over by the tile view. Fixed by having `cmd_Z` compose its own whole-grid frame (black, all
`U2_ROWS`, no map) before it blocks on the key. Verified by the owner from the SDL build.

The mid-word wrapping that remains on the page is **not** a defect. The original wraps at 40 columns
in exactly the same places — `AXES-01 B`/`OWS-01`, `TRI-L`/`ITHIUMS-01`, `GREEN GE`/`MS-01` — see
`captures/spice86/z-page-maxed/Z-maxed-G23MAX.png` and its `readings.md`. Left alone on purpose.

**Half 2, fixed:** after dismissing Z the page was not taken back down. `u2_overworld_draw` composites
vram rows 20..24 and never clears them, so the tail of the ITEMS list stayed printed over the status
band and the band itself vanished. Owner's screenshot showed `UMS-01 PASS` / `TRI-LITHI` stuck under
the map with no `H.P.` line anywhere.

The fix is an **overlay, not an edit**. `cmd_Z` snapshots `ctx->vram` (a plain struct: fixed cell
array plus cursor and flags, so a struct copy is the whole state), draws the page, and on a real
keypress puts the snapshot back. Nothing the game still needs was ever written, so there is nothing
to reconstruct — the map and the command band are untainted by construction. The cursor comes back
with the snapshot, so the `CMD:` prompt resumes where it was interrupted.

The restore fires only on a real keypress. `seam_key_wait` returning 0 means "scripted input
exhausted", which has no counterpart in a game that blocks; tearing the page down on that condition
made it vanish in the same command that drew it.

**The observability problem dissolved rather than being solved.** The earlier note here claimed the Z
page could not be observed because it exists inside one command and `--dump-each` only catches turn
boundaries — which is true, and was the wrong conclusion. Two probes replaced it:

* Send `Z` with **no** dismiss key. The original blocks with the page on screen, so an undismissed Z
  leaves the page as the final state, and the existing "no status band" gate keeps its teeth.
* Assert **no trace** by comparison: the final screen of `Z`, dismiss, `S` must be byte-identical to
  the final screen of `S` alone. That is the property the owner asked for, stated directly.

Both are gated in CP5 and both were run as controls. The first text-based version of the no-trace gate
was worthless — with the restore removed the leftover shows as the page still being up, not as stray
item text, so grepping for item names passed a broken build. The diff gate goes non-empty in exactly
that case.

**Closed as a defect, still open as a capability (2026-11-02).** The recorded cause
was wrong: backend selection does *not* run before argument parsing — `main()`
parses everything, then calls `wire_backends()`. The real reason `--sdl --script`
did nothing is simpler: no interactive backend reads `script_path`, so the flag was
dropped in silence and the run waited for a human who was never coming. That
silence is now a refusal (`--script` requires `--headless`, gated in CP0), and
`--renderer=sdl|terminal` replaced the `--sdl` special case with a build-dependent
default, so the wrong flag on the wrong binary says so instead of looking like a
broken window.

What remains genuinely open is the capability the note was reaching for: driving
the **SDL renderer** from a script, which would let a pixel bug be gated in CI
instead of resting on a human screenshot. `--headless` forces the null renderer,
so the two have never been combinable. `--dump-screen-at N` was built to help here
and broke sector loading for a reason never found; it was dropped, not diagnosed,
and should not be re-introduced blind. The cheap half is now gated anyway: CP4
proves the flagless SDL build presents a real frame under `SDL_VIDEODRIVER=dummy`,
which is the regression guard for "the default silently stopped being the window".


## Invariants (checked at every checkpoint)

| Check | Command | CP0 result |
| --- | --- | --- |
| Per-function byte proof | `python3 tools/check_func.py <SEG>` | 1299/1299 green (sample re-run + CI) |
| Mechanical rebuild | `bash tools/assemble_all.sh` | byte-identical |
| Clean-tree rebuild | `bash tools/assemble_clean.sh` | 1299/1299 cleaned, 37,344 B identical |
| Original untouched | md5 `fe884d5117971073ac8097cc7a6aca25` | unchanged |
| Engine test | `make -C engine test` | CP0 11/11, CP1 9/9, CP2 12/12 PASS |

---

## TODO: enhanced-mode cheat codes behind `cmd_Y` (owner, 2026-10-29, scoped not built)

The owner asked for a Doom-style cheat system, entered through `Y`, as a nod to id. It is a good
carrier for a reason that is not nostalgia: `asm/4542` already reads freeform text until Enter and
**writes nothing at all**, so the text-entry plumbing is something the game ships rather than
something the engine would invent. `cmd_Y` is implemented and gated as of the same day; the cheat
layer is not, and nothing below is built.

**It must not live behind `--dev`.** That flag's ruling is explicit — *"`--dev` may add observation
and an exit path. It may never change game behaviour. Not a damage number, not a spawn, not a
message, not which tile is passable."* That sentence rules out the two most desirable codes by
name, in the same terms, and `--dev` exists precisely because unrequested behaviour once masqueraded
as fidelity. Cheats are the loudest possible version of that mistake.

**So it needs its own flag, off by default.** `--enhanced` is not the right home either as things
stand: enhanced is currently "same rules and state as the original, but better presented", and a
god mode is not better presentation, it is new gameplay. Naming that honestly beats stretching the
existing category, and off-by-default keeps every gate and the CP2 byte-comparison baseline exactly
where they are.

**The mapping, with honest verdicts.** Doom codes, and what Ultima II can actually back:

| code | U2 equivalent | verdict |
| --- | --- | --- |
| `idclev##` | warp to sector `##` (00–99) | clean, and the most useful thing on this list — **but see the `DS:49` caution below** |
| `idclip` | `u2_mode_passable()` always accepts | clean; one function, already covered by 12 mode cases in CP5 P1 |
| `iddqd` | HP to max + a guard at the damage site | clean; HP is 2-byte BCD so "100%" is 9999, and the death test is `damage > hp`, not `>=` |
| `idmypos` | print sector + x,y in hex | clean, **and it is pure observation, so it is legal under `--dev` today with no new ruling** |
| `idbeholdA` | grant the HELM so `cmd_V` shows the world map | clean — the automap analogue already exists |
| `idbeholdS` | strength boost | clean, but stats wrap above 99 (proven, no ceiling guard on any add site) |
| `idkfa` / `idfa` | fill weapon/armour slots, POWER armour, max gold | partial — U2 has **no ammo and no keys**, so half the code has nothing to map onto |
| `idchoppers` | ready the best weapon | **blocked**: `cmd_R` (ready weapon) is still a `NULL` slot |
| `iddt` | bypass `cmd_V`'s `sector_second < 4` gate | partial |
| `idbeholdR / I / L / V` | radiation suit / invisibility / light / invulnerability | mostly invention. `V` is `iddqd`'s second half; `L` is the dungeon LIGHT spell, which `AGENTS.md` § 3 parks |
| `idspispopd` | duplicate of `idclip` | redundant — Doom needed two because they were two engines |
| `idmus##` | — | does not map. The only audio seam is `speaker_tone`; there are no music tracks to select |

**Rules that bind whoever builds it.**

- Original mode's `cmd_Y` stays exactly as the bytes have it: echo, no writes. The cheat parser is
  additive and reachable only with the flag on. Gateable directly — assert Y mutates nothing in
  original mode, which `cmd_yell.py` already does for the plain case.
- **Every cheat must announce itself.** Owner rule, 2026-10-26: an action or an effect with no
  message is a defect. And per the enhanced-effects precedent, the wording must be **visibly
  engine-authored, not dressed up as game text** — `HIT POINTS FULL — CHEAT` is right, a string in
  the game's own voice would be wrong.
- Echo through `char_print_wrap` (`L_0055f4`), the seam the original uses, so long input wraps the
  same way.
- Licence: the code *names* are a short functional homage and fine to keep verbatim; do not copy
  id's manual prose into the repo, and author the table and every message ourselves.

**`idclev` touches an open question.** Sector warping writes `DS:49`, and that byte has the G45
collision — `asm/3546` reads it as the era index while `asm/7030` writes it from the world byte.
A warp should set the sector digits and say so in a comment, not quietly invent world semantics
nobody has pinned down.

**A false promise to deflate before it matters.** `idclip` and `idclev` look like they would unblock
the capture queue — G35's tavern, G39's rocket, G40's horse beside shallows. They would not. Those
captures need the **original** binary to walk somewhere, and cheats exist only in our engine. The
original is served by `tools/u2_edit.py setpos` and `--player` seeding, which already warp from
outside. So this buys playability and engine-test convenience, not evidence — a good reason to build
it, and not a reason to file it against the queue.

**Suggested order:** `idmypos` under `--dev` (no ruling needed, immediate debugging value) → the
flag decision → `idclev` and `idclip` → the stat and item codes → `idchoppers` whenever `cmd_R`
lands.

---

## TODO: internationalisation (i18n)

**Status: not started. Recorded so it is designed once, deliberately, rather than
bolted on after the text layer has hardened.**

The engine currently has no language axis at all. Text reaches the screen as
compile-time `#define` string literals in `engine/content/gen_content.h`, generated
from the original executable by the Makefile and committed so a build without the
exe is still a complete game. That is simultaneously the good news
and the obstacle.

**Good news: a seam already exists.** Every string is reached through a `U2_S_*`
name (`U2_S_CMD_PROMPT`, `U2_S_STATUS_HP`, ...) rather than an inline literal at the
call site. Callers do not care where the text comes from, so replacing the macro
layer with a runtime table is a contained change, not a sweep.

**The obstacle is that those strings are game content.** They are extracted from a
copyrighted binary, they are English-only, and they are only kept out of git by being
build output. A translation catalogue cannot simply be a translation of them without
saying clearly which parts are original text and which are engine-authored.

Requirements when this is picked up:

1. **Runtime string table, not macros.** `U2_S_FOO` becomes an id resolved through a
   lookup, so the language can be chosen at launch (`--lang`) instead of at build
   time. Keep the existing names as the ids; they already describe intent.
2. **Separate the two provenances.** Strings extracted from the binary and
   engine-authored strings (e.g. `[engine] character on disk`, deviation D13) are
   different things and must be catalogued separately, or a translation project will
   inherit copyrighted text without knowing it.
3. **The text grid is fixed and narrow.** 40 columns, 8x8 cells, and several lines
   are positioned by absolute row and a right margin (`right_margin` 0x28 for the
   status block, 0x1d after it). German and Finnish will not fit where English does.
   Layout must be derived from the string, not assumed from the English width.
4. **The font is ASCII-only.** `core/font8x8_basic.h` is asserted to cover
   U+0000..U+007F. Accented Latin needs new glyphs; any non-Latin script needs a
   font and a wider cell model. This is the largest single cost and should be
   scoped before the rest.
5. **Numbers are packed BCD, not text.** HP, FOOD, EXP and GOLD are drawn from
   packed decimal bytes. Locale digit or grouping rules have nowhere to apply until
   that formatting is separated from the storage format.
6. **Do not touch the reconstruction.** The `asm/` tree holds the original inline
   strings because it is a byte-exact reconstruction; it is not the place for a
   language layer. i18n belongs entirely to the engine.

One concrete oddity worth remembering: the original map font has **no Q glyph**
(art `30`, where Q would fall, is a structural tile). Any translation that renders
through map tiles inherits that gap, so a language needing Q must route around it
rather than assume the glyph exists.

---

## TODO: VNC desktop container for remote watching

**Status: parked, and explicitly not blocking.** Recorded because it is genuinely
worth having, not because anything is waiting on it.

**Do not build this to unblock a capture.** Headless input and headless screenshots
already work through Spice86's MCP endpoint — see `tools/spice86_type.py` and
`spice86_start.sh --auto-play` in `docs/spice86-capture.md`. The idea for this
container came out of assuming a capture needed a display, and that assumption was
wrong.

**Amended 2026-10-22:** this is now *more* true, not less. `tools/spice86_screenshot.py`
captures the screen headlessly via the MCP `screenshot` tool, so an agent can see the
game without any display at all — which was the last reason this container seemed urgent.
What it does **not** provide is a human driving interactively. If the owner wants to play
and narrate, that still needs a window somewhere; but no agent task is waiting on it.

**What it is.** A podman container providing a virtual X display plus VNC, so the
owner can watch a live run from anywhere on the LAN, and so the headed-only tools
(`tools/vga_ring.py`, `tools/screenshot_diff.py`, `tools/spice86_keys.py`) become
usable from an agent shell, which currently has no `DISPLAY`, no `WAYLAND_DISPLAY`
and no `Xvfb`.

**Shape.**

| piece | plan |
| --- | --- |
| base image | `docker.io/library/ubuntu:24.04` — already present on `ai` |
| packages | `xvfb x11vnc novnc websockify x11-utils xdotool` |
| display | `Xvfb :99 -screen 0 1024x768x8`; host runs Spice86 with `DISPLAY=127.0.0.1:99` |
| networking | `--network host`, so 6099 (X), 5900 (VNC) and 6080 (noVNC web) land on the host directly |
| viewer | `x11vnc -display :99 -forever -shared -rfbauth` and `websockify 6080 localhost:5900`, browser at `http://ai.lan:6080/vnc.html` |
| X auth | X over TCP needs the server bound to `127.0.0.1` and an explicit `xhost`/`-auth` decision; do not reach for `-ac` |

**Constraints to honour when it gets built.**

1. **Project containers run ROOTLESS on `ai`** — plain `podman`, no sudo. Corrected
   2026-10-29: this line used to read "`sudo podman` is required", which was never true of
   project work and is what put the two `up.sh` launchers on `sudo -n podman`. Root's store
   and the rootless store are **separate image and container stores**, so an image built once
   with `sudo podman` is invisible to rootless `podman` and gets rebuilt rather than reused.
   `sudo podman` is only for root's system services (litellm, traefik, open-webui and
   friends). Quadlet / systemd units for the always-on set are root's and still need sudo.
2. **`0.0.0.0` exposure needs a password at minimum.** VNC auth over plain HTTP is
   weak; on a LAN it is a judgement call, so make it deliberately and record it, and
   prefer an SSH tunnel (`ssh -L 5900:localhost:5900 ai`) as the default recipe with
   the open listener as the opt-in.
3. **Never put game files in the image.** The container holds a display server only;
   the emulator and its run copy stay on the host under `tmp/`, per the usual rule
   that the game rewrites its own `player`.
4. If it becomes a long-lived service, do it as a **quadlet `.container`** — and
   remember `sudo systemctl daemon-reload` after any unit edit, then stop, `podman
   rm -f`, and start, or the old generated unit is cached and the change silently
   does nothing.
5. Keep it out of the reconstruction's gates. It is a viewing convenience; nothing in
   `make test` or `assemble_clean.sh` should ever depend on it.

## TODO: research the U1 Unity rebuild for transferable findings (opened 2026-10-23, owner-found)

**Target:** <https://u1unity.archiveofmyth.com/> — a fan project rebuilding the **1987 DOS
remake of Ultima I** in Unity, worked on in the open (a devlog with dated per-system entries,
and a GitHub repo referenced as `hoyty1/ultima-unified-engine`). It is not Ultima II, but the
overlap is real and the owner asked for a thorough pass rather than a glance.

**Second lead, unresolved (owner-reported, 2026-10-23).** A post reading *"Build 1.3.2 of the
Ultimate Ultima Engine for your testing pleasure. The last one had some bugs."* — the owner is
not sure it is the same project. Checked while logging this: the GitHub repo the U1 Unity
devlog merges from (`hoyty1/ultima-unified-engine`) returns **404** — private, renamed or
deleted — and no release numbered 1.3.2 appears in the public devlog. **First task of the
research pass: establish whether "Ultimate Ultima Engine" is this author's engine under its
repo name ("Ultima Unified Engine" — the initials match) or a different project entirely.**

**Verified in the meantime.** The same author distributes as **Ultima 1 Unity** on itch
(<https://archiveofmyth.itch.io/ultima-unity>) with a dated devlog of release notes
(<https://archiveofmyth.itch.io/ultima-unity/devlog>). Three entries are directly relevant
here and worth pulling into the research pass:

* *"Added sound effect set from Ultima II, III, and IV"* (2026-09-13) — confirms they parse
  U2 data files, so their format reading is an independent second opinion on something this
  project derived alone.
* *"Monsters will no longer despawn while chasing you around the globe"* (2026-08-09) — U1
  monster pursuit and lifecycle, the same family of question § 16 just settled for U2.
* *"Added Gameplay option for original starvation instant death"* (2026-09-21) — they
  reproduced the starvation edge case this project proved as a BCD underflow
  (`asm/0C62` zeroing, the fresh character dying on the 4001st PASS). If they modelled it as
  a mode toggle rather than a wrap, that is a useful contrast for CP11's enhanced rules.

**Why it is worth the time.** This is the same discipline as this project, applied one game
earlier: they describe pulling apart original DOS binaries, decoding tile and map formats
(**EGA row-planar tiles, nibble-packed overworld maps, column-major town data**), and
decompiling **combat formulas from the original EXE where possible**, with the guesswork
labelled as such. They also state plainly where they approximated — spawn tables "from
observed behavior since the exact original logic isn't fully understood", space behaviour as
"best guess from SPACE.EXE, close but probably not byte-exact". That candour is what makes a
sibling project usable: you can tell their facts from their guesses. They additionally import
**Ultima II art and sound effects**, so they have already decoded some U2 data structures
from the other side.

**What to look for, in priority order.**

1. **Combat formulas and turn structure.** Anything they publish on hit/miss rolls, damage
   rolls, or monster turn logic, even for U1 — the family is close and § 15/§ 16 just
   established that U2's hostility is a provoked state, so a second data point on how these
   games roll is worth having.
2. **U2 asset import paths.** If they document how they read Ultima II tiles or SFX, that is
   a second, independent reading of formats this project decoded from its own bytes — useful
   as corroboration and as a place a mistake could show up.
3. **Format techniques**: row-planar tile decoding, nibble-packed maps, column-major town
   layout. Compare against `docs/map-format.md` and the tile work; a technique that worked
   for them may explain a U2 structure that has been resisting.
4. **Their community-references page.** They credit the Ultima Dragons Discord and community
   docs for sprites, mechanics and quirks. Those sources may hold U2 material of the same
   grade as `bestiary/` — third-party, useful, and not authoritative here.
5. **The itch release notes as a changelog of their reverse-engineering.** The devlog is
   per-system and dated, so it can be mined for statements of fact about formats and
   mechanics, not just feature lists.
6. **Process ideas**: how they structure a public devlog per system, and how they mark
   best-effort reconstructions. This project's equivalent is the `# confidence:` header and
   the evidence grades; worth seeing whether their presentation makes the state easier to
   follow, since the handoff keeps having to explain where the certainty lives.

**Standing cautions, which the isolation and cleanroom rules already cover.**

* **Third-party data is a hypothesis the bytes get to veto** (`AGENTS.md` § 3). Nothing from
  this site promotes into `asm/` or into a claim about this binary without being re-derived
  from `original/` and `captures/` here. Same rule as `bestiary/`.
* **The cleanroom wall is not negotiable.** They hold Ultima II art. Anyone drawing
  substitute tiles must not read this site or anything it links — the only legal input for a
  redraw stays `docs/tile-art-spec.md`. Note this explicitly in the research write-up so the
  next agent does not treat "researched the U1 site" as harmless before an art task.
* **Different game, different binary.** U1's 1987 DOS remake is not U2's 1983 engine;
  conclusions do not transfer, methods do. That is the same boundary
  `docs/harness-portability.md` draws for generic tooling.

**Suggested shape of the deliverable:** one new document, `docs/external-u1-unity.md`, with a
table of claim → what they assert → whether this project's bytes agree → action taken. Empty
is a fine outcome; the point is a recorded pass rather than an unrecorded glance. Do it as its
own session, not as a side trip inside a checkpoint.

## TODO: headless podman container to play the SDL build over noVNC (opened 2026-10-23, owner) — **DONE 2026-10-27**

**Wanted:** the engine's SDL build (`make sdl` -> `u2-sdl`) running headless in a podman
container on `ai`, reachable from a browser over noVNC — the same shape as the Spice86
watching setup, but for *playing our engine* rather than watching the original.

**Do not duplicate the existing section.** `## TODO: VNC desktop container for remote
watching` above already works out the display plumbing (Xvfb, `x11vnc -forever -shared`,
`websockify 6080 -> 5900`, browser at `http://ai.lan:6080/vnc.html`) and the constraints that
apply to any container on this host: project containers run **rootless** (`podman`, no sudo;
this used to say `sudo podman` is required — corrected 2026-10-29, and the two `up.sh`
launchers were changed with it), quadlet edits need `systemctl daemon-reload` plus a stop /
`podman rm -f` / start cycle, `0.0.0.0` exposure needs a password at minimum with an SSH
tunnel as the default recipe, and **game files never go in the image** — content is mounted
read-only and passed with `--data-dir`. Read that section first; the delta here is only
"the thing inside the container is our SDL binary, not the emulator".

**What is genuinely new in this one.**

1. **The SDL path is already proven headless.** CP4 asserts
   `headless SDL: dummy driver presents a 320x200 frame` using `SDL_VIDEODRIVER=dummy`, so
   the binary runs with no display at all. That is the right mode for *tests and frame
   capture*, and the wrong mode for a human — `dummy` presents nothing to capture. The
   container therefore needs a real X server (Xvfb) and `SDL_VIDEODRIVER=x11`, with `DISPLAY`
   pointed at it, and `x11vnc` bound to that same display.
2. **Input has to come back.** Watching Spice86 is one-way; playing needs keyboard events
   delivered into the container. noVNC gives that, but it must be wired to the display the
   SDL window is on, and the engine reads keys through `src/platform/input_sdl.c`, so
   focus-follows-mouse and any window-manager surprises inside the container are live
   gameplay bugs, not cosmetics. Budget for that being the part that takes longest.
3. **Audio.** The build has `audio_beep` / `audio_null` / `audio_sdl`; a container with no
   sound server will pick `audio_null` or fail to open a device. Decide deliberately which,
   and record it — silent-by-default is fine for a remote session but should be a choice.
4. **It must stay out of the gates.** Nothing in `make test` or `assemble_clean.sh` may
   depend on the container existing. The CP4 dummy-driver assertion already covers the SDL
   path for CI; this container is a convenience for a human with a browser.
5. **Save state lives on the host.** The engine writes the `player` record into the content
   directory; if that is mounted read-only (as it should be for the original game files) the
   session needs a separate writable path for its save, or `Q` will fail in a confusing way.
   Decide the layout before building, and make the failure legible if the path is not
   writable.

**Suggested shape:** one quadlet `.container` under a project-local directory, image built
from a committed `Containerfile` (engine binary + Xvfb + x11vnc + websockify + a minimal
window manager), content mounted read-only, save path mounted read-write, ports 6080 (and
5900 only if not tunneled). Keep the `Containerfile` and the run recipe in the repo; keep the
game files out of it, per the licensing rules.

## The `--debug-path` segfault: not a code bug, a stale build

Reported and then resolved the same session, so the shape is worth keeping.

`--debug-path` on the SDL build dumped core in `snprintf` inside `main()`. Three
different builds disagreed with each other, which is what made it look like undefined
behaviour. It was not. The tree had a stale `build/gen_content.h`, and the same staleness
was separately making the container fail with `no executable and no cached tiles` — two
symptoms, one cause, and only the second one looked like a build problem.

`make clean` removed both. Clean builds at `-O0`, `-O2` and `-O2 -g` all run and write
their turn files; the container is healthy again with the feature enabled.

What is NOT explained, and is left as a fact rather than a story: the mechanism by which
the stale generated header produced a bad pointer in a `snprintf` argument. The
correlation is solid, the causal chain is not, and if it ever comes back the first thing
to do is `make clean` before believing anything else.

Two self-inflicted traps found on the way, both worth remembering:

- `make ... | head -3` sends SIGPIPE to make and kills the build partway, leaving no
  binary and a confusing `rc=127`. Filter make's output with `grep -v` or read the tail,
  never truncate the pipeline that produces the artefact.
- `gdb -batch -ex run` on an interactive engine hangs forever, because the inferior never
  exits and gdb waits for it to. Always wrap it in `timeout`.

## Travel modes — implemented, and stopped at a defect (2026-10-29)

`docs/handoff-vehicle-state-machine.md` was the work order. W1 (BOARD), W2 (per-mode
passability), W3 (per-mode food) and W4/W5 (`cmd_L`'s plane and rocket branches) are
implemented, and the findings that changed the rules are in `docs/passability-findings.md`
F14's second correction and F15. What is wired:

| piece | where | gate |
| --- | --- | --- |
| mode dispatch for passability | `u2_mode_passable()` in `overworld.c` | CP5 P1, 12 new mode cases |
| per-mode food (19 / 38 / 38 / 0 / 0) | `u2_overworld_step_food()` | CP5 V1 |
| `cmd_B` BOARD, five item gates, `BOARD WHAT?` | `commands.c` | CP5 V1 |
| `cmd_L` LAUNCH: plane flight + land, rocket flight + crash | `commands.c` | **not gated** |
| the tile BOARD leaves behind (grass / water) | `board_vehicle()` via `u2_world_terrain_put()` | CP5 V1, gated 2026-10-29 once the store defect closed |

**`cmd_L` is deliberately ungated, and that has to be said rather than left implicit.** Its
plane branch is observed in `flight-launched-live` and `airport32` (launch, `LAND PLANE.`,
and `LAND--NOT HERE!!!!!` all in the executed print set), but the flight loop is a
non-blocking poll loop and the engine's scripted input gives one poll per `WAIT` token, which
is a harness mapping rather than a measured equivalence. The rocket branch has never run in
any capture at all — `DS:13 = 0x28` appears in zero harvest dumps. Gating a flight loop
against a cadence nobody measured would produce a green that means nothing; `docs/needs-gameplay.md`
G39 and G43 are the captures that would let it be gated honestly.

### CLOSED 2026-10-29: a dirty map was written to the MONX filename

`u2_world_store(ctx, w, is_map)` built its path from `w->found_prefix`, and `found_prefix`
was **one field shared by both loaders**. `u2_world_load` set it on every load, and the
overworld always loads the map and then the monsters, so by save time it read `monx`. A
store of the **map** therefore wrote 4096 bytes of map data to `monxNN`, and returned
`U2_WORLD_OK`. It compounded: once `monxNN` was 4096 bytes, a later monster store wrote 256
bytes and copied the "tail" from offset 256 of the corrupted file, so the map bytes stayed.

Reproduced at HEAD (`45395c9`, no vehicle code involved): seed a sector with a door at
`(3,0)`, give the character a key, `U` + `RIGHT` to pave the door, then `Q`. `monx00` went
from 384 bytes to 4096 and every creature in the sector was gone on the next load.

Why no gate saw it: `world_check.c` loaded only a map, so the shared field still said `mapx`
when it stored, and its round-trip assertions passed honestly. The defect needed a test that
loads **both** kinds before storing — which is what the game always does.

**The fix, and the order it was done in.** Control first: a both-kind case in
`tests/world_check.c` that loads map then monsters into one world, edits a tile, stores the
map, and asserts the map file carries the change *and* the monster file is untouched in size
and bytes. Run against the unfixed store it failed three assertions while `storing the map
reports OK` passed — which is the defect in one line. Then the fix: `found_prefix` is now
`map_prefix` / `mon_prefix`, set and read per kind, plus a guard in `u2_world_store` that
returns `U2_WORLD_ERROR` rather than writing if the prefix it was given does not belong to
the kind being stored. The guard is the point as much as the split is: it converts any future
recurrence from silent destruction of the player's creature data into a failed save.

The two diagnostic notes that printed `sector 31 via monx` were reading the same shared field
and so always named the monster file; they now print the map's prefix, which is what the
sentence was trying to say. `docs/monster-spawn.md` § 10's quote is updated.

**No player data was damaged here.** A scan of the repo found 5519 `monx` files all at 384
bytes and 5518 `mapx` files all at 4224 — the shipped sizes are 256 + a 128-byte tail and
4096 + 128, which is what the engine preserves on write-back. The only corrupted files were
the `/var/tmp/vt*/` scratch dirs this session created while diagnosing it. Anyone who has run
an older engine build and saved can still check: a `monx` that is not 384 bytes has been
overwritten, and `references/ultima2/` is the pristine copy to restore from.

### Also recorded, not acted on

- `u2_world_tile_put()` writes only the live grid, and `u2_monsters_restamp()` rebuilds that
  grid from `map_base` on every creature turn — so a terrain edit made with `tile_put` alone
  is silently undone the next time anything moves. `u2_world_terrain_put()` was added for
  edits that are meant to last, and BOARD uses it. The UNLOCK paving at `asm/435E` still uses
  `tile_put` and has the same latent problem; left alone for the same reason as the defect
  above, and named in `world.h`.
- `cmd_X` (`asm/44D4`) is now decoded as the dismount and is still unimplemented. It is a
  letter with execution evidence, so its `NULL` slot is a real gap; it is outside the vehicle
  handoff's letter list and needs its own ticket.

## Buying a horse changes the mode without BOARD — and it exposed an engine defect (2026-10-29)

Owner-played evidence, `captures/spice86/shop-window-transport`. A Wizard bought a horse from the
TRANSPORT shoppe and the state moved like this: `DS:13` 0x7A → **0x22 (HORSE)**, gold 9999 → 9967,
and **nothing else** — all 16 item slots unchanged, all nine spell counts unchanged, and the tile
under the player still `0x70` town floor.

So a purchased horse is neither an inventory item nor a map tile. **The transport shoppe is the one
route found where the travel mode changes without `cmd_B`.** BOARD consumes a tile and derives the
mode from the tile under the player; the shoppe skips the tile entirely and writes the mode. The
vehicle work modelled only BOARD, so this is a missing branch, not a detail.

**OPEN DEFECT — NOW FIXED, same day.** `overworld.c` set `ctx->state.player_stamp = 0x78`
unconditionally at world open. But `asm/080D` and `asm/44D4` both compute the on-foot stamp as
`0x78 + 2*pclass`, and G41 is now closed with a live sample: a class-2 Wizard read `DS:13 = 0x7A`
on foot. **Three of four classes were stamped wrong the moment the engine entered the world** —
cleric `0x7A`, wizard `0x7C`, thief `0x7E`. It went unnoticed because every capture and every
seeded test character so far was a Fighter, which is the artifact G41 suspected — and that is the
whole lesson: a constant that is right for the only class anyone ever tested reads as verified.

Fixed to `u2_foot_stamp(ctx->state.pclass)`, the same rule function `cmd_X` already used for the
dismount, so the expression now has one author in the engine. Gate: `tests/foot_stamp.py` (CP5 V5),
all four classes, with the control run — restoring the constant reports FIGHTER ok and the other
three `got 0x78`, which is the shape of how this hid. **CP2 is unaffected and still green**: every
fixture there is a Fighter, whose stamp is 0x78 either way, so the baseline concern raised when this
was recorded did not materialise.

Two prices came out of the same session and they split a decode in half — see
`asm/20AB_bcd_price_from_stats.asm`: the horse at 32 gp is exactly what that routine produces, and
the axe at 25 gp cannot be produced by it at all, so the weapons shoppe has a separate price path.

---

## The engine writes sector files the original never writes — FIXED (2026-10-29)

`site_enter()` and `site_leave()` in `engine/src/game/overworld.c` write the outgoing sector
back to disk — `u2_monsters_store()` unconditionally, and `u2_world_store()` for anything
marked dirty. The comment justifying it read `asm/22FD stores the sector being left`.

**Both halves of that are false.** `asm/22FD` is `mov 0x6d,%al ; or %al,%al ; je` — a test of
`boots_count` and a branch, which `state.h` independently records as the paralysis-save gate.
It is not a store and has no I/O in it; see its header. And the original writes **no** sector
file under any transition: measured live on 2026-10-29 across several town entries and exits,
`mapx31`, `monx31`, `mapx30` and `monx30` in the run dir were byte-identical to the pristine
copies, with only `player` changed (`docs/save-format.md` § 10).

So the engine persists state the game discards. Observable consequences, all of them wrong in
the same direction — the engine remembers a world the original forgets:

| situation | original | engine today |
| --- | --- | --- |
| open a door, leave, re-enter | door closed, grid re-read | door stays closed (grid re-read), but the paved `mapx` was written to disk first |
| wound a guard, leave, re-enter | guard at authored HP, back at his cell | HP and positions persisted, so he stays wounded and displaced |
| kill everything in a town, re-enter | town repopulates from `monxNN` | town stays cleared |

**The fix was a deletion, and the control went red first.** The two `u2_world_store()` calls
are now gated on `ctx->keep_site_state`; `u2_monsters_store()` stays, because it is an
in-memory DS→world sync and not a file write, and the save path needs it. Gate:
`engine/tests/site_state.py`, wired as **CP5 V2**. It seeds a village sector (ones digit 1),
dirties the map with BOARD, steps off the north edge, and asserts the sector files on disk are
untouched; the second case asserts `--enhanced --keep-site-state` *does* write them. Run
against the unfixed engine the first case failed exactly as predicted — `mapx01 on disk
untouched ... saw 0x8` — which is the proof that the gate can fail where it enforces.

**The owner's option.** `--keep-site-state` keeps town state across a transition, as asked for
on 2026-10-29, and it is **refused in original mode** with exit 2: the default has to stay the
measured behaviour or the CP2 byte-comparison baseline would be measuring something the game
never did. `u2_ctx.keep_site_state` is documented as enhanced-only in `ctx.h`, next to the
`auto_pass_polls` precedent.

**What was deliberately not touched, and was right not to be**: the save path (`Q`) still writes
a dirty sector, and that is byte-supported — `asm/2305` names `MONXFF` as well as `MAPXFF`.
Measured later the same evening in `captures/spice86/save-wilderness`: an outdoor `Q` writes
`player` **and** `mapx30` **and** `monx30`; a `Q` inside a town is refused with
`ONLY OUTDOORS!`; a `Q` outdoors while `DS:6D != 0` is refused with `ONLY ON EARTH!`. The
first version of tonight's finding — "the original writes nothing but the player record" — was
wrong for exactly one reason: the `maxout` test character holds boots, `DS:6D` reads 1, and
every save it attempted had been silently refusing. A refusal that prints a plausible sentence
is the most expensive kind of no-op. Docs: `docs/save-format.md` § 10, `docs/map-format.md`
§ 10, `asm/3DFA_offer_only_outdoors_earth.asm`.

The transition rule is unchanged by all of this and CP5 V2 still holds it: crossing a site edge
writes nothing. The world is not stateless — it is stateful outdoors and stateless indoors, and
the refusal messages are the design saying so.

**It had already damaged a game file.** `original/monx71` was found rewritten (mtime
2026-10-23 15:57, same second as `monx70`, monster coords and HP drifted) — a run of an older
engine build using `original/` as its data dir. Restored from `references/ultima2/` on
2026-10-29 with the owner's approval; the damaged copy is preserved at
`/var/tmp/u2-harvest-quarantine/monx71-damaged-20261028`. `original/` now matches the pristine
backup on every file. This is also why the `map`→`monx` prefix bug above mattered: it was the
same routine, and it is what made the damage silent.

### UNLOCK paving — rationale corrected, ticket still open

The earlier note here said the `asm/435E` paving "re-closes on restamp" and treated that as a
bug to fix by writing through to `map_base`. Tonight's measurement changes the reasoning, not
the conclusion: the original keeps an opening for the whole visit (~38 cells churned by
creature movement, both openings intact) and then drops it on reload. The engine's
`map_base` rebuild on a creature turn is therefore **more** destructive than the game, and
`terrain_put()` is the right call for the paving — but as a fix for an engine-invented
problem, not a reconstruction of original behaviour. Still open: `cmd_U`'s paving uses
`tile_put`, so an opened door in the engine vanishes on the next creature move. It is now a
one-line change with its own case in `site_state.py`, and it no longer interacts with
persistence, because nothing persists.

## The Q guards are implemented; enhanced mode saves anywhere (2026-10-29)

`cmd_Q`'s chain, read off the bytes at file 0x3FC7 and recorded in `asm/3DC7_cmd_Q.asm`, has
three guards in a fixed order: `DS:4A` (indoors) → `ONLY OUTDOORS!`, `DS:6D` → `ONLY ON
EARTH!`, `DS:13 < 0x78` (aboard a vehicle) → `ONLY ON FOOT!`, then `ONE MOMENT PLEASE!` and
the save. The engine had the first and had stubbed the other two as unreachable until CP6/CP7.
Both had become reachable — vehicles shipped the same week, and `DS:6D` is a live byte — so all
three are now wired to real state behind `u2_save_policy`, and `U2_SAVE_POLICY_ENHANCED`
drops them for `--enhanced`, which is the owner's "save anywhere".

Gate: `engine/tests/save_gate.py`, wired as **CP5 V3**, thirteen checks. Each case asserts the
refusal text *and* that the record was untouched, or that the save really landed and the entry
pair followed the live position. Two lessons the controls taught the hard way:

* the first version's "nothing was saved" checks passed **trivially**, because a save with no
  movement writes a byte-identical record — the entry pair only moves if the player did. Every
  case now walks a step first, or the negative assertion means nothing.
* making the on-foot guard real broke two things that had been leaning on it being a stub:
  `vehicle_board.py` observed the tile BOARD leaves by saving and reading it back, which the
  original cannot do aboard a vehicle (that case now saves under `--enhanced` and says why),
  and `save_q_check.c` hand-rolled a world with no on-foot stamp — a state the game cannot be
  in. Both were fixture problems, and the guard was right in each case.

Still open: the engine has no writer for `DS:6D`, so the Earth gate only fires from a save
record that carries a non-zero world — which is exactly the case the original refuses, but
space travel itself is CP7 work.

`DS:6D` is no longer a mystery, and the answer was embarrassing: **it is the world byte** (Earth
0, planets 1..9, deep space 0x0A), written by `asm/7030_space_collision` and copied into the
sector tens digit. The reason it looked like a boots counter for two years is that
`tools/u2_edit.py` had a `boots` field at that address and `maxout` wrote 1 into it — every
maxed test character was silently standing on planet 1 and could not save, and `state.h` named
the byte after our own tool's write. G44 opened and closed the same day; the field is now
`world`, the bogus `boots` field is gone, and `maxout` leaves the character on Earth. See
`docs/save-format.md` § 10.

## CAST slot 7 is MAGIC MISSILE, and its effect is site-side, not dungeon-only (2026-10-29)

`captures/spice86/cmd-c-magic-missile` — a Wizard in the sector-20 tower, driven headless, with
`tools/bp_read.py` stopping the CPU inside the target scan. This closes the "LOCATED but not
attributed" note in `asm/3139_cast_spell_flow.asm` and it is the evidence behind decision **D14**
(whether `cmd_C` ships now), which is still the owner's call and still open.

What is now observed rather than inferred:

* **The dispatch.** Slot 7 (`DS:63 = 7`, readied with `M 7`) branches to the arm that calls the target
  scan at `asm/330C_cast_target_finder.asm`. The scan takes the first creature, slot 31 downwards,
  whose `mon_type` is non-zero, whose `mon_kind` equals `DS:25`, and whose `mon_x`/`mon_y` are exactly
  `DS:00 + DS:02` and `DS:01 + DS:03`. No match prints `CAST-MAGIC MISSILE-FAILED!` and **still spends
  the scroll**, which is the same spend-before-validate shape gate 5 already showed.
* **`DS:02`/`DS:03` are a signed facing delta**, measured `(01,00)` on entering the tower and
  `(00,FF)` after one Left turn, unchanged by Down. So the spell needs no aim prompt because it hits
  what the caster is facing. Both bytes sit inside `state.h`'s `reserved_02` and are now known to have
  a consumer.
* **The amount is `DS:20 = 2*DS:56 + 0x1e + CF`, and the `cmc ; sbb` idiom subtracts `DS:20 + 1`.**
  Two points, both exact: `DS:56 = 0x99` → `DS:20 = 0x51`, HP `0x99 → 0x47`; `DS:56 = 0x01` →
  `DS:20 = 0x20`, HP `0x99 → 0x78`. `DS:56` is the high BCD byte of experience, so the third-party
  "scales with level" claim is wrong in shape, not merely unproven: there is no level field and a
  binary doubling of a BCD digit is not monotone (82 damage at `0x99`, 33 at `0x01`).
* **Survived → nothing prints and the slot keeps the reduced HP. Died → the slot is cleared and the
  frame reads `KILLED--GOLD+3--EXP.+00`**, with the gold awarded in BCD and wrapping (9999 → 0002 on
  +3, consistent with the no-ceiling property the owner recalled). The award *table* is not decoded —
  the synthetic test slot carried no kind entry, hence `EXP.+00`.

Two process notes. The facing delta was first misread as the constants 2 and 3, because `adc 0x2,%al`
with no `$` is a memory operand in the listing dialect; the misread is recorded as a TRAP in the
scan's header and the generic lesson went to `docs/harness-portability.md` § 6. And holding a creature
in range needed an in-process poke loop — three scripted casts lost the race against the site tick.
Recipe in `docs/spice86-capture.md`, patterns 3 and 4.

What this does **not** settle: the other site-side branches (PASSWALL, SURFACE, PRAYER, BLINK's
validity rule, KILL's award) and whether `mon_kind` is a floor index. Registered as G46.

## D13 closed: `DS:4E` is charisma, and the price routine reads it (2026-10-29)

`captures/spice86/d13-cha-vs-wis` — the first character ever seeded with `DS:4E ≠ DS:4F`
(`0x20` and `0x40`, `int` held at `0x40`), which is the only shape that can discriminate the pair.
Two independent observations, and they agree:

* **The `Z` status screen printed `CHARISMA-20` and `WISDOM-40`.** The byte under the CHARISMA
  label is `DS:4E`. `engine/src/core/state.h` was right; `docs/spice86-live-findings.md` § 11 had
  them swapped and is corrected, with its "not proven" section retracted rather than deleted.
* **The same character bought the same horse for 52 gp** where the maxed character one day earlier
  paid 32 gp. That is what `asm/20AB` computes for `int + [DS:4E]` (`0x60`, bit length 7,
  `DI = 4+8-7 = 5` → 52) and not what it computes for `[DS:4F]` (bit length 8, `DI = 4` → 32).
  Owner played over noVNC; gold read live as `DS:58/59 = 99 47`, frame `b-horse-52gp.png`.

The owner's recollection that charisma moves prices survives the falsifiable test `docs/commerce.md`
§ 5b set for it — lower charisma, higher price, same vendor, same item — so it points at this
mechanism and not at a second one still unfound. `asm/20AB`'s DOUBT on the field identity is closed
and its name is earned; `tools/u2_edit.py` no longer marks the pair DISPUTED.

Still open in the same area, unchanged: the tavern's `0200` food quote is unreproduced and is not in
the reachable sequence. That is G23's remaining half.

One hazard found on the way, worth knowing before anyone drives the noVNC container: **injecting
keys into it over MCP crashes Spice86** — `InvalidOperationException: Collection was modified` in
`CfgCpuViewModel.RegenerateGraphFromNodeAsync`, racing the pause that `send_keyboard_key` causes.
Reads are fine. Let the human type and keep the host side read-only.

## G40 closed: the horse really does refuse shallow water (2026-10-29)

`captures/spice86/g40-horse-shallows` — sector 20 at (18,40), chosen by scanning every wilderness
sector for a shallow cell (`0x04`) with three or more foot-walkable neighbours, so one standing spot
tests land above, shallows right and shallows below.

On foot, Right walked into the shallows. With `DS:13` **held** at `0x22` across the key press, Up
walked onto land (the control that makes the refusals mean something — without it, "refused" would
be indistinguishable from "mounted cannot move" or "key lost"), and both Right and Down refused with
the position unchanged and the frame printing `EAST--INVALID MOVE!` / `SOUTH--INVALID MOVE!`.
`ExecutedInstructions` carries the horse arm and the verdict block, so the rule fired.

Two things worth keeping cold. **The engine needed no change** — `u2_mode_passable()` already rejects
`mode == 0x02` on the horse and plane arms, transcribed from the same bytes, and this is the first
live confirmation the transcription matches the original. And **`DS:13` is volatile**: the one-shot
`--poke` the `stepcost` session used can be restored by the time the key lands, which is why every
mounted row here was driven by the in-process hold loop and read back at the press. A poked mode that
you did not confirm at the moment of input is not a controlled experiment.

Still forced rather than boarded: no capture has bought or boarded a horse headlessly, so whatever a
real boarding writes beyond `DS:13` is untested.

## D12 closed: `DS:25` is the site floor index (2026-10-29)

`captures/spice86/d12-ladder-ds25` — the ladder capture `state.h` was waiting on. In the sector-24
tower, UP LADDER took `DS:25` `0 → 1` and DOWN LADDER took it back `1 → 0`, through the game's own
`incb`/`decb`; pressing `D` on a `0x20` down-ladder cell left the site to the world map and left the
byte at `0xFF`, an unguarded decrement below zero. The field is now `site_floor`.

Three corrections came out of re-reading the arms (`asm/3139`'s new LADDER ARMS note): the `cmp $0xf`
bound is on `DS:25` and the value validated is the **candidate floor**; the validity call is
`asm/1451_site_cell_open_on_floor` (renamed), which requires the cell at `DS:23/24` on that floor to
be **zero**, which is why UP LADDER fails at the tower entry cell and works one column over; and
**the tower and dungeon arms are swapped** — in a dungeon the two spells move the floor counter the
opposite way. The swap is a byte reading, not an observation; dungeons stay parked.

Corroboration, all measured: a tower's `mapxNN` is exactly 16 pages of 256 bytes matching the `0xf`
bound, `asm/1451` and `asm/68C0` both page the site grid by `floor*256`, and the live grid at
`DS:1800` is byte-identical to `mapx24` for floors 0 and 1 — the site grid is the raw file, unlike
the stamped overworld grid.

**And an inference of my own from earlier the same day is refuted.** `asm/330C` had suggested the CAST
target scan's `mon_kind == DS:25` test meant "same floor as me". It does not: the sector-24 tower's
floor-0 creatures carry `mon_kind` 0..15 at once and `monx24` is all zeros, so the kinds are generated
at load. The gate is literally "the creature's kind index equals the caster's floor number", and it is
live — at `DS:25 = 1` a held `mon_kind = 1` creature in the facing cell was hit where a `mon_kind = 0`
one was refused. Odd rule, and it is the game's.

`cmd_D` ran for the first time in this capture, which halves the never-executed-handler gap: `cmd_N`
(NEGATE TIME) is now the only letter with no execution evidence. **Superseded the same day** —
`captures/spice86/cmd-nr-live` ran `cmd_N` twice, so all 26 letters have now executed somewhere in
the corpus. See the READY WEAPON / NEGATE TIME section at the end of this file.

## CAST's KILL and BLINK arms, observed (2026-10-29)

`captures/spice86/g46-kill-blink` — same tower, same rig, two more of `asm/3139`'s dispatch arms
moved from inferred to observed. This is the first evidence against D14's premise that every spell
effect is unreachable without a dungeon: KILL, BLINK, LIGHT and the ladder spells were all filmed in
a tower.

* **KILL (slot 9)** calls the shared target scan and, on a match, jumps to the death path with **no
  damage step**. The discriminator is deliberate: the held creature was at full HP `0x99`, which
  MAGIC MISSILE leaves alive at `0x47`, and its slot was erased outright. The award landed on the
  character — `GOLD 9999 → 0006`, `EXP 9999 → 0001`, both wrapped BCD — so KILL pays out and the
  amount comes from the creature's entry, not from a table in the executable.
* **BLINK (slot 8)** showed both exits: `CAST-BLINK-FAILED!` when the random odd cell was a wall,
  and `(5,5) → (7,15)` on the next attempt — both coordinates odd, inside 1..15, exactly the
  `and $0xf ; or $0x1` shape. Its gate is the same `asm/1451` site-cell validity call the ladder
  spells use, so there is no separate BLINK rule to model.

Left unobserved in the dispatch: PASSWALL (4), SURFACE (5), PRAYER (6). G46 stays open for those.

### and confirmed against what the player sees (`captures/spice86/mm-owner-play`)

The owner then played the original in the browser himself — noVNC, host read-only, no scripted pokes —
and this closed the one link in the MAGIC MISSILE model that memory alone could not reach.

* **`DS:02/03` is the cell the corridor shows you.** He walked up, turned, said the creature "was right
  in front of me", pressed `C`, and the frame reads `CAST-MAGIC MISSILE` then
  `KILLED--GOLD+13--EXP.+03`. Every earlier hit had a creature the driver held in place, so the facing
  delta had only ever been checked against memory. It now agrees with the picture, and the renderer
  demonstrably draws the occupant of the facing cell while it lives.
* **The award is per-creature.** Scripted paid `+3/+00`; this paid `+13/+03`. Same spell, same floor,
  different victim. The selection mechanism is still undecoded, but "constant" is now refuted rather
  than assumed.
* **The owner independently found the location gate before reading about it** — MAGIC MISSILE refused
  on the overworld. Worth recording because the correction matters: the gate is `DS:4A >= 4` and **4 is
  a tower**, so towers cast and only the overworld and the settlement kinds refuse.
* **A stray `S` gave a third live `cmd_S`**: `STEAL DIRECT-PASS`, exactly the `asm/3F25` decode — print
  `STEAL DIRECT-`, fall through to the outcome word.


## TRANSACT landed: the ray, the quadrant selector, and one pricing path (2026-10-29)

`cmd_T` is implemented in `engine/src/game/commerce.{c,h}`, with the command entry in
`commands.c` and the direction arm in `overworld.c`, and `make test-cp6` gates it. The finding
that made it buildable is that three of the four things `docs/handoff-commerce-transact.md` § 4
listed as not-evidenced were not waiting on gameplay — they were waiting on the eight bytes of
`asm/1590` and the fragments after them, and on two hand-copied numbers being re-measured.

* **The grocery quote is `0020`, not `0200`, and the axe is 20, not 25.** Both were recorded as
  observations that `asm/20AB` could not produce, and one of them was written into that file's
  header as "known to fail a real observation". Stopping the CPU at the printer (`asm/17B7`) and
  at the charge (`asm/2052`) during scripted purchases gave `DS:27..2A = 00 20 00 12` at the print,
  the same pair at the charge, and gold `9999 → 9979` — and the frame says `0020`. The weapons
  shoppe routes through `asm/20AB` after all: `asm/1F00` passes the menu digit straight in, and
  the stopped entry register was `AL=0x03` for menu item 3. **One pricing path** for the shops,
  plus two that bypass it with a literal (the tavern drink at `0005`, the clue seller's digit in
  the hundreds byte). The per-shop index is now read at all six call sites.
* **`asm/1590` is a quadrant switch, not the menu-versus-line fork.** Player `X` and `Y` against
  `0x20`, then `[DS:4A]` against 2, for eight destinations (`docs/commerce.md` § 5d). The fork is
  `asm/4075`: whether the slot scan found a creature on the cursor cell. The sign table in the
  shipped data names the predicted shop wherever a sign names a shop at all, across six sectors of
  both kinds, which is also why § 2 of `docs/commerce.md` never found per-town catalogue variation:
  the variation is inside the sector.
* **The ray is two cells and the sign row is transparent by design** — near cell must be a
  high-bit sign letter or `0x7C`, far cell must be the `0x68` marker. The direction delta comes
  from `asm/2362`, the same aim gate cmd_A/S/U/O use; cmd_T's call to it had been invisible because
  `asm/405D` stores it as a forced-keep byte, so no `# calls:` line carried the edge.
* **The tlkx selection rule is the role byte**: `[slot+0x1D7] - 0x80` indexes the NUL-separated
  records in the sector's loaded `tlkxNN` buffer at `[DS:476]`, printed by `asm/154E`. Observed
  live with `AL=0x02` for a role-`0x82` Jester. **Implemented 2026-10-29**, both arms:
  `u2_world_load_tlkx` reads the sector's dialogue (256 bytes, `CX` at `asm/22D5` — the limit
  matters, since `tlkxNN` ships at 384 and record 6 decoded from past byte 256 is the sector's
  code tail), and `u2_world_tlkx_record` walks it exactly as `asm/154E` does. The typed arm
  (`asm/40CB`'s `street_*` chain) is wired too, with its seven literals extracted as `run`
  locators because each is a split `db` around an embedded `0x0d`. An unmatched type falls
  through `asm/41D5` -> `asm/4206` -> `asm/40A6` and prints `FUNNY, NO RESPONSE!`, so the
  default arm is a cited byte rather than an engine affordance. Gate: `CP6 talk` in
  `run_cp6.sh`, against the real `tlkx22`/`monx22`; it skips with a reason without a copy.
  Still open in this area: the duration of `asm/4209`'s on-screen pause, and where the line
  actually renders — `asm/4209`'s header records that the captures show no dialogue line
  between the command and the next prompt.
* **Dispatched and deliberately not implemented**: the cleric shrine, the magic shoppe, the pub and
  the clue seller. Each gap is commented where the branch is, with the reason (the spell-slot
  question for the two spell shops, the chatter table for the pub, the clue pool for the seller).
  `run_cp6.sh` asserts the pub stays silent, so a half-implementation cannot land quietly.
* **A purchase now redraws the status band.** The engine's `creature_turn()` does not, so gold
  bought things while the band kept showing the old number. The redraw sits in the TRANSACT arm
  where the mutation happened, with the reason in the comment.

## cmd_R and cmd_N land, and the string emitter had been lying for weeks (2026-10-29)

`captures/spice86/cmd-nr-live` — one headless run, maxed Fighter in sector 32, two command letters.
`cmd_R` (READY WEAPON) had executed in a few harvests but had never been read branch by branch, and
`cmd_N` (NEGATE TIME) had never executed at all in 186 harvests. Both are now implemented in
`engine/src/game/commands.c` and gated by `make test-cp6` (nine new cases).

* **`cmd_R` is `cmd_W`'s shape with a different stat.** Menu of nine, digit, pool name echoed, then
  ownership on `[DS:76 + digit]` and a gate on **agility** (`DS:4C`) against `digit * 8`, and on
  success `DS:61 = digit`. Copying the armour handler is the trap: that one gates on strength. The
  packed-byte rule applies identically — agility `0x40` is 64 stored, so digit 8 passes and digit 9
  (72) does not, for a maxed character. The weapon pool index equals the menu digit, which the pool
  itself confirms independently: `HANDS DAGGER MACE AXE BOW SWORD GREAT-SWORD LIGHT-SWORD PHASER
  QUICK-SWORD` abbreviates to exactly `1-DA … 9-QU`.
* **`DS:E3` is not a counter and was never one.** `items_held` is 16 slots from `DS:D6`, so `DS:E3`
  is `items_held[13]`, and the name table calls slot 13 STRANGE COIN. NEGATE TIME **spends a strange
  coin**: none left prints ` HOW?` / `YOU'RE NOT EINSTEIN`, one or more falls into `asm/3C04`, which
  decrements the count in packed decimal, prints `YOU RUB A COIN...` and stores `0x14` into `DS:26A`.
  The `0x01` that looked like a mystery charge on the seeded character was maxout handing out one of
  every item. This is the "look it up before you investigate it" rule biting again: `state.h` already
  documented that `items_held` runs to `0xE5` inclusive, and the near-miss was adding a `counter_E3`
  field inside an array that was already named.
* **`DS:26A` had no ticker in the engine.** `ai.c` honours it as the creature-move freeze, but
  `status_tick()` only decremented the three paralysis bytes, because `asm/0C33` starts at `DS:26B`.
  `asm/0C54`/`asm/0C5B` tick `DS:26A` the same way, and nothing could set it until now — so the
  handler that sets it came with the fix, or every creature on the map would have frozen forever.

**And the run exposed an emitter bug that had been corrupting shipped game text.**
`engine/tools/extract/extractor.py` escaped non-printable bytes as C hex escapes (`\xNN`), but a C
hex escape consumes every following hex digit, so `"...:\x0d1-DA"` is one escape (`0x0d1`, truncated
by the compiler to `0xD1`) and not CR-then-`1`. `char_print` masks the high bit, so the byte arrived
as `0x51` — `Q`. Five literals were affected, all of them menus whose newline glyph sits in front of
a digit: the WEAR ARMOUR menu, the armour and weapons shoppe menus, the transport "FOR YOU ONLY"
line, and the new READY WEAPON menu. Each had been rendering its line-initial digits raised by 0x20
(`1-DA` as `Q-DA`, `4-PLATE` as `T-PLATE`). Fixed by emitting octal escapes, which C bounds at three
 digits. Two things to keep: the CP5/CP6 gates stayed green through all of it because they grepped for
substrings that happened to survive, and the frame that finally made it visible was a *new* command's
frame — the defect was only found because `cmd_R`'s menu was long enough to show the pattern four
times in one screen.

**Also corrected by a frame, not a listing:** the weapons shoppe's offer prefix. The binary holds
`AH! YES! ` and `AH! YES! A `; the code had the second, and `w5-axe.png` shows `AH! YES! AXE`. See
`docs/commerce.md` § 5e.

## cmd_S lands, three owner corrections, and a Z teardown that was not (2026-10-29)

Three of the owner's recollections about the unimplemented letters were checked against the
bytes, and all three were better than what this project had written down.

* **`cmd_S` is not combat.** This file and `commands.c` both said its effect was "modelled in
  `combat.c` for CP5 ticket C6 and merely unwired". That conflated `asm/3F5D_steal_attempt`
  (the player robbing a shoppe) with `asm/0FCD_combat_thief_steal` (a THIEF monster emptying
  one of the player's slots mid-fight). The only thing they share is the English word. The
  real command is now implemented and gated.
* **`cmd_F` is not the attack arm.** `asm/35C9` gates on `[DS:13] == 0x24`, the ship stamp from
  `asm/2FAD`'s BOARD dispatch, so FIRE is the frigate's cannon and off the ship the entire
  command is `FIRE WHAT?`. Then it prints ` DIRECT-` and uses the aim gate: an aimed cannon.
  `cmd_A` is the attack. The old note was a guess from the word.
* **`cmd_G` is parked with dungeons** (owner, same day), alongside `cmd_D` and G47.

**What `cmd_S` turned out to be.** The same two-cell ray as TRANSACT, at the same merchant
marker, through the same `asm/2362` gate — whose call sits at `asm/3F36` as a bare forced-keep
`e8`, the third time that storage choice has hidden a real edge. No creature scan anywhere.
Two coin-flip rolls, then the quadrant the *player* is standing in decides which category falls
off the shelf: village north-east gives food, town north-east gives armour, town north-west gives
weapons, and everything else is `NO LUCK!`. A THIEF skips one of the two rolls rather than being
required, so the recollection that it "probably needs a thief" is half right, and "can steal from
shops" is entirely right. The high-end goods are unreachable by construction: `roll & 3` and
`roll & 7` with a `+1`, so REFLECT and POWER armour and the QUICK SWORD can never be stolen.

The branch table is gated purely (`u2_steal_loot_for`, 14 checks, including all five cells that
must *not* pay). The end-to-end half asserts the invariant the rolls cannot make deterministic —
printed line and mutated bytes must agree — plus a case with `--rng-state 000000000000` to force
the success arm, because the default seed refused it on forty consecutive runs and a gate that
only ever sees `NO LUCK!` would pass a broken FOOD branch.

**And `cmd_S` exposed a real defect in `cmd_Z`.** The CP5 gate "Z leaves no trace" compares a run
that opened the status page against a run that did not, using `S` as the inert probe key. `S` was
inert because it was unimplemented. The moment it printed anything, the diff showed the next
command's text landing two rows too high: `cmd_Z` snapshots the `u2_vram` on the way in and puts
it back on the way out, but the text cursor the seams actually walk is
`ctx->state.cursor_row/cursor_col` (DS:2E/2F), and `u2_status_draw_full` leaves that wherever the
item lists ended. The teardown's own comment claimed "the cursor goes back with it", which was
true of the vram's drawing position and false of the one that matters. Fixed by snapshotting the
text cursor and both margins alongside the vram.

Worth saying about the gate itself: it was not wrong, and it should not be weakened. It was
designed to compare whole screens rather than grep for item names precisely so a broken teardown
could not pass, and it caught this the first time the probe key produced output. The lesson is
about probe keys, not about the assertion — an inert probe is a gate that is quietly checking
nothing, and "inert" is a property of the world that can expire without warning when someone
implements the letter you were leaning on.

## cmd_F: the frigate's cannon, and a range the owner and the bytes disagree about (2026-10-29)

`cmd_F` is implemented (20 of 26 letters). The owner supplied the mechanic from memory — frigate
cannons, 3-space reach, 32..70 damage, which is what makes shelling a shoreline safe — and the
bytes agree on the ship and disagree on both numbers.

* **The gate is the ship.** `asm/35C9` reads `[DS:13]` and continues only on `0x24`, the ship
  stamp `asm/2FAD`'s BOARD dispatch assigns. Off the frigate the entire command is
  `CMD: FIRE WHAT?`, which is now gated in CP6.
* **The chain is `asm/35ED` tone → `asm/35F0` creature probe → `asm/35F3` slot → `asm/3603` roll
  → `asm/3606` damage → `asm/3611` → `asm/2C19` subtract.** It closes the standing question in
  `asm/2C19_monster_hp_subtract`'s header ("what selects the slot and why that magnitude is not
  established"), and it means `asm/3611` is not the town-NPC damage path it was once promoted to.
* **On a hit the original prints nothing.** No `str_out` exists between the probe and the damage
  apply, and the death branch (`asm/2C19`'s `jae` to `asm/2CA4`) stamps the emptied cell instead
  of announcing it. So `--MISS` is the only text, and the silence is cited rather than assumed.
  The tone that would have been the audible feedback is `asm/5EAC`, deliberately not emitted:
  `asm/5DA9`'s frequency arithmetic is unconfirmed against a heard tone, and a guessed tone is
  worse than none.
* **A cannon kill awards nothing.** The melee reward rolls live on `asm/2E3D`'s path; the cannon
  reaches `asm/2CA4`. `apply_damage_to_slot` takes an `award` flag so the two cannot be silently
  merged by whoever edits it next, and `attack_check` asserts the no-reward property.

**Two disagreements, both left open as G49 rather than resolved by typing.**

The **range** is one cell in the bytes: `asm/23E6` probes a single cell and its own header warns
it is "not a ray with a reach", while `cmd_T` builds its two-cell ray by stepping twice in its own
body and `cmd_F` does not. The recollection is three. Neither has been seen on a frame.

The **damage** is where this project was wrong, not just the memory. `asm/3606` is `or $0x20,%al`,
which *forces a bit* rather than masking, so the reachable set is every byte with bit 5 set —
32..63, 96..127, 160..191, 224..255. Floor 32, ceiling 255. `asm/2C19`'s header had recorded
"32..63" and this project repeated it into a test, which is how the error survived: the first draft
of `attack_check` asserted the tree's claim and went red on seeds 21, 22 and 23. That is the
lesson worth keeping — **a cited comment is a hypothesis, and the citation being in our own tree
does not promote it.** The owner's 32 matches the floor exactly; 70 is not reachable at all, since
bit 5 is clear in it.

### ...and then a live run settled the range the same day (G49a closed)

`captures/spice86/cannon-range-live`. The boarding that G49 assumed was the blocker turned out not
to be one: `asm/35C9` reads `[DS:13]` and nothing else, so the frigate state is one poke, and the
target is four more. The only real obstacle was that a planted creature wanders, which `DS:26A`
fixes — the freeze counter NEGATE TIME was proven to set a few hours earlier, doing unpaid work as
a test rig.

Fired East with the same keypress at three distances: **1 hit twice** (`mon_hp` `0x99→0x5B`, then
`0x99→0x76`), **2 missed**, **3 missed**. The repeatability at range 1 is what makes the misses a
finding rather than a bad roll. Every address in the chain is in the capture's
`ExecutedInstructions`, so the reading is `observed`.

**The cannon reaches one cell. The three-space memory is refuted by the game itself.** Two extras
came free, both of which were static readings an hour before:

* **A hit prints nothing.** The frame shows `FIRE DIRECT-EAST` with no `--MISS` and no damage line.
  The cited silence in `asm/35F0..asm/3611` was right.
* **HP is decremented in binary on a BCD-looking field.** `0x99 - 0x3E = 0x5B`, and `asm/2C19` has
  no `das`. The engine already did this; the note exists so nobody "corrects" it toward decimal.

One harness lesson, because it cost a wasted round: at the default `--keys` spacing the aim gate
(`asm/2362`, which polls a bounded number of times and then prints `PASS`) gave up before the
arrow arrived, and the frame read `CMD: FIRE DIRECT-PASS` with the arrow landing later as a stray
command. `--delay 0.05` fixes it. A `--PASS` on an aim prompt in a capture is usually this, and it
is the harness, not the game.

Still open from G49: a cannon **kill** has never been seen, so the no-reward reading stays static,
and no cannon has ever been fired after a real `BOARD`.

### ...then the owner drove it himself, and the manual turns out to be the thing that was wrong

He opened the noVNC session, walked onto the frigate tile at sector 31 (14,25), pressed `B`, and
fired. `[DS:13]` went to `0x24` for real — no poked stamp — and his verdict matched the capture:
**one tile**. Recorded as G49b, and it retires G49a's only caveat.

Two things came out of the session that were not the question.

**Boarding does not spend the BLUE TASSLE.** It read 5 before `B` and 5 after. That is a second
independent sample alongside `captures/spice86/shipcap3`, which had one, and it strengthens G42's
answer.

**The printed manual says the cannon has range, and the manual is wrong.** That is the interesting
part, because it explains the recollection rather than contradicting it: "three spaces, enough to
shell a shoreline safely" is not a bad memory, it is a documentation-sourced belief, and the owner
went and checked it against the game rather than arguing from it. The source order on this
mechanic ended up being manual (wrong) → recollection (wrong, probably sourced from the manual) →
bytes (right) → play (right). Worth stating plainly because a manual carries far more authority
than a walkthrough, and this project has a habit of correctly grading third-party walkthroughs
while letting period documentation slide.

One incidental, and it is a save-format finding rather than a cannon one: the owner's items were
being **stolen mid-play** by three thieves parked on the quay at (15,25)-(17,25), which is the
first time `asm/0FCD_combat_thief_steal` has emptied a player slot in front of us rather than in a
listing. HP fell from 9999 to 9085 in about a minute of standing there. It also means my first
explanation for the missing items — that the noVNC load had dropped them — was wrong, and I said it
out loud before checking, which is the pattern the thieves were the answer to.

## The site trio: IGNITE TORCH, DESCEND, KLIMB — decoded without a capture (2026-10-29)

Three handlers came off the NULL list with no emulator running and no owner session, which is
worth saying because the queue had them filed under "parked with dungeons" and they were not.

The reason they had never been read is a mechanical one. `cmd_I` is one byte and `cmd_K` is one
byte: each is a bare `e8` whose operand and whose text live in the *next* map fragment, so every
prior pass described them as "prints an inline string; call operand and text live in the following
map fragment" and moved on. The headers were accurate about the range and useless about the
command. Read across the boundary they turn out to be `IGNITE TORCH` and `KLIMB`, and `cmd_D`,
which had been observed running, is the same shape.

**KLIMB and DESCEND are exact mirrors, and neither is decodable alone.** Same site-kind gate (4
tower, 5 dungeon), same cell gate with the feature bit swapped (`0x10` up, `0x20` down), same two
floor arms with the direction swapped. A tower climbs up and descends out; a dungeon descends
deeper and climbs out. So `DS:25` is not height — it is a floor index that counts up out of a
tower and up into a dungeon, which is a strange thing to be until you notice it is the same number
read from opposite ends.

The confirming witness had been sitting in the tree since the morning. `d12-ladder-ds25` recorded
`DS:25 = 0xFF` after a DESCEND in the sector-24 tower, and nobody joined that to `asm/3823`'s
`decb` / `js` pair. 0xFF is what decrementing from zero leaves behind, and the `js` is what turns
it into an exit. One recorded byte, in the file for two hours, doing the work of a capture.

**IGNITE TORCH shares its light counter with the LIGHT spell.** Both write `0x96` into `DS:26`, so
a torch and that spell are one running count rather than two light sources — casting LIGHT in a
lit corridor spends a spell to refill a torch that is already burning. That was already in
`state.h` from the CAST work; what the cmd_I decode adds is that the *torch* side is real and
reachable, and that the whole command is three instructions and a store.

### Three things the tests caught that the reading did not

**`DS:25` is below the saved record.** The record is `DS:36..DS:135` and the floor lives at `DS:25`,
so it is scratch and the game does not remember which floor you were on. The first seed tried to
set it through the player file and got zeros back. The cases now drive it with keystrokes, which is
the better test anyway because it exercises the round trip instead of asserting a starting state.

**The two ladder cells really are distinct.** A round-trip case that climbed and then descended from
a `0x10`-only cell failed with DESCEND refusing — which is correct. The cell under the player has
the up feature and not the down one. That failure is now a permanent assertion, because it is the
cleanest proof in the suite that the feature bits are not interchangeable.

**`asm/53B5` prints two digits always**, fed by `daa` rather than by a stored BCD byte, so a floor
past 99 prints a wrapped pair instead of a third digit. Emulated rather than formatted with
`%02u`, so the quirk survives instead of being quietly smoothed over.

### What is still not modelled, and says so in the source

The engine has no floor term in its grid index — `asm/1451` computes
`word[DS:472] + floor*256 + y*16 + x` and the engine omits the floor multiplication, so a tower
above its first floor still draws page 0. And the exit block's `[DS:6D]` branch writes the player
record on leaving a dungeon, which means **leaving a dungeon saves the game**, observed never. Both
belong to the dungeon span AGENTS.md § 3 parks, and both are named at the mutation site rather than
left as a silence.

`site_leave()` became `u2_overworld_site_leave()` on the way, because cmd_D needs the same block the
sector-edge test uses and duplicating an exit path is how two exit paths start disagreeing.

**23 of 26.** What is left is `cmd_C` (gates decoded and capture-confirmed, every effect dungeon-side,
blocked on owner decision D14), `cmd_G` (needs ground items, a dungeon question), and `cmd_O` (needs
town NPCs and an attitude model). None of the three is a one-afternoon static decode the way these
were.

## CP7: the dungeon engine — entered, lit, walked, fought, looted, descended (2026-10-29)

`engine/src/game/dungeon.c`. The owner lifted the dungeon park and asked for the whole
thing: get in, light a torch, see the wireframe, walk, fight, pick up a box, go down a
level. All six work, and `make test-cp7` drives each one against a synthetic dungeon and
is wired into `make test`.

**The decode was the whole job, and almost none of it needed an emulator.** Every number
below came out of `asm/` plus one existing capture. What made that possible is that the
renderer's geometry lives in CS as plain tables, so once you know which bytes are tables
they can be read as bytes rather than argued about.

What closed, in order of how much it was blocking:

* **The grid is 16 pages of 16x16, not a 64x64 sector.** `asm/68C0` forms the cell address
  as `word[DS:472] + [DS:25]*256 + y*16 + x` with the row offset truncated to one byte, and
  `[DS:25]` is the floor. Confirmed three ways: `asm/1451`'s identical page arithmetic, the
  live page 0 of `captures/spice86/dungeon-35-live` being byte-identical to
  `original/mapx35`'s first 256 bytes, and a tower `mapxNN` being exactly 16 pages of 256.
  The engine had been reading a dungeon as a flat 64-wide grid, which is why `cmd_D`'s
  header carried a note about "a tower above its first floor still draws page 0". That note
  is now closed: `u2_world_dungeon_at()` carries the floor term.
* **A cell is two layers in one byte, and the renderer reads them separately.** `asm/6078`
  reads the centre column twice, once masked `& 0xF0` and once `& 0x07`. High nibble is
  terrain, low nibble is a stamp. The four terrain bits are now named by the code that tests
  them: `0x80` rock (`asm/6144`'s corridor-end test), `0x40` chest (`asm/363B`'s GET
  compares the cell under the player to `'@'`), `0x20` down ladder (`cmd_D`), `0x10` up
  ladder (`cmd_K`). The shipped `mapx35` uses seven values, all multiples of `0x10`, and
  only seven of the sixteen a free nibble would allow — which is why it never looked like an
  art index and never will.
* **The facing is a signed byte pair at `[DS:02]/[DS:03]`, not an enum.** `asm/277B`'s two
  turn commands negate it, so an unsigned reader sees every westward facing as `0xFF`.
  `state.h` now names both bytes with the proof in the comment.
* **The four direction commands are ADVANCE, RETREAT, TURN LEFT, TURN RIGHT**, all in
  `asm/277B`, transcribed instruction by instruction into `engine/src/game/dungeon.c`.
  ADVANCE refuses a cell whose low nibble is set or that is exactly `0x80`, which is what
  makes the chest mechanic work: you walk onto the chest and then GET it. RETREAT tests
  `& 0x8F` instead, a wider net, and that asymmetry is in the bytes and not a guess.
* **The trap fires off the FLOOR.** After a successful ADVANCE, one `rand_byte` is compared
  to `[DS:25]` and `jb` means deeper floors trap more often and floor 0 never traps at all.
  The trap zeroes `[DS:26]` — it puts out your light before it says anything — then
  `ARGH! A TRAP!`, then tools absorb it or the damage tail runs. The zero-tools damage is
  still G47 and the engine says so at the site rather than inventing a number.
* **The darkness is a byte.** `asm/6000` opens with `cmpb $0x0,0x26 ; je` and the jump skips
  the depth scan, the shape dispatch and the sprite pass entirely. So "IT'S DARK!" is not a
  palette trick or an overlay: nothing is drawn. The engine reproduces exactly that and the
  CP7 render check asserts the dark frame has zero ink in the map window.
* **The wireframe is a perspective projection out of eight-byte tables.** `asm/6078` samples
  three columns of cells out to eight depths — centre from the player's own cell, left and
  right one cell to either hand, all three advancing by the facing — and `asm/6144` picks a
  shape per depth from the cell bits and calls twelve short line routines. Each of those
  loads the line drawer's four coordinate registers from a CS table and calls `asm/67B3`
  one to nine times. The registers are **not** reset between calls, so a hand transcription
  silently drifts; `tools/gen_dungeon_draw.py` reads the twelve routines out of `asm/` and
  emits `engine/src/game/dungeon_draw.inc`, which carries the register state across calls the
  way the CPU does. Twenty-eight tables came out verbatim, including the eight base ones:
  `XL = 00 40 60 70 78 7C 7E 7F`, `XR = 0xFF - XL`, and `YT[d] + YB[d] = 0x7F` for every
  depth but the last.
* **The plot origin is (16,16) and the view maps 1:1 onto the frame.** `asm/4B76` adds
  `[DS:4BF]` and `[DS:4C1]` to the coordinates and nothing in the tree writes either byte,
  so the live capture is the only witness: both read 16. XL spans 0..0xFF and YT/YB span
  0..0x7F, so the view is a 256x128 rectangle inset 16 pixels inside a 320x200 screen. No
  scaling, which is the reading that makes the tables' own ranges land inside the frame.
  `asm/4B76` also ORs both plane bits, so it can only ever write colour 3 — the wireframe is
  white lines because white is the only pen `asm/4B76` owns and `asm/4BAA` is its eraser.

**One correction, and it was load-bearing.** `asm/6000`'s header said the three probes read
absolute cells `(0,1)`, `(dx,1+dy)`, `(-dx,1-dy)`. They do not. With the player at `(5,5)`
facing `(1,0)` the live bytes read `[DS:14]=0x10 [DS:15]=0x00 [DS:16]=0x80`, and the only
triple in live grid page 0 that matches is `(5,5)`, `(6,5)`, `(4,5)`: under, ahead, behind.
The old reading was inferred from operand bytes that are relative deltas, with the base added
upstream. The header now carries the correction and the capture that proves it.

**Two things the captures disagree about, recorded rather than resolved.**

* `docs/spice86-live-findings.md` § 13 tabulates `A` as ADVANCE in a dungeon; § 1b records
  `A` printing `ATTACK--MISS` in a tower. `asm/2B9B`, the site-kind gate at the head of the
  ATTACK chain, is `mov [DS:4A],al ; cmp $4,al ; jb L_002da5` and **both** destinations reach
  `asm/2BA5`, the arms gate — the `jb` targets it and the fall-through reaches it three bytes
  later. So the gate refuses nothing and there is no arm off the A jump-table entry that could
  print ADVANCE. The engine follows the bytes and the tower frame: `A` attacks the cell the
  facing names, and Up-arrow is ADVANCE. The movement § 13's row was describing is reachable
  either way; the row itself is now an open question, not a settled fact.
* `asm/3381` increments the floor with no bound check while `asm/1451` bounds the same byte
  with `cmp $0xf`. Still unreconciled, and still G50(c).

**What CP7 does NOT have, and each one is named where it would live.** The sprite pass reads a
stroke table at `DS:2900` (T9) that has no decoded consumer, so the engine marks the
convergent point of a stamped depth instead of inventing an outline. The trap's zero-tools
damage is G47. `GET`'s overworld arm (`'*'` armour, `','` weapon off `asm/361C`'s table) is
undecoded, so the command prints its word and does nothing outside a site. Dungeon creatures
do not yet move — the overworld sweep indexes the 64-wide grid and would corrupt a page, so
`asm/147F` spawns them and nothing walks them. And the leaving-a-dungeon save branch on
`[DS:6D]` in `asm/3823` is still unobserved.

**24 of 26.** `cmd_G` came off the NULL list with a real site arm and an honest overworld gap.
What is left is `cmd_C`'s effects (owner decision D14) and `cmd_O` (town NPCs and an attitude
model).

### CP7 addendum: the sprites, same day (2026-10-29)

The dungeon section above listed "no sprites, T9 undecoded" as a gap. It was not one.
Three hours later the creatures draw, and the reason it looked blocked is worth keeping:

* The table was never missing. `original/monsters` is loaded **verbatim** into `DS:2900`
  by `asm/07F3`, which is a DOS `AH=27h` random-block read with `CX = 0x800` and the
  transfer address in `[DS:474]`. The live DS at `0x2900` in `dungeon-35-live` is
  byte-identical to the file. So the art had been in the repo since day one and the item
  describing it said "needs a screenshot to derive", which was wrong twice: no screenshot
  was needed, and no owner was needed.
* The framing is **two bytes, not three**, and one instruction hides it: `asm/68ED` reads
  the second byte twice from the same pointer and only then advances. So `b0` is x (zero
  ends the list) and `b1` packs `y = b1 & 0x1F` with `pattern = b1 >> 5` in the top three
  bits. `asm/68A3` scales both by four; `asm/4BE0` paints `cs:[bp+0x48D4]` — the mask set
  `ff 11 88 cc 33 00 00 00` — as a one-to-four pixel run two scanline-pairs tall. A
  three-byte reading produces plausible garbage that still looks vaguely like a shape,
  which is the dangerous kind of wrong.
* The confirmation was a plot, not a proof: eight 256-byte blocks, seven distinct creature
  silhouettes, and a shared distance ladder in block 0's tail. `asm/07F3`'s `CX = 0x800`
  = 8 × 256 is what says the block index is the dungeon type, and the near arm's
  `add ax,[DS:474]` over the stamp byte scales by exactly 256, which agrees.
* **One reading I got wrong the same afternoon, and the shapes themselves refuted it.** The
  far arm of `asm/68ED` overwrites `ax` with `[DS:474]` *after* `ah` held the stamp, so the
  type term is dropped — and my first conclusion was that the ladder must therefore land in
  an empty block 0, so the original drew a sprite only for the creature directly ahead.
  Wrong, and wrong in the instructive way: block 0 is not empty, it merely starts with a
  zero byte, so a parser that begins at offset 0 sees nothing. The ladder offsets are
  absolute into block 0's tail, and plotted they are a 6x4 blob, then 2x3, 2x2, 1x2, 1x1 —
  a deliberately **typeless distance-simplification series**. The nearest creature gets its
  own art; everything further out gets a generic silhouette, because at that size the type
  would not be legible. The far arm dropping the type term is the design, not a bug.
  The lesson is not "capture beats static": plotting the data settled it better than the
  screenshot I had scheduled would have. A static reading that predicts something absurd
  should send you back to the bytes, not into a run.
* The gate asserts sprites with a **control**, not a threshold: the same run with and
  without `monsters` in the content directory, and the centre band must differ (591 vs 490
  pixels). Disabling the sprite pass was shown to make it red. A bare "ink > N" assertion
  would have passed with the pass dead, because the wireframe alone clears any small N.
* The strokes are art and are never committed. The engine reads them from the player's own
  directory through `u2_world_load_strokes()`, exactly like the tile atlas, and the test
  seed borrows `original/monsters` at run time. A content directory without it draws an
  empty corridor and says so on stderr.

### CP7 addendum: G54, the dungeon sweep (2026-10-29)

Dungeon creatures now walk. `u2_dungeon_creatures()` transcribes `asm/1151` runtime
`0x135E..0x148E`, and the finding is that this is a **second AI**, not a variant of the
first. The item that opened G54 said "the pursuit vector is already decoded and does not
care which grid it is on", and that was wrong in a way worth keeping: the dungeon has its
own vector at `asm/152E`, and the overworld's `asm/4F11` doubles the axis difference twice
before taking its sign specifically so a creature takes the short way round a 64x64 torus.
A 16x16 page has edges. Copying the overworld vector would have produced creatures that
walk through the west wall and reappear in the east.

Four differences, each one a byte, and none inferable from the overworld side:

* **Vector.** `asm/152E`: `stc; cmc; sbb` then the sign, no doubling.
* **Grid.** Cells are looked up on the creature's own floor, `[DI+0x1B7]`, gated against
  `[DS:25]` by `asm/143C`. In a dungeon that byte is not a kind index at all — `asm/147F`
  writes `slot >> 1`, which over 32 slots is exactly floor 0..15. Same byte, other meaning.
* **Wounded behaviour.** Under 15 HP the vector is negated and the creature flees. The
  overworld's twin of that same threshold regenerates instead.
* **Stamp.** A move clears the low nibble of the old cell and ORs `mon_type` into the new
  one. Terrain bits untouched.

`asm/1151` was called `dungeon_darkness` from its two strings; the strings are the last
third of it, so it is `dungeon_turn` now. `asm/143C` and `asm/144C` were unnamed stubs and
are the first two thirds of one function with `asm/1451`, the same cut-in-half failure that
hid `cmd_I`, `cmd_D` and `cmd_K`.

Two asymmetries are reproduced because the bytes have them. A destination holding another
creature **ends the slot's turn** rather than trying the other axis, while rock at `0x80`
does try the other axis. And a cell byte of exactly `0xFF` is ambiguous between "wall" and
"wrong floor", because `asm/143C` signals the mismatch with that same literal; the shipped
maps cannot produce it, and the collision is named rather than patched, because patching it
would invent a third answer the original does not have.

**The gate is a differential, and it had to be.** `--dump-state` emits one record per
process, so there is no per-turn series to compare. Run A enters, run B enters plus one
ADVANCE, and slot 2's authored walker goes `(3,5) -> (4,5)`. Slot 2 specifically, because
the spawner refills empty slots every turn — a test that asserted "the creature table
changed" would pass with the movement half deleted. Emptying the loop bound was shown to
make it red. Two controls failed before the right one was found, and both failures were
instructive: a `getenv` stub did not compile under `-Werror` and the suite reported PASS
against the previous binary, and a `sed -i` with a `/` in the replacement silently did
nothing. A control that does not build is a control that never ran.

**One behaviour change to flag for playtest.** `asm/147F` runs inside `asm/1151` every
turn, so an emptied slot refills on the next tick. A dungeon cannot be cleared by fighting.
That is what the bytes say, it was invisible before because nothing swept, and it is now
visible in the noVNC build.

### CP7 addendum: the first real side-by-side against the original (2026-10-29)

The owner asked for the thing that should have happened before any of the renderer claims:
load the original, enter a dungeon, screenshot both, look. That capture now exists and the
engine loses it clearly.

Reference: original Ultima II in **sector 35, floor 0, position (5,5), torch lit**
(`[DS:26]` read 0x95 after IGNITE). Frames on disk under `captures/screenshots/`
(gitignored, so described here): `dungeon-35-floor0-lit.png`,
`dungeon-35-floor0-dark.png`, `engine-dungeon-35-floor0.png`.

**What the original draws at (5,5) facing north.** An outer rectangle. A trapezoid centred
near the top. A horizontal band across the middle broken by short verticals. Two
rectangular blocks at left and right. A centre vertical that runs *below* the frame's
bottom edge. No long lines from the frame corners toward the centre.

**What the engine draws there.** A fan of long diagonals from the corners converging near
the middle, with a small nested structure at the centre. That is the owner's "permanent
diagonal line", and it is real.

**CORRECTION, same session, and it is a retraction of a claim made in the commit that
added this section.** I wrote, three paragraphs below, that the original has no
corner-to-centre diagonals and that the engine's fan was a systematic over-draw. That was
wrong, and it was wrong because I generalised from ONE frame at ONE position. A second
reference frame, taken after TURN RIGHT, TURN RIGHT, ADVANCE to (8,5) facing east
(`dungeon-35-floor0-adv.png`), shows the original drawing a textbook perspective corridor:
long diagonals from all four corners converging on a small centre rectangle, with a door
opening in the right wall. So the converging diagonals are exactly what the original does.

What actually differs is therefore narrower and more interesting: at (5,5) the original's
geometry is *shallow* — the near structure is large and the far face is close, so the
diagonals are short or absent — while at (8,5) down an open corridor the same code draws
the deep converging picture. The engine draws the deep converging picture at BOTH places.
So the suspect is the depth scan's stop rule in `asm/6078` and which shapes `asm/6144`
emits per depth, and specifically the hypothesis worth testing first: **the original stops
at the first blocking cell and draws the geometry of where the corridor actually ends,
while the engine walks all seven sampled depths regardless of what is in the way.** The
depth tables verify clean (`XL+XR == 0xFF`, `YT+YB == 0x7F`), so the tables are not the
fault, and `asm/67B3`'s Bresenham is not either.

Also retracted: the claim that loading a save already inside a dungeon sector left the
engine's command layer out of dungeon mode. It does not, and the engine was right. Every
dungeon route in the engine keys off `u2_site_is_dungeon()`, which is the sector's ones
digit, and the original's own `P` load path ends with `mov $0xff,[0x12]` (file 0x0A67) —
so `DS:12` is 0xFF even inside a dungeon, and `E` printing ENTER WHAT? in sector 35 is
what the original does too, because `asm/2B8F`'s first test is the ones digit being
non-zero. The only real difference at load is that the original prints IT'S DARK! before
the first command and the engine prints it on the first tick. That is a one-message timing
difference, not a state bug, and I had promoted it to "fix this first" on no evidence.

The lesson is the one AGENTS.md already states and I broke twice in one session: a
behavioural claim needs a frame, and **one** frame is not a rule. Both of my confident
conclusions here — the renderer's geometry and the load-state bug — came from a single
observation and both were wrong.

**Method note, because it cost the session and it is avoidable.** The first several
`--peek` reads in this capture returned a confident, wrong answer: they were attaching to a
**stale Spice86 from the previous day** that was squatting the default ports, not to the new
run. The tooling connects to whichever emulator answers. Fixed in `tools/spice86_common.sh`
and `tools/spice86_start.sh`: port allocation now excludes ports already handed out in the
same launch, and the launcher refuses to start if the three API ports are not distinct. The
collision only appears when the defaults are occupied, which is why it survived until now.

**And a third finding, which I also had to correct before it hardened.** The engine was
driven to (8,5) facing east — turn right twice, three `ADVANCE` — and I reported the frame
as pixel-identical to the one at (5,5) facing north. It is not. The two PPMs have different
md5 sums (`3136636947...` and `e0b8437c09...`); I compared two downscaled PNGs by eye and
asserted identity. That is the third over-claim in this section and the failure mode is
identical each time: a quick look standing in for a measurement.

What IS true, and it is the actual bug: **the engine's position never left (5,5).** The
dump-state record reads `"pos":{"x":5,"y":5}` for both scripts, while the light counter
moved `0x96 -> 0x91`, so five turns were consumed and the dungeon tick ran. The commands
printed `CMD: ADVANCE` and the world charged for them, and the player did not move. So the
defect is in dungeon movement or in the position mirror on the dungeon path, not in the
wireframe geometry — and the geometry comparison the owner asked for cannot be trusted until
the engine is actually standing where it thinks it is.

Order of work: (1) make dungeon ADVANCE/RETREAT move the player, verified by the position
field rather than by a frame; (2) re-render at (8,5) east and compare against
`dungeon-35-floor0-adv.png`; (3) only then judge the shape dispatch and the depth stop rule.

### CP7 addendum: the residual diagonal, measured rather than looked at (2026-10-29)

The commit before this one recorded "one long crossing diagonal remains that the original
does not draw". That is wrong, and it was wrong because it came from looking at a downscaled
PNG. Measured by sampling both images along the candidate lines:

| line | original | engine |
| --- | --- | --- |
| top-left -> bottom-right, (16,16)->(271,143) | 56/60 | 60/60 |
| top-right -> bottom-left, (271,16)->(16,143) | 56/60 | **30/60** |
| top edge | 60/60 | 60/60 |

So the original draws BOTH crossing diagonals, symmetrically, and the engine draws one of
them only halfway. The remaining defect is a MISSING mirror line, not a spurious extra one —
the opposite sign of the same misreading that produced the "no diagonals" claim earlier.

Where the drawn one comes from is now pinned: `asm/6425`'s second line is
`(XL[d],YT[d]) -> (XR[d],YB[d])`, i.e. top-left to bottom-right, and it is faithful — read
instruction by instruction, `x1` is set at runtime 0x6632, `y1` at 0x663A, `y2` at 0x663D,
`x2` at 0x6645, and the second call at 0x665B changes only `y2` to `YB[d]`, so `y1` carries
over as `YT[d]`. The generated `dg_6425` matches that exactly, including the carry. The same
routine's fourth line collapses to `x1==x2 && y1==y2`, and `asm/67B3` returns without
drawing in precisely that case (its opening guard at 0x69B4), which is a nice confirmation
that the endpoint model is right.

So the missing top-right -> bottom-left line has to be produced by something the engine is
not calling, or is calling with the x pair unmirrored. Candidates, in the order worth
checking: `asm/648D` (far face), the right-side wall shapes' interaction with the carried
`x1`, and whether `asm/6425` is entered a second time per depth in a path the engine's
`dg_draw` does not reproduce. This is a smaller bug than the over-draw and it is now
described by a number rather than an impression.

Process note, and it is the point: four claims about this renderer were made today and four
were wrong, every one of them derived from looking at an image instead of measuring it. The
fix each time was the same — sample pixels, count crossings, compare md5s. The project rule
says a behavioural claim needs a frame; what this session actually learned is that a frame
needs a measurement behind it too.

### CP7 addendum: both leads refuted, and the reference frames were the invalid part

The two candidates recorded in the previous addendum were both checked against bytes and
captures rather than reasoned about, and both are dead.

**Lead 1, "the original's facing has magnitude 3, so the scan steps differ" — refuted.**
`captures/spice86/dungeon-35-live/live-ds.bin` reads `DS:00..03 = 05 05 01 00` with
`DS:483..484 = 01 00`. In a dungeon the original was actually *entered* through, the facing
is a unit vector and the depth-scan copy agrees with it. The engine's unit facing is correct.

**Lead 2, "the engine's TURN RIGHT arm is `dy = dx; dy = 0` and so a second right turn does
nothing" — refuted.** The code is

    if (dx != 0) { dy = dx; dx = 0; }
    else         { dx = -dy; dy = 0; }

which is a correct clockwise quarter turn: north (0,-1) -> east (1,0) -> south (0,1) ->
west (-1,0) -> north. I had quoted it wrong from my own earlier prose instead of reading
`u2_dungeon_turn`. state.h's cited asm formula agrees.

**So why did the live original read facing (3,0) and move 3 cells on one ADVANCE?** Because
that state was reached by *loading a save directly into sector 35*, which is not a state a
normal game reaches. The facing pair is not part of the saved record, so a loaded-into
character arrives with whatever the bytes held; entering through a gate with `E` initialises
it, which is exactly what dungeon-35-live shows as (1,0). The engine now initialises it on
load too, which is the correct behaviour for a state the original reaches only by accident.

**The consequence for the renderer work, and it is the actual finding: the two frames were
never the same state.** The reference frames were captured from a loaded-in character with a
garbage facing of 3, so its depth columns sampled cells 3 apart while the engine's sampled 1
apart. A 517-pixel difference and a 218-pixel difference between those two pictures is not
evidence about the wireframe at all.

Correct method for the next attempt, and it is cheap: drive the original to a dungeon the way
a player does — wilderness sector 30, gate cell (25,5), press `E` — confirm the facing reads
`01 00` from live DS before believing anything drawn, ignite, screenshot, then put the engine
in the same entered state and diff. Read the facing, do not assume it.

Recorded because the failure here was not a missing tool or a missing frame. It was comparing
two pictures that looked like the same situation and were not, and the check that would have
caught it cost one `--peek`.

### CP7 addendum: the residual was a pen, not a missing edge (2026-11-01)

Four sessions of "missing 495 px, localized to the bottom band and the right edge" and the
ink was never missing. It was drawn somewhere else. The owner put it in one sentence from the
two screenshots side by side: the original frame is a rectangle and the rebuild has a line
running bottom-right to top-left. It does, and that line IS the bottom edge, plus the right
edge, both drawn from the wrong start point.

The cause is one property of `asm/67B3` that the transcription did not model. The four
coordinate registers are not two points, they are a pen and a target. The drawer's stepper
does `adc %al,0x479` at runtime 0x6A4B and `adc %al,0x47a` at 0x6A8F, once per pixel of the
major axis, so when it returns `[DS:479]/[DS:47A]` hold the ENDPOINT of the line it just
drew. A caller that loads only `0x47B/0x47C` is therefore drawing from wherever the previous
line finished. `asm/6425` does exactly that three times in a row, and read with the pen
model its four lines are top, right, bottom, left: a closed rectangle. Read with a literal
carry of `x1/y1` they are top, a crossing diagonal, a left edge, and a degenerate no-draw.
That is the whole residual: the bottom edge was not absent, it was the diagonal, and the
left edge the engine drew was the bottom edge arriving at the wrong place.

`dg_line` now advances the pen, and also honours the drawer's opening guard at 0x69B4 (a
line with `x1==x2 && y1==y2` is not plotted and does not move the pen). Nothing else changed
in the geometry path; the generated `dungeon_draw.inc` transcription was already faithful to
the `mov` instructions and needed no regeneration.

Measured, not looked at. The `U2_DG_LINES` seam in `dungeon.c` dumps every line the renderer
draws with its routine and depth, and the census on a level-0 entered-east frame went from 20
slanted lines of 69 to 3, all three of them `asm/66CB`'s chest lid, which is meant to be
slanted. Then the pixels, longest ink runs inside the window:

| frame | horizontals | verticals |
| --- | --- | --- |
| original `captures/screenshots/dungeon-35-entered-east-lit.png` | 256 px at y=16, 256 px at y=143 | 128 px at x=16, 128 px at x=271 |
| engine, before | 256 px at y=16, 256 px at y=48, **none at y=143** | 128 px at x=16, 128 px at x=271 |
| engine, after | 256 px at y=16, 256 px at y=143 | 128 px at x=16, 128 px at x=271 |

Same four edges, same four corners, box `x 16..271 y 16..143` on both sides. The interiors
still differ and that is expected: the CP7 fixture is a synthetic map in sector 20 while the
reference frame is the real sector 35, so the corridor contents are not the same corridor.
The frame is map-independent because it comes from the depth-0 tables, which is what makes it
a legitimate acceptance target and the interior a non-target.

CP7 ink moved 1530 to 1548, inside the gate's 400..1800 band, and the sprite control still
discriminates (520 with strokes against 406 without). CP0-CP7 green, `check_all.sh` green,
`assemble_clean.sh` green after the two header edits.

The process failure is worth naming plainly, because it is not the same one the two previous
addenda named. Those said, correctly, that looking at an image instead of measuring it produced
four wrong claims in a day. This session had the measurements and still spent four sessions on
the residual, because every measurement was of *ink present or absent in a band* and the defect
was *ink present at the wrong coordinates*. A diff that counts missing pixels in a region cannot
distinguish "not drawn" from "drawn elsewhere", and the number it returns reads like progress in
both cases. The line dump answered it in one run, and the line dump was written the day before.
The general form: when two renderings differ by a displacement, compare the primitives, never the
raster.
The transferable finding is narrower than the drama around it: **a register-carry
transcription has to model every write a callee makes to shared state, not only the writes
the caller can see.** `asm/67B3`'s header now says so, and `asm/6425` is named for what it
draws. The rename itself is not done: `tools/gen_dungeon_draw.py` hard-codes the ten asm
filenames in its `ROUTINES` list, so renaming is a tool edit plus a regeneration and is left
for the same person who does that.

## The release binary would have shipped a blank character-creation screen (found and fixed 2026-11-02)

While drafting README instructions for the player binaries (`u2-sdl-static`,
`u2-sdl.exe`, attached to a `v*` release by `release-builds.yml`) it emerged that
no release built so far would have been playable. The cause was that the two
string sources are not equal and nothing was checking which one a build got:

| source | strings | empty |
| --- | --- | --- |
| `original/ultimaii.exe` (every dev box) | 220 | 0 |
| `asm/` fallback (every CI runner, every release build) | 219, one omitted outright | **38** |

An empty literal prints as nothing, so a release binary would have shown an empty
title copyright, an empty character-creation screen (the `GENERATION`/`POINTS`/`FORM`
headings, the four races, the four classes, the sex labels, `TO PLAY ULTIMA ][`),
the seven `STREET_*` talk lines, the WEAR armour menu, the strength refusal and the
grocery question. Worse for test credibility, every assertion that greps the screen
for one of those strings would have passed while nothing was printed.

**Why the fallback loses them.** 38 of the 220 sit in regions Phase 3 rendered as
byte lists or as mis-decoded instructions rather than as `db "..."` text, so the
manifest's `run` locators have no asm-side literal to resolve against. Measured by
probing each one against the tree: 6 are in fact already quoted text and only
needed the manifest's fourth tuple element; 20 are recoverable from `db 0x..` byte
lists; 9 more are split around the game's `0x0d` newline glyph and are recoverable
by decoding mixed `db` lines; 3 are genuinely absent — `DISK_PLAYER` (its text
survives in a different file, `asm/7030_space_collision.asm:273`) and the two
`FOOD_YES_*` in `asm/17F5_sub_17f5.asm`, which has no `db` lines at all.

**That breakdown turned out to be the wrong question.** Hand-typing 35 texts into
the manifest would have fixed the *build* fallback, which nothing uses any more,
and would have copied the game's text into `extractor.py` as a third place to
drift. What actually needed the stronger read was the *verifier*, and it needs no
text at all: it already knows what to look for, because the committed header is
the thing under test. So `--verify` decodes every `db` payload of the file the
manifest names — quoted text, bare hex byte lists, and lines that mix them, which
is exactly what `DB_RE` refuses to match and for the reason that comment gives —
and asks whether the committed bytes are present in them. **214 of 220**, up from
181, with nothing duplicated to make it work. The claim it makes is the one worth
making: `asm/` assembles byte-for-byte identical, so bytes present there are
proven, not asserted.

Six are not present in any text form, in four files where the disassembler read
the string as instructions: `INTRO_BESTSELLER` (`asm/01A1`), `DISK_PLAYER`
(`asm/0764`), `Z_SPELL` (`asm/464D`), `MAGIC_SPELL_READY` (`asm/3B92`, 15 db bytes
in the file and none of them this) and `FOOD_YES_A`/`FOOD_YES_B` (`asm/17F5`).
They are named in the extractor's `UNPROVABLE` set, a string that is not provable
and not on the list is a red, and if the list shrinks the gate says so instead of
letting the constant rot.

**What was done instead, by owner ruling the same day.** The generated header is
committed. `engine/content/gen_content.h` is now tracked, the build has no rule to
regenerate it, and `make content` refuses without the exe. AGENTS.md § 0's
"convenience copy" bullet is amended to say what the rule was always protecting —
a *hand-maintained* copy — and the three enforced checks that replace the old
"never commit it" are in that bullet, in the header's own banner, and in
`engine/README.md`.

**The gate, and its controls.** `make -C engine content-check` requires every
committed string to be non-empty, every manifest symbol to be present, and every
string that asm cannot prove to be on the known-unprovable list. Controls, all six
demonstrated red before the gate was trusted: wrong text, blanked string,
shortened prefix, missing symbol, and a fallback header attempting to overwrite the
committed one. The last is refused by the extractor itself, not by the Makefile, so
it holds however the tool is run. They ship as
`engine/tests/content_header_selftest.py` and run inside CP1 and `check_all`.

**One trap found on the way, now asserted.** `CPPFLAGS` carried `-Ibuild` ahead of
the new `-Icontent`, and a stale `build/gen_content.h` from the old scheme
out-ranked the committed header — a wrong build that looked like the right one and
would have restored all 38 empties. `build/` is off the include path and CP2 now
fails if it goes back on.

## v0.0.1: the first release run, and the three things it found (2026-11-02)

A tag was cut with no claim on it except "see whether the release compiles work".
It found three, and this is what each one was.

* **linux-static: green.** The static-SDL player binary builds on a runner
  straight from the committed header, which is the property the whole header
  change existed to buy.
* **windows-cross: red at link, and pre-existing.** `time_posix.c` compiled clean
  under mingw and then failed to link, because mingw *declares* `clock_gettime`
  and `nanosleep` and the msvcrt it links has neither. So the Windows build had
  never worked, on any machine, and `deploy/winbuild/README.md` described it as
  though it had. Fixed with a real per-platform backend —
  `src/platform/time_win32.c`, `QueryPerformanceCounter` for ticks in PIT jiffies
  and `Sleep()` for delays, swapped in by `WIN_SRCS` — chosen by host OS rather
  than by mode, which is the one exception to how every other backend is picked.
  The exe now links and imports only stock DLLs. It was first **run** later the
  same day under wine (see the wine note at the end of this file), which promoted
  the tick maths and `Sleep()` behaviour from `inferred` to `observed`.
* **gate-suite had been red since 2026-10-29** on `engine build fresh`: a required
  gate with `engine/u2` as its prerequisite, in the one workflow that has no build
  step. It could never be anything but red, which is the same failure as a gate
  that can never be anything but green — it teaches everyone to ignore the suite.
  Now declared optional, and `engine-tests.yml` runs `make -C engine fresh` where
  a build exists and the check means something.

And one of my own, from the header move: `run_cp6.sh` still compiled
`commerce_check.c` with `-Ibuild`, so it passed on every dev box that had ever run
`make` and failed on a runner that had not. The CP2 assert catches that in the
Makefile and could not see it in a test script. Verified the way a runner does it
this time: `make clean`, then the whole suite.

The general form, for the harness doc: **a cross-build that only ever runs on a
developer's machine is not a build target, it is a rumour.** The link step is
where a portability lie dies, and only CI does the link step on the other
platform.

## The player binary could not open a window on any desktop (found 2026-11-02, by a person)

`v0.0.2` and `v0.0.3` shipped a Linux player binary with **no display backend at
all**. Measured off the released artifact, not inferred:

```
$ strings -a u2-sdl-static | grep -icE 'x11|wayland'
0
$ strings -a u2-sdl-static | grep -iE '^(dummy|offscreen|x11|wayland)$'
dummy
offscreen
```

Cause: SDL's cmake compiles out any video or audio backend whose dev packages are
missing, and it does so quietly. Neither `deploy/linuxbuild/Containerfile` (which
installed make, gcc, python3, cmake, ninja, curl, tar) nor the CI job (cmake,
ninja) installed a single X11 or Wayland header, so the static `libsdl2.a` had
nothing to build the backends from. `SDL_*_SHARED=ON` — the thing the Containerfile
comment cited as proof the binary would run anywhere — only defers the dependency
 to runtime; the backend still has to be configured in at build time. The comment
was not wrong about the mechanism, it was wrong about what had been built.

Why nothing caught it: every check in the pipeline tests *linkage*, and linkage was
fine. `ldd` shows no libsdl2, the static-link gate passes, the CP4 SDL gate runs
under `SDL_VIDEODRIVER=dummy` and gets a real frame — a headless test that proves
the render chain and says nothing about whether a window can exist. The only thing
that would ever notice was running it on a desktop, which is exactly what the
owner did.

Fixed in three layers, because a one-line fix here would have been the same class
of miss next time:

* both build paths install the X11/Xcb/Wayland/GL/ALSA/Pulse dev packages;
* `--video-drivers` prints what a binary can do, without needing a display, and a
  failed `SDL_CreateWindow` now prints the driver list and `SDL_VIDEODRIVER`, while
  a successful one prints `u2: video: wayland`;
* `deploy/linuxbuild/build.sh` and `release-builds.yml` both **fail the build**
  unless `x11` or `wayland` is in the artifact's list. Control run against the
  released `v0.0.3` binary: rejected. The CI SDL2 cache key went to v2, because the
  cached archive was the broken one and a green run on a stale cache would have
  hidden the fix.

Also found on the way: `build.sh --rebuild` passed `--build` to `podman build`,
which is not a flag it accepts, so **the rebuild path had never once worked** in
either container script. Nobody had needed it until now. Fixed to `--no-cache`.

The lesson is narrower than "test on a desktop", and more useful: **a build gate
that inspects the build's inputs is not a gate on the artifact's capability.** Ask
the artifact. It is usually one flag away from telling you.

## Both player binaries have been played; Windows under wine (owner, 2026-11-02)

The owner ran both released binaries on a Bazzite Linux machine and reports the
same play through each: starts, creates a character, plays the overworld, enters
towns, fights. The Windows `u2-sdl.exe` did this **under wine**, and it loaded a
`PLAYER` record written by the Linux build.

What that promotes, and what it does not:

* `time_win32.c` was written on the strength of a linker error and an API
  contract, and the docs said so. It now runs, and the game runs on it.
  `QueryPerformanceCounter` ticks and `Sleep()` delays move from `inferred` to
  **observed**.
* The console-subsystem link change (`-mwindows` dropped) is what let wine keep
  the stderr at all, so the diagnosis path that motivated it is real.
* Save portability had only ever been checked engine-against-original
  (`tools/save_cross_load.py`). Linux-written to Windows-read is a new axis and it
  passed, which is worth having on record even though the record is a byte layout
  and nothing in it should have differed.
* **Wine is not Windows.** It implements the Win32 API, not the graphics, audio and
  filesystem layers underneath. A real GPU path, a real sound device and real
  Windows filesystem behaviour are still untested, and no amount of wine runs will
  test them. `deploy/winbuild/README.md` says this rather than letting "works under
  wine" read as a Windows sign-off.
* Grade: **owner observation**, not a capture. There is no `captures/` directory
  for the wine run — no stderr log, no frame. If it is ever filed, the claim gets
  stronger; until then it is what a person reported, which outranks inference and
  still is not an artifact.

## Two owner-reported bugs, both real, both the same root cause (2026-11-02)

The owner reported that the engine puts moongates inside Port Boniface, and that water surrounds the
south exit of Port Boniface. **Both are engine defects, both were reproduced, and both are the same
mistake: a rule measured on the world map was applied to a site sector.** Capture:
`captures/spice86/s31-south-edge/`, with the measurements and the per-row colour census in its README.

**Defect 1 — time doors inside a settlement.** `engine/src/game/gates.c` stamps the active door with
no site-kind test, and advances the countdown wherever the player is. The original does neither:

* With the rotation armed inside sector 31, the town grid holds **zero `0xC0` cells**. The four era-3
  table cells hold `0x70` paving and `0x78` the player stamp. The control, run in the same session in
  sector 30 at the same era and slot, produced exactly one `0xC0` at the table cell `(33,23)`.
* **The countdown does not tick inside a site at all.** `DS:6F` poked to `01` stayed `01` across six
  moves inside sector 31, with `DS:6E` fixed; the same poke in sector 30 advanced the slot within four
  moves. Two independent mechanisms, each failing on its own, which is why the fix is two gates and not
  one.

**Defect 2 — the south edge draws the north edge.** `engine/src/core/view.c` masks both axes with
`& 63` unconditionally, and its comment justifies that with a single capture taken in sector 20, a
world-map sector. The tree already names the correct rule: `asm/5AB1_view_origin_by_mode` branches on
`DS:12`, and only the world-map arm (`asm/5B98_sub_5b98`) applies `and $0x3f`; the interior arm
(`asm/5ABB_sub_5abb`) computes the same origin deliberately unwrapped. Measured in the town, the rows
past the last grid row are **one constant filler tile** — identical colour census across four rows and
two frames — and the town's north sign band never appears at the bottom, which is exactly what a wrap
would put there. The engine renders the wrapped signs and water; the owner read that as a sea wall.

**Why the wilderness could not have found this.** `mapx30` rows 0, 1 and 60..63 are all `0x00`. Every
sector measured before today has water at both Y edges, so wrapping and filling are pixel-identical
there. A settlement is the only sector whose north edge is not water, which makes it the only place
the two hypotheses can be told apart. The one capture that had ever been taken inside a town settled it
in one frame.

**Also confirmed, and it was only inferred before:** the town arrival cell. Pressing `E` on the
wilderness mouth produced `DS:00/01 = (31,62)`, which is what `overworld.c` hardcodes. And stepping
south from the town's last row returns the character to the wilderness mouth `(34,20)` with the sector
back to `30` — the engine's `u2_site_edge_crossed()` and `u2_overworld_site_leave()` already match this.

**Fixed 2026-11-02, same day, after the owner approved it.** Three changes, not two — verifying the
first one is what found the third:

* `engine/src/core/view.c` now branches the way `asm/5AB1` does. `off_map == 0xFF` is the world map
  and wraps on both axes; anything else is a site, and a cell outside 0..63 draws
  `U2_VIEW_SITE_OFFGRID` (`0x08`). The 8-bit origin arithmetic is kept, so an origin below zero
  classifies as off-grid by `>= 64` exactly as the asm's `cmp $0x40 / jae` does.
* `engine/src/game/gates.c` gained `gate_is_on_world_map()`, and both the stamp and the countdown
  consult it. Two call sites because the original has two mechanisms.
* **The third defect, found by trying to prove the first one.** `u2_overworld_init_state` set
  `view_mode = 0xFF` unconditionally, so a save loaded inside a settlement came back in world-map
  mode and wrapped anyway — the town render looked unchanged even after the viewport was fixed. And
  `u2_overworld_site_leave` never reset it, so walking *out* of a town would have left the world map
  wrapped-off. `asm/0C1F` sets `DS:12` to 4, reads `DS:4A`, and falls through to `asm/0C2B` — which
  sets 0xFF — only when that digit is zero: **the mode is a function of the ones digit, not an event
  that site entry causes.** Both paths now derive it from the digit.

**Verification, and a gate that was shown to fail.** With the record loaded, the engine's frame of
sector 31 at `(31,62)` now matches the original's on **all ten viewport rows**, on all four CGA
colours, counts included — rows 7, 8 and 9 read black 4900 / cyan 220 in both. `tests/view_check.c`
gained the site-mode assertions and `tests/gate_check.c` the two settlement refusals, each with its
world-map control so a refusal cannot be passing for the wrong reason. `tests/site_viewport_gate.py`
is new: it builds its own Port Boniface save, renders it, and asserts the three off-grid rows agree
with each other and carry no glyph pixels — the property that distinguishes filler from wrap. Its
`--selftest` has five controls, and the gate was additionally run against the real mutation
(`torus = 1`, the pre-fix behaviour): it went red with `row 7 has 1420 grey pixels`, which is the
exact census of the wrapped sign row measured earlier, and `CP4` went red with it. Both were restored
and the mutation is not in the tree.

The filler's identity: **map byte `0x08`, art index `0x02` — the sector's own ground tile.** Proved by
bitmap match, not inference: the off-grid cell cropped from `03-at-row63.png` is identical to
`tmp/tiles/tile_02.png` at Hamming distance 0, and in the same frame a real town `0x08` cell and an
off-grid cell have the identical census (245 black + 11 cyan each). The owner saw this before we
measured it and called it grass, which is what it is. The same pass measured the byte→art mapping
inside a site for the first time — 16 distinct map bytes, every one at `byte >> 2`, distance 0 — so
`docs/art-index.md`'s transform holds on both sector classes. G57 is closed.

**Tooling change that came out of this, and is in the tree:** `--dump-map` used to render only the fixed
probe sector, so a frame from a loaded character could never be put beside it. It now renders from the
record when one is present, which is what made the side-by-side above possible at all.

## "It crashed" was a menu that never reached the screen (owner-found, 2026-11-02)

The owner bought a mace in Linda, pressed `R` to ready it, and reported a crash. **Nothing crashed.**
The READY WEAPON menu was printed and then the handler blocked for a digit, and the menu did not
appear until after the digit was typed — so the window sat unchanged while the game waited for a key
the player could not see a menu for. `W` did the same, and he found that himself. `cmd_M` had the
identical defect and nobody had noticed it.

**Why.** The original writes text to `0xB8000` and the CRT is always displaying it, so it has no
present-before-read step anywhere. A port with a back buffer does, and the presenting loop sits
*above* the command handler: `u2_overworld_loop` draws and presents, then dispatches the letter, and
the handler's own output is not shown when the handler returns to the loop — it is shown on the *next*
iteration, which is after the blocking read has already been satisfied.

**The contract was documented and not implemented.** `core/ctx.h` said of `present_now`: "seam_key_wait
calls it before blocking". It did not, and never had. That stale sentence is why three handlers were
missing the call while the shops were not: the reader of the comment would look for the fix in the
wrong place, and commerce.c had already patched it locally at its own sites.

**The fix is per-site, and there is a reason it is not general.** `seam_key_wait_shown()` now lives in
the seam layer and is called by `cmd_R`, `cmd_W`, `cmd_M`, the YELL prompt and (through
`show_and_wait`) every shop. Folding the same behaviour into `seam_key_wait` itself was tried before
and **broke CP4**: presenting from the shared wait path also runs at the title screen, where
`--dump-screen` sets its done-flag during the flush and the title flow then loops on a no-key result
forever. That history was in commerce.c's comment where only the shops could benefit from it; it is
now on the helper in `seam.h`, where the next person adding a prompt will find it. `creation.c`'s waits
stay on the plain seam for exactly that reason.

**Gate.** `tests/prompt_gate.py` reads the engine's trace and asserts that between the prompt's
`str_out` and the following `key_wait` there is a present — the user-visible property, not a proxy for
it. Three cases (R, W, M), five selftest controls all demonstrated red, and the gate was run against
the real mutation: reverting `cmd_R` alone made it say `prompt at line 64, read at line 140, nothing
presented between: the player types blind` and nothing else. Wired into `make test-cp6`.

**The general lesson, which is the second one this week.** A handler that asks the player something
must put the question on screen before it blocks, and in a back-buffered port that is a call someone
has to make. Any new `WHICH?`-style prompt needs `seam_key_wait_shown`, not `seam_key_wait`.

## The splash pictures: the engine now shows them (owner-found, 2026-11-03)

The owner noticed the engine's splash has no graphics. It did not, deliberately: `title.c` said the
original loads PIC picture files during the intro and "the engine has no bitmap content pipeline
until CP9, so the text screens stand alone." The CP9 pipeline turned out to be a non-event.

**There is no image format.** The files are literal images of CGA mode-4 video memory, and the
engine's `u2_frame` already stores one CGA index per pixel, so the "decoder" is a planar unpack of
16,000 bytes into 64,000. Even scanlines live at offset 0, odd at 8000, two bits per pixel, leftmost
pixel in the top two bits.

**What is not established, and is written down rather than papered over.** `asm/5351_sub_5351.asm` is
`mov $0xb800,%ax ; mov %ax,%ds`, and 0xB800 in mode 4 is the framebuffer, so the game certainly aims a
read at video. But `asm/535A_sub_535a.asm`, which follows it, restores `DS` and asks for **one** FCB
record into a fixed `DS:0x245` — not 16,384 bytes. Either the picture arrives by a path not yet
identified, or it arrives in many one-record reads with the buffer advanced by a caller nobody has
found. Both fit the bytes and neither is proven, so both asm files keep their placeholder names and
their headers say which half is inferred. The engine does not need the answer: it reads the file itself.

**Proven, not assumed.** Decoding `picdra` with that layout reproduces a live captured frame of the
original's splash at **100% colour-class agreement over 4000 sampled pixels**, and the engine's own
`u2_pic_blit` produces a frame whose census is **identical** to the capture's (13604 black / 1567 cyan
/ 539 magenta / 290 white, sampled every second pixel). The only difference is the engine's white being
255 where the emulator renders 170 — the same engine-wide palette choice already recorded against the
town comparison, not a picture problem.

**What the original actually shows, and the grade on each:**

| sequence | picture | grade |
| --- | --- | --- |
| intro, after the second presentation screen, held ~5 s | `PICDRA` (the Ultima II dragon logo) | observed — five consecutive one-second frames matched at 100% |
| 'D' demonstration, first couplet | `PICSPA` (starfield) | observed at 1 s sampling |
| 'D' demonstration, remaining couplets | `PICMIN` (the pyramid) | observed at 1 s sampling; **which couplet switches it is inferred** |
| `PICDNG`, `PICTWN`, `PICOUT` | referenced by name in the executable | **where they appear is not established** |
| `PICCAS` | referenced at file offset `0x6ba` | **has no file in the pack** — a load that can never find its data |

**Gate.** `tests/pic_check.c`, 16 checks, wired into CP4. The interesting part is what a census
*cannot* do: every candidate layout for these bytes is a permutation of the same 16,000 bytes, so the
global colour census is **identical** for the correct interleave, for the two fields swapped, and for a
naive non-interleaved read (all three give 52433/6061/4370/1136). A test written on the global census
would pass on a decode that drew the picture combed. What separates them is placement: scanline 4
carries eight magenta pixels and scanline 5 carries none under the correct layout, and swapping the
fields moves those eight to scanline 5. The gate asserts that by name, plus four quadrant censuses.
It also asserts a missing file is a clean refusal that leaves the frame untouched, and that `piccas`
is still absent — so if a future pack adds it, something notices.

**What is NOT gated, said plainly.** The `title.c` wiring cannot be exercised by any automated path
today. `--headless` installs the null renderer, which has no `present_frame`, so a headless run
correctly declines the picture; `--dump-screen` ends the run at the first presented frame, which is
`INTRO_1`'s text, three screens before the splash; and `--script` requires `--headless`. So the decode
is gated and the call site is not. Confirming the splash on screen needs a person with a window, or a
`--dev` affordance to dump the Nth frame rather than the first.

**Copyright posture unchanged and worth restating:** the pictures are read at runtime from the
player's own `--data-dir`, exactly like the tile art. Nothing is committed, nothing is substituted, and
with the file absent the text screens are byte-for-byte what they were before this existed.

## Resizing the window now scales the picture (owner-found, 2026-11-03)

The owner asked whether SDL supports scaling, because dragging the window left the graphics
unchanged. It did support it, and the backend already asked for it: `SDL_RenderCopy(ren, tex,
NULL, NULL)` with a NULL destination means "fill the output rectangle", and
`SDL_HINT_RENDER_SCALE_QUALITY=0` was set before the renderer existed so the upscale is
nearest-neighbour. Three separate defects stood between that and what the player sees.

1. **Nothing re-presented.** `input_sdl.c` pumped events but handled only `SDL_QUIT`,
   `SDL_APP_TERMINATING` and `SDL_KEYDOWN`. `SDL_WINDOWEVENT_SIZE_CHANGED` was dropped, and the
   game presents only when it has something new to draw — a resize is not something the game
   says. So the window kept the stale frame until the next keypress, or forever on the title
   menu. This is the whole reason it looked like SDL was ignoring the resize.
2. **The stretch distorted.** A NULL destination fills whatever aspect the window happens to
   be, so square source pixels became rectangles.
3. **No HiDPI.** Without `SDL_WINDOW_ALLOW_HIGHDPI` the drawable is not the window size on a
   scaled desktop, so the scale factor was wrong in a way the player cannot name.

**The fit is integer, and that is a deliberate cost.** The source is 2bpp CGA: a pixel is the
smallest unit the art has. At a fractional scale some source pixels become 3 device pixels and
some 4, which on the dragon logo reads as the outline wobbling and on 8x8 font cells reads as
text with uneven strokes. An integer scale makes every source pixel exactly `s x s` device
pixels; the price is letterbox bars, which the original on a real CGA monitor never had and
which every emulator of this era imposes anyway.

**Below 1:1 the rule inverts**, and returns `scale = 0` to say so. If the window is smaller than
320x200 there is no integer scale that fits, and the only alternative to a fractional fit is
cropping. Squashing hides nothing and the player can drag it wider; cropping loses content.

**The geometry is in `core/scale.c`, not in the backend.** Same principle as the palette and the
do-not-draw rule: a renderer may move pixels, it may not decide what they should look like, and
two backends each inventing their own fit would drift. The side benefit is that it is pure
integer arithmetic with no SDL dependency, so `tests/scale_check.c` gates it in the default
cc-only build with no display and no SDL installed, against the same code the SDL path calls.

**Gate.** 13 named cases plus a sweep of roughly 52,000 window sizes checking four invariants:
the scale is maximal for the output, the aspect is exactly preserved (cross-multiplied, no
floats), the image is centred to within a pixel, and the rect never escapes the window. Four
mutants each turn it red — per-axis scale (17,707 sweep failures), height-only scale (18,921),
no centring (43,496), crop-instead-of-fill (14,738). The first draft of the test was itself
wrong twice and both are worth recording: the centring tolerance assumed the spare pixel went
left instead of right, and the sweep printed an `ok` line unconditionally, so it reported
success on the same screen as 21,732 failures. A check that congratulates itself next to a wall
of red is the same defect as a gate that cannot go red.

**Not machine-verified, said plainly.** That the resize event repaints, and that the result looks
right, needs a person with a window: the dummy video driver creates a fixed-size window and there
is no flag to resize one. The arithmetic is gated; the event plumbing is not.

**This fix surfaced a bigger question, and it is now written down.** Chasing why the local SDL build
and the release SDL build differ led to the discovery that no SDL2-versus-SDL3 decision was ever
recorded, and that the host "SDL2" is `sdl2-compat` over SDL3 while the release pins real SDL2
2.32.8. The evaluation, the measured port surface, the option that SDL2 already had integer scaling
via `SDL_RenderSetIntegerScale()`, and a recommendation are in `docs/sdl2-vs-sdl3.md`. Nothing in
that document is implemented.

## Build honesty: option C of the SDL decision (2026-11-03)

The owner chose "C then B" from `docs/sdl2-vs-sdl3.md`. **C is implemented; B (the SDL3 port) is
not, and remains an open owner decision.** Read that document's § 8 for the full record; this is the
index entry.

What shipped: `engine/tools/sdl_probe.sh` (decides and names the SDL a build links), its 12-control
fixture suite `engine/tests/sdl_probe_selftest.sh` (wired into `check_all`), a build stamp on every
target, `--version`, four new `--version` assertions in CP0, the CP4 SDL gate preferring
`u2-sdl-static`, and a release workflow that stamps the tag and refuses an artifact that cannot name
it.

The one worth knowing cold, because it is the recurring shape in this project: the strict-mode
refusal was first written into the object-compile rule, which make skips when objects are current, so
`make sdl U2_STRICT_SDL=1` printed the warning and exited 0 on a dirty tree. It now lives in a phony
`sdl-probe-check` prerequisite that always runs. Same class as `asm_body_guard` comparing `HEAD` to
itself in CI: **a check that only fires under conditions you do not control is not a check.**

Deliberately not done: the 2.32.8 → 2.32.10 pin bump. That changes the release artifact and belongs
with the SDL-major decision, not before it.

## The engine moved to SDL3 (option B, 2026-11-03)

Follows immediately from the section above; read `docs/sdl2-vs-sdl3.md` § 9 for the full record.
The owner chose replace-outright over dual-backend, so **there is no SDL2 path in this tree any
more**. Release artifacts pin SDL3 3.4.18 from the official tarball, static, in all three build
paths.

Two changes are worth knowing cold because neither announces itself:

- **SDL3 textures default to linear filtering.** `SDL_HINT_RENDER_SCALE_QUALITY` is gone. A port
  that missed this still draws, still passes every colour-class census test in the tree, and shows
  the player smeared 2bpp CGA. It is now an explicit
  `SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST)` that warns if refused.
- **`SDL_InitSubSystem` returns `bool`, where SDL2 returned 0 for success.** Copied over unchanged
  it inverts the meaning in `input_sdl.c`, where the failure path is deliberately quiet.

Also: `SDL_RenderTexture` takes float rects (the integer fit from `core/scale.c` is widened at the
call site, because the integer version is what `tests/scale_check.c` can gate); the resize event
split into `RESIZED` and `PIXEL_SIZE_CHANGED`; and `sigaction` needs a per-file `_POSIX_C_SOURCE`
now that `pkg-config` no longer supplies `-D_GNU_SOURCE` the way `sdl2-config` did.

`SDL_LOGICAL_PRESENTATION_INTEGER_SCALE` exists and is deliberately unused — see § 9 for why.

**Confirmed on real hardware by the owner, 2026-11-03, before v0.0.8:** both release artifacts were
played end to end — `u2-sdl-static` on a Linux desktop, `u2-sdl.exe` on real Windows rather than
wine. First time the shipped artifacts were verified on their target platforms instead of inferred
from a cross-build.

**Still open: sound, partially.** The owner confirmed on 2026-11-03 that the **Linux** build
produces sound, "at least partially" — which is data, not a guess, and it retires the worry that
the SDL3 stream-audio port was silently silent. It is not a full confirmation: which tones fire has
not been enumerated, and the Windows artifact is unconfirmed on real Windows. The reason this stays
on the record rather than being ticked off is that silence passes every automated check in the tree,
so the only evidence available is a person listening.

### CP7 addendum: the asm/6078 lead is refuted, and what is left is a contradiction (2026-11-04)

The side-by-side at the matched state (sector 35, (5,5), east, lit — both sides verified from
live memory, original `DS:00..03 = 05 05 01 00`, engine `--dump-state` `pos 05 05`, `sector 03 05`)
closed the outer rectangle on both sides but still disagreed inside it. The lead recorded there
was that `asm/6078` builds the three depth columns differently from the engine's `dg_scan_build`,
or that `asm/6144` increments `[DS:482]` somewhere the engine does not. **Both halves are now
refuted, and the residual is worse than a wrong formula: the original's own columns and the
original's own frame do not agree with each other.**

**The columns, read live.** `tools/spice86_type.py --peek` against a fresh run driven the player's
own route (sector 30, mouth cell (25,5), `E`, then `I`), sampled with a tight poller at ~1100
request-pairs/s so the values are the ones the last render left behind:

```
pos=05 05 01 00   floor=00
[DS:48F] left    80 80 c0 80 00 80 80 80
[DS:497] centre  10 00 00 00 00 00 00 c0
[DS:49F] right   80 80 80 80 00 80 c0 80
```

`dg_scan_build`'s formula, applied to `mapx35` page 0 with `asm/68C0`'s exact addressing
(`x,y` masked `&0x3f`, offset `(y*16 + x) & 0xFF`, page base `[DS:472] = 0x1800` plus `floor*256`)
produces **the same three rows, byte for byte**. The engine's scan is not the bug. The live grid at
`DS:1800` is also byte-identical to `mapx35` page 0, so the inputs are the same bytes.

An earlier capture, `captures/spice86/dungeon-35-live/live-ds.bin`, has columns that walk *west*
from (5,5) while its `DS:02/03` reads `01 00`. That is not a second rule, it is that capture being
what the docs already said it was: a save loaded straight into sector 35, whose arrays belong to a
different render than the state bytes harvested with them. Brute-forcing every start cell and
direction against those arrays gives a unique answer (`W from (5,5)` for the centre, `W from (5,6)`
left, `W from (5,4)` right), which is exactly what a stale west-facing render would leave. Do not
use that capture for renderer work.

**The increment, read from the bytes.** `asm/6144` increments `[DS:482]` in two places: `0x6385`
in the rock-ahead path, after `asm/648D`, and `0x63FE` at the end of the open path. The second
`call asm/6425` is at `0x635F`, *before* either increment, so it draws at the depth it was already
at. The engine's `dg_draw` does the same. No difference.

**The contradiction.** With those columns the engine draws rock-side shapes at depth 0
(`dg_6377`/`dg_63ce`, whose first line is `(XL[d],YT[d]) -> (XL1[d],YT1[d])`, a receding diagonal)
and no depth-1 frame, because `centre[1] = 0x00` is open. The original's frame does the opposite,
and this is measured, not eyeballed — the window contains exactly one colour, `(170,170,170)`, so
there is no dim line hiding from a threshold:

| line | original | engine |
| --- | --- | --- |
| `asm/6377` d0 top, (16,16)->(80,48) | 2/60 (absent) | drawn |
| `asm/63ce` d0 top, (271,16)->(207,48) | 2/60 (absent) | drawn |
| middle of y=48, x 80..207 | 61/60 (present) | absent |
| middle of y=111, x 80..207 | 61/60 (present) | absent |
| ladder segment y=24 (ceiling) | 0/60 | drawn |
| ladder segment y=135 (floor) | 61/60 | absent |

The middle horizontals at y=48 and y=111 spanning x 80..207 are `XL1[0]..XR1[0]` at `YT1[0]`/`YB1[0]`,
which is the depth-1 frame, and the only thing that draws a frame is `asm/6425`. So the original
drew a depth-1 frame, which per `asm/6144` requires a rock centre at depth 1 — and its own live
`centre[1]` is `0x00`. And it drew the open-side shapes while its own live side arrays say rock.
The ladder is a vertical mirror of the engine's, with identical x extents, which points at
`asm/6594`/`asm/660C` being assigned to the opposite bits or transcribed mirrored.

**What that means, stated without padding.** Either the arrays `asm/6144` branches on are not the
arrays `asm/6078` fills the way the header labels them, or the columns a render uses are not the
columns that survive to be polled. Both are questions about a moment, and polling cannot answer a
question about a moment. The instrument for it is a breakpoint at `asm/6144`'s entry
(`L_006344`) dumping `[DS:48F..4A6]` at the instant of the dispatch, and **`tools/bp_read.py` cannot
arm on the current Spice86 build**: `--selftest` fails its positive control, arming on the address
the CPU is already executing —

```
selftest: CPU is at 17D:715
  positive control armed on the live address: False
  SELFTEST FAIL: trap did not arm and stop on an executing address
```

— so `breakCsIp` is refused even in the case that must work. Until that is fixed, or an equivalent
stop mechanism is found, the renderer's remaining difference cannot be attributed and no engine
change is justified by it. Fixing `bp_read.py` against this Spice86 is the next step, and it is a
tool fix, not a renderer fix.


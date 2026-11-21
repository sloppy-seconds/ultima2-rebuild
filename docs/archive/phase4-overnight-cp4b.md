# CP4b overnight task list — owner-approved work queue

> **ARCHIVED 2026-10-22 — queue complete.** The morning report is § Morning report
> below, and the summary landed in the CP4 row of `docs/phase4-state.md`. One item (T5)
> was held for the owner and its pre-approval flag is still unresolved. Do not resume
> work from this file.

Drafted 2026-10-20 evening, amended with the owner's time/food model.
Work through in order. Each task commits independently; `make test` must be
green at every commit. If a task's evidence is insufficient, STOP that task,
write findings into `docs/phase4-implementation.md` § CP4b as an open
question, and move on — never guess a mechanic (the doubling-bug rule).

Owner recollections are treated as design intent but must be confirmed
against `asm/` before implementation; where the bytes disagree, the bytes
win and the disagreement gets written down.

## Rules of engagement (standing, from AGENTS.md + kickoff)

- Project root only; no sibling directories. `asm/` + `disasm/redecoded/` are
  the evidence sources; raw-exe byte reads only to resolve a redecode
  ambiguity, and say so in the commit.
- Never heredoc a script; write file, syntax-check, run. Edit with the edit
  tool only. Scratch in `/var/tmp`.
- No subagents (GPU contention, owner directive).
- Zero game content committed; screenshots stay gitignored.
- CP2 evidence baseline: moves allowed ONLY when a change is evidence-backed,
  and each move gets a commit line + a gate-log entry naming the old and new
  digest. [OWNER: confirm pre-approval, or hold any task that moves it.]
- If everything below is done or blocked, stop and write a morning summary in
  this file under `## Morning report`. Do not invent new scope.

## T1 — Passability first, then the true crossing model (gate unblocker)

Recon 2026-10-20 evening reframed this task. Proven:

- Walk loop (file 0x0B36..0x0BF1, fully re-decoded): each direction arm is
  `inc/dec [DS:00]/[DS:01]` -> str_out direction word -> `call 0x2423`
  (passability, AL = travel mode from [DS:15], stored to [DS:29]) -> if
  result != 0: undo the step, echo `--INVALID MOVE!`.
- `asm/2423` head: `[0x29]=al; if [0x4A]==0: JUMP 0x248B` (overworld path,
  file 0x268B). The edge checks at 242D/2434 and the reload at 243E run
  ONLY when DS:4A != 0 — they are the SITE-map exit path (walk off a town
  map -> restore entry pair, set site kind 0, reload the sector map).
- Overworld path (file 0x268B..0x2743, re-decoded but NOT yet understood):
  branches on travel mode ([DS:29] & 0x7F): mode 2 applies a BCD -5 to the
  HP pair (das chain at 0x2695..0x26AC) with a jump to runtime 0xE62 on
  borrow (death?); mode 0x2E checks [DS:D6] and prints a message; a check
  of [DS:26B] guards a "move only on foot" message; [DS:13] compared to
  0x28. Every one of these needs a careful read before any of it is
  implemented.
- Sector arrival machinery: [DS:6E] is `2*sector + parity` (asm/26B7
  compares [DS:6E]>>1 to [DS:49]); asm/2744 steps it by 2, masks &7, and
  looks entry (x,y) from the cs:0x2798 table (file 0x2998) indexed
  `8*[DS:49] + [DS:6E]` -> [DS:23]/[DS:24], then calls 0x52B3 (player
  stamp). Table first bytes: 1d 38 1f 38 21 38 23 38 22 08 2f 1c 24 38
  14 25 10 10 30 18 2a 18 13 34 38 34 1c 0c 21 17 14 34 12 17 32 22 34
  18 08 0b — ends ~0x29BB (36 bytes, code resumes); the index formula and
  extent must be reconciled in T1d (8*sector+dir would need 80).
- Owner recollection (2026-10-20 night): west from the start sector shows
  more map; he assumed seamless scroll into the neighbour. CONFIRMED
  DISTINCTION by owner: town/site edge = FULL transition (reload, entry
  restore — matches asm/243E exactly); overworld edge = SMOOTH continuation
  into the neighbouring sector. The smooth mechanism is what T1a must find
  in the 0x248B path or its callees; if the bytes disagree with "smooth",
  the bytes win and the disagreement gets written down. Vehicles later:
  frigate (water), plane (any) — the sector system stays relevant for them.
- Edge probe: mapx20 and mapx30 are ringed almost entirely by index-0 tiles;
  many other sectors have mixed edges (land links or impassable mountains —
  unknown until passability is decoded).

Tasks, in order:

- T1a: decode the overworld passability path at file 0x268B end to end (and
  whatever it tail-calls): where the tile under the target cell is read
  (note asm/50B3 indexes by [DS:23]/[DS:24], NOT [DS:00]/[DS:01] — find
  where 23/24 are synced from 00/01), what makes a tile passable per travel
  mode, and what happens at the wrapped edge (x=0xFF indexes where?).
  Settle the art-label question here: the kickoff calls art 0 "water
  (magenta dashes)" but renders show the magenta dotted field as walkable
  land; the passability table is the arbiter, not the label. Cite every
  blocked tile id to an address.
- T1b: implement passability in `u2_overworld_run` for travel mode on foot:
  rejected step is undone and `--INVALID MOVE!` echoed through the CMD
  window (string proven in the movement loop). This alone makes the starting
  sector fully walkable and the boundary a natural wall.
- T1b2: implement the SMOOTH overworld crossing per the T1a decode: sector
  digit steps, map reloads, position continues at the opposite edge so the
  player-centred viewport makes it seamless. Missing sector files (the
  shipped gaps) must behave per the bytes — if the bytes offer no rule for
  a missing neighbour, refuse the step and document it, never invent.
- T1c: with passability in, re-probe every `*x0` sector: can a foot traveler
  reach an edge anywhere? If yes, implement the `243E` semantics faithfully
  (entry restore, site kind 0, reload) — the bytes do NOT support a
  neighbour-digit walk-over, so do not invent one; document what reaching an
  edge does per the bytes.
- T1d: dump the `cs:0x2798` arrival table (file `0x2998`, 8 bytes per sector)
  and find the writers of `DS:6E`; write up as the vehicle/arrival contract
  for later CPs. No vehicle implementation tonight.
- Tests: passability unit checks (blocked ids refused, walkable accepted,
  echo correct); edge probe output into the implementation doc.
- Acceptance: CP4 gate is walkable — owner can walk around sector 20 fully,
  water blocks with the original's echo; crossing question answered by
  evidence and written down, not guessed.

## T2 — Time passes: action tick, food drain, auto-PASS (owner unlock, 2026-10-20 night)

Owner's model: every action = one time unit; idle for a short period =
automatic PASS (the P action, which is itself an action); each action drains
a small unit of food; food at zero starts draining HP until DEATH. Save
already round-trips the whole record, so a drained food value persists
across Q/P automatically — that half needs no new work, only a test.

Recon first (recollection -> bytes):
- The main input loop (file `0x0B36..0x0BF1`): the PASS arm at `0x0B48`,
  `asm/3DBC_cmd_P`, and whether the loop times its key wait (INT 1Ah via the
  `asm/52CA` timed_wait seam) to auto-PASS on idle.
- Food/HP writers: every write to `DS:53/54` (food BCD pair) and `DS:51`
  (hp) — grep asm + redecoded ranges; find the per-action decrement and its
  size (one BCD unit? one food byte = hundred units?).
- The starvation chain: food==0 -> hp decrement site, and the death path
  (grep `db "DIED`, `DEATH`, `GAME OVER` — the fan screenshot `death.png`
  exists in captures, gitignored, as a visual cross-check).
- `asm/08ED` (called by the P path and the function-key speed control) and
  `DS:74` (adjusted ±0x200 bounded by `DS:554`/`0x3a00`) — check whether
  this is the time/speed counter.

Implementation (only what the bytes confirm):
- A tick function in the overworld loop: after each accepted action (move,
  PASS, later combat) apply the proven food decrement; at food zero apply
  the proven hp drain; on hp zero run the original's death path (or, if the
  death flow is out of scope, stop at hp==0 with a documented open
  question).
- Auto-PASS on idle: if proven, drive it from the clock seam so headless
  stays deterministic. DECISION POINT: if firing auto-PASS under the script
  clock would move the CP2 baseline via WAIT semantics, gate auto-PASS to
  real-clock backends and record the choice. If [not pre-approved], hold the
  auto-PASS sub-part and ship the action tick only.
- Tests: tick drains food by the proven amount; food 0 drains hp; drained
  food survives Q -> P (extends `save_q_check`); auto-PASS fires after the
  proven idle interval (real-clock path).
- Acceptance: walking around the map slowly eats food, visible in the status
  band; starvation works; every constant cited to an asm address.

## T3 — Window-close X ignored in the overworld (owner, confirmed)

`sdl_poll` returns -1 and sets `exit_code`; `seam_key_poll` collapses -1 to
0 and sets `input_exhausted`; the title flow checks `flow_stopped()` so X
works at the menu, while `u2_overworld_loop` checks neither and spins on.

- Fix in `u2_overworld_loop`: after a no-key poll, break when
  `ctx->exit_code || ctx->input_exhausted`. Do NOT change `seam_key_poll`'s
  return contract — callers test it with `!`, where -1 reads as "key got".
- Check the creation flow's wait loops for the same gap.
- Acceptance: X closes mid-game with exit 0; CP3 scripted tests green.

## T4 — SIGTERM defect (confirmed live 2026-10-20)

A stale `u2-sdl` process survived SIGTERM (kill) and only died on SIGKILL.

- Reproduce with `timeout -s TERM`; determine whether SDL installs the
  handler (`SDL_HINT_NO_SIGNAL_HANDLERS` behaviour in the brew build) or the
  event is queued but never drained.
- Fix in SDL init (likely: set the hint and install our own async-signal-safe
  flag handler checked in `sdl_poll`).
- Acceptance: `timeout -s TERM` exits with our code, not 137; root cause in
  the commit; proof line in the morning report.

## T5 — Creation tail: straight into the world

Original: after confirm, sets entry pair, saves, clears, prints the
"TO PLAY ULTIMA ][" prompt, `call 0x300`. Engine returns to the menu.

- Extract the prompt string (redecoded 5400 tail, file 0x5C1F region).
- On confirm: original's tail, then `u2_overworld_init_state` + loop (bitmap
  backends only; terminal keeps current flow).
- MOVES THE CP2 BASELINE. [OWNER: pre-approved?]
- Acceptance: C -> create -> world at (20,20) sector 20; CP3 green or
  updated with named reasons; baseline regenerated + logged.

## T6 — SDL dummy-driver segfault -> headless SDL test

`SDL_VIDEODRIVER=dummy ./u2-sdl --sdl` segfaults in `sdl_upload` via the
flush path.

- Root-cause (verify, don't guess: suspect renderer/texture creation order
  or the accelerated->software fallback on dummy).
- Then wire `--dump-screen` into `run_cp4.sh`: dummy run of the title flow
  dumps the first presented frame; assert PPM size + ink. SKIP when
  `u2-sdl` is not built.
- Acceptance: `make test` proves the cell->font->upload path headlessly.

## T7 — Screenshot differential test (standing directive, first attempt)

- `tools/screenshot_diff.py`: crop the DOSBox game area from
  `captures/screenshots/move-after.png`, downsample to 320x200, derive the
  40x25 colour-class grid; compare the status-block rows (20..23, cols
  30..39) against a headless engine render with the same BCD values. Map
  region out of scope tonight (sector/position differ).
- SKIP with named reason when images or PIL are absent.
- Acceptance: runs in `make test` (PASS or SKIP); proven to catch a
  deliberately blanked band (one manual poison check, noted in commit).

## T8 — Documentation pass (always last)

- `docs/phase4-implementation.md` § CP4b: font landed, CMD model, P->world
  contract, DS:4A correction, Q save contract, sector rule (T1), time/food
  model (T2) with every citation.
- `docs/phase4-state.md`: CP4 row, resume notes pointing here, gate-log
  entries for baseline moves.
- Morning report section below: shipped (commit list), open, owner decisions.

## T9 — Blog post series + sailboat retro (only if T1–T8 are done or passed)

Goal: turn this stretch of work into a publishable series of blog posts plus
a lessons-learned retro framed as a **sailboat retro** (anchors = what held
us back; sails = what drove us forward; rocks = hazards we hit; currents =
conditions of the project; island = where this is heading).

Source material (the implementation log is the journal; plus one owner-
granted exception):

- `~/.pi/agent/sessions/--var-home-paul-Projects-decompile-u2-v2--/*.jsonl`
  — the pi chat transcripts for THIS PROJECT ONLY (owner permission
  2026-10-20 night, scoped to the blog/retro task). Read-only, bounded:
  never open other projects' session directories; never copy transcripts
  wholesale into drafts (short quotes only); never quote anything that looks
  like a credential or token; the files are ~19 MB of JSONL — mine them with
  grep/jq for user messages and decision points, do not read them whole.
- `docs/phase4-implementation.md` (the full CP0–CP4b narrative with every
  correction and retraction — the honest version, not the highlight reel)
- `docs/phase4-state.md` gate log (baseline moves, owner decisions)
- `git log --format='%h %s'` — the commit messages are written as
  mini-narratives and are a ready spine for the series
- `docs/archive/phase4-overnight-cp4b.md` + its morning report (the autonomous-run
  experiment itself is one of the posts)

Suggested series arc (agent may restructure if the material says so):

1. *Byte-matching an impossible binary* — Phases 1–3, the pipeline, why
   1983 assembly resists modern decompilers (jump tables, self-patching
   filenames).
2. *The doubling bug* — every structural check passed, the render was wrong,
   a human with a screenshot caught it. Evidence vs plausibility.
3. *The byte that was two things* — DS:4A "last key" vs "sector digit": a
   plausible wrong model quietly corrupted saves until a live bug report
   became the evidence. Include the retirement of the wrong reading.
4. *Teaching a game's UI to an agent, and vice versa* — the CMD window:
   owner recollection cross-referenced against asm (0x0d as newline), the
   four-row model becoming 17 tests.
5. *The overnight contract* — prove-or-pass autonomy, why "pass with a
   reason" beats "guess with confidence", the progress-report discipline.
6. *Sailboat retro* — the lessons post. Anchors: the selfmod false premise,
   the torus misreading, heredoc traps, the sibling-directory contamination
   incident. Sails: redecode.py, tamper checks catching every baseline
   move, screenshots-as-tests, the owner-in-the-loop catches. Rocks:
   plausible-but-wrong renders, evidence that lived only in chat.

Rules: no game content or game-derived images in the drafts; cite real
addresses/commits so every claim is checkable; write drafts to
`docs/blog/` (one file per post, `NN-slug.md`), status `draft` in
frontmatter. Commit each draft separately.

Acceptance: 5–6 post drafts + the retro draft, each under ~1200 words,
each opening with a concrete story rather than a thesis.

## Explicitly NOT tonight

- No CP5+ scope beyond T2's proven tick (no combat, towns, monsters content,
  PICCAS).
- No `enhanced` mode work (CP11) beyond the policy hook already landed.
- No renaming `last_key` further, no asm/ tree edits, no git push.

## Morning report

Tree is green at HEAD (`make test` all suites pass; `make sdl` compiles). Six
tasks resolved: three shipped with code, one shipped the proven half, one is a
documented PASS where the premise was false, one is this write-up. Nothing was
guessed. Every claim below cites an address or a test name.

**Shipped (commit list, newest first):**
- `T7` screenshot differential harness (`tools/screenshot_diff.py`) + wired into
  `run_cp4.sh`.
- `T6` SDL dummy-driver hang fixed + headless SDL test in `run_cp4.sh`.
- `T2` (proven half) food tick on PASS + instant starvation death +
  `food_check.c` (14 checks).
- `T4` SIGTERM now ends an SDL run (two-deep root cause).
- `T3` window close ends the overworld loop.
- `T1` write-up (no code — see below).

---

**DONE T3** — Closing the SDL window mid-game now quits instead of spinning.
Player-visible: pressing the window X while walking used to leave the game
running; it now exits. Proof: `sdl_poll` sets `exit_code` and returns -1 on
`SDL_QUIT`; `seam_key_poll` collapses -1 to 0 and raises `input_exhausted`; the
overworld idle branch tested neither, so it looped forever. It now breaks on
`exit_code || input_exhausted`, matching what the title flow already did via
`flow_stopped()`. `seam_key_poll`'s return contract is untouched (callers test
it with `!`). No new test — the CP4 scripted tests already exercise the loop and
stayed green; the defect needs a live window to show.

**DONE T4** — `kill`/`timeout -s TERM` now stops an SDL run. Player-visible: a
polite stop (systemd, `timeout`) used to require `kill -9`. Root cause was two
layers deep, both measured not assumed: (1) the library is `sdl2-compat`
2.32.74, which does not implement `SDL_HINT_NO_SIGNAL_HANDLERS` (the string is
absent from `/usr/lib64/libSDL2-2.0.so.0`) and re-applies its own SIGTERM
disposition, so our handler is now asserted from `sdl_poll`, after every SDL
init; (2) the real blocker — the signal path set `exit_code = 0`, and every wait
loop tests `if (ctx->exit_code)`, where 0 reads as "no exit requested", so
`hold_for_esc` slept its whole 1.5 s budget through the signal (gdb showed the
main thread in `nanosleep`). It now sets 1 and also tests `input_exhausted`.
Proof: `kill -TERM` dies with our exit code 1 in under a second; `timeout -s
TERM 2` returns at exactly 2 s with no stale process (was: survived to SIGKILL).
Note: `timeout`'s own 124 is not an oracle on this coreutils — a script that
catches TERM and one that ignores it both report 124; the real test is prompt
death, which is what we measure.

**DONE T6** — `SDL_VIDEODRIVER=dummy ./u2-sdl --sdl --dump-screen` now runs
headless and is a test. The defect was a hang, not the segfault the task
suspected: the dummy driver wrote the frame (192015 B PPM on disk) then spun
forever in `wait_choice`, because `--dump-screen` sets `dump_screen_done`, the
poll turns that into `input_exhausted`, and `wait_choice` tested only
`exit_code`/headless — the same wrong-stop-flag class as T3/T4. `run_cp4.sh` now
drives the whole cell->font->upload path headlessly and asserts PPM size (192015
B) and non-black ink (572 px), and SKIPs when `u2-sdl` is not built.

**DONE T2 (proven half only)** — Walking around no longer freezes time: PASS
(space) spends food, and running out kills you. Player-visible: each PASS prints
`PASS` and drops FOOD by 10 units; at zero the character dies and HP/FOOD/EXP/
GOLD all go to 0000.

> **Corrected same day:** the "movement does not tick" and "no idle timer" notes
> below are both false. Live evidence: every accepted move costs **19** food, and
> the game auto-passes on idle for **10** each. See the CP4b retraction.
 Proof: file `0x0B0D` subtracts `$0x0a` from the DS:53/54/55
packed-decimal triple (low byte at 55) and file `0x0B2E` sends a borrow to the
death handler at runtime `0x0E62` (`asm/0C62`), which zeroes DS:51..59. Two
disagreements with the owner's model are recorded, not guessed: (a) there is no
"food at zero drains HP gradually" step anywhere in the tree — underflow is
instant, every stat at once; (b) the band prints only DS:53/54, so a fresh
`FOOD=0400` is really 4000 units and a character dies on the **4001st** PASS,
not the 41st. `food_check.c` (14 checks) locks the numbers, built against a
reference model of the `sbb`/`das` chain (the first C cut got the `das` AF term
and the `>0x99` compare wrong — caught before commit). **Not done, and why:**
the per-action tick for *movement* is not implemented because the walk loop calls
no food drain — `asm/2599`/`asm/2626` (−19) have no static caller — so a per-step
cost would be a guess. Auto-PASS on idle is not implemented: the main loop
(file `0x0AC8`) does not time its key wait, it just redraws on no-key, so there is
no idle timer to hang auto-PASS on. Both are open questions, not oversights.

**PASSED T1** — **RETRACTED the same day; see `docs/phase4-implementation.md § CP4b → RETRACTION`.** The paragraph below was wrong: the overworld *does* enforce tile-based passability. Live under Spice86, walking east onto tile `0x00` printed `--INVALID MOVE!` and bounced `DS:00` back every press, while walking south onto `0x0C` stuck. My decode of the passability function stopped at file `0x2743` and treated it as complete; it falls through to about `0x2880`, and the accept path (runtime `0x2664`, `x &= 0x3F; y &= 0x3F; ret 0`) and reject path (runtime `0x25FF`, `mov al,0xFF; ret`) are both in the part I never read. The original claim and its reasoning are kept here only as the record of how the error happened.

<details><summary>original (false) T1 entry</summary>

**PASSED T1** — The task's premise ("the gate-unblocker is the `asm/2423`
passability table") is false, and disproving it is the deliverable. A full
continuous decode of the walk loop and its callee shows `asm/2423` never reads a
tile: on the overworld it only applies vehicle costs (mode 2 spends 5 HP, mode
`0x2E` is the dungeon force-field) and returns 0 for on-foot travel. So
`--INVALID MOVE!` is unreachable by walking into terrain — the original lets you
walk anywhere on the overworld, water included. Implementing a water-block would
have been the doubling bug again. What the decode *did* settle: tile IDs (0 =
water, 4 = land, 8 = plane-landing, `0x78` = player, step-4 family from the
location table at file `0x4E61`), and the sector-crossing mechanism — it is the
`DS:6E` arrival system (`asm/26B7/26CC/2708/2744` + the 36-byte `cs:0x2798`
table), reached from overworld entry, not from a step. The fill loop's two
variants (void-fill when `DS:12==0xFF`, masked `&0x3F` wrap otherwise) prove the
foot path never draws a neighbouring sector, so the owner's "smooth crossing"
recollection is not what the bytes do on foot — recorded, not implemented.
**Evidence that would unblock a crossing change:** a live capture of the original
walking off the west edge (does the sector digit change, does the view scroll),
which the static bytes cannot answer. Full detail in
`docs/phase4-implementation.md § CP4b`.

**DONE T8 (this write-up)** — `docs/phase4-implementation.md § CP4b` records
T1/T2/T3/T4/T6/T7 with every citation; `docs/phase4-state.md` CP4 row updated.

**HELD — needs an owner decision:**
- **T5** (creation tail straight into the world) MOVES THE CP2 baseline and is
  marked `[OWNER: pre-approved?]` in the task list. The contract says a baseline
  move needs evidence *and* the pre-approval flag resolved; the tail itself is
  proven (redecoded `5400` tail, file `0x5C1F`), but I did not move the baseline
  without the flag cleared. One word from you and it is a short task.

**Not attempted:** T9 (blog/retro) — the queue did not fully clear, and T9 is
explicitly gated on T1–T8 being done or passed.


## Evening addendum (same day, after live-capture work)

* **T2 — now CLOSED.** The idle auto-PASS is implemented as a **poll count, not a
  clock**. Mechanism proven at runtime `0x08CE`/`0x08D1`/`0x08D6`/`0x08F9`: the
  wait loop polls `key_poll`, tests `cmp $0xff,%ah` for no-key, and advances byte
  counters with `incb DS:21; jne <loop>`, the iterate-until-wrap idiom. No
  `INT 1Ah` exists anywhere in the corpus and the only read of the BIOS tick at
  `0x40:6C` is the startup RNG seed in `asm/0100`, which rules out a hardware
  timer. The measured ~7.6 s at `--Cycles 3000` is an emulator artifact and is
  **not** encoded; `256` is used as a placeholder because it is the width of the
  counter the original uses, and the code says so plainly. Owner decision
  recorded: **hardcode for `original`, adjustable and disable-able for
  `enhanced`** — `ctx.auto_pass_polls` is the knob, but `u2_mode` is a renderer
  axis, so the fidelity axis it should hang off does not exist yet.
* **T1 — closer than it was, still open.** Passability, the `&0x3F` wrap and the
  19-per-move drain are implemented and cited to observed bytes. Blocked on the
  **mountain tile id** (needs one named sample from the owner) and the
  `DS:15`/`DS:18` travel-mode argument, which decides whether boat-on-water is
  passable.
* **New evidence, not from this plan:** combat and theft messages were recovered
  from the harvest via `tools/cfg_printed.py` gated on `ExecutedInstructions`.
  `LEGS PARALIZED!`, `--PARALIZED!`, `A THIEF STOLE SOMETHING!`, `--HIT!!!`,
  `--MISS`, `KILLED--GOLD+` and `--INVALID MOVE!` are all proven to have printed;
  Minax, the ring and the force field are proven not to have. See
  `docs/spice86-live-findings.md` § 7–§ 11.
* **T9 is now unblocked in principle** — T1–T8 are done or open-but-scoped, and
  today produced a great deal of publishable material, including two errors of
  mine that a cross-reference pass caught in the same run.

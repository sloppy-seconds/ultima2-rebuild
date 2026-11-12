# Handover — outstanding agent-drivable capture tests

Entry point for the open questions in `docs/needs-gameplay.md` that an **agent can
drive without the owner**. This file is an **index and a shared how-to**, not a
re-derivation: each item's full recipe, byte evidence and self-corrections live in
its `Gxx` row of `docs/needs-gameplay.md` (the source of truth). Do not copy a
recipe out of that table into here — a hand-maintained second copy drifts and then
lies (AGENTS.md § 3).

Generated 2026-11-12. Items were selected by: status not closed/done in the table,
the `who` column names an agent (or `both`), and the row tail confirms a genuinely
open sub-question (several rows carry a stale status column while the description
records a later partial closure — those are annotated below).

## Status (updated 2026-11-13) — the tables below are stale on these rows

The Minax-session follow-up work closed several rows listed below as open. `docs/needs-gameplay.md`
is the authority; this index lags it. Closed since generation, each by a named capture dir
under `captures/spice86/`:

- **G2** blocked step = 19 food (`g2-blocked-step`). **G5** mountain `0x10` blocks on foot (same step test).
- **G55** refused swamp step = −5 HP (`g55-swamp-refused-step`); **engine fix shipped + gated** (CP5 E1).
- **G69** frigate cannon = 0 food (`g69-cannon-food`). **G70** letter-P = 0 food, space = 10 (`g70-pass-vs-move`); **engine fix shipped + gated** (CP5 E1b).
- **G4** mode `0x2e` = force-field tile, step effect = the −1000 (closed statically). **G42** boarding does NOT spend the gating item (3 samples + no `items_held` subtract).
- **G41** class-dependent foot stamp — engine already correct (`u2_foot_stamp(pclass)`), the handover predated the fix.
- **G68** charisma moves the price pair (`12 08`@Cha99 vs `07 30`@Cha10) but the curve is confounded by per-press item variation and `asm/20AB` is known-incomplete — **partial**, not closed.
- Bonus (not a G-row): the horse travels **one cell** per turn and pays **38** food, confirmed by a real boarding (`horse-move-1cell`); the engine already matches.
- **G58** partial (`g58-demonstration`): the 'D' demonstration is one picture per couplet (PICOUT/PICTWN/PICDNG/PICSPA + a Minax field), refuting pic.h's inferred stars+pyramid; couplet 3 and the `present_poem` code fix remain open.
- **G51** closed (`g51-dungeon-A-attack`): in a dungeon `A` = ATTACK (`ATTACK--MISS`), `Up` = ADVANCE; refutes the `dungeon-35-live` A=ADVANCE claim; engine already matches. **G47** partial: advancing onto a trap prints `ARGH! A TRAP!` and puts out the torch (`DS:26`→0); tools-spend and zero-tools damage still open. **Dungeon-entry recipe now established: cmd_E standing on the door tile (sector 20 at (25,5)), then `I` to light a torch.**
- **G46** partial (`g46-cast-branches`): SURFACE observed (dungeon→overworld at the door cell); PASSWALL and PRAYER only failure cases captured; engine implements no CAST effects so none is gateable.

Still open and agent-drivable, roughly by cost: **G64b** (time-door transit trigger undecoded — stepping on the `DS:6E` gate cell is not sufficient), **G7** (needs a stat-add trigger), **G46** (CAST branches — needs a tower + spell), **G38/G48/G72** (guards/thieves — Towne Linda has no `0x60` guards; needs a different town or seeded creatures), **G6** (counter near 0), **G16** (`0xC0` event tile), **G58** (splash pics), **G71** (Pangea soft-lock). Blocked on off-Earth vehicle tiles (need the rocket or a planet setpos): **G77/G51/G49**. Blocked on tooling: **G30** (`spice86_type.py` has no modifier-key support for Ctrl-Y).

## How to drive any of these

One canonical sidecar runs the four capture primitives together alongside play:

```
tools/spice86_play_capture.py --out /var/tmp/u2/<name>/cap --frames
```

It auto-discovers the emulator port and run dir from `tmp/.spice86-session.env`,
and records frames + a change-triggered DS log + content md5s at once. Start the
original headless first:

```
tools/spice86_start.sh --headless --name <name> --player <seeded-file> --auto-play
tools/spice86_type.py --keys A,Right          # drive it; confirm by reading memory
tools/spice86_screenshot.py /var/tmp/u2/<name>/now.png
tools/spice86_stop.sh
```

Read `docs/spice86-capture.md` ("Which tool for which job") before driving the
emulator, and `docs/handoff-2026-11-12.md` § 4 (recipes) and § 5 (hazards). Seed
state instead of walking: `tools/u2_edit.py setpos <file> X Y` is the only way to
change sector/position; `--seed-map` installs a synthetic map.

**An item is only closed by a named capture directory** under
`captures/spice86/<name>/`, never by a belief about how the game works. A
behavioural claim about something visible needs a frame, not just a memory trace.

## Hazards that cost the last sessions time (read before starting)

- `bp_read.py`'s `breakCsIp` **cannot be removed once armed** — arming it during
  live play freezes the game on the next hit with no resume. Use breakpoints only
  headless or when a container restart is acceptable.
- Headless Spice86 **ignores SIGTERM/SIGINT**; a CFG harvest must come from
  `monitor dumpAll`, never a signal. Trace windows roll in seconds — poll DS live
  (`tools/poll_probe.py`, ~250 req/s at `0x08BE*16`), do not harvest after the fact.
- The screen is a **screenshot**, never a decode of `0xB8000`. A turn-recorder
  `.vga` is not a screen.
- The attack reads the **stamped grid at `DS:1800`**, not the raw creature slots; a
  poked slot with no grid stamp is unattackable. Attack is `A` alone, not `A,<dir>`.
- Do not `pkill -f spice86_play_capture` from a shell command whose own text matches
  that string — it kills the running script.
- Scratch and logs go to `/var/tmp`, never `/tmp` (tmpfs = RAM here).

## High priority

| ID | Question | State / what is blocking | Gate / tool |
| --- | --- | --- | --- |
| **G2** | Does a blocked step cost 19 food? | Turn-economy claim, never observed on a blocked move. | `spice86_play_capture.py` DS log across a refused step |
| **G64** | Does entering a site silently save the game, and do time doors? | Site-entry auto-save half is closed (auto-saves, pins outdoor state — see `docs/handoff-2026-11-03.md`); the **time-door / launch** half is open. If launch auto-saves, the engine not doing so is a real fidelity gap (deep-space stranding permanence). | md5 content files across a time-door transit and a rocket launch |
| **G77** | What is the plane's own march-step cost, in cycles? | Engine is roughly 6x too fast (`U2_PLANE_STEP_MS`). Needs a live rate measurement. | `spice86_play_capture.py` DS poll during plane flight; `captures/spice86/dive-sweep-rate/` is the prior |
| **G51** | Does `A` attack or advance inside a site? | Two captures disagree; bytes side with one. **Now unblocked** — dungeons are open (CP7). | one headless dungeon run: `A` at a creature, `A` at a wall, a frame each |
| **G55** | Does a refused step onto a swamp also cost 5 HP? | Effect-ladder position inside `asm/2423` unproven. Control: a refused step onto a NON-charging tile must stay free. | step onto swamp (refused) vs mountain (control), read HP either side |

## Medium priority

| ID | Question | State / what is blocking | Gate / tool |
| --- | --- | --- | --- |
| **G4** | What sets mode byte `0x2e`; step onto a force field. | Force-field effect unobserved. | DS diff across a force-field step |
| **G5** | Which tile is the mountain (rejection test)? | `agent + 30 s owner look`. | step test + a frame |
| **G6** | Is the single-byte counter underflow reachable? | Census claim; needs a live path to the underflow. | DS poll at the counter |
| **G7** | Stat cap 99 wrap — observed, not inferred. | Owner recalled wrap above 99; bytes agree on the layout but the wrap has not been seen. | force a stat near 99, add, read back |
| **G16** | What is the `0xC0` event tile actually do? | A probe threw the player y=20→57; 1311 occurrences, 262 each in `mapx25/35/45`. | step on one, record the event + a frame |
| **G19** | The dungeon creature set is unreachable by the tile join. | Codex dungeon images are 64x41 screenshot crops; dungeon is a wireframe renderer. | dungeon capture, match drawn sprites |
| **G38** | Does a town thief steal once it has aggro, and how does a town creature get aggro? | The alarm-by-proxy half G37 left open. **Trap:** sector 22 ships slots 8–11 at role `02` at load, so `02` is a data value as well as a transition target — do not chase a transition that was never one. | seed a thief, attack a *different* creature, watch `DS:D6..DF` |
| **G41** | Where does `DS:13` get set to the on-foot value, and is it class-dependent? | **Known engine bug:** the engine stamps `DS:13 = 0x78` unconditionally at world open; it should be `0x78 + 2*pclass`, so three of four classes are wrong on entry. The *where-set-in-original* half is still open (no caller of `asm/080D` in the corpus). | fix the class stamp (engine); the original half needs a caller found first |
| **G46** | What do the other CAST branches do — PASSWALL, SURFACE, PRAYER, BLINK's validity rule, KILL's award? | Decision D14 (does `cmd_C` ship gates only) is open. Agent-drivable via `docs/spice86-capture.md` patterns 3 and 4. | cast each, screenshot + DS diff |
| **G49** | How far does a frigate cannon reach, and what does it damage? | One-cell reading is `measured` from the listing, never observed; owner's range recollection is `recalled`. Needs a frigate boarding. | board, fire at range 2 and 3, read `mon_hp` before/after + a frame |
| **G68** | Does the Int+Cha price discount exist, and what is the curve? | `docs/goal-spec.md` P1 third-party claim, unverified. | price a shop item at two Int/Cha values |
| **G69** | Do frigate cannons really not consume food? | `docs/goal-spec.md` P5 claim; decides whether the farm-by-cannon strategy is real. | fire cannons, watch the food counter |
| **G71** | Is the Pangea spawn soft-lock real, and is it escapable? | `docs/goal-spec.md` P4 recovery-layer claim. | spawn at Pangea, attempt to leave |

## Low priority

| ID | Question | State / what is blocking | Gate / tool |
| --- | --- | --- | --- |
| **G13** | Reproduce `mapx30 (0,0)=0x50`. | Low value; a census reproduction. | map read |
| **G30** | Does `Ctrl-Y` move the character south? | Cosmetic; cheapest input-contract check. | send Ctrl-Y, read position |
| **G42** | Does boarding spend the gating item? | Whether vehicles are consumables. | board with `DS:E2=01`, read the item after |
| **G47** | TOOLS and dungeon traps — decoded statically, never observed. | Was parked behind the dungeon decision; **now unblocked** (CP7 open). Seed `DS:66` to 2 then 0, walk into the trap tile, read byte + HP, take a frame. | headless dungeon trap walk |
| **G48** | What is the `[DS:1D7]==1` refusal in STEAL guarding against? | Whether STEAL has a guard rule at all; what `asm/52B3` returns. | steal from a guard vs non-guard |
| **G58** | Which picture underlies each couplet of the demonstration; where do PICDNG/PICTWN/PICOUT appear? | Engine already shows the title pic. | splash capture, match `engine/src/core/pic.h` |
| **G70** | Does `[P]ass` cost less food than moving? | `docs/goal-spec.md` P3 optimisation claim. | pass vs move, compare food |
| **G72** | Guard-talk reachability (the half of the guard-kill claim still open). | HALF CLOSED 2026-11-08: the string-existence half is done via `tools/strings_atlas.py`; the live-reachability half remains. | one live talk to a seeded guard |

## Not agent-drivable (owner or blocked) — for completeness

- **G43** (plane airborne flag / W6), **G45 step 1**, **G47 step 1**, **G36**, **G12**,
  **G63**, **G65** — owner-only or need a frame per step the owner must supply.
- **G35** — the barkeep hint table: unblocked in principle but the remaining
  constraint is the `asm/1590` coordinate gate (`X>=0x20 AND Y>=0x20`); only
  markers at (34,41)/(48,48) in sector 21 are eligible. Agent-drivable but needs
  that specific stand, not a generic tavern drive.

## After a capture lands

Update the `Gxx` row in `docs/needs-gameplay.md` with the named capture dir and the
verdict (that table is the authority), rename/re-header any `asm/` file the finding
touches in the same commit, and — if the finding is about how a *generic* DOS harness
should be built — add a line to `docs/harness-portability.md` § 6.

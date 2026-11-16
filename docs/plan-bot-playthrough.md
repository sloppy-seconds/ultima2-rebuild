# Plan — a bot that plays from character creation to winning

Status: **plan only, nothing built.** Owner framing 2026-11-16: the bot does not need to fly a
plane, but it does need to fly a rocket (landing is expected to be the hard part), probably use a
boat, navigate the time doors, and know what to do from the start of the game to the win. A
walkthrough supplies checkpoints where the current goal changes. **Fighter build first, so magic
is out of scope.**

---

## 0. What this is for, and what it is not

The bot exists to be a **differential tester**: the same policy drives `engine/u2` and
`original/ultimaii.exe` through `Spice86Target`, and the DS blocks are diffed. A playthrough is
the deepest available differential test, because it exercises the engine in the order a player
does and for long enough to reach states no gate reaches on its own.

It is **not** a goal of this plan to build a bot that plays *well*. Winning is the coverage
mechanism, not the product. Where the walkthrough is unverified, the plan says so rather than
coding the guess.

### Non-goals

- **Magic.** Fighter-only removes `cmd_C` (CAST) from the path, which is one of the
  weaker-evidenced handlers, and removes the whole spell/threshold surface. Owner's call, and it
  is a good one for reasons beyond simplicity.
- **Plane flight.** Not needed for the win. Free-running with no airborne discriminator, so it is
  the worst vehicle in the game to automate and it is simply left out.
- **Playing well.** No optimal routing, no economy optimisation. "Enough to reach the win" only.
- **Replacing the CP/AP gates.** A bot run is slow and flaky by nature. Gates stay the contract;
  the bot is an additional, probabilistic layer.

---

## 1. Assets that already exist, with their grades

Stated first because two of the three hard-sounding requirements are already lookup tables.

| asset | where | grade |
| --- | --- | --- |
| Moongate cells and destinations, all five eras | `tools/census.py --what gate` | **measured** (`asm/26B7`+`26C3`+`26C6`, cells at exe `0x2998`) |
| Vehicle pads: sector, x, y, `gate_value` per mode | `tools/census.py --what vehicle` | **observed** |

The bot reads these as **data** — `disasm/census/*.tsv` via `bot/census.py`, not by shelling out to
`tools/census.py`. The TSVs are committed and `--check-fresh` regenerates and diffs them, so the
file is the reviewed artefact; a CLI query re-runs the generator instead, which means a bot could
pass against a table the committed tree does not have, and it couples the bot to a stdout format
nothing guards. `bot/census.py` also checks each file's declared row count, so a truncated table
raises instead of reading as a small table.
| Place name → sector | `docs/place-index.md`, `tools/place_index.py` | byte-derived |
| Rocket **launch**, end to end, proven against the original | `bot/launch.py` | demonstrated (reached Mars) |
| Bot can see the grid, the tile under foot, and whether an attack can land | `bot/target.py` `read_grid` / `tile_at` / `targets` | gated |
| One decoder for both binaries | `u2_state` is a byte mirror of DS; `u2_state_load_ds_buf()` | gated |
| Mode discriminator: title / world / site / dungeon / space | `u2_state_mode()` | gated, validated over the capture corpus |
| Win condition as engine predicates | `docs/goal-spec.md` | graded per claim |
| Dive geometry: 256-step cycle, ~26 s window | `engine/tests/dive_pace_check.c` | measured |

**The rule that keeps this honest:** the walkthrough's steps are **data with a grade per row**,
never code. `docs/goal-spec.md` grades its own claims as 3 `recalled`, 1 `refuted`, 1 `measured`.
A recalled step that becomes behaviour is indistinguishable from a verified one the moment it is
written in Python, and a bot failing on a recalled assumption reads as an engine bug.

---

## 2. The six layers

### Layer 1 — Perception

*What the bot can read.* **Largely done.**

Already exposed in the observation: `mode`, `pos`, `sector`, `char` (stats, items, gold),
`gate`, `screen`, `digest`, `targets`, and raw grid access.

Remaining:

- **`passable` per neighbour, or better, a whole-sector passability bitmap.** `u2_mode_passable(w,
  x, y, stamp)` is the verdict function (`asm/2423`'s mode dispatch at `asm/253B`) but it takes a
  `u2_world`, which the bot does not have. So the engine must answer it, exactly as it answers
  `targets`. **This is the shape of every remaining layer: the engine answers rules, the bot does
  search.** A pathfinder that reimplemented the default-deny tile table in Python would be a
  second copy of a rule the tree already has, and the second copy is the failure this project
  keeps naming.
- **NPC positions.** `tlkxNN` gives names and the record index is the quest role, but the bot
  needs "where is Lord British standing". `census --what npc` is the starting point; whether it
  carries coordinates needs checking before this is scoped.

*Gate:* every new field pinned in `bot/target_check.py` against a synthetic DS blob, offline, plus
one live assertion per field that can only be pinned live (as `tile_at`'s offset was).

### Layer 2 — Locomotion (the keystone)

*Get from A to B.* **Missing, and nothing below composes without it.**

- BFS/A* over the passability bitmap, per mount mode. Search is cheap and holds no game rules, so
  this is the part that *should* live in Python.
- **Crossing sector edges.** Walking off an edge changes sector and the grid must be re-read; the
  arrival cell is the mirror of the exit. `census --what site` already gives "where do I stand to
  enter a place", which is the same problem from the other side.
- **Standing on an exact cell** matters more than it sounds: a moongate is a single cell, and a pad
  is a single cell. So the primitive is `goto(sector, x, y)`, not "head north".
- **Per-mode passability is not a detail.** A grounded plane refuses forest, a horse refuses
  shallows, a ship needs water. The pathfinder must search with the *current* mode's verdict, and
  a path computed on foot is wrong the moment the bot boards something.

*Gate:* a pathfinder test that feeds a synthetic grid with a known maze and asserts the route,
plus a mutation control that breaks the neighbour ordering. Then a live `goto` in sector 40 that
arrives at a named cell, confirmed from DS.

### Layer 3 — Interaction primitives

*The 26 commands, minus magic.* The surface is already named and mostly high-confidence:

| need | command | note |
| --- | --- | --- |
| buy | `T` TRANSACT | a two-cell ray onto a vendor marker; `census --what vendor` gives markers and shops |
| ready weapon | `R` | menu, takes a digit |
| wear armour | `W` | menu, takes a digit |
| board | `B` | must be standing on the pad cell |
| launch | `L` | rocket path proven |
| descend / enter / climb | `D` / `E` / `K` | `D` gates on site kind |
| get item | `G` | |
| ignite torch | `I` | dungeon light; the dungeon checkpoint needs this |
| pass turns | `P` | the spawn driver, and how time is spent |
| offer gold | `O` | relevant if any "service" is actually a payment |
| save | `Q` | Earth-only, outdoors-only, on-foot-only |
| status | `Z` | the read-back for equipment state |

Each primitive is: send keys, then **confirm from DS**, never from the key being accepted. A
dropped key and a refused action are observationally identical, which `Spice86Target.key()`
already handles for the single-key case.

*Gate:* each primitive gets a before/after DS assertion in a live run, and a negative control that
proves the assertion can go red (the `g43-plane-grounded` positive-control lesson: "refused forest"
without "accepted grass" proves nothing).

### Layer 4 — The checkpoint script

*The walkthrough as data.* A table, one row per checkpoint:

```
id, name,
preconditions:  engine predicates over u2_state (gold >= N, item held, stat >= N, era == k)
goal:           a target — a cell, a place name, a sector kind, an item count
policy:         which interaction primitives are in play
completion:     a predicate that must become true
grade:          measured | observed | inferred | recalled | refuted
source:         which walkthrough line, or which asm
on_fail:        retry | reroute | abort
```

The owner's early-game sketch, transcribed at its honest grades:

| # | checkpoint | grade | note |
| --- | --- | --- | --- |
| 1 | create fighter, buy a weapon and armour, **ready and wear them** | inferred | `R`/`W` menus are decoded; the *choice* of what to buy is a strategy, not a byte |
| 2 | farm monsters for gold and items | recalled | P3/P4 cover spawn driving; `P` PASS is the cheapest spawn driver |
| 3 | Lord British for HP | **recalled, and unverified** | free or paid is an open question below |
| 4 | hotel clerk for stat upgrades | **recalled** | this is P2, and P1's "Int+Cha exactly 40 from three specific splits" is also recalled |
| 5 | dungeon crawl for trilithium, needing torches and tools | inferred | `I` ignite, `D` descend; CP7 models dungeon entry/light/descend/loot |
| 6 | moongate to a named era | **measured** | the gate table is the best-graded thing in the plan |
| 7 | board and launch the rocket | demonstrated | `bot/launch.py` |
| 8 | land the rocket | **the hard bit** | see § 4 |
| 9 | boat / ship | observed | pads in census; `G49` frigate reach is blocked but not needed for a fighter route |
| 10 | reach Castle Barataria, get the key items, kill Minax | graded per edge in `goal-spec.md` | the key-item dependency graph already has a grade per edge |

A row may not be promoted from `recalled` to `measured` without naming the capture or the asm that
did it, which is the same rule `needs-gameplay.md` already enforces.

### Layer 5 — Supervision

*Not optional, and this layer has a hard constraint.*

- **Death is terminal and memory-only.** `asm/0C62_death` zeroes HP in memory, writes nothing, and
  there is no "press any key" — the process must be hard-restarted. So supervision is
  restart-the-emulator, not reload-a-save-in-game.
- **Nothing persists off Earth. Ever.** Measured: six Planet X transitions and two deaths, zero
  bytes written. The consequence for a playthrough is severe and easy to miss: **a death off Earth
  loses the run** unless the bot saved on Earth before leaving. So the checkpoint script must
  contain explicit save-before-departure steps, and those are not a convenience, they are the only
  recovery mechanism that exists.
- **Stuck detection.** No progress predicate true for N turns, or a position that oscillates.
- **`u2_edit.py set sector` is unreliable** (a seeded sector 32 arrived as sector 40). A
  playthrough walks everywhere so it does not depend on this, but any checkpoint that *seeds*
  state must not trust it until it is investigated.

*Gate:* a supervision selftest — feed it a stuck trace and a death trace from existing captures
and require it to classify both.

### Layer 6 — Rocket landing

*The hard bit, and probably less hard than it looks.*

The dive is **not** free-running chaos the way plane flight is. It is a decoded **256-step cycle
with a ~26 s window**, both pinned by `dive_pace_check.c`. So landing is: observe phase, predict
the periodic trajectory, commit at the right phase. That is a prediction problem with a known
period, which is a different and easier class than the plane's "nothing about it is periodic".

What is needed: the dive's step geometry against the target cell, and a way to know when the
commit key must be sent. `U2_DIVE_STEP_MS` is a measured lower bound, so the timing budget should
be expressed in **steps**, not milliseconds — steps are the invariant, wall-clock is the emulator's.

*Gate:* land on a chosen cell, twice, with the arrival cell asserted from DS. If it cannot be done
twice it is not solved.

---

## 3. Sequencing

Each milestone is independently useful and independently gated. Nothing here is a big-bang
integration at the end.

| # | milestone | proves |
| --- | --- | --- |
| M1 | passability exposed by the engine; `goto()` on foot inside one sector | layers 1+2 compose |
| M2 | cross a sector edge and reach a named place | locomotion across sectors |
| M3 | buy a weapon, `R` ready it, `W` wear armour, asserted from DS | layer 3, and the first real checkpoint |
| M4 | enter a dungeon, light a torch, descend, come back with trilithium | the riskiest interaction chain |
| M5 | transit a moongate to a named era | the best-graded navigation target |
| M6 | board, launch, **land** the rocket on a chosen cell | layer 6 |
| M7 | supervision: survives a death and a stuck loop | layer 5 |
| M8 | the checkpoint table run end to end, on both binaries, DS diffed | the actual product |

M1 is the keystone and the first thing to build. M3 is the first milestone that would have been
impossible a week ago, because it needs grid sight, which did not exist until today.

---

## 4. Open questions

**For the owner:**

1. **Are Lord British's HP and the hotel clerk's stats free or paid?** P2's "grind with its own
   stop condition" framing suggests paid. It changes whether checkpoint 3/4 needs a gold threshold
   before it can fire, and a checkpoint whose precondition can never be met is a silent hang.
2. **Which walkthrough is the source?** The plan inherits `goal-spec.md`'s source. If there is a
   better or more complete one, the checkpoint table should be built from it once rather than
   transcribed twice.
3. **What counts as "the bot won"?** `goal-spec.md` has the Minax detector and the key-item graph;
   confirm the win predicate is that and not something more.

**For the tree, not for a person:**

4. Does `census --what npc` carry coordinates? If not, "walk to Lord British" needs a location
   source before M3 can include him.
5. `u2_edit.py set sector` — investigate before any checkpoint seeds state.
6. Is the dive's step count affected by the same redraw cost that made the warp frame cost
   119,055 cycles? If so the landing budget must be measured, not derived.

---

## 5. Hazards this plan is carrying forward

Written down because each one cost a session to learn:

- A refusal with no positive control is not a finding.
- A control placed before the thing it controls can destroy its own precondition.
- A rate measured by two reads around a sleep is not a rate.
- A synthetic fixture that omits a field the rule under test reads silently tests something else
  (this bit the `targets` checks twice in one hour).
- A derived classifier must be run over the existing corpus before it is believed.
- A gate that asserts a sufficient condition cannot go red.
- Quiescence is the right settle signal for a step-and-wait world and the wrong one for a world
  that keeps moving.
- A claim read off a thumbnail is not a claim about the image.
- A shipped item must be closed in every live handoff that lists it open, in the same commit.

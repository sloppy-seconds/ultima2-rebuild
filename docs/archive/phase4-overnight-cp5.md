# Overnight task queue — CP5 prep, tool hardening, cold cases

> **ARCHIVED 2026-10-22 — queue complete: 9 DONE, 1 PASSED (T8), 16 commits.**
> The per-task results and evidence are in `docs/phase4-overnight-cp5-report.md`, which
> stays live because it is the detailed record behind the BCD census, the T5/T6 object-store
> finding and the T7 dungeon-grid correction. Do not resume work from this file; the open
> tickets it spawned are in `docs/phase4-state.md` (B1..B5, C1..C7) and
> `docs/needs-gameplay.md` (G0..G13).

Autonomous queue for a fresh agent running unattended. Every task here is chosen
because it needs **no owner interaction**: no walking the game, no judgement calls
about design, no approvals. If a task turns out to need any of those, pass it with a
reason rather than improvising.

Standing context: `AGENTS.md` (hard law), `docs/phase4-state.md` (checkpoint table,
backlog, gate log), `docs/spice86-live-findings.md` § 12 and § 13 (the evidence base
this queue is drawn from).

---

## Rules of engagement

1. **Prove or pass.** Nothing may be asserted without a byte citation, a captured
   frame, or a recorded snapshot. If evidence is insufficient, write what was found
   and exactly what is missing into the progress report, mark the task PASSED, and
   move on. A passed task with a clear reason is a success. A guessed mechanic is a
   failure — see § 3 of `docs/archive/handoff-2026-10-21-evening.md` for three instances from
   one day, all of which the owner caught and the agent did not.
2. **No emulator launches.** Every task is offline. There is no one to drive the game,
   and an unattended headless session produces no turns and therefore no evidence.
   `tools/spice86_start.sh` should not be run.
3. **`make -C engine test` must be green at every commit**, and
   `bash tools/assemble_clean.sh` must report `CLEAN REBUILD BYTE-MATCH: 37344 bytes
   identical` if anything under `asm/` is touched. CP0 additionally requires that
   non-comment lines in `asm/` stay byte-identical across renames — filenames and
   headers may evolve, bodies may not. That gate caught a real mistake on 2026-10-21
   (label renames that looked cosmetic); do not fight it, adapt to it.
4. **Never write to `original/` or `references/`.** Never delete anything outside the
   working directory. Scratch goes to `/var/tmp`, never `/tmp` (tmpfs here; it takes
   RAM from the model).
5. **Never edit a file with a script, and never heredoc a non-trivial script.** Use the
   edit tool; write scripts to files and syntax-check them. Both rules exist because of
   recorded incidents.
6. **Commit in logical units** with the reason in the message. If a finding corrects an
   earlier committed claim, say so in the commit and keep the retraction visible rather
   than silently rewriting history.
7. Grade every claim: `observed`, `measured`, `inferred`, `recalled`. Write the grade
   down. Three of the corrections on 2026-10-21 were an ungraded `inferred` being read
   back later as fact.

---

## Tier A — tool correctness. Highest value, do these first.

A tool that lies is worse than no tool, and two were caught lying in one day.

### T1 — `tools/cfg_printed.py` silently dropped a genuinely executed print site

`LAND--NOT HERE!!!!!` at runtime `0x3B72` is present in `ExecutedInstructions` inside
`captures/spice86/dungeon-35-live/spice86dumpExecutionFlow.json`, but
`cfg_printed.py` did not list it as printed. The tool keys its site discovery on
something the dump no longer provides in the expected shape — a block parse returned
zero blocks during diagnosis.

Required:

* reproduce the miss against the committed capture and explain the mechanism
* make the tool derive print sites from `ExecutedInstructions` as the primary source,
  with CFG blocks only as a secondary seed, per the correction already recorded in
  `AGENTS.md` § 4
* make it **fail loudly** rather than return an empty list when the dump shape is not
  understood
* add a negative control to its own test: a message known to be unexecuted must be
  reported absent, and a message known to be executed must be reported present. Use
  `LAND--NOT HERE!!!!!` (present) and `MINAX CRIES: DIE FOOL!` (absent) as the pair.

Evidence to use: `captures/spice86/dungeon-35-live`, `flight-launched-live`,
`castle-03-live`, `create-test-live` (the last is a clean negative control — no
combat strings at all).

### T2 — `tools/xref_provenance.py --strict` is red and has been for a while

Four dangling citations, pre-existing and not caused by the 2026-10-21 session:

```
phase4-implementation.md: asm/0964 resolves to no function range
phase4-implementation.md: asm/5002 resolves to no function range
spice86-live-findings.md: asm/0964 resolves to no function range
spice86-live-findings.md: asm/5002 resolves to no function range
```

Required: determine whether the addresses are wrong or the functions are missing from
`asm/`, fix the correct one of the two, and get `--strict` green. Do not paper over it
by widening the regex. Note the tool also reports 101 named functions with no
`# provenance:` line — that is a known backlog, not part of this task, do not bulk-fill
`static` just to make a number go down.

### T3 — `tools/turn_recorder.py` defects

Two known problems, both from the 2026-10-21 fight recording
(`captures/spice86/fight-20261021-castle03/`, 3000 turns):

* **76 odometer anomalies.** The food-delta decomposition into moves and passes failed
  76 times. Either the model is wrong (food is not always 19 per move / 10 per pass) or
  the trigger misses turns. Determine which from the recorded data, and say which. If
  the model is wrong, that is a mechanic finding and belongs in the findings doc, not
  just a tool fix.
* **the `screen` field is junk.** The game runs in CGA graphics mode, so `0xB800` holds
  pixels, not a text page. The field has been emitting garbage. Either delete it or
  replace it with something honest (a VGA ink metric, which is what proved darkness was
  a real blank frame in `dungeon_capture.py`).

---

## Tier B — cold cases that are solvable offline from captured data

### T4 — the gold that vanished

`docs/spice86-live-findings.md` § 12 lists this as unexplained and explicitly refuses
to guess: gold went `9999 -> 14` in a single turn at turn 209 of the sector-00 fight.

The full DS block for every turn is recorded under
`captures/spice86/fight-20261021-castle03/turns/`. Diff turns 205..213 across the whole
`DS` block, not just the gold field, and find what else moved at the same instant. The
gold is 2-byte BCD at `DS:58/59`. Candidate mechanisms worth checking rather than
assuming: a theft variant that takes gold, a bribe or offer path, or a BCD corruption
from a write that produced invalid nibbles. Report the mechanism with the byte evidence
or pass with what is missing.

### T5 — two cells in `mapx30` that the shipped file does not explain

Observed 2026-10-21 while flying: the live grid differed from `mapx30` on disk in exactly
two cells, `mapx30` (0,0) held `0x50` (rocket) where the file has `0x00`, and (27,11)
held `0x30` (boat art) where the file has `0x08`. The owner had only ridden the plane
and a horse that session, so neither is attributable to a landing.

`docs/spice86-live-findings.md` § 13 established that dungeons stamp runtime objects
into the grid as small integers `0x01..0x07`. The open question is whether the
overworld does something similar with vehicle bytes, or whether sector entry places
vehicles from another source. Check the other captures in `captures/spice86/` for the
same signature and say whether this is one anomaly or a mechanism.

### T6 — the 30 cells that differed from `mapx03`

Same class of question, smaller. Recorded in § 12 as unaccounted for. Likely the same
answer as T5; do them together if so.

---

## Tier C — static decode work, no game needed

### T7 — the `y*16 + x` anomaly at file `0x6AC0`

`[DS:14]` is written at file `0x6238` from a call to `0x6AC0`. That routine computes an
index as `y*16 + x` against a grid that is 64 wide, which cannot be the sector grid.
`[DS:14] = map_byte >> 1` is already proven by other means, so this is not blocking
anything — but an unexplained addressing mode in the tile path is exactly the kind of
thing that bites later. Resolve what array it actually indexes.

### T8 — the force-field tile byte

`asm/24B5` is fully understood except for its trigger: which map byte makes a cell a
force field, and what drives the animated white/blue/purple render. The damage, the
ring gate at `[DS:D6]` and the death branch are all proven. Walk the callers of
`asm/24B5` statically and look for a tile comparison upstream. **This may be
unreachable without a capture; if so, pass it and say what capture would settle it.**
Do not guess a byte.

### T9 — decode the `monsters` file and render the shapes

`original/monsters` is 2176 bytes = 64 records × 34 bytes, and the records look like
coordinate pairs rather than names. There is no monster name table in the executable —
`THIEF` at file `0x50FE` is a *player class*, not a monster. So the only way to see what
a monster type looks like is to render its shape.

Deliverable: a tool that renders the shapes for the types actually observed on 2026-10-21
(`48, 52, 56, 60, 64, 96, 104, 240, 244, 248, 252`) and a contact sheet. That would put
names to the caster types (`0x34` legs, `0x38` arms, `0xF8` missile, `0x3C` sleep) and
the thief (`0x40`), which are currently identified only by behaviour. If the record
layout does not decode, report the layout evidence and pass.

---

## Tier D — only if T1..T9 are done or passed

### T10 — write CP5 acceptance tests, not CP5 code

`docs/phase4-state.md` § CP5 has tickets C1..C7 with byte citations. C7 is resolved; C5
is blocked on a capture. **Do not implement combat.** The checkpoint is not owner-gated
and starting it unattended would pre-empt a decision that is the owner's.

What is safe and useful: write the failing acceptance tests that C1, C2 and C6 imply, so
whoever implements CP5 starts against a specification rather than a blank. Specifically:

* C2 is the important one — the resist is a **~75% roll on a live inventory slot**, not
  boolean immunity. A test that would pass a boolean implementation is a bad test.
* C6 — theft deletes from the item array and nothing is restored on kill. 8 items taken,
  0 returned, measured over 3000 turns.
* C1 — three independent counters, `DS:26B/26C/26D`, 0..15, persisted in the save block.

Land them as skipped or expected-fail if the engine has no hook yet, and say which.

---

## Explicitly out of scope tonight

* Anything requiring the owner to walk the game. That includes the four-question
  passability capture in the backlog, the dungeon geometry decode, and C5.
* Starting CP5, CP6 or CP7 implementation.
* The `A`-`Z` dungeon byte decode. It is the biggest prize and it needs a live session
  with the owner driving; leads are recorded in the backlog item, do not burn the night
  on it.
* Bulk-adding `# provenance: static` to the 101 functions missing one.

---

## Progress report

Write to `docs/phase4-overnight-cp5-report.md` as you go, not at the end: one section
per task with status (`DONE`, `PASSED — reason`, `BLOCKED — what is needed`), the
evidence used, and the commit hash if anything landed. If the run dies partway, the file
should still say where it got to.

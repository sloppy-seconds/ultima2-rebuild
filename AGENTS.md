# AGENTS.md — Linux-Native DOS Game Decompilation & Byte-Matching Pipeline

This document defines the rules, isolation policy, and evidence discipline for AI
agents (and human collaborators) reverse engineering early 16-bit real-mode DOS
binaries (e.g., *Ultima II*) into assembly, re-engineering into C, and verifying
exact byte-for-byte fidelity.

> **Executable work orders:** Phase 1 (evidence capture), Phase 2 (function boundaries,
> code/data separation, per-function asm corpus) and Phase 3 (clean byte-exact ASM
> reconstruction, MASM dialect via jwasm target) are **complete and signed off** — see
> `disasm/phase1-summary.md`, `disasm/phase2-summary.md` and `disasm/phase3-summary.md`.
> Phase 4 (portable C engine reimplementation from the asm/ tree) is `docs/phase4-plan.md`;
> the live task queue is `docs/phase4-state.md` and the running log is
> `docs/phase4-implementation.md`. A plan for an enhanced-mode screen layout taken
> from the FM Towns version is `docs/enhanced-layout-plan.md` — plan only, nothing
> built, and its § 4 lists four decisions (viewport shape, frame size, string
> provenance, when it turns on) that need the owner before work starts.
> **The vehicle state machine is done except `cmd_L`'s flight-loop
> gating: read `docs/handoff-vehicle-state-machine.md` for the byte evidence, but note its status
> block at the head corrects three rows of its own § 1 table, and two things it named as blockers
> have since closed — `u2_world_store` no longer writes a dirty map to the `monx` filename, and
> BOARD's tile write is gated (CP5 V1) via `u2_world_terrain_put`.** W1 (BOARD), W2 (per-mode
> passability), W3 (per-mode food) and W4/W5 (`cmd_L`) are implemented and W1/W2/W3 are gated as
> CP5 P1 and V1. The one genuinely open question there (the plane's airborne flag, W6) is marked as
> needing a capture and not a guess, and is now `docs/needs-gameplay.md` G43.** **Scoped handoffs are
> the entry point for their own topic, not history:** `docs/handoff-commerce-transact.md` (**executed
> 2026-10-29, the day it was written — read its § 0 status block first**; `cmd_T` is now implemented
> in `engine/src/game/commerce.c` and gated by `make test-cp6`, and the headline is that three of its
> four "not evidenced" items closed statically: the grocery quote was `0020` and not `0200`, the axe
> was 20 and not 25, `asm/1590` is a quadrant-and-site-kind switch rather than the menu-versus-line
> fork, and the tlkx record index is the creature role byte minus `0x80`. **The talking branch is
> now implemented, both arms** (2026-10-29): `u2_world_load_tlkx` and `u2_world_tlkx_record` in
> `engine/src/core/world.c` carry the scripted arm, and `asm/40CB`'s `street_*` typed chain is
> wired with its seven literals extracted as `run` locators, because each is a split `db` around
> an embedded `0x0d` the db-index locator cannot see. `CP6 talk` in `run_cp6.sh` guards both
> against the real `tlkx22`/`monx22`), and
> `docs/moongates.md` (time doors:
> observed mechanic, decoded table, engine implementation, and three recorded self-corrections worth
> reading for method as much as content), and `docs/install-design.md` (**the current entry point for
> anything about assembling a playable game directory** — installer modes, the layout and precedence
> decisions still open, the invariants, the gate, and what `docs/handoff-themes-builder.md` left
> behind now that the theme work is locked off), and `docs/demo-record-playback.md`
> (**executed 2026-11-07, the day the status block names — read its § 0 first**: the
> engine's `Ctrl-R` record and `--playback` verify subsystem, gated by `make
> test-demo`, with fifteen deviations from the owner-ruled plan and each one's
> measurement written down rather than edited silently into the body), and
> `docs/handoff-save-scoped-worlds.md` (**executed 2026-11-05, the day it was written —
> read its § 0 status block first**) moves sector write-back out
> of the content directory into a per-save overlay under `saves/<slot>/`, which is where `mapx`
> mutation, the `player`/`PLAYER` case collision, and multiple save games all land. Spec is
> `docs/save-scoped-worlds.md` and its D1-D6 are all ruled, so do not re-argue them. Two things
> the plan got wrong are recorded in that status block: S2 and S3 cannot be separate commits,
> because D1 makes the slot directory and the flat record the same path; and the engine adopts
> a record a pre-slot tree left behind (`u2_slot_adopt_legacy`, spec § 4b, an owner-approved
> amendment to D6) because the drop-in layout has no installer to run. The gate for the whole change is `engine/tests/content_immutable.py`, wired
> into `run_cp4.sh`, and it runs its own red control every time. **To start a fresh
> agent, read `docs/handoff-2026-11-03.md` first (**current entry point** — the rocket and
> space session: G39, G59, G60 and G62 closed and G63/G64 added. `DS:13 = 0x28` finally
> observed after 167 empty harvests; the parked-rocket triple `DS:69/6A/6B` decoded, which
> is the reason a rocket launch silently saves; the world coordinate tables at file
> `0x7212`; the off-course gate `(fuel & 3) == 3`; `YOU HAVE EXPLODED!` shown to be a halt
> and not a death; death shown to be memory-only and not a save; site entry shown to
> auto-save and to pin the outdoor state; and G62 closed by finding that **there is no
> discriminator byte** — the in-space command loop is `asm/6999` and it has no `ret`. **§ 2
> of that file is mandatory reading: seven claims proved wrong, five written by this
> project inside one day, three corrected within hours of being made.** Its § 4 carries the
> working recipes — harvest-before-teardown, headless for scripted driving, find fields by
> DS diff, locate tables by pattern, controlled A/B, mutate your gate — and § 5 the
> hazards, including the trace loss on `up.sh --down` and the runtime/file offset
> direction). Then `docs/handoff-2026-10-29b.md` (superseded as the entry point but **not
> obsoleted** — its § 4 recipes and § 5 hazard list still stand, including the Spice86
> noVNC key-injection crash that the 11-03 session walked into anyway), then the overnight
> run that
> closed D13, G40 and D12 and part of G46: `DS:4E` is charisma proven twice, the horse really does
> refuse shallows and the engine already matched, `DS:25` is the site floor and is renamed
> `site_floor`, KILL and BLINK observed, `cmd_D` executed at last, plus the three hold-a-value-
> against-the-tick recipes in its § 4 and the Spice86 noVNC key-injection crash in its § 5), then
> `docs/handoff-2026-10-29.md` (the queue it supersedes,
> with items 2, 4 and 5 now ticked in place — **§ 2 is the part that
> matters: six recorded decodes proved wrong, four of them written by this project inside a week
> (`cmd_T`'s one-cell ray, `cmd_Y`'s "unresolved" fall-through which was a complete freeform input
> loop, the class names in our own editor, and an on-foot stamp hardcoded to the one value a Fighter
> would agree with); § 1 is open owner decisions D11 and D14 (the cheat-code flag, and whether
> `cmd_C` ships gates without effects — D12's `DS:25` and D13's charisma-vs-wisdom dispute both
> closed 2026-10-29, the latter by `captures/spice86/d13-cha-vs-wis`); 1222 `asm/` files,
> whole-binary md5 green, `check_all` green, 23 of 26 handlers implemented and **all 26 letters have
> now been seen executing** — `cmd_I`, `cmd_D` and `cmd_K` closed 2026-10-29 without a capture at
> all, because all three were readable the whole time and had been cut in half by the Phase 2 map
> (each is a one-byte `e8` whose operand and text live in the next fragment); `cmd_R` and `cmd_N` closed 2026-10-29 by
> `captures/spice86/cmd-nr-live`, which is also where `DS:E3` turned out to be `items_held[13]`, a
> STRANGE COIN, and where the string emitter's greedy `\xNN` escape was found to have been corrupting
> five shipped menus; § 3 is the ranked queue, § 4 is facts worth knowing cold (the
> sector's ones digit IS the site kind; the stamped grid at `DS:1800` is not the raw `mapx` bytes),
> § 5 is the copy-paste capture recipes including the live owner-play loop, § 6 is the hazards that
> cost this session time), then
> `docs/handoff-2026-10-28f.md` (the queue it supersedes: the command-loop evidence split was
> corrected from 14/12 to 22 proven / 4 unproven, the command dispatcher landed at
> `engine/src/game/commands.c` in the `asm/0A1F` shape, the marker gate
> `tools/cmd_marker_gate.py` gained 15 controls and went into CI, and `asm/4C50` was shown to render
> a string table as invented instructions and is NOT trustworthy as code), then
> `docs/handoff-2026-10-28e.md` (the queue it supersedes: the forced-keep corrections went 36 → 0,
> the rendering-gap list emptied, three documented dispositions were retracted, and both mutation
> harnesses were gates that exited 0 while printing FAIL), then
> `docs/handoff-2026-10-28d.md` (the queue it supersedes is closed except the two verdicts;
> what survives there is the speed-system identification, the era-table G14 closure, and the
> body-correction marker path's own rationale), then
> `docs/handoff-apple2-crosswalk.md` (executed 2026-10-28d, status block at its head: the
> static Apple II ↔ DOS anchor crosswalk is built and gated, all six questions Q0–Q5 carry a
> written verdict — five corroborated, one Apple-silent — the anchor count in
> `docs/apple2-reference.md` § 4 is measured rather than remembered, and what survives there is
> the "needs a person" list), then
> `docs/handoff-apple2-reference.md` (the queue it supersedes: all five items closed, contract 2's
> two named holes gone, three captures run and one of them refuting a claim made the same day, and
> the branch fast-forward merged into `main` — unpushed as of 2026-10-28), then
> `docs/handoff-2026-10-28b.md` (the two owner-approved renames and
> three owner-reported creature bugs are done, `mon_role` gates the town sweep only, and what is
> left there is for a person), then
> `docs/handoff-2026-10-28.md` for the recipes and the hazard list (its own queue is now
> closed: the spawn selector is pinned at runtime, the confirming unlock is on record, the Z
> screen and CP5 C3 are shipped — what survives in that file is the working recipes, including
> how to push from an agent shell and how to force a live value, and the twelve hazards that
> cost the last two sessions time). The work it supersedes is in
> `docs/handoff-2026-10-27-night.md` (its item queue is now resolved: 1, 6, 7, 8 done, 4 wired,
> 5 closed statically, 2 re-blocked on the selector, 3 spec'd not built; plus the nine hazards
> from that session), then `docs/handoff-cp5-2026-10-27.md` (CP5 W1–W9 and towns are done,
> what was proven, the single mistake that caused three false blockers), then
> `docs/phase4-state.md` § CP5.
> `docs/phase4-kickoff-cp5.md` still holds the CP5 wiring build order and the work-item
> list, but its sequencing is now historical — every W item on it has shipped or been
> replaced by a byte-level finding. Superseded handoffs
> (`docs/handoff-cp5-2026-10-24.md`, `docs/handoff-cp5-2026-10-23.md`,
> `docs/handoff-2026-10-23.md`) are historical record, not
> entry points; the 10-24 one is still worth reading for its traps and its working
> recipes. Older ones live under `docs/archive/`.

> **What this file is.** Rules and orientation only. It does not contain source
> code, tool listings, or reference material — those live in the files named
> here. Anything that quotes a file will drift from it and then lie.

---

## 0. Project Scope & Isolation (READ FIRST — HARD RULE)

**This project is standalone. The only directory an agent may read, write, search, list, or reason from is this project root and everything beneath it:**

```
/var/home/paul/Projects/decompile-u2-v2/
```

### Absolute prohibitions

* **NEVER read, list, search, open, or reference anything outside the path above.** This includes parent directories, sibling projects, and any other checkout of the same game.
* **NEVER use `..`, absolute paths outside the project root, or `find`/`grep`/`ls` that escapes the project root.** All paths in commands and code are relative to the project root or absolute under it.
* **NEVER make decisions, take hints, copy code, compare output, or infer intent from any other directory.** In particular these sibling directories are OFF LIMITS and are NOT the source of truth:
  * `/var/home/paul/Projects/decompile-u2` (the older v1 — a different, unrelated attempt)
  * `/var/home/paul/Projects/ultima2`
  * `/var/home/paul/Projects/agents/**` and every other folder under `/var/home/paul/Projects/`
* Do not "check a sister/previous version for reference." If information appears to be missing from this project, it is missing — treat it as an open question for the user, not something to go find elsewhere.

### In-project exception: the read-only backup

* `references/ultima2/` is a **pristine, read-only backup** of the original game files, kept inside this project in case the working copy in `original/` is ever damaged.
* It is part of this project, so reading it is allowed — but it is a **fallback, not a workspace**. Treat it as immutable: **never write to, edit, rename, or delete anything under `references/`.**
* The working copy is `original/`. Use `references/ultima2/` only to restore a damaged file from `original/`, and only with the user's approval.
* **One exception lives beside it, and it is not game content.** `references/ultima3/` is a
  third-party clone the owner added on 2026-10-24: a MIT-licensed Ultima III remake, a
  different game, kept here as a reference implementation. It is read-only for the same
  reason (nobody edits a reference), it is gitignored, and it is *not* a source of truth about
  this binary. Read `docs/external-ultima3-remake.md` before using it — that file carries the
  licence line (MIT code, not-ours art) and the two quarantines.
* **`references/win/` is a third-party rewrite, not a copy of this binary.** The owner
  added it on 2026-10-29: John Alderson's *Windows Native Ultima II* v1.01 (2000) — a
  native Win32 MFC **reimplementation** that reads the same data files, plus a set of
  community-sourced planet maps. It is read-only for the same reasons, it is gitignored,
  and it is **not** evidence about `original/ultimaii.exe` — the author himself lists the
  places where his arithmetic diverges. Read `docs/external-windows-port.md` before
  touching it; that file carries the rewrite-not-recompile evidence and the measured
  divergence between its `MAPG` maps and our `mapx` files.
* **`references/apple/` is the Apple II original, and it is game content.** Three 140 KB
  UCSD p-System volume images the owner added: the 6502 build the DOS port descends from.
  Reading it is allowed and it is now load-bearing, because it is how the lineage claim in § 1
  got tested. It is read-only for the same reason as the others, it is gitignored, and its
  extracted files land in `decompiled/apple2/`, which is also gitignored: **never commit
  them.** Two hard caveats, both measured: this is the **1989 Origin re-release**, not the
  1982 original, and it is a **corroborator, not a parent** — `--INVALID MOVE!` and the
  direction words exist only on the DOS side. Read `docs/apple2-reference.md` before using
  it; it carries the extraction recipe, the memory map and the provenance line.

### External tool binaries (allowed)

The isolation rule is about **data and decisions**, not about refusing to use installed software. Analysis tools may be invoked by path as infrastructure, exactly like `dotnet` or `objdump` on `PATH`:

* **Vendored in-project:** Spice86 (`tools/spice86/`), Reko (`tools/reko/`) and CiderPress2's `cp2` CLI (`tools/ciderpress2/`) — self-contained builds, gitignored. `cp2` is GPL-3.0 and is the only correct reader for the Apple II images; see `docs/apple2-reference.md` § 2.
* **Referenced by path:** Ghidra lives outside the project (`GHIDRA_HOME`, default `/var/home/paul/Projects/ghidra-12.1.4`). It is Apache-2.0 and too large to vendor, so it is the one external-tool path exception. `source tools/env.sh` wires it up.

Running a tool binary is fine. What the rule forbids is importing **prior findings** — earlier attempts at decompiling Ultima II (entry-point lists, symbol names, struct layouts, conclusions) from a sibling directory such as `decompile-u2` or `ultima2`. Generic, binary-agnostic tooling is safe to reuse; anything that encodes a *conclusion about this binary* must be regenerated from `original/` and `captures/` inside this project. Never browse a sister directory to "check how they did it."

### Why this rule exists

A previous agent read a sibling directory and made decisions based on it, which corrupted this project's work. Each project here is independent. Cross-contamination between `decompile-u2`, `decompile-u2-v2`, and `ultima2` has caused real confusion. Isolation is not optional.

### If you genuinely need something outside the project

Stop and ask the user, naming the exact path and why. Do not go look on your own.

### Provenance of the original files (important for byte-matching)

* The game files in `original/` (and the backup in `references/ultima2/`) were downloaded from the **Internet Archive**, not from a factory-shipped distribution.
* The pack **appears to already have community patches applied**. So these files may **not be 100% the original shipped binary**.
* This is accepted and expected — it is not a defect to chase. Treat the bytes in `original/ultimaii.exe` as the ground truth for *this* project, while remembering that a byte-for-byte "match" here means matching this patched pack, not necessarily Garriott's pristine 1983 release.
* Do **not** go hunting the internet or other directories for a "more original" binary to diff against. If a discrepancy looks patch-related, note it and move on; raise it with the user only if it blocks the task.

### Licensing & Commit Policy (Apache-2.0)

This project's own work is licensed **Apache-2.0** (see `LICENSE`). To avoid copyright
infringement and GPL/AGPL license contamination, **anything not compatible with Apache-2.0 must
never be committed to git.** The `.gitignore` enforces this; do not work around it.

* **Never commit — copyrighted game assets:** `original/` and `references/` hold the Ultima II
  binaries and data, which are not ours to redistribute. Users supply their own copy. These are
  gitignored; never `git add -f` them.
* **Never commit — GPL / incompatible tool builds:** `tools/spice86/` (GPL) and `tools/reko/`
  (GPL-2.0) are vendored, re-fetchable builds, not our source. Ghidra is Apache-2.0 but lives
  outside the repo. No AGPL-licensed code may be introduced anywhere in this project.
* **Never commit — bulky derived dumps:** raw runtime captures (`captures/spice86/*.bin`,
  `*.json`) and full decompiler output (`decompiled/`) are regenerable working artefacts derived
  from the copyrighted binary; keep them on disk, out of git.
* **Commit freely — our own authored work:** `AGENTS.md`, `docs/`, `tools/*.py`,
  `tools/ghidra/*.java`, `tools/env.sh`, the per-tool `README.md` files, and curated analysis
  notes we write (`disasm/*.md|tsv|txt`, reports, summaries). Small factual findings (addresses,
  sizes, verdicts) expressed in our own prose are fine; verbatim copyrighted blobs and GPL
  binaries are not.
* **The text/art line, ruled 2026-10-28.** Copyright protects game text exactly as much as it
  protects a sprite — there is no category exemption, and nobody should argue otherwise. The
  line this project actually draws is about *where the bytes live and whether they are
  load-bearing for the proof*:
  * **Text is committed, because it cannot be otherwise.** Every string the engine must print
    lives inside `ultimaii.exe`, and `asm/` is a byte-exact reconstruction of that file. There
    is no way to have the reconstruction without the text, and there is no cleanroom path for
    it: if the engine prints anything other than `NO DOOR THERE!` it is behaviourally wrong.
    Short functional strings are also the weakest claims that exist, which is most of them.
  * **Art and sound are never committed, and are substituted under the cleanroom.** They live
    in sidecar files (`pic*`, the tile data) that `asm/` does not reconstruct, and unlike text
    they *can* be redrawn — which is why `engine/assets/tiles/cleanroom/` and
    `docs/cleanroom-procedure.md` exist.
  * **A second copy of the text is allowed only where a generator writes it.** What is
    forbidden is a *hand-maintained* copy, because two hand-maintained copies drift and then
    lie. `engine/content/gen_content.h` is committed (owner ruling, 2026-11-02) because the
    alternative was the worse defect: the asm fallback resolves 181 of 220 strings and blanks
    the other 38, so every build without the player's `ultimaii.exe` — every CI runner, and
    therefore every release binary — shipped a blank character-creation screen and let any
    test that greps the screen for text pass vacuously. The discipline that replaces "never
    commit it" is three checks, all enforced: the extractor is the file's only writer, `make
    -C engine content` refuses to run without the exe so a fallback can never overwrite the
    good copy, and `make -C engine content-check` proves **214 of the 220** committed
    strings are present as data in `asm/` and that none of them is empty; the six that
    no locator can reach are named in a list that only ever shrinks. The build has no
    rule to remake the header at all, which is what makes a missing file a loud failure
    instead of a silent downgrade. Hand-maintained duplicates of game text in `docs/` stay
    forbidden: they have no generator and no gate.
* **OPEN TODO — the committed text table, for i18n.** A locale layer wants one canonical table
  keyed by stable IDs, so a translation can replace the English without touching code. The keys
  already exist: the extractor's `STRINGS` list is exactly `U2_S_NAME -> (asm file, which db
  line)`, committed, with the values generated. The gap is that the indexed name pool (weapons,
  armour, spells, items, stats, races, classes — `docs/status-screen-z.md` § 7) is emitted as
  seven whole arrays with no per-entry symbol, so a translator would translate arrays rather
  than entries. When i18n is scheduled, give each pool entry a `U2_S_`-style ID and keep the
  values emitted by the extractor into the committed header, never hand-written. Deliberately
  not done now.
* When in doubt about whether an artefact is safe to commit, **do not commit it and ask the user.**

---

## 1. Domain Context: Compiler & Tool Selection

*Ultima II* was hand-coded in 6502 assembly for Apple II and ported to x86 MS-DOS
as **pure assembly** — there is no C compiler output in the original binary.

> This was long an assumption carried in prose. It is now evidenced: the Apple 6502 build is
> extracted and disassembled, `JSR $4006` is the same inline-string printer contract as the
> DOS `str_out`, and 35 strings match verbatim in the same table order on both sides. Full
> record, including where the two builds **diverge**: `docs/apple2-reference.md`.

* Strict byte-for-byte matching therefore targets an **assembler** (JWasm, MASM
  dialect), which is what Phase 3 did and what CI verifies.
* The C engine in `engine/` is a **reimplementation**, not a byte-matching
  exercise: behavioural fidelity argued from the asm tree and confirmed by
  testing and playthroughs. It is not byte-proven and never will be.
* For mid-to-late DOS era titles compiled from C (Ultima IV/V and later), the
  historical compiler itself becomes the target. That does not apply here.

## 2. Pipeline and Current Reality

| phase | state | artefact |
| --- | --- | --- |
| 1 evidence capture | done | `captures/`, `disasm/phase1-summary.md` |
| 2 boundaries + code/data split | done | `disasm/phase2-summary.md` |
| 3 byte-exact asm reconstruction | done, CI-verified | `asm/`, `disasm/phase3-summary.md` |
| 4 portable C engine | **in progress** | `engine/`, `docs/phase4-state.md` |
| 5 portable build behind INT/port seams | planned | — |

Verification, in two independent layers:

* **the proof** — `bash tools/assemble_clean.sh` rebuilds the whole binary from
  `asm/` and checks it against the known md5; `python3 tools/check_func.py <SEG>`
  proves one function. CI workflow `asm-checksum` runs this on every push and
  must never be red.
* **the engine** — `make -C engine && make -C engine test` runs CP0..CP4
  acceptance tests. CI workflow `engine-tests`. The two workflows are separate
  on purpose: engine work-in-progress must not look like a regression in the
  reconstruction proof.

## 3. Working Rules

* **Command-loop scope is ruled (owner, 2026-10-28e): implement all 26 handlers, and the
  project is NOT code-complete while any unproven one stands.** The list of which letters are
  unproven is **not written here**, because a hand-maintained copy of a derived fact drifts and
  then lies — this bullet carried "twelve unproven: C D H J M N O S T V W Y" long after the
  corpus had moved on, and the marker rule attached to it pointed at handlers that had since
  gained execution evidence. **`python3 tools/cmd_handlers.py` is the authority**: it prints
  the crosswalk, its `exec` column is the evidence, and it is a gate that exits non-zero when
  the jump table, the filenames and the provenance lines disagree. Read it, do not recall it.

  What the rule means in practice, independent of the current count: a handler that is
  **implemented** but has never been observed executing carries an in-source marker —

  ```c
  /* UNPROVEN: cmd_C (CAST) has never been observed executing in a capture.
     Behaviour is static inference from asm/310B_cmd_C.asm. This handler is not
     code-complete until a capture exercises it. See docs/needs-gameplay.md. */
  ```

  A letter that is unproven **and** unimplemented owes no marker, because it is an honest gap
  rather than a stub that looks finished; `tools/cmd_marker_gate.py` enforces exactly that
  distinction and is wired into `check_all.sh`.

  The marker is not a formality and not a TODO. **A checkpoint, a milestone, or the phase may
  not be declared done, signed off, or "complete" with unproven handlers still carrying it** —
  say "complete except the N unproven command handlers" instead, or go get the capture. This
  exists because a stub that looks implemented is worse than a gap that says what it is: the
  CP5 scope rule already learned that a plausible boolean standing in for a proven distribution
  passes the tests that do not exist yet and reads as finished. Every handler written from
  static inference alone is another place where that can happen quietly.
  `python3 tools/cmd_handlers.py` prints the crosswalk and its `exec` column is the evidence;
  it is the authority for which ones, and it is a gate that exits non-zero when the jump
  table, the filenames and the provenance lines disagree.

* **`asm/` is the one lookup table for address → behaviour. There is no second
  one.** The filename carries the discovered name (`asm/0936_walk_loop.asm`) and
  the header carries `# name-evidence:`, `# provenance:` and `# confidence:`.
  Do not create flow tables, address cheat-sheets or symbol indexes in `docs/`
  or `disasm/` that duplicate this — two hand-maintained copies always drift and
  a stale index is worse than none. Any derived view must be *generated* by a
  tool, never kept by hand.
* **Look it up before you investigate it.** Start with
  `ls asm/ | grep -i <word>` and read the header. If it is already named, the
  work is done and re-deriving it from raw disassembly is a process failure.
  Then **spot-check one cited byte or instruction** to prove the entry still
  holds before trusting the rest of it.
* **When a session learns or corrects an address→behaviour fact, the same commit
  renames the `asm/` file and writes the header.** Not a follow-up, not a
  backlog item. If no file covers the address yet, record it in the owning
  function's header and say so.
* **A gate has to be able to fail in the place that enforces it, not just locally.**
  `engine/tests/asm_body_guard.py` compared the working tree to `HEAD`, and CI checks out a
  clean tree, so in CI it compared `HEAD` to itself and could only ever print `conserved`.
  A committed, unmarked body edit passed. Separately, `run_cp0.sh` runs the guard from
  `engine/`, where there is no `asm/`, so it inspected zero files and CP0 reported that as
  a PASS. Both were gates that had never once been capable of failing where it counted.
  The guard now takes `--base REF`, refuses with exit 2 when run in CI without a real base
  or when it finds no `asm/` files, and resolves the repo root itself. CI passes the merge
  base in `.github/workflows/asm-checksum.yml`. **When you add a gate, write the control
  that makes it red first, and run that control from the same directory and the same ref
  position CI will use** — a control run from a comfortable place proves nothing. The
  controls live in `engine/tests/asm_body_guard_selftest.sh` (10 of them) and are wired
  into `tools/check_all.sh`.

* **The one exception: a provably-wrong body may be corrected, if the file says so.**
  Freezing a body that is known to be false protects a lie, not a proof. A file may change
  its body only if it carries
  `# body corrected: YYYY-MM-DD -- evidence: <what proved the old decode wrong>`
  and the marker names its evidence. `asm_body_guard` permits marked changes and fails
  unmarked ones; **`assemble_clean.sh` is still the authority**, because a marked correction
  that changes any byte fails there. The two gates are different on purpose: the guard polices
  authorship, the byte match polices truth. Merging and splitting still need no marker, since
  they preserve the canonical stream. `tools/forced_keep_audit.py --selftest` enforces six
  hand-verified cases — three known-true boundaries and three known-false ones that must stay
  quiet — and `engine/tests/asm_body_guard_selftest.sh` proves the guard can still
  go red in all four shapes (unmarked change, marked change, marked change that alters bytes,
  merge). Use the marker only for a decode the bytes themselves refute — `asm/353A` and
  `asm/353D` are the model, where a forced-keep swallowed three bytes and the next fragment
  invented `push %ds` and `dec %cx` from the leftovers.

* **In `asm/`, only the filename and `#` comment lines may change. Labels are body.**
  A discovered name goes into the filename and the header; the `sub_xxxx:` label line
  stays exactly as it was. `engine/tests/run_cp0.sh` md5s every non-`#` line, both
  across a rename and on a modified file, so editing a label fails CP0. **This is the
  most frequently repeated mistake against this tree** — the instinct is that renaming
  `sub_2cd8:` to `keys_add_two:` alongside the file is tidier, and it is forbidden.
  Note it in the header instead (`# label: kept as ... on purpose`). Changing a label
  at all is a change to a CP0 invariant, not a naming detail: stop and ask.
* **A wrong name is worse than no name.** If bytes contradict an existing name,
  add a `# NAME DISPUTED:` line in the same commit and propose the rename;
  do not silently rename on your own inference.
* **Third-party data is a hypothesis the bytes get to veto.** `bestiary/` attaches
  community creature names to art indices, and it does so by measured bitmap
  identity — but the *name* itself comes from a wiki, not from this binary, and never
  will. Keep the two grades apart in anything you write, do not promote a third-party
  name into an `asm/` filename, and keep `bestiary/cache/` away from anyone drawing
  substitute art; it holds the originals in more variants than `original/` does.

* **The cleanroom wall separates the implementer from the originals, and an answer key is a
  breach just like a screenshot is.** `docs/tile-art-spec.md` is the only document someone
  drawing substitute art may read. It may carry **intent** (what a tile must communicate) but
  never a **trace** (pixel coordinates, measured densities, periods, component sizes) — a trace
  lets the original be reproduced without looking at it. Evidence and the bands fitted to it
  live in `docs/tile-art-evidence.md` and `tools/tile_rules.py`, both listed as forbidden in the
  spec's § 2. The gate is run **against** finished art and never handed to whoever makes it. If
  you are doing a redraw, you must not read either file. The whole procedure, the
  verification chain, the build history and the controls that failed are written down in
  `docs/cleanroom-procedure.md`.
* **`docs/needs-gameplay.md` is the capture worklist, and every blocked-on-play
  question belongs on it with a who-can-do-it verdict.** When a finding is recorded
  as inferred-but-never-observed, or a question is closed as needing a live session,
  the same commit adds or ticks an item there. Two rules come with it. First,
  **"needs gameplay" is not "needs the owner"** — say which, because the difference is
  a scripted afternoon against waiting on a diary appointment, and the project had
  conflated them until a "owner must walk" label survived on items an agent could
  drive. Second, **an item is only closed by a named capture directory**, never by a
  belief about how the game works.
* **`docs/harness-portability.md` is a live design report, and updating it is a
  same-commit obligation.** When a finding would change how a *generic* DOS
  decompilation harness should be built — an ISA idiom that broke a detector, a tool
  that returned a plausible wrong answer, a gate that could not fail, or a question
  only play could settle — the commit that records the finding also adds a line to
  that report's feedback log (§ 6). Most of these arrive disguised as ordinary bug
  fixes, which is exactly why they get missed. **Method transfers; conclusions do
  not.** That report must never contain addresses, DS layouts, record formats or
  symbol names from this binary — it is the boundary between what a future project
  may inherit and what it must re-derive from its own bytes. If a line only makes
  sense for this game, it belongs in `asm/` or the findings doc instead.

* **Every action the player takes, and every effect on the player, must produce a message.**
  Owner rule, 2026-10-26, after a live session showed HP draining from an adjacent monster while
  the log printed nothing but `CMD: PASS`. A state change that happens silently is a defect, not
  a design choice, and it applies in both directions: the player's own commands and anything the
  world does to them.

  The boundary is fidelity, not taste. Where the original has a message, the engine must print it.
  Where the original is genuinely silent, the engine stays silent **and says so in a comment at the
  mutation site**, citing the routine that proves it. "The message is probably elsewhere and I
  have not read it" is never a licence for silence — that is an open decode task, and the comment
  must name it. Silence that is not justified by a cited byte is the failure mode this rule exists
  to stop, because a silent engine looks finished and reads as broken.

  **A sound counts as a message** (owner, 2026-10-26). Where the original's feedback for an effect
  is a speaker tone rather than text, wiring the tone satisfies the rule and inventing text does
  not — the melee-wound path is exactly this case, where the contact loop plays a tone and prints
  nothing. The rule is that the player must be told something happened, not that it must be text.
  A guessed tone is worse than none: the divisor and duration come from the bytes or not at all.

  Known violations and their evidence live in `docs/phase4-state.md`, section "Silent player
  effects". Adding a mutation without a message and without a citation is a review failure.

* **Never write a probe, reader or driver that duplicates an existing tool. Look first.**
  The recurring failure in this project is not a wrong answer, it is an agent building
  something that already exists and getting a *plausible* result from it — a hand-rolled
  `dotnet` launch that silently skips the harvest, a text-mode screen decoder that returns
  confident box-drawing noise, a key injector that is rejected as an illegal command. The
  tree already has the tool; the index is `docs/spice86-capture.md` "Which tool for which
  job", and every `tools/*` takes `-h`. If a capture job feels like it needs a new script,
  the answer is almost always an existing one plus a few lines over it, and saying which
  one you used is part of the write-up.
* **Read `docs/spice86-capture.md` before driving the emulator, and use the scripts.**
  `tools/spice86_start.sh -h` lists every option and `tools/spice86_stop.sh` is the
  only safe teardown. That doc has a "Which tool for which job" table because this
  tooling was re-derived from scratch repeatedly — a hand-rolled `dotnet` launch skips
  the run-dir copy, the port pick, the state file and the harvest, and the harvest loss
  is silent. Two facts worth knowing cold: an agent shell on this host has **no display
  and no Xvfb**, which means there is no host window to focus — it does **not** mean no
  keystrokes. `tools/spice86_type.py` injects keys over the MCP endpoint with no window at
  all, and `deploy/spice86-novnc/up.sh` runs the emulator in a container that brings its own
  Xvfb and noVNC. `--headless` remains the default for unattended work because it is the
  simpler path, not the only one; and headless Spice86 **ignores SIGTERM and SIGINT**, so a CFG
  harvest must come from `monitor dumpAll`, never from a signal. When a capture seems
  to need walking somewhere, seed state instead — `--player` with `tools/u2_edit.py
  setpos` is the only way to change sector, and `--seed-map` installs a synthetic one.
* **Dungeons are open now (CP7, 2026-10-29), and the 3D view is still not evidence about
  the 2D world.** The park was the owner's call on 2026-10-28 and it was lifted on
  2026-10-29 to build `engine/src/game/dungeon.c`; the engine enters, lights, walks,
  fights, loots and descends, and `make test-cp7` gates it. What the park was protecting
  still holds and is the reason the two rule-sets stay separate: a dungeon is a
  first-person wireframe over a grid of 16 pages of 16x16 with its own byte semantics —
  `0x80` is rock there and something else on a sector map, `0x40` is a chest and not a
  tile — and nothing observed in a dungeon may settle how the overworld or a town draws,
  stamps, moves or kills anything, or the other way round. `docs/art-index.md` already
  records that dungeons do not stamp the grid the same way, and the art-index and
  creature-stamp rules were both measured in sector 00 and sector 31, which is why they
  hold. See `docs/phase4-state.md` § CP7 for what is decoded and what is still open.

* **The screen is a screenshot, never a decode.** The game draws text as **glyphs in
  graphics mode**, so there is no character cell to read: `0xB8000` as a text page is
  box-drawing noise and as CGA planes is a structured, half-plausible, wrong image. Use
  `tools/spice86_screenshot.py out.png` (MCP `screenshot`, no display needed) on a live
  run, and `tools/vga_screen.py` on a full `spice86dumpMemoryDump.bin`. A turn-recorder
  `turns/NNNNN.vga` is **not** a screen — it is the 6400-byte span the game's own
  screen-invert routine touches, and no renderer reconstructs a view from it; read the
  `.ds` dump for state instead, and screenshot live for pixels. This has been gotten wrong
  three times, each looking like a partial success.
* **A behavioural claim needs a frame.** The bullet above is about *how* to read the screen; this
  one is about *whether to look at all*. Memory and `ExecutedInstructions` answer **what changed**
  and **whether it ran**. Only a screenshot answers **what the player saw**, and the two can
  disagree. `cmd_V`'s helm branch executed, its counter decremented, and the render turned out to be
  a whole world map — nothing the code shape or the memory trace suggested, and the claim had already
  been written into an `asm/` header before anyone looked. So: when a finding describes something
  visible — a message, a screen, a sprite, a mode change — the capture that proves it **includes a
  frame**, and the header names it. A claim about visible behaviour backed only by bytes is graded
  `inferred`, however solid the trace is. Screenshots are cheap and the vision model is already
  here; cost is never the reason one was skipped. Read state *before* the action too — the `cmd_V`
  confusion started with a counter read only after the press, which made a consumed item look like an
  absent one.
* **Headless play is a solved loop; do not reinvent it.** `spice86_start.sh --headless
  --patch-arrows --player <file> --auto-play` loads a seeded character into the world
  unattended, `tools/spice86_type.py --keys A,Right` drives it (arrows only work because
  of `--patch-arrows`; without it every one is `ILLEGAL COMMAND!`), `spice86_screenshot.py`
  shows it, and reading DS over the HTTP API at `0x08BE*16` gives position `0x00/0x01`, HP
  `0x51`, the 32 creature slots at `0x137/0x157/0x177/0x197/0x1D7` and the sector grid at
  `0x1800`. Confirm a key landed by reading memory, never by the send returning ok — a
  dropped key looks exactly like a no-op.
* **Never run the game in place.** The game rewrites its own `player` file.
  `tools/spice86_start.sh` copies pristine `references/ultima2/` into a fresh
  timestamped dir under `tmp/` for every launch. Scratch and logs go to
  `/var/tmp`, never `/tmp` (which is tmpfs here and takes RAM from the model).
* **Never edit a file with a script.** Use the edit tool. A `str.replace` that
  does not match returns the string unchanged and exits 0, so a no-op and a
  success are indistinguishable. Same trap as `sed -i` with a pattern that
  misses.
* **Never heredoc a non-trivial script.** Write the file, syntax-check it, run
  the file.
* **Ask before capturing, and never block for minutes.** Long foreground captures
  swallow the owner's queued messages. Run detached and check between turns.
* **`make test` must be green at every commit that can break something.** Run it when a
  commit touches engine source, `asm/`, `engine/tests/`, tooling, or anything a gate reads.
  **Skip it for documentation-only commits** — prose in `docs/`, `AGENTS.md`, `LICENSE`,
  `THIRD-PARTY-LICENSES`. Running the suite there buys nothing and costs a couple of minutes
  per commit; the rule exists to catch regressions, not to certify spelling. If a docs change
  describes a gate or a tool, verify the commands it claims still work instead of running the
  suite — a documented command that has silently stopped working is the real defect.
  Baseline moves (CP2 evidence) still need owner approval and a named reason in the gate log
  in `docs/phase4-state.md`.
* **Save compatibility is checked against the real game, locally, on change — not in CI.**
  `python3 tools/save_cross_load.py` loads the original's record into the engine and then has
  the original load a record the engine wrote, reading both back out of live memory. It needs
  `original/` and `references/` (gitignored) and it measures a binary that does not change, so
  it is deliberately absent from `make test` and from every workflow. **Run it whenever save
  handling changes**: `engine/src/core/save.c`, the write-back in `engine/src/core/world.c`,
  the DS layout in `engine/src/core/state.h`, or the harness. Owner decision 2026-10-29;
  `docs/save-format.md` § 9 has the recipe and § 8 the reason a byte-exact comparison across
  the whole 256-byte window is not achievable (`DS:74/75` speed is calibrated at every startup
  and merely lives inside the saved span). `save_check.c` keeps owning the byte-format
  assertions in CI.

* **Spice86 polling:** the memory API sustains ~250 req/s (`tools/poll_probe.py`).
  The old 3 Hz limit belonged to the windowed CFG view model, not to reads.

## 4. Repository Layout

| path | contents | git |
| --- | --- | --- |
| `asm/` | the reconstruction: one file per function (`<ADDR>_<name>.asm`), headers carry evidence, confidence and provenance | tracked |
| `engine/` | portable C11 engine (Phase 4), own `README.md`, tests, `make` targets | tracked |
| `engine/assets/tiles/cleanroom/` | original substitute tile art, created under cleanroom protocol; `PROVENANCE.md` is the legal record and travels with it. Never a reproduction of the game's art. The implementer's only legal input is `docs/tile-art-spec.md`; the evidence and the gate's bands live in `docs/tile-art-evidence.md` and `tools/tile_rules.py` and are forbidden to them. **The full procedure, verification chain and recorded failures are in `docs/cleanroom-procedure.md` — read it before commissioning or performing a redraw.** | tracked |
| `tools/` | our authored Python/shell toolchain: `check_func.py`, `assemble_clean.sh`, `decode_str.py`, `dialect_masm.py`, `xref_provenance.py`, `cfg_printed.py`, `bcd_census.py`, `provenance_from_traces.py`, `reject_trap.py`, `bp_read.py` (stop the CPU at a runtime address and dump memory there; see the two hazards in its docstring), `demo_key_gate.py` (the control-byte availability proof behind Ctrl-R: no immediate comparison against 0x11 or 0x12 anywhere in `asm/`, and the cited Ctrl-S site still exists; a gate with a selftest, because a claim in prose goes stale the first time an `asm/` correction lands), `cmd_handlers.py` (generated crosswalk of the 26 command letters against `asm/` and the capture corpus, and a gate: it exits non-zero when the jump table, the filenames and the provenance lines disagree), `fragment_splits.py` (nominates routines the clean wave cut into pieces; `docs/fragment-splits.md`), `str_out_audit.py` (enumerates every `call str_out` from the executable and reports the rendering gap — printed messages `asm/` draws as instructions, which `strings_atlas.py` now carries from the calling convention itself as `source='str-out-contract'`; gate + list `disasm/str-out-rendering-gap.tsv`), `check_all.sh` (runs every gate in the project, one verdict per line, non-zero exit if any went red; `--fast` skips the two slow ones — run this before a commit you are not watching), `apple2_seeds.py` / `apple2_ghidra.sh` / `apple2_index.py` / `apple2_anchor_map.py` (the 6502 side: seed scan, whole import, committed index, and the gated Apple II <-> DOS string crosswalk; `docs/apple2-reference.md` § 4 and § 8), `vga_screen.py`, `map_render.py`, `map_viewer.py` (self-contained sector-map viewer; `-o FILE` writes it, `--serve` hosts it on :8099 for a remote look and serves the viewer blob and nothing else — never `http.server` in `tmp/`, which holds a copy of the game per launch), `tile_review.py`, `tile_rules.py`, `fmtowns_theme.py` (the developer's FM Towns v2 derivation from the disc image; the shipped one is `--install --fmtowns-media-dir`, implemented in `engine/src/core/fmtowns.c`, and `engine/tests/fmtowns_check.c` proves the two agree byte for byte), `engine/tools/fmtowns_map2h.py` (compiles `disasm/fmtowns-crosswalk.tsv` into the header the binary carries, so a player needs the disc and nothing else; `--verify` is a gate), `turn_recorder.py`, `vga_ring.py`, `poll_probe.py`, `screenshot_diff.py`, `spice86_{start,stop}.sh`, `u2_record.py` (phase 1 of the cross-game replay plan: drives the original
with a key script paced on **BIOS tick counts, never real seconds**, confirms every key was consumed via
the BIOS keyboard ring, and snapshots the compared DS field set per turn; the recording is
self-describing and carries its own exclusion list with reasons. `docs/plan-cross-game-replay.md`) | tracked |
| `docs/` | phase plans, state, implementation logs, live-capture findings, `install-design.md` (the current entry point for assembling a playable tree: installer modes, the layout and precedence rulings, the invariants, the gate, and what is built versus deferred), `theme-format.md` (the contract for what `--ega-theme-dir` reads: the v1 flat atlas, the v2 manifest format, discovery and resolution order, and every refusal), `spice86-capture.md` (the emulator runbook: headed vs headless, tool index, APIs, gotchas — read it before driving Spice86), `tooling-procedures.md` (input paths and the arrow patch), `harness-portability.md` (the generic-harness design report; method only, never conclusions), `needs-gameplay.md` (the capture worklist with who-can-do-it verdicts), `apple2-reference.md` (the Apple II disks: extraction, memory map, cross-ISA anchors), `sdl2-vs-sdl3.md` (the open SDL major-version decision: measured port surface, options, recommendation — a decision record for a choice that was never made) and the `contract-*.md` behavioural specs, which state a grade per claim and name their own holes | tracked |
| `disasm/` | curated analysis: phase summaries, function maps, verdicts, and `fragment-splits.tsv` (generated by `tools/fragment_splits.py` — regenerate with `--tsv --all`, never hand-edit, and check that both grades came back non-zero). **`function-map.tsv` is a tiling of discovered *starts*, not a list of routines** — a row means "entry point here, region ends at the next start", and routine truth lives in `asm/` headers. See `disasm/phase2-summary.md` before concluding a row is wrong. | tracked |
| `bestiary/` | third-party creature data joined to our tile table by bitmap identity. `bestiary.tsv` and `matches.md` are our own derived facts; `cache/` and `view.html` hold copyrighted art rips in seven platform variants. Read `docs/bestiary.md` before using it, and note the cleanroom entries. | `cache/` and `view.html` **gitignored** |
| `captures/` | runtime captures and screenshots | **gitignored** |
| `original/` | working copy of the game files — the executable **plus** its sidecar content (`mapxNN` sector maps, `monxNN` monster data, `tlkxNN` NPC dialogue, `pic*` pictures, the `player` save). Ultima II is not a single-file game: the 37 KB exe is the engine and the pack is ~94 files, about 2.2 MB | **gitignored** |
| `references/` | `ultima2/` is the pristine read-only backup; `ultima3/` is an owner-added third-party MIT remake clone (see `docs/external-ultima3-remake.md`); `apple/` is the Apple II 6502 original as p-System disk images (see `docs/apple2-reference.md`); `win/` is an owner-added third-party Win32 rewrite and its substitute planet maps (see `docs/external-windows-port.md`) | **gitignored** |
| `deploy/` | containerised runtimes. `spice86-novnc/` runs the original game with its own Xvfb + x11vnc + noVNC, so a headed capture needs nothing from the host; `novnc/` does the same for the engine build. `up.sh` in each is the entry point | tracked (the staged `u2-sdl` binary is not) |
| `decompiled/` | bulk decompiler output, regenerable | **gitignored** |
| `tmp/` | per-launch game copies | **gitignored** |
| `build/` | build output | **gitignored** |

Known names in `asm/` include the 26 command handlers `cmd_A`..`cmd_Z`,
`str_out`, and OS seam labels (`video_int10`, `key_int16`, `dos_int21`,
`speaker_tone`) marking hardware/DOS call sites — the replacement surface for
the engine.

> **`captures/spice86/spice86-cfg.json` is the authoritative source for routine entry points.** This game's command handlers are reached by `jmp *%bx` through a jump table, not `call`/`ret`, so Ghidra and Reko cannot discover them statically. Blocks in the runtime CFG were actually executed — use their start addresses (segment `017D`, `file_offset = offset + 0x200`) as the seed list for both decompilers. See `captures/README.md` for the schema, address mapping, and the relocation caveat (runtime bytes differ from file bytes wherever an MZ fixup applies).
>
> **Correction, 2026-10-21: CFG *blocks* are not proof of execution.** `spice86dumpCfgBlocks.json` lists **discovered** blocks — Spice86 statically expands branch successors — so a block can appear without ever having run. It reported `MINAX IS DEAD!!`, `RING PROTECTS FROM FIELD!` and `FIELD CAUSES 1000 DAMAGE!` for a session that met none of them. The authority for "this ran" is `ExecutedInstructions` inside `spice86dumpExecutionFlow.json` (a list of `{Offset, Segment, Linear}` dicts). Use block entries as a seed list, which is what they are good for, but never as evidence that code executed. See `tools/cfg_printed.py`, which gets this right.

## 5. Evidence Discipline (READ BEFORE ASSERTING ANYTHING)

Grade every claim, and say which grade you are asserting:

| grade | meaning |
| --- | --- |
| **observed** | a byte, memory value, or CFG block read from the binary or a live session |
| **measured** | derived from observations by a tool, with the tool and inputs named |
| **inferred** | reasoned from a partial read — a listing, a name, a plausible pattern |
| **recalled** | the owner's memory of playing the original |

### An owner observation outranks an agent inference

When the owner reports what they saw or did — "I was paralyzed", "the staff is
gone", "it went back to the splash", "it was a green gem" — that is **data**, and
it outranks anything inferred here. Every such conflict so far has resolved with
the agent wrong, not the owner: the green gem (a hex miscount, not a wrong table),
the post-creation splash (the engine had shortcutted a real program restart), the
stolen staff (later proven by bytes), and mountains blocking on foot.

So on conflict: re-check your own measurement first. Do not conclude the owner
misremembered until you have disproven your own reading, and if you still
disagree, **say so explicitly and name the evidence** — never quietly overwrite a
reported observation with an inference.

A recalled detail is also worth chasing when there is no conflict yet. On
2026-10-22 the owner recalled stats wrapping to 0 above 99; the bytes agreed
(1-byte packed decimal, no ceiling guard on any add site) and the recollection
opened up a general property of the save format that no agent had thought to look
for. Treat "I remember it did X" as a pointer at a place to measure.

### Bytes outrank everyone, including the owner

Recalled design intent is a hypothesis to test, not a conclusion. Where bytes
disagree with a recollection, record the disagreement rather than picking a side
silently.

### Derive; do not eyeball

* Compute indices and offsets in code. Splitting a hex string by eye invented a
  false discrepancy that wasted a session.
* Read a whole function before describing it. `asm/2423` declares a 10-byte range
  for a routine running to about `0x2680`; the truncated read produced a
  retracted finding.
* A script's output is a hypothesis, not a measurement. A hand-written analysis
  script that slices a file from the wrong base offset produces a confident table of
  impossibilities, and it looks exactly like real evidence. When a new derivation
  contradicts a prior *measured* finding, assume the new one is wrong first and
  re-read the code that consumes the same bytes: on 2026-10-23 a script slice claimed
  a proven record layout was impossible for hours, blocked four work items and
  compiled out working code, while the engine reading those bytes correctly sat one
  file away. Push a known-true case through the same slicing before believing a
  known-false one.
* List the existing corpus before harvesting runtime data. Paralysis, theft and
  combat functions were already named on disk while a capture was run to look for
  them.

### A tool that lies is worse than no tool

Validate any new analysis tool against a **negative control** — a case where the
answer is known to be *no* — before citing its output. `cfg_printed.py` reported a
Minax fight that never happened because it read discovered blocks instead of
executed ones; the pre-fight harvest, used as a control, exposed it immediately.
`bcd_census.py`'s first draft under-counted guards by 3× because it looked for
`cmp`/`test` and the real subtract idiom is `cmc; sbb; das; cmc` — the second `cmc`
re-exposes the borrow in CF for a following `jb`, so a cmp-based detector reports a
guarded site as unguarded. Both mistakes produced a *plausible* wrong number rather
than an error, which is the dangerous shape. Prefer failing loudly to returning a
plausible default, and make unproven numbers say so in the comment next to them.

### A gate that cannot go red is worse than a tool that lies

A checker that returns a wrong answer is bad; a checker that returns a wrong
**verdict** is worse, because everything downstream treats it as fact. So:
**demonstrate a gate failing before you trust it passing.** The test is cheap —
invert one expectation, or feed one known-bad input, confirm the gate goes red,
restore. Two minutes that would have caught both of these:

* `engine/tests/run_cp5.sh` carried an unconditional `exit 0` on the branch taken
  while `combat.c` was absent, which swallowed every failure before it. The CP5
  gate printed `FAIL` and still reported `PASS` to CI — and since that branch was
  the only one ever taken, the gate had never once been capable of failing.
* The same script asserted its controls in a comment instead of running them, so
  the one property that mattered — that the spec rejects a boolean-immunity
  implementation — had no proof behind it once the throwaway models in `/var/tmp`
  disappeared. Controls now ship as fixtures
  (`engine/tests/combat_model_{faithful,boolean_wrong}.c`) and run on every
  `make test`.

Prefer a gate that fails loudly on its own misconfiguration over one that reports
success when it checked nothing. A skip is fine; a skip that can hide a failure is
not.


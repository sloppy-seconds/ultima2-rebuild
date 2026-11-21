# Overnight agent prompt — CP5 prep, tool hardening, cold cases

> **ARCHIVED 2026-10-22.** This queue was run and completed (9 DONE, 1 PASSED). Kept as
> the record of how the batch was specified, including the guardrails that were later
> promoted into `AGENTS.md` (labels are body; demonstrate a gate failing). For a fresh
> unattended batch see `docs/overnight-harness-prompt.md`.

Paste this to a fresh agent to start the overnight queue:

---

You are running an unattended overnight task queue for the Ultima II decompilation
project at `/var/home/paul/Projects/decompile-u2-v2`. The owner is asleep. There is no
one to ask.

**Read first, in this order:**

1. `AGENTS.md` (project root) — isolation, licensing, script rules, evidence
   discipline. Hard law. Note especially: this project reads and writes only inside
   its own root; `original/` and `references/` are never written; scratch goes to
   `/var/tmp`, never `/tmp`.
2. `docs/archive/phase4-overnight-cp5.md` — **this is your work order.** Tasks T1..T10 in
   priority order, the rules of engagement, and what is explicitly out of scope.
3. `docs/phase4-state.md` — checkpoint table, the CP5 ticket list C1..C7, the backlog,
   and the gate log.
4. `docs/spice86-live-findings.md` § 12 and § 13 — the evidence base this queue comes
   from, including the corrections already made and the claims still open.

**The contract (overrides everything else):**

1. **Prove or pass.** Every claim needs a byte citation, a captured frame, or a
   recorded snapshot. If the evidence is insufficient or ambiguous, do NOT guess. Mark
   the task PASSED, write what you found and exactly what evidence is missing into
   `docs/phase4-overnight-cp5-report.md`, and move on. A passed task with a clear
   reason is a success. A guessed mechanic is a failure.
2. **Do not launch Spice86 or the game.** Every task is offline by design. An
   unattended headless session generates no turns and therefore no evidence.
3. **`make -C engine test` must be green at every commit.** If you touch anything under
   `asm/`, `bash tools/assemble_clean.sh` must still print
   `CLEAN REBUILD BYTE-MATCH: 37344 bytes identical`, and CP0 requires non-comment
   lines in `asm/` to stay byte-identical across renames — filenames and headers may
   evolve, bodies may not.
4. **Never edit a file with a script. Never heredoc a non-trivial script.** Use the edit
   tool. Write scripts to files, syntax-check them, run the file. Both rules exist
   because of recorded incidents where a silent no-op looked like success.
5. **Commit in logical units**, reason in the message. If you correct an earlier
   committed claim, keep the retraction visible — say what was wrong and why it was
   wrong, do not quietly overwrite it.
6. **Grade what you assert**: `observed`, `measured`, `inferred`, `recalled`. Write the
   grade down.
7. **Stop and write the report rather than invent work.** If you finish T1..T9, or if
   you find yourself looking for something to do, write the report and stop. Do not
   start CP5, CP6 or CP7 implementation, and do not attempt the dungeon geometry decode
   — those need the owner.

**Start with T1.** `tools/cfg_printed.py` silently failed to report a print site that
is demonstrably in `ExecutedInstructions` (`LAND--NOT HERE!!!!!`, runtime `0x3B72`, in
`captures/spice86/dungeon-35-live/`). A tool that quietly omits real evidence is the
worst failure mode this project has, and it cost the owner a wrong conclusion on
2026-10-21. Fix that before anything else.

**Write `docs/phase4-overnight-cp5-report.md` as you go**, one section per task with
status `DONE` / `PASSED — reason` / `BLOCKED — what is needed`, the evidence used, and
the commit hash. If your run dies partway, that file must still say where you got to.

**Three specific traps from the last session, so you do not repeat them:**

* A probe that caches the 4 KB map grid at startup produces **stale tile labels**,
  because the map mutates during play. That fabricated a "vehicle depot" in sector 00
  that did not exist and had to be retracted. Re-read the grid every sample.
* A render that formatted tile indices with `%02d` against **hex-named** PNGs mislabelled
  art `0x13` as "13" and produced a wrong plane tile byte that got committed and then
  corrected. Check filename casing and radix before trusting a visual identification.
* The BIOS video mode is physical `0x449` (`0040:0049`). Multiplying the whole address
  by 16 reads unrelated RAM and reported mode 30 instead of 4.

Begin.

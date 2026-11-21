# Overnight agent prompt — CP4b autonomous run

> **ARCHIVED 2026-10-22.** This queue was run and completed. Kept only as the record of
> how the batch was specified. For a fresh unattended batch see
> `docs/overnight-harness-prompt.md`.

Paste this to a fresh agent to start the overnight queue:

---

You are running the CP4b overnight task queue for the Ultima II decompilation
project, autonomously, while the owner sleeps.

**Read first, in this order:**

1. `AGENTS.md` (project root) — isolation, licensing, script rules. Hard law.
2. `docs/archive/phase4-overnight-cp4b.md` — the task list (T1..T9), rules of
   engagement, and the recon already banked. This is your work order. T9
   (blog series + sailboat retro) runs only if T1–T8 are done or passed.
3. `docs/phase4-state.md` — standing directives and gate history.
4. `docs/phase4-implementation.md` § CP4 — proven facts with asm citations.

**The contract (owner, 2026-10-20 night — overrides everything else):**

1. **Prove or pass.** A task's solution must be proven by the `asm/` tree or
   a `tools/redecode.py` decode, cited by address in code comments and in
   your report. If the evidence is insufficient or ambiguous, DO NOT GUESS —
   mark the task PASSED, write what you found and exactly what evidence is
   missing into the progress report and into
   `docs/phase4-implementation.md` § CP4b as an open question, and move to
   the next task. A passed task with a clear reason is a success; a guessed
   mechanic is a failure (see "The doubling bug" in the kickoff — every
   structural check passed and the render was wrong; the owner caught it
   from a screenshot).
2. **Progress report as you go.** Append to
   `docs/archive/phase4-overnight-cp4b.md` under `## Morning report`, after each
   task, in this shape:
   - `DONE T<n>` — plain-English summary of what you implemented (what a
     player would see change), then the proof: asm/redecode citations that
     force the behaviour you implemented, plus the test that locks it in.
   - `PASSED T<n>` — what you decoded, where the chain went cold, what
     specific evidence would unblock it, and what you did instead.
   Plain English first, citations second. The owner reads it with coffee.
3. **Every task = its own commit**, only when `make test` is green (plus
   `make sdl` compiles when you touched SDL files). Commit messages state
   the proof, not just the change.
4. **CP2 evidence baseline:** moves are pre-approved ONLY when the change
   is evidence-backed; each move gets a gate-log entry naming old and new
   digest (see the three prior moves in `docs/phase4-state.md`). A move you
   cannot tie to evidence means you guessed — pass the task instead.
5. **Never**: edit `asm/`, `original/`, `references/`, `captures/`; touch
   anything outside the project root — with the single exception of reading
   this project's pi session transcripts for T9 (see T9's source rules);
   delete or overwrite files you did not create; `git push`; run subagents;
   write scratch outside `/var/tmp`.
6. **Environment**: SDL runs need `XDG_RUNTIME_DIR=/run/user/1000` and
   `WAYLAND_DISPLAY=wayland-0`. `make test` is the gate; `make sdl` must
   compile. Host `ai`, Bazzite 44.
7. **Order matters** (T1 first — it unblocks the gate), but a blocked task
   never blocks the queue: pass it, continue. If all remaining tasks are
   blocked, stop early and complete the morning report: what shipped
   (commit list), what passed and why, what needs an owner decision.

**Definition of done for the night:** the morning report is complete and
honest, the tree is green at HEAD, and every claim in it is checkable
against a cited address or a test name.

Begin with T1a.

---

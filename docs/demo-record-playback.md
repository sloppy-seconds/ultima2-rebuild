# Plan — demo record and playback

## 0. Status — executed 2026-11-07

**Built, gated, and green.** `make test-demo` (`engine/tests/run_demo.sh`) records a
fixture, plays it back, and runs the control that makes each check red. The
implementation is `engine/src/core/demo.{h,c}`, `engine/src/platform/input_demo.c`,
the seam tap in `engine/src/core/seam.c`, the wiring in `engine/src/main.c`, the tool
`engine/tools/demo.py` and the availability gate `tools/demo_key_gate.py` (in
`tools/check_all.sh`). `demos/` is gitignored (D9).

All fifteen work items shipped, with nothing skipped on a machine that has built the
SDL artifact: `deploy/linuxbuild/build.sh` produces `engine/u2-sdl-static` (static
SDL3, in podman) and the gate's W8 leg replays the same demo through the real SDL
event loop with `SDL_VIDEODRIVER=dummy` — SDL renderer, host clock, the 16 ms seam
waits, no `--headless` — and every digest matches. It also measures W15's claim on the
same binary: SDL playback paces by default where headless does not. The artifact is
gitignored, so on a machine that has not built it the leg skips out loud and names the
command; what always runs is the weaker version of the same claim, the same demo
replayed under the **terminal** renderer and the null one, both headless, digests
compared (`--headless --renderer=terminal` is now honoured rather than ignored, which
is what made that possible). **Hazard 9's caveat is therefore closed**: it said "until
W8 passes, do not claim SDL playback is verified — say headless verified, SDL
unproven", and W8 has now passed against a real SDL3 build, so the claim is made. What
it proves is the narrow thing worth proving: a second renderer, a real event loop and a
real clock between the same records cannot move the DS state. W13 is done as
overworld → site → overworld; see deviation 5 for why creation is refused.

**Sixteen deviations from this plan, each one forced by a measurement.** They are
listed here rather than edited silently into the body, because the body is the owner's
spec and a spec that gets quietly rewritten is worse than a spec with a defect list.
The first fifteen came from building it; 16 to 19 came from the owner playing it on a
real install the day after it shipped, and each of those four was invisible to every
gate that had passed.

1. **The digest scope is not `u2_state_digest_json()` (D6).** The frozen CP2 scope
   embeds the engine version string and the platform backend names (`renderer`,
   `input`, `clock`). Using it would make D13 impossible by construction — every
   release bump would invalidate every demo — and would make W8 unprovable rather than
   unproven, because the null and SDL renderers differ in the digest by definition.
   The demo scope is behavioural and frozen at 1: the DS mirror with D3's excluded
   offsets blanked, plus the roll counter. Changing it is still a baseline move.
   What this costs, stated: the digest is a **state** proof and not a picture proof, so
   hazard 2's missing restamp is not caught by a digest (the creature table it is
   derived from is in there; the stamped grid is not). W7's digest check must not be
   cited as a frame check.
2. **Verification pairs record N with the state at the tap that delivers key N, not
   the tap that delivers N+1 (D12).** The plan's two D12 sentences are mutually
   inconsistent as written: a record holds the state *before* its own key, so at the
   tap for N+1 the live state is the state *after* N and the two differ by exactly one
   turn. Measured: the first implementation reported record 1 diverging with the live
   digest equal to record 2's. Verified against its own record, the comparison is the
   same instant on both sides, and the divergence lands on the key that caused it
   before that key is applied — finer than the plan asked for.
3. **The closing digest is compared at the poll after the last record, not at process
   exit (D12's first consequence).** The recorder writes `@end` at a poll, one draw
   after the last recorded key; comparing at exit compared the middle of a form to the
   end of a run. If the flow never reaches that point the run fails with
   `DEMO INCOMPLETE` and a count, rather than passing a check that never happened.
4. **`u2_demo_mark_auto_pass` is called from the overworld loop's idle arm, not from
   `u2_overworld_pass` (§ 6's call-site table).** `cmd_P` reaches `u2_overworld_pass`
   too, so a pass taken from a recorded key would write an extra `kind=auto` record and
   playback would synthesise a second PASS on top of the one `cmd_P` already performed.
   The idle arm is the only site that corresponds to the original's asm/08FF.
5. **Recording is refused in two start modes: `title` and `creation`.** The plan lists
   five modes; three are supported. The title flow reaches its menu by consuming keys
   of its own, so a checkpoint there cannot be resumed without replaying input the
   recording never made. Creation took a measurement to see: the form's cursor — which
   field is highlighted, which dropdown is open — lives in a local struct and not in the
   DS mirror, so a checkpoint taken mid-form names a state playback cannot resume and
   the digests diverge a few keys in. That is D12's quiescence rule with a wider reach
   than `ow->pending_cmd`: the engine keeps scratch outside the mirror in more places
   than one. Fixing it means moving the form's state into DS or into a second
   checkpoint block, which is a change to the save model and not a detail of the demo.
   W13's acceptance is therefore overworld → site → overworld, which is the substantive
   mode-crossing claim and is gated.
6. **`exclude` includes `0x75`.** § 4's example line reads `exclude 74 550 551 552 553`
   and omits 0x75, which is the high half of the same `uint16` speed field as 0x74. D3's
   table says DS:74..75, so the table is the decision and the example was a typo.
7. **`@end` carries two digests and `@checkpoint` carries no `turn=`.** `state=` is the
   closing state digest D12 needs; `sha=` is the framing digest hazard 4 describes, and
   it is now actually checked — an edited file is refused as corrupt rather than played
   as divergent, which is the distinction hazard 4 exists to keep. `turn=` was dropped
   from `@checkpoint` for the reason D12 already gives for renaming `@turn` to `@rec`:
   the demo has no turn concept.
8. **`--playback-pace` was added.** Not the `--playback-speed N` knob the plan declines
   to specify: it is a boolean, and it exists because W15's control needs to turn pacing
   on in a headless run to prove pacing is applied and not a no-op. It also wires the
   host clock in headless, because the script clock's sleep is instant and a pacing flag
   that silently does nothing is the failure mode.
9. **`--auto-pass-polls N` is now honoured in a headless run.** The headless exemption
   was for the *default* threshold, and a gate cannot record a `kind=auto` turn in the
   only mode a gate can run in without it. No CP2 baseline script passes the flag, so
   the baseline is untouched.
10. **`--headless --renderer=terminal` is honoured**, so renderer independence is
    measurable on a machine with no SDL build at all rather than only on one with it.
11. **`save-sha` verifies the checkpoint's own record region, not the viewer's save.**
    Refusing when the person watching has a different character loaded would make demos
    unshareable, which is the whole request, and playback overwrites the record region
    from the checkpoint anyway. So `save-sha` does the job it can honestly do: it turns a
    corrupted checkpoint into a broken file instead of a divergent demo.
12. **`overlay` lines are the sector files the world was built from** — `map_base`,
    `mon` and the `tlkx` read, digested once at load and named by the filename the loader
    actually resolved. The plan's "files the recording touches" cannot be known in
    advance; what is checkable is the bytes the run started from, and that is what
    refuses. A guard was needed here that the first version lacked: an absent `tlkx`
    leaves its prefix NULL and the fallback spelling produced `mapx00` for the dialogue
    buffer, so a sector with no dialogue reported that its own map file had changed.
13. **W4's control as written is not implementable.** "Truncate the span to `0x280` and
    confirm a speed-pinned run diverges" cannot diverge, because D3 excludes the speed
    bytes from comparison by design. The control that does prove the long span is doing
    work: a `0x280` checkpoint is refused by name (`checkpoint is 640 bytes`), so a
    partial mirror cannot be half-applied.
14. **D5's `--speed-pin` names the pinned values rather than suppressing a calibration.**
    The engine performs no startup calibration today — `game/speed.c` is a verified model
    with no callers — so there is nothing to suppress. The flag loads the recorded bytes,
    prints what is in force, and is the one place a future calibration has to stand down
    at. Default on.
15. **`digest-version` mismatch is a downgrade with its own exit code (6), as D13
    rules**, and `engine/tools/demo.py corpus` is the local regression reader W14 asks
    for. It refuses to run on an absent or empty directory rather than reporting success,
    because a corpus of copyrighted playthroughs cannot be committed and a gate that
    skips silently when it is absent is the defect AGENTS.md § 3 warns about.
16. **The `overlay` refusal only applies to content-layer bytes, and a slot-sourced
    digest is a note.** § 4's correction says refuse on the bytes that reach the world,
    and that is right; what the plan could not see is that the recorder often reads those
    bytes from *its own save slot*, and § 7.4 deliberately hides that slot from
    playback. Two honest runs, two layers, a false refusal — and it fired on the owner's
    first real recording, on a real install, with a digest no later run could ever
    reproduce. So the header now distinguishes `overlay` (content, enforced) from
    `overlay-slot` (the recorder's own save, noted once and never enforced), because no
    viewer can be expected to hold the recorder's save.
    *(Superseded in direction by § 13, which removes the two layers rather than grading them.)*
17. **`@end` records how the recording ended, and that decides where its state digest
    is compared.** A stop toggle leaves the run running, so the digest is a quiescent
    state and playback compares it at the matching poll. A quit ends the run, so the
    digest is an exit state and playback must compare it at exit. The first version
    assumed the toggle in both cases, which meant the owner's own headline requirement —
    *"quitting the game while recording should stop the recording gracefully"* — produced
    a file that was perfectly well formed and could never verify. `stop=toggle` and
    `stop=exit` now say which, and a run that reaches neither reports `DEMO INCOMPLETE`
    with a count rather than passing a check that never ran.
18. **A Ctrl-R on the way into play defers; it does not refuse.** The route from the menu
    to the world passes title-flow waits, so a player who presses the key while loading a
    character is standing at a title boundary while genuinely meaning to record a
    session. Refusing there made the ordinary flow produce nothing at all. The request
    now stays pending, says so once, is cancelled by a second press, and lands at the
    first boundary playback can resume into. Creation still refuses, for the reason in
    deviation 5, and says that reason instead of pointing at the title menu.
19. **`--playback-ignore-content` exists.** Same shape as `--salvage`: refuse by default,
    let a human override it explicitly and be told what it cost. Without it a demo whose
    content cannot be reproduced is simply unplayable, which is the opposite of the
    request that started this — and a recording made before deviation 16 was found is
    exactly that case.
20. **Deviation 16 is a workaround, not the design, and § 13 replaces it.** The two-tier
    `overlay` / `overlay-slot` split makes a demo recorded after any prior play
    best-effort, which is where the owner's own 165-record demo ended up. § 13 amends the
    format to carry a **terrain delta against content** instead of a digest of whatever the
    run loaded, which retires the split, retires `--demo-embed-overlays`, and finally makes
    the real-install case testable as gate leg W16. **§ 13 is design only and unbuilt**; the
    five assertions that prove its load-bearing property are shipped and gated. **The build order is
    written out below the amendment: Phase 1 terrain restore, Phase 2 `map_base`, Phase 3 the delta.**

Everything below is the plan as ruled on 2026-11-05, unchanged, with the D-numbers the
implementation cites.

---

**Status: planned, not built.** Designed 2026-11-05, and the nine open questions were ruled by the owner
the same day — those rulings are D11 to D13 and the settled list in § 11. The one that changed the design
is item 5 (**recording must survive mode transitions**), which moved the verification point off the
overworld turn and onto the input seam: see D12. No code exists for this subsystem: `demo.c`,
`input_demo.c`, `--playback`, `--recording` and the `demos/` directory are all proposals in this file.
What does exist and is load-bearing is listed in § 5; everything else here is a work item in § 9.

Owner request, kept verbatim because it is the spec:

> I would do CTRL R to start recording it, and CTRL R to stop recording it. quitting the game while
> recording should stop the recording gracefully. to playback the recording you'd run the engine binary
> with `--playback ./<filename>. I think probably we'd create a ./demo directory, and each demo
> recording would get its own file or directory of files (or be compiled into a .zip from a temp
> location?) as recordings/demos should be easily shareable.

Two follow-up rulings from the same conversation, both adopted here:

* the recording unit should be **the turn the player took**, not the keystroke. Ruled on 2026-11-05 and the
  answer is subtler than the question: the unit is **the key the game consumed**, because a turn spans
  several keys and a mode without a turn counter has no turns to count (§ 3 D1 and D12).
* the idle auto-PASS may be suspended for record and playback; the starting state must be pinned somehow
  (owner asked; § 3 D3 to D5 answer it).

---

## 1. What this is, in one paragraph

An interactive record/playback facility for the **engine**, not for the original. `Ctrl-R` toggles
recording at a quiescent point; from then on every key the game consumes appends one small line to the
demo holding that key and a digest of the state it was delivered into. Quitting, crashing or `Ctrl-R` all
end the file cleanly, and an interrupted file is detectable rather than silently wrong. `--playback FILE`
restores the recorded starting state, replays the keys through the same input seam a player's keys arrive
on, and **verifies every record's digest against the recording**, exiting non-zero at the first divergence
with the record number and the field difference. Demos are one plain-text file each, small enough to
share, and the subsystem is the engine-side half of `docs/plan-cross-game-replay.md`.

## 2. Why this is not a hack

Three properties separate this from "log the keys and hope":

1. **The verification is per record and mandatory.** A demo that plays back wrong is a failed playback, not
   a demo that plays out differently. `replay.c` already proves the mechanism works (CP2's committed
   evidence reproduces bit for bit); this extends the same idea to a file a player owns.
2. **The replay path is the real input path.** Recorded keys are re-injected through `u2_input`, past the
   same seam, in the same alphabet game logic sees. There is no parallel "demo code path" that could
   drift from play.
3. **The state is pinned by the byte, not by a seed.** `--rng-state` and `u2_state_load_ds` already exist
   and are gated. A demo resumes from what the recording actually had, so the fact that the original's
   idle loop advances the RNG on the BIOS timer (`docs/plan-cross-game-replay.md` § 6b, § 6c) stops
   mattering.

---

## 3. Decisions that have to be right before any code

Each decision is stated with the alternative it rejects and the evidence that settles it. These are the
things that are expensive to change later.

### D1 — record the keys the game **consumed**, not raw keystrokes, and not resolved actions

Rejected: raw keystroke logging at the SDL/terminal boundary. Reason: the original and the engine both
consume more keys than they act on, and fewer keys than the player presses. The measured case is on
record — `tools/u2_record.py`'s first acceptance run found that the `Return` in its own load sequence is
**never consumed** by the game (`docs/plan-cross-game-replay.md` § 7a). A raw-keystroke demo replays that
`Return` through the input layer and re-derives a different stimulus.

Rejected: recording the resolved action (`WALK RIGHT`, `ATTACK --`). Reason: `u2_cmd_dispatch` takes only
a letter. Every argument is gathered *inside* the handler through further key reads, so an action-level
record needs a hook in each of the 26 handlers (`tools/cmd_handlers.py` is the roster). That is 26 places
to forget, in a project whose recorded failure mode is exactly that.

**Adopted: tap the seam.** `seam_key_poll` / `seam_key_wait` are the single point where a key becomes
game logic's stimulus (`asm/4F4D` is the original's one seam, and one seam is the documented reason both
engine entries must answer in the same alphabet). Journal what passes there, after `key_uppercase_fold`,
after the Ctrl-S swallow. That sequence is downstream of every key that was never consumed and upstream
of every handler, so it needs no per-handler hooks and cannot record a key the game ignored.

Consequence worth stating plainly: this is *stimulus*-exact, not *action*-exact. The demo reproduces the
turn because the same stimulus meets the same state through the same code, and the per-turn digest (D6)
proves it did. If a future engine change makes the same stimulus mean a different action, playback fails
loudly at that turn instead of silently recording something else.

### D2 — the toggle takes effect at a **quiescent** point, never mid-command

`Ctrl-R` arrives as a key, which means it can arrive **mid-turn** — half-way through a `cmd_Y` freeform
input, or between a direction key and its confirmation. A recorder that starts there would write a
checkpoint from the middle of a partially resolved command, and playback would start from a state the
game never occupies at a boundary.

So the toggle sets a **pending** flag and takes effect at the next **quiescent** point: the moment a mode
is about to wait for its next key, with no command half-typed.

> **D12 supersedes the mechanism below.** This section originally defined the boundary as `ow->turns`,
incremented in exactly one place (`creature_turn()`, `overworld.c:107`, `one resolved turn; --debug cli
keys off this`). That works for the overworld and fails for creation, a dungeon and space, so the recording
and verification unit moved to the input seam. What survives here is only the toggle's timing:

* a demo never starts or stops with a command half-typed, so `--playback` never mounts a partially resolved
  command;
* `Ctrl-R` is idempotent under a double tap and cannot produce a half-written record.

The reason the start point has to be quiescent is concrete: `ow->pending_cmd` (`overworld.h:53`) is a
half-typed command held in an engine struct, not in the DS mirror, so a checkpoint taken mid-command would
have to restore state the format does not carry.

### D3 — the checkpoint is the **whole DS mirror**, and comparison excludes the clock-derived bytes

`sizeof(u2_state) == 0x650` (1616 bytes), pinned by `_Static_assert` in `state.h`, with an offset assert
per field. Taking the whole span is the answer to "how do we know what exact state the game is in": the
struct is the DS working segment, so a whole-span snapshot cannot forget a field. A curated field list
can, and this project has been bitten by exactly that (`docs/plan-cross-game-replay.md` § 9).

There is one exception class, and it is measured rather than guessed:

| bytes | why it must not be compared | grade |
| --- | --- | --- |
| `DS:74..75` speed | calibrated at every startup by counting busy passes in a 20-tick window (`asm/06AC`); 16 of 16 turns differed across two identical boots | observed, `plan-cross-game-replay.md` § 6a |
| `DS:550..553` `tick_low`/`tick_high` | the BIOS tick count saved by `asm/06B0`; a free-running clock by definition | observed (`state.h` offsets) |

Both live inside the span, so the snapshot carries them and the **comparison** excludes them. Excluding
them from the snapshot instead would lose the ability to restore a mid-session speed value, which D5
needs. The exclusion list is written into each demo file with its reason, so a future reader cannot
mistake it for an oversight.

Note the precedent: `--dump-ds` writes `0x280` bytes, which covers player, creatures, wander vectors,
paralysis counters and the RNG register but stops before the speed and tick fields. That span is right
for a *differ* and wrong for a *checkpoint*, and the demo needs the checkpoint.

### D4 — auto-PASS is **materialised** while recording and **disabled** while playing back

Two different answers for the two directions, and the asymmetry is the point.

* **Playback disables it, unconditionally.** The playback loop polls for keys between turns; each poll
  adds to `idle_polls`, and at `U2_AUTO_PASS_POLLS` the engine performs a PASS that was never in the
  recording. Playback would then inject turns of its own.
* **Recording does not suspend it.** The original genuinely drains food and steps monsters while the
  player thinks (`asm/08FF`; `state.h:343` calls the idle auto-PASS out by name). Suspending it would make
  the demo a recording of a game that behaves differently from the one being demoed. Instead every
  auto-PASS is written as a turn of `kind=auto` with an empty key list, and playback reproduces it by
  synthesising the PASS at that point.

This is also what decouples demos from `U2_AUTO_PASS_POLLS = 256`, which `overworld.c:708` documents as a
placeholder — *"mechanism proven, threshold not... Do not cite it as proven."* With auto-PASS turns in
the file, changing the threshold later changes how a session is recorded, not whether an existing demo
replays.

### D5 — playback **pins** the clock-derived state instead of recalibrating it

`DS:74` is excluded from comparison (D3), but "not compared" is not "not used". It feeds `DS:580[]`,
animation delays and key-wait timeouts via `asm/0711_speed_delay_lookup`. If a playback session
recalibrates to a different value and anything reads it on a path that can change state, the demo
diverges for a reason that looks like a deep bug.

So playback loads the recorded `DS:74/75` and **suppresses the startup calibration** for the session.
`tick_low`/`tick_high` are loaded too. This is a small, honest, one-flag change (`--speed-pin`), and it
turns an unexamined dependency into a pinned one.

### D6 — every record carries a digest, and a demo that plays back wrong is a **failure**

Per-record `sha256` over the frozen `u2_state_digest_json()` scope. Not `u2_state_json()`: that separation
exists because coupling the two invalidated every recorded CP2 replay when a diagnostic field was added
(`replay.c` § 5 in the replay plan). The frozen scope is at version 1 and changing it is a baseline move.

Playback hashes each record as it resolves and compares. On mismatch: report the record index, write the DS
diff (via the differ in § 5) to stderr, and exit non-zero. `--playback-continue` is for diagnosis only and
is documented as such.

### D7 — one plain-text file per demo, digests by default, snapshots on request

Rejected: a directory of files per demo. Rejected: always a `.zip`. Both solve a sparseness problem this
format does not have.

Text wins because it is diffable, greppable, truncation-detectable, and needs no archiver to inspect. It
also matches the existing house style: `--dump-ds` writes hex, `--replay-report` writes JSON, and
`docs/plan-cross-game-replay.md` § 8 already rules that a shareable demo is *"a key script plus a
checkpoint"* — text, tiny, diffable — not video.

**Size discipline:** the default demo stores the full 1616-byte checkpoint once and a 64-char digest per
record. A 500-turn demo is roughly 90 KB of text (D12: one digest per delivered key, not per turn). Full
per-record DS snapshots are available behind `--demo-snapshots` for local debugging, where the diagnosis
matters more than the bytes.

**Owner ruling 2026-11-05: plain text for now.** Zip stays deferred, and the condition that would revive it
is named rather than left vague: a demo that must carry its own sector overlays to be self-contained (§ 4,
`--demo-embed-overlays`) stops being a text file, and that is the day to reconsider the container.

> **Closed 2026-11-07 by § 13.** A demo never has to carry its overlays, because it carries a delta
> against content instead. `--demo-embed-overlays` is retired as an idea and the container question
> does not arise.

### D8 — a file with no epilogue is **incomplete** and refused unless asked

Recording appends and flushes per record, so a hard kill loses at most the open record. The reader treats a
missing `@end` line, or a turn-count mismatch, as an incomplete recording and refuses to play it by
default. `--salvage` plays the turns that are present and says how many were dropped.

Without this, `Ctrl-C` produces a file that looks fine and silently stops early, which is indistinguishable
from a demo that played back correctly up to the point it stopped. That is the same shape as the gates
this project has been burned by (§ 5, "a gate that cannot go red").

### D9 — demos are user data: `demos/` is gitignored, and the gate builds its own fixture

A demo encodes a playthrough of copyrighted content, exactly as a save file does, and `engine/saves/` is
already gitignored for that reason (`AGENTS.md` § 0, `.gitignore`: *"Player saves... user data, never a
repo artefact"*). So `demos/` joins it.

The consequence for the gate is deliberate: `engine/tests/run_demo.sh` **records its own fixture at test
time** from a short scripted headless session, then plays it back against the digests it just produced. No
committed game data, no external dependency, and it goes red when either half breaks.

### D10 — `Ctrl-R` is a seam-level key, swallowed before game logic, like `Ctrl-S`

`Ctrl-S` is already handled this way: `U2_KEY_SCREENSHOT 0x13` in `seam.c:170`, intercepted by
`take_screenshot_key()` and returned to the caller as "no key". `Ctrl-R` follows the identical pattern with
`0x12`.

Availability is measured, not assumed: the only `cmp` against a control byte in the corpus is
`asm/4F76`'s `cmp $0x13,%al`, and **no `cmp $0x12` exists anywhere in `asm/`**. That is the same
reasoning that let `--dev` claim `Ctrl-Q` (`0x11`), and § 9 makes it a gate rather than a claim so a later
`asm/` correction cannot quietly take the key away.

### D11 — playback is **watchable**; pacing is presentation and must not reach state

Owner ruling 2026-11-05: watchable, and the speed can be tuned later.

So playback paces records at roughly real time by default, and the pacing lives in the clock backend the
mode already selects rather than in the demo. Headless playback is the exception and stays unthrottled,
because there is nothing to watch and the gate in W7 has to run fast enough to be run.

The engineering constraint behind the ruling is that **pacing must not be able to change state**. It
cannot, by construction, because records are driven from the file and not from the clock — but "cannot by
construction" is the kind of claim this project prefers to measure, so W8 runs the same demo headless and
under SDL and compares every digest. If those agree, presentation is provably inert; if they disagree,
that is a finding about the engine, not about pacing.

A `--playback-speed N` knob is deliberately not specified here. Adding one before the first demo exists
would be tuning a number nobody has watched.

### D12 — verification happens at the **input seam**, so mode transitions need no turn hook

This is the design change forced by the owner ruling that recording must survive switching states
(character creation to play, overworld to dungeon, dungeon back out).

D2's original shape hooked the overworld's turn counter. That fails on the ruling for a concrete reason:
the counter is `ow->turns`, and there is exactly one of it, in the overworld. Character creation, a
dungeon turn, and an in-space command turn do not increment it. Key reads are spread across seven
modules — `commands.c` (14 sites), `title.c` (9), `overworld.c` (3), `commerce.c` (3), `game.c` (1),
`creation.c` (1) — and the 14 in `commands.c` are *inside* a turn, not turns. One boundary hook per module
would be seven places to forget, which is the same defect D1 rejected for actions.

**Instead: verify at the seam, which every mode already passes through.**

* **Recording** writes a record immediately *before* delivering key N to game logic, holding the digest of
  the state at that instant. Interleaved `kind=auto` records mark the idle auto-PASS, which is the one
  event that is not a key and therefore the one thing the seam cannot see.
* **Playback** delivers key N and, before delivering key N+1, checks the live digest against N's recorded
  digest.

Why this is strictly better than a turn hook:

* it works in creation, title, overworld, town, dungeon and space with **no per-mode code**, because all
  of them ask the seam for their next key;
* it is finer-grained than a turn: a three-key turn is verified three times, so an intra-turn divergence
  is localised to the key that caused it;
* the state at a seam boundary is exactly the state playback will be in when it resumes there, which
  removes the "is a turn boundary a legal resume point" question entirely.

The cost is a larger file — one digest per delivered key rather than per turn, so roughly 2x. A 500-turn
demo is about 90 KB of text. That is an acceptable trade for removing seven hook sites and a
mode-specific boundary definition, and D7's `--demo-snapshots` stays the debugging lever.

Two consequences that need stating because they are where this will be gotten wrong:

1. **The last key is unverified.** There is no key N+1 to trigger the final comparison, so
   `u2_demo_finish` compares a closing digest written at `@end`. W1 and W9 both cover it.
2. **Recording start still needs a quiescent point.** A checkpoint taken mid-command would have to restore
   engine-side scratch that is not in the DS mirror — `ow->pending_cmd` is the concrete example
   (`overworld.h:53`: a half-typed command, `0` when nothing is pending). So D2 survives in narrowed form:
   `Ctrl-R` is only honoured when no command is in progress; pressed mid-command it sets a pending flag and
   applies at the next top-level wait. Playback therefore always resumes at a point where `pending_cmd` was
   `0`, and that field never enters the format.

That narrowing is why the header records the **start mode** (§ 4): a demo started during character creation
resumes in creation, and a demo started in the overworld resumes in the overworld.

### D13 — cross-version playback is a **supported use case**, not a warning

Owner ruling 2026-11-05: playing back demos recorded on older engine builds is wanted, for checking that a
bug was actually fixed and for catching regressions.

That splits three questions the format was conflating:

| question | answer |
| --- | --- |
| can an old demo be **replayed** on a new engine? | **yes.** The stimulus is a checkpoint plus keys, and neither depends on the engine version. |
| can an old demo be **verified** on a new engine? | **only while `digest_version` matches.** The digest scope is frozen per version (`replay.c`, `dump_state.c`); comparing a scope-1 digest to a scope-2 digest is comparing two different functions. |
| is a divergence on an old demo a **failure**? | **no, it is the data.** A divergence on a demo recorded by the *same* build is a bug; on a demo recorded by an older build it is the answer to "did this change?" |

So `engine` is informational and printed, `digest-version` mismatch downgrades the run to **playback
unverified** with a loud line and a distinct exit code, and the "divergence is a failure" rule (D6) is
scoped to demos recorded in the same run. That is what keeps W7's gate honest: D9 already makes it record
its own fixture rather than committing one.

This also makes a **local regression corpus** possible: keep `demos/` (gitignored) as a set of historical
scenarios and replay them against a new build with `demo.py corpus DIR`. It reports per file: matches,
first divergence, or unverifiable. It is a tool for a person, not a CI gate, because a corpus of
copyrighted playthroughs cannot be committed and a gate that silently skips when the corpus is absent is
the defect `AGENTS.md` § 3 warns about.

---

## 4. The file format

One UTF-8 text file, `.demo` extension. Every line is one of: a directive (`@` prefix), a `key=value`
header field, a payload line (two leading spaces), or a comment (`#`).

```
udemo 1
engine 0.5.0-dev
content extracted
sector 03
span 1616
exclude 74 550 551 552 553
save-sha 1f3c...64 hex
overlay mapx03 d0b1...64 hex
overlay monx03 8a22...64 hex
art ega
theme fmtowns
enhanced 1

@checkpoint turn=0 keys=0 rolls=0 sector=03
  0003060000000000000000...          # 1616 bytes, 32 per line, hex
@checkpoint-sha 9e77...64 hex

@rec 1 kind=key keys=0d:1c,52:13 rolls=3 sha=ab12...64 hex
@rec 2 kind=auto keys= rolls=1 sha=cd34...64 hex
@rec 3 kind=key keys=1b:01 rolls=0 sha=ef56...64 hex

@end turns 3 sha=0011...64 hex of everything above
```

Grammar, field by field:

| element | meaning |
| --- | --- |
| `udemo N` | format version, first line, mandatory. Unrecognised version refuses. |
| `engine`, `digest-version` | what produced it and which frozen digest scope the `sha` values use. `engine` is informational (D13); `digest-version` decides whether verification is possible at all. |
| `content` | the art source (`cleanroom` or `extracted`). **Informational, never a refusal** — see the correction below. |
| `mode` | the state the recording started in: `creation`, `overworld`, `site`, `dungeon` or `space`. Playback resumes into this mode (D12). |
| `sector` | the sector the recording started in, as two decimal digits. Cross-checked against the checkpoint on load, reusing the existing refusal in `overworld_resume`. |
| `span` | the DS span captured. Present so a future span change is visible rather than silently short. |
| `exclude` | DS offsets excluded from comparison, with the reason in § 3 D3. |
| `save-sha` | digest of the PLAYER record at the checkpoint. |
| `overlay` | one line per sector overlay file the recording touches, with its digest. Playback refuses on mismatch and names the file. |
| `art`, `theme`, `enhanced` | display settings, so a demo looks like the session it recorded. These may not affect state (§ 7), and § 9 W8 is the gate that proves it. |
| `@checkpoint` | the starting state. `keys`/`rolls` are counters at that instant; `sector` repeated for the cross-check. |
| `@rec N` | one record: one delivered input, in order, `N` strictly increasing. `kind` is `key` or `auto`. `keys` is a comma-separated list of `ch:scan` hex pairs in delivery order; empty for `auto`. `rolls` is the RNG roll count at that point, a cheap second signal alongside the digest. Named `@rec` rather than `@turn` deliberately — a turn spans several keys, and these are keys (D12). |
| `sha` | the frozen digests (D6). |
| `@end` | epilogue. Its presence is what makes the file complete (D8). The trailing digest covers everything above it. |

Key pairs are `ch:scan`, always both, so there is no conditional encoding: `0d:1c` is Enter, `52:13` is
`R`, `00:48` is the up arrow. `--demo-snapshots` adds an optional `@snap N` block with 1616 hex bytes
after the matching `@rec` line; the reader ignores it.

Deliberately absent: anything about the renderer, the host, or the wall clock. A demo that recorded
"how long the player looked at the screen" would not be reproducible, and time is not part of the
stimulus (D4, and `plan-cross-game-replay.md` § 6c).

### The `content` correction (owner ruling, 2026-11-05)

An earlier draft of this plan refused playback when `content` differed, i.e. cleanroom art against
extracted art. That was the wrong granularity and the line is withdrawn: `content_source` is a **tiles**
marker (`main.c:719` greps it for `cleanroom` versus `tiles extracted`), and substitute art cannot change
game state. Refusing on it would make demos unshareable between the two legal ways of supplying assets for
no gain.

The refusal stays where state actually lives:

| input to state | policy |
| --- | --- |
| `mapx`/`monx`/`tlkx` content files | refuse on digest mismatch, naming the file |
| the slot's sector overlays (`mapx`/`monx` the recording itself mutated) | refuse on digest mismatch, naming the file **(overtaken: deviation 16 notes these once instead; § 13 removes the category)** |
| the PLAYER record | refuse on digest mismatch (`save-sha`) |
| tile art, theme, `fmtowns` art | note once on stderr, play |

That is the whole correction: refuse on bytes that reach the world, note on bytes that reach the screen.

### Filenames

`demos/<YYYYMMDD>T<HHMMSS>.demo` — ISO 8601 **basic** format, e.g. `20261105T231407.demo`.

Extended format (`2026-11-05T23:14:07`) is invalid in a filename on Windows filesystems, and the engine
ships a Windows build (`make sdl-win`). The house already has this shape: the SDL screenshot writer uses
`%Y%m%d-%H%M%S` (`renderer_sdl.c:425`), and `main.c:839` uses extended ISO 8601 only for a *stamp inside*
content, where colons are legal. Basic format keeps lexical order equal to chronological order, so a
directory listing sorts correctly.

Uniqueness: one-second resolution on a single-instance engine is enough in practice, and the writer still
checks for an existing file and appends `-2`, `-3` rather than overwriting, because a silent overwrite of
the demo someone was about to share is unrecoverable.

Do not derive the stamp from `ctx->clock`. Headless and playback use `u2_clock_script`, which is virtual by
design, so a scripted run would produce the same filename every time. Use host time (`time` +
`localtime`, as the screenshot writer does), and provide `--demo-name` so a gate can pin it.

---

## 5. What already exists — do not rebuild it

| capability | where | state |
| --- | --- | --- |
| per-turn screen + frozen-state digests | `engine/src/core/replay.c`, `--replay-report` | works; CP2 evidence reproduces |
| verify a report against a fresh run | `engine/tools/playlog.py verify` | works |
| resume from a captured RNG register | `u2_rng_load`, `--rng-state HEX6` | works, `rng_check.c` 38 checks, both controls run |
| resume from a raw DS checkpoint | `u2_state_load_ds`, `--load-ds` | works; validates sector against the loaded world and refuses with a message naming `--data-dir` |
| produce such a checkpoint | `--dump-ds` (`0x280` hex bytes) | works; span is too short for a demo (D3) |
| single key seam every key passes | `seam_key_poll`, `seam_key_wait` | works; carries the Ctrl-S precedent (D10) |
| key injection backend | `u2_input_script` | works headless |
| a `u2_input` backend is the injection point | `platform.h` `u2_input` | works |
| per-turn frame + state file pair | `u2_turnlog`, `--debug cli`, `--debug-limit` | works; the observer call sites are the ones to reuse (D2) |
| turn counter | `ow->turns` / `ow->logged_turn` | works; debug only — the demo does **not** use it (D12) |
| DS differ (skeleton) | `tools/replay_diff.py` | partial: command-level alignment and BCD/hex normalisation open. This plan supplies it a producer and a turn-aligned feed |
| a per-save sector overlay, and a redirectable save dir | `core/slot.h`, `--save-dir` | works; this is what D6 and the playback isolation in § 7 use |

The one thing that does **not** exist and is load-bearing: a `u2_input` backend that reads a demo. That is
`input_demo.c`, and it is small because `u2_input_script` is its template.

---

## 6. Architecture

Four pieces, one of which is a backend and three of which are a state machine.

```
engine/src/core/demo.h            format structs, recorder state, playback state, the public API
engine/src/core/demo.c            framing, write/read, per-record recording and verification
engine/src/platform/input_demo.c  u2_input backend that replays a demo's key stream
engine/tools/demo.py              inspect / verify / diff / convert, in tooling not the engine
```

The dependency direction is the existing one: `core/` knows nothing about backends, backends are chosen in
`main.c` by mode, and the demo module sits beside `replay.c` in `core/`.

Public surface, deliberately tiny:

```c
/* core/demo.h */
typedef enum { U2_DEMO_IDLE, U2_DEMO_RECORDING, U2_DEMO_PLAYING } u2_demo_state;

void u2_demo_begin_recording(u2_ctx *ctx, const char *path);
int  u2_demo_begin_playback(u2_ctx *ctx, const char *path);

/* The seam tap and the record boundary in one call — deliberately one, because two calls at the same
 * site is two things to get out of order. Called from seam_key_poll/seam_key_wait after the uppercase
 * fold and after the screenshot swallow, with the key about to be handed to game logic:
 *   recording — closes the previous record, writes this key's record, appends the key
 *   playback  — verifies the previous record, then lets the key through
 * Returns 1 when it swallowed the key (it was the toggle) and 0 otherwise. Divergence is reported
 * through u2_demo_finish and ctx->exit_code rather than by refusing the key. */
int  u2_demo_key(u2_ctx *ctx, u2_key *k);

/* The one event the seam cannot see: the idle auto-PASS is a turn with no key behind it (D4).
 * Called by u2_overworld_pass only. Writes an @rec record with kind=auto when recording, and at
 * playback it consumes the matching record instead of letting the loop synthesise its own pass. */
int  u2_demo_mark_auto_pass(u2_ctx *ctx);

/* Quiescence for the toggle (D12). The overworld loop and each mode's top-level wait call this, so
 * a pending Ctrl-R starts or stops the recording at a point where no command is half-typed. */
void u2_demo_quiescent(u2_ctx *ctx);

/* Flush and finalise. Idempotent, safe on every exit path, and the only writer of the epilogue. */
int  u2_demo_finish(u2_ctx *ctx);
```

Call sites, exhaustively — this list is the review checklist, and a new turn-producing loop that is
absent from it is a defect:

| site | call | why |
| --- | --- | --- |
| `seam_key_poll`, `seam_key_wait`, before the key is returned | `u2_demo_key` | the single seam every delivered key passes, in **every** mode — this is the whole of D12 and it is why no mode needs its own hook |
| `u2_overworld_pass` (`overworld.c:1191`) | `u2_demo_mark_auto_pass` | the idle auto-PASS is the one turn with no key behind it |
| each mode's top-level key wait (`overworld.c`, `creation.c`, `title.c`, `game.c`, `space.c`) | `u2_demo_quiescent` | where a pending `Ctrl-R` may take effect, because `pending_cmd` is `0` there |
| `main.c` exit tail, beside `--dump-ds` / `--replay-report` | `u2_demo_finish` | graceful quit while recording (owner requirement), and the closing digest |
| `main.c` SIGINT/SIGTERM path, beside the SDL quit path | `u2_demo_finish` | the window-closed and killed cases |

The asymmetry is the design: **one** site for recording and verifying (the seam), and a short list for the
things the seam structurally cannot observe (a turn with no key, and "no command is in progress"). Any
future addition to the second list is a defect in the first.

`--playback` wiring in `wire_backends`: renderer stays whatever the mode chose (headless, terminal or
SDL), clock becomes `u2_clock_script`, input becomes `u2_input_demo`, and `auto_pass_polls` is forced off.
`--script` and `--playback` together is refused, with the same loud style as the existing `--script needs
--headless` refusal, because two input backends is a configuration that has no meaning.

---

## 7. Lifecycles

### Recording

1. `Ctrl-R` in play sets `pending_start`. A message goes to the log: `RECORDING - Ctrl-R TO STOP`. The
   message is not decoration: `AGENTS.md` § 3 requires every action the player takes to produce one, and
   a recording that started silently means the player does not know to stop it.
2. At the next **quiescent** point (D12) the recorder opens the file, writes the header including `mode`
   and `digest-version`, and captures the checkpoint from the live DS mirror (D3). This point is the only
   legal start, which is what keeps `pending_cmd` out of the format.
3. Every delivered key appends one record **before** it reaches game logic, and the file is flushed. An
   idle auto-PASS appends a `kind=auto` record. Recording therefore crosses creation to play, overworld to
   a site, and a site back out with no special case (D12).
4. `Ctrl-R` again sets `pending_stop`; at the next quiescent point the epilogue and closing digest are
   written, the file is closed, and the log shows `RECORDED N RECORDS -> demos/<name>.demo`.
5. Quit, window close, SIGINT and SIGTERM all reach `u2_demo_finish`, which writes the epilogue if the
   recording is open. A hard kill loses at most the open turn and produces a file the reader calls
   incomplete (D8).

Default path: `demos/<YYYY-MM-DD-HHMM>-s<NN>.demo`, under `--demo-dir` if given, created on demand. Never
under `/tmp`: that is tmpfs on this host and a long recording would take RAM from the model
(`AGENTS.md` § 0).

### Playback

1. `--playback FILE` parses the file, refuses an unrecognised version, an incomplete file without
   `--salvage`, or a `content` mismatch.
2. Loads the checkpoint through a new in-memory entry point (`u2_state_load_ds_buf`) beside the existing
   path-based one, at the same point in `overworld_resume` where `--load-ds` is applied — after the world
   and `monx` load, before `u2_monsters_restamp`.
3. Verifies `sector` against the loaded world (existing refusal), verifies `save-sha` and every
   `overlay` digest, and pins the speed and tick fields (D5).
4. **Redirects writes.** Playback must not touch the player's saves or overlays: it sets a scratch save
   directory for the session and says so once on stderr. Otherwise a demo containing a `Q` save or an
   auto-save on site entry would mutate the viewer's slot — the original auto-saves on site entry and on
   rocket launch (2026-11-03 findings), so this is not hypothetical.
5. Steps the records. `u2_input_demo` delivers the keys in order; the seam tap `u2_demo_key`
   verifies the state against the digest recorded at the same point in the recording (D12), so no mode
   needs its own boundary and an intra-turn divergence is caught at the key that caused it.
6. On the last record, `u2_demo_finish` compares the closing digest and prints `DEMO PLAYED n/n RECORDS`. On
   a divergence, prints the record index, the key that preceded it, and the field diff from
   `tools/replay_diff.py`.
7. **Pacing** (D11): SDL and terminal playback pace at roughly real time so the demo is watchable; headless
   playback runs unthrottled so the gate is fast. Pacing is presentation only, and W8 is the gate that
   proves the two produce identical digests.
8. **Cross-version** (D13): a `digest-version` mismatch downgrades to *playback unverified* with a loud
   line and a distinct exit code. The demo still plays, because the stimulus does not depend on the engine
   version — which is exactly what makes an old demo useful for confirming a bug was fixed.

### The `Ctrl-R`-during-playback question

`Ctrl-R` in playback stops playback and returns to play **without** starting a recording, so a player
cannot accidentally record a playback. Recording a playback is refused with a message, because the
result would be a demo of a demo with two checkpoints and one honest explanation.

---

## 8. Determinism: what is pinned, what is proven, and what is not

| question | status |
| --- | --- |
| engine headless replay from `(seed or rng-state, key script)` | **proven** — CP2 evidence reproduces; `playlog.py verify` re-runs it |
| engine replay from a checkpoint + key stream | **new, and this plan's burden of proof** — § 9 W7 is the gate |
| does the renderer affect game state | **unproven** — § 9 W8 is the gate that decides it, and it is cheap: replay the same demo headless and under SDL and compare every digest |
| original's determinism | **proven with a condition** — `docs/plan-cross-game-replay.md` § 6c: reproducible when input is paced on emulated ticks, not real seconds |
| original's idle RNG advance | **observed, and neutralised** by D3/D4 rather than reproduced |
| speed/tick fields | **excluded** from comparison (D3) and **pinned** on load (D5) |
| `U2_AUTO_PASS_POLLS` | a documented placeholder; demos carry their own auto-pass turns so a threshold change cannot break an existing file (D4) |

One caveat that is easy to state and expensive to forget: this table is about the **engine**. Nothing in
this plan says anything about the original's behaviour, and a demo recorded by the engine is not evidence
about `original/ultimaii.exe`. The cross-game half of the work is separate and is where
`docs/plan-cross-game-replay.md` § 4.5's differ gets its other producer.

---

## 9. Work items

Each item names the acceptance and **the control that makes it red**. A gate that cannot fail where it is
enforced is worse than no gate (`AGENTS.md` § 3), and this project has shipped that defect three times.

| # | deliverable | acceptance | the control |
| --- | --- | --- | --- |
| **W1** | `core/demo.c` framing: writer and parser for § 4, round-trip in memory | unit test writes a 5-record demo, parses it, compares field for field | flip one `sha` byte in the parsed copy: the test must fail |
| **W2** | `--demo-dir`, `Ctrl-R` toggle with quiescent-start semantics (D2, D10, D12) | scripted headless run sends `0x12`, a file appears, and the record count equals the number of seam deliveries in the trace | remove the toggle from the script: no file must be written (`0x12` must not reach the dispatcher as `ILLEGAL COMMAND`); and send `0x12` mid-command to confirm the start waits for quiescence |
| **W3** | the seam tap (D1, D12) | a recorded demo's key list equals the keys the engine's trace shows as delivered, in order | feed a script with keys the game never consumes (the known unconsumed `Return`): they must be **absent** from the demo |
| **W4** | checkpoint capture at full `0x650` span, and `u2_state_load_ds_buf` | record, then playback from the in-file checkpoint reproduces turn 0's digest | truncate the span to `0x280` and confirm the creature arrays still load (they do) but a speed-pinned run diverges, proving the long span is doing work |
| **W5** | auto-PASS as `kind=auto`, and auto-PASS disabled in playback (D4) | a recording made while idling contains `kind=auto` turns and replays them | re-enable auto-PASS in playback: the gate must go red at the first auto turn |
| **W6** | playback isolation: scratch save dir, overlay redirect | playback of a demo containing a site entry leaves `saves/` byte-identical | pre-populate a slot, run, assert digests; then disable the redirect and assert the gate now fails |
| **W7** | **`make test-demo`**, `engine/tests/run_demo.sh`: record and play back a scripted headless session, self-contained fixture (D9) | every digest matches; `DEMO PLAYED n/n` | mutate one digest in the fixture: must exit non-zero naming the record |
| **W8** | renderer independence: same demo headless and under SDL (dummy video driver), digests compared | all digests identical | if it fails, that is a **finding**, not a test bug: record it in § 8 and open a defect rather than relaxing the gate |
| **W9** | truncation and salvage (D8) | a file with no `@end` is refused by default and played with `--salvage` | assert the refusal message names the record count recovered |
| **W10** | `engine/tools/demo.py`: `inspect`, `verify`, `diff`, `export-keys` (to a `.keys` script + checkpoint), and `--to-demo` for a `playlog.py` report | each subcommand has a test with a known-bad input | the conversion test feeds a report whose key count disagrees and must refuse |
| **W11** | the `0x12` availability gate: no `cmp` against `0x12` anywhere in `asm/`, and the dev/screenshot control bytes still match their sites | one line in `tools/check_all.sh` | perturb a test-only copy of an `asm/` file to compare `$0x12` and confirm the gate goes red |
| **W12** | documentation: `engine/README.md`, `--help`, and a `docs/harness-portability.md` § 6 feedback line | help text and README agree with the implemented flags (verify the commands, per § 3) | none needed: prose, checked by running the flags |
| **W13** | mode-crossing recording (D12): record across creation → play → site → back out | one demo contains records from all three modes and replays to the same closing digest | truncate the demo at the mode boundary and confirm playback fails at the first missing record, not silently |
| **W14** | cross-version playback (D13): `demo.py corpus DIR` reporting match / first divergence / unverifiable | a demo recorded by build A replays on build B and reports its first divergence | bump `digest_version` in a test build and confirm the run downgrades to *unverified* with the distinct exit code |
| **W15** | pacing (D11): SDL/terminal playback paces, headless does not | a 60-record demo takes visibly longer under SDL than headless, and both produce identical digests | set the headless pace to real time and confirm W7's runtime grows, proving the pacing is applied and not a no-op |

Suggested order: W1, W2, W3, W4, W5, W6, W7 — W7 is the first point at which the subsystem is worth
anything, and every earlier item is a component of it. W8 and W9 follow immediately. W10 to W12 are the
polish that makes it a subsystem rather than a feature.

---

## 10. Hazards

1. **The span change is not free.** `--dump-ds` writes `0x280` and `--load-ds` reads whatever it is given;
   the demo needs `0x650`. Widening `--dump-ds` would change an existing gate's fixture shape. Add the
   demo's own writer rather than moving the old one, and leave `--dump-ds` alone.
2. **`u2_monsters_restamp` after load.** `overworld_resume` calls it after applying a `--load-ds`
   checkpoint, which is how the grid at `DS:1800` is rebuilt from the creature table. A playback path that
   loads state and forgets the restamp will present a frame where every creature is invisible while the
   state digest agrees. W7's frame check, not just its digest check, is what catches it.
3. **The recorder's own polling perturbs what it measures.** `tools/u2_record.py`'s first version added a
   tick wait *after* a consumption poll and reintroduced the exact jitter it existed to remove
   (`plan-cross-game-replay.md` § 7a). The engine-side tap has no tick budget to spend, but the same class
   of error is available: the observer must not poll, sleep, or read the clock. It records and returns.
4. **Digests are not the file.** `@end`'s trailing digest covers framing, not the checkpoint's payload
   bytes; those are covered by `@checkpoint-sha`. Two digests, two jobs, and a file where only one is
   checked is a file where a corrupted checkpoint replays as a divergent demo instead of a broken file.
5. **A demo can encode a defect faithfully.** If the engine is wrong at turn 200, the demo records the
   wrong behaviour and replays it perfectly. A demo is evidence of reproducibility, never evidence of
   fidelity. Say so wherever a demo is cited as proof.
6. **A mode that does not reach the seam.** D12 rests on every mode asking `seam_key_poll`/`seam_key_wait`
   for its next key. A future mode that reads input some other way would record a demo with missing records
   rather than an error. The mitigation is W7's record-count check against the trace, plus W2 and W13
   controls that cross creation into play and a site and back.
7. **The virtual clock in filenames.** `u2_clock_script` is virtual by design, so a filename derived from
   `ctx->clock` is identical on every run. Use host time (§ 4, Filenames). This is the kind of bug that
   produces a silently overwritten file rather than a failure.
8. **`Ctrl-R` inside a freeform input loop.** `cmd_Y` runs its own complete input loop
   (`docs/handoff-2026-10-29.md` § 2 records that it was misread as an unresolved fall-through). The seam
   tap is upstream of it, so the toggle works there, and the quiescent rule (D12) means the recording
   starts after that command finishes. Test it: W2's control should include a `Ctrl-R` pressed mid-command.
9. **SDL is not headless.** Playback under SDL runs a real event loop and `seam_wait_ms(16)`. Nothing in
   that path may feed state; W8 decides. Until W8 passes, do not claim SDL playback is verified — say
   "headless verified, SDL unproven" exactly as `plan-cross-game-replay.md` § 6 does.

---

## 11. Owner rulings — all settled 2026-11-05

Every question in this section was ruled on 2026-11-05. Recorded with where each one landed, so a later
reader can tell a ruling from a suggestion.

| # | question | ruling | landed in |
| --- | --- | --- | --- |
| 1 | playback pace | **watchable**, tunable later | D11 |
| 2 | start from a checkpoint or from boot | **checkpoint first** | D3, § 7 |
| 3 | zip or text | **plain text for now** | D7 |
| 4 | refuse on `content` mismatch | **withdrawn** — refuse on state bytes, note on art bytes | § 4, *The `content` correction* |
| 5 | `Ctrl-R` scope | **everywhere**, and it must survive state transitions | D12, the design change |
| 6 | does a demo need frames | **no** | D7 |
| 7 | divergent playback is a failure | **yes** — exit non-zero, name the record | D6 |
| 8 | old demos on a new engine | **wanted**, for regression testing | D13 |
| 9 | demo filename | **ISO 8601 timestamp**, effectively unique | § 4, *Filenames* |
| — | directory | **`demos/`** | D9 |

Ruling 5 is the one that cost design. It moved the verification point from the overworld turn to the input
seam (D12), which is why the file is keyed on records rather than turns and why a demo can start in
character creation. Ruling 8 is the one that stops `engine` version being a refusal: a demo that cannot be
played by a later build is useless for the regression testing it was asked for.

Nothing in this section is open. The two questions the plan cannot answer without a demo existing are the
pacing number (D11) and whether `demos/` should grow a curated corpus convention (D13); both are
deliberately deferred rather than guessed.

---

## 12. Relationship to the cross-game replay plan

`docs/plan-cross-game-replay.md` is the larger idea: record the **original** under Spice86 and diff our
engine against it turn by turn. This plan is the engine half of it and does not compete with it:

| replay plan item | this plan |
| --- | --- |
| § 4.1 resume from a captured RNG register | done already (`u2_rng_load`); the demo carries it inside the checkpoint |
| § 4.4 capture the starting save, not just the seed | **this plan, D3** — a full DS checkpoint rather than a save |
| § 4.5 the differ | gives it a producer and a turn-aligned feed; `tools/replay_diff.py` still needs the alignment work |
| § 6 determinism hazards | unchanged, and § 8 here restates them so a demo claim cannot be read as a fidelity claim |
| § 8 policy: demos ship as script plus checkpoint | **this plan is that artefact**, and D7/D9 keep it that way |

The direction that will actually be built first is the cross-game one, because it is the one that finds
bugs. This subsystem is what makes the result *shareable*: a repeatable scenario a person can send to
another person, rather than a capture directory only this machine can read.

---

## 13. Amendment — the world delta (ruled 2026-11-07, **not built**)

**Design only. Nothing in this section is implemented.** It is written down the day the direction
was ruled because § 4's overlay policy, D7's deferred flag and deviation 16's two-tier split all
point here, and because the reasoning that produced it is worth more than the summary in a commit
message would preserve.

### 13.1 The defect a digest cannot fix

§ 4 refuses playback when the sector bytes a viewer loads disagree with the digest the recorder
captured. That is the right instinct and the wrong mechanism, and it failed on the owner's first
real recording on a real install.

The reason is that the recorder and the viewer are not allowed to read the same files. The recorder
plays, and play writes sector overlays into its own save slot (`docs/save-scoped-worlds.md`), so
the bytes it digests are its slot's. § 7.4 then deliberately hides that slot from playback, so a
viewer cannot be asked to hold it. Deviation 16 handled that honestly by splitting the category in
two and refusing to enforce the half that could not be honoured. Correct, and it left the property
nobody wanted: **a demo recorded after any prior play is best-effort, and `--playback-ignore-content`
is how you play one.** That is the state the owner's 165-record demo is in.

A digest describes the world. It does not travel, and it cannot be applied. What a demo needs is not
a stronger check on the world it started from but **the small amount by which that world differed
from the one a viewer already has**.

### 13.2 The property that makes that amount small (proven 2026-11-07, with controls)

The overlay's `mapx` differs from shipped content almost entirely by **baked creature stamps**, and
stamps are derivable. Four facts, all read off the code and then confirmed by running it:

* `u2_state_load_ds_buf()` is a wholesale `memcpy` of the whole DS mirror and refuses a partial
  span (`state.c:57`). Mounting a checkpoint therefore restores the creature table, not just the
  player.
* `u2_monsters_restamp()` reads `const u2_state *s = &ctx->state` — the mirror — and **not**
  `w->mon`. It first rebuilds all 4096 cells from `map_base`, then stamps `s->mon_type[i]` at
  `s->mon_y[i] * 64 + s->mon_x[i]` (`monsters.c:138`).
* The six creature arrays sit at `0x137..0x1F7`, inside `U2_DEMO_SPAN = 0x650`.
* The ordering hazard is already closed: `ctx->demo_restamp_pending` goes up when a world-mode
  checkpoint lands (`demo.c:557`) and is consumed in the command loop (`overworld.c:1937`), which
  re-syncs `ow->x/y` and restamps before anything draws. Its own comment names the exact failure
  this was worried about — *"the grid was stamped from the freshly loaded monx"*.

So a viewer with pristine content, the checkpoint and a restamp converges on the recorder's grid.
**`monx` needs no delta.** The only world bytes a demo has to carry are terrain edits nothing else
can derive: an unlocked door, a boarded vehicle, a looted chest, a parked rocket — and the rocket is
itself re-stamped from `DS:69/6A/6B`, so it is not even one of those.

Proven, not inferred. `engine/tests/demo_check.c` carries five assertions (`checkpoint_drives_the_stamp`)
and each was demonstrated red by a mutation, per AGENTS.md § 3:

| control | mutation | went red |
| --- | --- | --- |
| A | drop the restamp after the checkpoint mounts | the two post-checkpoint assertions, and nothing else |
| B | stop `u2_monsters_restamp` rebuilding from `map_base` | the ghost assertion only |
| C | shrink `U2_DEMO_SPAN` to `0x100` | the span assertion, plus the round-trip it genuinely breaks |

Control A is placed *before* the restamp call rather than after it, because after it the assertion
would pass for the wrong reason and the control would be vacuous.

### 13.3 The format

`@world` lines replace `overlay` lines in a v2 file, one group per sector the recording loaded:

```
@world mapx20 base=<sha256 of the content bytes> delta=<x>,<y>:<old>-><new>;...
@world monx20 base=<sha256> delta=none
@world tlkx20 base=<sha256> delta=none
```

| field | rule |
| --- | --- |
| `base` | digest of the **content-layer** bytes the loader would have read had the slot held no overlay. This is what makes the refusal satisfiable again: two people with the same release pack match. |
| `delta` | the cells where the bytes actually loaded differ from `base`. Empty for a recording made on a clean install, which is the case that already works today. |
| cell | decimal `x,y`, bytes two hex digits, no `0x`. |
| order | ascending by `y * 64 + x`, always. D7 chose text so demos would be diffable; an unordered delta is not. |
| `base=absent` | a file the viewer has none of. Normal for `tlkx`; several sectors ship without one. Needs no delta. |

Playback: load content, verify `base` (refusal on mismatch, naming the file — the existing
behaviour, now one a clean viewer can pass), apply `delta`, play. `--playback-ignore-content`
keeps its meaning and shrinks to its proper use: overriding a genuine `base` mismatch, not
papering over a slot the viewer cannot hold.

`tlkx` needs no delta because play never writes it — `world.c:359` states it flat: *"tlkx is
read-only, so nothing records a root for it."*

### 13.4 What this retires

* **Deviation 16's two-tier split.** There is no longer a content layer versus a slot layer to be
  honest about, because the delta is taken against content regardless of where the bytes came from.
  `overlay-slot`, the note-once path and `slot_note` all go away rather than being corrected.
* **`--demo-embed-overlays`.** D7 named it as the condition that would revive `.zip`: *"a demo that
  must carry its own sector overlays to be self-contained stops being a text file, and that is the
  day to reconsider the container."* Under § 13 a demo never must. The flag is retired as an idea
  and D7's condition is closed, not deferred.
* **The stale row in § 4's correction table**, which still says slot overlays "refuse on digest
  mismatch" — deviation 16 already overtook it.

**Licensing, which is the reason option three died.** A base sha is a hash and a cell delta is
player-generated state, so a § 13 demo has the same standing as a save file under D9. Embedding
full sector bytes would be distributing 4224 bytes of authored terrain per sector, which is the
thing `AGENTS.md` § 0 forbids and D9 reasons around. The delta is not merely smaller; it is the only
variant that clears the line while satisfying "easily shareable".

### 13.5 Versioning

`udemo 1` → `udemo 2`. **`format-version` bumps; `digest-version` does not**, because the DS digest
scope is untouched and D13's downgrade-to-unverified path must not fire for a reason as shallow as a
new header line. A v2 reader plays a v1 file with v1 semantics (digest compare, no delta); an
unrecognised version still refuses, per § 4.

### 13.6 Gate legs

| item | leg | control |
| --- | --- | --- |
| **W16** | record in a world whose slot holds a dirty `mapx`, replay in a clean world over the same content, require a clean verification | strip the `delta` from the file: must diverge or refuse, never pass quietly |
| **W17** | `base` mismatch refuses and names the file | re-point the existing `dd` mutation at `run_demo.sh:304` from digest to `base` |
| **W18** | two recordings of the same edit emit byte-identical `@world` lines | write the delta out of canonical order and require the writer to be the thing asserted |
| **W19** | a v1 file still plays under a v2 reader | claim `udemo 99` and require refusal |

**W16 is the point of the whole amendment.** It is the leg that could not be written today, because
today it cannot pass — which is exactly the gap `docs/harness-portability.md` records: fifteen
controls on a synthetic fixture and three failures on first real use.

### 13.7 Open items, named rather than discovered later

* **More than one sector.** `U2_DEMO_MAX_OVERLAYS` is 3 — one sector's three files, captured at the
  quiescent point. A recording that walks sectors dirties and loads more than one, and the viewer
  diverges on the second sector it enters. The delta must cover **every sector the recording
  loads**, so the bound has to become a list. Unresolved and load-bearing.
* **Site geometry.** Inside a town, tower or dungeon `map_base` is 16 pages of 16×16 indexed by
  floor, not a 64×64 grid (`monsters.c:143`). `y * 64 + x` is meaningless there. The header already
  carries `mode`, so the encoding can be page-aware, but the format has to say which geometry a
  cell is in. Do not reuse the overworld spelling for a site and hope.
* **Engine-to-engine only.** `map_base` is an engine construct and `docs/map-format.md` warns it
  "must not be read as a model of the original's memory". A § 13 delta is therefore an
  engine-to-engine contract. Fine — replaying a demo against the original is
  `docs/plan-cross-game-replay.md`, not this — but the header should say so in one line.
* **If 13.2 ever stops holding.** The five assertions now guard it, so this is monitored rather than
  open: if the checkpoint stops driving the stamp, creature state moves into the delta and the
  format grows. It stays license-clean either way, since positions are state and not content.

### 13.8 Parked with reasons: the save-side delta

The same afternoon raised a wider idea — that `saves/<slot>/` should hold a delta against content
instead of a full sector copy, so a save stops shadowing later content fixes.

**Owner ruling 2026-11-07: parked, and § 13 is the reason.** The demo requirement was the only
load-bearing argument for it. Once the demo carries its own delta, the overlay format has no
consumer that needs the change, and what is left is content-fix precedence — real but weak, and
answerable later with a content-version stamp in the slot rather than a format change. The full-copy
overlay also stays faithful to `asm/2305`, which writes the stamped grid, and `asm/2305`'s own header
still reads `confidence: low` and "behavior not yet reviewed". Changing a format to deviate from a
routine nobody has finished reading is not a trade worth making twice.

---

## Implementation plan — terrain restore, then `map_base`, then the demo delta

**Owner ruling 2026-11-08: this ordering, and the architecture work is more important than the demo
work.** Phases are sequential on purpose. Phase 1 is a small faithful correction that everything else
cites. Phase 2 is a question before it is a diff. Phase 3 is § 13 and is built to be indifferent to
how Phase 2 resolves.

Each phase lists its stop-and-ask points, because a plan that predicts every outcome is lying.

## 13.9 Phase 1 — the terrain byte: rename and restore

**The finding, in one line.** `DS:1B7[slot]` is the terrain byte saved under that creature.
`asm/0CEE_spawn_tile_store` writes it at spawn from the rolled cell, and `asm/2233_sub_2233`
restores it into the grid when the creature departs. The engine calls the field `mon_kind`, glosses
it as "kind index", and hard-codes `0x70` where the original restores the slot's own byte.

**Evidence, and its grades.**

| claim | grade | source |
| --- | --- | --- |
| `asm/2233` restores `DS:1B7[di]` into the grid cell on departure | **observed** | `mov 0x1b7(%di),%al` + `mov %al,(%bx,%si)` |
| `asm/0CEE` fills it at spawn from the cell's terrain | **observed** | header: "the terrain tile of the rolled spawn coordinate, returned by `asm/50B3_map_tile_ptr` one instruction earlier" |
| no constant `0x70` is ever stored to the grid | **measured** | `grep -rn '\$0x70' asm/*.asm` returns three `cmp`s, one unrelated `DS:23E` store, one `add` |
| the Apple II implementation agrees independently | **observed** | `LDA $4D80,X ; LDY #$0091 ; STA ($06),Y` |
| `monx[128..159]` holds terrain, only ever `0x70` or `0x08` | **measured** | 511 live slots across 33 sectors; 355 × `0x70`, 156 × `0x08` |
| it matches a real unbaked neighbour 469/502 times | **measured, corroborating only** | a neighbour's terrain is not necessarily the creature's |
| a land creature visibly leaves `0x08` and not paving | **inferred — no frame** | `docs/needs-gameplay.md` G66 |

### The complication: `DS:1B7` has three claims on it

This is the thing most likely to go wrong, so it goes before any diff.

| claim | where it comes from | status |
| --- | --- | --- |
| terrain byte | `asm/2233`, `asm/0CEE`, the shipped-data measurement | best evidenced |
| dungeon floor | `asm/147F` fills it with `slot>>1`; `dungeon.c:657,700,795,867` and `monsters.c:181` read it as a floor | live code, five sites |
| "demonstrably **not** a floor" | `asm/330C_cast_target_finder`'s header, and `state.h:129` | also live |

These are not obviously reconcilable and **the plan does not resolve them by fiat.** The reading that
fits all three is that `DS:1B7` is "what is under this creature" — a terrain byte on the world map, a
floor index in a site, because a floor is a dungeon's answer to the same question. That is
**inferred**, and Phase 1 must not depend on it.

**So Phase 1 is deliberately narrow: change only the overworld restore path.** Do not touch
`dungeon.c`, do not rename the dungeon's use, and do not assert a single meaning for the field. The
rename below leaves the dungeon sites reading the same bytes with the same behaviour.

### The changes

1. **`engine/src/core/state.h:421` — rename and correct.** `mon_kind` → `mon_terrain`. The current
   comment is wrong twice over, and both errors belong in the new comment so they cannot return:
   - *"`asm/0AD3` dispatches the special attacks on it"* — **false.** `asm/0AD3`'s own header says it
     "reads the type byte from `[DI+0x197]`" and dispatches `0x34`/`0x38`/`0xF8`/`0x3C` from **type**.
     It never touches `0x1B7`.
   - *"Kind index"* — superseded by § 9's closure.

   Also record that the 2026-10-22 retraction in `asm/0AD3`'s header was **half right**: it correctly
   refuted "sprite" and correctly observed the values `0x08`/`0x0C`, then mislabelled them a kind
   index when they are two members of the step gate's terrain accept set `{0x08, 0x0C, 0x5C, 0x70,
   0x00}`.

   Keep `_Static_assert(U2_OFF(mon_terrain) == 0x1B7)`. **The offset is body-adjacent: the value must
   not move, only the name.**
2. **`engine/src/game/monsters.c:199` — `u2_monsters_clear_spawn_cells`.** Replace `k_clear = 0x70`
   with `s->mon_terrain[i]`. Keep the baked-cell guard (`map_base[cell] != mon_type[i] → skip`);
   it is still correct, because it restricts the pass to cells the shipped file actually baked, which
   is the case § 9 measured. What changes is *what gets written*. Say in the comment that the guard
   reaching the right outcome for the wrong reason is why this was missed, and that the original needs
   no guard at all — it restores unconditionally on departure, and the guard exists only because the
   engine restores at load instead.
3. **`engine/src/game/monsters.c:465` — leave alone.** The spawner already writes
   `s->mon_terrain[slot] = tile;`. The variable at the store site is already named `tile`, which is
   the strongest sign available that the field's meaning was known here and lost at the read sites.
4. **Mechanical rename only, no behaviour change:** `combat.c:284`, `combat.h:41`, `overworld.c:246`,
   `dump_state.c:145`, `monsters.h:5`, `monsters.c:12,45,94,247`.

### Gate legs

| leg | assertion | red control |
| --- | --- | --- |
| **T1** | a baked spawn cell is restored to **its slot's own terrain byte**, not a constant | set two slots' terrain bytes to `0x08` and `0x70`, assert the two cells differ after the clear. Today both become `0x70`, so this is red before it is green |
| **T2** | a slot whose cell is **not** baked is untouched | the existing `monsters_check.c` assertion already covers this and must stay green |
| **T3** | the dungeon sites are unaffected | `make test-cp7` unchanged, and `dungeon.c` absent from the diff |
| **T4** | the offset did not move | the `0x1B7` `_Static_assert` must survive the rename |

`monsters_check.c:162,177,187` assert `0x70` by name and must be rewritten to assert the slot's byte.
**Rewrite them first and watch them go red against the unmodified `monsters.c`** — that is the
control, and per AGENTS.md § 3 it runs before the fix, not after.

### Stop and ask if

- the land-sector frame from G66 contradicts the restore value. A screenshot showing paving left
  behind where the slot byte says `0x08` would mean the restore is not what `asm/2233` appears to
  say, and that outranks this whole section.
- the rename forces a change in `dungeon.c` to compile. That means the two meanings are entangled in
  code and not just in prose, and Phase 1 should stop and be absorbed into Phase 2.

## 13.10 Phase 2 — does `map_base` still need to exist?

**This phase begins with a measurement, not a diff.** Do not start it by deleting the buffer.

`docs/map-format.md` § 10 states as measured fact that *"the original does not rebuild the grid from a
pristine copy on a creature turn, and the engine must not either."* The engine does exactly that:
`u2_monsters_restamp` runs `for (i...) w->map[i] = w->map_base[i]` on every creature turn. Three
constructs exist only to survive that self-inflicted erasure:

| construct | why it exists |
| --- | --- |
| `u2_world_terrain_put` | its own comment: "the reason is **this module's own design rather than anything in the bytes**" |
| `u2_world_dungeon_terrain_put` | the paged twin, same reason |
| `u2_monsters_clear_spawn_cells` | baked art would otherwise return from `map_base` on the first step |

`DS:1B7` answers the baked-art question per slot, which is the only reason `map_base` was invented.
If that holds, one buffer plus restore-on-departure reproduces the original and deletes all three.

### Step 2a — settle what `asm/2199` reads

**SETTLED 2026-11-08. The original has one grid, the gate reads the stamped one, and the engine
already matches it. The contradiction flagged in the first draft of this section was my error and is
retracted below.**

The verdict, and it is short:

* `asm/2199`'s own name-evidence says the tile arrives in `AL` one instruction after
  `call L_0052b3`. So the gate is handed a grid byte by `asm/50B3_map_tile_ptr`.
* `asm/50B3` resolves the cell through `word[DS:472]`, and that word is `0x1800` in every live dump
  (`asm/68C0`'s provenance). Grepping writes of `0x472` finds only `asm/2290` (sector read) and
  `asm/2305` (sector store). **One pointer, one grid.** There is no unstamped copy for a probe to
  read, so the accept set is evaluated over a grid that already carries creature type bytes. The
  stamp *is* the occupancy rule, by construction.
* The engine reproduces this correctly. `u2_world_tile_at()` returns `w->map` — the stamped grid —
  and `u2_world_ground_at()` is the one that returns `map_base`. `creature_may_enter` uses the
  former.

**The retraction, recorded because a wrong claim in a plan is worse than an open question.** The first
draft asserted that `creature_may_enter` read `map_base` and so could not be seeing the stamps its own
comment depends on. That came from reading `world.h:194`'s `return w->map_base[...]` and attributing
it to `u2_world_tile_at` instead of to `u2_world_ground_at` four lines below. It is wrong, there is no
stacking bug, and `ai.c`'s occupancy story is correctly implemented. The check that caught it cost one
`sed` over the right line range, which is the argument for doing step 2a as a read before treating any
of its premises as fact.

**What survives, and it is narrower.** The § 10 contradiction is real but smaller than it looked:
`monsters.c:163` does rebuild `w->map` from `map_base` on every creature turn, which the original does
not do. But `map_base` also serves a purpose the original never needed — `u2_world_ground_at` feeds
`view.h:ground[]` so an EGA renderer can draw terrain and then a sprite over it, and DOS draws exactly
one art index per cell and has no such requirement. So the likely outcome is **partial**, not a
one-buffer refactor: delete the rebuild and the dual writes, restore per slot on departure and kill,
and **keep `map_base` as the enhanced renderer's ground source.** That is a smaller change than the
one floated above, and it does not break the EGA path.

**One loose end, noted and not chased.** `world.h`'s comment on `map_base` says a cell holds *"a
creature's type byte, or 0x78 for the player"*, but nothing in the engine writes a player stamp into
`w->map` — `0x78` is `DS:13`/`player_stamp`, a mode byte, and `u2_foot_stamp()` returns `0x78 + 2 *
pclass`. Either that comment describes the original rather than the engine, or a stamp is missing.
It matters for any code that asks `tile_at()` what the ground under the player is — `overworld.c:2137`
does exactly that in the unlock path. **Check it before trusting that site**, and do not assume it is
fine because nothing has complained.

### Step 2b — inventory before deciding

| site | today | with one buffer |
| --- | --- | --- |
| `monsters.c:restamp` | rebuild source | **deleted** |
| `monsters.c:clear_spawn_cells` | restores baked cells at load | **deleted**; restore moves to the departure path |
| `world.h:terrain_put`, `dungeon_terrain_put` | dual write | collapse to a single write |
| `world.h:u2_world_tile_at` | **already `w->map`**, the stamped grid | unchanged — step 2a settled this |
| `world.h:u2_world_ground_at` | `map_base`, and it is what feeds `view.h:ground[]` for EGA | **stays.** The original has no equivalent need, so this is the one legitimate reason the buffer survives |
| `ai.c:236,318` | `map_base` directly, for the departure restore | becomes `w->map[old] = mon_terrain[slot]` |
| `view.h:ground[]`, `view.c:110` | second read so EGA can draw terrain then a sprite | **needs an answer**: with one buffer, what goes under a sprite? |
| `demo.c:1143` | digest source | already moving to raw file bytes in Phase 3 |
| `world.c:335` | the load-time `memcpy` | deleted |

`view.h:ground[]` is the hard one and the likely reason Phase 2 does not fully land. `DS:1B7` may
answer it per creature — it *is* the terrain under that creature — but that covers only cells occupied
at that instant, and the view is rebuilt from the grid every frame.

### Step 2c — decide, in writing

| outcome | when |
| --- | --- |
| **partial** — the leading candidate after step 2a | delete the rebuild-from-`map_base` and the dual writes; restore per slot on departure and on kill; keep `map_base` solely as `u2_world_ground_at`'s source for the enhanced renderer |
| **full one-buffer** | only if `view.h:ground[]` proves derivable from the stamped grid plus `mon_terrain[slot]` for occupied cells, so `map_base` can go entirely |
| **no change** | if removing the rebuild breaks door or vehicle durability (M1), or the per-slot restore turns out insufficient outside the departure path. Then § 10's contradiction stands, documented, and the engine stays a deliberate divergence |

Worth stating plainly, because it changes what "faithful" means here: **the original has no EGA
requirement at all.** It draws one art index per cell, so `ground_at` is an enhancement of ours and
not a fidelity claim. Keeping `map_base` for it is therefore not a divergence from the bytes — it is
extra machinery the bytes are silent about, which is a different and much more comfortable category.

### Step 2c — DECIDED 2026-11-08: **partial**, and Phase 1b is what makes it possible

Two facts settled it, both checked rather than assumed.

**1. `map_base` has a live consumer.** `frame.c:308` calls `u2_view_ground_at()` to composite a
sprite over terrain in the EGA path. So `map_base` is not dead weight and the full one-buffer outcome
is off the table. (An earlier grep for consumers came back empty and would have led to the opposite
conclusion; it matched `.ground`/`->ground` and missed the accessor. Recorded because a null result
from a pattern that is too narrow is the same failure as a gate that cannot go red.)

**2. The original never rebuilds. It patches one cell at a time.** Three sites, all targeted:

| event | the original | the engine today |
| --- | --- | --- |
| departure | `asm/2233` restores `DS:1B7[di]` into the old cell | `ai.c` restores `map_base[old]`, then a full rebuild |
| arrival | `asm/225A` saves the new cell's terrain into `DS:1B7[di]`, then stamps | now does this, Phase 1b |
| death | `asm/2E3D` masks the cell with `and $0xF0` through `[DS:8]` — one cell | full rebuild at `overworld.c:1865` |

So § 10's finding stands and the engine's rebuild is a genuine divergence, but it is a divergence of
*method*, not of outcome — and removing it is a refactor with its own gates, not a deletion.

**The dependency nobody saw.** `clear_spawn_cells` writes **both** arrays, and that is what forces the
load-time rebuild: blank the stamp in `map`, then restamp to put it back. If it wrote only `map_base`,
`map` would keep the file's stamps — which are already correct, because `mapx` and `monx` are a matched
save pair — and the load rebuild becomes unnecessary. But fixing `map_base` at a creature's *current*
cell requires the slot byte to describe the current cell, and that is exactly what Phase 1b added.
**Before `asm/225A` was mirrored, the partial outcome was not reachable.** The phases turned out to
be load-bearing on each other, not just sequential.

**Decision, as a sequence of separately-gated steps:**

| step | change | gate |
| --- | --- | --- |
| 2c-i | `clear_spawn_cells` writes `map_base` only, not `map` | M2, and the ghost counter must stay at zero |
| 2c-ii | drop the move-path rebuild at `overworld.c:130` — `ai.c` already commits per creature and its own comment calls the rebuild "redundant rather than wrong" | M3, the `monx61 (31,44)` stacked-cell case |
| 2c-iii | replace the kill-path rebuild at `overworld.c:1865` with a targeted restore of the dead slot's cell | M1, and re-read the staging-vs-map trap recorded in that site's comment before touching it |
| 2c-iv | keep the load-path and demo-mount rebuilds | — |
| 2c-v | `terrain_put`'s dual write **stays**, but its comment must be rewritten: it is no longer "survive our own rebuild", it is "keep `ground_at` honest" | M1 |

**Not decided, and deliberately so:** whether the load-path rebuild can also go. It can probably be
replaced by a stamp-only pass, but the demo-mount site (`:1944`) genuinely needs a full re-stamp after
a checkpoint lands, and collapsing the two would couple Phase 2 to Phase 3. Leave it until Phase 3
exists.

### Steps 2c-i, 2c-ii, 2c-v — shipped 2026-11-08. Step 2c-iii — rejected as written

**2c-i** `clear_spawn_cells` now writes `map_base` only. In a saved sector the baked byte *is* the
stamp, so blanking it was always the wrong job — it is what forced the load rebuild to put it back.
Behaviourally neutral while the load rebuild stands, and correct in meaning. `monsters_check` gained a
`stamp_kept` assertion for the new contract: 38 checks, 0 failures.

**2c-ii** the move-path rebuild is gone. This is the one that actually removes a divergence from
§ 10. Its control was run *before* the removal: disabling `ai.c`'s per-creature stamp commit makes
`ai_check`'s M3 leg go red — slot 3 stacks onto the cell slot 4 entered in the same turn — which
proves the assertion measures the commit and not the call being deleted. Removing a redundant call
without a test that can tell the two apart is just deleting coverage. 60 checks, 0 failures.

**2c-v** `terrain_put`'s dual write stays, but its comment was lying. It said the write was needed to
survive the rebuild; with 2c-ii gone that stopped being true the same day. The real reason now is
`u2_world_ground_at` → `frame.c:308`: an edit that missed `map_base` would be right on CGA and show
stale ground under a sprite on EGA — a wrong answer only one renderer can see.

**2c-iii was wrong as planned, and the plan's own stop-and-ask caught it.** The step said "replace the
kill-path rebuild with a targeted restore of the dead slot's cell". It cannot be done that way, in
that order:

```c
/* combat.c, the lethal branch — runs FIRST */
ctx->state.mon_kind[slot] = 0;      /* death clears hp, type and kind (file 0x30D2..0x30DC) */
...
/* overworld.c:1865, the r == 2 branch — runs SECOND */
u2_monsters_restamp(ctx, &ow->world);
```

By the time the restore site is reached, the terrain byte has already been zeroed by the record clear.
A naive targeted restore would write `0x00` — **water** — into the cell, which is the letter-A defect
from that site's own comment wearing a different hat. The original gets away with it because it masks
the cell *before* clearing the record, and because its mask (`and $0xF0`) is a dungeon nibble
operation, not an overworld terrain restore.

Doing it properly means moving the restore into `combat.c` ahead of the clear, and giving it a dungeon
variant — a different change from the one the plan described, in a path with an owner-visible
screenshot behind it (`captures/spice86/cp5-fight/screen3.png → screen4.png`). **Left undone on
purpose.** The kill-path rebuild is correct today; it is merely not what the original does.



**Whatever is chosen, record it in `docs/map-format.md` § 10 next to the finding it resolves.** A
measured finding the code contradicts, with no note saying why, is the shape AGENTS.md § 3 calls a
gate that cannot go red.

### Gate legs

| leg | assertion | red control |
| --- | --- | --- |
| **M1** | an opened door survives a creature turn (`site_state.py` case C) | must stay green through every step |
| **M2** | a creature leaving a cell restores that slot's terrain byte via the **departure** path, not a load pass | remove the departure restore: must go red |
| **M3** | no two creatures share a cell | seed `monx61 (31,44)`, the one shipped stacked cell, and assert the outcome matches the original's data |
| **M4** | `make test` CP0–CP7 green at every commit in this phase | — |

### Stop and ask if

- step 2a shows `asm/2199` reads an unstamped grid. Then the original *does* keep a second copy,
  `map_base` is faithful after all, and Phase 2 becomes a documentation change rather than a refactor.
- `view.h:ground[]` turns out to need a full second buffer. That is a legitimate answer, not a
  failure, and it should be written down as one.

## 13.11 Phase 3 — the demo world delta

§ 13.1–13.8 stand. Two corrections from the last two days apply, recorded here rather than by
rewriting § 13.3, so the reasoning that produced the wrong version stays visible:

1. **The digest and the delta are both raw file bytes, never `map_base`.** `map_base` is mutated by
   `clear_spawn_cells` before `note_world` runs (`overworld.c:1365`, then `:1473`), so digesting it
   ties the digest to whichever restore mechanism the engine implements — i.e. to Phases 1 and 2.
   Owner ruling 2026-11-08: **the existing demo does not matter**, so the `digest-version` bump that
   follows is free. File-vs-file also dodges the 2-row-shift hazard in `map-format.md` entirely,
   because live grid state is never diffed.
2. **The apply hook is at sector load, not at the quiescent point.** A delta must land in the grid
   before `clear_spawn_cells` runs, so it goes immediately after `u2_world_load(map)` at
   `overworld.c:~1355` and in `site_reload` at `~265` — not beside `demo_restamp_pending`.

Build order, cheapest proof first:

| step | work | leg | control |
| --- | --- | --- | --- |
| 3a | content-only sector read into a caller buffer; must not touch the live world | **W17** | re-point the existing `dd` mutation at `run_demo.sh:304` from digest to `base` |
| 3b | writer: delta at note time, file-vs-file, canonical order by `y*64+x` | **W18** | two recordings of one edit emit identical `@world` lines |
| 3c | reader + apply at the two load sites; `format-version` 1 → 2, v1 files still play | **W19** | claim `udemo 99` and require refusal |
| 3d | per-sector: `U2_DEMO_MAX_OVERLAYS` 3 → growable list, `overlay_checked` latch keyed by sector | **W16** | record dirty-slot, replay clean, require clean verification; strip the delta and it must diverge |

**W16 is the reason.** It is the leg that cannot be written today because it cannot pass today, and
`docs/harness-portability.md` records exactly this gap: fifteen controls on a synthetic fixture and
three failures on first real use.

### Stop and ask if

- the load-time apply hook needs the demo parsed before the world loads. That is staging, not
  mount-at-quiescent, and it reshapes playback ordering rather than adding a feature.
- a site-mode delta is requested before the cell encoding for paged grids is ruled (§ 13.7).

## 13.12 Sequencing summary

| phase | size | risk | unblocks |
| --- | --- | --- | --- |
| 1 | small, ~6 files | low; one behaviour change, one rename | G66's frame, and an honest `state.h` — **done, plus Phase 1b** |
| 2 | unknown until 2a | **the real risk is here**, and 2a is cheap enough to find out early | whether `terrain_put` and `clear_spawn_cells` can go — **decided: partial, five gated steps** |
| 3 | medium, well-specified | low once 1 and 2 settle | shareable demos, W16 |

Phase 1 ships alone. Phase 2 ships as a decision record even if the decision is "no change". Phase 3
is indifferent to Phase 2's outcome by construction, which is what makes it safe to specify now.

**Status after 2026-11-08:** Phase 1 and Phase 1b are shipped. Phase 2 is shipped except **2c-iii**,
which was rejected as written — see the step 2c note. 2c-i, 2c-ii and 2c-v landed together, because
2c-i is a behavioural no-op until 2c-ii gives it a reason. Phase 3 is unchanged and still unbuilt.


# Outstanding work, validated against the tree

**What this is.** One list of what is actually still open, produced 2026-11-15 by reading the
handoffs, then re-checking every item they call outstanding against the code, the gates and the
capture corpus. Nothing here is recalled and nothing is copied from a handoff without being
confirmed. Where a handoff said "open" and the tree says "shipped", the item is not in the list
below — it is in § 7, which is the correction record.

**Re-checked the same day, later.** Two rows of § 1 had already closed while this file was being
written: the `DS:46` writer (closed statically by G80b) and the pub (shipped). Both are in § 7
now. This is the exact failure this file was written to catch, catching itself.

**What this is not.** `docs/needs-gameplay.md` is the capture worklist and stays the authority
for open *questions*; `docs/handoff-outstanding-agent-tests.md` is the index of the subset an
agent can drive without the owner, and it is fresher than anything it indexes (updated
2026-11-13). This file is about **work to build or decide**, not questions to measure, and it
names the G-row rather than restating its recipe whenever one exists.

Grades are the AGENTS.md § 5 grades. `owner` means a person has to choose or look; `agent` means
it can be driven from a shell.

---

## 1. Work to build: gaps the code names, plus owner-ruled items

These are the only places the engine admits it does not do something. `grep -rn "engine_gap(ctx,"
engine/src/` returns **none** as of 2026-11-15 — the last one closed and is recorded in § 7 — and
the command table has no `NULL` slots left: all 26 letters are implemented and no `UNPROVEN:`
marker remains anywhere in `engine/src/`. What is left in this section is work the engine does
not claim to be missing, which is a different kind of gap.

| item | where | what is missing | grade | who |
| --- | --- | --- | --- | --- |
| The goal-spec policy rule | `docs/goal-spec.md` P8 | The engine now knows *why* something is invulnerable (slot 0, G78) but the policy layer has no way to express "stop attacking this", so a bot will attack an invulnerable guard forever. This is a bot rule, not engine behaviour. | measured | agent |

| Gendered speech in enhanced mode | `docs/enhanced-layout-plan.md` | **Owner-ruled 2026-11-15, not built.** Enhanced mode should address the player according to their gender. This is a deliberate divergence from the original and must be treated as one: the DOS binary's only gendered line is the cleric shrine's `BROTHER`/`SISTER`, and that branch is **dead** — `DS:46` holds the typed letter `'M'`/`'F'` while the code compares against `1` (G80b, `asm/1869_cleric_shrine.asm` header). So there is no original behaviour to copy, only an intent to implement. Two consequences: it needs its own gate rather than a fidelity test, and it must not be switched on in classic mode, where matching the original means always saying `BROTHER`. Before building, find every gendered line the enhanced sources actually have — the FM Towns material in `references/` and `disasm/fmtowns-crosswalk.tsv` is where to look, because the DOS exe has exactly one and it never fires. | owner-ruled, unbuilt | agent |

## 2. Decoded, drawn, but not yet honest in the gate

| item | where | what is missing | grade | who |
| --- | --- | --- | --- | --- |
| **Hyperwarp warp field — BUILT 2026-11-15.** The animation runs now: `commands.c` `space_warp_field()` and `space_warp_engage()` drive `asm/6DFD`'s five bounded redraw loops in the order the bytes run them, presenting a frame per iteration. The frame **counts** are bytes (128, 128, 63, 126, 64); the frame **time** was invented and is now **measured** — `U2_WARP_FRAME_MS = 25`, from 119,055 cycles per field redraw sampled 24 times during a live hyperwarp (spread 1.25%, which is itself the finding: a draw-cost-bound loop has no programmed pause). Gated by `space_view_gate.py` leg 8, with a control that drops a phase and must go red. Still not faithful: `asm/6B3D`'s mode ≥ 2 branch is **read, and it draws nothing** — the star is plotted before the branch diverges, so the two arms put the same pixel down. What the branch selects is a rate-limited tone (mode < 2 drives `asm/5CD2_speaker_freq` every pass; mode ≥ 2 counts a CS-side word down and fires `asm/7242` on zero, which reloads the word to `0xE0` or `0x18` and also reaches the speaker). So phases C and D drawing the same radiating field as B is **faithful, not a gap** — there is no second animation to build. What the engine lacked was the sound, and **that is now sampled and built**: `asm/6DFD` seeds a CS-side divisor at `0x2200` with increment `-1` and flips it to `+1` at phase B, `asm/6B3D` advances it once per star (64 a frame), so the warp is a rising pitch from 137 Hz on A, falling on B, and a slow `0xE0`-spaced pulse on the mode ≥ 2 arm. `commands.c` `space_warp_phase` models it and leg 10 gates it, with a control for the sign flip because deleting a tone is obvious and inverting one is not. **And a second thing, corrected the same day after the owner pointed at what he actually remembers:** the streaks — stars stretched into lines, like they are moving fast — are real, and they are not a routine, they are **mode 0 failing to erase**. `je L_006e14` skips the erase of the star's old position, the warp path never clears the screen between redraws, so mode-0 frames accumulate into a trail radiating from the ship. `u2_space_scene_draw` calls `u2_frame_clear` on **every** frame, so the engine cannot produce a trail at any mode — that is why the built warp reads as a shimmering field rather than streaks, and it is a buildable gap, not a capture question. **Built the same day:** the clear is now guarded on a `keep_frame` field the warp sets from the mode (`mode == 0` keeps the previous frame, exactly asm/6B3D's rule), and star radii grow with a `warp_frame` counter so there is something to accumulate. Gated by `space_view_gate.py` leg 9 with a control that deletes the guard and must go red — an unconditional clear is what a renderer does by default, so this is the regression most likely to arrive disguised as a tidy-up. **One named divergence remains, and it is the way the streaks leave, not that they appear:** asm/6B3D erases per star, so the trail phase A lays down is eaten dot by dot as phase B runs, whereas the engine wipes the whole frame on B's first frame. The phase table itself is bytes, re-read from `asm/6DFD` while building this: 128 frames at mode 0, 128 at mode 1, then 63 stepping the mode by 2 while its sign bit is clear. The text below is kept because it records a claim this file made and then refuted the same day. | The radial field is **drawn** and its centre, rectangle and origin are measured, but nothing selects the branch, so the animation exists and never plays. **The stated blocker is gone: the trigger and duration were never a capture question.** They are in `asm/6DFD`, which is the only caller that sets the field's motion mode — four bounded redraw loops, `DS:4C7` as the mode (0 plot-in-place, 1 erase-and-plot, ≥2 a further branch), `DS:4C8` as the frame counter at `0x80`/`0x40`, delays `0x16`/`0x18`/`0x1a`. The field routine itself is now `asm/6B3D_space_field_draw`, renamed from `sub_6b3d`, and its two 64-entry star tables corroborate the count the engine measured off frames. What is left is an **engine feature, not a decode**: the space scene is a stateless render with nowhere to put a countdown. Also corrected: this file's own note that the warp "lasts under a second" was inferred from a 4-5 s frame sampler and the bytes say a few hundred frames. Still undecoded and not needed for the branch: the per-star algorithm. | **built** — counts and frame time both measured | done; G83 and G87 both closed by measurement 2026-11-16 |
| The stamp nibble in a PASSWALL-cleared cell | `commands.c` `cmd_C` slot 4 | The original restamps creature and object nibbles into the grid; the engine draws creatures from the arrays and never stamps, so a cleared cell stays cleanly zero. **This is not cosmetic, and the row used to understate it.** The dungeon walk gate is `(c & 0x8f) != 0` — bit 7 is rock, the **low nibble is a stamp in the way** — so if the original's stamp pass drops a nibble back into a cell PASSWALL just cleared, the original prints `-BLOCKED-` where the engine walks through. That is a gameplay divergence, not byte hygiene, and it changes the verdict on the row: do not wave it off until it is measured. What is settled and now gated: a hole you do clear is walkable — `tests/passwall_check.c`, 10 checks, mutant control writes `0x01` instead of `0` and goes red on the walk assertion specifically. **What would close the rest:** one live read. Cast PASSWALL on a wall with a creature in the cell behind it, read the stamped grid byte at `DS:1800`, and try to walk. If the original's nibble is `0` too, this row is deleted; if it is not, we know whether anyone can see it. | measured | agent, needs one live read |

## 3. Open questions that block a fidelity claim

Short list; the recipe and byte evidence live in the named G-row.

| item | blocks | grade | who |
| --- | --- | --- | --- |
| **G81 — CLOSED 2026-11-15, statically, and the premise was the wrong question.** The answer: the owner's rule holds with no exception, the chest write keeps the both-copies helper, and what makes an emptied chest refill is the **loader**, not the store — `asm/34E4` runs the store gate *before* it changes `DS:4A`, so it banks the sector you are leaving and never the one you enter, then re-reads `0x1000` bytes over `DS:472` for the sector you enter; `asm/3823` on the way out writes only the player record. A site's own `mapx` is read fresh on every entry and never written. Pinned by two checks in `engine/tests/site_check.c`. The paragraph below is kept as the record of how far the wrong premise got. | `dungeon.c` empties a chest through `terrain_put`, which writes `map_base` and therefore **persists between entries**. Owner ruled the same day that nothing in a dungeon persists between entries, which is why PASSWALL now uses `put`. Both cannot be right. **The static angle is now followed, and the premise was right.** `asm/2305` is a real, live store: `AH=28h`, `CX=0x1000`, `DX=[DS:472]` — the whole 4096-byte sector — and it executes end to end in 33 harvests. It is reached only through `asm/22FD`, whose entire body is `if (DS:6D == 0) store()`, so **the store is Earth-only**, and its callers are site entry for town, tower and dungeon, `cmd_L`, and the save path. So persistence of a mutated sector is a genuine property of the original on Earth, and "nothing persists" is not a design rule. What this does NOT settle: whether the chest's emptied byte is in the buffer at the moment a store fires — `cmd_G` (GET) is not itself a caller — so the live test is still the test (empty a chest, leave, walk back in). It also re-opens a decision: the engine's store-on-transition was deleted in 2026-10-29 on the strength of `asm/22FD`'s old header, which asserted the original writes no sector file at all. That header is now corrected, and the observation it leaned on (sector files byte-identical after town entries and exits) is exactly what storing unchanged content produces. **Re-adding the store is an owner decision, not a fix.** **New support for the "does not persist" side, 2026-11-15:** `captures/spice86/g82-site-autosave` entered a dungeon, spent time in it, and climbed back out, and the dungeon's own `mapx35` and `monx35` never moved — the only sector file that changed in the whole run was `monx30`, the outdoor sector being left. A mutated dungeon cell has no visible path to a file. | **closed — measured** | done, no capture needed |
| **G43** — the plane's forest rule, and W6 the plane's airborne flag | `docs/handoff-vehicle-state-machine.md` W6 is the one genuinely open item there and it is marked as needing a capture rather than a guess. | recalled | agent, needs the plane |
| **G49** — frigate cannon reach | `cmd_F`. Owner recalls a 3-space range; nothing decoded gives a range at all. | recalled | agent, needs a frigate |
| **G77** — the plane's own march-step cost | `U2_PLANE_STEP_MS` is a borrowed 103 ms floor. | inferred | agent |
| **G76** — column 40 spill or clamp | one frame at margin `0x28` with a full-width line. | inferred | agent |
| ~~**G30** — does `Ctrl-Y` move south~~ **CLOSED STATICALLY** | **Yes — and the engine was wrong.** The arrow table `CS:0x4F09` maps scancodes to control bytes (Down→`0x19`), and `key_poll` swallows three of the four (`asm/4F58`/`4F5C`/`4F60` compare `0x5`/`0xe`/`0x17`) but never `0x19`. `asm/4FBE` matches on `AH`, `asm/4FCC` writes the mapped byte to `AL`, `asm/4FD1`'s `mov $0xff,%ah` destroys the scancode — so a typed Ctrl-Y and the Down arrow are the same two bytes downstream. The engine let `0x19` fall through to the command jump table and silently do nothing; now mapped in `seam_key_poll`, one arm covering overworld **and** dungeon, because `asm/277B` has Down mean RETREAT in a site and the original cannot distinguish them there. `engine/tests/dirkey_check.c`, 12 checks in CP0, mutation control red on the two naming assertions. **No capture could have answered it:** Spice86 applies Shift but ignores Ctrl, and the BDA ring is emulator-owned. | measured (bytes) | closed |
| **G84** — does a stamp land back in a PASSWALL-cleared cell, and does it block the step | the walk gate's low nibble is a movement test, so this is gameplay and not byte hygiene. One `DS:1800` read settles it. | measured (the mask), unknown (the stamp) | agent, headless |
| **G85** — the warp animation has never been seen playing | **CLOSED 2026-11-16 by `captures/engine/g85-warp/`, and it found a real divergence.** "Needs a human with eyes" was wrong — the harness attaches PNGs; what was missing was a flag that could write a frame from the middle of a 509-frame animation. Added `--dump-screen-every` and `--start-at`, staged both sides identically (same record, same pad, same four keys), and compared. The original radiates from **the ship** through 360 degrees, sparse and dotted; the engine converges on a knot and smears along **one diagonal axis** — a beam, not a burst — and draws no ship. The mode-0 trail work was necessary and not sufficient. Also: the engine's right-hand readout reads `XENO / YARO / ZABO` where the original reads `XENO / YENO / ZARO`. Follow-ups are the radial geometry fix and the label check. **G88, filed as the blocker on that fix, was closed the same day and its premise was wrong:** the `cs:0x6cac` table is 16 static entries of `index + 1` indexed by `(x ^ 0xff) >> 4`, a per-x-bucket stagger against a global counter — which explains the original's sparse dots but is not a motion vector and so is not the radial cause. **G89** now carries what is actually left: the y path and the derivation of `DS:4C3`/`DS:4C6`. Until that is answered there is no faithful warp fix, and inventing a radial formula is forbidden rather than merely unwise. | closed — engine diverges | done; fix is engine work, gated on G89 |
| **G86 — ANSWERED 2026-11-15, headless, and the row's first draft was wrong.** Owner: the rocket landing looks slower in the original. The 492,074 cycles/step is already a loop-level measurement, not one callee's; the soft part is dividing it by a nominal 4.77 MHz on an emulator that models no 8088 bus wait states, which makes 103 ms a **lower bound** — and a lower bound looks exactly like a too-fast engine when a player meets it. Three real defects fell out: the pace figure defined three times under two names in one file, the seam it cited (`asm/52CA`) is the DOS FCB call wrapper renamed away from `timed_wait` on 2026-10-21 with the engine's prose never following, and `space.h` still said the descent starts at `(0,6)` after the capture corrected it to `(0,0)`. Pinned by `tests/dive_pace_check.c`, 21 checks. **What is left is a feel call, not a byte:** leave 103 labelled as a bound, or set it from memory. | owner decision on the number | measured |
| **G58 — CLOSED 2026-11-15.** The pictures were already observed; the engine was the open half. `present_poem()` now maps each couplet to its own picture instead of the refuted two-picture guess, and `pic.h` carries the three names the executable itself references. Couplet 3 stays inferred (never observed, keeps the town picture) and couplet 6's `picmin` label stays unverified — the file is not a pyramid. | the code fix, which shipped | measured, one row inferred | done |

## 4. Decisions only the owner can make

| item | doc | state |
| --- | --- | --- |
| The enhanced-mode layout: viewport shape, frame size, string provenance, when it turns on | `docs/enhanced-layout-plan.md` § 4 | Still a plan. Verified: "No engine code has been changed for this." Four decisions, none taken. |
| SDL2 vs SDL3 | `docs/sdl2-vs-sdl3.md` | An undecided decision record. Note the code has already drifted one way: `make sdl` probes **SDL3 only**, so the doc is behind the Makefile rather than the other way round. |
| CP11 | `docs/phase4-state.md` | Tabled by the owner 2026-10-28e, deliberately not lost. Listed so it is not mistaken for forgotten. |

## 5. Built and green, awaiting an owner gate

Not work — waiting on a person. Listed because "what is left" is the wrong question if these
read as done.

- **CP1** — awaiting owner eyeball against DOSBox.
- **CP3** — done, awaiting owner gate.
- **CP9** (SDL renderer) — largely delivered 2026-11-03, not signed off.
- **CP10** — sign-off, todo by definition.

## 6. Environment, not code

- **Context-mode needs Node 24.15+; this host has 22.23.3.** Worked around throughout this
  session by running probes through `live_terminal_run` into `/var/tmp` files. It costs time on
  every session and nobody has written down who owns fixing it.

One bullet was deleted from this section on 2026-11-15, and it is worth recording that it was here
at all: it said SDL3 is not installed on this host, so `u2-sdl-static` cannot be rebuilt here, so
the `engine build fresh` gate would stay red forever. All three clauses were wrong. The host is not
meant to have SDL3 — `./deploy/linuxbuild/build.sh` builds the static-SDL artifact in a container,
the image exists, and running it takes about a minute. The gate was red because of a dirty build
tree, which `make clean` fixes. An environment constraint that was really a missing command sat in
this file justifying itself for weeks. The build, play and seed paths are now in
`docs/tooling-procedures.md` § "Build, play, seed".

---

## 7. Checked and found already shipped — the corrections

Everything below was listed as outstanding in a handoff that is still named as an entry point,
and is not. Each was verified against code or a gate, not against another document.

| claimed open | where it was claimed | actual state, and how it was checked |
| --- | --- | --- |
| The last `engine_gap()`: "no dialogue for that role" | `docs/outstanding.md` § 1 (this file), and `commerce.c:724` | **Closed 2026-11-15, and the gap was the wrong shape.** The row said one role "has no dialogue and says so rather than inventing one", which sounded like the honest end of the decode. It was not honest: `asm/4209` is `sbb $0x80,%al ; call L_00174e` and **nothing else** — no branch on whether a record came out — and `asm/154E_tlkx_record_printer` walks NUL-delimited records and returns via `al = es:[di] ; cmp 0 ; je 1580` having emitted nothing when the index runs past the records into the buffer's trailing zeros. So the original is **silent** for an out-of-range role, and the engine was printing an admission where the bytes print nothing. Now it returns a new `U2_TRANSACT_SILENT` so a test can see the path without text appearing, and the `engine_gap` helper went with its last call site rather than sitting unused behind a `-Werror` build. Two checks in `commerce_check.c` pin it. The wider lesson: an `[engine]` marker is only honest where the original is unknown; where the bytes show silence, an admission is a divergence wearing the costume of honesty. |
| `space_view_gate.py` had no red control | `docs/outstanding.md` § 2 (this file) | **Closed 2026-11-15.** Two things had happened since the row was written and neither was recorded here. Leg 7 was added 2026-11-11, after the owner reported the same symptom a second time: it runs the SDL renderer headlessly, captures the frame the engine actually uploads, and requires the hyperwarp prompt to add ink over the orbit frame — the only leg that looks at pixels rather than at the structure the code writes. And the leg-5 control, which existed only as a recipe in the gate's own header, now **executes**: `space_check_selftest.sh` runs the clean gate green, then runs it against a `commands.c` with the dive wait deleted and requires red, and it refuses to trust the mutant unless the line provably left the file. Selftest went from 6 controls to 8. The row was right when written; it had simply been overtaken. |
| The site-exit save the engine did not model | `docs/outstanding.md` § 1 (this file), across three rewrites | **Built 2026-11-15.** `u2_overworld_autosave_record()` in `overworld.c`, called from exactly the two paths the bytes name: the ladder exit in `site_shift()` (`asm/3823`, runtime `0x389d`) and walking off a site edge (`asm/2453` → `asm/2474`, runtime `0x2474`). Record only, no sector, Earth only. The other two callers of `site_leave()` — CAST SURFACE and CAST UP-LADDER at floor 0 — are deliberately excluded, because they land on the slot-5 target at runtime `0x32A2` and that path carries no write. Control run: stub the write out and exactly one assertion goes red. What this row claimed before it was built was wrong twice: first that the engine autosaved on nothing (it already did, in three places), then that a rocket launch does not save (it does, gated on Earth and a wilderness sector). |
| The pub's `1-BUY` and `2-TIP` | `docs/outstanding.md` § 1 (this file) | **Shipped today, 2026-11-15.** `shop_pub` in `commerce.c`, with the fifteen literals (five menu, ten barkeep) committed through the extractor as 4-tuple `run` locators, so they resolve from `asm/` with no copy of the game present — better than the shop menus, which are exe-only. The index rule turned out to be readable after all: `1-BUY` stores the constant 1 (the `rand_byte` at `asm/15E1:1847` is drawn, masked by `and $0x3` and then overwritten), and `2-TIP` charges the digit and lands on `digit + sign_of_al(rand_byte)` computed in a byte and forced to 0 at 10. `run_cp6.sh` gates all three arms of `asm/50D8` with pinned `--rng-state` values, plus a control that a digit outside 1 and 2 prints nothing and costs nothing. Two controls were run against the gate and both went red. |
| The clue seller (`U2_SHOP_CLUE`) | `docs/outstanding.md` § 1 (this file) | **Built 2026-11-15, the day it was written up as blocked on plumbing.** The plumbing was the whole blocker and it took one re-cut: `asm/19D7` split at runtime `1b6a` into three files — the menu plus the cursor-seed NUL stays, the ten clues become `asm/1B6B_clue_pool.asm` as `db` lines, and the armour/weapons/transport catalogues become `asm/1D1D_shop_menus.asm`. `check_func.py` passes on all three and `assemble_clean.sh` rebuilds 1214 functions byte-ident. `shop_clue` in `commerce.c` charges the digit x100 by placement and prints entry `digit`; `run_cp6.sh` gates digit 0 (free, still buys clue 0), 3 (300 gold) and 9 (900 gold), each clue asserted as two lines because of the embedded newline glyph, and a control that shifts the index by one was run against the gate and went red. The eleven strings are committed through the extractor and resolve from `asm/` with no game present. |
| The cleric shrine's greeting, and whether it prints the NAME | `docs/outstanding.md` § 3 (this file), `docs/needs-gameplay.md` G80 | **Closed 2026-11-15, statically.** `asm/507A` is not a one-instruction stub, it is a fragment split: `bx := 0`, then `mov 0x36(%bx),%al` / test / `call char_print` / `inc %bx` / loop across five fragments, and `DS:36` is the name field of the save record. So the shrine says `BROTHER <name>` and the engine was already right — the claim was just unproven. The fact was in the tree: `asm/0C62_death`'s header had read the same call correctly for the death announcement. Renamed to `asm/507A_print_player_name.asm` with the evidence in its header. |
| The `DS:46` writer, and with it G80's `SISTER` question | `docs/outstanding.md` § 1 and § 3 (this file) | **Closed 2026-11-15 by G80b, and this file simply had not been told.** Exactly one writer, file `0x5951`, `mov %al,0x46` with `cmp $0x4d,%al` (`'M'`) three bytes later, so `DS:46` holds the typed letter and the shrine's `cmp $0x1` can never match. `SISTER` is dead code in the original and the engine is already faithful — do not "fix" it. Evidence is in `asm/1869_cleric_shrine.asm`'s header and `docs/needs-gameplay.md` G80b. |
| Minax does nothing (role 0 skipped) | `handoff-2026-11-12.md` § 1a | **Shipped 2026-11-12.** `ai.c` runs the `asm/1040` arm with `U2_S_MINAX_CRIES` and the packed-decimal −100 on `DS:51`; 24 Minax checks in `ai_check.c`, wired into CP4 and CP5. The premise also got corrected the same day: Minax is `role 0x84`, reached through the **bit-7** branch, not `role == 0`. |
| Minax's teleport is observed but undecoded | § 1b | **Decoded and shipped.** `u2_minax_teleport()` at `overworld.c:2053`, called from the melee and cannon survive paths. It is a **swap of `mon_x` and `mon_y`** — its own inverse, which is exactly why she oscillates NE↔SW. Proven live: slot 31 went (60,3)→(3,60) on one hit. |
| Invulnerable castle creatures have no mechanism | § 1c | **Decoded and shipped.** There is no invulnerability flag: a creature parked in **slot 0** is invulnerable to melee, talk and cannon because every scan skips slot 0. `monsters.c:326`, `u2_monsters_at_attackable`, G78. The same slot-0 skip turns up in the CAST target finder. |
| The engine's ending has neither the fade nor the flight | § 1d | **Shipped.** `victory_fade` / `victory_flight` in `overworld.c`, gated by `tests/victory_end.py` with three mutation controls, running inside `run_cp4.sh`. The fade paints **random** cells (`L_005417` is the RNG) and the ending writes nothing to disk. |
| The `cmd_E` signpost arm is unbuilt, frame in hand | § 1e | **Built.** `signpost_era_label` in `overworld.c`, and G79 closed it statically: there is exactly one off-Earth signpost (Uranus, sector 60) and the dispatch is byte-determined. |
| CP8 is `todo` | `phase4-state.md` CP table | The table's status column is stale; the endgame shipped 2026-11-12. Corrected in the table. |
| CP7 is "partly informed 2026-10-21" | same table | Stale by its own body, which describes the space layer shipping 2026-11-04. `make test-cp7` gates dungeons with the CAST section on top. Corrected. |
| CP4 "T5 held" | same table | Stale two lines further down in the same file: T5 shipped with owner approval and the CP2 baseline moved 40 → 42. Corrected. |
| `'P'` does not clear the screen | § 1f | Fixed 2026-11-12, `run_play` clears before its first read. Verified in the handoff's own text; it was listed as fixed inside the "still open" section. |
| G72, the invulnerable-guard dialogue tell | `goal-spec.md` Q9 | Closed — the phenomenon is G78, and the dialogue tell does not exist in the DOS binary. |

**The process finding, which is the part worth keeping.** Nine of the ten items above were stale
in `handoff-2026-11-12.md`, which AGENTS.md names as the current entry point, and the document
that superseded it (`docs/cp8-minax-gaps.md`) is from the *same day*. The handoff was written
earlier in the session and never given the status block that several other handoffs carry. An
agent that reads the entry point first — which is the instruction — will report shipped work as
the queue, exactly as happened here. The fix applied with this document is a status block at the
head of that handoff's § 1; the rule that would prevent the class of it is that a session which
ships an item listed open in a live handoff closes it **in that handoff**, in the same commit,
rather than only in the topic doc.

---

## 8. Handover notes, 2026-11-15 session

Concrete detail a fresh agent will need and would otherwise re-derive. Ordered by how much time
each one would have cost.

### The 0x200 skew — read this before touching any address

**`asm/` filenames and `# func` ranges are RUNTIME addresses. `L_<addr>` labels are FILE
offsets.** The relation is **runtime + 0x200 = file**. An earlier revision of this note said the
filename was a file offset too, and it was wrong often enough to cost this session a wrong answer:
every caller attribution in the first pass of the `asm/22FD` investigation was misfiled because it
assumed the filename was a file offset. The proof is in the files themselves — `asm/22FD` declares
`# func 22FD..2303` and its first label is `L_0024fd`; `asm/4050_cmd_T` declares `4050..40A5` and
starts at `L_00425d`. Every file in the tree differs by exactly `0x200` between the two, so one of
them is runtime and the other is file, and `docs/phase4-state.md` names which: "the name is `MAPX`
+ two digits self-patched into the code stream (`asm/2290`, file `0x24b7`)" — `0x24b7` is inside
`asm/2290`, so the filename is the runtime one.

**And the function map is keyed the same way as the filenames.** `disasm/function-map.tsv` rows are
runtime ranges, so a caller found by scanning the executable at a *file* offset must be converted
(`- 0x200`) before you look it up. Mixing them does not error; it silently attributes every call
site to the wrong routine, which is worse than failing.

Mixing the two is also what made the clue table look unreachable for a year: the cursor operand
`0x1b6a` appears to land inside the string "SORRY, GOOD DAY", but it is the NUL at file `0x1d6a`,
one byte before entry 0. Two headers carried this error silently (`asm/16BD`, `asm/4598`'s
filename-vs-label mismatch). When a decode looks impossible, convert before concluding.

### The shared string-walker, and how text is emitted here

Two mechanisms, both confirmed from bytes this session:

* **Inline.** `call L_0051df` (str_out) prints the string sitting at its **return address** — the
  text follows the call in the stream. Confirmed twice independently.
* **Table.** `asm/155D` (body label `L_00175d`) walks a NUL-separated table: cursor in `DS:27:28`
  pointing at the NUL *before* entry 0, index in `SI`, emits entry `SI`. Used by the tavern
  (`asm/16BD`, cursor runtime `0x16bc`) and the clue seller (`asm/19D7`, cursor runtime `0x1b6a`).

Because text is emitted by falling into it, **an address-reference search will not find a table**.
`grep` for `lea` operands and you conclude the data is dead. It isn't. This is written in
`asm/16BD`'s confidence note and it still took a session to relearn.

### Three spans are string tables drawn as instructions

`asm/4C50`, `asm/16BD` (fixed 2026-10-25), and `asm/19D7`'s `1D6A..1F1C` (fixed 2026-11-15, and
the bytes are now their own file, `asm/1B6B_clue_pool.asm`). The tell is a run of `.byte 0x??
# forced-kept: gas re-encodes '...' differently` whose bytes are ASCII. **If a forced-keep run
spells words, it is text.** Assume any long forced-keep run deserves this check before being
cited as code.

### The clue seller — built, and the recipe that did it

The decode, in one place because three files now share it:

1. Menu: `TIME AND TRAVEL MADE ME WISE,` `0x0d` `PRECIOUS GOLD, A CLUE IT BUYS!HOW MUCH WILL YOU PAY *100?`
2. `read_decimal_digit` (`asm/2028`) → digit 0..9.
3. Price: `price[0] = digit`, `price[1] = 0x00` — packed decimal placement, so digit 3 is 300 and
   digit 0 is free. No multiply anywhere, which is what the menu text says.
4. `SI = digit + 1`; cursor `DS:27 = 0x1b6a`; `call asm/155D` → emits entry `digit`.
5. The ten strings live in `asm/1B6B_clue_pool.asm`, transcribed from bytes (not from eye — two
   periods were wrong on the first pass and the generator caught it).

**The re-cut recipe, since it is the same one `asm/16BD` came from and it will come up again.** Cut
the span file at the pool boundary into before / pool / after; give each piece a header; render the
pool from the executable as `db` lines split on the newline glyph the way `asm/437E` splits
`'VIEW',0x8d,'WITH MAGICAL HELM!'`; replace the glued `.byte` line at the boundary with the single
NUL it still owes, commented, because the walker's cursor points at it; then, in order:

* three rows in `disasm/function-map.tsv` — that map, not a glob, is what `assemble_clean.sh`
  assembles in, and a row with a stray trailing tab makes `check_func.py` die on field count;
* `python3 tools/check_func.py <SEG>` per segment;
* `bash tools/assemble_clean.sh` over the whole binary — this is the only real proof;
* `# body corrected:` markers on **all three** files, not just the pool. `asm_body_guard` flags a
  new file and a changed file even when the bytes never moved, because the canonical stream changed
  where the pool was re-rendered; `asm/15E1` carries the same marker for the same reason.

One trap worth naming: `check_func.py` reads `L_<hex>` tokens in **header comments** as branch
targets, so a prose mention of a label outside your own range is an error. Write bare hex in
prose.

### G81 may be upside-down — and it partly is

The chest-vs-PASSWALL persistence contradiction was framed as "the owner's rule vs the chest
code". The third possibility was written down as a hypothesis and is now measured: **the original
writes the sector back to disk**, so a mutated sector can persist. `asm/2305_sector_store.asm`
opens `AH=28h, CX=0x1000, DX=[DS:472]` — the whole 4096-byte sector — and it runs end to end in
33 harvests. It has no direct caller anywhere in the binary; it is reached only by the single `je`
in `asm/22FD_sector_store_gate.asm`, whose entire body is `if (DS:6D == 0) save()`. Earth only.
And it does not stop at the sector: under that one `AH=28h` and before a single `ret` it writes
three files — `mapxNN`, then `monxNN` (256 B from `DS:137`), then the player record (256 B from
`DS:36`). The Phase 2 map had cut the routine into four files and the second piece was named
`monster_data_load` and described as a read; see that file's history, merged 2026-11-15.

Two consequences worth more than the finding itself. First, **a gate on a store is part of the
store's path**: `asm/22FD` was read in 2026-10-29 as "not a sector store, there is no file I/O in
these seven bytes" — true, and it missed that the routine's only branch lands on one. Second, the
observation used to prove the original never writes a sector was that `mapx31`/`monx31` came back
**byte-identical** after several town entries and exits. Storing unchanged content produces exactly
that. It was never evidence of absence, and the engine's store-on-transition was deleted on the
strength of it. Whether to put it back is the owner's call — see G81 in § 3 and the 2026-11-11
stamped-write-back ruling in `docs/phase4-state.md`, because what gets written is the part that
caused the ghost-creature bug.

### The method lesson, which is the one to keep

Four items labelled "needs a live shoppe" or "needs gameplay" in this session's own docs were
closed **statically** in under an hour: the clue pool, the pub's table, the `SISTER` branch, and
the `DS:46` writer. In every case the bytes were already in the tree and the blocker was a wrong
mental model (the 0x200 skew, an address-search that cannot work, a grep over mangled text instead
of raw encodings). **Before scheduling a capture, ask what specifically the capture would tell you
that a read cannot** — and if the answer is "which byte is written", search the raw encodings for
the writer rather than the asm text, because the asm text is exactly what was wrong.

# Outstanding work, validated against the tree

**What this is.** One list of what is actually still open, produced 2026-11-15 by reading the
handoffs, then re-checking every item they call outstanding against the code, the gates and the
capture corpus. Nothing here is recalled and nothing is copied from a handoff without being
confirmed. Where a handoff said "open" and the tree says "shipped", the item is not in the list
below — it is in § 7, which is the correction record.

**What this is not.** `docs/needs-gameplay.md` is the capture worklist and stays the authority
for open *questions*; `docs/handoff-outstanding-agent-tests.md` is the index of the subset an
agent can drive without the owner, and it is fresher than anything it indexes (updated
2026-11-13). This file is about **work to build or decide**, not questions to measure, and it
names the G-row rather than restating its recipe whenever one exists.

Grades are the AGENTS.md § 5 grades. `owner` means a person has to choose or look; `agent` means
it can be driven from a shell.

---

## 1. Engine gaps the code itself names

These are the only places the engine admits it does not do something. `grep -rn "engine_gap(ctx,"
engine/src/` returns two, and the command table has no `NULL` slots left — all 26 letters are
implemented and no `UNPROVEN:` marker remains anywhere in `engine/src/`.

| item | where | what is missing | grade | who |
| --- | --- | --- | --- | --- |
| The clue seller | `commerce.c` `U2_SHOP_CLUE` | The digit-to-price mechanism is known (the digit is placed into the hundreds byte, no multiply). What is missing is a **selection rule for the clue pool** — nobody has mapped which clue a given digit buys. | inferred | agent, needs a live shoppe |
| The pub | `commerce.c` `U2_SHOP_PUB` | `1-BUY` is a constant 5 gold and is buildable. `2-TIP` drives the barkeep chatter table (`asm/155D` + `asm/16BD`), which is read but has **never been observed executing**. Half a pub is worse than none, so it refuses. | inferred | agent |
| The talking half of `cmd_T` | `commerce.c`, `"no dialogue for that role"` | The scripted arm and the `street_*` chain are wired; one role has no dialogue and says so rather than inventing one. | inferred | agent |
| The goal-spec policy rule | `docs/goal-spec.md` P8 | The engine now knows *why* something is invulnerable (slot 0, G78) but the policy layer has no way to express "stop attacking this", so a bot will attack an invulnerable guard forever. This is a bot rule, not engine behaviour. | measured | agent |

## 2. Decoded, drawn, but not yet honest in the gate

| item | where | what is missing | grade | who |
| --- | --- | --- | --- | --- |
| Hyperwarp warp field | `space_view.c:76` — `out->warping = 0;` with a comment saying so | The radial field is **drawn** and its centre, rectangle and origin are measured, but nothing selects the branch, because the **trigger and duration are undecoded**. Until they are, the animation exists and never plays. | inferred | agent, needs a capture of a hyperwarp |
| `space_view_gate.py` is weaker than it looks | `engine/tests/space_view_gate.py` | The handoff's own words: it "asserts a present call, a paint call, the prompt count and the sprite geometry — and **all of them passed while the screen was** [wrong]". It has not grown a red control since that was written. Per AGENTS.md a gate that cannot go red where it enforces is worse than no gate. | measured | agent |
| The stamp nibble in a PASSWALL-cleared cell | `commands.c` `cmd_C` slot 4 | The original restamps creature and object nibbles into the grid; the engine draws creatures from the arrays and never stamps, so a cleared cell stays cleanly zero. Named in the handler rather than left silent. It is probably not worth building, but it is a real difference and it should stay written down until someone decides so. | measured | owner to wave off, or agent to build |

## 3. Open questions that block a fidelity claim

Short list; the recipe and byte evidence live in the named G-row.

| item | blocks | grade | who |
| --- | --- | --- | --- |
| **G81** — the chest / PASSWALL contradiction | `dungeon.c` empties a chest through `terrain_put`, which writes `map_base` and therefore **persists between entries**. Owner ruled the same day that nothing in a dungeon persists between entries, which is why PASSWALL now uses `put`. Both cannot be right. The chest rule came from bytes read within one visit; nobody has emptied a chest, left, and walked back in. | inferred vs owner-ruled, unresolved | agent, headless |
| **G80** — does the cleric shrine ever say `SISTER` | `commerce.c` `sell_spell`. `asm/1869` compares `[DS:46]` to `1`; that byte holds the typed `'M'`/`'F'`, so read literally it always says BROTHER. Same capture settles whether the greeting prints the name (`asm/507A` is a one-instruction stub). | inferred | agent, headless |
| **G43** — the plane's forest rule, and W6 the plane's airborne flag | `docs/handoff-vehicle-state-machine.md` W6 is the one genuinely open item there and it is marked as needing a capture rather than a guess. | recalled | agent, needs the plane |
| **G49** — frigate cannon reach | `cmd_F`. Owner recalls a 3-space range; nothing decoded gives a range at all. | recalled | agent, needs a frigate |
| **G77** — the plane's own march-step cost | `U2_PLANE_STEP_MS` is a borrowed 103 ms floor. | inferred | agent |
| **G76** — column 40 spill or clamp | one frame at margin `0x28` with a full-width line. | inferred | agent |
| **G30** — does `Ctrl-Y` move south | blocked on tooling: `spice86_type.py` has no modifier-key support. | inferred | agent, after a tool change |
| **G58** — which picture underlies each couplet of the demonstration | the `present_poem` code fix is still open. | measured | agent |

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

- **SDL3 is not installed on this host.** `make sdl` fails with `no usable SDL3 found`, so
  `u2-sdl-static` cannot be rebuilt here, so the `engine build fresh` gate in
  `tools/check_all.sh` has been red for that reason since before the spell work and will stay
  red until SDL3 is installed or the stale binary is removed by someone who owns that choice.
  Every other gate is green.
- **Context-mode needs Node 24.15+; this host has 22.23.3.** Worked around throughout this
  session by running probes through `live_terminal_run` into `/var/tmp` files. It costs time on
  every session and nobody has written down who owns fixing it.

---

## 7. Checked and found already shipped — the corrections

Everything below was listed as outstanding in a handoff that is still named as an entry point,
and is not. Each was verified against code or a gate, not against another document.

| claimed open | where it was claimed | actual state, and how it was checked |
| --- | --- | --- |
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

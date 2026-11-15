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
engine/src/` returns one, and the command table has no `NULL` slots left — all 26 letters are
implemented and no `UNPROVEN:` marker remains anywhere in `engine/src/`.

| item | where | what is missing | grade | who |
| --- | --- | --- | --- | --- |
| Implement the clue seller (was: map it) | `commerce.c` `U2_SHOP_CLUE` | **Decoded 2026-11-15, no capture was needed.** The pool is ten NUL-separated strings at file `0x1d6b..0x1f1c`, currently drawn as fake instructions inside `asm/19D7_magic_shop_menu.asm` (see its header). The rule: `read_decimal_digit` returns 0..9, that digit is the price via `×100` byte placement (`price[0] = digit`, `price[1] = 0x00`, so digit 3 is 300 gold and digit 0 is free), `SI = digit + 1`, cursor `DS:27 = 0x1b6a`, and the shared walker `asm/155D` emits entry `digit`. Ten digits, ten entries. **Blocked on plumbing, not on knowledge:** the strings must be committed via the extractor, and `make content-check` proves a committed string is present *as data* in `asm/` — which they are not yet, because they are rendered as `.byte` forced-keeps. The fix is a re-cut of `asm/19D7` at `1D6A` into two files plus two rows in `disasm/function-map.tsv`, exactly as `asm/16BD` was re-cut from `asm/15E1`. The `db` rendering is byte-exact and already generated; the split was not attempted here because it edits the byte-exact reconstruction and needs `check_func` plus the full `assemble_clean.sh` proof behind it. | measured | agent |
| The talking half of `cmd_T` | `commerce.c`, `"no dialogue for that role"` | The scripted arm and the `street_*` chain are wired; one role has no dialogue and says so rather than inventing one. | inferred | agent |
| The talking half of `cmd_T` | `commerce.c`, `"no dialogue for that role"` | The scripted arm and the `street_*` chain are wired; one role has no dialogue and says so rather than inventing one. | inferred | agent |
| The goal-spec policy rule | `docs/goal-spec.md` P8 | The engine now knows *why* something is invulnerable (slot 0, G78) but the policy layer has no way to express "stop attacking this", so a bot will attack an invulnerable guard forever. This is a bot rule, not engine behaviour. | measured | agent |

| Gendered speech in enhanced mode | `docs/enhanced-layout-plan.md` | **Owner-ruled 2026-11-15, not built.** Enhanced mode should address the player according to their gender. This is a deliberate divergence from the original and must be treated as one: the DOS binary's only gendered line is the cleric shrine's `BROTHER`/`SISTER`, and that branch is **dead** — `DS:46` holds the typed letter `'M'`/`'F'` while the code compares against `1` (G80b, `asm/1869_cleric_shrine.asm` header). So there is no original behaviour to copy, only an intent to implement. Two consequences: it needs its own gate rather than a fidelity test, and it must not be switched on in classic mode, where matching the original means always saying `BROTHER`. Before building, find every gendered line the enhanced sources actually have — the FM Towns material in `references/` and `disasm/fmtowns-crosswalk.tsv` is where to look, because the DOS exe has exactly one and it never fires. | owner-ruled, unbuilt | agent |

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
| **G81** — the chest / PASSWALL persistence contradiction | `dungeon.c` empties a chest through `terrain_put`, which writes `map_base` and therefore **persists between entries**. Owner ruled the same day that nothing in a dungeon persists between entries, which is why PASSWALL now uses `put`. Both cannot be right. The chest rule came from bytes read within one visit; nobody has emptied a chest, left, and walked back in. **Static angle found 2026-11-15, not yet followed:** the original writes the whole sector back to disk (`asm/2305`), so in the original anything that mutates the sector CAN persist — which would make the owner's recollection the thing that is wrong, not the chest code. Read what `asm/2305` actually writes before changing either call. | inferred vs owner-ruled, unresolved | agent, static first |
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
| The pub's `1-BUY` and `2-TIP` | `docs/outstanding.md` § 1 (this file) | **Shipped today, 2026-11-15.** `shop_pub` in `commerce.c`, with the fifteen literals (five menu, ten barkeep) committed through the extractor as 4-tuple `run` locators, so they resolve from `asm/` with no copy of the game present — better than the shop menus, which are exe-only. The index rule turned out to be readable after all: `1-BUY` stores the constant 1 (the `rand_byte` at `asm/15E1:1847` is drawn, masked by `and $0x3` and then overwritten), and `2-TIP` charges the digit and lands on `digit + sign_of_al(rand_byte)` computed in a byte and forced to 0 at 10. `run_cp6.sh` gates all three arms of `asm/50D8` with pinned `--rng-state` values, plus a control that a digit outside 1 and 2 prints nothing and costs nothing. Two controls were run against the gate and both went red. |
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

`lea`/`mov` **operands inside asm bodies are runtime addresses**. `L_<addr>` **labels and `asm/`
filenames are file offsets**. The relation is **runtime + 0x200 = file**. Mixing them is what made
the clue table look unreachable for a year: the cursor operand `0x1b6a` appears to land inside the
string "SORRY, GOOD DAY", but it is the NUL at file `0x1d6a`, one byte before entry 0. Two headers
carried this error silently (`asm/16BD`, `asm/4598`'s filename-vs-label mismatch). When a decode
looks impossible, convert before concluding.

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

`asm/4C50`, `asm/16BD` (fixed 2026-10-25), and `asm/19D7`'s `1D6A..1F1C` (fixed in header, bytes
not yet re-cut). The tell is a run of `.byte 0x??  # forced-kept: gas re-encodes '...' differently`
whose bytes are ASCII. **If a forced-keep run spells words, it is text.** Assume any long
forced-keep run deserves this check before being cited as code.

### The clue seller — complete spec, ready to build

1. Menu: `TIME AND TRAVEL MADE ME WISE,` `0x0d` `PRECIOUS GOLD, A CLUE IT BUYS!HOW MUCH WILL YOU PAY *100?`
2. `read_decimal_digit` (`asm/2028`) → digit 0..9.
3. Price: `price[0] = digit`, `price[1] = 0x00` — packed decimal placement, so digit 3 is 300 and
   digit 0 is free. No multiply anywhere, which is what the menu text says.
4. `SI = digit + 1`; cursor `DS:27 = 0x1b6a`; `call asm/155D` → emits entry `digit`.
5. The ten strings are in `asm/19D7_magic_shop_menu.asm`'s header, transcribed from bytes (not
   from eye — two periods were wrong on the first pass and the generator caught it).

**The blocker is plumbing.** To commit the strings they go through the extractor, and
`make content-check` proves a committed string is present *as data* in `asm/`. They are not data
yet — they are `.byte` forced-keeps. So: re-cut `asm/19D7` at `1D6A` into two files, add two rows
to `disasm/function-map.tsv` (that map, not a glob, is what `assemble_clean.sh` builds in), give
the new file a `# body corrected:` marker naming the evidence, then add the extractor entries and
build `U2_SHOP_CLUE`. `asm/16BD` is the worked precedent, split from `asm/15E1`.

### G81 may be upside-down

The chest-vs-PASSWALL persistence contradiction was framed as "the owner's rule vs the chest
code". There is a third possibility nobody wrote down: **the original writes the whole sector back
to disk** (`asm/2305`), so in the original a mutated sector *can* persist, and the recollection is
what is wrong. Read what `asm/2305` actually writes before changing either call — the answer may
make both current behaviours correct and the rule the thing that needs the exception.

### The method lesson, which is the one to keep

Four items labelled "needs a live shoppe" or "needs gameplay" in this session's own docs were
closed **statically** in under an hour: the clue pool, the pub's table, the `SISTER` branch, and
the `DS:46` writer. In every case the bytes were already in the tree and the blocker was a wrong
mental model (the 0x200 skew, an address-search that cannot work, a grep over mangled text instead
of raw encodings). **Before scheduling a capture, ask what specifically the capture would tell you
that a read cannot** — and if the answer is "which byte is written", search the raw encodings for
the writer rather than the asm text, because the asm text is exactly what was wrong.

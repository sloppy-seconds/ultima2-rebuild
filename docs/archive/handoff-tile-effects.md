# Handoff — tile effects: implement swamp damage, and the tile-effect table

**Status: executed 2026-11-02, the day it was written.** The swamp cost is implemented at
the step site in `engine/src/game/overworld.c` (`u2_swamp_damage`, silent, BCD, underflow
death), the force-field rung gained the missing `[DS:4A] == 0` gate, and both are gated by
**CP5 E1** (`engine/tests/swamp_damage.py`, eight cases, wired into `run_cp5.sh`) including
all three owner controls. Both gate controls were run red first: a 6-HP charge turns
`enter` red, and firing the ladder on the tile you stand on instead of the tile you enter
turns five of eight red. Items 1–6 of § 6 are done; the one genuinely open thread is new:
the bytes put the ladder ahead of the verdict, so a refused mounted step onto a swamp may
also pay — recorded as `docs/needs-gameplay.md` G55, not guessed. Two annotations in this
document were corrected on the way through: the `jb`/`jmp` branch map in § 1 (the two arms
were the other way round; the net fact never changed) and the reading of the `[DS:4A] == 0`
gate — it is the sector's ones digit, the site kind, so "world map" here means the outdoor
wilderness sectors (ones digit 0), and the effects fire on any of them, never inside a
village, town, tower or dungeon. Everything below is graded; `observed` means a byte or a
live memory value, `measured` means derived by a named tool, `owner` means Paul played it.

> **Read this first, because it cost a whole session:** there are **two** numbers per
> tile and they are constantly confused. A **map byte** is the raw byte in `mapxNN`.
> A **mode** is `map_byte >> 1`. The passability ladder and every tile effect test the
> **mode**. `asm/5B71_mode_table_copy.asm` already recorded the resolution in its own
> header: `0x5C >> 1 == 0x2e` "explains the mode without it". When you write or read a
> rule, say which one you mean.

## 0. What is being asked for

1. Implement the **swamp** damage rule in the engine (below, fully specified).
2. Record the **tile-effect table** so it stops being re-derived.
3. Fix the naming ambiguity in `engine/src/game/overworld.c` that made a previous
   session believe the engine called the damaging tile "grass". It does not — it
   calls a *different* tile grass, and never names the damaging one.

## 1. The mechanism, end to end

All of it is in the `asm/2423` passability span, which the bulk wave cut into
fragments (`2423`, `242D`, `2434`, `243B`, `243E`, `2453`, `245A`, `2474`, `2488`,
`248B`, `2495`, `24B1`, `24B5`). Read the span, not a fragment.

```
asm/2423  L_002623  mov [DS:29],al          ; save the mode the walk arm passed in
                  mov al,[DS:4A]
                  cmp al,0x00
                  je  L_00268b              ; on the world map -> straight to the effects
asm/248B  L_00268b  mov al,[DS:29]
                  and al,0x7F
                  cmp al,0x02               ; mode 2 == map byte 0x04
                  jne L_0026b1              ; not swamp -> next effect
asm/2495  L_002695  stc / mov al,[DS:52] / cmc / sbb al,0x05 / das / cmc
                      mov [DS:52],al        ; HP low BCD byte
                  mov al,[DS:51] / cmc / sbb al,0x00 / das / cmc
                      mov [DS:51],al        ; HP high BCD byte
                  jb  L_0026b1              ; no borrow (the cmc inverts) -> next rung, survive
                  jmp L_000e62              ; borrow out -> death tail (asm/0C62)
                  ; (corrected 2026-11-02: an earlier draft of this block had the two arms
                  ; swapped. The trailing cmc after each das re-exposes the borrow inverted,
                  ; so jb is the survival path. Net fact unchanged: underflow is death.)
asm/24B1  L_0026b1  cmp al,0x2E             ; mode 0x2E == map byte 0x5C
                  jne L_002720              ; not the force field -> done
asm/24B5          mov al,[DS:D6]            ; ring counter
                  cmp al,0x00
                  je  L_0026E0              ; no ring -> full damage
                  str_out "RING PROTECTS FROM FIELD!"
          L_0026E0  str_out "FIELD CAUSES 1000 DAMAGE!"
                  ... sbb al,0x10 on [DS:51] ; 0x10 in the HIGH byte == 1000
```

The mode arrives from the walk arm: `asm/5B71` copies `DS:2CB -> DS:15` and
`DS:2DE -> DS:18`, and the four walk arms pass `DS:15` or `DS:18` as AL. `DS:2CB`
and `DS:2DE` have **no direct writers** — see the open question in § 5.

## 2. The tile-effect table

Counts are `measured`: every `mapxNN` byte across all 33 sector files, first 4096
bytes each. Passability is the engine's existing `foot_ladder`, which is correct.
The two damage rows are what is missing.

| map byte | mode | tiles | on foot | effect on **entry** | message |
|---|---|---|---|---|---|
| `0x00` | `0x00` | 36283 | **reject** | open sea | `--INVALID MOVE!` |
| `0x04` | `0x02` | 1571 | accept | **−5 HP** | **none** |
| `0x08` | `0x04` | 37541 | accept | none — this is **grass** | — |
| `0x0C` | `0x06` | 14181 | accept | none | — |
| `0x10` | `0x08` | 4971 | **reject** | mountain | `--INVALID MOVE!` |
| `0x44` | `0x22` | 12 | accept | horse tile | — |
| `0x48` | `0x24` | 14 | accept | ship tile | — |
| `0x4C` | `0x26` | 17 | accept | plane tile | — |
| `0x50` | `0x28` | 4 | accept | rocket tile | — |
| `0x5C` | `0x2E` | 76 | accept | **−1000 HP**, unless `[DS:D6]` ring protects | `FIELD CAUSES 1000 DAMAGE!` / `RING PROTECTS FROM FIELD!` |
| `0x70` | `0x38` | 12691 | accept | town paving | — |
| any with mode `>= 0x16` | — | — | **reject** | the threshold rule: sign letters, building walls, closed door | `--INVALID MOVE!` |

**There are exactly two HP-costing tiles in the whole walk span.** That is a
`measured` claim: a scan of every `asm/` file whose runtime span falls in
`0x2423..0x2700` for `cmp $imm,%al` and `sbb/sub $imm,%al` returned only mode `0x02`
(−5) and mode `0x2E` (−1000). It agrees with the owner's memory: force field and
swamp, nothing else.

## 3. The rule to implement

> On the world map (`[DS:4A] == 0` — the sector's ones digit, i.e. an outdoor wilderness
> sector), a step **onto** a tile whose **map byte is `0x04`** costs **5 HP**, packed
> decimal, borrow propagated into the high byte. Underflow is death, via the existing
> tail. **No message.**

Evidence for each clause:

- **onto, not standing** — `owner`, three-way control: PASS while standing on swamp
  costs nothing; safe -> swamp costs 5; swamp -> safe costs nothing.
- **exactly 5** — `observed`: HP read live at `DS:51/52` across nine consecutive
  steps went 0370, 0365, 0360, 0355, 0350, 0345, 0340, 0335, 0330. Every delta −5.
  Reproduced earlier at 0400 -> 0385 over three steps.
- **packed decimal** — `observed`: the bytes are BCD pairs, and the asm uses the
  house `stc; cmc; sbb; das; cmc` idiom.
- **silent** — `observed`: the message log during the walk showed only `PASS`. The
  original prints nothing. Per AGENTS § 3 the engine stays silent **and says so in a
  comment at the mutation site**, citing `asm/2495`.
- **world map only** — `observed`: the branch into the effects is gated on
  `[DS:4A] == 0`.

Engine pieces that already exist and should be reused, not reinvented:

- `bcd_sbb()` in `overworld.c` — the same helper `food_spend()` uses, and
  `food_spend` is the precedent for a deliberately silent BCD drain with a citation.
- `u2_death(ctx)` — the existing underflow target.
- `foot_ladder()` already computes `(tile >> 1) & 0x7F`; the damage belongs beside
  it, not inside it, because `foot_ladder` returns a verdict and a cost is not a
  verdict.

## 4. Vehicle arms — yes, there are separate tables

`observed`, from the arm bodies. The engine models these in `u2_mode_passable()` and
that part is **already correct**; none of them carries a damage rule.

| mode | arm | rule |
|---|---|---|
| rocket | `asm/2542` | `jmp` to unconditional reject — a rocket never walks |
| ship | `asm/2619` | `cmp [DS:29],0x00` — **water only**. The `cmp $0x80` in that routine can never match, since the largest possible mode is `0xFC >> 1 = 0x7E` |
| plane | `asm/2604` | loops, refusing mode `0x06` (map `0x0C`) |
| horse | `asm/2557` | its own arm; **G40 records that a horse refuses the swamp tile** |

**Unresolved and worth checking while implementing:** G40 says the horse *refuses*
shallow water. If "shallow water" in G40 is this same map byte `0x04`, then foot
enters-and-pays and horse refuses, and the two rules must be written to agree. Read
`asm/2557` and the G40 entry in `docs/phase4-state.md` before finalising, or the
engine may end up with a horse that wades.

## 5. Open questions, named rather than guessed

1. **`DS:2CB` / `DS:2DE` / `DS:2E0` / `DS:2F3` have no direct writers** anywhere in
   the exe — `asm/5B71_mode_table_copy.asm` says so itself and points at
   `docs/passability-findings.md` F3. They are filled by an indirect store. The
   Apple II crosswalk closed the watchpoint ticket as a DOS-port artefact, so this
   may be unresolvable and may not matter — but it means the *provenance* of the mode
   byte is inferred, even though the mode's *value* is confirmed by live reads.
2. **Is map byte `0x04` "swamp"?** `owner` says it looks like swamp, and 1571 tiles
   scattered in patches fits. The art index may name it. Do not promote a guessed
   name into an `asm/` filename without a second witness.
3. **Does the force field damage exist in the engine at all?** **Closed 2026-11-02: it
   did** — `u2_force_field` in `overworld.c`, reached from the step site, and separately
   `u2_combat_force_field` in `combat.c` gated by `combat_check.c`. What it did **not**
   have was the `[DS:4A] == 0` gate, and site maps carry swamp tiles, so the step-site
   call now carries the gate for both rungs and CP5 E1 guards both.
4. **NEW, opened by the re-read: does a refused step onto a swamp also cost 5?** The
   effect ladder sits at the *entry* of `asm/2423`, ahead of the food charge and ahead of
   the verdict, so a horse turned away from the shallows at `asm/2626` would pay the 5
   before the refusal. No capture has tested this; the engine implements the narrower
   accepted-step claim and says so at the call site. `docs/needs-gameplay.md` G55.

## 6. Suggested order of work

All six executed 2026-11-02, in the order listed, except that item 1 became a verification
(the force field was already implemented) and gained the sector gate it was missing.

1. Check whether the force field effect is implemented. If not, do it in the same
   change — they are one table and one code path.
2. Read `asm/2557` and G40, settle the horse interaction. **Settled: G40 was closed
   2026-10-29 by `captures/spice86/g40-horse-shallows` — the horse refuses map `0x04`,
   the same tile foot enters and pays for, and `u2_mode_passable` already implemented it.
   The two rules agree by construction: the vehicle arms reject mode `0x02`, so the only
   accepted step onto a swamp is on foot.**
3. Implement the swamp cost beside `foot_ladder`, silent, with the citation.
4. Gate it with **all three** owner controls, not just the happy path:
   PASS on swamp costs nothing; safe -> swamp costs 5; swamp -> safe costs nothing.
   A naive implementation passes only the middle one, which is exactly the one that
   would fool a reviewer.
5. Rename `asm/2495_sub_2495.asm` -> `asm/2495_swamp_damage.asm` and
   `asm/248B_sub_248b.asm` -> `asm/248B_swamp_damage_gate.asm`, writing headers that
   cite this document and the live HP readings. Per AGENTS § 3 the rename and the
   header go in the same commit as the finding.
6. Fix the `overworld.c` comment so it never again reads as if a map byte were a
   mode.

## 7. Method notes, so the next session does not repeat the cost

- **`MEMORY_WRITE` breakpoints in this Spice86 build report a plausible but wrong
  IP.** Two attempts pointed at runtime `0x24b1`, which the byte-proven tree shows is
  inside `mov dx,[DS:472]` — a read. The static grep that found the truth in one
  shot was: search `asm/` for `sbb $0x5,%al` followed by `mov %al,0x52`.
- **The file byte offset in `ultimaii.exe` equals the runtime address; the `asm/`
  filename is runtime minus `0x200`.** Docs in this repo use "file" loosely for
  both, and that cost several wrong claims in one session.
- **The answer was already in the tree.** `asm/5B71`'s header contained
  `0x5C >> 1 == 0x2e`, and `docs/passability-findings.md` F14 already stated the
  gate variable is `map_byte >> 1`. Two live breakpoint attempts were spent
  rediscovering what a header already said. Look before measuring.

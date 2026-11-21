# CP5 kickoff — the wiring that makes a fight reachable and visible

Written 2026-10-23 after the combat-mode audit, the live fight captures and G24/G25.
Reads: `AGENTS.md`, `docs/phase4-state.md` § CP5, `docs/archive/handoff-cp5-2026-10-23.md`,
`docs/spice86-live-findings.md` § 14, § 15, § 16, § 17.

**Acceptance bar (owner's words):** *"lets get to a rendered fight in the game"*. Not a green
suite. The `phase4-plan.md` gate is "owner wins a fight".

**Comment standard (standing directive):** every new function cites the `asm/` file behind
each behaviour, or says plainly that it is a guess.

---

## 1. What already exists — do not rebuild any of this

| piece | where | state |
| --- | --- | --- |
| C1/C2/C4/C5/C6 mechanics | `engine/src/game/combat.c` | implemented, spec-covered, 4066 checks green |
| The spec and its controls | `engine/tests/combat_check.c`, `run_cp5.sh` | discriminates; controls and a zero-check self-test run every `make test` |
| Sector I/O for `mapx` **and a buffer for** `monx` | `engine/src/core/world.h` — `u2_world.mon[]`, `mon_loaded`, `mon_dirty`, `U2_MON_TILES 256 /* CX at asm/22D5 */` | **Half there.** The buffer, the dirty flag and the store path existed; the loader did **not** work — `u2_world_load` iterated one hardcoded map-only prefix list, so `is_map == 0` read a *map* file into the monster buffer. Fixed in W1: separate `k_map_prefixes` / `k_mon_prefixes` (`monx/MONX/mong/MONG`, the Galaxy pack ships `updated/MONG10..`). Caught by `monsters_check.c`, which simply could not load `monx31`. |
| CMD window, status band, prompt | `engine/src/game/status.c`, `overworld.c` | CP4; the fight UI is a line in this window, so rendering cost is near zero |
| Save gates incl. the Earth gate | `u2_save_policy` in `overworld.h` | all three gates lifted already, `DS:6D` included |
| Paralysis counters | `state.h` `paral_legs/arms/sleep` at `0x26B/26C/26D` | present, with `_Static_assert` on the offsets |

**So CP5's remaining work is interpretation and wiring, not I/O plumbing and not
mechanics** — with the one exception in the table above, which W1 fixed.

**W1 is done (2026-10-23).** `state.h` carries the six arrays at their proven offsets, each
pinned by `_Static_assert` (`mon_x 0x137`, `mon_y 0x157`, `mon_hp 0x177`, `mon_type 0x197`,
`mon_kind 0x1B7`, `mon_role 0x1D7`), plus `counter_26A` carved out of the reserved block with
a `NAME UNKNOWN` comment rather than an invented one. `engine/src/game/monsters.c` does the
monx↔DS copy and the cell lookups; `engine/tests/monsters_check.c` runs in `run_cp5.sh` and
checks the mapping against `original/monx31` using values read out of the live emulator —
all six sampled slots match exactly, and a control asserts the six columns are
distinguishable so a swapped offset cannot pass silently.

---

## 2. The evidence the wiring must honour

Each line is a finding with its citation; the detail lives in the findings doc, not here.

1. **Combat entry is `A` + a direction**, and the fight renders as one line of the world
   view's `CMD:` log — `ATTACK--EAST--HIT!!!`, `KILLED--GOLD+1--EXP.+02`. No battle screen,
   no mode. `asm/2B8F_cmd_A`, `asm/2BEC_combat_hit_msg`, `asm/2E3D_combat_attack_flow`,
   capture `cp5-fight`.
2. **A creature blocks movement**: stepping onto one prints `EAST--INVALID MOVE!` over a
   walkable tile. C3's paralysis rejection joins this existing path.
3. **The counter tick is time, not moves** — `asm/0C33..0C53`, observed decaying while the
   player stood still, in a 3000-turn recording and live. It ticks a **fourth, unnamed
   counter at `DS:26A`**.
4. **Status is not persisted and cannot be** — poked `07 09 08 08`, saved with `Q`, absent
   from the 384-byte file, `00 00 00 00` on reload. G24 closed.
5. **Hostility is provoked.** Landing a blow on anything in a town flips **every guard in
   the sector** to `[DI+0x1D7] == 0x01`; a missed swing flips nothing; 65 s idle flips
   nothing. Alerted guards converge and take HP. § 16, capture `cp5-g25-aggro`.
6. **The alarm cannot survive a save either** — `DS:1D7` maps to file `0x1A1`, past the end.
7. **Death zeroes four on-screen fields** and prints `<NAME> IS DEAD!` — `asm/0C62`
   (`0` into `DS:51..59`), confirmed on screen.
8. **Low HP draws `H.P.` in red** — a renderer state CP4 never produced.
9. **BCD fields wrap** — GOLD `9999 -> 0010`, EXP `9999 -> 0001` seen live; this is the B1
   ticket's `bcd_adc` landing with its first caller.

---

## 3. Work items, in build order

**W1 — creature state.** Add the six 32-slot arrays to `state.h` at `DS:137` x, `DS:157` y,
`DS:177` hp, `DS:197` type, `DS:1B7` kind, `DS:1D7` role, each with `_Static_assert` like the
paralysis fields. New `engine/src/game/monsters.c` maps `world.mon[]` offsets
`0..31/32..63/64..95/96..127/128..159/160..191` onto them at sector load, per
`docs/monster-spawn.md` § 6 (matched 32/32 live on type, hp and the role array).
Cite: `asm/22D5` (the 256-byte read), `docs/monster-spawn.md` § 6.
Test: load sector 31's `monx`, assert the arrays equal the values read live from the
emulator in this session (`captures/spice86/cp5-g25-aggro/roles-at-load.txt` is a
ready-made expected output).

> **Side finding to fold in:** `docs/monster-spawn.md` § 7 asks how the sixth array arrives
> when `asm/22D5` reads `CX=0x100` = 256 bytes, "which covers arrays 1–5 and not the sixth at
> `160..191`". Six arrays of 32 bytes is 192 bytes; a 256-byte read covers all six with 64 to
> spare. The premise looks like arithmetic slip, not a mystery. Verify against `asm/22D5` and
> close it.

**W2 — draw the creatures.** Art index is `(cell)>>2` from the **sector grid cell**, not the
monster type (`docs/art-index.md`, 256/256 on the disagreeing sample). Terminal mode needs a
glyph decision (see § 4 Q1).

**W3 — `cmd_A`.** Direction from the same source the original uses; find the target by
scanning the creature arrays for the adjacent cell; roll; on hit subtract from `[i].hp`
(`asm/2E3D`: `sbb` the damage byte, on borrow clear `x/y/hp/type/kind` — the record is
cleared, not flagged); print `MISS` / `--HIT!!!` / `KILLED--GOLD+n--EXP.+m`. Gold and exp
adds use the new `bcd_adc` (B1 ticket: mirror `bcd_sbb`, discard the carry, cite
`asm/2DC1`/`asm/2DEE`, pin the measured `9999 + 15 -> 14`).

**W4 — occupied tile rejects the move.** Same reject path as mountains and paralysis
(`asm/2423` region, `--INVALID MOVE!`). **Site not yet located at byte level** — implement
against the observation and say so in the comment until the branch is found.

**W5 — the alarm.** On a landed hit: set `role = 0x01` on the target, and on every creature
of the guard class in the loaded sector. **Site not located**; the behaviour is measured
(§ 16) and the capture is the citation. Keep it in one function so a later byte-level finding
is a one-place change.

**W6 — alerted creatures act.** Pursue when alerted, strike when adjacent. Observed
(convergence + HP `9999 -> 9796`), **not located at byte level**. Keep the pursuit rule
behind one function; § 16's open question (does `0x01` cause attacking, or record it) is
still open and this is where it gets settled.

**W6b — the creature's own turn, and it is usually a no-op.** Measured at pass boundaries in
§ 18: only **7% of (creature, turn) pairs produced any movement** (21 of 288), movers shift
**2 cells** not 1, and 26 of 32 slots never moved — including the Fighter standing adjacent
and every Merchant. So the loop is `for each creature: take a turn`, where a turn is mostly
nothing, **not** `for each creature: step`. Implementing the intuitive version would churn
the world ~6x faster than the original and empty the streets of the stillness that makes the
town look inhabited. **Both numbers come from one 10-boundary window in one sector** —
re-measure in a wilderness sector before either is hard-coded, and say which capture the
constant came from.

**W7 — the turn timer: already implemented, do not rebuild it.** `overworld.c` performs the
idle auto-PASS today: it echoes `PASS`, charges `U2_FOOD_PER_PASS`, and its threshold is
`U2_AUTO_PASS_POLLS 256` — a **poll count**, because the bytes say so (`incb DS:21; jne
<loop>` at runtime `0x08D6`; no `INT 1Ah` in the corpus, and the only BIOS-tick read is the
startup RNG seed). The enhanced-mode knob the owner asked for also exists:
`--auto-pass-polls`, with `-1` = pure turn-based. **Do not replace this with a millisecond
budget** — the original's turn length varies with machine speed, and a wall-clock deadline
would be a faithfulness regression dressed up as a fix. See `docs/spice86-live-findings.md`
§ 18 addendum.

What is genuinely left in W7, and both are blocked:

* **The half-action expiry** (`ATTACK--PASS`) needs a command that can be half-typed, so it
  lands with W3, not before it.
* **The status tick is parked.** The counters do **not** tick once per turn — measured: 15
  decrements in ~0.7 s while exactly one PASS elapsed. `asm/0C33..0C53`'s caller is still
  undiscovered, so the cadence is unknown and no decrement may be wired until it is found.
  That search is now a CP5 prerequisite item, not an implementation detail.

**W8 — death.** Zero `DS:51..59`, print `<NAME> IS DEAD!` into the CMD window. Cite
`asm/0C62`.

**W9 — the gate.** A scripted headless fight (`engine/tests/run_cp5.sh` extension or a
`play_to` script): spawn, approach, `A`, hit, kill, assert the log lines and the BCD wrap.
Then the owner plays one and wins.

---

## 4. Renderer ruling and the one remaining decision

**Q1 — RESOLVED by the owner, 2026-10-23: pause terminal mode, build for SDL.** Terminal is
explicitly deferred to its own later project, so W2 draws creatures through the framebuffer
path only, and nothing in CP5 should grow a terminal-mode branch. The existing terminal
renderer stays as it is and is no longer the acceptance surface; the CP5 gate is played in
the SDL build (see `docs/phase4-state.md` § TODO: headless podman container to play the SDL
build over noVNC, which is how a human without a local display plays it).

**Q2 — RESOLVED, and the resolution is better than the proposal.** The owner's model — *"each
turn has a timeout, so if I don't perform an action before that timeout it PASS, or if I only
do half an action it does `ATTACK--PASS`"* — was tested the same day and is **right on both
halves**: with no input at all the log filled with `CMD: PASS`, and pressing `A` alone
produced `ATTACK--PASS`. Evidence: `docs/spice86-live-findings.md` § 18, capture
`captures/spice86/cp5-turn-timeout/`.

**But the mechanism is a poll counter, not a clock** (`incb DS:21; jne` at runtime `0x08D6`),
and the engine already models it that way — so the determinism worry dissolves instead of
being traded away: a turn is a loop count, which is reproducible by construction, and the
~7.5 s figure is what that count happens to cost at `--Cycles 3000`. A prior session measured
~7.6 s at the same setting and deliberately refused to encode it; § 18 reproduces the number
and upholds that refusal.

**Enhanced mode (CP11) keeps the knob the owner asked for**, and it already exists:
`--auto-pass-polls`, where `-1` is "no timeout", i.e. a true turn-based game. It stays an
enhanced-mode surface, never a default.

**Also open, and it blocks nothing:** the art question under SDL. Creature art index is
`cell >> 2` from the map cell (`docs/art-index.md`), and the engine only has cleanroom
tiles — so creatures will draw as substitute art, which is the intended arrangement, but
whoever supplies creature art must go through the cleanroom procedure and must not read the
U1 Unity project or the bestiary cache.

---

## 5. Standing rules for this checkpoint

* No stubs. A stub for "resist paralysis" encodes boolean immunity and looks finished.
* Anything guessed says *guess* in the comment, with what would settle it.
* `make -C engine test` and `bash tools/assemble_clean.sh` green at every commit that can
  break something.
* If a behaviour turns out to need a battle screen or a combat-mode flag, **stop and ask** —
  the owner ruled there is no leaves-combat state, and that is now screen-proven.

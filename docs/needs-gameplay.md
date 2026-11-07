# Needs gameplay — capture worklist

**Status: live document.** Opened 2026-10-22. Purpose: a single tick-off list of
everything still blocked on someone playing the game, with an honest per-item
verdict on **whether the agent can do it unattended or whether it needs the owner**.

The reason this file exists is that "needs gameplay" had quietly become a synonym
for "needs the owner", and it is mostly neither. Most of these need a *driven*
session, not a *human* one. The distinction decides whether an item is blocked on
a diary appointment or on an afternoon of scripting.

> **Do not duplicate specs here.** Register dumps, breakpoints and success criteria
> live in `docs/passability-findings.md § Open` and the backlog items in
> `docs/phase4-state.md`. This file only adds the who/can-I-do dimension and the
> tick state. Two hand-maintained copies always drift (`AGENTS.md` § 3).

---

## Tick convention

| mark | meaning |
| --- | --- |
| `[ ]` | open, nothing captured |
| `[~]` | partial — some readings landed, a named gap remains |
| `[x]` | closed by a named capture under `captures/spice86/` |
| `[!]` | blocked on tooling, not on gameplay — fix the tool first |
| `[H]` | genuinely needs the owner; agent attempts would produce junk |

**An item is only `[x]` if a capture directory is named.** "I think it works this
way" is not a capture. Closed items move to § Closed with the harvest name and the
commit that recorded the finding.

---

## Standing hazards — read before driving anything

These are all paid-for. Every one of them produced a wrong answer at some point.

1. **A send is not a move.** `xdotool` returning 0 means dispatched, not landed.
   The first passability sweep concluded "impassable" from positions that never
   changed because keystrokes were lost during a read-pause — nine capture
   directories (`sweep00`..`sweep28`) of confidently wrong data, invalidated only
   after F13 noticed every measured tile was a shop/sign letter. **Never** infer
   terrain from a non-move without proving the key arrived. See item **G0**.
   **Mechanism found 2026-10-28:** `key_poll` (`asm/4F4D..4FDE`) deliberately drains
   the BIOS keyboard buffer after each read and throws away everything after the first
   key, so a burst of injected keys loses all but one no matter how the transport
   behaves. One key, then wait for the state to move. See
   `docs/phase4-implementation.md` § "Re-verified hop by hop".
2. **HTTP memory reads pause the CPU.** Reads return correct data, and the
   emulator reports `isPaused: false` shortly after, but a read is not a free peek.
   Sequence deliberately when timing matters.
3. **The multi-key timing window is unmeasured on this host.** `~2 ms to ~1500 ms`
   is marked `sibling:hypothesis`; the 30 ms default "must be re-measured here
   before it is trusted".
4. **Never send a confirm key blind.** `Y` is the YELL command and takes a text
   argument; a stray `Y` opens a prompt and every later key is eaten as yell text.
5. **Re-read the grid every sample.** A probe that caches the 4 KB grid at startup
   produces stale tile labels, because the grid is a live object store (T5/T6).
   This fabricated a vehicle depot that did not exist.
6. **Radix.** Monster types in the findings are **decimal**. A `%02d` format against
   hex-named PNGs produced a committed-and-then-corrected wrong plane tile.
7. **Never run in place.** The game rewrites its own `player` file. Always
   `tools/spice86_start.sh`, which copies the pristine pack into a fresh timestamped
   dir under `tmp/`.
8. **Ask before capturing, and never block for minutes.** Run detached and poll.

---

## G0 — prerequisite: harden the probe `[!]`

**Who: agent. Confidence: high. Blocks: G2, G4, G5, G6.**

> **Finding 2026-10-22, measured — the obvious way to do G0 does not work, and two
> of the three candidate signals are already known-bad.** This section was written
> assuming a reject could be counted from the execution dump. It cannot.

**The reject site is real.** `asm/0A02_invalid_move_msg` prints `--INVALID MOVE!`,
and `tools/cfg_printed.py` confirms it genuinely printed in `mountain-walk`,
`play-test-live`, `create-test-live` and `sweep00`. So the address is right.

**But `ExecutedInstructions` is a deduplicated address set, not a trace.** In
`mountain-walk`, `play-test-live` and `create-test-live` the offset `0A02` appears
**exactly once each**, while the dumps hold 1,564–2,477 entries with only ~6
repeats total (offsets 0, 4, 12, 16 — not real re-executions). So it can answer
"did a reject ever happen in this session" and **cannot** answer "how many, and
between which steps". Per-step classification from existing dumps is impossible,
which is exactly what a passability probe needs.

**The second candidate is explicitly forbidden.** `DS:20` was documented as an
invalid-move counter, and `engine/src/core/state.h` now says: *"DISPUTED: docs
called this an invalid-move counter, but the harvest shows it decremented in the
walk loop and overwritten at runtime 0x2BC7. Treat as scratch; do not use as a
reject signal."* `tools/turn_recorder.py` was nevertheless labelling turns
`reject` from `DS:20` — it has been corrected to emit `stalled` (no move, no food
spent, **cause unresolved**) and to name the byte `scratch_20`. Any earlier
`rejected: true` in an existing `index.jsonl` is therefore not evidence of a
reject.

**Viable live signals:**

1. **GDB `breakCsIp` trap on the reject site — BUILT AND MECHANISM-VALIDATED
   2026-10-22.** `tools/reject_trap.py`. Proven to fire: arming on the address the
   CPU was already executing produced a stop, with no gameplay involved. It speaks
   the remote protocol directly rather than driving `gdb(1)`, because gdb with a
   piped stdin emits nothing at all and `gdb -batch` cannot arm-and-wait (the
   target is running on attach, so `continue` is refused).
2. **Frame-diff on the message region** via `tools/vga_ring.py`. Still available as
   a fallback; heavier, and it must separate the reject message from other text
   changing in the same region.

**One trap for whoever uses this, and it is the dangerous kind:**
`monitor breakCsIp` parses its arguments as **hex**, while `/api/status` reports
**decimal**. Passing the API's decimal values silently arms a breakpoint on an
address that can never execute, and every later "no reject observed" then means
nothing. `tools/reject_trap.py` converts in one asserted place
(`cs_ip_from_status`) for exactly that reason.

**What still keeps G0 open:** the trap is proven as a mechanism, not yet against a
real rejected step.

**CLOSED for the trap itself on 2026-10-23 — it has now fired on a real rejected step.**
`tools/bp_read.py` armed `breakCsIp 17D 0A02` on a live headless run (sector 31, player
seeded to (50,21), a direction with a wall in it), then a movement key was sent. Result:
`STOP at 017D:0A02 (paused=True)` with `DS:00/01` still `(50,21)` — the step was rejected
and the reject print site was caught executing, not inferred. The harvest corroborates it:
`captures/spice86/g0-reject/` has `0A02` in `ExecutedInstructions` and `17B7`/`20AB`
absent, which is the right shape for a run that walked into a wall and did nothing else.

Two things this does **not** close. (a) The negative half — confirming the trap stays quiet
on a *successful* step — was not run; the tool was pointed at a blocked direction only. (b)
Wiring the per-step outcome into `tools/turn_recorder.py` and the probe `[!]` is still to
do, and that is the part the passability sweep actually needs. Requirements unchanged:

* after each step require **one of**: position changed, or a counted reject; if
  neither, treat it as a **dropped key** and retry — never as terrain;
* record which outcome occurred, per step, in the harvest;
* assert the sample actually contains the tile class being claimed. The sweep
  measured fifteen tiles and all fifteen were letters `B`–`P`; a per-run histogram
  of tile classes would have caught that instantly.

**Staging is already solved:** `tools/u2_mkprobe.py` generates a probe sector that
puts every terrain byte next to walkable ground, and `spice86_start.sh --seed-map`
installs it pre-launch. A saved character (`AIRTEST`, seen in `tmp/*/player`) can be
seeded with `--player`, so character creation is not an obstacle either.

---

## The items

| id | item | who | confidence | blocks |
| --- | --- | --- | --- | --- |
| G1 | ~~art index: `(map_byte>>1)&0xFE` vs `map_byte>>2`~~ **CLOSED 2026-10-23 by `captures/spice86/art-index/`.** The transform was never the live question -- `view.h`'s two-step composes to `>>2` for all 256 byte values. The live question was **which field**, and the answer is **the sector grid cell, not the monster `type`**: on the only disagreeing sample in the corpus (sector 00 slot 6, type `0x34`, cell `0x38`) the drawn tile matches art `0x0E` = `cell>>2` at **256/256 pixels** and art `0x0D` = `type>>2` at 171/256. Geometry independently confirmed on 18 further slots at 256/256. Full write-up: `docs/art-index.md` | agent | **done** | renderer correctness |
| G2 | does a blocked step cost 19 food | agent | **high** | turn economy |
| G3 | ~~attribute the indirect writers of `DS:2CB`/`DS:2DE`~~ **CLOSED 2026-10-28d as a DOS-port artefact, not by capture.** The reason this item existed was C5/F3: the tile-dependence of passability had to enter through those four bytes, because nothing else wrote them. That premise is gone — `0x5C >> 1 == 0x2e` and `0x5C` is the force field (`captures/spice86/g29-field`), so the mode is explained without them. And the cross-ISA check came back silent: the Apple build has no equivalent indirection at all (no copy-block shape, no tile-indexed shift-and-read, and its step bytes are written only by the routines that compute them) — see `asm/5B71_mode_table_copy`. **What is NOT answered:** who actually writes those four bytes on the DOS side. Nobody knows, and nobody needs to for the contract. Reopen only if a mode discrepancy appears that the `>>1` rule cannot explain. | agent | **closed, unanswered** | was C5, F3 |
| G4 | what sets mode byte `0x2e`; step onto a force field | agent | **medium** | C5, T8 |
| G5 | which tile is the mountain (rejection test) | agent + 30 s owner look | **medium** | CP4/CP6 passability |
| G6 | single-byte counter underflow: is it reachable | agent | **medium** | census claim |
| G7 | stat cap 99 wrap, observed rather than inferred | agent | **medium** | B1/B2 confidence |
| G8 | ~~identify `DS:96+i`~~ **CLOSED 2026-10-23 by `captures/spice86/armour-buy/`.** It **is** the owned-armour count table, and the index is the **menu number, 1-based**: buying CLOTH (item 1) took `DS:97` from `00` to `01` and gold from 9999 to 9987, exactly the quoted `0012`. `DS:96` itself is unused, so the six armour types live at `DS:97..9C` -- do not zero-base it. The shop was reached by the rule in `docs/commerce.md` § 5 (an NPC standing at the sign; sector 32's Merchant at (38,3) above `ARMOURY`), enumerated by `tools/shop_access.py`. Two caveats recorded in § 4b: the shop closes after one purchase so a second type was not observed, and `maxout` leaves the table empty while `DS:62` says POWER is worn | agent | **done** | census candidate, now proven |
| G9 | **~~dungeon light timer reaching 0 — does the screen darken~~ CLOSED 2026-10-29 statically, and the answer is better than the question.** The darkening is not a palette trick: `asm/6000` opens with `cmpb $0x0,0x26 ; je L_00621d` and that jump skips the depth scan, the shape dispatch and the sprite pass entirely, so with no light **nothing is drawn at all**. That is a byte in the render entry, not an inference from a timer, and `engine/tests/run_cp7.sh` now asserts it by counting ink in the map window of a dark frame (zero) against a lit one (1923 pixels, 256 distinct columns). **What is still unobserved:** the transition mid-dungeon, i.e. `TORCH BURNED OUT!` printed by a step rather than forced. The engine implements it from `asm/1151`'s `decb 0x26` / `jne` pair and the CP7 gate checks the counter ticks down, not that the burn-out frame was ever filmed. | agent | **closed statically; transition still unobserved** | CP7 |
| G10 | **~~dungeon geometry + wireframe renderer decode~~ CLOSED 2026-10-29, statically, and it never needed an owner session.** The item said "the renderer is not in the decoded tree at all (no pixel-plot path anywhere in `asm/`)" — the pixel plot was in the tree, at `asm/4B76`, filed as an unnamed bulk-wave fragment called `sub_4b76`, and the reason no pixel-plot search found it is that it does not write to `0xA0000` at all: it takes a row SEGMENT from a table at `cs:0x48DC` (0xB800 even rows, 0xBA00 odd, i.e. CGA interlace) and ORs a plane mask from `cs:0x48D0`. Once it was named, the rest followed from bytes: the grid is 16 pages of 16x16 indexed by `[DS:25]` (`asm/68C0`), the terrain nibble splits into rock/chest/down/up, `asm/6078` samples three columns to eight depths, `asm/6144` picks a shape per depth, twelve routines draw one to nine lines each out of eight-entry CS perspective tables, and the plot origin is the live `[DS:4BF]/[DS:4C1]` pair, both 16. `tools/gen_dungeon_draw.py` transcribes the twelve mechanically because the line drawer's four registers are **not** cleared between calls and a hand copy drifts. Full write-up in `docs/phase4-state.md` § CP7; engine in `engine/src/game/dungeon.c`. **What is left of the original item:** T9, the stroke table at `DS:2900` — see G53 below. | agent | **closed** | CP7, T9 |
| G11 | **~~`A`–`Z` dungeon byte decode~~ CLOSED 2026-10-29, and the framing was the obstacle.** The item assumed the letter tiles were one encoding shared with the overworld sign-boards, and asked for an owner to walk a maze to sort them out. They are not one encoding. `mapx35` uses exactly seven values, all multiples of `0x10`, and the four high-nibble bits are named by the code that tests each one: `0x80` rock (`asm/6144`'s corridor-end branch), `0x40` chest (`asm/363B`'s GET compares the cell under the player to `'@'`), `0x20` down ladder (`cmd_D`'s gate), `0x10` up ladder (`cmd_K`'s gate). The low nibble is a separate layer entirely — `asm/6078` reads the centre cell twice, once `& 0xF0` and once `& 0x07` — and holds the stamps the loader writes, which is why the shipped file understates what the grid can hold. Where the letters spell `AIR PORT` and `TORTURE CHAMBER` that is the overworld sign-board rule and it has not changed. | agent | **closed** | CP7, the sector-rulesets table |
| G12 | force-field animated render (white/blue/purple) source | **owner** | **low** | C5 cosmetics |
| G13 | reproduce `mapx30 (0,0)=0x50` | agent | **low value** | nothing |
| G14 | who writes `%di` before `0x3546` — **the ticket's own hypothesis is now the leading reading, and it came from the Apple side.** The ancestor has the same five-way dispatch (`$777B`: `LDX $4E13` then a `BEQ/DEX` chain, same order, 2112 A.D. as the fall-through), and `$4E13` has exactly ONE writer in the resident image (`$6BF8`) which computes it as `LDA $4E38 ; LSR A ; ... ; ADC #$01` — **shift right one, plus one**, the arithmetic that turns ten things into five. `$4E13` sits one byte below `$4E14`, which `asm/2199` pins to `DS:4A` = the sector ONES digit, and `tools/u2_edit.py` records DS:49/4A as the sector's two decimal digits. So: tens 0-9, `>>1`, five eras — exactly "five time periods against ten sector tens-groups". Inferred, not observed; `asm/3546_time_index_message` carries the full evidence. **What is left:** the DOS writer of the index. The DS:49 reads that do exist (asm/2290, 22D5, 2305, 26B7, 26CC, 2708, cmd_O; written by asm/26C6) are filename and sector-loading sites, not this one. A `bp_read --ip 3546 --state` dumping DI alongside DS:49 in two different sectors settles it in minutes, and that is the capture worth asking for. **CLOSED 2026-10-28d without a capture.** The writer of `%di` was found two instructions upstream at runtime `353C..3540`, and it had been hidden by a mis-decode rather than being absent: `mov 0x49,%bl ; mov %bx,%di`, so the index is the sector tens digit used **directly**. The Apple `>>1 then +1` does not carry over — the DOS world is 5x5 sectors, so five digits already are five eras, and the two builds partition the same map differently. Full table in `asm/3546_time_index_message`; the mis-decode is in `disasm/defects.md`. What remains is not a question and not blocked on anyone: the era-to-sector mapping has never been executed in a harvest, so it is byte-verified and play-unobserved. | agent | **closed** | time-period display, CP6 |
| G15 | locate towns -- **largely answered, and not the way the ticket assumed.** There is no separate site-kind byte: `DS:4A` is the ones digit of the sector number, and that digit *is* the site kind. All 15 `tlkx` dialogue files land in ones-digit 1/2/3 and none in 0/4/5, so the town sectors are known from the shipped file set alone. **What remains:** which *named* town occupies which sector, and what each shop stocks. See `docs/sector-rulesets.md` § 1 | agent | **medium** | shops, signs, CP6 |
| G16 | `0xC0` event tile: step on one and record what the event actually is (a probe threw the player y=20→57; 1311 occurrences, exactly 262 in each of `mapx25/35/45`) | agent | **medium** | tile art, CP4/CP6 |
| G17 | ~~find the HP computation~~ **done for the overworld**: the field is `[DI+0x177]` (proven by `asm/2E3D_combat_attack_flow.asm`, which subtracts damage from it and zeroes the record on borrow) and nine spawn arms store per-kind immediates that match the third-party HP **9 for 9** (`tools/monster_hp_dispatch.py`, exits non-zero on a disagreement). **Also closed:** `asm/147F_dungeon_monster_spawn.asm` is confirmed the dungeon spawner (only caller `asm/1151_dungeon_darkness`) and computes HP from the sprite, and Guard's 500 is **refuted by field width** -- HP is one byte, so the codex's 255 stands. **What is left:** the townsfolk. They have no arm at all (measured negative -- no `mov $0x60/0x64/0x68/0x6C/0xF4,%al` anywhere), and the static search is now **exhausted**: every write site of `[DI+0x177]` is enumerated (nine spawn immediates, the dungeon computation, two subtract paths, the kill clear) and none can be a town NPC spawner. The `asm/3611` lead raised on the site-kind test was retracted -- it applies damage, it does not create NPCs. So this one needs a live read of `[DI+0x177]` for a Guard or Merchant in a town, which closes the question either way. The dungeon formula is read but not validated against a live monster | agent | **high** | Merchant/Cleric/Jester/King stats; CP5 combat spec | | agent | **high** | Merchant/Cleric/Jester/King stats; CP5 combat spec |
| G18 | settle **which monster record field drives the drawn art**. The `0x40` contradiction is **closed** -- the thief is type `0xFC` with HP 32, so `0x40` is the empty sentinel and the earlier "thief is monster type 0x40" in `docs/spice86-live-findings.md` is wrong. But a second claim from the same day is now **retracted**: `[DI+0x1B7]` is NOT a sprite. A live read of all 32 slots shows it holds `0x08`/`0x0C` -- a small kind index, which is exactly what the spawn guards compare against (`cmp $0x8,%al`). The arms write `[DI+0x197]` and the map cell, not `0x1B7`. So the "8 agreements across two fields" was really **4 agreements from one field relabelled**, and the type->art transform rests on the four special-casting types only. What remains: confirm `>>2` on ordinary wandering spawns with a non-maxed character | agent | **medium** | naming monster types, CP5 |
| G20 | **~~enter an `X4` sector (24, 34 or 44)~~ CLOSED 2026-10-23 by `captures/spice86/tower-34` and `tower-34-open`.** Sector 34 is **dungeon-shaped**, not a third ruleset: map bytes all multiples of `0x10`, `IT'S DARK!` on entry, `I` = IGNITE TORCH with the `DS:26` light counter, arrows = `TURN LEFT`/`TURN RIGHT`, monsters use the `asm/147F` encoding and formula (40/40 slots). So the split is `{4,5}` vs `{0,1,2,3}`, not dungeon vs everything else. **Two things did not carry over:** `A` is ATTACK not ADVANCE (so the mode-aware dispatch keys off something narrower than inside/outside -- `DS:29` is 0x00 here vs 0x92 in the dungeon), and the first-person viewport renders a bordered but empty rectangle even with the torch lit. Full table: `docs/sector-rulesets.md` § 1b | agent | **done** | towers, CP5/CP6, the ruleset table |
| G21 | **what each shop stocks, and at what price -- ANSWERED, and the answer is "no table exists".** Every catalogue is an inline string printed via the string-output routine; the four `list_header_*` routines (renamed 2026-10-28 from `shop_header_*`, which named a caller nobody found) are 13-byte label printers that fall through. Weapons 8 items, armour 6 (CLOTH/LEATHER/CHAIN/PLATE/REFLECT/POWER), plus horse dealer, clue seller, tavern and cleric offering. Prices are computed inline -- the tavern sets a 16-bit 5 at `DS:27/28`, the clue seller multiplies a typed amount by 100. The two 6-spell lists are **the cleric shrine and the wizard shop**, each class-gated (`asm/184C`, `asm/19BA`), **not** town variation -- no per-town catalogue difference is demonstrated anywhere. Full write-up with grades and limits: `docs/commerce.md`. Street NPCs (merchant/thief/guard) do not transact at all | agent | **high** | shops, economy, CP6 |
| G22 | **CLOSED 2026-10-23 -- owner ruled, merges executed.** The premise in this row was itself wrong: the region past `19D7`'s end is not shop code but three shared helpers, and `19D7..2027` was left alone because its handlers end in tail jumps, so any fall-through boundary there is a judgement and not a fact the bytes assert. What was done instead: `2028..2051` (7 fragments -> `read_decimal_digit`), `2052..20AA` (4 -> `gold_spend_price`), `20AB..2121` (5 -> `bcd_price_from_stats`). 16 files -> 3, body content proven conserved, `assemble_clean.sh` byte-match unchanged. Rationale, measurements and the doubt: `docs/g22-split-proposal.md` | done | -- | commerce, function-map accuracy |
| G23 | **is the vendor food price stat-derived? -- the static half is now answered and it did NOT come out clean.** `asm/20AB_bcd_price_from_stats.asm` seeds `DS:27/28` and `DS:29/2A` to `0004`, takes the loop count as the **bit length** of `[DS:50]+[DS:4E]` -- **not** the `popcount` this row originally said, that was a misreading -- and walks a Fibonacci-style BCD sequence seeded 4,4 into `DS:27/28`. The printer was decoded the same day (asm/17B7 -> 51A5/51A9/51AE -> 53B5/53C5, itself two more fragment-splits of the G22 kind): it emits **DS:27 as two decimal digits then DS:28 as two**, so the observed `0200` means `DS:27=0x02 DS:28=0x00`. The model for the tavern call (`AL=3`, maxed character) leaves `0020`. **And 200 is not a value this loop can produce at any loop count** -- the reachable set is 4, 8, 12, 20, 32, 52, 84, 136, 220, 356, 576, 932, ... So either the food quote does not consume this routine's result, or the loop is still misread, or the printed string was not `0200`. **A live attempt on 2026-10-23 first failed to reach the quote, but the reason was wrong the first time it was written and is now corrected: it was never a timing problem.** Headless transactions are proven working -- sector 32, `T,Up` at `--delay 0.02` opened `THE ARMOUR SHOPPE`, `1` quoted `FOR YOU ONLY 0012`, `y` completed it, gold `9999→9987`, `DS:97` `00→01`, no human involved. The sector-31 food vendor instead returned `CMD: TRANSACT-<dir>` then `FUNNY, NO RESPONSE!` from every reachable tile, and that message is now decoded (`docs/commerce.md` § 5b): it means **the ray did not land on a `0x68` Merchant**, not that the key was lost. `monx31` puts a Merchant at (51,15) but that is where creatures *start*; live ones move. **Sweep run 2026-10-23: NEGATIVE.** Live creature arrays (`DS:137/157/197`) confirmed five `0x68` Merchants in sector 31 including one at (51,15) unmoved from spawn, and one at (55,17) that had moved -- so the lookup works. But from every maze-reachable tile beside them, all four directions, retries at three delays, **no price breakpoint fired**; the band gave `CMD: TRANSACT-NORTH` then `FUNNY, NO RESPONSE!` (delivered, refused). The ray rule is not "stand two cells away" and the armoury geometry does not generalise. **Ray decoded 2026-10-23 (static, as planned).** `asm/50B3_map_tile_ptr` returns the live grid cell at `DS:23/24`; `asm/4085` steps that cursor by `DS:19/1A`; the cell must be `0x68` or `0x7C` or the game says `FUNNY, NO RESPONSE!`; on a match `asm/40A3` jumps to runtime `1590`, the vendor dispatcher (corroborated: `1590/1591/1594/1596` executed in `food-buy`). **`0x68` is the merchant marker and it is in the map itself** -- in `mapx31` it occurs at exactly the seven `monx31` Merchant spawns, 7/7. That also explains the failed sweep: the delta comes from the *movement* code (`asm/0B0F`), not the talk direction, so the cursor stepped to (52,17) instead of the `0x68` at (51,15). **Prediction to test, one run:** step one cell toward the target, then press `T` with no direction. Full chain and grades in `docs/commerce.md` § 5c. Bonus, still free in the same read: set `DS:4E` and `DS:4F` DIFFERENT values -- every maxout character has them equal, which is why `tools/u2_edit.py` still calls cha/wis DISPUTED -- and one quote identifies which byte the routine reads **-- DONE 2026-10-29, by purchase rather
than by quote: the owner played a character with `DS:4E=0x20` / `DS:4F=0x40` in noVNC and paid
52 gp for the same horse a maxed character bought for 32, and the `Z` screen printed
`CHARISMA-20`. `DS:4E` is charisma, `asm/20AB` reads it, and the state.h label is proven. The
food-quote half of this row is still open.** `captures/spice86/d13-cha-vs-wis` | done | -- | **CLOSED 2026-10-29 by `captures/spice86/g23-food-live` and `captures/spice86/g23-weapons`.** The position problem was the blocker and it was a geometry problem, not a timing one: the ray is two cells (near = a sign letter or `0x7C`, far = the `0x68` marker), so the sweep stood beside the wrong marker. From (55,21) in sector 31 with `0x7C` at (55,20) and the marker at (55,19), `T,Up` at `--delay 0.05` opened the grocery, and `tools/bp_read.py --ip 17B7` stopped there with `DS:27..2A = 00 20 00 12` and gold `9999`; `--ip 2052` stopped on the same purchase with the same pair and gold fell to `9979`. So: one pricing path, the printed pair IS the charged pair, and the printed number is `0020` -- the `0200` this row and `docs/commerce.md` § 3b carried for a week was a transposed digit in a hand-copied screen, and `asm/20AB` with `AL=3` on a maxed character produces `0020`/`0012` exactly, both pairs. The second failing observation went the same way: the weapons shoppe routes through `asm/20AB` after all (`asm/1F00` passes the menu digit), and the axe is 20, not 25 (`captures/spice86/g23-weapons`, frame `w5-axe.png`, `FOR YOU ONLY 0020`, gold `9999→9979`, `DS:79` `01→02`). The `×100` economy is the clue seller's byte placement and nothing else; stats do move commerce prices, via `int + cha`; and `asm/20AB` is the price routine for every shop that computes one. |
| G24 | **~~do the three status counters survive a save?~~ CLOSED 2026-10-23 by `captures/spice86/cp5-g24-save/` -- no, and they cannot be.** Poked `DS:26A..26D = 07 09 08 08`, pressed `Q` in-game, and the bytes are absent from the 384-byte file (they would have to sit at file offset 0x234); on reload they read `00 00 00 00`. Only five bytes differed seed-to-saved, all inside the proven record. The same arithmetic puts the § 16 town alarm (`DS:1D7` -> file 0x1A1) outside the record too, so an alerted town cannot survive a save either. Write-up: `docs/spice86-live-findings.md` § 17. Bonus from the same runs: the poked counters were watched decaying in lockstep while the player stood still, and `Q` outdoors in sector 00 turned out to be gated on `[DS:6D]` with `ONLY ON EARTH!`, which contradicts `DS:6D` being the stale boots count -- flagged in `docs/phase4-state.md` § CP5. The owner was asked whether paralysis persists and answered that they did not know about SAVE, but that **"there's not really a leaves-combat state"** -- so persistence-by-save is the only persistence question there is. C1 places the counters at `DS:26B/26C/26D` (619..621), which is **outside** the proven save record `DS:36..135` (54..309) per `docs/save-format.md`. So either they are never persisted, or the save record is larger than currently proven -- and the second would mean `save-format.md` is wrong about its own boundary. **Settleable headlessly, no play needed:** seed a character with legs=9, arms=8, sleep=8 (values actually observed together on 2026-10-21), let the game write the `player` file, reload it, and read 619..621 back. Also worth capturing while a session is live: whether the legs and arms gates really are independent in play, i.e. paralysed legs still permitting an attack and paralysed arms still permitting movement, which is what the owner recalls and what a single boolean flag cannot express | agent | **high** -- it is a seed-save-reload-read cycle, all four steps already scripted in `tools/u2_edit.py` and `tools/spice86_start.sh --player` | C1, C3, `docs/save-format.md`'s declared record boundary, whether CP5 can treat status as persistent state at all |
| G25 | **~~is the role byte the cause of hostility or its record?~~ CLOSED 2026-10-23 by `captures/spice86/cp5-g25-aggro/`.** `0x01` is an **alarm**, not a class: guards are born `0x03`/`0x00` (baseline at load), and landing a blow on *anything* in a town flips every guard in the loaded sector to `0x01` simultaneously -- including slots at d31, d44 and d46 that were never touched -- after which they form up on the player and take HP (`9999 -> 9796`). Two controls: 65 s idle on a fresh load flips nothing (guards wander, roles hold), and a *missed* swing flips nothing. So the byte records violence that landed; whether it is also read back as a cause is the remaining thread, noted in § 16. Full write-up: `docs/spice86-live-findings.md` § 16 | agent | **done** | CP5 encounter model, `docs/monster-spawn.md` § 6, the guard-hostility thread |
| G26 | **~~is the guard response sector-wide or radius-based?~~ ANSWERED for the alarm, 2026-10-23 by `captures/spice86/cp5-g25-aggro/`: sector-wide.** Every guard in the loaded sector flipped to `0x01` from one blow landed at the player's feet, and distance did not predict it -- d1 and d46 both flipped, and the d44/d46 pair sat in a different part of the map entirely. What is *not* settled: whether far guards then actually travel to the player (the d31+ ones flipped but stayed put in the window observed), and whether *killing* a guard escalates beyond wounding one | agent | medium -- CP5 can model the alarm as sector-wide and refine the pursuit rule later | CP5 guard behaviour, `docs/spice86-live-findings.md` § 15, § 16 |
| G27 | **~~what is `DS:26A`?~~ CLOSED 2026-10-28 by `captures/spice86/g28-pursuit`.** It is the creature-move freeze counter. The only non-zero writer is `asm/3C04_coin_rub` (the `YOU RUB A COIN...` ENILNO secret), which stores `0x14`. **Measured live:** poking `DS:26A = 0x14` froze both test creatures for three consecutive commands while a baseline moved them before and a clear restored movement immediately after — a clean A/B/A — and the counter fell exactly one per completed command (`14 → 13 → 12 → 11`). `asm/080D` zeroes `DS:26A..26D` together and `asm/0C33..0C53` ticks them down, as before | done | -- | C1's counter set, the coin rub's gameplay effect, `asm/2199_creature_step_gate`'s first clause |
| G28 | **~~does the `-1` in the creature pursuit delta behave the way the bytes say?~~ CLOSED 2026-10-28 by `captures/spice86/g28-pursuit`, and the answer is that the `-1` does not exist.** The claim came from reading `stc` then `sub` in `asm/4F11` and assuming `SUB` subtracts CF. **It does not; only `SBB` does.** `DS:19/DS:1A` were read directly after each sweep, 20 trials, each with a control creature that had to move for the trial to count, and every trial gave the plain symmetric difference: a creature at `(player-1, player-1)` steps straight onto the player. `sign(4*(player-mon))` matches the true shortest torus direction with **0 mismatches over 4096 pairs** (excluding the 64 exact antipodes). Contact was confirmed alongside it: an adjacent creature holds position and the player's HP falls. Side finding: `DS:20` reads 0 after every turn despite contacts, so it is not usable as between-turn state | done | -- | CP5 creature AI, the contact trigger, `docs/contract-creature-tick.md` step 2 (corrected the same commit) |
| G29 | **~~what is tile `0x5C`?~~ CLOSED 2026-10-28 by `captures/spice86/g29-field`: it is the FORCE FIELD.** Seeded into sector 03 beside the `0x5C` ring at (32,29) and stepped onto it. With no ring, HP fell **exactly 1000** on six separate crossings (`9999→8999→7999→6999→5999→4999→3999`); with the ring held (`DS:D6 = 01`) it fell nothing; stepping onto a plain `0x70` town tile fell nothing either. **This is the C5 placement answer**, and it means the reason `docs/phase4-state.md` § CP5 gave for refusing to wire C5 to a tile was a units error: it searched `mapx00` for the mode byte `0x2e`, but the mode is `map_byte >> 1` and map bytes are multiples of 4, so the test could not have found it. The `0x5C -> 0x2E` sample was already recorded in `asm/2FAD_vehicle_board_dispatch`'s header | done | -- | C5 placement, the step gate's species clause (monsters cross fields, NPC classes do not), `docs/passability-findings.md` open questions |
| G30 | **does `Ctrl-Y` move the character south?** The arrow scancode table maps Up/Down/Right/Left to `0x0E/0x19/0x05/0x17`, and the key seam swallows the typed control equivalents of three of them (`0x05`, `0x0E`, `0x17`) but **not** `0x19`. So the bytes predict a typed `Ctrl-Y` walks the character south while `Ctrl-E`, `Ctrl-N` and `Ctrl-W` do nothing. One session, four keystrokes, read `DS:00/01` after each — and it also settles the swallow list and the `AH=0xFF` convention in one go | agent | **low** — cosmetic, but it is the cheapest available confirmation that the traced key seam is right, and a wrong answer here means the swallow decode is wrong | CP4 input, `docs/contract-overworld-walk.md`, the arrow-vs-typed-key story |
| G19 | the **dungeon creature set is unreachable by the tile join** — all seven codex dungeon images are `64x41` screenshot crops, and the dungeon is a wireframe renderer whose outlines probably live in `original/monsters` (64 records x 34 B of coordinate pairs, still T9). Dungeon Orc, Daemon and Balron are **different creatures from their overworld namesakes**: different art, HP `17-153` / `177-217` / `209-249` against `16` / `64` / `255`. Identify them through the stroke table, not the tile table | agent | **medium** | dungeon rendering, T9, any dungeon bestiary |
| G36 | **can a thief steal keys from a guard? Owner asked to settle this by human play, so the verdict is `owner` by request, not by elimination.** The bytes say no, and they are worth reading before you sit down: `asm/3F5D`'s ONLY target test is `cmp $0x68,%al`, and `0x68` is the merchant marker (`docs/commerce.md`), not the Guard; and a census of every string in the binary gives the complete steal outcome set as `STEAL DIRECT-` / `STEAL FOOD!` / `STEAL ARMOUR!` / `STEAL WEAPONS!` -- there is no keys outcome anywhere. What probably fused in the memory is that guards DO carry keys -- the hint table at `asm/16BD` says `GUARDS CARRY KEYS!` and the third-party bestiary records Guard special as "Drops 2 keys" -- but that is from killing, and the cheat sheet says so ("to get keys, kill guards"). **What a human playtest adds that static cannot:** whether the aim at a guard produces some other outcome the strings do not cover (silent transfer, a wrong-item bug, or a message reached by composition rather than a literal), and whether the Apple II build differs -- the 6502 side is extracted in-tree and untested for this. Play as a thief, `S` + direction at a guard, and report what prints | **owner** (requested) | **low** | whether cmd_S's target set is complete, and one standing `recalled` vs `measured` conflict |
| G35 | **does the barkeep actually print the chatter/hint table, and in what order?** The table is `asm/16BD_barkeep_chatter_hints.asm` (ten NUL-separated entries: three pieces of bar chatter then seven hints), reached by the selector at `asm/155D` through a cursor the tavern menu stores at `DS:27:28`. The decode is byte-proven and the mechanism is read directly, but nothing on this path has executed in any of the 142 flow-dump harvests. **Attempted 2026-10-26 and NOT closed — and the item was mis-scoped.** It is not a short drive-to-a-tavern job: the pub sits behind a four-condition chain that has to be satisfied together, all of it now measured — cmd_T must aim at a cell holding `0x68` (`asm/409F`), the vendor dispatcher `asm/1590` needs the PLAYER at `X >= 0x20` **and** `Y >= 0x20` (both are `jb 15ba`; a low coordinate diverts to the weapons shoppe instead, which was seen on screen from sector 82 (11,52)), `asm/15D6` then needs site kind `DS:4A == 2`, and the pub body is entered at **`15E4`, not the declared `15E1`** — `15E1` is a `jmp 0x19AF` trampoline for non-towns. Qualifying `0x68` markers exist in sectors 22 (51,49 / 53,53 / 54,53), 32 (53,46), 82 (51,49) and 92 (51,43). At the two tried the aim answered `FUNNY.. NO RESPONSE?` — the ray found no transacting NPC standing at the marker, which is the remaining unknown. **What is left:** find which NPC type must stand on the `0x68` cell (the horse dealer was next to sector 82's (51,49)), i.e. cross the live NPC positions against the qualifying markers rather than against the letter runs `tools/shop_access.py` reports. Still agent-drivable, but it needs that cross first. **UPDATED 2026-10-29 — the cross is done, and it is simpler than expected: the NPC type byte is the same value as the tile marker.** Live in sector 21, `mon_type` 0x68 (Merchant) slots stood at (5,5), (15,46) and (20,46), and all three are `0x68` cells in `mapx21`. So the question is answered. **A new blocker replaced it, and it is a geometry one.** Sector 21 has eight `0x68` markers — (5,5) (16,16) (48,16) (34,41) (15,46) (20,46) (16,48) (48,48) — and a map scan shows **none of them has an orthogonally adjacent cell of the street tile the player walks on** (`0x0c`); the cells around them are `0x08`, and stepping from a street cell into one answered `INVALID MOVE!`. A one-cell ray from `asm/409F` therefore cannot land from the street in that town. Two consequences, both untested: the coordinate gate in `asm/1590` (`X >= 0x20` AND `Y >= 0x20`) means only **(34,41) and (48,48)** are even eligible in sector 21, so every attempt so far was aimed at a low-coordinate marker that diverts to the weapons shoppe anyway; and there is almost certainly a **doorway tile** into the shop interior that nobody has identified (`0x78` appears in the shop's wall row). Next step is that, not the NPC cross: find the tile that makes a town cell walkable, or stand at (34,41)/(48,48) and see what the ray answers.
**THE GEOMETRY BLOCKER IS RETRACTED, same day — it was an artifact of a bad comparison, and the shop works.** The owner bought a horse over the shop counter in sector 22 while memory was read live (`captures/spice86/shop-window-transport`). Player at (48,53), vendor on the `0x68` marker at (48,51), the sign lettering at y=52 between them: **you buy across the sign band, not next to the vendor.** So the adjacency premise was wrong twice over — the ray is not one cell, and the walkability scan compared raw `mapx` tile bytes against the stamped grid at `DS:1800` as if they were one representation. They are not: the same ground is `0x0c` in `mapx21` and `0x70` in the grid, so the "no adjacent street cell" result measured the wrong numbers. **G35 is unblocked by this and the hint-table question is answerable again**; the remaining real constraint is the `asm/1590` coordinate gate, and note that sector 21 is the **village** (the sector's ones digit is the site kind: 1 village, 2 town, 4 tower), so `WIZARDRY`/`SPELLS` are village shops reached from the village gate, not the town gate | agent | **medium** | whether the hint table is live, `docs/third-party-claims.md` § 3's corroboration grade |
| G38 | **does a town thief steal once it has aggro, and how does a town creature get aggro?** Owner's hypothesis 2026-10-26, following G37: thieves do not steal unless aggro'd, and the open sub-questions are whether attacking one in town gives it aggro, and whether a creature gets aggro from *another* being attacked the way guards alert. The bytes are friendly to this — role 2 is the alerted state and `asm/113A` is "on the world map there is nobody to alert", so alerting is town-scoped. **Attempted 2026-10-26 as `captures/spice86/linda-aggro` and NOT tested — the method was wrong, and the readbacks say so.** `--poke` on `mon_x`/`mon_y` moves the creature arrays but not the **grid stamp** at `DS:1800`; movement and the aim ray read the grid, the sweep reads the arrays, so the pinned thief was somewhere the aim could not see. `ATTACK--` executed and neither `HIT!!!` nor `MISS` ever printed, i.e. the aim resolved to no target. **What is needed:** a driver that never pokes positions — poll live thief positions each turn, wait for genuine adjacency by wandering or walking, fire `A` + the matching arrow, and confirm the aim landed by the frame rather than by the send returning ok; diff all 32 role bytes before/after for the transition; then `PASS` and watch `DS:D6..DF`. Second variant attacks a *different* creature to test the alarm-by-proxy half. **One trap for whoever takes it:** sector 22 ships slots 8–11 at role `02` **at load**, so `02` is a data value as well as a transition target — do not chase a transition that was never one | agent | **medium** | whether the steal has an aggro precondition at all, which is what G37 left open; and the town alert model |
| G39 | **~~the rocket has never been entered in any capture~~ CLOSED 2026-11-03** by `captures/spice86/g39-rocket-flight`, an owner-played session in the noVNC container. `DS:13 = 0x28` observed twice, frame each time, and `cmd_L` now reads executed in 4 of 210 harvests. Every rocket rule inferred from bytes survived contact: the southward flight is `y += 2` with `x += 1` on multiples of 8 from `(0,6)`, and it predicted **both** landings to the cell, `(7,62)` and `(5,44)`. Crash-on-non-grass is real and fired twice (`0x00` water, `0x0c` forest), each running the death tail at runtime `0x0E62`. Three corrections to what this file and the engine believed: the rocket is not a one-shot teleport but a two-phase vehicle (orbit, then dive); food keeps ticking while you sit on the pad, so "costs no food" is about steps only; and a launch spends one TRI-LITHIUM, which `docs/save-format.md` § 5 recorded as never observed. Newly found and not in any earlier plan: hyperwarp dumps you off course iff `(fuel & 3) == 3`, `XENO/YAKO/ZABO` are `DS:4CA/4CB/4CC` derived from world-indexed tables, space needs armour >= REFLECT, and a landing always arrives in a world's wilderness sector (Earth to 40, world N to N0). Still open inside it: landing on `0x08` grass is predicted safe and untested, `YOU HAVE EXPLODED!` has only ever been seen not to happen, and the world-9 name routine sits past `0x729B`, the highest offset ever executed. | done | **closed** | the space layer, and it puts CP8's frontier precisely at `0x729B` |
| G40 | **does a horse really refuse shallow water?** `asm/2626` rejects mode `0x02` (map `0x04`) after running the foot ladder, which is the one tile that makes the mounted rule not "the foot rule". No capture has ever walked a horse: `DS:13 = 0x22` appears once, in the `stepcost` forced poke, and that session read food, not movement. **Agent-drivable**, same recipe as G39 with a horse tile beside shallows, and it needs the position read before and after, not just the food. **CLOSED 2026-10-29 — yes, it refuses, and the engine was already right.** `captures/spice86/g40-horse-shallows`: sector 20 at (18,40), map `0x0c`, shallow water (`0x04`) to the right and below, land above. On foot Right moved onto the shallows; with `DS:13` held at `0x22` across the key press, Up moved onto land (the control, so a refusal is not "mounted cannot move") and both Right and Down refused with the position unchanged. `ExecutedInstructions` carries the horse arm and the verdict block, so the reject is the rule firing and not a lost key. `u2_mode_passable` implements exactly this and needed no change. The mode is still a held poke rather than a real boarding — a headless transport purchase is still unproven | agent | **closed** | F15's mounted rule, and whether the engine's `u2_mode_passable` inherited a rule from a dead branch |
| G41 | **where does `DS:13` get set to the on-foot value, and is it class-dependent in play?** `asm/080D` and `asm/44D4` both compute `0x78 + 2*pclass`, so a Mage should stamp `0x7A` and a Thief `0x7E`. Every capture reads `0x78` because every capture used a Fighter, and no caller of `asm/080D` exists in the corpus (no label reference, and a byte scan for near calls/jumps to that runtime address is empty). **Agent-drivable:** create a non-Fighter with `tools/u2_edit.py`, enter the world, read `DS:13`. One sample either way. **CLOSED 2026-10-29 — the class dependence is real.** A Wizard (`class 2`) loaded from a seeded record read `DS:13 = 0x7A` on foot, which is `0x78 + 2*2` exactly as `asm/080D` computes, captured live in `captures/spice86/shop-window-transport` (read immediately before the horse purchase, when the mode was still the on-foot stamp). Every earlier capture read `0x78` because every earlier character was a Fighter, which is precisely the artifact this item suspected. **Consequence for the engine:** `overworld.c` sets `ctx->state.player_stamp = 0x78` unconditionally at world open, so three of four classes are stamped wrong the moment the engine enters the world — it should be `0x78 + 2*pclass`. The second half of the question, *where* `DS:13` gets set in the original, is still open: no caller of `asm/080D` exists in the corpus and that part is unchanged | agent | **medium** | whether the engine's constant `0x78` at world open is wrong for three of four classes |
| G42 | **does boarding spend the gating item?** `shipcap3` boarded the frigate with `DS:E2 = 01` and still reads `01` afterwards, which says no — on one sample, from a maxed character, where the starting count is assumed rather than recorded. The engine does not spend, and `asm/2FD6`/`asm/3038`/`asm/3048` contain no subtract on any `items_held` slot, so the bytes agree. What would close it is a boarding with a count of 2 read before and after. **Second sample 2026-11-03, and it is the better one:** the rocket was boarded with ANKH 1 and TRI-LITHIUM 1 and both still read 1 afterwards (`captures/spice86/g39-rocket-flight`), so boarding checks and does not spend there too. Still one-sample-per-item, but now for two different vehicles. | agent | **low** | whether vehicles are consumables, which changes what a playthrough can plan around |
| G44 | **~~what is `DS:6D`, really?~~ CLOSED 2026-10-29, same day, from the bytes: it is the WORLD byte.** 0 = Earth, 1..9 = the planets, 0x0A = deep space. Evidence: `asm/7030_space_collision` is the only writer in the tree and it stores `0x0A` immediately before printing `YOU ARE IN DEEP SPACE.`, stores a world index beside `YOU ARE ORBITING `, dispatches on `cmp $0x0`/`$0x1`/`$0x2`…, and answers `cmp $0xa` inside the landing path with `REQUEST DENIED!` — you cannot land in deep space. It then does `mov 0x6d,%al ; mov %al,0x49`, copying the world into the sector **tens** digit, which is how `MAPXNN` reaches a planet's sectors at all. `asm/3DFA`'s message `ONLY ON EARTH!` is a literal description of the test, and the game's own intro supplies the axis: `ACROSS THE FACE OF THE EARTH` … `TO THE PLANETS OF OUR SOLAR SYSTEM`. **Why it had the wrong name:** `tools/u2_edit.py` defined a `boots` field at `DS:6D` and `maxout` wrote 1 into it, so every maxout character was silently on planet 1 and could not save — and `state.h` then named the byte `boots_count` after our own write. Boots are `items_held[BOOTS]` at `DS:D9`, which is what the paralysis resist actually reads; that also retires the old `STALE COPY` note, since a byte that never was boots obviously did not change when a thief took them. The shipped `player` carries `0x00` here. Fixed: field renamed to `world` in `state.h`, the bogus `boots` field deleted from `u2_edit.py`, and `maxout` no longer moves the character off Earth | agent | **closed** | `docs/save-format.md` § 10, `asm/3DFA_offer_only_outdoors_earth.asm`, `asm/7030_space_collision.asm`, CP5 V3 |
| G43 | **is the plane's forest rule the grounded rule only?** `asm/2604` refuses forest, and `asm/38B0`'s flight loop consults no passability routine at all — so the byte answer is "airborne flight has no tile rule". But the flight loop has only ever been filmed launching and landing, never crossing a forest tile, and the owner's recollection ("plane on land can't traverse forest") is about the ground case. Sector 41 (the airfield town) is the place: `BOARD` a plane, `LAUNCH`, fly over forest, record accept/refuse. This is the handoff's W6 and it is still the one genuinely open vehicle question | owner (needs a frame per step) | **high** | whether the engine's plane rule is scoped to the ground correctly |
| G45 | **is the sector tens digit a world or a time period?** **RESOLVED 2026-10-29 — on Earth the tens digit is the ERA axis.** Observed by stepping through a time door (`docs/moongates.md`, `captures/spice86/moongate-cycle`): `DS:49` went `02 → 03 → 02` while `DS:6D`, the world byte, stayed `00`. So `asm/3546`'s era reading is correct for Earth and the `asm/7030` write from `DS:6D` is the off-Earth case, not a competing meaning for the same situation — which is why reading the two routines side by side could never decide it. **Confirmed from the other side 2026-11-03:** a rocket landing on world X wrote `DS:49 = 0x09` and put the character in sector 90, while landing on Earth wrote `DS:49 = 0x04` and put them in sector 40 (`captures/spice86/g39-rocket-flight`). The off-Earth branch is the world axis, and Earth's rocket arrival is hardcoded to era 4 rather than to 0 — which is the first time either reading has been observed moving by a planet landing. **Step 1 below is closed, and by a different route than written, because there is no time machine in this game at all**: the phrase entered this project as a recalled idea, the binary has no such object, and the real mechanism is a runtime-placed moongate that transits you on the step onto it, no `E`. **Step 2 is still open** and still needs the G39 rocket. Original text, kept for the record: The two readings collide and neither has been observed. `asm/3546` uses `DS:49` as the era index (`mov 0x49,%bl ; mov %bx,%di`, then `or/je` so tens 0 = `ANOS: LEGENDS!`, 1 = 9,000,000 B.C., 2 = 1423 B.C., 3 = 1990 A.D., 4+ = 2112 A.D.) — that is G14's closed conclusion. But `asm/7030_space_collision` **writes** `DS:49` from `DS:6D`, the world byte (0 Earth, 1..9 planets, 0x0A deep space, set beside `YOU ARE ORBITING ` and `YOU ARE IN DEEP SPACE.`). One byte, two meanings. The shipped content argues for worlds: `mapx` exists for tens 0..9 and **every tens group has exactly one wilderness `X0` plus its own sites**, which is what a world looks like; five time periods do not explain ten groups. The Apple twin argues the other way, sort of: it derives its index as `(x >> 1) + 1`, arithmetic that turns ten things into five, which fits eras taken from a ten-valued thing rather than a direct copy of it. **Experiment that settles it, in order of cost:** (1) on Earth, read `DS:49/4A` and photograph the `ANOS:` line, use the time machine, read both again — if the tens digit moves, eras live in the tens digit and the world byte is a different axis that merely shares the byte on landing; (2) reach a planet by rocket and do the same — if `DS:49` becomes the planet index while `ANOS:` still claims a terrestrial date, the two are genuinely the same byte and the era label is simply wrong off-Earth, which is a finding in its own right. Step 1 is owner-play (the time machine is a scripted story beat); step 2 is the G39 rocket capture with two extra reads | owner for step 1, agent for step 2 | **medium** — nothing in the engine depends on the answer yet, but CP7 (space travel) and any time-period content will, and a wrong axis there means writing the wrong sector arithmetic | `asm/3546_time_index_message.asm`, `asm/7030_space_collision.asm`, G14's closure, CP7 |
| G46 | **what do the other CAST branches do — PASSWALL, SURFACE, PRAYER, BLINK's validity rule, and KILL's award?** Slot 7 is now fully observed (`captures/spice86/cmd-c-magic-missile`: the facing scan, the amount, both exits, and the `KILLED--GOLD+3--EXP.+00` message), and the target scan is shared, so what remains is the branches nobody has filmed. Each needs the same shape of test: a tower, the slot readied with `M`, and a `tools/bp_read.py` stop on the branch to see which arm runs. BLINK's random-odd position write is proven from bytes but its validity call has never been seen to refuse; KILL reaches the same death path a MAGIC MISSILE kill does and its award table is undecoded; the third-party "PRAYER is 50%" claim was NOT FOUND in the bytes and is still a hypothesis. **PARTLY DONE 2026-10-29 — KILL and BLINK are now observed** (`captures/spice86/g46-kill-blink`): KILL shares the CAST target scan and jumps straight to the death path with no damage step, proven by the discriminator that a creature at full HP `0x99` — which MAGIC MISSILE leaves alive — had its slot erased outright, with GOLD `9999 → 0006` and EXP `9999 → 0001` awarded as wrapping BCD; BLINK showed both exits, `CAST-BLINK-FAILED!` on one attempt and `(5,5) → (7,15)` on the next, both coordinates odd and inside 1..15, gated by the same `asm/1451` site-cell validity call the ladder spells use. **What is left in this item: PASSWALL (4), SURFACE (5), PRAYER (6)**. One more thing this item should settle while it is in there: ~~whether `mon_kind` is a floor index~~ **ANSWERED the same day, before this item was ever run: it is NOT** — the sector-24 tower's floor-0 creatures carry `mon_kind` 0..15 simultaneously and `monx24` is all zeros, so the kinds are generated at load and the CAST gate is literally "kind index == caster's floor number" (live flip recorded in `asm/330C_cast_target_finder.asm`). `DS:25` itself was proven the floor index by `captures/spice86/d12-ladder-ds25` and renamed `site_floor`. **Agent-drivable** with the recipe in `docs/spice86-capture.md` patterns 3 and 4 | agent | **medium** — decision D14 (does `cmd_C` ship gates only), and any CP6 spell content |
| G47 | **TOOLS and dungeon traps — decoded statically, never observed, and parked behind the dungeon decision.** The owner's recollection ('tools are one-use, consumed to disarm a trap and absorb its damage') is confirmed by bytes and by nothing else: `asm/277B` prints `ARGH! A TRAP!`, reads `DS:66`, prints `ESCAPED! BY USE OF TOOLS!`, and at `0x2904..0x290C` does `stc ; cmc ; sbb $1,%al ; das ; cmc ; mov %al,0x66`. `DS:66` is **not** an `items_held` slot — it is a packed-decimal counter beside torches (`DS:64`) and keys (`DS:65`), and the item name table at file `0x501D` has no TOOLS entry, which is why a scan scoped to the item array reported the game had no such item. Four things remain unobserved, all of them inside a dungeon: (1) the spend itself, with `DS:66` read before and after; (2) **what happens with zero tools** — the no-tools arm of the same branch is the damage, and its amount and its HP path are undecoded; (3) where tools come from, i.e. `asm/2CF6_tools_add_one`'s callers, and whether they are a dungeon pickup, a shop, or a random drop; (4) whether the ceiling-99 rule `state.h` asserts is enforced at the increment or only claimed. **Blocked on an owner decision, not on an owner's time**: AGENTS.md § 3 parks dungeons until overworld and town are finished, and the dungeon renderer is undecoded, so no agent can reach a trap today. Once that is lifted the capture is agent-drivable headless — seed `DS:66` to 2 and to 0 with `tools/u2_edit.py`, walk into the trap tile, read the byte and the HP either side, and take a frame per the rule about visible claims. Recorded in `docs/save-format.md` § 5 and in `asm/277B_sub_277b.asm`'s header | owner to unpark, then agent | **low while dungeons are parked** — nothing in the engine models traps or tools yet, and CP6 is overworld and town | traps, the `DS:64/65/66` consumable trio, and any dungeon checkpoint |
| G48 | **what is the `[DS:1D7] == 1` refusal in STEAL guarding against?** `asm/3F5D` refuses a steal outright when the byte at `DS:1D7` is 1, and `DS:1D7` is the base of the creature role array (`mon_role[0]`). Three things are wrong with that as a fact. It is an **un-indexed** read of slot 0 on a code path that has no creature scan to have indexed anything; the roles seen anywhere in the corpus are `0x60..0xFC`, so `1` is not a role value in the sense the tlkx index uses; and the grid reader at `asm/52B3` may well leave something there, which would make the whole test about the target cell rather than about a creature. The owner's recollection was "maybe guards?", which is a reasonable guess and still a guess. **Agent-drivable, and cheap:** the same town setup as the TRANSACT captures — stand so the ray lands on a merchant marker, pin `--rng-state 000000000000` so the two rolls always pass, and vary `mon_role[0]` between 0 and 1 across two runs, reading whether the loot landed. Two runs, and it also settles whether `asm/52B3` writes the byte. The engine currently reproduces the byte literally (`mon_role[0] == 1` refuses) rather than guessing which interpretation is right | agent | **low** — `cmd_S` is implemented and gated either way; this is one branch whose meaning is unproven, and it is named at the branch | whether STEAL has a guard rule at all, and what `asm/52B3` really returns |
| G49 | **how far does a frigate cannon reach, and what does it actually damage?** The owner recalls a **3-space** range, which is what makes shelling a shore from 2-3 tiles out safe, and damage "typically between 32 and 70". The bytes disagree on both, and the disagreement is recorded rather than resolved by whoever typed the code. **Range:** `asm/35F0` calls `asm/23E6`, which seeds its cursor at player + delta and compares every creature against that **single cell** — its own header carries a standing warning that it is "not a ray with a reach". `cmd_T` gets two cells by stepping the cursor twice in its own body; `cmd_F` has no such stepping, and the only call between the aim gate and the probe is `asm/5EAC`, a tone wrapper that pushes four registers and touches nothing. So the engine implements **one cell**. **Damage:** `asm/3606` is `or $0x20,%al` on a random byte, which forces bit 5 rather than masking, so the reachable set is every byte with bit 5 set — 32..63, 96..127, 160..191, 224..255. Floor 32, ceiling 255. The 32 agrees exactly with the recollection; **70 (0x46) is not reachable at all**, bit 5 clear. Note this also corrects this project's own `asm/2C19` header, which had recorded "32..63" and was refuted by `engine/tests/attack_check.c` sweeping all 256 seeds. **Experiment that settles the range**, agent-drivable: board a frigate at Port Boniface (`BLUE TASSLE` + `BOARD`, the `shipcap3` recipe in `docs/spice86-capture.md`), park so a creature sits exactly 2 cells away in line with a direction, `F` + that direction, and read `mon_hp` before and after plus a frame. One hit at range 2 refutes the bytes and proves the recollection; a `--MISS` refutes the recollection. Do the same at 3. **Grade: the one-cell reading is `measured` from the listing and never observed; the recollection is `recalled`.** Neither has been seen on a frame | agent (needs a frigate boarding, which is itself unproven headless) | **medium** — `cmd_F` is implemented and gated on the off-ship arm only, and the on-ship arm has no end-to-end gate because the ship stamp is not in the saved record | whether naval combat is usable from a safe distance, and the ceiling on any future `cmd_F` playtest |
| G49a | **~~how far does a frigate cannon reach?~~ CLOSED 2026-10-29 by `captures/spice86/cannon-range-live`, same day it was opened, and the owner's recollection lost.** **One cell.** The frigate state was built with a single poke (`[DS:13] = 0x24` is the whole gate, so no boarding was needed) and the target by planting `mon_type/x/y` at a chosen cell with `[DS:26A]` frozen so it could not wander — NEGATE TIME's own mechanic, proven hours earlier, doing the job here. Fired East with the same keypress at three distances: **1 hit twice** (`mon_hp` `0x99→0x5B`, then `0x99→0x76`), **2 missed**, **3 missed**. Repeatability at range 1 is what makes the misses a finding rather than a bad roll. Every address in the chain is in that capture's `ExecutedInstructions`, so this is `observed`. Two extras came free: the **hit prints nothing** (`FIRE DIRECT-EAST` with no `--MISS`, exactly as the absence of any `str_out` in `asm/35F0..asm/3611` predicted), and the HP subtract is **plain binary, not BCD** — `0x99 - 0x3E = 0x5B` and `asm/2C19` has no `das`, so a BCD-looking field is decremented in binary and the engine must not be "corrected" toward decimal. What is still open from G49: a cannon **kill** has never been seen (so the no-reward reading stays static), and no cannon shot has ever been fired after a real `BOARD`. The engine's `u2_combat_cannon` needed no change |
| G49b | **~~has a cannon ever been fired after a real BOARD?~~ CLOSED 2026-10-29, same day, by the owner driving it himself** over `deploy/spice86-novnc`. He walked onto the frigate tile at sector 31 (14,25), pressed `B`, and `[DS:13]` went to `0x24` for real — no poked stamp — then fired and reached the same verdict: **one tile**. So G49a's one caveat is gone, and the range is now agreed by three independent readings: the listing, a scripted poke capture, and a human with a keyboard. **Two things came out of the session that were not the question.** First, **boarding does not spend the BLUE TASSLE**: it stood at 5 before `B` and 5 after, which is a second independent sample alongside `shipcap3` and strengthens G42's no. Second, and this is the one worth keeping: **the printed manual says the cannon has range, and the manual is wrong.** The owner's recollection of three spaces came from somewhere reasonable — the documentation says so. So the source order on this mechanic is manual (wrong) > recollection (wrong, and probably sourced from the manual) > bytes (right) > play (right). A period manual is a third-party claim and gets the same veto as a BBS walkthrough | done | **closed** | nothing — the engine already matched, and G42 gained a second sample |
| G50 | **the site trio's effects have never been seen, only their entries.** `cmd_I`, `cmd_D` and `cmd_K` are implemented from a static decode read across a Phase 2 fragment boundary, and all three have execution evidence for the entry but not for what they do. Three specific unknowns, in descending value. **(a)** Does leaving a dungeon really save the game: `asm/3823`'s exit block branches on `[DS:6D]` and one arm writes the 256-byte player record with an int-21h handle write, which has never been observed and is the only place in the corpus where the game saves itself without being asked. **(b)** The light counter in motion: `[DS:26]` refilled to `0x96` by a torch rather than by the LIGHT spell, and `TORCH BURNED OUT!` printed by a step rather than by a cast. The CAST capture gave us the spell side; the torch side is the same byte by inference. **(c)** Whether a tower's top floor is reachable at all — `asm/3381` increments with no bound check while `asm/1451` bounds the same byte with `cmp $0xf`, so the counter and the grid index disagree about the ceiling and no observation says which one wins. **(a)** and **(b)** need a tower or a dungeon walked into, which is the parked subsystem; **(c)** needs someone to climb past the top on purpose. None of them block the engine, which implements the decoded behaviour and names its own gaps at the mutation site | owner (needs a real site; the engine has no floor-aware grid) | **open** | dungeon park, save-format completeness | done | **closed** | nothing — the engine already matched |
| G51 | **does `A` attack or advance inside a site — the two captures disagree and the bytes side with one of them.** `docs/spice86-live-findings.md` § 13 tabulates `A` as ADVANCE in a dungeon, from `captures/spice86/dungeon-35-live`. § 1b records `A` printing `ATTACK--MISS` in `captures/spice86/tower-34`. `asm/2B9B`, the site-kind gate at the head of the ATTACK chain, is `mov [DS:4A],al ; cmp $4,al ; jb L_002da5`, and **both** destinations reach `asm/2BA5`, the arms-paralysis gate: the `jb` targets it directly and the fall-through arrives three bytes later. So the gate refuses nothing, and there is no arm off the `A` jump-table entry that could print ADVANCE. The engine follows the bytes and the tower frame — `A` attacks the cell the facing names, Up-arrow is ADVANCE — and `run_cp7.sh` gates the attack. **What would close it:** one headless run in a dungeon pressing `A` with a creature directly ahead and one pressing it against a wall, with a frame each. Agent-drivable now that the dungeon is open. Do not re-derive it from `asm/2B9B` alone; the disagreement is about what the § 13 capture actually pressed, not about what the gate does | agent | **open** | the dungeon command map, § 13's own table |
| G52 | **`GET`'s overworld arm is undecoded, so the command is half a command.** Inside a site the whole thing is now byte-read and gated: the cell under the player must be `0x40`, print ` CHEST!`, clear the cell, roll one byte against `0x40` for gold or TRI-LITHIUM. Outside a site `asm/363B` reads `[DS:14]` against `0x2a` `'*'` for armour, `0x2c` `','` for a weapon and `0x40` `'@'` for a chest, and picks WHICH item with `asm/361C` plus a roll. `asm/361C` is unnamed and its table has not been read, so the engine prints `GET` and completes without touching anything outside a site — an honest gap rather than a stub that looks finished. **What would close it:** read `asm/361C` and the two BCD increment sites in `asm/363B`; a static afternoon, not a capture, and the item pools are already extracted for the shop code | agent | **open** | `cmd_G`, the ground-item question § 13 raised |
| G53 | **~~T9: the dungeon stroke table at `DS:2900` has still no decoded consumer~~ CLOSED 2026-10-29, statically, the same day it was opened — and the item's own framing was the obstacle twice over.** It said "identify them through the stroke table, not the tile table" and "needs a screenshot to derive", and neither was true. The table is `original/monsters`, loaded verbatim at `DS:2900` by `asm/07F3` (a DOS `AH=27h` block read, `CX=0x800`, transfer address `[DS:474]`), and the live DS at that address is byte-identical to the shipped file — so the data was in the repo from the first day. The framing was the hard part and it is TWO bytes, not three: `asm/68ED` reads the second byte twice from the same pointer and only then advances, so `b0` is x (0 ends the list) and `b1` packs y in the low five bits with the pattern index in the top three. `asm/68A3` scales both by four; `asm/4BE0` paints `cs:[bp+0x48D4]` (`ff 11 88 cc 33 00 00 00`) as a 1..4 pixel run two scanline-pairs tall. Plotting the file that way yields **seven distinct creature silhouettes and an empty block 0**, and `asm/07F3`'s `CX=0x800` = 8 × 256 explains the block index as the dungeon type. Engine: `u2_dungeon_sprites()`, gated by `run_cp7.sh` against a with-art / without-art control (591 vs 490 centre-band pixels), and shown going red by disabling the pass. **The far arm is explained too, and not as a bug:** it drops the type term and indexes block 0's tail via `cs:697E = 00 00 80 C0 E0 F0 F8 FC`, and plotted those offsets are a 6x4 blob, then 2x3, 2x2, 1x2, 1x1 — a deliberately typeless distance-simplification series, so the nearest creature gets its own art and everything further out gets a generic silhouette. An earlier reading the same day called block 0 empty and concluded only the nearest creature was drawn; block 0 merely starts with a zero byte, so a parser beginning at offset 0 sees nothing, and plotting the shapes settled that better than the screenshot that had been scheduled to settle it | agent | **closed** | the dungeon sprite pass, any dungeon bestiary |
| G54 | **dungeon creatures spawn but do not walk.** `asm/147F`'s formula is implemented and validated (54/54 slots against two captures), so a dungeon is populated on entry. What is missing is the movement half: the overworld sweep `u2_ai_creature_turn` indexes the grid as `y*64 + x`, and pointing it at a dungeon page would stamp the wrong cells and corrupt the page it is standing in. So the engine leaves dungeon creatures where they spawned. **What would close it:** a dungeon-shaped twin of the sweep that reads passability off the paged grid (`0x80` refuses, a stamp refuses) and stamps through `u2_world_dungeon_put`. The pursuit vector is already decoded and does not care which grid it is on — **this last clause was wrong and is the interesting part.** The dungeon has its OWN step vector, `asm/152E`, reached only from `asm/1151`, and it differs from the overworld's `asm/4F11` on purpose: `asm/4F11` doubles the difference twice before taking its sign, and that x4 is what makes an overworld creature take the short way round a 64x64 torus. A 16x16 page has edges and no wrap, so copying the overworld vector would have been a bug. Four differences, each a byte, and none of them guessable from the overworld side: the vector (`152E`, no doubling), the grid (indexed by the creature's own floor byte `[DI+0x1B7]`, which in a dungeon is `slot >> 1` = floor 0..15 and NOT a kind index, gated by `asm/143C` against `[DS:25]`), the wounded behaviour (under 15 HP the vector is NEGATED and the creature flees, where the overworld regenerates it), and the stamp (a nibble, not a cell: clear the old cell's low nibble, OR `mon_type` into the new one). `asm/1151` was named `dungeon_darkness` from its two strings and is now `dungeon_turn`, because the strings were the last third of it. `asm/143C`/`144C`/`1451` are one function the Phase 2 map cut into three, the same failure that hid `cmd_I`/`cmd_D`/`cmd_K`. Engine: `u2_dungeon_creatures()`, gated by CP7 section 6b as a DIFFERENTIAL between two runs (entry, and entry plus one tick) because `--dump-state` emits one record per process; walker `(3,5) -> (4,5)`, and shown going red by emptying the loop bound. **Two things it does NOT close.** What a creature does on REACHING the player is runtime `0x1508` in `asm/12DB_dungeon_events` and is still undecoded, so the engine declines the move as the overworld does. And `asm/147F` runs every turn inside `asm/1151`, which means an emptied slot refills on the next tick — a dungeon cannot be cleared by fighting. That is what the bytes say and it is now visible in play, which is a behaviour change from the previous build and worth a playtest verdict | agent | **closed** | dungeon combat pressure, the food-per-turn balance |
| G55 | **does a refused step onto a swamp also cost 5 HP?** The effect ladder (`asm/248B` -> `asm/2495`, the −5) sits at the ENTRY of `asm/2423`, ahead of the food charge and ahead of the verdict, so the bytes say a mounted player turned away from the shallows by `asm/2626` still pays the 5 before the `--INVALID MOVE!`. The engine implements the narrower accepted-step claim (the owner's three controls are all satisfied by it) and says so at the call site in `overworld.c`. **Agent-drivable:** seed a horse tile beside shallows as in `captures/spice86/g40-horse-shallows`, hold `DS:13 = 0x22`, read `DS:51/52` before and after a refused step onto the `0x04`; the same arm on foot against a mountain is the no-effect control (a rejected step onto a NON-charging tile must stay free, or the charge is just per-attempt). If it pays, the two calls at the step site move above the verdict branch and nothing else changes. Opened 2026-11-02 while executing `docs/handoff-tile-effects.md`. | agent | **open** | the tile-effect ladder's position inside `asm/2423`, CP5 E1 |

| G56 | **~~does the wound flash also fire on the player's own attack?~~ CLOSED 2026-11-02 by the owner's clarification plus a motif scan of the same capture.** Owner: "I flashed when he hit me, and he flashed when I hit him." Frame 00257 in `captures/spice86/wound-flash-20261102` shows the identical ring pattern at span offset 2836 — five scanlines above the player's fixed 3237, i.e. the orc's cell — ~2.5 s after `CMD: ATTACK--` enters the log, with the player untouched. The ring marks the **hit unit's cell in both directions**. Mechanism fits the bytes: `asm/5255` is one of four fixed-position wrappers (`5255/525E/5267/5270`, each `mov $imm,%ax ; mov $imm,%bx ; jmp` into the shared renderer `asm/527F`→`asm/4C1E`, BX selecting a window of the segment table at `cs:[BX+0x48DC]`). **Still open inside the closed:** which variant the attack path calls (no callers of the other three found in `asm/`), the wrapper-to-cell mapping, and whether a swung-and-MISSED attack also flashes (untested). E1's wiring hooks both paths, each at the victim's cell. | agent | **closed** | E1 wiring, `asm/5255` header |
| G57 | **~~what tile does the original draw for cells outside a site sector's grid?~~ CLOSED 2026-11-02, same day, by bitmap match — it is map byte `0x08`, art index `0x02`, the ground tile.** The filler cell cropped from `03-at-row63.png` is identical to `tmp/tiles/tile_02.png` at Hamming distance 0 (11 set pixels, a sparse diagonal scatter), and a cell-by-cell census in the same frame shows a real town `0x08` cell and an off-grid cell agreeing exactly: 245 black + 11 cyan each. So the original fills past the edge with **the sector's own ground tile**, which is why the owner read it as grass and not as a glitch — it *is* grass, the same tile the town is mostly made of. No new capture was needed: the method was to crop a 16x16 cell from the existing frame, take each tile's set-pixel mask (background = its most common colour) and pick the nearest of the 64 extracted art records. The same pass measured the byte→art mapping inside a site for the first time — 16 distinct map bytes in one frame, every one landing on `byte >> 2` at distance 0 — so `docs/art-index.md`'s transform now holds on both sector classes rather than only on the one it was derived from. Ruled out along the way: an out-of-bounds read of the grid (the bytes after `DS:2800` are a string table) and the wrapped north strip (the town's sign band never appears) | agent | **closed** | `asm/5ABB_sub_5abb`, `asm/5AB1_view_origin_by_mode`, `engine/src/core/view.c`, `docs/art-index.md` § 1, `docs/moongates.md` § 8 |
| G58 | **which picture underlies each couplet of the demonstration, and where do PICDNG, PICTWN and PICOUT appear?** The engine now shows the splash pictures (`engine/src/core/pic.c`, gated by `tests/pic_check.c`), and two things about them are inferred rather than observed. First, the demonstration: a one-second-sampled capture shows `PICSPA` (starfield) first and `PICMIN` (pyramid) for the rest of the poem, which is not fine enough to say **which couplet changes the picture** — `title.c` currently switches after the first couplet and says so in a comment. Second, `PICDNG`, `PICTWN` and `PICOUT` are referenced by name in the executable (file offsets `0x5c3`, `0x63d`, `0x72a`) and are used by neither the intro nor the demo, so **where they appear is unknown**; candidates worth testing are the story shown when a new character starts, the endings, and the `CAST` region where one PIC name string also sits. Note `PICCAS` is referenced at `0x6ba` and has **no file in the pack**, so one load can never find its data. **Agent-drivable** with the recipe in `docs/spice86-capture.md`: start headless, screenshot at 200 ms through the intro and the `'D'` demonstration, and match each frame against the six decoded pics by colour class over the top 160 rows only (the poem text sits below them). Beware that injecting `D` is flaky — it was swallowed twice during the capture that produced this item, and a press that lands while a hold is running does nothing | agent | **low** — the engine already shows the two pictures it has evidence for; this refines the demo and finds the missing sequences | `engine/src/core/pic.h`, `engine/src/game/title.c`, `captures/spice86/splash-look`, `captures/spice86/splash-seq` |
| G59 | **~~the rocket's survivable tile has never been demonstrated~~ CLOSED 2026-11-03** by `captures/spice86/g59-grass-landing`, which landed on `0x08` at `(1,10)` in sector 40 with HP untouched. The framing of this row was the obstacle twice over. First it assumed the rule was "land on grass", and it is not — the rule is "land on one of the grass cells the march happens to pass over". Then the first closure under-counted the march: it stopped at the first bottom-to-top wrap and reported **two cells on Earth** and seven in world X. Corrected 2026-11-04 (owner challenge, then measured): the march `y+=2 ; x+=1 on multiples of 8` wraps y at 64 and shifts x east by 8 on every wrap, so it cycles **256 distinct cells**, not 32. Intersecting the full march with `mapx40`/`mapx90` (survival test `(tile & 0x7f)==0x08`) gives **28 grass cells on Earth** and **62 in world X** — plenty, not a handful. The observed landing `(1,10)` stands; the two live landings `(7,62)` and `(5,44)` were simply in the first sweep because the player pressed early. That the march wraps is straight from `asm/3B94` (`and $0x3f,%al`), and `engine/src/game/space.c:u2_space_descent_step` already models it; only the hand-count was wrong. Also measured: the dive spends no fuel, and it delivers you to sector 40 exactly as `asm/7030`'s hardcoded Earth branch says. | done | **closed** | the rocket is usable in 28 cells on Earth — a far weaker constraint than the first closure recorded | 
| G60 | **~~`YOU HAVE EXPLODED!` has only ever been observed not to happen~~ CLOSED 2026-11-03** by `captures/spice86/g60-plate-halt`, and the answer was not what this row assumed: **it is not a death.** With PLATE (4) seeded the message printed and the game then spun on `jmp L_006c77`. HP stayed 9999, GOLD 9999, `DS:51..59` were NOT zeroed, and the passive food tick stopped between two samples six seconds apart. The death tail's signature is the exact opposite. So failing the armour gate is an unrecoverable freeze with the ledger intact, not a death — and `engine/src/game/space.c` had been written an hour earlier assuming lethality, which a screenshot alone would never have exposed. Second finding from the same run: the check is not on the launch path at all; a PLATE character launched, got `HYPERWARP ENGAGED!` and kept ticking. | done | **closed** | armour >= REFLECT is a hard requirement for space, and failing it is a freeze rather than a death, which changes what a playthrough can recover from | 
| G61 | **does the ring from Father Antos gate Minax?** `tlkx92` says `FIND ANTOS, EARN THE RING!` and `ANTOS IS ON 'X'!`; `tlkx93` has `FATHER ANTOS CHANTS: YOU HAVE EARNED MY BLESSING. RETURN AND CLAIM THE RING!`. So the chain is find Antos in sector 93 on world X, earn a blessing, claim a ring, and the ring is `items_held[0]` at `DS:D6`, a slot the engine already models. What is NOT evidenced is the last link: nothing yet shows the ring being required to reach or kill Minax, and `asm/1040_minax_proximity_check` does not read it. Needs a playthrough that visits Antos and then attacks Minax with and without the ring, or a decode of what consumes `DS:D6`. **Partly agent-drivable**: the Antos visit is seedable, the with-and-without Minax test is not until CP8 exists. | both | **high** | CP8's acceptance criteria, and whether the endgame has a real progression gate or only a strong monster |
| G63 | **~~the rocket can only land in a handful of cells per world, and that is a progression constraint~~ largely dissolved 2026-11-04.** The premise came from G59's first closure, which counted only the first sweep. The march actually wraps bottom-to-top and shifts east, cycling 256 cells, and **28 of them are grass on Earth** (62 in world X) — so a rocket crossing is not the needle-thread the row claimed. What is left is a much smaller question: the survivable cells are still a fixed subset of the march, not "any grass anywhere", so aiming still matters, but there is no hard progression gate. Lowered to a nice-to-have hint, not a blocker for CP8. | paul | **low** | whether to add a dive-screen hint at all; the constraint it was meant to address is far weaker than first recorded |
| G65 | **~~does a quit request reach the creation form as "the player left" in a real window?~~ CLOSED 2026-11-06 by owner observation.** Fixed the same day from the owner's report that closing the window or pressing Ctrl-C on the creation screen acted as the Down arrow: `seam_key_wait` passed SDL's `-1` straight through, callers tested only for `0`, and the untouched `u2_key` still held the last key pressed. `engine/tests/keywait_check.c` pins the seam contract with a fake backend (8 checks, red control run first); SIGINT was proven end-to-end headless with `setsid` + `kill -INT`. **No capture directory is named, and none can be:** `SDL_EVENT_QUIT` on the creation form needs a real window, SDL cannot be driven headlessly without a script, and `main` refuses `--script` outside headless. The closure is the owner running `./u2-sdl-static --enhanced`, pressing `C`, walking a couple of fields with Down, and confirming that both the window close and Ctrl-C exit instead of moving the cursor — a direct observation, which outranks inference, not a belief that the code looked right. | paul | **closed** | nothing; the one path a script cannot drive is now confirmed by the person who found it |
| G66 | **`[~]` what does the original write into a cell when a creature leaves it?** `docs/map-format.md` § 9 left this open between two mechanisms, *departure writes `0x70`* and *load clears to `0x70`*, and said settling it needs a sector where spawn cells sit on non-paving ground. **It is neither, and the bytes answer it.** `asm/2233_sub_2233` is the departure site: `mov 0x1b7(%di),%al` + `mov %al,(%bx,%si)` restores the **per-slot saved terrain byte at `DS:1B7`** into the grid cell, and `asm/0CEE_spawn_tile_store` writes that byte at spawn from the rolled cell's own terrain, returned by `asm/50B3_map_tile_ptr`. No `movb $0x70` into the grid exists anywhere in the corpus (`grep '\$0x70' asm/` finds only `cmp`s and one unrelated `DS:23E` store), so the constant hypothesis is dead. The Apple II twin corroborates independently: `LDA $4D80,X ; LDY #$0091 ; STA ($06),Y`. **And the shipped data agrees**: across all 33 sectors and 511 live `monx` slots, the column at `monx[128..159]` — which `engine/src/core/state.h` calls `mon_kind` and glosses as "kind index" — takes exactly **two values, `0x70` (355) and `0x08` (156)**, and for 469 of 502 baked slots it equals a real unbaked neighbour terrain. It is a terrain byte, and it is only ever paving or land because that is where authored creatures stand. **So § 9 measured the paving case and generalised it.** Restoring `DS:1B7` looks exactly like "write `0x70`" in a town and nothing like it in the field. **What is left for a frame:** the visible outcome in a land sector — a creature that walked off its spawn cell should leave `0x08`, not paving. Seed into sector 00 or 20, watch one creature move, screenshot the cell it left. Agent-drivable, cheap, and it is the half the engine currently gets wrong: `u2_monsters_clear_spawn_cells` hard-codes `k_clear = 0x70` where the original restores the slot's own byte, and the 2026-11-06 "only clear a baked cell" guard produces the right outcome for the wrong reason. | agent | **high** | `mon_kind`'s name and meaning in `state.h`; `clear_spawn_cells`; § 9's open question; and whether `map_base` is needed at all |
| G64 | **does entering a site silently save the game, and do time doors?** Two halves, one cheap and one not. **(a) Site entry:** `asm/341E` (village), `asm/343F` (`-TOWN`, `-TOWER`), `asm/34B5`/`asm/34CF` (`-CASTLE`) and `asm/34E4` (`-DUNGEON`) all record the return cell into `DS:5A/5B` and then `call L_0024fd`, the save body. If that reaches disk then every site entry writes the save, and `docs/save-format.md` § 10's "crossing a site edge writes nothing" row is wrong — that row measured `mapx`/`monx` and may never have compared `player` to its seed. **Agent-drivable and cheap:** seed in a wilderness sector, walk into a town gate, diff `player` against the seed. The rocket launch was proven this way in minutes (`§ 10` has the A/B). **CLOSED 2026-11-03, same hour it was written: yes, site entry saves.** Seeded into sector 30 at `(34,20)`, pressed `E`, arrived in sector 31 at `(31,62)`, and the run's `player` differed from the seed by 5 bytes (food `999999` -> `999989`). The saved sector is **30, the one you came from**, so the auto-save pins the outdoor state — which is the mechanism behind dying in a town and waking outside it. § 10's "site edge writes nothing" row is corrected there. **(b) Time doors:** the owner states that a time-period transition also forces an automatic save, so a bad jump is permanent too. Not found in the bytes — the five unexplained save-body callers turned out to be site entries, and `docs/moongates.md` evidences no transition save. Needs either a decode of the rotation routine (still undiscovered; `asm/2708`–`2775` was refuted) or a live time-door crossing with a `player` diff either side. **(c) The engine:** if site entry and launch both auto-save, the engine currently does neither, which is a fidelity gap with real consequences — the original's save-on-launch is what makes a deep-space stranding permanent, and an engine that does not save there lets a player undo what the original would not. | agent | **high** | whether the engine's save triggers are faithful, and what a playthrough silently mutates on disk |
| G62 | **what byte tells the rocket that it is in orbit rather than on the pad?** **CLOSED 2026-11-03: there is no discriminator byte.** `asm/6999_space_status` **is** the in-space command loop, entered from exactly one place (`asm/38B0_cmd_L` line 177, `call L_006b99`), and it has **no `ret` at all** — it leaves only by jumping out: `H` into `asm/6DFD_hyperwarp_flow`, `L` into `asm/7030_space_collision`'s dive arm, whose `ret` pops asm/6999's own return address and lands back in asm/38B0 right after the call, in the descent. The state is *which loop owns input*, and that lives on the stack, not in a byte. That is why every pad-vs-orbit byte came back identical: the search was for a flag that never existed. Settled on the way: `L` while landed in a wilderness sector **launches** and writes the return triple `DS:69=1`, `DS:6A/6B` = the launch cell, which is the `[DS:6D]==0 AND [DS:4A]==0` block `asm/38B0` described and which had never executed; the off-course gate confirmed a third time on a third fuel value; and `REQUEST DENIED!` confirmed live from deep space with position, fuel and sector all untouched. **Engine: `commands.c:launch_rocket` now runs the nested loop** (launch preamble to space command loop with readout/CMD:/arrows/H/L to dive to descent), so the southward march fires on the dive, not on launch. Two asm/6999 entry behaviours stay unmodelled because the bytes and the capture disagree and guessing a site would claim a mechanism: the armour gate (G60 observed it fire from the status path, not launch) and the launch-time off-course roll (never observed to move the world on launch). | done | **closed** | the space layer's control flow, and it removes the last "faithful half" from the rocket handler |

**CLOSED 2026-11-03, statically, and the question was the obstacle.** There is no
byte, and looking for one was the mistake. `asm/6999_space_status` **is** the in-space
command loop: `cmd_L`'s rocket arm calls into it (`L_006b99`, its only caller in the
whole tree) and stays there. That loop prints `CMD: `, polls a key, and dispatches —
four arrow scan codes held in `DS:26E..271` to `LEFT`/`RIGHT`/`CLIMB`/`DIVE`, `H` to
`asm/6DFD_hyperwarp_flow`, `L` to `asm/7030_space_collision`. **There is no `ret`
anywhere in it** — zero occurrences — so it exits only by jumping out into the landing
path. The second `L` never reaches `cmd_L` at all; it is consumed by the space loop.
The state was on the stack, which is why every `DS` diff between pad and orbit matched.

That also confirms structurally what live play had already shown: the southward march
belongs to the dive, reached from the landing path, not to the launch falling through.

What the engine needs is now well-defined rather than speculative: a **nested space loop**
inside the rocket launch — readout, prompt, arrows/H/L — that breaks into the descent on
`L`. Not a discriminator byte, and not two command handlers. See `asm/6999_space_status`
for the full dispatch. | done | **closed** | the shape of `cmd_L`'s rocket arm, and the
engine's nested-loop refactor |

G14, G15 and G16 were carried in from the 2026-10-21 handoffs on 2026-10-22, where they
were open but recorded nowhere but a document being archived. See
`docs/archive/handoff-2026-10-22.md` § Triage.

G1–G5 are `docs/passability-findings.md § Open` questions 1–5; the technical spec
lives there. What follows is only the driving plan and the honest verdict.

G17, G18 and G19 were rewritten the same day they were filed, after the bestiary
join pulled up the existing monster findings. The first versions of G17 and G18
were both wrong about what is actually unknown: G17 assumed a monster HP field had
been located (it never has, and it is now known not to be stored), and G18 assumed
the type→art transform rested on one observation (it is corroborated by eight).

### G1 — art index `[ ]`
**Action:** enter any sector, take three ordinary steps, dump `DS:272` (256 B) and
the sector grid each time. Compare against both candidate transforms.
**Why the agent can do it:** pure memory read; no navigation judgement, no visual
identification. Ordinary walking is already scriptable.
**Needs owner:** no.

### G2 — food cost of a blocked step `[x]` — closed 2026-10-25: **it costs the same 19**
**Action:** stand adjacent to **water** (`0x00`, already proven blocked) and step
into it; read `DS:53..55` before and after.
**Why the agent can do it:** water is a known blocked tile, so this does **not**
depend on settling the mountain first — that dependency was assumed and is wrong.
**Needs owner:** no.

**Attempted 2026-10-25 (`tmp/20261025-213046`, screenshot
`captures/spice86/g2-blocked-food-attempt.png`). It did not answer, and both failure modes are
worth writing down before anyone else runs this:**

1. **The auto-PASS confounds any idle-gap measurement.** First reading looked like a blocked
   step costing 19 food (999999 → 999980). The control — read food three times with *no input
   at all* — showed it falling ~10 per window on its own. That is the documented auto-PASS
   (~7.5 s at `--Cycles 3000`, 10 food each) firing between the reads, not a step cost. Any
   food measurement must therefore bracket a single keypress with back-to-back reads, or raise
   `auto_pass_polls` out of the window, or it is measuring the wrong thing while looking right.
2. **`tmp/seed-townhp/player` is a hostile probe seed.** The character was **`ARMS PARALYZED!`**
   and ringed by creatures, so every direction returned `EAST--INVALID MOVE!` and the position
   never moved across six keypresses — a blocked step was never actually attempted, and the
   tight re-measurement showed food flat across three presses because nothing happened at all.
   Use `tmp/seed/player` (maxed, unparalysed) and confirm the step *lands* by reading `DS:00/01`
   before treating any food delta as a cost.

**Protocol for the next attempt:** seed `tmp/seed/player`; pick a water cell by reading the
sector grid at `DS:1800`; read `DS:00/01` and `DS:53..55`; send the one arrow that steps into
it; read both again immediately; accept the sample only if the position changed (for the
control case) or the target was provably blocked (for the blocked case). Repeat three times.

**Closed 2026-10-25 by `captures/spice86/g2-step-food/`** (run `tmp/20261025-213254`, label
`stepcost`, seed `tmp/seed/player`, sector 31). The protocol worked and the answer is that
**a refused step costs the full 19**, the same as an accepted one:

- `Left` into the blocked cell at (49,24): `DS:00/01` unchanged, food 997904 -> 997885,
  and `blocked-west-zoom.png` shows `CMD: WEST--INVALID MOVE!`, which is the proof the
  keypress landed rather than being dropped. A second refused step gave the same -19.
- `Down` into an open cell: moved, 997875 -> 997856, also exactly -19.
- Control: three reads with no input, flat.

The discriminator that makes these safe against the auto-PASS confound is arithmetic:
the PASS spends 10 and a step spends 19, so a delta of 9 mod 10 is exactly one step
charge and 8 mod 10 is exactly two, whatever passes fired in between. The mechanism is
`asm/2423_passability` subtracting at `asm/2599_food_drain_walk.asm` *ahead of* the tile
verdict, which all four walk arms call. Engine consequence: the reject arm used to charge
nothing, and the accept arm charged 13 rather than 19 (see G34 and
`docs/phase4-state.md` CP4). Both are fixed and gated by `CP4 G2` in `engine/tests/run_cp4.sh`
with `engine/tests/g2_step_food.py`.

### G3 — who writes `DS:2CB`/`DS:2DE` `[x]` — closed 2026-10-28d as a DOS-port artefact, unanswered
**Closed without a capture, and that needs saying plainly because the item's own rule is "only a
named capture closes an item".** The rule exists to stop beliefs closing questions. Here the
question itself dissolved: the item was load-bearing only for C5/F3, and `0x5C >> 1 == 0x2e`
explains the mode with no help from those bytes. The crosswalk was then asked whether the Apple
build has the same indirection — the one static tiebreaker available — and it does not, on three
shape scans (`asm/5B71_mode_table_copy` carries the detail). So there is no second witness to
find, and an afternoon of watchpoints would buy a fact nobody needs.
**Still true:** the writers are unknown. If a mode discrepancy ever shows up that `>>1` cannot
explain, this reopens and the action below is the right one.
**Action (if reopened):** capture both bytes before and after each of: sector entry, boundary
crossing, boarding, alighting, stepping onto a field. Boarding is stageable —
`mapx03` has the four-vehicle depot at (36–39, 24), so `--seed-map`/`--player` can
put the character next to it.
**Why the agent can do it:** all reads; the events are reachable by scripted
movement. **Risk:** if the writer is only triggered by an event we have not
thought to stage, we get "no change observed", which is weaker than a real answer.
**Needs owner:** no, but a negative result should be treated as inconclusive.

### G4 — mode byte `0x2e` `[ ]`
**Action:** one ordinary step and one step onto a force field, dumping
`AL, DS:13, DS:15, DS:18, DS:29, DS:2CB, DS:2DE` plus the destination tile.
**Why medium:** a force field has to be *reached*. One is on record at fight turn
2909 in sector 03, so the location is known — but navigating to it reliably is the
same hard problem as G0.
**Needs owner:** no, provided G0 lands first.

### G5 — the mountain `[ ]`
**Action:** stage a map with a candidate tile adjacent to the player, attempt the
step, record whether the ladder rejects.
**Why not fully agent:** the *rejection* is measurable automatically; the
identification — "is the thing being rejected what a player calls a mountain" — is
a visual judgement. The owner's earlier answer (art `0x04`, diamond lattice) is
already on record, so this may be a formality rather than a session.
**Needs owner:** a 30-second look at one screenshot, not a session.

### G6 — single-byte underflow reachability `[ ]`
**Action:** get `keys`, `torches` or `tools` to zero deliberately, then trigger the
decrement anyway and read the byte. The census proved the subtract sites have **no
borrow test**, so the bytes predict `0 − 1 → 99`; nothing has ever observed it.
**Why medium:** reaching zero may take a long scripted grind, and an upstream
"do you have one?" guard may make it unreachable — which is itself the answer.
**Needs owner:** no. **Value:** converts the census's most speculative claim from
inferred to observed, or kills it.

### G7 — stat cap 99 wrap `[ ]`
**Action:** raise one stat to 99 (the OFFER-GOLD path in `asm/3C2E` and the cleric
raise in `asm/1869` both add without a clamp), then raise it once more and read.
**Why medium:** needs gold and a repeatable raise path.
**Needs owner:** no. **Value:** this is the owner's recalled behaviour; observing
it would be the cleanest possible confirmation of the whole BCD-wrap line of work.

### G8 — identify `DS:96+i` `[ ]`
**Action:** buy an armour piece, read `DS:96+i` before and after; then have a thief
steal one and read again.
**Why high:** three asm sites already do BCD arithmetic on `0x96(%di)` beside
`0x76(%di)`; one purchase confirms or kills the owned-armour hypothesis in minutes.
**Needs owner:** no. This is the cheapest un-closed item on the list.

### G9 — dungeon light timer `[ ]`
**Action:** sit in a dungeon until the light counter reaches 0 and capture the
frame. `dungeon_capture.py` already does change-triggered frame recording.
**Needs owner:** no, though getting into a dungeon and surviving is the fiddly part.

### G10 — dungeon geometry and the wireframe renderer `[H]`
The largest unexplained surface. Dungeons are first-person 3D; the tile renderer
does not cover them. `mapx35` uses only 7 distinct bytes, all multiples of `0x10`,
and only 7 of 16 possible combinations occur — a flag or small-enum encoding, not
an art index. Leads are in the backlog item.
**Why not the agent:** navigating a first-person wireframe maze to reach specific
geometry is exactly the skill a scripted probe lacks, and the renderer is not in
the decoded tree at all (no pixel-plot path anywhere in `asm/`).
**Also owns:** T9 — the `original/monsters` stroke table at `DS:2900` has no
consumer until this is decoded.
**Needs owner:** yes.

### G11 — `A`–`Z` dungeon byte decode `[H]`
Biggest prize, same blocker as G10. Leads recorded in the backlog; do not burn an
unattended night on it.
**Needs owner:** yes.

### G12 — force-field animated render `[H]`
What drives the white/blue/purple animation is not in `asm/24B5` (it only flashes
and sounds). Recognising an animation needs eyes on a screen.
**Needs owner:** yes — a short video or a few timed screenshots, not a session.

### G13 — the `mapx30 (0,0)=0x50` rocket `[ ]` *(low value)*
One cell, one capture, four dead ends already (see findings § 13). Only worth
re-checking opportunistically if a sector-30 session happens for another reason.
**Do not schedule anything for this.**

---

### G17 — the monster HP model `[ ]` *(static prerequisite first)*
**What is already settled:** monster HP is **not stored**. `tools/hp_table_scan.py`
searched the executable for a per-creature HP table across 13 encoding x indexing
combinations — raw byte, 16-bit LE and the hundreds-first BCD pair the player's own
HP uses, indexed by art and by the type/sprite values that fold to an art under
`>>2` — and nothing reached even a "all but one" bar. `original/monsters`
(64 records x 34 B) was tested the same way and is coordinate pairs, not stats, so
T9 loses that candidate. A loose threshold would have invented a table; the negative
is the useful answer.
**What the ranges say instead:** the seven dungeon HP ranges are two exact
arithmetic progressions, `min = 17 + 32k` and `max = 153 + 32*(k//2)` in
dungeon-table order — 14 data points, four constants, 7/7 on both. The overworld
values lean the same way: `32 x {1,2,2,4,5,6,7}` with Balron at `255` (`0xFF`, a
byte ceiling where 256 would land) and Orc's 16 the one outlier. Dungeon HP is
computed from creature index and depth; overworld HP has no depth, so it has no
range. Grade: **measured against third-party numbers**, which is not the same as
measured against the binary.
**Static step (do this first, no emulator):** find the computation in the spawn and
combat code. The unexplained fifth record field `DS:0x177+i` is the obvious
candidate — it is written from `asm/0D4F`, `asm/0D6F`, `asm/0DAF`, `asm/0DC4` and
`asm/147F`, and `asm/212A` does `incb 0x177(%di)`, which reads like a per-monster
counter rather than a position.
**Live step (then this):** the compound at sector 00 (30,32) already holds 31
simultaneous monsters and the spawn RNG is seed-repeatable, so one session can dump
all 32 slots at spawn and tie the formula to a live record. Memory reads sustain
about 250 req/s.
**Settled from the existing capture corpus, not a new run.** Reading the monster
record out of every dump in `captures/spice86/` showed the arms ARE the live path:
about 40 captures have live HP exactly equal to the arm immediate with zero
mismatches, and `play-test` (the landbridge session) shows a damaged Sea serpent at
44 of 64. The sector-0 compound reading 255 for everything is **not** an override and
never was -- `monx00[64:96]` is all `0xFF`, and placed monsters load HP from that table.
See `docs/monster-spawn.md` § 1, which supersedes the "area override" framing used here
earlier the same day.

That same dump confirms the townsfolk and validates the third-party ranges: Guard
`0x60` at **255** (codex right, Gamer Corner's 500 wrong), Merchant `0x68` at **30 and
50** (codex 5-50), Cleric `0xF4` at **25/30/40** (codex 10-60). Town NPC HP exists and
sits inside the published range; what remains unknown is the *assignment* site, since
no arm covers them.

**Genuinely still open, and narrow:** the rule by which an area raises HP above the
base (castle vs compound vs overworld), and why the sector-0 compound saturates every
slot to 255. That is a static question about area setup, not a capture question.

**Needs owner:** no. `--auto-play` and `tools/spice86_type.py` drive a headless run
end to end; no display, no keystrokes through X.
**Blocks:** nothing for the HP column, which is measured and live for ordinary
spawns. The area-override rule is what CP5 combat fidelity needs.

### G18 — which record field drives the art `[x]`

> **CLOSED 2026-10-23 by `captures/spice86/art-index/`.** The answer is **neither record
> field**: the renderer reads the **sector grid cell** at `DS:1800[y*64+x]`, and applies
> `>>2` to it. Proven on the only type/cell disagreement in the corpus (sector 00 slot 6,
> type `0x34`, cell `0x38`) where the drawn tile matches art `0x0E` = `cell>>2` at
> **256/256 pixels** and art `0x0D` = `type>>2` at 171/256, with the viewport geometry
> independently confirmed on 18 further slots. Full write-up and the one-sample caveat:
> `docs/art-index.md`. The text below is kept as the record of how the claim got wrong
> twice before it got right once.
**What is already settled:** the record is three parallel byte arrays, stride 1,
32 slots — `x = DS:0x137+i`, `y = DS:0x157+i`, `sprite = DS:0x1B7+i`,
`active = DS:0x197+i` — measured, and the earlier stride-`0x20` guess is recorded as
wrong. Two of those fields are candidate "type" bytes, and **both** fold to the
right art under `>>2`:

| field | measured value | `>>2` | bestiary name | documented behaviour |
| --- | --- | --- | --- | --- |
| `0x197` | `0x34` | `0x0D` | Daemon | leg paralysis |
| `0x197` | `0x38` | `0x0E` | Devil | arm paralysis |
| `0x197` | `0x3C` | `0x0F` | Balron | sleep |
| `0x197` | `0xF8` | `0x3E` | Wizard | magic missile |
| `0x1B7` | `0x60` | `0x18` | Guard | drops 2 keys |
| `0x1B7` | `0xF0` | `0x3C` | Fighter | drops torches |
| `0x1B7` | `0xF8` | `0x3E` | Wizard | magic missile |
| `0x1B7` | `0xFC` | `0x3F` | Thief | steals items |

**Correction, 2026-10-22 (same day).** The table above is the retracted version. The
"eight for eight from two independent fields" claim is **wrong**: `[DI+0x1B7]` is not a
sprite but a small kind index (`0x08`/`0x0C`), which is what the spawn guards compare
against. So there were never two fields — there were **four agreements from one field
relabelled**, and they are arithmetic only. The spawn arms contain no shift and no mask;
they store the type as a plain immediate (`mov $0x30,%al; mov %al,0x197(%di)`).

**What is settled now, and it is more than the transform:**

* The `0x40` contradiction is **closed** — the thief is type `0xFC` with HP 32, `0x40` is
  the empty sentinel, and "the thief is monster type `0x40`" in
  `docs/spice86-live-findings.md` is wrong.
* **`view.h` is not defective and L2 was misframed.** `u2_view_index` returns the staging
  byte `(map_byte>>1)&0xFE` and `u2_view_art` halves it; `((b>>1)&0xFE)>>1 == b>>2` for
  every one of the 256 byte values, checked exhaustively. The engine already implements
  `>>2`, in the two-step the original uses. Anyone tempted to "fix" it should read
  `view.h:78-94` first.
* **New open question:** the dungeon `type` field is a **different encoding**, not the
  overworld type byte. In `dungeon-35-live` the active slots hold `0x01`/`0x02`, not the
  `0x2C`–`0xFC` of the overworld, and `asm/147F` builds HP as `or $0x10,%al` rather than
  storing an arm immediate. So dungeon monsters are a separate representation and the
  overworld grid test does not apply to them.
* **A term I invented and must not survive:** there is **no "airport mode"**. The
  `airport*` capture directories are names from a prior session chasing an owner
  recollection of "a town with an airport"; the game has plane and rocket *tiles*
  (`mapx03` depot, plane = map byte `0x4C` art `0x13`, landing gated to grass `0x08`),
  not a mode. `airport32`'s 4 KB grid at `DS:1800` is **byte-identical** to
  `dungeon-35-live`'s — it is a dungeon capture with a stale name. Any claim of the form
  "dungeon and airport mode have no grid stamping" was wrong twice over.
* **Grid stamping holds at scale in the overworld:** `DS:0x1800[y*64+x] == type` for 330 of
  387 monster cells across 49 captures. The base itself was already documented twice
  (`spice86-live-findings.md:85` "Sector buffer is loaded at DS:1800" and
  `phase4-overnight-cp5-report.md:393`), so this was a re-derivation, not a discovery.

**Action:** map the dungeon encoding — what `0x01`/`0x02` mean, and where dungeon objects
are drawn from. Do not reuse the overworld grid test on dungeon dumps.
**Why the agent can do it:** both dumps are already on disk; this is a search, not a capture.
**Needs owner:** no.
**Blocks:** naming monster *types*. Art indices are named already; types are not, and a
wrong field would attach every name to the wrong creature. Sibling of G1.

### G19 — the dungeon set is outside the tile join `[ ]`
**Why it exists:** the bestiary join named **zero** dungeon arts, and that was
design, not a miss — every codex dungeon image is a `64x41` screenshot crop, so the
eligibility rule rejected all seven. Worse, the dungeon is a wireframe renderer, and
`tools/monster_shapes.py` already suspects `original/monsters` (64 records x 34 B of
coordinate pairs) is where the outlines live.
**The trap:** dungeon Orc, Daemon and Balron are **different creatures from their
overworld namesakes** — different art, and HP `17-153` / `177-217` / `209-249`
against `16` / `64` / `255`. Town is the opposite case: Thief and Wizard are the
*same* creature as on the overworld, same image and same HP, reused as NPCs.
So a name earned from the overworld tile table must never be carried into a dungeon
row. Note also that the codex's dungeon Daemon image is filed as
`U2DungeonMinotaur.png` — the source disagrees with itself about what that creature
is called.
**Action:** decode the stroke table (this is also T9), then identify dungeon
creatures from their drawn outline.
**Why the agent can do it:** static decode plus a dungeon session already scriptable.
**Needs owner:** no, though G10 (dungeon geometry) does.
**Blocks:** any dungeon bestiary, T9, the dungeon renderer.


## Session planning — group by setup, not by question

One launch can close several items. Cheapest order:

1. **Sector 20, fresh character** → G1, G2, G5, G8. No special staging.
2. **`mapx03` vehicle depot staging** → G3, G4.
3. **Grind session (gold, zeroed counters)** → G6, G7.
4. **Dungeon 35** → G9, and the first probes for G10/G11 with the owner driving.

G0 must land before any of the rejection-based items (G2, G4, G5, G6) produce
results worth recording.

---

## Maintenance

When a finding anywhere in the project turns out to be **blocked on gameplay** —
including one that says "inferred from ISA semantics, never observed" — the same
commit that records the finding adds an item here with a who/confidence verdict.
When a session closes an item, tick it, name the harvest directory, and move the
detail to § Closed.

### G26 — the creature-table layout `[x]` *(closed same day — the question was bogus)*
**Outcome:** `docs/monster-spawn.md` § 2 was right all along. The layout was never
wrong; the script that claimed otherwise sliced `monxNN` from offset 128 instead of 0,
four fields out of position, and every "structural impossibility" it reported was an
artefact of that one constant. A fresh live capture (sector 31, `G23MAX`) matches
`monx31` at **32/32** on `hp`, `type` and `role`, and 30/32 on `kind`, with `x`/`y`
under-matching only because creatures walk after spawn. The post-mortem, including the
two process rules that came out of it — check what the code reads before declaring the
code wrong, and give every new script a positive control — is § 10 of that document.
**Cost of the false alarm:** the CP5 creature stamp was compiled out for hours, W2/W4/W5/W6
were declared blocked, and the owner was told the reconstruction had a defect. It did not.

### G27 — what the game does after the death announcement `[ ]` *(agent-drivable, ~20 min)*

**Question:** `asm/0C62..0C99` zeroes `DS:51..59` and prints `<NAME> IS DEAD!`, and every
site that can kill jumps into it. Nothing has ever recorded what happens *next*. Does the
walk loop accept another command? Does it go back to the title, wipe the save, or sit
there with a dead character who can still walk?

**Why it matters now:** the engine prints the announcement and carries on playing, which is
a guess by omission recorded in `engine/src/game/overworld.c:u2_death`. It is also the one
thing between `death1.png` and a CP5 gate that ends a fight the way the original does.

**Who can do it:** an agent, unattended. The recipe already exists — the capture that
produced `captures/spice86/cp5-weak-fight/death1.png` was a seeded low-stat character
driven headless. Seed 1 HP, stand next to something hostile until the death line prints,
then keep sending keys and screenshot after each. `--patch-arrows` plus
`tools/spice86_type.py`, and confirm every key landed by reading DS, never by the send
returning ok.

**Success criterion:** a named capture directory holding the frame *after* the
announcement, plus a verdict on whether `DS:00/01` moved under a direction key.

### G28 — what makes a world-map cell a town gate `[ ]` *(agent-drivable)*

**Question:** `E` (ENTER) dispatches on `[DS:14] >> 1` — 6 town, 7 tower, 8 castle,
9 dungeon — and refuses with ` WHAT?` everywhere else. `DS:14` is `mode_a`, copied from
the `DS:2DF` region by `asm/5B71_mode_table_copy`, and that region has no direct writers
(`docs/passability-findings.md` F3). So the open question is narrow: **what is `DS:14` at
a cell where `E` works, and how does it relate to the tile byte under the player?**

**Why it matters:** this is the last thing between the engine and "walk into a town". The
entry machinery itself is six steps and a digit and is written up in
`docs/sector-rulesets.md` § 6; the sector-edge leave path is already fully proven. Only
the gate value is missing.

**Who can do it:** an agent, unattended, and the cheap version is not a 4096-cell sweep.
Start a world sector (`mapx30`), and at each of a dozen cells of *different tile bytes*
read the mode block `DS:12..DS:18` over the HTTP API and press `E`, recording whether the
answer is ` WHAT?`. Twelve samples of (tile byte, DS:14, DS:15, DS:18, E-answer) is
enough to see whether `DS:14` is a function of the tile at the player's cell or of
something else entirely — which is the answer that actually unblocks the work.

**Success criterion:** a named capture directory holding the sample table, and a verdict
on whether `mode_a` is derived from the tile byte.

### G32 — is food consumed while sailing? `[x]` *(closed 2026-10-29 by bytes + `captures/spice86/stepcost/`)*
**Answer: no, and the same is true of the rocket — but for different reasons, and the horse and
the plane DO pay.** `asm/253B_food_mode_dispatch` branches on `[DS:13]` before anything else and
the ship arm (`asm/2619`) and the rocket arm (`asm/2542`) reach no drain at all: the ship because
it has no subtract, the rocket because it rejects the step before reaching one. Horse and plane
pay 38 (two 19s, `asm/2626`). The guard the item asked for is the mode dispatch itself, and the
measured figures are the `stepcost` forced-`DS:13` samples recorded in
`asm/2626_food_drain_mounted`'s header. Engine: `u2_overworld_step_food()`, gated by CP5 V1.

**Owner recollection, recorded as `recalled` and treated as a pointer, not a conclusion:** food
is **not** consumed while sailing on a ship, and because killing enemies with cannons is still
possible at sea, this drastically lowers total food consumption. The first half is now settled;
the cannon half is a dungeon/tower question and is parked.

**Why it matters to the engine:** if true, food spend is gated on the vehicle byte, and the
engine currently spends food on the same terms whether the player is on foot, aboard, or in a
rocket. That is a whole class of wrongness in the survival model, and it is the difference between
sea travel being an escape and being a slow suicide.

**How to test it, statically first:** the walk loop's food site is the one T2 settled (PASS spends
10 via `DS:53/54/55`, underflow zeroes `DS:51..59`). Read the instructions guarding that spend and
look for a test on `[DS:13]` (the stamped sprite, `>= 0x78` means on foot) or `[DS:14]` (vehicle
code, `map_byte >> 1`, CP5 ticket C7: `0x48` is the ship). If the spend is guarded, the recollection
is confirmed without a capture. If it is not, the recollection is still worth a live run — and the
live run is the one that outranks this file.

**Live protocol, if needed:** `tools/u2_mkprobe.py` a sector with a ship tile under the player,
`--seed-map` it, board with `B`, then step and read `DS:53..55` back-to-back around each keypress.
Accept a sample only if `DS:00/01` changed — see G2's failure before repeating it.

### G34 — food vs mode of transport, and the gremlin's fixed 100 `[~]` *(claim 1 and the
internal conflict are settled; the gremlin figure is untouched)*
Third-party wiki data supplied by the owner. Grade: **third-party hypothesis — the bytes get to
veto**. It is worth more than the last one because it is checkable against code already decoded.

**Claims, in the order they can be settled:**

1. **Food is not consumed while aboard a frigate, a bi-plane or a rocket — except when `PASS`
   (SPACE) is used.** This refines the owner's own earlier recollection ("food is not consumed
   while sailing"), which was directionally right and missed the PASS exception. **Testable
   statically right now:** the food-spend site is `DS:53/54/55` under the routine T2 settled, and
   the vehicle byte is `[DS:14]` (`map_byte >> 1`; `0x48` ship, `0x4C` plane, `0x50` rocket, per
   CP5 ticket C7). Look for a guard on `DS:14` around the spend. If the spend is ungated, the
   claim is false in this build or the exemption lives in the caller.
   **Settled 2026-10-25, `captures/spice86/g2-step-food/`.** The guard exists and it is on
   `[DS:13]`, the same byte in the `>> 1` domain (`0x24` ship, `0x26` plane, `0x28` rocket),
   at `asm/253B_food_mode_dispatch.asm` inside the passability routine. The verdict is
   **half the claim**: the ship and the rocket are exempt (0 food per step, measured), the
   plane is **not** — it pays, and it pays twice (38), because `asm/2626_food_drain_mounted.asm`
   calls the delay tail that falls through a second subtract. The horse pays 38 too. On foot
   it is 19. The claim's `PASS` exception is real and separate: PASS always costs 10 from
   `asm/090D_sub_090d.asm` whatever the mode. Grade: the exempt/charged split is observed under
   a forced `DS:13`, and `DS:13` is volatile, so a real boarding capture would still be worth
   having — but the branch structure is bytes, not inference.
2. **Food declines while in dungeons.** Dungeons are parked (AGENTS.md § 3), so this is recorded
   and deferred, not testable now.
3. **Gremlins steal a fixed 100 food units each turn.** `asm/12DB_dungeon_events.asm` carries the
   string `A GREMLIN STOLE SOME FOOD!`, so the site exists and is named. **Check the subtract
   against 100** — a fixed 100 versus a random amount changes the engine's survival model, and
   CP5 ticket C6 already learned once that a "fixed" theft in this game was actually a BCD
   decrement of 1 on a rolled slot. Same trap, worth re-measuring rather than assuming.
4. **"Mode of transportation no longer affects the rate of food consumption"** (vs Ultima I).
   Read against claim 1 this is about *rate while moving* (foot vs horse = same), not about the
   aboard exemption. Worth keeping the two separate in anything written, because they can look
   like a contradiction when merged.

**And a conflict on our own side, which outranks all of the above: RESOLVED 2026-10-25, and
T2 was the false one.** The step-cost measurement taken 2026-10-28e (`tmp/20261025-213254`,
maxed seed, sector 31) showed food falling by **19 per successful step**, and CP4b T2 had
recorded the opposite: "the walk loop calls no food drain". Re-run with the auto-PASS
excluded from the window — one arrow, reads back to back, `DS:00/01` confirmed — the drain
survives at exactly 19, accepted and refused alike, and so does the byte-level explanation:
all four walk arms `call 0x2423`, and `asm/2599_food_drain_walk.asm` / `asm/2626_food_drain_mounted.asm`
subtract `0x19` inside it, ahead of the verdict. **T2's "no per-step tick" is retracted**
(see `docs/phase4-state.md` CP4). The engine was under-charging every step twice over: it
charged nothing on a refusal, and it charged 13 rather than 19 on an acceptance, because
`U2_FOOD_PER_MOVE` was written `19u` where the asm's immediate is `0x19` and decimal 19 into
a packed-decimal subtract is thirteen. Both fixed, both gated.

Still open on this item: **claim 3**, the gremlin's fixed 100 food, untouched.

**Needs owner:** no. Claim 3 is static or headless-testable; 1 and the conflict above are closed.

### G33 — McDonall's take-out window: can food be stolen without limit? `[ ]` *(owner-recalled; needs a town seed)*

**Owner recollection:** McDonall's is special because it is ridiculously easy to steal amazing
amounts of food from its **take-out window**, so a player never has to pay for food at all.

**Why it matters:** CP5 ticket C6 measured theft as *one* item per attempt, BCD-decremented by 1,
with a silent failure and `A THIEF STOLE SOMETHING!` on success — 8 items stolen, 0 returned, in
`captures/spice86/`. If one shop can yield unbounded food, then either the decrement is per-call
rather than per-stock for that NPC, or the window is a different site from the general steal, or
the food stock is large enough that "amazing amounts" is what a decrement of 1 looks like over
many calls. All three are checkable and they imply different engine behaviour.

**What blocks it:** needs a town sector with McDonall's in it and a character positioned at the
window. `tools/u2_edit.py setpos` can move sector and cell but the shop/NPC adjacency state is not
something the editor sets today. So: identify the shop record first (static, `tlkxNN` dialogue and
the `street_*`/`list_header_*` routines), then decide whether a seed is cheap.

**Needs owner:** no, but it is the more expensive of the two.


**Ran 2026-10-25. Two named capture directories, and a correction that matters more than the
captures.**

`captures/spice86/cmd-letters-20261025/` — H, J, Y executed (proved in `ExecutedInstructions`,
not inferred: offsets `3782`, `37DC`, `4542` present). Screenshots show the CMD log reading
`HYPER WHAT?`, `JUMP...WHEE...`, `YELL WHAT?`.

`captures/spice86/cmd-letters2-20261025/` — C, M, O, S, T executed (`310B`, `3B92`, `3C2E`,
`3F25`, `4050`). `M` printed `MAGIC SPELL READY #` and waited for a spell number; `T` printed
`TRANSACT-` and the turn expired into `TRANSACT--PASS`.

**Three findings the static read could not have produced:**

1. **`cmd_Y` does not return to the command loop.** After `YELL WHAT?`, every subsequent letter
   accumulated as text on the CMD line and never dispatched — the first run's log ended up
   reading `M1CS*RT`. That is the fall-through into `asm/4551` being *not* `cmd_done`, observed.
   It is why the second run excluded Y, and it vindicates leaving N and Y unimplemented: the
   engine's `cmd_Y` would have had to invent a return that the bytes do not have.
   **RETRACTED 2026-10-29 — the observation was right and the reading was backwards.**
   `asm/4551..asm/4567` is a complete freeform input loop: spin on the key seam, echo each
   character, end on Enter (`cmp $0xd,%al` at `asm/4559`), print the Enter, then `cmd_done` at
   `asm/4565`. The letters that "never dispatched" were the yell being echoed, and the loop does
   reach `cmd_done` — it cannot do so until a CR arrives. Nothing in the range writes memory, so
   the yell is display-only. What blocked this for four days was that the loop lay cut into seven
   fragments each headed "behavior not yet reviewed", so nobody read the range as a routine; the
   owner's recollection of the manual is what sent the re-read. `cmd_Y` is implemented and gated
   (`engine/tests/cmd_yell.py`, including the swallow case this finding got wrong).
2. **`cmd_J`'s message overstates its body.** `JUMP...WHEE...` with no jump anywhere in the
   routine and no tail-call hiding one. Now observed, not inferred. The shipped manual, recalled
   by the owner on 2026-10-29, describes J as a deliberate joke — "jump up and down", for
   frustration, touching nothing. So the overstatement is the feature. See
   `docs/third-party-claims.md` § 1 and the header of `asm/37DC_cmd_J.asm`.
3. **The crosswalk was lying.** Checking all 26 entries against the current 141-harvest corpus
   found 8 grades wrong: C H J M O S Y were recorded against a 60-harvest corpus that grew
   without the lines being refreshed, and **`cmd_T` had live citations in a format
   `cmd_handlers.py` cannot parse, so it printed `--` for a handler 24 pre-existing harvests
   already contained.** The real split is **22 proven / 4 unproven**, and the four are
   **D, N, V, W** — not 12. Full write-up: `4ad5d81`.
   *(Note, 2026-10-29: that split was true of the 141-harvest corpus of its own day and the corpus
   has grown since. `python3 tools/cmd_handlers.py` is the authority for the current numbers; this
   line is a record of what that session found, not a count to quote.)*

**PARKED by the owner 2026-10-28e, with a redirect.** D, N, V and W stay unimplemented and the
remaining evidence work on them waits. The owner's instruction was to park the unproven ones and
use what is already proven instead: **22 of 26 handlers now have execution evidence**, which is
enough to test a great many mechanics against the original. That is where the effort went.

**What remains, and why it is not cheap after all:**

- **D** — needs a dungeon; parked per AGENTS.md § 3.
- **G** — `GET` needs ground items, and where items come from on the ground is a dungeon
  question. **Parked with dungeons by the owner 2026-10-29**, alongside D and G47. Note also
  that `state.h` currently claims `asm/363B` GET increments `DS:E5`, which is the TRI-LITHIUM
  slot; that looks wrong after the consumables sweep in `docs/save-format.md` § 5 and should be
  re-checked before anything is built on it.
- **N** — ~~`NEGATE TIME` falls through into `asm/3BDD` (`mov 0xe3,%al`), not `cmd_done`.~~
  **CLOSED 2026-10-29** by `captures/spice86/cmd-nr-live`, which was the letter's first execution
  in 187 harvests. The fall-through is into `asm/3C04` (the coin rub) and it is not a text-collecting
  state: `DS:E3` is `items_held[13]`, which the name table at file 0x501D calls STRANGE COIN, so
  NEGATE TIME spends a strange coin and stores `0x14` into `DS:26A`, the creature-move freeze
  counter. Both branches are on a frame, with the byte read either side, and the engine implements
  it (`engine/src/game/commerce.c`'s sibling in `commands.c`, gated by `make test-cp6`). The same
  capture closed `cmd_R`.
- **V, W** — need a character seeded to *own* items and armour. `tools/u2_edit.py` can set
  position, stats and gold but not the item bytes yet; extending it is the prerequisite.

**Follow-up proposed, not built:** derive `cmd_handlers.py`'s `exec` column from `captures/`
directly instead of from a hand-kept `# provenance:` line. The line is a second copy of a
derivable fact, which is the thing AGENTS.md § 3 forbids, and it sat wrong across eight files
while every gate that read it reported faithfully.

**Who can do it: an agent, headless.** This is not an owner item. Every letter below is
reachable by sending one key to a live emulator, and `tools/spice86_type.py` does that with no
display involved.

`python3 tools/cmd_handlers.py` reports 14 of 26 handlers executed somewhere in the 60-harvest
corpus and twelve never: **C D H J M N O S T V W Y**. The owner's ruling (AGENTS.md § 3) is that
the engine implements all 26 and that these twelve may not be called code-complete until a
capture exercises them, so the capture is the only thing that closes the marker.

What each one needs, in the order that makes the session cheap (group by setup, per the section
above — one seeded character, one sector, a run down the letters):

- **H, J** — print a message and are done. `HYPER WHAT?` and `JUMP...WHEE...`. The interesting
  part is what they do *not* do: `cmd_J`'s message promises a jump the body never performs, and
  the manual says that is on purpose (see finding 2 above).
- **Y** — no longer in this group. `asm/4542` falls into a freeform echo loop at `asm/4551..4567`
  terminated by Enter, implemented and gated. `cmd_N` was the letter still falling through into a
  fragment whose end is not established, and it is closed too — see the list above.
- **C, S, O, T** — print a prompt ending in a dash and consume a direction, like `U` already does.
  The direction is the whole question: which cell is aimed at, and what the effect is when the
  aim lands. `S`'s effect is already modelled in `combat.c` for CP5 ticket C6 and merely unwired.
- **D** — `DESCEND` then a key-code test whose meaning its own header records as unresolved, and
  dungeons are parked (AGENTS.md § 3), so this one needs a dungeon-capable session and is
  therefore last.
- **M** — `MAGIC SPELL READY #` needs the spell-ready state read alongside it.
- **V, W** — `VIEW WHAT?` / the helm view, and the armour menu with its ownership and strength
  gates. Both need a character seeded to *own* something, which `tools/u2_edit.py setpos` cannot
  do today; extending it to the item bytes is the prerequisite, not the capture.

**An item is closed by a named capture directory**, so the deliverable is
`captures/spice86/cmd-letters-<date>/` with one screenshot per letter and the `ExecutedInstructions`
entry proving each handler ran — not a belief about what the letter must do. Blockers worth
naming up front: `V` and `W` need item seeding, and `D` needs a dungeon.

## Closed

*Nothing yet.* Opened 2026-10-22 with thirteen items.

### G37 — does a placed town thief steal from the player in the original

**Grade: measured (capture `captures/spice86/linda-thief`, 2026-10-26) — no, it does not.**

Filed from the owner's report that the engine's thief by LINDA's entrance robs them as they pass and
that the original never did this. The engine reaches the steal from the contact branch
(`asm/0FCD_combat_thief_steal`), and the only execution evidence the steal had ever was
`castle-03-live`, so the town path was uncorroborated rather than known-wrong.

Driven: seeded into sector 22 at (6,47) with all 16 items ×1, then 108+ turns of adjacency including
20 turns with thief slot 20 **pinned** at (7,47) by `--poke`, i.e. forced orthogonal contact. Items
never changed and HP never dropped. `tools/cfg_printed.py` on the harvest reports **0 executed print
sites** containing `THIEF`, and `asm/0FCD`'s offset range has **0 hits** in `ExecutedInstructions`
while the creature step gate has 42 — so the sweep was live and the steal routine was not.

**What is still open, and it is a different question:** what actually gates the steal. This capture
says *not here*; it does not say where yes. The castle context is the only positive and dungeons are
parked, so that stays with a person.

**The capture's real find was not the question it was sent to answer.** Role-3 creatures **do** move
in the original — `monx22` places thief slot 21 at (8,47) and live it was seen at (9,48), (7,48) and
(8,48) — while the engine has `moves = (role == 1 || role == 2)` and therefore freezes role 3. That
is what the owner meant by "it never moves", and it is an engine defect with a named owner decision
attached, not a gameplay unknown. See `docs/phase4-state.md`.

**Follow-up, measured 2026-10-26 after the role-3 fix shipped (`e1db4f2`): the freeze was not what made
the engine steal.** With wanderers loitering like the original's, a fresh seeded save in sector 22
still loses an item inside ten `PASS` turns — `WANDS-01` present with no turns spent, gone after ten.
So the contact-steal for a town role-3 thief is a genuine divergence, not an artefact of the statue
behaviour, and G37's residue ("what actually gates the steal") is now G38's problem rather than this
one's. Measurement note, because it nearly produced a wrong answer: the engine rewrites the save in
place, so every run must start from a fresh copy of the seed or thefts compound across runs and the
numbers look like a trend.

**Now observed on screen, in the engine, in forrest town (sector 21).** Frame
`captures/engine/forrest-town-theft/turn-00342-theft.ppm`, turn 342, owner playing live:

```
CMD:   A THIEF STOLE SOMETHING!
CMD:   WEST
CMD:   WEST
```

So the divergence is player-visible, not a headless artefact. Sector 21's four thieves are all role 3,
same as LINDA's.

**A static lead fell out of the census, and it is the thing to chase next.** Cross-referencing placed
thieves against their role byte across every town sector:

| place | thieves | role |
| --- | --- | --- |
| sector 03 — the castle (has a `THIEVES GUILD` sign) | 5 at (28,47..50) | **1** |
| sector 31 PORT BONIFICE | 4 at (18,25..28) | **1** |
| sector 22 LINDA | 2 at (7,47),(8,47) | 3 |
| sector 21 forrest town | 4 | 3 |
| sector 82 MAKLER | 9 | 3, plus three at `0x81..0x83` |

**The only place the steal has ever been observed executing in the original is `castle-03-live`, and
every thief in sector 03 is role 1.** The hypothesis that fits G37's negative, this positive, and the
owner's "he's diagonal and not attacking" all at once: **the contact branch — swing and steal — belongs
to the role-1 full-pass path, and role-3 wanderers never reach it.** The engine runs the contact test
before the role gate, so it fires for role 3 too, which is the wrong shape.

Checkable statically and cheaply: establish whether `asm/0B25/0B39`'s candidate-vs-player test is
reachable from the role-3 chain at `asm/1086`, or only from the ungated sweep and the role-1 pass. If
only the latter, the fix is to move the contact test inside the role gate and the town theft stops by
itself. Grade: **inferred**, and it is the highest-value open thread on creature behaviour.

### G30 — what tile is underneath an unlocked door

**Grade: recalled (owner, 2026-10-27) — "when you open a door it disappears, I don't
remember what's underneath the tile."** Named unknown, not a guess to be filled in.

`asm/435E_sub_435e.asm` ends with `mov 0x14,%al; add %al,%al; ... mov %al,(%bx)` — it
writes `DS:14 * 2` into the map cell, so the door really is replaced in the sector
data rather than flagged elsewhere. What `DS:14` holds at that moment is not traced.

**Agent-drivable, no owner needed.** Headless Spice86: pick a sector containing map
byte `0xA0` (175 occurrences across 15 sectors), `--player` + `u2_edit.py setpos` to
stand adjacent, read the sector grid at `DS:0x1800` for that cell, send `U` then a
direction key, read the cell again. The before/after pair is the answer. Confirm the
key landed by reading memory, never by the send returning ok.

**Attempted 2026-10-28, still open — the recipe runs but the unlock refuses.** Sector 32
(28 doors in the first 4096 bytes, row-major `y*64+x`, confirmed against the live grid at
`DS:1800`), `G23MAX` with 20 keys, three placements, `--patch-arrows`:

| player | target cell | byte there | input | result |
| --- | --- | --- | --- | --- |
| (2,14) | north (2,13) | `0xA0` | `U` then `Up` | `NORTH--INVALID MOVE!`, cell still `0xA0`, keys still 20 |
| (8,1) | east (9,1) | `0xA0` | `U` then `Right` | `EAST--INVALID MOVE!`, unchanged |
| (8,1) after two accepted east moves | east (9,1) | `0xA0` | `Right Right U Right` | `EAST--INVALID MOVE!`, unchanged |

So the direction prompt *is* satisfied by a patched arrow (the result line names the
direction back), the target really holds the door byte, and the unlock still refuses. Two
things came out of the failure. **`DS:14` is not constant**: it read 6 in sector 20 and 4 in
sector 32, and `DS:14 * 2` is what gets written into the cell -- 12 and 8, both ordinary
walkable ground in those sectors. That is the first real evidence for what the byte under a
door *is*, and it is an inference from two reads, not an answer. And the aim computation in
`asm/4326` is only partly read: it sets `DS:24 = DS:01` (player y, no delta) and
`DS:23 = DS:00 + DS:19`, then `call L_0052b3` (`asm/50B3_map_tile_ptr`, the live grid cell),
so on its face it can only ever aim along x -- which would make a north/south unlock
structurally unable to hit anything, and is the next thing to check. **Whoever picks this up:
read `asm/4326..4339` end to end before another capture.** Runs were `g30`, `g30b`, `g30c`.

**CLOSED 2026-10-28, statically, and the refusals were the clue.** Reading the whole U chain
(`4309 -> 4310/4326 -> 433A -> 433E -> 4345/435E`) answers both halves:

* **What is written.** `asm/435E` writes `DS:14 * 2`, and `DS:14` is the tile **under the
  player** shifted right one — the same `DS:14 = map_byte >> 1` that `site_check.c` already
  records for `cmd_E`. Two live reads from the failed sessions confirm it exactly: sector 32
  with the player standing on `0x08` gave `DS:14 = 4`, and sector 20 on `0x0C` gave `DS:14 = 6`.
  So **opening a door paves it with the ground you are standing on.** That is the named answer
  the owner's "I don't remember what's underneath" was asking for, and it is falsifiable: an
  unlock from forest must leave `0x0C`.
* **Where it is written.** `asm/50B3_map_tile_ptr` leaves the cell pointer it computed in
  `DS:06` (`mov %bx,0x6`), and `asm/435E` writes through exactly that cached pointer
  (`mov 0x6(%di),%bx ; mov %al,(%bx)`). So the write lands in the live sector grid in DS —
  not a separate overlay, not the file. Whether an opened door survives leaving and
  re-entering is a question about the sector store (`asm/22FD`), not about this instruction.
* **Why every attempt refused.** `asm/4309` requires `DS:19 != 0` and `asm/4326` computes the
  target as `(x + DS:19, y)` with **no Y term anywhere in the chain**. UNLOCK is structurally
  horizontal-only; a north or south aim can never reach a door, and the recipe's "send `U`
  then a direction key" was aiming in an axis the command does not have. A working capture
  should step east or west first (so the delta is sideways) and then press `U` — and the
  untested part is now only whether the prompt re-reads a direction at all.

Renamed with filename+header only: `4309_unlock_aim_gate`, `4326_unlock_target`,
`435E_open_write_tile`.

Also capture in the same session: `DS:65` (keys) before and after, to confirm the
`das` decrement end to end, and whether the opened cell survives leaving and
re-entering the sector — that decides whether the write goes to the sector map or to
a separate overlay, which is the `(%bx)` question the asm leaves open.

**CAPTURED 2026-10-28 — one completed unlock, both predictions held.** Sector 32, `G23MAX`
with 20 keys, player at (33,1) standing on forest `0x0C`, door at (34,1) = `0xA0`, and
`DS:14 = 6` = `0x0C >> 1` exactly as the static reading predicts. Input `U` then `Right`
**sent in one burst with a 0.05 s gap**: the cell came back **`0x0C`** and `DS:65` went
`0x20` -> `0x19`, i.e. packed-decimal 20 -> 19. So the door really is paved with the ground
you are standing on, and the `cmc; sbb; das; cmc` decrement is real end to end. Both halves
of the prediction, observed.

**Why every earlier attempt refused, and it was not the axis.** `cmd_U` prints
`UNLOCK DIRECTION-` and then calls the direction reader at runtime `2370..2392`, which
polls for a key whose **high byte is 0xFF** (a BIOS extended scancode) and matches `AL`
against the four arrow scancodes held in `DS:0x26E..0x271`. If none arrives before its
loop counter wraps it prints `PASS` and leaves `DS:19` at zero, and `asm/4309` then refuses.
Sending `U`, waiting a second, and only then sending the arrow loses that race: the screen
showed `UNLOCK DIRECTION-Pass` followed by `EAST--INVALID MOVE!`, because the arrow arrived
after the prompt had given up and was consumed as an ordinary move instead. The fix is
timing, not aim — `--keys U,Right --delay 0.05`. Runs `unlock1`.

**Persistence question — MEASURED 2026-10-29, and the recollection was half right.**
Recalled (owner, 2026-10-28b): "when a door is opened it stays open". Answered at the bytes
on 2026-10-29 by an owner-driven live session over noVNC (`deploy/spice86-novnc`, sector 31,
character seeded onto the village gate at (34,20) in sector 30 so the game recorded its own
return point), reading `DS:0x1800` over the HTTP API between steps:

* **within one visit the opening is durable.** Two doors opened at (42,17) and (49,17), both
  paved to `0x70` (the paving the player stood on). The player then walked through a town
  with 32 live creatures; ~38 cells were churned by movement and **both openings survived**.
  So the original does not rebuild the grid from a pristine copy on a creature turn.
* **across a visit nothing survives.** Leaving and re-entering closed them. `mapx31` in the
  run dir was byte-identical to pristine, as were `monx31`, `mapx30`, `monx30`. **The only
  file the game writes back is `player`.** See `docs/save-format.md` § 10 and
  `docs/map-format.md` § 10.

So the recollection was right about the thing nobody had thought to separate — the opening
*does* stay open, for as long as you are in the sector — and wrong about persistence, which
the original does not have. The step that made this answerable was not the walk, it was
entering the town the way the game does: a save-seeded town position has no valid wilderness
return, which is what produced a bogus "it put me in the wrong place" on the first attempt.

**Still open, and it needs a named capture directory.** The rule is that a gameplay item
closes on a capture, not on a live session that was read over an API and left in a container
run dir under `/var/tmp`. The scripted re-run is exactly the sequence above — open a door,
damage a guard, leave, re-enter, diff the grid and the six creature arrays against pristine —
headless, harvested as `door-persistence`. That moves the corpus from 167 harvests to 168 and
`tools/cmd_handlers.py --write-provenance` legitimately rewrites those denominators; the
provenance gate went red on exactly that arithmetic when a *save* probe harvested by accident,
which is why this one has to be a deliberate harvest and not a side effect.

### G29 — sector crossing on the west land bridge: scroll or teleport

**Grade: recalled (owner, 2026-10-27) — "when I cross the land bridge west into the
next sector you have me teleporting, but it should scroll seamlessly, at least in the
overworld."**

This is not only a rendering complaint. It contradicts something the project has been
treating as byte-proven, and per the rule that an owner observation outranks an agent
inference, the contradiction has to be resolved before anything is changed.

**What the engine does now.** One resident sector (`engine/src/core/world.h`), and
`asm/5AE1` gives the window origin as `(x-9, y-5)` — the player sits at screen cell
(9,5) of a 20x16 viewport, not the centre. Off-sector cells take their tile from the
single byte `DS:12` (`tile_offsector`, since renamed `view_mode` — see the G29 resolution
below), which is proven and is why the engine paints a
uniform strip at the edges. Crossing is handled by `site_leave()` / the edge test.

**The conflict.** `asm/242D/2434` was read as testing **only the high edge**, which
means walking east or south leaves the sector while walking **west or north wraps
round the torus inside the same sector**. That reading is recorded in `site_leave()`
and in `world.h` as "the viewport never straddles a file". If it were true the owner
could not cross west into the next sector at all in the original — and they say they
can, routinely, and that it scrolls.

So one of these is wrong, and it is probably the reading of `242D/2434`, not the
owner. Two candidate designs, and they need different fixes:

1. **The original never straddles** (current engine model). Then "seamless" means the
   crossing must not move the player's *screen* position — the sector reload has to
   place the player at the opposite edge so the picture is continuous, and the
   teleport is a bug in how the engine repositions on reload, not in the viewport.
2. **The original does straddle**, and `DS:12` is only a fallback for cells beyond the
   world edge. Then the overworld needs two (or nine) resident sectors and the viewport
   sampling across the boundary — a real change to `world.c`, `view.c` and the crossing
   path.

**The capture that decides it.** Headless Spice86, sector 20, `--player` +
`tools/u2_edit.py setpos` to stand near the west edge on a land bridge. Read the
staging grid at `DS:0x272` (256 B) and `DS:12`, then walk west one step at a time
across the boundary, reading after each step.

- If the staging grid ever contains tiles from two different `mapx` files, design 2 is
  correct and `DS:12` is a fallback.
- If it never does, design 1 is correct, and the question becomes what the original
  does to the player's screen position at the moment of reload — read `DS:00/01` and
  the window origin across the crossing step.
- Either way, record whether the west edge actually changes sector or wraps in place,
  because that is the `242D/2434` reading being confirmed or refuted.

**Owner-only part:** whether the original's crossing is a smooth scroll or a fast
pan. The bytes can say what the viewport contains; only a person watching can say what
it looks like. Worth 5 minutes of the original under `tools/spice86_start.sh
--patch-arrows` with a real window.

#### G29 progress — capture 2026-10-27, the framing was wrong

Live capture against the containerised original (`deploy/spice86-novnc`, owner walking).

**Settled, by bytes:**

- The owner walked east from `x=57`; position became `x=7`, and the resident sector was
  still **20**. Decided mechanically, not by interpretation: `mapx20` and `mapx30` differ
  on 8 of 64 rows, and over those 512 discriminating cells the live map buffer at
  `[DS:0472]` (which reads `0x1800`) matched `mapx20` on **511** and `mapx30` on 496.
- Therefore **crossing an edge does not load a neighbouring sector.** The overworld is a
  torus *within* one 64x64 sector, and the tens digit of the sector name is not an
  east/west coordinate the way the engine's `site_leave()` assumes.
- `DS:12` reads `0xFF` in a live overworld session. The engine sets `tile_offsector = 4`.
  One of those is wrong and it is probably the engine's.
  *Correction 2026-10-28b: neither is wrong. `DS:12` is a mode byte — 0xFF in-world, 4 in an
  interior — and the engine writes exactly that. The field is now `state.view_mode`.*

**What this does to the ticket.** The two candidate designs as written are both wrong:
there is no second sector to straddle, so design 2 is dead. The seamlessness the owner
describes has to come from the **viewport wrapping its coordinates inside the sector** —
origin `(x-9, y-5)` masked to 0..63 — instead of painting off-sector cells from `DS:12`.
That is a much smaller change than design 2, and it is in `view.c`, not `world.c`.

**Still open, and the capture that was supposed to close it failed.** The wrap-versus-fill
test compared the staging row against both hypotheses and got **4/20 either way**, which
discriminates nothing. The row read back as uniform `0x84` — a staging byte with bit 7
(the do-not-draw flag) set — so the assumed grid geometry is wrong: either `DS:272` is
not the grid base in this build, or it is not 20 columns wide, or row 5 is not the
player's row. `U2_VIEW_TILES` in the engine is the thing to re-derive, not to trust.

Re-run: stand at a small `x` (say 2) on a row whose map tiles are visually distinctive at
both ends, dump `DS:272..DS:3B0` raw, and find the grid by matching the *known* tiles
around the player rather than assuming the layout. Once the grid is located, the
wrap-versus-fill question answers itself in one sample.

#### G29 RESOLVED 2026-10-27 — it was neither design. The viewport wraps.

Closed by capture, not by argument. The original does not load a neighbouring sector at
an edge (proven 511/512 against the discriminating rows of `mapx20` vs `mapx30`), and it
does not blank off-sector cells either. At player `(0,11)` with origin `(-9,6)`, staging
row 5 held the map's own columns 55..63 with bit 7 set:

    84 84 84 84 84 84 84 84 84 78 84 84 84 84 84 84 84 84 86 86

`0x08` grass -> `0x04` -> `0x84`; `0x0C` forest -> `0x06` -> `0x86`; column 9 is `0x78`,
the player stamp. The fill loop masks its coordinates into the sector and reads the torus
straight through. `u2_view_fill` now does the same. `DS:12` is not consulted by the
viewport at all, which left its real purpose open as G31 — **settled 2026-10-28**: `DS:12` is
a mode byte (0xFF world map, 4 interior) and its only readers are
`asm/5AB1_view_origin_by_mode` and `asm/5AF9`, where `DS:12 == 0xFF` is how `5AB1` decides
whether the viewport origin wraps. See `docs/monster-spawn.md` § 15.2 and `state.h`'s field,
now named `view_mode`.

A CP4 test had been asserting the opposite behaviour by name and passing; it is replaced
and the replacement says why.

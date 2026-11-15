# Handover — TRANSACT: what is evidenced, what is not, and what to build first

**Scoped handoff for a fresh agent implementing `cmd_T` in the engine.** It is not the project entry
point — read `AGENTS.md` first, then `docs/handoff-2026-10-29b.md` for general state. This file exists
because the commerce question was assessed on 2026-10-29 and the answer is "build half of it", and the
boundary between the two halves is the part that is easy to get wrong and expensive to rediscover.

---

## 0. STATUS — executed 2026-10-29, same day. Read this before § 3 and § 4; four of § 4's
seven "not evidenced" items closed, and two of them closed by refuting the observation that made
them open.

| § 5 step | outcome |
| --- | --- |
| 1. G23 | **done, and it refuted two observations.** `captures/spice86/g23-food-live`: `bp_read --ip 17B7` stopped mid-quote with `DS:27..2A = 00 20 00 12`, `--ip 2052` stopped on the same purchase with the same pair, gold `9999 → 9979`. The grocery quote is **`0020`, not `0200`** — a transposed digit in a hand-copied screen, and `asm/20AB` reproduces both pairs exactly. `captures/spice86/g23-weapons`: the axe is **20, not 25**, and the weapons shoppe *does* route through `asm/20AB` (`asm/1F00` passes the menu digit). **One pricing path**, plus two shops that bypass it with a literal. § 4.1 is closed. |
| 2. decode `asm/1590` | **done, and it was not the fork.** It is a quadrant switch on the player's own `X`/`Y` against `0x20`, then a site-kind test, for eight destinations — table in `docs/commerce.md` § 5d and in `asm/1590_transact_shop_selector`'s header, corroborated by the fifty shipped sign/NPC pairings. The menu-versus-line fork is `asm/4075`: whether the slot scan found a creature. § 4.3 and § 4.4 are closed. |
| 3. build the selling branch | **built**: `engine/src/game/commerce.{c,h}`, `cmd_T` in `commands.c`, the direction arm in `overworld.c`, 21 new extractor entries, and `make test-cp6` (`engine/tests/run_cp6.sh` + `commerce_check.c`, 38 pure checks + the four shops end to end against a synthetic sector). Selector, both catalogues that are read, the class gates, the quote, yes/no, the BCD charge and the owned-count arrays. |
| 4. leave the talking branch a `NULL` slot | **superseded 2026-10-29, the same day: it is now implemented, both arms.** The mechanism was decoded and observed first — `asm/40C0` branches on bit 7 of `[slot+0x1D7]`, `asm/4209` computes `role - 0x80`, `asm/154E` prints that record out of the loaded `tlkxNN` buffer at `[DS:476]`, stopped live with `AL=0x02` for a role-`0x82` Jester — and then the engine half landed: `u2_world_load_tlkx` plus `u2_world_tlkx_record` in `core/world.c`, and `asm/40CB`'s `street_*` chain with its seven literals extracted as `run` locators. The instruction below to leave it stubbed is historical; `CP6 talk` in `run_cp6.sh` is what guards it now. |

Still open after this run, and named where they are implemented: the cleric shrine, the magic
shoppe, the pub and the clue seller are dispatched but not implemented (each gap is commented at
its branch in `commerce.c` with the reason — the spell-slot question, the chatter table, the clue
pool). **Two of those four have since shipped, and this paragraph is the one this file got wrong
and left standing: the pub and the clue seller are both implemented now** (`shop_pub`, `shop_clue`).
The pub shipped 2026-11-15 on the chatter table that had been re-cut out as `asm/16BD` back on
2026-10-25; the clue seller shipped the same day on its pool, re-cut as `asm/1B6B_clue_pool.asm`.
`run_cp6.sh` gates both and each was checked against a mutation control. What is genuinely left
here is the cleric shrine and the magic shoppe. Where the dialogue line renders on screen is not
established. `[DS:46]` as gender is untouched, and G80b showed why that is correct: the branch is
dead code in the original. The ray's `0x7C` near-cell case is read but has never been the accepted
tile in a live purchase except in the grocery reproduction.

Repo at time of writing: `main`, tree clean, `check_all` green, CP0–CP6 green.

---

Repo at time of writing: `main` at `5546e3a`, pushed, tree clean, `check_all` green, CP0–CP5 green,
1222 files in `asm/`, 180 capture directories. **The authority for which handlers are implemented and
which have execution evidence is `python3 tools/cmd_handlers.py`** — do not trust any count quoted
here, including this one.

---

## 1. The one-paragraph answer

`cmd_T` has two outcomes and they are not equally understood. **Selling is evidenced end to end** — the
selector, the catalogues, the price operand, the gold and inventory arrays, and a complete headless
purchase that has been reproduced twice. **Talking is data-ready and mechanism-blind** — the dialogue
files are decoded and readable, but nothing maps "the NPC you transacted with" to "the line it says",
and the routine that decides menu-versus-line is an unreviewed 8-byte fragment. Build the selling
branch. Do not build the talking branch yet.

## 2. Naming, because it cost a round trip

There is **no TALK command**. The 26 letters are ATTACK, BOARD, CAST, DESCEND, ENTER, FIRE, GET,
HYPER WHAT?, two inline-string handlers, JUMP, LAUNCH, MAGIC-ready, NEGATE TIME, OFFER, PASS,
QUIT-OR-SAVE, READY WEAPON, STEAL, TRANSACT, UNLOCK, VIEW, WEAR, X-IT, YELL, and the status screen.
`T` does both jobs. `docs/commerce.md` § 5c is titled "how the talk ray works" and is about `cmd_T`;
that wording is fine but easy to misread as a second command.

## 3. Evidenced — safe to build on

**The selector.** `asm/4050_cmd_T.asm`, executed in 28 of 180 harvests, confidence high. Chain, every
step cited to bytes in `commerce.md` § 5c:

1. `asm/406E` refuses if site kind `DS:4A == 0` — nothing deals in the open world
2. `asm/407C` and `asm/409C` call `L_0052b3` = `asm/50B3_map_tile_ptr`, which returns the **live grid
   cell** at `DS:23/24`, not a creature record
3. the cell must be `0x7C` (`asm/4081`) or `0x68` (`asm/409F`), else `asm/40A6` prints
   `FUNNY, NO RESPONSE!`
4. on a match, `asm/40A3` tail-jumps to runtime `1590`

**`0x68` is the merchant marker and it lives in the map.** In `mapx31` the seven `0x68` cells are
*exactly* the seven Merchant spawn slots in `monx31` — 7/7. `tools/shop_access.py` enumerates all
fifty such pairings in the shipped data, so "who can transact" is a lookup, not a hunt.

**The catalogues.** All located and attributed to owning routines (`commerce.md` § 2): weapons 8 items
(DA MA AX BO SW GR LI PH), armour 6 (CLOTH LEATHER CHAIN PLATE REFLECT POWER), magic shoppe 6 wizard
spells, cleric shrine 6 cleric spells, horse dealer 1, clue seller 1, tavern drink-or-tip, cleric
offering. Each spell list is **class-gated** (`YOU ARE NOT A CLERIC!` / `YOU ARE NOT A WIZARD!`).
**No town-to-town catalogue variation exists anywhere** — that was tested and an earlier claim retracted
in place. Do not go looking for per-town stock.

**The inventory and money model.** `engine/src/core/state.h` maps every array with provenance:
`gold DS:58/59` as packed BCD with the **high byte first** (the naming is inverted and the comment says
so), `readied_weapon DS:61`, `worn_armour DS:62`, `weapons_owned DS:76[10]`, `owned_armour DS:80[8]`,
`armour_owned DS:97[6]`, `spell_counts DS:B6[10]`, `items_held DS:D6[16]`.

**A whole purchase, twice, headless.** Sector 32, maxout character at `(38,5)`, `T,Up` at
`--delay 0.02` → `THE ARMOUR SHOPPE` → `1` → `FOR YOU ONLY 0012` → `y` → gold `9999 → 9987`,
`DS:97 00 → 01`. Reproduced 2026-10-23 with no human in the loop, and the transact key-gap behaviour
was re-confirmed 2026-10-29. **Headless transacting works.** Any note saying it does not is stale;
`commerce.md` § 5b carries the retirement.

**The price plumbing.** `DS:27/28` is the price pair; printed by `asm/51A5` + `asm/53B5` (both
reassembled from fragment splits), charged by `asm/2052`. Two price setters are understood exactly and
involve no arithmetic at all:

* **tavern drink** — a constant. `asm/15E1` writes `0x05` to `DS:28` and `0x00` to `DS:27`. Price `0005`
* **clue seller** — the advertised `×100` is **byte placement**: the typed digit goes into the
  *hundreds* byte and the low byte is zeroed. There is no multiply in the path

## 4. Not evidenced — do not build on these

**1. The price formula does not reproduce reality.** `asm/20AB_bcd_price_from_stats` seeds two BCD pairs
at `0004`, computes `DI = bitlen([DS:50] + [DS:4E])` — **int + charisma**, `DS:4E` proven 2026-10-29 —
and walks a Fibonacci sequence `4, 8, 12, 20, 32, 52, 84, 136, 220, 356, 576, 932…` for `DI` steps,
with higher `int + cha` meaning **fewer** iterations and a **lower** price. A horse at 32 gp reproduces
exactly. **An axe quoted at 25 gp cannot be produced by this routine at all.** So there is at least one
pricing mechanism that has not been found, and shipping `asm/20AB` as "the price function" would be
shipping a formula known to fail a real observation.

**2. G23 is the cheap fix and should go first.** Read `DS:27/28/29/2A` and `DI` live at runtime `17B7`
and `2052` during a vendor purchase. This decides whether the shop code needs one pricing path or two,
and it is a **scripted afternoon, not an owner session** — `tools/bp_read.py --ip` plus a headless
purchase. Registered in `docs/needs-gameplay.md`.

**3. The ray's extent is unknown and the header says so explicitly.** `asm/4050`'s own header records
that "one cell from the player" was refuted: a live purchase completed with the player at `(48,53)` and
the vendor on the `0x68` marker at `(48,51)`, the sign row between them. **How far the ray actually
goes, and whether the sign row is transparent by design or simply not a stop condition, is NOT
established.** The header's instruction is verbatim: *"Do not write cmd_T to a one-cell test."* What
feeds the cursor at `DS:23/24` is also not established, and `asm/4085`'s `DS:23 += DS:19` may or may not
be the ray stepping.

**4. The menu-versus-line fork is undecoded.** `asm/1590_sub_1590.asm` is an **8-byte unreviewed
bulk-wave fragment**. That is the dispatcher `cmd_T` jumps to. Until it is read, there is no basis for
choosing between opening a shop and printing a line.

**5. `tlkx` format is decoded; the selection rule is not.** `tools/tlkx_decode.py` is the reference
decode: high-bit-set ASCII (the Apple II convention), `0x00` separates records, each record is
`<NPC NAME> <VERB>: <LINE>`. Sector 21 has six records. So *what can be said* is fully readable and
**which record a given NPC gets is completely unknown**. Guessing among six lines is exactly the kind of
plausible-wrong that has no test to catch it.

**6. Two named-but-unobserved things.** `asm/412C_street_merchant` prints a hardcoded
`A MERCHANT SAYS: / WILL YOU BUY MY APPLES?` — not `tlkx` at all — and its entry has **never appeared in
ExecutedInstructions**. Likewise the whole tavern chatter path (`asm/155D_chatter_table_selector` →
`asm/16BD_barkeep_chatter_hints`) is read, not observed, and its selector only picks entry `SI` from a
NUL-separated table. Note `asm/155D` is a **prologue only** — the routine runs to about `0x1581` and the
rest is in following tiling fragments.

**7. Two bytes still unnamed.** The class field behind the cleric/wizard guards, and confirmation that
`DS:46` is gender.

## 5. Suggested order

1. **G23 first.** One scripted purchase with `bp_read` stops at `17B7` and `2052`. It is small, it is
   agent-drivable, and it determines the shape of the pricing code.
2. **Decode `asm/1590` and its callees.** This is the fork. Everything about the talking branch waits on
   it, and it may also answer the ray extent.
3. **Build the selling branch**: `0x68`/`0x7C` selector, the four catalogues, class gates, quote,
   yes/no, gold subtract with the BCD high-byte-first rule, and the owned-count arrays. Verify against
   the sector-32 armour purchase, which is a known-good end-to-end case.
4. **Leave the talking branch stubbed and unimplemented** — a `NULL` slot, not a guess. Per `AGENTS.md`
   § 3, an unimplemented letter is an honest gap; a stub that prints a plausible line is not.

## 6. Recipes that work

- **Headless transact**: `tools/spice86_start.sh --headless --patch-arrows --player <file> --auto-play`,
  then `tools/spice86_type.py --keys T,Up --delay 0.02`. **The gap matters**: at 0.12 s and above the
  direction is drained first and you get `TRANSACT-PASS`.
- **The reject oracle.** `CMD: TRANSACT-<dir>` followed by `FUNNY, NO RESPONSE!` means the key **landed
  and was rejected**. Silence means the key never arrived. This is the cleanest way to tell a bad
  position from a dropped key, and it needs no trap.
- **`tools/shop_access.py`** enumerates every sign/NPC pairing in the shipped data.
- **`tools/tlkx_decode.py [original/tlkxNN …]`** prints the dialogue records. Read-only.
- **Seeding**: `tools/u2_edit.py set <file> sector 32` and `setpos <file> 38 5`. Sector digits matter —
  the ones digit **is** the site kind, so never `setpos` into a site sector; put the character in the
  wilderness and enter properly.

## 7. Hazards that cost this project time, most of them on 2026-10-29

- **A too-short observation window produces confident wrong answers.** Three in one day on the gate work
  alone. If a mechanism is driven by a counter, check how long the counter actually takes before
  concluding the mechanism is absent.
- **A plausible shape match is not a causal link.** A bulk-wave region matched a live mechanic in every
  particular and was refuted by a breakpoint that never fired across two demonstrated instances. Read
  the routine, or stop the CPU in it. Do not infer from shape.
- **In this listing dialect an operand with no `$` is a MEMORY reference**, not an immediate. `adc 0x2,%al`
  reads `DS:02`. This one misread cost a whole decode.
- **`asm/` filenames are file offsets; `L_xxxx` labels are runtime = file + 0x200.** But a *displacement*
  inside an instruction goes the other way: `%cs:0x2798` is file `0x2998`. Getting this backwards makes a
  data table read as garbage.
- **The stamped grid at `DS:1800` is the `mapxNN` grid shifted down two rows.** Diff the two without the
  shift and you get 1020 fake differences instead of 8 real ones. `docs/map-format.md` § 8.
- **`pkill -f pattern.py` kills your own shell**, because the invoking command line contains the pattern.
  Use `pgrep -f 'pattern\.py'` with the bracket trick, or kill by PID.
- **Injecting keys into the noVNC container crashes Spice86.** Reads and screenshots are safe. Human
  types, host watches.
- **`--write-provenance` refreshes digits and will not flip a verdict.** If a header says "never executed"
  and a capture shows it executed, the prose is a human edit — fix it deliberately, not by re-running a
  tool.

## 8. Context you may want from the same day

Time doors (moongates) were discovered, decoded and implemented while this assessment was being written.
`docs/moongates.md` is the findings page and is worth a read for method as much as content: it contains
three recorded self-corrections and a retracted refutation of the mechanic by an earlier document. The
engine now has `engine/src/game/gates.{c,h}` and a CP4 spec. None of it touches commerce, but it does
change `state.h` (`gate_index` at `0x6E`, `gate_countdown` at `0x6F`) and `overworld.c`'s walk loop.

# Overnight task queue — 2026-10-23

Autonomous queue for a fresh agent running unattended. Every task is chosen because it
needs **no owner interaction**: all inputs already exist inside this project, or are
reachable by a headless emulator run that needs no display and no keyboard through X.

**Start here:** read `AGENTS.md` (hard law), then `docs/archive/handoff-2026-10-23.md` (the
entry point, including the day's addendum), then this file.

Standing context: `docs/phase4-state.md` (checkpoint table, CP5 tickets, gate log),
`docs/needs-gameplay.md` (the capture worklist with who-can-do-it verdicts),
`docs/spice86-capture.md` (the emulator runbook — **read before driving Spice86**).

> **What changed since the last overnight queue.** Headless sessions can now see and
> act. `tools/spice86_type.py` injects keys through Spice86's MCP endpoint,
> `spice86_start.sh --auto-play` gets a seeded character into the world unattended,
> `tools/spice86_screenshot.py` captures the screen with no display, and
> `--patch-arrows` makes arrow keys register. Several tickets that used to mean "wait
> for the owner" are now Tier B below. The old rule "no emulator launches" applied to
> the *generic harness* queue, not to this one.

---

## Rules of engagement

1. **Prove or pass.** Nothing may be asserted without a byte citation, a captured frame,
   or a recorded run. If evidence is insufficient, write what was found and exactly what
   is missing into the progress report, mark the task PASSED, and move on. A passed task
   with a clear reason is a success; a guessed mechanism is a failure.
2. **Never read outside this project root.** `AGENTS.md` § 0. No sibling directory, no
   wiki, no prior attempt. If a task needs a file this project does not have, it is
   BLOCKED — say so and stop.
3. **Never write to `original/` or `references/`.** Read-only inputs. `references/` is
   the pristine backup and is restored only with the owner's approval.
4. **`make -C engine test` must be green at every commit that touches code, tooling,
   `asm/`, `engine/`, or anything a gate reads.** Documentation-only commits may skip it.
   If anything under `asm/` is touched, `bash tools/assemble_clean.sh` must still print
   `CLEAN REBUILD BYTE-MATCH: 37344 bytes identical`, and in `asm/` **only the filename
   and `#` comment lines may change — labels are body.**
5. **Demonstrate every new gate or checker failing before trusting it pass.** Invert one
   expectation or feed one known-bad input, confirm red, restore. A gate that cannot go
   red is worse than no gate; this project has been bitten by that repeatedly.
6. **Never edit a file with a script.** Use the edit tool. `str.replace` that misses
   returns the string unchanged and exits 0, so a no-op and a success are indistinguishable.
7. **Never heredoc a non-trivial script.** Write the file, syntax-check it, run the file.
8. **Scratch goes to `/var/tmp`, never `/tmp`** (tmpfs here; it takes RAM from the model).
9. **Never run the game in place.** `tools/spice86_start.sh` copies pristine
   `references/ultima2/` into a fresh timestamped dir under `tmp/` for every launch.
   Teardown only via `tools/spice86_stop.sh`.
10. **Commit in logical units**, reason in the message. Grade every claim:
    observed / measured / inferred / recalled.
11. **Stop and leave a report** rather than thrashing if: a gate goes red and cannot be
    fixed, the emulator will not start, or two consecutive tasks turn out to need the
    owner. Write what was attempted and what is missing.

---

## Tier A — static analysis, zero risk, inputs already on disk

### A1 — Assemble the sector → town name mapping

`docs/place-names.md` § 2 lists 237 letter runs across 15 sectors but never joins them
to the `tlkxNN` dialogue files or to the site-kind scheme. The mapping is fully derivable
from shipped data.

**Deliverable:** a table in `place-names.md`: sector, ones digit (site kind), town name
if the signs spell one, notable shop names, whether a `tlkx` exists.
**Verify:** every sector with a town name must have a `tlkx` file, and none with ones
digit 0/4/5 should. If that invariant breaks, that is a finding — record it, do not
"fix" the data.
**Stop:** a sector whose signs spell nothing → record as unnamed and move on.

### A2 — Re-sweep the map signs properly

The existing sweep required runs of 3+ letters and read **horizontally only**. It found
`TAKE OUT` by luck and would have missed shorter names and any vertical signage.

**Deliverable:** extend `tools/map_signs.py` to cover vertical runs and 2-letter names,
regenerate the inventory, and note what the first pass missed.
**Verify:** re-run on sector 31 and confirm it still finds `PORT BONIFICE` and
`MC DONALLS`; add a known-miss case as a control.
**Note:** `0x80` (`A`) and `0xE0`/`Y` are used as generic solids in some sectors
(`mapx15` is solid blocks of them) — filter them the way the first pass did, or the
output drowns.

### A3 — Find the real price source

`docs/commerce.md` § 3 records that `DS:27/28` is a **transient scratch write, not the
price field** — a dump after a vendor purchase found `22 19` there, not the `0200` the
game printed. The actual source of the printed price is unknown.

**Deliverable:** the routine and arithmetic that produce the printed price, with byte
citations; or a precise statement of what is missing.
**Verify:** the answer must explain `0200` for the vendor and the observed spend of 40
gold for 20 food points (2 gold per point).
**Stop:** if the chain cannot be closed statically, record exactly where it goes cold.
Do **not** invent a table — the whole finding of G21 was that no table exists.

### A4 — Map the dungeon encoding

Dungeon monsters use types `0x01`–`0x06`, not the overworld `0x2C`–`0xFC`; `dungeon-35-live`
matched `monx35` at 0/27, so dungeons use **neither** spawn path. `asm/147F_dungeon_monster_spawn`
builds HP with `or $0x10,%al`.

**Deliverable:** what `0x01`–`0x06` mean, where dungeon objects are stored and drawn from,
and how the HP formula maps to the third-party `17+32k` / `153+32*(k//2)` fit.
**Inputs already on disk:** `captures/spice86/dungeon-35-live/`, `airport32/`,
`original/monx35`, `asm/147F`, `asm/1151_dungeon_darkness`.
**Trap to avoid:** the active-slot filter `type not in (0, 0x40)` is an overworld
assumption. In a dungeon it admits the kind indices `0x01`/`0x02` and makes two
encodings look like one broken grid. Write the filter per ruleset.

### A5 — Measure `asm/19D7`'s true extent and propose the split (do not execute)

`asm/19D7_magic_shop_menu.asm` declares `19D7..2027` and contains five shop routines,
but the first observed purchase executed `2052..2130` — **past the declared end**. The
span is short at the top.

**Deliverable:** a proposal document listing the candidate routine boundaries in
`2028..2130` with byte citations, and what `check_func.py` would need to prove each.
**Hard limit:** **do not split the file.** It is a Phase 2/3 boundary change touching the
byte-proof harness and needs the owner's approval (ticket G22). Propose only.

---

## Tier B — headless captures, now automatable

Recipe for all of these:

```bash
mkdir -p tmp/seed && cp references/ultima2/player tmp/seed/player
python3 tools/u2_edit.py maxout tmp/seed/player
python3 tools/u2_edit.py set tmp/seed/player name AGENTTEST
python3 tools/u2_edit.py set tmp/seed/player sector <NN>
python3 tools/u2_edit.py setpos tmp/seed/player <x> <y>
tools/spice86_start.sh --headless --patch-arrows --player tmp/seed/player --auto-play --name <label>
# read memory:  GET http://127.0.0.1:20180/api/memory/<phys>/range/<n>  -> JSON {"values":[...]}
#   DS = 0x08BE*16 ; runtime offset = file offset - 0x200
# keys:         python3 tools/spice86_type.py --keys T,Up --delay 0.02
# screen:       python3 tools/spice86_screenshot.py /var/tmp/shot.png
tools/spice86_stop.sh        # dumps + harvests
```

**Memory reads pause the CPU and keys sent in that window are silently lost.** Wait
0.5–1 s after a read before injecting a key, and verify every step against the screenshot.

### B1 — G20: enter an `X4` tower sector

Sectors 24, 34, 44 are the only site kind never visited. This falsifies or confirms the
claim in `docs/sector-rulesets.md` § 1 that the sector's ones digit predicts the ruleset.

**Record:** does it use the overworld encoding, the dungeon encoding, or a third one?
Does grid stamping hold? Is there a `tlkx`? (There is not — which is itself the test.)

### B2 — G1/G18: confirm the art index on an ordinary wandering spawn

Read the map byte and the monster type for a real wandering spawn and compare against the
drawn tile. `view.h` already composes to `>>2` (`((b>>1)&0xFE)>>1 == b>>2` for all 256
bytes — do not "fix" it), so the open question is which field the renderer reads.

### B3 — G8: identify `DS:96+i`

Read `DS:96+i` before and after one armour purchase. Ticket says minutes, high confidence.
Note that the vendor flow needs `T` and the direction **queued together**
(`--keys T,Up --delay 0.02`); with a human-scale gap the game logs `TRANSACT-PASS`.

### B4 — Re-scan the corpus with `monx` correlation

`monster_record_scan.py` proves the 5-array `monx` layout on sectors 00 and 03 only.
Extend it to every sector that has placed monsters and report where the layout holds.

**Deliverable:** a per-sector table; any sector that breaks the layout is a finding.

---

## Tier C — tooling and hygiene

### C1 — `tools/strings_atlas.py`

A string extractor that resolves each embedded string to its **owning function** and
prints that function's name-evidence alongside it.

Why: at least three wrong claims today came from reading bare strings with addresses and
no owner — two adjacent spell catalogues were assumed to be town variants (they were the
cleric shrine and the wizard shop), and a commerce attribution was nearly "corrected"
into error. A harness that shows the owner makes the wrong story unreadable.

**Remember:** `asm/` filenames are **runtime** offsets; `L_xxxx` labels inside them are
**file** offsets, `0x200` apart. See `docs/decompiling.md` § address bases.
**Verify:** run it on the spell catalogues and confirm it attributes them to
`asm/1869_cleric_shrine` and `asm/19D7_magic_shop_menu` respectively.

### C2 — Roll the handoff forward

Create `docs/handoff-<date>.md`, move `handoff-2026-10-23.md` to `docs/archive/`,
and update the pointer in `AGENTS.md`. Carry forward: the four open threads, the standing
traps, and the fact that CP5 now implements C1/C2/C4/C5/C6 with all gates green.

### C3 — Feed `docs/harness-portability.md` § 6

Generic lessons from this session that belong in the design report, method only — no
addresses, DS layouts or symbol names from this binary:

- a screenshot tool existed all along; a **truncated tool listing** made it look absent,
  and a plausible partial list is worse than an error
- an X display for a GUI emulator must be **24-bit**; 8-bit is the classic DOS recipe and
  silently yields a blank screen with a happy log
- an extractor that prints a string without its owning function invites a wrong story
- a data file's records can contain **leftover source text from the platform the game was
  ported from**; sampling only the head of a file misses it

---

## Explicitly excluded, with reasons

| excluded | why |
| --- | --- |
| **G22 — splitting `asm/19D7`** | boundary change to the byte-proof harness; needs owner approval. A5 proposes only |
| **Any combat implementation, including C3** | the contract forbids implementing combat unattended. C3 is additionally blocked on passability |
| **CP5's actual gate** | "owner wins a fight" is by definition not unattended |
| Any delete, move or truncation outside the working dir | human approval per `AGENTS.md`; `/tmp` and `/var/tmp` hold other people's live data |
| Anything needing a wiki, a sibling project, or a "more original" binary | isolation rule, `AGENTS.md` § 0 |
| Renaming any `asm/` file on a third-party name | names from wikis are hypotheses; the bytes get veto |
| The VNC container | already built at `tools/vnc/`; it exists for a human to drive, which is the opposite of unattended |

---

## Progress report

Leave this section filled in at the bottom, whatever happened. An overnight run that
produced nothing but an honest report of what it tried and where it got stuck is a
successful run; one that produced confident prose with no citations is not.

| task | outcome | evidence / reason |
| --- | --- | --- |
| A1 | **DONE** | `docs/place-names.md` § 2c. The 15 sign-bearing sectors are exactly the 15 with a `tlkx` — the sets are equal, all ones-digit 1/2/3, none in 0/4/5. Nine sectors corroborate internally (speaker ↔ sign). `tools/tlkx_decode.py` added as the reference decode |
| A2 | **DONE — negative, with a control** | No vertical signage in any of 33 sectors. Every column run is two stacked horizontal signs or the lone-`I` graphic (`I` is 55.6% lone, 4× any other non-A/Y letter). `tools/map_signs.py --selftest` plants a real downward sign and was perturbed two ways to confirm it goes red. First pass's real misses were two-line signs and 1556 lines of A/Y noise |
| A3 | **DONE, with a named gap** | `DS:27/28` is the price operand — printed by `asm/51A5`/`asm/53B5` as four BCD digits and charged by `asm/2052` from the same two bytes. Corrects § 3. Tavern `0005` and clue-seller `×100`-as-byte-placement closed statically; who loads `0200` for the vendor goes cold at `asm/20AB` and is registered as **G23**. Also caught and corrected my own branch-polarity error in the same commit |
| A4 | **DONE** | `tools/dungeon_spawn_check.py`: `HP = ((2*type + slot>>1)*8) | 0x10`, **94/94** slots over four captures. `[DI+0x1B7]` is `slot>>1`, not a kind. The guard makes the slot the difficulty ladder. The formula reproduces the fan table exactly, uniformly −1, on both ladders and the pairing. Stamp population matches the monster histogram; the (x,y)→cell transform is **not** established and says so |
| A5 | **DONE — proposal only, nothing split** | `docs/g22-split-proposal.md`. The premise was wrong: `2028..2130` is three shared helpers (7/5/3 inbound callers, and the only `ret`s in the span are at `2051`/`20AA`/`2121`), not shop code. Recommends merging 15 fragments into 3 routines and explicitly advises *against* subdividing `19D7..2027` on fall-through grounds |
| B1 | **DONE — G20 closed** | `captures/spice86/tower-34`, `tower-34-open`. Sector 34 is dungeon-shaped: aligned map bytes, `IT'S DARK!`, torch + light counter, arrows = TURN LEFT/RIGHT, dungeon monster encoding (40/40 formula). Split is `{4,5}` vs `{0,1,2,3}`. Two non-carries: `A` is ATTACK, and the viewport renders empty |
| B2 | **DONE — G1/G18 closed** | `captures/spice86/art-index/`. The renderer reads the **grid cell**, not the monster `type`: on the only disagreeing sample the drawn tile matches `cell>>2` at 256/256 and `type>>2` at 171/256. Geometry confirmed on 18 further slots at 256/256. `docs/art-index.md` |
| B3 | **DONE — G8 closed** | `captures/spice86/armour-buy/` — first shop interior ever reached. `DS:97` went `00→01` buying CLOTH (menu item 1), gold −12 = the quoted `0012`. **1-based**: the six types are `DS:97..9C`. Shop closes after one purchase, so a second type was not observed; `maxout` leaves the table empty while `DS:62` says POWER is worn. Selector question closed too: an NPC standing at the sign (`tools/shop_access.py`, 50 pairings) |
| B4 | **DONE** | `tools/monx_layout_scan.py`. **Six** arrays, not five — `monx[160:191] → DS:0x1D7`. `type`/`HP` hold everywhere testable; **`kind` mismatches in 3 of 4 sectors**. `monxNN` non-empty exactly where `tlkxNN` exists plus sector 00. Tail: `[256]=0x1A` and `[257:384]==[129:256]` in every file checked |
| C1 | **DONE** | `tools/strings_atlas.py`. 336 strings, every offset verified byte-exact against the exe; selftest asserts the cleric catalogue → `asm/1869_cleric_shrine` and the wizard catalogue → `asm/19D7_magic_shop_menu` and that they differ. The control failed for real twice during development, which is what makes it worth keeping |
| C2 | **DONE** | `docs/archive/handoff-2026-10-23.md`; 2026-10-22 archived; pointers updated in `AGENTS.md`, `phase4-state.md` and the four documents that cited it |
| C3 | **DONE** | Seven entries added to `docs/harness-portability.md` § 6, checked clean of addresses, data layouts, record formats and symbol names from this binary |

Gate status at end of run: `make -C engine test` **PASS (CP0–CP4, CP5 spec, tiles art)** · `assemble_clean.sh` **CLEAN REBUILD BYTE-MATCH: 37344 bytes identical**

### Notes for whoever picks this up

* **Four tickets closed** (G1, G8, G18, G20) and **one opened** (G23). `docs/needs-gameplay.md`
  is updated in place; nothing was closed without naming a capture directory.
* **`docs/g22-split-proposal.md` needs an owner ruling** and it also *retracts* advice that
  `commerce.md` § 3 gave, so it should be read before anyone acts on the older text.
* **Three of today's findings corrected text written earlier the same day** (the price
  operand, the branch polarity, the array count). Each correction names what it replaced
  rather than editing silently — see the handoff's § Corrections list.
* **Nothing was pushed.** `git push` needs `GH_TOKEN` sourced from `.env`.

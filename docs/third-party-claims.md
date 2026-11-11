# Third-party claims: the Adric/Alzurian cheat sheet

**Grade: third-party hypothesis. The bytes get veto.** This file exists so a period walkthrough
can be *tested* against `asm/` rather than absorbed into it. Nothing here is a finding about this
binary unless a byte citation says so, and `AGENTS.md` § 3 is explicit that a third-party name
never gets promoted into an `asm/` filename on the strength of being third-party.

Source: "Ultima II: Revenge of the Enchantress — Cheat Sheet by Adric, Alzurian, with help from
Michael Stringer and The Beckster", a BBS-era walkthrough supplied by the owner 2026-10-28e.
Two other third-party items already on record: the grocery table (same day) and `bestiary/`.

Why it is worth a file rather than a paragraph: it names **concrete item gates for three of the
four command handlers this project parked as unproven**, which converts "needs a capture to find
out" into "needs a capture to confirm a specific prediction". That is a much cheaper experiment,
and a prediction that fails is a finding in its own right.

## 1. Claims already corroborated by bytes

| claim | byte evidence | grade |
| --- | --- | --- |
| `V` (View) requires a **helm** | `asm/437E_cmd_V.asm` prints `VIEW<0x8d>WITH MAGICAL HELM!` | corroborated — the string exists in the handler that the cheat sheet says needs the item |
| `L` (Launch) a plane requires a **brass button** | `asm/38B0_cmd_L.asm` prints `FUNNY THIS PLANE IS<0x8d>MISSING A BRASS BUTTON!` | corroborated — a refusal message for exactly that absence |
| `B` (Board) a frigate requires a **blue tassle** | `asm/4C55_name_pool.asm` holds `db "BLUE TASSLE",0x00` at pool[58] | corroborated — and the item name is binary text, not a walkthrough paraphrase |
| `L` (Launch) a rocket consumes a **tri-lithium** | `asm/363B_cmd_G.asm` prints `TRI-LITHIUM!`; `asm/4C55_name_pool.asm` holds `db "TRI-LITHIUM",0x00` at pool[61] | corroborated from two independent sites |
| `W` (Wear) offers an armour menu with a strength gate | `asm/43F3_cmd_W.asm` prints `WEAR ARMOUR:` + `1-CLOTH, 2-LEATHER, 3-CHAIN, 4-PLATE, 5-RE...` | corroborated as a menu; the gate itself is the unresolved part (see `commands.c`) |
| the **force field** needs the ring | CP5 ticket C5: ring gate `[DS:D6]`, exactly −1000 via `stc; sbb al,0x10; das`, both branches executed in `captures/spice86/g29-field` | **proven**, and the cheat sheet independently agrees |
| Charisma affects **prices** | `asm/20AB_bcd_price_from_stats` derives a price from `[DS:50] + [DS:4E]`, and higher stats give a *lower* number; the grocery table's 52 gp is the 6th value the loop can produce | corroborated twice from two third-party sources; still not proof, and the `DS:4E` charisma-vs-wisdom dispute is untouched |
| **gremlins steal food** | `asm/12DB_dungeon_events.asm` prints `A GREMLIN STOLE SOME FOOD!` | site exists; the claimed *fixed 100 units* is unverified and is worth checking, because CP5 C6 already found one "fixed" theft in this game was really a rolled BCD decrement |

### A second source: the official manual, by owner recollection (2026-10-29)

Different provenance from the cheat sheet above, and it should be kept visibly separate: this is
not a document held in the repo. The owner recalled two things from the shipped manual, grade
`recalled`, and both were then checked against the bytes. Both agreed, and in both cases the bytes
said something the manual could not.

| manual claim (recalled) | byte evidence | verdict |
| --- | --- | --- |
| `J` lets the character "jump up and down", a way to release frustration; it crosses nothing and touches no mechanic | `asm/37DC_cmd_J.asm` is three instructions — `call str_out`, the string, `jmp cmd_done` — with no write of any kind | **corroborated, and the bytes go further**: a handler with no write cannot touch a mechanic. The emptiness is the feature, so it is not a stub to be finished. |
| `Y` takes freeform text and ends it with Enter | `asm/4542_cmd_Y.asm` falls into `asm/4551..4567`: spin on the key seam, echo each character, `cmp $0xd,%al` to end, print the Enter, `cmd_done` | **corroborated**, and it resolved a capture this project had misread for four days — see `docs/needs-gameplay.md` finding #1. The range writes nothing, so the yell evaporates. |

The useful generalisation, and the reason both rows are in this file rather than in prose: a
manual can explain *why* a byte pattern is deliberate, which a disassembly cannot. It cannot
establish *what* the pattern does, and in both cases above the bytes were already sufficient —
what the recollection supplied was the reason to re-read a range nobody had bothered with.

## 2. Claims the binary does not corroborate (yet)

- **"Negate Time needs a strange coin."** ~~No `STRANGE COIN` string exists anywhere in `asm/`~~
  **the absence is explained, 2026-10-25: the name is in the pool.** `STRANGE COIN` is entry 13
  of the extracted items table and `QUICK SWORD` entry 9 of the weapons table (both 0-based, as
  `extractor.py` emits them) of the indexed name pool that
  `docs/status-screen-z.md` § 7 describes — `asm/4C55_name_pool.asm`, which is why neither string
  appears in `asm/` as a `db` at a call site. The pool is
  read by `engine/tools/extract/extractor.py` from the exe and asserted against the `HANDS == 19`
  anchor, so both names are binary text, not walkthrough paraphrase. **So this is not a
  refutation of the claim and never was.** What the claim still lacks is the other half: that
  `cmd_N` consults that item at all, which is the parked-handler question in
  `engine/src/game/commands.c`, and it needs a capture of `N` running (it is one of the four
  handlers with no execution evidence).
- **"Launch a plane needs a skull key."** ~~not corroborated~~ **REFUTED AS A GAP — it is
  corroborated.** The first search looked for `SKULL KEY` (singular) and found nothing; the hint
  table says `AVIATORS USE SKULL KEYS!`. Recorded here because the miss was the search's, not the
  binary's, and because "absent from my grep" is not "absent from the game".
- **"The Quicksword is the only weapon that can kill Minax."** Not checked. It is a strong,
  cheap, falsifiable claim and `asm/2BA5_attack_arms_gate.asm` is the place to look.

## 3. The real find: an in-game hint table in the binary, and a decode defect on top of it

Chasing the cheat sheet's item names turned up a contiguous NUL-separated **hint table** in
`original/ultimaii.exe`, which states several of these requirements from
the binary itself rather than from a walkthrough. Measured 2026-10-25, the seven run file
`0x18E8..0x1997` (runtime `0x16E8..0x1797`), preceded in the same blob by three pieces of NPC
chatter (`"CAUGH..."`, `"ISN'T THIS A GREAT GAME?"`, `"HHMMM..."`) from file `0x18BD`:

```
SOME FIGHTERS WEAR MAGIC HELMS!
AVIATORS USE SKULL KEYS!
SAYLORS WEAR BLUE TASSLES!
MAGES CARRY WANDS OR STAFFS!
GUARDS CARRY KEYS!
ANKHS OPEN SPACE!
PLANES NEED BRASS BUTTONS!
```

That is seven in-game claims mapping onto the cheat sheet: helms for the view, skull keys and brass
buttons for launching a plane, blue tassles for a frigate, keys carried by guards (the cheat sheet
says exactly that: "to get keys, kill guards"), and ankhs opening space. It is the strongest
third-party-independent corroboration in this file, because the strings are the game's own.

**How they are reached, because an address search alone says the opposite.** Nothing in the
executable loads any of these addresses: there is no `mov reg, imm16` to any entry, no word
anywhere equal to one, and no offset table — so a reference scan concludes the blob is dead data
and the corroboration claim collapses. It is not dead. The tavern menu's tail stores a **cursor**
into it: at runtime `0x16a8..0x16af`, `lea ax,cs:0x16bc ; mov ds:0x27,ax`, then a call to
`asm/155D`, which sets `ES=CS`, loads `DI` from `[DS:27:28]`, skips `SI` NULs and emits the entry
it lands on. The cursor is set one byte early, to the NUL that ends the menu's own code, because
the walker skips to the next NUL before it starts counting. Recorded in the headers of
`asm/16BD_barkeep_chatter_hints.asm` and `asm/155D_chatter_table_selector.asm`.

Two consequences worth stating. `DS:27:28` is shared scratch — `docs/commerce.md` § 3b measured the
same two bytes as the shop price operand, so it has at least two unrelated jobs, the same hazard as
`DS:1F`. And the range runs to file `0x1998`, not `0x1997`: the last byte is the terminator of
`PLANES NEED BRASS BUTTONS!`, which an earlier line of this section cut off.

**And the defect, stated correctly after a first version of this section got it wrong twice.**
The strings are complete in the binary; what was broken was the reconstruction. The first error
was location: the fragments quoted below were attributed to this hint table, and they are not
from it — `db "UE TASSLE"` is the tail of the **name pool's** `BLUE TASSLE` item entry, and the
hint table is a different region entirely, at file `0x18BD..0x1997` (runtime `0x16BD..0x1797`),
tucked at the tail of `asm/15E1_tavern_menu.asm` where the clean wave left it as honest
`.byte` forced-keeps rather than invented mnemonics. The second error was thinking the two were
one problem.

What the old listing looked like, in the region this section first pointed at:

```
L_00506f:  add    %al,0x4c(%bp,%si)      # these are string bytes, not code
L_005072:  db "UE TASSLE"                # tail of pool[58] "BLUE TASSLE", NOT the hint table
...
L_005093:  add    %dl,0x52(%si)          # also string bytes
L_005096:  db "I-LITHIUM"                # tail of pool[61] "TRI-LITHIUM"
```

— the clean wave decoded text as machine code and cut the strings mid-word, and its own
`# strings:` line admitted fragments like `"GGER"`, `"CE"`, `"ORD"` alongside 48 others. Same
class as the G22 splits and the `asm/353A`/`asm/353D` forced-keep swallow.

**The name-pool half is fixed as of 2026-10-25**: it is `asm/4C55_name_pool.asm` now, one entry
per line with its index tagged, byte-identical under `assemble_clean.sh`. **The hint table is
not**, and it is the obvious next cut: ten entries, `"CAUGH..."`, `"ISN'T THIS A GREAT GAME?"`,
`"HHMMM..."` and then the seven claims this file is about, sitting in one contiguous
NUL-separated run with code ending in a `jmp` immediately before it. `tools/fragment_splits.py`
nominates the tavern file; the method is already written down in `asm/4C55_name_pool.asm`'s
header. **A walkthrough read led to a decode-tree bug** — a route nobody here had tried.

## 4. Claims that are out of scope or parked

Dungeons (`food declines in dungeons`, the dungeon light timer, the wireframe view) are parked per
`AGENTS.md` § 3. Planet coordinates (X = 9-9-9, Earth = 6-6-6), the time-door network, Minax's
teleport at low HP, and the specific NPC scripts (Lord British's 50-gold-for-300-HP, the Hotel
California merchant's random +4 attribute, Father Antos's blessing, the old man's 900 gold for
the ring) are all town/NPC behaviour, which is CP6 and needs the town command set first.

One of them deserves flagging because it contradicts a measured finding rather than an open
question: the cheat sheet distinguishes **non-diagonal and diagonal versions of the game** and
gives different routes for each. This project measured a diagonal step at `asm/212e` and recorded
it as simply how creatures move. If diagonals are a *version* difference, that measurement is
build-specific and the engine may be modelling a variant. Worth a look before CP6 hard-codes it.

**Three of those parked NPC claims have since closed, and one closed wrong** (2026-11-08 to
2026-11-11, all static plus one owner play session each side):

* *the Hotel California merchant's random +4 attribute* — **correct, and now observed.** One RNG
  draw decides both whether it fires (`r < 6`, so 75%) and which stat it lands on, and the gain
  is four BCD adds of the hundreds digit, so +4 per 100. There is no ceiling guard: owner play
  2026-11-11 offered the clerk in sector 32 with every stat at 99 and got 4 back, which is the
  wrap the bytes predict (99 → 00 with the carry chained into the next of the four adds → 04).
  The claim's "random" is doing work here: the stat is not chosen by the amount offered.
* *Father Antos's blessing* — **correct.** `DS:6C`, written by `asm/4209:422B` on Planet X in a
  castle sector by role `0x81`. See `docs/npc-index.md` § 4 and G61.
* *the old man's 900 gold for the ring* — **the mechanic is right and the number is not.** The
  ring is `asm/3C2E`'s role-`0x82` arm in sector 32, gated on the blessing, but both thresholds
  in the bytes are `cmp $0x5`: five hundreds, so **500**, and the amount is one digit — 900 is
  not enterable. The claim is a plausible misremembering of the same handler's other figure, and
  the digit width is what rules it out rather than the walkthrough disagreeing with itself.
  **And it is spent, not just compared** (2026-11-11): `asm/3C2E` calls `asm/2052_gold_spend_price`
  before it looks at who the recipient is, so the 500 leaves the purse even when the answer is
  EARN THE RING!, and a player short of it gets YOU DONT HAVE THAT MUCH! instead of ever hearing
  about the blessing. That also makes the clerk's random +4 a **purchased** service at 100 gold
  per 4 points, which is a second, independent reason the walkthrough's "stop at 95" advice
  exists: 95 + 4 = 99 is the last raise that does not wrap, and each one costs money.

## 5. How to use this file

Treat every row as a prediction with a named place to test it. When a capture or a byte settles
one, the finding goes in `asm/`'s header or `docs/spice86-live-findings.md` — **not here**. This
file is the intake queue, and a row that has been settled should be replaced by a pointer to where
the answer now lives, so there is never a second copy of a conclusion.

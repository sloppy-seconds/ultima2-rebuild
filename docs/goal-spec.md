# Goal Specification — What "Winning Ultima II" Means, in Engine Predicates

**Status: specification only. Nothing here is built.** No policy code, no endgame detector,
no bot. This file turns third-party walkthrough prose into predicates an engine test can
evaluate, so that CP8 (`docs/phase4-state.md`: *Endgame + full playthrough*, `todo`, locked)
has a definition instead of a vibe.

> **Provenance, and the grade of everything in it.** The source is *Ultima II walkthrough*,
> The Codex of Ultima Wisdom — "a heavily modified version of the original by Fallible
> Dragon" — fetched 2026-11-08 from
> `https://wiki.ultimacodex.com/wiki/Ultima_II_walkthrough`. It is **third-party prose about
> a game, not evidence about `original/ultimaii.exe`**. Per AGENTS.md § 3 and § 5 every claim
> below carries a grade, and the third-party ones are graded `recalled` at best unless our own
> bytes say otherwise. The precedent for why: `bestiary/` attaches community *names* to tiles
> we matched by bitmap identity, and `docs/moongates.md` records three of *this project's* own
> decodes being refuted by capture. Third-party data is a hypothesis the bytes get to veto.
>
> § 6 is the verification ledger: which claims were checked against the engine, and what came
> back. It is the most important section in this file, and the one to read first if you only
> read one.

---

## 1. The win condition, as an engine-checkable detector

The walkthrough's endgame paragraph, verbatim in substance:

> Once you have the force field ring, the Quicksword, 9900 or more hit points from Lord
> British, and power armor, it's time to kill a sorceress. Head to Shadowguard, located in
> the center of the Time of Legends. […] In the northeast corner of the castle, attack
> Minax. One hit! […] she has teleported to the chamber in the southwest corner. Head there
> and attack again. Keep switching between the two until she dies.

Split into a **readiness conjunction** (checkable before entering) and a **fight loop**
(checkable during).

### 1.1 Readiness — all four must hold

| # | Predicate | Engine field | Grade |
| --- | --- | --- | --- |
| W1 | force field ring held | `state.items_held[0] != 0` — index 0 is the pool's `RING` entry. Worth noting the engine writes the **bare literal 0** here: there is no `U2_ITEM_RING` define, unlike the five vehicle tokens | **measured** — `overworld.c:856` gates `u2_force_field` on exactly this byte, and the refusal string `RING PROTECTS FROM FIELD!` is `U2_S_FIELD_RING_PROTECTS` |
| W2 | quick sword readied | `state.readied_weapon == 9` (pool `HANDS…QUICK SWORD`, DS:0x61) | **measured** — the pool order is committed content; the wield path is `cmd_R` |
| W3 | power armour worn | `state.worn_armour == 6` (pool `…REFLECT, POWER`, DS:0x62) | **measured** that 6 is POWER and DS:0x62 is the worn slot. **The requirement is probably over-specified**: the engine's only measured armour gate, `u2_space_armour_ok`, accepts `>= 5`, so REFLECT satisfies it, and the walkthrough's own table marks both as mandatory for space. Requiring POWER specifically is folklore unless something in Shadowguard distinguishes them by evasion rate — and nothing has been found that does |
| W4 | displayed HP ≥ 9900 | HP is the packed-decimal pair `[0x51],[0x52]` (proven in `game/status.h`'s table: `H.P.= [0x51],[0x52]`) | **recalled** — 9900 has never been observed in any capture, and no capture has ever met Minax. The threshold is a number to test, not a fact to trust |

W4 deserves the suspicion it looks like. Four packed digits cap displayed HP at 9999, so
"9900 or more" is suspiciously close to the ceiling, and the number is exactly what a
walkthrough author would write after watching a character get topped up to near-max. The
detector should therefore treat W4 as **a threshold to be confirmed by reaching it**, not as
a precondition to be argued about. What is *measured* is only the encoding.

### 1.2 Location — "inside Shadowguard, in the Time of Legends"

| # | Predicate | Engine field | Grade |
| --- | --- | --- | --- |
| W5 | era is Time of Legends | `state.sector_first == 0` — DS:0x49 is the era index on Earth, and `asm/3546_time_index_message.asm` reads tens 0 as `ANOS: LEGENDS!` | **measured** for the era reading (`docs/moongates.md`, `captures/spice86/moongate-cycle`); **recalled** that 0 is where Shadowguard is |
| W6 | inside a site | `state.sector_second != 0` (DS:0x4A is the site kind, and the ones digit *is* the kind) | **measured** |
| W7 | Minax is on the board | `∃ i : state.mon_type[i] == 0x40` | **measured** — `asm/1040_minax_proximity_check.asm` identifies Minax as creature type `0x40` (art index `0x40>>2 == 0x10`), and that routine only runs when `DS:4A != 0`, i.e. inside a site, which is consistent with W6 |

W7 is the strongest predicate in this section and the one the engine should lead with: it is
the only endgame object with a byte-level identification already on disk.

### 1.3 The fight — the detector, not the precondition

The win signal is **`mon_type[i] == 0x40` going from present to absent while the character is
alive**, with the teleport pattern as the loop's shape:

```
win := (Minax was present at turn t) AND (Minax is absent at turn t+k)
       AND (character alive)
       AND (the last player action against her cell was an attack)
```

Three things the engine already knows that make this detector non-trivial:

* **Minax strikes from up to four cells away.** `asm/1040_minax_proximity_check.asm` tests
  `mon_type == 0x40`, then `|mon_x - player_x| < 4` and `|mon_y - player_y| < 4` via
  `asm/50EF`, then calls `asm/0F8E_combat_minax_attack`. So "adjacent" is the wrong range
  for her, and a bot written to the ordinary melee range will take damage it cannot explain.
  The 100-damage figure in that routine's header is marked *external claim … needs a live
  re-check*; treat it as unverified.
* **`MINAX IS DEAD!!` exists as a string** — in `asm/1040_minax_proximity_check.asm`'s
  region — and it is also the exact string AGENTS.md § 4 names as a *false positive* of
  reading CFG blocks instead of `ExecutedInstructions`. So the string is a candidate win
  signal and a known trap at the same time. **A detector that greps for it in discovered
  blocks would be the mistake this project has already made once.** If it is used at all, it
  must come from the executed-instruction stream plus a frame.
* **Death is memory-only, not a save** (`docs/handoff-2026-11-03.md`). A failed attempt is
  not recoverable by reloading, so the detector must distinguish *dead* from *winning*
  immediately, in-session, or the bot will lose the run silently.

### 1.4 What the detector must NOT be

Not a screen-scrape of the ending text. The C64 ending is a different artefact from the DOS
one (the walkthrough illustrates it with `EndingU2C64.PNG`), and this project's ground truth
is the DOS binary. The predicate set above is state-based for that reason, and § 5 lists the
port-specific claims to keep out.

---

## 2. The key-item dependency graph, as checkable predicates

The valuable part of the walkthrough is not the route, it is this graph. Five of its edges
are **already implemented and byte-cited in the engine** — which means the graph is not
third-party opinion, it is partly our own evidence with a community label on top.

| Edge | Predicate (gate) | Engine evidence | Grade |
| --- | --- | --- | --- |
| ankh → board rocket | `items_held[U2_ITEM_ANKH] != 0` | `overworld.h:253`, DS:DD, `asm/3048`, refusal `YOU MUST HAVE AN ANKH!` | **measured** |
| tri-lithium → launch rocket | `items_held[U2_ITEM_TRI_LITHIUM] != 0` | `overworld.h:257`, DS:E5, `asm/38B0`, refusal `SHIP INCAPABLE OF LAUNCH!` | **measured** |
| blue tassle → board frigate | `items_held[U2_ITEM_BLUE_TASSLE] != 0` | `overworld.h:256`, DS:E2, `asm/2FF6`, refusal `THE CREW OF THIS SHIP WILL NOT LET YOU BOARD!` | **measured** |
| skull key → board plane | `items_held[U2_ITEM_SKULL_KEY] != 0` | `overworld.h:254`, DS:DF, `asm/3048` | **measured** |
| brass button → launch plane | `items_held[U2_ITEM_BRASS_BUTTON] != 0` | `overworld.h:255`, DS:E1, `asm/38B0`, refusal `MISSING A BRASS BUTTON!` | **measured** |
| reflect/power armour → survive space | `worn_armour >= 5` | **measured, and implemented**: `u2_space_armour_ok()` in `engine/src/game/space.c:69` is exactly `worn_armour >= 5`, and **G60** closed the question by capture (`captures/spice86/g60-plate-halt`) | **measured** — with two sharp edges the walkthrough does not mention: the check is **not on the launch path** (a PLATE character launched fine and got `HYPERWARP ENGAGED!`), and failing it is an **unrecoverable freeze**, not a refusal and not a death. So "mandatory for space flight" is true and misleading at once |
| Father Antos blessing → force field ring available | **no engine field.** There is no `blessed` flag in `state.h` | the ring's gate is `items_held[0]` alone (W1); the *offering* precondition is unmodelled | **unverified, and currently unrepresentable** — see § 7.3 |
| force field ring → survive Shadowguard fields | `items_held[0] != 0` | `overworld.c:856`; the alternative branch prints `FIELD CAUSES 1000 DAMAGE!` and the routine's own comment records "minus 1000, and death if that goes below zero" | **measured** |
| keys → open locked doors | **no item-pool entry.** `cmd_O`'s key consumption is not modelled by `items_held` | the walkthrough says keys come only from killing guards, two per guard | **unverified** — `cmd_O` is implemented, but its key accounting is not a predicate today |
| quicksword → harm Minax | `readied_weapon == 9` | no byte has been found that makes any other weapon fail against type `0x40` | **recalled**, and the source itself hedges: "in some ports of *Ultima II*, any weapon will do" |

Two structural observations that matter more than any single row:

**The five measured edges are all *boarding and launching* gates, and they are all in one
routine pair.** `asm/3048` guards the two boardings that need a token, `asm/38B0` guards the
two launches. That is a shape, not a coincidence: the original put the vehicle-token checks
in two places, so a bot's dependency resolution is really "get these five bytes non-zero",
and the engine can answer that question today with five array reads.

**The unverified edges are all *story flags*, and the engine has no story-flag vocabulary for
them.** `state.h` has exactly one such byte, `story_flag` at DS:0x239, described as
"story/poem shown". Father Antos' blessing, the old man's willingness to sell, and
Santre's willingness to hand over the quicksword are all of this kind, and none is
represented. This is the single largest gap between "the engine can model the goal" and
"the engine can *check* the goal", and it is a capture problem before it is a code problem:
nobody has watched the blessing happen, so nobody knows which byte it writes.

---

## 3. Policy rules the walkthrough contains, translated

These are the parts of the prose that are *strategy* rather than *route*. Each is written as
a rule a policy module could implement and a test could check. None is built.

### P1 — The 38% discount: `Int + Cha` exactly 40, from three specific splits

> If you create a character whose "Int+Cha sum" is exactly **10+30 or 20+20 or 30+10**, you
> get a 38% discount on all prices since game start!

Translatable, and unusually well-specified: three exact vectors, not a range.

```
creation_policy: (stat_cha, stat_int) ∈ {(0x10,0x30), (0x20,0x20), (0x30,0x10)}
```

The engine's own arithmetic makes this reachable: `u2_creation_apply` takes the six stats,
the 90-point budget and the 10-point floor are the form's rules, and the class bonus is
applied by `commit()`. **The claim "38%" is unverified** — no capture has ever compared two
purchase prices across the discount threshold. What is verified is only that the three
vectors are constructible. The price table itself is a separate open decode.

The walkthrough also gives the shape of the price curve — cheaper near sum ≈ 40, cheaper
near ≈ 86, "skyrocket" past ≈ 160 — which is a *three-band* function of `Int+Cha`. That is
worth a capture of its own, because P1 and the Hotel California grind (P2) pull in opposite
directions on the same variable.

### P2 — Hotel California grind, with its own stop condition

> **[O]ffer** him gold… your chance of getting attribute gains is based upon your character's
> turn count. Offer just 100 GP at a time to minimize money losses, **[P]ass** or check your
> **[Z]tatus**, then offer 100 GP more. […] When any attribute (or the sum Int+Cha) reaches
> the limit (**95** for the single attributes, **155** for the sum Int+Cha), stop.

The rule, and the reason the numbers are 95 and 155 rather than 99 and 160:

```
loop while gold >= 100 and max(stat) < 95 and (cha + int) < 155:
    O, "100", CR
    P            # pass turns: the gain probability is a function of turn count
    read state; re-evaluate
```

The 4-point margin is the walkthrough protecting itself against the **wrap**, and the wrap is
real in our bytes: stats are one packed decimal byte each with **no ceiling guard on any add
site** (`docs/phase4-state.md`, the owner-recalled-and-then-confirmed wrap). A stat that
crosses 99 does not clamp, it resets. So 95 is not superstition, it is the largest stop point
that leaves room for a multi-point gain. **Grade: the wrap is measured; the 95/155 margins
and the turn-count dependence are recalled.**

Note the conflict P1 creates with P2: grinding pushes `Int+Cha` up toward 155, straight
through the ≈86 band and past the ≈160 cliff. A policy has to decide the order — spend the
discount window before grinding, or grind first and accept the price curve — and the
walkthrough never says. That is an open question for a capture, not a gap in this spec.

### P3 — Pass-turns spawning, and why it is the cheapest spawn driver

> Use the **[P]ass** command to pass turns while waiting for them to show up, since it uses
> less food.

Engine-checkable, and already partly ours: the per-mode food cost is implemented (CP5 W3),
and `cmd_P` is implemented. What is *not* measured is the comparative claim — that PASS
costs less food than walking. That is a two-run A/B with a food read, cheap, and an agent can
drive it headless. Until then P3 is a `recalled` optimisation on top of a `measured` command.

### P4 — Pangea as the farming era, and the spawn-deadlock warning

> Because enemies may spawn onto islands currently unreachable to you and will *not* respawn,
> Pangea may be the most prudent choice for this task, as it is a mostly contiguous
> continent.

This is the most interesting claim in the file, because it describes a **failure state the
engine can enter and cannot leave**: spawn slots occupied by unreachable creatures, no
respawn, no encounters, a soft-lock. The predicate is checkable — `mon_type[]` full of
creatures whose `(mon_x, mon_y)` the player cannot path to — and the recovery is the
interesting half (§ 4). Grade: **recalled**, and it is the kind of claim that has to be
either reproduced or dismissed before a bot is built on it. If it is true, a naive bot will
hang in Pangea forever and the hang will look like a bug in the bot.

### P5 — Frigate cannons do not consume food

> Now that you have got a frigate you can kill monsters by firing with the cannons, and you
> do not use up food!

Checkable and cheap: fire cannons, read food, compare against a PASS-only baseline over the
same turn count. The engine's food charging is centralised enough that a wrong claim here
would show up immediately. Grade: **recalled**.

### P6 — Board-then-kill, the frigate acquisition pattern

> wait for a frigate to appear, and then instead of killing it, endure the blasts, **board
> it, move away, and then kill the second frigate**.

A two-phase policy with a reason: killing the first frigate leaves you without the thing you
came for, and the second one arrives while you are still in range. The engine-checkable part
is that boarding a frigate *requires* the blue tassle (W/§ 2, measured) and that a boarded
vehicle becomes the player's. The "endure the blasts" part is a survivability calculation
against a damage number nobody has measured for frigates. Grade: **recalled** pattern,
**measured** precondition.

### P7 — Negate time to bypass what cannot be killed

> Do not attack the monsters, negate time to get past them. Be careful: there are several
> invincible monsters within the keep which can not be defeated by any means.

`cmd_N` (Negate) is implemented, and `STRANGE COIN` is the consumable that powers it
(`items_held[13]` — DS:E3 turned out to be a STRANGE COIN, per `docs/handoff-2026-10-29.md` § 5.
There is no `U2_ITEM_STRANGE_COIN` define yet, only the five vehicle-token defines). So the *mechanism* is measured; the
existence of unkillable creatures inside Shadowguard is **recalled and load-bearing** — if
Minax herself is one of them outside the quicksword, the whole readiness conjunction changes.
This is the highest-value capture on the list and it needs someone to reach Shadowguard.

### P8 — Guards as the only key source, and the invulnerable-guard tell

> There is only one way to get keys: kill a guard. You will get two keys for each guard you
> kill. […] there are guards who are invulnerable! These can be identified when you try and
> talk to them, as the game says: "Amusing! No answer".

The tell is a **string**, which makes it engine-checkable in principle: a policy can talk to
a candidate guard and read the message tap for that text before spending an attack. That is
exactly what the API's `message` field exists for (§ 7). Grade: **recalled**, though the
string's existence in the binary is checkable with `tools/strings_atlas.py` and has not been
checked as part of writing this file.

---

## 4. What the walkthrough does NOT provide

Stated plainly, because the failure mode of a walkthrough-as-spec is assuming the prose is
complete. It is not, in four specific ways.

1. **No spatial or map knowledge at all.** Every location is a name — New San Antonio, Pirate
   Harbour, Castle Barataria, "the red square", "the northeast corner" — and none is a
   coordinate. The engine has sector maps (`mapxNN`) and a site-kind byte, but no name→sector
   table, and this project has never built one. **A bot cannot use this file to navigate.**
   Navigation is a separate deliverable, and `docs/needs-gameplay.md` does not currently carry
   an item for "build the place-name → sector index".
2. **No recovery from failure states.** The walkthrough assumes a successful run. It names one
   soft-lock (P4) and offers no exit from it. It says nothing about death, about running out
   of gold, about spending the last tri-lithium in deep space, or about the case the
   handoffs already proved: **death is memory-only, so there is no reload to recover to**
   unless the bot checkpoints its own saves. Recovery policy is entirely unspecified here, and
   § 7.2 gives it its own module for that reason rather than pretending the prose covers it.
3. **No dungeon interior solutions.** The page links out to a separate document,
   *Dungeon solutions for Ultima II*, and contains none of it. Castle Barataria — which the
   blessing chain requires, "in the left hand area past a maze" — is a dungeon, and its
   interior is not in this file. CP7 (dungeons) is implemented in the engine, so the
   capability exists and the *route* does not.
4. **No time-door routes.** The walkthrough says "the Asian time gate in your starting
   timeline is a two-way gate to Pangea" and nothing else about the network. Meanwhile
   `docs/moongates.md` has a decoded table, rotating gates, and three recorded
   self-corrections. **Where the two disagree, our document wins, because it was measured
   from this binary.** The era rotation is time-dependent and the walkthrough describes gates
   as fixed places.

---

## 5. Version-specific and port-specific claims — quarantined

Claims the source itself marks as varying by port, or that plainly do. None may enter a
predicate without a DOS-specific capture.

| Claim | Why it is quarantined |
| --- | --- |
| "only the Quicksword will harm Minax (**in some ports of *Ultima II*, any weapon will do**)" | The source flags the variance itself. Our binary's answer is unknown: no byte has been found that gates damage against type `0x40` by weapon. |
| "If you are playing the original **Apple II** version, it is impossible to raise Strength during the game, therefore you must set it to 29 or 31 during character creation. This is fixed in the 1989 release." | About a different binary. Ours is the DOS port, and `references/apple/` is a corroborator and not a parent (`docs/apple2-reference.md`). Recorded here only because it explains *why* the armour minimums in § 6 are 29 and 31 — the author is writing for a build where those numbers are the only ones available. |
| The C64 ending screenshot | Different port, different artefact. Our ground truth is `original/ultimaii.exe`. |
| "Japanese versions: consumable" (blue tassles) | Different port. Our tassle gate reads `items_held[12] != 0` and does not consume — and that is measured, so the DOS answer is already known and it is "permanent". |
| Force field ring price: **500 GP** in the items table, **900 gold** in the walkthrough prose | The source contradicts itself. Neither number is verified against our price table, and the price table is not decoded. Do not put either in a predicate. |

---

## 6. Verification ledger — what the engine said back

The claims most worth checking were the numeric ones, because numbers are where third-party
prose is either corroborated or caught. Result: **the armour and weapon thresholds match our
bytes exactly**, which is a stronger position than this file started in.

The engine's wield and wear gates are the **same rule against the same kind of byte**:
`cmd_R` refuses when `digit*8 >= state.stat_agi` (`commands.c:644` is the armour twin,
`digit*8 >= state.stat_str`), and both compare against the **raw packed-decimal byte**, not
the displayed number. That single fact generates every threshold in the walkthrough's two
equipment tables. Evaluating the rule:

| Item | pool digit | engine-derived minimum | walkthrough's table | agreement |
| --- | --- | --- | --- | --- |
| Dagger | 1 | agility **9** | 9 | **exact** |
| Mace | 2 | agility **11** | 11 | **exact** |
| Axe | 3 | agility **19** | 19 | **exact** |
| Bow | 4 | agility **21** | 21 | **exact** |
| Quick Sword | 9 | agility **49** | 49 | **exact** |
| Cloth armour | 1 | strength **9** | 9 | **exact** |
| Leather | 2 | strength **11** | 11 | **exact** |
| Chain | 3 | strength **19** | 19 | **exact** |
| Plate | 4 | strength **21** | 21 | **exact** |
| Reflect | 5 | strength **29** | 29 | **exact** |
| Power | 6 | strength **31** | 31 | **exact** |

Eleven rows, eleven exact matches, including the two numbers the endgame actually depends on
(Power 31, Quick Sword 49). The derivation was run as code rather than done by eye, per the
project's derive-don't-eyeball rule; the boundary cases are what make it convincing — at
agility 48 the quicksword is refused and at 49 it is accepted, because `0x48` is 72 and
`9*8` is 72, while `0x49` is 73.

Two consequences worth stating. **First**, the walkthrough's equipment prose is trustworthy
on numbers, which raises the prior on the other numeric claims in § 3 and § 1 — though it
does not prove any of them. **Second**, the Apple II aside in § 5 now reads correctly: the
author says a 1989-era Apple build cannot raise Strength, so it must be created at **29 or
31** — which are precisely Reflect's and Power's thresholds from this table. That is a
third-party claim about a different port that our own bytes independently explain. It does not
generalise: the unchecked rows are unchecked, and the discount percentage, the price bands,
the 9900 HP and the whole Shadowguard interior are still third-party assertions.

**Method note, for `docs/harness-portability.md`:** a community walkthrough's *numbers* can be
validated against a reimplementation's arithmetic even when its *route* cannot be validated at
all, because thresholds fall out of the rules rather than out of the map. Where a walkthrough
states a minimum stat for an item, that is a claim about a comparison and a comparison is
already in the asm. Where it states a place, there is nothing to check without navigation, and
navigation is precisely what the prose never supplies.

---

## 7. Headcount for later — a Python bot on the existing API

**Not built. This section exists so the estimate is written down before the work is
estimated in conversation.** Paul's ruling: the bot is Python.

### 7.1 The shape

`engine/tools/u2api.py` already gives a Python caller `create()`, `key()`, `state()`,
`quit()`, and it raises rather than returning a truncated line. The step loop is therefore
already expressible:

```python
obs = session.key(action)          # one action in, one observation out
```

Everything above that is policy. The layering follows the spec's own sections, and each layer
is independently testable because each one's preconditions are predicates in this file:

| Layer | Module | Owns | Spec section | Testable today? |
| --- | --- | --- | --- | --- |
| 0 | `bot/creation.py` | pick the stat vector (P1), call `create()` | § 3 P1 | **yes** — `u2_creation_apply` is gated by AP2/AP3 |
| 1 | `bot/economy.py` | Hotel California grind, shopping, the discount window | § 3 P1, P2 | **no** — needs inventory and prices visible (§ 7.3) |
| 2 | `bot/navigation.py` | era/sector movement, time doors, place→sector | § 4.1, `docs/moongates.md` | **no** — needs a place index that does not exist anywhere |
| 3 | `bot/combat.py` | engage/disengage, board-then-kill, negate-to-bypass | § 3 P6, P7 | **partly** — `creatures` with `mon_type` is already in the observation |
| 4 | `bot/recovery.py` | checkpointing saves, death detection, soft-lock escape | § 4.2 | **no** — needs a death/defeat signal |
| 5 | `bot/goal.py` | the W1–W7 conjunction, the Minax loop | § 1 | **partly** — W1, W2, W3, W6, W7 are readable; W4 needs an HP decode |

The honest headline: **layers 0, 3 and 5 can be written today against the API as it stands.
Layers 1, 2 and 4 cannot, and the reason is not the bot — it is that the observation does not
yet expose what they need.**

### 7.2 Why recovery is its own layer and not an afterthought

The engine's death is memory-only. A bot that plays for an hour and dies has nothing to
return to unless it wrote a save. So recovery is not error handling bolted on at the end; it
is the layer that decides *when to save*, and every other layer's failure is expressed as a
call into it. That is also what makes the CP8 gate cheap to run repeatedly: a checkpointing
bot can be restarted at the last good state instead of replaying from creation, which is the
same argument D1–D6 made for save-scoped worlds.

### 7.3 What the API needs before a Python bot is worth writing

Ranked by what blocks the most layers. Each is a small change to `core/api.c`; none is a
design problem, and all of them are the kind of gap that is cheaper to name now than to
discover at 2am against a bot that cannot see its own inventory.

1. **Inventory, weapons and armour in the observation.** `items_held[16]`, `weapons_owned[10]`,
   `armour_owned[6]`, `readied_weapon`, `worn_armour` are all on the state block and **none is
   in `u2_state_json`**. Without them the bot cannot evaluate a single predicate from § 2, and
   W1–W3 of the win condition are unreadable. This is the blocking item. Extend the `char`
   block the API already emits rather than growing `u2_state_json`, for the reason recorded in
   `docs/play-api.md` deviation 4.
2. **A defeat signal.** Something that says "this character is dead and this run is over".
   Today a caller would infer it from HP, and HP is packed decimal, so an inference bug looks
   like a bot playing a corpse.
3. **HP as a decoded number, not two packed bytes.** The bot should not have to know about
   BCD. The engine already has `unpack_bcd`; the observation should use it. This is also what
   makes W4 testable rather than arguable.
4. **The era and world as named fields.** `sector_first` doubles as the era index on Earth and
   the world index off it (`docs/needs-gameplay.md` G45, resolved but subtle), and
   `docs/planet-maps-galaxy-patch.md` records that sector 20 is *both* Earth-1423 and Venus.
   A bot reading raw sector digits will get this wrong in exactly the way that took this
   project a capture to get right. Expose `era` and `world` as derived fields with the G45
   rule applied, so the subtlety lives in one place.
5. **A structured "what is the game asking for".** `awaiting` is always `key` (deviation 3),
   and the plan's finer kinds were never built. A policy wants to know whether the game is at
   a menu, a freeform word prompt, or a directional prompt. The message tap is evidence but
   not a contract.
6. **`mode` distinguishing dungeon and space.** Named as unmet in `docs/play-api.md`. P7 and
   the Shadowguard loop both happen in a dungeon, and a bot that thinks it is on the
   overworld will apply the wrong movement rules — the two rule-sets are explicitly not
   interchangeable (AGENTS.md § 3, the CP7 park lifting).

Items 1–3 are small and unblock three layers. Items 4–6 are each a decision about where a
derived meaning should live, and they should be decided once, in the engine, rather than
re-derived per bot.

### 7.4 What is *not* an API gap

The step loop, the key alphabet, character creation, and the digest are done. A bot can be
written, run, recorded by the demo subsystem, and diffed with `engine/tools/demo_span_diff.py` today. The
gap is perception, not control.

---

## 8. Open questions, ranked by what they block

| # | Question | Blocks | Who can answer it |
| --- | --- | --- | --- |
| Q1 | Which byte does Father Antos' blessing write, and does the old man's offer read it? **Already registered as G61** — `tlkx92`/`tlkx93` carry the chain and the ring is `items_held[0]` at DS:D6; what is unevidenced is the last link | the whole § 2 story-flag row, and any endgame detector that wants to *prove* the chain rather than assert the item | needs a capture; agent-drivable if the Planet X route can be scripted, otherwise owner-play |
| Q2 | Is Minax killable with a weapon other than the quicksword in **this** binary? | W2, and whether the readiness conjunction is real or folklore | needs a capture at Shadowguard; owner-play, since it needs the full chain |
| Q3 | Does the 38% discount exist, and what is the price curve against `Int+Cha`? **Registered as G68.** | P1, P2's ordering conflict | agent: two seeded characters, one purchase each, read gold |
| Q4 | Do frigate cannons really not consume food? **Registered as G69.** | P5 | agent: A/B with a food read |
| Q5 | Does PASS really cost less food than moving? **Registered as G70.** | P3 | agent: A/B with a food read |
| Q6 | Is the Pangea spawn soft-lock real, and is it escapable? **Registered as G71.** | P4, and layer 4 (recovery) | agent, but it needs a long scripted run |

| Q7 | Where is Shadowguard, in sector and cell terms? | § 4.1 navigation, and the Minax loop's NE/SW cells | needs a capture; owner-play |
| Q8 | Is 9900 HP reachable, and how does Lord British's top-up work? | W4 | needs a capture at Castle Britannia |
| Q9 | Do the invulnerable guards really print `Amusing! No answer`? **Registered as G72.** | P8's tell | agent: `tools/strings_atlas.py`, then a live talk |

Q3–Q6 and Q9 are agent-drivable and cheap, and all five are now on the worklist as **G68–G72** with who-can-do-it verdicts, per the rule that a question needing a capture belongs there rather than only in a spec. Q1 is already **G61**. Q1, Q2, Q7 and Q8 need the endgame route,
which is the same route the bot is meant to discover — so they are blocked on each other in a
way worth being honest about: **the walkthrough is not enough to build the bot, and the bot is
the thing that would verify the walkthrough.** The way out is to build layers 0/3/5 and the
§ 7.3 perception items first, and let a partial bot answer Q3–Q6 and Q9 without ever reaching
Shadowguard.

---

## 9. Deliberately not done here

* No policy code, no bot, no endgame detector. Paul's instruction was spec only.
* No `asm/` file renamed and no header changed: nothing in this file is a new
  address→behaviour decode about the binary. The thresholds in § 6 were derived from rules
  already named and already cited in `commands.c`, which is the correct home for them.
* No place-name index (§ 4.1). It is a deliverable, not a section of a spec.
* No CP8 gate. A gate for a goal this file cannot yet fully express would be a gate that
  cannot go red in the place that matters.

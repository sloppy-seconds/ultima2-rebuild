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

**The two-chamber teleport loop is now OBSERVED, not recalled.** Owner play session in the
original, 2026-11-12, `captures/spice86/minax-fight-live/`: *"every hit she teleports between
the north east chamber and the south west chamber."* The walkthrough's description was right
about both corners and about the mechanic being a repeat rather than a one-shot. What is still
**recalled and unverified** is the rest of that paragraph: that one hit is always a miss on the
first try, that the quicksword is required rather than merely sufficient, and the 9900 HP
threshold in W4. The owner also reported a ranged attack landing on him throughout, which is
consistent with the `asm/0AD3` specials acting at range and with magic missile having no resist
at all -- HP fell from 9000 to 2336 with a cloak in place.

Split into a **readiness conjunction** (checkable before entering) and a **fight loop**
(checkable during).

### 1.1 Readiness — all four must hold

| # | Predicate | Engine field | Grade |
| --- | --- | --- | --- |
| W1 | force field ring held | `state.items_held[0] != 0` — index 0 is the pool's `RING` entry. Worth noting the engine writes the **bare literal 0** here: there is no `U2_ITEM_RING` define, unlike the five vehicle tokens | **measured** — `overworld.c:856` gates `u2_force_field` on exactly this byte, and the refusal string `RING PROTECTS FROM FIELD!` is `U2_S_FIELD_RING_PROTECTS` |
| W2 | quick sword readied | `state.readied_weapon == 9` (pool `HANDS…QUICK SWORD`, DS:0x61) | **measured** — the pool order is committed content; the wield path is `cmd_R`. **And it cannot be bought**: the weapons shoppe's printed catalogue ends at `8-PH` (PHASER) and the shop's accepted bound is 8, so QUICK SWORD is absent from the catalogue rather than gated by it. Its only source in the binary is Santre in sector 32, whose grant sets `DS:7F` = `weapons_owned[9]`. The quicksword is a quest item, not merchandise — no amount of gold-grinding substitutes for the conversation |
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

## 1a. The victory chain, decoded statically on 2026-11-09

CP8 was blocked on the belief that the endgame needed a capture, because `MINAX IS DEAD!!` has
never appeared in an `ExecutedInstructions`. It does not. The whole chain is readable in `asm/`
and is now named there. This matters for the goal spec more than for any other file, because it
changes what W2 is even claiming.

```
asm/2CB5_combat_kill_minax_gate     mov 0x197(%di),%al ; cmp $0x40,%al ; jne <kill-drop>
      |                                                               |
      | type != 0x40 -------------------------------------------------+
      | type == 0x40 (fall through, no call edge)
      v
asm/2CCD_victory_call               call L_0074a0 ; jmp L_000c33
      v
asm/72A0 (file 0x74A0)              the victory sequence
```

**The gate inspects the creature's type and nothing else.** No weapon check, no damage-source
check, no quick-sword test, no HP threshold, no era test. That is the finding, and it lands on
W2 directly:

> **W2 ("quick sword readied") is not a precondition of the victory condition.** The path to
> `MINAX IS DEAD!!` never reads `readied_weapon`. So either the quick sword is required
> *upstream*, in the damage arithmetic that brings Minax's HP to zero, or the claim is
> folklore. This is a much smaller question than "is W2 true", and it is now located: the
> damage formula is `((8 * readied_weapon + strength) >> 2)` on raw packed decimal
> (`combat.c:347`, from the asm), so the question becomes whether Minax's HP can be reached at
> all without index 9 -- which is arithmetic, and answerable without a capture.

W4 ("displayed HP >= 9900") also moved. Minax's attack is now measured: it subtracts one from
the **hundreds** byte of the packed-decimal HP pair, so it is exactly 100 HP per strike and it
kills by underflow, not by zeroing. At 9900 that is 99 strikes; the ceiling the instruction can
address is 9999. So 9900 is no longer a bare recollection -- it is a number with a mechanism
under it, and the mechanism is `asm/0F8E`.

What the ending *does*, once fired: narrows the print margin to 40, prints
`MINAX IS DEAD!! / ALL HER WORKS SHALL DIE!`, repaints a whole 4096-cell sector with `0x74`
(art `0x1D`, solid black), names `MAPX30` inline, prints `YOU FEEL A STRANGE FORCE!` and then
the completion text, sets `view_mode = 0xFF`, `player_stamp = 0x28` (the rocket stamp) and the
player position to `(0, 0)`, and enters a looping animation that never returns.

Two grades to keep apart. The loop bounds, the fill byte, the state writes and the embedded
filename are **measured from the bytes**. The reading of `L_005417` and `L_0061a6` as
cell-addressing and file I/O is **inferred**, and none of it has ever executed -- see
`docs/needs-gameplay.md` G75 for the capture that would settle the difference.

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
| Father Antos blessing → force field ring available | **`DS:6C`** — one byte, with a named writer and a named reader | `asm/4209` sets `DS:6C=1` when talking to role `0x81` with `DS:6D==9` and `DS:4A==3` (Father Antos, `tlkx93` record 1); `asm/3C2E_cmd_O` grants `DS:D6=1` (the ring) only when `DS:6C != 0`. Closed 2026-11-08; see `docs/npc-index.md` § 2 and G61 | **measured** (static) — this row said "unrepresentable" and was wrong in the good direction |
| force field ring → survive Shadowguard fields | `items_held[0] != 0` | `overworld.c:856`; the alternative branch prints `FIELD CAUSES 1000 DAMAGE!` and the routine's own comment records "minus 1000, and death if that goes below zero" | **measured** |
| keys → open locked doors | **no item-pool entry.** `cmd_O`'s key consumption is not modelled by `items_held` | the walkthrough says keys come only from killing guards, two per guard | **unverified** — `cmd_O` is implemented, but its key accounting is not a predicate today |
| quicksword → harm Minax | `readied_weapon == 9` | no byte has been found that makes any other weapon fail against type `0x40` | **recalled**, and the source itself hedges: "in some ports of *Ultima II*, any weapon will do" |

Two structural observations that matter more than any single row:

**The five measured edges are all *boarding and launching* gates, and they are all in one
routine pair.** `asm/3048` guards the two boardings that need a token, `asm/38B0` guards the
two launches. That is a shape, not a coincidence: the original put the vehicle-token checks
in two places, so a bot's dependency resolution is really "get these five bytes non-zero",
and the engine can answer that question today with five array reads.

**The story-flag edges have now largely collapsed into bytes, which is the opposite of what
this section first said.** The blessing is `DS:6C`, the ring is `DS:D6`, the quicksword grant is
`DS:7F` — three quest bytes with named writers and readers, all found statically from `asm/4209`
and `asm/3C2E_cmd_O` without a single capture. What remains genuinely unrepresented is much
smaller than the original claim: whether the *ring blocks anything*, since nothing reads `DS:D6`
except the force-field damage path (`overworld.c:856`). `state.h` still has only one general
story byte (`story_flag`, DS:0x239), but the quest chain turned out not to need one.

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
    O, "100", CR     # only works in sector 32 -- see below
    read state; re-evaluate
```

**This one has since been decoded from the bytes, and it is no longer a recalled claim.**
`asm/3C2E_cmd_O.asm` at `L_003f7e` is the generic bit-7 offer, and it does one RNG draw that
decides everything: `r = rng & 7`, the raise fires when `r < 6`, and the stat raised is
`DS:4B + r`. So:

| claim | bytes say | grade |
| --- | --- | --- |
| 75% raise / 25% "thank you" | **exactly right** — 6 of 8 vs 2 of 8 | **measured** |
| +4 per 100 gold | **exactly right** — four `adc 0x27 / daa` BCD adds, so the gain is 4 × (hundreds offered) | **measured** |
| a random attribute | **yes**, and uniformly: the same `r` indexes the six stats | **measured** |
| no ceiling guard, so stats wrap | **confirmed** — the result writes straight back to `DS:4B+r` with no compare | **measured** |
| stop at 95 | **now derived, not advised**: 95 + 4 = 99 is the last safe raise, and 96 + 4 wraps | **measured** |
| "chance is based on your turn count" | **wrong for this binary** — there is no turn term anywhere in the path. The first walkthrough said this; the bytes disagree, and the bytes win | **refuted** |
| location is the Hotel in New San Antonio, 1990 A.D. | the whole special-offer path is gated on `DS:4A==2 && DS:49==3`, i.e. **sector 32 and nowhere else** | **measured** (sector), **inferred** (that it is the Hotel) |

The 155 stop for the Int+Cha sum is the same arithmetic one level up: it leaves room for one
+4 without crossing the 160 cliff the walkthrough describes.

Two things this does **not** settle. That the bit-7 NPC in sector 32 is the Hotel clerk is
third-party-corroborated inference — the listing cannot name an NPC. And the save-and-reload
tip is a strategy the game neither supports nor blocks: death is memory-only
(`docs/handoff-2026-11-03.md`), and whether a mid-run manual save is worth the turns is a
policy question, not a byte one.

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
exactly what the API's `message` field exists for (§ 7).

**Checked, and it fails.** `AMUSING`, `Amusing`, `NO ANSWER`, `No answer`, `NO REPLY`,
`WILL NOT TALK` and `INVULN` do not occur anywhere in `original/ultimaii.exe` — searched over
the whole file, case-insensitively, on 2026-11-08. So this particular tell is **not available
in the DOS binary**, and a policy written to it would never fire. What survives is the
underlying claim, which is separately plausible and separately unverified: that some guards
cannot be killed. The mechanism for detecting that in this port is unknown, and the likely
answer is that the invulnerability is a creature-type or HP property rather than a dialogue
response. Grade: **refuted for the tell, recalled for the phenomenon.**

**Closed 2026-11-16, on the phenomenon rather than the tell.** The mechanism turned out to be
structural and already decoded: `asm/23E6` scans slots 31 down to 1 and never examines slot 0,
so a creature parked there is unhittable by melee, talk and cannon while still drawing, still
blocking the cell and still running its AI (G78). What was missing was a way for a *policy* to
see that, so the observation now carries `targets` — four values in N,E,S,W order, 1 attackable,
0 empty, **-1 present but not targetable** — computed by the engine's own predicate and not by a
second copy of the slot scan in the bot. `bot/policy.py` is the policy layer this section was the
first rule of, and the rule generalises past guards: an attack the engine says cannot land is not
retried, whatever caused that. Grade: **measured**, with the tell still refuted and unused.

---

## 4. What the walkthrough does NOT provide

Stated plainly, because the failure mode of a walkthrough-as-spec is assuming the prose is
complete. It is not, in four specific ways.

1. **No spatial or map knowledge at all.** Every location is a name — New San Antonio, Pirate
   Harbour, Castle Barataria, "the red square", "the northeast corner" — and none is a
   coordinate. **This gap has since closed from an unexpected direction, and the closure is
   `docs/place-index.md`:** the game spells its place names as alphabet tiles inside the
   sector maps, so `mapx22` contains `TOWNE LINDA` and `mapx93` contains `CASTLE BARATARIA`
   as map data. `tools/place_index.py` decodes all 33 sectors, and the result is a
   byte-derived name→sector index — which is the deliverable § 7.3 called impossible to base
   on the walkthrough. What the walkthrough still does not provide is *routes between* places;
   it no longer owns the names. One correction to the earlier reading on this page: the
   community names are mostly the game's own text, not third-party invention, and the one that
   is genuinely third-party (sector 32 = New San Antonio) is supported by `tlkx32`'s Hotel
   California line rather than by the map.
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
5. **It does not know that place identity is not the sector number.** `mapx23` and `mapx33`
   have identical decoded interiors — Lord British's castle in 1423 B.C. and again in 1990
   A.D. — so two sectors are one place, while `PORT BONIFICE` exists in exactly one era. A
   bot that keys reachability on sector rather than on (name, kind) will get this wrong
   silently. `docs/place-index.md` § 3a.

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
| Force field ring price: **500 GP** in the items table, **900 gold** in the walkthrough prose | The source contradicts itself, and **the bytes have now voted**: `asm/3C2E_cmd_O.asm` gates the ring grant on `cmp $0x5` against the hundreds offered, so the threshold is **500**. The prose's 900 is wrong, or is what someone paid after a failed attempt. The price *charged* is still not decoded — only the gate is. |

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
third-party claim about a different port that our own bytes independently explain.

**A third thing falls out, and the walkthrough never states it.** Power armour needs strength
31 and the quicksword needs agility 49, and creation gives 90 points with a floor of 10 per
stat. Even taking the male +5 and fighter +15 strength bonuses, reaching both thresholds
costs 16 + 49 + 4×10 = 105 points. **So the endgame's own equipment requirements make the
Hotel California grind mandatory rather than optional** — a character cannot be created ready,
and P2 sits on the critical path to W2 and W3 instead of being a way to get there faster.
Checked against the engine: the largest legal vector that still shows the bonuses landing is
20/20/10/10/10/20, which is exactly the budget, and AP11 pins it.

None of the above generalises downward to the rest of the page: the unchecked rows stay
unchecked, and the discount percentage, the price bands, the 9900 HP and the whole Shadowguard
interior are still third-party assertions.

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
| 2 | `bot/navigation.py` | era/sector movement, time doors, place→sector | § 4.1, `docs/moongates.md` | **partly now** — the place→sector index exists (`docs/place-index.md`, `tools/place_index.py`); what is missing is routing between sectors and the rotating time-door table applied |
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

Ranked by what blocks the most layers. **Items 1, 2 and 3 were built on 2026-11-08** and are
marked as such; 4, 5 and 6 remain open. A fourth thing was found and fixed while building
them, and it is recorded first because it blocked everything else.

0. **`create` did not enter the world.** *(built)* The first API build wrote a character and
   left the title flow running, so a caller that created a character and then sent game keys
   was driving the menu. The handoff lives in `title.c`'s `case 'C'`, which the API never
   passes through. Fixed by setting `enter_world` — the engine's own request-to-play flag —
   and teaching `flow_stopped()` and `wait_choice` to see it. Without this, items 1–3 would
   have produced a bot that could read its inventory while standing at a splash screen.

1. **Inventory, weapons and armour in the observation.** *(built — `char.weapons`,
   `char.items`, `char.armour`, `char.wield`, `char.worn`; gated by AP8 and AP11)* `items_held[16]`, `weapons_owned[10]`,
   `armour_owned[6]`, `readied_weapon`, `worn_armour` are all on the state block and **none is
   in `u2_state_json`**. Without them the bot cannot evaluate a single predicate from § 2, and
   W1–W3 of the win condition are unreadable. This is the blocking item. Extend the `char`
   block the API already emits rather than growing `u2_state_json`, for the reason recorded in
   `docs/play-api.md` deviation 4.
2. **A defeat signal.** *(built — `char.dead`, gated by AP10 in both directions)* Something that says "this character is dead and this run is over".
   Today a caller would infer it from HP, and HP is packed decimal, so an inference bug looks
   like a bot playing a corpse.
3. **HP as a decoded number, not two packed bytes.** *(built — `char.hp`, and `food`, `exp`
   and `gold` likewise, all read through `u2_state_bcd_pair`)* The bot should not have to know about
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
| Q1 | ~~Which byte does Father Antos' blessing write, and does the old man's offer read it?~~ **CLOSED 2026-11-08, statically: `DS:6C`, written by `asm/4209` and read by `cmd_O`'s ring arm.** The remaining sliver of G61 — whether the ring gates Minax herself — is still open and still needs the endgame route | only the "does the ring matter to the fight" question | owner-play, and only for that sliver |
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

# CP8 gaps found by the owner's Minax fight (2026-11-12)

The owner won the game in the original and compared it against the engine. Three gaps, all
observed by a person rather than inferred, and the first two share one cause.

## 1 and 2. Minax neither strikes from range nor teleports, because role 0 is skipped

`engine/src/game/ai.c`, inside the gated (town/castle) sweep:

```c
const int gated_sweep = (st->sector_second >= 1 && st->sector_second <= 3);
int casts = 1, retreat = 1, moves = 1, wander = 0, contact = 1;
if (gated_sweep) {
    const uint8_t role = st->mon_role[slot];
    if (role == 0 || (role & 0x80)) continue;      /* <-- here */
    casts = (role == 1);
```

Minax is `mon_role == 0` in a castle sector, so the engine `continue`s past her and she never
moves, never rolls, and never strikes. The comment a few lines above already admits the hole:

```
role == 0  -> asm/1040, the Minax proximity check, and thence next
              slot for everything that is not Minax. ... The Minax arm
              is still not modelled.
```

What the original actually does with role 0 is route it into `asm/1040_minax_proximity_check`:
test `mon_type == 0x40`, then `abs(mon_x - player_x) < 4` and `abs(mon_y - player_y) < 4` via
`asm/50EF`, then call `asm/0F8E_combat_minax_attack`, which prints `MINAX CRIES: DIE FOOL!`
and does a packed-decimal subtract of one on the **high** byte of the HP pair — exactly 100 HP,
with the borrow running the death tail. `asm/1040` is reached from `asm/218b`, the **no-move
tail**: it fires when all three candidate steps were refused or the accepted cell is the
player's. That is why she can strike from three cells away without moving.

**Observed in the original, first time ever:** the owner reported "she cries die fool and range
attacks me, when I'm about 3 steps away from her", indoors. That is `asm/1040`'s own `< 4`
window and its `[DS:4A] != 0` gate, seen executing. Headers in `asm/1040` and `asm/0F8E` said
those strings were discovered-but-not-executed; both now record the execution.

**Also observed, not yet decoded: she teleports between the north-east and south-west chambers
on every hit.** The walkthrough's claim, graded recalled in `docs/goal-spec.md` § 1, is now
observed — including which two corners. No address is known. The frame series in
`captures/spice86/minax-fight-live/` (1104 frames at 1/s) is the only evidence, and the trace
window in the noVNC container cannot hold a one-shot event, so the teleport has to be found
from the listing or from a memory diff, not from a harvest.

**Why the paralysis and sleep never appeared in the engine.** Same line. In a castle,
`casts = (role == 1)`, so only role-1 creatures roll for a special at all, and Minax — role 0 —
never reaches the roll. The owner was hit repeatedly by arms paralysis and sleep in the original
and saw neither in the engine.

### What to build, in order

1. **`asm/1040` + `asm/0F8E`.** In the gated sweep, replace the bare `continue` for `role == 0`
   with: if `mon_type == 0x40`, run the proximity test and the Minax strike, then go to the next
   slot; otherwise continue as now. The strike is `MINAX CRIES: DIE FOOL!` then a packed-decimal
   minus-one on `DS:51`, death on borrow. Gate on the **observable**: a player three cells away
   with known HP must come out 100 lower, and a player five cells away must be untouched. The
   five-cell arm is the control that makes the range window real rather than decorative.
2. **The teleport.** Needs a decode first. Start from the chamber geometry in the frames and
   from `asm/2CB5`'s neighbourhood; do not guess a destination.
3. **Re-check the dungeon sweep** (`engine/src/game/dungeon.c`), which has its own type table and
   its own sleep effect (`type 7 ... paral_sleep := rand & 7`) and may have the same skip.

## 3. The engine's ending is not the ending

The engine, on the kill, set `player_stamp = 0x28` and the next frame showed a rocket standing
in the player's cell in `CHAMBER ONE`. The owner: "i don't change, but the map slow gets fades
out then i end up in a rocket flying and I get the 'you have saved the universe'."

So the original's sequence is: print `MINAX IS DEAD!!` / `ALL HER WORKS SHALL DIE!`, then a
**progressive** fade (the 64x64 fill of `0x74` calls the map render and a delay once per outer
iteration — 64 renders, which is why it crawls), then `DS:13 = 0x28` which is the rocket *mode*,
then the flight loop, then `YOU HAVE SAVED THE UNIVERSE, AND COMPLETED ULTIMA ][!`. The player
sprite never becomes a rocket; the mode byte changes and the view changes with it.

`engine/src/game/victory.c` implements the state writes and the text but not the fade or the
flight, and the engine's renderer drew the mode byte as if it were the avatar. Both need
fixing, and the fade is the part that makes it read as an ending rather than a teleport.

## Not a gap: any weapon will do

The phaser took Minax from 199 to 33 and then killed her. `docs/goal-spec.md` W2's
"quicksword → harm Minax" edge is refuted **as a requirement**; the third-party hedge "in some
ports of Ultima II, any weapon will do" is the branch this binary is on. Weaker weapons simply
do less damage and take longer. Recorded here so nobody re-adds the requirement.

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

1. **`asm/1040` + `asm/0F8E`.** **SHIPPED 2026-11-12, then CORRECTED the same day against
   the shipped data.** In the gated sweep the bare `continue` for `role == 0` now runs the
   Minax arm: if `mon_type == 0x40`, the reach test
   (`abs(dx) < 4 && abs(dy) < 4`) and the strike fire, then the slot is done; every other
   role-0 creature still continues inert. **The correction, found by proving it against
   `monx03`:** the handoff said Minax is `role == 0`, but `monx03` slot 31 (type 0x40) is
   `role 0x84`. asm/102B's `je` (role==0) and asm/1033's `js` (any bit-7 role) BOTH route to
   asm/1040, and Minax reaches it through the **bit-7** branch. So the strike must be gated on
   `role == 0 || (role & 0x80)`, not `role == 0` alone — the first cut of this fix gated on
   `role == 0` and so still skipped her. The scripted townsfolk at 0x81..0x83 share the bit-7
   route and stay inert because asm/1040 rejects their type. The strike is
   `MINAX CRIES: DIE FOOL!` (added to the extractor as `U2_S_MINAX_CRIES`, asm/0F8E file
   0x1192) then a packed-decimal minus-one on `DS:51`, death on borrow, and a kill aborts the
   sweep the way asm/0F8E's `jmp` to asm/0C62 does. Gated in `ai_check` (9 new checks): a
   player three cells away comes out exactly 100 lower at role 0x84 AND at role 0 (both entry
   branches); the four-cell and five-cell arms are the controls that make the `< 4` window
   real rather than decorative; the bit-7-type-0x30 control proves the strike is gated on
   `mon_type == 0x40` and not on the role; the no-roll check separates it from asm/0AAA's
   special; and the high-byte-zero check proves the death-on-borrow. Reverting to the
   handoff's `role == 0`-only premise turns 8 checks red.
2. **The teleport — NOT FOUND in the decoded binary (2026-11-12).** The handoff said
   "start from `asm/2CB5`'s neighbourhood," but `asm/2CB5` is the **kill/victory** gate, not
   a hit path, and there is no teleport on the hit path either. Reading the three places a
   teleport could live:
   - **The player's attack, survive branch** (`asm/2E3D` file 0x30CA): `mov %al,0x177(%di)`
     (store reduced HP) then `jmp L_000c33` (return to the command loop). No type check, no
     move, no teleport.
   - **Minax's own AI path** (`asm/1040` → `asm/1048` → `asm/0F8E`): the reach test and the
     strike, then fall to the next slot (`L_00126d`). It never writes `mon_x`/`mon_y`, so she
     does not move. She is role 0x84, which routes to `asm/1040` and back — she is stationary
     at her spawn cell (60,3 in `monx03`).
   - **Every writer of `mon_x`(0x137)/`mon_y`(0x157)** in the tree (`asm/0CC0`, `asm/0CDC`,
     `asm/212E`, `asm/215C`, `asm/216E`, `asm/2216`, `asm/2233`, `asm/4F11`, `asm/10CE`,
     `asm/10FC`): all add a ±1 step vector and wrap with `and $0x3f`. None writes a fixed
     chamber coordinate or a large delta, which is what a teleport would have to do.
   So the "teleports between the NE and SW chambers on every hit" claim is **not supported by
   the bytes examined**. The frames are 1/s and mix fight, the Z status screen and the ending;
   they show the fight happening but cannot decode a mechanism, and the previous session's
   "observed" grade was a person seeing Minax in two chambers, not a traced position change.
   **What would settle it definitively:** a live capture — seed a character into sector 03,
   `spice86_type.py --peek` Minax's `mon_x`/`mon_y` (slot 31, `DS:137+31`/`DS:157+31`), attack
   her once, and re-peek. If the cell changed, a teleport exists and the diff names the writer;
   if it did not, the claim is refuted outright. That is agent-drivable and is the honest next
   step; guessing a destination from chamber geometry is what the handoff warned against and
   what the bytes do not support.
3. **Re-check the dungeon sweep** (`engine/src/game/dungeon.c`), which has its own type table and
   its own sleep effect (`type 7 ... paral_sleep := rand & 7`) and may have the same skip.
   **Checked 2026-11-12: it does not.** The dungeon sweep never reads `mon_role`, so there is
   no role-0 skip; its special dispatch is on the dungeon type table (cases 2/5/7), and Minax
   is a castle creature that never appears in a dungeon. No change needed there.

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

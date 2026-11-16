# Plane flight, observed by the owner: there is no airborne byte

**Result: the bot cannot tell it is flying from memory.** Not "has trouble telling" — there is
no byte that distinguishes airborne from mounted-on-the-ground. The only signal is motion.

The owner flew the original in the noVNC container while a sampler logged
`x, y, mode, DS:21, DS:22, sector, world, hp` at ~5 Hz. Reported actions, in order: grabbed a
plane, `B` to board, `L` to launch, flew north, hit the town edge and was ejected to the
overworld, had to press `L` again to launch, flew a bit, landed on grass.

## What the log shows

| observation | value |
| --- | --- |
| boarding | `DS:13` `0x78` (foot) → `0x26` (plane) at 18:29:37 |
| town exit while airborne | sector `41` → `40` at 18:29:43, `DS:13` still `0x26` |
| **after landing, standing still** | `DS:13` **still `0x26`**, 0 position changes in the final 20 s |
| heading pair after landing | `DS:21/22` = `(134, 207)` — **identical to the value during flight** |
| free-running | three windows with move-rate 0.90–1.00, i.e. the position changed at *every* sample |

**`DS:13` is a mount byte, not a flight flag.** It says "riding a plane" and does not change when
the plane leaves the ground or lands. It stayed `0x26` for three and a half minutes after
landing while the character was stationary.

**The heading pair is not a usable discriminator either**, which is the part that refutes the
static reading. `asm/38B0` initialises `DS:21 = 0`, `DS:22 = 0xFF` on launch and arrows
overwrite it, which looks like a direction vector. Across the flight it took **84 distinct
values**, with `DS:21` spanning the full `0x00..0xFF` and `DS:22` clustering in `192..255`. That
is not a heading. It also never returns to a ground value — the last sample, standing on grass,
reads the same `(134, 207)` as mid-flight. So the earlier note that `DS:21/22` is "the candidate
discriminator with no off edge" was too generous: it has no off edge *and* it is not cleanly a
heading field. Worth remembering that `asm/0BDB_apply_hp_damage` also writes `0x22`.

**Free-running is confirmed, not inferred.** The owner's answer to "does it wait for you or run
away" was the third window: at 5 Hz sampling the position changed at every single sample. The
plane outruns the sampler, so the true rate is above 5 cells/s and unknown from this data.

**A new behavioural fact: the flight loop ends at a sector change and does not restart.** The
town exit moved the character to sector 40 with the mode still `0x26`, and a second `L` was
required. So `asm/38B0`'s loop is per-map, not per-flight-session — consistent with the header's
"crossing `x==0` or `y==0` while `[DS:4A]` is non-zero leaves the site through the sector reload
at `asm/243E`", and it means a bot cannot treat launch as a one-time setup.

## What a bot can actually use

Not a byte — a behavioural test: **sample position twice; if it advanced and no key was sent,
the plane is airborne.** That is the only honest discriminator this capture leaves. It is weaker
than a flag (it needs two samples and a quiet input window) but it is the only one that exists.

## The bot can fly it — `bot_fly.py`, same day

The absence of an airborne byte turned out not to block a bot, because motion is enough. The
logged run, headless against the original:

```
loaded: pos (3, 10) mode 0x78  (want pad (3, 10))
boarded: mode 0x26
-- LAUNCH --
  L #1: no motion yet
  airborne after 2 press(es) of L: (3, 57) -> (3, 52)
-- rate: 2 s of no input --
  (3, 52) -> (3, 27)  = 25 cells in ~2 s (~12 cells/s)
-- STEER: press Right --
  path: 3,27 14,26 17,26 20,26 23,26 26,26 29,26 32,26 35,26
-- LAND: press L until motion stops --
  try 1..3: still moving
  LANDED after 4 press(es) at (44, 26), mode 0x26
VERDICT: bot launched, steered, and landed the plane
```

Steering worked and the script's own check for it was the thing that was wrong: the plane was
travelling north (`y` 57→52→27), and after `Right` the path ran `x` 3→35 with `y` fixed at 26.
That *is* the heading change. The check compared only the last three intervals, saw `dx=9, dy=0`
and printed "heading changed: False" — a verdict computed from a window too short to contain the
transition it was testing for.

Three things this does not mean:

* **The bot never knew it was flying.** It inferred airborne from motion-without-input, which is
  the only discriminator that exists. It works, and it is inference rather than observation.
* **Landing is blind.** `L` was pressed until motion stopped, so the plane came down on whatever
  grass it happened to cross, not a chosen target. Aiming a landing needs the bot to predict
  where a free-running plane will be, at ~12 cells/s, from samples that are already stale.
* **The plane badly outruns the sampler.** Consecutive samples are `(3,57)`, `(3,52)`, `(52,26)`,
  `(12,26)` — the bot is steering semi-blind, and the 12 cells/s figure is a floor set by the
  sampling gap, not a measurement.

Seeding note, since it cost two runs: for a **site** sector the game restores `entry_x/entry_y`
as the arrival position and `setpos` does not survive the load. That is why the owner appeared at
(31,62) instead of the `setpos` cell, and the bot has to put the *pad* in the entry pair to start
on it. `bot_fly.py` does this.


## Two things this capture does not settle
- **Whether creatures move while airborne.** The owner did not fly past a creature, so the
  callee-list inference in `asm/38B0` (flight loop calls only map render, a discarded speed
  lookup, and key poll) is still unconfirmed by observation.
- **The plane's march-step cost (G77).** The move-rate windows are sampler-limited, not
  game-limited, so they bound the rate from below at ~5 cells/s and say nothing precise.

## The seeding mistake, and what it actually taught

The save was built with `sector 32` (New San Antonio, per census) and `setpos 5 10`. The log
shows the character arriving at **sector 40, position (44,14)**, then entering **sector 41** at
(31,62). The reason is now known and is stated above: for a site sector the game restores
`entry_x/entry_y` as the arrival position, so `setpos` is ignored and the entry pair I set to
(31,62) is exactly where the character appeared. Sector 32 vs 41 is the census town versus the
airfield town the owner then walked into. Nothing is broken in `u2_edit.py`; the record has two
position fields and the site one wins.

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

## Two things this capture does not settle

- **Whether creatures move while airborne.** The owner did not fly past a creature, so the
  callee-list inference in `asm/38B0` (flight loop calls only map render, a discarded speed
  lookup, and key poll) is still unconfirmed by observation.
- **The plane's march-step cost (G77).** The move-rate windows are sampler-limited, not
  game-limited, so they bound the rate from below at ~5 cells/s and say nothing precise.

## A tooling caveat, recorded rather than hidden

The save was built with `sector 32` (New San Antonio, per census) and `setpos 5 10`. The log
shows the character arriving at **sector 40, position (44,14)**, then entering **sector 41** at
(31,62). So the requested sector did not take effect as intended. It worked out — sector 41 is
the airfield town with 14 plane pads, which is a better starting point than the one asked for —
but `u2_edit.py set sector` plus `setpos` did not produce the sector requested, and that is
worth a look before the next seeded session depends on it.

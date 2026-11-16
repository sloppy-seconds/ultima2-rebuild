# G43 sub-case: a grounded plane refuses forest, and the refusal is specific to forest

**Closed 2026-11-16.** This is the one sub-case of G43 that `asm/2604_mode_arm_plane.asm`
could not settle on its own. The header decoded the mechanism correctly but said so in its own
provenance line:

> whether the arm itself ran depends on the player walking while mounted, and no capture has
> recorded that. The forest reject is therefore inferred-from-bytes.

That is the gap this run fills. The bytes said the plane's grounded rule is the mounted rule
*plus* a forest rejection; this observes it.

## Result

| # | step | expected | observed |
| --- | --- | --- | --- |
| 1 | on foot, step east onto forest | accept | **moved** (10,10)->(11,10) |
| 2 | on foot, step west back onto the pad | accept | **moved** (11,10)->(10,10) |
| 3 | `B` to board | `DS:13` 0x78 -> 0x26 | **0x26** |
| 4 | grounded plane, step east onto forest | refuse | **refused** (10,10)->(10,10) |
| 5 | grounded plane, step west onto grass | accept | **moved** (10,10)->(9,10) |

Step 1 says the map is right and forest is walkable on foot. Step 3 says boarding worked and
the mode byte is the one `asm/253B` dispatches on. Step 4 is the claim. **Step 5 is what makes
step 4 evidence**: without it, "the plane refused to step onto forest" is indistinguishable
from "a grounded plane cannot move at all", and those are completely different findings.

## Method, and why the map is synthetic

`--seed-map` installed `mapx40.patched` here: `tools/u2_mkprobe.py`'s generated sector
(background `0x08` grass, 4033 of 4096 cells) with two bytes patched — a plane pad `0x4c` at
(10,10) and one forest tile `0x0c` at (11,10). Pad, forest and open grass as three mutually
adjacent cells is what the experiment needs, and **no real map offers it**:

- **sector 80**'s three pads each sit in a pocket ringed by mountain `0x10` and forest `0x0c`.
  Every neighbour of pad (9,40) is forest or mountain, so there is no cell a mounted plane can
  accept — no positive control is possible there.
- **sector 41**'s fourteen pads sit behind a `0x78` wall at y=14, with the woodland on the far
  side. Reachable apron, unreachable forest.

Two earlier attempts failed for reasons worth keeping, because both looked like results:

1. **Attempt 1** mounted correctly at sector 80 and the plane refused to step west onto
   forest. Correct-looking, and unusable: no positive control, exactly the defect step 5
   exists to close here.
2. **Attempt 2** tried to add a foot control step *before* boarding. The control step walked
   the player off the pad and onto forest, so the following `B` had nothing to board, the mode
   stayed `0x78`, and every "mounted" probe in that run was silently a foot probe. A control
   placed before the thing it controls can destroy the precondition. Here the foot control
   runs first but steps *back onto the pad*, and the mount sits between the two halves.

## What this does and does not prove

Proven: a grounded plane refuses forest and accepts grass, and the mode it does that in is
`0x26`, the value `asm/253B` routes to `asm/2604`. That is the behaviour the header inferred,
now observed.

Not proven, and not claimed: **which instruction refused.** Breakpoints at runtime `0x253B`
(the dispatch entry) and `0x2607` (the plane arm) never fired in either attempt, including on
steps that demonstrably succeeded — attempt 2's control moved the player and the dispatch
breakpoint still reported no hit. So `bp_read` at those addresses is not a reliable probe of
this path on this build, and that is a harness fact rather than a finding about the game. The
attribution to `asm/2604` rests on the decode plus the mode byte, not on a breakpoint hit.

Also unchanged: the airborne question was already answered statically. `asm/38B0`'s flight loop
consults no passability routine and charges no food, and landing is accepted on grass `0x08`
only. So the forest rule is grounded-only, which is what the owner's recollection said.

## Reproduce

`drive.py` in this directory is the driver. It regenerates the synthetic map (so no sector
blob is committed — one `u2_mkprobe.py` call plus two byte patches), builds the save, runs the
five steps against the original in headless Spice86, and prints the table. Position is read
from `DS:00/01` over the HTTP API, never inferred from the keypress having been sent.

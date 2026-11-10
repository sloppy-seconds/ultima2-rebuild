# Handoff — the space view (fixes 2 and 3)

**Status: fix 1 and fix 2 shipped; fix 3 shipped in part.** Fix 1 = `aec9d6c` (boarding draws
the vehicle). Fixes 2 and 3 = `8b57ba5` (`space_loop` presents instead of spinning; the orbit /
deep-space screen is drawn: star field, ship cross, readout values).

**What is still open, and what it needs:**

| item | state | needs |
| --- | --- | --- |
| hyperwarp **radiating** star field | **partly decoded and drawn** (`141d610`) | the view rectangle, the (16,16) origin, white-only, and the view-centre radial origin are all measured and gated. What is missing is the per-star algorithm, the warp star count and the animation rate — see `captures/spice86/g39-rocket-flight/README.md`, which names the exact capture that would settle them (a ~10 Hz screenshot + `DS:479/47A` poll across the warp window; the 4–5 s frames loop cannot, and no re-reading of it will) |
| **the gate is weaker than it looks** | `space_view_gate.py` asserts a present call, a paint call, the prompt count and the sprite geometry — and **all of them passed while the screen was visibly wrong** (text pane blanked, `83eda28`). None of them says anything about what the player can see | a real check needs a rendered frame: drive a scripted `B, L` and assert lit pixels exist in the text rows of the dump PPM. Blocked on the same missing thing as leg 3 — no scripted route to a rocket |
| warp field **trigger and duration** | undecoded | `scene_from()` sets `warping = 0` and says so. The radial branch is drawn from the measured centre but nothing currently selects it, because inventing a timing would be claiming a mechanism |
| readout **position** | drawn as text lines, not the original's fixed right-hand cells | `asm/6999` draws the digits graphically at fixed cells; those cells are not decoded. The values are right, the layout is approximate |
| arrow step size | `U2_SPACE_SHIP_STEP 4`, **UNMEASURED** | one live peek: `8be:47b` before and after one `Right` |
| § 5 leg 3 gate | not written | needs a scripted run that can *reach* space; the suite has no route to a rocket today |
| star field positions | generated inside the **measured** rectangle; the rectangle is fidelity, the points are not | the drawing routine is unidentified — see the g39 README re-read |

Everything below is the original scoping, kept as written.

## 0. Why 2 and 3 are the same job

`engine/src/game/commands.c:1092` `space_loop`:

```c
for (;;) {
    seam_str_out(ctx, U2_S_CMD_PROMPT);
    const int r = seam_key_poll(ctx, &k);
    if (r < 0) return;
    if (!r) continue;      /* hot spin, and re-prints CMD: every pass */
    ...
}
```

Three defects: unbounded busy spin (owner saw 60% CPU), a prompt re-printed every iteration,
and **no present call anywhere in the loop**. Fixing the spin without adding a frame to present
would just spin politely over a stale overworld image. So: build the view, then give the loop the
overworld's idle-and-present shape.

## 1. What the space screen actually is — measured, not guessed

All four frames are in `captures/spice86/g39-rocket-flight/`. Read them before writing code.

| frame | what it shows |
| --- | --- |
| `21-space.png` | **hyperwarp animation**: white dots radiating from a centre point on black, `HYPERWARP ENGAGED!` bottom-left, `FUEL=08 / XENO=06 / YAKO=06 / ZABO=06` bottom-right |
| `22-right.png` | **orbit view**: scattered static stars + a small ship marker (a 3–5 px cross) at the ship's screen position, text pane `HYPERWARP ENGAGED!` / `YOU ARE ORBITING EARTH.` / `CMD: RIGHT` / `CMD:`, same right-hand readout |
| `23-hyperspace.png` | **warp target entry**: `CMD: HYPERWARP TO:` / `XENO=9 YAKO=9 ZABO=9` / `PREPARE FOR HYPERWARP!` — note the typed target appears in the *text* pane while the right column keeps showing the current 06/06/06 |
| `24-arrive.png` | **deep space**: same field, `YOU ARE IN DEEP SPACE.`, readout `XENO=07 / YAKO=01 / ZABO=01` |

So the static view is: black field, scattered stars, one ship marker, four-line right-hand
readout, and the normal text pane below. The radiating field is the warp transition only.

## 2. The model is already done — do not rebuild it

`engine/src/game/space.h` + `space.c` already carry, each byte-cited:

- the ten-world coordinate table (`asm/6999`, file `0x7212`, located by pattern)
- `u2_space_world_at()` — orbit requires **equality** with a world's triple, no radius
- the sun test (`4/4/4` → `U HIT THE SUN!`, then falls through to deep space)
- hyperwarp: fuel spend, **off-course ⟺ `(fuel & 3) == 3`**, then coords `rand & 7`
- dive / landing sector rule: Earth → 40, world N → N0, ones digit forced to 0
- the armour gate at `DS:62 >= 5` (REFLECT or POWER), and it is **not a death**

`asm/6999_space_status` names the readout fields as **`DS:4CA/4CB/4CC`** — live scratch, outside
the save record, never persisted. Do not add them to `state.h` as saved fields.

## 3. State the loop must render
From `captures/spice86/g62-orbit-state/README.md`, observed in order:

| state | `DS:13` | `DS:6D` | sector | pos | xyz | fuel |
| --- | --- | --- | --- | --- | --- | --- |
| on pad, sector 41 | `0x78` | `00` | 41 | (38,5) | 0/0/0 | 09 |
| boarded | `0x28` | `00` | 41 | (38,3) | 0/0/0 | 09 |
| **in orbit** after launch | `0x28` | `00` | 41 | (38,3) | 6/6/6 | 08 |
| **landed**, sector 40 | `0x28` | `00` | 40 | (1,10) | 6/6/6 | 08 |
| after `L` while landed | `0x28` | `0a` | 40 | (1,10) | 7/1/1 | 07 |

Two things this settles:

- **launch goes to orbit; a second `L` dives.** Sector and position do not change on launch.
- `DS:6D` is the world byte and it **does** move (`00` Earth, `0A` deep space, `09` X).

## 3a. The orbit/landed discriminator IS CLOSED — 2026-11-10, `captures/spice86/w6-orbit-dive/`

**Do not call this W6.** The task that commissioned the capture used that label, but W6 already
means *the plane's airborne flag* in `docs/handoff-vehicle-state-machine.md` § 3, which is a
different question and is still open. The capture directory keeps the `w6-` prefix; prose does not.

Read that README; it carries the three-state table and the byte evidence. Summary:

**The owner's model is confirmed.** Launch changes only the coordinate triple (`DS:4CA/4CB/4CC`
`0/0/0 -> 6/6/6`) and the fuel byte (`DS:E5` `09 -> 08`). Position, sector, `DS:13`, `DS:14` and
`DS:12` are byte-identical between the pad and orbit. A second `L`, pressed while genuinely in
orbit, runs the dive: sector `41 -> 40`, position moves, `DS:12` `04 -> ff`.

**There is no in-space boolean, and that is the finding.** The only observable that separates
orbit from both neighbours is that **`DS:272..DS:371` and `DS:372..DS:471` are uniformly `0xFF`
while in space** -- all 512 bytes. On the pad they hold the village's staged map; after landing
they hold sector 40's. It is a consequence of having no map to stage, not a designed flag.

**So do not test the tables. Keep the loop.** `cmd_L` is only reachable from the overworld loop;
while in space a different loop owns the keyboard and `L` means dive there. "In space" is a
property of which loop is reading the keyboard. The engine already has this shape --
`launch_rocket` calls `space_loop` at `commands.c:1201` -- so the fix is to make `space_loop`
run and draw, not to invent a flag. Testing the staging tables would be reconstructing a control
-flow fact out of a rendering side effect, which is how plausible booleans get into this codebase.

Still unexplained and now measurably not the answer: `DS:21`/`DS:22` (`02->24`, `e2->e5` on launch,
**unchanged by the dive**, still `reserved_21[2]` in `state.h`).

## 4. Drawing primitives that already exist

`engine/src/core/frame.h`: `u2_frame_clear`, `u2_frame_put`, `u2_frame_put_logical`,
`u2_frame_xor_logical`, `u2_frame_pixel`, `u2_frame_line`, plus `u2_frame_draw_view{,_ega}`.
The star field is `u2_frame_put` on scattered coordinates; the ship marker is a few pixels.
`asm/6CC4_space_ship_erase` is the original's erase-before-redraw for the marker — read it before
deciding whether the marker XORs or overwrites.

The star positions: **not yet decoded.** Either find the table/routine in `asm/` around `6999`–
`6DFD`, or generate them from the engine RNG and record that as an enhancement, not as fidelity.
Do not present a generated field as the original's.

## 5. Gate plan

A gate must be able to fail where it enforces.

1. **No hot spin** — DONE in `8b57ba5`, `tests/space_view_gate.py` leg 1, wired into CP4, control
   seen to fail. Asserts a present call, no bare empty-poll `continue`, a paint call, and one
   prompt per command.
2. **Ship sprite geometry** — DONE, same gate leg 2: the sweep counts are read out of
   `asm/6CC4_space_ship_erase`'s own header rather than restated, so it checks the evidence and
   not the fix.
3. **Readout values** — PARTIAL. The values are derived correctly from `st->world` and `DS:E5`,
   and `space_readout()` prints them, but no gate drives a run far enough to assert it. Blocked on
   the same thing as leg 4 below.
4. **Orbit vs deep space / dive sector, end to end** — the pure rules are covered by
   `space_check.c` (37 checks), but nothing in the suite can reach space in a live run. Building
   that route is the next useful piece of work here.

## 6. Also still open from the same playtest

- **The orbit/landed discriminator** — closed 2026-11-10 by `captures/spice86/w6-orbit-dive/`;
  see § 3a. Nothing distinguishes orbit from landed except the staging tables being uniformly
  `0xFF`, and the recommendation is to keep the loop-scoped structure rather than test them.
  **The plane's airborne flag (the real W6) is still open.**
- The **status panel icon** still uses the hardcoded `U2_OVERWORLD_PLAYER_ART`. Deliberate, and
  `board_stamp.py` asserts the constant stays referenced so the question remains visible. Nobody
  has evidence for what the panel draws while mounted.
- `up.sh` should pass `--debug-limit 0` for interactive containers, or the turn recorder should
  keep a bounded window instead of going dark at 200 turns. It went dark during exactly the
  playtest that needed it, and the only signal was one line in a container log.

# Handoff — the space view (fixes 2 and 3)

**Status: not started.** Fix 1 shipped in `aec9d6c`. This is the remaining work, written down
because it is one job and not two.

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

Two things this settles and one it does not:

- **launch goes to orbit; a second `L` dives.** Sector and position do not change on launch.
- `DS:6D` is the world byte and it **does** move (`00` Earth, `0A` deep space, `09` X).
- **Not settled: what distinguishes orbit from landed.** Both read `DS:13 = 0x28`, `DS:6D = 00`,
  xyz `6/6/6`. The handoff on the vehicle state machine lists this as **W6** and says it needs a
  capture, not a guess. Check `g62`'s three PNGs and `flight-launched-live` before inventing a
  flag — a plausible boolean standing in for an unproven one is the failure this project has
  named repeatedly.

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

1. **No hot spin**: assert `space_loop` contains a present call and no bare `continue` on the
   empty-key path. Control: remove the present, watch it go red.
2. **Readout**: drive a launch in a scripted run and assert the frame's right column reads
   `FUEL=` and the world's triple — `6/6/6` for Earth orbit, `9/9/9` for X. Compare against the
   capture PNGs, not against your own expectation.
3. **Orbit vs deep space**: `9/9/9` → `YOU ARE ORBITING X.`; an off-course roll → `YOU ARE IN DEEP
   SPACE.` with coords in 0..7. `space.h` already takes the RNG as an argument precisely so this
   can be forced.
4. **The landing sector rule**: dive from world N lands in sector N0, ones digit 0.

## 6. Also still open from the same playtest

- **W6** (above) — the orbit/landed discriminator.
- The **status panel icon** still uses the hardcoded `U2_OVERWORLD_PLAYER_ART`. Deliberate, and
  `board_stamp.py` asserts the constant stays referenced so the question remains visible. Nobody
  has evidence for what the panel draws while mounted.
- `up.sh` should pass `--debug-limit 0` for interactive containers, or the turn recorder should
  keep a bounded window instead of going dark at 200 turns. It went dark during exactly the
  playtest that needed it, and the only signal was one line in a container log.

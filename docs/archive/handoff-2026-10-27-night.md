# Handover — 2026-10-27 evening: work an agent can do alone

> **SUPERSEDED 2026-10-28 — read `docs/archive/handoff-2026-10-28.md` instead.** This file is kept as
> the record of what was asked and what was found, not as the entry point. Of the queue below:
> items 1, 6, 7 and 8 are done (7 twice — the followup refuted the asymmetry this file left
> open), item 5 (G30) is closed statically and item 4 is wired on the strength of it, item 3
> is spec'd in `docs/status-screen-z.md` but not built, and item 2 is still blocked, now on
> the selector rather than the caller. The "one thing to do first" below — capture the return
> address — turned out to be impossible in principle, not just awkward: the spawner is reached
> by fall-through and has no caller frame. See `docs/monster-spawn.md` § 15.2.

Entry point for a fresh session. Paul is asleep. Everything below is **agent-drivable**:
no gameplay, no opinion, no password he has to type. Ordered by what each item unblocks.

> **Update, 2026-10-28 second pass — items 1, 6, 7 and 8 are done; 2, 4 and 5 are not.**
> Item 1: see `docs/monster-spawn.md` § 15.2 — there is no return address, the spawner is
> the tail of the per-command creature sweep, `DS:4A` is the sector's low decimal digit and
> `DS:60` is a rotating selector, not a counter. Item 2 stays blocked, now on the *selector*
> rather than the caller; § 15.2 names the measurement. Item 3 got a full spec
> (`docs/status-screen-z.md`) and no implementation: the handoff's handler guess was wrong
> (it is `asm/4568_cmd_Z`, found via the A–Z table), the name tables are located but are not
> in `gen_content.h` yet, and `Z` was measured to cost no food — but whether the post-command
> sweep runs after `Z` is unmeasured and it needs live monsters. Item 4 is still blocked on
> item 5. Item 5 (G30) was attempted three times and refused every time; see
> `docs/needs-gameplay.md` G30 — read `asm/4326`'s aim computation before another capture.
> Item 6: `DS:12` is a world-map/interior mode byte and `L_005cb1` is a viewport origin that
> wraps only on the world map (§ 15.2). Item 7: **refuted in the other direction** — the
> sector *does* change on foot, but only out of a site; `site_leave()` was firing on the
> world map too and is now gated, with three new `site_check` checks, mutation-tested.
> Item 8: both fixed (`--debug-limit`, `tools/vnc/up.sh` port refusal), both verified live.
> Proof and suite green throughout: 37344 bytes identical, 93 PASS / 0 FAIL.
>
> One thing that bit and is not resolved: running `u2-sdl --play-map --data-dir original/`
> from the project reports `sector 20: no such sector file` even though `original/mapx20`
> exists and is readable, and a copied content directory loads `sector 00` instead. The
> container path works, so this is most likely my invocation rather than an engine defect —
> but it cost enough time to be worth a line in the engine README from someone who knows
> what `--play-map` actually expects.

> **Update, 2026-10-28 (next session, read this before the queue below).**
> **Item 1 is closed, and the answer is not what it asked for.** There is no return
> address to capture: at a stop inside the spawner the guest stack is empty (SS:SP exactly
> at its initial value, `peekRet` answers `017D:0000`), because the spawner is reached by
> fall-through, not by a call. The trigger is now static and named — every completed
> command falls through `cmd_done` to `asm/0A38_post_command_tail`, which dispatches on the
> sector's low decimal digit (`DS:4A`) into the creature sweep, and the sweep's last act is
> `asm/0AA4_monster_spawn_entry`. Full write-up, measurements and the four emulator hazards
> that cost the time: **`docs/monster-spawn.md` § 15.2**. `tools/bp_read.py` grew
> `--state --ret --ret-type --stack --callstack` to get there.
> **Item 2 stays blocked**, on a different unknown than the handoff assumed: the cadence is
> proven, the *selector* is not (the `DS:60` bit chain and the `DS:4A` 1..5 arms). § 15.2
> names the paired-sample measurement that closes it. Do not implement a spawn rule yet.
> **Item 5 (G30) was attempted and is still open** — three unlock attempts, all refused, and
> the aim computation in `asm/4326` is the next thing to read. Details in
> `docs/needs-gameplay.md` G30. **Item 4 is therefore still blocked on 5.**
> Item 3 (Z stats) was not started. Items 6-8 not started.

Read first, in this order: `AGENTS.md` (rules, isolation, evidence grades), this file,
then `docs/monster-spawn.md` § 15 / § 15.1 if you are doing item 1.

## State of the world, verified not remembered

```
main            9972c17, pushed, tree clean
CI              both workflows green (asm-checksum, engine-tests)
make -C engine test   CP0..CP5 green
bash tools/assemble_clean.sh   37344 bytes identical
engine container      GONE — Paul killed it. Rebuild: see "Containers" below
spice86 container     Exited (137). Restart: ./deploy/spice86-novnc/up.sh
fmt container         Up, holds host port 6080 (another project; do not stop it)
```

`/var/tmp/u2/novnc-saves/PLAYER` is the test character `G23MAX`: sector 20 at (20,20),
HP/gold/EXP 9999, **20 keys**, all stats 40. `/var/tmp/u2/novnc-saves/tiles.u2tl` is the
tile cache it needs. Both live outside git like every other save.

## The one thing to do first

### 1. Name the wandering-spawn trigger (extends `tools/bp_read.py`)

**Why it is first:** without monsters there is no combat, CP5 cannot be tested by playing,
and the starting map is empty. Everything else on this list is polish next to that.

**What is already proven** — do not re-derive it, spot-check one byte and trust the rest:

- `monx20[64:96]` is all zeros, so sector 20 has **no placed monsters**; everything there
  is a wandering spawn. `u2_monsters_spawn()` only `memcpy`s placed monsters out of
  `monxNN`, so the engine has no wandering path at all.
- The per-kind arms dispatch on **`[DI+0x1B7]`**: `asm/0D44_sub_0d44.asm` is
  `mov 0x1b7(%di),%al; cmp $0x8,%al; je <orc arm>`. Kind 8 = Orc.
- Dispatch above the arms is a **binary search** of `jae`/`jmp` stubs at `0D06..0D43`.
- Every arm ends in `asm/0BAF_monster_hp_epilogue.asm` = `call L_005cb1`, behaviour
  unknown, called from 6+ other sites so it is shared machinery.
- **The dispatch is reached by ordinary walking.** Breakpoint at runtime `0x0D44` in a
  headless sector-20 session: 13 steps = no hit; ~100 steps = `STOP at 017D:0D44` with
  type slots holding `34 30 FC 30` (Daemon/Orc/Thief) and HP `40 10 20 10`.

**What is missing:** the return address at that stop. It names the caller, which tells you
whether the trigger is per-step, per-N-steps, on a roll, or on the status tick.

**Recipe:**

```sh
# one emulator, default ports. The spice86 container squats 20180/20181 — stop it first.
sudo podman stop u2-spice86
nohup bash tools/spice86_start.sh --headless --patch-arrows \
     --player /var/tmp/u2/novnc-saves/PLAYER --auto-play --name spawncaller > /var/tmp/u2/r.log 2>&1 &
sleep 35 && python3 tools/spice86_type.py --probe      # must show G23MAX, CS:017D
```

Then arm and walk. **Do not run `--selftest` on a run you care about** (hazards below).

```sh
nohup timeout 160 python3 tools/bp_read.py --ip 0D44 --cs 17D --dump 137:160 --timeout 150 \
      > /var/tmp/u2/bp.log 2>&1 &
sleep 6
for i in $(seq 1 25); do python3 tools/spice86_type.py --keys Right,Down,Left,Up >/dev/null 2>&1; done
```

**The actual task:** `peekRet` and `callStack` are **GDB monitor commands on the RSP port**
(20190), not HTTP API paths. `curl http://127.0.0.1:20180/api/peekRet` returns an **empty
200** — not an error, and it reads exactly like "no data". Extend `bp_read.py` to open the
RSP port and send `vCmdstring:monitor peekRet;` (or the `qSupported`-negotiated equivalent)
while the target is stopped, and print the result. **Extend that tool; do not write a
second one** — `AGENTS.md` forbids duplicating a probe, and the recurrence of wrong answers
here has almost always been a hand-rolled second version of something that already worked.

**Acceptance:** a named asm file for the trigger (rename per the `asm/` rules — filename and
`#` header only, labels are body), a § 15.2 in `docs/monster-spawn.md` citing the return
address, and `bash tools/assemble_clean.sh` still printing `37344 bytes identical`.

### 2. Implement the wandering spawn (blocked on 1)

Once the caller is known, the HP values are already proven by the named arms and matched
live. Transcribe, cite each arm in the comment, and add checks to `engine/tests/ai_check.c`
or a new `spawn_check.c`. Gate it behind a real cadence, not a guess. If item 1 does not
resolve, **stop and leave the engine with no spawn logic** — a plausible wrong spawn rule
is worse than an empty field.

### 3. Z stats screen (`Z`) — self-contained, zero unknowns

Not wired: there is no `case 'Z'` anywhere in the engine. `status.c` only draws the
persistent `H.P./FOOD/EXP./GOLD` block. `asm/462F_status_keys.asm` is the likely handler.
We have a **screenshot of the real screen** taken this session (owner pressed `Z` on
`G23MAX`): name line, `A LEVEL 00 FEMALE HUMAN FIGHTER`, WEAPON/ARMOUR/KEYS/TOOLS, the
six stats, WEAPONS/ARMOUR/ITEMS inventory lists, SPELLS. That image is the spec — pull it
from the session or re-take it from the spice86 container, which is agent-drivable.

Acceptance: a `status_check.c` addition asserting the rendered page against the screenshot,
plus `Z` pausing and resuming without consuming a turn (verify against the original: does
`Z` cost food? measure, do not assume).

### 4. `U` = Unlock — the rule is fully proven, only the wiring is missing

Bytes, all of it:

| asm | fact |
| --- | --- |
| `42F1_cmd_U` | prints `UNLOCK DIRECTION-` |
| `4326` | computes the target cell from the aim pair, fetches its tile |
| `433A` | `cmp $0xa0,%al; jne open_no_door` — **door = map byte `0xA0`** |
| `433E` | `mov 0x65,%al; or %al,%al; jne` — needs `DS:65` keys, else `NO KEYS THAT FIT!` |
| `435E` | `stc; sbb $0x1; das; cmc; mov %al,0x65` — **consumes one key, BCD** |
| `435E` | `mov 0x14,%al; add %al,%al; ... mov %al,(%bx)` — overwrites the map cell |

Do **G30 first** (below) so you know what byte to write instead of inventing one. The
engine already has `u2_door_can_open()`-shaped knowledge in a comment block in
`overworld.c` and doors are currently just impassable, which is the honest half.

### 5. G30 — what tile is under an unlocked door (capture, ~20 min)

Owner does not remember. `asm/435E` writes `DS:14 * 2` into the cell; what `DS:14` holds
at that moment is untraced. Recipe is written in `docs/needs-gameplay.md` § G30. Character
has 20 keys, so this is testable immediately.

### 6. G31 — what actually reads `DS:12`

`DS:12` reads `0xFF` in a live overworld session; the engine sets `tile_offsector = 4`.
Since the viewport-wrap fix, **nothing in the viewport consumes it at all**, so it is an
unexplained byte rather than a wrong constant. Find what writes and reads it. Low value,
but it is a loose end that will mislead the next person.

### 7. `site_leave()` still assumes the tens digit is an east/west coordinate

Proven this session that **the sector does not change on foot at an edge** (511/512 against
the discriminating rows of `mapx20` vs `mapx30`). The engine's sector-crossing logic was
built on the old reading. Audit `site_leave()` and the `asm/242D/2434` comment against that
result and correct or refute it. This is a real defect independent of the viewport bug,
which is already fixed.

### 8. Housekeeping, each under 15 minutes

- **`--debug cli` has no rotation.** It wrote 288 files in 45 seconds with nobody touching
  it, because the engine auto-PASSes while idle. Add a turn cap or ring buffer before
  anyone leaves a session running overnight.
- **`tools/vnc/up.sh` lies.** It defaults to 6080/5900, `fmt` holds 6080, websockify fails
  to bind, and the script prints `http://<host>:6080/vnc.html` as if it worked. It also
  does not forward `VNC_PORT`/`NOVNC_PORT`/`DISPLAY_NUM` even though `start.sh` reads them.
  Make it fail loudly on a port conflict.
- **`docs/needs-gameplay.md` had two G28s** (I caused that). Fixed to G30 in this commit —
  keep an eye on it, the file is append-only in practice.

## Containers, if you need one running

```sh
# engine in a browser (port 6081, LAN, no password — owner's decision, recorded)
cd /var/home/paul/Projects/decompile-u2-v2
make -C engine sdl && cp engine/u2-sdl deploy/novnc/u2-sdl
sudo podman build -t u2-novnc:latest deploy/novnc/
sudo podman run -d --name u2-novnc -p 6081:6081 \
  -v "$PWD/original:/content:ro,z" -v /var/tmp/u2/novnc-saves:/saves:z u2-novnc:latest
# :z is NOT optional — without it SELinux denies the mount and the engine blames
# a missing content directory. Xvfb must be 640x400x24 (window size, not 320x200).

# original game in a browser (port 6085)
./deploy/spice86-novnc/up.sh
```

## Hazards that cost time today — read these before debugging anything

1. **A stale `build/gen_content.h` produces two unrelated-looking failures**: a segfault
   in `snprintf` on the SDL build, and `no executable and no cached tiles` in the
   container. `make clean` fixed both. The mechanism from stale header to bad pointer is
   **not explained** — if it returns, `make clean` before believing anything else.
2. **`make ... | head -N` SIGPIPEs make to death mid-build.** The old binary survives with
   a fresh timestamp, every later step succeeds, and you ship a binary without the fix.
   This happened three times in one day. Never truncate the pipeline that produces the
   artifact.
3. **`make -C engine test` does NOT relink `u2-sdl`.** That is the separate `sdl` target.
   Verify deploys by **content**: `md5sum` inside the container vs host, or `strings` for a
   symbol only the new code has. A log line proves nothing — it prints identically for the
   old build.
4. **`gdb -batch -ex run` on an interactive engine hangs forever.** The inferior never
   exits and gdb waits. Always wrap in `timeout`.
5. **`bp_read --selftest` poisons the run.** Spice86 cannot remove a breakpoint, so the
   selftest's trap keeps firing. Restart before any real capture.
6. **`bp_read --dump` takes `OFF:LEN` with LEN decimal** (`137:160`, not `137:A0`).
7. **`bp_read` continues the target by default.** A memory read after the stop is a
   *different state*, not a later observation of the same one. I nearly built a theory on
   exactly that.
8. **Two address spaces.** `label = file + 0x200`. Runtime `0x0D44` is file `0x0F44`. This
   has caused several false blockers; check it before concluding an address is wrong.
9. **`CFLAGS=` on the make command line replaces `CFLAGS +=` wholesale.** Use `OPT="-O2 -g"`
   to add symbols while keeping `-Wall -Wextra -Werror`.

## Rules that bit today, so you do not learn them again

- **Default-deny passability is a decision, not a default.** It sealed every town (gates
  are arts `0x05..0x09` and you must stand ON a gate for `E` to fire) and blocked town
  paving (art `0x1C`, map `0x70`, 12889 cells — the second most common byte in the pack).
  Both were caught by the owner playing, not by tests. `u2_tile_walkable_byte()` exists so
  the rule is now assertable; keep it that way.
- **A test that encodes a wrong belief is worse than no test.** `view_check.c` asserted
  "columns left of the sector edge take the off-map tile, not a wrap" and passed, making
  the bug load-bearing. When you fix a belief, fix the test in the same commit and say why.
- **Demonstrate a gate failing before trusting it passing.** Every new check in this
  session was mutation-tested; keep doing it, it is cheap.
- **Owner reports outrank agent inference.** Both "seamless scroll" and "real terrain to my
  west" beat my reading of the asm, and both times the bytes agreed with the owner once I
  measured instead of argued.
- **Do not commit** `original/`, `references/`, `captures/`, `decompiled/`, `tmp/`,
  `tools/spice86/`, `tools/reko/`, or `deploy/novnc/u2-sdl`. All gitignored for a reason.

## If you get stuck

Stop and write down what you measured and what you could not resolve, in the same style as
this file. A named unknown is useful to Paul; a confident guess is worse than nothing.
Never touch anything outside this project root, and never delete outside the working tree
without asking by name.

# Bot driver — one policy, two executables

**Status: spec, then built same day. § 6 records what the build found that the spec did not
know.**

## 0. The decision this file makes

The bot is Python (owner ruling, `docs/goal-spec.md` § 7) and until now it could only drive
the **engine**, through `--api`. The original in Spice86 was treated as reachable only by
hand-driven captures. That was a framing error, not a technical one: the input path works
(`send_keyboard_key`), the observation path works (read DS over HTTP at ~250 req/s), and the
engine's state struct is already a byte-for-byte mirror of the original's DS segment.

So: **one bot, two targets.** The same policy code plays the engine build and the original,
and the interesting consequence is § 5 — running both on the same key script and diffing the
DS is a differential tester, which is stronger evidence than any CP gate because the CP gates
only check the engine against expectations we wrote ourselves.

## 1. The invariant that shapes everything

**The DS decode exists in exactly one place: the engine's C.**

The tempting design is a Python module that parses the DS blob — `player_x = ds[0x00]`,
`hp = bcd(ds[0x51], ds[0x52])`, and so on. That is a second hand-maintained copy of a decode,
which is the failure this project names over and over: two copies drift, and the drift is
invisible until a bot acts on a wrong field. The engine already has the decode
(`u2_state_json`, `u2_state_bcd_pair`, the `char` block the API emits) and already has a raw
DS loader (`u2_state_load_ds_buf`, which is a `memcpy` behind a length check).

So the adapter does not decode anything. It ships bytes to the engine and gets the engine's
own observation back:

```
Spice86 DS blob  ->  u2 --ds-json FILE  ->  the same JSON `--api` emits
```

If the two targets ever disagree about what a byte means, they cannot, because there is one
decoder. This is also what makes § 5 meaningful: a diff between the two targets is a diff
about the *game*, not about two different parsers.

## 2. Interface

`bot/target.py`. Both targets satisfy:

```python
class Target:
    name: str
    def start(self) -> None
    def state(self) -> dict          # identical shape on both
    def key(self, token: str) -> dict  # one action in, one observation out
    def close(self) -> None
```

`EngineTarget` wraps `engine/tools/u2api.py` (`Session.create/key/state/quit`).
`Spice86Target` wraps the emulator: keys via the MCP endpoint, state via an HTTP DS read
piped through `--ds-json`.

`key()` returns an observation on both, so policy never branches on target. That is the whole
point: a policy that says `if obs["char"]["hp"] < 20: ...` must work on either binary.

## 3. The original's input rules, handled by construction

Two properties of `asm/4F4D_key_poll` make naive driving silently lossy, so the adapter
enforces them rather than documenting them:

1. **The typeahead drain.** `asm/4F69..asm/4F74` loops AH=01h/AH=00h until the buffer is
   empty, discarding everything after the first key. Two keys sent in one poll window lose
   the second, and a lost key is observationally identical to a refused move. So `key()`
   sends one key and does not return until the observation shows the poll consumed it.
2. **Confirmation from memory, never from the send.** A dropped key returns `ok`. Every
   `key()` therefore reads state after sending and reports what the world did, not whether
   the write succeeded.

Modifiers are out of scope and stay out: `send_keyboard_key` applies Shift and ignores Ctrl,
the BDA ring is emulator-owned, and `write_io_port` on `0x60` does not feed the queue. See
`captures/spice86/g30-ctrl-y/`. Nothing open needs a chord.

## 4. Death and restart

Death in the original is memory-only and terminal — `asm/0C62_death` zeroes `DS:51..59` and
repaints, writes nothing, and there is no in-game recovery. So `Spice86Target` owns process
lifecycle: on death it tears down and relaunches from a named save, because a bot that cannot
recover cannot play for long. `char.dead` already exists as a signal (goal-spec § 7.3 item 2).

## 5. Differential mode

The reason to build this at all. Same key script, both targets, diff the DS after each step:

```
python3 bot/diff.py --script play_to/minax.keys --seeds engine_save original_save
```

Fields that legitimately differ are excluded **by name, with a reason**, and the list is not
new: `tools/u2_record.py` already carries one for exactly this problem. The known example is
`DS:74/75` (speed), which the original recalibrates at every startup and which therefore
cannot match across two processes — `docs/save-format.md` § 8 has the measurement.

An exclusion list without reasons is how a differential tester ends up excluding the bug.

## 6. What the build found

The spec was written first and was wrong in five places. Each is recorded rather than edited
silently into the body, because the next person needs the trap, not the tidy version.

1. **`--ds-json` has to run after `wire_backends`, and the file already said so.** The first
   attempt put the early exit right after `u2_state_init` and segfaulted inside
   `u2_state_json`. `api.c` has a comment above `u2_api_attach` explaining that the first
   observation "names the renderer, input and clock, and those only exist once
   `wire_backends` has chosen them" — the answer was in the tree and was not read first.
   gdb said in ten seconds what the comment would have said for free.

2. **The `.ds` files under `captures/` are raw binary, not the hex text `--load-ds` takes.**
   10240 bytes each, larger than the 1616-byte mirror, so the leading `sizeof(u2_state)`
   bytes are the DS segment. `--ds-json` sniffs on content and accepts both, because a flag
   that silently parsed binary as hex would return plausible garbage fields instead of an
   error.

3. **`strchr(set, '\0')` returns the terminator, which is non-NULL.** So the content sniff
   above classified an all-zero blob — a blank DS mirror, a real and common case — as valid
   hex and handed it to the text loader, which rejected it. Caught by the gate's title-blob
   check, not by reading the code. The explicit `!= 0` is now in the loop with a comment.

4. **The terminal renderer writes ANSI clear-screen into stdout.** The first parse used
   `startswith('{"u2api"')` and reported a complete, correct observation as missing. Fixed
   twice: `--headless` keeps the renderer out, and the parse searches for the substring so it
   does not depend on the first flag working.

5. **The digest is not a settle signal.** The first `key()` waited for the digest to *change*
   as proof the game had consumed the key. The digest covers the RNG register, and the game
   rolls while it idles, so the digest moves constantly and the call returned before the game
   had polled anything — reporting a character that never moved as though the key had been
   processed. `key()` now waits for **quiescence** (two consecutive identical digests), and
   `moved()` compares position, sector, HP and food instead. This is the same class of error
   as the disconnected wire: a signal that always fires is not a signal.

6. **The DS fields nest at `state.state`.** The API's `state` block is the debug dump and the
   fields live one level below it. `Target.fields()` unwraps it once so policy never learns
   the nesting.

7. **The gate caught two errors in the test itself**, which is the argument for writing
   checks rather than eyeballing: `S`'s BIOS make-code is `0x1F`, not `0x1E`, and character
   creation spends from a **90-point** stat budget (`asm/56E6`), so `[99]*6` is refused.

8. **The live run showed the refusal working, which was the point.** Driving the original
   through the adapter, `Ctrl-Y` came back as *refused by the adapter* with the ring message,
   not as a stationary character. That distinction is the whole reason § 3 exists.

## 7. Live proof, and how to run it

```bash
tools/spice86_start.sh --headless --patch-arrows --player SAVE --auto-play
python3 bot/play_original.py          # walks the original: Up/Left/Right/Down, food ticks
python3 bot/target_check.py           # the gated version, no emulator needed
```

`play_original.py` is a demonstration with output, not a gate. `target_check.py` is the gate
and is wired into `check_all.sh`.

## 8. Still open before the bot can reach the outstanding captures

The adapter removed the *access* problem. What is left is the *scenario* problem — reaching a
state the bot cannot earn by playing, which is a different thing and was conflated with it:

| item | what it needs that play cannot produce |
| --- | --- |
| G84 | a creature standing in a specific dungeon cell. `setpos` moves the player, not the monsters. |
| G49 | a frigate offshore with a shore in range. No vehicle-placement tool exists. |
| G43, G77 | the plane airborne. Needs a launch, which needs the plane in the right sector. |
| G83, G87 | **NOT seedable, corrected the same day this table was written — see below.** Needs a rocket launch. |
| G76 | a frame read by a human or a vision model; the bot can capture it but not read glyphs. |
| G75 | winning the game, which is goal-spec's whole problem, not this adapter's. |

**The row above originally said G83 and G87 were "reachable today — space with fuel is
seedable". That was asserted without testing it, which is the exact habit this project exists to
stamp out, so it was checked.** `u2_edit.py set world 10` (0x0A, the deep-space value) writes
the byte and it lands — `DS:6D` reads `0x0A` on a live run. The game then loaded the overworld
in sector 40 anyway, at the character's normal entry cell, and drew the normal overworld.
So the world byte alone does not produce a space start: the sector triple and the entry pair
still decide where you are, and nothing in the save says "in flight".

Reaching space therefore needs a **launch**, which means the rocket present, in a sector that
can launch, with fuel — and that is the same blocker as G43 and G77 one level up. It is not an
adapter gap; the adapter reads whatever state the game is in. It is that no tool can put a
vehicle where the game will accept it, and the field that marks "airborne" is the one W6
already flagged as needing a capture rather than a guess.

**A second gap fell out of the same test, and it is now closed.** With `DS:6D = 0x0A` the bot
reported `mode: "world"`, because `emit_obs` derived mode from `sector_second` and could not
see a space state at all — the gap `docs/play-api.md` deviation 2 used to name. It mattered
more than when it was written: a bot that cannot tell it is in space cannot decide to
hyperwarp.

`u2_state_mode()` in `engine/src/core/state.c` closes it, and it is worth being precise about
what that means. There is still no in-space byte, and the discriminator is the staging pair
being uniformly `0xFF` — a side effect of having no map to stage, not a flag. It works, it
works identically on both executables because it reads the state block and not a ctx, and it
is validated against the captures that defined the three states. But it is derived from a
rendering side effect, so it is bounded by what has been observed: a rocket launched from a
sector whose ones digit is 4 or 5 would read as dungeon, and whether that is reachable is the
same unknown as W6.


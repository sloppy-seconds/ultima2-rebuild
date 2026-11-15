# Plan — a play API: character creation and `step(action) -> observation`

## 0. Status — built 2026-11-08, gated by `make test-api`

`--api` works: line-delimited JSON on stdin/stdout, the engine speaks first, and a session
of `create` then keys drives the title flow into the world. `engine/tools/u2api.py` is the
client and `engine/tests/run_api.sh` is the gate (17 legs, each with the control that makes
it red — the budget check was disabled to prove AP3 can fail, and it failed).

Wired into `make test` as `test-api`, plus `make api` for a one-command smoke.

**Thirteen deviations from this plan, each forced by a measurement or by a gap the plan did not
name.** They are listed here rather than edited silently into § 3 and § 4.

1. **`turn` became `rolls`.** There is no turn counter on the state block — `ow->turns`
   lives in the overworld struct and does not exist at the title or in creation. The
   observation reports `rng_rolls`, which is already inside the digest scope and so
   correlates with the digest it sits next to.
2. **`mode` is derived, not tracked.** § 4 promised `title|creation|overworld|site|dungeon|
   space`. There is no phase field, so it is read from the state by `u2_state_mode()` in
   `state.c`, which returns `title`, `space`, `dungeon`, `site` or `world`, and `creation`
   is layered on by `api.c` because the engine's request-to-play flag has no DS address.
   Dungeon and space **are** now distinguished; they were not when this deviation was
   written, and the reason they could not be is worth keeping.

   Space is not a byte. In orbit the sector, position, `DS:12`, `DS:13` and `DS:14` are
   byte-identical to the launch pad, so the only observable that separates them is the two
   staging buffers (`DS:272..471`) being uniformly `0xFF` — measured in
   `captures/spice86/w6-orbit-dive` and recorded at `docs/handoff-space-view.md` § 3a, which
   also rightly warns that control flow must not test it. Nothing here gates on it; this is a
   report.

   And the staging pair does not mean space on its own. It means "nothing is staged", and a
   dungeon shares that: `captures/spice86/dungeon-entry` ev034..ev051 is a dungeon descent
   with the pair uniformly `0xFF`, and the first version of the rule called all eighteen
   frames space while its own synthetic test sat green. The dungeon digit is therefore tested
   first. `bot/mode_probe.py` runs the rule over every DS capture on disk and
   `bot/mode_orbit_check.py` runs it against the pad/orbit/landed frames, which is what makes
   the ordering a checked fact instead of an opinion.
3. **`awaiting` is always `key`.** The plan said it would be derived from the site that
   blocked. Nothing records which call blocked, so the honest value is the one that is
   always true: the engine is blocked on a key. A finer answer needs a seam change.
4. **A `char` block was added to the observation.** `u2_state_json` does not expose stats or
   name, so a bot could create a character and then not be able to see it. Rather than grow
   the debug serializer — which CP2's evidence is read through — the API emits its own
   `char` block from the state fields. `u2_state_json` is untouched.
5. **`--api` refuses `--dump-state`.** Hazard 1 in practice: the dump writes to the stdout
   the protocol owns. Refused with a message rather than silently suppressed, because a flag
   that looks like it is working and is quietly dropped is the failure mode this repo
   refuses everywhere else.
6. **The key alphabet moved to `core/keys.c`.** The plan did not anticipate that the API
   would need `--script`'s vocabulary, and copying `named_key` plus the hex and single-char
   rules would have been a second copy of an alphabet — the exact class of defect § A4
   exists to avoid. `input_script.c` now calls the same function the API calls.
7. **`u2_api_attach` and `u2_api_begin` are two calls.** The input backend's `init` needs a
   session to exist, but the first observation reads the renderer, input and clock names off
   `ctx`, and those are null until the backends are wired. Attaching early and speaking after
   is the fix; the first attempt segfaulted in `u2_state_json`, which is the more honest way
   to discover an ordering constraint than a silent empty observation.
8. **The terminal-renderer refusal checks the resolved renderer, not the flag.** `--headless`
   resolves the renderer itself, so testing `renderer_choice` refused a perfectly good
   `--headless --api` run. What matters is whether something will actually draw on stdout.

9. **`create` did not enter the world, and `mode` said it had.** The plan assumed a created
   character was a playable one. It was not: `u2_creation_apply` writes the record and the
   state, but the interactive flow's handoff lives in `title.c`'s `case 'C'`, which the API
   never passes through, so the title flow kept cycling its splash while `mode` reported
   `world` off the name heuristic. Fixed by setting the engine's own `enter_world` flag and
   adding it to `flow_stopped()` and `wait_choice`'s stop test — a request made from outside a
   flow's key handling is invisible to a loop that only reads keys. `mode` gained a third
   value, `created`, for the state between "has a character" and "is in the world", because
   collapsing those two is what hid the bug. Gated by AP9, whose first version asserted on the
   sector and passed with the handoff disabled — the record carries sector 20 either way — so
   the discriminator is now the overworld's own `CMD:` prompt.
10. **The observation grew a gear block, because a bot could create a character and not see
    it.** `items_held[16]`, `weapons_owned[10]`, `armour_owned[6]`, `readied_weapon`,
    `worn_armour`, decoded `hp`/`food`/`exp`/`gold`, and a `dead` flag. See
    `docs/goal-spec.md` § 7.3, whose items 1–3 this closes. Two encodings are handled inside
    the engine rather than being handed to the caller: the armour counts are indexed one below
    the menu digit (`armour_owned[0]` is CLOTH), and the ledger fields are packed decimal read
    through `u2_state_bcd_pair`, the same accessor the panel uses.


Everything below is the plan as written, unchanged, with the D-numbers and A-numbers the
implementation cites.

---

## 0.1 Original status — planned, not built

Requested by the owner 2026-11-08, after he asked how far the bot had gotten. The honest answer
was: no policy exists, and the one structural thing missing is this. Scope as given: *"at
minimum a character-creation API and a play API (the step(action) -> observation seam you
identified)."*

This is a plan. Nothing here is built yet; `--api`, `core/api.c`, `platform/input_api.c` and
`tools/u2api.py` are all proposals in this file. What already exists and is load-bearing is § 5.

**Why it is a plan and not a patch.** Two of the decisions below change what the engine prints
and where, and one of them (A4) touches character creation, which is CP2 byte-comparison
territory. Those deserve to be written down before code, in a repo whose recorded failure mode
is a plausible implementation that duplicated a rule and then drifted.

---

## 1. What this is, in one paragraph

Run the engine as a request/response server over stdio. The engine says *"I am at this state
and I need a key"*; the caller answers with one key, or with a declarative character
specification, or with a request to stop. Each answer is followed by an observation: the text
grid, the named state fields, the printed message the game produced, and the same digest the
demo recorder uses. A bot, a test, a fuzzer and a human watching a transcript all speak the
same protocol, and a session driven through it can be recorded with the existing demo
subsystem without any change to either.

## 2. Why this is not just `--script` with extra steps

`--script` is batch: the whole route is written before the run, the engine runs to the end, and
state is readable at exit. That is the right tool for a regression and the wrong tool for a
player, for three reasons this project has already felt:

1. **No closed loop.** A script cannot branch on what the game did. Every `play_to/*.keys`
   route is a fixed transcript, which is why the longest one is 83 lines of arrows rather than
   a game.
2. **Observation is post-hoc.** `--dump-state` fires at exit. A player needs the state *at the
   moment it is being asked for a key*, which is a different point in the loop and the exact
   point `docs/demo-record-playback.md` D12 had to move its own verification to.
3. **No back-and-forth for multi-key commands.** `ATTACK--` consumes a second key inside the
   handler. A caller of a batch script cannot know that; a caller of a server is told.

## 3. Decisions

### A1 — line-delimited JSON over stdin/stdout, not a socket

Rejected: a local HTTP or Unix-socket server. Reason: it needs a port, a lifecycle, and a
client that can wait for readiness, and it duplicates what stdio already gives a subprocess.
Every gate in this repo drives the engine as a child process; the API should be usable by the
same three lines of Python they already use. stdout is the machine channel and **stays** the
machine channel: every diagnostic in the engine already goes to stderr, so nothing has to move.

Consequence: one JSON object per line, no framing, no chunking. A caller reads a line, parses,
writes a line. `tools/u2api.py` wraps that so nobody hand-rolls it, because a hand-rolled
reader of a protocol is where protocol bugs hide.

### A2 — the engine speaks first

The engine opens with `ready` and every turn ends with `await`. The caller never polls.

Rejected: caller-asks, engine-answers. Reason: the number of keys a command needs is not
knowable from outside — `A` needs one more, `U` needs one more, `W` needs none, a `cmd_Y`
freeform prompt needs a whole word and a CR. A request/response model where the caller
initiates would either deadlock waiting for an answer to a question that was not asked, or
guess. `await` makes the engine's need explicit, and it makes the bot loop three lines long.

`await` carries the observation, so there is exactly one message per rendezvous and the two
sides can never disagree about which state a key was delivered into — the same property D12
buys for demos, arrived at from the other direction.

### A3 — keys arrive through `u2_input`, never a parallel path

`platform/input_api.c` is a `u2_input` backend whose `poll`/`wait` block on the caller's line.
`input_script.c` and `input_demo.c` are the templates, and `input_demo.c`'s header states the
rule this one inherits: *"there is no parallel demo code path"*. A key delivered through the
API is the same alphabet, through the same seam, past the same `key_uppercase_fold` and the
same Ctrl-S swallow, as a key typed by a person.

This is what makes an API session comparable to a demo and a demo replayable from an API
session. It is also why the API needs no changes to game logic at all.

### A4 — character creation is declarative, and reuses `commit()`

Rejected: driving the creation form through the API at keystroke level. Reason, and it is
this repo's own finding: `docs/demo-record-playback.md` deviation 5 measured that the form's
cursor — which field is highlighted, which dropdown is open — lives in a local struct and not
in the DS mirror. A keystroke-level creation API would have to reimplement that state to know
what the form currently holds, which is a second hand-maintained copy of the form, and copies
of this kind drift and then lie.

Adopted: `{"cmd":"create", name, sex, race, class, stats[6]}` builds a `form` and calls the
existing `commit()` in `game/creation.c`, exposed as `u2_creation_apply()`. One source for

### `{"cmd":"load"}` — the menu's P, declaratively (deviation 13)

Adopted: `{"cmd":"load"}` runs `u2_save_load()` — the same routine `title.c` runs for the P
key — and then sets `enter_world`, so the caller lands in the world without ever seeing a
menu. `create` is "press C and fill the form" declaratively; this is "press P" declaratively.
Both are menu *actions*, never menu *navigation*.

**It is refused once a handoff has happened**, with `load_only_at_menu`. The original cannot
load mid-session: loading means exiting to the menu and coming back. Guarding on
`enter_world` rather than `world_started` is deliberate and was forced by a measurement —
`world_started` is only set when the world loop actually runs a turn, so a caller that sends
an invalid key never starts the loop and `world_started` stays 0 forever, which let
`create` followed by `load` through. The menu offers C *or* P, so "a handoff already
happened" is the real condition.

Recovery for a bot is therefore: end the process, relaunch with `--api --save-dir X`, send
`{"cmd":"load"}`. This is the sanctioned answer to "how does a bot reload a save", and it is
deliberately not a mid-session load. A real load verb usable in-play waits on a multi-slot
save/load system, which `docs/save-scoped-worlds.md` D5 defers: one slot exists today and
`--save NAME` is not implemented.

Errors: `load_no_character` (the slot exists but holds a blank record — the original's own
`DS:36 == 0` test), `load_not_ready` (the path is unusable or absent, the insert-disk
analogue), `load_failed` (I/O). They are named apart because a caller that cannot tell an
empty slot from a broken one will retry forever.

Gate: `run_api.sh` AP13 (save outdoors, relaunch, load, state matches), AP14 (an empty slot
refuses rather than loading a blank character — the control that stops AP13 passing by
loading nothing), AP15 (load after create is refused). Control exercised: making `do_load`
skip `u2_save_load()` turns AP13 and AP14 red.
the BCD bonuses, the `sub $0x31` conversions, the entry cell, the starting HP/food/gold. The
API adds no rules and cannot contradict the form.

### A5 — the API validates exactly what the form validates, and refuses the rest

A declarative API that lets a caller set any stat to any value is a cheat engine, not a play
API, and it would produce states the game can never reach — which makes every digest computed
over them evidence about nothing. So the same constraints, from the same constants:

| rule | constant | source |
| --- | --- | --- |
| each stat >= 10 | `STAT_MIN 0x0A` | `asm/5C69` |
| six stats spend a 90-point budget | `BUDGET_START 0x90` | `asm/56E6` |
| name fits the 16-byte field | `[0x36..0x45]` | `asm/5400` region |
| sex `M`/`F`, race 1..4, class 1..4 | the dropdown tables | `OPT_SEX`/`OPT_RACE`/`OPT_CLASS` |

A refusal is an `error` event naming the rule, and the session continues — the caller is
allowed to try again, exactly as the form keeps the player.

**Not** modelled, and named so: the original restarts the whole generation on an overspend or a
stat below the floor (`asm/5C66` → `0x5698`). The form already deviates by refusing instead of
restarting; the API follows the form, not the original, and says so here rather than in a
comment three months from now.

### A6 — the observation carries what the game printed

Owner rule, 2026-10-26: every action the player takes and every effect on the player must
produce a message, and silence that is not justified by a cited byte is a defect. An API that
returned only numbers would make a bot blind to precisely that rule. So the observation
includes `message`: the text the engine emitted since the last delivered key.

Mechanism: a tap in the seam's text output (`seam_char_print` / `seam_str_out`), accumulating
into a bounded buffer on `u2_ctx`, drained into the observation. Bounded and capped with an
explicit truncation marker, because a formatter that silently drops text is the same lie in
the other direction.

### A7 — the observation carries the demo digest

`u2_demo_digest()` already defines the behavioural state scope (the DS mirror with D3's
excluded offsets blanked, plus the roll counter). Reusing it means:

* a bot's session can be recorded with `--demo-dir` and replayed and verified by the existing
  gate, with no new format;
* an API observation and a demo record are directly comparable, which is how a bot's replay
  divergence gets diagnosed with `demo_span_diff.py` rather than with printfs.

### A8 — `--api` refuses combinations that would fight over stdio

`--api` with `--script` or `--playback` is refused at parse time with its own exit code, in the
shape of the existing `--playback` + `--script` refusal. Two input backends cannot both own the
key, and a silent precedence rule would mean the flag that looks like it is working is not.

`--api` with the terminal renderer is also refused: both want stdout. SDL and headless are fine
(headless is the default for tests), and the text grid in the observation comes from the VRAM,
not from the renderer, so a headless API session still reports what the player would see.

---

## 4. The protocol

One JSON object per line, in both directions. `"u2api":1` on every engine message so a caller
can tell our output from a stray print.

### Engine → caller

```json
{"u2api":1,"event":"ready","obs":{...}}
{"u2api":1,"event":"await","obs":{...}}
{"u2api":1,"event":"end","obs":{...},"reason":"quit|input_closed|game_over"}
{"u2api":1,"event":"error","code":"stat_below_floor","message":"..."}
```

### Caller → engine

```json
{"cmd":"create","name":"BRITISH","sex":"M","race":1,"class":1,"stats":[10,10,10,10,10,40]}
{"cmd":"key","key":"U"}
{"cmd":"key","key":"<CR>"}
{"cmd":"state"}
{"cmd":"quit"}
```

`key` is one key in the same alphabet `--script` uses — `A`, `0xf3`, `CR`, `ESC`, `WAIT` —
parsed by the shared `u2_key_from_token()` in `core/keys.c`, so there is no second
vocabulary to learn and no second table to drift. A multi-character token that is not in
the alphabet is an `unknown_key` error rather than a silent split: a caller that means
three keys sends three messages and sees three observations.

### The observation

```json
{
  "mode": "title|creation|overworld|site|dungeon|space",
  "screen": ["row 0", "row 1", "..."],
  "message": "CMD: ATTACK--EAST--HIT!!!",
  "state": { "...named fields from u2_dump_state_json..." },
  "digest": "sha256 of the demo scope (A7)",
  "turn": 12,
  "awaiting": "key|word|nothing"
}
```

`awaiting` is what makes A2 usable: the engine says *what kind* of input it wants, so a caller
does not have to infer from a trailing `WHICH?` that a menu is open. It is derived from the
site that blocked, not guessed.

---

11. **The API could not play the game, and every existing leg passed anyway.** Deviation 9
    fixed `create` so it *requested* the world; it did not make the world run. A `create` is
    delivered from inside `seam_key_poll`, while some title-flow screen is already sitting in
    that poll, and the title flow's stop test lives in its **no-key** arm — so when the create
    returned, the next key arrived on the key arm, was read as a menu key, and was lost. The
    overworld loop was then entered with the caller's input already spent.

    Measured, before the fix: **forty direction keys, `rolls` never left 0**, the overworld's
    own key poll reached **zero** times, and `u2_gate_turn` never ran. The API answered
    `await` to every command, reported `mode: "world"`, and did nothing. AP9 passed throughout
    because it greps for `CMD:`, which the world loop emits once before it exits — the
    assertion proved a prompt was printed, not that a world was running. It is the exact shape
    AGENTS. § 3 calls a gate that cannot go red.

    Found by instrumenting every poll site rather than reasoning about it: marking the seven
    `seam_key_poll` calls in `title.c` showed the create landing in `hold_for_esc`'s headless
    arm, twice. An earlier attempt to fix it there was reverted because it changed scripted
    runs and broke CP2's committed replay evidence — the correct lesson being that the fix
    belongs in one place, not in six poll sites.

    The fix is that one place: **`u2_api_command` parks a key while a handoff is pending**
    (`enter_world && !world_started`) and answers `U2_API_NONE`, which is the state `api.h`
    had always declared and never returned. Every flow that consults `flow_stopped()` then
    unwinds on its own, and the parked key is handed to the overworld loop when it asks.
    `world_started` exists because `enter_world` stays set for the whole session and so cannot
    distinguish "handoff pending" from "handoff long finished".

    `AP12` is the leg that was missing. Its discriminator is `rolls`, which no screen redraw
    can fake — only a completed world turn advances the RNG — paired with a keyless control
    leg proving the test can go red. Reverting the parking fix reproduces the failure exactly:
    `rolls 0 cd 0`.

12. **The observation grew a `gate` block, because a bot cannot walk to a door it cannot see.**
    `state` is the debug dump and it has never carried `DS:6E` or `DS:6F`, so until now there
    was no way to ask where a time door was, when it would move, or where stepping on it would
    lead. The block reports `present, x, y, slot, countdown, era, world, dest_era, dest_x,
    dest_y`.

    `present/x/y` are **published by the draw path** onto `ctx`, following the
    `screen_player_col/row` precedent, rather than re-derived in `api.c`. Two reasons: a second
    latch for "has it stamped yet" would drift from the one the renderer uses, and the caller
    should be told about the door that was actually drawn. The rest is derived through the same
    pure functions the engine uses, so there is one rule and not two. The block is omitted
    outside the world — at the title the era and world bytes are both zero, and reporting
    `era: 0` there would read as a real location.

    Note what `present: false` means at load, because it is correct and not a gap: the original
    stamps nothing until the countdown first expires, and `asm/2701` is `decb 0x6f ; je`, so a
    countdown that starts at zero takes ~256 turns to get there. See `docs/moongates.md`.

## 5. What already exists — do not rebuild it

| need | already there |
| --- | --- |
| key delivery into game logic | `u2_input` vtable; `input_script.c`, `input_demo.c` as templates |
| the key alphabet | `--script` tokens, `key_uppercase_fold`, the Ctrl-S swallow, all in `core/seam.c` |
| named state as JSON | `u2_dump_state_json()` in `debug/dump_state.{h,c}` |
| behavioural digest | `u2_demo_digest()` in `core/demo.c` |
| the text grid | `u2_dump_screen()`, and the VRAM it reads |
| what mode we are in | `u2_demo_playback_mode()` / the `U2_DEMO_MODE_*` enum |
| declarative character write | `commit()` in `game/creation.c`, with the bonus tables as data |
| recording a session | the whole demo subsystem; `--demo-dir` composes with this |
| reading a divergence back | `engine/tools/demo_span_diff.py`, `tests/demo_field.py` |

## 6. Work items

| # | work | gate |
| --- | --- | --- |
| A-W1 | `core/api.{h,c}`: the protocol, the observation builder, the text tap | `tests/api_check.c` — framing, observation fields, the tap's truncation, and a control per assertion |
| A-W2 | `u2_creation_apply()` in `game/creation.c`, and the A5 validation | `tests/api_check.c`: every rule in A5 goes red individually; a valid spec produces byte-identical state to the same character made through the form |
| A-W3 | `platform/input_api.c`: the blocking `u2_input` backend | `tests/run_api.sh`: a scripted conversation drives the title into the world |
| A-W4 | `--api` wiring in `main.c`, plus the A8 refusals | `run_api.sh`: each refused combination exits with its own code and names the conflict |
| A-W5 | `tools/u2api.py`: a Python client so nothing hand-rolls the protocol | `run_api.sh` uses it, so the client is exercised by every leg |
| A-W6 | a bot-shaped end-to-end example: create, enter the world, walk, and stop, asserting the digest moves and the messages are non-empty | `run_api.sh`, and it is the leg that proves the seam is actually enough to play |
| A-W7 | `make api` target and this doc's status block updated | same shape as `make drive`: refuses with a reason when its input is absent |

## 7. Hazards

1. **Stdout discipline.** One stray `printf` to stdout corrupts the protocol mid-line and the
   caller sees garbage instead of an error. Every existing stdout writer must be off in API
   mode (`--dump-state`, `--dump-each`, the `overworld:` note line). The gate must include a
   leg that runs a whole session and asserts every line it read parses.
2. **Blocking forever.** A caller that dies leaves the engine blocked on stdin. stdin EOF must
   end the run cleanly with `reason:"input_closed"`, and the engine must not spin when the
   caller is merely slow. `poll()` blocking on a line read is the whole design, so this is
   also the main way a bug here would present: a hung test.
3. **The text tap is a second copy of the screen.** A6 must capture what was *printed*, not
   re-render the grid, or the two will disagree whenever a message is printed off-screen. If
   the tap and the grid ever disagree, the grid is the picture and the tap is the message; say
   which one a field came from in the field name.
4. **A bot is not the deliverable.** Nothing here decides anything. If this doc is read as
   progress toward a player, it is not: it is the seam a player would plug into, and the
   policy is still the whole remaining distance.
5. **Do not let the API become a way to set state.** A-W2 is character creation because the
   owner asked for it, and it is validated against the game's own rules. A generic
   `{"cmd":"set","field":...}` would be trivially useful for tests and would quietly turn every
   digest produced through it into evidence about a game that cannot exist. If a test needs to
   force a field, `--load-ds` already exists and is honest about being a test hook.

## 8. Open questions for the owner

1. **Does `create` need to write the save file**, or only the in-memory record? Writing it makes
   the session resumable by `--play-map`; not writing it keeps the API side-effect-free. The
   plan says write, because a created character you cannot return to is a character you cannot
   debug.
2. **Is a `game_over` observation in scope now?** Nothing in the engine detects an ending —
   CP8 is still `todo` — so the honest answer is no, and `reason` stays `quit` /
   `input_closed` until CP8 defines the rest.

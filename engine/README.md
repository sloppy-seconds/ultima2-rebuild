# engine/ — portable C engine (Phase 4)

A native Linux reimplementation of *Ultima II*, written by reading the
byte-proven `asm/` tree. No DOSBox. Behavioural fidelity is argued from the
asm and confirmed by playthroughs; it is **not** byte-proven. The asm tree
remains the only proven artefact — see `disasm/phase3-summary.md`.

## Build and run

```sh
make                      # cc only, C11, no third-party dependencies
make test                 # CP0..CP7 acceptance tests (towns, combat, commerce,
                          # dialogue and the dungeon engine are CP5..CP7)
./u2                      # interactive terminal play
./u2 --headless --script tests/title.keys --dump-state
./u2 --headless --script tests/title.keys --dump-each   # frame-by-frame grid log
./u2 --headless --script tests/title.keys --trace=trace.log
./u2 --headless --script tests/play_to/reach_everything_today.keys --seed 0 \
     --replay-report /var/tmp/run.json                  # verifiable transcript
python3 tools/playlog.py verify /var/tmp/run.json       # REPRODUCES | DIVERGES
./u2 --headless --selftest --script tests/selftest.keys --dump-state
make content              # regenerate build/gen_content.h only
```

`--headless`, `--script`, `--dump-state`, `--dump-each`, `--trace`, `--seed` and
`--replay-report` are the agent-facing contract — see [docs/playing.md](docs/playing.md).

### SDL and cross-builds

```sh
make sdl                  # native SDL3 build (needs SDL3 on the host; make sure
                          # pkg-config can see sdl3.pc — an owner-approved
                          # dependency). Prints a loud note when what it linked is
                          # not the pinned static SDL3 the release ships.
make sdl-static           # single-file Linux player binary: SDL3 linked static,
                          # glibc dynamic; needs a static libSDL3.a prefix — on
                          # Bazzite run ../deploy/linuxbuild/build.sh
make sdl-win              # Windows x86_64 .exe, static SDL3 — needs the mingw
                          # toolchain; on Bazzite run ../deploy/winbuild/build.sh
                          # which does exactly this inside a container
make licenses             # regenerate src/core/licenses_data.h from LICENSE and
make licenses-check       # engine/licenses; the header is committed, not built
```

The static builds are the player-facing artifacts: one file plus the user's own
game data, no container and no package installs (the containers are build hosts
only).

### Renderer selection

`--renderer=sdl|terminal`, and the default is a property of the **build**: `sdl`
in a binary compiled with SDL, `terminal` in the cc-only one. That is what lets
`./u2-sdl-static` need no flags and no terminal, while every test script keeps
driving the text build it has always driven. `--sdl` stays as an alias.

Two refusals, both because a silently ignored flag reads exactly like a working
one:

* `--renderer=sdl` in a binary built without SDL is an error, not a quiet fall
  back to the text grid. This is the failure a player meets first, and it used to
  be indistinguishable from "the window failed to open".
* `--script` outside `--headless` is refused. No interactive backend reads
  `script_path`, so the keys would be dropped and the run would wait for a human.
  `docs/phase4-state.md` had this recorded as "`--sdl` cannot take `--script`"
  and blamed argument order; the order was never the problem.

Gated: CP0 asserts the refusals on the terminal build, CP4 asserts the flagless
SDL build really does present a frame (`--dump-screen` is SDL-only, so a written
frame is the proof) and that `--renderer=terminal` writes none.

### `--video-drivers`, and why the static build needs X11/Wayland headers

`--video-drivers` prints the video backends compiled into the binary and exits,
with no display required. It exists because the first player binaries were
unplayable on a desktop: SDL's cmake **silently compiles out** any backend whose
dev packages are missing, and the build containers installed none, so the static
`libsdl2.a` contained only `dummy` and `offscreen`. The binary linked, was
properly static, passed every check in the pipeline, and could not create a
window anywhere. `deploy/linuxbuild/build.sh` and `release-builds.yml` now both
fail unless `x11` or `wayland` appears in that list.

If a window still does not open, run it and read the line: SDL init failure now
prints the drivers present and the value of `SDL_VIDEODRIVER`, and a successful
startup prints which driver it got (`u2: video: wayland`). `u2-sdl-static` links only libc/libm/libdl — SDL dlopens X11/Wayland/ALSA
at runtime — and `u2-sdl.exe` imports only stock Windows DLLs. Cutting a `v*`
release tag builds both on CI and attaches them, with SHA256SUMS, via
`.github/workflows/release-builds.yml`.

`sdl-win` produces one `u2-sdl.exe` whose only DLL imports are stock Windows
ones (SDL2 is built static from the pinned release tarball inside the winbuild
image — Fedora dropped `mingw64-sdl2`, so the image owns the build). The wall
clock is the one backend chosen by host OS rather than by mode:
`src/platform/time_win32.c` replaces `time_posix.c` in the win targets, because
mingw declares `clock_gettime` and `nanosleep` and then has nothing to link them
against — a compile-clean, link-fail bug that only a cross-build can show, and
the release workflow found it before this directory did. The Windows binary now
runs: the owner played it **under wine on Bazzite Linux** (2026-11-02) through
character creation, the overworld, towns and combat, and it loaded a save the
Linux build wrote. Wine is not Windows — see `deploy/winbuild/README.md` for what
that does and does not establish. The macOS build is deliberately not
cross-compiled from Linux (Apple SDK); it is a CI macos-runner job, deferred by
the owner 2026-11-02. The Windows entry point is the SDL backend; the terminal
backend compiles to a stub that says so.

**The window is resizable and the picture scales with it.** The fit is integer and centred, so a
source pixel is always exactly `s x s` device pixels and the remainder becomes letterbox bars;
below 1:1 it fills the window instead, because cropping loses content and squashing does not.
The geometry is `src/core/scale.c` — pure integer arithmetic, no SDL dependency — rather than
something inside the backend, on the same rule as the palette: a renderer moves pixels, it does
not decide what they look like. `tests/scale_check.c` gates it in the cc-only build.

**`--version` says which build you are holding**, and `make sdl` asks before it links. The reason is
`docs/sdl2-vs-sdl3.md`: on Bazzite and Arch, `sdl2-config` resolves to `sdl2-compat`, which is the
SDL2 API implemented over SDL3, so a dev build compiles, runs, and is not the artifact the release
ships. `engine/tools/sdl_probe.sh` decides and reports; a static prefix wins and is stamped
`artifact=release`, anything else prints that it is a dev build. `U2_STRICT_SDL=1` makes that a
refusal. The output distinguishes the compile-time stamp from the runtime SDL version, because the
two disagreeing is itself the signal.

`build/`, `u2` and every generated header are gitignored.

## Game text is committed; art and data are not

The engine's strings live in `content/gen_content.h`, which is **generated and
committed**. That is deliberate, and it is ruled in AGENTS.md § 0 ("The text/art
line"): the game's text is the one kind of content the engine cannot substitute,
and the reconstruction in `asm/` already carries every byte of it.

The reason it is committed rather than rebuilt is that the two sources are not
equal. `tools/extract/extractor.py` reads either of them:

```sh
make content          # from ../original/ultimaii.exe — all 220 strings
make content-check    # the drift gate: asm/ vs the committed header, needs no exe
python3 tools/extract/extractor.py --emit text    # SYMBOL<TAB>value, for tests
```

The `asm/` fallback resolves **181 of 220** and leaves 38 as empty literals,
because those strings sit in regions Phase 3 rendered as byte lists or as
mis-decoded code rather than as `db "..."` text — the whole character-creation
screen, the seven `STREET_*` talk lines, the WEAR menu and the grocery question
among them. An empty literal prints as nothing, so a build that regenerated the
header on a machine without the exe would ship a blank character-creation screen,
and every test that greps the screen for text would pass while nothing was
printed. So the fallback is no longer a build source; it is the verifier.

And as a verifier it does better than it does as a builder: **214 of 220**.
`--verify` does not use the quoted-literal index at all. It decodes every `db`
payload of the file the manifest names — quoted text, bare hex byte lists, and
lines that mix them — and requires each committed string to be present in those
bytes. Nothing is duplicated into the manifest to make that work, which is the
whole point: the claim being checked is "asm/ contains these bytes", and `asm/`
assembles back byte-for-byte identical, so present means proven. Six strings are
not present in any text form, in four files where the disassembler read the
string as instructions; they are named in `UNPROVABLE`, and a seventh name is a
red.

Three things make the committed copy safe, and all three are enforced:

* the extractor is the file's only writer — its banner says "do not edit by hand"
* `make content` refuses without the exe, and the extractor itself refuses to
  write a fallback header over an exe-derived one (`--force` overrides, by name)
* `make content-check` fails if a committed string is empty, if the committed
  header omits a symbol the manifest lists, or if a string asm cannot prove is
  not on the known-unprovable list. If the list shrinks it says so instead of
  staying quiet, because the gate is meant to only ever get stronger

There is deliberately **no make rule to remake `$(GEN)`**. The build consumes the
committed file and fails loudly if it is missing. `build/` is off the include
path for the same reason: a stale `build/gen_content.h` from before this change
would otherwise shadow the committed header and silently restore the 38 empties.

Art and sound stay substituted under the cleanroom (`assets/tiles/cleanroom/`,
`docs/cleanroom-procedure.md`), and the world data — `mapxNN`, `monxNN`, `tlkxNN`,
the tile art the engine extracts at runtime — is still the player's own copy and
is never in this repository.

## Screen model

40 columns x 25 rows, not the 80x25 the plan assumed. The row-clear loop prints
40 spaces (`asm/53A0_sub_53a0.asm`), the printable margin is 0x26/0x28
(`asm/456B_sub_456b.asm`, `asm/48A7_sub_48a7.asm`), and the game sets video
mode 4 — 320x200, exactly 40x25 cells of 8x8 (`asm/4A3E_sub_4a3e.asm`). The
text printer reproduces `char_print`'s control flow: attribute from `DS:0x23E`,
CR returns to `DS:0x54F`, wrap at `DS:0x31`, scroll of rows 20-23 at row 24.

## Layout

```
src/core/       vram grid, DS state struct, rng, timers, the seam layer
src/game/       game logic; each module cites the asm functions it implements
src/platform/   renderer_{term,null} input_{term,script} audio_{beep,null} time_{posix,script}
src/debug/      --trace seam log, --dump-state screen + JSON
tests/          scripted playthroughs and the checkpoint test runners
tools/extract/  extractor.py — the user's copy -> build/gen_content.h
docs/playing.md CLI contract — CP2 replaced the planned MCP play-API with
                this; there is no server and no tools/u2_mcp/ (the line that
                used to sit here described a directory that never existed)
```

## The seam rule

Game logic talks to the outside world only through `src/core/seam.h`, which
routes to the four platform vtables in `src/platform/platform.h`. No terminal
escape, `sleep()`, `read()` or SDL call may appear under `src/game/`. Each seam
stands in for one named routine in the asm tree, and `--trace` prints the asm
address on every call, which is what makes a trace auditable against `asm/`:

| seam | asm spec | engine |
| --- | --- | --- |
| `char_out` | — engine primitive, no located asm site | `seam_char_out` |
| `char_print` | `asm/5003_char_print.asm` | `seam_char_print` |
| `char_print_wrap` | `asm/53F4_char_print_wrap.asm` | `seam_str_out` |
| `str_out` | `asm/4FDF_str_out.asm` | `seam_str_out` |
| `str_out_at` | `asm/51EB_str_out_at.asm` | `seam_str_out_at` |
| cursor set | `asm/51FD_str_out_core.asm` | `seam_goto` |
| row clear | `asm/53A0_sub_53a0.asm` | `seam_clear_row` |
| `key_poll` | `asm/4F4D_key_poll.asm` | `seam_key_poll` |
| `rand_byte` | `asm/5217_rand_byte.asm` | `seam_rand_byte` |
| `timed_wait` | `asm/52CA_timed_wait.asm` | `seam_wait_ms` |
| `speaker_freq` | `asm/5CD2_speaker_freq.asm` | `seam_tone` |
| `map_tile_ptr` | `asm/50B3_map_tile_ptr.asm` | overworld module (CP4) |
| `cmd_prompt` / `cmd_dispatch` / `cmd_done` | `asm/087D`, `asm/0A1F`, `asm/0A33` | command loop (CP6) |

`key_poll` is larger than the function boundary Phase 3 drew: it consumes the
key (INT 16h AH=00h), maps a few control bytes to NUL, and upper-cases a-z with
`and $0xdf`. The engine does the same, so everything downstream sees uppercase.

## Two global stores, on purpose

The original runs with `CS` at the loaded image and `DS` pointing at a
separately allocated 64 KiB working segment (`asm/0100_sub_0100.asm`; runtime
`DS = 0x08BE`). Bare-displacement globals in the asm live in DS; a second set
of statics lives in the image segment and is addressed with a `%cs:` override.
`src/core/state.h` mirrors the DS segment as a struct with compile-time offset
assertions. CS-side statics belong to the module that owns them and are cited
in that module's header. See `disasm/selfmod-capture.md`.

## Headless is a first-class mode

`--headless` swaps in the null renderer, the script input and a virtual clock
that does not really sleep, so a key script replays identically and stdout is
machine-readable.

One flag opts out of the virtual clock by name. `--playback-pace` asks for
real-time pacing in a headless playback, and the script clock's sleep is instant
by design, so the flag wires the host clock instead. A presentation knob that
silently does nothing is worse than one that is not offered, and the demo gate
(W15) measures that the paced and unpaced runs reach the same verdict.

`--headless --renderer=terminal` is honoured rather than ignored: the grid still
composes and the escapes still go to stdout, while the input and clock stay
scripted so the run is still reproducible. That combination exists because "the
renderer cannot reach game state" is a claim that can only be made by running a
second renderer over the same demo and comparing digests.

## Demo recording and playback

`Ctrl-R` records; `--playback FILE` replays and verifies. The design is
`docs/demo-record-playback.md` and the implementation is `src/core/demo.c`,
`src/platform/input_demo.c` and the gate `tests/run_demo.sh` (`make test-demo`).

What a `.demo` holds, in one plain-text file: a checkpoint of the whole DS mirror
(`0x650`, not `--dump-ds`'s `0x280`), then one record per key the game consumed,
each carrying a digest of the state that key was delivered into. Playback restores
the checkpoint, re-injects the keys through the same `u2_input` seam a player's keys
arrive on, and compares every digest. A demo that plays back wrong is a failed
playback: it names the record and exits 5. A demo written under a different digest
scope plays and exits 6, because the stimulus does not depend on the engine version
but the comparison does.

Three properties worth knowing before using one:

* **The unit is the delivered key, not the turn.** That is what lets one file cross
  the overworld, a site and back out with no per-mode hook, and it localises a
  divergence to the key that caused it.
* **Playback writes nowhere you own.** A `--playback` run redirects its save
  directory to a scratch directory beside the demo, because the original auto-saves
  on site entry and on rocket launch and a viewer watching someone else's demo must
  not have their own slot mutated by it.
* **A demo is evidence of reproducibility, never of fidelity.** If the engine is
  wrong at record 200 the demo records the wrong behaviour and replays it perfectly.

`demos/` is gitignored: a demo encodes a playthrough of copyrighted content exactly
as a save file does. The gate records its own fixture from engine-authored filler
(`tests/demo_seed.py`) and no committed game data.

`engine/tools/demo.py` is the tool side: `inspect`, `verify`, `against-trace`,
`diff`, `export-keys`, `from-playlog` and `corpus` (replay a directory of saved
 demos and report match / first divergence / unverifiable per file).

## Randomness

`rand_byte` is a six-byte feedback shift register at `DS:0x23F..0x244`
(`asm/5217` + `asm/522B` + `asm/5236`, which Phase 3 had split into three
fragments). It is seeded once at program launch from the BIOS keyboard-flag
byte at `0x40:6C` (`asm/0100`) and nothing else writes those bytes. So rolls
are a pure function of seed and roll count: encounters, combat rolls and monster
spawns replay exactly for the same inputs, with no clock involved. `--seed N`
sets that byte (default 0, what a cold boot gives); `--rng-sequence N` prints
the first N values as a test vector.

## Known limits

* Behaviour is argued from the asm and checked by playing, not differentially
  tested against the original (owner decision — see `docs/phase4-plan.md`).
* The `rand_byte` vector in `tests/run_cp0.sh` is derived from a hand decode of
  the three fragments, not captured from the running game; it guards against
  regressions and documents the reading.
* The title strings contain a two-byte bracket sequence where the game means
  "II" (in the game title, the intro's part number and the PLAY menu line). The
  original never redefines the font, so this is a glyph question, not a decoding
  one; the engine renders the bytes as extracted and CP9 decides the mapping.
* The intro in the original also loads PIC picture files (the FCB names near
  file `0x419`, the sector read at `asm/535A_sub_535a.asm`). The engine has no
  bitmap pipeline until CP9, so the text screens stand alone.
* Sound and timing feel are approximated: the beep backend is the terminal
  bell until the SDL2 target at CP9, and `timed_wait` is a wall-clock delay
  rather than a cycle burn. Intro hold durations are a choice, not a fact.
* CGA glyph and colour authenticity is a CP9 decision.

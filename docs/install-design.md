# Installer design — building a game directory out of a pile of media

Status: **design, and three slices of it are now built.** This document takes over
as the entry point for everything about *assembling* a playable tree, and it
inherits the parts of `docs/handoff-themes-builder.md` that were never built. That
handover is now **locked off** — its slice C list is folded into § 10 below rather
than left in a handover, because handovers are entry points and this is the one.

**Built and gated (2026-11-04).** The content roots (§ 3, layout B) in
`engine/src/core/roots.c`, with `world.c`, `tiles.c` and `pic.c` all resolving
through the one list; `config.ini` with the precedence rule (§ 4) in
`engine/src/core/config.c` and the shared parser in `core/ini.c`; the installer
itself in `engine/src/core/install.c`, reached as `--install --to DIR` and
`--install --check --to DIR` in both builds; the theme builder in
`engine/src/core/themes.c` behind `--install --themes-dir`, which replaced and
removed `tools/build_themes.py`; and `core/artifact.h`, which keeps the
dev-checkout fallbacks out of a release artifact. Gates: `world_check` (78 checks),
`config_check` (35), `install_check` (70), and `install_controls.py` (13
properties, each proved able to go red). All of it runs headless in CP4.

**Not built:** self-update (ruled out, § 9). The FM Towns derivation that § 11
deferred has since been ported: `--install --fmtowns-media-dir` derives
`themes/fmtowns` from the player's own disc, with the DOS-to-FM tile mapping
compiled into the binary as `engine/content/fmtowns_map.h`.

**The five open decisions were ruled on 2026-11-04** and the rulings are recorded in
§ 11, along with the two that came out of them (D6, D7) and the follow-up ruling
D7a that settled the last contradiction. **The design is settled; nothing here is
built yet.**

Prerequisite reading: `AGENTS.md`, then `docs/theme-format.md` (the contract for
what a themes directory holds), then `docs/planet-maps-galaxy-patch.md` (why the
outer planets need a fan patch at all).

Grades per `AGENTS.md` § 5. Claims marked *measured* were checked against the tree
on 2026-11-04; claims marked *proposal* are decisions being asked for, not facts.

---

## 0. The problem, stated honestly

Today the shipped artifact is a single binary that you drop next to the game files
and hope. `--data-dir` and `--save-dir` point at things, and if the player's
directory is missing a file the engine reports what it could not find — which is
good behaviour for an engine and a poor story for a person who has downloaded a
1982 game in three zips from two different projects.

Two facts make this worse than a copy operation:

* The game's content is **layered**. `original/` is the DOS release; `updated/` is
  the fan Galaxy patch carrying `MAPG*`/`MONG*`/`TLKG*`, which are the outer
  planets the floppy release lost to an MS-DOS filename collision
  (`MAPG10` vs `mapx10`). A tree with only the first layer is a game with one
  planet, and it runs perfectly happily, so the player cannot tell.
* The shipped binary currently **knows where a developer's checkout lives**:
  `references/u2upgrade` is a fallback candidate in both the theme and picture
  resolution orders. That is a dev convenience baked into a player artifact, and an
  installer that writes an explicit config is the honest place to delete it.

**Both modes must keep working.** "Drop the binary in the Internet Archive copy and
run it" is true today and stays true; the installer is additive, not a
prerequisite. If that drifts into requiring the installer, the artifact has become
worse than the thing it replaces.

## 1. Why the installer lives in the binary

*measured* — `engine/src/core/world.c` already resolves content with a
patched-first candidate list: `updated/MAPG` → `updated/mapg` → `MAPG` → `mapx`,
and the same shape for `MONG` and `TLKG`, recording the bare prefix on write-back
so a map read from `updated/` still stores under a legal root name.

That ordering is the whole Galaxy-patch mechanism, and it is the reason the
installer should not be a separate script: a second implementation of "which file
wins" in another language is free to drift from the one the engine actually uses,
and the failure is silent — "I copied everything and the planets still do not
work". Same argument for themes: `docs/theme-format.md` is a contract between the
builder and `tiles_ega.c`, and every bug in this thread so far came from the two
sides of that contract disagreeing.

Secondary reasons, in order of weight:

* No interpreter on the target. A player artifact should not require Python.
* The installer becomes testable by the same CP harness as the engine, which is the
  only kind of test this project trusts.
* *measured* — the binary already has a tool family that prints and exits ahead of
  the game: `--version`, `--licenses`, `--video-drivers`. `--install` is not a new
  kind of thing, it is the fourth member.
* It gives a future self-update a place to live (§ 9).

## 2. Modes

| mode | what it does | writes |
| --- | --- | --- |
| `--install --to DIR` | build a game tree at DIR from the supplied sources | DIR |
| `--install --check` | inventory a tree and report what is missing, stale or unexpected | nothing |
| `--dry-run` | with `--install`: plan and report, write nothing. Shows which copy of a shared filename wins, and what the theme step would do | nothing |
| `--no-prompt` | with `--install`: never ask. A flag that was not supplied is refused instead of prompted for. Prompts are also off automatically when stdin is not a terminal (§ 13) | — |
| `--original-dir DIR` | the original DOS release: an unzipped IA copy, a CD rip, or an existing emulator directory | — |
| `--updated-dir DIR` | the Galaxy patch layer. If omitted and `<original-dir>/updated` exists, that is used (D9, § 12) | — |
| `--themes-dir DIR` | the fan art package (a `U2Upgrade` directory or the patcher archive). Repeatable | — |
| `--fmtowns-media-dir DIR` | the FM Towns disc image, or a directory holding one; derives `themes/fmtowns` in the same run (§ 10) | — |

Every question the installer can ask has a flag, so nothing about its behaviour
requires a keyboard. `--to` is the target and is required; the source flags are what
a prompt would otherwise ask for, and a prompt is only ever the fallback (§ 8).

Three rules about the shape:

1. **Declarative first, prompts second.** Every input has a flag. Interactive
   prompting is only the fallback when a required input is missing (§ 13). The
   original form of this rule said a prompt loop "cannot be gated", which turned out
   to be half wrong: the question text cannot, but the two properties that actually
   matter can, and both are now checks. That a terminal-less stdin refuses rather than
   blocks is asserted directly, and the target rule the prompts depend on is covered
   by a control. What is genuinely untested is the wording and the re-ask loop, and
   that is the honest limit rather than a reason to skip the gate.
2. **It runs before video init**, like the other tool modes. An installer that
   needs a display is useless on a headless box, and headless is where it gets
   tested.
3. **Re-running is normal and must be safe.** The realistic sequence is install the
   original, play for a week, discover the upgrade pack, re-run. So `--install` is
   `--check` plus a write, not a separate code path. What exactly "re-running" means
   against a target that already exists is the open point in § 11.

## 3. Target layout — ruled: option B, explicit layers

*Ruled 2026-11-04: B.* The installed tree is `data/original/` + `data/updated/`,
and `world.c` learns both names. The owner's reasoning: explicit layers are easier
to keep idempotent, easier to extend with a further layer later, and obviously
organised to a human who opens the directory.

The formulation that stops this becoming two layouts: the content root becomes an
**ordered list of roots**, not a single path.

```
data/updated/     <- patch layer, wins
data/original/    <- release layer
```

and for the drop-in case, which stays required:

```
./updated/        <- patch layer, wins
./                <- release layer
```

Layer order is fixed (patch over release) and within one root the existing
`MAPG` / `mapg` / `mapx` try-order is untouched. Drop-in is then the degenerate case
of the same mechanism rather than a special path, which is what makes "both work" a
claim the gate can test instead of a hope. Note the trap to avoid: the fan patch's
own `RENGAL.BAT` *renames* `mapx` to `mapg` in place. The installer copies verbatim
and lets precedence choose; it never runs that rename.

What was on the table, kept as the record of why B and not A:

| option | shape | cost |
| --- | --- | --- |
| A. engine-native | `data/` holds the release files, `data/updated/` holds the patch files, verbatim | zero engine change, but the directory name is a magic string the installer must keep in step with `world.c`, and it says nothing to a human opening the folder |
| **B. explicit layers** *(chosen)* | `data/original/`, `data/updated/`, and `world.c` learns both names | one engine change now, and the layout states what it means instead of encoding it in a name |

A was the smaller change and B was rejected only on the strength of the reasoning in
the first paragraph: A's zero-cost claim is real for the first version and false for
every version after it, because every later layer has to be squeezed into a naming
convention rather than added to a list.

Three parts, plus the config file:

* `data/` — the game content. **Immutable after install.** D6 below used to keep
  sector write-back here and to let an installed tree drift under play; that ruling
  is superseded by `docs/save-scoped-worlds.md`, and every write now goes into a save
  slot. `--install --check` treats content drift as a defect, not a state.
* `saves/` — one directory per save game: the `PLAYER` record and only the sector
  files that save has changed (`saves/PLAYER/mapx20`). The original rewrites
  `player` in place, which is why `AGENTS.md` says never run the game in place, and
  a DOS run and an engine run still do not fight over one file. The slot is created
  by the engine on first write, never seeded by the installer.
* `themes/` — built by `core/themes.c` in the same `--install` run (§ 10). The
  Python builder that used to do this was deleted rather than kept beside it, so
  there is one definition of the format and it is the one in the shipped binary.
* `config.ini` in the game directory, naming the three.

**D6 — superseded 2026-11-05 by `docs/save-scoped-worlds.md`.** *measured, and the
measurement is what overturned it* — this ruled that sector write-back stayed in the
content directory and that `--install --check` had to tolerate play drift. The drift
was not cosmetic: `u2_world_store` writes the *stamped* grid, `map_base` loads its
pristine terrain from that same file, and a baked creature byte became terrain. The
owner's `mapx20` differed from the release at exactly two bytes, both stamps. Write-back
now targets `saves/<slot>/`, content is immutable, and the gate went with it. What
survives here is the writability-at-store-time rule: a read-only content directory is
still playable, because it is no longer the write target.

## 4. Config file and precedence — ruled: `config.ini`, global

*Ruled 2026-11-04.* The global file is `config.ini`, and it keeps the `key = value`
grammar `THEME.INI` already uses, so there is one parser and one set of refusals
rather than two.

**`THEME.INI` stays per-theme and is not folded in.** The two files have different
subjects, different writers and different lifetimes:

| file | subject | written by | read when |
| --- | --- | --- | --- |
| `config.ini` | where things are, what the user prefers | the installer, and the user | every startup |
| `<theme>/THEME.INI` | what the art in *that directory* is: tile size, bpp, bank layout, palette, scope, `MAP.TSV` | the installer (`core/themes.c`) | when that theme is selected |

Three reasons the per-theme file cannot move into a global one:

* the engine resolves a theme by name from the themes directory and must be able to
  do it with no global file present at all
* the metadata travels with the art — copy a theme directory in and it works,
  whereas a global config would need an edit per theme
* the builder emits it as part of emitting a theme and `--verify` re-derives it to
  catch drift. If installing a theme mutated one shared file, then installing two
  themes is two writes to the same file, and the idempotence story gets worse
  rather than better

What does go in `config.ini` is a `[themes]` section for the settings side: the
themes directory, and anything else that is a preference rather than a fact about
the art.

**The installer does not record an active theme** (owner ruling, 2026-11-13; this
reverses the 2026-11-06 behaviour described below). It writes `dir = themes` when it
built any themes, plus a commented `# theme = 10` example, and no active `theme =`
line. The reason: a config line is an *explicit* choice as far as art auto-detection
is concerned, so a recorded theme would freeze the pick order and override a later
`Ctrl-T` or a changed install. The engine auto-picks at launch instead — `10`, then
`default`, then `fmtowns`, then CGA — so a tree installed here and a tree assembled
by hand agree without a pinned line. `U2_INSTALL_THEMES` and
`U2_EGA_THEMES_DIR_NAME` are tied by a `_Static_assert` in `core/install.c` rather
than by two files agreeing.

> Historical (2026-11-06, superseded): the installer used to record the theme it
> thought the engine would pick (`fmtowns` if derived, else `default`). That froze
> the choice into config.ini, which is why it was removed.

Precedence, and this is the part that must be explicit or the feature rots:

```
command line  >  config.ini  >  nothing
```

* An explicit path from either source is **authoritative with no fall-through**.
  That is the `[LOCKED]` rule from `docs/ega-graphics-plan.md` § 2 for
  `--ega-theme-dir`, and a config file is a second place a path can come from, so
  it inherits the rule: a typo in the ini is reported, never papered over by
  finding the theme somewhere else.
* **The shipped artifact stops carrying dev fallbacks.** `references/u2upgrade`
  stays a candidate for a developer checkout (driven by the ini or the flags) and
  disappears from the release build, which is what `U2_ARTIFACT=release` is for.
* A `config.ini` naming a directory that does not exist is an error at startup, not
  a silent default.

**What a relative path in `config.ini` is relative to.** The precedence rule says
nothing about this, and the two candidate answers differ in whether an installed
tree works when the player launches it from anywhere. Ruled in implementation:
**relative to the file that names it**, not the cwd. `--config
/games/ultima2/config.ini` from a home directory would otherwise resolve
`data = data` to `~/data` and the file would describe a layout it does not have.
An absolute value is taken exactly as written, and a plain `config.ini` in the
launch directory resolves against `.`, which is the original's default-drive model
— so the drop-in case is untouched. `saves` is the one entry allowed to name a
directory that does not exist yet: it is output, and `save.c` creates it on first
write the way the original would have.

**`[install]` — what `--install` was given, recorded so a re-run needs no arguments.**
*added 2026-11-05, owner request: "the install should put all the install paths in
config.ini so if you keep it alongside your .exe it can reinstall without asking
questions."* The installer emits the sources it used alongside the sections it already
wrote:

```ini
[install]
to = .                    # this directory; the file lives in the target, so it moves with it
original = /abs/path/to/release
updated = /abs/path/to/release/updated     # only when it was given explicitly
themes = /abs/path/to/U2Upgrade            # themes2..4 for further sources
fmtowns = /abs/path/to/disc
```

Four things follow from that, and each is a rule rather than an implication:

* **Discovery.** `--install` with no `--to` looks for `config.ini` in the directory it is
  run from, then beside the running executable (resolved through PATH and symlinks).
  That is what makes "keep the binary in the game folder" the whole configuration. A
  tree with no config has no opinion, which is not an error: the installer asks, or
  refuses under `--no-prompt`.
* **Precedence does not change.** flag > config.ini > prompt, the same rule every other
  path in the engine follows. A recorded value never overrides a flag.
* **A recorded source that has moved is reported and left unset**, so the prompt asks for
  it or a script refuses. Silently dropping the theme source used last time would build
  a different tree under the same manifest, which is the one outcome an installer must
  never produce.
* **The section is exempt from the startup existence check**, and for a different reason
  than `saves`. These paths name the player's *media* — a stick, a download folder — and
  a game you can play must not depend on the disc being mounted. Only a re-install
  depends on it, and that is where the missing path gets reported. The keys are still
  known-key checked and duplicate-checked like every other section, so a typo in
  `[install]` is a refusal at startup rather than a mystery later.

`to` is recorded as `.` on purpose: the config file is inside the target, so the game
directory can be moved wholesale and the record moves with it. The sources are absolute
because they live outside it and a relative path would mean nothing after a move.

Two refusals worth naming, because both are the difference between a config that
can lie and one that cannot. An **unknown key is a refusal**, not an ignore: a key
nobody reads is indistinguishable from a typo, and a silently ignored typo is a
player editing the wrong line forever. And a **value longer than the parser's cap
is a refusal, not a truncation** — a halved path is a path that points somewhere
else. The cap is 255 characters, raised from the 63 that `THEME.INI` needed, since
a real filesystem path does not fit in 63.

**Where the code lives.** *ruled* — `--install` ships in both builds and is purely
text/terminal, needing no SDL, which is what makes it testable headless and in CI.
The consequence is structural: the installer goes in `engine/src/core/`, not
`platform/`, and must not reach for a renderer at all.

## 5. Inputs it must recognise

Discovery is **by content, never by name** — the lesson from the theme builder,
where keying on `EGATHEME.*` produced a tree with no themes in it.

| input | how it is recognised | note |
| --- | --- | --- |
| original DOS release | presence of `ultimaii.exe` plus `mapx*`/`monx*`/`tlkx*` | an IA zip, a CD rip, or a directory someone has already unpacked |
| Galaxy patch | a directory holding `MAPG*`/`MONG*`/`TLKG*` | often named `updated/`, but the name is not the evidence |
| **a tree where the patch already ran** | `mapg*` sitting beside `monx*` at the root | this is an *input*, not a mess to clean up — see § 8 |
| fan art package | a directory containing a tile atlas, at any name or depth | *measured* — `references/up/Files/U2Upgrade` and `references/u2upgrade` differ in exactly three files (`U2.CFG`, `u2up.exe`, `u2upw.exe`); all art is byte-identical, so either is a valid source and the version drift is in the patcher's own binaries |
| FM Towns disc | the `.img` with `GRAPH/*.TIF` inside | `--fmtowns-media-dir`; produces a derived v2 theme. The mapping it needs is compiled into the binary, so this is a player feature, not a developer-box one |
| themes | reshaped into the layout `docs/theme-format.md` specifies | the vendor's directory names and depth are not our layout |

The patcher is **linked, never bundled**:
`https://github.com/Fenyx4/UltimaPatcher/releases/download/1.70/UltimaPatcher.zip`.
That is also the only posture compatible with `AGENTS.md` — the art is Origin's and
the patcher is a third party's, so the installer asks the player to supply both and
writes only into the player's own directory.

## 6. Invariants

These are the rules that make the difference between a tool that feels solid and
one that feels magical, and every one of them is here because of something that
went wrong in this project.

1. **Never write into a source.** Hash the source tree before and after, and prove
   it is untouched. The installer and the theme builder both refuse to write into
   `original/` or `references/`, and the gate proves it by comparing the sources
   byte-for-byte before and after an install.
2. **Byte-identical copies, no transforms.** No renames, no case-fixing, no
   `RENGAL.BAT`. A transformation is a claim, and claims need evidence.
3. **Idempotent.** A second run changes nothing and says so — which needs the § 11
   open point settled, because "a second run" and "the target must be empty" do not
   yet agree. Since the save-scoped layout this is a stronger claim than it was: play
   no longer changes the tree at all, so a second run really does change nothing.
4. **Location-independent.** Everything resolves from argv and the ini, never from
   where the binary sits. *measured* — deriving paths from the script's own
   location is exactly what emitted `themes/ultima2/EGATHEME.C64/` when the theme
   builder was run beside the game on another machine, and the engine then refused
   every theme by name while the builder reported success.
5. **Verify by re-derivation, not by "the copy returned 0".** Build, then
   re-derive what the tree should contain and diff it. A gate that cannot go red in
   the place it enforces is worse than no gate.
6. **Every state change is reported.** The owner's rule for the game applies to the
   tool: no silent mutation.

## 7. The gate

Greenfield is easy to test, and a tmp-dir run is only a gate if it asserts the
**result** rather than the run. The fixture sources already exist on a dev box:
`references/ultima2/` (pristine release), `references/up/Files/U2Upgrade` (the
patcher's own tree), `references/fmtowns/` (the disc).

Six assertions, each written red-first per `AGENTS.md` § 3:

1. the installed tree is **exactly** the expected file set — no strays, nothing
   missing. File set and sizes, and since the save-scoped layout there is no drift
   carve-out at all: a content file that differs from its manifest row is a defect
2. every copied file is byte-identical to its source
3. the engine, pointed at the installed tree with **no other flags**, loads Earth
   *and* at least one outer planet — this is the assertion that proves the patch
   layer did something, and the only one that catches "copied the files, planets
   still missing"
4. re-running the install is a no-op, once § 11 settles what re-running into a tree
   we already built means
5. `--install --check` goes red on a deliberately broken tree (a missing `TLKG81`,
   a truncated `EGATILES`, a stale theme)
6. the source directory is byte-for-byte untouched

Plus one structural rule about how it is run: **the binary is invoked from a third
directory** — neither the install target nor a source — so any cwd or self-path
assumption fails loudly instead of working by accident on the developer's machine.

## 8. Cleanup: reorganise into a new directory, never touch the old one

*Ruled 2026-11-04, and it is a better answer than the one this section started
with.* A player who has been running the game under DOSBox or a similar emulator may
already have a directory holding some or all of this content. The first draft
treated "clean up an existing install" as a destructive mode with a confirmation
flow. The ruling removes the category: **the installer does not clean anything up,
it builds a new tree somewhere else.**

* `--to DIR` accepts an **empty directory, or one that does not exist and is
  created**. Anything else is refused. There is no merge, no overwrite and no
  deletion, so the mode has no destructive path to get wrong.
* The existing emulator directory is then just another **source** — § 5 recognises a
  tree where the patch already ran, which is the common case in the wild — and the
  old install keeps working under DOSBox until the player decides otherwise.
  Deleting it is their call, made later, with a file manager and a backup.
* This is why detection and cleanup were the same code path in the first draft. A
  directory where `RENGAL.BAT` already ran has `mapg` where the release had `mapx`,
  and a tool that classifies by expected-name-then-delete reads that as junk when it
  is the Galaxy patch already applied. Under this ruling the misclassification is no
  longer dangerous, which is the argument for it.
* `AGENTS.md` has a standing incident about an agent deleting irreplaceable files it
  was confident about. A mode whose worst failure is "refused: directory not empty"
  is the shape this project should prefer on principle.

One case survives the ruling and needs an explicit answer: if the source directory
holds the player's own `player` save, adopting it is a **migration**, not a cleanup.
The installer must never silently overwrite a player's record. Since the save-scoped
layout it does not install a `player` into `saves/` at all — a new game starts with no
slot — and the migration of `docs/save-scoped-worlds.md` § 4 is the one place that
moves an existing record, printing every move and promoting anything it cannot prove is
the release blank.

## 9. Self-update: ruled out

*Ruled 2026-11-04: out.* The reasoning stays below, because it is the record of why,
and because the cheap half is still a reasonable thing to want someday.

*measured* — the pieces already exist. The binary stamps `U2_VERSION`,
`U2_BUILD_TARGET`, `U2_SDL_IMPL`, `U2_SDL_LINKAGE` and `U2_ARTIFACT`, prints them
under `--version`, and `release-builds.yml` publishes assets plus a
`SHA256SUMS.txt` on a `v*` tag, and refuses to ship a dev-shaped build.

Splitting it matters because the two halves have completely different costs:

* **`--check-update` (cheap, no new trust).** Compare the stamped version against a
  published list over HTTPS, print "a newer release exists at URL", download
  nothing. This builds the channel plumbing and the version comparison under test
  with none of the risk.
* **Self-replacement (expensive, and not now).** Three reasons, in order:
  1. A checksum fetched from the same origin as the binary proves nothing. If the
     channel is compromised the sums are too. The releases are **not signed
     today**, so the first requirement is signing them, not writing the downloader.
  2. It undermines a guarantee the release workflow deliberately enforces: the
     binary identifies itself as the release build precisely so that "v0.0.7 does
     not work" is answerable. A self-mutating binary breaks that unless it records
     what it updated from and to and keeps the previous binary rather than
     overwriting it.
  3. Windows cannot overwrite a running executable — rename-then-replace — and the
     release workflow deliberately never executes the Windows binary, so that path
     would ship untested.

## 10. What the theme work leaves behind

**Done and gated** (see `docs/theme-format.md` for the contract): the v1 and v2
loaders, content-based theme discovery, the reshaped `default/` theme, `--ega`
compositing at 640×400 with the doubled-dump ruling, the theme palette driving both
the renderer and the text ink, `engine/tests/theme_v2_check.c` with 39 checks
including six refusals, and the theme builder itself — `core/themes.c`, reached
from `--install --themes-dir`, which replaced `tools/build_themes.py` and that tool
has been deleted.

The replacement was verified before the deletion rather than after: the C builder and
the Python builder were run over the same real media and their trees compared. The
file sets came out identical and every file byte-identical except
`default/THEME.INI`, whose only difference is the provenance line naming the new
writer. That check found a real divergence on the way — the C builder keyed root
picture dedup on the filename and the Python one keyed it on the picture *base*, so
`PICDNG.EGA` and `PICDNG-E` both landed at the themes root. The Python behaviour was
the correct contract (one picture per base, first source wins) and is now pinned by
a test.

**Inherited by the installer** — these were theme-handover items and they are now
this document's problem:

| item | where it lands |
| --- | --- |
| asking the player for the art package | § 5, `--themes-dir`, repeatable — **built** |
| the FM Towns disc as an optional source | § 5, `--fmtowns-media-dir` — **built** in `core/fmtowns.c`. The art is read from the player's disc and the mapping is compiled in (`engine/content/fmtowns_map.h`, generated from `disasm/fmtowns-crosswalk.tsv`), because a player has the disc and not the repository. |
| `themes/` being built rather than hand-copied | § 3, `core/themes.c` — **built** |
| the dev-checkout fallbacks going away in a release build | § 4 — **done**: `core/artifact.h` gates the `references/u2upgrade` candidates in both `tiles_ega.c` and `pic_ega.c`, so a release-stamped binary searches only paths it was told |

The § 3 phrase "shared rather than reimplemented" turned out not to be achievable
literally — the builder was Python and the installer is C — and the answer the owner
ruled was not to share a contract across two implementations but to delete one of
them. One builder, in the shipped binary, no Python on the target.

**Still engine work, not installer work** — unchanged from the handover's slice C,
and still held:

* animation (frame lists are already carried in `MAP.TSV`; needs a tick source and
  a game-tick-vs-wall-clock decision)
* the `PLAYER` bank, which needs a second mapping key, not more rows
* dungeon and tower art, and whether dungeon appearance comes from the map byte at
  all
* horse and boat directional variants; per-planet terrain beyond tile 17
* per-theme pictures — `EGATHEME.10` and `EGATHEME.C64` ship their own `PICDNG` and
  `PICSPA` and they genuinely differ, but `pic_ega.c` resolves pictures at the
  themes-dir top level only
* the `EGACOLOR` remap, decoded and understood, wired to nothing
* per-screen FM Towns palettes, unproven

## 11. Decisions

Ruled 2026-11-04, in the order they were asked:

| # | decision | ruling |
| --- | --- | --- |
| 1 | Layout | **B** — `data/original/` + `data/updated/`, with `./` + `./updated/` still working for the drop-in case (§ 3) |
| 2 | Config file | **`config.ini`**, global, same grammar as `THEME.INI`; `THEME.INI` stays per-theme; a `[themes]` section carries the settings (§ 4) |
| 3 | Which builds | **both**, and `--install` is purely terminal with no SDL, so it runs headless and in CI; the code lives in `engine/src/core/` (§ 4) |
| 4 | FM Towns disc | deferred at the time; **since built** as `--fmtowns-media-dir` (see § 10) — the derivation was ported to C rather than left in the deleted Python tool |
| 5 | Self-update | **out** (§ 9) |
| D6 | Sector write-back | **superseded**: write-back moved to `saves/<slot>/` and content became immutable — see `docs/save-scoped-worlds.md` and § 3 |
| D7 | `--to` target | **empty or nonexistent only** (§ 8), with D7a below governing re-runs |
| D7a | Re-run into our own tree | **allowed via an install manifest** — empty installs, manifested completes, foreign non-empty refuses (§ 11) |
| D8 | Shared filenames | **the patch layer wins** when the bytes differ, and keeps the release's spelling of the name (§ 12) |
| D9 | Finding the Galaxy patch | **auto-detected at `<original-dir>/updated`** when `--updated-dir` is not given (§ 12) |
| D10 | `.pat` deltas | **detected and reported, not applied** for now; applying them is deferred (§ 12.4) |
| D7b | Target holding only the engine | **counts as empty**, matched by device and inode (§ 13) |
| D11 | Prompts | **four questions, flag > prompt > nothing**, pre-flight is the dry run, EOF refuses (§ 13) |

**D7a — re-running into a tree we built is allowed, via a manifest.** *Ruled
2026-11-04.* D7 on its own contradicted § 2 rule 3: the realistic flow is install
the release, find the Galaxy patch next week, and add it to the same tree, and a
blanket "target must be empty" refuses exactly that. The resolution is that the
installer writes a manifest of what it emitted, the same device
`themes/_BUILD.tsv` already uses for the theme builder, and a target is then one of:

| target state | behaviour |
| --- | --- |
| empty, or nonexistent and created | install |
| carries a valid install manifest | **complete** — add the layers that are missing, re-verify, report what was added |
| non-empty with no manifest | refuse, and say why. This is the case D7 exists to protect |

So D7's protection survives intact — a directory the installer did not build is never
written to — while idempotence stays testable, because a second run against our own
tree is a real operation with a verifiable no-op result rather than an error path.
It also keeps the saves in place, which the rejected alternative (a fresh `--to` per
run) would have thrown away or forced out of the game directory.

## 12. The patch layer, and three rulings about where it comes from

Ruled 2026-11-05, prompted by the owner asking whether the upgrade pack contains
Moonstone Dragon's map patch, and whether the installer should look for the Galaxy
patch inside the release rather than asking for it separately.

### 12.1 Two names, one patch

The Galaxy map patch and the Moonstone Dragon map patch are the same thing. Both
halves of that are in our own tree, so this is `observed` and not folklore:

* `references/up/Files/U2Upgrade/u2up.ini` names the delta and makes it a
  prerequisite, not an option:
  `u2-map.pat=Moonstone Dragon's Map Patch (1997)`, and separately
  `base=u2-map.pat`, meaning the EGA upgrade patches are computed *relative to* it.
  The full chain is `releases=u2-u16.pat,u2-uc.pat,u2-map.pat,u2-gog.pat`.
* `references/ultima2/updated/GCHANGES.TXT` is that patch's own readme, headed
  *"Ultima II -- The Fixed Version, Patch by Mike Marcelais, The Moonstone Dragon,
  April 25, 1996"*, and it documents the mechanism: three `mov` instructions
  overwrite the memory where the MON/MAP/TLK filename is built, so galactic maps
  load as `MAPG??` and player maps as `MAPX??` and stop colliding. That is the same
  `G`-versus-`X` disambiguation § 3 of `docs/planet-maps-galaxy-patch.md` derives
  from our own bytes, arrived at independently by the person who wrote the patch.

So `updated/` in the Internet Archive pack is the **already-applied result**, and
`u2-map.pat` in the upgrade pack is **the same patch as a delta**. One is a
destination, the other is a diff.

**One recalled detail did not survive the readme.** The owner remembered the patch
fixing "the greenland dungeon". GCHANGES.TXT names no Greenland. What it documents
is `MAPX15` being corrupt — the last 260 bytes hold the city data belonging to
`MAPX11` instead of the dungeon for sector 15 -- and a dungeon entrance in **Pangea**
that had been removed because of that corruption and which the patch restores. The
shape of the recollection (galaxy maps plus a dungeon fix) is right; the place name
is not, and the shipped readme outranks the memory here.

### 12.2 What actually differs, and the number I got wrong the first time

Comparing the release against `updated/` case-insensitively: **59 shared filenames,
11 of which differ.**

| file | bytes differing | note |
| --- | --- | --- |
| `mapx15` | 260, starting at byte 3837 of 4224 | exactly the corruption GCHANGES.TXT describes |
| `mapx10` | 1, at byte 544 | |
| `tlkx03` `tlkx11` `tlkx21` `tlkx22` `tlkx23` `tlkx31` `tlkx32` `tlkx33` `tlkx41` | same size, content differs | dialogue, nine sectors |

Two things worth recording about the measurement itself.

First, **an earlier pass over this reported "exactly two differ" and that was wrong.**
The shell loop tested `references/ultima2/TLKX03`, which does not exist, because the
release spells those files lowercase and `updated/` spells them uppercase; the `-f`
test failed, the file was skipped, and the loop reported a confident, plausible,
wrong count. This is the failure mode `AGENTS.md` § 5 warns about in the section on
deriving rather than eyeballing, and it is worth noting that it recurred here in a
different guise: a case-sensitive existence check silently dropping half the inputs.

Second, **that casing split is itself a bug source.** On a case-sensitive filesystem
`data/original/tlkx03` and `data/original/TLKX03` are two files, and the engine finds
one of them. An install that copied both would leave the corrupt one in play depending
on which name the engine happened to ask for.

### 12.3 D8 and D9, and what they fixed

**D8 -- the patch layer wins a shared filename.** The rule was "first source wins"
with the release scanned first, which meant the corrupt release `mapx15` beat the
copy whose entire purpose is to fix it. The installer was re-introducing a 1996 bug.
Now a patch source may replace a planned release-layer file when the bytes differ,
the report says `patch ... replacing a different copy (D8)`, and the summary counts
them separately from shadows. The destination keeps the **release's** spelling of the
name, so the casing split in § 12.2 collapses to one file instead of two.

Gated by `install_check` ("a file in both sources is taken from the patch, not the
release (D8)", plus the two casing assertions) and by the control
`patch-loses-the-shadow`, which flips the `may_override` argument and must go red.

**D9 -- the Galaxy patch is auto-detected at `<original-dir>/updated`.** It ships
inside the release directory, so asking for it as a separate flag asks the player to
know the pack's internal layout. An explicit `--updated-dir` still wins, and the
report says when the layer was found on its own.

**A caveat D8 cannot resolve, found by running it against a second copy of the media.**
On the test machine the installer reported patching over six sector files, not two:
as well as `mapx10` and `mapx15` it took `mapx20`, `mapx30`, `monx20` and `monx30`.
Those four are not fixes. The pristine `updated/` copies there are byte-identical to
this repository's reference copies, and it was the *release* copies that differed,
because the original game had been played in that directory and rewrites its own
sector maps as it goes (D6). So "the release copy differs from the patch copy" has
two causes and the bytes alone cannot tell them apart: the release copy was either
corrupt or played-in.

The behaviour is still the right one either way, because the patch layer is pristine
by construction, so preferring it is correct whether the release copy was broken or
mid-game. What was wrong was the report, which said *patch* about an event that might
have been someone's save state being reverted. Sector files now say so:

```
  patch   mapx15 taken from the patch source; the release copy differed, which
          is either a fix or play drift (D6)
```

Non-sector files, where only a fix is possible, keep the plain wording. A player who
wants their played-in world back has it in the save slot: `saves/<slot>/` holds the
record and every sector that save changed, and the installer's migration (§ 4 of
`docs/save-scoped-worlds.md`) moves a played-in content file there rather than
overwriting it.

### 12.4 D10 -- `.pat` deltas are reported, not applied

If a player has a pristine release and the upgrade pack but no `updated/`, the map
fixes exist only as `u2-map.pat`, which the installer cannot apply. Its header is
`PZ1`/`FZ1` with embedded filenames, an LZH-family archive rather than a byte delta,
so supporting it is a real piece of work and not a few lines.

The ruling is that the installer **says so out loud**: a `.pat` in a source directory
is reported as a patch delta the installer does not apply, pointing at this section,
instead of being silently counted as "not game content". Silence was the worst of the
three possible answers, because the player would hold the fix, skip it, and be told
nothing.

### 12.5 Deferred, in rough order of value

1. **Apply `.pat` deltas.** Read the `PZ1`/`FZ1` container, extract, and let
   `--install` take a pristine release plus `u2-map.pat` as a complete input. This
   is what `u2up.exe` does, and it is the only way to serve a player who has the
   upgrade pack and a 1989 release but no `updated/` directory.
2. **Accept `.zip` downloads directly.** Every one of these sources — the Internet
   Archive pack, the upgrade pack, the FM Towns image — reaches players as an
   archive, and today `--install` needs them unpacked first. Accepting a `.zip` path
   for `--original-dir`, `--updated-dir` and `--themes-dir` would remove a step that
   currently has to be explained, and would let the installer read a CD image out of
   an archive too. Needs a zip reader in C (stored and deflate), and the same rule as
   everywhere else here: read from the archive, never re-encode what comes out.
3. **Verify against the patch's own byte templates.** GCHANGES.TXT carries the exact
   before/after byte sequences for the `ultimaii.exe` edits. A check that our
   `original/ultimaii.exe` matches one of those templates would tell us which release
   level the Internet Archive pack actually is, which § 0 of this document currently
   concedes we do not know.

## 13. The prompt layer — built 2026-11-05

Precedence, stated once because every question depends on it: **flag > prompt >
nothing**. Anything a flag already answered is never asked. That is what keeps the
rule in § 2 true in the only way that matters — a fully-specified command line
behaves identically with and without a terminal, so a script's behaviour cannot
change because someone ran it by hand.

Four questions, in the order they matter:

| question | default | required |
| --- | --- | --- |
| Install into which directory? | `.` | yes, via the default |
| Where is your original Ultima II (DOS) directory? | — | yes |
| Any fan art pack to install themes from? | none | no |
| An FM Towns disc image or directory? | none | no |

The Galaxy patch is **not** a question. D9 finds `<original>/updated` on its own,
and asking would require the player to know the pack's internal layout — the same
reason `--updated-dir` became optional.

Three properties worth naming, because each is a decision rather than a detail:

* **A wrong path is re-asked, not guessed past.** Same rule as `--ega-theme` for an
  explicit directory: an answer is not a hint. Five attempts, then a refusal.
* **The pre-flight is the dry run**, not a second summary written for the prompt
  path. One code path produces the plan, so the confirmation cannot disagree with
  what gets written.
* **EOF is an error, not an empty answer.** A required question that gets no answer
  refuses. This is the difference between a script failing loudly and a script
  hanging, and `install_check` asserts it by pointing stdin at `/dev/null` and
  requiring a refusal.

Prompts are on only when `isatty(stdin)` and `--no-prompt` was not given.

### D7b — a target may hold the running binary

*Ruled 2026-11-05, as a consequence of the default target being `.`.* D7 says the
target must be empty, nonexistent, or carry our manifest. But the shape people
actually use is: make a folder, drop the engine in it, run `--install`. Under a
literal D7 the first thing that player meets is a refusal to write into the only
directory they have.

So a directory whose **only** content is the running executable counts as empty.
Matched by device and inode, not by name — a same-named file elsewhere does not
qualify — and a subdirectory disqualifies, because a subdirectory is content. A
foreign refusal now also names the alternative: a directory that already holds a
downloaded game can be played in place with `--data-dir`, which is the case D7
protects and the player can be told about rather than merely blocked.


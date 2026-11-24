# Dungeon stroke art: what the seven stamps actually look like

**What this is.** The dungeon has no tile-art sprites. The shipped file `monsters` holds one
*shape* per dungeon stamp, and this note records what those seven shapes look like and what that
is worth as evidence for the stamp → name mapping.

> **CLEANROOM: this document is trace, not intent.** It describes measured shapes from the
> original's art data. Anyone drawing substitute art must not read it, exactly as with
> `docs/tile-art-evidence.md` and `tools/tile_rules.py`. The only permitted input for a redraw is
> `docs/tile-art-spec.md`. See `docs/cleanroom-procedure.md`.

**What was already decoded, and is not claimed here.** `asm/07F3` identifies the file (loaded
verbatim to `DS:2900`, one `0x800` block, so the trailing 128 bytes of the 2176-byte file are
never transferred). `asm/68ED_dungeon_sprite_pass` has carried the record framing since
2026-10-29 — **two bytes**, not three: `b0` is `x` and `x == 0` ends the list, `b1 & 0x1F` is `y`,
and `b1 >> 5` is a pattern index read from the *same* byte with no advance between. That header
also already states that plotting the file that way "yields seven distinct creature silhouettes
and an empty block 0", that blocks 1..7 each hold one full-detail creature, and that block 0's
tail holds the depth ladder `cs:697E = 00 00 80 C0 E0 F0 F8 FC` indexes into — 24 records at
`0x080`, 6 at `0x0C0`, 4 at `0x0E0`, 2 at `0x0F0`, 1 at `0x0F8` — described there as "a
deliberately typeless simplification series", so the far arm dropping the type term is the design
and not a bug. **All of that predates this note.** What was missing is the one thing the header
asserts but nobody had looked at: what the seven silhouettes *are*.

**Tool.** `tools/dungeon_strokes.py` decodes and renders the file. `--selftest` runs eight
controls with no game file needed; a missing `original/monsters` exits 77 ("cannot run"), because
the game files are gitignored by policy. Rendered PNGs are derived from copyrighted art and go
under `/var/tmp`, never into git.

## The seven shapes

Record counts rise monotonically with the stamp — 64, 66, 75, 86, 87, 93, 127 — so the later
stamps are drawn with more detail, which is at least consistent with the floor-scaled strength
ladder in `docs/monster-spawn.md` § 9.

| stamp | records | what the plot shows | third-party name at this position | grade after looking |
| --- | --- | --- | --- | --- |
| 1 | 64 | Upright humanoid. Head with two small protrusions, a bright horizontal band at the shoulders, arms outstretched to both sides, two legs with feet. | Orc | **consistent, not discriminating** — an orc and a man would plot identically at this resolution |
| 2 | 66 | A legless rounded mass, widest through the middle, tapering top and bottom. Drawn **entirely in one half-on dither pattern** (`p4` on all 66 records — the only single-pattern block), so the whole figure reads as translucent. | Ghost | **corroborated twice** — the proven torch-blown-out special, and a shape with no limbs and a uniform dither |
| 3 | 75 | A domed, bell-like body with **many legs splaying outward and downward** on both sides. | Carrion creeper | **corroborated** — the only many-legged thing in the candidate list, and nothing else in the list it could be |
| 4 | 86 | A long, low, **sinuous S-curve with no limbs at all**, a thicker head end carrying two bright pixels. | Viper | **corroborated** — it is a serpent; there is no other serpent to choose |
| 5 | 87 | A low domed body with a bright central band and small feet, plus four detached diamond marks floating at the corners. | Gremlin | **proven** — `asm/12DB` keys on `cmp $0x5` and prints `A GREMLIN STOLE SOME FOOD!` |
| 6 | 93 | Broad-shouldered upright humanoid, two horn-like protrusions above the head, arms at the sides, bright feet. | **Minotaur** | **inferred, differential** — see *The special asymmetry* below. The shape alone cannot choose between Daemon and Minotaur; the specials can, and they choose Minotaur |
| 7 | 127 | The largest figure by a wide margin: an upright torso with a **wide fan of wings spreading from the shoulders** to both sides, head at top centre, two legs with feet. | Balron | **corroborated twice** — the proven sleep special, and a winged demon |

**What this is worth.** Before looking, four of the seven names rested on nothing but their position
in a third-party level-ordered list. After looking, **stamps 3 and 4 are hard to reconcile with any
reordering** — a many-legged dome and a limbless serpent are not interchangeable with anything else
on that list — and **stamp 7 agrees twice over**, a winged demon whose proven special is the sleep
the guide attributes to a balron. Stamp 2's uniform half-dither is a small independent touch: a
figure drawn entirely in a translucent pattern is a plausible way to draw a ghost with four
colours and no alpha.

**What it is not.** Still no byte-level pairing for stamps 1, 3, 4, 6 — no string, no table, nothing
in the executable that says "stamp 4 means Viper". The shapes are consistent evidence, and for 3
and 4 they are consistent in a way that leaves no alternative *within the candidate set*, but the
candidate set is third-party. Stamp 1 remains the weakest row in the table: a generic humanoid at
this resolution identifies nothing, and it is the one row that would survive any reordering, which
is exactly why it proves the least.

**Caveat on the rendering.** `tools/dungeon_strokes.py` is a reimplementation of
`dg_sprite_page` in `engine/src/game/dungeon.c`, which is itself the engine's transcription of
`asm/68ED` and `asm/68A3`. The *geometry* — which cells are painted and which columns within them —
follows those rules directly. The colours are the tool's author's choice and mean nothing; the
original's palette mapping is not claimed here.

## The block that is not empty, and the tail nobody reads

Two small things worth writing down because both are easy to get wrong:

* **Block 0 terminates at offset 0 and is not empty.** Its first byte is `0x00`, so a parser that
  starts at base 0 stops immediately and reports nothing — while the same bytes hold the depth
  ladder at `0x080..0x0FF`, which `asm/68ED` reaches from a different base. "Block 0 is empty" is
  a parser artefact, and `asm/68ED` is right to describe it as the ladder's home rather than as
  blank space. The ladder's record counts shrink monotonically (24, 6, 4, 2, 1): the distant
  creatures are deliberately typeless, which is why a far-off monster never tells you what it is.
* **The last 128 bytes are never transferred, and they are not padding.** `asm/07F3` reads
  `CX = 0x800` from a 2176-byte file, so bytes `2048..2175` never reach memory. They are a
  **complete, properly terminated record list**: 63 records in 126 bytes, closed by `00 00` at the
  very end of the file. Measured against block 7 record by record: **62 of the 63 are identical, in
  order, to records 65..126 of block 7**, and the remaining one differs from block 7's record 64 by
  a single coordinate — `(x=26, y=15, p=1)` in the tail against `(x=32, y=15, p=1)` in block 7. So
  the tail is **a near-copy of the second half of the stamp-7 figure, with one stroke moved**, not
  slack, not an eighth creature, and not a deliberate simplification. Block 7 itself is complete and
  self-terminated: 127 records × 2 bytes + terminator = exactly 256, filling its block.

  **Purpose: dead data, and the honest answer is that the bytes cannot say more.** Two readings fit
  everything observed. Either it is an **authoring artefact** — a stale copy left behind when the
  stamp-7 figure was edited, which is what a one-record difference looks like — or the file **grew
  past the read length the loader was written for**, since 2048 is exactly eight blocks of 256 and
  the extra chunk is exactly half a block of the previous block's tail. Both predict a near-copy with
  a one-record difference sitting past the boundary nothing reads, and nothing in the loaded 2048
  bytes points at it. Grade: **observed** for the record list, its termination and the one-record
  difference; **inferred** for either intent. `tools/dungeon_strokes.py` deliberately does not render
  it, so nobody mistakes it for in-game art.

## No pictures in this file, deliberately

**Rendered plots are not committed here, and ASCII art was considered and rejected for the same
reason.** A plot of the strokes — PNG or ASCII — is not a *description* of the artwork, it is the
artwork in a lossier encoding. The test is recognisability, and it passes: the renders made from
this data were sparse, dithered and coarse, and stamp 7 was still recognisably a winged demon. The
project's ruled line on art (AGENTS, "the text/art line", 2026-10-28) is that art is never committed
and is substituted under the cleanroom, and the argument that a small or low-fidelity excerpt is too
slight to protect was already rejected for text: there is no category exemption.

There is also a second cost specific to this repository. **A committed picture would be a better
answer key than the prose above**, which is the exact thing the cleanroom wall exists to keep away
from anyone drawing substitutes. This document is already on the forbidden list in
`docs/tile-art-spec.md` § 2 for describing the shapes in words; putting the shapes themselves in the
repo would be building the wall and then setting the originals into it.

What is committed instead, and why that is enough: the **prose shape descriptions** above (which are
what actually carried the evidentiary argument — "the only limbless serpent on the list" does the
work, a picture adds nothing to it), and the **measurements** below. The renders live under
`/var/tmp` and are regenerated on demand from the player's own files by `tools/dungeon_strokes.py`,
which is the same arrangement as the tile viewer: the tool is committed, the derived originals are
not.

The sanctioned route to a committed *picture* of a dungeon creature is **generative, not
reproductive**: a procedural substitute that emits format-compatible stroke blocks from an intent
spec, never from these shapes. That is filed in `docs/outstanding.md` § 1, and it has a real
function rather than a documentary one — without the `monsters` file the engine currently draws no
dungeon creatures at all.

### Measurements

Structural, not visual: enough to argue from, not enough to see.

| stamp | records | x extent | y extent | pattern histogram |
| --- | --- | --- | --- | --- |
| 1 | 64 | 24..38 | 11..23 | `p1:59 p2:3 p5:2` |
| 2 | 66 | 28..38 | 11..22 | `p4:66` (single pattern — the whole figure is half-on) |
| 3 | 75 | 23..41 | 8..22 | `p1:3 p2:20 p3:38 p4:14` |
| 4 | 86 | 22..41 | 13..23 | `p0:4 p1:80 p2:2` |
| 5 | 87 | 24..41 | 10..23 | `p0:10 p1:45 p2:4 p3:6 p4:22` |
| 6 | 93 | 26..38 | 9..23 | `p0:4 p1:64 p3:23 p4:2` |
| 7 | 127 | 19..45 | 8..23 | `p0:11 p1:62 p2:4 p3:29 p4:21` |

Depth ladder inside block 0: 24 records at `0x080`, 6 at `0x0C0`, 4 at `0x0E0`, 2 at `0x0F0`,
1 at `0x0F8`, nothing at `0x0FC`. Never-transferred tail: 128 bytes, a complete 63-record list closed by `00 00`; 62 of its records are
identical and in order to records 65..126 of stamp 7, and the 63rd differs from stamp 7's record 64
by one x coordinate (26 vs 32).

### The special asymmetry settles stamp 6

**The dungeon gives a contact special to exactly three of the seven stamps, and the set is not
arbitrary.** `asm/12DB_dungeon_events` switches on the dungeon type and does something for three of
them and *nothing at all* for the other four: type 2 blows out the torch, type 5 steals food, type 7
prints `YOU FEEL A STRONG MAGIC!` and writes `paral_sleep`. That is the whole table.

**And Balron is the positive control that makes the absence meaningful.** The overworld Balron
(`0x3C`) puts you to sleep, resisted by GREEN IDOL; dungeon stamp 7 puts you to sleep. So when the
original carries a creature between domains, it carries the creature's signature effect with it.
That turns "stamp 6 has no special" from a curiosity into evidence: the overworld Daemon's entire
identity *is* leg paralysis (`0x34`, resisted by BOOTS), so a dungeon Daemon missing its paralysis
would be a broken copy of itself, while the Minotaur has no special in either domain and is
therefore fully consistent with a stamp that does nothing.

**So stamp 6 is Minotaur, not Daemon.** Grade **inferred** — it is an argument from a differential
with a positive control, not an observation, and it outranks the list-position hypothesis it replaces
without pretending to be proof. The same asymmetry leaves stamp 1 untouched: the Orc has no special
in either domain, so its pairing still rests on name and position alone, and no argument of this kind
can reach it.

A second consequence, and it is the one that matters for the record index: **Orc and Balron exist in
both domains with different stats**, which is ruled in `docs/plan-record-index.md` § 8 as D6 — one
creature, stats keyed by population. See also the sea serpent's placed-versus-spawned pair, which is
the same shape.

## The names are already shipped

**The engine binds these seven names today, and the enhanced renderer is hard-wired to them.**
`engine/src/game/dungeon_assets.c:306-312` loads `MON1..MON7` as `dg_spr_orc`, `dg_spr_ghost`,
`dg_spr_carrion_creeper`, `dg_spr_viper_emerald`, `dg_spr_gremlin`, `dg_spr_daemon_minotaur`,
`dg_spr_balron`, and `engine/src/game/dungeon3d.c:339` maps `stamp 1..7` onto `MON1..MON7`. The
sprites are generated from `concepts/dungeon2/`, which is our own concept art, so the naming is a
**decision already made and already drawn**, not an open question. The grades in the table above
describe the *evidence* behind that decision; they are not a queue.

The only live consequence is a risk note: **stamps 1 and 6 rest on list position alone**, so if a
capture ever reordered the set, those two are the art that would have to be redrawn. The concept
brief anticipated exactly this — "the art should stand on its own silhouette even if a later capture
reorders them" — which is the right posture for art built on a hypothesis.

**And the executable's own text names exactly one of the seven.** Searching every `db` string in
`asm/` for the candidate names returns only the gremlin, via `A GREMLIN STOLE SOME FOOD!` in
`asm/12DB_dungeon_events.asm`. Every apparent `ORC` hit is inside `TORCH` or `TORCHES`, which is
worth recording because it is exactly the false positive a naive grep produces: ten files "contain
ORC", none of them names an orc. So the one proven pairing in the whole set is the one the string
hands us, and the other six names are ours to defend or replace.

## Related

* `docs/concept-dungeon-brief.md` — the stamp → name hypothesis, the three proven contact specials,
  and the per-stamp concept-art briefs built from it.
* `docs/monster-spawn.md` § 9 — the dungeon encoding: `type = (rand & 7) + 1` with 7 rejected,
  `[slot+0x1B7] = slot>>1` = the floor, and the HP formula.
* `asm/68ED_dungeon_sprite_pass`, `asm/68A3_sprite_stroke`, `asm/07F3_dos_block_read` — the drawing
  routine, the stroke plotter, and the loader.
* `docs/cleanroom-procedure.md` — the wall this document sits on the wrong side of for an artist.

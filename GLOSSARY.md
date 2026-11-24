# Glossary

The domain language of this project. Architecture vocabulary — **module**, **interface**,
**depth**, **seam**, **adapter**, **leverage**, **locality** — is defined in the
`codebase-design` skill and is not repeated here.

This file was created on 2026-11-21 when the architecture review settled the first terms that
needed an owner. Add a term when it stops being a phrase and becomes a thing two people refer
to; do not pre-seed it.

## Sector residency

The state of a sector's data being loaded and stamped and therefore drawable and walkable.
Not "loaded": a sector whose map bytes are in memory but whose creatures have not been stamped
is not resident, and the difference has produced repeat bugs.

Making a sector resident is an ordered sequence — map, demo overlay, monster data, demo
overlay, spawn, clear spawn cells, dialogue, restamp, view mode, status, the player-facing
note — and the order is load-bearing. It currently has no module and three callers, which is
the defect [issue #14](https://github.com/sloppy-seconds/ultima2-rebuild/issues/14) exists to
close.

**Ruled 2026-11-21, built 2026-11-22.** The **DS mirror** (`engine/src/core/state.h`) is
authoritative for position, sector and view mode, and it is the *owner* rather than a mirror of
something else: a movement site writes `ctx->state.sector_x` and every reader reads the same
byte. The overworld holds no second copy, so there is no publish step and no ordering between
two copies to get wrong.

## Quiescent point

The one moment in a turn at which a demo checkpoint is taken and at which a checkpoint that
just landed is absorbed: after the world has settled and before the next key is read. Absorb
is the creature-grid restamp; position needs no absorbing now that the mirror owns it.

The reason the point is named at all is the silent revert recorded at `overworld.c:1793-1798`,
where a resumed position correct on read-back reverted to the entry point on the first drawn
frame. That hazard could only exist while the draw path republished a second copy.

## Command tail

The shared epilogue every command owes the player: the echoed word, the prompt, a status redraw
where one is due, and the creature turn. Before 2026-11-22 it had no owner — six handlers set a
bare letter and six hand-written arms in the walk loop each re-assembled it, so whether the
sweep and the band redraw run was a fact about a call site rather than a fact about the command.

**Ruled 2026-11-21, built 2026-11-22.** `k_cmd_cont` in `game/commands.c` carries the
continuation — the word, the direction callback, whether the status redraws — reached through
`u2_cmd_continuation(letter)`, and `u2_cmd_tail` in `game/overworld.c` owns the epilogue. The
loop asks one question and never switches on a letter. A fourth field, `is_turn`, turned out not
to be needed: every one of these commands passes a turn, refusals included.

## Seed module

The module that puts the engine into a named state for a test: sector, cell, hit points,
inventory, a tile, a world directory. It is the interface the test suite has been re-implementing
by hand in about eleven places.

**Ruled 2026-11-21.** Offsets are generated from `state.h`, never copied from it. A test names
intent — "the player is in sector 32 with the ankh" — and does not write `p[0x49 - 0x36]`.
**Not yet true in the code, as of 2026-11-23:** nothing parses `state.h`, and
`tools/u2_edit.py` hand-copies the base and the
field table from it. The parser this ruling assumes is a deliverable of
`docs/plan-code-index.md` § 9; until it lands, do not cite this line as settled.

## DS mirror

`engine/src/core/state.h`: the C struct that *is* the original's DS segment, every named field
pinned by `_Static_assert` so a layout drift is a build failure. It is also the save format.
It is the one offset table, and the authority any other offset source must derive from.
**Ruled 2026-11-23:** the mirror is authority for what a byte is *called*; the asm tree is
authority for what *touches* it. A generated footprint index may read the mirror's names and
must never invent one.

## Footprint

The set of memory bytes an `asm/` routine touches, with the site that touches each. A static map
of ownership, not of values: it answers *who writes this byte*, never *what value is in it*.
_Avoid_: dataflow (it promises a value-flow analysis this is not), xref, cross-reference.

## Coverage grade

How far a given `asm/` region has been read, derived from its own header rather than from the
`confidence:` stamp, which a bulk wave assigned. **segmented** — boundaries proven, bytes exact,
body unread. **decoded** — someone has read the body and recorded what it does. **observed** —
its behaviour has been seen running in a named capture.
_Avoid_: "understood"; "confidence" used as a synonym for grade; "grade" used unqualified.

## Negative fact

A claim that something does *not* happen or is nowhere done — "charisma has no other effect".
It carries a `scope` naming the method that established the absence and that method's blind
spots, because absences are only as strong as the search. A negative fact may not be graded
above `inferred` until the method is exhaustive.
_Avoid_: treating "nobody saw it" as an absence proof; a negative fact without a scope.

## Observation mining

The pass that derives candidate facts from tensions between the index tables rather than from
reading one routine at a time: a field written and never read, a guard that never fires, a claimed
call edge never observed executing, a cap that does not cap. Its purpose is to make quirks arrive
as **facts** — cited, graded, and checkable — because a quirk written as a prose aside is a claim
nobody can verify. Ruled 2026-11-23, `docs/adr/0004-prose-is-a-generated-view.md`.
_Avoid_: calling a guess "interesting" instead of filing it as an `inferred` fact; mining for
confirmations of a story already told.

## Editorial lane

The one place in the bible where prose is not anchored to a fact: voice and judgement that cannot be
fact-backed, kept visibly ungraded, never sharing a sentence with a graded claim, and marked as not
derived from the store. The critic reviews it only for containing no checkable claim. It exists
because unanchored colour smuggled into an anchored sentence is how grade laundering happens.
_Avoid_: an editorial sentence that asserts something checkable; a page where the reader cannot tell
which sentences carry evidence.

## Evidence grade

How a claim was established: `observed`, `measured`, `inferred`, `recalled`, per the AGENTS.md
evidence discipline. It attaches to a claim; a coverage grade attaches to a region of the binary.
The two scales share the word `observed` and mean different things by it, so neither is ever
rendered as bare "grade".
_Avoid_: confidence, certainty, "grade" unqualified.

## Behavioural core

The engine code that models what the original does, as opposed to code that only makes it run
here. It is the code that owes an `asm/` citation, and the exemption is the point: renderer,
platform, debug and generated-header code citing nothing is correct, not a gap. The boundary is
owned by the generator's exemption list, not by prose.
_Avoid_: "uncited code", "code with no evidence".

## The bible

The graded, generated reference to *the game*: a view over facts, where every claim carries how it
was established and dies when the decode behind it is corrected. It is distinct from the docs,
which are working notes about our reconstruction, and from the index, which maps bytes to
behaviour for contributors. It describes the game, not the build.
_Avoid_: wiki, guide (that word belongs to the third-party one), handbook.

## Art precedence

The order the engine searches for art: explicit theme dir, then the roots sublayers, then
`themes/`, then a dev fallback, once. It has three owners today and each gives a different
answer, which is a recorded owner-reported bug rather than a tidiness complaint. See
[issue #10](https://github.com/sloppy-seconds/ultima2-rebuild/issues/10).

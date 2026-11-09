# Census design — one read surface, not one file

**Status: accepted direction, first increment shipped.** The shipped part is § 3 items 1
and 2; the rest is the plan.

## 1. Why

The art-01 argument is the evidence. A tile was called "shallow / rough water" in
`tile-art-spec.md`, "scrub" in `passability-findings.md`, and **swamp** in
`asm/248B_swamp_damage_gate.asm` — and the last one was right, because it is the only
one that is a byte-cited proof. `grep -l swamp` returns ten files; `grep -l shallow`
returns ten files. The correct name was in the filename of the proof
(`asm/2495_swamp_damage.asm`) and nobody grepped for it.

Measured sprawl at the time of writing:

| | count |
| --- | --- |
| `disasm/*.tsv` | 18 |
| `disasm/census/*.tsv` | 8 |
| python tools | 93 |
| hand-maintained table rows in five docs | 224 |

`tools/generated_fresh.sh` exists only to diff generated files against regenerations.
A tool whose whole job is detecting drift is a drift tax with a filename.

**Paul's framing: "we don't have a gate problem, we have a data sprawl problem."**
Largely right, and it produced the direction below. One correction, because it changes
what gets built: consolidation alone would **not** have caught art 01. A census that
ingested all ten "shallow" files would have ingested the wrong name too. What caught
it was joining a source the census had never read. So the operative fix is **breadth of
join plus contradiction detection**, not merging for its own sake.

## 2. Three hard limits on "one census owns everything"

1. **The cleanroom wall requires file separation.** `tile-art-spec.md` is the *only*
   permitted input for an art implementer; `tile-art-evidence.md` and
   `tools/tile_rules.py` are forbidden **because they are separate files**. A single
   file owning all data necessarily contains the answer key, and then the wall is one
   `open()` from broken. Not negotiable.
2. **`asm/` is the proof, not a lookup.** Headers are byte-adjacent to what they
   describe and md5'd by CP0. They can be *read by* the census; they cannot be *moved
   into* it without breaking `assemble_clean.sh` and `asm_body_guard`.
3. **Consolidation without grade discipline makes it worse.** `grade=measured` sat on
   a tile row whose *name* was an interpretation and laundered it into a fact. Merge
   more sources into one authoritative-looking table and you get more laundered
   inference, not less.

So: **fewer places to read, same number of places to derive from, plus a checker that
can say "these two sources disagree."**

## 3. The plan

| # | change | status |
| --- | --- | --- |
| 1 | Census ingests `asm/` header vocabulary as a naming source | **shipped** |
| 2 | `--verify` gains cross-source contradiction checks | **shipped** |
| 3 | Docs stop hand-maintaining tables; they generate from census or link to it | open — retires the 224 rows |
| 4 | Each fact gets exactly one generator; generators stay, duplicates go | open |
| 5 | A second, implementer-safe census output: names and intent, **no measurements** | open — required by limit 1 |
| 6 | Deprecate the docs' tables, **not** the source TSVs | open |

Item 5 is the one that keeps the wall standing while the census grows. It is not
optional if the census is going to own naming.

## 4. Shipped: what the tile table now carries

New columns: `gate_value`, `walkable`, `name`, `name_source`, `name_grade`.

`walkable` is a Python transcription of `asm/2423`'s foot ladder, keyed on
`map_byte >> 1` — the mode, **not** the art index, which is the error that caused every
passability regression this project has had.

`name` is read from `asm/` headers, not from the art description. `tile-art.tsv`
carries a pixel *description* and never claimed to name anything; conflating the two is
what let a wrong name arrive wearing a `measured` badge.

Result for the ground tiles:

| art | byte | gate | walkable | name | grade |
| --- | --- | --- | --- | --- | --- |
| `00` | `0x00` | `0x00` | no | water | measured |
| `01` | `0x04` | `0x02` | yes | **swamp** | measured |
| `02` | `0x08` | `0x04` | yes | grass | measured |
| `03` | `0x0C` | `0x06` | yes | — | unnamed |
| `04` | `0x10` | `0x08` | no | mountain | measured |
| `09` | `0x24` | `0x12` | yes | — | unnamed |
| `1C` | `0x70` | `0x38` | yes | — | unnamed |

Three tiles are `unnamed` **on purpose**. The proof does not name them, so the census
does not invent one. That is the discipline the old table lacked: `unnamed` is a
visible gap, a blank is a lie.

### The checks added

- **name-vs-walkable**: a tile named or described as water that the foot ladder
  *accepts* is a naming error, not a terrain type. Same for mountain.
- **citation rot**: a tile citing `asm/<file>` for its name fails if that file is gone
  or no longer contains the name. Renames cannot silently orphan a citation.
- **proof override**: if `asm/248B` names a byte and the census says otherwise, fail.

**Control, demonstrated rather than asserted**: with `0x00` removed from the ladder's
reject list, `--verify` fails with *"tile 0x00 is named/described as water but the foot
ladder accepts it"* and exits 1. Restored, green.

Two bugs found while building this, both of the "plausible wrong answer" shape: the
header regex matched line-by-line and silently lost `mountain`, which wraps across two
header lines; and joining header lines kept each line's leading `#`, so the word after
`0x10->0x08` was `#`. Both produced a *missing* name rather than an error, which is
exactly how a check quietly stops checking.

## 5. What this does not fix

The census is still a join. It can only argue between sources it ingested. The rule
that would have ended the art-01 argument in ten seconds is already in `AGENTS.md`:

> *"Look it up before you investigate it. Start with `ls asm/ | grep -i <word>`."*

No tool replaces that. The census now makes the answer easy to find; it does not make
the habit unnecessary.

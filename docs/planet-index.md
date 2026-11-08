# Planet Index — the hyperjump coordinates are a table in the executable

**Status: built 2026-11-08.** `tools/planet_index.py` reads the table and joins it to
`docs/place-index.md`; `disasm/planet-index.tsv` is its generated output.

> **The headline.** The rocket asks for three numbers — the game calls them **XENO, YAKO, ZABO**
> — and hyperjumps to whatever world sits at that triple. Those numbers are a 30-byte table in
> `ultimaii.exe`, and orbit is tested by **equality** with the triple, no tolerance. So the entire
> inter-planet routing problem is solved by one table, and it needed no capture.

## 1. The table, and the trap in reading it

At file **`0x7212`**: ten X values, then ten Y values, then ten Z values, indexed by the world
byte. **Column-major, not ten triples.**

That distinction is the whole finding, and the trap is instructive. Read as consecutive triples
the same bytes give `(6,5,3) (6,1,2) (9,4,0) …` — every value in range, nothing obviously broken,
and no planet matching anything. A first pass here produced exactly that and found no match at
all. The signature that disambiguates is the one `docs/handoff-2026-11-03.md` recorded from live
screens: the first entry of each column is Earth `(6,6,6)` and the last is Planet X `(9,9,9)`.
Scanning the whole binary for that property returns **exactly one offset**, so the tool locates
the table by its signature rather than by a hard-coded address that would drift silently.

| world byte | planet | XENO, YAKO, ZABO | landing sector | settlements (decoded tile text) | dungeons / towers |
| --- | --- | --- | --- | --- | --- |
| 0 | EARTH | **6, 6, 6** | **40** (hardcoded to era 4) | 11 BARADINS TOWN · 22 TOWNE LINDA · 31 PORT BONIFICE · 32 New San Antonio (wiki) · 41 Pirates Harbour (wiki) | 15 Greenland, 24, 25, 34, 35, 44, 45 |
| 1 | MERCURY | **5, 4, 5** | 10 | — | 15 dungeon |
| 2 | VENUS | **3, 3, 4** | 20 | — | — |
| 3 | MARS | **6, 2, 3** | 30 | 32 **TOWNE MARY** | — |
| 4 | JUPITER | **1, 3, 4** | 40 | 41 EAT PREPPIES (the Preppies) | 44 tower, 45 dungeon |
| 5 | SATURN | **2, 8, 5** | 50 | — | — |
| 6 | URANUS | **9, 4, 6** | 60 | 61 **NEW JESTER** | — |
| 7 | NEPTUNE | **4, 0, 5** | 70 | 71 COMPUTER + CAMP | — |
| 8 | PLUTO | **0, 1, 4** | 80 | 81 TOMMERSVILLE · 82 **TOWNE MAKLER** | 85 dungeon |
| 9 | PLANET X | **9, 9, 9** | 90 | 92 **TOWNE BASKO** · 93 **CASTLE BARATARIA** | — |

Earth's landing sector is the exception the handoff already recorded: a rocket arriving on Earth
is hardcoded to era 4, so it lands in sector 40 rather than 00. The tool encodes that rather than
computing `N0` blindly, because a table that silently disagrees with a recorded observation is the
kind of artefact that gets trusted anyway.

### Two known misses

Sector 03 shows `(unnamed)` even though its tiles spell **SHADOW GUARD**, and sector 81 shows
`(unnamed)` even though its tiles spell **TOMMERSVILLE**. Both are game text; both are missed
because the name rules look for a phrase opening with `TOWNE`/`PORT`/`CASTLE` or closing with
`TOWN`, and neither does. They are visible in the `rooms` column of `place-index.tsv`. The
alternative — a looser rule that guesses at proper nouns — would start inventing names as often as
finding them, so the miss is left visible instead.

## 2. Three independent confirmations

1. **Nine of nine** rows the Ultima Codex publishes match our bytes exactly.
2. The Codex page also publishes a **census of every digit** across its full table including the
   Sun: `{0:2, 1:2, 2:2, 3:4, 4:9, 5:4, 6:5, 7:0, 8:1, 9:4}`. Our 30 bytes plus the Sun's
   `(4,4,4)` reproduce it **exactly**. That is effectively a checksum over the whole table — a
   single misread byte moves a count — and it is what makes Pluto's `(0,1,4)` trustworthy even
   though the wiki's Pluto row was not visible in the fetched page.
3. The game's own dialogue says **`SHE'S GOT TO BE AT 9-9-9!`** (`tlkx11`, the Phylosopher),
   agreeing with `table[9]` from inside the binary.

`--verify` asserts all three, plus that the locator is unique and that `7` never appears. The
controls were exercised, not assumed: reading the table as ten triples — the original mistake —
turns it red with `EARTH is (6,5,3)`, and perturbing one pinned wiki coordinate turns it red on
that row.

## 3. Two things the wiki gets that the bytes do not, and one it does not have

* **The Sun is not in the table.** `(4,4,4)` is manual-derived wiki data. Entering it is a death,
  not a destination, and there is no world byte for it.
* **`7` never appears** in any coordinate, in either source. The wiki notices this too.
* **Pluto's coordinates** came out of our bytes before the wiki row was visible in the fetch, and
  the digit census confirms them.

## 4. What this closes

Before this, the inter-planet layer of routing was the vague part of the map. It is now:

```
world byte -> triple          (this table)
triple     -> orbit achieved   equality, no tolerance (asm/7030 region, handoff 2026-11-03)
world N    -> landing sector   N0, ones digit forced to 0 (observed, same handoff)
sector     -> what is there    docs/place-index.md
```

A bot can navigate the solar system from these four facts with no capture and no guess. The
remaining routing gaps are the **intra-sector BFS** (all the pieces exist —
`u2_mode_passable` plus the tiles — but no pathfinder has been written) and the **time-door cells
for eras other than 2** (the rotation rule is fully decoded in `docs/moongates.md`; only era 2's
four gate cells were ever watched).

## 5. Settlement names the wiki adds

`disasm/place-wiki-names.tsv` gained the three joins where the wiki names a settlement our
name-rules did not catch: **New Jester** (Uranus — the tile text literally contains `NEW JESTER`,
it simply does not open with TOWNE/PORT/CASTLE), **Computer Camp** (Neptune — `COMPUTER` and
`CAMP` are separate signs), and **Pirates Harbour** (Earth 41 — the tile text says `PIRATES
COVE`, so this one is a genuine naming disagreement between the wiki and the game, recorded
rather than resolved).

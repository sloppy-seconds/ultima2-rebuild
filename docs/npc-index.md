# NPC Index — the game names its NPCs too, in the dialogue file

**Status: built 2026-11-08.** `tools/npc_index.py` generates `disasm/npc-index.tsv`
(92 records, 15 sectors). Companion to `docs/place-index.md`; same discipline, same
crosswalk pattern.

> **The headline.** The game names almost all of its NPCs itself. They are the first line of
> each `tlkxNN` record, high-bit encoded, and the record's **position** is its identity: the
> creature slot byte at `DS:1D7` minus `0x80` selects which record prints
> (`asm/4209_talk_scripted_npc.asm`). So an NPC's identity is `(sector, record index)`, and
> both halves are bytes on disk. Third-party names were permitted for unnamed NPCs (owner
> ruling) and turned out to be needed for exactly **two** of 92.

## 1. The role index is the interesting half

The record number is not a print index — it is the game's **quest-dispatch key**. `cmd_O`
enumerates roles `0x81`, `0x82`, `0x83` and gives each a different effect, and `asm/4209`
branches on `0x81` to set the blessing byte. That is why the index keys on role rather than on
speaker: names are what a player reads, roles are what the engine tests.

Sector 32, era 1990 A.D. — the whole endgame economy in four NPCs:

| role | the game's own text | what `cmd_O` does with it |
| --- | --- | --- |
| `0x81` | **SANTRE THE SWASHBUCKLER** — "BEWARE, I'VE A QUICK BLADE!" | offer ≥ 500g → `ENILNO IS YOURS!`, sets `DS:7F` — the quicksword |
| `0x82` | **AN OLD MAN** — "I'M AN OLD MAN." | offer ≥ 500g **and `DS:6C != 0`** → `THE RING IS YOURS!`, sets `DS:D6`; without the blessing → `EARN THE RING!` |
| `0x83` | **THE FLOPY FIGHTER** — "I OWN ONE OF EVERYTHING!" | offer → `HERE TAKE THIS!`, grants a random weapon `DS:76+(rng&7)+1` |
| `0x84` | **A CLERK** — "WELCOME TO THE HOTEL CALIFORNIA!" | the generic bit-7 arm — **the stat grind**: 75% chance of +4 × hundreds to a random stat |

Sector 93, Planet X, castle:

| role | text | effect |
| --- | --- | --- |
| `0x81` | **FATHER ANTOS** — "YOU HAVE EARNED MY BLESSING. RETURN AND CLAIM THE RING!" | `asm/4209`: talking to this role when `DS:6D==9` and `DS:4A==3` sets **`DS:6C = 1`** |

`THE FLOPY FIGHTER` handing out a random weapon because he owns one of everything is not a
guess at meaning — it is what the bytes do, and the joke is the confirmation.

## 1a. The quicksword cannot be bought, and that is a design statement

Worth recording because it looks, at first, like a shop item: the weapons shoppe's own printed
catalogue is

```
THE WEAPONS SHOPPE:
1-DA, 2-MA, 3-AX, 4-BO,
5-SW, 6-GR, 7-LI, 8-PH.
YOUR INTEREST?
```

Eight entries, ending at **8-PH** — DAGGER through PHASER. QUICK SWORD is pool index 9 and is
**not on the menu at all**. It is not gated or hidden; the catalogue simply does not list it, and
`asm/1E4F` bounds the accepted digit at 8 (`shop_weapons` passes that bound, and menu 3 = AX was
confirmed live against `DS:79`). So **PHASER is the ceiling of what money can buy**, and the
quicksword's only source in the binary is Santre.

That closes a loop rather than opening one. `cmd_O` role `0x81` sets `DS:7F`, and
`weapons_owned` is `DS:76..7F` — so `DS:7F` is `weapons_owned[9]`, the quicksword's own slot.
`ENILNO IS YOURS!` literally means "you now own one QUICK SWORD". And the other offer arm,
role `0x83`, grants `DS:76 + (rng & 7) + 1`, i.e. indices 1..8 — DAGGER..PHASER, **never 9**.
There is no third path.

The consequence for `docs/goal-spec.md` W2: the quicksword is a **quest item, not
merchandise**, so no amount of the Hotel California gold-grinding substitutes for talking to
Santre in sector 32. It is the one endgame requirement that cannot be bought out of.

## 2. The endgame chain is now byte-proven end to end

This is what the index actually bought:

```
talk to role 0x81 in sector 93 (Planet X, castle)   ->  DS:6C = 1     (the blessing)
offer >= 500g to role 0x82 in sector 32             ->  DS:D6 = 1     (the force field ring)
offer >= 500g to role 0x81 in sector 32             ->  DS:7F = 1     (the quicksword)
offer 100g to role 0x84 in sector 32                ->  75% chance, +4 to a random stat
```

`docs/goal-spec.md` § 2 listed the blessing as **"unverified, and currently unrepresentable"**
because `state.h` had no story-flag vocabulary for it. That is now wrong in the good direction:
the blessing is `DS:6C`, one byte, with a named writer and a named reader. **G61 is closed.**

One process note, because it is the kind this repo keeps having to write down: the writer was
**already documented** in `asm/4209_talk_scripted_npc.asm`'s header, which lists
`422B  if [DS:6D] == 9 and [DS:4A] == 3 and [di+0x1D7] == 0x81: [DS:6C] = 1`. What was stale
was `state.h`'s `reserved_6C /* no reader or writer found */` and, downstream of it, G61. The
answer was on disk; nobody had looked the byte up rather than the question up.

## 3. The collision has a cast

`cmd_O` gates on `DS:49 == 3 && DS:4A == 2` — the sector digits — and **never checks the world
byte**. Sector 32 is both Earth-1990 and Mars, and the Galaxy patch gives Mars an entirely
different set of NPCs in the same four roles:

| role | Earth (`tlkx32`) | Mars (`TLKG32`) |
| --- | --- | --- |
| `0x81` | SANTRE THE SWASHBUCKLER | Marlin Perkins — "Go in and I'll zap you!" |
| `0x82` | AN OLD MAN | Owen K. Garriott — "Have you seen my shuttle?" |
| `0x83` | THE FLOPY FIGHTER | Hecate — "Bubble bubble toil and trouble!" |
| `0x84` | A CLERK | The Historian — "George Washington slept here!" |

So on the byte reading, **the quicksword and the ring are obtainable on Mars as well**, from
different characters filling the same role slots. That is inferred from the gate not testing
`DS:6D`, and it is worth a capture before it is relied on — but it is the sort of thing the
original single-layer view could never have shown.

It also nearly corrupted the crosswalk: without a `layer` column the join labelled **Owen K.
Garriott** as "Old man". The layer column is in the file and `--verify` resolves rows against
layer+sector+record for that reason.

## 4. Where third-party names were used, and where they were not

`disasm/npc-wiki-names.tsv` has **two rows**, both `inferred`, both in Earth sector 32:

* role `0x84` → **The Clerk**. The game gives a job title. The wiki treats him as an entity,
  and the bytes support the join independently: `cmd_O`'s generic arm handles exactly the roles
  its special list omits, and this is the record that reaches the grind.
* role `0x82` → **Old man**. The game gives a description. The join is by function — role
  `0x82` in sector 32 is the only ring grant in the binary.

Everything else is the game's own: Santre, Father Antos, Queen Susan, King Ozy, Brother Antos,
Bishop Bob, Andre the French Chef, Commander Decker, Ugly Irving, Kieth Zabalaoui, Big Bob Jaws,
Bill Bruiser, Debbie the Lifeguard, Howie the Pest, Richard Garriott, Robert Woodhead, Andy
Greenberg, the Tommerviks, Justin the Jailer, Sing Lee the Cook, Grendel the Bum, Ithilian the
Wench, the Wandering Bard, the Sightless Sage, the Phylosopher, the Swamp / Court / French /
Stoned Jester, and the rest. **The wiki has nothing to add to any of them.**

Note the game's spelling is **SANTRE**; the wiki's link text renders it "Sentri". The bytes win,
as usual, and the difference is recorded rather than tidied.

## 5. The talk file format, checked against a third-party description (2026-11-09)

ModdingWiki publishes an *Ultima II Talk Format* page with a reader. Every claim in it was
tested against `original/tlkx*` and `original/updated/TLKX*` rather than accepted.

| claim | result |
| --- | --- |
| every file is exactly 384 bytes | **confirmed**, all 24 files |
| file begins with a `0x00` marker | **confirmed** |
| four null-terminated dialogue strings | **confirmed, by two independent routes** -- see below |
| rotate by 128, ROT13-like | **confirmed**: `byte ^ 0x80` decodes everything. Same fact as "the high bit is set" |
| line endings are CR | **confirmed** |
| three rows, thirty columns, CR implied at 30 | **confirmed**, with one string that only looks like a violation |
| the tail is garbage from the memory buffer | **confirmed**, and it is more interesting than garbage |

### The four-record limit, from two directions

The wiki derives four from reading the file; this tool derives it from the **creature role
byte**, which is a completely independent signal. They agree exactly:

```
files: 23   total records: 92
records per (layer, sector): {4: 23}
```

92 = 23 x 4, four in every file, no exceptions. Role bytes `0x81`..`0x84` map to dialogue
indices 1..4, which is the join `npc_index.py` already uses.

**The trap here is real and was walked into while writing this.** Splitting the file on nulls and
keeping segments that are mostly printable gives **5 to 10** "dialogues" per file, not four. The
extra segments are not corrupt -- they are legible dialogue text, from the buffer that was still
in memory when the file was written. `tlkx03`'s tail repeats its own MINAX line; `tlkx23`'s and
`tlkx31`'s tails each carry a Wandering Bard line. Printability cannot distinguish authored text
from residue because the residue *is* text. The correct discriminator is the role byte, or
 equivalently the engine's own read limit -- which is why the wiki's reader stops at the fourth
null instead of trusting what it can decode, and why this tool keys off the role byte rather than
the nulls.

### The thirty-column rule, and the one string that tests it

Exactly one authored row exceeds 30 columns, in `tlkx32`:

```
SANTRE THE SWASHBUCKLER WARNS:BEWARE, I'VE A QUICK BLADE!
   len=57  explicit CRs=0
```

That is not a violation. The rule is that a row ends with CR *unless it is 30 columns, where a
CR is implied*, so this renders as 30 + 27 -- two rows of the three-row budget, with the author
relying on the implied break. Worth noting it is **Santre**, the sector-32 NPC who is the only
source of the QUICK SWORD: the one string that stress-tests the wrap rule sits on the win path.

### The Galaxy patch recases dialogue, and that is a systematic layer difference

The patch layer rewrites the same records in mixed case. Record 1 of each shared sector:

| sector | root `tlkxNN` | patched `TLKXNN` |
| --- | --- | --- |
| 23 | `BISHOP BOB PREACHES: CONFESS TO BROTHER ANTOS!` | `Bishop Bob preaches: Confess to Brother Antos!` |
| 33 | `SISTER SLEDGE SINGS: WE ARE FAMILY!` | `Sister Sledge sings: We are family!` |
| 32 | `AN OLD MAN MUMBLES: I'M AN OLD MAN.` | `An old man mumbles: I'm an old man.` |
| 41 | `CHUCKLES THE BUMBLE CRIES: CAN I LOOK YET...` | `Chuckles the Bumble cries: Can I look yet...` |

The content is identical; only the casing differs. This matters for anything that compares the
two layers or displays both: a case-sensitive diff reports every shared record as changed, which
is the same class of error `docs/planet-maps-galaxy-patch.md` § 2a records for filenames. The
speaker names in `disasm/npc-index.tsv` come from the **root** layer and are therefore uppercase.

### A fossil of the game's own source code, and a line that exists nowhere else

The wiki says *"There is even some source code to be found in some of the files!"* Confirmed --
and it is in the **raw** bytes, not the rotated ones, which is why a first pass that decrypted
everything found nothing:

```
tlkx11  tlkx21  tlkx22  tlkx32:
        PRBYTE    PRINT    $0D,"HOW 'BOUT IT? ",$00
```

That is an assembler macro invocation from the game's own source, sitting in the buffer residue.
And the string it prints -- `HOW 'BOUT IT?` -- **appears in no authored dialogue in any talk
file**, on either layer. So it is a cut or unused line, preserved only because the assembler's
text buffer was still in memory when someone saved a talk file over the top of it.

Nothing in the engine should read past the fourth record. This section is documentation of what
lives there, not a suggestion to use it.

## 6. Method

Two traps, both live in the tool's comments rather than only here:

* **The tail is not text.** The shipped `tlkx` files are 384 bytes and the last ~128 are
  assembly source, so record 5+ of several sectors decodes to `PRBYTE PRINT $0D,"HOW 'BOUT
  IT?"`. The engine's `U2_TLKX_BYTES` limit exists for this reason and the tool copies it;
  `--verify` fails if an assembly record ever reaches the output.
* **A closed verb list silently deletes NPCs.** The first draft matched speakers against a list
  of speech verbs and returned an empty speaker for every Galaxy-patch record, because Mars
  uses `threatens` and `asks` where Earth uses `WARNS`. The parser now splits on the first
  colon and trims a verb only when one is recognised.

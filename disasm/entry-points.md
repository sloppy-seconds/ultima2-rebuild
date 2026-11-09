# Entry Points (CP1)

**Source:** `captures/spice86/spice86-cfg.json` (Spice86 runtime CFG, one playthrough).
**Extractor:** `tools/extract_entry_points.py` (re-runnable from project root).
**Output:** `disasm/entry-points.txt` — one 4-hex-digit offset per line, `XXXX  # block=<id>`.

## Filter rule

1. Keep blocks whose `entry` segment is `017D` (game code segment). `F000` (48 blocks) and
   `0A29` (1 block) are BIOS/ROM and dropped.
2. A block flagged `dead` is dropped **only if** none of its `pred` blocks is live (non-dead);
   otherwise kept (dead ≠ not-an-entry).
3. Dedup by offset (none found), sort ascending.

## Counts (measured)

| Metric | Value |
| --- | --- |
| Total CFG blocks | 845 |
| Blocks in `017D` | 796 |
| Dead blocks in `017D` | 181 |
| Dead kept (≥1 live pred) | 78 |
| Dead dropped (all preds dead) | 103 |
| **Unique entry offsets emitted** | **693** |
| Duplicates removed | 0 |

Note: the plan expected the count "within a few of 796 (dedup may reduce it)"; the actual
reduction (693) comes from the plan's own dead-drop rule, not dedup. The plan's on-failure note
anticipates this ("197 blocks marked dead ... do not pad the list"). All `pred` ids resolved;
no block had an empty pred list.

## Address mapping (from plan §1 — all notations name the same byte)

| Notation | Form | File offset |
| --- | --- | --- |
| Runtime CFG block | `017D:XXXX` | `XXXX + 0x200` |
| Ghidra address | `1000:XXXX` | `XXXX + 0x200` |
| Reko address | `0800:XXXX` | `XXXX + 0x200` |
| `objdump -b binary` | prints file offset | as-is |

The `entries=` file for `DecompileMz.java` takes the bare `XXXX` (no segment, no `0x200` bias).

## Verification

**Full first-opcode check (all 693):** for every entry, the first opcode bytes recorded in the
CFG `asm` were compared against `original/ultimaii.exe` at `XXXX+0x200`. **692/693 match.**
The single mismatch is block 96 at `0100` (file `0x300`): CFG records runtime `B8BE08`
(`mov ax,0x08BE`), file holds `b8 41 07` (`mov ax,0x0741`) — the documented MZ relocation
fixup (see `captures/README.md`), expected, not an error.

**objdump spot-checks (3 random entries, seed=1).** Note: binutils 2.46 requires `-j .data`
for `objdump -d -b binary --start-address=...` to emit instructions (see implementation log).

```
== entry 0962 -> file 0xb62
     b62:  a0 15 00     mov    0x15,%al
     b65:  e8 bb 1a     call   0x2623
== entry 5188 -> file 0x5388
    5388:  a0 58 00     mov    0x58,%al
    538b:  b7 00        mov    $0x0,%bh
    538d:  8a           mov    0x59,%bl   (truncated 6-byte window)
== entry 06AC -> file 0x8ac
     8ac:  b4 00        mov    $0x0,%ah
     8ae:  cd 1a        int    0x1a
```

All three decode as plausible routine starts (absolute `mov`, register init, DOS `int 0x1a`),
not mid-string garbage.

---

## v2 — merged with user-authorized external captures (2026-10-19 night)

The project owner explicitly authorized raw runtime CFG exports from
`/var/home/paul/Projects/ultima2/references/extracted/live-capture/` (see
`captures/external-20261019/PROVENANCE.md`; only raw captures were imported, no prior
analysis). The extractor now merges all 45 sources (in-project CFG + 44 external harvests:
shops, death loop, frigate, Lord British, Port Bonifice, cumulative watch-* series).

- Byte-verified first: 40,162 external block checks against `original/ultimaii.exe`;
  only mismatch = the known `0100` relocation fixup. Same binary confirmed.
- Merged unique entries: **1113** (was 693). New seed union with jump table: **1125**
  (12 handlers still CFG-uncovered: C D G H J K L M O R T V W X).
- Handler coverage from runtime evidence: 14/26 (was 7/26).

## The program entry point is not in this list, and cannot be

Everything above is a *routine* entry discovered from the runtime CFG — what executed during a
playthrough. The **program** entry point is a different kind of fact: it is declared in the MZ
header and needs no capture at all.

| field | value | meaning |
| --- | --- | --- |
| `e_cs:e_ip` | `0x8ac:0x0000` | image-relative, so image `0x8ac0` = file `0x8cc0` |
| `e_ss:e_sp` | `0x9fe:0x0064` | the stack DOS sets up before transferring control |
| `e_cparhdr` | `0x20` | 512-byte header, so image offset + `0x200` = file offset |
| `e_crlc` | `1` | one relocation entry — see `captures/README.md` |

The bytes at file `0x8cc0` confirm it independently of the header claim:

```
8c d8      mov ax,es
8e c0      mov es,ax
05 10 00   add ax,0x0010
8e d0      mov ss,ax
bc 00 00   mov sp,0x0000
```

That is the textbook MASM-style startup prologue — adopt ES from the PSP, place SS just above
the image, zero SP. Nothing else in the file looks like it.

Two things worth being explicit about. First, **the CFG could never have told us this**: the
entry is configured, not discovered, and a capture records what ran. Second, the entry sits at
image `0x8ac0`, which is *inside* the range owned by `asm/8387_sub_8387.asm` (`8387..8CCF`) and
is therefore currently an anonymous `L_008ac0` label. It is named in that file's header rather
than split out, because splitting changes the CP0 tiling and that is a decision for the owner,
not a documentation fix.

Note also that the relocated DS-initialising instruction at image `0x100` is **not** the entry —
it is a separate startup path at the very start of the machine code. Two different startup-ish
addresses, and only one of them is what DOS jumps to.

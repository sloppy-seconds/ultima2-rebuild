# Runtime capture artefacts

Live observations of the game executing under Spice86, as opposed to static analysis of the
file on disk.

| File | What it is |
| --- | --- |
| `spice86-cfg.json` | 845 basic blocks, 143 partitions, 11 entry points |
| `spice86-functions.json` | 83 discovered functions with call counts |
| `live-ds-4f4d.bin` | 4096 bytes of the data segment, `DS:0000` onward |

## Why this matters more than the decompiler output

Both Ghidra and Reko find function boundaries by following `call` edges. This game's command
handlers are reached by `jmp *%bx` through a jump table and return with `jmp` to a shared
exit, never by `call`/`ret`. Static reachability cannot see them, and on this binary neither
tool agreed with the known entry points: of 46 addresses known to be routine starts, 11 were
Reko procedure starts and 7 were Ghidra function starts.

The runtime CFG has no such blind spot. A block recorded here was **actually executed**, which
makes its start address the strongest available evidence for where a routine begins. 796 of
the 845 blocks are in the game's code segment.

Use these addresses as the seed list for both decompilers rather than trusting either tool's
automatic discovery.

## Address mapping

Block entries are labelled `017D:071E`. The segment is the **real runtime code segment**, and
the conversion to a file offset is:

```
file_offset = offset + 0x200
```

Verified two ways:

- Block `017D:071E` records `EBF5` (`jmp short 0x0715`). File `0x71E + 0x200 = 0x91E` holds
  `eb f5`.
- 795 of the 796 code blocks have their first instruction's opcode bytes present at the
  computed file offset.

The `017D` value is not arbitrary. The data segment at runtime is `0x08BE`, the immediate
stored in the file at `0x0300` is `0x0741`, and `0x08BE - 0x0741 = 0x17D`.

## The one block that does not match, and what it teaches

Block 96, entry `017D:0100`, file `0x300`:

```
CFG records:  B8BE08   mov AX,0x08BE
file holds:   b8 41 07 mov ax,0x0741
```

This is **not** a discrepancy between binaries. The file value is patched at load time by the
MZ relocation fixup, so the running program genuinely executes `mov ax,0x08BE`. The CFG
captured the runtime instruction, while the file holds the pre-relocation one.

Two consequences worth remembering:

- A byte-for-byte comparison of a runtime-captured instruction against the file will differ
  wherever a relocation fixup applies, and do not treat those diffs as an error in either
  source. **There is exactly one such place.** The MZ header's `e_crlc` (relocation count) is
  `1`, and its single entry points at image offset `0x101` — file `0x301` — which is the
  immediate operand of the `mov ax,0x0741` / `mov ds,ax` pair at image `0x100`:

  ```
  file 0x300:  b8 41 07 8e d8 05 10 03 8e
               mov ax,0x0741  mov ds,ax  add ax,0x310  mov ss,...
               ^^^^^^^^^^^^^
               the one relocated word in the whole 37,344-byte image
  ```

  So the entire file-versus-runtime byte discrepancy is one instruction's immediate. Anything
  else that differs between a capture and the file is a real finding, not relocation noise.
  This paragraph previously said "the segment-initialising instructions", plural, which was a
  guess about a count nobody had read out of the header.
- The arithmetic ties out from two directions, which is what makes it a fact rather than a
  story: `0x0741 + 0x17D = 0x08BE`, and `0x17D` is the segment every block in this very CFG is
  tagged with. The load paragraph is therefore confirmed by the capture that produced the
  fixed-up value and by the segment the same capture reports.
- The runtime segment values are only knowable from a capture like this one. The file alone
  does not tell you that `DS` will be `0x08BE`. It does now tell you *where* it will be patched,
  which is what the header's relocation table is for.

## Limits of this capture

- **One session, one playthrough.** 2,438 instruction records against roughly 10,000 code
  bytes means this is a sample of the paths that were walked, not the complete CFG. Code that
  the session never reached is absent, and absence here is not evidence that something is not
  code.
- Captured from an interactive live instance rather than a clean shutdown export.
- 197 blocks are marked `dead`.
- Blocks in segments `0A29` and `F000` are BIOS and ROM, not the game. Filter to `017D` for
  game code.

## Block schema

```json
{
  "id": 467,
  "entry": "017D:071E",
  "term": "017D:071E",
  "pred": [465],
  "succ": [469],
  "asm": ["EBF5|jmp short 0x0715"],
  "dead": null,
  "incomplete": null
}
```

`asm` entries are `HEXBYTES|disassembly`, which is what makes the byte-level verification
above possible. `pred` and `succ` are block ids, not addresses.

## Provenance

These files were captured from a Spice86 session run against a copy of `ultimaii.exe` that is
**byte-identical to this project's `original/ultimaii.exe`** (same MD5, same MZ header). The
capture therefore describes this binary, and every block address has been re-verified against
`original/ultimaii.exe` as described above.

They were produced outside this directory. Treat the artefacts as valid input, and re-capture
from `original/` whenever a session needs current data or coverage of paths this one missed.

## Per-run layout under `spice86/`

The whole `captures/spice86/` tree is gitignored (`.gitignore`), so anything here is
on-disk working evidence, not a committed artefact. Curated conclusions belong in
`disasm/` and `docs/`; this directory holds the raw material they were derived from.

Two shapes live here:

* `spice86/<label>/` and `spice86/<label>-live/` — one Spice86 harvest, written by
  `tools/spice86_stop.sh` (clean exit) or `tools/spice86_start.sh --harvest` (mid-play,
  via `monitor dumpall` on the GDB port). Contains `spice86dumpCfgBlocks.json`,
  `spice86dumpCfgPartitions.json`, `spice86dumpExecutionFlow.json`,
  `spice86dumpMemoryDump.bin` and, for a live harvest, `live-ds.bin`. Remember that
  CFG *blocks* are discovered, not executed; only `ExecutedInstructions` inside
  `spice86dumpExecutionFlow.json` proves something ran. `tools/cfg_printed.py` gets
  this right.
* `spice86/fight-<date>-<place>/` — a `tools/turn_recorder.py` recording. Change-
  triggered on the food odometer, so snapshots land on turn boundaries rather than
  between them. `index.jsonl` has one line per turn (position, food delta, HP, gold,
  EXP, the parsed monster list, and an odometer sanity flag); `turns/NNNNN.ds` is the
  full `DS` block and `turns/NNNNN.vga` the video span for that turn, so any later
  tool can re-render history instead of re-running the game. `recorder.log` is the
  console transcript, and its final line reports the anomaly count.

Caveat carried forward: the recorder's `screen` field is junk while the game is in CGA
graphics mode, because `0xB800` holds pixels there and not a text page. Read messages
from `ExecutedInstructions`, and read state from the `.ds` files.

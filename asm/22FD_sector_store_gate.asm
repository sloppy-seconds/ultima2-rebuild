# sector_store_gate — `if (DS:6D == 0) store_sector();` the sector being left
# name-evidence: the whole body IS the gate. `a0 6d 00` mov DS:6D,%al; `0a c0` or %al,%al;
#           `74 01` je +1; and the `c3` ret lives in asm/2304. The je target is runtime 0x2305,
#           which is asm/2305_sector_store.asm. There is no other instruction in the span, so the
#           routine's entire behaviour is: on Earth, store; off Earth, return.
# func 22FD..2303  len 0007 B  evidence: seed+call+reko
# DS:6D is the WORLD byte: 0 = Earth, 1..9 the planets, 0x0A deep space (state.h:333, and
#           asm/7030_space_collision writes it). So the store is Earth-only. Corroborated
#           independently by asm/3DFA_offer_only_outdoors_earth, whose measured save guard prints
#           'ONLY ON EARTH!' when DS:6D != 0 and writes nothing.
# callers (eight, all direct `call`, mapped by RUNTIME address -- see the address-space note in
#           docs/outstanding.md before re-deriving any of these):
#             asm/26A8  runtime 0x26b4        a thin "store the current sector" wrapper
#             asm/341E  runtime 0x3427
#             asm/343F  runtime 0x345c, 0x3492   the '-TOWN' / '-TOWER' arrival arms
#             asm/34C3  runtime 0x34cc
#             asm/34E4  runtime 0x3504        the '-DUNGEON' arrival arm
#             asm/38B0_cmd_L  runtime 0x395a
#             asm/3DFA  runtime 0x3e4a        the save path, behind its own DS:6D guard
#           The site-entry arms are the shape that matters. asm/34E4 prints '-DUNGEON', sets the
#           FCB random record, calls THIS routine, then sets the arrival cell and calls asm/2290
#           to load the sector being entered: store the sector you are leaving, load the one you
#           are entering. asm/343F does the same for '-TOWN' and '-TOWER'.
# provenance: measured 2026-11-15, three independent ways. (1) The bytes above, read from
#           original/ultimaii.exe at file 0x24fd. (2) The je target resolves to asm/2305, which
#           appears END TO END -- 0x2305 through 0x2335, the DOS call included -- in the
#           ExecutedInstructions of 33 harvests, and the instruction immediately before every one
#           of those entries is 0x2302, this routine's je. The TLKX dialogue read asm/22D5, which
#           uses the same DOS seam, appears in 13, which is the control showing the scan works.
#           (3) In both harvests carrying a memory dump alongside a flow dump, DS:6D == 0x00.
# confidence: high
# CORRECTION, and it matters. The 2026-10-29 revision of this header concluded the opposite and
#           said so forcefully enough that the engine was changed on the strength of it: it called
#           this "NOT a sector store", asserted "the original provably writes no sector file at
#           all", and that assertion was used to delete the engine's store-on-transition. Both
#           halves of that reasoning fail. The routine does not contain a store -- it never claimed
#           to -- but its only branch LANDS on one, and a gate on a store is part of the store's
#           path. And the observation cited as proof (mapx31, monx31, mapx30, monx30 byte-identical
#           to pristine after several town entries and exits) is exactly what a store of UNCHANGED
#           content produces: same bytes, same file. It was never evidence of absence.
#           What that session got right and is worth keeping: DS:6D is the world byte and never was
#           boots_count -- that name was our own tool's invention, an earlier revision of this
#           header called this a boots/paralysis gate on the strength of it, and the byte never was
#           boots. And the engine's store-on-transition had independently damaged a game file
#           (original/monx71, mtime 2026-10-23, monster coords and HP drifted; restored from
#           references/). The defect there was WHAT it wrote, not THAT it wrote. See the 2026-11-11
#           stamped-write-back ruling in docs/phase4-state.md and G81 in docs/outstanding.md.
#           Re-opening the engine's store-on-transition is a behaviour change with save-file
#           consequences and is NOT done here; it needs the owner.
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
sub_22fd:
L_0024fd:
        mov    0x6d,%al
L_002500:
        .byte 0x0a,0xc0  # forced-kept: gas re-encodes 'or     %al,%al' differently
L_002502:
        je L_002505

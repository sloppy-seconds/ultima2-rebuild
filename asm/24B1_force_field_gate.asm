# force_field_gate — the mode test that decides whether entering a tile costs 1000 HP
# name-evidence: `cmp al,0x2E; jne L_002720`. It is the second rung of the same ladder as
#           asm/248B_swamp_damage_gate: fall through on a swamp miss, test for mode 0x2E,
#           and mode 0x2E == map byte 0x5C. That identity is what `asm/5B71_mode_table_copy`
#           had already recorded as `0x5C >> 1 == 0x2e explains the mode without it`, and it
#           is the reason the long-open "who writes DS:2CB/2DE/2E0/2F3" question turned out
#           to be a dead end: the mode is the map byte shifted once, not a table lookup.
#           A match falls through to asm/24B5_force_field_event, which checks the ring
#           counter at [DS:D6] and subtracts `sbb al,0x10` from [DS:51] — 0x10 in the HIGH
#           BCD byte, i.e. 1000, matching its own string "FIELD CAUSES 1000 DAMAGE!".
#           A miss jumps past both effects and the step costs nothing.
#           Renamed 2026-11-02 from sub_24b1. See docs/archive/handoff-tile-effects.md.
# provenance: static read of the asm/2423 span 2026-11-02. The mode-to-tile identity comes
#           from asm/5B71_mode_table_copy's own header (`0x5C >> 1 == 0x2e`), and the
#           1000 figure is the asm/24B5 string "FIELD CAUSES 1000 DAMAGE!" agreeing with
#           `sbb al,0x10` on the HIGH BCD byte. Not yet observed live: no capture has
#           walked onto a force field, so the effect's runtime behaviour is inferred.
# corrected 2026-11-20 -- evidence: captures/spice86/g29-field/spice86dumpMemoryDump.bin
#           reads DS:49 = 0x00, DS:4A = 0x03, DS:51 = 0x39, i.e. sector 03 at 3999 HP, the
#           last row of the recorded 9999-down-to-3999 crossing sequence. So the rung HAS
#           been observed live, in sector 03, and the "not yet observed" line above is kept
#           as written because it is part of the record.
#           The rung carries no sector gate. asm/2423's entry does read `mov 0x4a,%al;
#           cmp $0x0,%al; je L_00268b`, and that was transcribed as a permission test for
#           the whole ladder; the engine implemented `if (sector_lo != 0) return;` and the
#           CP5 comment said so. Both are refuted by the capture above and by the data: all
#           72 force-field tiles in the shipped set are in sector 03 and the 4 in sector 71,
#           neither of which passes a ones-digit-equals-zero test, and sector 03 also holds
#           53 swamp tiles. The `je` is an early route to the ladder, not a gate on it.
# func 24B1..24B4  len 0004 B  evidence: seed+reko
# confidence: medium (bytes proven by check_func.py)
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
sub_24b1:
L_0026b1:
        cmp    $0x2e,%al
L_0026b3:
        jne L_002720

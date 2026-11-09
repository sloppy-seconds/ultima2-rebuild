# item_slot_add_one — add 1 to one held-item count, slot given by %di
# name-evidence: bytes, whole function read (observed). Body is
#           `mov $0x0,%ah; mov %ax,%di; clc; mov 0xd6(%di),%al; adc $0x1,%al; daa;
#           mov %al,0xd6(%di)`. DS:D6+i is the held-item count array, 16 slots,
#           per docs/save-format.md (proven); the slot index arrives in AL and is
#           zero-extended into DI. Renamed from sub_2d07 on 2026-10-22 by
#           tools/bcd_census.py.
# SUPERSEDED 2026-11-09: the claim above that no call exists was right about the call and
#           wrong about the conclusion. There is no call because the entry is a
#           FALL-THROUGH from asm/2D03 (`and $0xf,%al ; je skip`), inside the thief branch
#           of the kill-drop dispatch at asm/2CE5. Proven by execution: 0x2d07 is in
#           ExecutedInstructions 8 times across baseline-fight, play-test-live and
#           play-test. The game action is now proven -- it is the thief's random item
#           drop -- and the index is rand & 15, so slot 0 (the RING, which asm/3C2E pays
#           for gold instead) is excluded and every other slot is reachable. This is the
#           only source in the binary for the ANKH at DS:DD, and therefore the only route
#           to boarding the rocket for a character that did not start with one.
# label: kept as `sub_2d07` on purpose. CP0 (engine/tests/run_cp0.sh) pins every
#           non-comment line across a rename, so only the filename and the header
#           may carry a discovered name. Do not "tidy" the label to match.
# func 2D07..2D16  len 0010 B  evidence: seed+reko
# provenance: static:tools/bcd_census.py
# confidence: high for the arithmetic (16-byte whole function, no clamp, so a slot
#           wraps 99 -> 00); low for the caller and for which item %di selects.
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
sub_2d07:
L_002f07:
        mov    $0x0,%ah
L_002f09:
        .byte 0x8b,0xf8  # forced-kept: gas re-encodes 'mov    %ax,%di' differently
L_002f0b:
        clc
L_002f0c:
        mov    0xd6(%di),%al
L_002f10:
        adc    $0x1,%al
L_002f12:
        daa
L_002f13:
        mov    %al,0xd6(%di)

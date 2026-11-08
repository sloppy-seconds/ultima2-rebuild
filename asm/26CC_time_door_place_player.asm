# time_door_place_player — puts the player on the currently active time-door cell of the current
#           era: index `8*DS:49 + DS:6E` into the cell table at file 0x2998, stored to DS:00/DS:01.
# name-evidence: same table and same index arithmetic as asm/2708 and asm/2744, but the destination
#           is DS:00/DS:01 (the player x/y) where those two store to DS:23/DS:24 (a candidate cell).
#           That difference is what makes this the arrival placement rather than the rotation probe.
# func 26CC..26F1  len 0026 B  evidence: seed+reko
# calls: L_005cb1
# provenance: static (bytes read in full, 2026-11-08); the table it indexes is measured, 20 of 20
#           cells decoded and 12 confirmed live -- docs/moongates.md section 3.
# confidence: medium (the index arithmetic and the DS:00/01 store are bytes; that this runs on
#           arrival rather than on some other transition is inferred from the call graph)
# notes: Labels kept as L_<addr> on purpose (CP3 anchors); sub_26cc: is body.
sub_26cc:
L_0028cc:
        clc
L_0028cd:
        mov    0x49,%al
L_0028d0:
        .byte 0x02,0xc0  # forced-kept: gas re-encodes 'add    %al,%al' differently
L_0028d2:
        .byte 0x02,0xc0  # forced-kept: gas re-encodes 'add    %al,%al' differently
L_0028d4:
        .byte 0x02,0xc0  # forced-kept: gas re-encodes 'add    %al,%al' differently
L_0028d6:
        adc    0x6e,%al
L_0028da:
        mov    $0x0,%ah
L_0028dc:
        .byte 0x8b,0xf8  # forced-kept: gas re-encodes 'mov    %ax,%di' differently
L_0028de:
        mov    %cs:0x2798(%di),%al
L_0028e3:
        mov    %al,0x0
L_0028e6:
        inc    %di
L_0028e7:
        mov    %cs:0x2798(%di),%al
L_0028ec:
        mov    %al,0x1
L_0028ef:
        call L_005cb1

# 4223_world_eq9_gate — reads DS:6D and branches unless it equals 9. DS:6D is the world byte —
#           asm/7030 is its only writer and what it writes fixes the meaning — so this is a test on
#           which world the player is on, with 9 naming one of them.
# name-evidence: body only — `mov 0x6d,%al ; cmp $0x9,%al ; jne L_004446`. The DS:20 index it also
#           builds into DI is not used by the compared read, which is why this file reads as a
#           fragment rather than a whole routine.
# provenance: static (decoded from the body 2026-11-23); never executed in any capture.
# confidence: high for the comparison and for DS:6D being the world byte; none for which world 9 is.
#           Naming that would need the world table, not this file.
# func 4223..4231  len 000F B  evidence: seed+reko
# confidence: low (bytes proven by check_func.py)
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
sub_4223:
L_004423:
        mov    $0x0,%bh
L_004425:
        mov    0x20,%bl
L_004429:
        .byte 0x8b,0xfb  # forced-kept: gas re-encodes 'mov    %bx,%di' differently
L_00442b:
        mov    0x6d,%al
L_00442e:
        cmp    $0x9,%al
L_004430:
        jne L_004446

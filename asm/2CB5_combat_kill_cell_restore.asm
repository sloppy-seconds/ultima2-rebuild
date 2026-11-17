# combat_kill_minax_gate — the victory condition: a killed creature whose type is 0x40
# name-evidence: `mov 0x197(%di),%al ; cmp $0x40,%al ; jne L_002ed3`, where 0x197 is
#           mon_type[32] (state.h:433, with its own note that DI is the slot index) and 0x40
#           is Minax (asm/1040_minax_proximity_check, art 0x40>>2 == 0x10).
# RESOLVED 2026-11-09: this 24-byte stub is THE ENDGAME GATE, and it was unreadable for the
#           same reason asm/2D07 and cmd_I/cmd_D/cmd_K were: the Phase 2 map chopped the kill
#           path into unnamed fragments, and the victory entry is a FALL-THROUGH, not a call.
#             2CB5  mov $0x0,%bh ; mov $0x0,%bl ; mov %bx,%si
#             2EBB  mov 0x1b7(%di),%al        ; a per-creature field, monx[128..159];
#                   unnamed in state.h, so not asserted here
#             2EBF  mov 0x6,%bx
#             2EC3  mov %al,(%bx,%si)         ; write it back through the slot pointer
#             2EC5  mov 0x197(%di),%al        ; the creature's TYPE
#             2EC9  cmp $0x40,%al             ; Minax?
#             2ECB  jne L_002ed3              ; no  -> asm/2CD3, the ordinary kill-drop dispatch
#                   (fall through)            ; yes -> asm/2CCD, the victory call
#           So the two arms of a kill are Minax-or-not, and the Minax arm is three instructions
#           wide. Nothing else in the binary reaches the victory sequence.
# func 2CB5..2CCC  len 0018 B  evidence: seed+reko
# confidence: medium — the bytes and the compare are solid; the type-0x40-is-Minak identification
#           rests on the art index, and NO CAPTURE HAS EVER EXECUTED THIS PATH. See
#           docs/needs-gameplay.md for the capture that would close that gap.
# provenance: static. Read from the listing 2026-11-09; bytes proven by tools/check_func.py.
#           Never executed: the victory strings are discovered-but-not-executed in every harvest.
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
sub_2cb5:
L_002eb5:
        mov    $0x0,%bh
L_002eb7:
        mov    $0x0,%bl
L_002eb9:
        .byte 0x8b,0xf3  # forced-kept: gas re-encodes 'mov    %bx,%si' differently
L_002ebb:
        mov    0x1b7(%di),%al
L_002ebf:
        mov    0x6,%bx
L_002ec3:
        mov    %al,(%bx,%si)
L_002ec5:
        mov    0x197(%di),%al
L_002ec9:
        cmp    $0x40,%al
L_002ecb:
        jne L_002ed3

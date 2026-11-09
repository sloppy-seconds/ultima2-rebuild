# sub_2d22 — mechanical clean (bulk wave); behavior not yet reviewed
# func 2D22..2D8B  len 006A B  evidence: seed+reko
# calls: L_002f8c, L_005417
# provenance: static:bytes (both type branches read from the listing, function complete at 0x6A bytes) + live:play-test-live -- the HELM 1->2 and STAFF 1->2 kills in the sector-00 fight are these two branches, predicted before they were matched.
# confidence: low (bytes proven by check_func.py)
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
# RESOLVED 2026-11-09: this is the NON-THIEF arm of the kill-drop dispatch, reached by
#           `jne L_002f22` from asm/2CE5. Two type branches, and the function is complete
#           at 0x6A bytes with both accounted for:
#             0xF0 -> 25% HELM (DS:DB), then torches += (rand&3)+1   (asm/2D26..2D52)
#             0xF8 -> WAND or STAFF, 50/50                         (asm/2D64..2D7C)
#             else -> nothing; L_002f8b is a bare nop
#           Consistency check that this is read correctly and not merely plausibly:
#           asm/0AD3 makes 0xF8 the magic-missile caster, and a caster leaving a wand or
#           a staff agrees. Both live observations we already had are predicted by these
#           two branches -- the `HELM 1 -> 2` and `STAFF 1 -> 2` kills in the sector-00
#           fight. Renamed from sub_2d22 on the strength of this.
sub_2d22:
L_002f22:
        cmp    $0xf0,%al
L_002f24:
        jne L_002f60
L_002f26:
        .byte 0x8b,0xdf  # forced-kept: gas re-encodes 'mov    %di,%bx' differently
L_002f28:
        mov    %bl,0x1f
L_002f2c:
        call L_005417
L_002f2f:
        cmp    $0x40,%al
L_002f31:
        jae L_002f3d
L_002f33:
        clc
L_002f34:
        mov    0xdb,%al
L_002f37:
        adc    $0x1,%al
L_002f39:
        daa
L_002f3a:
        mov    %al,0xdb
L_002f3d:
        mov    $0x0,%bh
L_002f3f:
        mov    0x1f,%bl
L_002f43:
        .byte 0x8b,0xfb  # forced-kept: gas re-encodes 'mov    %bx,%di' differently
L_002f45:
        call L_005417
L_002f48:
        and    $0x3,%al
L_002f4a:
        adc    $0x1,%al
L_002f4c:
        clc
L_002f4d:
        adc    0x64,%al
L_002f51:
        daa
L_002f52:
        mov    %al,0x64
L_002f55:
        mov    $0x0,%bh
L_002f57:
        mov    0x1f,%bl
L_002f5b:
        .byte 0x8b,0xfb  # forced-kept: gas re-encodes 'mov    %bx,%di' differently
L_002f5d:
        jmp L_002f8c
L_002f5f:
        nop
L_002f60:
        cmp    $0xf8,%al
L_002f62:
        jne L_002f8b
L_002f64:
        .byte 0x8b,0xdf  # forced-kept: gas re-encodes 'mov    %di,%bx' differently
L_002f66:
        mov    %bl,0x1f
L_002f6a:
        call L_005417
L_002f6d:
        and    $0x1,%al
L_002f6f:
        mov    $0x0,%ah
L_002f71:
        .byte 0x8b,0xf8  # forced-kept: gas re-encodes 'mov    %ax,%di' differently
L_002f73:
        inc    %di
L_002f74:
        clc
L_002f75:
        mov    0xd6(%di),%al
L_002f79:
        adc    $0x1,%al
L_002f7b:
        daa
L_002f7c:
        mov    %al,0xd6(%di)
L_002f80:
        mov    $0x0,%bh
L_002f82:
        mov    0x1f,%bl
L_002f86:
        .byte 0x8b,0xfb  # forced-kept: gas re-encodes 'mov    %bx,%di' differently
L_002f88:
        jmp L_002f8c
L_002f8a:
        nop
L_002f8b:
        nop

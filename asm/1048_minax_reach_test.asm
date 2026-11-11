# minax_reach_test — the body of Minax's proximity check: |mon - player| < 4 on both
#           axes, then the strike. This is the fall-through from asm/1040 when
#           mon_type == 0x40 (asm/1040's `jne L_00126d` skips it for every other type).
# name-evidence: the instruction sequence, read 2026-11-12 while proving the CP8 1a fix
#           against monx03. asm/1040's header described this reach test but the actual
#           instructions live here, in the fragment asm/1040 falls through to:
#             1048  mov 0x137(%di),%al   ; al = mon_x[slot]
#             104c  stc ; cmc            ; clear CF
#             104e  sbb 0x0,%al          ; al = mon_x - DS:00  (DS:00 = player x, sector_x)
#             1052  call L_0052ef        ; magnitude of al (see helper caveat below)
#             1055  cmp $0x4,%al
#             1057  jae L_00126d         ; |mon_x - player_x| >= 4 -> next slot
#             1059  mov 0x157(%di),%al   ; al = mon_y[slot]
#             105f  sbb 0x1,%al          ; al = mon_y - DS:01  (DS:01 = player y, sector_y)
#             1063  call L_0052ef
#             1066  cmp $0x4,%al
#             1068  jae L_00126d         ; |mon_y - player_y| >= 4 -> next slot
#             106a  call L_00118e        ; = asm/0F8E_combat_minax_attack: the strike
#           So the reach is strictly < 4 on each axis, measured from the player's own cell
#           (DS:00/DS:01), and it is a per-axis test, not a radius. This is exactly what
#           engine/src/game/ai.c's minax_strike gate reproduces (abs8(px-mon_x) < 4 &&
#           abs8(py-mon_y) < 4), and it is the first execution-adjacent confirmation of
#           asm/1040's "up to four cells" claim: the threshold is a byte in these bytes.
# HELPER CAVEAT (open): the magnitude call target is L_0052ef (asm/52EF), whose committed
#           body (`mov %bx,%si ; mov $0x8,%bx ; sub %bx,%si`) does not touch %al, so it is
#           either mis-decoded by the bulk wave (asm/52EF is marked "not yet reviewed") or
#           the magnitude is produced by a path not shown here. asm/1040's header attributed
#           the abs to asm/50EF (the proven signed-byte abs). The `cmp $0x4 ; jae` after the
#           call is what pins the semantics regardless of which helper it is: the compared
#           value is a non-negative magnitude and the threshold is 4. The engine uses abs8
#           and < 4, which matches the threshold and the player-relative subtraction; the
#           exact helper identity is left open rather than asserted.
# NOT a teleport: this routine only strikes and falls to the next slot (L_00126d). It does
#           not write mon_x/mon_y, so Minax does not move here. See docs/cp8-minax-gaps.md
#           § 2 for the teleport claim and why the decoded paths do not support it.
# func 1048..106C  len 0025 B  evidence: seed+reko
# calls: L_00118e, L_0052ef
# provenance: static. Read from the listing 2026-11-12 while proving the CP8 1a fix against
#           monx03; bytes proven by tools/check_func.py. The reach test and the strike call
#           are bytes; the L_0052ef magnitude helper is flagged unidentified above. Never
#           executed in a harvest (the Minax fight is frames-only, no trace of this path).
# confidence: high for the reach test's shape and threshold (bytes read directly); low for
#           the L_0052ef helper's exact function (flagged above)
# label: kept as sub_1048 on purpose — labels are body and CP0 md5s them
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
sub_1048:
L_001248:
        mov    0x137(%di),%al
L_00124c:
        stc
L_00124d:
        cmc
L_00124e:
        sbb    0x0,%al
L_001252:
        call L_0052ef
L_001255:
        cmp    $0x4,%al
L_001257:
        jae L_00126d
L_001259:
        mov    0x157(%di),%al
L_00125d:
        stc
L_00125e:
        cmc
L_00125f:
        sbb    0x1,%al
L_001263:
        call L_0052ef
L_001266:
        cmp    $0x4,%al
L_001268:
        jae L_00126d
L_00126a:
        call L_00118e

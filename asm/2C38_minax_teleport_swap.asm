# minax_teleport_swap — Minax's teleport on a surviving hit: swap mon_x and mon_y,
#           restamp the grid, set mon_role to 0x84, print "SHE'S GONE!!!".
# name-evidence: reached from asm/2C2D_minax_hit_teleport_gate when the hit creature is
#           type 0x40. The body (file offsets):
#             2e38  call L_004d26 ; call L_0061a6 ; call L_004d26   ; save ops (see note)
#             2e49  al = mon_x ; DS:23 = al                         ; stage old x
#             2e50  al = mon_y ; DS:24 = al                         ; stage old y
#             2e57  call L_0052b3                                   ; grid ptr for old cell
#             2e5a  al = mon_kind ; [grid] = al                     ; clear old stamp (terrain)
#             2e64  mon_y = DS:23 (old x)                           ; SWAP
#             2e6b  mon_x = DS:24 (old y) ; DS:23 = al              ; SWAP
#             2e75  DS:24 = mon_y ; call L_0052b3                   ; grid ptr for new cell
#             2e7f  al = mon_type ; [grid] = al                     ; stamp new cell
#             2e89  mon_role = 0x84                                 ; Minax's role byte
#             2e8f  str_out "SHE'S GONE!!!"                         ; leading 0x8d newline
#             2ea1  jmp L_000c33                                    ; return to command loop
#           So the "teleport between the NE and SW chambers" is a coordinate SWAP: her
#           spawn (60,3) in monx03 becomes (3,60), and a second hit swaps it back -- the
#           oscillation the owner saw. It is not a fixed destination and not a random jump.
# PROVEN LIVE 2026-11-12: seeded adjacent to Minax in sector 03, one QUICK SWORD hit moved
#           slot 31 from (60,3) to (3,60) (mon_x 0x3c->0x03, mon_y 0x03->0x3c), HP 255->199,
#           and the swap is exactly what the bytes do. Retracts the first static pass that
#           claimed no teleport exists.
# L_0061a6 (the middle save call): this is the same helper that sits beside the inline
#           "MAPX30  " filename in asm/72A0's ending (G75a: sector save or screen call?).
#           Here it runs on a teleport, between two L_004d26 calls, and the teleport moves
#           Minax's grid cell -- so this is evidence that L_0061a6 is a sector SAVE (persist
#           the world after mutating it), not a screen call. Strengthens the G75a answer
#           toward "save"; still not byte-proven (L_0061a6's own body is unread).
# func 2C38..2CA3  len 006C B  evidence: seed+reko
# calls: L_000c33, L_004d26, L_0051df (str_out), L_0052b3, L_0061a6
# provenance: static, decoded 2026-11-12 while confirming the live teleport; bytes proven by
#           tools/check_func.py. The string "SHE'S GONE!!!" is committed as U2_S_SHE_GONE.
# confidence: high for the swap/restamp/role/message (bytes read directly + live-confirmed);
#           medium for the L_0061a6 = sector-save reading (corroborating, not proven)
# label: kept as sub_2c38 on purpose — labels are body and CP0 md5s them
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
sub_2c38:
L_002e38:
        call L_004d26
L_002e3b:
        call L_0061a6
L_002e3e:
        call L_004d26
L_002e41:
        mov    $0x0,%bh
L_002e43:
        mov    0x20,%bl
L_002e47:
        .byte 0x8b,0xfb  # forced-kept: gas re-encodes 'mov    %bx,%di' differently
L_002e49:
        mov    0x137(%di),%al
L_002e4d:
        mov    %al,0x23
L_002e50:
        mov    0x157(%di),%al
L_002e54:
        mov    %al,0x24
L_002e57:
        call L_0052b3
L_002e5a:
        mov    0x1b7(%di),%al
L_002e5e:
        mov    0x6,%bx
L_002e62:
        mov    %al,(%bx,%si)
L_002e64:
        mov    0x23,%al
L_002e67:
        mov    %al,0x157(%di)
L_002e6b:
        mov    0x24,%al
L_002e6e:
        mov    %al,0x137(%di)
L_002e72:
        mov    %al,0x23
L_002e75:
        mov    0x157(%di),%al
L_002e79:
        mov    %al,0x24
L_002e7c:
        call L_0052b3
L_002e7f:
        mov    0x197(%di),%al
L_002e83:
        mov    0x6,%bx
L_002e87:
        mov    %al,(%bx,%si)
L_002e89:
        mov    $0x84,%al
L_002e8b:
        mov    %al,0x1d7(%di)
L_002e8f:
        call L_0051df
        db 0x8d,"SHE'S GONE!!!",0x00        # string
L_002ea1:
        jmp L_000c33

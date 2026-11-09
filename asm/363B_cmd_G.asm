# cmd_G — command handler 'G' (GET): prints "GET", reads key byte DS:004A;
#           '*' -> ARMOUR path, ',' -> WEAPON path, else " WHAT?";
#           WEAPON: sub_361c + sub_5217 random pick, BCD-increments weapon slot
#           [di+0x76]; EMPTY when roll fails; '@' -> CHEST path ("CHEST! IT
#           CONTAINS ", gold / TRI-LITHIUM handling); returns to dispatcher 0xc33.
# func 363B..3781  len 0147 B  evidence: seed+reko (jump table cs:0730 entry G)
# provenance: live:airport32 (+1 other harvests), static
#           entry executed in 2 of 214 harvests; derived by tools/provenance_from_traces.py
# confidence: medium (flow + messages proven by bytes; NPC/quest semantics inferred)
# RESOLVED 2026-11-09: the town/site arm is decoded, and the earlier note blaming "asm/361C's
#           unread table" was wrong. asm/361C (L_00381c, filed inside asm/3611) is the CELL
#           CLEARING helper -- `cmpb $0x4,0x4a`, then stamp 0x08 land over the tile via
#           L_0052b3 -- and the item index comes from the roll that follows it. The arm is:
#             DS:4A < 4 selects the town path; the cell under the player is then
#             0x2a -> " ARMOUR", rand & 3   -> armour_owned[idx] += 1   (DS:96+idx)
#             0x2c -> " WEAPON", rand & 7   -> weapons_owned[idx] += 1  (DS:76+idx)
#             idx == 0 -> " EMPTY!" ; anything else -> " WHAT?"
#           The clear happens BEFORE the roll, so an empty chest is still consumed.
#           Weapon indices 1..7 are DAGGER..LIGHT SWORD, which is what the third-party guide
#           claims independently ("up to the light sword (n.7)"); index 7 of the pool really
#           is LIGHT SWORD, and PHASER (8) and QUICK SWORD (9) are excluded. Armour 1..3 is
#           CLOTH, LEATHER, CHAIN, so PLATE, REFLECT and POWER are never chest loot.
#           Reachability, measured: 0x2c occurs at seven cells in the shipped maps (mapx31
#           (4,23),(4,60),(4,61); mapx32 (26,43),(26,44); mapx41 (16,35),(25,41)) and 0x2a
#           occurs in NO mapx file -- see original/updated/GCHANGES.TXT on the CD dump
#           overwriting same-named Galactic-disk files.
# notes: re-decoded with tools/decode_str.py (str_out-aware); gas re-encode diffs
#        kept as .byte (disp8/disp16 and reg-op form choices)
cmd_G:
L_00383b:
        call L_0051df
        db "GET",0x00        # string
L_003842:
        .byte 0xa0,0x4a,0x00  # forced-kept: gas re-encodes 'mov    0x4a,%al' differently
L_003845:
        cmp    $0x4,%al
L_003847:
        jb L_00384c
L_003849:
        jmp L_0038c3
L_00384b:
        nop
L_00384c:
        nop
L_00384d:
        mov    0x14,%al
L_003850:
        cmp    $0x2a,%al
L_003852:
        je L_00389b
L_003854:
        cmp    $0x2c,%al
L_003856:
        je L_003865
L_003858:
        call L_0051df
        db " WHAT?",0x00        # string
L_003862:
        jmp L_000c33
L_003865:
        call L_0051df
        db " WEAPON",0x00        # string
L_003870:
        call L_00381c
L_003873:
        call L_005417
L_003876:
        .byte 0x24,0x07  # forced-kept: gas re-encodes 'and    $0x7,%al' differently
L_003878:
        je L_00388d
L_00387a:
        mov    $0x0,%ah
L_00387c:
        .byte 0x8b,0xf8  # forced-kept: gas re-encodes 'mov    %ax,%di' differently
L_00387e:
        clc
L_00387f:
        .byte 0x8a,0x85,0x76,0x00  # forced-kept: gas re-encodes 'mov    0x76(%di),%al' differently
L_003883:
        adc    $0x1,%al
L_003885:
        daa
L_003886:
        .byte 0x88,0x85,0x76,0x00  # forced-kept: gas re-encodes 'mov    %al,0x76(%di)' differently
L_00388a:
        jmp L_000c33
L_00388d:
        call L_0051df
        db " EMPTY!",0x00        # string
L_003898:
        jmp L_000c33
L_00389b:
        call L_0051df
        db " ARMOUR",0x00        # string
L_0038a6:
        call L_00381c
L_0038a9:
        call L_005417
L_0038ac:
        and    $0x3,%al
L_0038ae:
        je L_00388d
L_0038b0:
        mov    $0x0,%ah
L_0038b2:
        .byte 0x8b,0xf8  # forced-kept: gas re-encodes 'mov    %ax,%di' differently
L_0038b4:
        clc
L_0038b5:
        mov    0x96(%di),%al
L_0038b9:
        adc    $0x1,%al
L_0038bb:
        daa
L_0038bc:
        mov    %al,0x96(%di)
L_0038c0:
        jmp L_000c33
L_0038c3:
        nop
L_0038c4:
        mov    0x14,%al
L_0038c7:
        cmp    $0x40,%al
L_0038c9:
        je L_0038d8
L_0038cb:
        call L_0051df
        db " WHAT?",0x00        # string
L_0038d5:
        jmp L_000c33
L_0038d8:
        nop
L_0038d9:
        call L_0051df
        db " CHEST!",0x8d,"IT CONTAINS ",0x00        # string
L_0038f1:
        mov    0x0,%al
L_0038f4:
        mov    %al,0x23
L_0038f7:
        mov    0x1,%al
L_0038fa:
        mov    %al,0x24
L_0038fd:
        mov    0x25,%al
L_003900:
        call L_001651
L_003903:
        mov    $0x0,%al
L_003905:
        mov    0x8,%bx
L_003909:
        mov    %al,(%bx,%si)
L_00390b:
        mov    0x25,%al
L_00390e:
        cmp    $0xf,%al
L_003910:
        je L_00395a
L_003912:
        call L_005417
L_003915:
        cmp    $0x40,%al
L_003917:
        jb L_003946
L_003919:
        call L_0051df
        db "GOLD!",0x00        # string
L_003922:
        call L_005417
L_003925:
        and    $0x1f,%al
L_003927:
        adc    0x25,%al
L_00392b:
        adc    0x25,%al
L_00392f:
        and    $0x77,%al
L_003931:
        clc
L_003932:
        adc    0x59,%al
L_003936:
        daa
L_003937:
        mov    %al,0x59
L_00393a:
        mov    0x58,%al
L_00393d:
        adc    $0x0,%al
L_00393f:
        daa
L_003940:
        mov    %al,0x58
L_003943:
        jmp L_000c33
L_003946:
        mov    0x25,%al
L_003949:
        clc
L_00394a:
        rcr    $1,%al
L_00394c:
        jae L_003951
L_00394e:
        jmp L_00389b
L_003951:
        call L_0051df
        db "A ",0x00        # string
L_003957:
        jmp L_003865
L_00395a:
        call L_0051df
        db "TRI-LITHIUM!",0x00        # string
L_00396a:
        clc
L_00396b:
        mov    0xe5,%al
L_00396e:
        adc    $0x1,%al
L_003970:
        daa
L_003971:
        mov    %al,0xe5
L_003974:
        jmp L_000c33
        db 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00        # tail-pad

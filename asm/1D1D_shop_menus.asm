# shop_menus — the armour, weapons and transport catalogues, in one Phase 2 fragment
# name-evidence: strings '     THE ARMOUR SHOPPE:' / '  THE WEAPONS SHOPPE:' /
#           'WELCOME, TO THE TRANSPORT'
# func 1D1D..2027  len 030B B  evidence: seed+reko; re-cut 2026-11-15 from
#           asm/19D7_magic_shop_menu.asm
# note: THREE ROUTINES in one tiling fragment, and the boundary is a Phase 2 artefact and not a
#           property of the game. 1D1D is the armour catalogue, asm/1E4F is the weapons
#           catalogue — the same routine twice, which is why the engine implements them as one
#           function with a parameter — and asm/1F75 is the transport shoppe. The name covers
#           all three because the map's tiling puts all three in this range; cite the per-routine
#           entry point, not this file, when naming one of them.
# note: SCOPE, and now observed. Live 2026-10-29 (captures/spice86/shop-window-transport,
#           owner-played, wizard BUYTEST):
#           * **Buying a horse sets the travel mode directly.** DS:13 went 0x7A (on foot, class
#             2) to 0x22 (HORSE) as part of the purchase, with no tile stamped under the player
#             (the cell stayed 0x70 town floor) and no inventory write at all — all 16 item
#             slots and all nine spell counts were unchanged. This is the only route found so
#             far where the mode changes WITHOUT cmd_B: BOARD consumes a tile and derives the
#             mode from the tile under the player, and the transport shoppe skips the tile
#             entirely. Any vehicle state machine modelled only on BOARD will miss this branch.
#           * **The purchase happens across the shop's sign band.** Player (48,53), vendor on
#             the 0x68 marker at (48,51), the "TRANSPORT" lettering at y=52 between them. See
#             the correction in asm/4050_cmd_T.asm.
#           * **Prices here are not one mechanism.** Horse 32 gp matches the reachable output of
#             asm/20AB_bcd_price_from_stats at caller_AL=4; axe 25 gp cannot be produced by that
#             routine at any index or stat pair. So the weapon branch has a separate price path,
#             probably a table, and it has not been read.
# provenance: static
#           the entry never appears in ExecutedInstructions of any harvest; the observations
#           above are live play with frames, not harvested execution.
# confidence: low (bytes proven by check_func.py)
# split-from: asm/19D7_magic_shop_menu.asm (19D7..2027)
# boundary: this file starts at a DISPATCH TARGET, not a return. Runtime 0x1d1d is reached by a
#           direct `jmp` from file 0x17b4, inside the shop selector, which is what makes it a
#           boundary the bytes assert rather than one inferred from a fall-through — the
#           distinction docs/g22-split-proposal.md § 3 turns on, and the reason the armour,
#           weapons and transport catalogues stay in ONE file here: 1E4F and 1F75 are also jmp
#           targets, but 1D39, 1E69 and 1F93 are fall-throughs, and splitting on those would
#           invent boundaries the original does not contain.
# body corrected: 2026-11-15 -- evidence: new file, and its body is byte-for-byte the tail of
#           asm/19D7_magic_shop_menu.asm from runtime 0x1d1d. The re-cut put the clue pool that
#           sat in front of it (asm/1B6B_clue_pool.asm) into its own file, so this content is
#           new to the stream only in the sense that it now has a file of its own; no byte moved
#           and the split is byte-neutral. tools/assemble_clean.sh is the authority, not this
#           marker.
# refs: L_0051df, L_002228, L_001f8f, L_001fad, L_000c33, L_00528c, L_0022ab, L_0053a5, L_00514d, L_002001, L_0055f4, L_00202b, L_002252, L_0020b5, L_0020d3, L_002127, L_002151, L_0021d9, L_002207  (labels named by visible mnemonics; forced-keep bytes can hide more)
L_001f1d:
        .byte 0x90  # forced-kept: gas re-encodes 'nop' differently
L_001f1e:
        call L_0051df
        db "     THE ARMOUR SHOPPE:",0x0d,"1-CLOTH, 2-LEATHER, 3-CHAIN,",0x0d,"4-PLATE, 5-REFLECT, 6-POWER,",0x0d,"YOUR INTEREST? ",0x00        # string
L_001f83:
        call L_002228
L_001f86:
        je L_001f8f
L_001f88:
        .byte 0x3c,0x07  # forced-kept: gas re-encodes 'cmp    $0x7,%al' differently
L_001f8a:
        jae L_001f8f
L_001f8c:
        jmp L_001fad
L_001f8e:
        .byte 0x90  # forced-kept: gas re-encodes 'nop' differently
L_001f8f:
        call L_0051df
        db "THANKS FOR COMING BY...",0x00        # string
L_001faa:
        jmp L_000c33
L_001fad:
        .byte 0xa2,0x1f,0x00  # forced-kept: gas re-encodes 'mov    %al,0x1f' differently
L_001fb0:
        call L_0051df
        db "AH! YES! ",0x00        # string
L_001fbd:
        .byte 0xa0,0x1f,0x00  # forced-kept: gas re-encodes 'mov    0x1f,%al' differently
L_001fc0:
        .byte 0xf8  # forced-kept: gas re-encodes 'clc' differently
L_001fc1:
        .byte 0x14,0x1d  # forced-kept: gas re-encodes 'adc    $0x1d,%al' differently
L_001fc3:
        call L_00528c
L_001fc6:
        call L_0051df
        db 0x0d,"FOR YOU ONLY ",0x00        # string
L_001fd8:
        .byte 0xa0,0x1f,0x00  # forced-kept: gas re-encodes 'mov    0x1f,%al' differently
L_001fdb:
        .byte 0x02,0xc0  # forced-kept: gas re-encodes 'add    %al,%al' differently
L_001fdd:
        call L_0022ab
L_001fe0:
        .byte 0xa0,0x27,0x00  # forced-kept: gas re-encodes 'mov    0x27,%al' differently
L_001fe3:
        .byte 0xb7,0x00  # forced-kept: gas re-encodes 'mov    $0x0,%bh' differently
L_001fe5:
        .byte 0x8a,0x1e,0x28,0x00  # forced-kept: gas re-encodes 'mov    0x28,%bl' differently
L_001fe9:
        .byte 0x8b,0xfb  # forced-kept: gas re-encodes 'mov    %bx,%di' differently
L_001feb:
        call L_0053a5
L_001fee:
        call L_0051df
        db 0x0d,"HOW 'BOUT IT? ",0x00        # string
L_002001:
        call L_00514d
L_002004:
        .byte 0x80,0xfc,0xff  # forced-kept: gas re-encodes 'cmp    $0xff,%ah' differently
L_002007:
        jne L_002001
L_002009:
        .byte 0xa2,0x21,0x00  # forced-kept: gas re-encodes 'mov    %al,0x21' differently
L_00200c:
        call L_0055f4
L_00200f:
        .byte 0xb0,0x8d  # forced-kept: gas re-encodes 'mov    $0x8d,%al' differently
L_002011:
        call L_0055f4
L_002014:
        .byte 0xa0,0x21,0x00  # forced-kept: gas re-encodes 'mov    0x21,%al' differently
L_002017:
        .byte 0x3c,0x59  # forced-kept: gas re-encodes 'cmp    $0x59,%al' differently
L_002019:
        je L_00202b
L_00201b:
        call L_0051df
        db "OH, WELL.",0x00        # string
L_002028:
        jmp L_000c33
L_00202b:
        .byte 0x90  # forced-kept: gas re-encodes 'nop' differently
L_00202c:
        call L_002252
L_00202f:
        call L_0051df
        db "SOLD!",0x00        # string
L_002038:
        .byte 0xb7,0x00  # forced-kept: gas re-encodes 'mov    $0x0,%bh' differently
L_00203a:
        .byte 0x8a,0x1e,0x1f,0x00  # forced-kept: gas re-encodes 'mov    0x1f,%bl' differently
L_00203e:
        .byte 0x8b,0xfb  # forced-kept: gas re-encodes 'mov    %bx,%di' differently
L_002040:
        .byte 0x8a,0x85,0x96,0x00  # forced-kept: gas re-encodes 'mov    0x96(%di),%al' differently
L_002044:
        .byte 0xf8  # forced-kept: gas re-encodes 'clc' differently
L_002045:
        .byte 0x14,0x01  # forced-kept: gas re-encodes 'adc    $0x1,%al' differently
L_002047:
        .byte 0x27  # forced-kept: gas re-encodes 'daa' differently
L_002048:
        .byte 0x88,0x85,0x96,0x00  # forced-kept: gas re-encodes 'mov    %al,0x96(%di)' differently
L_00204c:
        jmp L_000c33
L_00204f:
        .byte 0x90  # forced-kept: gas re-encodes 'nop' differently
L_002050:
        call L_0051df
        db "  THE WEAPONS SHOPPE:",0x0d,"1-DA, 2-MA, 3-AX, 4-BO,",0x0d,"5-SW, 6-GR, 7-LI, 8-PH.",0x0d,"YOUR INTEREST? ",0x00        # string
L_0020a9:
        call L_002228
L_0020ac:
        je L_0020b5
L_0020ae:
        .byte 0x3c,0x09  # forced-kept: gas re-encodes 'cmp    $0x9,%al' differently
L_0020b0:
        je L_0020b5
L_0020b2:
        jmp L_0020d3
L_0020b4:
        .byte 0x90  # forced-kept: gas re-encodes 'nop' differently
L_0020b5:
        call L_0051df
        db "THANKS FOR COMING BY...",0x00        # string
L_0020d0:
        jmp L_000c33
L_0020d3:
        .byte 0xa2,0x1f,0x00  # forced-kept: gas re-encodes 'mov    %al,0x1f' differently
L_0020d6:
        call L_0051df
        db "AH! YES! A ",0x00        # string
L_0020e5:
        .byte 0xa0,0x1f,0x00  # forced-kept: gas re-encodes 'mov    0x1f,%al' differently
L_0020e8:
        .byte 0xf8  # forced-kept: gas re-encodes 'clc' differently
L_0020e9:
        .byte 0x14,0x13  # forced-kept: gas re-encodes 'adc    $0x13,%al' differently
L_0020eb:
        call L_00528c
L_0020ee:
        call L_0051df
        db 0x0d,"FOR YOU ONLY ",0x00        # string
L_002100:
        .byte 0xa0,0x1f,0x00  # forced-kept: gas re-encodes 'mov    0x1f,%al' differently
L_002103:
        call L_0022ab
L_002106:
        .byte 0xa0,0x27,0x00  # forced-kept: gas re-encodes 'mov    0x27,%al' differently
L_002109:
        .byte 0xb7,0x00  # forced-kept: gas re-encodes 'mov    $0x0,%bh' differently
L_00210b:
        .byte 0x8a,0x1e,0x28,0x00  # forced-kept: gas re-encodes 'mov    0x28,%bl' differently
L_00210f:
        .byte 0x8b,0xfb  # forced-kept: gas re-encodes 'mov    %bx,%di' differently
L_002111:
        call L_0053a5
L_002114:
        call L_0051df
        db 0x0d,"HOW 'BOUT IT? ",0x00        # string
L_002127:
        call L_00514d
L_00212a:
        .byte 0x80,0xfc,0xff  # forced-kept: gas re-encodes 'cmp    $0xff,%ah' differently
L_00212d:
        jne L_002127
L_00212f:
        .byte 0xa2,0x21,0x00  # forced-kept: gas re-encodes 'mov    %al,0x21' differently
L_002132:
        call L_0055f4
L_002135:
        .byte 0xb0,0x8d  # forced-kept: gas re-encodes 'mov    $0x8d,%al' differently
L_002137:
        call L_0055f4
L_00213a:
        .byte 0xa0,0x21,0x00  # forced-kept: gas re-encodes 'mov    0x21,%al' differently
L_00213d:
        .byte 0x3c,0x59  # forced-kept: gas re-encodes 'cmp    $0x59,%al' differently
L_00213f:
        je L_002151
L_002141:
        call L_0051df
        db "OH, WELL.",0x00        # string
L_00214e:
        jmp L_000c33
L_002151:
        .byte 0x90  # forced-kept: gas re-encodes 'nop' differently
L_002152:
        call L_002252
L_002155:
        call L_0051df
        db "SOLD!",0x00        # string
L_00215e:
        .byte 0xb7,0x00  # forced-kept: gas re-encodes 'mov    $0x0,%bh' differently
L_002160:
        .byte 0x8a,0x1e,0x1f,0x00  # forced-kept: gas re-encodes 'mov    0x1f,%bl' differently
L_002164:
        .byte 0x8b,0xfb  # forced-kept: gas re-encodes 'mov    %bx,%di' differently
L_002166:
        .byte 0x8a,0x85,0x76,0x00  # forced-kept: gas re-encodes 'mov    0x76(%di),%al' differently
L_00216a:
        .byte 0xf8  # forced-kept: gas re-encodes 'clc' differently
L_00216b:
        .byte 0x14,0x01  # forced-kept: gas re-encodes 'adc    $0x1,%al' differently
L_00216d:
        .byte 0x27  # forced-kept: gas re-encodes 'daa' differently
L_00216e:
        .byte 0x88,0x85,0x76,0x00  # forced-kept: gas re-encodes 'mov    %al,0x76(%di)' differently
L_002172:
        jmp L_000c33
L_002175:
        .byte 0x90  # forced-kept: gas re-encodes 'nop' differently
L_002176:
        call L_0051df
        db "WELCOME, TO THE TRANSPORT",0x0d,"SHOPPE. I SELL YOU HORSE ",0x00        # string
L_0021ad:
        .byte 0xb0,0x04  # forced-kept: gas re-encodes 'mov    $0x4,%al' differently
L_0021af:
        call L_0022ab
L_0021b2:
        .byte 0xa0,0x27,0x00  # forced-kept: gas re-encodes 'mov    0x27,%al' differently
L_0021b5:
        .byte 0xb7,0x00  # forced-kept: gas re-encodes 'mov    $0x0,%bh' differently
L_0021b7:
        .byte 0x8a,0x1e,0x28,0x00  # forced-kept: gas re-encodes 'mov    0x28,%bl' differently
L_0021bb:
        .byte 0x8b,0xfb  # forced-kept: gas re-encodes 'mov    %bx,%di' differently
L_0021bd:
        call L_0053a5
L_0021c0:
        call L_0051df
        db 0x0d,"GOLD, DEAL, OK? --- ",0x00        # string
L_0021d9:
        call L_00514d
L_0021dc:
        .byte 0x80,0xfc,0xff  # forced-kept: gas re-encodes 'cmp    $0xff,%ah' differently
L_0021df:
        jne L_0021d9
L_0021e1:
        .byte 0x50  # forced-kept: gas re-encodes 'push   %ax' differently
L_0021e2:
        call L_0055f4
L_0021e5:
        .byte 0xb0,0x8d  # forced-kept: gas re-encodes 'mov    $0x8d,%al' differently
L_0021e7:
        call L_0055f4
L_0021ea:
        .byte 0x58  # forced-kept: gas re-encodes 'pop    %ax' differently
L_0021eb:
        .byte 0x3c,0x59  # forced-kept: gas re-encodes 'cmp    $0x59,%al' differently
L_0021ed:
        je L_002207
L_0021ef:
        call L_0051df
        db 0x0d,"O.K. BYE THEN...",0x00        # string
L_002204:
        jmp L_000c33
L_002207:
        call L_002252
L_00220a:
        call L_0051df
        db 0x8d,"RIDE SWIFTLY!",0x00        # string
L_00221c:
        .byte 0xb0,0x22  # forced-kept: gas re-encodes 'mov    $0x22,%al' differently
L_00221e:
        .byte 0xa2,0x13,0x00  # forced-kept: gas re-encodes 'mov    %al,0x13' differently
L_002221:
        jmp L_000c33
L_002224:
        .byte 0x90  # forced-kept: gas re-encodes 'nop' differently
L_002225:
        jmp L_000c33

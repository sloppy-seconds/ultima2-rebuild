# magic_shop_menu — magic shop spell catalogue
# name-evidence: string '1-LIGHT, 2-...' spell list + wizard gibberish
# func 19D7..2027  len 0651 B  evidence: seed+reko
# strings: " MUSERREF OLDUM!"; "FIVE "; "S FOR "; "NO!" (+14 more)
# calls: L_000c33, L_00175d, L_001c3a, L_001c4f, L_001fad, L_0020d3, L_002228, L_002252 (+6 more)
# provenance: static
#           entry never appears in ExecutedInstructions of any of the 60 harvests; derived by tools/provenance_from_traces.py
# confidence: low (bytes proven by check_func.py)
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
# CLUE POOL, decoded 2026-11-15, and it is the reason this file's name is too narrow. The span
#           1D6A..1F1C (file offsets; the labels L_001d6a..L_001f1b) is NOT code. The disassembler
#           ran off the end of the clue seller's menu and drew ten NUL-separated strings as
#           invented instructions -- every `.byte 0x??  # forced-kept: gas re-encodes 'add
#           %al,0x53(%bx,%di)' differently` from L_001d6a to L_001f1b is text, not code. Same
#           failure already recorded for asm/4C50 and, in its own header, for this table's sibling
#           asm/16BD_barkeep_chatter_hints.asm.
#           Reachability, read from the bytes at file 1D3D..1D68 (labels L_001d3c..L_001d67):
#             call read_decimal_digit (asm/2028, returns one digit 0..9)
#             mov %al,0x1f / mov %al,0x27 / mov %al,0x28   DS:1F = digit; DS:27:28 = digit
#             call 0x2253                                  the gold charge (digit x 100 by
#                                                          placement, which is what the menu's
#                                                          own text says: HOW MUCH WILL YOU PAY *100?)
#             mov 0x1f,%bl ; mov %bx,%si ; inc %si         SI = digit + 1
#             lea %cs:0x1b6a,%ax ; mov %ax,0x27            cursor = runtime 0x1b6a = FILE 0x1d6a
#             mov %bx,%di (DI=0) ; call L_00175d           the walker, asm/155D
#           The cursor lands on the NUL at file 0x1d6a, one byte before entry 0 at 0x1d6b -- the
#           same off-by-one-by-design the sibling table documents. Ten digits (0..9) for ten
#           entries, so digit N buys entry N-1 and digit 0 is free. NOTE the 0x200 skew that has
#           tripped this tree twice: `lea` operands in bodies are RUNTIME addresses, L_<addr>
#           labels are FILE offsets, and runtime + 0x200 = file.
# strings (entry index, then the text; 0x8d is the in-string carriage return the emitter takes):
#           0 "ASK ME NO QUESTIONS,\x8dI'LL TELL YOU NO LIES."
#           1 "THE QUEEN IS THE KING.\x8dAND THE KING IS A SPY."
#           2 "THE EVIL MEN DO.\x8dIS A HORRIBLE THING."
#           3 "BUT DISPELLING MEANS\x8dTHAT YOU MUST HAVE THE RING."
#           4 "JUST WHERE IT IS,\x8dI CANNOT SAY."
#           5 "BUT I'LL TELL YOU RIGHT NOW,\x8dTHERE IS NO OTHER WAY."
#           6 "I HEAR IN A TOWN\x8dWHERE THE WATER RUNS FREE."
#           7 "THERE IS AN OLD MAN\x8dLIVING UNDER A TREE."
#           8 "HE HAS NOT A NAME\x8dBUT HE DOES HAVE A CLUE"
#           9 "MAYHAPS IF YOU FIND HIM,\x8dHE'LL TELL IT TO YOU!"
#           Entry 2 reads oddly and is left exactly as the bytes have it. Entry 8 has no trailing
#           period in the original. Neither is corrected: the bytes are the text.
# note: no asm file owns 1D6A, so this is recorded in the owning function's header per AGENTS § 3.
#           A re-cut into asm/1D6A_clue_pool.asm is the right end state (asm/16BD is the precedent,
#           split-from asm/15E1) but it needs a row in the function map that assemble_clean.sh
#           orders by, so it is a separate change with the full byte proof behind it.
# SCOPE WIDER THAN THE NAME, and now observed. This span (19D7..2027) carries several shoppe
#           menus, not only the magic one: the TRANSPORT and WEAPONS text sits in this same file.
#           Live 2026-10-29 (captures/spice86/shop-window-transport, owner-played, wizard BUYTEST):
#           * **Buying a horse sets the travel mode directly.** DS:13 went 0x7A (on foot, class 2)
#             to 0x22 (HORSE) as part of the purchase, with no tile stamped under the player (the
#             cell stayed 0x70 town floor) and no inventory write at all — all 16 item slots and all
#             nine spell counts were unchanged. This is the only route found so far where the mode
#             changes WITHOUT cmd_B: BOARD consumes a tile and derives the mode from the tile under
#             the player, and the transport shoppe skips the tile entirely. Any vehicle state
#             machine modelled only on BOARD will miss this branch.
#           * **The purchase happens across the shop's sign band.** Player (48,53), vendor on the
#             0x68 marker at (48,51), the "TRANSPORT" lettering at y=52 between them. See the
#             correction in asm/4050_cmd_T.asm.
#           * **Prices here are not one mechanism.** Horse 32 gp matches the reachable output of
#             asm/20AB_bcd_price_from_stats at caller_AL=4; axe 25 gp cannot be produced by that
#             routine at any index or stat pair. So the weapon branch of this file has a separate
#             price path, probably a table, and it has not been read.
#           Confidence on the file as a whole stays low — none of this was executed in a harvested
#           capture; it is live-play observation with frames, and the entry line above ("never
#           appears in ExecutedInstructions") predates the corpus growth and refers to the harvest
#           set, not to today.
magic_shop_menu:
L_001bd7:
        .byte 0x90  # forced-kept: gas re-encodes 'nop' differently
L_001bd8:
        call L_0051df
        db " MUSERREF OLDUM!",0x8d,"1-LIGHT, 2-L.D., 3-L.U.,",0x8d,"4-M.M., 5-BLINK, 6-KILL,",0x8d,"BIR IKI UC...? ",0x00        # string
L_001c2e:
        call L_002228
L_001c31:
        je L_001c3a
L_001c33:
        .byte 0x3c,0x07  # forced-kept: gas re-encodes 'cmp    $0x7,%al' differently
L_001c35:
        jae L_001c3a
L_001c37:
        jmp L_001c4f
L_001c39:
        .byte 0x90  # forced-kept: gas re-encodes 'nop' differently
L_001c3a:
        call L_0051df
        db 0x8d,"UGURLA OLSUN!",0x00        # string
L_001c4c:
        jmp L_000c33
L_001c4f:
        .byte 0x3c,0x04  # forced-kept: gas re-encodes 'cmp    $0x4,%al' differently
L_001c51:
        jb L_001c56
L_001c53:
        .byte 0xf8  # forced-kept: gas re-encodes 'clc' differently
L_001c54:
        .byte 0x14,0x03  # forced-kept: gas re-encodes 'adc    $0x3,%al' differently
L_001c56:
        .byte 0xa2,0x1f,0x00  # forced-kept: gas re-encodes 'mov    %al,0x1f' differently
L_001c59:
        call L_0051df
        db "FIVE ",0x00        # string
L_001c62:
        .byte 0xa0,0x1f,0x00  # forced-kept: gas re-encodes 'mov    0x1f,%al' differently
L_001c65:
        .byte 0xf8  # forced-kept: gas re-encodes 'clc' differently
L_001c66:
        .byte 0x14,0x24  # forced-kept: gas re-encodes 'adc    $0x24,%al' differently
L_001c68:
        call L_00528c
L_001c6b:
        call L_0051df
        db "S FOR ",0x00        # string
L_001c75:
        .byte 0xa0,0x1f,0x00  # forced-kept: gas re-encodes 'mov    0x1f,%al' differently
L_001c78:
        call L_0022ab
L_001c7b:
        .byte 0xa0,0x27,0x00  # forced-kept: gas re-encodes 'mov    0x27,%al' differently
L_001c7e:
        .byte 0xb7,0x00  # forced-kept: gas re-encodes 'mov    $0x0,%bh' differently
L_001c80:
        .byte 0x8a,0x1e,0x28,0x00  # forced-kept: gas re-encodes 'mov    0x28,%bl' differently
L_001c84:
        .byte 0x8b,0xfb  # forced-kept: gas re-encodes 'mov    %bx,%di' differently
L_001c86:
        call L_0053a5
L_001c89:
        call L_0051df
        db 0x8d,"LUTFEN EVET? ",0x00        # string
L_001c9b:
        call L_00514d
L_001c9e:
        .byte 0x80,0xfc,0xff  # forced-kept: gas re-encodes 'cmp    $0xff,%ah' differently
L_001ca1:
        jne L_001c9b
L_001ca3:
        .byte 0x3c,0x59  # forced-kept: gas re-encodes 'cmp    $0x59,%al' differently
L_001ca5:
        je L_001cb0
L_001ca7:
        call L_0051df
        db "NO!",0x00        # string
L_001cae:
        jmp L_001c3a
L_001cb0:
        call L_0051df
        db "YES",0x8d,0x00        # string
L_001cb8:
        call L_002252
L_001cbb:
        .byte 0xf8  # forced-kept: gas re-encodes 'clc' differently
L_001cbc:
        .byte 0xb7,0x00  # forced-kept: gas re-encodes 'mov    $0x0,%bh' differently
L_001cbe:
        .byte 0x8a,0x1e,0x1f,0x00  # forced-kept: gas re-encodes 'mov    0x1f,%bl' differently
L_001cc2:
        .byte 0x8b,0xfb  # forced-kept: gas re-encodes 'mov    %bx,%di' differently
L_001cc4:
        .byte 0x8a,0x85,0xb6,0x00  # forced-kept: gas re-encodes 'mov    0xb6(%di),%al' differently
L_001cc8:
        .byte 0x14,0x05  # forced-kept: gas re-encodes 'adc    $0x5,%al' differently
L_001cca:
        .byte 0x27  # forced-kept: gas re-encodes 'daa' differently
L_001ccb:
        .byte 0x88,0x85,0xb6,0x00  # forced-kept: gas re-encodes 'mov    %al,0xb6(%di)' differently
L_001ccf:
        call L_0051df
        db "GULE GULE!",0x00        # string
L_001cdd:
        jmp L_000c33
L_001ce0:
        .byte 0x90  # forced-kept: gas re-encodes 'nop' differently
L_001ce1:
        call L_0051df
        db "TIME AND TRAVEL MADE ME WISE,",0x0d,"PRECIOUS GOLD, A CLUE IT BUYS!HOW MUCH WILL YOU PAY *100?",0x00        # string
L_001d3c:
        call L_002228
L_001d3f:
        .byte 0xa2,0x1f,0x00  # forced-kept: gas re-encodes 'mov    %al,0x1f' differently
L_001d42:
        .byte 0xa2,0x27,0x00  # forced-kept: gas re-encodes 'mov    %al,0x27' differently
L_001d45:
        .byte 0xb0,0x00  # forced-kept: gas re-encodes 'mov    $0x0,%al' differently
L_001d47:
        .byte 0xa2,0x28,0x00  # forced-kept: gas re-encodes 'mov    %al,0x28' differently
L_001d4a:
        call L_002252
L_001d4d:
        .byte 0xb7,0x00  # forced-kept: gas re-encodes 'mov    $0x0,%bh' differently
L_001d4f:
        .byte 0x8a,0x1e,0x1f,0x00  # forced-kept: gas re-encodes 'mov    0x1f,%bl' differently
L_001d53:
        .byte 0x8b,0xf3  # forced-kept: gas re-encodes 'mov    %bx,%si' differently
L_001d55:
        .byte 0x46  # forced-kept: gas re-encodes 'inc    %si' differently
L_001d56:
        .byte 0x2e,0x8d,0x06,0x6a,0x1b  # forced-kept: gas re-encodes 'lea    %cs:0x1b6a,%ax' differently
L_001d5b:
        .byte 0xa3,0x27,0x00  # forced-kept: gas re-encodes 'mov    %ax,0x27' differently
L_001d5e:
        .byte 0xb7,0x00  # forced-kept: gas re-encodes 'mov    $0x0,%bh' differently
L_001d60:
        .byte 0xb3,0x00  # forced-kept: gas re-encodes 'mov    $0x0,%bl' differently
L_001d62:
        .byte 0x8b,0xfb  # forced-kept: gas re-encodes 'mov    %bx,%di' differently
L_001d64:
        call L_00175d
L_001d67:
        jmp L_000c33
L_001d6a:
        .byte 0x00,0x41,0x53  # forced-kept: gas re-encodes 'add    %al,0x53(%bx,%di)' differently
L_001d6d:
        .byte 0x4b  # forced-kept: gas re-encodes 'dec    %bx' differently
L_001d6e:
        .byte 0x20,0x4d,0x45  # forced-kept: gas re-encodes 'and    %cl,0x45(%di)' differently
L_001d71:
        .byte 0x20,0x4e,0x4f  # forced-kept: gas re-encodes 'and    %cl,0x4f(%bp)' differently
L_001d74:
        .byte 0x20,0x51,0x55  # forced-kept: gas re-encodes 'and    %dl,0x55(%bx,%di)' differently
L_001d77:
        .byte 0x45  # forced-kept: gas re-encodes 'inc    %bp' differently
L_001d78:
        .byte 0x53  # forced-kept: gas re-encodes 'push   %bx' differently
L_001d79:
        .byte 0x54  # forced-kept: gas re-encodes 'push   %sp' differently
L_001d7a:
        .byte 0x49  # forced-kept: gas re-encodes 'dec    %cx' differently
L_001d7b:
        .byte 0x4f  # forced-kept: gas re-encodes 'dec    %di' differently
L_001d7c:
        .byte 0x4e  # forced-kept: gas re-encodes 'dec    %si' differently
L_001d7d:
        .byte 0x53  # forced-kept: gas re-encodes 'push   %bx' differently
L_001d7e:
        .byte 0x2c,0x8d  # forced-kept: gas re-encodes 'sub    $0x8d,%al' differently
L_001d80:
        .byte 0x49  # forced-kept: gas re-encodes 'dec    %cx' differently
L_001d81:
        .byte 0x27  # forced-kept: gas re-encodes 'daa' differently
L_001d82:
        .byte 0x4c  # forced-kept: gas re-encodes 'dec    %sp' differently
L_001d83:
        .byte 0x4c  # forced-kept: gas re-encodes 'dec    %sp' differently
L_001d84:
        .byte 0x20,0x54,0x45  # forced-kept: gas re-encodes 'and    %dl,0x45(%si)' differently
L_001d87:
        .byte 0x4c  # forced-kept: gas re-encodes 'dec    %sp' differently
L_001d88:
        .byte 0x4c  # forced-kept: gas re-encodes 'dec    %sp' differently
L_001d89:
        .byte 0x20,0x59,0x4f  # forced-kept: gas re-encodes 'and    %bl,0x4f(%bx,%di)' differently
L_001d8c:
        .byte 0x55  # forced-kept: gas re-encodes 'push   %bp' differently
L_001d8d:
        .byte 0x20,0x4e,0x4f  # forced-kept: gas re-encodes 'and    %cl,0x4f(%bp)' differently
L_001d90:
        .byte 0x20,0x4c,0x49  # forced-kept: gas re-encodes 'and    %cl,0x49(%si)' differently
L_001d93:
        .byte 0x45  # forced-kept: gas re-encodes 'inc    %bp' differently
L_001d94:
        .byte 0x53  # forced-kept: gas re-encodes 'push   %bx' differently
L_001d95:
        .byte 0x2e,0x00,0x54,0x48  # forced-kept: gas re-encodes 'add    %dl,%cs:0x48(%si)' differently
L_001d99:
        .byte 0x45  # forced-kept: gas re-encodes 'inc    %bp' differently
L_001d9a:
        .byte 0x20,0x51,0x55  # forced-kept: gas re-encodes 'and    %dl,0x55(%bx,%di)' differently
L_001d9d:
        .byte 0x45  # forced-kept: gas re-encodes 'inc    %bp' differently
L_001d9e:
        .byte 0x45  # forced-kept: gas re-encodes 'inc    %bp' differently
L_001d9f:
        .byte 0x4e  # forced-kept: gas re-encodes 'dec    %si' differently
L_001da0:
        .byte 0x20,0x49,0x53  # forced-kept: gas re-encodes 'and    %cl,0x53(%bx,%di)' differently
L_001da3:
        .byte 0x20,0x54,0x48  # forced-kept: gas re-encodes 'and    %dl,0x48(%si)' differently
L_001da6:
        .byte 0x45  # forced-kept: gas re-encodes 'inc    %bp' differently
L_001da7:
        .byte 0x20,0x4b,0x49  # forced-kept: gas re-encodes 'and    %cl,0x49(%bp,%di)' differently
L_001daa:
        .byte 0x4e  # forced-kept: gas re-encodes 'dec    %si' differently
L_001dab:
        .byte 0x47  # forced-kept: gas re-encodes 'inc    %di' differently
L_001dac:
        .byte 0x8d,0x41,0x4e  # forced-kept: gas re-encodes 'lea    0x4e(%bx,%di),%ax' differently
L_001daf:
        .byte 0x44  # forced-kept: gas re-encodes 'inc    %sp' differently
L_001db0:
        .byte 0x20,0x54,0x48  # forced-kept: gas re-encodes 'and    %dl,0x48(%si)' differently
L_001db3:
        .byte 0x45  # forced-kept: gas re-encodes 'inc    %bp' differently
L_001db4:
        .byte 0x20,0x4b,0x49  # forced-kept: gas re-encodes 'and    %cl,0x49(%bp,%di)' differently
L_001db7:
        .byte 0x4e  # forced-kept: gas re-encodes 'dec    %si' differently
L_001db8:
        .byte 0x47  # forced-kept: gas re-encodes 'inc    %di' differently
L_001db9:
        .byte 0x20,0x49,0x53  # forced-kept: gas re-encodes 'and    %cl,0x53(%bx,%di)' differently
L_001dbc:
        .byte 0x20,0x41,0x20  # forced-kept: gas re-encodes 'and    %al,0x20(%bx,%di)' differently
L_001dbf:
        .byte 0x53  # forced-kept: gas re-encodes 'push   %bx' differently
L_001dc0:
        .byte 0x50  # forced-kept: gas re-encodes 'push   %ax' differently
L_001dc1:
        .byte 0x59  # forced-kept: gas re-encodes 'pop    %cx' differently
L_001dc2:
        .byte 0x2e,0x00,0x54,0x48  # forced-kept: gas re-encodes 'add    %dl,%cs:0x48(%si)' differently
L_001dc6:
        .byte 0x45  # forced-kept: gas re-encodes 'inc    %bp' differently
L_001dc7:
        .byte 0x20,0x45,0x56  # forced-kept: gas re-encodes 'and    %al,0x56(%di)' differently
L_001dca:
        .byte 0x49  # forced-kept: gas re-encodes 'dec    %cx' differently
L_001dcb:
        .byte 0x4c  # forced-kept: gas re-encodes 'dec    %sp' differently
L_001dcc:
        .byte 0x20,0x4d,0x45  # forced-kept: gas re-encodes 'and    %cl,0x45(%di)' differently
L_001dcf:
        .byte 0x4e  # forced-kept: gas re-encodes 'dec    %si' differently
L_001dd0:
        .byte 0x20,0x44,0x4f  # forced-kept: gas re-encodes 'and    %al,0x4f(%si)' differently
L_001dd3:
        .byte 0x8d,0x49,0x53  # forced-kept: gas re-encodes 'lea    0x53(%bx,%di),%cx' differently
L_001dd6:
        .byte 0x20,0x41,0x20  # forced-kept: gas re-encodes 'and    %al,0x20(%bx,%di)' differently
L_001dd9:
        .byte 0x48  # forced-kept: gas re-encodes 'dec    %ax' differently
L_001dda:
        .byte 0x4f  # forced-kept: gas re-encodes 'dec    %di' differently
L_001ddb:
        .byte 0x52  # forced-kept: gas re-encodes 'push   %dx' differently
L_001ddc:
        .byte 0x52  # forced-kept: gas re-encodes 'push   %dx' differently
L_001ddd:
        .byte 0x49  # forced-kept: gas re-encodes 'dec    %cx' differently
L_001dde:
        .byte 0x42  # forced-kept: gas re-encodes 'inc    %dx' differently
L_001ddf:
        .byte 0x4c  # forced-kept: gas re-encodes 'dec    %sp' differently
L_001de0:
        .byte 0x45  # forced-kept: gas re-encodes 'inc    %bp' differently
L_001de1:
        .byte 0x20,0x54,0x48  # forced-kept: gas re-encodes 'and    %dl,0x48(%si)' differently
L_001de4:
        .byte 0x49  # forced-kept: gas re-encodes 'dec    %cx' differently
L_001de5:
        .byte 0x4e  # forced-kept: gas re-encodes 'dec    %si' differently
L_001de6:
        .byte 0x47  # forced-kept: gas re-encodes 'inc    %di' differently
L_001de7:
        .byte 0x2e,0x00,0x42,0x55  # forced-kept: gas re-encodes 'add    %al,%cs:0x55(%bp,%si)' differently
L_001deb:
        .byte 0x54  # forced-kept: gas re-encodes 'push   %sp' differently
L_001dec:
        .byte 0x20,0x44,0x49  # forced-kept: gas re-encodes 'and    %al,0x49(%si)' differently
L_001def:
        .byte 0x53  # forced-kept: gas re-encodes 'push   %bx' differently
L_001df0:
        .byte 0x50  # forced-kept: gas re-encodes 'push   %ax' differently
L_001df1:
        .byte 0x45  # forced-kept: gas re-encodes 'inc    %bp' differently
L_001df2:
        .byte 0x4c  # forced-kept: gas re-encodes 'dec    %sp' differently
L_001df3:
        .byte 0x4c  # forced-kept: gas re-encodes 'dec    %sp' differently
L_001df4:
        .byte 0x49  # forced-kept: gas re-encodes 'dec    %cx' differently
L_001df5:
        .byte 0x4e  # forced-kept: gas re-encodes 'dec    %si' differently
L_001df6:
        .byte 0x47  # forced-kept: gas re-encodes 'inc    %di' differently
L_001df7:
        .byte 0x20,0x4d,0x45  # forced-kept: gas re-encodes 'and    %cl,0x45(%di)' differently
L_001dfa:
        .byte 0x41  # forced-kept: gas re-encodes 'inc    %cx' differently
L_001dfb:
        .byte 0x4e  # forced-kept: gas re-encodes 'dec    %si' differently
L_001dfc:
        .byte 0x53  # forced-kept: gas re-encodes 'push   %bx' differently
L_001dfd:
        .byte 0x8d,0x54,0x48  # forced-kept: gas re-encodes 'lea    0x48(%si),%dx' differently
L_001e00:
        .byte 0x41  # forced-kept: gas re-encodes 'inc    %cx' differently
L_001e01:
        .byte 0x54  # forced-kept: gas re-encodes 'push   %sp' differently
L_001e02:
        .byte 0x20,0x59,0x4f  # forced-kept: gas re-encodes 'and    %bl,0x4f(%bx,%di)' differently
L_001e05:
        .byte 0x55  # forced-kept: gas re-encodes 'push   %bp' differently
L_001e06:
        .byte 0x20,0x4d,0x55  # forced-kept: gas re-encodes 'and    %cl,0x55(%di)' differently
L_001e09:
        .byte 0x53  # forced-kept: gas re-encodes 'push   %bx' differently
L_001e0a:
        .byte 0x54  # forced-kept: gas re-encodes 'push   %sp' differently
L_001e0b:
        .byte 0x20,0x48,0x41  # forced-kept: gas re-encodes 'and    %cl,0x41(%bx,%si)' differently
L_001e0e:
        .byte 0x56  # forced-kept: gas re-encodes 'push   %si' differently
L_001e0f:
        .byte 0x45  # forced-kept: gas re-encodes 'inc    %bp' differently
L_001e10:
        .byte 0x20,0x54,0x48  # forced-kept: gas re-encodes 'and    %dl,0x48(%si)' differently
L_001e13:
        .byte 0x45  # forced-kept: gas re-encodes 'inc    %bp' differently
L_001e14:
        .byte 0x20,0x52,0x49  # forced-kept: gas re-encodes 'and    %dl,0x49(%bp,%si)' differently
L_001e17:
        .byte 0x4e  # forced-kept: gas re-encodes 'dec    %si' differently
L_001e18:
        .byte 0x47  # forced-kept: gas re-encodes 'inc    %di' differently
L_001e19:
        .byte 0x2e,0x00,0x4a,0x55  # forced-kept: gas re-encodes 'add    %cl,%cs:0x55(%bp,%si)' differently
L_001e1d:
        .byte 0x53  # forced-kept: gas re-encodes 'push   %bx' differently
L_001e1e:
        .byte 0x54  # forced-kept: gas re-encodes 'push   %sp' differently
L_001e1f:
        .byte 0x20,0x57,0x48  # forced-kept: gas re-encodes 'and    %dl,0x48(%bx)' differently
L_001e22:
        .byte 0x45  # forced-kept: gas re-encodes 'inc    %bp' differently
L_001e23:
        .byte 0x52  # forced-kept: gas re-encodes 'push   %dx' differently
L_001e24:
        .byte 0x45  # forced-kept: gas re-encodes 'inc    %bp' differently
L_001e25:
        .byte 0x20,0x49,0x54  # forced-kept: gas re-encodes 'and    %cl,0x54(%bx,%di)' differently
L_001e28:
        .byte 0x20,0x49,0x53  # forced-kept: gas re-encodes 'and    %cl,0x53(%bx,%di)' differently
L_001e2b:
        .byte 0x2c,0x8d  # forced-kept: gas re-encodes 'sub    $0x8d,%al' differently
L_001e2d:
        .byte 0x49  # forced-kept: gas re-encodes 'dec    %cx' differently
L_001e2e:
        .byte 0x20,0x43,0x41  # forced-kept: gas re-encodes 'and    %al,0x41(%bp,%di)' differently
L_001e31:
        .byte 0x4e  # forced-kept: gas re-encodes 'dec    %si' differently
L_001e32:
        .byte 0x4e  # forced-kept: gas re-encodes 'dec    %si' differently
L_001e33:
        .byte 0x4f  # forced-kept: gas re-encodes 'dec    %di' differently
L_001e34:
        .byte 0x54  # forced-kept: gas re-encodes 'push   %sp' differently
L_001e35:
        .byte 0x20,0x53,0x41  # forced-kept: gas re-encodes 'and    %dl,0x41(%bp,%di)' differently
L_001e38:
        .byte 0x59  # forced-kept: gas re-encodes 'pop    %cx' differently
L_001e39:
        .byte 0x2e,0x00,0x42,0x55  # forced-kept: gas re-encodes 'add    %al,%cs:0x55(%bp,%si)' differently
L_001e3d:
        .byte 0x54  # forced-kept: gas re-encodes 'push   %sp' differently
L_001e3e:
        .byte 0x20,0x49,0x27  # forced-kept: gas re-encodes 'and    %cl,0x27(%bx,%di)' differently
L_001e41:
        .byte 0x4c  # forced-kept: gas re-encodes 'dec    %sp' differently
L_001e42:
        .byte 0x4c  # forced-kept: gas re-encodes 'dec    %sp' differently
L_001e43:
        .byte 0x20,0x54,0x45  # forced-kept: gas re-encodes 'and    %dl,0x45(%si)' differently
L_001e46:
        .byte 0x4c  # forced-kept: gas re-encodes 'dec    %sp' differently
L_001e47:
        .byte 0x4c  # forced-kept: gas re-encodes 'dec    %sp' differently
L_001e48:
        .byte 0x20,0x59,0x4f  # forced-kept: gas re-encodes 'and    %bl,0x4f(%bx,%di)' differently
L_001e4b:
        .byte 0x55  # forced-kept: gas re-encodes 'push   %bp' differently
L_001e4c:
        .byte 0x20,0x52,0x49  # forced-kept: gas re-encodes 'and    %dl,0x49(%bp,%si)' differently
L_001e4f:
        .byte 0x47  # forced-kept: gas re-encodes 'inc    %di' differently
L_001e50:
        .byte 0x48  # forced-kept: gas re-encodes 'dec    %ax' differently
L_001e51:
        .byte 0x54  # forced-kept: gas re-encodes 'push   %sp' differently
L_001e52:
        .byte 0x20,0x4e,0x4f  # forced-kept: gas re-encodes 'and    %cl,0x4f(%bp)' differently
L_001e55:
        .byte 0x57  # forced-kept: gas re-encodes 'push   %di' differently
L_001e56:
        .byte 0x2c,0x8d  # forced-kept: gas re-encodes 'sub    $0x8d,%al' differently
L_001e58:
        .byte 0x54  # forced-kept: gas re-encodes 'push   %sp' differently
L_001e59:
        .byte 0x48  # forced-kept: gas re-encodes 'dec    %ax' differently
L_001e5a:
        .byte 0x45  # forced-kept: gas re-encodes 'inc    %bp' differently
L_001e5b:
        .byte 0x52  # forced-kept: gas re-encodes 'push   %dx' differently
L_001e5c:
        .byte 0x45  # forced-kept: gas re-encodes 'inc    %bp' differently
L_001e5d:
        .byte 0x20,0x49,0x53  # forced-kept: gas re-encodes 'and    %cl,0x53(%bx,%di)' differently
L_001e60:
        .byte 0x20,0x4e,0x4f  # forced-kept: gas re-encodes 'and    %cl,0x4f(%bp)' differently
L_001e63:
        .byte 0x20,0x4f,0x54  # forced-kept: gas re-encodes 'and    %cl,0x54(%bx)' differently
L_001e66:
        .byte 0x48  # forced-kept: gas re-encodes 'dec    %ax' differently
L_001e67:
        .byte 0x45  # forced-kept: gas re-encodes 'inc    %bp' differently
L_001e68:
        .byte 0x52  # forced-kept: gas re-encodes 'push   %dx' differently
L_001e69:
        .byte 0x20,0x57,0x41  # forced-kept: gas re-encodes 'and    %dl,0x41(%bx)' differently
L_001e6c:
        .byte 0x59  # forced-kept: gas re-encodes 'pop    %cx' differently
L_001e6d:
        .byte 0x2e,0x00,0x49,0x20  # forced-kept: gas re-encodes 'add    %cl,%cs:0x20(%bx,%di)' differently
L_001e71:
        .byte 0x48  # forced-kept: gas re-encodes 'dec    %ax' differently
L_001e72:
        .byte 0x45  # forced-kept: gas re-encodes 'inc    %bp' differently
L_001e73:
        .byte 0x41  # forced-kept: gas re-encodes 'inc    %cx' differently
L_001e74:
        .byte 0x52  # forced-kept: gas re-encodes 'push   %dx' differently
L_001e75:
        .byte 0x20,0x49,0x4e  # forced-kept: gas re-encodes 'and    %cl,0x4e(%bx,%di)' differently
L_001e78:
        .byte 0x20,0x41,0x20  # forced-kept: gas re-encodes 'and    %al,0x20(%bx,%di)' differently
L_001e7b:
        .byte 0x54  # forced-kept: gas re-encodes 'push   %sp' differently
L_001e7c:
        .byte 0x4f  # forced-kept: gas re-encodes 'dec    %di' differently
L_001e7d:
        .byte 0x57  # forced-kept: gas re-encodes 'push   %di' differently
L_001e7e:
        .byte 0x4e  # forced-kept: gas re-encodes 'dec    %si' differently
L_001e7f:
        .byte 0x8d,0x57,0x48  # forced-kept: gas re-encodes 'lea    0x48(%bx),%dx' differently
L_001e82:
        .byte 0x45  # forced-kept: gas re-encodes 'inc    %bp' differently
L_001e83:
        .byte 0x52  # forced-kept: gas re-encodes 'push   %dx' differently
L_001e84:
        .byte 0x45  # forced-kept: gas re-encodes 'inc    %bp' differently
L_001e85:
        .byte 0x20,0x54,0x48  # forced-kept: gas re-encodes 'and    %dl,0x48(%si)' differently
L_001e88:
        .byte 0x45  # forced-kept: gas re-encodes 'inc    %bp' differently
L_001e89:
        .byte 0x20,0x57,0x41  # forced-kept: gas re-encodes 'and    %dl,0x41(%bx)' differently
L_001e8c:
        .byte 0x54  # forced-kept: gas re-encodes 'push   %sp' differently
L_001e8d:
        .byte 0x45  # forced-kept: gas re-encodes 'inc    %bp' differently
L_001e8e:
        .byte 0x52  # forced-kept: gas re-encodes 'push   %dx' differently
L_001e8f:
        .byte 0x20,0x52,0x55  # forced-kept: gas re-encodes 'and    %dl,0x55(%bp,%si)' differently
L_001e92:
        .byte 0x4e  # forced-kept: gas re-encodes 'dec    %si' differently
L_001e93:
        .byte 0x53  # forced-kept: gas re-encodes 'push   %bx' differently
L_001e94:
        .byte 0x20,0x46,0x52  # forced-kept: gas re-encodes 'and    %al,0x52(%bp)' differently
L_001e97:
        .byte 0x45  # forced-kept: gas re-encodes 'inc    %bp' differently
L_001e98:
        .byte 0x45  # forced-kept: gas re-encodes 'inc    %bp' differently
L_001e99:
        .byte 0x2e,0x00,0x54,0x48  # forced-kept: gas re-encodes 'add    %dl,%cs:0x48(%si)' differently
L_001e9d:
        .byte 0x45  # forced-kept: gas re-encodes 'inc    %bp' differently
L_001e9e:
        .byte 0x52  # forced-kept: gas re-encodes 'push   %dx' differently
L_001e9f:
        .byte 0x45  # forced-kept: gas re-encodes 'inc    %bp' differently
L_001ea0:
        .byte 0x20,0x49,0x53  # forced-kept: gas re-encodes 'and    %cl,0x53(%bx,%di)' differently
L_001ea3:
        .byte 0x20,0x41,0x4e  # forced-kept: gas re-encodes 'and    %al,0x4e(%bx,%di)' differently
L_001ea6:
        .byte 0x20,0x4f,0x4c  # forced-kept: gas re-encodes 'and    %cl,0x4c(%bx)' differently
L_001ea9:
        .byte 0x44  # forced-kept: gas re-encodes 'inc    %sp' differently
L_001eaa:
        .byte 0x20,0x4d,0x41  # forced-kept: gas re-encodes 'and    %cl,0x41(%di)' differently
L_001ead:
        .byte 0x4e  # forced-kept: gas re-encodes 'dec    %si' differently
L_001eae:
        .byte 0x8d,0x4c,0x49  # forced-kept: gas re-encodes 'lea    0x49(%si),%cx' differently
L_001eb1:
        .byte 0x56  # forced-kept: gas re-encodes 'push   %si' differently
L_001eb2:
        .byte 0x49  # forced-kept: gas re-encodes 'dec    %cx' differently
L_001eb3:
        .byte 0x4e  # forced-kept: gas re-encodes 'dec    %si' differently
L_001eb4:
        .byte 0x47  # forced-kept: gas re-encodes 'inc    %di' differently
L_001eb5:
        .byte 0x20,0x55,0x4e  # forced-kept: gas re-encodes 'and    %dl,0x4e(%di)' differently
L_001eb8:
        .byte 0x44  # forced-kept: gas re-encodes 'inc    %sp' differently
L_001eb9:
        .byte 0x45  # forced-kept: gas re-encodes 'inc    %bp' differently
L_001eba:
        .byte 0x52  # forced-kept: gas re-encodes 'push   %dx' differently
L_001ebb:
        .byte 0x20,0x41,0x20  # forced-kept: gas re-encodes 'and    %al,0x20(%bx,%di)' differently
L_001ebe:
        .byte 0x54  # forced-kept: gas re-encodes 'push   %sp' differently
L_001ebf:
        .byte 0x52  # forced-kept: gas re-encodes 'push   %dx' differently
L_001ec0:
        .byte 0x45  # forced-kept: gas re-encodes 'inc    %bp' differently
L_001ec1:
        .byte 0x45  # forced-kept: gas re-encodes 'inc    %bp' differently
L_001ec2:
        .byte 0x2e,0x00,0x48,0x45  # forced-kept: gas re-encodes 'add    %cl,%cs:0x45(%bx,%si)' differently
L_001ec6:
        .byte 0x20,0x48,0x41  # forced-kept: gas re-encodes 'and    %cl,0x41(%bx,%si)' differently
L_001ec9:
        .byte 0x53  # forced-kept: gas re-encodes 'push   %bx' differently
L_001eca:
        .byte 0x20,0x4e,0x4f  # forced-kept: gas re-encodes 'and    %cl,0x4f(%bp)' differently
L_001ecd:
        .byte 0x54  # forced-kept: gas re-encodes 'push   %sp' differently
L_001ece:
        .byte 0x20,0x41,0x20  # forced-kept: gas re-encodes 'and    %al,0x20(%bx,%di)' differently
L_001ed1:
        .byte 0x4e  # forced-kept: gas re-encodes 'dec    %si' differently
L_001ed2:
        .byte 0x41  # forced-kept: gas re-encodes 'inc    %cx' differently
L_001ed3:
        .byte 0x4d  # forced-kept: gas re-encodes 'dec    %bp' differently
L_001ed4:
        .byte 0x45  # forced-kept: gas re-encodes 'inc    %bp' differently
L_001ed5:
        .byte 0x8d,0x42,0x55  # forced-kept: gas re-encodes 'lea    0x55(%bp,%si),%ax' differently
L_001ed8:
        .byte 0x54  # forced-kept: gas re-encodes 'push   %sp' differently
L_001ed9:
        .byte 0x20,0x48,0x45  # forced-kept: gas re-encodes 'and    %cl,0x45(%bx,%si)' differently
L_001edc:
        .byte 0x20,0x44,0x4f  # forced-kept: gas re-encodes 'and    %al,0x4f(%si)' differently
L_001edf:
        .byte 0x45  # forced-kept: gas re-encodes 'inc    %bp' differently
L_001ee0:
        .byte 0x53  # forced-kept: gas re-encodes 'push   %bx' differently
L_001ee1:
        .byte 0x20,0x48,0x41  # forced-kept: gas re-encodes 'and    %cl,0x41(%bx,%si)' differently
L_001ee4:
        .byte 0x56  # forced-kept: gas re-encodes 'push   %si' differently
L_001ee5:
        .byte 0x45  # forced-kept: gas re-encodes 'inc    %bp' differently
L_001ee6:
        .byte 0x20,0x41,0x20  # forced-kept: gas re-encodes 'and    %al,0x20(%bx,%di)' differently
L_001ee9:
        .byte 0x43  # forced-kept: gas re-encodes 'inc    %bx' differently
L_001eea:
        .byte 0x4c  # forced-kept: gas re-encodes 'dec    %sp' differently
L_001eeb:
        .byte 0x55  # forced-kept: gas re-encodes 'push   %bp' differently
L_001eec:
        .byte 0x45  # forced-kept: gas re-encodes 'inc    %bp' differently
L_001eed:
        .byte 0x00,0x4d,0x41  # forced-kept: gas re-encodes 'add    %cl,0x41(%di)' differently
L_001ef0:
        .byte 0x59  # forced-kept: gas re-encodes 'pop    %cx' differently
L_001ef1:
        .byte 0x48  # forced-kept: gas re-encodes 'dec    %ax' differently
L_001ef2:
        .byte 0x41  # forced-kept: gas re-encodes 'inc    %cx' differently
L_001ef3:
        .byte 0x50  # forced-kept: gas re-encodes 'push   %ax' differently
L_001ef4:
        .byte 0x53  # forced-kept: gas re-encodes 'push   %bx' differently
L_001ef5:
        .byte 0x20,0x49,0x46  # forced-kept: gas re-encodes 'and    %cl,0x46(%bx,%di)' differently
L_001ef8:
        .byte 0x20,0x59,0x4f  # forced-kept: gas re-encodes 'and    %bl,0x4f(%bx,%di)' differently
L_001efb:
        .byte 0x55  # forced-kept: gas re-encodes 'push   %bp' differently
L_001efc:
        .byte 0x20,0x46,0x49  # forced-kept: gas re-encodes 'and    %al,0x49(%bp)' differently
L_001eff:
        .byte 0x4e  # forced-kept: gas re-encodes 'dec    %si' differently
L_001f00:
        .byte 0x44  # forced-kept: gas re-encodes 'inc    %sp' differently
L_001f01:
        .byte 0x20,0x48,0x49  # forced-kept: gas re-encodes 'and    %cl,0x49(%bx,%si)' differently
L_001f04:
        .byte 0x4d  # forced-kept: gas re-encodes 'dec    %bp' differently
L_001f05:
        .byte 0x2c,0x8d  # forced-kept: gas re-encodes 'sub    $0x8d,%al' differently
L_001f07:
        .byte 0x48  # forced-kept: gas re-encodes 'dec    %ax' differently
L_001f08:
        .byte 0x45  # forced-kept: gas re-encodes 'inc    %bp' differently
L_001f09:
        .byte 0x27  # forced-kept: gas re-encodes 'daa' differently
L_001f0a:
        .byte 0x4c  # forced-kept: gas re-encodes 'dec    %sp' differently
L_001f0b:
        .byte 0x4c  # forced-kept: gas re-encodes 'dec    %sp' differently
L_001f0c:
        .byte 0x20,0x54,0x45  # forced-kept: gas re-encodes 'and    %dl,0x45(%si)' differently
L_001f0f:
        .byte 0x4c  # forced-kept: gas re-encodes 'dec    %sp' differently
L_001f10:
        .byte 0x4c  # forced-kept: gas re-encodes 'dec    %sp' differently
L_001f11:
        .byte 0x20,0x49,0x54  # forced-kept: gas re-encodes 'and    %cl,0x54(%bx,%di)' differently
L_001f14:
        .byte 0x20,0x54,0x4f  # forced-kept: gas re-encodes 'and    %dl,0x4f(%si)' differently
L_001f17:
        .byte 0x20,0x59,0x4f  # forced-kept: gas re-encodes 'and    %bl,0x4f(%bx,%di)' differently
L_001f1a:
        .byte 0x55  # forced-kept: gas re-encodes 'push   %bp' differently
L_001f1b:
        .byte 0x21,0x00  # forced-kept: gas re-encodes 'and    %ax,(%bx,%si)' differently
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

# magic_shop_menu — the wizard's spell catalogue and the clue seller's arm
# name-evidence: strings ' MUSERREF OLDUM!' / '1-LIGHT, 2-...' spell list, and the clue
#           seller's 'PRECIOUS GOLD, A CLUE IT BUYS!HOW MUCH WILL YOU PAY *100?'
# func 19D7..1B6A  len 0194 B  evidence: seed+reko; re-cut 2026-11-15 (the clue pool and the
#           armour/weapons/transport catalogues that shared this span are now their own files)
# note: TWO SHOPPES live in this span, which is why the name is narrower than the body. The
#           wizard's catalogue is the 19D7 entry; the clue seller is the asm/1AE0 arm, whose
#           tail is the last code here:
#             call read_decimal_digit (asm/2028, one digit 0..9)
#             mov %al,0x1f / mov %al,0x27 / mov %al,0x28   DS:1F = digit; DS:27:28 = digit
#             call 0x2253                                  the gold charge: the digit is the
#                                                          price x100 BY PLACEMENT, which is
#                                                          what the menu's own text says
#             mov 0x1f,%bl ; mov %bx,%si ; inc %si         SI = digit + 1
#             lea %cs:0x1b6a,%ax ; mov %ax,0x27            cursor = runtime 0x1b6a = FILE 0x1d6a
#             mov %bx,%di (DI=0) ; call L_00175d           the walker, asm/155D
#           The pool it indexes is asm/1B6B_clue_pool.asm and the walker emits entry `digit`,
#           so digit N buys entry N and digit 0 is free. An earlier draft of this header said
#           "entry N-1"; the walker's first NUL test fires on the cursor itself, so it does not.
# note: address spaces, because this file mixes them on purpose. `lea` operands inside bodies
#           are RUNTIME addresses; L_<addr> labels and asm/ filenames are FILE offsets, and
#           runtime + 0x200 = file. The cursor operand 0x1b6a is the NUL at file 0x1d6a, the
#           last byte of this file.
# provenance: static
#           the entry never appears in ExecutedInstructions of any harvest. The vendor
#           dispatcher upstream of it HAS been observed (see asm/4050_cmd_T.asm), so the door is
#           live and the threshold is the part still unseen.
# confidence: low (bytes proven by check_func.py)
# split-to: asm/1B6B_clue_pool.asm (1B6B..1D1C) and asm/1D1D_shop_menus.asm (1D1D..2027)
# boundary: this file's new end is a CODE/DATA line, not a routine boundary. The last byte is the
#           cursor-seed NUL the clue walker needs; the byte after it is the first clue. No `jmp`
#           or `call` reaches either side of that line — the pool is reached by the walker's cursor,
#           not by dispatch — so its proof is the cursor write plus the ten entry starts, not a
#           branch target. See docs/g22-split-proposal.md § 3 for why that is a stronger kind of
#           boundary than a fall-through, and asm/1D1D_shop_menus.asm for the dispatch kind.
# body corrected: 2026-11-15 -- evidence: this file now ends at runtime 0x1b6a (file 0x1d6a).
#           The bytes from 0x1d6b on were ten NUL-separated clue strings that the disassembler
#           had rendered as instructions and gas had force-kept as `.byte` blobs, and they are
#           now db lines in asm/1B6B_clue_pool.asm; the armour, weapons and transport
#           catalogues after them are asm/1D1D_shop_menus.asm. No byte moved and the last line
#           here is still the same 0x00 that it always was -- only the bytes that were glued to
#           it on the same `.byte` line left -- so the split is byte-neutral and
#           tools/assemble_clean.sh is the authority, not this marker.
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
# refs: L_0051df, L_002228, L_001c3a, L_001c4f, L_000c33, L_001c56, L_00528c, L_0022ab, L_0053a5, L_00514d, L_001c9b, L_001cb0, L_002252, L_00175d  (labels named by visible mnemonics; forced-keep bytes can hide more)
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
        .byte 0x00  # the cursor seed: asm/155D's walker skips this NUL to land on entry 0 at runtime 0x1b6b (file 0x1d6b), which is asm/1B6B_clue_pool.asm

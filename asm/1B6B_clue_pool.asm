# clue_pool — the clue seller's ten NUL-separated clues, indexed by the price digit
# name-evidence: reached from the clue seller's arm inside asm/19D7_magic_shop_menu.asm:
#           `lea %cs:0x1b6a,%ax ; mov %ax,0x27` sets the walker's cursor to the NUL one byte
#           BEFORE entry 0 and `call L_00175d` (asm/155D) walks the table, emitting entry SI
#           where SI = digit + 1. Ten digits for ten entries, so digit N buys entry N and
#           digit 0 is free — and the price is the same digit x100 by byte placement, which is
#           what the menu's own line says: 'PRECIOUS GOLD, A CLUE IT BUYS!HOW MUCH WILL YOU
#           PAY *100?'
# func 1B6B..1D1C  len 01B2 B  evidence: re-cut 2026-11-15 from asm/19D7_magic_shop_menu.asm
# provenance: static
#           neither this table, its caller, nor the walker appears in ExecutedInstructions of
#           any harvest that carries a flow dump. The table's CONTENT is certain (the bytes and
#           the entry starts both pin it) and the index rule is read directly off the caller,
#           but no clue has ever been seen printed on a frame. See docs/needs-gameplay.md.
# confidence: high (bytes proven by check_func.py; the cursor write, the walker and all ten
#             entry starts are read directly, and no immediate in the binary points at any
#             entry — which is why an address-reference search alone wrongly makes this look
#             like dead data)
# body corrected: 2026-11-15 -- evidence: the span file 0x1d6b..0x1f1c is ten NUL-separated
#           strings, not code. The old decode ran the disassembler off the end of the clue
#           seller's menu and invented instructions out of the bytes of "ASK ME NO
#           QUESTIONS,..." and friends; every `.byte 0x??  # forced-kept: gas re-encodes 'add
#           %al,0x53(%bx,%di)' differently` from file 0x1d6a through 0x1f1c was text. Independent
#           check: the ten entry starts sit at exactly the running sums of the string lengths
#           plus their terminators, which cannot happen by accident over 434 bytes, and the
#           span ends precisely where the armour catalogue's `nop` begins. Same failure already
#           recorded for asm/4C50 and for the sibling table asm/16BD_barkeep_chatter_hints.asm,
#           which was re-cut from asm/15E1 the same way.
# note: the cursor is set to RUNTIME 0x1b6a, ONE byte before the first entry — the NUL that ends
#           the clue seller's own code, which stays in asm/19D7_magic_shop_menu.asm with its own
#           comment. The walker skips to the next NUL before it starts counting, so that leading
#           NUL is what puts entry 0 at 0x1b6b. Do not "fix" the off-by-one.
# note: address spaces. The `lea` operand 0x1b6a and the `# func` range are RUNTIME addresses;
#           the L_<addr> labels in this file are FILE offsets, and runtime + 0x200 = file. Entry
#           0 is L_001d6b.
# note: the 0x8d inside every entry is the emitter's newline glyph, not a typo and not a
#           separator between two strings. asm/5003 char_print masks every byte with 0x7f
#           before it tests for 0x0d, so 0x8d moves the cursor down a row and each clue is two
#           lines on screen. The payload is split on it the way asm/437E_cmd_V.asm splits
#           'VIEW',0x8d,'WITH MAGICAL HELM!'.
# split-from: asm/19D7_magic_shop_menu.asm (19D7..2027)
# strings (entry index, then the text; \x8d is the emitter's newline glyph):
#           0 "ASK ME NO QUESTIONS,\x8dI'LL TELL YOU NO LIES."
#           1 "THE QUEEN IS THE KING\x8dAND THE KING IS A SPY."
#           2 "THE EVIL MEN DO\x8dIS A HORRIBLE THING."
#           3 "BUT DISPELLING MEANS\x8dTHAT YOU MUST HAVE THE RING."
#           4 "JUST WHERE IT IS,\x8dI CANNOT SAY."
#           5 "BUT I'LL TELL YOU RIGHT NOW,\x8dTHERE IS NO OTHER WAY."
#           6 "I HEAR IN A TOWN\x8dWHERE THE WATER RUNS FREE."
#           7 "THERE IS AN OLD MAN\x8dLIVING UNDER A TREE."
#           8 "HE HAS NOT A NAME\x8dBUT HE DOES HAVE A CLUE"
#           9 "MAYHAPS IF YOU FIND HIM,\x8dHE'LL TELL IT TO YOU!"
L_001d6b:
        db "ASK ME NO QUESTIONS,",0x8d,"I'LL TELL YOU NO LIES.",0x00        # entry 0
L_001d97:
        db "THE QUEEN IS THE KING",0x8d,"AND THE KING IS A SPY.",0x00        # entry 1
L_001dc4:
        db "THE EVIL MEN DO",0x8d,"IS A HORRIBLE THING.",0x00        # entry 2
L_001de9:
        db "BUT DISPELLING MEANS",0x8d,"THAT YOU MUST HAVE THE RING.",0x00        # entry 3
L_001e1b:
        db "JUST WHERE IT IS,",0x8d,"I CANNOT SAY.",0x00        # entry 4
L_001e3b:
        db "BUT I'LL TELL YOU RIGHT NOW,",0x8d,"THERE IS NO OTHER WAY.",0x00        # entry 5
L_001e6f:
        db "I HEAR IN A TOWN",0x8d,"WHERE THE WATER RUNS FREE.",0x00        # entry 6
L_001e9b:
        db "THERE IS AN OLD MAN",0x8d,"LIVING UNDER A TREE.",0x00        # entry 7
L_001ec4:
        db "HE HAS NOT A NAME",0x8d,"BUT HE DOES HAVE A CLUE",0x00        # entry 8
L_001eee:
        db "MAYHAPS IF YOU FIND HIM,",0x8d,"HE'LL TELL IT TO YOU!",0x00        # entry 9

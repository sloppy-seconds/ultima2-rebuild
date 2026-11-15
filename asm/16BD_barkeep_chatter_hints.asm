# barkeep_chatter_hints — NUL-separated bar chatter and hint table, indexed by a cursor
# name-evidence: the tavern menu's tail does `lea ax,cs:0x16bc ; mov ds:0x27,ax` and calls
#           asm/155D, which walks the table at [DS:27:28] skipping SI entries and emitting
#           the one it lands on. Entry 0 is the first string here. The three chatter lines
#           and the seven hints are the game's own text; see docs/third-party-claims.md § 3.
# func 16BD..1798  len 00DC B  evidence: re-cut 2026-10-25 from asm/15E1_tavern_menu.asm
# strings: "CAUGH..."; "ISN'T THIS A GREAT GAME?"; "HHMMM..."; "SOME FIGHTERS WEAR MAGIC HELMS!";
#          "AVIATORS USE SKULL KEYS!"; "SAYLORS WEAR BLUE TASSLES!"; "MAGES NEED WANDS OR STAFFS!";
#          "GUARDS CARRY KEYS!"; "ANKHS OPEN SPACE"; "PLANES NEED BRASS BUTTONS!"
# provenance: static
#           nothing here has been seen to run: 0x16BD and the selector at 0x155D are absent from
#           ExecutedInstructions of all 142 harvests that carry a flow dump, as is the caller.
#           The decode is read, not observed -- the table's CONTENT is certain (the bytes and the
#           group-base arithmetic both pin it) but which entry the game actually prints, and when,
#           is unverified. That is a capture away from being settled: walk into a tavern and talk
#           to the barkeep. See docs/needs-gameplay.md.
# confidence: high (bytes proven by check_func.py; the cursor write and the walker are both
#             read directly, and no immediate in the file points at any entry -- which is why
#             an address-reference search alone wrongly makes this look like dead data)
# body corrected: 2026-10-25 -- evidence: the span 16BD..1798 is ten NUL-separated strings, not code.
#           The old decode ran the disassembler off the end of the tavern menu and invented
#           instructions out of the bytes of "SOME FIGHTERS WEAR MAGIC HELMS!" and friends.
#           Independent check: the ten entry starts sit at exactly the running sums of the
#           string lengths plus their terminators, which cannot happen by accident over 220
#           bytes. The reachability proof is the `lea cs:0x16bc ; mov ds:0x27,ax` pair at
#           runtime 0x16a8..0x16af and the walker at asm/155D_sub_155d.asm.
# note: the cursor is set to 0x16bc, ONE byte before the first entry -- the NUL that ends the
#           tavern menu's own code. The walker skips to the next NUL before it starts counting,
#           so that leading NUL is what puts entry 0 at 0x16bd. Do not "fix" the off-by-one.
# note: address spaces, clarified 2026-11-15 because this header mixes them. 0x16bc and 0x16bd are
#           RUNTIME addresses; the entry labels in this file (L_0018bd onward) are FILE offsets,
#           and runtime + 0x200 = file. So the cursor at runtime 0x16bc is the NUL at file 0x18bc
#           and entry 0 at runtime 0x16bd is L_0018bd. The `lea cs:0x16bc ; mov ds:0x27,ax` pair is
#           at file 0x18a8..0x18ad (labels in asm/15E1_tavern_menu.asm), not 0x16a8. The sibling
#           clue pool was decoded the same way the same day and is documented in
#           asm/19D7_magic_shop_menu.asm's header; its cursor is runtime 0x1b6a = file 0x1d6a.
# note: this table is the BARKEEP's, reached from the tavern menu. The clue seller (asm/1AE0 arm of
#           asm/1590) uses a different table and a different cursor, so "which entry prints" is
#           settled for both without a capture: the index is the digit the player pays, and the
#           walker is shared. What remains unobserved is only the tavern's own index source.
# note: DS:27:28 is shared scratch. docs/commerce.md § 3b measured it as the price operand on
#           the shop path; here the same two bytes hold a code-segment cursor. Same aliasing
#           hazard as DS:1F, which has twenty-plus writers.
# split-from: asm/15E1_tavern_menu.asm (15E1..1798)
L_0018bd:
        db "CAUGH...",0x00        # entry 0  <- bar chatter
L_0018c6:
        db "ISN'T THIS A GREAT GAME?",0x00        # entry 1  <- bar chatter
L_0018df:
        db "HHMMM...",0x00        # entry 2  <- bar chatter
L_0018e8:
        db "SOME FIGHTERS WEAR MAGIC HELMS!",0x00        # entry 3  <- HINT: corroborates the cheat sheet's helm-for-VIEW claim
L_001908:
        db "AVIATORS USE SKULL KEYS!",0x00        # entry 4  <- HINT: corroborates skull-keys-for-launching-a-plane
L_001921:
        db "SAYLORS WEAR BLUE TASSLES!",0x00        # entry 5  <- HINT: corroborates blue-tassle-for-board
L_00193c:
        db "MAGES CARRY WANDS OR STAFFS!",0x00        # entry 6  <- HINT: mages and wands/staffs
L_001959:
        db "GUARDS CARRY KEYS!",0x00        # entry 7  <- HINT: guards carry keys, which is what the cheat sheet says too
L_00196c:
        db "ANKHS OPEN SPACE!",0x00        # entry 8  <- HINT: ankhs open space
L_00197e:
        db "PLANES NEED BRASS BUTTONS!",0x00        # entry 9  <- HINT: corroborates brass-button-for-launch

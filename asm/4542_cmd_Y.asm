# cmd_Y — command handler 'Y': prints "YELL WHAT?" and falls into its freeform yell loop
# func 4542..4550  len 015 B  evidence: seed+reko (jump table cs:0730 entry Y)
# provenance: live:cmd-letters-20261025; static
#           entry executed in 4 of 283 harvests; confirmed by the capture of 2026-10-25, which
#           also showed what the four-line body cannot say: after `YELL WHAT?` the command loop
#           does NOT return to the prompt -- later letters accumulated as text on the CMD line
#           and never dispatched. Was recorded as never executed against a 60-harvest corpus.
# CORRECTED 2026-10-29 — the fall-through into asm/4551 is resolved, and the capture was read
#           backwards. asm/4551..asm/4567 is a complete freeform input loop: spin on the key
#           seam, echo each character, end on Enter (asm/4559), print the Enter, then cmd_done
#           (asm/4565). The letters that "never dispatched" were the yell being echoed, and the
#           loop does reach cmd_done — it just cannot until CR arrives. The owner's recollection
#           of the manual ("type a freeform thing to yell out, with <enter>") is what sent the
#           re-read; the bytes agree, and add that nothing in 4542..4567 writes memory, so the
#           yell is display-only rather than merely invisible. Gate: engine/tests/cmd_yell.py.
# confidence: high (was medium: the 0x8d question below is what held it down)
# 0x8d is NOT a mystery glyph and the CP9 note about it is retired: it is the game's trailing
#           newline convention, the same byte that closes " IS DEAD!", "SAVED BY IDOL!" and the
#           Q guards, and the engine emits the newline itself exactly as it does for those.
cmd_Y:
        call L_0051df          # str_out — prints the inline string below
        db "YELL WHAT?",0x8d,0 # string (0x8d = high-bit glyph, non-printable)

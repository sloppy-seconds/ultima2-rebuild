# print_player_name — emit the character's name: DS:36 up to its NUL, one character at a time
# name-evidence: read directly across the fragment split. `bx := 0` here; asm/507D loads
#           `mov 0x36(%bx),%al` and tests it, asm/5085 calls asm/5003 char_print, asm/5088
#           increments bx and jumps back, and asm/508B is the `ret` on the NUL. DS:36 is the
#           first byte of the 256-byte save record and is the name field (engine/src/core/state.h:
#           `name[16]`, "cleared, then typed; entry ends on CR"). The independent check is
#           asm/0C62_death, whose own header has read this call the same way all along: the death
#           announcement is '<NAME> IS DEAD!' and the only thing between the three newlines and
#           the literal is this call.
# func 507A..507C  len 0003 B  evidence: seed+call+reko
# note: THIS FILE IS THE PROLOGUE ONLY, and that is a fragment split rather than a stub. The
#           routine runs 507A..0x508B across five tiling fragments (507A, 507D, 5085, 5088, 508B).
#           Read alone it is one instruction that zeroes a register and falls through, which is
#           what let asm/1869's header call it "a one-instruction stub" and keep the cleric
#           shrine's greeting graded inferred over a fact that was already written in
#           asm/0C62_death's header. Same shape as asm/155D. See disasm/fragment-splits.tsv and
#           tools/fragment_splits.py.
# calls: L_005203 (asm/5003_char_print), reached through asm/5085
# provenance: static, decoded 2026-11-15 by reading the five fragments end to end; no capture
# confidence: high (one loop, one exit, and the death announcement is this call printing that
#             field). The bytes themselves were always proven by check_func.py.
# label: kept as sub_507a on purpose — labels are body and CP0 md5s them
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
sub_507a:
L_00527a:
        mov    $0x0,%bx

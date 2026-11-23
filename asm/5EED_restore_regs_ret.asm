# 5EED_restore_regs_ret — a shared exit, not a routine: `pop %dx ; pop %cx ; pop %bx ; pop %ax
#           ; ret`. There is a byte-identical copy at file 0x6108, which is the entry region of
#           asm/5F08. Both copies have executed in the corpus, so this is one idiom emitted twice,
#           not a live copy and a dead one.
# name-evidence: body only — four pops in LIFO order against a `ret`. An epilogue has nothing to
#           name it from but its own shape.
# addresses: the filename is the runtime offset in segment 017D; the `L_` labels here are file
#           offsets, and the two differ by the 0x200 MZ load offset. This routine is file 0x60ED,
#           runtime 0x5EED. Reading a label as a file offset and a filename as a runtime offset in
#           the same sentence is how this file got a wrong header once already.
# provenance: static (decoded from the body 2026-11-23).
# confidence: high for what it is. The two `call L_005eed` sites in asm/5F08 at file 0x6163 and
#           0x6188 do NOT match it — they load DX/CX/BX from registers instead of pushing four
#           words, and neither site has ever executed. Left as a gap rather than explained: a call
#           into an epilogue is not a call this corpus can account for.
# func 5EED..5EF1  len 0005 B  evidence: seed+reko
# confidence: low (bytes proven by check_func.py)
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
sub_5eed:
L_0060ed:
        pop    %dx
L_0060ee:
        pop    %cx
L_0060ef:
        pop    %bx
L_0060f0:
        pop    %ax
L_0060f1:
        ret

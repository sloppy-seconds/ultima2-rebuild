# sub_3582 — mechanical clean (bulk wave); behavior not yet reviewed
# func 3582..3595  len 0014 B  evidence: reko
# strings: " 9,000,000 B.C."
# calls: L_000c33, L_0051df (str_out)
# confidence: low (bytes proven by check_func.py)
# body corrected: 2026-10-28e -- evidence: the second half of the era-table string started at
#           asm/3577, then a jmp back to the dispatcher, then the opcode byte of the next
#           `call str_out`. Every mnemonic this file showed was string text decoded as code
#           except the jmp, which was real. Re-cut from the NUL terminator at 3591 and the
#           dispatcher target, which fix the ends exactly. Found by tools/forced_keep_audit.py
#           and tools/str_out_audit.py.
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
sub_3582:
L_003782:
        db " 9,000,000 B.C.",0x00        # string; "ANOS:" is at asm/3577
L_003792:
        jmp L_000c33           # return to dispatcher
L_003795:
        .byte 0xe8  # forced-kept: stop-trunc call   0x51df

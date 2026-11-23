# 02F8_demo_help_line_print — writes two state bytes and prints one help line: the tail of
#           `movb $0xd,0x2f` (started in asm/02F7), then `mov $0x6,%al ; mov %al,0x2e`, then
#           `call str_out` on `'D' - FOR A DEMONSTRATION`. The first byte of this file is data, not
#           code, and the body-corrected note below is what established that.
# name-evidence: the string it prints, plus the `call L_0051df (str_out)` that prints it. The two
#           DS writes (0x2F = 0x0D, 0x2E = 6) are recorded without a meaning; neither is decoded
#           here.
# provenance: static (decoded from the body 2026-11-23). 0x04FC has executed in the corpus, so the
#           print is observed; the file's first four bytes are a data tail and are not code.
# confidence: high for what it prints. The name says help line, not screen, because no frame of it
#           has been captured — what is known comes from the bytes and one executed print.
# func 02F8..031D  len 0026 B  evidence: reko
# strings: "'D' - FOR A DEMONSTRATION"
# calls: L_0051df (str_out)
# confidence: low (bytes proven by check_func.py)
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
# body corrected: 2026-10-28d -- evidence: bytes 02F8..02FB are `06 2f 00 0d`, the tail of
#           `movb $0xd,0x2f` whose opcode 0xc6 is the forced-keep in asm/02F7.
sub_02f8:
L_0004f8:
        .byte 0x06, 0x2f, 0x00, 0x0d   # tail of `movb $0xd,0x2f` started at asm/02F7
L_0004fc:
        mov    $0x6,%al
L_0004fe:
        mov    %al,0x2e
L_000501:
        call L_0051df
        db "'D' - FOR A DEMONSTRATION",0x00        # string

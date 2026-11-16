# beep_short — the short fixed-parameter PC-speaker beep: divisor 0x500 (932 Hz), delay cx=0x4000
# name-evidence: bx=0x500, cx=0x4000 then a tail jump to asm/5DA9 tone_player. Heard live: a refused
#           step produced one tone whose speaker-gate span was 835,000 cycles (~175 ms at a nominal
#           4.77 MHz), divisor read from the register at asm/5CD2 while it played.
# callers: the refusal tail asm/0A30 jumps here, and about twenty refusal sites jump to that tail
#           (invalid move, NO DOOR THERE!, OFFER WHAT?, price too high, the vehicle board
#           refusals, the arms-refused attack); also the dungeon notices in asm/2D2A/asm/2D33
#           (TORCH BURNED OUT!, YOUR TORCH IS BLOWN OUT!, A GREMLIN STOLE SOME FOOD!,
#           YOU FEEL A STRONG MAGIC!) and asm/277B's ARGH! A TRAP! prefix.
# provenance: captures/spice86/step-sound-20261117 (gate-span + register), static
# confidence: medium (parameters and duration measured; the caller list is a static scan)
# func 5C95..5C9B  len 0007 B  evidence: seed+call+reko
# confidence: low (bytes proven by check_func.py)
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
sub_5c95:
L_005e95:
        cmpb   $0x0,0x54d
L_005e9a:
        jne L_005eb1

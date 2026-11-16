# play_seq0 — the sound-off guard and wrapper that plays tone sequence 0, the step thud
# name-evidence: `cmp byte [DS:54D],0 ; jne skip` (the sound-enabled flag), then the next fragments
#           push bx, set bx=0 and call asm/5D09 speaker_seq_player — so this is sequence index 0,
#           whose record at cs:5d5d is {count 5, divisor 0x4000} = 73 Hz. Measured live: it fires
#           TWICE on every step attempt, accepted or refused, which puts the sound ahead of the
#           terrain verdict (captures/spice86/step-sound-20261117). Callers are the arms of the
#           food/mode dispatch (asm/2557, asm/2575, asm/2590) and asm/08E3, which is unnamed.
# fragment: the guard is this file; the call is asm/5ECE and the ret is asm/5ED6.
# provenance: captures/spice86/step-sound-20261117, static
# confidence: medium
# func 5EC7..5ECD  len 0007 B  evidence: seed+call+reko
# confidence: low (bytes proven by check_func.py)
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
sub_5ec7:
L_0060c7:
        cmpb   $0x0,0x54d
L_0060cc:
        jne L_0060d6

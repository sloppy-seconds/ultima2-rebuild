# speaker_seq_player — plays one entry of the tone-sequence table at cs:5d9b
# name-evidence: bx selects an entry of a pointer table at cs:5d9b (four entries, read live as
#           5d5d 5d61 5d65 0000), each pointing at records of {count, divisor}; cx is set to count<<8
#           and bp to the record, then asm/5DA9 tone_player sounds it and the delay loop runs cx times
#           with an inner bound of DS:58C (read live as 0x0017). Observed at asm/5CD2 with
#           EBX=0x4000 ECX=0x0500 EBP=0x5D5D, and cs:5d5d read live as 05 00 40 00 —
#           {count 5, divisor 0x4000}.
# fragment: the tiling cuts this routine at 5D13; the loop body continues in the following fragments
#           (5D14..5D5B) and the ret is at 5D5B. tools/fragment_splits.py nominates exactly this shape.
# provenance: captures/spice86/step-sound-20261117 (registers at asm/5CD2, live table read), static
# confidence: medium
# func 5D09..5D13  len 000B B  evidence: seed+call+reko
# confidence: low (bytes proven by check_func.py)
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
sub_5d09:
L_005f09:
        push   %ax
L_005f0a:
        push   %bx
L_005f0b:
        push   %cx
L_005f0c:
        push   %bp
L_005f0d:
        cmpb   $0x0,0x54d
L_005f12:
        jne L_005f58

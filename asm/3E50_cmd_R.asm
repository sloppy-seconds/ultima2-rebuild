# cmd_R — command handler 'R' (READY WEAPON): prints the weapon menu
#           "READY WEAPON:\r1-DA, 2-MA, 3-AX, 4-BO,\r5-SW, 6-GR, 7-LI, 8-PH.\r
#           9-QU, WHICH? \0" then reads the choice.
# func 3E50..3E9E  len 004F B  evidence: seed+reko (jump table cs:0730 entry R)
# provenance: live:baseline-fight (+2 other harvests), static
#           entry executed in 8 of 282 harvests; derived by tools/provenance_from_traces.py
# confidence: medium (flow + messages proven by bytes; NPC/quest semantics inferred)
# notes: re-decoded with tools/decode_str.py (str_out-aware); gas re-encode diffs
#        kept as .byte (disp8/disp16 and reg-op form choices)
cmd_R:
cmd_R:
L_004050:
        call L_0051df
        db "READY WEAPON:",0x0d,"1-DA, 2-MA, 3-AX, 4-BO,",0x0d,"5-SW, 6-GR, 7-LI, 8-PH.",0x0d,"9-QU, WHICH? ",0x00        # string

# cmd_Z — command handler 'Z': tail-calls the mode-set/cursor-zero prologue (asm/4A1C_screen_clear_mode1.asm)
# func 4568..456A  len 003 B  evidence: seed+reko (jump table cs:0730 entry Z)
# provenance: live:airport (+11 other harvests), static
#           entry executed in 19 of 277 harvests; derived by tools/provenance_from_traces.py
# confidence: high (handler role proven; single tail call)
cmd_Z:
        call L_004c1c

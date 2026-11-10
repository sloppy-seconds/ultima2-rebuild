# underlay_copy — flush the staged cells, then record what is now on screen as the underlay
# name-evidence: `call L_004cad` (asm/4AAD_flush_staged_cells.asm) and then a 240-entry loop,
#           `mov 0x272(%di),%al ; and $0x7f,%al ; mov 0x372(%di),%al`, walking while the low byte of
#           the index runs to 0xF0. So DS:372 is DS:272 with the do-not-draw bit cleared: "what the
#           screen currently holds", and the stager's unchanged test compares against exactly this.
# WHY IT MATTERS: it explains the 0xFF fill at asm/38B0_cmd_L runtime 3964..3972. That fill is not
#           "the map is blank during the dive" -- it clears the underlay so the first staged pass
#           finds every cell changed and therefore draws all 200 of them.
# reached from: asm/5B98_sub_5b98.asm by fallthrough, i.e. it is the tail of asm/5AB1_map_render.
# provenance: live: dive-sweep-rate 2026-11-10 (5C6D..5C88 in ExecutedInstructions during a rocket
#           descent) plus the static read of file 0x5E6D..0x5E88
# confidence: high
# func 5C6D..5C6F  len 0003 B  evidence: seed+reko
# calls: L_004cad
# confidence: low (bytes proven by check_func.py)
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
sub_5c6d:
L_005e6d:
        call L_004cad

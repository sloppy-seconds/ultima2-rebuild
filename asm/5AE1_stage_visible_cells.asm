# stage_visible_cells — the staging loop of asm/5AB1_map_render: 20 columns x 10 rows into DS:272
# name-evidence: this is the loop head. `clc ; mov 0x4,%al ; adc 0x2,%al ; cmp $0x40` is the column
#           index plus the viewport origin DS:02 with a 64-cell bounds test, and the row test is the
#           same shape against DS:03. The outer counters run DS:04 to 0x14 and DS:05 to 0x0a, so the
#           window is 20x10 = 200 cells; the map byte becomes an art index by `rcr` twice and
#           `and $0xfe`, and the do-not-draw bit (`or $0x80`) is set when the cell is unchanged and
#           is either water (`0x00` with DS:54E == 0xFF) or the letter tile `0x2e`.
# reached from: asm/5B98_sub_5b98.asm by FALLTHROUGH, not by call -- which is why the fragment map
#           hid it. The flush at asm/4AAD_flush_staged_cells.asm is called from the tail of the
#           same routine (asm/5C6D_underlay_copy.asm).
# provenance: live: dive-sweep-rate 2026-11-10 (offsets 5AE1..5B3D in ExecutedInstructions during a
#           rocket descent) plus the static read of file 0x5CE1..0x5D71
# confidence: high for the loop bounds and the art-index transform; the DS:54E and 0x2e clauses are
#           read from the bytes and not separately observed
# func 5AE1..5AEC  len 000C B  evidence: seed+reko
# confidence: low (bytes proven by check_func.py)
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
sub_5ae1:
L_005ce1:
        clc
L_005ce2:
        mov    0x4,%al
L_005ce5:
        adc    0x2,%al
L_005ce9:
        cmp    $0x40,%al
L_005ceb:
        jae L_005cf9

# flush_staged_cells — draw the 200 staged cells at DS:272 into CGA, skipping do-not-draw cells
# name-evidence: saves ax/bx/cx/di/si, then two nested counters: `add $0x10,%ax ; cmp $0x140` (20 x
#           16 px columns) and `add $0x10,%bx ; cmp $0xa0` (10 x 16 px rows). Per cell it loads
#           `mov 0x272(%si),%cl`, tests it with `or %cl,%cl ; js` -- bit 7 is the do-not-draw flag
#           the stager sets -- and otherwise indexes the art-descriptor table at DS:5B0
#           (`mov 0x5b0(%di),%cx`) and calls asm/4A74_tile_blit_cga.asm.
# WHY THE SAVED %bx MATTERS: this is the second reason the speed-delay slots are inert. The dive
#           loop at asm/38B0_cmd_L loads `speed_delay[14]` into %bx and then calls the render, and
#           this routine pushes and pops %bx without reading it, exactly like key_poll. See
#           asm/0711_speed_delay_lookup's correction of 2026-11-10.
# reached from: asm/5C6D_underlay_copy.asm, which is the tail of asm/5AB1_map_render.
# provenance: live: dive-sweep-rate 2026-11-10 (4AAD..4AE0 in ExecutedInstructions during a rocket
#           descent) plus the static read of file 0x4CAD..0x4CE5
# confidence: high for the loop bounds, the skip test and the descriptor table
# func 4AAD..4AB7  len 000B B  evidence: seed+call+reko
# confidence: low (bytes proven by check_func.py)
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
sub_4aad:
L_004cad:
        push   %ax
L_004cae:
        push   %bx
L_004caf:
        push   %cx
L_004cb0:
        push   %di
L_004cb1:
        push   %si
L_004cb2:
        mov    $0x0,%bx
L_004cb5:
        mov    $0x0,%si

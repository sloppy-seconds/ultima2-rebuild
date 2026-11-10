# map_render — the whole sector render: viewport origin, stage the visible 20x10 window, flush to CGA
# RENAMED 2026-11-10 from view_origin_by_mode, because the old name is this project's recurring
#           fragment-boundary error: the routine does not stop where Phase 3 cut it. It is the map
#           renderer. Proof is a live dive, with every stage in ExecutedInstructions -- see
#           captures/spice86/dive-sweep-rate/README.md section 3. Read in address order:
#             5AB1  this entry: `cmpb $0xff,0x12`; the world map goes to asm/5B98_sub_5b98.asm, an
#                   interior to asm/5ABB_sub_5abb.asm
#             5B98  world-map arm: DS:03 = [DS:01]-6 & 63 and DS:02 = [DS:00]-10 & 63, then it
#                   FALLS THROUGH into the staging loop rather than returning
#             5AE1  asm/5AE1_stage_visible_cells.asm -- 20 columns (DS:04 to 0x14) x 10 rows
#                   (DS:05 to 0x0a) = 200 staged cells at DS:272
#             4AAD  asm/4AAD_flush_staged_cells.asm -- walks those 200 cells, skips any with bit 7
#                   set, takes the art descriptor from the table at DS:5B0 and calls
#                   asm/4A74_tile_blit_cga.asm per cell
#             5C6D  asm/5C6D_underlay_copy.asm -- DS:272 -> DS:372 masked with 0x7f, i.e. record
#                   what is now on screen, which is what the next stage compares against
#           WHY IT MATTERS: asm/38B0_cmd_L's rocket-descent loop calls this entry once per march
#           step and calls nothing else, so this routine IS the descent animation and its cost IS
#           the frame rate: 492,074 emulated cycles per call measured, about 10 steps/s at 4.77
#           MHz. A reading that stops at "viewport origin" concludes the dive draws nothing and is
#           invisible. It is not, and the owner caught that from live play on 2026-11-10.
# name-evidence: entry L_005cb1 is `call`ed from asm/0BAF_monster_hp_epilogue and five
#           others (0864, 08E6, 268C, 26CC, 38B0_cmd_L, 72A0). § 15 of
#           docs/monster-spawn.md listed this entry point as unknown behaviour; this is it.
#           It opens `cmpb $0xff,0x12` and branches between two near-identical
#           computations: asm/5B98 (world map) and asm/5ABB (interior). Both set
#           DS:03 = [DS:01] - 6 and DS:02 = [DS:00] - 10 via the `stc ; sbb ; cmc` idiom,
#           and the world-map branch alone masks both with `and $0x3f` -- the torus wrap.
#           So DS:02/03 are a viewport origin, and the only difference between overworld
#           and interior is whether that origin wraps.
# DS:12 IS NOT A TILE. It is a mode byte: 0xFF on the world map, 4 in an interior sector,
#           written by asm/0C1F_status_tick_branch and asm/0C2B on the [DS:4A] branch --
#           and DS:4A is the sector's low decimal digit (docs/monster-spawn.md § 15.2).
#           Live cross-check 2026-10-28: DS:12 read 0xFF in sector 20 (digit 0) and 4 in
#           sector 32 (digit 2). Two independent bytes agreeing on the same branch.
#           Confirmed behaviourally 2026-11-02 in a third sector, 31 (digit 1, a village): the
#           rendered frame shows the unwrapped arm, so the branch follows the ones digit and not
#           the particular site. captures/spice86/s31-south-edge/.
# Its only two readers in the tree are here and asm/5AF9_sub_5af9.asm (`mov 0x12,%al`).
# label: kept as sub_5ab1 on purpose (CP3 cross-reference anchor).
# func 5AB1..5AB7  len 0007 B  evidence: seed+call+reko
# provenance: static reassembly 2026-10-28 + the two live reads above
# confidence: high for the branch and the two origin computations; the -9/-5 vs -10/-6
#           difference against asm/5AE1 (which engine/src/core/state.h cites for the
#           window origin) is unreconciled and is a real open question, not a rounding
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
sub_5ab1:
L_005cb1:
        cmpb   $0xff,0x12
L_005cb6:
        jne L_005cbb

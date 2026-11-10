# tile_blit_cga — draw one 16x16 staged cell into the CGA planes
# name-evidence: `mov %cx,%si` (the art descriptor from the DS:5B0 table), `shr $1,%ax` twice to
#           turn a pixel column into a byte offset, `shl $1,%bx` for the row, then a 16-iteration
#           loop (`mov $0x10,%dl ; dec %dl ; jne`) that takes a **segment** from the plane table at
#           CS:48DC (`mov 0x48dc(%bx),%ax ; mov %ax,%es`) and writes four bytes of the tile row at
#           `es:di`. The segment table alternates 0xB800 and 0xBA00, which is the CGA colour/CGA
#           colour-intensity pair -- the same interleaving asm/4C1E_video_xor_patch's table shows.
# note: this is why a dive step costs what it costs. 200 cells x 16 rows x two planes of segment
#           loads and word writes is the frame budget, and the animation rate falls out of it.
# reached from: asm/4AAD_flush_staged_cells.asm, once per visible cell.
# provenance: live: dive-sweep-rate 2026-11-10 (4A74..4A86 in ExecutedInstructions during a rocket
#           descent) plus the static read of file 0x4C74..0x4CAC
# confidence: high for the loop shape and the plane-segment table; the per-row byte count is read
#           from the writes, not from a pixel-level diff
# func 4A74..4A88  len 0015 B  evidence: seed+call+reko
# confidence: low (bytes proven by check_func.py)
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
sub_4a74:
L_004c74:
        push   %ax
L_004c75:
        push   %bx
L_004c76:
        push   %dx
L_004c77:
        push   %di
L_004c78:
        push   %si
L_004c79:
        push   %es
L_004c7a:
        .byte 0x8b,0xf1  # forced-kept: gas re-encodes 'mov    %cx,%si' differently
L_004c7c:
        shr    $1,%ax
L_004c7e:
        shr    $1,%ax
L_004c80:
        .byte 0x8b,0xf8  # forced-kept: gas re-encodes 'mov    %ax,%di' differently
L_004c82:
        shl    $1,%bx
L_004c84:
        mov    $0x10,%dl
L_004c86:
        add    $0x2,%si

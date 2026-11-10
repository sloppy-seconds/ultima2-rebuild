# 5B98_view_origin_wrapped — viewport origin for the WORLD MAP: origin, then `and $0x3f`
# NOT THE END OF THE ROUTINE 2026-11-10: this arm does not return. It computes DS:02/DS:03 and
#           FALLS THROUGH into the staging loop at asm/5AE1_stage_visible_cells.asm, whose tail
#           (asm/5C6D_underlay_copy.asm) calls the flush and then returns. Together with the entry
#           at asm/5AB1_map_render.asm this is the whole sector render, and it is what the rocket
#           descent calls once per march step -- see captures/spice86/dive-sweep-rate.
# name-evidence: reached only from asm/5AB1_view_origin_by_mode when DS:12 == 0xFF, i.e.
#           on the world map. `stc ; mov 0x1,%al ; cmc ; sbb $0x5,%al ; cmc ; and $0x3f,%al`
#           puts DS:03 at [DS:01] - 6 wrapped into 0..63, and the same shape with $0x9 puts
#           DS:02 at [DS:00] - 10 wrapped. The mask is the whole difference from
#           asm/5ABB_sub_5abb.asm, which computes the same two values unwrapped.
# This is the byte-level statement that the overworld is a torus at the viewport level, and
# that an interior sector is not -- which is the shape the engine's viewport-wrap fix
# reached for from the other end. See docs/monster-spawn.md § 15.2 and handoff item 6.
# scope measured 2026-11-02: the mask belongs to this branch and only this branch, and the
#           wilderness alone could never have shown it. mapx30 rows 0, 1 and 60..63 are all
#           0x00, so a wrap and a constant filler are pixel-identical at both Y edges of every
#           sector measured before today. A settlement is the only sector whose north edge is
#           not water, so a settlement is the only place the two hypotheses separate; they did
#           in captures/spice86/s31-south-edge/, and the town took the unwrapped arm. See
#           asm/5ABB_sub_5abb.asm.
# label: kept as sub_5b98 on purpose (CP3 cross-reference anchor).
# func 5B98..5BC1  len 002A B  evidence: seed+reko
# provenance: static reassembly 2026-10-28
# confidence: high for the arithmetic; see the -9/-5 vs -10/-6 note in asm/5AB1's header
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
sub_5b98:
L_005d98:
        stc
L_005d99:
        mov    0x1,%al
L_005d9c:
        cmc
L_005d9d:
        sbb    $0x5,%al
L_005d9f:
        cmc
L_005da0:
        and    $0x3f,%al
L_005da2:
        mov    %al,0x3
L_005da5:
        stc
L_005da6:
        mov    0x0,%al
L_005da9:
        cmc
L_005daa:
        sbb    $0x9,%al
L_005dac:
        cmc
L_005dad:
        and    $0x3f,%al
L_005daf:
        mov    %al,0x2
L_005db2:
        mov    $0x0,%al
L_005db4:
        mov    %al,0x5
L_005db7:
        mov    %al,0x4
L_005dba:
        mov    $0x0,%ah
L_005dbc:
        .byte 0x8b,0xf0  # forced-kept: gas re-encodes 'mov    %ax,%si' differently
L_005dbe:
        mov    $0x0,%ah
L_005dc0:
        .byte 0x8b,0xf8  # forced-kept: gas re-encodes 'mov    %ax,%di' differently

# restage_parked_rocket — second copy of the rocket restage stamp, different entry
# name-evidence: body is byte-for-byte the same stamp as asm/0839 -- DS:6A/DS:6B into the
#           target pair DS:23/DS:24, call the grid pointer L_0052b3, stamp $0x50 (the
#           rocket tile), then jmp L_000c33 which is cmd_done. Unlike asm/0839 this one
#           ends on the command-complete tail, so it is entered as a command arm rather
#           than as a subroutine of the load path.
# NAMED 2026-11-03 from a reader/writer sweep of the whole tree; it was a bulk-wave stub
#           headed "behavior not yet reviewed". What it does is settled by its bytes.
# CALLER RESOLVED 2026-11-04 (docs/archive/handoff-2026-11-03.md § 3.6). There is no branch to
#           L_00265a anywhere in the binary -- no e8/e9/ea/9a and no stored word 0x265a
#           (the one apparent hit at file 0x620e is the segment-override bytes 3e 26 in
#           code, not a pointer). It is reached by FALL-THROUGH from asm/2453, which is
#           itself a fall-through arm of asm/243E, and the whole 243E..245a run sits
#           INSIDE asm/2423_passability (runtime 0x2423..0x2680) -- the same Phase 2 map
#           cut that hid cmd_I/cmd_D/cmd_K. asm/2453 is:
#               mov 0x6d,%al ; cmp $0x0,%al ; je L_002674   ; on Earth, skip the restage
#           so this restage arm runs only when the world byte is NOT Earth. The block it
#           falls out of (asm/243E) restores the outdoor position from DS:5A/DS:5B, forces
#           the sector ones digit to 0 (a wilderness sector), calls the sector reload
#           L_002490, and then -- off Earth only -- re-stamps the parked rocket at
#           DS:6A/DS:6B before jumping to cmd_done. That is the arrival path: a rocket
#           landing in a non-Earth wilderness sector restores the return cell and puts the
#           ship back on the grid.
#           See asm/38B0_cmd_L for the DS:69/6A/6B triple's semantics.
# func 245A..2473  len 001A B  evidence: seed+reko
# calls: L_000c33, L_0052b3
# provenance: live:flight-launched-live (+6 other harvests: airport32, dungeon-35-live, edge23, edge31, south31, west31), static
# confidence: low (bytes proven by check_func.py)
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
sub_245a:
L_00265a:
        mov    0x6a,%al
L_00265d:
        mov    %al,0x23
L_002660:
        mov    0x6b,%al
L_002663:
        mov    %al,0x24
L_002666:
        call L_0052b3
L_002669:
        mov    $0x50,%al
L_00266b:
        mov    0x6,%bx
L_00266f:
        mov    %al,(%bx,%si)
L_002671:
        jmp L_000c33

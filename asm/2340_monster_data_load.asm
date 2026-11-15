# monster_data_load — reads the 256-byte creature block into the creature arrays
# name-evidence: `b9 00 01 00` mov $0x100,%cx (256 records of one byte) and `8d 06 37 01`
#           lea 0x137,%dx, then the DOS seam. DS:137 is `mon_x[32]` (state.h:430, with a
#           _Static_assert pinning the offset), so 256 bytes from there is the monx block: the
#           x, y, kind and hit-point arrays for the 32 creature slots. The `MONXFF  ` template at
#           runtime 0x234a sits in this span and is what the name is patched into.
# func 2340..2351  len 0012 B  evidence: seed+reko
# note: AH is NOT set here, and neither is the FCB name. Both come from the caller, which is why
#           this reads as a fragment rather than a routine: asm/2305_sector_store is what patches
#           the MONX name, and the read/write mode is whichever AH the path left in place.
#           asm/22BF_sub_22bf is the same read on the load path (same CX, same DX) with its own
#           copy of the MONXFF template at runtime 0x22c9, so the binary carries the template
#           twice: once for the load pair (asm/2290 patches the MAPX sibling at runtime 0x22b7)
#           and once for the store pair.
# note: the last eight bytes of this span are NOT code. `4d 4f 4e 58 46 46 20 20` is the
#           `MONXFF  ` template, and the disassembler invented `dec %bp`, `dec %di`, `inc %si`,
#           `and %ah,(%bx,%si)` from the letters. Same failure as asm/2305's tail, asm/1B6B,
#           asm/16BD and asm/4C50. Left in place because the template belongs to this routine.
# provenance: static (bytes read from original/ultimaii.exe at file 0x2540)
# confidence: medium (the transfer size and destination are read directly; the caller that sets AH
#           and selects this FCB has not been traced)
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
sub_2340:
L_002540:
        mov    $0x100,%cx
L_002543:
        lea    0x137,%dx
L_002547:
        call L_0054ca
L_00254a:
        dec    %bp
L_00254b:
        dec    %di
L_00254c:
        dec    %si
L_00254d:
        pop    %ax
L_00254e:
        inc    %si
L_00254f:
        inc    %si
L_002550:
        and    %ah,(%bx,%si)

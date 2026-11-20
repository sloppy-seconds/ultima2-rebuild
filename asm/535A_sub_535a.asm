# sub_535a — an INT 21h FCB read of one record into DS:0x245, sitting in the picture load
# name-evidence: `pop %ds` restores the data segment that asm/5351_sub_5351.asm had pointed at
#           the framebuffer, then `lea 0x245,%dx` sets the transfer buffer, `movw $0x1,0x253`
#           sets an FCB record count of one, and the function number is taken from a table at
#           CS:4F04 (with CS:4F05 feeding CX) rather than being a literal -- the same
#           table-driven DOS call shape the sector reads elsewhere in this binary use.
# What is NOT claimed, and why the file keeps its placeholder name: a pic file is 16,384 bytes
#           of mode-4 video (32 sectors of 512), and this read asks for ONE record into a fixed
#           offset. Either the picture arrives through a path this routine is not part of, or it
#           arrives in many one-record reads with the buffer advanced by a caller that has not
#           been identified. Both are consistent with the bytes and neither is established, so
#           "pic_read" would overstate it. The empirical fact stands on its own regardless: the
#           files are literal mode-4 pixel data, established positionally against a live capture
#           on 2026-11-21 (see asm/5351's header, which this line used to point at for a census
#           claim that could not have distinguished the two candidate layouts).
# func 535A..5370  len 0017 B  evidence: seed+reko
# seams: dos_int21
# provenance: read 2026-11-03 while implementing the engine's splash pictures
# confidence: high for the instructions and the transfer-buffer offset; the pairing with
#           asm/5351 and the role of this read in loading a picture are inferred, not proven
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
sub_535a:
L_00555a:
        pop    %ds
L_00555b:
        lea    0x245,%dx
L_00555f:
        mov    %cs:0x4f05,%cx
L_005564:
        movw   $0x1,0x253
L_00556a:
        mov    %cs:0x4f04,%ah
L_00556f:
dos_int21:
        int $0x21    # dos_int21 seam

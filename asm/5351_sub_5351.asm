# sub_5351 — points DS at the CGA framebuffer: the address half of the picture load
# name-evidence: `mov $0xb800,%ax ; mov %ax,%ds`. In video mode 4 -- which asm/4A3E sets at
#           startup and the whole game runs in -- 0xB800 is not a scratch segment, it is the
#           framebuffer itself. Pointing DS there is what makes a subsequent read land on
#           screen with no drawing step at all.
# Why the pic* files need no decoder, measured rather than inferred: they are literal images
#           of mode-4 video, but NOT in the way this header used to claim. The claim was
#           "even scanlines at offset 0 and odd at 8000 reproduces a live captured frame at
#           100% colour-class agreement", and a colour-class agreement is a census, which
#           cannot distinguish the two candidate layouts at all -- both give exactly 11,567
#           inked pixels for picdra. Corrected 2026-11-21 by positional measurement against
#           a live capture (captures/spice86/cga-splash-live/): displayed row 2n equals file
#           row n on 187 of 187 sampled rows across two pictures, while displayed row 2n+1
#           equals file row 100+n on 2 of 100. The art is 320x100 displayed doubled, so only
#           the first 8,000 bytes of a pic file are pixels. The engine's unpack is gated on
#           that by engine/tests/pic_check.c, which now pins scanline pairs identical rather
#           than trusting any count.
# NOT renamed on purpose: the half that actually moves the bytes is asm/535A_sub_535a.asm,
#           and that read's transfer buffer is DS:0x245 with a record count of 1, which is not
#           16,000 bytes. So this routine's role is established and the pair's mechanism is
#           not, and "pic_load" would be a name for something only half read. See its header.
# func 5351..5355  len 0005 B  evidence: seed+reko
# provenance: read 2026-11-03 while implementing the engine's splash pictures
# confidence: high for what the two instructions do and for what 0xB800 is in mode 4; the
#           caller and the pairing with asm/535A are inferred
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
sub_5351:
L_005551:
        mov    $0xb800,%ax
L_005554:
        mov    %ax,%ds

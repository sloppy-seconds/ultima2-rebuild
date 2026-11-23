# space_ship_draw — draws the ship's sprite: a symmetric plus, 13 pixels, at DS:47B/DS:47C
# name-evidence: the body sets the plot pair `0x479/0x47A` from `0x47B/0x47C` and calls
#           `L_004d76` (pixel_plot) thirteen times in two perpendicular sweeps, advancing
#           one axis between calls. Its exact mirror is asm/6CC4, which calls pixel_erase
#           thirteen times with the same offsets, and the two agree pixel for pixel -- the
#           sprite is drawn and erased by the same shape, which is what a sprite is.
#           Computed, not eyeballed: walking the body and recording (0x479, 0x47A) at every
#           call gives
#               vertical    x = 0,  y = -3 -2 -1 0 +1 +2 +3      (7 pixels)
#               horizontal  y = 0,  x = -3 -2 -1    +2 +3  ...   (6 pixels, x=0 skipped)
#           so it is a symmetric 7x7 plus whose centre is plotted once, because the
#           horizontal arm steps over the column the vertical arm already drew.
#           CORRECTION 2026-11-23: asm/6CC4's header records the vertical as `DS:47C-3 to
#           DS:47C+2`, six pixels, and the engine implemented that -- an off-centre plus.
#           The body makes seven calls with six `incb` between them, so the vertical runs
#           -3..+3 like the horizontal. The engine's `U2_SPACE_SHIP_V_TO` was +2 and is now
#           +3.
# calls: L_004d76 (pixel_plot), 13 sites
# provenance: live:dive-rate2 (+10 other harvests: dive-rate2-live, dive-sweep-rate,
#           g39-rocket-flight, g60, g62h, g85, launch, w6-orbit-dive, w6-three,
#           w6-three-live), static
#           entry executed in 11 of 291 harvests; derived by tools/provenance_from_traces.py
# confidence: high for the shape (both arms enumerated above, and asm/6CC4 corroborates
#           pixel for pixel); the routine is called from asm/6B3D's head, once per pass.
# func 6D83..6DFC  len 007A B  evidence: call+reko
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
sub_6d83:
L_006f83:
        mov    0x47b,%al
L_006f86:
        mov    %al,0x479
L_006f89:
        mov    0x47c,%al
L_006f8c:
        stc
L_006f8d:
        cmc
L_006f8e:
        sbb    $0x3,%al
L_006f90:
        cmc
L_006f91:
        mov    %al,0x47a
L_006f94:
        call L_004d76
L_006f97:
        incb   0x47a
L_006f9b:
        call L_004d76
L_006f9e:
        incb   0x47a
L_006fa2:
        call L_004d76
L_006fa5:
        incb   0x47a
L_006fa9:
        call L_004d76
L_006fac:
        incb   0x47a
L_006fb0:
        call L_004d76
L_006fb3:
        incb   0x47a
L_006fb7:
        call L_004d76
L_006fba:
        incb   0x47a
L_006fbe:
        call L_004d76
L_006fc1:
        mov    0x47b,%al
L_006fc4:
        stc
L_006fc5:
        cmc
L_006fc6:
        sbb    $0x3,%al
L_006fc8:
        cmc
L_006fc9:
        mov    %al,0x479
L_006fcc:
        mov    0x47c,%al
L_006fcf:
        mov    %al,0x47a
L_006fd2:
        call L_004d76
L_006fd5:
        incb   0x479
L_006fd9:
        call L_004d76
L_006fdc:
        incb   0x479
L_006fe0:
        call L_004d76
L_006fe3:
        incb   0x479
L_006fe7:
        incb   0x479
L_006feb:
        call L_004d76
L_006fee:
        incb   0x479
L_006ff2:
        call L_004d76
L_006ff5:
        incb   0x479
L_006ff9:
        call L_004d76
L_006ffc:
        ret

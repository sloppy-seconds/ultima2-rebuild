# swamp_damage — subtract 5 HP, packed decimal, when the player enters a swamp tile
# name-evidence: the subtract itself. `stc; mov al,[DS:52]; cmc; sbb al,0x05; das; cmc;
#           mov [DS:52],al` then the same idiom with `sbb al,0x00` on [DS:51], then
#           `jb L_0026b1` and `jmp L_000e62`. The trailing `cmc` after each `das` re-exposes
#           the borrow INVERTED, so `jb` (CF set) is the SURVIVAL path — it continues to the
#           next rung of the effect ladder at asm/24B1 — and the fall-through `jmp L_000e62`
#           (file 0x0C62, asm/0C62_death) is the death tail. An earlier version of this header
#           had the two branches the other way round; the net fact (underflow is death) never
#           changed, but the branch map did, corrected 2026-11-02 by re-reading the idiom
#           against bcd_sbb's documented contract in the engine. DS:51/DS:52 are the HP
#           BCD pair, so this is a fixed 5 HP cost with borrow, not a randomised hit —
#           it is NOT asm/0BDB_apply_hp_damage, whose amount is `(rand & [DS:22]) & 0x77 + 1`.
#           Reached only from asm/248B, which gates on mode 0x02 == map byte 0x04 (swamp).
#           OBSERVED live 2026-11-02: HP read at DS:51/52 across nine consecutive steps
#           went 0370 0365 0360 0355 0350 0345 0340 0335 0330 — every delta exactly -5.
#           OWNER control the same day: PASS while standing on swamp costs nothing,
#           safe->swamp costs 5, swamp->safe costs nothing, so the charge is for ENTERING.
#           Silent: the message log during the walk showed only PASS.
#           NOT a flash event, and that is now observed, not just read: three controlled
#           swamp entries 2026-11-02 (captures/spice86/swamp-flash-20261102) show the
#           subtract executing with the flash family (asm/5255/525E/5267/5270 -> 527F ->
#           4C1E) and both tone routines at ZERO executions. A recalled "it flashes like
#           being hit" is the viewport redraw, not this routine.
#           Borrow propagation confirmed by a poked single step: HP 0400 -> 0395.
#           Renamed 2026-11-02 from sub_2495. Full table and the engine work still owed:
#           docs/archive/handoff-tile-effects.md.
# provenance: static read of the asm/2423 span 2026-11-02, confirmed live the same day:
#           HP read at DS:51/52 over nine consecutive steps on map byte 0x04 in sector 20
#           (0370..0330, every delta -5), and again earlier as 0400->0385 over three steps.
#           Owner control established the enter/exit/PASS asymmetry. Table and follow-up
#           work: docs/archive/handoff-tile-effects.md.
# func 2495..24B0  len 001C B  evidence: seed+reko
# calls: L_000e62
# confidence: medium (bytes proven by check_func.py; the meaning is observed live)
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
sub_2495:
L_002695:
        stc
L_002696:
        mov    0x52,%al
L_002699:
        cmc
L_00269a:
        sbb    $0x5,%al
L_00269c:
        das
L_00269d:
        cmc
L_00269e:
        mov    %al,0x52
L_0026a1:
        mov    0x51,%al
L_0026a4:
        cmc
L_0026a5:
        sbb    $0x0,%al
L_0026a7:
        das
L_0026a8:
        cmc
L_0026a9:
        mov    %al,0x51
L_0026ac:
        jb L_0026b1
L_0026ae:
        jmp L_000e62

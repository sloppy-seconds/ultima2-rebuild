# monster_step_vector — DS:19/DS:1A := the clamped one-cell step toward the player
# name-evidence: per axis, `stc ; mov al,[DS:00] ; sub al,[di+0x137] ; add al,al ;
#           add al,al ; call L_0052d8` and store to DS:19 (DS:01 / [di+0x157] / DS:1A).
#           asm/50D8_sign_of_al returns -1/0/+1, so the stored value is a unit step,
#           not a distance. This is the value asm/212E adds to mon_x/mon_y.
# WHY THE x4: sign(4*d) is not sign(d). 4*d overflows the signed byte at |d| >= 32,
#           which is exactly half the 64-cell grid, so the doubling-in-place is what
#           makes the vector take the SHORT WAY ROUND the torus rather than the long
#           way. Measured over all 4096 (player_x, mon_x) pairs: **0 mismatches** against
#           the true shortest torus direction, once the 64 exact-antipode cells (32 apart,
#           where both ways are equal) are set aside.
# THE `stc` IS A NO-OP, and a claim here said otherwise and was wrong. x86 `SUB` does
#           NOT subtract CF — only `SBB` does — so `stc` here changes nothing and the
#           difference is the plain player - mon. It is almost certainly a translated 6502
#           `SEC ; SBC`, where the SEC *was* required (6502 SBC computes A - M - 1 + C),
#           and carrying it over to SUB is harmless rather than buggy. RETRACTION 2026-10-28,
#           same day as the claim: an earlier version of this header asserted the port gained
#           a `-1` and that a creature therefore parked one cell short when approaching from
#           above/left. That was built on a misremembered flag semantic. Measured live in
#           captures/spice86/g28-pursuit: DS:19/DS:1A read after a sweep are the plain
#           symmetric difference in 20/20 trials with a valid control, including a creature
#           at (player-1, player-1) stepping straight onto the player. Contrast asm/0B0F,
#           which uses `stc ; cmc ; sbb` — there the pair genuinely matters, because SBB
#           does read CF.
# reached from: asm/0AA7 calls asm/4F36_monster_pursuit_vector first (the raw delta,
#           used by the asm/0AAA special-attack distance test), then every path through
#           the special dispatch joins at asm/0AFC, which calls THIS routine.
# func 4F11..4F1F  len 000F B  evidence: seed+call+reko
# apple2-twin: $4054 in STD.SUBS.OBJ, reached from the JMP vector $4000. Same shape per axis:
#           `SEC ; LDA $00 ; SBC $4D00,X ; ASL A ; ASL A ; JSR $4036` then store to $17 ($18
#           for the y pair via $4D20). Shared sign routine at $44D5 via vector $4036, shared
#           zero-page destination pair, same x4, same 32-byte-strided table pair ($4D00/$4D20
#           = this file's DS:137/DS:157). CORROBORATED, not observed: a second independent
#           implementation agrees, which is what makes the torus reading of the x4 original
#           1983 design rather than a DOS-port invention. It does not rename this file and it
#           does not raise the grade. Found 2026-10-28 via tools/apple2_anchor_map.py and the
#           hand-decode in docs/archive/handoff-apple2-crosswalk.md T0.
# provenance: static, decoded 2026-10-28; the x4/torus reading and the retraction of the
#           -1 were both settled live the same day (captures/spice86/g28-pursuit)
# confidence: high (bytes, and the resulting vector measured at runtime per slot)
# label: kept as sub_4f11 on purpose — labels are body and CP0 md5s them
# calls: L_0052d8
# confidence: low (bytes proven by check_func.py)
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
sub_4f11:
L_005111:
        stc
L_005112:
        mov    0x0,%al
L_005115:
        sub    0x137(%di),%al
L_005119:
        .byte 0x02,0xc0  # forced-kept: gas re-encodes 'add    %al,%al' differently
L_00511b:
        .byte 0x02,0xc0  # forced-kept: gas re-encodes 'add    %al,%al' differently
L_00511d:
        call L_0052d8

# find_creature_at_cursor — scan the 32 creature slots for one standing at player+delta
# name-evidence: seeds DS:23/24 to [DS:00]+[DS:19] and [DS:01]+[DS:1A], sets DI=31, then
#           loops DI downward comparing [DI+0x197] (type, skip if 0), [DI+0x137] (x) and
#           [DI+0x157] (y) against the cursor. Returns at 241E with DI = the matching
#           slot, or falls out at 2422 with DI == 0 when the scan exhausts. Three inbound
#           callers: asm/4050_cmd_T (runtime 4060), asm/2BC4, asm/35F0.
# func 23E6..2422  len 003D B  evidence: seed+call+reko
# provenance: static; reached live by the 2026-10-23 armour purchase (cmd_T path)
# confidence: high -- the loop is nine instructions and its two exits explain the branch
#           that the fragment view made look unreachable.
#
# MERGED 2026-10-23 from 7 fragments (23E6, 2402, 240A, 2414, 241E, 241F, 2422) under the
# owner's split ruling. Body lines unchanged and in address order; asm_body_guard.py
# proves it.
#
# WHY THIS MATTERS
#   asm/4050_cmd_T does `cmp bl,0 ; jne 40C0` on the low byte of DI immediately after
#   calling here. Read as fragments, DI looked like a constant 31, which would have made
#   the map-tile path dead code -- yet the shop opened. It is not dead: DI is whatever this
#   scan left behind. Creature on the cursor -> DI = slot -> creature path. Nothing there
#   -> DI == 0 -> map-tile path.
#
# DOUBT -- read this before citing it
#   * The cursor is `player + DS:19/1A`, i.e. exactly ONE delta step from the player, and
#     the scan is an equality test against that single cell. So this is not a ray with a
#     reach; it is a one-cell probe. Any description of TRANSACT as "looking down a line"
#     is not supported by this routine.
#   * **What writes DS:19/1A from a direction is still unknown.** Every live sample reads
#     (1,1). Candidates examined and ruled out: asm/1086, asm/10AA, asm/10BF (they move
#     values to and from the per-entity arrays [DI+0x1F7]/[DI+0x217] via DS:25 -- not
#     keystrokes). asm/0B0F negates DS:19/1A in place (stc;cmc;sbb from zero), so it is a
#     sign flip that some caller could use, but it does not source a direction. asm/152E
#     sets them to player minus [DI+0x137]/[DI+0x157], i.e. a vector from a creature, also
#     not a keystroke. So four of the five static writers are accounted for and none of
#     them is the direction handler. Do not claim the probe is aimed by the direction key.
#   * The scan starts at slot 31 and stops when DI reaches 0, so **slot 0 is never
#     examined** -- `dec di ; jne` exits on reaching zero. Whether slot 0 can ever hold a
#     creature that should be found is not established here. In monx31 slot 0 is a Guard at
#     (29,30), so it is populated in at least one sector.
#
# SLOT-0 INVULNERABILITY -- CONFIRMED 2026-11-12 (this closes G78).
#   The `dec %di ; jne` loop examines slots 31..1 and falls out at DI==0 WITHOUT examining
#   slot 0. Because asm/4050_cmd_T (talk), asm/2BC4 (melee ATTACK) and asm/35F0 (cannon)
#   all branch on `DI==0 -> no creature`, a creature parked in slot 0 is invisible to all
#   three interactions: melee prints MISS (via asm/2BC7 `cmp $0,%bl; jne` falling to the
#   MISS print at asm/2BD4->L_003009), talk prints FUNNY, NO RESPONSE, and the cannon finds
#   nothing -- while the creature still draws on screen, still blocks the cell, and still
#   runs its own AI and attacks the player. That is the whole invulnerability: there is no
#   invulnerable flag, no locked HP byte and no monster-defense modifier in the hit roll
#   (the roll at asm/2BDA is `hit iff (rand>>1) < [DS:4C]`, player agility only, measured);
#   the creature is simply never scanned. LIVE PROOF (captures of 2026-11-12, Port Boniface
#   sector 31, maxout agi-99 character): the guard the owner could not kill was slot 0 at
#   (31,47); the three guards the same character killed in the same session were slots 1, 3
#   and 4. All four are byte-identical (type 0x60, kind 0x70, role 0x01), which is exactly
#   why no creature-table column can predict it -- the discriminator is the SLOT INDEX, not
#   the creature's bytes. A breakpoint at the melee scan entry (runtime 2e3d) never fired on
#   the invulnerable guard's attacks, confirming the attack is refused at asm/2BC7 (DI==0)
#   before the roll and before asm/2E3D's damage scan. Engine divergence: u2_monsters_at
#   scans slot 0, so the engine lets the player kill a creature the original makes
#   unkillable; the faithful fix is to skip slot 0 in the target scan, matching this loop.
#   * Two `ret`s, not one: 241E (found) and 2422 (exhausted). That is why the merged span
#   * still grades as one routine -- the split rule requires the ret in the last row, and
#     both are in the last row here.
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
sub_23e6:
L_0025e6:
        clc
L_0025e7:
        mov    0x0,%al
L_0025ea:
        adc    0x19,%al
L_0025ee:
        mov    %al,0x23
L_0025f1:
        clc
L_0025f2:
        mov    0x1,%al
L_0025f5:
        adc    0x1a,%al
L_0025f9:
        mov    %al,0x24
L_0025fc:
        mov    $0x0,%bh
L_0025fe:
        mov    $0x1f,%bl
L_002600:
        .byte 0x8b,0xfb  # forced-kept: gas re-encodes 'mov    %bx,%di' differently
sub_2402:
L_002602:
        mov    0x197(%di),%al
L_002606:
        cmp    $0x0,%al
L_002608:
        je L_00261f
sub_240a:
L_00260a:
        mov    0x137(%di),%al
L_00260e:
        cmp    0x23,%al
L_002612:
        jne L_00261f
sub_2414:
L_002614:
        mov    0x157(%di),%al
L_002618:
        cmp    0x24,%al
L_00261c:
        jne L_00261f
sub_241e:
L_00261e:
        ret
sub_241f:
L_00261f:
        dec    %di
L_002620:
        jne L_002602
sub_2422:
L_002622:
        ret

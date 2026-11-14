# cast_target_finder — the CAST target scan: the first creature, slot 31 down to 1,
#           whose mon_type is non-zero, whose mon_kind equals DS:25, and whose
#           mon_x/mon_y are exactly DS:00+DS:02 and DS:01+DS:03. Found -> returns with
#           DI = the slot and ZF clear; nothing matched -> AL = 0, ZF set.
# func 330C..334C  len 0041 B  evidence: call
# name-evidence: reached only from the CAST dispatch (asm/3139_cast_spell_flow.asm
#           L_0034ad = slot 7 and L_003501 = slots 6 and 9), and every one of the
#           three tests was confirmed live on 2026-10-29 in captures/spice86/
#           cmd-c-magic-missile (a scripted hit, a scripted kill, and three refusals).
#
# TRAP, and it produced a wrong answer the same day this was decoded: the two adds are
#           `adc 0x2,%al` and `adc 0x3,%al` with NO `$`. In gas AT&T that is a MEMORY
#           operand — DS:02 and DS:03 — not the constants 2 and 3. Read as immediates
#           the scan aims two cells east and three south of the player, which is a
#           parity-impossible window (site cells are odd, odd+3 is even) and made the
#           spell look dead. DS:02/DS:03 are a SIGNED facing delta: measured live as
#           (01,00) on entering the tower, (00,FF) after one Left turn, and unchanged
#           by Down. So the spell hits the creature standing in the cell you face.
#           The 8-bit wrap is real and it is in the game's favour: (5,5) facing (0,-1)
#           aims at (5,4), not (5,260) — a driver that adds in 16 bits misses.
#           CONFIRMED AGAINST THE RENDERED VIEW 2026-10-29, which is the one link in this
#           model that had never been filmed. Every prior hit was a creature the driver held
#           in the facing cell, so the delta had only ever been checked against memory. In
#           owner play (captures/spice86/mm-owner-play) the player walked up, turned, said
#           the creature "was right in front of me" in the 3D view, pressed C, and the frame
#           shows `CAST-MAGIC MISSILE` then `KILLED--GOLD+13--EXP.+03` with GOLD 0004 -> 0017
#           and EXP 0000 -> 0003. So DS:02/03 is what the corridor shows ahead of you, and
#           the renderer does draw the occupant of the facing cell while it is alive.
#
# GRADES. observed: the three tests, the scan order (31 downwards, so the highest live
#           slot wins), the facing bytes, and that a match is required — a cast with an
#           empty facing cell printed `CAST-MAGIC MISSILE-FAILED!` and still spent the
#           scroll.
#           REFUTED 2026-10-29, and it was this file's own inference: that mon_kind is a floor
#           index and DS:25 the caster's floor. DS:25 IS the site floor (proven by the ladder
#           spells in captures/spice86/d12-ladder-ds25 — see asm/3139 and state.h), but mon_kind
#           is not a floor: in the sector-24 tower the creatures standing on floor 0 carry
#           mon_kind 0..15 simultaneously, and monx24 is all zeros, so the kinds are generated at
#           load rather than authored per floor. What the second test really is, measured:
#           `mon_kind == the caster's floor number`. Odd, and it is the game's. Live flip: at
#           DS:25 = 1 a held creature with mon_kind = 1 in the facing cell was hit (HP
#           0x99 -> 0x47), where the same poke with mon_kind = 0 was refused. So do not read a
#           kind match as "same floor as me". Whether the comparison means anything in a
#           genuinely multi-floor dungeon is untested; the sector-24 tower has one usable floor.
# measured: in the sector-24 tower the natural table had mon_kind 0 at slots 0 and 1
#           only (slot 0 empty), so with DS:25 = 0 the kind test admits exactly one
#           creature. Three scripted refusals were that test failing, not the position
#           test; the one natural success (frame d) was slot 1 wandering into the
#           facing cell.
#
# RECONFIRMED IN A REAL DUNGEON 2026-11-15 (captures/spice86/magic-casts-2026-11-15). Owner play
#           in sector 25, which has at least four floors, on floor 1: MAGIC MISSILE, PRAYER and
#           KILL each destroyed a creature the owner could see in the corridor ahead, with no
#           scripted pokes at all, so the one-cell facing rule is not a tower artefact. What is
#           still NOT measured is the kind test on a floor above 0 — mon_kind was not read at the
#           moment of any of those kills, so "the casts succeeded, therefore the kind test admitted
#           them" is an inference, not an observation. Whoever settles whether mon_kind == DS:25
#           means anything on a multi-floor dungeon should read the kind array at the kill, not
#           after it.
#
# provenance: live:cmd-c-magic-missile; entry stopped at runtime 0x330C with
#           tools/bp_read.py four times and the arrays dumped at the stop, which is
#           the only way this was settled — a poke-then-press race against the site
#           tick lost three times before that (recipe: docs/spice86-capture.md).
# confidence: high for the three tests and the facing semantics (bytes plus live
#           confirmation at both return exits); low for what mon_kind means.
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
sub_330c:
L_00350c:
        mov    $0x0,%bh
L_00350e:
        mov    $0x1f,%bl
L_003510:
        .byte 0x8b,0xfb  # forced-kept: gas re-encodes 'mov    %bx,%di' differently
L_003512:
        mov    0x197(%di),%al
L_003516:
        .byte 0x0a,0xc0  # forced-kept: gas re-encodes 'or     %al,%al' differently
L_003518:
        jne L_003522
L_00351a:
        dec    %di
L_00351b:
        jne L_003512
L_00351d:
        mov    $0x0,%al
L_00351f:
        .byte 0x0a,0xc0  # forced-kept: gas re-encodes 'or     %al,%al' differently
L_003521:
        ret
L_003522:
        mov    0x1b7(%di),%al
L_003526:
        cmp    0x25,%al
L_00352a:
        jne L_00351a
L_00352c:
        mov    0x0,%al
L_00352f:
        clc
L_003530:
        adc    0x2,%al
L_003534:
        cmp    0x137(%di),%al
L_003538:
        jne L_00351a
L_00353a:
        mov    0x1,%al
L_00353d:
        clc
L_00353e:
        adc    0x3,%al
L_003542:
        cmp    0x157(%di),%al
L_003546:
        jne L_00351a
L_003548:
        .byte 0x8b,0xc7  # forced-kept: gas re-encodes 'mov    %di,%ax' differently
L_00354a:
        .byte 0x0a,0xc0  # forced-kept: gas re-encodes 'or     %al,%al' differently
L_00354c:
        ret

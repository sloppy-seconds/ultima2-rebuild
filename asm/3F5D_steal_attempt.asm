# steal_attempt — STEAL command outcome rolls
# name-evidence: strings 'NO LUCK!' / 'STEAL FOOD!' in cmd_S chain
# func 3F5D..404F  len 00F3 B  evidence: reko
# strings: "NO LUCK!"; "STEAL FOOD!"; "STEAL ARMOUR!"; "STEAL WEAPONS!"
# calls: L_000c33, L_00133a, L_004198, L_004224, L_0051df (str_out), L_0052b3, L_005417
# provenance: static
#           entry never appears in ExecutedInstructions of any of the 60 harvests; derived by tools/provenance_from_traces.py
# confidence: low (bytes proven by check_func.py)
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
#
# THE OUTCOME TABLE, read 2026-10-29. This is the player stealing from a shoppe, and it is
# NOT the same mechanic as asm/0FCD_combat_thief_steal, where a THIEF creature empties one of
# the player's slots in combat. The two were conflated in engine/src/game/commands.c for four
# days, which is why cmd_S was filed as "already modelled in combat.c, just unwired". It is
# neither modelled nor related; only the English word is shared.
#
#   far cell != 0x68                       ->  "NO LUCK!"
#   [DS:47] != 3 (not a THIEF)             ->  one roll, js -> "NO LUCK!"
#   then, for everyone:                     ->  one roll, js -> "NO LUCK!"
#           [DS:1D7] == 1  ->  "NO LUCK!"   (the town-alarm flag; see the note below)
#   [DS:4A] == 1 (village) and y < 0x20 and x >= 0x20  ->  "STEAL FOOD!"    [DS:53] += 1, BCD
#   [DS:4A] == 2 (town)   and y < 0x20 and x >= 0x20  ->  "STEAL ARMOUR!"  [DS:96 + (roll&3)+1] += 1
#   [DS:4A] == 2 (town)   and y < 0x20 and x <  0x20  ->  "STEAL WEAPONS!" [DS:76 + (roll&7)+1] += 1
#   anything else, including [DS:4A] not in {1,2}     ->  "NO LUCK!"
#
# The quadrant rule is the same table as the TRANSACT selector in asm/1590, and it agrees
# entry for entry: a village's north-east is the grocery and the loot is food; a town's north-
# east is the armour shoppe and the loot is armour; a town's north-west is the weapons shoppe
# and the loot is weapons. The south halves are not implemented by the game at all, so a
# correctly-aimed steal at a transport shoppe, a pub, a shrine or a magic shoppe is simply
# "NO LUCK!". The item stolen is RANDOM within the category (roll & 3 for armour, & 7 for
# weapons, then +1 so slot 0 is never stolen), which is a different shape from buying, where
# the menu digit picks it.
#
# What a THIEF actually buys is one skipped roll, not eligibility: the class test at
# `cmp $0x3,[DS:47]` jumps PAST the first roll rather than gating the command, and the second
# roll is unconditional. So any class can steal and a thief just fails less often. Grade
# `measured` from the listing; the class byte identity is the proven one (asm/1841 tests 1 for
# cleric, asm/19AF tests 2 for wizard).
#
#           RESOLVED 2026-11-13 on the [DS:1D7] == 1 test. DS:1D7 is the base of the creature
# role array, and this is a bare un-indexed read of slot 0. It is NOT a role: [slot+0x1D7]==0x01
# is the town-ALARM flag (see asm/40C0's header and docs/spice86-live-findings.md § 16, capture
# cp5-g25-aggro). asm/113A (u2_ai_alarm) sets every slot 7..0 to 0x01 when a blow lands in a
# town, and asm/114A is the loop that writes them, so slot 0 is always written and the un-indexed
# read is a valid "is the town on alert" proxy. So the rule is: you cannot rob a shoppe while the
# town is alerted. The engine refuses on mon_role[0]==1 identically, which is now known-correct.
#
# DECODED 2026-11-13: the "NO LUCK!" path rolls once more and calls asm/133a only when the roll's
# low three bits are zero — `and $0x7,%al ; jne` skips it — so failing a steal has some
# consequence about one time in eight. What it is: asm/133a IS u2_ai_alarm (the same routine a
# landed blow calls), so a botched steal alerts the whole town's guards about one time in eight.
# Every failed steal reaches this exit (asm/3F59's near-cell miss and asm/3F5D's merchant miss,
# both luck rolls, the already-alerted test and every quadrant miss all jump to L_00417e), so the
# alarm can fire on an aim miss too. Engine wired in commerce.c steal_no_luck(); gated by
# engine/tests/steal_alarm_check.c (CP6).
steal_attempt:
L_00415d:
        nop
L_00415e:
        clc
L_00415f:
        mov    0x23,%al
L_004162:
        adc    0x19,%al
L_004166:
        mov    %al,0x23
L_004169:
        clc
L_00416a:
        mov    0x24,%al
L_00416d:
        adc    0x1a,%al
L_004171:
        mov    %al,0x24
L_004174:
        call L_0052b3
L_004177:
        cmp    $0x68,%al
L_004179:
        jne L_00417e
L_00417b:
        jmp L_004198
L_00417d:
        nop
L_00417e:
        call L_0051df
        db "NO LUCK!",0x00        # string
L_00418a:
        call L_005417
L_00418d:
        and    $0x7,%al
L_00418f:
        jne L_004194
L_004191:
        call L_00133a
L_004194:
        nop
L_004195:
        jmp L_000c33
L_004198:
        nop
L_004199:
        mov    0x47,%al
L_00419c:
        cmp    $0x3,%al
L_00419e:
        je L_0041a5
L_0041a0:
        call L_005417
L_0041a3:
        js L_00417e
L_0041a5:
        nop
L_0041a6:
        call L_005417
L_0041a9:
        js L_00417e
L_0041ab:
        mov    0x1d7,%al
L_0041ae:
        cmp    $0x1,%al
L_0041b0:
        je L_00417e
L_0041b2:
        mov    0x4a,%al
L_0041b5:
        cmp    $0x1,%al
L_0041b7:
        je L_0041ce
L_0041b9:
        cmp    $0x2,%al
L_0041bb:
        jne L_00417e
L_0041bd:
        mov    0x1,%al
L_0041c0:
        cmp    $0x20,%al
L_0041c2:
        jae L_00417e
L_0041c4:
        mov    0x0,%al
L_0041c7:
        cmp    $0x20,%al
L_0041c9:
        jae L_0041f9
L_0041cb:
        jmp L_004224
L_0041cd:
        nop
L_0041ce:
        mov    0x1,%al
L_0041d1:
        cmp    $0x20,%al
L_0041d3:
        jae L_00417e
L_0041d5:
        mov    0x0,%al
L_0041d8:
        cmp    $0x20,%al
L_0041da:
        jb L_00417e
L_0041dc:
        nop
L_0041dd:
        call L_0051df
        db "STEAL FOOD!",0x00        # string
L_0041ec:
        clc
L_0041ed:
        mov    0x53,%al
L_0041f0:
        adc    $0x1,%al
L_0041f2:
        daa
L_0041f3:
        mov    %al,0x53
L_0041f6:
        jmp L_000c33
L_0041f9:
        nop
L_0041fa:
        call L_0051df
        db "STEAL ARMOUR!",0x00        # string
L_00420b:
        call L_005417
L_00420e:
        and    $0x3,%al
L_004210:
        mov    $0x0,%ah
L_004212:
        .byte 0x8b,0xf8  # forced-kept: gas re-encodes 'mov    %ax,%di' differently
L_004214:
        inc    %di
L_004215:
        clc
L_004216:
        mov    0x96(%di),%al
L_00421a:
        adc    $0x1,%al
L_00421c:
        daa
L_00421d:
        mov    %al,0x96(%di)
L_004221:
        jmp L_000c33
L_004224:
        nop
L_004225:
        call L_0051df
        db "STEAL WEAPONS!",0x00        # string
L_004237:
        call L_005417
L_00423a:
        and    $0x7,%al
L_00423c:
        mov    $0x0,%ah
L_00423e:
        .byte 0x8b,0xf8  # forced-kept: gas re-encodes 'mov    %ax,%di' differently
L_004240:
        inc    %di
L_004241:
        clc
L_004242:
        .byte 0x8a,0x85,0x76,0x00  # forced-kept: gas re-encodes 'mov    0x76(%di),%al' differently
L_004246:
        adc    $0x1,%al
L_004248:
        daa
L_004249:
        .byte 0x88,0x85,0x76,0x00  # forced-kept: gas re-encodes 'mov    %al,0x76(%di)' differently
L_00424d:
        jmp L_000c33

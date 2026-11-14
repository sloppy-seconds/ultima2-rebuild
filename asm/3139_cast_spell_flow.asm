# cast_spell_flow — the CAST gates and the per-spell dispatch. NOT one routine.
# name-evidence: strings 'NEED WAND OR STAFF!' / 'NO SPELL!' / '-FAILED!'; cmd_C jumps into
#           this range at runtime 0x3153.
# BOUNDARY, corrected 2026-10-25: this file's declared start is wrong by design of the bulk
#           wave and the name covers two unrelated things. Runtime 0x3139..~0x3152 is
#           `sub_3339`, a SEGMENT helper — the caller passes a segment in AX, it sets ES to
#           it and works at offset 0x1900 — and its only callers are the dead block in
#           asm/310B_cmd_C.asm (video segment 0xB800, then 0xBA00). The CAST body starts at
#           runtime 0x3153, which is where cmd_C actually lands. Splitting them needs a new
#           row in disasm/function-map.tsv, so it is recorded here rather than done silently;
#           until then do not read 0x3139 as part of the cast flow.
# THE GATES, in byte order (all of this is read off the listing, not inferred):
#           1. print pool[0x24 + DS:63] — the readied spell's NAME, from the SPELLS group of
#              the indexed name pool (asm/4C55_name_pool.asm: NONE, LIGHT, DOWN LADDER,
#              UP LADDER, PASSWALL, SURFACE, PRAYER, MAGIC MISSILE, BLINK, KILL at pool[36..45]).
#           2. DS:D7 + DS:D8 must be non-zero, else 'NEED WAND OR STAFF!'. Neither byte is
#              modelled in engine/src/core/state.h yet.
#           3. DS:63 (the readied slot) zero -> return silently, no message at all.
#           4. DS:B6[DS:63] must be non-zero, else 'NO SPELL!'.
#           5. decrement DS:B6[DS:63] by 1 in packed decimal — spells are CONSUMABLE counts,
#              which is the same array the Z screen lists (docs/status-screen-z.md § 7).
#           6. DS:4A must be >= 4, else '-FAILED!'. DS:4A is the site kind: 0 world map,
#              1 village, 2 town, 3 castle, 4 tower, 5 dungeon. So CASTING ONLY WORKS IN A
#              TOWER OR A DUNGEON. That single byte explains the 2026-10-25 capture, whose
#              seed was standing on the world map.
#           7. dispatch on DS:63: 1 -> sets DS:26 = 0x96 (150), which is the dungeon light
#              step counter, and independently corroborates the owner's '~150 on I'
#              recollection from a completely different direction; 2 and 3 (DOWN/UP LADDER)
#              move DS:25 against a 0xf bound and additionally require DS:4A == 4; 4..9 branch
#              on into the rest of the range.
# LADDER ARMS, re-read from the bytes and WALKED 2026-10-29 (captures/spice86/d12-ladder-ds25).
#           The earlier one-line summary above was loose in three places, all corrected here.
#           1. The `cmp $0xf,%al` bound is tested on a reload of DS:25, not on DS:4A, and the
#              value handed to the validity call is DS:25 +/- 1 — the candidate floor.
#           2. The validity call is asm/1451_site_cell_open_on_floor: it reads the cell at
#              DS:23/DS:24 on the CANDIDATE floor and the callers fail on `jne`, so the move is
#              allowed only when that byte is ZERO (open). That is why UP LADDER printed
#              -FAILED! at the sector-24 tower entry cell (floor 1 there is 0x80, a wall) and
#              succeeded at a cell where floor 1 is open.
#           3. RETRACTED 2026-11-15, and this file's own claim: "Tower and dungeon are SWAPPED,
#              so in a dungeon the two spells do the opposite of what they do in a tower." The
#              branch is real — slot 2 takes the decrement path only when DS:4A == 4, slot 3 the
#              increment path only when DS:4A == 4 — but the conclusion drawn from it was wrong,
#              because it assumed DS:25 counts the same direction in both site kinds. Owner play
#              in a dungeon (captures/spice86/magic-casts-2026-11-15) settles the direction: the
#              player ENTERS on floor 0, DOWN LADDER took DS:25 0 -> 1 -> 2 and UP LADDER took it
#              back, so DS:25 is depth below the entrance and in a dungeon both spells do exactly
#              what they are named. What the cmp on DS:4A is really doing is using the opposite
#              floor orientation in a tower, which is one byte with two orientations, the same
#              hazard already recorded for DS:4A itself. The tower half is still only known to the
#              direction the byte moves (0 -> 1 on UP), never which way is up in a tower.
#           Also corrected: "at DS:25 == 0 the DOWN LADDER arm branches away (L_0034a2)". That
#              branch is the entry to the decrement path (L_00345a), which a dungeon reaches from
#              slot 3 and a tower from slot 2. So in a dungeon it is UP LADDER at floor 0 that
#              branches away, and L_0034a2 is the SURFACE arm — see the 2026-11-15 section below.
#           Also observed: cmd_D (pressing D on a 0x20 down-ladder cell) left the site to the
#           world map and left DS:25 at 0xFF — a decrement with no guard at the bottom.
#           There is no key read, no direction and no cursor anywhere in steps 1-7. CAST aims
#           at nothing; the spells that need a position are dungeon-side.
# CAPTURE-CONFIRMED 2026-10-29 — captures/spice86/cmd-c-cast-tower/ (wizard MAGUS, the tower in
#           sector 20, live frames). Three things the listing allowed but never forced anyone to
#           notice, all of them engine-relevant:
#           (a) **a failed cast still spends the spell.** On the world map `C` printed
#              `CAST-LIGHT-FAILED!` and the LIGHT count went 9 -> 8. Gate 5 (the decrement) runs
#              before gate 6 (`DS:4A >= 4`), so an implementation that checks the location first
#              and spends after — the natural way to write it — is wrong, and only a test that
#              counts stock would catch it.
#           (b) the NAME prints before every gate: with nothing readied, `C` printed `CAST-NONE`
#              rather than a refusal, because pool[36] is NONE and step 1 is unconditional.
#           (c) LIGHT has no darkness precondition and always spends: casting it in a lit corridor
#              consumed another charge and reset DS:26 to 0x96. "Cast LIGHT when it's dark" is a
#              play habit, not a rule.
# ONE SPELL LIST, TWO SHOPPED CATALOGUES (the cleric/wizard difference, 2026-10-29). The ten
#           names in pool[36..45] are the whole spell vocabulary and both classes draw from it; what
#           differs is what each is offered. asm/1869_cleric_shrine sells
#           `1-LIGHT, 2-L.D., 3-L.U., 4-PASS 5-SURFACE, 6-PRAYER` and asm/19D7_magic_shop_menu
#           sells `1-LIGHT, 2-L.D., 3-L.U., 4-M.M., 5-BLINK, 6-KILL`. So three are shared (LIGHT,
#           DOWN LADDER, UP LADDER), three are cleric-only (PASSWALL, SURFACE, PRAYER) and three
#           wizard-only (MAGIC MISSILE, BLINK, KILL). The class gate is the pair of refusals
#           asm/184C (` YOU ARE NOT A CLERIC!`) and asm/19BA (` YOU ARE NOT A WIZARD!`).
#           CLOSED 2026-11-15 without a capture, because the answer was in the bytes the whole
#           time and had been marked INFERRED for want of anyone reading asm/19D7 far enough. The
#           wizard menu remaps its own digits before it touches anything: at L_001c4f it runs
#           `cmp $0x4,%al ; jb L_001c56 ; clc ; adc $0x3,%al`, so choices 1,2,3 stay 1,2,3 and
#           choices 4,5,6 become 7,8,9. The remapped value is what the name print, the price call
#           and the DS:B6 increment all use, so buying "6-KILL" lands in DS:B6[9] and must be
#           readied with M 9. The cleric shrine has no such adjustment: its digits land on 1..6.
#           Both shops add five charges (`adc $0x5,%al ; daa`), which makes the quote readable as
#           `FIVE <NAME>S FOR <price>`.
#           Class values themselves come from pool[72..75] = FIGHTER, CLERIC, WIZARD, THIEF — not
#           "mage"/"paladin", which is corrected in asm/080D_sub_080d.asm and tools/u2_edit.py.
#           Also learned the hard way from this capture: the 3D view does not redraw on a move that
#           did not land, so two frames showed a stale IT'S DARK* after an illegal arrow and an
#           ADVANCE-BLOCKED. A dungeon frame is only evidence after a move that actually moved.
# THIRD-PARTY CROSS-CHECK 2026-10-29 (a class/spell write-up the owner supplied). Graded against
#           these bytes, and the grades are deliberately not merged into the claims above:
#           PROVEN, and more precisely than the claim: BLINK. L_0034d2 calls the rand seam twice,
#           `and $0xf ; or $0x1` each result (random ODD 1..15) into DS:23/DS:24, runs a validity
#           call (L_001651) that can fail into the -FAILED! path at L_003432, and then writes
#           DS:23 -> DS:00 and DS:24 -> DS:01. It overwrites the player position directly.
#           ATTRIBUTED 2026-10-29, and the "not attributed" above is RETRACTED: the
#           creature-HP subtract at L_0034bc is MAGIC MISSILE. Slot 7 dispatches to
#           L_0034ad, which calls the target scan (asm/330C_cast_target_finder.asm) and
#           on a match computes the amount and subtracts it. Live, headless, in
#           captures/spice86/cmd-c-magic-missile:
#             amount: DS:20 = 2*DS:56 + 0x1e + CF, and the `cmc ; sbb` idiom subtracts
#           DS:20 + 1, not DS:20. Measured at two points and both exact: DS:56 = 0x99
#           gives DS:20 = 0x51 and HP 0x99 -> 0x47; DS:56 = 0x01 gives DS:20 = 0x20 and
#           HP 0x99 -> 0x78.
#             "scales with level" is wrong in shape, not just unproven: DS:56 is the HIGH
#           BCD byte of experience (state.h, with its naming wart recorded), there is no
#           level field, and a binary doubling of a BCD digit is not monotone — 0x99
#           yields 82 damage and 0x01 yields 33.
#             survived -> the slot keeps the reduced HP and NOTHING prints. died (HP <=
#           the amount) -> L_0030d1 clears mon_hp/mon_type/mon_kind and the frame reads
#           `KILLED--GOLD+3--EXP.+00`, the gold awarded in BCD and wrapping: a 9999-gold
#           character went to 0002 on +3. EXP.+00 because the synthetic slot used for the
#           test carried no kind table entry.
#           THE AWARD IS PER-CREATURE, not a constant — corrected 2026-10-29 by
#           captures/spice86/mm-owner-play, the first natural owner-play kill: same spell,
#           same tower floor, and the frame reads `KILLED--GOLD+13--EXP.+03` with GOLD
#           0004 -> 0017 and EXP 0000 -> 0003 to match. So both numbers come from the victim's
#           own entry. How they are selected is still NOT decoded.
#             The `jb` after the second `cmc` is taken when the creature SURVIVED, the
#           opposite of how the idiom reads at a glance; invert it and the death path
#           looks like the survival path.
#           KILL (slot 9) AND BLINK (slot 8) OBSERVED 2026-10-29 —
#           captures/spice86/g46-kill-blink, same tower, same rig.
#           KILL: L_003501 calls the same target scan and, on a match, jumps straight to the
#           death path with NO damage step. Proven by the discriminator: a creature held at
#           full HP 0x99 — which MAGIC MISSILE leaves alive at 0x47 — had its slot erased
#           outright (mon_hp 0, mon_type 0). The frame shows the award landing on the
#           character: GOLD 9999 -> 0006 and EXP 9999 -> 0001, both wrapped BCD, so KILL
#           does pay out and the amount comes from the creature's own entry.
#           BLINK: both exits observed. The random odd cell is rejected as often as not
#           (`CAST-BLINK-FAILED!` on attempt 1), and on attempt 2 the position went
#           (5,5) -> (7,15) — both odd, inside 1..15, exactly the `and $0xf ; or $0x1`
#           shape. So "BLINK can fail" is confirmed and the gate is the same asm/1451
#           site-cell validity call the ladder spells use, not a separate rule.
#           SUPERSEDED 2026-11-15: this block read "Still unobserved in this file: PASSWALL (4),
#           SURFACE (5) and PRAYER (6)" and "NOT FOUND: a 50%-shaped test for PRAYER, a grid write
#           for PASSWALL, an exit-to-surface path for SURFACE". All three arms are now filmed, and
#           the 50% test was there all along — it is the `js` after `call rand_byte`, whose flag
#           comes from the tail of the generator in asm/5236, not from rand_byte's own body. See
#           the 2026-11-15 section below.
# 2026-11-15, ALL NINE SPELLS FILMED — captures/spice86/magic-casts-2026-11-15, owner play in a
#           browser (noVNC) with host-side memory reads, maxout WIZARD `MAGUS` seeded with all
#           nine slots at 99, dungeon sector 25 entered from sector 20 cell (25,5).
#           PASSWALL (slot 4) clears the cell in front. Facing west at a rock cell, live grid read
#           before and after one cast: (4,5) went 0x80 -> 0x00, the charge was spent, and the band
#           read `CAST-PASSWALL` with no -FAILED!. The third-party "remove the wall immediately in
#           front of you" is the rule. Unresolved detail: one owner cast left the opened cell
#           reading 0x01 rather than 0x00, so whether the arm clears the rock bit or zeroes the
#           byte, and whether a stamp layer is re-applied afterwards, is not settled.
#           PRAYER (slot 6) is KILL behind a coin: `call rand_byte ; js L_003432 ; jmp L_003501`,
#           so a clear sign bit runs the same target scan and the same death path as slot 9. Two
#           successes filmed, after four and one failures. The flip is fair — measured at 50.0% of
#           200000 draws simulated from the live generator state — so the third-party "50% chance
#           to destroy the enemy in front of you" is right and the earlier NOT FOUND was wrong.
#           SURFACE (slot 5) ignores depth: cast from floor 1 it returned the player to (25,5),
#           the world cell entered by, not a cell derived from the cast position. DS:25 is left at
#           its dungeon value on exit, which is harmless because entering sets it to 0.
#           UP LADDER at floor 0 IS SURFACE. The slot-3 arm opens `or %al,%al ; je L_0034a2` and
#           L_0034a2 is the slot-5 target, so at the entrance floor "up" falls into the exit. Cast
#           there it printed bare `CAST-UP LADDER`, spent the charge, and put the player on the
#           world map at (25,5).
#           The ladder gate the third-party table states as "if the square behind your destination
#           is open" is literal. asm/1451 tests DS:23/DS:24 on the candidate floor for exactly
#           zero, and DS:23/DS:24 is player minus facing, measured three ways in one session:
#           facing north (5,6), west (6,5), east (4,5). Facing west the candidate cell on floor 1
#           was 0x80 and the cast failed; turned east it was 0x00 and the cast moved the floor.
#           KILL (slot 9) refused twice in a row on nothing: two casts, two kills, no -FAILED!,
#           which is the no-dice difference from PRAYER.
#           THE AWARD IS FROM THE VICTIM, now proven rather than inferred. Five kills through the
#           one shared death path paid +13/+03, +11/+05, +17/+02, +13/+07, +17/+04. Two different
#           spells converge on L_0030d1, so identical code produced five different EXP values and
#           the only variable was the creature. Which field of the victim's record supplies the
#           pair is still not decoded.
#           A timing trap for anyone repeating this: a DS read taken immediately after the key send
#           returns can show a charge unspent, because the handler finishes after the send returns.
#           Twice in this session a spent spell read as unspent until re-read a beat later.
#           CORROBORATED ELSEWHERE: the class bonuses are exact (CLERIC +10 WIS at file 0x5AD5,
#           WIZARD +10 INT at 0x5B04, FIGHTER +15 STR, THIEF +10 AGI, and STAMINA gets no bonus
#           from sex, race or class at all) — see engine/src/game/creation.c. And shop price is
#           COMPUTED, not tabulated: asm/20AB_bcd_price_from_stats derives it from the caller's
#           index and the bit length of [DS:50] + [DS:4E], so a fixed per-spell cost like "576 gold
#           for KILL" cannot exist, and a high-INT buyer is quoted less for the same spell. Both
#           spell shops call the same function, so expense tracks index and stats, not class.
# func 3139..330B  len 01D3 B  evidence: call+reko
# strings: "-FAILED!"; "-FAILED!"; "NEED WAND OR STAFF!"; "NO SPELL!"
# calls: L_000c33, L_001651, L_0030d1, L_003432, L_0034a2, L_0034a5, L_0034ad, L_0034d2 (+9 more)
# provenance: static, re-read end to end 2026-10-25
#           the line here used to read "entry never appears in ExecutedInstructions of any of
#           the 60 harvests" — that is exactly the hand-kept claim cmd_handlers.py got wrong
#           eight times, and it has not been re-derived against the 141-harvest corpus. Do not
#           cite it. cmd_C itself is confirmed executed in 2 harvests.
# confidence: high for the gate order and the constants (all direct reads) and high for
#           slot 7 (dispatch, target scan, amount and both exits were observed live
#           2026-10-29); low for what each per-spell branch beyond 1..3 and 7 does,
#           which is site behaviour and parked (AGENTS.md § 3)
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
cast_spell_flow:
L_003339:
        nop
L_00333a:
        push   %bx
L_00333b:
        push   %es
L_00333c:
        mov    %ax,%es
L_00333e:
        mov    $0x0,%ax
L_003341:
        mov    $0x1900,%bx
L_003344:
        mov    %ax,%es:(%bx)
L_003347:
        add    $0x2,%bx
L_00334a:
        cmp    $0x1f40,%bx
L_00334e:
        jne L_003344
L_003350:
        pop    %es
L_003351:
        pop    %bx
L_003352:
        ret
L_003353:
        nop
L_003354:
        mov    0x63,%al
L_003357:
        clc
L_003358:
        adc    $0x24,%al
L_00335a:
        call L_00528c
L_00335d:
        mov    0xd7,%al
L_003360:
        clc
L_003361:
        adc    0xd8,%al
L_003365:
        jne L_003385
L_003367:
        call L_0051df
        db 0x8d,"NEED WAND OR STAFF!",0x00        # string
L_00337f:
        call L_006083
L_003382:
        jmp L_000c33
L_003385:
        nop
L_003386:
        mov    0x63,%al
L_003389:
        .byte 0x0a,0xc0  # forced-kept: gas re-encodes 'or     %al,%al' differently
L_00338b:
        jne L_003390
L_00338d:
        jmp L_000c33
L_003390:
        mov    $0x0,%ah
L_003392:
        .byte 0x8b,0xf8  # forced-kept: gas re-encodes 'mov    %ax,%di' differently
L_003394:
        mov    0xb6(%di),%al
L_003398:
        .byte 0x0a,0xc0  # forced-kept: gas re-encodes 'or     %al,%al' differently
L_00339a:
        jne L_0033b0
L_00339c:
        call L_0051df
        db 0x8d,"NO SPELL!",0x00        # string
L_0033aa:
        call L_006083
L_0033ad:
        jmp L_000c33
L_0033b0:
        call L_0061c3
L_0033b3:
        mov    $0x0,%bh
L_0033b5:
        mov    0x63,%bl
L_0033b9:
        .byte 0x8b,0xfb  # forced-kept: gas re-encodes 'mov    %bx,%di' differently
L_0033bb:
        stc
L_0033bc:
        mov    0xb6(%di),%al
L_0033c0:
        cmc
L_0033c1:
        sbb    $0x1,%al
L_0033c3:
        das
L_0033c4:
        cmc
L_0033c5:
        mov    %al,0xb6(%di)
L_0033c9:
        mov    0x4a,%al
L_0033cc:
        cmp    $0x4,%al
L_0033ce:
        jae L_0033e2
L_0033d0:
        call L_0051df
        db "-FAILED!",0x00        # string
L_0033dc:
        call L_006083
L_0033df:
        jmp L_000c33
L_0033e2:
        nop
L_0033e3:
        mov    0x63,%al
L_0033e6:
        .byte 0x0a,0xc0  # forced-kept: gas re-encodes 'or     %al,%al' differently
L_0033e8:
        jne L_0033ed
L_0033ea:
        jmp L_000c33
L_0033ed:
        cmp    $0x1,%al
L_0033ef:
        je L_00341c
L_0033f1:
        cmp    $0x2,%al
L_0033f3:
        je L_003424
L_0033f5:
        cmp    $0x3,%al
L_0033f7:
        je L_003453
L_0033f9:
        cmp    $0x4,%al
L_0033fb:
        je L_003472
L_0033fd:
        cmp    $0x5,%al
L_0033ff:
        je L_003410
L_003401:
        cmp    $0x6,%al
L_003403:
        je L_003413
L_003405:
        cmp    $0x7,%al
L_003407:
        je L_003416
L_003409:
        cmp    $0x8,%al
L_00340b:
        jne L_003419
L_00340d:
        jmp L_0034d2
L_003410:
        jmp L_0034a2
L_003413:
        jmp L_0034a5
L_003416:
        jmp L_0034ad
L_003419:
        jmp L_003501
L_00341c:
        mov    $0x96,%al
L_00341e:
        mov    %al,0x26
L_003421:
        jmp L_000c33
L_003424:
        mov    0x4a,%al
L_003427:
        cmp    $0x4,%al
L_003429:
        je L_00345a
L_00342b:
        mov    0x25,%al
L_00342e:
        cmp    $0xf,%al
L_003430:
        jb L_003444
L_003432:
        call L_0051df
        db "-FAILED!",0x00        # string
L_00343e:
        call L_006083
L_003441:
        jmp L_000c33
L_003444:
        clc
L_003445:
        adc    $0x1,%al
L_003447:
        call L_001651
L_00344a:
        jne L_003432
L_00344c:
        incb   0x25
L_003450:
        jmp L_003a2a
L_003453:
        mov    0x4a,%al
L_003456:
        cmp    $0x4,%al
L_003458:
        je L_00342b
L_00345a:
        mov    0x25,%al
L_00345d:
        .byte 0x0a,0xc0  # forced-kept: gas re-encodes 'or     %al,%al' differently
L_00345f:
        je L_0034a2
L_003461:
        stc
L_003462:
        cmc
L_003463:
        sbb    $0x1,%al
L_003465:
        cmc
L_003466:
        call L_001651
L_003469:
        jne L_003432
L_00346b:
        decb   0x25
L_00346f:
        jmp L_003a2a
L_003472:
        clc
L_003473:
        mov    0x0,%al
L_003476:
        adc    0x2,%al
L_00347a:
        and    $0xf,%al
L_00347c:
        je L_003432
L_00347e:
        mov    %al,0x23
L_003481:
        mov    0x1,%al
L_003484:
        adc    0x3,%al
L_003488:
        and    $0xf,%al
L_00348a:
        je L_003432
L_00348c:
        mov    %al,0x24
L_00348f:
        mov    0x25,%al
L_003492:
        call L_001651
L_003495:
        jns L_003432
L_003497:
        mov    $0x0,%al
L_003499:
        mov    0x8,%bx
L_00349d:
        mov    %al,(%bx,%si)
L_00349f:
        jmp L_000c33
L_0034a2:
        jmp L_003a44
L_0034a5:
        call L_005417
L_0034a8:
        js L_003432
L_0034aa:
        jmp L_003501
L_0034ac:
        nop
L_0034ad:
        call L_00350c
L_0034b0:
        je L_003432
L_0034b2:
        mov    0x56,%al
L_0034b5:
        .byte 0x02,0xc0  # forced-kept: gas re-encodes 'add    %al,%al' differently
L_0034b7:
        adc    $0x1e,%al
L_0034b9:
        mov    %al,0x20
L_0034bc:
        mov    0x177(%di),%al
L_0034c0:
        cmc
L_0034c1:
        sbb    0x20,%al
L_0034c5:
        cmc
L_0034c6:
        mov    %al,0x177(%di)
L_0034ca:
        jb L_0034cf
L_0034cc:
        jmp L_0030d1
L_0034cf:
        jmp L_000c33
L_0034d2:
        call L_005417
L_0034d5:
        and    $0xf,%al
L_0034d7:
        or     $0x1,%al
L_0034d9:
        mov    %al,0x23
L_0034dc:
        call L_005417
L_0034df:
        and    $0xf,%al
L_0034e1:
        or     $0x1,%al
L_0034e3:
        mov    %al,0x24
L_0034e6:
        mov    0x25,%al
L_0034e9:
        call L_001651
L_0034ec:
        je L_0034f1
L_0034ee:
        jmp L_003432
L_0034f1:
        nop
L_0034f2:
        mov    0x23,%al
L_0034f5:
        mov    %al,0x0
L_0034f8:
        mov    0x24,%al
L_0034fb:
        mov    %al,0x1
L_0034fe:
        jmp L_000c33
L_003501:
        call L_00350c
L_003504:
        jne L_003509
L_003506:
        jmp L_003432
L_003509:
        jmp L_0030d1

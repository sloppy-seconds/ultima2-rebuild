# bcd_price_from_stats — derives a BCD amount into DS:27/28 from int + charisma
# name-evidence: seeds the price pair DS:27/28 AND a second pair DS:29/2A to 0004,
#           derives a loop count from the bit length of [DS:50]+[DS:4E] (int + the
#           byte at DS:4E -- PROVEN 2026-10-29 to be CHARISMA, see the resolution below),
#           then runs a
#           swap-and-add BCD sequence DI times, leaving the result in DS:27/28 -- the
#           same operand pair asm/17B7 hands to the 4-digit BCD printer and asm/2052
#           charges. Three inbound callers: 17B2 (the tavern food quote),
#           1869_cleric_shrine, 19D7_magic_shop_menu. All three are places a price is
#           quoted.
# func 20AB..2121  len 0077 B  evidence: seed+call+reko
# provenance: static, 2026-10-23. First live prices observed 2026-10-29 (see LIVE PRICES below)
#           -- a horse at 32 gp reproduces exactly; an axe at 25 gp cannot be produced here at
#           all, which is new information about what this routine does NOT price.
# confidence: high on the instruction-level mechanics (bytes + a step-by-step
#           simulation); LOW on whether this is what produced the observed 0200.
#
# MERGED 2026-10-23 from the fragments 20AB, 20CF, 20D7, 20EB and 2121, per
# docs/g22-split-proposal.md § 2 with the owner's approval. 20AB is the only inbound
# call target in the range and 2121 is the terminating `ret`. Body lines unchanged and
# in address order; engine/tests/asm_body_guard.py proves it.
#
# HOW IT WORKS (observed, and simulated step by step)
#   20AB  clc; adc $0x8,%al ; mov %al,[DS:20]     DS:20 = caller_AL + 8 + CF
#         [DS:27],[DS:29] = 0 ; [DS:28],[DS:2A] = 4      two BCD pairs, both 0004
#         mov al,[DS:50] ; adc al,[DS:4E]         AL = int + cha + CF
#   20CF  inc %si ; clc ; rcr $1,%al ; or %al,%al ; jne
#         SI = number of right shifts to reach zero = the BIT LENGTH of int+cha
#   20D7  mov al,[DS:20]     (reads the OLD DS:20, before the next line overwrites it)
#         mov %bl,[DS:20]    DS:20 = bit length
#         stc; cmc; sbb [DS:20],%al; cmc          AL = (caller_AL+8+CF) - bitlength
#         mov $0,%ah ; mov %ax,%di                DI = loop count
#   20EB  loop, DI times:
#           al = [DS:2A] + [DS:28], das
#           [DS:2A] = old [DS:28] ; [DS:28] = al
#           al = [DS:29] + [DS:27] + CF, das
#           [DS:29] = old [DS:27] ; [DS:27] = al
#         dec %di ; jne
#   2121  ret
#
#   The loop is NOT multiply-by-repeated-addition, which is what a first reading
#   called it. Each pair takes the other pair's old value while the first receives
#   the sum, so the two pairs walk a Fibonacci sequence seeded 4,4:
#       DS:27/28 after DI iterations = 4, 8, 12, 20, 32, 52, 84, 136, 220, 356, 576, 932 ...
#   Simulated for the tavern call (caller_AL=3) on a maxed character (int=cha=0x40,
#   BCD 0x40+0x40=0x80, bit length 8): DI = 11-8 = 3 -> DS:27/28 = 0020.
#   A character with int=cha=0 gets DI = 10 -> 0576. **Higher int+cha means a LOWER
#   number.** The owner confirms on 2026-10-23 that charisma affecting costs is known
#   game behaviour, which is the strongest support this reading has; it is corroboration
#   of direction, not proof of the arithmetic.
#
# LIVE PRICES, 2026-10-29 — the first numbers the game actually charged, and they split the
#           routine's remit in two. Owner-played, character BUYTEST (class 2 WIZARD, DS:50=0x40,
#           DS:4E=0x40), gold read from DS:58/59 before and after each purchase, frames in
#           captures/spice86/shop-window-transport/:
#             TRANSPORT shoppe, horse:  9999 -> 9967  = **32 gp**
#             WEAPONS shoppe, axe:      9967 -> 9942  = **25 gp**
#           The loop's reachable values, re-simulated from the bytes with DAA's CF taken only from
#           the >0x99 adjustment (an earlier attempt let the auxiliary nibble fixup feed the second
#           adc and produced 4, 8, 112, 220 -- plausible-looking and wrong from the third term).
#           The corrected model reproduces this file's own hand-computed anchor, AL=3 on a maxed
#           character -> DI=3 -> 0020, which is the control that makes the rest of this credible:
#             DI:      0   1   2   3   4   5   6    7    8    9    10   11
#             value:   4   8  12  20  32  52  84  136  220  356  576  932
#           On int=cha=0x40 the bit length is 8, so DI = caller_AL + 8 - 8 = caller_AL.
#           **32 is in the sequence, at AL=4.** The horse price is exactly what this routine
#           produces, which is the first time this file has reproduced a number the game charged.
#           **25 is not in the sequence at any AL and at no stat pair tested** (swept AL 0..59 at
#           maxed stats, and AL 0..19 across stat pairs 0x10/0x20/0x40/0x60/0x99 on both bytes).
#           So the weapons shoppe is NOT priced by this routine. That is a real narrowing: the
#           three known callers (17B2 tavern, 1869 cleric shrine, 19D7 magic shoppe) are not
#           "all shops", and asm/19D7 in particular carries the transport AND weapons menus in one
#           file, so the two prices observed today come from the same file and only one of them
#           routes through here. Whoever reads 19D7's weapon branch should expect a separate price
#           path, probably a table.
#           BOTH OF THOSE OBSERVATIONS ARE NOW RETRACTED, and the routine survives both. They
#           were re-measured on 2026-10-29 by stopping the CPU inside the game during real
#           headless purchases instead of reading gold after the fact:
#             GROCERY: asm/17B7 was stopped mid-quote and left DS:27..2A = 00 20 00 12, and the
#               frame reads `THE FOOD HERE COSTS 0020 / PER 100, WANT ONE?` — not 0200. The
#               number this file could not produce was a transposition. AL=3 on a maxed
#               character gives DI=3, and the walk's third term is 20. captures/spice86/
#               g23-food-live.
#             AXE: the weapons shoppe DOES route through this routine — asm/1F03 is
#               `mov [DS:1F],%al ; call L_0022ab`, the menu digit straight in. Stopped at
#               asm/20AB it entered with AL=0x03 for menu item 3 and left DS:27/28 = 00 20; the
#               frame reads `FOR YOU ONLY 0020` and gold fell 9999 -> 9979. The axe was never
#               25. captures/spice86/g23-weapons.
#           The per-shop index, now read at all six call sites: grocery 3 (constant, asm/17B2),
#           transport 4 (constant, asm/1FAD), armour 2*digit (asm/1DD8 does `add %al,%al`),
#           weapons digit (asm/1F00), cleric shrine digit (asm/1931), wizard shoppe digit
#           (asm/1A78). There is ONE pricing path for the shops, plus two shops that bypass it
#           entirely with a literal: the tavern drink (0005, asm/15E1) and the clue seller
#           (digit in the hundreds byte, asm/1B42). "Probably a table" was the wrong shape of
#           answer; the index is a menu position.
#           One arithmetic slip in this header, corrected here: the third-party grocery check
#           worked `AL=4` at minimum stats and claimed DI=6 -> 0052. The table below puts 52 at
#           DI=5, and the grocery's AL is the constant 3, which at 0x10/0x10 gives bit length 6
#           and DI = 3+8-6 = 5 -> 0052. The wiki's number agrees once the caller is the right one.
#           ARMOUR REPRODUCED AT THREE POINTS, 2026-11-13 (captures/spice86/g68-cha-price-curve):
#           sector 32 armour shoppe, digit 1 (CLOTH, AL=2), Int held 0x99, only Cha varied, price
#           read from DS:27/28 and confirmed on the frame: Cha=0x99 -> 0032 (al 0x32, bl 6, DI 4);
#           Cha=0x10 -> 0012 (al 0xA9, bl 8, DI 2); Cha=0x67 -> 0356 (0x99+0x67=0x100 truncates to
#           al 0x00, bl 1, DI 9). All three match this routine exactly, including the 8-bit wrap
#           where the carry out of int+cha is discarded by the rcr loop and the price jumps UP. The
#           curve is a step function of bitlength(int+cha), NOT the third-party P1 "fall near 40/86,
#           skyrocket past 160" smooth discount -- that claim's direction is right, its shape is wrong.
#           Two things this does NOT settle. ~~The DS:4E charisma-vs-wisdom dispute~~ is
#           CLOSED 2026-10-29 (see the resolution below), so the only thing still open here is
#           the tavern's observed '0200', still unreproduced and still not in the sequence; G23
#           carries it. The DOUBT below stands in full except that "do not describe this routine as
#           the price formula until it reproduces a number the game actually printed" is now
#           satisfied for one number out of two.
#           Third-party cross-check, same day: the wiki's 52 gp grocery maximum and the claimed
#           576 gp for a batch of KILL are BOTH in the sequence (DI=5 and DI=10). Two independent
#           third-party numbers landing on a sequence derived from bytes neither source saw is the
#           strongest support this decode has. Still corroboration, not proof.
#
# DOUBT -- read this before citing the routine, and note two retractions inside it
#   * THIRD-PARTY CORROBORATION, 2026-10-28e, and it is the strongest support this file has.
#     The owner supplied grocery data from the Ultima II wiki: every grocery sells
#     "Provisions (100)" for **52 gp**, that 52 is the MAXIMUM price (charged when Charisma and
#     Intelligence are both at their minimum of 10), and that **the higher both stats, the lower
#     the price**. Two independent things about that match this routine, and neither was known
#     when the routine was decoded:
#       - the DIRECTION agrees. Higher int+cha shortens the shift count, which lengthens DI,
#         which walks the Fibonacci pair further -- wait, the opposite: a larger sum has a
#         LONGER bit length, so DI = (AL+8+CF) - bitlength gets SMALLER and the price gets
#         LOWER. The wiki says exactly that, from a source that never saw these bytes.
#       - the NUMBER agrees. The values this loop can produce are
#             4, 8, 12, 20, 32, 52, 84, 136, 220, 356, 576, 932, ...
#         and **52 is the 6th of them**. Worked at the wiki's minimum stats (int=cha=0x10, so
#         adc gives 0x20, bit length 6): DI = (AL+8+CF) - 6, and AL=4 yields DI=6 -> DS:27/28
#         = 0052. A caller with AL=4 on a minimum-stat character prints 52.
#     This does NOT close the DOUBT above: the tavern observation of '0200' is still
#     unreproduced, and 200 is still not in the sequence. What it does do is make "this routine
#     computes a quoted price" considerably harder to dismiss, and it suggests the tavern quote
#     and the grocery price may not consume the same call at all -- different callers pass
#     different AL (17B2 passes 3), so comparing the tavern's printed number against the
#     grocery's expected 52 may have been a category error.
#     Grade: corroboration of direction and of one reachable value, from a third-party source.
#     Still not bytes. The DS:4E charisma-vs-wisdom dispute is untouched by it -- the wiki names
#     charisma, which is a second reason to hope the label is right, and still not proof.
#   * WHICH STAT IS [DS:4E] — CLOSED 2026-10-29, and it is CHARISMA. Two independent
#     observations from `captures/spice86/d13-cha-vs-wis`, on a Wizard seeded with
#     `DS:4E = 0x20`, `DS:4F = 0x40`, `DS:50 = 0x40` (the first character ever with the two
#     bytes unequal, which is the only shape that can discriminate them):
#       - the `Z` status screen printed `CHARISMA-20` and `WISDOM-40`, so the byte the game
#         labels CHARISMA is `DS:4E`. `engine/src/core/state.h` was right;
#         `docs/spice86-live-findings.md` § 11 had them swapped and is corrected.
#       - the same character bought a horse at the TRANSPORT shoppe for **52 gp**, where the
#         maxed character one day earlier paid 32 gp for the same horse. That is exactly what
#         this routine predicts and it discriminates the two bytes: reading `int + [DS:4E]` =
#         `0x40 + 0x20 = 0x60`, bit length 7, `DI = 4 + 8 - 7 = 5` -> **52**; reading
#         `[DS:4F]` would give bit length 8, `DI = 4` -> 32, which is not what was charged.
#         Owner played; gold read live as `DS:58/59 = 99 47`.
#     So "int + cha" in this file is now a statement about bytes and not a label on hope, and
#     the file name is earned. The original note is kept below for the record.
#   * (superseded) WHICH STAT IS [DS:4E] IS NOT SETTLED, and this file's name leans on it.
#     tools/u2_edit.py records the dispute: engine/src/core/state.h labels DS:4E
#     charisma and DS:4F wisdom, docs/spice86-live-findings.md lists them the other way
#     round, and it cannot be resolved from a character where both bytes are equal --
#     which is every maxout character ever seeded here, including the one used for this
#     analysis. So "int + cha" here means "int + [DS:4E]", and if the labels are swapped
#     the mechanic is int + WISDOM. The owner's recollection is that CHARISMA moves
#     prices; that is a reason to hope this is charisma, not proof of it. Do not rename
#     this file on the strength of the recollection alone. A capture that sets DS:4E and
#     DS:4F to DIFFERENT values and watches the price settles it.
#   * RETRACTED (same day, twice): this routine was first described as deriving its
#     count from `popcount(int + cha)` and as a "multiply by repeated addition".
#     Both were wrong. The shift loop counts shifts, not set bits, and the loop is a
#     Fibonacci walk, not a multiply. The first simulation of it also had DAA
#     subtracting 6 instead of adding, which produced plausible nonsense (01A6 where
#     the answer is 0012) -- a good illustration of why a model must be checked
#     against a hand-computed case before it is quoted.
#   * THE MODEL DOES NOT REPRODUCE THE OBSERVED PRICE, and the gap is now known to be
#     real rather than a misreading of the print. asm/17B7 was decoded on 2026-10-23 --
#     it turned out to be TWO more fragment-splits of exactly the G22 kind (51A5/51A9/
#     51AE and 53B5/53C5 are one 4-digit BCD printer each, cut on branch targets). It
#     emits DS:27 as two decimal digits, then DS:28 as two. So the observed '0200' means
#     DS:27=0x02, DS:28=0x00, and this routine called from asm/17B2 with AL=3 on a maxed
#     character leaves DS:27/28 = 0020.
#     Worse than transposed: the values this loop can ever produce are
#         4, 8, 12, 20, 32, 52, 84, 136, 220, 356, 576, 932, ...
#     and **200 is not among them at any loop count**. On the current model this routine
#     cannot output the number the game printed. Either the food quote does not consume
#     this routine's result, or the loop is still misread, or the printed string was not
#     0200. Unresolved; G23 carries it. Do not describe this routine as "the price
#     formula" until it reproduces a number the game actually printed.
#   * Which of DS:27 / DS:28 is the high byte is now PROVEN by the printer rather than
#     inferred from asm/2052: asm/17B7 loads AL=DS:27 and DI=DS:28 and the printer emits
#     AL's two digits first. It agrees with the asm/2052 pairing. The residual doubt in
#     the bullet above is about the loop, not about the byte order.
#   * If DI computes to 0 the loop's `dec %di ; jne` wraps to 0xFFFF and runs 65536
#     times. Reachable in principle (caller_AL+8+CF == bitlength) and never observed.
#     Untested; do not assume the original guarded it.
#   * int and cha are read as raw BCD bytes and added with `adc`, not with `daa`, so
#     the sum is a binary add of two packed-decimal bytes. For 0x40+0x40 that is 0x80,
#     which happens to be a valid BCD byte; for 0x99+0x99 it is 0x32 with carry, which
#     is not. Whether that is a deliberate quirk or a sloppy add is unknown, and it
#     changes the curve for high stats.
#   * Six instructions in here are `forced-kept` byte sequences -- the vendored
#     assembler re-encodes those movs differently, so the listing stores the original
#     machine words and the mnemonics are our reading. check_func.py proves the bytes
#     round-trip, not the disassembly.
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
sub_20ab:
L_0022ab:
        nop
L_0022ac:
        clc
L_0022ad:
        adc    $0x8,%al
L_0022af:
        mov    %al,0x20
L_0022b2:
        mov    $0x0,%al
L_0022b4:
        mov    %al,0x27
L_0022b7:
        mov    %al,0x29
L_0022ba:
        mov    $0x4,%al
L_0022bc:
        mov    %al,0x28
L_0022bf:
        mov    %al,0x2a
L_0022c2:
        mov    0x50,%al
L_0022c5:
        adc    0x4e,%al
L_0022c9:
        mov    $0x0,%bh
L_0022cb:
        mov    $0x0,%bl
L_0022cd:
        .byte 0x8b,0xf3  # forced-kept: gas re-encodes 'mov    %bx,%si' differently
sub_20cf:
L_0022cf:
        inc    %si
L_0022d0:
        clc
L_0022d1:
        rcr    $1,%al
L_0022d3:
        .byte 0x0a,0xc0  # forced-kept: gas re-encodes 'or     %al,%al' differently
L_0022d5:
        jne L_0022cf
sub_20d7:
L_0022d7:
        mov    0x20,%al
L_0022da:
        .byte 0x8b,0xde  # forced-kept: gas re-encodes 'mov    %si,%bx' differently
L_0022dc:
        mov    %bl,0x20
L_0022e0:
        stc
L_0022e1:
        cmc
L_0022e2:
        sbb    0x20,%al
L_0022e6:
        cmc
L_0022e7:
        mov    $0x0,%ah
L_0022e9:
        .byte 0x8b,0xf8  # forced-kept: gas re-encodes 'mov    %ax,%di' differently
sub_20eb:
L_0022eb:
        clc
L_0022ec:
        mov    0x2a,%al
L_0022ef:
        adc    0x28,%al
L_0022f3:
        daa
L_0022f4:
        mov    $0x0,%bh
L_0022f6:
        mov    0x28,%bl
L_0022fa:
        .byte 0x8b,0xf3  # forced-kept: gas re-encodes 'mov    %bx,%si' differently
L_0022fc:
        .byte 0x8b,0xde  # forced-kept: gas re-encodes 'mov    %si,%bx' differently
L_0022fe:
        mov    %bl,0x2a
L_002302:
        mov    %al,0x28
L_002305:
        mov    0x29,%al
L_002308:
        adc    0x27,%al
L_00230c:
        daa
L_00230d:
        mov    $0x0,%bh
L_00230f:
        mov    0x27,%bl
L_002313:
        .byte 0x8b,0xf3  # forced-kept: gas re-encodes 'mov    %bx,%si' differently
L_002315:
        .byte 0x8b,0xde  # forced-kept: gas re-encodes 'mov    %si,%bx' differently
L_002317:
        mov    %bl,0x29
L_00231b:
        mov    %al,0x27
L_00231e:
        dec    %di
L_00231f:
        jne L_0022eb
sub_2121:
L_002321:
        ret

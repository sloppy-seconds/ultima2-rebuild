# creature_step_gate — may this creature step onto the cell staged in DS:23/DS:24?
# name-evidence: entry is `mov %al,0x1f` one instruction after `call L_0052b3`
#           (asm/50B3_map_tile_ptr), so AL on entry is the sector-grid tile byte of the
#           staged cell. Two returns: L_0023f7 `mov $0x0,%al; cmp $0x0,%al; ret` and
#           L_0023fc `mov $0xff,%al; cmp $0x0,%al; ret`. The trailing cmp is the contract:
#           the callers at asm/2150, asm/2169 and asm/2189 branch on ZF, so
#           **AL = 0 accept / AL = 0xFF refuse**, and ZF is the flag the callers read.
# WHICH GRID IT READS, settled 2026-11-08 (step 2a of docs/demo-record-playback.md § 13.10):
#           **the stamped grid, and there is only one.** The tile reaches this routine in AL from
#           `call L_0052b3`, which resolves the cell through `word[DS:472]`; that word is `0x1800`
#           in every live dump (`asm/68C0`'s provenance) and is written only by the sector read
#           `asm/2290` and the sector store `asm/2305`. There is no second, unstamped buffer for a
#           step-gate probe to read, so the accept set {0x08, 0x0C, 0x5C, 0x70, 0x00} is evaluated
#           over a grid that already carries creature type bytes. **The stamp is therefore the
#           original's occupancy rule, by construction and not by a separate test** — which is what
#           `engine/src/game/ai.c` claims to reproduce, and does: `creature_may_enter` reads
#           `u2_world_tile_at()`, which is `w->map`, the stamped grid. Confidence unchanged: high.
# REAL FUNCTION SPAN: runtime 2199..2200 (file 2399..2400). The `# func` line below
#           declares only this fragment's 10 bytes; the body continues through
#           asm/21A3 .. asm/21FC, all of which are parts of this one predicate.
#
# THE RULE, complete (decoded 2026-10-28; the full table with grades is
#           docs/contract-creature-tick.md § "The step gate"):
#             if DS:26A != 0            -> refuse, unconditionally (asm/21A3)
#             if kind in {0x2C, 0x48}   -> accept iff tile == 0x00 (asm/21A6, 21AE, 21D8)
#             else:
#                 if kind < 0x78 and tile == 0x5C -> accept   (asm/21B2, 21B6)
#                 if tile in {0x10, 0x04, 0x00}   -> refuse   (asm/21BD, 21C5, 21C9)
#                 if tile < 0x14                  -> accept   (asm/21CD, jb: 0x08, 0x0C)
#                 if tile == 0x70                 -> accept   (asm/21D1)
#                 otherwise                       -> refuse   (asm/21D5)
#             accept then passes a site guard: if DS:4A != 0, candidate x == 0 or
#           candidate y == 0 is refused (asm/21E2, 21E9, 21F0). In the wilderness
#           (DS:4A == 0) the 64x64 grid wraps and the edge row/column is walkable.
#         Reads DS:1F (the tile, stored by this routine's first instruction),
#         DS:197[DI] (mon_type), DS:26A, DS:4A, DS:23, DS:24. Writes DS:1F only.
#         Of the 64 tile bytes that ever occur in a sector map (all multiples of 4,
#           docs/passability-findings.md F12) exactly five are ever accepted: 0x08, 0x0C,
#           0x5C, 0x70 and — for the two water kinds only — 0x00.
# 0x5C IS THE FORCE FIELD, settled live 2026-10-28 (captures/spice86/g29-field). It is art
#           0x17 (checkerboard rows), 72 cells in mapx03 and 4 in mapx71, and it is the only
#           clause that splits on kind. Standing on it with no ring costs exactly 1000 HP,
#           observed six times; with the ring (DS:D6 != 0) it costs nothing; a plain 0x70 step
#           costs nothing. So the gate's reading is: monsters cross force fields, the four NPC
#           classes (Fighter/Cleric/Wizard/Thief, mon_type >= 0x78) may not. Same commit
#           corrects docs/phase4-state.md § CP5, which had ruled a tile trigger out on a units
#           error — it searched mapx00 for the MODE byte 0x2e, which is map_byte >> 1, and map
#           bytes are multiples of 4.
# apple2-twin: $6672 in ULTIMA.II.OBJ (the accept return is $66B2, the refuse return $66C2,
#           and the site guard is $66B2..$66C1). Hand-decoded from raw bytes, and it is a
#           clause-for-clause match, not a resemblance: `STA $F0` for the tile, then the
#           freeze counter $D9 (this file's DS:26A) refuses first; then `LDA $4D60,X` (the
#           type array, this file's DS:197) with `CMP #$2C`/`CMP #$48` for the two water kinds
#           accepting iff tile == 0; then `CMP #$78 ; BCS` guarding `CMP #$5C ; BEQ accept`;
#           then refuse {0x10, 0x04, 0x00}, accept below 0x14, accept 0x70, refuse otherwise;
#           then the same site guard on $4E14 (DS:4A) and the candidate pair $F4/$F5 (DS:23/24).
#           Answers two open questions at once, both at CORROBORATED grade — it does not rename
#           this file and does not promote the species reading below.
#             Q1 the 0x78 split: the threshold, the tile it guards and the direction of the
#                clause are original design, not a DOS-port invention. What is NOT corroborated
#                is the *meaning* of >= 0x78: "the four NPC classes" still rests on mon_type>>2
#                being the art index, and the Apple side only repeats the number.
#             Q2 the tile ladder: the accept set arrives on both sides through a compare ladder
#                over the same byte values, not a table lookup, so the ladder is the shape the
#                rule was written in from the start.
#           Found 2026-10-28 with tools/apple2_anchor_map.py (string anchors) and confirmed
#           against the image bytes, not against Ghidra's decompiler output.
# label: kept as sub_2199 on purpose — labels are body and CP0 md5s them
# func 2199..21FB  len 0063 B  evidence: seed+call+reko
# MERGED 2026-10-28d from 18 fragments the Phase 2/3 wave cut out of one routine: 21A3 21A6 21AE 21B2 21B6 21BD 21C5 21C9 21CD 21D1 21D5 21D8 21DF 21E2 21E9 21F0 21F7. Body lines are concatenated in address order and none was edited; engine/tests/asm_body_guard.py requires the tree's assemblable content to be conserved, and it is.
# provenance: static, decoded from asm/2199..asm/21FC in address order 2026-10-28
# confidence: high for the bytes and the verdict polarity (both returns are in the bytes
#           and three callers branch on them); medium for the species reading, which
#           depends on mon_type>>2 being the art index (docs/art-index.md)
sub_2199:
L_002399:
        mov    %al,0x1f
L_00239c:
        mov    0x26a,%al
L_00239f:
        cmp    $0x0,%al
L_0023a1:
        je L_0023a6
sub_21a3:
L_0023a3:
        jmp L_0023fc
L_0023a5:
        nop
sub_21a6:
L_0023a6:
        mov    0x197(%di),%al
L_0023aa:
        cmp    $0x2c,%al
L_0023ac:
        je L_0023d8
sub_21ae:
L_0023ae:
        cmp    $0x48,%al
L_0023b0:
        je L_0023d8
sub_21b2:
L_0023b2:
        cmp    $0x78,%al
L_0023b4:
        jae L_0023bd
sub_21b6:
L_0023b6:
        mov    0x1f,%al
L_0023b9:
        cmp    $0x5c,%al
L_0023bb:
        je L_0023e2
sub_21bd:
L_0023bd:
        nop
L_0023be:
        mov    0x1f,%al
L_0023c1:
        cmp    $0x10,%al
L_0023c3:
        je L_0023fc
sub_21c5:
L_0023c5:
        cmp    $0x4,%al
L_0023c7:
        je L_0023fc
sub_21c9:
L_0023c9:
        cmp    $0x0,%al
L_0023cb:
        je L_0023fc
sub_21cd:
L_0023cd:
        cmp    $0x14,%al
L_0023cf:
        jb L_0023e2
sub_21d1:
L_0023d1:
        cmp    $0x70,%al
L_0023d3:
        je L_0023e2
sub_21d5:
L_0023d5:
        jmp L_0023fc
L_0023d7:
        nop
sub_21d8:
L_0023d8:
        mov    0x1f,%al
L_0023db:
        cmp    $0x0,%al
L_0023dd:
        jne L_0023fc
sub_21df:
L_0023df:
        jmp L_0023e2
L_0023e1:
        nop
sub_21e2:
L_0023e2:
        mov    0x4a,%al
L_0023e5:
        cmp    $0x0,%al
L_0023e7:
        je L_0023f7
sub_21e9:
L_0023e9:
        mov    0x23,%al
L_0023ec:
        cmp    $0x0,%al
L_0023ee:
        je L_0023fc
sub_21f0:
L_0023f0:
        mov    0x24,%al
L_0023f3:
        cmp    $0x0,%al
L_0023f5:
        je L_0023fc
sub_21f7:
L_0023f7:
        mov    $0x0,%al
L_0023f9:
        cmp    $0x0,%al
L_0023fb:
        ret

# sector_store — ONE routine that writes THREE files: mapxNN, monxNN and the player record
# name-evidence: the body is three DOS writes and nothing else, all under a single `b4 28`
#           mov $0x28,%ah (DOS Write File, FCB form) that is set once at runtime 0x232c and
#           never reloaded, and a single `ret` at runtime 0x2361. The three writes are:
#             `b9 00 10` mov $0x1000,%cx ; `8b 16 72 04` mov 0x472,%dx ; call L_0054ca  "MAPXFF  "
#             `b9 00 01` mov $0x100,%cx  ; `8d 16 37 01` lea 0x137,%dx ; call L_0054ca  "MONXFF  "
#                                   `8d 16 36 00` lea 0x36,%dx  ; call L_0054ca  "PLAYER  "
#           i.e. 4096 bytes of the sector buffer at DS:472, then the 256-byte creature block at
#           DS:137 (`mon_x[32]`, state.h:430, _Static_assert-pinned), then the 256-byte player
#           record at DS:36. Each filename is inline in the stream immediately after its call.
#           Before the first write it self-patches FOUR filename digits with the sector number:
#           `2e a2 3c 23` / `2e a2 3d 23` write the two `F`s of `MAPXFF  ` at runtime 0x2338 and
#           `2e a2 4e 23` / `2e a2 4f 23` write the two `F`s of `MONXFF  ` at runtime 0x234a.
#           `a2 5a 00` / `a2 5b 00` set the FCB random record to 0,1 first.
# func 2305..2361  len 005D B  evidence: seed+reko, boundary confirmed by the ret at 2361
# MERGED 2026-11-15 from four files. The Phase 2 map cut this one routine into 2305, 2340, 2352
#           and 2361, on fall-through grounds, which the G22 ruling (§ 3) forbids — the same
#           failure already recorded for cmd_C, cmd_D and cmd_K. The cut at 0x2340 additionally
#           produced a confidently wrong name: the second half was called `monster_data_load` and
#           described as *reading* the creature block, because a read and a write take the same
#           CX and DX and only AH distinguishes them, and AH is set in the first fragment. It is a
#           write. Bodies are concatenated verbatim, every label line kept, so the canonical
#           stream is unchanged and no `body corrected:` marker is owed.
# note: the ASCII tails are NOT code. `4d 41 50 58 46 46 20 20` (`MAPXFF  `), `4d 4f 4e 58 46 46
#           20 20` (`MONXFF  `) and `50 4c 41 59 45 52 20 20` (`PLAYER  `) are the filename
#           templates, sitting in the stream immediately after the DOS call each belongs to, and
#           the disassembler ran on into them and invented `dec %bp`, `dec %di`, `inc %si`,
#           `push %ax`, `dec %sp`, `pop %cx`, `inc %bp`, `push %dx` and `and %ah,(%bx,%si)` from
#           the letters. Same failure already recorded for the clue pool (asm/1B6B), the barkeep
#           table (asm/16BD) and asm/4C50: **a forced-keep or nonsense run whose bytes are ASCII
#           is text.** Left as-is because each template is owned by this routine — it is what this
#           routine patches — and cutting them out would re-open the boundary question for 24 bytes.
#           The body is unchanged and assemble_clean.sh is the authority.
# reached from: asm/22FD_sector_store_gate, by that routine's ONLY branch (`je` when DS:6D == 0).
#           There is no direct call or jump to this address anywhere in the binary, which is why a
#           call-target scan alone makes it look dead; it is entered by fall-through-equivalent
#           conditional branch from the gate. The gate has eight callers, listed in its header:
#           site entry for town, tower and dungeon, cmd_L, the save path, and three wrappers.
# provenance: measured 2026-11-15, twice, and the second measurement refutes the first.
#           Executed END TO END — every offset 0x2305..0x2335 including the DOS call — in the
#           ExecutedInstructions of 33 harvests. The sector READ (asm/22D5) appears in 13, which
#           is the control that shows the scan works. In both harvests that carry a memory dump
#           alongside a flow dump, DS:6D == 0x00, i.e. Earth, as the gate requires.
#           Breakpoint evidence, captures/spice86/g82-planet-x-live method: a breakpoint on the
#           first DOS call at runtime 0x232c fires **once** per site entry and **zero** times per
#           site exit, while the entry's md5 diff shows `mapx30`, `monx30` AND `player` all
#           changed. One call, three files. The exit's `player` write is a different site, the
#           inline one in asm/3823 at runtime 0x389d, which its own breakpoint confirms.
# CORRECTION, 2026-11-15. Two claims in earlier versions of this header were wrong and both were
#           committed: that this routine "issues ONE write", and that a site entry writing
#           `monx30` while `mapx30` came back byte-identical proved only one file is written per
#           call. Both were inferred from which files' checksums moved, and both are the exact
#           trap recorded in docs/harness-portability.md § 6: **a file that comes back
#           byte-identical after an operation is not evidence the write did not happen.** Storing
#           unchanged content is indistinguishable from not storing. The bytes above settle it —
#           three writes, unconditionally, every call.
# confidence: high
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
sub_2305:
L_002505:
        clc
L_002506:
        mov    0x49,%al
L_002509:
        adc    $0x30,%al
L_00250b:
        mov    %al,%cs:0x233c
L_00250f:
        mov    %al,%cs:0x234e
L_002513:
        mov    0x4a,%al
L_002516:
        adc    $0x30,%al
L_002518:
        mov    %al,%cs:0x233d
L_00251c:
        mov    %al,%cs:0x234f
L_002520:
        mov    0x0,%al
L_002523:
        mov    %al,0x5a
L_002526:
        mov    0x1,%al
L_002529:
        mov    %al,0x5b
L_00252c:
        mov    $0x28,%ah
L_00252e:
        mov    $0x1000,%cx
L_002531:
        mov    0x472,%dx
L_002535:
        call L_0054ca
L_002538:
        dec    %bp
L_002539:
        inc    %cx
L_00253a:
        push   %ax
L_00253b:
        pop    %ax
L_00253c:
        inc    %si
L_00253d:
        inc    %si
L_00253e:
        and    %ah,(%bx,%si)
sub_2340:
L_002540:
        mov    $0x100,%cx
L_002543:
        lea    0x137,%dx
L_002547:
        call L_0054ca
L_00254a:
        dec    %bp
L_00254b:
        dec    %di
L_00254c:
        dec    %si
L_00254d:
        pop    %ax
L_00254e:
        inc    %si
L_00254f:
        inc    %si
L_002550:
        and    %ah,(%bx,%si)
sub_2352:
L_002552:
        lea    0x36,%dx
L_002556:
        call L_0054ca
L_002559:
        push   %ax
L_00255a:
        dec    %sp
L_00255b:
        inc    %cx
L_00255c:
        pop    %cx
L_00255d:
        inc    %bp
L_00255e:
        push   %dx
L_00255f:
        and    %ah,(%bx,%si)
sub_2361:
L_002561:
        ret

# sector_store — writes the whole 4096-byte sector buffer back to its own file, on Earth
# name-evidence: the body is the DOS write and nothing else. `b4 28` mov $0x28,%ah (DOS Write
#           File, FCB form); `b9 00 10` mov $0x1000,%cx (4096 records of 1 byte = one whole
#           sector); `8b 16 72 04` mov 0x472,%dx (DS:472, the 4096-byte sector buffer);
#           `e8 92 2f` call L_0054ca (the DOS seam). Before it, it self-patches TWO filename
#           templates with the sector digits: `2e a2 3c 23` / `2e a2 3d 23` write the two `F`s of
#           the `MAPXFF  ` at runtime 0x2338, and `2e a2 4e 23` / `2e a2 4f 23` write the two `F`s
#           of the `MONXFF  ` at runtime 0x234a, which lives in asm/2340_monster_data_load.asm.
#           `a2 5a 00` / `a2 5b 00` set the FCB random record to 0,1 first.
# func 2305..233F  len 003B B  evidence: seed+reko
# note: the last eight bytes of this span are NOT code. `4d 41 50 58 46 46 20 20` is the
#           `MAPXFF  ` template itself, sitting in the stream immediately after the DOS call, and
#           the disassembler ran on into it and invented `dec %bp`, `dec %di`, `inc %si`,
#           `and %ah,(%bx,%si)` from the letters. The routine proper ends at the `call L_0054ca`.
#           Same failure already recorded for the clue pool (asm/1B6B), the barkeep table
#           (asm/16BD) and asm/4C50: **a forced-keep or nonsense run whose bytes are ASCII is
#           text.** Left as-is rather than re-cut because the template is owned by this routine --
#           it is what this routine patches -- and splitting it would add a map row for eight
#           bytes. The body is unchanged and assemble_clean.sh is the authority.
# reached from: asm/22FD_sector_store_gate, by that routine's ONLY branch (`je` when DS:6D == 0).
#           There is no direct call or jump to this address anywhere in the binary, which is why a
#           call-target scan alone makes it look dead; it is entered by fall-through-equivalent
#           conditional branch from the gate. The gate has eight callers, listed in its header:
#           site entry for town, tower and dungeon, cmd_L, the save path, and three wrappers.
# provenance: measured 2026-11-15. Executed END TO END -- every offset 0x2305..0x2335 including
#           the DOS call -- in the ExecutedInstructions of 33 harvests. The sector READ (asm/22D5)
#           appears in 13, which is the control that shows the scan works. In both harvests that
#           carry a memory dump alongside a flow dump, DS:6D == 0x00, i.e. Earth, as the gate
#           requires.
# confidence: high
# note: what file, and what is NOT claimed here. This routine patches both the MAPX and the MONX
#           name and issues ONE write of the sector buffer at DS:472. Which of the two FCBs is
#           current at each call site has not been traced, so this header does not claim both files
#           are written per call. docs/phase4-state.md records the measured save-path behaviour
#           (outdoors + Earth writes player AND mapxNN AND monxNN), which is the stronger evidence
#           for both names being written, from asm/3DFA rather than from here.
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

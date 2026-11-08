# sub_2744 — mechanical clean (bulk wave); behavior not yet reviewed
# DECODED 2026-10-29: the mismatch path, and the only place in the tree where the number 8 and a
#           mod-8 counter appear together in a way that matches a live observation. Reached when
#           asm/2731 does NOT find `0xC0`. It sets DS:6F = 8, adds 2 to DS:6E, masks it with
#           `and $0x7,%al`, stores it back, then rebuilds the index from DS:1F and loads the next
#           candidate cell from the same table and re-tests it.
#           OBSERVED 2026-10-29 (docs/moongates.md § 2, captures/spice86/moongate-cycle): the
#           live rotation behaves exactly like this. DS:6F reloads to precisely 8 and then
#           decrements about every 4.2 s with the player standing still, and DS:6E steps
#           00 -> 02 -> 04 -> 06 -> 00, each value selecting the next cell of the era-2 row of
#           the table at file 0x2998. Four cells matched four table entries in index order,
#           and with a third era watched live the model is now 12/12 — see docs/moongates.md § 3.
#           CORRECTION to a recalled detail worth recording: the cycle is TIME, not turns. The
#           countdown ran at a steady rate with no input at all, so "every 8 steps" is wrong in
#           the unit even though the 8 is right.
#           HALF RIGHT, and the other half is in docs/moongates.md section 2. Settled 2026-11-08 by
#           captures/spice86/g73-6f-unit, which found the answer is BOTH and neither: one decrement
#           per consumed keypress, plus one per ~8 s when nothing is pressed. "Steady rate with no
#           input" was a true observation of the timeout arm. The turn reading in docs/moongates.md
#           was a true observation of the keypress arm. Neither session noticed the other variable.
#           ATTRIBUTION REFUTED 2026-10-29 — see asm/2708. The listing's shape matched the live
#           rotation and that match was coincidence, not causation: a stop at 17D:2908 never
#           fired across a window in which the gate rotated twice. The OBSERVED paragraph above
#           stands on its own measurements and is nothing to do with this file.
# func 2744..2775  len 0032 B  evidence: seed+reko
# calls: L_0052b3
# confidence: low (bytes proven by check_func.py)
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
sub_2744:
L_002944:
        mov    $0x8,%al
L_002946:
        mov    %al,0x6f
L_002949:
        incb   0x6e
L_00294d:
        incb   0x6e
L_002951:
        mov    0x6e,%al
L_002954:
        and    $0x7,%al
L_002956:
        mov    %al,0x6e
L_002959:
        clc
L_00295a:
        adc    0x1f,%al
L_00295e:
        mov    $0x0,%ah
L_002960:
        .byte 0x8b,0xf8  # forced-kept: gas re-encodes 'mov    %ax,%di' differently
L_002962:
        mov    %cs:0x2798(%di),%al
L_002967:
        mov    %al,0x23
L_00296a:
        inc    %di
L_00296b:
        mov    %cs:0x2798(%di),%al
L_002970:
        mov    %al,0x24
L_002973:
        call L_0052b3

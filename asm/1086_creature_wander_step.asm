# creature_wander_step — role >= 3: stage this slot's per-slot step vector, then roll
# name-evidence: reached from the role dispatch at asm/1035 (`al >= 3 -> L_001286`), and its body
#           loads DS:19/DS:1A — the step vector, the same pair asm/0AFC says `call L_005111`
#           overwrites with the unit step — from two per-slot arrays indexed by DS:25: DS:1F7 for x
#           and DS:217 for y. It does NOT compute anything toward the player, which is what makes it
#           a wander and not a pursuit. `L_005417` is rand_byte (established by
#           asm/0B62_melee_hit_roll and asm/0AAA_combat_special_attack_gate).
# PROVEN (measured, 2026-10-26, captures/spice86/linda-thief and linda-aggro):
#           * DS:1F7 and DS:217 hold ONLY 0x00, 0x01 and 0xFF live — signed unit steps in {-1,0,+1}.
#             Read from two independent memory dumps, and peeked live at DS:1F7/DS:217.
#           * They are non-zero ONLY in role-3 slots. In sector 22 slots 0..15 (roles 0, 2, 0x81..0x84)
#             are all zero; slots 16..31 (all role 03) carry the vectors.
#           * `monx22` ships ZEROS at file offsets 192 and 224, which map to DS:1F7/DS:217 under the
#             established monx[0] -> DS:137 mapping (192 + 0x137 = 0x1F7). So the vectors are written
#             at runtime, not loaded from the sector file.
#           * Role-3 creatures DO move. Thief slot 20 in sector 22 was seen at (7,47), (7,49) and
#             (8,48) across PASS commands, i.e. its direction changed during play. This is what
#             refutes the engine's `moves = (role == 1 || role == 2)`, which freezes them.
# THE WRITER AND THE MOVE SITE — found 2026-10-26 by reading the contiguous fall-through, static:
#           This fragment does NOT stand alone; asm/1086..asm/10E8 is one straight-line chain that the
#           role dispatch enters at L_001286 and that falls through, so the pieces below are one
#           routine cut into eight files by the bulk wave:
#             1086  DS:19 := V1[slot] ; DS:1A := V2[slot] ; call rand_byte
#             10A0  cmp $0x40,%al ; jae 10CE      ; 75% keep the stored direction, 25% re-roll
#             10A4  call rand_byte
#             10A7  call L_0052d8                 ; = asm/50D8_sign_of_al -> -1/0/+1
#             10AA  V1[slot] := AL ; DS:19 := AL ; call rand_byte
#             10BC  call L_0052d8                 ; sign_of_al again
#             10BF  V2[slot] := AL ; DS:1A := AL
#             10CE  clc ; al := mon_x[slot] ; adc DS:19,al ; and $0x3f ; DS:23 := al
#                   then range guards (cmp $0x4 / cmp $0x3c / cmp $0) that exit to L_001240, the
#                   same next-slot target asm/1033 uses
#           So it is a **random walk with a sticky direction**: the per-slot vector persists and is
#           re-rolled only on the 25% branch. That is what makes a role-3 creature loiter near its
#           post instead of scattering, and it is what was seen live — thief slot 20 drifted between
#           (7,47), (7,49) and (8,48) rather than hunting the player.
#           DS:23 is the staged candidate cell that asm/2199_creature_step_gate reads, so the wander
#           feeds the SAME step gate as the pursuit paths. The value in DS:19/1A is what asm/212E
#           adds to mon_x/mon_y (see asm/4F11_monster_step_vector).
# TWO CORRECTIONS, one of them this file's own:
#           * docs/commerce.md says of these arrays: "per-entity arrays, not direction vectors".
#             That is now **wrong** — asm/10AA and asm/10BF write sign_of_al(rand_byte) into them and
#             into DS:19/DS:1A in the same instruction pair. Corrected there.
#           * docs/archive/handoff-cp5-2026-10-24.md called this a "per-slot persistent direction". An earlier
#             revision of this header today called that word *doubtful*, because a tracked creature
#             changed direction. It was right and I was wrong: it IS persistent, re-rolled on only
#             one turn in four. Retracted.
# STILL NOT ESTABLISHED:
#           * Where the y candidate is staged (the fragments after asm/10CE, not yet read to the end)
#             and the exact instruction that enters asm/2199 from this chain.
#           * What the three range guards at asm/10CE are guarding against, and how they relate to
#             asm/2199's own edge guard on DS:4A.
#           * The identity of DS:25, still `reserved_25` in state.h. It indexes the arrays in all
#             four fragments, so it is almost certainly the slot index, but that is inference.
# NOTE on the dispatch: asm/1035 tests `al >= 3`, not `== 3`. The engine has `if (role > 3) continue`,
#           which differs. It is currently harmless: a census of all 33 shipped monx files gives
#           role 0x00 x686, 0x01 x54, 0x02 x75, 0x03 x179, 0x81..0x85 x62, and NO role > 3 anywhere.
#           If a sector ever ships one, the engine will freeze what the original wanders.
# provenance: live:linda-thief, live:linda-aggro (2026-10-26) + static reassembly; writer and move
#           staging found statically the same day
# confidence: high for the vector semantics, the 25% re-roll and the role-3 movement; medium for the
#           tail past asm/10CE, which is read but not finished
# func 1086..109F  len 001A B  evidence: seed+reko
# calls: L_005417
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
# label: kept as sub_1086 on purpose — labels are body (CP0)
sub_1086:
L_001286:
        nop
L_001287:
        mov    $0x0,%bh
L_001289:
        mov    0x25,%bl
L_00128d:
        .byte 0x8b,0xfb  # forced-kept: gas re-encodes 'mov    %bx,%di' differently
L_00128f:
        mov    0x1f7(%di),%al
L_001293:
        mov    %al,0x19
L_001296:
        mov    0x217(%di),%al
L_00129a:
        mov    %al,0x1a
L_00129d:
        call L_005417

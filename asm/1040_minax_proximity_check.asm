# minax_proximity_check — Minax strikes from up to four cells away
# name-evidence: tests mon_type == 0x40, then |mon_x - player_x| < 4 and
#   |mon_y - player_y| < 4 via asm/50EF (abs of a signed byte), then calls
#   L_00118e = asm/0F8E_combat_minax_attack. The reach-test instructions themselves are
#   in the fall-through fragment asm/1048_minax_reach_test (this file is only the
#   `cmp $0x40 ; jne` type gate); read together asm/1040 + asm/1048 are the one routine. Type 0x40 is Minax: the art index is
#   type>>2 (docs/art-index.md) and 0x40>>2 == 0x10, the tile asm/0F8E's own note
#   identifies as Minax by bitmap identity.
# reached from: TWO places. (1) the role dispatch: asm/102B's `or %al,%al ; je L_001240`
#   sends role == 0 here, and asm/1033's `js L_001240` sends any bit-7 role here (the sign
#   comes from the same `or`). Minax is role 0x84 in monx03 (slot 31, type 0x40), so she
#   reaches this routine through the bit-7 `js`, NOT through role == 0 -- the handoff's
#   "Minax is role 0" was wrong about the value. The scripted townsfolk at 0x81..0x83 also
#   route here and fall out at the `jne L_00126d` because their type is not 0x40. (2) the
#   creature move routine at asm/218b, and only when [DS:4A] != 0 (the sector's second
#   digit). asm/218b is reached both when a creature's candidate cell is the player's cell
#   and when all three candidate moves were refused, so this check runs for blocked movers
#   too. Path (1) is why she strikes every turn without moving.
# not: the general monster attack. Contact with the player produces no attack at
#   all — see docs/monster-spawn.md § 13, which is also the correction of the
#   "0x1240 sits in a gap" note that blocked CP5 W6 for a session.
# provenance: static. Read from the listing and decoded 2026-10-28d; bytes proven by
#           tools/check_func.py. The type-0x40-is-Minak identification is MEASURED, not recalled:
#           it comes from bitmap identity in docs/art-index.md (type>>2 -> art tile), and it is a
#           third-party-adjacent inference about a name, not an observation of the fight. No
#           harvest has ever executed this path — the only Minax strings in the corpus
#           (`MINAX IS DEAD!!`, `MINAX CRIES: DIE FOOL!`) are discovered-but-not-executed, and
#           docs/phase4-state.md records them as still unexecuted.
# EXECUTED 2026-11-12, ending the note above. Owner play session in the original under
#           Spice86 (captures/spice86/minax-fight-live/): he reported "she cries die fool and
#           range attacks me, when I'm about 3 steps away from her", inside a castle chamber.
#           That is this routine's own window stated in plain language -- `|mon_x - player_x|
#           < 4` and the same on y, i.e. up to three cells on each axis -- observed
#           independently by a person rather than read off the listing, and it is the first
#           execution of this path anywhere in the project's history. The `< 4` half of the
#           third-party "exactly 100 damage, range 3" claim is therefore no longer merely
#           corroborated by our bytes; the behaviour has been seen. Also confirmed by the
#           same session: it fires indoors, which is what `reached from asm/218b ... only when
#           [DS:4A] != 0` predicts, and HP fell 9000 -> 2336 at roughly 100 a strike.
# confidence: medium — the bytes and the reachability are solid, the Minax naming rests on the
#           art index rather than on any text this routine prints
# func 1040..1047  len 0008 B  evidence: seed+reko
# confidence: medium (the branch and the callee are bytes; the Minax identity is
#   third-party art matching plus our own type>>2 rule)
# label: kept as sub_1040 on purpose — labels are body and CP0 md5s them
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
sub_1040:
L_001240:
        mov    0x197(%di),%al
L_001244:
        cmp    $0x40,%al
L_001246:
        jne L_00126d

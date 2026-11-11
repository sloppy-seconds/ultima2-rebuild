# minax_hit_teleport_gate — on a SURVIVING hit, if the creature is Minax (type 0x40),
#           branch to the teleport at asm/2C38_minax_teleport_swap.
# name-evidence: `mov 0x197(%di),%al ; cmp $0x40,%al ; je L_002e38`. Reached from the
#           shared HP-subtract survive path: asm/2C19 (melee and cannon both subtract HP
#           here) falls through to asm/2C26_monster_hp_set (store the reduced HP), which
#           falls through to THIS gate. So a blow that does not kill Minax routes here and
#           then to the swap; every other type falls to asm/2C35 (`jmp L_000c33`, return).
# PROVEN LIVE 2026-11-12: seeded adjacent to Minax (slot 31, type 0x40) in sector 03, one
#           QUICK SWORD hit took her 255->199 and this gate sent her to asm/2C38, which
#           moved slot 31 from (60,3) to (3,60). This is the mechanism the CP8 handoff
#           called "observed, not decoded" and a first static pass wrongly refuted; the
#           teleport is real and lives here. See docs/cp8-minax-gaps.md § 2.
# func 2C2D..2C34  len 0008 B  evidence: seed+reko
# provenance: static, decoded 2026-11-12 from the listing while confirming the live teleport;
#           bytes proven by tools/check_func.py. The gate itself has not been seen in a
#           trace (the minax-fight capture is frames-only), but its effect was observed live.
# confidence: high (three bytes, and the effect is confirmed against a live memory diff)
# label: kept as sub_2c2d on purpose — labels are body and CP0 md5s them
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
sub_2c2d:
L_002e2d:
        mov    0x197(%di),%al
L_002e31:
        cmp    $0x40,%al
L_002e33:
        je L_002e38

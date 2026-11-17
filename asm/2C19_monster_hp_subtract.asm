# monster_hp_subtract — second HP decrement site: HP -= the damage byte at [DS:1F]
# name-evidence: `stc; mov 0x177(%di),%al; cmc; sbb 0x1F,%al; cmc; jae L_002ea4`,
#           falling through to asm/2C26_monster_hp_set on a hit. Same BCD-guard idiom
#           as asm/2E3D_combat_attack_flow (which subtracts [DS:20]); here the damage
#           source is [DS:1F], so this is a different damage route onto the same field.
#           [DS:1F] is now traced: asm/3606 sets it to `rand | 0x20`. This header used to read
#           that as "32..63" and that is WRONG — `or` forces bit 5 on a full-range byte, so the
#           reachable set is every byte with bit 5 set (32..63, 96..127, 160..191, 224..255):
#           floor 32, ceiling 255. Corrected 2026-10-29 by engine/tests/attack_check.c, which
#           sweeps all 256 seeds and fails if any roll lacks bit 5 or if nothing exceeds 63.
#           The caller is also now known: asm/35F3..asm/3611 is cmd_F, the frigate's cannon.
#           So "what selects the slot" is asm/23E6's one-cell creature probe, and the magnitude
#           is a forced-bit-5 random byte with no upper bound worth the name.
#           Reached by `jmp L_002e19` from asm/3611, which is the one routine found
#           that is aware of the site kind (`cmpb $0x4,[DS:4A]`, tower). That makes
#           asm/3611 the best remaining lead for how town and tower NPCs take damage,
#           since no spawn arm assigns them HP at all.
# THE KILL EXIT, added 2026-11-11. `jae L_002ea4` is the death branch, and asm/2CA4 is not the
#           end of it: that routine has no `ret` and falls through into the Minax gate, the
#           kill-drop dispatch and asm/2D8C_combat_kill_reward. So a cannon kill pays gold and
#           exp and rolls a drop exactly as a melee kill does — observed in
#           captures/spice86/cannon-kill-live as `KILLED--GOLD+13--EXP.+03`. Read the tail
#           before concluding a kill branch stops where its jump lands; see
#           asm/2CA4_combat_kill_cell_ptr for the full chain and for the engine defect this
#           fall-through caused.
# provenance: static, 2026-10-22; kill exit live 2026-11-11 (cannon-kill-live)
# confidence: low (bytes proven by check_func.py; the caller's role is inferred)
# label: kept as sub_2c19 on purpose — labels are body (CP0)
# func 2C19..2C25  len 000D B  evidence: seed+reko
# confidence: low (bytes proven by check_func.py)
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
sub_2c19:
L_002e19:
        stc
L_002e1a:
        mov    0x177(%di),%al
L_002e1e:
        cmc
L_002e1f:
        sbb    0x1f,%al
L_002e23:
        cmc
L_002e24:
        jae L_002ea4

# combat_kill_cell_ptr — a kill points the cell pointer at the dead creature's cell; the WRITE is in asm/2CB5, not here
# NAME CORRECTED: 2026-11-18 -- evidence: this file's own callee. `call L_0052b3` enters
#           asm/50B3_map_tile_ptr, which computes y*64+x+DS:0472, leaves the address in DS:6,
#           loads AL with the byte already there and RETURNS WITHOUT STORING. Nothing in these
#           seventeen bytes writes to the map. The old name, `combat_kill_stamp`, asserted a
#           store this file does not contain, and the engine implemented that misreading: it
#           left the corpse's sprite stamped and the player walked past a monster he had
#           already killed. The store is the next file down, asm/2CB5, which writes
#           DS:1B7[slot] through the pointer this one computed.
#           The name is kept as close as possible because the routine really is the kill path's
#           cell addressing step; only the word `stamp` was a claim the bytes do not support.
# name-evidence: `mov 0x137(%di),%al ; mov %al,0x23 ; mov 0x157(%di),%al ; mov %al,0x24 ;
#           call L_0052b3` — the dead slot's mon_x/mon_y into the cursor pair DS:23/24 and
#           then into asm/52B3, the cell stamper. Its only entry is `jae L_002ea4` from
#           asm/2C19_monster_hp_subtract, taken when the frigate cannon's damage exceeded the
#           creature's hp.
# func 2CA4..2CB4  len 0011 B  evidence: seed+reko+live
# calls: L_0052b3
# provenance: live:cannon-kill-live 2026-11-11 — a one-cell cannon kill of a planted creature
#           printed `KILLED--GOLD+13--EXP.+03` and moved DS:56..59, and that text exists only
#           downstream of here.
# confidence: medium (bytes proven by check_func.py; the exit is the point of this header)
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
#
# WHAT THIS FILE DOES NOT SHOW IS THE WHOLE POINT OF IT. The routine has no `ret` and no `jmp`:
# the very next byte is asm/2CB5_combat_kill_minax_gate, and the chain runs on through the
# kill-drop dispatch at asm/2CD3 / asm/2D22 — whose arms all tail-`jmp L_002f8c` — into
# asm/2D8C_combat_kill_reward, which clears the dead slot and prints `KILLED--GOLD+`.
# So a cannon kill pays gold and exp, rolls the type-keyed drop, and — because the Minax gate
# sits on the same fall-through — a cannon kill of Minax wins the game.
#
# This is the third time this address range hid a routine behind a fall-through (asm/2D07 and
# asm/2CB5 were the other two, both for the same reason: the Phase 2 map tiles *starts*, so a
# tail that is not a call edge is invisible to every static pass). It cost this project a
# shipped defect: `engine/src/game/combat.c` carried an `award` parameter that paid a cannon
# kill nothing, justified by the sentence "there is no reward roll anywhere on that path",
# which was a correct reading of eleven bytes and a wrong reading of the game. The gate is
# `engine/tests/cannon_kill.py`.
sub_2ca4:
L_002ea4:
        mov    0x137(%di),%al
L_002ea8:
        mov    %al,0x23
L_002eab:
        mov    0x157(%di),%al
L_002eaf:
        mov    %al,0x24
L_002eb2:
        call L_0052b3

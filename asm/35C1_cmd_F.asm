# cmd_F — command handler 'F': prints "FIRE" then falls
#           through into the following map fragment
# func 35C1..35C8  len 008 B  evidence: seed+reko (jump table cs:0730 entry F)
# provenance: live:airport32 (+3 other harvests), static
#           entry executed in 8 of 283 harvests; derived by tools/provenance_from_traces.py
# confidence: high (handler role proven; message literal)
# name-evidence: read 2026-10-29. FIRE is the FRIGATE'S CANNONS, not a melee attack, and the
#           mode gate says so: asm/35C9 reads [DS:13], the travel-mode stamp, and continues
#           only on 0x24, which is the ship value from asm/2FAD's BOARD dispatch (0x22 horse,
#           0x24 ship, 0x26 plane). Anywhere else the tail prints " WHAT?" at asm/35D0, so on
#           foot the whole command is `FIRE WHAT?`. On the frigate it prints " DIRECT-" at
#           asm/35DE and goes through the aim gate at asm/35EA -- an aimed cannon, the same
#           family as cmd_A/S/U/T.
#           This corrects `engine/src/game/commands.c`, which had filed F as "the combat/
#           attack arm" from the bare word FIRE. cmd_A is the attack; F is a vehicle weapon.
#           Owner recollection supplied the pointer, the mode byte confirmed it.
# provenance: live:cannon-range-live (2026-10-29)
#           OBSERVED, not inferred. The whole chain executed in a live run and every address in
#           it is in that capture's ExecutedInstructions: 35C1 entry, 35C9 gate, 35DE DIRECT-,
#           35ED tone, 35F0 probe, 35F3 slot, 3603 rand, 3606 or-$0x20, 3611 apply, 2C19
#           subtract, 2E09 --MISS. The frigate state was built by poking [DS:13]=0x24 (that is
#           the entire gate, so no boarding was needed) and the target by planting mon_type/x/y
#           at a chosen cell, with [DS:26A] frozen so the creature could not wander off it.
# RANGE: ONE CELL, measured. Targets at 1, 2 and 3 cells east of the player, fired at with the
#           same keypress: distance 1 hit twice (hp 0x99->0x5B and 0x99->0x76) and distances 2
#           and 3 missed both times, unchanged. The owner's recollection was a 3-space reach, and
#           the live game disagrees. asm/23E6 probes a single cell and this is now the second
#           witness, the first being a frame rather than a listing.
# SILENCE ON A HIT: also on the frame. The distance-1 shot printed `FIRE DIRECT-EAST` and nothing
#           more, while the distance-2 shot printed `FIRE DIRECT-EAST--MISS`. So the only text on
#           this path is the miss, exactly as asm/35F0..asm/3611's absence of any str_out said.
# HP IS BINARY, NOT BCD: the two hits subtracted 0x3E and 0x23 from a byte holding 0x99, and
#           asm/2C19 has no das. A BCD-looking field decremented in binary. Do not "fix" this.
#           Confirmed again by the owner's own playtest the same day, on a real boarding rather
#           than a poked stamp: `B` aboard the frigate at sector 31 (14,25) set [DS:13] = 0x24
#           and left [DS:E2] BLUE TASSLE unchanged at 5, so boarding does not spend the gating
#           item — a second independent sample alongside captures/spice86/shipcap3 (G42).
# OWNER PLAYTEST, 2026-10-29: the owner drove this himself over noVNC and reached the same
#           verdict — one tile. Worth recording which sources disagreed, because there were three
#           and only one of them was the game: the printed MANUAL says the cannon has range, the
#           owner's own recollection said three spaces, and both are wrong. `asm/23E6` probes one
#           cell, the poke test hit at 1 and missed at 2 and 3, and play agrees. A manual is a
#           third-party claim like any other, and this binary gets the veto.
cmd_F:
        call L_0051df          # str_out — prints the inline string below
        db "FIRE",0        # string

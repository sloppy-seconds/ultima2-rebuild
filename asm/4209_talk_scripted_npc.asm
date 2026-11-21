# talk_scripted_npc — the TRANSACT branch for a slot with a scripted line: role - 0x80, print that tlkx record
# func 4209..4210  len 0008 B  evidence: seed+reko
# calls: L_00174e
# name-evidence: read directly, 2026-10-29, together with the fragments after it (4211..4246):
#             4209  stc ; cmc ; sbb $0x80,%al ; cmc   AL = [slot+0x1D7] - 0x80
#             420E  call L_00174e                    -> asm/154E_tlkx_record_printer
#             4211  di = 3 ; si = 0xFFFF ; dec until zero   -- a delay, four times over, so
#             421A  bx = 4 ; call L_000911               the line stays up about half a second
#             4223  di = [DS:20]                       (the direction byte cmd_T stored)
#             422B  if [DS:6D] == 9 and [DS:4A] == 3 and [di+0x1D7] == 0x81: [DS:6C] = 1
#             4246  jmp L_000c33
#           Reached from asm/40C0, which branches on bit 7 of [DI+0x1D7]: set means "this NPC
#           has a scripted line", clear means walk the creature-type chain (asm/40CB onward,
#           the street_* handlers). The role byte is therefore a record index into the
#           sector's tlkx file — the rule docs/archive/handoff-commerce-transact.md § 4.5 called
#           "completely unknown" and § 5 said to leave unimplemented rather than guess at. It
#           is no longer a guess: stopped at asm/154E on 2026-10-29 with AL=0x02 for the
#           role-0x82 Jester at (48,26) in sector 31, whose tlkx31 record 2 is
#           `RONALL MC DONALL SINGS: TRY OUR NEW RIDE THROUGH!`.
# note: what the frame does NOT show is where the line renders. The captures taken 0.3-1.1 s
#           after the key show `CMD: TRANSACT-WEST` and then the next prompt with no dialogue
#           line between them, and the map area is redrawn. The printer runs and the pause
#           runs; the placement is still open.
# note: the [DS:6D]==9 arm is deep space, a castle sector and role 0x81 setting [DS:6C] — a
#           scripted flag with no other reader found here. Recorded, not interpreted.
# provenance: live:food-buy, g23-food2, jester3, play-vnc2, play-vnc2-live, spell-slot-map
#           entry executed in 6 of the harvests that carry a flow dump; derived from
#           ExecutedInstructions.
# confidence: high for the arithmetic and the call target (bytes plus a live stop); low for
#           what the delay loop's `call L_000911` does and for where the text lands on screen
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
sub_4209:
L_004409:
        stc
L_00440a:
        cmc
L_00440b:
        sbb    $0x80,%al
L_00440d:
        cmc
L_00440e:
        call L_00174e

# cmd_D — command handler 'D' (DESCEND): prints "DESCEND", gates on the SITE KIND
#         at DS:4A; 4 (tower) and 5 (dungeon) pass, anything else prints "-WHAT?" and re-reads
# CORRECTION 2026-10-29, and the inline `# key byte` on the first instruction is stale — left
#         in place because body lines are frozen (AGENTS.md § 3), so the correction lives here.
#         DS:4A is not a key byte. It is the site kind, proven many times over (0 world map, 1
#         village, 2 town, 3 castle, 4 tower, 5 dungeon), and `cmp $4`/`cmp $5` is therefore
#         "DESCEND only works inside a tower or a dungeon", which is what DESCEND means. The
#         second gate, `[DS:14] & 0x20`, is the cell feature: the sector-24 tower's grid uses
#         0x20 for the down-ladder cell the player was standing on, and DS:14 is the tile under
#         the player.
# EXECUTED 2026-10-29 (captures/spice86/d12-ladder-ds25): in the sector-24 tower, standing on a
#         0x20 cell at (5,5) with DS:4A = 4, pressing D printed `CMD: DESCEND` and put the player
#         back on the world map at the gate cell (24,40) with DS:25 = 0xFF. That makes cmd_D one
#         of the two handlers that had never run in any harvest; the frame is `c-cmdD-descend.png`.
#         THE 0xFF IS THE PROOF, not a detail. It is what `decb` from 0 leaves behind, and the
#         `js` in asm/3823 is what turns it into an exit. So the one live observation of this
#         handler also pins the shape of the floor block, and it is what makes cmd_D and cmd_K
#         readable only as a pair: same kind gate, same cell gate with the feature bit swapped
#         (0x20 down, 0x10 up), same two floor arms with the direction swapped. A tower climbs up
#         and descends out; a dungeon descends deeper and climbs out.
#         IMPLEMENTED 2026-10-29, so the older note here ("still NOT implemented, an honest gap")
#         is retired. Engine: game/commands.c cmd_D and cmd_K over one shared site_shift().
#         Gate: CP6, twelve cases including the underflow exit. Not modelled: the [DS:6D] branch
#         of the exit block, which writes the player record on leaving a dungeon, and the floor
#         term of asm/1451's grid index, so a tower above its first floor still draws page 0.
# func 334D..3380  len 052 B  evidence: seed+reko (jump table cs:0730 entry D)
# provenance: live:d12-ladder-ds25; static
#           entry executed in 2 of 285 harvests; confirmed by the capture of 2026-10-29, which is
#           the first time cmd_D appeared in ExecutedInstructions in any harvest.
# confidence: high (structure read off the bytes; both gates and the outcome observed live
#         2026-10-29; the two resume sites at 0x3570 are still a Phase 2 mis-align)
# notes: 0x356d..0x356f are unreached filler bytes (not a jmp: displacement
#        underflows); two je sites resume at 0x3570 mid-mis-decode (Phase 2
#        mis-align class). Original bytes there: e9 c0 d6.
cmd_D:
        call L_0051df          # str_out — prints the inline string below
        db "DESCEND",0         # string
        mov    0x4a,%al        # key byte
        cmp    $0x5,%al
        je     .Lcmd_d_dir_ok
        cmp    $0x4,%al
        je     .Lcmd_d_dir_ok
.Lcmd_d_retry:
        call L_0051df          # str_out — prints the inline string below
        db "-WHAT?",0          # string (NUL at 0x356c)
        db 0xe9,0xc0,0xd6      # data: unreached filler before resume target
.Lcmd_d_dir_ok:
        mov    0x14,%al
        and    $0x20,%al
        je     .Lcmd_d_retry
        mov    0x4a,%al
        cmp    $0x4,%al
        jne    L_003581
        jmp    L_003a23

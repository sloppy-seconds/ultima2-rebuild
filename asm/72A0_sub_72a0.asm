# sub_72a0 — mechanical clean (bulk wave); behavior not yet reviewed
#           ** THIS FILE BEGINS WITH THE VICTORY SEQUENCE. ** The routine at L_0074a0 is what
#           asm/2CCD_victory_call invokes when a killed creature's type is 0x40. It is the
#           endgame, and it is decoded below rather than renamed, because this file is a bulk-
#           wave catch-all spanning 0x10E7 bytes and the victory block is only its first ~0x160.
#           Splitting it out is the right change and it is a structural one (it retiles the CP0
#           map), so it is left for the owner rather than done inside a decode commit.
#           ** EXECUTED 2026-11-12: every step below has now been watched, in order, in the
#           original by the owner** (captures/spice86/minax-fight-live/, 1104 one-second
#           frames). His report: "i don't change, but the map slow gets fades out then i end
#           up in a rocket flying and I get the 'you have saved the universe'." That is this
#           listing, in sequence, and three things came out of matching them:
#           1. The 64x64 fill of 0x74 with `call L_005cb1` plus a delay on every OUTER
#              iteration is a PROGRESSIVE fade, not an instant blackout -- 64 renders, which
#              is why it crawls. "Repaints a whole sector black" was right about the byte and
#              wrong about the experience.
#           2. `DS:13 = 0x28` is the ROCKET code and the `75E1..` loop walking DS:01 by one
#              per step with a delay is the rocket FLYING. The player sprite does not change;
#              the mode byte does. The engine rendered a rocket in the player's cell when this
#              fired, which reads as a transformation and is not one -- check victory.c
#              against this before trusting that frame.
#           3. The fade was seen in the chamber the player was standing in, which is NOT
#              sector 30, so the embedded `MAPX30  ` filename being the fill target is now
#              the weakest claim in this header rather than one of the stronger ones.
#              **CORRECTED 2026-11-20, and the reasoning was the mistake.** The filename is
#              not the fill target at all: it is the target of a READ that happens AFTER the
#              fade (see 7535..754C below). A fade over the chamber and a load of sector 30
#              afterwards are not in conflict, and the owner's frames from this very capture
#              show a half-faded map of open ocean, which is a world map and not a chamber.
#              The observation was right; the inference drawn from it was wrong.
#           RESOLVED 2026-11-12 by a live capture (`captures/spice86/victory-live/`: 110
#           frames + a change-triggered DS log + content-file md5s, driven by
#           tools/spice86_play_capture.py while a seeded character killed a type-0x40
#           creature in sector 30). Two answers:
#           * `L_005417` is a RANDOM-BYTE GENERATOR, not a cell-addressing call. The
#             `DS:23`/`DS:24` values it feeds (masked `& 0x3f`) during the fade are scattered
#             across 0x00..0x3f with no monotonic pattern (66 sampled pairs, e.g. 22/19,
#             39/19, 0a/1c, 14/09, 01/1b ...). So the 64x64 fill paints RANDOM cells black
#             each iteration -- that is what makes it a slow fade, not a systematic sweep.
#           * `L_0061a6` beside the `MAPX30  ` filename is NOT a sector SAVE, and that is
#             all this bullet ever proved. Across the whole fade + flight the run-dir content
#             files were md5'd every 2s and NONE changed (0 write events over 90 content
#             files), so the black repaint is in-memory grid only and is never persisted.
#             **CORRECTED 2026-11-20: it is a sector LOAD.** The caller's bytes are
#             `mov $0x27,%ah` -- INT 21h AH=27h, FCB random BLOCK READ -- with `CX=0x1000`
#             (4096 records, one 64x64 grid) and `DS:DX` pointing at an FCB, followed inline
#             by the name `MAPX30  `. Sector 30 is Earth, 1990 A.D. So the ending fades the
#             chamber black, reads the 1990 wilderness, and flies the rocket over that. The
#             md5 test could not have detected this at any sampling rate, because a read does
#             not change an md5; the bullet's own grade line said the file-I/O reading was
#             unexecuted, and what it should have said was that the test was blind to reads.
#           Bonus (G75c): the trailing flight loop is non-terminating -- `DS:00`/`DS:01` kept
#             advancing (17,19 -> 62,57 -> ... -> 33,07) for the full 30s+ observed, so the
#             rocket flies until the run is quit; it does not return to the command loop.
# victory sequence, decoded statically 2026-11-09 (file offsets; image = file - 0x200):
#   74A1  movb $0x28,DS:31          ; right_margin = 40, so the text prints in a narrow column
#   74A6  call str_out              ; "\r\r     MINAX IS DEAD!!\rALL HER WORKS SHALL DIE!\r"
#   74DA  mov $0x40,%al ; DS:1f=al  ; OUTER loop, 64 iterations
#   74DF  call L_0060d7
#   74E2  mov $0x40,%al ; DS:20=al  ; INNER loop, 64 iterations  (64 x 64 = 4096 = one whole grid)
#   74E7  call L_005417 ; and $0x3f ; DS:23 = al                 ; a RANDOM byte, masked to 0..63 (see header: RNG, not a cell x)
#   74EF  call L_005417 ; and $0x3f ; DS:24 = al                 ; a RANDOM byte, masked to 0..63 (the fade paints random cells)
#   74F7  call L_0052b3
#   74FA  mov $0x74,%al ; bx=[DS:06] ; [bx]=al                   ; write 0x74 through the grid ptr
#   7502  decb DS:20 ; jne 74E7
#   7508  call L_005cb1 ; mov $0x0e,%bx ; call L_000911          ; delay
#   7511  decb DS:1f ; jne 74DF
#         The fill value 0x74 is art 0x74>>2 == 0x1D, which disasm/tile-art.tsv records as
#         SOLID BLACK. So the ending repaints a whole 4096-cell sector black, and the filename
#         a few instructions later says which one: "MAPX30  " -- sector 30, the 1990 A.D.
#         wilderness. "ALL HER WORKS SHALL DIE!" is not flavour text, it is a description of
#         the map write. Grade: the loop bounds and the fill byte are measured from the bytes;
#         "which sector" is measured from the embedded filename; the reading of L_005417 as
#         cell-addressing was refuted the same day (it is the RNG, above), and the reading of
#         L_0061a6 as file I/O is now measured from the caller's AH=27h, not inferred.
#   7517  call str_out              ; "\rYOU FEEL A STRANGE FORCE!"
#   7535  call L_0061a6
#   7538  mov $0x27,%ah             ; INT 21h AH=27h = FCB RANDOM BLOCK READ (not a delay;
#                                   ;   an earlier reading of this line as a delay is wrong)
#         mov $0x1000,%cx           ; 4096 records = one 64x64 sector grid
#         mov [0x0472],%dx          ; DS:DX = the FCB, per INT 21h AH=27h's calling contract
#         call L_0054ca             ; opens the name that follows inline
#   7544  db "MAPX30  "             ; an FCB-style padded filename: sector 30, Earth 1990 A.D.
#   754C  call L_0061a6             ; performs the DOS call with AH as set above: the READ
#   754F  call str_out              ; "\r\rYOU HAVE SAVED THE UNIVERSE,\rAND COMPLETED ULTIMA ][!"
#                                   ; " SEEK\rNOW TO CONQUER WICKED EXODUS,\r"
#   75AE  call str_out              ; "FOUND IN ULTIMA ]I[-D ]II[-P!"
#   75CF  mov $0xff,%al ; DS:12=al  ; DS:12 = 0xFF (the site/mode byte; 0xFF is a sentinel)
#   75D4  mov $0x28,%al ; DS:13=al  ; DS:13 = 0x28, which is the ROCKET vehicle code (asm/2FAD)
#   75D9  mov $0x00,%al ; DS:00=al ; DS:01=al   ; player x = 0, y = 0
#   75E1.. loop: al = (DS:01 + 1) & 0x3f -> DS:01, and likewise DS:00, guarded by jb, with a
#           delay each step and `jmp -0x2a` back to 75E1. An ending animation, not a return.
#         The `]` sequences are the string emitter's alternate-glyph escapes and are left
#         verbatim here rather than guessed at; decode them with tools/decode_str.py before
#         quoting the win text in prose.
# func 72A0..8386  len 10E7 B  evidence: call+reko
# calls: L_000911, L_0051df (str_out), L_0052b3, L_005417, L_0054ca, L_005cb1, L_0060d7, L_0061a6 (+1 more)
# confidence: low (bytes proven by check_func.py)
# bulk wave: L_<addr> labels kept as cross-reference anchors (CP3)
sub_72a0:
L_0074a0:
        db 0x90  # split: branch target inside data
L_0074a1:
        db 0xc6,0x06,0x31,0x00,0x28  # split: branch target inside data
L_0074a6:
        call L_0051df
        db 0x8d,0x8d,0x20,0x20,0x20,0x20,0x20,0x4d,0x49,0x4e,0x41,0x58,0x20,0x49,0x53,0x20,0x44,0x45,0x41,0x44,0x21,0x21,0x8d,0x41,0x4c,0x4c,0x20,0x48,0x45,0x52,0x20,0x57,0x4f,0x52,0x4b,0x53,0x20,0x53,0x48,0x41,0x4c,0x4c,0x20,0x44,0x49,0x45,0x21,0x8d,0x00  # split: branch target inside data
L_0074da:
        db 0xb0,0x40  # split: branch target inside data
L_0074dc:
        db 0xa2,0x1f,0x00  # split: branch target inside data
L_0074df:
        call L_0060d7
L_0074e2:
        db 0xb0,0x40  # split: branch target inside data
L_0074e4:
        db 0xa2,0x20,0x00  # split: branch target inside data
L_0074e7:
        call L_005417
L_0074ea:
        db 0x24,0x3f  # split: branch target inside data
L_0074ec:
        db 0xa2,0x23,0x00  # split: branch target inside data
L_0074ef:
        call L_005417
L_0074f2:
        db 0x24,0x3f  # split: branch target inside data
L_0074f4:
        db 0xa2,0x24,0x00  # split: branch target inside data
L_0074f7:
        call L_0052b3
L_0074fa:
        db 0xb0,0x74  # split: branch target inside data
L_0074fc:
        db 0x8b,0x1e,0x06,0x00  # split: branch target inside data
L_007500:
        db 0x88,0x00  # split: branch target inside data
L_007502:
        decb   0x20
L_007506:
        jne L_0074e7
L_007508:
        call L_005cb1
L_00750b:
        db 0xbb,0x0e,0x00  # split: branch target inside data
L_00750e:
        call L_000911
L_007511:
        decb   0x1f
L_007515:
        jne L_0074df
L_007517:
        call L_0051df
        db 0x8d,0x59,0x4f,0x55,0x20,0x46,0x45,0x45,0x4c,0x20,0x41,0x20,0x53,0x54,0x52,0x41,0x4e,0x47,0x45,0x20,0x46,0x4f,0x52,0x43,0x45,0x21,0x00  # split: branch target inside data
L_007535:
        call L_0061a6
L_007538:
        db 0xb4,0x27  # split: branch target inside data
L_00753a:
        db 0xb9,0x00,0x10  # split: branch target inside data
L_00753d:
        db 0x8b,0x16,0x72,0x04  # split: branch target inside data
L_007541:
        call L_0054ca
L_007544:
        db 0x4d  # split: branch target inside data
L_007545:
        db 0x41  # split: branch target inside data
L_007546:
        db 0x50  # split: branch target inside data
L_007547:
        db 0x58  # split: branch target inside data
L_007548:
        db 0x33,0x30  # split: branch target inside data
L_00754a:
        db 0x20,0x20  # split: branch target inside data
L_00754c:
        call L_0061a6
L_00754f:
        call L_0051df
        db 0x8d,0x8d,0x59,0x4f,0x55,0x20,0x48,0x41,0x56,0x45,0x20,0x53,0x41,0x56,0x45,0x44,0x20,0x54,0x48,0x45,0x20,0x55,0x4e,0x49,0x56,0x45,0x52,0x53,0x45,0x2c,0x8d,0x41,0x4e,0x44,0x20,0x43,0x4f,0x4d,0x50,0x4c,0x45,0x54,0x45,0x44,0x20,0x55,0x4c,0x54,0x49,0x4d,0x41,0x20,0x5d,0x5b,0x21,0x20,0x53,0x45,0x45,0x4b,0x8d,0x4e,0x4f,0x57,0x20,0x54,0x4f,0x20,0x43,0x4f,0x4e,0x51,0x55,0x45,0x52,0x20,0x57,0x49,0x43,0x4b,0x45,0x44,0x20,0x45,0x58,0x4f,0x44,0x55,0x53,0x2c,0x8d,0x00  # split: branch target inside data
L_0075ae:
        call L_0051df
        db 0x46,0x4f,0x55,0x4e,0x44,0x20,0x49,0x4e,0x20,0x55,0x4c,0x54,0x49,0x4d,0x41,0x20,0x5d,0x49,0x5b,0x2d,0x44,0x20,0x5d,0x49,0x49,0x5b,0x2d,0x50,0x21,0x00  # split: branch target inside data
L_0075cf:
        db 0xb0,0xff  # split: branch target inside data
L_0075d1:
        db 0xa2,0x12,0x00  # split: branch target inside data
L_0075d4:
        db 0xb0,0x28  # split: branch target inside data
L_0075d6:
        db 0xa2,0x13,0x00  # split: branch target inside data
L_0075d9:
        db 0xb0,0x00  # split: branch target inside data
L_0075db:
        db 0xa2,0x00,0x00  # split: branch target inside data
L_0075de:
        db 0xa2,0x01,0x00  # split: branch target inside data
L_0075e1:
        db 0xf8  # split: branch target inside data
L_0075e2:
        db 0xa0,0x01,0x00  # split: branch target inside data
L_0075e5:
        db 0x14,0x01  # split: branch target inside data
L_0075e7:
        db 0x24,0x3f  # split: branch target inside data
L_0075e9:
        db 0xa2,0x01,0x00  # split: branch target inside data
L_0075ec:
        db 0xf8  # split: branch target inside data
L_0075ed:
        db 0xd0,0xd8  # split: branch target inside data
L_0075ef:
        jb L_007600
L_0075f1:
        db 0xf8  # split: branch target inside data
L_0075f2:
        db 0xd0,0xd8  # split: branch target inside data
L_0075f4:
        jb L_007600
L_0075f6:
        db 0xa0,0x00,0x00  # split: branch target inside data
L_0075f9:
        db 0x14,0x01  # split: branch target inside data
L_0075fb:
        db 0x24,0x3f  # split: branch target inside data
L_0075fd:
        db 0xa2,0x00,0x00  # split: branch target inside data
L_007600:
        call L_005cb1
L_007603:
        db 0xbb,0x06,0x00  # split: branch target inside data
L_007606:
        call L_000911
L_007609:
        jmp L_0075e1
        db 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00  # split: branch target inside data
L_00784b:
        db 0x00,0x00  # split: branch target inside data
L_00784d:
        db 0x00,0x0f  # split: branch target inside data
        db 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00  # split: branch target inside data
L_00787b:
        db 0x00,0x00  # split: branch target inside data
L_00787d:
        db 0x00,0x0e,0x19,0x05  # split: branch target inside data
L_007881:
        db 0x17  # split: branch target inside data
        db 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00  # split: branch target inside data
L_007a82:
        db 0x00,0x18  # split: branch target inside data
L_007a84:
        db 0x00,0x29  # split: branch target inside data
L_007a86:
        db 0x00,0x28  # split: branch target inside data
        db 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00  # split: branch target inside data
L_007acc:
        db 0x00,0x00  # split: branch target inside data
L_007ace:
        db 0x00,0x10  # split: branch target inside data
L_007ad0:
        db 0x00,0x10  # split: branch target inside data
        db 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00  # split: branch target inside data
L_007b62:
        db 0x00,0x00  # split: branch target inside data
L_007b64:
        db 0x8a,0x06,0x00,0x00  # split: branch target inside data
L_007b68:
        db 0x06  # split: branch target inside data
L_007b69:
        db 0x00,0xde  # split: branch target inside data
L_007b6b:
        db 0x02,0x00  # split: branch target inside data
L_007b6d:
        db 0x20,0x00  # split: branch target inside data
L_007b6f:
        db 0x05,0x03,0x00  # split: branch target inside data
L_007b72:
        db 0x03,0x00  # split: branch target inside data
L_007b74:
        db 0x03,0x00  # split: branch target inside data
L_007b76:
        db 0x00,0x05  # split: branch target inside data
L_007b78:
        db 0x0a,0x00  # split: branch target inside data
L_007b7a:
        db 0x00,0x10  # split: branch target inside data
L_007b7c:
        loopne L_007b7f
L_007b7e:
        db 0x40  # split: branch target inside data
L_007b7f:
        db 0x01,0x40,0x02  # split: branch target inside data
L_007b82:
        db 0x00,0x02  # split: branch target inside data
L_007b84:
        db 0x00,0x08  # split: branch target inside data
L_007b86:
        db 0x01,0x00  # split: branch target inside data
        db 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00  # split: branch target inside data
L_007bc0:
        db 0x30,0x06,0x72,0x06  # split: branch target inside data
L_007bc4:
        db 0xb4,0x06  # split: branch target inside data
L_007bc6:
        db 0xf6,0x06,0x38,0x07,0x7a  # split: branch target inside data
L_007bcb:
        db 0x07  # split: branch target inside data
L_007bcc:
        db 0xbc,0x07,0xfe  # split: branch target inside data
L_007bcf:
        db 0x07  # split: branch target inside data
L_007bd0:
        db 0x40  # split: branch target inside data
L_007bd1:
        db 0x08,0x82,0x08,0xc4  # split: branch target inside data
L_007bd5:
        db 0x08,0x06,0x09,0x48  # split: branch target inside data
L_007bd9:
        db 0x09,0x8a,0x09,0xcc  # split: branch target inside data
L_007bdd:
        db 0x09,0x0e,0x0a,0x50  # split: branch target inside data
L_007be1:
        db 0x0a,0x92,0x0a,0xd4  # split: branch target inside data
L_007be5:
        db 0x0a,0x16,0x0b,0x58  # split: branch target inside data
L_007be9:
        db 0x0b,0x9a,0x0b,0xdc  # split: branch target inside data
L_007bed:
        db 0x0b,0x1e,0x0c,0x60  # split: branch target inside data
L_007bf1:
        db 0x0c,0xa2  # split: branch target inside data
L_007bf3:
        db 0x0c,0xe4  # split: branch target inside data
L_007bf5:
        db 0x0c,0x26  # split: branch target inside data
L_007bf7:
        db 0x0d,0x68,0x0d  # split: branch target inside data
L_007bfa:
        db 0xaa  # split: branch target inside data
L_007bfb:
        db 0x0d,0xec,0x0d  # split: branch target inside data
L_007bfe:
        db 0x2e,0x0e  # split: branch target inside data
L_007c00:
        jo L_007c10
L_007c02:
        db 0xb2,0x0e  # split: branch target inside data
L_007c04:
        db 0xf4  # split: branch target inside data
L_007c05:
        db 0x0e  # split: branch target inside data
L_007c06:
        db 0x36,0x0f,0x78,0x0f  # split: branch target inside data
L_007c0a:
        db 0xba,0x0f,0xfc  # split: branch target inside data
L_007c0d:
        db 0x0f,0x3e  # split: branch target inside data
L_007c0f:
        db 0x10  # split: branch target inside data
L_007c10:
        db 0x80,0x10,0xc2  # split: branch target inside data
L_007c13:
        db 0x10,0x04  # split: branch target inside data
L_007c15:
        db 0x11,0x46,0x11  # split: branch target inside data
L_007c18:
        db 0x88,0x11  # split: branch target inside data
L_007c1a:
        db 0xca,0x11,0x0c  # split: branch target inside data
L_007c1d:
        db 0x12,0x4e,0x12  # split: branch target inside data
L_007c20:
        db 0x90  # split: branch target inside data
L_007c21:
        db 0x12,0xd2  # split: branch target inside data
L_007c23:
        db 0x12,0x14  # split: branch target inside data
L_007c25:
        db 0x13,0x56,0x13  # split: branch target inside data
L_007c28:
        db 0x98  # split: branch target inside data
L_007c29:
        db 0x13,0xda  # split: branch target inside data
L_007c2b:
        db 0x13,0x1c  # split: branch target inside data
L_007c2d:
        db 0x14,0x5e  # split: branch target inside data
L_007c2f:
        db 0x14,0xa0  # split: branch target inside data
L_007c31:
        db 0x14,0xe2  # split: branch target inside data
L_007c33:
        db 0x14,0x24  # split: branch target inside data
L_007c35:
        db 0x15,0x66,0x15  # split: branch target inside data
L_007c38:
        db 0xa8,0x15  # split: branch target inside data
L_007c3a:
        db 0xea,0x15,0x2c,0x16,0x6e  # split: branch target inside data
L_007c3f:
        db 0x16  # split: branch target inside data
L_007c40:
        db 0x04,0x10  # split: branch target inside data
        db 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00  # split: branch target inside data
L_007c4a:
        db 0x22,0x00  # split: branch target inside data
L_007c4c:
        db 0x20,0x02  # split: branch target inside data
L_007c4e:
        db 0x00,0x22  # split: branch target inside data
L_007c50:
        db 0x02,0x20  # split: branch target inside data
        db 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00  # split: branch target inside data
L_007c5a:
        db 0x00,0x20  # split: branch target inside data
L_007c5c:
        db 0x02,0x22  # split: branch target inside data
L_007c5e:
        db 0x22,0x02  # split: branch target inside data
L_007c60:
        db 0x20,0x00  # split: branch target inside data
        db 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00  # split: branch target inside data
L_007c6a:
        db 0x20,0x02  # split: branch target inside data
L_007c6c:
        db 0x22,0x00  # split: branch target inside data
L_007c6e:
        db 0x02,0x20  # split: branch target inside data
L_007c70:
        db 0x00,0x22  # split: branch target inside data
        db 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00  # split: branch target inside data
L_007c7a:
        db 0x00,0x22  # split: branch target inside data
L_007c7c:
        db 0x02,0x20  # split: branch target inside data
L_007c7e:
        db 0x22,0x00  # split: branch target inside data
L_007c80:
        db 0x20,0x02  # split: branch target inside data
L_007c82:
        db 0x04,0x10  # split: branch target inside data
L_007c84:
        db 0x00,0x11  # split: branch target inside data
L_007c86:
        db 0x00,0x00  # split: branch target inside data
L_007c88:
        db 0x00,0x00  # split: branch target inside data
L_007c8a:
        db 0x00,0x00  # split: branch target inside data
L_007c8c:
        db 0x03,0x00  # split: branch target inside data
L_007c8e:
        db 0x01,0x10  # split: branch target inside data
L_007c90:
        db 0x00,0x00  # split: branch target inside data
L_007c92:
        db 0x00,0x00  # split: branch target inside data
L_007c94:
        db 0x01,0x10  # split: branch target inside data
L_007c96:
        db 0x00,0x30  # split: branch target inside data
L_007c98:
        db 0x00,0x00  # split: branch target inside data
L_007c9a:
        db 0x00,0x00  # split: branch target inside data
L_007c9c:
        db 0x00,0x11  # split: branch target inside data
L_007c9e:
        db 0x00,0x03  # split: branch target inside data
L_007ca0:
        db 0x00,0x00  # split: branch target inside data
L_007ca2:
        db 0x00,0x00  # split: branch target inside data
L_007ca4:
        db 0x00,0x03  # split: branch target inside data
L_007ca6:
        db 0x00,0x11  # split: branch target inside data
L_007ca8:
        db 0x00,0x00  # split: branch target inside data
L_007caa:
        db 0x00,0x00  # split: branch target inside data
L_007cac:
        db 0x01,0x10  # split: branch target inside data
L_007cae:
        db 0x00,0x30  # split: branch target inside data
L_007cb0:
        db 0x00,0x00  # split: branch target inside data
L_007cb2:
        db 0x00,0x00  # split: branch target inside data
L_007cb4:
        db 0x03,0x00  # split: branch target inside data
L_007cb6:
        db 0x11,0x00  # split: branch target inside data
L_007cb8:
        db 0x00,0x00  # split: branch target inside data
L_007cba:
        db 0x00,0x00  # split: branch target inside data
L_007cbc:
        db 0x11,0x00  # split: branch target inside data
L_007cbe:
        db 0x30,0x00  # split: branch target inside data
L_007cc0:
        db 0x00,0x00  # split: branch target inside data
L_007cc2:
        db 0x00,0x00  # split: branch target inside data
L_007cc4:
        db 0x04,0x10  # split: branch target inside data
L_007cc6:
        db 0x00,0x01  # split: branch target inside data
L_007cc8:
        db 0x00,0x00  # split: branch target inside data
L_007cca:
        db 0x00,0x00  # split: branch target inside data
L_007ccc:
        db 0x01,0x00  # split: branch target inside data
L_007cce:
        db 0x01,0x00  # split: branch target inside data
        db 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00  # split: branch target inside data
L_007cd8:
        db 0x00,0x01  # split: branch target inside data
L_007cda:
        db 0x00,0x10  # split: branch target inside data
        db 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00  # split: branch target inside data
L_007ce4:
        db 0x00,0x10  # split: branch target inside data
L_007ce6:
        db 0x00,0x01  # split: branch target inside data
L_007ce8:
        db 0x00,0x00  # split: branch target inside data
L_007cea:
        db 0x00,0x00  # split: branch target inside data
L_007cec:
        db 0x00,0x00  # split: branch target inside data
L_007cee:
        db 0x01,0x00  # split: branch target inside data
L_007cf0:
        db 0x00,0x00  # split: branch target inside data
L_007cf2:
        db 0x00,0x00  # split: branch target inside data
L_007cf4:
        db 0x01,0x00  # split: branch target inside data
L_007cf6:
        db 0x00,0x00  # split: branch target inside data
L_007cf8:
        db 0x00,0x00  # split: branch target inside data
L_007cfa:
        db 0x00,0x00  # split: branch target inside data
L_007cfc:
        db 0x00,0x10  # split: branch target inside data
L_007cfe:
        db 0x00,0x10  # split: branch target inside data
L_007d00:
        db 0x00,0x00  # split: branch target inside data
L_007d02:
        db 0x00,0x00  # split: branch target inside data
L_007d04:
        db 0x00,0x00  # split: branch target inside data
L_007d06:
        db 0x04,0x10  # split: branch target inside data
L_007d08:
        db 0x01,0x50,0x00  # split: branch target inside data
L_007d0b:
        db 0x00,0x15  # split: branch target inside data
L_007d0d:
        db 0x55  # split: branch target inside data
L_007d0e:
        db 0x00,0x00  # split: branch target inside data
L_007d10:
        db 0x15,0x55,0x00  # split: branch target inside data
L_007d13:
        db 0x00,0x15  # split: branch target inside data
L_007d15:
        db 0x55  # split: branch target inside data
L_007d16:
        db 0x00,0x00  # split: branch target inside data
L_007d18:
        db 0x01,0x50,0x00  # split: branch target inside data
L_007d1b:
        db 0x00,0x00  # split: branch target inside data
L_007d1d:
        db 0x00,0x01  # split: branch target inside data
L_007d1f:
        db 0x50  # split: branch target inside data
L_007d20:
        db 0x00,0x00  # split: branch target inside data
L_007d22:
        db 0x15,0x55,0x00  # split: branch target inside data
L_007d25:
        db 0x00,0x15  # split: branch target inside data
L_007d27:
        db 0x55  # split: branch target inside data
L_007d28:
        db 0x00,0x00  # split: branch target inside data
L_007d2a:
        db 0x15,0x55,0x00  # split: branch target inside data
L_007d2d:
        db 0x00,0x01  # split: branch target inside data
L_007d2f:
        db 0x50  # split: branch target inside data
L_007d30:
        db 0x01,0x50,0x00  # split: branch target inside data
L_007d33:
        db 0x00,0x15  # split: branch target inside data
L_007d35:
        db 0x55  # split: branch target inside data
L_007d36:
        db 0x00,0x00  # split: branch target inside data
L_007d38:
        db 0x15,0x55,0x00  # split: branch target inside data
L_007d3b:
        db 0x00,0x15  # split: branch target inside data
L_007d3d:
        db 0x55  # split: branch target inside data
L_007d3e:
        db 0x00,0x00  # split: branch target inside data
L_007d40:
        db 0x01,0x50,0x00  # split: branch target inside data
L_007d43:
        db 0x00,0x00  # split: branch target inside data
L_007d45:
        db 0x00,0x00  # split: branch target inside data
L_007d47:
        db 0x00,0x04  # split: branch target inside data
L_007d49:
        db 0x10,0xc0  # split: branch target inside data
L_007d4b:
        db 0xc0,0x0c,0x00  # split: branch target inside data
L_007d4e:
        db 0x33,0x30  # split: branch target inside data
L_007d50:
        db 0x33,0x03  # split: branch target inside data
L_007d52:
        db 0x0c,0x0c  # split: branch target inside data
L_007d54:
        db 0xc0,0xcc,0x30  # split: branch target inside data
L_007d57:
        db 0x03,0x00  # split: branch target inside data
L_007d59:
        db 0x30,0xc0  # split: branch target inside data
L_007d5b:
        db 0x0c,0x00  # split: branch target inside data
L_007d5d:
        db 0x0c,0x00  # split: branch target inside data
L_007d5f:
        db 0x30,0x00  # split: branch target inside data
L_007d61:
        db 0x03,0xc0  # split: branch target inside data
L_007d63:
        db 0xc0,0xc0,0x0c  # split: branch target inside data
L_007d66:
        db 0x33,0x03  # split: branch target inside data
L_007d68:
        db 0x30,0x30  # split: branch target inside data
L_007d6a:
        db 0x0c,0x0c  # split: branch target inside data
L_007d6c:
        db 0x0c,0xc0  # split: branch target inside data
L_007d6e:
        db 0x03,0x30  # split: branch target inside data
L_007d70:
        db 0x03,0x03  # split: branch target inside data
L_007d72:
        db 0xc0,0xc0,0x00  # split: branch target inside data
L_007d75:
        db 0xcc  # split: branch target inside data
L_007d76:
        db 0x33,0x00  # split: branch target inside data
L_007d78:
        db 0x00,0x30  # split: branch target inside data
L_007d7a:
        db 0x0c,0x00  # split: branch target inside data
L_007d7c:
        db 0x00,0xc0  # split: branch target inside data
L_007d7e:
        db 0x03,0x03  # split: branch target inside data
L_007d80:
        db 0x03,0x00  # split: branch target inside data
L_007d82:
        db 0x00,0xcc  # split: branch target inside data
L_007d84:
        db 0xcc  # split: branch target inside data
L_007d85:
        db 0x00,0x00  # split: branch target inside data
L_007d87:
        db 0x30,0x30  # split: branch target inside data
L_007d89:
        db 0x00,0x04  # split: branch target inside data
L_007d8b:
        db 0x10,0x00  # split: branch target inside data
L_007d8d:
        db 0x00,0x00  # split: branch target inside data
L_007d8f:
        db 0x00,0x03  # split: branch target inside data
L_007d91:
        db 0xc0,0x00,0x00  # split: branch target inside data
L_007d94:
        db 0x0c,0x30  # split: branch target inside data
L_007d96:
        db 0x0f,0x00,0x0c  # split: branch target inside data
L_007d99:
        db 0x30,0x30  # split: branch target inside data
L_007d9b:
        db 0xc0,0x03,0xc0  # split: branch target inside data
L_007d9e:
        db 0x30,0xc0  # split: branch target inside data
L_007da0:
        db 0x00,0x00  # split: branch target inside data
L_007da2:
        db 0x0f,0x00,0x00  # split: branch target inside data
L_007da5:
        db 0x0f,0x00,0x00  # split: branch target inside data
L_007da8:
        db 0x00,0x30  # split: branch target inside data
L_007daa:
        db 0xc0,0x00,0x00  # split: branch target inside data
L_007dad:
        db 0x30,0xc0  # split: branch target inside data
L_007daf:
        db 0x00,0x00  # split: branch target inside data
L_007db1:
        db 0x0f,0x00,0x00  # split: branch target inside data
L_007db4:
        db 0x00,0x00  # split: branch target inside data
L_007db6:
        db 0x03,0xc0  # split: branch target inside data
L_007db8:
        db 0x03,0xc0  # split: branch target inside data
L_007dba:
        db 0x0c,0x30  # split: branch target inside data
L_007dbc:
        db 0x0c,0x30  # split: branch target inside data
L_007dbe:
        db 0x0c,0x30  # split: branch target inside data
L_007dc0:
        db 0x0c,0x30  # split: branch target inside data
L_007dc2:
        db 0x03,0xc0  # split: branch target inside data
L_007dc4:
        db 0x03,0xc0  # split: branch target inside data
L_007dc6:
        db 0x00,0x00  # split: branch target inside data
L_007dc8:
        db 0x00,0x00  # split: branch target inside data
L_007dca:
        db 0x00,0x00  # split: branch target inside data
L_007dcc:
        db 0x04,0x10  # split: branch target inside data
        db 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00  # split: branch target inside data
L_007dd6:
        db 0x0f,0xfc,0x3f  # split: branch target inside data
L_007dd9:
        db 0xf0,0x0c,0x0c  # split: branch target inside data
L_007ddc:
        db 0x30,0x30  # split: branch target inside data
L_007dde:
        db 0x0c,0x0c  # split: branch target inside data
L_007de0:
        db 0x30,0x30  # split: branch target inside data
L_007de2:
        db 0x0c,0x0c  # split: branch target inside data
L_007de4:
        db 0x30,0x30  # split: branch target inside data
L_007de6:
        db 0x0f,0xff,0xff  # split: branch target inside data
L_007de9:
        db 0xf0,0x00,0x0c  # split: branch target inside data
L_007dec:
        db 0x30,0x00  # split: branch target inside data
L_007dee:
        db 0x00,0x0c  # split: branch target inside data
L_007df0:
        db 0x30,0x00  # split: branch target inside data
L_007df2:
        db 0x0f,0xff,0xff  # split: branch target inside data
L_007df5:
        db 0xf0,0x0c,0x0c  # split: branch target inside data
L_007df8:
        db 0x30,0x30  # split: branch target inside data
L_007dfa:
        db 0x0c,0x0c  # split: branch target inside data
L_007dfc:
        db 0x30,0x30  # split: branch target inside data
L_007dfe:
        db 0x0c,0x0c  # split: branch target inside data
L_007e00:
        db 0x30,0x30  # split: branch target inside data
L_007e02:
        db 0x0f,0xfc,0x3f  # split: branch target inside data
L_007e05:
        db 0xf0,0x00,0x00  # split: branch target inside data
L_007e08:
        db 0x00,0x00  # split: branch target inside data
L_007e0a:
        db 0x00,0x00  # split: branch target inside data
L_007e0c:
        db 0x00,0x00  # split: branch target inside data
L_007e0e:
        db 0x04,0x10  # split: branch target inside data
L_007e10:
        db 0x00,0x00  # split: branch target inside data
L_007e12:
        db 0x00,0x00  # split: branch target inside data
L_007e14:
        db 0x00,0x3c  # split: branch target inside data
L_007e16:
        db 0x3c,0x00  # split: branch target inside data
L_007e18:
        db 0x00,0x3f  # split: branch target inside data
L_007e1a:
        db 0xfc  # split: branch target inside data
L_007e1b:
        db 0x00,0x00  # split: branch target inside data
L_007e1d:
        db 0x3f  # split: branch target inside data
L_007e1e:
        db 0xfc  # split: branch target inside data
L_007e1f:
        db 0x00,0x00  # split: branch target inside data
L_007e21:
        db 0x0f,0xf0  # split: branch target inside data
L_007e23:
        db 0x00,0x00  # split: branch target inside data
L_007e25:
        db 0x0f,0xf0  # split: branch target inside data
L_007e27:
        db 0x00,0x00  # split: branch target inside data
L_007e29:
        db 0x0f,0xf0  # split: branch target inside data
L_007e2b:
        db 0x00,0x00  # split: branch target inside data
L_007e2d:
        db 0x0f,0xf0  # split: branch target inside data
L_007e2f:
        db 0x00,0x00  # split: branch target inside data
L_007e31:
        db 0x3f  # split: branch target inside data
L_007e32:
        db 0xfc  # split: branch target inside data
L_007e33:
        db 0x00,0x00  # split: branch target inside data
L_007e35:
        db 0x3f  # split: branch target inside data
L_007e36:
        db 0xfc  # split: branch target inside data
L_007e37:
        db 0x00,0x00  # split: branch target inside data
L_007e39:
        db 0x3f  # split: branch target inside data
L_007e3a:
        db 0xfc  # split: branch target inside data
L_007e3b:
        db 0x00,0x00  # split: branch target inside data
L_007e3d:
        db 0x3f  # split: branch target inside data
L_007e3e:
        db 0xfc  # split: branch target inside data
L_007e3f:
        db 0x00,0x00  # split: branch target inside data
L_007e41:
        db 0xff  # split: branch target inside data
L_007e42:
        db 0xff,0x00  # split: branch target inside data
L_007e44:
        db 0x00,0xff  # split: branch target inside data
L_007e46:
        db 0xff,0x00  # split: branch target inside data
L_007e48:
        db 0x00,0xff  # split: branch target inside data
L_007e4a:
        db 0xff,0x00  # split: branch target inside data
L_007e4c:
        db 0x00,0xff  # split: branch target inside data
L_007e4e:
        db 0xff,0x00  # split: branch target inside data
L_007e50:
        db 0x04,0x10  # split: branch target inside data
        db 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00  # split: branch target inside data
L_007e5a:
        db 0x0f,0x00,0x00  # split: branch target inside data
L_007e5d:
        db 0xf0,0x0f,0xff,0xff  # split: branch target inside data
L_007e61:
        db 0xf0,0x03,0xff  # split: branch target inside data
L_007e64:
        db 0xff,0xc0  # split: branch target inside data
L_007e66:
        db 0x03,0xc0  # split: branch target inside data
L_007e68:
        db 0x03,0xc0  # split: branch target inside data
L_007e6a:
        db 0x03,0xc0  # split: branch target inside data
L_007e6c:
        db 0x03,0xc0  # split: branch target inside data
L_007e6e:
        db 0x03,0xc0  # split: branch target inside data
L_007e70:
        db 0x03,0xc0  # split: branch target inside data
L_007e72:
        db 0x03,0xc0  # split: branch target inside data
L_007e74:
        db 0x03,0xc0  # split: branch target inside data
L_007e76:
        db 0x03,0xc0  # split: branch target inside data
L_007e78:
        db 0x03,0xc0  # split: branch target inside data
L_007e7a:
        db 0x03,0xc0  # split: branch target inside data
L_007e7c:
        db 0x03,0xc0  # split: branch target inside data
L_007e7e:
        db 0x03,0xfc  # split: branch target inside data
L_007e80:
        db 0x3f  # split: branch target inside data
L_007e81:
        db 0xc0,0x0f,0xfc  # split: branch target inside data
L_007e84:
        db 0x3f  # split: branch target inside data
L_007e85:
        db 0xf0,0x0f,0x00,0x00  # split: branch target inside data
L_007e89:
        db 0xf0,0x00,0x00  # split: branch target inside data
L_007e8c:
        db 0x00,0x00  # split: branch target inside data
L_007e8e:
        db 0x00,0x00  # split: branch target inside data
L_007e90:
        db 0x00,0x00  # split: branch target inside data
L_007e92:
        db 0x04,0x10  # split: branch target inside data
L_007e94:
        db 0xc0,0xc0,0x0c  # split: branch target inside data
L_007e97:
        db 0x00,0x33  # split: branch target inside data
L_007e99:
        db 0x30,0x33  # split: branch target inside data
L_007e9b:
        db 0x03,0x0c  # split: branch target inside data
L_007e9d:
        db 0x0c,0xc0  # split: branch target inside data
L_007e9f:
        db 0xcc  # split: branch target inside data
L_007ea0:
        db 0x30,0x03  # split: branch target inside data
L_007ea2:
        db 0x00,0x30  # split: branch target inside data
L_007ea4:
        db 0xc0,0x0c,0x00  # split: branch target inside data
L_007ea7:
        db 0x0c,0x00  # split: branch target inside data
L_007ea9:
        db 0x30,0x00  # split: branch target inside data
L_007eab:
        db 0x03,0xc0  # split: branch target inside data
L_007ead:
        db 0xc0,0xc0,0x0c  # split: branch target inside data
L_007eb0:
        db 0x33,0x03  # split: branch target inside data
L_007eb2:
        db 0x30,0x30  # split: branch target inside data
L_007eb4:
        db 0x0c,0x00  # split: branch target inside data
L_007eb6:
        db 0x00,0xc0  # split: branch target inside data
L_007eb8:
        db 0x03,0x0f  # split: branch target inside data
L_007eba:
        db 0xf0,0x03,0xc0  # split: branch target inside data
L_007ebd:
        db 0x3c,0x3c  # split: branch target inside data
L_007ebf:
        db 0x0c,0x30  # split: branch target inside data
L_007ec1:
        db 0x30,0x0c  # split: branch target inside data
L_007ec3:
        db 0x30,0x0c  # split: branch target inside data
L_007ec5:
        db 0x30,0x0c  # split: branch target inside data
L_007ec7:
        db 0xc0,0x03,0x30  # split: branch target inside data
L_007eca:
        db 0x0c,0x03  # split: branch target inside data
L_007ecc:
        db 0xc0,0x30,0x0c  # split: branch target inside data
L_007ecf:
        db 0x0c,0x30  # split: branch target inside data
L_007ed1:
        db 0x30,0x0c  # split: branch target inside data
L_007ed3:
        db 0x30,0x04  # split: branch target inside data
L_007ed5:
        db 0x10,0x00  # split: branch target inside data
L_007ed7:
        db 0x00,0x00  # split: branch target inside data
L_007ed9:
        db 0x00,0x00  # split: branch target inside data
L_007edb:
        db 0x03,0xc0  # split: branch target inside data
L_007edd:
        db 0x00,0x00  # split: branch target inside data
L_007edf:
        db 0x03,0xc0  # split: branch target inside data
L_007ee1:
        db 0x00,0x03  # split: branch target inside data
L_007ee3:
        db 0xff  # split: branch target inside data
L_007ee4:
        db 0xff,0xc0  # split: branch target inside data
L_007ee6:
        db 0x03,0xff  # split: branch target inside data
L_007ee8:
        db 0xff,0xc0  # split: branch target inside data
L_007eea:
        db 0x03,0xff  # split: branch target inside data
L_007eec:
        db 0xff,0xc0  # split: branch target inside data
L_007eee:
        db 0x03,0xff  # split: branch target inside data
L_007ef0:
        db 0xff,0xc0  # split: branch target inside data
L_007ef2:
        db 0x03,0xff  # split: branch target inside data
L_007ef4:
        db 0xff,0xc0  # split: branch target inside data
L_007ef6:
        db 0x00,0x03  # split: branch target inside data
L_007ef8:
        db 0xc0,0x00,0x00  # split: branch target inside data
L_007efb:
        db 0x03,0xc0  # split: branch target inside data
L_007efd:
        db 0x00,0x00  # split: branch target inside data
L_007eff:
        db 0x03,0xc0  # split: branch target inside data
L_007f01:
        db 0x00,0x00  # split: branch target inside data
L_007f03:
        db 0x03,0xc0  # split: branch target inside data
L_007f05:
        db 0x00,0x00  # split: branch target inside data
L_007f07:
        db 0x03,0xc0  # split: branch target inside data
L_007f09:
        db 0x00,0x00  # split: branch target inside data
L_007f0b:
        db 0xff  # split: branch target inside data
L_007f0c:
        db 0xff,0x00  # split: branch target inside data
L_007f0e:
        db 0x0f,0xff,0xff  # split: branch target inside data
L_007f11:
        db 0xf0,0x00,0x00  # split: branch target inside data
L_007f14:
        db 0x00,0x00  # split: branch target inside data
L_007f16:
        db 0x04,0x10  # split: branch target inside data
        db 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00  # split: branch target inside data
L_007f20:
        db 0x22,0x00  # split: branch target inside data
L_007f22:
        db 0x20,0x02  # split: branch target inside data
L_007f24:
        db 0x00,0x22  # split: branch target inside data
L_007f26:
        db 0x02,0x20  # split: branch target inside data
L_007f28:
        db 0x00,0x00  # split: branch target inside data
L_007f2a:
        db 0x00,0x00  # split: branch target inside data
L_007f2c:
        db 0x00,0x00  # split: branch target inside data
L_007f2e:
        db 0x0f,0xf0  # split: branch target inside data
L_007f30:
        db 0x00,0x20  # split: branch target inside data
L_007f32:
        db 0x3f  # split: branch target inside data
L_007f33:
        db 0xfc  # split: branch target inside data
L_007f34:
        db 0x22,0x00  # split: branch target inside data
L_007f36:
        db 0xc3  # split: branch target inside data
L_007f37:
        db 0xfc  # split: branch target inside data
L_007f38:
        db 0x00,0x00  # split: branch target inside data
L_007f3a:
        db 0x0c,0x3c  # split: branch target inside data
L_007f3c:
        db 0x00,0x00  # split: branch target inside data
L_007f3e:
        db 0x00,0x3c  # split: branch target inside data
L_007f40:
        db 0x20,0x00  # split: branch target inside data
L_007f42:
        db 0x00,0x3c  # split: branch target inside data
L_007f44:
        db 0x00,0xff  # split: branch target inside data
L_007f46:
        db 0xc0,0x3c,0x03  # split: branch target inside data
L_007f49:
        db 0xff,0xf0  # split: branch target inside data
L_007f4b:
        db 0x3c,0x0f  # split: branch target inside data
L_007f4d:
        db 0xff  # split: branch target inside data
L_007f4e:
        db 0xfc  # split: branch target inside data
L_007f4f:
        db 0x3c,0x00  # split: branch target inside data
L_007f51:
        db 0x22,0x02  # split: branch target inside data
L_007f53:
        db 0x20,0x22  # split: branch target inside data
L_007f55:
        db 0x02,0x22  # split: branch target inside data
L_007f57:
        db 0x02,0x04  # split: branch target inside data
L_007f59:
        db 0x10,0x00  # split: branch target inside data
L_007f5b:
        db 0x00,0x00  # split: branch target inside data
L_007f5d:
        db 0x00,0x00  # split: branch target inside data
L_007f5f:
        db 0x30,0x0c  # split: branch target inside data
L_007f61:
        db 0x00,0x00  # split: branch target inside data
L_007f63:
        db 0x3f  # split: branch target inside data
L_007f64:
        db 0xfc  # split: branch target inside data
L_007f65:
        db 0x00,0x00  # split: branch target inside data
L_007f67:
        db 0x0f,0xf0  # split: branch target inside data
L_007f69:
        db 0x00,0x00  # split: branch target inside data
L_007f6b:
        db 0x0f,0xf0  # split: branch target inside data
L_007f6d:
        db 0x00,0x00  # split: branch target inside data
L_007f6f:
        db 0x03,0xc0  # split: branch target inside data
L_007f71:
        db 0x00,0x00  # split: branch target inside data
L_007f73:
        db 0xff  # split: branch target inside data
L_007f74:
        db 0xff,0x00  # split: branch target inside data
L_007f76:
        db 0x03,0xff  # split: branch target inside data
L_007f78:
        db 0xff,0xc0  # split: branch target inside data
L_007f7a:
        db 0x0f  # split: branch target inside data
L_007f7b:
        db 0x0f,0xf0  # split: branch target inside data
L_007f7d:
        db 0xf0,0x0f  # split: branch target inside data
L_007f7f:
        db 0x0f,0xf0  # split: branch target inside data
L_007f81:
        db 0xf0,0x0f,0x3f  # split: branch target inside data
L_007f84:
        db 0xfc  # split: branch target inside data
L_007f85:
        db 0xf0,0x00,0x3c  # split: branch target inside data
L_007f88:
        db 0x3c,0x00  # split: branch target inside data
L_007f8a:
        db 0x00,0x3c  # split: branch target inside data
L_007f8c:
        db 0x3c,0x00  # split: branch target inside data
L_007f8e:
        db 0x00,0x3c  # split: branch target inside data
L_007f90:
        db 0x3c,0x00  # split: branch target inside data
L_007f92:
        db 0x00,0xfc  # split: branch target inside data
L_007f94:
        db 0x3f  # split: branch target inside data
L_007f95:
        db 0x00,0x00  # split: branch target inside data
L_007f97:
        db 0x00,0x00  # split: branch target inside data
L_007f99:
        db 0x00,0x04  # split: branch target inside data
L_007f9b:
        db 0x10,0x00  # split: branch target inside data
L_007f9d:
        db 0x00,0x00  # split: branch target inside data
L_007f9f:
        db 0x00,0x00  # split: branch target inside data
L_007fa1:
        db 0x0c,0x30  # split: branch target inside data
L_007fa3:
        db 0x00,0x00  # split: branch target inside data
L_007fa5:
        db 0x0f,0xf0  # split: branch target inside data
L_007fa7:
        db 0x00,0x00  # split: branch target inside data
L_007fa9:
        db 0x0f,0xf0  # split: branch target inside data
L_007fab:
        db 0x00,0x00  # split: branch target inside data
L_007fad:
        db 0x03,0xc0  # split: branch target inside data
L_007faf:
        db 0x00,0x30  # split: branch target inside data
L_007fb1:
        db 0x3f  # split: branch target inside data
L_007fb2:
        db 0xfc  # split: branch target inside data
L_007fb3:
        db 0x0c,0x0c  # split: branch target inside data
L_007fb5:
        db 0xcf  # split: branch target inside data
L_007fb6:
        db 0xf3,0x30,0x03  # split: branch target inside data
L_007fb9:
        db 0x0f,0xf0  # split: branch target inside data
L_007fbb:
        db 0xc0,0x00,0x03  # split: branch target inside data
L_007fbe:
        db 0xc0,0x00,0x00  # split: branch target inside data
L_007fc1:
        db 0x0f,0xf0  # split: branch target inside data
L_007fc3:
        db 0x00,0x00  # split: branch target inside data
L_007fc5:
        db 0x0f,0xf0  # split: branch target inside data
L_007fc7:
        db 0x00,0x00  # split: branch target inside data
L_007fc9:
        db 0x0c,0x30  # split: branch target inside data
L_007fcb:
        db 0x00,0x00  # split: branch target inside data
L_007fcd:
        db 0x0c,0x30  # split: branch target inside data
L_007fcf:
        db 0x00,0x00  # split: branch target inside data
L_007fd1:
        db 0x0c,0x30  # split: branch target inside data
L_007fd3:
        db 0x00,0x00  # split: branch target inside data
L_007fd5:
        db 0x3c,0x3c  # split: branch target inside data
L_007fd7:
        db 0x00,0x00  # split: branch target inside data
L_007fd9:
        db 0x00,0x00  # split: branch target inside data
L_007fdb:
        db 0x00,0x04  # split: branch target inside data
L_007fdd:
        db 0x10,0x00  # split: branch target inside data
L_007fdf:
        db 0x00,0x00  # split: branch target inside data
L_007fe1:
        db 0x80,0x00,0xc3  # split: branch target inside data
L_007fe4:
        db 0x08,0x08  # split: branch target inside data
L_007fe6:
        db 0x00,0xff  # split: branch target inside data
L_007fe8:
        db 0x08,0x88,0x00,0xff  # split: branch target inside data
L_007fec:
        db 0x03,0xf0  # split: branch target inside data
L_007fee:
        db 0x00,0x3c  # split: branch target inside data
L_007ff0:
        db 0x00,0xc0  # split: branch target inside data
L_007ff2:
        db 0x03,0xff  # split: branch target inside data
L_007ff4:
        db 0xc0,0xc0,0x0c  # split: branch target inside data
L_007ff7:
        db 0xff,0x33  # split: branch target inside data
L_007ff9:
        db 0xc0,0x0c,0x3c  # split: branch target inside data
L_007ffc:
        db 0x0c,0xc0  # split: branch target inside data
L_007ffe:
        db 0x0c,0x3c  # split: branch target inside data
L_008000:
        db 0x00,0xc0  # split: branch target inside data
L_008002:
        db 0x0c,0xff  # split: branch target inside data
L_008004:
        db 0x00,0xc0  # split: branch target inside data
L_008006:
        db 0x00,0xff  # split: branch target inside data
L_008008:
        db 0x00,0xc0  # split: branch target inside data
L_00800a:
        db 0x00,0xc3  # split: branch target inside data
L_00800c:
        db 0x00,0xc0  # split: branch target inside data
L_00800e:
        db 0x00,0xc3  # split: branch target inside data
L_008010:
        db 0x00,0xc0  # split: branch target inside data
L_008012:
        db 0x00,0xc3  # split: branch target inside data
L_008014:
        db 0x00,0xc0  # split: branch target inside data
L_008016:
        db 0x03,0xc3  # split: branch target inside data
L_008018:
        db 0xc0,0xc0,0x00  # split: branch target inside data
L_00801b:
        db 0x00,0x00  # split: branch target inside data
L_00801d:
        db 0x00,0x04  # split: branch target inside data
L_00801f:
        db 0x10,0x00  # split: branch target inside data
L_008021:
        db 0x00,0x00  # split: branch target inside data
L_008023:
        db 0x00,0x00  # split: branch target inside data
L_008025:
        db 0x0c,0x30  # split: branch target inside data
L_008027:
        db 0x00,0x00  # split: branch target inside data
L_008029:
        db 0x0f,0xf0  # split: branch target inside data
L_00802b:
        db 0x00,0x03  # split: branch target inside data
L_00802d:
        db 0x0f,0xf0  # split: branch target inside data
L_00802f:
        db 0xc0,0x0f,0xc3  # split: branch target inside data
L_008032:
        db 0xc3  # split: branch target inside data
L_008033:
        db 0xf0,0x3f  # split: branch target inside data
L_008035:
        db 0xff  # split: branch target inside data
L_008036:
        db 0xff  # split: branch target inside data
L_008037:
        db 0xfc  # split: branch target inside data
L_008038:
        db 0x3f  # split: branch target inside data
L_008039:
        db 0xff  # split: branch target inside data
L_00803a:
        db 0xff  # split: branch target inside data
L_00803b:
        db 0xfc  # split: branch target inside data
L_00803c:
        db 0x3f  # split: branch target inside data
L_00803d:
        db 0xcf  # split: branch target inside data
L_00803e:
        db 0xf3,0xfc  # split: branch target inside data
L_008040:
        db 0x3f  # split: branch target inside data
L_008041:
        db 0xcf  # split: branch target inside data
L_008042:
        db 0xf3,0xfc  # split: branch target inside data
L_008044:
        db 0x3f  # split: branch target inside data
L_008045:
        db 0xc3  # split: branch target inside data
L_008046:
        db 0xc3  # split: branch target inside data
L_008047:
        db 0xfc  # split: branch target inside data
L_008048:
        db 0x3f  # split: branch target inside data
L_008049:
        db 0xcf  # split: branch target inside data
L_00804a:
        db 0xf3,0xfc  # split: branch target inside data
L_00804c:
        db 0x3f  # split: branch target inside data
L_00804d:
        db 0x0f,0xf0  # split: branch target inside data
L_00804f:
        db 0xfc  # split: branch target inside data
L_008050:
        db 0x3f  # split: branch target inside data
L_008051:
        db 0x0c,0x30  # split: branch target inside data
L_008053:
        db 0xfc  # split: branch target inside data
L_008054:
        db 0x3c,0x0c  # split: branch target inside data
L_008056:
        db 0x30,0x3c  # split: branch target inside data
L_008058:
        db 0x30,0x3c  # split: branch target inside data
L_00805a:
        db 0x3c,0x0c  # split: branch target inside data
L_00805c:
        db 0x00,0x00  # split: branch target inside data
L_00805e:
        db 0x00,0x00  # split: branch target inside data
L_008060:
        db 0x04,0x10  # split: branch target inside data
L_008062:
        db 0x00,0x00  # split: branch target inside data
L_008064:
        db 0x00,0x00  # split: branch target inside data
L_008066:
        db 0x00,0x0f  # split: branch target inside data
L_008068:
        db 0xc0,0x00,0x00  # split: branch target inside data
L_00806b:
        db 0x0f,0xc0,0x00  # split: branch target inside data
L_00806e:
        db 0x00,0x0f  # split: branch target inside data
L_008070:
        db 0xc0,0x00,0x00  # split: branch target inside data
L_008073:
        db 0x03,0x00  # split: branch target inside data
L_008075:
        db 0x00,0x3f  # split: branch target inside data
L_008077:
        db 0xff  # split: branch target inside data
L_008078:
        db 0xff,0xf0  # split: branch target inside data
L_00807a:
        db 0x0f,0xff,0xff  # split: branch target inside data
L_00807d:
        db 0xc0,0x0f,0xcf  # split: branch target inside data
L_008080:
        db 0xcf  # split: branch target inside data
L_008081:
        db 0xc0,0x0f,0xc3  # split: branch target inside data
L_008084:
        db 0x0f,0xc0,0x0f  # split: branch target inside data
L_008087:
        db 0x0f  # split: branch target inside data
L_008088:
        db 0xc3  # split: branch target inside data
L_008089:
        db 0xc0,0x0f,0x0f  # split: branch target inside data
L_00808c:
        db 0xc3  # split: branch target inside data
L_00808d:
        db 0xc0,0x0c,0x3f  # split: branch target inside data
L_008090:
        db 0xf0,0xc0,0x0c,0x3f  # split: branch target inside data
L_008094:
        db 0xf0,0xc0,0x00,0xff  # split: branch target inside data
L_008098:
        db 0xfc  # split: branch target inside data
L_008099:
        db 0x00,0x00  # split: branch target inside data
L_00809b:
        db 0xff  # split: branch target inside data
L_00809c:
        db 0xfc  # split: branch target inside data
L_00809d:
        db 0x00,0x00  # split: branch target inside data
L_00809f:
        db 0x30,0x30  # split: branch target inside data
L_0080a1:
        db 0x00,0x04  # split: branch target inside data
L_0080a3:
        db 0x10,0x00  # split: branch target inside data
        db 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00  # split: branch target inside data
L_0080b1:
        db 0x00,0x00  # split: branch target inside data
L_0080b3:
        db 0x00,0x03  # split: branch target inside data
L_0080b5:
        db 0xc0,0x00,0x00  # split: branch target inside data
L_0080b8:
        db 0x0f,0xf0  # split: branch target inside data
L_0080ba:
        db 0x00,0x00  # split: branch target inside data
L_0080bc:
        db 0x3f  # split: branch target inside data
L_0080bd:
        db 0xfc  # split: branch target inside data
L_0080be:
        db 0x00,0x00  # split: branch target inside data
L_0080c0:
        db 0x0c,0xff  # split: branch target inside data
L_0080c2:
        db 0xff,0xf0  # split: branch target inside data
L_0080c4:
        db 0x00,0x3f  # split: branch target inside data
L_0080c6:
        db 0xff  # split: branch target inside data
L_0080c7:
        db 0xfc  # split: branch target inside data
L_0080c8:
        db 0x00,0xff  # split: branch target inside data
L_0080ca:
        db 0xff  # split: branch target inside data
L_0080cb:
        db 0xfc  # split: branch target inside data
L_0080cc:
        db 0x00,0xff  # split: branch target inside data
L_0080ce:
        db 0xff,0xf0  # split: branch target inside data
L_0080d0:
        db 0x03,0x30  # split: branch target inside data
L_0080d2:
        db 0x0f,0x3c  # split: branch target inside data
L_0080d4:
        db 0x0c,0x30  # split: branch target inside data
L_0080d6:
        db 0x03,0x0c  # split: branch target inside data
L_0080d8:
        db 0x0c,0x30  # split: branch target inside data
L_0080da:
        db 0x03,0x0c  # split: branch target inside data
L_0080dc:
        db 0x00,0xc0  # split: branch target inside data
L_0080de:
        db 0x0c,0x30  # split: branch target inside data
L_0080e0:
        db 0x00,0x00  # split: branch target inside data
L_0080e2:
        db 0x00,0x00  # split: branch target inside data
L_0080e4:
        db 0x04,0x10  # split: branch target inside data
        db 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00  # split: branch target inside data
L_0080ee:
        db 0x00,0x03  # split: branch target inside data
L_0080f0:
        db 0x00,0x00  # split: branch target inside data
L_0080f2:
        db 0x00,0x03  # split: branch target inside data
L_0080f4:
        db 0xc0,0x00,0x00  # split: branch target inside data
L_0080f7:
        db 0xc3  # split: branch target inside data
L_0080f8:
        db 0x0c,0x00  # split: branch target inside data
L_0080fa:
        db 0x00,0xff  # split: branch target inside data
L_0080fc:
        db 0xfc  # split: branch target inside data
L_0080fd:
        db 0x00,0x0f  # split: branch target inside data
L_0080ff:
        db 0xf0,0xff,0xc0  # split: branch target inside data
L_008102:
        db 0x0c,0x30  # split: branch target inside data
L_008104:
        db 0xc0,0xc0,0x0c  # split: branch target inside data
L_008107:
        db 0x30,0xc0  # split: branch target inside data
L_008109:
        db 0xc0,0x0f,0xff  # split: branch target inside data
L_00810c:
        db 0xff,0xc0  # split: branch target inside data
L_00810e:
        db 0x00,0xc3  # split: branch target inside data
L_008110:
        db 0x0c,0x00  # split: branch target inside data
L_008112:
        db 0x3f  # split: branch target inside data
L_008113:
        db 0xff  # split: branch target inside data
L_008114:
        db 0xff  # split: branch target inside data
L_008115:
        db 0xfc  # split: branch target inside data
L_008116:
        db 0x3f  # split: branch target inside data
L_008117:
        db 0xcf  # split: branch target inside data
L_008118:
        db 0x3c,0xf0  # split: branch target inside data
L_00811a:
        db 0x0f,0xff,0xff  # split: branch target inside data
L_00811d:
        db 0xc0,0x03,0xff  # split: branch target inside data
L_008120:
        db 0xff,0x00  # split: branch target inside data
L_008122:
        db 0x00,0x00  # split: branch target inside data
L_008124:
        db 0x00,0x00  # split: branch target inside data
L_008126:
        db 0x04,0x10  # split: branch target inside data
L_008128:
        db 0x00,0x3f  # split: branch target inside data
L_00812a:
        db 0xfc  # split: branch target inside data
L_00812b:
        db 0x00,0x00  # split: branch target inside data
L_00812d:
        db 0x03,0xc0  # split: branch target inside data
L_00812f:
        db 0x00,0x00  # split: branch target inside data
L_008131:
        db 0x0f,0xf0  # split: branch target inside data
L_008133:
        db 0x00,0x0f  # split: branch target inside data
L_008135:
        db 0xff  # split: branch target inside data
L_008136:
        db 0xff,0xf0  # split: branch target inside data
L_008138:
        db 0x30,0x00  # split: branch target inside data
L_00813a:
        db 0x00,0x0c  # split: branch target inside data
L_00813c:
        db 0x30,0x03  # split: branch target inside data
L_00813e:
        db 0xc0,0x0c,0x30  # split: branch target inside data
L_008141:
        db 0x0c,0x30  # split: branch target inside data
L_008143:
        db 0x0c,0x0f  # split: branch target inside data
L_008145:
        db 0xfc  # split: branch target inside data
L_008146:
        db 0x3f  # split: branch target inside data
L_008147:
        db 0xf0,0x00,0x0f  # split: branch target inside data
L_00814a:
        db 0xf0,0x00,0x00  # split: branch target inside data
L_00814d:
        db 0x0c,0x30  # split: branch target inside data
L_00814f:
        db 0x00,0x00  # split: branch target inside data
L_008151:
        db 0x0c,0x30  # split: branch target inside data
L_008153:
        db 0x00,0x00  # split: branch target inside data
L_008155:
        db 0x0c,0x30  # split: branch target inside data
L_008157:
        db 0x00,0x03  # split: branch target inside data
L_008159:
        db 0xfc  # split: branch target inside data
L_00815a:
        db 0x3f  # split: branch target inside data
L_00815b:
        db 0xc0,0x0c,0x03  # split: branch target inside data
L_00815e:
        db 0xc0,0x30,0x03  # split: branch target inside data
L_008161:
        db 0xff  # split: branch target inside data
L_008162:
        db 0xff,0xc0  # split: branch target inside data
L_008164:
        db 0x00,0x03  # split: branch target inside data
L_008166:
        db 0xc0,0x00,0x04  # split: branch target inside data
L_008169:
        db 0x10,0x00  # split: branch target inside data
L_00816b:
        db 0x00,0xc0  # split: branch target inside data
L_00816d:
        db 0x00,0x00  # split: branch target inside data
L_00816f:
        db 0x00,0xc0  # split: branch target inside data
L_008171:
        db 0x00,0x00  # split: branch target inside data
L_008173:
        db 0x00,0xc0  # split: branch target inside data
L_008175:
        db 0x00,0x00  # split: branch target inside data
L_008177:
        db 0x03,0x30  # split: branch target inside data
L_008179:
        db 0x00,0x00  # split: branch target inside data
L_00817b:
        db 0x03,0x30  # split: branch target inside data
L_00817d:
        db 0x00,0x00  # split: branch target inside data
L_00817f:
        db 0x03,0x30  # split: branch target inside data
L_008181:
        db 0x00,0x00  # split: branch target inside data
L_008183:
        db 0x0c,0x0c  # split: branch target inside data
L_008185:
        db 0x00,0x00  # split: branch target inside data
L_008187:
        db 0x0c,0x0c  # split: branch target inside data
L_008189:
        db 0x00,0x00  # split: branch target inside data
L_00818b:
        db 0x0c,0x0c  # split: branch target inside data
L_00818d:
        db 0x00,0x00  # split: branch target inside data
L_00818f:
        db 0x0c,0x0c  # split: branch target inside data
L_008191:
        db 0x00,0x00  # split: branch target inside data
L_008193:
        db 0x0c,0x0c  # split: branch target inside data
L_008195:
        db 0x00,0x00  # split: branch target inside data
L_008197:
        db 0x0c,0x0c  # split: branch target inside data
L_008199:
        db 0x00,0x00  # split: branch target inside data
L_00819b:
        db 0x30,0xc3  # split: branch target inside data
L_00819d:
        db 0x00,0x00  # split: branch target inside data
L_00819f:
        db 0xc3  # split: branch target inside data
L_0081a0:
        db 0xf0,0xc0,0x03,0x0c  # split: branch target inside data
L_0081a4:
        db 0xcc  # split: branch target inside data
L_0081a5:
        db 0x30,0x03  # split: branch target inside data
L_0081a7:
        db 0xf0,0xc3  # split: branch target inside data
L_0081a9:
        db 0xf0,0x04,0x10  # split: branch target inside data
L_0081ac:
        db 0x00,0x00  # split: branch target inside data
L_0081ae:
        db 0x00,0x00  # split: branch target inside data
L_0081b0:
        db 0x0f,0xff,0xff  # split: branch target inside data
L_0081b3:
        db 0xf0,0x0c,0x00  # split: branch target inside data
L_0081b6:
        db 0x00,0x30  # split: branch target inside data
L_0081b8:
        db 0x0c,0x0f  # split: branch target inside data
L_0081ba:
        db 0xfc  # split: branch target inside data
L_0081bb:
        db 0x30,0x0c  # split: branch target inside data
L_0081bd:
        db 0x30,0x3c  # split: branch target inside data
L_0081bf:
        db 0x30,0x0c  # split: branch target inside data
L_0081c1:
        db 0x30,0x00  # split: branch target inside data
L_0081c3:
        db 0x30,0x0c  # split: branch target inside data
L_0081c5:
        db 0x0f,0xf0  # split: branch target inside data
L_0081c7:
        db 0x30,0x0c  # split: branch target inside data
L_0081c9:
        db 0x00,0x0c  # split: branch target inside data
L_0081cb:
        db 0x30,0x0c  # split: branch target inside data
L_0081cd:
        db 0x03,0xf0  # split: branch target inside data
L_0081cf:
        db 0x30,0x0c  # split: branch target inside data
L_0081d1:
        db 0x0c,0x00  # split: branch target inside data
L_0081d3:
        db 0x30,0x0c  # split: branch target inside data
L_0081d5:
        db 0x03,0xc0  # split: branch target inside data
L_0081d7:
        db 0x30,0x03  # split: branch target inside data
L_0081d9:
        db 0x00,0x30  # split: branch target inside data
L_0081db:
        db 0xc0,0x03,0x03  # split: branch target inside data
L_0081de:
        db 0xc0,0xc0,0x00  # split: branch target inside data
L_0081e1:
        db 0xf0,0x0f,0x00,0x00  # split: branch target inside data
L_0081e5:
        db 0x0f,0xf0  # split: branch target inside data
L_0081e7:
        db 0x00,0x00  # split: branch target inside data
L_0081e9:
        db 0x00,0x00  # split: branch target inside data
L_0081eb:
        db 0x00,0x04  # split: branch target inside data
L_0081ed:
        db 0x10,0x00  # split: branch target inside data
        db 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00  # split: branch target inside data
L_0081f7:
        db 0x00,0x00  # split: branch target inside data
L_0081f9:
        db 0x30,0x00  # split: branch target inside data
L_0081fb:
        db 0x00,0x00  # split: branch target inside data
L_0081fd:
        db 0xc0,0x00,0x00  # split: branch target inside data
L_008200:
        db 0x03,0x00  # split: branch target inside data
L_008202:
        db 0x00,0x00  # split: branch target inside data
L_008204:
        db 0x0c,0x00  # split: branch target inside data
L_008206:
        db 0x00,0x00  # split: branch target inside data
L_008208:
        db 0x30,0x00  # split: branch target inside data
L_00820a:
        db 0x00,0x00  # split: branch target inside data
L_00820c:
        db 0xc0,0x00,0x00  # split: branch target inside data
L_00820f:
        db 0x03,0x00  # split: branch target inside data
L_008211:
        db 0x00,0x0c  # split: branch target inside data
L_008213:
        db 0x0c,0x00  # split: branch target inside data
L_008215:
        db 0x00,0x03  # split: branch target inside data
L_008217:
        db 0x30,0x00  # split: branch target inside data
L_008219:
        db 0x00,0x00  # split: branch target inside data
L_00821b:
        db 0xc0,0x00,0x00  # split: branch target inside data
L_00821e:
        db 0x03,0x30  # split: branch target inside data
L_008220:
        db 0x00,0x00  # split: branch target inside data
L_008222:
        db 0x0c,0x0c  # split: branch target inside data
        db 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00  # split: branch target inside data
L_00822c:
        db 0x00,0x00  # split: branch target inside data
L_00822e:
        db 0x04,0x10  # split: branch target inside data
L_008230:
        db 0x88,0x88,0x88,0x88  # split: branch target inside data
L_008234:
        db 0x33,0x33  # split: branch target inside data
L_008236:
        db 0x33,0x33  # split: branch target inside data
L_008238:
        db 0x33,0x33  # split: branch target inside data
L_00823a:
        db 0x33,0x33  # split: branch target inside data
L_00823c:
        db 0x11,0x11  # split: branch target inside data
L_00823e:
        db 0x11,0x11  # split: branch target inside data
L_008240:
        db 0x11,0x11  # split: branch target inside data
L_008242:
        db 0x11,0x11  # split: branch target inside data
L_008244:
        db 0xcc  # split: branch target inside data
L_008245:
        db 0xcc  # split: branch target inside data
L_008246:
        db 0xcc  # split: branch target inside data
L_008247:
        db 0xcc  # split: branch target inside data
L_008248:
        db 0xcc  # split: branch target inside data
L_008249:
        db 0xcc  # split: branch target inside data
L_00824a:
        db 0xcc  # split: branch target inside data
L_00824b:
        db 0xcc  # split: branch target inside data
L_00824c:
        db 0x88,0x88,0x88,0x88  # split: branch target inside data
L_008250:
        db 0x88,0x88,0x88,0x88  # split: branch target inside data
L_008254:
        db 0x33,0x33  # split: branch target inside data
L_008256:
        db 0x33,0x33  # split: branch target inside data
L_008258:
        db 0x33,0x33  # split: branch target inside data
L_00825a:
        db 0x33,0x33  # split: branch target inside data
L_00825c:
        db 0x11,0x11  # split: branch target inside data
L_00825e:
        db 0x11,0x11  # split: branch target inside data
L_008260:
        db 0x11,0x11  # split: branch target inside data
L_008262:
        db 0x11,0x11  # split: branch target inside data
L_008264:
        db 0xcc  # split: branch target inside data
L_008265:
        db 0xcc  # split: branch target inside data
L_008266:
        db 0xcc  # split: branch target inside data
L_008267:
        db 0xcc  # split: branch target inside data
L_008268:
        db 0xcc  # split: branch target inside data
L_008269:
        db 0xcc  # split: branch target inside data
L_00826a:
        db 0xcc  # split: branch target inside data
L_00826b:
        db 0xcc  # split: branch target inside data
L_00826c:
        db 0x88,0x88,0x88,0x88  # split: branch target inside data
L_008270:
        db 0x04,0x10  # split: branch target inside data
L_008272:
        db 0x00,0x0f  # split: branch target inside data
L_008274:
        db 0xf0,0x00,0x00  # split: branch target inside data
L_008277:
        db 0x0f,0xf0  # split: branch target inside data
L_008279:
        db 0x00,0x00  # split: branch target inside data
L_00827b:
        db 0x0f,0xf0  # split: branch target inside data
L_00827d:
        db 0x00,0x00  # split: branch target inside data
L_00827f:
        db 0x03,0xc0  # split: branch target inside data
L_008281:
        db 0x00,0x03  # split: branch target inside data
L_008283:
        db 0xff  # split: branch target inside data
L_008284:
        db 0xff,0xc0  # split: branch target inside data
L_008286:
        db 0x0f,0xff,0xff  # split: branch target inside data
L_008289:
        db 0xf0,0x3f  # split: branch target inside data
L_00828b:
        db 0x3f  # split: branch target inside data
L_00828c:
        db 0xfc  # split: branch target inside data
L_00828d:
        db 0xfc  # split: branch target inside data
L_00828e:
        db 0x3c,0x0f  # split: branch target inside data
L_008290:
        db 0xf0,0x3c,0x3c  # split: branch target inside data
L_008293:
        db 0x03,0xc0  # split: branch target inside data
L_008295:
        db 0x3c,0x3c  # split: branch target inside data
L_008297:
        db 0x03,0xc0  # split: branch target inside data
L_008299:
        db 0x3c,0x00  # split: branch target inside data
L_00829b:
        db 0x0f,0xf0  # split: branch target inside data
L_00829d:
        db 0x00,0x00  # split: branch target inside data
L_00829f:
        db 0x3f  # split: branch target inside data
L_0082a0:
        db 0xfc  # split: branch target inside data
L_0082a1:
        db 0x00,0x00  # split: branch target inside data
L_0082a3:
        db 0x3c,0x3c  # split: branch target inside data
L_0082a5:
        db 0x00,0x00  # split: branch target inside data
L_0082a7:
        db 0x3c,0x3c  # split: branch target inside data
L_0082a9:
        db 0x00,0x00  # split: branch target inside data
L_0082ab:
        db 0x3c,0x3c  # split: branch target inside data
L_0082ad:
        db 0x00,0x00  # split: branch target inside data
L_0082af:
        db 0xfc  # split: branch target inside data
L_0082b0:
        db 0x3f  # split: branch target inside data
L_0082b1:
        db 0x00,0x04  # split: branch target inside data
L_0082b3:
        db 0x10,0x00  # split: branch target inside data
L_0082b5:
        db 0x00,0x00  # split: branch target inside data
L_0082b7:
        db 0x00,0x0c  # split: branch target inside data
L_0082b9:
        db 0x0f,0xc0,0xc0  # split: branch target inside data
L_0082bc:
        db 0x0c,0x0f  # split: branch target inside data
L_0082be:
        db 0xc0,0xc0,0x0f  # split: branch target inside data
L_0082c1:
        db 0x0f,0xc3,0x00  # split: branch target inside data
L_0082c4:
        db 0x03,0xc3  # split: branch target inside data
L_0082c6:
        db 0x0f,0x00,0x00  # split: branch target inside data
L_0082c9:
        db 0xff  # split: branch target inside data
L_0082ca:
        db 0xfc  # split: branch target inside data
L_0082cb:
        db 0x00,0x00  # split: branch target inside data
L_0082cd:
        db 0x3f  # split: branch target inside data
L_0082ce:
        db 0xf0,0x00,0x00  # split: branch target inside data
L_0082d1:
        db 0x0f,0xc0,0x00  # split: branch target inside data
L_0082d4:
        db 0x00,0x03  # split: branch target inside data
L_0082d6:
        db 0x00,0x00  # split: branch target inside data
L_0082d8:
        db 0x00,0x0f  # split: branch target inside data
L_0082da:
        db 0xc0,0x00,0x00  # split: branch target inside data
L_0082dd:
        db 0x0f,0xc0,0x00  # split: branch target inside data
L_0082e0:
        db 0x00,0x3c  # split: branch target inside data
L_0082e2:
        db 0xf0,0x00,0x00  # split: branch target inside data
L_0082e5:
        db 0xf0,0x3c,0x00  # split: branch target inside data
L_0082e8:
        db 0x03,0xc0  # split: branch target inside data
L_0082ea:
        db 0x0f,0x00,0x0f  # split: branch target inside data
L_0082ed:
        db 0x00,0x03  # split: branch target inside data
L_0082ef:
        db 0xc0,0x00,0x00  # split: branch target inside data
L_0082f2:
        db 0x00,0x00  # split: branch target inside data
L_0082f4:
        db 0x04,0x10  # split: branch target inside data
L_0082f6:
        db 0x00,0x00  # split: branch target inside data
L_0082f8:
        db 0x00,0x00  # split: branch target inside data
L_0082fa:
        db 0x00,0x0f  # split: branch target inside data
L_0082fc:
        db 0xc0,0x00,0x00  # split: branch target inside data
L_0082ff:
        db 0x0f,0xc0,0x00  # split: branch target inside data
L_008302:
        db 0x00,0x0f  # split: branch target inside data
L_008304:
        db 0xc0,0x00,0x00  # split: branch target inside data
L_008307:
        db 0x03,0x00  # split: branch target inside data
L_008309:
        db 0x00,0x00  # split: branch target inside data
L_00830b:
        db 0xff  # split: branch target inside data
L_00830c:
        db 0xfc  # split: branch target inside data
L_00830d:
        db 0x00,0x03  # split: branch target inside data
L_00830f:
        db 0x3f  # split: branch target inside data
L_008310:
        db 0xf3,0x00,0x03  # split: branch target inside data
L_008313:
        db 0x0f,0xc3,0x00  # split: branch target inside data
L_008316:
        db 0x03,0x03  # split: branch target inside data
L_008318:
        db 0x03,0x00  # split: branch target inside data
L_00831a:
        db 0x03,0x0f  # split: branch target inside data
L_00831c:
        db 0xc3  # split: branch target inside data
L_00831d:
        db 0x00,0x03  # split: branch target inside data
L_00831f:
        db 0x0f,0xc3,0x00  # split: branch target inside data
L_008322:
        db 0x00,0x3c  # split: branch target inside data
L_008324:
        db 0xf0,0x00,0x00  # split: branch target inside data
L_008327:
        db 0x30,0x30  # split: branch target inside data
L_008329:
        db 0x00,0x00  # split: branch target inside data
L_00832b:
        db 0x30,0x30  # split: branch target inside data
L_00832d:
        db 0x00,0x00  # split: branch target inside data
L_00832f:
        db 0xf0,0x3c,0x00  # split: branch target inside data
L_008332:
        db 0x00,0x00  # split: branch target inside data
L_008334:
        db 0x00,0x00  # split: branch target inside data
L_008336:
        db 0x04,0x10  # split: branch target inside data
L_008338:
        db 0x00,0x00  # split: branch target inside data
L_00833a:
        db 0x00,0x00  # split: branch target inside data
L_00833c:
        db 0x00,0x0f  # split: branch target inside data
L_00833e:
        db 0xf0,0x00,0x00  # split: branch target inside data
L_008341:
        db 0x0f,0xf0  # split: branch target inside data
L_008343:
        db 0x00,0x00  # split: branch target inside data
L_008345:
        db 0x0f,0xf0  # split: branch target inside data
L_008347:
        db 0x00,0x00  # split: branch target inside data
L_008349:
        db 0x03,0xc0  # split: branch target inside data
L_00834b:
        db 0x00,0x00  # split: branch target inside data
L_00834d:
        db 0x3f  # split: branch target inside data
L_00834e:
        db 0xfc  # split: branch target inside data
L_00834f:
        db 0x00,0x03  # split: branch target inside data
L_008351:
        db 0xff  # split: branch target inside data
L_008352:
        db 0xff,0xc0  # split: branch target inside data
L_008354:
        db 0x0f,0xcf  # split: branch target inside data
L_008356:
        db 0xf3,0xf0,0x0f,0x03,0xc0  # split: branch target inside data
L_00835b:
        db 0xf0,0x0f  # split: branch target inside data
L_00835d:
        db 0x0f,0xf0  # split: branch target inside data
L_00835f:
        db 0xf0,0x00,0x3f  # split: branch target inside data
L_008362:
        db 0xfc  # split: branch target inside data
L_008363:
        db 0x00,0x00  # split: branch target inside data
L_008365:
        db 0x3c,0x3c  # split: branch target inside data
L_008367:
        db 0x00,0x00  # split: branch target inside data
L_008369:
        db 0x3c,0x3c  # split: branch target inside data
L_00836b:
        db 0x00,0x00  # split: branch target inside data
L_00836d:
        db 0x3c,0x3c  # split: branch target inside data
L_00836f:
        db 0x00,0x00  # split: branch target inside data
L_008371:
        db 0x3c,0x3c  # split: branch target inside data
L_008373:
        db 0x00,0x00  # split: branch target inside data
L_008375:
        db 0x3c,0x3c  # split: branch target inside data
L_008377:
        db 0x00,0x04  # split: branch target inside data
L_008379:
        db 0x10,0x80,0x00,0x80  # split: branch target inside data
L_00837d:
        db 0x00,0x80,0x00,0x80  # split: branch target inside data
L_008381:
        db 0x00,0x80,0x00,0x80  # split: branch target inside data
L_008385:
        db 0x00,0x88,0x88,0x88  # split: branch target inside data
L_008389:
        db 0x88,0x00  # split: branch target inside data
L_00838b:
        db 0x80,0x00,0x80  # split: branch target inside data
L_00838e:
        db 0x00,0x80,0x00,0x80  # split: branch target inside data
L_008392:
        db 0x00,0x80,0x00,0x80  # split: branch target inside data
L_008396:
        db 0x88,0x88,0x88,0x88  # split: branch target inside data
L_00839a:
        db 0x80,0x00,0x80  # split: branch target inside data
L_00839d:
        db 0x00,0x80,0x00,0x80  # split: branch target inside data
L_0083a1:
        db 0x00,0x80,0x00,0x80  # split: branch target inside data
L_0083a5:
        db 0x00,0x88,0x88,0x88  # split: branch target inside data
L_0083a9:
        db 0x88,0x00  # split: branch target inside data
L_0083ab:
        db 0x80,0x00,0x80  # split: branch target inside data
L_0083ae:
        db 0x00,0x80,0x00,0x80  # split: branch target inside data
L_0083b2:
        db 0x00,0x80,0x00,0x80  # split: branch target inside data
L_0083b6:
        db 0x88,0x88,0x88,0x88  # split: branch target inside data
L_0083ba:
        db 0x04,0x10  # split: branch target inside data
        db 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00  # split: branch target inside data
L_0083fc:
        db 0x04,0x10  # split: branch target inside data
L_0083fe:
        db 0xff  # split: branch target inside data
L_0083ff:
        db 0xff  # split: branch target inside data
L_008400:
        db 0xff  # split: branch target inside data
L_008401:
        db 0xff  # split: branch target inside data
L_008402:
        db 0xff  # split: branch target inside data
L_008403:
        db 0xff  # split: branch target inside data
L_008404:
        db 0xff  # split: branch target inside data
L_008405:
        db 0xff  # split: branch target inside data
L_008406:
        db 0xff  # split: branch target inside data
L_008407:
        db 0xff  # split: branch target inside data
L_008408:
        db 0xff  # split: branch target inside data
L_008409:
        db 0xff  # split: branch target inside data
L_00840a:
        db 0xff  # split: branch target inside data
L_00840b:
        db 0xff  # split: branch target inside data
L_00840c:
        db 0xff  # split: branch target inside data
L_00840d:
        db 0xff  # split: branch target inside data
L_00840e:
        db 0xff  # split: branch target inside data
L_00840f:
        db 0xff  # split: branch target inside data
L_008410:
        db 0xff  # split: branch target inside data
L_008411:
        db 0xff  # split: branch target inside data
L_008412:
        db 0xff  # split: branch target inside data
L_008413:
        db 0xff  # split: branch target inside data
L_008414:
        db 0xff  # split: branch target inside data
L_008415:
        db 0xff  # split: branch target inside data
L_008416:
        db 0xff  # split: branch target inside data
L_008417:
        db 0xff  # split: branch target inside data
L_008418:
        db 0xff  # split: branch target inside data
L_008419:
        db 0xff  # split: branch target inside data
L_00841a:
        db 0xff  # split: branch target inside data
L_00841b:
        db 0xff  # split: branch target inside data
L_00841c:
        db 0xff  # split: branch target inside data
L_00841d:
        db 0xff  # split: branch target inside data
L_00841e:
        db 0xff  # split: branch target inside data
L_00841f:
        db 0xff  # split: branch target inside data
L_008420:
        db 0xff  # split: branch target inside data
L_008421:
        db 0xff  # split: branch target inside data
L_008422:
        db 0xff  # split: branch target inside data
L_008423:
        db 0xff  # split: branch target inside data
L_008424:
        db 0xff  # split: branch target inside data
L_008425:
        db 0xff  # split: branch target inside data
L_008426:
        db 0xff  # split: branch target inside data
L_008427:
        db 0xff  # split: branch target inside data
L_008428:
        db 0xff  # split: branch target inside data
L_008429:
        db 0xff  # split: branch target inside data
L_00842a:
        db 0xff  # split: branch target inside data
L_00842b:
        db 0xff  # split: branch target inside data
L_00842c:
        db 0xff  # split: branch target inside data
L_00842d:
        db 0xff  # split: branch target inside data
L_00842e:
        db 0xff  # split: branch target inside data
L_00842f:
        db 0xff  # split: branch target inside data
L_008430:
        db 0xff  # split: branch target inside data
L_008431:
        db 0xff  # split: branch target inside data
L_008432:
        db 0xff  # split: branch target inside data
L_008433:
        db 0xff  # split: branch target inside data
L_008434:
        db 0xff  # split: branch target inside data
L_008435:
        db 0xff  # split: branch target inside data
L_008436:
        db 0xff  # split: branch target inside data
L_008437:
        db 0xff  # split: branch target inside data
L_008438:
        db 0xff  # split: branch target inside data
L_008439:
        db 0xff  # split: branch target inside data
L_00843a:
        db 0xff  # split: branch target inside data
L_00843b:
        db 0xff  # split: branch target inside data
L_00843c:
        db 0xff  # split: branch target inside data
L_00843d:
        db 0xff,0x04  # split: branch target inside data
L_00843f:
        db 0x10,0xff  # split: branch target inside data
L_008441:
        db 0xff  # split: branch target inside data
L_008442:
        db 0xff  # split: branch target inside data
L_008443:
        db 0xff,0x00  # split: branch target inside data
        db 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00  # split: branch target inside data
L_008479:
        db 0x00,0x00  # split: branch target inside data
L_00847b:
        db 0x00,0xff  # split: branch target inside data
L_00847d:
        db 0xff  # split: branch target inside data
L_00847e:
        db 0xff  # split: branch target inside data
L_00847f:
        db 0xff,0x04  # split: branch target inside data
L_008481:
        db 0x10,0xff  # split: branch target inside data
L_008483:
        db 0xff  # split: branch target inside data
L_008484:
        db 0xff  # split: branch target inside data
L_008485:
        db 0xff,0x00  # split: branch target inside data
L_008487:
        db 0x00,0x00  # split: branch target inside data
L_008489:
        db 0x00,0x00  # split: branch target inside data
L_00848b:
        db 0x00,0xfc  # split: branch target inside data
L_00848d:
        db 0x00,0x00  # split: branch target inside data
L_00848f:
        db 0x03,0xcf  # split: branch target inside data
L_008491:
        db 0xc0,0x00,0x0f  # split: branch target inside data
L_008494:
        db 0x0f,0x00,0x00  # split: branch target inside data
L_008497:
        db 0x3c,0x0f  # split: branch target inside data
L_008499:
        db 0x00,0x00  # split: branch target inside data
L_00849b:
        db 0x3c,0x0f  # split: branch target inside data
L_00849d:
        db 0x00,0x00  # split: branch target inside data
L_00849f:
        db 0xfc  # split: branch target inside data
L_0084a0:
        db 0x0f,0x00,0x00  # split: branch target inside data
L_0084a3:
        db 0xff  # split: branch target inside data
L_0084a4:
        db 0xff,0x00  # split: branch target inside data
L_0084a6:
        db 0x00,0xfc  # split: branch target inside data
L_0084a8:
        db 0x0f,0x00,0x00  # split: branch target inside data
L_0084ab:
        db 0x3c,0x0f  # split: branch target inside data
L_0084ad:
        db 0x00,0x00  # split: branch target inside data
L_0084af:
        db 0x3c,0x0f  # split: branch target inside data
L_0084b1:
        db 0x00,0x00  # split: branch target inside data
L_0084b3:
        db 0x3c,0x0f  # split: branch target inside data
L_0084b5:
        db 0x00,0x00  # split: branch target inside data
L_0084b7:
        db 0xfc  # split: branch target inside data
L_0084b8:
        db 0x3f  # split: branch target inside data
L_0084b9:
        db 0x00,0x00  # split: branch target inside data
L_0084bb:
        db 0x00,0x00  # split: branch target inside data
L_0084bd:
        db 0x00,0xff  # split: branch target inside data
L_0084bf:
        db 0xff  # split: branch target inside data
L_0084c0:
        db 0xff  # split: branch target inside data
L_0084c1:
        db 0xff,0x04  # split: branch target inside data
L_0084c3:
        db 0x10,0xff  # split: branch target inside data
L_0084c5:
        db 0xff  # split: branch target inside data
L_0084c6:
        db 0xff  # split: branch target inside data
L_0084c7:
        db 0xff,0x00  # split: branch target inside data
L_0084c9:
        db 0x00,0x00  # split: branch target inside data
L_0084cb:
        db 0x00,0x00  # split: branch target inside data
L_0084cd:
        db 0x0f,0xf0  # split: branch target inside data
L_0084cf:
        db 0x00,0x00  # split: branch target inside data
L_0084d1:
        db 0x3c,0x3c  # split: branch target inside data
L_0084d3:
        db 0x00,0x00  # split: branch target inside data
L_0084d5:
        db 0xfc  # split: branch target inside data
L_0084d6:
        db 0x0f,0x00,0x00  # split: branch target inside data
L_0084d9:
        db 0x3c,0x0f  # split: branch target inside data
L_0084db:
        db 0x00,0x00  # split: branch target inside data
L_0084dd:
        db 0x3c,0x3c  # split: branch target inside data
L_0084df:
        db 0x00,0x00  # split: branch target inside data
L_0084e1:
        db 0x3f  # split: branch target inside data
L_0084e2:
        db 0xf0,0x00,0x00  # split: branch target inside data
L_0084e5:
        db 0x3c,0x3c  # split: branch target inside data
L_0084e7:
        db 0x00,0x00  # split: branch target inside data
L_0084e9:
        db 0x3c,0x0f  # split: branch target inside data
L_0084eb:
        db 0x00,0x00  # split: branch target inside data
L_0084ed:
        db 0x3c,0x0f  # split: branch target inside data
L_0084ef:
        db 0xc0,0x00,0xfc  # split: branch target inside data
L_0084f2:
        db 0x0f,0x00,0x00  # split: branch target inside data
L_0084f5:
        db 0x3c,0x3c  # split: branch target inside data
L_0084f7:
        db 0x00,0x00  # split: branch target inside data
L_0084f9:
        db 0x0f,0xf0  # split: branch target inside data
L_0084fb:
        db 0x00,0x00  # split: branch target inside data
L_0084fd:
        db 0x00,0x00  # split: branch target inside data
L_0084ff:
        db 0x00,0xff  # split: branch target inside data
L_008501:
        db 0xff  # split: branch target inside data
L_008502:
        db 0xff  # split: branch target inside data
L_008503:
        db 0xff,0x04  # split: branch target inside data
L_008505:
        db 0x10,0xff  # split: branch target inside data
L_008507:
        db 0xff  # split: branch target inside data
L_008508:
        db 0xff  # split: branch target inside data
L_008509:
        db 0xff,0x00  # split: branch target inside data
L_00850b:
        db 0x00,0x00  # split: branch target inside data
L_00850d:
        db 0x00,0x00  # split: branch target inside data
L_00850f:
        db 0x00,0xff  # split: branch target inside data
L_008511:
        db 0xc0,0x00,0x03  # split: branch target inside data
L_008514:
        db 0xcf  # split: branch target inside data
L_008515:
        db 0x00,0x00  # split: branch target inside data
L_008517:
        db 0x0f  # split: branch target inside data
L_008518:
        db 0x0f,0x00,0x00  # split: branch target inside data
L_00851b:
        db 0x3f  # split: branch target inside data
L_00851c:
        db 0x03,0x00  # split: branch target inside data
L_00851e:
        db 0x00,0x3c  # split: branch target inside data
L_008520:
        db 0x03,0x00  # split: branch target inside data
L_008522:
        db 0x00,0xfc  # split: branch target inside data
L_008524:
        db 0x00,0x00  # split: branch target inside data
L_008526:
        db 0x00,0xfc  # split: branch target inside data
L_008528:
        db 0x00,0x00  # split: branch target inside data
L_00852a:
        db 0x00,0x3c  # split: branch target inside data
L_00852c:
        db 0x00,0x00  # split: branch target inside data
L_00852e:
        db 0x00,0x3f  # split: branch target inside data
L_008530:
        db 0x03,0x00  # split: branch target inside data
L_008532:
        db 0x00,0x0f  # split: branch target inside data
L_008534:
        db 0x03,0x00  # split: branch target inside data
L_008536:
        db 0x00,0x03  # split: branch target inside data
L_008538:
        db 0xcf  # split: branch target inside data
L_008539:
        db 0x00,0x00  # split: branch target inside data
L_00853b:
        db 0x00,0xfc  # split: branch target inside data
L_00853d:
        db 0x00,0x00  # split: branch target inside data
L_00853f:
        db 0x00,0x00  # split: branch target inside data
L_008541:
        db 0x00,0xff  # split: branch target inside data
L_008543:
        db 0xff  # split: branch target inside data
L_008544:
        db 0xff  # split: branch target inside data
L_008545:
        db 0xff,0x04  # split: branch target inside data
L_008547:
        db 0x10,0xff  # split: branch target inside data
L_008549:
        db 0xff  # split: branch target inside data
L_00854a:
        db 0xff  # split: branch target inside data
L_00854b:
        db 0xff,0x00  # split: branch target inside data
L_00854d:
        db 0x00,0x00  # split: branch target inside data
L_00854f:
        db 0x00,0x00  # split: branch target inside data
L_008551:
        db 0x3f  # split: branch target inside data
L_008552:
        db 0xc0,0x00,0x00  # split: branch target inside data
L_008555:
        db 0xf0,0xf0,0x00,0x03  # split: branch target inside data
L_008559:
        db 0xf0,0x3c,0x00  # split: branch target inside data
L_00855c:
        db 0x00,0xf0  # split: branch target inside data
L_00855e:
        db 0x0f,0x00,0x00  # split: branch target inside data
L_008561:
        db 0xf0,0x0f,0x00,0x00  # split: branch target inside data
L_008565:
        db 0xf0,0x0f,0xc0,0x00  # split: branch target inside data
L_008569:
        db 0xf0,0x0f,0xc0,0x00  # split: branch target inside data
L_00856d:
        db 0xf0,0x0f,0x00,0x00  # split: branch target inside data
L_008571:
        db 0xf0,0x0f,0x00,0x03  # split: branch target inside data
L_008575:
        db 0xf0,0x3c,0x00  # split: branch target inside data
L_008578:
        db 0x00,0xf0  # split: branch target inside data
L_00857a:
        db 0xf0,0x00,0x00  # split: branch target inside data
L_00857d:
        db 0x3f  # split: branch target inside data
L_00857e:
        db 0xc0,0x00,0x00  # split: branch target inside data
L_008581:
        db 0x00,0x00  # split: branch target inside data
L_008583:
        db 0x00,0xff  # split: branch target inside data
L_008585:
        db 0xff  # split: branch target inside data
L_008586:
        db 0xff  # split: branch target inside data

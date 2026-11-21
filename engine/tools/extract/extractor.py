#!/usr/bin/env python3
"""extractor.py — turn a copy of the game into the engine's content header.

The header, `engine/content/gen_content.h`, is **generated and committed**. It
carries the game's strings, which are the copyright holder's text; AGENTS.md
§ 0 ("The text/art line") is the ruling that allows that, and it is allowed on
the same grounds as `asm/` itself: the engine cannot print anything else and
still be Ultima II, and the reconstruction already carries every byte of it.
What the ruling also demands is that the copy not be hand-maintained, because
two hand-maintained copies drift and then lie. So this script is the only
writer, the committed file is its output, and `--verify` is the drift gate.

Two sources, in priority order:

  exe  --exe PATH (default: <repo>/original/ultimaii.exe when present).
         The real bytes, located inside the function range named by the
         manifest, so a community-patched build that shifted a string is still
         read correctly rather than silently mismatched. This is the source the
         committed header is built from, because it is the only one that
         resolves all 220 strings.
  asm  the fallback (docs/phase4-plan.md CP1): the `db "...",0x00` lines of our
         own byte-proven asm/ tree. It resolves 181 of 220 and leaves 38 empty,
         because those live in regions Phase 3 rendered as byte lists or as
         instructions rather than as text. It is therefore no longer a build
         source — writing it over the committed header would silently blank the
         character-creation screen, which is what `--force` is there to stop.
         What it IS now is the verifier: every string asm can prove must agree
         with the committed file.

The manifest is (symbol, asm file, which string in that file). Offsets and
names are facts about the binary and ours to publish.

`--verify` is the drift gate and it is stronger than the build fallback, because
it does not use DB_RE at all: it decodes every `db` payload of the file the
manifest names — quoted text, bare hex byte lists, and lines that mix them — and
requires each committed string to be present in those bytes. That proves 214 of
the 220 committed strings against asm/ without duplicating any of them into this
file. The 6 it cannot prove are named in UNPROVABLE_CEILING's comment.

Usage:
  extractor.py [-o engine/content/gen_content.h] [--exe PATH] [--source auto|exe|asm]
  extractor.py --emit text      # SYMBOL<TAB>value per line, for test scripts
  extractor.py --check          # fail if the written header is not current
  extractor.py --verify         # fail if the committed header cannot be proven
                                # against asm/, or carries an empty string
"""
import argparse
import hashlib
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ENGINE = os.path.abspath(os.path.join(HERE, "..", ".."))
REPO = os.path.abspath(os.path.join(ENGINE, ".."))

# The known-good image (AGENTS.md, disasm/phase3-summary.md). A mismatch is a
# warning, not an error: original/ may carry community patches.
EXPECTED_MD5 = "fe884d5117971073ac8097cc7a6aca25"
FILE_BIAS = 0x200          # runtime segment offset -> file offset

# (symbol, asm file, locator)
#   locator = int index into the file's `db "...",0x00` lines  -> both sources work
#   locator = ("run", file_offset)                            -> exe only
# The "run" form is needed for strings that sit inside the regions Phase 3
# reconstructed as mis-decoded code (asm/02A3, asm/02E6): their bytes are
# proven but not written as db lines, so the documented asm fallback cannot
# recover them. Without an exe those entries come out empty and the engine
# skips the line; see engine/tests/run_cp1.sh, which asserts accordingly.
# CP1 scope: the presentation intro, the title/menu screen, and the 'D' poem.
MANIFEST = [
    ("U2_S_INTRO_ORIGIN",      "014B_sub_014b.asm", 0),
    ("U2_S_INTRO_PROUDLY",     "015E_sub_015e.asm", 0),
    ("U2_S_INTRO_PART",        "018D_sub_018d.asm", 0),
    ("U2_S_INTRO_BESTSELLER",  "01A1_sub_01a1.asm", 0),
    ("U2_S_INTRO_GENRE",       "01C4_sub_01c4.asm", 0),
    ("U2_S_INTRO_AUTHOR",      "01EA_sub_01ea.asm", 0),
    ("U2_S_TITLE_GAME",        "0246_sub_0246.asm", 0),
    ("U2_S_TITLE_REVENGE",     "025E_sub_025e.asm", 0),
    ("U2_S_TITLE_OF_THE",      "0273_sub_0273.asm", 0),
    ("U2_S_TITLE_ENCHANTRESS", "0288_sub_0288.asm", 0),
    ("U2_S_TITLE_COPYRIGHT",   "02A3_sub_02a3.asm", ("run", 0x04AD)),
    ("U2_S_TITLE_AND_ORIGIN",  "02CC_sub_02cc.asm", 0),
    ("U2_S_TITLE_TYPE",        "02E6_sub_02e6.asm", ("run", 0x04F0)),
    ("U2_S_MENU_DEMO",         "02F8_sub_02f8.asm", 0),
    ("U2_S_MENU_PLAY",         "031F_sub_031f.asm", 0),
    ("U2_S_MENU_CREATE",       "034B_sub_034b.asm", 0),
    ("U2_S_MENU_CHOICE",       "0375_sub_0375.asm", 0),
    ("U2_S_POEM_1A",           "03AC_sub_03ac.asm", 0),
    ("U2_S_POEM_1B",           "03AC_sub_03ac.asm", 1),
    ("U2_S_POEM_2A",           "03AC_sub_03ac.asm", 2),
    ("U2_S_POEM_2B",           "03AC_sub_03ac.asm", 3),
    ("U2_S_POEM_3A",           "03AC_sub_03ac.asm", 4),
    ("U2_S_POEM_3B",           "03AC_sub_03ac.asm", 5),
    ("U2_S_POEM_4A",           "03AC_sub_03ac.asm", 6),
    ("U2_S_POEM_4B",           "03AC_sub_03ac.asm", 7),
    ("U2_S_POEM_5A",           "03AC_sub_03ac.asm", 8),
    ("U2_S_POEM_5B",           "03AC_sub_03ac.asm", 9),
    ("U2_S_POEM_6A",           "03AC_sub_03ac.asm", 10),
    ("U2_S_POEM_6B",           "03AC_sub_03ac.asm", 11),
    # CP3: character creation and the status line. Every "run" offset below is a
    # string inside a region Phase 3 reconstructed as mis-decoded code, so the
    # asm fallback cannot recover it; the offsets come from the STR lines in
    # disasm/redecoded/5400_sub_5400.txt, which is our own decode.
    ("U2_S_CREATE_BLANK",      "5400_sub_5400.asm", ("run", 0x5615, 28)),
    # asm/0764 (file 0x0964): 'P' reads the player record and, only when the
    # name byte is zero, prints this and waits for ESC. Offset is the '(' — the
    # preceding byte belongs to the jump that skips the prompt on the first pass.
    ("U2_S_DISK_PLAYER",       "0764_sub_0764.asm", ("run", 0x0979, 20)),
    ("U2_S_CREATE_NOTBLANK1",  "5400_sub_5400.asm", ("run", 0x566B)),
    ("U2_S_CREATE_NOTBLANK2",  "5400_sub_5400.asm", ("run", 0x5689)),
    ("U2_S_CREATE_GENERATION", "5400_sub_5400.asm", ("run", 0x569E)),
    # The running total does NOT follow the GENERATION heading. asm/56C8 prints
    # this line and asm/5C85 addresses the value at column 33 row 2, which is
    # where this string's trailing ": " leaves the cursor. The db listing splits
    # the run at 0x56C8 ("INTS LEFT...") because the preceding bytes decode as
    # code, so the locator is the true start found in the binary.
    ("U2_S_CREATE_POINTS",     "5400_sub_5400.asm", ("run", 0x56C6)),
    ("U2_S_CREATE_FORM",       "5400_sub_5400.asm", ("run", 0x56F4)),
    ("U2_S_SEX_MALE",          "5400_sub_5400.asm", ("run", 0x595B)),
    ("U2_S_SEX_FEMALE",        "5400_sub_5400.asm", ("run", 0x5988)),
    ("U2_S_RACE_HUMAN",        "5400_sub_5400.asm", ("run", 0x59C9)),
    ("U2_S_RACE_ELF",          "5400_sub_5400.asm", ("run", 0x59F7)),
    ("U2_S_RACE_DWARF",        "5400_sub_5400.asm", ("run", 0x5A23)),
    ("U2_S_RACE_HOBBIT",       "5400_sub_5400.asm", ("run", 0x5A54)),
    ("U2_S_CLASS_FIGHTER",     "5400_sub_5400.asm", ("run", 0x5A9E)),
    ("U2_S_CLASS_CLERIC",      "5400_sub_5400.asm", ("run", 0x5ACE)),
    ("U2_S_CLASS_WIZARD",      "5400_sub_5400.asm", ("run", 0x5AFD)),
    ("U2_S_CLASS_THIEF",       "5400_sub_5400.asm", ("run", 0x5B2F)),
    ("U2_S_CREATE_TOPLAY",     "5400_sub_5400.asm", ("run", 0x5C1F)),
    ("U2_S_CREATE_PROGMSTR",   "5400_sub_5400.asm", ("run", 0x5C43)),
    ("U2_S_STATUS_HP",         "50F9_status_line_print.asm", 0),
    ("U2_S_STATUS_FOOD",       "50F9_status_line_print.asm", 1),
    ("U2_S_STATUS_EXP",        "50F9_status_line_print.asm", 2),
    ("U2_S_STATUS_GOLD",       "50F9_status_line_print.asm", 3),
    # The CMD prompt and the four direction words. The prompt is its own
    # routine (asm/087D); the direction words are standalone strings in the
    # movement arms (asm/0955/097C/09A3/09C9 — the echo sites named in
    # docs/phase4-implementation.md § CP4 "The movement model"). The CP4b
    # status band needs all five as content, not engine-typed text.
    ("U2_S_CMD_PROMPT",        "087D_cmd_prompt.asm", 0),
    ("U2_S_DIR_NORTH",         "0955_sub_0955.asm", 0),
    ("U2_S_DIR_SOUTH",         "097C_sub_097c.asm", 0),
    ("U2_S_DIR_WEST",          "09A3_sub_09a3.asm", 0),
    ("U2_S_DIR_EAST",          "09C9_sub_09c9.asm", 0),
    # The Q save flow (asm/3DC7 cmd_Q + the location guards asm/3DE4/3DFA).
    # The split attributed none of these as db lines (the prompt's line ends
    # ",0" not ",0x00"), so all five use run locators at their file addresses,
    # verified against the raw bytes. Note each guard string is preceded by a
    # 0x8d line-marker byte (the same high-bit convention as the tlkx files),
    # so the string starts one byte after the call: 0x3FCA, 0x3FE8, 0x4005,
    # 0x4022, 0x4037.
    ("U2_S_Q_PROMPT",          "3DC7_cmd_Q.asm", ("run", 0x3FCA, 18, "QUIT OR SAVE GAME.")),
    ("U2_S_Q_OUTDOORS",        "3DE4_offer_only_outdoors.asm", ("run", 0x3FE8, 14, "ONLY OUTDOORS!")),
    ("U2_S_Q_EARTH",           "3DFA_offer_only_outdoors_earth.asm", ("run", 0x4005, 14, "ONLY ON EARTH!")),
    ("U2_S_Q_FOOT",            "3DFA_offer_only_outdoors_earth.asm", ("run", 0x4022, 13, "ONLY ON FOOT!")),
    ("U2_S_Q_MOMENT",          "3DFA_offer_only_outdoors_earth.asm", ("run", 0x4037, 18, "ONE MOMENT PLEASE!")),
    # The PASS echo. The walk loop prints it from an inline string in the code
    # stream (call at file 0x0B45, bytes "PASS\0" at 0x0B48), which is the same
    # shape as the direction words; the split attributes it to no db line, so
    # it uses a run locator verified against the raw bytes.
    ("U2_S_PASS",              "0940_sub_0940.asm", ("run", 0x0B48, 4, "PASS")),
    # Rejected move. asm/0A02 is a whole routine whose only content is the
    # str_out call plus its inline string, so the db-index locator is exact and
    # needs no hand-checked file offset.
    ("U2_S_INVALID_MOVE",      "0A02_invalid_move_msg.asm", 0),
    # CP5 C3 — the movement reject a paralysed character gets. asm/2520 tests DS:26B inside
    # the passability routine and, if set, prints this and takes the same reject return
    # (runtime 0x2538) an impassable tile uses. The db line carries a trailing 0x0d newline
    # glyph, which puts it outside DB_RE, so this is a run locator; the engine emits the
    # newline itself, as it does for --INVALID MOVE!.
    ("U2_S_MOVE_PARALYZED",    "2527_sub_2527.asm", ("run", 0x272A, 12, "--PARALIZED!")),
    # The Z status screen's labels (docs/status-screen-z.md § 7). Each is the inline string
    # of a str_out call inside the handler, so the offsets are the call site plus three.
    # Four of these files were named "shop_header_*" by the bulk wave on a caller that was
    # never found; they are now list_header_* (see the headers). The Z screen is the only
    # consumer demonstrated anywhere. The three run locators are the strings whose db
    # lines DB_RE cannot see: "A LEVEL " carries a leading 0x8d newline glyph, and the
    # SPELL- / bare-dash labels sit on multi-string db lines.
    ("U2_S_Z_A_LEVEL",         "4580_level_gain_msg.asm", ("run", 0x4784, 8, "A LEVEL ")),
    ("U2_S_Z_WEAPON",          "45C4_status_weapon.asm", 0),
    ("U2_S_Z_TORCHES",         "45E9_status_torches.asm", 0),
    ("U2_S_Z_ARMOUR",          "460A_status_armour.asm", 0),
    ("U2_S_Z_KEYS",            "462F_status_keys.asm", 0),
    ("U2_S_Z_SPELL",           "464D_sub_464d.asm",       ("exe_only", 0x4850, 6, "SPELL-")),
    ("U2_S_Z_TOOLS",           "466F_status_tools.asm", 0),
    ("U2_S_Z_WEAPONS_LIST",    "4748_list_header_weapons.asm", 0),
    ("U2_S_Z_ARMOUR_LIST",     "47A2_list_header_armour.asm", 0),
    ("U2_S_Z_SPELLS_LIST",     "47FA_list_header_spells.asm", 0),
    ("U2_S_Z_ITEMS_LIST",      "4853_list_header_items.asm", 0),
    # The list separators. The plural is a literal S and the dash is separate, because the
    # item loop prints the S for everything except index 3 (BOOTS) and the dash always.
    ("U2_S_Z_PLURAL_DASH",     "4771_sub_4771.asm", 0),
    ("U2_S_Z_PLURAL_S",        "4881_sub_4881.asm", 0),
    # The bare dash the item loop prints after the plural S is deliberately NOT extracted.
    # It is punctuation, not content — the same class as the space and the digits, which the
    # engine also emits as literals — and the asm tree holds it as `.byte` fragments, so an
    # extraction would either die in CI or need an exe-only hole for a hyphen.
    # The creation tail (asm/5400 end). Both strings print before the tail calls
    # the program-startup routine, which is why finishing creation lands on the
    # splash rather than the menu. They are stored as byte lists in the listing
    # (`db 0x49,0x4e,...`) rather than quoted strings, so the db-index locator
    # cannot see them and these use verified run offsets: the disassembler
    # mis-split "TO PLAY ULTIMA ][" into data fragments, so the true starts were
    # taken from the binary itself.
    ("U2_S_TO_PLAY",           "5400_sub_5400.asm", ("run", 0x5C1F, 17)),
    ("U2_S_INSERT_PROGRAM",    "5400_sub_5400.asm", ("run", 0x5C43, 21)),
    # CP5 W3 — the attack command's own words. Offsets verified against the exe on
    # 2026-10-23; each also names the asm file that documents it, so the provenance
    # is readable even where the fallback cannot reach (see the DB_RE note).
    ("U2_S_ATTACK",            "2B8F_cmd_A.asm", 0),
    ("U2_S_HIT",               "2BEC_combat_hit_msg.asm", 0),
    ("U2_S_MISS",              "2E09_combat_miss_msg.asm", 0),
    ("U2_S_PARALYZED",         "2BAD_attack_arms_refusal_to_cmd_done.asm", 0),
    # CP5 C5 — the force field (tile 0x5C). Both carry the leading 0x8d newline glyph, so the asm
    # fallback cannot reach them by index and they use the exe-offset form, like U2_S_KILLED above.
    # Offsets verified against the exe 2026-10-29; the strings live in asm/24B5_force_field_event.
    ("U2_S_FIELD_RING_PROTECTS", "24B5_force_field_event.asm", ("run", 0x26C0, 25, "RING PROTECTS FROM FIELD!")),
    ("U2_S_FIELD_DAMAGE",        "24B5_force_field_event.asm", ("run", 0x26E4, 25, "FIELD CAUSES 1000 DAMAGE!")),
    # `db 0x8d,"KILLED--GOLD+"` — the leading glyph byte puts it outside the asm
    # fallback, so it is exe-only, like the title strings above.
    ("U2_S_KILLED",            "2E3D_combat_attack_flow.asm", ("run", 0x2FA6, 13, "KILLED--GOLD+")),
    ("U2_S_EXP_PLUS",          "2E3D_combat_attack_flow.asm", 1),
    # CP5 W8 — the death tail. asm/0C8C writes `db " IS DEAD!",0x8d,0x00`, and the
    # trailing 0x8d (the game's newline glyph, which char_print masks to CR) puts the
    # line outside DB_RE, so this is exe-only. Offset verified against the exe on
    # 2026-10-27: file 0x0E8F is `20 49 53 20 44 45 41 44 21` followed by `8d 00`.
    # Length 9 stops before the 8d because the engine emits the newline itself.
    ("U2_S_IS_DEAD",           "0C8C_character_dead_msg.asm", ("run", 0x0E8F, 9, " IS DEAD!")),
    # CP5 W6 — the creature special's own words. Every one of these is exe-only for
    # the same reason as U2_S_IS_DEAD: the db line carries a trailing glyph byte
    # (0x0d or 0x8d, both of which char_print masks to CR) between the text and the
    # terminator, which puts it outside DB_RE. Offsets and lengths verified against
    # the exe on 2026-10-27; each is unique in the file, so no locator ambiguity.
    ("U2_S_LEGS_PARALYZED",    "0E70_combat_paralyzed_legs.asm", ("run", 0x1073, 15, "LEGS PARALIZED!")),
    ("U2_S_SAVED_BY_BOOTS",    "0E70_combat_paralyzed_legs.asm", ("run", 0x109E, 23, "SAVED BY MAGICAL BOOTS!")),
    ("U2_S_ARMS_PARALYZED",    "0EC2_combat_paralyzed_arms.asm", ("run", 0x10C5, 15, "ARMS PARALIZED!")),
    ("U2_S_SAVED_BY_CLOAK",    "0EC2_combat_paralyzed_arms.asm", ("run", 0x10F0, 22, "SAVED BY MAGICAL CLOAK")),
    ("U2_S_MAGIC_MISSILE",     "0F12_combat_magic_missile.asm",  ("run", 0x1115, 14, "MAGIC MISSILE!")),
    ("U2_S_SLEEP_SPELL",       "0F48_combat_sleep_spell.asm",    ("run", 0x114B, 12, "SLEEP SPELL!")),
    ("U2_S_SAVED_BY_IDOL",     "0F48_combat_sleep_spell.asm",    ("run", 0x1173, 14, "SAVED BY IDOL!")),
    ("U2_S_THIEF_STOLE",       "0FCD_combat_thief_steal.asm",    ("run", 0x11F8, 24, "A THIEF STOLE SOMETHING!")),
    # CP8 1a — Minax's ranged strike (asm/1040 -> asm/0F8E). exe-only for the same reason as
    # the specials above: the db carries a trailing 0x8d (the game's newline glyph) between the
    # text and the terminator, which puts it outside DB_RE. Offset/length read off asm/0F8E
    # (the db sits at file 0x1192, right after the `call L_0051df` at 0x118f) and confirmed
    # unique in the exe on 2026-11-12. Length 22 stops before the 8d; the engine emits the
    # newline itself, exactly as the other special-message callers do.
    ("U2_S_MINAX_CRIES",       "0F8E_combat_minax_attack.asm",   ("run", 0x1192, 22, "MINAX CRIES: DIE FOOL!")),
    # CP8 1b — Minax's teleport message (asm/2C2D gate -> asm/2C38 teleport). Proven live
    # 2026-11-12: a surviving hit on the type-0x40 creature swaps her mon_x/mon_y and prints
    # this. The db in asm/2C38 is `db 0x8d,"SHE'S GONE!!!",0x00` -- a leading 0x8d (the game's
    # newline glyph) before the text, so the engine emits the newline itself and the length
    # covers only the 13 text bytes at file 0x2e93 (confirmed unique in the exe).
    ("U2_S_SHE_GONE",          "2C38_minax_teleport_swap.asm",  ("run", 0x2e93, 13, "SHE'S GONE!!!")),
    # Site transitions (docs/sector-rulesets.md § 6). cmd_E's " WHAT?" is the
    # second of eleven copies of that literal in the file, so every one of these is
    # addressed by offset rather than by index; the offsets were read off the asm
    # fragments and then confirmed against the exe on 2026-10-27.
    ("U2_S_ENTER",             "3389_cmd_E.asm",          ("run", 0x358C, 5, "ENTER")),
    ("U2_S_ENTER_WHAT",        "3399_sub_3399.asm",       ("run", 0x359C, 6, " WHAT?")),
    ("U2_S_ONLY_ON_FOOT",      "33AE_move_only_on_foot.asm", ("run", 0x35B1, 14, "-ONLY ON FOOT!")),
    # CP8 1e — cmd_E's signpost arm (asm/3389 -> the art-10 arm -> asm/3525 -> asm/353C
    # reads DS:49 -> asm/3546 era dispatch). Pressing E on a signpost (art 0x0A, tile
    # 0x28) prints "-THE SIGN READS:" then the era label for the sector tens digit, on
    # the next line. Captured on frame from the original 2026-11-12 and matching the
    # static decode, including the leading dash and the 0x0d that breaks the line.
    # The five era labels are asm/3546's dec/je dispatch on DS:49: 0 LEGENDS, 1 9M BC,
    # 2 1423 BC, 3 1990 AD, >=4 2112 AD. All offsets confirmed unique in the exe on
    # 2026-11-12. "-THE SIGN READS:" carries a trailing 0x0d (the game's newline) before
    # the terminator, so the length stops before it and the engine emits the newline.
    ("U2_S_SIGN_READS",        "3525_sub_3525.asm",          ("run", 0x3728, 16, "-THE SIGN READS:")),
    ("U2_S_ANOS_LEGENDS",      "3566_sub_3566.asm",          ("run", 0x3768, 14, "ANOS: LEGENDS!")),
    # "ANOS: 9,000,000 B.C." is contiguous in the exe at 0x377d but the clean wave cut it
    # in half across asm/3577 (`db "ANOS:"`) and asm/3582 (`db " 9,000,000 B.C."`), so no
    # single asm file's data stream carries the whole literal and it cannot be proven
    # present in asm/. It is exe-only for that reason and named on UNPROVABLE below.
    ("U2_S_ANOS_9M_BC",        "3577_sub_3577.asm",          ("run", 0x377d, 20)),
    ("U2_S_ANOS_1423_BC",      "3596_show_year_1990.asm",    ("run", 0x3798, 15, "ANOS: 1423 B.C.")),
    ("U2_S_ANOS_1990_AD",      "3596_show_year_1990.asm",    ("run", 0x37ae, 15, "ANOS: 1990 A.D.")),
    ("U2_S_ANOS_2112_AD",      "3546_time_index_message.asm", ("run", 0x3752, 15, "ANOS: 2112 A.D.")),
    ("U2_S_DESCEND",           "334D_cmd_D.asm",          ("run", 0x3550, 7, "DESCEND")),
    ("U2_S_DESCEND_WHAT",      "334D_cmd_D.asm",          ("run", 0x3566, 6, "-WHAT?")),
    # cmd_I (IGNITE TORCH) and cmd_K (KLIMB), decoded 2026-10-29. Both handlers are cut by the
    # Phase 2 map: each is a one-byte `e8` in its own cmd_* file with the operand and the text in
    # the fragment after it, so these live in the fragment files, not in cmd_I/cmd_K. Those
    # fragments were renamed to match the role this decode established.
    #
    # The two refusals deliberately do NOT get their own entries. cmd_K's is `-WH` at asm/37F2
    # plus `AT?` at asm/380B -- one string straddling a fragment boundary -- so it cannot be
    # written as a single `run` with a working asm fallback, and a 3-tuple would emit an empty
    # literal in CI where the exe is absent. Its bytes are identical to U2_S_DESCEND_WHAT, so the
    # engine prints that; the distinct call site is recorded in asm/37F2's header instead.
    #
    # "NONE OWNED!" and "TO LEVEL " both carry a leading 0x8d line-marker byte in the binary.
    # The offsets below start AFTER it, so the exe path and the asm fallback agree byte for byte;
    # the callers print the CR explicitly, which is the same convention asm/3BE4 established.
    ("U2_S_IGNITE_TORCH",      "3795_ignite_torch_gate.asm", ("run", 0x3997, 12, "IGNITE TORCH")),
    ("U2_S_IGNITE_NONE_OWNED", "37AE_ignite_torch_burn.asm", ("run", 0x39B9, 11, "NONE OWNED!")),
    ("U2_S_KLIMB",             "37F2_klimb_msg.asm",        ("run", 0x39F4, 5, "KLIMB")),
    ("U2_S_TO_LEVEL",          "3823_site_floor_down.asm",  ("run", 0x3A2E, 9, "TO LEVEL ")),
    # CP7 CAST: the four literals on the cast path, added 2026-11-15 when cmd_C was wired.
    # The spell NAME is not one of them -- it comes from the indexed pool (U2_T_SPELLS) via the
    # printer at L_00528c, the same route cmd_M uses, so only the frame and the three refusals
    # are literals here.
    #
    # These are FILE offsets, which is what a `run` locator has always meant here despite its
    # name -- the first draft of these four used runtime addresses and the extractor read them as
    # file offsets, which produced "GOLD+" where "CAST-" belonged and nothing else. The asm
    # label base is also file-relative (see asm/310B_cmd_C.asm on why that matters), so the two
    # agree and only the name misleads.
    #
    # "-FAILED!" exists twice in the file: file 0x33D3 is the location gate (DS:4A < 4) and
    # 0x3435 is the shared arm refusal every spell arm jumps to. The engine prints one literal
    # for both, so the locator names the arm copy and the second call site is recorded in
    # asm/3139_cast_spell_flow.asm rather than as a second symbol.
    #
    # "NEED WAND OR STAFF!" and "NO SPELL!" both carry the leading 0x8d line marker, so their
    # offsets start after it, per the convention above.
    ("U2_S_CAST",              "310B_cmd_C.asm",            ("run", 0x330E,  5, "CAST-")),
    ("U2_S_CAST_NEED_WAND",    "3139_cast_spell_flow.asm",  ("run", 0x336B, 19, "NEED WAND OR STAFF!")),
    ("U2_S_CAST_NO_SPELL",     "3139_cast_spell_flow.asm",  ("run", 0x33A0,  9, "NO SPELL!")),
    ("U2_S_CAST_FAILED",       "3139_cast_spell_flow.asm",  ("run", 0x3435,  8, "-FAILED!")),
    # The two spell shoppes, added 2026-11-15 when the cleric shrine and the magic shoppe were
    # wired. Located the same way as the four above -- file offsets, each verified against the
    # executable to sit immediately after a near `call`, and against the asm listing for text.
    #
    # One constant per QUOTED LITERAL the asm tree has, not one per message the shop prints.
    # Both menus reach the player as a single blob whose line breaks are the game's in-band 0x8D
    # (`db 0x8d,"1-LIGHT, 2-L.D., 3-L.U.,",0x8d,"4-PASS..."`), and the asm-fallback proof below
    # needs the fourth element to appear as one quoted literal, so a blob cannot be one constant.
    # Splitting at the breaks is not a compromise: seam_char_print masks bit 7 and turns 0x0D into
    # a newline, so the engine printing CR then the fragment is the same observable line the
    # emitter produces from the blob. What would have been lost is a constant nobody can prove.
    #
    # "1-LIGHT, 2-L.D., 3-L.U.," is offered by BOTH shoppes and is byte-identical in both, so it
    # is one constant proven against the cleric file. "FIVE " and "S FOR " likewise exist once
    # per shoppe (the cleric's at 0x1B15 / 0x1B27, the wizard's at 0x1C5C / 0x1C6B).
    ("U2_S_SHOP_CLERIC_WELC",   "1869_cleric_shrine.asm",   ("run", 0x1A6D,  8, "WELCOME ")),
    ("U2_S_SHOP_CLERIC_BRO",    "1869_cleric_shrine.asm",   ("run", 0x1A80,  8, "BROTHER ")),
    ("U2_S_SHOP_CLERIC_SIS",    "1869_cleric_shrine.asm",   ("run", 0x1A8F,  7, "SISTER ")),
    ("U2_S_SHOP_MENU_LADDERS",  "1869_cleric_shrine.asm",   ("run", 0x1A9E, 24, "1-LIGHT, 2-L.D., 3-L.U.,")),
    ("U2_S_SHOP_CLERIC_LINE2",  "1869_cleric_shrine.asm",   ("run", 0x1AB7, 27, "4-PASS 5-SURFACE, 6-PRAYER.")),
    ("U2_S_SHOP_CLERIC_PROMPT", "1869_cleric_shrine.asm",   ("run", 0x1AD3, 15, "YOUR INTEREST? ")),
    ("U2_S_SHOP_CLERIC_BYE",    "1869_cleric_shrine.asm",   ("run", 0x1AF2, 25, "FOLLOW THE LIGHT, FRIEND!")),
    ("U2_S_SHOP_FIVE",          "1869_cleric_shrine.asm",   ("run", 0x1B15,  5, "FIVE ")),
    ("U2_S_SHOP_S_FOR",         "1869_cleric_shrine.asm",   ("run", 0x1B27,  6, "S FOR ")),
    ("U2_S_SHOP_CLERIC_YESQ",   "1869_cleric_shrine.asm",   ("run", 0x1B46, 13, "YES, FRIEND? ")),
    ("U2_S_SHOP_CLERIC_NO_A",   "1869_cleric_shrine.asm",   ("run", 0x1B63,  2, "NO")),
    ("U2_S_SHOP_CLERIC_NO_B",   "1869_cleric_shrine.asm",   ("run", 0x1B66, 20, "I'M SORRY, GOOD DAY.")),
    ("U2_S_SHOP_CLERIC_YES_A",  "1869_cleric_shrine.asm",   ("run", 0x1B81,  4, "YES,")),
    ("U2_S_SHOP_CLERIC_YES_B",  "1869_cleric_shrine.asm",   ("run", 0x1B86, 14, "I WILL TAKE 5!")),
    ("U2_S_SHOP_WIZ_WELCOME",   "19D7_magic_shop_menu.asm", ("run", 0x1BDB, 16, " MUSERREF OLDUM!")),
    ("U2_S_SHOP_WIZ_LINE2",     "19D7_magic_shop_menu.asm", ("run", 0x1C05, 24, "4-M.M., 5-BLINK, 6-KILL,")),
    ("U2_S_SHOP_WIZ_PROMPT",    "19D7_magic_shop_menu.asm", ("run", 0x1C1E, 15, "BIR IKI UC...? ")),
    ("U2_S_SHOP_WIZ_BYE",       "19D7_magic_shop_menu.asm", ("run", 0x1C3E, 13, "UGURLA OLSUN!")),
    ("U2_S_SHOP_WIZ_YESQ",      "19D7_magic_shop_menu.asm", ("run", 0x1C8D, 13, "LUTFEN EVET? ")),
    ("U2_S_SHOP_WIZ_NO",        "19D7_magic_shop_menu.asm", ("run", 0x1CAA,  3, "NO!")),
    ("U2_S_SHOP_WIZ_YES",       "19D7_magic_shop_menu.asm", ("run", 0x1CB3,  3, "YES")),
    # CP7: the dungeon-side command set. asm/277B is the fragment that holds ADVANCE,
    # RETREAT, TURN LEFT, TURN RIGHT and the trap, and it is one of the bulk-wave files
    # whose strings Phase 3 rendered as db lines rather than instructions, so every one of
    # these is addressed by offset. Offsets and lengths read off the exe 2026-10-29 and
    # each is confirmed unique in the file by --check.
    #
    # "ADVANCE" and "-BLOCKED!" are two separate str_out calls at two separate sites: the
    # command word prints first and the refusal appends, so the engine prints them as two
    # calls too rather than inventing a third literal "ADVANCE-BLOCKED!" that the binary
    # does not contain.
    ("U2_S_ADVANCE",           "277B_dungeon_move_commands.asm",       ("run", 0x2A65,  7, "ADVANCE")),
    ("U2_S_BLOCKED",           "277B_dungeon_move_commands.asm",       ("run", 0x2A77,  9, "-BLOCKED!")),
    ("U2_S_TURN_LEFT",         "277B_dungeon_move_commands.asm",       ("run", 0x2B16,  9, "TURN LEFT")),
    ("U2_S_TURN_RIGHT",        "277B_dungeon_move_commands.asm",       ("run", 0x2B48, 10, "TURN RIGHT")),
    ("U2_S_RETREAT",           "277B_dungeon_move_commands.asm",       ("run", 0x2B7B,  7, "RETREAT")),
    ("U2_S_TRAP",              "277B_dungeon_move_commands.asm",       ("run", 0x2ABE, 13, "ARGH! A TRAP!")),
    ("U2_S_TOOLS_ESCAPED",     "277B_dungeon_move_commands.asm",       ("run", 0x2AE9, 25, "ESCAPED! BY USE OF TOOLS!")),
    # asm/1151's two darkness lines both carry a trailing 0x8d line marker, so the db line
    # reads `db "IT'S DARK!",0x8d,0x00`; the offsets below stop before the marker and the
    # caller prints the CR, which is the asm/3BE4 convention used everywhere else.
    ("U2_S_ITS_DARK",          "1151_dungeon_turn.asm", ("run", 0x149F, 10, "IT'S DARK!")),
    ("U2_S_TORCH_BURNED_OUT",  "1151_dungeon_turn.asm", ("run", 0x14B7, 17, "TORCH BURNED OUT!")),
    # asm/12DB's three dungeon contact arms. Each is reached from runtime 0x1508 when a
    # creature's candidate step is the player's cell, gated on a roll under 0x40 and on the
    # creature's type: 2 blows the torch, 5 is the gremlin that steals food, 7 puts you to
    # sleep. All three carry the same 8d 00 tail as the two above, so the CR is part of the
    # emitted bytes and the caller must not add a second one.
    ("U2_S_TORCH_BLOWN_OUT",   "12DB_dungeon_events.asm", ("run", 0x15B5, 24, "YOUR TORCH IS BLOWN OUT!")),
    ("U2_S_GREMLIN_STOLE",     "12DB_dungeon_events.asm", ("run", 0x15DD, 26, "A GREMLIN STOLE SOME FOOD!")),
    ("U2_S_STRONG_MAGIC",      "12DB_dungeon_events.asm", ("run", 0x1614, 24, "YOU FEEL A STRONG MAGIC!")),
    # GET's site arm (asm/363B, runtime 0x36C3). " CHEST!" keeps its leading space because
    # the binary has it, and "IT CONTAINS " follows the same str_out call separated by the
    # 0x8d marker, so the caller emits the CR between them.
    ("U2_S_CHEST",             "363B_cmd_G.asm",          ("run", 0x38DC,  7, " CHEST!")),
    ("U2_S_IT_CONTAINS",       "363B_cmd_G.asm",          ("run", 0x38E4, 12, "IT CONTAINS ")),
    ("U2_S_GOLD_EXCL",         "363B_cmd_G.asm",          ("run", 0x391C,  5, "GOLD!")),
    ("U2_S_TRI_LITHIUM",       "363B_cmd_G.asm",          ("run", 0x395D, 12, "TRI-LITHIUM!")),
    ("U2_S_GET_A",             "363B_cmd_G.asm",          ("run", 0x3954,  2, "A ")),
    ("U2_S_GET",               "363B_cmd_G.asm",          ("run", 0x383E,  3, "GET")),
    ("U2_S_GET_WEAPON",        "363B_cmd_G.asm",          ("run", 0x3868,  7, " WEAPON")),
    ("U2_S_GET_ARMOUR",        "363B_cmd_G.asm",          ("run", 0x389E,  7, " ARMOUR")),
    ("U2_S_GET_EMPTY",         "363B_cmd_G.asm",          ("run", 0x3890,  6, " EMPTY!")),
    ("U2_S_GET_WHAT",          "363B_cmd_G.asm",          ("run", 0x385B,  6, " WHAT?")),
    # The gold-for-HP tribute: TRANSACT aimed at a creature of type 0x6C. Observed live
    # 2026-10-29 (captures/spice86/lord-british-tribute), all four rungs of the ladder.
    # The charge line carries a leading AND a trailing 0x8d line marker; the offsets below
    # start after the leading one and the caller prints the CRs, which is the asm/3BE4
    # convention and keeps the exe path and the asm fallback byte-identical.
    ("U2_S_TRIBUTE_WELCOME",   "4249_tribute_welcome.asm",  ("run", 0x444D, 17, "WELCOME MY CHILD ")),
    ("U2_S_TRIBUTE_CHARGE",    "4262_tribute_charge.asm",   ("run", 0x4466, 25, "FIRST MY 50 G.P. TRIBUTE!")),
    ("U2_S_TRIBUTE_RAISE",     "428E_tribute_raise.asm",    ("run", 0x4491, 24, "AND FOR IT I RAISE THEE ")),
    ("U2_S_SITE_TOWN",         "343F_sub_343f.asm",       ("run", 0x364A, 5, "-TOWN")),
    ("U2_S_SITE_TOWER",        "343F_sub_343f.asm",       ("run", 0x367F, 6, "-TOWER")),
    ("U2_S_SITE_CASTLE",       "34B5_sub_34b5.asm",       ("run", 0x36B8, 7, "-CASTLE")),
    ("U2_S_SITE_DUNGEON",      "34E4_site_enter_dungeon.asm", ("run", 0x36EF, 8, "-DUNGEON")),
    # The UNLOCK command (asm/42F1 cmd_U and its two refusals). The prompt's db line
    # terminates with `,0` so the db-index locator finds it; both refusals carry a
    # leading 0x8d line-marker byte, which puts them outside DB_RE on purpose (see the
    # DB_RE note), so they are addressed by offset. Offsets read off the exe 2026-10-28
    # while closing G30 -- each literal is unique in the file.
    ("U2_S_UNLOCK_DIRECTION",  "42F1_cmd_U.asm",          0),
    ("U2_S_NO_DOOR_THERE",     "4310_open_no_door.asm",   ("run", 0x4514, 14, "NO DOOR THERE!")),
    ("U2_S_NO_KEYS_FIT",       "4345_open_no_key_fit.asm", ("run", 0x4549, 17, "NO KEYS THAT FIT!")),
    # The two command handlers whose entire body is one message: asm/3782 cmd_H and
    # asm/37DC cmd_J each print a literal and `jmp L_000c33` (cmd_done). Added for the
    # command dispatcher, engine/src/game/commands.c. Both are `high` confidence in their own
    # headers, and both have since been seen executing — the 60-harvest note that used to sit
    # here was stale against a corpus that has grown twice; tools/cmd_handlers.py is the
    # authority for what has executed, and it is why this line no longer carries a count.
    ("U2_S_HYPER_WHAT",        "3782_cmd_H.asm",           0),
    ("U2_S_JUMP_WHEE",         "37DC_cmd_J.asm",           0),
    # cmd_Y: the yell prompt. The db line is `db "YELL WHAT?",0x8d,0`, and the trailing 0x8d
    # is the game's newline glyph (the same convention as " IS DEAD!" and the Q guards), so
    # the run locator takes the ten text bytes and the engine emits the newline itself.
    # Offset verified against the raw bytes at file 0x4745: `e8 9a 0a` is the str_out call and
    # the string starts immediately after it, terminated `...3f 8d 00`.
    ("U2_S_YELL_WHAT",         "4542_cmd_Y.asm", ("run", 0x4745, 10, "YELL WHAT?")),
    # cmd_M: the ready-a-spell prompt and the word that introduces its echo. The echo line
    # is followed by the pool printer, so what lands after "SPELL READY => " is the spell
    # NAME, not a number — see the correction in asm/3B92_cmd_M.asm, which used to describe
    # that call as "numeric out". Both literals are unique in the file.
    ("U2_S_MAGIC_SPELL_READY", "3B92_cmd_M.asm",           0),
    ("U2_S_SPELL_READY_TO",    "3B92_cmd_M.asm",           1),
    # cmd_W (WEAR ARMOUR) and cmd_V (VIEW). Both were static-only until 2026-10-26, when two
    # scripted captures put their entries in ExecutedInstructions; see their asm headers. The W
    # menu is digit-choice, structurally the same shape as cmd_R's weapon menu, which the owner
    # recalled correctly and the bytes confirm.
    #
    # Only the single-segment db lines are addressable by index. DB_RE matches `db "text",0x00`, so
    # the three multi-segment lines in these two handlers — the W menu (four quoted parts split by
    # 0x0d glyphs), the strength refusal, and V's helm line with its mid-string 0x8d — are outside
    # it and need run locators, the same mechanism docs note for the impassable-tile message. They
    # are deliberately NOT squeezed in here with wrong indices: a silently mis-indexed string makes
    # the engine print the wrong words and still build.
    ("U2_S_NOT_OWNED",         "43F3_cmd_W.asm",           0),
    ("U2_S_ARMOUR_READY",      "43F3_cmd_W.asm",           1),
    ("U2_S_VIEW_WHAT",         "437E_cmd_V.asm",           0),
    # The three multi-segment lines, via run locators. Each offset was read back from
    # original/ultimaii.exe rather than derived: the runtime label minus 0x200 gives the file
    # address of the `call`, and the string starts three bytes later. Verified 2026-10-26:
    #   0x45F6 len 78  'WEAR ARMOUR:\r1-CLOTH, 2-LEATHER, 3-CHAIN,\r4-PLATE, 5-REFLECT,
    #                  6-POWER,\rWHICH? '
    #   0x4696 len 38  ' <-THOU ART NOT\rSTRONG ENOUGH TO WEAR!'
    #   0x45A0 len 23  'VIEW\x8dWITH MAGICAL HELM!'   (0x8d is this binary's newline glyph,
    #                  mid-string, not the leading line-marker convention)
    # The helm line is stored as TWO entries because the engine emits the newline itself rather
    # than passing a raw 0x8d through str_out — the same convention already used for KILLED--GOLD+
    # and --PARALIZED!. 0x45A4 is the glyph byte, so the halves are 0x45A0+4 and 0x45A5+18.
    ("U2_S_VIEW_HELM_A",       "437E_cmd_V.asm", ("run", 0x45A0, 4)),
    ("U2_S_VIEW_HELM_B",       "437E_cmd_V.asm", ("run", 0x45A5, 18)),
    ("U2_S_WEAR_ARMOUR_MENU",  "43F3_cmd_W.asm", ("run", 0x45F6, 78)),
    ("U2_S_NOT_STRONG_ENOUGH", "43F3_cmd_W.asm", ("run", 0x4696, 38)),
    # Two slices of the WEAR ARMOUR menu, so enhanced mode can compose a filtered list from the
    # original's own pieces instead of hand-copying game text into C. Offsets read back from the
    # exe: the header is the first 12 bytes of the menu run at 0x45F6, and "WHICH? " is the last 7
    # at 0x463D. The entry names themselves come from U2_T_ARMOUR, which is the pool at 0x501D.
    ("U2_S_WEAR_HEADER",       "43F3_cmd_W.asm", ("run", 0x45F6, 12)),
    ("U2_S_WHICH",             "43F3_cmd_W.asm", ("run", 0x463D, 7)),
    # The vehicles: cmd_B (BOARD, asm/2F6F -> asm/2FAD) and cmd_L (LAUNCH, asm/38B0).
    #
    # Every one of these is addressed by offset, not by index, for two reasons. Several
    # literals repeat -- " PLANE" appears four times in 0x3000..0x4000 and " WHAT?" eight
    # times in the file -- and locate() searching forward from the function's base would
    # land on an earlier copy and still report success, because the bytes match. The other
    # reason is the multi-segment lines: " SHIP",0x8d,"THE CREW..." and friends are one
    # str_out call with the binary's newline glyph mid-string, which DB_RE does not match.
    #
    # Offsets were read back from original/ultimaii.exe on 2026-10-30 the same way as the
    # cmd_W ones: the asm label is the file address of the `call str_out`, and the literal
    # starts three bytes later. Each was confirmed unique at its own address.
    #
    # BOARD's arms, in dispatch order (asm/2FAD): the refusal pairs are what the original
    # prints when the gating item is missing, and each names its own item -- "YOU MUST HAVE
    # AN ANKH!" sits behind DS:DD, which is items_held[7], and
    # docs/spice86-live-findings.md § 11 calls slot 7 ANKH. Same for SKULL KEY (DS:DF),
    # BRASS BUTTON (DS:E1), BLUE TASSLE (DS:E2) and TRI-LITHIUM (DS:E5).
    ("U2_S_BOARD",                 "2F6F_cmd_B.asm",            ("run", 0x3172,  5, "BOARD")),
    ("U2_S_BOARD_WHAT",            "2FB8_offer_what_msg.asm",   ("run", 0x31C3,  6, " WHAT?")),
    ("U2_S_BOARD_HORSE",           "2FD6_vehicle_horse.asm",    ("run", 0x31D9,  6, " HORSE")),
    ("U2_S_BOARD_SHIP_A",          "2FFE_vehicle_ship.asm",     ("run", 0x3201,  5, " SHIP")),
    ("U2_S_BOARD_SHIP_B",          "2FFE_vehicle_ship.asm",     ("run", 0x3207, 21, "THE CREW OF THIS SHIP")),
    ("U2_S_BOARD_SHIP_C",          "2FFE_vehicle_ship.asm",     ("run", 0x321D, 23, "WILL NOT LET YOU BOARD!")),
    ("U2_S_BOARD_FRIGATE",         "3038_vehicle_frigate.asm",  ("run", 0x323C,  8, " FRIGATE")),
    ("U2_S_BOARD_PLANE_REF_A",     "3048_vehicle_plane.asm",    ("run", 0x3264,  6, " PLANE")),
    ("U2_S_BOARD_PLANE_REF_B",     "3048_vehicle_plane.asm",    ("run", 0x326B, 25, "STRANGE YOU CAN'T GET IN!")),
    ("U2_S_BOARD_PLANE",           "3048_vehicle_plane.asm",    ("run", 0x328C,  6, " PLANE")),
    ("U2_S_BOARD_ROCKET",          "3048_vehicle_plane.asm",    ("run", 0x32AD,  7, " ROCKET")),
    ("U2_S_BOARD_ROCKET_REF_A",    "3048_vehicle_plane.asm",    ("run", 0x32C0, 25, "A METALIC VOICE COMMANDS:")),
    ("U2_S_BOARD_ROCKET_REF_B",    "3048_vehicle_plane.asm",    ("run", 0x32DA, 22, "YOU MUST HAVE AN ANKH!")),
    # LAUNCH, asm/38B0. Note the two metallic-voice lines are DIFFERENT strings: cmd_B's
    # says "A METALIC VOICE COMMANDS:" (sic, one L) and cmd_L's says "A METALLIC VOICE
    # SAYS:" (two Ls, different verb). They are not one message and must not be merged.
    ("U2_S_LAUNCH_WHAT",           "38B0_cmd_L.asm",            ("run", 0x3AC2, 12, "LAUNCH WHAT?")),
    ("U2_S_LAUNCH_ROCKET",         "38B0_cmd_L.asm",            ("run", 0x3AD5, 14, "LAUNCH--ROCKET")),
    ("U2_S_LAUNCH_ROCKET_REF_A",   "38B0_cmd_L.asm",            ("run", 0x3AEF, 22, "A METALLIC VOICE SAYS:")),
    ("U2_S_LAUNCH_ROCKET_REF_B",   "38B0_cmd_L.asm",            ("run", 0x3B06, 25, "SHIP INCAPABLE OF LAUNCH!")),
    ("U2_S_LAUNCH_ROCKET_GO",      "38B0_cmd_L.asm",            ("run", 0x3B27, 19, "PREPARE FOR LAUNCH!")),
    ("U2_S_PRESS_ANY_KEY",         "38B0_cmd_L.asm",            ("run", 0x3B84, 15, "(PRESS ANY KEY)")),
    ("U2_S_LAUNCH_PLANE",          "38B0_cmd_L.asm",            ("run", 0x3BFD, 13, "LAUNCH--PLANE")),
    ("U2_S_LAUNCH_PLANE_REF_A",    "38B0_cmd_L.asm",            ("run", 0x3C16, 19, "FUNNY THIS PLANE IS")),
    ("U2_S_LAUNCH_PLANE_REF_B",    "38B0_cmd_L.asm",            ("run", 0x3C2A, 23, "MISSING A BRASS BUTTON!")),
    ("U2_S_LAND_PLANE",            "38B0_cmd_L.asm",            ("run", 0x3D5D, 11, "LAND PLANE.")),
    ("U2_S_LAND_NOT_HERE",         "38B0_cmd_L.asm",            ("run", 0x3D75, 19, "LAND--NOT HERE!!!!!")),
    # The in-space rocket layer (item #1 of docs/archive/handoff-2026-11-03.md). asm/6999 is the
    # space command loop, asm/6DFD the hyperwarp arm, asm/7030 the orbit determination and
    # the dive. Every offset below was measured by searching original/ultimaii.exe for the
    # literal bytes (not derived from label arithmetic), and each is unique in the binary
    # except the four noted: LEFT/RIGHT also occur in the cmd_V helm text and XENO=/YAKO=/
    # ZABO= also occur in the readout with two leading spaces -- the space-loop copies are
    # the ones at 0x6cdb/0x6cf3 and 0x7103/0x7112/0x7122. Leading/trailing 0x8d newline
    # glyphs are excluded from the offset and length; the engine emits the newline itself.
    ("U2_S_SPACE_FUEL",            "6999_space_status.asm",     ("run", 0x6BF0,  7, "  FUEL=")),
    ("U2_S_SPACE_XENO",            "6999_space_status.asm",     ("run", 0x6C0A,  7, "  XENO=")),
    ("U2_S_SPACE_YAKO",            "6999_space_status.asm",     ("run", 0x6C24,  7, "  YAKO=")),
    ("U2_S_SPACE_ZABO",            "6999_space_status.asm",     ("run", 0x6C3E,  7, "  ZABO=")),
    ("U2_S_SPACE_LEFT",            "6999_space_status.asm",     ("run", 0x6CDB,  4, "LEFT")),
    ("U2_S_SPACE_RIGHT",           "6999_space_status.asm",     ("run", 0x6CF3,  5, "RIGHT")),
    ("U2_S_SPACE_CLIMB",           "6999_space_status.asm",     ("run", 0x6D0D,  5, "CLIMB")),
    ("U2_S_SPACE_DIVE",            "6999_space_status.asm",     ("run", 0x6D27,  4, "DIVE")),
    ("U2_S_SPACE_EXPLODED",        "6999_space_status.asm",     ("run", 0x6C5F, 18, "YOU HAVE EXPLODED!")),
    ("U2_S_HYPERWARP_ENGAGED",     "6DFD_hyperwarp_flow.asm",   ("run", 0x7000, 18, "HYPERWARP ENGAGED!")),
    ("U2_S_HYPERWARP_NO_FUEL",     "6DFD_hyperwarp_flow.asm",   ("run", 0x70CF,  8, "NO FUEL!")),
    ("U2_S_HYPERWARP_OFF_COURSE",  "6DFD_hyperwarp_flow.asm",   ("run", 0x7096, 16, "SHIP OFF COURSE!")),
    ("U2_S_HYPERWARP_TO",          "6DFD_hyperwarp_flow.asm",   ("run", 0x70F5, 13, "HYPERWARP TO:")),
    ("U2_S_HYPERWARP_XENO",        "6DFD_hyperwarp_flow.asm",   ("run", 0x7103,  5, "XENO=")),
    ("U2_S_HYPERWARP_YAKO",        "6DFD_hyperwarp_flow.asm",   ("run", 0x7112,  6, " YAKO=")),
    ("U2_S_HYPERWARP_ZABO",        "6DFD_hyperwarp_flow.asm",   ("run", 0x7122,  6, " ZABO=")),
    ("U2_S_HYPERWARP_PREPARE",     "6DFD_hyperwarp_flow.asm",   ("run", 0x7133, 22, "PREPARE FOR HYPERWARP!")),
    ("U2_S_ORBIT_DEEP_SPACE",      "7030_space_collision.asm",  ("run", 0x7291, 22, "YOU ARE IN DEEP SPACE.")),
    ("U2_S_ORBIT_ORBITING",        "7030_space_collision.asm",  ("run", 0x72B9, 17, "YOU ARE ORBITING ")),
    ("U2_S_ORBIT_SUN",             "7030_space_collision.asm",  ("run", 0x724D, 14, "U HIT THE SUN!")),
    ("U2_S_WORLD_EARTH",           "7030_space_collision.asm",  ("run", 0x72F8,  6, "EARTH.")),
    ("U2_S_WORLD_MERCURY",         "7030_space_collision.asm",  ("run", 0x7306,  8, "MERCURY.")),
    ("U2_S_WORLD_VENUS",           "7030_space_collision.asm",  ("run", 0x7316,  6, "VENUS.")),
    ("U2_S_WORLD_MARS",            "7030_space_collision.asm",  ("run", 0x7324,  5, "MARS.")),
    ("U2_S_WORLD_JUPITER",         "7030_space_collision.asm",  ("run", 0x7331,  8, "JUPITER.")),
    ("U2_S_WORLD_SATURN",          "7030_space_collision.asm",  ("run", 0x7341,  7, "SATURN.")),
    ("U2_S_WORLD_URANUS",          "7030_space_collision.asm",  ("run", 0x7350,  7, "URANUS.")),
    ("U2_S_WORLD_NEPTUNE",         "7030_space_collision.asm",  ("run", 0x735F,  8, "NEPTUNE.")),
    ("U2_S_WORLD_PLUTO",           "7030_space_collision.asm",  ("run", 0x736F,  6, "PLUTO.")),
    ("U2_S_DIVE_REQUESTED",        "7030_space_collision.asm",  ("run", 0x7395, 18, "LANDING REQUESTED!")),
    ("U2_S_DIVE_DENIED",           "7030_space_collision.asm",  ("run", 0x73B6, 15, "REQUEST DENIED!")),
    # asm/44D4 (X-IT, the dismount). " WHAT?" here is the on-foot refusal, a different site
    # from the identical text in cmd_E and cmd_B; the extractor keeps one entry per site so a
    # missing string names the handler that lost it.
    ("U2_S_XIT",                   "44D4_cmd_X.asm",            ("run", 0x46D7, 4,  "X-IT")),
    ("U2_S_XIT_WHAT",              "44D4_cmd_X.asm",            ("run", 0x46E6, 6,  " WHAT?")),
    ("U2_S_XIT_NOT_HERE",          "44D4_cmd_X.asm",            ("run", 0x4715, 10, "-NOT HERE!")),
    # cmd_T (TRANSACT) and the shops it reaches. See docs/commerce.md and engine/src/game/
    # commerce.c for what each one is.
    #
    # The three index-addressed ones are single-segment db lines. Everything else here is a
    # run locator because the shop literals are multi-segment (the menus carry the binary's
    # newline glyph between lines) or carry a leading glyph byte, and the engine emits the
    # newline itself rather than passing a raw 0x0d/0x8d through str_out — same convention as
    # U2_S_VIEW_HELM_A/B. Offsets were read back from original/ultimaii.exe and each was
    # confirmed to be the bytes named, at that address.
    ("U2_S_TRANSACT",              "4050_cmd_T.asm",  0),
    ("U2_S_NO_RESPONSE",           "40A6_yell_no_response.asm", 0),
    # The street_* typed responses (asm/40CB's creature-type chain, reached from cmd_T when a
    # slot's role byte is below 0x80). Each literal is split in the listing around an embedded
    # 0x0d -- `db "A GUARD SAYS:",0x0d,"PAY YOUR TAXES!",0x00` -- so neither locator form can
    # carry it in both modes: the db-index locator reports "has 0 strings" (no single trailing
    # literal) and a 4-tuple run locator's asm fallback searches for its text as ONE quoted
    # literal and cannot find it. These are therefore exe-only 3-tuples, the same shape as
    # U2_S_VIEW_HELM_A/B, which exist for the identical reason. Without a copy of the game the
    # street lines are empty; with one they are the original's bytes.
    #
    # Addresses are FILE offsets (runtime + 0x200); each is the byte after the 3-byte
    # `call str_out` at the handler's entry, and each length excludes the terminator.
    #
    # None of these seven has ever been observed executing -- every header reads "entry never
    # appears in ExecutedInstructions of any of the 60 harvests" -- so the strings and the
    # chain order are the evidence, and the order is read from the bytes: 0x6C tribute, 0x60
    # guard, 0x64 jester, 0x68 merchant, 0xF0 fighter, 0xF4 cleric, 0xF8 wizard, 0xFC thief.
    #
    # Note asm/4108's own `# name-evidence:` line says 'A JESTER SAYS:' while the db line it
    # cites says "A JESTER SINGS:". The bytes win; the header is a hypothesis.
    ("U2_S_STREET_GUARD",          "40E0_street_guard.asm",    ("run", 0x42E3, 29)),
    ("U2_S_STREET_JESTER",         "4108_street_jester.asm",   ("run", 0x430B, 25)),
    ("U2_S_STREET_MERCHANT",       "412C_street_merchant.asm", ("run", 0x432F, 40)),
    ("U2_S_STREET_FIGHTER",        "415F_street_fighter.asm",  ("run", 0x4362, 30)),
    ("U2_S_STREET_CLERIC",         "4188_street_cleric.asm",   ("run", 0x438B, 23)),
    ("U2_S_STREET_WIZARD",         "41AA_street_wizard.asm",   ("run", 0x43AD, 36)),
    ("U2_S_STREET_THIEF",          "41D9_street_thief.asm",    ("run", 0x43DC, 38)),
    ("U2_S_DONT_HAVE_THAT_MUCH",   "2052_gold_spend_price.asm", 0),
    # The four shop bodies live in asm/19D7 (the G22 boundary defect: one file, several
    # routines). Armour first, then weapons, then transport.
    ("U2_S_ARMOUR_MENU",           "1D1D_shop_menus.asm",        ("run", 0x1F21, 97, "     THE ARMOUR SHOPPE:")),
    ("U2_S_SHOP_THANKS",           "1D1D_shop_menus.asm",        ("run", 0x1F92, 23, "THANKS FOR COMING BY...")),
    ("U2_S_SHOP_AH_YES",           "1D1D_shop_menus.asm",        ("run", 0x1FB3,  9, "AH! YES! ")),
    ("U2_S_SHOP_FOR_YOU_ONLY",     "1D1D_shop_menus.asm",        ("run", 0x1FC9, 14, "FOR YOU ONLY ")),
    ("U2_S_SHOP_HOW_BOUT",         "1D1D_shop_menus.asm",        ("run", 0x1FF1, 15, "HOW 'BOUT IT? ")),
    ("U2_S_SHOP_OH_WELL",          "1D1D_shop_menus.asm",        ("run", 0x201E,  9, "OH, WELL.")),
    ("U2_S_SHOP_SOLD",             "1D1D_shop_menus.asm",        ("run", 0x2032,  5, "SOLD!")),
    ("U2_S_WEAPONS_MENU",          "1D1D_shop_menus.asm",        ("run", 0x2053, 85, "  THE WEAPONS SHOPPE:")),
    ("U2_S_SHOP_AH_YES_A",         "1D1D_shop_menus.asm",        ("run", 0x20D9, 11, "AH! YES! A ")),
    ("U2_S_TRANSPORT_MENU",        "1D1D_shop_menus.asm",        ("run", 0x2179, 51, "WELCOME, TO THE TRANSPORT")),
    ("U2_S_TRANSPORT_DEAL",        "1D1D_shop_menus.asm",        ("run", 0x21C3, 21, "GOLD, DEAL, OK? --- ")),
    ("U2_S_TRANSPORT_BYE",         "1D1D_shop_menus.asm",        ("run", 0x21F2, 17, "O.K. BYE THEN...")),
    ("U2_S_TRANSPORT_RIDE",        "1D1D_shop_menus.asm",        ("run", 0x220E, 13, "RIDE SWIFTLY!")),
    # The clue seller's pool (asm/1B6B_clue_pool.asm), ten NUL-separated two-line clues. The
    # digit the player pays IS the index and the price x100, so entry 0 is free and entry 9 costs
    # 900. Each literal carries the emitter's newline glyph (0x8d) in the middle, so the listing
    # splits the payload around it -- `db "ASK ME NO QUESTIONS,",0x8d,"I'LL TELL YOU NO LIES.",0x00`
    # -- and the raw-text fallback could not see it as one quoted literal. The data-stream proof
    # added for these is what makes them resolve from asm/ with no copy of the game present, the
    # same bytes `--verify` proves them against.
    ("U2_S_CLUE_MENU",             "19D7_magic_shop_menu.asm",   ("run", 0x1CE4, 87, "TIME AND TRAVEL MADE ME WISE,\rPRECIOUS GOLD, A CLUE IT BUYS!HOW MUCH WILL YOU PAY *100?")),
    ("U2_S_CLUE_0",                "1B6B_clue_pool.asm",         ("run", 0x1D6B, 43, "ASK ME NO QUESTIONS,\x8dI'LL TELL YOU NO LIES.")),
    ("U2_S_CLUE_1",                "1B6B_clue_pool.asm",         ("run", 0x1D97, 44, "THE QUEEN IS THE KING\x8dAND THE KING IS A SPY.")),
    ("U2_S_CLUE_2",                "1B6B_clue_pool.asm",         ("run", 0x1DC4, 36, "THE EVIL MEN DO\x8dIS A HORRIBLE THING.")),
    ("U2_S_CLUE_3",                "1B6B_clue_pool.asm",         ("run", 0x1DE9, 49, "BUT DISPELLING MEANS\x8dTHAT YOU MUST HAVE THE RING.")),
    ("U2_S_CLUE_4",                "1B6B_clue_pool.asm",         ("run", 0x1E1B, 31, "JUST WHERE IT IS,\x8dI CANNOT SAY.")),
    ("U2_S_CLUE_5",                "1B6B_clue_pool.asm",         ("run", 0x1E3B, 51, "BUT I'LL TELL YOU RIGHT NOW,\x8dTHERE IS NO OTHER WAY.")),
    ("U2_S_CLUE_6",                "1B6B_clue_pool.asm",         ("run", 0x1E6F, 43, "I HEAR IN A TOWN\x8dWHERE THE WATER RUNS FREE.")),
    ("U2_S_CLUE_7",                "1B6B_clue_pool.asm",         ("run", 0x1E9B, 40, "THERE IS AN OLD MAN\x8dLIVING UNDER A TREE.")),
    ("U2_S_CLUE_8",                "1B6B_clue_pool.asm",         ("run", 0x1EC4, 41, "HE HAS NOT A NAME\x8dBUT HE DOES HAVE A CLUE")),
    ("U2_S_CLUE_9",                "1B6B_clue_pool.asm",         ("run", 0x1EEE, 46, "MAYHAPS IF YOU FIND HIM,\x8dHE'LL TELL IT TO YOU!")),
    # The grocery quote (asm/1799 -> asm/17B2 -> asm/17B7 -> asm/17C5) and its two answers.
    ("U2_S_FOOD_COSTS",            "1799_tavern_food_price.asm", 0),
    ("U2_S_FOOD_PER100",           "17C5_sub_17c5.asm",        ("run", 0x19C9, 19, "PER 100, WANT ONE? ")),
    ("U2_S_FOOD_NO",               "17E9_sub_17e9.asm",        ("run", 0x19EC,  2, "NO")),
    # The grocery's yes-answer has no asm fallback at all: asm/17F5 is a bulk-wave fragment
    # that still renders the literal as `.byte` data, so there is no quoted text to search for
    # and these two come out empty without the user's copy. Callers guard, the same way they
    # guard U2_T_* when the name pool is absent.
    ("U2_S_FOOD_YES_A",            "17F5_sub_17f5.asm",        ("run", 0x19F5,  4)),
    ("U2_S_FOOD_YES_B",            "17F5_sub_17f5.asm",        ("run", 0x19FA, 27)),
    ("U2_S_THANK_YOU_AGAIN",       "181A_tavern_thanks.asm",     0),
    # The pub (asm/15E4, the west/south quadrant of a town; asm/15E1 is the menu body).
    #
    # asm/15E1's menu literal is ONE db line split on the game's newline glyph --
    # `db "WELCOME TO THE PUB!",0x0d,"1-BUY, 2-TIP --- ",0x00` -- so each line is its own symbol
    # and the engine prints the newline itself, the same convention as the cleric and wizard
    # menus. Each is a 4-tuple because each half IS a single quoted literal in the listing, which
    # is exactly what the asm fallback searches for; the whole-line form could never be found that
    # way, which is why the shop menus above are exe-only.
    #
    # The ten barkeep entries are single-segment db lines in asm/16BD, but that file tags them
    # `# entry N` and not `# string`, so DB_RE matches none of them and the int locator would
    # report the file as holding zero strings. The 4-tuple form reaches them without widening
    # DB_RE, which is the hazard that comment documents.
    #
    # Offsets are FILE offsets (runtime + 0x200), each one the byte after its own `call str_out`
    # or the first byte of the entry, and each length excludes the terminator. Read back from
    # original/ultimaii.exe and confirmed to be the bytes named.
    ("U2_S_PUB_WELCOME",           "15E1_tavern_menu.asm",       ("run", 0x17E8, 19, "WELCOME TO THE PUB!")),
    ("U2_S_PUB_MENU",              "15E1_tavern_menu.asm",       ("run", 0x17FC, 17, "1-BUY, 2-TIP --- ")),
    ("U2_S_PUB_STRONG_ONE",        "15E1_tavern_menu.asm",       ("run", 0x182D, 24, "HERE, HAVE A STRONG ONE!")),
    ("U2_S_PUB_TIP_HOW",           "15E1_tavern_menu.asm",       0),
    ("U2_S_PUB_BARKEEP_SAYS",      "15E1_tavern_menu.asm",       ("run", 0x188D, 17, "THE BARKEEP SAYS:")),
    ("U2_S_BARKEEP_0",             "16BD_barkeep_chatter_hints.asm", ("run", 0x18BD,  8, "CAUGH...")),
    ("U2_S_BARKEEP_1",             "16BD_barkeep_chatter_hints.asm", ("run", 0x18C6, 24, "ISN'T THIS A GREAT GAME?")),
    ("U2_S_BARKEEP_2",             "16BD_barkeep_chatter_hints.asm", ("run", 0x18DF,  8, "HHMMM...")),
    ("U2_S_BARKEEP_3",             "16BD_barkeep_chatter_hints.asm", ("run", 0x18E8, 31, "SOME FIGHTERS WEAR MAGIC HELMS!")),
    ("U2_S_BARKEEP_4",             "16BD_barkeep_chatter_hints.asm", ("run", 0x1908, 24, "AVIATORS USE SKULL KEYS!")),
    ("U2_S_BARKEEP_5",             "16BD_barkeep_chatter_hints.asm", ("run", 0x1921, 26, "SAYLORS WEAR BLUE TASSLES!")),
    ("U2_S_BARKEEP_6",             "16BD_barkeep_chatter_hints.asm", ("run", 0x193C, 28, "MAGES CARRY WANDS OR STAFFS!")),
    ("U2_S_BARKEEP_7",             "16BD_barkeep_chatter_hints.asm", ("run", 0x1959, 18, "GUARDS CARRY KEYS!")),
    ("U2_S_BARKEEP_8",             "16BD_barkeep_chatter_hints.asm", ("run", 0x196C, 17, "ANKHS OPEN SPACE!")),
    ("U2_S_BARKEEP_9",             "16BD_barkeep_chatter_hints.asm", ("run", 0x197E, 26, "PLANES NEED BRASS BUTTONS!")),
    # cmd_O (OFFER GOLD), asm/3C2E. Nine strings, in the order they appear in the file,
    # which is also the order of the handler's arms: the aim prompt, the two refusals, the
    # amount question, then the four special-recipient lines and the stat-raise line. The
    # whole special path is gated on sector 32 (see asm/3C2E_cmd_O.asm's header), so these
    # are the lines the Hotel California grind and the ring/quicksword chain print.
    ("U2_S_OFFER_GOLD_DIRECT",     "3C2E_cmd_O.asm", 0),
    ("U2_S_OFFER_TO_WHOM",         "3C2E_cmd_O.asm", 1),
    ("U2_S_OFFER_HOW_MUCH",        "3C2E_cmd_O.asm", 2),
    ("U2_S_OFFER_THANK_YOU",       "3C2E_cmd_O.asm", 3),
    ("U2_S_OFFER_ENILNO",          "3C2E_cmd_O.asm", 4),
    ("U2_S_OFFER_EARN_RING",       "3C2E_cmd_O.asm", 5),
    ("U2_S_OFFER_RING_IS_YOURS",   "3C2E_cmd_O.asm", 6),
    ("U2_S_OFFER_HERE_TAKE_THIS",  "3C2E_cmd_O.asm", 7),
    ("U2_S_OFFER_ALAKAZAM",        "3C2E_cmd_O.asm", 8),
    # cmd_R (READY WEAPON) and cmd_N (NEGATE TIME). Both were first observed executing on
    # 2026-10-29 in captures/spice86/cmd-nr-live, which is also where every branch below was
    # seen on a frame rather than only in the listing.
    ("U2_S_READY_WEAPON_MENU",     "3E50_cmd_R.asm",             ("run", 0x4053, 75, "READY WEAPON:")),
    ("U2_S_READY_NOT_AGILE_A",     "3ED4_ready_weapon_agility_gate.asm", ("run", 0x40E6, 16, " <-THOU ART NOT ")),
    ("U2_S_READY_NOT_AGILE_B",     "3ED4_ready_weapon_agility_gate.asm", ("run", 0x40F7, 22, "AGILE ENOUGH TO WIELD!")),
    ("U2_S_READY_OK",              "3ED4_ready_weapon_agility_gate.asm", 0),
    ("U2_S_NEGATE_TIME",           "3BCE_cmd_N.asm",             0),
    ("U2_S_NEGATE_NO_CHARGES",     "3BE4_sub_3be4.asm",          ("run", 0x3DED, 19, "YOU'RE NOT EINSTEIN")),
    ("U2_S_NEGATE_HOW",            "3BE4_sub_3be4.asm",          ("run", 0x3DE7,  5, " HOW?")),
    ("U2_S_COIN_RUB",              "3C04_coin_rub.asm",          ("run", 0x3E14, 17, "YOU RUB A COIN...")),
    # cmd_S (STEAL). The five literals are the whole of what the command can say: one refusal and
    # three loot lines, and the fourth branch of the table prints nothing at all because it is
    # refused earlier. See asm/3F5D_steal_attempt.asm for the table.
    ("U2_S_STEAL_DIRECT",          "3F25_cmd_S.asm",             0),
    ("U2_S_STEAL_NO_LUCK",         "3F5D_steal_attempt.asm",     0),
    ("U2_S_STEAL_FOOD",            "3F5D_steal_attempt.asm",     1),
    ("U2_S_STEAL_ARMOUR",          "3F5D_steal_attempt.asm",     2),
    ("U2_S_STEAL_WEAPONS",         "3F5D_steal_attempt.asm",     3),
    # cmd_F (FIRE). Three literals, and the third is the whole off-ship command.
    ("U2_S_FIRE",                  "35C1_cmd_F.asm",             0),
    ("U2_S_FIRE_DIRECT",           "35DE_sub_35de.asm",          0),
    ("U2_S_FIRE_WHAT",             "35D0_sub_35d0.asm",          0),

    # CP8: the victory sequence, at the head of asm/72A0. These four need the "run" locator
    # because Phase 3 renders the whole 72A0 region as `db` hex -- the block sits past the
    # highest offset any capture ever executed, so the disassembler never saw it as code and
    # the ordinary db-index locator cannot see inside it. Same reason cmd_I, cmd_D and cmd_K
    # needed it. Offsets are file offsets, each string beginning immediately after its own
    # `call str_out`, and 0x8d is the emitter's CR.
    ("U2_S_MINAX_DEAD",            "72A0_sub_72a0.asm", ("run", 0x74A9, 48)),
    ("U2_S_STRANGE_FORCE",         "72A0_sub_72a0.asm", ("run", 0x751A, 26)),
    ("U2_S_WIN_UNIVERSE",          "72A0_sub_72a0.asm", ("run", 0x7552, 91)),
    ("U2_S_WIN_FOUND_IN",          "72A0_sub_72a0.asm", ("run", 0x75B1, 29)),
]

# The terminator is matched as `0x00` OR `0` because the asm tree is not uniform:
# asm/2B8F_cmd_A writes `db "ATTACK--",0` while the bulk wave writes `,0x00`.
# Narrowing this to one form silently drops a string from the fallback path, which
# is worse than a build error because the engine then prints an empty word.
# A leading `0xNN,` glyph byte (asm/2E3D's `db 0x8d,"KILLED--GOLD+"`) is NOT
# matched on purpose: accepting it would renumber the string indices of every file
# that has one, silently pointing existing manifest entries at the wrong string.
# That string is addressed by offset instead.
DB_RE = re.compile(r'^\s*db "((?:[^"\\]|\\.)*)",(?:0x00|0)\s*#\s*string')
FUNC_RE = re.compile(r'^#\s*func\s+([0-9A-Fa-f]{4})\.\.([0-9A-Fa-f]{4})')


def decode_gas_string(s):
    """Decode a gas db payload (the asm tree writes plain ASCII plus \\xNN)."""
    out = bytearray()
    i = 0
    while i < len(s):
        c = s[i]
        if c == "\\":
            nxt = s[i + 1] if i + 1 < len(s) else ""
            if nxt in ("x", "X"):
                out.append(int(s[i + 2:i + 4], 16))
                i += 4
                continue
            out.extend(bytes(s[i:i + 2], "latin-1").decode("unicode_escape").encode("latin-1"))
            i += 2
            continue
        out.append(ord(c))
        i += 1
    return bytes(out)


# Six committed strings cannot be proven against asm/, and the list is a ratchet
# rather than a fact to keep current: a seventh name is a red, and if someone
# ever re-decodes the four files involved the gate gets stronger and says so.
# In every case the file the manifest names has no `db` data at all (or 15 bytes
# of it), because the disassembler read the string as instructions; the bytes are
# still in asm/ and still assemble byte-exactly, but there is no text form for a
# locator to match. See docs/phase4-state.md.
UNPROVABLE = frozenset({
    "U2_S_INTRO_BESTSELLER",   # asm/01A1
    "U2_S_DISK_PLAYER",        # asm/0764 (the text survives elsewhere: asm/7030:273)
    "U2_S_Z_SPELL",            # asm/464D
    "U2_S_MAGIC_SPELL_READY",  # asm/3B92 (15 db bytes in the file, none of them this)
    "U2_S_FOOD_YES_A",         # asm/17F5
    "U2_S_FOOD_YES_B",         # asm/17F5
    "U2_S_ANOS_9M_BC",         # asm/3577: "ANOS:" here, " 9,000,000 B.C." in asm/3582;
                               # contiguous in the exe at 0x377d but split across two asm
                               # files, so no single file's data stream proves the whole.
})

DB_LINE_RE = re.compile(r"^\s*db\s+(.*?)\s*(?:#.*)?$")
DB_TOKEN_RE = re.compile(r'"((?:[^"\\]|\\.)*)"|0x([0-9a-fA-F]{1,2})')


def decode_db_payload(payload):
    """Decode one `db` payload to bytes: quoted text, a bare hex byte list, or a
    line that mixes them (`db 0x8d,"KILLED--GOLD+",0x00`).

    This deliberately does NOT reuse DB_RE, which matches only a whole quoted
    literal ending in a terminator and is kept narrow on purpose: widening it
    would renumber the db-index locators of every file with a leading glyph byte.
    A presence check has no indices to break, so it can read what the tree
    actually contains.
    """
    out = bytearray()
    for m in DB_TOKEN_RE.finditer(payload):
        if m.group(1) is not None:
            out.extend(decode_gas_string(m.group(1)))
        else:
            out.append(int(m.group(2), 16))
    return bytes(out)


def asm_db_stream(fname):
    """Every byte the asm tree declares as data in one file, in source order."""
    buf = bytearray()
    with open(os.path.join(REPO, "asm", fname), encoding="utf-8") as f:
        for line in f:
            m = DB_LINE_RE.match(line.rstrip("\n"))
            if m:
                buf.extend(decode_db_payload(m.group(1)))
    return bytes(buf)


def c_unescape(lit):
    """Inverse of c_escape: an octal escape of up to three digits, a backslash or a quote, verbatim otherwise."""
    out = bytearray()
    i = 0
    while i < len(lit):
        ch = lit[i]
        if ch != "\\":
            out.extend(ch.encode("latin-1"))
            i += 1
            continue
        nxt = lit[i + 1:i + 2]
        if nxt in ("\\", '"'):
            out.extend(nxt.encode("latin-1"))
            i += 2
            continue
        m = re.match(r"[0-7]{1,3}", lit[i + 1:i + 4])
        if not m:
            raise ValueError(f"committed header has a bad escape: {lit[i:i + 4]!r}")
        out.append(int(m.group(0), 8))
        i += 1 + len(m.group(0))
    return bytes(out)


def parse_asm(path):
    """Return (func_lo, func_hi, [string bytes in file order]) for one asm file."""
    lo = hi = None
    strings = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            m = FUNC_RE.match(line.rstrip("\n"))
            if m:
                lo = int(m.group(1), 16) + FILE_BIAS
                hi = int(m.group(2), 16) + FILE_BIAS
            m = DB_RE.match(line)
            if m:
                strings.append(decode_gas_string(m.group(1)))
    if lo is None:
        raise ValueError(f"{path}: no '# func' range line")
    return lo, hi, strings


def locate(exe, lo, hi, strings):
    """Find each string inside the function's range in the exe.

    Returns [(file_offset, bytes)] or None if any string is missing. Searching
    forward from the previous hit keeps repeated short strings unambiguous.
    """
    window_end = min(len(exe), hi + 0x100)
    pos = lo
    out = []
    for want in strings:
        idx = exe.find(want, pos, window_end)
        if idx < 0:
            return None
        end = exe.find(b"\x00", idx)
        out.append((idx, exe[idx:end]))
        pos = idx + len(want)
    return out


def collect(source, exe, exe_path, warn):
    cache = {}
    rows = []
    for sym, fname, loc in MANIFEST:
        path = os.path.join(REPO, "asm", fname)
        if not os.path.exists(path):
            sys.exit(f"extractor: missing {path}")
        if fname not in cache:
            lo, hi, strings = parse_asm(path)
            found = None
            if source in ("auto", "exe") and exe is not None:
                found = locate(exe, lo, hi, strings)
                if found is None and source == "exe":
                    sys.exit(f"extractor: strings for {fname} not found in {exe_path}")
                if found is None:
                    warn(f"extractor: {fname} not located in the exe; using the asm fallback")
            cache[fname] = (strings, found)
        strings, found = cache[fname]

        if isinstance(loc, tuple) and loc[0] == "exe_only":
            # Some labels exist only as bytes the bulk wave could not turn back into text —
            # "SPELL-" sits in asm/464E as `.byte 0x8f,0x09,0x53,0x50` and friends, because
            # the disassembler read the string as instructions. There is no honest asm
            # fallback for those, and inventing one by widening DB_RE is the hazard that
            # comment documents. So: real value when the user's copy is present, omitted
            # with a warning when it is not, and anything that prints it guards on the
            # define existing. CI loses that one label and still proves the field's
            # position and value; the dev box checks the text.
            off = loc[1]
            if exe is not None:
                rows.append((sym, exe[off:off + loc[2]], f"exe@0x{off:04X}"))
            else:
                warn(f"extractor: {sym} is exe-only (asm/ has it as bytes, not text); "
                     f"omitting — callers must #ifdef it")
            continue

        if isinstance(loc, tuple) and loc[0] == "run":
            off = loc[1]
            if exe is not None:
                # A 3-tuple gives an explicit byte length: some strings are not
                # NUL-terminated (the '(INSERT A BLANK PLAYER DISK)' prompt) and
                # reading to the next NUL would swallow following code bytes.
                # A 2-tuple keeps the original semantics — read up to but not
                # including the terminator — because the emitted C literal is
                # terminated anyway and changing this silently altered the
                # CP1 title strings.
                if len(loc) >= 3:
                    val = exe[off:off + loc[2]]
                else:
                    val = exe[off:exe.index(b"\x00", off)]
                rows.append((sym, val, f"exe@0x{off:04X}"))
            elif len(loc) >= 4:
                # The fourth element is the same string as it appears in the asm
                # tree, so the fallback can find it by content. Without it a `run`
                # locator has nothing to resolve against when the exe is absent and
                # the emitted literal was silently empty — which CI could not see
                # until a test started asserting on the text.
                #
                # This deliberately searches the file's raw text and NOT the parsed
                # db-string list: those lines carry a trailing glyph byte
                # (`db " IS DEAD!",0x8d,0x00`) and widening DB_RE to match them
                # would renumber the string indices of every file that has one,
                # which is the exact hazard the DB_RE comment above documents.
                # A second proof is tried first: the file's decoded DATA stream, the same
                # bytes `--verify` proves a committed string against, so the fallback can
                # never resolve something the gate would refuse. That reaches a literal the
                # listing splits on the game's newline glyph
                # (`db "ASK ME NO QUESTIONS,",0x8d,"I'LL TELL YOU NO LIES.",0x00`), which is
                # the shape the raw-text search below cannot see at all. It reads
                # asm_db_stream and NOT DB_RE's parsed list, so it adds a fallback without
                # moving a single db-index — the hazard the paragraph above documents.
                want = loc[3].encode("latin-1")
                in_data = want in asm_db_stream(fname)
                if not in_data and ('"%s"' % loc[3]) not in open(path, encoding="utf-8").read():
                    sys.exit(f"extractor: {sym}: asm fallback cannot find "
                             f"{loc[3]!r} as a quoted literal in {fname}")
                rows.append((sym, want, f"asm:{fname}{' data' if in_data else ''}"))
            else:
                rows.append((sym, b"", "unavailable without the user's copy"))
            continue

        idx = loc
        if idx >= len(strings):
            sys.exit(f"extractor: {fname} has {len(strings)} strings, manifest wants index {idx}")
        if found:
            off, val = found[idx]
            rows.append((sym, val, f"exe@0x{off:04X}"))
            if val != strings[idx]:
                warn(f"extractor: note {sym}: exe bytes differ from the asm reconstruction")
        else:
            rows.append((sym, strings[idx], f"asm:{fname}"))
    return rows


# ---------------------------------------------------------------------------
# The indexed name pool
#
# One contiguous run of NUL-separated entries in the data segment. asm/508C takes the
# entry index in AL, and the Z-screen print sites add a constant to reach each group,
# read off asm/45D2 (weapons, +0x13), asm/4618 (armour, +0x1d), file 0x4A1D (spells,
# +0x24), file 0x4A75 (items, +0x2e), the six stat literals 0x3E..0x43, asm/45A3
# (races, +0x44) and asm/45B1 (classes, +0x48).
#
# The base is anchored on HANDS at index 19, which is what the +0x13 sites require:
# readied_weapon 9 must print QUICK SWORD, and the WEAPONS: list starts its loop at
# index 1 and prints DAGGER first. Entry 0 is not a display string — it is the tail of
# a compressed picture that happens to be NUL-terminated before the pool begins — and
# it is counted because the code counts it. POOL_ANCHOR makes that explicit instead of
# leaving it to a lucky split.
POOL_FILE = "4C55_name_pool.asm"
POOL_ANCHOR_STRING = "IN THE WATER."   # pool index 1
POOL_ANCHOR_INDEX = 1
# One entry per db line, each tagged with the index the code uses: `db "DAGGER",0x00` tagged
# `# pool[20]`, and the empty entry as `db 0x00` tagged `# pool[0]`. The tags are what make the
# no-exe fallback checkable — the parser requires the indices to run 0..N with no gap and no
# duplicate, instead of hoping a split of one enormous db operand landed in the right places.
# That older form (every entry on one line, no tags) died with the asm/4C50 re-cut on 2026-10-25.
POOL_ENTRY_RE = re.compile(r'^\s*db\s+(?:"((?:[^"\\]|\\.)*)",0x00|0x00)\s+#\s*pool\[(\d+)\]')
NAME_TABLES = [
    ("U2_T_WEAPONS", 0x13, 10),
    ("U2_T_ARMOUR",  0x1d, 8),
    ("U2_T_SPELLS",  0x24, 10),
    ("U2_T_ITEMS",   0x2e, 16),
    ("U2_T_STATS",   0x3e, 6),
    ("U2_T_RACES",   0x44, 4),
    ("U2_T_CLASSES", 0x48, 4),
]

# The pool is exactly as long as the tables that index it: the last group is classes at
# 0x48 with four entries, so entry 75 (THIEF) is the last one and 76 is the count. The exe
# split used to run to a hard 96, which read straight past THIEF into the arrow scancode
# table at file 0x5109 and returned junk nobody looked at — harmless, but it made the exe
# path and the asm/ path disagree, and "they disagree" is the only warning a silent
# mis-split ever gives. Deriving the bound from NAME_TABLES is what makes the two sources
# comparable; engine/tests/status_check.c is what fails the build if either one drifts.
POOL_ENTRY_COUNT = max(base + count for _, base, count in NAME_TABLES)


def pool_from_exe(exe):
    """Split the pool region and return entries indexed the way the code indexes them.

    The split starts at the first display string, "IN THE WATER.", and one placeholder
    is prepended: the byte before it in the image is a second NUL, so the code's index
    base counts one entry that carries no text at all. That is what makes HANDS index 19,
    and pool_anchor_check asserts it rather than trusting the split.
    """
    first = exe.index(POOL_ANCHOR_STRING.encode("latin-1") + b"\0")
    ents = [b""] * POOL_ANCHOR_INDEX
    i = first
    while i < len(exe) and len(ents) < POOL_ENTRY_COUNT:
        if exe[i] == 0:
            i += 1
            continue
        j = exe.index(b"\0", i)
        ents.append(exe[i:j])
        i = j + 1
    return ents

def pool_from_asm(asm_dir):
    """The same pool from asm/POOL_FILE, for a build with no exe.

    Every entry carries its own index in a comment, so the split is verified rather than
    assumed: the indices must run 0..N with no gap and no duplicate, and the anchor entry has
    to sit where the code says it does. pool_anchor_check is the last line of defence; a drift
    fails the build instead of putting PHASER where QUICK SWORD belongs.
    """
    by_index = {}
    with open(os.path.join(asm_dir, POOL_FILE), encoding="utf-8") as f:
        for line in f:
            m = POOL_ENTRY_RE.match(line.rstrip("\n"))
            if not m:
                continue
            idx = int(m.group(2))
            if idx in by_index:
                raise SystemExit(f"extractor: {POOL_FILE} tags pool[{idx}] twice")
            by_index[idx] = decode_gas_string(m.group(1)) if m.group(1) else b""
    if not by_index:
        raise SystemExit(f"extractor: {POOL_FILE} has no tagged pool entries — did the "
                         f"re-cut lose the pool[N] tags this parser depends on?")
    top = max(by_index)
    if sorted(by_index) != list(range(top + 1)):
        raise SystemExit(f"extractor: {POOL_FILE} pool indices are not contiguous "
                         f"(0..{top} expected, {len(by_index)} found)")
    ents = [by_index[i] for i in range(top + 1)]
    anchor = POOL_ANCHOR_STRING.encode("latin-1")
    if len(ents) <= POOL_ANCHOR_INDEX or ents[POOL_ANCHOR_INDEX] != anchor:
        raise SystemExit(f"extractor: {POOL_FILE} has no {POOL_ANCHOR_STRING!r} at "
                         f"pool[{POOL_ANCHOR_INDEX}]")
    return ents


def pool_anchor_check(ents, source):
    """Fail loudly rather than emit a table that puts PHASER where QUICK SWORD belongs."""
    if len(ents) <= 75 or ents[19] != b"HANDS" or ents[28] != b"QUICK SWORD":
        raise SystemExit(f"extractor: name pool anchor broken from {source} "
                         f"— HANDS is not index 19")


def emit_tables(ents):
    """C declarations for the seven groups, or [] when the pool is unavailable."""
    out = []
    for sym, base, count in NAME_TABLES:
        items = ents[base:base + count]
        if len(items) != count:
            raise SystemExit(f"extractor: pool too short for {sym}")
        out.append(f"#define {sym}_N {count}")
        out.append(f"static const char *const {sym}[{sym}_N] = {{")
        out.append("    " + ", ".join(c_escape(v) for v in items))
        out.append("};")
    return out


def c_escape(data):
    out = []
    for b in data:
        if b in (0x22, 0x5C):
            out.append("\\%c" % b)
        elif 0x20 <= b < 0x7F:
            out.append(chr(b))
        else:
            # Octal, never \xNN. A C hex escape consumes every following hex digit, so the
            # literal "\x0d1-DA" is ONE escape (0x0d1, truncated to 0xd1) and not CR followed by
            # '1'. The game's newline is 0x0D/0x8D and the text after it is very often a digit, so
            # the greedy form silently rewrote it: the READY WEAPON menu came out as
            # "READY WEAPON:Q-DA ... U-SW ... Y-QU", each first digit of a line raised by 0x20.
            # An octal escape is capped at three digits, so "\0151-DA" is CR then '1'.
            out.append("\\%03o" % b)
    return '"' + "".join(out) + '"'


def ring_table():
    """The wound-flash XOR mask: the 64-byte table at runtime 0x5288.

    Read from the committed db line of asm/5287_data_mixed.asm — the byte-exact
    reconstruction — not from the exe, because the asm tree is the authority for
    this file either way (the exe path would read the same bytes). asm/4C1E_video_xor_patch
    consumes 16 records of 4 bytes, one per CGA scanline, 2bpp, leftmost pixel in
    the top two bits: a 16x16 ring that inverts the victim's cell (0xff records)
    behind a carved edge (the 0x0f/0xf0/0x3f/0xfc/0xc0/0x03 partials). The table
    in the file is 66 bytes; the loop walks 64. Observed live 2026-11-02:
    captures/spice86/wound-flash-20261102, mask matched record for record.
    """
    path = os.path.join(REPO, "asm", "5287_data_mixed.asm")
    for line in open(path, encoding="utf-8"):
        line = line.split("#")[0].strip()
        if line.startswith("db 0x"):
            vals = [int(t.strip(), 16) for t in line[3:].split(",")]
            if len(vals) < 64:
                raise SystemExit("extractor: ring table too short")
            return vals[:64]
    raise SystemExit("extractor: ring table db line not found in 5287_data_mixed.asm")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default=os.path.join(REPO, "original", "ultimaii.exe"))
    ap.add_argument("--source", choices=["auto", "exe", "asm"], default="auto")
    ap.add_argument("-o", "--out", default=os.path.join(ENGINE, "content", "gen_content.h"))
    ap.add_argument("--emit", choices=["header", "text"], default="header")
    ap.add_argument("--check", action="store_true",
                    help="fail if the existing output differs from what would be written")
    ap.add_argument("--verify", action="store_true",
                    help="fail if the asm fallback disagrees with the committed header")
    ap.add_argument("--force", action="store_true",
                    help="allow an asm-fallback header to overwrite an exe-derived one")
    args = ap.parse_args()

    def warn(msg):
        print(msg, file=sys.stderr)

    exe = None
    note = "asm fallback (no exe supplied)"
    if args.source in ("auto", "exe") and os.path.exists(args.exe):
        exe = open(args.exe, "rb").read()
        md5 = hashlib.md5(exe).hexdigest()
        note = f"exe:{os.path.relpath(args.exe, REPO)} md5={md5}"
        if md5 != EXPECTED_MD5:
            warn(f"extractor: warning: {args.exe} md5 {md5} != {EXPECTED_MD5}")
    elif args.source == "exe":
        sys.exit(f"extractor: --exe {args.exe} not found")

    rows = collect("exe" if exe is not None else "asm", exe, args.exe, warn)

    asm_dir = os.path.join(REPO, "asm")
    try:
        pool = pool_from_exe(exe) if exe is not None else pool_from_asm(asm_dir)
        pool_anchor_check(pool, "exe" if exe is not None else POOL_FILE)
        tables = emit_tables(pool)
    except (SystemExit, FileNotFoundError, ValueError) as e:
        # The single-string table is what the engine has always built against, so a pool
        # that cannot be reconstructed must not take it down: warn loudly, emit no
        # tables, and let whatever needs them fail at its own gate. docs/status-screen-z.md
        # § 7 records why the asm fallback cannot supply the pool today.
        warn(f"extractor: name pool unavailable ({e}); emitting no tables")
        tables = []

    if args.emit == "text":
        for sym, val, _ in rows:
            print(f"{sym}\t{val.decode('latin-1')}")
        return 0

    body = [
        "/* GENERATED by engine/tools/extract/extractor.py — do not edit by hand.",
        " *",
        " * Committed on purpose: this is the only copy of the game's text the build",
        " * has, and a CI runner without the player's copy cannot regenerate it. The",
        " * asm fallback resolves 181 of 220 strings and blanks the rest, so a build",
        " * that regenerated this file from asm alone would boot into an empty",
        " * character-creation screen. Refresh it deliberately:",
        " *   make -C engine content        (needs ../original/ultimaii.exe)",
        " * and let `make -C engine content-check` prove the committed text still",
        " * agrees with the byte-proven asm/ tree for every string asm can reach.",
        " *",
        f" * Source: {note}",
        " * These strings belong to the copyright holder of Ultima II. See AGENTS.md",
        " * § 0 for why the text is published and the art is not.",
        " */",
        "#ifndef U2_GEN_CONTENT_H",
        "#define U2_GEN_CONTENT_H",
        "",
        f'#define U2_CONTENT_SOURCE "{note}"',
        "",
    ]
    for sym, val, origin in rows:
        body.append(f"/* {origin} */")
        body.append(f"#define {sym} {c_escape(val)}")
    if tables:
        body += ["", "/* Indexed name pool, file 0x4F53.. — see the extractor's NAME_TABLES "
                 "comment for\n * how the Z screen's group offsets pin each base. */"]
        body += tables
    ring = ring_table()
    body += ["",
             "/* Wound-flash ring mask, asm/5287_data_mixed.asm table at runtime 0x5288:",
             " * 16 records of 4 bytes at 2bpp, one scanline each, XORed into video by",
             " * asm/4C1E and undone by the second call. See that header and",             " * captures/spice86/wound-flash-20261102. */",
             "static const uint8_t U2_RING_XOR[64] = {",
             "    " + ", ".join("0x%02X" % v for v in ring),
             "};"]
    body += ["", "#endif /* U2_GEN_CONTENT_H */", ""]
    text = "\n".join(body)

    if args.check:
        cur = ""
        if os.path.exists(args.out):
            cur = open(args.out, encoding="utf-8").read()
        if cur != text:
            warn(f"extractor: {args.out} is stale or missing")
            return 1
        return 0

    if args.verify:
        return verify_against_committed(args.out, warn)

    # Downgrade guard. The fallback header is a *lossy* header: 38 of its
    # strings are empty literals, and an empty literal prints as nothing, so a
    # build that silently fell back would ship a game with a blank character
    # creation screen and every test that greps for text would pass vacuously.
    # Refuse to write one over the good file unless it was asked for by name.
    if exe is None and os.path.exists(args.out) and not args.force:
        cur = open(args.out, encoding="utf-8").read()
        if 'U2_CONTENT_SOURCE "exe:' in cur:
            sys.exit("extractor: refusing to overwrite an exe-derived header with "
                     "the asm fallback (38 strings would go empty).\n"
                     f"         {args.out} is committed content, not build output.\n"
                     "         Pass --force if you really mean it.")

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, "w", encoding="utf-8") as f:
        f.write(text)
    print(f"extractor: wrote {os.path.relpath(args.out, REPO)} "
          f"({len(rows)} strings, {note})")
    return 0


def verify_against_committed(committed_path, warn):
    """The drift gate, and the only one that can run without the player's copy.

    For every string in the committed header, the asm tree must contain those
    exact bytes as data in the file the manifest names for it. That is the whole
    claim, and it needs no copy of the text in this script: `asm/` assembles back
    byte-for-byte identical to the binary (assemble_clean.sh), so bytes present
    there are proven, not asserted. Presence is checked against the file's
    decoded data stream rather than against DB_RE's quoted-literal index, which
    is why this reaches the strings asm renders as byte lists or splits on the
    game's newline glyph — 214 of 220, against 181 for the build fallback.

    Three ways to fail: a committed string that is empty (it would print as
    nothing and let a text assertion pass vacuously), a manifest symbol the
    committed header omits, or a string that asm cannot prove and that is not on
    the known-unprovable list.
    """
    if not os.path.exists(committed_path):
        sys.exit(f"extractor: --verify: {committed_path} is missing")

    committed = {}
    for ln in open(committed_path, encoding="utf-8"):
        if ln.startswith("#define U2_S_"):
            parts = ln.rstrip("\n").split(None, 2)
            if len(parts) == 3 and parts[2].startswith('"') and parts[2].endswith('"'):
                committed[parts[1]] = c_unescape(parts[2][1:-1])

    files = {sym: fname for sym, fname, _ in MANIFEST}
    empties = sorted(s for s, v in committed.items() if not v)
    missing = sorted(s for s in files if s not in committed)

    streams = {}
    unprovable = []
    for sym in sorted(committed):
        val = committed[sym]
        if not val:
            continue
        fname = files.get(sym)
        if fname is None:
            warn(f"extractor: VERIFY {sym}: in the committed header, not in the manifest")
            missing.append(sym)
            continue
        if fname not in streams:
            streams[fname] = asm_db_stream(fname)
        if val not in streams[fname]:
            unprovable.append(sym)

    for sym in empties:
        warn(f"extractor: VERIFY {sym}: committed header carries an EMPTY string")
    for sym in missing:
        warn(f"extractor: VERIFY {sym}: the manifest lists it, the committed header omits it")
    new = sorted(set(unprovable) - set(UNPROVABLE))
    for sym in new:
        warn(f"extractor: VERIFY {sym}: not present as data in the asm file that names it, "
             f"and not on the known-unprovable list")
    if empties or missing or new:
        warn(f"extractor: VERIFY FAIL — {len(committed)} committed strings, "
             f"{len(empties)} empty, {len(missing)} missing, "
             f"{len(unprovable)} unprovable against asm/")
        return 1
    if len(unprovable) < len(UNPROVABLE):
        warn(f"extractor: note: only {len(unprovable)} strings are unprovable against asm/; "
             f"the list still names {len(UNPROVABLE)}. Cross off what is now proven — this "
             f"gate only ever gets stronger.")
    print(f"extractor: VERIFY ok — {len(committed) - len(unprovable)} of {len(committed)} "
          f"committed strings proven present in asm/ data, {len(unprovable)} unprovable "
          f"(known list: {len(UNPROVABLE)}), none empty")
    return 0


if __name__ == "__main__":
    sys.exit(main())

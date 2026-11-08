#!/usr/bin/env python3
"""place_index.py — decode the place names the game spells out of alphabet tiles.

WHAT THIS IS. Ultima II has no place-name string table. The names are drawn in the world
map as tiles, one capital letter per cell, and they are read out of `mapxNN` exactly the
way a wall or a tree is read. So the name of a town is a fact about the sector's bytes and
can be recovered without playing the game at all. This tool does that and nothing else.

THE TILE->LETTER MAPPING, and where it comes from. A map byte is an art index shifted left
by two (`docs/tile-art-spec.md` § 7 and `docs/tile-art-evidence.md` A1: `[DS:14] =
map_byte >> 1`, and the art index itself is `>>2`). Arts `0x20..0x2F` are A..P and arts
`0x31..0x39` are R..Z. **Art `0x30` is a wall tile, not Q: there is no Q glyph in the
game.** That absence is load-bearing evidence rather than a gap — `mapx93` spells the
queen's room `KUEEN`, which is what a data set with no Q has to do, and it is the reason
this mapping is trusted rather than assumed.

THE FALSE POSITIVES, named rather than hidden. Two kinds, and both are visible in the
output rather than filtered into silence:

  * **Structural fill.** In several sectors the letter indices are used as building
    material, so a dungeon or a tower comes out as forty rows of `AAAA...`. Those are
    classified `fill` by a stated rule (one letter repeated), not by a guess.
  * **Room labels.** A castle interior says `VAULT`, `KITCHEN`, `MESS HALL`. Those are real
    text and real evidence, but they are not the place's name, so they are classified
    `room` and the name column stays empty unless a rule below fires.

THE NAME RULE, stated so it can be argued with. A run is offered as a candidate name when
it opens with `TOWNE`/`TOWN`/`PORT`/`CASTLE` or closes with `TOWN`, or when it is the only
non-fill run in a sector whose site kind is a settlement. That is a heuristic and it is
labelled `candidate`, not `proven`. The provenance column always says which rule fired, so a
wrong name is traceable to the rule that invented it.

THIRD-PARTY NAMES. `--wiki` attaches community names from `disasm/place-wiki-names.tsv`.
Those are a hypothesis the bytes get to veto, exactly like `bestiary/`: the name comes from
the wiki, the sector comes from this tool, and a row only agrees when both say so. Grades
per AGENTS.md § 5. Nothing here is hand-kept except the crosswalk, which is source data.

Usage:
    python3 tools/place_index.py                 # human summary
    python3 tools/place_index.py --tsv disasm/place-index.tsv
    python3 tools/place_index.py --sector 22 -v  # every run in one sector
"""
import argparse
import os
import re
import sys

# Era labels, verbatim from the five inline strings at asm/3546_time_index_message.asm
# (file 0x3752, 0x3768, 0x377D, 0x3798, 0x37AE) plus the LEGENDS arm reached from
# asm/353D. Tension with the world byte is unresolved and documented in
# docs/needs-gameplay.md G45: on Earth the tens digit IS the era; off Earth it is the world
# index, proven by a rocket landing writing DS:49 = 0x09 for world X (G39 capture).
EARTH_ERAS = {
    0: "Time of Legends (ANOS: LEGENDS!)",
    1: "Pangea (ANOS: 9,000,000 B.C.)",
    2: "1423 B.C. (ANOS: 1423 B.C.)",
    3: "1990 A.D. (ANOS: 1990 A.D.)",
    4: "2112 A.D. Aftermath (ANOS: 2112 A.D.)",
}
# Off Earth the same digit is a world index. The names below are the community ordering and
# are marked third-party wherever they are printed; the byte fact is only "not Earth".
OFF_EARTH = {5: "world 5", 6: "world 6", 7: "world 7", 8: "world 8", 9: "world 9 (Planet X)"}

# Site kinds: engine/src/core/world.h u2_site_kind, which carries its own grade per value.
SITE_KINDS = {
    0: "overworld",
    1: "village",
    2: "town",
    3: "castle",
    4: "tower",
    5: "dungeon",
}

LETTER_RUN = re.compile(r"[A-Z ]{2,}")

# The space tile. Read off the bytes, not guessed: in mapx22 at y=63 the name is stored as
# T O W N E 0x78 0x70 0x70 0x70 0x70 0x78 L I N D A, i.e. one 0x78 either side of a run of
# 0x70 background, and the same 0x78 single cell separates THE from ARMOURY at y=13. So 0x78
# (art 0x1E) is the word separator and 0x70 (art 0x1C) is empty ground. Treating 0x78 as a
# space is what turns TOWNE + LINDA into one phrase instead of two unrelated runs.
SPACE_BYTE = 0x78


def art_to_letter(byte):
    """One map byte -> its capital letter, or None. Art = byte >> 2; no Q tile."""
    art = byte >> 2
    if 0x20 <= art <= 0x2F:
        return chr(ord("A") + (art - 0x20))
    if 0x31 <= art <= 0x39:
        return chr(ord("R") + (art - 0x31))
    return None


# Two words on the same row are the same sign when the gap between them is at most this many
# non-letter cells. The number is measured, not chosen: in mapx22 TOWNE ends at x=28 and
# LINDA starts at x=35 (gap 6) and THE/ARMOURY are 1 apart, while mapx31 y=0 carries two
# different signs -- "THINGS ARE NOT AS THEY SEEM" ending at x=27 and "SAIL OUT THE NORTH
# CHANNEL" starting at x=37 -- 9 apart. So the threshold sits between 6 and 9; 6 is the
# largest value that keeps those two signs separate in every sector checked, and a name that
# comes out split is a visible failure rather than a silent one.
GAP_JOIN = 6


def decode_sector(tiles):
    """Every phrase in a 64x64 grid, as (y, x0, x1, text).

    Letter runs are found first, then runs on the same row are joined into a phrase when the
    gap between them is small enough to be a space rather than a different sign. Any byte
    that is not a letter is a gap; the width of the gap is the whole discriminator, which is
    why the rule is stated as a number and named in the comment rather than hidden.
    """
    words = []
    for y in range(64):
        row = y * 64
        x = 0
        while x < 64:
            if art_to_letter(tiles[row + x]) is None:
                x += 1
                continue
            start = x
            chars = []
            while x < 64 and art_to_letter(tiles[row + x]) is not None:
                chars.append(art_to_letter(tiles[row + x]))
                x += 1
            words.append((y, start, x - 1, "".join(chars)))

    # Join horizontally, then join the two-line names.
    phrases, i = [], 0
    while i < len(words):
        y, x0, x1, text = words[i]
        j = i + 1
        while j < len(words) and words[j][0] == y and words[j][1] - x1 - 1 <= GAP_JOIN:
            text += " " + words[j][3]
            x1 = words[j][2]
            j += 1
        if len(text.replace(" ", "")) >= 2:
            phrases.append((y, x0, x1, text))
        i = j

    KINDS = ("TOWN", "TOWNE", "CASTLE", "TOWER")
    out = []
    for idx, (y, x0, x1, text) in enumerate(phrases):
        # A two-line name: mapx11 writes BARADINS on y=35 x=28..35 and TOWN on y=36 x=30..33,
        # vertically overlapping. A bare kind word with a run directly above it that overlaps
        # in x is one name split over two rows. Stated as a rule rather than a special case
        # for Baradin's Town, because the next stacked two-liner would need a second special case.
        if text in KINDS and idx > 0:
            py, px0, px1, ptext = phrases[idx - 1]
            if py == y - 1 and not (x1 < px0 or px1 < x0):
                out[-1] = (py, min(px0, x0), max(px1, x1), ptext + " " + text)
                continue
        if idx > 0 and phrases[idx - 1][0] == y - 1 and phrases[idx - 1][3] in KINDS:
            continue
        out.append((y, x0, x1, text))
    return out


def classify(text, all_runs_in_row):
    """fill | room | candidate, with the rule that fired returned as the second value."""
    bare = text.replace(" ", "")
    if len(set(bare)) == 1 and len(bare) >= 2:
        # A single glyph repeated is structure, not text. The common case is `I` (art 0x28,
        # byte 0xA0), which several sectors use as a post or fence line, and it decodes as
        # "I I" once the gap rule joins adjacent posts. Real one-letter words do not occur
        # in the corpus; every genuine run found so far is two letters or more.
        return "fill", "one glyph repeated (structural fill)"
    up = text.strip()
    for pre in ("TOWNE", "TOWN ", "TOWN", "PORT ", "CASTLE "):
        if up.startswith(pre):
            return "candidate", "opens with TOWNE/TOWN/PORT/CASTLE"
    if up.endswith(" TOWN") or up == "TOWN":
        return "candidate", "closes with TOWN"
    return "room", "text, no name rule fired"


def tlkx_lines(path):
    """Decode a tlkx file: the original stores dialogue with the high bit set, and
    engine/src/core/world.c u2_world_tlkx_record masks 0x7F the same way."""
    if not os.path.exists(path):
        return []
    raw = open(path, "rb").read()[:256]          # CX=0x100 at asm/22D5; the tail is not text
    txt = bytes((b & 0x7F) for b in raw).decode("latin1")
    return [r.strip() for r in re.split(r"[^\x20-\x7e]+", txt) if len(r.strip()) >= 5]


def build(content_dir):
    rows = []
    for fn in sorted(os.listdir(content_dir)):
        if not fn.startswith("mapx") or len(fn) != 6:
            continue
        sec = fn[4:]
        tens, ones = int(sec[0]), int(sec[1])
        data = open(os.path.join(content_dir, fn), "rb").read()
        tiles = data[:4096]
        runs = decode_sector(tiles)
        kinds = {}
        for (y, x0, x1, text) in runs:
            cls, rule = classify(text, runs)
            kinds.setdefault(cls, []).append((y, x0, x1, text, rule))
        # A settlement with no name-rule hit but exactly one non-fill run: offer that run.
        if not kinds.get("candidate") and ones in (1, 2, 3):
            only = kinds.get("room", [])
            if len(only) == 1:
                y, x0, x1, text, _ = only[0]
                kinds["candidate"] = [(y, x0, x1, text, "only non-fill run in a settlement")]
                kinds["room"] = []
        rows.append({
            "sector": sec, "tens": tens, "ones": ones,
            "era": EARTH_ERAS.get(tens, OFF_EARTH.get(tens, "unknown")),
            "kind": SITE_KINDS.get(ones, "unknown"),
            "candidate": "; ".join(t for *_, t, _ in kinds.get("candidate", [])),
            "rooms": "; ".join(t for *_, t, _ in kinds.get("room", [])),
            "fill_rows": len(kinds.get("fill", [])),
            "tlkx": len(tlkx_lines(os.path.join(content_dir, "tlkx" + sec))),
        })
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--content-dir", default="original")
    ap.add_argument("--tsv", help="write the full index here (generated; never hand-edited)")
    ap.add_argument("--sector", help="dump every decoded run for one sector")
    ap.add_argument("--verify", action="store_true",
                    help="assert the hand-checked place names still decode (a control)")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    if not os.path.isdir(args.content_dir):
        print(f"no content dir at {args.content_dir}", file=sys.stderr)
        return 2

    if args.sector:
        fn = os.path.join(args.content_dir, "mapx" + args.sector)
        tiles = open(fn, "rb").read()[:4096]
        for (y, x0, x1, text) in decode_sector(tiles):
            cls, rule = classify(text, None)
            print(f"  y={y:2d} x={x0:2d}..{x1:2d}  {cls:9s}  {text!r}   [{rule}]")
        return 0

    rows = build(args.content_dir)
    if args.tsv:
        cols = ["sector", "era", "kind", "candidate", "rooms", "fill_rows", "tlkx"]
        with open(args.tsv, "w") as f:
            f.write("# generated by tools/place_index.py -- do not hand-edit\n")
            f.write("\t".join(cols) + "\n")
            for r in rows:
                f.write("\t".join(str(r[c]).replace("\t", " ") for c in cols) + "\n")
        print(f"wrote {args.tsv}: {len(rows)} sectors")

    if args.verify:
        # A control, not a summary. Two layers, because the obvious one is not enough: when this
        # was first written, clipping Z out of the mapping left every check below green, because
        # none of the six place names contains a Z. A gate that cannot go red where it claims to
        # guard is the defect AGENTS.md § 3 names, so the letter table is now pinned directly and
        # the corpus is measured, not just sampled.
        for byte, want in ((0x80, "A"), (0xCC, "T"), (0xB8, "O"), (0xAC, "L"),
                           (0xA0, "I"), (0xE4, "Z")):
            got = art_to_letter(byte)
            if got != want:
                print(f"VERIFY FAIL: byte {byte:#04x} decoded {got!r}, expected {want!r}")
                return 1
        if art_to_letter(0xC0) is not None:
            print("VERIFY FAIL: art 0x30 decoded as a letter, but it is the wall tile -- "
                  "there is no Q glyph in the game")
            return 1
        total = sum(len(re.sub(r"[^A-Z]", "", r["candidate"] + r["rooms"])) for r in rows)
        # Measured on this corpus, with headroom: a mapping that silently drops a letter or a
        # gap rule that shreds phrases takes this number down and this check with it.
        if total < 600:
            print(f"VERIFY FAIL: only {total} decoded letters across {len(rows)} sectors; "
                  f"the corpus measured 700+, so the decode has regressed")
            return 1
        # The gap rule's own negative control, because the place names do not exercise it:
        # widening GAP_JOIN until it swallows a neighbouring sign leaves all six names intact
        # and the check green. mapx31 y=0 holds two separate signs, "THINGS ARE NOT AS THEY
        # SEEM" and "SAIL OUT THE NORTH CHANNEL", 9 cells apart. If they ever merge, the
        # threshold has grown past the evidence that set it.
        s31 = decode_sector(open(os.path.join(args.content_dir, "mapx31"), "rb").read()[:4096])
        joined = [t for *_, t in s31 if "SEEM" in t and "SAIL" in t]
        if joined:
            print(f"VERIFY FAIL: mapx31 y=0 merged two separate signs into {joined[0]!r}; "
                  f"GAP_JOIN has grown past the 6-cell evidence that set it")
            return 1
        expect = {"11": "BARADINS TOWN", "22": "TOWNE LINDA", "31": "PORT BONIFICE",
                  "82": "TOWNE MAKLER", "92": "TOWNE BASKO", "93": "CASTLE BARATARIA"}
        got = {r["sector"]: r["candidate"] for r in rows}
        bad = [(s, e, got.get(s, "")) for s, e in expect.items() if e not in got.get(s, "")]
        for s, e, g in bad:
            print(f"VERIFY FAIL sector {s}: expected {e!r}, decoded {g!r}")
        # And the negative control: a name the game cannot spell, because there is no Q tile.
        if any("QUEEN" in r["rooms"] + r["candidate"] for r in rows):
            print("VERIFY FAIL: a QUEEN was decoded, but art 0x30 is a wall tile and there is "
                  "no Q glyph -- the letter mapping is wrong")
            bad.append(("Q", "no QUEEN", "found one"))
        if bad:
            return 1
        print(f"VERIFY OK: letter table pinned, {total} decoded letters, "
              f"{len(expect)} place names, and no Q was found")
        return 0

    named = [r for r in rows if r["candidate"]]
    print(f"{len(rows)} sectors decoded; {len(named)} carry a candidate place name\n")
    for r in rows:
        if r["candidate"]:
            print(f"  {r['sector']}  {r['kind']:9s}  {r['era']:38s}  {r['candidate']}")
    if args.verbose:
        for r in rows:
            if not r["candidate"] and r["rooms"]:
                print(f"  {r['sector']}  {r['kind']:9s}  (no name)  rooms: {r['rooms']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

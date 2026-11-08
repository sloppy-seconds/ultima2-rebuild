#!/usr/bin/env python3
"""npc_index.py — every NPC the game names, and the quest roles attached to them.

WHAT THIS IS. Ultima II's NPC names are not in the executable either. They are the first line
of each dialogue record in `tlkxNN` / `tlkgNN`, stored with the high bit set, and the record's
POSITION in the file is its role index: the creature slot byte at `DS:1D7` minus `0x80` selects
which record gets printed (`asm/4209_talk_scripted_npc.asm`). So an NPC's identity is
(sector, record index), and both halves are bytes on disk.

THE ROLE INDEX IS THE INTERESTING HALF. `asm/3C2E_cmd_O.asm` enumerates roles 0x81, 0x82 and
0x83 in sector 32 and gives each a different quest effect, and `asm/4209` sets the blessing byte
for role 0x81 in a castle on world 9. That means the record number is not a print index but the
game's own quest-dispatch key, and it is why this tool keys rows by it rather than by the
speaker's name: names are text a player reads, roles are what the engine branches on.

THE ONE CASE WHERE A NAME IS NOT ENOUGH, and the reason the crosswalk exists: sector 32 role
0x84 is `A CLERK STATES: WELCOME TO THE HOTEL CALIFORNIA!`. The game gives a job title, not a
name, and that record is the stat-raising grind -- the generic bit-7 arm of cmd_O handles every
role the special list does not name. So "the clerk" is an identity the bytes support and the
wiki supplies a label for. `disasm/npc-wiki-names.tsv` carries those, in a separate graded
column, exactly like the place crosswalk.

THIRD-PARTY NAMES are permitted (owner ruling, 2026-11-08) for NPCs the game does not name.
They live in the crosswalk and never in the `speaker` column, so a reader can always tell what
the bytes said from what a wiki said. `--verify` fails if a crosswalk row names a sector+role
this tool cannot resolve.

Usage:
    python3 tools/npc_index.py                       # human summary
    python3 tools/npc_index.py --tsv disasm/npc-index.tsv
    python3 tools/npc_index.py --verify              # the control
    python3 tools/npc_index.py --sector 32           # one sector's NPCs
"""
import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import place_index  # reuse the world/era tables; a second copy would drift

# Quest roles, each cited to the routine that branches on it. These are the only role numbers
# the binary actually tests, which is why they are hard-coded rather than inferred from the
# speaker text: the engine dispatches on the number, never on the name.
QUEST_ROLES = {
    ("32", 0x81): "cmd_O: offer >=500g -> 'ENILNO IS YOURS!', sets DS:7F (the quicksword)",
    ("32", 0x82): "cmd_O: offer >=500g AND DS:6C!=0 -> 'THE RING IS YOURS!', sets DS:D6; "
                  "without the blessing -> 'EARN THE RING!'",
    ("32", 0x83): "cmd_O: offer -> 'HERE TAKE THIS!', grants a random weapon DS:76+(rng&7)+1",
    ("32", 0x84): "cmd_O: the generic bit-7 arm -- the stat grind. r=rng&7; raise when r<6 "
                  "(75%); stat DS:4B+r gains 4 x hundreds offered",
    ("93", 0x81): "asm/4209: talking to this role when DS:6D==9 and DS:4A==3 sets DS:6C=1 "
                  "-- the Father Antos blessing the ring requires",
}

# The speaker is whatever precedes the first colon. A verb list is used only to TRIM a trailing
# verb ("SANTRE THE SWASHBUCKLER WARNS:" -> "SANTRE THE SWASHBUCKLER"); when no verb matches the
# whole prefix is kept, because the Galaxy patch's sector-32 dialogue uses verbs the shipped one
# never does -- "Marlin Perkins threatens:", "Owen K. Garriott asks:" -- and a closed verb list
# silently returns an empty speaker for every one of them. That is exactly how a first draft of
# this tool lost half its NPCs, so the fallback is the fix rather than a longer list.
VERB = re.compile(r"\s+\S+\s*$")
VERB_WORDS = {"says", "states", "asks", "cries", "warns", "orders", "ordains", "preaches",
              "burps", "screams", "mumbles", "brags", "boasts", "claims", "theorizes", "chants",
              "yells", "inquires", "demands", "pleads", "complains", "proclaims", "suggests",
              "laughs", "begs", "whimpers", "calls", "threatens", "hollers", "growls", "mutters"}


def split_speaker(flat):
    who, sep, line = flat.partition(":")
    if not sep:
        return "", flat
    parts = who.strip().rsplit(" ", 1)
    if len(parts) == 2 and parts[1].lower().rstrip("!.") in VERB_WORDS:
        return parts[0], line.strip()
    return who.strip(), line.strip()


def records(path, limit=4):
    """Decode a tlkx/tlkg file into (index, speaker, line) per record.

    Records are NUL-separated and the role index is the 1-based position, per
    asm/4209's `sbb $0x80,%al`. Only the first 256 bytes are read -- the engine's
    U2_TLKX_BYTES carries the reason: the shipped files are 384 bytes and the tail is
    assembly source, so record 5+ of several sectors decodes to garbage like
    `PRBYTE PRINT $0D,"HOW 'BOUT IT?" and must not be presented as an NPC.
    """
    if not path or not os.path.exists(path):
        return []
    raw = open(path, "rb").read()[:256]
    txt = bytes((b & 0x7F) for b in raw).decode("latin1")
    parts = [c.strip() for c in txt.split("\x00") if len(c.strip()) >= 5]
    out = []
    for i, part in enumerate(parts, 1):
        if i > limit:
            break
        flat = " ".join(part.split())
        who, line = split_speaker(flat)
        if who or line:
            out.append((i, who, line))
    return out


def build(content_dir, worlds):
    rows = []
    seen = set()
    dirs = [(content_dir, "tlkx", "mapx"),
            (os.path.join(content_dir, "updated"), "TLKG", "MAPG")]
    for base, tprefix, mprefix in dirs:
        if not os.path.isdir(base):
            continue
        for fn in sorted(os.listdir(base)):
            if not fn.startswith(tprefix) or len(fn) != len(tprefix) + 2:
                continue
            sec = fn[len(tprefix):]
            if (base, sec) in seen:
                continue
            seen.add((base, sec))
            tens = int(sec[0])
            # Same rule as tools/place_index.py: on the mapx layer tens 0..4 is an EARTH era,
            # because Earth's five time periods occupy sectors 00-49 and collided with worlds
            # 1-4. Only the mapg layer, or a root tens >= 5, is a planet.
            if mprefix == "mapx" and tens <= 4:
                world, era = worlds[0], place_index.EARTH_ERAS.get(tens, "")
            else:
                world = worlds[tens] if tens < len(worlds) else place_index.WORLD_X
                era = ""
            kind = place_index.SITE_KINDS.get(int(sec[1]), "unknown")
            for (idx, who, line) in records(os.path.join(base, fn)):
                rows.append({
                    "sector": sec, "layer": "mapx" if mprefix == "mapx" else "mapg",
                    "world": world, "era": era, "kind": kind,
                    "role": f"0x{0x80 + idx:02X}", "index": idx,
                    "speaker": who, "line": line,
                    "quest": QUEST_ROLES.get((sec, 0x80 + idx), ""),
                })
    return rows


def load_wiki(path):
    out = {}
    if not os.path.exists(path):
        return out
    for line in open(path):
        line = line.rstrip("\n")
        if not line or line.startswith("#"):
            continue
        parts = line.split("\t")
        if len(parts) < 6 or parts[0] == "layer":
            continue
        out.setdefault((parts[0], parts[1], int(parts[2])), []).append(
            (parts[3], parts[5], parts[4]))   # name, grade, evidence
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--content-dir", default="original")
    ap.add_argument("--tsv")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--sector")
    args = ap.parse_args()

    if not os.path.isdir(args.content_dir):
        print(f"no content dir at {args.content_dir}", file=sys.stderr)
        return 2

    worlds, wsrc = place_index.world_names(os.path.join(args.content_dir, "ultimaii.exe"))
    wiki = load_wiki(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..",
                                  "disasm", "npc-wiki-names.tsv"))
    rows = build(args.content_dir, worlds)
    for r in rows:
        hits = wiki.get((r["layer"], r["sector"], r["index"]), [])
        r["wiki"] = "; ".join(f"{n} [{g}]" for n, g, _ in hits)

    if args.sector:
        for r in [x for x in rows if x["sector"] == args.sector and x["layer"] == "mapx"]:
            print(f"  {r['role']} rec{r['index']}  {r['speaker'] or '(no speaker)':32s} "
                  f"{r['line'][:44]}")
            if r["quest"]:
                print(f"        QUEST: {r['quest']}")
        return 0

    if args.tsv:
        cols = ["sector", "layer", "world", "era", "kind", "role", "index",
                "speaker", "wiki", "line", "quest"]
        with open(args.tsv, "w") as f:
            f.write("# generated by tools/npc_index.py -- do not hand-edit\n")
            f.write("\t".join(cols) + "\n")
            for r in rows:
                f.write("\t".join(str(r[c]).replace("\t", " ") for c in cols) + "\n")
        print(f"wrote {args.tsv}: {len(rows)} NPCs")

    if args.verify:
        named = [r for r in rows if r["speaker"]]
        if not named:
            print("VERIFY FAIL: no NPC speakers decoded at all -- the high-bit strip is wrong")
            return 1
        # The quest chain, row by row. These five are the endgame path and each is cited to a
        # routine that branches on the role number, so a decode that drifts takes the chain down.
        expect = {("32", 0x81): "SANTRE", ("32", 0x82): "OLD MAN",
                  ("32", 0x83): "FLOPY FIGHTER", ("32", 0x84): "CLERK",
                  ("93", 0x81): "FATHER ANTOS"}
        by = {(r["sector"], int(r["role"], 16)): (r["speaker"] + " " + r["line"]).upper()
              for r in rows if r["layer"] == "mapx"}
        bad = []
        for key, want in expect.items():
            got = by.get(key, "")
            if want not in got:
                bad.append(f"{key[0]} role {key[1]:#04x}: expected {want!r}, found {got[:40]!r}")
        # The tail-garbage guard: record 5+ of several sectors is assembly source, not an NPC.
        if any("PRBYTE" in r["line"] or "PRINT $" in r["line"] for r in rows):
            bad.append("an assembly tail record was presented as an NPC; the 256-byte limit "
                       "or the record cap has been raised")
        for key, hits in wiki.items():
            if not any(r["layer"] == key[0] and r["sector"] == key[1]
                       and r["index"] == key[2] for r in rows):
                bad.append(f"npc-wiki-names.tsv has a row for {key[0]} sector {key[1]} "
                           f"record {key[2]}, which no decoded NPC matches")
        for b in bad:
            print("VERIFY FAIL:", b)
        if bad:
            return 1
        print(f"VERIFY OK: {len(named)} named NPCs, {len(rows)} total, "
              f"{len(expect)} quest roles, {len(wiki)} crosswalk rows resolve")
        return 0

    print(f"{len(rows)} NPC records across "
          f"{len({r['sector'] for r in rows})} sectors -- world names {wsrc}\n")
    for r in rows:
        if r["quest"]:
            print(f"  {r['sector']} {r['role']}  {r['world']:9s} {r['kind']:7s} "
                  f"{r['speaker'] or '(no speaker)'}")
            print(f"        {r['line'][:70]}")
            print(f"        QUEST  {r['quest'][:100]}")
            if r["wiki"]:
                print(f"        WIKI   {r['wiki']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

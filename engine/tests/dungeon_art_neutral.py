#!/usr/bin/env python3
"""dungeon_art_neutral.py — the dungeon must draw the same under every theme.

    python3 tests/dungeon_art_neutral.py [--selftest] [FILE ...]

Exits 0 when the dungeon renderer consults no theme atlas, 1 when it does.

Why this is a gate and not a comment. On 2026-11-06 the in-game notice
`THEME ART DOES NOT COVER DUNGEONS` was removed, because it fired on every dungeon
entry under the FM Towns theme to say the art was wrong in the one screen where the
theme makes no difference: the dungeon is a first-person wireframe drawn by
`dg_line` in CGA colour 3 (asm/4B76), and `dungeon.c` never touches a theme tile.
`default` stayed silent only because its manifest CLAIMS `scope = dungeon`.

That makes the removal safe for exactly one reason, and the reason is a fact about
the renderer that a future change could undo. If someone wires theme art into the
dungeon — a textured wall, a themed chest sprite — then a theme whose scope omits
dungeons is suddenly telling the truth, the notice has to come back, and this check
goes red so the decision is revisited rather than inherited.

Since 2026-11-14 the enhanced dungeon draws procedural art (dungeon_assets.c),
so the scan covers dungeon3d.c and dungeon_assets.c too: the 3D view is
theme-free by the same rule the wireframe was theme-free by.

--selftest proves the check can fail: it runs the same scan over a synthetic file
that does consult the atlas, and requires a finding. A gate that has never been
able to go red is worse than no gate.
"""
import re
import sys

# Anything that reaches a theme atlas from the dungeon renderer. Deliberately
# broad: the point is "the dungeon consulted the theme", not which call it used.
PATTERNS = [
    r"u2_tiles_ega\w*",          # the atlas API: get/present/open/...
    r"tiles_ega",                # the struct itself
    r"u2_tiles_(get|present|blit)",
    r"scope_dungeon",            # a renderer that branches on scope is drawing themed art
    r"art_index|u2_art_index",   # the theme-mapped art index
]

DEFAULT_TARGETS = ["src/game/dungeon.c", "src/game/dungeon3d.c",
                   "src/game/dungeon_assets.c"]


def scan(text):
    hits = []
    for pat in PATTERNS:
        for i, line in enumerate(text.splitlines(), 1):
            stripped = line.strip()
            if stripped.startswith("*") or stripped.startswith("/*"):
                continue        # comment prose is not a call
            if re.search(pat, line):
                hits.append((i, pat, stripped[:90]))
    return hits


def check_file(path):
    try:
        with open(path, "r", errors="replace") as f:
            return scan(f.read())
    except OSError as e:
        print(f"dungeon_art_neutral: {e}", file=sys.stderr)
        return None


# A renderer that DOES consult the theme — the thing this gate exists to catch.
SYNTHETIC_THMED_DUNGEON = """
static void dg_wall(u2_ctx *ctx, int tile) {
    u2_tiles_ega_present(&ctx->theme, tile, 0, 0);
}
"""


def selftest():
    ok = True
    hits = scan(SYNTHETIC_THMED_DUNGEON)
    if not hits:
        print("SELFTEST FAIL  a themed dungeon renderer was not detected")
        ok = False
    else:
        print(f"SELFTEST ok    a themed dungeon renderer is detected ({len(hits)} hit(s))")
    if scan("/* the dungeon is a wireframe; no tiles_ega here */\n"):
        print("SELFTEST FAIL  comment prose was counted as a call")
        ok = False
    else:
        print("SELFTEST ok    comment prose is not counted")
    return 0 if ok else 1


def main(argv):
    if "--selftest" in argv:
        return selftest()
    targets = [a for a in argv[1:]] or DEFAULT_TARGETS
    bad = 0
    for t in targets:
        hits = check_file(t)
        if hits is None:
            return 2
        if hits:
            print(f"FAIL  {t} consults the theme atlas, so theme scope is now "
                  f"load-bearing for the dungeon:")
            for line, pat, text in hits:
                print(f"        {line}: /{pat}/  {text}")
            bad = 1
    if not bad:
        print(f"ok    {len(targets)} file(s): the dungeon draws no theme art, "
              f"so it plays the same under every theme")
    return bad


if __name__ == "__main__":
    sys.exit(main(sys.argv))

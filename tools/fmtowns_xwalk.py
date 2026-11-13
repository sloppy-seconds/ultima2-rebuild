#!/usr/bin/env python3
"""
fmtowns_xwalk.py — a drag-and-drop editor for the DOS -> FM Towns tile crosswalk.

WHY THIS EXISTS
---------------
The FM Towns art on `references/fmtowns/` is a *different art set*, not a redraw
of the DOS 64-entry vocabulary. Silhouette similarity cannot separate one
humanoid from another, so no crosswalk falls out of the bytes. A person has to
name it. This puts both sets in front of that person, pre-seeds everything that
CAN be derived, and asks for confirmation on the rest.

WHAT THE FM TOWN BANKS HOLD (measured, see docs/fmtowns-graphics.md)
  * every file is 512 header bytes + 64 tiles of 512 B: 32x32, 4bpp packed,
    high nibble = left pixel, and only indices 0..7 occur
  * MAP2_1 / MAP2_2 / MAP2_3 are the per-planet overworld banks: terrain, walls
    and borders in 0..16 and **the letters A..Z at tiles 38..63, identical in all
    three**. That shared letter block is why an earlier reading took these files
    for map data instead of art. Comparing them tile by tile, only tiles 1-2, 4-5,
    7-11 and 13-16 differ at all; everything else is byte-identical. Tile 16 is
    the moongate on planet 1 and the shield on planets 2 and 3, which is the one
    place the three planets disagree about what a tile means.
  * EXTRA holds the objects: ships at 16..19, horses at 20..24, trees, buildings
  * ENEMY2 holds creatures, PLAYER the avatar, CHEST/DUNG1/DUNG2 the dungeon
  * one DOS tile usually maps to a RUN of FM tiles (frames and variants), so a
    mapping is a set, not a single tile
  * animation frames are ADJACENT, stride 1. An earlier reading of this file said
    the stride was 2, measured as a mean agreement over all candidate pairs. That
    was wrong, and the owner's own identified pairs refute it directly: on the
    pairs he chose by eye, adjacent agreement beats the best stride-2 partner in
    six of seven cases and by a wide margin (EXTRA/3+4 0.99 vs 0.13, EXTRA/1+2
    0.97 vs 0.15, ENEMY2/40+41 0.93 vs 0.20, ENEMY2/16+17 0.82 vs 0.38). A mean
    over unidentified pairs measured the similarity of like-looking tiles, not the
    frame order. Re-measure on identified pairs before trusting a statistic here.

PALETTE
  The index->RGB table is not in any asset file and not a static table in the
  executable: `EGB_palette` at `0x103c84` in `ENCHANT.EXP` is a setter that
  stores a far pointer out of `[edi+8]`, so the table is assembled at run time.
  It was therefore read off the running machine, and the in-game table is the one
  below. Two things about it are worth knowing before you distrust it:
    * index 3 is BLACK, a duplicate of 0. That is not a placeholder. The letter
      tiles use exactly indices {3,7}, which is black on white, and that is what
      the letters look like in a real screenshot.
    * only indices 0..7 occur in the art, so the upper plane is unused here.
  An earlier version of this tool used an EGA-bright ordering on the strength of
  reading the letters as cyan-on-white. That was a misread of `00FCFC` as
  `00FFFF`, and it made every bank look like dithered noise. The machine's own
  table is correct and the art proves it.

USAGE
    python3 tools/tile_atlas.py --out tmp/tiles      # if tiles are missing
    python3 tools/fmtowns_xwalk.py                   # write tmp/fmtowns-xwalk/app.html
    python3 tools/fmtowns_xwalk.py --serve           # and serve it on :8098
    python3 tools/fmtowns_xwalk.py --import FILE     # resolve an export back to text
    python3 tools/fmtowns_xwalk.py --prefill         # print the derived candidates only

Game content (the decoded tiles) is embedded in the generated page and is never
written to a tracked path. `tmp/` is gitignored.
"""
from __future__ import annotations

import argparse
import base64
import collections
import io
import json
import os
import re
import struct
import sys


def _pil_image():
    """Pillow, imported where it is used rather than at module scope.

    Only the art readers need it. `--check` validates the committed crosswalk and
    touches no image, but a top-level `from PIL import Image` made it die with
    ModuleNotFoundError on any host without Pillow -- which turned a passing
    structural gate into a red CI job for a reason unrelated to the property being
    gated. Importing on demand keeps `--check` runnable everywhere and names the
    missing dependency in the one mode that genuinely needs it.
    """
    try:
        from PIL import Image
    except ImportError:
        sys.exit("this mode needs Pillow (python3 -m pip install Pillow); "
                 "--check does not")
    return Image


ROOT = os.path.normpath(os.path.dirname(os.path.abspath(__file__)) + "/..")
DISC = os.path.join(ROOT, "references/fmtowns")
FRAMES = os.path.join(ROOT, "bestiary/cache/frames/fm-towns")
OUTDIR = os.path.join(ROOT, "tmp/fmtowns-xwalk")

TILE_W = TILE_H = 32
HDR = 512
TILE_BYTES = 512

# Tray order. All nine banks are embedded; HIDDEN_BY_DEFAULT decides which start
# unticked. Planet 2 and 3 are visible by default because the owner needs them to
# judge the 15 per-planet terrain tiles; the dungeon set is what starts folded.
BANK_ORDER = ["MAP2_1", "MAP2_2", "MAP2_3", "EXTRA", "ENEMY2", "PLAYER"]
DEFERRED_BANKS = ["CHEST", "DUNG1", "DUNG2"]
HIDDEN_BY_DEFAULT = ["CHEST", "DUNG1", "DUNG2"]

BANK_NOTE = {
    "MAP2_1": "planet 1 terrain (per-planet band; 16 = moongate) + A-Z at 38-63",
    "MAP2_2": "planet 2 terrain (per-planet band; 16 = shield) + A-Z at 38-63",
    "MAP2_3": "planet 3 terrain (per-planet band; 16 = shield) + A-Z at 38-63",
    "EXTRA": "objects: ships 16-19, horses 20-24, trees, buildings",
    "ENEMY2": "creatures",
    "PLAYER": "avatar",
    "CHEST": "dungeon", "DUNG1": "dungeon", "DUNG2": "dungeon",
}
DUNGEON_BANKS = ["CHEST", "DUNG1", "DUNG2"]

# Measured by comparing the three MAP2 banks tile by tile. Tiles 0, 3, 12, 17 to
# 34 and 36 to 63 are byte-identical across all three; the per-planet terrain is
# confined to 1-2, 4-5, 7-11, 13-16 and 35. Tile 16 is the interesting one: 2 and
# 3 are identical there and 1 is not, which is the moongate-versus-shield split
# the owner reported. Consequence for the crosswalk: a DOS art index maps to ONE
# tile for the shared band and up to THREE for the per-planet band.
MAP2_SHARED = [0, 3, 12] + list(range(17, 35)) + list(range(36, 64))
MAP2_PER_PLANET = [1, 2, 4, 5, 6, 7, 8, 9, 10, 11, 13, 14, 15, 16, 35]

# Read from the running machine, in-game table, indices 0..7. See the PALETTE
# note above: index 3 really is black.
PAL = [(0, 0, 0), (0, 0, 255), (255, 0, 0), (0, 0, 0),
       (0, 255, 0), (0, 255, 255), (255, 255, 0), (255, 255, 255)]
PAL_GREY = [(i * 36, i * 36, i * 36) for i in range(8)]
PAL_NAME = "in-game table read from the running machine"

KIND_ORDER = ["terrain", "object", "creature", "block", "font"]

# FM Towns art in bestiary/cache came from the Ultima Codex under the names it
# uses. The DOS side carries the same third-party names (tools/bestiary_match.py
# attached them by bitmap identity), so the two sides join on the name.
FM_FRAME_NAMES = {
    "orc": "FMOrc", "goblin": "FMGoblin", "wizard": "FMWizard",
    "guard": "FMGuard", "king": "FMKing", "merchant": "FMMerchant",
    "thief": "FMThief", "fighter": "FMFighter3", "cleric": "FMCleric2",
    "jester": "JesterFM-Towns", "demon": "FMDemon", "daemon": "FMDemon",
    "mane": "FMMane", "pazuzu": "FMPazuzu", "orcus": "FMOrcus",
    "sea serpent": "FMSeaSerp", "ship": "FMShip", "frigate": "FMShip",
}


# --------------------------------------------------------------------------
# ISO-9660 over raw 2352-byte CD sectors
# --------------------------------------------------------------------------
SEC, DSIZE = 2352, 2048


class RawISO:
    def __init__(self, path):
        self.f = open(path, "rb")
        self.f.seek(0, 2)
        self.nsec = self.f.tell() // SEC

    def block(self, lba, n=1):
        self.f.seek(lba * SEC)
        raw = self.f.read(SEC * n)
        return b"".join(raw[i * SEC + 16:i * SEC + 16 + DSIZE] for i in range(n))

    def read(self, lba, length):
        return self.block(lba, (length + DSIZE - 1) // DSIZE)[:length]


class DirRec:
    def __init__(self, raw, iso):
        self.iso = iso
        self.lba = struct.unpack_from("<I", raw, 2)[0]
        self.size = struct.unpack_from("<I", raw, 10)[0]
        self.flags = raw[25]
        self.nlen = raw[32]
        self.name = raw[33:33 + self.nlen].decode("latin-1")

    @property
    def is_dir(self):
        return bool(self.flags & 2)

    def children(self):
        data, out, off = self.iso.read(self.lba, self.size), [], 0
        while off < len(data):
            n = data[off]
            if n == 0:
                off = (off // DSIZE + 1) * DSIZE
                continue
            r = DirRec(data[off:off + n], self.iso)
            if not (r.nlen == 1 and r.name[0] in "\x00\x01"):
                out.append(r)
            off += n
        return out

    def data(self):
        return self.iso.read(self.lba, self.size)


def iso_root(iso):
    for s in range(16, 32):
        raw = iso.block(s)
        if raw[:7] == b"\x01CD001\x01":
            return DirRec(raw[156:190], iso)
    sys.exit("no ISO-9660 primary volume descriptor in the disc image")


def find_disc():
    if not os.path.isdir(DISC):
        sys.exit("no FM Towns media in references/fmtowns/")
    imgs = [f for f in sorted(os.listdir(DISC)) if f.lower().endswith(".img")]
    if not imgs:
        sys.exit("references/fmtowns/ holds no .img disc image")
    return os.path.join(DISC, imgs[0])


def disc_files(path):
    iso = RawISO(path)
    out, stack = {}, [iso_root(iso)]
    while stack:
        for c in stack.pop().children():
            if c.is_dir:
                stack.append(c)
                continue
            out[re.sub(r";\d+$", "", c.name).upper()] = c.data()
    return out


# --------------------------------------------------------------------------
# decoding
# --------------------------------------------------------------------------
TIFF_MAGIC = b"II*\x00"


def _tiff_first_ifd(blob):
    """Minimal little-endian TIFF IFD reader for the first IFD: {tag: scalar}.
    Scalar SHORT/LONG tags resolve to their value; array tags return their stored
    offset, which is enough for the scalar tags the monster banks use (256/257/266/273)."""
    if blob[:4] != TIFF_MAGIC:
        return None
    ifd = int.from_bytes(blob[4:8], "little")
    n = int.from_bytes(blob[ifd:ifd + 2], "little")
    tags = {}
    for k in range(n):
        e = ifd + 2 + k * 12
        tag = int.from_bytes(blob[e:e + 2], "little")
        typ = int.from_bytes(blob[e + 2:e + 4], "little")
        cnt = int.from_bytes(blob[e + 4:e + 8], "little")
        raw = blob[e + 8:e + 12]
        if typ == 3 and cnt == 1:
            tags[tag] = int.from_bytes(raw[:2], "little")
        else:
            tags[tag] = int.from_bytes(raw, "little")
    return tags


def bank_dims(blob):
    """(w, h, strip_offset, fill_order). Raw banks (DUNG1/DUNG2/CHEST/MAP*) are 32x32
    at HDR, high nibble = left. Monster banks (DUNMON1/2/3) are TIFF-wrapped with their
    own size (128/64/32) and FillOrder=2 (low nibble = left). See
    docs/fmtowns-dungeon-render-brief.md § 4."""
    t = _tiff_first_ifd(blob)
    if t is None:
        return TILE_W, TILE_H, HDR, 1
    return (t.get(256, TILE_W), t.get(257, TILE_H), t.get(273, HDR), t.get(266, 1))


def decode_bank(blob):
    """Decode a GRAPH bank into a list of index lists (one per sprite/tile).
    Raw banks are 32x32 4bpp high-nibble-left (values masked to 0..7, as measured);
    monster banks are TIFF-wrapped 4bpp low-nibble-left at 128/64/32, 26 sprites, and
    keep their full 0..15 range. The two formats are told apart by the TIFF magic."""
    w, h, strip, fill = bank_dims(blob)
    body = blob[strip:]
    per = (w * h) // 2
    tiles = []
    for t in range(len(body) // per):
        base = t * per
        px = [0] * (w * h)
        for i in range(w * h):
            b = body[base + (i >> 1)]
            lo, hi = b & 0xF, (b >> 4) & 0xF
            if fill == 2:
                px[i] = lo if i % 2 == 0 else hi
            else:
                px[i] = (hi if i % 2 == 0 else lo) & 7
        tiles.append(px)
    return tiles


def tile_hw(px):
    """(w, h) of a decoded tile, derived from its length (banks are square)."""
    d = int(round(len(px) ** 0.5))
    return d, d


def png_from_indices(px, pal, w=TILE_W, h=TILE_H):
    im = _pil_image().new("RGB", (w, h))
    n = len(pal)
    im.putdata([pal[i] if i < n else pal[-1] for i in px])
    buf = io.BytesIO()
    im.save(buf, "PNG")
    return "data:image/png;base64," + base64.b64encode(buf.getvalue()).decode()


def b64_file(path):
    return "data:image/png;base64," + base64.b64encode(open(path, "rb").read()).decode()


# --------------------------------------------------------------------------
# our side
# --------------------------------------------------------------------------
def load_tile_art():
    """disasm/tile-art.tsv -> {art_index: {kind, description, glyph}}."""
    path = os.path.join(ROOT, "disasm/tile-art.tsv")
    art = {}
    for line in open(path):
        line = line.rstrip("\n")
        if not line or line.startswith("#"):
            continue
        parts = line.strip().split("\t")
        if len(parts) < 4 or not re.fullmatch(r"[0-9A-Fa-f]{2}", parts[0]):
            continue
        art[int(parts[0], 16)] = {
            "kind": parts[2].strip() or "?",
            "description": parts[3].strip(),
            "glyph": parts[4].strip() if len(parts) > 4 else ""}
    if not art:
        sys.exit("no rows parsed from %s" % path)
    return art


def dos_tiles(d):
    t = {}
    for n in range(64):
        p = os.path.join(d, "tile_%02X.png" % n)
        if os.path.exists(p):
            t[n] = b64_file(p)
    if len(t) < 64:
        sys.exit("need 64 DOS tiles, found %d in %s.\n"
                 "  Run: python3 tools/tile_atlas.py --out tmp/tiles" % (len(t), d))
    return t


def bestiary_names():
    """art_index -> third-party creature name, from the generated bestiary table."""
    path = os.path.join(ROOT, "bestiary/bestiary.tsv")
    out = {}
    if not os.path.exists(path):
        return out
    for line in open(path):
        if line.startswith("#"):
            continue
        c = line.rstrip("\n").split("\t")
        if len(c) < 2 or not re.fullmatch(r"[0-9A-Fa-f]{2}", c[0]):
            continue
        if c[1] and c[0].upper() not in out:
            out[c[0].upper()] = c[1]
    return out


# --------------------------------------------------------------------------
# derived candidates
# --------------------------------------------------------------------------
def silhouette(png_path):
    """The Codex FM frames carry no alpha; the background is the corner colour."""
    im = _pil_image().open(png_path).convert("RGB")
    if im.size != (TILE_W, TILE_H):
        im = im.resize((TILE_W, TILE_H))
    a = im.load()
    bg = a[0, 0]
    return [1 if a[x, y] != bg else 0 for y in range(TILE_H) for x in range(TILE_W)]


def iou(a, b):
    inter = union = 0
    for x, y in zip(a, b):
        if x and y:
            inter += 1
        if x or y:
            union += 1
    return inter / union if union else 0.0


def fm_frame_sils():
    """{creature name: [silhouette per frame]} from the Codex frames we already hold."""
    out = {}
    if not os.path.isdir(FRAMES):
        return out
    for d in sorted(os.listdir(FRAMES)):
        p = os.path.join(FRAMES, d, "frame_00.png")
        if not os.path.exists(p):
            continue
        key = frame_key(d)
        if key:
            out.setdefault(key, []).append(silhouette(p))
    return out


def build_prefill(art, banks):
    """Everything about the crosswalk that can be DERIVED, as candidates only.

    Two sources, and they are not equally strong:
      * letters: the DOS font rows carry their glyph, and MAP2_1 tiles 38..63 are
        A..Z in order. That is a measured, structural match.
      * creatures: DOS art names and Codex FM frame names are both third-party
        names, joined by name, then located by silhouette. Name plus shape, and
        the shape step is weak between humanoids. Treat as a hint.
    """
    cands = dict(OWNER_SETS)
    why = dict(OWNER_WHY)
    conf = {k: "owner" for k in OWNER_SETS}
    sils = {bn: [sil_from_idx(t) for t in ts] for bn, ts in banks.items()
            if ts and len(ts[0]) == TILE_W * TILE_H}
    frames = fm_frame_sils()
    names = bestiary_names()

    for n, row in art.items():
        key = format(n, "02X")
        g = row["glyph"]
        if row["kind"] == "font" and len(g) == 1 and "A" <= g <= "Z":
            cands[key] = ["MAP2_1/%d" % (38 + ord(g) - ord("A"))]
            why[key] = "letter %s: DOS font row -> MAP2_1 tile 38+index (structural)" % g
            conf[key] = "strong"
            continue
        name = names.get(key) or row["description"].split(" \u2014 ")[0].strip()
        fkey = frame_key(name)
        if not fkey or fkey not in frames:
            continue
        best = None
        for s in frames[fkey]:
            for bn, sl in sils.items():
                for t, x in enumerate(sl):
                    sc = iou(s, x)
                    if best is None or sc > best[0]:
                        best = (sc, "%s/%d" % (bn, t))
        if best and best[0] >= 0.55 and key not in cands:
            cands[key] = [best[1]]
            # The gap to the runner-up is the honest confidence signal: a humanoid
            # that beats the next humanoid by 0.01 is the NAME doing the work, not
            # the shape. Surfaced as the ghost border colour so the eye goes to the
            # thin ones first.
            nxt = 0.0
            for s in frames[fkey]:
                for bn, sl in sils.items():
                    for t, x in enumerate(sl):
                        sc = iou(s, x)
                        if sc < best[0] and sc > nxt:
                            nxt = sc
            gap = best[0] - nxt
            conf[key] = "strong" if gap >= 0.06 else (
                "thin" if best[0] >= 0.65 else "weak")
            why[key] = "creature %r -> Codex FM frame: IoU %.2f, gap to runner-up %.2f (%s)" % (
                name, best[0], gap, conf[key])
    return cands, why, conf


# Owner-reported pairings (Paul, 2026-11-04), given while reviewing the tray. An
# owner observation outranks an agent inference, so these win over the silhouette
# guess below. The ranges are contiguous runs in the bank, which is the shape a
# multi-frame FM Towns object takes.
OWNER_SETS = {
    "11": ["EXTRA/%d" % i for i in range(20, 25)],   # horse: 20..24
    "12": ["EXTRA/%d" % i for i in range(16, 20)],   # boat: 16..19
}
OWNER_WHY = {
    "11": "owner: the horse is EXTRA 20-24",
    "12": "owner: the boat is EXTRA 16-19",
}


def frame_key(name):
    """A DOS-side creature name -> the key fm_frame_sils() files the Codex
    frames under. FM_FRAME_NAMES covers the spellings that differ (Daemon vs
    Demon, Frigate vs Ship); everything else joins on the name itself."""
    if not name or len(name) > 20:
        return None
    n = name.strip().lower()
    n = FM_FRAME_NAMES.get(n, n)
    return re.sub(r"^(fm|FM)|fm-towns$|FM-Towns$", "", n).strip("-").lower()


def sil_from_idx(px):
    return [1 if v else 0 for v in px]


# --------------------------------------------------------------------------
# the page
# --------------------------------------------------------------------------
def build_html(dos, banks, art, prefill, why, conf):
    groups = collections.defaultdict(list)
    for n, row in art.items():
        groups[row["kind"]].append(n)
    for k in groups:
        groups[k].sort()
    order = [x for x in KIND_ORDER if x in groups] + \
            [x for x in sorted(groups) if x not in KIND_ORDER]

    dos_json = json.dumps({
        "n": [format(k, "02X") for k in sorted(dos)],
        "img": {format(k, "02X"): v for k, v in dos.items()},
        "meta": {format(k, "02X"): v for k, v in art.items()},
        "order": order,
    })
    banks_json = json.dumps({
        "names": list(banks),
        "note": BANK_NOTE,
        # Deferred banks are embedded but start hidden, so switching to planet 2,
        # planet 3 or the dungeon set is a tick, not a rebuild. Owner asked for
        # this after the planet-1-only default hid the only candidates that exist
        # for DOS arts 17, 3A and 3B, which are dungeon tiles.
        "hidden": [b for b in HIDDEN_BY_DEFAULT if b in banks],
        "c": {b: [png_from_indices(t, PAL, *tile_hw(t)) for t in v] for b, v in banks.items()},
        "g": {b: [png_from_indices(t, PAL_GREY, *tile_hw(t)) for t in v] for b, v in banks.items()},
    })
    # A data URL missing its prefix renders as a silent 404 per tile, which in a
    # browser looks like an empty tray rather than a bug. Refuse to emit that.
    for label, blob in (("dos", dos_json), ("banks", banks_json)):
        if '"data:image/' not in blob:
            sys.exit("internal error: %s block carries no data:image URLs" % label)
        if re.search(r'"iVBOR', blob):
            sys.exit("internal error: %s block has a bare base64 PNG" % label)
    return (PAGE.replace("__DOS__", dos_json)
                .replace("__BANKS__", banks_json)
                .replace("__PREFILL__", json.dumps(prefill))
                .replace("__WHY__", json.dumps(why))
                .replace("__CONF__", json.dumps(conf)))


PAGE = r"""<!DOCTYPE html><html><head><meta charset="utf-8">
<title>FM Towns crosswalk</title><style>
:root{--bg:#15171a;--fg:#e6e6e6;--dim:#8a8f98;--line:#2b2f36;--hi:#ffd24a}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);font:13px/1.45 system-ui,sans-serif;
 padding-bottom:270px}
header{position:sticky;top:0;z-index:9;background:#1b1e22;border-bottom:1px solid var(--line);
 padding:7px 14px;display:flex;gap:11px;align-items:center;flex-wrap:wrap}
h1{font-size:14px;margin:0 6px 0 0}
button,select{background:#2a2f36;color:var(--fg);border:1px solid #3c434c;border-radius:4px;
 padding:4px 8px;font:inherit;cursor:pointer}
button:hover{background:#39414b}
#stat{color:var(--hi);font-variant-numeric:tabular-nums}
main{display:flex;gap:0;align-items:flex-start}
#left{flex:1 1 auto;padding:8px 14px;max-width:860px}
h2{font-size:11px;text-transform:uppercase;letter-spacing:.09em;color:var(--dim);
 margin:14px 0 5px;border-bottom:1px solid var(--line);padding-bottom:3px}
.row{display:flex;align-items:center;gap:9px;padding:3px 6px;border-radius:5px}
.row:hover{background:#1c1f24}
img.dos{width:44px;height:44px;image-rendering:pixelated;border:1px solid #3a3f47;background:#000}
.idx{font:12px ui-monospace,monospace;color:var(--hi);width:30px;flex:none}
.desc{flex:1 1 auto;color:var(--dim);font-size:12px;min-width:80px}
.slot{min-width:52px;height:52px;flex:none;border:1px dashed #4a525c;border-radius:4px;
 display:flex;flex-wrap:wrap;align-content:center;justify-content:center;background:#101215;
 position:relative;max-width:112px}
.slot.over{border-color:var(--hi);background:#232830}
.slot.filled{border-style:solid;border-color:#3f6b46}
.slot.none{border-style:solid;border-color:#6b3f3f;background:#1a1416}
.slot img{width:25px;height:25px;image-rendering:pixelated;cursor:grab}
.slot.one img{width:48px;height:48px}
.slot .lbl,.slot .no{color:#5c636c;font-size:10px}
.slot .no{color:#a06a6a;text-align:center}
.ghost{position:absolute;inset:0;display:flex;flex-wrap:wrap;align-content:center;
 justify-content:center;border:2px dashed #2f7d8f;border-radius:4px;background:#0d2a31cc;
 cursor:pointer;overflow:hidden}
.ghost img{width:24px;height:24px;image-rendering:pixelated}
.ghost.one img{width:48px;height:48px}
.ghost .gtag{position:absolute;right:0;bottom:0;background:#04222acc;color:#9fe8ff;
 font:9px ui-monospace,monospace;padding:0 3px}
.ghost.c-owner{border-color:#3f9b52;background:#0d2a1acc}
.ghost.c-strong{border-color:#2f7d8f;background:#0d2a31cc}
.ghost.c-thin{border-color:#c8963e;background:#2a2010cc}
.ghost.c-weak{border-color:#b05555;background:#2a1414cc}
.why{font-size:10px;color:#5f8fa0;flex:0 0 150px}
#tray{position:fixed;left:0;right:0;bottom:0;background:#1a1d21;border-top:2px solid #3a4048;
 padding:5px 12px 7px;max-height:270px;overflow:auto}
#tray h3{margin:2px 0 3px;font-size:11px;text-transform:uppercase;letter-spacing:.06em;
 color:var(--dim)}
#tray h3 span{color:#5c636c;text-transform:none;letter-spacing:0}
.bank{display:flex;flex-wrap:wrap;gap:3px;margin-bottom:5px}
.t{position:relative;width:34px;height:34px;border:1px solid #333a42;background:#000;cursor:grab}
.t img{width:32px;height:32px;image-rendering:pixelated;display:block}
.t .l{position:absolute;left:0;bottom:0;background:#000a;color:#bcd;font:8px ui-monospace,monospace;
 padding:0 2px}
.t.used{border-color:#2f6b3a}
.t.used::after{content:attr(data-c);position:absolute;right:0;top:0;background:#2f6b3a;color:#fff;
 font:8px ui-monospace,monospace;padding:0 2px;border-radius:0 0 0 3px}
.hint{color:var(--dim);font-size:11px}
#right{flex:0 0 250px;padding:8px 12px;border-left:1px solid var(--line);position:sticky;top:44px}
#preview{display:grid;grid-template-columns:repeat(4,1fr);gap:4px}
#preview img{width:100%;image-rendering:pixelated;border:1px solid #333a42;background:#000}
label{color:var(--dim);font-size:11px}
.banksel{display:flex;gap:8px;align-items:center;flex-wrap:wrap;margin-bottom:5px;
 font-size:11px;color:var(--dim)}
.bsel{display:inline-flex;gap:3px;align-items:center;padding:1px 6px;border:1px solid #3c434c;
 border-radius:10px;cursor:pointer;color:#7c848e}
.bsel.on{border-color:#3f6b46;color:#cfe}
.bsel input{margin:0}
</style></head><body>
<header>
 <h1>DOS &rarr; FM Towns</h1>
 <span id="stat"></span>
 <label><input type="checkbox" id="unmapped"> unmapped only</label>
 <label>palette <select id="pal"><option value="c">colour (machine table)</option>
  <option value="g">greyscale</option></select></label>
 <button id="accept">accept all candidates</button>
 <button id="exp">export TSV</button><button id="expj">export JSON</button>
 <button id="copy">copy</button><button id="clr">clear all</button>
 <span class="hint">drag to <b>add</b> a tile to a row's set &middot; click a placed tile to
  remove it &middot; right-click a row = no equivalent &middot; dashed box = derived
  candidate, click to accept &middot; border:
  <b style="color:#3f9b52">green</b>=owner,
  <b style="color:#2f7d8f">teal</b>=strong,
  <b style="color:#c8963e">amber</b>=thin,
  <b style="color:#b05555">red</b>=weak</span>
</header>
<main><div id="left"></div>
 <div id="right"><h2>mapped</h2><div id="preview"></div></div></main>
<div id="tray"></div>
<script>
const DOS=__DOS__, BANKS=__BANKS__, PREFILL=__PREFILL__, WHY=__WHY__, CONF=__CONF__;
const KEY='u2.fmtowns.xwalk.v3';
let V=JSON.parse(localStorage.getItem(KEY)||'{}');
let pal=localStorage.getItem(KEY+'.pal')||'c';
const NONE='NONE';
const save=()=>localStorage.setItem(KEY,JSON.stringify(V));
const src=(b,m)=>(pal==='g'?BANKS.g:BANKS.c)[b][m];
const list=v=>Array.isArray(v)?v:[v];
const real=n=>DOS.meta[n]&&DOS.meta[n].kind!=='font';
const done=()=>DOS.n.filter(n=>V[n]&&real(n)).length;
function stat(){document.getElementById('stat').textContent=done()+' / '+
  DOS.n.filter(real).length+' mapped'}
function counts(){const c={};for(const a in V){if(V[a]===NONE)continue;
  for(const t of list(V[a]))c[t]=(c[t]||0)+1}return c}

function makeSlot(key){
 const s=document.createElement('div');s.dataset.art=key;const v=V[key];
 if(v===NONE){s.className='slot none';s.innerHTML='<span class="no">none</span>'}
 else if(v){const ts=list(v);s.className='slot'+(ts.length===1?' one':'')+' filled';
  ts.forEach(t=>{const[b,m]=t.split('/');const im=document.createElement('img');
   im.src=src(b,+m);im.draggable=true;im.title=t+' — click to remove';
   im.onclick=()=>{const x=list(V[key]).filter(y=>y!==t);
     if(x.length)V[key]=x;else delete V[key];save();render()};
   im.addEventListener('dragstart',e=>{e.dataTransfer.setData('text/plain',t);
     const x=list(V[key]).filter(y=>y!==t);if(x.length)V[key]=x;else delete V[key]});
   s.appendChild(im)});
 } else {s.className='slot';s.innerHTML='<span class="lbl">drop</span>'};
 if(!v&&PREFILL[key]){const ts=PREFILL[key];const g=document.createElement('div');
  g.className='ghost'+(ts.length===1?' one':'')+(CONF[key]?' c-'+CONF[key]:'');
  ts.slice(0,4).forEach(t=>{const[b,m]=t.split('/');const gi=document.createElement('img');
   gi.src=src(b,+m);g.appendChild(gi)});
  const tag=document.createElement('span');tag.className='gtag';
  tag.textContent=ts.length>1?ts.length+' tiles':ts[0];g.appendChild(tag);
  g.title=(ts.join(' ')+'\n'+(WHY[key]||'derived candidate')+'\nclick to accept');
  g.onclick=()=>{V[key]=ts.slice();save();render()};s.appendChild(g)}
 s.addEventListener('dragover',e=>{e.preventDefault();s.classList.add('over')});
 s.addEventListener('dragleave',()=>s.classList.remove('over'));
 s.addEventListener('drop',e=>{e.preventDefault();s.classList.remove('over');
  const x=e.dataTransfer.getData('text/plain');
  if(!/^(\w+)\/\d+$/.test(x))return;
  const cur=V[key]&&V[key]!==NONE?list(V[key]):[];
  if(!cur.includes(x))cur.push(x);V[key]=cur;save();render()});
 s.addEventListener('contextmenu',e=>{e.preventDefault();
  if(V[key])delete V[key];else V[key]=NONE;save();render()});
 return s;
}

// Which tray groups start visible. Everything is embedded either way.
let SHOW = JSON.parse(localStorage.getItem(KEY + '.show') || 'null')
     || Object.fromEntries(BANKS.names.map(b => [b, !BANKS.hidden.includes(b)]));
const setShow = () => localStorage.setItem(KEY + '.show', JSON.stringify(SHOW));

function render(){
 const L=document.getElementById('left');L.innerHTML='';
 for(const kind of DOS.order){
  const ns=DOS.n.filter(n=>DOS.meta[n]&&DOS.meta[n].kind===kind);
  if(!ns.length)continue;
  const h=document.createElement('h2');h.textContent=kind+' ('+ns.length+')';L.appendChild(h);
  for(const n of ns){
   if(document.getElementById('unmapped').checked&&V[n])continue;
   const r=document.createElement('div');r.className='row';const m=DOS.meta[n];
   const i1=document.createElement('span');i1.className='idx';i1.textContent=n;
   const im=document.createElement('img');im.className='dos';im.src=DOS.img[n];
   const ds=document.createElement('span');ds.className='desc';
   ds.textContent=(m.description||'')+(m.glyph?'  ['+m.glyph+']':'');
   r.append(i1,im,ds,r.appendChild(makeSlot(n)));
   if(PREFILL[n]&&!V[n]){const w=document.createElement('span');w.className='why';
    w.textContent=WHY[n]||'candidate';r.appendChild(w)}
   L.appendChild(r);
  }
 }
 const C=counts(),T=document.getElementById('tray');T.innerHTML='';
 const bar=document.createElement('div');bar.className='banksel';
 bar.innerHTML='<span>banks:</span>';
 for(const b of BANKS.names){
  const lb=document.createElement('label');lb.className='bsel'+(SHOW[b]?' on':'');
  const cb=document.createElement('input');cb.type='checkbox';cb.checked=!!SHOW[b];
  cb.onchange=()=>{SHOW[b]=cb.checked;setShow();render()};
  lb.append(cb,document.createTextNode(b));bar.appendChild(lb);
 }
 T.appendChild(bar);
 for(const b of BANKS.names){
  if(!SHOW[b])continue;
  const hh=document.createElement('h3');
  hh.innerHTML=b+' <span>'+(BANKS.note[b]||'')+' &middot; '+BANKS.c[b].length+'</span>';
  T.appendChild(hh);
  const row=document.createElement('div');row.className='bank';
  BANKS.c[b].forEach((_,i)=>{const id=b+'/'+i;
   const d=document.createElement('div');d.className='t'+(C[id]?' used':'');
   d.dataset.c=C[id]||'';d.title=id;d.draggable=true;
   d.innerHTML='<img src="'+src(b,i)+'"><span class="l">'+i+'</span>';
   d.addEventListener('dragstart',e=>e.dataTransfer.setData('text/plain',id));
   row.appendChild(d)});
  T.appendChild(row);
 }
 const P=document.getElementById('preview');P.innerHTML='';
 for(const n of DOS.n){const v=V[n];if(!v||v===NONE)continue;
  for(const t of list(v)){const im=document.createElement('img');im.src=src(...t.split('/').map((x,i)=>i?+x:x));
   im.title=n+' = '+t;P.appendChild(im)}}
 stat();
}

document.getElementById('unmapped').onchange=render;
document.getElementById('pal').onchange=e=>{pal=e.target.value;
 localStorage.setItem(KEY+'.pal',pal);render()};
document.getElementById('pal').value=pal;
document.getElementById('accept').onclick=()=>{
 let n=0;for(const k in PREFILL){if(!V[k]){V[k]=PREFILL[k].slice();n++}}
 save();render();alert('accepted '+n+' candidates')};
function tsv(){
 let s='# fmtowns-crosswalk\n# art_index\tfm_tiles\tcount\tnote\n';
 for(const n of DOS.n){const v=V[n];if(!v)continue;
  if(v===NONE){s+=n+'\t-\t0\tno equivalent\n';continue}
  const ts=list(v);s+=n+'\t'+ts.join(',')+'\t'+ts.length+'\t\n'}
 return s}
function dl(name,txt){const a=document.createElement('a');
 a.href=URL.createObjectURL(new Blob([txt],{type:'text/plain'}));a.download=name;a.click()}
document.getElementById('exp').onclick=()=>dl('fmtowns-crosswalk.tsv',tsv());
document.getElementById('expj').onclick=()=>dl('fmtowns-crosswalk.json',JSON.stringify(V,null,1));
document.getElementById('copy').onclick=()=>navigator.clipboard.writeText(tsv());
document.getElementById('clr').onclick=()=>{
 if(confirm('clear all '+Object.keys(V).length+' mappings?')){V={};save();render()}};
render();
</script></body></html>
"""


# --------------------------------------------------------------------------
def do_import(path):
    art = load_tile_art()
    txt = open(path).read()
    pairs = {}
    if path.endswith(".json"):
        pairs = json.loads(txt)
    else:
        for line in txt.splitlines():
            if line.startswith("#") or not line.strip():
                continue
            c = line.split("\t")
            if len(c) >= 3:
                # Matches the export: art_index, comma-joined BANK/tile list,
                # count, note. A '-' in the tile column is an explicit none.
                pairs[c[0].strip().upper()] = ("none" if c[1].strip() == "-"
                                              else c[1].strip())
    print("# art\tkind\tfm tiles\twhat the DOS tile is")
    for a in sorted(pairs, key=lambda x: int(x, 16)):
        m = art.get(int(a, 16), {})
        print("%s\t%s\t%s\t%s" % (a, m.get("kind", "?"), pairs[a],
                                  m.get("description", "")))
    miss = [format(a, "02X") for a in art
            if format(a, "02X") not in pairs and art[a]["kind"] != "font"]
    if miss:
        print("# unmapped (%d): %s" % (len(miss), " ".join(sorted(miss))))
        # Usable as a gate: a crosswalk with holes is not done. Font rows are
        # excluded because the FM set has no glyph for the DOS 'Q' slot, which is
        # bars rather than a letter, so 26 letters can never all land.
        sys.exit(1)
    print("# complete: every non-font art entry is mapped or explicitly none")


def do_export(owner_path, out_path, banks, art):
    """Owner JSON + derived candidates -> the committed crosswalk TSV.

    The owner mapping is the authored input; the derived candidates fill only what
    the owner left open. Re-running this reproduces the committed table exactly,
    which is what makes the TSV a generated artefact rather than a hand-maintained
    one that will drift and lie.
    """
    owner = json.loads(open(owner_path).read())
    prefill, _why, conf = build_prefill(art, banks)
    merged = {}
    for k in set(owner) | set(prefill):
        if not re.fullmatch(r"[0-9A-Fa-f]{2}", k):
            continue
        n = int(k, 16)
        if n not in art:
            continue
        key = format(n, "02X")
        if key in owner:
            merged[key] = (list(owner[key]), "owner")
        elif art[n]["kind"] != "font":
            merged[key] = (list(prefill[k]), conf.get(k, "derived"))
        else:
            merged[key] = (list(prefill[k]), conf.get(k, "derived"))

    lines = [
        "# fmtowns-crosswalk.tsv -- DOS art index -> FM Towns tile set.",
        "#",
        "# GENERATED by `tools/fmtowns_xwalk.py --export`. Do not hand-edit: edit",
        "# the owner mapping and re-export. Tiles are BANK/N.",
        "#",
        "# Format: art_index, comma-joined BANK/tile list, count, source, description.",
        "# source: `owner` = named by Paul; `strong`/`thin`/`weak` = derived from the",
        "# DOS creature name joined to a Codex FM frame, graded by silhouette gap to",
        "# the runner-up. A tile repeated across rows is a deliberate collapse.",
        "#",
        "# Scope: OVERWORLD AND TOWNS ONLY. Dungeon and tower art is not mapped",
        "# (owner, 2026-11-04). The PLAYER bank is entirely unmapped because FM Towns",
        "# varies the avatar by race and class, an axis the DOS atlas does not have.",
        "# Horse and boat have directional variants not yet resolved; each row carries",
        "# the animation pair only.",
        "#",
        "# 26 of the 64 DOS entries are the A-Z sign glyphs and map structurally to",
        "# MAP2_1 tiles 38..63. The DOS Q slot holds bars, not a letter, so it has no",
        "# counterpart and is absent.",
        "#",
        "# One row is not planet 1: art 17 (forcefield) maps to MAP2_1/34, chosen over",
        "# the true FM identification MAP2_3/35 so the table resolves against a single",
        "# planet-1 atlas. See docs/fmtowns-graphics.md.",
        "#",
        "# art\tfm_tiles\tcount\tsource\tdescription",
    ]
    for k in sorted(merged, key=lambda x: int(x, 16)):
        ts, src = merged[k]
        lines.append("%s\t%s\t%d\t%s\t%s"
                     % (k, ",".join(ts), len(ts), src, art[int(k, 16)]["description"]))
    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    with open(out_path, "w") as f:
        f.write("\n".join(lines) + "\n")
    print("wrote %s: %d rows" % (out_path, len(merged)))


KNOWN_BANKS = BANK_ORDER + DEFERRED_BANKS


def do_check(path, art):
    """Gate on the committed crosswalk. Disc-free: validates structure, not pixels.

    Fails on: a missing non-font entry, an unknown bank, a tile index outside the
    bank, a letter row that breaks the structural rule, or a malformed row. A tile
    used by more than one row is NOT a failure, because the owner collapsed the
    banner frame-rule tiles deliberately.
    """
    problems, rows = [], {}
    for ln, line in enumerate(open(path), 1):
        line = line.rstrip("\n")
        if not line or line.startswith("#"):
            continue
        c = line.split("\t")
        if len(c) < 4:
            problems.append("line %d: expected 5 tab-separated fields" % ln)
            continue
        idx, tiles = c[0].strip(), c[1].strip()
        if not re.fullmatch(r"[0-9A-F]{2}", idx):
            problems.append("line %d: bad art index %r" % (ln, idx))
            continue
        rows[idx] = tiles
        if tiles == "-":
            continue
        for t in tiles.split(","):
            m = re.fullmatch(r"(\w+)/(\d{1,2})", t)
            if not m:
                problems.append("line %d: bad tile %r" % (ln, t))
                continue
            bank, num = m.group(1), int(m.group(2))
            if bank not in KNOWN_BANKS:
                problems.append("line %d: unknown bank %r" % (ln, bank))
            if not 0 <= num <= 63:
                problems.append("line %d: tile %s outside 0..63" % (ln, t))

    for n, row in art.items():
        key = format(n, "02X")
        if row["kind"] != "font" and key not in rows:
            problems.append("art %s (%s) is not mapped and not marked none"
                            % (key, row["kind"]))
        if row["kind"] == "font" and len(row["glyph"]) == 1 and "A" <= row["glyph"] <= "Z":
            want = "MAP2_1/%d" % (38 + ord(row["glyph"]) - ord("A"))
            got = rows.get(key, "")
            if got and got != want:
                problems.append("art %s is letter %s but maps to %s, expected %s"
                                % (key, row["glyph"], got, want))

    if problems:
        for p in problems:
            print("FAIL %s" % p)
        sys.exit(1)
    print("fmtowns crosswalk OK: %d rows, every non-font entry mapped, "
          "letter rows consistent" % len(rows))


def main():
    ap = argparse.ArgumentParser(description="FM Towns crosswalk editor")
    ap.add_argument("--out", default=os.path.join(OUTDIR, "app.html"))
    ap.add_argument("--tiles", default=os.path.join(ROOT, "tmp/tiles"))
    ap.add_argument("--banks", default=",".join(BANK_ORDER + DEFERRED_BANKS),
                    help="comma list to embed; deferred banks are embedded but start "
                         "hidden behind a tick in the tray")
    ap.add_argument("--serve", action="store_true")
    ap.add_argument("--port", type=int, default=8098)
    ap.add_argument("--import", dest="imp", metavar="FILE")
    ap.add_argument("--export", nargs=2, metavar=("OWNER_JSON", "OUT_TSV"),
                    help="merge an owner mapping with the derived candidates into a TSV")
    ap.add_argument("--check", metavar="TSV",
                    help="gate a crosswalk TSV; exits non-zero if it is incomplete or wrong")
    ap.add_argument("--prefill", action="store_true",
                    help="print the derived candidates and exit")
    args = ap.parse_args()

    if args.imp:
        do_import(args.imp)
        return

    if args.check:
        do_check(args.check, load_tile_art())
        return

    files = disc_files(find_disc())
    banks = {}
    for name in args.banks.split(","):
        name = name.strip().upper()
        if not name:
            continue
        if name + ".TIF" not in files:
            sys.exit("no GRAPH/%s.TIF on the disc" % name)
        banks[name] = decode_bank(files[name + ".TIF"])

    art = load_tile_art()
    prefill, why, conf = build_prefill(art, banks)

    if args.export:
        do_export(args.export[0], args.export[1], banks, art)
        return

    # The per-planet claim in BANK_NOTE is a measured fact, so re-measure it here
    # rather than trust the comment. If the banks ever change, this goes red.
    if all(b in banks for b in ("MAP2_1", "MAP2_2", "MAP2_3")):
        b1, b2, b3 = banks["MAP2_1"], banks["MAP2_2"], banks["MAP2_3"]
        shared = [t for t in range(64) if b1[t] == b2[t] == b3[t]]
        if shared != MAP2_SHARED:
            sys.exit("MAP2_SHARED is stale: measured %s" % shared)
        if sorted(MAP2_SHARED + MAP2_PER_PLANET) != list(range(64)):
            sys.exit("MAP2_SHARED + MAP2_PER_PLANET does not tile 0..63")
        if not (b2[16] == b3[16] and b1[16] != b2[16]):
            sys.exit("tile 16 no longer splits 1 vs 2==3: the moongate/shield "
                     "claim needs re-checking")
        print("MAP2 banks: %d tiles shared, %d per-planet; tile 16 splits 1 vs 2==3"
              % (len(shared), len(MAP2_PER_PLANET)))

    if args.prefill:
        for k in sorted(prefill, key=lambda x: int(x, 16)):
            print("%s -> %-14s  %s" % (k, ",".join(prefill[k]), why[k]))
        print("# %d of 64 art entries derived" % len(prefill))
        return

    html = build_html(dos_tiles(args.tiles), banks, art, prefill, why, conf)
    grades = collections.Counter(conf.values())
    print("candidates by confidence: " +
          "  ".join("%s=%d" % (k, grades[k]) for k in
                    ["owner", "strong", "thin", "weak"] if grades[k]))
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, "w") as f:
        f.write(html)
    print("banks: " + "  ".join("%s=%d" % (b, len(banks[b])) for b in banks))
    print("derived candidates: %d" % len(prefill))
    print("wrote %s (%.1f MB)" % (args.out, len(html) / 1e6))

    if args.serve:
        import functools
        import http.server
        d = os.path.dirname(os.path.abspath(args.out))
        h = functools.partial(http.server.SimpleHTTPRequestHandler, directory=d)
        print("serving http://0.0.0.0:%d/%s  (Ctrl-C to stop)"
              % (args.port, os.path.basename(args.out)))
        http.server.ThreadingHTTPServer(("0.0.0.0", args.port), h).serve_forever()


if __name__ == "__main__":
    main()

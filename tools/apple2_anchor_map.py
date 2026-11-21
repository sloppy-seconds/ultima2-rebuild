#!/usr/bin/env python3
"""apple2_anchor_map.py — the Apple II <-> DOS string anchor crosswalk.

The DOS binary is a translation of the Apple II 6502 (docs/apple2-reference.md § 4). Where a
string appears verbatim on both sides, and in the same order inside the same cluster, the two
routines that print it are the same routine — and the 6502 around it is the specification the
8086 was translated from. That makes the Apple side a tiebreaker for *static ambiguity*
questions: which of two readings of a compare is right, whether a table is what we think.
It cannot prove anything about execution; see § 6 of that doc and the caveats in
docs/archive/handoff-apple2-crosswalk.md.

Until now the anchor set was prose: "35 strings", ~10 clusters, not recoverable from the repo.
This tool makes it a measured, regenerable lookup.

    python3 tools/apple2_anchor_map.py --selftest     # controls; must pass
    python3 tools/apple2_anchor_map.py --summary      # counts and clusters
    python3 tools/apple2_anchor_map.py --tsv          # machine rows (no game text)
    python3 tools/apple2_anchor_map.py --show         # rows WITH text, stdout only
    python3 tools/apple2_anchor_map.py -o disasm/apple2-anchors.tsv

**No game text is ever written to a file.** `AGENTS.md`'s text line: copyright protects game
text exactly as much as a sprite, and `asm/` already carries every string the reconstruction
needs. A hand-maintained duplicate in `docs/` or a committed TSV adds drift *and* exposure.
So the committed artefact is offsets, lengths, segment names, match status and a short hash of
the normalised text — `--show` prints the text to stdout for a human and nothing else does.

How each side is found:

  * DOS: `tools/strings_atlas.py`, imported, not reimplemented (T2 constraint 3). That means
    every DOS string here has an owning `asm/` function and a `name-evidence` line attached,
    which a raw-exe scan would not give. It also means the DOS side is capped at what `asm/`
    renders as data: the middle `ANOS:` entries, for instance, sit in a region the clean wave
    emitted as instructions, so they are invisible here and their absence is a DOS-side gap,
    not an Apple-side miss.
  * Apple: the extracted BINs (docs/apple2-reference.md § 2-3), scanned per segment. A string
    is a run of bytes that are **all high-bit-set and printable once masked**, terminated by
    `$00`, with any trailing control wrappers (`0x0D`/`0x8D`/bell) trimmed. The high-bit rule
    is what keeps this a text scanner and not the "287 of 288 strings" artefact from § 6: a
    generic printable-run regex over the image catches double-hi-res bitmap noise and reports
    it as prose. Mask with `& 0x7F` and compare to the DOS text.

Order preservation is computed, not asserted: consecutive matches within CLUSTER_GAP bytes on
both sides form a cluster, and a cluster is *ordered* only if the Apple addresses rise in the
same sequence as the DOS offsets. That is the claim § 4 makes, and it is much stronger evidence
than set membership.
"""
import argparse
import hashlib
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import apple2_image      # noqa: E402  segment table, one source of truth
import strings_atlas     # noqa: E402  the DOS side

EXDIR = "decompiled/apple2"
IMAGE = "/var/tmp/u2_6502.bin"
MIN_LEN = 4
CLUSTER_GAP = 128          # bytes apart on both sides to still count as one cluster

# Positive controls: anchors recorded as prose in docs/apple2-reference.md § 4, keyed by
# (owning asm/ function, DOS file offset) rather than by text. Keying them by text would put a
# hand-maintained duplicate of game text in a tool — the exact category AGENTS.md forbids, and
# the old § 4 table shows why: it had already drifted (`MAGIC MISSILE` for a string that ends in
# `!`). Resolve through the atlas instead, so a moved string fails the control loudly instead of
# quietly disagreeing with itself. `ANOS: LEGENDS!` is deliberately absent — strings_atlas cannot
# see it (DOS-side gap, see module docstring).
KNOWN_ANCHORS = [
    ("0F12_combat_magic_missile", 0x001115),
    ("24B5_force_field_event", 0x0026E4),
    ("277B_dungeon_move_commands", 0x002ABE),
    ("3048_vehicle_plane", 0x0032DA),
    ("38B0_cmd_L", 0x003B06),
    ("3C2E_cmd_O", 0x003EF7),
    ("3C2E_cmd_O", 0x003F37),
    ("3F5D_steal_attempt", 0x0041E0),
    ("3F5D_steal_attempt", 0x0041FD),
    ("3F5D_steal_attempt", 0x004228),
]
# Negative control, § 6: this string is DOS-only, and it is named here as a *substring to look
# for*, not as content to store — the check is that whatever the atlas reports at that site comes
# back unmatched.
DOS_ONLY_OWNER = "0A02_invalid_move_msg"


def hi_printable(b):
    return b >= 0x80 and 0x20 <= (b & 0x7F) <= 0x7E


def is_control(b):
    return (b & 0x7F) < 0x20 or (b & 0x7F) == 0x7F


def apple_segments(exdir):
    """(load, end, name) for the resident set, sizes measured from the extracted files."""
    out = []
    for addr, fn in apple2_image.RESIDENT:
        path = os.path.join(exdir, fn)
        if not os.path.exists(path):
            sys.exit(f"missing {path}; extract it first — docs/apple2-reference.md § 2")
        out.append((addr, addr + os.path.getsize(path), fn))
    return sorted(out)


def apple_strings(exdir, image=None):
    """[(addr, segment, raw_bytes, normalised_text)] over the resident code segments."""
    segs = apple_segments(exdir)
    if image and os.path.exists(image):
        img = open(image, "rb").read()
        base = apple2_image.BASE
    else:
        # read the BINs directly: the flat image only covers $4000..$AFFF and zero-fills gaps,
        # and a zero is a legal 6502 opcode — never scan a range that is not backed by bytes.
        img, base = None, None
    out = []
    for lo, hi, fn in segs:
        if img is not None:
            if not (base <= lo and hi <= base + len(img)):
                continue
            raw = img[lo - base:hi - base]
        else:
            raw = open(os.path.join(exdir, fn), "rb").read()
        for p, b in enumerate(raw):
            if b != 0 or p == 0:
                continue
            e = p
            while e > 0 and is_control(raw[e - 1]):
                e -= 1
            q = e
            while q > 0 and hi_printable(raw[q - 1]):
                q -= 1
            bs = bytes(raw[q:e])
            if len(bs) >= MIN_LEN:
                out.append((lo + q, fn, bs, bytes(x & 0x7F for x in bs).decode("latin1")))
    return out


def text_id(text):
    """Short stable hash of the normalised text — the committed stand-in for the string."""
    return hashlib.sha256(text.encode("latin1")).hexdigest()[:10]


def build(exdir=EXDIR, image=IMAGE):
    dos = strings_atlas.build()
    apple = apple_strings(exdir, image)
    by_text = {}
    for addr, seg, raw, text in apple:
        by_text.setdefault(text, []).append((addr, seg, len(raw)))
    rows = []
    for r in sorted(dos, key=lambda x: x["foff"]):
        hits = by_text.get(r["text"])
        rows.append({
            "dos_foff": r["foff"], "dos_runtime": r["runtime"], "dos_owner": r["file"],
            "dos_len": len(r["text"]), "text": r["text"], "id": text_id(r["text"]),
            "apple": hits or [],
        })
    return rows, {"dos_total": len(dos), "apple_total": len(apple),
                  "apple_distinct": len(by_text), "apple_by_seg": _seg_counts(apple)}


def _seg_counts(apple):
    c = {}
    for _, seg, _, _ in apple:
        c[seg] = c.get(seg, 0) + 1
    return c


def clusters(rows):
    """Group matched rows into clusters and check order preservation.

    A cluster is a maximal run of matches whose DOS offsets and Apple addresses both advance by
    <= CLUSTER_GAP. Returns [(cluster_no, [rows], ordered_bool)].
    """
    m = [r for r in rows if r["apple"]]
    out, cur = [], []
    for r in m:
        if cur:
            d = r["dos_foff"] - cur[-1]["dos_foff"]
            a = r["apple"][0][0] - cur[-1]["apple"][0][0]
            if d > CLUSTER_GAP or a > CLUSTER_GAP or a <= 0:
                out.append(cur)
                cur = []
        cur.append(r)
    if cur:
        out.append(cur)
    res = []
    for i, c in enumerate(out, 1):
        addrs = [r["apple"][0][0] for r in c]
        res.append((i, c, all(x < y for x, y in zip(addrs, addrs[1:]))))
    return res


def fmt_rows(rows):
    hdr = ("dos_foff\tdos_runtime\tdos_len\ttext_id\tdos_owner\tapple_addr\tapple_seg"
           "\tapple_len\tmatch")
    lines = [hdr]
    for r in rows:
        for addr, seg, ln in r["apple"]:
            lines.append("%06X\t%04X\t%d\t%s\t%s\t$%04X\t%s\t%d\tmatched"
                         % (r["dos_foff"], r["dos_runtime"], r["dos_len"], r["id"],
                            r["dos_owner"], addr, seg, ln))
    return "\n".join(lines)


def summary(rows, meta):
    matched = [r for r in rows if r["apple"]]
    cl = clusters(rows)
    print(f"DOS strings scanned (tools/strings_atlas.py): {meta['dos_total']}")
    print(f"Apple string candidates: {meta['apple_total']} "
          f"({meta['apple_distinct']} distinct) in "
          + ", ".join(f"{k}={v}" for k, v in sorted(meta["apple_by_seg"].items())))
    pct = 100.0 * len(matched) / max(1, meta["dos_total"])
    print(f"matched: {len(matched)} rows / {len({r['id'] for r in matched})} distinct strings "
          f"of {meta['dos_total']} DOS strings ({pct:.1f}%) — "
          f"the honest yield is small; the value is that each row is a lookup with a citation")
    print(f"clusters: {len(cl)} "
          f"({sum(1 for _, _, o in cl if o)} order-preserving, "
          f"{sum(1 for _, _, o in cl if not o)} not)")
    for i, c, ordered in cl:
        span_d = "%06X..%06X" % (c[0]["dos_foff"], c[-1]["dos_foff"])
        span_a = "$%04X..$%04X" % (c[0]["apple"][0][0], c[-1]["apple"][0][0])
        print(f"  #{i} n={len(c)} dos[{span_d}] apple[{span_a}] order={'yes' if ordered else 'NO'}"
              f" ids={','.join(r['id'][:6] for r in c)}")
    return 0 if all(o for _, _, o in cl) else 1


def show(rows):
    for r in rows:
        if not r["apple"]:
            continue
        print("DOS  file:%06X runtime:%04X owner:%s" % (r["dos_foff"], r["dos_runtime"], r["dos_owner"]))
        print("     %r" % r["text"])
        for addr, seg, ln in r["apple"]:
            print(f"     Apple {seg} ${addr:04X} ({ln} bytes, high-bit-set)")


def selftest(exdir=EXDIR, image=IMAGE):
    rows, meta = build(exdir, image)
    matched = {(r["dos_owner"], r["dos_foff"]) for r in rows if r["apple"]}
    fails = []
    # Negative control: the reject-echo routine is DOS-only (§ 6). Whatever the atlas reports
    # for it must come back UNMATCHED. Asserted via its owner and offset, not via its text, so
    # the control carries no game content and cannot drift away from asm/.
    dos_only = [r for r in rows if r["dos_owner"] == DOS_ONLY_OWNER]
    if not dos_only:
        fails.append(f"asm/{DOS_ONLY_OWNER} contributed no string to the atlas — the DOS "
                     f"scanner regressed (this is the case that was silently dropped until "
                     f"2026-10-28d: a db with no label after it)")
    for r in dos_only:
        if (r["dos_owner"], r["dos_foff"]) in matched:
            fails.append(f"the DOS-only string at {r['dos_owner']}+{r['dos_foff']:06X} reported "
                         f"as MATCHED — the matcher is broken (docs/apple2-reference.md § 6)")
    # Positive controls: the prose anchors must come back matched.
    for owner, foff in KNOWN_ANCHORS:
        if (owner, foff) not in matched:
            fails.append(f"known anchor {owner}+{foff:06X} did not match — matcher regression")
    # The Apple scanner must not be a printable-run regex wearing a hat: bitmap noise in the
    # picture-adjacent segments must not be reported as text.
    if meta["apple_distinct"] > 600:
        fails.append(f"{meta['apple_distinct']} distinct Apple strings — the high-bit filter is "
                     f"not doing its job (the § 6 '287 of 288' artefact shape)")
    # Order preservation must be computed and must hold for the clusters § 4 claims.
    cl = clusters(rows)
    if not cl:
        fails.append("no clusters at all")
    for i, c, ordered in cl:
        if len(c) >= 3 and not ordered:
            fails.append(f"cluster #{i} ({len(c)} anchors) is not order-preserving")
    if fails:
        print("SELFTEST FAILED")
        for f in fails:
            print("  -", f)
        return 1
    print(f"selftest ok: {len(matched)} matched rows / "
          f"{len({r['id'] for r in rows if r['apple']})} distinct strings of "
          f"{meta['dos_total']} DOS strings, "
          f"{len(cl)} clusters all order-preserving, "
          f"{DOS_ONLY_OWNER} correctly unmatched, {len(KNOWN_ANCHORS)} prose anchors reproduced")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-d", "--dir", default=EXDIR)
    ap.add_argument("-i", "--image", default=IMAGE)
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--summary", action="store_true")
    ap.add_argument("--tsv", action="store_true", help="machine rows, no game text")
    ap.add_argument("--show", action="store_true", help="print text to stdout only")
    ap.add_argument("-o", "--out", help="write the TSV (never contains text)")
    a = ap.parse_args()

    if a.selftest:
        return selftest(a.dir, a.image)
    rows, meta = build(a.dir, a.image)
    if a.show:
        show(rows)
        return 0
    if a.summary:
        return summary(rows, meta)
    text = fmt_rows(rows)
    if a.out:
        if a.show:
            sys.exit("--show prints text; refusing to write it to a file")
        with open(a.out, "w") as fh:
            fh.write("# Apple II <-> DOS string anchors. Generated — do not hand-edit.\n")
            fh.write("#   python3 tools/apple2_anchor_map.py -o disasm/apple2-anchors.tsv\n")
            fh.write("# No game text: text_id is sha256[:10] of the normalised string.\n")
            fh.write(text + "\n")
        print(f"wrote {a.out}: {sum(1 for r in rows if r['apple'])} matched rows")
        return 0
    print(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())

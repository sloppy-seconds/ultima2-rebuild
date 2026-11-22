#!/usr/bin/env python3
"""
save_gate.py — CP5 gate for the three Q guards, and the enhanced mode that drops them.

The chain is cmd_Q at file 0x3FC7, read off the bytes (objdump of original/ultimaii.exe,
2026-10-29), in this order and no other:

    3FDD  mov al,[DS:4A] ; or al,al ; je 3FFA   -> inside a site: "ONLY OUTDOORS!"
    3FFA  mov al,[DS:6D] ; or al,al ; je 4017   -> DS:6D set:    "ONLY ON EARTH!"
    4017  mov al,[DS:13] ; cmp $0x78 ; jae 4033 -> not on foot:  "ONLY ON FOOT!"
    4033  "ONE MOMENT PLEASE!" ; call asm/22FD  -> the save runs

Two of these the engine had stubbed away on assumptions that have since died: the comment said
DS:6D is a planet counter that cannot be non-zero until CP7, and DS:13 is a vehicle stamp that
cannot be set until CP6. Vehicles landed the same week this gate was written, and DS:6D is a
live byte that refuses the save for any character not on Earth — which is what it has always
meant (world 0 = Earth, 1..9 = the planets, 0x0A = deep space; see G44 and
`asm/3DFA_offer_only_outdoors_earth.asm`). The CP7 guess was right; the boots name on it was
ours, written by `tools/u2_edit.py`. See captures/spice86/save-wilderness and
docs/save-format.md § 10.

Enhanced mode is the owner's call of 2026-10-29: save anywhere. It is a policy, not an edit to
the loop — u2_overworld_save() consults u2_save_policy and nothing else, which is the reason
the struct exists.

Every case asserts the refusal text AND that no save happened, or that the save did. A gate
that only checks the message would pass on an engine that printed the words and saved anyway.
"""
import os
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import u2_seed                                  # noqa: E402
import u2_gate as G

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
SEED = os.path.join(REPO, "engine", "tests", "cp5_fight_seed.py")

GRASS, TILE_HORSE = 0x08, 0x44
# The fields are named through u2_seed, which reads the addresses from state.h at runtime.
SITE_X, SITE_Y = 2, 0

fails = []


def check(cond, msg):
    print(("  ok    " if cond else "  FAIL  ") + msg)
    if not cond:
        fails.append(msg)


def build(work, site=False, tiles=None, ds6d=0):
    """A wilderness seed, or a village seed (ones digit 1) for the indoors case."""
    if os.path.isdir(work):
        shutil.rmtree(work)
    subprocess.run([sys.executable, SEED, work], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    mp = os.path.join(work, "content", "mapx%s" % ("01" if site else "00"))
    if site:
        d = bytearray([GRASS]) * 4096
        for (x, y), v in (tiles or {}).items():
            d[y * 64 + x] = v
        open(mp, "wb").write(bytes(d))
        mon = bytearray(256)
        mon[0], mon[32] = 30, 30
        mon[64], mon[96], mon[160] = 5, 0x68, 1
        open(os.path.join(work, "content", "monx01"), "wb").write(bytes(mon))
    elif tiles:
        d = bytearray(open(mp, "rb").read())
        for (x, y), v in tiles.items():
            d[y * 64 + x] = v
        open(mp, "wb").write(bytes(d))

    pp = G.record(work)
    p = u2_seed.PlayerRecord.load(pp)
    if site:
        p.set_raw("sector_second", 1)
        p.set_raw("entry_x", SITE_X)     # the pair the save rewrites with the live position
        p.set_raw("entry_y", SITE_Y)
    p.set_raw("world", ds6d)             # non-zero is what the Earth gate reads
    p.write(pp)
    return work


def run(work, keys, extra=(), name="case"):
    kp = os.path.join(work, "k-%s" % name)
    with open(kp, "w") as f:
        for k in keys:
            f.write(G.KEYS.get(k, k) + "\n")
    return subprocess.run(
        [G.ENGINE, "--headless", "--play-map", "--cleanroom",
         "--data-dir", os.path.join(work, "content"),
         "--save-dir", os.path.join(work, "save"),
         "--script", kp, "--dump-state", *extra],
        check=False, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True).stdout


def saved_record(work):
    return bytearray(open(G.record(work), "rb").read())


def case(label, work, keys, expect, extra=None, site=False, tiles=None, ds6d=0):
    """Run one guard and assert both halves of it: the words, and whether the record moved.

    Every case walks one step first. Without that, a save that happened anyway would write a
    byte-identical record — the entry pair only moves if the position did — and the "nothing
    was saved" assertion would pass on the very bug it is meant to catch.

    The guard cases must name the mode. The three Q guards are dropped by design in enhanced
    mode (that is what cases 5 and 6 assert), and the default fidelity is enhanced, so a
    case that passes no mode flag runs enhanced rules while claiming to test the original
    ones. That is how this gate went red with six failures and stayed red: the refusals were
    never printed because the engine was correctly not refusing, and the record moved
    because enhanced mode saves anywhere. `--classic` is the opt-out and it is now explicit
    rather than inherited from a default that has since moved.
    """
    if extra is None:
        extra = ["--classic"]
    work = build(work, site=site, tiles=tiles, ds6d=ds6d)
    keys = ["DOWN"] + list(keys) if keys[0] != "DOWN" else list(keys)
    before = bytes(saved_record(work))
    text = G.flatten(run(work, keys, extra=extra))
    try:
        after = bytes(saved_record(work))
    except OSError:
        after = before
    saved = after != before
    if expect == "moment":
        check("ONE MOMENT PLEASE!" in text, "%s: printed ONE MOMENT PLEASE!" % label)
        check(saved, "%s: the record was actually written" % label)
    else:
        check(expect in text, "%s: printed %s" % (label, expect))
        check(not saved, "%s: nothing was saved (the record is byte-identical)" % label)
    shutil.rmtree(work, ignore_errors=True)


def main():
    print("save_gate: the three Q guards, and enhanced mode saving anywhere")
    tmp = lambda p: tempfile.mkdtemp(prefix=p)

    # 1. Indoors: the sector's ones digit is a site kind, so Q must refuse.
    case("indoors", tmp("u2-sg-1-"), ["Q"], "ONLY OUTDOORS!", site=True)

    # 2. DS:6D set (what a character holding boots looks like), outdoors.
    case("off-earth", tmp("u2-sg-2-"), ["Q"], "ONLY ON EARTH!", ds6d=1)

    # 3. Aboard a vehicle: BOARD sets DS:13 to the horse stamp, which is below 0x78.
    case("mounted", tmp("u2-sg-3-"), ["DOWN", "B", "Q"], "ONLY ON FOOT!",
         tiles={(SITE_X, SITE_Y + 1): TILE_HORSE})

    # 4. The clean path: outdoors, on foot, DS:6D clear. The save must really happen, and
    #    the entry pair must follow the live position (asm/2305's first act).
    work = tmp("u2-sg-4-")
    build(work)
    before = bytes(saved_record(work))
    text = G.flatten(run(work, ["DOWN", "DOWN", "Q"], name="four"))
    after = saved_record(work)
    st = G.state_from_text(run(work, ["DOWN", "DOWN", "Q"], name="fourb"))
    check("ONE MOMENT PLEASE!" in text, "clean: printed ONE MOMENT PLEASE!")
    check(bytes(after) != before, "clean: the record was actually written")
    if st is not None:
        x, y = G.pos(st)
        rec = u2_seed.PlayerRecord(bytearray(after))
        check(rec.get("entry_x") == x and rec.get("entry_y") == y,
              "clean: the saved entry pair is the live position (%d,%d)" % (x, y))
    shutil.rmtree(work, ignore_errors=True)

    # 5. Enhanced: every guard is dropped, including indoors.
    case("enhanced indoors", tmp("u2-sg-5-"), ["Q"], "moment",
         extra=["--enhanced"], site=True)
    case("enhanced mounted", tmp("u2-sg-6-"), ["DOWN", "B", "Q"], "moment",
         extra=["--enhanced"], tiles={(SITE_X, SITE_Y + 1): TILE_HORSE})

    # 6. The upgrade path: a tree an older engine left behind, where the record was a
    #    FILE at saves/PLAYER -- exactly where the slot directory now has to go. The
    #    property is not that the file moves, which save_check covers, but WHEN: if
    #    the adoption happens after the startup load, the game holds a blank state and
    #    the first save overwrites the record it just moved. That is silent loss of a
    #    player's character, so it is asserted end to end against the real binary.
    #    Demonstrated red, not assumed: building the engine with the startup adoption
    #    removed makes the second check fail with the name coming back as six zero
    #    bytes, which is the character gone and a green-looking save in its place.
    work = tmp("u2-sg-7-")
    build(work)
    rec = G.record(work)                                  # save/PLAYER/PLAYER
    slot = os.path.join(work, "save", "PLAYER")
    with open(rec, "rb") as f:
        seeded = f.read()
    os.remove(rec)
    os.rmdir(slot)
    with open(slot, "wb") as f:                          # back to the pre-slot layout
        f.write(seeded)
    text = G.flatten(run(work, ["DOWN", "Q"], name="legacy"))
    adopted = os.path.isdir(slot) and os.path.exists(rec)
    check(adopted, "legacy: the flat record was adopted into a slot directory")
    if adopted:
        with open(rec, "rb") as f:
            after = f.read()
        check(after[:6] == seeded[:6],
              "legacy: adoption ran before the load, so the character survived (%r)"
              % after[:6])
        check(after != seeded,
              "  and the Q really wrote: the entry pair moved")
    else:
        check(False, "legacy: no slot directory was created, so the save had nowhere to go")
    shutil.rmtree(work, ignore_errors=True)

    print("SAVE GATE: %s" % ("FAIL (%d)" % len(fails) if fails else "PASS"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())

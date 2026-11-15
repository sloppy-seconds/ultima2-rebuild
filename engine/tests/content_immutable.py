#!/usr/bin/env python3
"""content_immutable.py — the gate docs/save-scoped-worlds.md § 5 calls the one that matters most.

The property: **once the content tree exists, playing the game never modifies a file
inside it.** Every sector write belongs to a save and lands in `saves/<slot>/`. This is
the exact thing that went wrong on the owner's install — a played-in `mapx20` with two
creature stamps baked into the release map, and `map_base` loading its pristine terrain
from that same file, so the damage was sticky.

Why a gate and not an eyeball: the write-back is faithful to `asm/2305`, so what changed is
*where* it lands, and "where" is invisible from how the game behaves on screen. A hash of
the content tree before and after a real session is the only thing that says it.

Three ways this could pass vacuously, each handled rather than assumed:

  * the session writes nothing, so nothing could have drifted. The gate therefore asserts
    the positive half too: the slot must hold an overlay, and the overlay must be the file
    that received the change. No overlay means the comparison above proved nothing. The
    scenario is BOARD then Q, because boarding is a terrain edit (u2_world_terrain_put)
    and the save is what stores a dirty sector — the same route tests/vehicle_board.py
    uses, rather than a second invention of it.
  * the harness points at the wrong directory and compares an empty tree. It asserts the
    tree it hashed is non-empty before it starts.
  * the comparison cannot fail. `--control` dirties one byte of a content file between the
    two hashes and requires the diff to name it. A control that cannot go red is the
    failure mode AGENTS.md § 3 exists to stop, so it runs on every invocation, not only on
    demand.

And the gate itself was demonstrated red where it enforces, not only locally: building
the engine against a `u2_world_store` that writes back into the layer the sector was
read from — the rule this change replaced — makes it report
`playing a session changed nothing under the content directory: mapx00: ... -> ...`
and `the run did write a sector, and it landed in the save slot`. A passing hash
comparison with no overlay behind it is the vacuous shape, and it is a failure here.

Everything it touches is engine-authored filler from cp5_fight_seed.py, in a throwaway
directory, so this runs in CI with no game media present.

Usage: content_immutable.py [WORKDIR] [--control-only]
"""
import hashlib
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import u2_gate as G                                    # noqa: E402

ENGINE = G.ENGINE
SEED = G.SEED

fails = 0
checks = 0


def check(cond, msg):
    global fails, checks
    checks += 1
    if cond:
        print("  ok    %s" % msg)
    else:
        print("  FAIL  %s" % msg)
        fails += 1


def hash_tree(root):
    """Every file under `root`, keyed by relative path.

    Recursive on purpose: the installed layout nests content two levels deep
    (`data/original`, `data/updated`), and a flat list over the root would hash
    nothing at all and report a flawless result.
    """
    out = {}
    for dirpath, _dirs, names in os.walk(root):
        for n in names:
            full = os.path.join(dirpath, n)
            h = hashlib.sha256()
            with open(full, "rb") as f:
                for chunk in iter(lambda: f.read(65536), b""):
                    h.update(chunk)
            out[os.path.relpath(full, root)] = h.hexdigest()
    return out


def diff_trees(before, after):
    """Every difference in either direction: changed, added or removed.

    A comparison that looked only at files present in both would miss the one that
    matters most here, which is a sector file appearing where it should not have.
    """
    out = []
    for k in sorted(set(before) | set(after)):
        if k not in after:
            out.append("%s: removed" % k)
        elif k not in before:
            out.append("%s: added (%s)" % (k, after[k][:12]))
        elif before[k] != after[k]:
            out.append("%s: %s -> %s" % (k, before[k][:12], after[k][:12]))
    return out


def seed(work):
    """A fresh seed with a horse on the cell the player enters on.

    Fresh per run, because the engine rewrites the save in place and a reused dir
    has produced wrong results before (handoff trap 4).
    """
    return G.seed(work, tiles={G.ENTRY: G.TILE_HORSE})


def play(work):
    """Board, then save.

    BOARD is a terrain edit, so it dirties the sector; Q is what stores it
    (engine/src/game/overworld.c, save_body). The save needs --enhanced because
    asm/3DFA's third guard refuses Q while aboard a vehicle — the same reason and
    the same flag tests/vehicle_board.py gives for the same route.
    """
    return G.run_stdout(work, ["B", "Q"], name="save", extra=["--enhanced"])


def slot_files(work):
    slot = os.path.join(work, "save", "PLAYER")
    return sorted(os.listdir(slot)) if os.path.isdir(slot) else []


def run_gate(work):
    play_dir = seed(os.path.join(work, "gate"))
    content = os.path.join(play_dir, "content")

    before = hash_tree(content)
    check(len(before) > 0, "the content tree is non-empty, so the comparison can see something")

    play(play_dir)

    after = hash_tree(content)
    diffs = diff_trees(before, after)
    check(not diffs, "playing a session changed nothing under the content directory%s"
          % ("" if not diffs else ": " + "; ".join(diffs)))

    overlays = slot_files(play_dir)
    if overlays:
        check("mapx00" in overlays,
              "the run did write a sector, and it landed in the save slot "
              "(saves/PLAYER/%s)" % ", ".join(overlays))
    else:
        check(False, "the run wrote no overlay at all, so the comparison above proved nothing")

    # The change itself, on the right side of the wall: BOARD replaces the tile under the
    # player, so the overlay must show the cell consumed and the content copy must still
    # show the horse. Two halves of one claim, and the second is the property.
    x, y = G.ENTRY
    ov = os.path.join(play_dir, "save", "PLAYER", "mapx00")
    if os.path.exists(ov):
        overlay = open(ov, "rb").read()
        kept = open(os.path.join(content, "mapx00"), "rb").read()
        check(overlay[y * 64 + x] != G.TILE_HORSE,
              "the overlay carries the tile BOARD left behind")
        check(kept[y * 64 + x] == G.TILE_HORSE,
              "  and the content copy still holds the horse it shipped with")
    check(all(not o.endswith(".u2tmp") for o in overlays),
          "no temp file was left behind in the slot")


def run_control(work):
    """The same comparison against a tree that really was dirtied. It must go red."""
    ctl = seed(os.path.join(work, "control"))
    content = os.path.join(ctl, "content")
    before = hash_tree(content)
    victim = os.path.join(content, "mapx00")
    raw = bytearray(open(victim, "rb").read())
    raw[0] ^= 0xFF
    open(victim, "wb").write(bytes(raw))
    caught = diff_trees(before, hash_tree(content))
    check(any("mapx00" in d for d in caught),
          "the control: one dirtied content byte is caught by the comparison (%s)"
          % ("; ".join(caught) if caught else "nothing reported"))


def main():
    # Answer --help before anything else, including before the engine-binary check. The
    # doc_command_gate (tools/doc_command_gate.py) cites this file from AGENTS.md and proves
    # every cited command answers --help; without this early exit it had to be run against a
    # built tree, so `make clean` turned a documentation gate red for a build reason.
    if "--help" in sys.argv or "-h" in sys.argv:
        print(__doc__)
        print("usage: content_immutable.py [WORKDIR] [--control-only]")
        print("  WORKDIR         scratch dir for the throwaway content tree"
              " (default /var/tmp/u2-content-immutable)")
        print("  --control-only  run only the red control, not the gate")
        return 0
    work = sys.argv[1] if len(sys.argv) > 1 and not sys.argv[1].startswith("-") \
        else "/var/tmp/u2-content-immutable"
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work, exist_ok=True)
    if not os.path.exists(ENGINE):
        print("  FAIL  no engine binary at %s — run make -C engine first" % ENGINE)
        return 1
    if "--control-only" not in sys.argv:
        run_gate(work)
    run_control(work)
    print("content immutable: %s (%d checks, %d failures)"
          % ("FAIL" if fails else "PASS", checks, fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())

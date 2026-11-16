#!/usr/bin/env python3
"""doc_command_gate.py — prove that the commands the docs name still exist and still answer.

Why this exists. `docs/tooling-procedures.md` and `docs/spice86-capture.md` are the pages a
session is told to consult BEFORE writing a script, and they are full of commands. A page that
names a tool which has been renamed, deleted, or had its interface changed is worse than no page:
it reads as authority, and the session follows it into a failure and then re-derives the real
answer by hand — the exact cost these pages exist to prevent. Two such drifts were found by
accident on 2026-11-15 while writing the build/play/seed sections: a note asserting the SDL
artifact could not be built on this host (it can, in a container, in about a minute) and a
"which tool for which job" table missing one of the two entries it was supposed to cover.

The rule this enforces is narrow and mechanical:

  * every repo-relative `tools/`, `engine/tools/`, `engine/tests/` or `deploy/` script a checked
    doc names MUST exist;
  * every `.py` among them MUST exit 0 on `--help`;
  * every `.sh` among them MUST parse under `bash -n`.

That is all it claims. It does not check that a documented command does what the prose says; only
that the thing it points at is present and self-describing. A tool that answers `--help` with
someone else's usage text passes this gate and is still broken — see `tools/u2_endgame_save.py`,
which used to forward `--help` to `cp` and print coreutils' help.

Usage:
    python3 tools/doc_command_gate.py                 # check the default docs
    python3 tools/doc_command_gate.py DOC [DOC ...]   # check specific docs
    python3 tools/doc_command_gate.py --selftest      # prove the gate can go red
    python3 tools/doc_command_gate.py --list          # what it found, one path per line

Exit 0 = every referenced command is present and answers. Exit 1 = at least one is not.
"""

import os
import re
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# The pages that carry commands a session is expected to run. Kept short on purpose: every entry
# is a promise this gate now has to keep, so adding a doc here is a decision, not a glob.
DEFAULT_DOCS = [
    "docs/tooling-procedures.md",
    "docs/spice86-capture.md",
    # A plan that names tools is a document that will be followed. Plans are where a command goes
    # stale quietly, because nobody re-reads them the way they re-read a runbook.
    "docs/plan-bot-playthrough.md",
    "AGENTS.md",
    # The in-repo skills carry runnable commands and are routed to automatically, so a stale one
    # is worse than a stale doc: the model loads it without being told to. They are gated for the
    # same reason the docs are.
    ".agents/skills/u2-build/SKILL.md",
    ".agents/skills/u2-play/SKILL.md",
    ".agents/skills/u2-character/SKILL.md",
]

# Repo-relative script directories the docs are allowed to point at. Data paths (original/,
# captures/, tmp/) are deliberately excluded: this gate is about executables.
PREFIXES = ("tools/", "engine/tools/", "engine/tests/", "deploy/")

# `tools/foo.py`, `deploy/novnc/up.sh`, `engine/tests/cp6_commerce_seed.py`. Stops at
# whitespace, backtick, quote or bracket so inline-code punctuation is not swallowed.
REF_RE = re.compile(r"(?:%s)[A-Za-z0-9_./-]+\.(?:py|sh)" % "|".join(p.rstrip("/") + r"/" for p in PREFIXES))

HELP_TIMEOUT = 30


def referenced(doc_text):
    """Every candidate script path named in a doc, in first-seen order, de-duplicated."""
    seen, out = set(), []
    for m in REF_RE.finditer(doc_text):
        p = m.group(0).rstrip(".,;:")
        if p not in seen:
            seen.add(p)
            out.append(p)
    return out


def check_one(path, repo=None):
    """Return None if the path is fine, else the reason it is not."""
    full = os.path.join(repo or REPO, path)
    if not os.path.exists(full):
        return "named by a doc but does not exist"
    if path.endswith(".py"):
        try:
            r = subprocess.run([sys.executable, full, "--help"],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                               timeout=HELP_TIMEOUT, cwd=repo or REPO)
        except subprocess.TimeoutExpired:
            return f"`--help` did not return within {HELP_TIMEOUT}s"
        if r.returncode != 0:
            return f"`--help` exited {r.returncode}"
        return None
    if path.endswith(".sh"):
        r = subprocess.run(["bash", "-n", full], stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL, timeout=30, cwd=repo or REPO)
        if r.returncode != 0:
            return f"`bash -n` exited {r.returncode}"
        return None
    return None


def check_docs(docs, repo=None):
    """docs is a list of (label, text). Returns a list of (label, path, reason)."""
    fails, seen = [], set()
    for label, text in docs:
        for path in referenced(text):
            if path in seen:
                continue
            seen.add(path)
            why = check_one(path, repo)
            if why:
                fails.append((label, path, why))
    return fails


def read_docs(paths, repo=None):
    out = []
    for p in paths:
        full = p if os.path.isabs(p) else os.path.join(repo or REPO, p)
        if not os.path.exists(full):
            out.append((p, ""))
            continue
        with open(full, encoding="utf-8", errors="replace") as f:
            out.append((p, f.read()))
    return out


def selftest():
    """Four controls, run against a throwaway repo root so the fixtures land under the same
    prefixes the real scan uses. The first version of this selftest wrote its fixtures to a temp
    dir OUTSIDE the repo, the reference regex ignored them, and two of the four controls passed
    while checking nothing at all -- which is precisely the failure mode this file exists to stop.
    Three must go red, one must stay green."""
    ok = True
    with tempfile.TemporaryDirectory() as td:
        os.makedirs(os.path.join(td, "tools"))
        os.makedirs(os.path.join(td, "docs"))

        def fixture(name, body):
            with open(os.path.join(td, name), "w") as f:
                f.write(body)

        # a .py that exists but whose --help fails
        fixture("tools/bad.py", "import sys\nsys.exit(3)\n")
        # a .sh that does not parse
        fixture("tools/bad.sh", "#!/usr/bin/env bash\nif [ 1 -eq 1 ]; then\n")
        # a .py that answers, the green case
        fixture("tools/good.py",
                "import sys\nif '--help' in sys.argv:\n    print('usage')\n    raise SystemExit(0)\n")
        fixture("docs/ghost.md", "run `tools/no_such_tool_here.py` first\n")
        fixture("docs/badhelp.md", "run `tools/bad.py` first\n")
        fixture("docs/badsh.md", "run `tools/bad.sh` first\n")
        fixture("docs/good.md", "run `tools/good.py` first\n")

        cases = [
            ("a doc naming a missing tool must go red", ["docs/ghost.md"], 1),
            ("a tool whose --help fails must go red", ["docs/badhelp.md"], 1),
            ("a shell script that does not parse must go red", ["docs/badsh.md"], 1),
            ("a doc naming a working tool must stay green", ["docs/good.md"], 0),
        ]
        for name, docs, want in cases:
            fails = check_docs(read_docs(docs, td), td)
            got = 1 if fails else 0
            if got != want:
                print(f"SELFTEST FAIL: {name} (got exit {got}, wanted {want})")
                ok = False
            else:
                print(f"selftest ok: {name}")
    print("SELFTEST:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


def main(argv):
    # `-h` must answer before anything else, or `--help` falls through to a full check of the
    # default docs -- which cite this file, so the check runs `--help` on itself and recurses until
    # the subprocess timeout. The gate caught its own tool with exactly the check it exists to run,
    # via a `| tail` that swallowed the exit status and let the commit land anyway.
    if "-h" in argv or "--help" in argv:
        print((__doc__ or "").strip())
        return 0
    if "--selftest" in argv:
        return selftest()
    docs = [a for a in argv if not a.startswith("-")]
    if not docs:
        docs = DEFAULT_DOCS
    paths = []
    for label, text in read_docs(docs):
        paths.extend(referenced(text))
    seen, uniq = set(), []
    for p in paths:
        if p not in seen:
            seen.add(p)
            uniq.append(p)

    if "--list" in argv:
        for p in sorted(uniq):
            print(p)
        return 0

    fails = check_docs(read_docs(docs))
    print(f"doc command gate: {len(uniq)} command(s) referenced by {len(docs)} doc(s)")
    for label, path, why in fails:
        print(f"  FAIL  {path}  ({why})   cited in {label}")
    if fails:
        print("DOC COMMAND GATE: FAIL")
        return 1
    print("DOC COMMAND GATE: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))

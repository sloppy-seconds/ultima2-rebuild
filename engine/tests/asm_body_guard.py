#!/usr/bin/env python3
"""asm_body_guard.py — the asm/ invariant CP0 actually needs.

Why this exists instead of the older per-file md5 check
------------------------------------------------------
The previous guard compared, file by file, the md5 of each changed file's
non-comment lines against HEAD, and treated ANY deletion as a failure. That was
right while the only permitted asm/ edit was a rename. It became wrong the moment
function *boundaries* were allowed to move: merging seven fragments into one
routine deletes six files and grows a seventh, and every one of those six
deletions is the guard reporting success as vandalism.

What is genuinely invariant is not which file a line lives in. It is that the
**assemblable content of the whole tree** is unchanged: no instruction line added,
removed, reordered or edited. Renaming, merging and splitting are all legitimate
maintenance acts on that content; editing it is not.

The check
---------
Build a canonical stream: for every `asm/*.asm`, sorted by the 4-hex start address
in its filename, concatenate the file's non-comment lines. Because the fragments
tile their address ranges contiguously and in order, sorting by start address makes
the stream independent of where the file boundaries sit. Merging and splitting
preserve it exactly. Any body edit, insertion, deletion or reorder breaks it.

Compared against the same stream built from `HEAD`.

Comment lines (leading '#') are excluded, so headers, evidence notes and
provenance may be rewritten freely -- which is what AGENTS.md § 3 requires in the
same commit that renames a file.

THE ONE EXCEPTION: a marked body correction
-------------------------------------------
Sometimes the body is provably WRONG, not merely mis-filed. asm/353A forced-kept one byte
of a four-byte instruction and asm/353D then decoded the leftover bytes as `push %ds` and
`dec %cx`: mnemonics that are not the code. `tools/forced_keep_audit.py` finds at least 63
such sites. Freezing those bodies forever protects a known falsehood, which is not what a
proof is for.

So a body may change IF AND ONLY IF the file carries a marker line:

    # body corrected: YYYY-MM-DD -- evidence: <what proved the old decode wrong>

and the marker must name its evidence. The marker grants permission to edit, it does not
grant permission to be wrong: **`tools/assemble_clean.sh` remains the authority.** A
corrected body that changes any byte fails there, so a marker cannot smuggle in a semantic
change -- if it still assembles to the same bytes it is by construction a valid decode of
them. The two gates are deliberately different: this one polices authorship, that one
polices truth.

Merging and splitting still need no marker, because they preserve the canonical stream.
Removing a file whose body lines vanish is still a failure even with a marker: the content
has to go somewhere.

Usage:
    python3 engine/tests/asm_body_guard.py [--base REF] [--list-marked]

  --base REF     compare against REF instead of HEAD. Locally HEAD is right, because the
                 guard's job there is to catch an edit before it is committed. In CI it is
                 WRONG: a clean checkout makes HEAD and the worktree the same thing, so the
                 comparison is vacuous and can only PASS. CI must pass the merge base. The
                 guard detects this and exits 2 rather than reporting a pass it did not earn.
  --list-marked  list every file carrying a correction marker, with its evidence.

Exit 0 = body content conserved, or changed only in marked files. Exit 1 = changed in an
unmarked file, or content removed. Exit 2 = misconfigured (CI without a real base).
"""
import os
import re
import subprocess
import sys

START_RE = re.compile(r'^([0-9A-Fa-f]{4})_')
MARKER_RE = re.compile(r'^#\s*body corrected:\s*(.+)$', re.I)
EVIDENCE_RE = re.compile(r'evidence\s*:', re.I)


def marker_in(text):
    """Return the marker's text if the file carries a well-formed one, else None."""
    for ln in text.splitlines():
        m = MARKER_RE.match(ln)
        if m:
            body = m.group(1).strip()
            if not EVIDENCE_RE.search(body):
                return 'MALFORMED (must include "evidence:")'
            return body
    return None


def body_lines(text):
    """Non-comment, non-blank-only lines, exactly as the assembler sees them."""
    return [ln for ln in text.splitlines() if not ln.lstrip().startswith('#')]


def stream_from_git(ref):
    """{filename: body lines} for asm/ at a git ref."""
    listing = subprocess.run(
        ['git', 'ls-tree', '-r', '--name-only', ref, '--', 'asm/'],
        capture_output=True, text=True, check=True).stdout.split()
    out = {}
    for path in listing:
        if not path.endswith('.asm'):
            continue
        txt = subprocess.run(['git', 'show', f'{ref}:{path}'],
                             capture_output=True, text=True, check=True).stdout
        out[path] = body_lines(txt)
    return out


def stream_from_worktree():
    import glob
    import os
    out = {}
    for path in sorted(glob.glob('asm/*.asm')):
        out[path] = body_lines(open(path, errors='replace').read())
    return out


def canonical(files):
    """One flat list of body lines, ordered by each file's declared start address."""
    def key(item):
        path, _ = item
        base = path.split('/')[-1]
        m = START_RE.match(base)
        # files without a 4-hex prefix sort last, by name, deterministically
        return (0, int(m.group(1), 16), base) if m else (1, 0, base)
    flat = []
    for path, lines in sorted(files.items(), key=key):
        flat.extend(lines)
    return flat


def raw_from_git(ref):
    """{filename: full text} for asm/ at a git ref, so markers can be read."""
    listing = subprocess.run(
        ['git', 'ls-tree', '-r', '--name-only', ref, '--', 'asm/'],
        capture_output=True, text=True, check=True).stdout.split()
    out = {}
    for path in listing:
        if path.endswith('.asm'):
            out[path] = subprocess.run(['git', 'show', f'{ref}:{path}'],
                                       capture_output=True, text=True,
                                       check=True).stdout
    return out


def raw_from_worktree():
    import glob
    out = {}
    for path in sorted(glob.glob('asm/*.asm')):
        out[path] = open(path, errors='replace').read()
    return out


def parse_args(argv):
    """--base REF (default HEAD) and --list-marked. Kept hand-rolled: this runs inside
    `make test` before anything else and must not gain dependencies."""
    base, list_marked = 'HEAD', False
    i = 0
    while i < len(argv):
        if argv[i] == '--base':
            i += 1
            if i >= len(argv):
                sys.exit('asm_body_guard: --base needs a ref')
            base = argv[i]
        elif argv[i] == '--list-marked':
            list_marked = True
        elif argv[i] in ('-h', '--help'):
            print(__doc__)
            sys.exit(0)
        else:
            sys.exit(f'asm_body_guard: unknown argument {argv[i]!r}')
        i += 1
    return base, list_marked


def vacuous_in_ci(base):
    """Refuse to report a meaningless PASS.

    The guard compares a working tree against a ref. On a clean CI checkout the two are
    the same commit, so the comparison is between a thing and itself and can only ever
    print "conserved" -- which is a gate that cannot go red, the exact failure AGENTS.md
    § 3 singles out. It did exactly that for as long as anyone can remember: a committed
    unmarked body edit sails through, and CI cheerfully reports the guard passing.

    So in CI, insist on a real base. Locally, HEAD is the right default because the value
    is catching an edit BEFORE it is committed.
    """
    ci = os.environ.get('CI') or os.environ.get('GITHUB_ACTIONS')
    if not ci:
        return None
    if base == 'HEAD':
        return ('running in CI with --base HEAD: the checkout is clean, so this compares '
                'HEAD to itself and can only PASS. Pass the merge base, e.g. '
                '--base "$MERGE_BASE". Refusing to report a vacuous pass.')
    try:
        head = subprocess.run(['git', 'rev-parse', 'HEAD'],
                              capture_output=True, text=True, check=True).stdout.strip()
        base_sha = subprocess.run(['git', 'rev-parse', base],
                                  capture_output=True, text=True, check=True).stdout.strip()
    except subprocess.CalledProcessError as exc:
        return f'cannot resolve base ref {base!r} in CI: {exc}'
    if head == base_sha:
        return (f'--base {base} resolves to HEAD ({head[:12]}): the comparison would be '
                'between a commit and itself. The base ref was probably not fetched.')
    return None


def list_marked_files():
    """Every asm/ file carrying a correction marker, with its evidence text."""
    import glob
    rows = []
    for path in sorted(glob.glob('asm/*.asm')):
        mk = marker_in(open(path, errors='replace').read())
        if mk:
            rows.append((os.path.basename(path), mk))
    return rows


def to_repo_root():
    """cd to the repo root.

    The guard globs `asm/` and runs `git ls-tree -- asm/`, both of which are relative to
    the current directory. It is invoked from `engine/` by tests/run_cp0.sh, where there
    is no `asm/` at all -- so it found zero files, printed "conserved (0 lines, 0 -> 0
    files)", and CP0 reported that as a PASS. That was a second vacuous gate, independent
    of the HEAD-vs-HEAD one, and it had been green the whole time.
    """
    try:
        root = subprocess.run(['git', 'rev-parse', '--show-toplevel'],
                              capture_output=True, text=True, check=True).stdout.strip()
    except subprocess.CalledProcessError:
        return False
    if root:
        os.chdir(root)
    return True


def main():
    base, list_marked = parse_args(sys.argv[1:])
    if not to_repo_root():
        print('asm_body_guard: MISCONFIGURED -- not inside a git repo, so asm/ cannot be found')
        return 2

    if list_marked:
        rows = list_marked_files()
        for name, mk in rows:
            print(f'{name}\t{mk}')
        print(f'# {len(rows)} file(s) carry a body-correction marker')
        return 0

    why = vacuous_in_ci(base)
    if why:
        print(f'asm_body_guard: MISCONFIGURED -- {why}')
        return 2

    head = stream_from_git(base)
    work = stream_from_worktree()
    if not work:
        print('asm_body_guard: MISCONFIGURED -- found 0 files in asm/ from %s. '
              'A guard that inspects nothing cannot go red; refusing to report conserved.'
              % os.getcwd())
        return 2
    a, b = canonical(head), canonical(work)
    if not a:
        print(f'asm_body_guard: MISCONFIGURED -- base {base!r} has 0 asm/ files.')
        return 2
    if a == b:
        print('asm/ body content conserved vs %s (%d lines, %d -> %d files)'
              % (base, len(b), len(head), len(work)))
        return 0

    # The stream changed. That is legal only inside files that carry a correction marker.
    head_raw, work_raw = raw_from_git(base), raw_from_worktree()
    # A rename is a removal plus an addition, and the whole point of this tree's naming rule is
    # that a discovered name goes into the filename in the same commit that learns it. Before
    # this, a rename that also corrected a body was permitted only by accident: the new file
    # passed on its marker, the removed one was an unconditional offender, and the pair went
    # green the moment it was committed because the guard compares against HEAD. That is a gate
    # whose verdict depends on whether you committed before running it, which is not a gate.
    # A removal is now permitted when the same address reappears as a new file carrying a
    # marker — i.e. the rename is attributed to a correction, which is the only legitimate
    # reason for a body to move between filenames.
    # Two rename shapes, and they are not the same case. A PURE rename keeps the same address and a
    # byte-identical body and moves only the filename: that is what section 3 REQUIRES when a
    # session learns what a routine does, and it owes no marker because no body changed. Ten such
    # pairs made main red on 2026-11-23 -- the guard had no case for them, so every legitimate
    # rename was reported as an unmarked body edit. A rename that ALSO corrects a body is the other
    # shape: the new file must carry the marker, and the removal is attributed to it.
    head_by_addr = {os.path.basename(p).split('_')[0]: p for p in head_raw}
    work_by_addr = {os.path.basename(p).split('_')[0]: p for p in work_raw}
    pure_renames = set()
    for addr, old_path in head_by_addr.items():
        new_path = work_by_addr.get(addr)
        if (new_path and new_path != old_path
                and body_lines(head_raw[old_path]) == body_lines(work_raw[new_path])):
            pure_renames.add(old_path)
            pure_renames.add(new_path)
    renamed_targets = {os.path.basename(p).split('_')[0]
                       for p, raw in work_raw.items()
                       if p not in head_raw and marker_in(raw)}
    offenders, permitted = [], []
    for path in sorted(set(head_raw) | set(work_raw)):
        old, new = head_raw.get(path), work_raw.get(path)
        if path in pure_renames:
            permitted.append((path, 'renamed; body byte-identical, so a name change is not a body change'))
        elif old is None:          # brand-new file: its body is new content
            mk = marker_in(new) if new else None
            (permitted if mk else offenders).append((path, mk or 'new file, no marker'))
        elif new is None:          # removed file: its body went somewhere or vanished
            mk = marker_in(old) if old else None
            addr = os.path.basename(path).split('_')[0]
            if addr in renamed_targets:
                permitted.append((path, 'renamed (body moved to a marked new file at %s)' % addr))
            else:
                offenders.append((path, mk or 'file removed'))
        elif head_raw[path] != work_raw[path] and body_lines(old) != body_lines(new):
            mk = marker_in(new)
            (permitted if mk else offenders).append(
                (path, mk or 'body changed, no marker'))
    if offenders:
        print('asm/ BODY CONTENT CHANGED (%d lines at HEAD -> %d now)' % (len(a), len(b)))
        for i, (x, y) in enumerate(zip(a, b)):
            if x != y:
                print('  first difference at body line %d:' % i)
                print('    HEAD: %r' % x)
                print('    now : %r' % y)
                break
        else:
            print('  prefix identical; length differs (content added or removed)')
        print('\n  %d file(s) changed body WITHOUT a valid marker:' % len(offenders))
        for path, why in offenders:
            print('    %s  (%s)' % (path.split('/')[-1], why))
        print('\n  To correct a provably-wrong decode, add to that file:\n'
              '    # body corrected: YYYY-MM-DD -- evidence: <what proved it>\n'
              '  then run tools/assemble_clean.sh: a correction that changes any byte\n'
              '  must still fail there, and that failure is the real gate.')
        return 1
    print('asm/ body content CHANGED in %d marked file(s) -- permitted:' % len(permitted))
    for path, mk in permitted:
        print('  %s\n      marker: %s' % (path.split('/')[-1], mk))
    print('  REMINDER: this gate only checks authorship. Run tools/assemble_clean.sh;\n'
          '  a marked correction that changes any byte must fail there.')
    return 0


if __name__ == '__main__':
    sys.exit(main())

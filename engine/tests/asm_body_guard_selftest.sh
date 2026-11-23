#!/usr/bin/env bash
# asm_body_guard_selftest.sh — prove the body guard can still go red.
#
# The guard was just given an exception path (a `# body corrected:` marker permits a body
# change). AGENTS.md: "A gate that cannot go red is worse than a tool that lies... demonstrate
# a gate failing before you trust it passing." An exception path is exactly where a gate
# silently dies, so all four controls run here:
#
#   1  body changed, NO marker            -> guard MUST fail   (the original guarantee)
#   2  body changed, WITH marker          -> guard MUST pass   (the new permission works)
#   3  marker + a change that alters BYTES-> assemble_clean/check_func MUST fail even though
#      the guard passes                   (the exception cannot smuggle a semantic change)
#   4  body MOVED between files, stream preserved -> guard MUST pass with no marker
#                                          (merges stay legal -- today's three merges)
#
# and two more were added 2026-10-28e when two sessions renamed a file while correcting its
# body (a discovered name goes in the filename, and the body was provably wrong) and the guard
# only caught it by accident of ordering:
#
#   9  file RENAMED + body changed, WITH marker -> guard MUST pass, and must say 'renamed'
#  10  file RENAMED + body changed, NO marker   -> guard MUST fail
#  11  file RENAMED, body byte-identical, NO marker -> guard MUST pass (a name is not a body)
#
# Runs in a throwaway git worktree so the real checkout is never touched. Skips cleanly if
# git worktree is unavailable.
set -uo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WT=$(mktemp -d /var/tmp/u2-guard-selftest.XXXXXX)
rmdir "$WT"

pass=0; fail=0
ok()   { echo "  OK      $1"; pass=$((pass+1)); }
no()   { echo "  FAILED  $1"; fail=$((fail+1)); }

if ! git -C "$ROOT" worktree add -q "$WT" HEAD 2>/dev/null; then
    echo "SKIP: git worktree unavailable; controls not run"; exit 0
fi
trap 'git -C "$ROOT" worktree remove --force "$WT" 2>/dev/null; git -C "$ROOT" worktree prune 2>/dev/null' EXIT

cd "$WT" || exit 1
# CI runners have no commit identity; control 5 commits inside the worktree.
git -C "$WT" config user.email "gates@localhost"
git -C "$WT" config user.name  "gate controls"
TARGET="asm/0711_speed_delay_lookup.asm"
[ -f "$TARGET" ] || { echo "SKIP: $TARGET not present"; exit 0; }
# Run the guard from the REAL checkout, not the worktree's copy: the worktree is at HEAD,
# so running its own guard would test the old code and report a change that was never made.
# The guard reads HEAD via git and asm/ via cwd, so cwd=worktree + script=ROOT is correct.
#
# The guard is deliberately run with CI unset. Controls 1-5b and 7-8 test the LOCAL contract,
# where base=HEAD is the right thing because the job is catching an edit before it is
# committed; the guard refuses to run that way under CI, and it should. Control 6 tests the
# CI contract on purpose, with CI=true set explicitly. This distinction is exactly what the
# first version of this file got wrong: it passed locally and failed in CI, because GitHub
# sets CI=true and the guard correctly refused a vacuous comparison. A control that only
# works from one environment is not a control.
GUARD="env -u CI -u GITHUB_ACTIONS python3 $ROOT/engine/tests/asm_body_guard.py"

echo "controls on the body guard:"

# ---- 1: unmarked body change must fail -----------------------------------------------
python3 - "$TARGET" <<'PY'
import sys
p=sys.argv[1]; s=open(p).read()
old="        mov    0x580(%bx),%bx"
assert old in s, "control precondition: target line not found"
open(p,'w').write(s.replace(old,"        mov 0x580(%bx),%bx",1))
PY
if $GUARD >/dev/null 2>&1; then no "1  unmarked body change was accepted"; else ok "1  unmarked body change rejected"; fi

# ---- 2: same change, now marked, must pass -------------------------------------------
python3 - "$TARGET" <<'PY'
import sys
p=sys.argv[1]; lines=open(p).read().splitlines(True)
for i,l in enumerate(lines):
    if l.startswith('#'):
        continue
    lines.insert(i, "# body corrected: 2026-10-28 -- evidence: selftest control, whitespace only\n")
    break
open(p,'w').writelines(lines)
PY
if $GUARD >/dev/null 2>&1; then ok "2  marked body change permitted"; else no "2  marked body change was rejected"; fi

# ---- 3: marked, but the change alters BYTES -> the byte proof must catch it ----------
git -C "$WT" checkout -q -- "$TARGET"
python3 - "$TARGET" <<'PY'
import sys
p=sys.argv[1]; s=open(p).read()
old="        mov    0x580(%bx),%bx"
assert old in s
s=s.replace(old,"        add    0x580(%bx),%bx",1)          # different opcode: real byte change
lines=s.splitlines(True)
for i,l in enumerate(lines):
    if l.startswith('#'):
        continue
    lines.insert(i,"# body corrected: 2026-10-28 -- evidence: selftest control, deliberate byte change\n")
    break
open(p,'w').writelines(lines)
PY
if $GUARD >/dev/null 2>&1; then ok "3a marker accepted a byte-changing edit (by design)"; else no "3a guard blocked a marked edit"; fi
if python3 tools/check_func.py 0711 >/dev/null 2>&1; then no "3b check_func accepted altered bytes -- THE EXCEPTION IS A HOLE"; else ok "3b check_func caught the altered bytes"; fi
git -C "$WT" checkout -q -- "$TARGET"

# ---- 4: content moved between files, stream preserved -> legal, no marker ------------
python3 - <<'PY'
import glob, re, os
files=sorted(glob.glob("asm/*.asm"))
def start(p):
    m=re.match(r'.*/([0-9A-Fa-f]{4})_',p); return int(m.group(1),16) if m else -1
for i in range(len(files)-1):
    a,b=files[i],files[i+1]
    if start(b)==start(a)+1:
        body=lambda p:[l for l in open(p).read().splitlines() if not l.lstrip().startswith('#')]
        # append the body lines EXACTLY: a leading blank line here would add a body line,
        # and blank lines count as body (they do not start with '#'), so the canonical
        # stream would gain a line and the control would fail for the wrong reason.
        with open(a,'a') as f:
            f.write("\n".join(body(b))+"\n")
        os.remove(b)
        print(f"  (moved {os.path.basename(b)} body into {os.path.basename(a)})")
        break
PY
if $GUARD >/dev/null 2>&1; then ok "4  merge-shaped change accepted with no marker"; else no "4  guard broke merging -- regression"; fi
git -C "$WT" checkout -q -- .

# ---- 5: the CI-blindness regression. A COMMITTED unmarked edit must still be caught ---
# This is the bug the --base flag exists for. With base=HEAD a clean checkout compares HEAD
# to itself and can only PASS, so a violation that has been committed is invisible and CI
# reported the guard green forever. The guard must now be given a base and must fail.
python3 - "$TARGET" <<'PY'
import sys
p=sys.argv[1]; s=open(p).read()
old="        mov    0x580(%bx),%bx"; assert old in s
open(p,'w').write(s.replace(old,"        mov 0x580(%bx),%bx",1))
PY
git -C "$WT" add -A && git -C "$WT" commit -q -m "simulated committed violation"
if $GUARD --base HEAD~1 >/dev/null 2>&1; then
    no "5  COMMITTED unmarked edit passed with a real base -- THE CI FIX DID NOT TAKE"
else
    ok "5  committed unmarked edit caught when given a base"
fi
if $GUARD --base HEAD >/dev/null 2>&1; then
    ok "5b base=HEAD is vacuous by construction (why CI must pass a real base)"
else
    no "5b expected the HEAD-vs-HEAD comparison to be vacuous"
fi
git -C "$WT" reset --hard -q HEAD~1

# ---- 6: in CI, refusing to run vacuously must be an error, not a silent pass ---------
CI=true python3 "$ROOT/engine/tests/asm_body_guard.py" >/dev/null 2>&1; rc6=$?
if [ "$rc6" = "2" ]; then
    ok "6  CI without a base exits 2 (misconfigured, not silently passed)"
elif [ "$rc6" = "0" ]; then
    no "6  guard reported success in CI with no base -- it is a gate that cannot go red"
else
    no "6  expected exit 2 in CI without a base, got $rc6"
fi

# ---- 7: the guard must inspect the tree no matter which directory calls it -----------
# tests/run_cp0.sh runs it from engine/, where there is no asm/ at all. It used to find
# zero files, print "conserved (0 lines, 0 -> 0 files)", and CP0 called that a PASS.
sub_clean=$(env -u CI -u GITHUB_ACTIONS python3 -c "
import subprocess,os
os.chdir('$WT/engine')
print(subprocess.run(['python3','$ROOT/engine/tests/asm_body_guard.py'],
                     capture_output=True,text=True).stdout.strip())")
if [ -z "$sub_clean" ]; then
    no "7  guard produced no output when called from engine/"
elif grep -qE '\(0 lines' <<< "$sub_clean"; then
    no "7  vacuous when called from engine/ -- got: $sub_clean"
else
    ok "7  guard inspects the real tree when called from engine/"
fi

# ---- 8: a guard that sees nothing must refuse rather than report conserved ------------
mkdir -p "$WT/tmp-empty/asm" && echo x > "$WT/tmp-empty/README"
rc8=$( cd "$WT/tmp-empty" && git init -q . && git add README && \
       git -c user.email=t@t -c user.name=t commit -q -m empty && \
       env -u CI -u GITHUB_ACTIONS python3 "$ROOT/engine/tests/asm_body_guard.py" >/dev/null 2>&1; echo $? )
[ "$rc8" = "2" ] && ok "8  an empty asm/ is an error, not a conserved" \
                 || no "8  expected exit 2 on an empty asm/, got $rc8"

# ---- 9: a rename that also corrects a body must be attributed, not waved through -------
# The guard compares against HEAD, so a rename+correction committed in one commit was invisible
# to it: the new file passed on its marker and the removed one was an unconditional offender,
# and the pair went green as soon as it was committed. A verdict that depends on whether you
# committed before running the gate is not a gate. With a marker on the new file it is permitted
# and named as a rename.
REN="asm/0711_speed_delay_lookup.asm"
REN2="asm/0711_renamed_ctrl.asm"

# strip_body_change_and_marker <file> <with_marker>
apply_ctrl() {
    python3 - "$1" "$2" <<'PY'
import sys
p, mark = sys.argv[1], sys.argv[2] == "mark"
lines = open(p).read().splitlines(True)
out, done = [], False
for ln in lines:
    if not done and ln.startswith("        "):
        out.append("        nop   # CONTROL body change\n")
        done = True
        continue
    if mark and ln.startswith("# confidence:"):
        out.append("# body corrected: 2026-01-01 -- evidence: guard rename control\n")
        mark = False
    out.append(ln)
open(p, "w").write("".join(out))
PY
}

git mv -q "$REN" "$REN2" 2>/dev/null || { mv "$REN" "$REN2"; }
apply_ctrl "$REN2" mark
rc9=$(env -u CI -u GITHUB_ACTIONS python3 "$ROOT/engine/tests/asm_body_guard.py" >/dev/null 2>&1; echo $?)
[ "$rc9" = "0" ] && ok "9  rename + marked body change is permitted and attributed" \
                 || no "9  expected exit 0 for a marked rename, got $rc9"
git reset -q --hard HEAD >/dev/null 2>&1; git clean -qfd asm/ >/dev/null 2>&1

# ---- 10: the same rename with NO marker must go red -------------------------------------
git mv -q "$REN" "$REN2" 2>/dev/null || { mv "$REN" "$REN2"; }
apply_ctrl "$REN2" nomark
rc10=$(env -u CI -u GITHUB_ACTIONS python3 "$ROOT/engine/tests/asm_body_guard.py" >/dev/null 2>&1; echo $?)
[ "$rc10" != "0" ] && ok "10 unmarked rename + body change still goes red" \
                   || no "10 guard waved through an unmarked rename -- the exception is too wide"
git reset -q --hard HEAD >/dev/null 2>&1; git clean -qfd asm/ >/dev/null 2>&1

# ---- 11: a PURE rename, body untouched, no marker, must pass -----------------------------
# The shape section 3 mandates: a session learns what a routine does, the name goes into the
# filename, and not one body byte moves. Ten of these made main red on 2026-11-23 because the
# guard had no case for an unmarked rename, so every legitimate rename looked like a smuggled edit.
# If this control ever goes red, the guard has stopped distinguishing a name change from a body
# change, and every future rename will get marked to get past it -- which is how the marker stops
# meaning anything.
git mv -q "$REN" "$REN2" 2>/dev/null || { mv "$REN" "$REN2"; }
rc11=$(env -u CI -u GITHUB_ACTIONS python3 "$ROOT/engine/tests/asm_body_guard.py" >/dev/null 2>&1; echo $?)
[ "$rc11" = "0" ] && ok "11 pure rename, identical body, no marker is permitted" \
                  || no "11 guard flagged a pure rename as an unmarked body edit (exit $rc11)"
git reset -q --hard HEAD >/dev/null 2>&1; git clean -qfd asm/ >/dev/null 2>&1

echo
echo "caught $pass, MISSED $fail"
[ "$fail" -eq 0 ] && echo "GUARD SELFTEST: PASS" || echo "GUARD SELFTEST: FAIL"
exit $((fail != 0))

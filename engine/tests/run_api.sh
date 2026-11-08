#!/usr/bin/env bash
# tests/run_api.sh — the play API gate. docs/play-api.md, work items A-W1..A-W7.
#
# Every leg is paired with the thing that would make it red, because a gate that cannot
# fail where it is enforced is the defect AGENTS.md § 3 says this project has shipped
# more than once. The controls here are not decorative: AP3 is five refusals, and a
# validation that never refuses is a validation that is not running.
set -uo pipefail

cd "$(dirname "$0")/.." || exit 1
fail=0; skips=0
ok()   { printf '  PASS  %s\n' "$1"; }
bad()  { printf '  FAIL  %s\n' "$1"; fail=1; }
skip() { printf '  SKIP  %s\n' "$1"; skips=$((skips + 1)); }

WORK=$(mktemp -d "${TMPDIR:-/var/tmp}/u2-api.XXXXXX")
trap 'rm -rf "$WORK"' EXIT
U2=./u2
CLIENT="python3 tools/u2api.py"

printf 'engine play API test\n'
[ -x "$U2" ] || { bad "engine not built: run make first"; exit 1; }

# A real content tree is needed only to open the world; the API itself is exercised
# against the cleanroom art so this leg runs in CI with no game files.
DATA=""
[ -d ../original ] && DATA="--data-dir ../original"

# One session, raw stdout captured, so a leg can assert on the bytes and not only on
# what the client chose to raise on.
session() { # session NAME <<< commands-on-stdin
    local name="$1"; shift
    "$U2" --headless --api --cleanroom $DATA --save-dir "$WORK/$name.save" \
         --demo-dir "$WORK/$name.demos" "$@" > "$WORK/$name.out" 2> "$WORK/$name.err"
    return $?
}

json_ok() { # every non-empty line parses
    python3 -c '
import json,sys
bad=0
for i,l in enumerate(open(sys.argv[1])):
    l=l.rstrip("\n")
    if not l: continue
    try:
        o=json.loads(l)
        if o.get("u2api")!=1: bad+=1
    except Exception: bad+=1
sys.exit(1 if bad else 0)' "$1"
}

field() { # field FILE EVENT PATH -> prints a dotted path out of the obs of EVENT
    python3 -c '
import json,sys
want=sys.argv[2]; path=sys.argv[3].split(".")
for l in open(sys.argv[1]):
    l=l.strip()
    if not l: continue
    try: o=json.loads(l)
    except Exception: continue
    if o.get("event")!=want: continue
    v=o.get("obs",{})
    for k in path:
        if k=="": continue
        v=v.get(k) if isinstance(v,dict) else None
        if v is None: break
    print(v if v is not None else "")
    break' "$1" "$2" "$3"
}

# --- AP1: the stream is one JSON object per line, start to finish ----------------
printf '{"cmd":"key","key":"P"}\n{"cmd":"key","key":"U"}\n{"cmd":"quit"}\n' \
    | session ap1
if [ -s "$WORK/ap1.out" ] && json_ok "$WORK/ap1.out"; then
    ok "AP1: every line of a session parses as one API object ($(wc -l < "$WORK/ap1.out") lines)"
else
    bad "AP1: the stream carries a line that is not JSON: $(grep -n -v '^{' "$WORK/ap1.out" | head -1)"
fi
# The control: a stray write to stdout would be exactly one bad line among good ones.
printf 'not json at all\n' > "$WORK/dirty.out"
json_ok "$WORK/dirty.out" && bad "AP1 control: json_ok accepted a non-JSON line" \
                          || ok "AP1 control: json_ok goes red on a stray stdout write"

# --- AP2: create writes the character, with the form's own bonuses --------------
# Male +5 STR, human +5 INT, fighter +15 STR, over a base of STR 10 / INT 40.
# If the API grew its own bonus table instead of calling commit(), these drift.
printf '{"cmd":"create","name":"BRITISH","sex":"M","race":1,"class":1,"stats":[10,10,10,10,10,40]}\n{"cmd":"quit"}\n' \
    | session ap2
STR=$(field "$WORK/ap2.out" created char.stats.str)
INT=$(field "$WORK/ap2.out" created char.stats.int)
NAME=$(field "$WORK/ap2.out" created char.name)
if [ "$STR" = "30" ] && [ "$INT" = "45" ] && [ "$NAME" = "BRITISH" ]; then
    ok "AP2: create applies the form's bonuses (STR 10+5+15=$STR, INT 40+5=$INT, name $NAME)"
else
    bad "AP2: created character is STR=$STR INT=$INT name=$NAME (want 30 / 45 / BRITISH)"
fi
[ -f "$WORK/ap2.save/PLAYER/PLAYER" ] \
    && ok "AP2: the created character is written to the save, so a session is resumable" \
    || bad "AP2: create left no save record"

# --- AP3: every A5 rule refuses on its own --------------------------------------
refuses() { # refuses LABEL JSON
    printf '%s\n{"cmd":"quit"}\n' "$2" | session "ap3_$1"
    grep -q '"event":"error"' "$WORK/ap3_$1.out"
}
RULES_OK=1
for spec in \
    "stat_floor|{\"cmd\":\"create\",\"name\":\"X\",\"sex\":\"M\",\"race\":1,\"class\":1,\"stats\":[$((10-1)),10,10,10,10,40]}" \
    "budget|{\"cmd\":\"create\",\"name\":\"X\",\"sex\":\"M\",\"race\":1,\"class\":1,\"stats\":[40,40,10,10,10,10]}" \
    "name_long|{\"cmd\":\"create\",\"name\":\"ABCDEFGHIJKLMNOP\",\"sex\":\"M\",\"race\":1,\"class\":1,\"stats\":[10,10,10,10,10,40]}" \
    "bad_race|{\"cmd\":\"create\",\"name\":\"X\",\"sex\":\"M\",\"race\":9,\"class\":1,\"stats\":[10,10,10,10,10,40]}" \
    "bad_class|{\"cmd\":\"create\",\"name\":\"X\",\"sex\":\"M\",\"race\":1,\"class\":9,\"stats\":[10,10,10,10,10,40]}" \
    "bad_sex|{\"cmd\":\"create\",\"name\":\"X\",\"sex\":\"Z\",\"race\":1,\"class\":1,\"stats\":[10,10,10,10,10,40]}" \
    ; do
    label="${spec%%|*}"; body="${spec#*|}"
    if refuses "$label" "$body"; then
        ok "AP3: $label refused"
    else
        bad "AP3: $label was accepted — the rule is not being checked"; RULES_OK=0
    fi
done
# And the positive control: the same shape with legal values must NOT refuse.
printf '{"cmd":"create","name":"X","sex":"M","race":1,"class":1,"stats":[10,10,10,10,10,40]}\n{"cmd":"quit"}\n' \
    | session ap3_ok
grep -q '"event":"created"' "$WORK/ap3_ok.out" \
    && ok "AP3 control: a legal character is accepted, so the refusals are not blanket" \
    || bad "AP3 control: a legal character was refused"

# --- AP4: a key reaches the game and moves the state ----------------------------
printf '{"cmd":"key","key":"P"}\n{"cmd":"key","key":"P"}\n{"cmd":"quit"}\n' | session ap4
D1=$(field "$WORK/ap4.out" await digest)
N=$(grep -c '"event":"await"' "$WORK/ap4.out")
if [ "$N" -ge 2 ] && [ -n "$D1" ]; then
    ok "AP4: keys are delivered through the seam and each answer carries a digest ($N awaits)"
else
    bad "AP4: only $N await events, digest='$D1'"
fi

# --- AP5: the A8 refusals -------------------------------------------------------
for combo in "--script /dev/null|script" "--playback /dev/null|playback"; do
    flags="${combo%%|*}"; label="${combo##*|}"
    RC=0
    "$U2" --headless --api $flags --cleanroom $DATA >/dev/null 2>&1 || RC=$?
    [ "$RC" -eq 2 ] && ok "AP5: --api with --$label is refused with its own exit code" \
                    || bad "AP5: --api with --$label exited $RC, want 2"
done
RC=0
printf '{"cmd":"quit"}\n' | "$U2" --api --renderer=terminal --cleanroom $DATA \
    >/dev/null 2>&1 || RC=$?
[ "$RC" -ne 0 ] && ok "AP5: --api with the terminal renderer is refused (both want stdout)" \
                 || bad "AP5: the terminal renderer was allowed to draw on the protocol"

# --- AP6: a caller that vanishes does not hang the engine -----------------------
RC=0
timeout 20 "$U2" --headless --api --cleanroom $DATA < /dev/null >/dev/null 2>&1 || RC=$?
[ "$RC" -eq 0 ] && ok "AP6: stdin at EOF ends the run cleanly instead of blocking forever" \
                 || bad "AP6: a closed stdin left the engine failing (exit $RC)"

# --- AP7: the client drives a whole session -------------------------------------
RC=0
U2="$PWD/$U2" SAVE_DIR="$WORK/ap7.save" $CLIENT > "$WORK/ap7.out" 2>&1 || RC=$?
if [ "$RC" -eq 0 ] && grep -q '"event": "created"' "$WORK/ap7.out"; then
    ok "AP7: tools/u2api.py drives create and three keys end to end"
else
    bad "AP7: the client failed (exit $RC): $(tail -2 "$WORK/ap7.out")"
fi

echo
[ "$fail" -ne 0 ] && { echo "API TEST: FAIL"; exit 1; }
[ "$skips" -ne 0 ] && echo "API TEST: PASS ($skips leg(s) skipped)" || echo "API TEST: PASS"

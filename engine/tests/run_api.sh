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

# AP2b: the numbering round-trips. The state stores race/class 0..3 and create takes 1..4,
# so a caller that sends race 2 must read race 2 back and not 1.
printf '{"cmd":"create","name":"ROUNDTRIP","sex":"F","race":2,"class":3,"stats":[10,10,10,10,10,40]}\n{"cmd":"quit"}\n' \
    | session ap2b
R=$(field "$WORK/ap2b.out" created char.race)
C=$(field "$WORK/ap2b.out" created char.class)
X=$(field "$WORK/ap2b.out" created char.sex)
if [ "$R" = "2" ] && [ "$C" = "3" ] && [ "$X" = "F" ]; then
    ok "AP2b: race/class/sex round-trip in one numbering (sent 2/3/F, read $R/$C/$X)"
else
    bad "AP2b: round-trip is $R/$C/$X, want 2/3/F"
fi

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

# --- AP8: the gear block a bot needs is present, complete, and offset-correct ------
printf '{"cmd":"create","name":"GEAR","sex":"M","race":1,"class":1,"stats":[10,10,10,10,10,40]}\n{"cmd":"quit"}\n' \
    | session ap8
GEAR=$(python3 -c '
import json,sys
for l in open(sys.argv[1]):
    l=l.strip()
    if not l: continue
    try: o=json.loads(l)
    except Exception: continue
    if o.get("event")!="created": continue
    c=o["obs"]["char"]
    items=list(c["items"]); arm=list(c["armour"])
    toks=["ANKH","TRI-LITHIUM","BLUE TASSLE","SKULL KEY","BRASS BUTTON"]
    print("items",len(items),"weaps",len(c["weapons"]),"arm",len(arm),
          "tokens",all(t in items for t in toks),
          "hp",c["hp"],"food",c["food"],"gold",c["gold"],"dead",c["dead"],
          "armorder","".join(a[0] for a in arm),
          "wield",c["wield"]["index"],c["wield"]["name"],
          "worn",c["worn"]["index"],c["worn"]["name"])
    break' "$WORK/ap8.out")
case "$GEAR" in
  *"items 16 weaps 10 arm 7 tokens True"*"hp 400 food 400 gold 400 dead False"*"wield 0 HANDS"*"worn 0 SKIN"*)
    ok "AP8: gear block is complete (16 items, 10 weapons, 7 armour, all five vehicle tokens, decoded 400/400/400, not dead)" ;;
  *) bad "AP8: gear block is wrong or incomplete: $GEAR" ;;
esac
# The armour pool is offset by one (armour_owned[0] is CLOTH, not SKIN, because the Z loop
# reads 0x96(%di) with di = the menu digit). Pinning the name ORDER is what catches a
# regression that silently shifts every armour count by one. SCCPRP is the first letter of
# each key in order: SKIN, CLOTH, LEATHER, CHAIN, PLATE, REFLECT, POWER.
case "$GEAR" in
  *"armorder SCLCPRP"*) ok "AP8 control: armour names run SKIN,CLOTH,LEATHER,CHAIN,PLATE,REFLECT,POWER -- the offset is applied, not assumed" ;;
  *) bad "AP8 control: armour name order is wrong, so the off-by-one is not being handled" ;;
esac

# --- AP9: create actually enters the world ---------------------------------------
# The first build created a character and left the title flow running, so a caller that sent
# game keys was driving the menu. Sector 20 is where creation puts you (asm/5BDC..5BE8).
# The discriminator is the command prompt, not the sector: `create` writes sector 20 into
# the record either way, so a sector read passes even when the title flow still owns the
# keys. `CMD:` is only ever emitted by the overworld, so it is the thing that actually proves
# the handoff happened. (Measured while writing this leg: the sector check passed with the
# handoff disabled, which is why it is not the assertion.)
if grep -q 'CMD:' "$WORK/ap8.out"; then
    ok "AP9: create hands off into the world -- the overworld's CMD: prompt was emitted"
else
    bad "AP9: no CMD: prompt after create; the title flow still owns the keys"
fi
MODE=$(field "$WORK/ap8.out" created mode)
[ "$MODE" = "world" ] && ok "AP9: mode reports world once the handoff happened" \
                      || bad "AP9: mode after create is '$MODE', want world"
# And the negative half: with no character the mode must not claim a world.
printf '{"cmd":"quit"}\n' | session ap9b
M0=$(field "$WORK/ap9b.out" ready mode)
[ "$M0" = "title" ] && ok "AP9 control: with no character the mode is title, so the field can go red" \
                    || bad "AP9 control: mode with no character is '$M0', want title"

# --- AP10: the death signal, in both directions ----------------------------------
# asm/0C62_death.asm zeroes DS:51..59 and prints "<name> IS DEAD!"; the capture named in that
# header shows all four ledger fields at 0000. So the signature is the whole ledger, not HP
# alone, and a predicate that watched only HP would call a wounded character a corpse.
python3 -c '
import sys
b = bytearray(0x280)
nm = b"DEADBOT"
b[0x36:0x36+len(nm)] = nm
b[0x49] = 2; b[0x4A] = 0
sys.stdout.write(" ".join("%02x" % x for x in b) + "\n")' > "$WORK/dead.ds"
printf '{"cmd":"create","name":"LIVE","sex":"M","race":1,"class":1,"stats":[10,10,10,10,10,40]}\n{"cmd":"quit"}\n' \
    | session ap10 --load-ds "$WORK/dead.ds"
DEAD=$(field "$WORK/ap10.out" end char.dead)
DNAME=$(field "$WORK/ap10.out" end char.name)
if [ "$DEAD" = "True" ] && [ "$DNAME" = "DEADBOT" ]; then
    ok "AP10: a zeroed ledger over a named character reads dead=true (the asm/0C62 signature)"
else
    bad "AP10: dead=$DEAD name=$DNAME -- the death signature is not being detected"
fi
grep -q '"dead":false' "$WORK/ap8.out" \
    && ok "AP10 control: a living character reads dead=false, so the predicate is not always true" \
    || bad "AP10 control: a fresh character was reported dead"

# --- AP11: every win-condition predicate in docs/goal-spec.md § 1 is readable ------
# § 7.3 item 1 existed because not one dependency-graph predicate could be read through
# --api. This leg says they can be now. The stat vector is the largest legal one that still
# shows the class and sex bonuses landing: 20+20+10+10+10+20 is exactly the 90-point budget,
# and male +5 STR plus fighter +15 STR turns base 20 into 40, while human +5 INT turns the
# trailing 20 into 25. Note what CANNOT be created: STR 31 and AGI 49 together need 105
# points even after the fighter bonus, so the endgame's own equipment thresholds make the
# Hotel California grind mandatory rather than optional. That is goal-spec § 3 P2.
printf '{"cmd":"create","name":"W","sex":"M","race":1,"class":1,"stats":[20,20,10,10,10,20]}\n{"cmd":"quit"}\n' \
    | session ap11
WOK=$(python3 -c '
import json,sys
for l in open(sys.argv[1]):
    l=l.strip()
    if not l: continue
    try: o=json.loads(l)
    except Exception: continue
    if o.get("event")!="created": continue
    c=o["obs"]["char"]
    have=all(k in c["items"] for k in ["RING","ANKH","TRI-LITHIUM","BLUE TASSLE","SKULL KEY","BRASS BUTTON"])
    print("gates", 1 if have else 0,
          "qs", 1 if "QUICK SWORD" in c["weapons"] else 0,
          "pw", 1 if "POWER" in c["armour"] and "REFLECT" in c["armour"] else 0,
          "str", c["stats"]["str"], "agi", c["stats"]["agi"])
    break' "$WORK/ap11.out")
case "$WOK" in
  "gates 1 qs 1 pw 1 str 40 agi 20")
    ok "AP11: every W-predicate reads from the observation (ring/ankh/tassle/skull/button/lithium, quicksword, reflect+power), and the male-fighter bonuses land" ;;
  *) bad "AP11: a win-condition predicate is not readable: $WOK" ;;
esac

echo
[ "$fail" -ne 0 ] && { echo "API TEST: FAIL"; exit 1; }
[ "$skips" -ne 0 ] && echo "API TEST: PASS ($skips leg(s) skipped)" || echo "API TEST: PASS"

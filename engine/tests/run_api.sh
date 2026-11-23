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

# Some legs assert on what the world does, and there is no world without the gitignored content
# files. Before this, `DATA` was set conditionally and the world legs then failed in CI with
# symptoms that read as engine defects -- AP9 "no CMD: prompt", AP10 "dead=False", AP12 "rolls 0
# cd 0" -- for three days while the code they named was never wrong. The precondition is the data
# dir itself, not CI: gate on the thing that is actually missing, and the same rule holds locally,
# where content is expected and its absence is a real problem worth a loud failure elsewhere.
HAVE_CONTENT=0
[ -n "$DATA" ] && HAVE_CONTENT=1

skip() { printf '  SKIP  %s\n' "$1"; }

# need_content LABEL -> 0 if the world can run, else prints a SKIP and returns 1.
need_content() {
    [ "$HAVE_CONTENT" = 1 ] && return 0
    skip "$1: needs world content (original/ is gitignored), so this leg cannot run here"
    return 1
}

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

# --- AP2d: BOTH creation paths archive, not just the one the gate can drive -------
# Structural, and labelled as such so nobody mistakes it for a behavioural arm. AP2c drives
# the declarative route (--api create -> u2_creation_apply). The route a player takes is the
# title screen's 'C' -> u2_creation_flow, and that one cannot be driven from a script here:
# the creation form is a cursor-driven screen and a blind key script never completes it, so
# every attempt just fell back to the splash and reported nothing.
#
# That gap is how this shipped broken. The rotation was wired into u2_creation_apply only,
# AP2c was green, and no player ever got a clean world. This check exists so the same hole
# cannot reopen quietly: it fails if either caller stops reaching the shared helper.
# It does NOT prove the interactive form completes a creation; only a person or a real
# scripted form driver can show that.
for fn in u2_creation_apply u2_creation_flow; do
    # Anchor on the definition line, not on the name anywhere. The first draft matched
    # index($0, "creation_flow()") and hit a sentence in a comment above the helper, then ran
    # to the helper's own closing brace — so it reported the helper reaching itself and passed
    # with the real call deleted. The control found it; a control that is not run is not a
    # control.
    body=$(awk -v f="$fn" '$0 ~ "^int " f "\\(" {on=1} on {print} on && /^}/ {exit}' \
              src/game/creation.c)
    case "$body" in
        *creation_archive_previous*)
            ok "AP2d: $fn reaches creation_archive_previous, so a new character gets a new world" ;;
        *)
            bad "AP2d: $fn does NOT archive the previous slot — one of the two creation paths " \
                "is creating a character into the old character's mutated world" ;;
    esac
done

# --- AP2c: a new character gets a new world, and the old one is kept ------------
# One slot exists (D5 defers --save NAME) and the slot holds the sector overlay as well
# as the record, so without a rotation the previous character's mutated and stamped
# sectors are the ground the next character loads. That is how a brand new character on
# the shipped tree was walking past monsters that are not there.
# The fixture is a slot holding a record and an overlay, which is exactly what a played
# slot looks like; the overlay here is a stub, because the gate is about where files go.
mkdir -p "$WORK/ap2c.save/PLAYER"
cp "$WORK/ap2.save/PLAYER/PLAYER" "$WORK/ap2c.save/PLAYER/PLAYER"
: > "$WORK/ap2c.save/PLAYER/mapx20"
printf '{"cmd":"create","name":"FRESH","sex":"M","race":1,"class":1,"stats":[10,10,10,10,10,40]}\n{"cmd":"quit"}\n' \
    | session ap2c
arch=$(find "$WORK/ap2c.save" -maxdepth 1 -name 'PLAYER.*' -type d | head -1)
if [ -n "$arch" ] && [ -f "$arch/mapx20" ]; then
    ok "AP2c: create rotated the previous slot aside with its overlay ($(basename "$arch"))"
else
    bad "AP2c: create did not archive the slot that held a game (archives: $(find "$WORK/ap2c.save" -maxdepth 1 -name 'PLAYER.*' | tr '\n' ' '))"
fi
if [ -f "$WORK/ap2c.save/PLAYER/PLAYER" ] && [ -z "$(find "$WORK/ap2c.save/PLAYER" -name 'mapx*' -o -name 'monx*' 2>/dev/null)" ]; then
    ok "AP2c: the new slot holds a record and no sector overlay, so the new world is the release world"
else
    bad "AP2c: the new slot is not clean: $(find "$WORK/ap2c.save/PLAYER" -type f | tr '\n' ' ')"
fi
# Control: a slot with no record is not a game (D6: an empty slot is a lie about a save),
# and rotating one would put a timestamped directory in the save directory for every
# launch that never got as far as a save. The second create above DID rotate — the slot
# held a real record — which is the other half of the rule: a character is worth keeping.
before=$(find "$WORK/ap2c.save" -maxdepth 1 -name 'PLAYER.*' -type d | wc -l)
printf '{"cmd":"create","name":"AGAIN","sex":"M","race":1,"class":1,"stats":[10,10,10,10,10,40]}\n{"cmd":"quit"}\n' \
    | "$U2" --headless --api --cleanroom $DATA --save-dir "$WORK/ap2c.save" \
             --demo-dir "$WORK/ap2c2.demos" > "$WORK/ap2c2.out" 2>"$WORK/ap2c2.err"
after=$(find "$WORK/ap2c.save" -maxdepth 1 -name 'PLAYER.*' -type d | wc -l)
if [ "$after" = "$((before + 1))" ]; then
    ok "AP2c control: a slot holding a real record is worth an archive ($before -> $after)"
else
    bad "AP2c control: creating over a real save left the archive count at $before -> $after"
fi
mkdir -p "$WORK/ap2d.save/PLAYER"
printf '{"cmd":"create","name":"BLANK","sex":"M","race":1,"class":1,"stats":[10,10,10,10,10,40]}\n{"cmd":"quit"}\n' \
    | session ap2d
n=$(find "$WORK/ap2d.save" -maxdepth 1 -name 'PLAYER.*' -type d | wc -l)
if [ "$n" = "0" ]; then
    ok "AP2c control: an empty slot is not a game and rotates to nothing (archives: $n)"
else
    bad "AP2c control: an empty slot produced $n archive director$([ "$n" = 1 ] && echo y || echo ies)"
fi

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
if need_content "AP9"; then
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
fi

# --- AP10: the death signal, in both directions ----------------------------------
# asm/0C62_death.asm zeroes DS:51..59 and prints "<name> IS DEAD!"; the capture named in that
# header shows all four ledger fields at 0000. So the signature is the whole ledger, not HP
# alone, and a predicate that watched only HP would call a wounded character a corpse.
if need_content "AP10"; then
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
fi

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

# --- AP12: the world actually turns, and the door is visible to the caller --------
# This leg exists because the API used to accept every key, answer `await`, report
# `mode: world`, and take no turn at all. A `create` is delivered from inside a poll some
# title-flow screen was already sitting in, and the title flow's stop test lives in its
# no-key arm, so the flow ate the caller's game keys as menu keys and the overworld loop
# was entered with the input spent. Measured before the fix: forty direction keys, `rolls`
# never left 0, and the overworld's own key poll was reached zero times.
#
# `rolls` is the discriminator that cannot be faked by a screen redraw: it is the engine's
# RNG counter, and only a completed world turn advances it. The countdown is the second
# half -- it proves the turn body reached the gate tick, and its value also pins the
# cold-load wrap (256 turns to the first door, so five turns reads 251, not 3).
if need_content "AP12"; then
printf '{"cmd":"create","name":"T","sex":"M","race":1,"class":1,"stats":[20,20,10,10,10,20]}\n' > "$WORK/ap12.keys"
for i in $(seq 1 12); do printf '{"cmd":"key","key":"0xf1"}\n' >> "$WORK/ap12.keys"; done
printf '{"cmd":"quit"}\n' >> "$WORK/ap12.keys"
session ap12 < "$WORK/ap12.keys"
T12=$(python3 -c '
import json,sys
rolls=cd=None
for l in open(sys.argv[1]):
    l=l.strip()
    if not l: continue
    try: o=json.loads(l)
    except Exception: continue
    obs=o.get("obs") or {}
    g=obs.get("gate")
    if g is not None: rolls, cd = obs.get("rolls"), g.get("countdown")
print("rolls", rolls, "cd", cd)' "$WORK/ap12.out")
case "$T12" in
  "rolls "0*" cd 25"*) bad "AP12: the world took no turn ($T12) -- keys are being eaten before the overworld loop" ;;
  "rolls "[1-9]*" cd 251") ok "AP12: keys reach the world -- rng advanced and the gate countdown ticked ($T12)" ;;
  *) bad "AP12: unexpected world state: $T12" ;;
esac
# The control: with no keys at all nothing may have turned. Without this the rolls test
# above would pass against any build that emitted a number.
printf '{"cmd":"create","name":"T","sex":"M","race":1,"class":1,"stats":[20,20,10,10,10,20]}\n{"cmd":"quit"}\n' | session ap12b
python3 -c '
import json,sys
for l in open(sys.argv[1]):
    l=l.strip()
    if not l: continue
    try: o=json.loads(l)
    except Exception: continue
    g=(o.get("obs") or {}).get("gate")
    if g is not None and (o["obs"].get("rolls") or 0) != 0: raise SystemExit(1)
' "$WORK/ap12b.out" \
  && ok "AP12 control: with no keys nothing turned, so the rolls test can go red" \
  || bad "AP12 control: a keyless session advanced the rng"
fi

# And the door itself must be readable, not just implied. At load the original stamps
# nothing, so `present` is false here and that is the correct answer, not a gap.
printf '{"cmd":"create","name":"T","sex":"M","race":1,"class":1,"stats":[20,20,10,10,10,20]}\n{"cmd":"quit"}\n' | session ap12c
python3 -c '
import json,sys
for l in open(sys.argv[1]):
    l=l.strip()
    if not l: continue
    o=json.loads(l)
    g=(o.get("obs") or {}).get("gate")
    if g is None: continue
    need={"present","x","y","slot","countdown","era","world","dest_era","dest_x","dest_y"}
    raise SystemExit(0 if need <= set(g) else 1)
raise SystemExit(1)
' "$WORK/ap12c.out" \
  && ok "AP12: the observation carries a full gate block (cell, slot, countdown, era, world, destination)" \
  || bad "AP12: the gate block is missing or incomplete"

# --- AP13: {"cmd":"load"} boots a saved character straight into the game ----------
# The bot never sees the menu. `create` is "press C and fill the form" declaratively;
# `load` is "press P" declaratively, running the same u2_save_load() the title flow uses.
# Recovery for a bot is therefore: end the process, relaunch against the slot, load.
if [ -z "$DATA" ]; then
    skip "AP13: the round trip needs a content tree that can open a world"
else
    SLOT="$WORK/ap13.slot"
    printf '{"cmd":"create","name":"BOT","race":1,"class":1,"stats":[10,10,10,10,10,40]}\n{"cmd":"key","key":"Down"}\n{"cmd":"key","key":"Q"}\n{"cmd":"quit"}\n' \
      | "$U2" --headless --api --cleanroom $DATA --save-dir "$SLOT" --demo-dir "$WORK/ap13.demos" \
             > "$WORK/ap13a.out" 2>/dev/null
    printf '{"cmd":"load"}\n{"cmd":"state"}\n{"cmd":"quit"}\n' \
      | "$U2" --headless --api --cleanroom $DATA --save-dir "$SLOT" --demo-dir "$WORK/ap13.demos" \
             > "$WORK/ap13b.out" 2>/dev/null
    if grep -q '"event":"loaded"' "$WORK/ap13b.out" \
       && grep -q '"name":"BOT"' "$WORK/ap13b.out"; then
        ok "AP13: a saved character reloads straight into the game with no menu in between"
    else
        bad "AP13: load did not restore the saved character (loaded=$(grep -c '"'"'"event":"loaded"'"'"' "$WORK/ap13b.out") named=$(grep -c '"'"'"name":"BOT"'"'"' "$WORK/ap13b.out"))"
    fi

    # --- AP14 (control): an empty slot must refuse, not silently hand back a blank ------
    # Without this, AP13 could pass by loading nothing at all: a blank record and a loaded
    # record both leave the caller "in the world", and only the error distinguishes them.
    printf '{"cmd":"load"}\n{"cmd":"quit"}\n' \
      | "$U2" --headless --api --cleanroom $DATA --save-dir "$WORK/ap14.empty" \
             --demo-dir "$WORK/ap14.demos" > "$WORK/ap14.out" 2>/dev/null
    if grep -q '"event":"error"' "$WORK/ap14.out" && ! grep -q '"event":"loaded"' "$WORK/ap14.out"; then
        ok "AP14 control: an empty save slot refuses instead of loading a blank character"
    else
        bad "AP14 control: an empty slot did not refuse -- load can pass while loading nothing"
    fi

    # --- AP15: no mid-session load. The original cannot, so neither can we. -------------
    printf '{"cmd":"create","name":"BOT","race":1,"class":1,"stats":[10,10,10,10,10,40]}\n{"cmd":"load"}\n{"cmd":"quit"}\n' \
      | "$U2" --headless --api --cleanroom $DATA --save-dir "$WORK/ap15.slot" \
             --demo-dir "$WORK/ap15.demos" > "$WORK/ap15.out" 2>/dev/null
    if grep -q 'load_only_at_menu' "$WORK/ap15.out"; then
        ok "AP15: load once in the world is refused -- the menu is the only load point"
    else
        bad "AP15: load was accepted mid-session, which the original cannot do"
    fi
fi

echo
[ "$fail" -ne 0 ] && { echo "API TEST: FAIL"; exit 1; }
[ "$skips" -ne 0 ] && echo "API TEST: PASS ($skips leg(s) skipped)" || echo "API TEST: PASS"

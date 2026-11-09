#!/usr/bin/env bash
# tests/run_cp6.sh — CP6 commerce SPEC: the TRANSACT ray, the vendor selector, the price.
#
# Three things happen here, in the order run_cp5.sh established:
#
#   1. tests/commerce_check.c is compiled and run. It is the pure half: the price walk,
#      the selector table, and the BCD charge. It needs no content and runs in CI.
#   2. The behavioural half drives ./u2 against a synthetic sector built by
#      tests/cp6_commerce_seed.py — engine-authored filler, never the player's own files —
#      and asserts the shop text, the gold, and the owned-count slot for each purchase.
#   3. The gaps are asserted too. The pub, the cleric shrine, the wizard shoppe, the clue
#      seller and the talking branch are NOT implemented, and a gate that only checks what
#      works cannot tell an unimplemented branch from a broken one. So the pub case asserts
#      silence, and the two broken-ray cases assert the refusal.
#
# The behavioural half SKIPs when the content strings are empty, which is what they are
# without a copy of the game to extract from (docs/phase4-plan.md: the engine ships zero
# content). A skip that can hide a failure is not a skip — so the skip names the symbols it
# needed, and commerce_check still runs.
set -uo pipefail

cd "$(dirname "$0")/.." || exit 1
fail=0
ok()   { printf '  PASS  %s\n' "$1"; }
bad()  { printf '  FAIL  %s\n' "$1"; fail=1; }
skip() { printf '  SKIP  %s\n' "$1"; }

WORK=$(mktemp -d "${TMPDIR:-/var/tmp}/u2-cp6.XXXXXX")
trap 'rm -rf "$WORK"' EXIT

# --- 1. the pure spec --------------------------------------------------------
if cc -std=c11 -Wall -Wextra -Werror -Isrc -Icontent -o "$WORK/commerce_check" \
        tests/commerce_check.c src/core/*.c src/game/*.c src/debug/*.c 2>"$WORK/cc.err"; then
    "$WORK/commerce_check" > "$WORK/cc.out" 2>&1
    rc=$?
    sed 's/^/        /' "$WORK/cc.out"
    n=$(sed -n 's/^commerce_check: \([0-9]\{1,\}\) checks, [0-9]\{1,\} failures$/\1/p' "$WORK/cc.out" | tail -1)
    if [ "$rc" -eq 0 ] && [ -n "$n" ]; then
        ok "CP6 commerce spec: ${n:-0} checks, price walk + selector + charge"
    else
        bad "CP6 commerce spec: commerce_check reported failures (rc=$rc)"
    fi
else
    bad "CP6 commerce spec: commerce_check.c did not compile"
    sed 's/^/        /' "$WORK/cc.err" | head -10
fi

# --- 2/3. the behavioural half ----------------------------------------------
# Every shop literal here is a `run` locator (the menus are multi-segment db lines the asm
# fallback cannot index), so with no copy of the game they come out empty and the prints are
# empty too. Asserting on them then would be asserting on nothing.
NEED="U2_S_ARMOUR_MENU U2_S_WEAPONS_MENU U2_S_TRANSPORT_MENU U2_S_FOOD_COSTS U2_S_SHOP_SOLD U2_S_GET_WEAPON U2_S_GET_ARMOUR U2_S_GET_WHAT"
EMPTY=""
for sym in $NEED; do
    if grep -q "^#define ${sym} \"\"" content/gen_content.h 2>/dev/null; then
        EMPTY="${EMPTY} ${sym}"
    fi
done

if [ -n "$EMPTY" ]; then
    skip "CP6 commerce behaviour: no copy of the game to extract from (empty:${EMPTY})"
elif [ ! -x ./u2 ]; then
    bad "CP6 commerce behaviour: ./u2 is not built — run make before make test"
else
    run_case() {   # run_case SEEDCASE LABEL KEYS OUTFILE [RNGSTATE]
        local seed="$1" label="$2" keys="$3" out="$4" rng="${5:-}"
        rm -rf "$WORK/$label"
        python3 tests/cp6_commerce_seed.py "$WORK/$label" "$seed" >/dev/null 2>&1 || return 1
        printf "$keys" > "$WORK/$label.keys"
        local extra=()
        [ -n "$rng" ] && extra=(--rng-state "$rng")
        ./u2 --headless --play-map --cleanroom --data-dir "$WORK/$label/content" \
             --save-dir "$WORK/$label/save" --script "$WORK/$label.keys" "${extra[@]}" \
             --trace="$out.trace" --dump-each --dump-state > "$out" 2>&1
        # Flatten the transcript: the engine prints a menu in several str_out calls (the literal,
        # then the pool name, then the price), so a phrase like "AH! YES! CLOTH" spans three of
        # them. Joining every text= field into one stream is what makes a whole-phrase grep work.
        sed -n 's/.*text=//p' "$out.trace" 2>/dev/null | tr -d '\n' > "$out.txt"
    }
    # Text assertions read the transcript, not the final frame: the message band is four rows and
    # scrolls, so a long exchange loses its opening lines off the top of the screen and a frame
    # grep silently under-checks. said OUTFILE "phrase".
    said() { grep -qF -- "$2" "$1.txt" 2>/dev/null; }
    gold_of() { sed -n 's/.*"gold":\([0-9]\{1,\}\).*/\1/p' "$1" | tail -1; }
    field_of() { sed -n 's/.*"'"$2"'":"\([^"]*\)".*/\1/p' "$1" | tail -1; }

    # (a) armour, the known-good end-to-end case: sector 32 (38,5), T + north, item 1, Y.
    #     Quote 0012 and gold 9999 -> 9987 are both live observations of the ORIGINAL
    #     (captures/spice86/armour-buy), so this is the engine matching the game and not
    #     matching itself.
    if run_case armour armour 'T\n0xf0\n1\nY\n' "$WORK/armour.out"; then
        said "$WORK/armour.out" "AH! YES! CLOTH" \
            && ok "CP6 T: the ray reaches the armour shoppe and echoes the pool name" \
            || { bad "CP6 T: no armour name from (38,5) north"; sed -n '21,24p' "$WORK/armour.out"; }
        said "$WORK/armour.out" "YOU ONLY 0012" \
            && ok "CP6 T: CLOTH quotes 0012, the number the original quoted live" \
            || bad "CP6 T: expected 'FOR YOU ONLY 0012'"
        [ "$(gold_of "$WORK/armour.out")" = "9987" ] \
            && ok "CP6 T: gold 9999 -> 9987, the observed charge" \
            || bad "CP6 T: gold is $(gold_of "$WORK/armour.out"), expected 9987"
        said "$WORK/armour.out" "SOLD!" \
            && ok "CP6 T: the yes branch prints SOLD!" \
            || bad "CP6 T: no SOLD! on a yes answer"
    else
        bad "CP6 T: armour case did not seed"
    fi

    # (b) declining costs nothing: asm/2017's non-'Y' arm prints OH, WELL. and never calls
    #     the charge routine at all.
    if run_case armour armour-decline 'T\n0xf0\n1\nN\n' "$WORK/decline.out"; then
        [ "$(gold_of "$WORK/decline.out")" = "9999" ] \
            && ok "CP6 T: answering N leaves gold at 9999 (asm/202B is past the branch)" \
            || bad "CP6 T: a declined purchase took gold: $(gold_of "$WORK/decline.out")"
        said "$WORK/decline.out" "OH, WELL." \
            && ok "CP6 T: the refusal line is the original's OH, WELL." \
            || bad "CP6 T: expected 'OH, WELL.' on a no answer"
    fi

    # (c) weapons: same shape, price index = the digit itself, so item 1 is 0008 and item 3
    #     is 0020 — the axe number the live stop at asm/20AB produced. The offer line is
    #     "AH! YES! AXE" in the original's own frame (captures/spice86/g23-weapons/w5-axe.png),
    #     so the shoppe does name the item it is offering; what it does not do is enumerate the
    #     pool the way cmd_W's menu does, and that is the gap asserted next.
    if run_case weapons weapons 'T\n0xf0\n3\nY\n' "$WORK/weapons.out"; then
        said "$WORK/weapons.out" "AH! YES! AXE" \
            && ok "CP6 T: the north-west half of a town is the weapons shoppe" \
            || bad "CP6 T: no weapons name from (9,5) north"
        for other in MACE PHASER "QUICK SWORD"; do
            said "$WORK/weapons.out" "$other" \
                && bad "CP6 T: the weapons shoppe enumerated the pool (found $other)" \
                || true
        done
        ok "CP6 T: the weapons shoppe offers one item and does not list the pool"
        said "$WORK/weapons.out" "YOU ONLY 0020" \
            && ok "CP6 T: AX quotes 0020, the number asm/20AB was stopped leaving" \
            || bad "CP6 T: expected 'FOR YOU ONLY 0020' for menu item 3"
        [ "$(gold_of "$WORK/weapons.out")" = "9979" ] \
            && ok "CP6 T: the axe charge is 20, as the live purchase took it" \
            || bad "CP6 T: axe gold is $(gold_of "$WORK/weapons.out"), expected 9979"
    fi

    # (d) transport: no menu digit at all — the horse price is the constant 4 — and the
    #     purchase mounts, which is the only place outside cmd_B the travel mode changes.
    if run_case transport transport 'T\n0xf0\nY\n' "$WORK/transport.out"; then
        said "$WORK/transport.out" "GOLD, DEAL, OK" \
            && ok "CP6 T: the south-east half of a town is the transport shoppe" \
            || bad "CP6 T: no transport offer from (38,40) north"
        said "$WORK/transport.out" "0032" \
            && ok "CP6 T: a horse quotes 0032 at maxed stats" \
            || bad "CP6 T: expected the horse price 0032"
        [ "$(gold_of "$WORK/transport.out")" = "9967" ] \
            && ok "CP6 T: the horse charge is 32, the first live price asm/20AB reproduced" \
            || bad "CP6 T: horse gold is $(gold_of "$WORK/transport.out"), expected 9967"
        [ "$(field_of "$WORK/transport.out" stamp)" = "22" ] \
            && ok "CP6 T: buying the horse sets DS:13 to 0x22, no tile and no inventory slot" \
            || bad "CP6 T: DS:13 is $(field_of "$WORK/transport.out" stamp), expected 22"
    fi

    # (d2) the SAME purchase driven with a LOWERCASE y.
    #
    # The case that was missing while a human could not buy anything at all. asm/4FD3..asm/4FDB
    # folds a-z to A-Z inside the key seam and asm/4F4D's header records that BOTH returns fall
    # into that tail, but the engine folded only on the poll path. The command loop polls, so
    # every scripted test above passed; the shops WAIT, so a real keyboard was refused.
    #
    # No capture could have caught this, which is the actual lesson: tools/spice86_type.py
    # sends the SHIFTED key, so no automated input has ever produced a lowercase byte. A gate
    # that only exercises the one input the injector happens to emit is not testing the input.
    if run_case transport transport-lc 'T\n0xf0\ny\n' "$WORK/transport_lc.out"; then
        [ "$(gold_of "$WORK/transport_lc.out")" = "9967" ] \
            && ok "CP6 T: a LOWERCASE y buys the horse -- asm/4FD3 upper-casing is on the wait path too" \
            || bad "CP6 T: lowercase y did not buy (gold $(gold_of "$WORK/transport_lc.out"), expected 9967) -- the seam fold is missing on the wait path"
        [ "$(field_of "$WORK/transport_lc.out" stamp)" = "22" ] \
            && ok "CP6 T: lowercase y mounts the horse, DS:13 = 0x22" \
            || bad "CP6 T: lowercase y left DS:13 at $(field_of "$WORK/transport_lc.out" stamp)"
    fi

    # (e) grocery: a village's north-east half, the constant index 3, and the food byte
    #     moving by ONE in packed decimal (asm/181A), which is not what "PER 100" suggests.
    if run_case grocery grocery 'T\n0xf0\nY\n' "$WORK/grocery.out"; then
        said "$WORK/grocery.out" "COSTS 0020" \
            && ok "CP6 T: the grocery quote is 0020, not the 0200 this project repeated for a week" \
            || { bad "CP6 T: expected 'THE FOOD HERE COSTS 0020'"; sed -n '21,24p' "$WORK/grocery.out"; }
        [ "$(gold_of "$WORK/grocery.out")" = "9979" ] \
            && ok "CP6 T: the grocery charge is 20, measured at asm/2052 and in gold" \
            || bad "CP6 T: grocery gold is $(gold_of "$WORK/grocery.out"), expected 9979"
        [ "$(field_of "$WORK/grocery.out" food)" = "51 00 00" ] \
            && ok "CP6 T: food 5000 -> 5100: asm/181A adds one unit to DS:53, not a hundred" \
            || bad "CP6 T: food is $(field_of "$WORK/grocery.out" food), expected '51 00 00'"
    fi

    # (f) the pub is dispatched and unimplemented. Silence is the honest answer, and it is
    #     asserted so that a future half-implementation cannot land quietly.
    if run_case pub pub 'T\n0xf0\n' "$WORK/pub.out"; then
        if said "$WORK/pub.out" "WELCOME TO THE PUB" || said "$WORK/pub.out" "1-BUY"; then
            bad "CP6 T: the pub printed a menu, and commerce.c says it is not implemented"
        else
            ok "CP6 T: the pub quadrant stays silent, matching the documented gap"
        fi
    fi

    # (g) the two ways the ray fails. Both must print FUNNY, NO RESPONSE! and nothing else,
    #     and neither may open a shop.
    for c in nosign nomarker; do
        if run_case "$c" "$c" 'T\n0xf0\n' "$WORK/$c.out"; then
            said "$WORK/$c.out" "FUNNY, NO R" \
                && ok "CP6 T: ${c} ray refuses with FUNNY, NO RESPONSE!" \
                || { bad "CP6 T: ${c} should refuse; got:"; sed -n '21,24p' "$WORK/$c.out"; }
            if said "$WORK/$c.out" "SHOPPE" || said "$WORK/$c.out" "COSTS"; then
                bad "CP6 T: ${c} refused and opened a shop anyway"
            else
                ok "CP6 T: ${c} opens nothing"
            fi
        fi
    done
    # --- the two command letters first observed on 2026-10-29 -----------------
    # cmd_R (READY WEAPON) and cmd_N (NEGATE TIME). Both are gated here because both were
    # read-only-until-this-week, and cmd_N is the letter that had never executed in any harvest.

    # (h) ready a weapon you own and are agile enough for. The seed owns 1..9 except 7, and
    #     agility is 0x40 = 64 packed, so digit 8 passes and digit 9 (72) does not.
    if run_case armour cmdr-ok 'R\n3\n' "$WORK/cmdr-ok.out"; then
        said "$WORK/cmdr-ok.out" "AXE READY." \
            && ok "CP6 R: READY WEAPON echoes the pool NAME and readies the digit" \
            || { bad "CP6 R: expected 'AXE READY.'"; sed -n '21,24p' "$WORK/cmdr-ok.out"; }
    fi
    if run_case armour cmdr-owned 'R\n7\n' "$WORK/cmdr-owned.out"; then
        said "$WORK/cmdr-owned.out" "NOT OWNED" \
            && ok "CP6 R: an unowned weapon is refused on [DS:76 + digit]" \
            || bad "CP6 R: expected NOT OWNED for the weapon the seed does not hold"
        said "$WORK/cmdr-owned.out" " READY." \
            && bad "CP6 R: readied a weapon the seed does not own" \
            || ok "CP6 R: the refusal path leaves nothing readied"
    fi
    if run_case armour cmdr-agile 'R\n9\n' "$WORK/cmdr-agile.out"; then
        said "$WORK/cmdr-agile.out" "AGILE ENOUGH TO WIELD!" \
            && ok "CP6 R: digit 9 fails the AGILITY gate (9*8=72 > packed 0x40=64)" \
            || bad "CP6 R: expected the agility refusal at digit 9"
        said "$WORK/cmdr-agile.out" "STRONG ENOUGH" \
            && bad "CP6 R: used cmd_W's strength gate instead of agility" \
            || ok "CP6 R: the gate is agility, not the armour handler's strength"
    fi

    # --- the prompt gate -----------------------------------------------------
    # R, W and M all print a menu and then block for a digit. The presenting loop sits above
    # the handler, so without a present at the site the menu reaches the screen only after
    # the answer is typed, and the owner read that as a crash. tests/prompt_gate.py asserts
    # the ordering from the trace; its selftest runs regardless, so the checker is never
    # silently unexercised.
    if python3 tests/prompt_gate.py --selftest >"$WORK/promptgate.self" 2>&1; then
        ok "CP6 prompt gate selftest: all five controls can go red"
    else
        bad "CP6 prompt gate selftest failed:"
        sed 's/^/        /' "$WORK/promptgate.self"
    fi
    if python3 tests/prompt_gate.py >"$WORK/promptgate.out" 2>&1; then
        ok "CP6 prompt gate: R, W and M each put their menu on screen before reading the answer"
    else
        bad "CP6 prompt gate: $(grep '^FAIL' "$WORK/promptgate.out" | head -3 | tr '\n' ';')"
    fi

    # (i) NEGATE TIME spends a STRANGE COIN (items_held[13], which is what DS:E3 is) and sets the
    #     creature-move freeze counter, which then ticks like the paralysis bytes.
    if run_case armour cmdn 'N\n' "$WORK/cmdn.out"; then
        said "$WORK/cmdn.out" "YOU RUB A COIN" \
            && ok "CP6 N: with a strange coin, NEGATE TIME falls into the coin rub" \
            || { bad "CP6 N: expected 'YOU RUB A COIN...'"; sed -n '21,24p' "$WORK/cmdn.out"; }
        items=$(sed -n 's/.*"items":"\([^"]*\)".*/\1/p' "$WORK/cmdn.out" | tail -1)
        case "$items" in
            *"00 00 00 00 00 00 00 00 00 00 00 00 00 00"*)
                ok "CP6 N: the strange coin was spent (items_held[13] -> 0)" ;;
        *)  bad "CP6 N: items did not lose the coin: $items" ;;
        esac
        para=$(sed -n 's/.*"paralysis":"\([^"]*\)".*/\1/p' "$WORK/cmdn.out" | tail -1)
        case "$para" in
            "14 "*) ok "CP6 N: DS:26A set to 0x14, the creature-move freeze" ;;
        *)  bad "CP6 N: DS:26A is '$para', expected 14 ..." ;;
        esac
    fi
    if run_case armour cmdn2 'N\nN\n' "$WORK/cmdn2.out"; then
        said "$WORK/cmdn2.out" "EINSTEIN" \
            && ok "CP6 N: with no coin left it refuses and changes nothing" \
            || { bad "CP6 N: expected the second N to refuse"; sed -n '21,24p' "$WORK/cmdn2.out"; }
    fi
    # --- cmd_S (STEAL) --------------------------------------------------------
    # The branch table is gated purely above (u2_steal_loot_for, 14 checks). What the
    # end-to-end half can add is not the branch — asm/3FA0 puts two coin-flip rolls in front of
    # it, so which branch a scripted run reaches is a function of the RNG and not of the code,
    # and a gate that asserted on one fixed outcome would be asserting on the seed. What it CAN
    # assert is the invariant the original actually guarantees: the line printed and the bytes
    # changed agree. A run that printed STEAL FOOD! and left food alone, or printed NO LUCK!
    # and took food, is broken either way, and both are caught.
    if run_case grocery steal-food 'S\n0xf0\n' "$WORK/steal-food.out"; then
        food=$(field_of "$WORK/steal-food.out" food)
        refused=$(said "$WORK/steal-food.out" "NO LUCK!" && echo yes || echo no)
        paid=$(said "$WORK/steal-food.out" "STEAL FOOD!" && echo yes || echo no)
        case "$refused/$paid/$food" in
            "no/yes/51 00 00") ok "CP6 S: printed STEAL FOOD! and food went 5000 -> 5100" ;;
            "yes/no/50 00 00") ok "CP6 S: printed NO LUCK! and food did not move" ;;
        *)  bad "CP6 S: text and state disagree (refused=$refused paid=$paid food=$food)" ;;
        esac
    fi

    # And the same case with the shift register pinned to all zeros, which makes rand_byte
    # return 0 twice and forces the success arm. Without this the run above can sit on the
    # refusal forever — it happened to do exactly that for forty seeds — and a gate that only
    # ever sees NO LUCK! would pass a broken FOOD branch.
    if run_case grocery steal-win 'S\n0xf0\n' "$WORK/steal-win.out" "000000000000"; then
        said "$WORK/steal-win.out" "STEAL FOOD!" \
            && ok "CP6 S: with the rolls forced, the village north-east arm runs" \
            || bad "CP6 S: expected STEAL FOOD! on a forced roll"
        [ "$(field_of "$WORK/steal-win.out" food)" = "51 00 00" ] \
            && ok "CP6 S: food 5000 -> 5100, one unit in packed decimal (asm/3FDD)" \
            || bad "CP6 S: food is $(field_of "$WORK/steal-win.out" food), expected 51 00 00"
    fi

    # The two town arms, also with the rolls forced. The dump does not expose the owned-count
    # arrays, so these assert on the line and on nothing else — which is still worth having, be-
    # cause the index arithmetic ([DS:96 + (roll&3)+1] and [DS:76 + (roll&7)+1], both +1 so slot
    # 0 is never stolen) is exactly the kind of off-by-one that prints the right words while
    # writing the wrong byte, and a later dump field can close that half.
    if run_case armour steal-armour 'S\n0xf0\n' "$WORK/steal-armour.out" "000000000000"; then
        said "$WORK/steal-armour.out" "STEAL ARMOUR!" \
            && ok "CP6 S: a town's north-east steals armour" \
            || { bad "CP6 S: expected STEAL ARMOUR! from (38,5) north"; \
                 sed -n 's/.*text=//p' "$WORK/steal-armour.out.trace" | tr -d '\n'; echo; }
    fi
    if run_case weapons steal-weapons 'S\n0xf0\n' "$WORK/steal-weapons.out" "000000000000"; then
        said "$WORK/steal-weapons.out" "STEAL WEAPONS!" \
            && ok "CP6 S: a town's north-west steals weapons" \
            || { bad "CP6 S: expected STEAL WEAPONS! from the north-west cell"; \
                 sed -n 's/.*text=//p' "$WORK/steal-weapons.out.trace" | tr -d '\n'; echo; }
    fi

    # The ray is the same two cells as TRANSACT, so the two ways it fails must refuse here too,
    # and refuse without touching anything. These are roll-independent: asm/3F59 rejects before
    # the first roll is spent.
    for c in nosign nomarker; do
        if run_case "$c" "steal-$c" 'S\n0xf0\n' "$WORK/steal-$c.out"; then
            said "$WORK/steal-$c.out" "NO LUCK!" \
                && ok "CP6 S: ${c} ray refuses with NO LUCK!" \
                || { bad "CP6 S: ${c} should refuse"; sed -n '21,24p' "$WORK/steal-$c.out"; }
            if said "$WORK/steal-$c.out" "STEAL FOOD\|STEAL ARMOUR\|STEAL WEAPONS"; then
                bad "CP6 S: ${c} refused and stole anyway"
            else
                ok "CP6 S: ${c} takes nothing"
            fi
        fi
    done

    # A south-half aim is the one the game has no arm for, and it must never pay. The grocery
    # seed's marker cells are to the north, so aim south by standing the same way and pressing
    # South: the near cell is plain floor, which is the same refusal as nomarker and is asserted
    # for its own reason above.
    if run_case armour steal-south 'S\n0xf1\n' "$WORK/steal-south.out"; then
        said "$WORK/steal-south.out" "NO LUCK!" \
            && ok "CP6 S: an aim at nothing refuses" \
            || bad "CP6 S: expected NO LUCK! on a south aim from the armour cell"
    fi
    # --- cmd_F (FIRE) -------------------------------------------------------
    # Only the off-ship arm is gateable end-to-end: the ship stamp DS:13 is not inside the saved
    # record (the record starts at DS:36), so a seed cannot put the player on a frigate and the
    # on-ship path is reachable only by boarding. The damage formula, the kill-without-reward
    # rule and the miss are gated in attack_check instead.
    if run_case armour fire-foot 'F\n' "$WORK/fire-foot.out"; then
        said "$WORK/fire-foot.out" "FIRE WHAT?" \
            && ok "CP6 F: on foot the whole command is FIRE WHAT? (asm/35C9 gates on the ship stamp)" \
            || { bad "CP6 F: expected 'FIRE WHAT?' on land"; sed -n '21,24p' "$WORK/fire-foot.out"; }
        said "$WORK/fire-foot.out" " DIRECT-" \
            && bad "CP6 F: aimed a cannon while off the frigate" \
            || ok "CP6 F: no bearing prompt off the ship"
    fi
    # --- cmd_I (IGNITE TORCH), cmd_D (DESCEND), cmd_K (KLIMB) ----------------
    # The site trio, decoded 2026-10-29 from the fragments the Phase 2 map had cut in half.
    # All three gate on the site kind, and the ones digit of the seeded sector IS that kind,
    # so sector 24 opens the gates without the engine needing a tower interior.
    if run_case torch_lit cmdi 'I\n' "$WORK/cmdi.out"; then
        said "$WORK/cmdi.out" "IGNITE TORCH" \
            && ok "CP6 I: IGNITE TORCH prints its name" \
            || bad "CP6 I: expected 'IGNITE TORCH'"
        said "$WORK/cmdi.out" "NONE OWNED!" \
            && bad "CP6 I: refused a torch the player has" \
            || ok "CP6 I: with torches held it does not refuse"
        light=$(field_of "$WORK/cmdi.out" light)
        case "$light" in
            "02 96") ok "CP6 I: torch 0x03 -> 0x02 (packed decimal) and the light counter refilled to 0x96" ;;
        *)  bad "CP6 I: light pair is '$light', expected '02 96'" ;;
        esac
    fi
    if run_case torch_none cmdi2 'I\n' "$WORK/cmdi2.out"; then
        said "$WORK/cmdi2.out" "NONE OWNED!" \
            && ok "CP6 I: with no torches it prints NONE OWNED!" \
            || bad "CP6 I: expected 'NONE OWNED!' with zero torches"
        light=$(field_of "$WORK/cmdi2.out" light)
        case "$light" in
            "00 "*) ok "CP6 I: the refusal spends nothing" ;;
        *)  bad "CP6 I: refusal changed the light pair: '$light'" ;;
        esac
    fi
    # The kind gate: outside a tower or dungeon the command prints and does nothing at all.
    # That emptiness is the handler, not a gap in it -- three instructions and one store.
    if run_case torch_outside cmdi3 'I\n' "$WORK/cmdi3.out"; then
        said "$WORK/cmdi3.out" "IGNITE TORCH" \
            && ok "CP6 I: in a town it still prints IGNITE TORCH" \
            || bad "CP6 I: expected 'IGNITE TORCH' in a town"
        said "$WORK/cmdi3.out" "NONE OWNED!" \
            && bad "CP6 I: reached the torch test below the kind gate" \
            || ok "CP6 I: the kind gate short-circuits before the torch test"
        light=$(field_of "$WORK/cmdi3.out" light)
        case "$light" in
            "03 00") ok "CP6 I: outside a site a torch is not spent and the counter is not touched" ;;
        *)  bad "CP6 I: kind gate mutated light pair: '$light'" ;;
        esac
    fi
    if run_case tower_climb cmdk 'K\n' "$WORK/cmdk.out"; then
        said "$WORK/cmdk.out" "KLIMB" \
            && ok "CP6 K: KLIMB prints its name" \
            || bad "CP6 K: expected 'KLIMB'"
        said "$WORK/cmdk.out" "TO LEVEL 01" \
            && ok "CP6 K: in a tower KLIMB increments the floor and prints it as two BCD digits" \
            || { bad "CP6 K: expected 'TO LEVEL 01'"; tail -3 "$WORK/cmdk.out.txt"; }
        f=$(field_of "$WORK/cmdk.out" floor)
        [ "$f" = "01" ] && ok "CP6 K: DS:25 went 0 -> 1" || bad "CP6 K: floor is '$f', expected 01"
    fi
    # asm/53B5 always emits two digits, high nibble first, and asm/3823 feeds it `daa` rather
    # than a stored BCD byte. A one-digit level would mean the printer was reimplemented wrong.
    # Two climbs, because [DS:25] is below the saved record and cannot be seeded to 1.
    if run_case tower_climb cmdk2 'K\nK\n' "$WORK/cmdk2.out"; then
        said "$WORK/cmdk2.out" "TO LEVEL 02" \
            && ok "CP6 K: a second climb goes to 02, so the printer is not a bare %d" \
            || { bad "CP6 K: expected 'TO LEVEL 02' after two climbs"; tail -3 "$WORK/cmdk2.out.txt"; }
        f=$(field_of "$WORK/cmdk2.out" floor)
        [ "$f" = "02" ] && ok "CP6 K: two climbs took DS:25 to 2" || bad "CP6 K: floor is '$f', expected 02"
    fi
    if run_case tower_noladder cmdk3 'K\n' "$WORK/cmdk3.out"; then
        said "$WORK/cmdk3.out" "-WHAT?" \
            && ok "CP6 K: on a cell without the 0x10 feature it refuses" \
            || bad "CP6 K: expected '-WHAT?' on a plain cell"
        f=$(field_of "$WORK/cmdk3.out" floor)
        [ "$f" = "00" ] && ok "CP6 K: the refusal leaves the floor alone" \
                        || bad "CP6 K: refusal moved the floor to '$f'"
    fi
    if run_case town_climb cmdk4 'K\n' "$WORK/cmdk4.out"; then
        said "$WORK/cmdk4.out" "-WHAT?" \
            && ok "CP6 K: outside a tower or dungeon KLIMB refuses even on a ladder cell" \
            || bad "CP6 K: expected '-WHAT?' in a town"
    fi
    # DESCEND is KLIMB's mirror, and the tower arm is the one with a live witness: decrementing
    # floor 0 gives 0xFF, the `js` fires, and the site is left. That is d12-ladder-ds25.
    # Climb first to get onto floor 1, since the floor cannot be seeded.
    if run_case tower_both cmdd 'K\nD\n' "$WORK/cmdd.out"; then
        said "$WORK/cmdd.out" "DESCEND" \
            && ok "CP6 D: DESCEND prints its name" \
            || bad "CP6 D: expected 'DESCEND'"
        said "$WORK/cmdd.out" "TO LEVEL 00" \
            && ok "CP6 D: in a tower DESCEND decrements, so the pair are mirrors not synonyms" \
            || { bad "CP6 D: expected 'TO LEVEL 00' after a climb"; tail -3 "$WORK/cmdd.out.txt"; }
        f=$(field_of "$WORK/cmdd.out" floor)
        [ "$f" = "00" ] && ok "CP6 D: K then D round-trips DS:25 back to 0" \
                        || bad "CP6 D: floor is '$f', expected 00"
    fi
    if run_case tower_descend cmdd2 'D\n' "$WORK/cmdd2.out"; then
        # --dump-state prints the sector twice, once as raw bytes and once as the two decimal
        # digits; field_of takes the last, which is the "24"/"20" form.
        sec=$(field_of "$WORK/cmdd2.out" sector)
        case "$sec" in
            "20") ok "CP6 D: at floor 0 the decrement underflows, js fires, and the site is left (sector 24 -> 20)" ;;
        *)  bad "CP6 D: expected the site to be left; sector is '$sec', expected 20" ;;
        esac
        f=$(field_of "$WORK/cmdd2.out" floor)
        [ "$f" = "ff" ] && ok "CP6 D: DS:25 is left at 0xFF, the unguarded underflow the live capture recorded" \
                        || bad "CP6 D: floor is '$f', expected ff"
        said "$WORK/cmdd2.out" "TO LEVEL" \
            && bad "CP6 D: printed a level line on the arm that exits instead" \
            || ok "CP6 D: the exit arm prints no level"
    fi
    # And the two ladder cells really are distinct: on a cell with only the up feature,
    # DESCEND refuses even in a tower. This assertion is here because an earlier version of
    # the round-trip case used a 0x10 cell and failed for exactly this reason, which is the
    # gate proving the feature bits are not interchangeable.
    if run_case tower_climb cmdd4 'D\n' "$WORK/cmdd4.out"; then
        said "$WORK/cmdd4.out" "-WHAT?" \
            && ok "CP6 D: on an up-ladder cell only, DESCEND refuses (0x10 and 0x20 are distinct)" \
            || bad "CP6 D: expected '-WHAT?' on a 0x10-only cell"
    fi
    if run_case town_descend cmdd3 'D\n' "$WORK/cmdd3.out"; then
        said "$WORK/cmdd3.out" "-WHAT?" \
            && ok "CP6 D: in a town DESCEND refuses" \
            || bad "CP6 D: expected '-WHAT?' in a town"
    fi

    # --- the tribute: TRANSACT aimed at a type-0x6C creature ------------------
    # Every rung observed live in captures/spice86/lord-british-tribute, so these assertions
    # are the engine matching the game rather than matching itself. The amounts print as four
    # digits because asm/51A5 renders the rung and then a zeroed DI.
    if run_case tribute_low tribute 'T\n0xf0\n' "$WORK/tribute.out"; then
        said "$WORK/tribute.out" "WELCOME MY CHILD" \
            && ok "CP6 tribute: the greeting prints" \
            || bad "CP6 tribute: expected 'WELCOME MY CHILD'"
        said "$WORK/tribute.out" "FIRST MY 50 G.P. TRIBUTE!" \
            && ok "CP6 tribute: the charge is announced" \
            || bad "CP6 tribute: expected the 50 GP line"
        said "$WORK/tribute.out" "AND FOR IT I RAISE THEE 0300" \
            && ok "CP6 tribute: rung 3 renders as 0300, four digits, value in the top pair" \
            || { bad "CP6 tribute: expected 'RAISE THEE 0300'"; tail -3 "$WORK/tribute.out.txt"; }
        h=$(field_of "$WORK/tribute.out" hp)
        [ "$h" = "03 01" ] && ok "CP6 tribute: HP 0001 -> 0301, so the raise lands on the HIGH byte only" \
                           || bad "CP6 tribute: hp is '$h', expected '03 01'"
        g=$(field_of "$WORK/tribute.out" gold)
        [ "$g" = "99 49" ] && ok "CP6 tribute: gold 9999 -> 9949, exactly 50 GP" \
                           || bad "CP6 tribute: gold is '$g', expected '99 49'"
    fi
    if run_case tribute_mid tributem 'T\n0xf0\n' "$WORK/tributem.out"; then
        said "$WORK/tributem.out" "RAISE THEE 0200" \
            && ok "CP6 tribute: at HP 5000 the rung drops to 2" \
            || bad "CP6 tribute: expected 'RAISE THEE 0200' at HP 5000"
        h=$(field_of "$WORK/tributem.out" hp)
        [ "$h" = "52 00" ] && ok "CP6 tribute: HP 5000 -> 5200" \
                           || bad "CP6 tribute: hp is '$h', expected '52 00'"
    fi
    # The rung that looks like a rip-off and is really an overflow guard.
    if run_case tribute_max tributax 'T\n0xf0\n' "$WORK/tributax.out"; then
        said "$WORK/tributax.out" "RAISE THEE 0000" \
            && ok "CP6 tribute: at HP 9901 the rung is 0, which is what stops HP exceeding 9999" \
            || bad "CP6 tribute: expected 'RAISE THEE 0000' at HP 9901"
        h=$(field_of "$WORK/tributax.out" hp)
        [ "$h" = "99 01" ] && ok "CP6 tribute: HP unchanged at the top rung" \
                           || bad "CP6 tribute: hp moved on the +0 rung: '$h'"
        g=$(field_of "$WORK/tributax.out" gold)
        [ "$g" = "99 49" ] && ok "CP6 tribute: the 50 GP is still taken on the +0 rung (spend precedes ladder)" \
                           || bad "CP6 tribute: gold is '$g', expected '99 49' — the tribute should still be charged"
    fi
    # asm/2052 prints its own refusal and does not return to the success path, so a failed
    # spend must print the raise lines not at all and must not touch HP.
    if run_case tribute_broke tributeb 'T\n0xf0\n' "$WORK/tributeb.out"; then
        said "$WORK/tributeb.out" "YOU DONT HAVE THAT MUCH!" \
            && ok "CP6 tribute: with 20 GP the spend refuses" \
            || bad "CP6 tribute: expected the insufficient-gold refusal"
        said "$WORK/tributeb.out" "RAISE THEE" \
            && bad "CP6 tribute: printed a raise after a failed spend" \
            || ok "CP6 tribute: a failed spend stops before the ladder"
        h=$(field_of "$WORK/tributeb.out" hp)
        [ "$h" = "00 01" ] && ok "CP6 tribute: HP untouched when the tribute could not be paid" \
                           || bad "CP6 tribute: hp is '$h', expected '00 01'"
    fi

    # --- first-person sites are OPEN (CP7) --------------------------------------
    # These two cases used to assert the opposite. The engine used to refuse kinds 4 and 5
    # behind an [engine]-prefixed notice because it could not draw a first-person view, and
    # the gate pinned that refusal so the lock could not silently widen or narrow. CP7 built
    # the renderer (game/dungeon.c), so the assertion is now that the notice is GONE and the
    # mouth behaves like every other site: the sector digit moves. The town case below is
    # still the control, and it is still the reason these two cannot quietly regress into
    # "nothing enters".
    # sector_second is emitted as a bare JSON number ("sector_second":0), not the quoted hex
    # pairs field_of reads, so it needs its own extractor.
    secsec() { sed -n 's/.*"sector_second":\([0-9]\{1,\}\).*/\1/p' "$1" | tail -1; }
    if run_case enter_tower locktower 'E\n' "$WORK/locktower.out"; then
        said "$WORK/locktower.out" "[engine]" \
            && { bad "CP6 lock: the tower is locked again"; tail -3 "$WORK/locktower.out.txt"; } \
            || ok "CP6 lock: the tower mouth is no longer refused -- CP7 opened it"
        s=$(secsec "$WORK/locktower.out")
        [ "$s" = "4" ] && ok "CP6 lock: entering a tower sets the site digit to 4" \
                       || bad "CP6 lock: tower entry set digit '$s', expected 4"
    fi
    if run_case enter_dungeon lockdgn 'E\n' "$WORK/lockdgn.out"; then
        said "$WORK/lockdgn.out" "[engine]" \
            && { bad "CP6 lock: the dungeon is locked again"; tail -3 "$WORK/lockdgn.out.txt"; } \
            || ok "CP6 lock: the dungeon mouth is open too -- kinds 4 and 5 are one family"
        s=$(secsec "$WORK/lockdgn.out")
        [ "$s" = "5" ] && ok "CP6 lock: entering a dungeon sets the site digit to 5" \
                       || bad "CP6 lock: dungeon entry set digit '$s', expected 5"
    fi
    if run_case enter_town locktown 'E\n' "$WORK/locktown.out"; then
        said "$WORK/locktown.out" "[engine]" \
            && bad "CP6 lock: the town was locked too -- the gate is too wide" \
            || ok "CP6 lock: a town mouth still opens, so the lock is specific to kinds 4 and 5"
        s=$(secsec "$WORK/locktown.out")
        [ "$s" = "2" ] && ok "CP6 lock: entering a town sets the site digit to 2 as before" \
                       || bad "CP6 lock: town entry set digit '$s', expected 2"
        # The stamp check. The town seeded above holds exactly one guard, so a run that
        # loaded zero creatures would pass every assertion below -- check the count first.
        n=$(sed -n 's/.*"mon_type":"\([^"]*\).*/\1/p' "$WORK/locktown.out" | tail -1 | wc -w)
        tcount=$(sed -n 's/.*"mon_type":"\([^"]*\).*/\1/p' "$WORK/locktown.out" | tail -1 | tr ' ' '\n' | grep -cv '^00$')
        [ "$tcount" -gt 0 ] && ok "CP6 stamp: the town really has $tcount creature(s), so the checks below are not vacuous" \
                           || bad "CP6 stamp: the seeded town loaded no creatures at all"
        unst=$(sed -n 's/.*"unstamped":\([0-9]\{1,\}\).*/\1/p' "$WORK/locktown.out" | tail -1)
        [ "$unst" = "0" ] && ok "CP6 stamp: every creature is stamped on the very first frame -- no invisible monsters on entry" \
                          || bad "CP6 stamp: $unst creature(s) are in the table but not on the grid after entry"
        gh=$(sed -n 's/.*"ghosts":\([0-9]\{1,\}\).*/\1/p' "$WORK/locktown.out" | tail -1)
        [ "$gh" = "0" ] && ok "CP6 stamp: no ghost cells either -- nothing baked into the map survived as terrain" \
                        || bad "CP6 stamp: $gh ghost cell(s) after entry"
    fi

    # The load-moment latch, against the real map files.
    #
    # This one needs original/mapx20 and original/mapx22, and it is the only assertion that
    # can see the defect at all: the synthetic town gives the guard somewhere to walk, so the
    # post-command creature sweep restamps it and every end-of-run measurement reads clean
    # whether or not the load path stamped. That is how a real bug sat behind a passing gate
    # for a whole session. Measured here: 0 with the load restamp, 32 without it.
    #
    # It SKIPs without the player's copy rather than passing vacuously on synthetic data.
    if [ -r ../original/mapx20 ] && [ -r ../original/mapx22 ]; then
        rm -rf "$WORK/realtown"; mkdir -p "$WORK/realtown/save/PLAYER"
        python3 ../tools/u2_edit.py set ../original/player sector 20 -o "$WORK/realtown/save/PLAYER/PLAYER" >/dev/null 2>&1
        python3 ../tools/u2_edit.py setpos "$WORK/realtown/save/PLAYER/PLAYER" 36 29 >/dev/null 2>&1
        printf 'E\n' > "$WORK/realtown/k.keys"
        real=$(./u2 --headless --play-map --data-dir ../original \
                    --save-dir "$WORK/realtown/save" --script "$WORK/realtown/k.keys" \
                    --dump-state 2>&1 | sed -n 's/.*"unstamped_at_load":\([0-9]\{1,\}\).*/\1/p' | tail -1)
        case "${real:-}" in
            0)  ok "CP6 stamp: real sector-22 entry leaves 0 of 32 creatures unstamped on the first frame" ;;
            '') bad "CP6 stamp: real town entry produced no unstamped_at_load reading" ;;
            *)  bad "CP6 stamp: $real creatures are invisible on entry to the real sector 22 -- the load path did not stamp" ;;
        esac
    else
        skip "CP6 stamp: load-moment latch needs the player's mapx20/mapx22 (synthetic data cannot show this defect)"
    fi

    # --- GET's fixtures in the VAULT: asm/363B's ARMOUR and WEAPON arms -------------
    # The grant is SILENT in the original -- asm/363B prints " WEAPON" and then only touches
    # the owned array -- so a transcript grep proves the branch was taken and nothing about
    # the payout. Each leg therefore asserts BOTH the message and the array, and the RNG is
    # pinned so the roll cannot come up 0 (EMPTY!) and turn a passing leg into a coin flip.
    #
    # vault_plain is the load-bearing one: without it the arm could quietly become "grant on
    # any tile" and both positive legs would still pass.
    nonzero() { [ -n "$(printf '%s' "$1" | tr -d ' 0')" ]; }
    rawfield() { sed -n 's/.*"'"$2"'":"\([^"]*\)".*/\1/p' "$1" | tail -1; }

    RNGPIN="020201000000"
    for c in vault_sword vault_shield vault_plain; do
        run_case "$c" "vault_$c" 'G\n' "$WORK/out_$c" "$RNGPIN"
    done

    vs="$WORK/out_vault_sword"; vh="$WORK/out_vault_shield"; vp="$WORK/out_vault_plain"

    if [ -s "$vs" ] && said "$vs" " WEAPON" && nonzero "$(rawfield "$vs" weapons)"; then
        ok "CP6 GET: 0x58 (art 0x2c) in the VAULT grants a weapon  [$(rawfield "$vs" weapons)]"
    else
        bad "CP6 GET: standing on 0x58 did not grant a weapon (said=$(said "$vs" ' WEAPON' && echo y || echo n) weapons=$(rawfield "$vs" weapons))"
    fi

    if [ -s "$vh" ] && said "$vh" " ARMOUR" && nonzero "$(rawfield "$vh" armour)"; then
        ok "CP6 GET: 0x54 (art 0x2a) in the VAULT grants armour  [$(rawfield "$vh" armour)]"
    else
        bad "CP6 GET: standing on 0x54 did not grant armour (said=$(said "$vh" ' ARMOUR' && echo y || echo n) armour=$(rawfield "$vh" armour))"
    fi

    if [ -s "$vp" ] && said "$vp" " WHAT?" && ! nonzero "$(rawfield "$vp" weapons)" \
            && ! nonzero "$(rawfield "$vp" armour)"; then
        ok "CP6 GET control: a plain floor tile refuses and grants nothing"
    else
        bad "CP6 GET control: a plain floor tile did not refuse cleanly (said=$(said "$vp" ' WHAT?' && echo y || echo n) weapons=$(rawfield "$vp" weapons) armour=$(rawfield "$vp" armour))"
    fi
fi

printf '\n'
    # A consumed TRANSACT- must produce a reply. Three paths in commerce.c used to return
    # from the dealer dispatch having printed nothing at all -- the tlkx talking branch and
    # two shop gaps -- which is what made an owner's "neither of them respond" and "it froze"
    # out of a perfectly live game. See tests/cp6_silence_check.py for why the obvious grep
    # for this is inert and what the non-inert assertion had to be.
    #
    # Needs the real sector 31: the synthetic seed has no NPC standing in a shop's sign cell,
    # and that interception is the whole defect. Skips with a reason rather than passing on
    # data that cannot show it.
    if [ -r ../original/mapx31 ] && [ -r ../original/monx31 ]; then
        python3 tests/cp6_silence_check.py "$PWD/u2" ../original "$WORK/silence" \
            >"$WORK/silence.out" 2>&1
        sil=$?
        case "$sil" in
            0) ok "CP6 silence: $(tail -1 "$WORK/silence.out")" ;;
            1) bad "CP6 silence: $(tail -1 "$WORK/silence.out")" ;;
            *) bad "CP6 silence: the check could not run (exit $sil) -- $(tail -1 "$WORK/silence.out")" ;;
        esac
    else
        skip "CP6 silence: needs the player's mapx31/monx31 (no synthetic NPC blocks the ray)"
    fi

    # The scripted talk branch, against the real dialogue files.
    #
    # Owner report that started this: two NPCs south of the TRANSPORT shoppe in town Linda
    # share a name and one of them should say something about trash, and neither answered.
    # They are JOHN MAYER and GERRY MAYER, tlkx22 records 2 and 3, standing at (53,55) and
    # (54,55) with role bytes 0x82 and 0x83 -- and the role byte IS the record index, which is
    # the rule asm/4209 implements as `sbb $0x80,%al` before calling asm/154E.
    #
    # Needs the player's tlkx22: the synthetic seed ships no dialogue file, and a talk test
    # against an absent tlkx would assert only that the fallback message prints.
    if [ -r ../original/tlkx22 ] && [ -r ../original/monx22 ]; then
        rm -rf "$WORK/talk"; mkdir -p "$WORK/talk/save/PLAYER"
        python3 ../tools/u2_edit.py set ../original/player sector 22 -o "$WORK/talk/save/PLAYER/PLAYER" >/dev/null 2>&1
        python3 ../tools/u2_edit.py setpos "$WORK/talk/save/PLAYER/PLAYER" 53 56 >/dev/null 2>&1
        printf 'T\n' > "$WORK/talk/k.keys"
        printf '\360\n' >> "$WORK/talk/k.keys"        # 0xF0 = up, toward John Mayer at (53,55)
        talk=$(./u2 --headless --play-map --data-dir ../original \
                    --save-dir "$WORK/talk/save" --script "$WORK/talk/k.keys" \
                    --dump-state 2>&1)
        echo "$talk" | grep -qa "JOHN MAYER" \
            && ok "CP6 talk: a role-0x82 slot speaks tlkx22 record 2 (asm/4209 -> asm/154E)" \
            || bad "CP6 talk: John Mayer said nothing -- the scripted branch is not printing the record"
        echo "$talk" | grep -qa "TAKE OUT THE TRASH" \
            && ok "CP6 talk: the record's second line renders, so the embedded CR is handled" \
            || bad "CP6 talk: the line after the embedded CR is missing from the screen"
        # The typed arm: asm/41D5's fall-through goes to asm/4206 -> asm/40A6, so an
        # unmatched creature answers with FUNNY, NO RESPONSE!, and a matched type answers
        # with its own fixed line. The merchant at (51,51) is type 0x68 with role 0x02, so
        # it takes asm/412C rather than the tlkx path.
        python3 ../tools/u2_edit.py setpos "$WORK/talk/save/PLAYER/PLAYER" 51 52 >/dev/null 2>&1
        talk2=$(./u2 --headless --play-map --data-dir ../original \
                     --save-dir "$WORK/talk/save" --script "$WORK/talk/k.keys" \
                     --dump-state 2>&1)
        echo "$talk2" | grep -qa "A MERCHANT" \
            && ok "CP6 talk: a type-0x68 slot with role below 0x80 takes the typed chain (asm/412C)" \
            || bad "CP6 talk: the merchant said nothing -- the typed chain is not wired"
        echo "$talk2" | grep -qa "WILL YOU BUY MY APPLES" \
            && ok "CP6 talk: the merchant's second line renders from the split db literal" \
            || bad "CP6 talk: the merchant's line after the CR is missing"
        # The other arm of asm/40C0 must still be an honest gap, not a silent one.
        echo "$talk" | grep -qa "FUNNY, NO RESPONSE" \
            && bad "CP6 talk: the ray refused instead of reaching the NPC -- geometry moved" || true
    else
        skip "CP6 talk: needs the player's tlkx22/monx22 (synthetic seed ships no dialogue)"
    fi

if [ "$fail" -eq 0 ]; then
    printf 'CP6 SPEC: PASS\n'
else
    printf 'CP6 SPEC: FAIL\n'
fi
exit "$fail"

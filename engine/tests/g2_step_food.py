#!/usr/bin/env python3
"""Build the fixtures for the G2 gate: does a refused step cost food?

The base fixture is the CP5 fight seed (tests/cp5_fight_seed.py), reused rather
than re-invented. Two things change per arm, and every byte is asserted rather
than hoped for, because a fixture that silently did not apply is the failure mode
this project keeps writing down:

  monx00 hp/type of slot 0 -> 0   the slot is empty, so nothing but the player's
                                  own step spends the turn and nothing wounds the
                                  player into a status-band repaint.
  PLAYER DS:53/54/55               either 00/00/13 (thirteen units) or 99/99/00.
                                  Thirteen is the discriminator for the COST: the
                                  step charge is `sbb $0x19`, nineteen, so one step
                                  underflows and the character dies. A charge of
                                  thirteen — what the port produced when it passed
                                  decimal 19 where the asm's immediate 0x19 was
                                  meant — leaves zero and survives, and no charge
                                  at all survives. Death is observable without any
                                  assumption about when the band is repainted,
                                  because the death tail zeroes and redraws it.
                                  99/99/00 is for the arm that has to survive in
                                  order to show its reject line.

Arms (the matching key is 0xf2 = U2_KEY_LEFT, one west step from the entry (2,0)):

  water-poor  mapx00[(1,0)] -> 0x00 water (art 0x00, proven blocked, F11), food 13.
              A REFUSED step must still kill. This is the G2 answer.
  grass-poor  all grass, food 13. An ACCEPTED step must kill too — same charge,
              asm/2423 subtracts ahead of the verdict.
  water-rich  water, food 9999-units. Must survive and print `WEST--INVALID MOVE!`,
              which is what proves the water arm above really travelled the reject
              path rather than the accept path.
  none-poor   no key at all, food 13. The control: if the character dies with no
              input the gate is measuring something else and cannot fail.

Usage: g2_step_food.py DIR water-poor|grass-poor|water-rich|none-poor
"""
import os
import sys

# The seeders share one offset source: state.h, read at runtime by u2_seed.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import u2_seed                                  # noqa: E402

# The file offset of DS:53, the food triple, named rather than written. patch() below takes
# file offsets with an expected-old byte, so this wants the block-relative offset.
_rec = u2_seed.PlayerRecord()
F0 = _rec.off("food")
POOR = (0x00, 0x00, 0x13)
RICH = (0x99, 0x99, 0x00)
ARMS = {
    "water-poor": ("water", POOR),
    "grass-poor": ("grass", POOR),
    "water-rich": ("water", RICH),
    "none-poor":  ("none",  POOR),
}


def patch(path, edits, what):
    data = bytearray(open(path, "rb").read())
    for off, want, value in edits:
        assert data[off] == want, "%s: byte %d is %02X, expected %02X" % (
            what, off, data[off], want)
        data[off] = value
    open(path, "wb").write(bytes(data))


def main(out_dir, arm):
    kind, food = ARMS[arm]
    content = os.path.join(out_dir, "content")
    save = os.path.join(out_dir, "save")

    patch(os.path.join(content, "monx00"),
          [(64, 5, 0), (96, 0x68, 0)], "monx00")

    if kind == "water":
        patch(os.path.join(content, "mapx00"),
              [(0 * 64 + 1, 0x08, 0x00)], "mapx00")

    patch(os.path.join(save, "PLAYER", "PLAYER"),      # saves/<slot>/PLAYER; core/slot.h
          [(F0, 0x99, food[0]), (F0 + 1, 0x99, food[1]),
           (F0 + 2, 0x99, food[2])], "PLAYER")
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 3 or sys.argv[2] not in ARMS:
        print("usage: g2_step_food.py DIR %s" % "|".join(ARMS), file=sys.stderr)
        raise SystemExit(2)
    raise SystemExit(main(sys.argv[1], sys.argv[2]))

"""G83: what one frame of the star-field draw actually costs.

24 stops at the entry of asm/6B3D_space_field_draw, cycles read from the monitor's own
register dump. The delta between consecutive stops is the cost of one full field redraw plus
the caller's loop overhead between calls -- which is the right scope, because that is what
the player waits through. asm/6B3D walks all 64 star-table entries in one call, so one call
is one frame.

Reported as cycles and as milliseconds at the nominal 4.77 MHz, with the same caveat G86
carries: Spice86 counts CPU cycles and models no 8088 bus wait states, so the millisecond
figure is a LOWER bound on real hardware, not an estimate of it.
"""
import re
import statistics
import sys

cycles = [int(m) for m in re.findall(r"Cycles=(\d+)", open(sys.argv[1]).read())]
# bp_read's --state line can repeat for a single stop; collapse runs of equal values rather
# than treating a duplicate as a zero-cost frame, which would halve the mean.
uniq = [c for i, c in enumerate(cycles) if i == 0 or c != cycles[i - 1]]
deltas = [b - a for a, b in zip(uniq, uniq[1:])]
deltas = [d for d in deltas if d > 0]

if not deltas:
    raise SystemExit("no usable deltas from %d samples" % len(cycles))

HZ = 4_770_000
mean = statistics.mean(deltas)
print("samples            %d stops, %d distinct, %d deltas" % (len(cycles), len(uniq), len(deltas)))
print("cycles per frame   min %d  median %d  mean %d  max %d"
      % (min(deltas), statistics.median(deltas), mean, max(deltas)))
print("spread             %.2f%% of mean (a tight spread means the loop is not rate-limited)"
      % (100.0 * (max(deltas) - min(deltas)) / mean))
print()
print("at nominal 4.77 MHz: %.1f ms per frame  ->  %.1f frames/s" % (1000.0 * mean / HZ, HZ / mean))
print()
print("The engine currently uses U2_WARP_FRAME_MS = 2, invented and labelled as such.")
print("Measured here: %.0f ms. That is %.1fx the placeholder."
      % (1000.0 * mean / HZ, (1000.0 * mean / HZ) / 2.0))
print()
print("Caveat, same as G86: this is a lower bound. Spice86 models no 8088 bus wait states,")
print("so real hardware spent more wall-clock time per cycle than this division assumes.")

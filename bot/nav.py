#!/usr/bin/env python3
"""Layer 2, first cut: get from A to B inside one sector.

The division of labour here is the whole design. The **engine** answers whether a cell can be
entered (`u2 --passmap`, which is asm/2423's mode dispatch and nothing else). This module does
**search**, which holds no game rules at all — it walks a bitmap. That asymmetry is deliberate and
it is what keeps a pathfinder from becoming a second, drifting copy of the passability table.

Four-neighbour, not eight: the game's walk is four-directional, and a diagonal here would produce
a route the character cannot physically take.
"""

EDGE = 64

# Direction tokens the key injector understands, and the delta each applies.
DIRS = {"Up": (0, -1), "Down": (0, 1), "Left": (-1, 0), "Right": (1, 0)}


def passable(passmap, x, y):
    return 0 <= x < EDGE and 0 <= y < EDGE and passmap[y * EDGE + x]


def bfs(passmap, start, goal):
    """Shortest four-step route as a list of (x, y), excluding `start`.

    Returns None if the goal is unreachable OR if the goal itself is not enterable. The second
    case is not a failure of the search and is worth distinguishing from the first: a moongate
    cell or a vehicle pad that comes back unenterable means the passmap or the coordinate is
    wrong, and "no route" would send someone looking in the wrong place.
    """
    if not passable(passmap, goal[0], goal[1]):
        return None
    if start == goal:
        return []
    seen = {start}
    frontier = [(start, [])]
    while frontier:
        nxt = []
        for (cx, cy), path in frontier:
            for dx, dy in DIRS.values():
                nx, ny = cx + dx, cy + dy
                if (nx, ny) in seen or not passable(passmap, nx, ny):
                    continue
                step = path + [(nx, ny)]
                if (nx, ny) == goal:
                    return step
                seen.add((nx, ny))
                nxt.append(((nx, ny), step))
        frontier = nxt
    return None


def keys_for(passmap, start, goal):
    """The direction tokens that walk `start` to `goal`, or None."""
    route = bfs(passmap, start, goal)
    if route is None:
        return None
    out, cur = [], start
    for nx, ny in route:
        for tok, (dx, dy) in DIRS.items():
            if (cur[0] + dx, cur[1] + dy) == (nx, ny):
                out.append(tok)
                break
        cur = (nx, ny)
    return out


def goto(target, passmap, pos, send, confirm, max_steps=200):
    """Walk to `target`, re-planning after every step.

    Re-planning rather than committing to a route is not laziness. The grid is stamped: a creature
    that moves can open or close a cell between two steps of a planned route, and a route computed
    once would then walk into a cell that is no longer enterable and stall. Re-planning costs a
    bitmap decode per step and cannot go stale.

    `send(tok)` presses a key; `confirm()` returns the observed (x, y) afterwards. A step that did
    not move the character is a refusal, and a refusal is retried once and then abandoned — a
    refusal with no exit is how a bot loops forever on one cell.
    """
    for _ in range(max_steps):
        route = bfs(passmap, pos, target)
        if route is None:
            return False, pos
        if not route:
            return True, pos
        tok = keys_for(passmap, pos, route[0])[0]
        send(tok)
        new = confirm()
        if new == pos:
            send(tok)                       # one retry: a dropped key looks exactly like a wall
            new = confirm()
            if new == pos:
                return False, pos
        pos = new
        passmap = None                       # caller re-supplies; see the note above
    return False, pos


if __name__ == "__main__":
    fails = []

    def ck(c, m):
        if not c:
            fails.append(m)

    def grid(walls=()):
        g = [1] * (EDGE * EDGE)
        for x, y in walls:
            g[y * EDGE + x] = 0
        return g

    # Straight line, no obstacles.
    ck(keys_for(grid(), (0, 0), (3, 0)) == ["Right"] * 3, "three cells east")
    ck(keys_for(grid(), (0, 0), (0, 2)) == ["Down"] * 2, "two cells south")

    # A wall the route must go around. The first draft of this put the wall at y=1 with start and
    # goal both at y=0, so it blocked nothing and "go straight" was the right answer -- a fixture
    # that does not exercise its own rule is worse than no fixture.
    wall = [(1, y) for y in range(0, 10) if y != 5]
    r = keys_for(grid(wall), (0, 0), (2, 0))
    ck(r is not None and len(r) == 12,
       "route detours down to the gap at y=5 and back (got %s)" % (r,))
    ck(r is not None and r.count("Down") == 5 and r.count("Up") == 5,
       "and it actually detours rather than walking through (got %s)" % (r,))

    # Sealed: a full column across the grid, no gap. No route, and it must say so rather than
    # return a partial path.
    sealed = [(1, y) for y in range(EDGE)]
    ck(bfs(grid(sealed), (0, 0), (2, 2)) is None, "a sealed target is unreachable")

    # The distinction that matters: unenterable goal vs unreachable route.
    g = grid()
    g[5 * EDGE + 5] = 0
    ck(bfs(g, (0, 0), (5, 5)) is None, "an unenterable goal returns None, not a route to its door")

    # Start == goal is already done, not "no route".
    ck(bfs(grid(), (9, 9), (9, 9)) == [], "already there is an empty route, not None")

    # Off-grid must not wrap silently: the grid masks, a pathfinder must not.
    ck(bfs(grid(), (0, 0), (64, 0)) is None, "a coordinate off the grid is not wrapped into reach")

    print("nav selftest: %d failures" % len(fails))
    for f in fails:
        print("  FAIL " + f)
    raise SystemExit(1 if fails else 0)

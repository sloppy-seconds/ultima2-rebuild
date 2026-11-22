/*
 * tests/site_check.c — the world-map gate table: which tile enters which site.
 *
 * asm/3389_cmd_E prints ENTER and asm/33A6..33FF dispatches on [DS:14] >> 1, and
 * DS:14 is map_byte >> 1 (CP5 ticket C7), so the gate is tile >> 2. The mapping
 * below is that shift, and the digit column is NOT the shift: 7 is a tower, which
 * is sector digit 4, and 8 is a castle, which is digit 3. Anyone who "simplifies"
 * that will produce towns that load tower files, quietly.
 *
 * The second half is the check that matters, and it is the one that can only be
 * made against the player's own content: every gate cell in every world sector
 * must name a sector file that exists. On the 2026-10-27 content set that is 21
 * gate cells and 21 existing files, and the dungeon gate in mapx30 is the cell the
 * live capture walked into when DS:49/4A went from 3,0 to 3,5.
 *
 * argv[1] is the content directory; without it the data half SKIPs (a missing
 * --data-dir is not a test failure, same rule as world_check.c).
 *
 * Build and run by tests/run_cp5.sh.
 */
#include <stdio.h>
#include <string.h>

#include "core/ctx.h"
#include "core/world.h"
#include "game/overworld.h"
#include "gen_content.h"

static int fails, checks, skipped;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { fails++; \
    printf("  FAIL  " __VA_ARGS__); printf("\n"); } } while (0)

/* Does the needle appear anywhere on the screen grid? The signpost prints two lines
 * into the message window; asserting presence anywhere avoids pinning the exact row,
 * which the seam's scroll controls. */
static int grid_has(u2_ctx *ctx, const char *needle)
{
    char row[U2_COLS + 1];
    for (int r = 0; r < U2_ROWS; r++) {
        for (int c = 0; c < U2_COLS; c++) row[c] = (char)ctx->vram.cell[r][c].ch;
        row[U2_COLS] = '\0';
        if (strstr(row, needle)) return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *label;
    int ax, ay;

    /* --- the table itself, from asm/33C3..33FF ------------------------------ */
    struct { uint8_t tile; int digit; const char *name; int ax, ay; } want[] = {
        { 0x14, 1, "village",  0x1f, 0x3e },
        { 0x18, 2, "town",     0x1f, 0x3e },
        { 0x1c, 4, "tower",    0x05, 0x05 },
        { 0x20, 3, "castle",   0x1f, 0x3e },
        { 0x24, 5, "dungeon",  0x05, 0x05 },
    };
    for (unsigned i = 0; i < sizeof want / sizeof want[0]; i++) {
        const int d = u2_site_for_tile(want[i].tile, &label, &ax, &ay);
        CHECK(d == want[i].digit, "%s: tile 0x%02x -> digit %d, expected %d",
              want[i].name, want[i].tile, d, want[i].digit);
        CHECK(ax == want[i].ax && ay == want[i].ay,
              "%s: arrival cell (%d,%d), expected (%d,%d)",
              want[i].name, ax, ay, want[i].ax, want[i].ay);
    }

    /* The village arm has no announcement: asm/341E saves the entry pair and
     * loads, with no str_out in front of it. Every other arm prints one. */
    (void)u2_site_for_tile(0x14, &label, &ax, &ay);
    CHECK(label == NULL, "village announces nothing (asm/341E has no string)");
    (void)u2_site_for_tile(0x18, &label, &ax, &ay);
    CHECK(label != NULL && strcmp(label, U2_S_SITE_TOWN) == 0,
          "town announces -TOWN (asm/343F)");

    /* --- the controls: ordinary terrain is not a door ----------------------- */
    /* Without these, a table that matched on every byte would pass the five
       positive checks above and turn every grass tile in the world into a gate. */
    uint8_t plain[] = { 0x00, 0x04, 0x08, 0x0c, 0x10, 0x40, 0x44, 0x4c, 0x50, 0x78 };
    int refusals = 0;
    for (unsigned i = 0; i < sizeof plain; i++) {
        const int d = u2_site_for_tile(plain[i], &label, &ax, &ay);
        if (d == 0) refusals++;
        else printf("  note  tile 0x%02x is a gate (digit %d) — check asm/33C3\n",
                    plain[i], d);
    }
    CHECK(refusals == (int)sizeof plain,
          "control: water, forest, mountain, the player stamp and the four vehicle "
          "tiles are not doors");

    /* --- the data half: every gate cell names a sector that exists ---------- */
    if (argc < 2) {
        skipped++;
        printf("  SKIP  gate-cell cross-check (no content directory given)\n");
    } else {
        u2_ctx ctx;
        memset(&ctx, 0, sizeof ctx);
        ctx.data_dir = argv[1];
        u2_world w;

        int gates = 0, missing = 0;
        for (int hi = 0; hi <= 9; hi++) {
            u2_sector_id id = { (uint8_t)hi, 0 };
            memset(&w, 0, sizeof w);
            if (u2_world_load(&ctx, &w, 1, id) != U2_WORLD_OK)
                continue;                       /* this world sector is not in the pack */

            for (int cell = 0; cell < U2_SECTOR_TILES; cell++) {
                const uint8_t t = w.map[cell];
                const int digit = u2_site_for_tile(t, &label, &ax, &ay);
                if (digit == 0) continue;
                gates++;
                u2_world probe;
                memset(&probe, 0, sizeof probe);
                u2_sector_id sid = { (uint8_t)hi, (uint8_t)digit };
                if (u2_world_load(&ctx, &probe, 1, sid) != U2_WORLD_OK) {
                    missing++;
                    printf("  FAIL  mapx%d%d tile 0x%02x at (%d,%d) names a gate to "
                           "mapx%d%d, which is not there\n",
                           hi, 0, t, cell % U2_SECTOR_EDGE, cell / U2_SECTOR_EDGE,
                           hi, digit);
                }
            }
        }
        CHECK(gates > 0, "the pack has gate cells to check (found %d)", gates);
        CHECK(missing == 0, "every gate cell in every world sector names a sector "
                            "file that exists (%d gates checked)", gates);

        /* G81, closed statically 2026-11-15: nothing a site does to its own sector survives
         * re-entry, and the reason is the loader, not the write helper. asm/34E4 — the
         * "-DUNGEON" arm — calls the store gate at L_0024fd BEFORE it touches DS:4A, so what
         * gets stored is the sector you are standing in and never the one you are entering;
         * then it calls L_002490, which reads 0x1000 bytes over DS:472, the same buffer the
         * store writes from. Leaving is asm/3823, which writes the player record and nothing
         * else. So a town's or dungeon's own mapx is read fresh from disk on every entry and
         * is never written back: an emptied chest refills, and the owner's "nothing in a town
         * or a dungeon persists" rule needs no exception. The engine already matches, because
         * site_reload() runs on both transitions. Pinned here rather than left to that
         * argument, because the chest write in dungeon.c uses the both-copies helper and
         * someone reading only that line will reach for the weaker one. */
        {
            u2_sector_id id = { 3, 1 };          /* sector 31: a town, and it has chests */
            const int cell = 4 + 23 * U2_SECTOR_EDGE;
            u2_world a, b;
            memset(&a, 0, sizeof a);
            if (u2_world_load(&ctx, &a, 1, id) == U2_WORLD_OK) {
                const uint8_t was = a.map[cell];
                u2_world_terrain_put(&a, 4, 23, 0x08);
                CHECK(a.map[cell] == 0x08,
                      "G81: the clear is visible in the live buffer");
                memset(&b, 0, sizeof b);
                (void)u2_world_load(&ctx, &b, 1, id);
                CHECK(b.map[cell] == was,
                      "G81: and a reload restores it — the reload, not the helper, is "
                      "what makes a cleared chest come back");
            } else {
                skipped++;
                printf("  SKIP  G81 reload check (mapx31 not in the pack)\n");
            }
        }
    }

    /* --- gates must admit the player, or `E` can never fire -----------------
     * `E` dispatches on the tile the player is STANDING on, so a gate that refuses
     * entry is a town that cannot be entered. The default-deny passability rewrite
     * of 2026-10-27 blocked arts 0x06/0x07/0x09 as "solid structures" on art-shape
     * reasoning and sealed every town in the game; the suite stayed green because
     * nothing asserted the walkability half of the gate rule. These checks are the
     * reason that cannot happen quietly again. */
    static const struct { int kind; const char *name; } gatek[] = {
        { 5, "village" }, { 6, "town" }, { 7, "tower" },
        { 8, "castle" },  { 9, "dungeon" }
    };
    for (size_t i = 0; i < sizeof gatek / sizeof gatek[0]; i++) {
        const uint8_t mb = (uint8_t)(gatek[i].kind << 2);
        CHECK(u2_tile_walkable_byte(mb),
              "%s gate (tile >> 2 == %d, map byte 0x%02X) is walkable, so E can fire "
              "on it", gatek[i].name, gatek[i].kind, mb);
    }

    /* The four verdicts that were proven by actually walking, kept as a fence around
     * the permissive side of the table: if someone re-widens the default, these go
     * red instead of the world quietly becoming walkable. */
    CHECK(!u2_tile_walkable_byte(0x00), "water 0x00 stays impassable on foot");
    CHECK(!u2_tile_walkable_byte(0x10), "mountain 0x10 stays impassable on foot");
    CHECK(u2_tile_walkable_byte(0x08),  "grass 0x08 stays passable");
    /* Shallow water. docs/map-format.md § 3 records 0x04 as passing from the F11 walks, and
     * the default-deny rewrite of 2026-10-27 blocked it by omission — the same failure mode
     * that sealed every town. asm/2199 refuses 0x04 for land creatures, so the asymmetry
     * (player wades, land monster will not) is what marks it shallows rather than sea. */
    CHECK(u2_tile_walkable_byte(0x04),  "shallow water 0x04 is passable on foot");
    CHECK(u2_tile_walkable_byte(0x0C),  "forest 0x0C stays passable");
    CHECK(!u2_tile_walkable_byte(0xA0), "door 0xA0 stays impassable until unlocked");
    CHECK(!u2_tile_walkable_byte(0x78), "solid-white wall 0x78 stays impassable");
    CHECK(!u2_tile_walkable_byte(0x80), "letter A 0x80 stays impassable");
    /* Town paving (art 0x1C). Blocked by default-deny until the owner, standing in
     * a town, reported the cobblestone around them should be walkable. */
    CHECK(u2_tile_walkable_byte(0x70),
          "town paving 0x70 is passable: it is the ground between the buildings, "
          "12889 cells across 15 sectors");

    /* --- the sector edge: a site leaves on ANY edge, the world map wraps ---------
     * Measured 2026-10-28 by walking off an edge with the save's entry point set
     * away from that edge, so "returned to the entry cell" and "wrapped to x=0"
     * could be told apart. Every edge tested left:
     *   sector 31 east  (digit 1) -> digits 3,0, player at the entry, DS:12 4->0xFF
     *   sector 31 west  (digit 1) -> digits 3,0, player back at the entry from x=0
     *   sector 31 south (digit 1) -> digits 3,0, player at the entry
     *   sector 23 east  (digit 3) -> digits 2,0, player at the entry
     *   sector 20 (digit 0)       -> wraps, sector unchanged (the 511/512 result)
     * Two defects lived here before: the walk loop called site_leave() on any high-edge
     * crossing (so the overworld's torus wrap became a teleport), and the edge test only
     * looked at the high side, so a west or north crossing in a site wrapped instead of
     * leaving. The north edge is the one case that could not be reached by walking in
     * sector 31 and so is inferred from the other three, not measured.
     */
    CHECK(!u2_site_edge_crossed(0, 64, 30) && !u2_site_edge_crossed(0, -1, 30),
          "a world sector (second digit 0) WRAPS at the edge, it does not leave");
    CHECK(u2_site_edge_crossed(1, 64, 35),
          "sector 31 leaves across the east edge, into sector 30");
    CHECK(u2_site_edge_crossed(1, -1, 35),
          "sector 31 leaves across the WEST edge too -- the old high-edge-only test "
          "wrapped instead, and that was wrong");
    CHECK(u2_site_edge_crossed(1, 0, 64),
          "sector 31 leaves across the south edge");
    CHECK(u2_site_edge_crossed(3, 64, 2),
          "sector 23 leaves across the east edge, into sector 20 -- so the digit is a "
          "site index, not a compass axis: 3 goes to 0, not to 2");
    CHECK(!u2_site_edge_crossed(1, 63, 63) && !u2_site_edge_crossed(1, 0, 0),
          "a step that stays inside the sector is not a crossing, corner included");

    /* --- UNLOCK (asm/4309..435E), item 4 ---------------------------------------
     * The rule came out of reading the whole chain after three live attempts refused,
     * and the load-bearing surprise is that there is no vertical aim: asm/4326 computes
     * (x + DS:19, y) and never touches a Y delta, so north and south cannot open
     * anything however the prompt reads. */
    CHECK(u2_unlock_rule(0, 0xA0, 5) == U2_UNLOCK_NO_AIM,
          "a north/south aim (dx == 0) is refused before the cell is even looked at -- "
          "the door byte does not matter");
    CHECK(u2_unlock_rule(-1, 0xA0, 5) == U2_UNLOCK_OPENED,
          "west works as well as east: the gate is a sideways delta, not a direction");
    CHECK(u2_unlock_rule(1, 0x0C, 5) == U2_UNLOCK_NO_DOOR,
          "a non-door cell gives NO DOOR THERE!");
    CHECK(u2_unlock_rule(1, 0xA0, 0) == U2_UNLOCK_NO_KEYS,
          "a door with no keys gives NO KEYS THAT FIT!");
    CHECK(u2_unlock_rule(1, 0x0C, 0) == U2_UNLOCK_NO_DOOR,
          "no door AND no keys reports NO DOOR THERE!, because asm/433A tests the cell "
          "before asm/433E tests the keys -- this is the case that tells the two orders apart");
    CHECK(u2_unlock_rule(1, 0xA0, 5) == U2_UNLOCK_OPENED,
          "door plus a key opens");
    /* The byte written over the door: DS:14*2, and DS:14 is the tile under the player
     * >> 1. Two live reads pinned this (sector 32 on 0x08 -> DS:14 4; sector 20 on
     * 0x0C -> DS:14 6), so the door is paved with the ground you stand on. */
    CHECK(u2_unlock_replacement(0x08) == 0x08, "grass under foot paves the door with grass");
    CHECK(u2_unlock_replacement(0x0C) == 0x0C, "forest under foot paves the door with forest");
    CHECK(u2_unlock_replacement(0x09) == 0x08,
          "the low bit is cleared by the >>1<<1, so an odd ground byte rounds down");
    /* asm/435E spends one key in packed decimal. The stc/cmc pair only clears carry --
     * it is minus one, not minus two, and the BCD means 0x20 becomes 0x19 not 0x1F. */
    CHECK(u2_unlock_spend_key(0x20) == 0x19, "20 keys (packed decimal 0x20) spends to 19");
    CHECK(u2_unlock_spend_key(0x10) == 0x09, "10 keys spends to 09, borrowing across the nibble");
    CHECK(u2_unlock_spend_key(0x01) == 0x00, "the last key spends to zero");

    /* asm/44D4 (X-IT, the dismount). The rule is unit-tested rather than only end to end,
     * because the "-NOT HERE!" arm cannot be reached from a scripted foot start: it needs a
     * non-ship vehicle standing on water, which in play means a plane flown over the sea and
     * that is the ungated flight loop (G43). The bytes are proven; the route is not. */
    CHECK(u2_dismount_rule(0x78, 0x04) == U2_DISMOUNT_ON_FOOT,
          "on foot over grass is still ' WHAT?' — the on-foot test comes first");
    CHECK(u2_dismount_rule(0x7E, 0x04) == U2_DISMOUNT_ON_FOOT,
          "a non-fighter on foot is still on foot (0x7E >= 0x78)");
    CHECK(u2_dismount_rule(0x22, 0x04) == U2_DISMOUNT_OK,
          "horse over grass dismounts");
    CHECK(u2_dismount_rule(0x24, 0x00) == U2_DISMOUNT_OK,
          "ship over water dismounts — the ship's own clause");
    CHECK(u2_dismount_rule(0x22, 0x00) == U2_DISMOUNT_NOT_HERE,
          "horse over water is '-NOT HERE!': only the ship may be dropped afloat");
    CHECK(u2_dismount_rule(0x26, 0x00) == U2_DISMOUNT_NOT_HERE,
          "plane over water is '-NOT HERE!' too");
    CHECK(u2_dismount_rule(0x26, 0x06) == U2_DISMOUNT_NOT_HERE,
          "neither grass nor water (forest, mode_a 0x06) refuses the dismount");
    /* The tile put back is 2*[DS:13], the exact inverse of BOARD's shift right. */
    CHECK(u2_dismount_tile(0x22) == 0x44, "horse returns the 0x44 tile");
    CHECK(u2_dismount_tile(0x24) == 0x48, "ship returns the 0x48 tile");
    CHECK(u2_dismount_tile(0x26) == 0x4C, "plane returns the 0x4C tile");
    CHECK(u2_dismount_tile(0x28) == 0x50, "rocket returns the 0x50 tile");
    /* The on-foot stamp is class-dependent: 0x78 + 2*pclass (asm/44D4 and asm/080D agree).
     * Every capture shows 0x78 because every capture so far was a fighter. */
    CHECK(u2_foot_stamp(0) == 0x78, "FIGHTER dismounts to 0x78, the value every capture shows");
    CHECK(u2_foot_stamp(1) == 0x7A, "CLERIC dismounts to 0x7A");
    CHECK(u2_foot_stamp(2) == 0x7C, "WIZARD dismounts to 0x7C");
    CHECK(u2_foot_stamp(3) == 0x7E, "THIEF dismounts to 0x7E");
    /* And the restored stamp must still read as on foot to the guards that compare against
     * 0x78 — the Q on-foot gate and cmd_E both use >= 0x78, so a class-dependent value must
     * not be mistaken for a vehicle. */
    CHECK(u2_foot_stamp(3) >= 0x78, "0x7E still passes every 'on foot' comparison in the game");

    /* --- the signpost arm of cmd_E (asm/3389 -> asm/3525 -> asm/3546) -----------
     * Standing on a signpost (art 0x0A, tile 0x28) outdoors and pressing E prints
     * "-THE SIGN READS:" then the era label for the sector tens digit on the next
     * line. Captured on frame from the original 2026-11-12. site_for_tile leaves art
     * 10 as no-door, so this is u2_overworld_enter's own branch, not the site table.
     * The era dispatch is asm/3546's dec/je ladder on DS:49: 0 LEGENDS, 1 9M BC,
     * 2 1423 BC, 3 1990 AD, >=4 2112 AD. */
    {
        static const struct { uint8_t tens; const char *era; } eras[] = {
            { 0, "ANOS: LEGENDS!" },
            { 1, "ANOS: 9,000,000 B.C." },
            { 2, "ANOS: 1423 B.C." },
            { 3, "ANOS: 1990 A.D." },
            { 4, "ANOS: 2112 A.D." },
            { 9, "ANOS: 2112 A.D." },   /* >=4 fall-through: the off-Earth world index */
        };
        for (unsigned i = 0; i < sizeof eras / sizeof eras[0]; i++) {
            u2_ctx c; u2_overworld ow;
            memset(&c, 0, sizeof c); memset(&ow, 0, sizeof ow);
            u2_vram_init(&c.vram);
            u2_state_init(&c.state);
            c.state.sector_first = eras[i].tens;
            c.state.sector_second = 0;         /* outdoors */
            c.state.player_stamp = 0x78;       /* on foot */
            c.state.sector_x = 10; c.state.sector_y = 10;
            ow.world.map_loaded = 1;
            ow.world.map[10 * U2_SECTOR_EDGE + 10] = 0x28;   /* the signpost */
            u2_overworld_enter(&c, &ow);
            CHECK(grid_has(&c, "-THE SIGN READS:"),
                  "signpost at era %u prints -THE SIGN READS:", eras[i].tens);
            CHECK(grid_has(&c, eras[i].era),
                  "signpost at DS:49=%u prints the era label %s", eras[i].tens, eras[i].era);
        }
        /* CONTROL: a non-signpost tile (grass) does NOT print the sign — it takes the
           no-door " WHAT?" path. Without this the branch could fire on every tile. */
        u2_ctx c; u2_overworld ow;
        memset(&c, 0, sizeof c); memset(&ow, 0, sizeof ow);
        u2_vram_init(&c.vram); u2_state_init(&c.state);
        c.state.sector_second = 0; c.state.player_stamp = 0x78;
        c.state.sector_x = 10; c.state.sector_y = 10; ow.world.map_loaded = 1;
        ow.world.map[10 * U2_SECTOR_EDGE + 10] = 0x08;   /* grass, not a signpost */
        u2_overworld_enter(&c, &ow);
        CHECK(!grid_has(&c, "-THE SIGN READS:"),
              "control: grass under foot is not a signpost, no sign is read");
    }

    /* --- Minax's teleport on a surviving hit (asm/2C2D -> asm/2C38) ----------------
     * Proven live 2026-11-12: seeded adjacent to Minax (slot 31, type 0x40) in sector 03,
     * one QUICK SWORD hit took her 255->199 and moved slot 31 from (60,3) to (3,60).
     * asm/2C38 SWAPS mon_x and mon_y (so the NE spawn becomes the SW, and a second hit
     * swaps back -- the NE<->SW oscillation), sets mon_role to 0x84, and prints
     * "SHE'S GONE!!!". The type-0x40 gate itself is asm/2C2D, enforced by the melee and
     * cannon callers (r==1 && mon_type==0x40); this exercises the mechanism directly. */
    {
        u2_ctx c; u2_world w;
        memset(&c, 0, sizeof c); memset(&w, 0, sizeof w);
        u2_vram_init(&c.vram); u2_state_init(&c.state);
        w.map_loaded = 1;
        const int slot = 31;
        c.state.mon_type[slot] = 0x40;
        c.state.mon_x[slot] = 60; c.state.mon_y[slot] = 3;
        c.state.mon_role[slot] = 0x84;
        u2_minax_teleport(&c, &w, slot);
        CHECK(c.state.mon_x[slot] == 3 && c.state.mon_y[slot] == 60,
              "teleport swaps x,y: (60,3) -> (3,60), the NE->SW move");
        CHECK(c.state.mon_role[slot] == 0x84,
              "teleport sets mon_role to 0x84 (asm/2C38 file 0x2e89)");
        CHECK(grid_has(&c, "SHE'S GONE!!!"),
              "teleport prints SHE'S GONE!!! (asm/2C38 file 0x2e93)");
        /* The swap is its own inverse: a second hit returns her to the NE corner. This is
           what makes the behaviour an oscillation between two chambers, not a one-way move,
           and it is the property the owner's "teleports between NE and SW" describes. */
        u2_minax_teleport(&c, &w, slot);
        CHECK(c.state.mon_x[slot] == 60 && c.state.mon_y[slot] == 3,
              "second teleport swaps back to (60,3): the NE<->SW oscillation");
    }

    printf("site_check: %d checks, %d failures%s\n", checks, fails,
           skipped ? " (data cross-check skipped)" : "");
    return fails != 0;
}

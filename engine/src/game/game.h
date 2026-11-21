#ifndef U2_GAME_H
#define U2_GAME_H

/*
 * Game driver.
 *
 * CP0 ships the loop only: init, one frame per iteration, clean shutdown.
 * The title/menu flow lands in CP1 (game/title.c), character creation in
 * CP3, the overworld in CP4, and so on — each as its own module citing the
 * asm functions it implements.
 */

#include "core/ctx.h"

int  u2_game_init(u2_ctx *ctx);
void u2_game_run(u2_ctx *ctx);
void u2_game_shutdown(u2_ctx *ctx);

/* The death handoff: what happens between a world loop returning with the player
 * dead and either a new world drawn or the run ending.
 *
 * Two entries need it — `u2_game_run`'s world loop and `main.c`'s `--play-map`
 * loop — and they used to each carry their own copy of the ordering, with the
 * same warning comment written in both. That is the shape this exists to stop:
 * an ordering contract maintained twice is one that gets fixed once.
 *
 * The verdict, and nothing else. Where to go on U2_HANDOFF_NO_SAVE is the
 * caller's business — the driver falls back to the menu, the development entry
 * exits — so it is not decided here.
 */
typedef enum {
    U2_HANDOFF_END,      /* no death, or the run ends: --quit-on-death, or a spent script */
    U2_HANDOFF_REENTER,  /* the record is restored and the screen cleared: draw a new world */
    U2_HANDOFF_NO_SAVE   /* dead, but the record could not be read; caller picks the fallback */
} u2_handoff_t;

u2_handoff_t u2_game_death_handoff(u2_ctx *ctx);

#endif /* U2_GAME_H */

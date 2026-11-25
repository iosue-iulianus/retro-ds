/* Castlevania: Symphony of the Night (USA, SLUS-00067): what RetroDS reads
 * from the game (docs/games/symphony-of-the-night.md). Its addresses are
 * the PlayStation's (0x80000000 up), its pictures come from the game's
 * video RAM (SwanStation with RetroDS's patch: 1024 x 512 halfwords). */
#ifndef RETRODS_SOTN_H
#define RETRODS_SOTN_H

#include <stdbool.h>
#include <stdint.h>

#include "emu.h"

#define SOTN_CODE "SLUS-00067"

bool sotn_is(const char* gameCode);

/* SOTN-1: the title's logo, while the title screen is up (and its picture
 * and palette are the ones measured): RGBA, its clear pixels transparent,
 * *w x *h; in the title screen's 512-wide pixels, each line twice (so it
 * shows as the game's picture does). malloc'd; NULL if the title isn't up. */
uint8_t* sotn_title_logo(Emu* e, int* w, int* h);

#endif

/* A Link to the Past's dungeon map, drawn by SNES DS (alttp_dungeon.c;
 * docs/games/a-link-to-the-past.md, ALTTP-5 and ALTTP-6). Used by alttp.c. */
#ifndef SNESDS_ALTTP_DUNGEON_H
#define SNESDS_ALTTP_DUNGEON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct AltDungeon AltDungeon;

/* The floors as the game's map opens on them (Link's floor at the top) */
#define ALTDUN_AS_GAME (-128)

/* rom: the game as played, kept by the caller while this lives. Makes the
 * map's BG tiles (decompressed from the cartridge) once. */
AltDungeon* altdun_new(const uint8_t* rom, size_t size);
void altdun_free(AltDungeon* d);

/* The dungeon map of the dungeon Link is in (false outside dungeons), as
 * the game's own map screen shows it, 256 x 224 (0xAABBGGRR), from the
 * game's work RAM (128 KB) and video RAM (64 KB: the HUD's tiles). view:
 * ALTDUN_AS_GAME, or the floor to show at the top (0 1F, 1 2F, -1 B1...);
 * *out: the picture, valid until the next call. */
bool altdun_draw(AltDungeon* d, const uint8_t* ram, const uint8_t* vram, int view, uint32_t** out);
/* The dungeon's floors (lowest and highest: -1 B1, 0 1F...) and Link's */
bool altdun_floors(const AltDungeon* d, const uint8_t* ram, int* lowest, int* highest, int* link);
/* The floor whose number is under (x, y) on the floor list, else ALTDUN_AS_GAME */
int altdun_floor_at(const AltDungeon* d, const uint8_t* ram, int x, int y);

#endif

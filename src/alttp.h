/* A Link to the Past's second screen, made by SNES DS
 * (docs/games/a-link-to-the-past.md, ALTTP-1 and on):
 * - the items screen (ALTTP-10): the game's START screen built from the
 *   cartridge's tables and Link's items, its panes moved for touch (the Y
 *   items at the bottom right), drawn with the HUD's tiles; a tap selects
 *   an item as the game's own screen would;
 * - the world map (ALTTP-2): the ground the game draws (BG2 in video RAM,
 *   in its palette buffer's colours) kept as the camera passes over it, per
 *   world, the rest under the world map's clouds; saved per file.
 *
 * Pictures are 0xAABBGGRR (SDL_PIXELFORMAT_ABGR8888). */
#ifndef SNESDS_ALTTP_H
#define SNESDS_ALTTP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ALTTP_W 256
#define ALTTP_H 224
#define ALTTP_WORLD 4096 /* each world is 4096 x 4096 pixels */

typedef struct Alttp Alttp;

/* rom: the game as played (no copier header), taken over (freed by
 * alttp_free). NULL if it isn't the ROM this knows (USA, 777AAC2F). */
Alttp* alttp_new(uint8_t* rom, size_t size);
void alttp_free(Alttp* a);

/* What changed in alttp_update */
enum { ALTTP_ITEMS = 1, ALTTP_MAP = 2, ALTTP_AREA = 4, ALTTP_FILE = 8 };
/* Once per pass of the loop, with the game's work RAM (128 KB), its video
 * RAM (64 KB, may be NULL: then nothing is taken for the map) and its
 * cartridge RAM (8 KB, may be NULL; its $1FFE says which file is played).
 * Takes the ground on screen when Link is outdoors and playable (ALTTP-2,
 * ALTTP-8) and builds the items screen again when it changed. */
int alttp_update(Alttp* a, const uint8_t* wram, const uint8_t* vram, const uint8_t* sram);

/* Link is played: walking about indoors or out, the screen up (X is the
 * second screen's then, ALTTP-1; a tap may select an item, ALTTP-10) */
bool alttp_playing(const Alttp* a);
/* The title, the file screens, the attract mode (ALTTP-9: the logo) */
bool alttp_in_menu(const Alttp* a);
/* Something to show: the game has been played since it was loaded */
bool alttp_ready(const Alttp* a);

/* The items screen, ALTTP_W x ALTTP_H */
const uint32_t* alttp_items(const Alttp* a);
/* The Y item (1 bow ... 20 mirror) Link has under a tap at (x, y) on the
 * items screen, or 0 */
int alttp_items_hit(const Alttp* a, int x, int y);

/* One byte to write to the game's work RAM (an offset in its 128 KB) */
typedef struct {
	uint32_t addr;
	uint8_t val;
} AltWrite;
/* Selecting Y item `item` as the game's own items screen leaves it when
 * it closes (ItemMenu_Close, $0D:DFBA): the item, the one Y uses, the
 * HUD's item box and its upload. Fills out (at most max) and returns how
 * many; 0 if Link doesn't have it or isn't playable. To be written
 * between two frames. */
int alttp_select(const Alttp* a, const uint8_t* wram, int item, AltWrite* out, int max);

/* The world map. Which world Link is in (0 Light, 1 Dark) and where, in
 * world pixels (the last place outdoors while he's indoors) */
void alttp_link(const Alttp* a, int* world, int* x, int* y);
/* The map of `world` at `zoom` (1, 2 or 4 world pixels a shown pixel):
 * w x h shown pixels from world pixel (vx, vy), the seen ground and the
 * clouds elsewhere, into out (w x h) */
void alttp_compose(const Alttp* a, int world, int zoom, int vx, int vy, int w, int h, uint32_t* out);
/* Link's marker: the items screen's flashing ring (32 x 32, its middle at
 * the marker's place), and whether it shows now (it blinks as the ring) */
const uint32_t* alttp_marker(const Alttp* a);
bool alttp_marker_on(const Alttp* a);

/* The seen record (ALTTP-2) for each of the game's files, in SNES DS's own
 * file beside the battery save. alttp_update says ALTTP_FILE when Link is
 * played from another file than before (alttp_file: 1 to 3, from the
 * cartridge RAM's $1FFE): the last file's record is saved, then this
 * one's loaded, or cleared if the player has just named a new file
 * (alttp_take_new_file, once). alttp_dirty: changed since load or save. */
int alttp_file(const Alttp* a);
bool alttp_take_new_file(Alttp* a);
void alttp_clear_seen(Alttp* a);
bool alttp_load_seen(Alttp* a, const char* path);
bool alttp_save_seen(Alttp* a, const char* path);
bool alttp_dirty(const Alttp* a);
/* The record packed for a save state (malloc'd, the file's format), and a
 * state's record merged into the file's: what it had seen added, nothing
 * taken away (ALTTP-2: seen parts stay seen) */
void* alttp_seen_pack(const Alttp* a, size_t* n);
bool alttp_seen_merge(Alttp* a, const void* data, size_t n);

/* The dungeon map (ALTTP-5), as the game's own map screen shows it,
 * ALTTP_W x ALTTP_H, from the cartridge, the RAM of the last update and the
 * game's video RAM now (the HUD's tiles); NULL outside dungeons. view:
 * ALTTP_FLOOR_AS_GAME (Link's floor at the top, as the game opens it) or
 * the floor to show at the top (0 1F, 1 2F, -1 B1...: ALTTP-6). Valid
 * until the next call. */
#define ALTTP_FLOOR_AS_GAME (-128)
bool alttp_in_dungeon(const Alttp* a);
const uint32_t* alttp_dungeon_map(Alttp* a, const uint8_t* vram, int view);
/* The dungeon's floors, lowest and highest, and Link's */
bool alttp_dungeon_floors(const Alttp* a, int* lowest, int* highest, int* link);
/* The floor whose number is under (x, y) on the map's floor list, or
 * ALTTP_FLOOR_AS_GAME */
int alttp_dungeon_floor_at(const Alttp* a, int x, int y);

/* Tests: take the ground on screen now, whatever the game is doing (the
 * harness's dumps were taken while playing) */
void alttp_capture_now(Alttp* a, const uint8_t* wram, const uint8_t* vram);

#endif

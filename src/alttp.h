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

#include "writes.h"

#define ALTTP_W 256
#define ALTTP_H 224
#define ALTTP_WORLD 4096 /* each world is 4096 x 4096 pixels */
#define ALTTP_PIECE 512 /* the map is kept in pieces of 512 x 512 world pixels */
#define ALTTP_PIECES (ALTTP_WORLD / ALTTP_PIECE) /* a row of them */
#define ALTTP_SKY_W 1024 /* the clouds' picture (the fog), repeated over the world */
#define ALTTP_SKY_H 1024

typedef struct Alttp Alttp;

/* rom: the game as played (no copier header), taken over (freed by
 * alttp_free). NULL if it isn't the ROM this knows (USA, 777AAC2F). */
Alttp* alttp_new(uint8_t* rom, size_t size);
void alttp_free(Alttp* a);

/* What changed in alttp_update */
enum { ALTTP_ITEMS = 1, ALTTP_MAP = 2, ALTTP_AREA = 4, ALTTP_FILE = 8, ALTTP_MARKER = 16, ALTTP_GOALS = 32, ALTTP_FILES = 64 };
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

/* What alttp_select may write (ALTTP-10; docs/games/a-link-to-the-past.md,
 * "Selecting an item"), for emu_apply */
const WriteRange* alttp_write_ranges(int* n);
/* Selecting Y item `item` as the game's own items screen leaves it when
 * it closes (ItemMenu_Close, $0D:DFBA): the item, the one Y uses, the
 * HUD's item box and its upload. Fills out (at most max) and returns how
 * many; 0 if Link doesn't have it or isn't playable. To be written
 * between two frames. */
int alttp_select(const Alttp* a, const uint8_t* wram, int item, Write* out, int max);

/* The world map. Which world Link is in (0 Light, 1 Dark) and where, in
 * world pixels (the last place outdoors while he's indoors) */
void alttp_link(const Alttp* a, int* world, int* x, int* y);
/* The map of `world` at `zoom` (1, 2 or 4 world pixels a shown pixel):
 * w x h shown pixels from world pixel (vx, vy), the seen ground and the
 * clouds elsewhere, into out (w x h) */
void alttp_compose(const Alttp* a, int world, int zoom, int vx, int vy, int w, int h, uint32_t* out);
/* Whether world pixel (x, y) of `world` has been seen (its 16 x 16 square) */
bool alttp_seen_at(const Alttp* a, int world, int x, int y);
/* The map in pieces, for drawing it on the GPU: piece i (row by row,
 * ALTTP_PIECES a row) of `world` at level L (each pixel 2^L x 2^L world
 * pixels, averaged), (ALTTP_PIECE >> L) squared pixels into out; what
 * hasn't been seen transparent (partly, where a pixel was partly seen),
 * the fog's edge over the ground by it (the clouds' puffs) and fading out
 * from it.
 * alttp_piece_version: changes whenever the piece does; 0 while there's
 * nothing in it. alttp_sky: the clouds shown where nothing is seen,
 * ALTTP_SKY_W x ALTTP_SKY_H, in the world's own colours. */
uint32_t alttp_piece_version(const Alttp* a, int world, int i);
void alttp_piece(const Alttp* a, int world, int i, int level, uint32_t* out);
/* The same in two steps, to make it on another thread: alttp_piece_job
 * copies what the piece is made from (NULL: no memory; free() it), then
 * alttp_piece_make makes it from the copy alone (any thread). */
typedef struct AltPieceJob AltPieceJob;
AltPieceJob* alttp_piece_job(const Alttp* a, int world, int i);
void alttp_piece_make(const AltPieceJob* j, int level, uint32_t* out);
const uint32_t* alttp_sky(const Alttp* a, int world);
/* The map's zoom indicator's pane (ALTTP-3): a frame of the items
 * screen's, black inside, ALTTP_ZOOM_PANE_W x _H; made again with the
 * items screen (ALTTP_ITEMS) */
#define ALTTP_ZOOM_PANE_W 32
#define ALTTP_ZOOM_PANE_H 80
const uint32_t* alttp_zoom_pane(const Alttp* a);

/* The world map's goal markers (ALTTP-11), as the game's own map would
 * show them now in the world Link is in (blinking as there): each at
 * world pixel (x, y), its picture (size x size, the top left of a 16 x 16
 * place) drawn from (dx, dy) of its own pixels from there; picture i is
 * at (16 i, 0) of alttp_goal_pictures (ALTTP_GOAL_MAX * 16 x 16).
 * alttp_update says ALTTP_GOALS when they changed. */
#define ALTTP_GOAL_MAX 8 /* the 7 goal markers and the mirror's portal */
typedef struct {
	int x, y, dx, dy, size;
} AltGoal;
int alttp_goals(const Alttp* a, const AltGoal** list);
const uint32_t* alttp_goal_pictures(const Alttp* a);
/* Link's marker (32 x 32, its middle at Link's place): his sprite as the
 * game draws it (ALTTP-4: facing and moving as he is; alttp_update says
 * ALTTP_MARKER when it changed), or the items screen's flashing ring until
 * he has been seen; and whether it shows now (the ring blinks) */
const uint32_t* alttp_marker(const Alttp* a);
bool alttp_marker_on(const Alttp* a);

/* The seen record (ALTTP-2) for each of the game's files, in SNES DS's own
 * file beside the battery save. alttp_update says ALTTP_FILE when Link is
 * played from another file than before (alttp_file: 1 to 3, from the
 * cartridge RAM's $1FFE): the last file's record is saved, then this
 * one's loaded, or cleared if the player has just named a new file
 * (alttp_take_new_file, once). alttp_dirty: changed since load or save. */
int alttp_file(const Alttp* a);
/* The game's files erased or copied on its file screen since last asked
 * (alttp_update says ALTTP_FILES): the seen record goes with its file */
enum { ALTTP_FILE_ERASED = 1, ALTTP_FILE_COPIED = 2 };
typedef struct {
	int kind, file, from; /* files 1 to 3; from: a copy's source */
} AltSlotEvent;
int alttp_take_file_events(Alttp* a, AltSlotEvent* out, int max);
bool alttp_take_new_file(Alttp* a);
void alttp_clear_seen(Alttp* a);
bool alttp_load_seen(Alttp* a, const char* path);
bool alttp_save_seen(Alttp* a, const char* path);
bool alttp_dirty(const Alttp* a);
/* The record packed for a save state (malloc'd, the file's format), and a
 * state's record merged into the file's: what it had seen added, nothing
 * taken away (ALTTP-2: seen parts stay seen) */
void* alttp_seen_pack(Alttp* a, size_t* n);
/* Saving without a stall: a piece whose kept compressed copy is out of
 * date, its ground (ALTTP_PIECE squared words) copied into out, its
 * version in *ver (false if none); and its copy compressed (zlib, from
 * that ground, on any thread), handed back (z taken; dropped if the piece
 * has changed since). A save then has little left to compress. */
bool alttp_stale_piece(const Alttp* a, int* world, int* piece, uint16_t* out, uint32_t* ver);
void alttp_put_compressed(Alttp* a, int world, int piece, uint32_t ver, uint8_t* z, uint32_t n);
bool alttp_seen_merge(Alttp* a, const void* data, size_t n);

/* The dungeon map (ALTTP-5), as the game's own map screen shows it,
 * ALTTP_W x ALTTP_H, from the cartridge, the RAM of the last update and the
 * game's video RAM now (the HUD's tiles); NULL outside dungeons. view:
 * ALTTP_FLOOR_AS_GAME (Link's floor at the top, as the game opens it) or
 * the floor to show at the top (0 1F, 1 2F, -1 B1...: ALTTP-6). Valid
 * until the next call. */
#define ALTTP_FLOOR_AS_GAME (-128)
bool alttp_in_dungeon(const Alttp* a);
/* Link is in a special area of the overworld (area $80 and up: the Master
 * Sword's grove, Zora's waterfall...), which the world map doesn't show */
bool alttp_in_special(const Alttp* a);
/* The game over screen (module $12): it takes the video memory the maps
 * are drawn from */
bool alttp_game_over(const Alttp* a);
/* ALTTP-14: the game's title (module 0), and the y of its sword's top on
 * the game's screen (SNES pixels, its picture from logo_drop_path: 48
 * pixels from x 48), where it is or, before it starts down, would be
 * (negative: above the screen); false while there's no sword to show */
bool alttp_title(const Alttp* a);
bool alttp_title_sword(const Alttp* a, int* y);
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

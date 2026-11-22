/* A Link to the Past's second screen (alttp.h). Addresses, formats and how
 * each was checked: docs/games/a-link-to-the-past.md; the Python
 * references, tools/alttp_items.py and tools/alttp_world.py, do the same
 * and are checked against the game's own screens. */
#include "alttp.h"
#include "alttp_dungeon.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define ROM_CRC 0x777AAC2Fu

/* work RAM (offsets in its 128 KB) */
#define R_MODULE 0x0010
#define R_SUBMODULE 0x0011
#define R_INIDISP 0x0013
#define R_FRAME 0x001A
#define R_INDOORS 0x001B
#define R_TM 0x001C
#define R_LINK_Y 0x0020
#define R_LINK_X 0x0022
#define R_AREA 0x008A
#define R_BG2_X 0x00E2
#define R_BG2_Y 0x00E8
#define R_MENU_CUSTOM 0x0112 /* a spell's animation (START not allowed) */
#define R_CUR_ITEM 0x0202
#define R_RING 0x0207
#define R_DEBUG_1 0x020B
#define R_IMMOBILE 0x02E4
#define R_EQUIP 0x0303
#define R_EQ_DEBUG 0x034B
#define R_DUNGEON 0x040C
#define R_HUD_UPLOAD 0x0016
#define R_BLOCK_MENU 0x0FFC
#define R_PALETTE 0xC500
#define R_HUD 0xC700
#define R_ITEMS 0xF340 /* the items, bow first ($7EF340...) */
#define R_BOTTLE 0xF34F
#define R_BOTTLES 0xF35C
#define R_COMPASS 0xF364
#define R_BIG_KEY 0xF366
#define R_DUNGEON_MAP 0xF368
#define R_HEART_PIECES 0xF36B
#define R_PENDANTS 0xF374
#define R_ABILITIES 0xF379
#define R_CRYSTALS 0xF37A
#define R_PROGRESS 0xF3C5

/* video RAM, word addresses, as the game sets the PPU for play */
#define V_BG2_MAP 0x0000
#define V_BG1_MAP 0x1000
#define V_BG12_CHR 0x2000
#define V_BG3_CHR 0x7000

/* the cartridge (SNES addresses) */
#define ITEM_ICON_POINTERS 0x0DFA93u
#define MENU_CURSOR_POSITIONS 0x0DFAD5u
#define MENU_TO_EQUIP 0x0DFA15u
#define NAME_YITEMS 0x0DF1C9u
#define NAME_BOTTLES 0x0DF449u
#define NAME_POWDER 0x0DF549u
#define NAME_FLUTE 0x0DF569u
#define NAME_MIRROR 0x0DF5A9u
#define NAME_BOW 0x0DF5C9u
#define PENDANTS_WINDOW 0x0DE860u
#define CRYSTALS_WINDOW 0x0DE914u
#define LIFT_TEXT 0x0DF931u
#define ABILITY_TEXT 0x0DF959u
#define HEART_PIECES 0x0DF911u
#define EQUIPMENT_TEXT 0x0DED09u
#define DUNGEON_ITEM_TEXT 0x0DED19u
#define MAP_TILES 0x18C000u
#define MAP_TILEMAP 0x0AC727u
#define MAP_PALETTE 0x0ADB27u /* 128 Light World colours, then 128 Dark World */

#define BLANK 0x207F
#define FILL 0x24F5
#define MENU_ROW0 3 /* the items screen's first row shown */

#define CELL 16 /* what counts as seen: 16 x 16 world pixels, whole */
#define CELLS (ALTTP_WORLD / CELL)
#define CHUNK 512 /* the ground is kept in 512 x 512 pieces (an area) */
#define CHUNKS (ALTTP_WORLD / CHUNK)
#define SKY_W 512
#define SKY_H 80

enum { P_ITEMS, P_NAME, P_PROGRESS, P_ABILITY, P_EQUIPMENT, P_COUNT };
/* each pane's place on the game's own screen, and on SNES DS's (ALTTP-10:
 * the Y items at the bottom right, every pane moved whole) */
static const int8_t paneGame[P_COUNT][2] = { { 1, 5 }, { 21, 5 }, { 21, 11 }, { 1, 21 }, { 21, 21 } };
static const int8_t paneTouch[P_COUNT][2] = { { 12, 15 }, { 1, 24 }, { 1, 15 }, { 12, 5 }, { 1, 5 } };

struct Alttp {
	uint8_t* rom;
	size_t romSize;
	AltDungeon* dun; /* the dungeon map (alttp_dungeon.c) */
	uint8_t ram[0x20000]; /* the work RAM of the last update */
	bool have;            /* ram holds the game's */
	bool played;          /* Link has been played since the game was loaded */
	bool namingSeen;      /* a new file was named: its record starts empty */
	/* the items screen */
	uint16_t menu[32 * 32];
	uint32_t items[ALTTP_W * ALTTP_H];
	uint32_t itemsKey;
	uint32_t marker[32 * 32];
	/* the map */
	uint16_t* chunk[2][CHUNKS * CHUNKS]; /* SNES colour | 0x8000 where taken, 0 elsewhere */
	uint8_t seen[2][CELLS * CELLS / 8];
	uint32_t sky[2][SKY_W * SKY_H];
	int linkWorld, linkX, linkY;
	int lastArea;
	int takenX, takenY, takenWorld, takenAge; /* the last screen taken, and passes since */
	int file; /* 1 to 3, 0 not known */
	bool dirty;
};

/* ---- the cartridge */

static uint32_t lorom(uint32_t a) {
	return ((a >> 16) & 0x7F) * 0x8000 + (a & 0x7FFF);
}

static uint16_t rom16(const Alttp* a, uint32_t addr) {
	uint32_t o = lorom(addr);
	return o + 1 < a->romSize ? (uint16_t) (a->rom[o] | a->rom[o + 1] << 8) : 0;
}

static uint8_t rom8(const Alttp* a, uint32_t addr) {
	uint32_t o = lorom(addr);
	return o < a->romSize ? a->rom[o] : 0;
}

static uint32_t crc32_of(const uint8_t* p, size_t n) {
	return (uint32_t) crc32(0, p, (uInt) n);
}

static uint32_t abgr(uint16_t c) {
	uint32_t r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
	r = r << 3 | r >> 2, g = g << 3 | g >> 2, b = b << 3 | b >> 2;
	return 0xFF000000u | b << 16 | g << 8 | r;
}

/* the world map's top 10 tile rows: the clouds, in each world's colours */
static void make_sky(Alttp* a) {
	for (int w = 0; w < 2; ++w) {
		for (int ty = 0; ty < SKY_H / 8; ++ty) {
			for (int tx = 0; tx < SKY_W / 8; ++tx) {
				int q = tx >= 32, c = tx & 31;
				uint8_t ch = rom8(a, MAP_TILEMAP + q * 1024 + ty * 32 + c);
				for (int y = 0; y < 8; ++y) {
					for (int x = 0; x < 8; ++x) {
						uint8_t i = rom8(a, MAP_TILES + ch * 64 + y * 8 + x) & 0x7F;
						a->sky[w][(ty * 8 + y) * SKY_W + tx * 8 + x] = abgr(rom16(a, MAP_PALETTE + w * 256 + i * 2));
					}
				}
			}
		}
	}
}

Alttp* alttp_new(uint8_t* rom, size_t size) {
	if (!rom || size < 0x100000 || crc32_of(rom, size) != ROM_CRC) {
		free(rom);
		return NULL;
	}
	Alttp* a = calloc(1, sizeof *a);
	if (!a) {
		free(rom);
		return NULL;
	}
	a->rom = rom, a->romSize = size;
	a->dun = altdun_new(rom, size);
	a->lastArea = -1;
	a->takenX = -1;
	a->itemsKey = 1;
	make_sky(a);
	for (size_t i = 0; i < ALTTP_W * ALTTP_H; ++i) a->items[i] = 0xFF000000u;
	return a;
}

static void clear_seen(Alttp* a) {
	for (int w = 0; w < 2; ++w) {
		for (int i = 0; i < CHUNKS * CHUNKS; ++i) {
			free(a->chunk[w][i]);
			a->chunk[w][i] = NULL;
		}
	}
	memset(a->seen, 0, sizeof a->seen);
}

void alttp_free(Alttp* a) {
	if (!a) return;
	clear_seen(a);
	altdun_free(a->dun);
	free(a->rom);
	free(a);
}

/* ---- the game's state */

static uint8_t r8(const Alttp* a, uint32_t o) {
	return a->ram[o & 0x1FFFF];
}

static uint16_t r16(const Alttp* a, uint32_t o) {
	return (uint16_t) (r8(a, o) | r8(a, o + 1) << 8);
}

bool alttp_playing(const Alttp* a) {
	uint8_t m = r8(a, R_MODULE);
	return a->have && (m == 0x07 || m == 0x09 || m == 0x0B) && r8(a, R_SUBMODULE) == 0;
}

bool alttp_in_menu(const Alttp* a) {
	uint8_t m = r8(a, R_MODULE);
	return !a->have || m <= 0x05 || m == 0x14;
}

bool alttp_ready(const Alttp* a) {
	return a->played;
}

/* outdoors in a world's own areas, playable, the screen at full brightness */
static bool outdoors_still(const Alttp* a) {
	uint8_t m = r8(a, R_MODULE);
	return (m == 0x09 || m == 0x0B) && r8(a, R_SUBMODULE) == 0 && r8(a, R_INDOORS) == 0 && r8(a, R_INIDISP) == 0x0F && r8(a, R_AREA) < 0x80;
}

/* ---- the items screen (ALTTP-10; tools/alttp_items.py) */

typedef struct {
	uint16_t* out;
	int dx, dy;
} Pane;

static Pane pane(Alttp* a, int p) {
	return (Pane) { a->menu, paneTouch[p][0] - paneGame[p][0], paneTouch[p][1] - paneGame[p][1] };
}

static void put(Pane* p, int c, int r, uint16_t w) {
	c += p->dx, r += p->dy;
	if (c >= 0 && c < 32 && r >= 0 && r < 32) p->out[r * 32 + c] = w;
}

static void icon(Pane* p, int c, int r, const uint16_t four[4]) {
	put(p, c, r, four[0]);
	put(p, c + 1, r, four[1]);
	put(p, c, r + 1, four[2]);
	put(p, c + 1, r + 1, four[3]);
}

static void rom_icon(const Alttp* a, Pane* p, int c, int r, uint32_t addr) {
	uint16_t w[4];
	for (int i = 0; i < 4; ++i) w[i] = rom16(a, addr + i * 2);
	icon(p, c, r, w);
}

static void rom_block(const Alttp* a, Pane* p, int c, int r, uint32_t addr, int w, int h) {
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) put(p, c + x, r + y, rom16(a, addr + (y * w + x) * 2));
	}
}

/* the game's frame: corners $FB, sides $FC, top and bottom $F9 */
static void box(Pane* p, int c0, int r0, int c1, int r1, int pal) {
	uint16_t q = (uint16_t) (pal << 10);
	put(p, c0, r0, 0x20FB | q);
	put(p, c1, r0, 0x60FB | q);
	put(p, c0, r1, 0xA0FB | q);
	put(p, c1, r1, 0xE0FB | q);
	for (int r = r0 + 1; r < r1; ++r) put(p, c0, r, 0x20FC | q), put(p, c1, r, 0x60FC | q);
	for (int c = c0 + 1; c < c1; ++c) put(p, c, r0, 0x20F9 | q), put(p, c, r1, 0xA0F9 | q);
	for (int r = r0 + 1; r < r1; ++r) {
		for (int c = c0 + 1; c < c1; ++c) put(p, c, r, FILL);
	}
}

/* the icon table of item kind k (1 bow ... 20 mirror, 21 gloves ... 28 mail) */
static uint32_t icon_table(const Alttp* a, int k) {
	return 0x0D0000u | rom16(a, ITEM_ICON_POINTERS + (k - 1) * 2);
}

/* Y item `item`'s level as its icon shows it */
static int item_level(const Alttp* a, int item) {
	int v = r8(a, R_ITEMS + item - 1);
	if (item == 4) return v != 0; /* bombs: any */
	if (item == 16) {             /* the bottle: the one selected */
		int i = r8(a, R_BOTTLE);
		return i ? r8(a, R_BOTTLES + i - 1) : 0;
	}
	return v;
}

static uint32_t item_icon(const Alttp* a, int item) {
	return icon_table(a, item) + item_level(a, item) * 8;
}

static void slot_place(const Alttp* a, int i, int* c, int* r) {
	int o = (rom16(a, MENU_CURSOR_POSITIONS + i * 2) - 0x1000) / 2;
	*c = o % 32, *r = o / 32;
}

static uint32_t name_text(const Alttp* a, int cur) {
	if (cur == 0) return NAME_YITEMS + 19 * 32;
	if (cur == 16) {
		int i = r8(a, R_BOTTLE), content = i ? r8(a, R_BOTTLES + i - 1) : 0;
		return NAME_BOTTLES + (content - 1) * 32;
	}
	static const struct {
		int item;
		uint32_t ram, table;
	} special[] = { { 5, 0xF344, NAME_POWDER }, { 20, 0xF353, NAME_MIRROR }, { 13, 0xF34C, NAME_FLUTE }, { 1, 0xF340, NAME_BOW } };
	for (size_t k = 0; k < sizeof special / sizeof special[0]; ++k) {
		if (special[k].item != cur) continue;
		int v = r8(a, special[k].ram);
		if (v != 1) return special[k].table + (v - 2) * 32;
	}
	return NAME_YITEMS + (cur - 1) * 32;
}

static void ring(Pane* p, int c, int r) {
	const uint16_t q = 7 << 10;
	put(p, c, r - 1, q | 0x2061), put(p, c + 1, r - 1, q | 0x6061);
	put(p, c - 1, r, q | 0x2070), put(p, c + 2, r, q | 0x6070);
	put(p, c - 1, r + 1, q | 0xA070), put(p, c + 2, r + 1, q | 0xE070);
	put(p, c, r + 2, q | 0xA061), put(p, c + 1, r + 2, q | 0xE061);
	put(p, c - 1, r - 1, q | 0x2060), put(p, c + 2, r - 1, q | 0x6060);
	put(p, c + 2, r + 2, q | 0xE060), put(p, c - 1, r + 2, q | 0xA060);
}

static bool have_any(const Alttp* a) {
	for (int i = 0; i < 20; ++i) {
		if (r8(a, R_ITEMS + i)) return true;
	}
	return false;
}

static void build_menu(Alttp* a, bool ringOn) {
	for (int i = 0; i < 32 * 32; ++i) a->menu[i] = BLANK;
	int cur = r8(a, R_CUR_ITEM);

	/* the Y items (DrawYItems, $0D:E3D9) */
	Pane p = pane(a, P_ITEMS);
	box(&p, 1, 5, 19, 19, have_any(a) ? 7 : 0);
	put(&p, 2, 6, 0x3CF0), put(&p, 2, 7, 0x3CF1);
	put(&p, 3, 5, 0x246E), put(&p, 4, 5, 0x246F);
	for (int i = 0; i < 20; ++i) {
		int c, r;
		slot_place(a, i, &c, &r);
		rom_icon(a, &p, c, r, item_icon(a, i + 1));
	}

	/* the selected item and its name (DrawEquippedYItem, $0D:EB3A) */
	Pane n = pane(a, P_NAME);
	box(&n, 21, 5, 30, 10, 7);
	if (cur >= 1 && cur <= 20) {
		int c, r;
		slot_place(a, cur - 1, &c, &r);
		rom_icon(a, &n, 25, 6, item_icon(a, cur));
		if (ringOn) ring(&p, c, r);
	}
	rom_block(a, &n, 22, 8, name_text(a, cur), 8, 2);

	/* the pendants or crystals (DrawProgressWindow, $0D:E9C8) */
	Pane g = pane(a, P_PROGRESS);
	if (r8(a, R_PROGRESS) < 3) {
		rom_block(a, &g, 21, 11, PENDANTS_WINDOW, 10, 9);
		static const struct {
			int c, r;
			uint16_t base;
		} pend[3] = { { 25, 14, 0x252B }, { 23, 17, 0x2D2B }, { 27, 17, 0x3D2B } };
		int have = r8(a, R_PENDANTS);
		for (int i = 0; i < 3; ++i) {
			uint16_t b = have & (1 << i) ? pend[i].base : 0x313B;
			uint16_t w[4] = { b, (uint16_t) (b + 1), (uint16_t) (b + 2), (uint16_t) (b + 3) };
			icon(&g, pend[i].c, pend[i].r, w);
		}
	} else {
		rom_block(a, &g, 21, 11, CRYSTALS_WINDOW, 10, 9);
		static const int8_t at[7][2] = { { 24, 14 }, { 26, 14 }, { 23, 16 }, { 25, 16 }, { 27, 16 }, { 24, 18 }, { 26, 18 } };
		int have = r8(a, R_CRYSTALS);
		for (int i = 0; i < 7; ++i) {
			if (have & (1 << i)) put(&g, at[i][0], at[i][1], 0x2D44), put(&g, at[i][0] + 1, at[i][1], 0x2D45);
		}
	}

	/* DO: the abilities (DrawAbilityText, $0D:E6B6), boots, gloves, flippers, pearl */
	Pane d = pane(a, P_ABILITY);
	box(&d, 1, 21, 19, 29, 1);
	int flags = r8(a, R_ABILITIES);
	for (int i = 0; i < 2; ++i) {
		for (int j = 0; j < 3; ++j) {
			if (flags & (0x80 >> (i * 4 + j))) rom_block(a, &d, 4 + j * 5, 22 + i * 2, ABILITY_TEXT + (i * 4 + j) * 20, 5, 2);
		}
	}
	put(&d, 2, 22, 0xA4F0), put(&d, 2, 23, 0x24F2);
	put(&d, 3, 21, 0x2482), put(&d, 4, 21, 0x2483);
	static const int8_t equip[4][2] = { { 22, 4 }, { 21, 8 }, { 23, 12 }, { 24, 16 } }; /* boots, gloves, flippers, pearl */
	for (int i = 0; i < 4; ++i) {
		int k = equip[i][0];
		rom_icon(a, &d, equip[i][1], 27, icon_table(a, k) + r8(a, 0xF354 + k - 21) * 8);
	}
	int gloves = r8(a, 0xF354);
	if (gloves) rom_block(a, &d, 4, 22, LIFT_TEXT + (gloves != 1) * 20, 5, 2);

	/* the equipment (DrawEquipmentWindow, $0D:ED29) */
	Pane e = pane(a, P_EQUIPMENT);
	box(&e, 21, 21, 30, 29, 2);
	for (int i = 0; i < 8; ++i) put(&e, 22 + i, 25, 0x28D7);
	rom_block(a, &e, 22, 22, EQUIPMENT_TEXT, 8, 1);
	rom_block(a, &e, 22, 26, DUNGEON_ITEM_TEXT, 8, 1);
	int dungeon = r8(a, R_DUNGEON);
	if (dungeon == 0xFF) {
		for (int i = 0; i < 8; ++i) put(&e, 22 + i, 26, FILL);
		rom_icon(a, &e, 25, 27, HEART_PIECES + (r8(a, R_HEART_PIECES) & 3) * 8);
	}
	int sword = r8(a, 0xF359);
	rom_icon(a, &e, 22, 23, icon_table(a, 26) + (sword == 0xFF ? 0 : sword) * 8);
	rom_icon(a, &e, 25, 23, icon_table(a, 27) + r8(a, 0xF35A) * 8);
	rom_icon(a, &e, 28, 23, icon_table(a, 28) + r8(a, 0xF35B) * 8);
	if (dungeon != 0xFF) {
		uint16_t bit = (uint16_t) (0x8000 >> ((dungeon >> 1) & 15));
		static const uint16_t bigKey[4] = { 0x28D6, 0x68D6, 0x28E6, 0x28E7 };
		static const uint16_t map[4] = { 0x28DE, 0x28DF, 0x28EE, 0x28EF };
		static const uint16_t compass[4] = { 0x24BF, 0x64BF, 0x2CCF, 0x6CCF };
		if (r16(a, R_BIG_KEY) & bit) icon(&e, 28, 27, bigKey);
		if (r16(a, R_DUNGEON_MAP) & bit) icon(&e, 22, 27, map);
		if (r16(a, R_COMPASS) & bit) icon(&e, 25, 27, compass);
	}
}

/* a tile of BG3 (2 bits a pixel) or BG1/BG2 (4) from video RAM */
static uint16_t vword(const uint8_t* vram, uint32_t w) {
	w &= 0x7FFF;
	return (uint16_t) (vram[w * 2] | vram[w * 2 + 1] << 8);
}

static void draw_tile2(const Alttp* a, const uint8_t* vram, uint16_t e, uint32_t* out, int stride) {
	int t = e & 0x3FF, pal = (e >> 10) & 7;
	for (int y = 0; y < 8; ++y) {
		uint16_t row = vword(vram, V_BG3_CHR + t * 8 + (e & 0x8000 ? 7 - y : y));
		for (int x = 0; x < 8; ++x) {
			int b = e & 0x4000 ? x : 7 - x;
			int ci = (row >> b & 1) | (row >> (8 + b) & 1) << 1;
			if (ci) out[y * stride + x] = abgr(r16(a, R_PALETTE + (pal * 4 + ci) * 2));
		}
	}
}

static void render_items(Alttp* a, const uint8_t* vram) {
	for (size_t i = 0; i < ALTTP_W * ALTTP_H; ++i) a->items[i] = 0xFF000000u;
	for (int r = MENU_ROW0; r < MENU_ROW0 + ALTTP_H / 8; ++r) {
		for (int c = 0; c < 32; ++c) {
			uint16_t e = a->menu[r * 32 + c];
			if (e != BLANK) draw_tile2(a, vram, e, &a->items[(r - MENU_ROW0) * 8 * ALTTP_W + c * 8], ALTTP_W);
		}
	}
	/* Link's marker on the map: the same ring, round a 16 x 16 middle */
	for (int i = 0; i < 32 * 32; ++i) a->marker[i] = 0;
	static const int8_t ringAt[12][2] = { { 1, 0 }, { 2, 0 }, { 0, 1 }, { 3, 1 }, { 0, 2 }, { 3, 2 }, { 1, 3 }, { 2, 3 }, { 0, 0 }, { 3, 0 }, { 3, 3 }, { 0, 3 } };
	static const uint16_t ringTile[12] = { 0x2061, 0x6061, 0x2070, 0x6070, 0xA070, 0xE070, 0xA061, 0xE061, 0x2060, 0x6060, 0xE060, 0xA060 };
	for (int i = 0; i < 12; ++i) {
		uint32_t tile[64];
		for (int k = 0; k < 64; ++k) tile[k] = 0;
		draw_tile2(a, vram, (uint16_t) (ringTile[i] | 7 << 10), tile, 8);
		/* (only the ring: its black, the menu's pane behind it, left out) */
		for (int y = 0; y < 8; ++y) {
			for (int x = 0; x < 8; ++x) {
				uint32_t c = tile[y * 8 + x];
				a->marker[(ringAt[i][1] * 8 + y) * 32 + ringAt[i][0] * 8 + x] = (c & 0xFFFFFF) ? c : 0;
			}
		}
	}
}

/* what the items screen depends on, so it's built again only when it changes */
static uint32_t items_key(const Alttp* a, const uint8_t* vram) {
	uint32_t k = crc32(0, a->ram + R_ITEMS, 0x40);
	uint8_t more[4] = { r8(a, R_CUR_ITEM), r8(a, R_DUNGEON), r8(a, R_PROGRESS), r8(a, R_BOTTLE) };
	k = crc32(k, more, sizeof more);
	k = crc32(k, a->ram + R_PALETTE, 64);
	k = crc32(k, vram + V_BG3_CHR * 2, 0x2000);
	return k | 1;
}

const uint32_t* alttp_items(const Alttp* a) {
	return a->items;
}

/* Link has it (a bottle: any bottle) */
static bool selectable(const Alttp* a, int item) {
	if (item == 16) return r8(a, R_BOTTLES) | r8(a, R_BOTTLES + 1) | r8(a, R_BOTTLES + 2) | r8(a, R_BOTTLES + 3);
	return item_level(a, item) != 0;
}

int alttp_items_hit(const Alttp* a, int x, int y) {
	int dx = paneTouch[P_ITEMS][0] - paneGame[P_ITEMS][0], dy = paneTouch[P_ITEMS][1] - paneGame[P_ITEMS][1];
	for (int i = 0; i < 20; ++i) {
		int c, r;
		slot_place(a, i, &c, &r);
		/* the icon's 16 x 16 and 4 pixels round it (slots are 24 apart) */
		int x0 = (c + dx) * 8 - 4, y0 = (r + dy - MENU_ROW0) * 8 - 4;
		if (x >= x0 && x < x0 + 24 && y >= y0 && y < y0 + 24) return selectable(a, i + 1) ? i + 1 : 0;
	}
	return 0;
}

int alttp_select(const Alttp* a, const uint8_t* wram, int item, AltWrite* out, int max) {
	(void) wram;
	if (item < 1 || item > 20 || max < 12 || !alttp_playing(a)) return 0;
	/* when the game itself would let START open its screen */
	if (r8(a, R_MENU_CUSTOM) | r8(a, R_IMMOBILE) | r8(a, R_BLOCK_MENU)) return 0;
	if (!selectable(a, item)) return 0;
	int n = 0;
	int bottle = r8(a, R_BOTTLE);
	if (item == 16 && !bottle) {
		/* as the game's screen does when it opens: the first bottle */
		for (int i = 0; i < 4 && !bottle; ++i) {
			if (r8(a, R_BOTTLES + i)) bottle = i + 1;
		}
		out[n++] = (AltWrite) { R_BOTTLE, (uint8_t) bottle };
	} else if (item == 16 && r8(a, R_CUR_ITEM) == 16) {
		/* the bottle tapped again: the next bottle Link has, as Down does
		 * in the game's bottle menu (Hud_BottleMenu, $0D:E0DF) */
		int v = bottle - 1;
		do v = (v + 1) & 3;
		while (!r8(a, R_BOTTLES + v));
		bottle = v + 1;
		out[n++] = (AltWrite) { R_BOTTLE, (uint8_t) bottle };
	}
	uint8_t equip = rom8(a, MENU_TO_EQUIP + item);
	out[n++] = (AltWrite) { R_CUR_ITEM, (uint8_t) item };
	out[n++] = (AltWrite) { R_EQUIP, equip };
	out[n++] = (AltWrite) { R_EQ_DEBUG, (uint8_t) (equip == 5 || equip == 6 ? 0 : 2) };
	if (equip != 5 && equip != 6) out[n++] = (AltWrite) { R_DEBUG_1, 0 };
	/* the HUD's item box (UpdateHUDBuffer, $0D:FB3C) and its upload */
	int level = r8(a, R_ITEMS + item - 1);
	if (item == 4) level = level != 0;
	if (item == 16) level = r8(a, R_BOTTLES + bottle - 1);
	uint32_t t = icon_table(a, item) + level * 8;
	static const uint16_t at[4] = { 0x4A, 0x4C, 0x8A, 0x8C };
	for (int i = 0; i < 4; ++i) {
		uint16_t w = rom16(a, t + i * 2);
		out[n++] = (AltWrite) { R_HUD + at[i], (uint8_t) w };
		out[n++] = (AltWrite) { R_HUD + at[i] + 1, (uint8_t) (w >> 8) };
	}
	out[n++] = (AltWrite) { R_HUD_UPLOAD, 1 };
	return n;
}

/* ---- the world map (ALTTP-2; tools/alttp_world.py) */

/* Colour indices of BG1 or BG2 along a line: n pixels from layer pixel
 * (x, y), 0 where clear. Each tile's row decoded once. */
static void bg_row(const uint8_t* vram, uint32_t mapBase, int x, int y, int n, uint8_t* out) {
	y &= 511;
	for (int i = 0; i < n;) {
		int lx = (x + i) & 511;
		int screen = (lx >> 8) + ((y >> 8) << 1);
		uint16_t e = vword(vram, mapBase + screen * 0x400 + ((y >> 3) & 31) * 32 + ((lx >> 3) & 31));
		int ty = e & 0x8000 ? 7 - (y & 7) : y & 7;
		uint32_t base = V_BG12_CHR + (e & 0x3FF) * 16;
		uint16_t p0 = vword(vram, base + ty), p1 = vword(vram, base + 8 + ty);
		int pal = ((e >> 10) & 7) * 16;
		for (int tx = lx & 7; tx < 8 && i < n; ++tx, ++i) {
			int b = e & 0x4000 ? tx : 7 - tx;
			int c = (p0 >> b & 1) | (p0 >> (8 + b) & 1) << 1 | (p1 >> b & 1) << 2 | (p1 >> (8 + b) & 1) << 3;
			out[i] = (uint8_t) (c ? pal + c : 0);
		}
	}
}

static uint16_t* chunk_for(Alttp* a, int w, int x, int y) {
	uint16_t** c = &a->chunk[w][(y / CHUNK) * CHUNKS + x / CHUNK];
	if (!*c) *c = calloc(CHUNK * CHUNK, sizeof(uint16_t));
	return *c;
}

static void capture(Alttp* a, const uint8_t* vram) {
	int w = (r8(a, R_AREA) & 0x40) != 0;
	int cx = r16(a, R_BG2_X), cy = r16(a, R_BG2_Y) + 1; /* line 0 shows row scroll + 1 */
	bool bg1 = r8(a, R_TM) & 1;
	uint8_t i2[ALTTP_W], i1[ALTTP_W];
	for (int y = 0; y < ALTTP_H; ++y) {
		int wy = cy + y;
		if (wy < 0 || wy >= ALTTP_WORLD) continue;
		bg_row(vram, V_BG2_MAP, cx, wy, ALTTP_W, i2);
		if (bg1) bg_row(vram, V_BG1_MAP, cx, wy, ALTTP_W, i1);
		for (int x = 0; x < ALTTP_W; ++x) {
			int wx = cx + x;
			if (wx < 0 || wx >= ALTTP_WORLD) continue;
			int i = bg1 && i1[x] ? i1[x] : i2[x];
			uint16_t* c = chunk_for(a, w, wx, wy);
			if (c) c[(wy % CHUNK) * CHUNK + wx % CHUNK] = (uint16_t) (r16(a, R_PALETTE + i * 2) | 0x8000);
		}
	}
	/* whole cells only */
	for (int gy = (cy + CELL - 1) / CELL; gy < (cy + ALTTP_H) / CELL && gy < CELLS; ++gy) {
		for (int gx = (cx + CELL - 1) / CELL; gx < (cx + ALTTP_W) / CELL && gx < CELLS; ++gx) {
			if (gx >= 0 && gy >= 0) a->seen[w][(gy * CELLS + gx) >> 3] |= (uint8_t) (1 << (gx & 7));
		}
	}
	a->dirty = true;
}

void alttp_capture_now(Alttp* a, const uint8_t* wram, const uint8_t* vram) {
	memcpy(a->ram, wram, sizeof a->ram);
	a->have = true;
	capture(a, vram);
}

static bool is_seen(const Alttp* a, int w, int x, int y) {
	if (x < 0 || y < 0 || x >= ALTTP_WORLD || y >= ALTTP_WORLD) return false;
	int gx = x / CELL, gy = y / CELL;
	return a->seen[w][(gy * CELLS + gx) >> 3] & (1 << (gx & 7));
}

static uint16_t ground(const Alttp* a, int w, int x, int y) {
	const uint16_t* c = a->chunk[w][(y / CHUNK) * CHUNKS + x / CHUNK];
	return c ? c[(y % CHUNK) * CHUNK + x % CHUNK] : 0;
}

void alttp_compose(const Alttp* a, int world, int zoom, int vx, int vy, int w, int h, uint32_t* out) {
	world &= 1;
	if (zoom != 2 && zoom != 4) zoom = 1;
	const uint32_t* sky = a->sky[world];
	int sx0 = vx / zoom, sy0 = vy / zoom;
	for (int oy = 0; oy < h; ++oy) {
		int wy = vy + oy * zoom;
		uint32_t* o = out + (size_t) oy * w;
		const uint32_t* skyRow = sky + (size_t) ((((sy0 + oy) % SKY_H) + SKY_H) % SKY_H) * SKY_W;
		for (int ox = 0; ox < w; ++ox) {
			int wx = vx + ox * zoom;
			if (!is_seen(a, world, wx, wy)) {
				o[ox] = skyRow[(((sx0 + ox) % SKY_W) + SKY_W) % SKY_W];
				continue;
			}
			if (zoom == 1) {
				uint16_t c = ground(a, world, wx, wy);
				o[ox] = c ? abgr(c) : 0xFF000000u;
				continue;
			}
			/* the block's average (what was taken of it) */
			unsigned r = 0, g = 0, b = 0, n = 0;
			for (int y = 0; y < zoom; ++y) {
				for (int x = 0; x < zoom; ++x) {
					uint16_t c = ground(a, world, wx + x, wy + y);
					if (!c) continue;
					r += c & 31, g += (c >> 5) & 31, b += (c >> 10) & 31, ++n;
				}
			}
			if (!n) {
				o[ox] = 0xFF000000u;
				continue;
			}
			/* (the average in 8 bits a channel, as abgr() widens 5) */
			uint32_t R = r * 255 / (31 * n), G = g * 255 / (31 * n), B = b * 255 / (31 * n);
			o[ox] = 0xFF000000u | B << 16 | G << 8 | R;
		}
	}
}

void alttp_link(const Alttp* a, int* world, int* x, int* y) {
	*world = a->linkWorld, *x = a->linkX, *y = a->linkY;
}

const uint32_t* alttp_marker(const Alttp* a) {
	return a->marker;
}

bool alttp_marker_on(const Alttp* a) {
	return (r8(a, R_FRAME) & 0x10) != 0;
}

/* ---- once a pass */

int alttp_update(Alttp* a, const uint8_t* wram, const uint8_t* vram, const uint8_t* sram) {
	memcpy(a->ram, wram, sizeof a->ram);
	a->have = true;
	int ch = 0;
	uint8_t m = r8(a, R_MODULE);
	if (m == 0x04) a->namingSeen = true; /* a new file: its record starts empty */
	if (alttp_playing(a)) a->played = true;
	if (sram && alttp_playing(a)) {
		int f = sram[0x1FFE] / 2;
		f = f >= 1 && f <= 3 ? f : 0;
		if (f && f != a->file) a->file = f, ch |= ALTTP_FILE;
	}
	if (outdoors_still(a)) {
		int w = (r8(a, R_AREA) & 0x40) != 0;
		a->linkWorld = w;
		a->linkX = r16(a, R_LINK_X) + 8, a->linkY = r16(a, R_LINK_Y) + 12;
		/* the screen taken when the camera has moved, else every 30
		 * passes (what changes in place: a cut bush, a lifted rock) */
		int cx = r16(a, R_BG2_X), cy = r16(a, R_BG2_Y);
		bool moved = cx != a->takenX || cy != a->takenY || w != a->takenWorld;
		if (vram && (moved || ++a->takenAge >= 30)) {
			capture(a, vram), ch |= ALTTP_MAP;
			a->takenX = cx, a->takenY = cy, a->takenWorld = w, a->takenAge = 0;
		}
		if (r8(a, R_AREA) != a->lastArea) a->lastArea = r8(a, R_AREA), ch |= ALTTP_AREA;
	}
	if (vram) {
		uint32_t k = items_key(a, vram);
		if (k != a->itemsKey) {
			a->itemsKey = k;
			build_menu(a, true);
			render_items(a, vram);
			ch |= ALTTP_ITEMS;
		}
	}
	return ch;
}

/* ---- the dungeon map (ALTTP-5, ALTTP-6; alttp_dungeon.c) */

bool alttp_in_dungeon(const Alttp* a) {
	return a->have && r8(a, R_INDOORS) && r8(a, R_DUNGEON) != 0xFF;
}

const uint32_t* alttp_dungeon_map(Alttp* a, const uint8_t* vram, int view) {
	uint32_t* out = NULL;
	if (!a->dun || !alttp_in_dungeon(a) || !altdun_draw(a->dun, a->ram, vram, view, &out)) return NULL;
	return out;
}

bool alttp_dungeon_floors(const Alttp* a, int* lowest, int* highest, int* link) {
	return a->dun && alttp_in_dungeon(a) && altdun_floors(a->dun, a->ram, lowest, highest, link);
}

int alttp_dungeon_floor_at(const Alttp* a, int x, int y) {
	return a->dun && alttp_in_dungeon(a) ? altdun_floor_at(a->dun, a->ram, x, y) : ALTTP_FLOOR_AS_GAME;
}

/* ---- the seen record's file: "SDSALTP1", the size, then zlib of: per
 * world, the seen cells' bits, the pieces' mask (64 bits) and each piece */

bool alttp_dirty(const Alttp* a) {
	return a->dirty;
}

int alttp_file(const Alttp* a) {
	return a->file;
}

bool alttp_take_new_file(Alttp* a) {
	bool n = a->namingSeen;
	a->namingSeen = false;
	return n;
}

void alttp_clear_seen(Alttp* a) {
	clear_seen(a);
	a->dirty = true;
}

bool alttp_save_seen(Alttp* a, const char* path) {
	size_t size = 0;
	for (int w = 0; w < 2; ++w) {
		size += sizeof a->seen[w] + 8;
		for (int i = 0; i < CHUNKS * CHUNKS; ++i) size += a->chunk[w][i] ? CHUNK * CHUNK * 2 : 0;
	}
	uint8_t* raw = malloc(size);
	uLongf zn = compressBound((uLong) size);
	uint8_t* z = malloc(zn);
	bool ok = false;
	if (raw && z) {
		size_t at = 0;
		for (int w = 0; w < 2; ++w) {
			memcpy(raw + at, a->seen[w], sizeof a->seen[w]), at += sizeof a->seen[w];
			uint64_t mask = 0;
			for (int i = 0; i < CHUNKS * CHUNKS; ++i) mask |= (uint64_t) (a->chunk[w][i] != NULL) << i;
			for (int k = 0; k < 8; ++k) raw[at++] = (uint8_t) (mask >> (k * 8));
			for (int i = 0; i < CHUNKS * CHUNKS; ++i) {
				if (!a->chunk[w][i]) continue;
				for (int p = 0; p < CHUNK * CHUNK; ++p) raw[at++] = (uint8_t) a->chunk[w][i][p], raw[at++] = (uint8_t) (a->chunk[w][i][p] >> 8);
			}
		}
		if (compress2(z, &zn, raw, (uLong) size, 6) == Z_OK) {
			char tmp[1100];
			snprintf(tmp, sizeof tmp, "%s.tmp", path);
			FILE* f = fopen(tmp, "wb");
			uint8_t head[12] = { 'S', 'D', 'S', 'A', 'L', 'T', 'P', '1', (uint8_t) size, (uint8_t) (size >> 8), (uint8_t) (size >> 16), (uint8_t) (size >> 24) };
			ok = f && fwrite(head, 1, 12, f) == 12 && fwrite(z, 1, zn, f) == zn;
			if (f && fclose(f) != 0) ok = false;
			ok = ok && rename(tmp, path) == 0;
		}
	}
	free(raw);
	free(z);
	if (ok) a->dirty = false;
	return ok;
}

bool alttp_load_seen(Alttp* a, const char* path) {
	clear_seen(a);
	a->dirty = false;
	FILE* f = fopen(path, "rb");
	if (!f) return false;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8_t* z = n > 12 ? malloc((size_t) n) : NULL;
	bool ok = z && fread(z, 1, (size_t) n, f) == (size_t) n && !memcmp(z, "SDSALTP1", 8);
	fclose(f);
	uLongf size = ok ? (uLongf) (z[8] | z[9] << 8 | z[10] << 16 | (uint32_t) z[11] << 24) : 0;
	uint8_t* raw = ok && size <= 2u * (sizeof a->seen[0] + 8 + CHUNKS * CHUNKS * CHUNK * CHUNK * 2) ? malloc(size) : NULL;
	ok = raw && uncompress(raw, &size, z + 12, (uLong) (n - 12)) == Z_OK;
	size_t at = 0;
	for (int w = 0; w < 2 && ok; ++w) {
		if (at + sizeof a->seen[w] + 8 > size) {
			ok = false;
			break;
		}
		memcpy(a->seen[w], raw + at, sizeof a->seen[w]), at += sizeof a->seen[w];
		uint64_t mask = 0;
		for (int k = 0; k < 8; ++k) mask |= (uint64_t) raw[at++] << (k * 8);
		for (int i = 0; i < CHUNKS * CHUNKS && ok; ++i) {
			if (!(mask >> i & 1)) continue;
			if (at + CHUNK * CHUNK * 2 > size || !(a->chunk[w][i] = malloc(CHUNK * CHUNK * 2))) {
				ok = false;
				break;
			}
			for (int p = 0; p < CHUNK * CHUNK; ++p, at += 2) a->chunk[w][i][p] = (uint16_t) (raw[at] | raw[at + 1] << 8);
		}
	}
	free(z);
	free(raw);
	if (!ok) clear_seen(a);
	return ok;
}

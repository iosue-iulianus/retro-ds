/* A Link to the Past's second screen (alttp.h). Addresses, formats and how
 * each was checked: docs/games/a-link-to-the-past.md; the Python
 * references, tools/alttp_items.py and tools/alttp_world.py, do the same
 * and are checked against the game's own screens. */
#include "alttp.h"
#include "alttp_dungeon.h"

#include <math.h>
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
#define R_TS 0x001D      /* the sub screen's layers */
#define R_CGWSEL 0x0099  /* colour maths: bit 1, with the sub screen (else the fixed colour) */
#define R_CGADSUB 0x009A /* bit 5 on the backdrop; bit 6 halved; bit 7 subtracted */
#define R_COLDATA 0x009C /* the fixed colour: $9C red, $9D green, $9E blue (low 5 bits) */
#define R_BG1_X 0x00E0
#define R_BG1_Y 0x00E6
#define R_LINK_Y 0x0020
#define R_LINK_X 0x0022
#define R_FACING 0x002F /* the way Link faces: 0 up, 2 down, 4 left, 6 right */
#define R_AREA 0x008A
#define R_AREA_BIG 0x0712 /* the area is 1024 x 1024 (else 512) */
#define R_BG2_X 0x00E2
#define R_BG2_Y 0x00E8
#define R_OAM 0x0800      /* the game's copy of the sprite list, 128 x 4 bytes */
#define R_SORT_OFFSET 0x0352 /* where Link's sprites start in it, in bytes */
#define R_OAM_HIGH 0x0A20 /* each sprite's 9th x bit (bit 0) and size (bit 1), a byte each */
#define OAM_COPY (R_OAM_HIGH + 128 - R_OAM)
#define R_TITLE_SWORD 0x00C8 /* the title's sword's y (16 bits; -130 to 30) */
#define R_TITLE_FADE 0xC007  /* the title's ZELDA fade-in: a countdown, a step every 3 frames */
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
#define R_MAP_STAGE 0xF3C7 /* which goal markers the world map shows (the story's stage) */
#define R_OW_EVENT_5B 0xF2DB /* bit $20: no goal markers any more */
#define R_PORTAL_X 0x1ABF /* the mirror's portal in the Light World (world pixels): x low, $1ACF x high, $1ADF y low, $1AEF y high; all 0: none */

/* video RAM, word addresses, as the game sets the PPU for play */
#define V_BG2_MAP 0x0000
#define V_BG1_MAP 0x1000
#define V_BG12_CHR 0x2000
#define V_BG3_CHR 0x7000
#define V_OBJ_CHR 0x4000 /* sprite tiles: Link's head is 0x00, his body 0x02 */

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
/* the world map's goal markers (ALTTP-11): for marker i (0 to 6) and the
 * stage k (0 to 8), x and y (world pixels, $FF00 none) at GOAL_POSX/Y +
 * $24 i + 2 k, attributes and tile at GOAL_TILE + $12 i + 2 k */
#define GOAL_POSX 0x0ABDE4u
#define GOAL_POSY 0x0ABDF6u
#define GOAL_TILE 0x0ABEE0u
#define GOAL_RED_X 0x0ABF5Eu    /* the red X's 4 frames (8 x 8 tiles) */
#define PORTAL_PROPS 0x0ABF62u  /* the mirror's portal: its 4 frames' attributes (it turns), tile $6A */
#define GOAL_NUMBERS 0x0AC515u  /* each crystal's number (8 x 8 tiles) */
#define PENDANT_BITS 0x0AC5A6u  /* markers 0 to 2's pendants in $7EF374 */
#define CRYSTAL_BITS 0x0AC5BFu  /* markers 0 to 6's crystals in $7EF37A */
#define SPR_SHEET_BANK 0x00CFF3u /* GFXSheetPointers, sprites: bank, high, low */
#define SPR_SHEET_HIGH 0x00D0D2u
#define SPR_SHEET_LOW 0x00D1B1u
#define GOAL_SHEET 5            /* the markers' tiles: its second half (stored as it is), tiles $60 to $7F */

#define BLANK 0x207F
#define FILL 0x24F5
#define MENU_ROW0 3 /* the items screen's first row shown */

#define CELL 16 /* what counts as seen: 16 x 16 world pixels, whole */
#define CELLS (ALTTP_WORLD / CELL)
#define CHUNK ALTTP_PIECE /* the ground is kept in 512 x 512 pieces (an area) */
#define CHUNKS ALTTP_PIECES
#define SKY_W ALTTP_SKY_W
#define SKY_H ALTTP_SKY_H

enum { P_ITEMS, P_NAME, P_PROGRESS, P_ABILITY, P_EQUIPMENT, P_COUNT };
/* each pane's place on the game's own screen, and on RetroDS's (ALTTP-10:
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
	uint32_t zoomPane[ALTTP_ZOOM_PANE_W * ALTTP_ZOOM_PANE_H]; /* the zoom indicator's pane */
	uint32_t marker[32 * 32];
	bool markerLink;    /* the marker is Link's sprite (else the ring, until he's seen) */
	/* Link's picture for each way he faces (up, down, left, right as $2F / 2):
	 * what it was made from, 2 taken standing still, 1 not yet; the one shown */
	uint32_t dirPx[4][32 * 32];
	uint32_t dirKey[4];
	uint8_t dirHave[4];
	uint32_t livePx[32 * 32], liveKey; /* as he is now, walking or standing */
	bool wasBusy;
	int shownDir, stoodDir, lastDir; /* the way he stood still the update before (-1 not), the way he faced */
	/* the sprite list's copy as it was the frame before ($1A, frame) and
	 * this frame: the picture (and the tiles in video RAM) go with the
	 * copy made the frame before, sent to the PPU at its end */
	uint8_t oam[2][OAM_COPY + 2];
	int oamFrame[2];
	/* the map */
	uint16_t* chunk[2][CHUNKS * CHUNKS]; /* SNES colour | 0x8000 where taken, 0 elsewhere */
	/* each piece compressed as the record keeps it, made again only when
	 * the piece has changed since (so saving costs the new pieces only) */
	uint8_t* zchunk[2][CHUNKS * CHUNKS];
	uint32_t zlen[2][CHUNKS * CHUNKS];
	bool zstale[2][CHUNKS * CHUNKS];
	uint8_t seen[2][CELLS * CELLS / 8];
	/* each piece's last change (gen when it changed: odd, never 0) */
	uint32_t pver[2][CHUNKS * CHUNKS];
	uint32_t gen;
	uint32_t sky[2][SKY_W * SKY_H];
	/* the clouds' own colours (the fog's edge): their body, light edge,
	 * shaded edge and rim, from the sky's most used ones */
	uint32_t cloud[2][4];
	/* the goal markers: their tiles ($60 to $7F, video RAM words), the
	 * ones shown now and their pictures */
	uint16_t goalChr[32 * 16];
	AltGoal goal[ALTTP_GOAL_MAX];
	int nGoals;
	uint32_t goalPx[ALTTP_GOAL_MAX * 16 * 16];
	uint32_t goalKey;
	int linkWorld, linkX, linkY;
	int lastArea;
	int takenX, takenY, takenWorld, takenAge; /* the last screen taken, and passes since */
	int file; /* 1 to 3, 0 not known */
	/* the title's sword (ALTTP-14): the fade-in's countdown as last seen,
	 * the update it changed at, and updates counted */
	int titleCount, titleCountAt, updates;
	/* the game's files in its cartridge RAM, as at the last update (ALTTP-2:
	 * the seen record goes with its file: erased, copied) */
	bool slotsKnown, slotValid[3];
	AltSlotEvent slotEv[8];
	int nSlotEv;
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

/* The clouds shown where nothing is seen, in each world's colours, made
 * to repeat without a seam: the world map's clouds are puffs on a plain
 * tile; each whole puff of the map's (its tiles all cloud tiles, as the
 * top rows use) is scattered over a picture that wraps round at its
 * edges, a tile apart at least, in the same order every time. */
static void make_sky(Alttp* a) {
	enum { N = 64, TW = SKY_W / 8, TH = SKY_H / 8 };
	static uint8_t map[N][N];
	int count[256] = { 0 }, plain = 0;
	for (int ty = 0; ty < N; ++ty) {
		for (int tx = 0; tx < N; ++tx) {
			map[ty][tx] = rom8(a, MAP_TILEMAP + ((tx >= 32) + 2 * (ty >= 32)) * 1024 + (ty & 31) * 32 + (tx & 31));
			if (ty < 9 && ++count[map[ty][tx]] > count[plain]) plain = map[ty][tx];
		}
	}
	bool cloudTile[256] = { false };
	for (int ty = 0; ty < 9; ++ty) {
		for (int tx = 0; tx < N; ++tx) cloudTile[map[ty][tx]] = map[ty][tx] != plain;
	}
	/* the puffs: tiles not plain, touching (corners too) */
	static int8_t mark[N][N];
	static int puffTile[N * N];
	int puffStart[64], puffLen[64], nPuffs = 0, nTiles = 0;
	memset(mark, 0, sizeof mark);
	for (int sy = 0; sy < N; ++sy) {
		for (int sx = 0; sx < N; ++sx) {
			if (map[sy][sx] == plain || mark[sy][sx]) continue;
			static int stack[N * N];
			int n = 0, start = nTiles;
			bool whole = true;
			stack[n++] = sy * N + sx, mark[sy][sx] = 1;
			while (n) {
				int k = stack[--n], y = k / N, x = k % N;
				puffTile[nTiles++] = k;
				if (!cloudTile[map[y][x]] || y == 0 || x == 0 || y == N - 1 || x == N - 1) whole = false;
				for (int dy = -1; dy <= 1; ++dy) {
					for (int dx = -1; dx <= 1; ++dx) {
						int ny = y + dy, nx = x + dx;
						if (ny < 0 || nx < 0 || ny >= N || nx >= N || mark[ny][nx] || map[ny][nx] == plain) continue;
						mark[ny][nx] = 1, stack[n++] = ny * N + nx;
					}
				}
			}
			if (whole && nPuffs < 64) puffStart[nPuffs] = start, puffLen[nPuffs++] = nTiles - start;
			else nTiles = start;
		}
	}
	/* scattered: each puff by turns at the next free place a simple
	 * sequence picks, until a sixth of the picture's tiles are cloud */
	static uint8_t sky[TH][TW];
	static bool used[TH][TW];
	memset(sky, plain, sizeof sky);
	memset(used, 0, sizeof used);
	uint32_t seed = 12345;
	int filled = 0;
	for (int tries = 0; tries < 40000 && nPuffs && filled < TW * TH / 6; ++tries) {
		int p = tries % nPuffs;
		seed = seed * 1103515245u + 12345u;
		int ox = (int) (seed >> 8) % TW;
		seed = seed * 1103515245u + 12345u;
		int oy = (int) (seed >> 8) % TH;
		int bx = puffTile[puffStart[p]] % N, by = puffTile[puffStart[p]] / N;
		bool fits = true;
		for (int k = 0; k < puffLen[p] && fits; ++k) {
			int x = puffTile[puffStart[p] + k] % N - bx + ox, y = puffTile[puffStart[p] + k] / N - by + oy;
			for (int dy = -1; dy <= 1 && fits; ++dy) {
				for (int dx = -1; dx <= 1; ++dx) {
					if (used[((y + dy) % TH + TH) % TH][((x + dx) % TW + TW) % TW]) fits = false;
				}
			}
		}
		if (!fits) continue;
		for (int k = 0; k < puffLen[p]; ++k) {
			int t = puffTile[puffStart[p] + k];
			int x = ((t % N - bx + ox) % TW + TW) % TW, y = ((t / N - by + oy) % TH + TH) % TH;
			sky[y][x] = map[t / N][t % N], used[y][x] = true;
			++filled;
		}
	}
	for (int w = 0; w < 2; ++w) {
		for (int ty = 0; ty < TH; ++ty) {
			for (int tx = 0; tx < TW; ++tx) {
				uint8_t ch = sky[ty][tx];
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

/* the clouds' body (the colour most used), then the lightest, a shade
 * and the darkest of the next most used, for the fog's edge */
static void cloud_colours(Alttp* a) {
	for (int w = 0; w < 2; ++w) {
		uint32_t col[8] = { 0 };
		int cnt[8] = { 0 }, n = 0;
		for (int i = 0; i < SKY_W * SKY_H; ++i) {
			uint32_t c = a->sky[w][i];
			int k = 0;
			while (k < n && col[k] != c) ++k;
			if (k == n && n < 8) col[n] = c, cnt[n++] = 0;
			if (k < n) ++cnt[k];
		}
		/* by use, most first */
		for (int i = 0; i < n; ++i) {
			for (int j = i + 1; j < n; ++j) {
				if (cnt[j] > cnt[i]) {
					uint32_t t = col[i];
					col[i] = col[j], col[j] = t;
					int u = cnt[i];
					cnt[i] = cnt[j], cnt[j] = u;
				}
			}
		}
		int m = n < 5 ? n : 5, light = 0, dark = 0;
		#define LUM(c) (((c) & 0xFF) + ((c) >> 8 & 0xFF) + ((c) >> 16 & 0xFF))
		for (int i = 0; i < m; ++i) {
			if (LUM(col[i]) > LUM(col[light])) light = i;
			if (LUM(col[i]) < LUM(col[dark])) dark = i;
		}
		#undef LUM
		a->cloud[w][0] = col[0], a->cloud[w][1] = col[light];
		a->cloud[w][2] = col[m > 1 ? 1 : 0], a->cloud[w][3] = col[dark];
	}
}

/* the goal markers' tiles as the world map loads them (LoadChrHalfSlot
 * with $0AAA = $10): the second half of sprite sheet 5, kept in the
 * cartridge as it is (3 bits a pixel), the fourth plane set where any
 * other is */
static void goal_tiles(Alttp* a) {
	uint32_t at = (uint32_t) rom8(a, SPR_SHEET_BANK + GOAL_SHEET) << 16 | (uint32_t) rom8(a, SPR_SHEET_HIGH + GOAL_SHEET) << 8 | rom8(a, SPR_SHEET_LOW + GOAL_SHEET);
	uint32_t o = lorom(at) + 0x300;
	for (int t = 0; t < 32; ++t) {
		for (int y = 0; y < 8; ++y) {
			uint32_t b = o + (uint32_t) t * 24;
			uint8_t p0 = b + 2 * y + 1 < a->romSize ? a->rom[b + 2 * y] : 0, p1 = b + 2 * y + 1 < a->romSize ? a->rom[b + 2 * y + 1] : 0;
			uint8_t p2 = b + 16 + y < a->romSize ? a->rom[b + 16 + y] : 0;
			a->goalChr[t * 16 + y] = (uint16_t) (p0 | p1 << 8);
			a->goalChr[t * 16 + 8 + y] = (uint16_t) (p2 | (p0 | p1 | p2) << 8);
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
	a->stoodDir = a->lastDir = -1;
	a->gen = 1;
	a->oamFrame[0] = a->oamFrame[1] = -1;
	a->takenX = -1;
	a->itemsKey = 1;
	make_sky(a);
	cloud_colours(a);
	goal_tiles(a);
	for (size_t i = 0; i < ALTTP_W * ALTTP_H; ++i) a->items[i] = 0xFF000000u;
	return a;
}

/* every piece to be drawn again (the record replaced) */
static void changed_all(Alttp* a) {
	a->gen += 2;
	for (int w = 0; w < 2; ++w) {
		for (int i = 0; i < CHUNKS * CHUNKS; ++i) a->pver[w][i] = a->gen;
	}
}

static void clear_seen(Alttp* a) {
	for (int w = 0; w < 2; ++w) {
		for (int i = 0; i < CHUNKS * CHUNKS; ++i) {
			free(a->chunk[w][i]);
			free(a->zchunk[w][i]);
			a->chunk[w][i] = NULL, a->zchunk[w][i] = NULL, a->zlen[w][i] = 0, a->zstale[w][i] = false;
		}
	}
	memset(a->seen, 0, sizeof a->seen);
	changed_all(a);
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

/* the overworld scrolling from one area to the next (submodules 5 to 7;
 * $13 to $15 after a mosaic), the screen at full brightness */
static bool area_scrolling(const Alttp* a) {
	uint8_t m = r8(a, R_MODULE), sub = r8(a, R_SUBMODULE);
	return (m == 0x09 || m == 0x0B) && ((sub >= 0x05 && sub <= 0x07) || (sub >= 0x13 && sub <= 0x15)) && !r8(a, R_INDOORS) &&
	       r8(a, R_INIDISP) == 0x0F && r8(a, R_AREA) < 0x80;
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

static void build_menu(Alttp* a, bool ringOn) {
	for (int i = 0; i < 32 * 32; ++i) a->menu[i] = BLANK;
	int cur = r8(a, R_CUR_ITEM);

	/* the Y items (DrawYItems, $0D:E3D9; its frame always in palette 7:
	 * every caller gives GetPaletteMask 1, Link's items or not) */
	Pane p = pane(a, P_ITEMS);
	box(&p, 1, 5, 19, 19, 7);
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

/* the zoom indicator's pane: a frame of the items screen's (the Y items',
 * green), black inside, from the same tiles in video RAM */
static void render_zoom_pane(Alttp* a, const uint8_t* vram) {
	enum { C = ALTTP_ZOOM_PANE_W / 8, R = ALTTP_ZOOM_PANE_H / 8 };
	static uint16_t m[32 * 32];
	Pane p = { m, 0, 0 };
	box(&p, 0, 0, C - 1, R - 1, 7);
	enum { W = ALTTP_ZOOM_PANE_W, H = ALTTP_ZOOM_PANE_H };
	uint32_t* px = a->zoomPane;
	for (int i = 0; i < W * H; ++i) px[i] = 0;
	for (int r = 0; r < R; ++r) {
		for (int c = 0; c < C; ++c) draw_tile2(a, vram, m[r * 32 + c], &px[r * 8 * W + c * 8], W);
	}
	/* outside the frame clear (the map shows there, up to the tiles' own
	 * one-pixel black outline, kept: the owner's choice); inside, black:
	 * what can't be reached from the edge without crossing the frame */
	static uint8_t out[W * H];
	static int stack[W * H];
	memset(out, 0, sizeof out);
	int n = 0;
	for (int i = 0; i < W * H; ++i) {
		int x = i % W, y = i / W;
		if ((x == 0 || y == 0 || x == W - 1 || y == H - 1) && !px[i]) out[i] = 1, stack[n++] = i;
	}
	while (n) {
		int i = stack[--n], x = i % W, y = i / W;
		const int nb[4] = { x > 0 ? i - 1 : -1, x < W - 1 ? i + 1 : -1, y > 0 ? i - W : -1, y < H - 1 ? i + W : -1 };
		for (int k = 0; k < 4; ++k) {
			if (nb[k] >= 0 && !out[nb[k]] && !px[nb[k]]) out[nb[k]] = 1, stack[n++] = nb[k];
		}
	}
	for (int i = 0; i < W * H; ++i) {
		if (!px[i] && !out[i]) px[i] = 0xFF000000u;
	}
}

const uint32_t* alttp_zoom_pane(const Alttp* a) {
	return a->zoomPane;
}

static void render_items(Alttp* a, const uint8_t* vram) {
	render_zoom_pane(a, vram);
	for (size_t i = 0; i < ALTTP_W * ALTTP_H; ++i) a->items[i] = 0xFF000000u;
	for (int r = MENU_ROW0; r < MENU_ROW0 + ALTTP_H / 8; ++r) {
		for (int c = 0; c < 32; ++c) {
			uint16_t e = a->menu[r * 32 + c];
			if (e != BLANK) draw_tile2(a, vram, e, &a->items[(r - MENU_ROW0) * 8 * ALTTP_W + c * 8], ALTTP_W);
		}
	}
	/* Link's marker on the map until his sprite has been seen: the same
	 * ring, round a 16 x 16 middle */
	if (a->markerLink) return;
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

static const WriteRange altWrites[] = {
	{ 0x7E0202, 1, "the Y item" },
	{ 0x7E0303, 1, "the item Y uses" },
	{ 0x7E034B, 1, "the equipment debug flag" },
	{ 0x7E020B, 1, "the first debug flag" },
	{ 0x7E0016, 1, "the HUD's upload" },
	{ 0x7EF34F, 1, "the bottle selected" },
	{ 0x7EC74A, 4, "the HUD's item box, top" },
	{ 0x7EC78A, 4, "the HUD's item box, bottom" },
};

const WriteRange* alttp_write_ranges(int* n) {
	*n = (int) (sizeof altWrites / sizeof altWrites[0]);
	return altWrites;
}

int alttp_select(const Alttp* a, const uint8_t* wram, int item, Write* out, int max) {
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
		out[n++] = (Write) { 0x7E0000 | (R_BOTTLE), (uint8_t) bottle };
	} else if (item == 16 && r8(a, R_CUR_ITEM) == 16) {
		/* the bottle tapped again: the next bottle Link has, as Down does
		 * in the game's bottle menu (Hud_BottleMenu, $0D:E0DF) */
		int v = bottle - 1;
		do v = (v + 1) & 3;
		while (!r8(a, R_BOTTLES + v));
		bottle = v + 1;
		out[n++] = (Write) { 0x7E0000 | (R_BOTTLE), (uint8_t) bottle };
	}
	uint8_t equip = rom8(a, MENU_TO_EQUIP + item);
	out[n++] = (Write) { 0x7E0000 | (R_CUR_ITEM), (uint8_t) item };
	out[n++] = (Write) { 0x7E0000 | (R_EQUIP), equip };
	out[n++] = (Write) { 0x7E0000 | (R_EQ_DEBUG), (uint8_t) (equip == 5 || equip == 6 ? 0 : 2) };
	if (equip != 5 && equip != 6) out[n++] = (Write) { 0x7E0000 | (R_DEBUG_1), 0 };
	/* the HUD's item box (UpdateHUDBuffer, $0D:FB3C) and its upload */
	int level = r8(a, R_ITEMS + item - 1);
	if (item == 4) level = level != 0;
	if (item == 16) level = r8(a, R_BOTTLES + bottle - 1);
	uint32_t t = icon_table(a, item) + level * 8;
	static const uint16_t at[4] = { 0x4A, 0x4C, 0x8A, 0x8C };
	for (int i = 0; i < 4; ++i) {
		uint16_t w = rom16(a, t + i * 2);
		out[n++] = (Write) { 0x7E0000 | (R_HUD + at[i]), (uint8_t) w };
		out[n++] = (Write) { 0x7E0000 | (R_HUD + at[i] + 1), (uint8_t) (w >> 8) };
	}
	out[n++] = (Write) { 0x7E0000 | (R_HUD_UPLOAD), 1 };
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

/* the piece holding world pixel (x, y), made if needed; the caller writes
 * to it, so its compressed copy is out of date */
static uint16_t* chunk_for(Alttp* a, int w, int x, int y) {
	int i = (y / CHUNK) * CHUNKS + x / CHUNK;
	uint16_t** c = &a->chunk[w][i];
	if (!*c) *c = calloc(CHUNK * CHUNK, sizeof(uint16_t));
	a->zstale[w][i] = true;
	a->pver[w][i] = a->gen;
	return *c;
}

/* The backdrop's colour where the game adds to it (colour maths on the
 * backdrop): the sub screen's BG1 there (at its own scroll), else the
 * fixed colour; added or subtracted, halved if the game halves it (the
 * Dark World's red sky over the pyramid, area $5B: BG1 on the sub
 * screen). 0: no colour maths on the backdrop. */
static uint16_t backdrop_math(const Alttp* a, uint16_t back, uint8_t subIndex) {
	uint8_t ws = r8(a, R_CGWSEL), as = r8(a, R_CGADSUB);
	if (!(as & 0x20)) return back;
	uint16_t sub;
	bool fixed = !(ws & 2) || !subIndex;
	if (!fixed) sub = r16(a, R_PALETTE + subIndex * 2);
	else sub = (uint16_t) ((r8(a, R_COLDATA) & 31) | (r8(a, R_COLDATA + 1) & 31) << 5 | (r8(a, R_COLDATA + 2) & 31) << 10);
	int out[3];
	for (int k = 0; k < 3; ++k) {
		int m = back >> (5 * k) & 31, s = sub >> (5 * k) & 31;
		int v = as & 0x80 ? m - s : m + s;
		/* (halving applies unless the sub screen was its backdrop, the fixed colour) */
		if ((as & 0x40) && !fixed) v >>= 1;
		out[k] = v < 0 ? 0 : v > 31 ? 31 : v;
	}
	return (uint16_t) (out[0] | out[1] << 5 | out[2] << 10);
}

/* The screen's ground, and up to EDGE_REACH pixels past the screen on a
 * side where the area's edge is that close (the camera stops at an area's
 * edge, and the last squares by it are never whole on the screen; the
 * game has the rows between in video RAM already). Never past the area
 * ($8A's place, 512 or 1024 world pixels square by $0712). */
#define EDGE_REACH 32
static bool is_seen(const Alttp* a, int w, int x, int y);
static void capture(Alttp* a, const uint8_t* vram, bool scrolling) {
	a->gen += 2;
	int area = r8(a, R_AREA), w = (area & 0x40) != 0;
	int size = r16(a, R_AREA_BIG) ? 1024 : 512;
	int ax0 = (area & 7) * 512, ay0 = ((area & 0x3F) >> 3) * 512, ax1 = ax0 + size, ay1 = ay0 + size;
	int cx = r16(a, R_BG2_X), cy = r16(a, R_BG2_Y) + 1; /* line 0 shows row scroll + 1 */
	/* the part taken: the screen, out to the area's edge where it's near */
	int x0 = cx, x1 = cx + ALTTP_W, y0 = cy, y1 = cy + ALTTP_H;
	if (scrolling) {
		/* (the scroll from one area to the next, the new one's in charge:
		 * only its part of the screen) */
		x0 = x0 > ax0 ? x0 : ax0, y0 = y0 > ay0 ? y0 : ay0, x1 = x1 < ax1 ? x1 : ax1, y1 = y1 < ay1 ? y1 : ay1;
	} else if (x0 - ax0 <= EDGE_REACH) x0 = (x0 - EDGE_REACH > ax0 ? x0 - EDGE_REACH : ax0);
	if (ax1 - x1 <= EDGE_REACH) x1 = (x1 + EDGE_REACH < ax1 ? x1 + EDGE_REACH : ax1);
	if (y0 - ay0 <= EDGE_REACH) y0 = (y0 - EDGE_REACH > ay0 ? y0 - EDGE_REACH : ay0);
	if (ay1 - y1 <= EDGE_REACH) y1 = (y1 + EDGE_REACH < ay1 ? y1 + EDGE_REACH : ay1);
	x0 = x0 < 0 ? 0 : x0, y0 = y0 < 0 ? 0 : y0;
	x1 = x1 > ALTTP_WORLD ? ALTTP_WORLD : x1, y1 = y1 > ALTTP_WORLD ? ALTTP_WORLD : y1;
	if (x1 <= x0 || y1 <= y0) return;
	bool bg1 = r8(a, R_TM) & 1;
	bool subBg1 = (r8(a, R_TS) & 1) && (r8(a, R_CGADSUB) & 0x20);
	int sx = r16(a, R_BG1_X), sy = r16(a, R_BG1_Y) + 1;
	int n = x1 - x0;
	uint8_t i2[ALTTP_W + 2 * EDGE_REACH], i1[ALTTP_W + 2 * EDGE_REACH], s1[ALTTP_W + 2 * EDGE_REACH];
	for (int wy = y0; wy < y1; ++wy) {
		bg_row(vram, V_BG2_MAP, x0, wy, n, i2);
		if (bg1) bg_row(vram, V_BG1_MAP, x0, wy, n, i1);
		/* (the sub screen's BG1 at its own scroll, as far from it as BG2's) */
		if (subBg1) bg_row(vram, V_BG1_MAP, sx + (x0 - cx), sy + (wy - cy), n, s1);
		for (int k = 0; k < n; ++k) {
			int wx = x0 + k;
			int i = bg1 && i1[k] ? i1[k] : i2[k];
			uint16_t col = r16(a, R_PALETTE + i * 2);
			if (!i) col = backdrop_math(a, col, subBg1 ? s1[k] : 0);
			/* (while scrolling, only squares not seen yet: the ground already
			 * taken is never written over then) */
			if (scrolling && is_seen(a, w, wx, wy)) continue;
			uint16_t* c = chunk_for(a, w, wx, wy);
			if (c) c[(wy % CHUNK) * CHUNK + wx % CHUNK] = (uint16_t) ((col & 0x7FFF) | 0x8000);
		}
	}
	/* whole cells only */
	for (int gy = (y0 + CELL - 1) / CELL; gy < y1 / CELL && gy < CELLS; ++gy) {
		for (int gx = (x0 + CELL - 1) / CELL; gx < x1 / CELL && gx < CELLS; ++gx) a->seen[w][(gy * CELLS + gx) >> 3] |= (uint8_t) (1 << (gx & 7));
	}
	a->dirty = true;
}

void alttp_capture_now(Alttp* a, const uint8_t* wram, const uint8_t* vram) {
	memcpy(a->ram, wram, sizeof a->ram);
	a->have = true;
	capture(a, vram, false);
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

bool alttp_seen_at(const Alttp* a, int world, int x, int y) {
	return is_seen(a, world & 1, x, y);
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

/* A piece changes with its neighbours too: the fog's edge reaches across */
uint32_t alttp_piece_version(const Alttp* a, int world, int i) {
	world &= 1;
	if (i < 0 || i >= CHUNKS * CHUNKS) return 0;
	uint32_t v = 0;
	bool any = false;
	for (int dy = -1; dy <= 1; ++dy) {
		for (int dx = -1; dx <= 1; ++dx) {
			int x = i % CHUNKS + dx, y = i / CHUNKS + dy;
			if (x < 0 || y < 0 || x >= CHUNKS || y >= CHUNKS || !a->chunk[world][y * CHUNKS + x]) continue;
			any = true;
			v = v * 31 + a->pver[world][y * CHUNKS + x];
		}
	}
	return any ? v | 1 : 0;
}

/* ---- the fog's edge (ALTTP-2): the clouds puff out over the ground at
 * the edge of what's been seen, as the game's world map draws its clouds
 * round the land: round puffs, one on each unseen square by the seen
 * ones, of a size its own (most small, some large), in the clouds' body
 * colour with a light edge above left and a shaded one below right, the
 * puff's shadow on the ground below right of it; and from the edge out,
 * the clouds' body fading into the clouds' picture (so their pattern
 * doesn't end on a straight line). Only where seen squares are near. */
#define FOG_REACH 2   /* squares round a pixel that can reach it */
#define FOG_FADE 28   /* the fade into the clouds' picture, world pixels */
#define FOG_SHADOW 3  /* the shadow's offset, below right */

static bool cell_seen(const Alttp* a, int w, int gx, int gy) {
	if (gx < 0 || gy < 0 || gx >= CELLS || gy >= CELLS) return false;
	return a->seen[w][(gy * CELLS + gx) >> 3] & (1 << (gx & 7));
}

/* the puff on unseen square (gx, gy): its radius, world pixels */
static int puff_radius(int w, int gx, int gy) {
	uint32_t h = (uint32_t) gx * 73856093u ^ (uint32_t) gy * 19349663u ^ (uint32_t) w * 83492791u;
	h ^= h >> 13, h *= 0x5bd1e995u, h ^= h >> 15;
	return h % 4 == 0 ? 20 + (int) (h >> 8) % 8 : 12 + (int) (h >> 8) % 5;
}

/* A piece's squares and those round it within reach: -1 seen, else the
 * puff's radius (0: none, deep in the clouds) */
enum { FOG_S = CHUNK / CELL, FOG_M = FOG_REACH + 1, FOG_G = FOG_S + 2 * FOG_M };
typedef struct {
	int x0, y0; /* the piece's top left, world pixels */
	int8_t g[FOG_G][FOG_G];
	bool mixed[FOG_S][FOG_S];
} Fog;

/* What a piece is made from, copied (alttp_piece_job): made on any thread */
struct AltPieceJob {
	Fog fog;
	uint32_t cloud[4];
	bool hasGround;
	uint16_t ground[CHUNK * CHUNK];
};

static void fog_grid(const Alttp* a, int w, int x0, int y0, Fog* f) {
	f->x0 = x0, f->y0 = y0;
	int gx0 = x0 / CELL - FOG_M, gy0 = y0 / CELL - FOG_M;
	for (int y = 0; y < FOG_G; ++y) {
		for (int x = 0; x < FOG_G; ++x) {
			int cx = gx0 + x, cy = gy0 + y;
			if (cell_seen(a, w, cx, cy)) {
				f->g[y][x] = -1;
				continue;
			}
			/* (a puff only by seen squares: deep in the clouds there's none to see) */
			bool by = cell_seen(a, w, cx - 1, cy) || cell_seen(a, w, cx + 1, cy) || cell_seen(a, w, cx, cy - 1) || cell_seen(a, w, cx, cy + 1);
			f->g[y][x] = by ? (int8_t) puff_radius(w, cx, cy) : 0;
		}
	}
	for (int y = 0; y < FOG_S; ++y) {
		for (int x = 0; x < FOG_S; ++x) {
			int seen = 0, unseen = 0;
			for (int dy = -FOG_M; dy <= FOG_M; ++dy) {
				for (int dx = -FOG_M; dx <= FOG_M; ++dx) {
					if (f->g[y + FOG_M + dy][x + FOG_M + dx] < 0) ++seen;
					else ++unseen;
				}
			}
			f->mixed[y][x] = seen && unseen;
		}
	}
}

/* how deep (x, y) (in the piece, may be a little outside) is in the
 * clouds' puffs (> 0 inside; -1 outside all), and which way the edge
 * nearest faces: *dir > 0 below right */
static float puff_depth(const Fog* f, int x, int y, float* dir) {
	int gx = (x >= 0 ? x / CELL : -1) + FOG_M, gy = (y >= 0 ? y / CELL : -1) + FOG_M;
	float best = -1, px = x + 0.5f, py = y + 0.5f;
	*dir = 0;
	for (int cy = gy - FOG_REACH; cy <= gy + FOG_REACH; ++cy) {
		if (cy < 0 || cy >= FOG_G) continue;
		for (int cx = gx - FOG_REACH; cx <= gx + FOG_REACH; ++cx) {
			if (cx < 0 || cx >= FOG_G) continue;
			int r = f->g[cy][cx];
			if (r <= 0) continue;
			float dx = px - ((cx - FOG_M) * CELL + CELL / 2), dy = py - ((cy - FOG_M) * CELL + CELL / 2);
			float d2 = dx * dx + dy * dy;
			if (d2 >= (float) (r * r)) continue;
			float d = r - sqrtf(d2);
			if (d > best) best = d, *dir = dx + dy;
		}
	}
	return best;
}

/* distance from (x, y) to the nearest seen square within reach */
static float seen_distance(const Fog* f, int x, int y) {
	int gx = x / CELL + FOG_M, gy = y / CELL + FOG_M;
	float best = 1e9f;
	for (int cy = gy - FOG_REACH; cy <= gy + FOG_REACH; ++cy) {
		for (int cx = gx - FOG_REACH; cx <= gx + FOG_REACH; ++cx) {
			if (f->g[cy][cx] >= 0) continue;
			int x0 = (cx - FOG_M) * CELL, y0 = (cy - FOG_M) * CELL;
			float dx = x < x0 ? x0 - x : x >= x0 + CELL ? x - (x0 + CELL) + 1 : 0;
			float dy = y < y0 ? y0 - y : y >= y0 + CELL ? y - (y0 + CELL) + 1 : 0;
			float d2 = dx * dx + dy * dy;
			if (d2 < best) best = d2;
		}
	}
	return sqrtf(best);
}

static uint32_t with_alpha(uint32_t c, unsigned al) {
	return (c & 0xFFFFFFu) | (uint32_t) al << 24;
}

/* one world pixel of the map as drawn ((x, y) in the piece): the ground,
 * the fog's edge over it, or the fade into the clouds (alpha 0: the
 * clouds' picture shows) */
static uint32_t map_pixel(const AltPieceJob* j, int x, int y) {
	const Fog* f = &j->fog;
	const uint16_t* c = j->hasGround ? j->ground : NULL;
	bool seen = f->g[y / CELL + FOG_M][x / CELL + FOG_M] < 0;
	uint16_t g = seen && c ? c[y * CHUNK + x] : 0;
	if (!f->mixed[y / CELL][x / CELL]) return g ? abgr(g) : 0;
	const uint32_t* cl = j->cloud;
	if (g) {
		float dir, d = puff_depth(f, x, y, &dir);
		if (d > 0) {
			if (d < 1.5f) return dir > 0 ? cl[3] : cl[1]; /* the rim, lit above left */
			if (d < 3 && dir > 0) return cl[2];
			return cl[0];
		}
		/* the puffs' shadow */
		if (puff_depth(f, x - FOG_SHADOW, y - FOG_SHADOW, &dir) > 0) {
			uint32_t v = abgr(g);
			return 0xFF000000u | ((v >> 16 & 0xFF) * 3 / 4) << 16 | ((v >> 8 & 0xFF) * 3 / 4) << 8 | (v & 0xFF) * 3 / 4;
		}
		return abgr(g);
	}
	if (seen) return 0; /* (seen, nothing taken: not drawn) */
	/* unseen: the clouds' body by the edge, fading into their picture */
	float d = seen_distance(f, x, y);
	if (d >= FOG_FADE) return 0;
	float t = d / FOG_FADE;
	return with_alpha(cl[0], (unsigned) lroundf(255 * (1 - t * t)));
}

AltPieceJob* alttp_piece_job(const Alttp* a, int world, int i) {
	world &= 1;
	if (i < 0 || i >= CHUNKS * CHUNKS) return NULL;
	AltPieceJob* j = malloc(sizeof *j);
	if (!j) return NULL;
	fog_grid(a, world, (i % CHUNKS) * CHUNK, (i / CHUNKS) * CHUNK, &j->fog);
	memcpy(j->cloud, a->cloud[world], sizeof j->cloud);
	j->hasGround = a->chunk[world][i] != NULL;
	if (j->hasGround) memcpy(j->ground, a->chunk[world][i], sizeof j->ground);
	return j;
}

void alttp_piece_make(const AltPieceJob* j, int level, uint32_t* out) {
	level = level < 0 ? 0 : level > 4 ? 4 : level;
	int b = 1 << level, n = CHUNK >> level;
	for (int oy = 0; oy < n; ++oy) {
		uint32_t* o = out + (size_t) oy * n;
		for (int ox = 0; ox < n; ++ox) {
			if (b == 1) {
				o[ox] = map_pixel(j, ox, oy);
				continue;
			}
			/* the block's average, weighted by how much of each is there */
			unsigned r = 0, g = 0, bl = 0, al = 0;
			for (int y = oy * b; y < oy * b + b; ++y) {
				for (int x = ox * b; x < ox * b + b; ++x) {
					uint32_t v = map_pixel(j, x, y);
					unsigned va = v >> 24;
					r += (v & 0xFF) * va, g += (v >> 8 & 0xFF) * va, bl += (v >> 16 & 0xFF) * va, al += va;
				}
			}
			if (!al) {
				o[ox] = 0;
				continue;
			}
			o[ox] = (uint32_t) (al / (unsigned) (b * b)) << 24 | (bl / al) << 16 | (g / al) << 8 | r / al;
		}
	}
}

void alttp_piece(const Alttp* a, int world, int i, int level, uint32_t* out) {
	AltPieceJob* j = alttp_piece_job(a, world, i);
	if (j) alttp_piece_make(j, level, out);
	else memset(out, 0, (size_t) (CHUNK >> level) * (CHUNK >> level) * 4);
	free(j);
}

const uint32_t* alttp_sky(const Alttp* a, int world) {
	return a->sky[world & 1];
}

void alttp_link(const Alttp* a, int* world, int* x, int* y) {
	*world = a->linkWorld, *x = a->linkX, *y = a->linkY;
}

/* ---- Link's marker on the map: his sprite as the game draws it now */

/* a sprite of the game's (size 8 or 16, 4 bits a pixel) from video RAM
 * into the marker, its top left at (ox, oy); attr: its vhoopppN byte */
static void draw_sprite(const Alttp* a, uint32_t* out, const uint8_t* vram, int size, int c, uint8_t attr, int ox, int oy) {
	int pal = (attr >> 1) & 7;
	for (int py = 0; py < size; ++py) {
		int sy = attr & 0x80 ? size - 1 - py : py;
		for (int px = 0; px < size; ++px) {
			int sx = attr & 0x40 ? size - 1 - px : px;
			int x = ox + px, y = oy + py;
			if (x < 0 || x >= 32 || y < 0 || y >= 32) continue;
			uint32_t t = V_OBJ_CHR + (uint32_t) ((c + (sx >> 3) + (sy >> 3) * 16) & 0x1FF) * 16;
			uint16_t p01 = vword(vram, t + (sy & 7)), p23 = vword(vram, t + 8 + (sy & 7));
			int b = 7 - (sx & 7);
			int ci = (p01 >> b & 1) | (p01 >> (8 + b) & 1) << 1 | (p23 >> b & 1) << 2 | (p23 >> (8 + b) & 1) << 3;
			if (ci) out[y * 32 + x] = abgr(r16(a, R_PALETTE + (128 + pal * 16 + ci) * 2));
		}
	}
}

/* Link's sprites as the game draws him now: the 12 slots of the sprite list
 * from $0352's place (his body, head, shield, sword, the item in his
 * hand); his own tiles are the first 32 (0x00 head, 0x02 body), which
 * the game fills with his pose each frame. Drawn round his body as they
 * are on screen. True if the marker changed. */
#define LINK_SLOTS 12
static bool link_take(Alttp* a, const uint8_t* vram, const uint8_t* list, int first, int n, const int* x, const int* y,
                      const bool* shown, int body, int d, bool standing);
static uint32_t link_key(const Alttp* a, const uint8_t* vram, const uint8_t* list, int first, int n, const int* x, const int* y,
                         const bool* shown, int body);
static void link_draw(const Alttp* a, const uint8_t* vram, const uint8_t* list, int first, int n, const int* x, const int* y,
                      const bool* shown, int body, uint32_t* pic);

static bool link_marker(Alttp* a, const uint8_t* vram) {
	/* the copy the shown picture was made from, if the update before
	 * was the frame before (else this frame's: a frame out, at most) */
	uint8_t f = r8(a, R_FRAME);
	if (f != a->oamFrame[1]) {
		memcpy(a->oam[0], a->oam[1], sizeof a->oam[0]), a->oamFrame[0] = a->oamFrame[1];
		memcpy(a->oam[1], a->ram + R_OAM, OAM_COPY), memcpy(a->oam[1] + OAM_COPY, a->ram + R_SORT_OFFSET, 2);
		a->oamFrame[1] = f;
	}
	const uint8_t* list = a->oam[a->oamFrame[0] == (uint8_t) (f - 1) ? 0 : 1];
	int first = ((list[OAM_COPY] | list[OAM_COPY + 1] << 8) >> 2) & 0x7F;
	int n = 128 - first < LINK_SLOTS ? 128 - first : LINK_SLOTS;
	int x[LINK_SLOTS], y[LINK_SLOTS], body = -1;
	bool shown[LINK_SLOTS];
	for (int i = 0; i < n; ++i) {
		const uint8_t* o = list + (first + i) * 4;
		uint8_t hi = list[R_OAM_HIGH - R_OAM + first + i];
		x[i] = o[0] | (hi & 1) << 8, y[i] = o[1];
		if (x[i] >= 256) x[i] -= 512;
		shown[i] = o[2] < 0x20 && !(o[3] & 1) && y[i] < 0xE0 && x[i] > -16 && x[i] < 256;
		if (shown[i] && o[2] == 0x02 && (hi & 2) && body < 0) body = i;
	}
	/* ALTTP-4: as he is while he walks or stands (his gear, his tunic
	 * too), but not while he swings his sword or uses an item: then his
	 * picture standing that way (taken while he stands still, else the
	 * first seen that way) */
	int d = (r8(a, R_FACING) >> 1) & 3;
	uint8_t handler = r8(a, 0x5D); /* 0 on foot, 4 swimming, $17 a bunny */
	bool busy = r8(a, 0x3A) || r8(a, 0x4D) || r8(a, 0x0301) || r8(a, 0x0308) || r8(a, 0x037A) || r8(a, 0x02DA) ||
	            (handler != 0x00 && handler != 0x04 && handler != 0x17);
	bool standing = !busy && !r8(a, 0x27) && !r8(a, 0x28) && !r8(a, 0x2E);
	/* (the same for two frames: the sprite list is the frame before's,
	 * the RAM this frame's) */
	bool still = standing && a->stoodDir == d, held = a->lastDir == d, wasBusy = a->wasBusy;
	a->stoodDir = standing ? d : -1, a->lastDir = d, a->wasBusy = busy;
	if (body >= 0 && (still || (held && !wasBusy && !a->dirHave[d]))) link_take(a, vram, list, first, n, x, y, shown, body, d, still);
	const uint32_t* show = NULL;
	if (body >= 0 && held && !busy && !wasBusy) {
		/* walking or standing: as on the screen */
		uint32_t k = link_key(a, vram, list, first, n, x, y, shown, body);
		if (k != a->liveKey) link_draw(a, vram, list, first, n, x, y, shown, body, a->livePx), a->liveKey = k;
		show = a->livePx;
	} else if (a->dirHave[d] && (busy || !held)) {
		show = a->dirPx[d];
	}
	if (!show || (a->markerLink && !memcmp(a->marker, show, sizeof a->marker))) return false;
	memcpy(a->marker, show, sizeof a->marker);
	a->markerLink = true;
	return true;
}

/* Link's picture for way d from this frame's sprites (if it differs from
 * the one kept); true if it changed */
/* what Link's picture is made from: his tiles, the sprite palettes, his
 * sprites' places round his body */
static uint32_t link_key(const Alttp* a, const uint8_t* vram, const uint8_t* list, int first, int n, const int* x, const int* y,
                         const bool* shown, int body) {
	uint32_t k = crc32(0, vram + V_OBJ_CHR * 2, 0x400);
	k = crc32(k, a->ram + R_PALETTE + 128 * 2, 256);
	for (int i = 0; i < n; ++i) {
		if (!shown[i]) continue;
		const uint8_t* o = list + (first + i) * 4;
		uint8_t e[5] = { o[2], o[3], (uint8_t) (x[i] - x[body]), (uint8_t) (y[i] - y[body]), list[R_OAM_HIGH - R_OAM + first + i] & 2 };
		k = crc32(k, e, sizeof e);
	}
	return k | 1;
}

/* Link's picture (32 x 32) from this frame's sprites */
static void link_draw(const Alttp* a, const uint8_t* vram, const uint8_t* list, int first, int n, const int* x, const int* y,
                      const bool* shown, int body, uint32_t* pic) {
	for (int i = 0; i < 32 * 32; ++i) pic[i] = 0;
	/* the body's top left where it is round Link's middle (alttp_link):
	 * 8 pixels right of Link's x and a line below y + 8; the first slots
	 * over the later ones, as the SNES shows them */
	for (int i = n - 1; i >= 0; --i) {
		if (!shown[i]) continue;
		const uint8_t* o = list + (first + i) * 4;
		int mx = 8 + x[i] - x[body], my = 13 + y[i] - y[body];
		if (list[R_OAM_HIGH - R_OAM + first + i] & 2) draw_sprite(a, pic, vram, 16, o[2], o[3], mx, my);
		else draw_sprite(a, pic, vram, 8, o[2], o[3], mx, my);
	}
}

/* His picture for way d taken again if it differs from the one kept */
static bool link_take(Alttp* a, const uint8_t* vram, const uint8_t* list, int first, int n, const int* x, const int* y,
                      const bool* shown, int body, int d, bool standing) {
	uint32_t k = link_key(a, vram, list, first, n, x, y, shown, body);
	if (a->dirHave[d] && k == a->dirKey[d]) {
		if (standing) a->dirHave[d] = 2;
		return false;
	}
	a->dirKey[d] = k, a->dirHave[d] = standing ? 2 : 1;
	link_draw(a, vram, list, first, n, x, y, shown, body, a->dirPx[d]);
	return true;
}

/* ---- the world map's goal markers (ALTTP-11), as WorldMap_HandleSprites
 * ($0A:BF66) chooses them: by the stage ($7EF3C7), none once $7EF2DB bit
 * $20 is set or in the other world (stages 6 and up are the Dark
 * World's); a pendant or crystal won, its marker gone; blinking with the
 * frame counter as on the game's map */

/* a sprite tile of the markers' ($60 to $7F) into a 16 x 16 picture */
static void goal_sprite(Alttp* a, uint32_t* out, int size, int c, uint8_t attr) {
	int pal = (attr >> 1) & 7;
	for (int py = 0; py < size; ++py) {
		int sy = attr & 0x80 ? size - 1 - py : py;
		for (int px = 0; px < size; ++px) {
			int sx = attr & 0x40 ? size - 1 - px : px;
			int t = (c - 0x60 + (sx >> 3) + (sy >> 3) * 16) & 31;
			uint16_t p01 = a->goalChr[t * 16 + (sy & 7)], p23 = a->goalChr[t * 16 + 8 + (sy & 7)];
			int b = 7 - (sx & 7);
			int ci = (p01 >> b & 1) | (p01 >> (8 + b) & 1) << 1 | (p23 >> b & 1) << 2 | (p23 >> (8 + b) & 1) << 3;
			if (ci) out[py * 16 + px] = abgr(r16(a, R_PALETTE + (128 + pal * 16 + ci) * 2));
		}
	}
}

/* The markers shown now; true if they or their pictures changed */
static bool goals_update(Alttp* a) {
	AltGoal g[ALTTP_GOAL_MAX];
	uint8_t chr[ALTTP_GOAL_MAX], attr[ALTTP_GOAL_MAX];
	int n = 0, k = r8(a, R_MAP_STAGE), f = r8(a, R_FRAME);
	bool dark = k >= 6;
	if (k <= 8 && !(r8(a, R_OW_EVENT_5B) & 0x20) && dark == (a->linkWorld == 1)) {
		for (int i = 0; i < 7; ++i) { /* (the game's 7 goal markers) */
			int x = rom16(a, GOAL_POSX + i * 0x24 + k * 2), y = rom16(a, GOAL_POSY + i * 0x24 + k * 2);
			if (x & 0x8000) continue;
			if (k == 3 && i < 3 && (r8(a, R_PENDANTS) & rom8(a, PENDANT_BITS + i))) continue;
			if (k == 7 && (r8(a, R_CRYSTALS) & rom8(a, CRYSTAL_BITS + i))) continue;
			uint8_t fl = rom8(a, GOAL_TILE + i * 0x12 + k * 2), ch = rom8(a, GOAL_TILE + i * 0x12 + k * 2 + 1);
			if (ch && ch != 0x64 && (f & 0x10)) continue; /* (blinks: off half the time) */
			if (ch) x -= 4, y -= 4;
			AltGoal* o = &g[n];
			o->x = x, o->y = y;
			if (!ch) {
				/* the red X, turning */
				chr[n] = rom8(a, GOAL_RED_X + ((f >> 3) & 3)), attr[n] = 0x32, o->size = 8, o->dx = o->dy = -4;
			} else if (ch == 0x64 && !(f & 0x10)) {
				/* a crystal, blinking with its number */
				chr[n] = rom8(a, GOAL_NUMBERS + i), attr[n] = 0x32, o->size = 8, o->dx = o->dy = 0;
			} else {
				chr[n] = ch, attr[n] = fl, o->size = 16, o->dx = o->dy = -4;
			}
			++n;
		}
	}
	/* the mirror's portal, turning, where Link left the Dark World by the
	 * mirror (the game's own map shows it in the Light World) */
	int px = r8(a, R_PORTAL_X) | r8(a, R_PORTAL_X + 0x10) << 8, py = r8(a, R_PORTAL_X + 0x20) | r8(a, R_PORTAL_X + 0x30) << 8;
	if ((px | py) && a->linkWorld == 0 && n < ALTTP_GOAL_MAX) {
		AltGoal* o = &g[n];
		o->x = px, o->y = py, o->size = 16, o->dx = o->dy = -4;
		chr[n] = 0x6A, attr[n] = rom8(a, PORTAL_PROPS + ((f >> 1) & 3));
		++n;
	}
	uint32_t key = crc32(0, (const Bytef*) g, (uInt) (sizeof g[0] * (size_t) n));
	key = crc32(key, chr, (uInt) n), key = crc32(key, attr, (uInt) n);
	key = crc32(key, a->ram + R_PALETTE + 256, 256) ^ (uint32_t) n;
	key |= 1;
	if (key == a->goalKey) return false;
	a->goalKey = key;
	memcpy(a->goal, g, sizeof g[0] * (size_t) n);
	a->nGoals = n;
	memset(a->goalPx, 0, sizeof a->goalPx);
	uint32_t tmp[16 * 16];
	for (int i = 0; i < n; ++i) {
		memset(tmp, 0, sizeof tmp);
		goal_sprite(a, tmp, g[i].size, chr[i], attr[i]);
		for (int y = 0; y < 16; ++y) memcpy(&a->goalPx[y * ALTTP_GOAL_MAX * 16 + i * 16], &tmp[y * 16], 16 * 4);
	}
	return true;
}

int alttp_goals(const Alttp* a, const AltGoal** list) {
	*list = a->goal;
	return a->nGoals;
}

const uint32_t* alttp_goal_pictures(const Alttp* a) {
	return a->goalPx;
}

const uint32_t* alttp_marker(const Alttp* a) {
	return a->marker;
}

bool alttp_marker_on(const Alttp* a) {
	/* (the ring blinks as the game's own; Link's sprite stays) */
	return a->markerLink || (r8(a, R_FRAME) & 0x10) != 0;
}

/* ---- the game's files (ALTTP-2): a file erased on the file screen's
 * Erase (module $03) or made by its Copy (module $02), seen in the
 * cartridge RAM: each file is $500 bytes from $500 x (file - 1), valid
 * while its word at $3E5 is $55AA; a copy is byte for byte its source */
static void slots_watch(Alttp* a, const uint8_t* sram, uint8_t module) {
	bool valid[3];
	for (int k = 0; k < 3; ++k) valid[k] = (sram[k * 0x500 + 0x3E5] | sram[k * 0x500 + 0x3E6] << 8) == 0x55AA;
	for (int k = 0; k < 3 && a->slotsKnown; ++k) {
		if (a->nSlotEv >= 8) break;
		if (a->slotValid[k] && !valid[k] && module == 0x03) {
			a->slotEv[a->nSlotEv++] = (AltSlotEvent) { ALTTP_FILE_ERASED, k + 1, 0 };
		} else if (!a->slotValid[k] && valid[k] && module == 0x02) {
			for (int s = 0; s < 3; ++s) {
				if (s != k && valid[s] && !memcmp(sram + s * 0x500, sram + k * 0x500, 0x500)) {
					a->slotEv[a->nSlotEv++] = (AltSlotEvent) { ALTTP_FILE_COPIED, k + 1, s + 1 };
					break;
				}
			}
		}
	}
	memcpy(a->slotValid, valid, sizeof valid);
	a->slotsKnown = true;
}

int alttp_take_file_events(Alttp* a, AltSlotEvent* out, int max) {
	int n = a->nSlotEv < max ? a->nSlotEv : max;
	memcpy(out, a->slotEv, sizeof out[0] * (size_t) n);
	a->nSlotEv = 0;
	return n;
}

/* ---- once a pass */

int alttp_update(Alttp* a, const uint8_t* wram, const uint8_t* vram, const uint8_t* sram) {
	memcpy(a->ram, wram, sizeof a->ram);
	a->have = true;
	++a->updates;
	if (r8(a, R_MODULE) == 0x00 && r8(a, R_SUBMODULE) == 0x05 && r8(a, R_TITLE_FADE) != a->titleCount)
		a->titleCount = r8(a, R_TITLE_FADE), a->titleCountAt = a->updates;
	int ch = 0;
	uint8_t m = r8(a, R_MODULE);
	if (m == 0x04) a->namingSeen = true; /* a new file: its record starts empty */
	if (alttp_playing(a)) a->played = true;
	if (sram) {
		int before = a->nSlotEv;
		slots_watch(a, sram, m);
		if (a->nSlotEv != before) ch |= ALTTP_FILES;
	}
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
			capture(a, vram, false), ch |= ALTTP_MAP;
			a->takenX = cx, a->takenY = cy, a->takenWorld = w, a->takenAge = 0;
		}
		if (r8(a, R_AREA) != a->lastArea) a->lastArea = r8(a, R_AREA), ch |= ALTTP_AREA;
		if (vram && link_marker(a, vram)) ch |= ALTTP_MARKER;
	} else if (vram && area_scrolling(a)) {
		/* the scroll from one area to the next shows the strip by the
		 * boundary that the screen never shows whole otherwise */
		capture(a, vram, true), ch |= ALTTP_MAP;
	}
	if (goals_update(a)) ch |= ALTTP_GOALS;
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

bool alttp_title(const Alttp* a) {
	return a->have && r8(a, R_MODULE) == 0x00;
}

/* The game's title (module 0): the ZELDA lettering fades in (submodule 5:
 * its countdown at $7EC007 a step every 3 frames), then the sword comes
 * down (submodule 6 on: $C8 from -130 to 30, 16 a frame; drawn the
 * frame after at $C8 - 16 + 8, its first sprite 8 below $C8's place
 * less 8). Before it starts, where it would be: as many frames ahead as
 * the countdown has left. */
bool alttp_title_sword(const Alttp* a, int* y) {
	if (!alttp_title(a)) return false;
	uint8_t sub = r8(a, R_SUBMODULE);
	if (sub == 0x05) {
		int left = 3 * (a->titleCount - 1) + (3 - (a->updates - a->titleCountAt));
		*y = -122 - 16 * (left > 0 ? left : 0);
		return a->titleCount > 0;
	}
	if (sub < 0x06 || sub > 0x08) return false;
	int16_t p = (int16_t) r16(a, R_TITLE_SWORD);
	*y = p == 30 ? 38 : p - 8;
	return true;
}

bool alttp_game_over(const Alttp* a) {
	return a->have && r8(a, R_MODULE) == 0x12;
}

bool alttp_in_special(const Alttp* a) {
	uint8_t m = r8(a, R_MODULE);
	return a->have && (m == 0x09 || m == 0x0B) && !r8(a, R_INDOORS) && r8(a, R_AREA) >= 0x80;
}

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

/* ---- the seen record's file (pack's format, below) */

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

bool alttp_stale_piece(const Alttp* a, int* world, int* piece, uint16_t* out, uint32_t* ver) {
	for (int w = 0; w < 2; ++w) {
		for (int i = 0; i < CHUNKS * CHUNKS; ++i) {
			if (!a->chunk[w][i] || !(a->zstale[w][i] || !a->zchunk[w][i])) continue;
			memcpy(out, a->chunk[w][i], CHUNK * CHUNK * 2);
			*world = w, *piece = i, *ver = a->pver[w][i];
			return true;
		}
	}
	return false;
}

void alttp_put_compressed(Alttp* a, int world, int piece, uint32_t ver, uint8_t* z, uint32_t n) {
	world &= 1;
	if (piece < 0 || piece >= CHUNKS * CHUNKS || !a->chunk[world][piece] || a->pver[world][piece] != ver) {
		free(z); /* (changed since: made again later) */
		return;
	}
	free(a->zchunk[world][piece]);
	a->zchunk[world][piece] = z, a->zlen[world][piece] = n, a->zstale[world][piece] = false;
}

/* The record packed (the file's and the state section's format):
 * "SDSALTP2"; each world's seen squares (a bit each, 8 KB); then for each
 * world the pieces present (64 bits) and each one as a 32-bit length and
 * its 512 x 512 SNES colours (little-endian words) compressed with zlib.
 * Made in three steps so the slow part (compressing the pieces changed
 * since their last compressed copy, writing the file) can run on another
 * thread: job_take copies the record (on the game's thread: memory copies
 * only), job_pack packs the copy (any thread), job_give_back hands the
 * pieces it compressed back to the record (the game's thread). */
#define SEEN_BYTES (CELLS * CELLS / 8)
struct AltSaveJob {
	uint8_t seen[2][SEEN_BYTES];
	bool has[2][CHUNKS * CHUNKS];
	uint8_t* z[2][CHUNKS * CHUNKS]; /* the compressed piece (a copy, or made by job_pack) */
	uint32_t zl[2][CHUNKS * CHUNKS];
	uint16_t* raw[2][CHUNKS * CHUNKS]; /* a piece to compress, a copy */
	uint32_t ver[2][CHUNKS * CHUNKS];
	bool made[2][CHUNKS * CHUNKS]; /* compressed by job_pack */
};

static void job_free(AltSaveJob* j) {
	if (!j) return;
	for (int w = 0; w < 2; ++w) {
		for (int i = 0; i < CHUNKS * CHUNKS; ++i) free(j->z[w][i]), free(j->raw[w][i]);
	}
	free(j);
}

static AltSaveJob* job_take(const Alttp* a) {
	AltSaveJob* j = calloc(1, sizeof *j);
	if (!j) return NULL;
	memcpy(j->seen, a->seen, sizeof j->seen);
	for (int w = 0; w < 2; ++w) {
		for (int i = 0; i < CHUNKS * CHUNKS; ++i) {
			if (!a->chunk[w][i]) continue;
			j->has[w][i] = true;
			if (a->zstale[w][i] || !a->zchunk[w][i]) {
				if (!(j->raw[w][i] = malloc(CHUNK * CHUNK * 2))) goto fail;
				memcpy(j->raw[w][i], a->chunk[w][i], CHUNK * CHUNK * 2);
				j->ver[w][i] = a->pver[w][i];
			} else {
				if (!(j->z[w][i] = malloc(a->zlen[w][i]))) goto fail;
				memcpy(j->z[w][i], a->zchunk[w][i], a->zlen[w][i]);
				j->zl[w][i] = a->zlen[w][i];
			}
		}
	}
	return j;
fail:
	job_free(j);
	return NULL;
}

static uint8_t* job_pack(AltSaveJob* j, size_t* n) {
	size_t size = 8 + 2 * SEEN_BYTES;
	for (int w = 0; w < 2; ++w) {
		size += 8;
		for (int i = 0; i < CHUNKS * CHUNKS; ++i) {
			if (!j->has[w][i]) continue;
			if (j->raw[w][i]) {
				/* (pieces are in memory as host words: little-endian on every target) */
				uLongf zn = compressBound(CHUNK * CHUNK * 2);
				uint8_t* z = malloc(zn);
				if (!z || compress2(z, &zn, (const Bytef*) j->raw[w][i], CHUNK * CHUNK * 2, 6) != Z_OK) {
					free(z);
					return NULL;
				}
				free(j->raw[w][i]), j->raw[w][i] = NULL;
				j->z[w][i] = z, j->zl[w][i] = (uint32_t) zn, j->made[w][i] = true;
			}
			size += 4 + j->zl[w][i];
		}
	}
	uint8_t* out = malloc(size);
	if (!out) return NULL;
	size_t at = 0;
	memcpy(out, "SDSALTP2", 8), at = 8;
	for (int w = 0; w < 2; ++w) memcpy(out + at, j->seen[w], SEEN_BYTES), at += SEEN_BYTES;
	for (int w = 0; w < 2; ++w) {
		uint64_t mask = 0;
		for (int i = 0; i < CHUNKS * CHUNKS; ++i) mask |= (uint64_t) j->has[w][i] << i;
		for (int k = 0; k < 8; ++k) out[at++] = (uint8_t) (mask >> (k * 8));
		for (int i = 0; i < CHUNKS * CHUNKS; ++i) {
			if (!j->has[w][i]) continue;
			uint32_t zl = j->zl[w][i];
			for (int k = 0; k < 4; ++k) out[at++] = (uint8_t) (zl >> (k * 8));
			memcpy(out + at, j->z[w][i], zl), at += zl;
		}
	}
	*n = at;
	return out;
}

static void job_give_back(Alttp* a, AltSaveJob* j) {
	if (!j) return;
	for (int w = 0; w < 2; ++w) {
		for (int i = 0; i < CHUNKS * CHUNKS; ++i) {
			if (!j->made[w][i]) continue;
			/* (kept if the piece hasn't changed since: alttp_put_compressed checks) */
			alttp_put_compressed(a, w, i, j->ver[w][i], j->z[w][i], j->zl[w][i]);
			j->z[w][i] = NULL;
		}
	}
	job_free(j);
}

static uint8_t* pack(Alttp* a, size_t* n) {
	AltSaveJob* j = job_take(a);
	uint8_t* out = j ? job_pack(j, n) : NULL;
	job_give_back(a, j);
	return out;
}

/* A packed record into seen and chunk (empty on entry; the caller frees
 * what's there when it fails too). zchunk / zlen (may be NULL): keep each
 * piece's compressed copy too. */
static bool unpack(const uint8_t* z, size_t n, uint8_t seen[2][SEEN_BYTES], uint16_t* chunk[2][CHUNKS * CHUNKS],
                   uint8_t* zchunk[2][CHUNKS * CHUNKS], uint32_t zlen[2][CHUNKS * CHUNKS]) {
	if (n < 8 + 2 * SEEN_BYTES + 16 || memcmp(z, "SDSALTP2", 8)) return false;
	size_t at = 8;
	for (int w = 0; w < 2; ++w) memcpy(seen[w], z + at, SEEN_BYTES), at += SEEN_BYTES;
	for (int w = 0; w < 2; ++w) {
		if (at + 8 > n) return false;
		uint64_t mask = 0;
		for (int k = 0; k < 8; ++k) mask |= (uint64_t) z[at++] << (k * 8);
		for (int i = 0; i < CHUNKS * CHUNKS; ++i) {
			if (!(mask >> i & 1)) continue;
			if (at + 4 > n) return false;
			uint32_t zl = (uint32_t) (z[at] | z[at + 1] << 8 | z[at + 2] << 16 | (uint32_t) z[at + 3] << 24);
			at += 4;
			if (zl > n - at || !(chunk[w][i] = malloc(CHUNK * CHUNK * 2))) return false;
			uLongf dl = CHUNK * CHUNK * 2;
			if (uncompress((Bytef*) chunk[w][i], &dl, z + at, zl) != Z_OK || dl != CHUNK * CHUNK * 2) return false;
			if (zchunk && (zchunk[w][i] = malloc(zl))) memcpy(zchunk[w][i], z + at, zl), zlen[w][i] = zl;
			at += zl;
		}
	}
	return true;
}

AltSaveJob* alttp_save_begin(Alttp* a) {
	AltSaveJob* j = job_take(a);
	if (j) a->dirty = false;
	return j;
}

bool alttp_save_write(AltSaveJob* j, const char* path) {
	size_t n = 0;
	uint8_t* z = j ? job_pack(j, &n) : NULL;
	bool ok = false;
	if (z) {
		char tmp[1100];
		snprintf(tmp, sizeof tmp, "%s.tmp", path);
		FILE* f = fopen(tmp, "wb");
		ok = f && fwrite(z, 1, n, f) == n;
		if (f && fclose(f) != 0) ok = false;
		ok = ok && rename(tmp, path) == 0;
	}
	free(z);
	return ok;
}

void alttp_save_end(Alttp* a, AltSaveJob* j, bool ok) {
	if (!ok) a->dirty = true;
	job_give_back(a, j);
}

bool alttp_save_seen(Alttp* a, const char* path) {
	AltSaveJob* j = alttp_save_begin(a);
	bool ok = j && alttp_save_write(j, path);
	alttp_save_end(a, j, ok);
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
	bool ok = z && fread(z, 1, (size_t) n, f) == (size_t) n;
	fclose(f);
	ok = ok && unpack(z, (size_t) n, a->seen, a->chunk, a->zchunk, a->zlen);
	free(z);
	if (!ok) clear_seen(a);
	changed_all(a);
	return ok;
}

void* alttp_seen_pack(Alttp* a, size_t* n) {
	return pack(a, n);
}

bool alttp_seen_merge(Alttp* a, const void* data, size_t n) {
	static uint8_t seen[2][SEEN_BYTES];
	uint16_t* chunk[2][CHUNKS * CHUNKS];
	memset(seen, 0, sizeof seen);
	memset(chunk, 0, sizeof chunk);
	bool ok = unpack(data, n, seen, chunk, NULL, NULL);
	int added = 0;
	a->gen += 2;
	for (int w = 0; w < 2 && ok; ++w) {
		for (int gy = 0; gy < CELLS; ++gy) {
			for (int gx = 0; gx < CELLS; ++gx) {
				int bit = 1 << (gx & 7), byte = (gy * CELLS + gx) >> 3;
				if (!(seen[w][byte] & bit) || (a->seen[w][byte] & bit)) continue;
				/* a square only the state had seen: its pixels, then seen */
				const uint16_t* from = chunk[w][(gy * CELL / CHUNK) * CHUNKS + gx * CELL / CHUNK];
				uint16_t* to = from ? chunk_for(a, w, gx * CELL, gy * CELL) : NULL;
				if (!to) continue;
				for (int y = 0; y < CELL; ++y) {
					int o = ((gy * CELL + y) % CHUNK) * CHUNK + (gx * CELL) % CHUNK;
					memcpy(to + o, from + o, CELL * 2);
				}
				a->seen[w][byte] |= (uint8_t) bit;
				++added;
			}
		}
	}
	for (int w = 0; w < 2; ++w) {
		for (int i = 0; i < CHUNKS * CHUNKS; ++i) free(chunk[w][i]);
	}
	if (added) a->dirty = true;
	return ok;
}

/* A Link to the Past's dungeon map (ALTTP-5, ALTTP-6), drawn by SNES DS as
 * the game draws it when X is pressed in a dungeon (Module0E_03_01_DrawMap,
 * $0A:E0DC), without the game opening it. The Python references are
 * tools/alttp_dungeon.py (tilemaps, sprites, the picture: pixel for pixel
 * as the game's in 74 dumps) and tools/alttp_gfx.py (the tiles); addresses
 * and formats in docs/games/a-link-to-the-past.md. */
#include "alttp_dungeon.h"

#include <stdlib.h>
#include <string.h>

/* the tiles: GFXSheetPointers and the tilesets */
#define BG_BANK 0x00CF80u
#define BG_HIGH 0x00D05Fu
#define BG_LOW 0x00D13Eu
#define SPR_BANK 0x00CFF3u
#define SPR_HIGH 0x00D0D2u
#define SPR_LOW 0x00D1B1u
#define MAIN_TILESETS 0x00E073u
#define AUX_TILESETS 0x00DD97u
#define SPRITE_TILESETS 0x00DB57u
#define MAP_MAIN 0x20 /* the dungeon map's tilesets ($0A:E0E4) */
#define MAP_AUX 0x40
#define LINK_HEAD 0x10A9C0u /* Link's head: tiles 0, 1 (and 16, 17 a row below) */
/* the tilemaps */
#define FRAME_STRIPES 0x0EDA9Cu
#define DUNGEON_LEVEL 0x0AE196u
#define LEVEL_TOP 0x0AE15Au
#define LEVEL_BOTTOM 0x0AE168u
#define NUMERALS_TOP 0x0AE176u
#define NUMERALS_BOTTOM 0x0AE186u
#define BACKDROP_POSITION 0x0AE1E1u
#define GRADIENT_TILES 0x0AEFD1u
#define MOUNTAIN_STRIPES 0x0AEFDFu
#define BOX_TILES 0x0AE2E5u
#define FLOOR_COUNT 0x0AF5D9u
#define ROOM_POINTERS 0x0AF605u
#define ROOM_ORDER_POINTERS 0x0AFBE4u
#define ROOM_TEMPLATES 0x0AF009u
#define CORNER_TILES 0x0AE429u
#define CORNER_ADDR 0x0AE431u
#define HORIZONTAL_TILES 0x0AE439u
#define HORIZONTAL_ADDR 0x0AE43Du
#define VERTICAL_TILES 0x0AE441u
#define VERTICAL_ADDR 0x0AE445u
#define FLOOR_NUMBERS 0x0AE4E9u
#define ROW_OFFSET 0x0AE56Fu
/* the sprites */
#define MARKER_Y_BASE 0x0AE7F3u
#define FAIRY_ROOMS 0x0AE7F9u
#define FAIRY_REPLACEMENTS 0x0AE7FFu
#define BOSS_ROOMS 0x0AE807u
#define BLINK_PROPS 0x0AEB48u
#define MARKER_DX 0x0AEB8Au
#define MARKER_DY 0x0AEB8Eu
#define MARKER_PROPS 0x0AEB92u
#define MARKER_FLASH 0x0AEB96u
#define FLOOR_ICON_Y 0x0AECBEu
#define FLOOR_NUMBER_CHAR 0x0AECC6u
#define BLINKER_CHAR 0x0AED50u
#define BOSS_POSITION 0x0AEE5Du
#define BOSS_FLOOR 0x0AEE79u
/* the palettes */
#define HUD_PALETTE 0x1BD6A0u
#define MAP_BG_PALETTE 0x1BE544u
#define MAP_SPRITE_PALETTE 0x1BD70Au

#define LETTER_B 0x1F1C
#define LETTER_F 0x1F1D
#define EMPTY_TEMPLATE 0x51
#define DUNGEONS 14

typedef struct {
	uint8_t x, y, tile, attr;
	bool big, on;
} Obj;

struct AltDungeon {
	const uint8_t* rom;
	size_t size;
	uint16_t vram[0x8000]; /* tiles and tilemaps as the map has them */
	uint16_t sprTiles[DUNGEONS][0x1000]; /* each dungeon's sprite set ($5000 up) */
	bool sprMade[DUNGEONS];
	uint16_t pal[256];
	Obj oam[128];
	uint32_t out[256 * 224];
};

static uint32_t lorom(uint32_t a) {
	return ((a >> 16) & 0x7F) * 0x8000 + (a & 0x7FFF);
}

static uint8_t b8(const AltDungeon* d, uint32_t a) {
	uint32_t o = lorom(a);
	return o < d->size ? d->rom[o] : 0xFF;
}

static uint16_t w16(const AltDungeon* d, uint32_t a) {
	return (uint16_t) (b8(d, a) | b8(d, a + 1) << 8);
}

static int s8(int v) {
	return (v & 0xFF) >= 0x80 ? (v & 0xFF) - 0x100 : v & 0xFF;
}

/* ---- the tiles (tools/alttp_gfx.py) */

/* The game's decompression ($00:E7DE) into out (max bytes); the length */
static int decompress(const AltDungeon* d, uint32_t addr, uint8_t* out, int max) {
	uint32_t src = lorom(addr);
	int n = 0;
	for (;;) {
		if (src >= d->size) return n;
		uint8_t cmd = d->rom[src++];
		if (cmd == 0xFF) return n;
		int len, op;
		if ((cmd & 0xE0) != 0xE0) {
			len = (cmd & 0x1F) + 1, op = cmd >> 5;
		} else {
			if (src >= d->size) return n;
			len = (((cmd & 3) << 8) | d->rom[src++]) + 1, op = (cmd >> 2) & 7;
		}
		if (src + 2 > d->size) return n;
		if (op == 0) {
			for (int i = 0; i < len && n < max && src < d->size; ++i) out[n++] = d->rom[src++];
		} else if (op == 1) {
			uint8_t v = d->rom[src++];
			for (int i = 0; i < len && n < max; ++i) out[n++] = v;
		} else if (op == 2) {
			uint8_t lo = d->rom[src], hi = d->rom[src + 1];
			src += 2;
			for (int i = 0; i < len && n < max; ++i) out[n++] = i & 1 ? hi : lo;
		} else if (op == 3) {
			uint8_t v = d->rom[src++];
			for (int i = 0; i < len && n < max; ++i) out[n++] = (uint8_t) (v + i);
		} else {
			int off = d->rom[src] | d->rom[src + 1] << 8;
			src += 2;
			for (int i = 0; i < len && n < max; ++i) {
				uint8_t v = off + i < n ? out[off + i] : 0;
				out[n++] = v;
			}
		}
	}
}

/* A sheet (64 tiles, 3 bits a pixel) widened to 4 into 0x400 VRAM words
 * (Do3To4Low / Do3To4High: the fourth plane set where any other is) */
static void load_sheet(const AltDungeon* d, uint32_t bank, uint32_t high, uint32_t low, int index, bool hi, uint16_t* to) {
	static uint8_t sheet[0x2000];
	uint32_t addr = (uint32_t) b8(d, bank + index) << 16 | (uint32_t) b8(d, high + index) << 8 | b8(d, low + index);
	int n = decompress(d, addr, sheet, (int) sizeof sheet);
	for (int t = 0; t < 64; ++t) {
		const uint8_t* s = sheet + t * 24;
		for (int y = 0; y < 8; ++y) {
			bool have = t * 24 + 24 <= n;
			uint8_t p0 = have ? s[2 * y] : 0, p1 = have ? s[2 * y + 1] : 0, p2 = have ? s[16 + y] : 0;
			to[t * 16 + y] = (uint16_t) (p0 | p1 << 8);
			to[t * 16 + 8 + y] = (uint16_t) (p2 | (hi ? (p0 | p1 | p2) << 8 : 0));
		}
	}
}

static void make_bg_tiles(AltDungeon* d) {
	uint8_t mt[8], at[4];
	for (int i = 0; i < 8; ++i) mt[i] = b8(d, MAIN_TILESETS + MAP_MAIN * 8 + i);
	for (int i = 0; i < 4; ++i) at[i] = b8(d, AUX_TILESETS + MAP_AUX * 4 + i);
	int sheets[8] = { mt[0], mt[1], mt[2], at[0] ? at[0] : mt[3], at[1] ? at[1] : mt[4], at[2] ? at[2] : mt[5], at[3] ? at[3] : mt[6], mt[7] };
	static const int slot[8] = { 7, 6, 5, 4, 3, 2, 1, 0 };
	for (int k = 0; k < 8; ++k) {
		bool hi = slot[k] == 7 || slot[k] == 2 || slot[k] == 3 || slot[k] == 4; /* (main tileset $20 and up) */
		load_sheet(d, BG_BANK, BG_HIGH, BG_LOW, sheets[k], hi, d->vram + 0x2000 + k * 0x400);
	}
	/* Link's head (16 x 16): uncompressed, a row of 16 tiles is $200 bytes */
	static const int tiles[4] = { 0, 1, 16, 17 };
	for (int k = 0; k < 4; ++k) {
		uint32_t src = LINK_HEAD + (uint32_t) (tiles[k] & 15) * 32 + (uint32_t) (tiles[k] >> 4) * 0x200;
		for (int w = 0; w < 16; ++w) d->vram[0x4000 + tiles[k] * 16 + w] = w16(d, src + w * 2);
	}
}

static const uint16_t* sprite_tiles(AltDungeon* d, int dung) {
	if (!d->sprMade[dung]) {
		int set = 0x80 + dung;
		for (int k = 0; k < 4; ++k) {
			int n = b8(d, SPRITE_TILESETS + set * 4 + k);
			bool hi = n == 0x52 || n == 0x53 || n == 0x5A || n == 0x5B || n == 0x5C || n == 0x5E || n == 0x5F;
			load_sheet(d, SPR_BANK, SPR_HIGH, SPR_LOW, n, hi, d->sprTiles[dung] + k * 0x400);
		}
		d->sprMade[dung] = true;
	}
	return d->sprTiles[dung];
}

AltDungeon* altdun_new(const uint8_t* rom, size_t size) {
	AltDungeon* d = calloc(1, sizeof *d);
	if (!d) return NULL;
	d->rom = rom, d->size = size;
	make_bg_tiles(d);
	return d;
}

void altdun_free(AltDungeon* d) {
	free(d);
}

/* ---- the tilemaps (tools/alttp_dungeon.py's build) */

static uint8_t r8(const uint8_t* ram, uint32_t a) {
	return ram[a & 0x1FFFF];
}

static uint16_t r16(const uint8_t* ram, uint32_t a) {
	return (uint16_t) (r8(ram, a) | r8(ram, a + 1) << 8);
}

/* The game's stripes (HandleStripes14, $00:92A1), from the cartridge or a buffer */
static void stripes(AltDungeon* d, const uint8_t* buf, uint32_t romAt) {
#define RD(p) (buf ? buf[p] : b8(d, romAt + (p)))
	uint32_t p = 0;
	for (int guard = 0; guard < 512 && !(RD(p) & 0x80); ++guard) {
		int addr = RD(p) << 8 | RD(p + 1);
		int flags = RD(p + 2) << 8 | RD(p + 3);
		int n = (flags & 0x3FFF) + 1, step = flags & 0x8000 ? 32 : 1;
		p += 4;
		if (flags & 0x4000) {
			uint16_t v = (uint16_t) (RD(p) | RD(p + 1) << 8);
			for (int i = 0; i < (n + 1) / 2; ++i) d->vram[(addr + i * step) & 0x7FFF] = v;
			p += 2;
		} else {
			for (int i = 0; i < n / 2; ++i) d->vram[(addr + i * step) & 0x7FFF] = (uint16_t) (RD(p + 2 * i) | RD(p + 2 * i + 1) << 8);
			p += (uint32_t) n;
		}
	}
#undef RD
}

typedef struct {
	int dung, up, down, t5, cur, shown;
	uint32_t layoutAt, orderAt;
} Floors;

static bool floors_of(const AltDungeon* d, const uint8_t* ram, int view, Floors* f) {
	int x2 = r8(ram, 0x040C);
	if (x2 == 0xFF || x2 / 2 >= DUNGEONS) return false;
	f->dung = x2 / 2;
	f->t5 = w16(d, FLOOR_COUNT + f->dung * 2);
	f->up = (f->t5 >> 4) & 15, f->down = f->t5 & 15;
	f->cur = r8(ram, 0xA4);
	/* as the game opens it: Link's floor at the top, or above it on the lowest */
	f->shown = f->cur == ((-f->down) & 0xFF) ? (f->cur + 1) & 0xFF : f->cur;
	if (view != ALTDUN_AS_GAME && f->up + f->down >= 2) {
		/* a floor chosen (ALTTP-6): at the top, or the one above it on the lowest */
		int v = view & 0xFF;
		f->shown = v == ((-f->down) & 0xFF) ? (v + 1) & 0xFF : v;
	}
	f->layoutAt = 0x0A0000u | w16(d, ROOM_POINTERS + f->dung * 2);
	f->orderAt = 0x0A0000u | w16(d, ROOM_ORDER_POINTERS + f->dung * 2);
	return true;
}

static void put(uint16_t* buf, int offBytes, int pd, uint16_t v) {
	buf[((offBytes + pd) & 0xFFF) >> 1] = v;
}

static void draw_floor(AltDungeon* d, const uint8_t* ram, const Floors* f, uint16_t* buf, int floor, int pd) {
	const uint16_t mask = 0xEFFF;
	for (int p = 0xDE; p != 0x39E; p += 0x40) put(buf, p, pd, 0x0F00), put(buf, p + 2, pd, 0x0F00);
	uint16_t a, b;
	if (floor & 0x80) a = LETTER_B, b = w16(d, FLOOR_NUMBERS + ((~floor) & 0xFF) * 2);
	else a = w16(d, FLOOR_NUMBERS + (floor & 15) * 2), b = LETTER_F;
	put(buf, 0x35E, pd, a & mask);
	put(buf, 0x360, pd, b & mask);
	for (int i = 0; i < 4; ++i) put(buf, w16(d, CORNER_ADDR + i * 2), pd, w16(d, CORNER_TILES + i * 2) & mask);
	for (int i = 0; i < 2; ++i) {
		int a0 = w16(d, HORIZONTAL_ADDR + i * 2);
		for (int k = 0; k < 20; k += 2) put(buf, a0 + k, pd, w16(d, HORIZONTAL_TILES + i * 2) & mask);
	}
	for (int i = 0; i < 2; ++i) {
		int a0 = w16(d, VERTICAL_ADDR + i * 2);
		for (int k = 0; k < 0x280; k += 0x40) put(buf, a0 + k, pd, w16(d, VERTICAL_TILES + i * 2) & mask);
	}
	int fi = (floor + f->down) & 0xFF;
	if (fi >= f->up + f->down) return;
	bool haveMap = r16(ram, 0xF368) & (0x8000 >> f->dung);
	int floors = f->up + f->down;
	for (int row = 0; row < 5; ++row) {
		int x = w16(d, ROW_OFFSET + row * 2);
		for (int col = 0; col < 5; ++col, x += 4) {
			int v = b8(d, f->layoutAt + fi * 25 + row * 5 + col);
			int tpl, seen = 0;
			if (v == 0x0F) {
				tpl = EMPTY_TEMPLATE;
			} else {
				seen = r16(ram, 0xF000 + v * 2) & 15;
				int count = 0;
				for (int k = 0; k < floors * 25 && b8(d, f->layoutAt + k) != v; ++k) count += b8(d, f->layoutAt + k) != 0x0F;
				tpl = b8(d, f->orderAt + count);
			}
			static const int bit[4] = { 8, 4, 2, 1 }, off[4] = { 0, 2, 0x40, 0x42 };
			for (int q = 0; q < 4; ++q) {
				uint16_t w0 = w16(d, ROOM_TEMPLATES + tpl * 8 + q * 2);
				int add = 0;
				if (w0 != 0x0B00 && !(seen & bit[q])) {
					if (!(w0 & 0x1000)) {
						add = 0x400;
					} else if (haveMap) {
						put(buf, x + off[q], pd, (uint16_t) ((w0 & ~0x1C00) | 0x0C00));
						continue;
					}
				}
				put(buf, x + off[q], pd, (haveMap || (seen & bit[q])) ? (uint16_t) (w0 + add) : 0x0B00);
			}
		}
	}
}

static void build(AltDungeon* d, const uint8_t* ram, const Floors* f) {
	for (int i = 0; i < 0x2000; ++i) d->vram[i] = 0x0300;
	for (int i = 0; i < 0x800; ++i) d->vram[0x6000 + i] = 0x007F;
	stripes(d, NULL, FRAME_STRIPES);
	int lv = b8(d, DUNGEON_LEVEL + f->dung);
	if (lv != 0xFF) {
		/* LEVEL n, for the Dark World's dungeons */
		uint32_t hdr[2] = { LEVEL_TOP, LEVEL_BOTTOM }, num[2] = { NUMERALS_TOP, NUMERALS_BOTTOM };
		for (int k = 0; k < 2; ++k) {
			uint8_t buf[17];
			for (int i = 0; i < 14; ++i) buf[i] = b8(d, hdr[k] + i);
			uint16_t w = w16(d, num[k] + (lv >> 1) * 2);
			buf[14] = (uint8_t) w, buf[15] = (uint8_t) (w >> 8), buf[16] = 0xFF;
			stripes(d, buf, 0);
		}
	}
	/* the floor list's backdrop */
	if (f->t5 & 0x100) {
		uint8_t buf[43];
		for (int i = 0; i < 42; ++i) buf[i] = b8(d, MOUNTAIN_STRIPES + i);
		buf[42] = 0xFF;
		stripes(d, buf, 0);
		for (int k = 0; k < 16; ++k) {
			for (int x = 0; x < 8; ++x) d->vram[0x1123 + k * 0x20 + x] = 0x1B2E;
		}
	}
	int lo = f->t5 & 0xFF;
	int sel = lo >= 0x50 ? (lo >> 4) - 4 : (lo & 15) >= 5 ? (lo & 15) : 0;
	int t7 = w16(d, BACKDROP_POSITION + sel * 2), t7org = t7;
	for (int j = 0; t7 < 0x1360; t7 += 0x20) {
		uint16_t v = (uint16_t) (w16(d, GRADIENT_TILES + j * 2) + (f->t5 & 0x200 ? 0x400 : 0));
		for (int x = 0; x < 8; ++x) d->vram[(t7 + x) & 0x7FFF] = v;
		j += j != 6;
	}
	/* the floor list's boxes */
	int r14 = t7org - 0x40 + 2 + f->down * 0x40;
	for (int i = 0; i < f->up + f->down; ++i, r14 -= 0x40) {
		for (int k = 0; k < 4; ++k) {
			d->vram[(r14 + k) & 0x7FFF] = w16(d, BOX_TILES + k * 2);
			d->vram[(r14 + 0x20 + k) & 0x7FFF] = w16(d, BOX_TILES + (4 + k) * 2);
		}
	}
	/* the two floors' rooms, in the game's buffer, then BG2's left screens */
	static uint16_t buf[2048];
	for (int i = 0; i < 2048; ++i) buf[i] = 0x0300;
	draw_floor(d, ram, f, buf, f->shown, 0);
	draw_floor(d, ram, f, buf, (f->shown - 1) & 0xFF, 0x300);
	memcpy(d->vram, buf, 0x800);
	memcpy(d->vram + 0x800, buf + 0x400, 0x800);
}

/* ---- the sprites (DungeonMap_DrawSprites, $0A:EAB2) */

static int list_place(const Floors* f, int r) {
	if (4 - f->down >= 0) {
		r += 4 - f->down;
		if (f->up - 4 >= 0) r -= f->up - 4;
	}
	return r;
}

static void set_obj(AltDungeon* d, int i, int x, int y, int tile, int attr, bool big) {
	d->oam[i] = (Obj) { (uint8_t) x, (uint8_t) y, (uint8_t) tile, (uint8_t) attr, big, true };
}

static void place_sprites(AltDungeon* d, const uint8_t* ram, const Floors* f) {
	memset(d->oam, 0, sizeof d->oam);
	int frame = r8(ram, 0x1A);
	int down = f->down, up = f->up, cur = f->cur, shown = f->shown;
	/* Link pointing at his floor */
	set_obj(d, 0, 0x19, b8(d, FLOOR_ICON_Y + list_place(f, (down + cur) & 0xFF)) - 4, 0, 0x3E, true);
	/* Link's room (DungeonMap_DrawRoomMarkers) */
	int room = r16(ram, 0xA0);
	for (int i = 0; i < 3; ++i) {
		if (room == w16(d, FAIRY_ROOMS + i * 2)) room = w16(d, FAIRY_REPLACEMENTS + i * 2);
	}
	int floor1 = (down + cur) & 0xFF, xc = 0, yc = 0;
	for (int k = 0; k < 25; ++k) {
		if (b8(d, f->layoutAt + floor1 * 25 + k) == (room & 0xFF)) break;
		if (xc < 64) xc += 16;
		else xc = 0, yc += 16;
	}
	int var3 = (xc + 0x90 + ((r16(ram, 0x22) & 0x1E0) >> 5)) & 0xFF;
	int var6 = yc;
	/* where Link's floor is from the top shown one: a floor is 96 lines */
	int var5 = (yc + w16(d, MARKER_Y_BASE) + 0x60 * s8(shown - cur) + ((r16(ram, 0x20) & 0x1E0) >> 5)) & 0xFFFF;
	/* the boss's room */
	int bossFloor = w16(d, BOSS_FLOOR + f->dung * 2);
	int floor2 = (down + bossFloor) & 0xFF, var7 = 0x40, var8 = 0x40;
	int look = w16(d, BOSS_ROOMS + f->dung * 2) & 0xFF;
	for (int j = 24; j >= 0; --j) {
		int c = b8(d, f->layoutAt + floor2 * 25 + j);
		if (c != 0x0F && c == look) break;
		var7 -= 0x10;
		if (var7 < 0) var7 = 0x40, var8 = (var8 - 0x10) & 0xFF;
	}
	var8 = (var8 + 0x60 * s8(shown - (bossFloor & 0xFF)) + w16(d, MARKER_Y_BASE)) & 0xFFFF;
	int pos = 1;
	for (int r14 = 0; r14 < 2; ++r14) {
		for (int i = 3; i >= 0; --i, ++pos) {
			int r15 = (var6 + w16(d, MARKER_Y_BASE + r14 * 2)) & 0xFF;
			int fr = (frame >> 2) & 1;
			if (((var5 + 1) & 0xF0) == r15 + 1 && var5 < 256) fr += 2;
			set_obj(d, pos, s8(b8(d, MARKER_DX + i)) + (var3 & 0xF0), r15 + s8(b8(d, MARKER_DY + i)), 0, b8(d, MARKER_FLASH + fr) | b8(d, MARKER_PROPS + i), true);
		}
	}
	set_obj(d, pos++, var3 - 3, (var5 < 256 ? var5 : 0xF0) - 3, 0x34, b8(d, BLINK_PROPS + ((frame >> 2) & 3)), false);
	/* the boss, with the compass, until beaten */
	bool beaten = r16(ram, 0xF000 + look * 2) & 0x800, compass = r16(ram, 0xF364) & (0x8000 >> f->dung);
	if (bossFloor != 0xFFFF && !beaten && compass && (frame & 15) < 10) {
		set_obj(d, pos++, 0x4C, b8(d, FLOOR_ICON_Y + list_place(f, (down + bossFloor) & 0xFF)), 0x31, 0x33, false);
		int xy = w16(d, BOSS_POSITION + f->dung * 2);
		set_obj(d, pos++, (xy >> 8) + var7 + 0x90, var8 < 256 ? (xy & 0xFF) + var8 : 0xF0, 0x31, 0x33, false);
	}
	/* each floor's number in the list */
	int yv = 7;
	if (up + down != 8 && up < 4) {
		yv = 6;
		for (int i = 3; i != 0 && i != up; --i) --yv;
		if (down >= 5) {
			for (int i = 5; i != down && down != 8; ++i) ++yv;
		}
	}
	int r4 = (b8(d, FLOOR_ICON_Y + yv) + 1) & 0xFF, r2 = (up - 1) & 0xFF, r3 = (-down) & 0xFF;
	for (int guard = 0; guard < 16 && pos < 0x3E; ++guard) {
		int a = r2 & 0x80 ? 0x1C : b8(d, FLOOR_NUMBER_CHAR + r2);
		int b = r2 & 0x80 ? b8(d, FLOOR_NUMBER_CHAR + (r2 ^ 0xFF)) : 0x1D;
		set_obj(d, pos++, 0x30, r4, a, 0x3D, false);
		set_obj(d, pos++, 0x38, r4, b, 0x3D, false);
		r4 = (r4 + 16) & 0xFF;
		bool done = r2 == r3;
		r2 = (r2 - 1) & 0xFF;
		if (done) break;
	}
	/* the shown floors lit in the list, every other 16 frames */
	int flag = up + down != 1, floor = (shown - flag) & 0xFF, r0 = 0;
	for (int k = 0; k <= flag; ++k) r0 = list_place(f, (floor + down) & 0xFF), floor = (floor + 1) & 0xFF;
	if (frame & 0x10) {
		int y0 = (b8(d, FLOOR_ICON_Y + r0) - 4) & 0xFF;
		for (int fl = flag; fl >= 0; --fl) {
			int x = 40, p = 0x40 + (fl ? 8 : 0);
			for (int i = 3; i >= 0; --i, x += 8, ++p) {
				int at = 0x3D | (i ? 0 : 0x40), ch = b8(d, BLINKER_CHAR + i);
				set_obj(d, p, x, y0 + fl * 16, ch, at, false);
				set_obj(d, p + 4, x, y0 + fl * 16 + 8, ch, at | 0x80, false);
			}
		}
	}
}

/* ---- the picture (tools/alttp_dungeon.py's render) */

static uint32_t abgr(uint16_t c) {
	uint32_t r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
	return 0xFF000000u | (b << 3 | b >> 2) << 16 | (g << 3 | g >> 2) << 8 | (r << 3 | r >> 2);
}

/* a background pixel: colour index (0 clear) and the tile's priority */
static int bg(const AltDungeon* d, int mapBase, int chrBase, int bpp, int x, int y, int* prio) {
	y = (y + 1) & 511, x &= 511; /* (picture line 0 shows row 1) */
	int scr = (x >> 8) + ((y >> 8) << 1);
	uint16_t e = d->vram[(mapBase + scr * 0x400 + ((y >> 3) & 31) * 32 + ((x >> 3) & 31)) & 0x7FFF];
	int tx = e & 0x4000 ? 7 - (x & 7) : x & 7, ty = e & 0x8000 ? 7 - (y & 7) : y & 7, b = 7 - tx, c;
	*prio = (e >> 13) & 1;
	if (bpp == 4) {
		int a = chrBase + (e & 0x3FF) * 16;
		uint16_t p0 = d->vram[(a + ty) & 0x7FFF], p1 = d->vram[(a + 8 + ty) & 0x7FFF];
		c = (p0 >> b & 1) | (p0 >> (8 + b) & 1) << 1 | (p1 >> b & 1) << 2 | (p1 >> (8 + b) & 1) << 3;
		return c ? ((e >> 10) & 7) * 16 + c : 0;
	}
	uint16_t p0 = d->vram[(chrBase + (e & 0x3FF) * 8 + ty) & 0x7FFF];
	c = (p0 >> b & 1) | (p0 >> (8 + b) & 1) << 1;
	return c ? ((e >> 10) & 7) * 4 + c : 0;
}

static void render(AltDungeon* d) {
	/* the sprites into a picture-sized buffer: colour + 1, priority (the first in OAM on top) */
	static uint16_t spr[256 * 224];
	memset(spr, 0, sizeof spr);
	for (int i = 127; i >= 0; --i) {
		const Obj* o = &d->oam[i];
		if (!o->on) continue;
		int size = o->big ? 16 : 8, base = o->attr & 1 ? 0x5000 : 0x4000;
		for (int yy = 0; yy < size; ++yy) {
			int sy = (o->y + yy) & 0xFF; /* (picture line Y: a line below BG row Y) */
			if (sy >= 224) continue;
			for (int xx = 0; xx < size; ++xx) {
				int sx = o->x + xx;
				if (sx >= 256) continue;
				int tx = o->attr & 0x40 ? size - 1 - xx : xx, ty = o->attr & 0x80 ? size - 1 - yy : yy;
				int tile = (o->tile + (tx >> 3) + (ty >> 3) * 16) & 0xFF;
				int a = base + tile * 16;
				uint16_t p0 = d->vram[a + (ty & 7)], p1 = d->vram[a + 8 + (ty & 7)];
				int b = 7 - (tx & 7);
				int c = (p0 >> b & 1) | (p0 >> (8 + b) & 1) << 1 | (p1 >> b & 1) << 2 | (p1 >> (8 + b) & 1) << 3;
				if (c) spr[sy * 256 + sx] = (uint16_t) ((128 + ((o->attr >> 1) & 7) * 16 + c + 1) | ((o->attr >> 4) & 3) << 12);
			}
		}
	}
	for (int y = 0; y < 224; ++y) {
		for (int x = 0; x < 256; ++x) {
			int p2, p3;
			int b2 = bg(d, 0x0000, 0x2000, 4, x, y, &p2), b3 = bg(d, 0x6000, 0x7000, 2, x, y, &p3);
			uint16_t s = spr[y * 256 + x];
			int sc = s ? (s & 0xFFF) - 1 : 0, sp = s >> 12;
			/* mode 1 with BG3 on top: BG3 hi, OBJ 3, BG2 hi, OBJ 2, BG2 lo, OBJ 1, BG3 lo, OBJ 0 */
			int c = -1;
			if (b3 && p3) c = b3;
			else if (s && sp == 3) c = sc;
			else if (b2 && p2) c = b2;
			else if (s && sp == 2) c = sc;
			else if (b2) c = b2;
			else if (s && sp == 1) c = sc;
			else if (b3) c = b3;
			else if (s) c = sc;
			if (c >= 0) {
				d->out[y * 256 + x] = abgr(d->pal[c]);
				continue;
			}
			/* the backdrop, the grid (BG1, sub screen) added to it */
			int p1, b1 = bg(d, 0x1000, 0x2000, 4, x, y, &p1);
			uint16_t back = d->pal[0], sub = b1 ? d->pal[b1] : 0, sum = 0;
			for (int k = 0; k < 15; k += 5) {
				int v = ((back >> k) & 31) + ((sub >> k) & 31);
				sum |= (uint16_t) ((v > 31 ? 31 : v) << k);
			}
			d->out[y * 256 + x] = abgr(sum);
		}
	}
}

bool altdun_draw(AltDungeon* d, const uint8_t* ram, const uint8_t* vram, int view, uint32_t** out) {
	Floors f;
	if (!vram || !floors_of(d, ram, view, &f)) return false;
	/* the HUD's tiles (BG3: the frames, MAP) and Link's own sprite tiles
	 * stay as the game has them; the map's sprite set from the cartridge */
	for (int i = 0; i < 0x1000; ++i) d->vram[0x7000 + i] = (uint16_t) (vram[(0x7000 + i) * 2] | vram[(0x7000 + i) * 2 + 1] << 8);
	memcpy(d->vram + 0x5000, sprite_tiles(d, f.dung), 0x2000);
	/* the colours: the game's, with the map's own over them */
	for (int i = 0; i < 256; ++i) d->pal[i] = r16(ram, 0xC500 + i * 2);
	for (int i = 0; i < 32; ++i) d->pal[i] = w16(d, HUD_PALETTE + i * 2);
	for (int i = 0; i < 96; ++i) d->pal[32 + i] = w16(d, MAP_BG_PALETTE + i * 2);
	for (int r = 0; r < 3; ++r) {
		for (int c = 0; c < 7; ++c) d->pal[(12 + r) * 16 + 1 + c] = w16(d, MAP_SPRITE_PALETTE + (r * 7 + c) * 2);
	}
	build(d, ram, &f);
	place_sprites(d, ram, &f);
	render(d);
	*out = d->out;
	return true;
}

bool altdun_floors(const AltDungeon* d, const uint8_t* ram, int* lowest, int* highest, int* link) {
	Floors f;
	if (!floors_of(d, ram, ALTDUN_AS_GAME, &f)) return false;
	*lowest = -f.down, *highest = f.up - 1, *link = s8(f.cur);
	return true;
}

int altdun_floor_at(const AltDungeon* d, const uint8_t* ram, int x, int y) {
	Floors f;
	if (!floors_of(d, ram, ALTDUN_AS_GAME, &f) || x < 0x20 || x >= 0x50) return ALTDUN_AS_GAME;
	/* the floor numbers' lines, the top floor first (as place_sprites) */
	int yv = 7;
	if (f.up + f.down != 8 && f.up < 4) {
		yv = 6;
		for (int i = 3; i != 0 && i != f.up; --i) --yv;
		if (f.down >= 5) {
			for (int i = 5; i != f.down && f.down != 8; ++i) ++yv;
		}
	}
	int top = b8(d, FLOOR_ICON_Y + yv) + 1 - 4;
	for (int k = 0, fl = f.up - 1; fl >= -f.down; ++k, --fl) {
		if (y >= top + k * 16 && y < top + k * 16 + 16) return fl;
	}
	return ALTDUN_AS_GAME;
}

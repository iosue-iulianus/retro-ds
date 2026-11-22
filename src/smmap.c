/* Super Metroid's pause map drawn by SNES DS (smmap.h). Where everything
 * is, from P.JBoy's bank logs: docs/games/super-metroid.md, "The map's
 * data". tools/sm_map.py is the same drawing in Python, checked pixel for
 * pixel against the game's own pause screen; this follows it. */
#include "smmap.h"

#include <stdlib.h>
#include <string.h>

/* Map screen sprites (menu spritemaps, $82:C569) */
enum {
	SP_SAVE = 0x08, SP_BOSS = 0x09, SP_ENERGY = 0x0A, SP_MISSILE = 0x0B, SP_LR_RING = 0x2A,
	SP_MAPSTATION = 0x4E, SP_CROSS = 0x62, SP_GUNSHIP = 0x63,
};
static const int markerIds[3] = { 0x5F, 0x60, 0x61 };

struct SmMap {
	uint8_t* rom;
	size_t size;
	uint32_t pal[256];          /* the pause screen's colours ($B6:F000) */
	uint32_t ringPal[14][16];   /* sprite palette 3 through its pulse ($82:A987) */
	uint8_t bgTiles[512][64];   /* the map and frame's tiles ($B6:8000), a colour number a pixel */
	uint8_t spTiles[256][64];   /* the menu sprites' tiles ($B6:C000) */
	uint32_t chrome[SMMAP_W * SMMAP_H];
	uint32_t over[SMMAP_W * SMMAP_H]; /* the chrome's pixels over the map */
	uint32_t layer[SMMAP_LW * SMMAP_LH];
	uint32_t marker[3][SMMAP_MARKER * SMMAP_MARKER];

	/* from the game's RAM, the last update */
	int state, area, rawArea;
	uint8_t explored[256];
	int collected, station, bosses, saveArea, usedSaves, loadStation;
	int samusX, samusY, roomX, roomY;
	/* derived */
	uint64_t layerKey;
	bool haveLayer;
	int chromeArea, ringFrame;
	unsigned tick;
	int markerFrame;
	int minX, maxX, minY, maxY; /* the game's MapScroll_MinX... */
};

/* ---- the cartridge */
static size_t lorom(uint32_t a) {
	return (size_t) ((a >> 16) & 0x7F) * 0x8000 + (a & 0x7FFF);
}
static int r8(const SmMap* m, uint32_t a) {
	size_t o = lorom(a);
	return o < m->size ? m->rom[o] : 0;
}
static int r16(const SmMap* m, uint32_t a) {
	return r8(m, a) | r8(m, a + 1) << 8;
}
static uint32_t r24(const SmMap* m, uint32_t a) {
	return (uint32_t) r16(m, a) | (uint32_t) r8(m, a + 2) << 16;
}
static int s16(int v) {
	v &= 0xFFFF;
	return v & 0x8000 ? v - 0x10000 : v;
}

static uint32_t bgr555(int c) {
	int r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
	r = r << 3 | r >> 2, g = g << 3 | g >> 2, b = b << 3 | b >> 2;
	return 0xFF000000u | (uint32_t) b << 16 | (uint32_t) g << 8 | (uint32_t) r;
}

/* 8 x 8 tiles, 4 bits a pixel, into a colour number a pixel */
static void decode_tiles(const SmMap* m, uint32_t addr, int n, uint8_t (*out)[64]) {
	for (int t = 0; t < n; ++t) {
		for (int y = 0; y < 8; ++y) {
			uint32_t a = addr + (uint32_t) t * 32 + (uint32_t) y * 2;
			int p0 = r8(m, a), p1 = r8(m, a + 1), p2 = r8(m, a + 16), p3 = r8(m, a + 17);
			for (int x = 0; x < 8; ++x) {
				int b = 7 - x;
				out[t][y * 8 + x] = (uint8_t) (((p0 >> b) & 1) | ((p1 >> b) & 1) << 1 | ((p2 >> b) & 1) << 2 | ((p3 >> b) & 1) << 3);
			}
		}
	}
}

SmMap* smmap_new(uint8_t* rom, size_t size) {
	if (!rom || size < 0x300000) {
		free(rom);
		return NULL;
	}
	SmMap* m = calloc(1, sizeof(*m));
	if (!m) {
		free(rom);
		return NULL;
	}
	m->rom = rom, m->size = size;
	for (int i = 0; i < 256; ++i) m->pal[i] = bgr555(r16(m, 0xB6F000 + i * 2));
	/* Handle_PauseScreen_PaletteAnimation: 16 colours a step */
	for (int f = 0; f < 14; ++f) {
		for (int i = 0; i < 16; ++i) m->ringPal[f][i] = bgr555(r16(m, 0x82A987 + f * 32 + i * 2));
	}
	decode_tiles(m, 0xB68000, 512, m->bgTiles);
	decode_tiles(m, 0xB6C000, 256, m->spTiles);
	m->chromeArea = m->ringFrame = -1;
	m->markerFrame = -1;
	return m;
}

void smmap_free(SmMap* m) {
	if (!m) return;
	free(m->rom);
	free(m);
}

/* ---- sprites
 * A spritemap: a count word, then 5-byte entries: word s000000xxxxxxxxx
 * (s: 16 x 16; x 9-bit signed), a signed Y byte, word YXppPPPttttttttt.
 * AddSpritemapFrom_82C569_TableToOAM replaces the palette (PPP) with the
 * caller's. The pause screen's sprites are 8 x 8 or 16 x 16 (OBSEL $01);
 * a 16 x 16's tiles are n, n+1 above n+16, n+17. The first entry is on
 * top. (x0, y0): the origin in dst's pixels. colours: the 16 of the
 * palette used. */
static void draw_spritemap(const SmMap* m, uint32_t* dst, int dw, int dh, int x0, int y0, int id,
                           const uint32_t* colours) {
	uint32_t a = 0x820000u | (uint32_t) r16(m, 0x82C569 + id * 2);
	int n = r16(m, a);
	if (n > 64) return;
	for (int k = n - 1; k >= 0; --k) {
		uint32_t e = a + 2 + (uint32_t) k * 5;
		int w = r16(m, e), dy = r8(m, e + 2), attr = r16(m, e + 3);
		int dx = w & 0x1FF;
		if (dx & 0x100) dx -= 0x200;
		if (dy & 0x80) dy -= 0x100;
		int size = w & 0x8000 ? 16 : 8, tile = attr & 0x1FF;
		bool hf = attr & 0x4000, vf = attr & 0x8000;
		for (int y = 0; y < size; ++y) {
			int py = y0 + dy + y;
			if (py < 0 || py >= dh) continue;
			int sy = vf ? size - 1 - y : y;
			for (int x = 0; x < size; ++x) {
				int px = x0 + dx + x;
				if (px < 0 || px >= dw) continue;
				int sx = hf ? size - 1 - x : x;
				int t = (tile & ~0xF) | ((tile + (sx >> 3)) & 0xF);
				t = (t + (sy >> 3) * 16) & 0xFF;
				int v = m->spTiles[t][(sy & 7) * 8 + (sx & 7)];
				if (v) dst[py * dw + px] = colours[v];
			}
		}
	}
}

static const uint32_t* sprite_palette(const SmMap* m, int bits) {
	return &m->pal[128 + ((bits >> 9) & 7) * 16];
}

/* ---- the map layer */
static bool explored_at(const SmMap* m, int x, int y) {
	/* Check_if_MapPosition_isExplored ($82:B855): x, y in map pixels */
	int i = ((x & 0xFF00) >> 1) + ((x & 0xFF) >> 6) + ((y & 0xFFF8) >> 1);
	if (i < 0 || i >= 256) return false;
	return (m->explored[i] >> (7 - ((x >> 3) & 7))) & 1;
}

/* A sprite on the layer: map pixels, the SNES showing a sprite a line
 * lower than the map row with the same number */
static void layer_sprite(SmMap* m, int x, int y, int id, int palBits) {
	draw_spritemap(m, m->layer, SMMAP_LW, SMMAP_LH, s16(x), s16(y) + 1, id, sprite_palette(m, palBits));
}

/* The sprites of one icon list (Draw_MapIcons_ofGivenType, $82:B81C):
 * an entry per bit of mask, where its square is explored. Collected
 * first (they're drawn last to first, the first on top). */
typedef struct { int x, y, id, pal; } Icon;
static int icon_list(const SmMap* m, uint32_t ptrs, int id, int pal, unsigned mask, Icon* out, int n, int max) {
	int p = r16(m, ptrs + m->area * 2);
	if (!p) return n;
	for (uint32_t t = 0x820000u | (uint32_t) p; n < max && !(r16(m, t) & 0x8000); t += 4, mask >>= 1) {
		if (!(mask & 1)) continue;
		int x = r16(m, t), y = r16(m, t + 2);
		if (explored_at(m, x, y)) out[n++] = (Icon) { x, y, id, pal };
	}
	return n;
}

static void build_layer(SmMap* m) {
	memset(m->layer, 0, sizeof(m->layer));
	int area = m->area;
	/* LoadPauseMenuMapTilemap ($82:943D): explored, the word with bit 10
	 * clear (pink); revealed by the map station, as it is (blue); else
	 * the empty tile */
	uint32_t tm = r24(m, 0x82964A + area * 3);
	uint32_t md = 0x820000u | (uint32_t) r16(m, 0x829717 + area * 2);
	for (int i = 0; i < 2048; ++i) {
		int src = r16(m, tm + (uint32_t) i * 2);
		int bit = 7 - (i & 7);
		int w;
		if ((m->explored[i >> 3] >> bit) & 1) w = src & 0xFBFF;
		else if (m->station && ((r8(m, md + (uint32_t) (i >> 3)) >> bit) & 1)) w = src;
		else w = 0x001F;
		int j = i & 1023, cx = (i >> 10) * 32 + (j & 31), cy = j >> 5;
		const uint8_t* t = m->bgTiles[w & 0x3FF];
		const uint32_t* pal = &m->pal[((w >> 10) & 7) * 16];
		bool hf = w & 0x4000, vf = w & 0x8000;
		for (int y = 0; y < 8; ++y) {
			uint32_t* row = &m->layer[(cy * 8 + y) * SMMAP_LW + cx * 8];
			const uint8_t* trow = &t[(vf ? 7 - y : y) * 8];
			for (int x = 0; x < 8; ++x) {
				int v = trow[hf ? 7 - x : x];
				if (v) row[x] = pal[v];
			}
		}
	}

	/* The icons, in the game's order (Draw_Map_Icons, $82:B672) */
	Icon ic[96];
	int n = 0;
	int bp = r16(m, 0x82C7CB + area * 2);
	if (bp) {
		/* bosses ($82:B892): a beaten one crossed out, the others while
		 * the area's map is collected */
		int bosses = m->bosses;
		for (uint32_t t = 0x820000u | (uint32_t) bp; n < 90 && r16(m, t) != 0xFFFF; t += 4, bosses >>= 1) {
			int x = r16(m, t), y = r16(m, t + 2);
			if (x == 0xFFFE) continue;
			if (bosses & 1) {
				ic[n++] = (Icon) { x, y, SP_CROSS, 0x0E00 };
				ic[n++] = (Icon) { x, y, SP_BOSS, 0x0C00 };
			} else if (m->collected) {
				ic[n++] = (Icon) { x, y, SP_BOSS, 0x0E00 };
			}
		}
	}
	n = icon_list(m, 0x82C7DB, SP_MISSILE, 0x0E00, 0xFFFF, ic, n, 96);
	n = icon_list(m, 0x82C7EB, SP_ENERGY, 0x0E00, 0xFFFF, ic, n, 96);
	n = icon_list(m, 0x82C7FB, SP_MAPSTATION, 0x0E00, 0xFFFF, ic, n, 96);
	/* the save station the game was loaded from, in the area saved in,
	 * if it's been used ($82:B798) */
	if (area == m->saveArea) {
		int bit = r8(m, 0x82B7C9 + m->loadStation);
		unsigned mask = (unsigned) (m->usedSaves & 0xFF00) | (unsigned) (m->usedSaves & bit & 0xFF);
		n = icon_list(m, 0x82C80B, SP_SAVE, 0x0400, mask, ic, n, 96);
	}
	/* the gunship, in Crateria, at its save station */
	if (area == 0 && n < 96) ic[n++] = (Icon) { r16(m, 0x82C853), r16(m, 0x82C855), SP_GUNSHIP, 0x0E00 };
	/* the elevators' destinations, with the area's map station ($82:BB30) */
	if (m->station) {
		for (uint32_t t = 0x820000u | (uint32_t) r16(m, 0x82C74D + area * 2); n < 96 && r16(m, t) != 0xFFFF; t += 6) {
			ic[n++] = (Icon) { r16(m, t), r16(m, t + 2), r16(m, t + 4), 0 };
		}
	}
	for (int i = n - 1; i >= 0; --i) layer_sprite(m, ic[i].x, ic[i].y, ic[i].id, ic[i].pal);
	m->haveLayer = true;
}

/* DetermineMapScrollLimits ($82:9EC4): the explored squares' (or with the
 * area's map collected, its whole map's) leftmost, rightmost, top and
 * bottom, in map pixels */
static void find_limits(SmMap* m) {
	uint8_t bits[256];
	if (m->collected) {
		uint32_t md = 0x820000u | (uint32_t) r16(m, 0x829717 + m->area * 2);
		for (int i = 0; i < 256; ++i) bits[i] = (uint8_t) r8(m, md + (uint32_t) i);
	} else {
		memcpy(bits, m->explored, 256);
	}
	int c0 = -1, c1 = -1, row0 = -1, row1 = -1;
	for (int c = 0; c < 64; ++c) {
		for (int r = 0; r < 32; ++r) {
			if (!((bits[(c >> 5) * 0x80 + r * 4 + ((c & 31) >> 3)] >> (7 - (c & 7))) & 1)) continue;
			if (c0 < 0) c0 = c;
			c1 = c;
			if (row0 < 0 || r < row0) row0 = r;
			if (r > row1) row1 = r;
		}
	}
	if (c0 < 0) {
		/* nothing explored (not seen in play): Samus's square */
		c0 = c1 = m->roomX + (m->samusX >> 8);
		row0 = row1 = m->roomY + (m->samusY >> 8) + 1;
	}
	m->minX = c0 * 8, m->maxX = c1 * 8, m->minY = row0 * 8, m->maxY = row1 * 8;
	if (m->area == 4) m->minX -= 0x18; /* (Maridia: as the game does) */
}

/* ---- the chrome */
static void build_chrome(SmMap* m) {
	/* BG2: the frame, the grid, the plates (Tilemap_BG2PauseScreen_..._0,
	 * $B6:E000), the area's name (AreaLabelTilemaps, $82:965F, 12 tiles
	 * at row 5, column 10), the plates' colours on the map page
	 * (Set_PauseScreen_ButtonLabelPalettes_MapScreen, $82:A796) */
	uint16_t w[1024];
	for (int i = 0; i < 1024; ++i) w[i] = (uint16_t) r16(m, 0xB6E000 + i * 2);
	uint32_t label = 0x820000u | (uint32_t) r16(m, 0x82965F + m->area * 2);
	for (int k = 0; k < 12; ++k) w[0xAA + k] = (uint16_t) r16(m, label + (uint32_t) k * 2);
	static const struct { int off, n, pal; } plates[] = {
		{ 0x64A, 5, 5 }, { 0x68A, 5, 5 }, { 0x66C, 5, 2 }, { 0x6AC, 5, 2 }, { 0x658, 4, 2 }, { 0x698, 4, 2 },
	};
	for (size_t p = 0; p < sizeof(plates) / sizeof(plates[0]); ++p) {
		for (int k = 0; k < plates[p].n; ++k) {
			int i = plates[p].off / 2 + k;
			w[i] = (uint16_t) ((w[i] & 0xE3FF) | plates[p].pal << 10);
		}
	}
	/* the chrome's line y is the screen's SMMAP_Y0 + y, BG row y + 1 */
	for (int y = 0; y < SMMAP_H; ++y) {
		int sy = SMMAP_Y0 + y, row = (sy + 1) & 255;
		for (int x = 0; x < SMMAP_W; ++x) {
			uint32_t c = 0xFF000000u, o = 0;
			/* the plates' row: only SAMUS and its pill (SM-7) */
			bool keep = sy < 199 || (x >= 174 && x < 248);
			if (keep) {
				int word = w[(row >> 3) * 32 + (x >> 3)];
				int ty = row & 7, tx = x & 7;
				if (word & 0x8000) ty = 7 - ty;
				if (word & 0x4000) tx = 7 - tx;
				int v = m->bgTiles[word & 0x3FF][ty * 8 + tx];
				if (v) c = m->pal[((word >> 10) & 7) * 16 + v];
				/* the frame's tiles have priority over the map's (all
				 * low): their pixels cover it, their black inner edge
				 * too; the grid's are under it */
				if (v && (word & 0x2000)) o = c;
			}
			m->chrome[y * SMMAP_W + x] = c;
			m->over[y * SMMAP_W + x] = o;
		}
	}
	/* the pill's ring (EquipmentScreen_Draw_L_R_Highlight, $82:A59A: at
	 * x E8, y CF), pulsing in sprite palette 3 */
	draw_spritemap(m, m->chrome, SMMAP_W, SMMAP_H, 0xE8, 0xCF - SMMAP_Y0, SP_LR_RING, m->ringPal[m->ringFrame < 0 ? 0 : m->ringFrame]);
	/* a small arrow in place of the R (SM-7): the pill's grey and its
	 * letter's purple */
	static const char* const arrow[5] = { "....#....", ".....#...", ".######..", ".....#...", "....#...." };
	for (int y = 0; y < 5; ++y) {
		for (int x = 0; x < 9; ++x) {
			m->chrome[(205 - SMMAP_Y0 + y) * SMMAP_W + 228 + x] = arrow[y][x] == '#' ? 0xFF8C0042u : 0xFFBDBDBDu;
		}
	}
}

/* ---- the marker */
static void build_markers(SmMap* m) {
	for (int f = 0; f < 3; ++f) {
		memset(m->marker[f], 0, sizeof(m->marker[f]));
		draw_spritemap(m, m->marker[f], SMMAP_MARKER, SMMAP_MARKER, SMMAP_MARKER / 2, SMMAP_MARKER / 2, markerIds[f],
		               sprite_palette(m, 0x0E00));
	}
}

/* ---- each frame */
static uint64_t fnv(uint64_t h, const void* p, size_t n) {
	const unsigned char* b = p;
	for (size_t i = 0; i < n; ++i) h = (h ^ b[i]) * 1099511628211ull;
	return h;
}

static int ram16(const uint8_t* wram, uint32_t a) {
	return wram[a] | wram[a + 1] << 8;
}

int smmap_update(SmMap* m, const uint8_t* wram) {
	int changed = 0;
	if (m->markerFrame < 0) build_markers(m);
	m->state = wram[0x0998];
	m->rawArea = ram16(wram, 0x079F);
	m->area = m->rawArea >= 7 ? 0 : m->rawArea;
	int oldSq = (m->roomX + (m->samusX >> 8)) | (m->roomY + (m->samusY >> 8)) << 8;
	int oldArea = m->area;
	memcpy(m->explored, &wram[0x07F7], 256);
	m->collected = ram16(wram, 0x0789);
	m->station = wram[0xD908 + (m->rawArea & 7)];
	m->bosses = wram[0xD828 + m->area];
	m->saveArea = ram16(wram, 0xD918);
	m->usedSaves = ram16(wram, 0xD8F8 + m->area * 2);
	m->loadStation = ram16(wram, 0x078B) & 0xFF;
	m->samusX = ram16(wram, 0x0AF6), m->samusY = ram16(wram, 0x0AFA);
	m->roomX = ram16(wram, 0x07A1), m->roomY = ram16(wram, 0x07A3);
	int sq = (m->roomX + (m->samusX >> 8)) | (m->roomY + (m->samusY >> 8)) << 8;
	if (sq != oldSq || m->area != oldArea) changed |= SMMAP_SQUARE;

	uint64_t key = 1469598103934665603ull;
	int vals[] = { m->area, m->collected, m->station, m->bosses, m->saveArea, m->usedSaves, m->loadStation };
	key = fnv(key, vals, sizeof(vals));
	key = fnv(key, m->explored, 256);
	if (!m->haveLayer || key != m->layerKey) {
		m->layerKey = key;
		build_layer(m);
		changed |= SMMAP_LAYER;
	}
	find_limits(m); /* (cheap; Samus's square counts when nothing's explored) */

	/* the blinking: the marker 8, 4, 8, 4 frames ($82:BA25), the ring's
	 * palette 15 frames, then 13 steps of 3 ($82:C10C) */
	++m->tick;
	static const int markerSeq[4] = { 0, 1, 2, 1 }, markerLen[4] = { 8, 4, 8, 4 };
	int t = (int) (m->tick % 24), f = 0;
	while (t >= markerLen[f]) t -= markerLen[f++];
	int mf = markerSeq[f];
	if (mf != m->markerFrame || (changed & SMMAP_SQUARE)) changed |= SMMAP_MARKER_CHANGED;
	m->markerFrame = mf;
	int rt = (int) (m->tick % 54);
	int rf = rt < 15 ? 0 : 1 + (rt - 15) / 3;
	if (rf != m->ringFrame || m->area != m->chromeArea) {
		m->ringFrame = rf;
		m->chromeArea = m->area;
		build_chrome(m);
		changed |= SMMAP_CHROME;
	}
	return changed;
}

bool smmap_live(const SmMap* m) {
	return m->state >= 0x08 && m->state <= 0x12 && m->rawArea <= 5;
}

bool smmap_in_menu(const SmMap* m) {
	return m->state <= 0x05;
}

const uint32_t* smmap_chrome(const SmMap* m) {
	return m->chrome;
}

const uint32_t* smmap_over(const SmMap* m) {
	return m->over;
}

const uint32_t* smmap_layer(const SmMap* m) {
	return m->layer;
}

const uint32_t* smmap_marker(const SmMap* m) {
	return m->marker[m->markerFrame < 0 ? 0 : m->markerFrame];
}

/* SetupMapScrollingForPauseMenu ($82:9E27), with A = 80 */
void smmap_home(const SmMap* m, int* vx, int* vy) {
	int x = (m->maxX - m->minX) / 2 + m->minX - 0x80;
	int p = ((m->samusX >> 8) + m->roomX) * 8;
	int t = 0xE0 - (p - x);
	if (t < 0) {
		x -= t;
	} else {
		t = 0x20 - (p - x);
		if (t >= 0) x -= t;
	}
	int c = (m->maxY - m->minY) / 2 + 0x10 + m->minY;
	int y = s16(-((0x80 - c) & 0xFFF8));
	p = ((m->samusY >> 8) + m->roomY + 1) * 8;
	t = 0x40 - (p - y);
	if (t >= 0) {
		y -= t;
		if (y < -0x28) y = -0x28;
	}
	*vx = s16(x), *vy = y;
}

/* Handle_MapScrollArrows ($82:B934): the game scrolls left while its view
 * is right of MinX - 18, right while left of MaxX + 18 - 100, up while
 * below MinY - 38, down while above MaxY - B1 (8 pixels a step). The
 * home view is always allowed. */
void smmap_limits(const SmMap* m, int* x0, int* x1, int* y0, int* y1) {
	int hx, hy;
	smmap_home(m, &hx, &hy);
	int a = m->minX - 0x18, b = m->maxX + 0x18 - 0x100, c = m->minY - 0x38, d = m->maxY - 0xB1;
	*x0 = a < hx ? a : hx, *x1 = b > hx ? b : hx;
	*y0 = c < hy ? c : hy, *y1 = d > hy ? d : hy;
}

void smmap_place(const SmMap* m, int vx, int vy, int* lx, int* ly, int* mx, int* my) {
	/* chrome line y shows map row y + SMMAP_Y0 + vy + 1 */
	*lx = -vx, *ly = -(vy + SMMAP_Y0 + 1);
	/* the marker ($82:B9C8): placed as a sprite, a row lower in map rows */
	int x = ((m->samusX >> 8) + m->roomX) * 8, y = ((m->samusY >> 8) + m->roomY + 1) * 8;
	*mx = s16(x) - SMMAP_MARKER / 2 + *lx;
	*my = s16(y) + 1 - SMMAP_MARKER / 2 + *ly;
}

void smmap_set_blink(SmMap* m, int markerFrame, int ringFrame) {
	if (markerFrame >= 0 && markerFrame < 3) m->markerFrame = markerFrame;
	if (ringFrame >= 0 && ringFrame < 14 && ringFrame != m->ringFrame) m->ringFrame = ringFrame, build_chrome(m);
}

void smmap_compose(const SmMap* m, int vx, int vy, uint32_t* out) {
	memcpy(out, m->chrome, sizeof(m->chrome));
	int lx, ly, mx, my;
	smmap_place(m, vx, vy, &lx, &ly, &mx, &my);
	const uint32_t* mk = smmap_marker(m);
	for (int y = SMMAP_WIN_Y; y < SMMAP_WIN_Y + SMMAP_WIN_H; ++y) {
		for (int x = SMMAP_WIN_X; x < SMMAP_WIN_X + SMMAP_WIN_W; ++x) {
			/* (the layer repeats, as the SNES's does) */
			int u = (x - lx) & (SMMAP_LW - 1), v = (y - ly) & (SMMAP_LH - 1);
			if (m->layer[v * SMMAP_LW + u]) out[y * SMMAP_W + x] = m->layer[v * SMMAP_LW + u];
			u = x - mx, v = y - my;
			if (u >= 0 && v >= 0 && u < SMMAP_MARKER && v < SMMAP_MARKER && mk[v * SMMAP_MARKER + u]) {
				out[y * SMMAP_W + x] = mk[v * SMMAP_MARKER + u];
			}
			if (m->over[y * SMMAP_W + x]) out[y * SMMAP_W + x] = m->over[y * SMMAP_W + x];
		}
	}
}

/* Castlevania: Symphony of the Night (see sotn.h). Addresses and places in
 * video RAM measured in the harness (docs/games/symphony-of-the-night.md),
 * sotn-decomp's names for them in comments: a reference only, no code. */
#include "sotn.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* g_GameState: 1 the title, 2 playing, 8 the file screens */
#define GAME_STATE 0x8003C734u
#define STATE_TITLE 1

/* The title's logo: an 8-bit texture at video RAM (780, 2), 230 x 121
 * halfwords (460 pixels), its palette at (512, 240); CRC32s of both as the
 * title shows them (the same from the title's first frame, measured from
 * power-on to well after "Press start"). */
#define LOGO_X 780
#define LOGO_Y 2
#define LOGO_HW 230
#define LOGO_H 121
#define LOGO_CLUT_X 512
#define LOGO_CLUT_Y 240
#define LOGO_TEX_CRC 0xBE361342u
#define LOGO_CLUT_CRC 0x1876343Fu

#define VRAM_W 1024
#define VRAM_H 512

bool sotn_is(const char* gameCode) {
	return gameCode && !strcmp(gameCode, SOTN_CODE);
}

static const uint16_t* vram(Emu* e) {
	size_t n = 0;
	const uint8_t* v = emu_memory(e, EMU_MEM_VRAM, &n);
	return v && n >= VRAM_W * VRAM_H * 2 ? (const uint16_t*) v : NULL;
}

static uint32_t rect_crc(const uint16_t* v, int x, int y, int w, int h) {
	uint32_t crc = 0;
	for (int r = 0; r < h; ++r) crc = emu_crc32(crc, v + (size_t) (y + r) * VRAM_W + x, (size_t) w * 2);
	return crc;
}

static uint8_t c5(unsigned c) {
	return (uint8_t) (c << 3 | c >> 2);
}

uint8_t* sotn_title_logo(Emu* e, int* w, int* h) {
	if (!sotn_is(e->gameCode) || emu_read32(e, GAME_STATE) != STATE_TITLE) return NULL;
	const uint16_t* v = vram(e);
	if (!v || rect_crc(v, LOGO_X, LOGO_Y, LOGO_HW, LOGO_H) != LOGO_TEX_CRC ||
	    rect_crc(v, LOGO_CLUT_X, LOGO_CLUT_Y, 256, 1) != LOGO_CLUT_CRC)
		return NULL;
	const uint16_t* clut = v + (size_t) LOGO_CLUT_Y * VRAM_W + LOGO_CLUT_X;
	int W = LOGO_HW * 2, H = LOGO_H * 2;
	uint8_t* out = calloc((size_t) W * H, 4);
	if (!out) return NULL;
	for (int y = 0; y < LOGO_H; ++y) {
		for (int x = 0; x < W; ++x) {
			uint16_t t = v[(size_t) (LOGO_Y + y) * VRAM_W + LOGO_X + x / 2];
			uint16_t c = clut[(t >> (8 * (x & 1))) & 0xFF];
			if (!c) continue; /* (the PlayStation's clear colour) */
			uint8_t px[4] = { c5(c & 31), c5((c >> 5) & 31), c5((c >> 10) & 31), 255 };
			memcpy(out + ((size_t) (y * 2) * W + x) * 4, px, 4);
			memcpy(out + ((size_t) (y * 2 + 1) * W + x) * 4, px, 4);
		}
	}
	*w = W, *h = H;
	return out;
}

/* ---------------------------------------------------------------- the map (SOTN-2 to 4) */

#define ROOM_COUNT 0x8003C760u   /* g_RoomCount */
#define STAGE 0x800974A0u        /* g_StageId: bit 0x20 the inverted castle */
#define WINDOW_COLORS 0x8003CAC0u /* g_Settings.windowColors[3] */
#define TILEMAP_LEFT 0x800730B0u /* g_Tilemap.left, .top: the room's square */
#define TILEMAP_TOP 0x800730B4u
#define PLAYER_X 0x800973F0u     /* in the room, 256 to a square */
#define PLAYER_Y 0x800973F4u
#define CLUT_IDS 0x8003C104u     /* g_ClutIds: the game's palettes by number */
#define ROOMS_ALL 942            /* the file screen's percentage: rooms x 100 / 942 */

/* the game's pictures in video RAM: texture pages (x, y), palettes by
 * their number in its table */
#define MAP_PAGE_X 832
#define MAP_PAGE_Y 256
#define MAP_CLUT 0x1C0
#define FONT_PAGE_X 896
#define FONT_PAGE_Y 256
#define FONT_CLUT 0x196
#define CURSOR_PAGE_X 640
#define CURSOR_PAGE_Y 256
#define CURSOR_CLUT 0x170

static uint32_t rgba(uint16_t c) {
	if (!c) return 0;
	return 0xFF000000u | (uint32_t) c5((c >> 10) & 31) << 16 | (uint32_t) c5((c >> 5) & 31) << 8 | c5(c & 31);
}

static void palette(Emu* e, const uint16_t* v, int id, uint32_t out[16]) {
	uint16_t cid = emu_read16(e, CLUT_IDS + (uint32_t) id * 2);
	const uint16_t* c = v + (size_t) (cid >> 6) * VRAM_W + (cid & 63) * 16;
	for (int i = 0; i < 16; ++i) out[i] = rgba(c[i]);
}

/* w x h pixels of a 4-bit texture from (u, v) of the page at (px, py) */
static void tex4(const uint16_t* v, int px, int py, int u, int vv, int w, int h, uint8_t* out, int stride) {
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) {
			uint16_t t = v[(size_t) (py + vv + y) * VRAM_W + px + (u + x) / 4];
			out[y * stride + x] = (t >> (4 * ((u + x) & 3))) & 15;
		}
	}
}

bool sotn_read(Emu* e, SotnView* sv) {
	const uint16_t* v = vram(e);
	if (!v) return false;
	sv->state = (int) emu_read32(e, GAME_STATE);
	sv->rooms = (int) emu_read32(e, ROOM_COUNT);
	sv->inverted = emu_read32(e, STAGE) & 0x20;
	for (int i = 0; i < 3; ++i) sv->window[i] = (int) emu_read32(e, WINDOW_COLORS + 4 * (uint32_t) i) & 31;
	sv->sqX = (int) emu_read32(e, TILEMAP_LEFT) + (int) (emu_read32(e, PLAYER_X) >> 8);
	sv->sqY = (int) emu_read32(e, TILEMAP_TOP) + (int) (emu_read32(e, PLAYER_Y) >> 8);
	/* Select draws the map's 256 x 256 as a 255 x 255 quad from (0, 1)
	 * of its picture; in the inverted castle turned half round, from
	 * (255, 240) to (0, -15) */
	static uint8_t tex[256][256];
	tex4(v, MAP_PAGE_X, MAP_PAGE_Y, 0, 0, 256, 256, &tex[0][0], 256);
	for (int y = 0; y < SOTN_PIC_H; ++y) {
		for (int x = 0; x < SOTN_PIC_W; ++x) {
			int tu = sv->inverted ? 255 - x : x, tv = sv->inverted ? 240 - y : y - 1;
			sv->pic[y][x] = tv >= 0 && tv < 256 ? tex[tv][tu] : 0;
		}
	}
	palette(e, v, MAP_CLUT, sv->mapPal);
	tex4(v, FONT_PAGE_X, FONT_PAGE_Y, 0, 0, 128, 128, &sv->font[0][0], 128);
	palette(e, v, FONT_CLUT, sv->fontPal);
	tex4(v, CURSOR_PAGE_X, CURSOR_PAGE_Y, 0, 0, 32, 32, &sv->cursor[0][0], 32);
	palette(e, v, CURSOR_CLUT, sv->cursorPal);
	return true;
}

bool sotn_has_map(const SotnView* v) {
	return v->rooms > 0;
}

uint32_t sotn_view_crc(const SotnView* v) {
	uint32_t c = emu_crc32(0, v->pic, sizeof(v->pic));
	c = emu_crc32(c, v->mapPal, sizeof(v->mapPal));
	c = emu_crc32(c, v->font, sizeof(v->font));
	c = emu_crc32(c, v->fontPal, sizeof(v->fontPal));
	c = emu_crc32(c, v->window, sizeof(v->window));
	c = emu_crc32(c, &v->rooms, sizeof(v->rooms));
	c = emu_crc32(c, &v->inverted, sizeof(v->inverted));
	return c;
}

/* the castle's part of Select's picture */
static void castle_rect(const SotnView* v, int* x, int* y) {
	*x = v->inverted ? 6 : 8;
	*y = v->inverted ? 31 : 21;
}

/* The PlayStation's dither: added to each 8-bit channel before it's cut
 * to 5 bits, by the pixel's place in a 4 x 4 square. */
static const int dither[4][4] = { { -4, 0, -3, 1 }, { 2, -2, 3, -1 }, { -3, 1, -4, 0 }, { 3, -1, 2, -2 } };

static uint8_t dithered(int c, int x, int y) {
	c += dither[y & 3][x & 3];
	c = c < 0 ? 0 : c > 255 ? 255 : c;
	return c5((unsigned) c >> 3);
}

/* The status screen's window: a gradient from the window colour less 32
 * at the top to it plus 32 at the bottom (the other way round in the
 * inverted castle), dithered, and a white edge a pixel wide */
static void window(const SotnView* v, uint32_t* px, int stride, int x0, int y0, int w, int h) {
	int top[3], bot[3];
	for (int i = 0; i < 3; ++i) {
		int a = v->window[i] * 8 - 32, b = v->window[i] * 8 + 32;
		top[i] = a < 0 ? 0 : a;
		bot[i] = b > 255 ? 255 : b;
		if (v->inverted) {
			int t = top[i];
			top[i] = bot[i], bot[i] = t;
		}
	}
	for (int y = 0; y < h; ++y) {
		int c[3];
		for (int i = 0; i < 3; ++i) c[i] = top[i] + (bot[i] - top[i]) * y / (h > 1 ? h - 1 : 1);
		for (int x = 0; x < w; ++x) {
			uint32_t p;
			if (x == 0 || y == 0 || x == w - 1 || y == h - 1) p = 0xFFFFFBFFu; /* (255, 251, 255) */
			else
				p = 0xFF000000u | (uint32_t) dithered(c[2], x0 + x, y0 + y) << 16 | (uint32_t) dithered(c[1], x0 + x, y0 + y) << 8 |
				    dithered(c[0], x0 + x, y0 + y);
			px[(y0 + y) * stride + x0 + x] = p;
		}
	}
}

static void text(const SotnView* v, uint32_t* px, int stride, int x, int y, const char* s) {
	for (; *s; ++s, x += 8) {
		int ch = (unsigned char) *s - 0x20;
		if (ch <= 0 || ch > 0x7F) continue;
		for (int r = 0; r < 8; ++r) {
			for (int c = 0; c < 8; ++c) {
				uint8_t i = v->font[(ch >> 4) * 8 + r][(ch & 15) * 8 + c];
				if (i && v->fontPal[i]) px[(y + r) * stride + x + c] = v->fontPal[i];
			}
		}
	}
}

void sotn_compose(const SotnView* v, uint32_t* px, int cw, int ch, SotnLayout* out) {
	const int m = 4, in = 6; /* black round the window; the map's and panes' room inside it */
	for (int i = 0; i < cw * ch; ++i) px[i] = 0xFF000000u;
	window(v, px, cw, m, m, cw - 2 * m, ch - 2 * m);
	/* the map, at the left, its clear pixels showing the window */
	int cx, cy;
	castle_rect(v, &cx, &cy);
	int mx = m + in, my = (ch - SOTN_MAP_H) / 2;
	if (my < m + 1) my = m + 1;
	for (int y = 0; y < SOTN_MAP_H && my + y < ch - m - 1; ++y) {
		for (int x = 0; x < SOTN_MAP_W && mx + x < cw - m - 1; ++x) {
			uint8_t i = v->pic[cy + y][cx + x];
			if (i && v->mapPal[i]) px[(my + y) * cw + mx + x] = v->mapPal[i];
		}
	}
	/* ROOMS and the percentage in a pane at the bottom right, as the
	 * status screen and the file screen write them */
	char rooms[16], pct[16], line[32];
	snprintf(rooms, sizeof rooms, "%d", v->rooms);
	int tenths = v->rooms * 1000 / ROOMS_ALL;
	snprintf(pct, sizeof pct, "%d.%d%%", tenths / 10, tenths % 10);
	const int cols = 12, pw = cols * 8 + 16, ph = 8 * 2 + 6 + 12;
	int px0 = cw - m - in - pw, py0 = ch - m - in - ph;
	if (px0 > m && py0 > m) {
		window(v, px, cw, px0, py0, pw, ph);
		snprintf(line, sizeof line, "ROOMS%*s", cols - 5, rooms);
		text(v, px, cw, px0 + 8, py0 + 6, line);
		snprintf(line, sizeof line, "%*s", cols, pct);
		text(v, px, cw, px0 + 8, py0 + 6 + 8 + 6, line);
	}
	/* the logo's place: the top right, beside the map */
	float lx = (float) (mx + SOTN_MAP_W + in), lw = (float) (cw - m - in) - lx;
	float lh = lw / SOTN_LOGO_ASPECT, maxH = (float) (py0 - in - (m + in));
	if (lh > maxH) lh = maxH, lw = lh * SOTN_LOGO_ASPECT;
	out->mapX = mx - cx, out->mapY = my - cy; /* (Select's picture's origin) */
	out->logoX = lx + ((float) (cw - m - in) - lx - lw) / 2, out->logoY = (float) (m + in);
	out->logoW = lw > 0 ? lw : 0, out->logoH = lh > 0 ? lh : 0;
}

void sotn_cursor_pixels(const SotnView* v, uint32_t px[32 * 32]) {
	for (int y = 0; y < 32; ++y) {
		for (int x = 0; x < 32; ++x) px[y * 32 + x] = v->cursorPal[v->cursor[y][x]];
	}
}

void sotn_cursor_rect(const SotnView* v, const SotnLayout* l, int t, int* x, int* y, int* size) {
	/* as the game's map cursor: a 32-pixel square closing onto Alucard's
	 * square over 64 frames, quickly at first (in Select's picture) */
	int tick = 63 - (t & 63);
	tick = 15 - ((tick * tick) >> 8);
	if (tick == 0) tick = 15;
	int gx = v->sqX * 4 - 14, gy = v->sqY * 4 - 13;
	if (v->inverted) gx -= 1, gy -= 17;
	*x = l->mapX + gx + tick;
	*y = l->mapY + gy + tick;
	*size = 32 - tick * 2;
}

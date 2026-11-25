/* Castlevania: Symphony of the Night (see sotn.h). Addresses and places in
 * video RAM measured in the harness (docs/games/symphony-of-the-night.md),
 * sotn-decomp's names for them in comments: a reference only, no code. */
#include "sotn.h"
#include "disc.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* g_GameState: 1 the title, 2 playing, 8 the file screens */
#define GAME_STATE 0x8003C734u

#define VRAM_W 1024
#define VRAM_H 512

/* The title's logo: an 8-bit texture at video RAM (780, 2), 230 x 121
 * halfwords (460 pixels), its palette at (512, 240). The title loads it
 * from BIN/F_TITLE0.BIN, which is video RAM x 512 to 1023, y 0 to 255,
 * stored in columns 64 halfwords wide, each as four 32 x 128 blocks (left
 * and right of its top half, then of its bottom half). CRC32s of the
 * logo's halfwords row by row and of its palette, as measured. */
#define TITLE_FILE "BIN/F_TITLE0.BIN"
#define TITLE_FILE_SIZE 262144
#define LOGO_X 780
#define LOGO_Y 2
#define LOGO_HW 230
#define LOGO_H 121
#define LOGO_CLUT_X 512
#define LOGO_CLUT_Y 240
#define LOGO_TEX_CRC 0xBE361342u
#define LOGO_CLUT_CRC 0x1876343Fu

bool sotn_is(const char* gameCode) {
	return gameCode && !strcmp(gameCode, SOTN_CODE);
}

static const uint16_t* vram(Emu* e) {
	size_t n = 0;
	const uint8_t* v = emu_memory(e, EMU_MEM_VRAM, &n);
	return v && n >= VRAM_W * VRAM_H * 2 ? (const uint16_t*) v : NULL;
}

static uint8_t c5(unsigned c) {
	return (uint8_t) (c << 3 | c >> 2);
}

/* the halfword at video RAM (x, y), x 512 to 1023 and y 0 to 255, in F_TITLE0 */
static uint16_t title_hw(const uint8_t* f, int x, int y) {
	x -= 512;
	size_t o = (size_t) (x / 64) * 32768 + (size_t) (x % 64 / 32) * 8192 + (size_t) (y / 128) * 16384 + (size_t) (y % 128) * 64 + (size_t) (x % 32) * 2;
	return (uint16_t) (f[o] | f[o + 1] << 8);
}

uint8_t* sotn_disc_logo(const char* cuePath, int* w, int* h) {
	size_t n = 0;
	uint8_t* f = disc_read_file(cuePath, TITLE_FILE, &n);
	if (!f || n != TITLE_FILE_SIZE) {
		free(f);
		return NULL;
	}
	uint16_t tex[LOGO_H][LOGO_HW], clut[256];
	for (int y = 0; y < LOGO_H; ++y) {
		for (int x = 0; x < LOGO_HW; ++x) tex[y][x] = title_hw(f, LOGO_X + x, LOGO_Y + y);
	}
	for (int i = 0; i < 256; ++i) clut[i] = title_hw(f, LOGO_CLUT_X + i, LOGO_CLUT_Y);
	free(f);
	if (emu_crc32(0, tex, sizeof(tex)) != LOGO_TEX_CRC || emu_crc32(0, clut, sizeof(clut)) != LOGO_CLUT_CRC) return NULL;
	/* its 460 x 121 pixels at their shape on a television (the title's
	 * 512-wide pixels 0.625 as wide as a line is high): each 2.5 pixels
	 * wide and 4 high, 1150 x 484 */
	int W = LOGO_HW * 2 * 5 / 2, H = LOGO_H * 4;
	uint8_t* out = calloc((size_t) W * H, 4);
	if (!out) return NULL;
	for (int y = 0; y < H; ++y) {
		for (int x = 0; x < W; ++x) {
			int sx = x * 2 / 5;
			uint16_t t = tex[y / 4][sx / 2];
			uint16_t c = clut[(t >> (8 * (sx & 1))) & 0xFF];
			if (!c) continue; /* (the PlayStation's clear colour) */
			uint8_t* o = out + ((size_t) y * W + x) * 4;
			o[0] = c5(c & 31), o[1] = c5((c >> 5) & 31), o[2] = c5((c >> 10) & 31), o[3] = 255;
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
	sv->roomX = (int) emu_read32(e, TILEMAP_LEFT), sv->roomY = (int) emu_read32(e, TILEMAP_TOP);
	sv->sqX = sv->roomX + (int) (emu_read32(e, PLAYER_X) >> 8);
	sv->sqY = sv->roomY + (int) (emu_read32(e, PLAYER_Y) >> 8);
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
/* (dx, dy: where (x0, y0) is on the screen, for the dither's pattern) */
static void window(const SotnView* v, uint32_t* px, int stride, int x0, int y0, int w, int h, int dx, int dy) {
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
				p = 0xFF000000u | (uint32_t) dithered(c[2], dx + x, dy + y) << 16 | (uint32_t) dithered(c[1], dx + x, dy + y) << 8 |
				    dithered(c[0], dx + x, dy + y);
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

#define PANE_COLS 12

void sotn_compose(const SotnView* v, uint32_t* px, int cw, int ch, SotnLayout* out) {
	const int m = 4, in = 6; /* black round the window; the map's and pane's room inside it */
	for (int i = 0; i < cw * ch; ++i) px[i] = 0xFF000000u;
	window(v, px, cw, m, m, cw - 2 * m, ch - 2 * m, m, m);
	out->winX = m + 1, out->winY = m + 1, out->winW = cw - 2 * m - 2, out->winH = ch - 2 * m - 2;
	/* the map at the left, its clear pixels showing the window */
	castle_rect(v, &out->castleX, &out->castleY);
	int mx = m + in, my = (ch - SOTN_MAP_H) / 2;
	if (my < m + 1) my = m + 1;
	out->mapX = mx - out->castleX, out->mapY = my - out->castleY;
	/* ROOMS and the percentage in a pane at the bottom right */
	out->paneW = PANE_COLS * 8 + 16, out->paneH = 8 * 2 + 6 + 12;
	out->paneX = cw - m - in - out->paneW, out->paneY = ch - m - in - out->paneH;
	if (out->paneX <= m || out->paneY <= m) out->paneW = out->paneH = 0;
	/* the logo's place: the top right, beside the map */
	float lx = (float) (mx + SOTN_MAP_W + in), lw = (float) (cw - m - in) - lx;
	float lh = lw / SOTN_LOGO_ASPECT, maxH = (float) (out->paneY - in - (m + in));
	if (lh > maxH) lh = maxH, lw = lh * SOTN_LOGO_ASPECT;
	out->logoX = lx + ((float) (cw - m - in) - lx - lw) / 2, out->logoY = (float) (m + in);
	out->logoW = lw > 0 ? lw : 0, out->logoH = lh > 0 ? lh : 0;
}

void sotn_map_pixels(const SotnView* v, uint32_t* px) {
	for (int y = 0; y < SOTN_PIC_H; ++y) {
		for (int x = 0; x < SOTN_PIC_W; ++x) {
			uint8_t i = v->pic[y][x];
			px[y * SOTN_PIC_W + x] = i ? v->mapPal[i] : 0;
		}
	}
}

void sotn_pane_pixels(const SotnView* v, const SotnLayout* l, uint32_t* px) {
	if (!l->paneW || !l->paneH) return;
	/* as the status screen writes ROOMS, and the file screen the
	 * percentage (rooms x 1000 / 942, in tenths) */
	char rooms[16], pct[16], line[32];
	snprintf(rooms, sizeof rooms, "%d", v->rooms);
	int tenths = v->rooms * 1000 / ROOMS_ALL;
	snprintf(pct, sizeof pct, "%d.%d%%", tenths / 10, tenths % 10);
	window(v, px, l->paneW, 0, 0, l->paneW, l->paneH, l->paneX, l->paneY);
	snprintf(line, sizeof line, "ROOMS%*s", PANE_COLS - 5, rooms);
	text(v, px, l->paneW, 8, 6, line);
	snprintf(line, sizeof line, "%*s", PANE_COLS, pct);
	text(v, px, l->paneW, 8, 6 + 8 + 6, line);
}

void sotn_cursor_pixels(const SotnView* v, uint32_t px[32 * 32]) {
	for (int y = 0; y < 32; ++y) {
		for (int x = 0; x < 32; ++x) px[y * 32 + x] = v->cursorPal[v->cursor[y][x]];
	}
}

void sotn_cursor_rect(const SotnView* v, int t, int* x, int* y, int* size) {
	/* as the game's map cursor: a 32-pixel square closing onto Alucard's
	 * square over 64 frames, quickly at first (in Select's picture) */
	int tick = 63 - (t & 63);
	tick = 15 - ((tick * tick) >> 8);
	if (tick == 0) tick = 15;
	int gx = v->sqX * 4 - 14, gy = v->sqY * 4 - 13;
	if (v->inverted) gx -= 1, gy -= 17;
	*x = gx + tick;
	*y = gy + tick;
	*size = 32 - tick * 2;
}

/* ---------------------------------------------------------------- the menu (SOTN-6, 7) */

#define ENGINE_STEP 0x8003C9A4u   /* g_GameEngineStep */
#define MENU_STEP 0x800978F8u     /* g_MenuStep */
#define PAUSE_ALLOWED 0x8003C8B8u /* g_PauseAllowed */

int sotn_engine(Emu* e) {
	return (int) emu_read32(e, ENGINE_STEP);
}

int sotn_menu_step(Emu* e) {
	return (int) emu_read32(e, MENU_STEP);
}

bool sotn_menu_openable(Emu* e) {
	return sotn_is(e->gameCode) && emu_read32(e, GAME_STATE) == 2 && sotn_engine(e) == SOTN_ENGINE_PLAYING && emu_read32(e, PAUSE_ALLOWED) &&
	       emu_read32(e, ROOM_COUNT) > 0; /* (the prologue: Richter's pause, not the menu) */
}

static bool white(uint32_t c) {
	return (c & 0xFF) > 240 && ((c >> 8) & 0xFF) > 240 && ((c >> 16) & 0xFF) > 240;
}

static bool near(uint32_t a, uint32_t b) {
	for (int k = 0; k < 24; k += 8) {
		int d = (int) ((a >> k) & 255) - (int) ((b >> k) & 255);
		if (d > 24 || d < -24) return false;
	}
	return !white(a);
}

bool sotn_widen(const uint32_t* fb, int w, int h, uint32_t* out, int ow, int* oxOut) {
	if (w < 64 || h < 64 || ow <= w) return false;
	/* the window's left and right edges: the outermost columns white on
	 * most of the picture's rows */
	int L = -1, R = -1;
	for (int x = 0; x < w / 4 && L < 0; ++x) {
		int n = 0;
		for (int y = 0; y < h; ++y) n += white(fb[y * w + x]);
		if (n > h / 2) L = x;
	}
	for (int x = w - 1; x > w * 3 / 4 && R < 0; --x) {
		int n = 0;
		for (int y = 0; y < h; ++y) n += white(fb[y * w + x]);
		if (n > h / 2) R = x;
	}
	if (L < 0 || R < 0 || R - L < w / 2) return false;
	/* the window centred, its edges at the widened picture's own (the
	 * picture placed on whole dither steps) */
	int ox = ((ow - (R - L + 1)) / 2 - L) & ~3;
	int nL = 0, nR = ow - 1;
	if (oxOut) *oxOut = ox;
	/* each window row's colours just inside its edges, and whether they're
	 * clean (the dither's pattern repeating there, nothing else: another
	 * window passing over the edge as the menu animates isn't) */
	enum { NONE, ACROSS, WINDOW };
	static uint8_t kind[1024];
	static uint32_t sl[1024][4], sr[1024][4];
	static bool okL[1024], okR[1024];
	/* and the last clean ones each row had, from earlier pictures (a
	 * window's gradient stays put while others move over it) */
	static uint32_t lastL[1024][4], lastR[1024][4];
	static bool haveLastL[1024], haveLastR[1024];
	if (h > 1024) return false;
	for (int y = 0; y < h; ++y) {
		const uint32_t* row = fb + y * w;
		bool edges = white(row[L]) && white(row[R]);
		int whites = 0;
		for (int x = L; x <= R; ++x) whites += white(row[x]);
		kind[y] = !edges ? NONE : whites > (R - L) * 9 / 10 ? ACROSS : WINDOW;
		okL[y] = okR[y] = kind[y] == WINDOW;
		for (int k = 0; k < 4 && kind[y] == WINDOW; ++k) {
			sl[y][k] = row[L + 4 + k], sr[y][k] = row[R - 4 - k];
			/* (and the four within the dither's few steps of each other:
			 * anything drawn there repeating every 4 pixels isn't it) */
			okL[y] = okL[y] && row[L + 1 + k] == row[L + 5 + k] && near(sl[y][k], row[L + 4]);
			okR[y] = okR[y] && row[R - 1 - k] == row[R - 5 - k] && near(sr[y][k], row[R - 4]);
		}
	}
	for (int y = 0; y < h; ++y) {
		const uint32_t* row = fb + y * w;
		uint32_t* o = out + y * ow;
		for (int x = 0; x < ow; ++x) o[x] = 0xFF000000u;
		for (int x = 0; x < w; ++x) o[ox + x] = row[x];
		if (kind[y] == ACROSS) {
			/* a top or bottom edge: across */
			for (int x = nL; x <= nR; ++x) o[x] = row[L + 1];
		} else if (kind[y] == WINDOW) {
			/* a window's row: its colours continued, in the dither's 4-pixel
			 * steps, from just inside its edges; not clean, the nearest
			 * clean row's on the dither's same row (y % 4), above or below */
			static const uint32_t black[4] = { 0xFF000000u, 0xFF000000u, 0xFF000000u, 0xFF000000u };
			const uint32_t *a = okL[y] ? sl[y] : haveLastL[y] ? lastL[y] : NULL, *b = okR[y] ? sr[y] : haveLastR[y] ? lastR[y] : NULL;
			for (int d = 4; !a && d < 64; d += 4) {
				if (y - d >= 0 && okL[y - d]) a = sl[y - d];
				else if (y + d < h && okL[y + d]) a = sl[y + d];
			}
			for (int d = 4; !b && d < 64; d += 4) {
				if (y - d >= 0 && okR[y - d]) b = sr[y - d];
				else if (y + d < h && okR[y + d]) b = sr[y + d];
			}
			if (!a) a = black;
			if (!b) b = black;
			if (okL[y]) memcpy(lastL[y], sl[y], sizeof lastL[y]), haveLastL[y] = true;
			if (okR[y]) memcpy(lastR[y], sr[y], sizeof lastR[y]), haveLastR[y] = true;
			for (int x = nL + 1; x <= ox + L; ++x) o[x] = a[((x - ox - L - 4) % 4 + 4) % 4];
			for (int x = ox + R; x < nR; ++x) o[x] = b[((ox + R - 4 - x) % 4 + 4) % 4];
			o[nL] = row[L], o[nR] = row[R];
		}
	}
	return true;
}

void sotn_text(const SotnView* v, uint32_t* px, int stride, int x, int y, const char* s) {
	text(v, px, stride, x, y, s);
}

void sotn_window(const SotnView* v, uint32_t* px, int stride, int x, int y, int w, int h, int dx, int dy) {
	window(v, px, stride, x, y, w, h, dx, dy);
}

void sotn_button(const SotnView* v, uint32_t* px, int stride, int x, int y, int which) {
	/* the font's row 14: arrows, then ○ ✕ □ △ from its 9th place */
	int u = (8 + which) * 8, vv = 14 * 8;
	for (int r = 0; r < 8; ++r) {
		for (int c = 0; c < 8; ++c) {
			uint8_t i = v->font[vv + r][u + c];
			if (i && v->fontPal[i]) px[(y + r) * stride + x + c] = v->fontPal[i];
		}
	}
}

/* ---------------------------------------------------------------- SOTN-8: the parts spread */

/* A part of a menu page: its rectangle in the game's picture, and how far
 * it moves (dx in whole dither steps of 4; dy rows). Only what's drawn on
 * the window moves (letters, icons, the portrait): its background stays,
 * so no seam shows (the gradient is drawn as two triangles, the colour
 * stepping partway across some rows). */
typedef struct {
	int x0, y0, x1, y1, dx, dy;
} Part;

#define SPREAD_L (-36)
#define SPREAD_R 44
/* the menu's pages, measured on the owner's saves (Nov 25, 2026): which
 * steps show them, the white lines of their windows where they rest (all
 * there, and no other long one: nothing moving over the page), and their
 * parts */
typedef struct {
	int y, x0, x1;
} Line;
typedef struct {
	int stepLo, stepHi;
	const Line* lines;
	int nLines;
	const Part* parts;
	int nParts;
} Page;

#define FULL(y) { y, 1, 358 }
/* the status page (MENU_STEP_OPENED) */
static const Line statusLines[] = { FULL(16), FULL(215), { 92, 158, 233 }, { 171, 158, 233 } };
static const Part statusParts[] = {
	{ 15, 28, 84, 99, SPREAD_L, 0 },    /* the portrait */
	{ 15, 100, 110, 158, SPREAD_L, 0 }, /* its foot, STR to LCK and their bonuses */
	{ 31, 166, 152, 200, SPREAD_L, 0 }, /* EXP, NEXT, GOLD */
	{ 247, 28, 336, 118, SPREAD_R, 0 }, /* LEVEL, STATUS, ATT, DEF */
	{ 239, 138, 336, 165, SPREAD_R, 0 }, /* ROOMS, KILLS */
	/* TIME on NEXT's line, its numbers under KILLS' (the owner, Nov 25,
	 * 2026: clear of the Back button below it) */
	{ 206, 182, 320, 192, SPREAD_R + 16, -4 },
};
/* The menu's other pages (Relics, Equip, Spells, System, Familiars) stay
 * as the game lays them out, widened only (the owner, Nov 25, 2026: spread,
 * their blinking cursors kept them jumping, and Relics' looked wrong). */
#define N(a) (int) (sizeof(a) / sizeof(a[0]))
static const Page pages[] = {
	{ 0x10, 0x10, statusLines, N(statusLines), statusParts, N(statusParts) },
};

static const Page* page_of(int step) {
	for (int i = 0; i < N(pages); ++i) {
		if (step >= pages[i].stepLo && step <= pages[i].stepHi) return &pages[i];
	}
	return NULL;
}

/* a white line at row y of the widened picture from x0 to x1 of the game's */
static bool white_run(const uint32_t* px, int ow, int ox, int y, int x0, int x1) {
	for (int x = x0; x <= x1; ++x) {
		if (!white(px[y * ow + ox + x])) return false;
	}
	return true;
}

bool sotn_same_page(int stepA, int stepB) {
	return page_of(stepA) && page_of(stepA) == page_of(stepB);
}

bool sotn_page_lines(const uint32_t* px, int ow, int h, int ox, int step) {
	const Page* p = page_of(step);
	if (!p || h < 216) return false;
	for (int i = 0; i < p->nLines; ++i) {
		if (!white_run(px, ow, ox, p->lines[i].y, p->lines[i].x0, p->lines[i].x1)) return false;
	}
	return true;
}

bool sotn_settled(const uint32_t* px, int ow, int h, int ox, int step) {
	const Page* p = page_of(step);
	if (!sotn_page_lines(px, ow, h, ox, step)) return false;
	/* no other window's edge across the page */
	for (int y = 17; y < 215; ++y) {
		bool listed = false;
		for (int i = 0; i < p->nLines && !listed; ++i) listed = p->lines[i].y == y;
		if (listed) continue;
		int run = 0;
		for (int x = 1; x < 358; ++x) {
			run = white(px[y * ow + ox + x]) ? run + 1 : 0;
			if (run > 24) return false;
		}
	}
	return true;
}

bool sotn_spread(uint32_t* px, int ow, int h, int ox, int step, float t) {
	const Page* pg = page_of(step);
	if (!pg || h < 216 || ow < 16) return false;
	size_t bytes = (size_t) ow * h * 4;
	uint32_t* src = malloc(bytes);
	if (!src) return false;
	memcpy(src, px, bytes);
	/* the row's background: its colours at the widened bands' left and
	 * right (either side of the gradient's step), by the dither's place */
#define BGL(x, y) src[(y) * ow + 4 + (((x) - 4) & 3)]
#define BGR(x, y) src[(y) * ow + ow - 8 + (((x) - (ow - 8)) & 3)]
#define IS_BG(c, x, y) ((c) == BGL(x, y) || (c) == BGR(x, y))
	/* first what every part draws taken away, the background it covered in
	 * its place (that side of the step's: the nearest background left of it) */
	for (int i = 0; i < pg->nParts; ++i) {
		const Part* p = &pg->parts[i];
		for (int y = p->y0; y <= p->y1; ++y) {
			bool right = false;
			for (int x = ox + p->x0 - 1; x > 0; --x) {
				uint32_t c = src[y * ow + x];
				if (IS_BG(c, x, y)) {
					right = c != BGL(x, y);
					break;
				}
			}
			for (int x = ox + p->x0; x <= ox + p->x1; ++x) {
				uint32_t c = src[y * ow + x];
				if (IS_BG(c, x, y)) right = c != BGL(x, y);
				else px[y * ow + x] = right ? BGR(x, y) : BGL(x, y);
			}
		}
	}
	/* then put where it goes */
	for (int i = 0; i < pg->nParts; ++i) {
		const Part* p = &pg->parts[i];
		int dx = (int) lroundf(p->dx * t / 4) * 4, dy = (int) lroundf(p->dy * t);
		for (int y = p->y0; y <= p->y1; ++y) {
			for (int x = ox + p->x0; x <= ox + p->x1; ++x) {
				int X = x + dx, Y = y + dy;
				uint32_t c = src[y * ow + x];
				if (X < 1 || X >= ow - 1 || Y < 0 || Y >= h || IS_BG(c, x, y)) continue;
				px[Y * ow + X] = c;
			}
		}
	}
#undef IS_BG
#undef BGR
#undef BGL
	free(src);
	return true;
}

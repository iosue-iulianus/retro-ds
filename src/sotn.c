/* Castlevania: Symphony of the Night (see sotn.h). Addresses and places in
 * video RAM measured in the harness (docs/games/symphony-of-the-night.md),
 * sotn-decomp's names for them in comments: a reference only, no code. */
#include "sotn.h"

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

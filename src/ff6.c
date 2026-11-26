/* Final Fantasy VI (see ff6.h, docs/games/final-fantasy-vi.md). */
#include "ff6.h"

#include <stdlib.h>
#include <string.h>

bool ff6_is(const char* gameCode) {
	return gameCode && !strcmp(gameCode, FF6_CODE);
}

/* The NMI handlers the game's programs install (measured, Nov 26, 2026) */
Ff6Part ff6_part(Emu* e) {
	if (emu_read8(e, 0x7E1500) != 0x5C) return FF6_OTHER; /* (JML) */
	uint32_t nmi = emu_read24(e, 0x7E1501);
	if (nmi == 0xC00182) return FF6_FIELD;
	if (nmi == 0xC10BA7) return FF6_BATTLE;
	if (nmi == 0xC31387) return FF6_MENU;
	if ((nmi >> 16) == 0xEE) return FF6_WORLD; /* (the world map's programs, bank $EE) */
	return FF6_OTHER;
}

const char* ff6_part_name(Ff6Part p) {
	static const char* const names[] = { "other", "field", "world map", "battle", "menu" };
	return p >= 0 && p <= FF6_MENU ? names[p] : "?";
}

void ff6_battle(Emu* e, Ff6Battle* b) {
	memset(b, 0, sizeof(*b));
	b->open = emu_read8(e, 0x7E7BCA) != 0;
	b->menu = emu_read8(e, 0x7E7BC2);
	b->slot = emu_read8(e, 0x7E62CA) & 3;
	b->cursor = emu_read8(e, 0x7E890F + (uint32_t) b->slot) & 3;
	/* each slot's four commands: number, flags (bit 7: greyed), targeting */
	for (int i = 0; i < 4; ++i) {
		uint32_t a = 0x7E202E + (uint32_t) (b->slot * 12 + i * 3);
		uint8_t n = emu_read8(e, a), f = emu_read8(e, a + 1);
		b->cmd[i] = n == 0xFF ? -1 : n;
		b->disabled[i] = n == 0xFF || (f & 0x80);
	}
	/* Config's Command set (the save's $1D4D bit 7): 1 short */
	b->shortSet = emu_read8(e, 0x7E1D4D) & 0x80;
	b->partyTarget = emu_read8(e, 0x7E7B7D) & 0x0F;
	b->monTarget = emu_read8(e, 0x7E7B7E) & 0x3F;
	b->monAlive = 0;
	for (int i = 0; i < 6; ++i) {
		/* alive: here ($3AA8 + 2n bit 0), not dead (its status, $3EEC + 2n bit 7) */
		bool here = emu_read8(e, 0x7E3AA8 + 2 * (uint32_t) i) & 1, dead = emu_read8(e, 0x7E3EEC + 2 * (uint32_t) i) & 0x80;
		if (here && !dead) b->monAlive |= 1 << i;
		uint16_t id = emu_read16(e, 0x7E2001 + 2 * (uint32_t) i);
		b->monType[i] = id == 0xFFFF ? -1 : id & 0x1FF;
		b->monX[i] = emu_read16(e, 0x7E80C3 + 2 * (uint32_t) i);
		b->monY[i] = emu_read16(e, 0x7E80CF + 2 * (uint32_t) i);
	}
}

static int lowest_bit(int m) {
	for (int i = 0; i < 16; ++i) {
		if (m >> i & 1) return i;
	}
	return -1;
}

void ff6_aim_start(Ff6Aim* a, int want) {
	memset(a, 0, sizeof(*a));
	a->want = want;
	a->lastMask = -1;
}

uint32_t ff6_aim_next(Ff6Aim* a, const Ff6Battle* b) {
	if (a->want < 0) return 0;
	if (!b->open || b->menu != FF6_MENU_TARGET) return a->want = -1, 0;
	int mask = b->partyTarget | b->monTarget << 4; /* (want's own bit numbers) */
	/* the last press: did it move the cursor? */
	static const uint32_t dirs[4] = { K_LEFT, K_RIGHT, K_UP, K_DOWN };
	if (a->lastKey && mask == a->lastMask) {
		for (int d = 0; d < 4; ++d) {
			if (a->lastKey == dirs[d]) a->blocked |= 1 << d;
		}
	} else if (mask != a->lastMask) {
		a->blocked = 0;
	}
	if (mask >> a->want & 1) return a->want = -1, K_A; /* on it */
	if (!mask || ++a->steps > 12) return a->want = -1, 0;
	/* the directions to try, best first */
	int order[4], n = 0;
	if (a->want >= 4 && b->partyTarget) {
		order[n++] = 0; /* Left, over to the monsters */
	} else if (a->want < 4 && !b->partyTarget) {
		order[n++] = 1; /* Right, toward the party */
	} else if (a->want < 4) {
		int at = lowest_bit(b->partyTarget);
		bool down = ((a->want - at) & 3) <= ((at - a->want) & 3);
		order[n++] = down ? 3 : 2, order[n++] = down ? 2 : 3;
	} else {
		int at = lowest_bit(b->monTarget), w = a->want - 4;
		int dx = b->monX[w] - b->monX[at], dy = b->monY[w] - b->monY[at];
		int h = dx < 0 ? 0 : 1, v = dy < 0 ? 2 : 3;
		bool hFirst = abs(dx) >= abs(dy);
		order[n++] = hFirst ? h : v;
		if (hFirst ? dy : dx) order[n++] = hFirst ? v : h;
		order[n++] = hFirst ? v ^ 1 : h ^ 1; /* (round the other way) */
		order[n++] = hFirst ? h ^ 1 : v ^ 1;
	}
	for (int i = 0; i < n; ++i) {
		if (!(a->blocked >> order[i] & 1)) {
			a->lastMask = mask, a->lastKey = dirs[order[i]];
			return a->lastKey;
		}
	}
	return a->want = -1, 0;
}

int ff6_group_target(const Ff6Battle* b, int monsterId) {
	if (monsterId < 0) return -1;
	int at = b->monTarget ? lowest_bit(b->monTarget) : -1;
	if (at >= 0 && b->monType[at] == monsterId && (b->monAlive >> at & 1)) return 4 + at;
	int best = -1, bestD = 0;
	for (int i = 0; i < 6; ++i) {
		if (b->monType[i] != monsterId || !(b->monAlive >> i & 1)) continue;
		int d = at >= 0 ? abs(b->monX[i] - b->monX[at]) + abs(b->monY[i] - b->monY[at]) : i;
		if (best < 0 || d < bestD) best = i, bestD = d;
	}
	return best < 0 ? -1 : 4 + best;
}

int ff6_command_presses(const Ff6Battle* b, int row, uint32_t keys[8]) {
	if (!b->open || b->menu != FF6_MENU_COMMANDS || b->shortSet) return 0;
	if (row < 0 || row > 3 || b->disabled[row]) return 0;
	int n = 0, at = b->cursor;
	/* Down as the game moves it: to the next row that isn't greyed */
	for (int guard = 0; at != row && guard < 4; ++guard) {
		do at = (at + 1) & 3; while (b->disabled[at] && at != b->cursor);
		keys[n++] = K_DOWN;
	}
	if (at != row) return 0;
	keys[n++] = K_A;
	return n;
}

/* ---- FF6-5: the party screen (see ff6.h) */

#include <stdio.h>
#include <stdlib.h>

struct Ff6Rom {
	uint8_t* d;
	size_t n;
};

Ff6Rom* ff6_rom_load(const char* path) {
	FILE* f = path ? fopen(path, "rb") : NULL;
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	Ff6Rom* r = calloc(1, sizeof(*r));
	r->d = n > 0 ? malloc((size_t) n) : NULL;
	if (!r->d || fread(r->d, 1, (size_t) n, f) != (size_t) n) {
		fclose(f);
		ff6_rom_free(r);
		return NULL;
	}
	fclose(f);
	r->n = (size_t) n;
	if (r->n % 1024 == 512) { /* (a copier header) */
		memmove(r->d, r->d + 512, r->n - 512);
		r->n -= 512;
	}
	if (r->n < 0x300000) {
		ff6_rom_free(r);
		return NULL;
	}
	return r;
}

void ff6_rom_free(Ff6Rom* r) {
	if (!r) return;
	free(r->d);
	free(r);
}

/* a HiROM address ($C0-$FF banks) in the file */
static const uint8_t* at(const Ff6Rom* r, uint32_t addr) {
	uint32_t o = addr & 0x3FFFFF;
	return o < r->n ? r->d + o : r->d;
}

static uint16_t rd16(const uint8_t* p) {
	return (uint16_t) (p[0] | p[1] << 8);
}

#define CHAR_GOGO 12

void ff6_party_read(Emu* e, Ff6Party* p) {
	memset(p, 0, sizeof(*p));
	bool field = ff6_part(e) == FF6_FIELD;
	int party = emu_read8(e, 0x7E1A6D) & 7;
	bool espers = emu_read8(e, 0x7E1A69) | emu_read8(e, 0x7E1A6A) | emu_read8(e, 0x7E1A6B) | emu_read8(e, 0x7E1A6C);
	for (int i = 0; i < 16; ++i) {
		/* (the field keeps the party's flags in its objects, $0867 + 41 x n,
		 * and writes them back to $1850 as the menu opens) */
		uint8_t f = field ? emu_read8(e, 0x7E0867 + 41 * (uint32_t) i) : emu_read8(e, 0x7E1850 + (uint32_t) i);
		if (!(f & 0x40) || (f & 7) != party) continue;
		Ff6Member* m = &p->m[f >> 3 & 3];
		uint32_t c = 0x7E1600 + 37 * (uint32_t) i;
		m->here = true, m->id = i, m->backRow = f & 0x20;
		for (int k = 0; k < 6; ++k) m->name[k] = (char) emu_read8(e, c + 2 + (uint32_t) k);
		m->level = emu_read8(e, c + 8);
		/* CalcMaxHPMP: the base plus a boost (the top 2 bits: 1 25%, 2 50%,
		 * 3 12.5%), at most 9999 HP and 999 MP; the current at most that */
		for (int k = 0; k < 2; ++k) {
			uint16_t raw = emu_read16(e, c + (k ? 0xF : 0xB));
			int base = raw & 0x3FFF, b = raw >> 14;
			int max = base + (b == 1 ? base >> 2 : b == 2 ? base >> 1 : b == 3 ? base >> 3 : 0);
			int cap = k ? 999 : 9999;
			if (max > cap) max = cap;
			int cur = emu_read16(e, c + (k ? 0xD : 0x9));
			if (cur > max) cur = max;
			if (k) m->mp = cur, m->mpMax = max;
			else m->hp = cur, m->hpMax = max;
		}
		/* CheckMPVisible: any esper in the party, Gogo, or a spell known */
		int actor = emu_read8(e, c);
		m->showMp = espers || actor == CHAR_GOGO;
		if (!m->showMp && actor < CHAR_GOGO) {
			for (int s = 0; s < 54 && !m->showMp; ++s) m->showMp = emu_read8(e, 0x7E1A6E + 54 * (uint32_t) actor + (uint32_t) s) == 0xFF;
		}
		/* the portrait: an imp's, Locke's for Locke, else the character's graphic */
		m->portrait = (emu_read8(e, c + 0x14) & 0x20) ? 15 : actor == 1 ? 1 : emu_read8(e, c + 1);
	}
	p->style = emu_read8(e, 0x7E1D4E) & 7;
	for (int k = 0; k < 7; ++k) p->winPal[k] = emu_read16(e, 0x7E1D57 + 14 * (uint32_t) p->style + 2 * (uint32_t) k);
	p->fontColour = emu_read16(e, 0x7E1D55);
	if (emu_read8(e, 0x7E1188) & 0x10) { /* (a countdown timer instead) */
		int s = emu_read16(e, 0x7E1189) / 60 / 60;
		p->hours = s / 60, p->minutes = s % 60;
	} else {
		p->hours = emu_read8(e, 0x7E021B), p->minutes = emu_read8(e, 0x7E021C);
	}
	p->gp = emu_read24(e, 0x7E1860), p->steps = emu_read24(e, 0x7E1866);
	if (p->gp > 9999999) p->gp = 9999999;
	if (p->steps > 9999999) p->steps = 9999999;
}

/* ---- drawing: the SNES's way (8 x 8 tiles, BGR555 palettes) */

static uint32_t abgr(uint16_t c) {
	unsigned r = c & 31, g = c >> 5 & 31, b = c >> 10 & 31;
	r = r << 3 | r >> 2, g = g << 3 | g >> 2, b = b << 3 | b >> 2;
	return 0xFF000000u | b << 16 | g << 8 | r;
}

/* a 4bpp (32 bytes) or 2bpp (16 bytes) tile's pixel */
static int pix(const uint8_t* t, int bpp, int x, int y) {
	int s = 7 - x, v = (t[2 * y] >> s & 1) | (t[2 * y + 1] >> s & 1) << 1;
	if (bpp == 4) v |= (t[16 + 2 * y] >> s & 1) << 2 | (t[16 + 2 * y + 1] >> s & 1) << 3;
	return v;
}

/* The main menu's colour math on its windows (InitGradientHDMA): every
 * channel of a window's pixel, 11 down to 0 added on the top 112 lines (a
 * step every 10), then 1 up subtracted */
static uint16_t gradient(uint16_t c, int y) {
	int band = y / 10, n = band <= 11 ? 11 - band : band - 11;
	int ch[3] = { c & 31, c >> 5 & 31, c >> 10 & 31 };
	for (int i = 0; i < 3; ++i) {
		ch[i] += y < 112 ? n : -n;
		ch[i] = ch[i] < 0 ? 0 : ch[i] > 31 ? 31 : ch[i];
	}
	return (uint16_t) (ch[0] | ch[1] << 5 | ch[2] << 10);
}

/* A window as DrawWindow lays it (window_pos: its inside's top left and
 * size in tiles; the border round it): the style's 28 tiles, the inside a
 * 4 x 4 pattern, the side borders alternating by row, top and bottom by
 * column (WindowTileTbl) */
static int canvasW = FF6_PIC_W, canvasH = FF6_PIC_H, winUp = 1; /* (the picture being drawn; the windows' scroll) */

static void window(uint16_t* bg, bool* on, const uint8_t* gfx, int tx, int ty, int w, int h) {
	for (int r = 0; r < h + 2; ++r) {
		for (int c = 0; c < w + 2; ++c) {
			int t;
			if (r == 0) t = c == 0 ? 16 : c == w + 1 ? 19 : (c - 1) % 2 ? 18 : 17;
			else if (r == h + 1) t = c == 0 ? 24 : c == w + 1 ? 27 : (c - 1) % 2 ? 26 : 25;
			else if (c == 0) t = (r - 1) % 2 ? 22 : 20;
			else if (c == w + 1) t = (r - 1) % 2 ? 23 : 21;
			else t = (r - 1) % 4 * 4 + (c - 1) % 4;
			for (int y = 0; y < 8; ++y) {
				for (int x = 0; x < 8; ++x) {
					int px = (tx + c) * 8 + x, py = (ty + r) * 8 + y - winUp; /* (the menu's BG2 scrolled 1 up) */
					if (px < 0 || py < 0 || px >= canvasW || py >= canvasH) continue;
					int v = pix(gfx + t * 32, 4, x, y);
					bg[py * canvasW + px] = (uint16_t) v;
					on[py * canvasW + px] = true; /* (the window's own tile, clear pixels too) */
				}
			}
		}
	}
}

/* the text codes the game uses (its menu font) */
#define CH_0 0xB4
#define CH_SLASH 0xC0
#define CH_COLON 0xC1
#define CH_BLANK 0xFF

/* The main menu's text layers' scroll: BG1 (the party's text) 3 lines up,
 * BG3 (Time, Steps, Gp) 1 up, as its windows */
static int textUp = 3;

/* one character of the fixed-width font (C4/7FC0, 2bpp) at a tile, in a
 * 4-colour text palette (0 clear) */
static void glyph(uint32_t* out, const Ff6Rom* r, int code, int tx, int ty, const uint32_t pal[4]) {
	if (code == CH_BLANK || code < 0) return;
	const uint8_t* t = at(r, 0xC47FC0 + (uint32_t) code * 16);
	for (int y = 0; y < 8; ++y) {
		for (int x = 0; x < 8; ++x) {
			int v = pix(t, 2, x, y), px = tx * 8 + x, py = ty * 8 + y - textUp;
			if (v && px < canvasW && py >= 0 && py < canvasH) out[py * canvasW + px] = pal[v];
		}
	}
}

static void text(uint32_t* out, const Ff6Rom* r, const char* s, int tx, int ty, const uint32_t pal[4]) {
	for (; *s; ++s, ++tx) glyph(out, r, (uint8_t) *s, tx, ty, pal);
}

/* a number in `digits` places, leading zeros blank (all but the last) or
 * kept (zeros) */
static void number(uint32_t* out, const Ff6Rom* r, unsigned v, int digits, bool zeros, int tx, int ty, const uint32_t pal[4]) {
	for (int i = digits - 1; i >= 0; --i) {
		unsigned d = v % 10;
		bool blank = !zeros && v == 0 && i != digits - 1;
		glyph(out, r, blank ? CH_BLANK : (int) (CH_0 + d), tx + i, ty, pal);
		v /= 10;
	}
}

/* a menu text palette (D8/E800: 40 of 4 colours), its main colour the
 * player's font colour for the white ones */
static void text_pal(const Ff6Rom* r, int i, uint32_t pal[4], int fontColour) {
	const uint8_t* p = at(r, 0xD8E800 + (uint32_t) i * 8);
	for (int k = 0; k < 4; ++k) pal[k] = abgr(rd16(p + 2 * k));
	if (fontColour >= 0) pal[3] = abgr((uint16_t) fontColour);
}

/* the menu's letters for its labels (the game's text codes) */
static const char kLV[] = { (char) 0x8B, (char) 0x95, 0 };  /* LV */
static const char kHP[] = { (char) 0x87, (char) 0x8F, 0 };  /* HP */
static const char kMP[] = { (char) 0x8C, (char) 0x8F, 0 };  /* MP */
static const char kSlash[] = { (char) CH_SLASH, 0 };
static const char kTime[] = { (char) 0x93, (char) 0xA2, (char) 0xA6, (char) 0x9E, 0 };  /* Time */
static const char kSteps[] = { (char) 0x92, (char) 0xAD, (char) 0x9E, (char) 0xA9, (char) 0xAC, 0 };  /* Steps */
static const char kGp[] = { (char) 0x86, (char) 0xA9, 0 };  /* Gp */
static const char kColon[] = { (char) CH_COLON, 0 };

void ff6_party_draw(const Ff6Rom* rom, const Ff6Party* p, uint32_t* out) {
	canvasW = FF6_PIC_W, canvasH = FF6_PIC_H, winUp = 1;
	for (int i = 0; i < FF6_PIC_W * FF6_PIC_H; ++i) out[i] = 0xFF000000u;
	if (!rom) return;
	/* BG2: the windows (DrawMainMenu, its command list left out), in the
	 * style's tiles (ED/0000) and the player's colours */
	static uint16_t bg[FF6_PIC_W * FF6_PIC_H];
	static bool on[FF6_PIC_W * FF6_PIC_H];
	memset(bg, 0, sizeof(bg));
	memset(on, 0, sizeof(on));
	const uint8_t* gfx = at(rom, 0xED0000 + 0x380 * (uint32_t) p->style);
	window(bg, on, gfx, 1, 1, 28, 24);  /* MainMenuCharWindow */
	window(bg, on, gfx, 23, 16, 6, 2);  /* MainMenuTimeWindow */
	window(bg, on, gfx, 22, 20, 7, 5);  /* MainMenuStepsWindow */
	const uint8_t* defPal = at(rom, 0xED1C00 + 32 * (uint32_t) p->style);
	for (int y = 0; y < FF6_PIC_H; ++y) {
		for (int x = 0; x < FF6_PIC_W; ++x) {
			int v = bg[y * FF6_PIC_W + x];
			if (!v) continue;
			uint16_t c = v <= 7 ? p->winPal[v - 1] : rd16(defPal + 2 * v);
			out[y * FF6_PIC_W + x] = abgr(gradient(c, y));
		}
	}
	/* the portraits (sprites, 5 x 5 tiles: ED/1D00, palettes ED/5860, the
	 * menu's tables at C3/6F00 and C3/6F1B), the back row's further right */
	for (int s = 0; s < 4; ++s) {
		const Ff6Member* m = &p->m[s];
		if (!m->here || m->portrait < 0 || m->portrait > 26) continue;
		int palNo = *at(rom, 0xC36F00 + (uint32_t) m->portrait);
		uint16_t off = rd16(at(rom, 0xC36F1B + 2 * (uint32_t) m->portrait));
		const uint8_t* g = at(rom, 0xED1D00 + off);
		const uint8_t* pp = at(rom, 0xED5860 + 32 * (uint32_t) palNo);
		int ox = m->backRow ? 26 : 14, oy = 21 + 48 * s;
		/* the menu's sprites for it (C3/0B8F: a count, then x (bit 7: 16 x
		 * 16), y, tile, attributes); its 25 tiles in video RAM from $60,
		 * 16 to a row */
		const uint8_t* sp = at(rom, 0xC30B8F);
		for (int k = 0; k < sp[0] && k < 16; ++k) {
			const uint8_t* e4 = sp + 1 + 4 * k;
			bool big = e4[0] & 0x80;
			int sx = e4[0] & 0x7F, sy = e4[1], tile = e4[2] - 0x60;
			for (int q = 0; q < (big ? 4 : 1); ++q) {
				int tt = tile + (q & 1) + (q >> 1) * 16; /* (a 16 x 16 sprite's four tiles) */
				int gi = tt < 16 ? tt : 16 + (tt - 16);
				if (gi < 0 || gi >= 25) continue;
				int bx = ox + sx + (q & 1) * 8, by = oy + sy + (q >> 1) * 8;
				for (int y = 0; y < 8; ++y) {
					for (int x = 0; x < 8; ++x) {
						int v = pix(g + gi * 32, 4, x, y), px = bx + x, py = by + y;
						if (v && px < FF6_PIC_W && py < FF6_PIC_H) out[py * FF6_PIC_W + px] = abgr(rd16(pp + 2 * v));
					}
				}
			}
		}
	}
	/* the text: BG1's (palette n: D8/E800's 4 n) and BG3's (palette n: its n) */
	textUp = 3; /* (BG1) */
	uint32_t white[4], teal1[4], teal3[4];
	text_pal(rom, 0, white, p->fontColour);
	text_pal(rom, 4, teal1, -1);
	text_pal(rom, 3, teal3, -1);
	for (int s = 0; s < 4; ++s) {
		const Ff6Member* m = &p->m[s];
		if (!m->here) continue;
		int dy = 6 * s;
		text(out, rom, m->name, 8, 3 + dy, white);
		text(out, rom, kLV, 10, 5 + dy, teal1);
		text(out, rom, kHP, 10, 6 + dy, teal1);
		number(out, rom, (unsigned) m->level, 2, false, 15, 5 + dy, white);
		number(out, rom, (unsigned) m->hp, 4, false, 13, 6 + dy, white);
		text(out, rom, kSlash, 17, 6 + dy, white);
		number(out, rom, (unsigned) m->hpMax, 4, false, 18, 6 + dy, white);
		if (m->showMp) {
			text(out, rom, kMP, 10, 7 + dy, teal1);
			number(out, rom, (unsigned) m->mp, 4, false, 13, 7 + dy, white);
			text(out, rom, kSlash, 17, 7 + dy, white);
			number(out, rom, (unsigned) m->mpMax, 4, false, 18, 7 + dy, white);
		}
	}
	textUp = 1; /* (BG3) */
	text(out, rom, kTime, 25, 17, teal3);
	number(out, rom, (unsigned) p->hours, 2, false, 25, 18, white);
	text(out, rom, kColon, 27, 18, white);
	number(out, rom, (unsigned) p->minutes, 2, true, 28, 18, white);
	text(out, rom, kSteps, 23, 21, teal3);
	number(out, rom, p->steps, 7, false, 23, 22, white);
	text(out, rom, kGp, 23, 24, teal3);
	number(out, rom, p->gp, 7, false, 23, 25, white);
}

/* ---- FF6-2: the battle screen, drawn */

void ff6_battle_view(Emu* e, Ff6BattleView* v) {
	memset(v, 0, sizeof(*v));
	ff6_battle(e, &v->menu);
	int shown = emu_read8(e, 0x7E201D); /* (the characters shown: bits 0-3) */
	for (int s = 0; s < 4; ++s) {
		Ff6Fighter* f = &v->f[s];
		/* the battle's own copy for its windows ($2EAE + 32 x slot) */
		uint32_t b = 0x7E2EAE + 32 * (uint32_t) s;
		f->here = (shown >> s & 1) != 0;
		if (!f->here) continue;
		f->portrait = emu_read8(e, b);
		for (int k = 0; k < 6; ++k) f->name[k] = (char) emu_read8(e, b + 1 + (uint32_t) k);
		f->hp = emu_read16(e, b + 7), f->hpMax = emu_read16(e, b + 9);
		f->mp = emu_read16(e, b + 11), f->mpMax = emu_read16(e, b + 13);
		f->atb = emu_read8(e, 0x7E619E + (uint32_t) s);
	}
	for (int k = 0; k < 4; ++k) {
		uint16_t id = emu_read16(e, 0x7E200D + 2 * (uint32_t) k);
		v->enemy[k] = id == 0xFFFF ? -1 : id & 0x1FF;
	}
	v->gauge = emu_read8(e, 0x7E2021) & 1;
	v->style = emu_read8(e, 0x7E1D4E) & 7;
	for (int k = 0; k < 7; ++k) v->winPal[k] = emu_read16(e, 0x7E1D57 + 14 * (uint32_t) v->style + 2 * (uint32_t) k);
	v->fontColour = emu_read16(e, 0x7E1D55);
}

/* a portrait at a pixel, as the menu's sprites lay it (see ff6_party_draw) */
static void portrait(const Ff6Rom* rom, uint32_t* out, int which, int ox, int oy) {
	if (which < 0 || which > 26) return;
	int palNo = *at(rom, 0xC36F00 + (uint32_t) which);
	uint16_t off = rd16(at(rom, 0xC36F1B + 2 * (uint32_t) which));
	const uint8_t* g = at(rom, 0xED1D00 + off);
	const uint8_t* pp = at(rom, 0xED5860 + 32 * (uint32_t) palNo);
	const uint8_t* sp = at(rom, 0xC30B8F);
	for (int k = 0; k < sp[0] && k < 16; ++k) {
		const uint8_t* e4 = sp + 1 + 4 * k;
		bool big = e4[0] & 0x80;
		int sx = e4[0] & 0x7F, sy = e4[1], tile = e4[2] - 0x60;
		for (int q = 0; q < (big ? 4 : 1); ++q) {
			int gi = tile + (q & 1) + (q >> 1) * 16;
			if (gi < 0 || gi >= 25) continue;
			int bx = ox + sx + (q & 1) * 8, by = oy + sy + (q >> 1) * 8;
			for (int y = 0; y < 8; ++y) {
				for (int x = 0; x < 8; ++x) {
					int c = pix(g + gi * 32, 4, x, y), px = bx + x, py = by + y;
					if (c && px >= 0 && py >= 0 && px < canvasW && py < canvasH) out[py * canvasW + px] = abgr(rd16(pp + 2 * c));
				}
			}
		}
	}
}

/* the battle's font palettes (ED/62C0: 8 of 4 colours) */
static void battle_pal(const Ff6Rom* r, int i, uint32_t pal[4]) {
	const uint8_t* p = at(r, 0xED62C0 + (uint32_t) i * 8);
	for (int k = 0; k < 4; ++k) pal[k] = abgr(rd16(p + 2 * k));
}

/* the game's hand cursor (ED/5AC0, 4bpp, tiles 0, 1, 16, 17; palette
 * D8/E8B8), its top left at x, y */
static void hand(uint32_t* out, const Ff6Rom* rom, int hx, int hy) {
	const uint8_t* cg = at(rom, 0xED5AC0);
	const uint8_t* cp = at(rom, 0xD8E8B8);
	for (int q = 0; q < 4; ++q) {
		int t = (q & 1) + (q >> 1) * 16;
		for (int y = 0; y < 8; ++y) {
			for (int x = 0; x < 8; ++x) {
				int c = pix(cg + t * 32, 4, x, y), px = hx + (q & 1) * 8 + x, py = hy + (q >> 1) * 8 + y;
				if (c && px >= 0 && px < canvasW && py >= 0 && py < canvasH) out[py * canvasW + px] = abgr(rd16(cp + 2 * (c & 3)));
			}
		}
	}
}

void ff6_battle_draw(const Ff6Rom* rom, const Ff6BattleView* v, uint32_t* out) {
	canvasW = FF6_BATTLE_W, canvasH = FF6_BATTLE_H, winUp = 0, textUp = 0;
	for (int i = 0; i < canvasW * canvasH; ++i) out[i] = 0xFF000000u;
	if (!rom) return;
	static uint16_t bg[FF6_BATTLE_W * FF6_BATTLE_H];
	static bool on[FF6_BATTLE_W * FF6_BATTLE_H];
	memset(bg, 0, sizeof(bg));
	memset(on, 0, sizeof(on));
	const uint8_t* gfx = at(rom, 0xED0000 + 0x380 * (uint32_t) v->style);
	window(bg, on, gfx, 0, 0, 38, 13);  /* the party (rows 0 to 14) */
	window(bg, on, gfx, 0, 15, 18, 13); /* the commands */
	window(bg, on, gfx, 20, 15, 18, 13); /* the enemies */
	const uint8_t* defPal = at(rom, 0xED1C00 + 32 * (uint32_t) v->style);
	for (int y = 0; y < canvasH; ++y) {
		for (int x = 0; x < canvasW; ++x) {
			int c = bg[y * canvasW + x];
			if (!c) continue;
			uint16_t col = c <= 7 ? v->winPal[c - 1] : rd16(defPal + 2 * c);
			/* (the menu's gradient, by each window's own lines) */
			out[y * canvasW + x] = abgr(gradient(col, y < 120 ? y * 224 / 120 : (y - 120) * 224 / 120));
		}
	}
	uint32_t white[4], teal[4], yellow[4], grey[4], gFill[4], gFull[4];
	text_pal(rom, 0, white, v->fontColour);
	text_pal(rom, 4, teal, -1);
	text_pal(rom, 2, yellow, -1);
	text_pal(rom, 1, grey, -1);
	battle_pal(rom, 5, gFill);
	battle_pal(rom, 2, gFull);
	/* the party, two by two: portrait, name (yellow: whose turn), HP, MP,
	 * the time gauge */
	for (int s = 0; s < 4; ++s) {
		const Ff6Fighter* f = &v->f[s];
		if (!f->here) continue;
		int bx = 1 + 19 * (s % 2), by = 1 + 6 * (s / 2);
		portrait(rom, out, f->portrait, bx * 8 + 2, by * 8 + 4);
		bool turn = v->menu.open && v->menu.slot == s;
		text(out, rom, f->name, bx + 6, by + 1, turn ? yellow : white);
		text(out, rom, kHP, bx + 6, by + 2, teal);
		number(out, rom, (unsigned) f->hp, 4, false, bx + 8, by + 2, white);
		text(out, rom, kSlash, bx + 12, by + 2, white);
		number(out, rom, (unsigned) f->hpMax, 4, false, bx + 13, by + 2, white);
		if (f->mpMax > 0) {
			text(out, rom, kMP, bx + 6, by + 3, teal);
			number(out, rom, (unsigned) f->mp, 4, false, bx + 8, by + 3, white);
			text(out, rom, kSlash, bx + 12, by + 3, white);
			number(out, rom, (unsigned) f->mpMax, 4, false, bx + 13, by + 3, white);
		}
		if (v->gauge) {
			/* DrawGaugeText: a left end, 4 pieces (GaugeTextTbl: 32 steps), a right end */
			const uint32_t* gp = f->atb == 0xFF ? gFull : gFill;
			int i = f->atb >> 3;
			glyph(out, rom, 0xF9, bx + 6, by + 4, gp);
			for (int k = 0; k < 4; ++k) {
				int n = i + 1 - 8 * k;
				glyph(out, rom, 0xF0 + (n < 0 ? 0 : n > 8 ? 8 : n), bx + 7 + k, by + 4, gp);
			}
			glyph(out, rom, 0xFA, bx + 11, by + 4, gp);
		}
	}
	/* the commands of the character whose turn it is (ROM D8/CEA0: 7 letters
	 * each), greyed ones grey; spaced for fingers */
	if (v->menu.open) {
		for (int k = 0; k < 4; ++k) {
			if (v->menu.cmd[k] < 0) continue;
			const uint8_t* nm = at(rom, 0xD8CEA0 + 7 * (uint32_t) v->menu.cmd[k]);
			char name[8];
			for (int c = 0; c < 7; ++c) name[c] = (char) nm[c];
			name[7] = 0;
			text(out, rom, name, 3, 16 + 3 * k, v->menu.disabled[k] ? grey : white);
		}
	}
	/* the game's hand cursor on the command its cursor is on: the pad's
	 * player sees where it is, the game screen no longer showing the list */
	if (v->menu.open && v->menu.menu == FF6_MENU_COMMANDS) hand(out, rom, 4, (16 + 3 * v->menu.cursor) * 8 - 4);
	/* choosing a target: the hand beside each name and character the
	 * game's cursor covers (FF6-3) */
	bool aiming = v->menu.open && v->menu.menu == FF6_MENU_TARGET;
	for (int s = 0; aiming && s < 4; ++s) {
		if (v->f[s].here && (v->menu.partyTarget >> s & 1)) hand(out, rom, (1 + 19 * (s % 2)) * 8 + 32, (1 + 6 * (s / 2) + 1) * 8 - 4);
	}
	/* the enemies (ROM CF/C050: 10 letters each) */
	for (int k = 0; k < 4; ++k) {
		if (v->enemy[k] < 0) continue;
		const uint8_t* nm = at(rom, 0xCFC050 + 10 * (uint32_t) v->enemy[k]);
		char name[11];
		for (int c = 0; c < 10; ++c) name[c] = (char) nm[c];
		name[10] = 0;
		text(out, rom, name, 22, 16 + 3 * k, white);
		bool covered = false;
		for (int i = 0; aiming && i < 6; ++i) covered |= (v->menu.monTarget >> i & 1) && v->menu.monType[i] == v->enemy[k];
		if (covered) hand(out, rom, 20 * 8 + 4, (16 + 3 * k) * 8 - 4);
	}
}

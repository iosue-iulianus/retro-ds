/* Final Fantasy VI (see ff6.h, docs/games/final-fantasy-vi.md). */
#include "ff6.h"

#include <stdlib.h>
#include <string.h>

bool ff6_is(const char* gameCode) {
	/* (1.1, Rev 1: only banks C1 and C2's battle code differ; every table
	 * RetroDS reads, the RAM and the handlers it tells the game's parts by
	 * are the same) */
	return gameCode && (!strcmp(gameCode, FF6_CODE) || !strcmp(gameCode, FF6_CODE_REV1));
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
	/* each slot's four commands: number, flags (bit 7: greyed), targeting;
	 * Relm controlling a monster ($62CC + slot): its attacks instead
	 * ($205E, the same shape, attack numbers) */
	b->control = emu_read8(e, 0x7E62CC + (uint32_t) b->slot) != 0;
	for (int i = 0; i < 4; ++i) {
		uint32_t a = (b->control ? 0x7E205E : 0x7E202E) + (uint32_t) (b->slot * 12 + i * 3);
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
	b->targetFlags = emu_read8(e, 0x7E7A84);
	b->validParty = emu_read8(e, 0x7E201D) & emu_read8(e, 0x7E61AC) & emu_read8(e, 0x7E61AD) & emu_read8(e, 0x7E6193) & 0x0F;
	b->validMons = emu_read8(e, 0x7E0092) & 0x3F;
	b->attack = emu_read8(e, 0x7E7A85);
	b->actionCmd = -1;
	for (uint32_t k = 0; k < 4; ++k) {
		if (emu_read8(e, 0x7E2BAE + 8 * k) == 0xFF && emu_read8(e, 0x7E2BAF + 8 * k) != 0xFF) {
			b->actionCmd = emu_read8(e, 0x7E2BAF + 8 * k);
			break;
		}
	}
	b->itemHeld = emu_read8(e, 0x7E7B02) != 0;
	b->itemHeldAt = emu_read16(e, 0x7E7B03) / 5;
}

/* ---- FF6-3: touch, written as the game's menu does it */

static const WriteRange ff6Writes[] = {
	{ 0x7E890F, 4, "the command lists' cursors (FF6-3)" },
	{ 0x7E8913, 0x58, "the battle lists' cursors: Magic, Lore, Rage, Dance, Magitek, Item, Throw, Tools (FF6-3)" },
	{ 0x7E7B7D, 2, "the target cursor: the party, the monsters (FF6-3)" },
};

const WriteRange* ff6_write_ranges(int* n) {
	*n = (int) (sizeof ff6Writes / sizeof ff6Writes[0]);
	return ff6Writes;
}

int ff6_command_writes(const Ff6Battle* b, int row, Write* out) {
	if (!b->open || b->menu != FF6_MENU_COMMANDS || row < 0 || row > 3 || b->disabled[row]) return -1;
	out[0] = (Write) { 0x7E890F + (uint32_t) b->slot, (uint8_t) row };
	return 1;
}

/* the targeting flags ($7A84) */
#define T_MANUAL 0x01   /* the cursor can be moved */
#define T_ONE_SIDE 0x02 /* not to the other side */
#define T_AUTO 0x10     /* no A needed */
#define T_ROULETTE 0x80

int ff6_target_writes(const Ff6Battle* b, int want, Write* out) {
	if (!b->open || b->menu != FF6_MENU_TARGET || want < 0 || want > 9) return -1;
	bool party = want < 4;
	int bit = 1 << (party ? want : want - 4);
	/* covered already (a spell on all of them, or the one the cursor's on) */
	if ((party ? b->partyTarget : b->monTarget) & bit) return 0;
	int f = b->targetFlags;
	if ((f & T_ROULETTE) || (f & T_AUTO) || !(f & T_MANUAL)) return -1;
	/* only to the side the cursor's on, for an action kept to one */
	if ((f & T_ONE_SIDE) && (party ? !b->partyTarget : !b->monTarget)) return -1;
	/* a target the game would take: present and targetable */
	if (!((party ? b->validParty : b->validMons) & bit)) return -1;
	out[0] = (Write) { 0x7E7B7D, (uint8_t) (party ? bit : 0) };
	out[1] = (Write) { 0x7E7B7E, (uint8_t) (party ? 0 : bit) };
	return 2;
}

/* the lists (cursor.asm's states): their cursor bytes (+ slot; scroll 0:
 * none), how many across, the most they scroll, how many rows */
typedef struct {
	int menu;
	uint32_t scroll, x, y;
	int cols, maxScroll, rows;
} ListDef;
static const ListDef listDefs[] = {
	{ FF6_MENU_MAGIC, 0x7E8913, 0x7E8917, 0x7E891B, 2, 23, 54 },
	{ FF6_MENU_ITEM, 0x7E8947, 0x7E894B, 0x7E894F, 1, 251, 256 },
	{ FF6_MENU_LORE, 0x7E891F, 0x7E8923, 0x7E8927, 1, 20, 24 },
	{ FF6_MENU_RAGE, 0x7E892B, 0x7E892F, 0x7E8933, 2, 124, 256 },
	{ FF6_MENU_DANCE, 0, 0x7E8937, 0x7E893B, 2, 0, 8 },
	{ FF6_MENU_MAGITEK, 0, 0x7E893F, 0x7E8943, 2, 0, 8 },
	{ FF6_MENU_THROW, 0x7E8953, 0x7E8957, 0x7E895B, 1, 251, 256 },
	{ FF6_MENU_TOOLS, 0x7E895F, 0x7E8963, 0x7E8967, 2, 0, 8 },
};

static const ListDef* list_def(int menu) {
	if (menu == FF6_MENU_ESPER) menu = FF6_MENU_MAGIC; /* (its esper window: the Magic list's page) */
	for (size_t i = 0; i < sizeof listDefs / sizeof listDefs[0]; ++i) {
		if (listDefs[i].menu == menu) return &listDefs[i];
	}
	return NULL;
}

bool ff6_command_menu(int menu) {
	/* (the command list, Row's and Def.'s windows: opening 17 / 19,
	 * closing 18 / 1A, open 24 / 27) */
	return menu == FF6_MENU_COMMANDS || menu == 0x17 || menu == 0x18 || menu == 0x19 || menu == 0x1A || menu == FF6_MENU_ROW || menu == FF6_MENU_DEF;
}

bool ff6_list_menu(int menu) {
	return list_def(menu) != NULL;
}

int ff6_list_writes(const Ff6Battle* b, int index, Write* out) {
	const ListDef* d = list_def(b->menu);
	uint32_t c = (uint32_t) b->slot;
	if (!b->open || !d) return -1;
	if (index == FF6_ESPER && d->menu == FF6_MENU_MAGIC) index = 0; /* (the top row: Up opens the esper) */
	else if (index < 0 || index >= d->rows) return -1;
	int row = index / d->cols, scroll = row < d->maxScroll ? row : d->maxScroll, n = 0;
	if (d->scroll) out[n++] = (Write) { d->scroll + c, (uint8_t) scroll };
	else scroll = 0;
	out[n++] = (Write) { d->y + c, (uint8_t) (row - scroll) };
	out[n++] = (Write) { d->x + c, (uint8_t) (index % d->cols) };
	return n;
}

int ff6_list_keys(const Ff6Battle* b, int index, uint32_t keys[3]) {
	/* the esper's window open: A summons; a spell: Down closes it (the
	 * cursor as written), a pause, A */
	if (b->menu == FF6_MENU_ESPER) return index == FF6_ESPER ? (keys[0] = K_A, 1) : (keys[0] = K_DOWN, keys[1] = 0, keys[2] = K_A, 3);
	if (index == FF6_ESPER) return keys[0] = K_UP, keys[1] = 0, keys[2] = K_A, 3;
	if (b->menu != FF6_MENU_ITEM) return keys[0] = K_A, 1;
	if (b->itemHeld && b->itemHeldAt == index) return keys[0] = K_A, 1;
	int n = 0;
	if (b->itemHeld) keys[n++] = K_B;
	keys[n++] = K_A, keys[n++] = K_A;
	return n;
}

static const uint8_t* at(const Ff6Rom* r, uint32_t a); /* (the ROM at an address: below) */

/* the Blitz codes' buttons (the disassembly's BlitzButtonMaskTbl, by
 * BLITZ_CODE: none, A, B, X, Y, L, R, down-left, down, down-right, right,
 * up-right, up, up-left, left) */
static const uint32_t blitzKeys[15] = {
	0, K_A, K_B, K_X, K_Y, K_L, K_R, K_DOWN | K_LEFT, K_DOWN, K_DOWN | K_RIGHT, K_RIGHT, K_UP | K_RIGHT, K_UP, K_UP | K_LEFT, K_LEFT,
};

/* a Blitz's code from the ROM (C4/7A40: 12 bytes each, 11 buttons, A
 * last, then the count x 2) */
static int blitz_code(const Ff6Rom* rom, int k, uint8_t code[11]) {
	const uint8_t* b = at(rom, 0xC47A40 + 12 * (uint32_t) (k & 7));
	int n = b[11] / 2;
	if (n > 11) n = 11;
	for (int i = 0; i < n; ++i) code[i] = b[i] < 15 ? b[i] : 0;
	return n;
}

int ff6_blitz_keys(const Ff6Rom* rom, int k, uint32_t keys[11]) {
	if (!rom || k < 0 || k > 7) return 0;
	uint8_t code[11];
	int n = blitz_code(rom, k, code);
	for (int i = 0; i < n; ++i) keys[i] = blitzKeys[code[i]];
	return n;
}

/* a list's row: what it holds (FF: nothing), its MP cost or how many,
 * greyed */
static bool list_row(Emu* e, const Ff6Battle* m, int menu, int i, uint8_t* id, uint8_t* val, bool* off) {
	uint32_t slot = (uint32_t) m->slot, a;
	*val = 0, *off = false;
	switch (menu) {
	case FF6_MENU_MAGIC:
	case FF6_MENU_LORE:
		/* the spell lists: $208E + 316 x slot, 4 bytes a row (spell, greyed,
		 * targeting, MP): the esper, 54 spells, 24 lores */
		a = 0x7E208E + 316 * slot + 4 * (uint32_t) (i + (menu == FF6_MENU_MAGIC ? 1 : 55));
		*id = emu_read8(e, a), *off = emu_read8(e, a + 1) & 0x80, *val = emu_read8(e, a + 3);
		break;
	case FF6_MENU_ITEM:
		/* the inventory: $2686, 5 bytes (item, use flags: bit 7 not in
		 * battle, targeting, how many, equip) */
		a = 0x7E2686 + 5 * (uint32_t) i;
		*id = emu_read8(e, a), *off = emu_read8(e, a + 1) & 0x80, *val = emu_read8(e, a + 3);
		break;
	case FF6_MENU_RAGE:
		*id = emu_read8(e, 0x7E257E + (uint32_t) i); /* (the rages known: monsters) */
		break;
	case FF6_MENU_DANCE:
		*id = emu_read8(e, 0x7E267E + (uint32_t) i); /* (bit 7: not known) */
		if (*id & 0x80) *id = 0xFF;
		break;
	case FF6_MENU_MAGITEK: {
		/* the ROM's tables (C1/910C Terra's, C1/9114 the others': FF none),
		 * Terra by her battle graphic ($2EAE + 32 x slot, 0) */
		static const uint8_t terraTek[8] = { 0, 1, 2, 3, 4, 5, 6, 7 }, otherTek[8] = { 0, 1, 2, 0xFF, 4, 0xFF, 0xFF, 0xFF };
		bool terra = emu_read8(e, 0x7E2EAE + 32 * slot) == 0;
		*id = i < 8 ? (terra ? terraTek : otherTek)[i] : 0xFF;
		break;
	}
	case FF6_MENU_THROW:
	case FF6_MENU_TOOLS:
		/* the list the game filters for them: $4005, 3 bytes (item, how
		 * many, targeting) */
		a = 0x7E4005 + 3 * (uint32_t) i;
		*id = emu_read8(e, a), *val = emu_read8(e, a + 1);
		break;
	default:
		return false;
	}
	return *id != 0xFF;
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
static int clipX = 1 << 20, clipY = 1 << 20; /* text isn't drawn right of and below both (the Back window) */

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
			if (v && px < canvasW && py >= 0 && py < canvasH && !(px >= clipX && py >= clipY)) out[py * canvasW + px] = pal[v];
		}
	}
}

/* a glyph mirrored (h) and / or upside down (v): the font has an up
 * arrow, a right one and a down-left one, not the others */
static void glyph_flip(uint32_t* out, const Ff6Rom* r, int code, int tx, int ty, const uint32_t pal[4], bool h, bool v) {
	const uint8_t* t = at(r, 0xC47FC0 + (uint32_t) code * 16);
	for (int y = 0; y < 8; ++y) {
		for (int x = 0; x < 8; ++x) {
			int c = pix(t, 2, h ? 7 - x : x, v ? 7 - y : y), px = tx * 8 + x, py = ty * 8 + y - textUp;
			if (c && px < canvasW && py >= 0 && py < canvasH && !(px >= clipX && py >= clipY)) out[py * canvasW + px] = pal[c];
		}
	}
}

static void glyph_flipped(uint32_t* out, const Ff6Rom* r, int code, int tx, int ty, const uint32_t pal[4]) {
	glyph_flip(out, r, code, tx, ty, pal, false, true);
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
static const char kBack[] = { (char) 0x81, (char) 0x9A, (char) 0x9C, (char) 0xA4, 0 }; /* Back */
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
	/* (as a battle starts, for about a second, its copies aren't made yet:
	 * $201D is FF and every HP FFFF) */
	v->ready = shown && !(shown & 0xF0);
	for (int s = 0; s < 4; ++s) {
		Ff6Fighter* f = &v->f[s];
		/* the battle's own copy for its windows ($2EAE + 32 x slot) */
		uint32_t b = 0x7E2EAE + 32 * (uint32_t) s;
		f->here = (shown >> s & 1) != 0;
		if (!f->here) continue;
		f->portrait = emu_read8(e, b);
		for (int k = 0; k < 6; ++k) f->name[k] = (char) emu_read8(e, b + 1 + (uint32_t) k);
		f->hp = emu_read16(e, b + 7), f->hpMax = emu_read16(e, b + 9);
		if (f->hp == 0xFFFF || f->hpMax == 0xFFFF) v->ready = false;
		f->mp = emu_read16(e, b + 11), f->mpMax = emu_read16(e, b + 13);
		f->atb = emu_read8(e, 0x7E619E + (uint32_t) s);
		/* the first status, as the game's status window names it
		 * (GetFirstStatus: status 1's bit 7 first... then status 3 and 4's) */
		uint16_t st12 = (uint16_t) (emu_read8(e, b + 15) << 8 | emu_read8(e, b + 16));
		uint16_t st34 = (uint16_t) (emu_read8(e, b + 17) << 8 | emu_read8(e, b + 18));
		f->status = -1;
		for (int k = 0; k < 32 && f->status < 0; ++k) {
			if ((k < 16 ? st12 : st34) >> (15 - (k & 15)) & 1) f->status = k;
		}
	}
	/* every enemy alive, a row each (the game lists each kind once), top
	 * to bottom as they stand */
	for (int i = 0; i < 6; ++i) {
		if (!(v->menu.monAlive >> i & 1) || v->menu.monType[i] < 0) continue;
		int k = v->nEnemies++;
		while (k > 0 && v->menu.monY[v->enemy[k - 1]] > v->menu.monY[i]) v->enemy[k] = v->enemy[k - 1], --k;
		v->enemy[k] = i;
	}
	/* Blitz: the ones Sabin knows ($1D28); SwdTech: how many Cyan knows
	 * ($2020 + 1), the bar ($7B82: 32 a level, 4 frames a step) */
	v->blitzWant = v->bushidoWant = -1;
	v->blitzKnown = emu_read8(e, 0x7E1D28);
	v->bushidoKnown = (emu_read8(e, 0x7E2020) & 7) + 1;
	v->bushidoBar = emu_read8(e, 0x7E7B82);
	/* an open list, as the game lays it out: every place from the top (the
	 * game's cursor stops on empty ones too), as far as the last that holds
	 * something or the cursor; the esper's line above Magic's */
	v->listCursor = v->listCursorLine = -1;
	const Ff6Battle* m = &v->menu;
	const ListDef* d = m->open ? list_def(m->menu) : NULL;
	if (d) {
		uint32_t c = (uint32_t) m->slot;
		int at = ((d->scroll ? emu_read8(e, d->scroll + c) : 0) + emu_read8(e, d->y + c)) * d->cols + emu_read8(e, d->x + c);
		v->listCols = d->cols;
		v->listShowVal = d->menu == FF6_MENU_MAGIC || d->menu == FF6_MENU_ITEM || d->menu == FF6_MENU_LORE || d->menu == FF6_MENU_THROW;
		if (d->menu == FF6_MENU_MAGIC && !emu_read8(e, 0x7E7AE8)) {
			/* the esper (the game's: Up from the top row; not for X-Magic's
			 * spells, $7AE8, where Up doesn't open it) */
			uint32_t a = 0x7E208E + 316 * c;
			uint8_t id = emu_read8(e, a);
			if (id != 0xFF) {
				v->listEsper = true, v->esperId = id;
				v->esperOff = emu_read8(e, a + 1) & 0x80, v->esperVal = emu_read8(e, a + 3);
			}
		}
		int last = -1;
		for (int i = 0; i < d->rows && i < 256; ++i) {
			uint8_t id, val;
			bool off;
			if (!list_row(e, m, d->menu, i, &id, &val, &off)) id = 0xFF, val = 0, off = false;
			else last = i;
			v->listId[i] = id, v->listVal[i] = val, v->listOff[i] = off;
		}
		if (m->menu == FF6_MENU_ESPER && v->listEsper) v->listCursor = FF6_ESPER, v->listCursorLine = 0;
		else if (m->menu != FF6_MENU_ESPER && at < d->rows) v->listCursor = at, v->listCursorLine = (v->listEsper ? 1 : 0) + at / d->cols;
		if (v->listCursor >= 0 && v->listCursor != FF6_ESPER && v->listCursor > last) last = v->listCursor;
		int rows = last < 0 ? 1 : last / d->cols + 1; /* (whole rows, at least one) */
		v->nList = rows * d->cols;
		if (v->nList > d->rows) v->nList = d->rows;
		v->nLines = (v->listEsper ? 1 : 0) + rows;
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
static int handXY[16], nHands;
int ff6_battle_hands(int* xy, int max) {
	int n = nHands < max ? nHands : max;
	memcpy(xy, handXY, sizeof(int) * 2 * (size_t) n);
	return n;
}

static void hand(uint32_t* out, const Ff6Rom* rom, int hx, int hy) {
	if (nHands < 8) handXY[2 * nHands] = hx, handXY[2 * nHands + 1] = hy, ++nHands;
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

/* ---- FF6-2, FF6-3: the battle's screens, laid out as the DS's Chrono
 * Trigger lays its own (pages of big buttons), in the game's windows, font
 * and art. In tiles of 8 pixels on a 40 x 30 screen. */

/* windows: drawn into bg in order (a later one over an earlier), each
 * with the menu's gradient down its own height */
typedef struct { int tx, ty, w, h; } Win;
static Win wins[24];
static int nWins;

static void add_win(int tx, int ty, int w, int h) {
	if (nWins < 24) wins[nWins++] = (Win) { tx, ty, w, h };
}

static void draw_wins(const Ff6Rom* rom, int style, const uint16_t winPal[7], uint32_t* out) {
	static uint16_t bg[FF6_BATTLE_W * FF6_BATTLE_H];
	static bool on[FF6_BATTLE_W * FF6_BATTLE_H];
	const uint8_t* gfx = at(rom, 0xED0000 + 0x380 * (uint32_t) style);
	const uint8_t* defPal = at(rom, 0xED1C00 + 32 * (uint32_t) style);
	for (int k = 0; k < nWins; ++k) {
		const Win* w = &wins[k];
		memset(bg, 0, sizeof(bg[0]) * (size_t) (canvasW * canvasH));
		memset(on, 0, sizeof(on[0]) * (size_t) (canvasW * canvasH));
		window(bg, on, gfx, w->tx, w->ty, w->w, w->h);
		int y0 = w->ty * 8, hh = (w->h + 2) * 8;
		for (int y = y0; y < y0 + hh && y < canvasH; ++y) {
			for (int x = w->tx * 8; x < (w->tx + w->w + 2) * 8 && x < canvasW; ++x) {
				int c = bg[y * canvasW + x];
				if (!c) continue;
				uint16_t col = c <= 7 ? winPal[c - 1] : rd16(defPal + 2 * c);
				out[y * canvasW + x] = abgr(gradient(col, (y - y0) * 224 / hh));
			}
		}
	}
	nWins = 0;
}

typedef struct { uint32_t white[4], teal[4], yellow[4], grey[4], gFill[4], gFull[4]; } Pals;

static void battle_pals(const Ff6Rom* rom, const Ff6BattleView* v, Pals* p) {
	text_pal(rom, 0, p->white, v->fontColour);
	text_pal(rom, 4, p->teal, -1);
	text_pal(rom, 2, p->yellow, -1);
	text_pal(rom, 1, p->grey, -1);
	battle_pal(rom, 5, p->gFill);
	battle_pal(rom, 2, p->gFull);
}

/* the time gauge (DrawGaugeText: a left end, 4 pieces of GaugeTextTbl's
 * 32 steps, a right end), 6 tiles from tx */
static void gauge(uint32_t* out, const Ff6Rom* rom, const Pals* p, int atb, int tx, int ty) {
	const uint32_t* gp = atb == 0xFF ? p->gFull : p->gFill;
	int i = atb >> 3;
	glyph(out, rom, 0xF9, tx, ty, gp);
	for (int k = 0; k < 4; ++k) {
		int n = i + 1 - 8 * k;
		glyph(out, rom, 0xF0 + (n < 0 ? 0 : n > 8 ? 8 : n), tx + 1 + k, ty, gp);
	}
	glyph(out, rom, 0xFA, tx + 5, ty, gp);
}

/* a longer gauge of the game's pieces: `pieces` of 8 steps between its
 * ends, filled to `fill` of 128 */
static void long_gauge(uint32_t* out, const Ff6Rom* rom, const Pals* p, int fill, int tx, int ty, int pieces) {
	int steps = fill * pieces * 8 / 128;
	glyph(out, rom, 0xF9, tx, ty, p->gFill);
	for (int k = 0; k < pieces; ++k) {
		int n = steps - 8 * k;
		glyph(out, rom, 0xF0 + (n < 0 ? 0 : n > 8 ? 8 : n), tx + 1 + k, ty, p->gFill);
	}
	glyph(out, rom, 0xFA, tx + 1 + pieces, ty, p->gFill);
}

/* a character as the party window shows them: portrait, name (yellow:
 * whose turn), HP, MP, the time gauge; bx, by the block's top left */
static void fighter(uint32_t* out, const Ff6Rom* rom, const Ff6BattleView* v, const Pals* p, int s, int bx, int by) {
	const Ff6Fighter* f = &v->f[s];
	portrait(rom, out, f->portrait, bx * 8 + 2, by * 8 + 4);
	bool turn = v->menu.open && v->menu.slot == s;
	text(out, rom, f->name, bx + 6, by + 1, turn ? p->yellow : p->white);
	/* (a space after HP and MP: 4 digits ran into them, as in the game's
	 * menu, and the battle's windows have the room) */
	text(out, rom, kHP, bx + 6, by + 2, p->teal);
	number(out, rom, (unsigned) f->hp, 4, false, bx + 9, by + 2, p->white);
	text(out, rom, kSlash, bx + 13, by + 2, p->white);
	number(out, rom, (unsigned) f->hpMax, 4, false, bx + 14, by + 2, p->white);
	if (f->mpMax > 0) {
		text(out, rom, kMP, bx + 6, by + 3, p->teal);
		number(out, rom, (unsigned) f->mp, 4, false, bx + 9, by + 3, p->white);
		text(out, rom, kSlash, bx + 13, by + 3, p->white);
		number(out, rom, (unsigned) f->mpMax, 4, false, bx + 14, by + 3, p->white);
	}
	if (v->gauge) gauge(out, rom, p, f->atb, bx + 6, by + 4);
}

static void name_of(const Ff6Rom* rom, uint32_t table, int len, int i, char* out) {
	const uint8_t* nm = at(rom, table + (uint32_t) (len * i));
	for (int c = 0; c < len; ++c) out[c] = (char) nm[c];
	out[len] = 0;
}

/* an attack's name: spells E6/F567 (7), esper attacks E6/FE8F (10, from
 * 36), the rest E6/F7B9 (10, from 51) */
static void attack_name(const Ff6Rom* rom, int id, char* out) {
	if (id < 0x36) name_of(rom, 0xE6F567, 7, id, out);
	else if (id < 0x51) name_of(rom, 0xE6FE8F, 10, id - 0x36, out);
	else name_of(rom, 0xE6F7B9, 10, id - 0x51 < 175 ? id - 0x51 : 0, out);
}

/* a list row's name, from the ROM: spells E6/F567 (7: an icon, 6
 * letters), espers E6/F6E1 (8; their attacks 36 on), items D2/B300 (13:
 * an icon, 12), attacks E6/F7B9 (10, from attack 51: lores from 8B, by
 * the lore's number; Magitek's from 83), monsters CF/C050 (10: rages), dances E6/FF9D (12) */
static void list_name(const Ff6Rom* rom, int menu, bool esper, int id, char* out) {
	if (esper) name_of(rom, 0xE6F6E1, 8, id < 27 ? id : 0, out);
	else if (menu == FF6_MENU_MAGIC) name_of(rom, 0xE6F567, 7, id < 54 ? id : 0, out);
	else if (menu == FF6_MENU_ITEM || menu == FF6_MENU_THROW || menu == FF6_MENU_TOOLS) name_of(rom, 0xD2B300, 13, id, out);
	else if (menu == FF6_MENU_LORE) name_of(rom, 0xE6F7B9, 10, 0x8B - 0x51 + (id < 24 ? id : 0), out);
	else if (menu == FF6_MENU_MAGITEK) name_of(rom, 0xE6F7B9, 10, 0x83 + id - 0x51, out);
	else if (menu == FF6_MENU_RAGE) name_of(rom, 0xCFC050, 10, id, out);
	else if (menu == FF6_MENU_DANCE) name_of(rom, 0xE6FF9D, 12, id & 7, out);
	else out[0] = 0;
}

int ff6_battle_page(const Ff6BattleView* v) {
	if (!v->menu.open) return FF6_PAGE_WAIT;
	if (ff6_list_menu(v->menu.menu)) return FF6_PAGE_LIST;
	if (v->menu.menu == FF6_MENU_BLITZ) return FF6_PAGE_BLITZ;
	if (v->menu.menu == FF6_MENU_BUSHIDO) return FF6_PAGE_BUSHIDO;
	return v->menu.menu == FF6_MENU_TARGET ? FF6_PAGE_TARGET : FF6_PAGE_COMMANDS;
}

/* the pages' places (tiles): the enemies' grid (two across, three down),
 * the party's row with Back at its end */
#define GRID_TX(i) (20 * ((i) % 2))
#define GRID_TY(i) (8 + 5 * ((i) / 2))
#define ROW_TX(j) (8 * (j))
#define ROW_TY 23
/* the lists' lines, 3 tiles high (a window one line inside) */
#define LIST_TY(l) (8 + 3 * (l))
/* Blitz's buttons: two across, four down, two lines each (the name, the
 * code); SwdTech's, two across, a line each; Back alone under them */
#define LIST_TX(i) (20 * ((i) % 2))
#define LIST2_TY(i) LIST_TY((i) / 2)
#define BLITZ_TX(i) (20 * ((i) % 2))
#define BLITZ_TY(i) (8 + 4 * ((i) / 2))
#define BACK_TX 32
#define BACK_TY 24

/* the command page's buttons, as the game's cursor moves (Win: a window's
 * top left and inside size, in tiles): the Window set (and Relm's
 * Control) a column, Row left of it and Def. right (the pad's Left and
 * Right open them); the Short set the game's cross (Up, Left, Right,
 * Down choose its four), Row and Def. at the sides (L, R) */
static Win command_box(const Ff6Battle* m, int k) {
	static const Win cross[4] = { { 14, 8, 10, 4 }, { 8, 14, 10, 4 }, { 20, 14, 10, 4 }, { 14, 20, 10, 4 } };
	if (m->shortSet && !m->control) return cross[k & 3];
	return (Win) { 8, 8 + 5 * k, 22, 3 };
}
#define ROWDEF_TX(j) ((j) ? 32 : 0)
#define ROWDEF_TY 8
#define ROWDEF_H 18
static bool in_win(Win b, int tx, int ty) {
	return tx >= b.tx && tx < b.tx + b.w + 2 && ty >= b.ty && ty < b.ty + b.h + 2;
}
/* a list's place on its page: the line (from the page's top) and the
 * column, a window one line high; the esper's line full width */
static Win list_box(const Ff6BattleView* v, int line, int col) {
	if (v->listCols == 1) return (Win) { 0, LIST_TY(line), 38, 1 };
	return (Win) { 20 * col, LIST_TY(line), 18, 1 };
}
/* the place (or FF6_ESPER) on a line of the page and column; -1 none */
static int list_at(const Ff6BattleView* v, int line, int col) {
	int l = v->listTop + line;
	if (l < 0 || l >= v->nLines) return -1;
	if (v->listEsper && l == 0) return FF6_ESPER;
	int i = (l - (v->listEsper ? 1 : 0)) * v->listCols + col;
	return col < v->listCols && i < v->nList ? i : -1;
}

void ff6_list_follow(Ff6BattleView* v, int* top, int* seenLine, int* seenMenu) {
	int lm = ff6_battle_page(v) == FF6_PAGE_LIST ? (v->menu.menu == FF6_MENU_ESPER ? FF6_MENU_MAGIC : v->menu.menu) : 0;
	if (lm != *seenMenu) *seenMenu = lm, *seenLine = -2, *top = 0;
	if (lm && v->listCursorLine != *seenLine) {
		*seenLine = v->listCursorLine;
		if (*seenLine >= 0 && *seenLine < *top) *top = *seenLine;
		else if (*seenLine >= *top + FF6_LIST_LINES) *top = *seenLine - FF6_LIST_LINES + 1;
	}
	if (*top > v->nLines - FF6_LIST_LINES) *top = v->nLines - FF6_LIST_LINES;
	if (*top < 0) *top = 0;
	v->listTop = *top;
}

int ff6_battle_hit(const Ff6BattleView* v, int x, int y, int* arg) {
	int tx = x / 8, ty = y / 8, page = ff6_battle_page(v);
	*arg = -1;
	if (page == FF6_PAGE_COMMANDS) {
		if (tx < 20 && ty < 8) return FF6_HIT_STATUS;
		for (int j = 0; j < 2 && !v->menu.control; ++j) {
			if (in_win((Win) { ROWDEF_TX(j), ROWDEF_TY, 6, ROWDEF_H }, tx, ty)) return j ? FF6_HIT_DEF : FF6_HIT_ROWCMD;
		}
		for (int k = 0; k < 4; ++k) {
			if (in_win(command_box(&v->menu, k), tx, ty)) return *arg = k, FF6_HIT_COMMAND;
		}
	} else if (page == FF6_PAGE_LIST) {
		for (int l = 0; l < FF6_LIST_LINES; ++l) {
			for (int c = 0; c < v->listCols; ++c) {
				int i = list_at(v, l, c);
				Win b = i == FF6_ESPER ? (Win) { 0, LIST_TY(l), 38, 1 } : list_box(v, l, c);
				if (i >= 0 && (i == FF6_ESPER || v->listId[i] != 0xFF) && in_win(b, tx, ty)) return *arg = i, FF6_HIT_ROW;
				if (i == FF6_ESPER) break;
			}
		}
		if (ty >= ROW_TY) {
			int j = tx / 8;
			if (j == 0 && v->listTop > 0) return FF6_HIT_UP;
			if (j == 1 && v->listTop + FF6_LIST_LINES < v->nLines) return FF6_HIT_DOWN;
			if (j == 4) return FF6_HIT_BACK;
		}
	} else if (page == FF6_PAGE_BLITZ) {
		for (int i = 0, k = 0; k < 8; ++k) {
			if (!(v->blitzKnown >> k & 1)) continue;
			if (tx >= BLITZ_TX(i) && tx < BLITZ_TX(i) + 20 && ty >= BLITZ_TY(i) && ty < BLITZ_TY(i) + 4) return *arg = k, FF6_HIT_ROW;
			++i;
		}
		if (tx >= BACK_TX && ty >= BACK_TY) return FF6_HIT_BACK;
	} else if (page == FF6_PAGE_BUSHIDO) {
		for (int k = 0; k < v->bushidoKnown; ++k) {
			if (tx >= LIST_TX(k) && tx < LIST_TX(k) + 20 && ty >= LIST2_TY(k) && ty < LIST2_TY(k) + 3) return *arg = k, FF6_HIT_ROW;
		}
		if (tx >= BACK_TX && ty >= BACK_TY) return FF6_HIT_BACK;
	} else if (page == FF6_PAGE_TARGET) {
		for (int i = 0; i < v->nEnemies; ++i) {
			if (tx >= GRID_TX(i) && tx < GRID_TX(i) + 20 && ty >= GRID_TY(i) && ty < GRID_TY(i) + 5) return *arg = v->enemy[i], FF6_HIT_ENEMY;
		}
		if (ty >= ROW_TY) {
			int j = tx / 8;
			if (j == 4) return FF6_HIT_BACK;
			if (j < 4 && v->f[j].here) return *arg = j, FF6_HIT_PARTY;
		}
	}
	return FF6_HIT_NONE;
}

void ff6_battle_draw(const Ff6Rom* rom, const Ff6BattleView* v, uint32_t* out) {
	nHands = 0;
	canvasW = FF6_BATTLE_W, canvasH = FF6_BATTLE_H, winUp = 0, textUp = 0;
	for (int i = 0; i < canvasW * canvasH; ++i) out[i] = 0xFF000000u;
	if (!rom) return;
	Pals p;
	battle_pals(rom, v, &p);
	int page = ff6_battle_page(v);
	const Ff6Battle* m = &v->menu;
	if (page == FF6_PAGE_WAIT) {
		/* the party, two by two; the enemies below, two across */
		add_win(0, 0, 38, 13);
		add_win(0, 15, 38, 13);
		draw_wins(rom, v->style, v->winPal, out);
		for (int s = 0; s < 4; ++s) {
			if (v->f[s].here) fighter(out, rom, v, &p, s, 1 + 19 * (s % 2), 1 + 6 * (s / 2));
		}
		for (int i = 0; i < v->nEnemies; ++i) {
			char name[11];
			name_of(rom, 0xCFC050, 10, m->monType[v->enemy[i]], name);
			text(out, rom, name, 3 + 19 * (i % 2), 17 + 3 * (i / 2), p.white);
		}
		return;
	}
	/* whose turn it is, top left */
	add_win(0, 0, 18, 6);
	if (page == FF6_PAGE_COMMANDS) {
		add_win(20, 0, 18, 6); /* the enemies, to read */
		for (int k = 0; k < 4; ++k) {
			Win b = command_box(m, k);
			add_win(b.tx, b.ty, b.w, b.h);
		}
		if (!m->control) {
			add_win(ROWDEF_TX(0), ROWDEF_TY, 6, ROWDEF_H); /* Row, Def. (the pad's Left and Right; L, R) */
			add_win(ROWDEF_TX(1), ROWDEF_TY, 6, ROWDEF_H);
		}
	} else if (page == FF6_PAGE_BLITZ || page == FF6_PAGE_BUSHIDO) {
		add_win(20, 0, 18, 6);
		for (int i = 0; i < 8; ++i) {
			if (page == FF6_PAGE_BLITZ) add_win(BLITZ_TX(i), BLITZ_TY(i), 18, 2);
			else add_win(LIST_TX(i), LIST2_TY(i), 18, 1);
		}
		add_win(BACK_TX, BACK_TY, 6, 4);
	} else if (page == FF6_PAGE_LIST) {
		add_win(20, 0, 18, 6);
		/* a window for each place that holds something (the gaps empty) */
		for (int l = 0; l < FF6_LIST_LINES; ++l) {
			for (int c = 0; c < v->listCols; ++c) {
				int i = list_at(v, l, c);
				if (i == FF6_ESPER) {
					add_win(0, LIST_TY(l), 38, 1);
					break;
				}
				Win b = list_box(v, l, c);
				if (i >= 0 && v->listId[i] != 0xFF) add_win(b.tx, b.ty, b.w, b.h);
			}
		}
		add_win(ROW_TX(0), ROW_TY, 6, 5);
		add_win(ROW_TX(1), ROW_TY, 6, 5);
		add_win(ROW_TX(2), ROW_TY, 14, 5);
		add_win(ROW_TX(4), ROW_TY, 6, 5);
	} else {
		add_win(20, 0, 18, 6);
		for (int i = 0; i < 6; ++i) add_win(GRID_TX(i), GRID_TY(i), 18, 3);
		for (int j = 0; j < 5; ++j) add_win(ROW_TX(j), ROW_TY, 6, 5);
	}
	draw_wins(rom, v->style, v->winPal, out);
	if (v->f[m->slot].here) fighter(out, rom, v, &p, m->slot, 1, 1);
	if (page == FF6_PAGE_COMMANDS) {
		/* the commands (ROM D8/CEA0: 7 letters each), a button each, greyed
		 * ones grey; the game's hand on the one its cursor is on */
		for (int k = 0; k < 4; ++k) {
			Win b = command_box(m, k);
			int ty = b.ty + 1 + (b.h - 1) / 2; /* (the middle line) */
			if (m->cmd[k] >= 0) {
				char name[11];
				if (m->control) attack_name(rom, m->cmd[k], name); /* (a controlled monster's attacks) */
				else name_of(rom, 0xD8CEA0, 7, m->cmd[k], name);
				text(out, rom, name, b.tx + 3, ty, m->disabled[k] ? p.grey : p.white);
			}
			if (m->menu == FF6_MENU_COMMANDS && m->cursor == k) hand(out, rom, (b.tx + 1) * 8, ty * 8 - 4);
		}
		if (!m->control) {
			/* Row and Def. (the command names 14, 15); the hand on the one
			 * the pad has opened (the game's states 24, 27) */
			for (int j = 0; j < 2; ++j) {
				char name[8];
				int ty = ROWDEF_TY + 1 + (ROWDEF_H - 1) / 2;
				name_of(rom, 0xD8CEA0, 7, 0x14 + j, name);
				text(out, rom, name, ROWDEF_TX(j) + 3, ty, p.white);
				if (m->menu == (j ? FF6_MENU_DEF : FF6_MENU_ROW)) hand(out, rom, (ROWDEF_TX(j) + 1) * 8, ty * 8 - 4);
			}
		}
		for (int i = 0; i < v->nEnemies; ++i) {
			char name[11];
			name_of(rom, 0xCFC050, 10, m->monType[v->enemy[i]], name);
			text(out, rom, name, 22, 1 + i, p.white);
		}
		return;
	}
	if (page == FF6_PAGE_BLITZ || page == FF6_PAGE_BUSHIDO) {
		if (m->cmd[m->cursor & 3] >= 0) {
			char name[8];
			name_of(rom, 0xD8CEA0, 7, m->cmd[m->cursor & 3], name);
			text(out, rom, name, 22, 2, p.white);
		}
		if (page == FF6_PAGE_BLITZ) {
			/* the Blitzes Sabin knows: the name (attacks from 5D), the code in
			 * the font's letters and arrows (up D4, right D5, down-left D6,
			 * turned for the others) */
			for (int i = 0, k = 0; k < 8; ++k) {
				if (!(v->blitzKnown >> k & 1)) continue;
				char name[11];
				name_of(rom, 0xE6F7B9, 10, 0x5D - 0x51 + k, name);
				text(out, rom, name, BLITZ_TX(i) + 2, BLITZ_TY(i) + 1, k == v->blitzWant ? p.yellow : p.white);
				uint8_t code[11];
				int n = blitz_code(rom, k, code);
				for (int j = 0; j < n - 1 && j < 10; ++j) { /* (the last, A, left out) */
					int c = code[j], x = BLITZ_TX(i) + 3 + j, y = BLITZ_TY(i) + 2;
					static const char letters[7] = { 0, 'A', 'B', 'X', 'Y', 'L', 'R' };
					if (c >= 1 && c <= 6) glyph(out, rom, 0x80 + letters[c] - 'A', x, y, p.teal);
					else if (c == 8 || c == 12) glyph_flip(out, rom, 0xD4, x, y, p.teal, false, c == 8);
					else if (c == 10 || c == 14) glyph_flip(out, rom, 0xD5, x, y, p.teal, c == 14, false);
					else if (c == 7 || c == 9 || c == 11 || c == 13) glyph_flip(out, rom, 0xD6, x, y, p.teal, c == 9 || c == 11, c == 11 || c == 13);
				}
				++i;
			}
		} else {
			/* the bar, as the game's gauge (its pieces, 0 to 255 over the
			 * levels known), the techniques Cyan knows (CF/3C40: 12 letters),
			 * the hand on the bar's level, the one tapped yellow till it comes */
			int full = v->bushidoKnown * 32, level = v->bushidoBar >> 5;
			long_gauge(out, rom, &p, full ? (v->bushidoBar & 0xFF) * 128 / full : 0, 22, 4, 14);
			for (int k = 0; k < v->bushidoKnown; ++k) {
				char name[13];
				name_of(rom, 0xCF3C40, 12, k, name);
				text(out, rom, name, LIST_TX(k) + 2, LIST2_TY(k) + 1, k == v->bushidoWant ? p.yellow : p.white);
				if (k == level) hand(out, rom, LIST_TX(k) * 8, (LIST2_TY(k) + 1) * 8 - 4);
			}
		}
		text(out, rom, kBack, BACK_TX + 2, BACK_TY + 3, p.white);
		return;
	}
	if (page == FF6_PAGE_LIST) {
		/* the list: the command, the row the game's cursor is on; a button
		 * a row (the game's icon and name, the MP cost or how many; greyed
		 * ones grey), the game's hand on its cursor's; the page's arrows,
		 * which page, Back */
		if (m->cmd[m->cursor & 3] >= 0) {
			char name[8];
			name_of(rom, 0xD8CEA0, 7, m->cmd[m->cursor & 3], name);
			text(out, rom, name, 22, 2, p.white);
		}
		int lm = m->menu == FF6_MENU_ESPER ? FF6_MENU_MAGIC : m->menu;
		char name[14];
		if (v->listCursor == FF6_ESPER) list_name(rom, lm, true, v->esperId, name), text(out, rom, name, 22, 4, p.yellow);
		else if (v->listCursor >= 0 && v->listId[v->listCursor] != 0xFF) list_name(rom, lm, false, v->listId[v->listCursor], name), text(out, rom, name, 22, 4, p.yellow);
		for (int l = 0; l < FF6_LIST_LINES; ++l) {
			for (int c = 0; c < v->listCols; ++c) {
				int i = list_at(v, l, c);
				if (i < 0) continue;
				bool esper = i == FF6_ESPER;
				Win b = esper ? (Win) { 0, LIST_TY(l), 38, 1 } : list_box(v, l, c);
				if (!esper && v->listId[i] != 0xFF) {
					bool off = esper ? v->esperOff : v->listOff[i];
					const uint32_t* pal = off ? p.grey : p.white;
					list_name(rom, lm, false, v->listId[i], name);
					text(out, rom, name, b.tx + 2, b.ty + 1, pal);
					if (v->listShowVal) number(out, rom, v->listVal[i], 2, false, b.tx + b.w - 2, b.ty + 1, pal);
				} else if (esper) {
					const uint32_t* pal = v->esperOff ? p.grey : p.white;
					list_name(rom, lm, true, v->esperId, name);
					text(out, rom, name, b.tx + 2, b.ty + 1, pal);
					number(out, rom, v->esperVal, 2, false, b.tx + b.w - 2, b.ty + 1, pal);
				}
				/* (the hand on an empty place too: the game's cursor stops there) */
				if (i == v->listCursor) hand(out, rom, b.tx * 8, (b.ty + 1) * 8 - 4);
				if (esper) break;
			}
		}
		/* the arrows: the font's up arrow (D4), drawn upside down for down;
		 * which page (the one most of the lines shown are on) */
		glyph(out, rom, v->listTop > 0 ? 0xD4 : CH_BLANK, ROW_TX(0) + 3, ROW_TY + 4, p.white);
		if (v->listTop + FF6_LIST_LINES < v->nLines) glyph_flipped(out, rom, 0xD4, ROW_TX(1) + 3, ROW_TY + 4, p.white);
		int pages = (v->nLines + FF6_LIST_LINES - 1) / FF6_LIST_LINES;
		if (pages > 1) {
			int dg = pages > 9 ? 2 : 1, page = (v->listTop + FF6_LIST_LINES / 2) / FF6_LIST_LINES + 1;
			if (v->listTop + FF6_LIST_LINES >= v->nLines) page = pages;
			number(out, rom, (unsigned) page, dg, false, ROW_TX(2) + 7 - dg, ROW_TY + 4, p.white);
			text(out, rom, kSlash, ROW_TX(2) + 7, ROW_TY + 4, p.white);
			number(out, rom, (unsigned) pages, dg, false, ROW_TX(2) + 8, ROW_TY + 4, p.white);
		}
		text(out, rom, kBack, ROW_TX(4) + 2, ROW_TY + 4, p.white);
		return;
	}
	/* choosing a target: the command and the target the cursor is on, the
	 * enemies' buttons (every one alive, top to bottom as they stand; the
	 * hand on those the cursor covers), the party's, Back */
	if (m->cursor >= 0 && m->cmd[m->cursor & 3] >= 0) {
		/* (Magic and Item: the spell or item chosen, $7A85) */
		int cmd = m->actionCmd >= 0 ? m->actionCmd : m->cmd[m->cursor & 3], a = m->attack;
		char name[14];
		if ((cmd == 0x02 || cmd == 0x17) && a < 54) list_name(rom, FF6_MENU_MAGIC, false, a, name); /* (Magic, X-Magic) */
		else if (cmd == 0x19 && a < 27) list_name(rom, FF6_MENU_MAGIC, true, a, name); /* (Summon: the esper) */
		else if ((cmd == 0x01 || cmd == 0x08 || cmd == 0x09) && a < 0xFF) list_name(rom, FF6_MENU_ITEM, false, a, name); /* (Item, Throw, Tools) */
		else if (cmd == 0x0C && a < 24) list_name(rom, FF6_MENU_LORE, false, a, name);
		else if (cmd == 0x1D && a < 8) list_name(rom, FF6_MENU_MAGITEK, false, a, name);
		else if (cmd == 0x10) list_name(rom, FF6_MENU_RAGE, false, a, name);
		else if (cmd == 0x13 && a < 8) list_name(rom, FF6_MENU_DANCE, false, a, name);
		else name_of(rom, 0xD8CEA0, 7, cmd, name);
		text(out, rom, name, 22, 2, p.white);
	}
	int covered = 0, at = -1;
	for (int i = 0; i < 6; ++i) covered += m->monTarget >> i & 1, at = m->monTarget >> i & 1 ? i : at;
	for (int s = 0; s < 4; ++s) covered += m->partyTarget >> s & 1, at = m->partyTarget >> s & 1 ? 8 + s : at;
	if (covered == 1 && at < 8) {
		char name[11];
		name_of(rom, 0xCFC050, 10, m->monType[at], name);
		text(out, rom, name, 22, 4, p.yellow);
	} else if (covered == 1 && v->f[at - 8].here) {
		text(out, rom, v->f[at - 8].name, 22, 4, p.yellow);
		/* their first status, as the game's own status window shows it
		 * (C2/ADE1: 10 letters a status) */
		if (v->f[at - 8].status >= 0) {
			char st[11];
			name_of(rom, 0xC2ADE1, 10, v->f[at - 8].status, st);
			text(out, rom, st, 22, 5, p.teal);
		}
	}
	for (int i = 0; i < v->nEnemies; ++i) {
		char name[11];
		name_of(rom, 0xCFC050, 10, m->monType[v->enemy[i]], name);
		text(out, rom, name, GRID_TX(i) + 5, GRID_TY(i) + 2, p.white);
		if (m->monTarget >> v->enemy[i] & 1) hand(out, rom, (GRID_TX(i) + 2) * 8, (GRID_TY(i) + 2) * 8 - 4);
	}
	for (int s = 0; s < 4; ++s) {
		if (!v->f[s].here) continue;
		text(out, rom, v->f[s].name, ROW_TX(s) + 1, ROW_TY + 4, s == m->slot ? p.yellow : p.white);
		if (m->partyTarget >> s & 1) hand(out, rom, ROW_TX(s) * 8 + 24, (ROW_TY + 2) * 8 - 4);
	}
	text(out, rom, kBack, ROW_TX(4) + 2, ROW_TY + 4, p.white);
}

/* FF6-2: the party on the game screen, in the strip where the game's
 * battle windows were: a line each, name, HP, MP, the time gauge */
void ff6_strip_draw(const Ff6Rom* rom, const Ff6BattleView* v, uint32_t* out) {
	canvasW = FF6_STRIP_W, canvasH = FF6_STRIP_H, winUp = 0, textUp = 0;
	for (int i = 0; i < canvasW * canvasH; ++i) out[i] = 0xFF000000u;
	if (!rom) return;
	Pals p;
	battle_pals(rom, v, &p);
	add_win(0, 0, 30, 6);
	draw_wins(rom, v->style, v->winPal, out);
	for (int s = 0, row = 0; s < 4; ++s) {
		const Ff6Fighter* f = &v->f[s];
		if (!f->here) continue;
		/* (rows 12 pixels apart: tile row 1 and a shift up) */
		int y = 8 + 12 * row++, ty = (y + 7) / 8;
		textUp = ty * 8 - y;
		bool turn = v->menu.open && v->menu.slot == s;
		text(out, rom, f->name, 1, ty, turn ? p.yellow : p.white);
		number(out, rom, (unsigned) f->hp, 4, false, 8, ty, p.white);
		text(out, rom, kSlash, 12, ty, p.white);
		number(out, rom, (unsigned) f->hpMax, 4, false, 13, ty, p.white);
		if (f->mpMax > 0) {
			number(out, rom, (unsigned) f->mp, 3, false, 18, ty, p.teal);
			text(out, rom, kSlash, 21, ty, p.teal);
			number(out, rom, (unsigned) f->mpMax, 3, false, 22, ty, p.teal);
		}
		if (v->gauge) gauge(out, rom, &p, f->atb, 25, ty);
	}
	textUp = 0;
}

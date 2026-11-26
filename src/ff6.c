/* Final Fantasy VI (see ff6.h, docs/games/final-fantasy-vi.md). */
#include "ff6.h"

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

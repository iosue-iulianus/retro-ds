/* Final Fantasy VI (Final Fantasy III USA 1.0): what RetroDS reads from the
 * game (docs/games/final-fantasy-vi.md). Addresses as the SNES CPU sees
 * them; the disassembly everything8215/ff6 (GPL-3.0) is the reference for
 * them, no code from it. */
#ifndef RETRODS_FF6_H
#define RETRODS_FF6_H

#include <stdbool.h>
#include <stdint.h>

#include "emu.h"

#define FF6_CODE "A27F1C7A" /* Final Fantasy III (USA) 1.0 */

bool ff6_is(const char* gameCode);

/* Which part of the game runs: from the NMI handler its program installed
 * ($1500 holds JML, $1501-$1503 the handler) */
typedef enum {
	FF6_OTHER,  /* the title, the opening, cutscenes, the ending: anything else */
	FF6_FIELD,  /* towns, dungeons */
	FF6_WORLD,  /* the world map (its program in bank $EE) */
	FF6_BATTLE,
	FF6_MENU,   /* the main menu (X) and its screens, save and load */
} Ff6Part;
Ff6Part ff6_part(Emu* e);
const char* ff6_part_name(Ff6Part p);

/* FF6-2: the battle windows, a strip of the game's picture (its pixels):
 * rows 151 to 213 hold every battle window (the party's, the commands,
 * the Magic and Item lists...); FF6_STRIP_Y0 to Y1 take them with their
 * edges. */
#define FF6_STRIP_Y0 150
#define FF6_STRIP_Y1 214 /* (exclusive) */

/* FF6-3: the battle menu as touch needs it */
typedef struct {
	bool open;         /* a character's battle menu is up ($7BCA) */
	int menu;          /* which ($7BC2): FF6_MENU_COMMANDS, FF6_MENU_TARGET, ... */
	int slot;          /* whose: the party slot, 0 to 3 ($62CA) */
	int cursor;        /* the command list's cursor, 0 to 3 ($890F + slot) */
	int cmd[4];        /* the slot's commands (their numbers), -1 none */
	bool disabled[4];  /* greyed out: the cursor skips them */
	bool shortSet;     /* Config's Command set "short" (a cross, not a list) */
} Ff6Battle;
#define FF6_MENU_COMMANDS 0x05
#define FF6_MENU_TARGET 0x38
void ff6_battle(Emu* e, Ff6Battle* b);

/* The command list's rows on the game's picture: row r is
 * y FF6_CMD_Y0 + r * FF6_CMD_DY .. + FF6_CMD_DY, x FF6_CMD_X0 .. FF6_CMD_X1
 * (the window's inside). */
#define FF6_CMD_X0 8
#define FF6_CMD_X1 100
#define FF6_CMD_Y0 160
#define FF6_CMD_DY 12

/* FF6-3: the presses that take the command list's cursor to row `row`
 * and choose it, as a player's would: Down as many times as the game's
 * cursor needs to get there (it skips greyed commands), then A. Written to
 * keys (up to 8); the count, 0 if the row can't be chosen (none, greyed,
 * or the menu isn't the command list). */
int ff6_command_presses(const Ff6Battle* b, int row, uint32_t keys[8]);

#endif

/* ---- FF6-5: the party screen, drawn by RetroDS from the game's data and
 * art: the main menu's party window and its Time and Steps / Gp windows,
 * in the player's window style and colours, the game's portraits, font and
 * rules for what it shows. */

#define FF6_PIC_W 256
#define FF6_PIC_H 224

/* The ROM as played (a copier header taken off), for the art; NULL if it
 * can't be read. Freed with ff6_rom_free. */
typedef struct Ff6Rom Ff6Rom;
Ff6Rom* ff6_rom_load(const char* path);
void ff6_rom_free(Ff6Rom* r);

/* One character in the party, as the main menu shows it */
typedef struct {
	bool here;          /* a character in this slot */
	int id;             /* 0 to 15: the save's character */
	bool backRow;
	char name[7];       /* in the game's own text codes (FF6 encoding), 0-ended */
	int level, hp, hpMax, mp, mpMax;
	bool showMp;        /* the game shows MP (CheckMPVisible) */
	int portrait;       /* the portrait it uses */
} Ff6Member;

typedef struct {
	Ff6Member m[4];     /* by battle order */
	int style;          /* Config's Window, 0 to 7 */
	uint16_t winPal[7]; /* its colours 1 to 7 (BGR555) */
	uint16_t fontColour;
	int hours, minutes;
	uint32_t steps, gp;
} Ff6Party;

/* What the party screen is drawn from, read between frames: the field's
 * own copy of the party's flags while it runs ($0867 + 41 x n), else the
 * save's ($1850). */
void ff6_party_read(Emu* e, Ff6Party* p);

/* The party screen as the game's main menu draws it (its command list
 * left out), 256 x 224, ABGR8888 (0xAABBGGRR). */
void ff6_party_draw(const Ff6Rom* rom, const Ff6Party* p, uint32_t* out);

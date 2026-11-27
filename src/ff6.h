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
	/* choosing a target: who the game's cursor covers */
	int partyTarget;   /* bits 0-3, the party's slots ($7B7D) */
	int monTarget;     /* bits 0-5, the monsters ($7B7E) */
	/* the monsters */
	int monAlive;      /* bits 0-5 (here, $3AA8 + 2n bit 0; not dead, $3EEC + 2n bit 7) */
	int monType[6];    /* monster numbers ($2001), -1 none */
	int monX[6], monY[6]; /* places on the battlefield ($80C3, $80CF) */
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

/* FF6-3: moving the game's target cursor to a chosen target with the
 * D-pad, a press at a time, looking where each press took it. want: a
 * party slot 0 to 3, or 4 + a monster slot (0 to 5). Start with
 * ff6_aim_start, then ff6_aim_next after every press has played: the next
 * key (a direction, then K_A on the target), 0 when done or stuck (12
 * presses, or no way further). */
typedef struct {
	int want;          /* -1 none */
	int steps;
	int blocked;       /* directions that didn't move it from here: bits L R U D */
	int lastMask;      /* where the cursor was before the last press */
	uint32_t lastKey;
} Ff6Aim;
void ff6_aim_start(Ff6Aim* a, int want);
uint32_t ff6_aim_next(Ff6Aim* a, const Ff6Battle* b);

/* FF6-3: the target for a tap on an enemy's name (monsterId: its monster
 * number): 4 + the monster slot the cursor is on if it's one of those,
 * else the nearest alive one; -1 none. */
int ff6_group_target(const Ff6Battle* b, int monsterId);

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

/* ---- FF6-2: the battle screen, drawn by RetroDS (replacing the moved
 * windows): the party with portraits, HP, MP and time gauges, the active
 * character's commands, the enemies, in the game's windows and font,
 * laid out for a 4:3 second screen */

#define FF6_BATTLE_W 320
#define FF6_BATTLE_H 240

typedef struct {
	bool here;
	char name[7];
	int portrait;
	int hp, hpMax, mp, mpMax;
	int atb;           /* the time gauge, 0 to 255 */
} Ff6Fighter;

typedef struct {
	bool ready;        /* the battle's copies are made (not as it starts) */
	Ff6Fighter f[4];
	Ff6Battle menu;    /* whose turn, the commands, the cursor */
	int enemy[6];      /* the enemies alive, top to bottom on the battlefield: their monster slots */
	int nEnemies;
	bool gauge;        /* Config's time gauge on */
	int style;
	uint16_t winPal[7];
	uint16_t fontColour;
} Ff6BattleView;

void ff6_battle_view(Emu* e, Ff6BattleView* v);
void ff6_battle_draw(const Ff6Rom* rom, const Ff6BattleView* v, uint32_t* out);
/* which page it shows: waiting (the party two by two, the enemies),
 * someone's commands (their status, a button each), choosing a target
 * (the enemies' and the party's buttons, Back) */
enum { FF6_PAGE_WAIT, FF6_PAGE_COMMANDS, FF6_PAGE_TARGET };
int ff6_battle_page(const Ff6BattleView* v);
/* what's at x, y on it (its pixels): a command (arg: its row), an enemy
 * (arg: its monster slot), a character (arg: the party slot), Back, the
 * status window of whose turn it is (on the command page) */
enum { FF6_HIT_NONE, FF6_HIT_COMMAND, FF6_HIT_ENEMY, FF6_HIT_PARTY, FF6_HIT_BACK, FF6_HIT_STATUS };
int ff6_battle_hit(const Ff6BattleView* v, int x, int y, int* arg);

/* FF6-2: the party on the game screen, where the game's battle windows
 * were (its rows FF6_STRIP_Y0 to Y1): name, HP, MP, the time gauge */
#define FF6_STRIP_W 256
#define FF6_STRIP_H 64
void ff6_strip_draw(const Ff6Rom* rom, const Ff6BattleView* v, uint32_t* out);



/* Final Fantasy VI (Final Fantasy III USA 1.0): what RetroDS reads from the
 * game (docs/games/final-fantasy-vi.md). Addresses as the SNES CPU sees
 * them; the disassembly everything8215/ff6 (GPL-3.0) is the reference for
 * them, no code from it. */
#ifndef RETRODS_FF6_H
#define RETRODS_FF6_H

#include <stdbool.h>
#include <stdint.h>

#include "emu.h"
#include "writes.h"

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
	bool control;      /* Relm controlling a monster: cmd[] are its attacks */
	/* choosing a target: who the game's cursor covers */
	int partyTarget;   /* bits 0-3, the party's slots ($7B7D) */
	int monTarget;     /* bits 0-5, the monsters ($7B7E) */
	/* the monsters */
	int monAlive;      /* bits 0-5 (here, $3AA8 + 2n bit 0; not dead, $3EEC + 2n bit 7) */
	int monType[6];    /* monster numbers ($2001), -1 none */
	int monX[6], monY[6]; /* places on the battlefield ($80C3, $80CF) */
	/* targeting (the game's checks, cursor.asm's target select) */
	int targetFlags;   /* the action's ($7A84: TARGET in the disassembly's const.inc) */
	int validParty;    /* $201D & $61AC & $61AD & $6193 */
	int validMons;     /* $0092 */
	int attack;        /* the spell or item chosen ($7A85) */
	int actionCmd;     /* the command of the action being chosen (the queue $2BAE, 8 bytes an action: the one with no character yet), -1 */
	/* an item picked up in the Item list to be moved ($7B02), from where
	 * ($7B03: its row x 5) */
	bool itemHeld;
	int itemHeldAt;
} Ff6Battle;
#define FF6_MENU_COMMANDS 0x05
#define FF6_MENU_TARGET 0x38
#define FF6_MENU_MAGIC 0x0E
#define FF6_MENU_ITEM 0x0A
#define FF6_MENU_LORE 0x1B
#define FF6_MENU_RAGE 0x1E
#define FF6_MENU_DANCE 0x21
#define FF6_MENU_MAGITEK 0x2A
#define FF6_MENU_THROW 0x2D
#define FF6_MENU_TOOLS 0x30
#define FF6_MENU_BLITZ 0x3D   /* (a button code, then A) */
#define FF6_MENU_BUSHIDO 0x37 /* (SwdTech: A when the bar is at the level) */
#define FF6_MENU_SLOT 0x08    /* (A stops the reels) */
#define FF6_MENU_ROW 0x24     /* (Row's window: A changes rows) */
#define FF6_MENU_DEF 0x27     /* (Def.'s: A defends) */
/* the lists RetroDS draws (one of the FF6_MENU_ above) */
bool ff6_list_menu(int menu);
/* the command page's (the command list, Row's and Def.'s windows) */
bool ff6_command_menu(int menu);
void ff6_battle(Emu* e, Ff6Battle* b);

/* FF6-3: touch done as the game's own menu does it: the menu's cursor
 * written where the tap was (writes.h: the cursor bytes the D-pad moves,
 * ff6_write_ranges), then A pressed, the game choosing as from its pad.
 * Each makes the writes into out (up to 4) and returns their count; -1
 * when the game wouldn't take it (a greyed command, a target its
 * targeting rules don't allow, an empty list row). */
const WriteRange* ff6_write_ranges(int* n);
/* a command: row 0 to 3 of the command list */
int ff6_command_writes(const Ff6Battle* b, int row, Write* out);
/* a target: a party slot 0 to 3, or 4 + a monster slot. 0 writes: the
 * game's cursor already covers it (a spell on all), or it can't be moved
 * (A takes the game's own choice) */
int ff6_target_writes(const Ff6Battle* b, int want, Write* out);
/* a row of an open list: its place in the game's list (FF6_ESPER: the
 * Magic list's esper, which Up on its top row opens) */
#define FF6_ESPER 255
int ff6_list_writes(const Ff6Battle* b, int index, Write* out);
/* the keys that choose that row once written (up to 3; 0: a pause, 12
 * frames): A; Item A twice (the game's first A picks an item up to move
 * it, a second on it uses it), after B if another item is held (else
 * they'd swap); the esper Up, a pause (its window opening), A */
int ff6_list_keys(const Ff6Battle* b, int index, uint32_t keys[3]);
/* FF6-3: a Blitz (0 to 7): its code's buttons from the ROM, A last (up to
 * 11), pressed one at a time as the player would; 0 if none */
typedef struct Ff6Rom Ff6Rom;
int ff6_blitz_keys(const Ff6Rom* rom, int k, uint32_t keys[11]);

#endif

/* ---- FF6-5: the party screen, drawn by RetroDS from the game's data and
 * art: the main menu's party window and its Time and Steps / Gp windows,
 * in the player's window style and colours, the game's portraits, font and
 * rules for what it shows. */

#define FF6_PIC_W 256
#define FF6_PIC_H 224

/* The ROM as played (a copier header taken off), for the art; NULL if it
 * can't be read. Freed with ff6_rom_free. */
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
	int status;        /* the first status the game's status window would name (0 Wound, 1 Petrify...), -1 none */
} Ff6Fighter;

typedef struct {
	bool ready;        /* the battle's copies are made (not as it starts) */
	Ff6Fighter f[4];
	Ff6Battle menu;
	/* an open list (Magic, Item, Lore, Rage, Dance, Magitek, Throw,
	 * Tools): its rows that hold something, in the game's order (their
	 * places in its list; the Magic list's esper first, FF6_ESPER), the
	 * game's cursor's row (-1: on none of them), the first row shown (the
	 * play session's page, FF6_LIST_PAGE rows a page) */
	int nList;
	uint8_t listAt[256], listId[256], listVal[256]; /* place, what (spell, item...), MP cost or how many */
	bool listOff[256];  /* greyed: too little MP, not for battle */
	bool listShowVal;   /* (MP or how many shown) */
	/* Blitz: the ones known (bits); SwdTech: how many known, the bar (32 a
	 * level); the one tapped, waiting to be done (-1; the play session's) */
	int blitzKnown, bushidoKnown, bushidoBar, blitzWant, bushidoWant;
	int listCursor, listTop;    /* whose turn, the commands, the cursor */
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
enum { FF6_PAGE_WAIT, FF6_PAGE_COMMANDS, FF6_PAGE_TARGET, FF6_PAGE_LIST, FF6_PAGE_BLITZ, FF6_PAGE_BUSHIDO };
#define FF6_LIST_PAGE 10
int ff6_battle_page(const Ff6BattleView* v);
/* what's at x, y on it (its pixels): a command (arg: its row), an enemy
 * (arg: its monster slot), a character (arg: the party slot), Back, the
 * status window of whose turn it is (on the command page) */
enum { FF6_HIT_NONE, FF6_HIT_COMMAND, FF6_HIT_ENEMY, FF6_HIT_PARTY, FF6_HIT_BACK, FF6_HIT_STATUS, FF6_HIT_ROW, FF6_HIT_UP, FF6_HIT_DOWN, FF6_HIT_ROWCMD, FF6_HIT_DEF };
/* (FF6_HIT_ROW: a row of the list, arg its index in the view's list) */
int ff6_battle_hit(const Ff6BattleView* v, int x, int y, int* arg);

/* FF6-2: the party on the game screen, where the game's battle windows
 * were (its rows FF6_STRIP_Y0 to Y1): name, HP, MP, the time gauge */
#define FF6_STRIP_W 256
#define FF6_STRIP_H 64
void ff6_strip_draw(const Ff6Rom* rom, const Ff6BattleView* v, uint32_t* out);



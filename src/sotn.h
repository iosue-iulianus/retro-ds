/* Castlevania: Symphony of the Night (USA, SLUS-00067): what RetroDS reads
 * from the game (docs/games/symphony-of-the-night.md). Its addresses are
 * the PlayStation's (0x80000000 up), its pictures come from the game's
 * video RAM (SwanStation with RetroDS's patch: 1024 x 512 halfwords). */
#ifndef RETRODS_SOTN_H
#define RETRODS_SOTN_H

#include <stdbool.h>
#include <stdint.h>

#include "emu.h"

#define SOTN_CODE "SLUS-00067"

bool sotn_is(const char* gameCode);

/* SOTN-1: the title's logo, from the disc (the file the title loads it
 * from, checked against the logo measured): RGBA, its clear pixels
 * transparent, at its shape on a television (1150 x 484). malloc'd; NULL
 * if it isn't there. */
uint8_t* sotn_disc_logo(const char* cuePath, int* w, int* h);

/* ---- SOTN-2 to 4: the castle map on the second screen */

#define SOTN_PIC_W 256 /* the game's map screen (Select), in its pixels */
#define SOTN_PIC_H 240
#define SOTN_MAP_W 242 /* the castle's part of it */
#define SOTN_MAP_H 190

/* What the second screen is drawn from, read between frames. */
typedef struct {
	int state;         /* the game's: 1 title, 2 playing, 8 file screens ... */
	int rooms;         /* rooms explored */
	bool inverted;     /* the inverted castle */
	int window[3];     /* the player's window colour (System), 0 to 31 each */
	int sqX, sqY;      /* Alucard's square on the map */
	int roomX, roomY;  /* his room's top left square */
	/* the map as Select shows it: palette indices, 0 clear */
	uint8_t pic[SOTN_PIC_H][SOTN_PIC_W];
	uint32_t mapPal[16]; /* RGBA (0xAABBGGRR) */
	/* the status screen's 8 x 8 font (16 x 16 characters from ' ') */
	uint8_t font[128][128];
	uint32_t fontPal[16];
	/* the map cursor's picture */
	uint8_t cursor[32][32];
	uint32_t cursorPal[16];
} SotnView;

/* Reads it; false without the game's video RAM. */
bool sotn_read(Emu* e, SotnView* v);
/* The map's to be shown (else the logo, SOTN-1): Alucard is playing in
 * the castle (the prologue has no map). */
bool sotn_has_map(const SotnView* v);
/* A checksum of everything the composed picture depends on. */
uint32_t sotn_view_crc(const SotnView* v);

/* Where things go on the second screen, in the game's pixels (SOTN-4) */
typedef struct {
	int winX, winY, winW, winH;   /* the big window's inside (the map's clip) */
	int mapX, mapY;               /* Select's picture's origin, the map at rest */
	int castleX, castleY;         /* the castle's part of Select's picture */
	int paneX, paneY, paneW, paneH; /* the ROOMS pane */
	float logoX, logoY, logoW, logoH; /* the logo's place */
} SotnLayout;
#define SOTN_LOGO_ASPECT 2.376f /* the title logo's width / height, square pixels */

/* SOTN-4: the big window (cw x ch, RGBA), and where everything goes. */
void sotn_compose(const SotnView* v, uint32_t* px, int cw, int ch, SotnLayout* out);
/* SOTN-2: the map as Select shows it, SOTN_PIC_W x SOTN_PIC_H RGBA, clear
 * where there's no map */
void sotn_map_pixels(const SotnView* v, uint32_t* px);
/* SOTN-4: the ROOMS pane, l->paneW x l->paneH RGBA */
void sotn_pane_pixels(const SotnView* v, const SotnLayout* l, uint32_t* px);
/* The map cursor's picture (32 x 32 RGBA) */
void sotn_cursor_pixels(const SotnView* v, uint32_t px[32 * 32]);
/* SOTN-3: the cursor's square in Select's picture at animation frame t
 * (0 to 63, as the game's: it closes onto Alucard's square) */
void sotn_cursor_rect(const SotnView* v, int t, int* x, int* y, int* size);

/* ---- SOTN-6, 7: the menu on the second screen */
#define SOTN_ENGINE_PLAYING 1
#define SOTN_ENGINE_MENU 2
#define SOTN_MENU_TOP 0x10  /* the menu open on its first page (MENU_STEP_OPENED) */
#define SOTN_MENU_EXIT 3    /* from here up to SOTN_MENU_TOP: the menu closing */
int sotn_engine(Emu* e);      /* the game's engine step */
int sotn_menu_step(Emu* e);   /* its menu's step (SOTN_MENU_TOP at the menu's top) */
/* Start would open the game's menu now */
bool sotn_menu_openable(Emu* e);
/* SOTN-7: the game's picture fb (w x h, RGBA) widened to ow x h (ow > w):
 * its window in the middle, carried out to the edges (its white edge at
 * the widened picture's first and last columns). False
 * (out untouched) unless a window fills the picture from edge to edge. */
bool sotn_widen(const uint32_t* fb, int w, int h, uint32_t* out, int ow, int* ox);

#endif

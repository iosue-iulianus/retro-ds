/* The second screens RetroDS draws itself, split out of the play session
 * (game.c): Super Metroid's (game_sm.c) and A Link to the Past's
 * (game_alttp.c). What they and game.c share; internal to the three. */
#ifndef GAME_SCREENS_H
#define GAME_SCREENS_H

#include "alttp.h"
#include "config.h"
#include "emu.h"
#include "paths.h"
#include "profiles.h"
#include "smmap.h"
#include "ui.h"
#include <SDL.h>
#include <stdbool.h>
#include <stdint.h>

/* ---- game.c's, used by the second screens */
extern Config* cfg;
extern Emu emu;
extern bool debug;           /* RETRODS_DEBUG */
extern uint64_t frameCount;
extern GamePaths gp;
extern bool hudHide;         /* the profile's hide rectangles are covered now (SM-6) */
extern const char* romPath;  /* the game's ROM */
extern int picW, picH;       /* the game's picture as shown (256 x 224 or 239) */
#define SHADER_ON (cfg->shader != SH_OFF)
int game_screen(void);       /* the screen the game is on */

/* save states' RetroDS block (game.c) */
typedef struct { uint8_t* p; size_t n, cap; } Blob;
void blob_add(Blob* b, const void* d, size_t n);
void blob_u32(Blob* b, uint32_t v);
void blob_section(Blob* b, const char* tag, const void* d, size_t n);
uint32_t rd32(const uint8_t* p);

/* The second screen's GPU work in the last second, for the slow frames'
 * line (A Link to the Past's map: what it sent to the GPU) */
typedef struct {
	bool on;
	int uploads, made, dropped, dungeon, mostDrawn, drawn;
} MapWork;
extern MapWork mapWork;
void map_work_frame(void); /* a redraw of the map begins */

/* ---- Super Metroid (game_sm.c; src/smmap.c, SM-7 to SM-10) */
extern SmMap* nat;
extern const MapProfile* natProf;
extern bool natShown; /* the second screen shows it (else the logo, SM-4) */
void native_open(const char* rom, const MapProfile* p);
void native_free(void);
SDL_Texture* native_texture(int v, int w, int h, SDL_BlendMode blend);
void native_textures(void);
void native_tick(void);
void native_step(void);
void draw_native(int s);
int native_touch(const Pointer* p);

/* ---- A Link to the Past (game_alttp.c; src/alttp.c, ALTTP-1 and on) */
extern Alttp* alt;
extern bool altShown; /* the second screen shows it (else the logo, ALTTP-9) */
extern int altPage;   /* 0 the items screen, 1 the map (ALTTP-1) */
bool alt_no_map(void);     /* no map here: houses, caves, special areas (the logo) */
void draw_logo_at(int s, int dy); /* the game's logo on the second screen, dy down (game.c) */
void alt_open(const char* rom);
void alt_free(void);
void alt_textures(void);
void alt_tick(void);
void alt_step(void);
void alt_sword_load(void);
void alt_state_block(Blob* b);
void alt_state_restore(const uint8_t* d, uint32_t n);
void draw_alt(int s);
void draw_alt_title(int s);
int alt_touch(const Pointer* p);
uint32_t alt_keys(uint32_t keys); /* the game's keys, X taken for the map */

/* ---- Symphony of the Night (game_sotn.c; src/sotn.c, SOTN-2 to 4) */
extern bool sot;      /* the game is Symphony of the Night */
void sot_open(const char* rom);
void sot_free(void);
void sot_textures(void);
void sot_tick(void);
bool sot_map_shown(void); /* the map is up (else the logo, SOTN-1) */
void draw_sot(int s);
bool sot_screen_shown(void); /* SotN's map or menu page (else the logo) */
uint32_t sot_keys(uint32_t keys); /* SOTN-6: Start taken, the presses for the game */
bool sot_frozen(void);       /* SOTN-6: the game stopped for the menu page */
bool sot_cover_game(void);   /* SOTN-6: the game screen shows the logo */
int sot_touch(const Pointer* p); /* SOTN-5: tap and pinch zoom, drag */
void draw_logo_in(int s, SDL_Rect area); /* the game's logo filling area (game.c) */

#endif

/* Shadow-instance map capture.
 *
 * A second, silent emulator core is loaded with a snapshot of the live game,
 * fed the button presses that open the in-game map, and run until the map
 * screen is up and stable. The map area is cropped out and handed back.
 * The live game is never paused or touched.
 */
#ifndef RETRODS_MAPCAP_H
#define RETRODS_MAPCAP_H

#include "emu.h"

typedef struct MapCapture MapCapture;

#define MAP_MAX_FRAMES 16

typedef struct MapResult {
	bool ok;
	const char* why; /* failure reason when !ok */
	int w, h;
	int nFrames;                 /* >1 when the map has an animated marker */
	int frameStep;               /* emulated frames between animation frames */
	uint32_t* pixels;            /* == frames[0] */
	uint32_t* frames[MAP_MAX_FRAMES];
	uint32_t serial;             /* increments with each successful capture */
	/* the view it shows (SM-7): page, and the scroll it got (the game
	 * stops at its map's edges) for the scroll asked for */
	int page, dx, dy, askedDx, askedDy;
	/* the whole map (profiles with touch.bgW, the map's page): bigW x
	 * bigH, and where in it the frame's inside starts as captured (the
	 * game's own view, centred on the player) */
	bool bigOk;
	uint32_t* big;
	int bigW, bigH, bigX, bigY;
} MapResult;

/* Returns NULL when no profile matches gameCode. */
/* tight: use the profile's bezel-free crop and prompt masks when it has one. */
MapCapture* mapcap_create(const char* romPath, const char* gameCode, bool tight);
void mapcap_destroy(MapCapture* mc);
const char* mapcap_title(MapCapture* mc);

/* Synchronous capture (used by tools). Result valid until next call. */
MapResult mapcap_capture_sync(MapCapture* mc, Emu* live);

/* Asynchronous API for the frontend. request() snapshots the live core if
 * the worker is idle and returns true if a capture was started. poll() copies
 * the newest successful result into *out (caller-owned buffers are managed
 * internally) and returns true if it is newer than lastSerial. */
/* (taken just before the game's next frame, which runs with nextKeys:
 * emu_state_save_live, so the snapshot leaves the game where it was) */
bool mapcap_request(MapCapture* mc, Emu* live, uint32_t nextKeys);
bool mapcap_busy(MapCapture* mc);
/* Calls off the capture running, if any (the game moved on: a newer one's
 * wanted): it stops at once and publishes nothing; mapcap_busy is false
 * again as soon as it has. */
void mapcap_cancel(MapCapture* mc);
/* The view later captures show (profiles with touch, SM-7): page 0 the
 * map, 1 the second page; dx, dy the map scrolled from where the game
 * centres it (SNES pixels, + right / down). */
void mapcap_set_view(MapCapture* mc, int page, int dx, int dy);
/* "Map Animations: Off": later captures record the profile's whole marker loop. */
void mapcap_set_still(MapCapture* mc, bool still);
const struct MapProfile* mapcap_profile(MapCapture* mc);
bool mapcap_poll(MapCapture* mc, uint32_t lastSerial, MapResult* out);
/* Consecutive failed captures since the last success (and the last reason). */
int mapcap_failures(MapCapture* mc, const char** why);

/* Cheap check on the live core: is this a moment where capturing makes sense?
 * (in gameplay, not in a menu). Profiles without a check return true. */
bool mapcap_live_ok(MapCapture* mc, Emu* live);
/* The game is back on its title / file menus (profiles without a check: never). */
bool mapcap_in_menu(MapCapture* mc, Emu* live);

/* Returns a value that changes when the player moves to a new map cell
 * (or 0 if the profile cannot tell). Used to trigger captures early. */
uint32_t mapcap_room_key(MapCapture* mc, Emu* live);

/* Size of the map image this profile produces. */
void mapcap_size(MapCapture* mc, int* w, int* h);

#endif

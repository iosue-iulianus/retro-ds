/* Per-game knowledge: how to open the map and how to recognise it. */
#ifndef SNESDS_PROFILES_H
#define SNESDS_PROFILES_H

#include "emu.h"

/* Behind the logo on the second screen, and around what a game shows
 * there that doesn't fill it (GEN-1): dark grey, so the game screen's
 * picture shows against the black around it (ABGR8888) */
#define SCREEN_GREY 0xFF303030u
/* behind something live on the second screen: black, the idle logo's dark
 * grey being for idle (GEN-1) */
#define SCREEN_LIVE 0xFF000000u

typedef struct {
	uint32_t keys;
	int frames;
} MacroStep;

/* A screen pixel that must have a given colour while the map is up. */
typedef struct {
	int x, y;
	uint32_t rgb; /* 0xRRGGBB */
} Probe;

/* A memory location that must hold a given value while the map is up. */
typedef struct {
	uint32_t addr; /* 0 terminates the list */
	uint32_t mask;
	uint32_t value;
	int size; /* 1, 2 or 4 bytes */
	bool hasAlt; /* also accept `alt` (A Link to the Past: overworld OR dungeon) */
	uint32_t alt;
	bool range; /* with hasAlt: anything from value to alt */
} MemProbe;

typedef struct MapProfile {
	const char* codes[4]; /* the releases this profile handles: game codes (emu.h gameCode) */
	const char* title;
	const char* listName; /* name in the game list instead of the file name */

	MacroStep open[8]; /* button sequence that brings up the map */
	/* Another sequence used instead when every openAltIf probe matches as
	 * the capture starts (a screen where the usual button does something
	 * else first) */
	MemProbe openAltIf[2];
	MacroStep openAlt[8];
	/* Optional second step: once the map is up (mem probes), if open2If
	 * matches, wait open2Delay frames and press open2 (a map that zooms out
	 * with a second press). */
	MemProbe open2If[2];
	int open2Delay;
	MacroStep open2[4];
	int settleMax;     /* frames to wait after the macro for the map to settle */
	int openWithin;    /* give up if the memory probes don't match within this many frames (0 = settleMax) */
	int openTries;     /* with openWithin: press `open` again this many times in all before giving
	                    * up (0, 1: once): a game that ignores it for a while */
	/* Dim the last map while the map can't be brought up (a game whose map
	 * button does nothing in some places). Other games keep the last map at
	 * full brightness: their captures also fail briefly during room
	 * transitions. */
	bool dimWhenUnavailable;
	/* No map yet: the game is listed and the second screen shows its logo,
	 * but nothing is captured. */
	bool logoOnly;
	/* Logo-only games: screens worth copying to the second screen. When
	 * `show` matches (at least showMin probes, for `stable` frames) the game
	 * screen is copied there and stays through play, pauses and loading; it
	 * goes back to the logo when one of the `revert` screens (menus)
	 * matches. Probe colours within `tol` per channel. */
	struct {
		Probe show[8];
		int showMin;
		Probe revert[2][8];
		int revertMin[2];
		int stable, tol;
	} snap;
	/* frames between captures instead of map_interval when fewer (0: the
	 * setting's) */
	int interval;
	int stableFrames;  /* consecutive "quiet" frames required */
	int maxDiff;       /* pixels allowed to change between quiet frames (blinking markers) */
	int crop_x, crop_y, crop_w, crop_h;

	/* Optional tighter crop that drops the screen's frame and title bar
	 * (used when map_crop is on), and rectangles (screen coords) to cover
	 * with empty map-grid tiles, e.g. button prompts drawn over the map.
	 * gridX/gridY/gridCell give the phase and size of the background grid. */
	int tight_x, tight_y, tight_w, tight_h;
	struct { int x, y, w, h; } masks[8];
	int gridX, gridY, gridCell;
	/* Lettering to erase (when map_crop is on): inside the rect (screen
	 * coords), pixels of exactly these n colours (0xRRGGBB, n = 0 means 1; n < 0
	 * paints the whole rect in rgb[0], for text that animates) are
	 * filled in from the pixels around them (or, with flat, all set to the most
	 * common other colour in the rect), which leaves the button or plate they
	 * sit on untouched. With grid, every pixel in the rect is rebuilt from the
	 * same spot 1-3 grid cells (gridCell) above and to the sides, outside any
	 * grid rect: for prompts that also clear the map grid behind them. */
	struct { int x, y, w, h; int n; uint32_t rgb[8]; bool flat, grid; } glyphs[10];
	/* Recolouring (when map_crop is on, after glyphs): inside the rect, each
	 * colour from[i] becomes to[i] (0xRRGGBB, n pairs). */
	struct { int x, y, w, h; int n; uint32_t from[16], to[16]; } recolor[6];

	Probe probes[8];
	int minProbes;    /* how many probes must match (0 = all) */
	int tolerance;    /* per-channel colour tolerance */
	uint32_t bgColor; /* map background; >= minForeground pixels must differ from it */
	int minForeground;

	MemProbe mem[4]; /* checked on the shadow core once the map should be up */
	MemProbe live[4]; /* checked on the live core before snapshotting (gameplay) */
	/* Back on the title / file menus (all must match): forget the map and
	 * show the logo again until the next capture. */
	MemProbe menu[2];
	/* While every one of these matches, the second screen shows the logo
	 * (as with no map yet), its map picture kept for when they stop
	 * matching */
	MemProbe logoIf[2];
	MemProbe logoIfOr[2]; /* or while every one of these matches */

	/* Parts of the game screen covered in black while every hideIf probe
	 * matches (screen coords): a HUD piece the second screen makes
	 * pointless, as Super Metroid's mini-map (SM-6). Only what's shown
	 * changes: the game, its picture and screenshots are as the game
	 * draws them. Only while the second screen shows the map. */
	struct { int x, y, w, h; } hide[2];
	MemProbe hideIf[3];

	/* Touch on the second screen (SM-7): a drag scrolls the map, a tap on
	 * a plate switches to a second page; both done on the hidden copy with
	 * the game's own buttons, after the map is up. Off while scrollX is 0. */
	struct {
		uint32_t scrollX, scrollY; /* the hidden copy's view position (2 bytes each) */
		MacroStep page2[2];        /* from the map to the second page */
		MemProbe page2Mem[2];      /* the second page is up (with mem[0]) */
		Probe page2Probes[8];      /* the second page's own fixed pixels (all must match) */
		/* a tap in here switches: [0] on the map, [1] on the second page
		 * (screen coords) */
		struct { int x, y, w, h; } tap[2];
		/* live game: values that change as the player moves; a change puts
		 * the second screen back on the map, centred (2 bytes each) */
		uint32_t moved[2];
		/* The whole map at once, so a drag only moves the picture: after
		 * the map is up, the hidden copy's view (scrollX, scrollY) is set
		 * in turn to cover its bgW x bgH map layer, and each view's
		 * `centre` (screen coords, clear of the scroll arrows) is kept;
		 * the frame's inside, `inner` (screen coords), shows a window of
		 * it. Off while bgW is 0 (scrolled captures instead). */
		int bgW, bgH;
		struct { int x, y, w, h; } centre, inner;
	} touch;
	/* Painted over every capture (screen coords): with `filled`, the rect
	 * in `fill` (0xRRGGBB), then `rows` from x, y: '#' in `ink`, '.' in
	 * `fill`, anything else left as it is; or, with fromDx / fromDy, the
	 * rect copied from that far away (to cover a line with the grid or
	 * frame beside it). `page`: 0 on every page, 1 on the map only, 2 on
	 * the second page only (touch). */
	struct { int x, y, w, h; bool filled; uint32_t fill, ink; const char* rows[8]; int page; int fromDx, fromDy; } paint[10];

	/* Values that change when the player enters a new room; a change
	 * triggers an early capture. Only addr/size are used. */
	MemProbe room[4];

	/* The title screen, for the game's logo in the game list and on the
	 * second screen: from power-on, wait, optionally press Start (to skip an
	 * intro) and wait again, or play `steps` instead; then the picture
	 * inside the box (maxX, maxY 0: to the edge) is the logo. */
	struct {
		int wait, wait2;
		bool pressStart;
		MacroStep steps[24];
		int minX, minY, maxX, maxY;
		int rev; /* bump when the logo changes, so cached logos are made again */
	} logo;

	int animFrames; /* frames to record for the blinking/pulsing marker loop */
	/* Shown still even with Map Animations on (the still frame, stillPick) */
	bool mapStill;
	int animStep;   /* emulated frames between recorded frames */
	/* "Map Animations: Off": a marker loop longer than animFrames * animStep
	 * is recorded over this many frames instead, so the still frame can
	 * always come from the same point of the loop. */
	int animCycle;
	/* How the still frame is chosen for each animated spot: the brightest
	 * state (default), or the state that covers the most pixels (ties: the
	 * most contrast) for markers whose brightest state isn't the marker. */
	/* (STILL_FIRST: the first frame, for frames that are pages) */
	/* (STILL_RED: a spot with red in it at its reddest, others at their
	 * brightest: a marker that fades red to white) */
	enum { STILL_BRIGHT, STILL_COVER, STILL_FIRST, STILL_RED } stillPick;
	/* Instead of opening a map: the profile builds the second screen's
	 * picture itself (crop_w x crop_h, into out) on the shadow core from
	 * the live game's state; NULL on success, else why not. */
	const char* (*compose)(Emu* s, const void* state, uint32_t* out, int w, int h);
	/* Instead of compose: several pictures (pages shown in turn, `*step`
	 * emulated frames each); out[0..*n) on return, *n the most on the way in */
	const char* (*composePages)(Emu* s, const void* state, uint32_t* const* out, int* n, int* step, int w, int h);
} MapProfile;

const MapProfile* profile_find(const char* gameCode);
/* What a ROM is, by its CRC32 (emu.romCrc) against every known dump of its
 * release (src/dumps_data.h): */
typedef enum {
	DUMP_UNLISTED, /* a release with no list of dumps (no profile): not checked */
	DUMP_TESTED, /* the dump the regression tests run on */
	DUMP_OFFICIAL, /* another official dump (a revision, a reissue...), untested */
	DUMP_MOD_TESTED, /* a mod the tests run on */
	DUMP_MOD, /* another known mod, untested */
	DUMP_MODIFIED, /* none of them: patched, hacked, trimmed or a bad dump */
} DumpKind;
/* name: No-Intro's name of the dump, or the mod's (NULL when not known) */
DumpKind profile_dump(const char* gameCode, uint32_t crc, const char** name);
/* The release's No-Intro name, without a revision's or reissue's mark;
 * false if it has no list of dumps */
bool profile_release_name(const char* gameCode, char* out, size_t size);
/* A patch SNES DS knows (src/dumps_data.h), by its file's checksum: the
 * CRC32 of the file without its last four bytes, and its size */
typedef struct {
	char code[9];      /* the release it's for (its game code; "" if not known) */
	uint32_t id;
	uint32_t size;
	uint32_t base;     /* the ROM it's made for (0: any of the release's, or not known) */
	uint32_t result;   /* the ROM it makes from that (0: not known) */
	const char* kind;  /* "colour", "voice removal", "translation"... */
	const char* name;
	const char* baseName; /* the base's No-Intro name, or "the game with <patch>" */
	bool tested;       /* the game's cases pass on the result */
} KnownPatch;
/* the n-th entry for a patch file (one per ROM it's made for), or NULL */
const KnownPatch* profile_patch(const uint8_t* file, size_t size, int n);
bool memprobes_match(const MemProbe* list, int n, Emu* e);

#endif

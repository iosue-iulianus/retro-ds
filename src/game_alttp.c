/* A Link to the Past's second screen in a play session (game.c): the
 * items screen, the world map and the dungeon maps drawn by RetroDS
 * (src/alttp.c, src/alttp_dungeon.c), zoomed, dragged and tapped. Moved
 * out of game.c as it was (Nov 24, 2026). */
#include "game_screens.h"
#include "android.h"
#include "i18n.h"
#include "logo.h"
#include "ui.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

/* ---- A Link to the Past's second screen, made by RetroDS (src/alttp.c;
 * docs/games/a-link-to-the-past.md, ALTTP-1 and on): the items screen,
 * or the world map (X switches); taps select items, a drag moves the map
 * and a tap on it changes its zoom. Nothing is captured on a hidden copy. */
Alttp* alt;
bool altShown;   /* the second screen shows it (else the logo, ALTTP-9) */
int altPage;     /* 0 the items screen, 1 the map (ALTTP-1) */
/* ALTTP-3: the map's zooms, world pixels a pixel of the screen (each tap
 * the next, in; from the closest, the farthest; a pinch goes between
 * them and settles on the nearest), the one it's at or going to, and the
 * zoom shown now (gliding to it) */
static const float altZooms[] = { 0.5f, 1, 2, 4 };
#define ALT_ZOOMS ((int) (sizeof altZooms / sizeof altZooms[0]))
static int altZoomI = 2;
static float altZ = 2;
#define ALT_Z altZ
static bool altPinch; /* two fingers on the map */
static bool altFocus; static float altFocusX, altFocusY; /* ALTTP-3: a tap's place, gliding to the middle */
static float altPinchSpan, altPinchZ, altPinchWX, altPinchWY; /* at its start: the span, zoom, the world point under the fingers */
static bool altXDown;
static bool altItemsDirty;
static SDL_Texture *altItemsTex[2], *altMarkerTex[2], *altSkyTex[2][2], *altGoalTex[2], *altTriTex[2];
#define ALT_SKY_TEXEL 2 /* world pixels to one of the clouds' (ALTTP-2) */
#define ALT_TRI_W 16 /* the zoom indicator's Triforce */
#define ALT_TRI_H 14
static const uint32_t* alt_triforce(void);
static SDL_Texture* altZoomTex[2];
/* ALTTP-14: the title's sword (the logo's drop picture), once it's there */
static SDL_Texture* altSwordTex[2];
static int altSwordW, altSwordH, altSwordTry;

void alt_sword_load(void) {
	if (altSwordTex[0] || altSwordTex[1] || ++altSwordTry % 60 != 1) return; /* (tried once a second: it's made after the logo) */
	char lp[1200], dp[1200];
	if (ui_logo_source(cfg, romPath, emu.gameCode, lp, sizeof lp) != LOGO_EXTRACTED) return;
	logo_drop_path(lp, dp, sizeof dp);
	FILE* f = fopen(dp, "rb");
	if (!f) return;
	fclose(f);
	if (ui_load_image(dp, altSwordTex, &altSwordW, &altSwordH, false)) fprintf(stderr, "title: the sword from %s\n", dp);
}

/* The top screen at the game's title: black, and the sword coming down it
 * to where the game's own comes into its screen, at the same speed, its
 * screen's bottom edge taken as the game's screen's top */
void draw_alt_title(int s) {
	SDL_Rect r = screens[s].rect;
	ui_fill(s, (SDL_Rect) { 0, 0, r.w, r.h }, (SDL_Color) { 0, 0, 0, 255 });
	int y, g = game_screen();
	if (!altSwordTex[screens[s].view] || !alttp_title_sword(alt, &y)) return;
	SDL_Rect img = ui_image_rect(g, picW, picH, cfg->scale);
	float k = picH > 0 ? img.h / (float) picH : 1;
	/* in millimetres across the two screens: each one's pixels per inch
	 * (the same when not known), the gap between them (screen_gap_mm) */
	float pt = 0, pb = 0, gap = cfg->screenGapMm >= 0 ? (float) cfg->screenGapMm : 0;
#ifdef __ANDROID__
	pt = android_display_ppi(screens[s].view), pb = android_display_ppi(screens[g].view);
	if (cfg->screenGapMm < 0) gap = (float) android_screen_gap_mm();
#endif
	float ratio = pt > 0 && pb > 0 ? pt / pb : 1, kt = k * ratio; /* top screen pixels to a game pixel */
	float gapPx = pt > 0 ? gap / 25.4f * pt : 0; /* the gap, in the top screen's pixels */
	int w = (int) lroundf(altSwordW * kt), h = (int) lroundf(altSwordH * kt);
	int ty = r.h + (int) lroundf(gapPx + img.y * ratio + y * kt);
	int tx = r.w / 2 + (int) lroundf((img.x + 48 * k - screens[g].rect.w / 2.0f) * ratio);
	if (ty >= r.h || ty + h <= 0) return;
	ui_image_part(s, altSwordTex, (SDL_Rect) { 0, 0, altSwordW, altSwordH }, (SDL_Rect) { tx, ty, w, h });
} /* the zoom indicator, put together (alt_zoom_indicator) */
static int altZoomAt = -1; /* the level it shows (-1: to be made again) */
/* ALTTP-1: the items screen over the map, as the game's own comes over the
 * game: the panes solid (their windows black), clear round them. (By the
 * panes' places: their titles, DO and ITEM, sit on the frames' top edges, so
 * the black round a pane runs into it there.) */
static const uint32_t* alt_items_clear(const uint32_t* in) {
	static uint32_t out[ALTTP_W * ALTTP_H];
	int r[8][4], n = alttp_items_panes(r, 8);
	for (int y = 0; y < ALTTP_H; ++y) {
		for (int x = 0; x < ALTTP_W; ++x) {
			int i = y * ALTTP_W + x;
			bool in_pane = false;
			for (int k = 0; k < n && !in_pane; ++k) in_pane = x >= r[k][0] && y >= r[k][1] && x < r[k][0] + r[k][2] && y < r[k][1] + r[k][3];
			out[i] = !in_pane ? 0 : (in[i] >> 24) ? in[i] | 0xFF000000u : 0xFF000000u;
		}
	}
	return out;
}
/* ALTTP-1: how far the items are up (0 away below the screen, 1 up) */
static float altItemsUp = 1;
static Uint64 altSlideT;

/* The map's pieces on the GPU (alttp_piece): made when first shown, made
 * again when the piece changed (at most every ALT_REMAKE seconds while
 * Link walks), the least lately shown let go past ALT_BUDGET bytes. The
 * map scrolls and zooms by moving them: nothing is drawn on the CPU then. */
typedef struct {
	int world, piece, level; /* -1: free */
	uint32_t ver;            /* the piece's version it was made from */
	Uint64 madeT;
	unsigned shown;          /* the frame it was last shown */
	/* two textures (one view's each: the second screen's), made again by
	 * turns: the one shown is never written, which made drivers wait for
	 * the GPU to finish drawing from it (the dungeon map's dropped frames
	 * on the Duo); texB shown when flip */
	SDL_Texture *tex[2], *texB[2];
	bool flip;
} AltPiece;
#define ALT_CACHE 128
#define ALT_BUDGET (40u << 20)
#define ALT_REMAKE 0.25
static AltPiece altCache[ALT_CACHE];
static size_t altCacheBytes;
static unsigned altFrame;
/* The pieces are made on a thread of their own, one at a time, from a copy
 * of what each is made from (alttp_piece_job): the frames never wait on
 * it. State 0 idle, 1 a piece asked for, 2 being made, 3 made (altWOut). */
static SDL_Thread* altWorker;
static SDL_mutex* altWLock;
static SDL_cond* altWCond;
static bool altWQuit;
static int altWState, altWWorld, altWPiece, altWLevel;
static uint32_t altWVer;
static AltPieceJob* altWJob;
static uint32_t* altWOut;
/* ALTTP-6: the floor shown at the top of the dungeon map (as the game
 * opens it until the player picks one; back to that when Link changes
 * floor), and Link's floor when it was picked */
static int altFloor = ALTTP_FLOOR_AS_GAME, altFloorLink;
static uint32_t altDunKey; /* what the dungeon map's texture was drawn from (0: nothing) */
/* ALTTP-5: the dungeon map widened to the screen's shape, its frame's
 * colour (its edge pixels) at the sides, drawn as one picture so the
 * LCD grid falls on the sides as on the frame */
static uint32_t altDunPx[ALTTP_W * ALTTP_H];
static bool altDunNew;
static uint32_t* altDunWide;
static int altDunWW, altDunWH, altDunWView = -1;
static SDL_Texture *altDunWideTex[2], *altDunWideTexB[2]; /* two, written by turns (below) */
static bool altDunFlip;                 /* altDunWideTexB the one shown */
static uint32_t altDunShown[ALTTP_W * ALTTP_H]; /* the picture last sent to the GPU */
/* The view (the map's top left, world pixels): on Link, or the player's
 * from a drag until Link enters another area (ALTTP-3) */
static float altVX, altVY, altVelX, altVelY;
static bool altHaveView, altUser, altDrag, altDragMoved, altTapOk;
static int altDownX, altDownY;
static float altDownVX, altDownVY, altPrevX, altPrevY;
static Uint64 altStepT, altPrevT, altSavedT;
static int altFile; /* the game's file whose seen record is loaded (1 to 3) */
/* A loaded state's seen record, merged once Link is played from its file */
static uint8_t* altPending;
static size_t altPendingN;
static int altPendingFile;

/* The seen record of file f, beside the battery save: <save>.alttp<f> */
static void alt_seen_path(int f, char* out, size_t n) {
	snprintf(out, n, "%s.alttp%d", gp.sav, f);
}

/* The seen record's pieces compressed ahead of its saves, on a thread of
 * their own, one at a time (a save used to compress them all at once on
 * the main thread: a stall of 50 to 70 ms each minute on the Duo) */
static struct {
	SDL_Thread* thread;
	SDL_atomic_t done;
	int world, piece;
	uint32_t ver, zn;
	uint16_t* ground;
	uint8_t* z;
} altPack;

static int alt_compress_run(void* unused) {
	(void) unused;
	uLongf zn = compressBound(ALTTP_PIECE * ALTTP_PIECE * 2);
	altPack.z = malloc(zn);
	if (altPack.z && compress2(altPack.z, &zn, (const Bytef*) altPack.ground, ALTTP_PIECE * ALTTP_PIECE * 2, 6) == Z_OK) altPack.zn = (uint32_t) zn;
	else free(altPack.z), altPack.z = NULL;
	SDL_AtomicSet(&altPack.done, 1);
	return 0;
}

/* Once a pass: a finished piece handed back, the next started (every
 * half second at most) */
static void alt_compress_tick(void) {
	static Uint64 lastT;
	if (altPack.thread) {
		if (!SDL_AtomicGet(&altPack.done)) return;
		SDL_WaitThread(altPack.thread, NULL);
		altPack.thread = NULL;
		if (altPack.z && alt) alttp_put_compressed(alt, altPack.world, altPack.piece, altPack.ver, altPack.z, altPack.zn);
		else free(altPack.z);
		altPack.z = NULL;
	}
	Uint64 now = SDL_GetPerformanceCounter();
	if (!alt || (double) (now - lastT) / SDL_GetPerformanceFrequency() < 0.5) return;
	lastT = now;
	if (!altPack.ground) altPack.ground = malloc((size_t) ALTTP_PIECE * ALTTP_PIECE * 2);
	if (!altPack.ground || !alttp_stale_piece(alt, &altPack.world, &altPack.piece, altPack.ground, &altPack.ver)) return;
	SDL_AtomicSet(&altPack.done, 0);
	altPack.thread = SDL_CreateThread(alt_compress_run, "map record", NULL);
}

static void alt_compress_stop(void) {
	if (altPack.thread) SDL_WaitThread(altPack.thread, NULL), altPack.thread = NULL;
	free(altPack.z), altPack.z = NULL;
	free(altPack.ground), altPack.ground = NULL;
}

/* The seen record saved while playing (every minute): copied on the
 * game's thread, compressed and written on another (ALTTP-2; it took 20
 * to 60 ms on the Duo on the game's thread, a dropped frame a minute) */
static struct {
	SDL_Thread* thread;
	SDL_atomic_t done;
	AltSaveJob* job;
	char path[1100];
	bool ok;
	int file;
	double copyMs;
	Uint64 t0;
} altSave;

static int alt_save_run(void* unused) {
	(void) unused;
	altSave.ok = alttp_save_write(altSave.job, altSave.path);
	SDL_AtomicSet(&altSave.done, 1);
	return 0;
}

/* A save started: handed back once written (wait: until it is) */
static void alt_save_finish(bool wait) {
	if (!altSave.thread || (!wait && !SDL_AtomicGet(&altSave.done))) return;
	SDL_WaitThread(altSave.thread, NULL);
	altSave.thread = NULL;
	if (alt) alttp_save_end(alt, altSave.job, altSave.ok);
	altSave.job = NULL;
	fprintf(stderr, "map seen (file %d): %s %s (%.1f ms on the game's thread, %.0f ms in all)\n", altSave.file,
	        altSave.ok ? "saved to" : "can't save", altSave.path, altSave.copyMs,
	        (double) (SDL_GetPerformanceCounter() - altSave.t0) * 1000.0 / SDL_GetPerformanceFrequency());
}

static void alt_save_seen(void);

static void alt_save_start(void) {
	if (altSave.thread || !alt || !altFile || !alttp_dirty(alt) || !gp.sav[0]) return;
	alt_seen_path(altFile, altSave.path, sizeof altSave.path);
	altSave.file = altFile;
	altSave.t0 = SDL_GetPerformanceCounter();
	altSave.job = alttp_save_begin(alt);
	if (!altSave.job) return;
	altSave.copyMs = (double) (SDL_GetPerformanceCounter() - altSave.t0) * 1000.0 / SDL_GetPerformanceFrequency();
	SDL_AtomicSet(&altSave.done, 0);
	altSave.thread = SDL_CreateThread(alt_save_run, "map save", NULL);
	if (!altSave.thread) {
		/* (no thread: written here, as before) */
		alttp_save_end(alt, altSave.job, false);
		altSave.job = NULL;
		alt_save_seen();
	}
}

static void alt_save_seen(void) {
	alt_save_finish(true); /* (one started before: written first) */
	if (!alt || !altFile || !alttp_dirty(alt) || !gp.sav[0]) return;
	char path[1100];
	alt_seen_path(altFile, path, sizeof path);
	Uint64 t0 = SDL_GetPerformanceCounter();
	bool ok = alttp_save_seen(alt, path);
	fprintf(stderr, "map seen (file %d): %s %s (%.0f ms)\n", altFile, ok ? "saved to" : "can't save", path,
	        (double) (SDL_GetPerformanceCounter() - t0) * 1000.0 / SDL_GetPerformanceFrequency());
}

/* ALTTP-2 in save states: the file's seen record (section "ALTP": the
 * file, 4 bytes, then the record as its own file has it). Loading one adds
 * what it had seen to that file's record; nothing seen is taken away. */
void alt_state_block(Blob* b) {
	if (!alt || !altFile) return;
	size_t n = 0;
	uint8_t* z = alttp_seen_pack(alt, &n);
	if (!z) return;
	Blob m = { 0 };
	blob_u32(&m, (uint32_t) altFile), blob_add(&m, z, n);
	if (m.p) blob_section(b, "ALTP", m.p, m.n);
	free(m.p);
	free(z);
}

static void alt_merge_pending(void) {
	if (!alt || !altPending || !altFile || altPendingFile != altFile) return;
	bool ok = alttp_seen_merge(alt, altPending, altPendingN);
	fprintf(stderr, "map seen (file %d): the state's record %s\n", altFile, ok ? "added" : "not readable");
	free(altPending);
	altPending = NULL;
}

void alt_state_restore(const uint8_t* d, uint32_t n) {
	if (!alt || n <= 4) return;
	free(altPending);
	altPending = malloc(n - 4);
	if (!altPending) return;
	memcpy(altPending, d + 4, n - 4);
	altPendingN = n - 4, altPendingFile = (int) rd32(d);
	alt_merge_pending(); /* (now if it's this file's, else once its file is played) */
}

static int alt_worker_run(void* unused) {
	(void) unused;
	SDL_LockMutex(altWLock);
	for (;;) {
		while (!altWQuit && altWState != 1) SDL_CondWait(altWCond, altWLock);
		if (altWQuit) break;
		AltPieceJob* j = altWJob;
		int level = altWLevel;
		altWJob = NULL, altWState = 2;
		SDL_UnlockMutex(altWLock);
		alttp_piece_make(j, level, altWOut);
		free(j);
		SDL_LockMutex(altWLock);
		altWState = 3;
	}
	SDL_UnlockMutex(altWLock);
	return 0;
}

static bool alt_worker_start(void) {
	if (altWorker) return true;
	altWOut = malloc((size_t) ALTTP_PIECE * ALTTP_PIECE * 4);
	altWLock = SDL_CreateMutex(), altWCond = SDL_CreateCond();
	altWQuit = false, altWState = 0;
	if (altWOut && altWLock && altWCond) altWorker = SDL_CreateThread(alt_worker_run, "map pieces", NULL);
	if (!altWorker) fprintf(stderr, "map: no thread for its pieces\n");
	return altWorker != NULL;
}

static void alt_worker_stop(void) {
	if (altWorker) {
		SDL_LockMutex(altWLock);
		altWQuit = true;
		SDL_CondSignal(altWCond);
		SDL_UnlockMutex(altWLock);
		SDL_WaitThread(altWorker, NULL);
		altWorker = NULL;
	}
	free(altWJob);
	altWJob = NULL;
	if (altWCond) SDL_DestroyCond(altWCond);
	if (altWLock) SDL_DestroyMutex(altWLock);
	altWCond = NULL, altWLock = NULL;
	free(altWOut);
	altWOut = NULL;
	altWState = 0;
}

static void alt_cache_clear(void) {
	for (int k = 0; k < ALT_CACHE; ++k) {
		for (int i = 0; i < 2; ++i) {
			if (altCache[k].tex[i]) SDL_DestroyTexture(altCache[k].tex[i]);
			if (altCache[k].texB[i]) SDL_DestroyTexture(altCache[k].texB[i]);
		}
		memset(&altCache[k], 0, sizeof altCache[k]);
		altCache[k].world = -1;
	}
	altCacheBytes = 0;
}

void alt_free(void) {
	free(altPending);
	altPending = NULL;
	if (!alt) return;
	alt_compress_stop();
	alt_save_seen();
	alttp_free(alt);
	alt = NULL;
	altShown = altHaveView = altUser = altDrag = altPinch = false;
	ui_set_pinch(false);
	altPage = 0, altFile = 0, altFloor = ALTTP_FLOOR_AS_GAME, altDunKey = 0;
	for (int i = 0; i < 2; ++i) {
		SDL_Texture** t[] = { &altItemsTex[i], &altMarkerTex[i], &altDunWideTex[i], &altDunWideTexB[i], &altSkyTex[0][i], &altSkyTex[1][i], &altGoalTex[i], &altTriTex[i], &altZoomTex[i], &altSwordTex[i] };
		for (int k = 0; k < 10; ++k) {
			if (*t[k]) SDL_DestroyTexture(*t[k]);
			*t[k] = NULL;
		}
	}
	alt_cache_clear();
	alt_worker_stop();
}

void alt_open(const char* rom) {
	size_t n = 0;
	uint8_t* data = emu_rom_read_played(rom, &n, NULL, NULL, NULL);
	alt = alttp_new(data, n);
	fprintf(stderr, "second screen: %s\n", alt ? "drawn by RetroDS (A Link to the Past's items and map)" : "can't draw A Link to the Past's screens");
}

void alt_textures(void) {
	if (!alt) return;
	alt_cache_clear();
	for (int i = 0; i < nViews; ++i) {
		SDL_Texture** t[] = { &altItemsTex[i], &altMarkerTex[i], &altDunWideTex[i], &altDunWideTexB[i], &altSkyTex[0][i], &altSkyTex[1][i], &altGoalTex[i], &altTriTex[i], &altZoomTex[i] };
		for (int k = 0; k < 9; ++k) {
			if (*t[k]) SDL_DestroyTexture(*t[k]);
		}
		altItemsTex[i] = native_texture(i, ALTTP_W, ALTTP_H, SDL_BLENDMODE_BLEND); /* (clear round the panes: ALTTP-1) */
		altDunWideTex[i] = altDunWideTexB[i] = NULL; /* (made at its size when shown) */
		altMarkerTex[i] = native_texture(i, 32, 32, SDL_BLENDMODE_BLEND);
		altGoalTex[i] = native_texture(i, ALTTP_GOAL_MAX * 16, 16, SDL_BLENDMODE_BLEND);
		altTriTex[i] = native_texture(i, ALT_TRI_W, ALT_TRI_H, SDL_BLENDMODE_BLEND);
		if (altTriTex[i]) ui_update_texture(altTriTex[i], alt_triforce(), ALT_TRI_W * 4);
		altZoomTex[i] = native_texture(i, ALTTP_ZOOM_PANE_W, ALTTP_ZOOM_PANE_H, SDL_BLENDMODE_BLEND);
		altZoomAt = -1;
		if (altGoalTex[i]) ui_update_texture(altGoalTex[i], alttp_goal_pictures(alt), ALTTP_GOAL_MAX * 16 * 4);
		for (int w = 0; w < 2; ++w) {
			altSkyTex[w][i] = native_texture(i, ALTTP_SKY_W, ALTTP_SKY_H, SDL_BLENDMODE_NONE);
			if (altSkyTex[w][i]) ui_update_texture(altSkyTex[w][i], alttp_sky(alt, w), ALTTP_SKY_W * 4);
		}
	}
	altItemsDirty = true;
	altDunKey = 0, altDunWView = -1;
}

/* The map fills the second screen, whatever its shape (ALTTP-3): its
 * size in world pixels at the zoom */
static void alt_view_size(float* w, float* h) {
	SDL_Rect a = screens[nScreens == 2 ? 1 - game_screen() : 0].rect;
	*w = SDL_max(a.w, 1) * ALT_Z, *h = SDL_max(a.h, 1) * ALT_Z;
}

/* The view inside the world (in the middle where the world is smaller) */
static void alt_clamp(float* vx, float* vy) {
	float w, h;
	alt_view_size(&w, &h);
	*vx = w >= ALTTP_WORLD ? (ALTTP_WORLD - w) / 2 : SDL_clamp(*vx, 0.0f, ALTTP_WORLD - w);
	*vy = h >= ALTTP_WORLD ? (ALTTP_WORLD - h) / 2 : SDL_clamp(*vy, 0.0f, ALTTP_WORLD - h);
}

/* Where the view rests: Link in the middle, inside the world */
static void alt_home(float* vx, float* vy) {
	int wd, x, y;
	alttp_link(alt, &wd, &x, &y);
	float w, h;
	alt_view_size(&w, &h);
	*vx = x - w / 2, *vy = y - h / 2;
	alt_clamp(vx, vy);
}

/* ALTTP-2: the seen record goes with its file. Erased on the game's file
 * screen: its record set aside (<record>.bak, the one before replaced);
 * copied: the source's record copied to the new file (the one there set
 * aside first). */
static void alt_set_aside(const char* path) {
	char bak[1200];
	snprintf(bak, sizeof bak, "%s.bak", path);
	FILE* f = fopen(path, "rb");
	if (!f) return;
	fclose(f);
	remove(bak);
	if (rename(path, bak) == 0) fprintf(stderr, "map seen: %s set aside as %s\n", path, bak);
}

static bool alt_copy_file(const char* from, const char* to) {
	FILE* in = fopen(from, "rb");
	if (!in) return false;
	FILE* out = fopen(to, "wb");
	bool ok = out != NULL;
	char buf[65536];
	size_t n;
	while (ok && (n = fread(buf, 1, sizeof buf, in)) > 0) ok = fwrite(buf, 1, n, out) == n;
	fclose(in);
	if (out && fclose(out) != 0) ok = false;
	return ok;
}

static void alt_file_events(void) {
	AltSlotEvent ev[8];
	int n = alttp_take_file_events(alt, ev, 8);
	if (n > 0) alt_save_finish(true); /* (a save being written: done before files move) */
	for (int i = 0; i < n && gp.sav[0]; ++i) {
		char path[1100], from[1100];
		alt_seen_path(ev[i].file, path, sizeof path);
		if (ev[i].kind == ALTTP_FILE_ERASED) {
			fprintf(stderr, "map seen: the game's file %d erased\n", ev[i].file);
			alt_set_aside(path);
			if (altFile == ev[i].file) alttp_clear_seen(alt);
		} else if (ev[i].kind == ALTTP_FILE_COPIED) {
			fprintf(stderr, "map seen: the game's file %d copied to %d\n", ev[i].from, ev[i].file);
			if (altFile == ev[i].from) alt_save_seen(); /* (its file up to date first) */
			alt_seen_path(ev[i].from, from, sizeof from);
			alt_set_aside(path);
			bool ok = alt_copy_file(from, path);
			fprintf(stderr, "map seen: %s %s\n", ok ? "copied to" : "nothing to copy to", path);
			if (altFile == ev[i].file) {
				if (ok) alttp_load_seen(alt, path);
				else alttp_clear_seen(alt);
			}
		}
	}
}

/* After the frames of a pass of the loop: the game read again */
void alt_tick(void) {
	mapWork.on = true;
	size_t n = 0, vn = 0, sn = 0;
	const uint8_t* w = emu_memory(&emu, EMU_MEM_WRAM, &n);
	const uint8_t* v = emu_memory(&emu, EMU_MEM_VRAM, &vn);
	const uint8_t* sr = emu_memory(&emu, EMU_MEM_SRAM, &sn);
	if (!w || n < 0x20000) return;
	int ch = alttp_update(alt, w, v && vn >= 0x10000 ? v : NULL, sr && sn >= 0x2000 ? sr : NULL);
	if (ch & ALTTP_FILE) {
		/* another of the game's files: its own seen record */
		alt_save_seen();
		altFile = alttp_file(alt);
		char path[1100];
		alt_seen_path(altFile, path, sizeof path);
		if (alttp_take_new_file(alt)) {
			alttp_clear_seen(alt);
			fprintf(stderr, "map seen (file %d): a new file, starts empty\n", altFile);
		} else {
			bool ok = alttp_load_seen(alt, path);
			fprintf(stderr, "map seen (file %d): %s %s\n", altFile, ok ? "loaded from" : "nothing yet in", path);
		}
	}
	alt_merge_pending();
	alt_compress_tick();
	alt_save_finish(false);
	if (ch & ALTTP_FILES) alt_file_events();
	if (ch & ALTTP_ITEMS) altItemsDirty = true, altZoomAt = -1;
	for (int i = 0; i < nViews && (ch & ALTTP_MARKER); ++i) {
		if (altMarkerTex[i]) ui_update_texture(altMarkerTex[i], alttp_marker(alt), 32 * 4);
	}
	for (int i = 0; i < nViews && (ch & ALTTP_GOALS); ++i) {
		if (altGoalTex[i]) ui_update_texture(altGoalTex[i], alttp_goal_pictures(alt), ALTTP_GOAL_MAX * 16 * 4);
	}
	if ((ch & ALTTP_AREA) && !altDrag) altUser = false, altVelX = altVelY = 0; /* ALTTP-3: back on Link */
	/* ALTTP-9: the logo at the menus, and at game over (the game takes the
	 * video memory the maps are drawn from there) */
	if (alttp_in_menu(alt) || alttp_game_over(alt)) altShown = false;
	else if (alttp_ready(alt)) altShown = true;
	/* the seen record, now and then while playing */
	Uint64 now = SDL_GetPerformanceCounter();
	if (!altSavedT) altSavedT = now;
	if ((double) (now - altSavedT) / SDL_GetPerformanceFrequency() > 60) altSavedT = now, alt_save_start();
	/* ALTTP-5: the dungeon map, under the items (it blinks as the game's) */
	if (alttp_in_dungeon(alt) && alttp_playing(alt)) {
		/* (drawn while Link is played: elsewhere, game over and the like,
		 * video memory holds other tiles; the last drawing stays) */
		int lo, hi, link;
		if (alttp_dungeon_floors(alt, &lo, &hi, &link) && altFloor != ALTTP_FLOOR_AS_GAME && link != altFloorLink)
			altFloor = ALTTP_FLOOR_AS_GAME; /* ALTTP-6: Link changed floor */
		/* drawn again when it can have changed: its quickest blink is 4
		 * frames; the rooms, Link and the items with the save data */
		uint32_t key = (uint32_t) (w[0x1A] >> 2) | (uint32_t) (altFloor & 0xFF) << 8 | (uint32_t) w[0xA4] << 16 | (uint32_t) w[0xA0] << 24;
		key ^= (uint32_t) emu_crc32(0, w + 0xF000, 0x500) ^ (uint32_t) (w[0x20] | w[0x22] << 8) * 0x9E3779B1u;
		const uint32_t* px = key != altDunKey ? alttp_dungeon_map(alt, v && vn >= 0x10000 ? v : NULL, altFloor) : NULL;
		if (px) altDunKey = key, memcpy(altDunPx, px, sizeof altDunPx), altDunNew = true;
	} else if (!alttp_in_dungeon(alt)) {
		altFloor = ALTTP_FLOOR_AS_GAME;
	}
	if (altItemsDirty) {
		const uint32_t* items = alt_items_clear(alttp_items(alt));
		for (int i = 0; i < nViews; ++i) {
			if (altItemsTex[i]) ui_update_texture(altItemsTex[i], items, ALTTP_W * 4);
			if (altMarkerTex[i]) ui_update_texture(altMarkerTex[i], alttp_marker(alt), 32 * 4);
		}
		altItemsDirty = false;
	}
}

/* Once a drawn frame: the view moves on (a glide back to Link, a fling) */
void alt_step(void) {
	Uint64 now = SDL_GetPerformanceCounter();
	float dt = altStepT ? (float) ((double) (now - altStepT) / SDL_GetPerformanceFrequency()) : 0;
	altStepT = now;
	dt = SDL_min(dt, 0.1f);
	if (!altPinch && (altZ != altZooms[altZoomI] || altFocus)) {
		/* the zoom glides to its level round the middle of the screen, the
		 * place a tap was at gliding to the middle meanwhile */
		float w, h;
		alt_view_size(&w, &h);
		float mx = altVX + w / 2, my = altVY + h / 2, to = altZooms[altZoomI];
		float k = 1 - expf(-dt / 0.06f);
		altZ = fabsf(to - altZ) < 0.002f * to ? to : altZ * powf(to / altZ, k);
		if (altFocus) {
			mx += (altFocusX - mx) * k, my += (altFocusY - my) * k;
			if (fabsf(altFocusX - mx) < 0.5f && fabsf(altFocusY - my) < 0.5f) mx = altFocusX, my = altFocusY, altFocus = false;
		}
		alt_view_size(&w, &h);
		altVX = mx - w / 2, altVY = my - h / 2;
		if (altUser) alt_clamp(&altVX, &altVY);
	}
	float hx, hy;
	alt_home(&hx, &hy);
	if (!altHaveView) altVX = hx, altVY = hy, altHaveView = true;
	if (altDrag || altPinch) return;
	if (altUser) {
		if (altVelX == 0 && altVelY == 0) return;
		altVX += altVelX * dt, altVY += altVelY * dt;
		float slow = expf(-dt / 0.35f);
		altVelX *= slow, altVelY *= slow;
		float bx = altVX, by = altVY;
		alt_clamp(&altVX, &altVY);
		if (bx != altVX || fabsf(altVelX) < 4) altVelX = 0;
		if (by != altVY || fabsf(altVelY) < 4) altVelY = 0;
		return;
	}
	float a = 1 - expf(-dt / 0.09f);
	altVX += (hx - altVX) * a, altVY += (hy - altVY) * a;
	if (fabsf(hx - altVX) < 0.02f) altVX = hx;
	if (fabsf(hy - altVY) < 0.02f) altVY = hy;
}

static SDL_Rect alt_rect(int s, float* kx, float* ky) {
	SDL_Rect img = ui_image_rect(s, ALTTP_W, ALTTP_H, cfg->mapScale);
	*kx = img.w / (float) ALTTP_W, *ky = img.h / (float) ALTTP_H;
	return img;
}

/* A piece of the map as a texture of view v, made (again) if it changed:
 * NULL if it isn't made yet (this frame's time for making them gone) */
static size_t bytes_of(const AltPiece* e) {
	size_t n = (size_t) (ALTTP_PIECE >> e->level);
	return n * n * 4;
}

static AltPiece* alt_cache_find(int world, int piece, int level) {
	for (int k = 0; k < ALT_CACHE; ++k) {
		if (altCache[k].world == world && altCache[k].piece == piece && altCache[k].level == level) return &altCache[k];
	}
	return NULL;
}

static void alt_cache_drop(AltPiece* e) {
	for (int i = 0; i < 2; ++i) {
		if (e->tex[i]) SDL_DestroyTexture(e->tex[i]), altCacheBytes -= bytes_of(e), ++mapWork.dropped;
		if (e->texB[i]) SDL_DestroyTexture(e->texB[i]), altCacheBytes -= bytes_of(e), ++mapWork.dropped;
	}
	memset(e, 0, sizeof *e);
	e->world = -1;
}

/* A place for a piece: a free one, else the one shown longest ago (not
 * this frame); within the budget, the ones shown longest ago let go */
static AltPiece* alt_cache_take(int world, int piece, int level) {
	AltPiece* e = alt_cache_find(world, piece, level);
	if (e) return e;
	for (int k = 0; k < ALT_CACHE; ++k) {
		AltPiece* c = &altCache[k];
		if (c->world < 0) {
			e = c;
			break;
		}
		if (c->shown != altFrame && (!e || c->shown < e->shown)) e = c;
	}
	if (!e) return NULL;
	alt_cache_drop(e);
	e->world = world, e->piece = piece, e->level = level;
	size_t n = (size_t) (ALTTP_PIECE >> level);
	while (altCacheBytes + n * n * 4 > ALT_BUDGET) {
		AltPiece* old = NULL;
		for (int k = 0; k < ALT_CACHE; ++k) {
			AltPiece* c = &altCache[k];
			if (c != e && c->world >= 0 && c->shown != altFrame && (!old || c->shown < old->shown)) old = c;
		}
		if (!old) break;
		alt_cache_drop(old);
	}
	return e;
}

/* The piece the thread made, onto its texture (view v) */
static void alt_piece_collect(int v) {
	if (!altWorker) return;
	SDL_LockMutex(altWLock);
	if (altWState == 3) {
		AltPiece* e = alt_cache_take(altWWorld, altWPiece, altWLevel);
		int n = ALTTP_PIECE >> altWLevel;
		/* (into the texture not shown: the first time, tex) */
		bool shownAny = e && (e->flip ? e->texB[v] : e->tex[v]);
		SDL_Texture** t = !e ? NULL : !shownAny ? (e->flip ? &e->texB[v] : &e->tex[v]) : (e->flip ? &e->tex[v] : &e->texB[v]);
		if (t && !*t && (*t = native_texture(v, n, n, SDL_BLENDMODE_BLEND))) altCacheBytes += bytes_of(e), ++mapWork.made;
		if (t && *t) {
			ui_update_texture(*t, altWOut, n * 4);
			++mapWork.uploads;
			if (shownAny) e->flip = !e->flip;
			e->ver = altWVer, e->madeT = SDL_GetPerformanceCounter();
		}
		altWState = 0;
	}
	SDL_UnlockMutex(altWLock);
}

/* A piece of the map as a texture of view v (NULL: not made yet); *want:
 * it's to be made (again: changed, and made ALT_REMAKE seconds ago or
 * more) */
static SDL_Texture* const* alt_piece_tex(int v, int world, int piece, int level, uint32_t ver, bool* want) {
	AltPiece* e = alt_cache_find(world, piece, level);
	if (e) e->shown = altFrame;
	SDL_Texture* const* shown = e ? (e->flip ? e->texB : e->tex) : NULL;
	bool have = shown && shown[v];
	*want = !have || (e->ver != ver && (double) (SDL_GetPerformanceCounter() - e->madeT) / SDL_GetPerformanceFrequency() >= ALT_REMAKE);
	return have ? shown : NULL;
}

/* The thread asked for a piece, when it's free */
static void alt_piece_ask(int world, int piece, int level, uint32_t ver) {
	if (!alt_worker_start()) return;
	SDL_LockMutex(altWLock);
	if (altWState == 0) {
		altWJob = alttp_piece_job(alt, world, piece);
		if (altWJob) {
			altWWorld = world, altWPiece = piece, altWLevel = level, altWVer = ver, altWState = 1;
			SDL_CondSignal(altWCond);
			/* (made again no sooner than ALT_REMAKE from now) */
			AltPiece* e = alt_cache_find(world, piece, level);
			if (e) e->madeT = SDL_GetPerformanceCounter();
		}
	}
	SDL_UnlockMutex(altWLock);
}

/* A Triforce, drawn here (three gold triangles, a dark outline), for the
 * zoom indicator's current level */
static const uint32_t* alt_triforce(void) {
	static uint32_t px[ALT_TRI_W * ALT_TRI_H];
	static bool made;
	if (made) return px;
	made = true;
	bool in[ALT_TRI_H][ALT_TRI_W];
	for (int y = 0; y < ALT_TRI_H; ++y) {
		for (int x = 0; x < ALT_TRI_W; ++x) {
			float dx = fabsf(x + 0.5f - ALT_TRI_W / 2.0f);
			float half = (y + 1) * (ALT_TRI_W / 2.0f) / ALT_TRI_H; /* the whole triangle's */
			bool inside = dx <= half;
			/* the hole: an upside-down triangle in the lower half */
			if (y >= ALT_TRI_H / 2) inside = inside && dx > (ALT_TRI_H - 1 - y + 0.5f) * (ALT_TRI_W / 2.0f) / ALT_TRI_H;
			in[y][x] = inside;
		}
	}
	for (int y = 0; y < ALT_TRI_H; ++y) {
		for (int x = 0; x < ALT_TRI_W; ++x) {
			uint32_t c = 0;
			if (in[y][x]) {
				bool edge = y == 0 || x == 0 || y == ALT_TRI_H - 1 || x == ALT_TRI_W - 1 || !in[y - 1][x] || !in[y + 1][x] || !in[y][x - 1] || !in[y][x + 1];
				/* (0xAABBGGRR) the outline dark brown; the gold lighter at the left */
				c = edge ? 0xFF003860u : x < ALT_TRI_W / 2 ? 0xFF58E0F8u : 0xFF30C0F0u;
			}
			px[y * ALT_TRI_W + x] = c;
		}
	}
	return px;
}

/* ALTTP-3: the zoom's levels in the screen's bottom right corner, in a
 * pane of the items screen's (its frame from the game's tiles, black
 * inside, solid as the game's panes), the closest at the top: a dot
 * each, a Triforce for the level it's at (or nearest, while pinched).
 * Put together as one picture, drawn at a whole number of screen pixels
 * to one of its own: the game's pixel size. */
static void alt_zoom_indicator(int s, SDL_Rect a) {
	int v = screens[s].view;
	if (!altZoomTex[v]) return;
	int at = 0;
	for (int i = 1; i < ALT_ZOOMS; ++i) {
		if (fabsf(logf(altZooms[i] / altZ)) < fabsf(logf(altZooms[at] / altZ))) at = i;
	}
	if (at != altZoomAt) {
		enum { W = ALTTP_ZOOM_PANE_W, H = ALTTP_ZOOM_PANE_H };
		static uint32_t px[W * H];
		memcpy(px, alttp_zoom_pane(alt), sizeof px);
		const uint32_t* tri = alt_triforce();
		static const char dot[4][5] = { ".##.", "####", "####", ".##." };
		for (int i = 0; i < ALT_ZOOMS; ++i) {
			int y0 = 8 + i * 16;
			if (i == at) {
				for (int y = 0; y < ALT_TRI_H; ++y) {
					for (int x = 0; x < ALT_TRI_W; ++x) {
						if (tri[y * ALT_TRI_W + x] >> 24) px[(y0 + 1 + y) * W + 8 + x] = tri[y * ALT_TRI_W + x];
					}
				}
			} else {
				for (int y = 0; y < 4; ++y) {
					for (int x = 0; x < 4; ++x) {
						if (dot[y][x] == '#') px[(y0 + 6 + y) * W + 14 + x] = 0xFF909090u;
					}
				}
			}
		}
		ui_update_texture(altZoomTex[v], px, W * 4);
		altZoomAt = at;
	}
	/* (its pixels the size of the game's on the game screen, by the two
	 * screens' pixels per inch, to a whole number: the owner, Nov 24, 2026;
	 * the sizes unknown, the game screen's pixels as they are) */
	int g = game_screen();
	SDL_Rect gi = ui_image_rect(g, picW, picH, cfg->scale);
	float gp = picH > 0 ? (float) gi.h / picH : 2, pg = ui_screen_ppi(g), ps = ui_screen_ppi(s);
	if (pg > 0 && ps > 0) gp *= ps / pg;
	int k = SDL_max(1, (int) (gp + 0.5f)), m = SDL_max(2, a.h / 54);
	int w = ALTTP_ZOOM_PANE_W * k, h = ALTTP_ZOOM_PANE_H * k;
	ui_image_part(s, altZoomTex, (SDL_Rect) { 0, 0, ALTTP_ZOOM_PANE_W, ALTTP_ZOOM_PANE_H }, (SDL_Rect) { a.x + a.w - w - m, a.y + a.h - h - m, w, h });
}

/* The map (ALTTP-2, ALTTP-3): the whole second screen, whatever its
 * shape; the clouds, the seen ground over them in pieces at the zoom's
 * level (each texture pixel one or two of the screen's), Link's marker at
 * a size of its own. The view's top left is (altVX, altVY), world pixels. */
static void draw_alt_map(int s) {
	int world, lx, ly;
	alttp_link(alt, &world, &lx, &ly);
	/* (the whole screen, whatever screen_match makes of its picture's place) */
	SDL_Rect a = { 0, 0, screens[s].rect.w, screens[s].rect.h };
	int v = screens[s].view;
	float z = ALT_Z;
	++altFrame;
	map_work_frame(); /* (the pieces counted per redraw) */
	alt_piece_collect(v);
	SDL_Rect clip = { a.x + screens[s].rect.x, a.y + screens[s].rect.y, a.w, a.h };
	ui_clip(s, &clip);
	/* where the world's top left is on the screen */
	int ox = a.x - (int) lroundf(altVX / z), oy = a.y - (int) lroundf(altVY / z);
	/* the clouds, fixed to the world and zooming with it: ALT_SKY_TEXEL
	 * world pixels to one of theirs, the picture repeated every
	 * ALTTP_SKY_W x that (twice across a world: its repeats don't show,
	 * and a zoom grows and shrinks them with the ground; drawn at a size of
	 * their own on the screen, they repeated in a grid that jumped as the
	 * zoom changed: the owner, Nov 23, 2026) */
	{
		const float span = ALTTP_SKY_W * ALT_SKY_TEXEL, spanH = ALTTP_SKY_H * ALT_SKY_TEXEL;
		int n0 = (int) floorf(altVX / span), n1 = (int) floorf((altVX + a.w * z) / span);
		int m0 = (int) floorf(altVY / spanH), m1 = (int) floorf((altVY + a.h * z) / spanH);
		for (int my = m0; my <= m1; ++my) {
			int y = oy + (int) lroundf(my * spanH / z), yh = oy + (int) lroundf((my + 1) * spanH / z) - y;
			for (int mx = n0; mx <= n1; ++mx) {
				int x = ox + (int) lroundf(mx * span / z), xw = ox + (int) lroundf((mx + 1) * span / z) - x;
				ui_image_part(s, altSkyTex[world & 1], (SDL_Rect) { 0, 0, ALTTP_SKY_W, ALTTP_SKY_H }, (SDL_Rect) { x, y, xw, yh });
			}
		}
	}
	int k = SDL_max(1, a.h / 540);
	(void) k;
	/* the ground */
	int level = z >= 4 ? 2 : z >= 2 ? 1 : 0;
	int n = ALTTP_PIECE >> level;
	/* the piece to make next: the one nearest the view's middle */
	float cxw = altVX + a.w * z / 2, cyw = altVY + a.h * z / 2;
	int next = -1;
	uint32_t nextVer = 0;
	float nextD = 1e30f;
	for (int py = 0; py < ALTTP_PIECES; ++py) {
		/* (each edge rounded once, so pieces meet at any zoom) */
		int y = oy + (int) lroundf(py * ALTTP_PIECE / z), ph = oy + (int) lroundf((py + 1) * ALTTP_PIECE / z) - y;
		if (y >= a.y + a.h || y + ph <= a.y) continue;
		for (int px = 0; px < ALTTP_PIECES; ++px) {
			int x = ox + (int) lroundf(px * ALTTP_PIECE / z), pw = ox + (int) lroundf((px + 1) * ALTTP_PIECE / z) - x;
			if (x >= a.x + a.w || x + pw <= a.x) continue;
			int i = py * ALTTP_PIECES + px;
			uint32_t ver = alttp_piece_version(alt, world, i);
			bool want = false;
			SDL_Texture* const* t = ver ? alt_piece_tex(v, world, i, level, ver, &want) : NULL;
			if (t) ui_image_part(s, t, (SDL_Rect) { 0, 0, n, n }, (SDL_Rect) { x, y, pw, ph }), ++mapWork.drawn;
			float dx = (px + 0.5f) * ALTTP_PIECE - cxw, dy = (py + 0.5f) * ALTTP_PIECE - cyw;
			/* (one not made at all before one made but out of date) */
			float d = dx * dx + dy * dy + (t ? 1e12f : 0);
			if (want && d < nextD) next = i, nextVer = ver, nextD = d;
		}
	}
	if (next >= 0) alt_piece_ask(world, next, level, nextVer);
	/* ALTTP-11: the goal markers, then Link: their sprites at three of the
	 * screen's pixels to one of theirs (1080 lines), whatever the zoom */
	int m = SDL_max(1, a.h / 360);
	const AltGoal* gl;
	int ng = alttp_goals(alt, &gl);
	for (int i = 0; i < ng; ++i) {
		int gx = ox + (int) lroundf(gl[i].x / z) + gl[i].dx * m, gy = oy + (int) lroundf(gl[i].y / z) + gl[i].dy * m;
		ui_image_part(s, altGoalTex, (SDL_Rect) { i * 16, 0, gl[i].size, gl[i].size }, (SDL_Rect) { gx, gy, gl[i].size * m, gl[i].size * m });
	}
	alt_zoom_indicator(s, a);
	if (alttp_marker_on(alt)) {
		int mx = ox + (int) lroundf(lx / z) - 16 * m, my = oy + (int) lroundf(ly / z) - 16 * m;
		ui_image_part(s, altMarkerTex, (SDL_Rect) { 0, 0, 32, 32 }, (SDL_Rect) { mx, my, 32 * m, 32 * m });
	}
	ui_clip(s, NULL);
}

void draw_alt(int s) {
	/* in a dungeon the items slide over black, and the dungeon's map shows
	 * once they're away (the owner, Nov 24, 2026: the map under them on the
	 * overworld only) */
	bool dungeon = alttp_in_dungeon(alt);
	if (dungeon && altItemsUp > 0) {
		/* (black: the screen's fill, as before) */
	} else if (dungeon) {
		/* the picture's own scale, the canvas as many of its pixels as the screen holds */
		SDL_Rect img = ui_image_rect(s, ALTTP_W, ALTTP_H, cfg->mapScale);
		float k = img.w > 0 ? img.w / (float) ALTTP_W : 1;
		SDL_Rect pa = screens[s].pic;
		int W = SDL_clamp((int) (pa.w / k), ALTTP_W, 2048), H = SDL_clamp((int) (pa.h / k), ALTTP_H, 2048);
		int v = screens[s].view;
		if (W != altDunWW || H != altDunWH || v != altDunWView || !altDunWideTex[v]) {
			uint32_t* nb = realloc(altDunWide, (size_t) W * H * 4);
			if (!nb) return;
			altDunWide = nb, altDunWW = W, altDunWH = H, altDunWView = v;
			if (altDunWideTex[v]) SDL_DestroyTexture(altDunWideTex[v]);
			if (altDunWideTexB[v]) SDL_DestroyTexture(altDunWideTexB[v]);
			altDunWideTex[v] = native_texture(v, W, H, SDL_BLENDMODE_NONE);
			altDunWideTexB[v] = native_texture(v, W, H, SDL_BLENDMODE_NONE);
			altDunNew = true;
			memset(altDunShown, 0, sizeof altDunShown); /* (sent again) */
		}
		if (altDunNew && altDunWideTex[v]) {
			/* the background round the frames black, not the game's brown (the
			 * owner's choice, Nov 22, 2026): its colour reached from the
			 * picture's edge (the frames' wood has it too) */
			uint32_t back = altDunPx[0];
			static uint8_t out[ALTTP_W * ALTTP_H];
			static int stack[ALTTP_W * ALTTP_H];
			memset(out, 0, sizeof out);
			int n = 0;
			for (int i = 0; i < ALTTP_W * ALTTP_H; ++i) {
				int x = i % ALTTP_W, y = i / ALTTP_W;
				if ((x == 0 || y == 0 || x == ALTTP_W - 1 || y == ALTTP_H - 1) && altDunPx[i] == back) out[i] = 1, stack[n++] = i;
			}
			while (n) {
				int i = stack[--n], x = i % ALTTP_W, y = i / ALTTP_W;
				const int nb[4] = { x > 0 ? i - 1 : -1, x < ALTTP_W - 1 ? i + 1 : -1, y > 0 ? i - ALTTP_W : -1, y < ALTTP_H - 1 ? i + ALTTP_W : -1 };
				for (int k = 0; k < 4; ++k) {
					if (nb[k] >= 0 && !out[nb[k]] && altDunPx[nb[k]] == back) out[nb[k]] = 1, stack[n++] = nb[k];
				}
			}
			for (int i = 0; i < ALTTP_W * ALTTP_H; ++i) {
				if (out[i]) altDunPx[i] = 0xFF000000u;
			}
			altDunNew = false;
			/* sent to the GPU only when the picture has changed (most of its
			 * 4-frame steps change nothing), and into the texture not shown
			 * last: changing one the GPU may still be drawing from made the
			 * driver wait for it (frames of 25 ms and more on the Duo) */
			if (!memcmp(altDunShown, altDunPx, sizeof altDunShown) || !altDunWideTexB[v]) goto shown;
			memcpy(altDunShown, altDunPx, sizeof altDunShown);
			back = 0xFF000000u;
			for (int i = 0; i < W * H; ++i) altDunWide[i] = back;
			int x0 = (W - ALTTP_W) / 2, y0 = (H - ALTTP_H) / 2;
			for (int y = 0; y < ALTTP_H; ++y) memcpy(&altDunWide[(y0 + y) * W + x0], &altDunPx[y * ALTTP_W], ALTTP_W * 4);
			altDunFlip = !altDunFlip;
			ui_update_texture(altDunFlip ? altDunWideTexB[v] : altDunWideTex[v], altDunWide, W * 4);
			++mapWork.dungeon;
		}
	shown:
		ui_image_pixels(s, altDunFlip ? altDunWideTexB : altDunWideTex, W, H, cfg->mapScale, SHADER_ON);
	}
	else draw_alt_map(s);
	/* ALTTP-1: the items over it, sliding up from the screen's bottom or
	 * down off it at the game's speed: 8 of the items' pixels a frame of
	 * the game's (60.1 a second), whatever the display's rate */
	SDL_Rect img = ui_image_rect(s, ALTTP_W, ALTTP_H, cfg->mapScale);
	float k = img.h > 0 ? img.h / (float) ALTTP_H : 1, dist = SDL_max(1.f, (float) screens[s].rect.h - img.y);
	Uint64 now = SDL_GetPerformanceCounter();
	double dt = altSlideT ? (double) (now - altSlideT) / SDL_GetPerformanceFrequency() : 0;
	altSlideT = now;
	float to = altPage == 0 ? 1.f : 0.f, step = (float) (SDL_min(dt, 0.1) * 8 * k * 60.0988 / dist);
	altItemsUp = to > altItemsUp ? SDL_min(to, altItemsUp + step) : SDL_max(to, altItemsUp - step);
	if (altItemsUp > 0) ui_image_pixels_at(s, altItemsTex, ALTTP_W, ALTTP_H, cfg->mapScale, SHADER_ON, (int) ((1 - altItemsUp) * dist + 0.5f));
}

static void alt_sound(uint8_t id);
#define ALT_SFX_OPEN 0x11
#define ALT_SFX_CLOSE 0x12
/* ALTTP-1: X, while Link is played, switches the second screen between
 * the items and the map instead of opening the game's own map. Returns
 * the keys the game gets. */
uint32_t alt_keys(uint32_t keys) {
	if (!alt || nScreens != 2 || !alttp_playing(alt)) {
		altXDown = (keys & K_X) != 0;
		return keys;
	}
	bool x = (keys & K_X) != 0;
	if (x && !altXDown) {
		altPage ^= 1;
		if (altPage) altUser = false, altHaveView = false, altFocus = false;
		alt_sound(altPage ? ALT_SFX_CLOSE : ALT_SFX_OPEN); /* ALTTP-16 */
		fprintf(stderr, "second screen: the items %s\n", altPage ? "slide away" : "slide up");
	}
	altXDown = x;
	return keys & ~(uint32_t) K_X;
}

/* ALTTP-16: the game plays its own sound: $11 the items screen opening,
 * $12 closing, asked as the game asks (its sound request, $7E012F),
 * between frames, only when nothing is waiting there */
static void alt_sound(uint8_t id) {
	size_t n = 0;
	const uint8_t* w = emu_memory(&emu, EMU_MEM_WRAM, &n);
	if (!w || n < 0x20000 || !alttp_playing(alt) || w[0x012F]) return;
	Write out = { 0x7E012F, id };
	int nOk;
	const WriteRange* ok = alttp_write_ranges(&nOk);
	char what[64];
	snprintf(what, sizeof what, "(frame %llu) sound $%02X", (unsigned long long) frameCount, id);
	emu_apply(&emu, what, &out, 1, ok, nOk);
}

/* A tap on the second screen: on the items screen, Y item selected
 * (ALTTP-10, written to the game between frames as its own items screen
 * would: an exception listed in docs/games/a-link-to-the-past.md; with
 * RetroAchievements' hardcore it is to be switched off, ra.c plays casual
 * only for now); on the map, the next zoom (ALTTP-3) */
static void alt_tap(int x, int y) {
	if (altPage == 1 && alttp_in_special(alt)) return; /* (the logo) */
	if (altPage == 1 && !alttp_in_dungeon(alt)) {
		/* (alt_step glides there, the place tapped gliding to the middle:
		 * the owner, Nov 23, 2026; a pinch zooms round its fingers) */
		altFocusX = altVX + x * altZ, altFocusY = altVY + y * altZ, altFocus = true;
		altUser = true, altVelX = altVelY = 0;
		altZoomI = (altZoomI + ALT_ZOOMS - 1) % ALT_ZOOMS; /* (closer: the owner, Nov 24, 2026) */
		alt_sound(ALT_SFX_OPEN); /* ALTTP-16 */
		fprintf(stderr, "map zoom: %g world pixels a pixel\n", altZooms[altZoomI]);
		return;
	}
	float kx, ky;
	SDL_Rect img = alt_rect(1 - game_screen(), &kx, &ky);
	if (kx <= 0 || ky <= 0) return;
	int cx = (int) floorf((x - img.x) / kx), cy = (int) floorf((y - img.y) / ky);
	if (cx < 0 || cy < 0 || cx >= ALTTP_W || cy >= ALTTP_H) return;
	if (altPage == 1 && alttp_in_dungeon(alt)) {
		/* ALTTP-6: a floor in the list shown at the top, as Up and Down do */
		int f = alttp_dungeon_floor_at(alt, cx, cy), lo, hi, link;
		if (f != ALTTP_FLOOR_AS_GAME && alttp_dungeon_floors(alt, &lo, &hi, &link)) {
			altFloor = f == link ? ALTTP_FLOOR_AS_GAME : f;
			altFloorLink = link;
			fprintf(stderr, "dungeon map: floor %d\n", f);
		}
		return;
	}
	int item = alttp_items_hit(alt, cx, cy);
	if (!item) return;
	size_t n = 0;
	const uint8_t* w = emu_memory(&emu, EMU_MEM_WRAM, &n);
	if (!w || n < 0x20000) return;
	Write out[32];
	int k = alttp_select(alt, w, item, out, 32), nOk;
	const WriteRange* ok = alttp_write_ranges(&nOk);
	char what[96];
	snprintf(what, sizeof what, "(frame %llu) Y item %d", (unsigned long long) frameCount, item);
	if (k > 0 && emu_apply(&emu, what, out, k, ok, nOk) > 0) alt_tick(); /* (the items screen shows it now) */
	else if (!k) fprintf(stderr, "items: item %d not selected (not had, or not playable)\n", item);
}

/* A touch on the second screen: a drag moves the map with the finger (a
 * fling carries on), a tap goes to alt_tap. Never the menu. */
int alt_touch(const Pointer* p) {
	if (nScreens != 2) return 0;
	if (p->kind >= 4) {
		/* ALTTP-3: a pinch zooms round the point between the fingers,
		 * which stays under them; let go, the zoom settles on the nearest
		 * level; the finger left drags on */
		int s = 1 - game_screen();
		if (altPage != 1 || alttp_in_dungeon(alt) || alttp_in_special(alt) || !altShown || p->screen != s) return 1;
		SDL_Rect a = { 0, 0, screens[s].rect.w, screens[s].rect.h }; /* (as draw_alt_map's) */
		if (p->kind == 5) {
			if (altPinch) {
				int best = 0;
				for (int i = 1; i < ALT_ZOOMS; ++i) {
					if (fabsf(logf(altZooms[i] / altZ)) < fabsf(logf(altZooms[best] / altZ))) best = i;
				}
				altZoomI = best;
				fprintf(stderr, "map zoom: %g world pixels a pixel (pinched)\n", altZooms[altZoomI]);
			}
			altPinch = false;
			altDrag = true, altDragMoved = true, altTapOk = false;
			altDownX = p->x, altDownY = p->y, altDownVX = altVX, altDownVY = altVY;
			altPrevX = (float) p->x, altPrevY = (float) p->y, altPrevT = SDL_GetPerformanceCounter();
			altVelX = altVelY = 0;
			return 1;
		}
		if (p->span < 8) return 1;
		if (!altPinch) {
			altPinch = true, altDrag = false, altDragMoved = true, altTapOk = false;
			altPinchSpan = p->span, altPinchZ = altZ;
			altPinchWX = altVX + (p->x - a.x) * altZ, altPinchWY = altVY + (p->y - a.y) * altZ;
			altVelX = altVelY = 0;
		}
		altZ = SDL_clamp(altPinchZ * altPinchSpan / p->span, altZooms[0], altZooms[ALT_ZOOMS - 1]);
		altVX = altPinchWX - (p->x - a.x) * altZ, altVY = altPinchWY - (p->y - a.y) * altZ;
		alt_clamp(&altVX, &altVY);
		altUser = true;
		return 1;
	}
	if (p->screen == game_screen() && !(altDrag && p->kind != 1)) return 0;
	Uint64 now = SDL_GetPerformanceCounter();
	if (p->kind == 1) {
		altFocus = false; /* (a finger on the map: it's the finger's now) */
		altDrag = altShown;
		altDragMoved = false;
		altTapOk = altShown;
		altDownX = p->x, altDownY = p->y;
		altDownVX = altVX, altDownVY = altVY;
		altPrevX = (float) p->x, altPrevY = (float) p->y, altPrevT = now;
		altVelX = altVelY = 0;
		return 1;
	}
	if (!altDrag) return 1;
	float kx, ky;
	alt_rect(1 - game_screen(), &kx, &ky);
	if (kx <= 0 || ky <= 0) return 1;
	if (fabsf((p->x - altDownX) / kx) > 4 || fabsf((p->y - altDownY) / ky) > 4) altDragMoved = true;
	if (altDragMoved && altPage == 1 && !alttp_in_dungeon(alt) && !alttp_in_special(alt)) {
		altVX = altDownVX - (p->x - altDownX) * ALT_Z, altVY = altDownVY - (p->y - altDownY) * ALT_Z;
		alt_clamp(&altVX, &altVY);
		altUser = true;
		double dt = (double) (now - altPrevT) / SDL_GetPerformanceFrequency();
		if (dt > 0.004) {
			float vx = -(p->x - altPrevX) * ALT_Z / (float) dt, vy = -(p->y - altPrevY) * ALT_Z / (float) dt;
			altVelX = altVelX * 0.5f + vx * 0.5f, altVelY = altVelY * 0.5f + vy * 0.5f;
			altPrevX = (float) p->x, altPrevY = (float) p->y, altPrevT = now;
		}
	}
	if (p->kind != 2) return 1;
	altDrag = false;
	altPinch = false;
	if (!altDragMoved) {
		if (altTapOk) alt_tap(p->x, p->y);
		return 1;
	}
	if (altPage == 1 && alttp_in_dungeon(alt)) {
		/* ALTTP-6: a swipe down shows the floor above, up the one below */
		int lo, hi, link;
		if (alttp_dungeon_floors(alt, &lo, &hi, &link) && hi > lo && fabsf((p->y - altDownY) / ky) > 24) {
			int top = altFloor == ALTTP_FLOOR_AS_GAME ? (link == lo ? lo + 1 : link) : altFloor;
			top += p->y > altDownY ? 1 : -1;
			top = SDL_clamp(top, lo + 1, hi);
			altFloor = top, altFloorLink = link;
			fprintf(stderr, "dungeon map: floor %d at the top\n", top);
		}
		return 1;
	}
	if (altPage == 1) {
		if ((double) (now - altPrevT) / SDL_GetPerformanceFrequency() > 0.08) altVelX = altVelY = 0;
		altStepT = 0;
	}
	return 1;
}


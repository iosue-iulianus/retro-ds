/* One play session: the emulator, the second-screen map, and the pause menu. */
#include "game.h"
#include "i18n.h"
#include "android.h"
#include "brand.h"
#include "emu.h"
#include "mapcap.h"
#include "alttp.h"
#include "game_screens.h"
#include <zlib.h>
#include "smmap.h"
#include "hotkeys.h"
#include "paths.h"
#include "ui.h"
#include "bezel.h"
#include "ra.h"
#include "logo.h"
#include <ctype.h>
#include <dirent.h>
#include <strings.h>
#include <math.h>
#include "profiles.h"
#include "patch.h"
#include "cheats.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#ifdef __ANDROID__
#include <sys/system_properties.h>
#endif
#ifdef __linux__
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

/* Times the game thread was taken off its core (involuntary context
 * switches): another thread got it, so the frame ran late through no work
 * of ours. Linux only; 0 elsewhere. */
static long thread_preemptions(void) {
#if defined(__linux__) && defined(RUSAGE_THREAD)
	struct rusage ru;
	if (getrusage(RUSAGE_THREAD, &ru) == 0) return ru.ru_nivcsw;
#endif
	return 0;
}
static double thread_cpu_ms(void) {
	struct timespec t;
	if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t) != 0) return 0;
	return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}
/* CLOCK_MONOTONIC in seconds: the time the kernel log and the journal's
 * monotonic stamps use, so a stall can be matched with what the system did
 * (launch.sh logs both on ROCKNIX) */
static double mono_seconds(void) {
	struct timespec t;
	if (clock_gettime(CLOCK_MONOTONIC, &t) != 0) return 0;
	return t.tv_sec + t.tv_nsec / 1e9;
}
static int cpu_mhz(int cpu) {
#ifdef __linux__
	if (cpu < 0) return 0;
	char path[96];
	snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", cpu);
	FILE* f = fopen(path, "r");
	long k = 0;
	if (f) {
		if (fscanf(f, "%ld", &k) != 1) k = 0;
		fclose(f);
	}
	return (int) (k / 1000);
#else
	(void) cpu;
	return 0;
#endif
}
static int current_cpu(void) {
#if defined(__linux__) && defined(SYS_getcpu)
	unsigned cpu = 0;
	return syscall(SYS_getcpu, &cpu, NULL, NULL) == 0 ? (int) cpu : -1;
#else
	return -1;
#endif
}

/* the game's frame rate, as the core gives it (an NTSC SNES: 60.0988) */
#define SNES_FPS_NTSC 60.0988
static double game_fps(void);
#define FF_MAX_RUNS 30 /* fast-forward at Max: at most this many frames a display frame */

Config* cfg;
Emu emu;
static double game_fps(void) {
	return emu.fps > 0 ? emu.fps : SNES_FPS_NTSC;
}

/* ------------------------------------------------- frame buffer (option)
 * With frame_buffer on, the next frame is emulated on this thread while the
 * game loop draws and shows the one before: a frame that runs long (the
 * game thread kept off the CPU for a few ms, common on Android) no longer
 * misses the screen's refresh, at the cost of one frame of input delay.
 * The worker only runs emu_frame; everything else that touches the core
 * (sound, RetroAchievements, map, probes, states, cheats, the menu) stays
 * on the game loop and runs while the worker is idle: the loop waits for
 * it at the top of each iteration and starts it again just before drawing. */
static struct {
	pthread_t thread;
	pthread_mutex_t lock;
	pthread_cond_t cond;
	bool started, busy, quit;
	uint32_t keys;
} ahead = { .lock = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER };
static bool aheadInFlight; /* the worker is emulating the next frame */
static bool aheadReady;    /* it has: that frame is emulated but not yet used */
static uint32_t aheadFb[EMU_MAX_W * EMU_MAX_H]; /* the frame on screen, while the worker writes the next */

bool debug; /* RETRODS_DEBUG (declared again below) */

/* One frame of the game with these keys (on the worker too, while the main
 * thread leaves the core alone). */
static uint32_t frameKeys; /* the GBA keys of the last frame run */
/* Bug reports (the quick bar's bug button): the keys of every frame run
 * and a state a second, kept for the last ~11 seconds, so a report can
 * hold the game from 10 seconds before and every key since. bugFrame
 * counts the frames run on the game's core (run_frame, on the worker
 * too); the states are taken between frames (bug_tick). Loading a state,
 * rewinding or restarting starts the log again (bug_reset). */
#define BUG_LOG 2048    /* frames of keys kept (a power of two) */
#define BUG_SNAPS 12    /* states kept */
#define BUG_EVERY 60    /* frames between them */
#define BUG_BACK 600    /* how far back a report goes: 10 seconds */
static uint64_t bugFrame, bugStart;
static uint16_t bugKeys[BUG_LOG];
static struct {
	void* st;
	uint64_t frame;
	bool have;
} bugSnap[BUG_SNAPS];
uint64_t frameCount;

static void run_frame(uint32_t keys) {
	frameKeys = keys;
	bugKeys[bugFrame & (BUG_LOG - 1)] = keys & K_ALL;
	emu_frame(&emu, keys & K_ALL);
	++bugFrame;
}

/* Start the bug log again from here (the frames before belong to another moment). */
static void bug_reset(void) {
	bugStart = bugFrame;
	for (int i = 0; i < BUG_SNAPS; ++i) bugSnap[i].have = false;
}

/* Between frames (the worker idle): a state a second for the bug log. */
static void bug_tick(uint32_t nextKeys) {
	if (!emu.core) return;
	int oldest = 0;
	uint64_t newest = 0;
	bool any = false;
	for (int i = 0; i < BUG_SNAPS; ++i) {
		if (!bugSnap[i].have) {
			oldest = i;
			continue;
		}
		if (!any || bugSnap[i].frame > newest) newest = bugSnap[i].frame;
		any = true;
		if (bugSnap[oldest].have && bugSnap[i].frame < bugSnap[oldest].frame) oldest = i;
	}
	if (any && bugFrame < newest + BUG_EVERY) return;
	if (any && bugFrame == newest) return;
	if (!bugSnap[oldest].st) bugSnap[oldest].st = malloc(emu_state_size(&emu));
	if (!bugSnap[oldest].st) return;
	bugSnap[oldest].have = emu_state_save_live(&emu, nextKeys, bugSnap[oldest].st);
	bugSnap[oldest].frame = bugFrame;
}

static void* ahead_worker(void* arg) {
	(void) arg;
#ifdef __ANDROID__
	int nice = android_raise_thread();
#else
	int nice = 0;
#endif
	fprintf(stderr, "frame buffer: emulation thread started%s\n", nice ? " (priority raised)" : "");
	pthread_mutex_lock(&ahead.lock);
	for (;;) {
		while (!ahead.busy && !ahead.quit) pthread_cond_wait(&ahead.cond, &ahead.lock);
		if (ahead.quit) break;
		uint32_t keys = ahead.keys;
		pthread_mutex_unlock(&ahead.lock);
		run_frame(keys);
		pthread_mutex_lock(&ahead.lock);
		ahead.busy = false;
		pthread_cond_broadcast(&ahead.cond);
	}
	pthread_mutex_unlock(&ahead.lock);
	return NULL;
}

/* false: no thread (emulate inline instead) */
static bool ahead_start(uint32_t keys) {
	pthread_mutex_lock(&ahead.lock);
	if (!ahead.started) {
		ahead.quit = false;
		ahead.started = pthread_create(&ahead.thread, NULL, ahead_worker, NULL) == 0;
	}
	bool ok = ahead.started;
	if (ok) {
		ahead.keys = keys;
		ahead.busy = true;
		pthread_cond_broadcast(&ahead.cond);
	}
	pthread_mutex_unlock(&ahead.lock);
	return ok;
}

/* Wait for the frame being emulated, if any (then the core is ours again). */
static void ahead_sync(void) {
	if (!aheadInFlight) return;
	pthread_mutex_lock(&ahead.lock);
	while (ahead.busy) pthread_cond_wait(&ahead.cond, &ahead.lock);
	pthread_mutex_unlock(&ahead.lock);
	aheadInFlight = false;
	aheadReady = true;
}

static void ahead_stop(void) {
	ahead_sync();
	aheadReady = false;
	pthread_mutex_lock(&ahead.lock);
	bool started = ahead.started;
	ahead.quit = true;
	pthread_cond_broadcast(&ahead.cond);
	pthread_mutex_unlock(&ahead.lock);
	if (started) pthread_join(ahead.thread, NULL);
	ahead.started = false;
}
static MapCapture* mc;
static char subCode[9];          /* the profile in use: the game's code */
/* what the ROM is, by its CRC32 (the log, bug reports) */
static const char* dump_text(char* buf, size_t n) {
	const char* name;
	switch (profile_dump(emu.gameCode, emu.romCrc, &name)) {
	case DUMP_TESTED: snprintf(buf, n, "CRC32 %08x, the dump SNES DS is tested on (%s)", emu.romCrc, name); break;
	case DUMP_OFFICIAL: snprintf(buf, n, "CRC32 %08x, an official dump SNES DS isn't tested on (%s)", emu.romCrc, name); break;
	case DUMP_MOD_TESTED: snprintf(buf, n, "CRC32 %08x, a mod SNES DS is tested on (%s)", emu.romCrc, name); break;
	case DUMP_MOD: snprintf(buf, n, "CRC32 %08x, a known mod SNES DS isn't tested on (%s)", emu.romCrc, name); break;
	case DUMP_MODIFIED:
		snprintf(buf, n, "CRC32 %08x, not a known dump of %.8s: modified (a patch or hack), trimmed or a bad dump", emu.romCrc,
		         emu.gameCode);
		break;
	default: snprintf(buf, n, "CRC32 %08x, not checked (no list of %.8s's dumps)", emu.romCrc, emu.gameCode); break;
	}
	/* a patch file beside the ROM (GEN-9): which, and what the file was before it */
	size_t l = strlen(buf);
	if (emu.patch == PATCH_APPLIED || emu.patch == PATCH_PARTIAL) {
		const char* fname = NULL;
		DumpKind fk = profile_dump(emu.gameCode, emu.fileCrc, &fname);
		snprintf(buf + l, n - l, "; patched as it started with %s, the ROM file itself CRC32 %08x (%s)", emu.patchName,
		         emu.fileCrc,
		         fk == DUMP_TESTED     ? "the tested dump"
		         : fk == DUMP_OFFICIAL ? "an official dump"
		         : fk == DUMP_MODIFIED ? "not a known dump"
		                               : "a known mod");
		l = strlen(buf);
		if (emu.patch == PATCH_PARTIAL)
			snprintf(buf + l, n - l, "; not applied: %s (made for another ROM, or for the ROM before the patches above it, or can't be read)",
			         emu.patchSkipped);
	} else if (emu.patch == PATCH_WRONG_ROM) {
		snprintf(buf + l, n - l, "; %s beside it was made for another ROM, not applied", emu.patchName);
	} else if (emu.patch == PATCH_UNREADABLE) {
		snprintf(buf + l, n - l, "; %s beside it can't be read, not applied", emu.patchName);
	}
	return buf;
}
GamePaths gp;

static SDL_Texture* gameTex[2];
/* The game's picture: texW x texH as the core draws it (made again when
 * that changes), shown as picW x picH: the SNES screen, 256 wide (hi-res
 * pictures are 512 wide) and 224 or 239 tall (interlaced ones twice that). */
static int texW, texH;
int picW = SNES_W, picH = SNES_H;
static void game_tex_fit(int w, int h) {
	if (w <= 0 || h <= 0 || (w == texW && h == texH && gameTex[0])) return;
	for (int i = 0; i < nViews; ++i) {
		if (gameTex[i]) SDL_DestroyTexture(gameTex[i]);
		gameTex[i] = SDL_CreateTexture(views[i].ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, w, h);
		if (gameTex[i]) SDL_SetTextureBlendMode(gameTex[i], SDL_BLENDMODE_NONE);
	}
	texW = w, texH = h;
	picW = SNES_W;
	picH = h > 240 ? h / 2 : h;
	if (debug) fprintf(stderr, "frame %llu: picture %dx%d, shown as %dx%d\n", (unsigned long long) frameCount, w, h, picW, picH);
}
/* A w x h picture into an SNES_W x SNES_H one: hi-res and interlaced
 * pictures halved, a taller one cut, a smaller one black around */
static void pic_copy(uint32_t* out, const uint32_t* fb, int w, int h) {
	int sx = w > SNES_W ? w / SNES_W : 1, sy = h > 240 ? 2 : 1;
	for (int y = 0; y < SNES_H; ++y) {
		for (int x = 0; x < SNES_W; ++x) {
			int fx = x * sx, fy = y * sy;
			out[y * SNES_W + x] = fx < w && fy < h ? fb[fy * w + fx] : 0xFF000000u;
		}
	}
}
static SDL_Texture* mapTex[2];
static SDL_Texture* logoTex[2];
/* the logo centred in a GBA-sized picture, so it's scaled (and gridded)
 * like the game; NULL for a user's high-resolution logo */
static SDL_Texture* logoCanvasTex[2];
/* user overlay PNGs: [0] top screen, [1] bottom (load_overlay) */
static SDL_Texture* overlayTex[2][2];
static int overlayW[2], overlayH[2];
static char overlayMode[2][32];
/* SNES DS's own bezel (draw_bezel, bezel.c): [0] top screen, [1] bottom,
 * made again when anything it's drawn from changes */
static SDL_Texture* bezelTex[2][2];
static int bezelW[2], bezelH[2];
static char bezelKey[2][96];
static int logoW, logoH;
static bool logoPixel;
static int logoSub = 1; /* logoTex's pixels per GBA pixel (LOGO_SUB: a scraped logo at full resolution) */
static bool logoProvisional; /* a scraped logo sized before the extracted one existed (GEN-11) */
static MapResult mapRes;
static int mapW = SNES_W, mapH = SNES_H;
static int mapShownIdx = -1;
/* "Map Animations: Off" shows this composed still instead of mapRes.frames[] */
#define STILL_IDX MAP_MAX_FRAMES
static uint32_t* stillPx;        /* mapW * mapH */
static int stillW, stillH;
static bool stillValid;
/* The map button did nothing at the last attempt (A Link to the Past indoors
 * without a map): keep showing the last map, dimmed. */
static bool mapInactive;
static bool logoOnly; /* the game has a profile but no map: logo on the second screen */
static const MapProfile* snapProf; /* logo-only game with screens copied to the second screen */
/* the profile's logoIf probes match: the logo, the map picture and HUD kept */
static bool logoForced;
bool hudHide; /* the profile's hide rectangles are covered now (SM-6) */
/* SM-7, touch on the second screen's map: the view asked for (page 0 the
 * map, 1 the second page; the map scrolled dx, dy SNES pixels from where
 * the game centres it), and a drag under way (screen pixels) */
static int viewPage, viewDx, viewDy;
static bool mapDrag;
static int dragX0, dragY0, dragX, dragY;
static uint32_t movedKey;
static bool mapReupload; /* upload the shown map frame again (its window moved) */
static void map_reshow(void);
static bool stitch_on(void);
/* logoIf just stopped matching: the logo still, until the second screen has
 * a new picture to show (a HUD picture drawn, a map picture taken), at
 * most a second; not the last picture from before for a frame (SM4-11) */
static int logoHolding;
static void upload_map_frame(int idx);
static const uint32_t* map_px(int idx);
static bool logo_draw(uint32_t* px, int cw, int ch, int k, int top, int alpha);
static void logo_refresh(void);
static int snapShow, snapRevert;   /* consecutive frames matching show / a revert screen */
static uint32_t snapShown[SNES_W * SNES_H]; /* the copy on the second screen (kept with save states), SNES_W x SNES_H */

static uint64_t mapAnimStart;
/* which of the map's frames shows now (they turn every frameStep frames
 * from mapAnimStart); never out of range, even with mapAnimStart ahead of
 * frameCount (it once came from the game played before: frames[-1]) */
static int map_anim_index(void) {
	if (mapRes.nFrames < 1 || frameCount < mapAnimStart) return 0;
	return (int) (((frameCount - mapAnimStart) / (uint64_t) SDL_max(1, mapRes.frameStep)) % (uint64_t) mapRes.nFrames);
}
static bool singleShowsMap;
static double fps, speedPct;
static Uint64 emuTicks, drawTicks; /* time spent emulating / drawing, for the 5 s report */

/* Slow frames (work over a frame's time): the worst each second, broken down,
 * logged on Android (where frame time is tight) or with RETRODS_DEBUG. */
typedef struct {
	double total, input, emulate, ra, map, draw, top, bottom;
	int redraws;

	unsigned long long frame;
	long preempted; /* times the thread was put off its core during the frame */
	int cpu, mhz;   /* where it ended, and that core's clock then */
	double cpuMs;   /* time the thread actually ran (the rest it waited) */
	double waitEmu, waitDraw; /* of the waiting: during emulation, during drawing */
	double audio;   /* of emulating: handing the sound to SDL (its lock is shared with the audio thread) */
} SlowFrame;
static SlowFrame slowWorst;
static int slowCount;

/* Stalls (issue #50): a loop iteration over STALL_MS from top to top, any
 * build, no RETRODS_DEBUG needed. Each part of the iteration is timed, the
 * wait before the frame and the sleep after it included (slow frames above
 * only see the work in between), so one line in a player's log says where
 * the time went. At most STALL_LINES lines per 5 s; the 5 s report counts
 * them all. */
#define STALL_MS 100
#define STALL_LINES 4
typedef struct {
	Uint64 top, synced, work, emu0, emu1, draw0, draw1, sleep0, end;
	double sleepWant;  /* s */
	double audio;      /* ms handing sound to SDL */
	double presentTop, presentBottom;
	double cpuMs;      /* the thread's CPU time at the top */
	long preempt;      /* its involuntary switches at the top */
	int queueMs;       /* the sound queue at the top */
	unsigned long long frame;
	int kept;          /* frames the keeper ran during the present */
	bool valid;
} LoopMarks;
static LoopMarks loopCur;
static int stallCount, stallLogged;
static unsigned keptTotal; /* frames the keeper ran (the 5 s report) */
static double stallWorst;

static void slow_frame_note(SlowFrame f) {
	++slowCount;
	if (f.total > slowWorst.total) slowWorst = f;
}

/* The second screen's GPU work in the last second, for the slow frames'
 * line (A Link to the Past's map: what it sent to the GPU) */
MapWork mapWork;

/* A redraw of the map begins: the last one's count of pieces kept if the most */
void map_work_frame(void) {
	if (mapWork.drawn > mapWork.mostDrawn) mapWork.mostDrawn = mapWork.drawn;
	mapWork.drawn = 0;
}

static void slow_frame_report(bool debug) {
	char work[160] = "";
	if (mapWork.on) {
		snprintf(work, sizeof work, "; this second the map sent %d piece(s) (%d new texture(s), %d dropped), the dungeon map %d time(s), drew %d piece(s) at most a frame",
		         mapWork.uploads, mapWork.made, mapWork.dropped, mapWork.dungeon, mapWork.mostDrawn);
	}
	memset(&mapWork, 0, sizeof mapWork);
#ifndef __ANDROID__
	if (!debug) {
		slowCount = 0;
		slowWorst.total = 0;
		return;
	}
#endif
	if (!slowCount) return;
	const SlowFrame* w = &slowWorst;
	fprintf(stderr,
	        "slow frames: %d in the last second; worst (frame %llu) %.1f ms: input/menu %.1f, emulating %.1f (sound hand-off %.1f), "
	        "RetroAchievements %.1f, map %.1f, drawing %.1f (top screen %s %.1f, bottom %s %.1f); "
	        "ran %.1f ms of it (waited %.1f: %.1f while emulating, %.1f while drawing), preempted %ld time(s), on cpu %d at %d MHz%s\n",
	        slowCount, w->frame, w->total, w->input, w->emulate, w->audio, w->ra, w->map, w->draw,
	        w->redraws & 1 ? "redrawn" : "kept", w->top, w->redraws & 2 ? "redrawn" : "kept", w->bottom, w->cpuMs, w->total - w->cpuMs, w->waitEmu, w->waitDraw, w->preempted, w->cpu, w->mhz, work);
	slowCount = 0;
	slowWorst.total = 0;
}
bool debug;

/* ---------------------------------------------------------------- menu */

enum MenuItem { M_RESUME, M_CHEATS, M_PATCHES, M_ACH, M_SHOT, M_SWAP, M_SCALE, M_LCD, M_OVERLAY, M_FPS, M_SAVE, M_LOAD, M_RESTART, M_LIBRARY, M_EXIT, M_FRAMEBUF, M_LOGO, M_MATCH, M_REWIND, M_AUTOSAVE, M_AUTOLOAD, M_BUG, M_SLOT, M_SHAPES, M_FF, M_COLOR, M_GHOST, M_BUTTONS, M_PIXLOGO, M_PLACE };
static const char* menuLabels[] = {
	/* title case: drawn in Futura from the built-in images (tools/make_brand.py) */
	[M_RESUME] = N_("Resume"), [M_SWAP] = N_("Swap Screens"), [M_SAVE] = N_("Save State"), [M_SCALE] = "", [M_LCD] = "", [M_FPS] = "", [M_FRAMEBUF] = "", [M_OVERLAY] = "", [M_LOGO] = "", [M_MATCH] = "", [M_REWIND] = "", [M_RESTART] = N_("Restart Game"),
	[M_LOAD] = N_("Load State"), [M_CHEATS] = "Cheats", [M_PATCHES] = "Patches", [M_BUTTONS] = N_("Buttons"), [M_ACH] = "Achievements", [M_SHOT] = N_("Take Screenshot"), [M_LIBRARY] = N_("Close Game"), [M_EXIT] = N_("Close SNES DS"),
	[M_AUTOSAVE] = "", [M_AUTOLOAD] = "", [M_BUG] = N_("Submit a Bug Report"), [M_SLOT] = "", [M_FF] = "", [M_COLOR] = "", [M_GHOST] = "", [M_PIXLOGO] = "", [M_PLACE] = "",
};
static int menuItems[12], nMenuItems;
/* Some buttons are half as wide, two to a row (Save State | Load State):
 * each item's row and side (-1: the whole row, 0 left, 1 right). */
static int itemRow[12], itemSide[12], nMenuRows;
static bool half_item(int item) {
	return item == M_RESUME || item == M_RESTART || item == M_SAVE || item == M_LOAD || item == M_AUTOSAVE || item == M_AUTOLOAD || item == M_REWIND || item == M_FF ||
	       item == M_COLOR || item == M_GHOST || item == M_CHEATS || item == M_PATCHES || item == M_SHOT || item == M_BUG;
}
/* Close Game and Close SNES DS are double height: two rows tall, side by side
 * when both are there, else the one across the whole row */
static bool tall_item(int item) {
	return item == M_LIBRARY || item == M_EXIT;
}
static int item_span(int item) {
	return tall_item(item) ? 2 : 1;
}
/* rows, sides and the row count for a page's items (two half items in a row share it; a tall one takes two rows) */
static int layout_rows(const int* items, int n, int* row, int* side) {
	int rows = 0;
	for (int i = 0; i < n; ++i) {
		bool both = i + 1 < n && ((half_item(items[i]) && half_item(items[i + 1])) || (tall_item(items[i]) && tall_item(items[i + 1])));
		if (both) {
			row[i] = row[i + 1] = rows;
			rows += item_span(items[i]);
			side[i] = 0, side[i + 1] = 1;
			++i;
		} else {
			row[i] = rows;
			rows += item_span(items[i]);
			side[i] = -1;
		}
	}
	return rows;
}
/* Six pages, switched with L1/R1 or the tabs: Game holds buttons
 * (actions); the other five are rows of settings (docs/MENUS.md), the same
 * categories as the game list's Settings, each changed for this game (A) or
 * for all games (Y), kept in states/<game>.cfg or states/prefs.cfg. */
#define N_PAGES 6
enum { PG_GAME, PG_DISPLAY, PG_SCREENS, PG_CONTROLS, PG_SPEED, PG_MAP };
#define ICON_SYSTEM 100 /* a toolbox: Settings > System (not a page) */
static int pages[N_PAGES][12], pageLen[N_PAGES], curPage;
#define ROW_MAX 16 /* settings on a page */
#define ROW_VIS 8  /* most shown at once: more scroll */
static const char* rowKeys[N_PAGES][ROW_MAX]; /* a settings page's rows: the settings' keys, or "@name" for a button that opens something */
static int rowN[N_PAGES];
static int rowSel;     /* 0: the This game / All games bar; 1...: the rows */
static int rowTop;     /* the first row shown (0-based; the rows before it are scrolled off) */
static bool scopeAll;  /* the bar: changes go to all games */
static int rowPressed = -1;
static int rowDragY;       /* where a finger last moved the list from */
static bool rowDragged;    /* the finger scrolled the list: not a tap */
static bool page_is_rows(int p) {
	return p >= PG_DISPLAY;
}
/* Buttons still held when the menu closes (or after a HOME combo) are kept
 * from the game until released, so e.g. the A that picked "Save state"
 * doesn't make Samus jump. */
static bool suppress[B_COUNT];
#define HELD(b) (btnNow[b] && !suppress[b])
/* the hotkeys (hotkeys.c): the hotkey button, each action's button or -1 */
static int hkMod = B_HOME;
static int hkBtn[HK_COUNT];

static void suppress_held(void) {
	for (int b = 0; b < B_COUNT; ++b) suppress[b] = btnNow[b];
}
static const char* pageNames[N_PAGES] = { "Game", N_("Display"), "Screens", "Controls", N_("Speed"), N_("Map")};
/* Restart Game, Close Game and Close SNES DS ask first: chosen once, the
 * button says "Are You Sure?" (and the note what happens); chosen again,
 * it's done. Another page, another button or closing the menu calls it off. */
static int armedItem = -1;
static void rebuild_textures(void);
static void commit_current(const char* key);
static bool confirm_item(int item) {
	return item == M_RESTART || item == M_LIBRARY || item == M_EXIT;
}
static bool menuOpen;
static int menuSel;
static int menuPressed = -1;      /* item under a finger that went down in the menu */
static bool ignorePointerUp;      /* the tap that opened the menu */
static char menuNote[64];
static bool pacedFallback; /* Match Display Rate fell back to the game's rate (pacing_update) */

#define C_BTN ui_accent_color(AC_BTN)
#define C_BTN_SEL ui_accent_color(AC_BTN_SEL)
#define C_BTN_EDGE ui_accent_color(AC_EDGE)
static const SDL_Color C_TEXT = { 236, 236, 244, 255 };
static const SDL_Color C_DIS = { 100, 100, 120, 255 };
#define C_GOLD ui_accent_color(AC_HI)

static Cheats cheats;
static SDL_Rect tab_rect(int scr, int t);
static SDL_Rect ach_area(int scr);

/* Every label a pause-menu button can show, for sizing the text so the
 * widest fits (item_label gives the current one). */
static const char* const allLabels[] = {
	N_("Resume"), N_("Restart Game"), N_("Are You Sure?"), N_("Save State"), N_("Load State"), "Cheats", "Patches", "Achievements",
	N_("Take Screenshot"), N_("Close Game"), N_("Close SNES DS"), N_("Submit a Bug Report"),
	"\u2039   Slot 0: Sep 30, 12:59 PM   \u203A", /* (the slot row, with its arrows) */
};
/* the labels of the half-width buttons, which fit in half the width */
static const char* const halfLabels[] = {
	N_("Save State"), N_("Load State"), "Save State 0", "Load State 0", N_("Resume"), N_("Restart Game"), N_("Are You Sure?"),
	"Cheats", "Patches", N_("Take Screenshot"), N_("Submit a Bug Report"),
};

/* The pause menu's layout: buttons across the whole screen (a small margin
 * at the edges), all the same size on every page, as tall as the page with
 * the most buttons allows (with the page tabs above and a line for notes
 * below), and the text as big as the button height and the widest label
 * allow. */
typedef struct {
	int margin, w, h, gap; /* buttons: x = margin, width w, height h, gap between them */
	int tabY, top;         /* the tab row's y; the first button's y */
	int cap, tabCap, noteCap, pad, rad;
	int prompt;            /* L1 / R1 prompt width, beside the tabs */
} MenuGeom;

static MenuGeom menu_geom(int scr) {
	static MenuGeom cache;
	static int cw = -1, ch = -1, cn = -1;
	SDL_Rect r = screens[scr].rect;
	int n = 1;
	for (int p = 0; p < N_PAGES; ++p) {
		int row[12], side[12];
		n = SDL_max(n, page_is_rows(p) ? SDL_min(rowN[p], ROW_VIS) + 1 : SDL_min(layout_rows(pages[p], pageLen[p], row, side), ROW_VIS));
	}
	if (r.w == cw && r.h == ch && n == cn) return cache;
	MenuGeom g;
	float px = ui_px(scr);
	g.margin = px * 4;
	g.w = r.w - g.margin * 2;
	/* margin, tab row (h), gap (h * 0.4), n rows (h) with gaps (h * 0.2),
	 * the note line (h * 0.7), margin */
	g.h = (int) ((r.h - g.margin * 2) / (1.0 + 0.4 + n + 0.2 * (n - 1) + 0.7));
	g.gap = g.h / 5;
	g.tabY = g.margin;
	g.top = g.tabY + g.h + g.h * 2 / 5;
	g.pad = g.h / 3;
	g.rad = SDL_max(2, g.h / 5);
	/* text: capitals at most 40% of the button, and the widest label fits */
	int ref = 100, widest = 1;
	for (size_t i = 0; i < sizeof(allLabels) / sizeof(*allLabels); ++i)
		widest = SDL_max(widest, ui_label_width(scr, ref, allLabels[i]));
	g.cap = SDL_min(g.h * 2 / 5, (g.w - g.pad * 2) * ref / widest);
	widest = 1;
	for (size_t i = 0; i < sizeof(halfLabels) / sizeof(*halfLabels); ++i)
		widest = SDL_max(widest, ui_label_width(scr, ref, halfLabels[i]));
	for (int k = 0; k < 2; ++k) { /* (the slot buttons: "Save State 0", "Load State 0" in the language's words) */
		char b[96];
		snprintf(b, sizeof(b), k ? tr("Load State %d") : tr("Save State %d"), 0);
		widest = SDL_max(widest, ui_label_width(scr, ref, b));
	}
	g.cap = SDL_min(g.cap, ((g.w - g.gap) / 2 - g.pad * 2) * ref / widest);
	g.noteCap = g.cap * 4 / 5;
	/* the tabs share the row between the L1 / R1 prompts, each as wide as its name needs */
	g.prompt = g.h * 9 / 10;
	int tabsW = g.w - (g.prompt + g.gap) * 2 - g.gap * (N_PAGES - 1), names = 0;
	for (int t = 0; t < N_PAGES; ++t) names += ui_label_width(scr, ref, pageNames[t]);
	/* each tab: its name plus pad on both sides */
	g.tabCap = SDL_min(g.cap, (tabsW - g.pad * 2 * N_PAGES) * ref / SDL_max(1, names));
	g.rad = SDL_max(2, g.h / 5);
	cache = g, cw = r.w, ch = r.h, cn = n;
	return g;
}

/* The pause menu's save slot (0-9): Save State and Load State use it.
 * At start, the slot saved to last (or 0). */
static int stateSlot;
static void slot_pick_newest(void) {
	time_t best = 0;
	stateSlot = 0;
	for (int i = 0; i < 10; ++i) {
		struct stat st;
		if (stat(gp.slot[i], &st) == 0 && st.st_mtime > best) best = st.st_mtime, stateSlot = i;
	}
}
/* What's in a slot shared with RetroArch (issue #39), looked at again
 * only when the file changes */
static StateFileKind slot_kind(const struct stat* st, bool* fromSnesds) {
	static struct {
		char path[1024];
		time_t time;
		off_t size;
		StateFileKind kind;
		bool snesds;
	} seen[10];
	int i = stateSlot;
	if (strcmp(seen[i].path, gp.slot[i]) || seen[i].time != st->st_mtime || seen[i].size != st->st_size) {
		snprintf(seen[i].path, sizeof(seen[i].path), "%s", gp.slot[i]);
		seen[i].time = st->st_mtime, seen[i].size = st->st_size;
		seen[i].kind = emu_state_file_kind(&emu, gp.slot[i], &seen[i].snesds);
	}
	*fromSnesds = seen[i].snesds;
	return seen[i].kind;
}
/* "Slot 3: Oct 31, 1:45 PM" (when it was saved), or "Slot 3: Empty";
 * shared with RetroArch, also who saved it: "Slot 3: Oct 31, 1:45 PM, RetroArch",
 * "Slot 3: Another emulator's" */
static const char* slot_label(void) {
	static char buf[80];
	struct stat st;
	if (stat(gp.slot[stateSlot], &st) != 0) {
		snprintf(buf, sizeof(buf), tr("Slot %d: Empty"), stateSlot);
		return buf;
	}
	const char* by = "";
	if (gp.raDir[0]) {
		bool fromSnesds;
		StateFileKind k = slot_kind(&st, &fromSnesds);
		if (k == STATE_FILE_OTHER_CORE || k == STATE_FILE_UNREADABLE) {
			snprintf(buf, sizeof(buf), tr("Slot %d: %s"), stateSlot, k == STATE_FILE_OTHER_CORE ? tr("Another emulator's"): tr("Can't be read"));
			return buf;
		}
		if (!fromSnesds) by = N_(", RetroArch");
	}
	struct tm t, now;
	time_t n = time(NULL);
	localtime_r(&st.st_mtime, &t);
	localtime_r(&n, &now);
	static const char* const mon[12] = { N_("Jan"), N_("Feb"), N_("Mar"), N_("Apr"), N_("May"), N_("Jun"), N_("Jul"), N_("Aug"), N_("Sep"), N_("Oct"), N_("Nov"), N_("Dec")};
	if (t.tm_year != now.tm_year) snprintf(buf, sizeof(buf), tr("Slot %d: %s %d, %d%s"), stateSlot, mon[t.tm_mon], t.tm_mday, t.tm_year + 1900, by);
	else snprintf(buf, sizeof(buf), tr("Slot %d: %s %d, %d:%02d %s%s"), stateSlot, mon[t.tm_mon], t.tm_mday, (t.tm_hour + 11) % 12 + 1, t.tm_min, t.tm_hour < 12 ? tr("AM"): tr("PM"), by);
	return buf;
}
static void slot_step(int d) {
	stateSlot = (stateSlot + d + 10) % 10;
	menuNote[0] = 0;
}

static bool item_enabled(int item) {
	if (item == M_CHEATS) return cheats.n > 0;
	if (item == M_SHAPES) return ui_screens_differ(); /* (two screens of a shape: nothing to match) */
	if (item == M_PLACE) return nScreens == 2;         /* (one screen: no gap) */
	return item != M_LOAD || file_exists(gp.slot[stateSlot]);
}

/* ---------------------------------------------------------------- cheats screen
 *
 * "Cheats" on the Game page opens a list of the game's cheats with an On/Off
 * pill each: D-pad / A to toggle, L/R a page at a time, B (or the Back box)
 * back to the menu. Touch: tap a cheat to toggle it, drag to scroll. */
static bool cheatsOpen;
/* "Achievements" (RetroAchievements signed in): the list from ra.c, under
 * the same Back / title boxes. D-pad scrolls, L/R a page, B back. */
static bool achOpen;
static int achPressY, achPressScroll;
static bool achPressed;
static int cheatSel;                 /* row index */
static int cheatScroll;              /* pixels */
static int cheatRows[CHEAT_MAX_FILES + 1024], nCheatRows; /* >= 0 cheat index, < 0 file header (-1 - file) */
static int cheatPressRow = -1, cheatPressY, cheatPressScroll;
static bool cheatDragging;

static void build_cheat_rows(void) {
	nCheatRows = 0;
	for (int f = 0; f < cheats.nFiles; ++f) {
		if (cheats.nFiles > 1) cheatRows[nCheatRows++] = -1 - f;
		for (int i = 0; i < cheats.n && nCheatRows < (int) SDL_arraysize(cheatRows); ++i) {
			if (cheats.items[i].file == f) cheatRows[nCheatRows++] = i;
		}
	}
}

typedef struct {
	float px;
	int tpx, top, rowH, gap, x, w, listH, footY;
} CheatGeom;

static CheatGeom cheat_geom(int scr) {
	CheatGeom g;
	SDL_Rect r = screens[scr].rect;
	g.px = ui_px(scr);
	g.tpx = SDL_max(2, g.px * 3 / 4);
	g.top = g.px * 26;
	g.rowH = g.tpx * 7 + g.px * 5;
	g.gap = g.px * 2;
	g.x = g.px * 4; /* the pause menu's margin */
	g.w = r.w - g.px * 8;
	g.footY = r.h - g.tpx * 9 * 2 - g.px * 5;
	g.listH = g.footY - g.px * 6 - g.top;
	return g;
}

static int cheat_max_scroll(const CheatGeom* g) {
	return SDL_max(0, nCheatRows * (g->rowH + g->gap) - g->gap - g->listH);
}

static void cheat_keep_visible(int scr) {
	CheatGeom g = cheat_geom(scr);
	int y = cheatSel * (g.rowH + g.gap);
	if (y < cheatScroll) cheatScroll = y;
	if (y + g.rowH > cheatScroll + g.listH) cheatScroll = y + g.rowH - g.listH;
	cheatScroll = SDL_clamp(cheatScroll, 0, cheat_max_scroll(&g));
}

static void cheat_move(int dir) {
	int i = cheatSel;
	for (int k = 0; k < nCheatRows; ++k) {
		i += dir;
		if (i < 0 || i >= nCheatRows) return;
		if (cheatRows[i] >= 0) {
			cheatSel = i;
			cheat_keep_visible(ui_touch_screen());
			return;
		}
	}
}

static void open_cheats(void) {
	build_cheat_rows();
	cheatsOpen = true;
	cheatSel = 0;
	cheatScroll = 0;
	if (nCheatRows && cheatRows[0] < 0) cheat_move(1);
	cheatPressRow = -1;
}

static void toggle_cheat(int row) {
	if (row < 0 || row >= nCheatRows || cheatRows[row] < 0) return;
	Cheat* c = &cheats.items[cheatRows[row]];
	c->on = !c->on;
	cheats_apply(&cheats, &emu);
	cheats_save(&cheats);
}

/* row under screen-local y, or -1 */
static int cheat_row_at(int scr, int x, int y) {
	CheatGeom g = cheat_geom(scr);
	if (y < g.top || y >= g.top + g.listH || x < g.x || x >= g.x + g.w) return -1;
	int ly = y - g.top + cheatScroll;
	int row = ly / (g.rowH + g.gap);
	if (ly % (g.rowH + g.gap) >= g.rowH || row >= nCheatRows) return -1;
	return row;
}

/* fits s into maxChars, ending in "..." when cut */
/* s in Jost, cut short with "..." if it's wider than maxW. */
static void fit_text(char* out, size_t size, int font, int capPx, const char* s, int maxW) {
	snprintf(out, size, "%s", s);
	if (ui_font_width(font, capPx, out) <= maxW) return;
	size_t n = strlen(out);
	while (n > 0) {
		do --n; while (n > 0 && ((unsigned char) out[n] & 0xC0) == 0x80); /* whole UTF-8 characters */
		snprintf(out, size, "%.*s...", (int) n, s);
		if (ui_font_width(font, capPx, out) <= maxW) return;
	}
}

static void draw_cheats(int scr) {
	CheatGeom g = cheat_geom(scr);
	SDL_Rect r = screens[scr].rect;
	float px = g.px;
	int rad = menu_geom(scr).rad, cap = menu_geom(scr).cap;
	/* top: Back box (where the Game tab is) and the title */
	SDL_Rect b = tab_rect(scr, 0), t = tab_rect(scr, 1);
	ui_fill_round(scr, b, C_BTN, rad);
	ui_label(scr, b.x + b.w / 2, b.y + b.h / 2, cap, "Back", C_TEXT);
	ui_fill_round(scr, t, C_BTN_EDGE, rad);
	ui_label(scr, t.x + t.w / 2, t.y + t.h / 2, cap, "Cheats", (SDL_Color) { 20, 16, 36, 255 });
	SDL_Rect clip = { screens[scr].rect.x, screens[scr].rect.y + g.top, r.w, g.listH };
	ui_clip(scr, &clip);
	int tcap = g.tpx * 5; /* Jost capitals, about the pixel font's height */
	int pillW = ui_label_width(scr, tcap, "Off") + px * 8;
	int nameW = g.w - pillW - px * 10;
	for (int i = 0; i < nCheatRows; ++i) {
		int y = g.top + i * (g.rowH + g.gap) - cheatScroll;
		if (y + g.rowH <= g.top || y >= g.top + g.listH) continue;
		char line[300];
		if (cheatRows[i] < 0) {
			const CheatFile* f = &cheats.files[-1 - cheatRows[i]];
			fit_text(line, sizeof(line), TF_BOLD, tcap, f->label, g.w - px * 4);
			ui_font_text(scr, TF_BOLD, g.x + px * 2, y + g.rowH / 2, tcap, line, C_GOLD, -1);
			continue;
		}
		const Cheat* c = &cheats.items[cheatRows[i]];
		bool sel = i == cheatSel;
		SDL_Rect row = { g.x, y, g.w, g.rowH };
		ui_key(scr, row, rad, sel ? KEY_SEL : KEY_PLAIN);
		fit_text(line, sizeof(line), TF_BODY, tcap, c->desc, nameW);
		ui_font_text(scr, TF_BODY, g.x + px * 4, y + g.rowH / 2, tcap, line, C_TEXT, -1);
		SDL_Rect pill = { g.x + g.w - pillW - px * 3, y + px * 2, pillW, g.rowH - px * 4 };
		ui_fill_round(scr, pill, c->on ? (SDL_Color) { 64, 168, 112, 255 } : (SDL_Color) { 60, 56, 84, 255 }, px * 2);
		ui_label(scr, pill.x + pill.w / 2, pill.y + pill.h / 2, tcap, c->on ? "On" : "Off",
		         c->on ? C_TEXT : (SDL_Color) { 160, 156, 184, 255 });
	}
	ui_clip(scr, NULL);
	/* scroll bar */
	int maxS = cheat_max_scroll(&g);
	if (maxS > 0) {
		int total = g.listH + maxS;
		int bh = SDL_max(px * 6, g.listH * g.listH / total);
		int by = g.top + (g.listH - bh) * cheatScroll / maxS;
		ui_fill_round(scr, (SDL_Rect) { r.w - px * 4, by, px * 2, bh }, C_BTN_EDGE, px);
	}
	/* footer: the selected cheat's whole name when the row had to cut it */
	if (cheatSel >= 0 && cheatSel < nCheatRows && cheatRows[cheatSel] >= 0 &&
	    ui_font_width(TF_BODY, tcap, cheats.items[cheatRows[cheatSel]].desc) > nameW) {
		ui_font_wrap(scr, TF_BODY, px * 6, g.footY - tcap / 2, r.w - px * 12, tcap, cheats.items[cheatRows[cheatSel]].desc,
		             C_GOLD);
	}
	char count[48];
	snprintf(count, sizeof(count), tr("%d on"), cheats_on(&cheats));
	ui_font_text(scr, TF_BODY, r.w - px * 4, r.h - g.tpx * 5, tcap, count, C_DIS, 1);
}

/* ---------------------------------------------------------------- patches screen (GEN-9)
 *
 * "Patches" on the Extras page: the game's patches folder's files with an
 * On/Off pill each, the file beside the ROM (always on, applied first), and
 * two switches for the patched game's own battery save and states. The ones
 * on come first, numbered in the order they're applied; X (or a tap on the
 * number) picks one up to move it. Switching a patch asks first: A again
 * restarts the game with the change, Y keeps it for later, B calls it off;
 * while the choice isn't what the game started with, a "Restart" row at the
 * top does it. Each patch is tried on the ROM as the list opens, and the
 * ones on are tried together, in order, so the list can say which can't
 * follow which (a UPS, BPS or xdelta is made for the unchanged ROM) and which
 * change the same bytes. */
const char* romPath; /* (the game's ROM; set by game_run) */
static PatchChoice patchChoice;  /* as chosen (saved as it changes) */
static PatchChoice patchStarted; /* as the game started */
static char patchDir[1024];
static bool patchReload; /* the game restarted for a patch switched on or off: no auto-load */
static bool patchReloadNext;
/* (GEN-9) a change that can't upset the saves: the moment saved here before
 * the game reopens with it, and loaded straight back, so it carries on */
static bool patchHandoffNext;
static char patchHandoff[1100];
enum { PR_RESTART, PR_SAVE, PR_STATES, PR_HEADER, PR_SUMMARY, PR_BESIDE, PR_PATCH, PR_EMPTY };
/* what each file does to the ROM on its own (worked out as the list opens) */
typedef struct {
	uint32_t from, to; /* [from, to) */
} ByteRange;
typedef struct {
	char file[256];
	char note[300];
	bool usable;       /* applies to this ROM on its own */
	ByteRange* ranges; /* (usable) the bytes it changes */
	int nRanges;
	/* a patch SNES DS knows (docs/patches.md): the ROMs it's made for, and
	 * what to say when it goes on one of them (an optional patch: on
	 * another patch's result, found when they're tried together) */
	const KnownPatch* known;
	uint32_t bases[8];
	int nBases;
	bool baseOk; /* made for this ROM (or for any of its release's) */
	char okNote[300];
} PatchInfo;
#define PATCH_ROWS 70
static PatchInfo patchInfo[PATCH_ROWS];
static int nPatchInfo;
static bool patchBesideAt0; /* patchInfo[0] is the file beside the ROM */
typedef struct {
	int kind;
	char file[256]; /* (PR_PATCH, PR_BESIDE) its file name */
	char label[300];
	char note[600]; /* the footer's text when it's selected */
	bool usable;    /* (PR_PATCH) can be switched on */
	int order;      /* (PR_PATCH) its place among the ones on, from 0, or -1 */
	int chain;      /* (on) 0 applies, 1 applies but shares bytes with one above, 2 fails where it is */
} PatchRow;
static PatchRow patchRows[PATCH_ROWS + 8];
static int nPatchRows, patchSel, patchScroll, patchArmed = -1;
static bool patchesOpen, patchMoving;
static char patchNote[300];
static int patchPressRow = -1, patchPressY, patchPressScroll;
static bool patchDragging;

static PatchRow* patch_row_add(int kind, const char* file, const char* label, const char* note, bool usable) {
	if (nPatchRows >= PATCH_ROWS + 8) return NULL;
	PatchRow* r = &patchRows[nPatchRows++];
	memset(r, 0, sizeof(*r));
	r->kind = kind;
	r->order = -1;
	snprintf(r->file, sizeof(r->file), "%s", file ? file : "");
	snprintf(r->label, sizeof(r->label), "%s", label);
	snprintf(r->note, sizeof(r->note), "%s", note ? note : "");
	r->usable = usable;
	return r;
}

static int patch_name_cmp(const void* a, const void* b) {
	return strcasecmp((const char*) a, (const char*) b);
}

static uint8_t* patch_file_read(const char* path, size_t* n) {
	*n = 0;
	uint8_t* p = NULL;
	FILE* f = fopen(path, "rb");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	rewind(f);
	if (len > 0 && len <= 64L * 1024 * 1024 && (p = malloc((size_t) len)) && fread(p, 1, (size_t) len, f) == (size_t) len)
		*n = (size_t) len;
	else {
		free(p);
		p = NULL;
	}
	fclose(f);
	return p;
}

/* the bytes out changes from rom (gaps under 16 bytes joined; past rom's end, all) */
static void patch_ranges(const uint8_t* rom, size_t romN, const uint8_t* out, size_t outN, PatchInfo* pi) {
	int cap = 0;
	size_t n = romN < outN ? romN : outN;
	for (size_t i = 0; i < n;) {
		if (rom[i] == out[i]) {
			++i;
			continue;
		}
		size_t j = i + 1, same = 0;
		while (j < n && same < 16) {
			same = rom[j] == out[j] ? same + 1 : 0;
			++j;
		}
		ByteRange br = { (uint32_t) i, (uint32_t) (j - same) };
		if (pi->nRanges == cap) {
			cap = cap ? cap * 2 : 64;
			ByteRange* nr = realloc(pi->ranges, sizeof(ByteRange) * (size_t) cap);
			if (!nr) return;
			pi->ranges = nr;
		}
		pi->ranges[pi->nRanges++] = br;
		i = j;
	}
	if (outN > romN) {
		ByteRange* nr = realloc(pi->ranges, sizeof(ByteRange) * (size_t) (pi->nRanges + 1));
		if (!nr) return;
		pi->ranges = nr;
		pi->ranges[pi->nRanges++] = (ByteRange) { (uint32_t) romN, (uint32_t) outN };
	}
}

static bool patch_overlap(const PatchInfo* a, const PatchInfo* b) {
	int i = 0, j = 0;
	while (i < a->nRanges && j < b->nRanges) {
		if (a->ranges[i].to <= b->ranges[j].from) ++i;
		else if (b->ranges[j].to <= a->ranges[i].from) ++j;
		else return true;
	}
	return false;
}

/* what a ROM the patches made is ("Tested romhack: ..." / "Untested ...") */
static void patch_result_text(const uint8_t* out, size_t outN, char* note, size_t size) {
	const char* name = NULL;
	switch (profile_dump(emu.gameCode, emu_crc32(0, out, outN), &name)) {
	case DUMP_MOD_TESTED: snprintf(note, size, tr("Tested romhack: %s"), name); break;
	case DUMP_MOD: snprintf(note, size, tr("Known romhack: %s, untested"), name); break;
	case DUMP_TESTED:
	case DUMP_OFFICIAL: snprintf(note, size, tr("Turns this ROM into another official release: %s"), name ? name : ""); break;
	default: snprintf(note, size, tr("Untested: the second screen may not work with it")); break;
	}
}

/* a known patch's note when it goes on the ROM it's made for */
static void patch_known_text(const KnownPatch* k, const uint8_t* out, size_t outN, char* note, size_t size) {
	const char* name = NULL;
	DumpKind d = out ? profile_dump(emu.gameCode, emu_crc32(0, out, outN), &name) : DUMP_MODIFIED;
	if (d == DUMP_MOD_TESTED) snprintf(note, size, tr("Known %s patch, tested: %s"), k->kind, k->name);
	else snprintf(note, size, tr("Known %s patch, untested: %s. The second screen may not work with it"), k->kind, k->name);
}

/* what a patch does to this ROM, on its own */
static void patch_describe(const uint8_t* rom, size_t romN, uint32_t romCrc, const char* path, PatchInfo* pi) {
	size_t pn = 0;
	uint8_t* p = patch_file_read(path, &pn);
	uint8_t* out = NULL;
	size_t outN = 0;
	int r = pn && rom ? patch_apply(p, pn, rom, romN, &out, &outN) : PATCH_UNREADABLE;
	/* known (docs/patches.md)? Made for this ROM? */
	pi->known = NULL;
	pi->nBases = 0;
	pi->baseOk = false;
	const KnownPatch* k;
	for (int i = 0; pn && (k = profile_patch(p, pn, i)); ++i) {
		if (strncmp(k->code, emu.gameCode, 8) && k->code[0]) continue; /* (another game's, with the same file) */
		if (!pi->known) pi->known = k;
		if (!k->base || k->base == romCrc) pi->baseOk = true, pi->known = k;
		else if (pi->nBases < 8) pi->bases[pi->nBases++] = k->base;
	}
	bool compressed = pn && patch_xdelta_compressed(p, pn);
	free(p);
	pi->usable = r == PATCH_APPLIED;
	if (pi->known && r == PATCH_APPLIED) patch_known_text(pi->known, out, outN, pi->okNote, sizeof(pi->okNote));
	if (r == PATCH_WRONG_ROM && pi->known) {
		snprintf(pi->note, sizeof(pi->note), tr("Known %s patch: %s, made for %s: it can't be switched on for this ROM"), pi->known->kind,
		         pi->known->name, pi->known->baseName);
	} else if (r == PATCH_WRONG_ROM) {
		snprintf(pi->note, sizeof(pi->note), tr("Made for another version of the game: it can't be switched on for this ROM"));
	} else if (r != PATCH_APPLIED && compressed) {
		snprintf(pi->note, sizeof(pi->note),
		         tr("An xdelta made with compression, which SNES DS can't read: patch the ROM with it beforehand, or make it again without (xdelta3 -S none)"));
	} else if (r != PATCH_APPLIED) {
		snprintf(pi->note, sizeof(pi->note), tr("Can't be read: not a patch, or damaged"));
	} else if (pi->known && !pi->baseOk) {
		/* an IPS can't say which ROM it's for: the list knows */
		snprintf(pi->note, sizeof(pi->note), tr("Known %s patch: %s, made for %s. This ROM is another version: it may break the game"),
		         pi->known->kind, pi->known->name, pi->known->baseName);
	} else if (pi->known) {
		snprintf(pi->note, sizeof(pi->note), "%s", pi->okNote);
	} else {
		patch_result_text(out, outN, pi->note, sizeof(pi->note));
	}
	if (r == PATCH_APPLIED) patch_ranges(rom, romN, out, outN, pi);
	free(out);
}

static PatchInfo* patch_info(const char* file) {
	for (int i = patchBesideAt0 ? 1 : 0; i < nPatchInfo; ++i)
		if (!strcmp(patchInfo[i].file, file)) return &patchInfo[i];
	return NULL;
}

static void patch_title_type(const char* file, char* out, size_t size) {
	char title[256], type[8] = "";
	patch_title(file, title, sizeof(title));
	const char* ext = strrchr(file, '.');
	if (ext && (!strcasecmp(ext, ".xdelta") || !strcasecmp(ext, ".vcdiff"))) snprintf(type, sizeof(type), "xdelta");
	else
		for (int k = 0; ext && ext[k + 1] && k < 3; ++k) type[k] = (char) toupper((unsigned char) ext[k + 1]);
	snprintf(out, size, "%s (%s)", title, type);
}

/* The rows, from patchInfo and the choice; the selection kept on the same
 * row. With more than one patch to apply, they're tried together. */
static void patch_rebuild(void) {
	int selKind = patchSel >= 0 && patchSel < nPatchRows ? patchRows[patchSel].kind : PR_SAVE;
	char selFile[256];
	snprintf(selFile, sizeof(selFile), "%s", patchSel >= 0 && patchSel < nPatchRows ? patchRows[patchSel].file : "");
	nPatchRows = 0;
	if (!patch_choice_same(&patchChoice, &patchStarted))
		patch_row_add(PR_RESTART, NULL, "Patches changed since the game started",
		              "The patches aren't as the game started: restart it to play with them (or they're used from the next start)", true);
	patch_row_add(PR_SAVE, NULL, "Separate battery save",
	              "With a patch on, the game keeps its own battery save (\"<game> + <patch>\"): off, it uses the original's", true);
	patch_row_add(PR_STATES, NULL, "Separate save states",
	              "With a patch on, the game keeps its own save states: off, it uses the original's (a state can come out garbled in the other)", true);
	char folder[300];
	const char* slash = strrchr(patchDir, '/');
	snprintf(folder, sizeof(folder), tr("In patches/%s"), slash ? slash + 1 : patchDir);
	patch_row_add(PR_HEADER, NULL, folder, NULL, false);
	int summaryAt = nPatchRows;
	int nApply = (patchBesideAt0 ? 1 : 0) + patchChoice.nOn;
	if (nApply > 1) patch_row_add(PR_SUMMARY, NULL, "", NULL, false);
	if (patchBesideAt0) {
		PatchInfo* pi = &patchInfo[0];
		char label[300], full[600];
		snprintf(label, sizeof(label), tr("%s (beside the ROM)"), pi->file);
		snprintf(full, sizeof(full), tr("Always on, applied first (RetroArch's way: a patch named as the ROM). %s"), pi->note);
		patch_row_add(PR_BESIDE, pi->file, label, full, false);
	}
	/* the ones on, in order */
	for (int i = 0; i < patchChoice.nOn; ++i) {
		PatchInfo* pi = patch_info(patchChoice.on[i]);
		char label[300], t[280];
		patch_title_type(patchChoice.on[i], t, sizeof(t));
		if (nApply > 1) snprintf(label, sizeof(label), "%d. %s", i + 1, t);
		else snprintf(label, sizeof(label), "%s", t);
		PatchRow* r = patch_row_add(PR_PATCH, patchChoice.on[i], label,
		                            pi ? pi->note : "Not in the folder any more: it's left out", pi && pi->usable);
		if (r) r->order = i;
	}
	/* the rest, by name */
	int first = patchBesideAt0 ? 1 : 0;
	for (int i = first; i < nPatchInfo; ++i) {
		if (patch_choice_find(&patchChoice, patchInfo[i].file) >= 0) continue;
		char t[280];
		patch_title_type(patchInfo[i].file, t, sizeof(t));
		patch_row_add(PR_PATCH, patchInfo[i].file, t, patchInfo[i].note, patchInfo[i].usable);
	}
	if (nPatchInfo == first) {
		char note[1300];
		snprintf(note, sizeof(note), tr("Put .ips, .ups, .bps or .xdelta files in %s"), patchDir);
		patch_row_add(PR_EMPTY, NULL, "No patches here yet", note, false);
	}
	/* together, in order, as the game will apply them */
	if (nApply > 1) {
		size_t romN = 0;
		uint8_t* rom = emu_rom_read_all(romPath, &romN);
		const PatchInfo* appliedInfo[PATCH_ON_MAX + 1];
		const char* appliedName[PATCH_ON_MAX + 1];
		int nApplied = 0;
		for (int k = 0; rom && k < nApply; ++k) {
			bool beside = patchBesideAt0 && k == 0;
			const char* file = beside ? patchInfo[0].file : patchChoice.on[k - (patchBesideAt0 ? 1 : 0)];
			PatchInfo* pi = beside ? &patchInfo[0] : patch_info(file);
			PatchRow* row = NULL;
			for (int j = 0; j < nPatchRows; ++j)
				if ((patchRows[j].kind == (beside ? PR_BESIDE : PR_PATCH)) && !strcmp(patchRows[j].file, file)) row = &patchRows[j];
			char path[1400];
			if (beside) patch_find(romPath, path, sizeof(path));
			else snprintf(path, sizeof(path), "%s/%s", patchDir, file);
			size_t pn = 0;
			uint8_t* p = pi ? patch_file_read(path, &pn) : NULL;
			uint8_t* out = NULL;
			size_t outN = 0;
			int r = pn ? patch_apply(p, pn, rom, romN, &out, &outN) : PATCH_UNREADABLE;
			free(p);
			/* a known patch made for another patch's result (an optional one, after its main patch): fine here */
			if (r == PATCH_APPLIED && row && pi && pi->known && !pi->baseOk) {
				uint32_t here = emu_crc32(0, rom, romN);
				for (int b = 0; b < pi->nBases; ++b)
					if (pi->bases[b] == here) snprintf(row->note, sizeof(row->note), "%s", pi->okNote);
			}
			if (r == PATCH_APPLIED) {
				free(rom);
				rom = out, romN = outN;
				for (int a = 0; pi && row && a < nApplied; ++a) {
					if (!appliedInfo[a] || !patch_overlap(pi, appliedInfo[a])) continue;
					row->chain = 1;
					char other[280];
					if (appliedName[a]) patch_title_type(appliedName[a], other, sizeof(other));
					else snprintf(other, sizeof(other), tr("the patch beside the ROM"));
					snprintf(row->note, sizeof(row->note),
					         tr("Changes some of the same bytes as %s, and goes over it there: they may not work together"), other);
					break;
				}
				appliedInfo[nApplied] = pi;
				appliedName[nApplied++] = beside ? NULL : patchChoice.on[k - (patchBesideAt0 ? 1 : 0)];
			} else {
				free(out);
				if (row && pi && pi->usable) {
					row->chain = 2;
					snprintf(row->note, sizeof(row->note),
					         tr("Left out where it is: it's made for the unchanged ROM, so it has to be first (only one such patch "
					         "can be on). Move it up with X"));
				} else if (row) {
					row->chain = 2;
				}
			}
		}
		char res[300] = "";
		if (rom) patch_result_text(rom, romN, res, sizeof(res));
		free(rom);
		snprintf(patchRows[summaryAt].label, sizeof(patchRows[0].label), tr("Together: %s"), res);
		snprintf(patchRows[summaryAt].note, sizeof(patchRows[0].note), "%s", res);
	}
	/* the selection where it was */
	patchSel = -1;
	for (int i = 0; i < nPatchRows && patchSel < 0; ++i)
		if (patchRows[i].kind == selKind && !strcmp(patchRows[i].file, selFile)) patchSel = i;
	for (int i = 0; i < nPatchRows && patchSel < 0; ++i)
		if (patchRows[i].kind == PR_SAVE) patchSel = i;
}

static void open_patches(void) {
	for (int i = 0; i < nPatchInfo; ++i) free(patchInfo[i].ranges);
	memset(patchInfo, 0, sizeof(patchInfo));
	nPatchInfo = 0;
	nPatchRows = 0;
	patchSel = patchScroll = 0;
	patchArmed = -1;
	patchMoving = false;
	patchNote[0] = 0;
	patchPressRow = -1;
	size_t romN = 0;
	uint8_t* rom = emu_rom_read_all(romPath, &romN);
	uint32_t romCrc = rom ? emu_crc32(0, rom, romN) : 0;
	char beside[1024];
	patchBesideAt0 = patch_find(romPath, beside, sizeof(beside));
	if (patchBesideAt0) {
		PatchInfo* pi = &patchInfo[nPatchInfo++];
		snprintf(pi->file, sizeof(pi->file), "%s", patch_name(beside));
		patch_describe(rom, romN, romCrc, beside, pi);
	}
	char names[PATCH_ROWS][256];
	int n = 0;
	DIR* d = opendir(patchDir);
	struct dirent* e;
	while (d && (e = readdir(d)) && n < PATCH_ROWS - 2) {
		const char* ext = strrchr(e->d_name, '.');
		if (e->d_name[0] != '.' && ext &&
		    (!strcasecmp(ext, ".ips") || !strcasecmp(ext, ".ups") || !strcasecmp(ext, ".bps") || !strcasecmp(ext, ".xdelta") ||
		     !strcasecmp(ext, ".vcdiff")))
			snprintf(names[n++], sizeof(names[0]), "%s", e->d_name);
	}
	if (d) closedir(d);
	qsort(names, (size_t) n, sizeof(names[0]), patch_name_cmp);
	for (int i = 0; i < n; ++i) {
		char path[1300];
		snprintf(path, sizeof(path), "%s/%s", patchDir, names[i]);
		PatchInfo* pi = &patchInfo[nPatchInfo++];
		snprintf(pi->file, sizeof(pi->file), "%s", names[i]);
		patch_describe(rom, romN, romCrc, path, pi);
	}
	free(rom);
	patch_rebuild();
	patchSel = 0;
	patchesOpen = true;
}

static bool patch_is_on(const PatchRow* r) {
	if (r->kind == PR_SAVE) return patchChoice.sepSave;
	if (r->kind == PR_STATES) return patchChoice.sepStates;
	if (r->kind == PR_BESIDE) return true;
	return r->kind == PR_PATCH && r->order >= 0;
}

static bool patch_row_text_only(int kind) {
	return kind == PR_HEADER || kind == PR_SUMMARY || kind == PR_EMPTY;
}

static int patch_max_scroll(const CheatGeom* g) {
	return SDL_max(0, nPatchRows * (g->rowH + g->gap) - g->gap - g->listH);
}

static void patch_keep_visible(int scr) {
	CheatGeom g = cheat_geom(scr);
	int y = patchSel * (g.rowH + g.gap);
	if (y < patchScroll) patchScroll = y;
	if (y + g.rowH > patchScroll + g.listH) patchScroll = y + g.rowH - g.listH;
	patchScroll = SDL_clamp(patchScroll, 0, patch_max_scroll(&g));
}

/* the one picked up (X) a place up or down among the ones on */
static void patch_shift(int dir) {
	if (patchSel < 0 || patchSel >= nPatchRows) return;
	int i = patchRows[patchSel].order, j = i + dir;
	if (i < 0 || j < 0 || j >= patchChoice.nOn) return;
	char t[256];
	memcpy(t, patchChoice.on[i], sizeof(t));
	memcpy(patchChoice.on[i], patchChoice.on[j], sizeof(t));
	memcpy(patchChoice.on[j], t, sizeof(t));
	patch_choice_save(gp.patches, &patchChoice);
	fprintf(stderr, "patches: %s moved to %d\n", t, j + 1);
	patch_rebuild();
	patch_keep_visible(ui_touch_screen());
}

static void patch_move(int dir) {
	if (patchMoving) {
		patch_shift(dir);
		return;
	}
	for (int i = patchSel + dir; i >= 0 && i < nPatchRows; i += dir) {
		if (!patch_row_text_only(patchRows[i].kind)) {
			patchSel = i;
			break;
		}
	}
	patchArmed = -1;
	patchNote[0] = 0;
	patch_keep_visible(ui_touch_screen());
}

/* X (or a tap on its number): pick the selected patch up to move it, or put it down */
static void patch_pick_up(void) {
	patchArmed = -1;
	if (patchMoving) {
		patchMoving = false;
		snprintf(patchNote, sizeof(patchNote), "%s",
		         patch_choice_same(&patchChoice, &patchStarted) ? tr("As the game started"): tr("Restart from the top row to play with this order"));
		return;
	}
	const PatchRow* r = patchSel >= 0 && patchSel < nPatchRows ? &patchRows[patchSel] : NULL;
	if (!r || r->kind != PR_PATCH || r->order < 0) return;
	if (patchChoice.nOn < 2) {
		snprintf(patchNote, sizeof(patchNote), tr("Only patches that are on have an order, and this is the only one"));
		return;
	}
	patchMoving = true;
	snprintf(patchNote, sizeof(patchNote), tr("Moving it: Up / Down or tap where it goes, then A (or tap it again). The one lower in the list is applied later"));
}

/* Can the game carry on from this moment with the patches as chosen (GEN-9)?
 * The patched ROM the same size as the one playing, and the battery save and
 * save states the same files. */
static bool patch_live_ok(void) {
	char code[9] = "????????";
	emu_rom_code(romPath, code);
	size_t nowN = 0, newN = 0;
	int nowR = PATCH_NONE, newR = PATCH_NONE;
	free(emu_rom_read_played(romPath, &nowN, &nowR, NULL, NULL));
	char files[PATCH_ON_MAX + 1][1024];
	int n = patch_plan(cfg, romPath, code, &patchChoice, files, PATCH_ON_MAX + 1);
	free(emu_rom_read_patched(romPath, files, n, &newN, &newR, NULL));
	if (!nowN || nowN != newN) return false;
	GamePaths a, b;
	game_paths(cfg, romPath, &a);
	game_paths(cfg, romPath, &b);
	if (nowR == PATCH_APPLIED || nowR == PATCH_PARTIAL) game_paths_patched(cfg, &a, &patchStarted);
	if (newR == PATCH_APPLIED || newR == PATCH_PARTIAL) game_paths_patched(cfg, &b, &patchChoice);
	return !strcmp(a.sav, b.sav) && !strcmp(a.quick, b.quick) && !strcmp(a.autoState, b.autoState);
}

static bool state_save(const char* path);
/* the restart for a change: carrying on from this moment when it can */
static bool patch_restart(void) {
	patchReloadNext = true;
	patchHandoffNext = false;
	if (patch_live_ok()) {
		snprintf(patchHandoff, sizeof(patchHandoff), "%s/%s.patch-switch.state", cfg->stateDir, gp.base);
		patchHandoffNext = state_save(patchHandoff);
	}
	fprintf(stderr, "patches: %s\n", patchHandoffNext ? "carrying on with the change" : "restarting with the change");
	return true;
}

/* A on a row (or a tap). Returns true when the game is to restart (GEN-9). */
static bool patch_choose(int row, bool later) {
	if (row < 0 || row >= nPatchRows) return false;
	PatchRow* r = &patchRows[row];
	if (patchMoving) {
		patch_pick_up();
		return false;
	}
	if (r->kind == PR_SAVE || r->kind == PR_STATES) {
		bool* v = r->kind == PR_SAVE ? &patchChoice.sepSave : &patchChoice.sepStates;
		*v = !*v;
		patch_choice_save(gp.patches, &patchChoice);
		snprintf(patchNote, sizeof(patchNote), "%s", patchChoice.nOn ? tr("Used from the next time the game starts"): tr("Saved for when a patch is on"));
		return false;
	}
	if (r->kind == PR_RESTART) {
		if (later) return false;
		if (patchArmed != row) {
			patchArmed = row;
			snprintf(patchNote, sizeof(patchNote), "%s",
			         patch_live_ok() ? tr("Are you sure? A: use them now and carry on from here. B: call it off"): tr("Are you sure? Save your game first. A: restart now. B: call it off"));
			return false;
		}
		patchArmed = -1;
		return patch_restart();
	}
	if (r->kind != PR_PATCH) return false;
	bool on = patch_is_on(r);
	if (!on && !r->usable) {
		snprintf(patchNote, sizeof(patchNote), "%s", r->note);
		return false;
	}
	if (!on && patchChoice.nOn >= PATCH_ON_MAX) {
		snprintf(patchNote, sizeof(patchNote), tr("Up to %d patches can be on at once"), PATCH_ON_MAX);
		return false;
	}
	if (patchArmed != row) {
		patchArmed = row;
		/* (what A would do: tried on a copy of the choice) */
		PatchChoice keep = patchChoice;
		if (on) {
			for (int k = r->order; k + 1 < patchChoice.nOn; ++k) memcpy(patchChoice.on[k], patchChoice.on[k + 1], sizeof(patchChoice.on[0]));
			--patchChoice.nOn;
		} else {
			snprintf(patchChoice.on[patchChoice.nOn++], sizeof(patchChoice.on[0]), "%s", r->file);
		}
		bool live = patch_live_ok();
		patchChoice = keep;
		if (live)
			snprintf(patchNote, sizeof(patchNote), tr("Are you sure? A: switch it %s now and carry on from here. Y: not yet (more changes first). B: call it off"),
			         on ? tr("off"): tr("on"));
		else
			snprintf(patchNote, sizeof(patchNote),
			         tr("Are you sure? Save your game first. A: restart %s it now. Y: not yet (more changes first). B: call it off"),
			         on ? tr("without"): tr("with"));
		return false;
	}
	patchArmed = -1;
	char file[256];
	snprintf(file, sizeof(file), "%s", r->file);
	if (on) {
		int i = r->order;
		for (int k = i; k + 1 < patchChoice.nOn; ++k) memcpy(patchChoice.on[k], patchChoice.on[k + 1], sizeof(patchChoice.on[0]));
		--patchChoice.nOn;
	} else {
		snprintf(patchChoice.on[patchChoice.nOn++], sizeof(patchChoice.on[0]), "%s", file); /* (applied last) */
	}
	patch_choice_save(gp.patches, &patchChoice);
	fprintf(stderr, "patches: %s %s%s\n", file, on ? "switched off" : "switched on", later ? ", for later" : "");
	if (later) {
		patch_rebuild();
		patch_keep_visible(ui_touch_screen());
		snprintf(patchNote, sizeof(patchNote), "%s", patch_choice_same(&patchChoice, &patchStarted) ? tr("As the game started"): tr("Restart from the top row to play with it (or it's used from the next start)"));
		return false;
	}
	return patch_restart();
}

/* is screen-local x on the number of a patch that's on (to pick it up) */
static bool patch_on_number(int scr, int row, int x) {
	CheatGeom g = cheat_geom(scr);
	return row >= 0 && row < nPatchRows && patchRows[row].kind == PR_PATCH && patchRows[row].order >= 0 && patchChoice.nOn > 1 &&
	       x < g.x + g.px * 14;
}

/* row under screen-local y, or -1 */
static int patch_row_at(int scr, int x, int y) {
	CheatGeom g = cheat_geom(scr);
	if (y < g.top || y >= g.top + g.listH || x < g.x || x >= g.x + g.w) return -1;
	int ly = y - g.top + patchScroll;
	int row = ly / (g.rowH + g.gap);
	if (ly % (g.rowH + g.gap) >= g.rowH || row >= nPatchRows) return -1;
	return row;
}

static void draw_patches(int scr) {
	CheatGeom g = cheat_geom(scr);
	SDL_Rect r = screens[scr].rect;
	float px = g.px;
	int rad = menu_geom(scr).rad, cap = menu_geom(scr).cap;
	SDL_Rect b = tab_rect(scr, 0), t = tab_rect(scr, 1);
	ui_fill_round(scr, b, C_BTN, rad);
	ui_label(scr, b.x + b.w / 2, b.y + b.h / 2, cap, "Back", C_TEXT);
	ui_fill_round(scr, t, C_BTN_EDGE, rad);
	ui_label(scr, t.x + t.w / 2, t.y + t.h / 2, cap, "Patches", (SDL_Color) { 20, 16, 36, 255 });
	SDL_Rect clip = { screens[scr].rect.x, screens[scr].rect.y + g.top, r.w, g.listH };
	ui_clip(scr, &clip);
	int tcap = g.tpx * 5;
	int pillW = ui_label_width(scr, tcap, "Restart") + px * 8;
	int nameW = g.w - pillW - px * 10;
	for (int i = 0; i < nPatchRows; ++i) {
		int y = g.top + i * (g.rowH + g.gap) - patchScroll;
		if (y + g.rowH <= g.top || y >= g.top + g.listH) continue;
		const PatchRow* pr = &patchRows[i];
		char line[300];
		if (patch_row_text_only(pr->kind)) {
			fit_text(line, sizeof(line), pr->kind == PR_HEADER ? TF_BOLD : TF_BODY, tcap, pr->label, g.w - px * 4);
			ui_font_text(scr, pr->kind == PR_HEADER ? TF_BOLD : TF_BODY, g.x + px * 2, y + g.rowH / 2, tcap, line,
			             pr->kind == PR_HEADER ? C_GOLD : pr->kind == PR_SUMMARY ? C_TEXT : C_DIS, -1);
			continue;
		}
		bool sel = i == patchSel;
		SDL_Rect row = { g.x, y, g.w, g.rowH };
		ui_key(scr, row, rad, sel ? KEY_SEL : KEY_PLAIN);
		bool on = patch_is_on(pr);
		bool dim = pr->kind == PR_PATCH && !pr->usable && !on;
		bool moving = sel && patchMoving;
		fit_text(line, sizeof(line), TF_BODY, tcap, pr->label, nameW);
		/* (a patch that shares bytes with one above it: its name in gold) */
		ui_font_text(scr, TF_BODY, g.x + px * 4, y + g.rowH / 2, tcap, line, dim ? C_DIS : on && pr->chain == 1 ? C_GOLD : C_TEXT, -1);
		const char* pill = i == patchArmed ? N_("Sure?"): moving        ? N_("Move"): pr->kind == PR_RESTART ? "Restart"
		                   : pr->kind == PR_BESIDE  ? (pr->chain == 2 ? N_("Fails"): N_("Always"))
		                   : dim                    ? "-"
		                   : on && pr->chain == 2   ? N_("Fails"): on                     ? "On"
		                                            : "Off";
		bool gold = i == patchArmed || moving || pr->kind == PR_RESTART;
		SDL_Rect pl = { g.x + g.w - pillW - px * 3, y + px * 2, pillW, g.rowH - px * 4 };
		SDL_Color pc = gold                   ? C_GOLD
		               : on && pr->chain == 2 ? (SDL_Color) { 176, 72, 72, 255 }
		               : on                   ? (SDL_Color) { 64, 168, 112, 255 }
		                                      : (SDL_Color) { 60, 56, 84, 255 };
		ui_fill_round(scr, pl, pc, px * 2);
		ui_label(scr, pl.x + pl.w / 2, pl.y + pl.h / 2, tcap, pill,
		         gold ? (SDL_Color) { 20, 16, 36, 255 } : on ? C_TEXT : (SDL_Color) { 160, 156, 184, 255 });
	}
	ui_clip(scr, NULL);
	int maxS = patch_max_scroll(&g);
	if (maxS > 0) {
		int total = g.listH + maxS;
		int bh = SDL_max(px * 6, g.listH * g.listH / total);
		int by = g.top + (g.listH - bh) * patchScroll / maxS;
		ui_fill_round(scr, (SDL_Rect) { r.w - px * 4, by, px * 2, bh }, C_BTN_EDGE, px);
	}
	/* footer: what was just done or asked, else what the selected row is */
	const char* foot = patchNote[0] ? patchNote : (patchSel >= 0 && patchSel < nPatchRows ? patchRows[patchSel].note : "");
	if (foot[0]) ui_font_wrap(scr, TF_BODY, px * 6, g.footY - tcap / 2, r.w - px * 12, tcap, foot, C_GOLD);
}

/* a button page with more rows than ROW_VIS scrolls: itemTop is its first row shown */
static int itemTop;
static bool item_shown(int i) {
	return itemRow[i] >= itemTop && itemRow[i] + item_span(menuItems[i]) <= itemTop + ROW_VIS;
}
/* the selected button kept on screen */
static void items_scroll_to_sel(void) {
	if (itemRow[menuSel] < itemTop) itemTop = itemRow[menuSel];
	if (itemRow[menuSel] + item_span(menuItems[menuSel]) > itemTop + ROW_VIS) itemTop = itemRow[menuSel] + item_span(menuItems[menuSel]) - ROW_VIS;
	itemTop = SDL_clamp(itemTop, 0, SDL_max(0, nMenuRows - ROW_VIS));
}
static SDL_Rect item_rect(int scr, int i) {
	MenuGeom g = menu_geom(scr);
	SDL_Rect r = { g.margin, g.top + (itemRow[i] - itemTop) * (g.h + g.gap), g.w, g.h * item_span(menuItems[i]) + g.gap * (item_span(menuItems[i]) - 1) };
	if (itemSide[i] >= 0) {
		int hw = (g.w - g.gap) / 2;
		r.x += itemSide[i] * (g.w - hw);
		r.w = hw;
	}
	return r;
}

/* box t of n across the top of the menu: the page tabs (n = N_PAGES), or
 * Back and the title on the cheats / achievements screens (n = 2) */
static SDL_Rect top_box(int scr, int t, int n) {
	MenuGeom g = menu_geom(scr);
	if (n == N_PAGES) {
		/* the page tabs (icons, drawn by draw_tab_icon) between the L1 / R1 prompts, sharing the width */
		int x = g.margin + g.prompt + g.gap, avail = g.w - (g.prompt + g.gap) * 2 - g.gap * (n - 1);
		int x0 = x + t * avail / n + t * g.gap, x1 = x + (t + 1) * avail / n + t * g.gap;
		return (SDL_Rect) { x0, g.tabY, x1 - x0, g.h };
	}
	int tw = (g.w - g.gap * (n - 1)) / n;
	return (SDL_Rect) { g.margin + t * (tw + g.gap), g.tabY, tw, g.h };
}

static SDL_Rect tab_rect(int scr, int t) {
	return top_box(scr, t, 2);
}

/* ---------------------------------------------------------------- bug report note
 *
 * After the bug button (the quick bar's, or Extras > Submit a Bug Report),
 * the game paused: a note for the report, typed on a keyboard on the
 * touchscreen (or with the D-pad and A), written at the top of the
 * report's info file. Skip, or Done with nothing typed, saves it without
 * one. The report is the moment the button was pressed: its files are
 * gathered then (bug_report), and zipped with the note (bug_finish). */
static bool noteOpen;
static bool noteFromBar;   /* the quick bar's button: done closes the menu too */
static char noteText[241];
static int noteLen;
static bool noteShift;
static int noteRow, noteCol; /* the key the D-pad is on */
static int notePressKey = -1;
static bool bug_finish(const char* note, char* out, size_t outSize);
static void note_open(bool fromBar);
static void note_done(bool keep);

/* the keys: rows of labels, "\x01" Shift, "\x02" Del, "\x03" Space, "\x04" Done */
#define NOTE_ROWS 5
static const char* const noteKeys[NOTE_ROWS][11] = {
	{ "1", "2", "3", "4", "5", "6", "7", "8", "9", "0" },
	{ "q", "w", "e", "r", "t", "y", "u", "i", "o", "p" },
	{ "a", "s", "d", "f", "g", "h", "j", "k", "l", "'" },
	{ "\x01", "z", "x", "c", "v", "b", "n", "m", ",", ".", "\x02" },
	{ "\x03", "\x04" },
};
static const char* const noteShifted[10] = { "!", "?", "\"", "-", ":", ";", "/", "(", ")", "&" };
static int note_row_len(int r) {
	int n = 0;
	while (n < 11 && noteKeys[r][n]) ++n;
	return n;
}
/* a key's width in twelfths of the row: Shift and Del a half wider, Space most of the last row */
static int note_key_units(int r, int c) {
	const char* k = noteKeys[r][c];
	if (k[0] == '\x01' || k[0] == '\x02') return 15;
	if (k[0] == '\x03') return 80;
	if (k[0] == '\x04') return 40;
	return 10;
}
typedef struct { SDL_Rect field; int top, rowH, gap; } NoteGeom;
static NoteGeom note_geom(int scr) {
	MenuGeom mg = menu_geom(scr);
	SDL_Rect r = screens[scr].rect;
	NoteGeom g;
	g.gap = mg.gap;
	g.field = (SDL_Rect) { mg.margin, mg.top, mg.w, mg.h * 9 / 4 };
	g.top = g.field.y + g.field.h + mg.gap * 2;
	g.rowH = (r.h - mg.margin - g.top - g.gap * (NOTE_ROWS - 1)) / NOTE_ROWS;
	return g;
}
static SDL_Rect note_key_rect(int scr, int r, int c) {
	MenuGeom mg = menu_geom(scr);
	NoteGeom g = note_geom(scr);
	int n = note_row_len(r), units = 0, before = 0;
	for (int i = 0; i < n; ++i) units += note_key_units(r, i), before += i < c ? note_key_units(r, i) : 0;
	int avail = mg.w - g.gap * (n - 1);
	int x0 = mg.margin + before * avail / units + c * g.gap, x1 = mg.margin + (before + note_key_units(r, c)) * avail / units + c * g.gap;
	return (SDL_Rect) { x0, g.top + r * (g.rowH + g.gap), x1 - x0, g.rowH };
}
static const char* note_key_label(int r, int c) {
	const char* k = noteKeys[r][c];
	static char up[4];
	switch (k[0]) {
	case '\x01': return noteShift ? "ABC" : "abc";
	case '\x02': return N_("Del");
	case '\x03': return N_("Space");
	case '\x04': return noteLen ? N_("Done"): N_("Done (No Note)");
	}
	if (!noteShift) return k;
	if (r == 0) return noteShifted[c];
	if (k[0] >= 'a' && k[0] <= 'z') return up[0] = (char) (k[0] - 32), up[1] = 0, up;
	return k;
}
static void note_type(const char* s) {
	size_t n = strlen(s);
	if (noteLen + n < sizeof(noteText)) memcpy(noteText + noteLen, s, n), noteLen += (int) n, noteText[noteLen] = 0;
}
/* a key pressed (touch or A) */
static void note_press(int r, int c) {
	const char* k = noteKeys[r][c];
	switch (k[0]) {
	case '\x01': noteShift = !noteShift; return;
	case '\x02': if (noteLen) noteText[--noteLen] = 0; return;
	case '\x03': note_type(" "); return;
	case '\x04': note_done(true); return;
	}
	note_type(note_key_label(r, c));
	noteShift = false; /* (one letter, as on a phone) */
}
static bool note_key_at(int scr, int x, int y, int* r, int* c) {
	for (int i = 0; i < NOTE_ROWS; ++i) {
		for (int j = 0; j < note_row_len(i); ++j) {
			SDL_Rect k = note_key_rect(scr, i, j);
			if (x >= k.x && x < k.x + k.w && y >= k.y && y < k.y + k.h) return *r = i, *c = j, true;
		}
	}
	return false;
}
/* the D-pad: across a row (round at the ends), or to the key above / below nearest across */
static void note_move(int scr, int dr, int dc) {
	if (dc) {
		int n = note_row_len(noteRow);
		noteCol = (noteCol + dc + n) % n;
		return;
	}
	SDL_Rect k = note_key_rect(scr, noteRow, noteCol);
	int cx = k.x + k.w / 2;
	noteRow = (noteRow + dr + NOTE_ROWS) % NOTE_ROWS;
	int best = 0, bd = INT_MAX;
	for (int j = 0; j < note_row_len(noteRow); ++j) {
		SDL_Rect o = note_key_rect(scr, noteRow, j);
		int d = abs(o.x + o.w / 2 - cx);
		if (d < bd) bd = d, best = j;
	}
	noteCol = best;
}
static void draw_note(int scr) {
	MenuGeom mg = menu_geom(scr);
	NoteGeom g = note_geom(scr);
	int rad = mg.rad, cap = mg.cap;
	/* (on a plain backdrop: the game's picture behind the keys made them hard to read) */
	ui_fill(scr, (SDL_Rect) { 0, 0, screens[scr].rect.w, screens[scr].rect.h }, (SDL_Color) { 12, 10, 22, 255 });
	SDL_Rect b = tab_rect(scr, 0), t = tab_rect(scr, 1);
	ui_fill_round(scr, b, C_BTN, rad);
	ui_label(scr, b.x + b.w / 2, b.y + b.h / 2, cap, "Skip", C_TEXT);
	ui_fill_round(scr, t, C_BTN_EDGE, rad);
	ui_label(scr, t.x + t.w / 2, t.y + t.h / 2, cap, "Bug Report", (SDL_Color) { 20, 16, 36, 255 });
	/* the note so far, with a cursor; the hint while it's empty */
	ui_fill_round(scr, g.field, (SDL_Color) { 30, 26, 50, 255 }, rad);
	int tc = SDL_max(4, g.field.h / 6), pad = mg.pad;
	SDL_Rect clip = { screens[scr].rect.x + g.field.x, screens[scr].rect.y + g.field.y, g.field.w, g.field.h };
	ui_clip(scr, &clip);
	if (!noteLen) {
		ui_font_wrap(scr, TF_BODY, g.field.x + pad, g.field.y + pad / 2, g.field.w - pad * 2, tc,
		             "Add a note (optional): what happened, and what you expected to happen.", C_DIS);
	} else {
		char shown[sizeof(noteText) + 4];
		snprintf(shown, sizeof(shown), "%s%s", noteText, (SDL_GetTicks() / 500) & 1 ? "|" : "");
		ui_font_wrap(scr, TF_BODY, g.field.x + pad, g.field.y + pad / 2, g.field.w - pad * 2, tc, shown, C_TEXT);
	}
	ui_clip(scr, NULL);
	/* the keys */
	int kc = SDL_min(cap, g.rowH * 2 / 5);
	for (int r = 0; r < NOTE_ROWS; ++r) {
		for (int c = 0; c < note_row_len(r); ++c) {
			SDL_Rect k = note_key_rect(scr, r, c);
			bool sel = r == noteRow && c == noteCol;
			bool special = noteKeys[r][c][0] < ' ';
			bool lit = noteKeys[r][c][0] == '\x01' && noteShift;
			(void) special; /* (the special keys look like the others) */
			ui_key(scr, k, rad, sel ? KEY_SEL : lit ? KEY_LIT : KEY_PLAIN);
			ui_label(scr, k.x + k.w / 2, k.y + k.h / 2, kc, note_key_label(r, c), lit ? (SDL_Color) { 20, 16, 36, 255 } : C_TEXT);
		}
	}
}

/* The device's battery, 0-100, or -1 if unknown: the battery in
 * /sys/class/power_supply (ROCKNIX), else SDL's (Android). Read at most
 * every 5 seconds. */
static int battery_percent(void) {
	static int pct = -1;
	static Uint32 last;
	static bool read;
	Uint32 now = SDL_GetTicks();
	if (read && now - last < 5000) return pct;
	read = true;
	last = now;
	pct = -1;
#ifndef __ANDROID__
	DIR* d = opendir("/sys/class/power_supply");
	if (d) {
		struct dirent* de;
		while (pct < 0 && (de = readdir(d))) {
			if (de->d_name[0] == '.') continue;
			char path[512], type[32] = "";
			snprintf(path, sizeof(path), "/sys/class/power_supply/%s/type", de->d_name);
			FILE* f = fopen(path, "r");
			if (!f) continue;
			if (!fgets(type, sizeof(type), f)) type[0] = 0;
			fclose(f);
			if (strncmp(type, "Battery", 7)) continue;
			snprintf(path, sizeof(path), "/sys/class/power_supply/%s/capacity", de->d_name);
			f = fopen(path, "r");
			if (!f) continue;
			int v;
			if (fscanf(f, "%d", &v) == 1 && v >= 0 && v <= 100) pct = v;
			fclose(f);
		}
		closedir(d);
	}
#endif
	if (pct < 0) {
		int p = -1;
		SDL_PowerState st = SDL_GetPowerInfo(NULL, &p);
		if (st != SDL_POWERSTATE_UNKNOWN && st != SDL_POWERSTATE_NO_BATTERY && p >= 0) pct = p;
	}
	return pct;
}

/* "Paused" in the middle of the other screen, on a key */
static void draw_paused_label(int o) {
	float px = ui_px(o) + 1;
	int cap = px * 7 * 34 / 35, tw = ui_font_width(TF_BOLD, cap, "Paused"), padX = px * 10, h = cap * 2 + px * 8;
	SDL_Rect r = screens[o].rect;
	SDL_Rect k = { (r.w - tw) / 2 - padX, (r.h - h) / 2, tw + padX * 2, h };
	ui_key(o, k, px * 4, KEY_PLAIN);
	ui_label(o, r.w / 2, r.h / 2, cap, "Paused", ui_theme_light() ? ui_key_ink() : C_GOLD); /* (dark grey on the light theme's keys) */
}

/* Status text on the top (first) screen while paused, with a drop shadow:
 * align -1 at the top left, 1 at the top right. */
static void draw_status(const char* text, int align) {
	int s = 0; /* the top screen, whatever is on it */
	float px = ui_px(s);
	int cap = px * 5;
	int x = align < 0 ? (int) (px * 5) : screens[s].rect.w - (int) (px * 5), cy = px * 7;
	/* on a key, so it shows over the game's picture (dark or light, as the menus) */
	int tw = ui_font_width(TF_BOLD, cap, text), padX = px * 4, h = cap * 2 + px * 3;
	SDL_Rect k = { align < 0 ? x - padX : x - tw - padX, cy - h / 2, tw + padX * 2, h };
	ui_key(s, k, px * 3, KEY_PLAIN);
	ui_font_text(s, TF_BOLD, x, cy, cap, text, C_TEXT, align);
}

/* While paused: the time (24-hour HH:MM) at the top left of the top screen
 * and the battery level at the top right. */
static void draw_battery(void) {
	char buf[16];
	time_t now = time(NULL);
	struct tm tm;
	if (localtime_r(&now, &tm)) {
		snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
		draw_status(buf, -1);
	}
	int pct = battery_percent();
	if (pct >= 0) {
		snprintf(buf, sizeof(buf), "%d%%", pct);
		draw_status(buf, 1);
	}
}

/* ---------------------------------------------------------------- the quick bar
 *
 * Swiping up from the bottom of the touchscreen brings up a bar along its
 * bottom, over whatever it shows, without pausing: the time, the battery
 * and the frames a second, then icon buttons: Save State, Load State,
 * Settings (the pause menu), Bug Report (bug_report) and Close SNES DS
 * (tapped twice). In games with another button layout, Controls above.
 * Swiping down, the pause menu opening or 5 seconds untouched put it
 * away. Greys only, to sit over any game's colours. */
static bool barOpen;
static float barShown;          /* 0 hidden .. 1 all the way up (sliding) */
static Uint32 barSince;         /* last touched */
static Uint32 barLitAt;         /* a button's action just done: it shows white a moment */
static int barLit = -1;
static Uint32 barExitArmed;     /* Close SNES DS tapped once (a second tap within 3 s closes) */
enum { G_NONE, G_TRACK, G_DONE, G_BTN, G_PIN };
static int barGesture = G_NONE;
static int barBtn = -1;         /* the button under the finger (G_BTN) */
static int barGX, barGY;        /* where the finger went down */
static bool barDownOpen;        /* the bar was up when it went down */
enum { BB_SAVE, BB_LOAD, BB_SETTINGS, BB_BUG, BB_EXIT, BB_COUNT };

static const SDL_Color G_BG = { 22, 22, 22, 238 }, G_EDGE = { 128, 128, 128, 255 }, G_BTN_C = { 60, 60, 60, 255 },
                       G_BTN_DOWN = { 120, 120, 120, 255 }, G_TEXT = { 236, 236, 236, 255 }, G_DIS = { 110, 110, 110, 255 },
                       G_LIT = { 255, 255, 255, 255 };

static const char* const icon_save[] = {
	"XXXXXXXXXXXXX...",
	"XX.XXXXXXX.XXX..",
	"XX.XXXXXXX.XXXX.",
	"XX.XXXX..X.XXXXX",
	"XX.XXXX..X.XXXXX",
	"XX.XXXXXXX.XXXXX",
	"XX.........XXXXX",
	"XXXXXXXXXXXXXXXX",
	"XXXXXXXXXXXXXXXX",
	"XX............XX",
	"XX............XX",
	"XX.XXXXXXXXXX.XX",
	"XX............XX",
	"XX.XXXXXXXXXX.XX",
	"XX............XX",
	"XXXXXXXXXXXXXXXX",
	NULL,
};
static const char* const icon_load[] = {
	"................",
	"XXXXXX..........",
	"XXXXXXX.........",
	"XX...XXXXXXXXX..",
	"XX..........XX..",
	"XX..........XX..",
	"XX..XXXXXXXXXXXX",
	"XX.XXXXXXXXXXXXX",
	"XX.XX........XX.",
	"XXXX........XX..",
	"XXX.........XX..",
	"XXX........XX...",
	"XXXXXXXXXXXXX...",
	"XXXXXXXXXXXX....",
	"................",
	"................",
	NULL,
};
static const char* const icon_gear[] = {
	"......XXXX......",
	"......XXXX......",
	"..XX.XXXXXX.XX..",
	"..XXXXXXXXXXXX..",
	"...XXXXXXXXXX...",
	"..XXXX....XXXX..",
	"XXXXX......XXXXX",
	"XXXXX......XXXXX",
	"XXXXX......XXXXX",
	"XXXXX......XXXXX",
	"..XXXX....XXXX..",
	"...XXXXXXXXXX...",
	"..XXXXXXXXXXXX..",
	"..XX.XXXXXX.XX..",
	"......XXXX......",
	"......XXXX......",
	NULL,
};
static const char* const icon_bug[] = {
	"..X.......X..",
	"...X.....X...",
	"....XXXXX....",
	"...XXXXXXX...",
	".............",
	"X..XXX.XXX..X",
	".X.XXX.XXX.X.",
	"..XXXX.XXXX..",
	"...XXX.XXX...",
	"XXXXXX.XXXXXX",
	"...XXX.XXX...",
	"..XXXX.XXXX..",
	".X.XXX.XXX.X.",
	"X...XX.XX...X",
	NULL,
};
static const char* const icon_exit[] = {
	"XXXXXXX.........",
	"X.....X.....XX..",
	"X.....X.....XX..",
	"X.....X.........",
	"X.....X....XXX..",
	"X.....X...XXXXX.",
	"X.....X..XX.XX.X",
	"X.....X....XX...",
	"X.....X....XXX..",
	"X.....X...XX.XX.",
	"X.....X..XX...XX",
	"X.....X.XX......",
	"X.....X.........",
	"X.....X.........",
	"X.....X.........",
	"XXXXXXX.........",
	NULL,
};

/* Top to bottom: in games with another button layout (profile
 * controls), the Controls picker, centred; the status row (time, battery,
 * FPS); the buttons, across the whole width. */
static int bar_status_h(int s) {
	return ui_px(s) * 14;
}
static int bar_buttons_h(int s) {
	return ui_px(s) * 22;
}
static int bar_pick_h(int s) {
	return 0;
}
/* under the buttons: a line for messages (State saved...) */
static int bar_msg_h(int s) {
	return ui_px(s) * 13;
}

static SDL_Rect bar_rect(int s) {
	SDL_Rect r = screens[s].rect;
	float px = ui_px(s);
	int m = px * 4, h = bar_pick_h(s) + bar_status_h(s) + bar_buttons_h(s) + bar_msg_h(s);
	int slide = (int) ((1.0f - barShown) * (h + m));
	return (SDL_Rect) { m, r.h - m - h + slide, r.w - m * 2, h };
}

/* the buttons: the bottom row, sharing its width */
static SDL_Rect bar_button(int s, int i) {
	SDL_Rect b = bar_rect(s);
	float px = ui_px(s);
	int in = px * 3, gap = px * 3, h = bar_buttons_h(s) - in;
	int x0 = b.x + in + (b.w - in * 2 + gap) * i / BB_COUNT, x1 = b.x + in + (b.w - in * 2 + gap) * (i + 1) / BB_COUNT - gap;
	return (SDL_Rect) { x0, b.y + bar_pick_h(s) + bar_status_h(s), x1 - x0, h };
}

/* the pin, a square in the top right corner */
static SDL_Rect bar_pin(int s) {
	SDL_Rect b = bar_rect(s);
	float px = ui_px(s);
	int in = px * 2, w = px * 10;
	return (SDL_Rect) { b.x + b.w - in - w, b.y + in, w, w };
}

/* the time, FPS and battery: three cells across the row above the buttons */
static SDL_Rect bar_cell(int s, int i) {
	SDL_Rect b = bar_rect(s);
	int x0 = b.x + b.w * i / 3, x1 = b.x + b.w * (i + 1) / 3;
	return (SDL_Rect) { x0, b.y + bar_pick_h(s), x1 - x0, bar_status_h(s) };
}

static int bar_button_at(int s, int x, int y) {
	SDL_Point pt = { x, y };
	for (int i = 0; i < BB_COUNT; ++i) {
		SDL_Rect r = bar_button(s, i);
		if (SDL_PointInRect(&pt, &r)) return i;
	}
	return -1;
}

static void save_prefs(void);

static const char* const icon_pin[] = {
	"...XXXXXX...",
	"...XXXXXX...",
	"....XXXX....",
	"....XXXX....",
	"....XXXX....",
	"...XXXXXX...",
	"..XXXXXXXX..",
	"XXXXXXXXXXXX",
	".....XX.....",
	".....XX.....",
	".....XX.....",
	"......X.....",
	NULL,
};

static void bar_close(void) {
	barOpen = false;
	barGesture = G_NONE;
	barExitArmed = 0;
}
/* L3, or the quick menu hotkey: up, as with a swipe; again: down */
static void bar_toggle(void) {
	if (barOpen) {
		bar_close();
		return;
	}
	barOpen = true;
	barSince = SDL_GetTicks();
}

/* pixel art ('X' rows, NULL-ended), as big as fits size, centred on cx, cy */
static void draw_icon(int s, const char* const* art, int cx, int cy, int size, SDL_Color c) {
	int rows = 0, cols = (int) strlen(art[0]);
	while (art[rows]) ++rows;
	int n = SDL_max(rows, cols), u = SDL_max(1, (size + n / 2) / n); /* (rounded: on 640x480 screens 2 rather than 1) */
	int x0 = cx - cols * u / 2, y0 = cy - rows * u / 2;
	for (int y = 0; y < rows; ++y) {
		for (int x = 0; x < cols;) {
			if (art[y][x] != 'X') {
				++x;
				continue;
			}
			int e = x;
			while (e < cols && art[y][e] == 'X') ++e;
			ui_fill(s, (SDL_Rect) { x0 + x * u, y0 + y * u, (e - x) * u, u }, c);
			x = e;
		}
	}
}

/* The hint at a game's start, "Swipe up for quick menu" under an arrow
 * pointing up, at the bottom of the touchscreen: it slides down and away 3
 * seconds in, or at the first touch, or as the bar or the menu comes up
 * (not shown with the bar pinned: it's up already; nor with quick_bar_hint
 * = 0). */
static int hintState;     /* 0 not yet shown, 1 up, 2 going */
static Uint32 hintSince;  /* its first frame */
static float hintShown;   /* 0 away .. 1 all the way up */

static void hint_dismiss(void) {
	if (hintState == 1) hintState = 2;
}

static void draw_hint(void) {
	if (hintState == 0) {
		if (cfg->barPinned || !cfg->barHint) {
			hintState = 2;
			return;
		}
		hintState = 1, hintSince = SDL_GetTicks(), hintShown = 1;
	}
	if (menuOpen) hintState = 2, hintShown = 0;
	if (hintState == 1 && (barOpen || SDL_GetTicks() - hintSince >= 3000)) hintState = 2;
	if (hintState == 2) hintShown = SDL_max(0.0f, hintShown - 0.12f);
	if (hintShown <= 0) return;
	int s = ui_touch_screen();
	float px = ui_px(s);
	static const char* text = N_("Swipe up for quick menu");
	int cap = px * 7, tw = ui_font_width(TF_BOLD, cap, text);
	int padX = px * 6, padY = px * 4, aw = px * 7, ah = px * 6, m = px * 6;
	int w = tw + padX * 2, h = cap + padY * 2;
	SDL_Rect r = screens[s].rect;
	int y = r.h - m - h + (int) ((1.0f - hintShown) * (h + ah + m + 1));
	SDL_Rect b = { (r.w - w) / 2, y, w, h };
	ui_fill_round(s, (SDL_Rect) { b.x - 1, b.y - 1, b.w + 2, b.h + 2 }, G_EDGE, px * 3 + 1);
	ui_fill_round(s, b, G_BG, px * 3);
	/* the arrow on top: the box's edge round it, filled as the box */
	int cx = r.w / 2;
	for (int i = 0; i <= ah; ++i) {
		int half = aw * i / SDL_max(1, ah);
		ui_fill(s, (SDL_Rect) { cx - half - 1, b.y - ah - 1 + i, half * 2 + 2, 1 }, G_EDGE);
		if (half * 2 - 2 > 0) ui_fill(s, (SDL_Rect) { cx - half + 1, b.y - ah - 1 + i, half * 2 - 2, 1 }, G_BG);
	}
	ui_fill(s, (SDL_Rect) { cx - aw + 1, b.y - 1, aw * 2 - 2, 2 }, G_BG); /* (open into the box) */
	ui_font_text(s, TF_BOLD, cx, b.y + b.h / 2, cap, text, G_TEXT, 0);
}

/* While rewinding (L2): "Rewinding..." after a rewind sign (two arrows
 * pointing left) in a box like the hint's, larger, in the middle of the
 * second screen (with one screen, of the game screen); "Can't rewind
 * further" at the rewind buffer's start. While fast-forwarding (R2) the
 * same box says "Fast forwarding..." after two arrows pointing right. It
 * fades as rewinding or fast-forwarding stops. */
int game_screen(void);
static int rewindBox;       /* 0 none, 1 rewinding, 2 at the buffer's start (this frame) */
static bool ffBox;          /* fast-forwarding (this frame) */
static float rewindShown;   /* 0 .. 1 */
static bool rewindRight;    /* the sign shown points right (fast-forward) */
static const char* rewindText = N_("Rewinding...");

static void draw_rewind(void) {
	if (rewindBox) {
		rewindShown = 1;
		rewindRight = false;
		rewindText = rewindBox == 2 ? N_("Can't rewind further"): N_("Rewinding...");
	} else if (ffBox) {
		rewindShown = 1;
		rewindRight = true;
		rewindText = N_("Fast forwarding...");
	} else {
		rewindShown = SDL_max(0.0f, rewindShown - 0.15f);
	}
	if (rewindShown <= 0) return;
	int s = nScreens > 1 ? 1 - game_screen() : 0;
	float px = ui_px(s);
	int cap = px * 10, tw = ui_font_width(TF_BOLD, cap, rewindText);
	int padX = px * 8, padY = px * 6, ih = cap * 3 / 2, iw = ih, gap = px * 6;
	int w = padX + iw + gap + tw + padX, h = SDL_max(cap, ih) + padY * 2;
	SDL_Rect r = screens[s].rect;
	SDL_Rect b = { (r.w - w) / 2, (r.h - h) / 2, w, h };
	if (rewindShown < 0.35f) return; /* (a key doesn't fade: it goes) */
	SDL_Color fg = ui_key_ink();
	ui_key(s, b, px * 4, KEY_PLAIN);
	/* the sign: two triangles side by side, pointing left (rewind) or right (fast-forward) */
	int ix = b.x + padX, cy = b.y + b.h / 2, half = ih / 2, tri = iw / 2;
	for (int t = 0; t < 2; ++t) {
		int x0 = ix + t * tri;
		for (int i = 0; i < tri; ++i) {
			int hh = half * (i + 1) / tri;
			int x = rewindRight ? x0 + tri - 1 - i : x0 + i;
			ui_fill(s, (SDL_Rect) { x, cy - hh, 1, hh * 2 }, fg);
		}
	}
	ui_font_text(s, TF_BOLD, ix + iw + gap + tw / 2, cy, cap, rewindText, fg, 0);
}

static void draw_bar(void) {
	barShown = barOpen ? SDL_min(1.0f, barShown + 0.25f) : SDL_max(0.0f, barShown - 0.25f);
	if (barShown <= 0) return;
	if (barExitArmed && SDL_GetTicks() - barExitArmed > 3000) barExitArmed = 0;
	int s = ui_touch_screen();
	float px = ui_px(s);
	SDL_Rect b = bar_rect(s);
	ui_fill_round(s, (SDL_Rect) { b.x - 1, b.y - 1, b.w + 2, b.h + 2 }, G_EDGE, px * 4 + 1);
	ui_fill_round(s, b, G_BG, px * 4);
	int cap = px * 6;
	char buf[3][24] = { "", "", "" };
	time_t now = time(NULL);
	struct tm tm;
	if (localtime_r(&now, &tm)) snprintf(buf[0], sizeof(buf[0]), "%02d:%02d", tm.tm_hour, tm.tm_min);
	int pct = battery_percent();
	if (fps > 0) snprintf(buf[1], sizeof(buf[1]), "%.1f FPS", fps);
	else snprintf(buf[1], sizeof(buf[1]), "\u2014 FPS"); /* (the first second) */
	if (pct >= 0) snprintf(buf[2], sizeof(buf[2]), "%d%%", pct);
	else snprintf(buf[2], sizeof(buf[2]), "\u2014"); /* (no battery found) */
	for (int i = 0; i < 3; ++i) {
		SDL_Rect c = bar_cell(s, i);
		int w = ui_font_width(TF_BOLD, cap, buf[i]), fc = w > c.w - px * 2 ? cap * (c.w - px * 2) / SDL_max(1, w) : cap;
		ui_font_text(s, TF_BOLD, c.x + c.w / 2, c.y + c.h / 2, fc, buf[i], G_TEXT, 0);
	}
	static const char* const* const art[BB_COUNT] = { icon_save, icon_load, icon_gear, icon_bug, icon_exit };
	bool lit = barLit >= 0 && SDL_GetTicks() - barLitAt < 1500;
	for (int i = 0; i < BB_COUNT; ++i) {
		SDL_Rect r = bar_button(s, i);
		bool down = barGesture == G_BTN && barBtn == i;
		bool armed = i == BB_EXIT && barExitArmed;
		bool en = i != BB_LOAD || file_exists(gp.quick);
		bool done = lit && barLit == i; /* (just saved, loaded or reported) */
		ui_fill_round(s, r, down || armed || done ? G_BTN_DOWN : G_BTN_C, px * 3);
		draw_icon(s, art[i], r.x + r.w / 2, r.y + r.h / 2, r.h * 7 / 10, !en ? G_DIS : done || armed ? G_LIT : G_TEXT);
	}
	/* the pin: grey, or white while pinned */
	SDL_Rect pin = bar_pin(s);
	ui_fill_round(s, pin, cfg->barPinned ? G_BTN_DOWN : G_BTN_C, px * 2);
	draw_icon(s, icon_pin, pin.x + pin.w / 2, pin.y + pin.h / 2, pin.h * 7 / 10, cfg->barPinned ? G_LIT : G_DIS);
	/* messages (the OSD's, while the bar covers where it's drawn) */
	const char* msg = ui_osd_text();
	if (msg) {
		SDL_Rect last = bar_button(s, 0);
		int my = last.y + last.h + bar_msg_h(s) / 2, mc = px * 5;
		int w = ui_font_width(TF_BOLD, mc, msg);
		if (w > b.w - px * 8) mc = mc * (b.w - px * 8) / SDL_max(1, w);
		ui_font_text(s, TF_BOLD, b.x + b.w / 2, my, mc, msg, G_TEXT, 0);
	}
}

static void set_page(int page) {
	curPage = page;
	nMenuItems = pageLen[page];
	memcpy(menuItems, pages[page], sizeof(int) * nMenuItems);
	nMenuRows = layout_rows(menuItems, nMenuItems, itemRow, itemSide);
	menuSel = 0;
	itemTop = 0;
	menuPressed = -1;
	menuNote[0] = 0;
	armedItem = -1;
	rowSel = page_is_rows(page) ? 1 : 0;
	rowTop = 0;
	rowPressed = -1;
}

/* D-pad up / down: the row above or below (round at the ends), the same side of a split row */
static void menu_move(int dir) {
	/* from the last row a tall button covers going down, the first going up */
	int from = dir > 0 ? itemRow[menuSel] + item_span(menuItems[menuSel]) - 1 : itemRow[menuSel];
	int row = (from + dir + nMenuRows) % nMenuRows, side = SDL_max(0, itemSide[menuSel]);
	int to = -1;
	for (int i = 0; i < nMenuItems; ++i) {
		if (row < itemRow[i] || row >= itemRow[i] + item_span(menuItems[i])) continue;
		if (to < 0 || itemSide[i] == side) to = i;
	}
	if (to >= 0) menuSel = to;
	items_scroll_to_sel();
}

static int tab_at(int scr, int x, int y, int n) {
	for (int t = 0; t < n; ++t) {
		SDL_Rect r = top_box(scr, t, n);
		if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) return t;
	}
	return -1;
}

static int item_at(int scr, int x, int y) {
	for (int i = 0; i < nMenuItems; ++i) {
		if (!item_shown(i)) continue;
		SDL_Rect r = item_rect(scr, i);
		if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) return i;
	}
	return -1;
}

/* Display > Shader: the label and the note under the buttons, by SH_* */
#define SHADER_ON (cfg->shader != SH_OFF)
static const char* const shaderLabels[SH_COUNT] = {
	N_("Shader: Off"), N_("Shader: LCD Grid"), N_("Shader: Scanlines"), N_("Shader: Soft Cells"),
	N_("Shader: Subpixels (Light)"), N_("Shader: Subpixels"), N_("Shader: Subpixels (Strong)"),
};
static const char* const shaderNotes[SH_COUNT] = {
	N_("The game's pixels as they are"),
	N_("Lines between the pixels, like a handheld's LCD"),
	N_("A dark line under each row of pixels"),
	N_("Each pixel a little darker toward its edges"),
	N_("Red, green and blue stripes, gentle: for dim screens"),
	N_("Red, green and blue stripes, like a handheld's LCD"),
	N_("Red, green and blue stripes, strong: for bright screens"),
};
static const char* shader_label(int sh) {
	return shaderLabels[sh >= 0 && sh < SH_COUNT ? sh : 0];
}
/* Display > LCD Ghosting: the game's last frame (as the emulator drew it),
 * to show each frame half and half with it */
static uint32_t ghostPrev[EMU_MAX_W * EMU_MAX_H], ghostOut[EMU_MAX_W * EMU_MAX_H];
static bool ghostHave;
static int ghostW, ghostH;
static uint64_t ghostFrame;
static const uint32_t* ghost_frame(const uint32_t* fb, int w, int h) {
	if (ghostHave && frameCount == ghostFrame && w == ghostW && h == ghostH) return ghostOut; /* (the same frame drawn again) */
	if (!ghostHave || w != ghostW || h != ghostH) {
		memcpy(ghostPrev, fb, (size_t) w * h * sizeof(uint32_t));
		ghostHave = true, ghostW = w, ghostH = h;
	}
	/* half this frame, half the one before: a sprite the game flickers
	 * every other frame shows half see-through, as on a slow LCD */
	for (int i = 0; i < w * h; ++i) {
		uint32_t a = fb[i], b = ghostPrev[i];
		ghostOut[i] = (a | b) - (((a ^ b) & 0xFEFEFEFEu) >> 1); /* the rounded-up average of each byte */
		ghostPrev[i] = a;
	}
	ghostFrame = frameCount;
	return ghostOut;
}

/* RRGGBB to 0xRRGGBB */
static uint32_t hex_rgb(const char* h) {
	return (uint32_t) strtoul(h, NULL, 16) & 0xFFFFFF;
}

/* ---------------------------------------------------------------- Display > Overlays
 *
 * A screen of its own (like Cheats): Back and the title on top, then the
 * rows: Overlays (Off / Generated / Custom (in /overlays)), Which Screen
 * (not when Off), and for Generated the logo, the two bezel colours and
 * Reset Bezels. D-pad / A (LEFT / RIGHT also cycle), B back; touch taps.
 * The colours open the picker; its Y (or a tap on the hex value) the hex
 * keypad. The game screen isn't dimmed meanwhile, so the bezel shows as it
 * will be, the picker's colour too as it's picked (draw_bezel). */
static bool ovlOpen;
static int ovlSel, ovlPress = -1;
enum { OR_MODE, OR_SCREENS, OR_LOGO, OR_INNER, OR_OUTER, OR_RESET };

static int ovl_rows(int* rows) {
	int n = 0;
	rows[n++] = OR_MODE;
	if (cfg->overlayMode != OVM_OFF) rows[n++] = OR_SCREENS;
	if (cfg->overlayMode == OVM_GENERATED) rows[n++] = OR_LOGO, rows[n++] = OR_INNER, rows[n++] = OR_OUTER, rows[n++] = OR_RESET;
	return n;
}
static SDL_Rect ovl_row_rect(int scr, int i) {
	MenuGeom g = menu_geom(scr);
	return (SDL_Rect) { g.margin, g.top + i * (g.h + g.gap), g.w, g.h };
}
static const char* ovl_row_label(int row) {
	static const char* const modes[3] = { N_("Overlays: Off"), N_("Overlays: Generated"), N_("Overlays: Custom (in /overlays)")};
	switch (row) {
	case OR_MODE: return modes[SDL_clamp(cfg->overlayMode, 0, 2)];
	case OR_SCREENS: return cfg->overlays == OVL_TOP ? N_("Which Screen: Top Only"): cfg->overlays == OVL_BOTTOM ? N_("Which Screen: Bottom Only"): N_("Which Screen: Both");
	case OR_LOGO: return cfg->bezelLogo ? N_("SNES DS Logo: Show"): N_("SNES DS Logo: Hide");
	case OR_INNER: return N_("Inner Bezel Color");
	case OR_OUTER: return N_("Outer Bezel Color");
	default: return N_("Reset Bezels");
	}
}

/* -------- the colour picker: hue / saturation / value, as the bezel shows it */
static int pickWhich = -1; /* 0 inner, 1 outer; -1 closed */
static float pickH, pickS, pickV;
static int pickPreset = -1; /* the last preset X stepped to */
static int pickDrag;        /* finger on 1 the square, 2 the hue strip */
static SDL_Texture* pickSqTex[2], *pickHueTex[2];
static float pickSqHue = -1;
static int pickTexW, pickTexH;
static const uint32_t pickPresets[2][8] = {
	{ 0x1A1A1A, 0x000000, 0x2B2B2B, 0x3C3C3C, 0x102060, 0x2A1F55, 0x3A1020, 0xE8E6E3 },
	{ 0x102060, 0x4B3A8C, 0x000000, 0x9FB7C9, 0xC9C9C9, 0xB03A7A, 0xD66A1E, 0xE8E6E3 },
};

static uint32_t hsv_rgb(float h, float s, float v) {
	float c = v * s, x = c * (1 - fabsf(fmodf(h / 60, 2) - 1)), m = v - c, r = 0, g = 0, b = 0;
	if (h < 60) r = c, g = x;
	else if (h < 120) r = x, g = c;
	else if (h < 180) g = c, b = x;
	else if (h < 240) g = x, b = c;
	else if (h < 300) r = x, b = c;
	else r = c, b = x;
	return ((uint32_t) lroundf((r + m) * 255) << 16) | ((uint32_t) lroundf((g + m) * 255) << 8) | (uint32_t) lroundf((b + m) * 255);
}
static void rgb_hsv(uint32_t rgb, float* h, float* s, float* v) {
	float r = ((rgb >> 16) & 0xFF) / 255.0f, g = ((rgb >> 8) & 0xFF) / 255.0f, b = (rgb & 0xFF) / 255.0f;
	float mx = fmaxf(r, fmaxf(g, b)), mn = fminf(r, fminf(g, b)), d = mx - mn;
	float hh = 0;
	if (d > 0) {
		if (mx == r) hh = fmodf((g - b) / d, 6);
		else if (mx == g) hh = (b - r) / d + 2;
		else hh = (r - g) / d + 4;
		hh *= 60;
		if (hh < 0) hh += 360;
	}
	*h = hh, *s = mx > 0 ? d / mx : 0, *v = mx;
}
static uint32_t pick_rgb(void) {
	return hsv_rgb(pickH, pickS, pickV);
}
static SDL_Color rgb_color(uint32_t rgb) {
	return (SDL_Color) { (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, 255 };
}
static void pick_open(int which) {
	pickWhich = which;
	rgb_hsv(hex_rgb(which ? cfg->bezelOuter : cfg->bezelInner), &pickH, &pickS, &pickV);
	pickPreset = -1;
	pickDrag = 0;
}
static void pick_close(bool keep) {
	if (keep && pickWhich >= 0) {
		snprintf(pickWhich ? cfg->bezelOuter : cfg->bezelInner, 8, "%06X", (unsigned) pick_rgb());
		save_prefs();
	}
	pickWhich = -1;
	pickDrag = 0;
}

typedef struct {
	SDL_Rect title, sq, hue, now, next, presets[8], hex, cancel, ok;
	int hintY, cap, small;
} PickGeom;
static PickGeom pick_geom(int scr) {
	MenuGeom mg = menu_geom(scr);
	SDL_Rect r = screens[scr].rect;
	PickGeom p;
	int m = mg.margin, gap = mg.gap * 2;
	p.cap = mg.cap;
	p.small = mg.cap * 2 / 3;
	p.title = (SDL_Rect) { m, mg.tabY, mg.w, mg.h };
	int bh = mg.h, hw = (mg.w - mg.gap) / 2;
	p.cancel = (SDL_Rect) { m, r.h - m - bh, hw, bh };
	p.ok = (SDL_Rect) { m + mg.w - hw, r.h - m - bh, hw, bh };
	p.hintY = p.cancel.y - mg.gap * 2 - p.small;
	int top = p.title.y + p.title.h + gap, bot = p.hintY - p.small - mg.gap;
	int side = SDL_min(bot - top, mg.w * 9 / 20);
	p.sq = (SDL_Rect) { m, top, side, side };
	p.hue = (SDL_Rect) { m + side + gap, top, SDL_max(8, side / 9), side };
	int rx = p.hue.x + p.hue.w + gap, rw = m + mg.w - rx;
	int prevH = side / 5;
	p.now = (SDL_Rect) { rx, top, rw / 2, prevH };
	p.next = (SDL_Rect) { rx + rw / 2, top, rw - rw / 2, prevH };
	int hexH = SDL_min(mg.h, side / 5);
	p.hex = (SDL_Rect) { rx, top + side - hexH, rw, hexH };
	int py = p.now.y + prevH + gap, ph = (p.hex.y - gap - py - mg.gap) / 2, pw = (rw - mg.gap * 3) / 4;
	for (int i = 0; i < 8; ++i) p.presets[i] = (SDL_Rect) { rx + (i % 4) * (pw + mg.gap), py + (i / 4) * (ph + mg.gap), pw, ph };
	return p;
}
static bool in_rect(SDL_Rect r, int x, int y) {
	return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}
static void pick_touch(int scr, int x, int y) {
	PickGeom p = pick_geom(scr);
	if (pickDrag == 1) {
		pickS = SDL_clamp((float) (x - p.sq.x) / p.sq.w, 0.0f, 1.0f);
		pickV = SDL_clamp(1 - (float) (y - p.sq.y) / p.sq.h, 0.0f, 1.0f);
	} else if (pickDrag == 2) {
		pickH = SDL_clamp((float) (y - p.hue.y) / p.hue.h * 360, 0.0f, 359.9f);
	}
}
/* the textures: the square for the current hue (made again when it moves), the hue strip once */
static void pick_textures(void) {
	const int n = 64;
	if (!pickHueTex[0] && !pickHueTex[1]) {
		SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, 1, 360, 32, SDL_PIXELFORMAT_RGBA32);
		if (s) {
			uint8_t* px = s->pixels;
			for (int y = 0; y < 360; ++y) {
				uint32_t c = hsv_rgb((float) y, 1, 1);
				uint8_t* q = px + y * s->pitch;
				q[0] = c >> 16, q[1] = c >> 8, q[2] = c, q[3] = 255;
			}
			int w, h;
			ui_load_surface(s, pickHueTex, &w, &h, true);
		}
	}
	if (pickSqHue != pickH) {
		ui_free_image(pickSqTex);
		pickSqHue = pickH;
		SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, n, n, 32, SDL_PIXELFORMAT_RGBA32);
		if (s) {
			uint8_t* px = s->pixels;
			for (int y = 0; y < n; ++y) {
				for (int x = 0; x < n; ++x) {
					uint32_t c = hsv_rgb(pickH, x / (float) (n - 1), 1 - y / (float) (n - 1));
					uint8_t* q = px + y * s->pitch + x * 4;
					q[0] = c >> 16, q[1] = c >> 8, q[2] = c, q[3] = 255;
				}
			}
			ui_load_surface(s, pickSqTex, &pickTexW, &pickTexH, true);
		}
	}
}
/* a ring marker centred on (x, y) */
static void draw_ring(int scr, int x, int y, int r, SDL_Color fill) {
	ui_fill_round(scr, (SDL_Rect) { x - r - 2, y - r - 2, r * 2 + 4, r * 2 + 4 }, (SDL_Color) { 0, 0, 0, 255 }, r + 2);
	ui_fill_round(scr, (SDL_Rect) { x - r, y - r, r * 2, r * 2 }, (SDL_Color) { 255, 255, 255, 255 }, r);
	int k = SDL_max(1, r * 2 / 3);
	ui_fill_round(scr, (SDL_Rect) { x - k, y - k, k * 2, k * 2 }, fill, k);
}
/* a small button prompt ("A", "L1") and its words after it; returns the x after them */
static int draw_btn_hint(int scr, int x, int cy, int cap, const char* btn, const char* words) {
	int bw = ui_label_width(scr, cap, btn) + cap;
	ui_fill_round(scr, (SDL_Rect) { x, cy - cap, bw, cap * 2 }, C_TEXT, cap / 2);
	ui_label(scr, x + bw / 2, cy, cap, btn, (SDL_Color) { 20, 16, 36, 255 });
	ui_font_text(scr, TF_BODY, x + bw + cap / 2, cy, cap, words, C_DIS, -1);
	return x + bw + cap / 2 + ui_font_width(TF_BODY, cap, words) + cap * 2;
}
static int hint_width(int scr, int cap, const char* btn, const char* words) {
	return ui_label_width(scr, cap, btn) + cap + cap / 2 + ui_font_width(TF_BODY, cap, words) + cap * 2;
}

/* -------- the hex keypad (Y in the picker, or a tap on the hex value) */
static bool hexOpen;
static char hexText[8];
static int hexLen, hexRow, hexCol, hexPress = -1;
static const char* const hexKeys[3][8] = {
	{ "0", "1", "2", "3", "4", "5", "6", "7" },
	{ "8", "9", "A", "B", "C", "D", "E", "F" },
	{ N_("Del"), N_("Done")},
};
static int hex_row_len(int r) {
	return r == 2 ? 2 : 8;
}
static SDL_Rect hex_field(int scr) {
	MenuGeom mg = menu_geom(scr);
	return (SDL_Rect) { mg.margin, mg.top, mg.w, mg.h * 3 / 2 };
}
static SDL_Rect hex_key_rect(int scr, int r, int c) {
	MenuGeom mg = menu_geom(scr);
	SDL_Rect f = hex_field(scr);
	int top = f.y + f.h + mg.gap * 2, rowH = (screens[scr].rect.h - mg.margin - top - mg.gap * 2) / 3;
	int n = hex_row_len(r), kw = (mg.w - mg.gap * (n - 1)) / n;
	return (SDL_Rect) { mg.margin + c * (kw + mg.gap), top + r * (rowH + mg.gap), c == n - 1 ? mg.margin + mg.w - (mg.margin + c * (kw + mg.gap)) : kw, rowH };
}
static bool hex_key_at(int scr, int x, int y, int* r, int* c) {
	for (int i = 0; i < 3; ++i)
		for (int j = 0; j < hex_row_len(i); ++j)
			if (in_rect(hex_key_rect(scr, i, j), x, y)) return *r = i, *c = j, true;
	return false;
}
static void hex_open(void) {
	hexOpen = true;
	hexLen = 0, hexText[0] = 0;
	hexRow = 0, hexCol = 0;
	hexPress = -1;
}
/* Done: six digits (or three, #ABC = #AABBCC) become the picker's colour */
static void hex_done(void) {
	if (hexLen != 6 && hexLen != 3) return;
	char full[sizeof(hexText)];
	if (hexLen == 3) snprintf(full, sizeof(full), "%c%c%c%c%c%c", hexText[0], hexText[0], hexText[1], hexText[1], hexText[2], hexText[2]);
	else snprintf(full, sizeof(full), "%s", hexText);
	rgb_hsv(hex_rgb(full), &pickH, &pickS, &pickV);
	hexOpen = false;
}
static void hex_press(int r, int c) {
	if (r == 2) {
		if (c == 0) {
			if (hexLen) hexText[--hexLen] = 0;
		} else {
			hex_done();
		}
		return;
	}
	if (hexLen < 6) hexText[hexLen++] = hexKeys[r][c][0], hexText[hexLen] = 0;
}
static void hex_move(int dr, int dc) {
	if (dc) {
		int n = hex_row_len(hexRow);
		hexCol = (hexCol + dc + n) % n;
		return;
	}
	int from = hex_row_len(hexRow);
	hexRow = (hexRow + dr + 3) % 3;
	int n = hex_row_len(hexRow);
	hexCol = SDL_min(n - 1, hexCol * n / from);
}

/* -------- drawing */
static void draw_ovl_menu(int scr) {
	MenuGeom mg = menu_geom(scr);
	int rad = mg.rad, cap = mg.cap;
	SDL_Rect b = tab_rect(scr, 0), t = tab_rect(scr, 1);
	ui_fill_round(scr, b, C_BTN, rad);
	ui_label(scr, b.x + b.w / 2, b.y + b.h / 2, cap, "Back", C_TEXT);
	ui_fill_round(scr, t, C_BTN_EDGE, rad);
	ui_label(scr, t.x + t.w / 2, t.y + t.h / 2, cap, "Overlays", (SDL_Color) { 20, 16, 36, 255 });
	int rows[8], n = ovl_rows(rows);
	if (ovlSel >= n) ovlSel = n - 1;
	for (int i = 0; i < n; ++i) {
		SDL_Rect r = ovl_row_rect(scr, i);
		bool sel = i == ovlSel;
		ui_key(scr, r, rad, sel ? KEY_SEL : KEY_PLAIN);
		const char* label = ovl_row_label(rows[i]);
		int lc = SDL_min(cap, cap * (r.w - mg.pad * 2) / SDL_max(1, ui_label_width(scr, cap, label)));
		if (rows[i] == OR_INNER || rows[i] == OR_OUTER) {
			/* the label, its swatch and the hex value, centred together */
			const char* hex = rows[i] == OR_INNER ? cfg->bezelInner : cfg->bezelOuter;
			char hv[10];
			snprintf(hv, sizeof(hv), "#%s", hex);
			int sw = mg.h * 3 / 5, hc = cap * 4 / 5, gap = mg.pad / 2;
			int lw = ui_label_width(scr, cap, label), hw = ui_label_width(scr, hc, hv);
			int x = r.x + (r.w - (lw + gap + sw + gap + hw)) / 2, cy = r.y + r.h / 2;
			ui_label(scr, x + lw / 2, cy, cap, label, C_TEXT);
			x += lw + gap;
			int e2 = SDL_max(2, sw / 12);
			ui_fill_round(scr, (SDL_Rect) { x, cy - sw / 2, sw, sw }, C_TEXT, SDL_max(2, sw / 6));
			ui_fill_round(scr, (SDL_Rect) { x + e2, cy - sw / 2 + e2, sw - e2 * 2, sw - e2 * 2 }, rgb_color(hex_rgb(hex)), SDL_max(1, sw / 8));
			x += sw + gap;
			ui_label(scr, x + hw / 2, cy, hc, hv, (SDL_Color) { 185, 179, 211, 255 });
		} else {
			ui_label(scr, r.x + r.w / 2, r.y + r.h / 2, lc, label, C_TEXT);
		}
	}
	if (cfg->overlayMode == OVM_CUSTOM) {
		SDL_Rect last = ovl_row_rect(scr, n - 1);
		ui_font_wrap(scr, TF_BODY, mg.margin + mg.pad, last.y + last.h + mg.gap * 2, mg.w - mg.pad * 2, mg.noteCap,
		             "Your own PNGs in snes-ds/overlays/. A screen without one gets the generated bezel in its default colors.", C_DIS);
	}
}

static void draw_picker(int scr) {
	PickGeom p = pick_geom(scr);
	MenuGeom mg = menu_geom(scr);
	SDL_Rect r = screens[scr].rect;
	ui_fill(scr, (SDL_Rect) { 0, 0, r.w, r.h }, (SDL_Color) { 12, 10, 22, 255 });
	ui_fill_round(scr, p.title, C_BTN_EDGE, mg.rad);
	ui_label(scr, p.title.x + p.title.w / 2, p.title.y + p.title.h / 2, p.cap, pickWhich ? "Outer Bezel Color" : "Inner Bezel Color", (SDL_Color) { 20, 16, 36, 255 });
	pick_textures();
	ui_image(scr, pickSqTex, pickTexW, pickTexH, "stretch", &p.sq);
	ui_image(scr, pickHueTex, 1, 360, "stretch", &p.hue);
	uint32_t now = pick_rgb();
	int rr = SDL_max(6, p.sq.w / 28);
	draw_ring(scr, p.sq.x + (int) lroundf(pickS * p.sq.w), p.sq.y + (int) lroundf((1 - pickV) * p.sq.h), rr, rgb_color(now));
	int hy = p.hue.y + (int) lroundf(pickH / 360 * p.hue.h);
	ui_fill_round(scr, (SDL_Rect) { p.hue.x - 6, hy - 6, p.hue.w + 12, 12 }, (SDL_Color) { 0, 0, 0, 255 }, 5);
	ui_fill_round(scr, (SDL_Rect) { p.hue.x - 4, hy - 4, p.hue.w + 8, 8 }, (SDL_Color) { 255, 255, 255, 255 }, 3);
	/* now / new */
	uint32_t was = hex_rgb(pickWhich ? cfg->bezelOuter : cfg->bezelInner);
	ui_fill(scr, p.now, rgb_color(was));
	ui_fill(scr, p.next, rgb_color(now));
	ui_font_text(scr, TF_BOLD, p.now.x + mg.pad / 2, p.now.y + p.now.h - p.small, p.small, "Now", C_TEXT, -1);
	ui_font_text(scr, TF_BOLD, p.next.x + mg.pad / 2, p.next.y + p.next.h - p.small, p.small, "New", C_TEXT, -1);
	for (int i = 0; i < 8; ++i) {
		uint32_t c = pickPresets[pickWhich][i];
		SDL_Rect q = p.presets[i];
		bool on = c == now;
		int e = on ? SDL_max(3, q.h / 10) : 2;
		ui_fill_round(scr, q, on ? C_TEXT : (SDL_Color) { 58, 54, 80, 255 }, mg.rad);
		ui_fill_round(scr, (SDL_Rect) { q.x + e, q.y + e, q.w - e * 2, q.h - e * 2 }, rgb_color(c), SDL_max(1, mg.rad - e));
	}
	char hv[12];
	snprintf(hv, sizeof(hv), "#%06X", (unsigned) now);
	ui_fill_round(scr, p.hex, (SDL_Color) { 30, 26, 50, 255 }, mg.rad);
	ui_label(scr, p.hex.x + p.hex.w / 2, p.hex.y + p.hex.h / 2, SDL_min(p.cap, p.hex.h * 2 / 5), hv, C_TEXT);
	/* the buttons, centred in a line */
	const char* btn[4] = { N_("D-pad"), "L1 R1", "X", "Y" };
	const char* words[4] = { N_("color"), N_("hue"), N_("presets"), N_("hex")};
	int hc = p.small, tw = 0;
	for (int i = 0; i < 4; ++i) tw += hint_width(scr, hc, btn[i], words[i]);
	if (tw > mg.w) hc = hc * mg.w / tw, tw = mg.w;
	int x = (r.w - tw) / 2;
	for (int i = 0; i < 4; ++i) x = draw_btn_hint(scr, x, p.hintY, hc, btn[i], words[i]);
	ui_key(scr, p.cancel, mg.rad, KEY_PLAIN);
	ui_label(scr, p.cancel.x + p.cancel.w / 2, p.cancel.y + p.cancel.h / 2, p.cap, "Cancel", C_TEXT);
	ui_key(scr, p.ok, mg.rad, KEY_SEL);
	ui_label(scr, p.ok.x + p.ok.w / 2, p.ok.y + p.ok.h / 2, p.cap, "OK", C_TEXT);
}

static void draw_hex(int scr) {
	MenuGeom mg = menu_geom(scr);
	SDL_Rect r = screens[scr].rect;
	ui_fill(scr, (SDL_Rect) { 0, 0, r.w, r.h }, (SDL_Color) { 12, 10, 22, 255 });
	SDL_Rect b = tab_rect(scr, 0), t = tab_rect(scr, 1);
	ui_fill_round(scr, b, C_BTN, mg.rad);
	ui_label(scr, b.x + b.w / 2, b.y + b.h / 2, mg.cap, "Back", C_TEXT);
	ui_fill_round(scr, t, C_BTN_EDGE, mg.rad);
	ui_label(scr, t.x + t.w / 2, t.y + t.h / 2, mg.cap, "Hex Color", (SDL_Color) { 20, 16, 36, 255 });
	SDL_Rect f = hex_field(scr);
	ui_fill_round(scr, f, (SDL_Color) { 30, 26, 50, 255 }, mg.rad);
	char shown[16];
	snprintf(shown, sizeof(shown), "#%s%s", hexText, hexLen < 6 && (SDL_GetTicks() / 500) & 1 ? "|" : "");
	ui_label(scr, f.x + f.w / 2, f.y + f.h / 2, mg.cap * 3 / 2, shown, hexLen ? C_TEXT : C_DIS);
	bool canDone = hexLen == 6 || hexLen == 3;
	for (int i = 0; i < 3; ++i) {
		for (int j = 0; j < hex_row_len(i); ++j) {
			SDL_Rect k = hex_key_rect(scr, i, j);
			bool sel = i == hexRow && j == hexCol;
			ui_key(scr, k, mg.rad, sel ? KEY_SEL : KEY_PLAIN);
			bool dim = i == 2 && j == 1 && !canDone;
			ui_label(scr, k.x + k.w / 2, k.y + k.h / 2, SDL_min(mg.cap, k.h * 2 / 5), hexKeys[i][j], dim ? C_DIS : C_TEXT);
		}
	}
}

/* -------- what a row does (dir: -1 LEFT, else forward) */
static void ovl_activate(int i, int dir) {
	int rows[8], n = ovl_rows(rows);
	if (i < 0 || i >= n) return;
	switch (rows[i]) {
	case OR_MODE:
		cfg->overlayMode = (SDL_clamp(cfg->overlayMode, 0, 2) + (dir < 0 ? 2 : 1)) % 3;
		break;
	case OR_SCREENS: {
		static const int order[3] = { OVL_BOTH, OVL_TOP, OVL_BOTTOM };
		int k = cfg->overlays == OVL_TOP ? 1 : cfg->overlays == OVL_BOTTOM ? 2 : 0;
		cfg->overlays = order[(k + (dir < 0 ? 2 : 1)) % 3];
		break;
	}
	case OR_LOGO:
		cfg->bezelLogo = !cfg->bezelLogo;
		break;
	case OR_INNER:
	case OR_OUTER:
		pick_open(rows[i] == OR_OUTER);
		return;
	case OR_RESET:
		cfg->bezelLogo = true;
		snprintf(cfg->bezelInner, sizeof(cfg->bezelInner), "%06X", BEZEL_INNER_DEFAULT);
		snprintf(cfg->bezelOuter, sizeof(cfg->bezelOuter), "%06X", BEZEL_OUTER_DEFAULT);
		break;
	}
	commit_current("overlays"); /* (the layer in force) */
	commit_current("overlay_screens");
	commit_current("bezel_logo");
	save_prefs();
}
static void ovl_close_all(void) {
	ovlOpen = false;
	hexOpen = false;
	pickWhich = -1;
	pickDrag = 0;
}

/* ---------------------------------------------------------------- face buttons
 *
 * GEN-10: which GBA button each face button gives the game, by position
 * (right, bottom, top, left): GBA A / B / L / R, Start, Select, Turbo A /
 * Turbo B or nothing. Controls > the four button rows (button_right ... in
 * snes-ds.cfg, for this game or for all games); with none chosen, ab_swap's
 * layout. Before v1.5.4 a game's choices were kept in <game>.buttons: read
 * once into the game's own settings (fb_load). Only the game's keys change
 * (run loop, the GBA layout's branch): SNES DS's own screens keep B_A / B_B.
 * In games with a layout picker (profile `controls`), the SNES / four-button
 * layout decides the face buttons wherever it's in use. */
enum { FB_RIGHT, FB_BOTTOM, FB_TOP, FB_LEFT, FB_FACES, FB_L = FB_FACES, FB_R, FB_UP, FB_DOWN, FB_PADLEFT, FB_PADRIGHT, FB_COUNT };
enum { FG_A, FG_B, FG_X, FG_Y, FG_L, FG_R, FG_UP, FG_DOWN, FG_LEFT, FG_RIGHT, FG_START, FG_SELECT, FG_TURBO_A, FG_TURBO_B, FG_NONE, FG_COUNT };
static const char* const fbNames[FB_COUNT] = { "right", "bottom", "top", "left", "l", "r", "dpad_up", "dpad_down", "dpad_left", "dpad_right" };
static const char* const fgNames[FG_COUNT] = { "a", "b", "x", "y", "l", "r", "up", "down", "left", "right", "start", "select", "turbo_a", "turbo_b", "none" };
static const char* const fgLabels[FG_COUNT] = { "SNES A", "SNES B", "SNES X", "SNES Y", "SNES L", "SNES R", "SNES Up", "SNES Down", "SNES Left", "SNES Right", "Start", "Select", "Turbo A", "Turbo B", "Nothing" };
static int fbMap[FB_COUNT]; /* each face button's FG_ */
static bool fbSet;          /* a choice in force (else ab_swap's) */

/* from the settings in force (called again whenever one changes) */
static void fb_default(void) {
	/* the SNES pad's layout by position: A right, B bottom, X top, Y left
	 * (ab_swap: A and B swapped, and X and Y) */
	fbMap[FB_RIGHT] = cfg->abSwap ? FG_B : FG_A;
	fbMap[FB_BOTTOM] = cfg->abSwap ? FG_A : FG_B;
	fbMap[FB_TOP] = cfg->abSwap ? FG_Y : FG_X;
	fbMap[FB_LEFT] = cfg->abSwap ? FG_X : FG_Y;
	fbMap[FB_L] = FG_L, fbMap[FB_R] = FG_R, fbMap[FB_UP] = FG_UP, fbMap[FB_DOWN] = FG_DOWN, fbMap[FB_PADLEFT] = FG_LEFT, fbMap[FB_PADRIGHT] = FG_RIGHT;
	fbSet = false;
	for (int i = 0; i < FB_COUNT; ++i)
		for (int j = 0; j < FG_COUNT && cfg->faceBtn[i][0]; ++j)
			if (!strcmp(cfg->faceBtn[i], fgNames[j])) fbMap[i] = j, fbSet = true;
}

/* "right GBA A, bottom GBA B, ..." for the log and bug reports */
static const char* fb_describe(void) {
	static char s[320];
	s[0] = 0;
	for (int i = 0; i < FB_COUNT; ++i)
		snprintf(s + strlen(s), sizeof(s) - strlen(s), "%s%s %s", i ? ", " : "", fbNames[i], fgLabels[fbMap[i]]);
	return s;
}

static void game_keys_reload(void);
static void fb_load(void) {
	/* a game's <game>.buttons from before v1.5.4: moved into its own settings */
	char old[1400];
	snprintf(old, sizeof(old), "%s/%s.buttons", cfg->stateDir, gp.base);
	FILE* f = fopen(old, "r");
	if (f) {
		char k[16], v[16], lines[FB_COUNT][2][24];
		const char* kv[FB_COUNT][2];
		int n = 0;
		while (fscanf(f, "%15s %15s", k, v) == 2 && n < FB_COUNT) {
			for (int i = 0; i < FB_COUNT; ++i) {
				if (strcmp(k, fbNames[i])) continue;
				snprintf(lines[n][0], 24, "button_%s", fbNames[i]);
				snprintf(lines[n][1], 24, "%s", v);
				kv[n][0] = lines[n][0], kv[n][1] = lines[n][1];
				++n;
			}
		}
		fclose(f);
		if (n) {
			config_kv_update(gp.settings, "# this game's own settings: they win over the ones for all games", n, kv);
			config_load_game_file(cfg, gp.settings); /* (put over the settings now) */
			fprintf(stderr, "buttons: %s.buttons moved into %s\n", gp.base, gp.settings);
		}
		remove(old);
		game_keys_reload();
	}
	fb_default();
	if (fbSet) fprintf(stderr, "buttons: %s\n", fb_describe());
}

/* the SNES keys the buttons give the game */
static uint32_t fb_keys(bool faces) {
	static const uint32_t key[FG_COUNT] = { K_A, K_B, K_X, K_Y, K_L, K_R, K_UP, K_DOWN, K_LEFT, K_RIGHT, K_START, K_SELECT, K_A, K_B, 0 };
	/* Turbo A / B (#55): the key goes down and up turbo_rate times a second (60 frames a second) */
	int period = SDL_clamp(60 / SDL_max(1, cfg->turboRate), 2, 12);
	bool turboOn = (frameCount % (uint64_t) period) < (uint64_t) (period / 2);
	/* (ui.c puts the bottom button on B_A with ab_swap: by position again) */
	int phys[FB_COUNT] = { cfg->abSwap ? B_B : B_A, cfg->abSwap ? B_A : B_B, B_X, B_Y, B_L, B_R, B_UP, B_DOWN, B_LEFT, B_RIGHT };
	uint32_t keys = 0;
	/* faces only, or the shoulders and the D-pad (only = false) */
	for (int i = faces ? 0 : FB_FACES; i < (faces ? FB_FACES : FB_COUNT); ++i)
		if (HELD(phys[i]) && (fbMap[i] != FG_TURBO_A && fbMap[i] != FG_TURBO_B ? true : turboOn)) keys |= key[fbMap[i]];
	return keys;
}

static const char* scale_label(const char* scale) {
	return !strcmp(scale, "fit")       ? N_("Scale: Full Width"):
	       !strcmp(scale, "zoom")      ? N_("Scale: Zoom"):
	       !strcmp(scale, "stretch")   ? N_("Scale: Stretch"): N_("Scale: Pixel Perfect");
}

/* ---------------------------------------------------------------- settings pages
 *
 * Display, Screens, Controls, Speed and Map are rows of settings (config.c's
 * table), each changed for this game (A, or a tap with "This game" lit) or
 * for all games (Y, or "All games" lit): this game's go to states/<game>.cfg,
 * all games' to states/prefs.cfg. A row with a value of its own for this game
 * wears a "Game" tag; X (or a tap on the tag) takes it away. Row 0 is the
 * This game / All games bar. */
static char gameKeys[48][40]; /* the keys states/<game>.cfg holds */
static int nGameKeys;
static bool game_has_key(const char* k) {
	for (int i = 0; i < nGameKeys; ++i)
		if (!strcmp(gameKeys[i], k)) return true;
	return false;
}
static bool game_has(const Setting* st) {
	return st->field == SF_FF ? game_has_key("fast_forward") || game_has_key("ff_speed") : game_has_key(st->key);
}

/* row i (0 the bar, 1... the settings) on screen; a row scrolled off sits above or below the rows shown */
static SDL_Rect srow_rect(int scr, int i) {
	MenuGeom g = menu_geom(scr);
	int slot = i == 0 ? 0 : i - rowTop;
	return (SDL_Rect) { g.margin, g.top + slot * (g.h + g.gap), g.w, g.h };
}
static bool srow_shown(int i) {
	return i == 0 || (i > rowTop && i <= rowTop + ROW_VIS);
}
/* the selected row kept on screen */
static void rows_scroll_to_sel(void) {
	if (rowSel >= 1 && rowSel <= rowTop) rowTop = rowSel - 1;
	if (rowSel > rowTop + ROW_VIS) rowTop = rowSel - ROW_VIS;
	rowTop = SDL_clamp(rowTop, 0, SDL_max(0, rowN[curPage] - ROW_VIS));
}
static SDL_Rect scope_rect(int scr, int half) {
	MenuGeom g = menu_geom(scr);
	SDL_Rect r = srow_rect(scr, 0);
	int hw = (r.w - g.gap) / 2;
	return (SDL_Rect) { r.x + half * (r.w - hw), r.y, hw, r.h };
}
static SDL_Rect rowTag[ROW_MAX + 1], rowBack[ROW_MAX + 1]; /* where the last drawing put each row's Game tag and left arrow (for touch) */
static bool rowTagOn[ROW_MAX + 1];

/* why a row can't be changed here, or NULL */
static const char* row_disabled(const char* key) {
	if (!strcmp(key, "screen_match") && !ui_screens_differ()) return N_("Your screens are the same shape: nothing to match");
	if (!strcmp(key, "screen_placement") && nScreens != 2) return N_("Only one screen: nothing to move");
	return NULL;
}

static void draw_rows_page(int scr, const MenuGeom* mg) {
	int cap = mg->cap, rad = mg->rad, n = rowN[curPage];
	/* the bar */
	for (int h = 0; h < 2; ++h) {
		SDL_Rect r = scope_rect(scr, h);
		bool on = (h == 1) == scopeAll;
		ui_key(scr, r, rad, on ? (rowSel == 0 ? KEY_SEL : KEY_LIT) : KEY_PLAIN);
		ui_label(scr, r.x + r.w / 2, r.y + r.h / 2, cap, h ? "All games" : "This game", on ? (rowSel == 0 ? C_TEXT : (SDL_Color) { 20, 16, 36, 255 }) : C_DIS);
	}
	const char* note = NULL;
	bool noteGold = false;
	rowTop = SDL_clamp(rowTop, 0, SDL_max(0, n - ROW_VIS));
	for (int i = 1; i <= n; ++i) {
		rowTagOn[i] = false;
		if (!srow_shown(i)) continue;
		SDL_Rect r = srow_rect(scr, i);
		bool sel = i == rowSel;
		ui_key(scr, r, rad, sel ? KEY_SEL : KEY_PLAIN);
		const char* key = rowKeys[curPage][i - 1];
		int cy = r.y + r.h / 2, lc = cap * 9 / 10;
		if (key[0] == '@') {
			ui_font_text(scr, TF_BOLD, r.x + mg->pad, cy, lc, "Overlay colors and logo", C_TEXT, -1);
			ui_font_text(scr, TF_BOLD, r.x + r.w - mg->pad, cy, cap, "›", sel ? C_TEXT : C_DIS, 1);
			if (sel) note = N_("Opens the bezel's own page, for whichever the game or all games have");
			continue;
		}
		const Setting* st = config_setting(key);
		if (!st) continue;
		const char* off = row_disabled(key);
		char val[48];
		config_setting_text(cfg, st, val, sizeof(val));
		if (off && !strcmp(key, "frame_buffer")) snprintf(val, sizeof(val), tr("Always on here"));
		bool has = game_has(st);
		SDL_Color vc = off ? C_DIS : C_GOLD;
		ui_font_text(scr, TF_BOLD, r.x + mg->pad, cy, lc, st->label, off ? C_DIS : C_TEXT, -1);
		int lw = ui_font_width(TF_BOLD, lc, st->label);
		int ax = r.x + r.w - mg->pad;
		ui_font_text(scr, TF_BOLD, ax, cy, cap, "›", sel ? C_TEXT : C_DIS, 1);
		int vw = ui_font_width(TF_BOLD, lc, val);
		ui_font_text(scr, TF_BOLD, ax - cap, cy, lc, val, vc, 1);
		int bx = ax - cap * 2 - vw;
		ui_font_text(scr, TF_BOLD, bx, cy, cap, "‹", sel ? C_TEXT : C_DIS, 1);
		rowBack[i] = (SDL_Rect) { bx - cap * 2, r.y, cap * 3, r.h };
		if (has) {
			/* the Game tag, after the label */
			int tc = SDL_max(6, cap * 5 / 8), tw = ui_font_width(TF_BOLD, tc, "Game") + mg->pad;
			rowTag[i] = (SDL_Rect) { r.x + mg->pad + lw + mg->pad / 2, cy - tc, tw, tc * 2 };
			ui_fill_round(scr, rowTag[i], C_GOLD, tc / 2);
			ui_font_text(scr, TF_BOLD, rowTag[i].x + tw / 2, cy, tc, "Game", (SDL_Color) { 20, 16, 36, 255 }, 0);
			rowTagOn[i] = true;
		}
		if (sel) {
			note = off ? off : has ? N_("This game has its own value. X, or going on past the last choice: same as all games"): st->note;
		}
	}
	if (menuNote[0]) note = menuNote, noteGold = true;
	else if (rowSel == 0) note = N_("A: the one lit above. Y: all games. X: undo this game's value");
	if (n > ROW_VIS) {
		/* more rows than fit: a bar at the right edge, where the rows are in the list */
		SDL_Rect a = srow_rect(scr, rowTop + 1), z = srow_rect(scr, rowTop + ROW_VIS);
		int bw = SDL_max(2, mg->margin / 2), bx = screens[scr].rect.w - mg->margin + (mg->margin - bw) / 2;
		int th = z.y + z.h - a.y;
		ui_fill_round(scr, (SDL_Rect) { bx, a.y, bw, th }, (SDL_Color) { 60, 56, 84, 255 }, bw / 2);
		int bh = SDL_max(bw * 3, th * ROW_VIS / n), by = a.y + (th - bh) * rowTop / SDL_max(1, n - ROW_VIS);
		ui_fill_round(scr, (SDL_Rect) { bx, by, bw, bh }, C_GOLD, bw / 2);
	}
	if (note) {
		SDL_Rect last = srow_rect(scr, SDL_min(n, rowTop + ROW_VIS));
		int nc = SDL_min(mg->noteCap, mg->noteCap * mg->w / SDL_max(1, ui_font_width(TF_BOLD, mg->noteCap, note)));
		/* on a dark panel: the bezel's logo (or the game's picture) can be behind the line */
		int ny = last.y + last.h + mg->h * 3 / 5, nh = nc * 3;
		ui_fill_round(scr, (SDL_Rect) { last.x, ny - nh / 2, last.w, nh }, (SDL_Color) { 8, 6, 18, 245 }, rad);
		ui_font_text(scr, TF_BOLD, screens[scr].rect.w / 2, ny, nc, note, noteGold ? C_GOLD : (SDL_Color) { 190, 186, 205, 255 }, 0);
	}
}

/* The tabs' pictures, drawn from rectangles on a 24 x 24 grid (nothing to
 * load): Game a handheld, Tools a toolbox, Display a monitor, Screens two
 * screens, Controls a gamepad, Speed fast forward, Map a pin. fg is the
 * picture, bg what's behind it (its cut-outs). */
static void icon_fill(int scr, SDL_Rect box, int size, float gx, float gy, float gw, float gh, float gr, SDL_Color c) {
	float u = size / 24.0f;
	int x0 = box.x + (box.w - size) / 2, y0 = box.y + (box.h - size) / 2;
	int ix = x0 + (int) lroundf(gx * u), iy = y0 + (int) lroundf(gy * u);
	int iw = SDL_max(1, (int) lroundf((gx + gw) * u) - (int) lroundf(gx * u)), ih = SDL_max(1, (int) lroundf((gy + gh) * u) - (int) lroundf(gy * u));
	ui_fill_round(scr, (SDL_Rect) { ix, iy, iw, ih }, c, (int) lroundf(gr * u));
}
/* a triangle pointing right (dir 1) or down (dir 2), as one-row strips */
static void icon_tri(int scr, SDL_Rect box, int size, float gx, float gy, float gw, float gh, int dir, SDL_Color c) {
	float u = size / 24.0f;
	int x0 = box.x + (box.w - size) / 2, y0 = box.y + (box.h - size) / 2;
	int ix = x0 + (int) lroundf(gx * u), iy = y0 + (int) lroundf(gy * u), iw = (int) lroundf(gw * u), ih = (int) lroundf(gh * u);
	if (dir == 1) {
		for (int r = 0; r < ih; ++r) {
			float t = fabsf((r + 0.5f) / ih * 2 - 1);
			int w = SDL_max(1, (int) lroundf(iw * (1 - t)));
			ui_fill(scr, (SDL_Rect) { ix, iy + r, w, 1 }, c);
		}
	} else {
		for (int r = 0; r < ih; ++r) {
			int w = SDL_max(1, (int) lroundf(iw * (1 - (r + 0.5f) / ih)));
			ui_fill(scr, (SDL_Rect) { ix + (iw - w) / 2, iy + r, w, 1 }, c);
		}
	}
}
static void draw_tab_icon(int scr, int page, SDL_Rect box, SDL_Color fg, SDL_Color bg) {
	int size = SDL_min(box.h * 68 / 100, box.w * 80 / 100);
	SDL_Rect b = box;
#define F(x, y, w, h, r, c) icon_fill(scr, b, size, x, y, w, h, r, c)
	switch (page) {
	case PG_GAME: /* a handheld: screen, D-pad, two buttons */
		F(5, 2, 14, 20, 3, fg);
		F(7, 4, 10, 8, 1, bg);
		F(7.5f, 16.8f, 4, 1.4f, 0.5f, bg);
		F(8.8f, 15.5f, 1.4f, 4, 0.5f, bg);
		F(14, 15.2f, 2.4f, 2.4f, 1.2f, bg);
		F(16.4f, 17.4f, 2.4f, 2.4f, 1.2f, bg);
		break;
	case ICON_SYSTEM: /* a toolbox */
		F(8, 2.5f, 8, 6, 2.5f, fg);
		F(10, 4.5f, 4, 4, 1, bg);
		F(3, 8, 18, 13.5f, 2, fg);
		F(3, 13.2f, 18, 1.4f, 0, bg);
		F(10.2f, 11.5f, 3.6f, 4.8f, 1, fg);
		break;
	case PG_DISPLAY: /* a monitor */
		F(2, 3.5f, 20, 14, 2, fg);
		F(4, 5.5f, 16, 10, 1, bg);
		F(10, 17.5f, 4, 3, 0, fg);
		F(7, 20, 10, 2, 1, fg);
		break;
	case PG_SCREENS: /* two screens, one over the other */
		F(4, 1.5f, 16, 9.5f, 1.5f, fg);
		F(6, 3.5f, 12, 5.5f, 0.5f, bg);
		F(4, 13, 16, 9.5f, 1.5f, fg);
		F(6, 15, 12, 5.5f, 0.5f, bg);
		break;
	case PG_CONTROLS: /* a gamepad */
		F(1, 6.5f, 22, 12, 6, fg);
		F(4.5f, 11.6f, 6, 1.8f, 0.5f, bg);
		F(6.6f, 9.5f, 1.8f, 6, 0.5f, bg);
		F(14.2f, 9.2f, 3.2f, 3.2f, 1.6f, bg);
		F(17.4f, 12.2f, 3.2f, 3.2f, 1.6f, bg);
		break;
	case PG_SPEED: /* fast forward */
		icon_tri(scr, b, size, 2.5f, 5, 9.5f, 14, 1, fg);
		icon_tri(scr, b, size, 12, 5, 9.5f, 14, 1, fg);
		break;
	case PG_MAP: /* a pin */
		icon_tri(scr, b, size, 6.2f, 12, 11.6f, 10.5f, 2, fg);
		F(4.5f, 1.5f, 15, 15, 7.5f, fg);
		F(9, 6, 6, 6, 3, bg);
		break;
	}
#undef F
}

void game_page_icon(int scr, int icon, SDL_Rect box, SDL_Color fg, SDL_Color bg) {
	draw_tab_icon(scr, icon, box, fg, bg);
}

static void draw_menu(void) {
	static SDL_Texture* shoulderTex[2][2]; /* L1, R1 prompts, per view */
	static int shoulderW, shoulderH;
	if (!shoulderTex[0][0] && !shoulderTex[0][1]) {
		ui_load_brand(BRAND_BTN_L1, shoulderTex[0], &shoulderW, &shoulderH);
		ui_load_brand(BRAND_BTN_R1, shoulderTex[1], &shoulderW, &shoulderH);
	}
	int scr = ui_touch_screen();
	/* (Overlays: the other screens undimmed, to show the bezel as it is) */
	bool preview = ovlOpen || pickWhich >= 0 || hexOpen;
	for (int s = 0; s < nScreens; ++s) if (!preview || s == scr) ui_dim(s, 170);
	draw_battery();
	float px = ui_px(scr);
	int cap = px * 11 / 2;   /* Futura capital height */
	int rad = px * 3;        /* box corner radius */
	if (achOpen) {
		cap = menu_geom(scr).cap;
		rad = menu_geom(scr).rad;
		SDL_Rect b = tab_rect(scr, 0), t = tab_rect(scr, 1);
		ui_fill_round(scr, b, C_BTN, rad);
		ui_label(scr, b.x + b.w / 2, b.y + b.h / 2, cap, "Back", C_TEXT);
		ui_fill_round(scr, t, C_BTN_EDGE, rad);
		ui_label(scr, t.x + t.w / 2, t.y + t.h / 2, cap, "Achievements", (SDL_Color) { 20, 16, 36, 255 });
		ra_list_draw(scr, ach_area(scr));
		if (nScreens > 1) {
			int o = ui_other_screen();
			draw_paused_label(o);
		}
		return;
	}
	if (noteOpen) {
		draw_note(scr);
		if (nScreens > 1) {
			int o = ui_other_screen();
			draw_paused_label(o);
		}
		return;
	}
	if (patchesOpen) {
		draw_patches(scr);
		if (nScreens > 1) {
			int o = ui_other_screen();
			draw_paused_label(o);
		}
		return;
	}
	if (preview) {
		if (hexOpen) draw_hex(scr);
		else if (pickWhich >= 0) draw_picker(scr);
		else draw_ovl_menu(scr);
		return;
	}
	if (cheatsOpen) {
		draw_cheats(scr);
		if (nScreens > 1) {
			int o = ui_other_screen();
			draw_paused_label(o);
		}
		return;
	}
	/* (moved off the button that asked: called off, by the D-pad or a finger) */
	if (armedItem >= 0 && menuItems[menuSel] != armedItem) armedItem = -1, menuNote[0] = 0;
	MenuGeom mg = menu_geom(scr);
	cap = mg.cap;
	rad = mg.rad;
	for (int t = 0; t < N_PAGES; ++t) {
		SDL_Rect r = top_box(scr, t, N_PAGES);
		bool on = t == curPage;
		ui_key(scr, r, rad, on ? KEY_LIT : KEY_PLAIN);
		draw_tab_icon(scr, t, r, on ? (SDL_Color) { 20, 16, 36, 255 } : ui_theme_light() ? (SDL_Color) { 92, 94, 112, 255 } : (SDL_Color) { 150, 152, 168, 255 }, ui_key_mid(on ? KEY_LIT : KEY_PLAIN));
	}
	SDL_Rect t0 = top_box(scr, 0, N_PAGES), t1 = top_box(scr, N_PAGES - 1, N_PAGES);
	/* L1 / R1 button prompts beside the tabs (the prompt art is 25:16) */
	int gw = mg.prompt, gh = gw * 16 / 25;
	SDL_Rect g0 = { mg.margin, t0.y + (t0.h - gh) / 2, gw, gh };
	SDL_Rect g1 = { mg.margin + mg.w - gw, t1.y + (t1.h - gh) / 2, gw, gh };
	if (shoulderTex[0][0] || shoulderTex[0][1]) {
		ui_image(scr, shoulderTex[0], shoulderW, shoulderH, "stretch", &g0);
		ui_image(scr, shoulderTex[1], shoulderW, shoulderH, "stretch", &g1);
	} else {
		ui_text(scr, t0.x - px * 14, t0.y + (t0.h - px * 7) / 2, px, "L1", C_DIS);
		ui_text(scr, t1.x + t1.w + px * 3, t1.y + (t1.h - px * 7) / 2, px, "R1", C_DIS);
	}
	if (page_is_rows(curPage)) {
		draw_rows_page(scr, &mg);
		if (nScreens > 1) {
			int o = ui_other_screen();
			draw_paused_label(o);
		}
		return;
	}
	itemTop = SDL_clamp(itemTop, 0, SDL_max(0, nMenuRows - ROW_VIS));
	if (nMenuRows > ROW_VIS) {
		/* more buttons than fit: a bar at the right edge */
		SDL_Rect a = { 0, mg.top, 0, 0 };
		int th = ROW_VIS * (mg.h + mg.gap) - mg.gap, bw = SDL_max(2, mg.margin / 2), bx = screens[scr].rect.w - mg.margin + (mg.margin - bw) / 2;
		ui_fill_round(scr, (SDL_Rect) { bx, a.y, bw, th }, (SDL_Color) { 60, 56, 84, 255 }, bw / 2);
		int bh = SDL_max(bw * 3, th * ROW_VIS / nMenuRows), by = a.y + (th - bh) * itemTop / SDL_max(1, nMenuRows - ROW_VIS);
		ui_fill_round(scr, (SDL_Rect) { bx, by, bw, bh }, C_GOLD, bw / 2);
	}
	for (int i = 0; i < nMenuItems; ++i) {
		if (!item_shown(i)) continue;
		SDL_Rect r = item_rect(scr, i);
		bool sel = i == menuSel;
		ui_key(scr, r, rad, sel ? KEY_SEL : KEY_PLAIN);
		bool en = item_enabled(menuItems[i]);
		const char* label = menuLabels[menuItems[i]];
		if (menuItems[i] == M_SCALE) label = scale_label(cfg->scale);
		if (menuItems[i] == M_LCD) label = shader_label(cfg->shader);
		if (menuItems[i] == M_COLOR) label = cfg->colorCorrection ? N_("Color Correction: On"): N_("Color Correction: Off");
		if (menuItems[i] == M_GHOST) label = cfg->ghosting ? N_("LCD Ghosting: On"): N_("LCD Ghosting: Off");
		if (menuItems[i] == M_AUTOSAVE) label = cfg->autoSave ? N_("Save on Quit: On"): N_("Save on Quit: Off");
		if (menuItems[i] == M_AUTOLOAD) label = cfg->autoLoad ? N_("Auto-Load: On"): N_("Auto-Load: Off");
		if (menuItems[i] == M_FPS) label = cfg->showFps ? N_("Show FPS: On"): N_("Show FPS: Off");
		if (menuItems[i] == M_FRAMEBUF) label = cfg->frameBuffer ? N_("Frame Buffer: On"): N_("Frame Buffer: Off");
		if (menuItems[i] == M_REWIND) label = cfg->rewind ? N_("Rewind: On"): N_("Rewind: Off");
		if (menuItems[i] == M_FF) {
			static char ffLabel[32];
			if (cfg->fastForward && cfg->ffSpeed <= 0) snprintf(ffLabel, sizeof(ffLabel), tr("Fast Forward: Max"));
			else if (cfg->fastForward) snprintf(ffLabel, sizeof(ffLabel), tr("Fast Forward: %dx"), cfg->ffSpeed);
			label = cfg->fastForward ? ffLabel : N_("Fast Forward: Off");
		}
		if (menuItems[i] == M_MATCH) label = !cfg->matchDisplay ? N_("Match Display Rate: Off"): pacedFallback ? N_("Match Display Rate: Off for now"): N_("Match Display Rate: On");
		if (menuItems[i] == M_SHAPES) label = !en || !strcmp(cfg->screenMatch, "keep") ? N_("Keep device aspect ratios"):
		                                      !strcmp(cfg->screenMatch, "aspect") ? N_("Match screen aspect ratios"): N_("Match screen sizes and aspect ratios");
		if (menuItems[i] == M_PLACE) label = en && !strcmp(cfg->screenPlace, "no_gap") ? N_("Screen Placement: Closest to Gap"): N_("Screen Placement: Centered");
		if (menuItems[i] == M_PIXLOGO) label = cfg->customLogos == CL_OFF ? N_("Custom Logos: Do Not Use"): cfg->customLogos == CL_FULL ? N_("Custom Logos: Full Detail"): N_("Custom Logos: Pixelated");
		if (menuItems[i] == M_OVERLAY) {
			static const char* const ovl[3] = { N_("Overlays: Off"), N_("Overlays: Generated"), N_("Overlays: Custom")};
			label = ovl[SDL_clamp(cfg->overlayMode, 0, 2)];
		}
		if (menuItems[i] == armedItem) label = N_("Are You Sure?");
		/* (the slot they use: "Save State 3", "Load State 3") */
		static char slotBtn[2][24];
		if (menuItems[i] == M_SAVE || menuItems[i] == M_LOAD) {
			int k = menuItems[i] == M_LOAD;
			snprintf(slotBtn[k], sizeof(slotBtn[k]), k ? tr("Load State %d") : tr("Save State %d"), stateSlot);
			label = slotBtn[k];
		}
		if (menuItems[i] == M_SLOT) {
			/* the slot, with arrows at the ends: LEFT / RIGHT, or a tap on an end */
			label = slot_label();
			ui_label(scr, r.x + mg.pad, r.y + r.h / 2, cap, "\u2039", sel ? C_TEXT : C_DIS);
			ui_label(scr, r.x + r.w - mg.pad, r.y + r.h / 2, cap, "\u203A", sel ? C_TEXT : C_DIS);
		}
		int lc = cap;
		if (tall_item(menuItems[i])) {
			/* double height: the text as big as the button and its widest label allow */
			static const char* const tallLabels[] = { "Close Game", "Close SNES DS", "Are You Sure?" };
			int widest = 1;
			for (size_t k = 0; k < sizeof(tallLabels) / sizeof(*tallLabels); ++k) widest = SDL_max(widest, ui_label_width(scr, 100, tallLabels[k]));
			lc = SDL_min(r.h * 2 / 5, (r.w - mg.pad * 2) * 100 / widest);
		}
		ui_label(scr, r.x + r.w / 2, r.y + r.h / 2, lc, label, en ? C_TEXT : C_DIS);
	}
	if (menuNote[0]) {
		int lastRow = SDL_min(nMenuRows, ROW_VIS) - 1;
		SDL_Rect last = { 0, mg.top + lastRow * (mg.h + mg.gap), 0, mg.h };
		int nc = SDL_min(mg.noteCap, mg.noteCap * mg.w / SDL_max(1, ui_font_width(TF_BOLD, mg.noteCap, menuNote)));
		/* (the last item is always a whole row: Close SNES DS, Show FPS...) */
		ui_font_text(scr, TF_BOLD, screens[scr].rect.w / 2, last.y + last.h + mg.h * 3 / 5, nc, menuNote, C_GOLD, 0);
	}
	if (nScreens > 1) {
		int o = ui_other_screen();
		draw_paused_label(o);
	}
}

/* ---------------------------------------------------------------- helpers */

static int probes_hit(const uint32_t* fb, const Probe* p, int tol) {
	int n = 0;
	for (int i = 0; i < 8 && (p[i].x || p[i].y || p[i].rgb); ++i) {
		uint32_t c = p[i].x < emu.w && p[i].y < emu.h ? fb[p[i].y * emu.w + p[i].x] : 0;
		int dr = (int) (c & 0xFF) - (int) ((p[i].rgb >> 16) & 0xFF);
		int dg = (int) ((c >> 8) & 0xFF) - (int) ((p[i].rgb >> 8) & 0xFF);
		int db = (int) ((c >> 16) & 0xFF) - (int) (p[i].rgb & 0xFF);
		n += abs(dr) <= tol && abs(dg) <= tol && abs(db) <= tol;
	}
	return n;
}

/* Renders the current frame again without these sprites (on top of the
 * ones already hidden: `kept`, put back afterwards) into out, then puts the
 * game back exactly as it was (state, picture, sound). */

/* Logo-only games: copy a recognised screen (THPS2 goal sheet) to the second
 * screen; back to the logo at the menus. */
/* The grabbed sky and scenery (the rows above the horizon), scaled up to
 * cover the canvas (centred), and dimmed well back. */
static bool frame_is_dark(const uint32_t* fb);

int stbi_write_png(char const* filename, int w, int h, int comp, const void* data, int stride_in_bytes);

static void update_snapshot(void) {
	const typeof(snapProf->snap)* sn = &snapProf->snap;
	int tol = sn->tol ? sn->tol : 12;
	if (probes_hit(emu.fb, sn->show, tol) >= sn->showMin) {
		snapRevert = 0;
		if (++snapShow >= SDL_max(1, sn->stable)) {
			pic_copy(snapShown, emu.fb, emu.w, emu.h);
			for (int i = 0; i < nViews; ++i) ui_update_texture(mapTex[i], snapShown, SNES_W * 4);
			if (mapShownIdx < 0 && debug) fprintf(stderr, "frame %llu: screen copied to the second screen\n", (unsigned long long) frameCount);
			mapShownIdx = 0;
		}
		return;
	}
	snapShow = 0;
	if (mapShownIdx < 0) return;
	bool menu = false;
	for (int k = 0; k < 2; ++k) {
		if (sn->revertMin[k] && probes_hit(emu.fb, sn->revert[k], tol) >= sn->revertMin[k]) menu = true;
	}
	if (!menu) {
		snapRevert = 0;
	} else if (++snapRevert >= SDL_max(1, sn->stable)) {
		mapShownIdx = -1;
		snapRevert = 0;
		if (debug) fprintf(stderr, "frame %llu: back at the menus, logo\n", (unsigned long long) frameCount);
	}
}

int game_screen(void);

static void save_thumbs(void) {
	SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(emu.fb, emu.w, emu.h, 32, emu.w * 4, SDL_PIXELFORMAT_XBGR8888);
	if (s) {
		SDL_SaveBMP(s, gp.thumb);
		SDL_FreeSurface(s);
	}
	const uint32_t* still = stillValid ? stillPx : mapRes.frames[0];
	if (mapShownIdx >= 0 && still) {
		s = SDL_CreateRGBSurfaceWithFormatFrom((void*) still, mapW, mapH, 32, mapW * 4, SDL_PIXELFORMAT_XBGR8888);
		if (s) {
			SDL_SaveBMP(s, gp.mapThumb);
			SDL_FreeSurface(s);
		}
	}
}

static void map_show_new(bool fresh);

/* The second screen kept with save states (inside the state file: emu.c
 * emu_state_file_*_extra): what it holds that the loaded game can't give
 * back at once. A block "GDS2", version, the game's code, then sections
 * (a 4-letter tag, a size, the data):
 *   MAP  the map or status pictures shown (w, h, frames, step, frames' pixels)
 *   SNAP the screen copied there (THPS2's goal sheet)
 *   TITL the level name kept above the HUD (Super Mario Advance 2)
 *   CARD the title card taken (Sonic Advance)
 * Put back at the next frame after the load (screen_restore_apply), where
 * the game's code is the same; anything else of the second screen comes
 * back from the loaded game as usual. */
void blob_add(Blob* b, const void* d, size_t n) {
	if (b->n + n > b->cap) {
		size_t c = SDL_max(b->cap * 2, b->n + n + 4096);
		uint8_t* q = realloc(b->p, c);
		if (!q) return;
		b->p = q, b->cap = c;
	}
	memcpy(b->p + b->n, d, n), b->n += n;
}
void blob_u32(Blob* b, uint32_t v) { blob_add(b, &v, 4); }
void blob_section(Blob* b, const char* tag, const void* d, size_t n) {
	blob_add(b, tag, 4), blob_u32(b, (uint32_t) n), blob_add(b, d, n);
}
/* (A Link to the Past's seen record in states: game_alttp.c) */

/* The second screen's block in a state: "SDS2", version 1, the game's
 * 8-character code, then tagged sections (blob_find) */
#define SCREEN_BLOB_HEAD 16
static void* screen_blob(size_t* n) {
	Blob b = { 0 };
	blob_add(&b, "SDS2", 4), blob_u32(&b, 1), blob_add(&b, subCode, 8);
	if (mapShownIdx >= 0 && !snapProf && mapRes.ok && mapRes.frames[0] && mapRes.nFrames > 0 && mapRes.w > 0 && mapRes.h > 0) {
		Blob m = { 0 };
		size_t px = (size_t) mapRes.w * mapRes.h * 4;
		blob_u32(&m, (uint32_t) mapRes.w), blob_u32(&m, (uint32_t) mapRes.h), blob_u32(&m, (uint32_t) mapRes.nFrames), blob_u32(&m, (uint32_t) mapRes.frameStep);
		for (int i = 0; i < mapRes.nFrames; ++i) blob_add(&m, mapRes.frames[i], px);
		if (m.p) blob_section(&b, "MAP ", m.p, m.n);
		free(m.p);
	}
	if (snapProf && mapShownIdx >= 0) blob_section(&b, "SNAP", snapShown, sizeof(snapShown));
	alt_state_block(&b);
	*n = b.n;
	return b.p;
}
/* a slot or the automatic state in RetroArch's folder (shared with it: issue #39) */
static bool state_is_retroarch(const char* path) {
	size_t n = strlen(gp.raDir);
	return n && !strncmp(path, gp.raDir, n) && path[n] == '/';
}
/* RetroArch's thumbnail beside a state (<state>.png, for its menu): the
 * game's picture as it is now, as the core drew it */
static void state_thumbnail(const char* path) {
	size_t n = (size_t) emu.w * emu.h;
	uint8_t* px = malloc(n * 4);
	if (!px) return;
	memcpy(px, emu.fb, n * 4); /* (bytes R, G, B, X) */
	for (size_t i = 0; i < n; ++i) px[i * 4 + 3] = 255;
	char png[1100], tmp[1110];
	snprintf(png, sizeof(png), "%s.png", path);
	snprintf(tmp, sizeof(tmp), "%s.tmp.png", path);
	if (!stbi_write_png(tmp, emu.w, emu.h, 4, px, emu.w * 4) || rename(tmp, png) != 0) {
		remove(tmp);
		fprintf(stderr, "save state: no thumbnail %s\n", png);
	}
	free(px);
}
static bool state_file_save(const char* path) {
	size_t n;
	void* blob = screen_blob(&n);
	bool ra = state_is_retroarch(path);
	StateFormat fmt = !ra ? STATE_FORMAT_OWN : gp.raCompress ? STATE_FORMAT_RETROARCH_RZIP : STATE_FORMAT_RETROARCH;
	if (ra) mkdirs(gp.raDir);
	bool ok = emu_state_save_file_as(&emu, path, blob, blob ? n : 0, fmt);
	free(blob);
	if (ok && ra) state_thumbnail(path);
	return ok;
}
static uint8_t* screenRestore; /* a loaded state's block, put back at the next frame */
static size_t screenRestoreN;
static bool screenRestorePending;
static const uint8_t* blob_find(const uint8_t* b, size_t n, const char* tag, uint32_t* size) {
	for (size_t at = SCREEN_BLOB_HEAD; at + 8 <= n;) {
		uint32_t sz;
		memcpy(&sz, b + at + 4, 4);
		if (at + 8 + sz > n) break;
		if (!memcmp(b + at, tag, 4)) return *size = sz, b + at + 8;
		at += 8 + sz;
	}
	return NULL;
}
uint32_t rd32(const uint8_t* p) { uint32_t v; memcpy(&v, p, 4); return v; }
/* (at the start of a frame, everything set up) */
static void screen_restore_apply(void) {
	if (!screenRestorePending) return;
	screenRestorePending = false;
	const uint8_t* b = screenRestore;
	size_t n = screenRestoreN;
	bool ours = b && n >= SCREEN_BLOB_HEAD && !memcmp(b, "SDS2", 4) && rd32(b + 4) == 1 && !memcmp(b + 8, subCode, 8);
	logoForced = false, logoHolding = 0;
	if (!ours) {
		if (b && debug) fprintf(stderr, "load state: its second screen is for another game or version: not used\n");
		return;
	}
	uint32_t sz;
	const uint8_t* d;
	if ((d = blob_find(b, n, "MAP ", &sz)) && sz >= 16) {
		int w = (int) rd32(d), h = (int) rd32(d + 4), nf = (int) rd32(d + 8), step = (int) rd32(d + 12);
		size_t px = (size_t) w * h * 4;
		/* (only at the size this game's captures are now, its texture's: a
		 * state saved before the profile's crop changed keeps a map of
		 * another size, left out) */
		bool sized = w == mapW && h == mapH;
		if (!sized && debug) fprintf(stderr, "load state: its map is %dx%d, captures are %dx%d now: not used\n", w, h, mapW, mapH);
		if (sized && mapRes.frames[0] && w == mapRes.w && h == mapRes.h && nf >= 1 && nf <= MAP_MAX_FRAMES && sz == 16 + px * nf) {
			for (int i = 0; i < nf; ++i) memcpy(mapRes.frames[i], d + 16 + px * i, px);
			mapRes.nFrames = nf, mapRes.frameStep = step, mapRes.ok = true, mapRes.pixels = mapRes.frames[0];
			map_show_new(false);
		} else if (sized && !mapRes.frames[0] && w > 0 && h > 0 && nf >= 1 && nf <= MAP_MAX_FRAMES && sz == 16 + px * nf) {
			/* (no capture yet this session: the frames made here, as mapcap_poll does) */
			for (int i = 0; i < MAP_MAX_FRAMES; ++i) mapRes.frames[i] = malloc(px);
			bool ok = true;
			for (int i = 0; i < MAP_MAX_FRAMES; ++i) ok = ok && mapRes.frames[i];
			if (ok) {
				for (int i = 0; i < nf; ++i) memcpy(mapRes.frames[i], d + 16 + px * i, px);
				mapRes.w = w, mapRes.h = h, mapRes.nFrames = nf, mapRes.frameStep = step, mapRes.ok = true, mapRes.pixels = mapRes.frames[0];
				map_show_new(false);
			}
		}
	}
	if (snapProf && (d = blob_find(b, n, "SNAP", &sz)) && sz == sizeof(snapShown)) {
		memcpy(snapShown, d, sz);
		for (int i = 0; i < nViews; ++i) ui_update_texture(mapTex[i], snapShown, SNES_W * 4);
		mapShownIdx = 0;
	}
	if ((d = blob_find(b, n, "ALTP", &sz))) alt_state_restore(d, sz);
	if (debug) fprintf(stderr, "load state: the second screen from it (%s%s)\n", blob_find(b, n, "MAP ", &sz) ? " map" : "",
	                   blob_find(b, n, "SNAP", &sz) ? " copy" : "");
}
/* Save states, with the RetroAchievements progress beside them (<state>.ra;
 * for one in RetroArch's folder, in SNES DS's own: <state_dir>/retroarch/) */
static const char* state_ra_side(const char* path) {
	static char side[1200];
	if (!state_is_retroarch(path)) return path;
	snprintf(side, sizeof(side), "%s/retroarch", cfg->stateDir);
	mkdirs(side);
	snprintf(side, sizeof(side), "%s/retroarch/%s", cfg->stateDir, strrchr(path, '/') + 1);
	return side;
}
static bool state_save(const char* path) {
	bool ok = state_file_save(path);
	if (ok) ra_state_saved(state_ra_side(path));
	return ok;
}

/* GEN-9: a state saved with another ROM than the one playing (other
 * patches, none, another dump) still loads, with a note */
static bool stateWarned; /* (the last load's note is up: callers' "loaded" notes wait) */
static bool stateNoCheck;
static void state_rom_check(const char* path) {
	uint32_t crc;
	stateWarned = false;
	if (!emu_state_file_rom_crc(path, &crc) || crc == emu.romCrc) return;
	const char* name = NULL;
	DumpKind k = profile_dump(emu.gameCode, crc, &name);
	char what[300];
	bool patched = emu.patch == PATCH_APPLIED || emu.patch == PATCH_PARTIAL;
	if (patched && crc == emu.fileCrc) snprintf(what, sizeof(what), tr("without the patches on"));
	else if (name && (k == DUMP_MOD || k == DUMP_MOD_TESTED)) snprintf(what, sizeof(what), tr("with %s"), name);
	else if (name) {
		/* (an official dump: the unchanged game, if this one's changed) */
		DumpKind now = profile_dump(emu.gameCode, emu.romCrc, NULL);
		bool changed = patched || now == DUMP_MOD || now == DUMP_MOD_TESTED || now == DUMP_MODIFIED;
		snprintf(what, sizeof(what), changed ? tr("with the unchanged game (%s)"): tr("with another dump of the game (%s)"), name);
	}
	else snprintf(what, sizeof(what), tr("with another version of the game (other patches, or another ROM)"));
	fprintf(stderr, "load state: %s was saved %s (ROM CRC32 %08x, playing %08x)\n", path, what, crc, emu.romCrc);
	ui_osd_long("This state was saved %s: it may not work", what);
	stateWarned = true;
}

static StateFileKind stateLoadKind; /* (what the last state_load found: why it failed) */
static bool state_load(const char* path) {
	bool fromSnesds = false;
	stateLoadKind = emu_state_file_kind(&emu, path, &fromSnesds);
	bool ok = emu_state_load_file(&emu, path);
	{
		static const char* const kinds[] = { "no file", "SNES DS's", "RetroArch's", "another emulator's", "unreadable" };
		fprintf(stderr, "load state: %s: %s (%s%s)\n", path, ok ? "loaded" : "not loaded", kinds[stateLoadKind],
		        stateLoadKind == STATE_FILE_RETROARCH ? (fromSnesds ? ", saved by SNES DS" : ", saved by RetroArch") : "");
	}
	stateWarned = false;
	if (ok && !stateNoCheck) state_rom_check(path);
	if (ok) {
		/* (saved by RetroArch over one of SNES DS's: the progress kept with
		 * that one is another moment's, so achievements start over) */
		const char* side = state_ra_side(path);
		if (side != path && !fromSnesds) {
			char ra[1210];
			snprintf(ra, sizeof(ra), "%s.ra", side);
			remove(ra);
		}
		ra_state_loaded(side);
	}
	/* (the second screen kept with it: put back at the next frame) */
	if (ok) {
		free(screenRestore);
		screenRestore = emu_state_file_get_extra(&emu, path, &screenRestoreN);
		screenRestorePending = true;
	}
	if (ok) bug_reset();
	if (ok) aheadReady = false; /* a frame emulated before the load: emulate afresh */
	return ok;
}

/* ---------------------------------------------------------------- bug reports
 *
 * The quick bar's bug button: <asset_dir>/bug-reports/GAME-DATE_TIME.zip
 * holding the game now (GAME-DATE_TIME.state), 10 seconds before
 * (...-rewound.state) and every key the game got in between
 * (...-keys.txt: the presses and lifts to read, then the same as a
 * harness script that plays it back from the rewound state, exactly). */

/* README.txt in the bug-reports folder: how to send a report (written with
 * the first report, if it isn't there; replaced when the text changes) */
static const char bugReadme[] =
	"SNES DS bug reports\n"
	"==================\n"
	"\n"
	"Each zip in this folder is one bug report, saved by the quick bar's bug\n"
	"button or by Submit a Bug Report in the pause menu (Extras), after a\n"
	"note you can type (or skip) while the game waits. It holds:\n"
	"\n"
	"  - GAME-DATE.state           the game at the moment you made the report\n"
	"  - GAME-DATE-rewound.state   the game about 10 seconds before\n"
	"  - GAME-DATE-keys.txt        every button you pressed in between\n"
	"  - GAME-DATE-info.txt        your note (if you typed one), your device,\n"
	"                              system, screens and settings, and the end\n"
	"                              of SNES DS's log\n"
	"\n"
	"In the pinball games' two-screen view, also what both screens showed\n"
	"(GAME-DATE-tall.png, with the two extra copies' own pictures), the\n"
	"view's last 10 seconds (GAME-DATE-tall.txt) and the copies themselves\n"
	"(GAME-DATE-copy0.state, -copy1.state).\n"
	"\n"
	"With these, the problem can be played back exactly as it happened.\n"
	"Your game itself (the ROM) is not in the report, and lines of the log\n"
	"that mention passwords or tokens are left out.\n"
	"\n"
	"\n"
	"How to send a report\n"
	"--------------------\n"
	"\n"
	"1. Make the report right after the problem shows (it keeps only the\n"
	"   last 10 seconds or so). If it takes a while to happen, make one\n"
	"   while it's on the screen. After loading a state, rewinding or\n"
	"   restarting, play on for 10 seconds before making one.\n"
	"2. Copy the zip from this folder to a computer or phone.\n"
	"3. Open a new issue on GitHub:\n"
	"   https://github.com/iosue-iulianus/snes-ds/issues/new\n"
	"   (you need a free GitHub account).\n"
	"4. Give it a short title: the game and what's wrong, for example\n"
	"   \"F-Zero: GP Legend: the course map flickers\".\n"
	"5. Drag the zip into the description box to attach it.\n"
	"6. Write what's below, then press Create.\n"
	"\n"
	"\n"
	"What to write\n"
	"-------------\n"
	"\n"
	"  - The game and its release (USA, Europe, Japan...), if it isn't in\n"
	"    the title.\n"
	"  - What you were doing: where you were in the game (a level, a race,\n"
	"    a menu, a battle) and what you pressed.\n"
	"  - What happened, on which screen (the game screen or the second one).\n"
	"  - What you expected to see instead. For the second screen, say what\n"
	"    you'd like it to show there and when.\n"
	"  - Whether it happens every time, and how to make it happen again.\n"
	"  - Photos or screenshots of both screens, if it's something you see.\n"
	"    A photo of the device is fine.\n"
	"  - Anything you changed in Settings or snes-ds.cfg, and whether cheats\n"
	"    were on.\n"
	"\n"
	"If SNES DS crashes, won't start, or a game won't load (so there's no way\n"
	"to make a report), attach snes-ds.log instead: it's in the SNES DS folder,\n"
	"next to this bug-reports folder. Say which system you use (ROCKNIX,\n"
	"BananaOS, Anbernic's own system, Android) and which device.\n"
	"\n"
	"Please don't attach game ROMs or BIOS files.\n";

static void write_bug_readme(const char* dir) {
	char path[1200];
	snprintf(path, sizeof(path), "%s/README.txt", dir);
	FILE* f = fopen(path, "rb");
	if (f) {
		static char old[sizeof(bugReadme) + 1];
		size_t n = fread(old, 1, sizeof(old), f);
		fclose(f);
		if (n == sizeof(bugReadme) - 1 && !memcmp(old, bugReadme, n)) return;
	}
	f = fopen(path, "wb");
	if (!f) return;
	fwrite(bugReadme, 1, sizeof(bugReadme) - 1, f);
	fclose(f);
}

static uint32_t crc32_of(const uint8_t* p, size_t n) {
	static uint32_t table[256];
	if (!table[1]) {
		for (uint32_t i = 0; i < 256; ++i) {
			uint32_t c = i;
			for (int k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
			table[i] = c;
		}
	}
	uint32_t c = 0xFFFFFFFFu;
	for (size_t i = 0; i < n; ++i) c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
	return c ^ 0xFFFFFFFFu;
}

static void put16(FILE* f, unsigned v) {
	fputc(v & 0xFF, f), fputc((v >> 8) & 0xFF, f);
}
static void put32(FILE* f, uint32_t v) {
	put16(f, v & 0xFFFF), put16(f, v >> 16);
}

/* A .zip of n files, stored (states don't shrink much, and it needs no zlib). */
int stbi_write_png(char const* filename, int w, int h, int comp, const void* data, int stride_in_bytes);
static bool zip_write(const char* path, const char* const* names, uint8_t* const* data, const size_t* sizes, int n) {
	FILE* f = fopen(path, "wb");
	if (!f) return false;
	time_t now = time(NULL);
	struct tm tm;
	localtime_r(&now, &tm);
	unsigned dosTime = (tm.tm_hour << 11) | (tm.tm_min << 5) | (tm.tm_sec / 2);
	unsigned dosDate = ((tm.tm_year - 80) << 9) | ((tm.tm_mon + 1) << 5) | tm.tm_mday;
	uint32_t offs[8], crcs[8];
	for (int i = 0; i < n; ++i) {
		offs[i] = (uint32_t) ftell(f);
		crcs[i] = crc32_of(data[i], sizes[i]);
		put32(f, 0x04034b50), put16(f, 20), put16(f, 0x0800), put16(f, 0), put16(f, dosTime), put16(f, dosDate);
		put32(f, crcs[i]), put32(f, (uint32_t) sizes[i]), put32(f, (uint32_t) sizes[i]);
		put16(f, (unsigned) strlen(names[i])), put16(f, 0);
		fwrite(names[i], 1, strlen(names[i]), f);
		fwrite(data[i], 1, sizes[i], f);
	}
	uint32_t cd = (uint32_t) ftell(f);
	for (int i = 0; i < n; ++i) {
		put32(f, 0x02014b50), put16(f, 20), put16(f, 20), put16(f, 0x0800), put16(f, 0), put16(f, dosTime), put16(f, dosDate);
		put32(f, crcs[i]), put32(f, (uint32_t) sizes[i]), put32(f, (uint32_t) sizes[i]);
		put16(f, (unsigned) strlen(names[i])), put16(f, 0), put16(f, 0), put16(f, 0), put16(f, 0), put32(f, 0), put32(f, offs[i]);
		fwrite(names[i], 1, strlen(names[i]), f);
	}
	uint32_t cdEnd = (uint32_t) ftell(f);
	put32(f, 0x06054b50), put16(f, 0), put16(f, 0), put16(f, n), put16(f, n), put32(f, cdEnd - cd), put32(f, cd), put16(f, 0);
	bool ok = !ferror(f);
	return fclose(f) == 0 && ok;
}

static uint8_t* read_whole(const char* path, size_t* size) {
	FILE* f = fopen(path, "rb");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8_t* b = n > 0 ? malloc(n) : NULL;
	if (b && fread(b, 1, n, f) != (size_t) n) free(b), b = NULL;
	fclose(f);
	*size = b ? (size_t) n : 0;
	return b;
}

static void keys_name(char* out, size_t size, unsigned k) {
	/* (emu.h's bit order; the names tools/snes-harness.c reads) */
	static const char* const names[12] = { "B", "Y", "SELECT", "START", "UP", "DOWN", "LEFT", "RIGHT", "A", "X", "L", "R" };
	out[0] = 0;
	for (int i = 0; i < 12; ++i) {
		if (k & (1u << i)) snprintf(out + strlen(out), size - strlen(out), "%s%s", out[0] ? "+" : "", names[i]);
	}
	if (!out[0]) snprintf(out, size, "-");
}

typedef struct {
	char* s;
	size_t n, cap;
} Text;
static void text_add(Text* t, const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	char line[512];
	int k = vsnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);
	if (k < 0) return;
	if ((size_t) k >= sizeof(line)) k = sizeof(line) - 1;
	if (t->n + k + 1 > t->cap) {
		size_t cap = (t->cap ? t->cap * 2 : 16384) + k;
		char* s = realloc(t->s, cap);
		if (!s) return;
		t->s = s, t->cap = cap;
	}
	memcpy(t->s + t->n, line, k + 1);
	t->n += k;
}

/* the first line of a small file, or "" */
static void read_line(const char* path, char* out, size_t size) {
	out[0] = 0;
	FILE* f = fopen(path, "r");
	if (!f) return;
	if (fgets(out, (int) size, f)) out[strcspn(out, "\r\n")] = 0;
	fclose(f);
}

/* where the log (stderr) is being written, or "" */
static void log_path(char* out, size_t size) {
	out[0] = 0;
#ifdef __ANDROID__
	if (android_home()) snprintf(out, size, "%s/snes-ds.log", android_home());
#else
	struct stat st;
	if (fstat(2, &st) != 0 || !S_ISREG(st.st_mode)) return;
	ssize_t n = readlink("/proc/self/fd/2", out, size - 1);
	out[n > 0 ? n : 0] = 0;
#endif
}

/* The report's -info.txt: the device and system, SNES DS's settings, the
 * game and the end of the log (without lines about passwords or tokens). */
static void bug_info(Text* t, const char* date, uint64_t back) {
#ifdef RETRODS_VERSION
	text_add(t, "SNES DS %s bug report, %s\n\n", RETRODS_VERSION, date);
#endif
	text_add(t, "== Device\n");
	char line[256];
#ifdef __ANDROID__
	char v[PROP_VALUE_MAX];
	__system_property_get("ro.product.manufacturer", v);
	text_add(t, "Device: %s", v);
	__system_property_get("ro.product.model", v);
	text_add(t, " %s\n", v);
	__system_property_get("ro.build.version.release", v);
	text_add(t, "Android %s", v);
	__system_property_get("ro.build.version.sdk", v);
	text_add(t, " (API %s)\n", v);
	__system_property_get("ro.build.display.id", v);
	text_add(t, "Build: %s\n", v);
#else
	read_line("/sys/firmware/devicetree/base/model", line, sizeof(line));
	if (line[0]) text_add(t, "Device: %s\n", line);
	FILE* f = fopen("/etc/os-release", "r");
	while (f && fgets(line, sizeof(line), f)) {
		if (!strncmp(line, "PRETTY_NAME=", 12) || !strncmp(line, "VERSION=", 8) || !strncmp(line, "BUILD_ID=", 9)) text_add(t, "os-release %s", line);
	}
	if (f) fclose(f);
	if (file_exists("/etc/banana")) text_add(t, "BananaOS (/etc/banana)\n");
#endif
	struct utsname u;
	if (uname(&u) == 0) text_add(t, "Kernel: %s %s %s\n", u.sysname, u.release, u.machine);
	text_add(t, "SDL video %s, audio %s\n", SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "?",
	         SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "?");
	for (int i = 0; i < nScreens; ++i) {
		text_add(t, "Screen %d: %dx%d%s%s\n", i, screens[i].rect.w, screens[i].rect.h, i == ui_touch_screen() ? ", touch" : "",
		         i == game_screen() ? ", game" : "");
	}
	int pct = battery_percent();
	if (pct >= 0) text_add(t, "Battery: %d%%\n", pct);

	text_add(t, "\n== Game\n");
	const MapProfile* prof = profile_find(subCode);
	text_add(t, "ROM: %s (%s%s%s), %s\n", gp.base, emu.gameCode, strcmp(subCode, emu.gameCode) ? ", playing " : "",
	         strcmp(subCode, emu.gameCode) ? subCode : "", prof ? prof->title : "no profile");
	{
		char d[640];
		text_add(t, "ROM dump: %s\n", dump_text(d, sizeof(d)));
	}
	text_add(t, "Running %.1f minutes (%llu frames), %.1f FPS (%.0f%%) when the report was made\n", SDL_GetTicks() / 60000.0,
	         (unsigned long long) frameCount, fps, speedPct);
	text_add(t, "Second screen: %s\n", mapShownIdx >= 0 ? "map / status picture" : logoOnly ? "logo (logo-only game)" : "logo");
	if (mc) {
		const char* why = NULL;
		int fails = mapcap_failures(mc, &why);
		if (fails) text_add(t, "Last capture: failed (%d in a row): %s\n", fails, why ? why : "?");
		else text_add(t, "Last capture: ok\n");
	}
	text_add(t, "Cheats on: %d. RetroAchievements: %s\n", cheats_on(&cheats),
	         ra_enabled() ? "signed in" : "off");
	text_add(t, "The rewound state is %llu frames before the other\n", (unsigned long long) back);
	/* (issue #39: slots 0-9 and the resume point shared with RetroArch, and where) */
	if (gp.raDir[0])
		text_add(t, "Save states: shared with RetroArch in %s (%s, %s); slot %d\n", gp.raDir, cfg->raStateDir[0] ? "chosen by hand" : "from its settings",
		         gp.raCompress ? "compressed" : "uncompressed", stateSlot);
	else
		text_add(t, "Save states: SNES DS's own%s; slot %d\n", !strcmp(cfg->stateFolder, "retroarch") ? " (shared with RetroArch, but its folder wasn't found)" : "",
		         stateSlot);

	text_add(t, "\n== Settings\n");
	text_add(t, "layout %s, swap %d, scale %s, map_scale %s, shader %s, color_correction %d, lcd_ghosting %d, smooth %d, overlays %d (screens %d, logo %d, %s / %s), border_color %s\n", cfg->layout,
	         cfg->swap, cfg->scale, cfg->mapScale, shaderNames[cfg->shader], cfg->colorCorrection, cfg->ghosting, cfg->smooth, cfg->overlayMode, cfg->overlays, cfg->bezelLogo, cfg->bezelInner, cfg->bezelOuter, cfg->borderColor);
	text_add(t, "map %d, map_interval %d\n", cfg->map, cfg->mapInterval);
	text_add(t, "logo_folder %s, custom_logos %s; logo: %s\n", cfg->logoFolder[0] ? cfg->logoFolder : "(not set)",
	         cfg->customLogos == CL_OFF ? "off" : cfg->customLogos == CL_FULL ? "full" : "pixelated", ui_logo_note());
	text_add(t, "frame_buffer %d, match_display %d, vsync %d, audio_latency %d, rewind %d (%d s), fast_forward %d, ff_speed %d\n", cfg->frameBuffer,
	         cfg->matchDisplay, cfg->vsync, cfg->audioLatency, cfg->rewind, cfg->rewindSeconds, cfg->fastForward, cfg->ffSpeed);
	text_add(t, "hotkey %s, menu_combo %s, home_button %d, ab_swap %d, touch_map %s, touch_screen %s\n", cfg->hotkey, cfg->menuCombo,
	         cfg->homeButton, cfg->abSwap, cfg->touchMap, cfg->touchScreen);
	text_add(t, "hotkeys: %s +", hotkeyBtnNames[hkMod]);
	for (int i = 0; i < HK_COUNT; ++i) text_add(t, " %s %s%s", hotkeyNames[i], hkBtn[i] >= 0 ? hotkeyBtnNames[hkBtn[i]] : "none", i + 1 < HK_COUNT ? "," : "\n");
	text_add(t, "save_on_quit %d, auto_load %d, quick_bar_pinned %d\n", cfg->autoSave, cfg->autoLoad, cfg->barPinned);
	text_add(t, "buttons (this game): %s%s\n", fb_describe(), fbSet ? "" : " (the default)");

	char lp[1024];
	log_path(lp, sizeof(lp));
	text_add(t, "\n== The end of the log%s%s\n", lp[0] ? ", " : " (not found)", lp);
	fflush(stderr);
	FILE* lf = lp[0] ? fopen(lp, "rb") : NULL;
	if (!lf) return;
	fseek(lf, 0, SEEK_END);
	long n = ftell(lf), keep = 48 * 1024;
	fseek(lf, n > keep ? n - keep : 0, SEEK_SET);
	if (n > keep && !fgets(line, sizeof(line), lf)) line[0] = 0; /* (from the next whole line) */
	char big[1024];
	while (fgets(big, sizeof(big), lf)) {
		if (strstr(big, "password") || strstr(big, "token")) continue;
		text_add(t, "%s", big);
	}
	fclose(lf);
}

/* Writes the report; out gets its file name (or why it failed). */
/* a report's files, gathered when the button was pressed, until it's zipped (bug_finish) */
static struct {
	bool pending;
	int nf;
	uint64_t back;
	char names[8][340], paths[8][1500], zip[1500], base[300];
} bugPend;

static bool bug_report(char* out, size_t outSize) {
	if (bugPend.pending) {
		char o[200];
		bug_finish("", o, sizeof(o)); /* (one still waiting for its note: saved as it is) */
	}
	/* the state closest to 10 seconds back (the oldest there is, if the log's younger) */
	int pick = -1;
	for (int i = 0; i < BUG_SNAPS; ++i) {
		if (!bugSnap[i].have) continue;
		if (pick < 0) {
			pick = i;
			continue;
		}
		bool fits = bugSnap[i].frame + BUG_BACK <= bugFrame, pickFits = bugSnap[pick].frame + BUG_BACK <= bugFrame;
		if (fits && (!pickFits || bugSnap[i].frame > bugSnap[pick].frame)) pick = i;
		else if (!fits && !pickFits && bugSnap[i].frame < bugSnap[pick].frame) pick = i;
	}
	if (pick < 0 || bugFrame - bugSnap[pick].frame >= BUG_LOG) {
		snprintf(out, outSize, tr("Nothing to report yet."));
		return false;
	}
	uint64_t from = bugSnap[pick].frame, to = bugFrame;

	char dir[1100], stamp[32], base[300], name[4][340], tmp[4][1500], zip[1500];
	snprintf(dir, sizeof(dir), "%s/bug-reports", cfg->assetDir);
	mkdirs(dir);
	write_bug_readme(dir);
	time_t now = time(NULL);
	struct tm tm;
	localtime_r(&now, &tm);
	strftime(stamp, sizeof(stamp), "%Y-%m-%d_%H-%M-%S", &tm);
	/* (a name the harness reads as one word: no spaces) */
	snprintf(base, sizeof(base), "%s-%s", gp.base, stamp);
	for (char* c = base; *c; ++c) {
		if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '-' || *c == '_' || *c == '.')) *c = '_';
	}
	snprintf(name[0], sizeof(name[0]), "%s.state", base);
	snprintf(name[1], sizeof(name[1]), "%s-rewound.state", base);
	snprintf(name[2], sizeof(name[2]), "%s-keys.txt", base);
	snprintf(name[3], sizeof(name[3]), "%s-info.txt", base);
	for (int i = 0; i < 4; ++i) snprintf(tmp[i], sizeof(tmp[i]), "%s/%s", dir, name[i]);
	snprintf(zip, sizeof(zip), "%s/%s.zip", dir, base);

	/* the game now; then the state from back then, written the same way, and the game put back */
	bool ok = state_file_save(tmp[0]);
	size_t sz = emu_state_size(&emu);
	void* cur = malloc(sz);
	if (ok && cur && emu_state_save(&emu, cur)) {
		ok = emu_state_load(&emu, bugSnap[pick].st) && emu_state_save_file(&emu, tmp[1]);
		emu_state_load(&emu, cur);
	} else {
		ok = false;
	}
	free(cur);

	/* the keys: presses and lifts to read, then the frames to play back */
	Text t = { 0 };
	double fps60 = game_fps();
	char date[64], k[64];
	strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S", &tm);
#ifdef RETRODS_VERSION
	const char* version = RETRODS_VERSION;
#else
	const char* version = "?";
#endif
	text_add(&t, "# SNES DS bug report, %s\n", date);
	text_add(&t, "# SNES DS %s, %s (%s%s%s)\n", version, gp.base, emu.gameCode, strcmp(subCode, emu.gameCode) ? ", playing " : "",
	         strcmp(subCode, emu.gameCode) ? subCode : "");
	text_add(&t, "# Cheats on: %d.\n", cheats_on(&cheats));
	text_add(&t, "# %s is the game %.1f seconds (%llu frames) before %s.\n", name[1], (to - from) / fps60,
	         (unsigned long long) (to - from), name[0]);
	text_add(&t, "# The keys the game got from then on (after SNES DS's button layouts), by frame:\n#\n");
	text_add(&t, "#  frame    time  event\n");
	unsigned prev = 0;
	uint64_t since[12] = { 0 };
	for (uint64_t f = from; f <= to; ++f) {
		unsigned cur = f < to ? bugKeys[f & (BUG_LOG - 1)] : prev; /* (at the end: the keys still held) */
		unsigned on = cur & ~prev, off = prev & ~cur;
		for (int b = 0; b < 12; ++b) {
			keys_name(k, sizeof(k), 1u << b);
			if (off & (1u << b)) {
				text_add(&t, "# %6llu %6.2fs  lift %s (held %llu frames)\n", (unsigned long long) (f - from), (f - from) / fps60, k,
				         (unsigned long long) (f - since[b]));
			}
		}
		for (int b = 0; b < 12; ++b) {
			keys_name(k, sizeof(k), 1u << b);
			if (on & (1u << b)) {
				text_add(&t, "# %6llu %6.2fs  %s %s\n", (unsigned long long) (f - from), (f - from) / fps60, f == from ? "already holding" : "press", k);
				since[b] = f;
			}
		}
		prev = cur;
	}
	for (int b = 0; b < 12; ++b) {
		if (prev & (1u << b)) {
			keys_name(k, sizeof(k), 1u << b);
			text_add(&t, "# %6llu %6.2fs  still holding %s (for %llu frames)\n", (unsigned long long) (to - from), (to - from) / fps60, k,
			         (unsigned long long) (to - since[b]));
		}
	}
	text_add(&t, "#\n# To play it back: build/host/snes-harness CORE ROM OUTDIR %s, run in the folder this\n", name[2]);
	text_add(&t, "# report was unzipped to; it ends where %s is.\n", name[0]);
	text_add(&t, "load ./%s\n", name[1]);
	for (uint64_t f = from; f < to;) {
		unsigned kf = bugKeys[f & (BUG_LOG - 1)];
		uint64_t e = f + 1;
		while (e < to && bugKeys[e & (BUG_LOG - 1)] == kf) ++e;
		keys_name(k, sizeof(k), kf);
		text_add(&t, "hold %s %llu\n", k, (unsigned long long) (e - f));
		f = e;
	}
	if (t.s) {
		FILE* f = fopen(tmp[2], "wb");
		if (!f || fwrite(t.s, 1, t.n, f) != t.n) ok = false;
		if (f) fclose(f);
	} else {
		ok = false;
	}
	free(t.s);

	/* the device, settings and log */
	Text info = { 0 };
	bug_info(&info, date, to - from);
	FILE* inf = info.s ? fopen(tmp[3], "wb") : NULL;
	if (!inf || fwrite(info.s, 1, info.n, inf) != info.n) ok = false;
	if (inf) fclose(inf);
	free(info.s);

	/* kept for bug_finish, which zips them with the player's note */
	int nf = 4;
	const char* names[4] = { name[0], name[1], name[2], name[3] };
	const char* paths[4] = { tmp[0], tmp[1], tmp[2], tmp[3] };
	if (!ok) {
		for (int i = 0; i < nf; ++i) remove(paths[i]);
		fprintf(stderr, "bug report: %s failed\n", zip);
		snprintf(out, outSize, tr("Couldn't save the bug report."));
		return false;
	}
	bugPend.pending = true;
	bugPend.nf = nf;
	bugPend.back = to - from;
	for (int i = 0; i < nf; ++i) {
		snprintf(bugPend.names[i], sizeof(bugPend.names[i]), "%s", names[i]);
		snprintf(bugPend.paths[i], sizeof(bugPend.paths[i]), "%s", paths[i]);
	}
	snprintf(bugPend.zip, sizeof(bugPend.zip), "%s", zip);
	snprintf(bugPend.base, sizeof(bugPend.base), "%s", base);
	snprintf(out, outSize, "bug-reports/%s.zip", base);
	return true;
}

/* The report's files into one .zip, with the player's note (if any) at the
 * top of its info file. */
static bool bug_finish(const char* note, char* out, size_t outSize) {
	if (!bugPend.pending) {
		snprintf(out, outSize, tr("Couldn't save the bug report."));
		return false;
	}
	bugPend.pending = false;
	bool ok = true;
	uint8_t* data[8] = { 0 };
	size_t sizes[8] = { 0 };
	const char* names[8];
	for (int i = 0; i < bugPend.nf; ++i) {
		names[i] = bugPend.names[i];
		ok = ok && (data[i] = read_whole(bugPend.paths[i], &sizes[i])) != NULL;
	}
	/* (the info file is the fourth: the note after its first line) */
	if (ok && note && note[0]) {
		const char* nl = memchr(data[3], '\n', sizes[3]);
		size_t head = nl ? (size_t) (nl - (const char*) data[3]) + 1 : 0;
		Text t = { 0 };
		text_add(&t, "%.*s\n== Note from the player\n%s\n", (int) head, (const char*) data[3], note);
		if (t.s) {
			size_t n = t.n + sizes[3] - head;
			uint8_t* m = malloc(n);
			if (m) {
				memcpy(m, t.s, t.n);
				memcpy(m + t.n, data[3] + head, sizes[3] - head);
				free(data[3]), data[3] = m, sizes[3] = n;
			}
			free(t.s);
		}
	}
	if (ok) ok = zip_write(bugPend.zip, names, data, sizes, bugPend.nf);
	for (int i = 0; i < bugPend.nf; ++i) free(data[i]), remove(bugPend.paths[i]);
	if (!ok) remove(bugPend.zip);
	fprintf(stderr, "bug report: %s %s (%llu frames back%s)\n", bugPend.zip, ok ? "saved" : "failed", (unsigned long long) bugPend.back,
	        note && note[0] ? ", with a note" : "");
	if (ok) snprintf(out, outSize, "bug-reports/%s.zip", bugPend.base);
	else snprintf(out, outSize, tr("Couldn't save the bug report."));
	return ok;
}

static void note_open(bool fromBar) {
	noteOpen = true;
	noteFromBar = fromBar;
	noteLen = 0, noteText[0] = 0;
	noteShift = true; /* (a capital first) */
	noteRow = 1, noteCol = 0;
	notePressKey = -1;
	menuNote[0] = 0;
}
/* Done (keep: with what's typed) or Skip: the report saved; back to the
 * menu, or (from the quick bar) to the game */
static void note_done(bool keep) {
	char out[200];
	bool ok = bug_finish(keep ? noteText : "", out, sizeof(out));
	noteOpen = false;
	if (noteFromBar) {
		menuOpen = false;
		ui_osd(ok ? "Bug report saved in bug-reports." : "%s", out);
	} else {
		snprintf(menuNote, sizeof(menuNote), "%s", ok ? tr("Bug report saved in bug-reports"): out);
	}
}

static void auto_save(void) {
	if (!cfg->autoSave) return;
	if (state_save(gp.autoState)) {
		save_thumbs();
		fprintf(stderr, "auto-saved %s\n", gp.autoState);
	} else {
		fprintf(stderr, "auto-save failed\n");
	}
}

/* The app is being put in the background (Android may kill it there):
 * keep a resume point. Called between frames, from ui_events. */
static void on_background(void) {
	auto_save();
	if (bugPend.pending) {
		/* (Android may kill the app there: a report waiting for its note saved with what's typed) */
		char out[200];
		bug_finish(noteOpen ? noteText : "", out, sizeof(out));
		if (noteOpen) noteOpen = false, menuNote[0] = 0;
	}
}

static void open_menu(void);

/* Back from Home / the app switcher: paused, rather than straight into the action. */
static void on_foreground(void) {
	if (!menuOpen) open_menu();
}

static const uint32_t* map_px(int idx) {
	return idx == STILL_IDX ? (stillValid ? stillPx : NULL) : mapRes.frames[idx];
}

/* (set to upload the picture shown again: the textures were made again) */
static const uint32_t* stitch_compose(const uint32_t* px);
static void upload_map_frame(int idx) {
	const uint32_t* px = map_px(idx);
	if ((idx == mapShownIdx && !mapReupload) || !px) return;
	if (stitch_on()) px = stitch_compose(px);
	mapReupload = false;
	for (int i = 0; i < nViews; ++i) ui_update_texture(mapTex[i], px, mapW * 4);
	mapShownIdx = idx;
}

static int cmp_u32(const void* a, const void* b) {
	uint32_t x = *(const uint32_t*) a, y = *(const uint32_t*) b;
	return x < y ? -1 : x > y;
}

/* Builds the still map for "Map Animations: Off" into out (w * h), and marks
 * the pixels that animate in mask. Each animated spot (pixels that change,
 * grouped when they're within a few pixels of each other) is taken from the
 * frame where it is most visible: its brightest state (a marker that blinks
 * on and off, a pulsing glow at its peak), with STILL_RED its reddest if it
 * has red in it, or with STILL_COVER the state that
 * covers the most pixels unlike the spot's surroundings, ties going to the
 * one with the most contrast. */
static void compose_still(const MapResult* r, int pick, uint32_t* out, unsigned char* mask) {
	int n = r->nFrames, w = r->w, h = r->h, wh = w * h;
	memcpy(out, r->frames[0], (size_t) wh * 4);
	memset(mask, 0, (size_t) wh);
	if (n < 2 || pick == STILL_FIRST) return;
	for (int i = 0; i < wh; ++i) {
		uint32_t p = r->frames[0][i] & 0xFFFFFF;
		for (int f = 1; f < n && !mask[i]; ++f) mask[i] = (r->frames[f][i] & 0xFFFFFF) != p;
	}
	int* label = calloc((size_t) wh, sizeof(int));
	int* queue = malloc((size_t) wh * sizeof(int));
	if (!label || !queue) {
		free(label);
		free(queue);
		return;
	}
	static uint32_t sample[4096];
	const int R = 3; /* animated pixels this close belong to the same spot */
	int spots = 0;
	for (int seed = 0; seed < wh; ++seed) {
		if (!mask[seed] || label[seed]) continue;
		/* flood-fill one spot; list its pixels in queue[0..len) */
		int id = ++spots, len = 0, head = 0;
		int x0 = w, y0 = h, x1 = 0, y1 = 0;
		label[seed] = id;
		queue[len++] = seed;
		while (head < len) {
			int p = queue[head++], px = p % w, py = p / w;
			x0 = SDL_min(x0, px), x1 = SDL_max(x1, px), y0 = SDL_min(y0, py), y1 = SDL_max(y1, py);
			for (int dy = -R; dy <= R; ++dy) {
				for (int dx = -R; dx <= R; ++dx) {
					int nx = px + dx, ny = py + dy;
					if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
					int q = ny * w + nx;
					if (mask[q] && !label[q]) label[q] = id, queue[len++] = q;
				}
			}
		}
		x0 = SDL_max(x0 - 2, 0), y0 = SDL_max(y0 - 2, 0), x1 = SDL_min(x1 + 2, w - 1), y1 = SDL_min(y1 + 2, h - 1);
		/* the surroundings' colour: most common still pixel around the spot */
		uint32_t bg = 0;
		if (pick == STILL_COVER) {
			int ns = 0;
			for (int y = y0; y <= y1; ++y) {
				for (int x = x0; x <= x1 && ns < (int) SDL_arraysize(sample); ++x) {
					if (!mask[y * w + x]) sample[ns++] = r->frames[0][y * w + x] & 0xFFFFFF;
				}
			}
			qsort(sample, ns, sizeof(sample[0]), cmp_u32);
			for (int i = 0, run = 0, top = 0; i < ns; ++i) {
				run = i && sample[i] == sample[i - 1] ? run + 1 : 1;
				if (run > top) top = run, bg = sample[i];
			}
		}
		int best = 0;
		long bestA = -1, bestB = -1;
		/* STILL_RED: a spot with real red in any frame is judged by how red it
		 * is (red above the mean of green and blue, summed); else brightest */
		bool red = false;
		for (int f = 0; f < n && pick == STILL_RED && !red; ++f) {
			for (int i = 0; i < len && !red; ++i) {
				uint32_t p = r->frames[f][queue[i]];
				int pr = p & 0xFF, pg = (p >> 8) & 0xFF, pb = (p >> 16) & 0xFF;
				red = pr - (pg > pb ? pg : pb) > 80;
			}
		}
		for (int f = 0; f < n; ++f) {
			const uint32_t* fr = r->frames[f];
			long a = 0, b = 0;
			for (int i = 0; i < len; ++i) {
				uint32_t p = fr[queue[i]];
				if (pick == STILL_COVER) a += (p & 0xFFFFFF) != bg;
				else if (red) a += SDL_max(0, (int) (p & 0xFF) - (int) (((p >> 8) & 0xFF) + ((p >> 16) & 0xFF)) / 2);
				else a += (p & 0xFF) + ((p >> 8) & 0xFF) + ((p >> 16) & 0xFF);
			}
			if (pick == STILL_COVER) {
				for (int y = y0; y <= y1; ++y) {
					for (int x = x0; x <= x1; ++x) {
						uint32_t p = fr[y * w + x];
						if (x < x1) {
							uint32_t q = fr[y * w + x + 1];
							for (int c = 0; c < 24; c += 8) b += abs((int) ((p >> c) & 0xFF) - (int) ((q >> c) & 0xFF));
						}
						if (y < y1) {
							uint32_t q = fr[(y + 1) * w + x];
							for (int c = 0; c < 24; c += 8) b += abs((int) ((p >> c) & 0xFF) - (int) ((q >> c) & 0xFF));
						}
					}
				}
			}
			if (a > bestA || (a == bestA && b > bestB)) best = f, bestA = a, bestB = b;
		}
		for (int i = 0; i < len; ++i) out[queue[i]] = r->frames[best][queue[i]];
	}
	free(label);
	free(queue);
}

/* After a fresh capture: rebuild the still map. A capture that only differs
 * from the one on screen where the markers animate (a pulse caught at
 * another point of its loop) keeps the old still, so it doesn't jump around
 * every time the map is refreshed. Returns true if the still changed. */
static bool stillStale; /* stillPx is from an older map (built only when it's shown) */
/* The map animates, unless the game's is always still */
static bool map_anim(void) {
	const MapProfile* p = mc ? mapcap_profile(mc) : NULL;
	return !(p && p->mapStill);
}

static bool update_still(void) {
	int w = mapRes.w, h = mapRes.h, wh = w * h;
	uint32_t* px = malloc((size_t) wh * 4);
	unsigned char* mask = malloc((size_t) wh);
	if (!px || !mask) {
		free(px);
		free(mask);
		return false;
	}
	const MapProfile* prof = mc ? mapcap_profile(mc) : NULL;
	compose_still(&mapRes, prof ? (int) prof->stillPick : STILL_BRIGHT, px, mask);
	if (stillValid && stillW == w && stillH == h) {
		bool same = true;
		for (int i = 0; i < wh && same; ++i) same = px[i] == stillPx[i] || mask[i];
		if (same) {
			free(px);
			free(mask);
			return false;
		}
	}
	free(stillPx);
	free(mask);
	stillPx = px, stillW = w, stillH = h, stillValid = true;
	return true;
}

/* A new map picture in mapRes (a capture; or one kept with a save state,
 * `fresh` false) */
static void map_show_new(bool fresh) {
	mapInactive = false;
	mapShownIdx = -1;
	/* (pages, a few seconds each: Fire Emblem's unit status; they
	 * keep turning at their pace through new captures) */
	bool pages = mapRes.nFrames > 1 && mapRes.frameStep >= 60;
	if (!pages) mapAnimStart = frameCount;
	/* the still (each marker frozen) is only shown with map animations
	 * off, and building it is real work: only then */
	bool stillChanged = false;
	stillStale = map_anim();
	if (!stillStale) stillChanged = update_still();
	int first = pages ? map_anim_index() : 0;
	upload_map_frame(map_anim() ? first : STILL_IDX);
	if (debug) fprintf(stderr, "frame %llu: map %s (%d frame(s) %d apart%s)\n", (unsigned long long) frameCount, fresh ? "updated" : "from the save state",
	                   mapRes.nFrames, mapRes.frameStep, stillStale ? "" : stillChanged ? ", still changed" : ", still unchanged");
}

/* a profile's own live picture is up (liveView): drawn as it is */

static bool frame_is_dark(const uint32_t* fb) {
	int bright = 0, total = 0;
	for (int i = 0; i < emu.w * emu.h; i += 7) {
		uint32_t c = fb[i];
		int lum = ((c & 0xFF) + ((c >> 8) & 0xFF) * 2 + ((c >> 16) & 0xFF)) >> 2;
		bright += lum > 40;
		++total;
	}
	return bright * 40 < total;
}

int game_screen(void) {
	if (nScreens < 2) return 0;
	return cfg->swap ? 1 : 0;
}

/* Frame pacing. The GBA runs at 59.73 frames a second; a 60 Hz screen then
 * shows one frame twice about every 3.7 s (a small, regular stutter).
 * "Match Display Rate" runs the game at the display's rate instead when it's
 * within 1.5% (60 Hz: 0.46% faster, sound resampled to match, a pitch change
 * nobody hears), and presents the game's screen in step with the display
 * (vsync), so each frame gets exactly one refresh. */
static double frameSec = 1.0 / SNES_FPS_NTSC;
static double pacedHz;    /* the game's rate when matched to a display (its refresh / pacedEvery), else 0 */
static int pacedView = -1, pacedEvery = 1; /* the view presented in step, and every how many refreshes */
static double audioSpeed = 1.0; /* emulated time per real time */

/* ---- Keeping the game and its sound going while a frame's present is held
 * up (issue #50). On one player's RG DS Plus (ROCKNIX, a second SD card),
 * presenting a frame stalled for half a second every few seconds: the
 * graphics driver waiting for sway to give a buffer back. Nothing in SNES DS
 * can make the screen take the frame sooner, but the emulator isn't needed
 * for presenting: once a present has taken 1.5 frames, this thread runs the
 * game's frames on time with the last keys held (run_frame, as the frame
 * buffer's worker does, so the bug log keeps every frame's keys) and hands
 * their sound over, until the present returns. The picture freezes for the
 * stall and then carries on from where the game is; the sound doesn't
 * break and the game doesn't fall behind. Not run: RetroAchievements'
 * frame check (main thread only), the rewind buffer's states, the map's
 * room checks; those carry on with the next frame. keep_sound = 0: off. */
static struct {
	pthread_t thread;
	pthread_mutex_t lock;
	pthread_cond_t cond;
	bool started, quit, armed, running;
	Uint64 armedAt;
	uint32_t keys;
	int ran;      /* frames run while armed */
	int ahead;    /* frames already emulated ahead when armed (the frame buffer's) */
	double ms;    /* how long the present took, as the keeper saw it */
	SDL_AudioDeviceID audio;
	int outRate, maxQueued;
	SDL_AudioStream* conv;
	int convFrom;
} keeper = { .lock = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER };

/* the frames' sound, converted as the main loop does (not fast-forwarding,
 * not matched: none of its other adjustments apply) */
static void keeper_audio(void) {
	static int16_t in[4096 * 2], out[8192 * 2];
	size_t got;
	while ((got = emu_audio_read(&emu, in, 4096)) > 0) {
		if (!keeper.audio) continue;
		if ((int) (SDL_GetQueuedAudioSize(keeper.audio) / 4) > keeper.maxQueued) continue;
		int rate = (int) lround(emu_audio_rate(&emu) * audioSpeed);
		if (rate != keeper.convFrom || !keeper.conv) {
			if (keeper.conv) SDL_FreeAudioStream(keeper.conv);
			keeper.conv = SDL_NewAudioStream(AUDIO_S16SYS, 2, rate, AUDIO_S16SYS, 2, keeper.outRate);
			keeper.convFrom = rate;
		}
		if (!keeper.conv) continue;
		SDL_AudioStreamPut(keeper.conv, in, (int) got * 4);
		int bytes;
		while ((bytes = SDL_AudioStreamGet(keeper.conv, out, sizeof(out))) > 0) {
			int vol = ui_volume();
			if (vol < 100) {
				int gain = vol * vol * 65536 / 10000;
				for (int k = 0; k < bytes / 2; ++k) out[k] = (int16_t) (out[k] * gain >> 16);
			}
			SDL_QueueAudio(keeper.audio, out, bytes);
		}
	}
}

static void* keeper_worker(void* arg) {
	(void) arg;
	const double freq = (double) SDL_GetPerformanceFrequency();
	pthread_mutex_lock(&keeper.lock);
	for (;;) {
		while (!keeper.armed && !keeper.quit) pthread_cond_wait(&keeper.cond, &keeper.lock);
		if (keeper.quit) break;
		Uint64 t0 = keeper.armedAt;
		int n = 0;
		while (keeper.armed && !keeper.quit) {
			/* the first frame once the present has taken 1.5 frames (one more if the
			 * frame buffer's worker already ran the next), then one a frame */
			double due = (1.5 + keeper.ahead + n) * frameSec, now = (double) (SDL_GetPerformanceCounter() - t0) / freq;
			if (now < due) {
				struct timespec ts;
				clock_gettime(CLOCK_REALTIME, &ts);
				long ns = (long) ((due - now) * 1e9);
				ts.tv_sec += (ts.tv_nsec + ns) / 1000000000L;
				ts.tv_nsec = (ts.tv_nsec + ns) % 1000000000L;
				pthread_cond_timedwait(&keeper.cond, &keeper.lock, &ts);
				continue;
			}
			keeper.running = true;
			uint32_t keys = keeper.keys;
			pthread_mutex_unlock(&keeper.lock);
			/* (a frame the frame buffer's worker is running comes first) */
			pthread_mutex_lock(&ahead.lock);
			while (ahead.busy) pthread_cond_wait(&ahead.cond, &ahead.lock);
			pthread_mutex_unlock(&ahead.lock);
			run_frame(keys);
			keeper_audio();
			pthread_mutex_lock(&keeper.lock);
			keeper.running = false;
			++keeper.ran;
			++n;
			pthread_cond_broadcast(&keeper.cond);
		}
	}
	pthread_mutex_unlock(&keeper.lock);
	return NULL;
}

/* Before a present: the keeper may run frames until keeper_disarm. */
static void keeper_arm(uint32_t keys, SDL_AudioDeviceID audio, int outRate, int maxQueued, int ahead) {
	pthread_mutex_lock(&keeper.lock);
	if (!keeper.started) {
		keeper.quit = false;
		keeper.started = pthread_create(&keeper.thread, NULL, keeper_worker, NULL) == 0;
	}
	if (keeper.started) {
		keeper.keys = keys;
		keeper.audio = audio, keeper.outRate = outRate, keeper.maxQueued = maxQueued;
		keeper.armedAt = SDL_GetPerformanceCounter();
		keeper.ran = 0;
		keeper.ahead = ahead;
		keeper.armed = true;
		pthread_cond_broadcast(&keeper.cond);
	}
	pthread_mutex_unlock(&keeper.lock);
}

/* After the present: the emulator is the loop's again; the frames run meanwhile. */
static int keeper_disarm(void) {
	pthread_mutex_lock(&keeper.lock);
	keeper.armed = false;
	pthread_cond_broadcast(&keeper.cond);
	while (keeper.running) pthread_cond_wait(&keeper.cond, &keeper.lock);
	int n = keeper.ran;
	keeper.ran = 0;
	pthread_mutex_unlock(&keeper.lock);
	return n;
}

static void keeper_stop(void) {
	pthread_mutex_lock(&keeper.lock);
	bool started = keeper.started;
	keeper.armed = false;
	keeper.quit = true;
	pthread_cond_broadcast(&keeper.cond);
	pthread_mutex_unlock(&keeper.lock);
	if (started) pthread_join(keeper.thread, NULL);
	keeper.started = false;
	if (keeper.conv) SDL_FreeAudioStream(keeper.conv);
	keeper.conv = NULL, keeper.convFrom = 0;
}
/* Matched, a frame that misses its refresh waits for the next one, and the
 * time is never made up: the game slows down (the GBA's rate catches up
 * after a slow frame). A game that keeps missing (three seconds in a row
 * with 3 or more) goes back to the GBA's rate until the setting is turned
 * on again or the game is started again (pacedFallback). */
static int pacedMisses, pacedMissSecs;

static void pacing_update(void) {
	/* the game's screen's display, else (its rate not known: the Duo's
	 * second display, a surface of its own) the other screen's */
	int view = nScreens > 0 ? screens[game_screen()].view : -1;
	double hz = view >= 0 ? ui_display_hz(view) : 0;
	if (hz <= 0 && nScreens == 2) view = screens[1 - game_screen()].view, hz = ui_display_hz(view);
	pacedHz = 0, pacedView = -1, pacedEvery = 1;
	pacedMisses = pacedMissSecs = 0;
	/* matched at the display's rate (the setting), or at a half, third or
	 * quarter of it whatever the setting: a 120 Hz display with the game's
	 * own clock gave each frame one, two or three refreshes (judder; the
	 * Duo's 120 Hz mode), matched each frame gets two */
	for (int k = 1; k <= 4 && !pacedFallback && hz > 0; ++k) {
		if ((k == 1 ? cfg->matchDisplay : true) && fabs(hz / k / game_fps() - 1) <= 0.015) {
			pacedHz = hz / k, pacedView = view, pacedEvery = k;
			break;
		}
	}
	frameSec = 1.0 / (pacedHz > 0 ? pacedHz : game_fps());
	audioSpeed = pacedHz > 0 ? pacedHz / game_fps() : 1.0;
	fprintf(stderr, "frame pacing: display %.3f Hz (every %d refresh(es)), game %.3f frames a second (%s)\n", hz, pacedEvery, 1.0 / frameSec,
	        pacedHz > 0 ? "matched to the display" : cfg->matchDisplay && pacedFallback ? "the game's rate: frames kept missing the display's refresh" :
	        cfg->matchDisplay ? "display too far off to match" : "the game's rate");
}

/* Rewind (hold L2): mGBA's rewind buffer, a state every second frame,
 * rewind_seconds long (default 30 s: 900 states, each kept as its
 * difference from the next, a few MB). */
static int rewindTick;
static bool wasRewinding;

static void rewind_setup(void) {
	emu_rewind_deinit();
	rewindTick = 0;
	if (!cfg->rewind || !emu.core) return;
	int secs = cfg->rewindSeconds < 5 ? 5 : cfg->rewindSeconds > 300 ? 300 : cfg->rewindSeconds;
	emu_rewind_init(&emu, (size_t) secs * 30);
	fprintf(stderr, "rewind: %d s (%d states)\n", secs, secs * 30);
}

/* ff_speed as snes-ds.cfg writes it: a number, or max (0, as atoi reads it) */
static const char* ff_speed_name(const Config* c) {
	static char s[12];
	if (c->ffSpeed <= 0) return "max";
	snprintf(s, sizeof(s), "%d", c->ffSpeed);
	return s;
}

/* Display choices made in the menu are remembered for next time (and the
 * game list's Performance setting: prefs_save from library.c). */
static void save_prefs(void) {
	prefs_save(cfg);
}

void prefs_save(Config* cfg) {
	/* what the settings table doesn't hold (it writes its own lines: the game
	 * list's Settings, the pause menu): the quick bar's pin, the shader the
	 * hotkey turns back on, the generated bezel's colours. Every other line
	 * stays (config_kv_update). */
	char prefs[1100];
	snprintf(prefs, sizeof(prefs), "%s/prefs.cfg", cfg->stateDir);
	const char* kv[4][2] = {
		{ "quick_bar_pinned", cfg->barPinned ? "1" : "0" },
		{ "shader_last", shaderNames[cfg->shaderLast > SH_OFF && cfg->shaderLast < SH_COUNT ? cfg->shaderLast : SH_GRID] },
		{ "bezel_inner", cfg->bezelInner },
		{ "bezel_outer", cfg->bezelOuter },
	};
	config_kv_update(prefs, "# written by snes-ds's menus; overrides snes-ds.cfg", 4, kv);
}

static bool gameTexAll = true; /* upload the game's frame to every view next time */

static void swap_screens(void) {
	/* the game moves to the other view, whose copy of the frame may be old
	 * (frames go only where the game is shown; the menu uploads none) */
	if (emu.fb) {
		game_tex_fit(emu.w, emu.h);
		for (int i = 0; i < nViews; ++i) ui_update_texture(gameTex[i], emu.fb, emu.w * 4);
	}
	if (nScreens < 2) {
		singleShowsMap = !singleShowsMap;
		return;
	}
	cfg->swap = !cfg->swap;
	cfg->swapChosen = true;
	commit_current("swap"); /* (the layer in force: this game's own, or all games') */
}

/* Display > Shader, or the shader hotkey: the pattern over the pixels */
static void set_shader(int sh) {
	bool subBefore = cfg->shader >= SH_SUB_LIGHT;
	cfg->shader = sh;
	if (sh != SH_OFF) cfg->shaderLast = sh;
	ui_shader(cfg->shader);
	if (subBefore || cfg->shader >= SH_SUB_LIGHT) rebuild_textures(); /* (the subpixels' brightening is in the colour table) */
	commit_current("shader");
	save_prefs();
}

/* ---- hotkeys (hotkeys.c): the hotkey button (HOME, SELECT, START or L3)
 * held with an action's button (Settings > Hotkeys, snes-ds.cfg).
 * SELECT or START as the hotkey still reach the game when tapped alone;
 * L3 tapped alone opens the quick menu, as it does when it isn't the hotkey. */
static void hotkeys_setup(void) {
	hotkeys_resolve(cfg, &hkMod, hkBtn, true);
	char line[200];
	int n = snprintf(line, sizeof(line), "hotkey: %s +", hotkeyBtnNames[hkMod]);
	for (int i = 0; i < HK_COUNT && n < (int) sizeof(line); ++i)
		n += snprintf(line + n, sizeof(line) - n, " %s %s%s", hotkeyNames[i], hkBtn[i] >= 0 ? hotkeyBtnNames[hkBtn[i]] : "none", i + 1 < HK_COUNT ? "," : "");
	fprintf(stderr, "%s\n", line);
}

static SDL_Color borderColor;

/* User overlay PNGs in <asset_dir>/overlays/, drawn over a whole screen
 * (transparent where the picture should show). A name is the parts that
 * apply, in this order, joined by dashes: the game's code (optional), the
 * screen (top or bottom; or, from before v1.5, primary: the game screen,
 * secondary: the other, which follow the picture when the screens are
 * swapped), the scaling (integer, fit, zoom, stretch; optional) and the
 * screen shapes (aspect or size, for screen_match; optional, and only
 * looked for while it's on and the screens' shapes differ). The most
 * specific found wins, top / bottom before primary / secondary at each step:
 *   <CODE>-<screen>-<scale>-<match>, <CODE>-<screen>-<match>,
 *   <CODE>-<screen>-<scale>, <CODE>-<screen>,
 *   (the second screen only, older names: <CODE>-<scale>, <CODE>)
 *   <screen>-<scale>-<match>, <screen>-<match>, <screen>-<scale>, <screen>
 * each with "-no_gap" after it while Screen Placement moves the picture
 * (load_overlay). wiki/Overlays.md explains them for players (top and bottom only). */
static void overlays_reload(void) {
	overlayMode[0][0] = overlayMode[1][0] = 0;
	bezelKey[0][0] = bezelKey[1][0] = 0;
}

/* s: the physical screen (0 top, 1 bottom); which: what's on it (0 the game, 1 the second screen) */
static void load_overlay(int s, int which) {
	const char* scale = which ? cfg->mapScale : cfg->scale;
	const char* match = strcmp(cfg->screenMatch, "keep") && ui_screens_differ() ? cfg->screenMatch : NULL;
	/* Screen Placement: Closest to Gap moves the picture where it doesn't
	 * fill the screen's height (Pixel Perfect, Full Width): an overlay made
	 * for it has "-no_gap" at the end of its name, and ones without it are left
	 * alone (their hole is where a centred picture goes) */
	const char* place = nScreens == 2 && !strcmp(cfg->screenPlace, "no_gap") && (!strcmp(scale, "integer") || !strcmp(scale, "fit")) ? "-no_gap" : "";
	char key[sizeof(overlayMode[0])];
	snprintf(key, sizeof(key), "%d|%s|%s|%s", which, scale, match ? match : "", place);
	if (!strcmp(overlayMode[s], key)) return;
	snprintf(overlayMode[s], sizeof(overlayMode[s]), "%s", key);
	ui_free_image(overlayTex[s]);
	const char* sides[2] = { s == 1 && nScreens == 2 ? "bottom" : "top", which ? "secondary" : "primary" };
	char names[20][64];
	int n = 0;
	for (int g = 0; g < 2; ++g) { /* this game's, then every game's */
		char code[8] = "";
		if (!g) snprintf(code, sizeof(code), "%.8s-", emu.gameCode);
		for (int k = 0; k < 2; ++k) if (match) snprintf(names[n++], 64, "%s%s-%s-%s", code, sides[k], scale, match);
		for (int k = 0; k < 2; ++k) if (match) snprintf(names[n++], 64, "%s%s-%s", code, sides[k], match);
		for (int k = 0; k < 2; ++k) snprintf(names[n++], 64, "%s%s-%s", code, sides[k], scale);
		for (int k = 0; k < 2; ++k) snprintf(names[n++], 64, "%s%s", code, sides[k]);
		if (!g && which) {
			snprintf(names[n++], 64, "%.8s-%s", emu.gameCode, scale); /* older names */
			snprintf(names[n++], 64, "%.8s", emu.gameCode);
		}
	}
	char path[1200];
	for (int i = 0; i < n; ++i) {
		snprintf(path, sizeof(path), "%s/overlays/%s%s.png", cfg->assetDir, names[i], place);
		if (ui_load_image(path, overlayTex[s], &overlayW[s], &overlayH[s], true)) {
			if (debug) fprintf(stderr, "overlay (%s screen): %s\n", sides[0], path);
			return;
		}
	}
}

/* SNES DS's own bezel round screen s's picture (bezel.c): in the player's
 * colours and with their logo setting (Overlays: Generated; while the
 * colour picker is open, its colour as it's picked), or in the defaults
 * (a screen without a PNG in Overlays: Custom). Nothing where the picture
 * fills the screen. */
/* The profile, when the second screen takes touch (SM-7) */
static const MapProfile* touch_prof(void) {
	const MapProfile* p = mc ? mapcap_profile(mc) : NULL;
	return p && p->touch.scrollX && cfg->map && nScreens == 2 ? p : NULL;
}

static int round8(int v) {
	return (v + (v >= 0 ? 4 : -4)) / 8 * 8;
}

/* SM-7 with the whole map (touch.bgW): the frame's inside shows a window
 * of it, the game's own view moved by the drag */
static bool stitch_on(void) {
	const MapProfile* tp = touch_prof();
	return tp && tp->touch.bgW && mapRes.bigOk && mapRes.big && mapRes.page == 0 && viewPage == 0;
}

/* the window's top left in the whole map: the committed scroll, and the
 * drag under way; kept on the map (the game's own view always allowed) */
static void stitch_window(int* ox, int* oy) {
	const MapProfile* tp = touch_prof();
	int dx = viewDx, dy = viewDy;
	if (mapDrag) {
		SDL_Rect img = ui_image_rect(1 - game_screen(), mapW, mapH, cfg->mapScale);
		if (img.w > 0 && img.h > 0) dx -= (dragX - dragX0) * mapW / img.w, dy -= (dragY - dragY0) * mapH / img.h;
	}
	int bx = mapRes.bigX, by = mapRes.bigY;
	*ox = SDL_clamp(bx + dx, SDL_min(0, bx), SDL_max(mapRes.bigW - tp->touch.inner.w, bx));
	*oy = SDL_clamp(by + dy, SDL_min(0, by), SDL_max(mapRes.bigH - tp->touch.inner.h, by));
}

/* a captured frame with its inside from the whole map, at the window */
static const uint32_t* stitch_compose(const uint32_t* px) {
	static uint32_t* buf;
	static size_t cap;
	const MapProfile* tp = touch_prof();
	size_t n = (size_t) mapW * mapH;
	if (cap < n) {
		uint32_t* b = realloc(buf, n * sizeof(uint32_t));
		if (!b) return px;
		buf = b, cap = n;
	}
	memcpy(buf, px, n * sizeof(uint32_t));
	int ox, oy;
	stitch_window(&ox, &oy);
	int W = mapRes.bigW, H = mapRes.bigH;
	int cx = tp->touch.inner.x - tp->crop_x, cy = tp->touch.inner.y - tp->crop_y;
	for (int y = 0; y < tp->touch.inner.h && cy + y < mapH; ++y) {
		const uint32_t* row = &mapRes.big[(((oy + y) % H + H) % H) * W];
		for (int x = 0; x < tp->touch.inner.w && cx + x < mapW; ++x) buf[(cy + y) * mapW + cx + x] = row[((ox + x) % W + W) % W];
	}
	return buf;
}

/* the map shown again as it is (its window moved) */
static void map_reshow(void) {
	if (mapShownIdx < 0) return;
	mapReupload = true;
	upload_map_frame(mapShownIdx);
}

/* A touch on the second screen (SM-7): a drag scrolls the map, a tap on
 * the profile's plate switches the page; never the menu. 0: not the
 * second screen's; 1: taken; 2: taken, and the view changed (capture). */
static int map_touch(const Pointer* p) {
	if (nat) return native_touch(p);
	if (alt) return alt_touch(p);
	const MapProfile* tp = touch_prof();
	if (!tp || (p->screen == game_screen() && !(mapDrag && p->kind != 1))) return 0;
	if (p->kind == 1) {
		mapDrag = mapShownIdx >= 0 && !logoForced;
		dragX0 = dragX = p->x, dragY0 = dragY = p->y;
		return 1;
	}
	if (!mapDrag) return 1;
	dragX = p->x, dragY = p->y;
	if (p->kind != 2) {
		if (stitch_on()) map_reshow(); /* (the whole map: the window follows the finger) */
		return 1;
	}
	if (stitch_on() && (abs(dragX - dragX0) > 8 || abs(dragY - dragY0) > 8)) {
		/* the whole map: the drag kept where it ended, no capture */
		int ox, oy;
		stitch_window(&ox, &oy);
		viewDx = ox - mapRes.bigX, viewDy = oy - mapRes.bigY;
		mapDrag = false;
		map_reshow();
		return 1;
	}
	mapDrag = false;
	int s = 1 - game_screen();
	SDL_Rect img = ui_image_rect(s, mapW, mapH, cfg->mapScale);
	if (img.w <= 0 || img.h <= 0) return 1;
	/* (SNES pixels) */
	int ddx = (dragX - dragX0) * mapW / img.w, ddy = (dragY - dragY0) * mapH / img.h;
	if (abs(ddx) < 4 && abs(ddy) < 4) {
		int sx = (dragX - img.x) * mapW / img.w + tp->crop_x, sy = (dragY - img.y) * mapH / img.h + tp->crop_y;
		const __typeof__(tp->touch.tap[0])* t = &tp->touch.tap[viewPage & 1];
		if (sx < t->x || sy < t->y || sx >= t->x + t->w || sy >= t->y + t->h) return 1;
		viewPage ^= 1, viewDx = viewDy = 0;
	} else if (viewPage == 0) {
		/* (the map follows the finger: the view goes the other way) */
		viewDx -= round8(ddx), viewDy -= round8(ddy);
	} else {
		return 1;
	}
	if (debug) fprintf(stderr, "map view: page %d, scrolled %d,%d\n", viewPage, viewDx, viewDy);
	mapcap_set_view(mc, viewPage, viewDx, viewDy);
	mapcap_cancel(mc);
	return 2;
}

/* While a drag is under way: the map's inside follows the finger (the
 * capture for where it ends replaces it) */
static void draw_drag_preview(int s) {
	const MapProfile* tp = touch_prof();
	if (!mapDrag || viewPage != 0 || !tp || stitch_on()) return;
	SDL_Rect img = ui_image_rect(s, mapW, mapH, cfg->mapScale);
	/* the frame's inside (crop coordinates): x 6 to 249, y 46 to 191 on screen */
	int ix0 = 6, iy0 = 46 - tp->crop_y, ix1 = 250, iy1 = 192 - tp->crop_y;
	SDL_Rect in = { img.x + ix0 * img.w / mapW, img.y + iy0 * img.h / mapH, 0, 0 };
	in.w = img.x + ix1 * img.w / mapW - in.x, in.h = img.y + iy1 * img.h / mapH - in.y;
	ui_fill(s, in, (SDL_Color) { 0, 0, 0, 255 });
	SDL_Rect clip = { in.x + screens[s].rect.x, in.y + screens[s].rect.y, in.w, in.h };
	ui_clip(s, &clip);
	/* only the frame's inside moves (the frame, the area's name and the
	 * plates stay where they are) */
	SDL_Rect src = { ix0, iy0, ix1 - ix0, iy1 - iy0 };
	SDL_Rect moved = { in.x + dragX - dragX0, in.y + dragY - dragY0, in.w, in.h };
	ui_image_part(s, mapTex, src, moved);
	ui_clip(s, NULL);
}

/* The profile's hide rectangles (SM-6), black over the game's picture */
static void draw_hud_hide(int s) {
	const MapProfile* p = mc ? mapcap_profile(mc) : natProf;
	if (!hudHide || !p || picW <= 0 || picH <= 0) return;
	SDL_Rect img = ui_image_rect(s, picW, picH, cfg->scale);
	for (int i = 0; i < 2 && p->hide[i].w > 0; ++i) {
		int x0 = img.x + p->hide[i].x * img.w / picW, x1 = img.x + (p->hide[i].x + p->hide[i].w) * img.w / picW;
		int y0 = img.y + p->hide[i].y * img.h / picH, y1 = img.y + (p->hide[i].y + p->hide[i].h) * img.h / picH;
		ui_fill(s, (SDL_Rect) { x0, y0, x1 - x0, y1 - y0 }, (SDL_Color) { 0, 0, 0, 255 });
	}
}

/* w x h: the picture inside (the game's, the map's or the logo's) */
static void draw_bezel(int s, const char* mode, bool defaults, int w, int h) {
	SDL_Rect r = ui_image_rect(s, w, h, mode), sr = screens[s].rect;
	/* (Zoom: the part on the screen) */
	int x0 = SDL_max(r.x, 0), y0 = SDL_max(r.y, 0), x1 = SDL_min(r.x + r.w, sr.w), y1 = SDL_min(r.y + r.h, sr.h);
	if (x0 <= 0 && y0 <= 0 && x1 >= sr.w && y1 >= sr.h) return;
	r = (SDL_Rect) { x0, y0, x1 - x0, y1 - y0 };
	uint32_t in = BEZEL_INNER_DEFAULT, out = BEZEL_OUTER_DEFAULT;
	bool logo = true;
	if (!defaults) {
		in = hex_rgb(cfg->bezelInner), out = hex_rgb(cfg->bezelOuter), logo = cfg->bezelLogo;
		if (pickWhich == 0) in = pick_rgb();
		if (pickWhich == 1) out = pick_rgb();
	}
	bool bottom = s == 1 && nScreens == 2;
	/* the logo by the hinge (below the top screen's picture, above the
	 * bottom one's); Screen Placement: Closest to Gap leaves no room there,
	 * so it's on the bottom screen only, below the picture (GEN-12) */
	bool above = bottom;
	if (nScreens == 2 && !strcmp(cfg->screenPlace, "no_gap")) {
		if (!bottom) logo = false;
		above = false;
	}
	char key[96];
	snprintf(key, sizeof(key), "%dx%d %d,%d,%d,%d %d%d %06X %06X %d", sr.w, sr.h, r.x, r.y, r.w, r.h, bottom, above, (unsigned) in, (unsigned) out, logo);
	if (strcmp(key, bezelKey[s])) {
		snprintf(bezelKey[s], sizeof(bezelKey[s]), "%s", key);
		ui_free_image(bezelTex[s]);
		Uint64 t0 = SDL_GetPerformanceCounter();
		SDL_Surface* srf = bezel_surface(sr.w, sr.h, r, above, in, out, logo);
		if (srf) ui_load_surface(srf, bezelTex[s], &bezelW[s], &bezelH[s], false);
		if (debug) fprintf(stderr, "bezel (%s screen): %s, %.1f ms\n", bottom ? "bottom" : "top", key,
		                   (SDL_GetPerformanceCounter() - t0) * 1000.0 / SDL_GetPerformanceFrequency());
	}
	if (bezelTex[s][0] || bezelTex[s][1]) ui_image(s, bezelTex[s], bezelW[s], bezelH[s], "stretch", NULL);
}

/* Overlays: on for this physical screen (one screen: the top one's setting) */
static bool overlay_on(int s) {
	return cfg->overlayMode != OVM_OFF && (cfg->overlays & (s == 1 && nScreens == 2 ? OVL_BOTTOM : OVL_TOP));
}

/* which: what's on the screen (0 the game, 1 the second screen) */
static void draw_overlay(int s, int which) {
	if (cfg->overlayMode == OVM_CUSTOM) {
		load_overlay(s, which);
		if (overlayTex[s][0] || overlayTex[s][1]) {
			ui_image(s, overlayTex[s], overlayW[s], overlayH[s], "stretch", NULL);
			return;
		}
	}
	/* round the second screen's picture: the map at its own size (a crop
	 * smaller than the SNES screen is scaled up on its own), else a whole
	 * SNES screen (the logo) */
	bool map = which && mapShownIdx >= 0 && !logoForced;
	int w = !which ? picW : map ? mapW : SNES_W, h = !which ? picH : map ? mapH : SNES_H;
	draw_bezel(s, which ? cfg->mapScale : cfg->scale, cfg->overlayMode != OVM_GENERATED, w, h);
}

/* Text size for the FPS counter and messages: fits the empty band (gap px tall)
 * above or below the picture, even in full-width scaling. */
static int small_text_px(int s, int gap) {
	return SDL_max(1, SDL_min(SDL_min(ui_px(s) - 1, 3), (gap - 6) / 7)); /* 7 rows tall, 3px margin each side */
}

/* Messages ("RESUMED", "STATE SAVED"...): bottom left of the touchscreen (the
 * bottom screen), in the band below the picture, at the FPS counter's size. */
static bool shotPending; /* Take Screenshot: capture the next frame drawn */

static void draw_osd(void) {
	const char* osd = ui_osd_text();
	if (!osd || menuOpen || shotPending || barShown > 0) return; /* (the quick bar shows it) */
	int s = ui_touch_screen();
	SDL_Rect r = screens[s].rect;
	bool gameHere = nScreens == 1 || s == game_screen() || !((mc || nat || logoOnly) && cfg->map);
	SDL_Rect img = ui_image_rect(s, picW, picH, gameHere ? cfg->scale : cfg->mapScale);
	int gap = r.h - (img.y + img.h);
	int fpx = small_text_px(s, img.y < gap ? img.y : gap);
	int fy = gap >= 7 * fpx + 2 ? img.y + img.h + (gap - 7 * fpx) / 2 : r.h - fpx * 9;
	int fx = img.x + fpx * 2, cy = fy + fpx * 7 / 2, cap = fpx * 6;
	ui_font_text(s, TF_BOLD, fx + SDL_max(1, fpx / 2), cy + SDL_max(1, fpx / 2), cap, osd, (SDL_Color) { 0, 0, 0, 255 }, -1);
	ui_font_text(s, TF_BOLD, fx, cy, cap, osd, C_TEXT, -1);
}

/* The game's logo at its own size (GEN-1) onto a cw x ch canvas: k canvas
 * pixels to a logo pixel (one GBA pixel each for an extracted logo),
 * centred, blended over what's there at `alpha` (255: opaque; the logo
 * alone, with nothing live to show, at half: LOGO_IDLE); with `top` (a HUD
 * above it), centred in the rows below that. A user's own logo too big
 * for that is shrunk to fit. False if there's no logo. */
#define LOGO_IDLE 128
static bool logo_draw(uint32_t* px, int cw, int ch, int k, int top, int alpha) {
	bool pixelArt = false;
	int sub = 1;
	SDL_Surface* lg = ui_logo_surface(cfg, romPath, emu.gameCode, &pixelArt, &sub);
	if (!lg) return false;
	int room = ch - top;
	if (room <= 0) {
		/* (no room below what fills the screen) */
		SDL_FreeSurface(lg);
		return false;
	}
	int w = SDL_max(1, lg->w * k / sub), h = SDL_max(1, lg->h * k / sub);
	if (sub > k) {
		/* a scraped logo at full resolution, onto a canvas with fewer
		 * pixels per GBA pixel: averaged down to them (GEN-11) */
		SDL_Surface* sm = ui_resample(lg, w, h);
		if (sm) SDL_FreeSurface(lg), lg = sm;
	}
	if (w > cw || h > room) {
		float sc = SDL_min((float) cw / lg->w, (float) room / lg->h);
		w = SDL_max(1, (int) (lg->w * sc)), h = SDL_max(1, (int) (lg->h * sc));
	}
	/* centred on the canvas, or with a HUD above it in the room below */
	int x0 = (cw - w) / 2, y0 = top ? top + (room - h) / 2 : (ch - h) / 2;
	SDL_LockSurface(lg);
	for (int y = 0; y < h; ++y) {
		const uint8_t* row = (const uint8_t*) lg->pixels + (size_t) (y * lg->h / h) * lg->pitch;
		for (int x = 0; x < w; ++x) {
			const uint8_t* p = row + (size_t) (x * lg->w / w) * 4; /* RGBA32: bytes R, G, B, A */
			int a = p[3] * alpha / 255;
			if (!a) continue;
			uint32_t* o = &px[(y0 + y) * cw + x0 + x];
			uint32_t out = 0xFF000000;
			for (int c = 0; c < 3; ++c) {
				int bg = (*o >> (c * 8)) & 0xFF;
				out |= (uint32_t) (bg + (p[c] - bg) * a / 255) << (c * 8); /* ABGR8888 */
			}
			*o = out;
		}
	}
	SDL_UnlockSurface(lg);
	SDL_FreeSurface(lg);
	return true;
}

/* The game's logo as a GBA screen would show it: centred on black in a
 * 240x160 picture, at half strength (nothing live to show), drawn with the second screen's scaling and the LCD grid.
 * Logos extracted from the ROM are at GBA resolution; a user's own logo
 * that's bigger stays a high-resolution image (logoCanvasTex NULL). */
static void make_logo_canvas(void) {
	ui_free_image(logoCanvasTex);
	bool pixelArt = false;
	int sub = 1;
	SDL_Surface* lg = ui_logo_surface(cfg, romPath, emu.gameCode, &pixelArt, &sub);
	if (!lg) return;
	logoSub = sub;
	bool big = lg->w > SNES_W * sub || lg->h > SNES_H * sub;
	SDL_FreeSurface(lg);
	if (big) return;
	if (sub > 1) {
		/* a scraped logo at full resolution (GEN-11): the same picture at
		 * sub pixels per GBA pixel, smoothly scaled to the GBA picture's place */
		int cw = SNES_W * sub, ch = SNES_H * sub;
		uint32_t* big4 = malloc((size_t) cw * ch * 4);
		if (!big4) return;
		for (int i = 0; i < cw * ch; ++i) big4[i] = 0xFF000000u; /* (OLED black, the owner's choice) */
		logo_draw(big4, cw, ch, sub, 0, LOGO_IDLE);
		for (int i = 0; i < nViews; ++i) {
			logoCanvasTex[i] = SDL_CreateTexture(views[i].ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, cw, ch);
			if (!logoCanvasTex[i]) continue;
			SDL_SetTextureBlendMode(logoCanvasTex[i], SDL_BLENDMODE_NONE);
			SDL_SetTextureScaleMode(logoCanvasTex[i], SDL_ScaleModeLinear);
			ui_update_texture(logoCanvasTex[i], big4, cw * 4);
		}
		free(big4);
		return;
	}
	static uint32_t px[SNES_W * SNES_H];
	for (int i = 0; i < SNES_W * SNES_H; ++i) px[i] = 0xFF000000u; /* (OLED black, the owner's choice) */
	logo_draw(px, SNES_W, SNES_H, 1, 0, LOGO_IDLE);
	for (int i = 0; i < nViews; ++i) {
		logoCanvasTex[i] = SDL_CreateTexture(views[i].ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, SNES_W, SNES_H);
		if (!logoCanvasTex[i]) continue;
		SDL_SetTextureBlendMode(logoCanvasTex[i], SDL_BLENDMODE_NONE);
		ui_update_texture(logoCanvasTex[i], px, SNES_W * 4);
	}
}

static void draw_game(void) {
	bool mapOn = (mc || nat || alt || logoOnly) && cfg->map; /* logo-only games: the logo stands in for the map */
	if (nat && natShown) native_step();
	if (alt && altShown && altPage == 1 && !alttp_in_dungeon(alt)) alt_step();
	for (int s = 0; s < nScreens; ++s) {
		bool showGame = nScreens == 1 ? !(singleShowsMap && mapOn) : (s == game_screen() || !mapOn);
		SDL_Rect r = screens[s].rect;
		if (showGame) {
			if (nScreens == 2 && !mapOn && s != game_screen()) {
				/* no map: the other screen black, in its bezel if it has one */
				if (overlay_on(s)) draw_overlay(s, 1);
				continue;
			}
			ui_image_pixels(s, gameTex, picW, picH, cfg->scale, SHADER_ON);
			draw_hud_hide(s);
			if (overlay_on(s)) draw_overlay(s, 0);
			if (cfg->showFps) {
				/* sized to sit in the empty band above the picture */
				char buf[48];
				snprintf(buf, sizeof(buf), "%.1f FPS  %d%%", fps, (int) (speedPct + 0.5));
				SDL_Rect img = ui_image_rect(s, picW, picH, cfg->scale);
				int gap = img.y;
				int fpx = small_text_px(s, gap);
				int fy = gap >= 7 * fpx + 2 ? (gap - 7 * fpx) / 2 : fpx * 2;
				int fx = img.x + fpx * 2;
				ui_text(s, fx + 1, fy + 1, fpx, buf, (SDL_Color) { 0, 0, 0, 255 });
				ui_text(s, fx, fy, fpx, buf, C_TEXT);
			}
			continue;
		}
		/* the map screen */
		if (nat && natShown) {
			ui_fill(s, (SDL_Rect) { 0, 0, r.w, r.h }, borderColor);
			draw_native(s);
		} else if (alt && alttp_title(alt) && nScreens == 2) {
			alt_sword_load();
			draw_alt_title(s);
		} else if (alt && altShown && !(altPage == 1 && alttp_in_special(alt))) {
			/* (ALTTP-12: in a special area the map gives way to the logo) */
			ui_fill(s, (SDL_Rect) { 0, 0, r.w, r.h }, borderColor);
			draw_alt(s);
		} else if (mapShownIdx >= 0 && !logoForced) {
			ui_fill(s, (SDL_Rect) { 0, 0, r.w, r.h }, borderColor);
			ui_image_pixels(s, mapTex, mapW, mapH, cfg->mapScale, SHADER_ON);
			draw_drag_preview(s);
		} else if ((logoCanvasTex[0] || logoCanvasTex[1]) && logoSub > 1) {
			/* a scraped logo at full resolution (GEN-11): its picture made at
			 * LOGO_SUB pixels per GBA pixel, onto where the GBA picture goes */
			SDL_Rect area = ui_image_rect(s, SNES_W, SNES_H, cfg->mapScale);
			ui_image(s, logoCanvasTex, SNES_W * logoSub, SNES_H * logoSub, "stretch", &area);
		} else if (logoCanvasTex[0] || logoCanvasTex[1]) {
			/* no map yet (title screen, intro...): the game's logo, as if the
			 * game drew it on a second GBA screen */
			ui_image_pixels(s, logoCanvasTex, SNES_W, SNES_H, cfg->mapScale, SHADER_ON);
		} else if (logoTex[0] || logoTex[1]) {
			/* a user's high-resolution logo: fitted to where the picture goes */
			SDL_Rect area = ui_image_rect(s, SNES_W, SNES_H, cfg->mapScale);
			ui_image(s, logoTex, logoW, logoH, logoPixel ? "integer" : "fit", &area);
		} else if (logoOnly) {
			/* no logo to show (none extracted for this game yet): its name */
			const MapProfile* tp = profile_find(emu.gameCode);
			SDL_Rect area = ui_image_rect(s, SNES_W, SNES_H, cfg->mapScale);
			int in = area.w / 10;
			area = (SDL_Rect) { area.x + in, area.y + in, area.w - in * 2, area.h - in * 2 };
			if (tp) ui_title_block(s, area, tp->title, (int) (ui_px(s) * 14), (SDL_Color) { 236, 236, 244, 255 });
		}
		/* whatever this screen shows: map, logo or nothing */
		if (overlay_on(s)) draw_overlay(s, 1);
		if (mapShownIdx >= 0 && mapInactive && !logoForced) {
			ui_dim(s, 120);
		}
	}
	draw_osd();
}

static void close_ach(void) {
	if (achOpen) ra_list_close();
	achOpen = false;
}

/* the list's area: below the Back / title boxes */
static SDL_Rect ach_area(int scr) {
	SDL_Rect r = screens[scr].rect;
	float px = ui_px(scr);
	return (SDL_Rect) { px * 4, px * 25, r.w - px * 8, r.h - px * 27 };
}

static void open_menu(void) {
	bar_close();
	barShown = 0;
	cheatsOpen = false;
	patchesOpen = false;
	ovl_close_all();
	close_ach();
	scopeAll = false;
	set_page(0);
	menuOpen = true;
	menuSel = 0;
	menuPressed = -1;
	menuNote[0] = 0;
	armedItem = -1;
}

/* ---- settings in layers (docs/MENUS.md): states/prefs.cfg for all games,
 * states/<game>.cfg over it for this one. uniCfg holds the settings for all
 * games while the game runs (cfg has this game's put over them). */
static Config uniCfg;
static const char* const gameHeader = "# this game's own settings: they win over the ones for all games";
static const char* const prefsHeader = "# written by snes-ds's menus; overrides snes-ds.cfg";

static void game_keys_reload(void) {
	nGameKeys = config_kv_keys(gp.settings, 48, gameKeys);
}

static void write_setting(const char* path, const char* header, const Config* c, const Setting* st) {
	char lines[2][2][24];
	const char* kv[2][2];
	int n = config_setting_lines(c, st, lines);
	for (int i = 0; i < n; ++i) kv[i][0] = lines[i][0], kv[i][1] = lines[i][1];
	config_kv_update(path, header, n, kv);
}
static void prefs_file(char* out, size_t n) {
	snprintf(out, n, "%s/prefs.cfg", cfg->stateDir);
}

/* a choice made some other way (the hotkeys, the overlay page): kept in the layer
 * in force, this game's own file if it has a value there, else all games' */
static void commit_current(const char* key) {
	const Setting* st = config_setting(key);
	if (!st) return;
	if (game_has(st)) {
		write_setting(gp.settings, gameHeader, cfg, st);
	} else {
		char path[1100];
		prefs_file(path, sizeof(path));
		config_setting_copy(&uniCfg, cfg, st);
		write_setting(path, prefsHeader, &uniCfg, st);
	}
}

/* put a changed setting to use in the game now */
static void setting_applied(const Setting* st) {
	const char* k = st->key;
	if (!strcmp(k, "shader") || !strcmp(k, "lcd_strength")) {
		if (cfg->shader != SH_OFF) cfg->shaderLast = cfg->shader;
		ui_shader(cfg->shader);
		rebuild_textures();
	} else if (!strcmp(k, "color_correction")) {
		ui_color_correction(cfg->colorCorrection);
		rebuild_textures(); /* (every picture given again, through it or not) */
	} else if (!strcmp(k, "lcd_ghosting")) {
		ghostHave = false;
	} else if (!strcmp(k, "swap")) {
		cfg->swapChosen = true;
		if (emu.fb) {
			game_tex_fit(emu.w, emu.h);
			for (int i = 0; i < nViews; ++i) ui_update_texture(gameTex[i], emu.fb, emu.w * 4);
		}
	} else if (!strcmp(k, "screen_match") || !strcmp(k, "screen_placement")) {
		ui_layout_pictures();
		overlays_reload(); /* (overlays can be made for each: overlay files) */
		gameTexAll = true;
	} else if (!strcmp(k, "brightness_top") || !strcmp(k, "brightness_bottom")) {
		ui_brightness(cfg->brightTop, cfg->brightBottom);
	} else if (!strcmp(k, "match_display")) {
		pacedFallback = false;
		pacing_update();
	} else if (!strcmp(k, "rewind") || !strcmp(k, "rewind_seconds")) {
		rewind_setup();
	} else if (!strcmp(k, "custom_logos")) {
		logo_refresh();
	} else if (!strcmp(k, "ab_swap") || !strncmp(k, "button_", 7)) {
		fb_default();
	}
}

static void rows_note(const char* what, bool all, bool kept) {
	if (menuNote[0] && !strncmp(menuNote, "Takes", 5)) return;
	snprintf(menuNote, sizeof(menuNote), "%s%s", all ? tr("Saved for all games"): tr("Saved for this game"), kept ? ": this game keeps its own value" : "");
	(void) what;
}

/* where a setting stands on its list (or its number), to tell when a step wrapped round */
static int step_pos(const Config* c, const Setting* st) {
	if (st->nChoices) return config_setting_index(c, st);
	return *(const int*) ((const char*) c + st->off);
}

/* step row `key` by dir, for all games or this game. For this game the list has
 * one more place, "same as all games": stepping past either end of the list
 * from a value of this game's own takes it away (row_reset) */

static void row_reset(const char* key);
static void row_change(const char* key, int dir, bool all) {
	menuNote[0] = 0;
	if (!strcmp(key, "@overlay")) {
		ovlOpen = true;
		ovlSel = 0;
		ovlPress = -1;
		return;
	}
	const Setting* st = config_setting(key);
	if (!st) return;
	const char* off = row_disabled(key);
	if (off) {
		snprintf(menuNote, sizeof(menuNote), "%s", off);
		return;
	}
	char path[1100];
	if (all) {
		if (!config_setting_step(&uniCfg, st, dir)) return;
		prefs_file(path, sizeof(path));
		write_setting(path, prefsHeader, &uniCfg, st);
		bool kept = game_has(st);
		if (!kept) {
			config_setting_copy(cfg, &uniCfg, st);
			setting_applied(st);
		}
		rows_note(key, true, kept);
	} else {
		int before = step_pos(cfg, st);
		bool had = game_has(st);
		if (!config_setting_step(cfg, st, dir)) return;
		int after = step_pos(cfg, st);
		if (had && (dir > 0 ? after < before : after > before)) {
			row_reset(key); /* (round the end of the list: back to the value for all games) */
			return;
		}
		write_setting(gp.settings, gameHeader, cfg, st);
		game_keys_reload();
		setting_applied(st);
		rows_note(key, false, false);
	}
	fprintf(stderr, "setting: %s = %s (%s)\n", key, st->choices ? st->choices[config_setting_index(cfg, st) < 0 ? 0 : config_setting_index(cfg, st)].value : "?", all ? "all games" : "this game");
}

/* X: this game goes back to the value for all games */
static void row_reset(const char* key) {
	menuNote[0] = 0;
	const Setting* st = config_setting(key);
	if (!st) return;
	if (!game_has(st)) {
		snprintf(menuNote, sizeof(menuNote), tr("Already the value for all games"));
		return;
	}
	char lines[2][2][24];
	const char* keys[2];
	int n = config_setting_lines(cfg, st, lines);
	for (int i = 0; i < n; ++i) keys[i] = lines[i][0];
	config_kv_remove(gp.settings, n, keys);
	game_keys_reload();
	config_setting_copy(cfg, &uniCfg, st);
	setting_applied(st);
	snprintf(menuNote, sizeof(menuNote), tr("Same as all games again"));
}

/* the controller on a settings page (the bar, then the rows) */
static void rows_input(void) {
	int n = rowN[curPage];
	if (btn_pressed(B_DOWN)) rowSel = (rowSel + 1) % (n + 1), menuNote[0] = 0;
	if (btn_pressed(B_UP)) rowSel = (rowSel + n) % (n + 1), menuNote[0] = 0;
	rows_scroll_to_sel();
	if (rowSel == 0) {
		if (btn_pressed(B_LEFT)) scopeAll = false;
		if (btn_pressed(B_RIGHT)) scopeAll = true;
		if (btn_pressed(B_A)) scopeAll = !scopeAll;
		return;
	}
	const char* key = rowKeys[curPage][rowSel - 1];
	bool all = scopeAll || btnNow[B_Y]; /* (Y held with a button: all games) */
	int dir = btn_pressed(B_LEFT) ? -1 : (btn_pressed(B_RIGHT) || btn_pressed(B_A)) ? 1 : 0;
	if (!dir && btn_pressed(B_Y)) dir = 1, all = true;
	if (dir) row_change(key, dir, all);
	if (btn_pressed(B_X) && key[0] != '@') row_reset(key);
}

/* a touch on a settings page, on its own: the bar sets which it changes, a row is
 * changed when the finger lifts on it (the left arrow steps back, the Game tag
 * takes this game's value away, the rest steps forward) */
static void rows_touch(const Pointer* p) {
	int n = rowN[curPage];
	if (p->kind == 1) {
		ignorePointerUp = false;
		rowPressed = -1;
		for (int h = 0; h < 2; ++h)
			if (in_rect(scope_rect(p->screen, h), p->x, p->y)) scopeAll = h == 1, rowSel = 0, menuNote[0] = 0, ignorePointerUp = true;
		for (int i = 1; i <= n; ++i)
			if (srow_shown(i) && in_rect(srow_rect(p->screen, i), p->x, p->y)) rowPressed = i, rowSel = i, rowDragY = p->y, rowDragged = false;
		if (rowPressed < 0) rowDragY = p->y, rowDragged = false;
	} else if (p->kind == 3 && n > ROW_VIS) {
		/* a finger dragged up or down moves the list a row at a time, and no longer counts as a tap */
		int step = SDL_max(1, srow_rect(p->screen, 2).y - srow_rect(p->screen, 1).y);
		if (abs(p->y - rowDragY) >= step) {
			rowTop = SDL_clamp(rowTop + (p->y < rowDragY ? 1 : -1), 0, n - ROW_VIS);
			rowDragY = p->y, rowDragged = true;
			ignorePointerUp = true;
		}
	} else if (p->kind == 2) {
		int i = rowPressed;
		if (!ignorePointerUp && !rowDragged && i >= 1 && srow_shown(i) && in_rect(srow_rect(p->screen, i), p->x, p->y)) {
			const char* key = rowKeys[curPage][i - 1];
			if (key[0] != '@' && rowTagOn[i] && in_rect(rowTag[i], p->x, p->y)) row_reset(key);
			else row_change(key, key[0] != '@' && in_rect(rowBack[i], p->x, p->y) ? -1 : 1, scopeAll);
		}
		rowPressed = -1;
		ignorePointerUp = false;
	}
}

/* Returns true if the session should end (*exitTo set). */
static bool activate(int item, enum GameExit* exitTo) {
	if (confirm_item(item) && armedItem != item) {
		armedItem = item;
		const char* lost = cfg->autoSave ? N_("your place is saved"): N_("unsaved progress is lost");
		if (item == M_RESTART) snprintf(menuNote, sizeof(menuNote), tr("Choose it again to restart: unsaved progress is lost"));
		else snprintf(menuNote, sizeof(menuNote), tr("Choose it again to close %s: %s"), item == M_EXIT ? "SNES DS" : tr("the game"), lost);
		return false;
	}
	armedItem = -1;
	switch (item) {
	case M_RESUME:
		menuOpen = false;
		break;
	case M_SHOT:
		menuOpen = false; /* taken from the next frame, without the menu */
		shotPending = true;
		break;
	case M_ACH:
		ra_list_open();
		achOpen = true;
		achPressed = false;
		break;
	case M_CHEATS:
		if (!cheats.n) snprintf(menuNote, sizeof(menuNote), tr("No cheats found (see Cheats in the wiki)"));
		else open_cheats();
		break;
	case M_PATCHES:
		open_patches();
		break;
	case M_SLOT:
		slot_step(1);
		break;
	case M_SAVE:
		if (state_save(gp.slot[stateSlot])) {
			char m[40];
			snprintf(m, sizeof(m), tr("State saved to slot %d"), stateSlot);
			ui_osd(m);
			menuOpen = false;
		} else {
			snprintf(menuNote, sizeof(menuNote), tr("Save failed"));
		}
		break;
	case M_LOAD:
		if (!item_enabled(M_LOAD)) {
			snprintf(menuNote, sizeof(menuNote), tr("Nothing saved in slot %d yet"), stateSlot);
		} else if (state_load(gp.slot[stateSlot])) {
			char m[40];
			snprintf(m, sizeof(m), tr("Slot %d loaded"), stateSlot);
			if (!stateWarned) ui_osd(m);
			menuOpen = false;
			return false;
		} else if (stateLoadKind == STATE_FILE_OTHER_CORE) {
			/* (RetroArch's, from another of its GBA cores: issue #39) */
			/* (Settings > Save States says RetroArch's core has to be mGBA) */
			snprintf(menuNote, sizeof(menuNote), tr("Saved by another emulator: can't load it"));
		} else if (stateLoadKind == STATE_FILE_UNREADABLE) {
			snprintf(menuNote, sizeof(menuNote), tr("This state can't be read"));
		} else {
			snprintf(menuNote, sizeof(menuNote), tr("Load failed"));
		}
		break;
	case M_BUG: {
		/* the moment the menu opened, and the 10 seconds before it */
		char what[200];
		if (bug_report(what, sizeof(what))) note_open(false); /* (a note, then it's saved) */
		else snprintf(menuNote, sizeof(menuNote), "%s", what);
		break;
	}
	case M_RESTART:
		emu_reset(&emu);
		aheadReady = false;
		bug_reset();
		ra_reset();
		mapShownIdx = -1; /* back to the logo until the next map */
		menuOpen = false;
		ui_osd("Game restarted");
		break;
	case M_LIBRARY:
		*exitTo = GAME_TO_LIBRARY;
		return true;
	case M_EXIT:
		*exitTo = GAME_QUIT_APP;
		return true;
	}
	return false;
}

/* Saves what's on the screens now (before ui_present) as
 * <screenshot_dir>/<game>-YYYYMMDD-HHMMSS.png, screens stacked top to bottom. */
static void take_screenshot(void) {
	char dir[600], path[1400];
	snprintf(dir, sizeof(dir), "%s", cfg->screenshotDir);
	mkdirs(dir);
	struct stat st;
	if (stat(dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
		snprintf(dir, sizeof(dir), "%s/screenshots", cfg->stateDir);
		mkdirs(dir);
	}
	time_t now = time(NULL);
	struct tm tmv;
	localtime_r(&now, &tmv);
	char stamp[32];
	strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tmv);
	snprintf(path, sizeof(path), "%s/%s-%s.png", dir, gp.base, stamp);
	for (int n = 2; file_exists(path) && n < 100; ++n) snprintf(path, sizeof(path), "%s/%s-%s-%d.png", dir, gp.base, stamp, n);
	ui_osd(ui_screenshot_stacked(path) ? "Screenshot saved" : "Screenshot failed");
}

/* ---------------------------------------------------------------- session */

/* The second screen's set-up for the profile with this code (the game's,
 * or for a compilation the game running in it: subCode): the map capture,
 * the HUD split, the fights' picture. */
static void ss_setup(const char* rom, const char* code) {
	viewPage = viewDx = viewDy = 0, mapDrag = false, movedKey = 0;
	mapW = SNES_W;
	mapH = SNES_H;
	memset(&mapRes, 0, sizeof(mapRes));
	mapShownIdx = -1;
	stillValid = false;
	mapInactive = false;
	singleShowsMap = false;
	logoOnly = false;
	snapProf = NULL;
	fb_load(); /* GEN-10 */
	if (cfg->map) {
#ifdef __ANDROID__
		android_set_pinning(cfg->pinCores);
#endif
		const MapProfile* lp = profile_find(code);
		native_open(rom, lp);
		mc = nat || alt ? NULL : mapcap_create(rom, code, true);
		if (nat) mapW = SMMAP_W, mapH = SMMAP_H;
		if (alt) mapW = ALTTP_W, mapH = ALTTP_H;
		logoOnly = lp && lp->logoOnly;
		snapProf = logoOnly && lp->snap.showMin ? lp : NULL;
		snapShow = snapRevert = 0;
		if (mc) mapcap_set_still(mc, !map_anim());
		if (logoOnly) fprintf(stderr, "logo only: %s\n", lp->title);
		if (mc) {
			fprintf(stderr, "map profile: %s\n", mapcap_title(mc));
			mapcap_size(mc, &mapW, &mapH);
		} else if (!logoOnly && !nat && !alt) {
			fprintf(stderr, "no map profile for %s\n", code);
		}
	}
}

/* the texture the second screen's set-up needs */
static void ss_textures(void) {
	for (int i = 0; i < nViews; ++i) {
		if (mapTex[i]) SDL_DestroyTexture(mapTex[i]);
		mapTex[i] = SDL_CreateTexture(views[i].ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, mapW, mapH);
		SDL_SetTextureBlendMode(mapTex[i], SDL_BLENDMODE_NONE);
	}
	native_textures();
	alt_textures();
}

/* After the device sleeps (or the renderer is reset), the GPU's copies of
 * the pictures may be gone: every texture is made again and given its
 * picture again (the game's comes with the next frame). */
const char* romPath;
static Uint32 lastLoop; /* the last pass of the play loop (SDL_GetTicks) */
static void rebuild_textures(void) {
	texW = texH = 0; /* (made again) */
	if (emu.fb) {
		game_tex_fit(emu.w, emu.h);
		for (int i = 0; i < nViews; ++i) ui_update_texture(gameTex[i], emu.fb, emu.w * 4);
	}
	ss_textures();
	if (mapShownIdx >= 0) mapReupload = true, upload_map_frame(mapShownIdx);
	ui_free_image(logoTex);
	if (romPath && ui_load_logo(cfg, romPath, emu.gameCode, logoTex, &logoW, &logoH, &logoPixel)) make_logo_canvas();
	else make_logo_canvas();
	logoProvisional = ui_logo_provisional();
	ui_textures_reset();
}

/* ---------------------------------------------------------------- patches (GEN-9)
 * The game's patches folder (made if it isn't there), what's switched on
 * (<game>.patches), and the patches every core opens the ROM with; with one
 * applied, the saves and states named "<game> + <patch>" as the switches
 * say. */

static void patches_setup(const char* rom) {
	char code[9] = "????????";
	emu_rom_code(rom, code);
	patch_folder(cfg, rom, code, patchDir, sizeof(patchDir), true);
	patch_choice_load(gp.patches, &patchChoice);
	patchStarted = patchChoice;
	char files[PATCH_ON_MAX + 1][1024];
	int n = patch_plan(cfg, rom, code, &patchChoice, files, PATCH_ON_MAX + 1);
	emu_patch_plan(rom, files, n);
	if (patchChoice.nOn) {
		/* their own saves only when something applies: patches that don't
		 * fit leave the game as it is, with its own saves */
		size_t sz = 0;
		int r = PATCH_NONE;
		free(emu_rom_read_played(rom, &sz, &r, NULL, NULL));
		if (r == PATCH_APPLIED || r == PATCH_PARTIAL) game_paths_patched(cfg, &gp, &patchChoice);
	}
	patchReload = patchReloadNext;
	patchReloadNext = false;
	if (!patchHandoffNext) patchHandoff[0] = 0;
	patchHandoffNext = false;
	char on[PATCH_ON_MAX * 260] = "";
	for (int i = 0, l = 0; i < patchChoice.nOn; ++i) l += snprintf(on + l, sizeof(on) - (size_t) l, "%s%s", i ? ", " : ", on: ", patchChoice.on[i]);
	fprintf(stderr, "patches: %s, %d to apply%s\n", patchDir, n, on);
}

/* The logo loaded again, and every picture it's in made again (extracted at
 * last, or a scraped / own logo's pixelate switched: GEN-11). */
static void logo_refresh(void) {
	ui_free_image(logoTex);
	ui_free_image(logoCanvasTex);
	logoSub = 1;
	bool got = ui_load_logo(cfg, romPath, emu.gameCode, logoTex, &logoW, &logoH, &logoPixel);
	logoProvisional = ui_logo_provisional();
	if (!got) return;
	make_logo_canvas();
}

static enum GameExit game_run_inner(Config* c, const char* rom, bool fromLibrary);

/* A game's own settings (issue #53, states/<game>.cfg) are put over the
 * settings for the length of the game: what the file changed goes back to
 * what it was when the game ends, unless it was changed again meanwhile
 * (a menu choice for all games, which is saved already). */
enum GameExit game_run(Config* c, const char* rom, bool fromLibrary) {
	GamePaths p;
	game_paths(c, rom, &p);
	Config before = *c;
	int n = config_load_game_file(c, p.settings);
	Config after = *c;
	uniCfg = before;
	if (n) {
		fprintf(stderr, "game settings: %d from %s\n", n, p.settings);
		ui_layout_pictures();
	}
	enum GameExit x = game_run_inner(c, rom, fromLibrary);
	ui_stick_dpad(true);
	/* back to the settings for all games (with what was changed for them meanwhile) */
	const unsigned char *b = (const unsigned char*) &before, *a = (const unsigned char*) &after;
	unsigned char* cur = (unsigned char*) c;
	for (size_t i = 0; i < sizeof(Config); ++i)
		if (a[i] != b[i] && cur[i] == a[i]) cur[i] = b[i];
	for (int i = 0; i < config_setting_count(); ++i) {
		const Setting* st = config_setting_at(i);
		if (config_key_per_game(st->key)) config_setting_copy(c, &uniCfg, st);
	}
	ui_layout_pictures();
	ui_brightness(c->brightTop, c->brightBottom);
	return x;
}

static enum GameExit game_run_inner(Config* c, const char* rom, bool fromLibrary) {
	cfg = c;
	debug = getenv("RETRODS_DEBUG") != NULL;
	romPath = rom, lastLoop = 0;
	game_paths(cfg, rom, &gp);
	ui_brightness(cfg->brightTop, cfg->brightBottom); /* (this game's own, if it has them) */
	game_keys_reload();
	patches_setup(rom); /* GEN-9: the patches folder, what's on, and the plan every core opens the ROM with */
	if (!emu_open(&emu, rom, gp.sav, true)) {
		ui_osd("Couldn't load the game");
		return fromLibrary ? GAME_TO_LIBRARY : GAME_QUIT_APP;
	}
	fprintf(stderr, "game %s (%s), save %s\n", gp.base, emu.gameCode, gp.sav);
	{
		char d[640];
		fprintf(stderr, "ROM: %s\n", dump_text(d, sizeof(d)));
	}
	if (emu.patch != PATCH_NONE) {
		ra_game_patched(); /* (RetroAchievements doesn't allow patched games) */
	} else {
		ra_game_start(&emu, rom);
	}
	cheats_load(&cheats, cfg, rom, emu.gameCode, gp.cheats);
	if (cheats_on(&cheats)) cheats_apply(&cheats, &emu);
	cheatsOpen = false;
	patchesOpen = false;
	ovl_close_all();
	ui_on_background(on_background);
	ui_on_foreground(on_foreground);

	mc = NULL;
	gameTexAll = true;
	frameCount = 0;
	mapAnimStart = 0; /* (the last game's count was further on) */
	menuOpen = false;
	snprintf(subCode, sizeof(subCode), "%.8s", emu.gameCode);
	ss_setup(rom, subCode);
	texW = texH = 0;
	game_tex_fit(emu.w, emu.h);
	ss_textures();
	unsigned bc = (unsigned) strtoul(cfg->borderColor, NULL, 16);
	borderColor = (SDL_Color) { (bc >> 16) & 0xFF, (bc >> 8) & 0xFF, bc & 0xFF, 255 };
	overlayMode[0][0] = overlayMode[1][0] = 0;
	bezelKey[0][0] = bezelKey[1][0] = 0;
	uint32_t logoGen = logo_generation();
	logoSub = 1;
	if (ui_load_logo(cfg, rom, emu.gameCode, logoTex, &logoW, &logoH, &logoPixel)) make_logo_canvas();
	logoProvisional = ui_logo_provisional();

	memset(pageLen, 0, sizeof(pageLen));
	memset(rowN, 0, sizeof(rowN));
	pages[PG_GAME][pageLen[PG_GAME]++] = M_RESUME;
	pages[PG_GAME][pageLen[PG_GAME]++] = M_RESTART;
	pages[PG_GAME][pageLen[PG_GAME]++] = M_SAVE;
	pages[PG_GAME][pageLen[PG_GAME]++] = M_LOAD;
	pages[PG_GAME][pageLen[PG_GAME]++] = M_SLOT;
	pages[PG_GAME][pageLen[PG_GAME]++] = M_CHEATS;
	pages[PG_GAME][pageLen[PG_GAME]++] = M_PATCHES;
	if (ra_enabled()) pages[PG_GAME][pageLen[PG_GAME]++] = M_ACH;
	pages[PG_GAME][pageLen[PG_GAME]++] = M_SHOT;
	pages[PG_GAME][pageLen[PG_GAME]++] = M_BUG;
	if (fromLibrary) pages[PG_GAME][pageLen[PG_GAME]++] = M_LIBRARY;
	pages[PG_GAME][pageLen[PG_GAME]++] = M_EXIT;
	ui_color_correction(cfg->colorCorrection); /* (Display > Color Correction, for this session's pictures) */
	ui_shader(cfg->shader);
#define ROW(pg, key) rowKeys[pg][rowN[pg]++] = key
	ROW(PG_DISPLAY, "scale");
	ROW(PG_DISPLAY, "map_scale");
	ROW(PG_DISPLAY, "shader");
	ROW(PG_DISPLAY, "lcd_strength");
	ROW(PG_DISPLAY, "color_correction");
	ROW(PG_DISPLAY, "lcd_ghosting");
	if (ui_logo_custom(cfg, romPath, emu.gameCode)) ROW(PG_DISPLAY, "custom_logos"); /* the game has the player's own or a scraped logo (GEN-11) */
	if (nScreens >= 2) ROW(PG_SCREENS, "swap");
	ROW(PG_SCREENS, "screen_match");
	ROW(PG_SCREENS, "screen_placement");
	if (nScreens >= 2) ROW(PG_SCREENS, "screen_gap_mm");
	ROW(PG_SCREENS, "overlays");
	ROW(PG_SCREENS, "overlay_screens");
	ROW(PG_SCREENS, "@overlay");
	ROW(PG_SCREENS, "brightness_top");
	if (nScreens >= 2) ROW(PG_SCREENS, "brightness_bottom");
	ROW(PG_CONTROLS, "left_stick_dpad");
	ROW(PG_CONTROLS, "ab_swap");
	ROW(PG_CONTROLS, "button_right");
	ROW(PG_CONTROLS, "button_bottom");
	ROW(PG_CONTROLS, "button_top");
	ROW(PG_CONTROLS, "button_left");
	ROW(PG_CONTROLS, "button_l");
	ROW(PG_CONTROLS, "button_r");
	ROW(PG_CONTROLS, "button_dpad_up");
	ROW(PG_CONTROLS, "button_dpad_down");
	ROW(PG_CONTROLS, "button_dpad_left");
	ROW(PG_CONTROLS, "button_dpad_right");
	ROW(PG_CONTROLS, "turbo_rate");
	ROW(PG_SPEED, "show_fps");
	ROW(PG_SPEED, "match_display");
	ROW(PG_SPEED, "frame_buffer");
	ROW(PG_SPEED, "rewind");
	ROW(PG_SPEED, "rewind_seconds");
	ROW(PG_SPEED, "fast_forward");
	ROW(PG_SPEED, "save_on_quit");
	ROW(PG_SPEED, "auto_load");
	ROW(PG_MAP, "map");
	ROW(PG_MAP, "map_interval");
#undef ROW
	set_page(0);
	slot_pick_newest();

	if (cfg->loadState[0]) {
		if (!state_load(cfg->loadState)) fprintf(stderr, "could not load %s\n", cfg->loadState);
		cfg->loadState[0] = 0;
	} else if (patchHandoff[0] && file_exists(patchHandoff)) {
		/* GEN-9: patches switched without upsetting the saves: on from that very moment */
		stateNoCheck = true; /* (saved with the ROM before the change, on purpose) */
		bool ok = state_load(patchHandoff);
		stateNoCheck = false;
		remove(patchHandoff);
		patchHandoff[0] = 0;
		if (!stateWarned) ui_osd(ok ? "Patches changed: carrying on from where you were" : "Patches changed");
	} else if (cfg->autoLoad && !patchReload && file_exists(gp.autoState)) { /* (not right after switching a patch) */
		if (state_load(gp.autoState)) {
			if (!stateWarned) ui_osd("Resumed. Tap or HOME for the menu");
		} else if (stateLoadKind == STATE_FILE_OTHER_CORE) {
			/* (RetroArch's automatic state, from another of its GBA cores) */
			ui_osd_long("Not resumed: RetroArch's automatic state is from another of its SNES cores. To share states, set RetroArch's SNES core to bsnes.");
		}
	} else {
		ui_osd("Tap the screen or press HOME for the menu");
	}
	if (cfg->barPinned) barOpen = true, barSince = SDL_GetTicks(); /* (pinned: up from the start) */
	rewind_setup();

	/* The device runs at a fixed rate. The GBA's own rate follows the game's
	 * sound settings (32768 Hz at power-on, 65536 Hz once Zero Mission is
	 * running...) and can change at any moment, so samples go through a
	 * converter that is rebuilt whenever it does. Opening the device at the
	 * rate of the moment made resumed games starve and fresh boots overflow. */
	SDL_AudioSpec want = { 0 }, have;
	want.freq = 48000;
	want.format = AUDIO_S16SYS;
	want.channels = 2;
	/* RETRODS_AUDIO_SAMPLES: testing only (SDL's dummy driver keeps exact time
	 * only when the buffer is a whole number of milliseconds, e.g. 480) */
	want.samples = getenv("RETRODS_AUDIO_SAMPLES") ? atoi(getenv("RETRODS_AUDIO_SAMPLES")) : 512;
	SDL_AudioDeviceID audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
#if !defined(__ANDROID__)
	if (!audio && getenv("AUDIODEV")) {
		/* the device launch.sh asked for (the RG DS Plus's Stock Linux OS: ALSA's pulse)
		 * won't open: the driver's own default instead */
		fprintf(stderr, "no audio on %s (%s): the default device instead\n", getenv("AUDIODEV"), SDL_GetError());
		unsetenv("AUDIODEV");
		SDL_AudioQuit();
		SDL_AudioInit(NULL);
		audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
	}
#endif
	if (!audio) {
		/* (a player on ROCKNIXDS heard nothing: the driver SDL picked may
		 * not reach the sound server; the others, in turn) */
		fprintf(stderr, "no audio with %s: %s\n", SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "no driver", SDL_GetError());
		static const char* const tryDrivers[] = { "pipewire", "pulseaudio", "alsa" };
		const char* first = SDL_GetCurrentAudioDriver();
		for (int i = 0; i < 3 && !audio; ++i) {
			if (first && !strcmp(first, tryDrivers[i])) continue;
			SDL_AudioQuit();
			if (SDL_AudioInit(tryDrivers[i]) != 0) continue;
			audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
			if (!audio) fprintf(stderr, "no audio with %s: %s\n", tryDrivers[i], SDL_GetError());
		}
		if (!audio) SDL_AudioQuit(), SDL_AudioInit(NULL);
	}
	if (audio) {
		SDL_PauseAudioDevice(audio, 0);
		fprintf(stderr, "audio: %s%s%s, %d Hz, %d channels, %d samples\n", SDL_GetCurrentAudioDriver(), getenv("AUDIODEV") ? " on " : "",
		        getenv("AUDIODEV") ? getenv("AUDIODEV") : "", have.freq, have.channels, have.samples);
	} else {
		fprintf(stderr, "no audio: %s\n", SDL_GetError());
	}
	SDL_AudioStream* audioConv = NULL;
	int convFrom = 0;
	/* The sound delay in use: the setting, raised a step when the sound keeps
	 * running dry (audio_auto; only for this run, the setting is left alone) */
	int audioMs = cfg->audioLatency;
	int targetQueued = want.freq * audioMs / 1000;
	uint64_t audioQuietUntil = 0; /* no dry-queue counting before this frame: the sound was being started again */
	uint64_t dryWindowStart = 0;
	int dryQueueBefore = 1, dryEvents = 0;
	/* testing only: RETRODS_TEST_STARVE=1 throws away the sound of 20 frames in every 100 (after frame 200) */
	const bool testStarve = getenv("RETRODS_TEST_STARVE") && atoi(getenv("RETRODS_TEST_STARVE"));

	static int16_t samples[4096 * 2];
	uint32_t lastRoomKey = 0;
	/* the room key as it is now, and for how many frames it's held */
	uint32_t seenRoomKey = 0;
	int seenRoomFor = 0;
	int sinceMapRequest = 1 << 20;
	bool mapWant = false; /* a capture is due: asked for before the next frame */
	uint32_t mapWantKey = 0;
	int lastFails = 0;
	bool forceMap = true, wasDark = false;
	long refreshAt = -1;
	hotkeys_setup();
	const int modBtn = hkMod;
	/* SELECT or START as the hotkey: a GBA button, held back from the game
	 * while it's down and handed over if it comes up with nothing pressed */
	const bool modIsGame = modBtn == B_SELECT || modBtn == B_START;
	bool comboUsed = false, ffModUsed = false; /* (ffModUsed: hotkey + R2 fast-forwarded) */
	int comboHold = -1, comboFrames = 0; /* menu combo: the button held back, frames since */
	int comboPulse[2] = { 0, 0 };        /* SELECT, START handed to the game late */
	int modPulse = 0; /* the hotkey button (SELECT or START), tapped alone: handed to the game */
	unsigned ffPhase = 0;
	bool noPace = getenv("RETRODS_NOPACE") != NULL;
	enum GameExit exitTo = GAME_QUIT_APP;
	bool done = false;
	bool wasMenu = false;

	pacedFallback = false; /* (a new game: matched again, if it's on) */
	pacing_update();
	const double freq = (double) SDL_GetPerformanceFrequency();
	double next = SDL_GetPerformanceCounter() / freq;
	double fpsT0 = next;
	int fpsFrames = 0;

#ifdef __ANDROID__
	/* a target well under the frame time: the system clocks the CPU so each
	 * frame's work takes about this long, leaving room for heavier frames
	 * (with the full 16.7 ms it clocked down until frames barely fit; with
	 * 65% the average was 11 ms and uneven frames went to 17-21 ms) */
	android_perf_init((int64_t) (frameSec * 0.5 * 1e9));
	android_set_pinning(cfg->pinCores);
	android_pin_game_thread();
#endif
	while (!done) {
		{
			/* the last iteration, if it was a stall (issue #50) */
			Uint64 t = SDL_GetPerformanceCounter();
			const double msPer = 1000.0 / (double) SDL_GetPerformanceFrequency();
			const LoopMarks* p = &loopCur;
			int queueMs = audio ? (int) (SDL_GetQueuedAudioSize(audio) / 4 * 1000 / want.freq) : 0;
			double gap = p->valid ? (double) (t - p->top) * msPer : 0;
			if (gap > STALL_MS && gap < 5000) {
				++stallCount;
				if (gap > stallWorst) stallWorst = gap;
				if (stallLogged < STALL_LINES) {
					++stallLogged;
					char keptNote[96] = "";
					if (p->kept) snprintf(keptNote, sizeof(keptNote), "; the game and its sound went on meanwhile (%d frame(s))", p->kept);
					fprintf(stderr,
					        "frame %llu: stall: %.0f ms between frames: waiting for the frame buffer %.1f, input and menu %.1f, emulating %.1f "
					        "(sound hand-off %.1f), second screen %.1f, drawing %.1f (top screen %.1f, bottom %.1f), after drawing %.1f, "
					        "sleeping %.1f (of %.1f asked); ran %.1f ms of it, preempted %ld time(s); sound queue %d -> %d ms; "
					        "ended at %.3f s on the system clock (as the kernel's and the journal's)%s\n",
					        p->frame, gap, (double) (p->synced - p->top) * msPer, (double) (p->emu0 - p->synced) * msPer,
					        (double) (p->emu1 - p->emu0) * msPer, p->audio, (double) (p->draw0 - p->emu1) * msPer,
					        (double) (p->draw1 - p->draw0) * msPer, p->presentTop, p->presentBottom, (double) (p->sleep0 - p->draw1) * msPer,
					        (double) (p->end - p->sleep0) * msPer, p->sleepWant * 1000.0, thread_cpu_ms() - p->cpuMs,
					        thread_preemptions() - p->preempt, p->queueMs, queueMs, mono_seconds(), p->kept ? keptNote : "");
				}
			}
			loopCur = (LoopMarks) { .top = t, .cpuMs = thread_cpu_ms(), .preempt = thread_preemptions(), .queueMs = queueMs, .frame = (unsigned long long) frameCount };
		}
		ahead_sync(); /* the core is the loop's again */
		loopCur.synced = SDL_GetPerformanceCounter();
		screen_restore_apply();
		Uint64 workT0 = SDL_GetPerformanceCounter(); /* this frame's work, without the wait at the end */
		long preempt0 = thread_preemptions();
		double cpuMs0 = thread_cpu_ms();
		if (((!logoTex[0] && !logoTex[1]) || logoProvisional) && logo_generation() != logoGen) {
			/* a logo finished extracting: the game's, or the one a scraped
			 * logo takes its size from (GEN-11) */
			logoGen = logo_generation();
			logo_refresh();
		}
		{
			/* back from sleep (frames stopped for over 2 seconds) or the
			 * renderer reset: the textures again */
			Uint32 nowT = SDL_GetTicks();
			bool reset = ui_render_reset();
			if (reset || (lastLoop && nowT - lastLoop > 2000)) {
				fprintf(stderr, "frame %llu: %s, pictures rebuilt\n", (unsigned long long) frameCount,
				        reset ? "renderer reset" : "back after a pause");
				rebuild_textures();
			}
			lastLoop = SDL_GetTicks();
		}
		ui_stick_dpad(cfg->leftStickDpad || menuOpen || barShown > 0); /* (#57: off for the game only) */
		if (!ui_events()) {
			exitTo = GAME_QUIT_APP;
			break;
		}
		ui_tick();
		ui_read_buttons();
		ui_set_pinch(alt && altPage == 1 && altShown && !menuOpen && nScreens == 2);
		{
			const char* ld = ui_script_load();
			if (ld && !state_load(ld)) fprintf(stderr, "could not load %s\n", ld);
		}

		/* ---- pointer: any tap opens the menu; taps in the menu press buttons */
		Pointer p;
		while (ui_next_pointer(&p)) {
			if (p.kind == 1) hint_dismiss(); /* (any touch puts the hint away) */
			if (p.kind >= 4) {
				if (!menuOpen) map_touch(&p);
				continue;
			}
			int mt = menuOpen ? 0 : map_touch(&p);
			if (mt == 2) forceMap = true;
			if (mt) continue;
			if (!menuOpen) {
				/* the quick bar: up from the bottom edge of the touchscreen
				 * brings it up, down puts it away; a tap still opens the
				 * menu (from the bottom edge or with the bar up: once the
				 * finger lifts without having swiped) */
				int ts = ui_touch_screen();
				float u = ui_px(ts);
				if (p.kind == 1) {
					barGesture = G_NONE;
					barDownOpen = barOpen;
					if (p.screen == ts && barOpen) {
						SDL_Rect pin = bar_pin(ts);
						SDL_Point pt = { p.x, p.y };
						barSince = SDL_GetTicks();
						barBtn = bar_button_at(ts, p.x, p.y);
						barGesture = barBtn >= 0 ? G_BTN : SDL_PointInRect(&pt, &pin) ? G_PIN : G_TRACK;
						barGX = p.x, barGY = p.y;
						continue;
					}
					if (p.screen == ts && p.y >= screens[ts].rect.h - u * 36) {
						barGesture = G_TRACK;
						barGX = p.x, barGY = p.y;
						continue;
					}
					open_menu();
					ignorePointerUp = true;
				} else if (p.kind == 3 && (barGesture == G_TRACK || barGesture == G_BTN || barGesture == G_PIN) && p.screen == ts) {
					int dy = p.y - barGY;
					if (!barOpen && dy <= -u * 10) {
						barOpen = true;
						barSince = SDL_GetTicks();
						barGesture = G_DONE;
						if (debug) fprintf(stderr, "frame %llu: quick bar up\n", (unsigned long long) frameCount);
					} else if (barOpen && dy >= u * 10) {
						bar_close();
						barGesture = G_DONE;
					}
				} else if (p.kind == 2) {
					int g = barGesture;
					barGesture = G_NONE;
					if (g == G_BTN && p.screen == ts && bar_button_at(ts, p.x, p.y) == barBtn) {
						int bb = barBtn;
						barSince = SDL_GetTicks();
						if (bb != BB_EXIT) barExitArmed = 0;
						if (bb == BB_SAVE) {
							bool ok = state_save(gp.quick);
							ui_osd(ok ? "State saved." : "Save failed.");
							if (ok) barLit = bb, barLitAt = barSince;
						} else if (bb == BB_LOAD) {
							if (!file_exists(gp.quick)) ui_osd("No quick state saved yet.");
							else if (state_load(gp.quick)) { if (!stateWarned) ui_osd("State loaded."); forceMap = true, barLit = bb, barLitAt = barSince; }
							else ui_osd("Load failed.");
						} else if (bb == BB_SETTINGS) {
							open_menu();
							ignorePointerUp = false;
						} else if (bb == BB_BUG) {
							char what[200];
							/* the moment now, then the game paused for a note */
							if (bug_report(what, sizeof(what))) barLit = bb, barLitAt = barSince, open_menu(), note_open(true);
							else ui_osd("%s", what);
						} else if (bb == BB_EXIT) {
							if (!barExitArmed) {
								barExitArmed = barSince;
								ui_osd("Tap again to close SNES DS.");
							} else {
								exitTo = GAME_QUIT_APP;
								done = true;
							}
						}
					} else if (g == G_PIN && p.screen == ts) {
						SDL_Rect pin = bar_pin(ts);
						SDL_Point pt = { p.x, p.y };
						if (SDL_PointInRect(&pt, &pin)) {
							cfg->barPinned = !cfg->barPinned;
							barSince = SDL_GetTicks();
							save_prefs();
							ui_osd(cfg->barPinned ? "Pinned: the bar stays up." : "Unpinned: the bar hides after 5 seconds.");
						}
					} else if (g == G_TRACK) {
						/* a tap, not a swipe: with the bar up, it goes away (pinned,
						 * it stays, and a tap outside it opens the menu);
						 * otherwise the menu */
						SDL_Rect b = bar_rect(ts);
						SDL_Point pt = { barGX, barGY };
						if (barDownOpen && !cfg->barPinned) {
							bar_close();
						} else if (!barDownOpen || !SDL_PointInRect(&pt, &b)) {
							open_menu();
							ignorePointerUp = false;
						}
					}
				}
				continue;
			}
			if (p.screen != ui_touch_screen()) continue;
			if (achOpen) {
				if (p.kind == 1) {
					if (tab_at(p.screen, p.x, p.y, 2) == 0) {
						close_ach(); /* Back */
						ignorePointerUp = true;
						continue;
					}
					achPressed = true;
					achPressY = p.y;
					achPressScroll = ra_list_scroll_pos();
				} else if (p.kind == 3 && achPressed) {
					ra_list_set_scroll(p.screen, achPressScroll - (p.y - achPressY));
				} else if (p.kind == 2) {
					achPressed = false;
					ignorePointerUp = false;
				}
				continue;
			}
			if (noteOpen) {
				int r, c;
				if (p.kind == 1) {
					ignorePointerUp = false;
					notePressKey = -1;
					if (tab_at(p.screen, p.x, p.y, 2) == 0) {
						note_done(false); /* Skip */
						ignorePointerUp = true;
						continue;
					}
					if (note_key_at(p.screen, p.x, p.y, &r, &c)) notePressKey = r * 16 + c, noteRow = r, noteCol = c;
				} else if (p.kind == 2) {
					if (!ignorePointerUp && note_key_at(p.screen, p.x, p.y, &r, &c) && r * 16 + c == notePressKey) note_press(r, c);
					notePressKey = -1;
					ignorePointerUp = false;
				}
				continue;
			}
			if (patchesOpen) {
				float px = ui_px(p.screen);
				if (p.kind == 1) {
					if (tab_at(p.screen, p.x, p.y, 2) == 0) {
						patchesOpen = false; /* Back */
						ignorePointerUp = true;
						continue;
					}
					patchPressRow = patch_row_at(p.screen, p.x, p.y);
					patchPressY = p.y;
					patchPressScroll = patchScroll;
					patchDragging = false;
					ignorePointerUp = false;
				} else if (p.kind == 3 && patchPressRow != -2) {
					if (abs(p.y - patchPressY) > px * 4) patchDragging = true;
					if (patchDragging) {
						CheatGeom g = cheat_geom(p.screen);
						patchScroll = SDL_clamp(patchPressScroll - (p.y - patchPressY), 0, patch_max_scroll(&g));
					}
				} else if (p.kind == 2) {
					int row = patch_row_at(p.screen, p.x, p.y);
					if (!ignorePointerUp && !patchDragging && row >= 0 && row == patchPressRow && patchMoving) {
						/* moving one: where it goes (another that's on), else put it down */
						int to = patchRows[row].kind == PR_PATCH ? patchRows[row].order : -1;
						int from = patchRows[patchSel].order;
						if (to >= 0 && to != from) {
							for (int k = 0; k < abs(to - from); ++k) patch_shift(to > from ? 1 : -1);
						} else {
							patch_pick_up();
						}
					} else if (!ignorePointerUp && !patchDragging && row >= 0 && row == patchPressRow &&
					           !patch_row_text_only(patchRows[row].kind)) {
						if (row != patchSel) patchArmed = -1, patchNote[0] = 0;
						patchSel = row;
						if (patch_on_number(p.screen, row, p.x)) {
							patch_pick_up(); /* (its number: picked up to move) */
						} else if (patch_choose(row, false)) {
							exitTo = GAME_RELOAD;
							done = true;
						}
					}
					patchPressRow = -2;
					patchDragging = false;
					ignorePointerUp = false;
				}
				continue;
			}
			if (hexOpen) {
				int r, c;
				if (p.kind == 1) {
					ignorePointerUp = false;
					hexPress = -1;
					if (tab_at(p.screen, p.x, p.y, 2) == 0) {
						hexOpen = false; /* Back */
						ignorePointerUp = true;
						continue;
					}
					if (hex_key_at(p.screen, p.x, p.y, &r, &c)) hexPress = r * 16 + c, hexRow = r, hexCol = c;
				} else if (p.kind == 2) {
					if (!ignorePointerUp && hex_key_at(p.screen, p.x, p.y, &r, &c) && r * 16 + c == hexPress) hex_press(r, c);
					hexPress = -1;
					ignorePointerUp = false;
				}
				continue;
			}
			if (pickWhich >= 0) {
				PickGeom pg = pick_geom(p.screen);
				if (p.kind == 1) {
					ignorePointerUp = false;
					if (in_rect(pg.sq, p.x, p.y)) pickDrag = 1;
					else if (in_rect((SDL_Rect) { pg.hue.x - pg.hue.w / 2, pg.hue.y, pg.hue.w * 2, pg.hue.h }, p.x, p.y)) pickDrag = 2;
					else {
						for (int i = 0; i < 8; ++i)
							if (in_rect(pg.presets[i], p.x, p.y)) rgb_hsv(pickPresets[pickWhich][i], &pickH, &pickS, &pickV), pickPreset = i;
						if (in_rect(pg.hex, p.x, p.y)) hex_open();
						else if (in_rect(pg.cancel, p.x, p.y)) pick_close(false);
						else if (in_rect(pg.ok, p.x, p.y)) pick_close(true);
						ignorePointerUp = true;
					}
					pick_touch(p.screen, p.x, p.y);
				} else if (p.kind == 3) {
					pick_touch(p.screen, p.x, p.y);
				} else if (p.kind == 2) {
					pick_touch(p.screen, p.x, p.y);
					pickDrag = 0;
					ignorePointerUp = false;
				}
				continue;
			}
			if (ovlOpen) {
				if (p.kind == 1) {
					ignorePointerUp = false;
					ovlPress = -1;
					if (tab_at(p.screen, p.x, p.y, 2) == 0) {
						ovlOpen = false; /* Back */
						ignorePointerUp = true;
						continue;
					}
					int rows[8], n = ovl_rows(rows);
					for (int i = 0; i < n; ++i)
						if (in_rect(ovl_row_rect(p.screen, i), p.x, p.y)) ovlPress = i, ovlSel = i;
				} else if (p.kind == 2) {
					int rows[8], n = ovl_rows(rows);
					for (int i = 0; i < n; ++i)
						if (!ignorePointerUp && i == ovlPress && in_rect(ovl_row_rect(p.screen, i), p.x, p.y)) ovl_activate(i, 1);
					ovlPress = -1;
					ignorePointerUp = false;
				}
				continue;
			}
			if (cheatsOpen) {
				float px = ui_px(p.screen);
				if (p.kind == 1) {
					if (tab_at(p.screen, p.x, p.y, 2) == 0) {
						cheatsOpen = false; /* Back */
						ignorePointerUp = true;
						continue;
					}
					cheatPressRow = cheat_row_at(p.screen, p.x, p.y);
					cheatPressY = p.y;
					cheatPressScroll = cheatScroll;
					cheatDragging = false;
					ignorePointerUp = false;
				} else if (p.kind == 3 && cheatPressRow != -2) {
					if (abs(p.y - cheatPressY) > px * 4) cheatDragging = true;
					if (cheatDragging) {
						CheatGeom g = cheat_geom(p.screen);
						cheatScroll = SDL_clamp(cheatPressScroll - (p.y - cheatPressY), 0, cheat_max_scroll(&g));
					}
				} else if (p.kind == 2) {
					int row = cheat_row_at(p.screen, p.x, p.y);
					if (!ignorePointerUp && !cheatDragging && row >= 0 && row == cheatPressRow && cheatRows[row] >= 0) {
						cheatSel = row;
						toggle_cheat(row);
					}
					cheatPressRow = -2;
					cheatDragging = false;
					ignorePointerUp = false;
				}
				continue;
			}
			if (p.kind == 1) {
				int t = tab_at(p.screen, p.x, p.y, N_PAGES);
				if (t >= 0) {
					if (t != curPage) set_page(t);
					ignorePointerUp = true;
					continue;
				}
			}
			if (page_is_rows(curPage)) {
				rows_touch(&p);
				continue;
			}
			int it = item_at(p.screen, p.x, p.y);
			if (p.kind == 1) {
				ignorePointerUp = false;
				menuPressed = it, rowDragY = p.y;
				if (it >= 0) menuSel = it;
			} else if (p.kind == 3 && nMenuRows > ROW_VIS && menuPressed >= 0 && abs(p.y - rowDragY) >= menu_geom(p.screen).h + menu_geom(p.screen).gap) {
				/* a finger dragged up or down moves the list a row at a time: no longer a tap */
				itemTop = SDL_clamp(itemTop + (p.y < rowDragY ? 1 : -1), 0, nMenuRows - ROW_VIS);
				rowDragY = p.y, menuPressed = -1, ignorePointerUp = true;
			} else if (p.kind == 3 && menuPressed >= 0 && it >= 0) {
				menuSel = it;
			} else if (p.kind == 2) {
				if (!ignorePointerUp && it >= 0 && it == menuPressed && menuItems[it] == M_SLOT) {
					/* (the slot row: its left third goes back a slot, the rest forward) */
					SDL_Rect r = item_rect(p.screen, it);
					slot_step(p.x < r.x + r.w / 3 ? -1 : 1);
				} else if (!ignorePointerUp && it >= 0 && it == menuPressed && activate(menuItems[it], &exitTo)) done = true;
				menuPressed = -1;
				ignorePointerUp = false;
			}
		}
		if (done) break;

		/* ---- the menu combo (menu_combo = select+start; on Android, where HOME
		 * belongs to the system): pressing both opens the menu. The first of
		 * the two is held back from the game for a few frames, so that a combo
		 * doesn't also press SELECT (a map in some games) or START (pause);
		 * if the other doesn't follow, it goes to the game after all. */
		bool comboOpened = false;
		if (!menuOpen && !strcmp(cfg->menuCombo, "select+start")) {
			if (comboHold < 0) {
				if (btn_pressed(B_SELECT) && btn_pressed(B_START)) comboHold = B_START, comboFrames = 0;
				else if (btn_pressed(B_SELECT) && !btnNow[B_START]) comboHold = B_SELECT, comboFrames = 0;
				else if (btn_pressed(B_START) && !btnNow[B_SELECT]) comboHold = B_START, comboFrames = 0;
			}
			if (comboHold >= 0) {
				int other = comboHold == B_SELECT ? B_START : B_SELECT;
				if (btnNow[comboHold] && btnNow[other]) {
					open_menu();
					comboOpened = true;
					comboUsed = true; /* not a hotkey, nor a plain SELECT for the game */
					comboHold = -1;
				} else if (!btnNow[comboHold]) {
					comboPulse[comboHold == B_START] = 4; /* a quick tap: give it to the game now */
					comboHold = -1;
				} else if (++comboFrames > 5) {
					comboHold = -1; /* just held: the game gets it from here */
				}
			}
		} else {
			comboHold = -1;
		}

		/* ---- HOME (or the hotkey modifier) alone toggles the menu; with a button it's a hotkey */
		bool mod = btnNow[modBtn];
		/* Hotkey + R2 (R2 no action's button): fast-forward, as R2 alone,
		 * with the game's buttons going through, so text can still be
		 * skipped (RetroArch's habit; issue #44). No hotkeys meanwhile,
		 * and letting go of the hotkey doesn't open the menu. */
		bool ffMod = false;
		if (mod && !menuOpen && cfg->fastForward && btnNow[B_R2] && !(cfg->rewind && btnNow[B_L2])) {
			ffMod = true;
			for (int i = 0; i < HK_COUNT; ++i) ffMod = ffMod && hkBtn[i] != B_R2;
			if (ffMod) comboUsed = ffModUsed = true;
		}
		if (mod && !menuOpen && !ffMod) {
			for (int i = 0; i < HK_COUNT; ++i) {
				if (hkBtn[i] < 0 || !btn_pressed(hkBtn[i])) continue;
				comboUsed = true;
				if (debug) fprintf(stderr, "hotkey: %s\n", hotkeyNames[i]);
				switch (i) {
				case HK_QUIT: exitTo = GAME_QUIT_APP, done = true; break;
				/* (the quick state, as the quick bar's buttons) */
				case HK_SAVE: ui_osd(state_save(gp.quick) ? "State saved." : "Save failed."); break;
				case HK_LOAD:
					if (!file_exists(gp.quick)) ui_osd("No quick state saved yet.");
					else if (state_load(gp.quick)) { if (!stateWarned) ui_osd("State loaded."); forceMap = true; }
					else ui_osd("Load failed.");
					break;
				case HK_BAR: bar_toggle(); break;
				case HK_SWAP: swap_screens(); break;
				case HK_SHADER:
					set_shader(cfg->shader != SH_OFF ? SH_OFF : cfg->shaderLast > SH_OFF && cfg->shaderLast < SH_COUNT ? cfg->shaderLast : SH_GRID);
					ui_osd("%s", shader_label(cfg->shader));
					break;
				}
			}
		}
		if (btn_released(B_HOME) && !(modBtn == B_HOME && comboUsed)) {
			if (menuOpen) menuOpen = false;
			else open_menu();
		}
		if (btn_released(modBtn)) {
			if (comboUsed && !ffModUsed) suppress_held(); /* (not after hotkey + R2: the game had them) */
			else if (!menuOpen && modIsGame) modPulse = 4; /* tapped alone: the game gets it */
			else if (!menuOpen && modBtn == B_L3) bar_toggle();
			comboUsed = ffModUsed = false;
		}
		if (done) break;

		if (menuOpen && achOpen) {
			int scr = ui_touch_screen();
			static int held[2];
			for (int k = 0; k < 2; ++k) {
				int b = k ? B_DOWN : B_UP;
				held[k] = btnNow[b] ? held[k] + 1 : 0;
				if (held[k] == 1 || (held[k] > 20 && held[k] % 4 == 0)) ra_list_scroll(scr, (k ? 1 : -1) * ra_list_step(scr));
			}
			int page = ach_area(scr).h * 3 / 4;
			if (btn_pressed(B_L)) ra_list_scroll(scr, -page);
			if (btn_pressed(B_R)) ra_list_scroll(scr, page);
			if (btn_pressed(B_B)) close_ach();
			if (btn_pressed(B_START)) close_ach(), menuOpen = false;
		} else if (menuOpen && noteOpen) {
			/* D-pad (repeating while held) and A on the keys; B deletes (or,
			 * with nothing typed, skips), X a space, SELECT Shift, START done */
			static int held[4];
			const int dirs[4] = { B_UP, B_DOWN, B_LEFT, B_RIGHT };
			for (int k = 0; k < 4; ++k) {
				held[k] = btnNow[dirs[k]] ? held[k] + 1 : 0;
				if (held[k] == 1 || (held[k] > 20 && held[k] % 4 == 0))
					note_move(ui_touch_screen(), k == 0 ? -1 : k == 1 ? 1 : 0, k == 2 ? -1 : k == 3 ? 1 : 0);
			}
			if (btn_pressed(B_A)) note_press(noteRow, noteCol);
			else if (btn_pressed(B_B)) {
				if (noteLen) noteText[--noteLen] = 0;
				else note_done(false);
			} else if (btn_pressed(B_X)) note_type(" ");
			else if (btn_pressed(B_SELECT)) noteShift = !noteShift;
			else if (btn_pressed(B_START)) note_done(true);
		} else if (menuOpen && patchesOpen) {
			static int pheld[2];
			for (int k = 0; k < 2; ++k) {
				int b = k ? B_DOWN : B_UP;
				pheld[k] = btnNow[b] ? pheld[k] + 1 : 0;
				if (pheld[k] == 1 || (pheld[k] > 20 && pheld[k] % 4 == 0)) patch_move(k ? 1 : -1);
			}
			CheatGeom g = cheat_geom(ui_touch_screen());
			int page = SDL_max(1, g.listH / (g.rowH + g.gap) - 1);
			if (!patchMoving && btn_pressed(B_L)) for (int k = 0; k < page; ++k) patch_move(-1);
			if (!patchMoving && btn_pressed(B_R)) for (int k = 0; k < page; ++k) patch_move(1);
			bool later = patchArmed >= 0 && btn_pressed(B_Y);
			if (btn_pressed(B_X)) patch_pick_up(); /* (pick a patch that's on up to move it, or put it down) */
			else if ((btn_pressed(B_A) || later) && patch_choose(patchSel, later)) {
				exitTo = GAME_RELOAD;
				done = true;
			}
			if (btn_pressed(B_B)) {
				if (patchMoving) patch_pick_up();
				else if (patchArmed >= 0) patchArmed = -1, patchNote[0] = 0;
				else patchesOpen = false;
			}
			if (btn_pressed(B_START)) patchMoving = false, patchesOpen = false, menuOpen = false;
		} else if (menuOpen && hexOpen) {
			/* D-pad (repeating) and A on the keys; B deletes (or, with nothing typed, goes back), START Done */
			static int held[4];
			const int dirs[4] = { B_UP, B_DOWN, B_LEFT, B_RIGHT };
			for (int k = 0; k < 4; ++k) {
				held[k] = btnNow[dirs[k]] ? held[k] + 1 : 0;
				if (held[k] == 1 || (held[k] > 20 && held[k] % 4 == 0)) hex_move(k == 0 ? -1 : k == 1 ? 1 : 0, k == 2 ? -1 : k == 3 ? 1 : 0);
			}
			if (btn_pressed(B_A)) hex_press(hexRow, hexCol);
			else if (btn_pressed(B_B)) {
				if (hexLen) hexText[--hexLen] = 0;
				else hexOpen = false;
			} else if (btn_pressed(B_START)) hex_done();
		} else if (menuOpen && pickWhich >= 0) {
			/* D-pad: saturation (across) and brightness (up / down); L1 / R1 the
			 * hue; held, a step at a time, then faster. X the next preset, Y
			 * the hex keypad, A OK, B Cancel, START OK and back to the game. */
			static int held[6];
			const int keys[6] = { B_LEFT, B_RIGHT, B_UP, B_DOWN, B_L, B_R };
			for (int k = 0; k < 6; ++k) {
				held[k] = btnNow[keys[k]] ? held[k] + 1 : 0;
				if (held[k] != 1 && held[k] <= 15) continue;
				float step = held[k] == 1 ? 0.01f : held[k] > 60 ? 0.03f : 0.01f;
				if (k == 0) pickS = SDL_max(0.0f, pickS - step);
				if (k == 1) pickS = SDL_min(1.0f, pickS + step);
				if (k == 2) pickV = SDL_min(1.0f, pickV + step);
				if (k == 3) pickV = SDL_max(0.0f, pickV - step);
				if (k >= 4) pickH = fmodf(pickH + (k == 4 ? -1 : 1) * step * 200 + 360, 360);
			}
			if (btn_pressed(B_X)) pickPreset = (pickPreset + 1) % 8, rgb_hsv(pickPresets[pickWhich][pickPreset], &pickH, &pickS, &pickV);
			else if (btn_pressed(B_Y)) hex_open();
			else if (btn_pressed(B_A)) pick_close(true);
			else if (btn_pressed(B_B)) pick_close(false);
			else if (btn_pressed(B_START)) pick_close(true), ovl_close_all(), menuOpen = false;
		} else if (menuOpen && ovlOpen) {
			int rows[8], n = ovl_rows(rows);
			if (btn_pressed(B_DOWN)) ovlSel = SDL_min(n - 1, ovlSel + 1);
			if (btn_pressed(B_UP)) ovlSel = SDL_max(0, ovlSel - 1);
			bool cycles = rows[ovlSel] == OR_MODE || rows[ovlSel] == OR_SCREENS || rows[ovlSel] == OR_LOGO;
			if (cycles && btn_pressed(B_LEFT)) ovl_activate(ovlSel, -1);
			else if (cycles && btn_pressed(B_RIGHT)) ovl_activate(ovlSel, 1);
			else if (btn_pressed(B_A)) ovl_activate(ovlSel, 1);
			else if (btn_pressed(B_B)) ovlOpen = false;
			else if (btn_pressed(B_START)) ovl_close_all(), menuOpen = false;
		} else if (menuOpen && cheatsOpen) {
			/* D-pad repeats while held */
			static int held[2];
			for (int k = 0; k < 2; ++k) {
				int b = k ? B_DOWN : B_UP;
				held[k] = btnNow[b] ? held[k] + 1 : 0;
				if (held[k] == 1 || (held[k] > 20 && held[k] % 4 == 0)) cheat_move(k ? 1 : -1);
			}
			CheatGeom g = cheat_geom(ui_touch_screen());
			int page = SDL_max(1, g.listH / (g.rowH + g.gap) - 1);
			if (btn_pressed(B_L)) for (int k = 0; k < page; ++k) cheat_move(-1);
			if (btn_pressed(B_R)) for (int k = 0; k < page; ++k) cheat_move(1);
			if (btn_pressed(B_A)) toggle_cheat(cheatSel);
			if (btn_pressed(B_B)) cheatsOpen = false;
			if (btn_pressed(B_START)) cheatsOpen = false, menuOpen = false;
		} else if (menuOpen) {
			if (!comboOpened) { /* (START just opened it: not also a close) */
				if (btn_pressed(B_L) && curPage > 0) set_page(curPage - 1);
				if (btn_pressed(B_R) && curPage < N_PAGES - 1) set_page(curPage + 1);
				if (page_is_rows(curPage)) {
					rows_input();
					if (btn_pressed(B_B) || btn_pressed(B_START)) menuOpen = false;
				} else {
				if (btn_pressed(B_DOWN)) menu_move(1);
				if (btn_pressed(B_UP)) menu_move(-1);
				if (menuItems[menuSel] == M_SLOT && (btn_pressed(B_LEFT) || btn_pressed(B_RIGHT))) slot_step(btn_pressed(B_LEFT) ? -1 : 1);
				if ((btn_pressed(B_LEFT) || btn_pressed(B_RIGHT)) && itemSide[menuSel] >= 0) {
					/* the other half of the row */
					int to = btn_pressed(B_LEFT) ? 0 : 1;
					for (int i = 0; i < nMenuItems; ++i)
						if (itemRow[i] == itemRow[menuSel] && itemSide[i] == to) menuSel = i;
				}
				if (btn_pressed(B_B) || btn_pressed(B_START)) menuOpen = false;
				else if (btn_pressed(B_A) && activate(menuItems[menuSel], &exitTo)) break;
				}
			}
			if (menuOpen && !wasMenu && audio) {
				SDL_PauseAudioDevice(audio, 1);
				SDL_ClearQueuedAudio(audio);
			}
		}
		if (wasMenu && !menuOpen) {
			if (noteOpen) note_done(true); /* (closed some other way: saved with what's typed) */
			suppress_held();
			if (cfg->barPinned) barOpen = true, barSince = SDL_GetTicks(); /* (pinned: back after the menu) */
			if (audio) SDL_PauseAudioDevice(audio, 0);
			next = SDL_GetPerformanceCounter() / freq;
			fpsT0 = next;
			fpsFrames = 0;
			pacedMisses = pacedMissSecs = 0; /* (the menu's time isn't the game's) */
			forceMap = true;
			audioQuietUntil = frameCount + 150; /* (the queue was cleared for the menu: refilling) */
		}
		wasMenu = menuOpen;

		if (menuOpen) {
			ra_idle();
			ui_clear();
			draw_game();
			draw_menu();
			ui_present();
			SDL_Delay(16);
			next = SDL_GetPerformanceCounter() / freq;
			continue;
		}

		if (btn_pressed(B_L3) && modBtn != B_L3) bar_toggle(); /* (L3: the quick menu, as a swipe up) */

		/* ---- emulate */
		uint32_t keys = 0;
		for (int b = 0; b < B_COUNT; ++b) {
			if (!btnNow[b]) suppress[b] = false; /* released: counts again */
		}
		if (modIsGame && btnNow[modBtn]) {
			/* the hotkey (SELECT or START) held: the actions' buttons are
			 * kept from the game until released, the rest go through */
			for (int i = 0; i < HK_COUNT; ++i) {
				if (hkBtn[i] >= 0 && btnNow[hkBtn[i]]) suppress[hkBtn[i]] = true;
			}
		}
		if (!btnNow[modBtn] || modIsGame || ffMod) {
			keys |= fb_keys(true); /* the player's choices (GEN-10), else ab_swap's */
			if (HELD(B_START) && modBtn != B_START && comboHold != B_START) keys |= K_START;
			if (HELD(B_SELECT) && modBtn != B_SELECT && comboHold != B_SELECT) keys |= K_SELECT;
			keys |= fb_keys(false); /* L, R and the D-pad, as chosen (GEN-10) */
		}
		if (modPulse > 0) keys |= modBtn == B_START ? K_START : K_SELECT, --modPulse;
		if (comboPulse[0] > 0) keys |= K_SELECT, --comboPulse[0];
		if (comboPulse[1] > 0) keys |= K_START, --comboPulse[1];
		keys = alt_keys(keys); /* ALTTP-1: X is the second screen's */
		{
			static uint32_t lastKeys;
			if (debug && keys != lastKeys) fprintf(stderr, "frame %llu: game keys %03x\n", (unsigned long long) frameCount, keys);
			lastKeys = keys;
		}

		bool rewinding = cfg->rewind && btnNow[B_L2];
		bool ff = cfg->fastForward && btnNow[B_R2] && !rewinding;
		ffBox = ff;
		/* Max (ff_speed 0): as many frames as fit in about 70% of a display
		 * frame (the rest for drawing it), up to FF_MAX_RUNS; the sound
		 * thinned out by the last count */
		const bool ffMax = ff && cfg->ffSpeed <= 0;
		static int maxRan = 4;
		int runs = ffMax ? FF_MAX_RUNS : ff ? cfg->ffSpeed : 1;
		int thin = ffMax ? maxRan : runs; /* every thin-th sound sample kept */
		/* Audio keeps flowing during fast-forward: every runs-th sample is kept,
		 * so the device gets real-time worth of (sped-up) sound. Starving it
		 * makes PulseAudio/PipeWire grow their buffers after each underrun,
		 * which is what made the lag worse every time. */
		int queuedNow = audio ? (int) (SDL_GetQueuedAudioSize(audio) / 4) : 0;
		bool dropAudio = queuedNow > targetQueued * 3; /* hard cap on drift */
		/* Health report (always on, every 5 s when something went wrong): audio
		 * underruns (the queue ran dry = a crackle), slow loop iterations, and
		 * how often the background map capture was running. */
		{
			static int underruns, slow, captureBusy, dropped, n;
			static int queueLo = 1 << 30, queueHi = -1, quietWindows; /* the queue's range this window (ms) */
			static long preemptT0 = -1;
			if (preemptT0 < 0) preemptT0 = thread_preemptions();
			static Uint64 lastIter;
			Uint64 nowT = SDL_GetPerformanceCounter();
			if (lastIter && !ff && (nowT - lastIter) * 1000 / SDL_GetPerformanceFrequency() > 25) ++slow;
			lastIter = nowT;
			if (audio && frameCount > 120 && queuedNow == 0 && !ff) ++underruns;
			/* The sound keeps running dry (a queue that had sound in it a frame ago and has
			 * none now, three times in about 10 s): the sound delay goes up a step. Not while
			 * fast-forwarding or rewinding, nor for 2.5 s after either or after the menu
			 * (the queue is being filled again then). */
			if (ff || rewinding || dropAudio) audioQuietUntil = frameCount + 150;
			if (audio && cfg->audioAuto && frameCount > 120 && frameCount >= audioQuietUntil) {
				if (queuedNow == 0 && dryQueueBefore > 0) {
					if (!dryEvents || frameCount - dryWindowStart > 600) dryWindowStart = frameCount, dryEvents = 0;
					if (++dryEvents >= 3) {
						static const int steps[] = { 32, 64, 96, 128, 192 };
						int to = audioMs;
						for (size_t k = 0; k < sizeof(steps) / sizeof(steps[0]); ++k)
							if (steps[k] > audioMs) { to = steps[k]; break; }
						if (to != audioMs) {
							fprintf(stderr, "frame %llu: the sound ran dry %d times in %.0f s: sound delay raised from %d to %d ms "
							                "for this run (audio_auto = 0 turns this off)\n",
							        (unsigned long long) frameCount, dryEvents, (double) (frameCount - dryWindowStart) / 60.0, audioMs, to);
							audioMs = to;
							targetQueued = want.freq * audioMs / 1000;
							ui_osd_long("Sound delay raised to %d ms: the sound kept breaking up", audioMs);
						}
						dryEvents = 0;
						audioQuietUntil = frameCount + 150; /* (the queue climbs to the new level first) */
					}
				}
				dryQueueBefore = queuedNow;
			} else {
				dryQueueBefore = 1;
			}
			if (audio && frameCount > 120 && !ff && !rewinding) {
				int qMs = queuedNow * 1000 / want.freq;
				if (qMs < queueLo) queueLo = qMs;
				if (qMs > queueHi) queueHi = qMs;
			}
			if (mc && mapcap_busy(mc)) ++captureBusy;
			if (dropAudio && !ff) ++dropped; /* (the queue over 3x its target: this frame's sound thrown away, a gap) */
			if (++n >= 300) {
#ifdef __ANDROID__
				const bool report = true; /* performance on Android devices is still being measured */
#else
				const bool report = underruns || slow || dropped || stallCount || keptTotal || debug;
#endif
				/* (all well: the line still comes every 30 s, for the queue's range) */
				bool show = report;
				if (!show && audio && ++quietWindows >= 6) show = true;
				if (show) quietWindows = 0;
				if (show) {
					double ms = 1000.0 / SDL_GetPerformanceFrequency() / n;
					fprintf(stderr, "frame %llu: last 5 s: %.1f fps, %d audio underrun(s), %d slow frame(s), map capture busy %d%%, "
					                "audio queue %d ms; per frame: emulating %.2f ms, drawing %.2f ms; game thread preempted %ld time(s)",
					        (unsigned long long) frameCount, fps, underruns, slow, captureBusy * 100 / n,
					        queuedNow * 1000 / want.freq, emuTicks * ms, drawTicks * ms, thread_preemptions() - preemptT0);
					if (queueHi >= 0) fprintf(stderr, "; audio queue between %d and %d ms (sound delay %d ms)", queueLo, queueHi, audioMs);
					if (stallCount) fprintf(stderr, "; %d stall(s) over %d ms, the longest %.0f ms", stallCount, STALL_MS, stallWorst);
					if (dropped) fprintf(stderr, "; sound thrown away on %d frame(s) (queue too long)", dropped);
					if (keptTotal) fprintf(stderr, "; %u frame(s) run while the screen held a frame up", keptTotal);
					fputc('\n', stderr);
				}
				underruns = slow = captureBusy = dropped = n = 0;
				queueLo = 1 << 30, queueHi = -1;
				stallCount = stallLogged = 0, stallWorst = 0;
				keptTotal = 0;
				preemptT0 = thread_preemptions();
				emuTicks = drawTicks = 0;
			}
		}
		if (debug && audio && frameCount % 30 == 0) {
			fprintf(stderr, "audio queue %d ms%s\n", queuedNow * 1000 / want.freq, ff ? " (ff)" : "");
		}
		Uint64 emuT0 = SDL_GetPerformanceCounter(), raT = 0, audT = 0;
		double emuCpu0 = thread_cpu_ms();
		if (rewinding) {
			/* Rewinding: each frame goes back one state of the buffer (two
			 * frames) and runs one frame to draw it, so it plays backwards at
			 * double speed; silent, no achievements. At the buffer's start
			 * the picture stays. */
			aheadReady = false; /* (a frame emulated ahead belongs to the present) */
			bug_reset();
			if (emu_rewind_step(&emu)) {
				emu_frame(&emu, 0);
				rewindBox = 1;
			} else {
				rewindBox = 2;
			}
			while (emu_audio_read(&emu, samples, 4096) > 0) {}
			rewindTick = 0;
			wasRewinding = true;
		} else if (wasRewinding) {
			wasRewinding = false;
			rewindBox = 0;
			forceMap = true; /* the map, as of where the rewind stopped */
		}
		/* Snapshots of the game between frames are taken here, with the keys
		 * the next frame runs with: bsnes runs the emulation to a sync point to
		 * take a state, and the game may read the controller on the way
		 * (emu_state_save_live). */
		if (!rewinding && runs > 0) {
			bug_tick(keys);
			if (mapWant && mc && !mapcap_busy(mc) && mapcap_request(mc, &emu, keys)) {
				sinceMapRequest = 0;
				lastRoomKey = mapWantKey;
				forceMap = false;
			}
			mapWant = false;
		}
		int ran = 0;
		for (int i = 0; i < (rewinding ? 0 : runs); ++i) {
			if (ffMax && i > 0 && (SDL_GetPerformanceCounter() - emuT0) / freq > frameSec * 0.7) break;
			++ran;
			if (i == 0 && aheadReady) aheadReady = false; /* emulated by the worker while the last frame was shown */
			else run_frame(keys);
			Uint64 raT0 = SDL_GetPerformanceCounter();
			ra_frame();
			raT += SDL_GetPerformanceCounter() - raT0;
			Uint64 audT0 = SDL_GetPerformanceCounter();
			size_t got;
			while ((got = emu_audio_read(&emu, samples, 4096)) > 0) {
				if (!audio || dropAudio) continue;
				if (testStarve && frameCount > 200 && frameCount % 100 >= 20 && frameCount % 100 < 40) continue;
				if (thin > 1) {
					size_t outN = 0;
					for (size_t f = 0; f < got; ++f) {
						if (ffPhase++ % thin == 0) {
							samples[outN * 2] = samples[f * 2];
							samples[outN * 2 + 1] = samples[f * 2 + 1];
							++outN;
						}
					}
					got = outN;
				}
				if (!got) continue;
				/* matched to the display, the game makes its sound audioSpeed times
				 * faster than real time: resample it from that much higher a rate */
				int rate = (int) lround(emu_audio_rate(&emu) * audioSpeed);
				if (rate != convFrom || !audioConv) {
					if (audioConv) SDL_FreeAudioStream(audioConv);
					audioConv = SDL_NewAudioStream(AUDIO_S16SYS, 2, rate, AUDIO_S16SYS, 2, want.freq);
					if (debug || convFrom) fprintf(stderr, "frame %llu: game audio rate %d Hz\n", (unsigned long long) frameCount, rate);
					convFrom = rate;
				}
				if (!audioConv) continue;
				SDL_AudioStreamPut(audioConv, samples, (int) got * 4);
				static int16_t conv[8192 * 2];
				int bytes;
				while ((bytes = SDL_AudioStreamGet(audioConv, conv, sizeof(conv))) > 0) {
					/* Paced by the display, the loop can't slow down or speed up to
					 * keep the sound queue level, so the sound gives: a sample frame
					 * dropped or repeated per chunk (at most ~0.1%, inaudible) while
					 * the queue is a quarter off its target. */
					if (pacedHz > 0 && bytes >= 8) {
						int q = (int) (SDL_GetQueuedAudioSize(audio) / 4);
						if (q > targetQueued * 5 / 4) bytes -= 4;
						else if (q < targetQueued * 3 / 4 && bytes + 4 <= (int) sizeof(conv)) {
							memcpy((char*) conv + bytes, (char*) conv + bytes - 4, 4);
							bytes += 4;
						}
					}
					int vol = ui_volume();
					if (vol < 100) {
						/* (squared: closer to how loud it sounds) */
						int gain = vol * vol * 65536 / 10000;
						for (int k = 0; k < bytes / 2; ++k) conv[k] = (int16_t) (conv[k] * gain >> 16);
					}
					SDL_QueueAudio(audio, conv, bytes);
				}
			}
			audT += SDL_GetPerformanceCounter() - audT0;
		}
		if (ffMax) maxRan = SDL_max(1, ran);
		runs = rewinding ? runs : SDL_max(1, ran); /* (the frames run, for the rewind buffer and the speed) */
		Uint64 emuT1 = SDL_GetPerformanceCounter();
		double emuCpu1 = thread_cpu_ms();
		emuTicks += emuT1 - emuT0;
		++frameCount;
		/* the rewind buffer: a state every second emulated frame */
		if (cfg->rewind && !rewinding && (rewindTick += runs) >= 2) {
			emu_rewind_append(&emu);
			rewindTick = 0;
		}

		/* ---- map refresh */
		if (mc) {
			++sinceMapRequest;
			{
				/* (logoIf: the logo for now, SM4-11) */
				const MapProfile* lp = mapcap_profile(mc);
				bool lf = lp && ((lp->logoIf[0].addr && memprobes_match(lp->logoIf, 2, &emu)) ||
				                 (lp->logoIfOr[0].addr && memprobes_match(lp->logoIfOr, 2, &emu)));
				if (lf) logoHolding = 60;
				else if (logoHolding > 0) --logoHolding;
				lf = lf || logoHolding > 0;
				if (debug && lf != logoForced) fprintf(stderr, "frame %llu: logo only %s\n", (unsigned long long) frameCount, lf ? "on" : "off");
				logoForced = lf;
			}
			bool dark = frame_is_dark(emu.fb);
			if (wasDark && !dark) refreshAt = (long) frameCount + 6;
			wasDark = dark;
			if (refreshAt >= 0 && (long) frameCount >= refreshAt) {
				forceMap = true;
				refreshAt = -1;
				if (debug) fprintf(stderr, "frame %llu: room transition\n", (unsigned long long) frameCount);
			}
			if (mapShownIdx >= 0 && frameCount % 30 == 0 && mapcap_in_menu(mc, &emu)) {
				/* quit to the title: the map belongs to the old session */
				mapShownIdx = -1;
				mapInactive = false;
				forceMap = true;
				/* (always logged: what sends the second screen back to the logo) */
				fprintf(stderr, "frame %llu: second screen back to the logo (the profile's menu probes matched)\n", (unsigned long long) frameCount);
			}
			uint32_t rk = mapcap_room_key(mc, &emu);
			if (rk != seenRoomKey) seenRoomKey = rk, seenRoomFor = 0;
			else ++seenRoomFor;
			/* a change counts once it has held for 3 frames (a cursor
			 * gliding to its place, Mario walking a path: not each step);
			 * a capture still running for the old one is called off, so
			 * the new one starts as soon as it has stopped */
			bool roomChanged = rk && rk != lastRoomKey && seenRoomFor >= 2;
			if (roomChanged && mapcap_busy(mc)) mapcap_cancel(mc);
			/* Back off while captures keep failing (e.g. the game says "playing"
			 * but the map can't open: intro cutscenes, briefings). Each failed
			 * attempt emulates a few seconds in the background, which starves
			 * the game and its audio on slower devices. */
			const char* why = NULL;
			int fails = mapcap_failures(mc, &why);
			if (fails != lastFails) {
				if (debug && fails) fprintf(stderr, "frame %llu: map capture failed (%d in a row): %s\n",
				                            (unsigned long long) frameCount, fails, why ? why : "?");
				const MapProfile* dp = mapcap_profile(mc);
				if (fails && why && dp && dp->dimWhenUnavailable &&
				    (!strcmp(why, "map screen did not appear") || !strcmp(why, "map button did nothing"))) {
					mapInactive = true;
				}
				lastFails = fails;
			}
			/* quick "no map here" failures (A Link to the Past indoors) cost little
			 * and are expected, so they don't back off */
			bool cheapFail = why && !strcmp(why, "map button did nothing");
			/* (a profile's own interval, when it's quicker: Swordcraft Story) */
			const MapProfile* ip = mapcap_profile(mc);
			int every = ip && ip->interval > 0 && ip->interval < cfg->mapInterval ? ip->interval : cfg->mapInterval;
			int interval = cheapFail ? every : every << SDL_min(fails, 3);
			/* after failures, a room change still triggers a try, but no more
			 * often than the normal interval */
			bool due = sinceMapRequest >= interval ||
			           ((forceMap || roomChanged) && (!fails || sinceMapRequest >= every));
			/* (requested just before the next frame, when its keys are known) */
			if (due && !mapcap_busy(mc) && mapcap_live_ok(mc, &emu)) mapWant = true, mapWantKey = rk;
			hudHide = ip && ip->hide[0].w > 0 && cfg->map && memprobes_match(ip->hideIf, 3, &emu);
			/* SM-7: the player moving puts the map back, centred */
			if (ip && ip->touch.scrollX) {
				uint32_t mk = (uint32_t) emu_read16(&emu, ip->touch.moved[0]) | (uint32_t) emu_read16(&emu, ip->touch.moved[1]) << 16;
				if (mk != movedKey) {
					movedKey = mk;
					if ((viewPage || viewDx || viewDy) && !mapDrag) {
						bool onlyScroll = viewPage == 0 && stitch_on();
						viewPage = viewDx = viewDy = 0;
						if (onlyScroll) {
							map_reshow(); /* (the whole map: back to the game's view, no capture) */
						} else {
							mapcap_set_view(mc, 0, 0, 0);
							mapcap_cancel(mc);
							forceMap = true;
						}
					}
				}
			}
			bool fresh = mapcap_poll(mc, mapRes.serial, &mapRes);
			if (fresh && mapcap_in_menu(mc, &emu)) {
				/* a capture that finished after quitting to the title: drop it */
			} else if (fresh) {
				/* SM-7: the scroll the game allowed (stopped at its map's edges) */
				if (!ip->touch.bgW && mapRes.page == viewPage && mapRes.askedDx == viewDx && mapRes.askedDy == viewDy && (mapRes.dx != viewDx || mapRes.dy != viewDy)) {
					viewDx = mapRes.dx, viewDy = mapRes.dy;
					mapcap_set_view(mc, viewPage, viewDx, viewDy);
				}
				map_show_new(true);
			} else if (map_anim() && mapRes.nFrames > 1 && mapShownIdx >= 0) {
				int idx = map_anim_index();
				upload_map_frame(idx);
			}
		}

		if (nat) native_tick();
		if (alt) alt_tick();
		if (snapProf) update_snapshot();

		Uint64 drawT0 = SDL_GetPerformanceCounter();
		double drawCpu0 = thread_cpu_ms();
		/* frame buffer: start the next frame now; this one is drawn from a copy */
		const uint32_t* shownFb = emu.fb;
		int shownW = emu.w, shownH = emu.h;
		if (cfg->frameBuffer && !ff && !rewinding) {
			memcpy(aheadFb, emu.fb, (size_t) emu.w * emu.h * sizeof(uint32_t));
			if (ahead_start(keys)) aheadInFlight = true, shownFb = aheadFb;
		}
		if (cfg->ghosting) shownFb = ghost_frame(shownFb, shownW, shownH);
		/* only the view the game is on needs it (all of them right after a swap,
		 * since the menu draws the game without uploading) */
		game_tex_fit(shownW, shownH);
		for (int i = 0; i < nViews; ++i) {
			if (gameTexAll || i == screens[game_screen()].view) ui_update_texture(gameTex[i], shownFb, shownW * 4);
		}
		gameTexAll = false;
		/* (tests, RETRODS_DRAW_SHOTS_ONLY: only the frames a screenshot or a
		 * touch needs are drawn) */
		if (!ui_script_quiet()) {
			ui_sync_view(pacedHz > 0 ? pacedView : cfg->vsync ? screens[game_screen()].view : -1, pacedHz > 0 ? pacedEvery : 1); /* Match Display Rate; vsync: testing (docs/ANDROID.md) */
			{
				/* the other screen's redraw waits a frame if this one is past 70% of its time */
				static double budget = -1;
				if (budget < 0) budget = getenv("RETRODS_DRAW_BUDGET") ? atof(getenv("RETRODS_DRAW_BUDGET")) : frameSec * 1000.0 * 0.7;
				ui_present_budget(ff ? -1 : screens[game_screen()].view, workT0, budget);
			}
			ui_clear();
			draw_game();
			if (shotPending) {
				shotPending = false;
				take_screenshot();
			}
			ra_draw(nScreens > 1 ? 1 - game_screen() : 0); /* after the screenshot: not in it */
			if (barOpen && !cfg->barPinned && barGesture == G_NONE && SDL_GetTicks() - barSince > 5000) bar_close();
			draw_rewind();
			draw_hint(); /* (under the bar, as it comes up) */
			draw_bar();
			/* (the keeper: the game and its sound go on if this is held up; not
			 * for tests, whose frames are counted exactly) */
			const bool keep = cfg->keepSound && audio && !ff && !rewinding && !noPace && !ui_scripted();
			if (keep) keeper_arm(keys, audio, want.freq, targetQueued * 3, aheadInFlight ? 1 : 0);
			ui_present();
			if (keep) {
				int kept = keeper_disarm();
				if (kept) {
					/* those frames are the game's time since: the next one is
					 * due a frame after the last of them */
					next += kept * (pacedHz > 0 ? frameSec / 1.005 : frameSec);
					frameCount += (unsigned) kept;
					loopCur.kept = kept;
					keptTotal += kept;
				}
			}
		}
		Uint64 drawT1 = SDL_GetPerformanceCounter();
		drawTicks += drawT1 - drawT0;
		{
			const double msPer = 1000.0 / (double) SDL_GetPerformanceFrequency();
			int vTop = screens[0].view, vBot = nScreens > 1 ? screens[1].view : vTop;
			loopCur.emu0 = emuT0, loopCur.emu1 = emuT1, loopCur.draw0 = drawT0, loopCur.draw1 = drawT1;
			loopCur.audio = (double) audT * msPer;
			loopCur.presentTop = ui_last_present_ms(vTop);
			loopCur.presentBottom = vBot != vTop ? ui_last_present_ms(vBot) : 0;
			loopCur.valid = !ff && !rewinding; /* (fast-forward and rewind run their own way) */
		}
		{
			double msPer = 1000.0 / (double) SDL_GetPerformanceFrequency();
			double total = (double) (drawT1 - workT0) * msPer;
			/* matched, each frame includes the wait for the display's refresh:
			 * only one that missed its refresh (about two frames long) is slow */
			if (!ff && !rewinding && pacedHz > 0 && total > frameSec * 1000.0 * 1.4) ++pacedMisses;
			if (!ff && total > frameSec * 1000.0 * (pacedHz > 0 ? 1.4 : 1.0)) {
				SlowFrame f = { total, (double) (emuT0 - workT0) * msPer, (double) (emuT1 - emuT0 - raT) * msPer, (double) raT * msPer,
					            (double) (drawT0 - emuT1) * msPer, (double) (drawT1 - drawT0) * msPer, 0, 0, 0,
					            (unsigned long long) frameCount };
				/* by screen (top first, as they're stacked), not by view */
				int sTop = 0, sBot = nScreens > 1 ? 1 : 0;
				int vTop = screens[sTop].view, vBot = screens[sBot].view;
				f.top = ui_last_present_ms(vTop);
				f.bottom = vBot != vTop ? ui_last_present_ms(vBot) : 0;
				int red = ui_last_redraws();
				f.redraws = ((red >> vTop) & 1) | (vBot != vTop ? ((red >> vBot) & 1) << 1 : 0);
				f.preempted = thread_preemptions() - preempt0;
				f.cpu = current_cpu();
				f.mhz = cpu_mhz(f.cpu);
				double cpuNow = thread_cpu_ms();
				f.cpuMs = cpuNow - cpuMs0;
				f.waitEmu = SDL_max(0.0, (double) (emuT1 - emuT0) * msPer - (emuCpu1 - emuCpu0));
				f.waitDraw = SDL_max(0.0, (double) (drawT1 - drawT0) * msPer - (cpuNow - drawCpu0));
				f.audio = (double) audT * msPer;
				slow_frame_note(f);
			}
			static Uint64 lastReport;
			if (drawT1 - lastReport > SDL_GetPerformanceFrequency()) {
				slow_frame_report(debug);
				lastReport = drawT1;
			}
		}
#ifdef __ANDROID__
		if (frameCount == 1) fprintf(stderr, "startup: first frame shown\n");
#endif

		/* ---- pacing: real-time clock nudged by audio queue depth */
		double now = SDL_GetPerformanceCounter() / freq;
		fpsFrames += runs; /* emulated frames */
		if (now - fpsT0 >= 1.0) {
			fps = fpsFrames / (now - fpsT0);
			speedPct = fps / game_fps() * 100.0;
			if (debug) fprintf(stderr, "speed %.1f fps (%.0f%%)\n", fps, speedPct);
			fpsFrames = 0;
			fpsT0 = now;
			if (pacedHz > 0) {
				pacedMissSecs = pacedMisses >= 3 ? pacedMissSecs + 1 : 0;
				if (pacedMisses >= 3) fprintf(stderr, "frame %llu: %d frame(s) missed the display's refresh in the last second\n", (unsigned long long) frameCount, pacedMisses);
				pacedMisses = 0;
				if (pacedMissSecs >= 3) {
					pacedFallback = true;
					pacing_update(); /* (logs the "frame pacing:" line again) */
					next = now;
				}
			}
		}
		/* fast-forward is paced too: ff_speed frames per real frame. Matched to
		 * the display, presenting in step with it sets the pace; the clock runs
		 * a little faster, as a limit for when the display doesn't make us wait. */
		next += pacedHz > 0 ? frameSec / 1.005 : frameSec;
		if (audio) {
			int queued = (int) (SDL_GetQueuedAudioSize(audio) / 4);
			if (queued > targetQueued * 3 / 2) next += frameSec * 0.015;
			else if (queued < targetQueued / 2) next -= frameSec * 0.015;
		}
		if (next < now - 4 * frameSec) next = now;
		if (noPace) next = now; /* benchmarking: run as fast as possible */
#ifdef __ANDROID__
		android_perf_report((int64_t) ((SDL_GetPerformanceCounter() - workT0) * 1e9 / freq));
#endif
		double wait = next - now;
		loopCur.sleep0 = SDL_GetPerformanceCounter();
		loopCur.sleepWant = wait > 0 ? wait : 0;
		if (wait > 0) {
			struct timespec ts = { 0, (long) (wait * 1e9) };
			nanosleep(&ts, NULL);
		}
		loopCur.end = SDL_GetPerformanceCounter();
	}

	keeper_stop();
	ahead_stop(); /* before anything else touches the core */
	ui_on_background(NULL);
	ui_on_foreground(NULL);
	ui_sync_view(-1, 1);
	fprintf(stderr, "leaving game after %llu frames\n", (unsigned long long) frameCount);
	if (bugPend.pending) {
		/* (a report waiting for its note: saved with what's typed) */
		char out[200];
		bug_finish(noteOpen ? noteText : "", out, sizeof(out));
		noteOpen = false;
	}
	auto_save();
	ra_game_end();
	if (audio) SDL_CloseAudioDevice(audio);
	if (audioConv) SDL_FreeAudioStream(audioConv);
	mapcap_destroy(mc);
	mc = NULL;
	native_free();
	alt_free(); /* (saves what each file has seen) */
	emu_rewind_deinit();
	emu_close(&emu); /* flushes the battery save */
	for (int i = 0; i < MAP_MAX_FRAMES; ++i) {
		free(mapRes.frames[i]);
		mapRes.frames[i] = NULL;
	}
	free(stillPx);
	stillPx = NULL;
	stillValid = false;
	for (int i = 0; i < nViews; ++i) {
		SDL_DestroyTexture(gameTex[i]);
		SDL_DestroyTexture(mapTex[i]);
		gameTex[i] = mapTex[i] = NULL;
	}
	ui_free_image(logoTex);
	ui_free_image(logoCanvasTex);
	ui_free_image(overlayTex[0]);
	ui_free_image(overlayTex[1]);
	overlayMode[0][0] = overlayMode[1][0] = 0;
	ui_free_image(bezelTex[0]);
	ui_free_image(bezelTex[1]);
	bezelKey[0][0] = bezelKey[1][0] = 0;
	return exitTo;
}

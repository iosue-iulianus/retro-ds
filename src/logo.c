#include "logo.h"
#include "emu.h"
#include "profiles.h"
#ifdef __ANDROID__
#include "android.h"
#endif

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wunused-function"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBIW_WINDOWS_UTF8 0
#include "stb_image_write.h"
#pragma GCC diagnostic pop

static volatile bool cancelAll;

/* The black round a logo, reached from the box's edge through black,
 * made transparent; black next to the logo (a pixel's outline) kept */
static void trim_black(uint8_t* rgba, int w, int h) {
	uint8_t* mark = calloc((size_t) w * h, 1);
	int* stack = malloc(sizeof(int) * (size_t) w * h);
	if (!mark || !stack) {
		free(mark), free(stack);
		return;
	}
	#define BLACK(i) (!rgba[(i) * 4] && !rgba[(i) * 4 + 1] && !rgba[(i) * 4 + 2])
	int n = 0;
	for (int x = 0; x < w; ++x) {
		int e[2] = { x, (h - 1) * w + x };
		for (int k = 0; k < 2; ++k) if (BLACK(e[k]) && !mark[e[k]]) mark[e[k]] = 1, stack[n++] = e[k];
	}
	for (int y = 0; y < h; ++y) {
		int e[2] = { y * w, y * w + w - 1 };
		for (int k = 0; k < 2; ++k) if (BLACK(e[k]) && !mark[e[k]]) mark[e[k]] = 1, stack[n++] = e[k];
	}
	while (n) {
		int i = stack[--n], x = i % w, y = i / w;
		const int nb[4][2] = { { x - 1, y }, { x + 1, y }, { x, y - 1 }, { x, y + 1 } };
		for (int k = 0; k < 4; ++k) {
			int nx = nb[k][0], ny = nb[k][1];
			if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
			int j = ny * w + nx;
			if (!mark[j] && BLACK(j)) mark[j] = 1, stack[n++] = j;
		}
	}
	/* kept: black next to (a corner counts) a pixel that isn't black */
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) {
			int i = y * w + x;
			if (!mark[i]) continue;
			bool edge = false;
			for (int dy = -1; dy <= 1 && !edge; ++dy) {
				for (int dx = -1; dx <= 1; ++dx) {
					int nx = x + dx, ny = y + dy;
					if (nx >= 0 && ny >= 0 && nx < w && ny < h && !BLACK(ny * w + nx)) edge = true;
				}
			}
			if (!edge) rgba[i * 4 + 3] = 0;
		}
	}
	#undef BLACK
	free(mark), free(stack);
}

/* The profile's second picture (dropRects), the frame after the logo's */
static void logo_drop(Emu* e, const MapProfile* p, const char* outPng) {
	if (p->logo.dropPokeAddr) emu_write8(e, p->logo.dropPokeAddr, p->logo.dropPokeVal);
	emu_frame_blind(e, 0);
	if (p->logo.dropPokeAddr) emu_write8(e, p->logo.dropPokeAddr, p->logo.dropPokeVal);
	emu_frame(e, 0);
	int x0 = e->w, y0 = e->h, x1 = 0, y1 = 0;
	for (int r = 0; r < 4 && p->logo.dropRects[r][2] > 0; ++r) {
		const int* q = p->logo.dropRects[r];
		x0 = q[0] < x0 ? q[0] : x0, y0 = q[1] < y0 ? q[1] : y0, x1 = q[2] > x1 ? q[2] : x1, y1 = q[3] > y1 ? q[3] : y1;
	}
	x1 = x1 > e->w ? e->w : x1, y1 = y1 > e->h ? e->h : y1;
	int w = x1 - x0, h = y1 - y0;
	uint8_t* rgba = w > 0 && h > 0 ? calloc((size_t) w * h, 4) : NULL;
	if (!rgba) return;
	for (int y = y0; y < y1; ++y) {
		for (int x = x0; x < x1; ++x) {
			bool in = false;
			for (int r = 0; r < 4 && p->logo.dropRects[r][2] > 0 && !in; ++r) {
				const int* q = p->logo.dropRects[r];
				in = x >= q[0] && x < q[2] && y >= q[1] && y < q[3];
			}
			uint32_t c = e->fb[y * e->w + x];
			/* (the backdrop, the frame's corner's colour: clear; a flash can colour it) */
			if (!in || (c & 0xFFFFFF) == (e->fb[0] & 0xFFFFFF)) continue;
			uint8_t* o = &rgba[((y - y0) * w + x - x0) * 4];
			o[0] = c & 0xFF, o[1] = (c >> 8) & 0xFF, o[2] = (c >> 16) & 0xFF, o[3] = 255;
		}
	}
	char path[1100], tmp[1100];
	logo_drop_path(outPng, path, sizeof path);
	snprintf(tmp, sizeof tmp, "%s.tmp", path);
	bool ok = stbi_write_png(tmp, w, h, 4, rgba, w * 4) && rename(tmp, path) == 0;
	free(rgba);
	fprintf(stderr, "logo: %s's drop %dx%d at %d,%d -> %s\n", p->codes[0], w, h, x0, y0, ok ? path : "write failed");
}

void logo_drop_path(const char* logoPng, char* out, size_t n) {
	snprintf(out, n, "%s", logoPng);
	char* dot = strrchr(out, '.');
	if (dot && (size_t) (dot - out) + 10 < n) strcpy(dot, "-drop.png");
}

/* The logo from the game's own title screen: the ROM booted on a silent
 * core and run to its title (the profile's logo.wait, pressStart and wait2,
 * or its steps); the picture inside the profile's box is kept as a PNG. */
bool logo_extract(const char* romPath, const char* gameCode, const char* outPng) {
	const MapProfile* p = profile_find(gameCode);
	if (!p || (!p->logo.wait && !p->logo.steps[0].frames)) return false;
	Emu e = { 0 };
	if (!emu_open(&e, romPath, NULL, false)) return false;
	e.cancel = &cancelAll;
	if (p->logo.steps[0].frames) {
		for (const MacroStep* s = p->logo.steps; s < p->logo.steps + 24 && s->frames; ++s) {
			for (int f = 0; f < s->frames; ++f) emu_frame_blind(&e, s->keys);
		}
	} else {
		for (int f = 0; f < p->logo.wait; ++f) emu_frame_blind(&e, 0);
		if (p->logo.pressStart) {
			for (int f = 0; f < 4; ++f) emu_frame_blind(&e, K_START);
			for (int f = 0; f < p->logo.wait2; ++f) emu_frame_blind(&e, 0);
		}
	}
	if (p->logo.pokeAddr) {
		/* (the game copies it to the PPU at the next frame's start) */
		emu_write8(&e, p->logo.pokeAddr, p->logo.pokeVal);
		emu_frame_blind(&e, 0);
		emu_write8(&e, p->logo.pokeAddr, p->logo.pokeVal);
	}
	emu_frame(&e, 0); /* (drawn: the picture taken) */
	bool ok = false;
	if (!emu_cancelled(&e)) {
		int x0 = p->logo.minX, y0 = p->logo.minY;
		int x1 = p->logo.maxX > 0 ? p->logo.maxX : e.w, y1 = p->logo.maxY > 0 ? p->logo.maxY : e.h;
		x1 = x1 > e.w ? e.w : x1, y1 = y1 > e.h ? e.h : y1;
		int w = x1 - x0, h = y1 - y0;
		uint8_t* rgba = w > 0 && h > 0 ? malloc((size_t) w * h * 4) : NULL;
		for (int y = 0; rgba && y < h; ++y) {
			for (int x = 0; x < w; ++x) {
				uint32_t c = e.fb[(y0 + y) * e.w + x0 + x];
				uint8_t* o = &rgba[(y * w + x) * 4];
				o[0] = c & 0xFF, o[1] = (c >> 8) & 0xFF, o[2] = (c >> 16) & 0xFF, o[3] = 255;
			}
		}
		if (rgba && p->logo.trimBlack) trim_black(rgba, w, h);
		char tmp[1100];
		snprintf(tmp, sizeof(tmp), "%s.tmp", outPng);
		ok = rgba && stbi_write_png(tmp, w, h, 4, rgba, w * 4) && rename(tmp, outPng) == 0;
		free(rgba);
		fprintf(stderr, "logo: %s %dx%d -> %s\n", gameCode, w, h, ok ? outPng : "write failed");
		if (ok && p->logo.dropRects[0][2] > 0) logo_drop(&e, p, outPng);
	}
	emu_close(&e);
	return ok;
}

/* ------------------------------------------------------------ queue */

typedef struct Job {
	char rom[1024], code[9], out[1024];
	bool urgent; /* runs while the queue's paused: the game being started or played */
	struct Job* next;
} Job;

static pthread_mutex_t qlock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t qcond = PTHREAD_COND_INITIALIZER;
static Job* queue;
static bool running, stopping;
static pthread_t thread;
static volatile uint32_t generation;
static int pending, finished; /* jobs queued or running / done since the queue last went idle */
static bool paused; /* a game's playing: only urgent jobs run */
static char current[9]; /* the code of the job running, "" if none */

static bool exists(const char* p) {
	struct stat st;
	return stat(p, &st) == 0;
}

static void do_job(Job* j) {
	if (!exists(j->out)) {
		logo_extract(j->rom, j->code, j->out);
	}
	free(j);
	__atomic_add_fetch(&generation, 1, __ATOMIC_SEQ_CST);
}

static void* worker(void* arg) {
	(void) arg;
	/* background work: let the game (and its audio) have the CPU first */
	setpriority(PRIO_PROCESS, (id_t) syscall(SYS_gettid), 19);
#ifdef __ANDROID__
	android_pin_worker_thread("logo extraction");
#endif
	pthread_mutex_lock(&qlock);
	while (!stopping) {
		/* the first job, or while paused the first urgent one */
		Job** at = &queue;
		while (*at && paused && !(*at)->urgent) at = &(*at)->next;
		Job* j = *at;
		if (!j) {
			pthread_cond_wait(&qcond, &qlock);
			continue;
		}
		*at = j->next;
		snprintf(current, sizeof(current), "%s", j->code);
		pthread_mutex_unlock(&qlock);
		do_job(j);
		pthread_mutex_lock(&qlock);
		current[0] = 0;
		--pending;
		++finished;
		pthread_cond_broadcast(&qcond);
	}
	pthread_mutex_unlock(&qlock);
	return NULL;
}

/* Outputs already tried this run. A job that couldn't make its file (the
 * logo not found on the title screen, or the folder not writable) isn't
 * queued again until the next start: each finished job makes the game list
 * reload its logos, which asked for the missing one again, so one failure
 * looped for ever (and "Preparing games" counted up without end). */
static char (*tried)[1024];
static int nTried, capTried;

static bool tried_before(const char* out) {
	for (int i = 0; i < nTried; ++i) {
		if (!strcmp(tried[i], out)) return true;
	}
	if (nTried == capTried) {
		int cap = capTried ? capTried * 2 : 32;
		char (*t)[1024] = realloc(tried, sizeof(*tried) * cap);
		if (!t) return false;
		tried = t;
		capTried = cap;
	}
	snprintf(tried[nTried++], sizeof(*tried), "%s", out);
	return false;
}

static void enqueue(const char* romPath, const char* gameCode, const char* outPng) {
	pthread_mutex_lock(&qlock);
	for (Job* j = queue; j; j = j->next) {
		if (!strcmp(j->out, outPng)) {
			pthread_mutex_unlock(&qlock);
			return;
		}
	}
	if (tried_before(outPng)) {
		pthread_mutex_unlock(&qlock);
		return;
	}
	Job* j = calloc(1, sizeof(*j));
	snprintf(j->rom, sizeof(j->rom), "%s", romPath);
	snprintf(j->code, sizeof(j->code), "%.8s", gameCode);
	snprintf(j->out, sizeof(j->out), "%s", outPng);
	j->urgent = paused; /* (asked for by the game being played: its own logo) */
	if (!pending) finished = 0;
	++pending;
	Job** tail = &queue;
	if (j->urgent) {
		while (*tail && (*tail)->urgent) tail = &(*tail)->next; /* (after the other urgent ones) */
		j->next = *tail;
	} else {
		while (*tail) tail = &(*tail)->next;
	}
	*tail = j;
	/* (SNESDS_SYNC=1, regression tests: inline) */
	if (!running && !(getenv("SNESDS_SYNC") && atoi(getenv("SNESDS_SYNC")))) running = pthread_create(&thread, NULL, worker, NULL) == 0;
	if (!running) {
		/* no thread (SNESDS_SYNC, or it couldn't start): the work now */
		while (queue) {
			Job* q = queue;
			queue = q->next;
			do_job(q);
			--pending;
			++finished;
		}
		pthread_mutex_unlock(&qlock);
		return;
	}
	pthread_cond_signal(&qcond);
	pthread_mutex_unlock(&qlock);
}

void logo_request(const char* romPath, const char* gameCode, const char* outPng) {
	if (exists(outPng)) return;
	const MapProfile* p = profile_find(gameCode);
	if (!p || (!p->logo.wait && !p->logo.steps[0].frames)) return;
	enqueue(romPath, gameCode, outPng);
}

void logo_front(const char* gameCode, bool urgent) {
	pthread_mutex_lock(&qlock);
	Job *mine = NULL, **mineTail = &mine;
	for (Job** at = &queue; *at;) {
		Job* j = *at;
		if (!strncmp(j->code, gameCode, 8)) {
			*at = j->next;
			j->next = NULL;
			j->urgent |= urgent;
			*mineTail = j, mineTail = &j->next;
		} else {
			at = &j->next;
		}
	}
	if (mine) {
		*mineTail = queue;
		queue = mine;
		pthread_cond_signal(&qcond);
	}
	pthread_mutex_unlock(&qlock);
}

bool logo_busy(const char* gameCode) {
	pthread_mutex_lock(&qlock);
	bool busy = (gameCode && !strncmp(current, gameCode, 8)) || (!gameCode && current[0]);
	for (Job* j = queue; j && gameCode && !busy; j = j->next) busy = !strncmp(j->code, gameCode, 8);
	pthread_mutex_unlock(&qlock);
	return busy;
}

void logo_pause(bool on) {
	pthread_mutex_lock(&qlock);
	paused = on;
	pthread_cond_signal(&qcond);
	pthread_mutex_unlock(&qlock);
}

void logo_progress(int* done, int* total) {
	pthread_mutex_lock(&qlock);
	*done = finished;
	*total = finished + pending;
	pthread_mutex_unlock(&qlock);
}

uint32_t logo_generation(void) {
	return __atomic_load_n(&generation, __ATOMIC_SEQ_CST);
}

void logo_shutdown(void) {
	cancelAll = true;
	pthread_mutex_lock(&qlock);
	stopping = true;
	while (queue) {
		Job* j = queue;
		queue = j->next;
		free(j);
	}
	pending = 0;
	pthread_cond_signal(&qcond);
	pthread_mutex_unlock(&qlock);
	if (running) pthread_join(thread, NULL);
	running = false;
}

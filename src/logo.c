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
	emu_frame(&e, 0); /* (drawn: the picture taken) */
	bool ok = false;
	if (!emu_cancelled(&e)) {
		int x0 = p->logo.minX, y0 = p->logo.minY;
		int x1 = p->logo.maxX > 0 ? p->logo.maxX : GBA_W, y1 = p->logo.maxY > 0 ? p->logo.maxY : GBA_H;
		x1 = x1 > GBA_W ? GBA_W : x1, y1 = y1 > GBA_H ? GBA_H : y1;
		int w = x1 - x0, h = y1 - y0;
		uint8_t* rgba = w > 0 && h > 0 ? malloc((size_t) w * h * 4) : NULL;
		for (int y = 0; rgba && y < h; ++y) {
			for (int x = 0; x < w; ++x) {
				uint32_t c = e.fb[(y0 + y) * GBA_W + x0 + x];
				uint8_t* o = &rgba[(y * w + x) * 4];
				o[0] = c & 0xFF, o[1] = (c >> 8) & 0xFF, o[2] = (c >> 16) & 0xFF, o[3] = 255;
			}
		}
		char tmp[1100];
		snprintf(tmp, sizeof(tmp), "%s.tmp", outPng);
		ok = rgba && stbi_write_png(tmp, w, h, 4, rgba, w * 4) && rename(tmp, outPng) == 0;
		free(rgba);
		fprintf(stderr, "logo: %s %dx%d -> %s\n", gameCode, w, h, ok ? outPng : "write failed");
	}
	emu_close(&e);
	return ok;
}

/* ------------------------------------------------------------ queue */

typedef struct Job {
	char rom[1024], code[5], out[1024];
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
static char current[5]; /* the code of the job running, "" if none */

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
	snprintf(j->code, sizeof(j->code), "%.4s", gameCode);
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
		if (!strncmp(j->code, gameCode, 4)) {
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
	bool busy = (gameCode && !strncmp(current, gameCode, 4)) || (!gameCode && current[0]);
	for (Job* j = queue; j && gameCode && !busy; j = j->next) busy = !strncmp(j->code, gameCode, 4);
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

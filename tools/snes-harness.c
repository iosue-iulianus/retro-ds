/* SNES DS M0 harness: two instances of a libretro core, driven by a script.
 *
 *   snes-harness CORE ROM OUTDIR SCRIPT
 *
 * CORE is copied to OUTDIR/core-a.so and OUTDIR/core-b.so so the two
 * instances (A: the live game, B: the hidden copy) get separate globals.
 * Each has its own data directory (OUTDIR/a, OUTDIR/b) for SRAM.
 *
 * Script: one command per line, '#' comments. Prefix a command with "b " to
 * run it on instance B (default A).
 *   wait N                 run N frames, no keys
 *   hold KEYS N            hold KEYS for N frames (A B X Y L R START SELECT
 *                          UP DOWN LEFT RIGHT, joined with '+'; '-' none)
 *   tap KEYS [COUNT]       press 3 frames, release 9, COUNT times
 *   shot NAME              OUTDIR/NAME.png of the current picture
 *   save NAME / load NAME  OUTDIR/NAME.state (a path with '/' as it is;
 *                          loading also takes RetroArch state files)
 *   peek ADDR [LEN]        print LEN bytes (hex SNES address: 7E0998)
 *   poke ADDR LEN VAL      write 1 or 2 bytes
 *   dumpram NAME           OUTDIR/NAME.ram: the 128 KB of work RAM
 *   copy                   A's state into B (timed)
 *   mapcap NAME [opts]     copy A into B, press a button on B and run B
 *                          until the screen is up and settled; writes
 *                          NAME-a.png (A, untouched) and NAME-b.png (B).
 *                          opts (key=value): press=START hold=4
 *                          until=ADDR:VAL[:MASK] (up to 4, all must hold)
 *                          stable=8 maxdiff=64 max=600 blind=1 (frames
 *                          before `until` holds run without drawing)
 *                          verbose=1 (pixels changed, frame by frame)
 *   bench [FRAMES]         the M0 measurements (see docs/M0-results.md)
 *   context N              savestate context for both (0 normal,
 *                          1 runahead same instance, 2 same binary)
 *   pattern N              bench's input: 0 none, 1 right/left/jump/stand
 *                          30 frames each (default), 2 right held, 3 as 1
 *                          but changing 15 frames off the serializes
 *   keysfirst 0|1          bench serializes A with emu_state_save_live
 *                          (the next frame's keys given first) or not (0,
 *                          the default, shows the problem it fixes)
 */
#include "../src/retro/emu.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

bool write_png(const char* path, const uint32_t* px, int w, int h, int stride); /* tools/png.c */

static const char* outDir;
static Emu inst[2];

static double now_ms(void) {
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static void out_path(char* buf, size_t n, const char* name, const char* ext) {
	if (strchr(name, '/')) snprintf(buf, n, "%s", name);
	else snprintf(buf, n, "%s/%s%s", outDir, name, ext);
}

static bool copy_file(const char* from, const char* to) {
	FILE* in = fopen(from, "rb");
	FILE* out = in ? fopen(to, "wb") : NULL;
	bool ok = in && out;
	char buf[1 << 16];
	size_t n;
	while (ok && (n = fread(buf, 1, sizeof buf, in)) > 0) ok = fwrite(buf, 1, n, out) == n;
	if (in) fclose(in);
	if (out && fclose(out)) ok = false;
	return ok;
}

static uint32_t parse_keys(const char* s) {
	static const struct { const char* name; uint32_t bit; } names[] = {
		{ "A", K_A }, { "B", K_B }, { "X", K_X }, { "Y", K_Y }, { "L", K_L }, { "R", K_R },
		{ "START", K_START }, { "SELECT", K_SELECT },
		{ "UP", K_UP }, { "DOWN", K_DOWN }, { "LEFT", K_LEFT }, { "RIGHT", K_RIGHT },
	};
	uint32_t keys = 0;
	char tmp[128];
	snprintf(tmp, sizeof tmp, "%s", s);
	char* save = NULL;
	for (char* tok = strtok_r(tmp, "+", &save); tok; tok = strtok_r(NULL, "+", &save)) {
		if (!strcmp(tok, "-")) continue;
		bool found = false;
		for (size_t i = 0; i < sizeof names / sizeof *names; ++i) {
			if (!strcmp(tok, names[i].name)) keys |= names[i].bit, found = true;
		}
		if (!found) fprintf(stderr, "unknown key %s\n", tok);
	}
	return keys;
}

static void shot(Emu* e, const char* name) {
	char path[1200];
	out_path(path, sizeof path, name, ".png");
	if (!write_png(path, e->fb, e->w, e->h, e->w)) fprintf(stderr, "can't write %s\n", path);
	else printf("shot %s (%dx%d)\n", path, e->w, e->h);
}

/* ---- state copy ---- */

static uint8_t* stateBuf;
static size_t stateCap;

/* A's state into B; returns ms, or -1 */
static double copy_state(Emu* from, Emu* to, size_t* sizeOut) {
	double t0 = now_ms();
	size_t n = emu_state_size(from);
	if (n > stateCap) {
		free(stateBuf);
		stateBuf = malloc(n);
		stateCap = n;
	}
	if (!emu_state_save(from, stateBuf, n) || !emu_state_load(to, stateBuf, n)) return -1;
	if (sizeOut) *sizeOut = n;
	return now_ms() - t0;
}

/* ---- map capture ---- */

typedef struct { uint32_t addr, value, mask; } Cond;

static bool conds_hold(Emu* e, const Cond* c, int n) {
	for (int i = 0; i < n; ++i) {
		uint32_t v = c[i].mask > 0xFF ? emu_read16(e, c[i].addr) : emu_read8(e, c[i].addr);
		if ((v & c[i].mask) != c[i].value) return false;
	}
	return true;
}

static int count_diff(const uint32_t* a, const uint32_t* b, size_t n) {
	int d = 0;
	for (size_t i = 0; i < n; ++i) d += (a[i] & 0xFFFFFF) != (b[i] & 0xFFFFFF);
	return d;
}

static bool all_black(const Emu* e) {
	for (size_t i = 0; i < (size_t) e->w * e->h; ++i) {
		if (e->fb[i] & 0xFFFFFF) return false;
	}
	return true;
}

static void mapcap(char* args) {
	char name[256] = "map";
	uint32_t press = K_START;
	int hold = 4, stable = 8, maxdiff = 64, maxFrames = 600;
	bool blind = true, verbose = false;
	Cond conds[4];
	int nConds = 0;
	char* save = NULL;
	for (char* tok = strtok_r(args, " \t", &save); tok; tok = strtok_r(NULL, " \t", &save)) {
		char* eq = strchr(tok, '=');
		if (!eq) {
			snprintf(name, sizeof name, "%s", tok);
			continue;
		}
		*eq = 0;
		const char* v = eq + 1;
		if (!strcmp(tok, "press")) press = parse_keys(v);
		else if (!strcmp(tok, "hold")) hold = atoi(v);
		else if (!strcmp(tok, "stable")) stable = atoi(v);
		else if (!strcmp(tok, "maxdiff")) maxdiff = atoi(v);
		else if (!strcmp(tok, "max")) maxFrames = atoi(v);
		else if (!strcmp(tok, "blind")) blind = atoi(v) != 0;
		else if (!strcmp(tok, "verbose")) verbose = atoi(v) != 0;
		else if (!strcmp(tok, "until") && nConds < 4) {
			unsigned a = 0, val = 0, mask = 0xFF;
			if (sscanf(v, "%x:%x:%x", &a, &val, &mask) >= 2) conds[nConds++] = (Cond) { a, val, mask };
		}
	}
	Emu* a = &inst[0];
	Emu* b = &inst[1];
	double t0 = now_ms();
	size_t stateSize = 0;
	double copyMs = copy_state(a, b, &stateSize);
	if (copyMs < 0) {
		printf("mapcap %s: state copy failed\n", name);
		return;
	}
	int f = 0;
	for (; f < hold; ++f) blind ? emu_frame_blind(b, press) : emu_frame(b, press);

	/* until the conditions hold: without drawing if blind */
	int condFrame = -1;
	while (f < maxFrames && nConds && !conds_hold(b, conds, nConds)) {
		blind ? emu_frame_blind(b, 0) : emu_frame(b, 0);
		++f;
	}
	if (nConds && f < maxFrames) condFrame = f;

	/* then until the picture holds still (not black) for `stable` frames */
	size_t cap = 0;
	uint32_t* prev = NULL;
	int quiet = 0, settleFrame = -1;
	while (f < maxFrames) {
		emu_frame(b, 0);
		++f;
		size_t n = (size_t) b->w * b->h;
		int diff = prev && n == cap ? count_diff(prev, b->fb, n) : -1;
		if (verbose) printf("  frame %d: %d pixels changed%s\n", f, diff, all_black(b) ? " (black)" : "");
		if (nConds && !conds_hold(b, conds, nConds)) quiet = 0;
		else if (diff >= 0 && !all_black(b) && diff <= maxdiff) ++quiet;
		else quiet = 0;
		if (n != cap) {
			free(prev);
			prev = malloc(n * sizeof *prev);
			cap = n;
		}
		memcpy(prev, b->fb, n * sizeof *prev);
		if (quiet >= stable) {
			settleFrame = f;
			break;
		}
	}
	free(prev);
	double ms = now_ms() - t0;
	if (settleFrame < 0) printf("mapcap %s: FAILED, not settled within %d frames (conditions %s)\n", name, maxFrames, condFrame >= 0 ? "held" : nConds ? "never held" : "none");
	else printf("mapcap %s: ok: state %zu bytes copied in %.2f ms; conditions at frame %d; settled at frame %d (%d stable); %.1f ms in all\n", name, stateSize, copyMs, condFrame, settleFrame, stable, ms);
	char nm[300];
	snprintf(nm, sizeof nm, "%s-a", name);
	shot(a, nm);
	snprintf(nm, sizeof nm, "%s-b", name);
	shot(b, nm);
}

/* ---- bench ---- */

static void* run_frames(void* p) {
	Emu* e = p;
	emu_frame(e, 0);
	return NULL;
}

/* a fixed input pattern for the comparisons: run right, run left, jump,
 * stand, 30 frames each, so the game state actually moves */
static bool keysFirst; /* script: keysfirst 1 */
static int patternMode = 1; /* script: pattern 0 (no keys), 1 (the pattern), 2 (RIGHT held), 3 (the pattern 15 frames later) */

static uint32_t pattern(int i) {
	static const uint32_t keys[4] = { K_RIGHT, K_LEFT, K_A, 0 };
	if (patternMode == 0) return 0;
	if (patternMode == 2) return K_RIGHT;
	if (patternMode == 3) return keys[((i + 15) / 30) % 4]; /* changes away from the serializes */
	return keys[(i / 30) % 4];
}

static bool same_machine(Emu* x, Emu* y, int* wramDiff, int* fbDiff) {
	size_t nx, ny;
	uint8_t* wx = emu_wram(x, &nx);
	uint8_t* wy = emu_wram(y, &ny);
	*wramDiff = 0;
	for (size_t i = 0; wx && wy && i < nx && i < ny; ++i) {
		if (wx[i] == wy[i]) continue;
		if (*wramDiff < 24) printf("%s%06zX:%02X/%02X", *wramDiff ? " " : "    differing (A/B): ", 0x7E0000 + i, wx[i], wy[i]);
		++*wramDiff;
	}
	if (*wramDiff) printf("\n");
	*fbDiff = x->w == y->w && x->h == y->h ? count_diff(x->fb, y->fb, (size_t) x->w * x->h) : -1;
	return *wramDiff == 0 && *fbDiff == 0;
}

static void bench(int frames) {
	Emu* a = &inst[0];
	Emu* b = &inst[1];
	printf("bench: core %s, ROM CRC32 %08X, %d frames per run, state context %d\n", a->coreName, a->romCrc, frames, a->stateContext);

	/* keep a starting point so every test starts from the same moment */
	size_t n = emu_state_size(a);
	uint8_t* start = malloc(n);
	uint8_t* tmp = malloc(n);
	uint8_t* tmp2 = malloc(n);
	emu_state_save(a, start, n);
	emu_state_load(b, start, n);

	double t0 = now_ms();
	for (int i = 0; i < frames; ++i) emu_frame(a, 0);
	double vis = (now_ms() - t0) / frames;
	emu_state_load(a, start, n);
	t0 = now_ms();
	for (int i = 0; i < frames; ++i) emu_frame_blind(a, 0);
	double blind = (now_ms() - t0) / frames;
	printf("  one instance: %.3f ms/frame drawn (%.0f fps), %.3f ms/frame blind (%.0f fps)\n", vis, 1000 / vis, blind, 1000 / blind);

	emu_state_load(a, start, n);
	t0 = now_ms();
	for (int i = 0; i < frames; ++i) {
		pthread_t t;
		pthread_create(&t, NULL, run_frames, b);
		emu_frame(a, 0);
		pthread_join(t, NULL);
	}
	double par = (now_ms() - t0) / frames;
	printf("  A and B at once on two threads: %.3f ms per frame pair (%.0f fps)\n", par, 1000 / par);

	int reps = 200;
	emu_state_load(a, start, n);
	t0 = now_ms();
	size_t sz = 0;
	for (int i = 0; i < reps; ++i) sz = emu_state_size(a);
	double sizeMs = (now_ms() - t0) / reps;
	t0 = now_ms();
	for (int i = 0; i < reps; ++i) emu_state_save(a, tmp, n);
	double saveMs = (now_ms() - t0) / reps;
	t0 = now_ms();
	for (int i = 0; i < reps; ++i) emu_state_load(a, tmp, n);
	double loadMs = (now_ms() - t0) / reps;
	t0 = now_ms();
	for (int i = 0; i < reps; ++i) emu_state_load(b, tmp, n);
	double loadBMs = (now_ms() - t0) / reps;
	printf("  state: %zu bytes; serialize_size %.3f ms, serialize %.3f ms, unserialize %.3f ms (same instance), %.3f ms (into B)\n", sz, sizeMs, saveMs, loadMs, loadBMs);

	/* does serializing run the core? two saves in a row with no frame between */
	emu_state_load(a, start, n);
	emu_frame(a, 0);
	emu_state_save(a, tmp, n);
	emu_state_save(a, tmp2, n);
	size_t diffBytes = 0;
	for (size_t i = 0; i < n; ++i) diffBytes += tmp[i] != tmp2[i];
	printf("  two serializes in a row differ in %zu bytes (%s)\n", diffBytes, diffBytes ? "the core ran between them: serialize advances it" : "serialize doesn't run the core");

	/* does serializing the live game every second change where it goes?
	 * A: serialize every 60 frames; B: never; same start, same (no) input */
	emu_state_load(a, start, n);
	emu_state_load(b, start, n);
	for (int i = 0; i < 600; ++i) {
		if (i % 60 == 0) {
			if (keysFirst) emu_state_save_live(a, pattern(i), tmp, n);
			else emu_state_save(a, tmp, n);
		}
		emu_frame(a, pattern(i));
		emu_frame(b, pattern(i));
	}
	int wd, fd;
	bool same = same_machine(a, b, &wd, &fd);
	printf("  600 frames with input, A serialized every 60, B never: %s (WRAM bytes differing %d, pixels differing %d)\n", same ? "identical" : "DIFFERENT", wd, fd);

	/* is a state copied from A into B faithful? both run 120 frames after */
	emu_state_load(a, start, n);
	for (int i = 0; i < 30; ++i) emu_frame(a, pattern(i));
	copy_state(a, b, NULL);
	for (int i = 0; i < 240; ++i) {
		emu_frame(a, pattern(i));
		emu_frame(b, pattern(i));
	}
	same = same_machine(a, b, &wd, &fd);
	printf("  copied A into B, 240 frames with input on both: %s (WRAM bytes differing %d, pixels differing %d)\n", same ? "identical" : "DIFFERENT", wd, fd);

	emu_state_load(a, start, n);
	emu_state_load(b, start, n);
	free(start);
	free(tmp);
	free(tmp2);
}

/* ---- script ---- */

int main(int argc, char** argv) {
	if (argc != 5) {
		fprintf(stderr, "usage: snes-harness CORE ROM OUTDIR SCRIPT\n");
		return 2;
	}
	const char* core = argv[1];
	const char* rom = argv[2];
	outDir = argv[3];
	mkdir(outDir, 0755);

	char corePath[2][1200], dataDir[2][1200];
	for (int i = 0; i < 2; ++i) {
		snprintf(corePath[i], sizeof corePath[i], "%s/core-%c.so", outDir, 'a' + i);
		snprintf(dataDir[i], sizeof dataDir[i], "%s/%c", outDir, 'a' + i);
		if (!copy_file(core, corePath[i])) {
			fprintf(stderr, "can't copy %s to %s\n", core, corePath[i]);
			return 1;
		}
		if (!emu_open(&inst[i], corePath[i], rom, dataDir[i], false)) return 1;
	}
	size_t wn = 0;
	emu_wram(&inst[0], &wn);
	printf("core %s, ROM CRC32 %08X (without copier header), %dx%d at %.3f fps, work RAM %zu bytes\n",
	       inst[0].coreName, inst[0].romCrc, inst[0].w, inst[0].h, inst[0].fps, wn);

	FILE* f = fopen(argv[4], "r");
	if (!f) {
		fprintf(stderr, "can't read %s\n", argv[4]);
		return 1;
	}
	char line[1024];
	while (fgets(line, sizeof line, f)) {
		char* hash = strchr(line, '#');
		if (hash) *hash = 0;
		char* p = line + strspn(line, " \t");
		p[strcspn(p, "\r\n")] = 0;
		if (!*p) continue;
		Emu* e = &inst[0];
		if (!strncmp(p, "b ", 2)) e = &inst[1], p += 2;
		else if (!strncmp(p, "a ", 2)) p += 2;

		char cmd[32] = "", s1[512] = "", s2[64] = "", s3[64] = "";
		sscanf(p, "%31s %511s %63s %63s", cmd, s1, s2, s3);
		if (!strcmp(cmd, "wait")) {
			for (int i = atoi(s1); i > 0; --i) emu_frame(e, 0);
		} else if (!strcmp(cmd, "hold")) {
			uint32_t k = parse_keys(s1);
			for (int i = atoi(s2); i > 0; --i) emu_frame(e, k);
		} else if (!strcmp(cmd, "tap")) {
			uint32_t k = parse_keys(s1);
			int count = *s2 ? atoi(s2) : 1;
			for (int c = 0; c < count; ++c) {
				for (int i = 0; i < 3; ++i) emu_frame(e, k);
				for (int i = 0; i < 9; ++i) emu_frame(e, 0);
			}
		} else if (!strcmp(cmd, "shot")) {
			shot(e, s1);
		} else if (!strcmp(cmd, "save") || !strcmp(cmd, "load")) {
			char path[1200];
			out_path(path, sizeof path, s1, ".state");
			bool ok = cmd[0] == 's' ? emu_state_save_file(e, path) : emu_state_load_file(e, path);
			printf("%s %s: %s\n", cmd, path, ok ? "ok" : "FAILED");
		} else if (!strcmp(cmd, "peek")) {
			unsigned addr = (unsigned) strtoul(s1, NULL, 16);
			int len = *s2 ? atoi(s2) : 1;
			printf("peek %06X:", addr);
			for (int i = 0; i < len; ++i) printf(" %02X", emu_read8(e, addr + i));
			printf("\n");
		} else if (!strcmp(cmd, "poke")) {
			unsigned addr = (unsigned) strtoul(s1, NULL, 16);
			unsigned v = (unsigned) strtoul(s3, NULL, 16);
			if (atoi(s2) == 2) emu_write16(e, addr, (uint16_t) v);
			else emu_write8(e, addr, (uint8_t) v);
		} else if (!strcmp(cmd, "dumpram")) {
			char path[1200];
			out_path(path, sizeof path, s1, ".ram");
			size_t n;
			uint8_t* w = emu_wram(e, &n);
			FILE* o = w ? fopen(path, "wb") : NULL;
			if (o) {
				fwrite(w, 1, n, o);
				fclose(o);
			}
			printf("dumpram %s: %s\n", path, o ? "ok" : "FAILED");
		} else if (!strcmp(cmd, "frame")) {
			printf("frame %lu\n", e->frames);
		} else if (!strcmp(cmd, "copy")) {
			size_t n = 0;
			double ms = copy_state(&inst[0], &inst[1], &n);
			printf("copy: %zu bytes, %.3f ms%s\n", n, ms, ms < 0 ? " FAILED" : "");
		} else if (!strcmp(cmd, "mapcap")) {
			mapcap(p + strlen("mapcap"));
		} else if (!strcmp(cmd, "bench")) {
			bench(*s1 ? atoi(s1) : 600);
		} else if (!strcmp(cmd, "keysfirst")) {
			keysFirst = atoi(s1) != 0;
		} else if (!strcmp(cmd, "pattern")) {
			patternMode = atoi(s1);
		} else if (!strcmp(cmd, "context")) {
			inst[0].stateContext = inst[1].stateContext = atoi(s1);
		} else {
			fprintf(stderr, "unknown command: %s\n", p);
		}
		fflush(stdout);
	}
	fclose(f);
	emu_close(&inst[1]);
	emu_close(&inst[0]);
	return 0;
}

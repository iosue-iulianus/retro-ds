/* RetroDS M0 harness: two instances of a libretro core, driven by a script.
 *
 *   retrods-harness CORE ROM OUTDIR SCRIPT
 *
 * ROM may be a PlayStation disc (.cue) with its core (SwanStation): then
 * only instance A runs, and RETRODS_BIOS_DIR names the BIOS folder.
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
 *   ff6 part               Final Fantasy VI (src/ff6.c): which part of the
 *                          game runs (field, world map, battle, menu, other)
 *   ff6 battle             its battle menu: open, menu, slot, cursor, commands
 *   ff6 tap ROW            FF6-3: a tap on the battle command list's row ROW
 *                          (0 to 3): the presses it makes, each played as a
 *                          tap (3 frames down, 9 up)
 *   ff6 menu NAME          FF6-5: the party screen RetroDS draws (src/ff6.c),
 *                          from A's RAM and the ROM: NAME.png, 256 x 224
 *   ff6 battlepic NAME     FF6-2: the battle screen RetroDS draws, NAME.png
 *   pokefile ADDR FILE [OFF LEN]  write FILE's bytes (LEN from OFF, else all)
 *                          from ADDR (Final Fantasy VI: a save slot's block
 *                          into $1600 for its party in the menu)
 *   dumpram NAME           OUTDIR/NAME.ram: the 128 KB of work RAM
 *   dumpvram NAME          OUTDIR/NAME.vram: the 64 KB of video RAM
 *   copy                   A's state into B (timed)
 *   mapcap NAME [opts]     copy A into B, press a button on B and run B
 *                          until the screen is up and settled; writes
 *                          NAME-a.png (A, untouched) and NAME-b.png (B).
 *                          opts (key=value): press=START hold=4
 *                          until=ADDR:VAL[:MASK] (up to 4, all must hold)
 *                          stable=8 maxdiff=64 max=600 blind=1 (frames
 *                          before `until` holds run without drawing)
 *                          verbose=1 (pixels changed, frame by frame)
 *   smmap NAME [VX VY|game|page]  Super Metroid's map as RetroDS draws it
 *                          (src/smmap.c), from A's RAM: NAME.png, 256 x
 *                          178, at the view given, the game's own ($B1,
 *                          $B3: its pause screen, the blinking as on its
 *                          picture) or where RetroDS rests it; or its
 *                          equipment page
 *   smtoggle CAT INDEX     Super Metroid: switch an item on or off as
 *                          the drawn equipment page does (src/smmap.c:
 *                          CAT 1 beams, 2 suit / misc., 3 boots)
 *   alttp on|off           A Link to the Past (src/alttp.c): read A's RAM
 *                          after every frame from now on (the map takes the
 *                          ground on screen as the app would)
 *   alttp items NAME       the items screen as the second screen shows it
 *   alttp map NAME ZOOM X Y W H   the world map Link is in, W x H shown
 *                          pixels from world pixel (X, Y) at ZOOM (1, 2, 4)
 *   alttp dungeon NAME [FLOOR]   the dungeon map Link is in (FLOOR at
 *                          the top, else as the game opens it)
 *   alttp piece NAME W I L   the map's piece I of world W at level L as
 *                          the app's GPU map draws it (NAME.png)
 *   alttp seen NAME W GX GY GW GH   the seen squares of world W, GW x GH
 *                          of them from square (GX, GY), a pixel each
 *                          (white: seen)
 *   alttp goals            the world map's markers shown now (ALTTP-11)
 *   alttp marker NAME      Link's marker on the map (32 x 32, its middle at
 *                          Link's place) and where it goes on the picture
 *   alttp select ITEM      select Y item ITEM (1 bow ... 20 mirror) as a
 *                          tap on the items screen would
 *   alttp offlist          a write off the game's list (the module's
 *                          $7E0010): emu_apply must refuse it, nothing written
 *   alttp take             take the ground on screen now, whatever the game
 *                          is doing
 *   alttp save PATH / alttp load PATH   the seen record
 *   alttp merge PATH       a saved record merged into the one held (as
 *                          loading a state with one does)
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
#include "../src/emu.h"
#include "../src/alttp.h"
#include "../src/smmap.h"
#include "../src/sotn.h"
#include "../src/ff6.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

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

/* ---- Super Metroid's native map ---- */

static SmMap* sm;
static bool sm_open(const char* romPath) {
	if (!sm) {
		size_t n = 0;
		uint8_t* rom = emu_rom_read_played(romPath, &n, NULL, NULL, NULL);
		sm = smmap_new(rom, n);
		if (!sm) fprintf(stderr, "smmap: not a ROM it knows\n");
	}
	return sm != NULL;
}

static void smtoggle_cmd(Emu* e, const char* romPath, int cat, int index) {
	size_t n;
	const uint8_t* w = emu_memory(e, EMU_MEM_WRAM, &n);
	if (!sm_open(romPath) || !w || n < 0x20000) return;
	Write out[128];
	int k = smmap_toggle(sm, w, cat, index, out, 128), nOk;
	const WriteRange* ok = smmap_write_ranges(&nOk);
	int done = k > 0 ? emu_apply(e, "smtoggle", out, k, ok, nOk) : 0;
	printf("smtoggle %d %d: %d byte(s) written%s\n", cat, index, done < 0 ? 0 : done, done < 0 ? " (REFUSED: off the list)" : "");
}

static void smmap_cmd(Emu* e, const char* romPath, const char* name, const char* a, const char* b) {
	if (!sm) {
		size_t n = 0;
		uint8_t* rom = emu_rom_read_played(romPath, &n, NULL, NULL, NULL);
		sm = smmap_new(rom, n);
		if (!sm) {
			fprintf(stderr, "smmap: not a ROM it knows\n");
			return;
		}
	}
	size_t n;
	const uint8_t* w = emu_memory(e, EMU_MEM_WRAM, &n);
	if (!w || n < 0x20000) return;
	smmap_update(sm, w);
	int vx, vy;
	if (!strcmp(a, "page")) {
		/* the equipment page, its pulsing as on the game's screen */
		smmap_set_blink(sm, -1, w[0x074F]);
		vx = vy = 0;
	} else if (!strcmp(a, "game")) {
		vx = (int16_t) (w[0xB1] | w[0xB2] << 8), vy = (int16_t) (w[0xB3] | w[0xB4] << 8);
		/* the marker as the picture has it: a frame late ($82:B9FC) */
		static const int seq[4] = { 0, 1, 2, 1 }, delay[4] = { 8, 4, 8, 4 };
		int f = (w[0x0776] >> 1) & 3;
		if ((w[0x0778] | w[0x0779] << 8) == delay[f] - 1) f = (f + 3) & 3;
		smmap_set_blink(sm, seq[f], w[0x074F]);
	} else if (*a) {
		vx = atoi(a), vy = atoi(b);
	} else {
		smmap_home(sm, &vx, &vy);
	}
	static uint32_t px[SMMAP_W * SMMAP_H];
	if (!strcmp(a, "page")) memcpy(px, smmap_page(sm), sizeof(px));
	else smmap_compose(sm, vx, vy, px);
	char path[1200];
	out_path(path, sizeof path, name, ".png");
	if (!write_png(path, px, SMMAP_W, SMMAP_H, SMMAP_W)) fprintf(stderr, "can't write %s\n", path);
	else printf("smmap %s (view %d, %d)\n", path, vx, vy);
}

/* ---- A Link to the Past (src/alttp.c) ---- */

static Alttp* alt;
static bool altTrack;

static bool alt_open(const char* romPath) {
	if (!alt) {
		size_t n = 0;
		uint8_t* rom = emu_rom_read_played(romPath, &n, NULL, NULL, NULL);
		alt = alttp_new(rom, n);
		if (!alt) fprintf(stderr, "alttp: not a ROM it knows\n");
	}
	return alt != NULL;
}

static void alt_update(Emu* e) {
	size_t n, vn, sn;
	const uint8_t* w = emu_memory(e, EMU_MEM_WRAM, &n);
	const uint8_t* v = emu_memory(e, EMU_MEM_VRAM, &vn);
	const uint8_t* s = emu_memory(e, EMU_MEM_SRAM, &sn);
	if (w && n >= 0x20000) alttp_update(alt, w, v && vn >= 0x10000 ? v : NULL, s && sn >= 0x2000 ? s : NULL);
}

/* One frame of A or B; A's RAM read after it while tracking */
static void frame(Emu* e, uint32_t keys) {
	emu_frame(e, keys);
	if (altTrack && alt && e == &inst[0]) alt_update(e);
}

static void alttp_cmd(Emu* e, const char* romPath, const char* line) {
	char sub[16] = "", name[512] = "";
	int zoom = 1, x = 0, y = 0, w = 256, h = 224;
	sscanf(line, "%*s %15s %511s %d %d %d %d %d", sub, name, &zoom, &x, &y, &w, &h);
	if (!alt_open(romPath)) return;
	char path[1200];
	if (!strcmp(sub, "on") || !strcmp(sub, "off")) {
		altTrack = sub[1] == 'n';
		if (altTrack) alt_update(e);
	} else if (!strcmp(sub, "items")) {
		alt_update(e);
		out_path(path, sizeof path, name, ".png");
		bool ok = write_png(path, alttp_items(alt), ALTTP_W, ALTTP_H, ALTTP_W);
		printf("alttp items %s: %s\n", path, ok ? "ok" : "FAILED");
	} else if (!strcmp(sub, "seen")) {
		/* (zoom, x, y, w, h: the world and the squares, a pixel each) */
		if (w < 1 || h < 1 || w > 256 || h > 256) return;
		uint32_t* px = malloc((size_t) w * h * 4);
		for (int gy = 0; px && gy < h; ++gy) {
			for (int gx = 0; gx < w; ++gx) px[gy * w + gx] = alttp_seen_at(alt, zoom, (x + gx) * 16, (y + gy) * 16) ? 0xFFFFFFFFu : 0xFF000000u;
		}
		out_path(path, sizeof path, name, ".png");
		bool ok = px && write_png(path, px, w, h, w);
		printf("alttp seen %s: %s\n", path, ok ? "ok" : "FAILED");
		free(px);
	} else if (!strcmp(sub, "piece")) {
		/* (zoom, x, y: the world, the piece, the level) */
		int n = ALTTP_PIECE >> (y < 0 ? 0 : y > 4 ? 4 : y);
		uint32_t* px = malloc((size_t) ALTTP_PIECE * ALTTP_PIECE * 4);
		double t0 = now_ms();
		if (px) alttp_piece(alt, zoom, x, y, px);
		double t1 = now_ms();
		out_path(path, sizeof path, name, ".png");
		bool ok = px && write_png(path, px, n, n, n);
		printf("alttp piece %s (version %u, %.1f ms): %s\n", path, alttp_piece_version(alt, zoom, x), t1 - t0, ok ? "ok" : "FAILED");
		free(px);
	} else if (!strcmp(sub, "goals")) {
		alt_update(e);
		const AltGoal* g;
		int n = alttp_goals(alt, &g);
		printf("alttp goals: %d\n", n);
		for (int i = 0; i < n; ++i) printf("  %d,%d size %d from %d,%d\n", g[i].x, g[i].y, g[i].size, g[i].dx, g[i].dy);
	} else if (!strcmp(sub, "marker")) {
		alt_update(e);
		size_t n;
		const uint8_t* wr = emu_memory(e, EMU_MEM_WRAM, &n);
		int world, lx, ly;
		alttp_link(alt, &world, &lx, &ly);
		out_path(path, sizeof path, name, ".png");
		bool ok = write_png(path, alttp_marker(alt), 32, 32, 32);
		/* (the marker's top left on the picture: Link's place less the camera) */
		int px = lx - 16 - (wr[0xE2] | wr[0xE3] << 8), py = ly - 16 - (wr[0xE8] | wr[0xE9] << 8) - 1;
		printf("alttp marker %s (on the picture at %d,%d): %s\n", path, px, py, ok ? "ok" : "FAILED");
	} else if (!strcmp(sub, "map")) {
		if (w < 1 || h < 1 || w > 4096 || h > 4096) return;
		uint32_t* px = malloc((size_t) w * h * 4);
		int world, lx, ly;
		alttp_link(alt, &world, &lx, &ly);
		alttp_compose(alt, world, zoom, x, y, w, h, px);
		out_path(path, sizeof path, name, ".png");
		bool ok = px && write_png(path, px, w, h, w);
		printf("alttp map %s (world %d, Link %d,%d): %s\n", path, world, lx, ly, ok ? "ok" : "FAILED");
		free(px);
	} else if (!strcmp(sub, "dungeon")) {
		size_t vn;
		alt_update(e);
		const uint8_t* v = emu_memory(e, EMU_MEM_VRAM, &vn);
		int floor = ALTTP_FLOOR_AS_GAME;
		sscanf(line, "%*s %*s %*s %d", &floor);
		const uint32_t* px = alttp_dungeon_map(alt, v, floor);
		out_path(path, sizeof path, name, ".png");
		bool ok = px && write_png(path, px, ALTTP_W, ALTTP_H, ALTTP_W);
		printf("alttp dungeon %s: %s\n", path, ok ? "ok" : px ? "FAILED" : "not in a dungeon");
	} else if (!strcmp(sub, "select")) {
		size_t n;
		alt_update(e);
		const uint8_t* wr = emu_memory(e, EMU_MEM_WRAM, &n);
		Write out[32];
		int k = alttp_select(alt, wr, atoi(name), out, 32), nOk;
		const WriteRange* ok = alttp_write_ranges(&nOk);
		int done = k > 0 ? emu_apply(e, "alttp select", out, k, ok, nOk) : 0;
		printf("alttp select %d: %d byte(s) written%s\n", atoi(name), done < 0 ? 0 : done, done < 0 ? " (REFUSED: off the list)" : "");
	} else if (!strcmp(sub, "offlist")) {
		size_t n;
		const uint8_t* wr = emu_memory(e, EMU_MEM_WRAM, &n);
		uint8_t before = wr ? wr[0x10] : 0;
		Write w[2] = { { 0x7E0202, 1 }, { 0x7E0010, (uint8_t) (before ^ 0xFF) } }; /* (one listed, one not) */
		int nOk;
		const WriteRange* ok = alttp_write_ranges(&nOk);
		uint8_t item = wr ? wr[0x202] : 0;
		int done = emu_apply(e, "offlist", w, 2, ok, nOk);
		printf("alttp offlist: %s; $7E0010 %s, $7E0202 %s\n", done < 0 ? "refused" : "APPLIED", wr && wr[0x10] == before ? "unchanged" : "CHANGED",
		       wr && wr[0x202] == item ? "unchanged" : "CHANGED");
	} else if (!strcmp(sub, "take")) {
		size_t n, vn;
		const uint8_t* wr = emu_memory(e, EMU_MEM_WRAM, &n);
		const uint8_t* v = emu_memory(e, EMU_MEM_VRAM, &vn);
		if (wr && v) alttp_capture_now(alt, wr, v);
	} else if (!strcmp(sub, "merge")) {
		FILE* f = fopen(name, "rb");
		uint8_t* d = NULL;
		long n = 0;
		if (f) {
			fseek(f, 0, SEEK_END), n = ftell(f), fseek(f, 0, SEEK_SET);
			d = n > 0 ? malloc((size_t) n) : NULL;
			if (d && fread(d, 1, (size_t) n, f) != (size_t) n) n = 0;
			fclose(f);
		}
		bool ok = d && n > 0 && alttp_seen_merge(alt, d, (size_t) n);
		free(d);
		printf("alttp merge %s: %s\n", name, ok ? "ok" : "FAILED");
	} else if (!strcmp(sub, "save") || !strcmp(sub, "load")) {
		double t0 = now_ms();
		bool ok = sub[0] == 's' ? alttp_save_seen(alt, name) : alttp_load_seen(alt, name);
		struct stat st;
		printf("alttp %s %s: %s (%.1f ms, %lld bytes)\n", sub, name, ok ? "ok" : "FAILED", now_ms() - t0,
		       stat(name, &st) == 0 ? (long long) st.st_size : -1LL);
	}
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
	if (!emu_state_save(from, stateBuf) || !emu_state_load(to, stateBuf)) return -1;
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
	uint8_t* wx = emu_memory(x, EMU_MEM_WRAM, &nx);
	uint8_t* wy = emu_memory(y, EMU_MEM_WRAM, &ny);
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
	printf("bench: ROM %s, %d frames per run, state context %d\n", a->gameCode, frames, a->stateContext);

	/* keep a starting point so every test starts from the same moment */
	size_t n = emu_state_size(a);
	uint8_t* start = malloc(n);
	uint8_t* tmp = malloc(n);
	uint8_t* tmp2 = malloc(n);
	emu_state_save(a, start);
	emu_state_load(b, start);

	double t0 = now_ms();
	for (int i = 0; i < frames; ++i) emu_frame(a, 0);
	double vis = (now_ms() - t0) / frames;
	emu_state_load(a, start);
	t0 = now_ms();
	for (int i = 0; i < frames; ++i) emu_frame_blind(a, 0);
	double blind = (now_ms() - t0) / frames;
	printf("  one instance: %.3f ms/frame drawn (%.0f fps), %.3f ms/frame blind (%.0f fps)\n", vis, 1000 / vis, blind, 1000 / blind);

	emu_state_load(a, start);
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
	emu_state_load(a, start);
	t0 = now_ms();
	size_t sz = 0;
	for (int i = 0; i < reps; ++i) sz = emu_state_size(a);
	double sizeMs = (now_ms() - t0) / reps;
	t0 = now_ms();
	for (int i = 0; i < reps; ++i) emu_state_save(a, tmp);
	double saveMs = (now_ms() - t0) / reps;
	t0 = now_ms();
	for (int i = 0; i < reps; ++i) emu_state_load(a, tmp);
	double loadMs = (now_ms() - t0) / reps;
	t0 = now_ms();
	for (int i = 0; i < reps; ++i) emu_state_load(b, tmp);
	double loadBMs = (now_ms() - t0) / reps;
	printf("  state: %zu bytes; serialize_size %.3f ms, serialize %.3f ms, unserialize %.3f ms (same instance), %.3f ms (into B)\n", sz, sizeMs, saveMs, loadMs, loadBMs);

	/* does serializing run the core? two saves in a row with no frame between */
	emu_state_load(a, start);
	emu_frame(a, 0);
	emu_state_save(a, tmp);
	emu_state_save(a, tmp2);
	size_t diffBytes = 0;
	for (size_t i = 0; i < n; ++i) diffBytes += tmp[i] != tmp2[i];
	printf("  two serializes in a row differ in %zu bytes (%s)\n", diffBytes, diffBytes ? "the core ran between them: serialize advances it" : "serialize doesn't run the core");

	/* does serializing the live game every second change where it goes?
	 * A: serialize every 60 frames; B: never; same start, same (no) input */
	emu_state_load(a, start);
	emu_state_load(b, start);
	for (int i = 0; i < 600; ++i) {
		if (i % 60 == 0) {
			if (keysFirst) emu_state_save_live(a, pattern(i), tmp);
			else emu_state_save(a, tmp);
		}
		emu_frame(a, pattern(i));
		emu_frame(b, pattern(i));
	}
	int wd, fd;
	bool same = same_machine(a, b, &wd, &fd);
	printf("  600 frames with input, A serialized every 60, B never: %s (WRAM bytes differing %d, pixels differing %d)\n", same ? "identical" : "DIFFERENT", wd, fd);

	/* is a state copied from A into B faithful? both run 120 frames after */
	emu_state_load(a, start);
	for (int i = 0; i < 30; ++i) emu_frame(a, pattern(i));
	copy_state(a, b, NULL);
	for (int i = 0; i < 240; ++i) {
		emu_frame(a, pattern(i));
		emu_frame(b, pattern(i));
	}
	same = same_machine(a, b, &wd, &fd);
	printf("  copied A into B, 240 frames with input on both: %s (WRAM bytes differing %d, pixels differing %d)\n", same ? "identical" : "DIFFERENT", wd, fd);

	emu_state_load(a, start);
	emu_state_load(b, start);
	free(start);
	free(tmp);
	free(tmp2);
}

/* ---- Symphony of the Night (src/sotn.c): the second screen as composed,
 * W x H of the game's pixels (384 x 216: the Duo's top screen at 5), with
 * the map cursor at its frame 40; the logo's place outlined */
static void sotn_cmd(Emu* e, const char* name, int w, int h) {
	static SotnView v;
	static uint32_t map[SOTN_PIC_W * SOTN_PIC_H];
	if (w <= 0) w = 384;
	if (h <= 0) h = 216;
	if (!sotn_read(e, &v)) {
		printf("sotn: no video RAM\n");
		return;
	}
	uint32_t* px = calloc((size_t) w * h, 4);
	SotnLayout l;
	sotn_compose(&v, px, w, h, &l);
	/* the map at rest, inside the window */
	sotn_map_pixels(&v, map);
	for (int y = 0; y < SOTN_PIC_H; ++y) {
		for (int x = 0; x < SOTN_PIC_W; ++x) {
			int X = l.mapX + x, Y = l.mapY + y;
			if (map[y * SOTN_PIC_W + x] && X >= l.winX && Y >= l.winY && X < l.winX + l.winW && Y < l.winY + l.winH) px[Y * w + X] = map[y * SOTN_PIC_W + x];
		}
	}
	/* the cursor, half and half as the game's */
	uint32_t cur[32 * 32];
	sotn_cursor_pixels(&v, cur);
	int cx, cy, cs;
	sotn_cursor_rect(&v, 40, &cx, &cy, &cs);
	for (int y = 0; y < cs; ++y) {
		for (int x = 0; x < cs; ++x) {
			int X = l.mapX + cx + x, Y = l.mapY + cy + y;
			uint32_t c = cur[(y * 32 / cs) * 32 + x * 32 / cs];
			if (!c || X < 0 || Y < 0 || X >= w || Y >= h) continue;
			uint32_t d = px[Y * w + X], o = 0xFF000000u;
			for (int k = 0; k < 24; k += 8) o |= ((((c >> k) & 255) + ((d >> k) & 255)) / 2) << k;
			px[Y * w + X] = o;
		}
	}
	/* the pane over it, the logo's place outlined */
	if (l.paneW) {
		uint32_t* pane = calloc((size_t) l.paneW * l.paneH, 4);
		sotn_pane_pixels(&v, &l, pane);
		for (int y = 0; y < l.paneH; ++y) memcpy(px + (size_t) (l.paneY + y) * w + l.paneX, pane + (size_t) y * l.paneW, (size_t) l.paneW * 4);
		free(pane);
	}
	for (int x = (int) l.logoX; x < (int) (l.logoX + l.logoW); ++x) px[(int) l.logoY * w + x] = px[(int) (l.logoY + l.logoH) * w + x] = 0xFF00FFFFu;
	char path[1200];
	out_path(path, sizeof path, name, ".png");
	write_png(path, px, w, h, w);
	printf("sotn %s: state %d, rooms %d, %s castle, square %d,%d, window %d,%d,%d, logo %.0fx%.0f at %.0f,%.0f\n", path, v.state, v.rooms,
	       v.inverted ? "inverted" : "first", v.sqX, v.sqY, v.window[0], v.window[1], v.window[2], l.logoW, l.logoH, l.logoX, l.logoY);
	free(px);
}

/* ---- script ---- */

int main(int argc, char** argv) {
	if (argc != 5) {
		fprintf(stderr, "usage: retrods-harness CORE ROM OUTDIR SCRIPT\n");
		return 2;
	}
	const char* core = argv[1];
	const char* rom = argv[2];
	outDir = argv[3];
	mkdir(outDir, 0755);

	/* the core copied to OUTDIR/core-a.so and core-b.so: one copy per instance.
	 * RETRODS_SAVE: a battery save for instance A, copied to OUTDIR/game.srm
	 * first (the game writes to its save: the original is never touched) */
	const System* sys = system_for_path(rom);
	if (!sys) {
		fprintf(stderr, "%s isn't a game RetroDS plays\n", rom);
		return 2;
	}
	if (!emu_set_core_copies(sys->id, core, outDir)) return 1;
	/* RETRODS_BIOS_DIR: the folder with the player's BIOS files
	 * (PlayStation: scph5501.bin) */
	if (getenv("RETRODS_BIOS_DIR")) emu_set_bios_dir(getenv("RETRODS_BIOS_DIR"));
	char save[1200] = "";
	if (getenv("RETRODS_SAVE") && getenv("RETRODS_SAVE")[0]) {
		snprintf(save, sizeof save, "%s/game.srm", outDir);
		size_t n = 0;
		FILE* in = fopen(getenv("RETRODS_SAVE"), "rb");
		FILE* out = in ? fopen(save, "wb") : NULL;
		char buf[1 << 16];
		while (in && out && (n = fread(buf, 1, sizeof buf, in)) > 0) fwrite(buf, 1, n, out);
		if (in) fclose(in);
		if (out) fclose(out);
		if (!in || !out) {
			fprintf(stderr, "can't copy the save %s to %s\n", getenv("RETRODS_SAVE"), save);
			return 1;
		}
	}
	/* (one core copy for a system that has one: instance B stays closed) */
	for (int i = 0; i < 2 && i < sys->coreCopies; ++i) {
		if (!emu_open(&inst[i], rom, i == 0 && save[0] ? save : NULL, false)) return 1;
	}
	size_t wn = 0, sn = 0;
	emu_memory(&inst[0], EMU_MEM_WRAM, &wn);
	emu_memory(&inst[0], EMU_MEM_SRAM, &sn);
	printf("ROM %s (as played %08X), %dx%d at %.3f fps, work RAM %zu bytes, save RAM %zu bytes, state %zu bytes\n",
	       inst[0].gameCode, inst[0].romCrc, inst[0].w, inst[0].h, inst[0].fps, wn, sn, emu_state_size(&inst[0]));

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
			for (int i = atoi(s1); i > 0; --i) frame(e, 0);
		} else if (!strcmp(cmd, "hold")) {
			uint32_t k = parse_keys(s1);
			for (int i = atoi(s2); i > 0; --i) frame(e, k);
		} else if (!strcmp(cmd, "tap")) {
			uint32_t k = parse_keys(s1);
			int count = *s2 ? atoi(s2) : 1;
			for (int c = 0; c < count; ++c) {
				for (int i = 0; i < 3; ++i) frame(e, k);
				for (int i = 0; i < 9; ++i) frame(e, 0);
			}
		} else if (!strcmp(cmd, "ff6")) {
			Ff6Battle b;
			ff6_battle(e, &b);
			if (!strcmp(s1, "part")) {
				printf("ff6 part: %s\n", ff6_part_name(ff6_part(e)));
			} else if (!strcmp(s1, "battle")) {
				printf("ff6 battle: open %d, menu %02X, slot %d, cursor %d, commands %d%s %d%s %d%s %d%s%s\n", b.open, b.menu, b.slot, b.cursor,
				       b.cmd[0], b.disabled[0] ? "(grey)" : "", b.cmd[1], b.disabled[1] ? "(grey)" : "", b.cmd[2], b.disabled[2] ? "(grey)" : "",
				       b.cmd[3], b.disabled[3] ? "(grey)" : "", b.shortSet ? ", short" : "");
			} else if (!strcmp(s1, "menu")) {
				static Ff6Rom* from;
				if (!from) from = ff6_rom_load(rom);
				Ff6Party party;
				ff6_party_read(e, &party);
				static uint32_t pic[FF6_PIC_W * FF6_PIC_H];
				ff6_party_draw(from, &party, pic);
				char path[1200];
				out_path(path, sizeof path, s2, ".png");
				printf("ff6 menu %s: %s; style %d;", path, write_png(path, pic, FF6_PIC_W, FF6_PIC_H, FF6_PIC_W) ? "ok" : "FAILED", party.style);
				for (int k = 0; k < 4; ++k)
					if (party.m[k].here)
						printf(" [%d] char %d lv %d hp %d/%d mp %d/%d%s%s", k, party.m[k].id, party.m[k].level, party.m[k].hp, party.m[k].hpMax, party.m[k].mp,
						       party.m[k].mpMax, party.m[k].showMp ? "" : " (no MP)", party.m[k].backRow ? " back" : "");
				printf("\n");
			} else if (!strcmp(s1, "battlepic")) {
				static Ff6Rom* from;
				if (!from) from = ff6_rom_load(rom);
				Ff6BattleView v;
				ff6_battle_view(e, &v);
				static uint32_t pic[FF6_BATTLE_W * FF6_BATTLE_H];
				ff6_battle_draw(from, &v, pic);
				char path[1200];
				out_path(path, sizeof path, s2, ".png");
				printf("ff6 battlepic %s: %s\n", path, write_png(path, pic, FF6_BATTLE_W, FF6_BATTLE_H, FF6_BATTLE_W) ? "ok" : "FAILED");
			} else if (!strcmp(s1, "tap")) {
				uint32_t keys[8];
				int n = ff6_command_presses(&b, atoi(s2), keys);
				printf("ff6 tap %d: %d presses:", atoi(s2), n);
				for (int k = 0; k < n; ++k) printf(" %s", keys[k] == K_A ? "A" : keys[k] == K_DOWN ? "DOWN" : "?");
				printf("\n");
				for (int k = 0; k < n; ++k) {
					for (int i = 0; i < 3; ++i) frame(e, keys[k]);
					for (int i = 0; i < 9; ++i) frame(e, 0);
				}
			} else if (!strcmp(s1, "aim")) {
				/* FF6-3: aim the target cursor at s2 (0-3 the party, 4-9 a
				 * monster slot; or "m" + a monster number, as a tap on its
				 * name), a press at a time as the play session does */
				int want = s2[0] == 'm' ? ff6_group_target(&b, (int) strtol(s2 + 1, NULL, 16)) : atoi(s2);
				Ff6Aim a;
				ff6_aim_start(&a, want);
				printf("ff6 aim %d: party %X monsters %02X alive %02X;", want, b.partyTarget, b.monTarget, b.monAlive);
				uint32_t k;
				while ((k = ff6_aim_next(&a, &b)) != 0) {
					printf(" %s", k == K_A ? "A" : k == K_LEFT ? "LEFT" : k == K_RIGHT ? "RIGHT" : k == K_UP ? "UP" : "DOWN");
					for (int i = 0; i < 3; ++i) frame(e, k);
					for (int i = 0; i < 9; ++i) frame(e, 0);
					if (k == K_A) break;
					ff6_battle(e, &b);
					printf(" (%X %02X)", b.partyTarget, b.monTarget);
				}
				printf("\n");
			}
		} else if (!strcmp(cmd, "alttp")) {
			alttp_cmd(e, rom, p);
		} else if (!strcmp(cmd, "sotnwide")) {
			/* SOTN-7: the picture widened to 16:9 */
			int ow = *s3 ? atoi(s3) : e->w * 4 / 3;
			uint32_t* out = calloc((size_t) ow * e->h, 4);
			int pox = 0;
			bool ok = sotn_widen(e->fb, e->w, e->h, out, ow, &pox);
			char path[1200];
			out_path(path, sizeof path, s1, ".png");
			if (ok) write_png(path, out, ow, e->h, ow);
			printf("sotnwide %s: %s, %dx%d -> %d wide, engine %d, menu step %X\n", path, ok ? "widened" : "no window", e->w, e->h, ow, sotn_engine(e), sotn_menu_step(e));
			free(out);
		} else if (!strcmp(cmd, "sotn")) {
			sotn_cmd(e, s1, atoi(s2), atoi(s3));
		} else if (!strcmp(cmd, "smtoggle")) {
			smtoggle_cmd(e, rom, atoi(s1), atoi(s2));
		} else if (!strcmp(cmd, "smmap")) {
			smmap_cmd(e, rom, s1, s2, s3);
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
		} else if (!strcmp(cmd, "pokefile")) {
			unsigned addr = (unsigned) strtoul(s1, NULL, 16);
			FILE* in = fopen(s2, "rb");
			long off = s3[0] ? strtol(s3, NULL, 0) : 0, len = -1, n = 0;
			char lenArg[32] = "";
			if (sscanf(line, "%*s %*s %*s %*s %31s", lenArg) == 1) len = strtol(lenArg, NULL, 0);
			int c;
			if (in && off) fseek(in, off, SEEK_SET);
			while (in && (len < 0 || n < len) && (c = fgetc(in)) != EOF) emu_write8(e, addr + (unsigned) n++, (uint8_t) c);
			if (in) fclose(in);
			printf("pokefile %s: %ld bytes at %06X\n", s2, n, addr);
		} else if (!strcmp(cmd, "dumpram") || !strcmp(cmd, "dumpvram")) {
			bool video = cmd[4] == 'v';
			char path[1200];
			out_path(path, sizeof path, s1, video ? ".vram" : ".ram");
			size_t n;
			uint8_t* w = emu_memory(e, video ? EMU_MEM_VRAM : EMU_MEM_WRAM, &n);
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

/* SNES DS's emulator: a libretro core loaded from a .so (see emu.h). */
#include "emu.h"
#include "libretro.h"
#include "patch.h"

#include <ctype.h>
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

/* ------------------------------------------------------------ the cores */

typedef struct EmuCore {
	void* handle;
	int slot; /* which core copy */
	char dataDir[1200]; /* the core's own system and save folder */
	unsigned pixelFormat;
	uint8_t* rom; /* the ROM as played, kept while the game is loaded */
	size_t romSize;

	void (*set_environment)(retro_environment_t);
	void (*set_video_refresh)(retro_video_refresh_t);
	void (*set_audio_sample)(retro_audio_sample_t);
	void (*set_audio_sample_batch)(retro_audio_sample_batch_t);
	void (*set_input_poll)(retro_input_poll_t);
	void (*set_input_state)(retro_input_state_t);
	void (*init)(void);
	void (*deinit)(void);
	unsigned (*api_version)(void);
	void (*get_system_info)(struct retro_system_info*);
	void (*get_system_av_info)(struct retro_system_av_info*);
	void (*set_controller_port_device)(unsigned, unsigned);
	void (*reset)(void);
	void (*run)(void);
	size_t (*serialize_size)(void);
	bool (*serialize)(void*, size_t);
	bool (*unserialize)(const void*, size_t);
	void (*cheat_reset)(void);
	void (*cheat_set)(unsigned, bool, const char*);
	bool (*load_game)(const struct retro_game_info*);
	void (*unload_game)(void);
	void* (*get_memory_data)(unsigned);
	size_t (*get_memory_size)(unsigned);
	char name[64];
	unsigned cheats; /* codes given to the core */
} EmuCore;

static pthread_mutex_t coresLock = PTHREAD_MUTEX_INITIALIZER;
static char corePaths[EMU_CORES][1024];
static int nCorePaths;
static bool coreBusy[EMU_CORES];
static char cacheDir[1024];

/* libretro's callbacks carry no context: each call into a core says which
 * Emu it's for, per thread (an Emu runs on one thread at a time). */
static __thread Emu* cur;
#define ENTER(e) Emu* prevCur = cur; cur = (e)
#define LEAVE() cur = prevCur

static bool debugLog(void) {
	static int on = -1;
	if (on < 0) on = getenv("RETRODS_DEBUG") != NULL;
	return on;
}

void emu_set_cores(const char* const* paths, int n, const char* cache) {
	pthread_mutex_lock(&coresLock);
	nCorePaths = 0;
	for (int i = 0; i < n && i < EMU_CORES; ++i) snprintf(corePaths[nCorePaths++], sizeof(corePaths[0]), "%s", paths[i]);
	snprintf(cacheDir, sizeof(cacheDir), "%s", cache ? cache : "");
	if (cacheDir[0]) mkdir(cacheDir, 0755);
	pthread_mutex_unlock(&coresLock);
}

static bool copy_if_changed(const char* from, const char* to) {
	struct stat a, b;
	if (stat(from, &a)) return false;
	if (!stat(to, &b) && b.st_size == a.st_size && b.st_mtime >= a.st_mtime) return true;
	FILE* in = fopen(from, "rb");
	char tmp[1100];
	snprintf(tmp, sizeof(tmp), "%s.tmp", to);
	FILE* out = in ? fopen(tmp, "wb") : NULL;
	bool ok = in && out;
	char buf[1 << 16];
	size_t k;
	while (ok && (k = fread(buf, 1, sizeof(buf), in)) > 0) ok = fwrite(buf, 1, k, out) == k;
	if (in) fclose(in);
	if (out && fclose(out)) ok = false;
	return ok && rename(tmp, to) == 0;
}

bool emu_set_core_copies(const char* core, const char* cache) {
	mkdir(cache, 0755);
	char paths[EMU_CORES][1200];
	const char* list[EMU_CORES];
	for (int i = 0; i < EMU_CORES; ++i) {
		snprintf(paths[i], sizeof(paths[i]), "%s/core-%c.so", cache, 'a' + i);
		if (!copy_if_changed(core, paths[i])) {
			fprintf(stderr, "emu: can't copy the core %s to %s\n", core, paths[i]);
			return false;
		}
		list[i] = paths[i];
	}
	emu_set_cores(list, EMU_CORES, cache);
	return true;
}

/* ------------------------------------------------------------ files and ROMs */

static uint8_t* read_file(const char* path, size_t* n) {
	struct stat st;
	*n = 0;
	if (stat(path, &st) || st.st_size <= 0 || st.st_size > 64 * 1024 * 1024) return NULL;
	FILE* f = fopen(path, "rb");
	uint8_t* p = f ? malloc((size_t) st.st_size) : NULL;
	if (p && fread(p, 1, (size_t) st.st_size, f) == (size_t) st.st_size) *n = (size_t) st.st_size;
	else {
		free(p);
		p = NULL;
	}
	if (f) fclose(f);
	return p;
}

/* written beside, then put in its place: a file is never half written */
static bool write_whole(const char* path, const void* data, size_t n) {
	char tmp[1100];
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	FILE* f = fopen(tmp, "wb");
	bool ok = f && fwrite(data, 1, n, f) == n;
	if (f && fclose(f)) ok = false;
	if (ok && rename(tmp, path) == 0) return true;
	remove(tmp);
	return false;
}

static bool has_ext(const char* name, const char* const* exts) {
	const char* dot = strrchr(name, '.');
	if (!dot) return false;
	for (int i = 0; exts[i]; ++i) {
		if (!strcasecmp(dot + 1, exts[i])) return true;
	}
	return false;
}

static const char* const romExts[] = { "sfc", "smc", "swc", "fig", NULL };

bool emu_is_archive_name(const char* name) {
	static const char* const exts[] = { "zip", NULL };
	return has_ext(name, exts);
}

bool emu_is_rom_name(const char* name) {
	return has_ext(name, romExts) || emu_is_archive_name(name);
}

static uint16_t le16(const uint8_t* p) {
	return (uint16_t) (p[0] | p[1] << 8);
}
static uint32_t le32(const uint8_t* p) {
	return (uint32_t) p[0] | (uint32_t) p[1] << 8 | (uint32_t) p[2] << 16 | (uint32_t) p[3] << 24;
}
static void put_le32(uint8_t* p, uint32_t v) {
	for (int i = 0; i < 4; ++i) p[i] = (uint8_t) (v >> (8 * i));
}

/* The first SNES ROM inside a .zip (stored or deflated), malloc'd. */
static uint8_t* zip_first_rom(const uint8_t* z, size_t zn, size_t* outN) {
	*outN = 0;
	if (zn < 22) return NULL;
	size_t eocd = zn - 22;
	while (eocd > 0 && le32(z + eocd) != 0x06054b50) --eocd; /* (a comment may follow it) */
	if (le32(z + eocd) != 0x06054b50) return NULL;
	unsigned entries = le16(z + eocd + 10);
	size_t at = le32(z + eocd + 16);
	for (unsigned i = 0; i < entries && at + 46 <= zn && le32(z + at) == 0x02014b50; ++i) {
		unsigned method = le16(z + at + 10);
		size_t csize = le32(z + at + 20), usize = le32(z + at + 24);
		unsigned nameN = le16(z + at + 28), extraN = le16(z + at + 30), commentN = le16(z + at + 32);
		size_t local = le32(z + at + 42);
		char name[260];
		snprintf(name, sizeof(name), "%.*s", (int) (nameN < 259 ? nameN : 259), (const char*) z + at + 46);
		at += 46 + nameN + extraN + commentN;
		if (!has_ext(name, romExts) || usize == 0 || usize > 32u * 1024 * 1024) continue;
		if (local + 30 > zn || le32(z + local) != 0x04034b50) return NULL;
		size_t data = local + 30 + le16(z + local + 26) + le16(z + local + 28);
		if (data + csize > zn) return NULL;
		uint8_t* out = malloc(usize);
		if (!out) return NULL;
		if (method == 0 && csize == usize) {
			memcpy(out, z + data, usize);
		} else if (method == 8) {
			z_stream s = { 0 };
			s.next_in = (Bytef*) (z + data), s.avail_in = (uInt) csize;
			s.next_out = out, s.avail_out = (uInt) usize;
			bool ok = inflateInit2(&s, -MAX_WBITS) == Z_OK && inflate(&s, Z_FINISH) == Z_STREAM_END && s.total_out == usize;
			inflateEnd(&s);
			if (!ok) {
				free(out);
				return NULL;
			}
		} else {
			free(out);
			fprintf(stderr, "emu: %s in the zip is compressed in a way SNES DS can't read (method %u)\n", name, method);
			return NULL;
		}
		*outN = usize;
		return out;
	}
	return NULL;
}

uint8_t* emu_rom_read_all(const char* path, size_t* size) {
	size_t n = 0;
	uint8_t* b = read_file(path, &n);
	if (b && emu_is_archive_name(path)) {
		size_t rn = 0;
		uint8_t* rom = zip_first_rom(b, n, &rn);
		free(b);
		b = rom, n = rn;
	}
	if (b && n % 1024 == 512 && n > 512) { /* a copier header */
		memmove(b, b + 512, n - 512);
		n -= 512;
	}
	*size = b ? n : 0;
	return b;
}

bool emu_rom_code(const char* path, char code[9]) {
	size_t n = 0;
	uint8_t* b = emu_rom_read_all(path, &n);
	if (!b) return false;
	snprintf(code, 9, "%08X", emu_crc32(0, b, n));
	free(b);
	return true;
}

/* The internal header: LoROM at $7FC0, HiROM at $FFC0 (ExHiROM at $40FFC0),
 * the one whose checksum and complement add up, else whose title prints. */
bool emu_rom_title(const char* path, char* out, size_t n) {
	size_t rn = 0;
	uint8_t* b = emu_rom_read_all(path, &rn);
	if (!b) return false;
	static const size_t bases[] = { 0x7FC0, 0xFFC0, 0x40FFC0 };
	const uint8_t* h = NULL;
	for (int pass = 0; pass < 2 && !h; ++pass) {
		for (size_t i = 0; i < 3 && !h; ++i) {
			if (bases[i] + 32 > rn) continue;
			const uint8_t* c = b + bases[i];
			bool sum = (uint16_t) (le16(c + 28) + le16(c + 30)) == 0xFFFF;
			bool printable = true;
			for (int k = 0; k < 21; ++k) printable = printable && c[k] >= 0x20 && c[k] < 0x7F;
			if (pass == 0 ? sum && printable : printable) h = c;
		}
	}
	bool ok = false;
	if (h) {
		int len = 21;
		while (len > 0 && h[len - 1] == ' ') --len;
		snprintf(out, n, "%.*s", len, (const char*) h);
		ok = len > 0;
	}
	free(b);
	return ok;
}

/* ---- patches (GEN-9): planned per ROM, or the patch file beside it */
#define PLAN_MAX 8
static pthread_mutex_t patchedLock = PTHREAD_MUTEX_INITIALIZER;
static struct {
	char rom[1024];
	char files[PLAN_MAX][1024];
	int n;
} plan;
/* The last patched ROM, so it's patched once: keyed by the ROM's and every
 * patch's paths, sizes and times. */
static struct {
	char key[PLAN_MAX * 1100 + 1100];
	int result;
	char names[256];
	char skipped[256]; /* (PATCH_PARTIAL) the ones left out */
	uint32_t fileCrc;
	uint8_t* data; /* the patched ROM (NULL unless something was applied) */
	size_t size;
} patched;

void emu_patch_plan(const char* rom, char files[][1024], int n) {
	pthread_mutex_lock(&patchedLock);
	snprintf(plan.rom, sizeof(plan.rom), "%s", rom ? rom : "");
	plan.n = 0;
	for (int i = 0; i < n && i < PLAN_MAX; ++i) snprintf(plan.files[plan.n++], sizeof(plan.files[0]), "%s", files[i]);
	pthread_mutex_unlock(&patchedLock);
}

/* The patched ROM for romPath with these patches, or NULL: *result says
 * why (PATCH_NONE: no patch). Several patches apply in order; one that
 * can't be applied is left out (the result says so) and the rest go on.
 * Returns a copy the caller frees. */
static uint8_t* patched_with(const char* romPath, char files[][1024], int n, size_t* size, int* result, char* patchName,
                             uint32_t* fileCrc, char* skipped) {
	*result = PATCH_NONE;
	if (n > PLAN_MAX) n = PLAN_MAX;
	if (!n) return NULL;
	char key[sizeof(patched.key)];
	struct stat st;
	int k = snprintf(key, sizeof(key), "%s|%lld|%lld", romPath, stat(romPath, &st) ? -1LL : (long long) st.st_size,
	                 stat(romPath, &st) ? -1LL : (long long) st.st_mtime);
	for (int i = 0; i < n && k < (int) sizeof(key); ++i) {
		bool ok = stat(files[i], &st) == 0;
		k += snprintf(key + k, sizeof(key) - (size_t) k, "|%s|%lld|%lld", files[i], ok ? (long long) st.st_size : -1LL,
		              ok ? (long long) st.st_mtime : -1LL);
	}
	pthread_mutex_lock(&patchedLock);
	if (strcmp(patched.key, key)) {
		free(patched.data);
		memset(&patched, 0, sizeof(patched));
		snprintf(patched.key, sizeof(patched.key), "%s", key);
		patched.result = PATCH_UNREADABLE;
		size_t romN = 0;
		uint8_t* rom = emu_rom_read_all(romPath, &romN);
		int applied = 0, failed = PATCH_NONE;
		if (rom) {
			patched.fileCrc = emu_crc32(0, rom, romN);
			for (int i = 0; i < n; ++i) {
				size_t pn = 0;
				uint8_t* p = read_file(files[i], &pn);
				uint8_t* out = NULL;
				size_t outN = 0;
				int r = p ? patch_apply(p, pn, rom, romN, &out, &outN) : PATCH_UNREADABLE;
				free(p);
				fprintf(stderr, "patch %s: %s\n", patch_name(files[i]),
				        r == PATCH_APPLIED     ? "applied"
				        : r == PATCH_WRONG_ROM ? "made for another ROM, not applied"
				                               : "can't be read, not applied");
				if (r == PATCH_APPLIED) {
					free(rom);
					rom = out;
					romN = outN;
					size_t l = strlen(patched.names);
					snprintf(patched.names + l, sizeof(patched.names) - l, "%s%s", l ? " + " : "", patch_name(files[i]));
					++applied;
				} else {
					if (failed == PATCH_NONE) failed = r;
					size_t l = strlen(patched.skipped);
					snprintf(patched.skipped + l, sizeof(patched.skipped) - l, "%s%s", l ? " + " : "", patch_name(files[i]));
				}
			}
			/* (none applied: why the first wasn't, under its name) */
			patched.result = failed == PATCH_NONE ? PATCH_APPLIED : applied ? PATCH_PARTIAL : failed;
			if (!applied) snprintf(patched.names, sizeof(patched.names), "%s", patched.skipped);
			if (applied) {
				patched.data = rom;
				patched.size = romN;
				rom = NULL;
			}
		}
		free(rom);
	}
	*result = patched.result;
	if (fileCrc) *fileCrc = patched.fileCrc;
	if (patchName) snprintf(patchName, 256, "%s", patched.names);
	if (skipped) snprintf(skipped, 256, "%s", patched.skipped);
	uint8_t* copy = NULL;
	if (patched.data && (copy = malloc(patched.size))) {
		memcpy(copy, patched.data, patched.size);
		*size = patched.size;
	}
	pthread_mutex_unlock(&patchedLock);
	return copy;
}

/* (with the planned patches, else the one beside the ROM) */
static uint8_t* patched_rom(const char* romPath, size_t* size, int* result, char* patchName, uint32_t* fileCrc, char* skipped) {
	char files[PLAN_MAX][1024];
	int n = 0;
	bool planned = false;
	pthread_mutex_lock(&patchedLock);
	if (plan.rom[0] && !strcmp(plan.rom, romPath)) {
		planned = true;
		for (int i = 0; i < plan.n; ++i) memcpy(files[n++], plan.files[i], sizeof(files[0]));
	}
	pthread_mutex_unlock(&patchedLock);
	if (!planned && patch_find(romPath, files[0], sizeof(files[0]))) n = 1; /* (beside the ROM) */
	return patched_with(romPath, files, n, size, result, patchName, fileCrc, skipped);
}

uint8_t* emu_rom_read_patched(const char* path, char files[][1024], int n, size_t* size, int* patch, char* patchName) {
	int r;
	uint8_t* p = patched_with(path, files, n, size, &r, patchName, NULL, NULL);
	if (patch) *patch = r;
	return p ? p : emu_rom_read_all(path, size);
}

uint8_t* emu_rom_read_played(const char* path, size_t* size, int* patch, char* patchName, uint32_t* fileCrc) {
	int r;
	uint8_t* p = patched_rom(path, size, &r, patchName, fileCrc, NULL);
	if (patch) *patch = r;
	if (p) return p;
	uint8_t* rom = emu_rom_read_all(path, size);
	if (rom && fileCrc) *fileCrc = emu_crc32(0, rom, *size);
	return rom;
}

/* ------------------------------------------------------------ callbacks */

static void log_cb(enum retro_log_level level, const char* fmt, ...) {
	if (level < RETRO_LOG_WARN && !debugLog()) return;
	Emu* e = cur;
	fprintf(stderr, "[%s] ", e && e->core ? e->core->name : "core");
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}

static bool env_cb(unsigned cmd, void* data) {
	Emu* e = cur;
	if (!e || !e->core) return false;
	switch (cmd) {
	case RETRO_ENVIRONMENT_GET_CAN_DUPE:
		if (data) *(bool*) data = true;
		return true;
	case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
		unsigned fmt = *(const enum retro_pixel_format*) data;
		if (fmt != RETRO_PIXEL_FORMAT_0RGB1555 && fmt != RETRO_PIXEL_FORMAT_RGB565 && fmt != RETRO_PIXEL_FORMAT_XRGB8888) return false;
		e->core->pixelFormat = fmt;
		return true;
	}
	case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
	case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
		/* (its own folder: the core's own save files never meet the player's) */
		*(const char**) data = e->core->dataDir;
		return true;
	case RETRO_ENVIRONMENT_GET_VARIABLE:
		((struct retro_variable*) data)->value = NULL; /* (the core's defaults) */
		return false;
	case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
		*(bool*) data = false;
		return true;
	case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
		*(unsigned*) data = 0;
		return true;
	case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
		((struct retro_log_callback*) data)->log = log_cb;
		return true;
	case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
		return true;
	case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE:
		/* bit 0 video, bit 1 audio, bit 3 audio may be skipped outright (a
		 * copy whose state is thrown away: no audio wanted) */
		*(int*) data = (e->blind ? 0 : 1) | (e->audio ? 2 : 8);
		return true;
	case RETRO_ENVIRONMENT_GET_SAVESTATE_CONTEXT:
		*(int*) data = e->stateContext;
		return true;
	case RETRO_ENVIRONMENT_GET_FASTFORWARDING:
		*(bool*) data = false;
		return true;
	case RETRO_ENVIRONMENT_GET_LANGUAGE:
		*(unsigned*) data = RETRO_LANGUAGE_ENGLISH;
		return true;
	case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO: {
		const struct retro_system_av_info* av = data;
		e->fps = av->timing.fps;
		e->sampleRate = av->timing.sample_rate;
		return true;
	}
	case RETRO_ENVIRONMENT_SET_MESSAGE:
		if (debugLog()) fprintf(stderr, "[%s] message: %s\n", e->core->name, ((const struct retro_message*) data)->msg);
		return true;
	/* accepted and not needed */
	case RETRO_ENVIRONMENT_SET_VARIABLES:
	case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
	case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
	case RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO:
	case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
	case RETRO_ENVIRONMENT_SET_GEOMETRY:
	case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
	case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
	case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:
	case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
	case RETRO_ENVIRONMENT_SET_CONTENT_INFO_OVERRIDE:
		return true;
	default:
		return false;
	}
}

static void video_cb(const void* data, unsigned w, unsigned h, size_t pitch) {
	Emu* e = cur;
	if (!e || !data || e->blind) return; /* NULL: the same picture again */
	if (w > EMU_MAX_W) w = EMU_MAX_W;
	if (h > EMU_MAX_H) h = EMU_MAX_H;
	e->w = (int) w;
	e->h = (int) h;
	unsigned fmt = e->core->pixelFormat;
	for (unsigned y = 0; y < h; ++y) {
		const uint8_t* row = (const uint8_t*) data + y * pitch;
		uint32_t* out = e->fb + (size_t) y * w;
		for (unsigned x = 0; x < w; ++x) {
			unsigned r, g, b;
			if (fmt == RETRO_PIXEL_FORMAT_XRGB8888) {
				uint32_t c = ((const uint32_t*) row)[x];
				r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
			} else if (fmt == RETRO_PIXEL_FORMAT_RGB565) {
				uint16_t c = ((const uint16_t*) row)[x];
				r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
				r = (r << 3) | (r >> 2), g = (g << 2) | (g >> 4), b = (b << 3) | (b >> 2);
			} else {
				uint16_t c = ((const uint16_t*) row)[x];
				r = (c >> 10) & 31, g = (c >> 5) & 31, b = c & 31;
				r = (r << 3) | (r >> 2), g = (g << 3) | (g >> 2), b = (b << 3) | (b >> 2);
			}
			out[x] = 0xFF000000u | b << 16 | g << 8 | r;
		}
	}
}

#define AUDIO_RING (1 << 15) /* stereo frames */

static size_t audio_push(Emu* e, const int16_t* data, size_t frames) {
	if (!e || !e->audio || !e->ring) return frames;
	for (size_t i = 0; i < frames; ++i) {
		size_t next = (e->ringHead + 1) % AUDIO_RING;
		if (next == e->ringTail) break; /* full: the rest is dropped */
		e->ring[e->ringHead * 2] = data[i * 2];
		e->ring[e->ringHead * 2 + 1] = data[i * 2 + 1];
		e->ringHead = next;
	}
	return frames;
}

static void audio_sample_cb(int16_t left, int16_t right) {
	int16_t s[2] = { left, right };
	audio_push(cur, s, 1);
}

static size_t audio_batch_cb(const int16_t* data, size_t frames) {
	return audio_push(cur, data, frames);
}

static void input_poll_cb(void) {}

static int16_t input_state_cb(unsigned port, unsigned device, unsigned index, unsigned id) {
	Emu* e = cur;
	if (!e || port != 0 || (device & RETRO_DEVICE_MASK) != RETRO_DEVICE_JOYPAD) return 0;
	if (id == RETRO_DEVICE_ID_JOYPAD_MASK) return (int16_t) (e->keys & K_ALL);
	return id < 16 ? (e->keys >> id) & 1 : 0;
}

/* ------------------------------------------------------------ battery saves
 * The cartridge's RAM, loaded from savePath as the game opens and written
 * back (on a thread of its own) whenever it has changed, checked every
 * half second, and as the game closes. */

typedef struct EmuSave {
	char path[1024];
	uint8_t* last; /* as last written */
	size_t size;
	unsigned since;
} EmuSave;

static pthread_mutex_t saveLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t saveCond = PTHREAD_COND_INITIALIZER;
static bool saveStarted;
static struct {
	char path[1024];
	uint8_t* data;
	size_t size;
} saveJob; /* the next file to write (a newer one replaces an older for the same path) */

static void* save_worker(void* arg) {
	for (;;) {
		pthread_mutex_lock(&saveLock);
		while (!saveJob.data) pthread_cond_wait(&saveCond, &saveLock);
		char path[1024];
		snprintf(path, sizeof(path), "%s", saveJob.path);
		uint8_t* data = saveJob.data;
		size_t size = saveJob.size;
		saveJob.data = NULL;
		pthread_mutex_unlock(&saveLock);
		if (!write_whole(path, data, size)) fprintf(stderr, "emu: can't write the save %s: %s\n", path, strerror(errno));
		free(data);
	}
	return NULL;
}

static void save_queue(const char* path, const uint8_t* data, size_t size) {
	uint8_t* copy = malloc(size);
	if (!copy) return;
	memcpy(copy, data, size);
	pthread_mutex_lock(&saveLock);
	if (!saveStarted) {
		pthread_t t;
		saveStarted = pthread_create(&t, NULL, save_worker, NULL) == 0;
		if (saveStarted) pthread_detach(t);
	}
	if (saveJob.data && strcmp(saveJob.path, path)) {
		/* (another game's still waiting: written here, now) */
		write_whole(saveJob.path, saveJob.data, saveJob.size);
		free(saveJob.data);
	} else {
		free(saveJob.data);
	}
	snprintf(saveJob.path, sizeof(saveJob.path), "%s", path);
	saveJob.data = copy, saveJob.size = size;
	pthread_cond_signal(&saveCond);
	pthread_mutex_unlock(&saveLock);
}

static void save_open(Emu* e, const char* savePath) {
	size_t size = 0;
	uint8_t* sram = emu_memory(e, EMU_MEM_SRAM, &size);
	if (!savePath || !sram || !size) return;
	EmuSave* s = calloc(1, sizeof(*s));
	if (!s || !(s->last = malloc(size))) {
		free(s);
		return;
	}
	snprintf(s->path, sizeof(s->path), "%s", savePath);
	s->size = size;
	size_t n = 0;
	uint8_t* b = read_file(savePath, &n);
	if (b) {
		memcpy(sram, b, n < size ? n : size);
		if (debugLog()) fprintf(stderr, "emu: save %s, %zu bytes (the game's %zu)\n", savePath, n, size);
	}
	free(b);
	memcpy(s->last, sram, size);
	e->save = s;
}

static void save_check(Emu* e, bool now) {
	EmuSave* s = e->save;
	if (!s || (!now && ++s->since < 30)) return;
	s->since = 0;
	size_t size = 0;
	uint8_t* sram = emu_memory(e, EMU_MEM_SRAM, &size);
	if (!sram || size != s->size || !memcmp(sram, s->last, size)) return;
	memcpy(s->last, sram, size);
	if (now) {
		if (!write_whole(s->path, sram, size)) fprintf(stderr, "emu: can't write the save %s\n", s->path);
	} else {
		save_queue(s->path, sram, size);
	}
}

/* ------------------------------------------------------------ opening and closing */

#define LOAD_SYM(c, name)                                                     \
	do {                                                                      \
		*(void**) &(c)->name = dlsym((c)->handle, "retro_" #name);            \
		if (!(c)->name) {                                                     \
			fprintf(stderr, "emu: the core has no retro_" #name "\n");        \
			goto fail;                                                        \
		}                                                                     \
	} while (0)

/* The ROM as played, in the cache folder (by its CRC32), for cores that
 * load from a path; a .ips beside the original isn't seen there twice. */
static bool cached_rom(const uint8_t* rom, size_t n, uint32_t crc, char* out, size_t outN) {
	if (!cacheDir[0]) return false;
	snprintf(out, outN, "%s/rom-%08X.sfc", cacheDir, crc);
	struct stat st;
	if (!stat(out, &st) && (size_t) st.st_size == n) return true;
	return write_whole(out, rom, n);
}

bool emu_open(Emu* e, const char* romPath, const char* savePath, bool audio) {
	memset(e, 0, sizeof(*e));
	e->audio = audio;

	pthread_mutex_lock(&coresLock);
	int slot = -1;
	for (int i = 0; i < nCorePaths && slot < 0; ++i) {
		if (!coreBusy[i]) slot = i, coreBusy[i] = true;
	}
	char corePath[1024];
	snprintf(corePath, sizeof(corePath), "%s", slot >= 0 ? corePaths[slot] : "");
	pthread_mutex_unlock(&coresLock);
	if (slot < 0) {
		fprintf(stderr, "emu: no free core copy (%d in use)\n", nCorePaths);
		return false;
	}

	EmuCore* c = calloc(1, sizeof(*c));
	if (!c) goto fail_slot;
	c->slot = slot;
	c->pixelFormat = RETRO_PIXEL_FORMAT_0RGB1555; /* libretro's default */
	snprintf(c->dataDir, sizeof(c->dataDir), "%s/core-%c", cacheDir[0] ? cacheDir : "/tmp", 'a' + slot);
	mkdir(c->dataDir, 0755);
	c->handle = dlopen(corePath, RTLD_NOW | RTLD_LOCAL);
	if (!c->handle) {
		fprintf(stderr, "emu: can't load the core %s: %s\n", corePath, dlerror());
		free(c);
		goto fail_slot;
	}
	e->core = c;

	LOAD_SYM(c, set_environment);
	LOAD_SYM(c, set_video_refresh);
	LOAD_SYM(c, set_audio_sample);
	LOAD_SYM(c, set_audio_sample_batch);
	LOAD_SYM(c, set_input_poll);
	LOAD_SYM(c, set_input_state);
	LOAD_SYM(c, init);
	LOAD_SYM(c, deinit);
	LOAD_SYM(c, api_version);
	LOAD_SYM(c, get_system_info);
	LOAD_SYM(c, get_system_av_info);
	LOAD_SYM(c, set_controller_port_device);
	LOAD_SYM(c, reset);
	LOAD_SYM(c, run);
	LOAD_SYM(c, serialize_size);
	LOAD_SYM(c, serialize);
	LOAD_SYM(c, unserialize);
	LOAD_SYM(c, cheat_reset);
	LOAD_SYM(c, cheat_set);
	LOAD_SYM(c, load_game);
	LOAD_SYM(c, unload_game);
	LOAD_SYM(c, get_memory_data);
	LOAD_SYM(c, get_memory_size);
	if (c->api_version() != RETRO_API_VERSION) {
		fprintf(stderr, "emu: the core has libretro API %u, not %u\n", c->api_version(), RETRO_API_VERSION);
		goto fail;
	}

	/* the ROM as played */
	size_t romN = 0;
	char patchName[256] = "";
	c->rom = emu_rom_read_played(romPath, &romN, &e->patch, patchName, &e->fileCrc);
	if (!c->rom) {
		fprintf(stderr, "emu: can't read %s\n", romPath);
		goto fail;
	}
	c->romSize = romN;
	e->romCrc = emu_crc32(0, c->rom, romN);
	snprintf(e->gameCode, sizeof(e->gameCode), "%08X", e->fileCrc);
	snprintf(e->patchName, sizeof(e->patchName), "%s", patchName);
	if (e->patch == PATCH_PARTIAL) {
		pthread_mutex_lock(&patchedLock);
		snprintf(e->patchSkipped, sizeof(e->patchSkipped), "%s", patched.skipped);
		pthread_mutex_unlock(&patchedLock);
	}
	char played[1200];
	if (!cached_rom(c->rom, romN, e->romCrc, played, sizeof(played))) snprintf(played, sizeof(played), "%s", romPath);

	e->fb = calloc((size_t) EMU_MAX_W * EMU_MAX_H, sizeof(*e->fb));
	if (audio) e->ring = calloc(AUDIO_RING * 2, sizeof(*e->ring));
	if (!e->fb || (audio && !e->ring)) goto fail;

	{
		ENTER(e);
		struct retro_system_info info = { 0 };
		c->get_system_info(&info);
		snprintf(c->name, sizeof(c->name), "%s", info.library_name ? info.library_name : "core");
		c->set_environment(env_cb);
		c->init();
		c->set_video_refresh(video_cb);
		c->set_audio_sample(audio_sample_cb);
		c->set_audio_sample_batch(audio_batch_cb);
		c->set_input_poll(input_poll_cb);
		c->set_input_state(input_state_cb);
		struct retro_game_info game = { .path = played, .data = c->rom, .size = romN };
		bool loaded = c->load_game(&game);
		if (loaded) {
			struct retro_system_av_info av = { 0 };
			c->get_system_av_info(&av);
			e->fps = av.timing.fps;
			e->sampleRate = av.timing.sample_rate;
			e->w = (int) av.geometry.base_width;
			e->h = (int) av.geometry.base_height;
			if (e->w > 256) e->w = 256, e->h = 224; /* (bsnes's base is its largest: 512 x 448) */
			c->set_controller_port_device(0, RETRO_DEVICE_JOYPAD);
			e->stateSize = c->serialize_size();
		} else {
			c->deinit();
		}
		LEAVE();
		if (!loaded) {
			fprintf(stderr, "emu: the core couldn't load %s\n", romPath);
			goto fail;
		}
	}
	save_open(e, savePath);
	if (debugLog()) fprintf(stderr, "emu: %s on core copy %c (%s), ROM %s, state %zu bytes\n", romPath, 'a' + slot, c->name, e->gameCode, e->stateSize);
	return true;

fail:
	if (c->handle) dlclose(c->handle);
	free(c->rom);
	free(c);
	free(e->fb);
	free(e->ring);
	e->core = NULL, e->fb = NULL, e->ring = NULL;
fail_slot:
	pthread_mutex_lock(&coresLock);
	coreBusy[slot] = false;
	pthread_mutex_unlock(&coresLock);
	return false;
}

void emu_close(Emu* e) {
	EmuCore* c = e->core;
	if (!c) return;
	save_check(e, true);
	{
		ENTER(e);
		c->unload_game();
		c->deinit();
		LEAVE();
	}
	dlclose(c->handle);
	pthread_mutex_lock(&coresLock);
	coreBusy[c->slot] = false;
	pthread_mutex_unlock(&coresLock);
	if (e->save) free(e->save->last);
	free(e->save);
	free(c->rom);
	free(c);
	free(e->fb);
	free(e->ring);
	e->core = NULL, e->fb = NULL, e->ring = NULL, e->save = NULL;
}

/* ------------------------------------------------------------ running */

bool emu_cancelled(const Emu* e) {
	return e->cancel && *e->cancel;
}

static void run_frame(Emu* e, uint32_t keys, bool blind) {
	if (!e->core || emu_cancelled(e)) return;
	ENTER(e);
	e->keys = keys & K_ALL;
	e->blind = blind;
	e->core->run();
	e->blind = false;
	LEAVE();
	++e->frames;
	save_check(e, false);
}

void emu_frame(Emu* e, uint32_t keys) {
	run_frame(e, keys, false);
}

void emu_frame_blind(Emu* e, uint32_t keys) {
	run_frame(e, keys, true);
}

void emu_reset(Emu* e) {
	if (!e->core) return;
	ENTER(e);
	e->core->reset();
	LEAVE();
}

/* ------------------------------------------------------------ states */

size_t emu_state_size(Emu* e) {
	return e->stateSize;
}

static bool serialize_in(Emu* e, int context, void* buf, size_t size) {
	ENTER(e);
	int was = e->stateContext;
	e->stateContext = context;
	bool ok = e->core->serialize(buf, size);
	e->stateContext = was;
	LEAVE();
	return ok;
}

bool emu_state_save(Emu* e, void* buf) {
	return e->core && serialize_in(e, RETRO_SAVESTATE_CONTEXT_NORMAL, buf, e->stateSize);
}

bool emu_state_save_live(Emu* e, uint32_t nextKeys, void* buf) {
	if (!e->core) return false;
	e->keys = nextKeys & K_ALL;
	return emu_state_save(e, buf);
}

static bool unserialize_in(Emu* e, const void* buf, size_t size) {
	ENTER(e);
	bool ok = e->core->unserialize(buf, size);
	LEAVE();
	return ok;
}

bool emu_state_load(Emu* e, const void* buf) {
	return e->core && unserialize_in(e, buf, e->stateSize);
}

/* ---- rewind: the newest state whole (`now`), and before it a ring of the
 * differences going back (each the XOR of a state and the one after it,
 * zero runs shortened), within a memory budget. States for this Emu only
 * (the same-instance context: taken without disturbing the game). */

#define REWIND_BUDGET (96u * 1024 * 1024)
static struct {
	Emu* owner;
	size_t size; /* a same-instance state's size */
	uint8_t* now;
	bool haveNow;
	uint8_t* scratch;
	uint8_t** deltas; /* ring, oldest at first */
	size_t* deltaN;
	size_t cap, first, count, bytes;
} rw;

void emu_rewind_deinit(void) {
	for (size_t i = 0; i < rw.count; ++i) free(rw.deltas[(rw.first + i) % rw.cap]);
	free(rw.deltas);
	free(rw.deltaN);
	free(rw.now);
	free(rw.scratch);
	memset(&rw, 0, sizeof(rw));
}

void emu_rewind_init(Emu* e, size_t entries) {
	emu_rewind_deinit();
	if (!e->core || !entries) return;
	ENTER(e);
	e->stateContext = RETRO_SAVESTATE_CONTEXT_RUNAHEAD_SAME_INSTANCE;
	rw.size = e->core->serialize_size();
	e->stateContext = RETRO_SAVESTATE_CONTEXT_NORMAL;
	LEAVE();
	rw.cap = entries;
	rw.deltas = calloc(entries, sizeof(*rw.deltas));
	rw.deltaN = calloc(entries, sizeof(*rw.deltaN));
	rw.now = malloc(rw.size);
	rw.scratch = malloc(rw.size);
	if (!rw.deltas || !rw.deltaN || !rw.now || !rw.scratch) {
		emu_rewind_deinit();
		return;
	}
	rw.owner = e;
}

/* XOR of a and b, zero runs written as (0, count): any other byte as it is */
static uint8_t* delta_pack(const uint8_t* a, const uint8_t* b, size_t n, size_t* outN) {
	uint8_t* out = malloc(n + n / 64 + 16);
	if (!out) return NULL;
	size_t o = 0;
	for (size_t i = 0; i < n;) {
		uint8_t x = a[i] ^ b[i];
		if (x) {
			out[o++] = x;
			++i;
			continue;
		}
		size_t run = 0;
		while (i < n && !(a[i] ^ b[i]) && run < 0xFFFFFF) ++i, ++run;
		out[o++] = 0;
		out[o++] = (uint8_t) run, out[o++] = (uint8_t) (run >> 8), out[o++] = (uint8_t) (run >> 16);
	}
	*outN = o;
	return realloc(out, o ? o : 1);
}

/* dst ^= the difference */
static void delta_apply(uint8_t* dst, size_t n, const uint8_t* d, size_t dn) {
	size_t i = 0;
	for (size_t o = 0; o < dn && i < n;) {
		uint8_t x = d[o++];
		if (x) {
			dst[i++] ^= x;
		} else if (o + 3 <= dn) {
			i += (size_t) d[o] | (size_t) d[o + 1] << 8 | (size_t) d[o + 2] << 16;
			o += 3;
		}
	}
}

static void rewind_drop_oldest(void) {
	free(rw.deltas[rw.first]);
	rw.bytes -= rw.deltaN[rw.first];
	rw.deltas[rw.first] = NULL;
	rw.first = (rw.first + 1) % rw.cap;
	--rw.count;
}

void emu_rewind_append(Emu* e) {
	if (rw.owner != e || !e->core) return;
	if (!serialize_in(e, RETRO_SAVESTATE_CONTEXT_RUNAHEAD_SAME_INSTANCE, rw.scratch, rw.size)) return;
	if (rw.haveNow) {
		size_t dn = 0;
		uint8_t* d = delta_pack(rw.now, rw.scratch, rw.size, &dn);
		if (d) {
			if (rw.count == rw.cap) rewind_drop_oldest();
			while (rw.count && rw.bytes + dn > REWIND_BUDGET) rewind_drop_oldest();
			size_t at = (rw.first + rw.count) % rw.cap;
			rw.deltas[at] = d, rw.deltaN[at] = dn;
			rw.bytes += dn;
			++rw.count;
		}
	}
	memcpy(rw.now, rw.scratch, rw.size);
	rw.haveNow = true;
}

bool emu_rewind_step(Emu* e) {
	if (rw.owner != e || !e->core || !rw.count) return false;
	size_t at = (rw.first + rw.count - 1) % rw.cap;
	delta_apply(rw.now, rw.size, rw.deltas[at], rw.deltaN[at]);
	free(rw.deltas[at]);
	rw.bytes -= rw.deltaN[at];
	rw.deltas[at] = NULL;
	--rw.count;
	ENTER(e);
	e->stateContext = RETRO_SAVESTATE_CONTEXT_RUNAHEAD_SAME_INSTANCE;
	bool ok = e->core->unserialize(rw.now, rw.size);
	e->stateContext = RETRO_SAVESTATE_CONTEXT_NORMAL;
	LEAVE();
	return ok;
}

size_t emu_rewind_count(void) {
	return rw.count;
}

/* ---- state files: RetroArch's format, "RASTATE" and version 1, then
 * blocks (a 4-character tag, a u32 size, the data, zeros to a multiple of
 * 8) up to "END ". Compressed by RetroArch as "#RZIPv" 1 "#", u32 chunk
 * size, u64 size, then each chunk as a u32 size and zlib's data. */

static const uint8_t RZIP_MAGIC[8] = { '#', 'R', 'Z', 'I', 'P', 'v', 1, '#' };
#define RZIP_CHUNK (128 * 1024) /* (RetroArch's) */
#define STATE_FILE_MAX (64 * 1024 * 1024)

typedef struct {
	uint8_t* b; /* the whole file, unwrapped */
	size_t n;
	bool compressed;
	const uint8_t* mem; size_t memN;
	const uint8_t* extra; size_t extraN;
	bool haveCrc; uint32_t crc;
} StateFile;

static bool state_file_read(const char* path, StateFile* sf, StateFileKind* kind) {
	memset(sf, 0, sizeof(*sf));
	*kind = STATE_FILE_NONE;
	size_t n = 0;
	uint8_t* b = read_file(path, &n);
	if (!b) return false;
	*kind = STATE_FILE_UNREADABLE;
	if (n >= 20 && !memcmp(b, RZIP_MAGIC, 8)) {
		uint32_t chunk = le32(b + 8);
		uint64_t total = (uint64_t) le32(b + 12) | (uint64_t) le32(b + 16) << 32;
		uint8_t* out = chunk && total && total <= STATE_FILE_MAX ? malloc((size_t) total) : NULL;
		size_t got = 0, at = 20;
		while (out && got < total) {
			uLongf dl = (uLongf) (total - got < chunk ? total - got : chunk);
			uint32_t zn = at + 4 <= n ? le32(b + at) : 0;
			if (!zn || zn > n - at - 4 || uncompress(out + got, &dl, b + at + 4, zn) != Z_OK || !dl) {
				free(out);
				out = NULL;
				break;
			}
			got += dl, at += 4 + zn;
		}
		free(b);
		if (!out) return false;
		b = out, n = got;
		sf->compressed = true;
	}
	sf->b = b, sf->n = n;
	if (n >= 8 && !memcmp(b, "RASTATE", 7) && b[7] == 1) {
		size_t at = 8;
		while (at + 8 <= n) {
			uint32_t sz = le32(b + at + 4);
			if (!memcmp(b + at, "END ", 4) || sz > n - at - 8) break;
			const uint8_t* d = b + at + 8;
			if (!memcmp(b + at, "MEM ", 4)) sf->mem = d, sf->memN = sz;
			else if (!memcmp(b + at, "SDSX", 4)) sf->extra = d, sf->extraN = sz;
			else if (!memcmp(b + at, "SDSC", 4) && sz >= 4) sf->haveCrc = true, sf->crc = le32(d);
			at += 8 + (((size_t) sz + 7) & ~(size_t) 7);
		}
	} else {
		/* (a core's bytes alone, as RetroArch wrote them before RASTATE) */
		sf->mem = b, sf->memN = n;
	}
	if (!sf->mem || !sf->memN) {
		free(sf->b);
		sf->b = NULL;
		return false;
	}
	*kind = sf->haveCrc && !sf->compressed ? STATE_FILE_OWN : STATE_FILE_RETROARCH;
	return true;
}

static void block(uint8_t** p, size_t* n, size_t* cap, const char* tag, const void* data, size_t size) {
	size_t need = *n + 8 + ((size + 7) & ~(size_t) 7);
	if (need > *cap) {
		*cap = need * 2;
		*p = realloc(*p, *cap);
	}
	memcpy(*p + *n, tag, 4);
	put_le32(*p + *n + 4, (uint32_t) size);
	if (size) memcpy(*p + *n + 8, data, size);
	memset(*p + *n + 8 + size, 0, ((size + 7) & ~(size_t) 7) - size);
	*n = need;
}

bool emu_state_save_file_as(Emu* e, const char* path, const void* extra, size_t extraN, StateFormat fmt) {
	if (!e->core) return false;
	uint8_t* mem = malloc(e->stateSize);
	if (!mem || !emu_state_save(e, mem)) {
		free(mem);
		return false;
	}
	size_t cap = e->stateSize + extraN + 256, n = 8;
	uint8_t* p = malloc(cap);
	memcpy(p, "RASTATE\x01", 8);
	block(&p, &n, &cap, "MEM ", mem, e->stateSize);
	uint8_t crc[4];
	put_le32(crc, e->romCrc);
	block(&p, &n, &cap, "SDSC", crc, 4);
	if (extra && extraN) block(&p, &n, &cap, "SDSX", extra, extraN);
	block(&p, &n, &cap, "END ", NULL, 0);
	free(mem);
	bool ok;
	if (fmt == STATE_FORMAT_RETROARCH_RZIP) {
		size_t zcap = 20 + (n / RZIP_CHUNK + 1) * (compressBound(RZIP_CHUNK) + 4);
		uint8_t* z = malloc(zcap);
		size_t zn = 20;
		ok = z != NULL;
		if (ok) {
			memcpy(z, RZIP_MAGIC, 8);
			put_le32(z + 8, RZIP_CHUNK);
			put_le32(z + 12, (uint32_t) n), put_le32(z + 16, (uint32_t) ((uint64_t) n >> 32));
			for (size_t at = 0; ok && at < n; at += RZIP_CHUNK) {
				uLongf dl = (uLongf) (zcap - zn - 4);
				ok = compress2(z + zn + 4, &dl, p + at, (uLong) (n - at < RZIP_CHUNK ? n - at : RZIP_CHUNK), 6) == Z_OK;
				put_le32(z + zn, (uint32_t) dl);
				zn += 4 + dl;
			}
			ok = ok && write_whole(path, z, zn);
		}
		free(z);
	} else {
		ok = write_whole(path, p, n);
	}
	free(p);
	return ok;
}

bool emu_state_save_file(Emu* e, const char* path) {
	return emu_state_save_file_as(e, path, NULL, 0, STATE_FORMAT_OWN);
}

bool emu_state_load_file(Emu* e, const char* path) {
	StateFile sf;
	StateFileKind kind;
	if (!e->core || !state_file_read(path, &sf, &kind)) return false;
	bool ok = sf.memN == e->stateSize && unserialize_in(e, sf.mem, sf.memN);
	if (!ok) fprintf(stderr, "emu: the core didn't take the state %s (%zu bytes, its states %zu)\n", path, sf.memN, e->stateSize);
	fprintf(stderr, "load state: %s: %s (%s)\n", path, ok ? "loaded" : "not loaded",
	        kind == STATE_FILE_OWN ? "SNES DS's" : sf.compressed ? "RetroArch's, compressed" : "RetroArch's");
	free(sf.b);
	return ok;
}

StateFileKind emu_state_file_kind(Emu* e, const char* path, bool* fromRetrods) {
	StateFile sf;
	StateFileKind kind;
	if (fromRetrods) *fromRetrods = false;
	if (!state_file_read(path, &sf, &kind)) return kind;
	if (fromRetrods) *fromRetrods = sf.haveCrc;
	if (e && e->core && sf.memN != e->stateSize) kind = STATE_FILE_OTHER_CORE;
	free(sf.b);
	return kind;
}

bool emu_state_file_rom_crc(const char* path, uint32_t* crc) {
	StateFile sf;
	StateFileKind kind;
	if (!state_file_read(path, &sf, &kind)) return false;
	bool ok = sf.haveCrc;
	if (ok) *crc = sf.crc;
	free(sf.b);
	return ok;
}

void* emu_state_file_get_extra(Emu* e, const char* path, size_t* n) {
	StateFile sf;
	StateFileKind kind;
	*n = 0;
	if (!state_file_read(path, &sf, &kind)) return NULL;
	void* out = NULL;
	if (sf.extra && sf.extraN && (out = malloc(sf.extraN))) {
		memcpy(out, sf.extra, sf.extraN);
		*n = sf.extraN;
	}
	free(sf.b);
	return out;
}

/* ------------------------------------------------------------ memory */

uint8_t* emu_memory(Emu* e, int region, size_t* size) {
	if (size) *size = 0;
	if (!e->core) return NULL;
	unsigned id = region == EMU_MEM_SRAM ? RETRO_MEMORY_SAVE_RAM
		: region == EMU_MEM_VRAM ? RETRO_MEMORY_VIDEO_RAM : RETRO_MEMORY_SYSTEM_RAM;
	ENTER(e);
	uint8_t* p = e->core->get_memory_data(id);
	size_t n = e->core->get_memory_size(id);
	LEAVE();
	if (size) *size = p ? n : 0;
	return p;
}

/* index into work RAM for an SNES address, or -1 */
static long wram_index(uint32_t addr) {
	uint32_t bank = (addr >> 16) & 0xFF, off = addr & 0xFFFF;
	if (bank == 0x7E || bank == 0x7F) return (long) ((addr & 0xFFFFFF) - 0x7E0000);
	if ((bank < 0x40 || (bank >= 0x80 && bank < 0xC0)) && off < 0x2000) return (long) off;
	return -1;
}

uint8_t emu_read8(Emu* e, uint32_t addr) {
	size_t n;
	uint8_t* w = emu_memory(e, EMU_MEM_WRAM, &n);
	long i = wram_index(addr);
	return w && i >= 0 && (size_t) i < n ? w[i] : 0;
}

uint16_t emu_read16(Emu* e, uint32_t addr) {
	return (uint16_t) (emu_read8(e, addr) | emu_read8(e, addr + 1) << 8);
}

uint32_t emu_read24(Emu* e, uint32_t addr) {
	return (uint32_t) emu_read16(e, addr) | (uint32_t) emu_read8(e, addr + 2) << 16;
}

uint32_t emu_read32(Emu* e, uint32_t addr) {
	return (uint32_t) emu_read16(e, addr) | (uint32_t) emu_read16(e, addr + 2) << 16;
}

void emu_write8(Emu* e, uint32_t addr, uint8_t value) {
	size_t n;
	uint8_t* w = emu_memory(e, EMU_MEM_WRAM, &n);
	long i = wram_index(addr);
	if (w && i >= 0 && (size_t) i < n) w[i] = value;
}

int emu_apply(Emu* e, const char* what, const Write* w, int n, const WriteRange* ok, int nOk) {
	size_t size;
	uint8_t* ram = emu_memory(e, EMU_MEM_WRAM, &size);
	if (!ram || n < 0) return -1;
	for (int i = 0; i < n; ++i) {
		long at = wram_index(w[i].addr);
		bool listed = false;
		for (int k = 0; k < nOk && !listed; ++k) listed = w[i].addr >= ok[k].addr && w[i].addr - ok[k].addr < ok[k].size;
		if (at < 0 || (size_t) at >= size || !listed) {
			fprintf(stderr, "write %s: refused, $%06X isn't %s; nothing written\n", what, (unsigned) w[i].addr,
			        listed ? "work RAM" : "on the game's list");
			return -1;
		}
	}
	char line[2048];
	int len = snprintf(line, sizeof line, "write %s:", what);
	for (int i = 0; i < n; ++i) {
		long at = wram_index(w[i].addr);
		if (len < (int) sizeof line - 24) len += snprintf(line + len, sizeof line - (size_t) len, " $%06X %02X>%02X", (unsigned) w[i].addr, ram[at], w[i].value);
		ram[at] = w[i].value;
	}
	fprintf(stderr, "%s%s\n", line, len >= (int) sizeof line - 24 ? " ..." : "");
	return n;
}

void emu_write16(Emu* e, uint32_t addr, uint16_t value) {
	emu_write8(e, addr, (uint8_t) value);
	emu_write8(e, addr + 1, (uint8_t) (value >> 8));
}

/* ------------------------------------------------------------ audio */

size_t emu_audio_read(Emu* e, int16_t* out, size_t maxFrames) {
	size_t n = 0;
	while (e->ring && n < maxFrames && e->ringTail != e->ringHead) {
		out[n * 2] = e->ring[e->ringTail * 2];
		out[n * 2 + 1] = e->ring[e->ringTail * 2 + 1];
		e->ringTail = (e->ringTail + 1) % AUDIO_RING;
		++n;
	}
	return n;
}

unsigned emu_audio_rate(Emu* e) {
	return (unsigned) (e->sampleRate + 0.5);
}

/* ------------------------------------------------------------ cheats */

void emu_cheats_clear(Emu* e) {
	if (!e->core) return;
	ENTER(e);
	e->core->cheat_reset();
	LEAVE();
	e->core->cheats = 0;
}

int emu_cheats_add(Emu* e, const char* name, const char* const* codes, int n) {
	if (!e->core || n <= 0) return 0;
	size_t len = 1;
	for (int i = 0; i < n; ++i) len += strlen(codes[i]) + 1;
	char* joined = calloc(1, len);
	if (!joined) return 0;
	for (int i = 0; i < n; ++i) {
		if (i) strcat(joined, "+");
		strcat(joined, codes[i]);
	}
	ENTER(e);
	e->core->cheat_set(e->core->cheats++, true, joined);
	LEAVE();
	if (debugLog()) fprintf(stderr, "cheat %s: %s\n", name ? name : "", joined);
	free(joined);
	return n;
}

/* ------------------------------------------------------------ CRC32 */

static uint32_t crcTable[256];
static pthread_once_t crcOnce = PTHREAD_ONCE_INIT;

static void crc_table_init(void) {
	for (uint32_t i = 0; i < 256; ++i) {
		uint32_t c = i;
		for (int k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
		crcTable[i] = c;
	}
}

uint32_t emu_crc32(uint32_t crc, const void* data, size_t n) {
	pthread_once(&crcOnce, crc_table_init);
	const uint8_t* p = data;
	crc = ~crc;
	while (n--) crc = crcTable[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
	return ~crc;
}

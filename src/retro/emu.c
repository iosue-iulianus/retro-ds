/* SNES DS: libretro core loader (see emu.h). */
#include "emu.h"
#include "libretro.h"

#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* ---- the loaded core ---- */

typedef struct EmuCore {
	void* handle;
	char path[1024];
	char dataDir[1024];
	unsigned pixelFormat; /* enum retro_pixel_format */
	uint8_t* rom; /* the ROM file's bytes, kept while the game is loaded */
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
	bool (*load_game)(const struct retro_game_info*);
	void (*unload_game)(void);
	void* (*get_memory_data)(unsigned);
	size_t (*get_memory_size)(unsigned);
} EmuCore;

/* Libraries in use by an open Emu: a second dlopen of the same file returns
 * the same handle, and with it the same globals. */
#define MAX_OPEN 8
static void* openHandles[MAX_OPEN];
static pthread_mutex_t openLock = PTHREAD_MUTEX_INITIALIZER;

/* libretro's callbacks carry no context: each call into a core says which
 * Emu it's for, per thread (an Emu runs on one thread at a time). */
static __thread Emu* cur;

static bool debugLog(void) {
	static int on = -1;
	if (on < 0) on = getenv("SNESDS_DEBUG") != NULL;
	return on;
}

/* ---- callbacks ---- */

static void log_cb(enum retro_log_level level, const char* fmt, ...) {
	if (level < RETRO_LOG_WARN && !debugLog()) return;
	Emu* e = cur;
	fprintf(stderr, "[%s] ", e ? e->coreName : "core");
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}

static const char* option_value(const Emu* e, const char* key) {
	for (int i = 0; i < e->nOptions; ++i) {
		if (!strcmp(e->options[i].key, key)) return e->options[i].value;
	}
	return NULL;
}

static bool env_cb(unsigned cmd, void* data) {
	Emu* e = cur;
	if (!e) return false;
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
		*(const char**) data = e->core->dataDir;
		return true;
	case RETRO_ENVIRONMENT_GET_VARIABLE: {
		struct retro_variable* v = data;
		v->value = option_value(e, v->key);
		return v->value != NULL;
	}
	case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
		*(bool*) data = false;
		return true;
	case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
		*(unsigned*) data = 0; /* legacy SET_VARIABLES: we only read values */
		return true;
	case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
		((struct retro_log_callback*) data)->log = log_cb;
		return true;
	case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
		return true;
	case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE: {
		/* bit 0 video, bit 1 audio, bit 3 audio may be skipped outright
		 * (only for a copy whose state is thrown away: no audio wanted) */
		int v = (e->blind ? 0 : 1) | (e->audio ? 2 : 8);
		*(int*) data = v;
		return true;
	}
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
		if (debugLog()) fprintf(stderr, "[%s] message: %s\n", e->coreName, ((const struct retro_message*) data)->msg);
		return true;
	/* accepted and ignored: descriptions the frontend doesn't need */
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
	if ((size_t) w * h > e->fbCap) {
		uint32_t* fb = realloc(e->fb, (size_t) w * h * sizeof *fb);
		if (!fb) return;
		e->fb = fb;
		e->fbCap = (size_t) w * h;
	}
	e->w = (int) w;
	e->h = (int) h;
	for (unsigned y = 0; y < h; ++y) {
		const uint8_t* row = (const uint8_t*) data + y * pitch;
		uint32_t* out = e->fb + (size_t) y * w;
		for (unsigned x = 0; x < w; ++x) {
			unsigned r, g, b;
			if (e->core->pixelFormat == RETRO_PIXEL_FORMAT_XRGB8888) {
				uint32_t c = ((const uint32_t*) row)[x];
				r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
			} else if (e->core->pixelFormat == RETRO_PIXEL_FORMAT_RGB565) {
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

static size_t audio_push(Emu* e, const int16_t* data, size_t frames) {
	if (!e || !e->audio || !e->ring) return frames;
	for (size_t i = 0; i < frames; ++i) {
		size_t next = (e->ringHead + 1) % EMU_AUDIO_RING;
		if (next == e->ringTail) break; /* full: drop the rest */
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
	if (id == RETRO_DEVICE_ID_JOYPAD_MASK) return (int16_t) (e->keys & 0xFFFF);
	return id < 16 ? (e->keys >> id) & 1 : 0;
}

/* ---- opening and closing ---- */

void emu_option(Emu* e, const char* key, const char* value) {
	int i = 0;
	while (i < e->nOptions && strcmp(e->options[i].key, key)) ++i;
	if (i == EMU_MAX_OPTIONS) return;
	snprintf(e->options[i].key, sizeof e->options[i].key, "%s", key);
	snprintf(e->options[i].value, sizeof e->options[i].value, "%s", value);
	if (i == e->nOptions) ++e->nOptions;
}

static uint8_t* read_file(const char* path, size_t* n) {
	FILE* f = fopen(path, "rb");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8_t* b = len > 0 ? malloc((size_t) len) : NULL;
	if (b && fread(b, 1, (size_t) len, f) != (size_t) len) {
		free(b);
		b = NULL;
	}
	fclose(f);
	*n = b ? (size_t) len : 0;
	return b;
}

static bool claim_handle(void* h) {
	pthread_mutex_lock(&openLock);
	bool ok = true;
	int freeSlot = -1;
	for (int i = 0; i < MAX_OPEN; ++i) {
		if (openHandles[i] == h) ok = false;
		if (!openHandles[i] && freeSlot < 0) freeSlot = i;
	}
	if (ok && freeSlot >= 0) openHandles[freeSlot] = h;
	pthread_mutex_unlock(&openLock);
	return ok && freeSlot >= 0;
}

static void release_handle(void* h) {
	pthread_mutex_lock(&openLock);
	for (int i = 0; i < MAX_OPEN; ++i) {
		if (openHandles[i] == h) openHandles[i] = NULL;
	}
	pthread_mutex_unlock(&openLock);
}

/* Battery save: <dataDir>/<ROM name without extension>.srm, for cores that
 * expose RETRO_MEMORY_SAVE_RAM and leave loading it to the frontend
 * (snes9x). bsnes loads the same file itself and exposes no save RAM.
 * Nothing is written back yet (M0). */
static void load_sram(Emu* e, const char* romPath) {
	EmuCore* c = e->core;
	uint8_t* sram = c->get_memory_data(RETRO_MEMORY_SAVE_RAM);
	size_t size = c->get_memory_size(RETRO_MEMORY_SAVE_RAM);
	if (!sram || !size) return;
	const char* base = strrchr(romPath, '/');
	base = base ? base + 1 : romPath;
	char path[2200];
	snprintf(path, sizeof path, "%s/%s", c->dataDir, base);
	char* dot = strrchr(path, '.');
	if (dot && dot > strrchr(path, '/')) *dot = 0;
	strncat(path, ".srm", sizeof path - strlen(path) - 1);
	size_t n = 0;
	uint8_t* b = read_file(path, &n);
	if (!b) return;
	memcpy(sram, b, n < size ? n : size);
	free(b);
	if (debugLog()) fprintf(stderr, "[%s] save RAM from %s (%zu bytes)\n", e->coreName, path, n);
}

#define LOAD_SYM(c, name)                                                     \
	do {                                                                        \
		*(void**) &(c)->name = dlsym((c)->handle, "retro_" #name);              \
		if (!(c)->name) {                                                       \
			fprintf(stderr, "emu: %s has no retro_" #name "\n", (c)->path);     \
			goto fail;                                                          \
		}                                                                       \
	} while (0)

bool emu_open(Emu* e, const char* corePath, const char* romPath, const char* dataDir, bool audio) {
	e->core = NULL;
	e->fb = NULL;
	e->w = e->h = 0;
	e->fbCap = 0;
	e->audio = audio;
	e->frames = 0;
	e->keys = 0;
	e->blind = false;
	e->ring = NULL;
	e->ringHead = e->ringTail = 0;
	e->coreName[0] = 0;

	EmuCore* c = calloc(1, sizeof *c);
	if (!c) return false;
	snprintf(c->path, sizeof c->path, "%s", corePath);
	snprintf(c->dataDir, sizeof c->dataDir, "%s", dataDir);
	mkdir(dataDir, 0755);
	c->pixelFormat = RETRO_PIXEL_FORMAT_0RGB1555; /* libretro's default */

	c->handle = dlopen(corePath, RTLD_NOW | RTLD_LOCAL);
	if (!c->handle) {
		fprintf(stderr, "emu: can't load %s: %s\n", corePath, dlerror());
		free(c);
		return false;
	}
	if (!claim_handle(c->handle)) {
		fprintf(stderr, "emu: %s is already in use by another instance (give each instance its own copy of the core)\n", corePath);
		dlclose(c->handle);
		free(c);
		return false;
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
	LOAD_SYM(c, load_game);
	LOAD_SYM(c, unload_game);
	LOAD_SYM(c, get_memory_data);
	LOAD_SYM(c, get_memory_size);

	if (c->api_version() != RETRO_API_VERSION) {
		fprintf(stderr, "emu: %s has libretro API %u, not %u\n", corePath, c->api_version(), RETRO_API_VERSION);
		goto fail;
	}

	Emu* prev = cur;
	cur = e;
	struct retro_system_info info = { 0 };
	c->get_system_info(&info);
	snprintf(e->coreName, sizeof e->coreName, "%s", info.library_name ? info.library_name : "core");

	c->set_environment(env_cb);
	c->init();
	c->set_video_refresh(video_cb);
	c->set_audio_sample(audio_sample_cb);
	c->set_audio_sample_batch(audio_batch_cb);
	c->set_input_poll(input_poll_cb);
	c->set_input_state(input_state_cb);

	c->rom = read_file(romPath, &c->romSize);
	if (!c->rom) {
		fprintf(stderr, "emu: can't read %s\n", romPath);
		c->deinit();
		cur = prev;
		goto fail;
	}
	size_t skip = c->romSize % 1024 == 512 ? 512 : 0;
	e->romCrc = emu_crc32(0, c->rom + skip, c->romSize - skip);

	struct retro_game_info game = { .path = romPath, .data = c->rom, .size = c->romSize };
	if (!c->load_game(&game)) {
		fprintf(stderr, "emu: %s couldn't load %s\n", e->coreName, romPath);
		c->deinit();
		cur = prev;
		goto fail;
	}
	struct retro_system_av_info av = { 0 };
	c->get_system_av_info(&av);
	e->fps = av.timing.fps;
	e->sampleRate = av.timing.sample_rate;
	e->fbCap = (size_t) av.geometry.max_width * av.geometry.max_height;
	e->fb = calloc(e->fbCap ? e->fbCap : 1, sizeof *e->fb);
	e->w = (int) av.geometry.base_width;
	e->h = (int) av.geometry.base_height;
	c->set_controller_port_device(0, RETRO_DEVICE_JOYPAD);
	load_sram(e, romPath);
	cur = prev;

	if (audio) e->ring = calloc(EMU_AUDIO_RING * 2, sizeof *e->ring);
	return true;

fail:
	release_handle(c->handle);
	dlclose(c->handle);
	free(c->rom);
	free(c);
	e->core = NULL;
	return false;
}

void emu_close(Emu* e) {
	EmuCore* c = e->core;
	if (!c) return;
	Emu* prev = cur;
	cur = e;
	c->unload_game();
	c->deinit();
	cur = prev;
	release_handle(c->handle);
	dlclose(c->handle);
	free(c->rom);
	free(c);
	free(e->fb);
	free(e->ring);
	e->core = NULL;
	e->fb = NULL;
	e->ring = NULL;
}

/* ---- running ---- */

bool emu_cancelled(const Emu* e) {
	return e->cancel && *e->cancel;
}

static void run_frame(Emu* e, uint32_t keys, bool blind) {
	if (emu_cancelled(e)) return;
	Emu* prev = cur;
	cur = e;
	e->keys = keys;
	e->blind = blind;
	e->core->run();
	e->blind = false;
	cur = prev;
	++e->frames;
}

void emu_frame(Emu* e, uint32_t keys) {
	run_frame(e, keys, false);
}

void emu_frame_blind(Emu* e, uint32_t keys) {
	run_frame(e, keys, true);
}

void emu_reset(Emu* e) {
	Emu* prev = cur;
	cur = e;
	e->core->reset();
	cur = prev;
}

/* ---- states ---- */

size_t emu_state_size(Emu* e) {
	Emu* prev = cur;
	cur = e;
	size_t n = e->core->serialize_size();
	cur = prev;
	return n;
}

bool emu_state_save(Emu* e, void* buf, size_t size) {
	Emu* prev = cur;
	cur = e;
	bool ok = e->core->serialize(buf, size);
	cur = prev;
	return ok;
}

bool emu_state_load(Emu* e, const void* buf, size_t size) {
	Emu* prev = cur;
	cur = e;
	bool ok = e->core->unserialize(buf, size);
	cur = prev;
	return ok;
}

bool emu_state_save_file(Emu* e, const char* path) {
	size_t n = emu_state_size(e);
	uint8_t* buf = n ? malloc(n) : NULL;
	bool ok = buf && emu_state_save(e, buf, n);
	if (ok) {
		FILE* f = fopen(path, "wb");
		ok = f && fwrite(buf, 1, n, f) == n;
		if (f && fclose(f)) ok = false;
	}
	free(buf);
	return ok;
}

/* RetroArch's state files: "RASTATE" and version 1, then blocks (a
 * 4-character tag, a u32 size, the data, zeros to a multiple of 8) up to
 * "END ", MEM being the core's own bytes; usually compressed as "#RZIPv" 1
 * "#", u32 chunk size, u64 size, then each chunk as a u32 size and zlib's
 * data. Same format as GBA DS's src/emu.c reads for its mGBA core. */
int uncompress(unsigned char* dest, unsigned long* destLen, const unsigned char* source, unsigned long sourceLen); /* zlib */

static uint32_t le32(const uint8_t* p) {
	return (uint32_t) p[0] | (uint32_t) p[1] << 8 | (uint32_t) p[2] << 16 | (uint32_t) p[3] << 24;
}

static uint8_t* unwrap_retroarch(uint8_t* b, size_t* bn) {
	static const uint8_t rzip[6] = { '#', 'R', 'Z', 'I', 'P', 'v' };
	if (*bn >= 20 && !memcmp(b, rzip, 6) && b[6] == 1 && b[7] == '#') {
		uint32_t chunk = le32(b + 8);
		uint64_t total = (uint64_t) le32(b + 12) | (uint64_t) le32(b + 16) << 32;
		uint8_t* out = chunk && total && total <= (64u << 20) ? malloc((size_t) total) : NULL;
		size_t got = 0, at = 20;
		while (out && got < total) {
			unsigned long dl = (unsigned long) (total - got < chunk ? total - got : chunk);
			uint32_t zn = at + 4 <= *bn ? le32(b + at) : 0;
			if (!zn || zn > *bn - at - 4 || uncompress(out + got, &dl, b + at + 4, zn) != 0 || !dl) {
				free(out);
				out = NULL;
				break;
			}
			got += dl, at += 4 + zn;
		}
		free(b);
		if (!out) return NULL;
		b = out;
		*bn = got;
	}
	if (*bn >= 8 && !memcmp(b, "RASTATE", 7) && b[7] == 1) {
		size_t at = 8;
		while (at + 8 <= *bn) {
			uint32_t sz = le32(b + at + 4);
			if (!memcmp(b + at, "END ", 4) || sz > *bn - at - 8) break;
			if (!memcmp(b + at, "MEM ", 4)) {
				memmove(b, b + at + 8, sz);
				*bn = sz;
				return b;
			}
			at += 8 + (((size_t) sz + 7) & ~(size_t) 7);
		}
		free(b);
		return NULL;
	}
	return b;
}

bool emu_state_load_file(Emu* e, const char* path) {
	size_t n = 0;
	uint8_t* b = read_file(path, &n);
	if (b) b = unwrap_retroarch(b, &n);
	if (!b) {
		fprintf(stderr, "emu: can't read state %s\n", path);
		return false;
	}
	bool ok = emu_state_load(e, b, n);
	if (!ok) fprintf(stderr, "emu: %s didn't accept state %s (%zu bytes; another core or version?)\n", e->coreName, path, n);
	free(b);
	return ok;
}

/* ---- memory ---- */

uint8_t* emu_wram(Emu* e, size_t* size) {
	Emu* prev = cur;
	cur = e;
	uint8_t* p = e->core->get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
	size_t n = e->core->get_memory_size(RETRO_MEMORY_SYSTEM_RAM);
	cur = prev;
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
	uint8_t* w = emu_wram(e, &n);
	long i = wram_index(addr);
	return w && i >= 0 && (size_t) i < n ? w[i] : 0;
}

uint16_t emu_read16(Emu* e, uint32_t addr) {
	return (uint16_t) (emu_read8(e, addr) | emu_read8(e, addr + 1) << 8);
}

uint32_t emu_read24(Emu* e, uint32_t addr) {
	return (uint32_t) emu_read16(e, addr) | (uint32_t) emu_read8(e, addr + 2) << 16;
}

void emu_write8(Emu* e, uint32_t addr, uint8_t value) {
	size_t n;
	uint8_t* w = emu_wram(e, &n);
	long i = wram_index(addr);
	if (w && i >= 0 && (size_t) i < n) w[i] = value;
}

void emu_write16(Emu* e, uint32_t addr, uint16_t value) {
	emu_write8(e, addr, (uint8_t) value);
	emu_write8(e, addr + 1, (uint8_t) (value >> 8));
}

/* ---- audio ---- */

size_t emu_audio_read(Emu* e, int16_t* out, size_t maxFrames) {
	size_t n = 0;
	while (e->ring && n < maxFrames && e->ringTail != e->ringHead) {
		out[n * 2] = e->ring[e->ringTail * 2];
		out[n * 2 + 1] = e->ring[e->ringTail * 2 + 1];
		e->ringTail = (e->ringTail + 1) % EMU_AUDIO_RING;
		++n;
	}
	return n;
}

unsigned emu_audio_rate(Emu* e) {
	return (unsigned) (e->sampleRate + 0.5);
}

/* ---- ROM identity ---- */

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

bool emu_rom_crc(const char* path, uint32_t* crc) {
	size_t n = 0;
	uint8_t* b = read_file(path, &n);
	if (!b) return false;
	size_t skip = n % 1024 == 512 ? 512 : 0;
	*crc = emu_crc32(0, b + skip, n - skip);
	free(b);
	return true;
}

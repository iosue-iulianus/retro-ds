/* SNES DS: a thin wrapper around a libretro core loaded from a .so.
 *
 * Each Emu owns one loaded core. libretro cores keep their state in globals,
 * so two Emus (the live game and the hidden copy) need two copies of the
 * core at two different paths (on the host: the same .so copied; on
 * Android: the core built twice under two sonames). emu_open refuses a core
 * path whose library is already in use by another open Emu.
 *
 * An Emu may run on any thread, but one thread at a time; two Emus may run
 * on two threads at once.
 *
 * Replaces GBA DS's src/emu.c (libmgba) once the port is done; until then
 * it builds only into the M0 tools (make m0). */
#ifndef SNESDS_RETRO_EMU_H
#define SNESDS_RETRO_EMU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* SNES pad buttons: bit n is libretro's RETRO_DEVICE_ID_JOYPAD_n, so a key
 * mask is also the core's input bitmask. */
enum {
	K_B = 1 << 0,
	K_Y = 1 << 1,
	K_SELECT = 1 << 2,
	K_START = 1 << 3,
	K_UP = 1 << 4,
	K_DOWN = 1 << 5,
	K_LEFT = 1 << 6,
	K_RIGHT = 1 << 7,
	K_A = 1 << 8,
	K_X = 1 << 9,
	K_L = 1 << 10,
	K_R = 1 << 11,
};

/* libretro's savestate contexts (RETRO_SAVESTATE_CONTEXT_*): what a state
 * is for. A core may take a faster path for anything but NORMAL. */
enum {
	EMU_STATE_NORMAL = 0,
	EMU_STATE_RUNAHEAD_SAME_INSTANCE = 1,
	EMU_STATE_RUNAHEAD_SAME_BINARY = 2,
	EMU_STATE_ROLLBACK_NETPLAY = 3,
};

#define EMU_MAX_OPTIONS 32
#define EMU_AUDIO_RING (1 << 15) /* stereo frames */

struct EmuCore;

typedef struct Emu {
	struct EmuCore* core;

	/* The last picture, as many pixels as the core drew (w x h, w wide in
	 * memory). Each pixel is bytes R, G, B, X (GBA DS's framebuffer format,
	 * so the capture code's colour probes carry over). w and h change when
	 * a game switches to hi-res (512 wide) or interlace. */
	uint32_t* fb;
	int w, h;
	size_t fbCap; /* pixels allocated */

	uint32_t romCrc; /* CRC32 of the ROM without a copier header */
	char coreName[64];
	double fps, sampleRate;

	bool audio; /* keep the samples (emu_audio_read); off for a hidden copy */
	unsigned long frames; /* frames run */

	/* The state context retro_serialize / retro_unserialize are told
	 * (EMU_STATE_*). Default NORMAL. */
	int stateContext;

	/* Set while a capture is called off: frames don't run (as GBA DS). */
	volatile bool* cancel;

	/* Core options (key = value), read by the core through GET_VARIABLE.
	 * Set before emu_open with emu_option. */
	struct { char key[64], value[64]; } options[EMU_MAX_OPTIONS];
	int nOptions;

	/* internal: per-frame state the callbacks use */
	uint32_t keys;
	bool blind;
	int16_t* ring; /* EMU_AUDIO_RING stereo frames */
	size_t ringHead, ringTail;
} Emu;

/* Sets a core option before emu_open (replaces one with the same key). */
void emu_option(Emu* e, const char* key, const char* value);

/* Loads the core at corePath and the ROM at romPath. dataDir is the core's
 * system and save directory (its SRAM lands there): give the hidden copy
 * a directory of its own so it never writes the player's save. */
bool emu_open(Emu* e, const char* corePath, const char* romPath, const char* dataDir, bool audio);
void emu_close(Emu* e);

/* Runs one frame with these keys held. */
void emu_frame(Emu* e, uint32_t keys);
/* The same, telling the core the picture isn't wanted (e->fb keeps the
 * last one). Cores that don't honour GET_AUDIO_VIDEO_ENABLE draw anyway. */
void emu_frame_blind(Emu* e, uint32_t keys);
bool emu_cancelled(const Emu* e);
void emu_reset(Emu* e);

/* States: the core's own bytes (retro_serialize). */
size_t emu_state_size(Emu* e);
bool emu_state_save(Emu* e, void* buf, size_t size);
bool emu_state_load(Emu* e, const void* buf, size_t size);
/* Files: saving writes the core's bytes; loading also takes RetroArch's
 * state files (RASTATE, RZIP-compressed or not) for the same core. */
bool emu_state_save_file(Emu* e, const char* path);
bool emu_state_load_file(Emu* e, const char* path);

/* Work RAM ($7E0000-$7FFFFF, 128 KB), or NULL if the core doesn't expose
 * RETRO_MEMORY_SYSTEM_RAM. */
uint8_t* emu_wram(Emu* e, size_t* size);
/* Reads and writes by SNES address as the CPU sees it: banks $7E/$7F, and
 * the low 8 KB mirror at $0000-$1FFF of banks $00-$3F and $80-$BF.
 * Anything else reads 0 and ignores writes. Little-endian. */
uint8_t emu_read8(Emu* e, uint32_t addr);
uint16_t emu_read16(Emu* e, uint32_t addr);
uint32_t emu_read24(Emu* e, uint32_t addr);
void emu_write8(Emu* e, uint32_t addr, uint8_t value);
void emu_write16(Emu* e, uint32_t addr, uint16_t value);

/* Up to maxFrames stereo frames of audio; returns frames read. */
size_t emu_audio_read(Emu* e, int16_t* out, size_t maxFrames);
unsigned emu_audio_rate(Emu* e);

/* CRC32 of a ROM file, without its 512-byte copier header if it has one
 * (size % 1024 == 512). false if the file can't be read. */
bool emu_rom_crc(const char* path, uint32_t* crc);
uint32_t emu_crc32(uint32_t crc, const void* data, size_t n);

#endif

/* RetroDS's emulator: a libretro core (bsnes) loaded from a .so.
 *
 * Each open Emu has a core of its own: libretro cores keep their state in
 * globals, so every Emu open at once (the game, the hidden copy that takes
 * map pictures, the logo's) loads its own copy of the core library. The
 * copies are given once with emu_set_cores: on Android the core built under
 * several sonames (libsnes_a.so, ...), on the desktop the same .so copied
 * to several paths. An Emu runs on one thread at a time; two Emus can run
 * on two threads at once. */
#ifndef RETRODS_EMU_H
#define RETRODS_EMU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "writes.h"

/* The SNES screen, as the player sees it: 256 x 224 (239 with overscan).
 * Hi-res pictures are 512 wide and interlaced ones twice as tall, but show
 * at the same size on screen. */
#define SNES_W 256
#define SNES_H 224
/* The largest picture a core draws (hi-res and interlace: 512 x 478). */
#define EMU_MAX_W 512
#define EMU_MAX_H 480
/* Emus open at once (core copies): the game, the map's hidden copy, the logo's. */
#define EMU_CORES 3

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
#define K_ALL 0xFFFu

struct EmuCore;
struct EmuSave;

typedef struct Emu {
	struct EmuCore* core; /* NULL while closed */

	/* The last picture, w x h as the core drew it (256 x 224 in most games,
	 * 512 wide in hi-res, 448 or 478 tall interlaced), w wide in memory.
	 * Each pixel is bytes R, G, B, 255. */
	uint32_t* fb;
	int w, h;

	/* The game: the CRC32 of its ROM file (the ROM inside a .zip), without
	 * a 512-byte copier header, as 8 hex digits. Profiles, logos and the
	 * game's own settings go by it. */
	char gameCode[9];
	uint32_t fileCrc; /* the same, as a number */
	uint32_t romCrc;  /* CRC32 of the ROM as played: patched if it was (profile_dump) */
	int patch; /* PatchResult (patch.h): the patches planned for it, and whether they went in (GEN-9) */
	char patchName[256]; /* the applied patches' file names, " + " between (or the one that wasn't) */
	char patchSkipped[256]; /* (PATCH_PARTIAL) the ones left out */

	bool audio; /* keep the samples (emu_audio_read); off for a hidden copy */
	unsigned long frames; /* frames run */
	double fps, sampleRate;
	/* Set while a capture is called off: frames don't run, so whatever the
	 * capture waits for gives up at once; emu_cancelled says so. */
	volatile bool* cancel;

	/* (internal) */
	uint32_t keys;
	bool blind;
	int stateContext; /* the RETRO_SAVESTATE_CONTEXT the core is told */
	size_t stateSize;
	int16_t* ring;
	size_t ringHead, ringTail;
	struct EmuSave* save;
} Emu;

/* The core copies (up to EMU_CORES paths, each a separate file) and a
 * folder for the ROMs as played and the cores' own files. Call once,
 * before any emu_open. */
void emu_set_cores(const char* const* paths, int n, const char* cacheDir);
/* The desktop's way: copies the core at `core` to cacheDir/core-a.so,
 * core-b.so... (unless they're there already, the same size and age) and
 * sets them up as above. */
bool emu_set_core_copies(const char* core, const char* cacheDir);

/* Loads the game: the ROM as played (patches applied, GEN-9) on a free
 * core copy. savePath (NULL: none): the battery save, loaded now and
 * written as it changes; the hidden copies get none. */
bool emu_open(Emu* e, const char* romPath, const char* savePath, bool audio);
void emu_close(Emu* e);
/* ROM files the game list takes: .sfc, .smc, .swc, .fig, and .zip */
bool emu_is_rom_name(const char* name);
bool emu_is_archive_name(const char* name); /* .zip */

/* One frame with these keys held. */
void emu_frame(Emu* e, uint32_t keys);
/* The same, the picture not wanted (e->fb keeps the last one; a core that
 * doesn't honour GET_AUDIO_VIDEO_ENABLE draws anyway). */
void emu_frame_blind(Emu* e, uint32_t keys);
bool emu_cancelled(const Emu* e);
void emu_reset(Emu* e);

/* States, the core's own bytes. They can go to another Emu of the same
 * game (the hidden copy) and into files. */
size_t emu_state_size(Emu* e);
bool emu_state_save(Emu* e, void* buf);
/* A state of the game being played, taken between frames when the next
 * frame will run with nextKeys. bsnes runs the emulation forward to a sync
 * point to take a state, and if the game reads the controller on the way it
 * reads the keys the core was last given: given the next frame's keys, the
 * state leaves the game exactly where it would have been
 * (docs/M0-results.md). */
bool emu_state_save_live(Emu* e, uint32_t nextKeys, void* buf);
bool emu_state_load(Emu* e, const void* buf);

/* Rewind: states appended now and then while playing (in this Emu only,
 * taken without disturbing the game), kept as differences from the next
 * one; each step goes back one (false: none left). */
void emu_rewind_init(Emu* e, size_t entries);
void emu_rewind_deinit(void);
void emu_rewind_append(Emu* e);
bool emu_rewind_step(Emu* e);
size_t emu_rewind_count(void);

/* State files: RetroArch's format (RASTATE, blocks up to "END "), so
 * RetroArch's bsnes core of the same version loads RetroDS's states and the
 * other way round. MEM holds the core's bytes; RetroDS adds SDSC (the ROM's
 * CRC32 as played) and SDSX (its second screen, `extra`), which RetroArch
 * skips. RetroArch usually compresses its files (RZIP, zlib chunks). */
typedef enum {
	STATE_FORMAT_OWN,            /* RetroDS's own states: RASTATE, uncompressed */
	STATE_FORMAT_RETROARCH,      /* RetroArch's, uncompressed */
	STATE_FORMAT_RETROARCH_RZIP, /* RetroArch's, compressed (its savestate_file_compression) */
} StateFormat;
typedef enum {
	STATE_FILE_NONE,       /* no such file */
	STATE_FILE_OWN,        /* saved by RetroDS */
	STATE_FILE_RETROARCH,  /* RetroArch's (or RetroDS's, saved into RetroArch's folder) */
	STATE_FILE_OTHER_CORE, /* another core's or another version's: not loadable */
	STATE_FILE_UNREADABLE, /* damaged, or compressed in a way RetroDS can't read */
} StateFileKind;
bool emu_state_save_file(Emu* e, const char* path);
bool emu_state_save_file_as(Emu* e, const char* path, const void* extra, size_t extraN, StateFormat fmt);
bool emu_state_load_file(Emu* e, const char* path);
/* *fromRetrods (may be NULL): saved by RetroDS (it has RetroDS's blocks) */
StateFileKind emu_state_file_kind(Emu* e, const char* path, bool* fromRetrods);
/* The CRC32 of the ROM (as played) a state was saved with; false if the
 * file doesn't say (not RetroDS's) */
bool emu_state_file_rom_crc(const char* path, uint32_t* crc);
/* RetroDS's second-screen data in a state file, malloc'd (NULL: none) */
void* emu_state_file_get_extra(Emu* e, const char* path, size_t* n);

/* ROMs. emu_rom_read_all: the ROM file (inside a .zip too) without a
 * copier header, malloc'd; NULL if it can't be read. */
uint8_t* emu_rom_read_all(const char* path, size_t* size);
/* The game's code (Emu.gameCode) for a ROM file; false if unreadable. */
bool emu_rom_code(const char* path, char code[9]);
/* The title in the ROM's internal header (21 characters, trailing spaces
 * cut), or false. */
bool emu_rom_title(const char* path, char* out, size_t n);
/* The ROM as it's played: patched when patches are planned for it (or a
 * patch file is beside it, GEN-9). *patch: PatchResult; patchName (if not
 * NULL, 256 bytes): the patch files' names; *fileCrc (if not NULL): the
 * ROM's CRC32 before any patch. malloc'd, NULL if the ROM can't be read. */
uint8_t* emu_rom_read_played(const char* path, size_t* size, int* patch, char* patchName, uint32_t* fileCrc);
/* The patches a game is played with (GEN-9), in order: whatever opens that
 * ROM from now on gets them; a NULL rom or no files: none planned (then the
 * patch file beside a ROM, if any). */
void emu_patch_plan(const char* rom, char files[][1024], int n);
/* The ROM with the patches given applied, in order (the game list's
 * checksum of each game as it would be played) */
uint8_t* emu_rom_read_patched(const char* path, char files[][1024], int n, size_t* size, int* patch, char* patchName);

/* Memory. Work RAM ($7E0000-$7FFFFF, 128 KB), the cartridge's battery
 * RAM and video RAM (64 KB, words little-endian; bsnes gives it only with
 * its fast PPU, the default), or NULL. Read-only for RetroDS. */
enum { EMU_MEM_WRAM, EMU_MEM_SRAM, EMU_MEM_VRAM };
uint8_t* emu_memory(Emu* e, int region, size_t* size);
/* By SNES address as the CPU sees it: banks $7E/$7F, and the low 8 KB
 * mirror at $0000-$1FFF of banks $00-$3F and $80-$BF. Anything else reads
 * 0 and ignores writes. Little-endian. */
uint8_t emu_read8(Emu* e, uint32_t addr);
uint16_t emu_read16(Emu* e, uint32_t addr);
uint32_t emu_read24(Emu* e, uint32_t addr);
uint32_t emu_read32(Emu* e, uint32_t addr);
void emu_write8(Emu* e, uint32_t addr, uint8_t value);
/* A batch of writes (writes.h), between frames: every address must lie in
 * one of the game's ranges and in work RAM, else none is written; each
 * one logged with its old and new value, under `what`. The number
 * written, -1 if refused. */
int emu_apply(Emu* e, const char* what, const Write* w, int n, const WriteRange* ok, int nOk);
void emu_write16(Emu* e, uint32_t addr, uint16_t value);

/* Up to maxFrames stereo frames of audio; returns frames read. */
size_t emu_audio_read(Emu* e, int16_t* out, size_t maxFrames);
unsigned emu_audio_rate(Emu* e);

/* Cheats, through the core (Game Genie, Pro Action Replay and raw codes,
 * as the core reads them). emu_cheats_add adds one set: its codes joined
 * with '+'. Returns the number of codes given to the core. */
void emu_cheats_clear(Emu* e);
int emu_cheats_add(Emu* e, const char* name, const char* const* codes, int n);

uint32_t emu_crc32(uint32_t crc, const void* data, size_t n);

/* tools/png.c (host tools only) */
bool write_png(const char* path, const uint32_t* px, int w, int h, int stride);

#endif

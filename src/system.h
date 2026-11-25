/* What differs between the consoles RetroDS plays (docs/SCOPE.md 2.2, kept
 * small): which files are a system's games, how its core takes them, its
 * BIOS, how many core copies it gets, and where its work RAM sits in the
 * CPU's address space. A game's system goes by its file's extension. */
#ifndef RETRODS_SYSTEM_H
#define RETRODS_SYSTEM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
	SYS_SNES,
	SYS_PSX,
	SYS_COUNT,
} SysId;

typedef struct System {
	SysId id;
	const char* key;  /* "snes", "psx": the ROM folder's name, logs */
	const char* name; /* "Super Nintendo", "PlayStation" */
	/* the game list's files (lowercase, no dot); a .zip holds a game too
	 * when `zip` */
	const char* const* exts;
	bool zip;
	/* the core opens the game itself, from its path (discs), instead of
	 * being handed its bytes: no copier header, no patches, no ROM cache */
	bool fullPath;
	/* core copies: SNES 3 (the game, the map's hidden copy, the logo's);
	 * the others 1 (the game alone: no hidden copy, no logo extracted) */
	int coreCopies;
	/* the BIOS the core wants in its system folder (NULL: none), and the
	 * known good dump's MD5 (lowercase hex) */
	const char* bios;
	const char* biosMd5;
	/* work RAM as the CPU sees it: the address of its byte 0 and the
	 * mirror mask (PSX: 2 MB at 0x00000000, 0x80000000 and 0xA0000000) */
	uint32_t ramBase, ramMask;
	/* rewind and fast forward on L2 / R2 (off for the PlayStation: its
	 * games use L2 and R2; owner, Nov 25, 2026) */
	bool rewindKeys;
} System;

const System* system_get(SysId id);
/* The system whose game this file is, by its extension (a .zip: the
 * SNES), or NULL. */
const System* system_for_path(const char* path);
/* Does any system take this file name as a game? */
bool system_is_game_name(const char* name);

#endif

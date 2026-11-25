/* The systems RetroDS plays (see system.h). */
#include "system.h"

#include <string.h>
#include <strings.h>

static const char* const snesExts[] = { "sfc", "smc", "swc", "fig", NULL };
/* (a disc is its .cue: the .bin files it lists aren't games on their own) */
static const char* const psxExts[] = { "cue", NULL };

static const System systems[SYS_COUNT] = {
	[SYS_SNES] = {
		.id = SYS_SNES,
		.key = "snes",
		.name = "Super Nintendo",
		.exts = snesExts,
		.zip = true,
		.coreCopies = 3,
		.ramBase = 0x7E0000,
		.ramMask = 0x1FFFF,
		.rewindKeys = true,
	},
	[SYS_PSX] = {
		.id = SYS_PSX,
		.key = "psx",
		.name = "PlayStation",
		.exts = psxExts,
		.fullPath = true,
		.coreCopies = 1,
		.bios = "scph5501.bin",
		.biosMd5 = "490f666e1afb15b7362b406ed1cea246",
		.ramBase = 0x80000000,
		.ramMask = 0x1FFFFF,
	},
};

const System* system_get(SysId id) {
	return id >= 0 && id < SYS_COUNT ? &systems[id] : NULL;
}

static bool ext_in(const char* ext, const char* const* exts) {
	for (int i = 0; exts[i]; ++i) {
		if (!strcasecmp(ext, exts[i])) return true;
	}
	return false;
}

const System* system_for_path(const char* path) {
	const char* slash = strrchr(path, '/');
	const char* dot = strrchr(slash ? slash : path, '.');
	if (!dot) return NULL;
	for (int i = 0; i < SYS_COUNT; ++i) {
		if (ext_in(dot + 1, systems[i].exts) || (systems[i].zip && !strcasecmp(dot + 1, "zip"))) return &systems[i];
	}
	return NULL;
}

bool system_is_game_name(const char* name) {
	return system_for_path(name) != NULL;
}

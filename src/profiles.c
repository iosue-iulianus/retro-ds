/* SNES DS's game profiles (MapProfile, profiles.h) and what's known about
 * each release's dumps.
 *
 * Empty for now: GBA DS's 64 GBA profiles were removed with the GBA
 * emulator. SNES profiles come with the port (Super Metroid first, M2);
 * GBA DS's are in git history (git show 3b452dc:src/profiles.c). */
#include "profiles.h"

#include <stdio.h>
#include <string.h>

static const MapProfile profiles[] = {
	{ .codes = { NULL } }, /* (an empty entry: C has no empty arrays) */
};

const MapProfile* profile_find(const char* gameCode) {
	for (size_t i = 0; gameCode && i < sizeof(profiles) / sizeof(*profiles); ++i) {
		for (int j = 0; j < 4 && profiles[i].codes[j]; ++j) {
			if (!strncmp(profiles[i].codes[j], gameCode, 8)) return &profiles[i];
		}
	}
	return NULL;
}

/* ---------------------------------------------------------- known dumps */
typedef struct {
	char code[9];
	uint32_t crc;
	bool tested;
	bool mod; /* a romhack or patch (a Mods row), not one of No-Intro's dumps */
	const char* name;
} KnownDump;
#include "dumps_data.h"

DumpKind profile_dump(const char* gameCode, uint32_t crc, const char** name) {
	bool listed = false;
	if (name) *name = NULL;
	for (size_t i = 0; i < sizeof(knownDumps) / sizeof(knownDumps[0]); ++i) {
		if (!knownDumps[i].code[0] || strncmp(knownDumps[i].code, gameCode, 8)) continue;
		if (!knownDumps[i].mod) listed = true; /* (only patches known for it: not checked) */
		if (knownDumps[i].crc != crc) continue;
		if (name) *name = knownDumps[i].name;
		if (knownDumps[i].mod) return knownDumps[i].tested ? DUMP_MOD_TESTED : DUMP_MOD;
		return knownDumps[i].tested ? DUMP_TESTED : DUMP_OFFICIAL;
	}
	return listed ? DUMP_MODIFIED : DUMP_UNLISTED;
}

const KnownPatch* profile_patch(const uint8_t* file, size_t size, int n) {
	if (size <= 4) return NULL;
	uint32_t id = emu_crc32(0, file, size - 4);
	for (size_t i = 0; i < sizeof(knownPatches) / sizeof(knownPatches[0]); ++i) {
		if (!knownPatches[i].size || knownPatches[i].id != id || knownPatches[i].size != size) continue;
		if (n-- == 0) return &knownPatches[i];
	}
	return NULL;
}

/* The release's name as No-Intro gives it ("Super Metroid (Japan, USA)
 * (En,Ja)"): its tested dump's, else its first official one's, without
 * what sets that dump apart ("(Rev 1)"...): the region and languages kept.
 * False for a release with no list of dumps. (GEN-9's patches folder.) */
bool profile_release_name(const char* gameCode, char* out, size_t size) {
	const char* name = NULL;
	for (size_t i = 0; i < sizeof(knownDumps) / sizeof(knownDumps[0]); ++i) {
		if (!knownDumps[i].code[0] || strncmp(knownDumps[i].code, gameCode, 8) || knownDumps[i].mod) continue;
		if (!name || knownDumps[i].tested) name = knownDumps[i].name;
		if (knownDumps[i].tested) break;
	}
	if (!name) return false;
	/* the title, then "(Region)", then "(En,Fr,...)" if it's languages */
	const char* p = strchr(name, '(');
	size_t keep = p ? (size_t) (p - name) : strlen(name);
	while (keep && name[keep - 1] == ' ') --keep;
	const char* e = p ? strchr(p, ')') : NULL;
	if (e) {
		keep = (size_t) (e + 1 - name); /* the region */
		if (e[1] == ' ' && e[2] == '(') {
			const char* p2 = e + 2;
			const char* e2 = strchr(p2, ')');
			bool langs = e2 && e2 > p2 + 1;
			for (const char* q = p2 + 1; langs && q < e2; ++q) {
				if (!((*q >= 'A' && *q <= 'Z') || (*q >= 'a' && *q <= 'z') || *q == ',')) langs = false;
			}
			if (langs) keep = (size_t) (e2 + 1 - name);
		}
	}
	snprintf(out, size, "%.*s", (int) keep, name);
	return true;
}

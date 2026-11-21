/* SNES DS's game profiles (MapProfile, profiles.h) and what's known about
 * each release's dumps.
 *
 * GBA DS's 64 GBA profiles were removed with the GBA emulator; they're in
 * git history (git show 3b452dc:src/profiles.c) as examples. Each game's
 * rules are in docs/games/<game>.md. */
#include "profiles.h"

#include <stdio.h>
#include <string.h>

static const MapProfile profiles[] = {
	/* Super Metroid (docs/games/super-metroid.md): Start opens the pause
	 * screen's map after a fade */
	{
		.codes = { "D63ED5F8" }, /* Japan and USA (one ROM) */
		.title = "Super Metroid",
		.open = { { K_START, 4 } },
		.settleMax = 180, .openWithin = 120,
		.stableFrames = 8, .maxDiff = 150, /* Samus's marker blinks */
		/* SM-2: the frame and the area name, and the row of plates below
		 * it (SM-7's taps); not the energy row above */
		.crop_x = 0, .crop_y = 38, .crop_w = 256, .crop_h = 178,
		/* the frame (top, sides, bottom) and the SAMUS plate (bottom right),
		 * which only the map page has. Nothing on the grid: it scrolls with
		 * the map, so its dots aren't anywhere fixed */
		.probes = { { 20, 43, 0xFFFFFF }, { 240, 43, 0xFFFFFF }, { 4, 100, 0xFFFFFF }, { 251, 100, 0xFFFFFF },
		            { 20, 194, 0xFFFFFF }, { 240, 194, 0xFFFFFF }, { 204, 202, 0xFFB500 } },
		.tolerance = 8,
		/* the pause screen, on its map page (01: the equipment page) */
		.mem = { { 0x7E0998, 0xFF, 0x0F, 1 }, { 0x7E0727, 0xFFFF, 0x0000, 2 } },
		/* playing (08), anywhere but Ceres Station (area 06: no pausing) */
		.live = { { 0x7E0998, 0xFF, 0x08, 1 }, { 0x7E079F, 0xFF, 0x00, 1, true, 0x05, true } },
		/* the title, file select and options screens (00 to 05) */
		.menu = { { 0x7E0998, 0xFF, 0x00, 1, true, 0x05, true } },
		.room = { { 0x7E079B, 0, 0, 2 } },
		/* SM-7: a drag scrolls the map (its view at $7E00B1, $7E00B3); a tap
		 * on SAMUS (or the R pill) opens the equipment page with R, on MAP
		 * (or the L pill) goes back; Samus moving ($7E0AF6, $7E0AFA) puts
		 * the map back, centred */
		.touch = { .scrollX = 0x7E00B1, .scrollY = 0x7E00B3,
		           .page2 = { { K_R, 6 } },
		           .page2Mem = { { 0x7E0727, 0xFFFF, 0x0001, 2 } },
		           .page2Probes = { { 20, 43, 0xFFFFFF }, { 240, 43, 0xFFFFFF }, { 4, 100, 0xFFFFFF }, { 251, 100, 0xFFFFFF },
		                            { 20, 194, 0xFFFFFF }, { 240, 194, 0xFFFFFF }, { 44, 206, 0xFFB500 } },
		           .tap = { { 172, 196, 84, 22 }, { 0, 196, 84, 22 } },
		           .moved = { 0x7E0AF6, 0x7E0AFA },
		           /* the area's whole map: its 512 x 256 layer, nine views */
		           .bgW = 512, .bgH = 256, .centre = { 30, 60, 196, 116 }, .inner = { 6, 46, 244, 146 } },
		/* SM-7's plates: EXIT / START covered; arrows on the L and R pills
		 * in place of the letters (the pill's grey and its letters' purple);
		 * each page shows only the plate to the other one (the map:
		 * SAMUS and its pill; the equipment page: MAP and its) */
		.paint = { { 96, 199, 64, 16, true, 0x000000 },
		           { 20, 205, 9, 5, true, 0xBDBDBD, 0x42008C, { "...#.....", "..#......", ".######..", "..#......", "...#....." } },
		           { 228, 205, 9, 5, true, 0xBDBDBD, 0x42008C, { "....#....", ".....#...", ".######..", ".....#...", "....#...." } },
		           { 8, 199, 74, 16, true, 0x000000, .page = 1 },
		           { 174, 199, 74, 16, true, 0x000000, .page = 2 },
		           /* the equipment page's line from SUPPLY up to the energy
		           * row's reserve tanks (left out above): the grid seven
		           * cells below over it, the frame's top edge beside it, and
		           * the gap it leaves in SUPPLY's wall closed */
		           { 10, 38, 4, 3, true, 0x000000, .page = 2 },
		           { 10, 41, 4, 4, .page = 2, .fromDx = 4 },
		           { 10, 45, 4, 52, .page = 2, .fromDy = 56 },
		           { 10, 97, 8, 4, .page = 2, .fromDy = 56 },
		           { 18, 95, 4, 8, .page = 2, .fromDy = -8 },
		           { 22, 97, 2, 4, true, 0x000000, .page = 2 } },
		/* SM-6: the HUD's mini-map, while Samus is played, goes through a
		 * door, pauses and unpauses (08 to 12), outside Ceres Station */
		.hide = { { 206, 4, 46, 28 } },
		.hideIf = { { 0x7E0998, 0xFF, 0x08, 1, true, 0x12, true }, { 0x7E079F, 0xFF, 0x00, 1, true, 0x05, true } },
		/* the title screen, its lettering */
		.logo = { .wait = 600, .pressStart = true, .wait2 = 126, .minX = 44, .minY = 17, .maxX = 224, .maxY = 80 },
		/* SM-5: still (its marker, the scroll arrows and the plates blink
		 * on 24- and 54-frame cycles: a loop short enough to record cut
		 * them off and they pulsed too fast) */
		.animFrames = 12, .animStep = 1, .mapStill = true, .animCycle = 54,
	},
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

/* PlayStation discs (see disc.h): the cue sheet's data track, and the
 * ISO 9660 file system on it as far as SYSTEM.CNF. */
#include "disc.h"
#include "emu.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* The cue's first track: its file (a path beside the cue) and how its
 * sectors are laid out. */
typedef struct {
	char file[1100];
	int sectorSize; /* 2352 (raw), 2336 (mode 2 without sync) or 2048 */
	int dataOffset; /* where the 2048 bytes of user data start in one */
} Track;

/* the word after `key` on a cue line, quoted or not */
static bool cue_value(const char* line, char* out, size_t n) {
	while (*line == ' ' || *line == '\t') ++line;
	if (*line == '"') {
		const char* end = strchr(line + 1, '"');
		if (!end) return false;
		snprintf(out, n, "%.*s", (int) (end - line - 1), line + 1);
	} else {
		size_t k = strcspn(line, " \t\r\n");
		snprintf(out, n, "%.*s", (int) k, line);
	}
	return out[0] != 0;
}

static bool cue_first_track(const char* cuePath, Track* t) {
	FILE* f = fopen(cuePath, "r");
	if (!f) return false;
	memset(t, 0, sizeof(*t));
	char line[1200], file[1024] = "";
	bool found = false;
	while (!found && fgets(line, sizeof line, f)) {
		const char* p = line + strspn(line, " \t");
		if (!strncasecmp(p, "FILE", 4) && (p[4] == ' ' || p[4] == '\t')) {
			if (!cue_value(p + 4, file, sizeof file)) file[0] = 0;
		} else if (!strncasecmp(p, "TRACK", 5) && file[0]) {
			char mode[32] = "";
			int num = 0;
			if (sscanf(p + 5, "%d %31s", &num, mode) != 2) continue;
			if (!strcasecmp(mode, "MODE2/2352")) t->sectorSize = 2352, t->dataOffset = 24;
			else if (!strcasecmp(mode, "MODE1/2352")) t->sectorSize = 2352, t->dataOffset = 16;
			else if (!strcasecmp(mode, "MODE2/2336")) t->sectorSize = 2336, t->dataOffset = 8;
			else if (!strcasecmp(mode, "MODE1/2048") || !strcasecmp(mode, "MODE2/2048")) t->sectorSize = 2048, t->dataOffset = 0;
			else continue; /* (an audio track first: not a game disc) */
			found = true;
		}
	}
	fclose(f);
	if (!found) return false;
	const char* slash = strrchr(cuePath, '/');
	if (file[0] == '/' || !slash) snprintf(t->file, sizeof t->file, "%s", file);
	else snprintf(t->file, sizeof t->file, "%.*s/%s", (int) (slash - cuePath), cuePath, file);
	return true;
}

static bool read_sector(FILE* f, const Track* t, uint32_t lba, uint8_t out[2048]) {
	if (fseek(f, (long) lba * t->sectorSize + t->dataOffset, SEEK_SET)) return false;
	return fread(out, 1, 2048, f) == 2048;
}

static uint32_t le32(const uint8_t* p) {
	return (uint32_t) p[0] | (uint32_t) p[1] << 8 | (uint32_t) p[2] << 16 | (uint32_t) p[3] << 24;
}

/* SYSTEM.CNF from the root directory, as text (up to n - 1 bytes) */
static bool read_system_cnf(FILE* f, const Track* t, const uint8_t pvd[2048], char* out, size_t n) {
	const uint8_t* root = pvd + 156;
	uint32_t lba = le32(root + 2), size = le32(root + 10);
	uint8_t sec[2048];
	for (uint32_t done = 0; done < size && done < 64 * 2048; done += 2048) {
		if (!read_sector(f, t, lba + done / 2048, sec)) return false;
		for (int at = 0; at < 2048;) {
			int len = sec[at];
			if (!len || at + len > 2048) break; /* (the rest of this sector is empty) */
			int nameN = sec[at + 32];
			const char* name = (const char*) sec + at + 33;
			if (nameN >= 10 && 33 + nameN <= len && !strncasecmp(name, "SYSTEM.CNF", 10) && (nameN == 10 || name[10] == ';')) {
				uint32_t flba = le32(sec + at + 2), fsize = le32(sec + at + 10);
				uint8_t data[2048];
				if (!fsize || !read_sector(f, t, flba, data)) return false;
				size_t k = fsize < 2048 ? fsize : 2048;
				if (k > n - 1) k = n - 1;
				memcpy(out, data, k);
				out[k] = 0;
				return true;
			}
			at += len;
		}
	}
	return false;
}

/* "BOOT = cdrom:\SLUS_000.67;1" -> "SLUS-00067" */
static bool serial_from_cnf(const char* cnf, char* out, size_t n) {
	const char* p = cnf;
	while ((p = strstr(p, "BOOT"))) {
		const char* q = p + 4;
		if (*q == '2') ++q; /* (BOOT2: a PS2 disc) */
		while (*q == ' ' || *q == '\t') ++q;
		if (*q != '=') {
			p = q;
			continue;
		}
		++q;
		size_t lineN = strcspn(q, "\r\n");
		char line[256];
		snprintf(line, sizeof line, "%.*s", (int) (lineN < 255 ? lineN : 255), q);
		char* name = line;
		for (char* c = line; *c; ++c) {
			if (*c == '\\' || *c == ':' || *c == '/') name = c + 1;
		}
		name[strcspn(name, "; \t")] = 0;
		size_t o = 0;
		for (const char* c = name; *c && o + 1 < n; ++c) {
			if (*c == '.') continue;
			out[o++] = *c == '_' ? '-' : (char) toupper((unsigned char) *c);
		}
		out[o] = 0;
		/* (a serial is letters, a hyphen and digits: anything else, say
		 * PSX.EXE, isn't one) */
		return o >= 6 && strchr(out, '-') && strcmp(out, "PSXEXE");
	}
	return false;
}

bool disc_code(const char* cuePath, char* out, size_t n) {
	Track t;
	if (!cue_first_track(cuePath, &t)) return false;
	FILE* f = fopen(t.file, "rb");
	if (!f) return false;
	uint8_t pvd[2048];
	bool ok = read_sector(f, &t, 16, pvd) && pvd[0] == 1 && !memcmp(pvd + 1, "CD001", 5);
	if (ok) {
		char cnf[2048];
		if (!read_system_cnf(f, &t, pvd, cnf, sizeof cnf) || !serial_from_cnf(cnf, out, n))
			snprintf(out, n, "%08X", emu_crc32(0, pvd, sizeof pvd));
	}
	fclose(f);
	return ok;
}

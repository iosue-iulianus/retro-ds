/* PlayStation discs as .cue sheets with their .bin tracks: what RetroDS
 * reads itself (the core reads the rest). */
#ifndef RETRODS_DISC_H
#define RETRODS_DISC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The game's code: the serial in the boot line of the disc's SYSTEM.CNF
 * (BOOT = cdrom:\SLUS_000.67;1 gives "SLUS-00067"); a disc without one
 * (a PSX.EXE boots) gets the CRC32 of its volume descriptor as 8 hex
 * digits. False if the cue or its data track can't be read. */
bool disc_code(const char* cuePath, char* out, size_t n);

/* A file on the disc's data track by its path ("BIN/F_TITLE0.BIN", any
 * case, no version), malloc'd (*n its size, up to 8 MB); NULL if it
 * isn't there or can't be read. */
uint8_t* disc_read_file(const char* cuePath, const char* path, size_t* n);

#endif

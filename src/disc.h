/* PlayStation discs as .cue sheets with their .bin tracks: what RetroDS
 * reads itself (the core reads the rest). */
#ifndef RETRODS_DISC_H
#define RETRODS_DISC_H

#include <stdbool.h>
#include <stddef.h>

/* The game's code: the serial in the boot line of the disc's SYSTEM.CNF
 * (BOOT = cdrom:\SLUS_000.67;1 gives "SLUS-00067"); a disc without one
 * (a PSX.EXE boots) gets the CRC32 of its volume descriptor as 8 hex
 * digits. False if the cue or its data track can't be read. */
bool disc_code(const char* cuePath, char* out, size_t n);

#endif

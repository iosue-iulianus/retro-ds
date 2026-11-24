/* Writes to a game's memory, in one shape for every module that makes them
 * (the second screen's taps: SM-10, ALTTP-10), applied only by emu_apply:
 * each address checked against the game's list of what it may write, all
 * of a batch or none of it, each one logged. */
#ifndef RETRODS_WRITES_H
#define RETRODS_WRITES_H

#include <stdint.h>

/* one byte: a full SNES address (work RAM: $7E0000 to $7FFFFF) */
typedef struct {
	uint32_t addr;
	uint8_t value;
} Write;

/* addresses a game may be written at: addr to addr + size - 1, and why
 * (as its doc lists them) */
typedef struct {
	uint32_t addr, size;
	const char* what;
} WriteRange;

#endif

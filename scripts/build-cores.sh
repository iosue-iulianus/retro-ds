#!/bin/sh
# SNES DS: fetch and build the libretro cores for the host, at pinned
# commits, into ../cores (beside the repo).
#
#   scripts/build-cores.sh            bsnes (the shipped core, patched)
#   scripts/build-cores.sh snes9x     also snes9x (local prototyping only:
#                                     its licence is not GPL-compatible, so
#                                     it is never distributed)
#
# bsnes gets scripts/cores/bsnes-memory.patch: libretro's bsnes exposes no
# memory through retro_get_memory_data, and SNES DS reads work RAM and loads
# and saves cartridge RAM itself (bsnes writes it only when the game is
# unloaded). It also takes states for the same instance (rewind) without
# synchronizing, so they never move the emulation forward.
set -eu

BSNES_URL=https://github.com/libretro/bsnes.git
BSNES_REV=63ee22f7af8f5d1ada295c2c98fd7bf261fa62a7
SNES9X_URL=https://github.com/libretro/snes9x.git
SNES9X_REV=204ea1c0b851f960cbf223075eb1c25641cb483c

HERE=$(cd "$(dirname "$0")/.." && pwd)
CORES=${SNESDS_CORES:-$HERE/../cores}
JOBS=$(nproc 2>/dev/null || echo 4)
mkdir -p "$CORES"

fetch() { # dir url rev
	if [ ! -d "$CORES/$1/.git" ]; then
		git clone -q "$2" "$CORES/$1"
	fi
	git -C "$CORES/$1" fetch -q origin
	git -C "$CORES/$1" checkout -q "$3"
}

fetch bsnes "$BSNES_URL" "$BSNES_REV"
git -C "$CORES/bsnes" checkout -q -- .
git -C "$CORES/bsnes" apply "$HERE/scripts/cores/bsnes-memory.patch"
make -C "$CORES/bsnes" -j"$JOBS" target=libretro binary=library
echo "bsnes: $CORES/bsnes/bsnes_libretro.so"

if [ "${1:-}" = snes9x ]; then
	fetch snes9x "$SNES9X_URL" "$SNES9X_REV"
	make -C "$CORES/snes9x/libretro" -j"$JOBS"
	echo "snes9x: $CORES/snes9x/libretro/snes9x_libretro.so"
fi

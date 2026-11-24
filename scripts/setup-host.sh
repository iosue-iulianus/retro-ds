#!/bin/bash
# Sets up the desktop (x86_64 Linux) build used for development and testing:
# SDL2 into ./deps, the libretro cores into ../cores (scripts/build-cores.sh),
# then the app (build/host/snes-ds, with the bsnes core linked beside it)
# and the harness (build/host/snes-harness).
#
# Needs: git, cmake, make, gcc, g++, zlib and libpng headers.
#
# Run the app without a screen (saves in a folder of their own: by default
# they go beside the ROM):
#   SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy RETRODS_WINDOW=1024x1536 \
#     build/host/snes-ds --state-dir=/tmp/st --save_dir=/tmp/sav --input-script=script.txt GAME.sfc
# (script lines are "TICK BUTTONS" with buttons like A, B+UP or - for none,
#  "TICK SHOT:file.bmp", "TICK TAP:x,y" with x,y as fractions of the whole
#  window, "TICK QUIT")
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$PWD"
DEPS="$ROOT/deps"
mkdir -p "$DEPS"
SDL_BRANCH=${SDL_BRANCH:-release-2.30.x}

if [ ! -f "$DEPS/sdl-host/lib/libSDL2.so" ]; then
	[ -d "$DEPS/SDL2" ] || git clone --depth 1 -b "$SDL_BRANCH" https://github.com/libsdl-org/SDL.git "$DEPS/SDL2"
	cmake -S "$DEPS/SDL2" -B "$DEPS/SDL2/build-host" -DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_INSTALL_PREFIX="$DEPS/sdl-host" -DSDL_TEST=OFF -DSDL_STATIC=OFF
	make -C "$DEPS/SDL2/build-host" -j"$(nproc)" install
fi

scripts/build-cores.sh
CORES=${RETRODS_CORES:-$ROOT/../cores}

make -j"$(nproc)"
make tools
ln -sf "$CORES/bsnes/bsnes_libretro.so" build/host/bsnes_libretro.so
echo "built build/host/snes-ds (core: build/host/bsnes_libretro.so) and build/host/snes-harness"

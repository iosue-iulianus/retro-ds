#!/bin/sh
# RetroDS: fetch and build the libretro cores for the host, at pinned
# commits, into ../cores (beside the repo).
#
#   scripts/build-cores.sh            bsnes (the SNES core, patched) and
#                                     SwanStation (the PlayStation's)
#   scripts/build-cores.sh snes9x     also snes9x (local prototyping only:
#                                     its licence is not GPL-compatible, so
#                                     it is never distributed)
#   scripts/build-cores.sh android    both for Android (arm64-v8a, with the NDK
#                                     in ANDROID_NDK_HOME): ../cores/bsnes/android/libretro.so,
#                                     ../cores/swanstation/android/libretro.so
#
# bsnes gets scripts/cores/bsnes-memory.patch: libretro's bsnes exposes no
# memory through retro_get_memory_data, and RetroDS reads work RAM and loads
# and saves cartridge RAM itself (bsnes writes it only when the game is
# unloaded); video RAM too, read for maps assembled from the game's own
# screens (A Link to the Past). It also takes states for the same instance (rewind) without
# synchronizing, so they never move the emulation forward.
#
# SwanStation (GPLv3, its sources GPLv2 or later) is built as it is, with
# CMake: RetroDS asks for its software renderer through the core options
# (src/emu.c), so its GL and Vulkan code is never used.
set -eu

BSNES_URL=https://github.com/libretro/bsnes.git
BSNES_REV=63ee22f7af8f5d1ada295c2c98fd7bf261fa62a7
SWANSTATION_URL=https://github.com/libretro/swanstation.git
SWANSTATION_REV=631f0fb31b809e0f889d7c551fe695788e39c1d3
SNES9X_URL=https://github.com/libretro/snes9x.git
SNES9X_REV=204ea1c0b851f960cbf223075eb1c25641cb483c

HERE=$(cd "$(dirname "$0")/.." && pwd)
CORES=${RETRODS_CORES:-$HERE/../cores}
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
fetch swanstation "$SWANSTATION_URL" "$SWANSTATION_REV"
if [ "${1:-}" = android ]; then
	NDK="${ANDROID_NDK_HOME:?set ANDROID_NDK_HOME to an Android NDK}"
	J="$CORES/bsnes/bsnes/target-libretro"
	# (newer NDKs' clang rejects nall's own std::is_signed specialization:
	# -Winvalid-specialization, an error by default)
	"$NDK/ndk-build" -C "$J/jni" -j"$JOBS" APP_ABI=arm64-v8a APP_PLATFORM=android-26 NDK_PROJECT_PATH="$J" \
		APP_CPPFLAGS="-Wno-invalid-specialization -Wno-unknown-warning-option"
	mkdir -p "$CORES/bsnes/android"
	cp "$J/libs/arm64-v8a/libretro.so" "$CORES/bsnes/android/libretro.so"
	echo "bsnes (Android, arm64-v8a): $CORES/bsnes/android/libretro.so"
	B="$CORES/swanstation/build-android"
	cmake -S "$CORES/swanstation" -B "$B" -DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
		-DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26
	cmake --build "$B" --target swanstation_libretro -j"$JOBS"
	mkdir -p "$CORES/swanstation/android"
	cp "$B/swanstation_libretro_android.so" "$CORES/swanstation/android/libretro.so"
	echo "SwanStation (Android, arm64-v8a): $CORES/swanstation/android/libretro.so"
	exit 0
fi
make -C "$CORES/bsnes" -j"$JOBS" target=libretro binary=library
echo "bsnes: $CORES/bsnes/bsnes_libretro.so"
cmake -S "$CORES/swanstation" -B "$CORES/swanstation/build-host" -DCMAKE_BUILD_TYPE=Release > /dev/null
cmake --build "$CORES/swanstation/build-host" --target swanstation_libretro -j"$JOBS"
echo "SwanStation: $CORES/swanstation/build-host/swanstation_libretro.so"

if [ "${1:-}" = snes9x ]; then
	fetch snes9x "$SNES9X_URL" "$SNES9X_REV"
	make -C "$CORES/snes9x/libretro" -j"$JOBS"
	echo "snes9x: $CORES/snes9x/libretro/snes9x_libretro.so"
fi

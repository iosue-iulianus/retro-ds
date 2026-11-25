#!/bin/bash
# Builds the Android APK (arm64-v8a): dist/retrods-vX.Y.Z-android.apk.
#
# Needs: the Android SDK (ANDROID_HOME or ANDROID_SDK_ROOT) with an NDK
# (ANDROID_NDK_HOME, or ANDROID_NDK_LATEST_HOME as on GitHub's runners),
# Gradle 8.7+ and JDK 17, git, cmake, and patchelf (installed with pip if
# it's missing). SDL2 is fetched into ./deps, the bsnes and SwanStation
# cores into ../cores (scripts/build-cores.sh android). This runs on GitHub
# Actions (.github/workflows/android.yml).
#
# bsnes goes into the APK three times, as libsnes_a.so, libsnes_b.so and
# libsnes_c.so, each with its own soname: libretro cores keep their state in
# globals, so the game, the map's hidden copy and the logo's each load their
# own copy (src/emu.h). SwanStation (the PlayStation) goes in once, as
# libswanstation.so.
#
# Release signing: set RETRODS_KEYSTORE (path), RETRODS_KEYSTORE_PASSWORD,
# RETRODS_KEY_ALIAS, RETRODS_KEY_PASSWORD; without them the APK is debug-signed.
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$PWD"
DEPS="$ROOT/deps"
mkdir -p "$DEPS"
SDL_BRANCH=${SDL_BRANCH:-release-2.30.x}
CORES=${RETRODS_CORES:-$ROOT/../cores}

NDK="${ANDROID_NDK_HOME:-${ANDROID_NDK_LATEST_HOME:-}}"
if [ -z "$NDK" ] || [ ! -f "$NDK/build/cmake/android.toolchain.cmake" ]; then
	echo "set ANDROID_NDK_HOME to an Android NDK" >&2
	exit 1
fi
export ANDROID_NDK_HOME="$NDK"
# Any arm64 (ARMv8.0): not -mcpu for a newer core, which allows
# instructions older chips don't have.
CPU_FLAGS="-march=armv8-a"

# --- sources -----------------------------------------------------------------
if [ ! -d "$DEPS/SDL2" ]; then
	git clone --depth 1 -b "$SDL_BRANCH" https://github.com/libsdl-org/SDL.git "$DEPS/SDL2"
fi

# --- the cores: bsnes three times, SwanStation once ---------------------------
RETRODS_CORES="$CORES" scripts/build-cores.sh android
if ! command -v patchelf > /dev/null; then
	python3 -m pip install --quiet --user patchelf
	export PATH="$HOME/.local/bin:$PATH"
fi
JNI=android/app/src/main/jniLibs/arm64-v8a
rm -rf android/app/src/main/jniLibs && mkdir -p "$JNI"
for c in a b c; do
	cp "$CORES/bsnes/android/libretro.so" "$JNI/libsnes_$c.so"
	patchelf --set-soname "libsnes_$c.so" "$JNI/libsnes_$c.so"
done
cp "$CORES/swanstation/android/libretro.so" "$JNI/libswanstation.so"
patchelf --set-soname "libswanstation.so" "$JNI/libswanstation.so"

# --- APK ---------------------------------------------------------------------
# licences, inside the APK (assets/licenses)
VERSION=$(cat VERSION)
LIC=android/app/src/main/assets/licenses
rm -rf "$LIC" && mkdir -p "$LIC"
cp LICENSE "$LIC/RETRODS-LICENSE.txt"
cp "$CORES/bsnes/GPLv3.txt" "$LIC/bsnes-GPLv3.txt"
SW="$CORES/swanstation"
cp "$SW/LICENSE" "$LIC/swanstation-GPLv3.txt"
{
	echo "SwanStation includes these, under their own licences:"
	echo
	echo "- libchdr (BSD 3-clause), with LZMA SDK (public domain), miniz (MIT / Unlicense) and zstd (BSD, (c) Meta Platforms)"
	echo "- OpenBIOS from PCSX-Redux (MIT)"
	echo "- VIXL (BSD 3-clause, (c) ARM Limited)"
	echo "- xxHash (BSD 2-clause, (c) Yann Collet)"
	echo "- libretro-common (MIT, (c) the RetroArch team)"
	echo "- stb_image (MIT or public domain, (c) Sean Barrett)"
	echo "- glad (generated OpenGL loader) and the Vulkan headers (Apache 2.0, (c) The Khronos Group)"
	for f in libchdr/LICENSE.txt libchdr/deps/lzma-25.01/LICENSE openbios/LICENSE.txt vixl/LICENCE xxhash/LICENSE; do
		printf "\n\n==== %s ====\n\n" "$f"
		cat "$SW/dep/$f"
	done
} > "$LIC/swanstation-third-party.txt"
cp "$DEPS/SDL2/LICENSE.txt" "$LIC/SDL2-LICENSE.txt"
cp assets/fonts/Barlow-OFL.txt "$LIC/Barlow-OFL.txt"
cp third_party/rcheevos/LICENSE "$LIC/rcheevos-LICENSE.txt"
cp third_party/libwebp/COPYING "$LIC/libwebp-COPYING.txt"
cp third_party/libwebp/PATENTS "$LIC/libwebp-PATENTS.txt"
printf "RetroDS %s is free software under the GNU General Public License, version 3 only (RETRODS-LICENSE.txt).\nIts Super Nintendo emulator is bsnes (c) byuu and contributors, GPLv3 (bsnes-GPLv3.txt), libretro's port with RetroDS's changes:\nhttps://github.com/libretro/bsnes (commit %s; the changes in RetroDS's scripts/cores/)\nIts PlayStation emulator is SwanStation (c) Connor McLaughlin and contributors, GPLv3 (swanstation-GPLv3.txt; what it includes: swanstation-third-party.txt):\nhttps://github.com/libretro/swanstation (commit %s)\nSDL2 (c) Sam Lantinga, zlib licence: https://github.com/libsdl-org/SDL (%s)\nIts text is set in Barlow (c) The Barlow Project Authors, SIL Open Font License 1.1 (Barlow-OFL.txt).\nRetroAchievements support: rcheevos (c) RetroAchievements, MIT (rcheevos-LICENSE.txt), %s.\nWebP images: libwebp (c) Google Inc., BSD 3-clause (libwebp-COPYING.txt, libwebp-PATENTS.txt), v1.6.0.\nRetroDS source: https://github.com/iosue-iulianus/retro-ds\nBased on GBA DS by kyannon (GPLv3): https://github.com/kyannon/gba-ds\n" \
	"$VERSION" "$(git -C "$CORES/bsnes" rev-parse --short HEAD)" "$(git -C "$SW" rev-parse --short HEAD)" "$SDL_BRANCH" "$(tail -1 third_party/rcheevos/VERSION)" > "$LIC/README.txt"

gradle -p android --no-daemon assembleRelease -PsdlDir="$DEPS/SDL2" -PcpuFlags="$CPU_FLAGS"

mkdir -p dist
APK="dist/retrods-v$VERSION-android.apk"
rm -f dist/retrods-*-android.apk
cp android/app/build/outputs/apk/release/app-release.apk "$APK"
echo "built $APK"

#!/bin/sh
# The test frontend (lrtest) for the PC and for Android (arm64).
#   tools/gamecube/build.sh host      -> tools/gamecube/lrtest-host (GLES through Mesa's EGL)
#   tools/gamecube/build.sh android   -> tools/gamecube/lrtest-android (GLES or Vulkan; NDK in ANDROID_NDK_HOME)
# The PC build needs the GLES 3 headers, which Ubuntu's Mesa packages lack:
# GLES3_INCLUDE points at a folder holding GLES3/ (the NDK's sysroot has one).
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
SRC="$HERE/../../src"
case "${1:-}" in
host)
	INC=${GLES3_INCLUDE:-${ANDROID_NDK_HOME:?set GLES3_INCLUDE or ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include}
	mkdir -p "$HERE/.gles3/GLES3" && cp "$INC"/GLES3/*.h "$HERE/.gles3/GLES3/"
	cc -O2 -g -Wall -I"$SRC" -I"$HERE/.gles3" -o "$HERE/lrtest-host" "$HERE/lrtest.c" -ldl -lm -lEGL \
		"$(ls /usr/lib/x86_64-linux-gnu/libGLESv2.so* | head -1)"
	rm -rf "$HERE/.gles3"
	echo "$HERE/lrtest-host"
	;;
android)
	CC="${ANDROID_NDK_HOME:?set ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android26-clang"
	"$CC" -O2 -g -Wall -I"$SRC" -I"$HERE" -o "$HERE/lrtest-android" "$HERE/lrtest.c" -ldl -lm -lEGL -lGLESv3 -lvulkan -landroid -lmediandk
	echo "$HERE/lrtest-android"
	;;
*)
	echo "usage: $0 host|android" >&2
	exit 2
	;;
esac

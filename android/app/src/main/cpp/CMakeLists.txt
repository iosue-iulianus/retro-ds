# Native part of the Android app: libSDL2.so (built from source) and
# libmain.so (RetroDS itself, the same sources as the desktop build, plus
# src/android.c). The emulator isn't linked in: it's the libretro core
# scripts/build-android.sh puts beside them (libsnes_a.so ...), loaded at
# run time (src/emu.c).
#
# Set by android/app/build.gradle from Gradle properties:
#   RETRODS_SDL_SRC     SDL2 source tree
#   RETRODS_VERSION     version string
cmake_minimum_required(VERSION 3.18)
project(retrods C CXX)

get_filename_component(RETRODS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)
if(NOT RETRODS_SDL_SRC)
	message(FATAL_ERROR "RETRODS_SDL_SRC is not set (build with scripts/build-android.sh)")
endif()

# --- SDL2 (shared: SDLActivity loads libSDL2.so, then libmain.so) ---
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
add_subdirectory("${RETRODS_SDL_SRC}" SDL)

# --- rcheevos (RetroAchievements, MIT), vendored; the same files as the Makefile's RC_SRC ---
set(RC "${RETRODS_ROOT}/third_party/rcheevos")
file(GLOB RC_SRC "${RC}/src/*.c" "${RC}/src/rapi/*.c" "${RC}/src/rcheevos/*.c")
add_library(rcheevos STATIC ${RC_SRC} "${RC}/src/rhash/md5.c")
set_target_properties(rcheevos PROPERTIES C_STANDARD 11 C_EXTENSIONS ON)
target_include_directories(rcheevos PUBLIC "${RC}/include" PRIVATE "${RC}/src")

# --- libwebp's decoder (WebP logos, BSD), vendored; the same files as the Makefile's WEBP_SRC ---
set(WEBP "${RETRODS_ROOT}/third_party/libwebp")
file(GLOB WEBP_SRC "${WEBP}/src/dec/*.c" "${WEBP}/src/dsp/*.c" "${WEBP}/src/utils/*.c")
add_library(webpdec STATIC ${WEBP_SRC})
set_target_properties(webpdec PROPERTIES C_STANDARD 11 C_EXTENSIONS ON)
target_include_directories(webpdec PUBLIC "${WEBP}")

# --- RetroDS ---
set(S "${RETRODS_ROOT}/src")
add_library(main SHARED
	${S}/main.c ${S}/config.c ${S}/font.c ${S}/ui.c ${S}/library.c ${S}/game.c ${S}/game_sm.c ${S}/game_alttp.c ${S}/game_sotn.c ${S}/game_ff6.c ${S}/paths.c ${S}/image.c ${S}/mapper.c ${S}/hotkeys.c ${S}/folderpick.c
	${S}/brand_data.c ${S}/bezel.c ${S}/bezel_logo.c ${S}/logo.c ${S}/cheats.c ${S}/esde.c ${S}/textfont.c ${S}/fonts_data.c ${S}/i18n.c ${S}/i18n_data.c
	${S}/emu.c ${S}/system.c ${S}/disc.c ${S}/sotn.c ${S}/ff6.c ${S}/patch.c ${S}/mapcap.c ${S}/smmap.c ${S}/alttp.c ${S}/alttp_dungeon.c ${S}/profiles.c ${S}/ra.c ${S}/update.c ${S}/http.c ${S}/sha256.c
	${S}/android.c)
set_target_properties(main PROPERTIES C_STANDARD 11 C_EXTENSIONS ON)
target_compile_options(main PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
target_compile_definitions(main PRIVATE RETRODS_VERSION="${RETRODS_VERSION}" RETRODS_RA)
target_include_directories(main PRIVATE "${RC}/src")
target_link_libraries(main PRIVATE SDL2::SDL2 rcheevos webpdec z dl android log EGL m)

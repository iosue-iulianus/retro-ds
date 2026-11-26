#!/bin/bash
# The test frontend on the PC with a GameCube game: one script, start to finish.
#   tools/gamecube/run_host.sh CORE.so GAME.iso WORKDIR SCRIPT
# WORKDIR holds system/dolphin-emu/Sys (Dolphin's Data/Sys), system/User (made by
# the core: memory cards in system/User/GC/USA/Card A/) and out/ (screenshots).
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
CORE=$1 GAME=$2 WORK=$3 SCRIPT=$4
mkdir -p "$WORK/out"
cd "$WORK"
"$HERE/lrtest-host" "$CORE" "$GAME" "$WORK/system" out "$SCRIPT" dolphin_renderer=Hardware \
	dolphin_main_cpu_thread=enabled dolphin_efb_scale=1 dolphin_shader_compilation_mode=2 \
	dolphin_wait_for_shaders=disabled 2>"$WORK/err.log"

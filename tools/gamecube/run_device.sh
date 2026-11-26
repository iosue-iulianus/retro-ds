#!/bin/bash
# The test frontend kept running on an Android device over adb, fed through a
# named pipe: the game boots once, then each set of commands takes seconds.
#   tools/gamecube/run_device.sh push CORE.so SYS_DIR    the frontend, the core and Dolphin's Data/Sys
#   tools/gamecube/run_device.sh start GAME_ON_DEVICE    start it (LRENV="LRTEST_VK=1" for Vulkan)
#   tools/gamecube/run_device.sh do 'CMD' 'CMD' ...      send script lines, print their output when done
#   tools/gamecube/run_device.sh pull NAME... DIR        screenshots out/NAME.png into DIR
#   tools/gamecube/run_device.sh stop
# ADB (default adb) and ADB_SERIAL choose the device. It won't start while
# RetroDS or Dolphin is in front (someone is playing).
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ADB="${ADB:-adb}${ADB_SERIAL:+ -s $ADB_SERIAL}"
T=${LRTEST_DIR:-/data/local/tmp/lrtest}
host_path() { if command -v wslpath >/dev/null && [[ $ADB == *.exe* ]]; then wslpath -w "$1"; else echo "$1"; fi; }
case "${1:-}" in
push)
	$ADB shell "mkdir -p $T/out $T/system/dolphin-emu"
	$ADB push "$(host_path "$HERE/lrtest-android")" $T/lrtest >/dev/null
	$ADB push "$(host_path "$2")" $T/core.so >/dev/null
	$ADB push "$(host_path "$3")" $T/system/dolphin-emu/ >/dev/null
	$ADB shell "chmod 755 $T/lrtest; ls $T"
	;;
start)
	$ADB shell 'dumpsys activity activities | grep ResumedActivity:' | grep -q -e retrods -e dolphinemu &&
		{ echo "the device is in use (RetroDS or Dolphin in front): not starting"; exit 1; }
	$ADB shell "cd $T && rm -f cmd out/*.png out/live.log && mkfifo cmd && \
		(nohup env ${LRENV:-LRTEST_NONE=1} ./lrtest ./core.so '$2' $T/system out cmd dolphin_renderer=Hardware \
		dolphin_cpu_core=4 dolphin_main_cpu_thread=enabled dolphin_efb_scale=1 dolphin_shader_compilation_mode=2 \
		dolphin_wait_for_shaders=disabled > out/live.log 2> out/err.log < /dev/null &); sleep 2; ps -A | grep -c '[l]rtest'"
	;;
do)
	shift
	m=$RANDOM
	tmp=$(mktemp)
	printf '%s\n' "$@" "mark $m" > "$tmp"
	n=$($ADB shell "wc -l < $T/out/live.log" | tr -d '\r ')
	$ADB push "$(host_path "$tmp")" $T/cmds.txt >/dev/null
	rm -f "$tmp"
	$ADB shell "cat $T/cmds.txt > $T/cmd; while ! grep -q 'mark $m' $T/out/live.log; do sleep 0.5; \
		ps -A | grep -q '[l]rtest' || { echo 'lrtest has stopped'; tail -5 $T/out/err.log; break; }; done; \
		tail -n +$((n + 1)) $T/out/live.log | grep -v '^mark '"
	;;
pull)
	shift
	dir=${*: -1}
	for f in "${@:1:$#-1}"; do $ADB pull $T/out/$f.png "$(host_path "$dir/$f.png")" >/dev/null; done
	;;
stop)
	$ADB shell "echo quit > $T/cmd & sleep 3; for p in \$(ps -A -o PID,ARGS | grep '[l]rtest' | cut -c1-6); do kill \$p; done"
	;;
*)
	sed -n 2,12p "$0"
	exit 2
	;;
esac

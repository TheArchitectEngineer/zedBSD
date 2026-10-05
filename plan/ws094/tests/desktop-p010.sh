#!/bin/sh
# ws094-p010 (L4a): the desktop's long names and a desktop of another size, on the Venus guest of an image with Files
# (plan/ws102/tests/build-inset-image.sh's).  HOME is /tmp/dhome; its Desktop has notes.txt, a longer name, two names
# of about 40 characters (one in Japanese) and far.txt; the layout file keeps far.txt at column 16, row 8 (a place
# only a 1920-wide desktop has).  zdesktop --glass starts files --desktop.
#  1. 1920x1080: far.txt at its saved 16,8; the long names in two lines, the longest with their middle left out
#     (names-1920.png); one selected (selected.png).
#  2. 1280x800 (the guest started again with the smaller display, the same layout file): far.txt, past the 13 x 7 grid,
#     takes a free cell (ZFILES DESKTOP moved name=far.txt from=16,8 ... saved=1); the layout file is unchanged
#     (names-1280.png).
#  3. 1920x1080 again, with the layout file from step 2: far.txt back at 16,8 (back-1920.png).
# Prints "desktop-p010: PASS" or "desktop-p010: FAIL".
#
#   plan/ws094/tests/desktop-p010.sh IMAGE OUTDIR
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
image=$1
out=$2
mkdir -p "$out"
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws094-p010-run}"
export GUEST_RUNTIME
status=0
width=1920
height=1080
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
put() { timeout 120 python3 plan/tools/guest/guest.py put "$1" "$2" >/dev/null 2>&1 </dev/null; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py --width "$width" --height "$height" "$GUEST_RUNTIME/qmp.sock" "$@" || { echo "pointer: FAILED"; status=1; }; }
shot() { pointer move $((width - 5)) $((height / 2)) sleep 300 >/dev/null; python3 plan/ws035/tests/zdesktop-check.py "$out/$1" --runtime "$GUEST_RUNTIME" >/dev/null; echo "shot $out/$1"; }

# Fails the run unless zdesktop's log has a line matching a pattern (within a few seconds).
expect_log() {
	tries=0
	while [ $tries -lt 10 ]; do
		guest "grep -aqE '$1' /tmp/zdesktop.log && echo found" | grep -q '^found$' && { echo "log: $1 ok"; return 0; }
		tries=$((tries + 1))
		sleep 1
	done
	echo "log: $1 MISSING"
	status=1
	return 1
}

# The guest at a size, the Desktop folder, the layout file (a local file), and zdesktop with Files.
desktop() {
	width=$1
	height=$2
	sh plan/ws035/tests/zdesktop-guest.sh stop >/dev/null 2>&1
	VENUS_SIZE=${width}x$height timeout 180 sh plan/ws035/tests/zdesktop-guest.sh start "$image" >/dev/null 2>&1
	tries=0
	until guest 'echo up' | grep -q '^up$' || [ $tries -ge 30 ]; do tries=$((tries + 1)); sleep 5; done
	sleep 10
	guest 'service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)" | awk "{print \$1}"); do kill $p; done; sleep 1
rm -rf /tmp/dhome; mkdir -p /tmp/dhome/Desktop /tmp/dhome/.config/keiland; cd /tmp/dhome/Desktop
printf "n\n" > notes.txt; printf "b\n" > "Budget notes 2026.pdf"; printf "f\n" > a_forty_character_file_name_for_tests.txt
printf "j\n" > "長い名前のファイルの例です今日の会議の記録と末尾.txt"; printf "far\n" > far.txt; echo made' >/dev/null
	put "$3" /tmp/dhome/.config/keiland/desktop-layout
	guest "export XDG_RUNTIME_DIR=/tmp HOME=/tmp/dhome; rm -f /tmp/wayland-0; picture=; [ -f /usr/share/keiland/wallpaper.png ] && picture=--wallpaper=/usr/share/keiland/wallpaper.png
/bin/wayland --testing --timeout=900 --width=$width --height=$height --glass \$picture --desktop-client='/bin/files --desktop' > /tmp/zdesktop.log 2>&1 </dev/null &
i=0; while ! grep -aq 'ZFILES DESKTOP ready' /tmp/zdesktop.log && [ \$i -lt 60 ]; do sleep 0.5; i=\$((i+1)); done; sleep 2; echo started" >/dev/null
	expect_log "ZFILES DESKTOP grid width=$width height=$((height - 44)) columns=[0-9]+ rows=[0-9]+"
}

# 1. 1920x1080.
printf 'far.txt\t16\t8\n' > "$out/layout-start"
desktop 1920 1080 "$out/layout-start"
expect_log 'ZFILES DESKTOP place name=far.txt column=16 row=8 '
shot names-1920.png
set -- $(guest "grep -a 'ZFILES DESKTOP place name=a_forty' /tmp/zdesktop.log | tail -1" | sed -n 's/.* x=\([0-9]*\) y=\([0-9]*\).*/\1 \2/p')
pointer move $((${1:-0} + 46)) $((${2:-0} + 34 + 30)) sleep 300 down sleep 60 up sleep 800 >/dev/null
expect_log 'ZFILES DESKTOP select name=a_forty_character_file_name_for_tests.txt selected=1'
shot selected.png

# 2. 1280x800: far.txt moved to a free cell; the layout file unchanged.
desktop 1280 800 "$out/layout-start"
expect_log 'ZFILES DESKTOP moved name=far.txt from=16,8 to=[0-9]+,[0-9]+ saved=1'
expect_log 'ZFILES DESKTOP place name=far.txt column=[0-9] row=[0-6] '
shot names-1280.png
guest 'cat /tmp/dhome/.config/keiland/desktop-layout' > "$out/layout-after-1280"
grep -q "^far.txt	16	8$" "$out/layout-after-1280" && echo "layout file unchanged ok" || { echo "layout file changed FAIL"; cat "$out/layout-after-1280"; status=1; }

# 3. 1920x1080 again with that file.
desktop 1920 1080 "$out/layout-after-1280"
expect_log 'ZFILES DESKTOP place name=far.txt column=16 row=8 '
shot back-1920.png

guest "grep -aE 'ZFILES DESKTOP (grid|moved|place|ready)' /tmp/zdesktop.log" > "$out/zdesktop.log"
sh plan/ws035/tests/zdesktop-guest.sh stop >/dev/null 2>&1
[ $status -eq 0 ] && echo "desktop-p010: PASS" || echo "desktop-p010: FAIL"
exit $status

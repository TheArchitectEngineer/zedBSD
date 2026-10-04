#!/bin/sh
# ws071-p014: the titlebar's state and events of files on the host (files-render, host-render.c).
# Builds nothing: run host-build.sh first.  Each case runs files-render on a fresh sample home and
# checks the lines it prints:
#  1. The dashboard (Today since ws127-p011): one part (Today), nothing to go back to, no query, no progress, no focus asked.
#  2. The path's first part, Back and Forward; Home (the home folder since ws127-p011).
#  3. The search typed (changed), searched 150 ms later (query), cancelled (Esc: back, no query).
#  4. Icons/List, Preview; Ctrl+F, and Ctrl+L once the search field is left, ask for the keyboard
#     (focus 5, then 4, the count rising).
#  5. The path's field submitted goes to the folder; a path that is no folder is said so.
#  6. A copy running shows its progress (0..1001), and none once it is done.
#  7. home.png: the window without the toolbar (the panels from the top margin).
#  8. (ws127-p010) The path's last part edits the path; its suggestions a second after the typing rests.
#
#   plan/tools/files/host-p014.sh [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
out=${1:-build/ws071-p014-host}
mkdir -p "$out"
home=$(pwd)/build/ws071-host/home
status=0

# Runs files-render on a fresh home with the actions given; the output goes to $out/NAME.txt.
run() {
	name=$1
	shift
	sh plan/tools/files/host-run.sh --fresh "$@" > "$out/$name.txt" 2>&1
}

# Fails the run unless the output of a case has a line matching a pattern.
expect() {
	if grep -qE "$2" "$out/$1.txt"; then
		echo "$1: $2 ok"
	else
		echo "$1: $2 MISSING"
		status=1
	fi
}

# 1. The dashboard.
run dashboard titlebar
expect dashboard '^titlebar back=0 forward=0 parts=1 path=Today field=.*/home query= view=0 preview=0 progress=-1 focus=0 serial=0$'

# 2. The path, Back, Forward, Home.
run path "--start=$home/Documents" titlebar tb=activated:4:0 titlebar tb=activated:1:0 titlebar tb=activated:2:0 titlebar tb=activated:3:0 titlebar
expect path '^titlebar back=0 forward=0 parts=2 path=Home\|Documents field=.*/home/Documents '
expect path 'LOCATION kind=folder path=.*/home items=7 '
expect path '^titlebar back=1 forward=0 parts=1 path=Home '
expect path '^titlebar back=0 forward=1 parts=2 '
expect path '^titlebar back=1 forward=0 parts=1 path=Home .*serial=0$'
expect path 'LOCATION kind=folder path=.*/home items=7 error=0$'

# 3. The search typed, searched, cancelled.
run search "--start=$home/Documents" tb=changed:5:note titlebar wait=200 titlebar tb=done:5:1:note titlebar
expect search 'TITLEBAR kind=1 id=5 detail=0 text=note'
expect search 'SEARCH done query=note results=1 '
expect search '^titlebar back=1 forward=0 parts=1 path=Search .* query=note '
expect search 'TITLEBAR kind=2 id=5 detail=1 text=note'
expect search '^titlebar back=0 forward=1 parts=2 path=Home\|Documents .* query= '

# 4. The view, the preview, the keyboard asked for.
run view "--start=$home/Documents" tb=activated:7:0 tb=activated:8:0 titlebar key=33:2 titlebar tb=done:5:2: key=38:2 titlebar tb=activated:6:0 tb=activated:8:0 titlebar
expect view '^titlebar .* view=1 preview=1 progress=-1 focus=0 serial=0$'
expect view '^titlebar .* view=1 preview=1 progress=-1 focus=5 serial=1$'
expect view '^titlebar .* view=1 preview=1 progress=-1 focus=4 serial=2$'
expect view '^titlebar .* view=0 preview=0 progress=-1 focus=4 serial=2$'

# 5. The path's field.
run field "--start=$home/Documents" "tb=done:4:0:$home/Pictures" titlebar "tb=done:4:0:$home/Nothing" "tb=done:4:1:$home/Music"
expect field 'LOCATION kind=folder path=.*/home/Pictures items=4 '
expect field '^titlebar back=1 forward=0 parts=2 path=Home\|Pictures '
expect field 'MESSAGE No folder at .*/home/Nothing'
if grep -q 'LOCATION kind=folder path=.*/home/Music' "$out/field.txt"; then
	echo "field: cancelled went somewhere MISSING"
	status=1
else
	echo "field: cancelled stays ok"
fi

# 6. A copy's progress (every item of Documents, with a large file, into Downloads).
run progress-home titlebar
dd if=/dev/zero of="$home/Documents/Large.bin" bs=1048576 count=256 2>/dev/null
sh plan/tools/files/host-run.sh "--start=$home/Documents" key=30:2 key=46:2 "tb=done:4:0:$home/Downloads" key=47:2 titlebar $(for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do printf "wait=500 "; done) titlebar > "$out/progress.txt" 2>&1
expect progress '^titlebar .* progress=([0-9]|[1-9][0-9]+) '
expect progress 'TASK done id=[0-9]+ kind=copy state=done '
tail -1 "$out/progress.txt" | grep -q 'progress=-1 ' && echo "progress: gone when done ok" || { echo "progress: gone when done MISSING"; status=1; }

# 8. ws127-p010: a click on the path's last part makes it a field (focus=4), and a second after the typing rests
#    the folders that start as typed are suggested (not before), a leading ~ kept in the texts, none for a missing folder.
run suggest "--start=$home/Documents" tb=activated:4:1 titlebar "tb=changed:4:$home/P" wait=500 suggestions wait=600 suggestions "tb=changed:4:~/D" wait=1100 suggestions "tb=changed:4:~/Nope/x" wait=1100 suggestions
expect suggest '^titlebar back=0 forward=0 parts=2 path=Home\|Documents .* focus=4 serial=1$'
expect suggest '^suggest serial=0 count=0$'
expect suggest '^suggest serial=1 count=2$'
expect suggest '^suggest label=Pictures/ text=.*/home/Pictures/$'
expect suggest '^suggest label=Projects/ text=.*/home/Projects/$'
expect suggest '^suggest serial=2 count=3$'
expect suggest '^suggest label=Desktop/ text=~/Desktop/$'
expect suggest '^suggest label=Downloads/ text=~/Downloads/$'
expect suggest '^suggest serial=3 count=0$'

# 7. The window without the toolbar.
run draw "--start=$home/Documents" "draw=$out/home.ppm"
expect draw "drew $out/home.ppm"

[ $status = 0 ] && echo "host-p014: PASS" || echo "host-p014: FAIL"
exit $status

#!/bin/sh
# ws095-p008: Japanese typed through the input method into zdesktop's own field (Files' search in the title bar) and
# into Files' rename field, on the Venus guest (the IME image, plan/ws095/tests/build-ime-image.sh: the Files image
# with keiland-ime).  zdesktop --glass at 1280x800 starts /usr/libexec/keiland-ime; Files opens /tmp/p008 (one file,
# a.txt).  Judged by Files' and zdesktop's logs, the folder read over SSH and the screens (never the guest's console):
#  1. Ctrl+F (the title bar's search field), Alt+Space to Japanese, "nihongo", Space: the preedit in the field
#     (search-preedit.png); Enter: 日本語 committed, Files' query is 日本語 (search.png).
#  2. Esc (the search ends), Ctrl+A, F2 (rename a.txt), "kanji", Space, Enter: 漢字 committed into the name; Enter
#     again: the file is renamed to 漢字.txt (Files' RENAME line, and the folder's listing; renamed.png).
#
#   plan/ws095/tests/ime-guest.sh start IMAGE     (the guest must be up; GUEST_RUNTIME names its runtime)
#   plan/ws095/tests/ime-p008.sh [OUTDIR]          (default build/ws095-shots/p008)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws095-run}"
export GUEST_RUNTIME
out=${1:-build/ws095-shots/p008}
mkdir -p "$out"
status=0
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1; }
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@"; sleep 1.2; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
shot() { pointer move 1270 790 sleep 600; check "$out/$1" >/dev/null; echo "shot: $out/$1"; }
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|/bin/[f]iles|[k]eiland-ime" | awk "{print \$1}"); do kill -CONT $p 2>/dev/null; kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|/bin/[f]iles|[k]eiland-ime" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'

# Waits up to 10 s for a log line matching a pattern; fails the run without one.
expect_log() {
	i=0
	while [ $i -lt 20 ]; do
		found=$(guest "grep -cE '$2' $1" | tail -1)
		if [ "${found:-0}" -gt 0 ] 2>/dev/null; then
			echo "log: $2 ok"
			return 0
		fi
		sleep 0.5
		i=$((i+1))
	done
	echo "log: $2 MISSING"
	status=1
}

# Fails the run unless Files' log (read on the host, which knows UTF-8; the guest's grep takes no UTF-8 pattern) has a line.
expect_files_log() {
	timeout 60 python3 plan/tools/guest/guest.py get /tmp/f.log "$out/f.log" >/dev/null 2>&1
	if grep -qF "$1" "$out/f.log"; then
		echo "files: $1 ok"
		return 0
	fi
	echo "files: $1 MISSING"
	status=1
}

guest "$stop_all" >/dev/null
guest 'export XDG_RUNTIME_DIR=/tmp HOME=/root; rm -f /tmp/wayland-0 /root/.config/kei/ime/ja-user.dict; rm -rf /tmp/p008
mkdir -p /tmp/p008; printf "a\n" > /tmp/p008/a.txt
picture=; [ -f /usr/share/keiland/wallpaper.png ] && picture=--wallpaper=/usr/share/keiland/wallpaper.png
/bin/wayland --testing --timeout=900 --width=1280 --height=800 --glass $picture > /tmp/zdesktop.log 2>&1 </dev/null & echo started' >/dev/null
expect_log /tmp/zdesktop.log 'KEI-IME READY'
guest 'export XDG_RUNTIME_DIR=/tmp HOME=/root; /bin/files --token=f1 --timeout-s=800 --width=1000 --height=640 /tmp/p008 > /tmp/f.log 2>&1 </dev/null & sleep 6; echo started' >/dev/null
expect_log /tmp/f.log 'LOCATION kind=folder path=/tmp/p008'

# 1. 日本語 in the title bar's search field.
keys '<ctrl-f>'
keys '<alt-spc>'
expect_log /tmp/zdesktop.log 'KWL IME language=ja'
keys 'nihongo'
keys ' '
sleep 1
shot search-preedit.png
keys '\n'
sleep 1
expect_files_log 'query=日本語'
shot search.png

# 2. 漢字 into the rename field, then the rename.
keys '<esc>'
keys '<alt-spc>'
expect_log /tmp/zdesktop.log 'KWL IME language=direct'
keys '<ctrl-a>'
keys '<f2>'
keys '<alt-spc>'
keys 'kanji'
keys ' '
keys '\n'
sleep 1
shot rename-field.png
keys '<alt-spc>'
keys '\n'
sleep 1
expect_files_log 'RENAME from=/tmp/p008/a.txt to=/tmp/p008/漢字.txt'
guest 'ls /tmp/p008' > "$out/listing.txt"
if grep -qF '漢字.txt' "$out/listing.txt"; then
	echo "folder: 漢字.txt ok"
else
	echo "folder: $(cat "$out/listing.txt") FAIL"
	status=1
fi
shot renamed.png

guest "$stop_all" >/dev/null
echo "ime-p008: status=$status"
exit $status

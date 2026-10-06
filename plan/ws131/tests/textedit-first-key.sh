#!/bin/sh
# ws131-p016 (q807-i02): which keys reach the Text Editor right after it starts, on the Venus guest of ime-p007
# (plan/ws095/tests/build-ime-textedit-image.sh, plan/ws095/tests/ime-guest.sh start).  T1-248 saw ime-p007 lose its
# first key (Ctrl+End) after the editor moved to kl_app.  The editor opens /tmp/fk.txt ("Hello" and a newline, two lines):
#  1. ready.png: the editor as READY says (the status bar shows Ln 1, Col 1).
#  2. Ctrl+End at once (as ime-p007 does): end-1.png should show Ln 2, Col 1.
#  3. Ctrl+Home, then Ctrl+End again after 2 s: home-2.png Ln 1, Col 1, end-2.png Ln 2, Col 1.
#  4. A started again, with 3 s before the first key: late-end.png Ln 2, Col 1.
# zdesktop's and the editor's logs are fetched (zdesktop.log, te-1.log, te-2.log) for the focus and the text input's lines.
# Judged by the PNGs' status bar (the user looks); nothing here passes or fails by itself.
#
#   plan/ws131/tests/textedit-first-key.sh [OUTDIR]       (default build/ws131-first-key)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws095-run}"
export GUEST_RUNTIME
out=${1:-build/ws131-first-key}
mkdir -p "$out"
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1; }
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
shot() { pointer move 1270 790 sleep 600; check "$out/$1" >/dev/null; echo "shot: $out/$1"; }
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[t]extedit|[k]eiland-ime" | awk "{print \$1}"); do kill -CONT $p 2>/dev/null; kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|[t]extedit|[k]eiland-ime" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
stop_editor='for p in $(ps -A -o pid,args | grep "[t]extedit" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -q "[t]extedit" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
start_editor='export XDG_RUNTIME_DIR=/tmp HOME=/root; /bin/textedit --timeout-s=300 --width=900 --height=520 /tmp/fk.txt > /tmp/te.log 2>&1 </dev/null & echo started'

# Waits up to 10 s for a log line matching a pattern.
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
}

guest "$stop_all" >/dev/null
guest 'export XDG_RUNTIME_DIR=/tmp HOME=/root; rm -f /tmp/wayland-0
printf "Hello\n" > /tmp/fk.txt
picture=; [ -f /usr/share/keiland/wallpaper.png ] && picture=--wallpaper=/usr/share/keiland/wallpaper.png
/bin/wayland --testing --timeout=600 --width=1280 --height=800 --glass $picture > /tmp/zdesktop.log 2>&1 </dev/null & echo started' >/dev/null
expect_log /tmp/zdesktop.log 'KEI-IME READY'

# 1-3. The first key at once, as ime-p007 sends it, then the same keys later.
guest "$start_editor" >/dev/null
expect_log /tmp/te.log 'TEXTEDIT READY'
keys '<ctrl-end>'
sleep 1.2
shot end-1.png
keys '<ctrl-home>'
sleep 1.2
shot home-2.png
sleep 2
keys '<ctrl-end>'
sleep 1.2
shot end-2.png
timeout 60 python3 plan/tools/guest/guest.py get /tmp/te.log "$out/te-1.log" >/dev/null 2>&1

# 4. The editor again, its first key 3 s after READY.
guest "$stop_editor" >/dev/null
guest "$start_editor" >/dev/null
expect_log /tmp/te.log 'TEXTEDIT READY'
sleep 1
shot ready.png
sleep 2
keys '<ctrl-end>'
sleep 1.2
shot late-end.png
timeout 60 python3 plan/tools/guest/guest.py get /tmp/te.log "$out/te-2.log" >/dev/null 2>&1
timeout 60 python3 plan/tools/guest/guest.py get /tmp/zdesktop.log "$out/zdesktop.log" >/dev/null 2>&1

guest "$stop_all" >/dev/null
echo "textedit-first-key: done (judge the status bar of end-1, home-2, end-2, ready, late-end)"

#!/bin/sh
# ws074-p032: the form controls in browser's window on the Venus guest (build-browser-image.sh).  zdesktop runs at
# 1280x800 with --glass and the wallpaper.
#  1. form.html at 900x640: Tab into the first field, type "kei" (the caret and the ring show: typed.png), Enter
#     submits through the default button: the view follows results.html?name=kei&... (LINK and NAVIGATE lines).
#  2. With --amazon: https://www.amazon.co.jp/ at 1200x690 (a live request, then the search): PageDown, a click on
#     the search field (its place from the host build's --dump=layout of a saved capture, build/ws074-amazon/
#     top-local-noscript.html from amazon-capture.py, which must exist: an inline replaced box on a line, or since the header's flexbox
#     (ws074-p035) a flex item "block <input> X Y W H control text", aimed 20 px below its top), type "kei"
#     (amazon-typed.png), Enter: the view goes to /s/ref=nb_sb_noss
#     with field-keywords=kei and the results page is shown (amazon-results.png).
# Neither log may have an ERROR line.
#
#   plan/ws074/tests/browser-guest.sh start      (the Venus guest must be up)
#   plan/ws074/tests/browser-p032.sh [--amazon] [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws074-run}"
export GUEST_RUNTIME
amazon=0
if [ "${1:-}" = --amazon ]; then
	amazon=1
	shift
fi
out=${1:-build/ws074-p032}
mkdir -p "$out"
guest() { timeout 180 python3 plan/tools/guest/guest.py run "$1" 2>&1; }
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@"; sleep 1; }
pages=/usr/share/browser-tests
stop_all='for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[b]rowser" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|[b]rowser" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
status=0

# Fails the run unless a log has a line matching a pattern.
expect_log() {
	found=$(guest "grep -cE '$2' $1" | tail -1)
	if [ "${found:-0}" -gt 0 ] 2>/dev/null; then
		echo "log: $2 ok"
	else
		echo "log: $2 MISSING"
		status=1
	fi
}

# The scroll of the browser's last frame.
last_scroll() {
	guest "grep 'ZBROWSER FRAME' /tmp/b.log | tail -1" | sed -n 's/.*scroll=\([0-9]*\).*/\1/p' | tail -1
}

# Starts zdesktop and the browser on a page at a size, and finds where the page is on the screen (wx, wy).
start() {
	guest "$stop_all" >/dev/null
	guest 'export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0
picture=; [ -f /usr/share/keiland/wallpaper.ppm ] && picture=--wallpaper=/usr/share/keiland/wallpaper.ppm
/bin/wayland --timeout=900 --width=1280 --height=800 --glass $picture > /tmp/zdesktop.log 2>&1 </dev/null & sleep 4; echo started' >/dev/null
	guest "export XDG_RUNTIME_DIR=/tmp; /bin/browser --width=$2 --height=$3 $1 > /tmp/b.log 2>&1 </dev/null & sleep $4; echo started" >/dev/null
	set -- $(guest "grep 'ZWL MAP client=' /tmp/zdesktop.log | tail -1" | sed -n 's/.* x=\([-0-9]*\) y=\([-0-9]*\).*/\1 \2/p')
	wx=${1:-0}
	wy=${2:-0}
	echo "browser: window at $wx,$wy"
}

# Takes a picture with the pointer at a corner inside the window (the keyboard stays with it).
shot() {
	pointer move $((wx + 10)) $((wy + 10)) sleep 400
	check "$out/$1" >/dev/null
}

# Checks both logs for errors and keeps the browser's.
finish() {
	errors=$(guest "grep -c 'ERROR' /tmp/zdesktop.log" | tail -1)
	if [ "${errors:-1}" = 0 ]; then echo "zdesktop: no ERROR"; else echo "zdesktop: ERROR lines"; status=1; fi
	browser_errors=$(guest "grep -c 'ZBROWSER ERROR' /tmp/b.log" | tail -1)
	if [ "${browser_errors:-1}" = 0 ]; then echo "browser: no ERROR"; else echo "browser: ERROR lines"; status=1; fi
	guest "grep -v CONSOLE /tmp/b.log" > "$out/$1"
}

# 1. form.html: Tab, type, Enter.
start "$pages/form.html" 900 640 5
expect_log /tmp/b.log 'ZBROWSER READY width=900 height=640'
pointer move $((wx + 880)) $((wy + 620)) sleep 400
keys '<tab>'
keys kei
shot typed.png
keys '<ret>'
expect_log /tmp/b.log 'ZBROWSER LINK href=results.html\?name=kei&q=kei&secret=abc&hint=&agree=on&color=red&kind=books'
expect_log /tmp/b.log "ZBROWSER NAVIGATE path=$pages/results.html"
shot submitted.png
finish form.log

# 2. amazon.co.jp: the search field, typing, Enter, the results.
if [ $amazon = 1 ]; then
	f=build/ws035-fonts
	field=$(build/ws074-host/plain/browser --dump=layout --width=1200 --height=690 --font=$f/Inter.ttf \
	    --mono-font=$f/JetBrainsMono-Regular.ttf --fallback-font=$f/DroidSansFallbackFull.ttf build/ws074-amazon/top-local-noscript.html 2>/dev/null |
	    awk '$1 == "line" { y = $3; h = $5 } $1 == "replaced" && $NF == "text" { printf "%d %d\n", $2 + $4 / 2, y + h / 2; exit }
	        $1 == "block" && $2 == "<input>" && $NF == "text" { printf "%d %d\n", $3 + $5 / 2, $4 + 20; exit }')
	echo "amazon: the search field at $field in the page"
	start https://www.amazon.co.jp/ 1200 690 40
	expect_log /tmp/b.log 'ZBROWSER READY width=1200'
	pointer move $((wx + 1180)) $((wy + 670)) sleep 400
	keys '<pgdn>'
	echo "amazon: scrolled down to $(last_scroll)"
	# The field is in the header now (ws074-p035), so the page goes back up before the click.
	keys '<pgup>'
	scroll=$(last_scroll)
	set -- $field
	pointer move $((wx + $1)) $((wy + $2 - ${scroll:-0})) sleep 300 down sleep 60 up sleep 800
	keys kei
	shot amazon-typed.png
	keys '<ret>'
	# The results page draws its 2 MB of stylesheets as they arrive; on the guest it is styled after 30 to 90 s (p035).
	sleep 90
	expect_log /tmp/b.log 'ZBROWSER NAVIGATE path=https://www.amazon.co.jp/s/ref=nb_sb_noss\?.*field-keywords=kei'
	shot amazon-results.png
	finish amazon.log
fi

guest "$stop_all" >/dev/null
echo "browser-p032: status $status ($out)"
exit $status

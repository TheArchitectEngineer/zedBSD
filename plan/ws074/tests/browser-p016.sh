#!/bin/sh
# ws074-p016: browser opens pages over HTTP in its window on the Venus guest (build-browser-image.sh).
# plan/ws074/tests/http-server.py runs on the host (0.0.0.0:PORT); the guest reaches it as 10.0.2.2.
# zdesktop runs at 1280x800 with --glass and the wallpaper; the browser opens first.html over http at 900x640.
# Checks, from the browser's ZBROWSER lines and zdesktop's log:
#  1. http-first.png: http://10.0.2.2:PORT/pages/first.html is shown (NAVIGATE with its URL and title).
#  2. http-link.png: a click on "link to the second page" opens second.html over http (the relative link
#     resolved against the http URL).
#  3. The titlebar's Back returns to first.html over http.
#  4. http-cookies.png: the location /cookie/set (redirected, with cookies) ends at /cookie/echo.
#  5. Neither log has an ERROR line.
# The link's place comes from the host build's --dump=layout of first.html at the same width.
#
#   plan/ws074/tests/browser-guest.sh start      (the Venus guest must be up)
#   plan/ws074/tests/browser-p016.sh [OUTDIR [PORT]]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws074-run}"
export GUEST_RUNTIME
out=${1:-build/ws074-p016}
port=${2:-8074}
mkdir -p "$out"
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1; }
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
site=http://10.0.2.2:$port
stop_all='for p in $(ps -A -o pid,args | grep -E "[z]desktop( |$)|[z]desktop-browser" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[z]desktop( |$)|[z]desktop-browser" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
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

# Fails the run unless the browser's last line of a kind matches.
expect_last() {
	last=$(guest "grep 'ZBROWSER $1 ' /tmp/b.log | tail -1")
	if printf '%s\n' "$last" | grep -qE "$2"; then
		echo "last $1: $2 ok"
	else
		echo "last $1: $2 MISSING (got: $last)"
		status=1
	fi
}

# A titlebar control's middle, from zdesktop's latest log line for it: "x y".
control_at() {
	guest "grep -E 'ZWL TITLEBAR control client=[0-9]+ .* where=floating id=$1 ' /tmp/zdesktop.log | tail -1" |
	    sed -n 's/.* x=\([-0-9]*\) y=\([-0-9]*\) width=\([0-9]*\) height=\([0-9]*\).*/\1 \2 \3 \4/p' |
	    { read x y w h; echo $(( ${x:-0} + ${w:-0} / 2 )) $(( ${y:-0} + ${h:-0} / 2 )); }
}

# Clicks a titlebar control.
control() {
	set -- $(control_at "$1")
	pointer move $(($1 - 2)) "$2" sleep 150 move "$1" "$2" sleep 300 down sleep 60 up sleep 1500
}

# Takes a picture with the pointer out of the way.
shot() {
	pointer move 1270 790 sleep 400
	check "$out/$1" >/dev/null
}

# The test server on the host.
python3 plan/ws074/tests/http-server.py --port "$port" > "$out/server.log" 2>&1 &
server=$!
sleep 1

# The link's middle in the page at 900 wide, from the host's layout: "x y".
f=build/ws035-fonts
link=$(build/ws074-host/plain/browser --dump=layout --width=900 --height=640 --font=$f/Inter.ttf \
    --mono-font=$f/JetBrainsMono-Regular.ttf --fallback-font=$f/DroidSansFallbackFull.ttf plan/ws074/tests/pages/first.html |
    awk '$1 == "line" { y = $3; h = $5 } $1 == "text" && $NF == "\"link\"" { printf "%d %d\n", $2 + $4 / 2, y + h / 2; exit }')
echo "link: at $link in the page"

guest "$stop_all" >/dev/null
guest 'export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0
picture=; [ -f /usr/share/keiland/wallpaper.ppm ] && picture=--wallpaper=/usr/share/keiland/wallpaper.ppm
/bin/wayland --timeout=900 --width=1280 --height=800 --glass $picture > /tmp/zdesktop.log 2>&1 </dev/null & sleep 4; echo started' >/dev/null
guest "export XDG_RUNTIME_DIR=/tmp; /bin/browser --width=900 --height=640 $site/pages/first.html > /tmp/b.log 2>&1 </dev/null & sleep 5; echo started" >/dev/null
set -- $(guest "grep 'ZWL MAP client=' /tmp/zdesktop.log | tail -1" | sed -n 's/.* x=\([-0-9]*\) y=\([-0-9]*\).*/\1 \2/p')
wx=${1:-0}; wy=${2:-0}
echo "browser: window at $wx,$wy"

# 1. The first page over http.
expect_log /tmp/b.log 'ZBROWSER READY width=900 height=640'
expect_last NAVIGATE "path=$site/pages/first.html title=browser: the first page"
shot http-first.png

# 2. The link, relative to the http URL.
set -- $link
pointer move $((wx + $1 - 2)) $((wy + $2)) sleep 150 move $((wx + $1)) $((wy + $2)) sleep 300 down sleep 60 up sleep 2000
expect_log /tmp/b.log 'ZBROWSER LINK href=second.html'
expect_last NAVIGATE "path=$site/pages/second.html title=browser: the second page"
expect_last TITLEBAR 'back=1 forward=0'
shot http-link.png

# 3. Back.
control 1
expect_last NAVIGATE "path=$site/pages/first.html "

# 4. The location: a redirect that sets cookies, then the page that shows them.
pointer move $((wx + 450)) $((wy + 300)) sleep 300
keys '<ctrl-l>'
sleep 1
keys '<ctrl-a>' "$site/cookie/set"
keys '<ret>'
sleep 2
expect_last NAVIGATE "path=$site/cookie/echo title=cookies"
shot http-cookies.png

# 5. No error in either log, then everything ends.
errors=$(guest "grep -c 'ERROR' /tmp/zdesktop.log" | tail -1)
if [ "${errors:-1}" = 0 ]; then echo "zdesktop: no ERROR"; else echo "zdesktop: ERROR lines"; status=1; fi
browser_errors=$(guest "grep -c 'ZBROWSER ERROR' /tmp/b.log" | tail -1)
if [ "${browser_errors:-1}" = 0 ]; then echo "browser: no ERROR"; else echo "browser: ERROR lines"; status=1; fi
guest "$stop_all" >/dev/null
kill $server
echo "browser-p016: status $status (pictures in $out)"
exit $status

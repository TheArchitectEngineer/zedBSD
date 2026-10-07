#!/bin/sh
# ws074-p045: browser's titlebar controls, links and history on the Venus guest
# (build-browser-image.sh).  zdesktop runs at 1280x800 with --glass and the wallpaper; the browser
# opens first.html at 900x640.  Checks, from the browser's ZBROWSER lines and zdesktop's log:
#  1. titlebar.png: the titlebar holds back, forward, reload and the location (TITLEBAR back=0 forward=0).
#  2. link.png: a click on "link to the second page" opens second.html (LINK, NAVIGATE, back=1).
#  3. The titlebar's Back returns to first.html (forward=1), Forward goes to second.html again.
#  4. location.png: Ctrl+L, the path of blocks.html typed and Enter open blocks.html.
#  5. Reload (F5) opens the same page again; Alt+Left goes back to second.html.
#  6. zdesktop's log has no ERROR line.
# The link's place comes from the host build's --dump=layout of first.html at the same width.
#
#   plan/ws074/tests/browser-guest.sh start      (the Venus guest must be up)
#   plan/ws074/tests/browser-p045.sh [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws074-run}"
export GUEST_RUNTIME
out=${1:-build/ws074-p045}
mkdir -p "$out"
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1; }
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
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

# Fails the run unless the browser's last NAVIGATE and TITLEBAR lines match.
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
	guest "grep -E 'KWL TITLEBAR control client=[0-9]+ .* where=floating id=$1 ' /tmp/zdesktop.log | tail -1" |
	    sed -n 's/.* x=\([-0-9]*\) y=\([-0-9]*\) width=\([0-9]*\) height=\([0-9]*\).*/\1 \2 \3 \4/p' |
	    { read x y w h; echo $(( ${x:-0} + ${w:-0} / 2 )) $(( ${y:-0} + ${h:-0} / 2 )); }
}

# Clicks a titlebar control.
control() {
	set -- $(control_at "$1")
	pointer move $(($1 - 2)) "$2" sleep 150 move "$1" "$2" sleep 300 down sleep 60 up sleep 1200
}

# Takes a picture with the pointer out of the way.
shot() {
	pointer move 1270 790 sleep 400
	check "$out/$1" >/dev/null
}

# The link's middle in the page at 900 wide, from the host's layout: "x y".
# The host build it comes from (plan/ws074/tests/host-build.sh), made when the worktree has none.
[ -x build/ws074-host/plain/browser ] || sh plan/ws074/tests/host-build.sh plain >/dev/null ||
    { echo "FAIL: the host build of browser"; exit 1; }
f=userland/desktop/fonts
link=$(build/ws074-host/plain/browser --dump=layout --width=900 --height=640 --font=$f/Mahora-Regular.ttf \
    --mono-font=$f/JetBrainsMono-Regular.ttf --fallback-font=$f/DroidSansFallbackFull.ttf plan/ws074/tests/pages/first.html |
    awk '$1 == "line" { y = $3; h = $5 } $1 == "text" && $NF == "\"link\"" { printf "%d %d\n", $2 + $4 / 2, y + h / 2; exit }')
echo "link: at $link in the page"
[ -n "$link" ] || { echo "FAIL: the link's place in the page"; exit 1; }

guest "$stop_all" >/dev/null
guest 'export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0
picture=; [ -f /usr/share/keiland/wallpaper.png ] && picture=--wallpaper=/usr/share/keiland/wallpaper.png
/bin/wayland --testing --timeout=900 --width=1280 --height=800 --glass $picture > /tmp/zdesktop.log 2>&1 </dev/null & sleep 4; echo started' >/dev/null
guest "export XDG_RUNTIME_DIR=/tmp; /bin/browser --width=900 --height=640 $pages/first.html > /tmp/b.log 2>&1 </dev/null & sleep 5; echo started" >/dev/null
set -- $(guest "grep 'KWL MAP client=' /tmp/zdesktop.log | tail -1" | sed -n 's/.* x=\([-0-9]*\) y=\([-0-9]*\).*/\1 \2/p')
wx=${1:-0}; wy=${2:-0}
echo "browser: window at $wx,$wy"

# 1. The titlebar's controls.
expect_log /tmp/b.log 'ZBROWSER READY width=900 height=640'
expect_last TITLEBAR "back=0 forward=0 path=$pages/first.html"
expect_log /tmp/zdesktop.log 'KWL TITLEBAR control client=[0-9]+ .* where=floating id=4 '
shot titlebar.png

# 2. The link.
set -- $link
pointer move $((wx + $1 - 2)) $((wy + $2)) sleep 150 move $((wx + $1)) $((wy + $2)) sleep 300 down sleep 60 up sleep 1500
expect_log /tmp/b.log 'ZBROWSER LINK href=second.html'
expect_last NAVIGATE "path=$pages/second.html title=browser: the second page"
expect_last TITLEBAR 'back=1 forward=0'
shot link.png

# 3. Back and Forward in the titlebar.
control 1
expect_last NAVIGATE "path=$pages/first.html "
expect_last TITLEBAR 'back=0 forward=1'
control 2
expect_last NAVIGATE "path=$pages/second.html "
expect_last TITLEBAR 'back=1 forward=0'

# 4. The location: Ctrl+L, a path, Enter.
pointer move $((wx + 450)) $((wy + 300)) sleep 300
keys '<ctrl-l>'
sleep 1
expect_log /tmp/zdesktop.log 'KWL TITLEBAR focus client=[0-9]+ surface=[0-9]+ id=4 edit=1'
keys '<ctrl-a>' "$pages/blocks.html"
shot location.png
keys '<ret>'
sleep 2
expect_last NAVIGATE "path=$pages/blocks.html title=browser: block and inline layout"
expect_last TITLEBAR 'back=1 forward=0'
shot blocks.png

# 5. Reload, then Alt+Left.
pointer move $((wx + 450)) $((wy + 300)) sleep 300
count=$(guest "grep -c 'ZBROWSER NAVIGATE path=$pages/blocks.html' /tmp/b.log" | tail -1)
keys '<f5>'
sleep 2
again=$(guest "grep -c 'ZBROWSER NAVIGATE path=$pages/blocks.html' /tmp/b.log" | tail -1)
if [ "${again:-0}" -gt "${count:-0}" ] 2>/dev/null; then echo "reload: ok"; else echo "reload: no new NAVIGATE"; status=1; fi
keys '<alt-left>'
sleep 2
expect_last NAVIGATE "path=$pages/second.html "

# 6. No error in zdesktop's log, then everything ends.
errors=$(guest "grep -c 'ERROR' /tmp/zdesktop.log" | tail -1)
if [ "${errors:-1}" = 0 ]; then echo "zdesktop: no ERROR"; else echo "zdesktop: ERROR lines"; status=1; fi
browser_errors=$(guest "grep -c 'ZBROWSER ERROR' /tmp/b.log" | tail -1)
if [ "${browser_errors:-1}" = 0 ]; then echo "browser: no ERROR"; else echo "browser: ERROR lines"; status=1; fi
guest "$stop_all" >/dev/null
echo "browser-p045: status $status (pictures in $out)"
exit $status

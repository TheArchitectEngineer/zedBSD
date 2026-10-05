#!/bin/sh
# ws074-p056: the keys, the focus and the pointer in browser's window on the Venus guest (build-browser-image.sh),
# as the DOM's events with the engine's default actions.  zdesktop runs at 1280x800 with --glass and the wallpaper;
# the browser opens keys.html at 900x640, whose listeners write what they get to the console (CONSOLE lines).
# Checks, from the browser's ZBROWSER lines and zdesktop's log:
#  1. A letter: keydown, keypress and keyup with the DOM's key, code and keyCode.
#  2. Tab: the focus goes to tabindex 1, 2, then the first link and the button (focusin lines); focus.png shows the
#     ring.  One more Tab reaches the last link below the tall block, which is scrolled into view (FRAME scroll > 0,
#     focus-scrolled.png).
#  3. Enter on the focused link follows it (LINK href=blocks.html, NAVIGATE blocks.html); Alt+Left comes back.
#  4. ArrowDown scrolls 40 pixels; Shift+ArrowDown, which the page cancels, does not; the wheel reaches the page.
#  5. A click on the first link: mousedown, mouseup and click reach the page, and the link is followed; Alt+Left back.
#  6. Ctrl+Q closes the window; neither log has an ERROR line.
# The first link's place comes from the host build's --dump=layout of keys.html at the same width.
#
#   plan/ws074/tests/browser-guest.sh start      (the Venus guest must be up)
#   plan/ws074/tests/browser-p056.sh [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws074-run}"
export GUEST_RUNTIME
out=${1:-build/ws074-p056}
mkdir -p "$out"
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1; }
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

# Takes a picture with the pointer at the window's bottom right corner, inside it (the keyboard stays with it).
shot() {
	pointer move $((wx + 880)) $((wy + 620)) sleep 400
	check "$out/$1" >/dev/null
}

# The middle of the first link's first word in the page at 900 wide, from the host's layout: "x y".
f=userland/desktop/fonts
link=$(build/ws074-host/plain/browser --dump=layout --width=900 --height=640 --font=$f/Inter.ttf \
    --mono-font=$f/JetBrainsMono-Regular.ttf --fallback-font=$f/DroidSansFallbackFull.ttf plan/ws074/tests/pages/keys.html 2>/dev/null |
    awk '$1 == "line" { y = $3; h = $5 } $1 == "text" && $NF == "\"The\"" { printf "%d %d\n", $2 + $4 / 2, y + h / 2; exit }')
echo "link: at $link in the page"

guest "$stop_all" >/dev/null
guest 'export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0
picture=; [ -f /usr/share/keiland/wallpaper.png ] && picture=--wallpaper=/usr/share/keiland/wallpaper.png
/bin/wayland --testing --timeout=900 --width=1280 --height=800 --glass $picture > /tmp/zdesktop.log 2>&1 </dev/null & sleep 4; echo started' >/dev/null
guest "export XDG_RUNTIME_DIR=/tmp; /bin/browser --width=900 --height=640 $pages/keys.html > /tmp/b.log 2>&1 </dev/null & sleep 5; echo started" >/dev/null
set -- $(guest "grep 'ZWL MAP client=' /tmp/zdesktop.log | tail -1" | sed -n 's/.* x=\([-0-9]*\) y=\([-0-9]*\).*/\1 \2/p')
wx=${1:-0}; wy=${2:-0}
echo "browser: window at $wx,$wy"
expect_log /tmp/b.log 'ZBROWSER READY width=900 height=640'

# The pointer over the window gives it the keyboard.
pointer move $((wx + 880)) $((wy + 620)) sleep 400

# 1. A letter.
keys x
expect_log /tmp/b.log 'ZBROWSER CONSOLE level=0 keydown key=x code=KeyX keyCode=88 shift=false ctrl=false alt=false repeat=false'
expect_log /tmp/b.log 'ZBROWSER CONSOLE level=0 keypress key=x'
expect_log /tmp/b.log 'ZBROWSER CONSOLE level=0 keyup key=x code=KeyX'

# 2. Tab through the page: tabindex 1, tabindex 2, the first link, the button, then the last link below.
keys '<tab>'
expect_log /tmp/b.log 'ZBROWSER CONSOLE level=0 focusin three'
shot focus.png
keys '<tab>' '<tab>'
expect_log /tmp/b.log 'ZBROWSER CONSOLE level=0 focusin two'
expect_log /tmp/b.log 'ZBROWSER CONSOLE level=0 focusin one'
shot focus-link.png
keys '<tab>' '<tab>'
expect_log /tmp/b.log 'ZBROWSER CONSOLE level=0 focusin four'
expect_log /tmp/b.log 'ZBROWSER CONSOLE level=0 focusin last'
scrolled=$(last_scroll)
echo "focus: the last link scrolled the page to ${scrolled:-?}"
if [ "${scrolled:-0}" -gt 0 ] 2>/dev/null; then echo "focus: scrolled into view ok"; else echo "focus: not scrolled into view"; status=1; fi
shot focus-scrolled.png

# 3. Enter follows the focused link; Alt+Left comes back.
keys '<ret>'
expect_log /tmp/b.log 'ZBROWSER CONSOLE level=0 click target=last'
expect_log /tmp/b.log 'ZBROWSER LINK href=blocks.html'
expect_log /tmp/b.log "ZBROWSER NAVIGATE path=$pages/blocks.html"
keys '<alt-left>'
back=$(guest "grep 'ZBROWSER NAVIGATE' /tmp/b.log | tail -1" | tail -1)
case $back in *keys.html*) echo "history: Alt+Left ok" ;; *) echo "history: Alt+Left went to $back"; status=1 ;; esac

# 4. ArrowDown scrolls; Shift+ArrowDown is canceled by the page; the wheel reaches the page.
keys '<down>'
down=$(last_scroll)
if [ "${down:-0}" = 40 ]; then echo "scroll: ArrowDown ok"; else echo "scroll: ArrowDown went to ${down:-?}"; status=1; fi
keys '<shift-down>'
expect_log /tmp/b.log 'ZBROWSER CONSOLE level=0 keydown key=ArrowDown code=ArrowDown keyCode=40 shift=true'
canceled=$(last_scroll)
if [ "${canceled:-0}" = 40 ]; then echo "scroll: the canceled key ok"; else echo "scroll: the canceled key went to ${canceled:-?}"; status=1; fi
pointer move $((wx + 450)) $((wy + 320)) sleep 300 wheel-down sleep 800
expect_log /tmp/b.log 'ZBROWSER CONSOLE level=0 wheel deltaY='
keys '<home>'

# 5. A click on the first link.
set -- $link
pointer move $((wx + $1 - 2)) $((wy + $2)) sleep 150 move $((wx + $1)) $((wy + $2)) sleep 300 down sleep 60 up sleep 1200
expect_log /tmp/b.log 'ZBROWSER CONSOLE level=0 mousedown button=0 target=one'
expect_log /tmp/b.log 'ZBROWSER CONSOLE level=0 click target=one'
expect_log /tmp/b.log 'ZBROWSER LINK href=first.html'
expect_log /tmp/b.log "ZBROWSER NAVIGATE path=$pages/first.html"
shot clicked-first.png
keys '<alt-left>'
back=$(guest "grep 'ZBROWSER NAVIGATE' /tmp/b.log | tail -1" | tail -1)
case $back in *keys.html*) echo "history: back from the click ok" ;; *) echo "history: back went to $back"; status=1 ;; esac

# 6. Ctrl+Q closes the window; no error in either log.
keys '<ctrl-q>'
sleep 3
left=$(guest "ps -A -o args | grep -c '[b]rowser'" | tail -1)
if [ "${left:-1}" = 0 ]; then echo "close: ok"; else echo "close: the browser is still running"; status=1; fi
errors=$(guest "grep -c 'ERROR' /tmp/zdesktop.log" | tail -1)
if [ "${errors:-1}" = 0 ]; then echo "zdesktop: no ERROR"; else echo "zdesktop: ERROR lines"; status=1; fi
browser_errors=$(guest "grep -c 'ZBROWSER ERROR' /tmp/b.log" | tail -1)
if [ "${browser_errors:-1}" = 0 ]; then echo "browser: no ERROR"; else echo "browser: ERROR lines"; status=1; fi
guest "cat /tmp/b.log" > "$out/b.log"
guest "$stop_all" >/dev/null
echo "browser-p056: status $status (pictures in $out)"
exit $status

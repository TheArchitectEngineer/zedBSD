#!/bin/sh
# ws128-p006: Terminal's Edit > Find, View > Theme and the kept font size on the Venus guest (any zdesktop image with
# the terminal, e.g. plan/ws128/tests/config-amd64-imageview.mk; start the guest first).  zdesktop at 1280x800.
#  1. A fresh terminal (no terminal.conf): MENU ready items=39; 120 lines "row N" written.
#  2. Ctrl+Shift+F (the menu's shortcut), "row 42": SEARCH found=1, the view back in the scrollback (view>0):
#     search.png (the bar on the last row, the match marked).  Enter: no older match (found=0, the match kept).
#     Esc: SEARCH closed.
#  3. View > Theme > Light: THEME theme=1 saved=0, terminal.conf has theme=1: light.png.
#  4. A second terminal reads it (SETTINGS ... theme=1).  Ctrl+- zooms out (ZOOM pixels=14) and terminal.conf keeps
#     font-size=14; a third terminal starts at 14 (MENU state ... pixels=14).  Ctrl+0 goes back to 16, which leaves
#     no font-size line.  The file is removed at the end.
#   plan/ws128/tests/terminal-p006-guest.sh [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws071-run}"
export GUEST_RUNTIME
out=${1:-build/ws128-p006-guest}
mkdir -p "$out"
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1; }
. plan/tools/guest/zwl-clients.sh
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[t]erminal|[f]iles" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|[t]erminal" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
status=0

# Fails the run unless a log has a line matching a pattern (within a few seconds).
expect_log() {
	tries=0
	found=0
	while [ $tries -lt 5 ]; do
		found=$(guest "grep -cE '$2' $1" | tail -1)
		[ "${found:-0}" -gt 0 ] 2>/dev/null && break
		tries=$((tries + 1))
		sleep 1
	done
	if [ "${found:-0}" -gt 0 ] 2>/dev/null; then
		echo "log: $2 ok"
	else
		echo "log: $2 MISSING"
		status=1
	fi
}
expect_guest() {
	if guest "$1 && echo YES" | grep -q YES; then echo "guest: $2 ok"; else echo "guest: $2 FAILED"; status=1; fi
}

# The centre (x) of a top-level item of a client's floating bar; the middle (y) of a popup row; the latest popup's left edge.
item_x() {
	guest "grep 'MENU bar client=$(zwl_app_client $1) .* where=floating item=$2 ' /tmp/zdesktop.log | tail -1" |
	    sed -n 's/.* offset=\([-0-9]*\) top=[-0-9]* width=\([0-9]*\).*/\1 \2/p' | { read offset width; echo $(( ${3:-0} + ${offset:-0} + ${width:-0} / 2 )); }
}
row_y() {
	guest "grep 'MENU row item=$1 ' /tmp/zdesktop.log | tail -1" | sed -n 's/.* y=\([0-9]*\) height=\([0-9]*\).*/\1 \2/p' | { read y h; echo $(( ${y:-0} + ${h:-0} / 2 )); }
}
popup_x() {
	guest "grep 'MENU open ' /tmp/zdesktop.log | tail -1" | sed -n 's/.* x=\([0-9]*\) y=.*/\1/p'
}
click() {
	pointer move $(($1 - 2)) "$2" sleep 150 move "$1" "$2" sleep 300 down sleep 60 up sleep "${3:-700}"
}

# Starts a terminal with a token and finds its window (client number $2).
start_terminal() {
	guest "export XDG_RUNTIME_DIR=/tmp; /bin/terminal --token=$1 --timeout-s=600 >> /tmp/t.log 2>&1 </dev/null & sleep 6; echo started" >/dev/null
	set -- $(guest "grep 'KWL MAP client=$(zwl_app_client $2) ' /tmp/zdesktop.log | tail -1" | sed -n 's/.* surface=\([0-9]*\) x=\([-0-9]*\) y=\([-0-9]*\).*/\1 \2 \3/p')
	wx=${2:-0}; wy=${3:-0}; bar=$((wy - 30))
	echo "terminal: surface ${1:-0} at $wx,$wy"
}

guest "$stop_all" >/dev/null
guest 'rm -f $HOME/.config/keiland/terminal.conf /tmp/t.log; i=0; : > /tmp/rows.txt; while [ $i -lt 120 ]; do echo "row $i" >> /tmp/rows.txt; i=$((i+1)); done; echo made' >/dev/null
guest 'export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0; picture=; [ -f /usr/share/keiland/wallpaper.png ] && picture=--wallpaper=/usr/share/keiland/wallpaper.png
/bin/wayland --testing --timeout=900 --width=1280 --height=800 --glass $picture > /tmp/zdesktop.log 2>&1 </dev/null & sleep 4; echo started' >/dev/null

# 1. A fresh terminal with 120 lines.
start_terminal t1 1
expect_log /tmp/t.log 'ZTERM SETTINGS run=t1 ambiguous_wide=0 font_size=16 theme=0'
expect_log /tmp/t.log 'ZTERM MENU ready items=39'
keys 'cat /tmp/rows.txt' '\n'
sleep 1.5

# 2. Find.
keys '<ctrl-shift-f>'
sleep 1
keys 'row 42'
sleep 1.5
expect_log /tmp/t.log 'ZTERM SEARCH run=t1 query=row 42 found=1 line=[0-9]+ column=0 cells=6 view=[1-9]'
pointer move 1250 780 sleep 400
check "$out/search.png" >/dev/null
keys '<ret>'
sleep 1
expect_log /tmp/t.log 'ZTERM SEARCH run=t1 query=row 42 found=0'
keys '<esc>'
sleep 1
expect_log /tmp/t.log 'ZTERM SEARCH run=t1 closed'

# 3. View > Theme > Light (the submenu is waited for before its row is clicked, as menu-p003 does for Text Size).
zwl_app_clients
click "$(item_x 1 3 $wx)" $bar
expect_log /tmp/zdesktop.log "MENU open client=$zc1 .*item=3 depth=1"
expect_log /tmp/zdesktop.log 'MENU row item=44 '
px=$(popup_x)
pointer move $((px + 40)) "$(row_y 44)" sleep 500 move $((px + 120)) "$(row_y 44)" sleep 800
expect_log /tmp/zdesktop.log "MENU open client=$zc1 .*item=44 depth=2"
expect_log /tmp/zdesktop.log 'MENU row item=46 '
click $(( $(popup_x) + 60 )) "$(row_y 46)" 1200
guest 'grep MENU /tmp/zdesktop.log' > "$out/zdesktop-menu.log"
expect_log /tmp/t.log 'ZTERM THEME run=t1 theme=1 saved=0'
expect_guest 'grep -qx "theme=1" $HOME/.config/keiland/terminal.conf' 'terminal.conf keeps theme=1'
pointer move 1250 780 sleep 400
check "$out/light.png" >/dev/null
keys '<ctrl-shift-q>'
sleep 2

# 4. The kept theme, then the kept size.
start_terminal t2 2
expect_log /tmp/t.log 'ZTERM SETTINGS run=t2 ambiguous_wide=0 font_size=16 theme=1'
keys '<ctrl-minus>'
sleep 1.5
expect_log /tmp/t.log 'ZTERM ZOOM run=t2 pixels=14 '
expect_guest 'grep -qx "font-size=14" $HOME/.config/keiland/terminal.conf' 'terminal.conf keeps font-size=14'
keys '<ctrl-shift-q>'
sleep 2
start_terminal t3 3
expect_log /tmp/t.log 'ZTERM SETTINGS run=t3 ambiguous_wide=0 font_size=14 theme=1'
expect_log /tmp/t.log 'ZTERM MENU state .* pixels=14 '
keys '<ctrl-0>'
sleep 1.5
expect_log /tmp/t.log 'ZTERM ZOOM run=t3 pixels=16 '
expect_guest '! grep -q "font-size" $HOME/.config/keiland/terminal.conf' 'the default size leaves no font-size line'
keys '<ctrl-shift-q>'
sleep 2

guest 'grep -E "FAILED|ERROR" /tmp/t.log /tmp/zdesktop.log' | tee "$out/errors.txt"
[ -s "$out/errors.txt" ] && status=1
guest 'cat /tmp/t.log' > "$out/t.log"
guest "$stop_all" >/dev/null
guest 'rm -f $HOME/.config/keiland/terminal.conf' >/dev/null
[ $status -eq 0 ] && echo "terminal-p006-guest: PASS (and judge the screens)" || echo "terminal-p006-guest: FAIL"
exit $status

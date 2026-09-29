#!/bin/sh
# ws094-p002: zdesktop's desktop surface (keiland_desktop_v1) on the Venus guest, with the probe client
# (desktop-probe.c, built by build-probe.sh).  The running guest gets this worktree's compositor and the probe;
# zdesktop --glass at 1280x800 with --desktop-token=T and no program of its own.
#   role      the probe with the token takes the role: configured under the system bar (0,34 1280x766), its image
#             committed and its frame done; a picture of its squares over the wallpaper (role.png)
#   refuse    a second probe (the role taken) and one with another token are refused
#   input     a left press, a right press and a key where no window is reach the probe (focus in, surface-local
#             places); a Files window over it: a press on the window takes the keyboard back (focus out), and one
#             beside it gives it to the desktop again; the window closed, the desktop does not get the keyboard by itself; pictures
#             (input.png, window.png)
#   home      App Home opens with the desktop in the layer that slides aside (home.png), and closes
#   home-drag a drag from the top-left corner held half way: the yellow square moves and shrinks with the layer (home-drag.png)
#   dnd       a file dragged out of the Files window and over no window reaches the probe (dnd enter)
#   touch     (the pen image) a tap where no window is reaches the probe by wl_touch, and gives it the keyboard
#   restart   zdesktop run again with --desktop-client=the probe --timeout-s=3: started with a token, started again
#             after it ends, and no more than four times a minute (start-limit)
# The steps read zdesktop's log and the probe's through SSH, and the pictures; nothing reads the console.
#   GUEST_RUNTIME=$PWD/build/ws094-run BIN=build/ws094-amd64 plan/ws094/tests/desktop-guest.sh OUTDIR STEP...
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
export GUEST_RUNTIME="${GUEST_RUNTIME:-$PWD/build/ws094-run}"
bin=${BIN:-build/ws094-amd64}
out=$1
shift
mkdir -p "$out"
status=0
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
put() { timeout 120 python3 plan/tools/guest/guest.py put "$1" "$2" >/dev/null 2>&1 </dev/null || { echo "put $1: FAILED"; status=1; }; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@" >/dev/null; sleep 0.7; }
shot() {
	python3 plan/ws035/tests/zdesktop-check.py "$out/$1" --runtime "$GUEST_RUNTIME" >/dev/null
	echo "shot $1"
}
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[d]esktop-probe|[f]iles" | awk "{print \$1}"); do kill $p; done; sleep 1'
env='export XDG_RUNTIME_DIR=/tmp WAYLAND_DISPLAY=wayland-0 HOME=/tmp/dhome;'

# Fails the run unless a log has a line matching a pattern (within a few seconds).
expect_log() {
	tries=0
	found=0
	while [ $tries -lt 8 ]; do
		found=$(guest "grep -acE '$2' $1" | tail -1)
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

# Fails the run unless a log has exactly as many lines matching a pattern as expected.
expect_count() {
	found=$(guest "grep -acE '$2' $1" | tail -1)
	if [ "${found:-x}" = "$3" ]; then
		echo "count: $2 = $3 ok"
	else
		echo "count: $2 = ${found:-?} (expected $3) MISSING"
		status=1
	fi
}

# Starts zdesktop with extra options.
compositor() {
	guest "$stop_all" >/dev/null
	guest "export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0; mkdir -p /tmp/dhome/Desktop; picture=; [ -f /usr/share/keiland/wallpaper.ppm ] && picture=--wallpaper=/usr/share/keiland/wallpaper.ppm
/bin/wayland --timeout=900 --width=1280 --height=800 --glass \$picture $1 > /tmp/zdesktop.log 2>&1 </dev/null &
i=0; while ! grep -q 'ZWL MODE' /tmp/zdesktop.log && [ \$i -lt 60 ]; do sleep 0.5; i=\$((i+1)); done; sleep 2; echo started" >/dev/null
}

for step in "$@"; do
	case "$step" in
	install)
		put "$bin/bin/wayland" /bin/wayland
		for library in libkeiland libvulkan libtruetype libwayland-client; do
			put "$bin/dynamic/$library.so" "/lib/$library.so"
		done
		put "$bin/ws094/desktop-probe" /tmp/desktop-probe
		guest 'chmod 755 /bin/wayland /tmp/desktop-probe' >/dev/null
		;;
	role)
		compositor '--desktop-token=T --desktop-client=none'
		guest "$env /tmp/desktop-probe --token=T > /tmp/probe.log 2>&1 </dev/null & sleep 3; echo started" >/dev/null
		expect_log /tmp/zdesktop.log 'ZWL DESKTOP role client=[0-9]+ surface=[0-9]+ x=0 y=34 width=1280 height=766'
		expect_log /tmp/probe.log 'DESKPROBE configure serial=[0-9]+ x=0 y=34 width=1280 height=766'
		expect_log /tmp/zdesktop.log 'ZWL DESKTOP ack serial='
		expect_log /tmp/probe.log 'DESKPROBE commit width=1280 height=766'
		expect_log /tmp/probe.log 'DESKPROBE frame'
		pointer move 640 500 sleep 300
		shot role.png
		;;
	refuse)
		guest "$env /tmp/desktop-probe --token=T --timeout-s=5 > /tmp/probe2.log 2>&1 </dev/null; echo done" >/dev/null
		expect_log /tmp/zdesktop.log 'ZWL DESKTOP refused client=[0-9]+ reason=role'
		expect_log /tmp/probe2.log 'DESKPROBE failed'
		guest "$env /tmp/desktop-probe --token=wrong --timeout-s=5 > /tmp/probe3.log 2>&1 </dev/null; echo done" >/dev/null
		expect_log /tmp/zdesktop.log 'ZWL DESKTOP refused client=[0-9]+ reason=token'
		expect_log /tmp/probe3.log 'DESKPROBE failed'
		expect_count /tmp/probe.log 'DESKPROBE failed' 0
		;;
	input)
		pointer move 600 400 sleep 300 down sleep 60 up sleep 500
		expect_log /tmp/zdesktop.log 'ZWL DESKTOP focus client='
		expect_log /tmp/probe.log 'DESKPROBE focus in'
		expect_log /tmp/probe.log 'DESKPROBE button x=600 y=366 button=272 state=1'
		pointer move 700 450 sleep 300 right-down sleep 60 right-up sleep 500
		expect_log /tmp/probe.log 'DESKPROBE button x=700 y=416 button=273 state=1'
		keys 'a'
		expect_log /tmp/probe.log 'DESKPROBE key key=30 state=1'
		shot input.png
		# A Files window over the desktop.
		guest "$env mkdir -p /tmp/dhome/Docs; echo hello > /tmp/dhome/Docs/note.txt; /bin/files --token=f1 --timeout-s=800 --width=700 --height=500 /tmp/dhome/Docs > /tmp/f.log 2>&1 </dev/null & sleep 6; echo started" >/dev/null
		expect_log /tmp/zdesktop.log 'ZWL MAP client=[0-9]+ '
		expect_log /tmp/zdesktop.log 'ZWL DESKTOP unfocus via=window'
		expect_log /tmp/probe.log 'DESKPROBE focus out'
		shot window.png
		set -- $(guest "grep -a 'ZWL MAP client=' /tmp/zdesktop.log | tail -1" | sed -n 's/.* x=\([-0-9]*\) y=\([-0-9]*\).*/\1 \2/p')
		wx=${1:-0}; wy=${2:-0}
		echo "files at $wx,$wy"
		# A press beside the window gives the desktop the keyboard, a press on the window takes it back.
		pointer move 1200 700 sleep 300 down sleep 60 up sleep 700
		expect_count /tmp/zdesktop.log 'ZWL DESKTOP focus client=' 2
		pointer move $((wx + 300)) $((wy + 200)) sleep 300 down sleep 60 up sleep 700
		expect_log /tmp/zdesktop.log 'ZWL DESKTOP unfocus via=press'
		expect_count /tmp/probe.log 'DESKPROBE focus out' 2
		# The last window closing does not give the desktop the keyboard (only a press does).
		guest 'for p in $(ps -A -o pid,args | grep -E "[f]iles" | awk "{print \$1}"); do kill $p; done; sleep 2; echo closed' >/dev/null
		expect_log /tmp/zdesktop.log 'ZWL UNMAP client=|ZWL CLEANUP client='
		expect_count /tmp/probe.log 'DESKPROBE focus in' 2
		expect_count /tmp/zdesktop.log 'ZWL DESKTOP focus client=' 2
		;;
	home)
		pointer move 23 17 sleep 300 down sleep 60 up sleep 1500 move 700 780 sleep 300
		expect_log /tmp/zdesktop.log 'ZWL HOME opened'
		shot home.png
		keys '<esc>'
		sleep 1.5
		shot home-closed.png
		;;
	home-drag)
		# A drag from the top-left corner, held half way: the desktop layer (the probe's yellow square with it) is shrunk and moved.
		pointer move 1 1 sleep 300 down sleep 100 move 20 16 sleep 100 move 60 45 sleep 100 move 110 85 sleep 800
		shot home-drag.png
		pointer move 30 20 sleep 100 move 2 2 sleep 200 up sleep 1500
		shot home-drag-closed.png
		;;
	dnd)
		# note.txt is the first row of the Files window's list view; it is dragged out to the desktop's right side.
		keys '<ctrl-2>'
		set -- $(guest "grep -a 'ZWL MAP client=' /tmp/zdesktop.log | tail -1" | sed -n 's/.* x=\([-0-9]*\) y=\([-0-9]*\).*/\1 \2/p')
		wx=${1:-0}; wy=${2:-0}
		pointer move $((wx + 400)) $((wy + 114)) sleep 300 down sleep 100 move $((wx + 420)) $((wy + 120)) sleep 80 move $((wx + 460)) $((wy + 140)) sleep 80 \
			move 1100 600 sleep 150 move 1180 650 sleep 150 move 1200 680 sleep 600
		expect_log /tmp/probe.log 'DESKPROBE dnd enter x=[0-9]+ y=[0-9]+'
		shot dnd.png
		pointer up sleep 800
		expect_log /tmp/probe.log 'DESKPROBE dnd (drop|leave)'
		;;
	touch)
		# (the pen image, /bin/touchinject) a tap where no window is reaches the probe by wl_touch.
		printf 'size 1279 799 2\nwait 2600\ndown 1 500 500\nwait 80\nup 1\nhold 800\n' > "$out/tap.script"
		put "$out/tap.script" /tmp/tap.script
		result=$(guest "/bin/touchinject /tmp/tap.script 2>&1; echo replay=\$?")
		printf '%s\n' "$result" | grep -q '^replay=0$' || { echo "touchinject: FAILED"; status=1; }
		expect_log /tmp/probe.log 'DESKPROBE touch down id=[0-9]+ x=500 y=466'
		expect_log /tmp/zdesktop.log 'ZWL DESKTOP focus client='
		;;
	restart)
		compositor '--desktop-client=/tmp/desktop-probe\ --timeout-s=3'
		sleep 45
		expect_log /tmp/zdesktop.log 'ZWL DESKTOP start pid=[0-9]+ command=/tmp/desktop-probe --timeout-s=3'
		expect_log /tmp/zdesktop.log 'ZWL DESKTOP role client='
		expect_log /tmp/zdesktop.log 'ZWL DESKTOP exited pid='
		expect_count /tmp/zdesktop.log 'ZWL DESKTOP start pid=' 4
		expect_log /tmp/zdesktop.log 'ZWL DESKTOP start-limit starts=4'
		guest 'grep -a "ZWL DESKTOP" /tmp/zdesktop.log' > "$out/restart-log.txt"
		;;
	stop)
		guest "$stop_all" >/dev/null
		;;
	*)
		echo "unknown step $step"
		status=1
		;;
	esac
done
errors=$(guest "grep ERROR /tmp/zdesktop.log | grep -cvE \"not the desktop's token|the desktop has a surface\"" | tail -1)
[ "${errors:-1}" = 0 ] && echo "zdesktop: no ERROR" || { echo "zdesktop: ERROR lines"; status=1; }
guest 'cat /tmp/probe.log' > "$out/probe.log"
guest 'grep -a "ZWL DESKTOP" /tmp/zdesktop.log' > "$out/zdesktop-desktop.log"
[ $status = 0 ] && echo "desktop-guest: PASS" || echo "desktop-guest: FAIL"
exit $status

#!/bin/sh
# ws099-p011 (BUG-121): drags a window's corners COUNT times on the Venus guest of the criteria image (kei's session
# at boot) and counts the times the window vanished.  APP is:
#   mview  Model viewer (Vulkan), opened from App Home the way the demonstration opens it
#   wlshm  a wl_shm window (/bin/wlshm --band, drawn by the CPU), started over SSH on the session's socket
# Drag N takes corner (N / 2) % 4 (bottom-right, top-left, top-right, bottom-left) outwards on even N and back
# inwards on odd N, by 60 to 196 pixels, in 8 steps 16 ms apart (a hand's drag sends many motions, so the client
# gets many configures while it recreates its swapchain).  After each drag:
#   - zdesktop's log must have one more "ZWL RESIZE settled" of the window (else "no-settle");
#   - the window vanished when its process is gone or zdesktop's log has "ZWL UNMAP" of it or "ZWL CLIENT gone" of
#     its client; then the lines about it (the app's MVIEW/WLSHM lines, zdesktop's ERROR/GPU_ERROR/UNMAP/CLIENT gone)
#     are saved in their order, which tells whether the app failed first or zdesktop dropped it, and the app is
#     started again.
# Every 10th drag and each vanishing photograph the screen.  Prints "RESIZE-STRESS RESULT app=APP drags=N
# vanished=V no-settle=S errors=E" and "resize-stress: PASS" (V = 0 and E = 0) or "resize-stress: FAIL".
#
#   VENUS_SIZE=1920x1280 plan/ws035/tests/zdesktop-guest.sh start build/ws099-criteria.img
#   VENUS_SIZE=1920x1280 plan/ws099/tests/resize-stress.sh [APP] [COUNT] [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws035-sq-run}"
export GUEST_RUNTIME
app=${1:-mview}
total=${2:-100}
out=${3:-build/ws099-shots/resize-stress-$app}
size=${VENUS_SIZE:-1920x1280}
screen_w=${size%x*}
screen_h=${size#*x}
mkdir -p "$out"
log=/run/user/1000/session.log
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py --width "$screen_w" --height "$screen_h" "$GUEST_RUNTIME/qmp.sock" "$@"; }
shot() { python3 plan/ws035/tests/zdesktop-check.py "$out/$1.png" --runtime "$GUEST_RUNTIME" >/dev/null 2>&1; }
vanished=0
nosettle=0
restarts=0
: > "$out/vanished.txt"

# The number of lines of the session's log matching a pattern.
count() {
	guest "grep -cE '$1' $log" | tail -1
}

# The last line of the session's log matching a pattern.
last() {
	guest "grep -E '$1' $log | tail -1"
}

# Waits until the session's log has more than BEFORE lines matching a pattern (within some seconds).
wait_more() {
	tries=0
	while [ $tries -lt "$3" ]; do
		found=$(count "$1")
		[ "${found:-0}" -gt "$2" ] 2>/dev/null && return 0
		tries=$((tries + 1))
		sleep 1
	done
	return 1
}

# Starts the app and finds its window (surface, client, process); returns 1 when it did not come.
open_app() {
	maps=$(count 'ZWL MAP client=')
	if [ "$app" = mview ]; then
		# App Home, then its Model viewer icon.
		pointer move 23 17 sleep 300 down sleep 60 up sleep 1500
		set -- $(last 'ZWL HOME icon name="Model viewer"' | sed -n 's/.* x=\([0-9]*\) y=\([0-9]*\).*/\1 \2/p')
		[ -n "${1:-}" ] || { echo "no Model viewer icon in App Home"; return 1; }
		pointer move "$1" "$2" sleep 200 down sleep 60 up
	else
		# Over SSH on the session's socket (root may connect to kei's).
		socket=$(last 'ZWL READY socket=' | sed -n 's/.*socket=\([^ ]*\).*/\1/p')
		guest "export XDG_RUNTIME_DIR=${socket%/*} WAYLAND_DISPLAY=${socket##*/}; /bin/wlshm --size=640x420 --band=ff30a060 --frames=100000 --hold --token=rs >> /tmp/rs-wlshm.log 2>&1 </dev/null & echo started" >/dev/null
	fi
	wait_more 'ZWL MAP client=' "$maps" 30 || { echo "no window came"; return 1; }
	sleep 3
	set -- $(last 'ZWL MAP client=' | sed -n 's/.*client=\([0-9]*\) surface=\([0-9]*\) x=\(-*[0-9]*\) y=\(-*[0-9]*\).*/\1 \2 \3 \4/p')
	client=$1 surface=$2 start_x=$3 start_y=$4
	if [ "$app" = mview ]; then
		set -- $(last 'MVIEW WINDOW run=' | sed -n 's/.* width=\([0-9]*\) height=\([0-9]*\).*/\1 \2/p')
		start_w=$1 start_h=$2
	else
		start_w=640 start_h=420
	fi
	pid=$(guest "ps -A -o pid,args | grep '[/]bin/$app' | tail -1" | awk '{print $1}' | tail -1)
	geometry
	echo "$app: pid $pid, client $client, surface $surface, ${width}x$height at $left,$top (title bar top) to $right,$bottom"
	return 0
}

# Reads the window's outline (the body; its title bar is 52 pixels above it) from its last settled resize, or its map.
geometry() {
	line=$(last "ZWL RESIZE settled surface=$surface ")
	case $line in
	*settled*) set -- $(echo "$line" | sed -n 's/.* x=\(-*[0-9]*\) y=\(-*[0-9]*\) width=\([0-9]*\) height=\([0-9]*\).*/\1 \2 \3 \4/p') ;;
	*) set -- "$start_x" "$start_y" "$start_w" "$start_h" ;;
	esac
	x=${1:-0} y=${2:-0} width=${3:-0} height=${4:-0}
	left=$x right=$((x + width)) top=$((y - 52)) bottom=$((y + height))
}

# Drags a point by (DX, DY) in 8 steps 16 ms apart.
drag() {
	px=$1 py=$2 dx=$3 dy=$4
	set -- move $((px - 6)) $((py - 6)) sleep 120 move "$px" "$py" sleep 250 down sleep 80
	step=1
	while [ $step -le 8 ]; do
		set -- "$@" move $((px + dx * step / 8)) $((py + dy * step / 8)) sleep 16
		step=$((step + 1))
	done
	pointer "$@" sleep 250 up sleep 150
}

# Records a vanished window: the lines about it in their order, a picture, and the app started again.
vanish() {
	vanished=$((vanished + 1))
	echo "drag $1: VANISHED ($2)" | tee -a "$out/vanished.txt"
	guest "grep -nE 'MVIEW|WLSHM|ZWL (ERROR|GPU_ERROR|MODE_ERROR)|ZWL UNMAP client=$client |ZWL CLIENT gone client=$client |ZWL RESIZE (start|end|settled) surface=$surface ' $log | tail -25; tail -5 /tmp/rs-wlshm.log 2>/dev/null" | tee -a "$out/vanished.txt"
	shot "vanished-$1"
	restarts=$((restarts + 1))
	[ $restarts -le 5 ] || return 1
	open_app
}

# kei's session, then the app.
wait_more 'ZWL HANDOFF go=1' 0 90 || { echo "no session"; echo "resize-stress: FAIL"; exit 1; }
sleep 3
errors_before=$(count 'ZWL (ERROR|GPU_ERROR|MODE_ERROR)|MVIEW FAILED')
open_app || { echo "RESIZE-STRESS RESULT app=$app drags=0 vanished=0 no-settle=0 errors=?"; echo "resize-stress: FAIL"; exit 1; }
shot opened

# The drags.
n=0
while [ $n -lt "$total" ]; do
	geometry
	distance=$((60 + (n / 2 * 37) % 137))
	sign=1
	[ $((n % 2)) -eq 1 ] && sign=-1
	settles=$(count "ZWL RESIZE settled surface=$surface ")
	case $(((n / 2) % 4)) in
	0) drag $((right + 3)) $((bottom + 3)) $((sign * distance)) $((sign * distance)) ;;
	1) drag $((left - 4)) $((top - 4)) $((-sign * distance)) $((-sign * distance)) ;;
	2) drag $((right + 3)) $((top - 4)) $((sign * distance)) $((-sign * distance)) ;;
	*) drag $((left - 4)) $((bottom + 3)) $((-sign * distance)) $((sign * distance)) ;;
	esac

	# The settle, the process and zdesktop's hold of the window, in one look.
	state=""
	tries=0
	while [ $tries -lt 10 ]; do
		state=$(guest "s=\$(grep -cE 'ZWL RESIZE settled surface=$surface ' $log); u=\$(grep -cE 'ZWL UNMAP client=$client surface=$surface\$|ZWL CLIENT gone client=$client ' $log); if kill -0 $pid 2>/dev/null; then a=1; else a=0; fi; echo \"STATE \$s \$u \$a\"" | grep '^STATE' | tail -1)
		set -- $state
		[ "${2:-0}" -gt "$settles" ] 2>/dev/null && break
		[ "${3:-0}" -gt 0 ] 2>/dev/null && break
		[ "${4:-1}" -eq 0 ] 2>/dev/null && break
		tries=$((tries + 1))
		sleep 1
	done
	set -- $state
	if [ "${4:-1}" -eq 0 ] 2>/dev/null; then
		vanish "$n" "process gone" || break
	elif [ "${3:-0}" -gt 0 ] 2>/dev/null; then
		vanish "$n" "zdesktop unmapped it or dropped its client" || break
	elif [ "${2:-0}" -le "$settles" ] 2>/dev/null; then
		nosettle=$((nosettle + 1))
		echo "drag $n: no settle"
	fi
	[ $((n % 10)) -eq 9 ] && shot "drag-$n"
	n=$((n + 1))
done

errors_after=$(count 'ZWL (ERROR|GPU_ERROR|MODE_ERROR)|MVIEW FAILED')
errors=$((${errors_after:-0} - ${errors_before:-0}))
shot final
guest "grep -E 'ZWL (ERROR|GPU_ERROR|MODE_ERROR)|MVIEW FAILED|ZWL CLIENT gone' $log" > "$out/errors.txt"
echo "RESIZE-STRESS RESULT app=$app drags=$n vanished=$vanished no-settle=$nosettle errors=$errors"
if [ "$vanished" -eq 0 ] && [ "$errors" -eq 0 ] && [ "$n" -eq "$total" ]; then
	echo "resize-stress: PASS"
	exit 0
fi
echo "resize-stress: FAIL"
exit 1

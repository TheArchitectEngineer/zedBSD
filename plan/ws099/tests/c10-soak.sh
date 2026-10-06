#!/bin/sh
# ws099-p001, C10: zdesktop runs C10_SECONDS (an hour) of continuous work without dying and without a KWL ERROR.
# zdesktop --glass at 1280x800 with the wallpaper; each round: three popup-probe windows opened, the top one dragged
# by its title bar, Wiseview opened and closed (Super+Tab, Esc), App Home opened and closed (the launcher, Esc), a
# window maximized and back (keys m, u to the probe), one probe closed by its close button and the rest ended.
# After each round zdesktop must be alive; at the end the log's KWL ERROR and FAILED lines are counted.
# Prints "C10 RESULT rounds=N seconds=S alive=0|1 errors=E" and "C10: PASS" or "C10: FAIL".
#
#   plan/ws035/tests/zdesktop-guest.sh start build/ws099-criteria.img
#   plan/ws099/tests/c10-soak.sh [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws035-sq-run}"
export GUEST_RUNTIME
out=${1:-build/ws099-shots/c10}
C10_SECONDS=${C10_SECONDS:-3600}
C10_MAX_ERRORS=${C10_MAX_ERRORS:-0}
mkdir -p "$out"
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@"; sleep 0.6; }
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[p]opup-probe" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|[p]opup-probe" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'

guest "$stop_all" >/dev/null
guest "export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0 /tmp/soak-*.pid; picture=; [ -f /usr/share/keiland/wallpaper.png ] && picture=--wallpaper=/usr/share/keiland/wallpaper.png
/bin/wayland --testing --timeout=$((C10_SECONDS + 1200)) --width=1280 --height=800 --glass \$picture > /tmp/zdesktop.log 2>&1 </dev/null & echo \$! > /tmp/soak-wayland.pid; i=0; while [ ! -S /tmp/wayland-0 ] && [ \$i -lt 60 ]; do sleep 0.5; i=\$((i+1)); done; sleep 1; echo started" >/dev/null
start=$(date +%s)
rounds=0
alive=1
while [ $(($(date +%s) - start)) -lt "$C10_SECONDS" ]; do
	# Three windows, cascaded from the centre; the pids kept (the guest's ps shows no arguments).
	guest "export XDG_RUNTIME_DIR=/tmp; for t in a b c; do /bin/popup-probe --timeout-s=600 --token=\$t > /dev/null 2>&1 </dev/null & echo \$! > /tmp/soak-\$t.pid; sleep 1; done; echo ok" >/dev/null
	sleep 1
	set -- $(guest "grep 'KWL MAP client=' /tmp/zdesktop.log | tail -1" | sed -n 's/.* x=\([-0-9]*\) y=\([-0-9]*\).*/\1 \2/p')
	wx=${1:-536}; wy=${2:-389}
	# The top one dragged by its title bar and back.
	pointer move $((wx + 150)) $((wy - 30)) sleep 200 down sleep 150 move $((wx + 190)) $((wy - 10)) sleep 150 move $((wx + 230)) $((wy + 10)) sleep 200 up sleep 400
	pointer move $((wx + 230)) $((wy + 10)) sleep 200 down sleep 150 move $((wx + 150)) $((wy - 30)) sleep 200 up sleep 400
	# Wiseview and App Home, opened and closed.
	keys '<super-tab>'; sleep 1.2; keys '<esc>'; sleep 1
	pointer move 23 17 sleep 200 down sleep 60 up sleep 1500
	keys '<esc>'; sleep 1
	# Maximized and back from the keyboard (the probe's m and u).
	pointer move $((wx + 200)) $((wy + 150)) sleep 200 down sleep 60 up sleep 300
	keys 'm'; sleep 1; keys 'u'; sleep 1
	# The top one closed by its close button, the others ended.
	pointer move $((wx + 400 - 27)) $((wy - 30)) sleep 300 down sleep 60 up sleep 1000
	guest 'for t in a b c; do kill $(cat /tmp/soak-$t.pid) 2>/dev/null; done; sleep 1; echo ok' >/dev/null
	pointer move 640 790 sleep 200
	rounds=$((rounds + 1))
	# zdesktop still there.
	state=$(guest 'kill -0 $(cat /tmp/soak-wayland.pid) 2>/dev/null && echo alive || echo gone' | tail -1)
	if [ "$state" != alive ]; then
		alive=0
		echo "zdesktop gone after round $rounds"
		break
	fi
	[ $((rounds % 20)) -eq 1 ] && check "$out/round-$rounds.png" >/dev/null
	[ $((rounds % 20)) -eq 1 ] && echo "round $rounds at $(($(date +%s) - start)) s"
done
seconds=$(($(date +%s) - start))
errors=$(guest 'grep -cE "KWL ERROR|KWL FAILED" /tmp/zdesktop.log' | tail -1)
guest 'grep -E "KWL ERROR|KWL FAILED" /tmp/zdesktop.log | head -20' > "$out/errors.txt"
guest 'tail -50 /tmp/zdesktop.log' > "$out/zdesktop-tail.log"
check "$out/end.png" >/dev/null
guest "$stop_all" >/dev/null
echo "C10 RESULT rounds=$rounds seconds=$seconds alive=$alive errors=${errors:-?}"
if [ "$alive" -eq 1 ] && [ "${errors:-1}" -le "$C10_MAX_ERRORS" ] 2>/dev/null && [ "$rounds" -gt 0 ]; then
	echo "C10: PASS"
	exit 0
fi
echo "C10: FAIL"
exit 1

#!/bin/sh
# ws134-p004: the System Monitor's input on the Venus guest with the test touch screen (the image of
# plan/ws134/tests/build-monitor-image.sh: config-amd64-monitor.mk has /dev/input-inject and touchinject; start the guest
# with plan/tools/files/files-guest.sh start IMAGE).  zdesktop --glass at 1280x800, the monitor on the critical recording
# with the clock stopped at 130 s (a card comes out at once).  The touch screen is declared 0..1279 by 0..799, so a
# finger's numbers are the output's pixels; the plates' places are the monitor's own ZMON PLATE lines plus the window's
# place (KWL MAP).  Each touch script waits 2.6 s after declaring its screen (the compositor looks for a new evdev node
# now and then).
#  1. A tap on the CPU plate: ZMON CARD open plate=cpu via=tap and the card drawn all the way out.  card.png.
#  2. A tap outside the card: ZMON CARD close plate=cpu.
#  3. A long press on the network's plate: its card, pinned (ZMON CARD pin=1).  A tap outside keeps it.  pinned.png.
#     Esc (a key): ZMON VIEW overview and the card back.
#  4. A swipe to the left across the network's plate: ZMON RANGE 15 min (from 5 min), no card.
#  5. Two fingers apart over the state's plate: ZMON VIEW detail plate=state via=pinch; together: ZMON VIEW overview
#     via=pinch; the range did not change.  overview.png.
#  6. A two-finger tap over the graphics' plate: ZMON VIEW detail plate=graphics; another: the overview.
#  7. Keys: Tab, Tab (ZMON FOCUS gpu), Enter (the GPU's card), P (pinned), Esc (the overview), Left (ZMON RANGE 5 min).
#  8. A finger dragged across the state's core: ZMON CORE turn= via=touch.
# Judged by the monitor's log (/tmp/monitor.log in the guest, read over SSH) and the pictures, not the console.
#
#   plan/ws134/tests/monitor-p004.sh [OUTDIR]
# Prints "monitor-p004: PASS" or "monitor-p004: FAIL".
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws071-run}"
export GUEST_RUNTIME
out=${1:-build/ws134-p004}
mkdir -p "$out"
status=0
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
put() { timeout 90 python3 plan/tools/guest/guest.py put "$1" "$2" >/dev/null 2>&1 </dev/null || { echo "put $1: FAILED"; status=1; }; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@" >/dev/null; }
shot() { python3 plan/ws035/tests/zdesktop-check.py "$out/$1" --runtime "$GUEST_RUNTIME" >/dev/null; echo "shot $1"; }
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[m]onitor|[t]ouchinject" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|[m]onitor" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'

# Fails the run unless the monitor's log has a line matching a pattern (within a few seconds).
expect_log() {
	tries=0
	found=0
	while [ $tries -lt 8 ]; do
		found=$(guest "grep -cE '$1' /tmp/monitor.log" | tail -1)
		[ "${found:-0}" -gt 0 ] 2>/dev/null && break
		tries=$((tries + 1))
		sleep 1
	done
	if [ "${found:-0}" -gt 0 ] 2>/dev/null; then
		echo "log: $1 ok"
	else
		echo "log: $1 MISSING"
		status=1
	fi
}

# Fails the run when the monitor's log has as many lines matching a pattern as before ($2) after a step: something
# that should not have happened did.
expect_count() {
	now=$(guest "grep -cE '$1' /tmp/monitor.log" | tail -1)
	if [ "${now:-x}" = "$2" ]; then
		echo "log: still $2 of '$1' ok"
	else
		echo "log: ${now:-?} of '$1' (want $2) MISSING"
		status=1
	fi
}

# Counts the lines matching a pattern in the monitor's log.
count() { guest "grep -cE '$1' /tmp/monitor.log" | tail -1; }

# Replays a touch script (a file in OUTDIR) in the guest; without the injector's replay=0 the step fails.
replay() {
	put "$out/$1" "/tmp/$1"
	result=$(guest "/bin/touchinject /tmp/$1 2>&1; echo replay=\$?")
	if ! printf '%s\n' "$result" | grep -q '^replay=0$'; then
		printf '%s\n' "$result" > "$out/$1.failed.log"
		echo "touchinject $1: FAILED (output in $out/$1.failed.log)"
		status=1
	fi
}

# The middle of a plate on the output: sets px and py from its ZMON PLATE line and the window's place.
plate() {
	set -- $(guest "grep 'ZMON PLATE name=$1 ' /tmp/monitor.log | tail -1" | sed -n 's/.* x=\([0-9]*\) y=\([0-9]*\) width=\([0-9]*\) height=\([0-9]*\).*/\1 \2 \3 \4/p')
	px=$((wx + ${1:-0} + ${3:-0} / 2))
	py=$((wy + ${2:-0} + ${4:-0} / 2))
}

# A tap at a point.
tap() {
	printf 'size 1279 799 2\nwait 2600\ndown 1 %d %d\nwait 60\nup 1\nhold 600\n' "$1" "$2" > "$out/$3.script"
	replay "$3.script"
}

# zdesktop, then the monitor on the critical recording with the clock stopped.
guest "$stop_all" >/dev/null
guest "export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0
picture=; [ -f /usr/share/keiland/wallpaper.png ] && picture=--wallpaper=/usr/share/keiland/wallpaper.png
/bin/wayland --testing --timeout=600 --width=1280 --height=800 --glass \$picture > /tmp/zdesktop.log 2>&1 </dev/null & for w in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do grep -q KWL.READY /tmp/zdesktop.log 2>/dev/null && break; sleep 0.5; done; sleep 1
/bin/monitor --timeout-s=400 --source=replay:/usr/share/monitor-tests/critical.txt --clock=fixed:130000 --token=p004 > /tmp/monitor.log 2>&1 </dev/null & echo started" >/dev/null
expect_log 'ZMON READY .* source=replay'
sleep 3
set -- $(guest "grep 'KWL MAP client=' /tmp/zdesktop.log | tail -1" | sed -n 's/.* x=\([-0-9]*\) y=\([-0-9]*\).*/\1 \2/p')
wx=${1:-0}
wy=${2:-0}
echo "window at $wx,$wy"
guest "grep -E 'ZMON (PLATE|READY)' /tmp/monitor.log" > "$out/layout.log"

# 1. A tap on the CPU plate: its card.
plate cpu
tap "$px" "$py" tap-cpu
expect_log 'ZMON CARD open plate=cpu via=tap'
expect_log 'ZMON CARD shown plate=cpu pinned=0'
sleep 1
shot card.png

# 2. A tap outside the card (the window's lower left corner, on the network's plate): the card goes back.
plate flow
tap $((wx + 40)) $((py + 60)) tap-outside
expect_log 'ZMON CARD close plate=cpu via=tap'

# 3. A long press on the network's plate: its card, pinned; a tap outside keeps it; Esc sends it back.
plate flow
printf 'size 1279 799 2\nwait 2600\ndown 1 %d %d\nhold 900\nup 1\nhold 600\n' "$px" "$py" > "$out/long-press.script"
replay long-press.script
expect_log 'ZMON CARD open plate=flow via=long-press'
expect_log 'ZMON CARD pin=1 plate=flow'
closes=$(count 'ZMON CARD close')
plate cpu
tap "$px" "$py" tap-pinned
expect_count 'ZMON CARD close' "$closes"
sleep 1
shot pinned.png
keys '<esc>'
expect_log 'ZMON VIEW overview via=key'
expect_log 'ZMON CARD close plate=flow via=key'

# 4. A swipe to the left across the network's plate: the next longer range, and no card.
plate flow
opens=$(count 'ZMON CARD open')
printf 'size 1279 799 2\nwait 2600\ndown 1 %d %d\nwait 50\nswipe -300 0 6 16.667\nup 1\nhold 800\n' $((px + 150)) "$py" > "$out/swipe.script"
replay swipe.script
expect_log 'ZMON RANGE 15 min'
expect_count 'ZMON CARD open' "$opens"

# 5. Two fingers apart over the state's plate: its detail; together: the overview; the range stays.
plate state
ranges=$(count 'ZMON RANGE')
{
	printf 'size 1279 799 2\nwait 2600\ndown 1 %d %d; down 2 %d %d\nwait 100\n' $((px - 50)) "$py" $((px + 50)) "$py"
	k=1
	while [ $k -le 20 ]; do
		printf 'move 1 %d %d; move 2 %d %d\nwait 16.667\n' $((px - 50 - 5 * k)) "$py" $((px + 50 + 5 * k)) "$py"
		k=$((k + 1))
	done
	printf 'hold 150\nup 1; up 2\nhold 800\n'
} > "$out/pinch-out.script"
replay pinch-out.script
expect_log 'ZMON VIEW detail plate=state via=pinch'
{
	printf 'size 1279 799 2\nwait 2600\ndown 1 %d %d; down 2 %d %d\nwait 100\n' $((px - 150)) "$py" $((px + 150)) "$py"
	k=1
	while [ $k -le 20 ]; do
		printf 'move 1 %d %d; move 2 %d %d\nwait 16.667\n' $((px - 150 + 5 * k)) "$py" $((px + 150 - 5 * k)) "$py"
		k=$((k + 1))
	done
	printf 'hold 150\nup 1; up 2\nhold 800\n'
} > "$out/pinch-in.script"
replay pinch-in.script
expect_log 'ZMON VIEW overview via=pinch'
expect_count 'ZMON RANGE' "$ranges"
sleep 1
shot overview.png

# 6. A two-finger tap over the graphics' plate: its detail; another: the overview.
plate graphics
printf 'size 1279 799 2\nwait 2600\ndown 1 %d %d; down 2 %d %d\nwait 100\nup 1; up 2\nhold 800\n' $((px - 40)) "$py" $((px + 40)) "$py" > "$out/two-tap.script"
replay two-tap.script
expect_log 'ZMON VIEW detail plate=graphics via=two-finger-tap'
replay two-tap.script
expect_log 'ZMON VIEW overview via=two-finger-tap'
expect_count 'ZMON RANGE' "$ranges"

# 7. The keys: the keyboard's plate, its card, the pin, the overview, the range.
keys '<tab>' '<tab>'
expect_log 'ZMON FOCUS plate=gpu'
keys '<ret>'
expect_log 'ZMON CARD open plate=gpu via=key'
keys 'p'
expect_log 'ZMON CARD pin=1 plate=gpu'
sleep 1
shot keys.png
keys '<esc>'
expect_log 'ZMON CARD close plate=gpu via=key'
keys '<left>'
expect_log 'ZMON RANGE 5 min'

# 8. A finger across the state's core: it turns, and comes back.
plate state
printf 'size 1279 799 2\nwait 2600\ndown 1 %d %d\nwait 50\nswipe 120 0 8 16.667\nhold 200\nup 1\nhold 800\n' $((px - 60)) $((py - 20)) > "$out/core.script"
replay core.script
expect_log 'ZMON CORE turn=0\.[0-9]+ via=touch'

# The monitor's log and the compositor's errors; everything stops.
guest 'grep -E "ZMON (READY|CARD|VIEW|RANGE|FOCUS|CORE|FAILED|DISCONNECTED)" /tmp/monitor.log' > "$out/monitor.log"
failed=$(guest "grep -cE 'ZMON (FAILED|DISCONNECTED)' /tmp/monitor.log" | tail -1)
[ "${failed:-1}" = 0 ] && echo "monitor: no failure ok" || { echo "monitor: failure lines MISSING"; status=1; }
errors=$(guest "grep -c ERROR /tmp/zdesktop.log" | tail -1)
[ "${errors:-1}" = 0 ] && echo "zdesktop: no ERROR" || { echo "zdesktop: ERROR lines"; status=1; }
guest "$stop_all" >/dev/null
[ $status -eq 0 ] && echo "monitor-p004: PASS" || echo "monitor-p004: FAIL"
exit $status

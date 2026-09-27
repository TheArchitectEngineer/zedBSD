#!/bin/sh
# ws035-p013: the network in the system bar, on the Venus guest (the network image,
# plan/ws035/tests/build-network-image.sh).  zdesktop --glass at 1280x800:
#  1. The real networkd (QEMU: a wired ue0, no Wi-Fi radio): the icon is the wired tree
#     (wired.png); a click opens the menu with "No Wi-Fi hardware" and the wired line
#     (wired-menu.png); a click outside closes it.
#  2. The networkd stand-in with a Wi-Fi radio (network-probe; networkd's socket is moved aside
#     meanwhile and put back at the end): the menu scans and lists three networks (list.png);
#     a click on "Kei Lab" joins it: the icon becomes the Wi-Fi bars, the network is checked
#     (joined.png); "Neighbor 5G" has no saved profile and the menu says so (failed.png);
#     the switch turns the Wi-Fi off (off.png).
# Part 2 is QEMU-only faking: the radio, the scan and the joins are the stand-in's.
#
#   GUEST_RUNTIME=... plan/ws035/tests/zdesktop-guest.sh start build/<x>/hdd-image.img
#   plan/ws035/tests/zdesktop-p013.sh [OUTDIR] [SHOTS PREFIX]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws035-sq-run}"
export GUEST_RUNTIME
out=${1:-build/ws035-p013}
prefix=${2:-}
mkdir -p "$out"
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
stop_all='for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[n]etwork-probe" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|[n]etwork-probe" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
start_desktop='export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0; picture=; [ -f /usr/share/keiland/wallpaper.ppm ] && picture=--wallpaper=/usr/share/keiland/wallpaper.ppm
/bin/wayland --timeout=600 --width=1280 --height=800 --glass $picture > /tmp/zdesktop.log 2>&1 </dev/null & sleep 5; echo started'
status=0

# Fails the run unless a log has a line matching a pattern (within a few seconds).
expect_log() {
	tries=0
	found=0
	while [ $tries -lt 8 ]; do
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
click() {
	pointer move $(($1 - 2)) "$2" sleep 150 move "$1" "$2" sleep 300 down sleep 60 up sleep "${3:-900}"
}
shot() {
	check "$out/$1" >/dev/null
	[ -n "$prefix" ] && cp "$out/$1" "$prefix$1"
}
# The middle of the icon, from zdesktop's log.
icon() {
	guest "grep 'ZWL NETWORK icon' /tmp/zdesktop.log | tail -1" | sed -n 's/.* x=\([0-9]*\) y=\([0-9]*\) width=\([0-9]*\) height=\([0-9]*\).*/\1 \2 \3 \4/p'
}
# The middle of the last laid-out row whose text is $1.
row() {
	guest "grep 'ZWL NETWORK row .*text=$1\$' /tmp/zdesktop.log | tail -1" | sed -n 's/.* x=\([0-9]*\) y=\([0-9]*\) width=\([0-9]*\) height=\([0-9]*\).*/\1 \2 \3 \4/p'
}

# 1. The real networkd.
guest "$stop_all" >/dev/null
guest "$start_desktop" >/dev/null
expect_log /tmp/zdesktop.log 'ZWL NETWORK state reachable=1 connected=1 kind=wired interface=[a-z]+[0-9]+ wifi=absent'
set -- $(icon)
ix=$(($1 + $3 / 2)); iy=$(($2 + $4 / 2))
echo "icon at $ix,$iy"
pointer move 700 400 sleep 400
shot wired.png
click "$ix" "$iy"
expect_log /tmp/zdesktop.log 'ZWL NETWORK open'
expect_log /tmp/zdesktop.log 'ZWL NETWORK row .*text=No Wi-Fi hardware'
expect_log /tmp/zdesktop.log 'ZWL NETWORK row .*text=Wired \([a-z]+[0-9]+\): connected'
pointer move 1100 300 sleep 400
shot wired-menu.png
click 400 400
expect_log /tmp/zdesktop.log 'ZWL NETWORK close via=outside'

# 2. The stand-in: networkd's socket aside, the stand-in in its place, zdesktop again.
guest "$stop_all" >/dev/null
guest 'mv /run/networkd.sock /run/networkd.sock.real; /bin/network-probe 240 > /tmp/probe.log 2>&1 </dev/null & sleep 1; echo started' >/dev/null
expect_log /tmp/probe.log 'NETPROBE listening'
guest "$start_desktop" >/dev/null
expect_log /tmp/zdesktop.log 'ZWL NETWORK state reachable=1 connected=1 kind=wired interface=em9 wifi=disconnected'
set -- $(icon)
ix=$(($1 + $3 / 2)); iy=$(($2 + $4 / 2))
click "$ix" "$iy" 1500
expect_log /tmp/zdesktop.log 'ZWL NETWORK request scan'
expect_log /tmp/zdesktop.log 'ZWL NETWORK scan count=3'
expect_log /tmp/zdesktop.log 'ZWL NETWORK row .*text=Kei Lab'
set -- $(row 'Kei Lab')
lx=$(($1 + 150)); ly=$(($2 + $4 / 2))
pointer move "$lx" "$ly" sleep 500
shot list.png

# Joining "Kei Lab".
click "$lx" "$ly" 1500
expect_log /tmp/probe.log 'NETPROBE request op=35 ssid=Kei Lab'
expect_log /tmp/zdesktop.log 'ZWL NETWORK state reachable=1 connected=1 kind=wifi interface=wlan0 wifi=connected ssid=Kei Lab'
expect_log /tmp/zdesktop.log 'ZWL NETWORK row .*text=Disconnect from Kei Lab'
pointer move 1100 500 sleep 500
shot joined.png

# "Neighbor 5G" has no profile.
set -- $(row 'Neighbor 5G')
click $(($1 + 150)) $(($2 + $4 / 2)) 1500
expect_log /tmp/zdesktop.log 'ZWL NETWORK done request=join error=[1-9]'
expect_log /tmp/zdesktop.log 'ZWL NETWORK row .*text=Could not join'
pointer move 1100 500 sleep 500
shot failed.png

# The switch turns the Wi-Fi off; the menu closes and the icon is struck through.
set -- $(row 'Wi-Fi')
click $(($1 + 150)) $(($2 + $4 / 2)) 1500
expect_log /tmp/probe.log 'NETPROBE request op=33'
expect_log /tmp/zdesktop.log 'ZWL NETWORK state .*wifi=off'
click 400 400
expect_log /tmp/zdesktop.log 'ZWL NETWORK close via=outside'
pointer move 700 400 sleep 500
shot off.png

errors=$(guest "grep -c ERROR /tmp/zdesktop.log" | tail -1)
[ "${errors:-1}" = 0 ] && echo "zdesktop: no ERROR" || { echo "zdesktop: ERROR lines"; status=1; }
guest 'cat /tmp/zdesktop.log' > "$out/zdesktop.log"
guest 'cat /tmp/probe.log' > "$out/probe.log"

# networkd's socket back; net show answers again.
guest "$stop_all" >/dev/null
guest 'rm -f /run/networkd.sock; mv /run/networkd.sock.real /run/networkd.sock; net show' > "$out/net-show.txt"
grep -q 'online' "$out/net-show.txt" && echo "networkd: back" || { echo "networkd: not back"; status=1; }
[ $status = 0 ] && echo "zdesktop-p013: PASS" || echo "zdesktop-p013: FAIL"
exit $status

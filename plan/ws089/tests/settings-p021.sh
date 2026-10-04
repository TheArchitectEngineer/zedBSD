#!/bin/sh
# ws089-p021: the Wi-Fi lists follow the scans by themselves (no Scan button), the compositor counts who asks for
# scans and asks networkd for them through libkeiland-backend, and Disconnect is a picture.  On the Venus guest of
# the Settings image (build-settings-image.sh), zdesktop --glass at 1280x800.
#  1. The real networkd (QEMU: a wired interface, no radio):
#     a. Settings on Wi-Fi asks for scans (ZSETTINGS NETWORK scanning on=1; ZWL NETWORK scan holders=1) and networkd
#        holds the lease: `net watch` says scan=1 on its wifi line, and route default=IF names the wired interface
#        that carries the default route (BUG-189's line);
#     b. (the minute, 2026-10-05) Settings stopped (SIGSTOP) asks no more: after a minute its asking ends
#        (ZWL SYSTEM scanning expired, holders=0) and networkd is told: `net watch` says scan=0;
#     c. Settings continued (SIGCONT) asks again at once (holders=1); Settings ended (kill): holders=0.
#  2. The networkd stand-in with a Wi-Fi radio (network-probe; networkd's socket moved aside), "Kei Lab" saved:
#     a. Settings A on Wi-Fi: the stand-in hears WIFI_SCAN_START (op=40) and WIFI_LIST (op=34) again every few seconds
#        (at least 3 within 10 s); the list (scan count=3) and no Scan button (no CONTROL index=2) (wifi-auto.png);
#     b. Kei Lab joined (control 100): its row has the Disconnect picture (CONTROL index=3) (wifi-connected.png);
#     c. Settings B on Network: holders=2; the system bar's menu opened: holders=3, its row of Kei Lab with the
#        Disconnect picture (menu-connected.png); Esc closes the menu: holders=2;
#     d. B goes to Ethernet (control 7): ZSETTINGS NETWORK scanning on=0 in B's log, holders=1;
#     e. A's Disconnect picture (control 3): op=36 (wifi-left.png);
#     f. A killed with SIGKILL (its client ends without a word): holders=0 and the stand-in hears WIFI_SCAN_STOP (op=41).
#  3. networkd's socket back; no ERROR line in zdesktop's log.
# PASS: every "ok" line and the last line settings-p021: PASS.  Part 2 is QEMU-only faking (the stand-in's radio).
#
#   plan/ws089/tests/settings-guest.sh start     (the guest must be up)
#   plan/ws089/tests/settings-p021.sh [OUTDIR]   (default build/ws089-shots/p021)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws089-run}"
export GUEST_RUNTIME
out=${1:-build/ws089-shots/p021}
mkdir -p "$out"
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@"; sleep 0.7; }
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[s]ettings|[n]etwork-probe" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|[s]ettings|[n]etwork-probe" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
start_desktop='export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0; picture=; [ -f /usr/share/keiland/wallpaper.ppm ] && picture=--wallpaper=/usr/share/keiland/wallpaper.ppm
/bin/wayland --timeout=900 --width=1280 --height=800 --glass $picture > /tmp/zdesktop.log 2>&1 </dev/null & for w in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do grep -q ZWL.READY /tmp/zdesktop.log 2>/dev/null && break; sleep 0.5; done; sleep 1; echo started'
status=0
. plan/ws089/tests/settings-wait.sh

# Fails the run unless a log has a line matching a pattern (within a few seconds).
expect_log() {
	tries=0
	found=0
	while [ $tries -lt 10 ]; do
		found=$(guest "grep -caE '$2' $1" | tail -1)
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

# Fails the run unless the last line of a log matching a prefix is the one expected (within a few seconds).
expect_last() {
	tries=0
	last=
	while [ $tries -lt 10 ]; do
		last=$(guest "grep -aE '$2' $1 | tail -1" | tail -1)
		[ "$last" = "$3" ] && break
		tries=$((tries + 1))
		sleep 1
	done
	if [ "$last" = "$3" ]; then
		echo "last: $3 ok"
	else
		echo "last: $3 MISSING (last: $last)"
		status=1
	fi
}

# Fails the run when a log has a line matching a pattern.
expect_none() {
	found=$(guest "grep -caE '$2' $1" | tail -1)
	if [ "${found:-0}" = 0 ]; then
		echo "none: $2 ok"
	else
		echo "none: $2 FOUND"
		status=1
	fi
}

# The wifi line and the route line networkd's watch says now (three seconds of net watch).
watch_now() {
	guest 'timeout 3 net watch 2>&1 | grep -aE "^(wifi|route) " | tail -2'
}

# Starts settings on a page with its log, and finds its window.
start_settings() {
	guest "export XDG_RUNTIME_DIR=/tmp HOME=/root; /bin/settings --timeout-s=800 $1 > $2 2>&1 </dev/null & sleep 5; echo started" >/dev/null
	find_window
	echo "settings: window at $wx,$wy"
}

# Clicks a control by its index, where the settings of a log last logged it (window coordinates).
control() {
	which=$1
	log=$2
	set -- $(guest "grep 'ZSETTINGS CONTROL index=$which ' $log | tail -1" | sed -n 's/.* x=\([-0-9]*\) y=\([-0-9]*\) width=\([0-9]*\) height=\([0-9]*\).*/\1 \2 \3 \4/p')
	if [ -z "${1:-}" ]; then
		echo "control $which: not found"
		status=1
		return
	fi
	cx=$((wx + $1 + $3 / 2)); cy=$((wy + $2 + $4 / 2))
	pointer move $((cx - 2)) "$cy" sleep 150 move "$cx" "$cy" sleep 300 down sleep 60 up sleep 1200
}

# A picture of the screen with the pointer out of the way.
shot() {
	pointer move 1270 790 sleep 600
	check "$out/$1" >/dev/null
	echo "shot: $out/$1"
}

wait_guest

# 1. The real networkd: the lease and the route's line.
guest "$stop_all" >/dev/null
guest "$start_desktop" >/dev/null
wait_desktop
start_settings wifi /tmp/s.log
wa=$wx; wb=$wy
expect_log /tmp/s.log 'ZSETTINGS NETWORK scanning on=1'
expect_last /tmp/zdesktop.log 'ZWL NETWORK scan holders=' 'ZWL NETWORK scan holders=1'
watched=$(watch_now)
echo "$watched" > "$out/watch-on.txt"
echo "$watched" | grep -q ' scan=1' && echo "networkd: scan=1 ok" || { echo "networkd: scan=1 MISSING"; status=1; }
echo "$watched" | grep -qE '^route default=[a-z]+[0-9]+' && echo "networkd: route default ok" || { echo "networkd: route default MISSING"; status=1; }
guest "pid=\$(ps -A -o pid,args | grep '[s]ettings' | awk '{print \$1}'); kill -STOP \$pid" >/dev/null
sleep 62
expect_log /tmp/zdesktop.log 'ZWL SYSTEM scanning expired client='
expect_last /tmp/zdesktop.log 'ZWL NETWORK scan holders=' 'ZWL NETWORK scan holders=0'
sleep 2
watched=$(watch_now)
echo "$watched" > "$out/watch-off.txt"
echo "$watched" | grep -q ' scan=0' && echo "networkd: scan=0 ok" || { echo "networkd: scan=0 MISSING"; status=1; }
guest "pid=\$(ps -A -o pid,args | grep '[s]ettings' | awk '{print \$1}'); kill -CONT \$pid" >/dev/null
expect_last /tmp/zdesktop.log 'ZWL NETWORK scan holders=' 'ZWL NETWORK scan holders=1'
guest "pid=\$(ps -A -o pid,args | grep '[s]ettings' | awk '{print \$1}'); kill \$pid" >/dev/null
expect_last /tmp/zdesktop.log 'ZWL NETWORK scan holders=' 'ZWL NETWORK scan holders=0'

# 2. The stand-in, with a key saved for Kei Lab first (against the real networkd).
guest "$stop_all" >/dev/null
guest 'rm -f /etc/wifi.conf; net wifi add "Kei Lab" --password keilab-2026 --auto yes; echo saved' >/dev/null
guest 'mv /run/networkd.sock /run/networkd.sock.real; /bin/network-probe 600 > /tmp/probe.log 2>&1 </dev/null & sleep 1; echo started' >/dev/null
expect_log /tmp/probe.log 'NETPROBE listening'
guest "$start_desktop" >/dev/null
wait_desktop

# a. Settings A on Wi-Fi: the lease, the readings, the list, no Scan button.
start_settings wifi /tmp/s.log
wa=$wx; wb=$wy
expect_log /tmp/probe.log 'NETPROBE request op=40 '
expect_log /tmp/s.log 'ZSETTINGS NETWORK scan count=3'
sleep 10
lists=$(guest "grep -c 'NETPROBE request op=34 ' /tmp/probe.log" | tail -1)
[ "${lists:-0}" -ge 3 ] 2>/dev/null && echo "probe: $lists readings ok" || { echo "probe: ${lists:-0} readings, fewer than 3"; status=1; }
expect_log /tmp/s.log 'ZSETTINGS CONTROL index=102 '
expect_none /tmp/s.log 'ZSETTINGS CONTROL index=2 '
shot wifi-auto.png

# b. Kei Lab joined, and the Disconnect picture on its row.
control 100 /tmp/s.log
expect_log /tmp/probe.log 'NETPROBE request op=35 ssid=Kei Lab'
expect_log /tmp/s.log 'ZSETTINGS NETWORK message bad=0 text=Connected to Kei Lab.'
expect_log /tmp/s.log 'ZSETTINGS CONTROL index=3 '
shot wifi-connected.png

# c. Settings B on Network, then the system bar's menu: three holders; Esc: two.
start_settings network /tmp/s2.log
wc=$wx; wd=$wy
expect_last /tmp/zdesktop.log 'ZWL NETWORK scan holders=' 'ZWL NETWORK scan holders=2'
set -- $(guest "grep -a 'ZWL NETWORK icon' /tmp/zdesktop.log | tail -1" | sed -n 's/.* x=\([0-9]*\) y=\([0-9]*\) width=\([0-9]*\) height=\([0-9]*\).*/\1 \2 \3 \4/p') 0 0 0 0
ix=$(($1 + $3 / 2)); iy=$(($2 + $4 / 2))
pointer move $((ix - 2)) "$iy" sleep 150 move "$ix" "$iy" sleep 300 down sleep 60 up sleep 1200
expect_last /tmp/zdesktop.log 'ZWL NETWORK scan holders=' 'ZWL NETWORK scan holders=3'
pointer move 1100 500 sleep 500
check "$out/menu-connected.png" >/dev/null
echo "shot: $out/menu-connected.png"
keys '<esc>'
expect_last /tmp/zdesktop.log 'ZWL NETWORK scan holders=' 'ZWL NETWORK scan holders=2'

# d. B goes to Ethernet: it asks no longer.
wx=$wc; wy=$wd
control 7 /tmp/s2.log
expect_log /tmp/s2.log 'ZSETTINGS NETWORK scanning on=0'
expect_last /tmp/zdesktop.log 'ZWL NETWORK scan holders=' 'ZWL NETWORK scan holders=1'

# e. A's Disconnect picture.
wx=$wa; wy=$wb
control 3 /tmp/s.log
expect_log /tmp/probe.log 'NETPROBE request op=36'
shot wifi-left.png

# f. A killed without a word: its asking goes with its client, and the stand-in is told to stop.
guest "pid=\$(ps -A -o pid,args | grep '[s]ettings.* wifi$' | awk '{print \$1}'); kill -9 \$pid" >/dev/null
expect_last /tmp/zdesktop.log 'ZWL NETWORK scan holders=' 'ZWL NETWORK scan holders=0'
expect_log /tmp/probe.log 'NETPROBE request op=41 '
expect_log /tmp/probe.log 'NETPROBE notify .* scan=0'

# 3. zdesktop saw no error; networkd's socket back.
errors=$(guest "grep -c ERROR /tmp/zdesktop.log" | tail -1)
[ "${errors:-1}" = 0 ] && echo "zdesktop: no ERROR" || { echo "zdesktop: ERROR lines"; guest "grep ERROR /tmp/zdesktop.log | head -5"; status=1; }
guest 'cat /tmp/s.log' > "$out/settings-a.log"
guest 'cat /tmp/s2.log' > "$out/settings-b.log"
guest 'cat /tmp/probe.log' > "$out/probe.log"
guest "grep -a 'ZWL NETWORK scan holders\|ZWL SYSTEM scanning' /tmp/zdesktop.log" > "$out/holders.log"
guest "$stop_all" >/dev/null
guest 'rm -f /run/networkd.sock; mv /run/networkd.sock.real /run/networkd.sock; rm -f /etc/wifi.conf; net show' > "$out/net-show.txt"
grep -q 'online' "$out/net-show.txt" && echo "networkd: back" || { echo "networkd: not back"; status=1; }
[ $status = 0 ] && echo "settings-p021: PASS" || echo "settings-p021: FAIL"

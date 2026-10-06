#!/bin/sh
# ws035-p104: the system bar's network menu in a normal user's graphical session, on the Venus guest of the
# graphical login image with the networkd stand-in (plan/ws035/tests/build-login-image.sh BUILD graphical-network).
# sessiond gives the session's user the group "network", which networkd's socket (0660, root:network) admits.
#  1. ws136-p002: the image has kei (uid 1000, password "kei") and logs kei in at boot (/etc/keiland/autologin,
#     since 2026-09-29); the run stops that session, empties the autologin file (restored at the end) and starts
#     sessiond itself, so the greeter offers kei, selected.  The password logs kei in.  The session's zdesktop reaches the real networkd (QEMU: wired ue0, no radio):
#     "KWL NETWORK state reachable=1 ... kind=wired" as uid 1000; the menu shows the wired line (wired-menu.png).
#  2. The stand-in with a Wi-Fi radio takes networkd's place, its socket set to networkd's owner and mode
#     (root:network 0660); the session is ended and kei logs in again.  As kei the menu scans (list.png),
#     joins "Kei Lab" (typing a key in the menu's field when kei's store has none, BUG-160) (joined.png) and turns
#     the Wi-Fi off (off.png).  Part 2's radio is QEMU-only faking.
#
#   GUEST_RUNTIME=... plan/ws035/tests/zdesktop-guest.sh start build/<x>/hdd-image.img
#   plan/ws035/tests/zdesktop-p104.sh [OUTDIR] [SHOTS PREFIX]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws035-sq-run}"
export GUEST_RUNTIME
out=${1:-build/ws035-p104}
prefix=${2:-}
mkdir -p "$out"
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@"; sleep 0.8; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py --width 1280 --height 800 "$GUEST_RUNTIME/qmp.sock" "$@"; }
session=/run/user/1000/session.log
status=0

# Fails the run unless a guest file has at least N lines matching a pattern (within some seconds).
expect_log() {
	tries=0
	found=0
	while [ $tries -lt "${3:-10}" ]; do
		found=$(guest "grep -cE '$2' $1" | tail -1)
		[ "${found:-0}" -ge "${4:-1}" ] 2>/dev/null && break
		tries=$((tries + 1))
		sleep 1
	done
	if [ "${found:-0}" -ge "${4:-1}" ] 2>/dev/null; then
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
# The middle of the network icon, from the session's log.
icon() {
	guest "grep 'KWL NETWORK icon' $session | tail -1" | sed -n 's/.* x=\([0-9]*\) y=\([0-9]*\) width=\([0-9]*\) height=\([0-9]*\).*/\1 \2 \3 \4/p'
}
# The middle of the last laid-out row whose text is $1.
row() {
	guest "grep 'KWL NETWORK row .*text=$1\$' $session | tail -1" | sed -n 's/.* x=\([0-9]*\) y=\([0-9]*\) width=\([0-9]*\) height=\([0-9]*\).*/\1 \2 \3 \4/p'
}
open_menu() {
	set -- $(icon)
	if [ -z "${1:-}" ]; then
		echo "no network icon"
		status=1
		return
	fi
	click $(($1 + $3 / 2)) $(($2 + $4 / 2)) 1500
}

# 1. The boot's session (kei's autologin) stopped, the autologin emptied, and a greeter that offers kei.
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[s]essiond|[w]ayland( |$)" | awk "{print \$1}"); do kill $p; done; sleep 2'
guest "$stop_all
[ -f /tmp/p104-autologin.saved ] || cp /etc/keiland/autologin /tmp/p104-autologin.saved; : > /etc/keiland/autologin
rm -f /var/log/sessiond.log /var/log/greeter.log $session" >/dev/null
guest 'id kei' | tail -1 | sed 's/^/user: /'
guest "/sbin/sessiond --graphical </dev/null >/dev/null 2>&1 & sleep 1; echo started" >/dev/null
expect_log /var/log/greeter.log 'KWL GREETER open users=[0-9]+ selected=kei' 30
sleep 2
keys 'kei' '\n'
expect_log $session 'KWL HANDOFF go=1' 30
guest "grep 'SESSIOND SESSION start' /var/log/sessiond.log | tail -1" | tail -1 | sed 's/^/session: /'
expect_log $session 'KWL NETWORK state reachable=1 connected=1 kind=wired interface=[a-z]+[0-9]+ wifi=absent'
open_menu
expect_log $session 'KWL NETWORK open'
expect_log $session 'KWL NETWORK row .*text=Wired \([a-z]+[0-9]+\): connected'
pointer move 1100 300 sleep 400
shot wired-menu.png
click 400 400
expect_log $session 'KWL NETWORK close via=outside'

# 2. The stand-in in networkd's place with networkd's owner and mode; kei's session again.
guest 'mv /run/networkd.sock /run/networkd.sock.real; /bin/network-probe 300 > /tmp/probe.log 2>&1 </dev/null & sleep 1; chown root:network /run/networkd.sock; chmod 0660 /run/networkd.sock; ls -l /run/networkd.sock' | tail -1 | sed 's/^/stand-in: /'
expect_log /tmp/probe.log 'NETPROBE listening'
guest 'kill $(sed -n "s/.*SESSIOND SESSION start user=kei .* pid=\([0-9]*\).*/\1/p" /var/log/sessiond.log | tail -1); echo killed' >/dev/null
expect_log /var/log/greeter.log 'KWL GREETER open users=[0-9]+ selected=kei' 30 2
sleep 2
keys 'kei' '\n'
expect_log $session 'KWL HANDOFF go=1' 30
expect_log $session 'KWL NETWORK state reachable=1 connected=1 kind=wired interface=em9 wifi=disconnected'
open_menu
expect_log $session 'KWL NETWORK scan count=3'
expect_log $session 'KWL NETWORK row .*text=Kei Lab'
set -- $(row 'Kei Lab')
lx=$(($1 + 150)); ly=$(($2 + $4 / 2))
pointer move "$lx" "$ly" sleep 500
shot list.png
click "$lx" "$ly" 1500
# Since BUG-160 (ws005-p029) a secured network without a key in the user's own store opens the menu's key field;
# kei has none the first time, so a key is typed (saved in kei's store, then the join).  ws136-p002.
asked=$(guest "grep -ac 'KWL NETWORK key open ssid=Kei Lab' $session" | tail -1)
if [ "${asked:-0}" -gt 0 ] 2>/dev/null; then
	echo "key field: open, a key is typed"
	keys 'kei-lab-p104' '\n'
	expect_log $session 'KWL NETWORK key saved ssid=Kei Lab'
fi
expect_log /tmp/probe.log 'NETPROBE request op=35 ssid=Kei Lab' 15
expect_log $session 'KWL NETWORK state reachable=1 connected=1 kind=wifi interface=wlan0 wifi=connected ssid=Kei Lab'
pointer move 1100 500 sleep 500
shot joined.png
set -- $(row 'Wi-Fi')
click $(($1 + 150)) $(($2 + $4 / 2)) 1500
expect_log /tmp/probe.log 'NETPROBE request op=33'
expect_log $session 'KWL NETWORK state .*wifi=off'
click 400 400
pointer move 700 400 sleep 500
shot off.png

guest "cat $session" > "$out/session.log"
guest 'cat /tmp/probe.log' > "$out/probe.log"

# networkd's socket back.
guest 'for p in $(ps -A -o pid,args | grep -E "[n]etwork-probe" | awk "{print \$1}"); do kill $p; done; rm -f /run/networkd.sock; mv /run/networkd.sock.real /run/networkd.sock; net show' > "$out/net-show.txt"
grep -q 'online' "$out/net-show.txt" && echo "networkd: back" || { echo "networkd: not back"; status=1; }

# The autologin as it was (kei's session keeps running, as the boot's did).
guest "[ -f /tmp/p104-autologin.saved ] && cat /tmp/p104-autologin.saved > /etc/keiland/autologin && rm -f /tmp/p104-autologin.saved" >/dev/null
[ $status = 0 ] && echo "zdesktop-p104: PASS" || echo "zdesktop-p104: FAIL"
exit $status

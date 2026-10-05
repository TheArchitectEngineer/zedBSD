#!/bin/sh
# ws089-p022: a wired interface's configuration from the user's session, on the Venus guest of the Settings image
# (build-settings-image.sh) with the real networkd (not the stand-in of settings-wifi-bugs.sh).  A second wired
# interface is plugged in through QMP (netdev_add user on 10.0.9.0/24 + device_add usb-net) so that the guest's own
# network, which the test speaks SSH over, is never touched (its DHCP gives an address of its own subnet).  /etc/net.conf and /etc/resolv.conf are kept aside and put back.
#  1. net lan set (root): the new interface static 10.0.9.20/24 with DNS 10.0.9.53: answered, its address is up,
#     net.conf names it (address 10.0.9.20, prefix-length 24), resolv.conf names the server, the system log says
#     "LAN_CONFIGURE interface=IF mode=static ... result=ok".
#  2. A member of the network group (kei) may: static 10.0.9.21 (logged with euid=1000); one who is not (tester,
#     added for the run) may not, and is told why.
#  3. Bad input is refused by networkd with its reason in the system log and changes nothing: the subnet's broadcast,
#     a gap in the mask, a router outside the subnet, the loopback, a radio name.
#  4. Settings' Ethernet page (zdesktop --glass and Settings as root): the interface shows static (NETWORK wired-link
#     name=IF mode=2 address=10.0.9.21); "Use DHCP" on its IPv4 card goes through the compositor and the backend to
#     networkd (WIRED result interface=IF errno=0) and net.conf says dhcp for it (ethernet-static.png,
#     ethernet-dhcp.png).
#  5. Back as it was: net.conf and resolv.conf restored, the interface unplugged.
# The image must have su (plan/ws089/tests/config-amd64-settings.mk names it since T1-155).
# PASS: every "ok" line and the last line settings-p022: PASS.
#   plan/ws089/tests/settings-guest.sh start              (the guest must be up)
#   plan/ws089/tests/settings-p022.sh [OUTDIR]            (default build/ws089-shots/p022)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws089-run}"
export GUEST_RUNTIME
out=${1:-build/ws089-shots/p022}
mkdir -p "$out"
qmp="$GUEST_RUNTIME/qmp.sock"
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
send() { timeout 40 python3 plan/ws049/tests/qmp-send.py "$qmp" "$@" >> "$out/qmp.txt" 2>&1; }
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[s]ettings" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|[s]ettings" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
start_desktop='export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0; picture=; [ -f /usr/share/keiland/wallpaper.ppm ] && picture=--wallpaper=/usr/share/keiland/wallpaper.ppm
/bin/wayland --timeout=900 --width=1280 --height=800 --glass $picture > /tmp/zdesktop.log 2>&1 </dev/null & for w in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do grep -q ZWL.READY /tmp/zdesktop.log 2>/dev/null && break; sleep 0.5; done; sleep 1; echo started'
status=0
. plan/ws089/tests/settings-wait.sh
pass() { echo "$1: ok"; }
fail() { echo "$1: FAILED"; status=1; }
expect_log() {
	tries=0
	found=0
	while [ $tries -lt "${3:-10}" ]; do
		found=$(guest "grep -caE '$2' $1" | tail -1)
		[ "${found:-0}" -gt 0 ] 2>/dev/null && break
		tries=$((tries + 1))
		sleep 1
	done
	if [ "${found:-0}" -gt 0 ] 2>/dev/null; then echo "log: $2 ok"; else echo "log: $2 MISSING"; status=1; fi
}
shot() {
	pointer move 1270 790 sleep 300
	check "$out/$1" >/dev/null
	echo "shot: $out/$1"
}
: > "$out/qmp.txt"

# The guest, its files kept aside, and a second wired interface plugged in.
wait_guest
guest "$stop_all" >/dev/null
guest 'cp -p /etc/net.conf /tmp/net.conf.kept; cp -p /etc/resolv.conf /tmp/resolv.conf.kept 2>/dev/null; ifconfig -a | sed -n "s/^\\([a-z][a-z0-9]*\\):.*/\\1/p" > /tmp/if.before; echo kept' >/dev/null
send netdev_add '{"type":"user","id":"lan2","net":"10.0.9.0/24"}'
send device_add '{"driver":"usb-net","bus":"xhci.0","netdev":"lan2","id":"lan2dev"}'
sleep 5
interface=$(guest 'ifconfig -a | sed -n "s/^\\([a-z][a-z0-9]*\\):.*/\\1/p" > /tmp/if.after; for i in $(cat /tmp/if.after); do grep -qw "$i" /tmp/if.before || echo $i; done' | tail -1)
echo "plugged in: ${interface:-none}"
[ -n "$interface" ] && pass "a second wired interface" || fail "a second wired interface"
interface=${interface:-ue1}

# 1. root.
guest "net lan set $interface static 10.0.9.20 255.255.255.0 --dns 10.0.9.53; echo exit=\$?; sleep 3; ifconfig $interface; grep -A9 \"^  $interface:\" /etc/net.conf; cat /etc/resolv.conf" > "$out/root.txt"
grep -q '^exit=0$' "$out/root.txt" && pass "root configures it" || fail "root configures it"
grep -q 'inet 10.0.9.20' "$out/root.txt" && pass "its address is up" || fail "its address is up"
grep -q 'address: 10.0.9.20' "$out/root.txt" && grep -q 'prefix-length: 24' "$out/root.txt" && pass "net.conf names it" || fail "net.conf names it"
grep -q 'nameserver 10.0.9.53' "$out/root.txt" && pass "resolv.conf names the server" || fail "resolv.conf names the server"
expect_log /var/log/messages "LAN_CONFIGURE interface=$interface mode=static address=10.0.9.20 .*result=ok"

# 2. A member and one who is not ("tester", added here and removed at the end: the image has no other user).
guest 'grep -q "^tester:" /etc/passwd || { echo "tester:x:1001:1001:Tester:/tmp:/bin/sh" >> /etc/passwd; echo "tester:x:1001:" >> /etc/group; }' >/dev/null
guest "su kei -c 'net lan set $interface static 10.0.9.21 255.255.255.0'; echo exit=\$?; sleep 3; ifconfig $interface" > "$out/member.txt"
grep -q '^exit=0$' "$out/member.txt" && grep -q 'inet 10.0.9.21' "$out/member.txt" && pass "a member of the network group may" || fail "a member of the network group may"
expect_log /var/log/messages "LAN_CONFIGURE interface=$interface mode=static address=10.0.9.21 .*euid=1000 result=ok"
guest "su tester -c 'net lan set $interface dhcp' 2>&1; echo exit=\$?; grep -A3 \"^  $interface:\" /etc/net.conf" > "$out/nobody.txt"
grep -q '^exit=0$' "$out/nobody.txt" && fail "one who is not a member may not" || pass "one who is not a member may not"
grep -q 'authorization\|not permitted\|Permission\|unavailable' "$out/nobody.txt" && pass "the refusal says why" || fail "the refusal says why"
grep -q 'dhcp: true' "$out/nobody.txt" && fail "nothing changed for the refused" || pass "nothing changed for the refused"

# 3. Bad input: each refused by networkd with its reason (the system log), not by a crash of net (T1-157).
for bad in "$interface static 10.0.9.255 255.255.255.0:the subnet's own or broadcast address" "$interface static 10.0.9.30 255.0.255.0:invalid netmask" \
    "$interface static 10.0.9.30 255.255.255.0 10.0.8.1:router outside the subnet" "lo0 dhcp:not a wired interface name" "wlan0 dhcp:not a wired interface name"; do
	words=${bad%%:*}
	# The pattern goes inside single quotes on the guest: its apostrophe becomes "." (T1-159).
	reason=$(printf '%s' "${bad#*:}" | tr "'" .)
	result=$(guest "net lan set $words >/dev/null 2>&1; echo exit=\$?" | tail -1)
	[ "$result" = "exit=1" ] && pass "refuses: $words" || fail "refuses: $words ($result)"
	expect_log /var/log/messages "LAN_CONFIGURE interface=[a-z0-9]+ euid=0 result=error errno=[0-9]+ reason=$reason" 3
done
guest "grep -A9 \"^  $interface:\" /etc/net.conf" > "$out/after-bad.txt"
grep -q 'address: 10.0.9.21' "$out/after-bad.txt" && pass "bad input changed nothing" || fail "bad input changed nothing"

# 4. Settings' Ethernet page (Settings and zdesktop as root, the compositor's user): Use DHCP through the compositor.
guest "$start_desktop" >/dev/null
wait_desktop
guest "export XDG_RUNTIME_DIR=/tmp HOME=/root; /bin/settings --timeout-s=600 ethernet > /tmp/s.log 2>&1 </dev/null & sleep 6; echo started" >/dev/null
find_window
expect_log /tmp/s.log "NETWORK wired-link name=$interface mode=2 address=10.0.9.21"
shot ethernet-static.png
# ue1's card is below ue0's: the page is scrolled until its Use DHCP is in the window (T1-155).
cy=9999
for turn in 1 2 3 4; do
	index=$(guest "grep -a 'ZSETTINGS CONTROL index=34[0-9] ' /tmp/s.log | tail -1" | sed -n 's/.*index=\(34[0-9]\) .*/\1/p')
	set -- $(guest "grep -a 'ZSETTINGS CONTROL index=${index:-340} ' /tmp/s.log | tail -1" | sed -n 's/.* x=\([-0-9]*\) y=\([-0-9]*\) width=\([0-9]*\) height=\([0-9]*\).*/\1 \2 \3 \4/p') 0 0 0 0
	cx=$((wx + $1 + $3 / 2)); cy=$((wy + $2 + $4 / 2))
	[ "$2" -gt 0 ] 2>/dev/null && [ "$cy" -lt 760 ] && break
	pointer move $((wx + 700)) $((wy + 400)) sleep 200 wheel-down wheel-down wheel-down wheel-down wheel-down wheel-down wheel-down wheel-down sleep 1000
done
echo "Use DHCP (index ${index:-none}) at $cx,$cy"
pointer move $((cx - 2)) "$cy" sleep 150 move "$cx" "$cy" sleep 300 down sleep 60 up sleep 3000
expect_log /tmp/s.log "WIRED result interface=$interface errno=0" 20
expect_log /tmp/zdesktop.log "ZWL SYSTEM network client=[0-9]+ wired interface=$interface mode=1 error=0"
guest "grep -A4 \"^  $interface:\" /etc/net.conf" > "$out/settings-dhcp.txt"
grep -q 'dhcp: true' "$out/settings-dhcp.txt" && pass "Use DHCP is kept in net.conf" || fail "Use DHCP is kept in net.conf"
shot ethernet-dhcp.png

# 5. Back as it was.
guest "$stop_all" >/dev/null
guest 'cp -p /tmp/net.conf.kept /etc/net.conf; [ -f /tmp/resolv.conf.kept ] && cp -p /tmp/resolv.conf.kept /etc/resolv.conf; for f in /etc/passwd /etc/group; do grep -v "^tester:" $f > /tmp/p022.f && cat /tmp/p022.f > $f; done; echo restored' >/dev/null
send device_del '{"id":"lan2dev"}'
sleep 2
send netdev_del '{"id":"lan2"}'
guest 'grep LAN_CONFIGURE /var/log/messages | tail -20' > "$out/messages.txt"
guest 'grep -a "ZSETTINGS NETWORK\|ZSETTINGS WIRED" /tmp/s.log' > "$out/settings.log"
[ $status = 0 ] && echo "settings-p022: PASS" || echo "settings-p022: FAIL"
exit $status

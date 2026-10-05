#!/bin/sh
# BUG-183, 185, 186, 187 (q703): the Wi-Fi's switch, Connecting, the key form and a refused key, on the Venus guest of
# the Settings image (build-settings-image.sh), zdesktop --glass at 1280x800, with the networkd stand-in
# (network-probe 600 3 "Neighbor 5G" 1: a join takes 3 s and is told as connecting meanwhile; Neighbor 5G's key is
# wrong) in place of networkd, "Kei Lab" saved.  QEMU-only faking of the radio; the real radio is the UAT's.
#  1. BUG-183: the switch off then on: Settings shows the position asked at once (NETWORK switch shows on=0, on=1)
#     and settles when the state agrees (NETWORK switch settled); off.png, on.png.
#  2. BUG-185: Kei Lab joined: while the join waits, Settings has the state connecting with Kei Lab
#     (NETWORK state ... wifi=3 ssid=Kei Lab) and its Wi-Fi page says "Connecting to Kei Lab..." (connecting.png);
#     the system bar's state line says the same (ZWL NETWORK state ... connecting); then Connected, and (BUG-189)
#     with the wired em9 carrying the default route the active network is em9 (kind=1 interface=em9).
#  3. BUG-186: Neighbor 5G (a new key): its form opens; a wrong key typed and Enter: the form closes at once
#     (NETWORK key-form closed) while it connects (form-closed.png).
#  4. BUG-187 (Settings): the key refused: the form opens again, empty (NETWORK key-form again ssid=Neighbor 5G
#     errno=EACCES), with "Neighbor 5G did not accept the key. Check the key and try again." (refused.png); the stand-in
#     joins nothing else (no other WIFI_CONNECT after the refusal).
#  5. BUG-187 (system bar): Neighbor 5G chosen in the bar's menu (its key saved now): refused, the menu's note says
#     "Neighbor 5G did not accept the key"; Esc and the menu opened again: the note is still there (bar-refused.png).
# PASS: every "ok" line and the last line settings-wifi-bugs: PASS.
#   plan/ws089/tests/settings-guest.sh start              (the guest must be up)
#   plan/ws089/tests/settings-wifi-bugs.sh [OUTDIR]       (default build/ws089-shots/wifi-bugs)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws089-run}"
export GUEST_RUNTIME
out=${1:-build/ws089-shots/wifi-bugs}
mkdir -p "$out"
guest() { timeout 90 python3 plan/tools/guest/guest.py run "$1" 2>&1 </dev/null; }
check() { python3 plan/ws035/tests/zdesktop-check.py "$@" --runtime "$GUEST_RUNTIME"; }
pointer() { python3 plan/ws035/tests/qmp-pointer.py "$GUEST_RUNTIME/qmp.sock" "$@"; }
keys() { python3 plan/ws035/tests/qmp-keys.py "$GUEST_RUNTIME/qmp.sock" "$@"; sleep 0.7; }
stop_all='service stop greeter >/dev/null 2>&1; for p in $(ps -A -o pid,args | grep -E "[w]ayland( |$)|[s]ettings|[n]etwork-probe" | awk "{print \$1}"); do kill $p; done; i=0; while ps -A -o args | grep -qE "[w]ayland( |$)|[s]ettings|[n]etwork-probe" && [ $i -lt 50 ]; do sleep 0.2; i=$((i+1)); done'
start_desktop='export XDG_RUNTIME_DIR=/tmp; rm -f /tmp/wayland-0; picture=; [ -f /usr/share/keiland/wallpaper.png ] && picture=--wallpaper=/usr/share/keiland/wallpaper.png
/bin/wayland --timeout=900 --width=1280 --height=800 --glass $picture > /tmp/zdesktop.log 2>&1 </dev/null & for w in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do grep -q ZWL.READY /tmp/zdesktop.log 2>/dev/null && break; sleep 0.5; done; sleep 1; echo started'
status=0
. plan/ws089/tests/settings-wait.sh
eacces=$(sed -n 's/^#define EACCES \([0-9]*\).*/\1/p' include/uapi/errno.h | head -1)

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
count_log() { guest "grep -caE '$2' $1" | tail -1; }
start_settings() {
	guest "export XDG_RUNTIME_DIR=/tmp HOME=/root; /bin/settings --timeout-s=800 $1 > $2 2>&1 </dev/null & echo \$! > $2.pid; sleep 5; echo started" >/dev/null
	find_window
	echo "settings: window at $wx,$wy"
}
control() {
	set -- $(guest "grep 'ZSETTINGS CONTROL index=$1 ' $2 | tail -1" | sed -n 's/.* x=\([-0-9]*\) y=\([-0-9]*\) width=\([0-9]*\) height=\([0-9]*\).*/\1 \2 \3 \4/p')
	if [ -z "${1:-}" ]; then echo "control: not found"; status=1; return; fi
	cx=$((wx + $1 + $3 / 2)); cy=$((wy + $2 + $4 / 2))
	pointer move $((cx - 2)) "$cy" sleep 150 move "$cx" "$cy" sleep 300 down sleep 60 up sleep "${3:-1200}"
}
shot() {
	pointer move 1270 790 sleep 300
	check "$out/$1" >/dev/null
	echo "shot: $out/$1"
}
menu_icon() {
	set -- $(guest "grep -a 'ZWL NETWORK icon' /tmp/zdesktop.log | tail -1" | sed -n 's/.* x=\([0-9]*\) y=\([0-9]*\) width=\([0-9]*\) height=\([0-9]*\).*/\1 \2 \3 \4/p') 0 0 0 0
	ix=$(($1 + $3 / 2)); iy=$(($2 + $4 / 2))
	pointer move $((ix - 2)) "$iy" sleep 150 move "$ix" "$iy" sleep 300 down sleep 60 up sleep 1200
}
menu_row() {
	set -- $(guest "grep -a 'ZWL NETWORK row .*text=$1\$' /tmp/zdesktop.log | tail -1" | sed -n 's/.* x=\([0-9]*\) y=\([0-9]*\) width=\([0-9]*\) height=\([0-9]*\).*/\1 \2 \3 \4/p') 0 0 0 0
	rx=$(($1 + $3 / 3)); ry=$(($2 + $4 / 2))
	pointer move $((rx - 2)) "$ry" sleep 150 move "$rx" "$ry" sleep 300 down sleep 60 up sleep 1200
}

wait_guest
guest "$stop_all" >/dev/null
guest 'rm -f /etc/wifi.conf; net wifi add "Kei Lab" --password keilab-2026 --auto yes; echo saved' >/dev/null
guest 'mv /run/networkd.sock /run/networkd.sock.real; /bin/network-probe 600 3 "Neighbor 5G" 1 > /tmp/probe.log 2>&1 </dev/null & sleep 1; echo started' >/dev/null
expect_log /tmp/probe.log 'NETPROBE listening'
guest "$start_desktop" >/dev/null
wait_desktop
start_settings wifi /tmp/s.log
expect_log /tmp/s.log 'ZSETTINGS NETWORK scan count=3'

# 1. BUG-183.
control 1 /tmp/s.log 300
expect_log /tmp/s.log 'ZSETTINGS NETWORK switch shows on=0'
shot off.png
expect_log /tmp/s.log 'ZSETTINGS NETWORK switch settled wifi=1'
control 1 /tmp/s.log 300
expect_log /tmp/s.log 'ZSETTINGS NETWORK switch shows on=1'
shot on.png
expect_log /tmp/s.log 'ZSETTINGS NETWORK switch settled wifi=[2-5]'
expect_log /tmp/s.log 'ZSETTINGS NETWORK scan count=3'

# 2. BUG-185.
control 100 /tmp/s.log 800
expect_log /tmp/s.log 'ZSETTINGS NETWORK state .* wifi=3 ssid=Kei Lab' 4
shot connecting.png
expect_log /tmp/zdesktop.log 'ZWL NETWORK state .*connecting' 4
expect_log /tmp/s.log 'ZSETTINGS NETWORK message bad=0 text=Connected to Kei Lab.'
# BUG-189: Wi-Fi and the wired em9 both up, the default route through em9: the active network is the wired one.
expect_log /tmp/s.log 'ZSETTINGS NETWORK state reachable=1 connected=1 kind=1 interface=em9 wifi=4 ssid=Kei Lab'

# 3. BUG-186.
control 102 /tmp/s.log
expect_log /tmp/s.log 'ZSETTINGS NETWORK key-form ssid=Neighbor 5G'
keys 'wrong-key-1' '<ret>'
expect_log /tmp/s.log 'ZSETTINGS NETWORK key-form closed ssid=Neighbor 5G' 3
shot form-closed.png

# 4. BUG-187 in Settings.
expect_log /tmp/probe.log 'NETPROBE join refused ssid=Neighbor 5G'
expect_log /tmp/s.log "ZSETTINGS NETWORK key-form again ssid=Neighbor 5G errno=$eacces"
expect_log /tmp/s.log 'ZSETTINGS NETWORK message bad=1 text=Neighbor 5G did not accept the key. Check the key and try again.'
shot refused.png
before=$(count_log /tmp/probe.log 'NETPROBE request op=35 ')
sleep 4
after=$(count_log /tmp/probe.log 'NETPROBE request op=35 ')
[ "$before" = "$after" ] && echo "probe: nothing else joined ok" || { echo "probe: joined again ($before -> $after) FAIL"; status=1; }
keys '<esc>'

# 5. BUG-187 in the system bar.
menu_icon
menu_row 'Neighbor 5G'
sleep 1
opened=$(count_log /tmp/zdesktop.log 'ZWL NETWORK key open ssid=Neighbor 5G')
if [ "${opened:-0}" -gt 0 ] 2>/dev/null; then
	# The bar has no key of its own for it: typed there (the same wrong key).
	keys 'wrong-key-1' '<ret>'
	expect_log /tmp/zdesktop.log 'ZWL NETWORK key handed ssid=Neighbor 5G'
fi
expect_log /tmp/zdesktop.log 'ZWL NETWORK request join ssid=Neighbor 5G'
expect_log /tmp/zdesktop.log 'ZWL NETWORK row .*text=Neighbor 5G did not accept the key' 8
keys '<esc>'
menu_icon
sleep 1
expect_log /tmp/zdesktop.log 'ZWL NETWORK open'
shot bar-refused.png
notes=$(count_log /tmp/zdesktop.log 'ZWL NETWORK row .*text=Neighbor 5G did not accept the key')
[ "${notes:-0}" -ge 2 ] 2>/dev/null && echo "bar: the failure stays after reopening ok" || { echo "bar: the failure went with the menu FAIL"; status=1; }
keys '<esc>'

guest "$stop_all" >/dev/null
guest 'mv /run/networkd.sock.real /run/networkd.sock 2>/dev/null; echo restored' >/dev/null
guest 'cat /tmp/probe.log' > "$out/probe.log"
guest 'grep -a "ZSETTINGS NETWORK" /tmp/s.log' > "$out/settings.log"
guest 'grep -a "ZWL NETWORK" /tmp/zdesktop.log' > "$out/zdesktop.log"
[ $status = 0 ] && echo "settings-wifi-bugs: PASS" || echo "settings-wifi-bugs: FAIL"
exit $status

#!/bin/sh
# ws132-p002 on the events guest (plan/ws132/tests/config-amd64-events.mk, started with
#   plan/tools/guest/guest.py start IMAGE).
# What QEMU can check of the system's events (q35 has no lid, AC adapter or battery):
#  1. systemevents -x: the refusals of /dev/system (a read before the subscription, a subscription to
#     nothing, to an unknown class, with a reserved word, a buffer smaller than a record, an empty
#     nonblocking read) -> "refusals ok".
#  2. systemevents -p: the power's state is unknown -> "power lid=- ac=- battery=- charging=-".
#  3. A reader subscribed to every class while the host, through QMP, plugs in and pulls out a USB
#     stick (blank, 16 MiB), a USB keyboard and a USB network adapter, one after the other on the same
#     port 4 (the harness's devices take ports 1-3, and qemu-xhci's other ports are USB 3 ones a
#     full-speed device cannot use; T1-106), then presses the power button
#     (system_powerdown).  The reader must print, in increasing sequence:
#       usb add / disk add (removable=1; a disk is named sda) / disk remove / usb remove     (the stick)
#       usb add / input add / input remove / usb remove                 (the keyboard)
#       usb add / network add / network remove / usb remove             (the adapter)
#       power press 1 power-button
#     The order between the USB line and the device's line of one plug is not checked.
#  4. The guest still answers on SSH after the power button (it only posts the event).
#
#   plan/tools/guest/guest.py start IMAGE; plan/tools/guest/guest.py wait
#   plan/ws132/tests/p002-guest.sh [OUTDIR]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
runtime=${GUEST_RUNTIME:-$PWD/build/guest}
qmp="$runtime/qmp.sock"
out=${1:-build/ws132-p002}
mkdir -p "$out"
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1; }
send() { timeout 40 python3 plan/ws049/tests/qmp-send.py "$qmp" "$@" >> "$out/qmp.txt" 2>&1; }
status=0
pass() { echo "$1: ok"; }
fail() { echo "$1: FAILED"; status=1; }

# 1. The refusals.
guest '/bin/systemevents -x' > "$out/refusals.txt"
if grep -q '^refusals ok' "$out/refusals.txt"; then pass refusals; else fail refusals; fi

# 2. The power's state.
guest '/bin/systemevents -p' > "$out/power.txt"
if grep -q '^power lid=- ac=- battery=- charging=-' "$out/power.txt"; then pass power-state; else fail power-state; fi

# 3. The reader, then the plugs.
: > "$out/qmp.txt"
guest '(/bin/systemevents -t 20000 > /tmp/events.txt 2>&1 &) ; sleep 1; cat /tmp/events.txt' > "$out/start.txt"
if ! grep -q '^ready' "$out/start.txt"; then fail reader-ready; fi
stick="$out/stick.img"
truncate -s 16M "$stick"
send blockdev-add "{\"driver\":\"raw\",\"node-name\":\"hotstick0\",\"file\":{\"driver\":\"file\",\"filename\":\"$(realpath "$stick")\"}}"
send device_add '{"driver":"usb-storage","bus":"xhci.0","port":"4","drive":"hotstick0","id":"hotstick"}'
sleep 4
send device_del '{"id":"hotstick"}'
sleep 3
send blockdev-del '{"node-name":"hotstick0"}'
send device_add '{"driver":"usb-kbd","bus":"xhci.0","port":"4","id":"hotkbd"}'
sleep 3
send device_del '{"id":"hotkbd"}'
sleep 3
send netdev_add '{"type":"user","id":"hotnet"}'
send device_add '{"driver":"usb-net","bus":"xhci.0","port":"4","netdev":"hotnet","id":"hotnic","mac":"52:54:00:33:00:02"}'
sleep 4
send device_del '{"id":"hotnic"}'
sleep 3
send system_powerdown
sleep 3
guest 'cat /tmp/events.txt' > "$out/events.txt"

# Each expected line, in the order of the plugs.
check_line() {
	if grep -Eq "$2" "$out/events.txt"; then pass "$1"; else fail "$1"; fi
}
check_line stick-usb-add '^event [0-9]+ usb add 0 usb[0-9]+\.[0-9]+ port=4 vendor=46f4 product=0001'
check_line stick-disk-add '^event [0-9]+ disk add 0 [a-z]+[0-9]* parent=- removable=1 block=512 blocks=32768'
check_line stick-disk-remove '^event [0-9]+ disk remove 0 [a-z]+[0-9]* parent=- removable=1'
check_line stick-usb-remove '^event [0-9]+ usb remove 0 usb[0-9]+\.[0-9]+ port=4 vendor=46f4'
check_line keyboard-usb-add '^event [0-9]+ usb add 0 usb[0-9]+\.[0-9]+ port=4 vendor=0627'
check_line keyboard-input-add '^event [0-9]+ input add 0 event[0-9]+ bus=3 '
check_line keyboard-input-remove '^event [0-9]+ input remove 0 event[0-9]+ bus=3 '
check_line keyboard-usb-remove '^event [0-9]+ usb remove 0 usb[0-9]+\.[0-9]+ port=4 vendor=0627'
check_line nic-usb-add '^event [0-9]+ usb add 0 usb[0-9]+\.[0-9]+ port=4 vendor=0525'
check_line nic-network-add '^event [0-9]+ network add 0 [a-z]+[0-9]+ ifindex=[0-9]+'
check_line nic-network-remove '^event [0-9]+ network remove 0 [a-z]+[0-9]+ ifindex=[0-9]+'
check_line nic-usb-remove '^event [0-9]+ usb remove 0 usb[0-9]+\.[0-9]+ port=4 vendor=0525'
check_line power-button '^event [0-9]+ power press 1 power-button -'
check_line any-event '^event'
if grep -Eq ' (overflow|unknown) ' "$out/events.txt"; then fail no-overflow-or-unknown-lines; fi

# The order: the stick's lines before the keyboard's, the keyboard's before the adapter's, the button last;
# and the sequence numbers increase.
order=$(awk '/^event/ { print $2, $3, $4 }' "$out/events.txt")
if printf '%s\n' "$order" | awk 'NR > 1 && $1 <= last { bad = 1 } { last = $1 } END { exit bad }'; then
	pass sequence
else
	fail sequence
fi
first_input=$(grep -n ' input add ' "$out/events.txt" | head -1 | cut -d: -f1)
last_disk=$(grep -n ' disk remove ' "$out/events.txt" | tail -1 | cut -d: -f1)
first_network=$(grep -n ' network add ' "$out/events.txt" | head -1 | cut -d: -f1)
press=$(grep -n ' power press ' "$out/events.txt" | tail -1 | cut -d: -f1)
if [ -n "$first_input" ] && [ -n "$last_disk" ] && [ -n "$first_network" ] && [ -n "$press" ] &&
	[ "$last_disk" -lt "$first_input" ] && [ "$first_input" -lt "$first_network" ] && [ "$first_network" -lt "$press" ]; then
	pass order
else
	fail order
fi

# 4. The guest still answers.
guest 'echo alive' > "$out/alive.txt"
if grep -q '^alive' "$out/alive.txt"; then pass alive; else fail alive; fi

echo "p002-guest: status $status (outputs in $out)"
exit $status

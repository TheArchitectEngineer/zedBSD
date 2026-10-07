#!/bin/sh
# ws113-p004a: plugs or unplugs a head of the Venus guest's virtio-gpu, for the hotplug and output tests of WS113.
# The guest must be started with VENUS_DISPLAY=dbus (plan/ws035/tests/zdesktop-guest.sh, and the launchers that call
# it), which puts QEMU's D-Bus display on the runtime's own bus.  A head is enabled when its console is given a size
# (SetUIInfo) and disabled when given 0x0; QEMU then sends the guest VIRTIO_GPU_EVENT_DISPLAY, as a monitor plugged or
# unplugged.
#
#   plan/tools/guest/venus-head.sh list                 the consoles: number, head, label
#   plan/tools/guest/venus-head.sh HEAD WIDTHxHEIGHT    plugs head HEAD of the Venus device at that size
#   plan/tools/guest/venus-head.sh HEAD off             unplugs it
#
# GUEST_RUNTIME names the runtime (default build/ws071-run, the files guest's).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
runtime=${GUEST_RUNTIME:-$PWD/build/ws071-run}
if [ ! -s "$runtime/dbus.address" ]; then
	echo "venus-head: no D-Bus display in $runtime (start the guest with VENUS_DISPLAY=dbus)" >&2
	exit 1
fi
DBUS_SESSION_BUS_ADDRESS=$(cat "$runtime/dbus.address")
export DBUS_SESSION_BUS_ADDRESS

# One property of a console, as gdbus prints it ("(<value>,)").
property() {
	timeout 10 gdbus call --session --dest org.qemu --object-path "/org/qemu/Display1/Console_$1" \
	    --method org.freedesktop.DBus.Properties.Get org.qemu.Display1.Console "$2" 2>/dev/null |
	    sed 's/^(<\(.*\)>,)$/\1/'
}

# The console of a head of the Venus device: the virtio-gpu consoles carry the head's number (the standard VGA
# adapter's console is head 0 too, and is told apart by its label).
console_of() {
	number=0
	while [ "$number" -lt 8 ]; do
		head=$(property "$number" Head)
		label=$(property "$number" Label)
		case "$label" in
		*irtio*|*venus*)
			if [ "$head" = "uint32 $1" ] || [ "$head" = "$1" ]; then
				echo "$number"
				return 0
			fi
			;;
		esac
		number=$((number + 1))
	done
	return 1
}

case "${1:-list}" in
list)
	number=0
	while [ "$number" -lt 8 ]; do
		label=$(property "$number" Label)
		if [ -n "$label" ]; then
			echo "console $number head $(property "$number" Head) label $label"
		fi
		number=$((number + 1))
	done
	;;
*)
	head=$1
	size=${2:?WIDTHxHEIGHT or off}
	console=$(console_of "$head") || { echo "venus-head: no console of head $head" >&2; exit 1; }
	width=0
	height=0
	if [ "$size" != off ]; then
		width=${size%x*}
		height=${size#*x}
	fi
	timeout 10 gdbus call --session --dest org.qemu --object-path "/org/qemu/Display1/Console_$console" \
	    --method org.qemu.Display1.Console.SetUIInfo 0 0 0 0 "$width" "$height" >/dev/null
	echo "venus-head: head $head (console $console) ${width}x${height}"
	;;
esac

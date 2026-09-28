#!/bin/sh
# ws075: runs on the 5330 (bigbang/usb-poll.sh) until bigbang/usb-poll-stop appears, at most 600 s.  Once a second it
# records the USB devices (sysfs) with the latest i915 HDMI or resident-display line of the guest's serial log, and for
# every device that was not there when it started it saves `lsusb -v`, the HID report descriptors of the device's
# interfaces and `usbhid-dump -e descriptor` while the device is present: bigbang/usb-poll.log and bigbang/usb-new/.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
cd /home/awe/bigbang
rm -rf usb-new usb-poll.log usb-poll-stop
mkdir -p usb-new
list() {
	for p in /sys/bus/usb/devices/*; do
		[ -f "$p/idVendor" ] || continue
		echo "$(basename "$p") $(cat "$p/idVendor"):$(cat "$p/idProduct") $(cat "$p/manufacturer" 2>/dev/null) / $(cat "$p/product" 2>/dev/null)"
	done
}
list > usb-baseline.txt
i=0
while [ ! -f usb-poll-stop ] && [ $i -lt 600 ]; do
	now=$(list)
	phase=$(grep -aE 'i915: (HDMI|LCD-B|display output|resident display)' run-parity-serial.log 2>/dev/null | tail -1 | cut -c1-160)
	echo "== $(date +%T) t=$i | $phase" >> usb-poll.log
	echo "$now" >> usb-poll.log
	echo "$now" | while read -r path id rest; do
		grep -q "^$path $id" usb-baseline.txt && continue
		d=usb-new/$(echo "$id" | tr : _)
		[ -d "$d" ] && continue
		mkdir -p "$d"
		echo "$path $id $rest (first seen t=$i $(date +%T))" > "$d/device.txt"
		sleep 1
		lsusb -v -d "$id" > "$d/lsusb-v.txt" 2>&1
		for h in /sys/bus/hid/devices/*; do
			case "$(readlink -f "$h")" in */$path/*|*/$path:*) ;; *) continue ;; esac
			n=$(basename "$h")
			od -An -tx1 -v "$h/report_descriptor" > "$d/rdesc-$n.hex" 2>&1
			cat "$h/uevent" > "$d/uevent-$n.txt" 2>&1
		done
		sudo -n usbhid-dump -m "$id" -e descriptor > "$d/usbhid-dump.txt" 2>&1
	done
	sleep 1
	i=$((i + 1))
done

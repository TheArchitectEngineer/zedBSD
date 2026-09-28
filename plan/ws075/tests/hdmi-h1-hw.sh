#!/bin/sh
# ws075 H1: one run of an HDMI scenario of the i915 test build on the 5330 (plan/ws031/tests/vkloop-hw.sh test
# SCENARIO, built with plan/ws075/tests/config-test-hw.mk into BUILD) while the 5330's own Linux watches its USB
# ports: the touch LCD on HDMI + USB may bring up its touch and pen HID only once HDMI carries a picture.  The guest
# has no passthrough of the physical USB controllers, so the host's view is the USB evidence.
#
# Holds the machine's lock for the whole run.  On the 5330 a poller (bigbang/h1-usb-poll.sh) records, once a second,
# the USB devices (vendor:product and name from sysfs) with the latest i915 HDMI line of the guest's serial log, and
# for every device that was not there before the run it saves `lsusb -v`, the HID report descriptors of its
# interfaces (/sys/bus/hid/devices/*/report_descriptor) and `usbhid-dump -e descriptor` while the device is present.
# Everything lands in OUTDIR: run.log, serial.log, build.log (as test-hw.sh), usb-before.txt, usb-after.txt,
# usb-poll.log and new-devices/.
#
#   plan/ws075/tests/hdmi-h1-hw.sh SCENARIO OUTDIR ["-DFLAG=1 ..."]
#       e.g. plan/ws075/tests/hdmi-h1-hw.sh hdmib build/ws075-h1/hdmib-720 "-DI915_TEST_HDMIB_WINDOW_MS=60000"
#   BUILD (default build/h1) and I915_HOST (default solaris10-man) pass through.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
[ $# -ge 2 ] || { echo "usage: $0 SCENARIO OUTDIR [FLAGS]"; exit 2; }
scenario=$1
out=$2
flags=${3:-}
cd "$(dirname -- "$0")/../../.."
rm -rf "$out"
mkdir -p "$out"
BUILD=${BUILD:-build/h1}
ZEDBSD_CONFIG=${ZEDBSD_CONFIG:-plan/ws075/tests/config-test-hw.mk}
I915_HOST=${I915_HOST:-solaris10-man}
export BUILD ZEDBSD_CONFIG I915_HOST

# The poller: runs on the 5330 until its stop file appears (at most 600 s).
cat > "$out/h1-usb-poll.sh" <<'EOF'
#!/bin/sh
cd /home/awe/bigbang
rm -rf h1-new h1-usb-poll.log h1-usb-stop
mkdir -p h1-new
list() {
	for p in /sys/bus/usb/devices/*; do
		[ -f "$p/idVendor" ] || continue
		echo "$(basename "$p") $(cat "$p/idVendor"):$(cat "$p/idProduct") $(cat "$p/manufacturer" 2>/dev/null) / $(cat "$p/product" 2>/dev/null)"
	done
}
list > h1-usb-baseline.txt
i=0
while [ ! -f h1-usb-stop ] && [ $i -lt 600 ]; do
	now=$(list)
	phase=$(grep -aE 'i915: (HDMI|LCD-B|test )' run-parity-serial.log 2>/dev/null | tail -1 | cut -c1-160)
	echo "== $(date +%T) t=$i | $phase" >> h1-usb-poll.log
	echo "$now" >> h1-usb-poll.log
	echo "$now" | while read -r path id rest; do
		grep -q "^$path $id" h1-usb-baseline.txt && continue
		d=h1-new/$(echo "$id" | tr : _)
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
EOF

# The machine, for the whole run; what the run left in /tmp is copied before the lock goes.
exec 9>/tmp/i915-hw.lock
flock 9
ssh $I915_HOST 'lsusb; echo; for p in /sys/bus/usb/devices/*; do [ -f $p/idVendor ] && echo "$(basename $p) $(cat $p/idVendor):$(cat $p/idProduct) $(cat $p/product 2>/dev/null)"; done' > "$out/usb-before.txt" 2>&1
scp -q "$out/h1-usb-poll.sh" $I915_HOST:bigbang/h1-usb-poll.sh
ssh $I915_HOST 'rm -f bigbang/run-parity-serial.log; nohup sh bigbang/h1-usb-poll.sh >/dev/null 2>&1 &'
rm -f /tmp/vkloop-last.log
plan/ws031/tests/vkloop-hw.sh "test $scenario $flags" > "$out/run.log" 2>&1
status=$?
ssh $I915_HOST 'touch bigbang/h1-usb-stop'
sleep 3
ssh $I915_HOST 'lsusb; echo; for p in /sys/bus/usb/devices/*; do [ -f $p/idVendor ] && echo "$(basename $p) $(cat $p/idVendor):$(cat $p/idProduct) $(cat $p/product 2>/dev/null)"; done; echo; sudo -n dmesg | tail -60' > "$out/usb-after.txt" 2>&1
scp -q $I915_HOST:bigbang/h1-usb-poll.log "$out/usb-poll.log"
scp -qr $I915_HOST:bigbang/h1-new "$out/new-devices"
[ -f /tmp/vkloop-last.log ] && cp /tmp/vkloop-last.log "$out/serial.log"
[ -f "$BUILD/resident-build.log" ] && cp "$BUILD/resident-build.log" "$out/build.log"
flock -u 9
echo "hdmi-h1-hw: $scenario vkloop-hw.sh exit=$status; $out/run.log"
grep -aE 'verdict|HDMI-B EDID|HDMI-EDID [A-Z]' "$out/run.log" | head -8
echo "new USB devices: $(ls "$out/new-devices" 2>/dev/null | tr '\n' ' ')"
exit $status

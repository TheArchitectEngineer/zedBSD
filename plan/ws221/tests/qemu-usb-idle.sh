#!/bin/sh
# An idle QEMU guest with a USB hub keeps its CPUs idle: the hub's status poll waits a quarter second at a time, and a
# synchronous USB wait used to spin on the CPU for all of it (one CPU always busy).  Builds the image
# (config-amd64-idle.mk) unless IMAGE is given, starts the SSH guest with a usb-hub on its xHCI and a keyboard behind
# it, lets it settle, reads hw.cputimes twice ten seconds apart over SSH and prints each CPU's busy share.
#   plan/ws221/tests/qemu-usb-idle.sh [IMAGE]
# The guest runs in build/ws221-qemu/run (GUEST_RUNTIME), the image is built in build/ws221-idle.  The last line is
# "qemu-usb-idle: PASS" when every CPU is busy less than 10 % of the time, else FAIL.  Judged over SSH, never the
# console.  Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
image=${1:-}
if [ -z "$image" ]; then
	flock /tmp/zedbsd-image-build.lock timeout 3600 sh plan/tools/guest/test-image.sh plan/ws221/tests/config-amd64-idle.mk \
	    build/ws221-idle > build/ws221-idle.log 2>&1 || { echo "qemu-usb-idle: FAIL (image build, build/ws221-idle.log)"; exit 1; }
	image=build/ws221-idle/hdd-image.img
fi
GUEST_RUNTIME=$(pwd)/build/ws221-qemu/run
export GUEST_RUNTIME
guest() { timeout "${T:-120}" python3 plan/tools/guest/guest.py "$@"; }
trap 'guest stop > /dev/null 2>&1' EXIT
guest start "$image" --qemu-extra "-device usb-hub,bus=xhci.0,port=5,id=hub1 -device usb-mouse,bus=xhci.0,port=5.1" ||
    { echo "qemu-usb-idle: FAIL (start)"; exit 1; }
T=300 guest wait --timeout 240 || { echo "qemu-usb-idle: FAIL (no SSH)"; exit 1; }
sleep 30
out=$(guest run 'lsusb; sysctl hw.cputimes; sleep 10; sysctl hw.cputimes' 2>&1 | tr -d '\r')
echo "$out" | grep -v cputimes
busy=$(echo "$out" | awk '/hw.cputimes: cpu=/ { for (i = 2; i <= NF; i++) { split($i, kv, "="); v[kv[1]] = kv[2] }
	c = v["cpu"]; t = v["user"] + v["system"] + v["idle"] + v["other"]; w = v["user"] + v["system"]
	if (c in bt) { d = t - bt[c]; p = 0; if (d > 0) p = (w - bw[c]) * 100 / d; printf "cpu%s=%.1f%% ", c, p; if (p >= 10) bad = 1 }
	else { bt[c] = t; bw[c] = w } } END { if (bad) printf "BAD" }')
echo "busy: $busy"
case $busy in
*BAD*|'') echo "qemu-usb-idle: FAIL"; exit 1 ;;
esac
echo "qemu-usb-idle: PASS"

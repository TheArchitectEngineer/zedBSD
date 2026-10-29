#!/bin/sh
# The QEMU xHCI's USB devices after a change of the host controller driver: builds the image (config-amd64-usb.mk)
# unless IMAGE is given and starts the SSH guest (boot disk a usb-storage stick, network a usb-net CDC ECM adapter, a
# usb-kbd, all on the xHCI) named p3-ws221 with a usb-hub on port 5 carrying a usb-mouse and a usb-tablet.  Over SSH:
#   lsusb lists the hub, the stick, the adapter and the three HID devices (vendor 0627);
#   a 16 MiB random file goes in over SSH (ECM) onto the stick, is copied there, compared with cmp, and comes back out
#   identical (checked on this host);
#   plan/ws221/tests/qemu-tcp.sh's transfers are not repeated here (run it separately).
#   plan/ws221/tests/qemu-usb-xhci.sh [IMAGE]
# The guest runs in build/ws221-qemu/xhci (GUEST_RUNTIME), the image is built in build/ws221-usb; the files go to
# build/ws221-qemu/xhci-files.  The last line is "qemu-usb-xhci: PASS" or FAIL.  Judged over SSH, never the console.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
image=${1:-}
if [ -z "$image" ]; then
	flock /tmp/zedbsd-image-build.lock timeout 3600 sh plan/tools/guest/test-image.sh plan/ws221/tests/config-amd64-usb.mk \
	    build/ws221-usb > build/ws221-usb.log 2>&1 || { echo "qemu-usb-xhci: FAIL (image build, build/ws221-usb.log)"; exit 1; }
	image=build/ws221-usb/hdd-image.img
fi
GUEST_RUNTIME=$(pwd)/build/ws221-qemu/xhci
export GUEST_RUNTIME
. plan/tools/fresh-out.sh
fresh_out build/ws221-qemu/xhci-files
files=build/ws221-qemu/xhci-files
guest() { timeout "${T:-120}" python3 plan/tools/guest/guest.py "$@"; }
trap 'guest stop > /dev/null 2>&1' EXIT
guest start "$image" --disk usb --qemu-extra "-name p3-ws221 -device usb-hub,bus=xhci.0,port=5,id=hub1 -device usb-mouse,bus=xhci.0,port=5.1 -device usb-tablet,bus=xhci.0,port=5.2" ||
    { echo "qemu-usb-xhci: FAIL (start)"; exit 1; }
T=600 guest wait --timeout 540 || { echo "qemu-usb-xhci: FAIL (no SSH)"; exit 1; }
sleep 10
fail=0

# The devices on the bus.
guest run lsusb > "$files/lsusb.txt" 2>&1
hid=$(grep -c 'ID 0627:' "$files/lsusb.txt")
echo "lsusb: $(grep -c 'ID ' "$files/lsusb.txt") devices, HID (0627) $hid"
for id in 0409:55aa 46f4:0001 0525:a4a2; do
	grep -q "ID $id" "$files/lsusb.txt" || { echo "missing $id"; fail=1; }
done
[ "$hid" -ge 3 ] || { echo "HID devices: $hid of 3"; fail=1; }

# A random file in over ECM onto the stick, copied there, compared, and back out.
head -c 16777216 /dev/urandom > "$files/in.bin"
T=300 guest put "$files/in.bin" /root/p014-in.bin || { echo "put failed"; fail=1; }
T=300 guest run "cp /root/p014-in.bin /root/p014-copy.bin && sync && cmp /root/p014-in.bin /root/p014-copy.bin && cksum /root/p014-copy.bin" \
    > "$files/guest-cmp.txt" 2>&1 || { echo "guest copy/cmp failed: $(cat "$files/guest-cmp.txt")"; fail=1; }
T=300 guest get /root/p014-copy.bin "$files/out.bin" || { echo "get failed"; fail=1; }
cmp "$files/in.bin" "$files/out.bin" > /dev/null 2>&1 || { echo "the file came back different"; fail=1; }
echo "storage+ecm: in $(cksum < "$files/in.bin" | cut -d' ' -f1) guest $(cut -d' ' -f1 "$files/guest-cmp.txt" 2>/dev/null) out $(cksum < "$files/out.bin" 2>/dev/null | cut -d' ' -f1)"

# SSH still answers.
guest run true || { echo "SSH gone"; fail=1; }
if [ "$fail" = 0 ]; then
	echo "qemu-usb-xhci: PASS"
else
	echo "qemu-usb-xhci: FAIL"
	exit 1
fi

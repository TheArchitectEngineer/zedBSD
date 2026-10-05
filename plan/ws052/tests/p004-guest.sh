#!/bin/sh
# ws052-p004 on the sleep guest (plan/ws052/tests/config-amd64-sleep.mk): the devices' suspend and resume through
# KERN_SYSTEM_SLEEP's devices mode (sleepctl), which suspends every device and resumes it at once without the
# processors' low-power idle.  Two runs, each on a fresh guest:
#
#   roundtrip   plan/tools/guest/guest.py start IMAGE; plan/tools/guest/guest.py wait
#               plan/ws052/tests/p004-guest.sh roundtrip [OUTDIR]
#     The guest boots from NVMe and its network (SSH) is a USB adapter on the xHCI controller, as guest.py sets up.
#     q35 binds no other driver.
#     1. sleepctl -x: an unknown mode and a nonzero reserved word are refused (EINVAL) -> "refusals ok".
#     2. A file is written and synced on the NVMe root file system, and its checksum kept.
#     3. sleepctl devices runs in the background, its output to /tmp/sleep.txt (the SSH session goes away with the
#        USB adapter).  The host waits, then reconnects for up to 90 s.  It must print
#        "sleep result=0 resume=0 device=-": NVMe suspended (shutdown, D3hot) and resumed (fresh queues), xHCI
#        suspended (Save State, ports in U3, D3hot) and resumed: QEMU's xHCI implements no Restore State (SRE), so the
#        driver runs it with the state it kept and checks that it answers a command (T1-156: detaching it to attach it
#        again cannot work, the USB core keeps its devices).
#     4. The guest answers on SSH again, the file reads back with its checksum, a new file of 16 MiB is written,
#        synced and read back, lsusb lists the adapter and the keyboard again, and the kernel's log (dmesg, read over
#        SSH) has "nvme: suspended", "nvme: resumed", "xhci: suspended" and "xhci: resumed".
#
#   hda         plan/tools/guest/guest.py start --qemu-extra '-device intel-hda -device hda-duplex' IMAGE
#               plan/tools/guest/guest.py wait; plan/ws052/tests/p004-guest.sh hda [OUTDIR]
#     The HD Audio controller now suspends (ws052-p005): audiod keeps its stream running, and the driver stops the
#     stream, holds the controller in reset, and starts it all again at the resume.
#     1. sleepctl devices (in the background, as above) prints "sleep result=0 resume=0 device=-".
#     2. The guest answers on SSH again, a new file of 16 MiB on the NVMe root is written, synced and read back.
#     3. dmesg has "hda: suspended" and "hda: resumed".
#
#   abort       plan/tools/guest/guest.py start --qemu-extra '-device piix3-usb-uhci' IMAGE
#               plan/tools/guest/guest.py wait; plan/ws052/tests/p004-guest.sh abort [OUTDIR]
#     The UHCI controller's driver has no suspend: the suspend must stop at it and resume what it suspended before
#     (the functions at lower slots, which may include the xHCI controller and so the SSH adapter).
#     1. sleepctl devices (in the background, as above) prints "sleep result=21 resume=0 device=pci 0000:00:XX.X uhci"
#        (21 is EOPNOTSUPP), and exits 1.
#     2. The guest still answers on SSH, and a new file of 16 MiB on the NVMe root is written, synced and read back.
#     3. dmesg has "pci: suspend of 0000:00:XX.X failed (error 21)".
#
# The serial and console logs are not read; the kernel's log is read with dmesg over SSH.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
mode=${1:-roundtrip}
out=${2:-build/ws052-p004-$mode}
mkdir -p "$out"
guest() { timeout 120 python3 plan/tools/guest/guest.py run "$1" 2>&1; }
status=0
pass() { echo "$1: ok"; }
fail() { echo "$1: FAILED"; status=1; }

# Waits until the guest answers on SSH again, for up to 90 s.
reconnect() {
	tries=0
	while [ $tries -lt 18 ]; do
		if timeout 15 python3 plan/tools/guest/guest.py run 'echo back' 2>/dev/null | grep -q '^back'; then
			return 0
		fi
		tries=$((tries + 1))
		sleep 5
	done
	return 1
}

# Writes 16 MiB, syncs, drops the cache and reads it back; prints "io ok" when the checksums agree.
io_check() {
	guest 'mkdir -p /var/tmp && dd if=/dev/urandom of=/var/tmp/p004-new bs=1048576 count=16 2>/dev/null && a=$(cksum < /var/tmp/p004-new) && sync && b=$(cksum < /var/tmp/p004-new) && [ "$a" = "$b" ] && echo "io ok"'
}

case "$mode" in
roundtrip)
	# 1. The refusals.
	guest '/bin/sleepctl -x' > "$out/refusals.txt"
	if grep -q '^refusals ok' "$out/refusals.txt"; then pass refusals; else fail refusals; fi

	# 2. A file written before the sleep.
	guest 'mkdir -p /var/tmp && dd if=/dev/urandom of=/var/tmp/p004-old bs=1048576 count=4 2>/dev/null; sync; cksum < /var/tmp/p004-old' > "$out/before.txt"

	# 3. The round trip, in the background.
	guest '(/bin/sleepctl devices > /tmp/sleep.txt 2>&1; echo "exit=$?" >> /tmp/sleep.txt) > /dev/null 2>&1 &
sleep 1; echo started' > "$out/start.txt"
	sleep 20
	if reconnect; then pass reconnect; else fail reconnect; fi
	guest 'cat /tmp/sleep.txt' > "$out/sleep.txt"
	if grep -q '^sleep result=0 resume=0 device=-' "$out/sleep.txt"; then pass round-trip; else fail round-trip; fi

	# 4. What runs after the resume.
	guest 'cksum < /var/tmp/p004-old' > "$out/after.txt"
	if [ -s "$out/before.txt" ] && cmp -s "$out/before.txt" "$out/after.txt"; then pass old-file; else fail old-file; fi
	io_check > "$out/io.txt"
	if grep -q '^io ok' "$out/io.txt"; then pass io; else fail io; fi
	guest '/bin/lsusb' > "$out/lsusb.txt"
	if grep -qi 'keyboard\|0627:0001' "$out/lsusb.txt"; then pass usb-keyboard; else fail usb-keyboard; fi
	guest '/bin/dmesg' > "$out/dmesg.txt"
	for line in 'nvme: suspended' 'nvme: resumed' 'xhci: suspended' 'xhci: resumed'; do
		if grep -q "$line" "$out/dmesg.txt"; then pass "dmesg '$line'"; else fail "dmesg '$line'"; fi
	done
	;;
hda)
	# 1. The round trip with the HD Audio controller.
	guest '(/bin/sleepctl devices > /tmp/sleep.txt 2>&1; echo "exit=$?" >> /tmp/sleep.txt) > /dev/null 2>&1 &
sleep 1; echo started' > "$out/start.txt"
	sleep 20
	if reconnect; then pass reconnect; else fail reconnect; fi
	guest 'cat /tmp/sleep.txt' > "$out/sleep.txt"
	if grep -q '^sleep result=0 resume=0 device=-' "$out/sleep.txt"; then pass round-trip; else fail round-trip; fi

	# 2. The guest still works.
	io_check > "$out/io.txt"
	if grep -q '^io ok' "$out/io.txt"; then pass io; else fail io; fi

	# 3. The kernel's log.
	guest '/bin/dmesg' > "$out/dmesg.txt"
	for line in 'hda: suspended' 'hda: resumed'; do
		if grep -q "$line" "$out/dmesg.txt"; then pass "dmesg '$line'"; else fail "dmesg '$line'"; fi
	done
	;;
abort)
	# 1. The suspend stops at the UHCI controller; the xHCI controller, at a lower slot, may have been suspended
	#    and resumed before, so the SSH session may go and the command runs in the background.
	guest '(/bin/sleepctl devices > /tmp/sleep.txt 2>&1; echo "exit=$?" >> /tmp/sleep.txt) > /dev/null 2>&1 &
sleep 1; echo started' > "$out/start.txt"
	sleep 20
	if reconnect; then pass reconnect; else fail reconnect; fi
	guest 'cat /tmp/sleep.txt' > "$out/sleep.txt"
	if grep -q '^sleep result=21 resume=0 device=pci 0000:00:[0-9a-f][0-9a-f]\.[0-7] uhci' "$out/sleep.txt" &&
	    grep -q '^exit=1' "$out/sleep.txt"; then
		pass abort-reason
	else
		fail abort-reason
	fi

	# 2. The guest still works.
	io_check > "$out/io.txt"
	if grep -q '^io ok' "$out/io.txt"; then pass io; else fail io; fi

	# 3. The kernel's log names the function.
	guest '/bin/dmesg' > "$out/dmesg.txt"
	if grep -q 'pci: suspend of 0000:00:[0-9a-f][0-9a-f]\.[0-7] failed (error 21)' "$out/dmesg.txt"; then pass dmesg; else fail dmesg; fi
	;;
*)
	echo "usage: p004-guest.sh roundtrip|hda|abort [OUTDIR]" >&2
	exit 2
	;;
esac

exit $status

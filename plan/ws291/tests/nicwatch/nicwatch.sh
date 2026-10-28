#!/bin/sh
# ws291: the RX 5600 XT machine's USB LAN adapter (ue0, RTL8156) sometimes gets no link after Debian used it before a
# warm reboot, and zedBSD then cannot be reached.  Started at boot (/etc/service.d/nicwatch): when ue0 has no IPv4
# address within LIMIT seconds, zedBSD reboots (UEFI's BootOrder brings Debian back, whose measure.sh boots zedBSD again
# with BootNext); at most MOST times in a row, counted in COUNT, which a boot with an address sets back to 0.  Without the
# link the power guard (guard.sh) cannot reach zedBSD either, and the GPU carries no test load before measure.sh starts one.
# Installed by measure.sh nicwatch (the 2026-10-28 user decision: test images and the machine's settings only).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
LIMIT=${NICWATCH_LIMIT:-150}
MOST=${NICWATCH_MOST:-3}
COUNT=/boot/boot0/nicwatch.count
waited=0
while [ "$waited" -lt "$LIMIT" ]; do
	if ifconfig ue0 2>/dev/null | grep -q 'inet [0-9]'; then
		echo 0 > "$COUNT"
		echo "nicwatch: ue0 has an address after ${waited}s"
		exit 0
	fi
	sleep 5
	waited=$((waited + 5))
done
count=$(cat "$COUNT" 2>/dev/null || echo 0)
case "$count" in
''|*[!0-9]*) count=0 ;;
esac
if [ "$count" -ge "$MOST" ]; then
	echo "nicwatch: ue0 has no address after ${LIMIT}s; $count reboots in a row already, staying up"
	exit 0
fi
echo $((count + 1)) > "$COUNT"
sync
echo "nicwatch: ue0 has no address after ${LIMIT}s; reboot $((count + 1)) of $MOST"
reboot

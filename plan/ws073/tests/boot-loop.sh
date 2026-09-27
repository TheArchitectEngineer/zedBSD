#!/bin/sh
# BUG-030 / BUG-041 reproduction, from the host: boots a guest image N times
# and, over SSH after each boot, looks in the kernel's message buffer
# (dmesg) for I/O timeouts and runs one command (e.g. a mount of a second
# disk).  Prints one line per boot and a summary.
#
#   sh plan/ws073/tests/boot-loop.sh IMAGE N DISK [COMMAND]
#
# DISK is usb (the boot disk on xHCI usb-storage, BUG-030) or nvme.
# EXTRA, when set, is passed to guest.py as --qemu-extra (e.g. a second
# NVMe for BUG-041).  COMMAND runs after each boot; its failure is counted.
# GUEST_RUNTIME defaults to build/ws073-loop.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
image=${1:?image}
count=${2:?count}
disk=${3:?usb or nvme}
command=${4:-true}
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
GUEST_RUNTIME=${GUEST_RUNTIME:-$root/build/ws073-loop}
export GUEST_RUNTIME
guest="python3 $root/plan/tools/guest/guest.py"
bad_boot=0
bad_log=0
bad_command=0
i=1
while [ "$i" -le "$count" ]; do
	$guest stop > /dev/null 2>&1
	if [ -n "${EXTRA:-}" ]; then
		$guest start --disk "$disk" "$image" --qemu-extra "$EXTRA" > /dev/null 2>&1
	else
		$guest start --disk "$disk" "$image" > /dev/null 2>&1
	fi
	if ! $guest wait > /dev/null 2>&1; then
		echo "boot $i: no SSH"
		bad_boot=$((bad_boot + 1))
		i=$((i + 1))
		continue
	fi
	log=$($guest run 'dmesg | grep -E "error=42|error=5 |timed out|ETIMEDOUT|CSW error|BOT data" ; true' 2>&1)
	if [ -n "$log" ]; then
		bad_log=$((bad_log + 1))
	fi
	if $guest run "$command" > /dev/null 2>&1; then
		result=ok
	else
		result=FAILED
		bad_command=$((bad_command + 1))
	fi
	echo "boot $i: command $result; timeouts in dmesg: $(echo "$log" | grep -c . )"
	if [ -n "$log" ]; then
		echo "$log" | sed 's/^/    /'
	fi
	i=$((i + 1))
done
$guest stop > /dev/null 2>&1
echo "boots $count, no SSH $bad_boot, with timeouts $bad_log, command failures $bad_command"

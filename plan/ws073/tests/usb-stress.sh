#!/bin/sh
# ws073-p040 (BUG-030): boots a guest from the xHCI usb-storage disk N times (KVM, or TCG with MODE=tcg, as
# boot-test.sh runs) and, over SSH after each boot, reads the whole USB disk raw a number of times (many READ(10)
# BOT commands), then looks in dmesg for the usb-storage I/O timeouts (BOT CBW/data/CSW error=42) and the list of
# timeouts.  One line per boot and a summary.  Nothing reads the console.
#
#   sh plan/ws073/tests/usb-stress.sh IMAGE N [PASSES]      (GUEST_RUNTIME per parallel run; MODE=kvm|tcg)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
image=${1:?image}
count=${2:?count}
passes=${3:-3}
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
GUEST_RUNTIME=${GUEST_RUNTIME:-$root/build/ws073-p040/run}
export GUEST_RUNTIME
guest="python3 $root/plan/tools/guest/guest.py"
extra=
[ "${MODE:-kvm}" = tcg ] && extra=--no-kvm
bad=0
i=1
while [ "$i" -le "$count" ]; do
	$guest stop > /dev/null 2>&1
	$guest start --disk usb $extra "$image" > /dev/null 2>&1
	if ! timeout 1500 $guest wait --timeout 1200 > /dev/null 2>&1; then
		echo "boot $i: no SSH"
		bad=$((bad + 1))
		i=$((i + 1))
		continue
	fi
	boot=$(timeout 120 $guest run 'dmesg | grep -cE "BOT (CBW|CSW|data).*error=|op=28 .*error="' 2>&1 | tail -1)
	timeout 3000 $guest run "p=0; while [ \$p -lt $passes ]; do dd if=/dev/sda of=/dev/null bs=65536 2>&1 | tail -1; p=\$((p+1)); done" > "$GUEST_RUNTIME/dd-$i.txt" 2>&1
	log=$(timeout 120 $guest run 'dmesg | grep -E "BOT (CBW|CSW|data).*error=|op=28 .*error=|usb-storage.*(reset|timed)" ; true' 2>&1)
	n=$(printf '%s\n' "$log" | grep -c 'error=')
	[ "$n" -gt 0 ] && bad=$((bad + 1))
	echo "boot $i: boot-time errors ${boot:-?}, after the reads $n error lines; dd: $(tail -1 "$GUEST_RUNTIME/dd-$i.txt")"
	[ "$n" -gt 0 ] && printf '%s\n' "$log" | head -8 | sed 's/^/    /'
	i=$((i + 1))
done
$guest stop > /dev/null 2>&1
echo "boots $count with usb-storage errors or no SSH: $bad"

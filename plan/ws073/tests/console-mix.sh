#!/bin/sh
# BUG-031 check, from the host: boots a guest image N times from USB (the
# USB and storage probes then log from several CPUs at once), photographs
# the screen after each boot and compares its kernel lines with the
# kernel's message buffer (dmesg over SSH).  A line of the screen that
# starts like a kernel line but is in no dmesg record (cut into the
# screen's 80 columns) was mixed with another record.  Prints the suspect
# lines of each boot and a summary.
#
#   sh plan/ws073/tests/console-mix.sh IMAGE N [OUT]
#
# OUT (default build/ws073-console-mix) keeps the pictures and texts.
# GUEST_RUNTIME defaults to build/ws073-mix.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
image=${1:?image}
count=${2:?count}
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=${3:-$root/build/ws073-console-mix}
GUEST_RUNTIME=${GUEST_RUNTIME:-$root/build/ws073-mix}
export GUEST_RUNTIME
guest="python3 $root/plan/tools/guest/guest.py"
mkdir -p "$out"
mixed_boots=0
i=1
while [ "$i" -le "$count" ]; do
	$guest stop > /dev/null 2>&1
	$guest start --disk usb "$image" > /dev/null 2>&1
	if ! $guest wait > /dev/null 2>&1; then
		echo "boot $i: no SSH"
		i=$((i + 1))
		continue
	fi
	$guest screenshot "$out/screen-$i.png" > "$out/screen-$i.log" 2>&1
	$guest run 'dmesg' > "$out/dmesg-$i.txt" 2>&1
	suspects=$(python3 - "$out/screen-$i.txt" "$out/dmesg-$i.txt" <<'EOF'
import re, sys
from pathlib import Path
screen_path, dmesg_path = sys.argv[1], sys.argv[2]
if not Path(screen_path).exists():
	print("(no screen text)")
	raise SystemExit
chunks = set()
for line in Path(dmesg_path).read_text(errors="replace").splitlines():
	line = line.rstrip()
	for at in range(0, max(len(line), 1), 80):
		chunks.add(line[at:at + 80].rstrip())
kernel = re.compile(r"^(vfs|usb[-a-z0-9]*|input|nvme|xhci|ehci|uhci|pci|loop[0-9]|swap|boot|acpi|hda|net|cdc[-a-z]*|sd[a-z]|disk|fat|ufs|tty|smp|cpu[0-9]*|kernel|graphics|i915|venus|rtl[0-9a-z]*|wifi|ax211): ")
for line in Path(screen_path).read_text(errors="replace").splitlines():
	line = line.rstrip()
	if not kernel.match(line):
		continue
	if line in chunks:
		continue
	if any(chunk.startswith(line) for chunk in chunks):
		continue
	print(line)
EOF
)
	if [ -n "$suspects" ]; then
		mixed_boots=$((mixed_boots + 1))
		echo "boot $i: suspect lines"
		echo "$suspects" | sed 's/^/    /'
	else
		echo "boot $i: screen lines match dmesg"
	fi
	i=$((i + 1))
done
$guest stop > /dev/null 2>&1
echo "boots $count, boots with suspect lines $mixed_boots"

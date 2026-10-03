#!/bin/sh
# BUG-051: boots the image BOOTS times and opens SESSIONS short SSH sessions after each boot, reporting failed
# sessions and processes the kernel killed with a fault signal (dmesg).  The guest is left running when a fault
# is seen, so it can be examined (KEEP=1 keeps it always).
#   sh plan/ws073/tests/bug051-repro.sh IMAGE [BOOTS] [SESSIONS]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
image=$1
boots=${2:-5}
sessions=${3:-30}
out=build/ws073-p030/repro
mkdir -p "$out"
g() { timeout 120 sh plan/ws073/tests/g.sh "$@" </dev/null; }
b=1
while [ "$b" -le "$boots" ]; do
	g stop > /dev/null 2>&1
	timeout 300 sh plan/ws073/tests/g.sh start "$image" </dev/null > "$out/start-$b.txt" 2>&1
	timeout 400 sh plan/ws073/tests/g.sh wait </dev/null >> "$out/start-$b.txt" 2>&1
	n=1
	failed=0
	while [ "$n" -le "$sessions" ]; do
		if ! g run 'uname -n > /dev/null' > "$out/s-$b-$n.txt" 2>&1; then
			failed=$((failed + 1))
			echo "boot $b session $n failed: $(tr '\n' ' ' < "$out/s-$b-$n.txt")"
		fi
		n=$((n + 1))
	done
	g run 'dmesg | grep -E "killed by signal" ; true' > "$out/dmesg-$b.txt" 2>&1
	killed=$(grep -c "killed by signal" "$out/dmesg-$b.txt")
	echo "boot $b: sessions $sessions, failed $failed, killed $killed"
	cat "$out/dmesg-$b.txt"
	if [ "$killed" != 0 ] || [ "${KEEP:-0}" = 1 ]; then
		echo "guest left running"
		exit 1
	fi
	b=$((b + 1))
done
g stop > /dev/null 2>&1
exit 0

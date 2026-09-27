#!/bin/sh
# BUG-082 stress, run in the guest as root: sync while short processes exit.
# sync revokes every mapped page of the files it writes back; a process that
# has just exited still has its mappings on those pages while its address
# space is torn down.  JOBS loops each run ROUNDS short programs (each maps
# its image and the C library from the root) while one loop runs sync.  A
# kernel that cannot wait out the dying space stops with "invalid shared VM
# reverse mapping", and this script never reports.
#
#   sh sync-exit.sh [JOBS [ROUNDS]]
#
# Prints "PASS sync-exit JOBS x ROUNDS" when every loop finished.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
jobs=${1:-4}
rounds=${2:-500}
work=/var/tmp/sync-exit
rm -rf "$work"
mkdir -p "$work"

# Short programs, each writing a little so that sync has pages to write.
j=0
while [ $j -lt "$jobs" ]; do
	(
		i=0
		while [ $i -lt "$rounds" ]; do
			echo $i > "$work/f$j"
			cat "$work/f$j" >/dev/null
			i=$((i + 1))
		done
		touch "$work/done$j"
	) &
	j=$((j + 1))
done

# sync until every loop is done.
while [ "$(ls "$work" | grep -c '^done')" -lt "$jobs" ]; do
	sync
done
wait
rm -rf "$work"
echo "PASS sync-exit $jobs x $rounds"

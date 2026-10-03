#!/bin/sh
# BUG-074 check, run in the guest as root: on a FAT directory, entries
# removed while the directory is read do not hide the ones after them, so
# rm -r empties it; a listing still returns every entry once, short and
# long names alike, across several clusters.
#
#   sh fat-rmr.sh DIR
#
# DIR is a directory on a mounted FAT volume (e.g. /boot/esp).  Prints one
# line per check and exits with the number of failures.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
top=${1:?directory on FAT}
failures=0

# check NAME COMMAND...: passes when COMMAND succeeds.
check() {
	name=$1
	shift
	if "$@" >/dev/null 2>&1; then
		echo "PASS $name"
	else
		echo "FAIL $name"
		failures=$((failures + 1))
	fi
}

# mkfiles DIR COUNT: COUNT files, half with short names and half with long ones.
mkfiles() {
	mkdir -p "$1"
	i=0
	while [ $i -lt "$2" ]; do
		echo $i > "$1/f$i"
		echo $i > "$1/a rather long file name number $i.txt"
		i=$((i + 2))
	done
}

rm -rf "$top/rmr"
mkfiles "$top/rmr/six" 6
check "rm -r a directory of 6" rm -r "$top/rmr/six"
check "it is gone" test ! -e "$top/rmr/six"

mkfiles "$top/rmr/many" 300
check "a listing returns all 300 once" sh -c "test \$(ls '$top/rmr/many' | wc -l) -eq 300 && test \$(ls '$top/rmr/many' | sort -u | wc -l) -eq 300"
check "find returns all 300" sh -c "test \$(find '$top/rmr/many' -type f | wc -l) -eq 300"
mkdir -p "$top/rmr/many/sub/deeper"
echo x > "$top/rmr/many/sub/deeper/x"
check "rm -r a directory of 300 and a subtree" rm -r "$top/rmr/many"
check "it is gone" test ! -e "$top/rmr/many"

# Removing every other entry by hand, then listing, returns the rest.
mkfiles "$top/rmr/half" 40
i=0
while [ $i -lt 40 ]; do
	rm "$top/rmr/half/f$i"
	i=$((i + 4))
done
check "a listing after removals returns the 30 left" sh -c "test \$(ls '$top/rmr/half' | wc -l) -eq 30"
check "rm -r the rest" rm -r "$top/rmr"
check "sync" sync

echo "failures $failures"
exit "$failures"

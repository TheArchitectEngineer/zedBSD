#!/bin/sh
# BUG-029 regression, run in the guest as root: /tmp takes many thousands of
# files, the rest of the system keeps opening files while it does, and the
# files can be removed and made again without losing capacity.  COUNT is the
# number of empty files per round (default 10000).  Prints one line per check
# and exits with the number of failures.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
COUNT=${COUNT:-10000}
dir=/tmp/bug029
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

# make_files N: makes N empty files in $dir, stopping at the first failure;
# prints how many it made.
make_files() {
	i=0
	while [ "$i" -lt "$1" ]; do
		: > "$dir/f$i" 2>/dev/null || break
		i=$((i + 1))
	done
	echo "$i"
}

# The rest of the system still opens files: a device, a file on the root,
# and a pipe.
others_work() {
	: < /dev/null || return 1
	cat /etc/passwd > /dev/null || return 1
	echo pipe | cat > /dev/null || return 1
	return 0
}

for round in 1 2 3; do
	rm -rf "$dir"
	mkdir -p "$dir"
	made=$(make_files "$COUNT")
	check "round $round: $COUNT empty files (made $made)" test "$made" -eq "$COUNT"
	check "round $round: other files still open" others_work
	check "round $round: ls sees them all" test "$(ls "$dir" | wc -l | tr -d ' ')" -eq "$made"
	check "round $round: remove them" rm -rf "$dir"
done

# Files with data stop at the byte quota, with the rest of the system intact.
mkdir -p "$dir"
i=0
while [ "$i" -lt 20000 ]; do
	echo x > "$dir/d$i" 2>/dev/null || break
	i=$((i + 1))
done
echo "files with one byte each before the quota: $i"
check "the byte quota, not the inodes, stopped it" sh -c "df /tmp | tail -1 | grep -q '100%'"
check "other files still open at the quota" others_work
check "remove the data files" rm -rf "$dir"
check "a new file after removal" sh -c "echo y > /tmp/bug029-after && rm /tmp/bug029-after"

echo "failures $failures"
exit "$failures"

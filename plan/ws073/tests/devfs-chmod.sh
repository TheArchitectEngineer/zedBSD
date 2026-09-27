#!/bin/sh
# BUG-067 regression, run in the guest as root: a character device node in
# devfs takes chmod and chown, the change shows on later lookups and on a
# descriptor opened before it, and mesg works on the console.  MESG names
# the mesg under test (default: the installed one).  The original modes are
# put back at the end.  Prints one line per check and exits with the number
# of failures.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
MESG=${MESG:-mesg}
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

# mode PATH: prints the permission string of PATH.
mode() {
	ls -l "$1" | cut -c1-10
}

# owner PATH: prints the owner and group of PATH.
owner() {
	stat "$1" | awk '/Uid:/ { print $4 ":" $6 }'
}

console_mode=$(mode /dev/console)
null_mode=$(mode /dev/null)

# chmod on the console, seen by a new lookup.
check "chmod g-w /dev/console" chmod g-w /dev/console
check "console lost group write" test "$(mode /dev/console)" = "crw-r--rw-"
check "chmod 620 /dev/console" chmod 620 /dev/console
check "console is 620" test "$(mode /dev/console)" = "crw--w----"

# A descriptor opened before a change sees the change in fstat.
exec 9</dev/null
check "chmod 600 /dev/null" chmod 600 /dev/null
check "fd opened earlier shows 600" test "$(ls -lL /dev/fd/9 | cut -c1-10)" = "crw-------"
exec 9<&-
check "chmod 666 /dev/null" chmod 666 /dev/null
check "null is 666" test "$(mode /dev/null)" = "crw-rw-rw-"

# chown keeps the mode; chmod keeps the owner.
check "chown 5:6 /dev/null" chown 5:6 /dev/null
check "null owner 5:6" test "$(owner /dev/null)" = "5:6"
check "chown kept the mode" test "$(mode /dev/null)" = "crw-rw-rw-"
check "chmod 644" chmod 644 /dev/null
check "chmod kept the owner" test "$(owner /dev/null)" = "5:6"
check "chown back to 0:0" chown 0:0 /dev/null

# mesg on the console.
"$MESG" n </dev/console
status=$?
check "mesg n reports 1" test "$status" = 1
check "mesg says is n" test "$("$MESG" </dev/console)" = "is n"
check "no group or other write" test "$(mode /dev/console | cut -c6,9)" = "--"
"$MESG" y </dev/console
status=$?
check "mesg y reports 0" test "$status" = 0
check "mesg says is y" test "$("$MESG" </dev/console)" = "is y"
check "group write is back" test "$(mode /dev/console | cut -c6)" = "w"

# Puts the original modes back.
chmod "$(echo "$console_mode" | cut -c2-10 | awk '{
	m = 0; for (i = 0; i < 3; i++) { d = 0;
	if (substr($0, i * 3 + 1, 1) == "r") d += 4;
	if (substr($0, i * 3 + 2, 1) == "w") d += 2;
	if (substr($0, i * 3 + 3, 1) == "x") d += 1;
	m = m * 10 + d } printf "%d", m }')" /dev/console
chmod 666 /dev/null
check "console mode restored" test "$(mode /dev/console)" = "$console_mode"
check "null mode restored" test "$(mode /dev/null)" = "$null_mode"

echo "failures $failures"
exit "$failures"

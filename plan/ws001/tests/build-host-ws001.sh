#!/bin/sh
# ws001: builds the userland/base utilities that WS001 works on for the host
# (Linux), next to the ones plan/tools/utils/build-host-utils.sh builds, so
# that plan/tools/utils/util-diff.py compares them with GNU in POSIX mode.
#   sh plan/ws001/tests/build-host-ws001.sh [OUTPUT_DIR] [UTILITY...]
#   (default output build/ws001/bin; default utilities: the list below)
set -e
out=${1:-build/ws001/bin}
if [ $# -gt 0 ]; then
	shift
fi
list="$*"
if [ -z "$list" ]; then
	list="env xargs cp mv id chown chgrp date time expand unexpand fold nl split csplit pr comm diff patch uname sleep mkdir mkfifo rmdir pathchk kill df du who tty logname strings file link unlink cksum cat tee cmp dd mesg nohup pwd"
fi
mkdir -p "$out"
failed=
for utility in $list; do
	if [ ! -d "userland/base/$utility" ]; then
		echo "missing: $utility" >&2
		failed="$failed $utility"
		continue
	fi
	# mv shares cp's copy of file hierarchies.
	extra=
	if [ "$utility" = mv ]; then
		extra=userland/base/cp/copy.c
	fi
	if ! cc -std=c11 -D_GNU_SOURCE -O1 -g -w -I. -Iinclude \
		userland/base/$utility/*.c $extra userland/base/common/command.c \
		-o "$out/$utility" -lm; then
		failed="$failed $utility"
	fi
done
if [ -n "$failed" ]; then
	echo "not built:$failed" >&2
fi
echo "$out"

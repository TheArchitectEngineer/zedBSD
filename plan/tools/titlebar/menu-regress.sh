#!/bin/sh
# ws070: runs WS035's zdesktop tests on the System Menu's guest (plan/tools/titlebar/menu-guest.sh, the lean
# image) and prints each one's last line.  A test WS070 had to adapt (plan/tools/titlebar/zdesktop-TEST-menu.sh)
# runs in place of WS035's.  The screens go to OUTDIR/<test>/, the logs to OUTDIR/<test>.log.
#
#   plan/tools/titlebar/menu-regress.sh OUTDIR TEST...     (TEST: p059 p064 p068 ...)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
GUEST_RUNTIME="${GUEST_RUNTIME:-$(pwd)/build/ws070-run}"
export GUEST_RUNTIME
out=$1
shift
mkdir -p "$out"
status=0
for test in "$@"; do
	script=plan/ws035/tests/zdesktop-$test.sh
	[ -x plan/tools/titlebar/zdesktop-$test-menu.sh ] && script=plan/tools/titlebar/zdesktop-$test-menu.sh
	timeout 900 sh "$script" "$out/$test" > "$out/$test.log" 2>&1
	result=$?
	[ $result -eq 0 ] || status=1
	echo "$test: exit=$result $(tail -1 "$out/$test.log")"
done
exit $status

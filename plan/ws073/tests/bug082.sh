#!/bin/sh
# BUG-082 on amd64 native (UEFI, NVMe, KVM), from the host: sync-exit.sh RUNS
# times in the full guest with this tree's kernel.  A kernel with the race
# stops with "fatal: ... invalid shared VM reverse mapping" (the screen is
# kept as OUT/hang.png) and the next run cannot connect; a failed run leaves
# the guest running for the debugger.
#
#   sh plan/ws073/tests/bug082.sh VMUNIX [RUNS [OUT]]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
vmunix=${1:?vmunix}
runs=${2:-20}
out=${3:-build/ws073-bug082}
here=$(cd "$(dirname "$0")" && pwd)
g="sh $here/g.sh"
mkdir -p "$out"
sh "$here/kernel-image.sh" "$vmunix" "$out/native.img" >/dev/null
$g stop >/dev/null 2>&1 || true
$g start "$out/native.img" >/dev/null
$g wait >/dev/null
$g put "$here/sync-exit.sh" /tmp/sync-exit.sh >/dev/null
passed=0
n=1
while [ $n -le "$runs" ]; do
	start=$(date +%s)
	if timeout 300 $g run 'sh /tmp/sync-exit.sh 4 300' 2>&1 | grep -q '^PASS'; then
		passed=$((passed + 1))
		echo "run $n: $(($(date +%s) - start)) s"
	else
		echo "FAIL run $n after $(($(date +%s) - start)) s (the guest is left running)"
		timeout 60 $g screenshot "$out/hang.png" >/dev/null 2>&1
		echo "bug082: $passed of $runs runs passed"
		exit 1
	fi
	n=$((n + 1))
done
$g stop >/dev/null
echo "bug082: $passed of $runs runs passed"
[ "$passed" = "$runs" ]

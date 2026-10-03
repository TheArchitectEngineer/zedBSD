#!/bin/sh
# ws073-p047 (BUG-052), run in the guest as root: the /tmp tmpfs is half of
# memory (not 32 MiB), a large file is written, read back, truncated and
# removed quickly and intact, a sparse file reads zeros in its holes, and the
# space comes back.  Needs a guest with at least 2 GiB of memory (the guest
# tools boot 8 GiB).  Prints one line per check and exits with the number of
# failures.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
failures=0
MIB=${MIB:-512}

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

# The used KiB of /tmp.
used_kib() {
	df -k /tmp | tail -1 | awk '{print $3}'
}

rm -rf /tmp/bug052
mkdir -p /tmp/bug052
size_kib=$(df -k /tmp | tail -1 | awk '{print $2}')
echo "tmpfs size ${size_kib} KiB"
check "the quota is at least 1 GiB (was 32 MiB)" test "$size_kib" -ge 1048576
before=$(used_kib)

# A 64 MiB random block on the root file system, written MIB/64 times into /tmp.
dd if=/dev/urandom of=/root/bug052-block bs=1048576 count=64 2>/dev/null
copies=$((MIB / 64))
start=$(date +%s)
i=0
: > /tmp/bug052/big
while [ "$i" -lt "$copies" ]; do
	cat /root/bug052-block >> /tmp/bug052/big || break
	i=$((i + 1))
done
end=$(date +%s)
echo "wrote ${MIB} MiB in $((end - start)) s"
check "the ${MIB} MiB file was written" test "$i" -eq "$copies"
check "its size is ${MIB} MiB" test "$(wc -c < /tmp/bug052/big | tr -d ' ')" -eq $((MIB * 1048576))
want=$(i=0; while [ "$i" -lt "$copies" ]; do cat /root/bug052-block; i=$((i + 1)); done | cksum)
got=$(cksum < /tmp/bug052/big)
check "it reads back intact" test "$want" = "$got"
check "stat counts its blocks" test "$(du -k /tmp/bug052/big | awk '{print $1}')" -ge $((MIB * 1024))

# Truncation inside the file keeps the prefix.
truncate -s 100000000 /tmp/bug052/big
want=$(head -c 100000000 /tmp/bug052/big | cksum)
got=$(i=0; while [ "$i" -lt "$copies" ]; do cat /root/bug052-block; i=$((i + 1)); done | head -c 100000000 | cksum)
check "truncation to 100000000 keeps the prefix" test "$want" = "$got"
check "truncation sets the size" test "$(wc -c < /tmp/bug052/big | tr -d ' ')" -eq 100000000

# Removal is quick and gives the space back.
start=$(date +%s)
rm /tmp/bug052/big
end=$(date +%s)
echo "removed in $((end - start)) s"
check "removal takes under 5 s" test $((end - start)) -lt 5
after=$(used_kib)
echo "used KiB before ${before}, after ${after}"
check "the space came back" test "$after" -le $((before + 1024))

# A sparse file: holes read as zeros, only written pages count.
truncate -s 1073741824 /tmp/bug052/sparse
printf 'zedbsd-bug052' | dd of=/tmp/bug052/sparse bs=1 seek=943718400 conv=notrunc 2>/dev/null
check "the sparse file is 1 GiB" test "$(wc -c < /tmp/bug052/sparse | tr -d ' ')" -eq 1073741824
check "the written bytes read back" test "$(dd if=/tmp/bug052/sparse bs=1 skip=943718400 count=13 2>/dev/null)" = "zedbsd-bug052"
check "a hole reads zeros" test "$(dd if=/tmp/bug052/sparse bs=4096 skip=1000 count=1 2>/dev/null | cksum)" = "$(dd if=/dev/zero bs=4096 count=1 2>/dev/null | cksum)"
check "the sparse file holds few blocks" test "$(du -k /tmp/bug052/sparse | awk '{print $1}')" -le 64
check "remove the sparse file" rm /tmp/bug052/sparse

rm -rf /tmp/bug052 /root/bug052-block
echo "failures $failures"
exit "$failures"

#!/bin/sh
# WS049 (ws049-p008, BUG-165): loads the Dell Latitude 5330's DSDT and SSDTs
# (plan/ws049/tests/latitude5330/) in the host harness.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
#   make -C plan/ws049/tests && plan/ws049/tests/check-latitude5330.sh
#
# The functions 0:10.6 and 0:10.7 (THC0, THC1) are absent, as on the
# machine (the BIOS disables them).  The checks:
#   1. the old kernel behaviour (--absent-pci-fails) stops the DSDT at
#      offset 0x1c89b, the offset of the 2026-10-04 UAT's dmesg;
#   2. the current behaviour (all ones, writes dropped) loads every table
#      with no "stopped" line;
#   3. with the EC at the _CRS ports 0x930/0x934, _REG and _INI run and
#      \_S5_, the battery's _BIF and _BST, the AC's _PSR and the lid's _LID
#      evaluate;
#   4. every method that takes no arguments runs, and the only failure is
#      \_SB_.PTID.TSDD (ENOENT: it reads \_TZ.TZ00._TMP, which the tables do
#      not define).
# The simulated memory reads zero, so the values are not the machine's;
# only whether the interpreter gets through is checked.  Exits 0 on success.
set -eu
repo=$(cd "$(dirname "$0")/../../.." && pwd)
host="$repo/build/ws049/host/aml-host"
dir="$repo/plan/ws049/tests/latitude5330"
out="$repo/build/ws049/latitude5330"
mkdir -p "$out"
tables="$dir/dsdt.dat"
for number in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do
	tables="$tables $dir/ssdt$number.dat"
done
absent="--absent-pci 0:10.6 --absent-pci 0:10.7"
failed=0

# 1. The old behaviour reproduces the UAT's stop.
"$host" $absent --absent-pci-fails $dir/dsdt.dat > "$out/old.txt" 2>&1 || true
if grep -q "DSDT Dell Inc stopped at offset 0x1c89b" "$out/old.txt"; then
	echo "old: stops at 0x1c89b, as on the machine"
else
	echo "old: FAILED, see $out/old.txt"
	failed=1
fi

# 2. Every table loads.
if "$host" $absent $tables > "$out/load.txt" 2>&1 &&
    ! grep -q "stopped at" "$out/load.txt"; then
	echo "load: every table loaded"
else
	echo "load: FAILED, see $out/load.txt"
	failed=1
fi

# 3. _REG, _INI and the power objects.
if "$host" $absent --events --ec --ec-ports 930,934 --reg --init \
    --eval '\_S5_' --eval '\_SB_.BAT0._BIF' --eval '\_SB_.BAT0._BST' \
    --eval '\_SB_.AC__._PSR' --eval '\_SB_.LID0._LID' \
    $tables > "$out/power.txt" 2>&1 &&
    ! grep -q "= error\|failed\|stopped at" "$out/power.txt" &&
    grep -q '^\\_S5_ = Package \[4\] { Integer 0x7,' "$out/power.txt"; then
	echo "power: _REG, _INI, _S5_, _BIF, _BST, _PSR, _LID evaluated"
else
	echo "power: FAILED, see $out/power.txt"
	failed=1
fi

# 4. Every method that takes no arguments.
"$host" --quiet $absent --events --ec --ec-ports 930,934 --reg --init \
    --methods $tables > "$out/methods.txt" 2>&1 || true
errors=$(grep "= error" "$out/methods.txt" | grep -v '^\\_SB_\.PTID\.TSDD = error 2$' | wc -l)
count=$(grep -c " = " "$out/methods.txt" || true)
if [ "$errors" -eq 0 ] && [ "$count" -gt 1000 ]; then
	echo "methods: $count ran, only the known TSDD failure"
else
	echo "methods: FAILED ($errors unexpected errors, $count ran), see $out/methods.txt"
	failed=1
fi

exit $failed

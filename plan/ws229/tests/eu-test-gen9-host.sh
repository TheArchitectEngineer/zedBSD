#!/bin/sh
# ws229-p005: the compute test's fixture (src/drivers/gpu/i915/tests/execution/eu-test.c) for Gen9, on the host.
#
# 1. eu-test-dump.c (eu-test.c compiled whole on the host) builds the batch and the interface descriptor for Gen12 and
#    for Gen9 and checks each batch's PIPELINE_SELECT words with the test's own check.
# 2. The Gen12 batch is byte for byte the one the test submitted before Gen9 (main bb57b2397, SHA-256 below).
# 3. The Gen9 store kernel equals Mesa's assembly of the reviewed listing eu-test-gen9.asm (brw_asm --gen=skl), and the
#    listing survives the brw_disasm -> brw_asm round trip.
# 4. plan/ws101/tests/host/genxml-check.py decodes both batches and descriptors with Mesa's genxml (gen120.xml, and
#    gen90.xml for Gen9: every dword starts or continues an instruction, no bit outside a field) and checks
#    eu-test-gen12.expect and eu-test-gen9.expect.  The gen90.xml root needs ws229-p004's genxml-check.py; until it is
#    merged, GENXML_CHECK names a copy that has it.
#
#   plan/ws229/tests/eu-test-gen9-host.sh     (BRW_TOOLS, GENXML and GENXML_CHECK as below)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
here=$repo/plan/ws229/tests
tools=${BRW_TOOLS:-$repo/build/mesa-tools/build-asm/src/intel/compiler}
genxml=${GENXML:-$repo/build/mesa-tools/mesa-25.0.7/src/intel/genxml}
check=${GENXML_CHECK:-$repo/plan/ws101/tests/host/genxml-check.py}
gen12_sha256=09786119116714c3801e3aacc940522ace93ebf6b2b2445acf89f6aa961b0fe6
# A new directory for each run under build/tmp; nothing is removed here (Q1's plan/tools/q1-clean.sh removes old runs).
. "$repo/plan/tools/fresh-out.sh"
fresh_out "$repo/build/tmp/ws229-eu-test-gen9"
work=$fresh_dir
status=0

# 1. The fixture of both versions.
cc -std=gnu11 -O1 -Wall -Wextra -Werror -DKERN_USER_ABI_LP64 -DHAL_ARCH_AMD64 -I"$repo/include" -I"$repo/src" \
	-I"$repo" -idirafter "$repo/include/libc" -ffunction-sections -fdata-sections -Wl,--gc-sections \
	-o "$work/dump" "$here/eu-test-dump.c" || exit 1
"$work/dump" "$work" || { echo "ws229 eu-test Gen9 host test FAIL (the dump)"; exit 1; }

# 2. Gen12 is unchanged.
sum=$(sha256sum "$work/eu-gen12.bin" | cut -d' ' -f1)
if [ "$sum" = "$gen12_sha256" ]; then
	echo "eu-gen12: the batch is the one before Gen9 (SHA-256 $sum)"
else
	echo "eu-gen12: FAIL the batch changed: SHA-256 $sum, expected $gen12_sha256"; status=1
fi

# 3. The Gen9 kernel.
if [ ! -x "$tools/brw_asm" ] || [ ! -x "$tools/brw_disasm" ]; then
	echo "kernel-gen9: FAIL no brw_asm/brw_disasm under $tools (plan/ws101/tests/host/mesa-tools.sh builds them)"; status=1
else
	"$tools/brw_asm" --gen=skl -o "$work/listing.bin" "$here/eu-test-gen9.asm" > "$work/listing.log" 2>&1
	if [ -s "$work/listing.bin" ] && cmp -s "$work/listing.bin" "$work/kernel-gen9.bin"; then
		echo "kernel-gen9: the table equals Mesa's assembly of eu-test-gen9.asm"
	else
		echo "kernel-gen9: FAIL the table differs from Mesa's assembly of eu-test-gen9.asm"; status=1
	fi
	"$tools/brw_disasm" --gen=skl --input-path="$work/kernel-gen9.bin" > "$work/kernel-gen9.asm" 2>&1
	"$tools/brw_asm" --gen=skl -o "$work/kernel-gen9.re" "$work/kernel-gen9.asm" > /dev/null 2>&1
	if grep -q 'ERROR\|illegal' "$work/kernel-gen9.asm" || ! cmp -s "$work/kernel-gen9.bin" "$work/kernel-gen9.re"; then
		echo "kernel-gen9: FAIL the brw_disasm -> brw_asm round trip"; status=1
	else
		echo "kernel-gen9: disassembled and re-assembled to the same bytes"
	fi
fi

# 4. The commands and the descriptors, decoded with Mesa's genxml.
cp "$here/eu-test-gen12.expect" "$work/eu-gen12.expect"
cp "$here/eu-test-gen9.expect" "$work/eu-gen9.expect"
python3 "$check" "$genxml" "$work/eu-gen12" || status=1
python3 "$check" "$genxml" "$work/eu-gen9" gen90.xml || status=1

if [ $status -ne 0 ]; then
	echo "ws229 eu-test Gen9 host test FAIL"
else
	echo "ws229 eu-test Gen9 host test PASS"
fi
exit $status

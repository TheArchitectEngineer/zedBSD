#!/bin/sh
# Host checks of the riscv64 framebuffer path (ws256-p011 (2), no emulator):
# the HAL's early-console drawing, the HAL's reading of the loader's handoff
# version 2, and the loader's list of the firmware's RAM.  Each is built
# twice, plain and with the address and undefined-behaviour sanitizers.
#   plan/ws256/tests/framebuffer/run.sh OUTDIR
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
root=$(cd -- "$(dirname -- "$0")/../../../.." && pwd)
here=$root/plan/ws256/tests/framebuffer
out=$1
mkdir -p "$out"
hal="-DHAL_ARCH_RISCV64 -I$root/include -I$root/src -I$root -I$root/src/hal/riscv64"
for flavour in plain sanitized; do
	extra=
	if [ "$flavour" = sanitized ]; then
		extra="-fsanitize=address,undefined -fno-sanitize-recover=all"
	fi
	# shellcheck disable=SC2086
	cc -std=c11 -O1 -g -Wall -Wextra -Werror $extra $hal \
		"$root/src/hal/riscv64/fbcons.c" "$root/src/hal/riscv64/font.c" \
		"$here/fbcons-host-test.c" -o "$out/fbcons-$flavour"
	# shellcheck disable=SC2086
	cc -std=c11 -O1 -g -Wall -Wextra -Werror $extra $hal \
		"$root/src/hal/riscv64/loader.c" \
		"$here/loader-take-host-test.c" -o "$out/loader-take-$flavour"
	# shellcheck disable=SC2086
	cc -std=c11 -O1 -g -Wall -Wextra -Werror $extra -I"$root" -I"$root/bootloader/uefi" \
		"$root/bootloader/uefi/riscv64-reserved.c" \
		"$here/reserved-host-test.c" -o "$out/reserved-$flavour"
	for test in fbcons loader-take reserved; do
		echo "== $test ($flavour)"
		"$out/$test-$flavour"
	done
done

#!/bin/sh
# ws214-p003a: host tests of the ungated display path and the bcm2711 lists made from the recorded state.
#   sh plan/ws214/tests/display-p003a-host.sh [output-directory]
# Runs, ordinary and with ASan/UBSan: the GPU core's ungated display operations (host-display-ungated.c),
# the bcm2711 flip, device and executor models (plan/ws141/tests/display-*-host-test.c), and the GPU core's
# existing display fixtures (ws141 cursor, ws113 display control, ws014 scanout and framework).
# Builds into the output directory only and deletes nothing.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
out=${1:-$repo/build/ws214-p003a-host}
mkdir -p "$out"
core="$repo/plan/ws014/tests/gpu-test-fd.c $repo/src/drivers/gpu/gpu.c $repo/src/kern/cdev.c \
    $repo/src/drivers/gpu/gpu-fence.c $repo/src/kern/handle.c $repo/src/kern/fd-object.c \
    $repo/src/kern/filedesc.c $repo/src/kern/vm-device.c $repo/src/drivers/pci/pci.c"
flip="$repo/plan/ws141/tests/display-lock-host.c $repo/plan/ws141/tests/display-wait-host.c $repo/src/drivers/gpu/bcm2711/display-flip.c $repo/src/drivers/gpu/bcm2711/display-irq.c"
for mode in ordinary sanitize; do
	extra=
	if test "$mode" = sanitize; then
		extra='-fsanitize=address,undefined -fno-omit-frame-pointer -no-pie'
	fi
	for test in plan/ws214/tests/host-display-ungated.c plan/ws141/tests/host-display-cursor.c \
	    plan/ws113/tests/host-display-control.c plan/ws014/tests/gpu-scanout.c; do
		name=$(basename "$test" .c)
		# shellcheck disable=SC2086
		cc -std=c11 -O1 -g -Wall -Wextra -Werror -ffunction-sections -fdata-sections $extra \
		    -DKERN_USER_ABI_LP64 -I"$repo/include" -I"$repo/src" -I"$repo/include/libc" -DKERN_UAPI_NATIVE \
		    "$repo/$test" $core -Wl,--gc-sections -o "$out/$name-$mode"
		ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 60 "$out/$name-$mode" > "$out/$name-$mode.log"
		tail -n 1 "$out/$name-$mode.log"
	done
	# shellcheck disable=SC2086
	cc -std=c11 -O1 -g -Wall -Wextra -Werror -ffunction-sections -fdata-sections $extra \
	    -DKERN_USER_ABI_LP64 -I"$repo/include" -I"$repo/src" -I"$repo/include/libc" -DKERN_UAPI_NATIVE \
	    -include "$repo/include/libc/sys/ioctl.h" \
	    "$repo/plan/ws014/tests/gpu-framework.c" $core -Wl,--gc-sections -o "$out/gpu-framework-$mode"
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 120 "$out/gpu-framework-$mode" > "$out/gpu-framework-$mode.log"
	tail -n 1 "$out/gpu-framework-$mode.log"
	for test in display-flip display-device; do
		extra_sources=
		if test "$test" = display-device; then
			extra_sources="$repo/src/drivers/gpu/bcm2711/buffer.c $repo/src/drivers/gpu/bcm2711/share.c $repo/src/drivers/gpu/bcm2711/display-device.c"
		fi
		# shellcheck disable=SC2086
		cc -std=c99 -D_POSIX_C_SOURCE=200809L -include time.h -O1 -g -Wall -Wextra -Werror $extra \
		    -I"$repo/include" -I"$repo/src" -I"$repo" \
		    "$repo/plan/ws141/tests/$test-host-test.c" $extra_sources $flip -o "$out/$test-$mode"
		ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 timeout 60 "$out/$test-$mode" > "$out/$test-$mode.log"
		tail -n 1 "$out/$test-$mode.log"
	done
done
echo "display-p003a-host.sh: PASS"

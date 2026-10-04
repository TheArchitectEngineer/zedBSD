#!/bin/sh
# ws113-p012: host test of the GPU core's GPU_DISPLAY_POWER and GPU_DISPLAY_REFRESH (src/drivers/gpu/gpu.c) with the
# real cdev registry and the ws014 fixture (plan/ws014/tests/gpu-framework.c, gpu-test-fd.c), ordinary and sanitized.
#   sh plan/ws113/tests/host-display-control.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT HUP INT TERM
for mode in ordinary sanitize; do
	extra=
	if test "$mode" = sanitize; then
		extra='-fsanitize=address,undefined -fno-omit-frame-pointer -no-pie'
	fi
	# shellcheck disable=SC2086
	cc -std=c11 -O1 -g -Wall -Wextra -Werror -ffunction-sections -fdata-sections $extra \
	    -DKERN_USER_ABI_LP64 -I"$repo/include" -I"$repo/src" -I"$repo/include/libc" -DKERN_UAPI_NATIVE \
	    "$repo/plan/ws113/tests/host-display-control.c" "$repo/plan/ws014/tests/gpu-test-fd.c" \
	    "$repo/src/drivers/gpu/gpu.c" "$repo/src/kern/cdev.c" \
	    "$repo/src/drivers/gpu/gpu-fence.c" "$repo/src/kern/handle.c" \
	    "$repo/src/kern/fd-object.c" "$repo/src/kern/filedesc.c" \
	    "$repo/src/kern/vm-device.c" "$repo/src/drivers/pci/pci.c" \
	    -Wl,--gc-sections -o "$work/$mode"
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 timeout 30 "$work/$mode"
done
echo "host-display-control.sh: PASS"

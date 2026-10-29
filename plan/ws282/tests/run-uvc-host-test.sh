#!/bin/sh
# Compiles the uvc driver's descriptor parser and frame assembler against a
# host fixture, once normally and once with ASan/UBSan, and runs both.
#
#   sh plan/ws282/tests/run-uvc-host-test.sh [--compile-only]
#
# The output goes to a new directory under build/tmp/ of the tree; nothing
# is removed.
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
mkdir -p "$repo/build/tmp"
work=$(mktemp -d "$repo/build/tmp/ws282-uvc.XXXXXX")
compile_only=0
if [ "${1:-}" = "--compile-only" ]; then
	compile_only=1
fi

flags="-std=c11 -Wall -Wextra -Werror -DKERN_USER_ABI_LP64 -DHAL_ARCH_AMD64
    -I$repo/include -I$repo/src -I$repo/include/libc -DKERN_UAPI_NATIVE
    -include $repo/include/libc/sys/ioctl.h"
sources="$repo/plan/ws282/tests/uvc-host-test.c $repo/src/drivers/usb/usb-uvc-desc.c $repo/src/drivers/usb/usb-uvc-payload.c"

# shellcheck disable=SC2086
cc $flags -O2 $sources -o "$work/ordinary"

# shellcheck disable=SC2086
cc $flags -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer $sources -o "$work/sanitized"
echo "uvc-host-test: built in $work"

if [ "$compile_only" -eq 1 ]; then
	exit 0
fi

"$work/ordinary"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$work/sanitized"

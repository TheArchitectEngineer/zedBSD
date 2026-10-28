#!/bin/sh
# Compiles the production camera framework against a host fixture with a
# fake source, once normally and once with ASan/UBSan, and runs both.  The
# UAPI header is also compiled for ILP32 and LP64 callers, so its
# _Static_assert layout checks hold for both.
#
#   sh plan/ws282/tests/run-cam-host-test.sh [--compile-only]
#
# The output goes to a new directory under build/tmp/ of the tree; nothing
# is removed.
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
mkdir -p "$repo/build/tmp"
work=$(mktemp -d "$repo/build/tmp/ws282-cam.XXXXXX")
compile_only=0
if [ "${1:-}" = "--compile-only" ]; then
	compile_only=1
fi

# The UAPI layout is the same for ILP32 and LP64 callers.
for abi in 32 64; do
	printf '#include <uapi/camera.h>\n' |
	    cc -std=c89 -m$abi -ffreestanding -Wall -Wextra -Werror \
	    -I"$repo/include" -I"$repo/include/libc" -DKERN_UAPI_NATIVE \
	    -x c -c - -o "$work/layout$abi.o"
done
echo "camera UAPI: ILP32/LP64 layout PASS"

flags="-std=c11 -Wall -Wextra -Werror -DKERN_USER_ABI_LP64 -DHAL_ARCH_AMD64 -DKERN_KCRT_NATIVE
    -I$repo/include -I$repo/src -I$repo/include/libc -DKERN_UAPI_NATIVE
    -include $repo/include/libc/sys/ioctl.h"

# shellcheck disable=SC2086
cc $flags -O2 "$repo/plan/ws282/tests/cam-host-test.c" \
    "$repo/src/drivers/camera/cam.c" -lpthread -o "$work/ordinary"

# shellcheck disable=SC2086
cc $flags -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    "$repo/plan/ws282/tests/cam-host-test.c" \
    "$repo/src/drivers/camera/cam.c" -lpthread -o "$work/sanitized"
echo "cam-host-test: built in $work"

if [ "$compile_only" -eq 1 ]; then
	exit 0
fi

"$work/ordinary"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$work/sanitized"

#!/bin/sh
# Compiles libyolox's sources with the host cc against the ZNET load test
# (T2b: a synthetic valid network, files broken in one place each, and the
# API's argument checks), once normally and once with ASan/UBSan, and runs
# both.  The host's Vulkan headers are used; nothing calls Vulkan.
#
#   sh plan/ws284/tests/run-znet-host-test.sh
#
# The output goes to a new directory under build/tmp/ of the tree; nothing
# is removed.
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
mkdir -p "$repo/build/tmp"
work=$(mktemp -d "$repo/build/tmp/ws284-znet.XXXXXX")
library="$repo/userland/desktop/libyolox"
sources="$repo/plan/ws284/tests/znet-host-test.c $library/model.c $library/runner.c $library/cpu.c
    $library/preprocess.c $library/decode.c $library/classes.c"
flags="-std=c11 -Wall -Wextra -Werror -I$library -D_POSIX_C_SOURCE=200809L"

# shellcheck disable=SC2086
cc $flags -O2 $sources -lm -ldl -o "$work/ordinary"
# shellcheck disable=SC2086
cc $flags -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer $sources -lm -ldl -o "$work/sanitized"

mkdir -p "$work/ordinary-files" "$work/sanitized-files"
"$work/ordinary" "$work/ordinary-files"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$work/sanitized" "$work/sanitized-files"

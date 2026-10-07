#!/bin/sh
# ws083-p003a: builds and runs the host fixture of the i915 request worker's video engine (host-vcs-worker.c),
# plainly and under ASan/UBSan, in a new directory under build/tmp (nothing is removed here; Q1's
# plan/tools/q1-clean.sh removes the runs build/tmp/ws083-vcs-worker does not point at).
#   sh plan/ws083/tests/run-host-vcs-worker.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
. "$repo/plan/tools/fresh-out.sh"
fresh_out "$repo/build/tmp/ws083-vcs-worker"
work=$fresh_dir
compiler=${CC:-cc}
base="-std=gnu11 -Wall -Wextra -Werror -Wno-unused-function -DKERN_USER_ABI_LP64 -DHAL_ARCH_AMD64 -I$repo/include -I$repo -idirafter $repo/include/libc"
"$compiler" $base -O1 -g -o "$work/plain" "$repo/plan/ws083/tests/host-vcs-worker.c"
"$work/plain"
"$compiler" $base -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -o "$work/asan" "$repo/plan/ws083/tests/host-vcs-worker.c"
"$work/asan"
echo "WS083 vcs worker host fixture PASS"

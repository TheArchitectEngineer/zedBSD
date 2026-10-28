#!/bin/sh
# ws291: builds and runs watermarks-test.c (the amdrdna watermark table against Linux's Watermarks_t) on the host.
#   usage: plan/ws291/tests/watermarks-test.sh [LINUX-TREE]   (default /home/awe/visionfive-linux, Linux 6.12)
# Output goes in a new directory below build/tmp; nothing is removed.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
repo=$(cd "$(dirname "$0")/../../.." && pwd)
linux=${1:-/home/awe/visionfive-linux}
mkdir -p "$repo/build/tmp"
out=$(mktemp -d "$repo/build/tmp/watermarks.XXXXXX")
cc -std=gnu11 -O1 -g -Wall -Wextra -Werror -DKERN_USER_ABI_LP64 -DKERN_UAPI_NATIVE -I"$repo/include" -I"$repo/src" -I"$repo" \
	-I"$linux/drivers/gpu/drm/amd/pm/swsmu/inc/pmfw_if" -include uapi/errno.h -fsanitize=address,undefined \
	-o "$out/watermarks-test" "$repo/plan/ws291/tests/watermarks-test.c" "$repo/src/drivers/gpu/amdrdna/thermal-format.c" "$repo/src/drivers/gpu/amdrdna/bytes.c" "$repo/src/drivers/gpu/amdrdna/vbios-table.c"
"$out/watermarks-test"

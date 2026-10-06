#!/bin/sh
# ws094-p003: builds files' host objects (plan/tools/files/host-build.sh) and host-desktop.c against them, and runs it.
#   sh plan/ws094/tests/host-desktop.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
sh plan/tools/files/host-build.sh >/dev/null || exit 1
out=build/ws071-host
objects=$(ls $out/obj/*.o | grep -v '/host-')
${CC:-cc} -O2 -g -Wall -Wextra -Werror -Wno-unused-parameter -D_GNU_SOURCE -I$out/include -Iuserland/desktop/files -I. \
    -o $out/host-desktop plan/ws094/tests/host-desktop.c $objects -lm || exit 1
# The work directory stays in build/tmp (2026-10-06 user: deleting is Q1's step; plan/tools/q1-clean.sh removes it).
mkdir -p build/tmp
temporary=$(mktemp -d "$(pwd)/build/tmp/host-desktop.XXXXXX")
$out/host-desktop "$temporary"
status=$?
exit $status

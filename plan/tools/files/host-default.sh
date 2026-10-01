#!/bin/sh
# ws093-p003: builds files' host objects (plan/tools/files/host-build.sh) and host-default.c against them, and runs it
# in a temporary folder (Always Open With: the user's list written by fm_apps_set_default and fm_apps_clear_default).
#   sh plan/tools/files/host-default.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
sh plan/tools/files/host-build.sh >/dev/null || exit 1
out=build/ws071-host
objects=$(ls $out/obj/*.o | grep -v '/host-')
${CC:-cc} -O2 -g -Wall -Wextra -Werror -Wno-unused-parameter -D_GNU_SOURCE -I$out/include -Iuserland/desktop/files -I. \
    -o $out/host-default plan/tools/files/host-default.c $objects -lm || exit 1
temporary=$(mktemp -d)
$out/host-default "$temporary"
status=$?
rm -rf "$temporary"
exit $status

#!/bin/sh
# ws132-p005: builds files' host objects (plan/tools/files/host-build.sh) and host-files-devices.c against them, and
# runs it in a temporary HOME (the removable devices in the sidebar and on Today).
#   sh plan/ws132/tests/run-host-files-devices.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -u
cd "$(dirname -- "$0")/../../.."
sh plan/tools/files/host-build.sh >/dev/null || exit 1
out=build/ws071-host
objects=$(ls $out/obj/*.o | grep -v '/host-')
${CC:-cc} -O1 -g -Wall -Wextra -Werror -Wno-unused-parameter -D_GNU_SOURCE -I$out/include -Iuserland/desktop/files -I. \
    -o $out/host-files-devices plan/ws132/tests/host-files-devices.c $objects -lm -ldl || exit 1
temporary=$(mktemp -d)
HOME=$temporary XDG_CONFIG_HOME=$temporary/config XDG_CACHE_HOME=$temporary/cache timeout 60 $out/host-files-devices userland/desktop/fonts/Inter.ttf
status=$?
rm -rf "$temporary"
exit $status

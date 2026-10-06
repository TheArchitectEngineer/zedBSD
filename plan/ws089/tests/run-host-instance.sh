#!/bin/sh
# ws089-p016: builds and runs the host test of libkeiland's one copy of a program (host-instance.c) against the
# Linux Keiland's libkeiland.so (make keiland-linux first; KEILAND_LINUX_BUILD names its directory, default
# build/keiland-linux), in a private runtime directory made here.  Last line: HOST-INSTANCE PASS.
#   sh plan/ws089/tests/run-host-instance.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../.."
lib=${KEILAND_LINUX_BUILD:-build/keiland-linux}/lib
# The work directory stays in build/tmp (2026-10-06 user: deleting is Q1's step; plan/tools/q1-clean.sh removes it).
mkdir -p build/tmp
work=$(mktemp -d "$(pwd)/build/tmp/run-host-instance.XXXXXX")
mkdir -m 700 "$work/run"
cc -std=gnu89 -O1 -g -Wall -Wextra -Werror -D_GNU_SOURCE -Iuserland/desktop/keiland \
    plan/ws089/tests/host-instance.c -L"$lib" -Wl,-rpath,"$(pwd)/$lib" -lkeiland -o "$work/host-instance"
LD_LIBRARY_PATH="$(pwd)/$lib" XDG_RUNTIME_DIR="$work/run" timeout 30 "$work/host-instance"

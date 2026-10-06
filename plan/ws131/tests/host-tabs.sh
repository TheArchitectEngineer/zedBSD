#!/bin/sh
# ws131-p018: builds and runs the host test of a window's declarative tabs (host-tabs.c with libkeiland's
# window-declare.c and declare.c; the titlebar's calls are stubs) on Linux.  Nothing is removed.
#   sh plan/ws131/tests/host-tabs.sh [OUTPUT]   (default build/ws131-host/host-tabs)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -e
cd "$(dirname "$0")/../../.."
out=${1:-build/ws131-host/host-tabs}
mkdir -p "$(dirname "$out")"
cc -std=c11 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Werror -Iuserland/desktop/keiland -Iuserland/desktop/libkeiland/ui -I. \
	plan/ws131/tests/host-tabs.c userland/desktop/libkeiland/ui/window-declare.c userland/desktop/libkeiland/ui/declare.c -o "$out"
"$out"

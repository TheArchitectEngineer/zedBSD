#!/bin/sh
# ws131-p015: builds and runs the host test of the declarative menus' and controls' model (host-declare.c with
# userland/desktop/libkeiland/ui/declare.c) on Linux.
#   sh plan/ws131/tests/host-declare.sh [OUTPUT]   (default build/ws131-host/host-declare)
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -e
cd "$(dirname "$0")/../../.."
out=${1:-build/ws131-host/host-declare}
mkdir -p "$(dirname "$out")"
cc -std=c11 -D_GNU_SOURCE -O1 -g -Wall -Wextra -Werror -Iuserland/desktop/include -Iuserland/desktop/libkeiland/ui -I. \
	plan/ws131/tests/host-declare.c userland/desktop/libkeiland/ui/declare.c -o "$out"
"$out"

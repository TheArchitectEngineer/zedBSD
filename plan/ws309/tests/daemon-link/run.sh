#!/bin/sh
# Builds and runs the host test of the daemons' connections on threads of their own (libkeiland-backend-zedbsd's
# daemon-link-zedbsd.c with volume-zedbsd.c and network-zedbsd.c, their sockets in a new folder) under ASan+UBSan and TSan.
# The last line of each run is "host-daemon-link: PASS" or "... FAIL".
#   sh plan/ws309/tests/daemon-link/run.sh [build-dir]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
cd "$(dirname -- "$0")/../../../.."
out=${1:-build/ws309/host-daemon-link}
mkdir -p "$out"
for sanitizer in address,undefined thread; do
	run=$(mktemp -d "$out/run.XXXXXX")
	${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -Wno-format-truncation -D_GNU_SOURCE -Iinclude -I. -fsanitize=$sanitizer -g -pthread \
	    "-DVOLUME_SOCKET_PATH=\"$run/volumed.sock\"" "-DNETWORKD_SOCKET=\"$run/networkd.sock\"" \
	    userland/desktop/libkeiland-backend-zedbsd/daemon-link-zedbsd.c \
	    userland/desktop/libkeiland-backend-zedbsd/volume-zedbsd.c \
	    userland/desktop/libkeiland-backend-zedbsd/network-zedbsd.c \
	    userland/base/net/protocol.c \
	    plan/ws309/tests/daemon-link/host-daemon-link.c -o "$out/host-daemon-link-${sanitizer%%,*}"
	echo "== sanitizer $sanitizer"
	timeout 120 "$out/host-daemon-link-${sanitizer%%,*}" "$run"
done

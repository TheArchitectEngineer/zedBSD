#!/bin/sh
# ws209-p002: audiod's endpoints and routes on the host.  Builds audiod for the host (-DAUDIOD_HOST_TEST: its socket and
# the Bluetooth daemon's and the seat's uids from the environment, compiled in only for this test) and the test client,
# in a new folder of the build (old folders are Q1's to remove); runs the scenario with this process's uid as the
# daemon's and the seat's, then the refusals with other uids.  The last line is "audiod-route: all PASS".
# usage: sh plan/ws209/tests/run-audiod-route-test.sh
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
mkdir -p "$repo/build/tmp"
work=$(mktemp -d "$repo/build/tmp/ws209-route.XXXXXX")
cd "$repo"
cc -std=gnu11 -D_GNU_SOURCE -DAUDIOD_HOST_TEST -Wall -Wextra -Werror -Wno-clobbered -O1 -g -fsanitize=address,undefined \
	-I. -Iinclude userland/base/audiod/main.c userland/base/audiod/mix.c userland/base/audiod/device.c \
	userland/base/audiod/route.c -lm -o "$work/audiod"
cc -std=gnu11 -D_GNU_SOURCE -Wall -Wextra -Werror -O1 -g -I. plan/ws209/tests/audiod-route-test.c -o "$work/test"
me=$(id -u)
# The scenario.
AUDIOD_TEST_SOCKET="$work/a.sock" AUDIOD_TEST_BLUETOOTH_UID=$me AUDIOD_TEST_SEAT_UID=$me "$work/audiod" 2>"$work/a.log" &
pid=$!
sleep 0.5
status=0
timeout 60 "$work/test" "$work/a.sock" || status=$?
kill "$pid" 2>/dev/null || true
wait "$pid" 2>/dev/null || true
# The refusals.
other=$((me + 12345))
AUDIOD_TEST_SOCKET="$work/b.sock" AUDIOD_TEST_BLUETOOTH_UID=$other AUDIOD_TEST_SEAT_UID=$other "$work/audiod" 2>"$work/b.log" &
pid=$!
sleep 0.5
timeout 30 "$work/test" "$work/b.sock" denied || status=$?
kill "$pid" 2>/dev/null || true
wait "$pid" 2>/dev/null || true
exit $status
